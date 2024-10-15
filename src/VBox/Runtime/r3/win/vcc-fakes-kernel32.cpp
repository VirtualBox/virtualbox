/* $Id: vcc-fakes-kernel32.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Tricks to make the Visual C++ 2010 CRT work on NT4, W2K and XP.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#ifdef DEBUG
# include <stdio.h> /* _snprintf */
#endif

#ifndef RT_ARCH_X86
# error "This code is X86 only"
#endif

#include <iprt/nt/nt-and-windows.h>

#include "vcc-fakes.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifndef HEAP_STANDARD
# define HEAP_STANDARD 0
#endif


/** Declare a kernel32 API.
 * @note We are not exporting them as that causes duplicate symbol troubles in
 *       the OpenGL bits. */
#define DECL_KERNEL32(a_Type) extern "C" a_Type WINAPI

#if defined(WDK_NTDDI_VERSION) && defined(NTDDI_WIN10)
# if WDK_NTDDI_VERSION >= NTDDI_WIN10 /* In Windows 10 SDK the 'Sequence' field has been renamed to 'CpuId'. */
#  define SLIST_HEADER_SEQUENCE_NOW_CALLED_CPUID
# endif
#endif



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static volatile bool g_fInitialized = false;
#define MAKE_IMPORT_ENTRY(a_uMajorVer, a_uMinorVer, a_Name, a_cb) DECLARE_FUNCTION_POINTER(a_Name, a_cb)
#ifdef VCC_FAKES_TARGET_VCC100
# include "vcc-fakes-kernel32-100.h"
#elif defined(VCC_FAKES_TARGET_VCC140)
# include "vcc-fakes-kernel32-141.h"
#elif defined(VCC_FAKES_TARGET_VCC141)
# include "vcc-fakes-kernel32-141.h"
#elif defined(VCC_FAKES_TARGET_VCC142)
# include "vcc-fakes-kernel32-141.h"
#else
# error "Port me!"
#endif


static BOOL FakeSetLastErrorFromNtStatus(NTSTATUS rcNt)
{
    DWORD dwErr;
    switch (rcNt)
    {
        case STATUS_INVALID_PARAMETER:
        case STATUS_INVALID_PARAMETER_1:
        case STATUS_INVALID_PARAMETER_2:
        case STATUS_INVALID_PARAMETER_3:
        case STATUS_INVALID_PARAMETER_4:
        case STATUS_INVALID_PARAMETER_5:
        case STATUS_INVALID_PARAMETER_6:
        case STATUS_INVALID_PARAMETER_7:
        case STATUS_INVALID_PARAMETER_8:
        case STATUS_INVALID_PARAMETER_9:
        case STATUS_INVALID_PARAMETER_10:
        case STATUS_INVALID_PARAMETER_11:
        case STATUS_INVALID_PARAMETER_12:
            dwErr = ERROR_INVALID_PARAMETER;
            break;

        case STATUS_INVALID_HANDLE:
            dwErr = ERROR_INVALID_HANDLE;
            break;

        case STATUS_ACCESS_DENIED:
            dwErr = ERROR_ACCESS_DENIED;
            break;

        default:
            dwErr = ERROR_INVALID_PARAMETER;
            break;
    }
    SetLastError(dwErr);
    return FALSE;
}



DECL_KERNEL32(PVOID) Fake_DecodePointer(PVOID pvEncoded)
{
    return pvEncoded;
}


DECL_KERNEL32(PVOID) Fake_EncodePointer(PVOID pvNative)
{
    return pvNative;
}


DECL_KERNEL32(BOOL) Fake_InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION pCritSect, DWORD cSpin)
{
    RT_NOREF(cSpin);
    InitializeCriticalSection(pCritSect);
    return TRUE;
}


DECL_KERNEL32(HANDLE) Fake_CreateIoCompletionPort(HANDLE hFile, HANDLE hExistingCompletionPort, ULONG_PTR uCompletionKey,
                                                  DWORD cConcurrentThreads)
{
    RT_NOREF(hFile, hExistingCompletionPort, uCompletionKey, cConcurrentThreads);
    SetLastError(ERROR_NOT_SUPPORTED);
    return NULL;
}


DECL_KERNEL32(BOOL) Fake_GetQueuedCompletionStatus(HANDLE hCompletionPort, PDWORD_PTR pcbTransfered, PULONG_PTR puCompletionKey,
                                                   LPOVERLAPPED *ppOverlapped, DWORD cMs)
{
    RT_NOREF(hCompletionPort, pcbTransfered, puCompletionKey, ppOverlapped, cMs);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}


DECL_KERNEL32(BOOL) Fake_PostQueuedCompletionStatus(HANDLE hCompletionPort, DWORD cbTransfered, ULONG_PTR uCompletionKey,
                                                    LPOVERLAPPED pOverlapped)
{
    RT_NOREF(hCompletionPort, cbTransfered, uCompletionKey, pOverlapped);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}


DECL_KERNEL32(BOOL) Fake_HeapSetInformation(HANDLE hHeap, HEAP_INFORMATION_CLASS enmInfoClass, PVOID pvBuf, SIZE_T cbBuf)
{
    RT_NOREF(hHeap);
    if (enmInfoClass == HeapCompatibilityInformation)
    {
        if (   cbBuf != sizeof(ULONG)
            || !pvBuf
            || *(PULONG)pvBuf == HEAP_STANDARD
           )
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return TRUE;
    }

    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}


DECL_KERNEL32(BOOL) Fake_HeapQueryInformation(HANDLE hHeap, HEAP_INFORMATION_CLASS enmInfoClass,
                                              PVOID pvBuf, SIZE_T cbBuf, PSIZE_T pcbRet)
{
    RT_NOREF(hHeap);
    if (enmInfoClass == HeapCompatibilityInformation)
    {
        *pcbRet = sizeof(ULONG);
        if (cbBuf < sizeof(ULONG) || !pvBuf)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        *(PULONG)pvBuf = HEAP_STANDARD;
        return TRUE;
    }

    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}


/* These are used by INTEL\mt_obj\Timer.obj: */

DECL_KERNEL32(HANDLE) Fake_CreateTimerQueue(void)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return NULL;
}

DECL_KERNEL32(BOOL) Fake_CreateTimerQueueTimer(PHANDLE phTimer, HANDLE hTimerQueue, WAITORTIMERCALLBACK pfnCallback, PVOID pvUser,
                                               DWORD msDueTime, DWORD msPeriod, ULONG fFlags)
{
    NOREF(phTimer); NOREF(hTimerQueue); NOREF(pfnCallback); NOREF(pvUser); NOREF(msDueTime); NOREF(msPeriod); NOREF(fFlags);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

DECL_KERNEL32(BOOL) Fake_DeleteTimerQueueTimer(HANDLE hTimerQueue, HANDLE hTimer, HANDLE hEvtCompletion)
{
    NOREF(hTimerQueue); NOREF(hTimer); NOREF(hEvtCompletion);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/* This is used by several APIs. */

DECL_KERNEL32(VOID) Fake_InitializeSListHead(PSLIST_HEADER pHead)
{
    pHead->Alignment = 0;
}


DECL_KERNEL32(PSLIST_ENTRY) Fake_InterlockedFlushSList(PSLIST_HEADER pHead)
{
    PSLIST_ENTRY pRet = NULL;
    if (pHead->Next.Next)
    {
        for (;;)
        {
            SLIST_HEADER OldHead = *pHead;
            SLIST_HEADER NewHead;
            NewHead.Alignment = 0;
#ifdef SLIST_HEADER_SEQUENCE_NOW_CALLED_CPUID
            NewHead.CpuId  = OldHead.CpuId + 1;
#else
            NewHead.Sequence  = OldHead.Sequence + 1;
#endif
            if (ASMAtomicCmpXchgU64(&pHead->Alignment, NewHead.Alignment, OldHead.Alignment))
            {
                pRet = OldHead.Next.Next;
                break;
            }
        }
    }
    return pRet;
}

DECL_KERNEL32(PSLIST_ENTRY) Fake_InterlockedPopEntrySList(PSLIST_HEADER pHead)
{
    PSLIST_ENTRY pRet = NULL;
    for (;;)
    {
        SLIST_HEADER OldHead = *pHead;
        pRet = OldHead.Next.Next;
        if (pRet)
        {
            SLIST_HEADER NewHead;
            __try
            {
                NewHead.Next.Next = pRet->Next;
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                continue;
            }
            NewHead.Depth     = OldHead.Depth - 1;
#ifdef SLIST_HEADER_SEQUENCE_NOW_CALLED_CPUID
            NewHead.CpuId  = OldHead.CpuId + 1;
#else
            NewHead.Sequence  = OldHead.Sequence + 1;
#endif
            if (ASMAtomicCmpXchgU64(&pHead->Alignment, NewHead.Alignment, OldHead.Alignment))
                break;
        }
        else
            break;
    }
    return pRet;
}

DECL_KERNEL32(PSLIST_ENTRY) Fake_InterlockedPushEntrySList(PSLIST_HEADER pHead, PSLIST_ENTRY pEntry)
{
    PSLIST_ENTRY pRet = NULL;
    for (;;)
    {
        SLIST_HEADER OldHead = *pHead;
        pRet = OldHead.Next.Next;
        pEntry->Next = pRet;
        SLIST_HEADER NewHead;
        NewHead.Next.Next = pEntry;
        NewHead.Depth     = OldHead.Depth + 1;
#ifdef SLIST_HEADER_SEQUENCE_NOW_CALLED_CPUID
        NewHead.CpuId  = OldHead.CpuId + 1;
#else
        NewHead.Sequence  = OldHead.Sequence + 1;
#endif
        if (ASMAtomicCmpXchgU64(&pHead->Alignment, NewHead.Alignment, OldHead.Alignment))
            break;
    }
    return pRet;
}

DECL_KERNEL32(WORD) Fake_QueryDepthSList(PSLIST_HEADER pHead)
{
    return pHead->Depth;
}


/* curl drags these in: */
DECL_KERNEL32(BOOL) Fake_VerifyVersionInfoA(LPOSVERSIONINFOEXA pInfo, DWORD fTypeMask, DWORDLONG fConditionMask)
{
    OSVERSIONINFOEXA VerInfo;
    RT_ZERO(VerInfo);
    VerInfo.dwOSVersionInfoSize = sizeof(VerInfo);
    if (!GetVersionExA((OSVERSIONINFO *)&VerInfo))
    {
        RT_ZERO(VerInfo);
        VerInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        BOOL fRet = GetVersionExA((OSVERSIONINFO *)&VerInfo);
        if (fRet)
        { /* likely */ }
        else
        {
            MY_ASSERT(false, "VerifyVersionInfoA: #1");
            return FALSE;
        }
    }

    BOOL fRet = TRUE;
    for (unsigned i = 0; i < 8 && fRet; i++)
        if (fTypeMask & RT_BIT_32(i))
        {
            uint32_t uLeft, uRight;
            switch (RT_BIT_32(i))
            {
#define MY_CASE(a_Num, a_Member) case a_Num: uLeft = VerInfo.a_Member; uRight = pInfo->a_Member; break
                MY_CASE(VER_MINORVERSION,       dwMinorVersion);
                MY_CASE(VER_MAJORVERSION,       dwMajorVersion);
                MY_CASE(VER_BUILDNUMBER,        dwBuildNumber);
                MY_CASE(VER_PLATFORMID,         dwPlatformId);
                MY_CASE(VER_SERVICEPACKMINOR,   wServicePackMinor);
                MY_CASE(VER_SERVICEPACKMAJOR,   wServicePackMinor);
                MY_CASE(VER_SUITENAME,          wSuiteMask);
                MY_CASE(VER_PRODUCT_TYPE,       wProductType);
#undef  MY_CASE
                default: uLeft = uRight = 0; MY_ASSERT(false, "VerifyVersionInfoA: #2");
            }
            switch ((uint8_t)(fConditionMask >> (i*8)))
            {
                case VER_EQUAL:             fRet = uLeft == uRight; break;
                case VER_GREATER:           fRet = uLeft >  uRight; break;
                case VER_GREATER_EQUAL:     fRet = uLeft >= uRight; break;
                case VER_LESS:              fRet = uLeft <  uRight; break;
                case VER_LESS_EQUAL:        fRet = uLeft <= uRight; break;
                case VER_AND:               fRet = (uLeft & uRight) == uRight; break;
                case VER_OR:                fRet = (uLeft & uRight) != 0; break;
                default:                    fRet = FALSE; MY_ASSERT(false, "VerifyVersionInfoA: #3"); break;
            }
        }

    return fRet;
}


DECL_KERNEL32(ULONGLONG) Fake_VerSetConditionMask(ULONGLONG fConditionMask, DWORD fTypeMask, BYTE bOperator)
{
    for (unsigned i = 0; i < 8; i++)
        if (fTypeMask & RT_BIT_32(i))
        {
            uint64_t fMask  = UINT64_C(0xff) << (i*8);
            fConditionMask &= ~fMask;
            fConditionMask |= (uint64_t)bOperator << (i*8);

        }
    return fConditionMask;
}


#if VCC_FAKES_TARGET >= 140
/** @since 5.0 (windows 2000) */
DECL_KERNEL32(BOOL) Fake_GetModuleHandleExW(DWORD dwFlags, LPCWSTR pwszModuleName, HMODULE *phModule)
{
    HMODULE hmod;
    if (dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)
    {
        /** @todo search the loader list. */
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    else
    {
        hmod = GetModuleHandleW(pwszModuleName);
        if (!hmod)
            return FALSE;
    }

    /*
     * Get references to the module.
     */
    if (   !(dwFlags & (GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_PIN))
        && GetModuleHandleW(NULL) != hmod)
    {
        WCHAR wszModule[MAX_PATH];
        if (GetModuleFileNameW(hmod, wszModule, RT_ELEMENTS(wszModule)) > 0)
        {
            if (dwFlags & GET_MODULE_HANDLE_EX_FLAG_PIN)
            {
                uint32_t cRefs = 32;
                while (cRefs-- > 0)
                    LoadLibraryW(wszModule);
            }
            else if (!LoadLibraryW(wszModule))
                return FALSE;
        }
    }

    *phModule = hmod;
    return TRUE;
}
#endif /* VCC_FAKES_TARGET >= 141 */


#if VCC_FAKES_TARGET >= 140
/** @since 5.0 (windows 2000) */
DECL_KERNEL32(BOOL) Fake_SetFilePointerEx(HANDLE hFile, LARGE_INTEGER offDistanceToMove,
                                          PLARGE_INTEGER pNewFilePointer, DWORD dwMoveMethod)
{
    NTSTATUS                    rcNt;
    IO_STATUS_BLOCK             Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;

    FILE_POSITION_INFORMATION   PosInfo;
    switch (dwMoveMethod)
    {
        case FILE_BEGIN:
            PosInfo.CurrentByteOffset = offDistanceToMove;
            break;

        case FILE_CURRENT:
            PosInfo.CurrentByteOffset.QuadPart = INT64_MAX;
            rcNt = NtQueryInformationFile(hFile, &Ios, &PosInfo, sizeof(PosInfo), FilePositionInformation);
            if (NT_SUCCESS(rcNt))
            {
                PosInfo.CurrentByteOffset.QuadPart += offDistanceToMove.QuadPart;
                break;
            }
            return FakeSetLastErrorFromNtStatus(rcNt);

        case FILE_END:
        {
            FILE_STANDARD_INFO StdInfo = {{0}};
            rcNt = NtQueryInformationFile(hFile, &Ios, &StdInfo, sizeof(StdInfo), FileStandardInformation);
            if (NT_SUCCESS(rcNt))
            {
                PosInfo.CurrentByteOffset.QuadPart = offDistanceToMove.QuadPart + StdInfo.EndOfFile.QuadPart;
                break;
            }
            return FakeSetLastErrorFromNtStatus(rcNt);
        }

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }

    rcNt = NtSetInformationFile(hFile, &Ios, &PosInfo, sizeof(PosInfo), FilePositionInformation);
    if (NT_SUCCESS(rcNt))
    {
        if (pNewFilePointer)
            *pNewFilePointer = PosInfo.CurrentByteOffset;
        return TRUE;
    }
    return FakeSetLastErrorFromNtStatus(rcNt);
}
#endif /* VCC_FAKES_TARGET >= 140 */


#if VCC_FAKES_TARGET >= 140
/** @since 5.0 (windows 2000) */
DECL_KERNEL32(BOOL) Fake_GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER pcbFile)
{
    IO_STATUS_BLOCK    Ios     = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    FILE_STANDARD_INFO StdInfo = {{0}};
    NTSTATUS rcNt = NtQueryInformationFile(hFile, &Ios, &StdInfo, sizeof(StdInfo), FileStandardInformation);
    if (NT_SUCCESS(rcNt))
    {
        *pcbFile = StdInfo.EndOfFile;
        return TRUE;
    }
    return FakeSetLastErrorFromNtStatus(rcNt);
}
#endif /* VCC_FAKES_TARGET >= 140 */




/*
 * NT 3.51 stuff.
 */

#if VCC_FAKES_TARGET >= 140
/** @since 4.0 */
DECL_KERNEL32(HANDLE) Fake_FindFirstFileExW(LPCWSTR pwszFileName, FINDEX_INFO_LEVELS enmInfoLevel, LPVOID pvFindFileData,
                                            FINDEX_SEARCH_OPS enmSearchOp, LPVOID pvSearchFilter, DWORD dwAdditionalFlags)
{
    // STL:                FindFirstFileExW(, FindExInfoBasic,    , FindExSearchNameMatch, NULL, 0);
    // CRT/_findfile:      FindFirstFileExW(, FindExInfoStandard, , FindExSearchNameMatch, NULL, 0);
    // CRT/argv_wildcards: FindFirstFileExW(, FindExInfoStandard, , FindExSearchNameMatch, NULL, 0);
    MY_ASSERT_STMT_RETURN(dwAdditionalFlags == 0, SetLastError(ERROR_INVALID_PARAMETER), INVALID_HANDLE_VALUE);
    MY_ASSERT_STMT_RETURN(pvSearchFilter == NULL, SetLastError(ERROR_INVALID_PARAMETER), INVALID_HANDLE_VALUE);
    MY_ASSERT_STMT_RETURN(enmSearchOp == FindExSearchNameMatch, SetLastError(ERROR_INVALID_PARAMETER), INVALID_HANDLE_VALUE);
    MY_ASSERT_STMT_RETURN(enmInfoLevel == FindExInfoStandard || enmInfoLevel == FindExInfoBasic,
                          SetLastError(ERROR_INVALID_PARAMETER), INVALID_HANDLE_VALUE);

    return FindFirstFileW(pwszFileName, (WIN32_FIND_DATAW *)pvFindFileData);
}
#endif /* VCC_FAKES_TARGET >= 140 */


DECL_KERNEL32(BOOL) Fake_IsProcessorFeaturePresent(DWORD enmProcessorFeature)
{
    /* Could make more of an effort here... */
    RT_NOREF(enmProcessorFeature);
    return FALSE;
}


DECL_KERNEL32(BOOL) Fake_CancelIo(HANDLE hHandle)
{
    /* All NT versions the NTDLL API this corresponds to. */
    RESOLVE_NTDLL_API(NtCancelIoFile);
    if (pfnNtCancelIoFile)
    {
        IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        NTSTATUS rcNt = pfnNtCancelIoFile(hHandle, &Ios);
        if (RT_SUCCESS(rcNt))
            return TRUE;
        if (rcNt == STATUS_INVALID_HANDLE)
            SetLastError(ERROR_INVALID_HANDLE);
        else
            SetLastError(ERROR_INVALID_FUNCTION);
    }
    else
        SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}


/*
 * NT 3.50 stuff.
 */

#if VCC_FAKES_TARGET >= 140
/** @since 3.51 */
DECL_KERNEL32(VOID) Fake_FreeLibraryAndExitThread(HMODULE hLibModule, DWORD dwExitCode)
{
    if (hLibModule)
        FreeModule(hLibModule);
    ExitThread(dwExitCode);
}
#endif /* VCC_FAKES_TARGET >= 140 */


DECL_KERNEL32(BOOL) Fake_IsDebuggerPresent(VOID)
{
    /* Fallback: */
    return FALSE;
}


DECL_KERNEL32(VOID) Fake_GetSystemTimeAsFileTime(LPFILETIME pTime)
{
    DWORD dwVersion = GetVersion();
    if (   (dwVersion & 0xff) > 3
        || (   (dwVersion & 0xff) == 3
            && ((dwVersion >> 8) & 0xff) >= 50) )
    {
        PKUSER_SHARED_DATA pUsd = (PKUSER_SHARED_DATA)MM_SHARED_USER_DATA_VA;

        /* use interrupt time */
        LARGE_INTEGER Time;
        do
        {
            Time.HighPart = pUsd->SystemTime.High1Time;
            Time.LowPart  = pUsd->SystemTime.LowPart;
        } while (pUsd->SystemTime.High2Time != Time.HighPart);

        pTime->dwHighDateTime = Time.HighPart;
        pTime->dwLowDateTime  = Time.LowPart;
    }
    else
    {
        /* NT 3.1 didn't have a KUSER_SHARED_DATA nor a GetSystemTimeAsFileTime export. */
        SYSTEMTIME SystemTime;
        GetSystemTime(&SystemTime);
        BOOL fRet = SystemTimeToFileTime(&SystemTime, pTime);
        if (fRet)
        { /* likely */ }
        else
        {
            MY_ASSERT(false, "GetSystemTimeAsFileTime: #2");
            pTime->dwHighDateTime = 0;
            pTime->dwLowDateTime  = 0;
        }
    }
}


/*
 * NT 3.1 stuff.
 */

DECL_KERNEL32(BOOL) Fake_GetVersionExA(LPOSVERSIONINFOA pInfo)
{
    DWORD dwVersion = GetVersion();

    /* Common fields: */
    pInfo->dwMajorVersion = dwVersion & 0xff;
    pInfo->dwMinorVersion = (dwVersion >> 8) & 0xff;
    if (!(dwVersion & RT_BIT_32(31)))
        pInfo->dwBuildNumber = dwVersion >> 16;
    else
        pInfo->dwBuildNumber = 511;
    pInfo->dwPlatformId = VER_PLATFORM_WIN32_NT;
/** @todo get CSD from registry. */
    pInfo->szCSDVersion[0] = '\0';

    /* OSVERSIONINFOEX fields: */
    if (pInfo->dwOSVersionInfoSize > sizeof((*pInfo)))
    {
        LPOSVERSIONINFOEXA pInfoEx = (LPOSVERSIONINFOEXA)pInfo;
        if (pInfoEx->dwOSVersionInfoSize > RT_UOFFSETOF(OSVERSIONINFOEXA, wServicePackMinor))
        {
            pInfoEx->wServicePackMajor = 0;
            pInfoEx->wServicePackMinor = 0;
        }
        if (pInfoEx->dwOSVersionInfoSize > RT_UOFFSETOF(OSVERSIONINFOEXA, wSuiteMask))
            pInfoEx->wSuiteMask = 0;
        if (pInfoEx->dwOSVersionInfoSize > RT_UOFFSETOF(OSVERSIONINFOEXA, wProductType))
            pInfoEx->wProductType = VER_NT_WORKSTATION;
        if (pInfoEx->wReserved > RT_UOFFSETOF(OSVERSIONINFOEXA, wProductType))
            pInfoEx->wReserved = 0;
    }

    return TRUE;
}


DECL_KERNEL32(BOOL) Fake_GetVersionExW(LPOSVERSIONINFOW pInfo)
{
    DWORD dwVersion = GetVersion();

    /* Common fields: */
    pInfo->dwMajorVersion = dwVersion & 0xff;
    pInfo->dwMinorVersion = (dwVersion >> 8) & 0xff;
    if (!(dwVersion & RT_BIT_32(31)))
        pInfo->dwBuildNumber = dwVersion >> 16;
    else
        pInfo->dwBuildNumber = 511;
    pInfo->dwPlatformId = VER_PLATFORM_WIN32_NT;
/** @todo get CSD from registry. */
    pInfo->szCSDVersion[0] = '\0';

    /* OSVERSIONINFOEX fields: */
    if (pInfo->dwOSVersionInfoSize > sizeof((*pInfo)))
    {
        LPOSVERSIONINFOEXW pInfoEx = (LPOSVERSIONINFOEXW)pInfo;
        if (pInfoEx->dwOSVersionInfoSize > RT_UOFFSETOF(OSVERSIONINFOEXW, wServicePackMinor))
        {
            pInfoEx->wServicePackMajor = 0;
            pInfoEx->wServicePackMinor = 0;
        }
        if (pInfoEx->dwOSVersionInfoSize > RT_UOFFSETOF(OSVERSIONINFOEXW, wSuiteMask))
            pInfoEx->wSuiteMask = 0;
        if (pInfoEx->dwOSVersionInfoSize > RT_UOFFSETOF(OSVERSIONINFOEXW, wProductType))
            pInfoEx->wProductType = VER_NT_WORKSTATION;
        if (pInfoEx->wReserved > RT_UOFFSETOF(OSVERSIONINFOEXW, wProductType))
            pInfoEx->wReserved = 0;
    }

    return TRUE;
}


DECL_KERNEL32(LPWCH) Fake_GetEnvironmentStringsW(void)
{
    /*
     * Environment is ANSI in NT 3.1. We should only be here for NT 3.1.
     * For now, don't try do a perfect job converting it, just do it.
     */
    char    *pszzEnv = (char *)RTNtCurrentPeb()->ProcessParameters->Environment;
    size_t   offEnv  = 0;
    while (pszzEnv[offEnv] != '\0')
    {
        size_t cchLen = strlen(&pszzEnv[offEnv]);
        offEnv += cchLen + 1;
    }
    size_t const cchEnv = offEnv + 1;

    PRTUTF16 pwszzEnv = (PRTUTF16)HeapAlloc(GetProcessHeap(), 0, cchEnv * sizeof(RTUTF16));
    if (!pwszzEnv)
        return NULL;
    for (offEnv = 0; offEnv < cchEnv; offEnv++)
    {
        unsigned char ch = pwszzEnv[offEnv];
        if (!(ch & 0x80))
            pwszzEnv[offEnv] = ch;
        else
            pwszzEnv[offEnv] = '_';
    }
    return pwszzEnv;
}


DECL_KERNEL32(BOOL) Fake_FreeEnvironmentStringsW(LPWCH pwszzEnv)
{
    if (pwszzEnv)
        HeapFree(GetProcessHeap(), 0, pwszzEnv);
    return TRUE;
}


DECL_KERNEL32(int) Fake_GetLocaleInfoA(LCID idLocale, LCTYPE enmType, LPSTR pData, int cchData)
{
    NOREF(idLocale); NOREF(enmType); NOREF(pData); NOREF(cchData);
    //MY_ASSERT(false, "GetLocaleInfoA: idLocale=%#x enmType=%#x cchData=%#x", idLocale, enmType, cchData);
    MY_ASSERT(false, "GetLocaleInfoA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}


#if VCC_FAKES_TARGET >= 140
/** @since 3.51 */
DECL_KERNEL32(BOOL) Fake_EnumSystemLocalesW(LOCALE_ENUMPROCW pfnLocaleEnum, DWORD dwFlags)
{
    RT_NOREF(pfnLocaleEnum, dwFlags);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
#endif /* VCC_FAKES_TARGET >= 140 */


DECL_KERNEL32(BOOL) Fake_EnumSystemLocalesA(LOCALE_ENUMPROCA pfnCallback, DWORD fFlags)
{
    NOREF(pfnCallback); NOREF(fFlags);
    //MY_ASSERT(false, "EnumSystemLocalesA: pfnCallback=%p fFlags=%#x", pfnCallback, fFlags);
    MY_ASSERT(false, "EnumSystemLocalesA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}


DECL_KERNEL32(BOOL) Fake_IsValidLocale(LCID idLocale, DWORD fFlags)
{
    NOREF(idLocale); NOREF(fFlags);
    //MY_ASSERT(false, "IsValidLocale: idLocale fFlags=%#x", idLocale, fFlags);
    MY_ASSERT(false, "IsValidLocale");
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}


DECL_KERNEL32(DWORD_PTR) Fake_SetThreadAffinityMask(HANDLE hThread, DWORD_PTR fAffinityMask)
{
    SYSTEM_INFO SysInfo;
    GetSystemInfo(&SysInfo);
    //MY_ASSERT(false, "SetThreadAffinityMask: hThread=%p fAffinityMask=%p SysInfo.dwActiveProcessorMask=%p",
    //          hThread, fAffinityMask, SysInfo.dwActiveProcessorMask);
    MY_ASSERT(false, "SetThreadAffinityMask");
    if (   SysInfo.dwActiveProcessorMask == fAffinityMask
        || fAffinityMask                 == ~(DWORD_PTR)0)
        return fAffinityMask;

    SetLastError(ERROR_NOT_SUPPORTED);
    RT_NOREF(hThread);
    return 0;
}


DECL_KERNEL32(BOOL) Fake_GetProcessAffinityMask(HANDLE hProcess, PDWORD_PTR pfProcessAffinityMask, PDWORD_PTR pfSystemAffinityMask)
{
    SYSTEM_INFO SysInfo;
    GetSystemInfo(&SysInfo);
    //MY_ASSERT(false, "GetProcessAffinityMask: SysInfo.dwActiveProcessorMask=%p", SysInfo.dwActiveProcessorMask);
    MY_ASSERT(false, "GetProcessAffinityMask");
    if (pfProcessAffinityMask)
        *pfProcessAffinityMask = SysInfo.dwActiveProcessorMask;
    if (pfSystemAffinityMask)
        *pfSystemAffinityMask  = SysInfo.dwActiveProcessorMask;
    RT_NOREF(hProcess);
    return TRUE;
}


DECL_KERNEL32(BOOL) Fake_GetHandleInformation(HANDLE hObject, DWORD *pfFlags)
{
    OBJECT_HANDLE_FLAG_INFORMATION  Info  = { 0, 0 };
    DWORD                           cbRet = sizeof(Info);
    NTSTATUS rcNt = NtQueryObject(hObject, ObjectHandleFlagInformation, &Info, sizeof(Info), &cbRet);
    if (NT_SUCCESS(rcNt))
    {
        *pfFlags = (Info.Inherit          ? HANDLE_FLAG_INHERIT : 0)
                 | (Info.ProtectFromClose ? HANDLE_FLAG_PROTECT_FROM_CLOSE : 0);
        return TRUE;
    }
    *pfFlags = 0;
    //MY_ASSERT(rcNt == STATUS_INVALID_HANDLE, "rcNt=%#x", rcNt);
    MY_ASSERT(rcNt == STATUS_INVALID_HANDLE || rcNt == STATUS_INVALID_INFO_CLASS, "GetHandleInformation");
    SetLastError(rcNt == STATUS_INVALID_HANDLE ? ERROR_INVALID_HANDLE : ERROR_INVALID_FUNCTION); /* see also process-win.cpp */
    return FALSE;
}


DECL_KERNEL32(BOOL) Fake_SetHandleInformation(HANDLE hObject, DWORD fMask, DWORD fFlags)
{
    NOREF(hObject); NOREF(fMask); NOREF(fFlags);
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}



/**
 * Resolves all the APIs ones and for all, updating the fake IAT entries.
 */
DECLASM(void) FakeResolve_kernel32(void)
{
    CURRENT_VERSION_VARIABLE();

    HMODULE hmod = GetModuleHandleW(L"kernel32");
    MY_ASSERT(hmod != NULL, "kernel32");

#undef MAKE_IMPORT_ENTRY
#define MAKE_IMPORT_ENTRY(a_uMajorVer, a_uMinorVer, a_Name, a_cb) RESOLVE_IMPORT(a_uMajorVer, a_uMinorVer, a_Name, a_cb)
#ifdef VCC_FAKES_TARGET_VCC100
# include "vcc-fakes-kernel32-100.h"
#elif defined(VCC_FAKES_TARGET_VCC140)
# include "vcc-fakes-kernel32-141.h"
#elif defined(VCC_FAKES_TARGET_VCC141)
# include "vcc-fakes-kernel32-141.h"
#elif defined(VCC_FAKES_TARGET_VCC142)
# include "vcc-fakes-kernel32-141.h"
#else
# error "Port me!"
#endif

    g_fInitialized = true;
}


/* Dummy to force dragging in this object in the link, so the linker
   won't accidentally use the symbols from kernel32. */
extern "C" int vcc100_kernel32_fakes_cpp(void)
{
    return 42;
}

