# The image is also used for libxslt.
#
# package           required for
# ------------------------------------------------------------
# libclang-rt-dev   sanitizer runtimes
# llvm              llvm-symbolizer (for sanitizer backtraces)
# git               libxslt, downstream projects
# libgcrypt-dev     libxslt
# xz-utils          make dist
# doxygen           documentation
# xsltproc          documentation
# docbook-xsl       documentation

FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get install -y --no-install-recommends \
        curl git ca-certificates \
        autoconf automake libtool pkg-config \
        make gcc clang llvm libclang-rt-dev \
        zlib1g-dev libgcrypt-dev \
        python3-dev \
        cmake meson \
        xz-utils \
        doxygen xsltproc docbook-xsl
WORKDIR /tests
RUN curl https://www.w3.org/XML/Test/xmlts20080827.tar.gz |tar xz

# XML::LibXML uses Alien::Libxml2 which has a huge dependency chain.
# We try to install most dependencies with apt. We also require
# libxml2-dev to stop Alien::Libxml2 from downloading and building
# libxml2 on its own.
RUN apt-get install -y --no-install-recommends \
        libperl-dev libxml2-dev cpanminus \
        libalien-build-perl \
        libio-socket-ssl-perl \
        libsort-versions-perl \
        liburi-perl \
        libxml-namespacesupport-perl  \
        libxml-sax-perl \
        libyaml-perl
RUN cpanm -n Alien::Libxml2
RUN apt-get remove -y libxml2-dev

# PHP
RUN apt-get install -y --no-install-recommends \
        bison re2c libsqlite3-dev

# lxml
RUN apt-get install -y --no-install-recommends \
        cython3

# xmlstarlet
RUN apt-get install -y --no-install-recommends \
        docbook-xsl-ns
