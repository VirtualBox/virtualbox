# $Id: dwarfdump-to-offsets.sed 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# For defining member offsets for selected struct so the ARM64 assembler can use them.
#
# The script ASSUMES that the input is well filtered and the the output doesn't
# change in any way that makes it difficult to match and edit.  The dwarfdump
# utility assumed there, is the one from llvm that shippes with apple's command
# line tools.
#
# These assumptions aren't ideal, however the two alternatives are:
#   1. Build time program to print the offsets via RT_UOFFSETOF,
#   2. Build time program parsing the dwarf info ourselves.
#
# The problem with the first option is that we have to get all the DEFS right
# as well as the compiler options to ensure that we end up with the same
# structure layout.  Doesn't led itself for cross building either (not all that
# relevant for darwin, but it probably is for linux).
#
# The second option is potentially a lot of work as we don't have any IPRT
# interface for browsing types found in debug info yet.  Once that is added, it
# shouldn't be all that difficult.
#

#
# Copyright (C) 2024 Oracle and/or its affiliates.
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
# This matches first level structure types, putting the name in the hold space for later use.
#
# ASSUMES that the DW_AT_xxx listing follow on separate lines, one line per attribute
# ASSUMES that there is a blank line after the DW_AT_xxx listing
# ASSUMES that first level structures have exactly one space after the colon.
#
/^0x[[:xdigit:]][[:xdigit:]]*: DW_TAG_structure_type/,/^[[:space:]]*$/ {
    /DW_AT_name/ {
        s/^.*DW_AT_name[[:space:]]*[(]["]//
        s/["][)][[:space:]]*$//
        h
    }
    /DW_AT_data_member_location/p
}



#
# This matches the first level of structure members.
#
# ASSUMES that the DW_AT_xxx listing follow on separate lines, one line per attribute
# ASSUMES that there is a blank line after the DW_AT_xxx listing
# ASSUMES that first level member have exactly three spaces after the colon.
#
/^0x[[:xdigit:]][[:xdigit:]]*:   DW_TAG_member/,/^[[:space:]]*$/ {
    /DW_AT_name/ {
        s/^.*DW_AT_name[[:space:]]*[(]["]/_OFF_/
        s/["][)][[:space:]]*$//
        x
        H
        x
        s/\(_OFF_.*\)[\n]\(.*\)$/#define \2\1 \\/
        p
    }
    /DW_AT_data_member_location/ {
        s/^[[:space:]]*DW_AT_data_member_location[[:space:]]*/                /
        p
    }
}

