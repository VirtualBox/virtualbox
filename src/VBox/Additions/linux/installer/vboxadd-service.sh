#!/bin/sh
# $Id: vboxadd-service.sh 168911 2025-05-16 16:06:57Z andreas.loeffler@oracle.com $
## @file
# Linux Additions Guest Additions service daemon init script.
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

# X-Conflicts-With is our own invention, which we use when converting to
# a systemd unit.

# chkconfig: 345 35 65
# description: VirtualBox Additions service
#
### BEGIN INIT INFO
# Provides:       vboxadd-service
# Required-Start: vboxadd
# Required-Stop:  vboxadd
# Default-Start:  2 3 4 5
# Default-Stop:   0 1 6
# X-Conflicts-With: systemd-timesyncd.service
# Description:    VirtualBox Additions Service
### END INIT INFO

PATH=$PATH:/bin:/sbin:/usr/sbin
SCRIPTNAME=vboxadd-service.sh

PIDFILE="/var/run/${SCRIPTNAME}"

# Preamble for Gentoo
if [ "`which $0`" = "/sbin/rc" ]; then
    shift
fi

begin()
{
    test -n "${2}" && echo "${SCRIPTNAME}: ${1}."
    logger -t "${SCRIPTNAME}" "${1}."
}

succ_msg()
{
    logger -t "${SCRIPTNAME}" "${1}."
}

fail_msg()
{
    echo "${SCRIPTNAME}: ${1}." >&2
    logger -t "${SCRIPTNAME}" "${1}."
}

daemon() {
    $1 $2 $3
}

killproc() {
    # On older (guest) OSes killall isn't available, so try using pkill instead.
    if test -x "$(which killall 2>/dev/null)"; then
        killall "$1"
    elif test -x "$(which pkill 2>/dev/null)"; then
        pkill "$1"
    else
        echo "Unable to determine kill binary; please report this bug!"
        exit 1
    fi
    rm -f $PIDFILE
}

if which start-stop-daemon >/dev/null 2>&1; then
    daemon() {
        start-stop-daemon --start --exec $1 -- $2 $3
    }

    killproc() {
        start-stop-daemon --stop --retry 2 --exec $@
    }
fi

binary=/usr/sbin/VBoxService

testbinary() {
    test -x "$binary" || {
        echo "Cannot run $binary"
        exit 1
    }
}

vboxaddrunning() {
    lsmod | grep -q "vboxguest[^_-]"
}

start() {
    if ! test -f $PIDFILE; then
        begin "Starting VirtualBox Guest Addition service" console;
        vboxaddrunning || {
            echo "VirtualBox Additions module not loaded!"
            exit 1
        }
        testbinary
        daemon $binary --pidfile $PIDFILE > /dev/null
        RETVAL=$?
        succ_msg "VirtualBox Guest Addition service started"
    fi
    return $RETVAL
}

stop() {
    if test -f $PIDFILE; then
        begin "Stopping VirtualBox Guest Addition service" console;
        killproc $binary
        RETVAL=$?
        if ! pidof VBoxService > /dev/null 2>&1; then
            rm -f $PIDFILE
            succ_msg "VirtualBox Guest Addition service stopped"
        else
            fail_msg "VirtualBox Guest Addition service failed to stop"
        fi
    fi
    return $RETVAL
}

restart() {
    stop && start
}

status() {
    echo -n "Checking for VBoxService"
    if [ -f $PIDFILE ]; then
        echo " ...running"
        RETVAL=0
    else
        echo " ...not running"
        RETVAL=1
    fi

    return $RETVAL
}

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
setup)
    ;;
cleanup)
    ;;
*)
    echo "Usage: $0 {start|stop|restart|status}"
    exit 1
esac
