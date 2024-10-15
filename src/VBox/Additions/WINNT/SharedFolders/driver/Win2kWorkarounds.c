/* $Id: Win2kWorkarounds.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Shared Folders - Windows 2000 Hacks.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#define FsRtlTeardownPerStreamContexts  FsRtlTeardownPerStreamContexts_AvoidIt
#define RtlGetVersion                   RtlGetVersion_AvoidIt
#define PsGetProcessImageFileName       PsGetProcessImageFileName_AvoidIt
#include "vbsf.h"

#include <iprt/asm.h>


#if 0
/*
 * FsRtlTeardownPerStreamContexts.
 */
static VOID __stdcall Resolve_FsRtlTeardownPerStreamContexts(PFSRTL_ADVANCED_FCB_HEADER);
static volatile PFN_FSRTLTEARDOWNPERSTREAMCONTEXTS g_pfnFsRtlTeardownPerStreamContexts = Resolve_FsRtlTeardownPerStreamContexts;


static VOID __stdcall Fake_FsRtlTeardownPerStreamContexts(PFSRTL_ADVANCED_FCB_HEADER pAdvancedHeader)
{
    PLIST_ENTRY pCur;

    ExAcquireFastMutex(pAdvancedHeader->FastMutex);

    pCur = pAdvancedHeader->FilterContexts.Flink;
    while (pCur != &pAdvancedHeader->FilterContexts)
    {
        PLIST_ENTRY                 pNext = pCur->Flink;
        PFSRTL_PER_STREAM_CONTEXT   pCtx  = CONTAINING_RECORD(pCur, FSRTL_PER_STREAM_CONTEXT, Links);
        Log(("Fake_FsRtlTeardownPerStreamContexts: %p\n", pCtx));
        pCtx->FreeCallback(pCtx);
        pCur = pNext;
    }
    InitializeListHead(&pAdvancedHeader->FilterContexts);

    ExReleaseFastMutex(pAdvancedHeader->FastMutex);
    return;
}


static VOID __stdcall Resolve_FsRtlTeardownPerStreamContexts(PFSRTL_ADVANCED_FCB_HEADER pAdvancedHeader)
{
    UNICODE_STRING                      RoutineName;
    PFN_FSRTLTEARDOWNPERSTREAMCONTEXTS  pfn;
    Log(("Resolve_FsRtlTeardownPerStreamContexts: %p\n", pAdvancedHeader));

    RtlInitUnicodeString(&RoutineName, L"KeIpiGenericCall");
    pfn = (PFN_FSRTLTEARDOWNPERSTREAMCONTEXTS)MmGetSystemRoutineAddress(&RoutineName);
    if (!pfn)
        pfn = Fake_FsRtlTeardownPerStreamContexts;
    ASMAtomicWritePtr(&g_pfnFsRtlTeardownPerStreamContexts, pfn);
    pfn(pAdvancedHeader);
}


#undef FsRtlTeardownPerStreamContexts
__declspec(dllexport) VOID
FsRtlTeardownPerStreamContexts(PFSRTL_ADVANCED_FCB_HEADER pAdvancedHeader)
{
    Log(("FsRtlTeardownPerStreamContexts: %p\n", pAdvancedHeader));
    g_pfnFsRtlTeardownPerStreamContexts(pAdvancedHeader);
    Log(("FsRtlTeardownPerStreamContexts: returns\n"));
}
#endif


/*
 * RtlGetVersion
 */
typedef NTSTATUS (__stdcall * PFNRTLGETVERSION)(PRTL_OSVERSIONINFOW);
static NTSTATUS __stdcall Resolve_RtlGetVersion(PRTL_OSVERSIONINFOW pVerInfo);
static volatile PFNRTLGETVERSION g_pfnRtlGetVersion = Resolve_RtlGetVersion;


static NTSTATUS __stdcall Fake_RtlGetVersion(PRTL_OSVERSIONINFOW pVerInfo)
{
    Log(("Fake_RtlGetVersion: %p\n", pVerInfo));
    if (pVerInfo->dwOSVersionInfoSize < sizeof(*pVerInfo))
    {
        Log(("Fake_RtlGetVersion: -> STATUS_INVALID_PARAMETER (size = %#x)\n", pVerInfo->dwOSVersionInfoSize));
        return STATUS_INVALID_PARAMETER;
    }

    /* Report Windows 2000 w/o SP. */
    pVerInfo->dwMajorVersion  = 5;
    pVerInfo->dwMinorVersion  = 0;
    pVerInfo->dwBuildNumber   = 2195;
    pVerInfo->dwPlatformId    = VER_PLATFORM_WIN32_NT;
    pVerInfo->szCSDVersion[0] = '\0';

    if (pVerInfo->dwOSVersionInfoSize >= sizeof(RTL_OSVERSIONINFOEXW))
    {
        PRTL_OSVERSIONINFOEXW pVerInfoEx = (PRTL_OSVERSIONINFOEXW)pVerInfo;
        pVerInfoEx->wServicePackMajor = 0;
        pVerInfoEx->wServicePackMinor = 0;
        pVerInfoEx->wSuiteMask        = 0;
        pVerInfoEx->wProductType      = VER_NT_WORKSTATION;
        pVerInfoEx->wReserved         = 0;
    }

    return STATUS_SUCCESS;
}


static NTSTATUS __stdcall Resolve_RtlGetVersion(PRTL_OSVERSIONINFOW pVerInfo)
{
    UNICODE_STRING  RoutineName;
    PFNRTLGETVERSION pfn;
    Log(("Resolve_RtlGetVersion: %p\n", pVerInfo));

    RtlInitUnicodeString(&RoutineName, L"RtlGetVersion");
    pfn = (PFNRTLGETVERSION)(uintptr_t)MmGetSystemRoutineAddress(&RoutineName);
    if (!pfn)
        pfn = Fake_RtlGetVersion;
    ASMAtomicWritePtr((void * volatile *)&g_pfnRtlGetVersion, (void *)(uintptr_t)pfn);

    return pfn(pVerInfo);
}


#undef RtlGetVersion
__declspec(dllexport) NTSTATUS __stdcall
RtlGetVersion(PRTL_OSVERSIONINFOW pVerInfo)
{
    return g_pfnRtlGetVersion(pVerInfo);
}


/*
 * PsGetProcessImageFileName
 */

typedef LPSTR (__stdcall * PFNPSGETPROCESSIMAGEFILENAME)(PEPROCESS pProcess);
static LPSTR __stdcall Resolve_PsGetProcessImageFileName(PEPROCESS pProcess);
static volatile PFNPSGETPROCESSIMAGEFILENAME g_pfnPsGetProcessImageFileName = Resolve_PsGetProcessImageFileName;


static LPSTR __stdcall Fake_PsGetProcessImageFileName(PEPROCESS pProcess)
{
    RT_NOREF(pProcess);
    Log(("Fake_PsGetProcessImageFileName: %p\n", pProcess));
    return "Fake_PsGetProcessImageFileName";
}


static LPSTR __stdcall Resolve_PsGetProcessImageFileName(PEPROCESS pProcess)
{
    UNICODE_STRING                  RoutineName;
    PFNPSGETPROCESSIMAGEFILENAME    pfn;
    Log(("Resolve_PsGetProcessImageFileName: %p\n", pProcess));

    RtlInitUnicodeString(&RoutineName, L"PsGetProcessImageFileName");
    pfn = (PFNPSGETPROCESSIMAGEFILENAME)(uintptr_t)MmGetSystemRoutineAddress(&RoutineName);
    if (!pfn)
        pfn = Fake_PsGetProcessImageFileName;
    ASMAtomicWritePtr((void * volatile *)&g_pfnPsGetProcessImageFileName, (void *)(uintptr_t)pfn);

    return pfn(pProcess);
}


#undef PsGetProcessImageFileName
__declspec(dllexport) LPSTR __stdcall
PsGetProcessImageFileName(PEPROCESS pProcess)
{
    return g_pfnPsGetProcessImageFileName(pProcess);
}

