// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/simple_fraction.h"

#include <stdlib.h>

#include <cmath>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace openscreen {
namespace {

constexpr char kDelimiter[] = "/";

// The Linux implementation of strtol is overly lenient on parsing strings, e.g.
// the string "not a number" is a valid number=0. We wrap it here to avoid
// complicated checking in usage.
ErrorOr<int> StringToLong(const std::string& str) {
  if (str.empty()) {
    return Error::Code::kParameterInvalid;
  }

  char* end_pointer;
  errno = 0;
  const int output = strtol(str.data(), &end_pointer, 10);
  if (*end_pointer != '\0' || errno != 0) {
    return Error::Code::kParameterInvalid;
  }
  return output;
}
}  // namespace

// static
ErrorOr<SimpleFraction> SimpleFraction::FromString(const std::string& value) {
  // Zeroth case: empty string.
  if (value.empty()) {
    return Error::Code::kParameterInvalid;
  }

  std::size_t delimiter_pos = value.find(kDelimiter);
  // First case: simple number, not a fraction.
  if (delimiter_pos == std::string::npos) {
    ErrorOr<int> numerator = StringToLong(value);
    if (numerator.is_error()) {
      return std::move(numerator.error());
    }
    return SimpleFraction{numerator.value(), 1};
  }
  // Second case: proper fraction.
  const std::string first_field = value.substr(0, delimiter_pos);
  const std::string second_field = value.substr(delimiter_pos + 1);
  ErrorOr<int> numerator = StringToLong(first_field);
  ErrorOr<int> denominator = StringToLong(second_field);
  if (numerator.is_error() || denominator.is_error()) {
    return Error::Code::kParameterInvalid;
  }
  return SimpleFraction{numerator.value(), denominator.value()};
}

std::string SimpleFraction::ToString() const {
  if (denominator == 1) {
    return std::to_string(numerator);
  }
  std::ostringstream ss;
  ss << numerator << kDelimiter << denominator;
  return ss.str();
}

bool SimpleFraction::operator==(const SimpleFraction& other) const {
  return numerator == other.numerator && denominator == other.denominator;
}

bool SimpleFraction::operator!=(const SimpleFraction& other) const {
  return !(*this == other);
}

bool SimpleFraction::is_defined() const {
  return denominator != 0;
}

bool SimpleFraction::is_positive() const {
  return is_defined() && (numerator >= 0) && (denominator > 0);
}

SimpleFraction::operator double() const {
  if (denominator == 0) {
    return nan("");
  }
  return static_cast<double>(numerator) / static_cast<double>(denominator);
}

}  // namespace openscreen
