#!/bin/sh
# $Id: postinst-common.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Oracle VirtualBox
# VirtualBox Linux post-installer common portions
#

#
# Copyright (C) 2015-2024 Oracle and/or its affiliates.
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

# Put bits of the post-installation here which should work the same for all of
# the Linux installers.  We do not use special helpers (e.g. dh_* on Debian),
# but that should not matter, as we know what those helpers actually do, and we
# have to work on those systems anyway when installed using the all
# distributions installer.
#
# We assume that all required files are in the same folder as this script
# (e.g. /opt/VirtualBox, /usr/lib/VirtualBox, the build output directory).

# The below is GNU-specific.  See VBox.sh for the longer Solaris/OS X version.
TARGET=`readlink -e -- "${0}"` || exit 1
MY_PATH="${TARGET%/[!/]*}"
cd "${MY_PATH}"
. "./routines.sh"

START=true
while test -n "${1}"; do
    case "${1}" in
        --nostart)
            START=
            ;;
        *)
            echo "Bad argument ${1}" >&2
            exit 1
            ;;
    esac
    shift
done

# Remove any traces of DKMS from previous installations.
for i in vboxhost vboxdrv vboxnetflt vboxnetadp; do
    rm -rf "/var/lib/dkms/${i}"*
done

# Install runlevel scripts and systemd unit files
install_init_script "${MY_PATH}/vboxdrv.sh" vboxdrv
if [ -f ${MY_PATH}/vboxballoonctrl-service.sh ]; then
    install_init_script "${MY_PATH}/vboxballoonctrl-service.sh" vboxballoonctrl-service
fi
if [ -f ${MY_PATH}/vboxautostart-service.sh ]; then
    install_init_script "${MY_PATH}/vboxautostart-service.sh" vboxautostart-service
fi
if [ -f ${MY_PATH}/vboxweb-service.sh ]; then
    install_init_script "${MY_PATH}/vboxweb-service.sh" vboxweb-service
fi
finish_init_script_install

delrunlevel vboxdrv
addrunlevel vboxdrv
delrunlevel vboxballoonctrl-service
if [ -f ${MY_PATH}/vboxballoonctrl-service.sh ]; then
    addrunlevel vboxballoonctrl-service
fi
delrunlevel vboxautostart-service
if [ -f ${MY_PATH}/vboxautostart-service.sh ]; then
    addrunlevel vboxautostart-service
fi
delrunlevel vboxweb-service
if [ -f ${MY_PATH}/vboxweb-service.sh ]; then
    addrunlevel vboxweb-service
fi

ln -sf "${MY_PATH}/postinst-common.sh" /sbin/vboxconfig

# Set SELinux permissions
# XXX SELinux: allow text relocation entries
if [ -x /usr/bin/chcon ]; then
    chcon -t texrel_shlib_t "${MY_PATH}"/*VBox* > /dev/null 2>&1
    chcon -t texrel_shlib_t "${MY_PATH}"/VBoxAuth.so \
        > /dev/null 2>&1
    chcon -t texrel_shlib_t "${MY_PATH}"/VirtualBox.so \
        > /dev/null 2>&1
    chcon -t texrel_shlib_t "${MY_PATH}"/components/VBox*.so \
        > /dev/null 2>&1
    chcon -t java_exec_t    "${MY_PATH}"/VirtualBox > /dev/null 2>&1
    chcon -t java_exec_t    "${MY_PATH}"/VBoxSDL > /dev/null 2>&1
    chcon -t java_exec_t    "${MY_PATH}"/VBoxHeadless \
        > /dev/null 2>&1
    chcon -t java_exec_t    "${MY_PATH}"/VBoxNetDHCP \
        > /dev/null 2>&1
    chcon -t java_exec_t    "${MY_PATH}"/VBoxNetNAT \
        > /dev/null 2>&1
    chcon -t java_exec_t    "${MY_PATH}"/VBoxExtPackHelperApp \
        > /dev/null 2>&1
    chcon -t java_exec_t    "${MY_PATH}"/vboxwebsrv > /dev/null 2>&1
    chcon -t bin_t          "${MY_PATH}"/src/vboxhost/build_in_tmp \
         > /dev/null 2>&1
    chcon -t bin_t          /usr/share/virtualbox/src/vboxhost/build_in_tmp \
         > /dev/null 2>&1
fi

test -n "${START}" &&
{
    if ! "${MY_PATH}/vboxdrv.sh" setup; then
        "${MY_PATH}/check_module_dependencies.sh" >&2
        echo >&2
        echo "There were problems setting up VirtualBox.  To re-start the set-up process, run" >&2
        echo "  /sbin/vboxconfig" >&2
        echo "as root.  If your system is using EFI Secure Boot you may need to sign the" >&2
        echo "kernel modules (vboxdrv, vboxnetflt, vboxnetadp, vboxpci) before you can load" >&2
        echo "them. Please see your Linux system's documentation for more information." >&2
    else
        start_init_script vboxdrv
        if [ -f ${MY_PATH}/vboxballoonctrl-service.sh ]; then
            start_init_script vboxballoonctrl-service
        fi
        if [ -f ${MY_PATH}/vboxautostart-service.sh ]; then
            start_init_script vboxautostart-service
        fi
        start_init_script vboxweb-service
    fi
}
