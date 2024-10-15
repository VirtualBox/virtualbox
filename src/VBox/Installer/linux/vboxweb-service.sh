#!/bin/sh
# $Id: vboxweb-service.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox web service API daemon init script.
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
# description: VirtualBox web service API
#
### BEGIN INIT INFO
# Provides:       vboxweb-service
# Required-Start: vboxdrv
# Required-Stop:  vboxdrv
# Default-Start:  2 3 4 5
# Default-Stop:   0 1 6
# Description:    VirtualBox web service API
# X-Required-Target-Start: network-online
### END INIT INFO

PATH=$PATH:/bin:/sbin:/usr/sbin
SCRIPTNAME=vboxweb-service.sh

[ -f /etc/vbox/vbox.cfg ] && . /etc/vbox/vbox.cfg

if [ -n "$INSTALL_DIR" ]; then
    binary="$INSTALL_DIR/vboxwebsrv"
    vboxmanage="$INSTALL_DIR/VBoxManage"
else
    binary="/usr/lib/virtualbox/vboxwebsrv"
    vboxmanage="/usr/lib/virtualbox/VBoxManage"
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
        fail_msg "VBOXWEB_USER must not contain multiple users!"
        exit 1
    fi
}

start() {
    if ! test -f $PIDFILE; then
        [ -z "$VBOXWEB_USER" ] && exit 0
        begin_msg "Starting VirtualBox web service" console;
        check_single_user $VBOXWEB_USER
        vboxdrvrunning || {
            fail_msg "VirtualBox kernel module not loaded!"
            exit 0
        }
        PARAMS="--background"
        [ -n "$VBOXWEB_HOST" ]           && PARAMS="$PARAMS -H $VBOXWEB_HOST"
        [ -n "$VBOXWEB_PORT" ]           && PARAMS="$PARAMS -p $VBOXWEB_PORT"
        [ -n "$VBOXWEB_SSL_KEYFILE" ]    && PARAMS="$PARAMS -s -K $VBOXWEB_SSL_KEYFILE"
        [ -n "$VBOXWEB_SSL_PASSWORDFILE" ] && PARAMS="$PARAMS -a $VBOXWEB_SSL_PASSWORDFILE"
        [ -n "$VBOXWEB_SSL_CACERT" ]     && PARAMS="$PARAMS -c $VBOXWEB_SSL_CACERT"
        [ -n "$VBOXWEB_SSL_CAPATH" ]     && PARAMS="$PARAMS -C $VBOXWEB_SSL_CAPATH"
        [ -n "$VBOXWEB_SSL_DHFILE" ]     && PARAMS="$PARAMS -D $VBOXWEB_SSL_DHFILE"
        [ -n "$VBOXWEB_SSL_RANDFILE" ]   && PARAMS="$PARAMS -r $VBOXWEB_SSL_RANDFILE"
        [ -n "$VBOXWEB_TIMEOUT" ]        && PARAMS="$PARAMS -t $VBOXWEB_TIMEOUT"
        [ -n "$VBOXWEB_CHECK_INTERVAL" ] && PARAMS="$PARAMS -i $VBOXWEB_CHECK_INTERVAL"
        [ -n "$VBOXWEB_THREADS" ]        && PARAMS="$PARAMS -T $VBOXWEB_THREADS"
        [ -n "$VBOXWEB_KEEPALIVE" ]      && PARAMS="$PARAMS -k $VBOXWEB_KEEPALIVE"
        [ -n "$VBOXWEB_AUTHENTICATION" ] && PARAMS="$PARAMS -A $VBOXWEB_AUTHENTICATION"
        [ -n "$VBOXWEB_LOGFILE" ]        && PARAMS="$PARAMS -F $VBOXWEB_LOGFILE"
        [ -n "$VBOXWEB_ROTATE" ]         && PARAMS="$PARAMS -R $VBOXWEB_ROTATE"
        [ -n "$VBOXWEB_LOGSIZE" ]        && PARAMS="$PARAMS -S $VBOXWEB_LOGSIZE"
        [ -n "$VBOXWEB_LOGINTERVAL" ]    && PARAMS="$PARAMS -I $VBOXWEB_LOGINTERVAL"
        # set authentication method + password hash
        if [ -n "$VBOXWEB_AUTH_LIBRARY" ]; then
            su - "$VBOXWEB_USER" -c "$vboxmanage setproperty websrvauthlibrary \"$VBOXWEB_AUTH_LIBRARY\""
            if [ $? -ne 0 ]; then
                fail_msg "Error $? setting webservice authentication library to $VBOXWEB_AUTH_LIBRARY"
            fi
        fi
        if [ -n "$VBOXWEB_AUTH_PWHASH" ]; then
            su - "$VBOXWEB_USER" -c "$vboxmanage setextradata global \"VBoxAuthSimple/users/$VBOXWEB_USER\" \"$VBOXWEB_AUTH_PWHASH\""
            if [ $? -ne 0 ]; then
                fail_msg "Error $? setting webservice password hash"
            fi
        fi
        # prevent inheriting this setting to VBoxSVC
        unset VBOX_RELEASE_LOG_DEST
        start_daemon $VBOXWEB_USER $binary $PARAMS > /dev/null 2>&1
        # ugly: wait until the final process has forked
        sleep .1
        PID=`pidof $binary 2>/dev/null`
        if [ -n "$PID" ]; then
            echo "$PID" > $PIDFILE
            RETVAL=0
            succ_msg "VirtualBox web service started"
        else
            RETVAL=1
            fail_msg "VirtualBox web service failed to start"
        fi
    fi
    return $RETVAL
}

stop() {
    if test -f $PIDFILE; then
        begin_msg "Stopping VirtualBox web service" console;
        killproc $binary
        RETVAL=$?
        # Be careful: wait 1 second, making sure that everything is cleaned up.
        sleep 1
        if ! pidof $binary > /dev/null 2>&1; then
            rm -f $PIDFILE
            succ_msg "VirtualBox web service stopped"
        else
            fail_msg "VirtualBox web service failed to stop"
        fi
    fi
    return $RETVAL
}

restart() {
    stop && start
}

status() {
    echo -n "Checking for VBox Web Service"
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
