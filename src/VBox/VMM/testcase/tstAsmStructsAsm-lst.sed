# $Id: tstAsmStructsAsm-lst.sed 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# For testing assembly struct when using yasm.
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
# Strip stuff lines and spaces we don't care about.
#
/ %line /d
/\[section /d
/\[bits /d
/\[absolute /d
/ times /d
s/ *[[:digit:]]* //
/^ *$/d
s/ *$//g
s/^ *//g
/^\.text$/d
/^\.data$/d
/^\.bss$/d
/ *\.unnamed_padding\./d
s/[[:space:]][[:space:]]*/ /g

#
# Figure which type of line this is and process it accordingly.
#
/^[[:alpha:]_][[:alnum:]_]*:/b struct
/^[[:alpha:]_][[:alnum:]_]*_size EQU \$ - .*$/b struct_equ
/<gap>/b member
/^\.[[:alpha:]_][[:alnum:]_.:]* res.*$/b member_two
/^\.[[:alpha:]_][[:alnum:]_.:]* EQU .*$/b member_two
/^\.[[:alpha:]_][[:alnum:]_.:]*:$/b member_alias
b error
b member_two


#
# Struct start / end.
#
:struct_equ
s/_size EQU.*$/_size/
:struct
s/:$//
h
s/^/global /
s/$/ ; struct/
b end


#
# Struct member
# Note: the 't' command doesn't seem to be working right with 's'.
#
:member
s/[[:xdigit:]]* *//
s/<gap> *//
/^\.[[:alnum:]_.]*[:]* .*$/!t error
s/\(\.[[:alnum:]_]*\)[:]* .*$/\1 /
G
s/^\([^ ]*\) \(.*\)$/global \2\1 ; member/
s/\n//m

b end


#
# Struct member, no address. yasm r1842 and later.
#
:member_two
s/[:]*  *res[bwdtq] .*$//
s/[:]*  *EQU .*$//
s/$/ /
/^\.[[:alnum:]_.]* *$/!t error
G
s/^\([^ ]*\) \(.*\)$/global \2\1 ; member2/
s/\n//m

b end

#
# Alias member like Host.cr0Fpu in 64-bit.  Drop it.
#
:member_alias
d
b end

:error
s/^/\nSed script logic error!\nBuffer: /
s/$/\nHold: /
G
q 1
b end


:end

