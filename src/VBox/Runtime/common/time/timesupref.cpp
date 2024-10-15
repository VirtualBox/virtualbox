/* $Id: timesupref.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Time using SUPLib, the C Implementation.
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

#if !defined(IN_GUEST) && !defined(RT_NO_GIP)


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/time.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/asm-math.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <VBox/sup.h>
#ifdef IN_RC
# include <VBox/vmm/vmm.h>
# include <VBox/vmm/vm.h>
#endif
#include "internal/time.h"


#define TMPL_MODE_SYNC_INVAR_NO_DELTA       1
#define TMPL_MODE_SYNC_INVAR_WITH_DELTA     2
#define TMPL_MODE_ASYNC                     3


/*
 * Use the XCHG instruction for some kind of serialization.
 */
#define TMPL_READ_FENCE()        ASMReadFence()

#undef  TMPL_MODE
#define TMPL_MODE                TMPL_MODE_SYNC_INVAR_NO_DELTA
#undef  TMPL_GET_CPU_METHOD
#define TMPL_GET_CPU_METHOD      0
#undef  rtTimeNanoTSInternalRef
#define rtTimeNanoTSInternalRef  RTTimeNanoTSLegacySyncInvarNoDelta
#include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacySyncInvarNoDelta);

#ifdef IN_RING3

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_SYNC_INVAR_WITH_DELTA
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacySyncInvarWithDeltaUseApicId
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacySyncInvarWithDeltaUseApicId);

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_SYNC_INVAR_WITH_DELTA
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID_EXT_0B
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacySyncInvarWithDeltaUseApicIdExt0B
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacySyncInvarWithDeltaUseApicIdExt0B);

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_SYNC_INVAR_WITH_DELTA
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID_EXT_8000001E
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacySyncInvarWithDeltaUseApicIdExt8000001E
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacySyncInvarWithDeltaUseApicIdExt8000001E);

# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacySyncInvarWithDeltaUseRdtscp
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacySyncInvarWithDeltaUseRdtscp);

# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacySyncInvarWithDeltaUseIdtrLim
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacySyncInvarWithDeltaUseIdtrLim);

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_ASYNC
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacyAsyncUseApicId
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacyAsyncUseApicId);

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_ASYNC
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID_EXT_0B
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacyAsyncUseApicIdExt0B
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacyAsyncUseApicIdExt0B);

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_ASYNC
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID_EXT_8000001E
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacyAsyncUseApicIdExt8000001E
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacyAsyncUseApicIdExt8000001E);

# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacyAsyncUseRdtscp
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacyAsyncUseRdtscp);

# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacyAsyncUseIdtrLim
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacyAsyncUseIdtrLim);

# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_RDTSCP_GROUP_IN_CH_NUMBER_IN_CL
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacyAsyncUseRdtscpGroupChNumCl
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacyAsyncUseRdtscpGroupChNumCl);

#else  /* IN_RC || IN_RING0: Disable interrupts and call getter function. */

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_SYNC_INVAR_WITH_DELTA
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     UINT32_MAX
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacySyncInvarWithDelta
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacySyncInvarWithDelta);

# undef  TMPL_MODE
# define TMPL_MODE TMPL_MODE_ASYNC
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLegacyAsync
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLegacyAsync);

#endif


/*
 * Use LFENCE for load serialization.
 */
#undef  TMPL_READ_FENCE
#define TMPL_READ_FENCE()        ASMReadFenceSSE2()

#undef  TMPL_MODE
#define TMPL_MODE                TMPL_MODE_SYNC_INVAR_NO_DELTA
#undef  TMPL_GET_CPU_METHOD
#define TMPL_GET_CPU_METHOD      0
#undef  rtTimeNanoTSInternalRef
#define rtTimeNanoTSInternalRef  RTTimeNanoTSLFenceSyncInvarNoDelta
#include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceSyncInvarNoDelta);

#ifdef IN_RING3

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_SYNC_INVAR_WITH_DELTA
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicId
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicId);

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_SYNC_INVAR_WITH_DELTA
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID_EXT_0B
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicIdExt0B
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicIdExt0B);

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_SYNC_INVAR_WITH_DELTA
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID_EXT_8000001E
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicIdExt8000001E
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicIdExt8000001E);

# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceSyncInvarWithDeltaUseRdtscp
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceSyncInvarWithDeltaUseRdtscp);

# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceSyncInvarWithDeltaUseIdtrLim
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceSyncInvarWithDeltaUseIdtrLim);

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_ASYNC
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceAsyncUseApicId
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceAsyncUseApicId);

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_ASYNC
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID_EXT_0B
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceAsyncUseApicIdExt0B
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceAsyncUseApicIdExt0B);

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_ASYNC
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_APIC_ID_EXT_8000001E
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceAsyncUseApicIdExt8000001E
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceAsyncUseApicIdExt8000001E);

# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceAsyncUseRdtscp
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceAsyncUseRdtscp);

# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceAsyncUseIdtrLim
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceAsyncUseIdtrLim);

# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     SUPGIPGETCPU_RDTSCP_GROUP_IN_CH_NUMBER_IN_CL
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceAsyncUseRdtscpGroupChNumCl
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceAsyncUseRdtscpGroupChNumCl);

#else  /* IN_RC || IN_RING0: Disable interrupts and call getter function. */

# undef  TMPL_MODE
# define TMPL_MODE               TMPL_MODE_SYNC_INVAR_WITH_DELTA
# undef  TMPL_GET_CPU_METHOD
# define TMPL_GET_CPU_METHOD     UINT32_MAX
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceSyncInvarWithDelta
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceSyncInvarWithDelta);

# undef  TMPL_MODE
# define TMPL_MODE TMPL_MODE_ASYNC
# undef  rtTimeNanoTSInternalRef
# define rtTimeNanoTSInternalRef RTTimeNanoTSLFenceAsync
# include "timesupref.h"
RT_EXPORT_SYMBOL(RTTimeNanoTSLFenceAsync);

#endif


#endif /* !IN_GUEST && !RT_NO_GIP */

