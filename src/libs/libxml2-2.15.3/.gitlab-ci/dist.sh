#!/bin/sh

set -e

mkdir -p libxml2-dist
cd libxml2-dist
sh ../autogen.sh --with-docs --with-legacy
VERSION=$(cat libxml-2.0.pc | grep Version | cut -d" " -f2)

# Build doc into the dist: https://gitlab.gnome.org/GNOME/libxml2/-/issues/1048
cd doc
make
cd ..

make distcheck V=1 DISTCHECK_CONFIGURE_FLAGS='--with-docs --with-legacy'

tar xJvf libxml2-$VERSION.tar.xz
cp -rf doc libxml2-$VERSION/dist-doc
tar cJvf libxml2-$VERSION.tar.xz libxml2-$VERSION

if [ -z "$CI_COMMIT_TAG" ]; then
    mv libxml2-$VERSION.tar.xz libxml2-git-$CI_COMMIT_SHORT_SHA.tar.xz
fi
