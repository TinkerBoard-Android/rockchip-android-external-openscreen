// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DISCOVERY_DNSSD_IMPL_DNS_DATA_GRAPH_H_
#define DISCOVERY_DNSSD_IMPL_DNS_DATA_GRAPH_H_

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "absl/types/optional.h"
#include "discovery/dnssd/impl/constants.h"
#include "discovery/dnssd/public/dns_sd_instance_endpoint.h"
#include "discovery/mdns/mdns_record_changed_callback.h"
#include "discovery/mdns/mdns_records.h"

namespace openscreen {
namespace discovery {

/*
 Per RFC 6763, the following mappings exist between the domains of the called
 out mDNS records:

                     --------------
                    | PTR Record |
                    --------------
                          /\
                         /  \
                        /    \
                       /      \
                      /        \
            -------------- --------------
            | SRV Record | | TXT Record |
            -------------- --------------
                  /\
                 /  \
                /    \
               /      \
              /        \
             /          \
    -------------- ---------------
    |  A Record  | | AAAA Record |
    -------------- ---------------

 Such that PTR records point to the domain of SRV and TXT records, and SRV
 records point to the domain of A and AAAA records. Below, these 3 separate
 sets are referred to as "Domain Groups".

 Though it is frequently the case that each A or AAAA record will only be
 pointed to by one SRV record domain, this is not a requirement for DNS-SD and
 in the wild this case does come up. On the other hand, it is expected that
 each PTR record domain will point to multiple SRV records.

 To represent this data, a multigraph structure has been used.
 - Each node of the graph represents a specific domain name
 - Each edge represents a parent-child relationship, such that node A is a
   parent of node B iff there exists some record x in A such that x points to
   the domain represented by B.
 In practice, it is expected that no more than one edge will ever exist
 between two nodes. A multigraph is used despite this to simplify the code and
 avoid a number of tricky edge cases (both literally and figuratively).

 Note the following:
 - This definition allows for cycles in the multigraph (which are unexpected
   but allowed by the RFC).
 - This definition allows for self loops (which are expected when a SRV record
   points to address records with the same domain).
 - The memory requirement for this graph is bounded due to a bound on the
   number of tracked records in the mDNS layer as part of
   discovery/mdns/mdns_querier.h.
*/
class DnsDataGraph {
 public:
  using DomainChangeCallback = std::function<void(const DomainName&)>;

  // The set of valid groups of domains, as called out in the hierarchy
  // described above.
  enum class DomainGroup { kNone = 0, kPtr, kSrvAndTxt, kAddress };

  // Get the domain group associated with the provided object.
  static DomainGroup GetDomainGroup(DnsType type);
  static DomainGroup GetDomainGroup(const MdnsRecord record);

  explicit DnsDataGraph(NetworkInterfaceIndex network_interface);
  ~DnsDataGraph();

  // Manually starts or stops tracking the provided domain. These methods should
  // only be called for top-level PTR domains.
  void StartTracking(const DomainName& domain,
                     DomainChangeCallback on_start_tracking);
  void StopTracking(const DomainName& domain,
                    DomainChangeCallback on_stop_tracking);

  // Attempts to create all DnsSdInstanceEndpoint objects with |name| associated
  // with the provided |domain_group|. If all required data for one such
  // endpoint has been received, and an error occurs while parsing this data,
  // then an error is returned in place of that endpoint.
  std::vector<ErrorOr<DnsSdInstanceEndpoint>> CreateEndpoints(
      DomainGroup domain_group,
      const DomainName& name) const;

  // Modifies this entity with the provided DnsRecord. If called with a valid
  // record type, the provided change will only be applied if the provided event
  // is valid at the time of calling. The returned result will be an error if
  // the change does not make sense from our current data state, and
  // Error::None() otherwise. Valid record types with which this method can be
  // called are PTR, SRV, TXT, A, and AAAA record types.
  //
  // TODO(issuetracker.google.com/157822423): Allow for duplicate records of
  // non-PTR types.
  Error ApplyDataRecordChange(MdnsRecord record,
                              RecordChangedEvent event,
                              DomainChangeCallback on_start_tracking,
                              DomainChangeCallback on_stop_tracking);

  size_t tracked_domain_count() const { return nodes_.size(); }

 private:
  friend class DnsDataGraphTests;

  // A single node of the graph represented by this type.
  class Node {
   public:
    Node(DomainName name, DnsDataGraph* graph);
    ~Node();

    // Applies a record change for this node.
    Error ApplyDataRecordChange(MdnsRecord record, RecordChangedEvent event);

    // Returns the first rdata of a record with type matching |type| in this
    // node's |records_|, or absl::nullopt if no such record exists.
    template <typename T>
    absl::optional<T> GetRdata(DnsType type) {
      auto it = FindRecord(type);
      if (it == records_.end()) {
        return absl::nullopt;
      } else {
        return std::cref(absl::get<T>(it->rdata()));
      }
    }

    const DomainName& name() const { return name_; }
    const std::vector<Node*>& parents() const { return parents_; }
    const std::vector<Node*>& children() const { return children_; }
    const std::vector<MdnsRecord>& records() const { return records_; }

   private:
    // Adds or removes an edge in |graph_|.
    // NOTE: The same edge may be added multiple times, and one call to remove
    // is needed for every such call.
    void AddChild(Node* child);
    void RemoveChild(Node* child);

    // Applies the specified change to domain |child| for this node.
    void ApplyChildChange(DomainName child_name, RecordChangedEvent event);

    // Finds an iterator to the record of the provided type, or to
    // records_.end() if no such record exists.
    std::vector<MdnsRecord>::iterator FindRecord(DnsType type);

    // The domain with which the data records stored in this node are
    // associated.
    const DomainName name_;

    // Currently extant mDNS Records at |name_|.
    std::vector<MdnsRecord> records_;

    // Nodes which contain records pointing to this node's |name|.
    std::vector<Node*> parents_;

    // Nodes containing records pointed to by the records in this node.
    std::vector<Node*> children_;

    // Graph containing this node.
    DnsDataGraph* graph_;
  };

  // Determines whether the provided node has the necessary records to be a
  // valid node at the specified domain level.
  static bool IsValidAddressNode(Node* node);
  static bool IsValidSrvAndTxtNode(Node* node);

  // Calculates the set of DnsSdInstanceEndpoints associated with the PTR
  // records present at the given |node|.
  std::vector<ErrorOr<DnsSdInstanceEndpoint>> CalculatePtrRecordEndpoints(
      Node* node) const;

  // Denotes whether the dtor for this instance has been called. This is
  // required for validation of Node instance functionality. See the
  // implementation of DnsDataGraph::Node::~Node() for more details.
  bool is_dtor_running_ = false;

  // Map from domain name to the node containing all records associated with the
  // name.
  std::map<DomainName, std::unique_ptr<Node>> nodes_;

  const NetworkInterfaceIndex network_interface_;

  // The methods to be called when a domain name either starts or stops being
  // referenced. These will only be set when a record change is ongoing, and act
  // as a single source of truth for the creation and deletion callbacks that
  // should be used during that operation.
  DomainChangeCallback on_node_creation_;
  DomainChangeCallback on_node_deletion_;
};

}  // namespace discovery
}  // namespace openscreen

#endif  // DISCOVERY_DNSSD_IMPL_DNS_DATA_GRAPH_H_
