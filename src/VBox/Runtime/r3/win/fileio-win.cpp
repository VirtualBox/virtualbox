/* $Id: fileio-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File I/O, native implementation for the Windows host platform.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_DIR
#ifndef _WIN32_WINNT
# define _WIN32_WINNT 0x0500
#endif
#include <iprt/nt/nt-and-windows.h>

#include <iprt/file.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/ldr.h>
#include <iprt/log.h>
#include <iprt/utf16.h>
#include "internal/file.h"
#include "internal/fs.h"
#include "internal/path.h"
#include "internal-r3-win.h" /* For g_enmWinVer + kRTWinOSType_XXX */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
typedef BOOL WINAPI FNVERIFYCONSOLEIOHANDLE(HANDLE);
typedef FNVERIFYCONSOLEIOHANDLE *PFNVERIFYCONSOLEIOHANDLE; /* No, nobody fell on the keyboard, really! */


/**
 * This is wrapper around the ugly SetFilePointer api.
 *
 * It's equivalent to SetFilePointerEx which we so unfortunately cannot use because of
 * it not being present in NT4 GA.
 *
 * @returns Success indicator. Extended error information obtainable using GetLastError().
 * @param   hFile       Filehandle.
 * @param   offSeek     Offset to seek.
 * @param   poffNew     Where to store the new file offset. NULL allowed.
 * @param   uMethod     Seek method. (The windows one!)
 */
DECLINLINE(bool) MySetFilePointer(RTFILE hFile, uint64_t offSeek, uint64_t *poffNew, unsigned uMethod)
{
    bool            fRc;
    LARGE_INTEGER   off;

    off.QuadPart = offSeek;
#if 1
    if (off.LowPart != INVALID_SET_FILE_POINTER)
    {
        off.LowPart = SetFilePointer((HANDLE)RTFileToNative(hFile), off.LowPart, &off.HighPart, uMethod);
        fRc = off.LowPart != INVALID_SET_FILE_POINTER;
    }
    else
    {
        SetLastError(NO_ERROR);
        off.LowPart = SetFilePointer((HANDLE)RTFileToNative(hFile), off.LowPart, &off.HighPart, uMethod);
        fRc = GetLastError() == NO_ERROR;
    }
#else
    fRc = SetFilePointerEx((HANDLE)RTFileToNative(hFile), off, &off, uMethod);
#endif
    if (fRc && poffNew)
        *poffNew = off.QuadPart;
    return fRc;
}


/**
 * Helper for checking if a VERR_DISK_FULL isn't a VERR_FILE_TOO_BIG.
 * @returns VERR_DISK_FULL or VERR_FILE_TOO_BIG.
 */
static int rtFileWinCheckIfDiskReallyFull(RTFILE hFile, uint64_t cbDesired)
{
    /*
     * Windows doesn't appear to have a way to query the file size limit of a
     * file system, so we have to deduce the limit from the file system driver name.
     * This means it will only work for known file systems.
     */
    if (cbDesired >= _2G - 1)
    {
        uint64_t cbMaxFile = UINT64_MAX;
        RTFSTYPE enmFsType;
        int rc = rtNtQueryFsType((HANDLE)RTFileToNative(hFile), &enmFsType);
        if (RT_SUCCESS(rc))
            switch (enmFsType)
            {
                case RTFSTYPE_NTFS:
                case RTFSTYPE_EXFAT:
                case RTFSTYPE_UDF:
                    cbMaxFile = UINT64_C(0xffffffffffffffff); /* (May be limited by IFS.) */
                    break;

                case RTFSTYPE_ISO9660:
                    cbMaxFile = 8 *_1T;
                    break;

                case RTFSTYPE_FAT:
                    cbMaxFile = _4G;
                    break;

                case RTFSTYPE_HPFS:
                    cbMaxFile = _2G;
                    break;

                default:
                    break;
            }
        if (cbDesired >= cbMaxFile)
            return VERR_FILE_TOO_BIG;
    }
    return VERR_DISK_FULL;
}


RTR3DECL(int) RTFileFromNative(PRTFILE pFile, RTHCINTPTR uNative)
{
    HANDLE h = (HANDLE)uNative;
    AssertCompile(sizeof(h) == sizeof(uNative));
    if (h == INVALID_HANDLE_VALUE)
    {
        AssertMsgFailed(("%p\n", uNative));
        *pFile = NIL_RTFILE;
        return VERR_INVALID_HANDLE;
    }
    *pFile = (RTFILE)h;
    return VINF_SUCCESS;
}


RTR3DECL(RTHCINTPTR) RTFileToNative(RTFILE hFile)
{
    AssertReturn(hFile != NIL_RTFILE, (RTHCINTPTR)INVALID_HANDLE_VALUE);
    return (RTHCINTPTR)hFile;
}


RTR3DECL(int) RTFileOpen(PRTFILE pFile, const char *pszFilename, uint64_t fOpen)
{
    return RTFileOpenEx(pszFilename, fOpen, pFile, NULL);
}


RTDECL(int) RTFileOpenEx(const char *pszFilename, uint64_t fOpen, PRTFILE phFile, PRTFILEACTION penmActionTaken)
{
    /*
     * Validate input.
     */
    AssertReturn(phFile, VERR_INVALID_PARAMETER);
    *phFile = NIL_RTFILE;
    if (penmActionTaken)
        *penmActionTaken = RTFILEACTION_INVALID;
    AssertReturn(pszFilename, VERR_INVALID_PARAMETER);

    /*
     * Merge forced open flags and validate them.
     */
    int rc = rtFileRecalcAndValidateFlags(&fOpen);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Determine disposition, access, share mode, creation flags, and security attributes
     * for the CreateFile API call.
     */
    DWORD dwCreationDisposition;
    switch (fOpen & RTFILE_O_ACTION_MASK)
    {
        case RTFILE_O_OPEN:
            dwCreationDisposition = fOpen & RTFILE_O_TRUNCATE ? TRUNCATE_EXISTING : OPEN_EXISTING;
            break;
        case RTFILE_O_OPEN_CREATE:
            dwCreationDisposition = OPEN_ALWAYS;
            break;
        case RTFILE_O_CREATE:
            dwCreationDisposition = CREATE_NEW;
            break;
        case RTFILE_O_CREATE_REPLACE:
            dwCreationDisposition = CREATE_ALWAYS;
            break;
        default:
            AssertMsgFailedReturn(("Impossible fOpen=%#llx\n", fOpen), VERR_INVALID_FLAGS);
    }

    DWORD dwDesiredAccess;
    switch (fOpen & RTFILE_O_ACCESS_MASK)
    {
        case RTFILE_O_READ:
            dwDesiredAccess = FILE_GENERIC_READ; /* RTFILE_O_APPEND is ignored. */
            break;
        case RTFILE_O_WRITE:
            dwDesiredAccess = fOpen & RTFILE_O_APPEND
                            ? FILE_GENERIC_WRITE & ~FILE_WRITE_DATA
                            : FILE_GENERIC_WRITE;
            break;
        case RTFILE_O_READWRITE:
            dwDesiredAccess = fOpen & RTFILE_O_APPEND
                            ? FILE_GENERIC_READ | (FILE_GENERIC_WRITE & ~FILE_WRITE_DATA)
                            : FILE_GENERIC_READ | FILE_GENERIC_WRITE;
            break;
        case RTFILE_O_ATTR_ONLY:
            if (fOpen & RTFILE_O_ACCESS_ATTR_MASK)
            {
                dwDesiredAccess = 0;
                break;
            }
            RT_FALL_THRU();
        default:
            AssertMsgFailedReturn(("Impossible fOpen=%#llx\n", fOpen), VERR_INVALID_FLAGS);
    }
    if (dwCreationDisposition == TRUNCATE_EXISTING)
        /* Required for truncating the file (see MSDN), it is *NOT* part of FILE_GENERIC_WRITE. */
        dwDesiredAccess |= GENERIC_WRITE;

    /* RTFileSetMode needs following rights as well. */
    switch (fOpen & RTFILE_O_ACCESS_ATTR_MASK)
    {
        case RTFILE_O_ACCESS_ATTR_READ:      dwDesiredAccess |= FILE_READ_ATTRIBUTES  | SYNCHRONIZE; break;
        case RTFILE_O_ACCESS_ATTR_WRITE:     dwDesiredAccess |= FILE_WRITE_ATTRIBUTES | SYNCHRONIZE; break;
        case RTFILE_O_ACCESS_ATTR_READWRITE: dwDesiredAccess |= FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE; break;
        default:
            /* Attributes access is the same as the file access. */
            switch (fOpen & RTFILE_O_ACCESS_MASK)
            {
                case RTFILE_O_READ:          dwDesiredAccess |= FILE_READ_ATTRIBUTES  | SYNCHRONIZE; break;
                case RTFILE_O_WRITE:         dwDesiredAccess |= FILE_WRITE_ATTRIBUTES | SYNCHRONIZE; break;
                case RTFILE_O_READWRITE:     dwDesiredAccess |= FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE; break;
                default:
                    AssertMsgFailedReturn(("Impossible fOpen=%#llx\n", fOpen), VERR_INVALID_FLAGS);
            }
    }

    DWORD dwShareMode;
    switch (fOpen & RTFILE_O_DENY_MASK)
    {
        case RTFILE_O_DENY_NONE:                                dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
        case RTFILE_O_DENY_READ:                                dwShareMode = FILE_SHARE_WRITE; break;
        case RTFILE_O_DENY_WRITE:                               dwShareMode = FILE_SHARE_READ; break;
        case RTFILE_O_DENY_READWRITE:                           dwShareMode = 0; break;

        case RTFILE_O_DENY_NOT_DELETE:                          dwShareMode = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE; break;
        case RTFILE_O_DENY_NOT_DELETE | RTFILE_O_DENY_READ:     dwShareMode = FILE_SHARE_DELETE | FILE_SHARE_WRITE; break;
        case RTFILE_O_DENY_NOT_DELETE | RTFILE_O_DENY_WRITE:    dwShareMode = FILE_SHARE_DELETE | FILE_SHARE_READ; break;
        case RTFILE_O_DENY_NOT_DELETE | RTFILE_O_DENY_READWRITE:dwShareMode = FILE_SHARE_DELETE; break;
        default:
            AssertMsgFailedReturn(("Impossible fOpen=%#llx\n", fOpen), VERR_INVALID_FLAGS);
    }

    SECURITY_ATTRIBUTES  SecurityAttributes;
    PSECURITY_ATTRIBUTES pSecurityAttributes = NULL;
    if (fOpen & RTFILE_O_INHERIT)
    {
        SecurityAttributes.nLength              = sizeof(SecurityAttributes);
        SecurityAttributes.lpSecurityDescriptor = NULL;
        SecurityAttributes.bInheritHandle       = TRUE;
        pSecurityAttributes = &SecurityAttributes;
    }

    DWORD dwFlagsAndAttributes;
    dwFlagsAndAttributes = !(fOpen & RTFILE_O_TEMP_AUTO_DELETE) ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_TEMPORARY;
    if (fOpen & RTFILE_O_TEMP_AUTO_DELETE)
        fOpen |= FILE_FLAG_DELETE_ON_CLOSE;
    if (fOpen & RTFILE_O_WRITE_THROUGH)
        dwFlagsAndAttributes |= FILE_FLAG_WRITE_THROUGH;
    if (fOpen & RTFILE_O_ASYNC_IO)
        dwFlagsAndAttributes |= FILE_FLAG_OVERLAPPED;
    if (fOpen & RTFILE_O_NO_CACHE)
    {
        dwFlagsAndAttributes |= FILE_FLAG_NO_BUFFERING;
        dwDesiredAccess &= ~FILE_APPEND_DATA;
    }

    /*
     * Open/Create the file.
     */
    PRTUTF16 pwszFilename;
    rc = RTPathWinFromUtf8(&pwszFilename, pszFilename, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        HANDLE hFile = CreateFileW(pwszFilename,
                                   dwDesiredAccess,
                                   dwShareMode,
                                   pSecurityAttributes,
                                   dwCreationDisposition,
                                   dwFlagsAndAttributes,
                                   NULL);
        DWORD const dwErr = GetLastError();
        if (hFile != INVALID_HANDLE_VALUE)
        {
            /*
             * Calculate the action taken value.
             */
            RTFILEACTION enmActionTaken;
            switch (dwCreationDisposition)
            {
                case CREATE_NEW:
                    enmActionTaken = RTFILEACTION_CREATED;
                    break;
                case CREATE_ALWAYS:
                    AssertMsg(dwErr == ERROR_ALREADY_EXISTS || dwErr == NO_ERROR, ("%u\n", dwErr));
                    enmActionTaken = dwErr == ERROR_ALREADY_EXISTS ? RTFILEACTION_REPLACED : RTFILEACTION_CREATED;
                    break;
                case OPEN_EXISTING:
                    enmActionTaken = RTFILEACTION_OPENED;
                    break;
                case OPEN_ALWAYS:
                    AssertMsg(dwErr == ERROR_ALREADY_EXISTS || dwErr == NO_ERROR, ("%u\n", dwErr));
                    enmActionTaken = dwErr == ERROR_ALREADY_EXISTS ? RTFILEACTION_OPENED : RTFILEACTION_CREATED;
                    break;
                case TRUNCATE_EXISTING:
                    enmActionTaken = RTFILEACTION_TRUNCATED;
                    break;
                default:
                    AssertMsgFailed(("%d %#x\n", dwCreationDisposition, dwCreationDisposition));
                    enmActionTaken = RTFILEACTION_INVALID;
                    break;
            }

            /*
             * Turn off indexing of directory through Windows Indexing Service if
             * we created a new file or replaced an existing one.
             */
            if (   (fOpen & RTFILE_O_NOT_CONTENT_INDEXED)
                && (   enmActionTaken == RTFILEACTION_CREATED
                    || enmActionTaken == RTFILEACTION_REPLACED) )
            {
                /** @todo there must be a way to do this via the handle! */
                if (!SetFileAttributesW(pwszFilename, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED))
                    rc = RTErrConvertFromWin32(GetLastError());
            }
            /*
             * If RTFILEACTION_OPENED, we may need to truncate the file.
             */
            else if (  (fOpen & (RTFILE_O_TRUNCATE | RTFILE_O_ACTION_MASK)) == (RTFILE_O_TRUNCATE | RTFILE_O_OPEN_CREATE)
                     && enmActionTaken == RTFILEACTION_OPENED)
            {
                if (SetEndOfFile(hFile))
                    enmActionTaken = RTFILEACTION_TRUNCATED;
                else
                    rc = RTErrConvertFromWin32(GetLastError());
            }
            if (penmActionTaken)
                *penmActionTaken = enmActionTaken;
            if (RT_SUCCESS(rc))
            {
                *phFile = (RTFILE)hFile;
                Assert((HANDLE)*phFile == hFile);
                RTPathWinFree(pwszFilename);
                return VINF_SUCCESS;
            }

            CloseHandle(hFile);
        }
        else
        {
            if (   penmActionTaken
                && dwCreationDisposition == CREATE_NEW
                && dwErr == ERROR_FILE_EXISTS)
                *penmActionTaken = RTFILEACTION_ALREADY_EXISTS;
            rc = RTErrConvertFromWin32(dwErr);
        }
        RTPathWinFree(pwszFilename);
    }
    return rc;
}


RTR3DECL(int)  RTFileOpenBitBucket(PRTFILE phFile, uint64_t fAccess)
{
    AssertReturn(   fAccess == RTFILE_O_READ
                 || fAccess == RTFILE_O_WRITE
                 || fAccess == RTFILE_O_READWRITE,
                 VERR_INVALID_PARAMETER);
    return RTFileOpen(phFile, "NUL", fAccess | RTFILE_O_DENY_NONE | RTFILE_O_OPEN);
}


RTDECL(int)  RTFileDup(RTFILE hFileSrc, uint64_t fFlags, PRTFILE phFileNew)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phFileNew, VERR_INVALID_POINTER);
    *phFileNew = NIL_RTFILE;
    AssertPtrReturn(phFileNew, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~(uint64_t)RTFILE_O_INHERIT), VERR_INVALID_FLAGS);

    /*
     * Do the job.
     */
    HANDLE hNew = INVALID_HANDLE_VALUE;
    if (DuplicateHandle(GetCurrentProcess(), (HANDLE)RTFileToNative(hFileSrc),
                        GetCurrentProcess(), &hNew, 0, RT_BOOL(fFlags & RTFILE_O_INHERIT), DUPLICATE_SAME_ACCESS))
    {
        *phFileNew = (RTFILE)hNew;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromWin32(GetLastError());
}


RTR3DECL(int)  RTFileClose(RTFILE hFile)
{
    if (hFile == NIL_RTFILE)
        return VINF_SUCCESS;
    if (CloseHandle((HANDLE)RTFileToNative(hFile)))
        return VINF_SUCCESS;
    return RTErrConvertFromWin32(GetLastError());
}


RTFILE rtFileGetStandard(RTHANDLESTD enmStdHandle)
{
    DWORD dwStdHandle;
    switch (enmStdHandle)
    {
        case RTHANDLESTD_INPUT:     dwStdHandle = STD_INPUT_HANDLE;  break;
        case RTHANDLESTD_OUTPUT:    dwStdHandle = STD_OUTPUT_HANDLE; break;
        case RTHANDLESTD_ERROR:     dwStdHandle = STD_ERROR_HANDLE;  break;
        default:
            AssertFailedReturn(NIL_RTFILE);
    }

    HANDLE hNative = GetStdHandle(dwStdHandle);
    if (hNative == INVALID_HANDLE_VALUE)
        return NIL_RTFILE;

    RTFILE hFile = (RTFILE)(uintptr_t)hNative;
    AssertReturn((HANDLE)(uintptr_t)hFile == hNative, NIL_RTFILE);
    return hFile;
}


RTR3DECL(int)  RTFileSeek(RTFILE hFile, int64_t offSeek, unsigned uMethod, uint64_t *poffActual)
{
    static ULONG aulSeekRecode[] =
    {
        FILE_BEGIN,
        FILE_CURRENT,
        FILE_END,
    };

    /*
     * Validate input.
     */
    if (uMethod > RTFILE_SEEK_END)
    {
        AssertMsgFailed(("Invalid uMethod=%d\n", uMethod));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Execute the seek.
     */
    if (MySetFilePointer(hFile, offSeek, poffActual, aulSeekRecode[uMethod]))
        return VINF_SUCCESS;
    return RTErrConvertFromWin32(GetLastError());
}


RTR3DECL(int)  RTFileRead(RTFILE hFile, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    if (cbToRead <= 0)
    {
        if (pcbRead)
            *pcbRead = 0;
        return VINF_SUCCESS;
    }
    ULONG cbToReadAdj = (ULONG)cbToRead;
    AssertReturn(cbToReadAdj == cbToRead, VERR_NUMBER_TOO_BIG);

    ULONG cbRead = 0;
    if (ReadFile((HANDLE)RTFileToNative(hFile), pvBuf, cbToReadAdj, &cbRead, NULL))
    {
        if (pcbRead)
            /* Caller can handle partial reads. */
            *pcbRead = cbRead;
        else
        {
            /* Caller expects everything to be read. */
            while (cbToReadAdj > cbRead)
            {
                ULONG cbReadPart = 0;
                if (!ReadFile((HANDLE)RTFileToNative(hFile), (char*)pvBuf + cbRead, cbToReadAdj - cbRead, &cbReadPart, NULL))
                    return RTErrConvertFromWin32(GetLastError());
                if (cbReadPart == 0)
                    return VERR_EOF;
                cbRead += cbReadPart;
            }
        }
        return VINF_SUCCESS;
    }

    /*
     * If it's a console, we might bump into out of memory conditions in the
     * ReadConsole call.
     */
    DWORD dwErr = GetLastError();
    if (dwErr == ERROR_NOT_ENOUGH_MEMORY)
    {
        ULONG cbChunk = cbToReadAdj / 2;
        if (cbChunk > 16*_1K)
            cbChunk = 16*_1K;
        else
            cbChunk = RT_ALIGN_32(cbChunk, 256);

        cbRead = 0;
        while (cbToReadAdj > cbRead)
        {
            ULONG cbToReadNow = RT_MIN(cbChunk, cbToReadAdj - cbRead);
            ULONG cbReadPart  = 0;
            if (!ReadFile((HANDLE)RTFileToNative(hFile), (char *)pvBuf + cbRead, cbToReadNow, &cbReadPart, NULL))
            {
                /* If we failed because the buffer is too big, shrink it and
                   try again. */
                dwErr = GetLastError();
                if (   dwErr == ERROR_NOT_ENOUGH_MEMORY
                    && cbChunk > 8)
                {
                    cbChunk /= 2;
                    continue;
                }
                return RTErrConvertFromWin32(dwErr);
            }
            cbRead += cbReadPart;

            /* Return if the caller can handle partial reads, otherwise try
               fill the buffer all the way up. */
            if (pcbRead)
            {
                *pcbRead = cbRead;
                break;
            }
            if (cbReadPart == 0)
                return VERR_EOF;
        }
        return VINF_SUCCESS;
    }

    return RTErrConvertFromWin32(dwErr);
}


RTDECL(int)  RTFileReadAt(RTFILE hFile, RTFOFF off, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    ULONG cbToReadAdj = (ULONG)cbToRead;
    AssertReturn(cbToReadAdj == cbToRead, VERR_NUMBER_TOO_BIG);

    OVERLAPPED Overlapped;
    Overlapped.Offset       = (uint32_t)off;
    Overlapped.OffsetHigh   = (uint32_t)(off >> 32);
    Overlapped.hEvent       = NULL;
    Overlapped.Internal     = 0;
    Overlapped.InternalHigh = 0;

    ULONG cbRead = 0;
    if (ReadFile((HANDLE)RTFileToNative(hFile), pvBuf, cbToReadAdj, &cbRead, &Overlapped))
    {
        if (pcbRead)
            /* Caller can handle partial reads. */
            *pcbRead = cbRead;
        else
        {
            /* Caller expects everything to be read. */
            while (cbToReadAdj > cbRead)
            {
                Overlapped.Offset       = (uint32_t)(off + cbRead);
                Overlapped.OffsetHigh   = (uint32_t)((off + cbRead) >> 32);
                Overlapped.hEvent       = NULL;
                Overlapped.Internal     = 0;
                Overlapped.InternalHigh = 0;

                ULONG cbReadPart = 0;
                if (!ReadFile((HANDLE)RTFileToNative(hFile), (char *)pvBuf + cbRead, cbToReadAdj - cbRead,
                              &cbReadPart, &Overlapped))
                    return RTErrConvertFromWin32(GetLastError());
                if (cbReadPart == 0)
                    return VERR_EOF;
                cbRead += cbReadPart;
            }
        }
        return VINF_SUCCESS;
    }

    /* We will get an EOF error when using overlapped I/O.  So, make sure we don't
       return it when pcbhRead is not NULL. */
    DWORD dwErr = GetLastError();
    if (pcbRead && dwErr == ERROR_HANDLE_EOF)
    {
        *pcbRead = 0;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromWin32(dwErr);
}


RTR3DECL(int)  RTFileWrite(RTFILE hFile, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    if (cbToWrite <= 0)
        return VINF_SUCCESS;
    ULONG const cbToWriteAdj = (ULONG)cbToWrite;
    AssertReturn(cbToWriteAdj == cbToWrite, VERR_NUMBER_TOO_BIG);

    ULONG cbWritten = 0;
    if (WriteFile((HANDLE)RTFileToNative(hFile), pvBuf, cbToWriteAdj, &cbWritten, NULL))
    {
        if (pcbWritten)
            /* Caller can handle partial writes. */
            *pcbWritten = RT_MIN(cbWritten, cbToWriteAdj); /* paranoia^3 */
        else
        {
            /* Caller expects everything to be written. */
            while (cbWritten < cbToWriteAdj)
            {
                ULONG cbWrittenPart = 0;
                if (!WriteFile((HANDLE)RTFileToNative(hFile), (char*)pvBuf + cbWritten,
                               cbToWriteAdj - cbWritten, &cbWrittenPart, NULL))
                {
                    int rc = RTErrConvertFromWin32(GetLastError());
                    if (rc == VERR_DISK_FULL)
                        rc = rtFileWinCheckIfDiskReallyFull(hFile, RTFileTell(hFile) + cbToWriteAdj - cbWritten);
                    return rc;
                }
                if (cbWrittenPart == 0)
                    return VERR_WRITE_ERROR;
                cbWritten += cbWrittenPart;
            }
        }
        return VINF_SUCCESS;
    }

    /*
     * If it's a console, we might bump into out of memory conditions in the
     * WriteConsole call.
     */
    DWORD dwErr = GetLastError();
    if (dwErr == ERROR_NOT_ENOUGH_MEMORY)
    {
        ULONG cbChunk = cbToWriteAdj / 2;
        if (cbChunk > _32K)
            cbChunk = _32K;
        else
            cbChunk = RT_ALIGN_32(cbChunk, 256);

        cbWritten = 0;
        while (cbWritten < cbToWriteAdj)
        {
            ULONG cbToWriteNow  = RT_MIN(cbChunk, cbToWriteAdj - cbWritten);
            ULONG cbWrittenPart = 0;
            if (!WriteFile((HANDLE)RTFileToNative(hFile), (const char *)pvBuf + cbWritten, cbToWriteNow, &cbWrittenPart, NULL))
            {
                /* If we failed because the buffer is too big, shrink it and
                   try again. */
                dwErr = GetLastError();
                if (   dwErr == ERROR_NOT_ENOUGH_MEMORY
                    && cbChunk > 8)
                {
                    cbChunk /= 2;
                    continue;
                }
                int rc = RTErrConvertFromWin32(dwErr);
                if (rc == VERR_DISK_FULL)
                    rc = rtFileWinCheckIfDiskReallyFull(hFile, RTFileTell(hFile) + cbToWriteNow);
                return rc;
            }
            cbWritten += cbWrittenPart;

            /* Return if the caller can handle partial writes, otherwise try
               write out everything. */
            if (pcbWritten)
            {
                *pcbWritten = RT_MIN(cbWritten, cbToWriteAdj); /* paranoia^3 */
                break;
            }
            if (cbWrittenPart == 0)
                return VERR_WRITE_ERROR;
        }
        return VINF_SUCCESS;
    }

    int rc = RTErrConvertFromWin32(dwErr);
    if (rc == VERR_DISK_FULL)
        rc = rtFileWinCheckIfDiskReallyFull(hFile, RTFileTell(hFile) + cbToWriteAdj);
    return rc;
}


RTDECL(int)  RTFileWriteAt(RTFILE hFile, RTFOFF off, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    ULONG const cbToWriteAdj = (ULONG)cbToWrite;
    AssertReturn(cbToWriteAdj == cbToWrite, VERR_NUMBER_TOO_BIG);

    OVERLAPPED Overlapped;
    Overlapped.Offset       = (uint32_t)off;
    Overlapped.OffsetHigh   = (uint32_t)(off >> 32);
    Overlapped.hEvent       = NULL;
    Overlapped.Internal     = 0;
    Overlapped.InternalHigh = 0;

    ULONG cbWritten = 0;
    if (WriteFile((HANDLE)RTFileToNative(hFile), pvBuf, cbToWriteAdj, &cbWritten, &Overlapped))
    {
        if (pcbWritten)
            /* Caller can handle partial writes. */
            *pcbWritten = RT_MIN(cbWritten, cbToWriteAdj); /* paranoia^3 */
        else
        {
            /* Caller expects everything to be written. */
            while (cbWritten < cbToWriteAdj)
            {
                Overlapped.Offset       = (uint32_t)(off + cbWritten);
                Overlapped.OffsetHigh   = (uint32_t)((off + cbWritten) >> 32);
                Overlapped.hEvent       = NULL;
                Overlapped.Internal     = 0;
                Overlapped.InternalHigh = 0;

                ULONG cbWrittenPart = 0;
                if (!WriteFile((HANDLE)RTFileToNative(hFile), (char*)pvBuf + cbWritten,
                               cbToWriteAdj - cbWritten, &cbWrittenPart, &Overlapped))
                {
                    int rc = RTErrConvertFromWin32(GetLastError());
                    if (rc == VERR_DISK_FULL)
                        rc = rtFileWinCheckIfDiskReallyFull(hFile, off + cbToWriteAdj);
                    return rc;
                }
                if (cbWrittenPart == 0)
                    return VERR_WRITE_ERROR;
                cbWritten += cbWrittenPart;
            }
        }
        return VINF_SUCCESS;
    }

    int rc = RTErrConvertFromWin32(GetLastError());
    if (rc == VERR_DISK_FULL)
        rc = rtFileWinCheckIfDiskReallyFull(hFile, off + cbToWriteAdj);
    return rc;
}


RTR3DECL(int)  RTFileFlush(RTFILE hFile)
{
    if (!FlushFileBuffers((HANDLE)RTFileToNative(hFile)))
    {
        int rc = GetLastError();
        Log(("FlushFileBuffers failed with %d\n", rc));
        return RTErrConvertFromWin32(rc);
    }
    return VINF_SUCCESS;
}

#if 1

/**
 * Checks the the two handles refers to the same file.
 *
 * @returns true if the same file, false if different ones or invalid handles.
 * @param   hFile1              Handle \#1.
 * @param   hFile2              Handle \#2.
 */
static bool rtFileIsSame(HANDLE hFile1, HANDLE hFile2)
{
    /*
     * We retry in case CreationTime or the Object ID is being modified and there
     * aren't any IndexNumber (file ID) on this kind of file system.
     */
    for (uint32_t iTries = 0; iTries < 3; iTries++)
    {
        /*
         * Fetch data to compare (being a little lazy here).
         */
        struct
        {
            HANDLE                      hFile;
            NTSTATUS                    rcObjId;
            FILE_OBJECTID_INFORMATION   ObjId;
            FILE_ALL_INFORMATION        All;
            FILE_FS_VOLUME_INFORMATION  Vol;
        } auData[2];
        auData[0].hFile = hFile1;
        auData[1].hFile = hFile2;

        for (uintptr_t i = 0; i < RT_ELEMENTS(auData); i++)
        {
            RT_ZERO(auData[i].ObjId);
            IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
            auData[i].rcObjId = NtQueryInformationFile(auData[i].hFile, &Ios, &auData[i].ObjId, sizeof(auData[i].ObjId),
                                                       FileObjectIdInformation);

            RT_ZERO(auData[i].All);
            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
            NTSTATUS rcNt = NtQueryInformationFile(auData[i].hFile, &Ios, &auData[i].All, sizeof(auData[i].All),
                                                   FileAllInformation);
            AssertReturn(rcNt == STATUS_BUFFER_OVERFLOW /* insufficient space for name info */ || NT_SUCCESS(rcNt), false);

            union
            {
                 FILE_FS_VOLUME_INFORMATION Info;
                 uint8_t                    abBuf[sizeof(FILE_FS_VOLUME_INFORMATION) + 4096];
            } uVol;
            RT_ZERO(uVol.Info);
            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
            rcNt = NtQueryVolumeInformationFile(auData[i].hFile, &Ios, &uVol, sizeof(uVol), FileFsVolumeInformation);
            if (NT_SUCCESS(rcNt))
                auData[i].Vol = uVol.Info;
            else
                RT_ZERO(auData[i].Vol);
        }

        /*
         * Compare it.
         */
        if (   auData[0].All.StandardInformation.Directory
            == auData[1].All.StandardInformation.Directory)
        { /* likely */ }
        else
            break;

        if (   (auData[0].All.BasicInformation.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT))
            == (auData[1].All.BasicInformation.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT)))
        { /* likely */ }
        else
            break;

        if (   auData[0].Vol.VolumeSerialNumber
            == auData[1].Vol.VolumeSerialNumber)
        { /* likely */ }
        else
            break;

        if (   auData[0].All.InternalInformation.IndexNumber.QuadPart
            == auData[1].All.InternalInformation.IndexNumber.QuadPart)
        { /* likely */ }
        else
            break;

        if (   !NT_SUCCESS(auData[0].rcObjId)
            || memcmp(&auData[0].ObjId, &auData[1].ObjId, RT_UOFFSETOF(FILE_OBJECTID_INFORMATION, ExtendedInfo)) == 0)
        {
            if (   auData[0].All.BasicInformation.CreationTime.QuadPart
                == auData[1].All.BasicInformation.CreationTime.QuadPart)
                return true;
        }
    }

    return false;
}


/**
 * If @a hFile is opened in append mode, try return a handle with
 * FILE_WRITE_DATA permissions.
 *
 * @returns Duplicate handle.
 * @param   hFile               The NT handle to check & duplicate.
 *
 * @todo    It would be much easier to implement this by not dropping the
 *          FILE_WRITE_DATA access and instead have the RTFileWrite APIs
 *          enforce the appending.  That will require keeping additional
 *          information along side the handle (instance structure).  However, on
 *          windows you can grant append permissions w/o giving people access to
 *          overwrite existing data, so the RTFileOpenEx code would have to deal
 *          with those kinds of STATUS_ACCESS_DENIED too then.
 */
static HANDLE rtFileReOpenAppendOnlyWithFullWriteAccess(HANDLE hFile)
{
    OBJECT_BASIC_INFORMATION BasicInfo = {0};
    ULONG                    cbActual  = 0;
    NTSTATUS rcNt = NtQueryObject(hFile, ObjectBasicInformation, &BasicInfo, sizeof(BasicInfo), &cbActual);
    if (NT_SUCCESS(rcNt))
    {
        if ((BasicInfo.GrantedAccess & (FILE_APPEND_DATA | FILE_WRITE_DATA)) == FILE_APPEND_DATA)
        {
            /*
             * We cannot use NtDuplicateObject here as it is not possible to
             * upgrade the access on files, only making it more strict.  So,
             * query the path and re-open it (we could do by file/object/whatever
             * id too, but that may not work with all file systems).
             */
            for (uint32_t i = 0; i < 16; i++)
            {
                UNICODE_STRING  NtName;
                int rc = RTNtPathFromHandle(&NtName, hFile, 0);
                AssertRCReturn(rc, INVALID_HANDLE_VALUE);

                HANDLE              hDupFile = RTNT_INVALID_HANDLE_VALUE;
                IO_STATUS_BLOCK     Ios      = RTNT_IO_STATUS_BLOCK_INITIALIZER;
                OBJECT_ATTRIBUTES   ObjAttr;
                InitializeObjectAttributes(&ObjAttr, &NtName, BasicInfo.Attributes & ~OBJ_INHERIT, NULL, NULL);

                rcNt = NtCreateFile(&hDupFile,
                                    BasicInfo.GrantedAccess | FILE_WRITE_DATA,
                                    &ObjAttr,
                                    &Ios,
                                    NULL /* AllocationSize*/,
                                    FILE_ATTRIBUTE_NORMAL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    FILE_OPEN,
                                    FILE_OPEN_FOR_BACKUP_INTENT /*??*/,
                                    NULL /*EaBuffer*/,
                                    0 /*EaLength*/);
                RTUtf16Free(NtName.Buffer);
                if (NT_SUCCESS(rcNt))
                {
                    /*
                     * Check that we've opened the same file.
                     */
                    if (rtFileIsSame(hFile, hDupFile))
                        return hDupFile;
                    NtClose(hDupFile);
                }
            }
            AssertFailed();
        }
    }
    return INVALID_HANDLE_VALUE;
}

#endif

RTR3DECL(int) RTFileSetSize(RTFILE hFile, uint64_t cbSize)
{
#if 1
    HANDLE hNtFile  = (HANDLE)RTFileToNative(hFile);
    HANDLE hDupFile = INVALID_HANDLE_VALUE;
    union
    {
        FILE_END_OF_FILE_INFORMATION Eof;
        FILE_ALLOCATION_INFORMATION  Alloc;
    } uInfo;

    /*
     * Change the EOF marker.
     *
     * HACK ALERT! If the file was opened in RTFILE_O_APPEND mode, we will have
     *             to re-open it with FILE_WRITE_DATA access to get the job done.
     *             This how ftruncate on a unixy system would work but not how
     *             it is done on Windows where appending is a separate permission
     *             rather than just a write modifier, making this hack totally wrong.
     */
    /** @todo The right way to fix this is either to add a RTFileSetSizeEx function
     *        for specifically requesting the unixy behaviour, or add an additional
     *        flag to RTFileOpen[Ex] to request the unixy append behaviour there.
     *        The latter would require saving the open flags in a instance data
     *        structure, which is a bit of a risky move, though something we should
     *        do in 6.2 (or later).
     *
     *        Note! Because handle interitance, it is not realyan option to
     *              always use FILE_WRITE_DATA and implement the RTFILE_O_APPEND
     *              bits in RTFileWrite and friends.  Besides, it's not like
     *              RTFILE_O_APPEND is so clearly defined anyway - see
     *              RTFileWriteAt.
     */
    IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    uInfo.Eof.EndOfFile.QuadPart = cbSize;
    NTSTATUS rcNt = NtSetInformationFile(hNtFile, &Ios, &uInfo.Eof, sizeof(uInfo.Eof), FileEndOfFileInformation);
    if (rcNt == STATUS_ACCESS_DENIED)
    {
        hDupFile = rtFileReOpenAppendOnlyWithFullWriteAccess(hNtFile);
        if (hDupFile != INVALID_HANDLE_VALUE)
        {
            hNtFile = hDupFile;
            uInfo.Eof.EndOfFile.QuadPart = cbSize;
            rcNt = NtSetInformationFile(hNtFile, &Ios, &uInfo.Eof, sizeof(uInfo.Eof), FileEndOfFileInformation);
        }
    }

    if (NT_SUCCESS(rcNt))
    {
        /*
         * Change the allocation.
         */
        uInfo.Alloc.AllocationSize.QuadPart = cbSize;
        rcNt = NtSetInformationFile(hNtFile, &Ios, &uInfo.Eof, sizeof(uInfo.Alloc), FileAllocationInformation);
    }

    /*
     * Close the temporary file handle:
     */
    if (hDupFile != INVALID_HANDLE_VALUE)
        NtClose(hDupFile);

    if (NT_SUCCESS(rcNt))
        return VINF_SUCCESS;
    return RTErrConvertFromNtStatus(rcNt);

#else /* this version of the code will fail to truncate files when RTFILE_O_APPEND is in effect, which isn't what we want... */
    /*
     * Get current file pointer.
     */
    int         rc;
    uint64_t    offCurrent;
    if (MySetFilePointer(hFile, 0, &offCurrent, FILE_CURRENT))
    {
        /*
         * Set new file pointer.
         */
        if (MySetFilePointer(hFile, cbSize, NULL, FILE_BEGIN))
        {
            /* set file pointer */
            if (SetEndOfFile((HANDLE)RTFileToNative(hFile)))
            {
                /*
                 * Restore file pointer and return.
                 * If the old pointer was beyond the new file end, ignore failure.
                 */
                if (    MySetFilePointer(hFile, offCurrent, NULL, FILE_BEGIN)
                    ||  offCurrent > cbSize)
                    return VINF_SUCCESS;
            }

            /*
             * Failed, try restoring the file pointer.
             */
            rc = GetLastError();
            MySetFilePointer(hFile, offCurrent, NULL, FILE_BEGIN);

            if (rc == ERROR_DISK_FULL)
                return rtFileWinCheckIfDiskReallyFull(hFile, cbSize);
        }
        else
            rc = GetLastError();
    }
    else
        rc = GetLastError();

    return RTErrConvertFromWin32(rc);
#endif
}


RTR3DECL(int)  RTFileQuerySize(RTFILE hFile, uint64_t *pcbSize)
{
    /*
     * GetFileSize works for most handles.
     */
    ULARGE_INTEGER  Size;
    Size.LowPart = GetFileSize((HANDLE)RTFileToNative(hFile), &Size.HighPart);
    if (Size.LowPart != INVALID_FILE_SIZE)
    {
        *pcbSize = Size.QuadPart;
        return VINF_SUCCESS;
    }
    int rc = RTErrConvertFromWin32(GetLastError());

    /*
     * Could it be a volume or a disk?
     */
    DISK_GEOMETRY   DriveGeo;
    DWORD           cbDriveGeo;
    if (DeviceIoControl((HANDLE)RTFileToNative(hFile),
                        IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
                        &DriveGeo, sizeof(DriveGeo), &cbDriveGeo, NULL))
    {
        if (   DriveGeo.MediaType == FixedMedia
            || DriveGeo.MediaType == RemovableMedia)
        {
            *pcbSize = DriveGeo.Cylinders.QuadPart
                     * DriveGeo.TracksPerCylinder
                     * DriveGeo.SectorsPerTrack
                     * DriveGeo.BytesPerSector;

            GET_LENGTH_INFORMATION  DiskLenInfo;
            DWORD                   Ignored;
            if (DeviceIoControl((HANDLE)RTFileToNative(hFile),
                                IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                                &DiskLenInfo, sizeof(DiskLenInfo), &Ignored, (LPOVERLAPPED)NULL))
            {
                /* IOCTL_DISK_GET_LENGTH_INFO is supported -- override cbSize. */
                *pcbSize = DiskLenInfo.Length.QuadPart;
            }
            return VINF_SUCCESS;
        }
    }

    /*
     * Return the GetFileSize result if not a volume/disk.
     */
    return rc;
}


RTR3DECL(int) RTFileQueryMaxSizeEx(RTFILE hFile, PRTFOFF pcbMax)
{
    /** @todo r=bird:
     * We might have to make this code OS version specific... In the worse
     * case, we'll have to try GetVolumeInformationByHandle on vista and fall
     * back on NtQueryVolumeInformationFile(,,,, FileFsAttributeInformation)
     * else where, and check for known file system names. (For LAN shares we'll
     * have to figure out the remote file system.) */
    RT_NOREF_PV(hFile); RT_NOREF_PV(pcbMax);
    return VERR_NOT_IMPLEMENTED;
}


RTR3DECL(bool) RTFileIsValid(RTFILE hFile)
{
    if (hFile != NIL_RTFILE)
    {
        DWORD dwType = GetFileType((HANDLE)RTFileToNative(hFile));
        switch (dwType)
        {
            case FILE_TYPE_CHAR:
            case FILE_TYPE_DISK:
            case FILE_TYPE_PIPE:
            case FILE_TYPE_REMOTE:
                return true;

            case FILE_TYPE_UNKNOWN:
                if (GetLastError() == NO_ERROR)
                    return true;
                break;

            default:
                break;
        }
    }
    return false;
}


#define LOW_DWORD(u64) ((DWORD)u64)
#define HIGH_DWORD(u64) (((DWORD *)&u64)[1])

RTR3DECL(int)  RTFileLock(RTFILE hFile, unsigned fLock, int64_t offLock, uint64_t cbLock)
{
    Assert(offLock >= 0);

    /* Check arguments. */
    if (fLock & ~RTFILE_LOCK_MASK)
    {
        AssertMsgFailed(("Invalid fLock=%08X\n", fLock));
        return VERR_INVALID_PARAMETER;
    }

    /* Prepare flags. */
    Assert(RTFILE_LOCK_WRITE);
    DWORD dwFlags = (fLock & RTFILE_LOCK_WRITE) ? LOCKFILE_EXCLUSIVE_LOCK : 0;
    Assert(RTFILE_LOCK_WAIT);
    if (!(fLock & RTFILE_LOCK_WAIT))
        dwFlags |= LOCKFILE_FAIL_IMMEDIATELY;

    /* Windows structure. */
    OVERLAPPED Overlapped;
    memset(&Overlapped, 0, sizeof(Overlapped));
    Overlapped.Offset = LOW_DWORD(offLock);
    Overlapped.OffsetHigh = HIGH_DWORD(offLock);

    /* Note: according to Microsoft, LockFileEx API call is available starting from NT 3.5 */
    if (LockFileEx((HANDLE)RTFileToNative(hFile), dwFlags, 0, LOW_DWORD(cbLock), HIGH_DWORD(cbLock), &Overlapped))
        return VINF_SUCCESS;

    return RTErrConvertFromWin32(GetLastError());
}


RTR3DECL(int)  RTFileChangeLock(RTFILE hFile, unsigned fLock, int64_t offLock, uint64_t cbLock)
{
    Assert(offLock >= 0);

    /* Check arguments. */
    if (fLock & ~RTFILE_LOCK_MASK)
    {
        AssertMsgFailed(("Invalid fLock=%08X\n", fLock));
        return VERR_INVALID_PARAMETER;
    }

    /* Remove old lock. */
    int rc = RTFileUnlock(hFile, offLock, cbLock);
    if (RT_FAILURE(rc))
        return rc;

    /* Set new lock. */
    rc = RTFileLock(hFile, fLock, offLock, cbLock);
    if (RT_SUCCESS(rc))
        return rc;

    /* Try to restore old lock. */
    unsigned fLockOld = (fLock & RTFILE_LOCK_WRITE) ? fLock & ~RTFILE_LOCK_WRITE : fLock | RTFILE_LOCK_WRITE;
    rc = RTFileLock(hFile, fLockOld, offLock, cbLock);
    if (RT_SUCCESS(rc))
        return VERR_FILE_LOCK_VIOLATION;
    else
        return VERR_FILE_LOCK_LOST;
}


RTR3DECL(int)  RTFileUnlock(RTFILE hFile, int64_t offLock, uint64_t cbLock)
{
    Assert(offLock >= 0);

    if (UnlockFile((HANDLE)RTFileToNative(hFile),
                   LOW_DWORD(offLock), HIGH_DWORD(offLock),
                   LOW_DWORD(cbLock), HIGH_DWORD(cbLock)))
        return VINF_SUCCESS;

    return RTErrConvertFromWin32(GetLastError());
}



RTR3DECL(int) RTFileQueryInfo(RTFILE hFile, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs)
{
    /*
     * Validate input.
     */
    if (hFile == NIL_RTFILE)
    {
        AssertMsgFailed(("Invalid hFile=%RTfile\n", hFile));
        return VERR_INVALID_PARAMETER;
    }
    if (!pObjInfo)
    {
        AssertMsgFailed(("Invalid pObjInfo=%p\n", pObjInfo));
        return VERR_INVALID_PARAMETER;
    }
    if (    enmAdditionalAttribs < RTFSOBJATTRADD_NOTHING
        ||  enmAdditionalAttribs > RTFSOBJATTRADD_LAST)
    {
        AssertMsgFailed(("Invalid enmAdditionalAttribs=%p\n", enmAdditionalAttribs));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Query file info.
     */
    HANDLE hHandle = (HANDLE)RTFileToNative(hFile);
#if 1
    uint64_t auBuf[168 / sizeof(uint64_t)]; /* Missing FILE_ALL_INFORMATION here. */
    int rc = rtPathNtQueryInfoFromHandle(hFile, auBuf, sizeof(auBuf), pObjInfo, enmAdditionalAttribs, NULL, 0);
    if (RT_SUCCESS(rc))
        return rc;

    /*
     * Console I/O handles make trouble here.  On older windows versions they
     * end up with ERROR_INVALID_HANDLE when handed to the above API, while on
     * more recent ones they cause different errors to appear.
     *
     * Thus, we must ignore the latter and doubly verify invalid handle claims.
     * We use the undocumented VerifyConsoleIoHandle to do this, falling back on
     * GetFileType should it not be there.
     */
    if (   rc == VERR_INVALID_HANDLE
        || rc == VERR_ACCESS_DENIED
        || rc == VERR_UNEXPECTED_FS_OBJ_TYPE)
    {
        static PFNVERIFYCONSOLEIOHANDLE s_pfnVerifyConsoleIoHandle = NULL;
        static bool volatile            s_fInitialized = false;
        PFNVERIFYCONSOLEIOHANDLE        pfnVerifyConsoleIoHandle;
        if (s_fInitialized)
            pfnVerifyConsoleIoHandle = s_pfnVerifyConsoleIoHandle;
        else
        {
            pfnVerifyConsoleIoHandle = (PFNVERIFYCONSOLEIOHANDLE)RTLdrGetSystemSymbol("kernel32.dll", "VerifyConsoleIoHandle");
            ASMAtomicWriteBool(&s_fInitialized, true);
        }
        if (   pfnVerifyConsoleIoHandle
            ? !pfnVerifyConsoleIoHandle(hHandle)
            : GetFileType(hHandle) == FILE_TYPE_UNKNOWN && GetLastError() != NO_ERROR)
            return VERR_INVALID_HANDLE;
    }
    /*
     * On Windows 10 and (hopefully) 8.1 we get ERROR_INVALID_FUNCTION with console
     * I/O handles and null device handles.  We must ignore these just like the
     * above invalid handle error.
     */
    else if (rc != VERR_INVALID_FUNCTION && rc != VERR_IO_BAD_COMMAND)
        return rc;

    RT_ZERO(*pObjInfo);
    pObjInfo->Attr.enmAdditional = enmAdditionalAttribs;
    pObjInfo->Attr.fMode = rtFsModeFromDos(RTFS_DOS_NT_DEVICE, "", 0, 0, 0);
    return VINF_SUCCESS;
#else

    BY_HANDLE_FILE_INFORMATION Data;
    if (!GetFileInformationByHandle(hHandle, &Data))
    {
        /*
         * Console I/O handles make trouble here.  On older windows versions they
         * end up with ERROR_INVALID_HANDLE when handed to the above API, while on
         * more recent ones they cause different errors to appear.
         *
         * Thus, we must ignore the latter and doubly verify invalid handle claims.
         * We use the undocumented VerifyConsoleIoHandle to do this, falling back on
         * GetFileType should it not be there.
         */
        DWORD dwErr = GetLastError();
        if (dwErr == ERROR_INVALID_HANDLE)
        {
            static PFNVERIFYCONSOLEIOHANDLE s_pfnVerifyConsoleIoHandle = NULL;
            static bool volatile            s_fInitialized = false;
            PFNVERIFYCONSOLEIOHANDLE        pfnVerifyConsoleIoHandle;
            if (s_fInitialized)
                pfnVerifyConsoleIoHandle = s_pfnVerifyConsoleIoHandle;
            else
            {
                pfnVerifyConsoleIoHandle = (PFNVERIFYCONSOLEIOHANDLE)RTLdrGetSystemSymbol("kernel32.dll", "VerifyConsoleIoHandle");
                ASMAtomicWriteBool(&s_fInitialized, true);
            }
            if (   pfnVerifyConsoleIoHandle
                ? !pfnVerifyConsoleIoHandle(hHandle)
                : GetFileType(hHandle) == FILE_TYPE_UNKNOWN && GetLastError() != NO_ERROR)
                return VERR_INVALID_HANDLE;
        }
        /*
         * On Windows 10 and (hopefully) 8.1 we get ERROR_INVALID_FUNCTION with console I/O
         * handles.  We must ignore these just like the above invalid handle error.
         */
        else if (dwErr != ERROR_INVALID_FUNCTION)
            return RTErrConvertFromWin32(dwErr);

        RT_ZERO(Data);
        Data.dwFileAttributes = RTFS_DOS_NT_DEVICE;
    }

    /*
     * Setup the returned data.
     */
    pObjInfo->cbObject    = ((uint64_t)Data.nFileSizeHigh << 32)
                          |  (uint64_t)Data.nFileSizeLow;
    pObjInfo->cbAllocated = pObjInfo->cbObject;

    Assert(sizeof(uint64_t) == sizeof(Data.ftCreationTime));
    RTTimeSpecSetNtTime(&pObjInfo->BirthTime,         *(uint64_t *)&Data.ftCreationTime);
    RTTimeSpecSetNtTime(&pObjInfo->AccessTime,        *(uint64_t *)&Data.ftLastAccessTime);
    RTTimeSpecSetNtTime(&pObjInfo->ModificationTime,  *(uint64_t *)&Data.ftLastWriteTime);
    pObjInfo->ChangeTime  = pObjInfo->ModificationTime;

    pObjInfo->Attr.fMode  = rtFsModeFromDos((Data.dwFileAttributes << RTFS_DOS_SHIFT) & RTFS_DOS_MASK_NT, "", 0,
                                            RTFSMODE_SYMLINK_REPARSE_TAG /* (symlink or not, doesn't usually matter here) */);

    /*
     * Requested attributes (we cannot provide anything actually).
     */
    switch (enmAdditionalAttribs)
    {
        case RTFSOBJATTRADD_NOTHING:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_NOTHING;
            break;

        case RTFSOBJATTRADD_UNIX:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_UNIX;
            pObjInfo->Attr.u.Unix.uid             = ~0U;
            pObjInfo->Attr.u.Unix.gid             = ~0U;
            pObjInfo->Attr.u.Unix.cHardlinks      = Data.nNumberOfLinks ? Data.nNumberOfLinks : 1;
            pObjInfo->Attr.u.Unix.INodeIdDevice   = Data.dwVolumeSerialNumber;
            pObjInfo->Attr.u.Unix.INodeId         = RT_MAKE_U64(Data.nFileIndexLow, Data.nFileIndexHigh);
            pObjInfo->Attr.u.Unix.fFlags          = 0;
            pObjInfo->Attr.u.Unix.GenerationId    = 0;
            pObjInfo->Attr.u.Unix.Device          = 0;
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_UNIX_OWNER;
            pObjInfo->Attr.u.UnixOwner.uid        = ~0U;
            pObjInfo->Attr.u.UnixOwner.szName[0]  = '\0'; /** @todo return something sensible here. */
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_UNIX_GROUP;
            pObjInfo->Attr.u.UnixGroup.gid        = ~0U;
            pObjInfo->Attr.u.UnixGroup.szName[0]  = '\0';
            break;

        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_EASIZE;
            pObjInfo->Attr.u.EASize.cb            = 0;
            break;

        default:
            AssertMsgFailed(("Impossible!\n"));
            return VERR_INTERNAL_ERROR;
    }

    return VINF_SUCCESS;
#endif
}


RTR3DECL(int) RTFileSetTimes(RTFILE hFile, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                             PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    RT_NOREF_PV(pChangeTime); /* Not exposed thru the windows API we're using. */

    if (!pAccessTime && !pModificationTime && !pBirthTime)
        return VINF_SUCCESS;    /* NOP */

    FILETIME    CreationTimeFT;
    PFILETIME   pCreationTimeFT = NULL;
    if (pBirthTime)
        pCreationTimeFT = RTTimeSpecGetNtFileTime(pBirthTime, &CreationTimeFT);

    FILETIME    LastAccessTimeFT;
    PFILETIME   pLastAccessTimeFT = NULL;
    if (pAccessTime)
        pLastAccessTimeFT = RTTimeSpecGetNtFileTime(pAccessTime, &LastAccessTimeFT);

    FILETIME    LastWriteTimeFT;
    PFILETIME   pLastWriteTimeFT = NULL;
    if (pModificationTime)
        pLastWriteTimeFT = RTTimeSpecGetNtFileTime(pModificationTime, &LastWriteTimeFT);

    int rc = VINF_SUCCESS;
    if (!SetFileTime((HANDLE)RTFileToNative(hFile), pCreationTimeFT, pLastAccessTimeFT, pLastWriteTimeFT))
    {
        DWORD Err = GetLastError();
        rc = RTErrConvertFromWin32(Err);
        Log(("RTFileSetTimes(%RTfile, %p, %p, %p, %p): SetFileTime failed with lasterr %d (%Rrc)\n",
             hFile, pAccessTime, pModificationTime, pChangeTime, pBirthTime, Err, rc));
    }
    return rc;
}


#if 0 /* RTFileSetMode is implemented by RTFileSetMode-r3-nt.cpp */
/* This comes from a source file with a different set of system headers (DDK)
 * so it can't be declared in a common header, like internal/file.h.
 */
extern int rtFileNativeSetAttributes(HANDLE FileHandle, ULONG FileAttributes);


RTR3DECL(int) RTFileSetMode(RTFILE hFile, RTFMODE fMode)
{
    /*
     * Normalize the mode and call the API.
     */
    fMode = rtFsModeNormalize(fMode, NULL, 0);
    if (!rtFsModeIsValid(fMode))
        return VERR_INVALID_PARAMETER;

    ULONG FileAttributes = (fMode & RTFS_DOS_MASK) >> RTFS_DOS_SHIFT;
    int Err = rtFileNativeSetAttributes((HANDLE)hFile, FileAttributes);
    if (Err != ERROR_SUCCESS)
    {
        int rc = RTErrConvertFromWin32(Err);
        Log(("RTFileSetMode(%RTfile, %RTfmode): rtFileNativeSetAttributes (0x%08X) failed with err %d (%Rrc)\n",
             hFile, fMode, FileAttributes, Err, rc));
        return rc;
    }
    return VINF_SUCCESS;
}
#endif


/* RTFileQueryFsSizes is implemented by ../nt/RTFileQueryFsSizes-nt.cpp */

#if 0 /* RTFileDelete is implemented by ../nt/RTFileDelete-r3-nt.cpp */
RTR3DECL(int)  RTFileDelete(const char *pszFilename)
{
    PRTUTF16 pwszFilename;
    int rc = RTPathWinFromUtf8(&pwszFilename, pszFilename, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        if (!DeleteFileW(pwszFilename))
            rc = RTErrConvertFromWin32(GetLastError());
        RTPathWinFree(pwszFilename);
    }

    return rc;
}
#endif


RTDECL(int) RTFileRename(const char *pszSrc, const char *pszDst, unsigned fRename)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDst, VERR_INVALID_POINTER);
    AssertMsgReturn(!(fRename & ~RTPATHRENAME_FLAGS_REPLACE), ("%#x\n", fRename), VERR_INVALID_PARAMETER);

    /*
     * Hand it on to the worker.
     */
    int rc = rtPathWin32MoveRename(pszSrc, pszDst,
                                   fRename & RTPATHRENAME_FLAGS_REPLACE ? MOVEFILE_REPLACE_EXISTING : 0,
                                   RTFS_TYPE_FILE);

    LogFlow(("RTFileMove(%p:{%s}, %p:{%s}, %#x): returns %Rrc\n",
             pszSrc, pszSrc, pszDst, pszDst, fRename, rc));
    return rc;

}


RTDECL(int) RTFileMove(const char *pszSrc, const char *pszDst, unsigned fMove)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDst, VERR_INVALID_POINTER);
    AssertMsgReturn(!(fMove & ~RTFILEMOVE_FLAGS_REPLACE), ("%#x\n", fMove), VERR_INVALID_PARAMETER);

    /*
     * Hand it on to the worker.
     */
    int rc = rtPathWin32MoveRename(pszSrc, pszDst,
                                   fMove & RTFILEMOVE_FLAGS_REPLACE
                                   ? MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING
                                   : MOVEFILE_COPY_ALLOWED,
                                   RTFS_TYPE_FILE);

    LogFlow(("RTFileMove(%p:{%s}, %p:{%s}, %#x): returns %Rrc\n",
             pszSrc, pszSrc, pszDst, pszDst, fMove, rc));
    return rc;
}

