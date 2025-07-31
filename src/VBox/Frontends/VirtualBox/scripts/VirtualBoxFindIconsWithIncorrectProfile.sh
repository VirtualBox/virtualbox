#!/bin/bash
## @file
# Searches for *.png files with incorrect sRGB profile.
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

# Compose a list of all .png files:
list=`find . -name '*.png'`

# Use pngtogd tool which uses libpng to check for incorrect profile:
for strEntry in $list
do
    err=$(pngtogd $strEntry /dev/null 2>&1 >/dev/null)
    if [ -n "$err" ]; then
        echo $strEntry
    fi
done
