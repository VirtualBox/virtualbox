#!/bin/sh

set -e

fuzzer="$1"

if [ -z "$fuzzer" ]; then
    echo usage: $0 fuzzer
    exit 1
fi

cd fuzz
make $fuzzer

if [ ! -e corpus/${fuzzer}_ ]; then
    mv corpus/$fuzzer corpus/${fuzzer}_
fi

mkdir -p corpus/$fuzzer
./$fuzzer -merge=1 -use_value_profile=1 corpus/$fuzzer corpus/${fuzzer}_
rm -rf corpus/${fuzzer}_
