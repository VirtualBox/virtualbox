# $Id: Makefile 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $
## @file
# Makefile for the VirtualBox Linux Host Network Filter Driver.
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

# Linux kbuild sets this to our source directory if we are called from there
obj ?= $(CURDIR)
include $(obj)/Makefile-header.gmk
VBOXNETFLT_DIR := $(VBOX_MODULE_SRC_DIR)

# Allow building directly from the subdirectory without assuming the toplevel
# makefile has done the copying. Not the default use case, but can be handy.
ifndef KBUILD_EXTRA_SYMBOLS
KBUILD_EXTRA_SYMBOLS=$(abspath $(VBOXNETFLT_DIR)/../vboxdrv/Module.symvers)
endif

VBOXMOD_NAME = vboxnetflt
VBOXMOD_OBJS = \
	linux/VBoxNetFlt-linux.o \
	VBoxNetFlt.o \
	SUPR0IdcClient.o \
	SUPR0IdcClientComponent.o \
	linux/SUPR0IdcClient-linux.o
ifeq ($(VBOX_KBUILD_TARGET_ARCH),x86)
VBOXMOD_OBJS += \
	math/gcc/divdi3.o \
	math/gcc/moddi3.o \
	math/gcc/qdivrem.o \
	math/gcc/udivdi3.o \
	math/gcc/udivmoddi4.o \
	math/gcc/divdi3.o \
	math/gcc/umoddi3.o
endif
VBOXMOD_INCL = \
	$(VBOXNETFLT_DIR) \
	$(VBOXNETFLT_DIR)include \
	$(VBOXNETFLT_DIR)r0drv/linux
VBOXMOD_DEFS = \
	RT_OS_LINUX \
	IN_RING0 \
	IN_RT_R0 \
	IN_SUP_R0 \
	VBOX \
	RT_WITH_VBOX \
	VBOX_WITH_HARDENING \
	VBOX_WITH_64_BITS_GUESTS # <-- must be consistent with Config.kmk!
VBOXMOD_CFLAGS = -include $(VBOXNETFLT_DIR)include/VBox/SUPDrvMangling.h -fno-pie -Wno-declaration-after-statement

include $(obj)/Makefile-footer.gmk

