#!/usr/bin/env bash

set -e

export LIBXML_DIR=$(pwd)
export MAKEFLAGS=-j$(nproc)
export NOCONFIGURE=1
./autogen.sh

git clone https://github.com/sparklemotion/nokogiri
cd nokogiri
gem install bundler:2.7.2
bundle _2.7.2_ install
bundle exec rake compile -- --with-xml2-source-dir=${LIBXML_DIR} --disable-xml2-legacy
bundle exec rake test
