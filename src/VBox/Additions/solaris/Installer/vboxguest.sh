#!/bin/sh
# $Id: vboxguest.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox Guest Additions kernel module control script for Solaris.
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
# The contents of this file may alternatively be used under the terms
# of the Common Development and Distribution License Version 1.0
# (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
# in the VirtualBox distribution, in which case the provisions of the
# CDDL are applicable instead of those of the GPL.
#
# You may elect to license modified versions of this file under the
# terms and conditions of either the GPL or the CDDL or both.
#
# SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
#

LC_ALL=C
export LC_ALL

LANG=C
export LANG

SILENTUNLOAD=""
MODNAME="vboxguest"
VFSMODNAME="vboxfs"
VMSMODNAME="vboxms"
MODDIR32="${PKG_INSTALL_ROOT}/usr/kernel/drv"
MODDIR64="${PKG_INSTALL_ROOT}/usr/kernel/drv/amd64"
VFSDIR32="${PKG_INSTALL_ROOT}/usr/kernel/fs"
VFSDIR64="${PKG_INSTALL_ROOT}/usr/kernel/fs/amd64"

abort()
{
    echo 1>&2 "## $1"
    exit 1
}

info()
{
    echo 1>&2 "$1"
}

check_if_installed()
{
    cputype=`isainfo -k`
    modulepath="$MODDIR32/$MODNAME"
    if test "$cputype" = "amd64"; then
        modulepath="$MODDIR64/$MODNAME"
    fi
    if test -f "$modulepath"; then
        return 0
    fi
    abort "VirtualBox kernel module ($MODNAME) NOT installed."
}

module_loaded()
{
    if test -z "$1"; then
        abort "missing argument to module_loaded()"
    fi

    if test "$REMOTE_INST" -eq 1; then
        return 1
    fi

    modname=$1
    # modinfo should now work properly since we prevent module autounloading.
    loadentry=`/usr/sbin/modinfo | grep "$modname "`
    if test -z "$loadentry"; then
        return 1
    fi
    return 0
}

vboxguest_loaded()
{
    module_loaded $MODNAME
    return $?
}

vboxfs_loaded()
{
    module_loaded $VFSMODNAME
    return $?
}

vboxms_loaded()
{
    module_loaded $VMSMODNAME
    return $?
}

check_root()
{
    # the reason we don't use "-u" is that some versions of id are old and do not
    # support this option (eg. Solaris 10) and do not have a "--version" to check it either
    # so go with the uglier but more generic approach
    idbin=`which id`
    isroot=`$idbin | grep "uid=0"`
    if test -z "$isroot"; then
        abort "This program must be run with administrator privileges.  Aborting"
    fi
}

start_module()
{
    if test "$REMOTE_INST" -eq 1; then
        /usr/sbin/add_drv $BASEDIR_OPT -i'pci80ee,cafe' -m'* 0666 root sys' $MODNAME 2>/dev/null || \
            abort "Failed to install VirtualBox guest kernel module into ${PKG_INSTALL_ROOT}."
        info "VirtualBox guest kernel module installed."
        return
    fi

    /usr/sbin/add_drv -i'pci80ee,cafe' -m'* 0666 root sys' $MODNAME
    if test ! vboxguest_loaded; then
        abort "Failed to load VirtualBox guest kernel module."
    elif test -c "/devices/pci@0,0/pci80ee,cafe@4:$MODNAME"; then
        info "VirtualBox guest kernel module loaded."
    else
        info "VirtualBox guest kernel module failed to attach."
    fi
}

stop_module()
{
    if test "$REMOTE_INST" -eq 1; then
        /usr/sbin/rem_drv $BASEDIR_OPT $MODNAME || abort "Failed to uninstall VirtualBox guest kernel module."
        info "VirtualBox guest kernel module uninstalled."
        return
    fi

    if vboxguest_loaded; then
        /usr/sbin/rem_drv $MODNAME || abort "Failed to unload VirtualBox guest kernel module."
        info "VirtualBox guest kernel module unloaded."
    elif test -z "$SILENTUNLOAD"; then
        info "VirtualBox guest kernel module not loaded."
    fi
}

start_vboxfs()
{
    if test "$REMOTE_INST" -eq 1; then
        return
    fi

    if vboxfs_loaded; then
        info "VirtualBox FileSystem kernel module already loaded."
    else
        /usr/sbin/modload -p fs/$VFSMODNAME || abort "Failed to load VirtualBox FileSystem kernel module."
        if test ! vboxfs_loaded; then
            info "Failed to load VirtualBox FileSystem kernel module."
        else
            info "VirtualBox FileSystem kernel module loaded."
        fi
    fi
}

stop_vboxfs()
{
    if test "$REMOTE_INST" -eq 1; then
        return
    fi

    if vboxfs_loaded; then
        vboxfs_mod_id=`/usr/sbin/modinfo | grep $VFSMODNAME | cut -f 1 -d ' ' `
        if test -n "$vboxfs_mod_id"; then
            /usr/sbin/modunload -i $vboxfs_mod_id || abort "Failed to unload VirtualBox FileSystem module."
            info "VirtualBox FileSystem kernel module unloaded."
        fi
    elif test -z "$SILENTUNLOAD"; then
        info "VirtualBox FileSystem kernel module not loaded."
    fi
}

start_vboxms()
{
    if test "$REMOTE_INST" -eq 1; then
        /usr/sbin/add_drv $BASEDIR_OPT -m'* 0666 root sys' $VMSMODNAME 2>/dev/null ||
            abort "Failed to install VirtualBox pointer integration module."
        info "VirtualBox pointer integration module installed."
        return
    fi

    /usr/sbin/add_drv -m'* 0666 root sys' $VMSMODNAME
    if test ! vboxms_loaded; then
        abort "Failed to load VirtualBox pointer integration module."
    elif test -c "/devices/pseudo/$VMSMODNAME@0:$VMSMODNAME"; then
        info "VirtualBox pointer integration module loaded."
    else
        info "VirtualBox pointer integration module failed to attach."
    fi
}

stop_vboxms()
{
    if test "$REMOTE_INST" -eq 1; then
        /usr/sbin/rem_drv $BASEDIR_OPT $VMSMODNAME || abort "Failed to uninstall VirtualBox pointer integration module."
        info "VirtualBox pointer integration module uninstalled."
        return
    fi

    if vboxms_loaded; then
        /usr/sbin/rem_drv $VMSMODNAME || abort "Failed to unload VirtualBox pointer integration module."
        info "VirtualBox pointer integration module unloaded."
    elif test -z "$SILENTUNLOAD"; then
        info "VirtualBox pointer integration module not loaded."
    fi
}

status_module()
{
    if vboxguest_loaded; then
        info "Running."
    else
        info "Stopped."
    fi
}

stop_all()
{
    stop_vboxms
    stop_vboxfs
    stop_module
    return 0
}

restart_all()
{
    stop_all
    start_module
    start_vboxfs
    start_vboxms
    return 0
}

# "Remote" installs ('pkgadd -R') can skip many of the steps below.
REMOTE_INST=0
BASEDIR_OPT=""
if test "x${PKG_INSTALL_ROOT:-/}" != "x/"; then
    BASEDIR_OPT="-b $PKG_INSTALL_ROOT"
    REMOTE_INST=1
fi
export REMOTE_INST
export BASEDIR_OPT

check_root
check_if_installed

if test "$2" = "silentunload"; then
    SILENTUNLOAD="$2"
fi

case "$1" in
stopall)
    stop_all
    ;;
restartall)
    restart_all
    ;;
start)
    start_module
    start_vboxms
    ;;
stop)
    stop_vboxms
    stop_module
    ;;
status)
    status_module
    ;;
vfsstart)
    start_vboxfs
    ;;
vfsstop)
    stop_vboxfs
    ;;
vmsstart)
    start_vboxms
    ;;
vmsstop)
    stop_vboxms
    ;;
*)
    echo "Usage: $0 {start|stop|restart|status}"
    exit 1
esac

exit 0

