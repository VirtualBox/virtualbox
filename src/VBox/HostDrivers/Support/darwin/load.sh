#!/bin/bash
# $Id: load.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# For development.
#

#
# Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

XNU_VERSION=`LC_ALL=C uname -r | LC_ALL=C cut -d . -f 1`
DRVNAME="VBoxDrv.kext"
BUNDLE="org.virtualbox.kext.VBoxDrv"

DIR=`dirname "$0"`
DIR=`cd "$DIR" && pwd`
DIR="$DIR/$DRVNAME"
if [ ! -d "$DIR" ]; then
    echo "Cannot find $DIR or it's not a directory..."
    exit 1;
fi
if [ -n "$*" ]; then
  OPTS="$*"
else
  OPTS="-t"
fi

# Make sure VBoxUSB is unloaded as it might be using symbols from us.
LOADED=`kextstat -b org.virtualbox.kext.VBoxUSB -l`
if test -n "$LOADED"; then
    echo "load.sh: Unloading org.virtualbox.kext.VBoxUSB..."
    sudo kextunload -v 6 -b org.virtualbox.kext.VBoxUSB
    LOADED=`kextstat -b org.virtualbox.kext.VBoxUSB -l`
    if test -n "$LOADED"; then
        echo "load.sh: failed to unload org.virtualbox.kext.VBoxUSB, see above..."
        exit 1;
    fi
    echo "load.sh: Successfully unloaded org.virtualbox.kext.VBoxUSB"
fi

# Make sure VBoxNetFlt is unloaded as it might be using symbols from us.
LOADED=`kextstat -b org.virtualbox.kext.VBoxNetFlt -l`
if test -n "$LOADED"; then
    echo "load.sh: Unloading org.virtualbox.kext.VBoxNetFlt..."
    sudo kextunload -v 6 -b org.virtualbox.kext.VBoxNetFlt
    LOADED=`kextstat -b org.virtualbox.kext.VBoxNetFlt -l`
    if test -n "$LOADED"; then
        echo "load.sh: failed to unload org.virtualbox.kext.VBoxNetFlt, see above..."
        exit 1;
    fi
    echo "load.sh: Successfully unloaded org.virtualbox.kext.VBoxNetFlt"
fi

# Make sure VBoxNetAdp is unloaded as it might be using symbols from us.
LOADED=`kextstat -b org.virtualbox.kext.VBoxNetAdp -l`
if test -n "$LOADED"; then
    echo "load.sh: Unloading org.virtualbox.kext.VBoxNetAdp..."
    sudo kextunload -v 6 -b org.virtualbox.kext.VBoxNetAdp
    LOADED=`kextstat -b org.virtualbox.kext.VBoxNetAdp -l`
    if test -n "$LOADED"; then
        echo "load.sh: failed to unload org.virtualbox.kext.VBoxNetAdp, see above..."
        exit 1;
    fi
    echo "load.sh: Successfully unloaded org.virtualbox.kext.VBoxNetAdp"
fi

# Try unload any existing instance first.
LOADED=`kextstat -b $BUNDLE -l`
if test -n "$LOADED"; then
    echo "load.sh: Unloading $BUNDLE..."
    sudo kextunload -v 6 -b $BUNDLE
    LOADED=`kextstat -b $BUNDLE -l`
    if test -n "$LOADED"; then
        echo "load.sh: failed to unload $BUNDLE, see above..."
        exit 1;
    fi
    echo "load.sh: Successfully unloaded $BUNDLE"
fi

set -e

# Copy the .kext to the symbols directory and tweak the kextload options.
if test -n "$VBOX_DARWIN_SYMS"; then
    echo "load.sh: copying the extension the symbol area..."
    rm -Rf "$VBOX_DARWIN_SYMS/$DRVNAME"
    mkdir -p "$VBOX_DARWIN_SYMS"
    cp -R "$DIR" "$VBOX_DARWIN_SYMS/"
    OPTS="$OPTS -s $VBOX_DARWIN_SYMS/ "
    sync
fi

trap "sudo chown -R `whoami` $DIR; exit 1" INT
trap "sudo chown -R `whoami` $DIR; exit 1" ERR
# On smbfs, this might succeed just fine but make no actual changes,
# so we might have to temporarily copy the driver to a local directory.
if sudo chown -R root:wheel "$DIR"; then
    OWNER=`/usr/bin/stat -f "%u" "$DIR"`
else
    OWNER=1000
fi
if test "$OWNER" -ne 0; then
    TMP_DIR=/tmp/loaddrv.tmp
    echo "load.sh: chown didn't work on $DIR, using temp location $TMP_DIR/$DRVNAME"

    # clean up first (no sudo rm)
    if test -e "$TMP_DIR"; then
        sudo chown -R `whoami` "$TMP_DIR"
        rm -Rf "$TMP_DIR"
    fi

    # make a copy and switch over DIR
    mkdir -p "$TMP_DIR/"
    sudo cp -Rp "$DIR" "$TMP_DIR/"
    DIR="$TMP_DIR/$DRVNAME"

    # retry
    sudo chown -R root:wheel "$DIR"
fi
sudo chmod -R o-rwx "$DIR"
sync
echo "load.sh: loading $DIR..."

if [ "$XNU_VERSION" -ge "10" ]; then
    echo "${SCRIPT_NAME}.sh: loading $DIR... (kextutil $OPTS \"$DIR\")"
    sudo kextutil $OPTS "$DIR"
else
    sudo kextload $OPTS "$DIR"
fi
sync
sudo chown -R `whoami` "$DIR"
#sudo chmod 666 /dev/vboxdrv
kextstat | grep org.virtualbox.kext
if [ -n "${VBOX_DARWIN_SYMS}"  -a   "$XNU_VERSION" -ge "10" ]; then
    dsymutil -o "${VBOX_DARWIN_SYMS}/${DRVNAME}.dSYM" "${DIR}/Contents/MacOS/`basename -s .kext ${DRVNAME}`"
    sync
fi

