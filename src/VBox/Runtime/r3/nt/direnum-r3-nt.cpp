/* $Id: direnum-r3-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Directory Enumeration, Native NT.
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
#include "internal-r3-nt.h"

#include <iprt/dir.h>
#include <iprt/path.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/utf16.h>
#include "internal/fs.h"
#include "internal/dir.h"
#include "internal/path.h"
#include "../win/internal-r3-win.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Whether to return a single record (TRUE) or multiple (FALSE). */
#define RTDIR_NT_SINGLE_RECORD  FALSE

/** Go hard on record chaining (has slight performance impact). */
#ifdef RT_STRICT
# define RTDIR_NT_STRICT
#endif


/* ASSUMES FileID comes after ShortName and the structs are identical up to that point. */
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, NextEntryOffset, FILE_ID_BOTH_DIR_INFORMATION, NextEntryOffset);
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, FileIndex      , FILE_ID_BOTH_DIR_INFORMATION, FileIndex      );
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, CreationTime   , FILE_ID_BOTH_DIR_INFORMATION, CreationTime   );
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, LastAccessTime , FILE_ID_BOTH_DIR_INFORMATION, LastAccessTime );
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, LastWriteTime  , FILE_ID_BOTH_DIR_INFORMATION, LastWriteTime  );
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, ChangeTime     , FILE_ID_BOTH_DIR_INFORMATION, ChangeTime     );
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, EndOfFile      , FILE_ID_BOTH_DIR_INFORMATION, EndOfFile      );
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, AllocationSize , FILE_ID_BOTH_DIR_INFORMATION, AllocationSize );
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, FileAttributes , FILE_ID_BOTH_DIR_INFORMATION, FileAttributes );
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, FileNameLength , FILE_ID_BOTH_DIR_INFORMATION, FileNameLength );
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, EaSize         , FILE_ID_BOTH_DIR_INFORMATION, EaSize         );
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, ShortNameLength, FILE_ID_BOTH_DIR_INFORMATION, ShortNameLength);
AssertCompileMembersSameSizeAndOffset(FILE_BOTH_DIR_INFORMATION, ShortName      , FILE_ID_BOTH_DIR_INFORMATION, ShortName      );



size_t rtDirNativeGetStructSize(const char *pszPath)
{
    NOREF(pszPath);
    return sizeof(RTDIRINTERNAL);
}


int rtDirNativeOpen(PRTDIRINTERNAL pDir, uintptr_t hRelativeDir, void *pvNativeRelative)
{
    /*
     * Convert the filter to UTF-16.
     */
    int rc;
    pDir->pNtFilterStr = NULL;
    if (   pDir->cchFilter > 0
        && pDir->enmFilter == RTDIRFILTER_WINNT)
    {
        PRTUTF16 pwszTmp;
        rc = RTStrToUtf16(pDir->pszFilter, &pwszTmp);
        if (RT_FAILURE(rc))
            return rc;
        pDir->NtFilterStr.Buffer = pwszTmp;
        pDir->NtFilterStr.Length = pDir->NtFilterStr.MaximumLength = (uint16_t)(RTUtf16Len(pwszTmp) * sizeof(RTUTF16));
        pDir->pNtFilterStr = &pDir->NtFilterStr;
    }

    /*
     * Try open the directory
     */
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
    bool fObjDir = false;
#endif
    if (hRelativeDir != ~(uintptr_t)0 && pvNativeRelative == NULL)
    {
        /* Caller already opened it, easy! */
        pDir->hDir = (HANDLE)hRelativeDir;
        rc = VINF_SUCCESS;
    }
    else
    {
        /*
         * If we have to check for reparse points, this gets complicated!
         */
        static int volatile g_fReparsePoints = -1;
        uint32_t            fOptions         = FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT;
        int fReparsePoints = g_fReparsePoints;
        if (   fReparsePoints != 0
            && (pDir->fFlags & RTDIR_F_NO_FOLLOW)
            && !pDir->fDirSlash)
            fOptions |= FILE_OPEN_REPARSE_POINT;

        ACCESS_MASK fDesiredAccess = FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE;
        for (;;)
        {
            if (pvNativeRelative == NULL)
                rc = RTNtPathOpenDir(pDir->pszPath,
                                     fDesiredAccess,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     fOptions,
                                     OBJ_CASE_INSENSITIVE,
                                     &pDir->hDir,
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
                                     &fObjDir
#else
                                     NULL
#endif
                                     );
            else
                rc = RTNtPathOpenDirEx((HANDLE)hRelativeDir,
                                       (struct _UNICODE_STRING *)pvNativeRelative,
                                       fDesiredAccess,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       fOptions,
                                       OBJ_CASE_INSENSITIVE,
                                       &pDir->hDir,
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
                                       &fObjDir
#else
                                       NULL
#endif
                                       );
            if (   rc == VERR_ACCESS_DENIED             /* Seen with c:\windows\system32\com\dmp on w7 & w10 (admin mode). */
                && (fDesiredAccess & FILE_TRAVERSE))
            {
                fDesiredAccess &= ~FILE_TRAVERSE;
                continue;
            }
            if (   !(fOptions & FILE_OPEN_REPARSE_POINT)
                || (rc != VINF_SUCCESS && rc != VERR_INVALID_PARAMETER) )
                break;
            if (rc == VINF_SUCCESS)
            {
                if (fReparsePoints == -1)
                    g_fReparsePoints = 1;

                /*
                 * We now need to check if we opened a symbolic directory link.
                 * (These can be enumerated, but contains only '.' and '..'.)
                 */
                FILE_ATTRIBUTE_TAG_INFORMATION  TagInfo = { 0, 0 };
                IO_STATUS_BLOCK                 Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
                NTSTATUS rcNt = NtQueryInformationFile(pDir->hDir, &Ios, &TagInfo, sizeof(TagInfo), FileAttributeTagInformation);
                AssertMsg(NT_SUCCESS(rcNt), ("%#x\n", rcNt));
                if (!NT_SUCCESS(rcNt))
                    TagInfo.FileAttributes = TagInfo.ReparseTag = 0;
                if (!(TagInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    break;

                NtClose(pDir->hDir);
                pDir->hDir = RTNT_INVALID_HANDLE_VALUE;

                if (TagInfo.ReparseTag == IO_REPARSE_TAG_SYMLINK)
                {
                    rc = VERR_IS_A_SYMLINK;
                    break;
                }

                /* Reparse point that isn't a symbolic link, try follow the reparsing. */
            }
            else if (fReparsePoints == -1)
                g_fReparsePoints = fReparsePoints = 0;
            fOptions &= ~FILE_OPEN_REPARSE_POINT;
        }

    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Init data.
         */
        pDir->fDataUnread = false; /* spelling it out */
        pDir->uDirDev     = 0;
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
        if (fObjDir)
            pDir->enmInfoClass = FileMaximumInformation; /* object directory. */
#endif
    }
    return rc;
}


RTDECL(int) RTDirClose(RTDIR hDir)
{
    PRTDIRINTERNAL pDir = hDir;

    /*
     * Validate input.
     */
    if (!pDir)
        return VERR_INVALID_PARAMETER;
    if (pDir->u32Magic != RTDIR_MAGIC)
    {
        AssertMsgFailed(("Invalid pDir=%p\n", pDir));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Close the handle.
     */
    pDir->u32Magic = ~RTDIR_MAGIC;
    if (pDir->hDir != RTNT_INVALID_HANDLE_VALUE)
    {
        int rc = RTNtPathClose(pDir->hDir);
        AssertRC(rc);
        pDir->hDir = RTNT_INVALID_HANDLE_VALUE;
    }
    RTStrFree(pDir->pszName);
    pDir->pszName = NULL;
    RTUtf16Free(pDir->NtFilterStr.Buffer);
    pDir->NtFilterStr.Buffer = NULL;
    RTMemFree(pDir->pabBuffer);
    pDir->pabBuffer = NULL;
    RTMemFree(pDir);

    return VINF_SUCCESS;
}


/**
 * Checks the validity of the current record.
 *
 * @returns IPRT status code
 * @param   pThis       The directory instance data.
 */
static int rtDirNtCheckRecord(PRTDIRINTERNAL pThis)
{
#if defined(RTDIR_NT_STRICT) || defined(RT_ARCH_X86)
# ifdef IPRT_WITH_NT_PATH_PASSTHRU
    if (pThis->enmInfoClass != FileMaximumInformation)
# endif
    {
        uintptr_t uEndAddr;
        if (pThis->enmInfoClass == FileIdBothDirectoryInformation)
            uEndAddr = (uintptr_t)&pThis->uCurData.pBothId->FileName[0];
        else
            uEndAddr = (uintptr_t)&pThis->uCurData.pBoth->FileName[0];

# ifdef RT_ARCH_X86
        /* Workaround for NT 3.1 bug where FAT returns a too short buffer length.
           Including all NT 3.x versions in case it bug was fixed till NT 4. */
        uintptr_t const uEndBuffer = (uintptr_t)&pThis->pabBuffer[pThis->cbBuffer];
        if (   uEndAddr < uEndBuffer
            && uEndAddr + pThis->uCurData.pBoth->FileNameLength <= uEndBuffer)
        { /* likely */ }
        else if (   (   g_enmWinVer == kRTWinOSType_NT310
                     || g_enmWinVer == kRTWinOSType_NT350 // not sure when it was fixed...
                     || g_enmWinVer == kRTWinOSType_NT351)
                 && pThis->enmInfoClass == FileBothDirectoryInformation)
        {
            size_t cbLeft = (uintptr_t)&pThis->pabBuffer[pThis->cbBufferAlloc] - (uintptr_t)pThis->uCurData.pBoth;
            if (   cbLeft >= RT_UOFFSETOF(FILE_BOTH_DIR_INFORMATION, FileName)
                && pThis->uCurData.pBoth->FileNameLength > 0
                && cbLeft >= RT_UOFFSETOF(FILE_BOTH_DIR_INFORMATION, FileName) + pThis->uCurData.pBoth->FileNameLength)
            {
                pThis->cbBuffer = ((uintptr_t)&pThis->uCurData.pBoth->FileName[0] + pThis->uCurData.pBoth->FileNameLength)
                                - (uintptr_t)&pThis->pabBuffer[0];
            }
        }
# endif

# ifdef RTDIR_NT_STRICT
        AssertReturn(uEndAddr < (uintptr_t)&pThis->pabBuffer[pThis->cbBuffer], VERR_IO_GEN_FAILURE);
        AssertReturn(pThis->uCurData.pBoth->FileNameLength < _64K, VERR_FILENAME_TOO_LONG);
        AssertReturn((pThis->uCurData.pBoth->FileNameLength & 1) == 0, VERR_IO_GEN_FAILURE);

        uEndAddr += pThis->uCurData.pBoth->FileNameLength;
        AssertReturn(uEndAddr <= (uintptr_t)&pThis->pabBuffer[pThis->cbBuffer], VERR_IO_GEN_FAILURE);

        AssertReturn((unsigned)pThis->uCurData.pBoth->ShortNameLength <= sizeof(pThis->uCurData.pBoth->ShortName),
                     VERR_IO_GEN_FAILURE);
# endif
    }
#else
    RT_NOREF_PV(pThis);
#endif

    return VINF_SUCCESS;
}


/**
 * Advances the buffer pointer.
 *
 * @param   pThis       The directory instance data.
 */
static int rtDirNtAdvanceBuffer(PRTDIRINTERNAL pThis)
{
    int rc;

#ifdef IPRT_WITH_NT_PATH_PASSTHRU
    if (pThis->enmInfoClass == FileMaximumInformation)
    {
        pThis->uCurData.pObjDir++;
        pThis->fDataUnread = pThis->uCurData.pObjDir->Name.Length != 0;
        return VINF_SUCCESS;
    }
#endif

    pThis->fDataUnread = false;

    uint32_t const offNext = pThis->uCurData.pBoth->NextEntryOffset;
    if (offNext == 0)
        return VINF_SUCCESS;

#ifdef RTDIR_NT_STRICT
    /* Make sure the next-record offset is beyond the current record. */
    size_t cbRec;
    if (pThis->enmInfoClass == FileIdBothDirectoryInformation)
        cbRec = RT_UOFFSETOF(FILE_ID_BOTH_DIR_INFORMATION, FileName);
    else
        cbRec = RT_UOFFSETOF(FILE_BOTH_DIR_INFORMATION, FileName);
    cbRec += pThis->uCurData.pBoth->FileNameLength;
    AssertReturn(offNext >= cbRec, VERR_IO_GEN_FAILURE);
#endif
    pThis->uCurData.u += offNext;

    rc = rtDirNtCheckRecord(pThis);
    pThis->fDataUnread = RT_SUCCESS(rc);
    return rc;
}


/**
 * Fetches more data from the file system.
 *
 * @returns IPRT status code
 * @param   pThis       The directory instance data.
 */
static int rtDirNtFetchMore(PRTDIRINTERNAL pThis)
{
    Assert(!pThis->fDataUnread);

    /*
     * Allocate the buffer the first time around.
     * We do this in lazy fashion as some users of RTDirOpen will not actually
     * list any files, just open it for various reasons.
     *
     * We also reduce the buffer size for networked devices as the windows 7-8.1,
     * server 2012, ++ CIFS servers or/and IFSes screws up buffers larger than 64KB.
     * There is an alternative hack below, btw.  We'll leave both in for now.
     */
    bool fFirst = false;
    if (!pThis->pabBuffer)
    {
        pThis->cbBufferAlloc = _256K;
        if (true) /** @todo skip for known local devices, like the boot device? */
        {
            IO_STATUS_BLOCK Ios2 = RTNT_IO_STATUS_BLOCK_INITIALIZER;
            FILE_FS_DEVICE_INFORMATION Info = { 0, 0 };
            NTSTATUS rcNt2 = NtQueryVolumeInformationFile(pThis->hDir, &Ios2, &Info, sizeof(Info), FileFsDeviceInformation);
            if (   !NT_SUCCESS(rcNt2)
                || (Info.Characteristics & FILE_REMOTE_DEVICE)
                || Info.DeviceType == FILE_DEVICE_NETWORK
                || Info.DeviceType == FILE_DEVICE_NETWORK_FILE_SYSTEM
                || Info.DeviceType == FILE_DEVICE_NETWORK_REDIRECTOR
                || Info.DeviceType == FILE_DEVICE_SMB)
                pThis->cbBufferAlloc = _64K;
        }

        fFirst = false;
        pThis->pabBuffer = (uint8_t *)RTMemAlloc(pThis->cbBufferAlloc);
        if (!pThis->pabBuffer)
        {
            do
            {
                pThis->cbBufferAlloc /= 4;
                pThis->pabBuffer = (uint8_t *)RTMemAlloc(pThis->cbBufferAlloc);
            } while (pThis->pabBuffer == NULL && pThis->cbBufferAlloc > _4K);
            if (!pThis->pabBuffer)
                return VERR_NO_MEMORY;
        }

        /*
         * Also try determining the device number.
         */
        PFILE_FS_VOLUME_INFORMATION pVolInfo = (PFILE_FS_VOLUME_INFORMATION)pThis->pabBuffer;
        pVolInfo->VolumeSerialNumber = 0;
        IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        NTSTATUS rcNt = NtQueryVolumeInformationFile(pThis->hDir, &Ios,
                                                     pVolInfo, RT_MIN(_2K, pThis->cbBufferAlloc),
                                                     FileFsVolumeInformation);
        if (NT_SUCCESS(rcNt) && NT_SUCCESS(Ios.Status))
            pThis->uDirDev = pVolInfo->VolumeSerialNumber;
        else
            pThis->uDirDev = 0;
        AssertCompile(sizeof(pThis->uDirDev) == sizeof(pVolInfo->VolumeSerialNumber));
        /** @todo Grow RTDEV to 64-bit and add low dword of VolumeCreationTime to the top of uDirDev. */
    }

    /*
     * Read more.
     */
    NTSTATUS rcNt;
    IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    if (pThis->enmInfoClass != (FILE_INFORMATION_CLASS)0)
    {
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
        if (pThis->enmInfoClass == FileMaximumInformation)
        {
            Ios.Information = 0;
            Ios.Status = rcNt = NtQueryDirectoryObject(pThis->hDir,
                                                       pThis->pabBuffer,
                                                       pThis->cbBufferAlloc,
                                                       RTDIR_NT_SINGLE_RECORD /*ReturnSingleEntry */,
                                                       pThis->fRestartScan,
                                                       &pThis->uObjDirCtx,
                                                       (PULONG)&Ios.Information);
        }
        else
#endif
            rcNt = NtQueryDirectoryFile(pThis->hDir,
                                        NULL /* Event */,
                                        NULL /* ApcRoutine */,
                                        NULL /* ApcContext */,
                                        &Ios,
                                        pThis->pabBuffer,
                                        pThis->cbBufferAlloc,
                                        pThis->enmInfoClass,
                                        RTDIR_NT_SINGLE_RECORD /*ReturnSingleEntry */,
                                        pThis->pNtFilterStr,
                                        pThis->fRestartScan);
    }
    else
    {
        /*
         * The first time around we have to figure which info class we can use
         * as well as the right buffer size.  We prefer an info class which
         * gives us file IDs (Vista+ IIRC) and we prefer large buffers (for long
         * ReFS file names and such), but we'll settle for whatever works...
         *
         * The windows 7 thru 8.1 CIFS servers have been observed to have
         * trouble with large buffers, but weirdly only when listing large
         * directories.  Seems 0x10000 is the max.  (Samba does not exhibit
         * these problems, of course.)
         *
         * This complicates things.  The buffer size issues causes an
         * STATUS_INVALID_PARAMETER error.  Now, you would expect the lack of
         * FileIdBothDirectoryInformation support to return
         * STATUS_INVALID_INFO_CLASS, but I'm not entirely sure if we can 100%
         * depend on third IFSs to get that right.  Nor, am I entirely confident
         * that we can depend on them to check the class before the buffer size.
         *
         * Thus the mess.
         */
        if (RT_MAKE_U64(RTNtCurrentPeb()->OSMinorVersion, RTNtCurrentPeb()->OSMajorVersion) > RT_MAKE_U64(0,5) /* > W2K */)
            pThis->enmInfoClass = FileIdBothDirectoryInformation; /* Introduced in XP, from I can tell. */
        else
            pThis->enmInfoClass = FileBothDirectoryInformation;
        rcNt = NtQueryDirectoryFile(pThis->hDir,
                                    NULL /* Event */,
                                    NULL /* ApcRoutine */,
                                    NULL /* ApcContext */,
                                    &Ios,
                                    pThis->pabBuffer,
                                    pThis->cbBufferAlloc,
                                    pThis->enmInfoClass,
                                    RTDIR_NT_SINGLE_RECORD /*ReturnSingleEntry */,
                                    pThis->pNtFilterStr,
                                    pThis->fRestartScan);
        if (NT_SUCCESS(rcNt))
        { /* likely */ }
        else
        {
            bool fRestartScan = pThis->fRestartScan;
            for (unsigned iRetry = 0; iRetry < 2; iRetry++)
            {
                if (   rcNt == STATUS_INVALID_INFO_CLASS
                    || rcNt == STATUS_INVALID_PARAMETER_8
                    || iRetry != 0)
                    pThis->enmInfoClass = FileBothDirectoryInformation;

                uint32_t cbBuffer = pThis->cbBufferAlloc;
                if (   rcNt == STATUS_INVALID_PARAMETER
                    || rcNt == STATUS_INVALID_PARAMETER_7
                    || rcNt == STATUS_INVALID_NETWORK_RESPONSE
                    || iRetry != 0)
                {
                    cbBuffer = RT_MIN(cbBuffer / 2, _64K);
                    fRestartScan = true;
                }

                for (;;)
                {
                    rcNt = NtQueryDirectoryFile(pThis->hDir,
                                                NULL /* Event */,
                                                NULL /* ApcRoutine */,
                                                NULL /* ApcContext */,
                                                &Ios,
                                                pThis->pabBuffer,
                                                cbBuffer,
                                                pThis->enmInfoClass,
                                                RTDIR_NT_SINGLE_RECORD /*ReturnSingleEntry */,
                                                pThis->pNtFilterStr,
                                                fRestartScan);
                    if (   NT_SUCCESS(rcNt)
                        || cbBuffer == pThis->cbBufferAlloc
                        || cbBuffer <= sizeof(*pThis->uCurData.pBothId) + sizeof(WCHAR) * 260)
                        break;

                    /* Reduce the buffer size agressivly and try again.  We fall back to
                       FindFirstFile values for the final lap.  This means we'll do 4 rounds
                       with the current initial buffer size (64KB, 8KB, 1KB, 0x278/0x268). */
                    cbBuffer /= 8;
                    if (cbBuffer < 1024)
                        cbBuffer = pThis->enmInfoClass == FileIdBothDirectoryInformation
                                 ? sizeof(*pThis->uCurData.pBothId) + sizeof(WCHAR) * 260
                                 : sizeof(*pThis->uCurData.pBoth)   + sizeof(WCHAR) * 260;
                }
                if (NT_SUCCESS(rcNt))
                {
                    pThis->cbBufferAlloc = cbBuffer;
                    break;
                }
            }
        }
    }
    if (!NT_SUCCESS(rcNt))
    {
        /* Note! VBoxSVR and CIFS file systems both ends up with STATUS_NO_SUCH_FILE here instead of STATUS_NO_MORE_FILES. */
        if (rcNt == STATUS_NO_MORE_FILES || rcNt == STATUS_NO_MORE_ENTRIES || rcNt == STATUS_NO_SUCH_FILE)
            return VERR_NO_MORE_FILES;
        return RTErrConvertFromNtStatus(rcNt);
    }
    pThis->fRestartScan = false;
    AssertMsg(  Ios.Information
              > (pThis->enmInfoClass == FileMaximumInformation ? sizeof(*pThis->uCurData.pObjDir) : sizeof(*pThis->uCurData.pBoth)),
              ("Ios.Information=%#x\n", Ios.Information));

    /*
     * Set up the data members.
     */
    pThis->uCurData.u  = (uintptr_t)pThis->pabBuffer;
    pThis->cbBuffer    = Ios.Information;

    int rc = rtDirNtCheckRecord(pThis);
    pThis->fDataUnread = RT_SUCCESS(rc);

    return rc;
}


/**
 * Converts the name from UTF-16 to UTF-8.
 *
 * Fortunately, the names are relative to the directory, so we won't have to do
 * any sweaty path style coversion. :-)
 *
 * @returns IPRT status code
 * @param   pThis       The directory instance data.
 * @param   cbName      The file name length in bytes.
 * @param   pwsName     The file name, not terminated.
 */
static int rtDirNtConvertName(PRTDIRINTERNAL pThis, uint32_t cbName, PCRTUTF16 pwsName)
{
    int rc = RTUtf16ToUtf8Ex(pwsName, cbName / 2, &pThis->pszName, pThis->cbNameAlloc, &pThis->cchName);
    if (RT_SUCCESS(rc))
    {
        if (!pThis->cbNameAlloc)
            pThis->cbNameAlloc = pThis->cchName + 1;
    }
    else if (rc == VERR_BUFFER_OVERFLOW)
    {
        RTStrFree(pThis->pszName);
        pThis->pszName = NULL;
        pThis->cbNameAlloc = 0;

        rc = RTUtf16ToUtf8Ex(pwsName, cbName / 2, &pThis->pszName, pThis->cbNameAlloc, &pThis->cchName);
        if (RT_SUCCESS(rc))
            pThis->cbNameAlloc = pThis->cchName + 1;
    }
    Assert(RT_SUCCESS(rc) ? pThis->pszName != NULL : pThis->pszName == NULL);
    return rc;
}


/**
 * Converts the name of the current record.
 *
 * @returns IPRT status code.
 * @param   pThis       The directory instance data.
 */
static int rtDirNtConvertCurName(PRTDIRINTERNAL pThis)
{
    switch (pThis->enmInfoClass)
    {
        case FileIdBothDirectoryInformation:
            return rtDirNtConvertName(pThis, pThis->uCurData.pBothId->FileNameLength, pThis->uCurData.pBothId->FileName);
        case FileBothDirectoryInformation:
            return rtDirNtConvertName(pThis, pThis->uCurData.pBoth->FileNameLength, pThis->uCurData.pBoth->FileName);
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
        case FileMaximumInformation:
            return rtDirNtConvertName(pThis, pThis->uCurData.pObjDir->Name.Length, pThis->uCurData.pObjDir->Name.Buffer);
#endif

        default:
            AssertFailedReturn(VERR_INTERNAL_ERROR_3);
    }
}


RTDECL(int) RTDirRead(RTDIR hDir, PRTDIRENTRY pDirEntry, size_t *pcbDirEntry)
{
    PRTDIRINTERNAL pDir = hDir;
    int rc;

    /*
     * Validate input.
     */
    AssertPtrReturn(pDir, VERR_INVALID_POINTER);
    AssertReturn(pDir->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pDirEntry, VERR_INVALID_POINTER);
    size_t cbDirEntry = sizeof(*pDirEntry);
    if (pcbDirEntry)
    {
        cbDirEntry = *pcbDirEntry;
        AssertMsgReturn(cbDirEntry >= RT_UOFFSETOF(RTDIRENTRY, szName[2]),
                        ("Invalid *pcbDirEntry=%zu (min %zu)\n", *pcbDirEntry, RT_UOFFSETOF(RTDIRENTRY, szName[2])),
                        VERR_INVALID_PARAMETER);
    }

    /*
     * Fetch data?
     */
    if (!pDir->fDataUnread)
    {
        rc = rtDirNtFetchMore(pDir);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Convert the filename to UTF-8.
     */
    rc = rtDirNtConvertCurName(pDir);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Check if we've got enough space to return the data.
     */
    const char  *pszName    = pDir->pszName;
    const size_t cchName    = pDir->cchName;
    const size_t cbRequired = RT_UOFFSETOF(RTDIRENTRY, szName[1]) + cchName;
    if (pcbDirEntry)
        *pcbDirEntry = cbRequired;
    if (cbRequired > cbDirEntry)
        return VERR_BUFFER_OVERFLOW;

    /*
     * Setup the returned data.
     */
    pDirEntry->cbName = (uint16_t)cchName; Assert(pDirEntry->cbName == cchName);
    memcpy(pDirEntry->szName, pszName, cchName + 1);

    pDirEntry->INodeId = pDir->enmInfoClass == FileIdBothDirectoryInformation
                       ? pDir->uCurData.pBothId->FileId.QuadPart : 0;

#ifdef IPRT_WITH_NT_PATH_PASSTHRU
    if (pDir->enmInfoClass != FileMaximumInformation)
#endif
    {
        switch (   pDir->uCurData.pBoth->FileAttributes
                & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
        {
            default:
                AssertFailed();
            case 0:
                pDirEntry->enmType = RTDIRENTRYTYPE_FILE;
                break;

            case FILE_ATTRIBUTE_DIRECTORY:
                pDirEntry->enmType = RTDIRENTRYTYPE_DIRECTORY;
                break;

            case FILE_ATTRIBUTE_REPARSE_POINT:
            case FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY:
                /* EaSize is here reused for returning the repharse tag value. */
                if (pDir->uCurData.pBoth->EaSize == IO_REPARSE_TAG_SYMLINK)
                    pDirEntry->enmType = RTDIRENTRYTYPE_SYMLINK;
                break;
        }
    }
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
    else
    {
        pDirEntry->enmType = RTDIRENTRYTYPE_UNKNOWN;
        if (rtNtCompWideStrAndAscii(pDir->uCurData.pObjDir->TypeName.Buffer, pDir->uCurData.pObjDir->TypeName.Length,
                                    RT_STR_TUPLE("Directory")))
            pDirEntry->enmType = RTDIRENTRYTYPE_DIRECTORY;
        else if (rtNtCompWideStrAndAscii(pDir->uCurData.pObjDir->TypeName.Buffer, pDir->uCurData.pObjDir->TypeName.Length,
                                         RT_STR_TUPLE("SymbolicLink")))
            pDirEntry->enmType = RTDIRENTRYTYPE_SYMLINK;
    }
#endif

    return rtDirNtAdvanceBuffer(pDir);
}


RTDECL(int) RTDirReadEx(RTDIR hDir, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry,
                        RTFSOBJATTRADD enmAdditionalAttribs, uint32_t fFlags)
{
    PRTDIRINTERNAL pDir = hDir;
    int rc;

    /*
     * Validate input.
     */
    AssertPtrReturn(pDir, VERR_INVALID_POINTER);
    AssertReturn(pDir->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pDirEntry, VERR_INVALID_POINTER);

    AssertReturn(enmAdditionalAttribs >= RTFSOBJATTRADD_NOTHING && enmAdditionalAttribs <= RTFSOBJATTRADD_LAST,
                 VERR_INVALID_PARAMETER);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);

    size_t cbDirEntry = sizeof(*pDirEntry);
    if (pcbDirEntry)
    {
        cbDirEntry = *pcbDirEntry;
        AssertMsgReturn(cbDirEntry >= RT_UOFFSETOF(RTDIRENTRYEX, szName[2]),
                        ("Invalid *pcbDirEntry=%zu (min %zu)\n", *pcbDirEntry, RT_UOFFSETOF(RTDIRENTRYEX, szName[2])),
                        VERR_INVALID_PARAMETER);
    }

    /*
     * Fetch data?
     */
    if (!pDir->fDataUnread)
    {
        rc = rtDirNtFetchMore(pDir);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Convert the filename to UTF-8.
     */
    rc = rtDirNtConvertCurName(pDir);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Check if we've got enough space to return the data.
     */
    const char  *pszName    = pDir->pszName;
    const size_t cchName    = pDir->cchName;
    const size_t cbRequired = RT_UOFFSETOF(RTDIRENTRYEX, szName[1]) + cchName;
    if (pcbDirEntry)
        *pcbDirEntry = cbRequired;
    if (cbRequired > cbDirEntry)
        return VERR_BUFFER_OVERFLOW;

    /*
     * Setup the returned data.
     */
    PFILE_BOTH_DIR_INFORMATION pBoth = pDir->uCurData.pBoth;

    pDirEntry->cbName  = (uint16_t)cchName;  Assert(pDirEntry->cbName == cchName);
    memcpy(pDirEntry->szName, pszName, cchName + 1);
    memset(pDirEntry->wszShortName, 0, sizeof(pDirEntry->wszShortName));
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
    if (pDir->enmInfoClass != FileMaximumInformation)
#endif
    {
        uint8_t cbShort = pBoth->ShortNameLength;
        if (cbShort > 0)
        {
            AssertStmt(cbShort < sizeof(pDirEntry->wszShortName), cbShort = sizeof(pDirEntry->wszShortName) - 2);
            memcpy(pDirEntry->wszShortName, pBoth->ShortName, cbShort);
            pDirEntry->cwcShortName = cbShort / 2;
        }
        else
            pDirEntry->cwcShortName = 0;

        pDirEntry->Info.cbObject    = pBoth->EndOfFile.QuadPart;
        pDirEntry->Info.cbAllocated = pBoth->AllocationSize.QuadPart;

        Assert(sizeof(uint64_t) == sizeof(pBoth->CreationTime));
        RTTimeSpecSetNtTime(&pDirEntry->Info.BirthTime,         pBoth->CreationTime.QuadPart);
        RTTimeSpecSetNtTime(&pDirEntry->Info.AccessTime,        pBoth->LastAccessTime.QuadPart);
        RTTimeSpecSetNtTime(&pDirEntry->Info.ModificationTime,  pBoth->LastWriteTime.QuadPart);
        RTTimeSpecSetNtTime(&pDirEntry->Info.ChangeTime,        pBoth->ChangeTime.QuadPart);

        pDirEntry->Info.Attr.fMode  = rtFsModeFromDos((pBoth->FileAttributes << RTFS_DOS_SHIFT) & RTFS_DOS_MASK_NT,
                                                       pszName, cchName, pBoth->EaSize, 0);
    }
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
    else
    {
        pDirEntry->cwcShortName = 0;
        pDirEntry->Info.cbObject    = 0;
        pDirEntry->Info.cbAllocated = 0;
        RTTimeSpecSetNtTime(&pDirEntry->Info.BirthTime,         0);
        RTTimeSpecSetNtTime(&pDirEntry->Info.AccessTime,        0);
        RTTimeSpecSetNtTime(&pDirEntry->Info.ModificationTime,  0);
        RTTimeSpecSetNtTime(&pDirEntry->Info.ChangeTime,        0);

        if (rtNtCompWideStrAndAscii(pDir->uCurData.pObjDir->TypeName.Buffer, pDir->uCurData.pObjDir->TypeName.Length,
                                    RT_STR_TUPLE("Directory")))
            pDirEntry->Info.Attr.fMode = RTFS_DOS_DIRECTORY | RTFS_TYPE_DIRECTORY | 0777;
        else if (rtNtCompWideStrAndAscii(pDir->uCurData.pObjDir->TypeName.Buffer, pDir->uCurData.pObjDir->TypeName.Length,
                                         RT_STR_TUPLE("SymbolicLink")))
            pDirEntry->Info.Attr.fMode = RTFS_DOS_NT_REPARSE_POINT | RTFS_TYPE_SYMLINK | 0777;
        else if (rtNtCompWideStrAndAscii(pDir->uCurData.pObjDir->TypeName.Buffer, pDir->uCurData.pObjDir->TypeName.Length,
                                         RT_STR_TUPLE("Device")))
            pDirEntry->Info.Attr.fMode = RTFS_DOS_NT_DEVICE | RTFS_TYPE_DEV_CHAR | 0666;
        else
            pDirEntry->Info.Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_FILE | 0666;
    }
#endif

    /*
     * Requested attributes (we cannot provide anything actually).
     */
    switch (enmAdditionalAttribs)
    {
        case RTFSOBJATTRADD_EASIZE:
            pDirEntry->Info.Attr.enmAdditional          = RTFSOBJATTRADD_EASIZE;
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
            if (pDir->enmInfoClass == FileMaximumInformation)
                pDirEntry->Info.Attr.u.EASize.cb        = 0;
            else
#endif
                pDirEntry->Info.Attr.u.EASize.cb        = pBoth->EaSize;
            break;

        case RTFSOBJATTRADD_UNIX:
            pDirEntry->Info.Attr.enmAdditional          = RTFSOBJATTRADD_UNIX;
            pDirEntry->Info.Attr.u.Unix.uid             = NIL_RTUID;
            pDirEntry->Info.Attr.u.Unix.gid             = NIL_RTGID;
            pDirEntry->Info.Attr.u.Unix.cHardlinks      = 1;
            pDirEntry->Info.Attr.u.Unix.INodeIdDevice   = pDir->uDirDev;
            pDirEntry->Info.Attr.u.Unix.INodeId         = 0;
            if (   pDir->enmInfoClass == FileIdBothDirectoryInformation
                && pDir->uCurData.pBothId->FileId.QuadPart != UINT64_MAX)
                pDirEntry->Info.Attr.u.Unix.INodeId     = pDir->uCurData.pBothId->FileId.QuadPart;
            pDirEntry->Info.Attr.u.Unix.fFlags          = 0;
            pDirEntry->Info.Attr.u.Unix.GenerationId    = 0;
            pDirEntry->Info.Attr.u.Unix.Device          = 0;
            break;

        case RTFSOBJATTRADD_NOTHING:
            pDirEntry->Info.Attr.enmAdditional          = RTFSOBJATTRADD_NOTHING;
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
            pDirEntry->Info.Attr.enmAdditional          = RTFSOBJATTRADD_UNIX_OWNER;
            pDirEntry->Info.Attr.u.UnixOwner.uid        = NIL_RTUID;
            pDirEntry->Info.Attr.u.UnixOwner.szName[0]  = '\0'; /** @todo return something sensible here. */
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            pDirEntry->Info.Attr.enmAdditional          = RTFSOBJATTRADD_UNIX_GROUP;
            pDirEntry->Info.Attr.u.UnixGroup.gid        = NIL_RTGID;
            pDirEntry->Info.Attr.u.UnixGroup.szName[0]  = '\0';
            break;

        default:
            AssertMsgFailed(("Impossible!\n"));
            return VERR_INTERNAL_ERROR;
    }

    /*
     * Follow links if requested.
     */
    if (   (fFlags & RTPATH_F_FOLLOW_LINK)
        && RTFS_IS_SYMLINK(fFlags))
    {
        /** @todo Symlinks: Find[First|Next]FileW will return info about
            the link, so RTPATH_F_FOLLOW_LINK is not handled correctly. */
    }

    /*
     * Finally advance the buffer.
     */
    return rtDirNtAdvanceBuffer(pDir);
}


RTDECL(int) RTDirRewind(RTDIR hDir)
{
    /*
     * Validate and digest input.
     */
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    /*
     * The work is done on the next call to rtDirNtFetchMore.
     */
    pThis->fRestartScan = true;
    pThis->fDataUnread  = false;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTDirQueryInfo(RTDIR hDir, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs)
{
    PRTDIRINTERNAL pDir = hDir;
    AssertPtrReturn(pDir, VERR_INVALID_POINTER);
    AssertReturn(pDir->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(enmAdditionalAttribs >= RTFSOBJATTRADD_NOTHING && enmAdditionalAttribs <= RTFSOBJATTRADD_LAST,
                 VERR_INVALID_PARAMETER);

    if (pDir->enmInfoClass == FileMaximumInformation)
    {
        /*
         * Directory object (see similar code above and rtPathNtQueryInfoInDirectoryObject).
         */
        pObjInfo->cbObject    = 0;
        pObjInfo->cbAllocated = 0;
        RTTimeSpecSetNtTime(&pObjInfo->BirthTime,         0);
        RTTimeSpecSetNtTime(&pObjInfo->AccessTime,        0);
        RTTimeSpecSetNtTime(&pObjInfo->ModificationTime,  0);
        RTTimeSpecSetNtTime(&pObjInfo->ChangeTime,        0);
        pObjInfo->Attr.fMode = RTFS_DOS_DIRECTORY | RTFS_TYPE_DIRECTORY | 0777;
        pObjInfo->Attr.enmAdditional = enmAdditionalAttribs;
        switch (enmAdditionalAttribs)
        {
            case RTFSOBJATTRADD_NOTHING:
            case RTFSOBJATTRADD_UNIX:
                pObjInfo->Attr.u.Unix.uid             = NIL_RTUID;
                pObjInfo->Attr.u.Unix.gid             = NIL_RTGID;
                pObjInfo->Attr.u.Unix.cHardlinks      = 1;
                pObjInfo->Attr.u.Unix.INodeIdDevice   = pDir->uDirDev;
                pObjInfo->Attr.u.Unix.INodeId         = 0;
                pObjInfo->Attr.u.Unix.fFlags          = 0;
                pObjInfo->Attr.u.Unix.GenerationId    = 0;
                pObjInfo->Attr.u.Unix.Device          = 0;
                break;

            case RTFSOBJATTRADD_EASIZE:
                pObjInfo->Attr.u.EASize.cb            = 0;
                break;

            case RTFSOBJATTRADD_UNIX_OWNER:
                pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_UNIX_OWNER;
                pObjInfo->Attr.u.UnixOwner.uid        = NIL_RTUID;
                pObjInfo->Attr.u.UnixOwner.szName[0]  = '\0'; /** @todo return something sensible here. */
                break;

            case RTFSOBJATTRADD_UNIX_GROUP:
                pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_UNIX_GROUP;
                pObjInfo->Attr.u.UnixGroup.gid        = NIL_RTGID;
                pObjInfo->Attr.u.UnixGroup.szName[0]  = '\0';
                break;

            default:
                AssertMsgFailed(("Impossible!\n"));
                return VERR_INTERNAL_ERROR_2;
        }
        return VINF_SUCCESS;
    }

    /*
     * Regular directory file.
     */
    uint8_t abBuf[_2K];
    return rtPathNtQueryInfoFromHandle(pDir->hDir, abBuf, sizeof(abBuf), pObjInfo, enmAdditionalAttribs, "", 0);
}

