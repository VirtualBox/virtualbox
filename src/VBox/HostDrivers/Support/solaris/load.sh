#!/bin/bash
# $Id: load.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# For development.
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

DRVNAME="vboxdrv"
DRIVERS_USING_IT="vboxusb vboxusbmon vboxnet vboxflt vboxbow"

DRVFILE=`dirname "$0"`
DRVFILE=`cd "$DRVFILE" && pwd`
DRVFILE="$DRVFILE/$DRVNAME"
if [ ! -f "$DRVFILE" ]; then
    echo "load.sh: Cannot find $DRVFILE or it's not a file..."
    exit 1;
fi
if [ ! -f "$DRVFILE.conf" ]; then
    echo "load.sh: Cannot find $DRVFILE.conf or it's not a file..."
    exit 1;
fi

SUDO=sudo
hash "${SUDO}" 2> /dev/null || SUDO=pfexec
#set -x

# Disable the zone access service.
servicefound=`svcs -H "virtualbox/zoneaccess" 2>/dev/null | grep '^online'`
if test ! -z "$servicefound"; then
    $SUDO svcadm disable svc:/application/virtualbox/zoneaccess:default
fi

# Unload driver that may depend on the driver we're going to (re-)load
# as well as the driver itself.
for drv in $DRIVERS_USING_IT $DRVNAME;
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
# Reconfigure the driver so it get a major number.
#
# Note! We have to copy the driver and config files to somewhere the kernel can
#       find them. It is searched for as drv/${DRVNAME}.conf in
#       kobj_module_path, which is usually:
#           /platform/i86pc/kernel /kernel /usr/kernel
#       To try prevent bad drivers from being loaded on the next boot, we remove
#       always the files.
#
MY_RC=1
set -e
$SUDO rm -f \
    "/platform/i86pc/kernel/drv/${DRVNAME}.conf" \
    "/platform/i86pc/kernel/drv/${DRVNAME}" \
    "/platform/i86pc/kernel/drv/amd64/${DRVNAME}"
sync
$SUDO cp "${DRVFILE}"      /platform/i86pc/kernel/drv/amd64/
$SUDO cp "${DRVFILE}.conf" /platform/i86pc/kernel/drv/
set +e

$SUDO rem_drv $DRVNAME
if $SUDO add_drv -v $DRVNAME; then
    sync
    if $SUDO modload "/platform/i86pc/kernel/drv/amd64/${DRVNAME}"; then
        echo "load.sh: successfully loaded the driver"
        modinfo | grep -w "$DRVNAME"
        MY_RC=0
        if test ! -h "/dev/vboxdrv"; then
            $SUDO ln -sf "/devices/pseudo/vboxdrv@0:vboxdrv" /dev/vboxdrv
            $SUDO chmod 0666 /dev/vboxdrv
        fi
    else
        dmesg | tail
        echo "load.sh: modload failed"
    fi
else
    dmesg | tail
    echo "load.sh: add_drv failed."
fi

$SUDO rm -f \
    "/platform/i86pc/kernel/drv/${DRVNAME}.conf" \
    "/platform/i86pc/kernel/drv/${DRVNAME}" \
    "/platform/i86pc/kernel/drv/amd64/${DRVNAME}"
sync

exit $MY_RC;

