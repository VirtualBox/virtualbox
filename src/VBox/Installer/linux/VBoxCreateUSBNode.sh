#! /bin/sh
# $Id: VBoxCreateUSBNode.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
## @file
# VirtualBox USB Proxy Service, Linux Specialization.
# udev helper for creating and removing device nodes for VirtualBox USB devices
#

#
# Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

# Constant, from the USB specifications
usb_class_hub=09

do_remove=0
case "$1" in "--remove")
  do_remove=1; shift;;
esac
bus=`expr "$2" '/' 128 + 1`
device=`expr "$2" '%' 128 + 1`
class="$3"
group="$4"
devdir="`printf "/dev/vboxusb/%.3d" $bus`"
devpath="`printf "/dev/vboxusb/%.3d/%.3d" $bus $device`"
case "$do_remove" in
  0)
  if test -n "$class" -a "$class" = "$usb_class_hub"
  then
      exit 0
  fi
  case "$group" in "") group="vboxusers";; esac
  mkdir /dev/vboxusb -m 0750 2>/dev/null
  chown root:$group /dev/vboxusb 2>/dev/null
  mkdir "$devdir" -m 0750 2>/dev/null
  chown root:$group "$devdir" 2>/dev/null
  mknod "$devpath" c $1 $2 -m 0660 2>/dev/null
  chown root:$group "$devpath" 2>/dev/null
  ;;
  1)
  rm -f "$devpath"
  ;;
esac

