#!/bin/bash
## @file
# Integrates HiDPI *.png files on the basis of existing LoDPI.
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


# Ignore null globs:
shopt -s nullglob

# Parse argument(s):
IFS='='
fVerbose=false
for argument in "$@"; do
    # Should we be verbose?
    if [[ "$argument" == -v || "$argument" == --verbose ]]; then fVerbose=true; fi
    # Do we have *input* PNG files directory passed?
    if [[ "$argument" == -i=* || "$argument" == --input=* ]]; then
        # Parse input argument data:
        read -ra dInputData <<< "$argument"
        # Get corresponding directory (wipe end-slash):
        strInputDirectory=${dInputData[1]%/}
        # Check if directory exists:
        if [ ! -d "$strInputDirectory" ]; then
            echo "Invalid 'input directory' argument: '$strInputDirectory', aborting.."
            exit 1
        fi
    fi
    # Do we have *parent* PNG file passed?
    if [[ "$argument" == -p=* || "$argument" == --parent=* ]]; then
        # Parse parent argument data:
        read -ra dParentData <<< "$argument"
        # Get corresponding file:
        strParentFile=${dParentData[1]}
        # Check if file exists:
        if [ ! -f "$strParentFile" ]; then
            echo "Invalid 'parent directory' argument: '$strParentFile', aborting.."
            exit 1
        fi
    fi
    # Do we have *output* PNG files directory passed?
    if [[ "$argument" == -o=* || "$argument" == --output=* ]]; then
        # Parse output PNG files directory argument data:
        read -ra dOutpuData <<< "$argument"
        # Get corresponding directory (wipe end-slash):
        strOutputDirectory=${dOutpuData[1]%/}
        # Check if directory exists:
        if [ ! -d "$strOutputDirectory" ]; then
            echo "Invalid 'output directory' argument: '$strOutputDirectory', aborting.."
            exit 1
        fi
    fi
done

# Fix argument(s):
if [[ -z "$strInputDirectory" ]]; then
    echo "--input (-i) directory is not set, aborting..";
    exit 1
fi
if [[ -z "$strParentFile" ]]; then
    echo "--parent (-p) file is not set, aborting..";
    exit 1
fi
if [[ -z "$strOutputDirectory" ]]; then
    echo "--output (-o) directory is not set, aborting..";
    exit 1
fi

# Enumerate input files..
FILES=$strInputDirectory/*.png
for strFullAbsoluteName in $FILES
do
    # Parse input file:
    if [ "$fVerbose" = true ]; then echo "Input file: $strFullAbsoluteName"; fi
    strNameWithSuffix=`basename $strFullAbsoluteName`

    # Prepare output file:
    strNewFullAbsoluteName=$strOutputDirectory/$strNameWithSuffix
    if [ "$fVerbose" = true ]; then echo "Output file: $strNewFullAbsoluteName"; fi

    # Integrate only if output file missing:
    if [ ! -f $strNewFullAbsoluteName ]; then
        svn cp $strParentFile $strNewFullAbsoluteName
        mv $strFullAbsoluteName $strNewFullAbsoluteName
    fi
done
