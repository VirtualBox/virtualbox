#!/bin/bash -eu

# OSS-Fuzz integration, see
# https://github.com/google/oss-fuzz/tree/master/projects/libxml2

# Add extra UBSan checks
if [ "$SANITIZER" = undefined ]; then
    extra_checks="integer,float-divide-by-zero"
    extra_cflags="-fsanitize=$extra_checks -fno-sanitize-recover=$extra_checks"
    export CFLAGS="$CFLAGS $extra_cflags"
    export CXXFLAGS="$CXXFLAGS $extra_cflags"
fi

# Don't enable zlib with MSan
if [ "$SANITIZER" = memory ]; then
    CONFIG=''
else
    CONFIG='--with-zlib'
fi

# Workaround for a LeakSanitizer crashes,
# see https://github.com/google/oss-fuzz/issues/11798.
if [ "$ARCHITECTURE" = "aarch64" ]; then
    export ASAN_OPTIONS=detect_leaks=0
fi

export V=1

./autogen.sh \
    --disable-shared \
    --without-debug \
    --without-http \
    --without-python \
    $CONFIG
make -j$(nproc)

cd fuzz
make clean-corpus
make fuzz.o

for fuzzer in \
    api html lint reader regexp schema uri valid xinclude xml xpath
do
    OBJS="$fuzzer.o"
    if [ "$fuzzer" = lint ]; then
        OBJS="$OBJS ../xmllint.o ../shell.o"
    fi
    make $OBJS
    # Link with $CXX
    $CXX $CXXFLAGS \
        $OBJS fuzz.o \
        -o $OUT/$fuzzer \
        $LIB_FUZZING_ENGINE \
        ../.libs/libxml2.a -Wl,-Bstatic -lz -Wl,-Bdynamic

    if [ $fuzzer != api ]; then
        [ -e seed/$fuzzer ] || make seed/$fuzzer.stamp
        zip -j $OUT/${fuzzer}_seed_corpus.zip seed/$fuzzer/*
    fi
done

cp *.dict *.options $OUT/
