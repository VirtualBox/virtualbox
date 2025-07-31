#!/bin/bash
## @file
# Checks for unused *.png files on the basis of existing sources.
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


# Ignore null globs:
shopt -s nullglob

# Parse argument(s):
IFS='='
fVerbose=false
for argument in "$@"; do
    # Do we have directory passed?
    if [[ "$argument" == -d=* || "$argument" == --directory=* ]]; then
        # Parse directory argument data:
        read -ra dParentData <<< "$argument"
        # Get corresponding directory (wipe end-slash):
        strDirectory=${dParentData[1]%/}
        # Check if directory exists:
        if [ ! -d "$strDirectory" ]; then
            echo "Invalid 'directory' argument: '$strDirectory', aborting.."
            exit 1
        fi
    fi
    # Do we have .png files mask passed?
    if [[ "$argument" == -m=* || "$argument" == --mask=* ]]; then
        # Parse input argument data:
        read -ra dInputData <<< "$argument"
        # Remember corresponding mask (no changes?):
        strMask=${dInputData[1]}
    fi
    # Do we have pattern passed?
    if [[ "$argument" == -p=* || "$argument" == --pattern=* ]]; then
        # Parse input argument data:
        read -ra dInputData <<< "$argument"
        # Remember corresponding pattern (no changes?):
        strPattern=${dInputData[1]}
    fi
    # Do we have substitute passed?
    if [[ "$argument" == -s=* || "$argument" == --substitute=* ]]; then
        # Parse input argument data:
        read -ra dInputData <<< "$argument"
        # Remember corresponding substitute (no changes?):
        strSubstitute=${dInputData[1]}
    fi
done

cd $strDirectory

# Compose a list of .png files.
list=$strMask*.png

for strEntry in $list
do
    # TODO: replace ../.. with actual path
    strReplacement=`echo $strEntry | sed s/$strPattern/$strSubstitute/`
    find ../.. -name "*.cpp" -print0 | xargs -0 sed -i "s/$strEntry/$strReplacement/g"
    find ../.. -name "*.qrc" -print0 | xargs -0 sed -i "s/$strEntry/$strReplacement/g"
done

for strEntry in $list
do
    strReplacement=`echo $strEntry | sed s/$strPattern/$strSubstitute/`
    svn mv $strEntry $strReplacement
done

cd -
