#!/bin/bash
# $Id: load.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Driver load script.
#

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

outdir=out/haiku.x86/debug/bin/additions
instdir=/boot/apps/VBoxAdditions


# vboxguest
mkdir -p ~/config/add-ons/kernel/generic/
cp $outdir/vboxguest ~/config/add-ons/kernel/generic/

# vboxdev
mkdir -p ~/config/add-ons/kernel/drivers/dev/misc/
cp $outdir/vboxdev ~/config/add-ons/kernel/drivers/bin/
ln -sf ../../bin/vboxdev ~/config/add-ons/kernel/drivers/dev/misc

# VBoxMouse
cp $outdir/VBoxMouse        ~/config/add-ons/input_server/devices/
cp $outdir/VBoxMouseFilter  ~/config/add-ons/input_server/filters/

# Services
mkdir -p $instdir
cp $outdir/VBoxService $instdir/
cp $outdir/VBoxTray    $instdir/
cp $outdir/VBoxControl $instdir/
ln -sf $instdir/VBoxService ~/config/boot/launch

