#!/bin/sh

set -e

mkdir -p libxml2-dist
cd libxml2-dist
sh ../autogen.sh
make distcheck V=1 DISTCHECK_CONFIGURE_FLAGS='--with-legacy'
if [ -z "$CI_COMMIT_TAG" ]; then
    mv libxml2-*.tar.xz libxml2-git-$CI_COMMIT_SHORT_SHA.tar.xz
fi
