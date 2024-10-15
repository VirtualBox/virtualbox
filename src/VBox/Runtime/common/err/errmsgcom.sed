# $Id: errmsgcom.sed 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# IPRT - SED script for converting COM errors
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

# we only care about message definitions
/No symbolic name defined/b skip_it
\/\/ MessageId: /b messageid
d
b end


# Everything else is deleted!
:skip_it
d
b end


#
# A message ID we care about
#
:messageid
# concatenate the next four lines to the string
N
N
N
N
{
    # remove DOS <CR>.
    s/\r//g
    # remove the message ID
    s/\/\/ MessageId: //g
    # remove the stuff in between
    s/\/\/\n\/\/ MessageText:\n\/\/\n\/\/  *//g
    # backslashes have to be escaped
    s/\\/\\\\/g
    # double quotes have to be escaped, too
    s/"/\\"/g
    # output C array entry
    s/\([[:alnum:]_]*\)[\t ]*\n\(.*\)[\t ]*$/{ "\2", "\1", \1 }, /
}
b end

# next expression
:end
