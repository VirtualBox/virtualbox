#!/bin/sh
# @file
## $Id: prerequisites-deb.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# Fetches prerequisites for Debian based GNU/Linux systems.
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

Install the dependencies needed for building VirtualBox on an deb-based Linux
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
read DEBVER < /etc/debian_version

apt-get update
# We deal with different distributions having different lists of prerequisites
# by splitting them into several apt commands.  Some will fail on some
# distributions, but at the end everything needed should be there.
apt-get install -y chrpath g++ make wget iasl libidl-dev libsdl1.2-dev \
    libsdl-ttf2.0-dev libpam0g-dev libssl-dev libpulse-dev \
    libasound2-dev xsltproc libxml2-dev libxml2-utils unzip \
    libxrandr-dev libxinerama-dev libcap-dev python-dev \
    libxmu-dev libxcursor-dev libcurl4-openssl-dev libdevmapper-dev \
    libvpx-dev g++-multilib libopus-dev || true
# 32-bits libs for 64-bit installs.
case `uname -m` in
   x86_64|amd64|AMD64)
       apt-get install -y libc6-dev-i386 lib32gcc1 lib32stdc++6 lib32z1-dev || true
       ;;
esac
# Only install Qt5 on recent distributions
case "${DEBVER}" in
7*|8*|jessie*|stretch*) ;;
*)
    apt-get install -y qttools5-dev-tools libqt5opengl5-dev \
        libqt5x11extras5-dev || true ;;
esac
test -n "${WITHDOCS}" &&
    apt-get install -y doxygen texlive texlive-latex-extra texlive-fonts-extra
# Ubuntu only
grep Ubuntu /etc/lsb-release 2>/dev/null >&2 &&
    apt-get install -y linux-headers-generic
# apt-get install wine linux-headers-`uname -r`  # Not for chroot installs.
