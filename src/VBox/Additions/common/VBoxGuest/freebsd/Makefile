# $Id: Makefile 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox Guest Additions Module Makefile.
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
KMOD = vboxguest

CFLAGS += -DRT_OS_FREEBSD -DIN_RING0 -DIN_RT_R0 -DIN_SUP_R0 -DVBOX -DRT_WITH_VBOX -Iinclude -I. -Ir0drv -w -DVBGL_VBOXGUEST -DVBOX_WITH_HGCM -DVBOX_WITH_64_BITS_GUESTS

.if (${MACHINE_ARCH} == "i386")
 CFLAGS += -DRT_ARCH_X86
.elif (${MACHINE_ARCH} == "amd64")
 CFLAGS += -DRT_ARCH_AMD64
.endif

SRCS = \
	VBoxGuest.c \
	VBoxGuest-freebsd.c \
	VBoxGuestR0LibGenericRequest.c \
	VBoxGuestR0LibHGCMInternal.c \
	VBoxGuestR0LibInit.c \
	VBoxGuestR0LibPhysHeap.c \
	VBoxGuestR0LibVMMDev.c

# Include needed interface headers so they are created during build
SRCS += \
	device_if.h \
	bus_if.h \
	pci_if.h \

.PATH:	${.CURDIR}/alloc
SRCS += \
	heapsimple.c

.PATH:	${.CURDIR}/common/err
SRCS += \
	RTErrConvertFromErrno.c \
	RTErrConvertToErrno.c \
	errinfo.c

.PATH:	${.CURDIR}/common/log
SRCS += \
	log.c \
	logellipsis.c \
	logrel.c \
	logrelellipsis.c \
	logcom.c \
	logformat.c \
	RTLogCreateEx.c

.PATH:	${.CURDIR}/common/misc
SRCS += \
	RTAssertMsg1Weak.c \
	RTAssertMsg2.c \
	RTAssertMsg2Add.c \
	RTAssertMsg2AddWeak.c \
	RTAssertMsg2AddWeakV.c \
	RTAssertMsg2Weak.c \
	RTAssertMsg2WeakV.c \
	assert.c \
	handletable.c \
	handletablectx.c \
	once.c \
	thread.c

.PATH:	${.CURDIR}/common/string
SRCS += \
	RTStrCat.c \
	RTStrCmp.c \
	RTStrCopy.c \
	RTStrCopyEx.c \
	RTStrCopyP.c \
	RTStrEnd.c \
	RTStrICmpAscii.c \
	RTStrNICmpAscii.c \
	RTStrNCmp.c \
	RTStrNLen.c \
	stringalloc.c \
	strformat.c \
	RTStrFormat.c \
	strformatnum.c \
	strformatrt.c \
	strformattype.c \
	strprintf.c \
	strprintf-ellipsis.c \
	strprintf2.c \
	strprintf2-ellipsis.c \
	strtonum.c \
	memchr.c \
	utf-8.c

.PATH:	${.CURDIR}/common/rand
SRCS += \
	rand.c \
	randadv.c \
	randparkmiller.c

.PATH:	${.CURDIR}/common/path
SRCS += \
	RTPathStripFilename.c

.PATH:	${.CURDIR}/common/checksum
SRCS += \
	crc32.c \
	ipv4.c

.PATH:	${.CURDIR}/common/table
SRCS += \
	avlpv.c

.PATH:	${.CURDIR}/common/time
SRCS += \
	time.c

.PATH:	${.CURDIR}/generic
SRCS += \
	uuid-generic.c \
	RTAssertShouldPanic-generic.c \
	RTLogWriteDebugger-generic.c \
	RTLogWriteStdOut-stub-generic.c \
	RTLogWriteStdErr-stub-generic.c \
	RTRandAdvCreateSystemFaster-generic.c \
	RTRandAdvCreateSystemTruer-generic.c \
	RTSemEventWait-2-ex-generic.c \
	RTSemEventWaitNoResume-2-ex-generic.c \
	RTSemEventMultiWait-2-ex-generic.c \
	RTSemEventMultiWaitNoResume-2-ex-generic.c \
	RTTimerCreate-generic.c \
	rtStrFormatKernelAddress-generic.c \
	timer-generic.c \
	errvars-generic.c \
	mppresent-generic.c

.PATH:	${.CURDIR}/r0drv
SRCS += \
	alloc-r0drv.c \
	initterm-r0drv.c \
	memobj-r0drv.c \
	powernotification-r0drv.c

.PATH:	${.CURDIR}/r0drv/freebsd
SRCS += \
	assert-r0drv-freebsd.c \
	alloc-r0drv-freebsd.c \
	initterm-r0drv-freebsd.c \
	memobj-r0drv-freebsd.c \
	memuserkernel-r0drv-freebsd.c \
	mp-r0drv-freebsd.c \
	process-r0drv-freebsd.c \
	semevent-r0drv-freebsd.c \
	semeventmulti-r0drv-freebsd.c \
	semfastmutex-r0drv-freebsd.c \
	semmutex-r0drv-freebsd.c \
	spinlock-r0drv-freebsd.c \
	thread-r0drv-freebsd.c \
	thread2-r0drv-freebsd.c \
	time-r0drv-freebsd.c

.PATH:	${.CURDIR}/r0drv/generic
SRCS += \
	semspinmutex-r0drv-generic.c \
	mpnotification-r0drv-generic.c \
	RTMpIsCpuWorkPending-r0drv-generic.c

.PATH:	${.CURDIR}/VBox
SRCS += \
	log-vbox.c \
	logbackdoor.c \
	RTLogWriteVmm-amd64-x86.

.include <bsd.kmod.mk>

