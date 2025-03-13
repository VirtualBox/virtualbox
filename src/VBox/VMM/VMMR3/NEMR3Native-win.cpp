/* $Id: NEMR3Native-win.cpp 108741 2025-03-13 11:24:03Z knut.osmundsen@oracle.com $ */
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
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#include <VBox/vmm/apic.h>
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
static decltype(WHvGetVirtualProcessorRegisters) *  g_pfnWHvGetVirtualProcessorRegisters;
static decltype(WHvSetVirtualProcessorRegisters) *  g_pfnWHvSetVirtualProcessorRegisters;
static decltype(WHvResumePartitionTime)            *g_pfnWHvResumePartitionTime;
static decltype(WHvSuspendPartitionTime)           *g_pfnWHvSuspendPartitionTime;
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

# define VidMessageSlotHandleAndGetNext             g_pfnVidMessageSlotHandleAndGetNext
# define VidStartVirtualProcessor                   g_pfnVidStartVirtualProcessor
# define VidStopVirtualProcessor                    g_pfnVidStopVirtualProcessor

#endif

/** WHV_MEMORY_ACCESS_TYPE names */
static const char * const g_apszWHvMemAccesstypes[4] = { "read", "write", "exec", "!undefined!" };


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLINLINE(int) nemR3NativeGCPhys2R3PtrReadOnly(PVM pVM, RTGCPHYS GCPhys, const void **ppv);
DECLINLINE(int) nemR3NativeGCPhys2R3PtrWriteable(PVM pVM, RTGCPHYS GCPhys, void **ppv);

/*
 * Instantate the code we used to share with ring-0.
 */
#include "../VMMAll/NEMAllNativeTemplate-win.cpp.h"



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

                LogRel(("NEM:  %s: Failed to import %s!%s: %Rrc",
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
DECLINLINE(HRESULT) WHvGetCapabilityWrapper(WHV_CAPABILITY_CODE enmCap, WHV_CAPABILITY *pOutput, uint32_t cbOutput)
{
    return g_pfnWHvGetCapability(enmCap, pOutput, cbOutput, NULL);
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
    /**
     * @todo Someone at Microsoft please explain weird API design:
     *   1. Pointless CapabilityCode duplication int the output;
     *   2. No output size.
     */
    WHV_CAPABILITY Caps;
    RT_ZERO(Caps);
    SetLastError(0);
    HRESULT hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeHypervisorPresent, &Caps, sizeof(Caps));
    DWORD   rcWin = GetLastError();
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeHypervisorPresent failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    if (!Caps.HypervisorPresent)
    {
        if (!RTPathExists(RTPATH_NT_PASSTHRU_PREFIX "Device\\VidExo"))
            return RTErrInfoSetF(pErrInfo, VERR_NEM_NOT_AVAILABLE,
                                 "WHvCapabilityCodeHypervisorPresent is FALSE! Make sure you have enabled the 'Windows Hypervisor Platform' feature.");
        return RTErrInfoSetF(pErrInfo, VERR_NEM_NOT_AVAILABLE, "WHvCapabilityCodeHypervisorPresent is FALSE! (%u)", rcWin);
    }
    LogRel(("NEM: WHvCapabilityCodeHypervisorPresent is TRUE, so this might work...\n"));


    /*
     * Check what extended VM exits are supported.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeExtendedVmExits, &Caps, sizeof(Caps));
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeExtendedVmExits failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    NEM_LOG_REL_CAP_EX("WHvCapabilityCodeExtendedVmExits", "%'#018RX64", Caps.ExtendedVmExits.AsUINT64);
#define NEM_LOG_REL_CAP_VM_EXIT(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.ExtendedVmExits.a_Field)
    NEM_LOG_REL_CAP_VM_EXIT(X64CpuidExit);
    NEM_LOG_REL_CAP_VM_EXIT(X64MsrExit);
    NEM_LOG_REL_CAP_VM_EXIT(ExceptionExit);
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_19040
    NEM_LOG_REL_CAP_VM_EXIT(X64RdtscExit);
    NEM_LOG_REL_CAP_VM_EXIT(X64ApicSmiExitTrap);
    NEM_LOG_REL_CAP_VM_EXIT(HypercallExit);
    NEM_LOG_REL_CAP_VM_EXIT(X64ApicInitSipiExitTrap);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN11_22000 /** @todo Could some of these may have been added earlier... */
    NEM_LOG_REL_CAP_VM_EXIT(X64ApicWriteLint0ExitTrap);
    NEM_LOG_REL_CAP_VM_EXIT(X64ApicWriteLint1ExitTrap);
    NEM_LOG_REL_CAP_VM_EXIT(X64ApicWriteSvrExitTrap);
    NEM_LOG_REL_CAP_VM_EXIT(UnknownSynicConnection);
    NEM_LOG_REL_CAP_VM_EXIT(RetargetUnknownVpciDevice);
    NEM_LOG_REL_CAP_VM_EXIT(X64ApicWriteLdrExitTrap);
    NEM_LOG_REL_CAP_VM_EXIT(X64ApicWriteDfrExitTrap);
    NEM_LOG_REL_CAP_VM_EXIT(GpaAccessFaultExit);
#endif
#undef  NEM_LOG_REL_CAP_VM_EXIT
    uint64_t const fKnownVmExits = RT_BIT_64(15) - 1U;
    if (Caps.ExtendedVmExits.AsUINT64 & ~fKnownVmExits)
        NEM_LOG_REL_CAP_SUB_EX("Unknown VM exit defs", "%#RX64", Caps.ExtendedVmExits.AsUINT64 & ~fKnownVmExits);
    pVM->nem.s.fExtendedMsrExit   = RT_BOOL(Caps.ExtendedVmExits.X64MsrExit);
    pVM->nem.s.fExtendedCpuIdExit = RT_BOOL(Caps.ExtendedVmExits.X64CpuidExit);
    pVM->nem.s.fExtendedXcptExit  = RT_BOOL(Caps.ExtendedVmExits.ExceptionExit);
    /** @todo RECHECK: WHV_EXTENDED_VM_EXITS typedef. */

    /*
     * Check features in case they end up defining any.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeFeatures, &Caps, sizeof(Caps));
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeFeatures failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    NEM_LOG_REL_CAP_EX("WHvCapabilityCodeFeatures", "%'#018RX64", Caps.Features.AsUINT64);
#define NEM_LOG_REL_CAP_FEATURE(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.Features.a_Field)
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_18362
    NEM_LOG_REL_CAP_FEATURE(PartialUnmap);
    NEM_LOG_REL_CAP_FEATURE(LocalApicEmulation);
    NEM_LOG_REL_CAP_FEATURE(Xsave);
    NEM_LOG_REL_CAP_FEATURE(DirtyPageTracking);
    NEM_LOG_REL_CAP_FEATURE(SpeculationControl);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_19040
    NEM_LOG_REL_CAP_FEATURE(ApicRemoteRead);
    NEM_LOG_REL_CAP_FEATURE(IdleSuspend);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN11_22000 /** @todo Could some of these may have been added earlier... */
    NEM_LOG_REL_CAP_FEATURE(VirtualPciDeviceSupport);
    NEM_LOG_REL_CAP_FEATURE(IommuSupport);
    NEM_LOG_REL_CAP_FEATURE(VpHotAddRemove);
#endif
#undef  NEM_LOG_REL_CAP_FEATURE
    const uint64_t fKnownFeatures = RT_BIT_64(10) - 1U;
    if (Caps.Features.AsUINT64 & ~fKnownFeatures)
        NEM_LOG_REL_CAP_SUB_EX("Unknown features", "%#RX64", Caps.ExtendedVmExits.AsUINT64 & ~fKnownVmExits);
    pVM->nem.s.fSpeculationControl = RT_BOOL(Caps.Features.SpeculationControl);
    /** @todo RECHECK: WHV_CAPABILITY_FEATURES typedef. */

    /*
     * Check supported exception exit bitmap bits.
     * We don't currently require this, so we just log failure.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeExceptionExitBitmap, &Caps, sizeof(Caps));
    if (SUCCEEDED(hrc))
        LogRel(("NEM: Supported exception exit bitmap: %#RX64\n", Caps.ExceptionExitBitmap));
    else
        LogRel(("NEM: Warning! WHvGetCapability/WHvCapabilityCodeExceptionExitBitmap failed: %Rhrc (Last=%#x/%u)",
                hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));

    /*
     * Check that the CPU vendor is supported.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeProcessorVendor, &Caps, sizeof(Caps));
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeProcessorVendor failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
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
        default:
            NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorVendor", "%d", Caps.ProcessorVendor);
            return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "Unknown processor vendor: %d", Caps.ProcessorVendor);
    }

    /*
     * CPU features, guessing these are virtual CPU features?
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeProcessorFeatures, &Caps, sizeof(Caps));
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeProcessorFeatures failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorFeatures", "%'#018RX64", Caps.ProcessorFeatures.AsUINT64);
#define NEM_LOG_REL_CPU_FEATURE(a_Field)    NEM_LOG_REL_CAP_SUB(#a_Field, Caps.ProcessorFeatures.a_Field)
    NEM_LOG_REL_CPU_FEATURE(Sse3Support);
    NEM_LOG_REL_CPU_FEATURE(LahfSahfSupport);
    NEM_LOG_REL_CPU_FEATURE(Ssse3Support);
    NEM_LOG_REL_CPU_FEATURE(Sse4_1Support);
    NEM_LOG_REL_CPU_FEATURE(Sse4_2Support);
    NEM_LOG_REL_CPU_FEATURE(Sse4aSupport);
    NEM_LOG_REL_CPU_FEATURE(XopSupport);
    NEM_LOG_REL_CPU_FEATURE(PopCntSupport);
    NEM_LOG_REL_CPU_FEATURE(Cmpxchg16bSupport);
    NEM_LOG_REL_CPU_FEATURE(Altmovcr8Support);
    NEM_LOG_REL_CPU_FEATURE(LzcntSupport);
    NEM_LOG_REL_CPU_FEATURE(MisAlignSseSupport);
    NEM_LOG_REL_CPU_FEATURE(MmxExtSupport);
    NEM_LOG_REL_CPU_FEATURE(Amd3DNowSupport);
    NEM_LOG_REL_CPU_FEATURE(ExtendedAmd3DNowSupport);
    NEM_LOG_REL_CPU_FEATURE(Page1GbSupport);
    NEM_LOG_REL_CPU_FEATURE(AesSupport);
    NEM_LOG_REL_CPU_FEATURE(PclmulqdqSupport);
    NEM_LOG_REL_CPU_FEATURE(PcidSupport);
    NEM_LOG_REL_CPU_FEATURE(Fma4Support);
    NEM_LOG_REL_CPU_FEATURE(F16CSupport);
    NEM_LOG_REL_CPU_FEATURE(RdRandSupport);
    NEM_LOG_REL_CPU_FEATURE(RdWrFsGsSupport);
    NEM_LOG_REL_CPU_FEATURE(SmepSupport);
    NEM_LOG_REL_CPU_FEATURE(EnhancedFastStringSupport);
    NEM_LOG_REL_CPU_FEATURE(Bmi1Support);
    NEM_LOG_REL_CPU_FEATURE(Bmi2Support);
    NEM_LOG_REL_CPU_FEATURE(Reserved1);
    NEM_LOG_REL_CPU_FEATURE(MovbeSupport);
    NEM_LOG_REL_CPU_FEATURE(Npiep1Support);
    NEM_LOG_REL_CPU_FEATURE(DepX87FPUSaveSupport);
    NEM_LOG_REL_CPU_FEATURE(RdSeedSupport);
    NEM_LOG_REL_CPU_FEATURE(AdxSupport);
    NEM_LOG_REL_CPU_FEATURE(IntelPrefetchSupport);
    NEM_LOG_REL_CPU_FEATURE(SmapSupport);
    NEM_LOG_REL_CPU_FEATURE(HleSupport);
    NEM_LOG_REL_CPU_FEATURE(RtmSupport);
    NEM_LOG_REL_CPU_FEATURE(RdtscpSupport);
    NEM_LOG_REL_CPU_FEATURE(ClflushoptSupport);
    NEM_LOG_REL_CPU_FEATURE(ClwbSupport);
    NEM_LOG_REL_CPU_FEATURE(ShaSupport);
    NEM_LOG_REL_CPU_FEATURE(X87PointersSavedSupport);
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_17134 /** @todo maybe some of these were added earlier... */
    NEM_LOG_REL_CPU_FEATURE(InvpcidSupport);
    NEM_LOG_REL_CPU_FEATURE(IbrsSupport);
    NEM_LOG_REL_CPU_FEATURE(StibpSupport);
    NEM_LOG_REL_CPU_FEATURE(IbpbSupport);
    NEM_LOG_REL_CPU_FEATURE(Reserved2);
    NEM_LOG_REL_CPU_FEATURE(SsbdSupport);
    NEM_LOG_REL_CPU_FEATURE(FastShortRepMovSupport);
    NEM_LOG_REL_CPU_FEATURE(Reserved3);
    NEM_LOG_REL_CPU_FEATURE(RdclNo);
    NEM_LOG_REL_CPU_FEATURE(IbrsAllSupport);
    NEM_LOG_REL_CPU_FEATURE(Reserved4);
    NEM_LOG_REL_CPU_FEATURE(SsbNo);
    NEM_LOG_REL_CPU_FEATURE(RsbANo);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN10_19040
    NEM_LOG_REL_CPU_FEATURE(Reserved5);
    NEM_LOG_REL_CPU_FEATURE(RdPidSupport);
    NEM_LOG_REL_CPU_FEATURE(UmipSupport);
    NEM_LOG_REL_CPU_FEATURE(MdsNoSupport);
    NEM_LOG_REL_CPU_FEATURE(MdClearSupport);
#endif
#if WDK_NTDDI_VERSION >= MY_NTDDI_WIN11_22000
    NEM_LOG_REL_CPU_FEATURE(TaaNoSupport);
    NEM_LOG_REL_CPU_FEATURE(TsxCtrlSupport);
    NEM_LOG_REL_CPU_FEATURE(Reserved6);
#endif
#undef NEM_LOG_REL_CPU_FEATURE
    pVM->nem.s.uCpuFeatures.u64 = Caps.ProcessorFeatures.AsUINT64;
    /** @todo RECHECK: WHV_PROCESSOR_FEATURES typedef. */

    /*
     * The cache line flush size.
     */
    RT_ZERO(Caps);
    hrc = WHvGetCapabilityWrapper(WHvCapabilityCodeProcessorClFlushSize, &Caps, sizeof(Caps));
    if (FAILED(hrc))
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED,
                             "WHvGetCapability/WHvCapabilityCodeProcessorClFlushSize failed: %Rhrc (Last=%#x/%u)",
                             hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
    NEM_LOG_REL_CAP_EX("WHvCapabilityCodeProcessorClFlushSize", "2^%u", Caps.ProcessorClFlushSize);
    if (Caps.ProcessorClFlushSize < 8 && Caps.ProcessorClFlushSize > 9)
        return RTErrInfoSetF(pErrInfo, VERR_NEM_INIT_FAILED, "Unsupported cache line flush size: %u", Caps.ProcessorClFlushSize);
    pVM->nem.s.cCacheLineFlushShift = Caps.ProcessorClFlushSize;

    /*
     * See if they've added more properties that we're not aware of.
     */
    /** @todo RECHECK: WHV_CAPABILITY_CODE typedef. */
    if (!IsDebuggerPresent()) /* Too noisy when in debugger, so skip. */
    {
        static const struct
        {
            uint32_t iMin, iMax; } s_aUnknowns[] =
        {
            { 0x0004, 0x000f },
            { 0x1003, 0x100f },
            { 0x2000, 0x200f },
            { 0x3000, 0x300f },
            { 0x4000, 0x400f },
        };
        for (uint32_t j = 0; j < RT_ELEMENTS(s_aUnknowns); j++)
            for (uint32_t i = s_aUnknowns[j].iMin; i <= s_aUnknowns[j].iMax; i++)
            {
                RT_ZERO(Caps);
                hrc = WHvGetCapabilityWrapper((WHV_CAPABILITY_CODE)i, &Caps, sizeof(Caps));
                if (SUCCEEDED(hrc))
                    LogRel(("NEM: Warning! Unknown capability %#x returning: %.*Rhxs\n", i, sizeof(Caps), &Caps));
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
    return VINF_SUCCESS;
}

#ifdef LOG_ENABLED

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
#ifdef LOG_ENABLED
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

    int rc;

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
             * We'll continue setup in nemR3NativeInitAfterCPUM.
             */
            pVM->nem.s.fCreatedEmts     = false;
            pVM->nem.s.hPartition       = hPartition;
            LogRel(("NEM: Created partition %p.\n", hPartition));
            return VINF_SUCCESS;
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


/**
 * Try initialize the native API.
 *
 * This may only do part of the job, more can be done in
 * nemR3NativeInitAfterCPUM() and nemR3NativeInitCompleted().
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   fFallback       Whether we're in fallback mode or use-NEM mode. In
 *                          the latter we'll fail if we cannot initialize.
 * @param   fForced         Whether the HMForced flag is set and we should
 *                          fail if we cannot initialize.
 */
int nemR3NativeInit(PVM pVM, bool fFallback, bool fForced)
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
                        STAMR3RegisterF(pVM, &pNemCpu->StatExitMemIntercept,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Number of intercepted memory exits",     "/NEM/CPU%u/ExitMemIntercept", idCpu);
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


/**
 * This is called after CPUMR3Init is done.
 *
 * @returns VBox status code.
 * @param   pVM                 The VM handle..
 */
int nemR3NativeInitAfterCPUM(PVM pVM)
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


int nemR3NativeInitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    //BOOL fRet = SetThreadPriority(GetCurrentThread(), 0);
    //AssertLogRel(fRet);

    NOREF(pVM); NOREF(enmWhat);
    return VINF_SUCCESS;
}


int nemR3NativeTerm(PVM pVM)
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


/**
 * VM reset notification.
 *
 * @param   pVM         The cross context VM structure.
 */
void nemR3NativeReset(PVM pVM)
{
#if 0
    /* Unfix the A20 gate. */
    pVM->nem.s.fA20Fixed = false;
#else
    RT_NOREF(pVM);
#endif
}


/**
 * Reset CPU due to INIT IPI or hot (un)plugging.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the CPU being
 *                      reset.
 * @param   fInitIpi    Whether this is the INIT IPI or hot (un)plugging case.
 */
void nemR3NativeResetCpu(PVMCPU pVCpu, bool fInitIpi)
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


VBOXSTRICTRC nemR3NativeRunGC(PVM pVM, PVMCPU pVCpu)
{
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


bool nemR3NativeSetSingleInstruction(PVM pVM, PVMCPU pVCpu, bool fEnable)
{
    NOREF(pVM); NOREF(pVCpu); NOREF(fEnable);
    return false;
}


void nemR3NativeNotifyFF(PVM pVM, PVMCPU pVCpu, uint32_t fFlags)
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

