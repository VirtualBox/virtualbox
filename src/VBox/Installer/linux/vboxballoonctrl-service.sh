#!/bin/sh
# $Id: vboxballoonctrl-service.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox watchdog daemon init script.
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

# chkconfig: 345 35 65
# description: VirtualBox watchdog daemon
#
### BEGIN INIT INFO
# Provides:       vboxballoonctrl-service
# Required-Start: vboxdrv
# Required-Stop:  vboxdrv
# Default-Start:  2 3 4 5
# Default-Stop:   0 1 6
# Description:    VirtualBox watchdog daemon
### END INIT INFO

PATH=$PATH:/bin:/sbin:/usr/sbin
SCRIPTNAME=vboxballoonctrl-service.sh

[ -f /etc/vbox/vbox.cfg ] && . /etc/vbox/vbox.cfg

if [ -n "$INSTALL_DIR" ]; then
    binary="$INSTALL_DIR/VBoxBalloonCtrl"
else
    binary="/usr/lib/virtualbox/VBoxBalloonCtrl"
fi

# silently exit if the package was uninstalled but not purged,
# applies to Debian packages only (but shouldn't hurt elsewhere)
[ ! -f /etc/debian_release -o -x $binary ] || exit 0

[ -r /etc/default/virtualbox ] && . /etc/default/virtualbox

PIDFILE="/var/run/${SCRIPTNAME}"

# Preamble for Gentoo
if [ "`which $0`" = "/sbin/rc" ]; then
    shift
fi

begin_msg()
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
    echo "${SCRIPTNAME}: failed: ${1}." >&2
    logger -t "${SCRIPTNAME}" "failed: ${1}."
}

start_daemon() {
    usr="$1"
    shift
    su - $usr -c "$*"
}

killproc() {
    killall $1
    rm -f $PIDFILE
}

if which start-stop-daemon >/dev/null 2>&1; then
    start_daemon() {
        usr="$1"
        shift
        bin="$1"
        shift
        start-stop-daemon --background --chuid $usr --start --exec $bin -- $@
    }

    killproc() {
        start-stop-daemon --stop --exec $@
    }
fi

vboxdrvrunning() {
    lsmod | grep -q "vboxdrv[^_-]"
}

check_single_user() {
    if [ -n "$2" ]; then
        fail_msg "VBOXWATCHDOG_USER must not contain multiple users!"
        exit 1
    fi
}

start() {
    if ! test -f $PIDFILE; then
        [ -z "$VBOXWATCHDOG_USER" -a -z "$VBOXBALLOONCTRL_USER" ] && exit 0
        [ -z "$VBOXWATCHDOG_USER" ] && VBOXWATCHDOG_USER="$VBOXBALLOONCTRL_USER"
        begin_msg "Starting VirtualBox watchdog service" console;
        check_single_user $VBOXWATCHDOG_USER
        vboxdrvrunning || {
            fail_msg "VirtualBox kernel module not loaded!"
            exit 0
        }
        # Handle legacy parameters, do not add any further ones unless absolutely necessary.
        [ -z "$VBOXWATCHDOG_BALLOON_INTERVAL" -a -n "$VBOXBALLOONCTRL_INTERVAL" ]           && VBOXWATCHDOG_BALLOON_INTERVAL="$VBOXBALLOONCTRL_INTERVAL"
        [ -z "$VBOXWATCHDOG_BALLOON_INCREMENT" -a -n "$VBOXBALLOONCTRL_INCREMENT" ]         && VBOXWATCHDOG_BALLOON_INCREMENT="$VBOXBALLOONCTRL_INCREMENT"
        [ -z "$VBOXWATCHDOG_BALLOON_DECREMENT" -a -n "$VBOXBALLOONCTRL_DECREMENT" ]         && VBOXWATCHDOG_BALLOON_DECREMENT="$VBOXBALLOONCTRL_DECREMENT"
        [ -z "$VBOXWATCHDOG_BALLOON_LOWERLIMIT" -a -n "$VBOXBALLOONCTRL_LOWERLIMIT" ]       && VBOXWATCHDOG_BALLOON_LOWERLIMIT="$VBOXBALLOONCTRL_LOWERLIMIT"
        [ -z "$VBOXWATCHDOG_BALLOON_SAFETYMARGIN" -a -n "$VBOXBALLOONCTRL_SAFETYMARGIN" ]   && VBOXWATCHDOG_BALLOON_SAFETYMARGIN="$VBOXBALLOONCTRL_SAFETYMARGIN"
        [ -z "$VBOXWATCHDOG_ROTATE" -a -n "$VBOXBALLOONCTRL_ROTATE" ]           && VBOXWATCHDOG_ROTATE="$VBOXBALLOONCTRL_ROTATE"
        [ -z "$VBOXWATCHDOG_LOGSIZE" -a -n "$VBOXBALLOONCTRL_LOGSIZE" ]         && VBOXWATCHDOG_LOGSIZE="$VBOXBALLOONCTRL_LOGSIZE"
        [ -z "$VBOXWATCHDOG_LOGINTERVAL" -a -n "$VBOXBALLOONCTRL_LOGINTERVAL" ] && VBOXWATCHDOG_LOGINTERVAL="$VBOXBALLOONCTRL_LOGINTERVAL"

        PARAMS="--background"
        [ -n "$VBOXWATCHDOG_BALLOON_INTERVAL" ]     && PARAMS="$PARAMS --balloon-interval \"$VBOXWATCHDOG_BALLOON_INTERVAL\""
        [ -n "$VBOXWATCHDOG_BALLOON_INCREMENT" ]    && PARAMS="$PARAMS --balloon-inc \"$VBOXWATCHDOG_BALLOON_INCREMENT\""
        [ -n "$VBOXWATCHDOG_BALLOON_DECREMENT" ]    && PARAMS="$PARAMS --balloon-dec \"$VBOXWATCHDOG_BALLOON_DECREMENT\""
        [ -n "$VBOXWATCHDOG_BALLOON_LOWERLIMIT" ]   && PARAMS="$PARAMS --balloon-lower-limit \"$VBOXWATCHDOG_BALLOON_LOWERLIMIT\""
        [ -n "$VBOXWATCHDOG_BALLOON_SAFETYMARGIN" ] && PARAMS="$PARAMS --balloon-safety-margin \"$VBOXWATCHDOG_BALLOON_SAFETYMARGIN\""
        [ -n "$VBOXWATCHDOG_ROTATE" ]       && PARAMS="$PARAMS -R \"$VBOXWATCHDOG_ROTATE\""
        [ -n "$VBOXWATCHDOG_LOGSIZE" ]      && PARAMS="$PARAMS -S \"$VBOXWATCHDOG_LOGSIZE\""
        [ -n "$VBOXWATCHDOG_LOGINTERVAL" ]  && PARAMS="$PARAMS -I \"$VBOXWATCHDOG_LOGINTERVAL\""
        # prevent inheriting this setting to VBoxSVC
        unset VBOX_RELEASE_LOG_DEST
        start_daemon $VBOXWATCHDOG_USER $binary $PARAMS > /dev/null 2>&1
        # ugly: wait until the final process has forked
        sleep .1
        PID=`pidof $binary 2>/dev/null`
        if [ -n "$PID" ]; then
            echo "$PID" > $PIDFILE
            RETVAL=0
            succ_msg "VirtualBox watchdog service started"
        else
            RETVAL=1
            fail_msg "VirtualBox watchdog service failed to start"
        fi
    fi
    return $RETVAL
}

stop() {
    if test -f $PIDFILE; then
        begin_msg "Stopping VirtualBox watchdog service" console;
        killproc $binary
        RETVAL=$?
        if ! pidof $binary > /dev/null 2>&1; then
            rm -f $PIDFILE
            succ_msg "VirtualBox watchdog service stopped"
        else
            fail_msg "VirtualBox watchdog service failed to stop"
        fi
    fi
    return $RETVAL
}

restart() {
    stop && start
}

status() {
    echo -n "Checking for VBox watchdog service"
    if [ -f $PIDFILE ]; then
        echo " ...running"
    else
        echo " ...not running"
    fi
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
force-reload)
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

exit $RETVAL
