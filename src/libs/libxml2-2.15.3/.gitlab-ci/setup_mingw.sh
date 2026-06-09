#!/bin/sh

pacman --noconfirm -Syu

prefix=
if [ -n "$MINGW_PACKAGE_PREFIX" ]; then
    prefix="${MINGW_PACKAGE_PREFIX}-"
fi
for module in docbook-xsl doxygen libiconv python xsltproc xz zlib "$@"; do
    pacman --noconfirm -S --needed ${prefix}$module
done

mkdir -p libxml2-build

if [ ! -e xmlconf ]; then
    wget https://www.w3.org/XML/Test/xmlts20080827.tar -O - |
       tar -x
fi
