# $Id: errmsg.sed 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# IPRT - SED script for converting */err.h.
#

#
# Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
# The contents of this file may alternatively be used under the terms
# of the Common Development and Distribution License Version 1.0
# (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
# in the VirtualBox distribution, in which case the provisions of the
# CDDL are applicable instead of those of the GPL.
#
# You may elect to license modified versions of this file under the
# terms and conditions of either the GPL or the CDDL or both.
#
# SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
#

# Handle text inside the markers.
/SED-START/,/SED-END/{

# if (#define) goto defines
/^[[:space:]]*#[[:space:]]*define/b defines

# if (/**) goto description
/\/\*\*/b description

}

# Everything else is deleted!
d
b end


##
# Convert the defines
:defines
s/^[[:space:]]*#[[:space:]]*define[[:space:]]*\([[:alnum:]_]*\)[[:space:]]*\(.*\)[[:space:]]*$/    "\1",\n     \1, false }, /
b end

##
# Convert descriptive comments. /** desc */
:description

# Read all the lines belonging to the comment into the buffer.
:look-for-end-of-comment
/\*\//bend-of-comment
N
blook-for-end-of-comment
:end-of-comment

# anything with @{ and @} is skipped
/@[\{\}]/d

# Fix double spaces
s/[[:space:]][[:space:]]/ /g

# Fix \# sequences (doxygen needs them, we don't).
s/\\#/#/g

# insert punctuation.
s/\([^.[:space:]]\)[[:space:]]*\*\//\1. \*\//

# convert /** short. more
s/[[:space:]]*\/\*\*[[:space:]]*/  { NULL, \"/
s/  { NULL, \"\([^.!?"]*[.!?][.!?]*\)/  { \"\1\",\n    \"\1/

# terminate the string
s/[[:space:]]*\*\//\"\,/

# translate empty lines into new-lines (only one, please).
s/[[:space:]]*[[:space:]]\*[[:space:]][[:space:]]*\*[[:space:]][[:space:]]*/\\n/g

# remove asterics.
s/[[:space:]]*[[:space:]]\*[[:space:]][[:space:]]*/ /g
b end


# next expression
:end
