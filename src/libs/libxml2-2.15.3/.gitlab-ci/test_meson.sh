#!/bin/sh

set -e

# compile with the following warnings:
# --warnlevel 3     : passes to the compiler -Wall -Wextra -Wpedantic
# --werror          : passes to the compiler -Werror
# --default-library : can be 'shared', 'static' or 'both'
meson  setup \
       --warnlevel 3 \
       --werror \
       --buildtype=debugoptimized \
       --default-library shared \
       -Ddocs=enabled \
       -Dhttp=enabled \
       -Dschematron=enabled \
       -Dzlib=enabled \
       -Dpython=enabled \
       builddir

ninja -C builddir

meson test --verbose -C builddir
