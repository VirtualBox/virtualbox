#!/bin/sh
# $Id: prerm-common.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Oracle VirtualBox
# VirtualBox Linux pre-uninstaller common portions
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

# Put bits of the pre-uninstallation here which should work the same for all of
# the Linux installers.  We do not use special helpers (e.g. dh_* on Debian),
# but that should not matter, as we know what those helpers actually do, and we
# have to work on those systems anyway when installed using the all
# distributions installer.
#
# We assume that all required files are in the same folder as this script
# (e.g. /opt/VirtualBox, /usr/lib/VirtualBox, the build output directory).
#
# Script exit status: 0 on success, 1 if VirtualBox is running and can not be
# stopped (installers may show an error themselves or just pass on standard
# error).


# The below is GNU-specific.  See VBox.sh for the longer Solaris/OS X version.
TARGET=`readlink -e -- "${0}"` || exit 1
MY_PATH="${TARGET%/[!/]*}"
cd "${MY_PATH}"
. "./routines.sh"

# Stop the balloon control service
stop_init_script vboxballoonctrl-service >/dev/null 2>&1
# Stop the autostart service
stop_init_script vboxautostart-service >/dev/null 2>&1
# Stop the web service
stop_init_script vboxweb-service >/dev/null 2>&1
# Do this check here after we terminated the web service: check whether VBoxSVC
# is running and exit if it can't be stopped.
check_running
# Terminate VBoxNetDHCP if running
terminate_proc VBoxNetDHCP
# Terminate VBoxNetNAT if running
terminate_proc VBoxNetNAT
delrunlevel vboxballoonctrl-service
remove_init_script vboxballoonctrl-service
delrunlevel vboxautostart-service
remove_init_script vboxautostart-service
delrunlevel vboxweb-service
remove_init_script vboxweb-service
# Stop kernel module and uninstall runlevel script
stop_init_script vboxdrv >/dev/null 2>&1
delrunlevel vboxdrv
remove_init_script vboxdrv
# And do final clean-up
"${MY_PATH}/vboxdrv.sh" cleanup >/dev/null  # Do not silence errors for now
# Stop host networking and uninstall runlevel script (obsolete)
stop_init_script vboxnet >/dev/null 2>&1
delrunlevel vboxnet >/dev/null 2>&1
remove_init_script vboxnet >/dev/null 2>&1
finish_init_script_install
rm -f /sbin/vboxconfig
exit 0
