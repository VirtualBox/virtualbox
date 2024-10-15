#!/bin/sh
# $Id: postinstall.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox postinstall script for Solaris.
#

#
# Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

rc=0
currentzone=`zonename`
if test "$currentzone" = "global"; then
    DEBUGOPT=`set -o 2>/dev/null | sed -ne 's/^xtrace *on$/--sh-trace/p'` # propagate pkgadd -v
    ${PKG_INSTALL_ROOT:=/}/opt/VirtualBox/pkginstall.sh --srv4 ${DEBUGOPT}
    rc=$?
fi

# installf inherits ${PKG_INSTALL_ROOT} from pkgadd, no need to explicitly specify
/usr/sbin/installf -f $PKGINST

# return 20 = requires reboot, 2 = partial failure, 0  = success
exit "$rc"

