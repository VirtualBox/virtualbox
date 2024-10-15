#!/usr/bin/env kmk_ash
# $Id: RemoveDirFromPath.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Shell (bash + kmk_ash) function for removing a directory from the PATH.
#
# Assumes KBUILD_HOST is set.
#

#
# Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

##
# Modifies the PATH variable by removing $1.
#
# @param 1     The PATH separator (":" or ";").
# @param 2     The directory to remove from the path.
RemoveDirFromPath()
{
    # Parameters.
    local MY_SEP="$1"
    local MY_DIR="$2"
    if test "${KBUILD_HOST}" = "win"; then
        MY_DIR="$(cygpath -u "${MY_DIR}")"
    fi

    # Set the PATH components as script argument.
    local MY_OLD_IFS="${IFS}"
    IFS="${MY_SEP}"
    set -- ${PATH}
    IFS="${MY_OLD_IFS}"

    # Iterate the components and rebuild the path.
    PATH=""
    local MY_SEP_PREV=""
    local MY_COMPONENT
    for MY_COMPONENT
    do
        if test "${MY_COMPONENT}" != "${MY_DIR}"; then
            PATH="${PATH}${MY_SEP_PREV}${MY_COMPONENT}"
            MY_SEP_PREV="${MY_SEP}" # Helps not eliminating empty entries.
        fi
    done
}

