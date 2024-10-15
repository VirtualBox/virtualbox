#!/bin/sh
# $Id: pkginstall.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
#
# VirtualBox postinstall script for Solaris.
#
# If you just installed VirtualBox using IPS/pkg(5), you should run this
# script once to avoid rebooting the system before using VirtualBox.
#

#
# Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

if test "$1" != "--srv4"; then
    # IPS package
    echo "Checking for older & partially installed bits..."
    ISIPS="--ips"
else
    # SRv4 package
    echo "Checking for older bits..."
    ISIPS=""
fi

# pkgadd -v
if test "$1" = "--sh-trace" || test "$2" = "--sh-trace" || test "$3" = "--sh-trace"; then
    set -x
fi
DEBUGOPT=`set -o 2>/dev/null | sed -ne 's/^xtrace *on$/--sh-trace/p'` # propagate pkgadd -v

# If PKG_INSTALL_ROOT is undefined or NULL, redefine to '/' and carry on.
${PKG_INSTALL_ROOT:=/}/opt/VirtualBox/vboxconfig.sh --preremove --fatal ${ISIPS} ${DEBUGOPT}

if test "$?" -eq 0; then
    echo "Installing new ones..."
    $PKG_INSTALL_ROOT/opt/VirtualBox/vboxconfig.sh --postinstall ${DEBUGOPT}
    rc=$?
    if test "$rc" -ne 0; then
        echo 1>&2 "## Completed but with errors."
        rc=1
    else
        if test "$1" != "--srv4"; then
            echo "Post installation completed successfully!"
        fi
    fi
else
    echo 1>&2 "## ERROR!! Failed to remove older/partially installed bits."
    rc=1
fi

exit "$rc"

