# Maintainer's Guide

## Working with the test suite

Most of the tests are contained in the `runtest` executable which
generally reads test cases from the `test` directory and compares output
to files in the `result` directory.

You can simply add new test cases and run `runtest -u` to update the
results. If you debug test failures, it's also useful to execute
`runtest -u` and then `git diff result` to get a diff between actual and
expected results. You can restore the original results by running
`git restore result` and `git clean -xd result`.

## Generated files

Some source code is generated with Python scripts in the `tools`
directory.

- `tools/genChRanges.py` generates code to handle character ranges
  from chvalid.def:
  - `chvalid.c`
  - `include/libxml/chvalid.h`

- `tools/genEscape prints lookup tables for serialization.

- `tools/genHtml5LibTests.py` creates test cases and expected results
  from the html5lib test suite:
  - `test/html-tokenizer`
  - `result/html-tokenizer`

- `tools/genHtmlEnt.py` prints lookup tables for HTML5 named character
  references (predefined entities):
  - `html5ent.inc`

- `tools/gentest.py` generates test code using the Doxygen XML output:
  - `testapi.c`

- `tools/genUnicode.py` generates code to handle Unicode ranges
  from Unicode data files:
  - `xmlunicode.c`

## Making a release

### Update the NEWS file

You can get started by running

    git log --format='- %s (%an)' [previous-release-tag]..

### Bump the version number

Update the version number in `VERSION` if you haven't done so already.

### Commit and verify tarball

Release tarballs are generated with a CI job and the `.gitlab-ci/dist.sh`
script. Push the release commit and inspect the tarball artifact generated
by Gitlab CI.

### Tag the release

Create an annotated tag and push it:

    git tag -a [version] -m 'Release [version]'
    git push origin [version]

This will upload the release to the downloads server using the GNOME
Release Service. For more details, see
<https://handbook.gnome.org/maintainers/release-pipeline.html>

### Create a GitLab release

Create or update a GitLab release on
<https://gitlab.gnome.org/GNOME/libxml2/-/releases>.

### Announce the release

Announce the release on https://discourse.gnome.org under topics 'libxml2'
and 'announcements'.

## Breaking the ABI

Unfortunately, libxml2 exposes many internal structs which makes some
beneficial changes impossible without breaking the ABI.

The following changes are allowed (after careful consideration):

- Appending members to structs which client code should never allocate
  directly. A notable example is xmlParserCtxt. Other structs like
  xmlError are allocated directly by client code and must not be changed.

- Making a void function return a value.

- Making functions accept const pointers unless it's a typedef for a
  callback.

- Changing signedness of struct members or function arguments.

## Updating the CI Docker image

Note that the CI image is used for libxslt as well. First create a
GitLab access token with maintainer role and `read_registry` and
`write_registry` permissions. Then run the following commands with the
Dockerfile in the .gitlab-ci directory:

    docker login -u <username> -p <access_token> \
        registry.gitlab.gnome.org
    docker build -t registry.gitlab.gnome.org/gnome/libxml2 - \
        < .gitlab-ci/Dockerfile
    docker push registry.gitlab.gnome.org/gnome/libxml2

