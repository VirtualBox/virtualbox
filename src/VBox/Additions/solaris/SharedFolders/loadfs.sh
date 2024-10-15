#!/bin/bash
# $Id: loadfs.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# For GA development.
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

DRVNAME="vboxfs"
MOUNTHLP="vboxfsmount"

DRVFILE=`dirname "$0"`
DRVFILE=`cd "$DRVFILE" && pwd`
MOUNTHLPFILE="$DRVFILE/$MOUNTHLP"
DRVFILE="$DRVFILE/$DRVNAME"
if [ ! -f "$DRVFILE" ]; then
    echo "loadfs.sh: Cannot find $DRVFILE or it's not a file..."
    exit 1;
fi
if [ ! -f "$MOUNTHLPFILE" ]; then
    echo "load.sh: Cannot find $MOUNTHLPFILE or it's not a file..."
    exit 1;
fi

SUDO=sudo
#set -x

# Unload the driver if loaded.
for drv in $DRVNAME;
do
    LOADED=`modinfo | grep -w "$drv"`
    if test -n "$LOADED"; then
        MODID=`echo "$LOADED" | cut -d ' ' -f 1`
        $SUDO modunload -i $MODID;
        LOADED=`modinfo | grep -w "$drv"`;
        if test -n "$LOADED"; then
            echo "load.sh: failed to unload $drv";
            dmesg | tail
            exit 1;
        fi
    fi
done

#
# Remove old stuff.
#
MY_RC=1
set -e
$SUDO rm -f \
    "/usr/kernel/fs/${DRVNAME}" \
    "/usr/kernel/fs/amd64/${DRVNAME}" \
    "/etc/fs/vboxfs/mount"
sync
set +e

#
# Install the mount program.
#
if [ ! -d /etc/fs/vboxfs ]; then
    $SUDO mkdir -p /etc/fs/vboxfs
fi
$SUDO ln -sf "$MOUNTHLPFILE" /etc/fs/vboxfs/mount

#
# Load the module. We can load it without copying it to /usr/kernel/fs/.
#
if $SUDO modload "$DRVFILE"; then
    sync
    MY_RC=0
else
    dmesg | tail
    echo "load.sh: add_drv failed."
fi

exit $MY_RC;

