/* $Id: combined-agnostic.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuest - Combine a bunch of OS agnostic sources into one compile unit.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#define LOG_GROUP LOG_GROUP_DEFAULT
#include "internal/iprt.h"
#include <VBox/log.h>

//#undef LOG_GROUP
#include "VBoxGuestR0LibGenericRequest.c"
#undef LOG_GROUP
#include "VBoxGuestR0LibHGCMInternal.c"
//#undef LOG_GROUP
#include "VBoxGuestR0LibInit.c"
//#undef LOG_GROUP
#include "VBoxGuestR0LibPhysHeap.c"
#undef LOG_GROUP
#include "VBoxGuestR0LibVMMDev.c"
#undef LOG_GROUP
#include "r0drv/alloc-r0drv.c"
#undef LOG_GROUP
#include "r0drv/initterm-r0drv.c"
#undef LOG_GROUP
#include "r0drv/memobj-r0drv.c"
#undef LOG_GROUP
#include "r0drv/mpnotification-r0drv.c"
#undef LOG_GROUP
#include "r0drv/powernotification-r0drv.c"
#undef LOG_GROUP
#include "r0drv/generic/semspinmutex-r0drv-generic.c"
#undef LOG_GROUP
#include "common/alloc/alloc.c"
#undef LOG_GROUP
#include "common/checksum/crc32.c"
#undef LOG_GROUP
#include "common/err/errinfo.c"
#undef LOG_GROUP
#include "common/log/log.c"
#undef LOG_GROUP
#include "common/log/logellipsis.c"
#undef LOG_GROUP
#include "common/log/logrel.c"
#undef LOG_GROUP
#include "common/log/logrelellipsis.c"
#undef LOG_GROUP
#include "common/log/logcom.c"
#undef LOG_GROUP
#include "common/log/logformat.c"
#undef LOG_GROUP
#include "common/log/RTLogCreateEx.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg1Weak.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2Add.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2AddWeak.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2AddWeakV.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2Weak.c"
#undef LOG_GROUP
#include "common/misc/RTAssertMsg2WeakV.c"
#undef LOG_GROUP
#include "common/misc/assert.c"
#undef LOG_GROUP
#include "common/misc/thread.c"
#undef LOG_GROUP
#include "common/string/RTStrCat.c"
#undef LOG_GROUP
#include "common/string/RTStrCmp.c"
#undef LOG_GROUP
#include "common/string/RTStrCopy.c"
#undef LOG_GROUP
#include "common/string/RTStrCopyEx.c"
#undef LOG_GROUP
#include "common/string/RTStrCopyP.c"
#undef LOG_GROUP
#include "common/string/RTStrEnd.c"
#undef LOG_GROUP
#include "common/string/RTStrICmpAscii.c"
#undef LOG_GROUP
#include "common/string/RTStrNICmpAscii.c"
#undef LOG_GROUP
#include "common/string/RTStrNCmp.c"
#undef LOG_GROUP
#include "common/string/RTStrNLen.c"
#undef LOG_GROUP
#include "common/string/stringalloc.c"
#undef LOG_GROUP
#include "common/string/strformat.c"
#undef LOG_GROUP
#include "common/string/RTStrFormat.c"
#undef LOG_GROUP
#include "common/string/strformatnum.c"
#undef LOG_GROUP
#include "common/string/strformattype.c"
#undef LOG_GROUP
#include "common/string/strprintf.c"
#undef LOG_GROUP
#include "common/string/strprintf-ellipsis.c"
#undef LOG_GROUP
#include "common/string/strprintf2.c"
#undef LOG_GROUP
#include "common/string/strprintf2-ellipsis.c"
#undef LOG_GROUP
#include "common/string/strtonum.c"
#undef LOG_GROUP
#include "common/string/utf-8.c"
#undef LOG_GROUP
#include "common/table/avlpv.c"
#undef LOG_GROUP
#include "common/time/time.c"
#undef LOG_GROUP
#include "generic/RTAssertShouldPanic-generic.c"
#undef LOG_GROUP
#include "generic/RTLogWriteStdErr-stub-generic.c"
#undef LOG_GROUP
#include "generic/RTLogWriteStdOut-stub-generic.c"
#undef LOG_GROUP
#include "generic/RTMpGetCoreCount-generic.c"
#undef LOG_GROUP
#include "generic/RTSemEventWait-2-ex-generic.c"
#undef LOG_GROUP
#include "generic/RTSemEventWaitNoResume-2-ex-generic.c"
#undef LOG_GROUP
#include "generic/RTSemEventMultiWait-2-ex-generic.c"
#undef LOG_GROUP
#include "generic/RTSemEventMultiWaitNoResume-2-ex-generic.c"
#undef LOG_GROUP
#include "generic/rtStrFormatKernelAddress-generic.c"
#undef LOG_GROUP
#include "generic/errvars-generic.c"
#undef LOG_GROUP
#include "generic/mppresent-generic.c"
#undef LOG_GROUP
#include "generic/system-page-size-generic.c"
#undef LOG_GROUP
#include "VBox/log-vbox.c"
#undef LOG_GROUP
#include "VBox/logbackdoor.c"
#undef LOG_GROUP
#if defined(RT_ARCH_AMD) || defined(RT_ARCH_X86)
# include "VBox/RTLogWriteVmm-amd64-x86.c"
#else
# include "generic/RTLogWriteVmm-stub-generic.c"
#endif

#ifdef RT_ARCH_AMD64
# undef LOG_GROUP
# include "common/alloc/heapsimple.c"
#endif

#if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
# undef LOG_GROUP
# include "common/asm/ASMMultU64ByU32DivByU32-generic.c"
#endif

#if 0 //def RT_ARCH_X86 - iprt/nocrt/limit.h clashes.
# include "common/math/gcc/divdi3.c"
# include "common/math/gcc/moddi3.c"
# include "common/math/gcc/udivdi3.c"
# include "common/math/gcc/udivmoddi4.c"
# include "common/math/gcc/umoddi3.c"
# include "common/math/gcc/qdivrem.c"
#endif

