/* $Id: MsiHack.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MsiHack - Exterimental DLL that intercept small ReadFile calls from
 *           MSI, CABINET and WINTEROP, buffering them using memory mapped files.
 *
 * @remarks Doesn't save as much as hoped on fast disks.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#define IPRT_NO_CRT_FOR_3RD_PARTY /* temp hack */
#include <iprt/win/windows.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef IPRT_NO_CRT
# include <wchar.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @MSI_HACK_HANDLE_TO_INDEX
 * Handle to g_papHandles index.
 */
#if ARCH_BITS == 64
# define MSI_HACK_HANDLE_TO_INDEX(hHandle)   (((uintptr_t)hHandle & ~UINT64_C(0x80000000)) >> 3)
#elif ARCH_BITS == 32
# define MSI_HACK_HANDLE_TO_INDEX(hHandle)   (((uintptr_t)hHandle & ~UINT32_C(0x80000000)) >> 2)
#else
# error "Unsupported or missing ARCH_BITS!"
#endif

/** Generic assertion macro. */
#define MSIHACK_ASSERT(a_Expr) \
    do { \
        if (!!(a_Expr))  { /* likely */ } \
        else MsiHackErrorF("Assertion failed at line " RT_STR(__LINE__) ": " #a_Expr "\n"); \
    } while (0)

/** Assertion macro that returns if expression is false. */
#define MSIHACK_ASSERT_RETURN(a_Expr, a_fRc) \
    do { \
        if (!!(a_Expr))  { /* likely */ } \
        else \
        { \
            MsiHackErrorF("Assertion failed at line " RT_STR(__LINE__) ": " #a_Expr "\n"); \
            return (a_fRc); \
        } \
    } while (0)

/** Assertion macro that executes a statemtn when false. */
#define MSIHACK_ASSERT_STMT(a_Expr, a_Stmt) \
    do { \
        if (!!(a_Expr))  { /* likely */ } \
        else \
        { \
            MsiHackErrorF("Assertion failed at line " RT_STR(__LINE__) ": " #a_Expr "\n"); \
            a_Stmt; \
        } \
    } while (0)

/** Assertion macro that executes a statemtn when false. */
#define MSIHACK_ASSERT_MSG(a_Expr, a_Msg) \
    do { \
        if (!!(a_Expr))  { /* likely */ } \
        else \
        { \
            MsiHackErrorF("Assertion failed at line " RT_STR(__LINE__) ": " #a_Expr "\n"); \
            MsiHackErrorF a_Msg; \
        } \
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Intercepted handle data.
 */
typedef struct MSIHACKHANDLE
{
    /** The actual handle value. */
    HANDLE              hHandle;
    /** The buffer. */
    uint8_t            *pbBuffer;
    /** Valid buffer size. */
    size_t              cbBuffer;
    /** The allocated buffer size. */
    size_t              cbBufferAlloc;
    /** The file offset of the buffer. */
    uint64_t            offFileBuffer;
    /** The file size. */
    uint64_t            cbFile;
    /** The current file offset. */
    uint64_t            offFile;
    /** Whether pbBuffer is a memory mapping of hHandle. */
    bool                fMemoryMapped;
    /** We only try caching a file onece. */
    bool                fDontTryAgain;
    /** Reference counter. */
    int32_t volatile    cRefs;
    /** Critical section protecting the handle. */
    CRITICAL_SECTION    CritSect;
} MSIHACKHANDLE;
/** Pointer to an intercepted handle. */
typedef MSIHACKHANDLE *PMSIHACKHANDLE;


/**
 * Replacement function entry.
 */
typedef struct MSIHACKREPLACEMENT
{
    /** The function name. */
    const char *pszFunction;
    /** The length of the function name. */
    size_t      cchFunction;
    /** The module name (optional). */
    const char *pszModule;
    /** The replacement function or data address. */
    uintptr_t   pfnReplacement;
} MSIHACKREPLACEMENT;
/** Pointer to a replacement function entry   */
typedef MSIHACKREPLACEMENT const *PCMSIHACKREPLACEMENT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Critical section protecting the handle table. */
static CRITICAL_SECTION     g_CritSect;
/** Size of the handle table. */
static size_t               g_cHandles;
/** The handle table. */
static PMSIHACKHANDLE      *g_papHandles;



void MsiHackErrorF(const char *pszFormat, ...)
{
    fprintf(stderr, "MsiHack: error: ");
    va_list va;
    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
}


void MsiHackDebugF(const char *pszFormat, ...)
{
    if (1)
    {
        fprintf(stderr, "MsiHack: debug: ");
        va_list va;
        va_start(va, pszFormat);
        vfprintf(stderr, pszFormat, va);
        va_end(va);
    }
}


/**
 * Destroys a handle.
 */
DECL_NO_INLINE(static, void) MsiHackHandleDestroy(PMSIHACKHANDLE pHandle)
{
    /* The handle value should always be invalid at this point! */
    MSIHACK_ASSERT(pHandle->hHandle == INVALID_HANDLE_VALUE);

    if (pHandle->fMemoryMapped)
        UnmapViewOfFile(pHandle->pbBuffer);
    else
        free(pHandle->pbBuffer);
    pHandle->pbBuffer = NULL;

    DeleteCriticalSection(&pHandle->CritSect);
    free(pHandle);
}


/**
 * Releases a handle reference.
 * @param   pHandle             The handle to release.
 */
DECLINLINE(void) MsiHackHandleRelease(PMSIHACKHANDLE pHandle)
{
    if (ASMAtomicDecS32(&pHandle->cRefs) != 0)
        return;
    MsiHackHandleDestroy(pHandle);
}


/**
 * Get and retain handle.
 *
 * @returns Pointer to a reference handle or NULL if not our handle.
 * @param   hHandle             The handle.
 */
DECLINLINE(PMSIHACKHANDLE) MsiHackHandleRetain(HANDLE hHandle)
{
    uintptr_t const idxHandle = MSI_HACK_HANDLE_TO_INDEX(hHandle);
    EnterCriticalSection(&g_CritSect);
    if (idxHandle < g_cHandles)
    {
        PMSIHACKHANDLE pHandle = g_papHandles[idxHandle];
        if (pHandle)
        {
            ASMAtomicIncS32(&pHandle->cRefs);
            LeaveCriticalSection(&g_CritSect);
            return pHandle;
        }
    }
    LeaveCriticalSection(&g_CritSect);
    return NULL;
}


/**
 * Enters @a pHandle into the handle table under @a hHandle.
 *
 * @returns true on succes, false on error.
 * @param   pHandle             The handle to enter.
 * @param   hHandle             The handle value to enter it under.
 */
static bool MsiHackHandleEnter(PMSIHACKHANDLE pHandle, HANDLE hHandle)
{
    uintptr_t const idxHandle = MSI_HACK_HANDLE_TO_INDEX(hHandle);
    EnterCriticalSection(&g_CritSect);

    /*
     * Make sure there is room in the handle table.
     */
    bool fOkay = idxHandle < g_cHandles;
    if (fOkay)
    { /* typical */ }
    else if (idxHandle < _1M)
    {
        size_t cNew = g_cHandles * 2;
        while (cNew < idxHandle)
            cNew *= 2;

        void *pvNew = realloc(g_papHandles, cNew * sizeof(g_papHandles[0]));
        if (pvNew)
        {
            g_papHandles = (PMSIHACKHANDLE *)pvNew;
            memset(&g_papHandles[g_cHandles], 0, (cNew - g_cHandles) * sizeof(g_papHandles[0]));
            g_cHandles   = cNew;
            fOkay = true;
        }
        else
            MsiHackErrorF("Failed to grow handle table from %p to %p entries!\n", g_cHandles, cNew);
    }
    else
        MsiHackErrorF("Handle %p (0x%p) is above the max handle table size limit!\n", hHandle, idxHandle);
    if (fOkay)
    {
        /*
         * Insert it into the table if the entry is empty.
         */
        if (g_papHandles[idxHandle] == NULL)
        {
            g_papHandles[idxHandle] = pHandle;
            LeaveCriticalSection(&g_CritSect);
            return true;
        }
        MsiHackErrorF("Handle table entry 0x%p (%p) is already busy with %p! Cannot replace with %p.\n",
                      hHandle, idxHandle, g_papHandles[idxHandle], pHandle);
    }

    LeaveCriticalSection(&g_CritSect);
    return false;
}


/**
 * Prepares a file for potential caching.
 *
 * If successful, the handled is entered into the handle table.
 *
 * @param   hFile               Handle to the file to cache.
 */
static void MsiHackFilePrepare(HANDLE hFile)
{
    DWORD const dwErrSaved = GetLastError();

    LARGE_INTEGER cbFile;
    if (GetFileSizeEx(hFile, &cbFile))
    {
        PMSIHACKHANDLE pHandle = (PMSIHACKHANDLE)calloc(1, sizeof(*pHandle));
        if (pHandle)
        {
            pHandle->cbFile        = cbFile.QuadPart;
            pHandle->pbBuffer      = NULL;
            pHandle->cbBuffer      = 0;
            pHandle->cbBufferAlloc = 0;
            pHandle->offFileBuffer = 0;
            pHandle->fMemoryMapped = true;
            pHandle->cRefs         = 1;
            InitializeCriticalSection(&pHandle->CritSect);
            if (MsiHackHandleEnter(pHandle, hFile))
            {
                SetLastError(dwErrSaved);
                return;
            }

            free(pHandle);
        }
    }

    SetLastError(dwErrSaved);
}


/**
 * Worker for MsiHackFileSetupCache
 *
 * @returns True if sucessfully cached, False if not.
 * @param   pHandle             The file.
 * @param   hFile               The current valid handle.
 */
static bool MsiHackFileSetupCache(PMSIHACKHANDLE pHandle, HANDLE hFile)
{
    DWORD const dwErrSaved = GetLastError();
    HANDLE hMapping = CreateFileMappingW(hFile, NULL /*pSecAttrs*/,  PAGE_READONLY,
                                         0 /*cbMaxLow*/, 0 /*cbMaxHigh*/, NULL /*pwszName*/);
    if (hMapping != NULL)
    {
        pHandle->pbBuffer = (uint8_t *)MapViewOfFile(hMapping, FILE_MAP_READ, 0 /*offFileHigh*/, 0 /*offFileLow*/,
                                                     (SIZE_T)pHandle->cbFile);
        if (pHandle->pbBuffer)
        {
            pHandle->cbBuffer      = (size_t)pHandle->cbFile;
            pHandle->cbBufferAlloc = (size_t)pHandle->cbFile;
            pHandle->offFileBuffer = 0;
            pHandle->fMemoryMapped = true;
            CloseHandle(hMapping);

            SetLastError(dwErrSaved);
            return true;
        }
        CloseHandle(hMapping);
    }
    SetLastError(dwErrSaved);
    pHandle->fDontTryAgain = true;
    return false;
}


/**
 * This is called to check if the file is cached and try cache it.
 *
 * We delay the actually caching till the file is read, so we don't waste time
 * mapping it into memory when all that is wanted is the file size or something
 * like that.
 *
 * @returns True if cached, False if not.
 * @param   pHandle             The file.
 * @param   hFile               The current valid handle.
 */
DECLINLINE(bool) MsiHackFileIsCached(PMSIHACKHANDLE pHandle, HANDLE hFile)
{
    if (pHandle->pbBuffer)
        return true;
    if (!pHandle->fDontTryAgain)
        return MsiHackFileSetupCache(pHandle, hFile);
    return false;
}


/** Kernel32 - CreateFileA */
static HANDLE WINAPI MsiHack_Kernel32_CreateFileA(LPCSTR pszFilename, DWORD dwDesiredAccess, DWORD dwShareMode,
                                                  LPSECURITY_ATTRIBUTES pSecAttrs, DWORD dwCreationDisposition,
                                                  DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    /*
     * If this is read-only access to the file, try cache it.
     */
    if (dwCreationDisposition == OPEN_EXISTING)
    {
        if (   dwDesiredAccess == GENERIC_READ
            || dwDesiredAccess == FILE_GENERIC_READ)
        {
            if (dwShareMode & FILE_SHARE_READ)
            {
                if (   !pSecAttrs
                    || (   pSecAttrs->nLength == sizeof(*pSecAttrs)
                        && pSecAttrs->lpSecurityDescriptor == NULL ) )
                {
                    HANDLE hFile = CreateFileA(pszFilename, dwDesiredAccess, dwShareMode, pSecAttrs,
                                               dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
                    if (hFile != INVALID_HANDLE_VALUE && hFile != NULL)
                    {
                        MsiHackDebugF("CreateFileA: %s\n", pszFilename );
                        MsiHackFilePrepare(hFile);
                    }
                    return hFile;
                }
            }
        }
    }

    /*
     * Don't intercept it.
     */
    return CreateFileA(pszFilename, dwDesiredAccess, dwShareMode, pSecAttrs,
                       dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}


/** Kernel32 - CreateFileW */
static HANDLE WINAPI MsiHack_Kernel32_CreateFileW(LPCWSTR pwszFilename, DWORD dwDesiredAccess, DWORD dwShareMode,
                                                  LPSECURITY_ATTRIBUTES pSecAttrs, DWORD dwCreationDisposition,
                                                  DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    /*
     * If this is read-only access to the file, try cache it.
     */
    if (dwCreationDisposition == OPEN_EXISTING)
    {
        if (   dwDesiredAccess == GENERIC_READ
            || dwDesiredAccess == FILE_GENERIC_READ)
        {
            if (dwShareMode & FILE_SHARE_READ)
            {
                if (   !pSecAttrs
                    || (   pSecAttrs->nLength == sizeof(*pSecAttrs)
                        && pSecAttrs->lpSecurityDescriptor == NULL ) )
                {
                    HANDLE hFile = CreateFileW(pwszFilename, dwDesiredAccess, dwShareMode, pSecAttrs,
                                               dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
                    if (hFile != INVALID_HANDLE_VALUE && hFile != NULL)
                    {
                        MsiHackDebugF("CreateFileW: %ls\n", pwszFilename);
                        MsiHackFilePrepare(hFile);
                    }
                    return hFile;
                }
            }
        }
    }

    /*
     * Don't intercept it.
     */
    return CreateFileW(pwszFilename, dwDesiredAccess, dwShareMode, pSecAttrs,
                       dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}


/** Kernel32 - SetFilePointer */
static DWORD WINAPI MsiHack_Kernel32_SetFilePointer(HANDLE hFile, LONG cbMove, PLONG pcbMoveHi, DWORD dwMoveMethod)
{
    /*
     * If intercepted handle, deal with it.
     */
    PMSIHACKHANDLE pHandle = MsiHackHandleRetain(hFile);
    if (pHandle)
    {
        if (MsiHackFileIsCached(pHandle, hFile))
        {
            int64_t offMove = pcbMoveHi ? ((int64_t)*pcbMoveHi << 32) | cbMove : cbMove;

            EnterCriticalSection(&pHandle->CritSect);
            switch (dwMoveMethod)
            {
                case FILE_BEGIN:
                    break;
                case FILE_CURRENT:
                    offMove += pHandle->offFile;
                    break;
                case FILE_END:
                    offMove += pHandle->cbFile;
                    break;
                default:
                    LeaveCriticalSection(&pHandle->CritSect);
                    MsiHackHandleRelease(pHandle);

                    MsiHackErrorF("SetFilePointer(%p) - invalid method!\n", hFile);
                    SetLastError(ERROR_INVALID_PARAMETER);
                    return INVALID_SET_FILE_POINTER;
            }

            if (offMove >= 0)
            {
                /* Seeking beyond the end isn't useful, so just clamp it. */
                if (offMove >= (int64_t)pHandle->cbFile)
                    offMove = (int64_t)pHandle->cbFile;
                pHandle->offFile = (uint64_t)offMove;
            }
            else
            {
                LeaveCriticalSection(&pHandle->CritSect);
                MsiHackHandleRelease(pHandle);

                MsiHackErrorF("SetFilePointer(%p) - negative seek!\n", hFile);
                SetLastError(ERROR_NEGATIVE_SEEK);
                return INVALID_SET_FILE_POINTER;
            }

            LeaveCriticalSection(&pHandle->CritSect);
            MsiHackHandleRelease(pHandle);

            if (pcbMoveHi)
                *pcbMoveHi = (uint64_t)offMove >> 32;
            SetLastError(NO_ERROR);
            return (uint32_t)offMove;
        }
        MsiHackHandleRelease(pHandle);
    }

    /*
     * Not one of ours.
     */
    return SetFilePointer(hFile, cbMove, pcbMoveHi, dwMoveMethod);
}


/** Kernel32 - SetFilePointerEx */
static BOOL WINAPI MsiHack_Kernel32_SetFilePointerEx(HANDLE hFile, LARGE_INTEGER offMove, PLARGE_INTEGER poffNew,
                                                     DWORD dwMoveMethod)
{
    /*
     * If intercepted handle, deal with it.
     */
    PMSIHACKHANDLE pHandle = MsiHackHandleRetain(hFile);
    if (pHandle)
    {
        if (MsiHackFileIsCached(pHandle, hFile))
        {
            int64_t offMyMove = offMove.QuadPart;

            EnterCriticalSection(&pHandle->CritSect);
            switch (dwMoveMethod)
            {
                case FILE_BEGIN:
                    break;
                case FILE_CURRENT:
                    offMyMove += pHandle->offFile;
                    break;
                case FILE_END:
                    offMyMove += pHandle->cbFile;
                    break;
                default:
                    LeaveCriticalSection(&pHandle->CritSect);
                    MsiHackHandleRelease(pHandle);

                    MsiHackErrorF("SetFilePointerEx(%p) - invalid method!\n", hFile);
                    SetLastError(ERROR_INVALID_PARAMETER);
                    return INVALID_SET_FILE_POINTER;
            }
            if (offMyMove >= 0)
            {
                /* Seeking beyond the end isn't useful, so just clamp it. */
                if (offMyMove >= (int64_t)pHandle->cbFile)
                    offMyMove = (int64_t)pHandle->cbFile;
                pHandle->offFile = (uint64_t)offMyMove;
            }
            else
            {
                LeaveCriticalSection(&pHandle->CritSect);
                MsiHackHandleRelease(pHandle);

                MsiHackErrorF("SetFilePointerEx(%p) - negative seek!\n", hFile);
                SetLastError(ERROR_NEGATIVE_SEEK);
                return INVALID_SET_FILE_POINTER;
            }

            LeaveCriticalSection(&pHandle->CritSect);
            MsiHackHandleRelease(pHandle);

            if (poffNew)
                poffNew->QuadPart = offMyMove;
            return TRUE;
        }
        MsiHackHandleRelease(pHandle);
    }

    /*
     * Not one of ours.
     */
    return SetFilePointerEx(hFile, offMove, poffNew, dwMoveMethod);
}


/** Kernel32 - ReadFile */
static BOOL WINAPI MsiHack_Kernel32_ReadFile(HANDLE hFile, LPVOID pvBuffer, DWORD cbToRead, LPDWORD pcbActuallyRead,
                                             LPOVERLAPPED pOverlapped)
{
    /*
     * If intercepted handle, deal with it.
     */
    PMSIHACKHANDLE pHandle = MsiHackHandleRetain(hFile);
    if (pHandle)
    {
        if (MsiHackFileIsCached(pHandle, hFile))
        {
            EnterCriticalSection(&pHandle->CritSect);
            uint32_t cbActually = pHandle->cbFile - pHandle->offFile;
            if (cbActually > cbToRead)
                cbActually = cbToRead;

            memcpy(pvBuffer, &pHandle->pbBuffer[pHandle->offFile], cbActually);
            pHandle->offFile += cbActually;

            LeaveCriticalSection(&pHandle->CritSect);
            MsiHackHandleRelease(pHandle);

            MSIHACK_ASSERT(!pOverlapped); MSIHACK_ASSERT(pcbActuallyRead);
            *pcbActuallyRead = cbActually;

            return TRUE;
        }
        MsiHackHandleRelease(pHandle);
    }

    /*
     * Not one of ours.
     */
    return ReadFile(hFile, pvBuffer, cbToRead, pcbActuallyRead, pOverlapped);
}


/** Kernel32 - ReadFileEx */
static BOOL WINAPI MsiHack_Kernel32_ReadFileEx(HANDLE hFile, LPVOID pvBuffer, DWORD cbToRead, LPOVERLAPPED pOverlapped,
                                               LPOVERLAPPED_COMPLETION_ROUTINE pfnCompletionRoutine)
{
    /*
     * If intercepted handle, deal with it.
     */
    PMSIHACKHANDLE pHandle = MsiHackHandleRetain(hFile);
    if (pHandle)
    {
        MsiHackHandleRelease(pHandle);

        MsiHackErrorF("Unexpected ReadFileEx call!\n");
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }

    /*
     * Not one of ours.
     */
    return ReadFileEx(hFile, pvBuffer, cbToRead, pOverlapped, pfnCompletionRoutine);
}


/** Kernel32 - DuplicateHandle */
static BOOL WINAPI MsiHack_Kernel32_DuplicateHandle(HANDLE hSrcProc, HANDLE hSrc, HANDLE hDstProc, PHANDLE phNew,
                                                    DWORD dwDesiredAccess, BOOL fInheritHandle, DWORD dwOptions)
{
    /*
     * We must catch our handles being duplicated.
     */
    if (   hSrcProc == GetCurrentProcess()
        && hDstProc == hSrcProc)
    {
        PMSIHACKHANDLE pSrcHandle = MsiHackHandleRetain(hSrcProc);
        if (pSrcHandle)
        {
            if (dwOptions & DUPLICATE_CLOSE_SOURCE)
                MsiHackErrorF("DUPLICATE_CLOSE_SOURCE is not implemented!\n");
            BOOL fRet = DuplicateHandle(hSrcProc, hSrc, hDstProc, phNew, dwDesiredAccess, fInheritHandle, dwOptions);
            if (fRet)
            {
                if (MsiHackHandleEnter(pSrcHandle, *phNew))
                    return fRet; /* don't release reference. */

                CloseHandle(*phNew);
                *phNew = INVALID_HANDLE_VALUE;
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                fRet = FALSE;
            }
            MsiHackHandleRelease(pSrcHandle);
            return fRet;
        }
    }

    /*
     * Not one of ours.
     */
    return DuplicateHandle(hSrcProc, hSrc, hDstProc, phNew, dwDesiredAccess, fInheritHandle, dwOptions);
}


/** Kernel32 - CloseHandle */
static BOOL WINAPI MsiHack_Kernel32_CloseHandle(HANDLE hObject)
{
    /*
     * If intercepted handle, remove it from the table.
     */
    uintptr_t const idxHandle = MSI_HACK_HANDLE_TO_INDEX(hObject);
    EnterCriticalSection(&g_CritSect);
    if (idxHandle < g_cHandles)
    {
        PMSIHACKHANDLE pHandle = g_papHandles[idxHandle];
        if (pHandle)
        {
            g_papHandles[idxHandle] = NULL;
            LeaveCriticalSection(&g_CritSect);

            /*
             * Then close the handle.
             */
            EnterCriticalSection(&pHandle->CritSect);
            BOOL fRet = CloseHandle(hObject);
            pHandle->hHandle = INVALID_HANDLE_VALUE;
            DWORD dwErr = GetLastError();
            LeaveCriticalSection(&pHandle->CritSect);

            /*
             * And finally release the reference held by the handle table.
             */
            MsiHackHandleRelease(pHandle);
            SetLastError(dwErr);
            return fRet;
        }
    }

    /*
     * Not one of ours.
     */
    LeaveCriticalSection(&g_CritSect);
    return CloseHandle(hObject);
}




/** Replacement functions.   */
static const MSIHACKREPLACEMENT g_aReplaceFunctions[] =
{
    { RT_STR_TUPLE("CreateFileA"),          NULL,       (uintptr_t)MsiHack_Kernel32_CreateFileA },
    { RT_STR_TUPLE("CreateFileW"),          NULL,       (uintptr_t)MsiHack_Kernel32_CreateFileW },
    { RT_STR_TUPLE("ReadFile"),             NULL,       (uintptr_t)MsiHack_Kernel32_ReadFile },
    { RT_STR_TUPLE("ReadFileEx"),           NULL,       (uintptr_t)MsiHack_Kernel32_ReadFileEx },
    { RT_STR_TUPLE("SetFilePointer"),       NULL,       (uintptr_t)MsiHack_Kernel32_SetFilePointer },
    { RT_STR_TUPLE("SetFilePointerEx"),     NULL,       (uintptr_t)MsiHack_Kernel32_SetFilePointerEx },
    { RT_STR_TUPLE("DuplicateHandle"),      NULL,       (uintptr_t)MsiHack_Kernel32_DuplicateHandle },
    { RT_STR_TUPLE("CloseHandle"),          NULL,       (uintptr_t)MsiHack_Kernel32_CloseHandle },
};



/**
 * Patches the import table of the given DLL.
 *
 * @returns true on success, false on failure.
 * @param   hmod                .
 */
__declspec(dllexport) /* kBuild workaround */
bool MsiHackPatchDll(HMODULE hmod)
{
    uint8_t const * const pbImage = (uint8_t const *)hmod;

    /*
     * Locate the import descriptors.
     */
    /* MZ header and PE headers. */
    IMAGE_NT_HEADERS const *pNtHdrs;
    IMAGE_DOS_HEADER const *pMzHdr = (IMAGE_DOS_HEADER const *)pbImage;
    if (pMzHdr->e_magic == IMAGE_DOS_SIGNATURE)
        pNtHdrs = (IMAGE_NT_HEADERS const *)&pbImage[pMzHdr->e_lfanew];
    else
        pNtHdrs = (IMAGE_NT_HEADERS const *)pbImage;

    /* Check PE header. */
    MSIHACK_ASSERT_RETURN(pNtHdrs->Signature == IMAGE_NT_SIGNATURE,   false);
    MSIHACK_ASSERT_RETURN(pNtHdrs->FileHeader.SizeOfOptionalHeader == sizeof(pNtHdrs->OptionalHeader), false);
    uint32_t const cbImage = pNtHdrs->OptionalHeader.SizeOfImage;

    /* Locate the import descriptor array. */
    IMAGE_DATA_DIRECTORY const *pDirEnt;
    pDirEnt = (IMAGE_DATA_DIRECTORY const *)&pNtHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (   pDirEnt->Size > 0
        && pDirEnt->VirtualAddress != 0)
    {
        const IMAGE_IMPORT_DESCRIPTOR  *pImpDesc    = (const IMAGE_IMPORT_DESCRIPTOR *)&pbImage[pDirEnt->VirtualAddress];
        uint32_t                        cLeft       = pDirEnt->Size / sizeof(*pImpDesc);
        MEMORY_BASIC_INFORMATION        ProtInfo    = { NULL, NULL, 0, 0, 0, 0, 0 };
        uint8_t                        *pbProtRange = NULL;
        SIZE_T                          cbProtRange = 0;
        DWORD                           fOldProt    = 0;
        uint32_t const                  cbPage      = 0x1000;
        BOOL                            fRc;

        MSIHACK_ASSERT_RETURN(pDirEnt->VirtualAddress < cbImage, false);
        MSIHACK_ASSERT_RETURN(pDirEnt->Size < cbImage, false);
        MSIHACK_ASSERT_RETURN(pDirEnt->VirtualAddress + pDirEnt->Size <= cbImage, false);

        /*
         * Walk the import descriptor array.
         * Note! This only works if there's a backup thunk array, otherwise we cannot get at the name.
         */
        while (   cLeft-- > 0
               && pImpDesc->Name > 0
               && pImpDesc->FirstThunk > 0)
        {
            uint32_t            iThunk;
            const char * const  pszImport   = (const char *)&pbImage[pImpDesc->Name];
            PIMAGE_THUNK_DATA   paThunks    = (PIMAGE_THUNK_DATA)&pbImage[pImpDesc->FirstThunk];
            PIMAGE_THUNK_DATA   paOrgThunks = (PIMAGE_THUNK_DATA)&pbImage[pImpDesc->OriginalFirstThunk];
            MSIHACK_ASSERT_RETURN(pImpDesc->Name < cbImage, false);
            MSIHACK_ASSERT_RETURN(pImpDesc->FirstThunk < cbImage, false);
            MSIHACK_ASSERT_RETURN(pImpDesc->OriginalFirstThunk < cbImage, false);
            MSIHACK_ASSERT_RETURN(pImpDesc->OriginalFirstThunk != pImpDesc->FirstThunk, false);
            MSIHACK_ASSERT_RETURN(pImpDesc->OriginalFirstThunk, false);

            /* Iterate the thunks. */
            for (iThunk = 0; paOrgThunks[iThunk].u1.Ordinal != 0; iThunk++)
            {
                uintptr_t const off = paOrgThunks[iThunk].u1.Function;
                MSIHACK_ASSERT_RETURN(off < cbImage, false);
                if (!IMAGE_SNAP_BY_ORDINAL(off))
                {
                    IMAGE_IMPORT_BY_NAME const *pName     = (IMAGE_IMPORT_BY_NAME const *)&pbImage[off];
                    size_t const                cchSymbol = strlen((const char *)&pName->Name[0]);
                    uint32_t                    i         = RT_ELEMENTS(g_aReplaceFunctions);
                    while (i-- > 0)
                        if (   g_aReplaceFunctions[i].cchFunction == cchSymbol
                            && memcmp(g_aReplaceFunctions[i].pszFunction, pName->Name, cchSymbol) == 0)
                        {
                            if (   !g_aReplaceFunctions[i].pszModule
                                || stricmp(g_aReplaceFunctions[i].pszModule, pszImport) == 0)
                            {
                                MsiHackDebugF("Replacing %s!%s\n", pszImport, pName->Name);

                                /* The .rdata section is normally read-only, so we need to make it writable first. */
                                if ((uintptr_t)&paThunks[iThunk] - (uintptr_t)pbProtRange >= cbPage)
                                {
                                    /* Restore previous .rdata page. */
                                    if (fOldProt)
                                    {
                                        fRc = VirtualProtect(pbProtRange, cbProtRange, fOldProt, NULL /*pfOldProt*/);
                                        MSIHACK_ASSERT(fRc);
                                        fOldProt = 0;
                                    }

                                    /* Query attributes for the current .rdata page. */
                                    pbProtRange = (uint8_t *)((uintptr_t)&paThunks[iThunk] & ~(uintptr_t)(cbPage - 1));
                                    cbProtRange = VirtualQuery(pbProtRange, &ProtInfo, sizeof(ProtInfo));
                                    MSIHACK_ASSERT(cbProtRange);
                                    if (cbProtRange)
                                    {
                                        switch (ProtInfo.Protect)
                                        {
                                            case PAGE_READWRITE:
                                            case PAGE_WRITECOPY:
                                            case PAGE_EXECUTE_READWRITE:
                                            case PAGE_EXECUTE_WRITECOPY:
                                                /* Already writable, nothing to do. */
                                                fRc = TRUE;
                                                break;

                                            default:
                                                MSIHACK_ASSERT_MSG(false, ("%#x\n", ProtInfo.Protect));
                                            case PAGE_READONLY:
                                                cbProtRange = cbPage;
                                                fRc = VirtualProtect(pbProtRange, cbProtRange, PAGE_READWRITE, &fOldProt);
                                                break;

                                            case PAGE_EXECUTE:
                                            case PAGE_EXECUTE_READ:
                                                cbProtRange = cbPage;
                                                fRc = VirtualProtect(pbProtRange, cbProtRange, PAGE_EXECUTE_READWRITE, &fOldProt);
                                                break;
                                        }
                                        MSIHACK_ASSERT_STMT(fRc, fOldProt = 0);
                                    }
                                }

                                paThunks[iThunk].u1.AddressOfData = g_aReplaceFunctions[i].pfnReplacement;
                                break;
                            }
                        }
                }
            }


            /* Next import descriptor. */
            pImpDesc++;
        }


        if (fOldProt)
        {
            DWORD fIgnore = 0;
            fRc = VirtualProtect(pbProtRange, cbProtRange, fOldProt, &fIgnore);
            MSIHACK_ASSERT_MSG(fRc, ("%u\n", GetLastError())); NOREF(fRc);
        }
        return true;
    }
    MsiHackErrorF("No imports in target DLL!\n");
    return false;
}


/**
 * The Dll main entry point.
 */
BOOL __stdcall DllMain(HANDLE hModule, DWORD dwReason, PVOID pvReserved)
{
    RT_NOREF_PV(pvReserved);

    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            /*
             * Make sure we cannot be unloaded.  This saves us the bother of ever
             * having to unpatch MSI.DLL
             */
            WCHAR wszName[MAX_PATH*2];
            SetLastError(NO_ERROR);
            if (   GetModuleFileNameW((HMODULE)hModule, wszName, RT_ELEMENTS(wszName)) > 0
                && GetLastError() == NO_ERROR)
            {
                int cExtraLoads = 32;
                while (cExtraLoads-- > 0)
                    LoadLibraryW(wszName);
            }

            /*
             * Initialize globals.
             */
            InitializeCriticalSection(&g_CritSect);
            g_papHandles = (PMSIHACKHANDLE *)calloc(sizeof(g_papHandles[0]), 8192);
            if (g_papHandles)
                g_cHandles = 8192;
            else
            {
                MsiHackErrorF("Failed to allocate handle table!\n");
                return FALSE;
            }

            /*
             * Find MSI and patch it.
             */
            static struct
            {
                const wchar_t   *pwszName;  /**< Dll name. */
                bool             fSystem;   /**< Set if system, clear if wix. */
            } s_aDlls[] =
            {
                { L"MSI.DLL", true },
                { L"CABINET.DLL", true },
                { L"WINTEROP.DLL", false },
            };

            for (unsigned i = 0; i < RT_ELEMENTS(s_aDlls); i++)
            {
                HMODULE hmodTarget = GetModuleHandleW(s_aDlls[i].pwszName);
                if (!hmodTarget)
                {
                    UINT cwc;
                    if (s_aDlls[i].fSystem)
                        cwc = GetSystemDirectoryW(wszName, RT_ELEMENTS(wszName) - 16);
                    else
                    {
                        cwc = GetModuleFileNameW(GetModuleHandleW(NULL), wszName, RT_ELEMENTS(wszName) - 16);
                        while (cwc > 0 && (wszName[cwc - 1] != '\\' && wszName[cwc - 1] != '/'))
                            wszName[--cwc] = '\0';
                    }
                    wszName[cwc++] = '\\';
                    wcscpy(&wszName[cwc], s_aDlls[i].pwszName);
                    hmodTarget = LoadLibraryW(wszName);
                    if (!hmodTarget)
                    {
                        MsiHackErrorF("%ls could not be found nor loaded (%ls): %u\n", &wszName[cwc], wszName, GetLastError());
                        return FALSE;
                    }
                }

                if (MsiHackPatchDll(hmodTarget))
                    MsiHackDebugF("MsiHackPatchDll returned successfully for %ls.\n", s_aDlls[i].pwszName);
                else
                    MsiHackErrorF("MsiHackPatchDll failed for %ls!\n", s_aDlls[i].pwszName);
            }
            break;
        }

        case DLL_PROCESS_DETACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        default:
            /* ignore */
            break;
    }
    return TRUE;
}

