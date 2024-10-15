# $Id: biosorg_check.sed 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# For converting biosorg_check_<addr> lines in a wlink mapfile
# to kmk_expr checks.
#

#
# Copyright (C) 2012-2024 Oracle and/or its affiliates.
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


/biosorg_check_at_/b check_at
/biosorg_check_before_or_at_/b check_before_or_at
b end

:check_at
#p # --debug
s/\(.*\)/\L\1/g
s/....:\(....\). *biosorg_check_at_0\(\1\)h *//
/^$/b end
q 1
b end

# after or equal.
:check_before_or_at
#p # --debug
s/\(.*\)/\L\1/g

h
s/^....:\(....\). *biosorg_check_before_or_at_0\(....\)h */\2/
x
s/^....:\(....\)/\1 /


# Loop for comparing the two addresses.  The one in the pattern buffer (left) must be
# smaller or equal to the one in in the pattern buffer (BIOSORG address).
:compare_loop
/^f/b match_f_or_greater
/^e/b match_e_or_greater
/^d/b match_d_or_greater
/^c/b match_c_or_greater
/^b/b match_b_or_greater
/^a/b match_a_or_greater
/^9/b match_9_or_greater
/^8/b match_8_or_greater
/^7/b match_7_or_greater
/^6/b match_6_or_greater
/^5/b match_5_or_greater
/^4/b match_4_or_greater
/^3/b match_3_or_greater
/^2/b match_2_or_greater
/^1/b match_1_or_greater
/^0/b match_0_or_greater
:bad
p
x
p
q 1
b end

:bad_other
x
b bad


:match_f_or_greater
x
/^f/b next_compare
b bad_other

:match_e_or_greater
x
/^f/b end
/^e/b next_compare
b bad_other

:match_d_or_greater
x
/^[e-f]/b end
/^d/b next_compare
b bad_other

:match_c_or_greater
x
/^[d-f]/b end
/^c/b next_compare
b bad_other

:match_b_or_greater
x
/^[c-f]/b end
/^b/b next_compare
b bad_other

:match_a_or_greater
x
/^[b-f]/b end
/^a/b next_compare
b bad_other

:match_9_or_greater
x
/^[a-f]/b end
/^9/b next_compare
b bad_other

:match_8_or_greater
x
/^[9a-f]/b end
/^8/b next_compare
b bad_other

:match_7_or_greater
x
/^[8-9a-f]/b end
/^7/b next_compare
b bad_other

:match_6_or_greater
x
/^[7-9a-f]/b end
/^6/b next_compare
b bad_other

:match_5_or_greater
x
/^[6-9a-f]/b end
/^5/b next_compare
b bad_other

:match_4_or_greater
x
/^[5-9a-f]/b end
/^4/b next_compare
b bad_other

:match_3_or_greater
x
/^[4-9a-f]/b end
/^3/b next_compare
b bad_other

:match_2_or_greater
x
/^[3-9a-f]/b end
/^2/b next_compare
b bad_other

:match_1_or_greater
x
/^[2-9a-f]/b end
/^1/b next_compare
b bad_other

:match_0_or_greater
x
/^[1-9a-f]/b end
/^0/b next_compare
b bad_other


# Next round of the loop.
# 1. Drop the leading digit of the max address (BIOSORG).
# 2. Check if we've reached end of the address. If so, check that we've reached the space in the actual address.
# 3. Switch buffers so the actual address in the pattern space.
# 4. Drop the leading digit of the actual address.
# 5. Repeat.
:next_compare
s/^.//
/^$/b end_of_compare
x
s/^.//
b compare_loop

:end_of_compare
x
/^. /b end
b bad

:end

