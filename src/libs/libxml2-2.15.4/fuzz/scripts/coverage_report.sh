#!/bin/sh

set -e

fuzzer="$1"

if [ -z "$fuzzer" ]; then
    echo usage: $0 fuzzer
    exit 1
fi

# Rebuild the project with coverage enabled

make distclean
export CC=clang
export CXX=clang++
export CFLAGS=" \
    -O1 -gline-tables-only \
    -fsanitize=fuzzer-no-link \
    -fprofile-instr-generate -fcoverage-mapping \
    -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION"
sh autogen.sh --without-python
make -j5
rm default.profraw

# Process corpus once

cd fuzz
make $fuzzer

./$fuzzer -runs=1 corpus/$fuzzer

# Generate HTML report

llvm-profdata merge default.profraw -o default.profdata
llvm-cov show -format=html -output-dir=report \
    -instr-profile default.profdata \
    ../.libs/libxml2.so
rm default.profraw default.profdata
