#!/bin/sh
# $Id: vboxautostart-service.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox autostart service init script.
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

# chkconfig: 345 35 65
# description: VirtualBox autostart service
#
### BEGIN INIT INFO
# Provides:       vboxautostart-service
# Required-Start: vboxdrv
# Required-Stop:  vboxdrv
# Default-Start:  2 3 4 5
# Default-Stop:   0 1 6
# Description:    VirtualBox autostart service
### END INIT INFO

PATH=$PATH:/bin:/sbin:/usr/sbin
SCRIPTNAME=vboxautostart-service.sh

[ -f /etc/debian_release -a -f /lib/lsb/init-functions ] || NOLSB=yes
[ -f /etc/vbox/vbox.cfg ] && . /etc/vbox/vbox.cfg

if [ -n "$INSTALL_DIR" ]; then
    binary="$INSTALL_DIR/VBoxAutostart"
else
    binary="/usr/lib/virtualbox/VBoxAutostart"
fi

# silently exit if the package was uninstalled but not purged,
# applies to Debian packages only (but shouldn't hurt elsewhere)
[ ! -f /etc/debian_release -o -x $binary ] || exit 0

[ -r /etc/default/virtualbox ] && . /etc/default/virtualbox

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

if which start-stop-daemon >/dev/null 2>&1; then
    start_daemon() {
        usr="$1"
        shift
        bin="$1"
        shift
        start-stop-daemon --chuid $usr --start --exec $bin -- $@
    }
fi

vboxdrvrunning() {
    lsmod | grep -q "vboxdrv[^_-]"
}

valid_db_entry() {

    entry="$1"
    [ -z "$entry" ] && return 1

    user="$2"
    [ -z "$user" ] && return 1

    user_name=$(id -n -u "$user" 2>/dev/null)
    [ -z "$user_name" ] && return 1

    user_id=$(id -u "$user" 2>/dev/null)

    # Verify that @user identifies a user *by name* (i.e. not a numeric id).
    # Careful, all numeric user names are legal.
    if [ "$user_id" = "$user" ] && [ "$user_name" != "$user" ]; then
        return 1
    fi

    # Verify whether file name is the same as file owner name.
    [ -z "$(find "$entry" -user "$user" -type f 2>/dev/null)" ] && return 1

    return 0
}

start() {
    [ -z "$VBOXAUTOSTART_DB" ] && exit 0
    [ -z "$VBOXAUTOSTART_CONFIG" ] && exit 0
    begin_msg "Starting VirtualBox VMs configured for autostart" console;
    vboxdrvrunning || {
        fail_msg "VirtualBox kernel module not loaded!"
        exit 0
    }
    PARAMS="--background --start --config $VBOXAUTOSTART_CONFIG"

    # prevent inheriting this setting to VBoxSVC
    unset VBOX_RELEASE_LOG_DEST

    for entry in "$VBOXAUTOSTART_DB"/*.start
    do
        user=$(basename "$entry" .start)
        [ "$user" = "*" ] && break
        valid_db_entry "$entry" "$user" || continue

        start_daemon "$user" "$binary" $PARAMS > /dev/null 2>&1
    done

    return $RETVAL
}

stop() {
    [ -z "$VBOXAUTOSTART_DB" ] && exit 0
    [ -z "$VBOXAUTOSTART_CONFIG" ] && exit 0

    PARAMS="--stop --config $VBOXAUTOSTART_CONFIG"

    # prevent inheriting this setting to VBoxSVC
    unset VBOX_RELEASE_LOG_DEST

    for entry in "$VBOXAUTOSTART_DB"/*.stop
    do
        user=$(basename "$entry" .stop)
        [ "$user" = "*" ] && break
        valid_db_entry "$entry" "$user" || continue

        start_daemon "$user" "$binary" $PARAMS > /dev/null 2>&1
    done

    return $RETVAL
}

case "$1" in
start)
    start
    ;;
stop)
    stop
    ;;
*)
    echo "Usage: $0 {start|stop}"
    exit 1
esac

exit $RETVAL
