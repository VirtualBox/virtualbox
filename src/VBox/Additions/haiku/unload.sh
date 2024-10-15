#!/bin/bash
# $Id: unload.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Driver unload script.
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

basedir=/boot/home/config/add-ons/
rm -f $basedir/input_server/devices/VBoxMouse
rm -f $basedir/kernel/drivers/bin/vboxdev
rm -f $basedir/kernel/drivers/dev/misc/vboxdev
rm -f $basedir/kernel/file_systems/vboxsf
rm -f $basedir/kernel/generic/vboxguest
rm -rf /boot/apps/VBoxAdditions

