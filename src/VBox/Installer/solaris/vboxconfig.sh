#!/bin/sh
# $Id: vboxconfig.sh 109330 2025-04-24 15:58:01Z brent.paulson@oracle.com $
## @file
# VirtualBox Configuration Script, Solaris host.
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

# Never use exit 2 or exit 20 etc., the return codes are used in
# SRv4 postinstall procedures which carry special meaning. Just use exit 1 for failure.

# LC_ALL should take precedence over LC_* and LANG but whatever...
LC_ALL=C
export LC_ALL

LANG=C
export LANG

VBOX_INSTALL_PATH="$PKG_INSTALL_ROOT/opt/VirtualBox"
CONFIG_DIR=/etc/vbox
CONFIG_FILES=filelist
DIR_CONF="$PKG_INSTALL_ROOT/platform/i86pc/kernel/drv"
DIR_MOD_32="$PKG_INSTALL_ROOT/platform/i86pc/kernel/drv"
DIR_MOD_64="$DIR_MOD_32/amd64"

# Default paths, these will be overridden by 'which' if they don't exist
BIN_ADDDRV=/usr/sbin/add_drv
BIN_REMDRV=/usr/sbin/rem_drv
BIN_MODLOAD=/usr/sbin/modload
BIN_MODUNLOAD=/usr/sbin/modunload
BIN_MODINFO=/usr/sbin/modinfo
BIN_DEVFSADM=/usr/sbin/devfsadm
BIN_BOOTADM=/sbin/bootadm
BIN_SVCADM=/usr/sbin/svcadm
BIN_SVCCFG=/usr/sbin/svccfg
BIN_SVCS=/usr/bin/svcs
BIN_IFCONFIG=/sbin/ifconfig
BIN_SVCS=/usr/bin/svcs
BIN_ID=/usr/bin/id
BIN_PKILL=/usr/bin/pkill
BIN_PGREP=/usr/bin/pgrep
BIN_IPADM=/usr/sbin/ipadm

# "vboxdrv" is also used in sed lines here (change those as well if it ever changes)
MOD_VBOXDRV=vboxdrv
DESC_VBOXDRV="Host"

MOD_VBOXNET=vboxnet
DESC_VBOXNET="NetAdapter"
MOD_VBOXNET_INST=8

MOD_VBOXFLT=vboxflt
DESC_VBOXFLT="NetFilter (STREAMS)"

MOD_VBOXBOW=vboxbow
DESC_VBOXBOW="NetFilter (Crossbow)"

MOD_VBOXUSBMON=vboxusbmon
DESC_VBOXUSBMON="USBMonitor"

MOD_VBOXUSB=vboxusb
DESC_VBOXUSB="USB"

UPDATEBOOTARCHIVE=0
REMOTEINST=0
FATALOP=fatal
NULLOP=nulloutput
SILENTOP=silent
IPSOP=ips
ISSILENT=
ISIPS=

infoprint()
{
    if test "x$ISSILENT" != "x$SILENTOP"; then
        echo 1>&2 "$1"
    fi
}

subprint()
{
    if test "x$ISSILENT" != "x$SILENTOP"; then
        echo 1>&2 "   - $1"
    fi
}

warnprint()
{
    if test "x$ISSILENT" != "x$SILENTOP"; then
        echo 1>&2 "   * Warning!! $1"
    fi
}

errorprint()
{
    echo 1>&2 "## $1"
}

helpprint()
{
    echo 1>&2 "$1"
}

printusage()
{
    helpprint "VirtualBox Configuration Script"
    helpprint "usage: $0 <operation> [options]"
    helpprint
    helpprint "<operation> must be one of the following:"
    helpprint "  --postinstall      Perform full post installation procedure"
    helpprint "  --preremove        Perform full pre remove procedure"
    helpprint "  --installdrivers   Only install the drivers"
    helpprint "  --removedrivers    Only remove the drivers"
    helpprint "  --setupdrivers     Setup drivers, reloads existing drivers"
    helpprint
    helpprint "[options] are one or more of the following:"
    helpprint "  --silent           Silent mode"
    helpprint "  --fatal            Don't continue on failure (required for postinstall)"
    helpprint "  --ips              This is an IPS package postinstall/preremove"
    helpprint "  --altkerndir       Use /usr/kernel/drv as the driver directory"
    helpprint
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
find_bins()
{
    # Search only for binaries that might be in different locations
    if test ! -x "$BIN_ID"; then
        BIN_ID=`find_bin_path "$BIN_ID"`
    fi

    if test ! -x "$BIN_ADDDRV"; then
        BIN_ADDDRV=`find_bin_path "$BIN_ADDDRV"`
    fi

    if test ! -x "$BIN_REMDRV"; then
        BIN_REMDRV=`find_bin_path "$BIN_REMDRV"`
    fi

    if test ! -x "$BIN_MODLOAD"; then
        BIN_MODLOAD=`check_bin_path "$BIN_MODLOAD"`
    fi

    if test ! -x "$BIN_MODUNLOAD"; then
        BIN_MODUNLOAD=`find_bin_path "$BIN_MODUNLOAD"`
    fi

    if test ! -x "$BIN_MODINFO"; then
        BIN_MODINFO=`find_bin_path "$BIN_MODINFO"`
    fi

    if test ! -x "$BIN_DEVFSADM"; then
        BIN_DEVFSADM=`find_bin_path "$BIN_DEVFSADM"`
    fi

    if test ! -x "$BIN_BOOTADM"; then
        BIN_BOOTADM=`find_bin_path "$BIN_BOOTADM"`
    fi

    if test ! -x "$BIN_SVCADM"; then
        BIN_SVCADM=`find_bin_path "$BIN_SVCADM"`
    fi

    if test ! -x "$BIN_SVCCFG"; then
        BIN_SVCCFG=`find_bin_path "$BIN_SVCCFG"`
    fi

    if test ! -x "$BIN_SVCS"; then
        BIN_SVCS=`find_bin_path "$BIN_SVCS"`
    fi

    if test ! -x "$BIN_IFCONFIG"; then
        BIN_IFCONFIG=`find_bin_path "$BIN_IFCONFIG"`
    fi

    if test ! -x "$BIN_PKILL"; then
        BIN_PKILL=`find_bin_path "$BIN_PKILL"`
    fi

    if test ! -x "$BIN_PGREP"; then
        BIN_PGREP=`find_bin_path "$BIN_PGREP"`
    fi

    if test ! -x "$BIN_IPADM"; then
        BIN_IPADM=`find_bin_path "$BIN_IPADM"`
    fi
}

# check_root()
# !! failure is always fatal
check_root()
{
    # Don't use "-u" option as some id binaries don't support it, instead
    # rely on "uid=101(username) gid=10(groupname) groups=10(staff)" output
    curuid=`$BIN_ID | cut -f 2 -d '=' | cut -f 1 -d '('`
    if test "$curuid" -ne 0; then
        errorprint "This script must be run with administrator privileges."
        exit 1
    fi
}

# get_unofficial_sysinfo()
# cannot fail
get_unofficial_sysinfo()
{
    HOST_OS_MAJORVERSION="11"
    HOST_OS_MINORVERSION="151"
}

# get_s11_4_sysinfo()
# cannot fail
get_s11_4_sysinfo()
{
    # See check in plumb_net for why this is > 174. The alternative is we declare 11.4+ as S12 with
    # a more accurate minor (build) version number. For now this is sufficient to workaround the ever
    # changing version numbering policy.
    HOST_OS_MAJORVERSION="11"
    HOST_OS_MINORVERSION="175"
}

# get_s11_5_or_newer_sysinfo()
# cannot fail
get_s11_5_or_newer_sysinfo()
{
    # See check in plumb_net for why this is 176.
    HOST_OS_MAJORVERSION="11"
    HOST_OS_MINORVERSION="176"
}

# get_sysinfo()
# cannot fail
get_sysinfo()
{
    STR_OSVER=`uname -v`
    case "$STR_OSVER" in
        # First check 'uname -v' and weed out the recognized, unofficial distros of Solaris
        omnios*|oi_*|illumos*)
            get_unofficial_sysinfo
            return 0
            ;;
        # Quick escape workaround for Solaris 11.4, changes the pkg FMRI (yet again). See BugDB #26494983.
        11.4.*)
            get_s11_4_sysinfo
            return 0
            ;;
        # Quick escape workaround for Solaris 11.5. See BugDB #26494983.
        11.5.*)
            get_s11_5_or_newer_sysinfo
            return 0
    esac

    BIN_PKG=`which pkg 2> /dev/null`
    if test -x "$BIN_PKG"; then
        PKGFMRI=`$BIN_PKG $BASEDIR_PKGOPT contents -H -t set -a name=pkg.fmri -o pkg.fmri pkg:/system/kernel 2> /dev/null`
        if test -z "$PKGFMRI"; then
            # Perhaps this is old pkg without '-a' option and/or system/kernel is missing and it's part of 'entire'
            # Try fallback.
            PKGFMRI=`$BIN_PKG $BASEDIR_PKGOPT contents -H -t set -o pkg.fmri entire | head -1 2> /dev/null`
            if test -z "$PKGFMRI"; then
                # Perhaps entire is conflicting. Try using opensolaris/entire.
                # Last fallback try.
                PKGFMRI=`$BIN_PKG $BASEDIR_PKGOPT contents -H -t set -o pkg.fmri opensolaris.org/entire | head -1 2> /dev/null`
            fi
        fi
        if test ! -z "$PKGFMRI"; then
            # The format is "pkg://solaris/system/kernel@0.5.11,5.11-0.161:20110315T070332Z"
            #            or "pkg://solaris/system/kernel@5.12,5.11-5.12.0.0.0.4.1:20120908T030246Z"
            #            or "pkg://solaris/system/kernel@0.5.11,5.11-0.175.0.0.0.1.0:20111012T032837Z"
            #            or "pkg://solaris/system/kernel@5.12-5.12.0.0.0.9.1.3.0:20121012T032837Z" [1]
            # [1]: The sed below doesn't handle this. It's instead parsed below in the PSARC/2012/240 case.
            STR_KERN_MAJOR=`echo "$PKGFMRI" | sed 's/^.*\@//;s/\,.*//'`
            if test ! -z "$STR_KERN_MAJOR"; then
                # The format is "0.5.11" or "5.12"
                # Let us just hardcode these for now, instead of trying to do things more generically. It's not
                # worth trying to bring more order to chaos as it's clear that the version numbering is subject to breakage
                # as it has been seen in the past.
                if test "x$STR_KERN_MAJOR" = "x5.12"; then
                    HOST_OS_MAJORVERSION="12"
                elif test "x$STR_KERN_MAJOR" = "x0.5.11" || test "x$STR_KERN_MAJOR" = "x5.11"; then
                    HOST_OS_MAJORVERSION="11"
                else
                    # This could be the PSARC/2012/240 naming scheme for S12.
                    # The format is "pkg://solaris/system/kernel@5.12-5.12.0.0.0.9.1.3.0:20121012T032837Z"
                    # The "5.12" following the "@" is the nominal version which we ignore for now as it is
                    # not set by most pkg(5) tools...
                    # STR_KERN_MAJOR is now of the format "5.12-5.12.0.0.0.9.1.3.0:20121012T032837Z" with '9' representing
                    # the build number.
                    BRANCH_VERSION=$STR_KERN_MAJOR
                    HOST_OS_MAJORVERSION=`echo "$BRANCH_VERSION" | cut -f2 -d'-' | cut -f1,2 -d'.'`
                    if test "x$HOST_OS_MAJORVERSION" = "x5.12"; then
                        HOST_OS_MAJORVERSION="12"
                        HOST_OS_MINORVERSION=`echo "$BRANCH_VERSION" | cut -f2 -d'-' | cut -f6 -d'.'`
                        return 0
                    else
                        errorprint "Failed to parse the Solaris kernel major version."
                        exit 1
                    fi
                fi

                # This applies only to S11 and S12 where the transitional "@5.12," component version is
                # still part of the pkg(5) package FMRI. The regular S12 will follow the PSARC/2012/240 naming scheme above.
                STR_KERN_MINOR=`echo "$PKGFMRI" | sed 's/^.*\@//;s/\:.*//;s/.*,//'`
                if test ! -z "$STR_KERN_MINOR"; then
                    # The HOST_OS_MINORVERSION is represented as follows:
                    # For S12 it represents the build numbers. e.g. for 4  :  "5.11-5.12.0.0.0.4.1"
                    # For S11 as the "nevada" version numbers. e.g. for 175:  "5.11-0.161" or "5.11-0.175.0.0.0.1.0"
                    if test "$HOST_OS_MAJORVERSION" -eq 12; then
                        HOST_OS_MINORVERSION=`echo "$STR_KERN_MINOR" | cut -f2 -d'-' | cut -f6 -d'.'`
                    elif test "$HOST_OS_MAJORVERSION" -eq 11; then
                        HOST_OS_MINORVERSION=`echo "$STR_KERN_MINOR" | cut -f2 -d'-' | cut -f2 -d'.'`
                    else
                        errorprint "Solaris kernel major version $HOST_OS_MAJORVERSION not supported."
                        exit 1
                    fi
                else
                    errorprint "Failed to parse the Solaris kernel minor version."
                    exit 1
                fi
            else
                errorprint "Failed to parse the Solaris kernel package version."
                exit 1
            fi
        else
            errorprint "Failed to detect the Solaris kernel package FMRI."
            exit 1
        fi
    else
        HOST_OS_MAJORVERSION=`uname -r`
        if test -z "$HOST_OS_MAJORVERSION" || test "x$HOST_OS_MAJORVERSION" != "x5.10";  then
            # S11 without 'pkg'?? Something's wrong... bail.
            errorprint "Solaris $HOST_OS_MAJORVERSION detected without executable $BIN_PKG !? I are confused."
            exit 1
        fi
        HOST_OS_MAJORVERSION="10"
        if test "$REMOTEINST" -eq 0; then
            # Use uname to verify it's S10.
            # Major version is S10, Minor version is no longer relevant (or used), use uname -v so it gets something
            # like "Generic_blah" for purely cosmetic purposes
            HOST_OS_MINORVERSION=`uname -v`
        else
            # Remote installs from S10 local.
            BIN_PKGCHK=`which pkgchk 2> /dev/null`
            if test ! -x "$BIN_PKGCHK"; then
                errorprint "Failed to find an executable pkgchk binary $BIN_PKGCHK."
                errorprint "Cannot determine Solaris version on remote target $PKG_INSTALL_ROOT"
                exit 1
            fi

            REMOTE_S10=`$BIN_PKGCHK -l -p /kernel/amd64/genunix $BASEDIR_PKGOPT 2> /dev/null | grep SUNWckr | tr -d ' \t'`
            if test ! -z "$REMOTE_S10" && test "x$REMOTE_S10" = "xSUNWckr"; then
                HOST_OS_MAJORVERSION="10"
                HOST_OS_MINORVERSION=""
            else
                errorprint "Remote target $PKG_INSTALL_ROOT is not Solaris 10."
                errorprint "Will not attempt to install to an unidentified remote target."
                exit 1
            fi
        fi
    fi
}

# check_zone()
# !! failure is always fatal
check_zone()
{
    currentzone=`zonename`
    if test "x$currentzone" != "xglobal"; then
        errorprint "This script must be run from the global zone."
        exit 1
    fi
}

# check_isa()
# !! failure is always fatal
check_isa()
{
    currentisa=`uname -i`
    if test "x$currentisa" = "xi86xpv"; then
        errorprint "VirtualBox cannot run under xVM Dom0! Fatal Error, Aborting installation!"
        exit 1
    fi
}

# check_module_arch()
# !! failure is always fatal
check_module_arch()
{
    cputype=`isainfo -k`
    if test "x$cputype" != "xamd64" && test "x$cputype" != "xi386"; then
        errorprint "VirtualBox works only on i386/amd64 hosts, not $cputype"
        exit 1
    fi
}

# update_boot_archive()
# cannot fail
update_boot_archive()
{
    infoprint "Updating the boot archive..."
    if test "$REMOTEINST" -eq 0; then
        $BIN_BOOTADM update-archive > /dev/null
    else
        $BIN_BOOTADM update-archive -R "$PKG_INSTALL_ROOT" > /dev/null
    fi
    UPDATEBOOTARCHIVE=0
}


# module_added(modname)
# returns 1 if added, 0 otherwise
module_added()
{
    if test -z "$1"; then
        errorprint "missing argument to module_added()"
        exit 1
    fi

    # Add a space at end of module name to make sure we have a perfect match to avoid
    # any substring matches: e.g "vboxusb" & "vboxusbmon"
    loadentry=`cat "$PKG_INSTALL_ROOT/etc/name_to_major" | grep "$1 "`
    if test -z "$loadentry"; then
        return 1
    fi
    return 0
}

# module_loaded(modname)
# returns 1 if loaded, 0 otherwise
module_loaded()
{
    if test -z "$1"; then
        errorprint "missing argument to module_loaded()"
        exit 1
    fi

    modname=$1
    # modinfo should now work properly since we prevent module autounloading.
    loadentry=`$BIN_MODINFO | grep "$modname "`
    if test -z "$loadentry"; then
        return 1
    fi
    return 0
}

# add_driver(modname, moddesc, fatal, nulloutput, [driverperm])
# failure: depends on "fatal"
add_driver()
{
    if test -z "$1" || test -z "$2"; then
        errorprint "missing argument to add_driver()"
        exit 1
    fi

    modname="$1"
    moddesc="$2"
    fatal="$3"
    nullop="$4"
    modperm="$5"

    if test -n "$modperm"; then
        if test "x$nullop" = "x$NULLOP"; then
            $BIN_ADDDRV $BASEDIR_OPT -m"$modperm" $modname  >/dev/null 2>&1
        else
            $BIN_ADDDRV $BASEDIR_OPT -m"$modperm" $modname
        fi
    else
        if test "x$nullop" = "x$NULLOP"; then
            $BIN_ADDDRV $BASEDIR_OPT $modname >/dev/null 2>&1
        else
            $BIN_ADDDRV $BASEDIR_OPT $modname
        fi
    fi

    if test "$?" -ne 0; then
        subprint "Adding: $moddesc module ...FAILED!"
        if test "x$fatal" = "x$FATALOP"; then
            exit 1
        fi
        return 1
    fi
    subprint "Added: $moddesc driver"
    return 0
}

# rem_driver(modname, moddesc, [fatal])
# failure: depends on [fatal]
rem_driver()
{
    if test -z "$1" || test -z "$2"; then
        errorprint "missing argument to rem_driver()"
        exit 1
    fi

    modname=$1
    moddesc=$2
    fatal=$3

    module_added $modname
    if test "$?" -eq 0; then
        UPDATEBOOTARCHIVE=1
        if test "x$ISIPS" != "x$IPSOP"; then
            $BIN_REMDRV $BASEDIR_OPT $modname
        else
            $BIN_REMDRV $BASEDIR_OPT $modname >/dev/null 2>&1
        fi
        # for remote installs, don't bother with return values of rem_drv
        if test "$?" -eq 0 || test "$REMOTEINST" -eq 1; then
            subprint "Removed: $moddesc driver"
            return 0
        else
            subprint "Removing: $moddesc  ...FAILED!"
            if test "x$fatal" = "x$FATALOP"; then
                exit 1
            fi
            return 1
        fi
    fi
}

# unload_module(modname, moddesc, retry, [fatal])
# failure: fatal
unload_module()
{
    if test -z "$1" || test -z "$2"; then
        errorprint "missing argument to unload_module()"
        exit 1
    fi

    # No-OP for non-root installs
    if test "$REMOTEINST" -eq 1; then
        return 0
    fi

    modname=$1
    moddesc=$2
    retry=$3
    fatal=$4
    modid=`$BIN_MODINFO | grep "$modname " | cut -f 1 -d ' ' `
    if test -n "$modid"; then
        $BIN_MODUNLOAD -i $modid
        if test "$?" -eq 0; then
            subprint "Unloaded: $moddesc module"
        else
            #
            # Hack for vboxdrv. Delayed removing when VMM thread-context hooks are used.
            # Our automated tests are probably too quick... Fix properly later.
            #
            result="$?"
            if test "$retry" -eq 1; then
                cmax=15
                cslept=0
                while test "$result" -ne 0;
                do
                    subprint "Unloading: $moddesc module ...FAILED! Busy? Retrying in 3 seconds..."
                    sleep 3
                    cslept=`expr $cslept + 3`
                    if test "$cslept" -ge "$cmax"; then
                        break
                    fi
                    $BIN_MODUNLOAD -i $modid
                    result="$?"
                done
            fi

            if test "$result" -ne 0; then
                subprint "Unloading: $moddesc module ...FAILED!"
                if test "x$fatal" = "x$FATALOP"; then
                    exit 1
                fi
            else
                subprint "Unloaded: $moddesc module"
            fi
            return 1
        fi
    fi
    return 0
}

# load_module(modname, moddesc, [fatal])
# pass "drv/modname" or "misc/vbi" etc.
# failure: fatal
load_module()
{
    if test -z "$1" || test -z "$2"; then
        errorprint "missing argument to load_module()"
        exit 1
    fi

    # No-OP for non-root installs
    if test "$REMOTEINST" -eq 1; then
        return 0
    fi

    modname=$1
    moddesc=$2
    fatal=$3
    $BIN_MODLOAD -p $modname
    if test "$?" -eq 0; then
        return 0
    else
        subprint "Loading: $moddesc module ...FAILED!"
        if test "x$fatal" = "x$FATALOP"; then
            exit 1
        fi
        return 1
    fi
}

load_vboxflt()
{
    if test -f "$DIR_CONF/vboxflt.conf"; then
        add_driver "$MOD_VBOXFLT" "$DESC_VBOXFLT" "$FATALOP"
        load_module "drv/$MOD_VBOXFLT" "$DESC_VBOXFLT" "$FATALOP"
    else
        # For custom pkgs that optionally ship this module, let's not fail but just warn
        warnprint "$DESC_VBOXFLT installation requested but not shipped in this package."
    fi
}

load_vboxbow()
{
    if test -f "$DIR_CONF/vboxbow.conf"; then
        add_driver "$MOD_VBOXBOW" "$DESC_VBOXBOW" "$FATALOP"
        load_module "drv/$MOD_VBOXBOW" "$DESC_VBOXBOW" "$FATALOP"
    else
        # For custom pkgs that optionally ship this module, let's not fail but just warn
        warnprint "$DESC_VBOXBOW installation requested but not shipped in this package."
    fi
}

# install_drivers()
# !! failure is always fatal
install_drivers()
{
    if test -f "$DIR_CONF/vboxdrv.conf"; then
        if test -n "_HARDENED_"; then
            add_driver "$MOD_VBOXDRV" "$DESC_VBOXDRV" "$FATALOP" "not-$NULLOP" "'* 0600 root sys','vboxdrvu 0666 root sys'"
        else
            add_driver "$MOD_VBOXDRV" "$DESC_VBOXDRV" "$FATALOP" "not-$NULLOP" "'* 0666 root sys','vboxdrvu 0666 root sys'"
        fi
        load_module "drv/$MOD_VBOXDRV" "$DESC_VBOXDRV" "$FATALOP"
    else
        errorprint "Extreme error! Missing $DIR_CONF/vboxdrv.conf, aborting."
        return 1
    fi

    # Figure out group to use for /etc/devlink.tab (before Solaris 11 SRU6
    # it was always using group sys)
    group=sys
    if [ -f "$PKG_INSTALL_ROOT/etc/dev/reserved_devnames" ]; then
        # Solaris 11 SRU6 and later use group root (check a file which isn't
        # tainted by VirtualBox install scripts and allow no other group)
        refgroup=`LC_ALL=C /usr/bin/ls -lL "$PKG_INSTALL_ROOT/etc/dev/reserved_devnames" | awk '{ print $4 }' 2>/dev/null`
        if [ $? -eq 0 -a "x$refgroup" = "xroot" ]; then
            group=root
        fi
        unset refgroup
    fi

    ## Add vboxdrv to devlink.tab (KEEP TABS!)
    if test -f "$PKG_INSTALL_ROOT/etc/devlink.tab"; then
        sed -e '/name=vboxdrv/d' -e '/name=vboxdrvu/d' "$PKG_INSTALL_ROOT/etc/devlink.tab" > "$PKG_INSTALL_ROOT/etc/devlink.vbox"
        echo "type=ddi_pseudo;name=vboxdrv;minor=vboxdrv	\D"  >> "$PKG_INSTALL_ROOT/etc/devlink.vbox"
        echo "type=ddi_pseudo;name=vboxdrv;minor=vboxdrvu	\M0" >> "$PKG_INSTALL_ROOT/etc/devlink.vbox"
        chmod 0644 "$PKG_INSTALL_ROOT/etc/devlink.vbox"
        chown root:$group "$PKG_INSTALL_ROOT/etc/devlink.vbox"
        mv -f "$PKG_INSTALL_ROOT/etc/devlink.vbox" "$PKG_INSTALL_ROOT/etc/devlink.tab"
    else
        errorprint "Missing $PKG_INSTALL_ROOT/etc/devlink.tab, aborting install"
        return 1
    fi

    # Create the device link for non-remote installs (not really relevant any more)
    if test "$REMOTEINST" -eq 0; then
        /usr/sbin/devfsadm -i "$MOD_VBOXDRV"
        if test "$?" -ne 0 || test ! -h "/dev/vboxdrv" || test ! -h "/dev/vboxdrvu" ; then
            errorprint "Failed to create device link for $MOD_VBOXDRV."
            exit 1
        fi
    fi

    # Load VBoxNetAdp
    if test -f "$DIR_CONF/vboxnet.conf"; then
        add_driver "$MOD_VBOXNET" "$DESC_VBOXNET" "$FATALOP"
        load_module "drv/$MOD_VBOXNET" "$DESC_VBOXNET" "$FATALOP"
    fi

    # If both vboxinst_vboxbow and vboxinst_vboxflt exist, bail.
    if test -f "$PKG_INSTALL_ROOT/etc/vboxinst_vboxflt" && test -f "$PKG_INSTALL_ROOT/etc/vboxinst_vboxbow"; then
        errorprint "Force-install files '$PKG_INSTALL_ROOT/etc/vboxinst_vboxflt' and '$PKG_INSTALL_ROOT/etc/vboxinst_vboxbow' both exist."
        errorprint "Cannot load $DESC_VBOXFLT and $DESC_VBOXBOW drivers at the same time."
        return 1
    fi

    # If the force-install files exists, install blindly
    if test -f "$PKG_INSTALL_ROOT/etc/vboxinst_vboxflt"; then
        subprint "Detected: Force-load file $PKG_INSTALL_ROOT/etc/vboxinst_vboxflt."
        load_vboxflt
    elif test -f "$PKG_INSTALL_ROOT/etc/vboxinst_vboxbow"; then
        subprint "Detected: Force-load file $PKG_INSTALL_ROOT/etc/vboxinst_vboxbow."
        load_vboxbow
    else
        # If host is S10 or S11 (< snv_159) or vboxbow isn't shipped, then load vboxflt
        if     test "$HOST_OS_MAJORVERSION" -eq 10 \
           || (test "$HOST_OS_MAJORVERSION" -eq 11 && test "$HOST_OS_MINORVERSION" -lt 159) \
           ||  test ! -f "$DIR_CONF/vboxbow.conf"; then
            load_vboxflt
        else
            # For S11 snv_159+ load vboxbow
            load_vboxbow
        fi
    fi

    # Load VBoxUSBMon, VBoxUSB
    try_vboxusb="no"
    if test -f "$DIR_CONF/vboxusbmon.conf"; then
        if test -f "$PKG_INSTALL_ROOT/etc/vboxinst_vboxusb"; then
            subprint "Detected: Force-load file $PKG_INSTALL_ROOT/etc/vboxinst_vboxusb."
            try_vboxusb="yes"
        else
            # For VirtualBox 3.1 the new USB code requires Nevada > 123 i.e. S12+ or S11 b124+
            if     test "$HOST_OS_MAJORVERSION" -gt 11 \
               || (test "$HOST_OS_MAJORVERSION" -eq 11 && test "$HOST_OS_MINORVERSION" -gt 123); then
                try_vboxusb="yes"
            else
                warnprint "Solaris 11 build 124 or higher required for USB support. Skipped installing USB support."
            fi
        fi
    fi
    if test "x$try_vboxusb" = "xyes"; then
        # Add a group "vboxuser" (8-character limit) for USB access.
        # All users which need host USB-passthrough support will have to be added to this group.
        groupadd vboxuser >/dev/null 2>&1

        add_driver "$MOD_VBOXUSBMON" "$DESC_VBOXUSBMON" "$FATALOP" "not-$NULLOP" "'* 0666 root sys'"
        load_module "drv/$MOD_VBOXUSBMON" "$DESC_VBOXUSBMON" "$FATALOP"

        chown root:vboxuser "/devices/pseudo/vboxusbmon@0:vboxusbmon"

        # Add vboxusbmon to devlink.tab
        sed -e '/name=vboxusbmon/d' "$PKG_INSTALL_ROOT/etc/devlink.tab" > "$PKG_INSTALL_ROOT/etc/devlink.vbox"
        echo "type=ddi_pseudo;name=vboxusbmon	\D" >> "$PKG_INSTALL_ROOT/etc/devlink.vbox"
        chmod 0644 "$PKG_INSTALL_ROOT/etc/devlink.vbox"
        chown root:$group "$PKG_INSTALL_ROOT/etc/devlink.vbox"
        mv -f "$PKG_INSTALL_ROOT/etc/devlink.vbox" "$PKG_INSTALL_ROOT/etc/devlink.tab"

        # Create the device link for non-remote installs
        if test "$REMOTEINST" -eq 0; then
            /usr/sbin/devfsadm -i  "$MOD_VBOXUSBMON"
            if test "$?" -ne 0; then
                errorprint "Failed to create device link for $MOD_VBOXUSBMON."
                exit 1
            fi
        fi

        # Add vboxusb if present
        # This driver is special, we need it in the boot-archive but since there is no
        # USB device to attach to now (it's done at runtime) it will fail to attach so
        # redirect attaching failure output to /dev/null
        if test -f "$DIR_CONF/vboxusb.conf"; then
            add_driver "$MOD_VBOXUSB" "$DESC_VBOXUSB" "$FATALOP" "$NULLOP"
            load_module "drv/$MOD_VBOXUSB" "$DESC_VBOXUSB" "$FATALOP"
        fi
    fi

    return $?
}

# remove_drivers([fatal])
# failure: depends on [fatal]
remove_drivers()
{
    fatal=$1

    # Figure out group to use for /etc/devlink.tab (before Solaris 11 SRU6
    # it was always using group sys)
    group=sys
    if [ -f "$PKG_INSTALL_ROOT/etc/dev/reserved_devnames" ]; then
        # Solaris 11 SRU6 and later use group root (check a file which isn't
        # tainted by VirtualBox install scripts and allow no other group)
        refgroup=`LC_ALL=C /usr/bin/ls -lL "$PKG_INSTALL_ROOT/etc/dev/reserved_devnames" | awk '{ print $4 }' 2>/dev/null`
        if [ $? -eq 0 -a "x$refgroup" = "xroot" ]; then
            group=root
        fi
        unset refgroup
    fi

    # Remove vboxdrv[u] from devlink.tab
    if test -f "$PKG_INSTALL_ROOT/etc/devlink.tab"; then
        devlinkfound=`cat "$PKG_INSTALL_ROOT/etc/devlink.tab" | grep vboxdrv`
        if test -n "$devlinkfound"; then
            sed -e '/name=vboxdrv/d' -e '/name=vboxdrvu/d' "$PKG_INSTALL_ROOT/etc/devlink.tab" > "$PKG_INSTALL_ROOT/etc/devlink.vbox"
            chmod 0644 "$PKG_INSTALL_ROOT/etc/devlink.vbox"
            chown root:$group "$PKG_INSTALL_ROOT/etc/devlink.vbox"
            mv -f "$PKG_INSTALL_ROOT/etc/devlink.vbox" "$PKG_INSTALL_ROOT/etc/devlink.tab"
        fi

        # Remove vboxusbmon from devlink.tab
        devlinkfound=`cat "$PKG_INSTALL_ROOT/etc/devlink.tab" | grep vboxusbmon`
        if test -n "$devlinkfound"; then
            sed -e '/name=vboxusbmon/d' "$PKG_INSTALL_ROOT/etc/devlink.tab" > "$PKG_INSTALL_ROOT/etc/devlink.vbox"
            chmod 0644 "$PKG_INSTALL_ROOT/etc/devlink.vbox"
            chown root:$group "$PKG_INSTALL_ROOT/etc/devlink.vbox"
            mv -f "$PKG_INSTALL_ROOT/etc/devlink.vbox" "$PKG_INSTALL_ROOT/etc/devlink.tab"
        fi
    fi

    unload_module "$MOD_VBOXUSB" "$DESC_VBOXUSB" 0 "$fatal"
    rem_driver "$MOD_VBOXUSB" "$DESC_VBOXUSB" "$fatal"

    unload_module "$MOD_VBOXUSBMON" "$DESC_VBOXUSBMON" 0 "$fatal"
    rem_driver "$MOD_VBOXUSBMON" "$DESC_VBOXUSBMON" "$fatal"

    unload_module "$MOD_VBOXFLT" "$DESC_VBOXFLT" 0 "$fatal"
    rem_driver "$MOD_VBOXFLT" "$DESC_VBOXFLT" "$fatal"

    unload_module "$MOD_VBOXBOW" "$DESC_VBOXBOW" 0 "$fatal"
    rem_driver "$MOD_VBOXBOW" "$DESC_VBOXBOW" "$fatal"

    unload_module "$MOD_VBOXNET" "$DESC_VBOXNET" 0 "$fatal"
    rem_driver "$MOD_VBOXNET" "$DESC_VBOXNET" "$fatal"

    unload_module "$MOD_VBOXDRV" "$DESC_VBOXDRV" 1 "$fatal"
    rem_driver "$MOD_VBOXDRV" "$DESC_VBOXDRV" "$fatal"

    # remove devlinks
    if test -h "$PKG_INSTALL_ROOT/dev/vboxdrv" || test -f "$PKG_INSTALL_ROOT/dev/vboxdrv"; then
        rm -f "$PKG_INSTALL_ROOT/dev/vboxdrv"
    fi
    if test -h "$PKG_INSTALL_ROOT/dev/vboxdrvu" || test -f "$PKG_INSTALL_ROOT/dev/vboxdrvu"; then
        rm -f "$PKG_INSTALL_ROOT/dev/vboxdrvu"
    fi
    if test -h "$PKG_INSTALL_ROOT/dev/vboxusbmon" || test -f "$PKG_INSTALL_ROOT/dev/vboxusbmon"; then
        rm -f "$PKG_INSTALL_ROOT/dev/vboxusbmon"
    fi

    # unpatch nwam/dhcpagent fix
    nwamfile="$PKG_INSTALL_ROOT/etc/nwam/llp"
    nwambackupfile=$nwamfile.vbox
    if test -f "$nwamfile"; then
        sed -e '/vboxnet/d' $nwamfile > $nwambackupfile
        mv -f $nwambackupfile $nwamfile
    fi

    # remove netmask configuration
    if test -h "$PKG_INSTALL_ROOT/etc/netmasks"; then
        nmaskfile="$PKG_INSTALL_ROOT/etc/inet/netmasks"
    else
        nmaskfile="$PKG_INSTALL_ROOT/etc/netmasks"
    fi
    nmaskbackupfile=$nmaskfile.vbox
    if test -f "$nmaskfile"; then
        sed -e '/#VirtualBox_SectionStart/,/#VirtualBox_SectionEnd/d' $nmaskfile > $nmaskbackupfile
        mv -f $nmaskbackupfile $nmaskfile
    fi

    if test $UPDATEBOOTARCHIVE -eq 1; then
        update_boot_archive
    fi

    return 0
}

# install_python_bindings(PYTHON_BIN PYTHON_VER)
# failure: non fatal
#
## @todo r=andy Merge this code with linux/routines.sh!
install_python_bindings()
{
    PYTHON_BIN="$1"
    PYTHON_VER="$2"

    # The python binary might not be there, so just exit silently
    if test -z "$PYTHON_BIN"; then
        return 0
    fi

    if test -z "$PYTHON_VER"; then
        errorprint "missing argument to install_python_bindings"
        return 1
    fi

    infoprint "Python found: $PYTHON_BIN, installing bindings..."

    # check if python has working distutils
    "$PYTHON_BIN" -c "from distutils.core import setup" > /dev/null 2>&1
    if test "$?" -ne 0; then
        subprint "Skipped: Python $PYTHON_VER does not have package 'distutils', skipping..."
        return 0
    fi

    MY_PYTHON_INSTALLER_PATH="$VBOX_INSTALL_PATH/sdk/installer/python"

    # Pass install path via environment
    export VBOX_INSTALL_PATH
    mkdir -p "$CONFIG_DIR"
    rm -f "$CONFIG_DIR/python-$CONFIG_FILES"

    $SHELL -c "cd ${MY_PYTHON_INSTALLER_PATH} && ${PYTHON_BIN} ./vboxapisetup.py install \
        --record \"$CONFIG_DIR/python-$CONFIG_FILES\"" > /dev/null 2>&1
    if test "$?" -eq 0; then
        cat "$CONFIG_DIR/python-$CONFIG_FILES" >> "$CONFIG_DIR/$CONFIG_FILES"
    else
        errorprint "Failed to install bindings for Python $PYTHON_VER"
    fi
    rm -f "$CONFIG_DIR/python-$CONFIG_FILES"

    # Remove files created by Python API setup.
    rm -rf "$MY_PYTHON_INSTALLER_PATH/build"
    return 0
}

## @todo r=andy Merge this code with linux/routines.sh!
maybe_run_python_bindings_installer() {
    MY_PYTHON_INSTALLER_PATH="$VBOX_INSTALL_PATH/sdk/installer/python"

    if test -f "$MY_PYTHON_INSTALLER_PATH/vboxapisetup.py" || test -h "$MY_PYTHON_INSTALLER_PATH/vboxapisetup.py"; then
        # Install python bindings for non-remote installs
        if test "$REMOTEINST" -eq 0; then
            infoprint "Installing Python bindings..."

            # Loop over all usual suspect Python executable names and try installing
            # the VirtualBox API bindings. Needs to prevent double installs which waste
            # quite a bit of time.
            PYTHON_VER_INSTALLED=""
            PYTHON_BINARIES="\
                python2.4  \
                python2.5  \
                python2.6  \
                python2.7  \
                python2    \
                python3.3  \
                python3.4  \
                python3.5  \
                python3.6  \
                python3.7  \
                python3.8  \
                python3.9  \
                python3.10 \
                python3.11 \
                python3    \
                python     \
                "

            for PYTHON_BIN in $PYTHON_BINARIES; do
                if [ "`$PYTHON_BIN -c 'import sys
if sys.version_info >= (2, 6) and (sys.version_info < (3, 0) or sys.version_info >= (3, 3)):
    print(\"test\")' 2> /dev/null`" != "test" ]; then
                    continue
                fi
                # Get python major/minor version, and skip if it was
                # already covered.  Uses grep -F to avoid trouble with '.'
                # matching any char.
                PYTHON_VER="`$PYTHON_BIN -c 'import sys
print("%s.%s" % (sys.version_info[0], sys.version_info[1]))' 2> /dev/null`"
                if echo "$PYTHON_VER_INSTALLED" | /usr/xpg4/bin/grep -Fq ":$PYTHON_VER:"; then
                    continue
                fi
                # Record which version will be installed. If it fails there
                # is no point trying with different executable/symlink
                # reporting the same version.
                PYTHON_VER_INSTALLED="$PYTHON_VER_INSTALLED:$PYTHON_VER:"
                install_python_bindings "$PYTHON_BIN" "$PYTHON_VER"
            done
            if [ -z "$PYTHON_VER_INSTALLED" ]; then
                warnprint "Python (2.4 to 2.7 or 3.3 and later) unavailable, skipping bindings installation."
            fi
        else
            warnprint "Skipped installing Python bindings. Run, as root, 'vboxapisetup.py install' manually from the booted system."
        fi
    fi

    return 0
}

# is_process_running(processname)
# returns 1 if the process is running, 0 otherwise
is_process_running()
{
    if test -z "$1"; then
        errorprint "missing argument to is_process_running()"
        exit 1
    fi

    procname="$1"
    $BIN_PGREP "$procname" > /dev/null 2>&1
    if test "$?" -eq 0; then
        return 1
    fi
    return 0
}


# stop_process(processname)
# failure: depends on [fatal]
stop_process()
{
    if test -z "$1"; then
        errorprint "missing argument to stop_process()"
        exit 1
    fi

    procname="$1"
    is_process_running "$procname"
    if test "$?" -eq 1; then
        $BIN_PKILL "$procname"
        sleep 2
        is_process_running "$procname"
        if test "$?" -eq 1; then
            subprint "Terminating: $procname  ...FAILED!"
            if test "x$fatal" = "x$FATALOP"; then
                exit 1
            fi
        else
            subprint "Terminated: $procname"
        fi
    fi
}

# start_service(servicename, shortFMRI pretty printing, full FMRI, log-file path)
# failure: non-fatal
start_service()
{
    if test -z "$1" || test -z "$2" || test -z "$3" || test -z "$4"; then
        errorprint "missing argument to enable_service()"
        exit 1
    fi

    # Since S11 the way to import a manifest is via restarting manifest-import which is asynchronous and can
    # take a while to complete, using disable/enable -s doesn't work either. So we restart it, and poll in
    # 1 second intervals to see if our service has been successfully imported and timeout after 'cmax' seconds.
    cmax=32
    cslept=0
    success=0

    $BIN_SVCS "$3" >/dev/null 2>&1
    while test "$?" -ne 0;
    do
        sleep 1
        cslept=`expr $cslept + 1`
        if test "$cslept" -eq "$cmax"; then
            success=1
            break
        fi
        $BIN_SVCS "$3" >/dev/null 2>&1
    done
    if test "$success" -eq 0; then
        $BIN_SVCADM enable -s "$3"
        if test "$?" -eq 0; then
            subprint "Enabled: $1"
            return 0
        else
            warnprint "Enabling $1  ...FAILED."
            warnprint "Refer $4 for details."
        fi
    else
        warnprint "Importing $1  ...FAILED."
        warnprint "Refer /var/svc/log/system-manifest-import:default.log for details."
    fi
    return 1
}


# stop_service(servicename, shortFMRI-suitable for grep, full FMRI)
# failure: non fatal
stop_service()
{
    if test -z "$1" || test -z "$2" || test -z "$3"; then
        errorprint "missing argument to stop_service()"
        exit 1
    fi
    servicefound=`$BIN_SVCS -H "$2" 2>/dev/null | grep '^online'`
    if test ! -z "$servicefound"; then
        $BIN_SVCADM disable -s "$3"
        # Don't delete the manifest, this is handled by the manifest class action
        # $BIN_SVCCFG delete "$3"
        if test "$?" -eq 0; then
            subprint "Disabled: $1"
        else
            subprint "Disabling: $1  ...ERROR(S)."
        fi
    fi
}


# plumb vboxnet0 instance
# failure: non fatal
plumb_net()
{
    # S11 175a renames vboxnet0 as 'netX', undo this and rename it back (Solaris 12, Solaris 11.4 or newer)
    if test "$HOST_OS_MAJORVERSION" -ge 12 || (test "$HOST_OS_MAJORVERSION" -eq 11 && test "$HOST_OS_MINORVERSION" -ge 175); then
        vanityname=`dladm show-phys -po link,device | grep vboxnet0 | cut -f1 -d':'`
        if test "$?" -eq 0 && test ! -z "$vanityname" && test "x$vanityname" != "xvboxnet0"; then
            dladm rename-link "$vanityname" vboxnet0
            if test "$?" -ne 0; then
                errorprint "Failed to rename vanity interface ($vanityname) to vboxnet0"
            fi
        fi
    fi

    # use ipadm for Solaris 12, Solaris 11.5 or newer
    if test "$HOST_OS_MAJORVERSION" -ge 12 || (test "$HOST_OS_MAJORVERSION" -eq 11 && test "$HOST_OS_MINORVERSION" -ge 176); then
        $BIN_IPADM create-ip vboxnet0
        if test "$?" -eq 0; then
            $BIN_IPADM create-addr -T static -a local="192.168.56.1/24" "vboxnet0/v4addr"
            if test "$?" -eq 0; then
                subprint "Configured: NetAdapter 'vboxnet0'"
            else
                warnprint "Failed to create local address for vboxnet0!"
            fi
        else
            warnprint "Failed to create IP instance for vboxnet0!"
        fi
    else
        $BIN_IFCONFIG vboxnet0 plumb
        $BIN_IFCONFIG vboxnet0 up
        if test "$?" -eq 0; then
            $BIN_IFCONFIG vboxnet0 192.168.56.1 netmask 255.255.255.0 up

            # /etc/netmasks is a symlink, older installers replaced this with
            # a copy of the actual file, repair that behaviour here.
            recreatelink=0
            if test -h "$PKG_INSTALL_ROOT/etc/netmasks"; then
                nmaskfile="$PKG_INSTALL_ROOT/etc/inet/netmasks"
            else
                nmaskfile="$PKG_INSTALL_ROOT/etc/netmasks"
                recreatelink=1
            fi

            # add the netmask to stay persistent across host reboots
            nmaskbackupfile=$nmaskfile.vbox
            if test -f $nmaskfile; then
                sed -e '/#VirtualBox_SectionStart/,/#VirtualBox_SectionEnd/d' $nmaskfile > $nmaskbackupfile

                if test "$recreatelink" -eq 1; then
                    # Check after removing our settings if /etc/netmasks is identifcal to /etc/inet/netmasks
                    anydiff=`diff $nmaskbackupfile "$PKG_INSTALL_ROOT/etc/inet/netmasks"`
                    if test ! -z "$anydiff"; then
                        # User may have some custom settings in /etc/netmasks, don't overwrite /etc/netmasks!
                        recreatelink=2
                    fi
                fi

                echo "#VirtualBox_SectionStart" >> $nmaskbackupfile
                inst=0
                networkn=56
                while test "$inst" -ne 1; do
                    echo "192.168.$networkn.0 255.255.255.0" >> $nmaskbackupfile
                    inst=`expr $inst + 1`
                    networkn=`expr $networkn + 1`
                done
                echo "#VirtualBox_SectionEnd" >> $nmaskbackupfile
                mv -f $nmaskbackupfile $nmaskfile

                # Recreate /etc/netmasks as a link if necessary
                if test "$recreatelink" -eq 1; then
                    cp -f "$PKG_INSTALL_ROOT/etc/netmasks" "$PKG_INSTALL_ROOT/etc/inet/netmasks"
                    ln -sf ./inet/netmasks "$PKG_INSTALL_ROOT/etc/netmasks"
                elif test "$recreatelink" -eq 2; then
                    warnprint "/etc/netmasks is a symlink (to /etc/inet/netmasks) that older"
                    warnprint "VirtualBox installers incorrectly overwrote. Now the contents"
                    warnprint "of /etc/netmasks and /etc/inet/netmasks differ, therefore "
                    warnprint "VirtualBox will not attempt to overwrite /etc/netmasks as a"
                    warnprint "symlink to /etc/inet/netmasks. Please resolve this manually"
                    warnprint "by updating /etc/inet/netmasks and creating /etc/netmasks as a"
                    warnprint "symlink to /etc/inet/netmasks"
                fi
            fi
        else
            # Should this be fatal?
            warnprint "Failed to bring up vboxnet0!"
        fi
    fi
}


# unplumb all vboxnet instances
# failure: fatal
unplumb_net()
{
    inst=0
    # use ipadm for Solaris 12, Solaris 11.5 or newer
    if test "$HOST_OS_MAJORVERSION" -ge 12 || (test "$HOST_OS_MAJORVERSION" -eq 11 && test "$HOST_OS_MINORVERSION" -ge 176); then
        while test "$inst" -ne $MOD_VBOXNET_INST; do
            vboxnetup=`$BIN_IPADM show-addr -p -o addrobj vboxnet$inst >/dev/null 2>&1`
            if test "$?" -eq 0; then
                $BIN_IPADM delete-addr vboxnet$inst/v4addr
                $BIN_IPADM delete-ip vboxnet$inst
                if test "$?" -ne 0; then
                    errorprint "VirtualBox NetAdapter 'vboxnet$inst' couldn't be removed (probably in use)."
                    if test "x$fatal" = "x$FATALOP"; then
                        exit 1
                    fi
                fi
            fi

            inst=`expr $inst + 1`
        done
    else
        inst=0
        while test "$inst" -ne $MOD_VBOXNET_INST; do
            vboxnetup=`$BIN_IFCONFIG vboxnet$inst >/dev/null 2>&1`
            if test "$?" -eq 0; then
                $BIN_IFCONFIG vboxnet$inst unplumb
                if test "$?" -ne 0; then
                    errorprint "VirtualBox NetAdapter 'vboxnet$inst' couldn't be unplumbed (probably in use)."
                    if test "x$fatal" = "x$FATALOP"; then
                        exit 1
                    fi
                fi
            fi

            # unplumb vboxnet0 ipv6
            vboxnetup=`$BIN_IFCONFIG vboxnet$inst inet6 >/dev/null 2>&1`
            if test "$?" -eq 0; then
                $BIN_IFCONFIG vboxnet$inst inet6 unplumb
                if test "$?" -ne 0; then
                    errorprint "VirtualBox NetAdapter 'vboxnet$inst' IPv6 couldn't be unplumbed (probably in use)."
                    if test "x$fatal" = "x$FATALOP"; then
                        exit 1
                    fi
                fi
            fi

            inst=`expr $inst + 1`
        done
    fi
}


# cleanup_install([fatal])
# failure: depends on [fatal]
cleanup_install()
{
    fatal=$1

    # No-Op for remote installs
    if test "$REMOTEINST" -eq 1; then
        return 0
    fi

    # stop the services
    stop_service "Web service" "virtualbox/webservice" "svc:/application/virtualbox/webservice:default"
    stop_service "Balloon control service" "virtualbox/balloonctrl" "svc:/application/virtualbox/balloonctrl:default"
    stop_service "Autostart service" "virtualbox/autostart" "svc:/application/virtualbox/autostart:default"
    stop_service "Zone access service" "virtualbox/zoneaccess" "svc:/application/virtualbox/zoneaccess:default"

    # DEBUG x4600b: verify that the ZoneAccess process is really gone
    is_process_running "VBoxZoneAccess"
    if test "$?" -eq 1; then
        warnprint "VBoxZoneAccess is alive despite its service being dead. Killing..."
        stop_process "VBoxZoneAccess"
    fi

    # unplumb all vboxnet instances for non-remote installs
    unplumb_net

    # Stop our other daemons, non-fatal
    stop_process "VBoxNetDHCP"
    stop_process "VBoxNetNAT"

    # Stop VBoxSVC quickly using SIGUSR1
    procname="VBoxSVC"
    procpid=`ps -eo pid,fname | grep $procname | grep -v grep | awk '{ print $1 }'`
    if test ! -z "$procpid" && test "$procpid" -ge 0; then
        kill -USR1 $procpid

        # Sleep a while and check if VBoxSVC is still running, if so fail uninstallation.
        sleep 2
        is_process_running "VBoxSVC"
        if test "$?" -eq 1; then
            errorprint "Cannot uninstall VirtualBox while VBoxSVC (pid $procpid) is still running."
            errorprint "Please shutdown all VMs and VirtualBox frontends before uninstalling VirtualBox."
            exit 1
        fi

        # Some VMs might still be alive after VBoxSVC as they poll less frequently before killing themselves
        # Just check for VBoxHeadless & VirtualBox frontends for now.
        is_process_running "VBoxHeadless"
        if test "$?" -eq 1; then
            errorprint "Cannot uninstall VirtualBox while VBoxHeadless is still running."
            errorprint "Please shutdown all VMs and VirtualBox frontends before uninstalling VirtualBox."
            exit 1
        fi

        is_process_running "VirtualBox"
        if test "$?" -eq 1; then
            errorprint "Cannot uninstall VirtualBox while any VM is still running."
            errorprint "Please shutdown all VMs and VirtualBox frontends before uninstalling VirtualBox."
            exit 1
        fi
    fi

    # Remove stuff installed for the Python bindings.
    if [ -r "$CONFIG_DIR/$CONFIG_FILES" ]; then
        rm -f `cat "$CONFIG_DIR/$CONFIG_FILES"` 2> /dev/null
        rm -f "$CONFIG_DIR/$CONFIG_FILES" 2> /dev/null
        rmdir "$CONFIG_DIR" 2> /dev/null
    fi
}


# postinstall()
# !! failure is always fatal
postinstall()
{
    infoprint "Detected Solaris $HOST_OS_MAJORVERSION Version $HOST_OS_MINORVERSION"

    infoprint "Loading VirtualBox kernel modules..."
    install_drivers

    if test "$?" -eq 0; then
        if test -f "$DIR_CONF/vboxnet.conf"; then
            # nwam/dhcpagent fix
            nwamfile="$PKG_INSTALL_ROOT/etc/nwam/llp"
            nwambackupfile=$nwamfile.vbox
            if test -f "$nwamfile"; then
                sed -e '/vboxnet/d' $nwamfile > $nwambackupfile

                # add all vboxnet instances as static to nwam
                inst=0
                networkn=56
                while test "$inst" -ne 1; do
                    echo "vboxnet$inst	static 192.168.$networkn.1" >> $nwambackupfile
                    inst=`expr $inst + 1`
                    networkn=`expr $networkn + 1`
                done
                mv -f $nwambackupfile $nwamfile
            fi

            # plumb and configure vboxnet0 for non-remote installs
            if test "$REMOTEINST" -eq 0; then
                plumb_net
            fi
        fi

        if     test -f "$PKG_INSTALL_ROOT/var/svc/manifest/application/virtualbox/virtualbox-webservice.xml" \
            || test -f "$PKG_INSTALL_ROOT/var/svc/manifest/application/virtualbox/virtualbox-zoneaccess.xml" \
            || test -f "$PKG_INSTALL_ROOT/var/svc/manifest/application/virtualbox/virtualbox-balloonctrl.xml"\
            || test -f "$PKG_INSTALL_ROOT/var/svc/manifest/application/virtualbox/virtualbox-autostart.xml"; then
            infoprint "Configuring services..."
            if test "$REMOTEINST" -eq 1; then
                subprint "Skipped for targeted installs."
            else
                # Since S11 the way to import a manifest is via restarting manifest-import which is asynchronous and can
                # take a while to complete, using disable/enable -s doesn't work either. So we restart it, and poll in
                # 1 second intervals to see if our service has been successfully imported and timeout after 'cmax' seconds.
                $BIN_SVCADM restart svc:system/manifest-import:default

                # Start ZoneAccess service, other services are disabled by default.
                start_service "Zone access service" "virtualbox/zoneaccess" "svc:/application/virtualbox/zoneaccess:default" \
                            "/var/svc/log/application-virtualbox-zoneaccess:default.log"
            fi
        fi

        # Update mime and desktop databases to get the right menu entries
        # and icons. There is still some delay until the GUI picks it up,
        # but that cannot be helped.
        if test -d "$PKG_INSTALL_ROOT/usr/share/icons"; then
            infoprint "Installing MIME types and icons..."
            if test "$REMOTEINST" -eq 0; then
                /usr/bin/update-mime-database /usr/share/mime >/dev/null 2>&1
                /usr/bin/update-desktop-database -q 2>/dev/null
            else
                subprint "Skipped for targeted installs."
            fi
        fi

        maybe_run_python_bindings_installer
        update_boot_archive

        return 0
    else
        errorprint "Failed to install drivers"
        exit 666
    fi
    return 1
}

# preremove([fatal])
# failure: depends on [fatal]
preremove()
{
    fatal=$1

    cleanup_install "$fatal"

    remove_drivers "$fatal"
    if test "$?" -eq 0; then
        return 0;
    fi
    return 1
}


# And it begins...
if test "x${PKG_INSTALL_ROOT:=/}" != "x/"; then
    BASEDIR_OPT="-b $PKG_INSTALL_ROOT"
    BASEDIR_PKGOPT="-R $PKG_INSTALL_ROOT"
    REMOTEINST=1
fi
find_bins
check_root
check_isa
check_zone
get_sysinfo


# Get command line options
while test $# -gt 0;
do
    case "$1" in
        --postinstall | --preremove | --installdrivers | --removedrivers | --setupdrivers)
            drvop="$1"
            ;;
        --fatal)
            fatal="$FATALOP"
            ;;
        --silent)
            ISSILENT="$SILENTOP"
            ;;
        --ips)
            ISIPS="$IPSOP"
            ;;
        --altkerndir)
            # Use alternate kernel driver config folder (dev only)
            DIR_CONF="/usr/kernel/drv"
            ;;
        --sh-trace) # forwarded pkgadd -v
            set -x
            ;;
        --help)
            printusage
            exit 1
            ;;
        *)
            # Take a hard line on invalid options.
            errorprint "Invalid command line option: \"$1\""
            exit 1;
            ;;
    esac
    shift
done

case "$drvop" in
--postinstall)
    check_module_arch
    postinstall
    ;;
--preremove)
    preremove "$fatal"
    ;;
--installdrivers)
    check_module_arch
    install_drivers
    ;;
--removedrivers)
    remove_drivers "$fatal"
    ;;
--setupdrivers)
    remove_drivers "$fatal"
    infoprint "Installing VirtualBox drivers:"
    install_drivers
    ;;
*)
    printusage
    exit 1
esac

exit "$?"
