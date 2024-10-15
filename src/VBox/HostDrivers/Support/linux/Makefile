# $Id: Makefile 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Makefile for the VirtualBox Linux Host Driver.
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
VBOXDRV_DIR := $(VBOX_MODULE_SRC_DIR)

VBOXMOD_NAME = vboxdrv
VBOXMOD_OBJS = \
	linux/SUPDrv-linux.o \
	SUPDrv.o \
	SUPDrvGip.o \
	SUPDrvSem.o \
	SUPDrvTracer.o \
	SUPLibAll.o \
	common/string/strformatrt.o

ifndef VBOX_WITHOUT_COMBINED_SOURCES
VBOXMOD_OBJS += \
	combined-agnostic1.o \
	combined-agnostic2.o \
	combined-os-specific.o
else  # VBOX_WITHOUT_COMBINED_SOURCES
VBOXMOD_OBJS += \
	r0drv/alloc-r0drv.o \
	r0drv/initterm-r0drv.o \
	r0drv/memobj-r0drv.o \
	r0drv/mpnotification-r0drv.o \
	r0drv/powernotification-r0drv.o \
	r0drv/linux/assert-r0drv-linux.o \
	r0drv/linux/alloc-r0drv-linux.o \
	r0drv/linux/initterm-r0drv-linux.o \
	r0drv/linux/memobj-r0drv-linux.o \
	r0drv/linux/memuserkernel-r0drv-linux.o \
	r0drv/linux/mp-r0drv-linux.o \
	r0drv/linux/mpnotification-r0drv-linux.o \
	r0drv/linux/process-r0drv-linux.o \
	r0drv/linux/rtStrFormatKernelAddress-r0drv-linux.o \
	r0drv/linux/semevent-r0drv-linux.o \
	r0drv/linux/semeventmulti-r0drv-linux.o \
	r0drv/linux/semfastmutex-r0drv-linux.o \
	r0drv/linux/semmutex-r0drv-linux.o \
	r0drv/linux/spinlock-r0drv-linux.o \
	r0drv/linux/thread-r0drv-linux.o \
	r0drv/linux/thread2-r0drv-linux.o \
	r0drv/linux/threadctxhooks-r0drv-linux.o \
	r0drv/linux/time-r0drv-linux.o \
	r0drv/linux/timer-r0drv-linux.o \
	r0drv/generic/semspinmutex-r0drv-generic.o \
	common/alloc/alloc.o \
	common/checksum/crc32.o \
	common/checksum/ipv4.o \
	common/checksum/ipv6.o \
	common/err/RTErrConvertFromErrno.o \
	common/err/RTErrConvertToErrno.o \
	common/err/errinfo.o \
	common/log/log.o \
	common/log/logellipsis.o \
	common/log/logrel.o \
	common/log/logrelellipsis.o \
	common/log/logcom.o \
	common/log/logformat.o \
	common/log/RTLogCreateEx.o \
	common/misc/RTAssertMsg1Weak.o \
	common/misc/RTAssertMsg2.o \
	common/misc/RTAssertMsg2Add.o \
	common/misc/RTAssertMsg2AddWeak.o \
	common/misc/RTAssertMsg2AddWeakV.o \
	common/misc/RTAssertMsg2Weak.o \
	common/misc/RTAssertMsg2WeakV.o \
	common/misc/assert.o \
	common/misc/handletable.o \
	common/misc/handletablectx.o \
	common/misc/thread.o \
	common/string/RTStrCat.o \
	common/string/RTStrCopy.o \
	common/string/RTStrCopyEx.o \
	common/string/RTStrCopyP.o \
	common/string/RTStrEnd.o \
	common/string/RTStrNCmp.o \
	common/string/RTStrNLen.o \
	common/string/stringalloc.o \
	common/string/strformat.o \
	common/string/RTStrFormat.o \
	common/string/strformatnum.o \
	common/string/strformattype.o \
	common/string/strprintf.o \
	common/string/strprintf-ellipsis.o \
	common/string/strprintf2.o \
	common/string/strprintf2-ellipsis.o \
	common/string/strtonum.o \
	common/table/avlpv.o \
	common/time/time.o \
	r0drv/linux/RTLogWriteDebugger-r0drv-linux.o \
	generic/RTAssertShouldPanic-generic.o \
	generic/RTLogWriteStdErr-stub-generic.o \
	generic/RTLogWriteStdOut-stub-generic.o \
	generic/RTLogWriteUser-generic.o \
	generic/RTMpGetArraySize-generic.o \
	generic/RTMpGetCoreCount-generic.o \
	generic/RTSemEventWait-2-ex-generic.o \
	generic/RTSemEventWaitNoResume-2-ex-generic.o \
	generic/RTSemEventMultiWait-2-ex-generic.o \
	generic/RTSemEventMultiWaitNoResume-2-ex-generic.o \
	generic/RTTimerCreate-generic.o \
	generic/errvars-generic.o \
	generic/mppresent-generic.o \
	generic/uuid-generic.o \
	VBox/log-vbox.o \
	VBox/RTLogWriteVmm-amd64-x86.o
 ifeq ($(VBOX_KBUILD_TARGET_ARCH),amd64)
VBOXMOD_OBJS += common/alloc/heapsimple.o
 endif
endif # VBOX_WITHOUT_COMBINED_SOURCES
ifeq ($(VBOX_KBUILD_TARGET_ARCH),x86)
VBOXMOD_OBJS += \
	math/gcc/divdi3.o \
	math/gcc/divmoddi4.o \
	math/gcc/moddi3.o \
	math/gcc/qdivrem.o \
	math/gcc/udivdi3.o \
	math/gcc/udivmoddi4.o \
	math/gcc/divdi3.o \
	math/gcc/umoddi3.o
endif
ifdef VBOX_WITH_NATIVE_DTRACE
VBOXMOD_OBJS += SUPDrvDTrace.o
endif

VBOXMOD_INCL  = \
	$(VBOXDRV_DIR) \
	$(VBOXDRV_DIR)include \
	$(VBOXDRV_DIR)r0drv/linux
ifdef VBOX_WITH_NATIVE_DTRACE
VBOXMOD_INCL += \
	/usr/include/linux \
	/usr/include
endif

VBOXMOD_DEFS = \
	RT_OS_LINUX \
	IN_RING0 \
	IN_RT_R0 \
	IN_SUP_R0 \
	VBOX \
	RT_WITH_VBOX \
	VBOX_WITH_HARDENING \
	SUPDRV_WITH_RELEASE_LOGGER \
	VBOX_WITHOUT_EFLAGS_AC_SET_IN_VBOXDRV \
	IPRT_WITHOUT_EFLAGS_AC_PRESERVING \
	VBOX_WITH_64_BITS_GUESTS # <-- must be consistent with Config.kmk!
ifndef CONFIG_VBOXDRV_FIXEDMAJOR
VBOXMOD_DEFS += CONFIG_VBOXDRV_AS_MISC
endif
ifdef VBOX_WITH_NATIVE_DTRACE
VBOXMOD_DEFS += VBOX_WITH_NATIVE_DTRACE
endif
ifdef VBOX_WITH_TEXT_MODMEM_HACK
VBOXMOD_DEFS += RTMEMALLOC_EXEC_HEAP VBOX_WITH_TEXT_MODMEM_HACK
endif
VBOXMOD_CFLAGS = -include $(VBOXDRV_DIR)include/VBox/SUPDrvMangling.h \
	-fno-omit-frame-pointer -fno-pie -Wno-declaration-after-statement


include $(obj)/Makefile-footer.gmk

check: $(VBOXMOD_0_TARGET)
	@if ! readelf -p __ksymtab_strings vboxdrv.ko | grep -E "\[.*\]  *(RT|g_..*RT.*)"; then \
	    echo "All exported IPRT symbols are properly renamed!"; \
	 else \
	    echo "error: Some exported IPRT symbols was not properly renamed! See above." >&2; \
	    false; \
	 fi

