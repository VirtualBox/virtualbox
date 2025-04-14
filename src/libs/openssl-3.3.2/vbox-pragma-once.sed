# $Id: vbox-pragma-once.sed 109212 2025-04-14 17:29:48Z aleksey.ilyushin@oracle.com $
## @file
# Wraps #pragma once up in #ifndef RT_WITHOUT_PRAGMA_ONCE...#endif.
#
# This is for gcc 3.3.x and older which considers #pragma once as obsolete and
# warns about it unconditionally.  This results in gigantic ValKit build logs
# and makes them hard to search for actual problems.
#
# Apply to include/crypto/*.h and include/internal/*.h with the modify
# in-place option of sed (not on windows, no eol attribs!):
#     kmk_sed -f vbox-pragma-once.sed -i include/crypto/*.h include/internal/*.h
#

#
# Copyright (C) 2006-2022 Oracle and/or its affiliates.
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

# Don't apply to already patched files, so if we see RT_WITHOUT_PRAGMA_ONCE
# we read in a few more lines.
/RT_WITHOUT_PRAGMA_ONCE/!b next

:skip
N
N
N
b end

:next
# Wrap pragma once in #ifndef RT_WITHOUT_PRAGMA_ONCE.
s,^\([[:space:]]*[#][[:space:]]*\)pragma[[:space:]][[:space:]]*once\(.*\)$,\1ifndef RT_WITHOUT_PRAGMA_ONCE                                                                         /* VBOX */\n\1pragma once\2\n\1endif                                                                                                 /* VBOX */,

:end
