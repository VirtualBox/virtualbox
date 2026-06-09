#!/bin/sh

set -e

srcdir=$(pwd)

mkdir -p install
installdir="$srcdir/install"
export PKG_CONFIG_PATH="$installdir/lib/pkgconfig"

sh autogen.sh "--prefix=$installdir" --with-docs --with-schematron --with-zlib
make -j$(nproc)
make install

# Make system XML catalog available
ln -s /etc install/etc

git clone --depth 1 https://gitlab.gnome.org/GNOME/libxslt.git
cd libxslt
sh autogen.sh \
    "--prefix=$installdir" \
    --without-python
make -j$(nproc)
make install
