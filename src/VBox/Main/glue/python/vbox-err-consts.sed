# $Id: vbox-err-consts.sed 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# IPRT - SED script for converting */err.h to a python dictionary.
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
# SPDX-License-Identifier: GPL-3.0-only
#

# Header and footer text:
1i <text>
$a </text>

# Handle text inside the markers.
/SED-START/,/SED-END/{

# if (#define) goto defines
/^[[:space:]]*#[[:space:]]*define/b defines

}

# Everything else is deleted!
:delete
d
b end


##
# Convert the defines
:defines

/^[[:space:]]*#[[:space:]]*define[[:space:]]*\([[:alnum:]_]*\)[[:space:](]*\([-+]*[[:space:]]*[[:digit:]][[:digit:]]*\)[[:space:])]*$/b define_okay
b delete
:define_okay
s/^[[:space:]]*#[[:space:]]*define[[:space:]]*\([[:alnum:]_]*\)[[:space:](]*\([-+]*[[:space:]]*[[:digit:]][[:digit:]]*\)[[:space:])]*$/        '\1': \2,/

b end


# next expression
:end
