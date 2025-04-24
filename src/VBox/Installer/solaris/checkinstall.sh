#!/bin/sh
# $Id: checkinstall.sh 109330 2025-04-24 15:58:01Z brent.paulson@oracle.com $
## @file
#
# VirtualBox checkinstall script for Solaris.
#

#
# Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

infoprint()
{
    echo 1>&2 "$1"
}

errorprint()
{
    echo 1>&2 "## $1"
}

abort_error()
{
    errorprint "Please close all VirtualBox processes and re-run this installer."
    exit 1
}

checkdep_ips()
{
    if test -z "$1"; then
        errorprint "Missing argument to checkdep_ips"
        return 1
    fi
    # using "list" without "-a" only lists installed pkgs which is what we need
    $BIN_PKG $BASEDIR_OPT list "$1" >/dev/null 2>&1
    if test "$?" -eq 0; then
        return 0
    fi
    PKG_MISSING_IPS="$PKG_MISSING_IPS $1"
    return 1
}

checkdep_ips_python()
{
    if test -z "$1"; then
        errorprint "Missing argument to checkdep_ips_python"
        return 1
    fi

    # Check installed packages for any python runtime from the argument list passed
    # to this function.  We can't use 'pkg list -q' here since it didn't exist until
    # S11.2 FCS (CR 16663535).
    for pkg in "${@}"; do
        $BIN_PKG $BASEDIR_OPT list "${pkg}" >/dev/null 2>&1
        if test "$?" -eq 0; then
            return 0
        fi
    done

    # 'pkg info' Branch fields:
    # For S11-11.3: TrunkID|Update|SRU|Reserved|BuildID|NightlyID
    # For S11.4:    TrunkID|Update|SRU|Reserved|Reserved|BuildID|NightlyID
    #   N.B. For S11-11.3: TrunkID=0.175  For S11.4: TrunkID=11
    # Example Solaris 11 FMRIs:
    # Solaris 11 pre-FCS = 5.12.0.0.0.128.1
    # Solaris 11 FCS =     0.175.0.0.0.2.0
    # Solaris 11.1 SRU21 = 0.175.1.21.0.4.1
    # Solaris 11.2 SRU15 = 0.175.2.14.0.2.2
    # Solaris 11.3 SRU36 = 0.175.3.36.0.8.0
    # Solaris 11.4 SRU55 = 11.4.55.0.1.138.3
    eval `$BIN_PKG $BASEDIR_OPT info system/kernel | \
        awk '/Branch:/ { split($2, array, "."); if ($2 == 175) {print "export UPDATE="array[3] " SRU="array[4];} \
            else {print "export UPDATE="array[2] " SRU="array[3]} }'`

    # If the parsing of the pkg FMRI failed for any reason then default to the first
    # Solaris 11.4 CBE which was released relatively recently (March 2022) and is
    # freely available.
    if test -z "$UPDATE"; then
        export UPDATE=4 SRU=42
    fi

    # Map of introduction and removal of python releases in Solaris 11 releases/SRUs:
    # python 2.6:  S11 FCS -> S11.3 SRU19 [removed in S11.3 SRU20]
    # python 2.7:  S11 FCS -> S11.4 SRU56 [removed in S11.4 SRU57]
    # python 3.4:  S11.3 FCS -> S11.4 SRU26 [removed in S11.4 SRU27]
    # python 3.5:  S11.4 FCS -> S11.4 SRU29 [removed in S11.4 SRU30]
    # python 3.7:  S11.4 SRU4 -> S11.4 SRU71 [removed in S11.4 SRU72]
    # python 3.9:  S11.4 SRU30 -> TBD
    # python 3.11: S11.4 SRU54 -> TBD
    # python 3.13: S11.4 SRU78 -> TBD
    if test "$UPDATE" -lt 3 || test "$UPDATE" -gt 4; then  # S11 FCS - S11.2 SRU15 or anything before S11 FCS
        PKG_MISSING_IPS="$PKG_MISSING_IPS runtime/python-26 or runtime/python-27"
    elif test "$UPDATE" -eq 3 && test "$SRU" -le 19; then  # S11.3 FCS - S11.3 SRU19
        PKG_MISSING_IPS="$PKG_MISSING_IPS runtime/python-26 or runtime/python-27 or runtime/python-34"
    elif test "$UPDATE" -eq 3 && test "$SRU" -gt 19; then  # S11.3 SRU20 - S11.3 SRU<latest>
        PKG_MISSING_IPS="$PKG_MISSING_IPS runtime/python-27 or runtime/python-34"
    elif test "$UPDATE" -eq 4 && test "$SRU" -le 3; then  # S11.4 FCS - S11.4 SRU3
        PKG_MISSING_IPS="$PKG_MISSING_IPS runtime/python-27 or runtime/python-34 or runtime/python-35"
    elif test "$UPDATE" -eq 4 && test "$SRU" -le 26; then  # S11.4 SRU4 - S11.4 SRU26
        PKG_MISSING_IPS="$PKG_MISSING_IPS runtime/python-27 or runtime/python-34 or runtime/python-35 or runtime/python-37"
    elif test "$UPDATE" -eq 4 && test "$SRU" -le 29; then  # S11.4 SRU27 - S11.4 SRU29
        PKG_MISSING_IPS="$PKG_MISSING_IPS runtime/python-27 or runtime/python-35 or runtime/python-37"
    elif test "$UPDATE" -eq 4 && test "$SRU" -le 53; then  # S11.4 SRU30 - S11.4 SRU53
        PKG_MISSING_IPS="$PKG_MISSING_IPS runtime/python-27 or runtime/python-37 or runtime/python-39"
    elif test "$UPDATE" -eq 4 && test "$SRU" -le 56; then  # S11.4 SRU54 - S11.4 SRU56
        PKG_MISSING_IPS="$PKG_MISSING_IPS runtime/python-27 or runtime/python-37 or runtime/python-39 or runtime/python-311"
    elif test "$UPDATE" -eq 4 && test "$SRU" -le 71; then  # S11.4 SRU57 - S11.4 SRU71
        PKG_MISSING_IPS="$PKG_MISSING_IPS runtime/python-37 or runtime/python-39 or runtime/python-311"
    elif test "$UPDATE" -eq 4 && test "$SRU" -gt 71; then  # S11.4 SRU72 - S11.4 SRU<latest>
        PKG_MISSING_IPS="$PKG_MISSING_IPS runtime/python-39 or runtime/python-311 or runtime/python-313"
    else # Fall through just in case.
        PKG_MISSING_IPS="$PKG_MISSING_IPS runtime/python-39 or runtime/python-311 or runtime/python-313"
    fi

    return 1
}

checkdep_ips_either()
{
    if test -z "$1" || test -z "$2"; then
        errorprint "Missing argument to checkdep_ips_either"
        return 1
    fi
    # using "list" without "-a" only lists installed pkgs which is what we need
    $BIN_PKG $BASEDIR_OPT list "$1" >/dev/null 2>&1
    if test "$?" -eq 0; then
        return 0
    fi
    $BIN_PKG $BASEDIR_OPT list "$2" >/dev/null 2>&1
    if test "$?" -eq 0; then
        return 0
    fi
    PKG_MISSING_IPS="$PKG_MISSING_IPS $1 or $2"
    return 1
}

disable_service()
{
    if test -z "$1" || test -z "$2"; then
        errorprint "Missing argument to disable_service"
        return 1
    fi
    servicefound=`$BIN_SVCS -H "$1" 2> /dev/null | grep '^online'`
    if test ! -z "$servicefound"; then
        infoprint "$2 ($1) is still enabled. Disabling..."
        $BIN_SVCADM disable -s "$1"
        # Don't delete the service, handled by manifest class action
        # /usr/sbin/svccfg delete $1
    fi
}

# find_bin_path()
# !! failure is always fatal
find_bin_path()
{
    if test -z "$1"; then
        errorprint "missing argument to find_bin_path()"
        exit 1
    fi

    binfilename=`basename $1`
    binfilepath=`which $binfilename 2> /dev/null`
    if test -x "$binfilepath"; then
        echo "$binfilepath"
        return 0
    else
        errorprint "$1 missing or is not an executable"
        exit 1
    fi
}

# find_bins()
# !! failure is always fatal
find_mandatory_bins()
{
    # Search only for binaries that might be in different locations
    if test ! -x "$BIN_SVCS"; then
        BIN_SVCS=`find_bin_path "$BIN_SVCS"`
    fi

    if test ! -x "$BIN_SVCADM"; then
        BIN_SVCADM=`find_bin_path "$BIN_SVCADM"`
    fi
}


#
# Begin execution
#

# Nothing to check for remote install
REMOTE_INST=0
if test "x${PKG_INSTALL_ROOT:=/}" != "x/"; then
    BASEDIR_OPT="-R $PKG_INSTALL_ROOT"
    REMOTE_INST=1
fi

# Nothing to check for non-global zones
currentzone=`zonename`
if test "x$currentzone" != "xglobal"; then
    exit 0
fi

PKG_MISSING_IPS=""
BIN_PKG=/usr/bin/pkg
BIN_SVCS=/usr/bin/svcs
BIN_SVCADM=/usr/sbin/svcadm

# Check non-optional binaries
find_mandatory_bins

infoprint "Checking package dependencies..."

if test -x "$BIN_PKG"; then
    checkdep_ips "system/library/iconv/iconv-core"
    checkdep_ips "x11/library/libice"
    checkdep_ips "x11/library/libsm"
    checkdep_ips "x11/library/libx11"
    checkdep_ips "x11/library/libxcb"
    checkdep_ips "x11/library/libxext"
    checkdep_ips "x11/library/libxfixes"
    checkdep_ips "x11/library/libxkbcommon"
    checkdep_ips "x11/library/libxrender"
    checkdep_ips "x11/library/mesa"
    checkdep_ips "x11/library/toolkit/libxt"
    checkdep_ips "x11/library/xcb-util"
    checkdep_ips_python "runtime/python-26" "runtime/python-27" "runtime/python-34" "runtime/python-35" "runtime/python-37" "runtime/python-39" "runtime/python-311" "runtime/python-313"
    checkdep_ips_either "system/library/gcc/gcc-c++-runtime" "system/library/gcc/gcc-c++-runtime-9"
    checkdep_ips_either "system/library/gcc/gcc-c-runtime" "system/library/gcc/gcc-c-runtime-9"
else
    PKG_MISSING_IPS="runtime/python-37 system/library/iconv/iconv-core system/library/gcc/gcc-c++-runtime-9 system/library/gcc/gcc-c-runtime-9"
fi

if test "x$PKG_MISSING_IPS" != "x"; then
    if test ! -x "$BIN_PKG"; then
        errorprint "Missing or non-executable binary: pkg ($BIN_PKG)."
        errorprint "Cannot check for dependencies."
        errorprint ""
        errorprint "Please install one of the required packaging system."
        exit 1
    fi
    errorprint "Missing packages: $PKG_MISSING_IPS"
    errorprint ""
    errorprint "Please install these packages before installing VirtualBox."
    exit 1
else
    infoprint "Done."
fi

# Nothing more to do for remote installs
if test "$REMOTE_INST" -eq 1; then
    exit 0
fi

# Check & disable running services
disable_service "svc:/application/virtualbox/zoneaccess"  "VirtualBox zone access service"
disable_service "svc:/application/virtualbox/webservice"  "VirtualBox web service"
disable_service "svc:/application/virtualbox/autostart"   "VirtualBox auto-start service"
disable_service "svc:/application/virtualbox/balloonctrl" "VirtualBox balloon-control service"

# Check if VBoxSVC is currently running
VBOXSVC_PID=`ps -eo pid,fname | grep VBoxSVC | grep -v grep | awk '{ print $1 }'`
if test ! -z "$VBOXSVC_PID" && test "$VBOXSVC_PID" -ge 0; then
    errorprint "VirtualBox's VBoxSVC (pid $VBOXSVC_PID) still appears to be running."
    abort_error
fi

# Check if VBoxNetDHCP is currently running
VBOXNETDHCP_PID=`ps -eo pid,fname | grep VBoxNetDHCP | grep -v grep | awk '{ print $1 }'`
if test ! -z "$VBOXNETDHCP_PID" && test "$VBOXNETDHCP_PID" -ge 0; then
    errorprint "VirtualBox's VBoxNetDHCP (pid $VBOXNETDHCP_PID) still appears to be running."
    abort_error
fi

# Check if VBoxNetNAT is currently running
VBOXNETNAT_PID=`ps -eo pid,fname | grep VBoxNetNAT | grep -v grep | awk '{ print $1 }'`
if test ! -z "$VBOXNETNAT_PID" && test "$VBOXNETNAT_PID" -ge 0; then
    errorprint "VirtualBox's VBoxNetNAT (pid $VBOXNETNAT_PID) still appears to be running."
    abort_error
fi

# Check if vboxnet is still plumbed, if so try unplumb it
BIN_IFCONFIG=`which ifconfig 2> /dev/null`
if test -x "$BIN_IFCONFIG"; then
    vboxnetup=`$BIN_IFCONFIG vboxnet0 >/dev/null 2>&1`
    if test "$?" -eq 0; then
        infoprint "VirtualBox NetAdapter is still plumbed"
        infoprint "Trying to remove old NetAdapter..."
        $BIN_IFCONFIG vboxnet0 unplumb
        if test "$?" -ne 0; then
            errorprint "VirtualBox NetAdapter 'vboxnet0' couldn't be unplumbed (probably in use)."
            abort_error
        fi
    fi
    vboxnetup=`$BIN_IFCONFIG vboxnet0 inet6 >/dev/null 2>&1`
    if test "$?" -eq 0; then
        infoprint "VirtualBox NetAdapter (Ipv6) is still plumbed"
        infoprint "Trying to remove old NetAdapter..."
        $BIN_IFCONFIG vboxnet0 inet6 unplumb
        if test "$?" -ne 0; then
            errorprint "VirtualBox NetAdapter 'vboxnet0' IPv6 couldn't be unplumbed (probably in use)."
            abort_error
        fi
    fi
fi

# Make sure that SMF has finished removing any services left over from a
# previous installation which may interfere with installing new ones.
# This is only relevant on Solaris 11 for SysV packages.
#
# See BugDB 14838646 for the original problem and @bugref{7866} for
# follow up fixes.
for i in 1 2 3 4 5 6 7 8 9 10; do
    $BIN_SVCS -H "svc:/application/virtualbox/autostart"   >/dev/null 2>&1 ||
    $BIN_SVCS -H "svc:/application/virtualbox/webservice"  >/dev/null 2>&1 ||
    $BIN_SVCS -H "svc:/application/virtualbox/zoneaccess"  >/dev/null 2>&1 ||
    $BIN_SVCS -H "svc:/application/virtualbox/balloonctrl" >/dev/null 2>&1 || break
    if test "${i}" = "1"; then
        printf "Waiting for services from previous installation to be removed."
    elif test "${i}" = "10"; then
        printf "\nWarning!!! Some service(s) still appears to be present"
    else
        printf "."
    fi
    sleep 1
done
test "${i}" = "1" || printf "\n"

exit 0

