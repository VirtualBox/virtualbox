#!/bin/sh

set -e

srcdir=$(pwd)
installdir="$srcdir/install"
export PKG_CONFIG_PATH="$installdir/lib/pkgconfig"

git clone --depth 1 https://github.com/php/php-src.git
cd php-src
./buildconf
./configure --with-xsl --enable-soap --enable-debug
make -j$(nproc)
make TESTS=" \
    -g FAIL \
    --show-diff \
    --no-progress \
    ext/dom \
    ext/libxml \
    ext/simplexml \
    ext/soap \
    ext/xml \
    ext/xmlreader \
    ext/xmlwriter \
    ext/xsl \
" test
