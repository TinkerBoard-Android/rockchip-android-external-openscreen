# Open Screen Library

This library implements the Open Screen Protocol.  Information about the
protocol can be found in the Open Screen [GitHub
repository](https://github.com/webscreens/openscreenprotocol).

## Build dependencies

Although we plan to address this in the future, there are currently some build
dependencies that you will need to provide yourself.

 - `gcc`
 - `gn`

   If you have a Chromium checkout, you can use `//buildtools/<system>/gn`.
   Otherwise, on 64-bit Linux you may use `//tools/install-build-tools.sh` to
   obtain a copy of `gn`.
 - `clang-format`

   `clang-format` may be provided by your OS, found under a Chromium checkout at
   `//buildtools/<system>/clang-format`, or downloaded in a similar manner to
   `gn` via `//tools/install-build-tools.sh`.
 - `ninja`

   [GitHub releases](https://github.com/ninja-build/ninja/releases)

## Building an example with GN and Ninja

After checking out the Open Screen library, make sure to initialize the
submodules for the dependencies.  The following commands will checkout all the
necessary submodules:

``` bash
  git submodule init
  git submodule update
```

The following commands will build the current example executable and run it.

``` bash
  gn gen out/Default    # Creates the build directory and necessary ninja files
  ninja -C out/Default  # Builds the executable with ninja
  out/Default/hello     # Runs the executable
```

The `-C` argument to `ninja` works just like it does for GNU Make: it specifies
the working directory for the build.  So the same could be done as follows:

``` bash
  gn gen out/Default
  cd out/Default
  ninja
  ./hello
```

After editing a file, only `ninja` needs to be rerun, not `gn`.

## Gerrit

The following sections contain some tips about dealing with Gerrit for code
reviews, specifically when pushing patches for review.

### Uploading a new change

There is official [Gerrit
documentation](https://gerrit-documentation.storage.googleapis.com/Documentation/2.14.7/user-upload.html#push_create)
for this which essentially amounts to:

``` bash
  git push origin HEAD:refs/for/master
```

You may also wish to install the [Change-Id commit-msg
hook](https://gerrit-documentation.storage.googleapis.com/Documentation/2.14.7/cmd-hook-commit-msg.html).
This adds a Change-Id line to each commit message locally, which Gerrit uses to
track changes.  Once installed, this can be toggled with `git config
gerrit.createChangeId <true|false>`.

The important thing to note, however, is that each commit in
`origin/master..HEAD` will get its own Gerrit change.  If you only want one
change for your current branch, which has multiple commits, you will need to
squash them with `git rebase -i`.  If you wish to upload a new patchset to the
same change later, you can use `git commit --amend` to preserve the Change-Id.

### Adding a new patchset to an existing change

Gerrit keeps track of changes using a
[Change-Id
line](https://gerrit-documentation.storage.googleapis.com/Documentation/2.14.7/user-changeid.html)
in each commit.  When there is no Change-Id line, Gerrit creates a new Change-Id
for the commit, and therefore a new change.  Gerrit's documentation for
[replacing a
change](https://gerrit-documentation.storage.googleapis.com/Documentation/2.14.7/user-upload.html#push_replace)
describes this.  So if you want to upload a new patchset to an existing review,
it should contain the appropriate Change-Id line in the commit message.  Of
course, the same note above about commits and changes being 1:1 still applies
here.
