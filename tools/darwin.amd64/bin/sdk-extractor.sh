#!/bin/bash
# $Id: sdk-extractor.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Extracts the SDKs from a commandline tools DMG.
#

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
#MY_PKGS="gcc4.2.pkg llvm-gcc4.2.pkg DeveloperToolsCLI.pkg xcrun.pkg JavaSDK.pkg MacOSX10.6.pkg MacOSX10.7.pkg"
#MY_LAST_PKG="MacOSX10.7.pkg"
#MY_PKGS="clang.pkg"
#MY_LAST_PKG="clang.pkg"
#declare -a MY_FULL_PKGS
#for i in $MY_PKGS;
#do
#    MY_FULL_PKGS[$((${#MY_FULL_PKGS[*]}))]="./Applications/Install Xcode.app/Contents/Resources/Packages/${i}"
#done

#
# Parse arguments.
#
MY_TMP_DIR=/var/tmp/sdkextractor
MY_DST_DIR="${MY_DARWIN_DIR}/sdk/incoming"
MY_DMG_FILE=
MY_PKG_FILE=
MY_DRY_RUN=

my_usage()
{
    echo "usage: $0 [--dry-run] [--tmpdir|-t <tmpdir>]  <--destination|-d> <dstdir>  <--filename|-f> <dir/Command_Line_Tools*.dmg|pkg>";
    echo ""
    echo "Works for command lines tools for Xcode 8.2 and later."
    echo "For older versions, the SDK must be extraced from Xcode itself, it seems."
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
            case "$1" in
                *.[dD][mM][gG])
                    MY_DMG_FILE="$1";
                    MY_PKG_FILE=;
                    ;;
                *.[pP][kK][gG])
                    MY_PKG_FILE="$1";
                    MY_DMG_FILE=;
                    ;;
                *)
                    echo "error: filename does not end with .dmg or .pkg." 1>&2;
                    exit 1;
                    ;;
            esac
            shift;
            ;;

        --dry-run)
            MY_DRY_RUN=1;
            ;;

        --h*|-h*|-?|--?)
            my_usage 0;
    esac
done

# We must have something to work with.
if [ -z "${MY_PKG_FILE}" -a -z "${MY_DMG_FILE}" ]; then
    echo "error: missing --filename <dir/Command_Line_Tools*.dmg|pkg>." 1>&2l
    my_usage 1;
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
MY_TMP_DIR_X="${MY_TMP_DIR}/x$$";
if ! mkdir -p "${MY_TMP_DIR_X}"; then
    echo "error: error creating '${MY_TMP_DIR_X}'." 1>&2;
    exit 1;
fi
MY_TMP_DIR_Y="${MY_TMP_DIR}/y$$";
if ! mkdir -p "${MY_TMP_DIR_Y}"; then
    echo "error: error creating '${MY_TMP_DIR_Y}'." 1>&2;
    exit 1;
fi

# Attach the DMG if one is given, then find the PKG file inside it.
MY_TMP_DIR_DMG="${MY_TMP_DIR}/dmg";
rmdir -- "${MY_TMP_DIR_DMG}" 2>/dev/null;
if [ -d "${MY_TMP_DIR_DMG}" ]; then
    echo "info: Unmount '${MY_TMP_DIR_DMG}'...";
    hdiutil detach -force "${MY_TMP_DIR_DMG}";
    if ! rmdir -- "${MY_TMP_DIR_DMG}"; then
        echo "error: failed to detach DMG from previous run (mountpoint='${MY_TMP_DIR_DMG}')." 1>&2;
        exit 1;
    fi
fi
if [ -n "${MY_DMG_FILE}" ]; then
    echo "info: Mounting '${MY_DMG_FILE}' at '${MY_TMP_DIR_DMG}'...";
    if ! mkdir -p -- "${MY_TMP_DIR_DMG}"; then
        echo "error: error creating '${MY_TMP_DIR_DMG}'." 1>&2;
        exit 1;
    fi

    if ! hdiutil attach -mountpoint "${MY_TMP_DIR_DMG}" -noautoopen -nobrowse -noverify "${MY_DMG_FILE}"; then
        echo "error: hdiutil attach failed for '${MY_DMG_FILE}'" 1>&2;
        exit 1;
    fi
    for x in "${MY_TMP_DIR_DMG}/"*.pkg;
    do
        if [ -e "${x}" ]; then
            MY_PKG_FILE=$x;
        fi
    done
    if [ -z "${MY_PKG_FILE}" ]; then
        echo "error: Found no .pkg file inside the DMG attached at '${MY_TMP_DIR_DMG}'." 1>&2;
        exit 1;
    fi
    echo "info: MY_PKG_FILE=${MY_PKG_FILE}";
fi

# Check the package file.
echo "info: Checking '${MY_PKG_FILE}'...";
if ! xar -tf "${MY_PKG_FILE}" > /dev/null ; then
    echo "error: xar has trouble with '${MY_PKG_FILE}'." 1>&2;
    exit 1;
fi

#
# Find the SDK packages and extract them to the 'x' directory.
#
# Command_Line_Tools_macOS_10.11_for_Xcode_8.2.dmg contains two packages with SDK
# in the name: CLTools_SDK_OSX1012.pkg and DevSDK_OSX1011.pkg.  The former ends up
# in /Library/Developer/ and the latter in root (/).  So, only pick the first one.
#
MY_SDK_PKGS=$(xar -tf "${MY_PKG_FILE}" | grep "SDK.*\.pkg$" | grep -v DevSDK_ | tr '\n\r' '  ')
if [ -z "${MY_SDK_PKGS}" ]; then
    echo "error: Found no SDK packages in '${MY_PKG_FILE}'." 1>&2;
    xar -tf "${MY_PKG_FILE}" 1>&2
    exit 1;
fi
echo "info: Extracking SDK packages: ${MY_SDK_PKGS}"

if ! xar -xf "${MY_PKG_FILE}" -C "${MY_TMP_DIR_X}" ${MY_SDK_PKGS}; then
    echo "error: Failed to extract ${MY_SDK_PKGS} from '${MY_PKG_FILE}'." 1>&2;
    exit 1;
fi

#
# Expand the unpacked packages into the Y directory.
#
for pkg in ${MY_SDK_PKGS};
do
    echo "info: Expanding '${MY_TMP_DIR_X}/${pkg}' using pbzx and cpio...";
    if ! pbzx "${MY_TMP_DIR_X}/${pkg}/Payload" > "${MY_TMP_DIR_X}/${pkg}/Payload.unpacked" ; then
        echo "error: Failed to unpack '${MY_TMP_DIR_X}/${pkg}/Payload' using pbzx." 1>&2;
        exit 1;
    fi

    MY_CWD=`pwd`
    cd "${MY_TMP_DIR_Y}" || exit 1;
    if ! cpio -i < "${MY_TMP_DIR_X}/${pkg}/Payload.unpacked"; then
        echo "error: Failed to expand '${MY_TMP_DIR_X}/${pkg}/Payload.unpacked' using cpio." 1>&2;
        exit 1;
    fi
    cd "${MY_CWD}"
done

#
# Now, pick the bits we want from the Y directory and move it over to the destination directory.
#
for sdk in "${MY_TMP_DIR_Y}/Library/Developer/CommandLineTools/SDKs/MacOSX10"*.sdk;
do
    MY_BASENAME=`basename "${sdk}"`
    echo "info: Moving '${sdk}/' to '${MY_DST_DIR}/${MY_BASENAME}'...";
    if [ -z "${MY_DRY_RUN}" ]; then
        if ! mv "${sdk}/" "${MY_DST_DIR}/${MY_BASENAME}"; then
            echo "error: Failed to move '${sdk}/' to '${MY_DST_DIR}/${MY_BASENAME}'." 1>&2;
            exit 1;
        fi
    fi
done

#
# Clean up.
#
echo "info: Successfully extracted. Cleaning up temporary files..."
if [ -d "${MY_TMP_DIR_DMG}" ]; then
    hdiutil detach -force "${MY_TMP_DIR_DMG}";
fi
rm -Rf -- "${MY_TMP_DIR_X}/" "${MY_TMP_DIR_Y}/"
rmdir -- "${MY_TMP_DIR_DMG}"
rmdir -- "${MY_TMP_DIR}"

