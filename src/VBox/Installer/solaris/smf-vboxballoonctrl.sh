#!/sbin/sh
# $Id: smf-vboxballoonctrl.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

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

#
# smf-vboxballoonctrl method
#
# Argument is the method name (start, stop, ...)

. /lib/svc/share/smf_include.sh

VW_OPT="$1"
VW_EXIT=0

case $VW_OPT in
    start)
        if [ ! -f /opt/VirtualBox/VBoxBalloonCtrl ]; then
            echo "ERROR: /opt/VirtualBox/VBoxBalloonCtrl does not exist."
            return $SMF_EXIT_ERR_CONFIG
        fi

        if [ ! -x /opt/VirtualBox/VBoxBalloonCtrl ]; then
            echo "ERROR: /opt/VirtualBox/VBoxBalloonCtrl is not executable."
            return $SMF_EXIT_ERR_CONFIG
        fi

        # Get svc configuration
        VBOXWATCHDOG_USER=`/usr/bin/svcprop -p config/user $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VBOXWATCHDOG_USER=
        VBOXWATCHDOG_BALLOON_INTERVAL=`/usr/bin/svcprop -p config/balloon_interval $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VBOXWATCHDOG_BALLOON_INTERVAL=
        VBOXWATCHDOG_BALLOON_INCREMENT=`/usr/bin/svcprop -p config/balloon_increment $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VBOXWATCHDOG_BALLOON_INCREMENT=
        VBOXWATCHDOG_BALLOON_DECREMENT=`/usr/bin/svcprop -p config/balloon_decrement $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VBOXWATCHDOG_BALLOON_DECREMENT=
        VBOXWATCHDOG_BALLOON_LOWERLIMIT=`/usr/bin/svcprop -p config/balloon_lowerlimit $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VBOXWATCHDOG_BALLOON_LOWERLIMIT=
        VBOXWATCHDOG_BALLOON_SAFETYMARGIN=`/usr/bin/svcprop -p config/balloon_safetymargin $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VBOXWATCHDOG_BALLOON_SAFETYMARGIN=
        VBOXWATCHDOG_ROTATE=`/usr/bin/svcprop -p config/logrotate $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VBOXWATCHDOG_ROTATE=
        VBOXWATCHDOG_LOGSIZE=`/usr/bin/svcprop -p config/logsize $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VBOXWATCHDOG_LOGSIZE=
        VBOXWATCHDOG_LOGINTERVAL=`/usr/bin/svcprop -p config/loginterval $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VBOXWATCHDOG_LOGINTERVAL=

        # Handle legacy parameters, do not add any further ones unless absolutely necessary.
        if [ -z "$VBOXWATCHDOG_BALLOON_INTERVAL" ]; then
            VBOXWATCHDOG_BALLOON_INTERVAL=`/usr/bin/svcprop -p config/interval $SMF_FMRI 2>/dev/null`
            [ $? != 0 ] && VBOXWATCHDOG_BALLOON_INTERVAL=
        fi
        if [ -z "$VBOXWATCHDOG_BALLOON_INCREMENT" ]; then
            VBOXWATCHDOG_BALLOON_INCREMENT=`/usr/bin/svcprop -p config/increment $SMF_FMRI 2>/dev/null`
            [ $? != 0 ] && VBOXWATCHDOG_BALLOON_INCREMENT=
        fi
        if [ -z "$VBOXWATCHDOG_BALLOON_DECREMENT" ]; then
            VBOXWATCHDOG_BALLOON_DECREMENT=`/usr/bin/svcprop -p config/decrement $SMF_FMRI 2>/dev/null`
            [ $? != 0 ] && VBOXWATCHDOG_BALLOON_DECREMENT=
        fi
        if [ -z "$VBOXWATCHDOG_BALLOON_LOWERLIMIT" ]; then
            VBOXWATCHDOG_BALLOON_LOWERLIMIT=`/usr/bin/svcprop -p config/lowerlimit $SMF_FMRI 2>/dev/null`
            [ $? != 0 ] && VBOXWATCHDOG_BALLOON_LOWERLIMIT=
        fi
        if [ -z "$VBOXWATCHDOG_BALLOON_SAFETYMARGIN" ]; then
            VBOXWATCHDOG_BALLOON_SAFETYMARGIN=`/usr/bin/svcprop -p config/safetymargin $SMF_FMRI 2>/dev/null`
            [ $? != 0 ] && VBOXWATCHDOG_BALLOON_SAFETYMARGIN=
        fi

        # Provide sensible defaults
        [ -z "$VBOXWATCHDOG_USER" ] && VBOXWATCHDOG_USER=root

        # Assemble the parameter list
        PARAMS="--background"
        [ -n "$VBOXWATCHDOG_BALLOON_INTERVAL" ]     && PARAMS="$PARAMS --balloon-interval \"$VBOXWATCHDOG_BALLOON_INTERVAL\""
        [ -n "$VBOXWATCHDOG_BALLOON_INCREMENT" ]    && PARAMS="$PARAMS --balloon-inc \"$VBOXWATCHDOG_BALLOON_INCREMENT\""
        [ -n "$VBOXWATCHDOG_BALLOON_DECREMENT" ]    && PARAMS="$PARAMS --balloon-dec \"$VBOXWATCHDOG_BALLOON_DECREMENT\""
        [ -n "$VBOXWATCHDOG_BALLOON_LOWERLIMIT" ]   && PARAMS="$PARAMS --balloon-lower-limit \"$VBOXWATCHDOG_BALLOON_LOWERLIMIT\""
        [ -n "$VBOXWATCHDOG_BALLOON_SAFETYMARGIN" ] && PARAMS="$PARAMS --balloon-safety-margin \"$VBOXWATCHDOG_BALLOON_SAFETYMARGIN\""
        [ -n "$VBOXWATCHDOG_ROTATE" ]       && PARAMS="$PARAMS -R \"$VBOXWATCHDOG_ROTATE\""
        [ -n "$VBOXWATCHDOG_LOGSIZE" ]      && PARAMS="$PARAMS -S \"$VBOXWATCHDOG_LOGSIZE\""
        [ -n "$VBOXWATCHDOG_LOGINTERVAL" ]  && PARAMS="$PARAMS -I \"$VBOXWATCHDOG_LOGINTERVAL\""

        exec su - "$VBOXWATCHDOG_USER" -c "/opt/VirtualBox/VBoxBalloonCtrl $PARAMS"

        VW_EXIT=$?
        if [ $VW_EXIT != 0 ]; then
            echo "VBoxBalloonCtrl failed with $VW_EXIT."
            VW_EXIT=1
        fi
    ;;
    stop)
        # Kill service contract
        smf_kill_contract $2 TERM 1
    ;;
    *)
        VW_EXIT=$SMF_EXIT_ERR_CONFIG
    ;;
esac

exit $VW_EXIT
