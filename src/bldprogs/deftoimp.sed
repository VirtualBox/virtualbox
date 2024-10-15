# $Id: deftoimp.sed 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# SED script for generating a dummy .c from a windows .def file.
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

#
# Remove comments and space. Skip empty lines.
#
s/;.*$//g
s/^[[:space:]][[:space:]]*//g
s/[[:space:]][[:space:]]*$//g
/^$/d

# Handle text after EXPORTS
/EXPORTS/,//{
s/^EXPORTS$//
/^$/b end


/[[:space:]]DATA$/b data

#
# Function export
#
:code
s/^\(.*\)$/EXPORT\nvoid \1(void);\nvoid \1(void){}/
b end


#
# Data export
#
:data
s/^\(.*\)[[:space:]]*DATA$/EXPORT_DATA void *\1 = (void *)0;/
b end

}
d
b end


# next expression
:end

