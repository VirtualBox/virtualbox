#!/bin/bash
## @file
# Generate *.qrc files on the basis of existing images.
# Splits output into 2 files.
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
    # Do we have PNG files directory passed?
    if [[ "$argument" == -d=* || "$argument" == --dir=* || "$argument" == --directory=* ]]; then
        # Parse PNG files directory argument data:
        read -ra dDirectoryData <<< "$argument"
        # Get corresponding directory (wipe end-slash):
        strDirectory=${dDirectoryData[1]%/}
        # Check if directory exists:
        if [ ! -d "$strDirectory" ]; then
            echo "Invalid 'directory' argument: '$strDirectory', aborting.."
            exit 1
        fi
    fi
    # Do we have output-file suffix passed?
    if [[ "$argument" == -s=* || "$argument" == --suffix=* ]]; then
        # Parse output-file suffix argument data:
        read -ra dSuffixData <<< "$argument"
        # Get corresponding suffix:
        strSuffix=${dSuffixData[1]}
    fi
done

# Fix argument(s):
if [[ -z "$strDirectory" ]]; then
    echo "--directory (-d) argument is not set, aborting..";
    exit 1
fi

# Calculate number of files:
if [ "$fVerbose" = true ]; then echo "PNG files 'directory' set to: $strDirectory"; fi
pngFiles=$strDirectory/*.png
strFileNumber=`ls -1 $pngFiles | wc -l`
let iFileNumber=strFileNumber
let iFileNumberHalf=iFileNumber/2
let iFileNumberHalfPlusOne=iFileNumberHalf+1
if [ "$fVerbose" = true ]; then echo "Found $iFileNumber PNG files.."; fi

# Enumerate files array:
let iInputFileIndex=0
let iOutputFileIndex=0
for strFullAbsoluteName in $pngFiles; do
    # Increment index:
    let iInputFileIndex++

    # Border conditions:
    if [[ "$iInputFileIndex" -eq 1 || "$iInputFileIndex" -eq "$iFileNumberHalfPlusOne" ]]
    then
        # Increment index:
        let iOutputFileIndex++
        # Re-fetch output-file name:
        strOutputFileName=VirtualBox$iOutputFileIndex$strSuffix.qrc
        # Write output-file header:
        if [ "$fVerbose" = true ]; then echo "Writing header for file: $strOutputFileName"; fi
        echo "<RCC>" > $strOutputFileName
        echo "    <qresource suffix=\"/\">" >> $strOutputFileName
    fi

    # Write output-file body:
    strNameWithSuffix=`basename $strFullAbsoluteName`
    if [ "$fVerbose" = true ]; then echo " Writing body for file: $strOutputFileName"; fi
    echo "        <file alias=\"$strNameWithSuffix\">$strFullAbsoluteName</file>" >> $strOutputFileName

    # Border conditions:
    if [[ ("$iInputFileIndex" -eq "$iFileNumberHalf") || ("$iInputFileIndex" -eq "$iFileNumber") ]]
    then
        # Write output-file footer:
        if [ "$fVerbose" = true ]; then echo "Writing footer for file: $strOutputFileName"; fi
        echo "    </qresource>" >> $strOutputFileName
        echo "</RCC>" >> $strOutputFileName
    fi
done
