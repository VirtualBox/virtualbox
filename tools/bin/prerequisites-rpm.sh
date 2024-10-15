#!/bin/sh -e
# @file
## $Id: prerequisites-rpm.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# Fetches prerequisites for RPM based GNU/Linux systems.
#

#
# Copyright (C) 2018-2024 Oracle and/or its affiliates.
#
# This file is part of VirtualBox base platform packages, as
# available from https://www.virtualbox.org.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, in version 3 of the
# License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses>.
#
# SPDX-License-Identifier: GPL-3.0-only
#

# What this script does:
usage_msg="\
Usage: `basename ${0}` [--with-docs]

Install the dependencies needed for building VirtualBox on an RPM-based Linux
system.  Additional distributions will be added as needed.  There are no plans
to add support for or to accept patches for distributions we do not package.
The \`--with-docs\' parameter is to install the packages needed for building
documentation.  It will also be implemented per distribution as needed."

# To repeat: there are no plans to add support for or to accept patches
# for distributions we do not package.

usage()
{
    echo "${usage_msg}"
    exit "${1}"
}

unset WITHDOCS

while test -n "${1}"; do
    case "${1}" in
    --with-docs)
        WITHDOCS=1
        shift ;;
    -h|--help)
        usage 0 ;;
    *)
        echo "Unknown parameter ${1}" >&2
        usage 1 ;;
    esac
done

export LC_ALL=C
PATH=/sbin:/usr/sbin:$PATH

# This list is valid for openSUSE 15.0
PACKAGES_OPENSUSE="\
bzip2 gcc-c++ glibc-devel gzip libcap-devel libcurl-devel libidl-devel \
libxslt-devel libvpx-devel libXmu-devel make libopenssl-devel  zlib-devel \
pam-devel libpulse-devel python-devel rpm-build libSDL_ttf-devel \
device-mapper-devel wget kernel-default-devel tar glibc-devel-32bit \
libstdc++-devel-32bit libpng16-devel libqt5-qtx11extras-devel \
libXcursor-devel libXinerama-devel libXrandr-devel alsa-devel gcc-c++-32bit \
libQt5Widgets-devel libQt5OpenGL-devel libQt5PrintSupport-devel \
libqt5-linguist libopus-devel"

if test -f /etc/SUSE-brand; then
    zypper install -y ${PACKAGES_OPENSUSE}
    exit 0
fi

PACKAGES_EL="bzip2 gcc-c++ glibc-devel gzip libcap-devel libIDL-devel \
    libxslt-devel libXmu-devel make openssl-devel pam-devel python-devel \
    rpm-build wget kernel kernel-devel tar libpng-devel libXcursor-devel \
    libXinerama-devel libXrandr-devel which"
PACKAGES_EL5="curl-devel SDL-devel libstdc++-devel.i386 openssh-clients \
    which gcc44-c++"
PACKAGES_EPEL5_ARCH="/usr/bin/python2.6:python26-2.6.8-2.el5 \
    /usr/bin/python2.6:python26-libs-2.6.8-2.el5 \
    /usr/bin/python2.6:python26-devel-2.6.8-2.el5 \
    /usr/bin/python2.6:libffi-3.0.5-1.el5 \
    /usr/share/doc/SDL_ttf-2.0.8/README:SDL_ttf-2.0.8-3.el5 \
    /usr/share/doc/SDL_ttf-2.0.8/README:SDL_ttf-devel-2.0.8-3.el5"
LOCAL_EL5="\
    /usr/local/include/pulse:\
https://freedesktop.org/software/pulseaudio/releases/pulseaudio-11.1.tar.gz"
PACKAGES_EL6_PLUS="libcurl-devel libstdc++-static libvpx-devel \
    pulseaudio-libs-devel SDL-static device-mapper-devel glibc-static \
    zlib-static glibc-devel.i686 libstdc++.i686 qt5-qttools-devel \
    qt5-qtx11extras-devel"
PACKAGES_EL7_PLUS="opus-devel"
PACKAGE_LIBNSL_X86="libnsl.i686"
DOCS_EL="texlive-latex texlive-latex-bin texlive-ec texlive-pdftex-def \
    texlive-fancybox"

if test -f /etc/redhat-release; then
    read elrelease < /etc/redhat-release
    case "${elrelease}" in
    *"release 5."*|*"release 6."*|*"release 7."*)
        INSTALL="yum install -y" ;;
    *)
        INSTALL="dnf install -y" ;;
    esac
    case "`uname -m`" in
    x86_64) ARCH=x86_64 ;;
    *) ARCH=i386 ;;
    esac
    egrepignore=\
"Setting up Install Process|already installed and latest version\
|Nothing to do"
    ${INSTALL} ${PACKAGES_EL} | egrep -v  "${egrepignore}"
    case "${elrelease}" in
    *"release 5."*)
        # Packages missing in EL5
        ${INSTALL} ${PACKAGES_EL5} | egrep -v  "${egrepignore}"
        for i in ${PACKAGES_EPEL5_ARCH}; do
            if test ! -r "${i%%:*}"; then
                wget "http://archives.fedoraproject.org/pub/archive/epel/5/\
${ARCH}/${i#*:}.${ARCH}.rpm" -P /tmp
                rpm -i "/tmp/${i#*:}.${ARCH}.rpm"
                rm "/tmp/${i#*:}.${ARCH}.rpm"
            fi
        done
        for i in ${LOCAL_EL5}; do
            if test ! -r "${i%%:*}"; then
                {
                    ARCHIVE="${i#*:}"
                    TMPNAME=`/tmp/date +'%s'`
                    mkdir -p "${TMPNAME}"
                    cd "${TMPNAME}"
                    wget "${ARCHIVE}"
                    case "${ARCHIVE}" in
                    *.tar.gz)
                        tar xzf "${ARCHIVE}" --strip-components 1 ;;
                    *)
                        echo Error: unknown archive type "${ARCHIVE}"
                        exit 1
                    esac
                    ./configure
                    make
                    make install
                    cd /tmp
                    rm -r "${TMPNAME}"
                }
            fi
        done ;;
    *)
        ${INSTALL} ${PACKAGES_EL6_PLUS} | egrep -v  "${egrepignore}"
        case "${elrelease}" in
        *"release 6."*) ;;
        *)
            ${INSTALL} ${PACKAGES_EL7_PLUS} | egrep -v  "${egrepignore}"
            test -n "${WITHDOCS}" &&
                ${INSTALL} ${DOCS_EL} | egrep -v  "${egrepignore}"
        esac
    esac
    test -e /usr/lib/libnsl.so.1 ||
        ${INSTALL} ${PACKAGE_LIBNSL_X86} | egrep -v  "${egrepignore}" || true
fi
