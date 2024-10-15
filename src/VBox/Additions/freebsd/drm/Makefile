# $Id: Makefile 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Makefile for the VirtualBox FreeBSD Host Driver.
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
# SPDX-License-Identifier: GPL-3.0-only
#

KMOD = vboxvideo

SRCS = \
	vboxvideo_drm.c

SRCS += device_if.h bus_if.h pci_if.h opt_drm.h

.include <bsd.kmod.mk>

