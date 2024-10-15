#!/sbin/sh
# $Id: smf-vboxautostart.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

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

#
# smf-vboxautostart method
#
# Argument is the method name (start, stop, ...)

. /lib/svc/share/smf_include.sh

VW_OPT="$1"
VW_EXIT=0

case $VW_OPT in
    start)
        if [ ! -f /opt/VirtualBox/VBoxAutostart ]; then
            echo "ERROR: /opt/VirtualBox/VBoxAutostart does not exist."
            return $SMF_EXIT_ERR_CONFIG
        fi

        if [ ! -x /opt/VirtualBox/VBoxAutostart ]; then
            echo "ERROR: /opt/VirtualBox/VBoxAutostart is not exectuable."
            return $SMF_EXIT_ERR_CONFIG
        fi

        # Get svc configuration
        VW_CONFIG=`/usr/bin/svcprop -p config/config $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_CONFIG=
        VW_ROTATE=`/usr/bin/svcprop -p config/logrotate $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_ROTATE=
        VW_LOGSIZE=`/usr/bin/svcprop -p config/logsize $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_LOGSIZE=
        VW_LOGINTERVAL=`/usr/bin/svcprop -p config/loginterval $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_LOGINTERVAL=
        VW_VBOXGROUP=`/usr/bin/svcprop -p config/vboxgroup $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_VBOXGROUP=

        # Provide sensible defaults
        [ -z "$VW_CONFIG" ] && VW_CONFIG=/etc/vbox/autostart.cfg
        [ -z "$VW_ROTATE" ] && VW_ROTATE=10
        [ -z "$VW_LOGSIZE" ] && VW_LOGSIZE=104857600
        [ -z "$VW_LOGINTERVAL" ] && VW_LOGINTERVAL=86400
        [ -z "$VW_VBOXGROUP" ] && VW_VBOXGROUP=staff

        # Get all users
        for VW_USER in `logins -g $VW_VBOXGROUP | cut -d' ' -f1`
        do
            su - "$VW_USER" -c "/opt/VirtualBox/VBoxAutostart --background --start --config \"$VW_CONFIG\" --logrotate \"$VW_ROTATE\" --logsize \"$VW_LOGSIZE\" --loginterval \"$VW_LOGINTERVAL\""

            VW_EXIT=$?
            if [ $VW_EXIT != 0 ]; then
                echo "VBoxAutostart failed with $VW_EXIT."
                VW_EXIT=1
                break
            fi
        done
    ;;
    stop)
        if [ ! -f /opt/VirtualBox/VBoxAutostart ]; then
            echo "ERROR: /opt/VirtualBox/VBoxAutostart does not exist."
            return $SMF_EXIT_ERR_CONFIG
        fi

        if [ ! -x /opt/VirtualBox/VBoxAutostart ]; then
            echo "ERROR: /opt/VirtualBox/VBoxAutostart is not executable."
            return $SMF_EXIT_ERR_CONFIG
        fi

        # Get svc configuration
        VW_CONFIG=`/usr/bin/svcprop -p config/config $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_CONFIG=
        VW_ROTATE=`/usr/bin/svcprop -p config/logrotate $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_ROTATE=
        VW_LOGSIZE=`/usr/bin/svcprop -p config/logsize $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_LOGSIZE=
        VW_LOGINTERVAL=`/usr/bin/svcprop -p config/loginterval $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_LOGINTERVAL=
        VW_VBOXGROUP=`/usr/bin/svcprop -p config/vboxgroup $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_VBOXGROUP=

        # Provide sensible defaults
        [ -z "$VW_CONFIG" ] && VW_CONFIG=/etc/vbox/autostart.cfg
        [ -z "$VW_ROTATE" ] && VW_ROTATE=10
        [ -z "$VW_LOGSIZE" ] && VW_LOGSIZE=104857600
        [ -z "$VW_LOGINTERVAL" ] && VW_LOGINTERVAL=86400
        [ -z "$VW_VBOXGROUP" ] && VW_VBOXGROUP=staff

        # Get all users
        for VW_USER in `logins -g $VW_VBOXGROUP | cut -d' ' -f1`
        do
            su - "$VW_USER" -c "/opt/VirtualBox/VBoxAutostart --stop --config \"$VW_CONFIG\" --logrotate \"$VW_ROTATE\" --logsize \"$VW_LOGSIZE\" --loginterval \"$VW_LOGINTERVAL\""

            VW_EXIT=$?
            if [ $VW_EXIT != 0 ]; then
                echo "VBoxAutostart failed with $VW_EXIT."
                VW_EXIT=1
                break
            fi
        done
    ;;
    *)
        VW_EXIT=$SMF_EXIT_ERR_CONFIG
    ;;
esac

exit $VW_EXIT
