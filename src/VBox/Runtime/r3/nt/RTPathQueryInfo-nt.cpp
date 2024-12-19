/* $Id: RTPathQueryInfo-nt.cpp 107437 2024-12-19 14:03:57Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - RTPathQueryInfo[Ex], Native NT.
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
#define LOG_GROUP RTLOGGROUP_FILE
#include "internal-r3-nt.h"

#include <iprt/path.h>
#include <iprt/err.h>
#include <iprt/time.h>
#include "internal/fs.h"
#include "internal/path.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Helper for comparing a UNICODE_STRING with a string litteral. */
#define ARE_UNICODE_STRINGS_EQUAL(a_UniStr, a_wszType) \
    (   (a_UniStr)->Length == sizeof(a_wszType) - sizeof(RTUTF16) \
     && memcmp((a_UniStr)->Buffer, a_wszType, sizeof(a_wszType) - sizeof(RTUTF16)) == 0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
typedef NTSTATUS (NTAPI *PFNNTQUERYFULLATTRIBUTESFILE)(struct _OBJECT_ATTRIBUTES *, struct _FILE_NETWORK_OPEN_INFORMATION *);
extern PFNNTQUERYFULLATTRIBUTESFILE g_pfnNtQueryFullAttributesFile; /* init-win.cpp */


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



/**
 * Splits up an NT path into directory and filename.
 *
 * @param   pNtName             The path to split.
 * @param   pNtParent           Where to return the directory path.
 * @param   pNtFilename         Where to return the filename part.
 * @param   fNoParentDirSlash   Whether to make sure the directory path doesn't
 *                              end with a slash (except root).
 */
static void ntPathNtSplitName(UNICODE_STRING const *pNtName, UNICODE_STRING *pNtParent, UNICODE_STRING *pNtFilename,
                              bool fNoParentDirSlash)
{
    PRTUTF16 pwszBuffer = pNtName->Buffer;
    size_t   off        = pNtName->Length / sizeof(RTUTF16);

    /* Skip trailing slash if present. */
    if (   off > 0
        && pwszBuffer[off - 1] == '\\')
        off--;

    /* Find the slash before that. */
    RTUTF16  wc;
    while (   off > 0
           && (wc = pwszBuffer[off - 1]) != '\\'
           && wc != '/')
        off--;
    if (off != 0)
    {
        pNtParent->Buffer        = pwszBuffer;
        pNtParent->MaximumLength = pNtParent->Length = (USHORT)(off * sizeof(RTUTF16));
    }
    else
    {
        AssertFailed(); /* This is impossible and won't work (NT doesn't know '.' or '..').  */
        /** @todo query the whole path as it is possible relative. Use the buffer for
         *        temporary name storage. */
        pNtParent->Buffer        = (PWCH)L".";
        pNtParent->Length        = 1 * sizeof(RTUTF16);
        pNtParent->MaximumLength = 2 * sizeof(RTUTF16);
    }

    pNtFilename->Buffer        = &pwszBuffer[off];
    pNtFilename->Length        = pNtName->Length        - (USHORT)(off * sizeof(RTUTF16));
    pNtFilename->MaximumLength = pNtName->MaximumLength - (USHORT)(off * sizeof(RTUTF16));

    while (   fNoParentDirSlash
           && pNtParent->Length > sizeof(RTUTF16)
           && pNtParent->Buffer[pNtParent->Length / sizeof(RTUTF16) - 1] == '\\')
        pNtParent->Length -= sizeof(RTUTF16);
}


/**
 * Deals with enmAddAttr != RTFSOBJATTRADD_UNIX.
 *
 * @returns IPRT status code (usually @a rc).
 * @param   rc                  The return code.
 * @param   pObjInfo            The info to complete.
 * @param   enmAddAttr          What to complete it with.  Caller should fill in
 *                              RTFSOBJATTRADD_UNIX.
 */
static int rtPathNtQueryInfoFillInDummyData(int rc, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_UNIX:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_UNIX;
            break;

        case RTFSOBJATTRADD_NOTHING:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_NOTHING;
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

        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_EASIZE;
            pObjInfo->Attr.u.EASize.cb            = 0;
            break;

        default:
            AssertMsgFailed(("Impossible!\n"));
            rc = VERR_INTERNAL_ERROR;
    }
    return rc;
}


/**
 * Deal with getting info about something that could be in a directory object.
 *
 * @returns IPRT status code
 * @param   pObjAttr        The NT object attribute.
 * @param   pObjInfo        Where to return the info.
 * @param   enmAddAttr      Which extra attributes to get (/fake).
 * @param   fFlags          The flags.
 * @param   pvBuf           Query buffer space.
 * @param   cbBuf           Size of the buffer.  ASSUMES lots of space.
 * @param   rcNtCaller      The status code that got us here.
 */
static int rtPathNtQueryInfoInDirectoryObject(OBJECT_ATTRIBUTES *pObjAttr, PRTFSOBJINFO pObjInfo,
                                              RTFSOBJATTRADD enmAddAttr, uint32_t fFlags,
                                              void *pvBuf, size_t cbBuf, NTSTATUS rcNtCaller)
{
    RT_NOREF(fFlags);

    /*
     * Special case: Root dir.
     */
    if (   pObjAttr->RootDirectory == NULL
        && pObjAttr->ObjectName->Length == sizeof(RTUTF16)
        && pObjAttr->ObjectName->Buffer[0] == '\\')
    {
        pObjInfo->cbObject    = 0;
        pObjInfo->cbAllocated = 0;
        RTTimeSpecSetNtTime(&pObjInfo->BirthTime,         0);
        RTTimeSpecSetNtTime(&pObjInfo->AccessTime,        0);
        RTTimeSpecSetNtTime(&pObjInfo->ModificationTime,  0);
        RTTimeSpecSetNtTime(&pObjInfo->ChangeTime,        0);
        pObjInfo->Attr.fMode = RTFS_DOS_DIRECTORY | RTFS_TYPE_DIRECTORY | 0777;
        return rtPathNtQueryInfoFillInDummyData(VINF_SUCCESS, pObjInfo, enmAddAttr);
    }

    /*
     * We must open and scan the parent directory object.
     */
    UNICODE_STRING NtDirName;
    UNICODE_STRING NtDirEntry;
    ntPathNtSplitName(pObjAttr->ObjectName, &NtDirName, &NtDirEntry, true /*fNoParentDirSlash*/);

    while (   NtDirEntry.Length > sizeof(RTUTF16)
           && NtDirEntry.Buffer[NtDirEntry.Length / sizeof(RTUTF16) - 1] == '\\')
        NtDirEntry.Length -= sizeof(RTUTF16);

    pObjAttr->ObjectName = &NtDirName;
    HANDLE   hDir = RTNT_INVALID_HANDLE_VALUE;
    NTSTATUS rcNt = NtOpenDirectoryObject(&hDir, DIRECTORY_QUERY | DIRECTORY_TRAVERSE, pObjAttr);
    if (NT_SUCCESS(rcNt))
    {
        ULONG uObjDirCtx = 0;
        for (;;)
        {
            ULONG cbReturned = 0;
            rcNt = NtQueryDirectoryObject(hDir,
                                          pvBuf,
                                          (ULONG)cbBuf,
                                          FALSE /*ReturnSingleEntry */,
                                          FALSE /*RestartScan*/,
                                          &uObjDirCtx,
                                          &cbReturned);
            if (!NT_SUCCESS(rcNt))
                break;

            for (POBJECT_DIRECTORY_INFORMATION pObjDir = (POBJECT_DIRECTORY_INFORMATION)pvBuf;
                 pObjDir->Name.Length != 0;
                 pObjDir++)
            {
                if (   pObjDir->Name.Length == NtDirEntry.Length
                    && memcmp(pObjDir->Name.Buffer, NtDirEntry.Buffer, NtDirEntry.Length) == 0)
                {
                    /*
                     * Find it.  Fill in the info we've got and return (see similar code in direnum-r3-nt.cpp).
                     */
                    NtClose(hDir);

                    pObjInfo->cbObject    = 0;
                    pObjInfo->cbAllocated = 0;
                    RTTimeSpecSetNtTime(&pObjInfo->BirthTime,         0);
                    RTTimeSpecSetNtTime(&pObjInfo->AccessTime,        0);
                    RTTimeSpecSetNtTime(&pObjInfo->ModificationTime,  0);
                    RTTimeSpecSetNtTime(&pObjInfo->ChangeTime,        0);

                    if (ARE_UNICODE_STRINGS_EQUAL(&pObjDir->TypeName, L"Directory"))
                        pObjInfo->Attr.fMode = RTFS_DOS_DIRECTORY | RTFS_TYPE_DIRECTORY | 0777;
                    else if (ARE_UNICODE_STRINGS_EQUAL(&pObjDir->TypeName, L"SymbolicLink"))
                        pObjInfo->Attr.fMode = RTFS_DOS_NT_REPARSE_POINT | RTFS_TYPE_SYMLINK | 0777;
                    else if (ARE_UNICODE_STRINGS_EQUAL(&pObjDir->TypeName, L"Device"))
                        pObjInfo->Attr.fMode = RTFS_DOS_NT_DEVICE | RTFS_TYPE_DEV_CHAR | 0666;
                    else
                        pObjInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_FILE | 0666;

                    pObjInfo->Attr.enmAdditional = enmAddAttr;
                    return rtPathNtQueryInfoFillInDummyData(VINF_SUCCESS, pObjInfo, enmAddAttr);
                }
            }
        }

        NtClose(hDir);
        if (rcNt == STATUS_NO_MORE_FILES || rcNt == STATUS_NO_MORE_ENTRIES || rcNt == STATUS_NO_SUCH_FILE)
            return VERR_FILE_NOT_FOUND;
    }
    else
        return RTErrConvertFromNtStatus(rcNtCaller);
    return RTErrConvertFromNtStatus(rcNt);
}


/**
 * Queries information from a file or directory handle.
 *
 * This is shared between the RTPathQueryInfo, RTFileQueryInfo and
 * RTDirQueryInfo code.
 *
 * @returns IPRT status code.
 * @param   hFile               The handle to query information from.  Must have
 *                              the necessary privileges.
 * @param   pvBuf               Pointer to a scratch buffer.
 * @param   cbBuf               The size of the buffer.  This must be large
 *                              enough to hold a FILE_ALL_INFORMATION struct.
 * @param   pObjInfo            Where to return information about the handle.
 * @param   enmAddAttr          What extra info to return.
 * @param   pszPath             The path if this is a file (for exe detect).
 * @param   uReparseTag         The reparse tag number (0 if not applicable) for
 *                              symlink detection/whatnot.
 */
DECLHIDDEN(int) rtPathNtQueryInfoFromHandle(HANDLE hFile, void *pvBuf, size_t cbBuf, PRTFSOBJINFO pObjInfo,
                                            RTFSOBJATTRADD enmAddAttr, const char *pszPath, ULONG uReparseTag)
{
    Assert(cbBuf >= sizeof(FILE_ALL_INFORMATION));

    /** @todo Try optimize this for when RTFSOBJATTRADD_UNIX isn't set? */
    IO_STATUS_BLOCK  Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    NTSTATUS rcNt = NtQueryInformationFile(hFile, &Ios, pvBuf, sizeof(FILE_ALL_INFORMATION), FileAllInformation);
    if (   NT_SUCCESS(rcNt)
        || rcNt == STATUS_BUFFER_OVERFLOW)
    {
        FILE_ALL_INFORMATION *pAllInfo = (FILE_ALL_INFORMATION *)pvBuf;
        pObjInfo->cbObject    = pAllInfo->StandardInformation.EndOfFile.QuadPart;
        pObjInfo->cbAllocated = pAllInfo->StandardInformation.AllocationSize.QuadPart;
        RTTimeSpecSetNtTime(&pObjInfo->BirthTime,         pAllInfo->BasicInformation.CreationTime.QuadPart);
        RTTimeSpecSetNtTime(&pObjInfo->AccessTime,        pAllInfo->BasicInformation.LastAccessTime.QuadPart);
        RTTimeSpecSetNtTime(&pObjInfo->ModificationTime,  pAllInfo->BasicInformation.LastWriteTime.QuadPart);
        RTTimeSpecSetNtTime(&pObjInfo->ChangeTime,        pAllInfo->BasicInformation.ChangeTime.QuadPart);
        pObjInfo->Attr.fMode = rtFsModeFromDos(  (pAllInfo->BasicInformation.FileAttributes << RTFS_DOS_SHIFT)
                                               & RTFS_DOS_MASK_NT,
                                               pszPath, pszPath ? strlen(pszPath) : 0, uReparseTag, 0);
        pObjInfo->Attr.enmAdditional = enmAddAttr;
        if (enmAddAttr == RTFSOBJATTRADD_UNIX)
        {
            pObjInfo->Attr.u.Unix.uid             = ~0U;
            pObjInfo->Attr.u.Unix.gid             = ~0U;
            pObjInfo->Attr.u.Unix.cHardlinks      = RT_MAX(1, pAllInfo->StandardInformation.NumberOfLinks);
            pObjInfo->Attr.u.Unix.INodeIdDevice   = 0; /* below */
            pObjInfo->Attr.u.Unix.INodeId         = pAllInfo->InternalInformation.IndexNumber.QuadPart;
            pObjInfo->Attr.u.Unix.fFlags          = 0;
            pObjInfo->Attr.u.Unix.GenerationId    = 0;
            pObjInfo->Attr.u.Unix.Device          = 0;

            /* Get the serial number. */
            rcNt = NtQueryVolumeInformationFile(hFile, &Ios, pvBuf, (ULONG)RT_MIN(cbBuf, _2K), FileFsVolumeInformation);
            if (NT_SUCCESS(rcNt) || rcNt == STATUS_BUFFER_OVERFLOW)
            {
                FILE_FS_VOLUME_INFORMATION *pVolInfo = (FILE_FS_VOLUME_INFORMATION *)pvBuf;
                pObjInfo->Attr.u.Unix.INodeIdDevice = pVolInfo->VolumeSerialNumber;
            }
        }

        return rtPathNtQueryInfoFillInDummyData(VINF_SUCCESS, pObjInfo, enmAddAttr);
    }
    return RTErrConvertFromNtStatus(rcNt);
}


/**
 * Worker for RTPathQueryInfoEx and RTDirRelPathQueryInfo.
 *
 * @returns IPRT status code.
 * @param   hRootDir            The root directory that pNtName is relative to.
 * @param   pNtName             The NT path which we want to query info for.
 * @param   pObjInfo            Where to return the info.
 * @param   enmAddAttr          What additional info to get/fake.
 * @param   fFlags              Query flags (RTPATH_F_XXX).
 * @param   pszPath             The path for detecting executables and such.
 *                              Pass empty string if not applicable/available.
 */
DECLHIDDEN(int) rtPathNtQueryInfoWorker(HANDLE hRootDir, UNICODE_STRING *pNtName, PRTFSOBJINFO pObjInfo,
                                        RTFSOBJATTRADD enmAddAttr, uint32_t fFlags, const char *pszPath)
{
    /*
     * There are a three different ways of doing this:
     *   1. Use NtQueryFullAttributesFile to the get basic file info.
     *   2. Open whatever the path points to and use NtQueryInformationFile.
     *   3. Open the parent directory and use NtQueryDirectoryFile like RTDirReadEx.
     *
     * The first two options may fail with sharing violations or access denied,
     * in which case we must use the last one as fallback.
     */
    HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
    IO_STATUS_BLOCK     Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    NTSTATUS            rcNt;
    OBJECT_ATTRIBUTES   ObjAttr;
    union
    {
        FILE_NETWORK_OPEN_INFORMATION   NetOpenInfo;
        FILE_ALL_INFORMATION            AllInfo;
        FILE_FS_VOLUME_INFORMATION      VolInfo;
        FILE_BOTH_DIR_INFORMATION       Both;
        FILE_ID_BOTH_DIR_INFORMATION    BothId;
        uint8_t                         abPadding[sizeof(FILE_ID_BOTH_DIR_INFORMATION) + RTPATH_MAX * sizeof(wchar_t)];
    } uBuf;

    /*
     * We can only use the first option if no additional UNIX attribs are
     * requested and it isn't a symbolic link.  NT directory object
     */
    int rc = VINF_TRY_AGAIN;
    if (   enmAddAttr != RTFSOBJATTRADD_UNIX
        && g_pfnNtQueryFullAttributesFile)
    {
        InitializeObjectAttributes(&ObjAttr, pNtName, OBJ_CASE_INSENSITIVE, hRootDir, NULL);
        rcNt = g_pfnNtQueryFullAttributesFile(&ObjAttr, &uBuf.NetOpenInfo);
        if (NT_SUCCESS(rcNt))
        {
            if (!(uBuf.NetOpenInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            {
                pObjInfo->cbObject    = uBuf.NetOpenInfo.EndOfFile.QuadPart;
                pObjInfo->cbAllocated = uBuf.NetOpenInfo.AllocationSize.QuadPart;
                RTTimeSpecSetNtTime(&pObjInfo->BirthTime,         uBuf.NetOpenInfo.CreationTime.QuadPart);
                RTTimeSpecSetNtTime(&pObjInfo->AccessTime,        uBuf.NetOpenInfo.LastAccessTime.QuadPart);
                RTTimeSpecSetNtTime(&pObjInfo->ModificationTime,  uBuf.NetOpenInfo.LastWriteTime.QuadPart);
                RTTimeSpecSetNtTime(&pObjInfo->ChangeTime,        uBuf.NetOpenInfo.ChangeTime.QuadPart);
                pObjInfo->Attr.fMode = rtFsModeFromDos((uBuf.NetOpenInfo.FileAttributes << RTFS_DOS_SHIFT) & RTFS_DOS_MASK_NT,
                                                       pszPath, strlen(pszPath), 0 /*uReparseTag*/, 0);
                pObjInfo->Attr.enmAdditional = enmAddAttr;

                return rtPathNtQueryInfoFillInDummyData(VINF_SUCCESS, pObjInfo, enmAddAttr);
            }
        }
        else if (   rcNt == STATUS_OBJECT_TYPE_MISMATCH
                 || rcNt == STATUS_OBJECT_NAME_INVALID
                 || rcNt == STATUS_INVALID_PARAMETER)
        {
            rc = rtPathNtQueryInfoInDirectoryObject(&ObjAttr, pObjInfo, enmAddAttr, fFlags, &uBuf, sizeof(uBuf), rcNt);
            if (RT_SUCCESS(rc))
                return rc;
        }
        else if (   rcNt != STATUS_ACCESS_DENIED
                 && rcNt != STATUS_SHARING_VIOLATION)
            rc = RTErrConvertFromNtStatus(rcNt);
        else
            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
    }

    /*
     * Try the 2nd option.  We might have to redo this if not following symbolic
     * links and the reparse point isn't a symbolic link but a mount point or similar.
     * We want to return information about the mounted root directory if we can, not
     * the directory in which it was mounted.
     */
    if (rc == VINF_TRY_AGAIN)
    {
        static int volatile g_fReparsePoints = -1;
        uint32_t            fOptions         = FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT;
        int fReparsePoints = g_fReparsePoints;
        if (fReparsePoints != 0 && !(fFlags & RTPATH_F_FOLLOW_LINK))
            fOptions |= FILE_OPEN_REPARSE_POINT;

        InitializeObjectAttributes(&ObjAttr, pNtName, OBJ_CASE_INSENSITIVE, hRootDir, NULL);
        rcNt = NtCreateFile(&hFile,
                            FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                            &ObjAttr,
                            &Ios,
                            NULL /*pcbFile*/,
                            FILE_ATTRIBUTE_NORMAL,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            FILE_OPEN,
                            fOptions,
                            NULL /*pvEaBuffer*/,
                            0 /*cbEa*/);
        if (   (   rcNt == STATUS_INVALID_PARAMETER
                || rcNt == STATUS_INVALID_PARAMETER_9)
            && fReparsePoints == -1
            && (fOptions & FILE_OPEN_REPARSE_POINT))
        {
            fOptions &= ~FILE_OPEN_REPARSE_POINT;
            rcNt = NtCreateFile(&hFile,
                                FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                &ObjAttr,
                                &Ios,
                                NULL /*pcbFile*/,
                                FILE_ATTRIBUTE_NORMAL,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_OPEN,
                                fOptions,
                                NULL /*pvEaBuffer*/,
                                0 /*cbEa*/);
            if (rcNt != STATUS_INVALID_PARAMETER)
                g_fReparsePoints = fReparsePoints = 0;
        }
        if (NT_SUCCESS(rcNt))
        {
            /* Query tag information first in order to try re-open non-symlink reparse points.
               Note! On NT4 the value later used for FileAttributeTagInformation was
                     called FileObjectIdInformation, but fortunately that could only
                     be set and will fail with STATUS_INVALID_INFO_CLASS when queried. */
            FILE_ATTRIBUTE_TAG_INFORMATION TagInfo;
            rcNt = NtQueryInformationFile(hFile, &Ios, &TagInfo, sizeof(TagInfo), FileAttributeTagInformation);
            if (!NT_SUCCESS(rcNt))
                TagInfo.FileAttributes = TagInfo.ReparseTag = 0;
            if (   !(TagInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                || TagInfo.ReparseTag == IO_REPARSE_TAG_SYMLINK
                || (fFlags & RTPATH_F_FOLLOW_LINK))
            { /* likely */ }
            else
            {
                /* Reparse point that isn't a symbolic link, try follow the reparsing. */
                HANDLE hFile2;
                RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
                rcNt = NtCreateFile(&hFile2,
                                    FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                    &ObjAttr,
                                    &Ios,
                                    NULL /*pcbFile*/,
                                    FILE_ATTRIBUTE_NORMAL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    FILE_OPEN,
                                    FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                    NULL /*pvEaBuffer*/,
                                    0 /*cbEa*/);
                if (NT_SUCCESS(rcNt))
                {
                    NtClose(hFile);
                    hFile = hFile2;
                    TagInfo.FileAttributes = TagInfo.ReparseTag = 0;
                }
            }

            /*
             * Get the information we need and convert it.
             */
            rc = rtPathNtQueryInfoFromHandle(hFile, &uBuf, sizeof(uBuf), pObjInfo, enmAddAttr, pszPath, TagInfo.ReparseTag);
            NtClose(hFile);
            if (RT_SUCCESS(rc))
                return rc;

            if (RT_FAILURE(rc))
                rc = VINF_TRY_AGAIN;
        }
        else if (   rcNt == STATUS_OBJECT_TYPE_MISMATCH
                 || rcNt == STATUS_OBJECT_NAME_INVALID
                 /*|| rcNt == STATUS_INVALID_PARAMETER*/)
        {
            rc = rtPathNtQueryInfoInDirectoryObject(&ObjAttr, pObjInfo, enmAddAttr, fFlags, &uBuf, sizeof(uBuf), rcNt);
            if (RT_SUCCESS(rc))
                return rc;
        }
        else if (   rcNt != STATUS_ACCESS_DENIED
                 && rcNt != STATUS_SHARING_VIOLATION)
            rc = RTErrConvertFromNtStatus(rcNt);
        else
            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
    }

    /*
     * Try the 3rd option if none of the other worked.
     * If none of the above worked, try open the directory and enumerate
     * the file we're after.  This
     */
    if (rc == VINF_TRY_AGAIN)
    {
        /* Split up the name into parent directory path and filename. */
        UNICODE_STRING NtDirName;
        UNICODE_STRING NtFilter;
        ntPathNtSplitName(pNtName, &NtDirName, &NtFilter, false /*fNoParentDirSlash*/);

        /* Try open the directory. */
        InitializeObjectAttributes(&ObjAttr, &NtDirName, OBJ_CASE_INSENSITIVE, hRootDir, NULL);
        rcNt = NtCreateFile(&hFile,
                            FILE_LIST_DIRECTORY | SYNCHRONIZE,
                            &ObjAttr,
                            &Ios,
                            NULL /*pcbFile*/,
                            FILE_ATTRIBUTE_NORMAL,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            FILE_OPEN,
                            FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                            NULL /*pvEaBuffer*/,
                            0 /*cbEa*/);
        if (NT_SUCCESS(rcNt))
        {
            FILE_INFORMATION_CLASS enmInfoClass;
            if (RT_MAKE_U64(RTNtCurrentPeb()->OSMinorVersion, RTNtCurrentPeb()->OSMajorVersion) > RT_MAKE_U64(0,5) /* > W2K */)
                enmInfoClass = FileIdBothDirectoryInformation; /* Introduced in XP, from I can tell. */
            else
                enmInfoClass = FileBothDirectoryInformation;
            rcNt = NtQueryDirectoryFile(hFile,
                                        NULL /* Event */,
                                        NULL /* ApcRoutine */,
                                        NULL /* ApcContext */,
                                        &Ios,
                                        &uBuf,
                                        RT_MIN(sizeof(uBuf), 0xfff0),
                                        enmInfoClass,
                                        TRUE /*ReturnSingleEntry */,
                                        &NtFilter,
                                        FALSE /*RestartScan */);
            if (NT_SUCCESS(rcNt))
            {
                pObjInfo->cbObject    = uBuf.Both.EndOfFile.QuadPart;
                pObjInfo->cbAllocated = uBuf.Both.AllocationSize.QuadPart;

                RTTimeSpecSetNtTime(&pObjInfo->BirthTime,         uBuf.Both.CreationTime.QuadPart);
                RTTimeSpecSetNtTime(&pObjInfo->AccessTime,        uBuf.Both.LastAccessTime.QuadPart);
                RTTimeSpecSetNtTime(&pObjInfo->ModificationTime,  uBuf.Both.LastWriteTime.QuadPart);
                RTTimeSpecSetNtTime(&pObjInfo->ChangeTime,        uBuf.Both.ChangeTime.QuadPart);

                pObjInfo->Attr.fMode  = rtFsModeFromDos((uBuf.Both.FileAttributes << RTFS_DOS_SHIFT) & RTFS_DOS_MASK_NT,
                                                        pszPath, strlen(pszPath), uBuf.Both.EaSize, 0);

                pObjInfo->Attr.enmAdditional = enmAddAttr;
                if (enmAddAttr == RTFSOBJATTRADD_UNIX)
                {
                    pObjInfo->Attr.u.Unix.uid             = ~0U;
                    pObjInfo->Attr.u.Unix.gid             = ~0U;
                    pObjInfo->Attr.u.Unix.cHardlinks      = 1;
                    pObjInfo->Attr.u.Unix.INodeIdDevice   = 0; /* below */
                    pObjInfo->Attr.u.Unix.INodeId         = enmInfoClass == FileIdBothDirectoryInformation
                                                          ? uBuf.BothId.FileId.QuadPart : 0;
                    pObjInfo->Attr.u.Unix.fFlags          = 0;
                    pObjInfo->Attr.u.Unix.GenerationId    = 0;
                    pObjInfo->Attr.u.Unix.Device          = 0;

                    /* Get the serial number. */
                    rcNt = NtQueryVolumeInformationFile(hFile, &Ios, &uBuf, RT_MIN(sizeof(uBuf), _2K),
                                                        FileFsVolumeInformation);
                    if (NT_SUCCESS(rcNt))
                        pObjInfo->Attr.u.Unix.INodeIdDevice = uBuf.VolInfo.VolumeSerialNumber;
                }

                rc = rtPathNtQueryInfoFillInDummyData(VINF_SUCCESS, pObjInfo, enmAddAttr);
            }
            else
                rc = RTErrConvertFromNtStatus(rcNt);

            NtClose(hFile);
        }
        /*
         * Quite possibly a object directory.
         */
        else if (   rcNt == STATUS_OBJECT_NAME_INVALID  /* with trailing slash */
                 || rcNt == STATUS_OBJECT_TYPE_MISMATCH /* without trailing slash */ )
        {
            InitializeObjectAttributes(&ObjAttr, pNtName, OBJ_CASE_INSENSITIVE, hRootDir, NULL);
            rc = rtPathNtQueryInfoInDirectoryObject(&ObjAttr, pObjInfo, enmAddAttr, fFlags, &uBuf, sizeof(uBuf), rcNt);
            if (RT_FAILURE(rc))
                rc = RTErrConvertFromNtStatus(rcNt);
        }
        else
            rc = RTErrConvertFromNtStatus(rcNt);
    }

    return rc;
}


RTR3DECL(int) RTPathQueryInfoEx(const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
    AssertMsgReturn(    enmAdditionalAttribs >= RTFSOBJATTRADD_NOTHING
                    &&  enmAdditionalAttribs <= RTFSOBJATTRADD_LAST,
                    ("Invalid enmAdditionalAttribs=%p\n", enmAdditionalAttribs),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);


    /*
     * Convert the input path and call common worker.
     */
    HANDLE         hRootDir;
    UNICODE_STRING NtName;
    int rc = RTNtPathFromWinUtf8(&NtName, &hRootDir, pszPath);
    if (RT_SUCCESS(rc))
    {
        rc = rtPathNtQueryInfoWorker(hRootDir, &NtName, pObjInfo, enmAdditionalAttribs, fFlags, pszPath);
        RTNtPathFree(&NtName, &hRootDir);
    }
    return rc;
}


RTR3DECL(int) RTPathQueryInfo(const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs)
{
    return RTPathQueryInfoEx(pszPath, pObjInfo, enmAdditionalAttribs, RTPATH_F_ON_LINK);
}

