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
       builddir

ninja -C builddir test

