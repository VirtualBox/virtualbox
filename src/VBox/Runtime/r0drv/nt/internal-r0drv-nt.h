/* $Id: internal-r0drv-nt.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal Header for the NT Ring-0 Driver Code.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_SRC_r0drv_nt_internal_r0drv_nt_h
#define IPRT_INCLUDED_SRC_r0drv_nt_internal_r0drv_nt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cpuset.h>
#include <iprt/nt/nt.h>

RT_C_DECLS_BEGIN

/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef ULONG (__stdcall *PFNMYEXSETTIMERRESOLUTION)(ULONG, BOOLEAN);
typedef VOID (__stdcall *PFNMYKEFLUSHQUEUEDDPCS)(VOID);
typedef VOID (__stdcall *PFNHALSENDSOFTWAREINTERRUPT)(ULONG ProcessorNumber, KIRQL Irql);
typedef int (__stdcall *PFNRTSENDIPI)(RTCPUID idCpu);
typedef ULONG_PTR (__stdcall *PFNRTKEIPIGENERICCALL)(PKIPI_BROADCAST_WORKER BroadcastFunction, ULONG_PTR  Context);
typedef ULONG (__stdcall *PFNRTRTLGETVERSION)(PRTL_OSVERSIONINFOEXW pVerInfo);
#ifndef RT_ARCH_AMD64
typedef ULONGLONG (__stdcall *PFNRTKEQUERYINTERRUPTTIME)(VOID);
typedef VOID (__stdcall *PFNRTKEQUERYSYSTEMTIME)(PLARGE_INTEGER pTime);
#endif
typedef ULONG64 (__stdcall *PFNRTKEQUERYINTERRUPTTIMEPRECISE)(PULONG64 pQpcTS);
typedef VOID (__stdcall *PFNRTKEQUERYSYSTEMTIMEPRECISE)(PLARGE_INTEGER pTime);
typedef PMDL (__stdcall *PFNMMALLOCATEPAGESFORMDLEX)(PHYSICAL_ADDRESS, PHYSICAL_ADDRESS, PHYSICAL_ADDRESS,
                                                     SIZE_T, MEMORY_CACHING_TYPE, ULONG);

#ifndef EX_TIMER_HIGH_RESOLUTION /* Too old DDK, so add missing bits. */
# define EX_TIMER_NO_WAKE               RT_BIT_32(3)
# define EX_TIMER_HIGH_RESOLUTION       RT_BIT_32(2)
# define EX_TIMER_NOTIFICATION          RT_BIT_32(31)
typedef struct _EX_TIMER *PEX_TIMER;
typedef VOID (__stdcall *PEXT_CALLBACK)(PEX_TIMER, void *);
typedef PEX_TIMER (__stdcall *PFNEXALLOCATETIMER)(PEXT_CALLBACK pfnCallback, void *pvUser, ULONG fFlags);

typedef VOID (__stdcall *PEXT_DELETE_CALLBACK)(void *);
typedef struct _EXT_DELETE_PARAMETERS
{
    ULONG                   Version;
    ULONG                   Reserved;
    PEXT_DELETE_CALLBACK    DeleteCallback;
    void                   *DeleteContext;
} EXT_DELETE_PARAMETERS;
typedef EXT_DELETE_PARAMETERS *PEXT_DELETE_PARAMETERS;
DECLINLINE(void) ExInitializeDeleteTimerParameters(PEXT_DELETE_PARAMETERS pParams)
{
    pParams->Version        = 0;
    pParams->Reserved       = 0;
    pParams->DeleteCallback = NULL;
    pParams->DeleteContext  = NULL;
}
typedef BOOLEAN   (__stdcall *PFNEXDELETETIMER)(PEX_TIMER pTimer, BOOLEAN fCancel, BOOLEAN fWait, PEXT_DELETE_PARAMETERS pParams);

typedef struct _EXT_SET_PARAMETERS_V0
{
    ULONG       Version;
    ULONG       Reserved;
    LONGLONG    NoWakeTolerance;
} EXT_SET_PARAMETERS;
typedef EXT_SET_PARAMETERS *PEXT_SET_PARAMETERS;
DECLINLINE(void) ExInitializeSetTimerParameters(PEXT_SET_PARAMETERS pParams)
{
    pParams->Version         = 0;
    pParams->Reserved        = 0;
    pParams->NoWakeTolerance = 0;
}
typedef BOOLEAN   (__stdcall *PFNEXSETTIMER)(PEX_TIMER pTimer, LONGLONG DueTime, LONGLONG Period, PEXT_SET_PARAMETERS pParams);

typedef BOOLEAN   (__stdcall *PFNEXCANCELTIMER)(PEX_TIMER pTimer, void *pvReserved);

#else
typedef decltype(ExAllocateTimer) *PFNEXALLOCATETIMER;
typedef decltype(ExDeleteTimer)   *PFNEXDELETETIMER;
typedef decltype(ExSetTimer)      *PFNEXSETTIMER;
typedef decltype(ExCancelTimer)   *PFNEXCANCELTIMER;
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
extern RTCPUSET                                g_rtMpNtCpuSet;
extern uint32_t                                g_cRtMpNtMaxGroups;
extern uint32_t                                g_cRtMpNtMaxCpus;
extern RTCPUID                                 g_aidRtMpNtByCpuSetIdx[RTCPUSET_MAX_CPUS];

extern decltype(ExAllocatePoolWithTag)        *g_pfnrtExAllocatePoolWithTag;
extern decltype(ExFreePoolWithTag)            *g_pfnrtExFreePoolWithTag;
extern PFNMYEXSETTIMERRESOLUTION               g_pfnrtNtExSetTimerResolution;
extern PFNEXALLOCATETIMER                      g_pfnrtExAllocateTimer;
extern PFNEXDELETETIMER                        g_pfnrtExDeleteTimer;
extern PFNEXSETTIMER                           g_pfnrtExSetTimer;
extern PFNEXCANCELTIMER                        g_pfnrtExCancelTimer;
extern PFNMYKEFLUSHQUEUEDDPCS                  g_pfnrtNtKeFlushQueuedDpcs;
extern PFNHALREQUESTIPI_W7PLUS                 g_pfnrtHalRequestIpiW7Plus;
extern PFNHALREQUESTIPI_PRE_W7                 g_pfnrtHalRequestIpiPreW7;
extern PFNHALSENDSOFTWAREINTERRUPT             g_pfnrtNtHalSendSoftwareInterrupt;
extern PFNRTSENDIPI                            g_pfnrtMpPokeCpuWorker;
extern PFNRTKEIPIGENERICCALL                   g_pfnrtKeIpiGenericCall;
extern PFNKESETTARGETPROCESSORDPCEX            g_pfnrtKeSetTargetProcessorDpcEx;
extern PFNKEINITIALIZEAFFINITYEX               g_pfnrtKeInitializeAffinityEx;
extern PFNKEADDPROCESSORAFFINITYEX             g_pfnrtKeAddProcessorAffinityEx;
extern PFNKEGETPROCESSORINDEXFROMNUMBER        g_pfnrtKeGetProcessorIndexFromNumber;
extern PFNKEGETPROCESSORNUMBERFROMINDEX        g_pfnrtKeGetProcessorNumberFromIndex;
extern PFNKEGETCURRENTPROCESSORNUMBEREX        g_pfnrtKeGetCurrentProcessorNumberEx;
extern PFNKEQUERYACTIVEPROCESSORS              g_pfnrtKeQueryActiveProcessors;
extern PFNKEQUERYMAXIMUMPROCESSORCOUNT         g_pfnrtKeQueryMaximumProcessorCount;
extern PFNKEQUERYMAXIMUMPROCESSORCOUNTEX       g_pfnrtKeQueryMaximumProcessorCountEx;
extern PFNKEQUERYMAXIMUMGROUPCOUNT             g_pfnrtKeQueryMaximumGroupCount;
extern PFNKEQUERYACTIVEPROCESSORCOUNT          g_pfnrtKeQueryActiveProcessorCount;
extern PFNKEQUERYACTIVEPROCESSORCOUNTEX        g_pfnrtKeQueryActiveProcessorCountEx;
extern PFNKEQUERYLOGICALPROCESSORRELATIONSHIP  g_pfnrtKeQueryLogicalProcessorRelationship;
extern PFNKEREGISTERPROCESSORCHANGECALLBACK    g_pfnrtKeRegisterProcessorChangeCallback;
extern PFNKEDEREGISTERPROCESSORCHANGECALLBACK  g_pfnrtKeDeregisterProcessorChangeCallback;
extern decltype(KeSetImportanceDpc)           *g_pfnrtKeSetImportanceDpc;
extern decltype(KeSetTargetProcessorDpc)      *g_pfnrtKeSetTargetProcessorDpc;
extern decltype(KeInitializeTimerEx)          *g_pfnrtKeInitializeTimerEx;
extern PFNKESHOULDYIELDPROCESSOR               g_pfnrtKeShouldYieldProcessor;
extern decltype(MmProtectMdlSystemAddress)    *g_pfnrtMmProtectMdlSystemAddress;
extern decltype(MmAllocatePagesForMdl)        *g_pfnrtMmAllocatePagesForMdl;
extern PFNMMALLOCATEPAGESFORMDLEX              g_pfnrtMmAllocatePagesForMdlEx;
extern decltype(MmFreePagesFromMdl)           *g_pfnrtMmFreePagesFromMdl;
extern decltype(MmMapLockedPagesSpecifyCache) *g_pfnrtMmMapLockedPagesSpecifyCache;
extern decltype(MmAllocateContiguousMemorySpecifyCache) *g_pfnrtMmAllocateContiguousMemorySpecifyCache;
extern decltype(MmSecureVirtualMemory)        *g_pfnrtMmSecureVirtualMemory;
extern decltype(MmUnsecureVirtualMemory)      *g_pfnrtMmUnsecureVirtualMemory;
extern decltype(PsIsThreadTerminating)        *g_pfnrtPsIsThreadTerminating;

extern PFNRTRTLGETVERSION                      g_pfnrtRtlGetVersion;
#ifdef RT_ARCH_X86
extern PFNRTKEQUERYINTERRUPTTIME               g_pfnrtKeQueryInterruptTime;
#endif
extern PFNRTKEQUERYINTERRUPTTIMEPRECISE        g_pfnrtKeQueryInterruptTimePrecise;
extern PFNRTKEQUERYSYSTEMTIMEPRECISE           g_pfnrtKeQuerySystemTimePrecise;

extern uint32_t                                g_offrtNtPbQuantumEnd;
extern uint32_t                                g_cbrtNtPbQuantumEnd;
extern uint32_t                                g_offrtNtPbDpcQueueDepth;

/** Makes an NT version for checking against g_uRtNtVersion. */
#define RTNT_MAKE_VERSION(uMajor, uMinor)       RT_MAKE_U32(uMinor, uMajor)

extern uint32_t                                g_uRtNtVersion;
extern uint8_t                                 g_uRtNtMajorVer;
extern uint8_t                                 g_uRtNtMinorVer;
extern uint32_t                                g_uRtNtBuildNo;

extern uintptr_t const                        *g_puRtMmHighestUserAddress;
extern uintptr_t const                        *g_puRtMmSystemRangeStart;


int __stdcall rtMpPokeCpuUsingFailureNotSupported(RTCPUID idCpu);
int __stdcall rtMpPokeCpuUsingDpc(RTCPUID idCpu);
int __stdcall rtMpPokeCpuUsingBroadcastIpi(RTCPUID idCpu);
int __stdcall rtMpPokeCpuUsingHalRequestIpiW7Plus(RTCPUID idCpu);
int __stdcall rtMpPokeCpuUsingHalRequestIpiPreW7(RTCPUID idCpu);

struct RTNTSDBOSVER;
DECLHIDDEN(int)  rtR0MpNtInit(struct RTNTSDBOSVER const *pOsVerInfo);
DECLHIDDEN(void) rtR0MpNtTerm(void);
DECLHIDDEN(int)  rtMpNtSetTargetProcessorDpc(KDPC *pDpc, RTCPUID idCpu);
#if defined(RT_ARCH_X86) && defined(NIL_RTDBGKRNLINFO)
DECLHIDDEN(int)  rtR0Nt3InitSymbols(RTDBGKRNLINFO hKrnlInfo);
#endif

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_SRC_r0drv_nt_internal_r0drv_nt_h */

