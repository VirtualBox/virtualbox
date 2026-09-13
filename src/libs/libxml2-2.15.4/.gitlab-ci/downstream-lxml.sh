#!/bin/sh

set -e

srcdir=$(pwd)
installdir="$srcdir/install"
export PKG_CONFIG_PATH="$installdir/lib/pkgconfig"

git clone --depth 1 https://github.com/lxml/lxml.git
cd lxml
make
LD_LIBRARY_PATH="$installdir/lib" make TESTFLAGS='' test
