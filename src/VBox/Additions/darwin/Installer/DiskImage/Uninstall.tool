#!/bin/sh
# $Id: Uninstall.tool 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## #file
# VirtualBox Guest Additions uninstall script.
#

#
# Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

# Override any funny stuff from the user.
export PATH="/bin:/usr/bin:/sbin:/usr/sbin:$PATH"

#
# Display a simple welcome message first.
#
echo ""
echo "Welcome to the VirtualBox Guest Additions uninstall script."
echo ""

# Check if user interraction is required to start uninstall process.
fUnattended=0
if test "$#" != "0"; then
    if test "$#" != "1" -o "$1" != "--unattended"; then
            echo "Error: Unknown argument(s): $*"
            echo ""
            echo "Usage: $0 [--unattended]"
            echo ""
            echo "If the '--unattended' option is not given, you will be prompted"
            echo "for a Yes/No before doing the actual uninstallation."
            echo ""
        exit 4;
    fi
    fUnattended="Yes"
fi

if test "$fUnattended" != "Yes"; then
    echo "Do you wish to continue none the less (Yes/No)?"
    read fUnattended
    if test "$fUnattended" != "Yes"  -a  "$fUnattended" != "YES"  -a  "$fUnattended" != "yes"; then
        echo "Aborting uninstall. (answer: '$fUnattended')".
        exit 2;
    fi
    echo ""
fi

# Stop services
echo "Checking running services..."
unload()
{
    ITEM_ID=$1
    ITEM_PATH=$2
    FORCED_USER=$3

    echo "Unloading $ITEM_ID"


    loaded="NO"
    test -n "$(sudo -u "$FORCED_USER" launchctl list | grep $ITEM_ID)" && loaded="YES"
    if [ "$loaded" = "YES" ] ; then
        sudo -p "Please enter $FORCED_USER's password (unloading $ITEM_ID):" sudo -u "$FORCED_USER" launchctl unload -F "$ITEM_PATH/$ITEM_ID.plist"
    fi

}

unload "org.virtualbox.additions.vboxservice" "/Library/LaunchDaemons" "root"
unload "org.virtualbox.additions.vboxclient" "/Library/LaunchAgents" `whoami`

# Unload kernel extensions
echo "Checking running kernel extensions..."
items="VBoxGuest"
for item in $items; do
    kext_item="org.virtualbox.kext.$item"
    loaded=`kextstat | grep $kext_item`
    if [ ! -z "$loaded" ] ; then
        echo "Unloading $item kernel extension"
        sudo -p "Please enter %u's password (unloading $item):" kextunload -b $kext_item
    fi
done

# Remove files and directories
echo "Checking files and directories..."
sudo -p "Please enter %u's password (removing files and directories):" rm -rf "/Library/Application Support/VirtualBox Guest Additions"
sudo -p "Please enter %u's password (removing files and directories):" rm -rf "/Library/Extensions/VBoxGuest.kext"
sudo -p "Please enter %u's password (removing files and directories):" rm -rf "/Library/LaunchAgents/org.virtualbox.additions.vboxclient.plist"
sudo -p "Please enter %u's password (removing files and directories):" rm -rf "/Library/LaunchDaemons/org.virtualbox.additions.vboxservice.plist"

# Cleaning up pkgutil database
echo "Checking package database ..."
items="kexts tools-and-services"
for item in $items; do
    pkg_item="org.virtualbox.pkg.additions.$item"
    installed=`pkgutil --pkgs="$pkg_item"`
    if [ ! -z "$installed" ] ; then
        sudo -p "Please enter %u's password (removing $pkg_item):" pkgutil --forget "$pkg_item"
    fi
done

# Remove our kexts from the cache.
echo "Updating kernel cache."
sudo -p "Please enter %u's password (refreshing kext cache):" touch "/System/Library/Extensions/"
sudo -p "Please enter %u's password (refreshing kext cache):" kextcache -update-volume /

echo "Done."
exit 0;

