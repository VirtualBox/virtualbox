#!/bin/bash
# $Id: xcode-4.1-extrator.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Extracts the necessary bits from the Xcode 4.1 lion package (inside installercode_41_lion.dmg).

#
# Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#
# Make sure we're talking the same language.
#
LC_ALL=C
export LC_ALL

#
# Figure the tools/darwin.x86 location.
#
MY_DARWIN_DIR=`dirname "$0"`
MY_DARWIN_DIR=`(cd "${MY_DARWIN_DIR}" ; pwd)`
MY_DARWIN_DIR=`dirname "${MY_DARWIN_DIR}"`

#
# Constants.
#
MY_PKGS="gcc4.2.pkg llvm-gcc4.2.pkg DeveloperToolsCLI.pkg xcrun.pkg JavaSDK.pkg MacOSX10.6.pkg MacOSX10.7.pkg"
MY_LAST_PKG="MacOSX10.7.pkg"
MY_PKGS="clang.pkg"
MY_LAST_PKG="clang.pkg"
declare -a MY_FULL_PKGS
for i in $MY_PKGS;
do
    MY_FULL_PKGS[$((${#MY_FULL_PKGS[*]}))]="./Applications/Install Xcode.app/Contents/Resources/Packages/${i}"
done

#
# Parse arguments.
#
MY_TMP_DIR=/var/tmp/xcode41extractor
MY_DST_DIR="${MY_DARWIN_DIR}/xcode/v41"
MY_PKG_FILE=

my_usage()
{
    echo "usage: $0 [--tmpdir|-t <tmpdir>]  <--destination|-d> <dstdir>  <--filename|-f> <dir/InstallXcodeLion.pkg>";
    exit $1;
}

while test $# -ge 1;
do
    ARG=$1;
    shift;
    case "$ARG" in

        --tmpdir|-t)
            if test $# -eq 0; then
                echo "error: missing --tmpdir argument." 1>&2;
                exit 1;
            fi
            MY_TMP_DIR="$1";
            shift;
            ;;

        --destination|-d)
            if test $# -eq 0; then
                echo "error: missing --tmpdir argument." 1>&2;
                exit 1;
            fi
            MY_DST_DIR="$1";
            shift;
            ;;

        --filename|-f)
            if test $# -eq 0; then
                echo "error: missing --filename argument." 1>&2;
                exit 1;
            fi
            MY_PKG_FILE="$1";
            shift;
            ;;

        --h*|-h*|-?|--?)
            my_usage 0;
    esac
done

# Check the package file.
if [ -z "${MY_PKG_FILE}" ]; then
    echo "error: missing --filename <dir/InstallXcodeLion.pkg>." 1>&2l
    my_usage 1;
fi
if ! xar -tf "${MY_PKG_FILE}" > /dev/null ; then
    echo "error: xar has trouble with '${MY_PKG_FILE}'." 1>&2;
    exit 1;
fi

# Check the destination directory.
if [ -z "${MY_DST_DIR}" ]; then
    echo "error: missing --destination <dstdir>." 1>&2;
    my_usage 1;
fi
if ! mkdir -p "${MY_DST_DIR}"; then
    echo "error: error creating '${MY_DST_DIR}'." 1>&2;
    exit 1;
fi

# Check the temporary directory.
if [ -z "${MY_TMP_DIR}" ]; then
    echo "error: empty --tmpdir <tmpdir>." 1>&2;
    my_usage 1;
fi
if ! mkdir -p "${MY_TMP_DIR}/x"; then
    echo "error: error creating '${MY_TMP_DIR}/x'." 1>&2;
    exit 1;
fi

#
# Extract the "Applications/Install Xcode.app" payload, calling it MainPayload.tar.
#
if [ ! -f "${MY_TMP_DIR}/x/MainPayload.tar" ]; then
    echo "info: Extracting '${MY_PKG_FILE}'..."
    if ! xar -xvf "${MY_PKG_FILE}" -C "${MY_TMP_DIR}/x"; then
        echo "error: extraction error." 1>&2;
        exit 1;
    fi
    if ! mv -f "${MY_TMP_DIR}/x/InstallXcodeLion.pkg/Payload" "${MY_TMP_DIR}/x/MainPayload.tar"; then
        echo "error: Failed to move the package payload. Did you get the right package file?" 1>&2;
        exit 1;
    fi
fi

#
# Extract the sub-packages from MainPayload.tar.
#
if [ ! -f "${MY_TMP_DIR}/x/${MY_LAST_PKG}" ]; then
    echo "info: Extracting packages from 'MainPayload.tar'..."
    if ! tar xvf "${MY_TMP_DIR}/x/MainPayload.tar" -C "${MY_TMP_DIR}/x" "${MY_FULL_PKGS[@]}"; then
        echo "error: Failure extracting sub-packages from MainPayload.tar (see above)." 1>&2;
        exit 1;
    fi

    for i in $MY_PKGS;
    do
        if ! mv -f "${MY_TMP_DIR}/x/Applications/Install Xcode.app/Contents/Resources/Packages/${i}" "${MY_TMP_DIR}/x/${i}"; then
            echo "error: Failed to move the package ${i}." 1>&2;
            exit 1;
        fi
    done
fi

#
# Work the sub-packages, extracting their payload content into the destination directory.
#
for i in $MY_PKGS;
do
    rm -f -- "${MY_TMP_DIR}/x/Payload";
    echo "info: Extracting payload of sub-package ${i}...";
    if ! xar -xvf "${MY_TMP_DIR}/x/${i}" -C "${MY_TMP_DIR}/x" Payload; then
        echo "error: Failed to extract the payload of sub-package ${i}." 1>&2;
        exit 1;
    fi
    if ! tar xvf "${MY_TMP_DIR}/x/Payload" -C "${MY_DST_DIR}"; then
        echo "error: Failed to extract the payload content of sub-package ${i}." 1>&2;
        exit 1;
    fi
done

#
# Clean up.
#
echo "info: Successfully extracted. Cleaning up temporary files..."
rm -Rf -- "${MY_TMP_DIR}/x/"
rmdir -- "${MY_TMP_DIR}"

