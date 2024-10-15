#!/bin/bash
# $Id: xcode-6.2-extractor.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Extracts the necessary bits from the Xcode 6.2 (Xcode_6.2.dmg,
# md5sum fe4c6c99182668cf14bfa5703bedeed6) and the Command Line
# Tools for Xcode 6.2 (10.9: commandlinetoolsosx10.9forxcode6.2.dmg,
# 10.10: commandlinetoolsosx10.10forxcode6.2.dmg).
#
# This script allows extracting the tools on systems where the command line
# tools refuse to install due to version checks.
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
# Parse arguments.
#
MY_DST_DIR="${MY_DARWIN_DIR}/xcode/v6.2"
MY_XCODE_APP="/Volumes/Xcode/Xcode.app"

my_usage()
{
    echo "usage: $0 <--destination|-d> <dstdir>  <--xcode-app|-x> </Volumes/Xcode/Xcode.app>";
    exit $1;
}

while test $# -ge 1;
do
    ARG=$1;
    shift;
    case "$ARG" in

        --destination|-d)
            if test $# -eq 0; then
                echo "error: missing --tmpdir argument." 1>&2;
                exit 1;
            fi
            MY_DST_DIR="$1";
            shift;
            ;;

        --xcode-app|-x)
            if test $# -eq 0; then
                echo "error: missing --xcode-app argument." 1>&2;
                exit 1;
            fi
            MY_XCODE_APP="$1";
            shift;
            ;;

        --h*|-h*|-?|--?)
            my_usage 0;
    esac
done

# Check the xcode application.
if [ -z "${MY_XCODE_APP}" ]; then
    echo "error: missing --xcode-app <dir/Xcode.app>." 1>&2l
    my_usage 1;
fi
if ! test -d "${MY_XCODE_APP}/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.9.sdk" ; then
    echo "error: missing '/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.9.sdk' under '${MY_XCODE_APP}'." 1>&2;
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

#
# Copy bits from the Xcode package. Must retain a valid .pkg bundle structure or xcrun
# doesn't work, which breaks 'cpp' (needed for dtrace and maybe more).
#
for item in \
        Contents/Info.plist \
        Contents/version.plist \
        Contents/PkgInfo\
        Contents/Frameworks/IDEFoundation.framework \
        Contents/PlugIns/IDEModelFoundation.ideplugin \
        Contents/PlugIns/IDEStandardExecutionActionsCore.ideplugin \
        Contents/PlugIns/Xcode3Core.ideplugin \
        Contents/SharedFrameworks/DTXConnectionServices.framework \
        Contents/SharedFrameworks/DVTFoundation.framework \
        Contents/SharedFrameworks/DVTSourceControl.framework \
        Contents/SharedFrameworks/LLDB.framework \
        Contents/Developer/Toolchains/XcodeDefault.xctoolchain \
        Contents/Developer/usr \
        Contents/Developer/Platforms/MacOSX.platform/Info.plist \
        Contents/Developer/Platforms/MacOSX.platform/version.plist \
        Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs \
        ;
do
    echo "Copying ${item}..."
    if [ -d "${MY_XCODE_APP}/${item}" ]; then
        if ! mkdir -p "${MY_DST_DIR}/x.app/${item}"; then
            echo "error: error creating directory '${MY_DST_DIR}/x.app/${item}'." 1>&2;
            exit 1;
        fi
        if ! cp -af "${MY_XCODE_APP}/${item}/" "${MY_DST_DIR}/x.app/${item}/"; then
            echo "error: problem occured copying directory \"${MY_XCODE_APP}/${item}/\" to \"${MY_DST_DIR}/x.app/${item}/\"." 1>&2;
            exit 1;
        fi
    else
        dir=`dirname "${item}"`
        if ! mkdir -p "${MY_DST_DIR}/x.app/${dir}"; then
            echo "error: error creating directory '${MY_DST_DIR}/x.app/${dir}'." 1>&2;
            exit 1;
        fi
        if ! cp -P "${MY_XCODE_APP}/${item}" "${MY_DST_DIR}/x.app/${item}"; then
            echo "error: problem occured copying \"${MY_XCODE_APP}/${item}\" to \"${MY_DST_DIR}/x.app/${item}\"." 1>&2;
            exit 1;
        fi
    fi
done

#
# Done.
#
echo "info: Successfully extracted."

