#!/bin/bash
# $Id: vboxguest.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox Guest Additions kernel module control script for FreeBSD.
#

#
# Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

VBOXGUESTFILE=""
SILENTUNLOAD=""

abort()
{
    echo 1>&2 "$1"
    exit 1
}

info()
{
    echo 1>&2 "$1"
}

get_module_path()
{
    moduledir="/boot/kernel";
    modulepath=$moduledir/vboxguest.ko
    if test -f "$modulepath"; then
        VBOXGUESTFILE="$modulepath"
    else
        VBOXGUESTFILE=""
    fi
}

check_if_installed()
{
    if test "$VBOXGUESTFILE" -a -f "$VBOXGUESTFILE"; then
        return 0
    fi
    abort "VirtualBox kernel module (vboxguest) not installed."
}

module_loaded()
{
    loadentry=`kldstat | grep vboxguest`
    if test -z "$loadentry"; then
        return 1
    fi
    return 0
}

check_root()
{
    if test `id -u` -ne 0; then
        abort "This program must be run with administrator privileges.  Aborting"
    fi
}

start()
{
    if module_loaded; then
        info "vboxguest already loaded..."
    else
        /sbin/kldload vboxguest.ko
        if ! module_loaded; then
            abort "Failed to load vboxguest."
        elif test -c "/dev/vboxguest"; then
            info "Loaded vboxguest."
        else
            stop
            abort "Aborting due to attach failure."
        fi
    fi
}

stop()
{
    if module_loaded; then
        /sbin/kldunload vboxguest.ko
        info "Unloaded vboxguest."
    elif test -z "$SILENTUNLOAD"; then
        info "vboxguest not loaded."
    fi
}

restart()
{
    stop
    sync
    start
    return 0
}

status()
{
    if module_loaded; then
        info "vboxguest running."
    else
        info "vboxguest stopped."
    fi
}

check_root
get_module_path
check_if_installed

if test "$2" = "silentunload"; then
    SILENTUNLOAD="$2"
fi

case "$1" in
start)
    start
    ;;
stop)
    stop
    ;;
restart)
    restart
    ;;
status)
    status
    ;;
*)
    echo "Usage: $0 {start|stop|restart|status}"
    exit 1
esac

exit

