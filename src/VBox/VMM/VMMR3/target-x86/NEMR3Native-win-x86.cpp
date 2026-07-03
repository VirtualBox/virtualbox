/* $Id: NEMR3Native-win-x86.cpp 114617 2026-07-03 17:54:57Z alexander.eichner@oracle.com $ */
/** @file
 * NEM - Native execution manager, native ring-3 Windows backend.
 *
 * Log group 2: Exit logging.
 * Log group 3: Log context on exit.
 * Log group 5: Ring-3 memory management
 * Log group 6: Ring-0 memory management
 * Log group 12: API intercepts.
 */

/*
 * Copyright (C) 2018-2025 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_NEM
#define VMCPU_INCL_CPUM_GST_CTX
#include <iprt/nt/nt-and-windows.h>
#include <iprt/nt/hyperv.h>
#include <iprt/nt/vid.h>
#include <WinHvPlatform.h>

#ifndef _WIN32_WINNT_WIN10
# error "Missing _WIN32_WINNT_WIN10"
#endif
#ifndef _WIN32_WINNT_WIN10_RS1 /* Missing define, causing trouble for us. */
# define _WIN32_WINNT_WIN10_RS1  (_WIN32_WINNT_WIN10 + 1)
#endif
#include <sysinfoapi.h>
#include <debugapi.h>
#include <errhandlingapi.h>
#include <fileapi.h>
#include <winerror.h> /* no api header for this. */

#include <VBox/vmm/nem.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/pdmapic.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/dbgftrace.h>
#include "NEMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <iprt/ldr.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/utf16.h>

#ifndef NTDDI_WIN10_VB /* Present in W10 2004 SDK, quite possibly earlier. */
HRESULT WINAPI WHvQueryGpaRangeDirtyBitmap(WHV_PARTITION_HANDLE, WHV_GUEST_PHYSICAL_ADDRESS, UINT64, UINT64 *, UINT32);
# define WHvMapGpaRangeFlagTrackDirtyPages      ((WHV_MAP_GPA_RANGE_FLAGS)0x00000008)
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef LOG_ENABLED
# define NEM_WIN_INTERCEPT_NT_IO_CTLS
#endif

/** VID I/O control detection: Fake partition handle input. */
#define NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE                      ((HANDLE)(uintptr_t)38479125)
/** VID I/O control detection: Fake partition ID return. */
#define NEM_WIN_IOCTL_DETECTOR_FAKE_PARTITION_ID                UINT64_C(0xfa1e000042424242)
/** VID I/O control detection: The property we get via VidGetPartitionProperty. */
#define NEM_WIN_IOCTL_DETECTOR_FAKE_PARTITION_PROPERTY_CODE     HvPartitionPropertyProcessorVendor
/** VID I/O control detection: Fake property value return. */
#define NEM_WIN_IOCTL_DETECTOR_FAKE_PARTITION_PROPERTY_VALUE    UINT64_C(0xf00dface01020304)
/** VID I/O control detection: Fake CPU index input. */
#define NEM_WIN_IOCTL_DETECTOR_FAKE_VP_INDEX                    UINT32_C(42)
/** VID I/O control detection: Fake timeout input. */
#define NEM_WIN_IOCTL_DETECTOR_FAKE_TIMEOUT                     UINT32_C(0x00080286)


#ifndef NTDDI_WIN10_RS4             /* We use this to support older SDKs. */
# define NTDDI_WIN10_RS4            (NTDDI_WIN10 + 5)   /* 17134 */
#endif
#ifndef NTDDI_WIN10_RS5             /* We use this to support older SDKs. */
# define NTDDI_WIN10_RS5            (NTDDI_WIN10 + 6)   /* ????? */
#endif
#ifndef NTDDI_WIN10_19H1            /* We use this to support older SDKs. */
# define NTDDI_WIN10_19H1           (NTDDI_WIN10 + 7)   /* 18362 */
#endif
#ifndef NTDDI_WIN10_VB
# define NTDDI_WIN10_VB             (NTDDI_WIN10 + 8)   /* 19040 */
#endif
#ifndef NTDDI_WIN10_MN
# define NTDDI_WIN10_MN             (NTDDI_WIN10 + 9)   /* ????? */
#endif
#ifndef NTDDI_WIN10_FE
# define NTDDI_WIN10_FE             (NTDDI_WIN10 + 10)  /* ????? */
#endif
#ifndef NTDDI_WIN10_CO
# define NTDDI_WIN10_CO             (NTDDI_WIN10 + 11)  /* 22000 */
#endif
#ifndef NTDDI_WIN10_NI
# define NTDDI_WIN10_NI             (NTDDI_WIN10 + 12)  /* 22621 */
#endif
#ifndef NTDDI_WIN10_CU
# define NTDDI_WIN10_CU             (NTDDI_WIN10 + 13)  /* ????? */
#endif
#ifndef NTDDI_WIN10_ZN
# define NTDDI_WIN10_ZN             (NTDDI_WIN10 + 14)  /* ????? */
#endif
#ifndef NTDDI_WIN10_GA
# define NTDDI_WIN10_GA             (NTDDI_WIN10 + 15)  /* ????? */
#endif
#ifndef NTDDI_WIN10_GE
# define NTDDI_WIN10_GE             (NTDDI_WIN10 + 16)  /* 26100 */
#endif

#define MY_NTDDI_WIN10_17134    NTDDI_WIN10_RS4
#define MY_NTDDI_WIN10_18362    NTDDI_WIN10_19H1
#define MY_NTDDI_WIN10_19040    NTDDI_WIN10_VB
#define MY_NTDDI_WIN11_22000    NTDDI_WIN10_CO
#define MY_NTDDI_WIN11_22621    NTDDI_WIN10_NI
#define MY_NTDDI_WIN11_26100    NTDDI_WIN10_GE


/** Copy back a segment from hyper-V. */
#define NEM_WIN_COPY_BACK_SEG(a_Dst, a_Src) \
            do { \
                (a_Dst).u64Base  = (a_Src).Base; \
                (a_Dst).u32Limit = (a_Src).Limit; \
                (a_Dst).ValidSel = (a_Dst).Sel = (a_Src).Selector; \
                (a_Dst).Attr.u   = (a_Src).Attributes; \
                (a_Dst).fFlags   = CPUMSELREG_FLAGS_VALID; \
            } while (0)

/** @def NEMWIN_ASSERT_MSG_REG_VAL
 * Asserts the correctness of a register value in a message/context.
 */
#if 0
# define NEMWIN_NEED_GET_REGISTER
# define NEMWIN_ASSERT_MSG_REG_VAL(a_pVCpu, a_enmReg, a_Expr, a_Msg) \
    do { \
        WHV_REGISTER_VALUE TmpVal; \
        nemR3WinGetRegister(a_pVCpu, a_enmReg, &TmpVal); \
        AssertMsg(a_Expr, a_Msg); \
    } while (0)
#else
# define NEMWIN_ASSERT_MSG_REG_VAL(a_pVCpu, a_enmReg, a_Expr, a_Msg) do { } while (0)
#endif

/** @def NEMWIN_ASSERT_MSG_REG_VAL
 * Asserts the correctness of a 64-bit register value in a message/context.
 */
#define NEMWIN_ASSERT_MSG_REG_VAL64(a_pVCpu, a_enmReg, a_u64Val) \
    NEMWIN_ASSERT_MSG_REG_VAL(a_pVCpu, a_enmReg, (a_u64Val) == TmpVal.Reg64, \
                              (#a_u64Val "=%#RX64, expected %#RX64\n", (a_u64Val), TmpVal.Reg64))
/** @def NEMWIN_ASSERT_MSG_REG_VAL
 * Asserts the correctness of a segment register value in a message/context.
 */
#define NEMWIN_ASSERT_MSG_REG_SEG(a_pVCpu, a_enmReg, a_SReg) \
    NEMWIN_ASSERT_MSG_REG_VAL(a_pVCpu, a_enmReg, \
                                 (a_SReg).Base       == TmpVal.Segment.Base \
                              && (a_SReg).Limit      == TmpVal.Segment.Limit \
                              && (a_SReg).Selector   == TmpVal.Segment.Selector \
                              && (a_SReg).Attributes == TmpVal.Segment.Attributes, \
                              ( #a_SReg "=%#RX16 {%#RX64 LB %#RX32,%#RX16} expected %#RX16 {%#RX64 LB %#RX32,%#RX16}\n", \
                               (a_SReg).Selector, (a_SReg).Base, (a_SReg).Limit, (a_SReg).Attributes, \
                               TmpVal.Segment.Selector, TmpVal.Segment.Base, TmpVal.Segment.Limit, TmpVal.Segment.Attributes))


/** WHvRegisterPendingEvent0 was renamed to WHvRegisterPendingEvent between
 *  SDK 17134 and 18362. */
#if WDK_NTDDI_VERSION < NTDDI_WIN10_19H1
# define WHvRegisterPendingEvent    WHvRegisterPendingEvent0
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** @name APIs imported from WinHvPlatform.dll
 * @{ */
static decltype(WHvGetCapability) *                 g_pfnWHvGetCapability;
static decltype(WHvCreatePartition) *               g_pfnWHvCreatePartition;
static decltype(WHvSetupPartition) *                g_pfnWHvSetupPartition;
static decltype(WHvDeletePartition) *               g_pfnWHvDeletePartition;
static decltype(WHvGetPartitionProperty) *          g_pfnWHvGetPartitionProperty;
static decltype(WHvSetPartitionProperty) *          g_pfnWHvSetPartitionProperty;
static decltype(WHvMapGpaRange) *                   g_pfnWHvMapGpaRange;
static decltype(WHvUnmapGpaRange) *                 g_pfnWHvUnmapGpaRange;
static decltype(WHvTranslateGva) *                  g_pfnWHvTranslateGva;
static decltype(WHvQueryGpaRangeDirtyBitmap) *      g_pfnWHvQueryGpaRangeDirtyBitmap;
static decltype(WHvCreateVirtualProcessor) *        g_pfnWHvCreateVirtualProcessor;
static decltype(WHvDeleteVirtualProcessor) *        g_pfnWHvDeleteVirtualProcessor;
static decltype(WHvRunVirtualProcessor) *           g_pfnWHvRunVirtualProcessor;
static decltype(WHvCancelRunVirtualProcessor) *     g_pfnWHvCancelRunVirtualProcessor;
decltype(WHvGetVirtualProcessorRegisters) *  g_pfnWHvGetVirtualProcessorRegisters;
decltype(WHvSetVirtualProcessorRegisters) *  g_pfnWHvSetVirtualProcessorRegisters;
static decltype(WHvResumePartitionTime)            *g_pfnWHvResumePartitionTime;
static decltype(WHvSuspendPartitionTime)           *g_pfnWHvSuspendPartitionTime;
decltype(WHvGetVirtualProcessorXsaveState) *        g_pfnWHvGetVirtualProcessorXsaveState = NULL;
decltype(WHvSetVirtualProcessorXsaveState) *        g_pfnWHvSetVirtualProcessorXsaveState = NULL;
decltype(WHvGetVirtualProcessorState) *             g_pfnWHvGetVirtualProcessorState = NULL;
decltype(WHvSetVirtualProcessorState) *             g_pfnWHvSetVirtualProcessorState = NULL;
decltype(WHvGetVirtualProcessorInterruptControllerState)  *g_pfnWHvGetVirtualProcessorInterruptControllerState = NULL;
decltype(WHvSetVirtualProcessorInterruptControllerState)  *g_pfnWHvSetVirtualProcessorInterruptControllerState = NULL;
decltype(WHvGetVirtualProcessorInterruptControllerState2) *g_pfnWHvGetVirtualProcessorInterruptControllerState2 = NULL;
decltype(WHvSetVirtualProcessorInterruptControllerState2) *g_pfnWHvSetVirtualProcessorInterruptControllerState2 = NULL;
decltype(WHvRequestInterrupt) *                     g_pfnWHvRequestInterrupt;
/** @} */

/** @name APIs imported from Vid.dll
 * @{ */
static decltype(VidGetHvPartitionId)               *g_pfnVidGetHvPartitionId;
static decltype(VidGetPartitionProperty)           *g_pfnVidGetPartitionProperty;
#ifdef LOG_ENABLED
static decltype(VidStartVirtualProcessor)          *g_pfnVidStartVirtualProcessor;
static decltype(VidStopVirtualProcessor)           *g_pfnVidStopVirtualProcessor;
static decltype(VidMessageSlotMap)                 *g_pfnVidMessageSlotMap;
static decltype(VidMessageSlotHandleAndGetNext)    *g_pfnVidMessageSlotHandleAndGetNext;
static decltype(VidGetVirtualProcessorState)       *g_pfnVidGetVirtualProcessorState;
static decltype(VidSetVirtualProcessorState)       *g_pfnVidSetVirtualProcessorState;
static decltype(VidGetVirtualProcessorRunningStatus) *g_pfnVidGetVirtualProcessorRunningStatus;
#endif
/** @} */

/** The Windows build number. */
static uint32_t g_uBuildNo = 17134;

/** NEM_WIN_PAGE_STATE_XXX names. */
static const char * const g_apszPageStates[4] = { "not-set", "unmapped", "readable", "writable" };

#ifdef LOG_ENABLED
/** HV_INTERCEPT_ACCESS_TYPE names. */
static const char * const g_apszHvInterceptAccessTypes[4] = { "read", "write", "exec", "!undefined!" };
#endif



/**
 * Import instructions.
 */
static const struct
{
    uint8_t     idxDll;     /**< 0 for WinHvPlatform.dll, 1 for vid.dll. */
    bool        fOptional;  /**< Set if import is optional. */
    PFNRT      *ppfn;       /**< The function pointer variable. */
    const char *pszName;    /**< The function name. */
} g_aImports[] =
{
#define NEM_WIN_IMPORT(a_idxDll, a_fOptional, a_Name) { (a_idxDll), (a_fOptional), (PFNRT *)&RT_CONCAT(g_pfn,a_Name), #a_Name }
    NEM_WIN_IMPORT(0, false, WHvGetCapability),
    NEM_WIN_IMPORT(0, false, WHvCreatePartition),
    NEM_WIN_IMPORT(0, false, WHvSetupPartition),
    NEM_WIN_IMPORT(0, false, WHvDeletePartition),
    NEM_WIN_IMPORT(0, false, WHvGetPartitionProperty),
    NEM_WIN_IMPORT(0, false, WHvSetPartitionProperty),
    NEM_WIN_IMPORT(0, false, WHvMapGpaRange),
    NEM_WIN_IMPORT(0, false, WHvUnmapGpaRange),
    NEM_WIN_IMPORT(0, false, WHvTranslateGva),
    NEM_WIN_IMPORT(0, true,  WHvQueryGpaRangeDirtyBitmap),
    NEM_WIN_IMPORT(0, false, WHvCreateVirtualProcessor),
    NEM_WIN_IMPORT(0, false, WHvDeleteVirtualProcessor),
    NEM_WIN_IMPORT(0, false, WHvRunVirtualProcessor),
    NEM_WIN_IMPORT(0, false, WHvCancelRunVirtualProcessor),
    NEM_WIN_IMPORT(0, false, WHvGetVirtualProcessorRegisters),
    NEM_WIN_IMPORT(0, false, WHvSetVirtualProcessorRegisters),
    NEM_WIN_IMPORT(0, true,  WHvResumePartitionTime),  /* since 19H1 */
    NEM_WIN_IMPORT(0, true,  WHvSuspendPartitionTime), /* since 19H1 */
    NEM_WIN_IMPORT(0, true,  WHvRequestInterrupt),
    NEM_WIN_IMPORT(0, true,  WHvGetVirtualProcessorXsaveState),
    NEM_WIN_IMPORT(0, true,  WHvSetVirtualProcessorXsaveState),
    NEM_WIN_IMPORT(0, true,  WHvGetVirtualProcessorState),
    NEM_WIN_IMPORT(0, true,  WHvSetVirtualProcessorState),
    NEM_WIN_IMPORT(0, true,  WHvGetVirtualProcessorInterruptControllerState),
    NEM_WIN_IMPORT(0, true,  WHvSetVirtualProcessorInterruptControllerState),
    NEM_WIN_IMPORT(0, true,  WHvGetVirtualProcessorInterruptControllerState2),
    NEM_WIN_IMPORT(0, true,  WHvSetVirtualProcessorInterruptControllerState2),

    NEM_WIN_IMPORT(1, true,  VidGetHvPartitionId),
    NEM_WIN_IMPORT(1, true,  VidGetPartitionProperty),
#ifdef LOG_ENABLED
    NEM_WIN_IMPORT(1, false, VidMessageSlotMap),
    NEM_WIN_IMPORT(1, false, VidMessageSlotHandleAndGetNext),
    NEM_WIN_IMPORT(1, false, VidStartVirtualProcessor),
    NEM_WIN_IMPORT(1, false, VidStopVirtualProcessor),
    NEM_WIN_IMPORT(1, false, VidGetVirtualProcessorState),
    NEM_WIN_IMPORT(1, false, VidSetVirtualProcessorState),
    NEM_WIN_IMPORT(1, false, VidGetVirtualProcessorRunningStatus),
#endif
#undef NEM_WIN_IMPORT
};


/** The real NtDeviceIoControlFile API in NTDLL.   */
static decltype(NtDeviceIoControlFile) *g_pfnNtDeviceIoControlFile;
/** Pointer to the NtDeviceIoControlFile import table entry. */
static decltype(NtDeviceIoControlFile) **g_ppfnVidNtDeviceIoControlFile;
#ifdef LOG_ENABLED
/** Info about the VidGetHvPartitionId I/O control interface. */
static NEMWINIOCTL g_IoCtlGetHvPartitionId;
/** Info about the VidGetPartitionProperty I/O control interface. */
static NEMWINIOCTL g_IoCtlGetPartitionProperty;
/** Info about the VidStartVirtualProcessor I/O control interface. */
static NEMWINIOCTL g_IoCtlStartVirtualProcessor;
/** Info about the VidStopVirtualProcessor I/O control interface. */
static NEMWINIOCTL g_IoCtlStopVirtualProcessor;
/** Info about the VidMessageSlotHandleAndGetNext I/O control interface. */
static NEMWINIOCTL g_IoCtlMessageSlotHandleAndGetNext;
/** Info about the VidMessageSlotMap I/O control interface - for logging. */
static NEMWINIOCTL g_IoCtlMessageSlotMap;
/** Info about the VidGetVirtualProcessorState I/O control interface - for logging. */
static NEMWINIOCTL g_IoCtlGetVirtualProcessorState;
/** Info about the VidSetVirtualProcessorState I/O control interface - for logging. */
static NEMWINIOCTL g_IoCtlSetVirtualProcessorState;
/** Pointer to what nemR3WinIoctlDetector_ForLogging should fill in. */
static NEMWINIOCTL *g_pIoCtlDetectForLogging;
#endif

#ifdef NEM_WIN_INTERCEPT_NT_IO_CTLS
/** Mapping slot for CPU #0.
 * @{  */
static VID_MESSAGE_MAPPING_HEADER            *g_pMsgSlotMapping = NULL;
static const HV_MESSAGE_HEADER               *g_pHvMsgHdr;
static const HV_X64_INTERCEPT_MESSAGE_HEADER *g_pX64MsgHdr;
/** @} */
#endif


/*
 * Let the preprocessor alias the APIs to import variables for better autocompletion.
 */
#ifndef IN_SLICKEDIT
# define WHvGetCapability                           g_pfnWHvGetCapability
# define WHvCreatePartition                         g_pfnWHvCreatePartition
# define WHvSetupPartition                          g_pfnWHvSetupPartition
# define WHvDeletePartition                         g_pfnWHvDeletePartition
# define WHvGetPartitionProperty                    g_pfnWHvGetPartitionProperty
# define WHvSetPartitionProperty                    g_pfnWHvSetPartitionProperty
# define WHvMapGpaRange                             g_pfnWHvMapGpaRange
# define WHvUnmapGpaRange                           g_pfnWHvUnmapGpaRange
# define WHvTranslateGva                            g_pfnWHvTranslateGva
# define WHvQueryGpaRangeDirtyBitmap                g_pfnWHvQueryGpaRangeDirtyBitmap
# define WHvCreateVirtualProcessor                  g_pfnWHvCreateVirtualProcessor
# define WHvDeleteVirtualProcessor                  g_pfnWHvDeleteVirtualProcessor
# define WHvRunVirtualProcessor                     g_pfnWHvRunVirtualProcessor
# define WHvGetRunExitContextSize                   g_pfnWHvGetRunExitContextSize
# define WHvCancelRunVirtualProcessor               g_pfnWHvCancelRunVirtualProcessor
# define WHvGetVirtualProcessorRegisters            g_pfnWHvGetVirtualProcessorRegisters
# define WHvSetVirtualProcessorRegisters            g_pfnWHvSetVirtualProcessorRegisters
# define WHvResumePartitionTime                     g_pfnWHvResumePartitionTime
# define WHvSuspendPartitionTime                    g_pfnWHvSuspendPartitionTime
# define WHvRequestInterrupt                        g_pfnWHvRequestInterrupt
# define WHvGetVirtualProcessorXsaveState           g_pfnWHvGetVirtualProcessorXsaveState
# define WHvSetVirtualProcessorXsaveState           g_pfnWHvSetVirtualProcessorXsaveState
# define WHvGetVirtualProcessorState                g_pfnWHvGetVirtualProcessorState
# define WHvSetVirtualProcessorState                g_pfnWHvSetVirtualProcessorState
# define WHvGetVirtualProcessorInterruptControllerState     g_pfnWHvGetVirtualProcessorInterruptControllerState
# define WHvGetVirtualProcessorInterruptControllerState2    g_pfnWHvGetVirtualProcessorInterruptControllerState2

# define VidMessageSlotHandleAndGetNext             g_pfnVidMessageSlotHandleAndGetNext
# define VidStartVirtualProcessor                   g_pfnVidStartVirtualProcessor
# define VidStopVirtualProcessor                    g_pfnVidStopVirtualProcessor

#endif

#if 0 /* unused */
/** WHV_MEMORY_ACCESS_TYPE names */
static const char * const g_apszWHvMemAccesstypes[4] = { "read", "write", "exec", "!undefined!" };
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLINLINE(int) nemR3NativeGCPhys2R3PtrReadOnly(PVM pVM, RTGCPHYS GCPhys, const void **ppv);
DECLINLINE(int) nemR3NativeGCPhys2R3PtrWriteable(PVM pVM, RTGCPHYS GCPhys, void **ppv);
static int      nemHCNativeSetPhysPage(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhysSrc, RTGCPHYS GCPhysDst,
                                       uint32_t fPageProt, uint8_t *pu2State, bool fBackingChanged);


#ifdef NEM_WIN_INTERCEPT_NT_IO_CTLS
/**
 * Wrapper that logs the call from VID.DLL.
 *
 * This is very handy for figuring out why an API call fails.
 */
static NTSTATUS WINAPI
nemR3WinLogWrapper_NtDeviceIoControlFile(HANDLE hFile, HANDLE hEvt, PIO_APC_ROUTINE pfnApcCallback, PVOID pvApcCtx,
                                         PIO_STATUS_BLOCK pIos, ULONG uFunction, PVOID pvInput, ULONG cbInput,
                                         PVOID pvOutput, ULONG cbOutput)
{

    char szFunction[32];
    const char *pszFunction;
    if (uFunction == g_IoCtlMessageSlotHandleAndGetNext.uFunction)
        pszFunction = "VidMessageSlotHandleAndGetNext";
    else if (uFunction == g_IoCtlStartVirtualProcessor.uFunction)
        pszFunction = "VidStartVirtualProcessor";
    else if (uFunction == g_IoCtlStopVirtualProcessor.uFunction)
        pszFunction = "VidStopVirtualProcessor";
    else if (uFunction == g_IoCtlMessageSlotMap.uFunction)
        pszFunction = "VidMessageSlotMap";
    else if (uFunction == g_IoCtlGetVirtualProcessorState.uFunction)
        pszFunction = "VidGetVirtualProcessorState";
    else if (uFunction == g_IoCtlSetVirtualProcessorState.uFunction)
        pszFunction = "VidSetVirtualProcessorState";
    else
    {
        RTStrPrintf(szFunction, sizeof(szFunction), "%#x", uFunction);
        pszFunction = szFunction;
    }

    if (cbInput > 0 && pvInput)
        Log12(("VID!NtDeviceIoControlFile: %s/input: %.*Rhxs\n", pszFunction, RT_MIN(cbInput, 32), pvInput));
    NTSTATUS rcNt = g_pfnNtDeviceIoControlFile(hFile, hEvt, pfnApcCallback, pvApcCtx, pIos, uFunction,
                                               pvInput, cbInput, pvOutput, cbOutput);
    if (!hEvt && !pfnApcCallback && !pvApcCtx)
        Log12(("VID!NtDeviceIoControlFile: hFile=%#zx pIos=%p->{s:%#x, i:%#zx} uFunction=%s Input=%p LB %#x Output=%p LB %#x) -> %#x; Caller=%p\n",
               hFile, pIos, pIos->Status, pIos->Information, pszFunction, pvInput, cbInput, pvOutput, cbOutput, rcNt, ASMReturnAddress()));
    else
        Log12(("VID!NtDeviceIoControlFile: hFile=%#zx hEvt=%#zx Apc=%p/%p pIos=%p->{s:%#x, i:%#zx} uFunction=%s Input=%p LB %#x Output=%p LB %#x) -> %#x; Caller=%p\n",
               hFile, hEvt, RT_CB_LOG_CAST(pfnApcCallback), pvApcCtx, pIos, pIos->Status, pIos->Information, pszFunction,
               pvInput, cbInput, pvOutput, cbOutput, rcNt, ASMReturnAddress()));
    if (cbOutput > 0 && pvOutput)
    {
        Log12(("VID!NtDeviceIoControlFile: %s/output: %.*Rhxs\n", pszFunction, RT_MIN(cbOutput, 32), pvOutput));
        if (uFunction == 0x2210cc && g_pMsgSlotMapping == NULL && cbOutput >= sizeof(void *))
        {
            g_pMsgSlotMapping = *(VID_MESSAGE_MAPPING_HEADER **)pvOutput;
            g_pHvMsgHdr       = (const HV_MESSAGE_HEADER               *)(g_pMsgSlotMapping + 1);
            g_pX64MsgHdr      = (const HV_X64_INTERCEPT_MESSAGE_HEADER *)(g_pHvMsgHdr + 1);
            Log12(("VID!NtDeviceIoControlFile: Message slot mapping: %p\n", g_pMsgSlotMapping));
        }
    }
    if (   g_pMsgSlotMapping
        && (   uFunction == g_IoCtlMessageSlotHandleAndGetNext.uFunction
            || uFunction == g_IoCtlStopVirtualProcessor.uFunction
            || uFunction == g_IoCtlMessageSlotMap.uFunction
               ))
        Log12(("VID!NtDeviceIoControlFile: enmVidMsgType=%#x cb=%#x msg=%#x payload=%u cs:rip=%04x:%08RX64 (%s)\n",
               g_pMsgSlotMapping->enmVidMsgType, g_pMsgSlotMapping->cbMessage,
               g_pHvMsgHdr->MessageType, g_pHvMsgHdr->PayloadSize,
               g_pX64MsgHdr->CsSegment.Selector, g_pX64MsgHdr->Rip, pszFunction));

    return rcNt;
}
#endif /* NEM_WIN_INTERCEPT_NT_IO_CTLS */


/**
 * Patches the call table of VID.DLL so we can intercept NtDeviceIoControlFile.
 *
 * This is for used to figure out the I/O control codes and in logging builds
 * for logging API calls that WinHvPlatform.dll does.
 *
 * @returns VBox status code.
 * @param   hLdrModVid      The VID module handle.
 * @param   pErrInfo        Where to return additional error information.
 */
static int nemR3WinInitVidIntercepts(RTLDRMOD hLdrModVid, PRTERRINFO pErrInfo)
{
    /*
     * Locate the real API.
     */
    g_pfnNtDeviceIoControlFile = (decltype(NtDeviceIoControlFile) *)RTLdrGetSystemSymbol("NTDLL.DLL", "NtDeviceIoControlFile");
    AssertReturn(g_pfnNtDeviceIoControlFile != NULL,
                 RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "Failed to resolve NtDeviceIoControlFile from NTDLL.DLL"));

    /*
     * Locate the PE header and get what we need from it.
     */
    uint8_t const *pbImage = (uint8_t const *)RTLdrGetNativeHandle(hLdrModVid);
    IMAGE_DOS_HEADER const *pMzHdr  = (IMAGE_DOS_HEADER const *)pbImage;
    AssertReturn(pMzHdr->e_magic == IMAGE_DOS_SIGNATURE,
                 RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "VID.DLL mapping doesn't start with MZ signature: %#x", pMzHdr->e_magic));
    IMAGE_NT_HEADERS const *pNtHdrs = (IMAGE_NT_HEADERS const *)&pbImage[pMzHdr->e_lfanew];
    AssertReturn(pNtHdrs->Signature == IMAGE_NT_SIGNATURE,
                 RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "VID.DLL has invalid PE signaturre: %#x @%#x",
                               pNtHdrs->Signature, pMzHdr->e_lfanew));

    uint32_t const             cbImage   = pNtHdrs->OptionalHeader.SizeOfImage;
    IMAGE_DATA_DIRECTORY const ImportDir = pNtHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    /*
     * Walk the import descriptor table looking for NTDLL.DLL.
     */
    AssertReturn(   ImportDir.Size > 0
                 && ImportDir.Size < cbImage,
                 RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "VID.DLL bad import directory size: %#x", ImportDir.Size));
    AssertReturn(   ImportDir.VirtualAddress > 0
                 && ImportDir.VirtualAddress <= cbImage - ImportDir.Size,
                 RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "VID.DLL bad import directory RVA: %#x", ImportDir.VirtualAddress));

    for (PIMAGE_IMPORT_DESCRIPTOR pImps = (PIMAGE_IMPORT_DESCRIPTOR)&pbImage[ImportDir.VirtualAddress];
         pImps->Name != 0 && pImps->FirstThunk != 0;
         pImps++)
    {
        AssertReturn(pImps->Name < cbImage,
                     RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "VID.DLL bad import directory entry name: %#x", pImps->Name));
        const char *pszModName = (const char *)&pbImage[pImps->Name];
        if (RTStrICmpAscii(pszModName, "ntdll.dll"))
            continue;
        AssertReturn(pImps->FirstThunk < cbImage,
                     RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "VID.DLL bad FirstThunk: %#x", pImps->FirstThunk));
        AssertReturn(pImps->OriginalFirstThunk < cbImage,
                     RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "VID.DLL bad FirstThunk: %#x", pImps->FirstThunk));

        /*
         * Walk the thunks table(s) looking for NtDeviceIoControlFile.
         */
        uintptr_t *puFirstThunk = (uintptr_t *)&pbImage[pImps->FirstThunk]; /* update this. */
        if (   pImps->OriginalFirstThunk != 0
            && pImps->OriginalFirstThunk != pImps->FirstThunk)
        {
            uintptr_t const *puOrgThunk = (uintptr_t const *)&pbImage[pImps->OriginalFirstThunk]; /* read from this. */
            uintptr_t        cLeft      = (cbImage - (RT_MAX(pImps->FirstThunk, pImps->OriginalFirstThunk)))
                                        / sizeof(*puFirstThunk);
            while (cLeft-- > 0 && *puOrgThunk != 0)
            {
                if (!(*puOrgThunk & IMAGE_ORDINAL_FLAG64)) /* ASSUMES 64-bit */
                {
                    AssertReturn(*puOrgThunk > 0 && *puOrgThunk < cbImage,
                                 RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "VID.DLL bad thunk entry: %#x", *puOrgThunk));

                    const char *pszSymbol = (const char *)&pbImage[*puOrgThunk + 2];
                    if (strcmp(pszSymbol, "NtDeviceIoControlFile") == 0)
                        g_ppfnVidNtDeviceIoControlFile = (decltype(NtDeviceIoControlFile) **)puFirstThunk;
                }

                puOrgThunk++;
                puFirstThunk++;
            }
        }
        else
        {
            /* No original thunk table, so scan the resolved symbols for a match
               with the NtDeviceIoControlFile address. */
            uintptr_t const uNeedle = (uintptr_t)g_pfnNtDeviceIoControlFile;
            uintptr_t       cLeft   = (cbImage - pImps->FirstThunk) / sizeof(*puFirstThunk);
            while (cLeft-- > 0 && *puFirstThunk != 0)
            {
                if (*puFirstThunk == uNeedle)
                    g_ppfnVidNtDeviceIoControlFile = (decltype(NtDeviceIoControlFile) **)puFirstThunk;
                puFirstThunk++;
            }
        }
    }

    if (g_ppfnVidNtDeviceIoControlFile != NULL)
    {
        /* Make the thunk writable we can freely modify it. */
        DWORD fOldProt = PAGE_READONLY;
        VirtualProtect((void *)(uintptr_t)g_ppfnVidNtDeviceIoControlFile, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &fOldProt);

#ifdef NEM_WIN_INTERCEPT_NT_IO_CTLS
        *g_ppfnVidNtDeviceIoControlFile = nemR3WinLogWrapper_NtDeviceIoControlFile;
#endif
        return VINF_SUCCESS;
    }
    return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "Failed to patch NtDeviceIoControlFile import in VID.DLL!");
}


/**
 * Worker for nemR3NativeInit that probes and load the native API.
 *
 * @returns VBox status code.
 * @param   fForced             Whether the HMForced flag is set and we should
 *                              fail if we cannot initialize.
 * @param   pErrInfo            Where to always return error info.
 */
static int nemR3WinInitProbeAndLoad(bool fForced, PRTERRINFO pErrInfo)
{
    /*
     * Check that the DLL files we need are present, but without loading them.
     * We'd like to avoid loading them unnecessarily.
     */
    WCHAR wszPath[MAX_PATH + 64];
    UINT  cwcPath = GetSystemDirectoryW(wszPath, MAX_PATH);
    if (cwcPath >= MAX_PATH || cwcPath < 2)
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "GetSystemDirectoryW failed (%#x / %u)", cwcPath, GetLastError());

    if (wszPath[cwcPath - 1] != '\\' || wszPath[cwcPath - 1] != '/')
        wszPath[cwcPath++] = '\\';
    RTUtf16CopyAscii(&wszPath[cwcPath], RT_ELEMENTS(wszPath) - cwcPath, "WinHvPlatform.dll");
    if (GetFileAttributesW(wszPath) == INVALID_FILE_ATTRIBUTES)
        return RTErrInfoSetF(pErrInfo, VERR_NEM_NOT_AVAILABLE, "The native API dll was not found (%ls)", wszPath);

    /*
     * Check that we're in a VM and that the hypervisor identifies itself as Hyper-V.
     */
    if (!ASMHasCpuId())
        return RTErrInfoSet(pErrInfo, VERR_NEM_NOT_AVAILABLE, "No CPUID support");
    if (!RTX86IsValidStdRange(ASMCpuId_EAX(0)))
        return RTErrInfoSet(pErrInfo, VERR_NEM_NOT_AVAILABLE, "No CPUID leaf #1");
    if (!(ASMCpuId_ECX(1) & X86_CPUID_FEATURE_ECX_HVP))
        return RTErrInfoSet(pErrInfo, VERR_NEM_NOT_AVAILABLE, "Not in a hypervisor partition (HVP=0)");

    uint32_t cMaxHyperLeaf = 0;
    uint32_t uEbx = 0;
    uint32_t uEcx = 0;
    uint32_t uEdx = 0;
    ASMCpuIdExSlow(0x40000000, 0, 0, 0, &cMaxHyperLeaf, &uEbx, &uEcx, &uEdx);
    if (!RTX86IsValidHypervisorRange(cMaxHyperLeaf))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_NOT_AVAILABLE, "Invalid hypervisor CPUID range (%#x %#x %#x %#x)",
                             cMaxHyperLeaf, uEbx, uEcx, uEdx);
    if (   uEbx != UINT32_C(0x7263694d) /* Micr */
        || uEcx != UINT32_C(0x666f736f) /* osof */
        || uEdx != UINT32_C(0x76482074) /* t Hv */)
        return RTErrInfoSetF(pErrInfo, VERR_NEM_NOT_AVAILABLE,
                             "Not Hyper-V CPUID signature: %#x %#x %#x (expected %#x %#x %#x)",
                             uEbx, uEcx, uEdx, UINT32_C(0x7263694d), UINT32_C(0x666f736f), UINT32_C(0x76482074));
    if (cMaxHyperLeaf < UINT32_C(0x40000005))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_NOT_AVAILABLE, "Too narrow hypervisor CPUID range (%#x)", cMaxHyperLeaf);

    /** @todo would be great if we could recognize a root partition from the
     *        CPUID info, but I currently don't dare do that. */

    /*
     * Now try load the DLLs and resolve the APIs.
     */
    static const char * const s_apszDllNames[2] = { "WinHvPlatform.dll",  "vid.dll" };
    RTLDRMOD                  ahMods[2]         = { NIL_RTLDRMOD,          NIL_RTLDRMOD };
    int                       rc = VINF_SUCCESS;
    for (unsigned i = 0; i < RT_ELEMENTS(s_apszDllNames); i++)
    {
        int rc2 = RTLdrLoadSystem(s_apszDllNames[i], true /*fNoUnload*/, &ahMods[i]);
        if (RT_FAILURE(rc2))
        {
            if (!RTErrInfoIsSet(pErrInfo))
                RTErrInfoSetF(pErrInfo, rc2, "Failed to load API DLL: %s: %Rrc", s_apszDllNames[i], rc2);
            else
                RTErrInfoAddF(pErrInfo, rc2, "; %s: %Rrc", s_apszDllNames[i], rc2);
            ahMods[i] = NIL_RTLDRMOD;
            rc = VERR_NEM_INIT_FAILED;
        }
    }
    if (RT_SUCCESS(rc))
        rc = nemR3WinInitVidIntercepts(ahMods[1], pErrInfo);
    if (RT_SUCCESS(rc))
    {
        for (unsigned i = 0; i < RT_ELEMENTS(g_aImports); i++)
        {
            int rc2 = RTLdrGetSymbol(ahMods[g_aImports[i].idxDll], g_aImports[i].pszName, (void **)g_aImports[i].ppfn);
            if (RT_SUCCESS(rc2))
            {
                if (g_aImports[i].fOptional)
                    LogRel(("NEM:  info: Found optional import %s!%s.\n",
                            s_apszDllNames[g_aImports[i].idxDll], g_aImports[i].pszName));
            }
            else
            {
                *g_aImports[i].ppfn = NULL;

                LogRel(("NEM:  %s: Failed to import %s!%s: %Rrc\n",
                        g_aImports[i].fOptional ? "info" : fForced ? "fatal" : "error",
                        s_apszDllNames[g_aImports[i].idxDll], g_aImports[i].pszName, rc2));
                if (!g_aImports[i].fOptional)
                {
                    if (RTErrInfoIsSet(pErrInfo))
                        RTErrInfoAddF(pErrInfo, rc2, ", %s!%s",
                                      s_apszDllNames[g_aImports[i].idxDll], g_aImports[i].pszName);
                    else
                        rc = RTErrInfoSetF(pErrInfo, rc2, "Failed to import: %s!%s",
                                           s_apszDllNames[g_aImports[i].idxDll], g_aImports[i].pszName);
                    Assert(RT_FAILURE(rc));
                }
            }
        }
        if (RT_SUCCESS(rc))
        {
            Assert(!RTErrInfoIsSet(pErrInfo));
        }
    }

    for (unsigned i = 0; i < RT_ELEMENTS(ahMods); i++)
        RTLdrClose(ahMods[i]);
    return rc;
}


/**
 * Wrapper for different WHvGetCapability signatures.
 */
static HRESULT
WHvGetCapabilityWrapper(WHV_CAPABILITY_CODE enmCap, WHV_CAPABILITY *pOutput, uint32_t cbOutput, uint32_t *pcbOutput = NULL)
{
    if (pcbOutput)
        *pcbOutput = cbOutput;
    return g_pfnWHvGetCapability(enmCap, pOutput, cbOutput, pcbOutput);
}


/**
 * Worker for nemR3NativeInit that gets the hypervisor capabilities.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pErrInfo            Where to always return error info.
 */
static int nemR3WinInitCheckCapabilities(PVM pVM, PRTERRINFO pErrInfo)
{
#define NEM_LOG_REL_CAP_EX(a_szField, a_szFmt, a_Value)     LogRel(("NEM: %-38s= " a_szFmt "\n", a_szField, a_Value))
#define NEM_LOG_REL_CAP_SUB_EX(a_szField, a_szFmt, a_Value) LogRel(("NEM:   %36s: " a_szFmt "\n", a_szField, a_Value))
#define NEM_LOG_REL_CAP_SUB(a_szField, a_Value)             NEM_LOG_REL_CAP_SUB_EX(a_szField, "%d", a_Value)
#define NEM_CHECK_RET_SIZE(a_cbRet, a_cbExpect, a_szWhat) do { \
            if ((a_cbRet) != (a_cbExpect) && g_uBuildNo >= 0x19000 /*??*/) \
                LogRel(("NEM: Warning! %s returned %u bytes, expected %u!\n", (a_cbRet), (a_cbExpect) )); \
        } while (0)

    /*
     * Is the hypervisor present with the desired capability?
     *
     * In build 17083 this translates into:
     *      - CPUID[0x00000001].HVP is set
     *      - CPUID[0x40000000] == "Microsoft Hv"
     *      - CPUID[0x40000001].eax == "Hv#1"
     *      - CPUID[0x40000003].ebx[12] is set.
     *      - VidGetExoPartitionProperty(INVALID_HANDLE_VALUE, 0x60000, &Ignored) returns
     *        a non-zero value.
     */
    WHV_CAPABILITY Caps;
    RT_ZERO(Caps);
    UINT32 cbRet;
    SetLastError(0);
    HRESULT hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeHypervisorPresent, &Caps, sizeof(Caps), &cbRet);
    DWORD   rcWin = GetLastError();
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeHypervisorPresent failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT32), "WHvCapabilityCodeHypervisorPresent");
    if (!Caps.HypervisorPresent)
    {
        if (!RTPathExists(RTPATH_NT_PASSTHRU_PREFIX "Device\\VidExo"))
            return RTErrInfoSetF(pErrInfo, VERR_NEM_NOT_AVAILABLE,
                                 "WHvCapabilityCodeHypervisorPresent is FALSE! Make sure you have enabled the 'Windows Hypervisor Platform' feature.");
        return RTErrInfoSetF(pErrInfo, VERR_NEM_NOT_AVAILABLE, "WHvCapabilityCodeHypervisorPresent is FALSE! (%u)", rcWin);
    }
    LogRel(("NEM: WHvCapabilityCodeHypervisorPresent is TRUE, so this might work...\n"));


    /*
     * 0x0002 - Check what extended VM exits are supported.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeExtendedVmExits, &Caps, sizeof(Caps));
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeExtendedVmExits failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64), "WHvCapabilityCodeExtendedVmExits");
    NEM_LOG_REL_CAP_EX("WHvCapabilityCodeExtendedVmExits", "%'#018RX64", Caps.ExtendedVmExits.AsUINT64);
#define NEM_LOG_REL_FIELD(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.ExtendedVmExits.a_Field)
    NEM_LOG_REL_FIELD(X64CpuidExit);
    NEM_LOG_REL_FIELD(X64MsrExit);
    NEM_LOG_REL_FIELD(ExceptionExit);
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_19040
    NEM_LOG_REL_FIELD(X64RdtscExit);
    NEM_LOG_REL_FIELD(X64ApicSmiExitTrap);
    NEM_LOG_REL_FIELD(HypercallExit);
    NEM_LOG_REL_FIELD(X64ApicInitSipiExitTrap);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN11_22000 /** @todo Could be some of these may have been added earlier... */
    NEM_LOG_REL_FIELD(X64ApicWriteLint0ExitTrap);
    NEM_LOG_REL_FIELD(X64ApicWriteLint1ExitTrap);
    NEM_LOG_REL_FIELD(X64ApicWriteSvrExitTrap);
    NEM_LOG_REL_FIELD(UnknownSynicConnection);
    NEM_LOG_REL_FIELD(RetargetUnknownVpciDevice);
    NEM_LOG_REL_FIELD(X64ApicWriteLdrExitTrap);
    NEM_LOG_REL_FIELD(X64ApicWriteDfrExitTrap);
    NEM_LOG_REL_FIELD(GpaAccessFaultExit);
#endif
#undef  NEM_LOG_REL_FIELD
    uint64_t const fKnownVmExits = RT_BIT_64(15) - 1U;
    if (Caps.ExtendedVmExits.AsUINT64 & ~fKnownVmExits)
        NEM_LOG_REL_CAP_SUB_EX("Unknown VM exit defs", "%#RX64", Caps.ExtendedVmExits.AsUINT64 & ~fKnownVmExits);
    pVM->nem.s.fExtendedMsrExit   = RT_BOOL(Caps.ExtendedVmExits.X64MsrExit);
    pVM->nem.s.fExtendedCpuIdExit = RT_BOOL(Caps.ExtendedVmExits.X64CpuidExit);
    pVM->nem.s.fExtendedXcptExit  = RT_BOOL(Caps.ExtendedVmExits.ExceptionExit);
    pVM->nem.s.fExtendedApicInitSipiTrap = RT_BOOL(Caps.ExtendedVmExits.X64ApicInitSipiExitTrap);
    /** @todo RECHECK: WHV_EXTENDED_VM_EXITS typedef. */

    /*
     * 0x0001 - Check features.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeFeatures, &Caps, sizeof(Caps));
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeFeatures failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64), "WHvCapabilityCodeFeatures");
    NEM_LOG_REL_CAP_EX("WHvCapabilityCodeFeatures", "%'#018RX64", Caps.Features.AsUINT64);
#define NEM_LOG_REL_FIELD(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.Features.a_Field)
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_18362
    NEM_LOG_REL_FIELD(PartialUnmap);
    NEM_LOG_REL_FIELD(LocalApicEmulation);
    NEM_LOG_REL_FIELD(Xsave);
    NEM_LOG_REL_FIELD(DirtyPageTracking);
    NEM_LOG_REL_FIELD(SpeculationControl);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_19040
    NEM_LOG_REL_FIELD(ApicRemoteRead);
    NEM_LOG_REL_FIELD(IdleSuspend);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN11_22000 /** @todo Could some of these may have been added earlier... */
    NEM_LOG_REL_FIELD(VirtualPciDeviceSupport);
    NEM_LOG_REL_FIELD(IommuSupport);
    NEM_LOG_REL_FIELD(VpHotAddRemove);
#endif
#undef  NEM_LOG_REL_FIELD
    const uint64_t fKnownFeatures = RT_BIT_64(10) - 1U;
    if (Caps.Features.AsUINT64 & ~fKnownFeatures)
        NEM_LOG_REL_CAP_SUB_EX("Unknown features", "%#RX64", Caps.ExtendedVmExits.AsUINT64 & ~fKnownFeatures);
    pVM->nem.s.fSpeculationControl = RT_BOOL(Caps.Features.SpeculationControl);
    pVM->nem.s.fLocalApicEmulation = RT_BOOL(Caps.Features.LocalApicEmulation);
    pVM->nem.s.fXsaveSupported     = RT_BOOL(Caps.Features.Xsave);
    /** @todo RECHECK: WHV_CAPABILITY_FEATURES typedef. */

    /*
     * 0x0003 - Check supported exception exit bitmap bits.
     * We don't currently require this, so we just log failure.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeExceptionExitBitmap, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64), "WHvCapabilityCodeExceptionExitBitmap");
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeExceptionExitBitmap", "%'#018RX64", Caps.ExceptionExitBitmap);
    }
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodeExceptionExitBitmap failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * 0x0004 - MSR exit bitmap.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeX64MsrExitBitmap, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64), "WHvCapabilityCodeX64MsrExitBitmap");
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeX64MsrExitBitmap", "%'#018RX64", Caps.X64MsrExitBitmap.AsUINT64);
#define NEM_LOG_REL_FIELD(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.X64MsrExitBitmap.a_Field)
        NEM_LOG_REL_FIELD(UnhandledMsrs);
        NEM_LOG_REL_FIELD(TscMsrWrite);
        NEM_LOG_REL_FIELD(TscMsrRead);
        NEM_LOG_REL_FIELD(ApicBaseMsrWrite);
        NEM_LOG_REL_FIELD(MiscEnableMsrRead);
        NEM_LOG_REL_FIELD(McUpdatePatchLevelMsrRead);
#undef  NEM_LOG_REL_FIELD
        const uint64_t fKnown = RT_BIT_64(6) - 1U;
        if (Caps.X64MsrExitBitmap.AsUINT64 & ~fKnown)
            NEM_LOG_REL_CAP_SUB_EX("Unknown MSR exit bits", "%#RX64", Caps.X64MsrExitBitmap.AsUINT64 & ~fKnown);
    }
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodeX64MsrExitBitmap failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * 0x0005 - GPA range population flags.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeGpaRangePopulateFlags, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT32), "WHvCapabilityCodeGpaRangePopulateFlags");
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeGpaRangePopulateFlags", "%'#010RX32", Caps.GpaRangePopulateFlags.AsUINT32);
#define NEM_LOG_REL_FIELD(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.GpaRangePopulateFlags.a_Field)
        NEM_LOG_REL_FIELD(Prefetch);
        NEM_LOG_REL_FIELD(AvoidHardFaults);
#undef  NEM_LOG_REL_FIELD
        const uint32_t fKnown = RT_BIT_32(2) - 1U;
        if (Caps.GpaRangePopulateFlags.AsUINT32 & ~fKnown)
            NEM_LOG_REL_CAP_SUB_EX("Unknown GPA Range Population flags", "%#RX32", Caps.GpaRangePopulateFlags.AsUINT32 & ~fKnown);
    }
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodeGpaRangePopulateFlags failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * 0x0006 - Scheduler features.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeSchedulerFeatures, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64), "WHvCapabilityCodeSchedulerFeatures");
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeSchedulerFeatures", "%'#018RX64", Caps.SchedulerFeatures.AsUINT64);
#define NEM_LOG_REL_FIELD(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.SchedulerFeatures.a_Field)
        NEM_LOG_REL_FIELD(CpuReserve);
        NEM_LOG_REL_FIELD(CpuCap);
        NEM_LOG_REL_FIELD(CpuWeight);
        NEM_LOG_REL_FIELD(CpuGroupId);
        NEM_LOG_REL_FIELD(DisableSmt);
#undef  NEM_LOG_REL_FIELD
        const uint64_t fKnown = RT_BIT_64(5) - 1U;
        if (Caps.SchedulerFeatures.AsUINT64 & ~fKnown)
            NEM_LOG_REL_CAP_SUB_EX("Unknown scheduler features", "%#RX64", Caps.SchedulerFeatures.AsUINT64 & ~fKnown);
    }
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodeSchedulerFeatures failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * 0x1000 - Check that the CPU vendor is supported.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeProcessorVendor, &Caps, sizeof(Caps));
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeProcessorVendor failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT32), "WHvCapabilityCodeProcessorVendor");
    switch (Caps.ProcessorVendor)
    {
        /** @todo RECHECK: WHV_PROCESSOR_VENDOR typedef. */
        case WHvProcessorVendorIntel:
            NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorVendor", "%d - Intel", Caps.ProcessorVendor);
            pVM->nem.s.enmCpuVendor = CPUMCPUVENDOR_INTEL;
            break;
        case WHvProcessorVendorAmd:
            NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorVendor", "%d - AMD", Caps.ProcessorVendor);
            pVM->nem.s.enmCpuVendor = CPUMCPUVENDOR_AMD;
            break;
        case WHvProcessorVendorHygon:
            NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorVendor", "%d - Hygon -- !untested!", Caps.ProcessorVendor);
            pVM->nem.s.enmCpuVendor = CPUMCPUVENDOR_HYGON;
            break;
        default:
            NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorVendor", "%d", Caps.ProcessorVendor);
            return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "Unknown processor vendor: %d", Caps.ProcessorVendor);
    }

    /*
     * 0x1001 - CPU features, guessing these are virtual CPU features?
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeProcessorFeatures, &Caps, sizeof(Caps));
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeProcessorFeatures failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64), "WHvCapabilityCodeProcessorFeatures");
    NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorFeatures", "%'#018RX64", Caps.ProcessorFeatures.AsUINT64);
#define NEM_LOG_REL_FIELD(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.ProcessorFeatures.a_Field)
    NEM_LOG_REL_FIELD(Sse3Support);
    NEM_LOG_REL_FIELD(LahfSahfSupport);
    NEM_LOG_REL_FIELD(Ssse3Support);
    NEM_LOG_REL_FIELD(Sse4_1Support);
    NEM_LOG_REL_FIELD(Sse4_2Support);
    NEM_LOG_REL_FIELD(Sse4aSupport);
    NEM_LOG_REL_FIELD(XopSupport);
    NEM_LOG_REL_FIELD(PopCntSupport);
    NEM_LOG_REL_FIELD(Cmpxchg16bSupport);
    NEM_LOG_REL_FIELD(Altmovcr8Support);
    NEM_LOG_REL_FIELD(LzcntSupport);
    NEM_LOG_REL_FIELD(MisAlignSseSupport);
    NEM_LOG_REL_FIELD(MmxExtSupport);
    NEM_LOG_REL_FIELD(Amd3DNowSupport);
    NEM_LOG_REL_FIELD(ExtendedAmd3DNowSupport);
    NEM_LOG_REL_FIELD(Page1GbSupport);
    NEM_LOG_REL_FIELD(AesSupport);
    NEM_LOG_REL_FIELD(PclmulqdqSupport);
    NEM_LOG_REL_FIELD(PcidSupport);
    NEM_LOG_REL_FIELD(Fma4Support);
    NEM_LOG_REL_FIELD(F16CSupport);
    NEM_LOG_REL_FIELD(RdRandSupport);
    NEM_LOG_REL_FIELD(RdWrFsGsSupport);
    NEM_LOG_REL_FIELD(SmepSupport);
    NEM_LOG_REL_FIELD(EnhancedFastStringSupport);
    NEM_LOG_REL_FIELD(Bmi1Support);
    NEM_LOG_REL_FIELD(Bmi2Support);
    NEM_LOG_REL_FIELD(Reserved1);
    NEM_LOG_REL_FIELD(MovbeSupport);
    NEM_LOG_REL_FIELD(Npiep1Support);
    NEM_LOG_REL_FIELD(DepX87FPUSaveSupport);
    NEM_LOG_REL_FIELD(RdSeedSupport);
    NEM_LOG_REL_FIELD(AdxSupport);
    NEM_LOG_REL_FIELD(IntelPrefetchSupport);
    NEM_LOG_REL_FIELD(SmapSupport);
    NEM_LOG_REL_FIELD(HleSupport);
    NEM_LOG_REL_FIELD(RtmSupport);
    NEM_LOG_REL_FIELD(RdtscpSupport);
    NEM_LOG_REL_FIELD(ClflushoptSupport);
    NEM_LOG_REL_FIELD(ClwbSupport);
    NEM_LOG_REL_FIELD(ShaSupport);
    NEM_LOG_REL_FIELD(X87PointersSavedSupport);
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_17134 /** @todo maybe some of these were added earlier... */
    NEM_LOG_REL_FIELD(InvpcidSupport);
    NEM_LOG_REL_FIELD(IbrsSupport);
    NEM_LOG_REL_FIELD(StibpSupport);
    NEM_LOG_REL_FIELD(IbpbSupport);
# if WDK_NTDDI_VERSION > MY_NTDDI_WIN11_22000
    NEM_LOG_REL_FIELD(UnrestrictedGuestSupport);
# else
    NEM_LOG_REL_FIELD(Reserved2);
# endif
    NEM_LOG_REL_FIELD(SsbdSupport);
    NEM_LOG_REL_FIELD(FastShortRepMovSupport);
    NEM_LOG_REL_FIELD(Reserved3);
    NEM_LOG_REL_FIELD(RdclNo);
    NEM_LOG_REL_FIELD(IbrsAllSupport);
    NEM_LOG_REL_FIELD(Reserved4);
    NEM_LOG_REL_FIELD(SsbNo);
    NEM_LOG_REL_FIELD(RsbANo);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_19040
    NEM_LOG_REL_FIELD(Reserved5);
    NEM_LOG_REL_FIELD(RdPidSupport);
    NEM_LOG_REL_FIELD(UmipSupport);
    NEM_LOG_REL_FIELD(MdsNoSupport);
    NEM_LOG_REL_FIELD(MdClearSupport);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN11_22000
    NEM_LOG_REL_FIELD(TaaNoSupport);
    NEM_LOG_REL_FIELD(TsxCtrlSupport);
    NEM_LOG_REL_FIELD(Reserved6);
#endif
#undef NEM_LOG_REL_FIELD
    pVM->nem.s.uCpuFeatures.u64 = Caps.ProcessorFeatures.AsUINT64;
    /** @todo RECHECK: WHV_PROCESSOR_FEATURES typedef. */

    /*
     * 0x1002 - The cache line flush size.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeProcessorClFlushSize, &Caps, sizeof(Caps));
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeProcessorClFlushSize failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT8), "WHvCapabilityCodeProcessorClFlushSize");
    NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorClFlushSize", "2^%u", Caps.ProcessorClFlushSize);
    if (Caps.ProcessorClFlushSize < 8 && Caps.ProcessorClFlushSize > 9)
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "Unsupported cache line flush size: %u", Caps.ProcessorClFlushSize);
    pVM->nem.s.cCacheLineFlushShift = Caps.ProcessorClFlushSize;

    /*
     * 0x1003 - Check supported Xsave features.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeProcessorXsaveFeatures, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64), "WHvCapabilityCodeProcessorXsaveFeatures");
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorXsaveFeatures", "%'#018RX64", Caps.ProcessorXsaveFeatures.AsUINT64);
#define NEM_LOG_REL_FIELD(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.ProcessorXsaveFeatures.a_Field)
        NEM_LOG_REL_FIELD(XsaveSupport);
        NEM_LOG_REL_FIELD(XsaveoptSupport);
        NEM_LOG_REL_FIELD(AvxSupport);
        NEM_LOG_REL_FIELD(Avx2Support);
        NEM_LOG_REL_FIELD(FmaSupport);
        NEM_LOG_REL_FIELD(MpxSupport);
        NEM_LOG_REL_FIELD(Avx512Support);
        NEM_LOG_REL_FIELD(Avx512DQSupport);
        NEM_LOG_REL_FIELD(Avx512BWSupport);
        NEM_LOG_REL_FIELD(Avx512VLSupport);
        NEM_LOG_REL_FIELD(XsaveCompSupport);
        NEM_LOG_REL_FIELD(XsaveSupervisorSupport);
        NEM_LOG_REL_FIELD(Xcr1Support);
        NEM_LOG_REL_FIELD(Avx512BitalgSupport);
        NEM_LOG_REL_FIELD(Avx512IfmaSupport);
        NEM_LOG_REL_FIELD(Avx512VBmiSupport);
        NEM_LOG_REL_FIELD(Avx512VBmi2Support);
        NEM_LOG_REL_FIELD(Avx512VnniSupport);
        NEM_LOG_REL_FIELD(GfniSupport);
        NEM_LOG_REL_FIELD(VaesSupport);
        NEM_LOG_REL_FIELD(Avx512VPopcntdqSupport);
        NEM_LOG_REL_FIELD(VpclmulqdqSupport);
        NEM_LOG_REL_FIELD(Avx512Bf16Support);
        NEM_LOG_REL_FIELD(Avx512Vp2IntersectSupport);
        NEM_LOG_REL_FIELD(Avx512Fp16Support);
        NEM_LOG_REL_FIELD(XfdSupport);
        NEM_LOG_REL_FIELD(AmxTileSupport);
        NEM_LOG_REL_FIELD(AmxBf16Support);
        NEM_LOG_REL_FIELD(AmxInt8Support);
        NEM_LOG_REL_FIELD(AvxVnniSupport);
#if WDK_NTDDI_VERSION > MY_NTDDI_WIN11_22000 /** @todo Introduced at some later point. */
        NEM_LOG_REL_FIELD(AvxIfmaSupport);
        NEM_LOG_REL_FIELD(AvxNeConvertSupport);
        NEM_LOG_REL_FIELD(AvxVnniInt8Support);
        NEM_LOG_REL_FIELD(AvxVnniInt16Support);
        NEM_LOG_REL_FIELD(Avx10_1_256Support);
        NEM_LOG_REL_FIELD(Avx10_1_512Support);
        NEM_LOG_REL_FIELD(AmxFp16Support);
#endif
#undef  NEM_LOG_REL_FIELD
        const uint64_t fKnown = RT_BIT_64(38) - 1U;
        if (Caps.ProcessorXsaveFeatures.AsUINT64 & ~fKnown)
            NEM_LOG_REL_CAP_SUB_EX("Unknown xsave features", "%#RX64", Caps.ProcessorXsaveFeatures.AsUINT64 & ~fKnown);
    }
    else
        LogRel(("NEM: %s WHvGetCapability/WHvCapabilityCodeProcessorXsaveFeatures failed: %Rhrc (Last=%#x/%u)",
                pVM->nem.s.fXsaveSupported ? "Warning!" : "Harmless:", hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * 0x1004 - Processor clock frequency.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeProcessorClockFrequency, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64), "WHvCapabilityCodeProcessorClockFrequency");
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorClockFrequency", "%'RU64", Caps.ProcessorClockFrequency);
    }
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodeProcessorClockFrequency failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * 0x1005 - Interrupt clock frequency.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeInterruptClockFrequency, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64), "WHvCapabilityCodeInterruptClockFrequency");
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeInterruptClockFrequency", "%'RU64", Caps.InterruptClockFrequency);
    }
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodeInterruptClockFrequency failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * 0x1006 - Processor feature banks.
     * Note! Bank0 is a duplicate of the WHvCapabilityCodeProcessorFeatures dump above.
     */
    RT_ZERO(Caps);
    Caps.ProcessorFeaturesBanks.BanksCount = WHV_PROCESSOR_FEATURES_BANKS_COUNT;
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeProcessorFeaturesBanks, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        AssertCompile(WHV_PROCESSOR_FEATURES_BANKS_COUNT == 2); /* adjust dumper code if this changes. */
        NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64) * 3, "WHvCapabilityCodeProcessorFeaturesBanks");
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorFeaturesBanks", "%u bank(s)", Caps.ProcessorFeaturesBanks.BanksCount);
        if (Caps.ProcessorFeaturesBanks.BanksCount >= 1 || Caps.ProcessorFeaturesBanks.AsUINT64[0])
        {
            NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorFeaturesBanks[0]", "%'#018RX64", Caps.ProcessorFeaturesBanks.AsUINT64[0]);
#define NEM_LOG_REL_FIELD(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.ProcessorFeaturesBanks.Bank0.a_Field)
            NEM_LOG_REL_FIELD(Sse3Support);
            NEM_LOG_REL_FIELD(LahfSahfSupport);
            NEM_LOG_REL_FIELD(Ssse3Support);
            NEM_LOG_REL_FIELD(Sse4_1Support);
            NEM_LOG_REL_FIELD(Sse4_2Support);
            NEM_LOG_REL_FIELD(Sse4aSupport);
            NEM_LOG_REL_FIELD(XopSupport);
            NEM_LOG_REL_FIELD(PopCntSupport);
            NEM_LOG_REL_FIELD(Cmpxchg16bSupport);
            NEM_LOG_REL_FIELD(Altmovcr8Support);
            NEM_LOG_REL_FIELD(LzcntSupport);
            NEM_LOG_REL_FIELD(MisAlignSseSupport);
            NEM_LOG_REL_FIELD(MmxExtSupport);
            NEM_LOG_REL_FIELD(Amd3DNowSupport);
            NEM_LOG_REL_FIELD(ExtendedAmd3DNowSupport);
            NEM_LOG_REL_FIELD(Page1GbSupport);
            NEM_LOG_REL_FIELD(AesSupport);
            NEM_LOG_REL_FIELD(PclmulqdqSupport);
            NEM_LOG_REL_FIELD(PcidSupport);
            NEM_LOG_REL_FIELD(Fma4Support);
            NEM_LOG_REL_FIELD(F16CSupport);
            NEM_LOG_REL_FIELD(RdRandSupport);
            NEM_LOG_REL_FIELD(RdWrFsGsSupport);
            NEM_LOG_REL_FIELD(SmepSupport);
            NEM_LOG_REL_FIELD(EnhancedFastStringSupport);
            NEM_LOG_REL_FIELD(Bmi1Support);
            NEM_LOG_REL_FIELD(Bmi2Support);
            NEM_LOG_REL_FIELD(Reserved1);
            NEM_LOG_REL_FIELD(MovbeSupport);
            NEM_LOG_REL_FIELD(Npiep1Support);
            NEM_LOG_REL_FIELD(DepX87FPUSaveSupport);
            NEM_LOG_REL_FIELD(RdSeedSupport);
            NEM_LOG_REL_FIELD(AdxSupport);
            NEM_LOG_REL_FIELD(IntelPrefetchSupport);
            NEM_LOG_REL_FIELD(SmapSupport);
            NEM_LOG_REL_FIELD(HleSupport);
            NEM_LOG_REL_FIELD(RtmSupport);
            NEM_LOG_REL_FIELD(RdtscpSupport);
            NEM_LOG_REL_FIELD(ClflushoptSupport);
            NEM_LOG_REL_FIELD(ClwbSupport);
            NEM_LOG_REL_FIELD(ShaSupport);
            NEM_LOG_REL_FIELD(X87PointersSavedSupport);
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_17134 /** @todo maybe some of these were added earlier... */
            NEM_LOG_REL_FIELD(InvpcidSupport);
            NEM_LOG_REL_FIELD(IbrsSupport);
            NEM_LOG_REL_FIELD(StibpSupport);
            NEM_LOG_REL_FIELD(IbpbSupport);
# if WDK_NTDDI_VERSION > MY_NTDDI_WIN11_22000
            NEM_LOG_REL_FIELD(UnrestrictedGuestSupport);
# else
            NEM_LOG_REL_FIELD(Reserved2);
# endif
            NEM_LOG_REL_FIELD(SsbdSupport);
            NEM_LOG_REL_FIELD(FastShortRepMovSupport);
            NEM_LOG_REL_FIELD(Reserved3);
            NEM_LOG_REL_FIELD(RdclNo);
            NEM_LOG_REL_FIELD(IbrsAllSupport);
            NEM_LOG_REL_FIELD(Reserved4);
            NEM_LOG_REL_FIELD(SsbNo);
            NEM_LOG_REL_FIELD(RsbANo);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_19040
            NEM_LOG_REL_FIELD(Reserved5);
            NEM_LOG_REL_FIELD(RdPidSupport);
            NEM_LOG_REL_FIELD(UmipSupport);
            NEM_LOG_REL_FIELD(MdsNoSupport);
            NEM_LOG_REL_FIELD(MdClearSupport);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN11_22000
            NEM_LOG_REL_FIELD(TaaNoSupport);
            NEM_LOG_REL_FIELD(TsxCtrlSupport);
            NEM_LOG_REL_FIELD(Reserved6);
#endif
#undef NEM_LOG_REL_FIELD
            /** @todo RECHECK: WHV_PROCESSOR_FEATURES typedef. */
        }
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorFeaturesBanks[1]", "%'#018RX64", Caps.ProcessorFeaturesBanks.AsUINT64[1]);
        if (Caps.ProcessorFeaturesBanks.BanksCount >= 2 || Caps.ProcessorFeaturesBanks.AsUINT64[1])
        {
#define NEM_LOG_REL_FIELD(a_Field) NEM_LOG_REL_CAP_SUB(#a_Field, Caps.ProcessorFeaturesBanks.Bank1.a_Field)
            NEM_LOG_REL_FIELD(ACountMCountSupport);
            NEM_LOG_REL_FIELD(TscInvariantSupport);
            NEM_LOG_REL_FIELD(ClZeroSupport);
            NEM_LOG_REL_FIELD(RdpruSupport);
#if WDK_NTDDI_VERSION > MY_NTDDI_WIN11_22000
            NEM_LOG_REL_FIELD(La57Support);
            NEM_LOG_REL_FIELD(MbecSupport);
#else
            NEM_LOG_REL_FIELD(Reserved2);
#endif
            NEM_LOG_REL_FIELD(NestedVirtSupport);
            NEM_LOG_REL_FIELD(PsfdSupport);
            NEM_LOG_REL_FIELD(CetSsSupport);
            NEM_LOG_REL_FIELD(CetIbtSupport);
            NEM_LOG_REL_FIELD(VmxExceptionInjectSupport);
#if WDK_NTDDI_VERSION > MY_NTDDI_WIN11_22000
            NEM_LOG_REL_FIELD(Reserved2);
#else
            NEM_LOG_REL_FIELD(Reserved4);
#endif
            NEM_LOG_REL_FIELD(UmwaitTpauseSupport);
            NEM_LOG_REL_FIELD(MovdiriSupport);
            NEM_LOG_REL_FIELD(Movdir64bSupport);
            NEM_LOG_REL_FIELD(CldemoteSupport);
            NEM_LOG_REL_FIELD(SerializeSupport);
            NEM_LOG_REL_FIELD(TscDeadlineTmrSupport);
            NEM_LOG_REL_FIELD(TscAdjustSupport);
            NEM_LOG_REL_FIELD(FZLRepMovsb);
            NEM_LOG_REL_FIELD(FSRepStosb);
            NEM_LOG_REL_FIELD(FSRepCmpsb);
#if WDK_NTDDI_VERSION > MY_NTDDI_WIN11_22000
            NEM_LOG_REL_FIELD(TsxLdTrkSupport);
            NEM_LOG_REL_FIELD(VmxInsOutsExitInfoSupport);
            NEM_LOG_REL_FIELD(Reserved3);
            NEM_LOG_REL_FIELD(SbdrSsdpNoSupport);
            NEM_LOG_REL_FIELD(FbsdpNoSupport);
            NEM_LOG_REL_FIELD(PsdpNoSupport);
            NEM_LOG_REL_FIELD(FbClearSupport);
            NEM_LOG_REL_FIELD(BtcNoSupport);
            NEM_LOG_REL_FIELD(IbpbRsbFlushSupport);
            NEM_LOG_REL_FIELD(StibpAlwaysOnSupport);
            NEM_LOG_REL_FIELD(PerfGlobalCtrlSupport);
            NEM_LOG_REL_FIELD(NptExecuteOnlySupport);
            NEM_LOG_REL_FIELD(NptADFlagsSupport);
            NEM_LOG_REL_FIELD(Npt1GbPageSupport);
            NEM_LOG_REL_FIELD(Reserved4);
            NEM_LOG_REL_FIELD(Reserved5);
            NEM_LOG_REL_FIELD(Reserved6);
            NEM_LOG_REL_FIELD(Reserved7);
            NEM_LOG_REL_FIELD(CmpccxaddSupport);
            NEM_LOG_REL_FIELD(Reserved8);
            NEM_LOG_REL_FIELD(Reserved9);
            NEM_LOG_REL_FIELD(Reserved10);
            NEM_LOG_REL_FIELD(Reserved11);
            NEM_LOG_REL_FIELD(PrefetchISupport);
            NEM_LOG_REL_FIELD(Sha512Support);
            NEM_LOG_REL_FIELD(Reserved12);
            NEM_LOG_REL_FIELD(Reserved13);
            NEM_LOG_REL_FIELD(Reserved14);
            NEM_LOG_REL_FIELD(SM3Support);
            NEM_LOG_REL_FIELD(SM4Support);
            uint64_t const fKnown = RT_BIT_64(64 - 12) - 1U;
#else
            uint64_t const fKnown = RT_BIT_64(64 - 42) - 1U;
#endif
#undef NEM_LOG_REL_FIELD
            if (Caps.ProcessorFeaturesBanks.Bank1.AsUINT64 & ~fKnown)
                NEM_LOG_REL_CAP_SUB_EX("Unknown bank 1 features", "%#RX64", Caps.ProcessorFeaturesBanks.Bank1.AsUINT64 & ~fKnown);
        }
    }
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodeProcessorFeaturesBanks failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * 0x1007 - Processor frequency cap.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeProcessorFrequencyCap, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        NEM_CHECK_RET_SIZE(cbRet, sizeof(Caps.ProcessorFrequencyCap), "WHvCapabilityCodeProcessorFrequencyCap");
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorFrequencyCap", "%.16Rhxs", &Caps);
        NEM_LOG_REL_CAP_SUB_EX("IsSupported", "%d", Caps.ProcessorFrequencyCap.IsSupported);
        NEM_LOG_REL_CAP_SUB_EX("Reserved", "%RX32", Caps.ProcessorFrequencyCap.Reserved);
        NEM_LOG_REL_CAP_SUB_EX("HighestFrequencyMhz",   "%'RU32", Caps.ProcessorFrequencyCap.HighestFrequencyMhz);
        NEM_LOG_REL_CAP_SUB_EX("NominalFrequencyMhz",   "%'RU32", Caps.ProcessorFrequencyCap.NominalFrequencyMhz);
        NEM_LOG_REL_CAP_SUB_EX("LowestFrequencyMhz",    "%'RU32", Caps.ProcessorFrequencyCap.LowestFrequencyMhz);
        NEM_LOG_REL_CAP_SUB_EX("FrequencyStepMhz",      "%'RU32", Caps.ProcessorFrequencyCap.FrequencyStepMhz);
    }
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodeProcessorFrequencyCap failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * 0x1008 - Synthetic processor freatures.
     */
    RT_ZERO(Caps);
    Caps.SyntheticProcessorFeaturesBanks.BanksCount = WHV_SYNTHETIC_PROCESSOR_FEATURES_BANKS_COUNT;
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeSyntheticProcessorFeaturesBanks, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        AssertCompile(WHV_SYNTHETIC_PROCESSOR_FEATURES_BANKS_COUNT == 1); /* adjust dumper code if this changes. */
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeSyntheticProcessorFeaturesBanks", "%u bank(s)", Caps.ProcessorFeaturesBanks.BanksCount);
        if (Caps.SyntheticProcessorFeaturesBanks.BanksCount >= 1 || Caps.SyntheticProcessorFeaturesBanks.AsUINT64[0])
        {
            NEM_LOG_REL_CAP_EX("WHvCapabilityCodeSyntheticProcessorFeaturesBanks[0]", "%'#018RX64",
                               Caps.SyntheticProcessorFeaturesBanks.AsUINT64[0]);
#define NEM_LOG_REL_FIELD(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.SyntheticProcessorFeaturesBanks.Bank0.a_Field)
            NEM_LOG_REL_FIELD(HypervisorPresent);
            NEM_LOG_REL_FIELD(Hv1);
            NEM_LOG_REL_FIELD(AccessVpRunTimeReg);
            NEM_LOG_REL_FIELD(AccessPartitionReferenceCounter);
            NEM_LOG_REL_FIELD(AccessSynicRegs);
            NEM_LOG_REL_FIELD(AccessSyntheticTimerRegs);
            NEM_LOG_REL_FIELD(AccessIntrCtrlRegs);
            NEM_LOG_REL_FIELD(AccessHypercallRegs);
            NEM_LOG_REL_FIELD(AccessVpIndex);
            NEM_LOG_REL_FIELD(AccessPartitionReferenceTsc);
            NEM_LOG_REL_FIELD(AccessGuestIdleReg);
            NEM_LOG_REL_FIELD(AccessFrequencyRegs);
            NEM_LOG_REL_FIELD(ReservedZ12);
            NEM_LOG_REL_FIELD(ReservedZ13);
            NEM_LOG_REL_FIELD(ReservedZ14);
            NEM_LOG_REL_FIELD(EnableExtendedGvaRangesForFlushVirtualAddressList);
            NEM_LOG_REL_FIELD(ReservedZ16);
            NEM_LOG_REL_FIELD(ReservedZ17);
            NEM_LOG_REL_FIELD(FastHypercallOutput);
            NEM_LOG_REL_FIELD(ReservedZ19);
            NEM_LOG_REL_FIELD(ReservedZ20);
            NEM_LOG_REL_FIELD(ReservedZ21);
            NEM_LOG_REL_FIELD(DirectSyntheticTimers);
            NEM_LOG_REL_FIELD(ReservedZ23);
            NEM_LOG_REL_FIELD(ExtendedProcessorMasks);
            NEM_LOG_REL_FIELD(TbFlushHypercalls);
            NEM_LOG_REL_FIELD(SyntheticClusterIpi);
            NEM_LOG_REL_FIELD(NotifyLongSpinWait);
            NEM_LOG_REL_FIELD(QueryNumaDistance);
            NEM_LOG_REL_FIELD(SignalEvents);
            NEM_LOG_REL_FIELD(RetargetDeviceInterrupt);
#if WDK_NTDDI_VERSION > MY_NTDDI_WIN11_22000
            NEM_LOG_REL_FIELD(RestoreTime);
            NEM_LOG_REL_FIELD(EnlightenedVmcs);
            NEM_LOG_REL_FIELD(NestedDebugCtl);
            NEM_LOG_REL_FIELD(SyntheticTimeUnhaltedTimer);
            NEM_LOG_REL_FIELD(IdleSpecCtrl);
            NEM_LOG_REL_FIELD(ReservedZ36);
            NEM_LOG_REL_FIELD(WakeVps);
            NEM_LOG_REL_FIELD(AccessVpRegs);
            NEM_LOG_REL_FIELD(ReservedZ39);
            NEM_LOG_REL_FIELD(ReservedZ40);
            uint64_t const fKnown = RT_BIT_64(64 - 33) - 1U;
#else
            uint64_t const fKnown = RT_BIT_64(64 - 23) - 1U;
#endif
#undef NEM_LOG_REL_FIELD
            /** @todo RECHECK: WHV_SYNTHETIC_PROCESSOR_FEATURES typedef. */
            if (Caps.SyntheticProcessorFeaturesBanks.AsUINT64[0] & ~fKnown)
                NEM_LOG_REL_CAP_SUB_EX("Unknown bank 0 features", "%#RX64",
                                       Caps.SyntheticProcessorFeaturesBanks.AsUINT64[0] & ~fKnown);
        }
    }
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodeSyntheticProcessorFeaturesBanks failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * 0x1009 - Performance monitor features.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeProcessorPerfmonFeatures, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64), "WHvCapabilityCodeProcessorPerfmonFeatures");
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorPerfmonFeatures", "%'#018RX64", Caps.ProcessorPerfmonFeatures.AsUINT64);
#define NEM_LOG_REL_FIELD(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.ProcessorPerfmonFeatures.a_Field)
        NEM_LOG_REL_FIELD(PmuSupport);
        NEM_LOG_REL_FIELD(LbrSupport);
#undef  NEM_LOG_REL_FIELD
        const uint64_t fKnown = RT_BIT_64(62) - 1U;
        if (Caps.ProcessorPerfmonFeatures.AsUINT64 & ~fKnown)
            NEM_LOG_REL_CAP_SUB_EX("Unknown Perfmon features", "%#RX64", Caps.ProcessorPerfmonFeatures.AsUINT64 & ~fKnown);
    }
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodeProcessorPerfmonFeatures failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * 0x100a - Physical address width.
     */
    RT_ZERO(Caps);
#if WDK_NTDDI_VERSION <= MY_NTDDI_WIN11_22000
# define WHvCapabilityCodePhysicalAddressWidth ((WHV_CAPABILITY_CODE)0x100a)
#endif
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodePhysicalAddressWidth, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
    {
        NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT32), "WHvCapabilityCodePhysicalAddressWidth");
#if WDK_NTDDI_VERSION > MY_NTDDI_WIN11_22000
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodePhysicalAddressWidth", "%RU32", Caps.PhysicalAddressWidth);
#else
        NEM_LOG_REL_CAP_EX("WHvCapabilityCodePhysicalAddressWidth", "%RU32", *(uint32_t const *)&Caps);
#endif
    }
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodePhysicalAddressWidth failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * Nested VMX caps.
     */
    static const struct { uint32_t uCode; const char *pszName; } s_aNestedVmxCaps[] =
    {
        { 0x2000, "WHvCapabilityCodeVmxBasic" },
        { 0x2001, "WHvCapabilityCodeVmxPinbasedCtls" },
        { 0x2002, "WHvCapabilityCodeVmxProcbasedCtls" },
        { 0x2003, "WHvCapabilityCodeVmxExitCtls" },
        { 0x2004, "WHvCapabilityCodeVmxEntryCtls" },
        { 0x2005, "WHvCapabilityCodeVmxMisc" },
        { 0x2006, "WHvCapabilityCodeVmxCr0Fixed0" },
        { 0x2007, "WHvCapabilityCodeVmxCr0Fixed1" },
        { 0x2008, "WHvCapabilityCodeVmxCr4Fixed0" },
        { 0x2009, "WHvCapabilityCodeVmxCr4Fixed1" },
        { 0x200a, "WHvCapabilityCodeVmxVmcsEnum" },
        { 0x200b, "WHvCapabilityCodeVmxProcbasedCtls2" },
        { 0x200c, "WHvCapabilityCodeVmxEptVpidCap" },
        { 0x200d, "WHvCapabilityCodeVmxTruePinbasedCtls" },
        { 0x200e, "WHvCapabilityCodeVmxTrueProcbasedCtls" },
        { 0x200f, "WHvCapabilityCodeVmxTrueExitCtls" },
        { 0x2010, "WHvCapabilityCodeVmxTrueEntryCtls" },
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aNestedVmxCaps); i++)
    {
        RT_ZERO(Caps);
        hrc = WHvGetCapabilityWrapper((WHV_CAPABILITY_CODE)s_aNestedVmxCaps[i].uCode, &Caps, sizeof(Caps));
        if (SUCCEEDED(hrc))
        {
            NEM_CHECK_RET_SIZE(cbRet, sizeof(UINT64), s_aNestedVmxCaps[i].pszName);
            NEM_LOG_REL_CAP_EX(s_aNestedVmxCaps[i].pszName, "%'#018RX64", *(uint64_t const *)&Caps);
        }
    }

    /*
     * See if they've added more properties that we're not aware of.
     */
    if (!IsDebuggerPresent()) /* Too noisy when in debugger, so skip. */
    {
        static const struct { uint32_t iMin, iMax; } s_aUnknowns[] =
        {
            { 0x0007, 0x001f },
            { 0x100b, 0x1017 },
            { 0x2011, 0x2017 },
            { 0x3000, 0x300f },
            { 0x4000, 0x400f },
        };
        for (uint32_t j = 0; j < RT_ELEMENTS(s_aUnknowns); j++)
            for (uint32_t i = s_aUnknowns[j].iMin; i <= s_aUnknowns[j].iMax; i++)
            {
                RT_ZERO(Caps);
                hrc = WHvGetCapabilityWrapper((WHV_CAPABILITY_CODE)i, &Caps, sizeof(Caps), &cbRet);
                if (SUCCEEDED(hrc))
                    LogRel(("NEM: Warning! Unknown capability %#x returning: %.*Rhxs (cbRet=%u)\n",
                            i, sizeof(Caps), &Caps, cbRet));
            }
    }

    /*
     * For proper operation, we require CPUID exits.
     */
    if (!pVM->nem.s.fExtendedCpuIdExit)
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "Missing required extended CPUID exit support");
    if (!pVM->nem.s.fExtendedMsrExit)
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "Missing required extended MSR exit support");
    if (!pVM->nem.s.fExtendedXcptExit)
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "Missing required extended exception exit support");

#undef NEM_LOG_REL_CAP_EX
#undef NEM_LOG_REL_CAP_SUB_EX
#undef NEM_LOG_REL_CAP_SUB
#undef NEM_CHECK_RET_SIZE
    return VINF_SUCCESS;
}

#if 0 /* def LOG_ENABLED */ /** r=aeichner: Causes assertions with newer hosts and isn't of much use anymore anyway. */

/**
 * Used to fill in g_IoCtlGetHvPartitionId.
 */
static NTSTATUS WINAPI
nemR3WinIoctlDetector_GetHvPartitionId(HANDLE hFile, HANDLE hEvt, PIO_APC_ROUTINE pfnApcCallback, PVOID pvApcCtx,
                                       PIO_STATUS_BLOCK pIos, ULONG uFunction, PVOID pvInput, ULONG cbInput,
                                       PVOID pvOutput, ULONG cbOutput)
{
    AssertLogRelMsgReturn(hFile == NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, ("hFile=%p\n", hFile), STATUS_INVALID_PARAMETER_1);
    RT_NOREF(hEvt); RT_NOREF(pfnApcCallback); RT_NOREF(pvApcCtx);
    AssertLogRelMsgReturn(RT_VALID_PTR(pIos), ("pIos=%p\n", pIos), STATUS_INVALID_PARAMETER_5);
    AssertLogRelMsgReturn(cbInput == 0, ("cbInput=%#x\n", cbInput), STATUS_INVALID_PARAMETER_8);
    RT_NOREF(pvInput);

    AssertLogRelMsgReturn(RT_VALID_PTR(pvOutput), ("pvOutput=%p\n", pvOutput), STATUS_INVALID_PARAMETER_9);
    AssertLogRelMsgReturn(cbOutput == sizeof(HV_PARTITION_ID), ("cbInput=%#x\n", cbInput), STATUS_INVALID_PARAMETER_10);
    *(HV_PARTITION_ID *)pvOutput = NEM_WIN_IOCTL_DETECTOR_FAKE_PARTITION_ID;

    g_IoCtlGetHvPartitionId.cbInput   = cbInput;
    g_IoCtlGetHvPartitionId.cbOutput  = cbOutput;
    g_IoCtlGetHvPartitionId.uFunction = uFunction;

    return STATUS_SUCCESS;
}


/**
 * Used to fill in g_IoCtlGetHvPartitionId.
 */
static NTSTATUS WINAPI
nemR3WinIoctlDetector_GetPartitionProperty(HANDLE hFile, HANDLE hEvt, PIO_APC_ROUTINE pfnApcCallback, PVOID pvApcCtx,
                                           PIO_STATUS_BLOCK pIos, ULONG uFunction, PVOID pvInput, ULONG cbInput,
                                           PVOID pvOutput, ULONG cbOutput)
{
    AssertLogRelMsgReturn(hFile == NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, ("hFile=%p\n", hFile), STATUS_INVALID_PARAMETER_1);
    RT_NOREF(hEvt); RT_NOREF(pfnApcCallback); RT_NOREF(pvApcCtx);
    AssertLogRelMsgReturn(RT_VALID_PTR(pIos), ("pIos=%p\n", pIos), STATUS_INVALID_PARAMETER_5);
    AssertLogRelMsgReturn(cbInput == sizeof(VID_PARTITION_PROPERTY_CODE), ("cbInput=%#x\n", cbInput), STATUS_INVALID_PARAMETER_8);
    AssertLogRelMsgReturn(RT_VALID_PTR(pvInput), ("pvInput=%p\n", pvInput), STATUS_INVALID_PARAMETER_9);
    AssertLogRelMsgReturn(*(VID_PARTITION_PROPERTY_CODE *)pvInput == NEM_WIN_IOCTL_DETECTOR_FAKE_PARTITION_PROPERTY_CODE,
                          ("*pvInput=%#x, expected %#x\n", *(HV_PARTITION_PROPERTY_CODE *)pvInput,
                           NEM_WIN_IOCTL_DETECTOR_FAKE_PARTITION_PROPERTY_CODE), STATUS_INVALID_PARAMETER_9);
    AssertLogRelMsgReturn(RT_VALID_PTR(pvOutput), ("pvOutput=%p\n", pvOutput), STATUS_INVALID_PARAMETER_9);
    AssertLogRelMsgReturn(cbOutput == sizeof(HV_PARTITION_PROPERTY), ("cbInput=%#x\n", cbInput), STATUS_INVALID_PARAMETER_10);
    *(HV_PARTITION_PROPERTY *)pvOutput = NEM_WIN_IOCTL_DETECTOR_FAKE_PARTITION_PROPERTY_VALUE;

    g_IoCtlGetPartitionProperty.cbInput   = cbInput;
    g_IoCtlGetPartitionProperty.cbOutput  = cbOutput;
    g_IoCtlGetPartitionProperty.uFunction = uFunction;

    return STATUS_SUCCESS;
}


/**
 * Used to fill in g_IoCtlStartVirtualProcessor.
 */
static NTSTATUS WINAPI
nemR3WinIoctlDetector_StartVirtualProcessor(HANDLE hFile, HANDLE hEvt, PIO_APC_ROUTINE pfnApcCallback, PVOID pvApcCtx,
                                            PIO_STATUS_BLOCK pIos, ULONG uFunction, PVOID pvInput, ULONG cbInput,
                                            PVOID pvOutput, ULONG cbOutput)
{
    AssertLogRelMsgReturn(hFile == NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, ("hFile=%p\n", hFile), STATUS_INVALID_PARAMETER_1);
    RT_NOREF(hEvt); RT_NOREF(pfnApcCallback); RT_NOREF(pvApcCtx);
    AssertLogRelMsgReturn(RT_VALID_PTR(pIos), ("pIos=%p\n", pIos), STATUS_INVALID_PARAMETER_5);
    AssertLogRelMsgReturn(cbInput == sizeof(HV_VP_INDEX), ("cbInput=%#x\n", cbInput), STATUS_INVALID_PARAMETER_8);
    AssertLogRelMsgReturn(RT_VALID_PTR(pvInput), ("pvInput=%p\n", pvInput), STATUS_INVALID_PARAMETER_9);
    AssertLogRelMsgReturn(*(HV_VP_INDEX *)pvInput == NEM_WIN_IOCTL_DETECTOR_FAKE_VP_INDEX,
                          ("*piCpu=%u\n", *(HV_VP_INDEX *)pvInput), STATUS_INVALID_PARAMETER_9);
    AssertLogRelMsgReturn(cbOutput == 0, ("cbInput=%#x\n", cbInput), STATUS_INVALID_PARAMETER_10);
    RT_NOREF(pvOutput);

    g_IoCtlStartVirtualProcessor.cbInput   = cbInput;
    g_IoCtlStartVirtualProcessor.cbOutput  = cbOutput;
    g_IoCtlStartVirtualProcessor.uFunction = uFunction;

    return STATUS_SUCCESS;
}


/**
 * Used to fill in g_IoCtlStartVirtualProcessor.
 */
static NTSTATUS WINAPI
nemR3WinIoctlDetector_StopVirtualProcessor(HANDLE hFile, HANDLE hEvt, PIO_APC_ROUTINE pfnApcCallback, PVOID pvApcCtx,
                                           PIO_STATUS_BLOCK pIos, ULONG uFunction, PVOID pvInput, ULONG cbInput,
                                           PVOID pvOutput, ULONG cbOutput)
{
    AssertLogRelMsgReturn(hFile == NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, ("hFile=%p\n", hFile), STATUS_INVALID_PARAMETER_1);
    RT_NOREF(hEvt); RT_NOREF(pfnApcCallback); RT_NOREF(pvApcCtx);
    AssertLogRelMsgReturn(RT_VALID_PTR(pIos), ("pIos=%p\n", pIos), STATUS_INVALID_PARAMETER_5);
    AssertLogRelMsgReturn(cbInput == sizeof(HV_VP_INDEX), ("cbInput=%#x\n", cbInput), STATUS_INVALID_PARAMETER_8);
    AssertLogRelMsgReturn(RT_VALID_PTR(pvInput), ("pvInput=%p\n", pvInput), STATUS_INVALID_PARAMETER_9);
    AssertLogRelMsgReturn(*(HV_VP_INDEX *)pvInput == NEM_WIN_IOCTL_DETECTOR_FAKE_VP_INDEX,
                          ("*piCpu=%u\n", *(HV_VP_INDEX *)pvInput), STATUS_INVALID_PARAMETER_9);
    AssertLogRelMsgReturn(cbOutput == 0, ("cbInput=%#x\n", cbInput), STATUS_INVALID_PARAMETER_10);
    RT_NOREF(pvOutput);

    g_IoCtlStopVirtualProcessor.cbInput   = cbInput;
    g_IoCtlStopVirtualProcessor.cbOutput  = cbOutput;
    g_IoCtlStopVirtualProcessor.uFunction = uFunction;

    return STATUS_SUCCESS;
}


/**
 * Used to fill in g_IoCtlMessageSlotHandleAndGetNext
 */
static NTSTATUS WINAPI
nemR3WinIoctlDetector_MessageSlotHandleAndGetNext(HANDLE hFile, HANDLE hEvt, PIO_APC_ROUTINE pfnApcCallback, PVOID pvApcCtx,
                                                  PIO_STATUS_BLOCK pIos, ULONG uFunction, PVOID pvInput, ULONG cbInput,
                                                  PVOID pvOutput, ULONG cbOutput)
{
    AssertLogRelMsgReturn(hFile == NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, ("hFile=%p\n", hFile), STATUS_INVALID_PARAMETER_1);
    RT_NOREF(hEvt); RT_NOREF(pfnApcCallback); RT_NOREF(pvApcCtx);
    AssertLogRelMsgReturn(RT_VALID_PTR(pIos), ("pIos=%p\n", pIos), STATUS_INVALID_PARAMETER_5);

    if (g_uBuildNo >= 17758)
    {
        /* No timeout since about build 17758, it's now always an infinite wait.  So, a somewhat compatible change.  */
        AssertLogRelMsgReturn(cbInput == RT_UOFFSETOF(VID_IOCTL_INPUT_MESSAGE_SLOT_HANDLE_AND_GET_NEXT, cMillies),
                              ("cbInput=%#x\n", cbInput),
                              STATUS_INVALID_PARAMETER_8);
        AssertLogRelMsgReturn(RT_VALID_PTR(pvInput), ("pvInput=%p\n", pvInput), STATUS_INVALID_PARAMETER_9);
        PCVID_IOCTL_INPUT_MESSAGE_SLOT_HANDLE_AND_GET_NEXT pVidIn = (PCVID_IOCTL_INPUT_MESSAGE_SLOT_HANDLE_AND_GET_NEXT)pvInput;
        AssertLogRelMsgReturn(   pVidIn->iCpu == NEM_WIN_IOCTL_DETECTOR_FAKE_VP_INDEX
                              && pVidIn->fFlags == VID_MSHAGN_F_HANDLE_MESSAGE,
                              ("iCpu=%u fFlags=%#x cMillies=%#x\n", pVidIn->iCpu, pVidIn->fFlags, pVidIn->cMillies),
                              STATUS_INVALID_PARAMETER_9);
        AssertLogRelMsgReturn(cbOutput == 0, ("cbInput=%#x\n", cbInput), STATUS_INVALID_PARAMETER_10);
    }
    else
    {
        AssertLogRelMsgReturn(cbInput == sizeof(VID_IOCTL_INPUT_MESSAGE_SLOT_HANDLE_AND_GET_NEXT), ("cbInput=%#x\n", cbInput),
                              STATUS_INVALID_PARAMETER_8);
        AssertLogRelMsgReturn(RT_VALID_PTR(pvInput), ("pvInput=%p\n", pvInput), STATUS_INVALID_PARAMETER_9);
        PCVID_IOCTL_INPUT_MESSAGE_SLOT_HANDLE_AND_GET_NEXT pVidIn = (PCVID_IOCTL_INPUT_MESSAGE_SLOT_HANDLE_AND_GET_NEXT)pvInput;
        AssertLogRelMsgReturn(   pVidIn->iCpu == NEM_WIN_IOCTL_DETECTOR_FAKE_VP_INDEX
                              && pVidIn->fFlags == VID_MSHAGN_F_HANDLE_MESSAGE
                              && pVidIn->cMillies == NEM_WIN_IOCTL_DETECTOR_FAKE_TIMEOUT,
                              ("iCpu=%u fFlags=%#x cMillies=%#x\n", pVidIn->iCpu, pVidIn->fFlags, pVidIn->cMillies),
                              STATUS_INVALID_PARAMETER_9);
        AssertLogRelMsgReturn(cbOutput == 0, ("cbInput=%#x\n", cbInput), STATUS_INVALID_PARAMETER_10);
        RT_NOREF(pvOutput);
    }

    g_IoCtlMessageSlotHandleAndGetNext.cbInput   = cbInput;
    g_IoCtlMessageSlotHandleAndGetNext.cbOutput  = cbOutput;
    g_IoCtlMessageSlotHandleAndGetNext.uFunction = uFunction;

    return STATUS_SUCCESS;
}

/**
 * Used to fill in what g_pIoCtlDetectForLogging points to.
 */
static NTSTATUS WINAPI nemR3WinIoctlDetector_ForLogging(HANDLE hFile, HANDLE hEvt, PIO_APC_ROUTINE pfnApcCallback, PVOID pvApcCtx,
                                                        PIO_STATUS_BLOCK pIos, ULONG uFunction, PVOID pvInput, ULONG cbInput,
                                                        PVOID pvOutput, ULONG cbOutput)
{
    RT_NOREF(hFile, hEvt, pfnApcCallback, pvApcCtx, pIos, pvInput, pvOutput);

    g_pIoCtlDetectForLogging->cbInput   = cbInput;
    g_pIoCtlDetectForLogging->cbOutput  = cbOutput;
    g_pIoCtlDetectForLogging->uFunction = uFunction;

    return STATUS_SUCCESS;
}

#endif /* LOG_ENABLED */

/**
 * Worker for nemR3NativeInit that detect I/O control function numbers for VID.
 *
 * We use the function numbers directly in ring-0 and to name functions when
 * logging NtDeviceIoControlFile calls.
 *
 * @note    We could alternatively do this by disassembling the respective
 *          functions, but hooking NtDeviceIoControlFile and making fake calls
 *          more easily provides the desired information.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.  Will set I/O
 *                              control info members.
 * @param   pErrInfo            Where to always return error info.
 */
static int nemR3WinInitDiscoverIoControlProperties(PVM pVM, PRTERRINFO pErrInfo)
{
    RT_NOREF(pVM, pErrInfo);

    /*
     * Probe the I/O control information for select VID APIs so we can use
     * them directly from ring-0 and better log them.
     *
     */
#if 0 /* def LOG_ENABLED */ /** r=aeichner: Causes assertions with newer hosts and isn't of much use anymore anyway. */
    decltype(NtDeviceIoControlFile) * const pfnOrg = *g_ppfnVidNtDeviceIoControlFile;

    /* VidGetHvPartitionId - must work due to our memory management. */
    BOOL fRet;
    if (g_pfnVidGetHvPartitionId)
    {
        HV_PARTITION_ID idHvPartition = HV_PARTITION_ID_INVALID;
        *g_ppfnVidNtDeviceIoControlFile = nemR3WinIoctlDetector_GetHvPartitionId;
        fRet = g_pfnVidGetHvPartitionId(NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, &idHvPartition);
        *g_ppfnVidNtDeviceIoControlFile = pfnOrg;
        AssertReturn(fRet && idHvPartition == NEM_WIN_IOCTL_DETECTOR_FAKE_PARTITION_ID && g_IoCtlGetHvPartitionId.uFunction != 0,
                     RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                                   "Problem figuring out VidGetHvPartitionId: fRet=%u idHvPartition=%#x dwErr=%u",
                                   fRet, idHvPartition, GetLastError()) );
        LogRel(("NEM: VidGetHvPartitionId            -> fun:%#x in:%#x out:%#x\n",
                g_IoCtlGetHvPartitionId.uFunction, g_IoCtlGetHvPartitionId.cbInput, g_IoCtlGetHvPartitionId.cbOutput));
    }

    /* VidGetPartitionProperty - must work as it's fallback for VidGetHvPartitionId. */
    if (g_ppfnVidNtDeviceIoControlFile)
    {
        HV_PARTITION_PROPERTY uPropValue = ~NEM_WIN_IOCTL_DETECTOR_FAKE_PARTITION_PROPERTY_VALUE;
        *g_ppfnVidNtDeviceIoControlFile = nemR3WinIoctlDetector_GetPartitionProperty;
        fRet = g_pfnVidGetPartitionProperty(NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, NEM_WIN_IOCTL_DETECTOR_FAKE_PARTITION_PROPERTY_CODE,
                                            &uPropValue);
        *g_ppfnVidNtDeviceIoControlFile = pfnOrg;
        AssertReturn(   fRet
                     && uPropValue == NEM_WIN_IOCTL_DETECTOR_FAKE_PARTITION_PROPERTY_VALUE
                     && g_IoCtlGetHvPartitionId.uFunction != 0,
                     RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                                   "Problem figuring out VidGetPartitionProperty: fRet=%u uPropValue=%#x dwErr=%u",
                                   fRet, uPropValue, GetLastError()) );
        LogRel(("NEM: VidGetPartitionProperty        -> fun:%#x in:%#x out:%#x\n",
                g_IoCtlGetPartitionProperty.uFunction, g_IoCtlGetPartitionProperty.cbInput, g_IoCtlGetPartitionProperty.cbOutput));
    }

    /* VidStartVirtualProcessor */
    *g_ppfnVidNtDeviceIoControlFile = nemR3WinIoctlDetector_StartVirtualProcessor;
    fRet = g_pfnVidStartVirtualProcessor(NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, NEM_WIN_IOCTL_DETECTOR_FAKE_VP_INDEX);
    *g_ppfnVidNtDeviceIoControlFile = pfnOrg;
    AssertStmt(fRet && g_IoCtlStartVirtualProcessor.uFunction != 0,
               RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_NEM_RING3_ONLY,
                                       "Problem figuring out VidStartVirtualProcessor: fRet=%u dwErr=%u", fRet, GetLastError()) );
    LogRel(("NEM: VidStartVirtualProcessor       -> fun:%#x in:%#x out:%#x\n", g_IoCtlStartVirtualProcessor.uFunction,
            g_IoCtlStartVirtualProcessor.cbInput, g_IoCtlStartVirtualProcessor.cbOutput));

    /* VidStopVirtualProcessor */
    *g_ppfnVidNtDeviceIoControlFile = nemR3WinIoctlDetector_StopVirtualProcessor;
    fRet = g_pfnVidStopVirtualProcessor(NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, NEM_WIN_IOCTL_DETECTOR_FAKE_VP_INDEX);
    *g_ppfnVidNtDeviceIoControlFile = pfnOrg;
    AssertStmt(fRet && g_IoCtlStopVirtualProcessor.uFunction != 0,
               RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_NEM_RING3_ONLY,
                                       "Problem figuring out VidStopVirtualProcessor: fRet=%u dwErr=%u", fRet, GetLastError()) );
    LogRel(("NEM: VidStopVirtualProcessor        -> fun:%#x in:%#x out:%#x\n", g_IoCtlStopVirtualProcessor.uFunction,
            g_IoCtlStopVirtualProcessor.cbInput, g_IoCtlStopVirtualProcessor.cbOutput));

    /* VidMessageSlotHandleAndGetNext */
    *g_ppfnVidNtDeviceIoControlFile = nemR3WinIoctlDetector_MessageSlotHandleAndGetNext;
    fRet = g_pfnVidMessageSlotHandleAndGetNext(NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE,
                                               NEM_WIN_IOCTL_DETECTOR_FAKE_VP_INDEX, VID_MSHAGN_F_HANDLE_MESSAGE,
                                               NEM_WIN_IOCTL_DETECTOR_FAKE_TIMEOUT);
    *g_ppfnVidNtDeviceIoControlFile = pfnOrg;
    AssertStmt(fRet && g_IoCtlMessageSlotHandleAndGetNext.uFunction != 0,
               RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_NEM_RING3_ONLY,
                                       "Problem figuring out VidMessageSlotHandleAndGetNext: fRet=%u dwErr=%u",
                                       fRet, GetLastError()) );
    LogRel(("NEM: VidMessageSlotHandleAndGetNext -> fun:%#x in:%#x out:%#x\n",
            g_IoCtlMessageSlotHandleAndGetNext.uFunction, g_IoCtlMessageSlotHandleAndGetNext.cbInput,
            g_IoCtlMessageSlotHandleAndGetNext.cbOutput));

    /* The following are only for logging: */
    union
    {
        VID_MAPPED_MESSAGE_SLOT MapSlot;
        HV_REGISTER_NAME        Name;
        HV_REGISTER_VALUE       Value;
    } uBuf;

    /* VidMessageSlotMap */
    g_pIoCtlDetectForLogging = &g_IoCtlMessageSlotMap;
    *g_ppfnVidNtDeviceIoControlFile = nemR3WinIoctlDetector_ForLogging;
    fRet = g_pfnVidMessageSlotMap(NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, &uBuf.MapSlot, NEM_WIN_IOCTL_DETECTOR_FAKE_VP_INDEX);
    *g_ppfnVidNtDeviceIoControlFile = pfnOrg;
    Assert(fRet);
    LogRel(("NEM: VidMessageSlotMap              -> fun:%#x in:%#x out:%#x\n", g_pIoCtlDetectForLogging->uFunction,
            g_pIoCtlDetectForLogging->cbInput, g_pIoCtlDetectForLogging->cbOutput));

    /* VidGetVirtualProcessorState */
    uBuf.Name = HvRegisterExplicitSuspend;
    g_pIoCtlDetectForLogging = &g_IoCtlGetVirtualProcessorState;
    *g_ppfnVidNtDeviceIoControlFile = nemR3WinIoctlDetector_ForLogging;
    fRet = g_pfnVidGetVirtualProcessorState(NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, NEM_WIN_IOCTL_DETECTOR_FAKE_VP_INDEX,
                                            &uBuf.Name, 1, &uBuf.Value);
    *g_ppfnVidNtDeviceIoControlFile = pfnOrg;
    Assert(fRet);
    LogRel(("NEM: VidGetVirtualProcessorState    -> fun:%#x in:%#x out:%#x\n", g_pIoCtlDetectForLogging->uFunction,
            g_pIoCtlDetectForLogging->cbInput, g_pIoCtlDetectForLogging->cbOutput));

    /* VidSetVirtualProcessorState */
    uBuf.Name = HvRegisterExplicitSuspend;
    g_pIoCtlDetectForLogging = &g_IoCtlSetVirtualProcessorState;
    *g_ppfnVidNtDeviceIoControlFile = nemR3WinIoctlDetector_ForLogging;
    fRet = g_pfnVidSetVirtualProcessorState(NEM_WIN_IOCTL_DETECTOR_FAKE_HANDLE, NEM_WIN_IOCTL_DETECTOR_FAKE_VP_INDEX,
                                            &uBuf.Name, 1, &uBuf.Value);
    *g_ppfnVidNtDeviceIoControlFile = pfnOrg;
    Assert(fRet);
    LogRel(("NEM: VidSetVirtualProcessorState    -> fun:%#x in:%#x out:%#x\n", g_pIoCtlDetectForLogging->uFunction,
            g_pIoCtlDetectForLogging->cbInput, g_pIoCtlDetectForLogging->cbOutput));

    g_pIoCtlDetectForLogging = NULL;
#endif /* LOG_ENABLED */

    return VINF_SUCCESS;
}


/**
 * Creates and sets up a Hyper-V (exo) partition.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pErrInfo            Where to always return error info.
 */
static int nemR3WinInitCreatePartition(PVM pVM, PRTERRINFO pErrInfo)
{
    AssertReturn(!pVM->nem.s.hPartition,       RTErrInfoSet(pErrInfo, VERR_WRONG_ORDER, "Wrong initalization order"));
    AssertReturn(!pVM->nem.s.hPartitionDevice, RTErrInfoSet(pErrInfo, VERR_WRONG_ORDER, "Wrong initalization order"));

    /*
     * Create the partition.
     */
    WHV_PARTITION_HANDLE hPartition;
    HRESULT hrc = WHvCreatePartition(&hPartition);
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "WHvCreatePartition failed with %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());

    int rc = VINF_SUCCESS;

    /*
     * Set partition properties, most importantly the CPU count.
     */
    /**
     * @todo Someone at Microsoft please explain another weird API:
     *  - Why this API doesn't take the WHV_PARTITION_PROPERTY_CODE value as an
     *    argument rather than as part of the struct.  That is so weird if you've
     *    used any other NT or windows API,  including WHvGetCapability().
     *  - Why use PVOID when WHV_PARTITION_PROPERTY is what's expected.  We
     *    technically only need 9 bytes for setting/getting
     *    WHVPartitionPropertyCodeProcessorClFlushSize, but the API insists on 16. */
    WHV_PARTITION_PROPERTY Property;
    RT_ZERO(Property);
    Property.ProcessorCount = pVM->cCpus;
    hrc = WHvSetPartitionProperty(hPartition, WHvPartitionPropertyCodeProcessorCount, &Property, sizeof(Property));
    if (SUCCEEDED(hrc))
    {
        RT_ZERO(Property);
        Property.ExtendedVmExits.X64CpuidExit  = pVM->nem.s.fExtendedCpuIdExit; /** @todo Register fixed results and restrict cpuid exits */
        Property.ExtendedVmExits.X64MsrExit    = pVM->nem.s.fExtendedMsrExit;
        Property.ExtendedVmExits.ExceptionExit = pVM->nem.s.fExtendedXcptExit;
        hrc = WHvSetPartitionProperty(hPartition, WHvPartitionPropertyCodeExtendedVmExits, &Property, sizeof(Property));
        if (SUCCEEDED(hrc))
        {
            /*
             * If the APIC is enabled and LocalApicEmulation is supported we'll use Hyper-V's APIC emulation
             * for best performance.
             */
            PCFGMNODE pCfgmApic = CFGMR3GetChild(CFGMR3GetRoot(pVM), "/Devices/apic");
            if (   pCfgmApic
                && pVM->nem.s.fLocalApicEmulation
                && 0) /** @todo Fix issues in Hyper-V APIC backend before activating. */
            {
                /* If setting this fails log an error but continue. */
                RT_ZERO(Property);
                Property.LocalApicEmulationMode = WHvX64LocalApicEmulationModeXApic;
                hrc = WHvSetPartitionProperty(hPartition, WHvPartitionPropertyCodeLocalApicEmulationMode, &Property, sizeof(Property));
                if (FAILED(hrc))
                {
                    LogRel(("NEM: Failed setting WHvPartitionPropertyCodeLocalApicEmulationMode to WHvX64LocalApicEmulationModeXApic: %Rhrc (Last=%#x/%u)\n",
                            hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
                    pVM->nem.s.fLocalApicEmulation = false;
                }
                else
                {
                    /* For SMP VMs we need INIT-SIPI VM-exits to initialize APs (non-BSPs). */
                    if (pVM->cCpus > 1)
                    {
                        if (pVM->nem.s.fExtendedApicInitSipiTrap)
                        {
                            RT_ZERO(Property);
                            Property.ExtendedVmExits.X64CpuidExit            = pVM->nem.s.fExtendedCpuIdExit;
                            Property.ExtendedVmExits.X64MsrExit              = pVM->nem.s.fExtendedMsrExit;
                            Property.ExtendedVmExits.ExceptionExit           = pVM->nem.s.fExtendedXcptExit;
                            Property.ExtendedVmExits.X64ApicInitSipiExitTrap = pVM->nem.s.fExtendedApicInitSipiTrap;
                            hrc = WHvSetPartitionProperty(hPartition, WHvPartitionPropertyCodeExtendedVmExits, &Property, sizeof(Property));
                            if (FAILED(hrc))
                                LogRel(("NEM: Failed setting WHvPartitionPropertyCodeExtendedVmExits with X64ApicInitSipiExitTrap: %Rhrc (Last=%#x/%u)",
                                        hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
                        }
                        else
                        {
                            LogRel(("NEM: X64ApicInitSipiExitTrap not supported, required by Hyper-V APIC backend for SMP VMs\n"));
                            hrc = E_NOINTERFACE;
                            Assert(FAILED(hrc)); /* Paranoia. */
                        }
                    }
                    if (FAILED(hrc))
                        pVM->nem.s.fLocalApicEmulation = false;
                    else
                    {
                        /* Rewrite the configuration tree to point to our APIC emulation. */
                        PCFGMNODE pCfgmDev = CFGMR3GetChild(CFGMR3GetRoot(pVM), "/Devices");
                        Assert(pCfgmDev);

                        PCFGMNODE pCfgmApicHv = NULL;
                        rc = CFGMR3InsertNode(pCfgmDev, "apic-nem", &pCfgmApicHv);
                        if (RT_SUCCESS(rc))
                        {
                            rc = CFGMR3CopyTree(pCfgmApicHv, pCfgmApic, CFGM_COPY_FLAGS_IGNORE_EXISTING_KEYS | CFGM_COPY_FLAGS_IGNORE_EXISTING_VALUES);
                            if (RT_SUCCESS(rc))
                                CFGMR3RemoveNode(pCfgmApic);
                        }

                        if (RT_FAILURE(rc))
                            rc = RTErrInfoSetF(pErrInfo, rc, "Failed replace APIC device config with Hyper-V one");
                    }
                }
            }
            else
                pVM->nem.s.fLocalApicEmulation = false;

            if (RT_SUCCESS(rc))
            {
                /*
                 * We'll continue setup in nemR3NativeInitAfterCPUM.
                 */
                pVM->nem.s.fCreatedEmts     = false;
                pVM->nem.s.hPartition       = hPartition;
                LogRel(("NEM: Created partition %p. APIC emulation mode: %s\n", hPartition,
                        pVM->nem.s.fLocalApicEmulation ? "Hyper-V" : "VirtualBox"));
                return VINF_SUCCESS;
            }
        }

        rc = RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED,
                           "Failed setting WHvPartitionPropertyCodeExtendedVmExits to %'#RX64: %Rhrc",
                           Property.ExtendedVmExits.AsUINT64, hrc);
    }
    else
        rc = RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED,
                           "Failed setting WHvPartitionPropertyCodeProcessorCount to %u: %Rhrc (Last=%#x/%u)",
                           pVM->cCpus, hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    WHvDeletePartition(hPartition);

    Assert(!pVM->nem.s.hPartitionDevice);
    Assert(!pVM->nem.s.hPartition);
    return rc;
}


/**
 * Makes sure APIC and firmware will not allow X2APIC mode.
 *
 * This is rather ugly.
 *
 * @returns VBox status code
 * @param   pVM             The cross context VM structure.
 */
static int nemR3WinDisableX2Apic(PVM pVM)
{
    /*
     * First make sure the 'Mode' config value of the APIC isn't set to X2APIC.
     * This defaults to APIC, so no need to change unless it's X2APIC.
     */
    PCFGMNODE pCfg = CFGMR3GetChild(CFGMR3GetRoot(pVM), "/Devices/apic/0/Config");
    if (!pCfg)
        pCfg = CFGMR3GetChild(CFGMR3GetRoot(pVM), "/Devices/apic-nem/0/Config");
    if (pCfg)
    {
        uint8_t bMode = 0;
        int rc = CFGMR3QueryU8(pCfg, "Mode", &bMode);
        AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_CFGM_VALUE_NOT_FOUND, ("%Rrc\n", rc), rc);
        if (RT_SUCCESS(rc) && bMode == PDMAPICMODE_X2APIC)
        {
            LogRel(("NEM: Adjusting APIC configuration from X2APIC to APIC max mode.  X2APIC is not supported by the WinHvPlatform API!\n"));
            LogRel(("NEM: Disable Hyper-V if you need X2APIC for your guests!\n"));
            rc = CFGMR3RemoveValue(pCfg, "Mode");
            rc = CFGMR3InsertInteger(pCfg, "Mode", PDMAPICMODE_APIC);
            AssertLogRelRCReturn(rc, rc);
        }
    }

    /*
     * Now the firmwares.
     * These also defaults to APIC and only needs adjusting if configured to X2APIC (2).
     */
    static const char * const s_apszFirmwareConfigs[] =
    {
        "/Devices/efi/0/Config",
        "/Devices/pcbios/0/Config",
    };
    for (unsigned i = 0; i < RT_ELEMENTS(s_apszFirmwareConfigs); i++)
    {
        pCfg = CFGMR3GetChild(CFGMR3GetRoot(pVM), "/Devices/APIC/0/Config");
        if (pCfg)
        {
            uint8_t bMode = 0;
            int rc = CFGMR3QueryU8(pCfg, "APIC", &bMode);
            AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_CFGM_VALUE_NOT_FOUND, ("%Rrc\n", rc), rc);
            if (RT_SUCCESS(rc) && bMode == 2)
            {
                LogRel(("NEM: Adjusting %s/Mode from 2 (X2APIC) to 1 (APIC).\n", s_apszFirmwareConfigs[i]));
                rc = CFGMR3RemoveValue(pCfg, "APIC");
                rc = CFGMR3InsertInteger(pCfg, "APIC", 1);
                AssertLogRelRCReturn(rc, rc);
            }
        }
    }

    return VINF_SUCCESS;
}


DECLHIDDEN(int) nemR3NativeInit(PVM pVM, bool fFallback, bool fForced)
{
    g_uBuildNo = RTSystemGetNtBuildNo();

    /*
     * Some state init.
     */
#ifdef NEM_WIN_WITH_A20
    pVM->nem.s.fA20Enabled = true;
#endif
#if 0
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PNEMCPU pNemCpu = &pVM->apCpusR3[idCpu]->nem.s;
    }
#endif

    /*
     * Error state.
     * The error message will be non-empty on failure and 'rc' will be set too.
     */
    RTERRINFOSTATIC ErrInfo;
    PRTERRINFO pErrInfo = RTErrInfoInitStatic(&ErrInfo);
    int rc = nemR3WinInitProbeAndLoad(fForced, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check the capabilties of the hypervisor, starting with whether it's present.
         */
        rc = nemR3WinInitCheckCapabilities(pVM, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            /*
             * Discover the VID I/O control function numbers we need (for interception
             * only these days).
             */
            rc = nemR3WinInitDiscoverIoControlProperties(pVM, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Create and initialize a partition.
                 */
                rc = nemR3WinInitCreatePartition(pVM, pErrInfo);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Set ourselves as the execution engine and make config adjustments.
                     */
                    VM_SET_MAIN_EXECUTION_ENGINE(pVM, VM_EXEC_ENGINE_NATIVE_API);
                    Log(("NEM: Marked active!\n"));
                    nemR3WinDisableX2Apic(pVM);
                    nemR3DisableCpuIsaExt(pVM, "MONITOR"); /* MONITOR is not supported by Hyper-V (MWAIT is sometimes). */
                    PGMR3EnableNemMode(pVM);

                    /*
                     * Register release statistics
                     */
                    STAMR3Register(pVM, (void *)&pVM->nem.s.cMappedPages, STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                                   "/NEM/PagesCurrentlyMapped", STAMUNIT_PAGES, "Number guest pages currently mapped by the VM");
                    STAMR3Register(pVM, (void *)&pVM->nem.s.StatMapPage, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                                   "/NEM/PagesMapCalls", STAMUNIT_PAGES, "Calls to WHvMapGpaRange/HvCallMapGpaPages");
                    STAMR3Register(pVM, (void *)&pVM->nem.s.StatMapPageFailed, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                                   "/NEM/PagesMapFails", STAMUNIT_PAGES, "Calls to WHvMapGpaRange/HvCallMapGpaPages that failed");
                    STAMR3Register(pVM, (void *)&pVM->nem.s.StatUnmapPage, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                                   "/NEM/PagesUnmapCalls", STAMUNIT_PAGES, "Calls to WHvUnmapGpaRange/HvCallUnmapGpaPages");
                    STAMR3Register(pVM, (void *)&pVM->nem.s.StatUnmapPageFailed, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                                   "/NEM/PagesUnmapFails", STAMUNIT_PAGES, "Calls to WHvUnmapGpaRange/HvCallUnmapGpaPages that failed");
                    STAMR3Register(pVM, &pVM->nem.s.StatProfMapGpaRange, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS,
                                   "/NEM/PagesMapGpaRange", STAMUNIT_TICKS_PER_CALL, "Profiling calls to WHvMapGpaRange for bigger stuff");
                    STAMR3Register(pVM, &pVM->nem.s.StatProfUnmapGpaRange, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS,
                                   "/NEM/PagesUnmapGpaRange", STAMUNIT_TICKS_PER_CALL, "Profiling calls to WHvUnmapGpaRange for bigger stuff");
                    STAMR3Register(pVM, &pVM->nem.s.StatProfMapGpaRangePage, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS,
                                   "/NEM/PagesMapGpaRangePage", STAMUNIT_TICKS_PER_CALL, "Profiling calls to WHvMapGpaRange for single pages");
                    STAMR3Register(pVM, &pVM->nem.s.StatProfUnmapGpaRangePage, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS,
                                   "/NEM/PagesUnmapGpaRangePage", STAMUNIT_TICKS_PER_CALL, "Profiling calls to WHvUnmapGpaRange for single pages");

                    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                    {
                        PNEMCPU pNemCpu = &pVM->apCpusR3[idCpu]->nem.s;
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitPortIo,          STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of port I/O exits",               "/NEM/CPU%u/ExitPortIo", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitMemUnmapped,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of unmapped memory exits",        "/NEM/CPU%u/ExitMemUnmapped", idCpu);
                        //STAMR3RegisterF(pVM, &pNemCpu->StatExitMemIntercept,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of intercepted memory exits",     "/NEM/CPU%u/ExitMemIntercept", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitHalt,            STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of HLT exits",                    "/NEM/CPU%u/ExitHalt", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitInterruptWindow, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of interrupt window exits",       "/NEM/CPU%u/ExitInterruptWindow", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitCpuId,           STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of CPUID exits",                  "/NEM/CPU%u/ExitCpuId", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitMsr,             STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of MSR access exits",             "/NEM/CPU%u/ExitMsr", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitException,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of exception exits",              "/NEM/CPU%u/ExitException", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitExceptionBp,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of #BP exits",                    "/NEM/CPU%u/ExitExceptionBp", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitExceptionDb,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of #DB exits",                    "/NEM/CPU%u/ExitExceptionDb", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitExceptionGp,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of #GP exits",                    "/NEM/CPU%u/ExitExceptionGp", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitExceptionGpMesa, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of #GP exits from mesa driver",   "/NEM/CPU%u/ExitExceptionGpMesa", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitExceptionUd,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of #UD exits",                    "/NEM/CPU%u/ExitExceptionUd", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitExceptionUdHandled, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of handled #UD exits",         "/NEM/CPU%u/ExitExceptionUdHandled", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitUnrecoverable,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of unrecoverable exits",          "/NEM/CPU%u/ExitUnrecoverable", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitApicEoi,         STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of APIC EOI exits",               "/NEM/CPU%u/ExitApicEoi", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitApicSipiInitTrap,STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of APIC SIPI/INIT trap exits",    "/NEM/CPU%u/ExitApicSipiInit", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitCanceled,        STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of canceled exits (host interrupt?)", "/NEM/CPU%u/ExitCanceled", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatGetMsgTimeout,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of get message timeouts/alerts",  "/NEM/CPU%u/GetMsgTimeout", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatStopCpuSuccess,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of successful CPU stops",         "/NEM/CPU%u/StopCpuSuccess", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatStopCpuPending,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of pending CPU stops",            "/NEM/CPU%u/StopCpuPending", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatStopCpuPendingAlerts,STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of pending CPU stop alerts",      "/NEM/CPU%u/StopCpuPendingAlerts", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatStopCpuPendingOdd,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of odd pending CPU stops (see code)", "/NEM/CPU%u/StopCpuPendingOdd", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatCancelChangedState,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of cancel changed state",         "/NEM/CPU%u/CancelChangedState", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatCancelAlertedThread, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of cancel alerted EMT",           "/NEM/CPU%u/CancelAlertedEMT", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatBreakOnFFPre,        STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of pre execution FF breaks",      "/NEM/CPU%u/BreakOnFFPre", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatBreakOnFFPost,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of post execution FF breaks",     "/NEM/CPU%u/BreakOnFFPost", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatBreakOnCancel,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of cancel execution breaks",      "/NEM/CPU%u/BreakOnCancel", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatBreakOnStatus,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of status code breaks",           "/NEM/CPU%u/BreakOnStatus", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatImportOnDemand,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of on-demand state imports",      "/NEM/CPU%u/ImportOnDemand", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatImportOnReturn,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of state imports on loop return", "/NEM/CPU%u/ImportOnReturn", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatImportOnReturnSkipped, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of skipped state imports on loop return", "/NEM/CPU%u/ImportOnReturnSkipped", idCpu);
                        STAMR3RegisterF(pVM, &pNemCpu->StatQueryCpuTick,        STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of TSC queries",                  "/NEM/CPU%u/QueryCpuTick", idCpu);
                    }

#if defined(VBOX_WITH_R0_MODULES) && !defined(VBOX_WITH_MINIMAL_R0)
                    if (!SUPR3IsDriverless())
                    {
                        PUVM pUVM = pVM->pUVM;
                        STAMR3RegisterRefresh(pUVM, &pVM->nem.s.R0Stats.cPagesAvailable, STAMTYPE_U64, STAMVISIBILITY_ALWAYS,
                                              STAMUNIT_PAGES, STAM_REFRESH_GRP_NEM, "Free pages available to the hypervisor",
                                              "/NEM/R0Stats/cPagesAvailable");
                        STAMR3RegisterRefresh(pUVM, &pVM->nem.s.R0Stats.cPagesInUse,     STAMTYPE_U64, STAMVISIBILITY_ALWAYS,
                                              STAMUNIT_PAGES, STAM_REFRESH_GRP_NEM, "Pages in use by hypervisor",
                                              "/NEM/R0Stats/cPagesInUse");
                    }
#endif /* VBOX_WITH_R0_MODULES && !VBOX_WITH_MINIMAL_R0 */

                }
            }
        }
    }

    /*
     * We only fail if in forced mode, otherwise just log the complaint and return.
     */
    Assert(pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API || RTErrInfoIsSet(pErrInfo));
    if (   (fForced || !fFallback)
        && pVM->bMainExecutionEngine != VM_EXEC_ENGINE_NATIVE_API)
        return VMSetError(pVM, RT_SUCCESS_NP(rc) ? VERR_NEM_NOT_AVAILABLE : rc, RT_SRC_POS, "%s", pErrInfo->pszMsg);

    if (RTErrInfoIsSet(pErrInfo))
        LogRel(("NEM: Not available: %s\n", pErrInfo->pszMsg));
    return VINF_SUCCESS;
}


DECLHIDDEN(int) nemR3NativeInitAfterCPUM(PVM pVM)
{
    /*
     * Validate sanity.
     */
    WHV_PARTITION_HANDLE hPartition = pVM->nem.s.hPartition;
    AssertReturn(hPartition != NULL, VERR_WRONG_ORDER);
    AssertReturn(!pVM->nem.s.hPartitionDevice, VERR_WRONG_ORDER);
    AssertReturn(!pVM->nem.s.fCreatedEmts, VERR_WRONG_ORDER);
    AssertReturn(pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API, VERR_WRONG_ORDER);

    /*
     * Determine whether we can and should export/import IA32_SPEC_CTRL.
     */
    pVM->nem.s.fDoIa32SpecCtrl = pVM->nem.s.fSpeculationControl
                              && g_CpumHostFeatures.s.fSpecCtrlMsr
                              && pVM->cpum.ro.GuestFeatures.fSpecCtrlMsr;

    /*
     * Continue setting up the partition now that we've got most of the CPUID feature stuff.
     */
    WHV_PARTITION_PROPERTY Property;
    HRESULT                hrc;

#if 0
    /* Not sure if we really need to set the vendor.
       Update: Apparently we don't. WHvPartitionPropertyCodeProcessorVendor was removed in 17110. */
    RT_ZERO(Property);
    Property.ProcessorVendor = pVM->nem.s.enmCpuVendor == CPUMCPUVENDOR_AMD ? WHvProcessorVendorAmd
                             : WHvProcessorVendorIntel;
    hrc = WHvSetPartitionProperty(hPartition, WHvPartitionPropertyCodeProcessorVendor, &Property, sizeof(Property));
    if (FAILED(hrc))
        return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                          "Failed to set WHvPartitionPropertyCodeProcessorVendor to %u: %Rhrc (Last=%#x/%u)",
                          Property.ProcessorVendor, hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
#endif

    /* Not sure if we really need to set the cache line flush size. */
    RT_ZERO(Property);
    Property.ProcessorClFlushSize = pVM->nem.s.cCacheLineFlushShift;
    hrc = WHvSetPartitionProperty(hPartition, WHvPartitionPropertyCodeProcessorClFlushSize, &Property, sizeof(Property));
    if (FAILED(hrc))
        return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                          "Failed to set WHvPartitionPropertyCodeProcessorClFlushSize to %u: %Rhrc (Last=%#x/%u)",
                          pVM->nem.s.cCacheLineFlushShift, hrc, RTNtLastStatusValue(), RTNtLastErrorValue());

    /* Intercept #DB, #BP and #UD exceptions. */
    RT_ZERO(Property);
    Property.ExceptionExitBitmap = RT_BIT_64(WHvX64ExceptionTypeDebugTrapOrFault)
                                 | RT_BIT_64(WHvX64ExceptionTypeBreakpointTrap)
                                 | RT_BIT_64(WHvX64ExceptionTypeInvalidOpcodeFault);

    /* Intercept #GP to workaround the buggy mesa vmwgfx driver. */
    PVMCPU pVCpu = pVM->apCpusR3[0]; /** @todo In theory per vCPU, in practice same for all. */
    if (pVCpu->nem.s.fTrapXcptGpForLovelyMesaDrv)
        Property.ExceptionExitBitmap |= RT_BIT_64(WHvX64ExceptionTypeGeneralProtectionFault);

    hrc = WHvSetPartitionProperty(hPartition, WHvPartitionPropertyCodeExceptionExitBitmap, &Property, sizeof(Property));
    if (FAILED(hrc))
        return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                          "Failed to set WHvPartitionPropertyCodeExceptionExitBitmap to %#RX64: %Rhrc (Last=%#x/%u)",
                          Property.ExceptionExitBitmap, hrc, RTNtLastStatusValue(), RTNtLastErrorValue());


    /*
     * Sync CPU features with CPUM.
     */
    /** @todo sync CPU features with CPUM. */

    /* Set the partition property. */
    RT_ZERO(Property);
    Property.ProcessorFeatures.AsUINT64 = pVM->nem.s.uCpuFeatures.u64;
    hrc = WHvSetPartitionProperty(hPartition, WHvPartitionPropertyCodeProcessorFeatures, &Property, sizeof(Property));
    if (FAILED(hrc))
        return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                          "Failed to set WHvPartitionPropertyCodeProcessorFeatures to %'#RX64: %Rhrc (Last=%#x/%u)",
                          pVM->nem.s.uCpuFeatures.u64, hrc, RTNtLastStatusValue(), RTNtLastErrorValue());

    /*
     * Set up the partition.
     *
     * Seems like this is where the partition is actually instantiated and we get
     * a handle to it.
     */
    hrc = WHvSetupPartition(hPartition);
    if (FAILED(hrc))
        return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                          "Call to WHvSetupPartition failed: %Rhrc (Last=%#x/%u)",
                          hrc, RTNtLastStatusValue(), RTNtLastErrorValue());

    /*
     * Hysterical raisins: Get the handle (could also fish this out via VID.DLL NtDeviceIoControlFile intercepting).
     */
    HANDLE hPartitionDevice;
    __try
    {
        hPartitionDevice = ((HANDLE *)hPartition)[1];
        if (!hPartitionDevice)
            hPartitionDevice = INVALID_HANDLE_VALUE;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hrc = GetExceptionCode();
        hPartitionDevice = INVALID_HANDLE_VALUE;
    }

    /* Test the handle. */
    HV_PARTITION_PROPERTY uValue = 0;
    if (   g_pfnVidGetPartitionProperty
        && hPartitionDevice != INVALID_HANDLE_VALUE
        && !g_pfnVidGetPartitionProperty(hPartitionDevice, HvPartitionPropertyProcessorVendor, &uValue))
        hPartitionDevice = INVALID_HANDLE_VALUE;
    LogRel(("NEM: HvPartitionPropertyProcessorVendor=%#llx (%lld)\n", uValue, uValue));

    /*
     * More hysterical rasins: Get the partition ID if we can.
     */
    HV_PARTITION_ID idHvPartition = HV_PARTITION_ID_INVALID;
    if (   g_pfnVidGetHvPartitionId
        && hPartitionDevice != INVALID_HANDLE_VALUE
        && !g_pfnVidGetHvPartitionId(hPartitionDevice, &idHvPartition))
    {
        idHvPartition = HV_PARTITION_ID_INVALID;
        Log(("NEM: VidGetHvPartitionId failed: %#x\n", GetLastError()));
    }
    pVM->nem.s.hPartitionDevice = hPartitionDevice;

    /*
     * Setup the EMTs.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        pVCpu = pVM->apCpusR3[idCpu];

        hrc = WHvCreateVirtualProcessor(hPartition, idCpu, 0 /*fFlags*/);
        if (FAILED(hrc))
        {
            NTSTATUS const rcNtLast  = RTNtLastStatusValue();
            DWORD const    dwErrLast = RTNtLastErrorValue();
            while (idCpu-- > 0)
            {
                HRESULT hrc2 = WHvDeleteVirtualProcessor(hPartition, idCpu);
                AssertLogRelMsg(SUCCEEDED(hrc2), ("WHvDeleteVirtualProcessor(%p, %u) -> %Rhrc (Last=%#x/%u)\n",
                                                  hPartition, idCpu, hrc2, RTNtLastStatusValue(),
                                                  RTNtLastErrorValue()));
            }
            return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                              "Call to WHvCreateVirtualProcessor failed: %Rhrc (Last=%#x/%u)", hrc, rcNtLast, dwErrLast);
        }
    }
    pVM->nem.s.fCreatedEmts = true;

    /* Determine the size of the xsave area if supported. */
    if (pVM->nem.s.fXsaveSupported)
    {
        pVCpu = pVM->apCpusR3[0];
        hrc = WHvGetVirtualProcessorXsaveState(pVM->nem.s.hPartition, pVCpu->idCpu, NULL, 0, &pVM->nem.s.cbXSaveArea);
        AssertLogRelMsgReturn(hrc == WHV_E_INSUFFICIENT_BUFFER, ("WHvGetVirtualProcessorState(%p, %u,%x,,) -> %Rhrc (Last=%#x/%u)\n",
                              pVM->nem.s.hPartition, pVCpu->idCpu, WHvVirtualProcessorStateTypeXsaveState,
                              hrc, RTNtLastStatusValue(), RTNtLastErrorValue()), VERR_NEM_VM_CREATE_FAILED);
        LogRel(("NEM: cbXSaveArea=%u\n", pVM->nem.s.cbXSaveArea));
        AssertLogRelMsgReturn(pVM->nem.s.cbXSaveArea <= sizeof(pVCpu->cpum.GstCtx.XState),
                              ("Returned XSAVE area exceeds what VirtualBox supported (%u > %zu)\n",
                              pVM->nem.s.cbXSaveArea, sizeof(pVCpu->cpum.GstCtx.XState)),
                              VERR_NEM_VM_CREATE_FAILED);

        /*
         * Query the default xsave area layout and check whether Hyper-V wants the compacted form. This can't be deduced from the
         * features exposed because at least on Intel CPUs older than Skylake XSaveComp is false but Hyper-V still expects the compacted form.
         * So we just query the default xsave area and the deduce the flag from there.
         */
        X86XSAVEAREA XState;
        hrc = WHvGetVirtualProcessorXsaveState(pVM->nem.s.hPartition, pVCpu->idCpu, &XState, pVM->nem.s.cbXSaveArea, NULL);
        AssertLogRelMsgReturn(hrc == ERROR_SUCCESS, ("WHvGetVirtualProcessorState(%p, %u,%x,,) -> %Rhrc (Last=%#x/%u)\n",
                              pVM->nem.s.hPartition, pVCpu->idCpu, WHvVirtualProcessorStateTypeXsaveState,
                              hrc, RTNtLastStatusValue(), RTNtLastErrorValue()), VERR_NEM_VM_CREATE_FAILED);
       pVM->nem.s.fXsaveComp = RT_BOOL(XState.Hdr.bmXComp & XSAVE_C_X);
       LogRel(("NEM: Default XSAVE area returned by Hyper-V\n%.*Rhxd\n", pVM->nem.s.cbXSaveArea, &XState));
    }

    LogRel(("NEM: Successfully set up partition (device handle %p, partition ID %#llx)\n", hPartitionDevice, idHvPartition));

    /*
     * Any hyper-v statistics we can get at now? HvCallMapStatsPage isn't accessible any more.
     */
    /** @todo stats   */

    /*
     * Adjust features.
     *
     * Note! We've already disabled X2APIC and MONITOR/MWAIT via CFGM during
     *       the first init call.
     */

    return VINF_SUCCESS;
}


DECLHIDDEN(int) nemR3NativeInitCompletedRing3(PVM pVM)
{
    //BOOL fRet = SetThreadPriority(GetCurrentThread(), 0);
    //AssertLogRel(fRet);

    RT_NOREF_PV(pVM);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) nemR3NativeTerm(PVM pVM)
{
    /*
     * Delete the partition.
     */
    WHV_PARTITION_HANDLE hPartition = pVM->nem.s.hPartition;
    pVM->nem.s.hPartition       = NULL;
    pVM->nem.s.hPartitionDevice = NULL;
    if (hPartition != NULL)
    {
        VMCPUID idCpu = pVM->nem.s.fCreatedEmts ? pVM->cCpus : 0;
        LogRel(("NEM: Destroying partition %p with its %u VCpus...\n", hPartition, idCpu));
        while (idCpu-- > 0)
        {
            PVMCPU pVCpu = pVM->apCpusR3[idCpu];
            pVCpu->nem.s.pvMsgSlotMapping = NULL;
            HRESULT hrc = WHvDeleteVirtualProcessor(hPartition, idCpu);
            AssertLogRelMsg(SUCCEEDED(hrc), ("WHvDeleteVirtualProcessor(%p, %u) -> %Rhrc (Last=%#x/%u)\n",
                                             hPartition, idCpu, hrc, RTNtLastStatusValue(),
                                             RTNtLastErrorValue()));
        }
        WHvDeletePartition(hPartition);
    }
    pVM->nem.s.fCreatedEmts = false;
    return VINF_SUCCESS;
}


DECLHIDDEN(void) nemR3NativeReset(PVM pVM)
{
#if 0
    /* Unfix the A20 gate. */
    pVM->nem.s.fA20Fixed = false;
#else
    RT_NOREF(pVM);
#endif
}


DECLHIDDEN(void) nemR3NativeResetCpu(PVMCPU pVCpu, bool fInitIpi)
{
#ifdef NEM_WIN_WITH_A20
    /* Lock the A20 gate if INIT IPI, make sure it's enabled.  */
    if (fInitIpi && pVCpu->idCpu > 0)
    {
        PVM pVM = pVCpu->CTX_SUFF(pVM);
        if (!pVM->nem.s.fA20Enabled)
            nemR3NativeNotifySetA20(pVCpu, true);
        pVM->nem.s.fA20Enabled = true;
        pVM->nem.s.fA20Fixed   = true;
    }
#else
    RT_NOREF(pVCpu, fInitIpi);
#endif
}


static int nemHCWinCopyStateToHyperV(PVMCC pVM, PVMCPUCC pVCpu)
{
    /*
     * The following is very similar to what nemR0WinExportState() does.
     */
    WHV_REGISTER_NAME  aenmNames[128];
    WHV_REGISTER_VALUE aValues[128];

    uint64_t const fWhat = ~pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_ALL | CPUMCTX_EXTRN_NEM_WIN_MASK);
    if (   !fWhat
        && pVCpu->nem.s.fCurrentInterruptWindows == pVCpu->nem.s.fDesiredInterruptWindows)
        return VINF_SUCCESS;
    uintptr_t iReg = 0;

#define ADD_REG64(a_enmName, a_uValue) do { \
            aenmNames[iReg]      = (a_enmName); \
            aValues[iReg].Reg128.High64 = 0; \
            aValues[iReg].Reg64  = (a_uValue); \
            iReg++; \
        } while (0)
#define ADD_REG128(a_enmName, a_uValueLo, a_uValueHi) do { \
            aenmNames[iReg] = (a_enmName); \
            aValues[iReg].Reg128.Low64  = (a_uValueLo); \
            aValues[iReg].Reg128.High64 = (a_uValueHi); \
            iReg++; \
        } while (0)

    /* GPRs */
    if (fWhat & CPUMCTX_EXTRN_GPRS_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_RAX)
            ADD_REG64(WHvX64RegisterRax, pVCpu->cpum.GstCtx.rax);
        if (fWhat & CPUMCTX_EXTRN_RCX)
            ADD_REG64(WHvX64RegisterRcx, pVCpu->cpum.GstCtx.rcx);
        if (fWhat & CPUMCTX_EXTRN_RDX)
            ADD_REG64(WHvX64RegisterRdx, pVCpu->cpum.GstCtx.rdx);
        if (fWhat & CPUMCTX_EXTRN_RBX)
            ADD_REG64(WHvX64RegisterRbx, pVCpu->cpum.GstCtx.rbx);
        if (fWhat & CPUMCTX_EXTRN_RSP)
            ADD_REG64(WHvX64RegisterRsp, pVCpu->cpum.GstCtx.rsp);
        if (fWhat & CPUMCTX_EXTRN_RBP)
            ADD_REG64(WHvX64RegisterRbp, pVCpu->cpum.GstCtx.rbp);
        if (fWhat & CPUMCTX_EXTRN_RSI)
            ADD_REG64(WHvX64RegisterRsi, pVCpu->cpum.GstCtx.rsi);
        if (fWhat & CPUMCTX_EXTRN_RDI)
            ADD_REG64(WHvX64RegisterRdi, pVCpu->cpum.GstCtx.rdi);
        if (fWhat & CPUMCTX_EXTRN_R8_R15)
        {
            ADD_REG64(WHvX64RegisterR8, pVCpu->cpum.GstCtx.r8);
            ADD_REG64(WHvX64RegisterR9, pVCpu->cpum.GstCtx.r9);
            ADD_REG64(WHvX64RegisterR10, pVCpu->cpum.GstCtx.r10);
            ADD_REG64(WHvX64RegisterR11, pVCpu->cpum.GstCtx.r11);
            ADD_REG64(WHvX64RegisterR12, pVCpu->cpum.GstCtx.r12);
            ADD_REG64(WHvX64RegisterR13, pVCpu->cpum.GstCtx.r13);
            ADD_REG64(WHvX64RegisterR14, pVCpu->cpum.GstCtx.r14);
            ADD_REG64(WHvX64RegisterR15, pVCpu->cpum.GstCtx.r15);
        }
    }

    /* RIP & Flags */
    if (fWhat & CPUMCTX_EXTRN_RIP)
        ADD_REG64(WHvX64RegisterRip, pVCpu->cpum.GstCtx.rip);
    if (fWhat & CPUMCTX_EXTRN_RFLAGS)
        ADD_REG64(WHvX64RegisterRflags, pVCpu->cpum.GstCtx.rflags.u);

    /* Segments */
#define ADD_SEG(a_enmName, a_SReg) \
        do { \
            aenmNames[iReg]                  = a_enmName; \
            aValues[iReg].Segment.Base       = (a_SReg).u64Base; \
            aValues[iReg].Segment.Limit      = (a_SReg).u32Limit; \
            aValues[iReg].Segment.Selector   = (a_SReg).Sel; \
            aValues[iReg].Segment.Attributes = (a_SReg).Attr.u; \
            iReg++; \
        } while (0)
    if (fWhat & CPUMCTX_EXTRN_SREG_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_ES)
            ADD_SEG(WHvX64RegisterEs,   pVCpu->cpum.GstCtx.es);
        if (fWhat & CPUMCTX_EXTRN_CS)
            ADD_SEG(WHvX64RegisterCs,   pVCpu->cpum.GstCtx.cs);
        if (fWhat & CPUMCTX_EXTRN_SS)
            ADD_SEG(WHvX64RegisterSs,   pVCpu->cpum.GstCtx.ss);
        if (fWhat & CPUMCTX_EXTRN_DS)
            ADD_SEG(WHvX64RegisterDs,   pVCpu->cpum.GstCtx.ds);
        if (fWhat & CPUMCTX_EXTRN_FS)
            ADD_SEG(WHvX64RegisterFs,   pVCpu->cpum.GstCtx.fs);
        if (fWhat & CPUMCTX_EXTRN_GS)
            ADD_SEG(WHvX64RegisterGs,   pVCpu->cpum.GstCtx.gs);
    }

    /* Descriptor tables & task segment. */
    if (fWhat & CPUMCTX_EXTRN_TABLE_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_LDTR)
            ADD_SEG(WHvX64RegisterLdtr, pVCpu->cpum.GstCtx.ldtr);
        if (fWhat & CPUMCTX_EXTRN_TR)
            ADD_SEG(WHvX64RegisterTr,   pVCpu->cpum.GstCtx.tr);
        if (fWhat & CPUMCTX_EXTRN_IDTR)
        {
            aenmNames[iReg] = WHvX64RegisterIdtr;
            aValues[iReg].Table.Limit = pVCpu->cpum.GstCtx.idtr.cbIdt;
            aValues[iReg].Table.Base  = pVCpu->cpum.GstCtx.idtr.pIdt;
            iReg++;
        }
        if (fWhat & CPUMCTX_EXTRN_GDTR)
        {
            aenmNames[iReg] = WHvX64RegisterGdtr;
            aValues[iReg].Table.Limit = pVCpu->cpum.GstCtx.gdtr.cbGdt;
            aValues[iReg].Table.Base  = pVCpu->cpum.GstCtx.gdtr.pGdt;
            iReg++;
        }
    }

    /* Control registers. */
    if (fWhat & CPUMCTX_EXTRN_CR_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_CR0)
            ADD_REG64(WHvX64RegisterCr0, pVCpu->cpum.GstCtx.cr0);
        if (fWhat & CPUMCTX_EXTRN_CR2)
            ADD_REG64(WHvX64RegisterCr2, pVCpu->cpum.GstCtx.cr2);
        if (fWhat & CPUMCTX_EXTRN_CR3)
            ADD_REG64(WHvX64RegisterCr3, pVCpu->cpum.GstCtx.cr3);
        if (fWhat & CPUMCTX_EXTRN_CR4)
            ADD_REG64(WHvX64RegisterCr4, pVCpu->cpum.GstCtx.cr4);
    }
    if (fWhat & CPUMCTX_EXTRN_APIC_TPR)
        ADD_REG64(WHvX64RegisterCr8, CPUMGetGuestCR8(pVCpu));

    /* Debug registers. */
/** @todo fixme. Figure out what the hyper-v version of KVM_SET_GUEST_DEBUG would be. */
    if (fWhat & CPUMCTX_EXTRN_DR0_DR3)
    {
        ADD_REG64(WHvX64RegisterDr0, pVCpu->cpum.GstCtx.dr[0]); // CPUMGetHyperDR0(pVCpu));
        ADD_REG64(WHvX64RegisterDr1, pVCpu->cpum.GstCtx.dr[1]); // CPUMGetHyperDR1(pVCpu));
        ADD_REG64(WHvX64RegisterDr2, pVCpu->cpum.GstCtx.dr[2]); // CPUMGetHyperDR2(pVCpu));
        ADD_REG64(WHvX64RegisterDr3, pVCpu->cpum.GstCtx.dr[3]); // CPUMGetHyperDR3(pVCpu));
    }
    if (fWhat & CPUMCTX_EXTRN_DR6)
        ADD_REG64(WHvX64RegisterDr6, pVCpu->cpum.GstCtx.dr[6]); // CPUMGetHyperDR6(pVCpu));
    if (fWhat & CPUMCTX_EXTRN_DR7)
        ADD_REG64(WHvX64RegisterDr7, pVCpu->cpum.GstCtx.dr[7]); // CPUMGetHyperDR7(pVCpu));

    if (!pVM->nem.s.fXsaveSupported)
    {
        /* Floating point state. */
        if (fWhat & CPUMCTX_EXTRN_X87)
        {
            ADD_REG128(WHvX64RegisterFpMmx0, pVCpu->cpum.GstCtx.XState.x87.aRegs[0].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[0].au64[1]);
            ADD_REG128(WHvX64RegisterFpMmx1, pVCpu->cpum.GstCtx.XState.x87.aRegs[1].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[1].au64[1]);
            ADD_REG128(WHvX64RegisterFpMmx2, pVCpu->cpum.GstCtx.XState.x87.aRegs[2].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[2].au64[1]);
            ADD_REG128(WHvX64RegisterFpMmx3, pVCpu->cpum.GstCtx.XState.x87.aRegs[3].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[3].au64[1]);
            ADD_REG128(WHvX64RegisterFpMmx4, pVCpu->cpum.GstCtx.XState.x87.aRegs[4].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[4].au64[1]);
            ADD_REG128(WHvX64RegisterFpMmx5, pVCpu->cpum.GstCtx.XState.x87.aRegs[5].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[5].au64[1]);
            ADD_REG128(WHvX64RegisterFpMmx6, pVCpu->cpum.GstCtx.XState.x87.aRegs[6].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[6].au64[1]);
            ADD_REG128(WHvX64RegisterFpMmx7, pVCpu->cpum.GstCtx.XState.x87.aRegs[7].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[7].au64[1]);

            aenmNames[iReg] = WHvX64RegisterFpControlStatus;
            aValues[iReg].FpControlStatus.FpControl = pVCpu->cpum.GstCtx.XState.x87.FCW;
            aValues[iReg].FpControlStatus.FpStatus  = pVCpu->cpum.GstCtx.XState.x87.FSW;
            aValues[iReg].FpControlStatus.FpTag     = pVCpu->cpum.GstCtx.XState.x87.FTW;
            aValues[iReg].FpControlStatus.Reserved  = pVCpu->cpum.GstCtx.XState.x87.FTW >> 8;
            aValues[iReg].FpControlStatus.LastFpOp  = pVCpu->cpum.GstCtx.XState.x87.FOP;
            aValues[iReg].FpControlStatus.LastFpRip = (pVCpu->cpum.GstCtx.XState.x87.FPUIP)
                                                    | ((uint64_t)pVCpu->cpum.GstCtx.XState.x87.CS << 32)
                                                    | ((uint64_t)pVCpu->cpum.GstCtx.XState.x87.Rsrvd1 << 48);
            iReg++;

            aenmNames[iReg] = WHvX64RegisterXmmControlStatus;
            aValues[iReg].XmmControlStatus.LastFpRdp            = (pVCpu->cpum.GstCtx.XState.x87.FPUDP)
                                                                | ((uint64_t)pVCpu->cpum.GstCtx.XState.x87.DS << 32)
                                                                | ((uint64_t)pVCpu->cpum.GstCtx.XState.x87.Rsrvd2 << 48);
            aValues[iReg].XmmControlStatus.XmmStatusControl     = pVCpu->cpum.GstCtx.XState.x87.MXCSR;
            aValues[iReg].XmmControlStatus.XmmStatusControlMask = pVCpu->cpum.GstCtx.XState.x87.MXCSR_MASK; /** @todo ??? (Isn't this an output field?) */
            iReg++;
        }

        /* Vector state. */
        if (fWhat & CPUMCTX_EXTRN_SSE_AVX)
        {
            ADD_REG128(WHvX64RegisterXmm0,  pVCpu->cpum.GstCtx.XState.x87.aXMM[ 0].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 0].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm1,  pVCpu->cpum.GstCtx.XState.x87.aXMM[ 1].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 1].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm2,  pVCpu->cpum.GstCtx.XState.x87.aXMM[ 2].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 2].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm3,  pVCpu->cpum.GstCtx.XState.x87.aXMM[ 3].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 3].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm4,  pVCpu->cpum.GstCtx.XState.x87.aXMM[ 4].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 4].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm5,  pVCpu->cpum.GstCtx.XState.x87.aXMM[ 5].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 5].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm6,  pVCpu->cpum.GstCtx.XState.x87.aXMM[ 6].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 6].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm7,  pVCpu->cpum.GstCtx.XState.x87.aXMM[ 7].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 7].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm8,  pVCpu->cpum.GstCtx.XState.x87.aXMM[ 8].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 8].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm9,  pVCpu->cpum.GstCtx.XState.x87.aXMM[ 9].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 9].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm10, pVCpu->cpum.GstCtx.XState.x87.aXMM[10].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[10].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm11, pVCpu->cpum.GstCtx.XState.x87.aXMM[11].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[11].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm12, pVCpu->cpum.GstCtx.XState.x87.aXMM[12].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[12].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm13, pVCpu->cpum.GstCtx.XState.x87.aXMM[13].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[13].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm14, pVCpu->cpum.GstCtx.XState.x87.aXMM[14].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[14].uXmm.s.Hi);
            ADD_REG128(WHvX64RegisterXmm15, pVCpu->cpum.GstCtx.XState.x87.aXMM[15].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[15].uXmm.s.Hi);
        }
    }
    else if (fWhat & CPUMCTX_EXTRN_XCRx)
        ADD_REG64(WHvX64RegisterXCr0, pVCpu->cpum.GstCtx.aXcr[0]);

    /* MSRs */
    // WHvX64RegisterTsc - don't touch
    if (fWhat & CPUMCTX_EXTRN_EFER)
        ADD_REG64(WHvX64RegisterEfer, pVCpu->cpum.GstCtx.msrEFER);
    if (fWhat & CPUMCTX_EXTRN_KERNEL_GS_BASE)
        ADD_REG64(WHvX64RegisterKernelGsBase, pVCpu->cpum.GstCtx.msrKERNELGSBASE);
    if (fWhat & CPUMCTX_EXTRN_SYSENTER_MSRS)
    {
        ADD_REG64(WHvX64RegisterSysenterCs, pVCpu->cpum.GstCtx.SysEnter.cs);
        ADD_REG64(WHvX64RegisterSysenterEip, pVCpu->cpum.GstCtx.SysEnter.eip);
        ADD_REG64(WHvX64RegisterSysenterEsp, pVCpu->cpum.GstCtx.SysEnter.esp);
    }
    if (fWhat & CPUMCTX_EXTRN_SYSCALL_MSRS)
    {
        ADD_REG64(WHvX64RegisterStar, pVCpu->cpum.GstCtx.msrSTAR);
        ADD_REG64(WHvX64RegisterLstar, pVCpu->cpum.GstCtx.msrLSTAR);
        ADD_REG64(WHvX64RegisterCstar, pVCpu->cpum.GstCtx.msrCSTAR);
        ADD_REG64(WHvX64RegisterSfmask, pVCpu->cpum.GstCtx.msrSFMASK);
    }
    if (fWhat & (CPUMCTX_EXTRN_TSC_AUX | CPUMCTX_EXTRN_OTHER_MSRS))
    {
        PCPUMCTXMSRS const pCtxMsrs = CPUMQueryGuestCtxMsrsPtr(pVCpu);
        if (fWhat & CPUMCTX_EXTRN_TSC_AUX)
            ADD_REG64(WHvX64RegisterTscAux, pCtxMsrs->msr.TscAux);
        if (fWhat & CPUMCTX_EXTRN_OTHER_MSRS)
        {
            ADD_REG64(WHvX64RegisterApicBase, PDMApicGetBaseMsrNoCheck(pVCpu));
            ADD_REG64(WHvX64RegisterPat, pVCpu->cpum.GstCtx.msrPAT);
#if 0 /** @todo check if WHvX64RegisterMsrMtrrCap works here... */
            ADD_REG64(WHvX64RegisterMsrMtrrCap, CPUMGetGuestIa32MtrrCap(pVCpu));
#endif
            ADD_REG64(WHvX64RegisterMsrMtrrDefType, pCtxMsrs->msr.MtrrDefType);
            ADD_REG64(WHvX64RegisterMsrMtrrFix64k00000, pCtxMsrs->msr.MtrrFix64K_00000);
            ADD_REG64(WHvX64RegisterMsrMtrrFix16k80000, pCtxMsrs->msr.MtrrFix16K_80000);
            ADD_REG64(WHvX64RegisterMsrMtrrFix16kA0000, pCtxMsrs->msr.MtrrFix16K_A0000);
            ADD_REG64(WHvX64RegisterMsrMtrrFix4kC0000,  pCtxMsrs->msr.MtrrFix4K_C0000);
            ADD_REG64(WHvX64RegisterMsrMtrrFix4kC8000,  pCtxMsrs->msr.MtrrFix4K_C8000);
            ADD_REG64(WHvX64RegisterMsrMtrrFix4kD0000,  pCtxMsrs->msr.MtrrFix4K_D0000);
            ADD_REG64(WHvX64RegisterMsrMtrrFix4kD8000,  pCtxMsrs->msr.MtrrFix4K_D8000);
            ADD_REG64(WHvX64RegisterMsrMtrrFix4kE0000,  pCtxMsrs->msr.MtrrFix4K_E0000);
            ADD_REG64(WHvX64RegisterMsrMtrrFix4kE8000,  pCtxMsrs->msr.MtrrFix4K_E8000);
            ADD_REG64(WHvX64RegisterMsrMtrrFix4kF0000,  pCtxMsrs->msr.MtrrFix4K_F0000);
            ADD_REG64(WHvX64RegisterMsrMtrrFix4kF8000,  pCtxMsrs->msr.MtrrFix4K_F8000);
            if (pVM->nem.s.fDoIa32SpecCtrl)
                ADD_REG64(WHvX64RegisterSpecCtrl, pCtxMsrs->msr.SpecCtrl);

#if 0 /** @todo these registers aren't available? Might explain something.. .*/
            const CPUMCPUVENDOR enmCpuVendor = CPUMGetHostCpuVendor(pVM);
            if (enmCpuVendor != CPUMCPUVENDOR_AMD)
            {
                ADD_REG64(HvX64RegisterIa32MiscEnable, pCtxMsrs->msr.MiscEnable);
                ADD_REG64(HvX64RegisterIa32FeatureControl, CPUMGetGuestIa32FeatureControl(pVCpu));
            }
#endif
        }
    }

    /* event injection (clear it). */
    if (fWhat & CPUMCTX_EXTRN_NEM_WIN_EVENT_INJECT)
        ADD_REG64(WHvRegisterPendingInterruption, 0);

    if (!pVM->nem.s.fLocalApicEmulation)
    {
        /* Interruptibility state.  This can get a little complicated since we get
           half of the state via HV_X64_VP_EXECUTION_STATE. */
        if (   (fWhat & (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI))
            ==          (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI) )
        {
            ADD_REG64(WHvRegisterInterruptState, 0);
            if (CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx))
                aValues[iReg - 1].InterruptState.InterruptShadow = 1;
            aValues[iReg - 1].InterruptState.NmiMasked = CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx);
        }
        else if (fWhat & CPUMCTX_EXTRN_INHIBIT_INT)
        {
            if (   pVCpu->nem.s.fLastInterruptShadow
                || CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx))
            {
                ADD_REG64(WHvRegisterInterruptState, 0);
                if (CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx))
                    aValues[iReg - 1].InterruptState.InterruptShadow = 1;
                /** @todo Retrieve NMI state, currently assuming it's zero. (yes this may happen on I/O) */
                //if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_BLOCK_NMIS))
                //    aValues[iReg - 1].InterruptState.NmiMasked = 1;
            }
        }
        else
            Assert(!(fWhat & CPUMCTX_EXTRN_INHIBIT_NMI));

        /* Interrupt windows. Always set if active as Hyper-V seems to be forgetful. */
        uint8_t const fDesiredIntWin = pVCpu->nem.s.fDesiredInterruptWindows;
        if (   fDesiredIntWin
            || pVCpu->nem.s.fCurrentInterruptWindows != fDesiredIntWin)
        {
            pVCpu->nem.s.fCurrentInterruptWindows = pVCpu->nem.s.fDesiredInterruptWindows;
            Log8(("Setting WHvX64RegisterDeliverabilityNotifications, fDesiredIntWin=%X\n", fDesiredIntWin));
            ADD_REG64(WHvX64RegisterDeliverabilityNotifications, fDesiredIntWin);
            Assert(aValues[iReg - 1].DeliverabilityNotifications.NmiNotification == RT_BOOL(fDesiredIntWin & NEM_WIN_INTW_F_NMI));
            Assert(aValues[iReg - 1].DeliverabilityNotifications.InterruptNotification == RT_BOOL(fDesiredIntWin & NEM_WIN_INTW_F_REGULAR));
            Assert(aValues[iReg - 1].DeliverabilityNotifications.InterruptPriority == (unsigned)((fDesiredIntWin & NEM_WIN_INTW_F_PRIO_MASK) >> NEM_WIN_INTW_F_PRIO_SHIFT));
        }
    }
    else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_PIC))
    {
        Log8(("Setting WHvX64RegisterDeliverabilityNotifications, fDesiredIntWin=%X fPicReadyForInterrupt=%RTbool\n",
              pVCpu->nem.s.fDesiredInterruptWindows, pVCpu->nem.s.fPicReadyForInterrupt));

        if (   pVCpu->nem.s.fDesiredInterruptWindows
            && pVCpu->nem.s.fPicReadyForInterrupt)
        {
            Assert(pVCpu->cpum.GstCtx.eflags.u & X86_EFL_IF);
            ADD_REG64(WHvRegisterPendingEvent, 0);

            uint8_t bInterrupt;
            int rc = PDMGetInterrupt(pVCpu, &bInterrupt);
            AssertRC(rc);

            aValues[iReg - 1].Reg64                    = 0;
            aValues[iReg - 1].ExtIntEvent.EventPending = 1;
            aValues[iReg - 1].ExtIntEvent.EventType    = WHvX64PendingEventExtInt;
            aValues[iReg - 1].ExtIntEvent.Vector       = bInterrupt;
        }

        if (!pVCpu->nem.s.fIrqWindowRegistered)
        {
            ADD_REG64(WHvX64RegisterDeliverabilityNotifications, 0);
            aValues[iReg - 1].DeliverabilityNotifications.InterruptNotification = 1;
            pVCpu->nem.s.fIrqWindowRegistered = true;
        }
    }

#undef ADD_REG64
#undef ADD_REG128
#undef ADD_SEG

    /*
     * Set the registers.
     */
    Assert(iReg < RT_ELEMENTS(aValues));
    Assert(iReg < RT_ELEMENTS(aenmNames));
#ifdef NEM_WIN_INTERCEPT_NT_IO_CTLS
    Log12(("Calling WHvSetVirtualProcessorRegisters(%p, %u, %p, %u, %p)\n",
           pVM->nem.s.hPartition, pVCpu->idCpu, aenmNames, iReg, aValues));
#endif

    pVCpu->nem.s.fPicReadyForInterrupt = false;

    if (!iReg)
        return VINF_SUCCESS;

    HRESULT hrc = WHvSetVirtualProcessorRegisters(pVM->nem.s.hPartition, pVCpu->idCpu, aenmNames, iReg, aValues);
    if (SUCCEEDED(hrc))
    {
        if (   (fWhat & (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE))
            && pVM->nem.s.fXsaveSupported)
        {
            /** @todo r=aeichner Hyper-V might expect the compacted form and fails with the standard layout then.
             *                   This isn't an issue right now as we don't support anything beyond AVX/AVX2.
             */
            if (pVM->nem.s.fXsaveComp)
                pVCpu->cpum.GstCtx.XState.Hdr.bmXComp = pVCpu->cpum.GstCtx.XState.Hdr.bmXState | XSAVE_C_X;
            if (WHvSetVirtualProcessorState)
                hrc = WHvSetVirtualProcessorState(pVM->nem.s.hPartition, pVCpu->idCpu, WHvVirtualProcessorStateTypeXsaveState,
                                                  &pVCpu->cpum.GstCtx.XState, pVM->nem.s.cbXSaveArea);
            else
                hrc = WHvSetVirtualProcessorXsaveState(pVM->nem.s.hPartition, pVCpu->idCpu, &pVCpu->cpum.GstCtx.XState,
                                                       pVM->nem.s.cbXSaveArea);
            if (pVM->nem.s.fXsaveComp)
                pVCpu->cpum.GstCtx.XState.Hdr.bmXComp &= ~XSAVE_C_X;
            if (FAILED(hrc))
            {
                AssertLogRelMsgFailed(("WHvSetVirtualProcessorState(%p, %u,%x,,) -> %Rhrc (Last=%#x/%u)\n",
                                       pVM->nem.s.hPartition, pVCpu->idCpu, WHvVirtualProcessorStateTypeXsaveState,
                                       hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
                return VERR_INTERNAL_ERROR;
            }
        }

        pVCpu->cpum.GstCtx.fExtrn |= CPUMCTX_EXTRN_ALL | CPUMCTX_EXTRN_NEM_WIN_MASK | CPUMCTX_EXTRN_KEEPER_NEM;
        return VINF_SUCCESS;
    }
    AssertLogRelMsgFailed(("WHvSetVirtualProcessorRegisters(%p, %u,,%u,) -> %Rhrc (Last=%#x/%u)\n",
                           pVM->nem.s.hPartition, pVCpu->idCpu, iReg,
                           hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
    /* Try to figure out the register causing the error. */
    for (uint32_t i = 0; i < iReg; i++)
    {
        hrc = WHvSetVirtualProcessorRegisters(pVM->nem.s.hPartition, pVCpu->idCpu, &aenmNames[i], 1, &aValues[i]);
        if (FAILED(hrc))
        {
            AssertLogRelMsgFailed(("WHvSetVirtualProcessorRegisters(%p, %u, %#RX64, 1, %#RX64) -> %Rhrc (Last=%#x/%u)\n",
                                   pVM->nem.s.hPartition, pVCpu->idCpu, aenmNames[i], aValues[i].Reg64,
                                   hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            break;
        }
    }
    return VERR_INTERNAL_ERROR;
}


static int nemHCWinCopyStateFromHyperV(PVMCC pVM, PVMCPUCC pVCpu, uint64_t fWhat)
{
    WHV_REGISTER_NAME  aenmNames[128];

    fWhat &= pVCpu->cpum.GstCtx.fExtrn;
    uintptr_t iReg = 0;

    /* GPRs */
    if (fWhat & CPUMCTX_EXTRN_GPRS_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_RAX)
            aenmNames[iReg++] = WHvX64RegisterRax;
        if (fWhat & CPUMCTX_EXTRN_RCX)
            aenmNames[iReg++] = WHvX64RegisterRcx;
        if (fWhat & CPUMCTX_EXTRN_RDX)
            aenmNames[iReg++] = WHvX64RegisterRdx;
        if (fWhat & CPUMCTX_EXTRN_RBX)
            aenmNames[iReg++] = WHvX64RegisterRbx;
        if (fWhat & CPUMCTX_EXTRN_RSP)
            aenmNames[iReg++] = WHvX64RegisterRsp;
        if (fWhat & CPUMCTX_EXTRN_RBP)
            aenmNames[iReg++] = WHvX64RegisterRbp;
        if (fWhat & CPUMCTX_EXTRN_RSI)
            aenmNames[iReg++] = WHvX64RegisterRsi;
        if (fWhat & CPUMCTX_EXTRN_RDI)
            aenmNames[iReg++] = WHvX64RegisterRdi;
        if (fWhat & CPUMCTX_EXTRN_R8_R15)
        {
            aenmNames[iReg++] = WHvX64RegisterR8;
            aenmNames[iReg++] = WHvX64RegisterR9;
            aenmNames[iReg++] = WHvX64RegisterR10;
            aenmNames[iReg++] = WHvX64RegisterR11;
            aenmNames[iReg++] = WHvX64RegisterR12;
            aenmNames[iReg++] = WHvX64RegisterR13;
            aenmNames[iReg++] = WHvX64RegisterR14;
            aenmNames[iReg++] = WHvX64RegisterR15;
        }
    }

    /* RIP & Flags */
    if (fWhat & CPUMCTX_EXTRN_RIP)
        aenmNames[iReg++] = WHvX64RegisterRip;
    if (fWhat & CPUMCTX_EXTRN_RFLAGS)
        aenmNames[iReg++] = WHvX64RegisterRflags;

    /* Segments */
    if (fWhat & CPUMCTX_EXTRN_SREG_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_ES)
            aenmNames[iReg++] = WHvX64RegisterEs;
        if (fWhat & CPUMCTX_EXTRN_CS)
            aenmNames[iReg++] = WHvX64RegisterCs;
        if (fWhat & CPUMCTX_EXTRN_SS)
            aenmNames[iReg++] = WHvX64RegisterSs;
        if (fWhat & CPUMCTX_EXTRN_DS)
            aenmNames[iReg++] = WHvX64RegisterDs;
        if (fWhat & CPUMCTX_EXTRN_FS)
            aenmNames[iReg++] = WHvX64RegisterFs;
        if (fWhat & CPUMCTX_EXTRN_GS)
            aenmNames[iReg++] = WHvX64RegisterGs;
    }

    /* Descriptor tables. */
    if (fWhat & CPUMCTX_EXTRN_TABLE_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_LDTR)
            aenmNames[iReg++] = WHvX64RegisterLdtr;
        if (fWhat & CPUMCTX_EXTRN_TR)
            aenmNames[iReg++] = WHvX64RegisterTr;
        if (fWhat & CPUMCTX_EXTRN_IDTR)
            aenmNames[iReg++] = WHvX64RegisterIdtr;
        if (fWhat & CPUMCTX_EXTRN_GDTR)
            aenmNames[iReg++] = WHvX64RegisterGdtr;
    }

    /* Control registers. */
    if (fWhat & CPUMCTX_EXTRN_CR_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_CR0)
            aenmNames[iReg++] = WHvX64RegisterCr0;
        if (fWhat & CPUMCTX_EXTRN_CR2)
            aenmNames[iReg++] = WHvX64RegisterCr2;
        if (fWhat & CPUMCTX_EXTRN_CR3)
            aenmNames[iReg++] = WHvX64RegisterCr3;
        if (fWhat & CPUMCTX_EXTRN_CR4)
            aenmNames[iReg++] = WHvX64RegisterCr4;
    }
    if (fWhat & CPUMCTX_EXTRN_APIC_TPR)
        aenmNames[iReg++] = WHvX64RegisterCr8;

    /* Debug registers. */
    if (fWhat & CPUMCTX_EXTRN_DR7)
        aenmNames[iReg++] = WHvX64RegisterDr7;
    if (fWhat & CPUMCTX_EXTRN_DR0_DR3)
    {
        if (!(fWhat & CPUMCTX_EXTRN_DR7) && (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_DR7))
        {
            fWhat |= CPUMCTX_EXTRN_DR7;
            aenmNames[iReg++] = WHvX64RegisterDr7;
        }
        aenmNames[iReg++] = WHvX64RegisterDr0;
        aenmNames[iReg++] = WHvX64RegisterDr1;
        aenmNames[iReg++] = WHvX64RegisterDr2;
        aenmNames[iReg++] = WHvX64RegisterDr3;
    }
    if (fWhat & CPUMCTX_EXTRN_DR6)
        aenmNames[iReg++] = WHvX64RegisterDr6;

    if (!pVM->nem.s.fXsaveSupported)
    {
        /* Floating point state. */
        if (fWhat & CPUMCTX_EXTRN_X87)
        {
            aenmNames[iReg++] = WHvX64RegisterFpMmx0;
            aenmNames[iReg++] = WHvX64RegisterFpMmx1;
            aenmNames[iReg++] = WHvX64RegisterFpMmx2;
            aenmNames[iReg++] = WHvX64RegisterFpMmx3;
            aenmNames[iReg++] = WHvX64RegisterFpMmx4;
            aenmNames[iReg++] = WHvX64RegisterFpMmx5;
            aenmNames[iReg++] = WHvX64RegisterFpMmx6;
            aenmNames[iReg++] = WHvX64RegisterFpMmx7;
            aenmNames[iReg++] = WHvX64RegisterFpControlStatus;
        }
        if (fWhat & (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX))
            aenmNames[iReg++] = WHvX64RegisterXmmControlStatus;

        /* Vector state. */
        if (fWhat & CPUMCTX_EXTRN_SSE_AVX)
        {
            aenmNames[iReg++] = WHvX64RegisterXmm0;
            aenmNames[iReg++] = WHvX64RegisterXmm1;
            aenmNames[iReg++] = WHvX64RegisterXmm2;
            aenmNames[iReg++] = WHvX64RegisterXmm3;
            aenmNames[iReg++] = WHvX64RegisterXmm4;
            aenmNames[iReg++] = WHvX64RegisterXmm5;
            aenmNames[iReg++] = WHvX64RegisterXmm6;
            aenmNames[iReg++] = WHvX64RegisterXmm7;
            aenmNames[iReg++] = WHvX64RegisterXmm8;
            aenmNames[iReg++] = WHvX64RegisterXmm9;
            aenmNames[iReg++] = WHvX64RegisterXmm10;
            aenmNames[iReg++] = WHvX64RegisterXmm11;
            aenmNames[iReg++] = WHvX64RegisterXmm12;
            aenmNames[iReg++] = WHvX64RegisterXmm13;
            aenmNames[iReg++] = WHvX64RegisterXmm14;
            aenmNames[iReg++] = WHvX64RegisterXmm15;
        }
    }
    else if (fWhat & CPUMCTX_EXTRN_XCRx)
        aenmNames[iReg++] = WHvX64RegisterXCr0;

    /* MSRs */
    // WHvX64RegisterTsc - don't touch
    if (fWhat & CPUMCTX_EXTRN_EFER)
        aenmNames[iReg++] = WHvX64RegisterEfer;
    if (fWhat & CPUMCTX_EXTRN_KERNEL_GS_BASE)
        aenmNames[iReg++] = WHvX64RegisterKernelGsBase;
    if (fWhat & CPUMCTX_EXTRN_SYSENTER_MSRS)
    {
        aenmNames[iReg++] = WHvX64RegisterSysenterCs;
        aenmNames[iReg++] = WHvX64RegisterSysenterEip;
        aenmNames[iReg++] = WHvX64RegisterSysenterEsp;
    }
    if (fWhat & CPUMCTX_EXTRN_SYSCALL_MSRS)
    {
        aenmNames[iReg++] = WHvX64RegisterStar;
        aenmNames[iReg++] = WHvX64RegisterLstar;
        aenmNames[iReg++] = WHvX64RegisterCstar;
        aenmNames[iReg++] = WHvX64RegisterSfmask;
    }

//#ifdef LOG_ENABLED
//    const CPUMCPUVENDOR enmCpuVendor = CPUMGetHostCpuVendor(pVM);
//#endif
    if (fWhat & CPUMCTX_EXTRN_TSC_AUX)
        aenmNames[iReg++] = WHvX64RegisterTscAux;
    if (fWhat & CPUMCTX_EXTRN_OTHER_MSRS)
    {
        aenmNames[iReg++] = WHvX64RegisterApicBase; /// @todo APIC BASE
        aenmNames[iReg++] = WHvX64RegisterPat;
#if 0 /*def LOG_ENABLED*/ /** @todo Check if WHvX64RegisterMsrMtrrCap works... */
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrCap;
#endif
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrDefType;
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrFix64k00000;
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrFix16k80000;
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrFix16kA0000;
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrFix4kC0000;
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrFix4kC8000;
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrFix4kD0000;
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrFix4kD8000;
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrFix4kE0000;
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrFix4kE8000;
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrFix4kF0000;
        aenmNames[iReg++] = WHvX64RegisterMsrMtrrFix4kF8000;
        if (pVM->nem.s.fDoIa32SpecCtrl)
            aenmNames[iReg++] = WHvX64RegisterSpecCtrl;
        /** @todo look for HvX64RegisterIa32MiscEnable and HvX64RegisterIa32FeatureControl? */
//#ifdef LOG_ENABLED
//        if (enmCpuVendor != CPUMCPUVENDOR_AMD)
//            aenmNames[iReg++] = HvX64RegisterIa32FeatureControl;
//#endif
    }

    /* Interruptibility. */
    if (fWhat & (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI))
    {
        aenmNames[iReg++] = WHvRegisterInterruptState;
        aenmNames[iReg++] = WHvX64RegisterRip;
    }

    /* event injection */
    aenmNames[iReg++] = WHvRegisterPendingInterruption;
    aenmNames[iReg++] = WHvRegisterPendingEvent;

    size_t const cRegs = iReg;
    Assert(cRegs < RT_ELEMENTS(aenmNames));

    /*
     * Get the registers.
     */
    WHV_REGISTER_VALUE aValues[128];
    RT_ZERO(aValues);
    Assert(RT_ELEMENTS(aValues) >= cRegs);
    Assert(RT_ELEMENTS(aenmNames) >= cRegs);
#ifdef NEM_WIN_INTERCEPT_NT_IO_CTLS
    Log12(("Calling WHvGetVirtualProcessorRegisters(%p, %u, %p, %u, %p)\n",
          pVM->nem.s.hPartition, pVCpu->idCpu, aenmNames, cRegs, aValues));
#endif
    HRESULT hrc = WHvGetVirtualProcessorRegisters(pVM->nem.s.hPartition, pVCpu->idCpu, aenmNames, (uint32_t)cRegs, aValues);
    AssertLogRelMsgReturn(SUCCEEDED(hrc),
                          ("WHvGetVirtualProcessorRegisters(%p, %u,,%u,) -> %Rhrc (Last=%#x/%u)\n",
                           pVM->nem.s.hPartition, pVCpu->idCpu, cRegs, hrc, RTNtLastStatusValue(), RTNtLastErrorValue())
                          , VERR_NEM_GET_REGISTERS_FAILED);

    iReg = 0;
#define GET_REG64(a_DstVar, a_enmName) do { \
            Assert(aenmNames[iReg] == (a_enmName)); \
            (a_DstVar) = aValues[iReg].Reg64; \
            iReg++; \
        } while (0)
#define GET_REG64_LOG7(a_DstVar, a_enmName, a_szLogName) do { \
            Assert(aenmNames[iReg] == (a_enmName)); \
            if ((a_DstVar) != aValues[iReg].Reg64) \
                Log7(("NEM/%u: " a_szLogName " changed %RX64 -> %RX64\n", pVCpu->idCpu, (a_DstVar), aValues[iReg].Reg64)); \
            (a_DstVar) = aValues[iReg].Reg64; \
            iReg++; \
        } while (0)
#define GET_REG128(a_DstVarLo, a_DstVarHi, a_enmName) do { \
            Assert(aenmNames[iReg] == a_enmName); \
            (a_DstVarLo) = aValues[iReg].Reg128.Low64; \
            (a_DstVarHi) = aValues[iReg].Reg128.High64; \
            iReg++; \
        } while (0)
#define GET_SEG(a_SReg, a_enmName) do { \
            Assert(aenmNames[iReg] == (a_enmName)); \
            NEM_WIN_COPY_BACK_SEG(a_SReg, aValues[iReg].Segment); \
            iReg++; \
        } while (0)

    /* GPRs */
    if (fWhat & CPUMCTX_EXTRN_GPRS_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_RAX)
            GET_REG64(pVCpu->cpum.GstCtx.rax, WHvX64RegisterRax);
        if (fWhat & CPUMCTX_EXTRN_RCX)
            GET_REG64(pVCpu->cpum.GstCtx.rcx, WHvX64RegisterRcx);
        if (fWhat & CPUMCTX_EXTRN_RDX)
            GET_REG64(pVCpu->cpum.GstCtx.rdx, WHvX64RegisterRdx);
        if (fWhat & CPUMCTX_EXTRN_RBX)
            GET_REG64(pVCpu->cpum.GstCtx.rbx, WHvX64RegisterRbx);
        if (fWhat & CPUMCTX_EXTRN_RSP)
            GET_REG64(pVCpu->cpum.GstCtx.rsp, WHvX64RegisterRsp);
        if (fWhat & CPUMCTX_EXTRN_RBP)
            GET_REG64(pVCpu->cpum.GstCtx.rbp, WHvX64RegisterRbp);
        if (fWhat & CPUMCTX_EXTRN_RSI)
            GET_REG64(pVCpu->cpum.GstCtx.rsi, WHvX64RegisterRsi);
        if (fWhat & CPUMCTX_EXTRN_RDI)
            GET_REG64(pVCpu->cpum.GstCtx.rdi, WHvX64RegisterRdi);
        if (fWhat & CPUMCTX_EXTRN_R8_R15)
        {
            GET_REG64(pVCpu->cpum.GstCtx.r8, WHvX64RegisterR8);
            GET_REG64(pVCpu->cpum.GstCtx.r9, WHvX64RegisterR9);
            GET_REG64(pVCpu->cpum.GstCtx.r10, WHvX64RegisterR10);
            GET_REG64(pVCpu->cpum.GstCtx.r11, WHvX64RegisterR11);
            GET_REG64(pVCpu->cpum.GstCtx.r12, WHvX64RegisterR12);
            GET_REG64(pVCpu->cpum.GstCtx.r13, WHvX64RegisterR13);
            GET_REG64(pVCpu->cpum.GstCtx.r14, WHvX64RegisterR14);
            GET_REG64(pVCpu->cpum.GstCtx.r15, WHvX64RegisterR15);
        }
    }

    /* RIP & Flags */
    if (fWhat & CPUMCTX_EXTRN_RIP)
        GET_REG64(pVCpu->cpum.GstCtx.rip, WHvX64RegisterRip);
    if (fWhat & CPUMCTX_EXTRN_RFLAGS)
        GET_REG64(pVCpu->cpum.GstCtx.rflags.u, WHvX64RegisterRflags);

    /* Segments */
    if (fWhat & CPUMCTX_EXTRN_SREG_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_ES)
            GET_SEG(pVCpu->cpum.GstCtx.es, WHvX64RegisterEs);
        if (fWhat & CPUMCTX_EXTRN_CS)
            GET_SEG(pVCpu->cpum.GstCtx.cs, WHvX64RegisterCs);
        if (fWhat & CPUMCTX_EXTRN_SS)
            GET_SEG(pVCpu->cpum.GstCtx.ss, WHvX64RegisterSs);
        if (fWhat & CPUMCTX_EXTRN_DS)
            GET_SEG(pVCpu->cpum.GstCtx.ds, WHvX64RegisterDs);
        if (fWhat & CPUMCTX_EXTRN_FS)
            GET_SEG(pVCpu->cpum.GstCtx.fs, WHvX64RegisterFs);
        if (fWhat & CPUMCTX_EXTRN_GS)
            GET_SEG(pVCpu->cpum.GstCtx.gs, WHvX64RegisterGs);
    }

    /* Descriptor tables and the task segment. */
    if (fWhat & CPUMCTX_EXTRN_TABLE_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_LDTR)
            GET_SEG(pVCpu->cpum.GstCtx.ldtr, WHvX64RegisterLdtr);

        if (fWhat & CPUMCTX_EXTRN_TR)
        {
            /* AMD-V likes loading TR with in AVAIL state, whereas intel insists on BUSY.  So,
               avoid to trigger sanity assertions around the code, always fix this. */
            GET_SEG(pVCpu->cpum.GstCtx.tr, WHvX64RegisterTr);
            switch (pVCpu->cpum.GstCtx.tr.Attr.n.u4Type)
            {
                case X86_SEL_TYPE_SYS_386_TSS_BUSY:
                case X86_SEL_TYPE_SYS_286_TSS_BUSY:
                    break;
                case X86_SEL_TYPE_SYS_386_TSS_AVAIL:
                    pVCpu->cpum.GstCtx.tr.Attr.n.u4Type = X86_SEL_TYPE_SYS_386_TSS_BUSY;
                    break;
                case X86_SEL_TYPE_SYS_286_TSS_AVAIL:
                    pVCpu->cpum.GstCtx.tr.Attr.n.u4Type = X86_SEL_TYPE_SYS_286_TSS_BUSY;
                    break;
            }
        }
        if (fWhat & CPUMCTX_EXTRN_IDTR)
        {
            Assert(aenmNames[iReg] == WHvX64RegisterIdtr);
            pVCpu->cpum.GstCtx.idtr.cbIdt = aValues[iReg].Table.Limit;
            pVCpu->cpum.GstCtx.idtr.pIdt  = aValues[iReg].Table.Base;
            iReg++;
        }
        if (fWhat & CPUMCTX_EXTRN_GDTR)
        {
            Assert(aenmNames[iReg] == WHvX64RegisterGdtr);
            pVCpu->cpum.GstCtx.gdtr.cbGdt = aValues[iReg].Table.Limit;
            pVCpu->cpum.GstCtx.gdtr.pGdt  = aValues[iReg].Table.Base;
            iReg++;
        }
    }

    /* Control registers. */
    bool fMaybeChangedMode = false;
    bool fUpdateCr3        = false;
    if (fWhat & CPUMCTX_EXTRN_CR_MASK)
    {
        if (fWhat & CPUMCTX_EXTRN_CR0)
        {
            Assert(aenmNames[iReg] == WHvX64RegisterCr0);
            if (pVCpu->cpum.GstCtx.cr0 != aValues[iReg].Reg64)
            {
                CPUMSetGuestCR0(pVCpu, aValues[iReg].Reg64);
                fMaybeChangedMode = true;
            }
            iReg++;
        }
        if (fWhat & CPUMCTX_EXTRN_CR2)
            GET_REG64(pVCpu->cpum.GstCtx.cr2, WHvX64RegisterCr2);
        if (fWhat & CPUMCTX_EXTRN_CR3)
        {
            if (pVCpu->cpum.GstCtx.cr3 != aValues[iReg].Reg64)
            {
                CPUMSetGuestCR3(pVCpu, aValues[iReg].Reg64);
                fUpdateCr3 = true;
            }
            iReg++;
        }
        if (fWhat & CPUMCTX_EXTRN_CR4)
        {
            if (pVCpu->cpum.GstCtx.cr4 != aValues[iReg].Reg64)
            {
                CPUMSetGuestCR4(pVCpu, aValues[iReg].Reg64);
                fMaybeChangedMode = true;
            }
            iReg++;
        }
    }
    if (fWhat & CPUMCTX_EXTRN_APIC_TPR)
    {
        Assert(aenmNames[iReg] == WHvX64RegisterCr8);
        if (!pVCpu->CTX_SUFF(pVM)->nem.s.fLocalApicEmulation)
            PDMApicSetTpr(pVCpu, (uint8_t)aValues[iReg].Reg64 << 4);
        iReg++;
    }

    /* Debug registers. */
    if (fWhat & CPUMCTX_EXTRN_DR7)
    {
        Assert(aenmNames[iReg] == WHvX64RegisterDr7);
        if (pVCpu->cpum.GstCtx.dr[7] != aValues[iReg].Reg64)
            CPUMSetGuestDR7(pVCpu, aValues[iReg].Reg64);
        pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_DR7; /* Hack alert! Avoids asserting when processing CPUMCTX_EXTRN_DR0_DR3. */
        iReg++;
    }
    if (fWhat & CPUMCTX_EXTRN_DR0_DR3)
    {
        Assert(aenmNames[iReg] == WHvX64RegisterDr0);
        Assert(aenmNames[iReg+3] == WHvX64RegisterDr3);
        if (pVCpu->cpum.GstCtx.dr[0] != aValues[iReg].Reg64)
            CPUMSetGuestDR0(pVCpu, aValues[iReg].Reg64);
        iReg++;
        if (pVCpu->cpum.GstCtx.dr[1] != aValues[iReg].Reg64)
            CPUMSetGuestDR1(pVCpu, aValues[iReg].Reg64);
        iReg++;
        if (pVCpu->cpum.GstCtx.dr[2] != aValues[iReg].Reg64)
            CPUMSetGuestDR2(pVCpu, aValues[iReg].Reg64);
        iReg++;
        if (pVCpu->cpum.GstCtx.dr[3] != aValues[iReg].Reg64)
            CPUMSetGuestDR3(pVCpu, aValues[iReg].Reg64);
        iReg++;
    }
    if (fWhat & CPUMCTX_EXTRN_DR6)
    {
        Assert(aenmNames[iReg] == WHvX64RegisterDr6);
        if (pVCpu->cpum.GstCtx.dr[6] != aValues[iReg].Reg64)
            CPUMSetGuestDR6(pVCpu, aValues[iReg].Reg64);
        iReg++;
    }

    bool fUpdateXcr0 = false;
    uint64_t u64Xcr0 = 0;
    if (!pVM->nem.s.fXsaveSupported)
    {
        /* Floating point state. */
        if (fWhat & CPUMCTX_EXTRN_X87)
        {
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aRegs[0].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[0].au64[1], WHvX64RegisterFpMmx0);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aRegs[1].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[1].au64[1], WHvX64RegisterFpMmx1);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aRegs[2].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[2].au64[1], WHvX64RegisterFpMmx2);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aRegs[3].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[3].au64[1], WHvX64RegisterFpMmx3);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aRegs[4].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[4].au64[1], WHvX64RegisterFpMmx4);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aRegs[5].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[5].au64[1], WHvX64RegisterFpMmx5);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aRegs[6].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[6].au64[1], WHvX64RegisterFpMmx6);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aRegs[7].au64[0], pVCpu->cpum.GstCtx.XState.x87.aRegs[7].au64[1], WHvX64RegisterFpMmx7);

            Assert(aenmNames[iReg] == WHvX64RegisterFpControlStatus);
            pVCpu->cpum.GstCtx.XState.x87.FCW        = aValues[iReg].FpControlStatus.FpControl;
            pVCpu->cpum.GstCtx.XState.x87.FSW        = aValues[iReg].FpControlStatus.FpStatus;
            pVCpu->cpum.GstCtx.XState.x87.FTW        = aValues[iReg].FpControlStatus.FpTag
                                            /*| (aValues[iReg].FpControlStatus.Reserved << 8)*/;
            pVCpu->cpum.GstCtx.XState.x87.FOP        = aValues[iReg].FpControlStatus.LastFpOp;
            pVCpu->cpum.GstCtx.XState.x87.FPUIP      = (uint32_t)aValues[iReg].FpControlStatus.LastFpRip;
            pVCpu->cpum.GstCtx.XState.x87.CS         = (uint16_t)(aValues[iReg].FpControlStatus.LastFpRip >> 32);
            pVCpu->cpum.GstCtx.XState.x87.Rsrvd1     = (uint16_t)(aValues[iReg].FpControlStatus.LastFpRip >> 48);
            iReg++;
        }

        if (fWhat & (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX))
        {
            Assert(aenmNames[iReg] == WHvX64RegisterXmmControlStatus);
            if (fWhat & CPUMCTX_EXTRN_X87)
            {
                pVCpu->cpum.GstCtx.XState.x87.FPUDP  = (uint32_t)aValues[iReg].XmmControlStatus.LastFpRdp;
                pVCpu->cpum.GstCtx.XState.x87.DS     = (uint16_t)(aValues[iReg].XmmControlStatus.LastFpRdp >> 32);
                pVCpu->cpum.GstCtx.XState.x87.Rsrvd2 = (uint16_t)(aValues[iReg].XmmControlStatus.LastFpRdp >> 48);
            }
            pVCpu->cpum.GstCtx.XState.x87.MXCSR      = aValues[iReg].XmmControlStatus.XmmStatusControl;
            pVCpu->cpum.GstCtx.XState.x87.MXCSR_MASK = aValues[iReg].XmmControlStatus.XmmStatusControlMask; /** @todo ??? (Isn't this an output field?) */
            iReg++;
        }

        /* Vector state. */
        if (fWhat & CPUMCTX_EXTRN_SSE_AVX)
        {
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[ 0].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 0].uXmm.s.Hi, WHvX64RegisterXmm0);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[ 1].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 1].uXmm.s.Hi, WHvX64RegisterXmm1);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[ 2].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 2].uXmm.s.Hi, WHvX64RegisterXmm2);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[ 3].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 3].uXmm.s.Hi, WHvX64RegisterXmm3);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[ 4].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 4].uXmm.s.Hi, WHvX64RegisterXmm4);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[ 5].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 5].uXmm.s.Hi, WHvX64RegisterXmm5);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[ 6].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 6].uXmm.s.Hi, WHvX64RegisterXmm6);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[ 7].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 7].uXmm.s.Hi, WHvX64RegisterXmm7);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[ 8].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 8].uXmm.s.Hi, WHvX64RegisterXmm8);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[ 9].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[ 9].uXmm.s.Hi, WHvX64RegisterXmm9);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[10].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[10].uXmm.s.Hi, WHvX64RegisterXmm10);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[11].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[11].uXmm.s.Hi, WHvX64RegisterXmm11);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[12].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[12].uXmm.s.Hi, WHvX64RegisterXmm12);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[13].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[13].uXmm.s.Hi, WHvX64RegisterXmm13);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[14].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[14].uXmm.s.Hi, WHvX64RegisterXmm14);
            GET_REG128(pVCpu->cpum.GstCtx.XState.x87.aXMM[15].uXmm.s.Lo, pVCpu->cpum.GstCtx.XState.x87.aXMM[15].uXmm.s.Hi, WHvX64RegisterXmm15);
        }
    }
    else
    {
        if (fWhat & CPUMCTX_EXTRN_XCRx)
        {
            Assert(aenmNames[iReg] == WHvX64RegisterXCr0);
            if (pVCpu->cpum.GstCtx.aXcr[0] != aValues[iReg].Reg64)
            {
                u64Xcr0 = aValues[iReg].Reg64;
                fUpdateXcr0 = true;
            }
            iReg++;
        }

        if (fWhat & (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE))
        {
            if (WHvGetVirtualProcessorState)
                hrc = WHvGetVirtualProcessorState(pVM->nem.s.hPartition, pVCpu->idCpu, WHvVirtualProcessorStateTypeXsaveState, &pVCpu->cpum.GstCtx.XState, pVM->nem.s.cbXSaveArea, NULL);
            else
                hrc = WHvGetVirtualProcessorXsaveState(pVM->nem.s.hPartition, pVCpu->idCpu, &pVCpu->cpum.GstCtx.XState, pVM->nem.s.cbXSaveArea, NULL);
            if (FAILED(hrc))
            {
                AssertLogRelMsgFailed(("WHvGetVirtualProcessorState(%p, %u,%x,,) -> %Rhrc (Last=%#x/%u)\n",
                                       pVM->nem.s.hPartition, pVCpu->idCpu, WHvVirtualProcessorStateTypeXsaveState,
                                       hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
                return VERR_NEM_GET_REGISTERS_FAILED;
            }
            /** @todo r=aeichner Hyper-V might return the compacted form which IEM doesn't handle so far.
             *                   This isn't an issue currently as we don't support anything beyond AVX/AVX2 right now, so we can just clear this bit.
             *                   Also, Hyper-V seems to return the whole state for all extensions like AVX512 etc. (there is no way to instruct Hyper-V to disable certain
             *                   components). So we strip everything we don't support right now to be on the safe side wrt. IEM.
             */
            pVCpu->cpum.GstCtx.XState.Hdr.bmXComp  &= (XSAVE_C_X87 | XSAVE_C_SSE | XSAVE_C_YMM);
            pVCpu->cpum.GstCtx.XState.Hdr.bmXState &= (XSAVE_C_X87 | XSAVE_C_SSE | XSAVE_C_YMM);
        }
    }

    /* MSRs */
    // WHvX64RegisterTsc - don't touch
    if (fWhat & CPUMCTX_EXTRN_EFER)
    {
        Assert(aenmNames[iReg] == WHvX64RegisterEfer);
        if (aValues[iReg].Reg64 != pVCpu->cpum.GstCtx.msrEFER)
        {
            Log7(("NEM/%u: MSR EFER changed %RX64 -> %RX64\n", pVCpu->idCpu, pVCpu->cpum.GstCtx.msrEFER, aValues[iReg].Reg64));
            if ((aValues[iReg].Reg64 ^ pVCpu->cpum.GstCtx.msrEFER) & MSR_K6_EFER_NXE)
                PGMNotifyNxeChanged(pVCpu, RT_BOOL(aValues[iReg].Reg64 & MSR_K6_EFER_NXE));
            pVCpu->cpum.GstCtx.msrEFER = aValues[iReg].Reg64;
            fMaybeChangedMode = true;
        }
        iReg++;
    }
    if (fWhat & CPUMCTX_EXTRN_KERNEL_GS_BASE)
        GET_REG64_LOG7(pVCpu->cpum.GstCtx.msrKERNELGSBASE, WHvX64RegisterKernelGsBase, "MSR KERNEL_GS_BASE");
    if (fWhat & CPUMCTX_EXTRN_SYSENTER_MSRS)
    {
        GET_REG64_LOG7(pVCpu->cpum.GstCtx.SysEnter.cs,  WHvX64RegisterSysenterCs,  "MSR SYSENTER.CS");
        GET_REG64_LOG7(pVCpu->cpum.GstCtx.SysEnter.eip, WHvX64RegisterSysenterEip, "MSR SYSENTER.EIP");
        GET_REG64_LOG7(pVCpu->cpum.GstCtx.SysEnter.esp, WHvX64RegisterSysenterEsp, "MSR SYSENTER.ESP");
    }
    if (fWhat & CPUMCTX_EXTRN_SYSCALL_MSRS)
    {
        GET_REG64_LOG7(pVCpu->cpum.GstCtx.msrSTAR,   WHvX64RegisterStar,   "MSR STAR");
        GET_REG64_LOG7(pVCpu->cpum.GstCtx.msrLSTAR,  WHvX64RegisterLstar,  "MSR LSTAR");
        GET_REG64_LOG7(pVCpu->cpum.GstCtx.msrCSTAR,  WHvX64RegisterCstar,  "MSR CSTAR");
        GET_REG64_LOG7(pVCpu->cpum.GstCtx.msrSFMASK, WHvX64RegisterSfmask, "MSR SFMASK");
    }
    if (fWhat & (CPUMCTX_EXTRN_TSC_AUX | CPUMCTX_EXTRN_OTHER_MSRS))
    {
        PCPUMCTXMSRS const pCtxMsrs = CPUMQueryGuestCtxMsrsPtr(pVCpu);
        if (fWhat & CPUMCTX_EXTRN_TSC_AUX)
            GET_REG64_LOG7(pCtxMsrs->msr.TscAux, WHvX64RegisterTscAux, "MSR TSC_AUX");
        if (fWhat & CPUMCTX_EXTRN_OTHER_MSRS)
        {
            Assert(aenmNames[iReg] == WHvX64RegisterApicBase);
            const uint64_t uOldBase = PDMApicGetBaseMsrNoCheck(pVCpu);
            if (aValues[iReg].Reg64 != uOldBase)
            {
                Log7(("NEM/%u: MSR APICBase changed %RX64 -> %RX64 (%RX64)\n",
                      pVCpu->idCpu, uOldBase, aValues[iReg].Reg64, aValues[iReg].Reg64 ^ uOldBase));
                int rc2 = PDMApicSetBaseMsr(pVCpu, aValues[iReg].Reg64);
                AssertLogRelMsg(rc2 == VINF_SUCCESS, ("%Rrc %RX64\n", rc2, aValues[iReg].Reg64));
            }
            iReg++;

            GET_REG64_LOG7(pVCpu->cpum.GstCtx.msrPAT, WHvX64RegisterPat, "MSR PAT");
#if 0 /*def LOG_ENABLED*/ /** @todo something's wrong with HvX64RegisterMtrrCap? (AMD) */
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrCap, WHvX64RegisterMsrMtrrCap);
#endif
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrDefType,      WHvX64RegisterMsrMtrrDefType,     "MSR MTRR_DEF_TYPE");
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrFix64K_00000, WHvX64RegisterMsrMtrrFix64k00000, "MSR MTRR_FIX_64K_00000");
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrFix16K_80000, WHvX64RegisterMsrMtrrFix16k80000, "MSR MTRR_FIX_16K_80000");
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrFix16K_A0000, WHvX64RegisterMsrMtrrFix16kA0000, "MSR MTRR_FIX_16K_A0000");
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrFix4K_C0000,  WHvX64RegisterMsrMtrrFix4kC0000,  "MSR MTRR_FIX_4K_C0000");
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrFix4K_C8000,  WHvX64RegisterMsrMtrrFix4kC8000,  "MSR MTRR_FIX_4K_C8000");
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrFix4K_D0000,  WHvX64RegisterMsrMtrrFix4kD0000,  "MSR MTRR_FIX_4K_D0000");
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrFix4K_D8000,  WHvX64RegisterMsrMtrrFix4kD8000,  "MSR MTRR_FIX_4K_D8000");
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrFix4K_E0000,  WHvX64RegisterMsrMtrrFix4kE0000,  "MSR MTRR_FIX_4K_E0000");
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrFix4K_E8000,  WHvX64RegisterMsrMtrrFix4kE8000,  "MSR MTRR_FIX_4K_E8000");
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrFix4K_F0000,  WHvX64RegisterMsrMtrrFix4kF0000,  "MSR MTRR_FIX_4K_F0000");
            GET_REG64_LOG7(pCtxMsrs->msr.MtrrFix4K_F8000,  WHvX64RegisterMsrMtrrFix4kF8000,  "MSR MTRR_FIX_4K_F8000");
            if (pVM->nem.s.fDoIa32SpecCtrl)
                GET_REG64_LOG7(pCtxMsrs->msr.SpecCtrl,     WHvX64RegisterSpecCtrl,           "MSR IA32_SPEC_CTRL");
            /** @todo look for HvX64RegisterIa32MiscEnable and HvX64RegisterIa32FeatureControl? */
        }
    }

    /* Interruptibility. */
    if (fWhat & (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI))
    {
        Assert(aenmNames[iReg] == WHvRegisterInterruptState);
        Assert(aenmNames[iReg + 1] == WHvX64RegisterRip);

        if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_INHIBIT_INT))
            pVCpu->nem.s.fLastInterruptShadow = CPUMUpdateInterruptShadowEx(&pVCpu->cpum.GstCtx,
                                                                            aValues[iReg].InterruptState.InterruptShadow,
                                                                            aValues[iReg + 1].Reg64);

        if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_INHIBIT_NMI))
            CPUMUpdateInterruptInhibitingByNmi(&pVCpu->cpum.GstCtx, aValues[iReg].InterruptState.NmiMasked);

        fWhat |= CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI;
        iReg += 2;
    }

    /* Event injection. */
    /// @todo WHvRegisterPendingInterruption
    Assert(aenmNames[iReg] == WHvRegisterPendingInterruption);
    if (aValues[iReg].PendingInterruption.InterruptionPending)
    {
        Log7(("PendingInterruption: type=%u vector=%#x errcd=%RTbool/%#x instr-len=%u nested=%u\n",
              aValues[iReg].PendingInterruption.InterruptionType, aValues[iReg].PendingInterruption.InterruptionVector,
              aValues[iReg].PendingInterruption.DeliverErrorCode, aValues[iReg].PendingInterruption.ErrorCode,
              aValues[iReg].PendingInterruption.InstructionLength, aValues[iReg].PendingInterruption.NestedEvent));
        AssertMsg((aValues[iReg].PendingInterruption.AsUINT64 & UINT64_C(0xfc00)) == 0,
                  ("%#RX64\n", aValues[iReg].PendingInterruption.AsUINT64));
    }

    /// @todo WHvRegisterPendingEvent

    /* Almost done, just update extrn flags and maybe change PGM mode. */
    pVCpu->cpum.GstCtx.fExtrn &= ~fWhat;
    if (!(pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_ALL | (CPUMCTX_EXTRN_NEM_WIN_MASK & ~CPUMCTX_EXTRN_NEM_WIN_EVENT_INJECT))))
        pVCpu->cpum.GstCtx.fExtrn = 0;

    if (fUpdateXcr0)
    {
        int rc = CPUMSetGuestXcr0(pVCpu, u64Xcr0);
        AssertMsgReturn(rc == VINF_SUCCESS, ("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_NEM_IPE_3);
    }

    /* Typical. */
    if (!fMaybeChangedMode && !fUpdateCr3)
        return VINF_SUCCESS;

    /*
     * Slow.
     */
    if (fMaybeChangedMode)
    {
        int rc = PGMChangeMode(pVCpu, pVCpu->cpum.GstCtx.cr0, pVCpu->cpum.GstCtx.cr4, pVCpu->cpum.GstCtx.msrEFER,
                               false /* fForce */);
        AssertMsgReturn(rc == VINF_SUCCESS, ("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_NEM_IPE_1);
    }

    if (fUpdateCr3)
    {
        int rc = PGMUpdateCR3(pVCpu, pVCpu->cpum.GstCtx.cr3);
        if (rc == VINF_SUCCESS)
        { /* likely */ }
        else
            AssertMsgFailedReturn(("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_NEM_IPE_2);
    }

    return VINF_SUCCESS;
}


/**
 * Interface for importing state on demand (used by IEM).
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context CPU structure.
 * @param   fWhat       What to import, CPUMCTX_EXTRN_XXX.
 */
VMM_INT_DECL(int) NEMImportStateOnDemand(PVMCPUCC pVCpu, uint64_t fWhat)
{
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnDemand);
    return nemHCWinCopyStateFromHyperV(pVCpu->pVMR3, pVCpu, fWhat);
}


/**
 * Query the CPU tick counter and optionally the TSC_AUX MSR value.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context CPU structure.
 * @param   pcTicks     Where to return the CPU tick count.
 * @param   puAux       Where to return the TSC_AUX register value.
 */
VMM_INT_DECL(int) NEMHCQueryCpuTick(PVMCPUCC pVCpu, uint64_t *pcTicks, uint32_t *puAux)
{
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatQueryCpuTick);

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    VMCPU_ASSERT_EMT_RETURN(pVCpu, VERR_VM_THREAD_NOT_EMT);
    AssertReturn(VM_IS_NEM_ENABLED(pVM), VERR_NEM_IPE_9);

    /* Call the offical API. */
    WHV_REGISTER_NAME  aenmNames[2] = { WHvX64RegisterTsc, WHvX64RegisterTscAux };
    WHV_REGISTER_VALUE aValues[2]   = { { { { 0, 0 } } }, { { { 0, 0 } } } };
    Assert(RT_ELEMENTS(aenmNames) == RT_ELEMENTS(aValues));
    HRESULT hrc = WHvGetVirtualProcessorRegisters(pVM->nem.s.hPartition, pVCpu->idCpu, aenmNames, 2, aValues);
    AssertLogRelMsgReturn(SUCCEEDED(hrc),
                          ("WHvGetVirtualProcessorRegisters(%p, %u,{tsc,tsc_aux},2,) -> %Rhrc (Last=%#x/%u)\n",
                           pVM->nem.s.hPartition, pVCpu->idCpu, hrc, RTNtLastStatusValue(), RTNtLastErrorValue())
                          , VERR_NEM_GET_REGISTERS_FAILED);
    *pcTicks = aValues[0].Reg64;
    if (puAux)
        *puAux = pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_TSC_AUX ? aValues[1].Reg64 : CPUMGetGuestTscAux(pVCpu);
    return VINF_SUCCESS;
}


/**
 * Resumes CPU clock (TSC) on all virtual CPUs.
 *
 * This is called by TM when the VM is started, restored, resumed or similar.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context CPU structure of the calling EMT.
 * @param   uPausedTscValue The TSC value at the time of pausing.
 */
VMM_INT_DECL(int) NEMHCResumeCpuTickOnAll(PVMCC pVM, PVMCPUCC pVCpu, uint64_t uPausedTscValue)
{
    VMCPU_ASSERT_EMT_RETURN(pVCpu, VERR_VM_THREAD_NOT_EMT);
    AssertReturn(VM_IS_NEM_ENABLED(pVM), VERR_NEM_IPE_9);

    /** @todo Do this WHvSuspendPartitionTime call to when the VM is suspended. */
    HRESULT hrcSuspend = E_FAIL;
    if (WHvSuspendPartitionTime && WHvResumePartitionTime)
    {
        hrcSuspend = WHvSuspendPartitionTime(pVM->nem.s.hPartition);
        AssertLogRelMsg(SUCCEEDED(hrcSuspend),
                        ("WHvSuspendPartitionTime(%p) -> %Rhrc (Last=%#x/%u)\n",
                         pVM->nem.s.hPartition, hrcSuspend, RTNtLastStatusValue(), RTNtLastErrorValue()));
    }

    /*
     * Call the offical API to do the job.
     */
    if (pVM->cCpus > 1)
        RTThreadYield(); /* Try decrease the chance that we get rescheduled in the middle. */

    /* Start with the first CPU. */
    WHV_REGISTER_NAME  enmName   = WHvX64RegisterTsc;
    WHV_REGISTER_VALUE Value     = { { { 0, 0 } } };
    Value.Reg64 = uPausedTscValue;
    uint64_t const     uFirstTsc = ASMReadTSC();
    HRESULT hrc = WHvSetVirtualProcessorRegisters(pVM->nem.s.hPartition, 0 /*iCpu*/, &enmName, 1, &Value);
    AssertLogRelMsgReturn(SUCCEEDED(hrc),
                          ("WHvSetVirtualProcessorRegisters(%p, 0,{tsc},2,%#RX64) -> %Rhrc (Last=%#x/%u)\n",
                           pVM->nem.s.hPartition, uPausedTscValue, hrc, RTNtLastStatusValue(), RTNtLastErrorValue())
                          , VERR_NEM_SET_TSC);

    /* Do the other CPUs, adjusting for elapsed TSC and keeping finger crossed
       that we don't introduce too much drift here. */
    for (VMCPUID iCpu = 1; iCpu < pVM->cCpus; iCpu++)
    {
        Assert(enmName == WHvX64RegisterTsc);
        const uint64_t offDelta = SUCCEEDED(hrcSuspend) ? 0 : ASMReadTSC() - uFirstTsc;
        Value.Reg64 = uPausedTscValue + offDelta;
        hrc = WHvSetVirtualProcessorRegisters(pVM->nem.s.hPartition, iCpu, &enmName, 1, &Value);
        AssertLogRelMsgReturn(SUCCEEDED(hrc),
                              ("WHvSetVirtualProcessorRegisters(%p, 0,{tsc},2,%#RX64 + %#RX64) -> %Rhrc (Last=%#x/%u)\n",
                               pVM->nem.s.hPartition, iCpu, uPausedTscValue, offDelta, hrc, RTNtLastStatusValue(), RTNtLastErrorValue())
                              , VERR_NEM_SET_TSC);
    }

    if (SUCCEEDED(hrcSuspend))
    {
        hrc = WHvResumePartitionTime(pVM->nem.s.hPartition);
        AssertLogRelMsgReturn(SUCCEEDED(hrc),
                              ("WHvResumePartitionTime(%p) -> %Rhrc (Last=%#x/%u)\n",
                               pVM->nem.s.hPartition, hrc, RTNtLastStatusValue(), RTNtLastErrorValue())
                              , VERR_NEM_SET_TSC);
    }

    return VINF_SUCCESS;
}

#ifdef LOG_ENABLED

/**
 * Get the virtual processor running status.
 */
DECLINLINE(VID_PROCESSOR_STATUS) nemHCWinCpuGetRunningStatus(PVMCPUCC pVCpu)
{
    RTERRVARS Saved;
    RTErrVarsSave(&Saved);

    /*
     * This API is disabled in release builds, it seems.  On build 17101 it requires
     * the following patch to be enabled (windbg): eb vid+12180 0f 84 98 00 00 00
     */
    VID_PROCESSOR_STATUS enmCpuStatus = VidProcessorStatusUndefined;
    NTSTATUS rcNt = g_pfnVidGetVirtualProcessorRunningStatus(pVCpu->pVMR3->nem.s.hPartitionDevice, pVCpu->idCpu, &enmCpuStatus);
    AssertMsg(NT_SUCCESS(rcNt), ("rcNt=%#x\n", rcNt));

    RTErrVarsRestore(&Saved);
    return enmCpuStatus;
}


/**
 * Logs the current CPU state.
 */
static void nemHCWinLogState(PVMCC pVM, PVMCPUCC pVCpu)
{
    if (LogIs3Enabled())
    {
# if 0 // def IN_RING3 - causes lazy state import assertions all over CPUM.
        char szRegs[4096];
        DBGFR3RegPrintf(pVM->pUVM, pVCpu->idCpu, &szRegs[0], sizeof(szRegs),
                        "rax=%016VR{rax} rbx=%016VR{rbx} rcx=%016VR{rcx} rdx=%016VR{rdx}\n"
                        "rsi=%016VR{rsi} rdi=%016VR{rdi} r8 =%016VR{r8} r9 =%016VR{r9}\n"
                        "r10=%016VR{r10} r11=%016VR{r11} r12=%016VR{r12} r13=%016VR{r13}\n"
                        "r14=%016VR{r14} r15=%016VR{r15} %VRF{rflags}\n"
                        "rip=%016VR{rip} rsp=%016VR{rsp} rbp=%016VR{rbp}\n"
                        "cs={%04VR{cs} base=%016VR{cs_base} limit=%08VR{cs_lim} flags=%04VR{cs_attr}} cr0=%016VR{cr0}\n"
                        "ds={%04VR{ds} base=%016VR{ds_base} limit=%08VR{ds_lim} flags=%04VR{ds_attr}} cr2=%016VR{cr2}\n"
                        "es={%04VR{es} base=%016VR{es_base} limit=%08VR{es_lim} flags=%04VR{es_attr}} cr3=%016VR{cr3}\n"
                        "fs={%04VR{fs} base=%016VR{fs_base} limit=%08VR{fs_lim} flags=%04VR{fs_attr}} cr4=%016VR{cr4}\n"
                        "gs={%04VR{gs} base=%016VR{gs_base} limit=%08VR{gs_lim} flags=%04VR{gs_attr}} cr8=%016VR{cr8}\n"
                        "ss={%04VR{ss} base=%016VR{ss_base} limit=%08VR{ss_lim} flags=%04VR{ss_attr}}\n"
                        "dr0=%016VR{dr0} dr1=%016VR{dr1} dr2=%016VR{dr2} dr3=%016VR{dr3}\n"
                        "dr6=%016VR{dr6} dr7=%016VR{dr7}\n"
                        "gdtr=%016VR{gdtr_base}:%04VR{gdtr_lim}  idtr=%016VR{idtr_base}:%04VR{idtr_lim}  rflags=%08VR{rflags}\n"
                        "ldtr={%04VR{ldtr} base=%016VR{ldtr_base} limit=%08VR{ldtr_lim} flags=%08VR{ldtr_attr}}\n"
                        "tr  ={%04VR{tr} base=%016VR{tr_base} limit=%08VR{tr_lim} flags=%08VR{tr_attr}}\n"
                        "    sysenter={cs=%04VR{sysenter_cs} eip=%08VR{sysenter_eip} esp=%08VR{sysenter_esp}}\n"
                        "        efer=%016VR{efer}\n"
                        "         pat=%016VR{pat}\n"
                        "     sf_mask=%016VR{sf_mask}\n"
                        "krnl_gs_base=%016VR{krnl_gs_base}\n"
                        "       lstar=%016VR{lstar}\n"
                        "        star=%016VR{star} cstar=%016VR{cstar}\n"
                        "fcw=%04VR{fcw} fsw=%04VR{fsw} ftw=%04VR{ftw} mxcsr=%04VR{mxcsr} mxcsr_mask=%04VR{mxcsr_mask}\n"
                        );

        char szInstr[256];
        DBGFR3DisasInstrEx(pVM->pUVM, pVCpu->idCpu, 0, 0,
                           DBGF_DISAS_FLAGS_CURRENT_GUEST | DBGF_DISAS_FLAGS_DEFAULT_MODE,
                           szInstr, sizeof(szInstr), NULL);
        Log3(("%s%s\n", szRegs, szInstr));
# else
        /** @todo stat logging in ring-0 */
        RT_NOREF(pVM, pVCpu);
# endif
    }
}

#endif /* LOG_ENABLED */

/**
 * Translates the execution stat bitfield into a short log string, WinHv version.
 *
 * @returns Read-only log string.
 * @param   pExitCtx        The exit context which state to summarize.
 */
static const char *nemR3WinExecStateToLogStr(WHV_VP_EXIT_CONTEXT const *pExitCtx)
{
    unsigned u = (unsigned)pExitCtx->ExecutionState.InterruptionPending
               | ((unsigned)pExitCtx->ExecutionState.DebugActive << 1)
               | ((unsigned)pExitCtx->ExecutionState.InterruptShadow << 2);
#define SWITCH_IT(a_szPrefix) \
        do \
            switch (u)\
            { \
                case 0x00: return a_szPrefix ""; \
                case 0x01: return a_szPrefix ",Pnd"; \
                case 0x02: return a_szPrefix ",Dbg"; \
                case 0x03: return a_szPrefix ",Pnd,Dbg"; \
                case 0x04: return a_szPrefix ",Shw"; \
                case 0x05: return a_szPrefix ",Pnd,Shw"; \
                case 0x06: return a_szPrefix ",Shw,Dbg"; \
                case 0x07: return a_szPrefix ",Pnd,Shw,Dbg"; \
                default: AssertFailedReturn("WTF?"); \
            } \
        while (0)
    if (pExitCtx->ExecutionState.EferLma)
        SWITCH_IT("LM");
    else if (pExitCtx->ExecutionState.Cr0Pe)
        SWITCH_IT("PM");
    else
        SWITCH_IT("RM");
#undef SWITCH_IT
}


/**
 * Advances the guest RIP and clear EFLAGS.RF, WinHv version.
 *
 * This may clear VMCPU_FF_INHIBIT_INTERRUPTS.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pExitCtx        The exit context.
 * @param   cbMinInstr      The minimum instruction length, or 1 if not unknown.
 */
DECLINLINE(void) nemR3WinAdvanceGuestRipAndClearRF(PVMCPUCC pVCpu, WHV_VP_EXIT_CONTEXT const *pExitCtx, uint8_t cbMinInstr)
{
    Assert(!(pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS)));

    /* Advance the RIP. */
    Assert(pExitCtx->InstructionLength >= cbMinInstr); RT_NOREF_PV(cbMinInstr);
    pVCpu->cpum.GstCtx.rip += pExitCtx->InstructionLength;
    pVCpu->cpum.GstCtx.rflags.Bits.u1RF = 0;
    CPUMClearInterruptShadow(&pVCpu->cpum.GstCtx);
}


/**
 * State to pass between nemHCWinHandleMemoryAccess / nemR3WinWHvHandleMemoryAccess
 * and nemHCWinHandleMemoryAccessPageCheckerCallback.
 */
typedef struct NEMHCWINHMACPCCSTATE
{
    /** Input: Write access. */
    bool    fWriteAccess;
    /** Output: Set if we did something. */
    bool    fDidSomething;
    /** Output: Set it we should resume. */
    bool    fCanResume;
} NEMHCWINHMACPCCSTATE;

/**
 * @callback_method_impl{FNPGMPHYSNEMCHECKPAGE,
 *      Worker for nemR3WinHandleMemoryAccess; pvUser points to a
 *      NEMHCWINHMACPCCSTATE structure. }
 */
static DECLCALLBACK(int)
nemHCWinHandleMemoryAccessPageCheckerCallback(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, PPGMPHYSNEMPAGEINFO pInfo, void *pvUser)
{
    NEMHCWINHMACPCCSTATE *pState = (NEMHCWINHMACPCCSTATE *)pvUser;
    pState->fDidSomething = false;
    pState->fCanResume    = false;

    /* If A20 is disabled, we may need to make another query on the masked
       page to get the correct protection information. */
    uint8_t  u2State = pInfo->u2NemState;
    RTGCPHYS GCPhysSrc;
#ifdef NEM_WIN_WITH_A20
    if (   pVM->nem.s.fA20Enabled
        || !NEM_WIN_IS_SUBJECT_TO_A20(GCPhys))
#endif
        GCPhysSrc = GCPhys;
#ifdef NEM_WIN_WITH_A20
    else
    {
        GCPhysSrc = GCPhys & ~(RTGCPHYS)RT_BIT_32(20);
        PGMPHYSNEMPAGEINFO Info2;
        int rc = PGMPhysNemPageInfoChecker(pVM, pVCpu, GCPhysSrc, pState->fWriteAccess, &Info2, NULL, NULL);
        AssertRCReturn(rc, rc);

        *pInfo = Info2;
        pInfo->u2NemState = u2State;
    }
#endif

    /*
     * Consolidate current page state with actual page protection and access type.
     * We don't really consider downgrades here, as they shouldn't happen.
     */
    /** @todo Someone at microsoft please explain:
     * I'm not sure WTF was going on, but I ended up in a loop if I remapped a
     * readonly page as writable (unmap, then map again).  Specifically, this was an
     * issue with the big VRAM mapping at 0xe0000000 when booing DSL 4.4.1.  So, in
     * a hope to work around that we no longer pre-map anything, just unmap stuff
     * and do it lazily here.  And here we will first unmap, restart, and then remap
     * with new protection or backing.
     */
    int rc;
    switch (u2State)
    {
        case NEM_WIN_PAGE_STATE_UNMAPPED:
        case NEM_WIN_PAGE_STATE_NOT_SET:
            if (pInfo->fNemProt == NEM_PAGE_PROT_NONE)
            {
                Log4(("nemHCWinHandleMemoryAccessPageCheckerCallback: %RGp - #1\n", GCPhys));
                return VINF_SUCCESS;
            }

            /* Don't bother remapping it if it's a write request to a non-writable page. */
            if (   pState->fWriteAccess
                && !(pInfo->fNemProt & NEM_PAGE_PROT_WRITE))
            {
                Log4(("nemHCWinHandleMemoryAccessPageCheckerCallback: %RGp - #1w\n", GCPhys));
                return VINF_SUCCESS;
            }

            /* Map the page. */
            rc = nemHCNativeSetPhysPage(pVM,
                                        pVCpu,
                                        GCPhysSrc & ~(RTGCPHYS)X86_PAGE_OFFSET_MASK,
                                        GCPhys & ~(RTGCPHYS)X86_PAGE_OFFSET_MASK,
                                        pInfo->fNemProt,
                                        &u2State,
                                        true /*fBackingState*/);
            pInfo->u2NemState = u2State;
            Log4(("nemHCWinHandleMemoryAccessPageCheckerCallback: %RGp - synced => %s + %Rrc\n",
                  GCPhys, g_apszPageStates[u2State], rc));
            pState->fDidSomething = true;
            pState->fCanResume    = true;
            return rc;

        case NEM_WIN_PAGE_STATE_READABLE:
            if (   !(pInfo->fNemProt & NEM_PAGE_PROT_WRITE)
                && (pInfo->fNemProt & (NEM_PAGE_PROT_READ | NEM_PAGE_PROT_EXECUTE)))
            {
                Log4(("nemHCWinHandleMemoryAccessPageCheckerCallback: %RGp - #2\n", GCPhys));
                return VINF_SUCCESS;
            }

            break;

        case NEM_WIN_PAGE_STATE_WRITABLE:
            if (pInfo->fNemProt & NEM_PAGE_PROT_WRITE)
            {
                if (pInfo->u2OldNemState == NEM_WIN_PAGE_STATE_WRITABLE)
                    Log4(("nemHCWinHandleMemoryAccessPageCheckerCallback: %RGp - #3a\n", GCPhys));
                else
                {
                    pState->fCanResume = true;
                    Log4(("nemHCWinHandleMemoryAccessPageCheckerCallback: %RGp - #3b (%s -> %s)\n",
                          GCPhys, g_apszPageStates[pInfo->u2OldNemState], g_apszPageStates[u2State]));
                }
                return VINF_SUCCESS;
            }
            break;

        default:
            AssertLogRelMsgFailedReturn(("u2State=%#x\n", u2State), VERR_NEM_IPE_4);
    }

    /*
     * Unmap and restart the instruction.
     * If this fails, which it does every so often, just unmap everything for now.
     */
    /** @todo figure out whether we mess up the state or if it's WHv.   */
    STAM_REL_PROFILE_START(&pVM->nem.s.StatProfUnmapGpaRangePage, a);
    HRESULT hrc = WHvUnmapGpaRange(pVM->nem.s.hPartition, GCPhys, X86_PAGE_SIZE);
    STAM_REL_PROFILE_STOP(&pVM->nem.s.StatProfUnmapGpaRangePage, a);
    if (SUCCEEDED(hrc))
    {
        pState->fDidSomething = true;
        pState->fCanResume    = true;
        pInfo->u2NemState = NEM_WIN_PAGE_STATE_UNMAPPED;
        STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPage);
        uint32_t cMappedPages = ASMAtomicDecU32(&pVM->nem.s.cMappedPages); NOREF(cMappedPages);
        Log5(("NEM GPA unmapped/exit: %RGp (was %s, cMappedPages=%u)\n", GCPhys, g_apszPageStates[u2State], cMappedPages));
        return VINF_SUCCESS;
    }
    STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPageFailed);
    LogRel(("nemHCWinHandleMemoryAccessPageCheckerCallback/unmap: GCPhysDst=%RGp %s hrc=%Rhrc (%#x)\n",
            GCPhys, g_apszPageStates[u2State], hrc, hrc));
    return VERR_NEM_UNMAP_PAGES_FAILED;
}


/**
 * Wrapper around nemHCWinCopyStateFromHyperV.
 *
 * Unlike the wrapped APIs, this checks whether it's necessary.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context per CPU structure.
 * @param   fWhat           What to import.
 * @param   pszCaller       Who is doing the importing.
 */
DECLINLINE(VBOXSTRICTRC) nemHCWinImportStateIfNeededStrict(PVMCPUCC pVCpu, uint64_t fWhat, const char *pszCaller)
{
    if (pVCpu->cpum.GstCtx.fExtrn & fWhat)
    {
        RT_NOREF(pszCaller);
        int rc = nemHCWinCopyStateFromHyperV(pVCpu->pVMR3, pVCpu, fWhat);
        AssertRCReturn(rc, rc);
    }
    return VINF_SUCCESS;
}


/**
 * Copies register state from the (common) exit context.
 *
 * ASSUMES no state copied yet.
 *
 * @param   pVCpu           The cross context per CPU structure.
 * @param   pExitCtx        The common exit context.
 * @sa      nemHCWinCopyStateFromX64Header
 */
DECLINLINE(void) nemR3WinCopyStateFromX64Header(PVMCPUCC pVCpu, WHV_VP_EXIT_CONTEXT const *pExitCtx)
{
    AssertMsg(   (pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_INHIBIT_INT))
              ==                              (CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_INHIBIT_INT),
              ("fExtrn=%#RX64\n", pVCpu->cpum.GstCtx.fExtrn));

    NEM_WIN_COPY_BACK_SEG(pVCpu->cpum.GstCtx.cs, pExitCtx->Cs);
    pVCpu->cpum.GstCtx.rip      = pExitCtx->Rip;
    pVCpu->cpum.GstCtx.rflags.u = pExitCtx->Rflags;
    pVCpu->nem.s.fLastInterruptShadow = CPUMUpdateInterruptShadowEx(&pVCpu->cpum.GstCtx,
                                                                    pExitCtx->ExecutionState.InterruptShadow,
                                                                    pExitCtx->Rip);
    if (!pVCpu->CTX_SUFF(pVM)->nem.s.fLocalApicEmulation)
        PDMApicSetTpr(pVCpu, pExitCtx->Cr8 << 4);
    else
        PDMApicImportState(pVCpu);

    pVCpu->cpum.GstCtx.fExtrn &= ~(CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_APIC_TPR);
}


/**
 * Deals with memory access exits (WHvRunVpExitReasonMemoryAccess).
 *
 * @returns Strict VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per CPU structure.
 * @param   pExit           The VM exit information to handle.
 * @sa      nemHCWinHandleMessageMemory
 */
static VBOXSTRICTRC
nemR3WinHandleExitMemory(PVMCC pVM, PVMCPUCC pVCpu, WHV_RUN_VP_EXIT_CONTEXT const *pExit)
{
    uint64_t const uHostTsc = ASMReadTSC();
    Assert(pExit->MemoryAccess.AccessInfo.AccessType != 3);

    /*
     * Whatever we do, we must clear pending event injection upon resume.
     */
    if (pExit->VpContext.ExecutionState.InterruptionPending)
        pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_NEM_WIN_EVENT_INJECT;

    /*
     * Ask PGM for information about the given GCPhys.  We need to check if we're
     * out of sync first.
     */
    NEMHCWINHMACPCCSTATE State = { pExit->MemoryAccess.AccessInfo.AccessType == WHvMemoryAccessWrite, false, false };
    PGMPHYSNEMPAGEINFO   Info;
    int rc = PGMPhysNemPageInfoChecker(pVM, pVCpu, pExit->MemoryAccess.Gpa, State.fWriteAccess, &Info,
                                       nemHCWinHandleMemoryAccessPageCheckerCallback, &State);
    if (RT_SUCCESS(rc))
    {
        if (Info.fNemProt & (  pExit->MemoryAccess.AccessInfo.AccessType == WHvMemoryAccessWrite
                             ? NEM_PAGE_PROT_WRITE : NEM_PAGE_PROT_READ))
        {
            if (State.fCanResume)
            {
                Log4(("MemExit/%u: %04x:%08RX64/%s: %RGp (=>%RHp) %s fProt=%u%s%s%s; restarting (%s)\n",
                      pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
                      pExit->MemoryAccess.Gpa, Info.HCPhys, g_apszPageStates[Info.u2NemState], Info.fNemProt,
                      Info.fHasHandlers ? " handlers" : "", Info.fZeroPage    ? " zero-pg" : "",
                      State.fDidSomething ? "" : " no-change", g_apszHvInterceptAccessTypes[pExit->MemoryAccess.AccessInfo.AccessType]));
                EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_MEMORY_ACCESS),
                                 pExit->VpContext.Rip + pExit->VpContext.Cs.Base, uHostTsc);
                return VINF_SUCCESS;
            }
        }
        Log4(("MemExit/%u: %04x:%08RX64/%s: %RGp (=>%RHp) %s fProt=%u%s%s%s; emulating (%s)\n",
              pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
              pExit->MemoryAccess.Gpa, Info.HCPhys, g_apszPageStates[Info.u2NemState], Info.fNemProt,
              Info.fHasHandlers ? " handlers" : "", Info.fZeroPage    ? " zero-pg" : "",
              State.fDidSomething ? "" : " no-change", g_apszHvInterceptAccessTypes[pExit->MemoryAccess.AccessInfo.AccessType]));
    }
    else
        Log4(("MemExit/%u: %04x:%08RX64/%s: %RGp rc=%Rrc%s; emulating (%s)\n",
              pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
              pExit->MemoryAccess.Gpa, rc, State.fDidSomething ? " modified-backing" : "",
              g_apszHvInterceptAccessTypes[pExit->MemoryAccess.AccessInfo.AccessType]));

    /*
     * Emulate the memory access, either access handler or special memory.
     */
    PCEMEXITREC pExitRec = EMHistoryAddExit(pVCpu,
                                              pExit->MemoryAccess.AccessInfo.AccessType == WHvMemoryAccessWrite
                                            ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_WRITE)
                                            : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_READ),
                                            pExit->VpContext.Rip + pExit->VpContext.Cs.Base, uHostTsc);
    nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);
    rc = nemHCWinCopyStateFromHyperV(pVM, pVCpu, NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM | CPUMCTX_EXTRN_DS | CPUMCTX_EXTRN_ES);
    AssertRCReturn(rc, rc);
    if (pExit->VpContext.ExecutionState.Reserved0 || pExit->VpContext.ExecutionState.Reserved1)
        Log(("MemExit/Hdr/State: Reserved0=%#x Reserved1=%#x\n", pExit->VpContext.ExecutionState.Reserved0, pExit->VpContext.ExecutionState.Reserved1));

    VBOXSTRICTRC rcStrict;
    if (!pExitRec)
    {
        //if (pMsg->InstructionByteCount > 0)
        //    Log4(("InstructionByteCount=%#x %.16Rhxs\n", pMsg->InstructionByteCount, pMsg->InstructionBytes));
        IEMTlbInvalidateAll(pVCpu);
        if (pExit->MemoryAccess.InstructionByteCount > 0)
            rcStrict = IEMExecOneWithPrefetchedByPC(pVCpu, pExit->VpContext.Rip, pExit->MemoryAccess.InstructionBytes, pExit->MemoryAccess.InstructionByteCount);
        else
            rcStrict = IEMExecOne(pVCpu);
        /** @todo do we need to do anything wrt debugging here?   */
    }
    else
    {
        /* Frequent access or probing. */
        rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);
        Log4(("MemExit/%u: %04x:%08RX64/%s: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
              pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
              VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    }
    return rcStrict;
}


/**
 * Deals with I/O port access exits (WHvRunVpExitReasonX64IoPortAccess).
 *
 * @returns Strict VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per CPU structure.
 * @param   pExit           The VM exit information to handle.
 * @sa      nemHCWinHandleMessageIoPort
 */
static VBOXSTRICTRC nemR3WinHandleExitIoPort(PVMCC pVM, PVMCPUCC pVCpu, WHV_RUN_VP_EXIT_CONTEXT const *pExit)
{
    Assert(   pExit->IoPortAccess.AccessInfo.AccessSize == 1
           || pExit->IoPortAccess.AccessInfo.AccessSize == 2
           || pExit->IoPortAccess.AccessInfo.AccessSize == 4);

    nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);

    /*
     * Whatever we do, we must clear pending event injection upon resume.
     */
    if (pExit->VpContext.ExecutionState.InterruptionPending)
        pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_NEM_WIN_EVENT_INJECT;

    /*
     * Add history first to avoid two paths doing EMHistoryExec calls.
     */
    PCEMEXITREC pExitRec = EMHistoryAddExit(pVCpu,
                                            !pExit->IoPortAccess.AccessInfo.StringOp
                                            ? (  pExit->MemoryAccess.AccessInfo.AccessType == WHvMemoryAccessWrite
                                               ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_X86_PIO_WRITE)
                                               : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_X86_PIO_READ))
                                            : (  pExit->MemoryAccess.AccessInfo.AccessType == WHvMemoryAccessWrite
                                               ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_X86_PIO_STR_WRITE)
                                               : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_X86_PIO_STR_READ)),
                                            pExit->VpContext.Rip + pExit->VpContext.Cs.Base, ASMReadTSC());
    if (!pExitRec)
    {
        VBOXSTRICTRC rcStrict;
        if (!pExit->IoPortAccess.AccessInfo.StringOp)
        {
            /*
             * Simple port I/O.
             */
            static uint32_t const s_fAndMask[8] =
            {   UINT32_MAX, UINT32_C(0xff), UINT32_C(0xffff), UINT32_MAX,   UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX   };
            uint32_t const        fAndMask      = s_fAndMask[pExit->IoPortAccess.AccessInfo.AccessSize];
            if (pExit->IoPortAccess.AccessInfo.IsWrite)
            {
                rcStrict = IOMIOPortWrite(pVM, pVCpu, pExit->IoPortAccess.PortNumber,
                                          (uint32_t)pExit->IoPortAccess.Rax & fAndMask,
                                          pExit->IoPortAccess.AccessInfo.AccessSize);
                Log4(("IOExit/%u: %04x:%08RX64/%s: OUT %#x, %#x LB %u rcStrict=%Rrc\n",
                      pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
                      pExit->IoPortAccess.PortNumber, (uint32_t)pExit->IoPortAccess.Rax & fAndMask,
                      pExit->IoPortAccess.AccessInfo.AccessSize, VBOXSTRICTRC_VAL(rcStrict) ));
                if (IOM_SUCCESS(rcStrict))
                    nemR3WinAdvanceGuestRipAndClearRF(pVCpu, &pExit->VpContext, 1);
            }
            else
            {
                uint32_t uValue = 0;
                rcStrict = IOMIOPortRead(pVM, pVCpu, pExit->IoPortAccess.PortNumber, &uValue,
                                         pExit->IoPortAccess.AccessInfo.AccessSize);
                Log4(("IOExit/%u: %04x:%08RX64/%s: IN %#x LB %u -> %#x, rcStrict=%Rrc\n",
                      pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
                      pExit->IoPortAccess.PortNumber, pExit->IoPortAccess.AccessInfo.AccessSize, uValue, VBOXSTRICTRC_VAL(rcStrict) ));
                if (IOM_SUCCESS(rcStrict))
                {
                    if (pExit->IoPortAccess.AccessInfo.AccessSize != 4)
                        pVCpu->cpum.GstCtx.rax = (pExit->IoPortAccess.Rax & ~(uint64_t)fAndMask) | (uValue & fAndMask);
                    else
                        pVCpu->cpum.GstCtx.rax = uValue;
                    pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_RAX;
                    Log4(("IOExit/%u: RAX %#RX64 -> %#RX64\n", pVCpu->idCpu, pExit->IoPortAccess.Rax, pVCpu->cpum.GstCtx.rax));
                    nemR3WinAdvanceGuestRipAndClearRF(pVCpu, &pExit->VpContext, 1);
                }
            }
        }
        else
        {
            /*
             * String port I/O.
             */
            /** @todo Someone at Microsoft please explain how we can get the address mode
             * from the IoPortAccess.VpContext.  CS.Attributes is only sufficient for
             * getting the default mode, it can always be overridden by a prefix.   This
             * forces us to interpret the instruction from opcodes, which is suboptimal.
             * Both AMD-V and VT-x includes the address size in the exit info, at least on
             * CPUs that are reasonably new.
             *
             * Of course, it's possible this is an undocumented and we just need to do some
             * experiments to figure out how it's communicated.  Alternatively, we can scan
             * the opcode bytes for possible evil prefixes.
             */
            pVCpu->cpum.GstCtx.fExtrn &= ~(  CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RCX | CPUMCTX_EXTRN_RDI | CPUMCTX_EXTRN_RSI
                                           | CPUMCTX_EXTRN_DS  | CPUMCTX_EXTRN_ES);
            NEM_WIN_COPY_BACK_SEG(pVCpu->cpum.GstCtx.ds, pExit->IoPortAccess.Ds);
            NEM_WIN_COPY_BACK_SEG(pVCpu->cpum.GstCtx.es, pExit->IoPortAccess.Es);
            pVCpu->cpum.GstCtx.rax = pExit->IoPortAccess.Rax;
            pVCpu->cpum.GstCtx.rcx = pExit->IoPortAccess.Rcx;
            pVCpu->cpum.GstCtx.rdi = pExit->IoPortAccess.Rdi;
            pVCpu->cpum.GstCtx.rsi = pExit->IoPortAccess.Rsi;
            int rc = nemHCWinCopyStateFromHyperV(pVM, pVCpu, NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM);
            AssertRCReturn(rc, rc);

            Log4(("IOExit/%u: %04x:%08RX64/%s: %s%s %#x LB %u (emulating)\n",
                  pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
                  pExit->IoPortAccess.AccessInfo.RepPrefix ? "REP " : "",
                  pExit->IoPortAccess.AccessInfo.IsWrite ? "OUTS" : "INS",
                  pExit->IoPortAccess.PortNumber, pExit->IoPortAccess.AccessInfo.AccessSize ));
            IEMTlbInvalidateAll(pVCpu);
            rcStrict = IEMExecOne(pVCpu);
        }
        if (IOM_SUCCESS(rcStrict))
        {
            /*
             * Do debug checks.
             */
            if (   pExit->VpContext.ExecutionState.DebugActive /** @todo Microsoft: Does DebugActive this only reflect DR7? */
                || (pExit->VpContext.Rflags & X86_EFL_TF)
                || DBGFBpIsHwIoArmed(pVM) )
            {
                /** @todo Debugging. */
            }
        }
        return rcStrict;
    }

    /*
     * Frequent exit or something needing probing.
     * Get state and call EMHistoryExec.
     */
    if (!pExit->IoPortAccess.AccessInfo.StringOp)
        pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_RAX;
    else
    {
        pVCpu->cpum.GstCtx.fExtrn &= ~(  CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RCX | CPUMCTX_EXTRN_RDI | CPUMCTX_EXTRN_RSI
                          | CPUMCTX_EXTRN_DS  | CPUMCTX_EXTRN_ES);
        NEM_WIN_COPY_BACK_SEG(pVCpu->cpum.GstCtx.ds, pExit->IoPortAccess.Ds);
        NEM_WIN_COPY_BACK_SEG(pVCpu->cpum.GstCtx.es, pExit->IoPortAccess.Es);
        pVCpu->cpum.GstCtx.rcx = pExit->IoPortAccess.Rcx;
        pVCpu->cpum.GstCtx.rdi = pExit->IoPortAccess.Rdi;
        pVCpu->cpum.GstCtx.rsi = pExit->IoPortAccess.Rsi;
    }
    pVCpu->cpum.GstCtx.rax = pExit->IoPortAccess.Rax;
    int rc = nemHCWinCopyStateFromHyperV(pVM, pVCpu, NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM);
    AssertRCReturn(rc, rc);
    Log4(("IOExit/%u: %04x:%08RX64/%s: %s%s%s %#x LB %u -> EMHistoryExec\n",
          pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
          pExit->IoPortAccess.AccessInfo.RepPrefix ? "REP " : "",
          pExit->IoPortAccess.AccessInfo.IsWrite ? "OUT" : "IN",
          pExit->IoPortAccess.AccessInfo.StringOp ? "S" : "",
          pExit->IoPortAccess.PortNumber, pExit->IoPortAccess.AccessInfo.AccessSize));
    VBOXSTRICTRC rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);
    Log4(("IOExit/%u: %04x:%08RX64/%s: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
          pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
          VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    return rcStrict;
}


/**
 * Deals with interrupt window exits (WHvRunVpExitReasonX64InterruptWindow).
 *
 * @returns Strict VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per CPU structure.
 * @param   pExit           The VM exit information to handle.
 * @sa      nemHCWinHandleMessageInterruptWindow
 */
static VBOXSTRICTRC nemR3WinHandleExitInterruptWindow(PVMCC pVM, PVMCPUCC pVCpu, WHV_RUN_VP_EXIT_CONTEXT const *pExit)
{
    /*
     * Assert message sanity.
     */
    AssertMsg(   pExit->InterruptWindow.DeliverableType == WHvX64PendingInterrupt
              || pExit->InterruptWindow.DeliverableType == WHvX64PendingNmi,
              ("%#x\n", pExit->InterruptWindow.DeliverableType));

    /*
     * Just copy the state we've got and handle it in the loop for now.
     */
    EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_INTTERRUPT_WINDOW),
                     pExit->VpContext.Rip + pExit->VpContext.Cs.Base, ASMReadTSC());

    nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);
    Log4(("IntWinExit/%u: %04x:%08RX64/%s: %u IF=%d InterruptShadow=%d CR8=%#x\n",
          pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip,  nemR3WinExecStateToLogStr(&pExit->VpContext),
          pExit->InterruptWindow.DeliverableType, RT_BOOL(pExit->VpContext.Rflags & X86_EFL_IF),
          pExit->VpContext.ExecutionState.InterruptShadow, pExit->VpContext.Cr8));

    pVCpu->nem.s.fIrqWindowRegistered  = false;
    pVCpu->nem.s.fPicReadyForInterrupt = true;

    /** @todo call nemHCWinHandleInterruptFF   */
    RT_NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * Deals with CPUID exits (WHvRunVpExitReasonX64Cpuid).
 *
 * @returns Strict VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per CPU structure.
 * @param   pExit           The VM exit information to handle.
 * @sa      nemHCWinHandleMessageCpuId
 */
static VBOXSTRICTRC
nemR3WinHandleExitCpuId(PVMCC pVM, PVMCPUCC pVCpu, WHV_RUN_VP_EXIT_CONTEXT const *pExit)
{
    PCEMEXITREC pExitRec = EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_X86_CPUID),
                                            pExit->VpContext.Rip + pExit->VpContext.Cs.Base, ASMReadTSC());
    if (!pExitRec)
    {
        /*
         * Soak up state and execute the instruction.
         */
        nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);
        VBOXSTRICTRC rcStrict = nemHCWinImportStateIfNeededStrict(pVCpu,
                                                                    IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK
                                                                  | CPUMCTX_EXTRN_CR3, /* May call PGMChangeMode() requiring cr3 (due to cr0 being imported). */
                                                                  "CPUID");
        if (rcStrict == VINF_SUCCESS)
        {
            /* Copy in the low register values (top is always cleared). */
            pVCpu->cpum.GstCtx.rax = (uint32_t)pExit->CpuidAccess.Rax;
            pVCpu->cpum.GstCtx.rcx = (uint32_t)pExit->CpuidAccess.Rcx;
            pVCpu->cpum.GstCtx.rdx = (uint32_t)pExit->CpuidAccess.Rdx;
            pVCpu->cpum.GstCtx.rbx = (uint32_t)pExit->CpuidAccess.Rbx;
            pVCpu->cpum.GstCtx.fExtrn &= ~(CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RCX | CPUMCTX_EXTRN_RDX | CPUMCTX_EXTRN_RBX);

            /* Execute the decoded instruction. */
            rcStrict = IEMExecDecodedCpuid(pVCpu, pExit->VpContext.InstructionLength);

            Log4(("CpuIdExit/%u: %04x:%08RX64/%s: rax=%08RX64 / rcx=%08RX64 / rdx=%08RX64 / rbx=%08RX64 -> %08RX32 / %08RX32 / %08RX32 / %08RX32 (hv: %08RX64 / %08RX64 / %08RX64 / %08RX64)\n",
                  pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
                  pExit->CpuidAccess.Rax,                           pExit->CpuidAccess.Rcx,              pExit->CpuidAccess.Rdx,              pExit->CpuidAccess.Rbx,
                  pVCpu->cpum.GstCtx.eax,                           pVCpu->cpum.GstCtx.ecx,              pVCpu->cpum.GstCtx.edx,              pVCpu->cpum.GstCtx.ebx,
                  pExit->CpuidAccess.DefaultResultRax, pExit->CpuidAccess.DefaultResultRcx, pExit->CpuidAccess.DefaultResultRdx, pExit->CpuidAccess.DefaultResultRbx));
        }

        RT_NOREF_PV(pVM);
        return rcStrict;
    }

    /*
     * Frequent exit or something needing probing.
     * Get state and call EMHistoryExec.
     */
    nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);
    pVCpu->cpum.GstCtx.rax = pExit->CpuidAccess.Rax;
    pVCpu->cpum.GstCtx.rcx = pExit->CpuidAccess.Rcx;
    pVCpu->cpum.GstCtx.rdx = pExit->CpuidAccess.Rdx;
    pVCpu->cpum.GstCtx.rbx = pExit->CpuidAccess.Rbx;
    pVCpu->cpum.GstCtx.fExtrn &= ~(CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RCX | CPUMCTX_EXTRN_RDX | CPUMCTX_EXTRN_RBX);
    Log4(("CpuIdExit/%u: %04x:%08RX64/%s: rax=%08RX64 / rcx=%08RX64 / rdx=%08RX64 / rbx=%08RX64 (hv: %08RX64 / %08RX64 / %08RX64 / %08RX64) ==> EMHistoryExec\n",
          pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
          pExit->CpuidAccess.Rax,                           pExit->CpuidAccess.Rcx,              pExit->CpuidAccess.Rdx,              pExit->CpuidAccess.Rbx,
          pExit->CpuidAccess.DefaultResultRax, pExit->CpuidAccess.DefaultResultRcx, pExit->CpuidAccess.DefaultResultRdx, pExit->CpuidAccess.DefaultResultRbx));
    int rc = nemHCWinCopyStateFromHyperV(pVM, pVCpu, NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM);
    AssertRCReturn(rc, rc);
    VBOXSTRICTRC rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);
    Log4(("CpuIdExit/%u: %04x:%08RX64/%s: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
          pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
          VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    return rcStrict;
}


/**
 * Deals with MSR access exits (WHvRunVpExitReasonX64MsrAccess).
 *
 * @returns Strict VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per CPU structure.
 * @param   pExit           The VM exit information to handle.
 * @sa      nemHCWinHandleMessageMsr
 */
static VBOXSTRICTRC nemR3WinHandleExitMsr(PVMCC pVM, PVMCPUCC pVCpu, WHV_RUN_VP_EXIT_CONTEXT const *pExit)
{
    /*
     * Check CPL as that's common to both RDMSR and WRMSR.
     */
    VBOXSTRICTRC rcStrict;
    if (pExit->VpContext.ExecutionState.Cpl == 0)
    {
        /*
         * Get all the MSR state.  Since we're getting EFER, we also need to
         * get CR0, CR4 and CR3.
         */
        PCEMEXITREC pExitRec = EMHistoryAddExit(pVCpu,
                                                  pExit->MsrAccess.AccessInfo.IsWrite
                                                ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_X86_MSR_WRITE)
                                                : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_X86_MSR_READ),
                                                pExit->VpContext.Rip + pExit->VpContext.Cs.Base, ASMReadTSC());
        nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);
        rcStrict = nemHCWinImportStateIfNeededStrict(pVCpu,
                                                     (!pExitRec ? 0 : IEM_CPUMCTX_EXTRN_MUST_MASK)
                                                     | CPUMCTX_EXTRN_ALL_MSRS | CPUMCTX_EXTRN_CR0
                                                     | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4,
                                                     "MSRs");
        if (rcStrict == VINF_SUCCESS)
        {
            if (!pExitRec)
            {
                /*
                 * Handle writes.
                 */
                if (pExit->MsrAccess.AccessInfo.IsWrite)
                {
                    rcStrict = CPUMSetGuestMsr(pVCpu, pExit->MsrAccess.MsrNumber,
                                               RT_MAKE_U64((uint32_t)pExit->MsrAccess.Rax, (uint32_t)pExit->MsrAccess.Rdx));
                    Log4(("MsrExit/%u: %04x:%08RX64/%s: WRMSR %08x, %08x:%08x -> %Rrc\n", pVCpu->idCpu, pExit->VpContext.Cs.Selector,
                          pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->MsrAccess.MsrNumber,
                          (uint32_t)pExit->MsrAccess.Rax, (uint32_t)pExit->MsrAccess.Rdx, VBOXSTRICTRC_VAL(rcStrict) ));
                    if (rcStrict == VINF_SUCCESS)
                    {
                        nemR3WinAdvanceGuestRipAndClearRF(pVCpu, &pExit->VpContext, 2);
                        return VINF_SUCCESS;
                    }
                    LogRel(("MsrExit/%u: %04x:%08RX64/%s: WRMSR %08x, %08x:%08x -> %Rrc!\n", pVCpu->idCpu,
                            pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
                            pExit->MsrAccess.MsrNumber, (uint32_t)pExit->MsrAccess.Rax, (uint32_t)pExit->MsrAccess.Rdx,
                            VBOXSTRICTRC_VAL(rcStrict) ));
                }
                /*
                 * Handle reads.
                 */
                else
                {
                    uint64_t uValue = 0;
                    rcStrict = CPUMQueryGuestMsr(pVCpu, pExit->MsrAccess.MsrNumber, &uValue);
                    Log4(("MsrExit/%u: %04x:%08RX64/%s: RDMSR %08x -> %08RX64 / %Rrc\n", pVCpu->idCpu,
                          pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
                          pExit->MsrAccess.MsrNumber, uValue, VBOXSTRICTRC_VAL(rcStrict) ));
                    if (rcStrict == VINF_SUCCESS)
                    {
                        pVCpu->cpum.GstCtx.rax = (uint32_t)uValue;
                        pVCpu->cpum.GstCtx.rdx = uValue >> 32;
                        pVCpu->cpum.GstCtx.fExtrn &= ~(CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RDX);
                        nemR3WinAdvanceGuestRipAndClearRF(pVCpu, &pExit->VpContext, 2);
                        return VINF_SUCCESS;
                    }
                    LogRel(("MsrExit/%u: %04x:%08RX64/%s: RDMSR %08x -> %08RX64 / %Rrc\n", pVCpu->idCpu, pExit->VpContext.Cs.Selector,
                            pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->MsrAccess.MsrNumber,
                            uValue, VBOXSTRICTRC_VAL(rcStrict) ));
                }
            }
            else
            {
                /*
                 * Handle frequent exit or something needing probing.
                 */
                Log4(("MsrExit/%u: %04x:%08RX64/%s: %sMSR %#08x\n",
                      pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
                      pExit->MsrAccess.AccessInfo.IsWrite ? "WR" : "RD", pExit->MsrAccess.MsrNumber));
                rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);
                Log4(("MsrExit/%u: %04x:%08RX64/%s: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
                      pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
                      VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
                return rcStrict;
            }
        }
        else
        {
            LogRel(("MsrExit/%u: %04x:%08RX64/%s: %sMSR %08x -> %Rrc - msr state import\n",
                    pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
                    pExit->MsrAccess.AccessInfo.IsWrite ? "WR" : "RD", pExit->MsrAccess.MsrNumber, VBOXSTRICTRC_VAL(rcStrict) ));
            return rcStrict;
        }
    }
    else if (pExit->MsrAccess.AccessInfo.IsWrite)
        Log4(("MsrExit/%u: %04x:%08RX64/%s: CPL %u -> #GP(0); WRMSR %08x, %08x:%08x\n", pVCpu->idCpu, pExit->VpContext.Cs.Selector,
              pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->VpContext.ExecutionState.Cpl,
              pExit->MsrAccess.MsrNumber, (uint32_t)pExit->MsrAccess.Rax, (uint32_t)pExit->MsrAccess.Rdx ));
    else
        Log4(("MsrExit/%u: %04x:%08RX64/%s: CPL %u -> #GP(0); RDMSR %08x\n", pVCpu->idCpu, pExit->VpContext.Cs.Selector,
              pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->VpContext.ExecutionState.Cpl,
              pExit->MsrAccess.MsrNumber));

    /*
     * If we get down here, we're supposed to #GP(0).
     */
    rcStrict = nemHCWinImportStateIfNeededStrict(pVCpu, NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM | CPUMCTX_EXTRN_ALL_MSRS, "MSR");
    if (rcStrict == VINF_SUCCESS)
    {
        IEMTlbInvalidateAll(pVCpu);
        rcStrict = IEMInjectTrap(pVCpu, X86_XCPT_GP, TRPM_TRAP, 0, 0, 0);
        if (rcStrict == VINF_IEM_RAISED_XCPT)
            rcStrict = VINF_SUCCESS;
        else if (rcStrict != VINF_SUCCESS)
            Log4(("MsrExit/%u: Injecting #GP(0) failed: %Rrc\n", VBOXSTRICTRC_VAL(rcStrict) ));
    }

    RT_NOREF_PV(pVM);
    return rcStrict;
}


/**
 * Worker for nemHCWinHandleMessageException & nemR3WinHandleExitException that
 * checks if the given opcodes are of interest at all.
 *
 * @returns true if interesting, false if not.
 * @param   cbOpcodes           Number of opcode bytes available.
 * @param   pbOpcodes           The opcode bytes.
 * @param   f64BitMode          Whether we're in 64-bit mode.
 */
DECLINLINE(bool) nemHcWinIsInterestingUndefinedOpcode(uint8_t cbOpcodes, uint8_t const *pbOpcodes, bool f64BitMode)
{
    /*
     * Currently only interested in VMCALL and VMMCALL.
     */
    while (cbOpcodes >= 3)
    {
        switch (pbOpcodes[0])
        {
            case 0x0f:
                switch (pbOpcodes[1])
                {
                    case 0x01:
                        switch (pbOpcodes[2])
                        {
                            case 0xc1: /* 0f 01 c1  VMCALL */
                                return true;
                            case 0xd9: /* 0f 01 d9  VMMCALL */
                                return true;
                            default:
                                break;
                        }
                        break;
                }
                break;

            default:
                return false;

            /* prefixes */
            case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
            case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
                if (!f64BitMode)
                    return false;
                RT_FALL_THRU();
            case X86_OP_PRF_CS:
            case X86_OP_PRF_SS:
            case X86_OP_PRF_DS:
            case X86_OP_PRF_ES:
            case X86_OP_PRF_FS:
            case X86_OP_PRF_GS:
            case X86_OP_PRF_SIZE_OP:
            case X86_OP_PRF_SIZE_ADDR:
            case X86_OP_PRF_LOCK:
            case X86_OP_PRF_REPZ:
            case X86_OP_PRF_REPNZ:
                cbOpcodes--;
                pbOpcodes++;
                continue;
        }
        break;
    }
    return false;
}


/**
 * Copies state included in a exception intercept exit.
 *
 * @param   pVCpu           The cross context per CPU structure.
 * @param   pExit           The VM exit information.
 * @param   fClearXcpt      Clear pending exception.
 */
DECLINLINE(void) nemR3WinCopyStateFromExceptionMessage(PVMCPUCC pVCpu, WHV_RUN_VP_EXIT_CONTEXT const *pExit, bool fClearXcpt)
{
    nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);
    if (fClearXcpt)
        pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_NEM_WIN_EVENT_INJECT;
}


/**
 * Advances the guest RIP by the number of bytes specified in @a cb.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cb          RIP increment value in bytes.
 */
DECLINLINE(void) nemHcWinAdvanceRip(PVMCPUCC pVCpu, uint32_t cb)
{
    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    pCtx->rip += cb;
    /** @todo Why not clear RF too? */
    CPUMClearInterruptShadow(&pVCpu->cpum.GstCtx);
}


/**
 * Hacks its way around the lovely mesa driver's backdoor accesses.
 *
 * @sa hmR0VmxHandleMesaDrvGp
 * @sa hmR0SvmHandleMesaDrvGp
 */
static int nemHcWinHandleMesaDrvGp(PVMCPUCC pVCpu, PCPUMCTX pCtx)
{
    Assert(!(pCtx->fExtrn & (CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_GPRS_MASK)));
    RT_NOREF(pCtx);

    /* For now we'll just skip the instruction. */
    nemHcWinAdvanceRip(pVCpu, 1);
    return VINF_SUCCESS;
}


/**
 * Checks if the \#GP'ing instruction is the mesa driver doing it's lovely
 * backdoor logging w/o checking what it is running inside.
 *
 * This recognizes an "IN EAX,DX" instruction executed in flat ring-3, with the
 * backdoor port and magic numbers loaded in registers.
 *
 * @returns true if it is, false if it isn't.
 * @sa      hmR0VmxIsMesaDrvGp
 * @sa      hmR0SvmIsMesaDrvGp
 */
DECLINLINE(bool) nemHcWinIsMesaDrvGp(PVMCPUCC pVCpu, PCPUMCTX pCtx, const uint8_t *pbInsn, uint32_t cbInsn)
{
    /* #GP(0) is already checked by caller. */

    /* Check magic and port. */
    Assert(!(pCtx->fExtrn & (CPUMCTX_EXTRN_RDX | CPUMCTX_EXTRN_RAX)));
    if (pCtx->dx != UINT32_C(0x5658))
        return false;
    if (pCtx->rax != UINT32_C(0x564d5868))
        return false;

    /* Flat ring-3 CS. */
    if (CPUMGetGuestCPL(pVCpu) != 3)
        return false;
    if (pCtx->cs.u64Base != 0)
        return false;

    /* 0xed:  IN eAX,dx */
    if (cbInsn < 1) /* Play safe (shouldn't happen). */
    {
        uint8_t abInstr[1];
        int rc = PGMPhysSimpleReadGCPtr(pVCpu, abInstr, pCtx->rip, sizeof(abInstr));
        if (RT_FAILURE(rc))
            return false;
        if (abInstr[0] != 0xed)
            return false;
    }
    else
    {
        if (pbInsn[0] != 0xed)
            return false;
    }

    return true;
}


/**
 * Deals with MSR access exits (WHvRunVpExitReasonException).
 *
 * @returns Strict VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per CPU structure.
 * @param   pExit           The VM exit information to handle.
 * @sa      nemR3WinHandleExitException
 */
static VBOXSTRICTRC nemR3WinHandleExitException(PVMCC pVM, PVMCPUCC pVCpu, WHV_RUN_VP_EXIT_CONTEXT const *pExit)
{
    /*
     * Get most of the register state since we'll end up making IEM inject the
     * event.  The exception isn't normally flaged as a pending event, so duh.
     *
     * Note! We can optimize this later with event injection.
     */
    Log4(("XcptExit/%u: %04x:%08RX64/%s: %x errcd=%#x parm=%RX64\n", pVCpu->idCpu, pExit->VpContext.Cs.Selector,
          pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->VpException.ExceptionType,
          pExit->VpException.ErrorCode, pExit->VpException.ExceptionParameter ));
    nemR3WinCopyStateFromExceptionMessage(pVCpu, pExit, true /*fClearXcpt*/);
    uint64_t fWhat = NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM;
    if (pExit->VpException.ExceptionType == X86_XCPT_DB)
        fWhat |= CPUMCTX_EXTRN_DR0_DR3 | CPUMCTX_EXTRN_DR7 | CPUMCTX_EXTRN_DR6;
    VBOXSTRICTRC rcStrict = nemHCWinImportStateIfNeededStrict(pVCpu, fWhat, "Xcpt");
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /*
     * Handle the intercept.
     */
    TRPMEVENT enmEvtType = TRPM_TRAP;
    switch (pExit->VpException.ExceptionType)
    {
        /*
         * We get undefined opcodes on VMMCALL(AMD) & VMCALL(Intel) instructions
         * and need to turn them over to GIM.
         *
         * Note! We do not check fGIMTrapXcptUD here ASSUMING that GIM only wants
         *       #UD for handling non-native hypercall instructions.  (IEM will
         *       decode both and let the GIM provider decide whether to accept it.)
         */
        case X86_XCPT_UD:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitExceptionUd);
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_XCPT_UD),
                             pExit->VpContext.Rip + pExit->VpContext.Cs.Base, ASMReadTSC());
            if (nemHcWinIsInterestingUndefinedOpcode(pExit->VpException.InstructionByteCount, pExit->VpException.InstructionBytes,
                                                     pExit->VpContext.ExecutionState.EferLma && pExit->VpContext.Cs.Long ))
            {
                IEMTlbInvalidateAll(pVCpu);
                rcStrict = IEMExecOneWithPrefetchedByPC(pVCpu, pExit->VpContext.Rip,
                                                        pExit->VpException.InstructionBytes,
                                                        pExit->VpException.InstructionByteCount);
                Log4(("XcptExit/%u: %04x:%08RX64/%s: #UD -> emulated -> %Rrc\n",
                      pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip,
                      nemR3WinExecStateToLogStr(&pExit->VpContext), VBOXSTRICTRC_VAL(rcStrict) ));
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitExceptionUdHandled);
                return rcStrict;
            }

            Log4(("XcptExit/%u: %04x:%08RX64/%s: #UD [%.*Rhxs] -> re-injected\n", pVCpu->idCpu,
                  pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext),
                  pExit->VpException.InstructionByteCount, pExit->VpException.InstructionBytes ));
            break;

        /*
         * Workaround the lovely mesa driver assuming that vmsvga means vmware
         * hypervisor and tries to log stuff to the host.
         */
        case X86_XCPT_GP:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitExceptionGp);
            /** @todo r=bird: Need workaround in IEM for this, right?
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_XCPT_GP),
                             pExit->VpContext.Rip + pExit->VpContext.Cs.Base, ASMReadTSC()); */
            if (   !pVCpu->nem.s.fTrapXcptGpForLovelyMesaDrv
                || !nemHcWinIsMesaDrvGp(pVCpu, &pVCpu->cpum.GstCtx, pExit->VpException.InstructionBytes,
                                        pExit->VpException.InstructionByteCount))
            {
#if 1 /** @todo Need to emulate instruction or we get a triple fault when trying to inject the \#GP... */
                IEMTlbInvalidateAll(pVCpu);
                rcStrict = IEMExecOneWithPrefetchedByPC(pVCpu, pExit->VpContext.Rip,
                                                        pExit->VpException.InstructionBytes,
                                                        pExit->VpException.InstructionByteCount);
                Log4(("XcptExit/%u: %04x:%08RX64/%s: #GP -> emulated -> %Rrc\n",
                      pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip,
                      nemR3WinExecStateToLogStr(&pExit->VpContext), VBOXSTRICTRC_VAL(rcStrict) ));
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitExceptionUdHandled);
                return rcStrict;
#else
                break;
#endif
            }
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitExceptionGpMesa);
            return nemHcWinHandleMesaDrvGp(pVCpu, &pVCpu->cpum.GstCtx);

        /*
         * Filter debug exceptions.
         */
        case X86_XCPT_DB:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitExceptionDb);
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_XCPT_DB),
                             pExit->VpContext.Rip + pExit->VpContext.Cs.Base, ASMReadTSC());
            Log4(("XcptExit/%u: %04x:%08RX64/%s: #DB - TODO\n",
                  pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext) ));
            break;

        case X86_XCPT_BP:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitExceptionBp);
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_XCPT_BP),
                             pExit->VpContext.Rip + pExit->VpContext.Cs.Base, ASMReadTSC());
            Log4(("XcptExit/%u: %04x:%08RX64/%s: #BP - TODO - %u\n", pVCpu->idCpu, pExit->VpContext.Cs.Selector,
                  pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->VpContext.InstructionLength));
            enmEvtType = TRPM_SOFTWARE_INT; /* We're at the INT3 instruction, not after it. */
            break;

        /* This shouldn't happen. */
        default:
            AssertLogRelMsgFailedReturn(("ExceptionType=%#x\n", pExit->VpException.ExceptionType),  VERR_IEM_IPE_6);
    }

    /*
     * Inject it.
     */
    IEMTlbInvalidateAll(pVCpu);
    rcStrict = IEMInjectTrap(pVCpu, pExit->VpException.ExceptionType, enmEvtType, pExit->VpException.ErrorCode,
                             pExit->VpException.ExceptionParameter /*??*/, pExit->VpContext.InstructionLength);
    Log4(("XcptExit/%u: %04x:%08RX64/%s: %#u -> injected -> %Rrc\n",
          pVCpu->idCpu, pExit->VpContext.Cs.Selector, pExit->VpContext.Rip,
          nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->VpException.ExceptionType, VBOXSTRICTRC_VAL(rcStrict) ));

    RT_NOREF_PV(pVM);
    return rcStrict;
}


/**
 * Deals with MSR access exits (WHvRunVpExitReasonUnrecoverableException).
 *
 * @returns Strict VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per CPU structure.
 * @param   pExit           The VM exit information to handle.
 * @sa      nemHCWinHandleMessageUnrecoverableException
 */
static VBOXSTRICTRC nemR3WinHandleExitUnrecoverableException(PVMCC pVM, PVMCPUCC pVCpu, WHV_RUN_VP_EXIT_CONTEXT const *pExit)
{
#if 0
    /*
     * Just copy the state we've got and handle it in the loop for now.
     */
    nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);
    Log(("TripleExit/%u: %04x:%08RX64/%s: RFL=%#RX64 -> VINF_EM_TRIPLE_FAULT\n", pVCpu->idCpu, pExit->VpContext.Cs.Selector,
         pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->VpContext.Rflags));
    RT_NOREF_PV(pVM);
    return VINF_EM_TRIPLE_FAULT;
#else
    /*
     * Let IEM decide whether this is really it.
     */
    EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_UNRECOVERABLE_EXCEPTION),
                     pExit->VpContext.Rip + pExit->VpContext.Cs.Base, ASMReadTSC());
    nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);
    VBOXSTRICTRC rcStrict = nemHCWinImportStateIfNeededStrict(pVCpu, NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM | CPUMCTX_EXTRN_ALL, "TripleExit");
    if (rcStrict == VINF_SUCCESS)
    {
        IEMTlbInvalidateAll(pVCpu);
        rcStrict = IEMExecOne(pVCpu);
        if (rcStrict == VINF_SUCCESS)
        {
            Log(("UnrecovExit/%u: %04x:%08RX64/%s: RFL=%#RX64 -> VINF_SUCCESS\n", pVCpu->idCpu, pExit->VpContext.Cs.Selector,
                 pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->VpContext.Rflags));
            pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_NEM_WIN_EVENT_INJECT; /* Make sure to reset pending #DB(0). */
            return VINF_SUCCESS;
        }
        if (rcStrict == VINF_EM_TRIPLE_FAULT)
            Log(("UnrecovExit/%u: %04x:%08RX64/%s: RFL=%#RX64 -> VINF_EM_TRIPLE_FAULT!\n", pVCpu->idCpu, pExit->VpContext.Cs.Selector,
                 pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->VpContext.Rflags, VBOXSTRICTRC_VAL(rcStrict) ));
        else
            Log(("UnrecovExit/%u: %04x:%08RX64/%s: RFL=%#RX64 -> %Rrc (IEMExecOne)\n", pVCpu->idCpu, pExit->VpContext.Cs.Selector,
                 pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->VpContext.Rflags, VBOXSTRICTRC_VAL(rcStrict) ));
    }
    else
        Log(("UnrecovExit/%u: %04x:%08RX64/%s: RFL=%#RX64 -> %Rrc (state import)\n", pVCpu->idCpu, pExit->VpContext.Cs.Selector,
             pExit->VpContext.Rip, nemR3WinExecStateToLogStr(&pExit->VpContext), pExit->VpContext.Rflags, VBOXSTRICTRC_VAL(rcStrict) ));
    RT_NOREF_PV(pVM);
    return rcStrict;
#endif
}


/**
 * Handles VM exits.
 *
 * @returns Strict VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per CPU structure.
 * @param   pExit           The VM exit information to handle.
 * @sa      nemHCWinHandleMessage
 */
static VBOXSTRICTRC nemR3WinHandleExit(PVMCC pVM, PVMCPUCC pVCpu, WHV_RUN_VP_EXIT_CONTEXT const *pExit)
{
    switch (pExit->ExitReason)
    {
        case WHvRunVpExitReasonMemoryAccess:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitMemUnmapped);
            return nemR3WinHandleExitMemory(pVM, pVCpu, pExit);

        case WHvRunVpExitReasonX64IoPortAccess:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitPortIo);
            return nemR3WinHandleExitIoPort(pVM, pVCpu, pExit);

        case WHvRunVpExitReasonX64Halt:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitHalt);
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_HALT),
                             pExit->VpContext.Rip + pExit->VpContext.Cs.Base, ASMReadTSC());
            nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);
            Log4(("HaltExit/%u\n", pVCpu->idCpu));
            return VINF_EM_HALT;

        case WHvRunVpExitReasonCanceled:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitCanceled);
            nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);
            return VINF_SUCCESS;

        case WHvRunVpExitReasonX64InterruptWindow:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitInterruptWindow);
            return nemR3WinHandleExitInterruptWindow(pVM, pVCpu, pExit);

        case WHvRunVpExitReasonX64Cpuid:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitCpuId);
            return nemR3WinHandleExitCpuId(pVM, pVCpu, pExit);

        case WHvRunVpExitReasonX64MsrAccess:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitMsr);
            return nemR3WinHandleExitMsr(pVM, pVCpu, pExit);

        case WHvRunVpExitReasonException:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitException);
            return nemR3WinHandleExitException(pVM, pVCpu, pExit);

        case WHvRunVpExitReasonUnrecoverableException:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitUnrecoverable);
            return nemR3WinHandleExitUnrecoverableException(pVM, pVCpu, pExit);

        case WHvRunVpExitReasonX64ApicEoi:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitApicEoi);
            Assert(pVM->nem.s.fLocalApicEmulation);
            PDMIoApicBroadcastEoi(pVCpu->CTX_SUFF(pVM), pExit->ApicEoi.InterruptVector);
            return VINF_SUCCESS;

        case WHvRunVpExitReasonUnsupportedFeature:
        case WHvRunVpExitReasonInvalidVpRegisterValue:
            LogRel(("Unimplemented exit:\n%.*Rhxd\n", (int)sizeof(*pExit), pExit));
            AssertLogRelMsgFailedReturn(("Unexpected exit on CPU #%u: %#x\n%.32Rhxd\n",
                                         pVCpu->idCpu, pExit->ExitReason, pExit), VERR_NEM_IPE_3);

        case WHvRunVpExitReasonX64ApicInitSipiTrap:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitApicSipiInitTrap);
            Assert(pVM->cCpus > 1);
            Assert(pVM->nem.s.fLocalApicEmulation);
            nemR3WinCopyStateFromX64Header(pVCpu, &pExit->VpContext);
            return PDMApicSetIcr(pVCpu, pExit->ApicInitSipi.ApicIcr);

        /* Undesired exits: */
        case WHvRunVpExitReasonNone:
        default:
            LogRel(("Unknown exit:\n%.*Rhxd\n", (int)sizeof(*pExit), pExit));
            AssertLogRelMsgFailedReturn(("Unknown exit on CPU #%u: %#x!\n", pVCpu->idCpu, pExit->ExitReason), VERR_NEM_IPE_3);
    }
}


/**
 * Deals with pending interrupt related force flags, may inject interrupt.
 *
 * @returns VBox strict status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context per CPU structure.
 * @param   pfInterruptWindows  Where to return interrupt window flags.
 */
static VBOXSTRICTRC nemHCWinHandleInterruptFF(PVMCC pVM, PVMCPUCC pVCpu, uint8_t *pfInterruptWindows)
{
    Assert(!TRPMHasTrap(pVCpu) && !pVM->nem.s.fLocalApicEmulation);
    RT_NOREF_PV(pVM);

    /*
     * First update APIC.  We ASSUME this won't need TPR/CR8.
     */
    if (VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_UPDATE_APIC))
    {
        PDMApicUpdatePendingInterrupts(pVCpu);
        if (!VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC
                                      | VMCPU_FF_INTERRUPT_NMI  | VMCPU_FF_INTERRUPT_SMI))
            return VINF_SUCCESS;
    }

    /*
     * We don't currently implement SMIs.
     */
    AssertReturn(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_SMI), VERR_NEM_IPE_0);

    /*
     * Check if we've got the minimum of state required for deciding whether we
     * can inject interrupts and NMIs.  If we don't have it, get all we might require
     * for injection via IEM.
     */
    bool const fPendingNmi = VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI);
    uint64_t   fNeedExtrn  = CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS
                           | (fPendingNmi ? CPUMCTX_EXTRN_INHIBIT_NMI : 0);
    if (pVCpu->cpum.GstCtx.fExtrn & fNeedExtrn)
    {
        VBOXSTRICTRC rcStrict = nemHCWinImportStateIfNeededStrict(pVCpu, NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM_XCPT, "IntFF");
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    /*
     * NMI? Try deliver it first.
     */
    if (fPendingNmi)
    {
        if (   !CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx)
            && !CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx))
        {
            VBOXSTRICTRC rcStrict = nemHCWinImportStateIfNeededStrict(pVCpu, NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM_XCPT, "NMI");
            if (rcStrict == VINF_SUCCESS)
            {
                VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_NMI);
                IEMTlbInvalidateAll(pVCpu);
                rcStrict = IEMInjectTrap(pVCpu, X86_XCPT_NMI, TRPM_HARDWARE_INT, 0, 0, 0);
                Log8(("Injected NMI on %u (%d)\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
            }
            return rcStrict;
        }
        *pfInterruptWindows |= NEM_WIN_INTW_F_NMI;
        Log8(("NMI window pending on %u\n", pVCpu->idCpu));
    }

    /*
     * APIC or PIC interrupt?
     */
    if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC))
    {
        /** @todo check NMI inhibiting here too! */
        if (   !CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx)
            && pVCpu->cpum.GstCtx.rflags.Bits.u1IF)
        {
            AssertCompile(NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM_XCPT & CPUMCTX_EXTRN_APIC_TPR);
            VBOXSTRICTRC rcStrict = nemHCWinImportStateIfNeededStrict(pVCpu, NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM_XCPT, "NMI");
            if (rcStrict == VINF_SUCCESS)
            {
                uint8_t bInterrupt;
                int rc = PDMGetInterrupt(pVCpu, &bInterrupt);
                if (RT_SUCCESS(rc))
                {
                    Log8(("Injecting interrupt %#x on %u: %04x:%08RX64 efl=%#x\n", bInterrupt, pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.eflags.u));
                    IEMTlbInvalidateAll(pVCpu);
                    rcStrict = IEMInjectTrap(pVCpu, bInterrupt, TRPM_HARDWARE_INT, 0, 0, 0);
                    Log8(("Injected interrupt %#x on %u (%d)\n", bInterrupt, pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
                }
                else if (rc == VERR_APIC_INTR_MASKED_BY_TPR)
                {
                    *pfInterruptWindows |= ((bInterrupt >> 4) << NEM_WIN_INTW_F_PRIO_SHIFT) | NEM_WIN_INTW_F_REGULAR;
                    Log8(("VERR_APIC_INTR_MASKED_BY_TPR: *pfInterruptWindows=%#x\n", *pfInterruptWindows));
                }
                else
                    Log8(("PDMGetInterrupt failed -> %Rrc\n", rc));
            }
            return rcStrict;
        }

        if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC) && !VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_PIC))
        {
            /* If only an APIC interrupt is pending, we need to know its priority. Otherwise we'll
             * likely get pointless deliverability notifications with IF=1 but TPR still too high.
             */
            Assert(!pVM->nem.s.fLocalApicEmulation);
            bool    fPendingIntr = false;
            uint8_t bTpr = 0;
            uint8_t bPendingIntr = 0;
            int rc = PDMApicGetTpr(pVCpu, &bTpr, &fPendingIntr, &bPendingIntr);
            AssertRC(rc);
            *pfInterruptWindows |= ((bPendingIntr >> 4) << NEM_WIN_INTW_F_PRIO_SHIFT) | NEM_WIN_INTW_F_REGULAR;
            Log8(("Interrupt window pending on %u: %#x (bTpr=%#x fPendingIntr=%d bPendingIntr=%#x)\n",
                  pVCpu->idCpu, *pfInterruptWindows, bTpr, fPendingIntr, bPendingIntr));
        }
        else
        {
            *pfInterruptWindows |= NEM_WIN_INTW_F_REGULAR;
            Log8(("Interrupt window pending on %u: %#x\n", pVCpu->idCpu, *pfInterruptWindows));
        }
    }

    return VINF_SUCCESS;
}


/**
 * Inner NEM runloop for windows.
 *
 * @returns Strict VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per CPU structure.
 */
static VBOXSTRICTRC nemHCWinRunGC(PVMCC pVM, PVMCPUCC pVCpu)
{
    LogFlow(("NEM/%u: %04x:%08RX64 efl=%#08RX64 <=\n", pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rflags.u));
#ifdef LOG_ENABLED
    if (LogIs3Enabled())
        nemHCWinLogState(pVM, pVCpu);
#endif

    /*
     * Try switch to NEM runloop state.
     */
    if (VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED))
    { /* likely */ }
    else
    {
        VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED_EXEC_NEM_CANCELED);
        LogFlow(("NEM/%u: returning immediately because canceled\n", pVCpu->idCpu));
        return VINF_SUCCESS;
    }

    /*
     * The run loop.
     *
     * Current approach to state updating to use the sledgehammer and sync
     * everything every time.  This will be optimized later.
     */
    const bool      fSingleStepping     = DBGFIsStepping(pVCpu);
//    const uint32_t  fCheckVmFFs         = !fSingleStepping ? VM_FF_HP_R0_PRE_HM_MASK
//                                                           : VM_FF_HP_R0_PRE_HM_STEP_MASK;
//    const uint32_t  fCheckCpuFFs        = !fSingleStepping ? VMCPU_FF_HP_R0_PRE_HM_MASK : VMCPU_FF_HP_R0_PRE_HM_STEP_MASK;
    VBOXSTRICTRC    rcStrict            = VINF_SUCCESS;
    for (unsigned iLoop = 0;; iLoop++)
    {
        /*
         * Pending interrupts or such?  Need to check and deal with this prior
         * to the state syncing.
         */
        pVCpu->nem.s.fDesiredInterruptWindows = 0;
        if (!pVM->nem.s.fLocalApicEmulation)
        {
            if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_UPDATE_APIC | VMCPU_FF_INTERRUPT_PIC
                                         | VMCPU_FF_INTERRUPT_NMI  | VMCPU_FF_INTERRUPT_SMI))
            {
                /* Try inject interrupt. */
                rcStrict = nemHCWinHandleInterruptFF(pVM, pVCpu, &pVCpu->nem.s.fDesiredInterruptWindows);
                if (rcStrict == VINF_SUCCESS)
                { /* likely */ }
                else
                {
                    LogFlow(("NEM/%u: breaking: nemHCWinHandleInterruptFF -> %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
                    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnStatus);
                    break;
                }
            }
        }
        else
        {
            /* We only need to handle the PIC usign ExtInt here, the APIC is handled through the NEM APIC backend. */
            Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC));

            if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_PIC))
                pVCpu->nem.s.fDesiredInterruptWindows |= NEM_WIN_INTW_F_REGULAR;
        }

#ifndef NEM_WIN_WITH_A20
        /*
         * Do not execute in hyper-V if the A20 isn't enabled.
         */
        if (PGMPhysIsA20Enabled(pVCpu))
        { /* likely */ }
        else
        {
            rcStrict = VINF_EM_RESCHEDULE_REM;
            LogFlow(("NEM/%u: breaking: A20 disabled\n", pVCpu->idCpu));
            break;
        }
#endif

        /*
         * Ensure that hyper-V has the whole state.
         * (We always update the interrupt windows settings when active as hyper-V seems
         * to forget about it after an exit.)
         */
        if (      (pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_ALL | CPUMCTX_EXTRN_NEM_WIN_MASK))
               !=                              (CPUMCTX_EXTRN_ALL | CPUMCTX_EXTRN_NEM_WIN_MASK)
            || (  (   pVCpu->nem.s.fDesiredInterruptWindows
                   || pVCpu->nem.s.fCurrentInterruptWindows != pVCpu->nem.s.fDesiredInterruptWindows) ) )
        {
            int rc2 = nemHCWinCopyStateToHyperV(pVM, pVCpu);
            AssertRCReturn(rc2, rc2);
        }

        /*
         * Poll timers and run for a bit.
         *
         * With the VID approach (ring-0 or ring-3) we can specify a timeout here,
         * so we take the time of the next timer event and uses that as a deadline.
         * The rounding heuristics are "tuned" so that rhel5 (1K timer) will boot fine.
         */
        /** @todo See if we cannot optimize this TMTimerPollGIP by only redoing
         *        the whole polling job when timers have changed... */
        uint64_t       offDeltaIgnored;
        uint64_t const nsNextTimerEvt = TMTimerPollGIP(pVM, pVCpu, &offDeltaIgnored); NOREF(nsNextTimerEvt);
        if (   !VM_FF_IS_ANY_SET(pVM, VM_FF_EMT_RENDEZVOUS | VM_FF_TM_VIRTUAL_SYNC)
            && !VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HM_TO_R3_MASK))
        {
            if (VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM_WAIT, VMCPUSTATE_STARTED_EXEC_NEM))
            {
#ifdef LOG_ENABLED
                if (LogIsFlowEnabled())
                {
                    static const WHV_REGISTER_NAME s_aNames[6] = { WHvX64RegisterCs, WHvX64RegisterRip, WHvX64RegisterRflags,
                                                                   WHvX64RegisterSs, WHvX64RegisterRsp, WHvX64RegisterCr0 };
                    WHV_REGISTER_VALUE aRegs[RT_ELEMENTS(s_aNames)] = { { { {0, 0} } } };
                    WHvGetVirtualProcessorRegisters(pVM->nem.s.hPartition, pVCpu->idCpu, s_aNames, RT_ELEMENTS(s_aNames), aRegs);
                    LogFlow(("NEM/%u: Entry @ %04x:%08RX64 IF=%d EFL=%#RX64 SS:RSP=%04x:%08RX64 cr0=%RX64\n",
                             pVCpu->idCpu, aRegs[0].Segment.Selector, aRegs[1].Reg64, RT_BOOL(aRegs[2].Reg64 & X86_EFL_IF),
                             aRegs[2].Reg64, aRegs[3].Segment.Selector, aRegs[4].Reg64, aRegs[5].Reg64));
                }
#endif
                if (pVCpu->CTX_SUFF(pVM)->nem.s.fLocalApicEmulation)
                    PDMApicExportState(pVCpu);

                WHV_RUN_VP_EXIT_CONTEXT ExitReason = {0};
                TMNotifyStartOfExecution(pVM, pVCpu);

                HRESULT hrc = WHvRunVirtualProcessor(pVM->nem.s.hPartition, pVCpu->idCpu, &ExitReason, sizeof(ExitReason));

                VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED_EXEC_NEM_WAIT);
                TMNotifyEndOfExecution(pVM, pVCpu, ASMReadTSC());
#ifdef LOG_ENABLED
                LogFlow(("NEM/%u: Exit  @ %04X:%08RX64 IF=%d CR8=%#x Reason=%#x\n", pVCpu->idCpu, ExitReason.VpContext.Cs.Selector,
                         ExitReason.VpContext.Rip, RT_BOOL(ExitReason.VpContext.Rflags & X86_EFL_IF), ExitReason.VpContext.Cr8,
                         ExitReason.ExitReason));
#endif
                if (SUCCEEDED(hrc))
                {
                    /*
                     * Deal with the message.
                     */
                    rcStrict = nemR3WinHandleExit(pVM, pVCpu, &ExitReason);
                    if (rcStrict == VINF_SUCCESS)
                    { /* hopefully likely */ }
                    else
                    {
                        LogFlow(("NEM/%u: breaking: nemHCWinHandleMessage -> %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
                        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnStatus);
                        break;
                    }
                }
                else
                    AssertLogRelMsgFailedReturn(("WHvRunVirtualProcessor failed for CPU #%u: %#x (%u)\n",
                                                 pVCpu->idCpu, hrc, GetLastError()),
                                                VERR_NEM_IPE_0);

                /*
                 * If no relevant FFs are pending, loop.
                 */
                if (   !VM_FF_IS_ANY_SET(   pVM,   !fSingleStepping ? VM_FF_HP_R0_PRE_HM_MASK    : VM_FF_HP_R0_PRE_HM_STEP_MASK)
                    && !VMCPU_FF_IS_ANY_SET(pVCpu, !fSingleStepping ? VMCPU_FF_HP_R0_PRE_HM_MASK : VMCPU_FF_HP_R0_PRE_HM_STEP_MASK) )
                    continue;

                /** @todo Try handle pending flags, not just return to EM loops.  Take care
                 *        not to set important RCs here unless we've handled a message. */
                LogFlow(("NEM/%u: breaking: pending FF (%#x / %#RX64)\n",
                         pVCpu->idCpu, pVM->fGlobalForcedActions, (uint64_t)pVCpu->fLocalForcedActions));
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnFFPost);
            }
            else
            {
                LogFlow(("NEM/%u: breaking: canceled %d (pre exec)\n", pVCpu->idCpu, VMCPU_GET_STATE(pVCpu) ));
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnCancel);
            }
        }
        else
        {
            LogFlow(("NEM/%u: breaking: pending FF (pre exec)\n", pVCpu->idCpu));
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnFFPre);
        }
        break;
    } /* the run loop */


    /*
     * If the CPU is running, make sure to stop it before we try sync back the
     * state and return to EM.  We don't sync back the whole state if we can help it.
     */
    if (!VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_EXEC_NEM))
        VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_EXEC_NEM_CANCELED);

    if (pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_ALL | (CPUMCTX_EXTRN_NEM_WIN_MASK & ~CPUMCTX_EXTRN_NEM_WIN_EVENT_INJECT)))
    {
        /* Try anticipate what we might need. */
        uint64_t fImport = IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI;
        if (   (rcStrict >= VINF_EM_FIRST && rcStrict <= VINF_EM_LAST)
            || RT_FAILURE(rcStrict))
            fImport = CPUMCTX_EXTRN_ALL | (CPUMCTX_EXTRN_NEM_WIN_MASK & ~CPUMCTX_EXTRN_NEM_WIN_EVENT_INJECT);
        else if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_PIC | VMCPU_FF_INTERRUPT_APIC
                                          | VMCPU_FF_INTERRUPT_NMI | VMCPU_FF_INTERRUPT_SMI))
            fImport |= IEM_CPUMCTX_EXTRN_XCPT_MASK;

        if (pVCpu->cpum.GstCtx.fExtrn & fImport)
        {
            int rc2 = nemHCWinCopyStateFromHyperV(pVM, pVCpu, fImport | CPUMCTX_EXTRN_NEM_WIN_EVENT_INJECT);
            if (RT_SUCCESS(rc2))
                pVCpu->cpum.GstCtx.fExtrn &= ~fImport;
            else if (RT_SUCCESS(rcStrict))
                rcStrict = rc2;
            if (!(pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_ALL | (CPUMCTX_EXTRN_NEM_WIN_MASK & ~CPUMCTX_EXTRN_NEM_WIN_EVENT_INJECT))))
                pVCpu->cpum.GstCtx.fExtrn = 0;
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturn);
        }
        else
        {
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturnSkipped);
            pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_NEM_WIN_EVENT_INJECT;
        }
    }
    else
    {
        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturnSkipped);
        pVCpu->cpum.GstCtx.fExtrn = 0;
    }

    LogFlow(("NEM/%u: %04x:%08RX64 efl=%#08RX64 => %Rrc\n", pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel,
             pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rflags.u, VBOXSTRICTRC_VAL(rcStrict) ));
    return rcStrict;
}

VMMR3_INT_DECL(VBOXSTRICTRC) NEMR3RunGC(PVM pVM, PVMCPU pVCpu)
{
    Assert(VM_IS_NEM_ENABLED(pVM));
    return nemHCWinRunGC(pVM, pVCpu);
}


VMMR3_INT_DECL(bool) NEMR3CanExecuteGuest(PVM pVM, PVMCPU pVCpu)
{
    Assert(VM_IS_NEM_ENABLED(pVM));

#ifndef NEM_WIN_WITH_A20
    /*
     * Only execute when the A20 gate is enabled because this lovely Hyper-V
     * blackbox does not seem to have any way to enable or disable A20.
     */
    RT_NOREF(pVM);
    return PGMPhysIsA20Enabled(pVCpu);
#else
    RT_NOREF(pVM, pVCpu);
    return true;
#endif
}


DECLHIDDEN(bool) nemR3NativeSetSingleInstruction(PVM pVM, PVMCPU pVCpu, bool fEnable)
{
    NOREF(pVM); NOREF(pVCpu); NOREF(fEnable);
    return false;
}


DECLHIDDEN(void) nemR3NativeNotifyFF(PVM pVM, PVMCPU pVCpu, uint32_t fFlags)
{
    Log8(("nemR3NativeNotifyFF: canceling %u\n", pVCpu->idCpu));
    HRESULT hrc = WHvCancelRunVirtualProcessor(pVM->nem.s.hPartition, pVCpu->idCpu, 0);
    AssertMsg(SUCCEEDED(hrc), ("WHvCancelRunVirtualProcessor -> hrc=%Rhrc\n", hrc));
    RT_NOREF_PV(hrc);
    RT_NOREF_PV(fFlags);
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChanged(PVM pVM, bool fUseDebugLoop)
{
    RT_NOREF(pVM, fUseDebugLoop);
    return false;
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChangedPerCpu(PVM pVM, PVMCPU pVCpu, bool fUseDebugLoop)
{
    RT_NOREF(pVM, pVCpu, fUseDebugLoop);
    return false;
}


DECLINLINE(int) nemR3NativeGCPhys2R3PtrReadOnly(PVM pVM, RTGCPHYS GCPhys, const void **ppv)
{
    PGMPAGEMAPLOCK Lock;
    int rc = PGMPhysGCPhys2CCPtrReadOnly(pVM, GCPhys, ppv, &Lock);
    if (RT_SUCCESS(rc))
        PGMPhysReleasePageMappingLock(pVM, &Lock);
    return rc;
}


DECLINLINE(int) nemR3NativeGCPhys2R3PtrWriteable(PVM pVM, RTGCPHYS GCPhys, void **ppv)
{
    PGMPAGEMAPLOCK Lock;
    int rc = PGMPhysGCPhys2CCPtr(pVM, GCPhys, ppv, &Lock);
    if (RT_SUCCESS(rc))
        PGMPhysReleasePageMappingLock(pVM, &Lock);
    return rc;
}


VMMR3_INT_DECL(int) NEMR3NotifyPhysRamRegister(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, void *pvR3,
                                               uint8_t *pu2State, uint32_t *puNemRange)
{
    Log5(("NEMR3NotifyPhysRamRegister: %RGp LB %RGp, pvR3=%p pu2State=%p (%d) puNemRange=%p (%d)\n",
          GCPhys, cb, pvR3, pu2State, pu2State, puNemRange, *puNemRange));

    *pu2State = UINT8_MAX;
    RT_NOREF(puNemRange);

    if (pvR3)
    {
        STAM_REL_PROFILE_START(&pVM->nem.s.StatProfMapGpaRange, a);
        HRESULT hrc = WHvMapGpaRange(pVM->nem.s.hPartition, pvR3, GCPhys, cb,
                                     WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute);
        STAM_REL_PROFILE_STOP(&pVM->nem.s.StatProfMapGpaRange, a);
        if (SUCCEEDED(hrc))
            *pu2State = NEM_WIN_PAGE_STATE_WRITABLE;
        else
        {
            LogRel(("NEMR3NotifyPhysRamRegister: GCPhys=%RGp LB %RGp pvR3=%p hrc=%Rhrc (%#x) Last=%#x/%u\n",
                    GCPhys, cb, pvR3, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            STAM_REL_COUNTER_INC(&pVM->nem.s.StatMapPageFailed);
            return VERR_NEM_MAP_PAGES_FAILED;
        }
    }
    return VINF_SUCCESS;
}


VMMR3_INT_DECL(bool) NEMR3IsMmio2DirtyPageTrackingSupported(PVM pVM)
{
    RT_NOREF(pVM);
    return g_pfnWHvQueryGpaRangeDirtyBitmap != NULL;
}


VMMR3_INT_DECL(int) NEMR3NotifyPhysMmioExMapEarly(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t fFlags,
                                                  void *pvRam, void *pvMmio2, uint8_t *pu2State, uint32_t *puNemRange)
{
    Log5(("NEMR3NotifyPhysMmioExMapEarly: %RGp LB %RGp fFlags=%#x pvRam=%p pvMmio2=%p pu2State=%p (%d) puNemRange=%p (%#x)\n",
          GCPhys, cb, fFlags, pvRam, pvMmio2, pu2State, *pu2State, puNemRange, puNemRange ? *puNemRange : UINT32_MAX));
    RT_NOREF(puNemRange);

    /*
     * Unmap the RAM we're replacing.
     */
    if (fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_REPLACE)
    {
        STAM_REL_PROFILE_START(&pVM->nem.s.StatProfUnmapGpaRange, a);
        HRESULT hrc = WHvUnmapGpaRange(pVM->nem.s.hPartition, GCPhys, cb);
        STAM_REL_PROFILE_STOP(&pVM->nem.s.StatProfUnmapGpaRange, a);
        if (SUCCEEDED(hrc))
        { /* likely */ }
        else if (pvMmio2)
            LogRel(("NEMR3NotifyPhysMmioExMapEarly: GCPhys=%RGp LB %RGp fFlags=%#x: Unmap -> hrc=%Rhrc (%#x) Last=%#x/%u (ignored)\n",
                    GCPhys, cb, fFlags, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
        else
        {
            LogRel(("NEMR3NotifyPhysMmioExMapEarly: GCPhys=%RGp LB %RGp fFlags=%#x: Unmap -> hrc=%Rhrc (%#x) Last=%#x/%u\n",
                    GCPhys, cb, fFlags, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPageFailed);
            return VERR_NEM_UNMAP_PAGES_FAILED;
        }
    }

    /*
     * Map MMIO2 if any.
     */
    if (pvMmio2)
    {
        Assert(fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_MMIO2);
        WHV_MAP_GPA_RANGE_FLAGS fWHvFlags = WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute;
        if ((fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_TRACK_DIRTY_PAGES) && g_pfnWHvQueryGpaRangeDirtyBitmap)
            fWHvFlags |= WHvMapGpaRangeFlagTrackDirtyPages;
        STAM_REL_PROFILE_START(&pVM->nem.s.StatProfMapGpaRange, a);
        HRESULT hrc = WHvMapGpaRange(pVM->nem.s.hPartition, pvMmio2, GCPhys, cb, fWHvFlags);
        STAM_REL_PROFILE_STOP(&pVM->nem.s.StatProfMapGpaRange, a);
        if (SUCCEEDED(hrc))
            *pu2State = NEM_WIN_PAGE_STATE_WRITABLE;
        else
        {
            LogRel(("NEMR3NotifyPhysMmioExMapEarly: GCPhys=%RGp LB %RGp fFlags=%#x pvMmio2=%p fWHvFlags=%#x: Map -> hrc=%Rhrc (%#x) Last=%#x/%u\n",
                    GCPhys, cb, fFlags, pvMmio2, fWHvFlags, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            STAM_REL_COUNTER_INC(&pVM->nem.s.StatMapPageFailed);
            return VERR_NEM_MAP_PAGES_FAILED;
        }
    }
    else
    {
        Assert(!(fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_MMIO2));
        *pu2State = NEM_WIN_PAGE_STATE_UNMAPPED;
    }
    RT_NOREF(pvRam);
    return VINF_SUCCESS;
}


VMMR3_INT_DECL(int) NEMR3NotifyPhysMmioExMapLate(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t fFlags,
                                                 void *pvRam, void *pvMmio2, uint32_t *puNemRange)
{
    RT_NOREF(pVM, GCPhys, cb, fFlags, pvRam, pvMmio2, puNemRange);
    return VINF_SUCCESS;
}


VMMR3_INT_DECL(int) NEMR3NotifyPhysMmioExUnmap(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t fFlags, void *pvRam,
                                               void *pvMmio2, uint8_t *pu2State, uint32_t *puNemRange)
{
    int rc = VINF_SUCCESS;
    Log5(("NEMR3NotifyPhysMmioExUnmap: %RGp LB %RGp fFlags=%#x pvRam=%p pvMmio2=%p pu2State=%p uNemRange=%#x (%#x)\n",
          GCPhys, cb, fFlags, pvRam, pvMmio2, pu2State, puNemRange, *puNemRange));

    /*
     * Unmap the MMIO2 pages.
     */
    /** @todo If we implement aliasing (MMIO2 page aliased into MMIO range),
     *        we may have more stuff to unmap even in case of pure MMIO... */
    if (fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_MMIO2)
    {
        STAM_REL_PROFILE_START(&pVM->nem.s.StatProfUnmapGpaRange, a);
        HRESULT hrc = WHvUnmapGpaRange(pVM->nem.s.hPartition, GCPhys, cb);
        STAM_REL_PROFILE_STOP(&pVM->nem.s.StatProfUnmapGpaRange, a);
        if (FAILED(hrc))
        {
            LogRel2(("NEMR3NotifyPhysMmioExUnmap: GCPhys=%RGp LB %RGp fFlags=%#x: Unmap -> hrc=%Rhrc (%#x) Last=%#x/%u (ignored)\n",
                     GCPhys, cb, fFlags, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            rc = VERR_NEM_UNMAP_PAGES_FAILED;
            STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPageFailed);
        }
    }

    /*
     * Restore the RAM we replaced.
     */
    if (fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_REPLACE)
    {
        AssertPtr(pvRam);
        STAM_REL_PROFILE_START(&pVM->nem.s.StatProfMapGpaRange, a);
        HRESULT hrc = WHvMapGpaRange(pVM->nem.s.hPartition, pvRam, GCPhys, cb,
                                     WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute);
        STAM_REL_PROFILE_STOP(&pVM->nem.s.StatProfMapGpaRange, a);
        if (SUCCEEDED(hrc))
        { /* likely */ }
        else
        {
            LogRel(("NEMR3NotifyPhysMmioExUnmap: GCPhys=%RGp LB %RGp pvMmio2=%p hrc=%Rhrc (%#x) Last=%#x/%u\n",
                    GCPhys, cb, pvMmio2, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            rc = VERR_NEM_MAP_PAGES_FAILED;
            STAM_REL_COUNTER_INC(&pVM->nem.s.StatMapPageFailed);
        }
        if (pu2State)
            *pu2State = NEM_WIN_PAGE_STATE_WRITABLE;
    }
    /* Mark the pages as unmapped if relevant. */
    else if (pu2State)
        *pu2State = NEM_WIN_PAGE_STATE_UNMAPPED;

    RT_NOREF(pvMmio2, puNemRange);
    return rc;
}


VMMR3_INT_DECL(int) NEMR3PhysMmio2QueryAndResetDirtyBitmap(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t uNemRange,
                                                           void *pvBitmap, size_t cbBitmap)
{
    Assert(VM_IS_NEM_ENABLED(pVM));
    AssertReturn(g_pfnWHvQueryGpaRangeDirtyBitmap, VERR_INTERNAL_ERROR_2);
    Assert(cbBitmap == (uint32_t)cbBitmap);
    RT_NOREF(uNemRange);

    /* This is being profiled by PGM, see /PGM/Mmio2QueryAndResetDirtyBitmap. */
    HRESULT hrc = WHvQueryGpaRangeDirtyBitmap(pVM->nem.s.hPartition, GCPhys, cb, (UINT64 *)pvBitmap, (uint32_t)cbBitmap);
    if (SUCCEEDED(hrc))
        return VINF_SUCCESS;

    AssertLogRelMsgFailed(("GCPhys=%RGp LB %RGp pvBitmap=%p LB %#zx hrc=%Rhrc (%#x) Last=%#x/%u\n",
                           GCPhys, cb, pvBitmap, cbBitmap, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
    return VERR_NEM_QUERY_DIRTY_BITMAP_FAILED;
}


VMMR3_INT_DECL(int)  NEMR3NotifyPhysRomRegisterEarly(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, void *pvPages, uint32_t fFlags,
                                                     uint8_t *pu2State, uint32_t *puNemRange)
{
    Log5(("nemR3NativeNotifyPhysRomRegisterEarly: %RGp LB %RGp pvPages=%p fFlags=%#x\n", GCPhys, cb, pvPages, fFlags));
    *pu2State   = UINT8_MAX;
    *puNemRange = 0;

#if 0 /* Let's not do this after all.  We'll protection change notifications for each page and if not we'll map them lazily. */
    RTGCPHYS const cPages = cb >> X86_PAGE_SHIFT;
    for (RTGCPHYS iPage = 0; iPage < cPages; iPage++, GCPhys += X86_PAGE_SIZE)
    {
        const void *pvPage;
        int rc = nemR3NativeGCPhys2R3PtrReadOnly(pVM, GCPhys, &pvPage);
        if (RT_SUCCESS(rc))
        {
            HRESULT hrc = WHvMapGpaRange(pVM->nem.s.hPartition, (void *)pvPage, GCPhys, X86_PAGE_SIZE,
                                         WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagExecute);
            if (SUCCEEDED(hrc))
            { /* likely */ }
            else
            {
                LogRel(("nemR3NativeNotifyPhysRomRegisterEarly: GCPhys=%RGp hrc=%Rhrc (%#x) Last=%#x/%u\n",
                        GCPhys, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
                return VERR_NEM_INIT_FAILED;
            }
        }
        else
        {
            LogRel(("nemR3NativeNotifyPhysRomRegisterEarly: GCPhys=%RGp rc=%Rrc\n", GCPhys, rc));
            return rc;
        }
    }
    RT_NOREF_PV(fFlags);
#else
    RT_NOREF(pVM, GCPhys, cb, pvPages, fFlags);
#endif
    return VINF_SUCCESS;
}


VMMR3_INT_DECL(int)  NEMR3NotifyPhysRomRegisterLate(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, void *pvPages,
                                                    uint32_t fFlags, uint8_t *pu2State, uint32_t *puNemRange)
{
    Log5(("nemR3NativeNotifyPhysRomRegisterLate: %RGp LB %RGp pvPages=%p fFlags=%#x pu2State=%p (%d) puNemRange=%p (%#x)\n",
          GCPhys, cb, pvPages, fFlags, pu2State, *pu2State, puNemRange, *puNemRange));
    *pu2State = UINT8_MAX;

    /*
     * (Re-)map readonly.
     */
    AssertPtrReturn(pvPages, VERR_INVALID_POINTER);
    STAM_REL_PROFILE_START(&pVM->nem.s.StatProfMapGpaRange, a);
    HRESULT hrc = WHvMapGpaRange(pVM->nem.s.hPartition, pvPages, GCPhys, cb, WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagExecute);
    STAM_REL_PROFILE_STOP(&pVM->nem.s.StatProfMapGpaRange, a);
    if (SUCCEEDED(hrc))
        *pu2State = NEM_WIN_PAGE_STATE_READABLE;
    else
    {
        LogRel(("nemR3NativeNotifyPhysRomRegisterEarly: GCPhys=%RGp LB %RGp pvPages=%p fFlags=%#x hrc=%Rhrc (%#x) Last=%#x/%u\n",
                GCPhys, cb, pvPages, fFlags, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
        STAM_REL_COUNTER_INC(&pVM->nem.s.StatMapPageFailed);
        return VERR_NEM_MAP_PAGES_FAILED;
    }
    RT_NOREF(fFlags, puNemRange);
    return VINF_SUCCESS;
}

#ifdef NEM_WIN_WITH_A20

/**
 * @callback_method_impl{FNPGMPHYSNEMCHECKPAGE}
 */
static DECLCALLBACK(int) nemR3WinUnsetForA20CheckerCallback(PVM pVM, PVMCPU pVCpu, RTGCPHYS GCPhys,
                                                            PPGMPHYSNEMPAGEINFO pInfo, void *pvUser)
{
    /* We'll just unmap the memory. */
    if (pInfo->u2NemState > NEM_WIN_PAGE_STATE_UNMAPPED)
    {
        HRESULT hrc = WHvUnmapGpaRange(pVM->nem.s.hPartition, GCPhys, X86_PAGE_SIZE);
        if (SUCCEEDED(hrc))
        {
            STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPage);
            uint32_t cMappedPages = ASMAtomicDecU32(&pVM->nem.s.cMappedPages); NOREF(cMappedPages);
            Log5(("NEM GPA unmapped/A20: %RGp (was %s, cMappedPages=%u)\n", GCPhys, g_apszPageStates[pInfo->u2NemState], cMappedPages));
            pInfo->u2NemState = NEM_WIN_PAGE_STATE_UNMAPPED;
        }
        else
        {
            STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPageFailed);
            LogRel(("nemR3WinUnsetForA20CheckerCallback/unmap: GCPhys=%RGp hrc=%Rhrc (%#x) Last=%#x/%u\n",
                    GCPhys, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            return VERR_INTERNAL_ERROR_2;
        }
    }
    RT_NOREF(pVCpu, pvUser);
    return VINF_SUCCESS;
}


/**
 * Unmaps a page from Hyper-V for the purpose of emulating A20 gate behavior.
 *
 * @returns The PGMPhysNemQueryPageInfo result.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   GCPhys          The page to unmap.
 */
static int nemR3WinUnmapPageForA20Gate(PVM pVM, PVMCPU pVCpu, RTGCPHYS GCPhys)
{
    PGMPHYSNEMPAGEINFO Info;
    return PGMPhysNemPageInfoChecker(pVM, pVCpu, GCPhys, false /*fMakeWritable*/, &Info,
                                     nemR3WinUnsetForA20CheckerCallback, NULL);
}

#endif /* NEM_WIN_WITH_A20 */

VMMR3_INT_DECL(void) NEMR3NotifySetA20(PVMCPU pVCpu, bool fEnabled)
{
    Log(("nemR3NativeNotifySetA20: fEnabled=%RTbool\n", fEnabled));
    Assert(VM_IS_NEM_ENABLED(pVCpu->CTX_SUFF(pVM)));
#ifdef NEM_WIN_WITH_A20
    PVM pVM = pVCpu->CTX_SUFF(pVM);
    if (!pVM->nem.s.fA20Fixed)
    {
        pVM->nem.s.fA20Enabled = fEnabled;
        for (RTGCPHYS GCPhys = _1M; GCPhys < _1M + _64K; GCPhys += X86_PAGE_SIZE)
            nemR3WinUnmapPageForA20Gate(pVM, pVCpu, GCPhys);
    }
#else
    RT_NOREF(pVCpu, fEnabled);
#endif
}


/**
 * @callback_method_impl{FNPGMPHYSNEMCHECKPAGE}
 */
static DECLCALLBACK(int) nemHCWinUnsetForA20CheckerCallback(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys,
                                                            PPGMPHYSNEMPAGEINFO pInfo, void *pvUser)
{
    /* We'll just unmap the memory. */
    if (pInfo->u2NemState > NEM_WIN_PAGE_STATE_UNMAPPED)
    {
        HRESULT hrc = WHvUnmapGpaRange(pVM->nem.s.hPartition, GCPhys, X86_PAGE_SIZE);
        if (SUCCEEDED(hrc))
        {
            STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPage);
            uint32_t cMappedPages = ASMAtomicDecU32(&pVM->nem.s.cMappedPages); NOREF(cMappedPages);
            Log5(("NEM GPA unmapped/A20: %RGp (was %s, cMappedPages=%u)\n", GCPhys, g_apszPageStates[pInfo->u2NemState], cMappedPages));
            pInfo->u2NemState = NEM_WIN_PAGE_STATE_UNMAPPED;
        }
        else
        {
            STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPageFailed);
            LogRel(("nemHCWinUnsetForA20CheckerCallback/unmap: GCPhys=%RGp hrc=%Rhrc (%#x) Last=%#x/%u\n",
                    GCPhys, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            return VERR_NEM_IPE_2;
        }
    }
    RT_NOREF(pVCpu, pvUser);
    return VINF_SUCCESS;
}


/**
 * Unmaps a page from Hyper-V for the purpose of emulating A20 gate behavior.
 *
 * @returns The PGMPhysNemQueryPageInfo result.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   GCPhys          The page to unmap.
 */
static int nemHCWinUnmapPageForA20Gate(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys)
{
    PGMPHYSNEMPAGEINFO Info;
    return PGMPhysNemPageInfoChecker(pVM, pVCpu, GCPhys, false /*fMakeWritable*/, &Info,
                                     nemHCWinUnsetForA20CheckerCallback, NULL);
}


void nemHCNativeNotifyHandlerPhysicalRegister(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhys, RTGCPHYS cb)
{
    Log5(("nemHCNativeNotifyHandlerPhysicalRegister: %RGp LB %RGp enmKind=%d\n", GCPhys, cb, enmKind));
    NOREF(pVM); NOREF(enmKind); NOREF(GCPhys); NOREF(cb);
}


VMM_INT_DECL(void) NEMHCNotifyHandlerPhysicalDeregister(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhys, RTGCPHYS cb,
                                                        RTR3PTR pvMemR3, uint8_t *pu2State)
{
    Log5(("NEMHCNotifyHandlerPhysicalDeregister: %RGp LB %RGp enmKind=%d pvMemR3=%p pu2State=%p (%d)\n",
          GCPhys, cb, enmKind, pvMemR3, pu2State, *pu2State));

    *pu2State = UINT8_MAX;
    if (pvMemR3)
    {
        STAM_REL_PROFILE_START(&pVM->nem.s.StatProfMapGpaRange, a);
        HRESULT hrc = WHvMapGpaRange(pVM->nem.s.hPartition, pvMemR3, GCPhys, cb,
                                     WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagExecute | WHvMapGpaRangeFlagWrite);
        STAM_REL_PROFILE_STOP(&pVM->nem.s.StatProfMapGpaRange, a);
        if (SUCCEEDED(hrc))
            *pu2State = NEM_WIN_PAGE_STATE_WRITABLE;
        else
            AssertLogRelMsgFailed(("NEMHCNotifyHandlerPhysicalDeregister: WHvMapGpaRange(,%p,%RGp,%RGp,) -> %Rhrc\n",
                                   pvMemR3, GCPhys, cb, hrc));
    }
    RT_NOREF(enmKind);
}


void nemHCNativeNotifyHandlerPhysicalModify(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhysOld,
                                            RTGCPHYS GCPhysNew, RTGCPHYS cb, bool fRestoreAsRAM)
{
    Log5(("nemHCNativeNotifyHandlerPhysicalModify: %RGp LB %RGp -> %RGp enmKind=%d fRestoreAsRAM=%d\n",
          GCPhysOld, cb, GCPhysNew, enmKind, fRestoreAsRAM));
    NOREF(pVM); NOREF(enmKind); NOREF(GCPhysOld); NOREF(GCPhysNew); NOREF(cb); NOREF(fRestoreAsRAM);
}


/**
 * Worker that maps pages into Hyper-V.
 *
 * This is used by the PGM physical page notifications as well as the memory
 * access VMEXIT handlers.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   GCPhysSrc       The source page address.
 * @param   GCPhysDst       The hyper-V destination page.  This may differ from
 *                          GCPhysSrc when A20 is disabled.
 * @param   fPageProt       NEM_PAGE_PROT_XXX.
 * @param   pu2State        Our page state (input/output).
 * @param   fBackingChanged Set if the page backing is being changed.
 * @thread  EMT(pVCpu)
 */
static int nemHCNativeSetPhysPage(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhysSrc, RTGCPHYS GCPhysDst,
                                  uint32_t fPageProt, uint8_t *pu2State, bool fBackingChanged)
{
    /*
     * Looks like we need to unmap a page before we can change the backing
     * or even modify the protection.  This is going to be *REALLY* efficient.
     * PGM lends us two bits to keep track of the state here.
     */
    RT_NOREF(pVCpu);
    uint8_t const u2OldState = *pu2State;
    uint8_t const u2NewState = fPageProt & NEM_PAGE_PROT_WRITE ? NEM_WIN_PAGE_STATE_WRITABLE
                             : fPageProt & NEM_PAGE_PROT_READ  ? NEM_WIN_PAGE_STATE_READABLE : NEM_WIN_PAGE_STATE_UNMAPPED;
    if (   fBackingChanged
        || u2NewState != u2OldState)
    {
        if (u2OldState > NEM_WIN_PAGE_STATE_UNMAPPED)
        {
            STAM_REL_PROFILE_START(&pVM->nem.s.StatProfUnmapGpaRangePage, a);
            HRESULT hrc = WHvUnmapGpaRange(pVM->nem.s.hPartition, GCPhysDst, X86_PAGE_SIZE);
            STAM_REL_PROFILE_STOP(&pVM->nem.s.StatProfUnmapGpaRangePage, a);
            if (SUCCEEDED(hrc))
            {
                *pu2State = NEM_WIN_PAGE_STATE_UNMAPPED;
                STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPage);
                uint32_t cMappedPages = ASMAtomicDecU32(&pVM->nem.s.cMappedPages); NOREF(cMappedPages);
                if (u2NewState == NEM_WIN_PAGE_STATE_UNMAPPED)
                {
                    Log5(("NEM GPA unmapped/set: %RGp (was %s, cMappedPages=%u)\n",
                          GCPhysDst, g_apszPageStates[u2OldState], cMappedPages));
                    return VINF_SUCCESS;
                }
            }
            else
            {
                STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPageFailed);
                LogRel(("nemHCNativeSetPhysPage/unmap: GCPhysDst=%RGp hrc=%Rhrc (%#x) Last=%#x/%u\n",
                        GCPhysDst, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
                return VERR_NEM_INIT_FAILED;
            }
        }
    }

    /*
     * Writeable mapping?
     */
    if (fPageProt & NEM_PAGE_PROT_WRITE)
    {
        void *pvPage;
        int rc = nemR3NativeGCPhys2R3PtrWriteable(pVM, GCPhysSrc, &pvPage);
        if (RT_SUCCESS(rc))
        {
            HRESULT hrc = WHvMapGpaRange(pVM->nem.s.hPartition, pvPage, GCPhysDst, X86_PAGE_SIZE,
                                         WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagExecute | WHvMapGpaRangeFlagWrite);
            if (SUCCEEDED(hrc))
            {
                *pu2State = NEM_WIN_PAGE_STATE_WRITABLE;
                STAM_REL_COUNTER_INC(&pVM->nem.s.StatMapPage);
                uint32_t cMappedPages = ASMAtomicIncU32(&pVM->nem.s.cMappedPages); NOREF(cMappedPages);
                Log5(("NEM GPA mapped/set: %RGp %s (was %s, cMappedPages=%u)\n",
                      GCPhysDst, g_apszPageStates[u2NewState], g_apszPageStates[u2OldState], cMappedPages));
                return VINF_SUCCESS;
            }
            STAM_REL_COUNTER_INC(&pVM->nem.s.StatMapPageFailed);
            LogRel(("nemHCNativeSetPhysPage/writable: GCPhysDst=%RGp hrc=%Rhrc (%#x) Last=%#x/%u\n",
                    GCPhysDst, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            return VERR_NEM_INIT_FAILED;
        }
        LogRel(("nemHCNativeSetPhysPage/writable: GCPhysSrc=%RGp rc=%Rrc\n", GCPhysSrc, rc));
        return rc;
    }

    if (fPageProt & NEM_PAGE_PROT_READ)
    {
        const void *pvPage;
        int rc = nemR3NativeGCPhys2R3PtrReadOnly(pVM, GCPhysSrc, &pvPage);
        if (RT_SUCCESS(rc))
        {
            STAM_REL_PROFILE_START(&pVM->nem.s.StatProfMapGpaRangePage, a);
            HRESULT hrc = WHvMapGpaRange(pVM->nem.s.hPartition, (void *)pvPage, GCPhysDst, X86_PAGE_SIZE,
                                         WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagExecute);
            STAM_REL_PROFILE_STOP(&pVM->nem.s.StatProfMapGpaRangePage, a);
            if (SUCCEEDED(hrc))
            {
                *pu2State = NEM_WIN_PAGE_STATE_READABLE;
                STAM_REL_COUNTER_INC(&pVM->nem.s.StatMapPage);
                uint32_t cMappedPages = ASMAtomicIncU32(&pVM->nem.s.cMappedPages); NOREF(cMappedPages);
                Log5(("NEM GPA mapped/set: %RGp %s (was %s, cMappedPages=%u)\n",
                      GCPhysDst, g_apszPageStates[u2NewState], g_apszPageStates[u2OldState], cMappedPages));
                return VINF_SUCCESS;
            }
            STAM_REL_COUNTER_INC(&pVM->nem.s.StatMapPageFailed);
            LogRel(("nemHCNativeSetPhysPage/readonly: GCPhysDst=%RGp hrc=%Rhrc (%#x) Last=%#x/%u\n",
                    GCPhysDst, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            return VERR_NEM_INIT_FAILED;
        }
        LogRel(("nemHCNativeSetPhysPage/readonly: GCPhysSrc=%RGp rc=%Rrc\n", GCPhysSrc, rc));
        return rc;
    }

    /* We already unmapped it above. */
    *pu2State = NEM_WIN_PAGE_STATE_UNMAPPED;
    return VINF_SUCCESS;
}


static int nemHCJustUnmapPageFromHyperV(PVMCC pVM, RTGCPHYS GCPhysDst, uint8_t *pu2State)
{
    if (*pu2State <= NEM_WIN_PAGE_STATE_UNMAPPED)
    {
        Log5(("nemHCJustUnmapPageFromHyperV: %RGp == unmapped\n", GCPhysDst));
        *pu2State = NEM_WIN_PAGE_STATE_UNMAPPED;
        return VINF_SUCCESS;
    }

    STAM_REL_PROFILE_START(&pVM->nem.s.StatProfUnmapGpaRangePage, a);
    HRESULT hrc = WHvUnmapGpaRange(pVM->nem.s.hPartition, GCPhysDst & ~(RTGCPHYS)X86_PAGE_OFFSET_MASK, X86_PAGE_SIZE);
    STAM_REL_PROFILE_STOP(&pVM->nem.s.StatProfUnmapGpaRangePage, a);
    if (SUCCEEDED(hrc))
    {
        STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPage);
        uint32_t cMappedPages = ASMAtomicDecU32(&pVM->nem.s.cMappedPages); NOREF(cMappedPages);
        *pu2State = NEM_WIN_PAGE_STATE_UNMAPPED;
        Log5(("nemHCJustUnmapPageFromHyperV: %RGp => unmapped (total %u)\n", GCPhysDst, cMappedPages));
        return VINF_SUCCESS;
    }
    STAM_REL_COUNTER_INC(&pVM->nem.s.StatUnmapPageFailed);
    LogRel(("nemHCJustUnmapPageFromHyperV(%RGp): failed! hrc=%Rhrc (%#x) Last=%#x/%u\n",
            GCPhysDst, hrc, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
    return VERR_NEM_IPE_6;
}


int nemHCNativeNotifyPhysPageAllocated(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys, uint32_t fPageProt,
                                       PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("nemHCNativeNotifyPhysPageAllocated: %RGp HCPhys=%RHp fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhys, fPageProt, enmType, *pu2State));
    RT_NOREF_PV(HCPhys); RT_NOREF_PV(enmType);

    int rc;
    RT_NOREF_PV(fPageProt);
#ifdef NEM_WIN_WITH_A20
    if (   pVM->nem.s.fA20Enabled
        || !NEM_WIN_IS_RELEVANT_TO_A20(GCPhys))
#endif
        rc = nemHCJustUnmapPageFromHyperV(pVM, GCPhys, pu2State);
#ifdef NEM_WIN_WITH_A20
    else if (!NEM_WIN_IS_SUBJECT_TO_A20(GCPhys))
        rc = nemHCJustUnmapPageFromHyperV(pVM, GCPhys, pu2State);
    else
        rc = VINF_SUCCESS; /* ignore since we've got the alias page at this address. */
#endif
    return rc;
}


VMM_INT_DECL(void) NEMHCNotifyPhysPageProtChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys, RTR3PTR pvR3, uint32_t fPageProt,
                                                  PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("NEMHCNotifyPhysPageProtChanged: %RGp HCPhys=%RHp fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhys, fPageProt, enmType, *pu2State));
    Assert(VM_IS_NEM_ENABLED(pVM));
    RT_NOREF(HCPhys, enmType, pvR3);

    RT_NOREF_PV(fPageProt);
#ifdef NEM_WIN_WITH_A20
    if (   pVM->nem.s.fA20Enabled
        || !NEM_WIN_IS_RELEVANT_TO_A20(GCPhys))
#endif
        nemHCJustUnmapPageFromHyperV(pVM, GCPhys, pu2State);
#ifdef NEM_WIN_WITH_A20
    else if (!NEM_WIN_IS_SUBJECT_TO_A20(GCPhys))
        nemHCJustUnmapPageFromHyperV(pVM, GCPhys, pu2State);
    /* else: ignore since we've got the alias page at this address. */
#endif
}


VMM_INT_DECL(void) NEMHCNotifyPhysPageChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhysPrev, RTHCPHYS HCPhysNew,
                                              RTR3PTR pvNewR3, uint32_t fPageProt, PGMPAGETYPE enmType, uint8_t *pu2State)
{
    Log5(("nemHCNativeNotifyPhysPageChanged: %RGp HCPhys=%RHp->%RHp pvNewR3=%p fPageProt=%#x enmType=%d *pu2State=%d\n",
          GCPhys, HCPhysPrev, HCPhysNew, pvNewR3, fPageProt, enmType, *pu2State));
    Assert(VM_IS_NEM_ENABLED(pVM));
    RT_NOREF(HCPhysPrev, HCPhysNew, pvNewR3, enmType);

    RT_NOREF_PV(fPageProt);
#ifdef NEM_WIN_WITH_A20
    if (   pVM->nem.s.fA20Enabled
        || !NEM_WIN_IS_RELEVANT_TO_A20(GCPhys))
#endif
        nemHCJustUnmapPageFromHyperV(pVM, GCPhys, pu2State);
#ifdef NEM_WIN_WITH_A20
    else if (!NEM_WIN_IS_SUBJECT_TO_A20(GCPhys))
        nemHCJustUnmapPageFromHyperV(pVM, GCPhys, pu2State);
    /* else: ignore since we've got the alias page at this address. */
#endif
}


/**
 * Returns features supported by the NEM backend.
 *
 * @returns Flags of features supported by the native NEM backend.
 * @param   pVM             The cross context VM structure.
 */
VMM_INT_DECL(uint32_t) NEMHCGetFeatures(PVMCC pVM)
{
    /** @todo Is NEM_FEAT_F_FULL_GST_EXEC always true? */
    return   NEM_FEAT_F_NESTED_PAGING
           | NEM_FEAT_F_FULL_GST_EXEC
           | (pVM->nem.s.fXsaveSupported ? NEM_FEAT_F_XSAVE_XRSTOR : 0);
}


VMMR3_INT_DECL(int) NEMR3WinGetPartitionHandle(PVM pVM, PRTHCUINTPTR pHCPtrHandle)
{
    AssertPtrReturn(pVM, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pHCPtrHandle, VERR_INVALID_PARAMETER);
    *pHCPtrHandle = (RTHCUINTPTR)pVM->nem.s.hPartition;
    return VINF_SUCCESS;
}


/** @page pg_nem_win NEM/win - Native Execution Manager, Windows.
 *
 * On Windows the Hyper-V root partition (dom0 in zen terminology) does not have
 * nested VT-x or AMD-V capabilities.  Early on raw-mode worked inside it, but
 * for a while now we've been getting \#GPs when trying to modify CR4 in the
 * world switcher.  So, when Hyper-V is active on Windows we have little choice
 * but to use Hyper-V to run our VMs.
 *
 *
 * @section sub_nem_win_whv   The WinHvPlatform API
 *
 * Since Windows 10 build 17083 there is a documented API for managing Hyper-V
 * VMs: header file WinHvPlatform.h and implementation in WinHvPlatform.dll.
 * This interface is a wrapper around the undocumented Virtualization
 * Infrastructure Driver (VID) API - VID.DLL and VID.SYS.  The wrapper is
 * written in C++, namespaced, early versions (at least) was using standard C++
 * container templates in several places.
 *
 * When creating a VM using WHvCreatePartition, it will only create the
 * WinHvPlatform structures for it, to which you get an abstract pointer.  The
 * VID API that actually creates the partition is first engaged when you call
 * WHvSetupPartition after first setting a lot of properties using
 * WHvSetPartitionProperty.  Since the VID API is just a very thin wrapper
 * around CreateFile and NtDeviceIoControlFile, it returns an actual HANDLE for
 * the partition to WinHvPlatform.  We fish this HANDLE out of the WinHvPlatform
 * partition structures because we need to talk directly to VID for reasons
 * we'll get to in a bit.  (Btw. we could also intercept the CreateFileW or
 * NtDeviceIoControlFile calls from VID.DLL to get the HANDLE should fishing in
 * the partition structures become difficult.)
 *
 * The WinHvPlatform API requires us to both set the number of guest CPUs before
 * setting up the partition and call WHvCreateVirtualProcessor for each of them.
 * The CPU creation function boils down to a VidMessageSlotMap call that sets up
 * and maps a message buffer into ring-3 for async communication with hyper-V
 * and/or the VID.SYS thread actually running the CPU thru
 * WinHvRunVpDispatchLoop().  When for instance a VMEXIT is encountered, hyper-V
 * sends a message that the WHvRunVirtualProcessor API retrieves (and later
 * acknowledges) via VidMessageSlotHandleAndGetNext.   Since or about build
 * 17757 a register page is also mapped into user space when creating the
 * virtual CPU.  It should be noteded that WHvDeleteVirtualProcessor doesn't do
 * much as there seems to be no partner function VidMessagesSlotMap that
 * reverses what it did.
 *
 * Memory is managed thru calls to WHvMapGpaRange and WHvUnmapGpaRange (GPA does
 * not mean grade point average here, but rather guest physical addressspace),
 * which corresponds to VidCreateVaGpaRangeSpecifyUserVa and VidDestroyGpaRange
 * respectively.  As 'UserVa' indicates, the functions works on user process
 * memory.  The mappings are also subject to quota restrictions, so the number
 * of ranges are limited and probably their total size as well.  Obviously
 * VID.SYS keeps track of the ranges, but so does WinHvPlatform, which means
 * there is a bit of overhead involved and quota restrctions makes sense.
 *
 * Running guest code is done through the WHvRunVirtualProcessor function.  It
 * asynchronously starts or resumes hyper-V CPU execution and then waits for an
 * VMEXIT message.  Hyper-V / VID.SYS will return information about the message
 * in the message buffer mapping, and WHvRunVirtualProcessor will convert that
 * finto it's own WHV_RUN_VP_EXIT_CONTEXT format.
 *
 * Other threads can interrupt the execution by using WHvCancelVirtualProcessor,
 * which since or about build 17757 uses VidMessageSlotHandleAndGetNext to do
 * the work (earlier builds would open the waiting thread, do a dummy
 * QueueUserAPC on it, and let it upon return use VidStopVirtualProcessor to
 * do the actual stopping).  While there is certainly a race between cancelation
 * and the CPU causing a natural VMEXIT, it is not known whether this still
 * causes extra work on subsequent WHvRunVirtualProcessor calls (it did in and
 * earlier than 17134).
 *
 * Registers are retrieved and set via WHvGetVirtualProcessorRegisters and
 * WHvSetVirtualProcessorRegisters.  In addition, several VMEXITs include
 * essential register state in the exit context information, potentially making
 * it possible to emulate the instruction causing the exit without involving
 * WHvGetVirtualProcessorRegisters.
 *
 *
 * @subsection subsec_nem_win_whv_cons  Issues & Feedback
 *
 * Here are some observations (mostly against build 17101):
 *
 * - The VMEXIT performance is dismal (build 17134).
 *
 *   Our proof of concept implementation with a kernel runloop (i.e. not using
 *   WHvRunVirtualProcessor and friends, but calling VID.SYS fast I/O control
 *   entry point directly) delivers 9-10% of the port I/O performance and only
 *   6-7% of the MMIO performance that we have with our own hypervisor.
 *
 *   When using the offical WinHvPlatform API, the numbers are %3 for port I/O
 *   and 5% for MMIO.
 *
 *   While the tests we've done are using tight tight loops only doing port I/O
 *   and MMIO, the problem is clearly visible when running regular guest OSes.
 *   Anything that hammers the VGA device would be suffering, for example:
 *
 *       - Windows 2000 boot screen animation overloads us with MMIO exits
 *         and won't even boot because all the time is spent in interrupt
 *         handlers and redrawin the screen.
 *
 *       - DSL 4.4 and its bootmenu logo is slower than molasses in january.
 *
 *   We have not found a workaround for this yet.
 *
 *   Something that might improve the issue a little is to detect blocks with
 *   excessive MMIO and port I/O exits and emulate instructions to cover
 *   multiple exits before letting Hyper-V have a go at the guest execution
 *   again.  This will only improve the situation under some circumstances,
 *   since emulating instructions without recompilation can be expensive, so
 *   there will only be real gains if the exitting instructions are tightly
 *   packed.
 *
 *   Update: Security fixes during the summer of 2018 caused the performance to
 *   dropped even more.
 *
 *   Update [build 17757]: Some performance improvements here, but they don't
 *   yet make up for what was lost this summer.
 *
 *
 * - We need a way to directly modify the TSC offset (or bias if you like).
 *
 *   The current approach of setting the WHvX64RegisterTsc register one by one
 *   on each virtual CPU in sequence will introduce random inaccuracies,
 *   especially if the thread doing the job is reschduled at a bad time.
 *
 *
 * - Unable to access WHvX64RegisterMsrMtrrCap (build 17134).
 *
 *
 * - On AMD Ryzen grub/debian 9.0 ends up with a unrecoverable exception
 *   when IA32_MTRR_PHYSMASK0 is written.
 *
 *
 * - The IA32_APIC_BASE register does not work right:
 *
 *      - Attempts by the guest to clear bit 11 (EN) are ignored, both the
 *        guest and the VMM reads back the old value.
 *
 *      - Attempts to modify the base address (bits NN:12) seems to be ignored
 *        in the same way.
 *
 *      - The VMM can modify both the base address as well as the the EN and
 *        BSP bits, however this is useless if we cannot intercept the WRMSR.
 *
 *      - Attempts by the guest to set the EXTD bit (X2APIC) result in \#GP(0),
 *        while the VMM ends up with with ERROR_HV_INVALID_PARAMETER.  Seems
 *        there is no way to support X2APIC.
 *
 *
 * - Not sure if this is a thing, but WHvCancelVirtualProcessor seems to cause
 *   cause a lot more spurious WHvRunVirtualProcessor returns that what we get
 *   with the replacement code.  By spurious returns we mean that the
 *   subsequent call to WHvRunVirtualProcessor would return immediately.
 *
 *   Update [build 17757]: New cancelation code might have addressed this, but
 *   haven't had time to test it yet.
 *
 *
 * - There is no API for modifying protection of a page within a GPA range.
 *
 *   From what we can tell, the only way to modify the protection (like readonly
 *   -> writable, or vice versa) is to first unmap the range and then remap it
 *   with the new protection.
 *
 *   We are for instance doing this quite a bit in order to track dirty VRAM
 *   pages.  VRAM pages starts out as readonly, when the guest writes to a page
 *   we take an exit, notes down which page it is, makes it writable and restart
 *   the instruction.  After refreshing the display, we reset all the writable
 *   pages to readonly again, bulk fashion.
 *
 *   Now to work around this issue, we do page sized GPA ranges.  In addition to
 *   add a lot of tracking overhead to WinHvPlatform and VID.SYS, this also
 *   causes us to exceed our quota before we've even mapped a default sized
 *   (128MB) VRAM page-by-page.  So, to work around this quota issue we have to
 *   lazily map pages and actively restrict the number of mappings.
 *
 *   Our best workaround thus far is bypassing WinHvPlatform and VID entirely
 *   when in comes to guest memory management and instead use the underlying
 *   hypercalls (HvCallMapGpaPages, HvCallUnmapGpaPages) to do it ourselves.
 *   (This also maps a whole lot better into our own guest page management
 *   infrastructure.)
 *
 *   Update [build 17757]: Introduces a KVM like dirty logging API which could
 *   help tracking dirty VGA pages, while being useless for shadow ROM and
 *   devices trying catch the guest updating descriptors and such.
 *
 *
 * - Observed problems doing WHvUnmapGpaRange immediately followed by
 *   WHvMapGpaRange.
 *
 *   As mentioned above, we've been forced to use this sequence when modifying
 *   page protection.   However, when transitioning from readonly to writable,
 *   we've ended up looping forever with the same write to readonly memory
 *   VMEXIT.  We're wondering if this issue might be related to the lazy mapping
 *   logic in WinHvPlatform.
 *
 *   Workaround: Insert a WHvRunVirtualProcessor call and make sure to get a GPA
 *   unmapped exit between the two calls.  Not entirely great performance wise
 *   (or the santity of our code).
 *
 *
 * - Implementing A20 gate behavior is tedious, where as correctly emulating the
 *   A20M# pin (present on 486 and later) is near impossible for SMP setups
 *   (e.g. possiblity of two CPUs with different A20 status).
 *
 *   Workaround #1 (obsolete): Only do A20 on CPU 0, restricting the emulation
 *   to HMA. We unmap all pages related to HMA (0x100000..0x10ffff) when the A20
 *   state changes, lazily syncing the right pages back when accessed.
 *
 *   Workaround #2 (used): Use IEM when the A20 gate is disabled.
 *
 *
 * - WHVRunVirtualProcessor wastes time converting VID/Hyper-V messages to its
 *   own format (WHV_RUN_VP_EXIT_CONTEXT).
 *
 *   We understand this might be because Microsoft wishes to remain free to
 *   modify the VID/Hyper-V messages, but it's still rather silly and does slow
 *   things down a little.  We'd much rather just process the messages directly.
 *
 *
 * - WHVRunVirtualProcessor would've benefited from using a callback interface:
 *
 *      - The potential size changes of the exit context structure wouldn't be
 *        an issue, since the function could manage that itself.
 *
 *      - State handling could probably be simplified (like cancelation).
 *
 *
 * - WHvGetVirtualProcessorRegisters and WHvSetVirtualProcessorRegisters
 *   internally converts register names, probably using temporary heap buffers.
 *
 *   From the looks of things, they are converting from WHV_REGISTER_NAME to
 *   HV_REGISTER_NAME from in the "Virtual Processor Register Names" section in
 *   the "Hypervisor Top-Level Functional Specification" document.  This feels
 *   like an awful waste of time.
 *
 *   We simply cannot understand why HV_REGISTER_NAME isn't used directly here,
 *   or at least the same values, making any conversion reduntant.  Restricting
 *   access to certain registers could easily be implement by scanning the
 *   inputs.
 *
 *   To avoid the heap + conversion overhead, we're currently using the
 *   HvCallGetVpRegisters and HvCallSetVpRegisters calls directly, at least for
 *   the ring-0 code.
 *
 *   Update [build 17757]: Register translation has been very cleverly
 *   optimized and made table driven (2 top level tables, 4 + 1 leaf tables).
 *   Register information consists of the 32-bit HV register name, register page
 *   offset, and flags (giving valid offset, size and more).  Register
 *   getting/settings seems to be done by hoping that the register page provides
 *   it all, and falling back on the VidSetVirtualProcessorState if one or more
 *   registers are not available there.
 *
 *   Note! We have currently not updated our ring-0 code to take the register
 *   page into account, so it's suffering a little compared to the ring-3 code
 *   that now uses the offical APIs for registers.
 *
 *
 * - The YMM and XCR0 registers are not yet named (17083).  This probably
 *   wouldn't be a problem if HV_REGISTER_NAME was used, see previous point.
 *
 *   Update [build 17757]: XCR0 is added. YMM register values seems to be put
 *   into a yet undocumented XsaveState interface.  Approach is a little bulky,
 *   but saves number of enums and dispenses with register transation.  Also,
 *   the underlying Vid setter API duplicates the input buffer on the heap,
 *   adding a 16 byte header.
 *
 *
 * - Why does VID.SYS only query/set 32 registers at the time thru the
 *   HvCallGetVpRegisters and HvCallSetVpRegisters hypercalls?
 *
 *   We've not trouble getting/setting all the registers defined by
 *   WHV_REGISTER_NAME in one hypercall (around 80).  Some kind of stack
 *   buffering or similar?
 *
 *
 * - To handle the VMMCALL / VMCALL instructions, it seems we need to intercept
 *   \#UD exceptions and inspect the opcodes.  A dedicated exit for hypercalls
 *   would be more efficient, esp. for guests using \#UD for other purposes..
 *
 *
 * - Wrong instruction length in the VpContext with unmapped GPA memory exit
 *   contexts on 17115/AMD.
 *
 *   One byte "PUSH CS" was reported as 2 bytes, while a two byte
 *   "MOV [EBX],EAX" was reported with a 1 byte instruction length.  Problem
 *   naturally present in untranslated hyper-v messages.
 *
 *
 * - The I/O port exit context information seems to be missing the address size
 *   information needed for correct string I/O emulation.
 *
 *   VT-x provides this information in bits 7:9 in the instruction information
 *   field on newer CPUs.  AMD-V in bits 7:9 in the EXITINFO1 field in the VMCB.
 *
 *   We can probably work around this by scanning the instruction bytes for
 *   address size prefixes.  Haven't investigated it any further yet.
 *
 *
 * - Querying WHvCapabilityCodeExceptionExitBitmap returns zero even when
 *   intercepts demonstrably works (17134).
 *
 *
 * - Querying HvPartitionPropertyDebugChannelId via HvCallGetPartitionProperty
 *   (hypercall) hangs the host (17134).
 *
 * - CommonUtilities::GuidToString needs a 'static' before the hex digit array,
 *   looks pointless to re-init a stack copy it for each call (novice mistake).
 *
 *
 * Old concerns that have been addressed:
 *
 * - The WHvCancelVirtualProcessor API schedules a dummy usermode APC callback
 *   in order to cancel any current or future alertable wait in VID.SYS during
 *   the VidMessageSlotHandleAndGetNext call.
 *
 *   IIRC this will make the kernel schedule the specified callback thru
 *   NTDLL!KiUserApcDispatcher by modifying the thread context and quite
 *   possibly the userland thread stack.  When the APC callback returns to
 *   KiUserApcDispatcher, it will call NtContinue to restore the old thread
 *   context and resume execution from there.  This naturally adds up to some
 *   CPU cycles, ring transitions aren't for free, especially after Spectre &
 *   Meltdown mitigations.
 *
 *   Using NtAltertThread call could do the same without the thread context
 *   modifications and the extra kernel call.
 *
 *   Update: All concerns have addressed in or about build 17757.
 *
 *   The WHvCancelVirtualProcessor API is now implemented using a new
 *   VidMessageSlotHandleAndGetNext() flag (4).  Codepath is slightly longer
 *   than NtAlertThread, but has the added benefit that spurious wakeups can be
 *   more easily reduced.
 *
 *
 * - When WHvRunVirtualProcessor returns without a message, or on a terse
 *   VID message like HLT, it will make a kernel call to get some registers.
 *   This is potentially inefficient if the caller decides he needs more
 *   register state.
 *
 *   It would be better to just return what's available and let the caller fetch
 *   what is missing from his point of view in a single kernel call.
 *
 *   Update: All concerns have been addressed in or about build 17757.  Selected
 *   registers are now available via shared memory and thus HLT should (not
 *   verified) no longer require a system call to compose the exit context data.
 *
 *
 * - The WHvRunVirtualProcessor implementation does lazy GPA range mappings when
 *   a unmapped GPA message is received from hyper-V.
 *
 *   Since MMIO is currently realized as unmapped GPA, this will slow down all
 *   MMIO accesses a tiny little bit as WHvRunVirtualProcessor looks up the
 *   guest physical address to check if it is a pending lazy mapping.
 *
 *   The lazy mapping feature makes no sense to us.  We as API user have all the
 *   information and can do lazy mapping ourselves if we want/have to (see next
 *   point).
 *
 *   Update: All concerns have been addressed in or about build 17757.
 *
 *
 * - The WHvGetCapability function has a weird design:
 *      - The CapabilityCode parameter is pointlessly duplicated in the output
 *        structure (WHV_CAPABILITY).
 *
 *      - API takes void pointer, but everyone will probably be using
 *        WHV_CAPABILITY due to WHV_CAPABILITY::CapabilityCode making it
 *        impractical to use anything else.
 *
 *      - No output size.
 *
 *      - See GetFileAttributesEx, GetFileInformationByHandleEx,
 *        FindFirstFileEx, and others for typical pattern for generic
 *        information getters.
 *
 *   Update: All concerns have been addressed in build 17110.
 *
 *
 * - The WHvGetPartitionProperty function uses the same weird design as
 *   WHvGetCapability, see above.
 *
 *   Update: All concerns have been addressed in build 17110.
 *
 *
 * - The WHvSetPartitionProperty function has a totally weird design too:
 *      - In contrast to its partner WHvGetPartitionProperty, the property code
 *        is not a separate input parameter here but part of the input
 *        structure.
 *
 *      - The input structure is a void pointer rather than a pointer to
 *        WHV_PARTITION_PROPERTY which everyone probably will be using because
 *        of the WHV_PARTITION_PROPERTY::PropertyCode field.
 *
 *      - Really, why use PVOID for the input when the function isn't accepting
 *        minimal sizes.  E.g. WHVPartitionPropertyCodeProcessorClFlushSize only
 *        requires a 9 byte input, but the function insists on 16 bytes (17083).
 *
 *      - See GetFileAttributesEx, SetFileInformationByHandle, FindFirstFileEx,
 *        and others for typical pattern for generic information setters and
 *        getters.
 *
 *   Update: All concerns have been addressed in build 17110.
 *
 *
 * @section sec_nem_win_large_pages     Large Pages
 *
 * We've got a standalone memory allocation and access testcase bs3-memalloc-1
 * which was run with 48GiB of guest RAM configured on a NUC 11 box running
 * Windows 11 GA.  In the simplified NEM memory mode no exits should be
 * generated while the access tests are running.
 *
 * The bs3-memalloc-1 results kind of hints at some tiny speed-up if the guest
 * RAM is allocated using the MEM_LARGE_PAGES flag, but only in the 3rd access
 * check (typical 350 000 MiB/s w/o and around 400 000 MiB/s).  The result for
 * the 2nd access varies a lot, perhaps hinting at some table optimizations
 * going on.
 *
 * The initial access where the memory is locked/whatever has absolutely horrid
 * results regardless of whether large pages are enabled or not. Typically
 * bobbing close to 500 MiB/s, non-large pages a little faster.
 *
 * NEM w/ simplified memory and MEM_LARGE_PAGES:
 * @verbatim
bs3-memalloc-1: TESTING...
bs3-memalloc-1: #0/0x0: 0x0000000000000000 LB 0x000000000009fc00 USABLE (1)
bs3-memalloc-1: #1/0x1: 0x000000000009fc00 LB 0x0000000000000400 RESERVED (2)
bs3-memalloc-1: #2/0x2: 0x00000000000f0000 LB 0x0000000000010000 RESERVED (2)
bs3-memalloc-1: #3/0x3: 0x0000000000100000 LB 0x00000000dfef0000 USABLE (1)
bs3-memalloc-1: #4/0x4: 0x00000000dfff0000 LB 0x0000000000010000 ACPI_RECLAIMABLE (3)
bs3-memalloc-1: #5/0x5: 0x00000000fec00000 LB 0x0000000000001000 RESERVED (2)
bs3-memalloc-1: #6/0x6: 0x00000000fee00000 LB 0x0000000000001000 RESERVED (2)
bs3-memalloc-1: #7/0x7: 0x00000000fffc0000 LB 0x0000000000040000 RESERVED (2)
bs3-memalloc-1: #8/0x9: 0x0000000100000000 LB 0x0000000b20000000 USABLE (1)
bs3-memalloc-1: Found 1 interesting entries covering 0xb20000000 bytes (44 GB).
bs3-memalloc-1: From 0x100000000 to 0xc20000000
bs3-memalloc-1: INT15h/E820                                                 : PASSED
bs3-memalloc-1: Mapping memory above 4GB                                    : PASSED
bs3-memalloc-1:   Pages                                                     :       11 665 408 pages
bs3-memalloc-1:   MiBs                                                      :           45 568 MB
bs3-memalloc-1:   Alloc elapsed                                             :   90 925 263 996 ns
bs3-memalloc-1:   Alloc elapsed in ticks                                    :  272 340 387 336 ticks
bs3-memalloc-1:   Page alloc time                                           :            7 794 ns/page
bs3-memalloc-1:   Page alloc time in ticks                                  :           23 345 ticks/page
bs3-memalloc-1:   Alloc thruput                                             :          128 296 pages/s
bs3-memalloc-1:   Alloc thruput in MiBs                                     :              501 MB/s
bs3-memalloc-1: Allocation speed                                            : PASSED
bs3-memalloc-1:   Access elapsed                                            :   85 074 483 467 ns
bs3-memalloc-1:   Access elapsed in ticks                                   :  254 816 088 412 ticks
bs3-memalloc-1:   Page access time                                          :            7 292 ns/page
bs3-memalloc-1:   Page access time in ticks                                 :           21 843 ticks/page
bs3-memalloc-1:   Access thruput                                            :          137 119 pages/s
bs3-memalloc-1:   Access thruput in MiBs                                    :              535 MB/s
bs3-memalloc-1: 2nd access                                                  : PASSED
bs3-memalloc-1:   Access elapsed                                            :      112 963 925 ns
bs3-memalloc-1:   Access elapsed in ticks                                   :      338 284 436 ticks
bs3-memalloc-1:   Page access time                                          :                9 ns/page
bs3-memalloc-1:   Page access time in ticks                                 :               28 ticks/page
bs3-memalloc-1:   Access thruput                                            :      103 266 666 pages/s
bs3-memalloc-1:   Access thruput in MiBs                                    :          403 385 MB/s
bs3-memalloc-1: 3rd access                                                  : PASSED
bs3-memalloc-1: SUCCESS
 * @endverbatim
 *
 * NEM w/ simplified memory and but no MEM_LARGE_PAGES:
 * @verbatim
bs3-memalloc-1: From 0x100000000 to 0xc20000000
bs3-memalloc-1:   Pages                                                     :       11 665 408 pages
bs3-memalloc-1:   MiBs                                                      :           45 568 MB
bs3-memalloc-1:   Alloc elapsed                                             :   90 062 027 900 ns
bs3-memalloc-1:   Alloc elapsed in ticks                                    :  269 754 826 466 ticks
bs3-memalloc-1:   Page alloc time                                           :            7 720 ns/page
bs3-memalloc-1:   Page alloc time in ticks                                  :           23 124 ticks/page
bs3-memalloc-1:   Alloc thruput                                             :          129 526 pages/s
bs3-memalloc-1:   Alloc thruput in MiBs                                     :              505 MB/s
bs3-memalloc-1: Allocation speed                                            : PASSED
bs3-memalloc-1:   Access elapsed                                            :    3 596 017 220 ns
bs3-memalloc-1:   Access elapsed in ticks                                   :   10 770 732 620 ticks
bs3-memalloc-1:   Page access time                                          :              308 ns/page
bs3-memalloc-1:   Page access time in ticks                                 :              923 ticks/page
bs3-memalloc-1:   Access thruput                                            :        3 243 980 pages/s
bs3-memalloc-1:   Access thruput in MiBs                                    :           12 671 MB/s
bs3-memalloc-1: 2nd access                                                  : PASSED
bs3-memalloc-1:   Access elapsed                                            :      133 060 160 ns
bs3-memalloc-1:   Access elapsed in ticks                                   :      398 459 884 ticks
bs3-memalloc-1:   Page access time                                          :               11 ns/page
bs3-memalloc-1:   Page access time in ticks                                 :               34 ticks/page
bs3-memalloc-1:   Access thruput                                            :       87 670 178 pages/s
bs3-memalloc-1:   Access thruput in MiBs                                    :          342 461 MB/s
bs3-memalloc-1: 3rd access                                                  : PASSED
 * @endverbatim
 *
 * Same everything but native VT-x and VBox (stripped output a little):
 * @verbatim
bs3-memalloc-1: From 0x100000000 to 0xc20000000
bs3-memalloc-1:   Pages                                                     :       11 665 408 pages
bs3-memalloc-1:   MiBs                                                      :           45 568 MB
bs3-memalloc-1:   Alloc elapsed                                             :      776 111 427 ns
bs3-memalloc-1:   Alloc elapsed in ticks                                    :    2 323 267 035 ticks
bs3-memalloc-1:   Page alloc time                                           :               66 ns/page
bs3-memalloc-1:   Page alloc time in ticks                                  :              199 ticks/page
bs3-memalloc-1:   Alloc thruput                                             :       15 030 584 pages/s
bs3-memalloc-1:   Alloc thruput in MiBs                                     :           58 713 MB/s
bs3-memalloc-1: Allocation speed                                            : PASSED
bs3-memalloc-1:   Access elapsed                                            :      112 141 904 ns
bs3-memalloc-1:   Access elapsed in ticks                                   :      335 751 077 ticks
bs3-memalloc-1:   Page access time                                          :                9 ns/page
bs3-memalloc-1:   Page access time in ticks                                 :               28 ticks/page
bs3-memalloc-1:   Access thruput                                            :      104 023 630 pages/s
bs3-memalloc-1:   Access thruput in MiBs                                    :          406 342 MB/s
bs3-memalloc-1: 2nd access                                                  : PASSED
bs3-memalloc-1:   Access elapsed                                            :      112 023 049 ns
bs3-memalloc-1:   Access elapsed in ticks                                   :      335 418 343 ticks
bs3-memalloc-1:   Page access time                                          :                9 ns/page
bs3-memalloc-1:   Page access time in ticks                                 :               28 ticks/page
bs3-memalloc-1:   Access thruput                                            :      104 133 998 pages/s
bs3-memalloc-1:   Access thruput in MiBs                                    :          406 773 MB/s
bs3-memalloc-1: 3rd access                                                  : PASSED
 * @endverbatim
 *
 * VBox with large pages disabled:
 * @verbatim
bs3-memalloc-1: From 0x100000000 to 0xc20000000
bs3-memalloc-1:   Pages                                                     :       11 665 408 pages
bs3-memalloc-1:   MiBs                                                      :           45 568 MB
bs3-memalloc-1:   Alloc elapsed                                             :   50 986 588 028 ns
bs3-memalloc-1:   Alloc elapsed in ticks                                    :  152 714 862 044 ticks
bs3-memalloc-1:   Page alloc time                                           :            4 370 ns/page
bs3-memalloc-1:   Page alloc time in ticks                                  :           13 091 ticks/page
bs3-memalloc-1:   Alloc thruput                                             :          228 793 pages/s
bs3-memalloc-1:   Alloc thruput in MiBs                                     :              893 MB/s
bs3-memalloc-1: Allocation speed                                            : PASSED
bs3-memalloc-1:   Access elapsed                                            :    2 849 641 741 ns
bs3-memalloc-1:   Access elapsed in ticks                                   :    8 535 372 249 ticks
bs3-memalloc-1:   Page access time                                          :              244 ns/page
bs3-memalloc-1:   Page access time in ticks                                 :              731 ticks/page
bs3-memalloc-1:   Access thruput                                            :        4 093 640 pages/s
bs3-memalloc-1:   Access thruput in MiBs                                    :           15 990 MB/s
bs3-memalloc-1: 2nd access                                                  : PASSED
bs3-memalloc-1:   Access elapsed                                            :    2 866 960 770 ns
bs3-memalloc-1:   Access elapsed in ticks                                   :    8 587 097 799 ticks
bs3-memalloc-1:   Page access time                                          :              245 ns/page
bs3-memalloc-1:   Page access time in ticks                                 :              736 ticks/page
bs3-memalloc-1:   Access thruput                                            :        4 068 910 pages/s
bs3-memalloc-1:   Access thruput in MiBs                                    :           15 894 MB/s
bs3-memalloc-1: 3rd access                                                  : PASSED
 * @endverbatim
 *
 * Comparing large pages, therer is an allocation speed difference of two order
 * of magnitude.  When disabling large pages in VBox the allocation numbers are
 * closer, and the is clear from the 2nd and 3rd access tests that VBox doesn't
 * spend enough memory on nested page tables as Hyper-V does.  The similar 2nd
 * and 3rd access numbers the two large page testruns seems to hint strongly at
 * Hyper-V eventually getting the large pages in place too, only that it sucks
 * hundredfold in the setting up phase.
 *
 *
 *
 * @section sec_nem_win_impl    Our implementation.
 *
 * We set out with the goal of wanting to run as much as possible in ring-0,
 * reasoning that this would give use the best performance.
 *
 * This goal was approached gradually, starting out with a pure WinHvPlatform
 * implementation, gradually replacing parts: register access, guest memory
 * handling, running virtual processors.  Then finally moving it all into
 * ring-0, while keeping most of it configurable so that we could make
 * comparisons (see NEMInternal.h and nemR3NativeRunGC()).
 *
 *
 * @subsection subsect_nem_win_impl_ioctl       VID.SYS I/O control calls
 *
 * To run things in ring-0 we need to talk directly to VID.SYS thru its I/O
 * control interface.  Looking at changes between like build 17083 and 17101 (if
 * memory serves) a set of the VID I/O control numbers shifted a little, which
 * means we need to determin them dynamically.  We currently do this by hooking
 * the NtDeviceIoControlFile API call from VID.DLL and snooping up the
 * parameters when making dummy calls to relevant APIs.  (We could also
 * disassemble the relevant APIs and try fish out the information from that, but
 * this is way simpler.)
 *
 * Issuing I/O control calls from ring-0 is facing a small challenge with
 * respect to direct buffering.  When using direct buffering the device will
 * typically check that the buffer is actually in the user address space range
 * and reject kernel addresses.  Fortunately, we've got the cross context VM
 * structure that is mapped into both kernel and user space, it's also locked
 * and safe to access from kernel space.  So, we place the I/O control buffers
 * in the per-CPU part of it (NEMCPU::uIoCtlBuf) and give the driver the user
 * address if direct access buffering or kernel address if not.
 *
 * The I/O control calls are 'abstracted' in the support driver, see
 * SUPR0IoCtlSetupForHandle(), SUPR0IoCtlPerform() and SUPR0IoCtlCleanup().
 *
 *
 * @subsection subsect_nem_win_impl_cpumctx     CPUMCTX
 *
 * Since the CPU state needs to live in Hyper-V when executing, we probably
 * should not transfer more than necessary when handling VMEXITs.  To help us
 * manage this CPUMCTX got a new field CPUMCTX::fExtrn that to indicate which
 * part of the state is currently externalized (== in Hyper-V).
 *
 *
 * @subsection sec_nem_win_benchmarks           Benchmarks.
 *
 * @subsubsection subsect_nem_win_benchmarks_bs2t1      17134/2018-06-22: Bootsector2-test1
 *
 * This is ValidationKit/bootsectors/bootsector2-test1.asm as of 2018-06-22
 * (internal r123172) running a the release build of VirtualBox from the same
 * source, though with exit optimizations disabled.  Host is AMD Threadripper 1950X
 * running out an up to date 64-bit Windows 10 build 17134.
 *
 * The base line column is using the official WinHv API for everything but physical
 * memory mapping.  The 2nd column is the default NEM/win configuration where we
 * put the main execution loop in ring-0, using hypercalls when we can and VID for
 * managing execution.  The 3rd column is regular VirtualBox using AMD-V directly,
 * hyper-V is disabled, main execution loop in ring-0.
 *
 * @verbatim
TESTING...                                                           WinHv API           Hypercalls + VID    VirtualBox AMD-V
  32-bit paged protected mode, CPUID                        :          108 874 ins/sec   113% / 123 602      1198% / 1 305 113
  32-bit pae protected mode, CPUID                          :          106 722 ins/sec   115% / 122 740      1232% / 1 315 201
  64-bit long mode, CPUID                                   :          106 798 ins/sec   114% / 122 111      1198% / 1 280 404
  16-bit unpaged protected mode, CPUID                      :          106 835 ins/sec   114% / 121 994      1216% / 1 299 665
  32-bit unpaged protected mode, CPUID                      :          105 257 ins/sec   115% / 121 772      1235% / 1 300 860
  real mode, CPUID                                          :          104 507 ins/sec   116% / 121 800      1228% / 1 283 848
CPUID EAX=1                                                 : PASSED
  32-bit paged protected mode, RDTSC                        :       99 581 834 ins/sec   100% / 100 323 307    93% / 93 473 299
  32-bit pae protected mode, RDTSC                          :       99 620 585 ins/sec   100% / 99 960 952     84% / 83 968 839
  64-bit long mode, RDTSC                                   :      100 540 009 ins/sec   100% / 100 946 372    93% / 93 652 826
  16-bit unpaged protected mode, RDTSC                      :       99 688 473 ins/sec   100% / 100 097 751    76% / 76 281 287
  32-bit unpaged protected mode, RDTSC                      :       98 385 857 ins/sec   102% / 100 510 404    94% / 93 379 536
  real mode, RDTSC                                          :      100 087 967 ins/sec   101% / 101 386 138    93% / 93 234 999
RDTSC                                                       : PASSED
  32-bit paged protected mode, Read CR4                     :        2 156 102 ins/sec    98% / 2 121 967   17114% / 369 009 009
  32-bit pae protected mode, Read CR4                       :        2 163 820 ins/sec    98% / 2 133 804   17469% / 377 999 261
  64-bit long mode, Read CR4                                :        2 164 822 ins/sec    98% / 2 128 698   18875% / 408 619 313
  16-bit unpaged protected mode, Read CR4                   :        2 162 367 ins/sec   100% / 2 168 508   17132% / 370 477 568
  32-bit unpaged protected mode, Read CR4                   :        2 163 189 ins/sec   100% / 2 169 808   16768% / 362 734 679
  real mode, Read CR4                                       :        2 162 436 ins/sec   100% / 2 164 914   15551% / 336 288 998
Read CR4                                                    : PASSED
  real mode, 32-bit IN                                      :          104 649 ins/sec   118% / 123 513      1028% / 1 075 831
  real mode, 32-bit OUT                                     :          107 102 ins/sec   115% / 123 660       982% / 1 052 259
  real mode, 32-bit IN-to-ring-3                            :          105 697 ins/sec    98% / 104 471       201% / 213 216
  real mode, 32-bit OUT-to-ring-3                           :          105 830 ins/sec    98% / 104 598       198% / 210 495
  16-bit unpaged protected mode, 32-bit IN                  :          104 855 ins/sec   117% / 123 174      1029% / 1 079 591
  16-bit unpaged protected mode, 32-bit OUT                 :          107 529 ins/sec   115% / 124 250       992% / 1 067 053
  16-bit unpaged protected mode, 32-bit IN-to-ring-3        :          106 337 ins/sec   103% / 109 565       196% / 209 367
  16-bit unpaged protected mode, 32-bit OUT-to-ring-3       :          107 558 ins/sec   100% / 108 237       191% / 206 387
  32-bit unpaged protected mode, 32-bit IN                  :          106 351 ins/sec   116% / 123 584      1016% / 1 081 325
  32-bit unpaged protected mode, 32-bit OUT                 :          106 424 ins/sec   116% / 124 252       995% / 1 059 408
  32-bit unpaged protected mode, 32-bit IN-to-ring-3        :          104 035 ins/sec   101% / 105 305       202% / 210 750
  32-bit unpaged protected mode, 32-bit OUT-to-ring-3       :          103 831 ins/sec   102% / 106 919       205% / 213 198
  32-bit paged protected mode, 32-bit IN                    :          103 356 ins/sec   119% / 123 870      1041% / 1 076 463
  32-bit paged protected mode, 32-bit OUT                   :          107 177 ins/sec   115% / 124 302       998% / 1 069 655
  32-bit paged protected mode, 32-bit IN-to-ring-3          :          104 491 ins/sec   100% / 104 744       200% / 209 264
  32-bit paged protected mode, 32-bit OUT-to-ring-3         :          106 603 ins/sec    97% / 103 849       197% / 210 219
  32-bit pae protected mode, 32-bit IN                      :          105 923 ins/sec   115% / 122 759      1041% / 1 103 261
  32-bit pae protected mode, 32-bit OUT                     :          107 083 ins/sec   117% / 126 057      1024% / 1 096 667
  32-bit pae protected mode, 32-bit IN-to-ring-3            :          106 114 ins/sec    97% / 103 496       199% / 211 312
  32-bit pae protected mode, 32-bit OUT-to-ring-3           :          105 675 ins/sec    96% / 102 096       198% / 209 890
  64-bit long mode, 32-bit IN                               :          105 800 ins/sec   113% / 120 006      1013% / 1 072 116
  64-bit long mode, 32-bit OUT                              :          105 635 ins/sec   113% / 120 375       997% / 1 053 655
  64-bit long mode, 32-bit IN-to-ring-3                     :          105 274 ins/sec    95% / 100 763       197% / 208 026
  64-bit long mode, 32-bit OUT-to-ring-3                    :          106 262 ins/sec    94% / 100 749       196% / 209 288
NOP I/O Port Access                                         : PASSED
  32-bit paged protected mode, 32-bit read                  :           57 687 ins/sec   119% / 69 136       1197% / 690 548
  32-bit paged protected mode, 32-bit write                 :           57 957 ins/sec   118% / 68 935       1183% / 685 930
  32-bit paged protected mode, 32-bit read-to-ring-3        :           57 958 ins/sec    95% / 55 432        276% / 160 505
  32-bit paged protected mode, 32-bit write-to-ring-3       :           57 922 ins/sec   100% / 58 340        304% / 176 464
  32-bit pae protected mode, 32-bit read                    :           57 478 ins/sec   119% / 68 453       1141% / 656 159
  32-bit pae protected mode, 32-bit write                   :           57 226 ins/sec   118% / 68 097       1157% / 662 504
  32-bit pae protected mode, 32-bit read-to-ring-3          :           57 582 ins/sec    94% / 54 651        268% / 154 867
  32-bit pae protected mode, 32-bit write-to-ring-3         :           57 697 ins/sec   100% / 57 750        299% / 173 030
  64-bit long mode, 32-bit read                             :           57 128 ins/sec   118% / 67 779       1071% / 611 949
  64-bit long mode, 32-bit write                            :           57 127 ins/sec   118% / 67 632       1084% / 619 395
  64-bit long mode, 32-bit read-to-ring-3                   :           57 181 ins/sec    94% / 54 123        265% / 151 937
  64-bit long mode, 32-bit write-to-ring-3                  :           57 297 ins/sec    99% / 57 286        294% / 168 694
  16-bit unpaged protected mode, 32-bit read                :           58 827 ins/sec   118% / 69 545       1185% / 697 602
  16-bit unpaged protected mode, 32-bit write               :           58 678 ins/sec   118% / 69 442       1183% / 694 387
  16-bit unpaged protected mode, 32-bit read-to-ring-3      :           57 841 ins/sec    96% / 55 730        275% / 159 163
  16-bit unpaged protected mode, 32-bit write-to-ring-3     :           57 855 ins/sec   101% / 58 834        304% / 176 169
  32-bit unpaged protected mode, 32-bit read                :           58 063 ins/sec   120% / 69 690       1233% / 716 444
  32-bit unpaged protected mode, 32-bit write               :           57 936 ins/sec   120% / 69 633       1199% / 694 753
  32-bit unpaged protected mode, 32-bit read-to-ring-3      :           58 451 ins/sec    96% / 56 183        273% / 159 972
  32-bit unpaged protected mode, 32-bit write-to-ring-3     :           58 962 ins/sec    99% / 58 955        298% / 175 936
  real mode, 32-bit read                                    :           58 571 ins/sec   118% / 69 478       1160% / 679 917
  real mode, 32-bit write                                   :           58 418 ins/sec   118% / 69 320       1185% / 692 513
  real mode, 32-bit read-to-ring-3                          :           58 072 ins/sec    96% / 55 751        274% / 159 145
  real mode, 32-bit write-to-ring-3                         :           57 870 ins/sec   101% / 58 755        307% / 178 042
NOP MMIO Access                                             : PASSED
SUCCESS
 * @endverbatim
 *
 * What we see here is:
 *
 *  - The WinHv API approach is 10 to 12 times slower for exits we can
 *    handle directly in ring-0 in the VBox AMD-V code.
 *
 *  - The WinHv API approach is 2 to 3 times slower for exits we have to
 *    go to ring-3 to handle with the VBox AMD-V code.
 *
 *  - By using hypercalls and VID.SYS from ring-0 we gain between
 *    13% and 20% over the WinHv API on exits handled in ring-0.
 *
 *  - For exits requiring ring-3 handling are between 6% slower and 3% faster
 *    than the WinHv API.
 *
 *
 * As a side note, it looks like Hyper-V doesn't let the guest read CR4 but
 * triggers exits all the time.  This isn't all that important these days since
 * OSes like Linux cache the CR4 value specifically to avoid these kinds of exits.
 *
 *
 * @subsubsection subsect_nem_win_benchmarks_bs2t1u1   17134/2018-10-02: Bootsector2-test1
 *
 * Update on 17134.  While expectantly testing a couple of newer builds (17758,
 * 17763) hoping for some increases in performance, the numbers turned out
 * altogether worse than the June test run.  So, we went back to the 1803
 * (17134) installation, made sure it was fully up to date (as per 2018-10-02)
 * and re-tested.
 *
 * The numbers had somehow turned significantly worse over the last 3-4 months,
 * dropping around  70%  for the WinHv API test, more for Hypercalls + VID.
 *
 * @verbatim
TESTING...                                                           WinHv API           Hypercalls + VID    VirtualBox AMD-V *
  32-bit paged protected mode, CPUID                        :           33 270 ins/sec        33 154
  real mode, CPUID                                          :           33 534 ins/sec        32 711
  [snip]
  32-bit paged protected mode, RDTSC                        :      102 216 011 ins/sec    98 225 419
  real mode, RDTSC                                          :      102 492 243 ins/sec    98 225 419
  [snip]
  32-bit paged protected mode, Read CR4                     :        2 096 165 ins/sec     2 123 815
  real mode, Read CR4                                       :        2 081 047 ins/sec     2 075 151
  [snip]
  32-bit paged protected mode, 32-bit IN                    :           32 739 ins/sec        33 655
  32-bit paged protected mode, 32-bit OUT                   :           32 702 ins/sec        33 777
  32-bit paged protected mode, 32-bit IN-to-ring-3          :           32 579 ins/sec        29 985
  32-bit paged protected mode, 32-bit OUT-to-ring-3         :           32 750 ins/sec        29 757
  [snip]
  32-bit paged protected mode, 32-bit read                  :           20 042 ins/sec        21 489
  32-bit paged protected mode, 32-bit write                 :           20 036 ins/sec        21 493
  32-bit paged protected mode, 32-bit read-to-ring-3        :           19 985 ins/sec        19 143
  32-bit paged protected mode, 32-bit write-to-ring-3       :           19 972 ins/sec        19 595

 * @endverbatim
 *
 * Suspects are security updates and/or microcode updates installed since then.
 * Given that the RDTSC and CR4 numbers are reasonably unchanges, it seems that
 * the Hyper-V core loop (in hvax64.exe) aren't affected.  Our ring-0 runloop
 * is equally affected as the ring-3 based runloop, so it cannot be ring
 * switching as such (unless the ring-0 loop is borked and we didn't notice yet).
 *
 * The issue is probably in the thread / process switching area, could be
 * something special for hyper-V interrupt delivery or worker thread switching.
 *
 * Really wish this thread ping-pong going on in VID.SYS could be eliminated!
 *
 *
 * @subsubsection subsect_nem_win_benchmarks_bs2t1u2   17763: Bootsector2-test1
 *
 * Some preliminary numbers for build 17763 on the 3.4 GHz AMD 1950X, the second
 * column will improve we get time to have a look the register page.
 *
 * There is a  50%  performance loss here compared to the June numbers with
 * build 17134.  The RDTSC numbers hits that it isn't in the Hyper-V core
 * (hvax64.exe), but something on the NT side.
 *
 * Clearing bit 20 in nt!KiSpeculationFeatures speeds things up (i.e. changing
 * the dword from 0x00300065 to 0x00200065 in windbg).  This is checked by
 * nt!KePrepareToDispatchVirtualProcessor, making it a no-op if the flag is
 * clear.  winhvr!WinHvpVpDispatchLoop call that function before making
 * hypercall 0xc2, which presumably does the heavy VCpu lifting in hvcax64.exe.
 *
 * @verbatim
TESTING...                                                           WinHv API           Hypercalls + VID  clr(bit-20) + WinHv API
  32-bit paged protected mode, CPUID                        :           54 145 ins/sec        51 436               130 076
  real mode, CPUID                                          :           54 178 ins/sec        51 713               130 449
  [snip]
  32-bit paged protected mode, RDTSC                        :       98 927 639 ins/sec   100 254 552           100 549 882
  real mode, RDTSC                                          :       99 601 206 ins/sec   100 886 699           100 470 957
  [snip]
  32-bit paged protected mode, 32-bit IN                    :           54 621 ins/sec        51 524               128 294
  32-bit paged protected mode, 32-bit OUT                   :           54 870 ins/sec        51 671               129 397
  32-bit paged protected mode, 32-bit IN-to-ring-3          :           54 624 ins/sec        43 964               127 874
  32-bit paged protected mode, 32-bit OUT-to-ring-3         :           54 803 ins/sec        44 087               129 443
  [snip]
  32-bit paged protected mode, 32-bit read                  :           28 230 ins/sec        34 042                48 113
  32-bit paged protected mode, 32-bit write                 :           27 962 ins/sec        34 050                48 069
  32-bit paged protected mode, 32-bit read-to-ring-3        :           27 841 ins/sec        28 397                48 146
  32-bit paged protected mode, 32-bit write-to-ring-3       :           27 896 ins/sec        29 455                47 970
 * @endverbatim
 *
 *
 * @subsubsection subsect_nem_win_benchmarks_w2k    17134/2018-06-22: Windows 2000 Boot & Shutdown
 *
 * Timing the startup and automatic shutdown of a Windows 2000 SP4 guest serves
 * as a real world benchmark and example of why exit performance is import.  When
 * Windows 2000 boots up is doing a lot of VGA redrawing of the boot animation,
 * which is very costly.  Not having installed guest additions leaves it in a VGA
 * mode after the bootup sequence is done, keep up the screen access expenses,
 * though the graphics driver more economical than the bootvid code.
 *
 * The VM was configured to automatically logon.  A startup script was installed
 * to perform the automatic shuting down and powering off the VM (thru
 * vts_shutdown.exe -f -p).  An offline snapshot of the VM was taken an restored
 * before each test run.  The test time run time is calculated from the monotonic
 * VBox.log timestamps, starting with the state change to 'RUNNING' and stopping
 * at 'POWERING_OFF'.
 *
 * The host OS and VirtualBox build is the same as for the bootsector2-test1
 * scenario.
 *
 * Results:
 *
 *  - WinHv API for all but physical page mappings:
 *          32 min 12.19 seconds
 *
 *  - The default NEM/win configuration where we put the main execution loop
 *    in ring-0, using hypercalls when we can and VID for managing execution:
 *          3 min 23.18 seconds
 *
 *  - Regular VirtualBox using AMD-V directly, hyper-V is disabled, main
 *    execution loop in ring-0:
 *          58.09 seconds
 *
 *  - WinHv API with exit history based optimizations:
 *          58.66 seconds
 *
 *  - Hypercall + VID.SYS with exit history base optimizations:
 *          58.94 seconds
 *
 * With a well above average machine needing over half an hour for booting a
 * nearly 20 year old guest kind of says it all.  The 13%-20% exit performance
 * increase we get by using hypercalls and VID.SYS directly pays off a lot here.
 * The 3m23s is almost acceptable in comparison to the half an hour.
 *
 * The similarity between the last three results strongly hits at windows 2000
 * doing a lot of waiting during boot and shutdown and isn't the best testcase
 * once a basic performance level is reached.
 *
 *
 * @subsubsection subsection_iem_win_benchmarks_deb9_nat    Debian 9 NAT performance
 *
 * This benchmark is about network performance over NAT from a 64-bit Debian 9
 * VM with a single CPU.  For network performance measurements, we use our own
 * NetPerf tool (ValidationKit/utils/network/NetPerf.cpp) to measure latency
 * and throughput.
 *
 * The setups, builds and configurations are as in the previous benchmarks
 * (release r123172 on 1950X running 64-bit W10/17134 (2016-06-xx).  Please note
 * that the exit optimizations hasn't yet been in tuned with NetPerf in mind.
 *
 * The NAT network setup was selected here since it's the default one and the
 * slowest one.  There is quite a bit of IPC with worker threads and packet
 * processing involved.
 *
 * Latency test is first up.  This is a classic back and forth between the two
 * NetPerf instances, where the key measurement is the roundrip latency.  The
 * values here are the lowest result over 3-6 runs.
 *
 * Against host system:
 *   - 152 258 ns/roundtrip - 100% - regular VirtualBox SVM
 *   - 271 059 ns/roundtrip - 178% - Hypercalls + VID.SYS in ring-0 with exit optimizations.
 *   - 280 149 ns/roundtrip - 184% - Hypercalls + VID.SYS in ring-0
 *   - 317 735 ns/roundtrip - 209% - Win HV API with exit optimizations.
 *   - 342 440 ns/roundtrip - 225% - Win HV API
 *
 * Against a remote Windows 10 system over a 10Gbps link:
 *   - 243 969 ns/roundtrip - 100% - regular VirtualBox SVM
 *   - 384 427 ns/roundtrip - 158% - Win HV API with exit optimizations.
 *   - 402 411 ns/roundtrip - 165% - Hypercalls + VID.SYS in ring-0
 *   - 406 313 ns/roundtrip - 167% - Win HV API
 *   - 413 160 ns/roundtrip - 169% - Hypercalls + VID.SYS in ring-0 with exit optimizations.
 *
 * What we see here is:
 *
 *   - Consistent and signficant latency increase using Hyper-V compared
 *     to directly harnessing AMD-V ourselves.
 *
 *   - When talking to the host, it's clear that the hypercalls + VID.SYS
 *     in ring-0 method pays off.
 *
 *   - When talking to a different host, the numbers are closer and it
 *     is not longer clear which Hyper-V execution method is better.
 *
 *
 * Throughput benchmarks are performed by one side pushing data full throttle
 * for 10 seconds (minus a 1 second at each end of the test), then reversing
 * the roles and measuring it in the other direction.  The tests ran 3-5 times
 * and below are the highest and lowest results in each direction.
 *
 * Receiving from host system:
 *   - Regular VirtualBox SVM:
 *      Max: 96 907 549 bytes/s - 100%
 *      Min: 86 912 095 bytes/s - 100%
 *   - Hypercalls + VID.SYS in ring-0:
 *      Max: 84 036 544 bytes/s - 87%
 *      Min: 64 978 112 bytes/s - 75%
 *   - Hypercalls + VID.SYS in ring-0 with exit optimizations:
 *      Max: 77 760 699 bytes/s - 80%
 *      Min: 72 677 171 bytes/s - 84%
 *   - Win HV API with exit optimizations:
 *      Max: 64 465 905 bytes/s - 67%
 *      Min: 62 286 369 bytes/s - 72%
 *   - Win HV API:
 *      Max: 62 466 631 bytes/s - 64%
 *      Min: 61 362 782 bytes/s - 70%
 *
 * Sending to the host system:
 *   - Regular VirtualBox SVM:
 *      Max: 87 728 652 bytes/s - 100%
 *      Min: 86 923 198 bytes/s - 100%
 *   - Hypercalls + VID.SYS in ring-0:
 *      Max: 84 280 749 bytes/s - 96%
 *      Min: 78 369 842 bytes/s - 90%
 *   - Hypercalls + VID.SYS in ring-0 with exit optimizations:
 *      Max: 84 119 932 bytes/s - 96%
 *      Min: 77 396 811 bytes/s - 89%
 *   - Win HV API:
 *      Max: 81 714 377 bytes/s - 93%
 *      Min: 78 697 419 bytes/s - 91%
 *   - Win HV API with exit optimizations:
 *      Max: 80 502 488 bytes/s - 91%
 *      Min: 71 164 978 bytes/s - 82%
 *
 * Receiving from a remote Windows 10 system over a 10Gbps link:
 *   - Hypercalls + VID.SYS in ring-0:
 *      Max: 115 346 922 bytes/s - 136%
 *      Min: 112 912 035 bytes/s - 137%
 *   - Regular VirtualBox SVM:
 *      Max:  84 517 504 bytes/s - 100%
 *      Min:  82 597 049 bytes/s - 100%
 *   - Hypercalls + VID.SYS in ring-0 with exit optimizations:
 *      Max:  77 736 251 bytes/s - 92%
 *      Min:  73 813 784 bytes/s - 89%
 *   - Win HV API with exit optimizations:
 *      Max:  63 035 587 bytes/s - 75%
 *      Min:  57 538 380 bytes/s - 70%
 *   - Win HV API:
 *      Max:  62 279 185 bytes/s - 74%
 *      Min:  56 813 866 bytes/s - 69%
 *
 * Sending to a remote Windows 10 system over a 10Gbps link:
 *   - Win HV API with exit optimizations:
 *      Max: 116 502 357 bytes/s - 103%
 *      Min:  49 046 550 bytes/s - 59%
 *   - Regular VirtualBox SVM:
 *      Max: 113 030 991 bytes/s - 100%
 *      Min:  83 059 511 bytes/s - 100%
 *   - Hypercalls + VID.SYS in ring-0:
 *      Max: 106 435 031 bytes/s - 94%
 *      Min:  47 253 510 bytes/s - 57%
 *   - Hypercalls + VID.SYS in ring-0 with exit optimizations:
 *      Max:  94 842 287 bytes/s - 84%
 *      Min:  68 362 172 bytes/s - 82%
 *   - Win HV API:
 *      Max:  65 165 225 bytes/s - 58%
 *      Min:  47 246 573 bytes/s - 57%
 *
 * What we see here is:
 *
 *   - Again consistent numbers when talking to the host.  Showing that the
 *     ring-0 approach is preferable to the ring-3 one.
 *
 *   - Again when talking to a remote host, things get more difficult to
 *     make sense of.  The spread is larger and direct AMD-V gets beaten by
 *     a different the Hyper-V approaches in each direction.
 *
 *   - However, if we treat the first entry (remote host) as weird spikes, the
 *     other entries are consistently worse compared to direct AMD-V.  For the
 *     send case we get really bad results for WinHV.
 *
 */

