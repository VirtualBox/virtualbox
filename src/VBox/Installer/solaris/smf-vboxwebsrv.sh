#!/sbin/sh
# $Id: smf-vboxwebsrv.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

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
# smf-vboxwebsrv method
#
# Argument is the method name (start, stop, ...)

. /lib/svc/share/smf_include.sh

VW_OPT="$1"
VW_EXIT=0

case $VW_OPT in
    start)
        if [ ! -f /opt/VirtualBox/vboxwebsrv ]; then
            echo "ERROR: /opt/VirtualBox/vboxwebsrv does not exist."
            return $SMF_EXIT_ERR_CONFIG
        fi

        if [ ! -x /opt/VirtualBox/vboxwebsrv ]; then
            echo "ERROR: /opt/VirtualBox/vboxwebsrv is not executable."
            return $SMF_EXIT_ERR_CONFIG
        fi

        # Get svc configuration
        VW_USER=`/usr/bin/svcprop -p config/user $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_USER=
        VW_HOST=`/usr/bin/svcprop -p config/host $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_HOST=
        VW_PORT=`/usr/bin/svcprop -p config/port $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_PORT=
        VW_SSL_KEYFILE=`/usr/bin/svcprop -p config/ssl_keyfile $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_SSL_KEYFILE=
        VW_SSL_PASSWORDFILE=`/usr/bin/svcprop -p config/ssl_passwordfile $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_SSL_PASSWORDFILE=
        VW_SSL_CACERT=`/usr/bin/svcprop -p config/ssl_cacert $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_SSL_CACERT=
        VW_SSL_CAPATH=`/usr/bin/svcprop -p config/ssl_capath $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_SSL_CAPATH=
        VW_SSL_DHFILE=`/usr/bin/svcprop -p config/ssl_dhfile $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_SSL_DHFILE=
        VW_SSL_RANDFILE=`/usr/bin/svcprop -p config/ssl_randfile $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_SSL_RANDFILE=
        VW_AUTH_LIBRARY=`/usr/bin/svcprop -p config/auth_library $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_AUTH_LIBRARY=
        VW_AUTH_PWHASH=`/usr/bin/svcprop -p config/auth_pwhash $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_AUTH_PWHASH=
        VW_TIMEOUT=`/usr/bin/svcprop -p config/timeout $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_TIMEOUT=
        VW_CHECK_INTERVAL=`/usr/bin/svcprop -p config/checkinterval $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_CHECK_INTERVAL=
        VW_THREADS=`/usr/bin/svcprop -p config/threads $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_THREADS=
        VW_KEEPALIVE=`/usr/bin/svcprop -p config/keepalive $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_KEEPALIVE=
        VW_AUTHENTICATION=`/usr/bin/svcprop -p config/authentication $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_AUTHENTICATION=
        VW_LOGFILE=`/usr/bin/svcprop -p config/logfile $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_LOGFILE=
        VW_ROTATE=`/usr/bin/svcprop -p config/logrotate $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_ROTATE=
        VW_LOGSIZE=`/usr/bin/svcprop -p config/logsize $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_LOGSIZE=
        VW_LOGINTERVAL=`/usr/bin/svcprop -p config/loginterval $SMF_FMRI 2>/dev/null`
        [ $? != 0 ] && VW_LOGINTERVAL=

        # Provide sensible defaults
        [ -z "$VW_USER" ] && VW_USER=root
        [ -z "$VW_HOST" ] && VW_HOST=localhost
        [ -z "$VW_PORT" -o "$VW_PORT" -eq 0 ] && VW_PORT=18083
        [ -z "$VW_TIMEOUT" ] && VW_TIMEOUT=20
        [ -z "$VW_CHECK_INTERVAL" ] && VW_CHECK_INTERVAL=5
        [ -z "$VW_THREADS" ] && VW_THREADS=100
        [ -z "$VW_KEEPALIVE" ] && VW_KEEPALIVE=100
        [ -z "$VW_ROTATE" ] && VW_ROTATE=10
        [ -z "$VW_LOGSIZE" ] && VW_LOGSIZE=104857600
        [ -z "$VW_LOGINTERVAL" ] && VW_LOGINTERVAL=86400

        # Derived and optional settings
        VW_SSL=
        [ -n "$VW_SSL_KEYFILE" ] && VW_SSL=--ssl
        [ -n "$VW_SSL_KEYFILE" ] && VW_SSL_KEYFILE="--keyfile $VW_SSL_KEYFILE"
        [ -n "$VW_SSL_PASSWORDFILE" ] && VW_SSL_PASSWORDFILE="--passwordfile $VW_SSL_PASSWORDFILE"
        [ -n "$VW_SSL_CACERT" ] && VW_SSL_CACERT="--cacert $VW_SSL_CACERT"
        [ -n "$VW_SSL_CAPATH" ] && VW_SSL_CAPATH="--capath $VW_SSL_CAPATH"
        [ -n "$VW_SSL_DHFILE" ] && VW_SSL_DHFILE="--dhfile $VW_SSL_DHFILE"
        [ -n "$VW_SSL_RANDFILE" ] && VW_SSL_RANDFILE="--randfile $VW_SSL_RANDFILE"
        [ -n "$VW_LOGFILE" ] && VW_LOGFILE="--logfile $VW_LOGFILE"

        # Set authentication method + password hash
        if [ -n "$VW_AUTH_LIBRARY" ]; then
            su - "$VW_USER" -c "/opt/VirtualBox/VBoxManage setproperty websrvauthlibrary \"$VW_AUTH_LIBRARY\""
            if [ $? != 0 ]; then
                echo "Error $? setting webservice authentication library to $VW_AUTH_LIBRARY"
            fi
        fi
        if [ -n "$VW_AUTH_PWHASH" ]; then
            su - "$VW_USER" -c "/opt/VirtualBox/VBoxManage setextradata global \"VBoxAuthSimple/users/$VW_USER\" \"$VW_AUTH_PWHASH\""
            if [ $? != 0 ]; then
                echo "Error $? setting webservice password hash"
            fi
        fi

        exec su - "$VW_USER" -c "/opt/VirtualBox/vboxwebsrv --background --host \"$VW_HOST\" --port \"$VW_PORT\" $VW_SSL $VW_SSL_KEYFILE $VW_SSL_PASSWORDFILE $VW_SSL_CACERT $VW_SSL_CAPATH $VW_SSL_DHFILE $VW_SSL_RANDFILE --timeout \"$VW_TIMEOUT\" --check-interval \"$VW_CHECK_INTERVAL\" --threads \"$VW_THREADS\" --keepalive \"$VW_KEEPALIVE\" --authentication \"$VW_AUTHENTICATION\" $VW_LOGFILE --logrotate \"$VW_ROTATE\" --logsize \"$VW_LOGSIZE\" --loginterval \"$VW_LOGINTERVAL\""

        VW_EXIT=$?
        if [ $VW_EXIT != 0 ]; then
            echo "vboxwebsrv failed with $VW_EXIT."
            VW_EXIT=1
        fi
    ;;
    stop)
        # Kill service contract
        smf_kill_contract $2 TERM 1
        # Be careful: wait 1 second, making sure that everything is cleaned up.
        smf_kill_contract $2 TERM 1
    ;;
    *)
        VW_EXIT=$SMF_EXIT_ERR_CONFIG
    ;;
esac

exit $VW_EXIT
