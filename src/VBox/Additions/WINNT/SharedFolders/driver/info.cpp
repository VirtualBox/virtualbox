/* $Id: info.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Shared Folders FSD - Information Querying & Setting Routines.
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
#include "vbsf.h"
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/mem.h>

extern "C" NTSTATUS NTAPI RxSetEndOfFileInfo(PRX_CONTEXT, PIRP, PFCB, PFOBX);


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Macro for copying a SHFLSTRING file name into a FILE_DIRECTORY_INFORMATION structure. */
#define INIT_FILE_NAME(obj, str) \
    do { \
        ULONG cbLength = (str).u16Length; \
        (obj)->FileNameLength = cbLength; \
        RtlCopyMemory((obj)->FileName, &(str).String.ucs2[0], cbLength + 2); \
    } while (0)


NTSTATUS VBoxMRxQueryDirectory(IN OUT PRX_CONTEXT RxContext)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RxCaptureFobx;
    RxCaptureFcb;

    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_VBOX_FOBX pVBoxFobx = VBoxMRxGetFileObjectExtension(capFobx);

    PUNICODE_STRING DirectoryName = GET_ALREADY_PREFIXED_NAME_FROM_CONTEXT(RxContext);
    PUNICODE_STRING Template = &capFobx->UnicodeQueryTemplate;
    FILE_INFORMATION_CLASS FileInformationClass = RxContext->Info.FileInformationClass;
    PCHAR pInfoBuffer = (PCHAR)RxContext->Info.Buffer;
    LONG cbMaxSize = RxContext->Info.Length;
    LONG *pLengthRemaining = (LONG *)&RxContext->Info.LengthRemaining;

    LONG cbToCopy;
    int vrc;
    uint8_t *pHGCMBuffer;
    uint32_t index, fSFFlags, cFiles, u32BufSize;
    LONG cbHGCMBuffer;
    PSHFLDIRINFO pDirEntry;

    ULONG *pNextOffset = 0;
    PSHFLSTRING ParsedPath = NULL;

    Log(("VBOXSF: MrxQueryDirectory: FileInformationClass %d, pVBoxFobx %p, hFile %RX64, pInfoBuffer %p\n",
         FileInformationClass, pVBoxFobx, pVBoxFobx->hFile, pInfoBuffer));

    if (!pVBoxFobx)
    {
        Log(("VBOXSF: MrxQueryDirectory: pVBoxFobx is invalid!\n"));
        return STATUS_INVALID_PARAMETER;
    }

    if (!DirectoryName)
        return STATUS_INVALID_PARAMETER;

    if (DirectoryName->Length == 0)
        Log(("VBOXSF: MrxQueryDirectory: DirectoryName = \\ (null string)\n"));
    else
        Log(("VBOXSF: MrxQueryDirectory: DirectoryName = %.*ls\n",
             DirectoryName->Length / sizeof(WCHAR), DirectoryName->Buffer));

    if (!Template)
        return STATUS_INVALID_PARAMETER;

    if (Template->Length == 0)
        Log(("VBOXSF: MrxQueryDirectory: Template = \\ (null string)\n"));
    else
        Log(("VBOXSF: MrxQueryDirectory: Template = %.*ls\n",
             Template->Length / sizeof(WCHAR), Template->Buffer));

    cbHGCMBuffer = RT_MAX(cbMaxSize, PAGE_SIZE);

    Log(("VBOXSF: MrxQueryDirectory: Allocating cbHGCMBuffer = %d\n",
         cbHGCMBuffer));

    pHGCMBuffer = (uint8_t *)vbsfNtAllocNonPagedMem(cbHGCMBuffer);
    if (!pHGCMBuffer)
    {
        AssertFailed();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Assume start from the beginning. */
    index = 0;
    if (RxContext->QueryDirectory.IndexSpecified == TRUE)
    {
        Log(("VBOXSF: MrxQueryDirectory: Index specified %d\n",
             index));
        index = RxContext->QueryDirectory.FileIndex;
    }

    fSFFlags = SHFL_LIST_NONE;
    if (RxContext->QueryDirectory.ReturnSingleEntry == TRUE)
    {
        Log(("VBOXSF: MrxQueryDirectory: Query single entry\n"));
        fSFFlags |= SHFL_LIST_RETURN_ONE;
    }
    if (   RxContext->QueryDirectory.RestartScan == TRUE
        && RxContext->QueryDirectory.InitialQuery == FALSE)
    {
        Log(("VBOXSF: MrxQueryDirectory: Restart scan\n"));
        fSFFlags |= SHFL_LIST_RESTART;
    }

    if (Template->Length)
    {
        ULONG ParsedPathSize, cch;

        /* Calculate size required for parsed path: dir + \ + template + 0. */
        ParsedPathSize = SHFLSTRING_HEADER_SIZE + Template->Length + sizeof(WCHAR);
        if (DirectoryName->Length)
            ParsedPathSize += DirectoryName->Length + sizeof(WCHAR);
        Log(("VBOXSF: MrxQueryDirectory: ParsedPathSize = %d\n", ParsedPathSize));

        ParsedPath = (PSHFLSTRING)vbsfNtAllocNonPagedMem(ParsedPathSize);
        if (!ParsedPath)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto end;
        }

        if (!ShflStringInitBuffer(ParsedPath, ParsedPathSize))
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto end;
        }

        cch = 0;
        if (DirectoryName->Length)
        {
            /* Copy directory name into ParsedPath. */
            RtlCopyMemory(ParsedPath->String.ucs2, DirectoryName->Buffer, DirectoryName->Length);
            cch += DirectoryName->Length / sizeof(WCHAR);

            /* Add terminating backslash. */
            ParsedPath->String.ucs2[cch] = L'\\';
            cch++;
        }

        RtlCopyMemory (&ParsedPath->String.ucs2[cch], Template->Buffer, Template->Length);
        cch += Template->Length / sizeof(WCHAR);

        /* Add terminating nul. */
        ParsedPath->String.ucs2[cch] = 0;

        /* cch is the number of chars without trailing nul. */
        ParsedPath->u16Length = (uint16_t)(cch * sizeof(WCHAR));

        AssertMsg(ParsedPath->u16Length + sizeof(WCHAR) == ParsedPath->u16Size,
                  ("u16Length %d, u16Size %d\n", ParsedPath->u16Length, ParsedPath->u16Size));

        Log(("VBOXSF: MrxQueryDirectory: ParsedPath = %.*ls\n",
             ParsedPath->u16Length / sizeof(WCHAR), ParsedPath->String.ucs2));
    }

    cFiles = 0;

    /* VbglR0SfDirInfo requires a pointer to uint32_t. */
    u32BufSize = cbHGCMBuffer;

    Log(("VBOXSF: MrxQueryDirectory: CallDirInfo: File = 0x%08x, Flags = 0x%08x, Index = %d, u32BufSize = %d\n",
         pVBoxFobx->hFile, fSFFlags, index, u32BufSize));
    vrc = VbglR0SfDirInfo(&g_SfClient, &pNetRootExtension->map, pVBoxFobx->hFile,
                          ParsedPath, fSFFlags, index, &u32BufSize, (PSHFLDIRINFO)pHGCMBuffer, &cFiles);
    Log(("VBOXSF: MrxQueryDirectory: u32BufSize after CallDirInfo = %d, rc = %Rrc\n",
         u32BufSize, vrc));

    switch (vrc)
    {
        case VINF_SUCCESS:
            /* Nothing to do here. */
            break;

        case VERR_NO_TRANSLATION:
            Log(("VBOXSF: MrxQueryDirectory: Host could not translate entry!\n"));
            break;

        case VERR_NO_MORE_FILES:
            if (cFiles <= 0) /* VERR_NO_MORE_FILES appears at the first lookup when just returning the current dir ".".
                              * So we also have to check for the cFiles counter. */
            {
                /* Not an error, but we have to handle the return value. */
                Log(("VBOXSF: MrxQueryDirectory: Host reported no more files!\n"));

                if (RxContext->QueryDirectory.InitialQuery)
                {
                    /* First call. MSDN on FindFirstFile: "If the function fails because no matching files
                     * can be found, the GetLastError function returns ERROR_FILE_NOT_FOUND."
                     * So map this rc to file not found.
                     */
                    Status = STATUS_NO_SUCH_FILE;
                }
                else
                {
                    /* Search continued. */
                    Status = STATUS_NO_MORE_FILES;
                }
            }
            break;

        case VERR_FILE_NOT_FOUND:
            Status = STATUS_NO_SUCH_FILE;
            Log(("VBOXSF: MrxQueryDirectory: no such file!\n"));
            break;

        default:
            Status = vbsfNtVBoxStatusToNt(vrc);
            Log(("VBOXSF: MrxQueryDirectory: Error %Rrc from CallDirInfo (cFiles=%d)!\n",
                 vrc, cFiles));
            break;
    }

    if (Status != STATUS_SUCCESS)
        goto end;

    /* Verify that the returned buffer length is not greater than the original one. */
    if (u32BufSize > (uint32_t)cbHGCMBuffer)
    {
        Log(("VBOXSF: MrxQueryDirectory: returned buffer size (%u) is invalid!!!\n",
             u32BufSize));
        Status = STATUS_INVALID_NETWORK_RESPONSE;
        goto end;
    }

    /* How many bytes remain in the buffer. */
    cbHGCMBuffer = u32BufSize;

    pDirEntry = (PSHFLDIRINFO)pHGCMBuffer;
    Status = STATUS_SUCCESS;

    Log(("VBOXSF: MrxQueryDirectory: cFiles=%d, Length=%d\n",
         cFiles, cbHGCMBuffer));

    while ((*pLengthRemaining) && (cFiles > 0) && (pDirEntry != NULL))
    {
        int cbEntry = RT_UOFFSETOF(SHFLDIRINFO, name.String) + pDirEntry->name.u16Size;

        if (cbEntry > cbHGCMBuffer)
        {
            Log(("VBOXSF: MrxQueryDirectory: Entry size (%d) exceeds the buffer size (%d)!!!\n",
                 cbEntry, cbHGCMBuffer));
            Status = STATUS_INVALID_NETWORK_RESPONSE;
            goto end;
        }

        switch (FileInformationClass)
        {
            case FileDirectoryInformation:
            {
                PFILE_DIRECTORY_INFORMATION pInfo = (PFILE_DIRECTORY_INFORMATION)pInfoBuffer;
                Log(("VBOXSF: MrxQueryDirectory: FileDirectoryInformation\n"));

                cbToCopy = sizeof(FILE_DIRECTORY_INFORMATION);
                /* Struct already contains one char for null terminator. */
                cbToCopy += pDirEntry->name.u16Size;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pDirEntry->Info.BirthTime); /* ridiculous name */
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pDirEntry->Info.AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pDirEntry->Info.ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pDirEntry->Info.ChangeTime);
                    pInfo->AllocationSize.QuadPart = pDirEntry->Info.cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pDirEntry->Info.cbObject;
                    pInfo->FileIndex               = index;
                    pInfo->FileAttributes          = VBoxToNTFileAttributes(pDirEntry->Info.Attr.fMode);

                    INIT_FILE_NAME(pInfo, pDirEntry->name);

                    /* Align to 8 byte boundary */
                    cbToCopy = RT_ALIGN(cbToCopy, sizeof(LONGLONG));
                    pInfo->NextEntryOffset = cbToCopy;
                    pNextOffset = &pInfo->NextEntryOffset;
                }
                else
                {
                    pInfo->NextEntryOffset = 0; /* last item */
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                break;
            }

            case FileFullDirectoryInformation:
            {
                PFILE_FULL_DIR_INFORMATION pInfo = (PFILE_FULL_DIR_INFORMATION)pInfoBuffer;
                Log(("VBOXSF: MrxQueryDirectory: FileFullDirectoryInformation\n"));

                cbToCopy = sizeof(FILE_FULL_DIR_INFORMATION);
                /* Struct already contains one char for null terminator. */
                cbToCopy += pDirEntry->name.u16Size;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pDirEntry->Info.BirthTime); /* ridiculous name */
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pDirEntry->Info.AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pDirEntry->Info.ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pDirEntry->Info.ChangeTime);
                    pInfo->AllocationSize.QuadPart = pDirEntry->Info.cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pDirEntry->Info.cbObject;
                    pInfo->EaSize                  = 0;
                    pInfo->FileIndex               = index;
                    pInfo->FileAttributes          = VBoxToNTFileAttributes(pDirEntry->Info.Attr.fMode);

                    INIT_FILE_NAME(pInfo, pDirEntry->name);

                    /* Align to 8 byte boundary */
                    cbToCopy = RT_ALIGN(cbToCopy, sizeof(LONGLONG));
                    pInfo->NextEntryOffset = cbToCopy;
                    pNextOffset = &pInfo->NextEntryOffset;
                }
                else
                {
                    pInfo->NextEntryOffset = 0; /* last item */
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                break;
            }

            case FileBothDirectoryInformation:
            {
                PFILE_BOTH_DIR_INFORMATION pInfo = (PFILE_BOTH_DIR_INFORMATION)pInfoBuffer;
                Log(("VBOXSF: MrxQueryDirectory: FileBothDirectoryInformation\n"));

                cbToCopy = sizeof(FILE_BOTH_DIR_INFORMATION);
                /* struct already contains one char for null terminator */
                cbToCopy += pDirEntry->name.u16Size;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pDirEntry->Info.BirthTime); /* ridiculous name */
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pDirEntry->Info.AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pDirEntry->Info.ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pDirEntry->Info.ChangeTime);
                    pInfo->AllocationSize.QuadPart = pDirEntry->Info.cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pDirEntry->Info.cbObject;
                    pInfo->EaSize                  = 0;
                    pInfo->ShortNameLength         = 0; /** @todo ? */
                    pInfo->FileIndex               = index;
                    pInfo->FileAttributes          = VBoxToNTFileAttributes(pDirEntry->Info.Attr.fMode);

                    INIT_FILE_NAME(pInfo, pDirEntry->name);

                    Log(("VBOXSF: MrxQueryDirectory: FileBothDirectoryInformation cbAlloc = %x cbObject = %x\n",
                         pDirEntry->Info.cbAllocated, pDirEntry->Info.cbObject));
                    Log(("VBOXSF: MrxQueryDirectory: FileBothDirectoryInformation cbToCopy = %d, name size=%d name len=%d\n",
                         cbToCopy, pDirEntry->name.u16Size, pDirEntry->name.u16Length));
                    Log(("VBOXSF: MrxQueryDirectory: FileBothDirectoryInformation File name %.*ls (DirInfo)\n",
                         pInfo->FileNameLength / sizeof(WCHAR), pInfo->FileName));
                    Log(("VBOXSF: MrxQueryDirectory: FileBothDirectoryInformation File name %.*ls (DirEntry)\n",
                         pDirEntry->name.u16Size / sizeof(WCHAR), pDirEntry->name.String.ucs2));

                    /* Align to 8 byte boundary. */
                    cbToCopy = RT_ALIGN(cbToCopy, sizeof(LONGLONG));
                    pInfo->NextEntryOffset = cbToCopy;
                    pNextOffset = &pInfo->NextEntryOffset;
                }
                else
                {
                    pInfo->NextEntryOffset = 0; /* Last item. */
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                break;
            }

            case FileIdBothDirectoryInformation:
            {
                PFILE_ID_BOTH_DIR_INFORMATION pInfo = (PFILE_ID_BOTH_DIR_INFORMATION)pInfoBuffer;
                Log(("VBOXSF: MrxQueryDirectory: FileIdBothDirectoryInformation\n"));

                cbToCopy = sizeof(FILE_ID_BOTH_DIR_INFORMATION);
                /* struct already contains one char for null terminator */
                cbToCopy += pDirEntry->name.u16Size;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pDirEntry->Info.BirthTime); /* ridiculous name */
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pDirEntry->Info.AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pDirEntry->Info.ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pDirEntry->Info.ChangeTime);
                    pInfo->AllocationSize.QuadPart = pDirEntry->Info.cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pDirEntry->Info.cbObject;
                    pInfo->EaSize                  = 0;
                    pInfo->ShortNameLength         = 0; /** @todo ? */
                    pInfo->EaSize                  = 0;
                    pInfo->FileId.QuadPart         = 0;
                    pInfo->FileAttributes          = VBoxToNTFileAttributes(pDirEntry->Info.Attr.fMode);

                    INIT_FILE_NAME(pInfo, pDirEntry->name);

                    Log(("VBOXSF: MrxQueryDirectory: FileIdBothDirectoryInformation cbAlloc = 0x%RX64 cbObject = 0x%RX64\n",
                         pDirEntry->Info.cbAllocated, pDirEntry->Info.cbObject));
                    Log(("VBOXSF: MrxQueryDirectory: FileIdBothDirectoryInformation cbToCopy = %d, name size=%d name len=%d\n",
                         cbToCopy, pDirEntry->name.u16Size, pDirEntry->name.u16Length));
                    Log(("VBOXSF: MrxQueryDirectory: FileIdBothDirectoryInformation File name %.*ls (DirInfo)\n",
                         pInfo->FileNameLength / sizeof(WCHAR), pInfo->FileName));
                    Log(("VBOXSF: MrxQueryDirectory: FileIdBothDirectoryInformation File name %.*ls (DirEntry)\n",
                         pDirEntry->name.u16Size / sizeof(WCHAR), pDirEntry->name.String.ucs2));

                    /* Align to 8 byte boundary. */
                    cbToCopy = RT_ALIGN(cbToCopy, sizeof(LONGLONG));
                    pInfo->NextEntryOffset = cbToCopy;
                    pNextOffset = &pInfo->NextEntryOffset;
                }
                else
                {
                    pInfo->NextEntryOffset = 0; /* Last item. */
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                break;
            }

            case FileNamesInformation:
            {
                PFILE_NAMES_INFORMATION pInfo = (PFILE_NAMES_INFORMATION)pInfoBuffer;
                Log(("VBOXSF: MrxQueryDirectory: FileNamesInformation\n"));

                cbToCopy = sizeof(FILE_NAMES_INFORMATION);
                /* Struct already contains one char for null terminator. */
                cbToCopy += pDirEntry->name.u16Size;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->FileIndex = index;

                    INIT_FILE_NAME(pInfo, pDirEntry->name);

                    Log(("VBOXSF: MrxQueryDirectory: FileNamesInformation: File name [%.*ls]\n",
                         pInfo->FileNameLength / sizeof(WCHAR), pInfo->FileName));

                    /* Align to 8 byte boundary. */
                    cbToCopy = RT_ALIGN(cbToCopy, sizeof(LONGLONG));
                    pInfo->NextEntryOffset = cbToCopy;
                    pNextOffset = &pInfo->NextEntryOffset;
                }
                else
                {
                    pInfo->NextEntryOffset = 0; /* Last item. */
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                break;
            }

            default:
                Log(("VBOXSF: MrxQueryDirectory: Not supported FileInformationClass %d!\n",
                     FileInformationClass));
                Status = STATUS_INVALID_PARAMETER;
                goto end;
        }

        cbHGCMBuffer -= cbEntry;
        pDirEntry = (PSHFLDIRINFO)((uintptr_t)pDirEntry + cbEntry);

        Log(("VBOXSF: MrxQueryDirectory: %d bytes left in HGCM buffer\n",
             cbHGCMBuffer));

        if (*pLengthRemaining >= cbToCopy)
        {
            pInfoBuffer += cbToCopy;
            *pLengthRemaining -= cbToCopy;
        }
        else
            break;

        if (RxContext->QueryDirectory.ReturnSingleEntry)
            break;

        /* More left? */
        if (cbHGCMBuffer <= 0)
            break;

        index++; /* File Index. */

        cFiles--;
    }

    if (pNextOffset)
        *pNextOffset = 0; /* Last pInfo->NextEntryOffset should be set to zero! */

end:
    if (pHGCMBuffer)
        vbsfNtFreeNonPagedMem(pHGCMBuffer);

    if (ParsedPath)
        vbsfNtFreeNonPagedMem(ParsedPath);

    Log(("VBOXSF: MrxQueryDirectory: Returned 0x%08X\n",
         Status));
    return Status;
}


/*********************************************************************************************************************************
*   NtQueryVolumeInformationFile                                                                                                 *
*********************************************************************************************************************************/

/**
 * Updates VBSFNTFCBEXT::VolInfo.
 *
 * Currently no kind of FCB lock is normally held.
 */
static NTSTATUS vbsfNtUpdateFcbVolInfo(PVBSFNTFCBEXT pVBoxFcbX, PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension,
                                       PMRX_VBOX_FOBX pVBoxFobx)
{
    NTSTATUS          rcNt;
    VBOXSFVOLINFOREQ *pReq = (VBOXSFVOLINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
    if (pReq)
    {
        int vrc = VbglR0SfHostReqQueryVolInfo(pNetRootExtension->map.root, pReq, pVBoxFobx->hFile);
        if (RT_SUCCESS(vrc))
        {
            /* Make the units compatible with NT before assigning. */
            if (pReq->VolInfo.ulBytesPerSector != 0)
            {
                if (pReq->VolInfo.ulBytesPerAllocationUnit > pReq->VolInfo.ulBytesPerSector)
                {
                    uint32_t cSectorsPerUnit = pReq->VolInfo.ulBytesPerAllocationUnit / pReq->VolInfo.ulBytesPerSector;
                    pReq->VolInfo.ulBytesPerAllocationUnit = pReq->VolInfo.ulBytesPerSector * cSectorsPerUnit;
                }
                else if (pReq->VolInfo.ulBytesPerAllocationUnit < pReq->VolInfo.ulBytesPerSector)
                    pReq->VolInfo.ulBytesPerAllocationUnit = pReq->VolInfo.ulBytesPerSector;
            }
            else if (pReq->VolInfo.ulBytesPerAllocationUnit == 0)
                pReq->VolInfo.ulBytesPerSector = pReq->VolInfo.ulBytesPerAllocationUnit = 512;
            else
                pReq->VolInfo.ulBytesPerSector = pReq->VolInfo.ulBytesPerAllocationUnit;

            /* Copy the info assigning: */
            ASMCompilerBarrier();
            pVBoxFcbX->VolInfo.ullTotalAllocationBytes       = pReq->VolInfo.ullTotalAllocationBytes;
            pVBoxFcbX->VolInfo.ullAvailableAllocationBytes   = pReq->VolInfo.ullAvailableAllocationBytes;
            pVBoxFcbX->VolInfo.ulBytesPerAllocationUnit      = pReq->VolInfo.ulBytesPerAllocationUnit;
            pVBoxFcbX->VolInfo.ulBytesPerSector              = pReq->VolInfo.ulBytesPerSector;
            pVBoxFcbX->VolInfo.ulSerial                      = pReq->VolInfo.ulSerial;
            pVBoxFcbX->VolInfo.fsProperties.cbMaxComponent   = pReq->VolInfo.fsProperties.cbMaxComponent;
            pVBoxFcbX->VolInfo.fsProperties.fRemote          = pReq->VolInfo.fsProperties.fRemote;
            pVBoxFcbX->VolInfo.fsProperties.fCaseSensitive   = pReq->VolInfo.fsProperties.fCaseSensitive;
            pVBoxFcbX->VolInfo.fsProperties.fReadOnly        = pReq->VolInfo.fsProperties.fReadOnly;
            /** @todo Use SHFL_FN_QUERY_MAP_INFO to get the correct read-only status of
             *        the share. */
            pVBoxFcbX->VolInfo.fsProperties.fSupportsUnicode = pReq->VolInfo.fsProperties.fSupportsUnicode;
            pVBoxFcbX->VolInfo.fsProperties.fCompressed      = pReq->VolInfo.fsProperties.fCompressed;
            pVBoxFcbX->VolInfo.fsProperties.fFileCompression = pReq->VolInfo.fsProperties.fFileCompression;
            ASMWriteFence();
            pVBoxFcbX->nsVolInfoUpToDate = RTTimeSystemNanoTS();
            ASMWriteFence();

            rcNt = STATUS_SUCCESS;
        }
        else
            rcNt = vbsfNtVBoxStatusToNt(vrc);
        VbglR0PhysHeapFree(pReq);
    }
    else
        rcNt = STATUS_INSUFFICIENT_RESOURCES;
    return rcNt;
}

/**
 * Handles NtQueryVolumeInformationFile / FileFsVolumeInformation
 */
static NTSTATUS vbsfNtQueryFsVolumeInfo(IN OUT PRX_CONTEXT pRxContext,
                                        PFILE_FS_VOLUME_INFORMATION pInfo,
                                        ULONG cbInfo,
                                        PMRX_NET_ROOT pNetRoot,
                                        PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension,
                                        PMRX_VBOX_FOBX pVBoxFobx,
                                        PVBSFNTFCBEXT pVBoxFcbX)
{
    /*
     * NtQueryVolumeInformationFile should've checked the minimum buffer size
     * but just in case.
     */
    AssertReturnStmt(cbInfo >= RT_UOFFSETOF(FILE_FS_VOLUME_INFORMATION, VolumeLabel),
                     pRxContext->InformationToReturn = RT_UOFFSETOF(FILE_FS_VOLUME_INFORMATION, VolumeLabel),
                     STATUS_BUFFER_TOO_SMALL);

    /*
     * Get up-to-date serial number.
     *
     * If we have a unixy host, we'll get additional unix attributes and the
     * serial number is the same as INodeIdDevice.
     *
     * Note! Because it's possible that the host has mount points within the
     *       shared folder as well as symbolic links pointing out files or
     *       directories outside the tree, we cannot just cache the serial
     *       number in the net root extension data and skip querying it here.
     *
     *       OTOH, only we don't report inode info from the host, so the only
     *       thing the serial number can be used for is to cache/whatever
     *       volume space information.  So, we should probably provide a
     *       shortcut here via mount option, registry and guest properties.
     */
    /** @todo Make See OTOH above wrt. one serial per net root.   */
    uint64_t nsNow = RTTimeSystemNanoTS();
    if (   pVBoxFobx->Info.Attr.enmAdditional == SHFLFSOBJATTRADD_UNIX
        && pVBoxFobx->Info.Attr.u.Unix.INodeIdDevice != 0
        && pVBoxFobx->nsUpToDate - nsNow < RT_NS_100US /** @todo implement proper TTL */)
        pInfo->VolumeSerialNumber = pVBoxFobx->Info.Attr.u.Unix.INodeIdDevice;
    else if (pVBoxFcbX->nsVolInfoUpToDate - nsNow < RT_NS_100MS /** @todo implement proper volume info TTL */ )
        pInfo->VolumeSerialNumber = pVBoxFcbX->VolInfo.ulSerial;
    else
    {
        /* Must fetch the info. */
        NTSTATUS Status = vbsfNtUpdateFcbVolInfo(pVBoxFcbX, pNetRootExtension, pVBoxFobx);
        if (NT_SUCCESS(Status))
            pInfo->VolumeSerialNumber = pVBoxFcbX->VolInfo.ulSerial;
        else
            return Status;
    }
    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsVolumeInformation: VolumeSerialNumber=%#010RX32\n", pInfo->VolumeSerialNumber));

    /*
     * Fill in the static info.
     */
    pInfo->VolumeCreationTime.QuadPart  = 0;
    pInfo->SupportsObjects              = FALSE;

    /*
     * The volume label.
     *
     * We may get queries with insufficient buffer space for the whole (or any)
     * volume label.  In those cases we're to return STATUS_BUFFER_OVERFLOW,
     * return the returned number of bytes in Ios.Information and set the
     * VolumeLabelLength to the actual length (rather than the returned).  At
     * least this is was FAT and NTFS does (however, it is not what the NulMrx
     * sample from the 6.1.6001.18002 does).
     *
     * Note! VolumeLabelLength is a byte count.
     * Note! NTFS does not include a terminator, so neither do we.
     */
    uint32_t const cbShareName  = pNetRoot->pNetRootName->Length
                                - pNetRoot->pSrvCall->pSrvCallName->Length
                                - sizeof(WCHAR) /* Remove the leading backslash. */;
    uint32_t const cbVolLabel   = VBOX_VOLNAME_PREFIX_SIZE + cbShareName;
    pInfo->VolumeLabelLength    = cbVolLabel;

    WCHAR const   *pwcShareName = &pNetRoot->pNetRootName->Buffer[pNetRoot->pSrvCall->pSrvCallName->Length / sizeof(WCHAR) + 1];
    uint32_t       cbCopied     = RT_UOFFSETOF(FILE_FS_VOLUME_INFORMATION, VolumeLabel);
    NTSTATUS       Status;
    if (cbInfo >= cbCopied + cbVolLabel)
    {
        memcpy(pInfo->VolumeLabel, VBOX_VOLNAME_PREFIX, VBOX_VOLNAME_PREFIX_SIZE);
        memcpy(&pInfo->VolumeLabel[VBOX_VOLNAME_PREFIX_SIZE / sizeof(WCHAR)], pwcShareName, cbShareName);
        cbCopied += cbVolLabel;
        Status = STATUS_SUCCESS;
        Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsVolumeInformation: full result (%#x)\n", cbCopied));
    }
    else
    {
        if (cbInfo > cbCopied)
        {
            uint32_t cbLeft = cbInfo - cbCopied;
            memcpy(pInfo->VolumeLabel, VBOX_VOLNAME_PREFIX, RT_MIN(cbLeft, VBOX_VOLNAME_PREFIX_SIZE));
            if (cbLeft > VBOX_VOLNAME_PREFIX_SIZE)
            {
                cbLeft -= VBOX_VOLNAME_PREFIX_SIZE;
                memcpy(&pInfo->VolumeLabel[VBOX_VOLNAME_PREFIX_SIZE / sizeof(WCHAR)], pwcShareName, RT_MIN(cbLeft, cbShareName));
            }
            Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsVolumeInformation: partial result (%#x, needed %#x)\n",
                 cbCopied, cbCopied + cbVolLabel));
            cbCopied = cbInfo;
        }
        else
            Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsVolumeInformation: partial result no label (%#x, needed %#x)\n",
                 cbCopied, cbCopied + cbVolLabel));
        Status = STATUS_BUFFER_OVERFLOW;
    }

    /*
     * Update the return length in the context.
     */
    pRxContext->Info.LengthRemaining = cbInfo - cbCopied;
    pRxContext->InformationToReturn  = cbCopied;

    return Status;
}

/**
 * Handles NtQueryVolumeInformationFile / FileFsSizeInformation
 *
 * @note Almost identical to vbsfNtQueryFsFullSizeInfo.
 */
static NTSTATUS vbsfNtQueryFsSizeInfo(IN OUT PRX_CONTEXT pRxContext,
                                      PFILE_FS_SIZE_INFORMATION pInfo,
                                      ULONG cbInfo,
                                      PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension,
                                      PMRX_VBOX_FOBX pVBoxFobx,
                                      PVBSFNTFCBEXT pVBoxFcbX)
{
    /*
     * NtQueryVolumeInformationFile should've checked the buffer size but just in case.
     */
    AssertReturnStmt(cbInfo >= sizeof(*pInfo), pRxContext->InformationToReturn = sizeof(*pInfo), STATUS_BUFFER_TOO_SMALL);

    /*
     * Get up-to-date information.
     * For the time being we always re-query this information from the host.
     */
    /** @todo don't requery this if it happens with XXXX ns of a _different_ info
     *        request to the same handle. */
    {
        /* Must fetch the info. */
        NTSTATUS Status = vbsfNtUpdateFcbVolInfo(pVBoxFcbX, pNetRootExtension, pVBoxFobx);
        if (NT_SUCCESS(Status))
        { /* likely */ }
        else
            return Status;
    }

    /* Make a copy of the info for paranoid reasons: */
    SHFLVOLINFO VolInfoCopy;
    memcpy(&VolInfoCopy, (void *)&pVBoxFcbX->VolInfo, sizeof(VolInfoCopy));
    ASMCompilerBarrier();

    /*
     * Produce the requested data.
     */
    pInfo->BytesPerSector                    = RT_MIN(VolInfoCopy.ulBytesPerSector, 1);
    pInfo->SectorsPerAllocationUnit          = VolInfoCopy.ulBytesPerAllocationUnit / pInfo->BytesPerSector;
    AssertReturn(pInfo->SectorsPerAllocationUnit > 0, STATUS_INTERNAL_ERROR);
    pInfo->TotalAllocationUnits.QuadPart     = pVBoxFcbX->VolInfo.ullTotalAllocationBytes
                                             / VolInfoCopy.ulBytesPerAllocationUnit;
    pInfo->AvailableAllocationUnits.QuadPart = pVBoxFcbX->VolInfo.ullAvailableAllocationBytes
                                             / VolInfoCopy.ulBytesPerAllocationUnit;

    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsSizeInformation: BytesPerSector           = %#010RX32\n",
         pInfo->BytesPerSector));
    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsSizeInformation: SectorsPerAllocationUnit = %#010RX32\n",
         pInfo->SectorsPerAllocationUnit));
    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsSizeInformation: TotalAllocationUnits     = %#018RX32\n",
         pInfo->TotalAllocationUnits.QuadPart));
    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsSizeInformation: AvailableAllocationUnits = %#018RX32\n",
         pInfo->AvailableAllocationUnits.QuadPart));

    /*
     * Update the return length in the context.
     */
    pRxContext->Info.LengthRemaining = cbInfo - sizeof(*pInfo);
    pRxContext->InformationToReturn  = sizeof(*pInfo);
    return STATUS_SUCCESS;
}

/**
 * Handles NtQueryVolumeInformationFile / FileFsFullSizeInformation
 *
 * @note Almost identical to vbsfNtQueryFsSizeInfo.
 */
static NTSTATUS vbsfNtQueryFsFullSizeInfo(IN OUT PRX_CONTEXT pRxContext,
                                          PFILE_FS_FULL_SIZE_INFORMATION pInfo,
                                          ULONG cbInfo,
                                          PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension,
                                          PMRX_VBOX_FOBX pVBoxFobx,
                                          PVBSFNTFCBEXT pVBoxFcbX)
{
    /*
     * NtQueryVolumeInformationFile should've checked the buffer size but just in case.
     */
    AssertReturnStmt(cbInfo >= sizeof(*pInfo), pRxContext->InformationToReturn = sizeof(*pInfo), STATUS_BUFFER_TOO_SMALL);

    /*
     * Get up-to-date information.
     * For the time being we always re-query this information from the host.
     */
    /** @todo don't requery this if it happens with XXXX ns of a _different_ info
     *        request to the same handle. */
    {
        /* Must fetch the info. */
        NTSTATUS Status = vbsfNtUpdateFcbVolInfo(pVBoxFcbX, pNetRootExtension, pVBoxFobx);
        if (NT_SUCCESS(Status))
        { /* likely */ }
        else
            return Status;
    }

    /* Make a copy of the info for paranoid reasons: */
    SHFLVOLINFO VolInfoCopy;
    memcpy(&VolInfoCopy, (void *)&pVBoxFcbX->VolInfo, sizeof(VolInfoCopy));
    ASMCompilerBarrier();

    /*
     * Produce the requested data.
     */
    pInfo->BytesPerSector                          = RT_MIN(VolInfoCopy.ulBytesPerSector, 1);
    pInfo->SectorsPerAllocationUnit                = VolInfoCopy.ulBytesPerAllocationUnit / pInfo->BytesPerSector;
    AssertReturn(pInfo->SectorsPerAllocationUnit > 0, STATUS_INTERNAL_ERROR);
    pInfo->TotalAllocationUnits.QuadPart           = pVBoxFcbX->VolInfo.ullTotalAllocationBytes
                                                   / VolInfoCopy.ulBytesPerAllocationUnit;
    pInfo->ActualAvailableAllocationUnits.QuadPart = pVBoxFcbX->VolInfo.ullAvailableAllocationBytes
                                                   / VolInfoCopy.ulBytesPerAllocationUnit;
    pInfo->CallerAvailableAllocationUnits.QuadPart = pInfo->ActualAvailableAllocationUnits.QuadPart;

    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsFullSizeInformation: BytesPerSector                 = %#010RX32\n",
         pInfo->BytesPerSector));
    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsFullSizeInformation: SectorsPerAllocationUnit       = %#010RX32\n",
         pInfo->SectorsPerAllocationUnit));
    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsFullSizeInformation: TotalAllocationUnits           = %#018RX32\n",
         pInfo->TotalAllocationUnits.QuadPart));
    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsFullSizeInformation: ActualAvailableAllocationUnits = %#018RX32\n",
         pInfo->ActualAvailableAllocationUnits.QuadPart));
    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsFullSizeInformation: CallerAvailableAllocationUnits = %#018RX32\n",
         pInfo->CallerAvailableAllocationUnits.QuadPart));

    /*
     * Update the return length in the context.
     */
    pRxContext->Info.LengthRemaining = cbInfo - sizeof(*pInfo);
    pRxContext->InformationToReturn  = sizeof(*pInfo);
    return STATUS_SUCCESS;
}

/**
 * Handles NtQueryVolumeInformationFile / FileFsDeviceInformation
 */
static NTSTATUS vbsfNtQueryFsDeviceInfo(IN OUT PRX_CONTEXT pRxContext,
                                        PFILE_FS_DEVICE_INFORMATION pInfo,
                                        ULONG cbInfo,
                                        PMRX_NET_ROOT pNetRoot)
{
    /*
     * NtQueryVolumeInformationFile should've checked the buffer size but just in case.
     */
    AssertReturnStmt(cbInfo >= sizeof(*pInfo), pRxContext->InformationToReturn = sizeof(*pInfo), STATUS_BUFFER_TOO_SMALL);

    /*
     * Produce the requested data.
     */
    pInfo->DeviceType      = pNetRoot->DeviceType;
    pInfo->Characteristics = FILE_REMOTE_DEVICE;

    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsFullSizeInformation: DeviceType = %#x\n", pInfo->DeviceType));
    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsFullSizeInformation: Characteristics = %#x (FILE_REMOTE_DEVICE)\n", FILE_REMOTE_DEVICE));

    /*
     * Update the return length in the context.
     */
    pRxContext->Info.LengthRemaining = cbInfo - sizeof(*pInfo);
    pRxContext->InformationToReturn  = sizeof(*pInfo);
    return STATUS_SUCCESS;
}

/**
 * Handles NtQueryVolumeInformationFile / FileFsDeviceInformation
 */
static NTSTATUS vbsfNtQueryFsAttributeInfo(IN OUT PRX_CONTEXT pRxContext,
                                           PFILE_FS_ATTRIBUTE_INFORMATION pInfo,
                                           ULONG cbInfo,
                                           PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension,
                                           PMRX_VBOX_FOBX pVBoxFobx,
                                           PVBSFNTFCBEXT pVBoxFcbX)
{
    static WCHAR const s_wszFsName[] = MRX_VBOX_FILESYS_NAME_U;
    static ULONG const s_cbFsName    = sizeof(s_wszFsName) - sizeof(s_wszFsName[0]);
    ULONG const        cbNeeded      = RT_UOFFSETOF(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName) + s_cbFsName;

    /*
     * NtQueryVolumeInformationFile should've checked the buffer size but just in case.
     */
    AssertReturnStmt(cbInfo >= RT_UOFFSETOF(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName),
                     pRxContext->InformationToReturn = cbNeeded,
                     STATUS_BUFFER_TOO_SMALL);

    /*
     * Get up-to-date information about filename length and such.
     */
    if (pVBoxFcbX->nsVolInfoUpToDate - RTTimeSystemNanoTS() < RT_NS_100MS /** @todo implement proper volume info TTL */ )
    {
        /* Must fetch the info. */
        NTSTATUS Status = vbsfNtUpdateFcbVolInfo(pVBoxFcbX, pNetRootExtension, pVBoxFobx);
        if (NT_SUCCESS(Status))
        { /* likely */ }
        else
            return Status;
    }

    /*
     * Produce the requested data.
     *
     * Note! The MaximumComponentNameLength is documented (1) to be in bytes, but
     *       NTFS and FAT32 both return 255, indicating that it is really a UTF-16 char count.
     *
     * Note! Both NTFS and FAT32 seems to be setting Ios.Information and FileSystemNameLength
     *       the number of bytes returned in the STATUS_BUFFER_OVERFLOW case, making it
     *       impossible to guess the length from the returned data.  RDR2 forwards information
     *       from the server, and samba returns a fixed FileSystemNameLength.
     *
     * (1) https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/ntifs/ns-ntifs-_file_fs_attribute_information
     */
    pInfo->FileSystemAttributes         = FILE_CASE_PRESERVED_NAMES;
    /** @todo Implement FILE_RETURNS_CLEANUP_RESULT_INFO. */
    if (pVBoxFcbX->VolInfo.fsProperties.fSupportsUnicode)
        pInfo->FileSystemAttributes    |= FILE_UNICODE_ON_DISK;
    if (pVBoxFcbX->VolInfo.fsProperties.fReadOnly)
        pInfo->FileSystemAttributes    |= FILE_READ_ONLY_VOLUME;
    if (pVBoxFcbX->VolInfo.fsProperties.fFileCompression)
        pInfo->FileSystemAttributes    |= FILE_FILE_COMPRESSION;
    else if (pVBoxFcbX->VolInfo.fsProperties.fCompressed)
        pInfo->FileSystemAttributes    |= FILE_VOLUME_IS_COMPRESSED;
    pInfo->MaximumComponentNameLength   = pVBoxFcbX->VolInfo.fsProperties.cbMaxComponent
                                        ? pVBoxFcbX->VolInfo.fsProperties.cbMaxComponent : 255;
    ULONG const cbStrCopied = RT_MIN(cbInfo - RT_UOFFSETOF(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName), s_cbFsName);
    pInfo->FileSystemNameLength         = s_cbFsName;
    if (cbStrCopied > 0)
        memcpy(pInfo->FileSystemName, MRX_VBOX_FILESYS_NAME_U, cbStrCopied);

    /*
     * Update the return length in the context.
     */
    pRxContext->Info.LengthRemaining = cbInfo - cbStrCopied - RT_UOFFSETOF(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName);
    pRxContext->InformationToReturn  = cbStrCopied + RT_UOFFSETOF(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName);
    return cbInfo >= cbNeeded ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
}

/**
 * Handles NtQueryVolumeInformationFile / FileFsSectorSizeInformation
 */
static NTSTATUS vbsfNtQueryFsSectorSizeInfo(IN OUT PRX_CONTEXT pRxContext,
                                            PFILE_FS_SECTOR_SIZE_INFORMATION pInfo,
                                            ULONG cbInfo,
                                            PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension,
                                            PMRX_VBOX_FOBX pVBoxFobx,
                                            PVBSFNTFCBEXT pVBoxFcbX)
{
    /*
     * NtQueryVolumeInformationFile should've checked the buffer size but just in case.
     */
    AssertReturnStmt(cbInfo >= sizeof(*pInfo), pRxContext->InformationToReturn = sizeof(*pInfo), STATUS_BUFFER_TOO_SMALL);

    /*
     * Get up-to-date sector size info.
     */
    if (pVBoxFcbX->nsVolInfoUpToDate - RTTimeSystemNanoTS() < RT_NS_100MS /** @todo implement proper volume info TTL */ )
    {
        /* Must fetch the info. */
        NTSTATUS Status = vbsfNtUpdateFcbVolInfo(pVBoxFcbX, pNetRootExtension, pVBoxFobx);
        if (NT_SUCCESS(Status))
        { /* likely */ }
        else
            return Status;
    }

    /*
     * Produce the requested data (currently no way to query more than the
     * basic sector size here, so just repeat it).
     */
    uint32_t const cbSector = pVBoxFcbX->VolInfo.ulBytesPerSector ? pVBoxFcbX->VolInfo.ulBytesPerSector : 512;
    pInfo->LogicalBytesPerSector                                 = cbSector;
    pInfo->PhysicalBytesPerSectorForAtomicity                    = cbSector;
    pInfo->PhysicalBytesPerSectorForPerformance                  = cbSector;
    pInfo->FileSystemEffectivePhysicalBytesPerSectorForAtomicity = cbSector;
    pInfo->Flags                                                 = 0;
    pInfo->ByteOffsetForSectorAlignment                          = SSINFO_OFFSET_UNKNOWN;
    pInfo->ByteOffsetForPartitionAlignment                       = SSINFO_OFFSET_UNKNOWN;

    /*
     * Update the return length in the context.
     */
    pRxContext->Info.LengthRemaining = cbInfo - sizeof(*pInfo);
    pRxContext->InformationToReturn  = sizeof(*pInfo);
    return STATUS_SUCCESS;
}


/**
 * Handles NtQueryVolumeInformationFile and similar.
 *
 * The RDBSS library does not do a whole lot for these queries.  No FCB locking.
 * The IO_STATUS_BLOCK updating differs too,  setting of Ios.Information is
 * limited to cbInitialBuf - RxContext->Info.LengthRemaining.
 */
NTSTATUS VBoxMRxQueryVolumeInfo(IN OUT PRX_CONTEXT RxContext)
{
#ifdef LOG_ENABLED
    static const char * const s_apszNames[] =
    {
        "FileFsInvalidZeroEntry",      "FileFsVolumeInformation",       "FileFsLabelInformation",
        "FileFsSizeInformation",       "FileFsDeviceInformation",       "FileFsAttributeInformation",
        "FileFsControlInformation",    "FileFsFullSizeInformation",     "FileFsObjectIdInformation",
        "FileFsDriverPathInformation", "FileFsVolumeFlagsInformation",  "FileFsSectorSizeInformation",
        "FileFsDataCopyInformation",   "FileFsMetadataSizeInformation", "FileFsFullSizeInformationEx",
    };
#endif
    RxCaptureFcb;
    RxCaptureFobx;
    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_VBOX_FOBX              pVBoxFobx         = VBoxMRxGetFileObjectExtension(capFobx);
    NTSTATUS                    Status;

    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: pInfoBuffer = %p, cbInfoBuffer = %d\n",
         RxContext->Info.Buffer, RxContext->Info.LengthRemaining));
    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: vboxFobx = %p, Handle = 0x%RX64\n",
         pVBoxFobx, pVBoxFobx ? pVBoxFobx->hFile : 0));

    switch (RxContext->Info.FsInformationClass)
    {
        case FileFsVolumeInformation:
            Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsVolumeInformation\n"));
            AssertReturn(pVBoxFobx, STATUS_INVALID_PARAMETER);
            Status = vbsfNtQueryFsVolumeInfo(RxContext, (PFILE_FS_VOLUME_INFORMATION)RxContext->Info.Buffer,
                                             RxContext->Info.Length, capFcb->pNetRoot, pNetRootExtension, pVBoxFobx,
                                             VBoxMRxGetFcbExtension(capFcb));
            break;

        case FileFsSizeInformation:
            Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsSizeInformation\n"));
            AssertReturn(pVBoxFobx, STATUS_INVALID_PARAMETER);
            Status = vbsfNtQueryFsSizeInfo(RxContext, (PFILE_FS_SIZE_INFORMATION)RxContext->Info.Buffer,
                                           RxContext->Info.Length, pNetRootExtension, pVBoxFobx,
                                           VBoxMRxGetFcbExtension(capFcb));
            break;

        case FileFsFullSizeInformation:
            Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsFullSizeInformation\n"));
            AssertReturn(pVBoxFobx, STATUS_INVALID_PARAMETER);
            Status = vbsfNtQueryFsFullSizeInfo(RxContext, (PFILE_FS_FULL_SIZE_INFORMATION)RxContext->Info.Buffer,
                                               RxContext->Info.Length, pNetRootExtension, pVBoxFobx,
                                               VBoxMRxGetFcbExtension(capFcb));
            break;

        case FileFsDeviceInformation:
            Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsDeviceInformation\n"));
            AssertReturn(pVBoxFobx, STATUS_INVALID_PARAMETER);
            Status = vbsfNtQueryFsDeviceInfo(RxContext, (PFILE_FS_DEVICE_INFORMATION)RxContext->Info.Buffer,
                                             RxContext->Info.Length, capFcb->pNetRoot);
            break;

        case FileFsAttributeInformation:
            Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsAttributeInformation\n"));
            AssertReturn(pVBoxFobx, STATUS_INVALID_PARAMETER);
            Status = vbsfNtQueryFsAttributeInfo(RxContext, (PFILE_FS_ATTRIBUTE_INFORMATION)RxContext->Info.Buffer,
                                                RxContext->Info.Length, pNetRootExtension, pVBoxFobx,
                                                VBoxMRxGetFcbExtension(capFcb));
            break;

        case FileFsSectorSizeInformation:
            Log(("VBOXSF: VBoxMRxQueryVolumeInfo: FileFsSectorSizeInformation\n"));
            AssertReturn(pVBoxFobx, STATUS_INVALID_PARAMETER);
            Status = vbsfNtQueryFsSectorSizeInfo(RxContext, (PFILE_FS_SECTOR_SIZE_INFORMATION)RxContext->Info.Buffer,
                                                 RxContext->Info.Length, pNetRootExtension, pVBoxFobx,
                                                 VBoxMRxGetFcbExtension(capFcb));
            break;

        case FileFsLabelInformation:
            AssertFailed(/* Only for setting, not for querying. */);
            RT_FALL_THRU();
        default:
            Log(("VBOXSF: VBoxMRxQueryVolumeInfo: Not supported FS_INFORMATION_CLASS value: %d (%s)!\n",
                 RxContext->Info.FsInformationClass,
                 (ULONG)RxContext->Info.FsInformationClass < RT_ELEMENTS(s_apszNames)
                 ? s_apszNames[RxContext->Info.FsInformationClass] : "??"));
            Status = STATUS_INVALID_PARAMETER;
            RxContext->InformationToReturn = 0;
            break;
    }

    /* Here is a weird issue I couldn't quite figure out.  When working directories, I
       seem to get semi-random stuff back in the IO_STATUS_BLOCK when returning failures
       for unsupported classes.  The difference between directories and files seemed to
       be the IRP_SYNCHRONOUS_API flag.  Poking around a little bit more, the UserIosb
       seems to be a ring-0 stack address rather than the usermode one and
       IopSynchronousApiServiceTail being used for copying it back to user mode because
       the handle wasn't synchronous or something.

       So, the following is kludge to make the IOS values 0,0 like FAT does it.  The
       real fix for this escapes me, but this should do the trick for now... */
    PIRP pIrp = RxContext->CurrentIrp;
    if (   pIrp
        && (pIrp->Flags & IRP_SYNCHRONOUS_API)
        && RTR0MemKernelIsValidAddr(pIrp->UserIosb))
    {
        Log2(("VBOXSF: VBoxMRxQueryVolumeInfo: IRP_SYNCHRONOUS_API hack: Setting UserIosb (%p) values!\n", pIrp->UserIosb));
        __try
        {
            pIrp->UserIosb->Status      = 0;
            pIrp->UserIosb->Information = RxContext->InformationToReturn;
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
#ifdef LOG_ENABLED
            NTSTATUS rcNt = GetExceptionCode();
            Log(("VBOXSF: VBoxMRxQueryVolumeInfo: Oops %#x accessing %p\n", rcNt, pIrp->UserIosb));
#endif
        }
    }
    Log(("VBOXSF: VBoxMRxQueryVolumeInfo: Returned %#010x\n", Status));
    return Status;
}


/*********************************************************************************************************************************
*   VBoxMRxQueryFileInfo                                                                                                         *
*********************************************************************************************************************************/

/**
 * Updates the FCBs copy of the file size.
 *
 * The RDBSS is using the file size from the FCB in a few places without giving
 * us the chance to make sure that the value is up to date and properly
 * reflecting the size of the actual file on the host.   Thus this mess to try
 * keep the the size up to date where ever possible as well as some hacks to
 * bypass RDBSS' use of the FCB file size.  (And no, we cannot just make the
 * FCB_STATE_FILESIZECACHEING_ENABLED flag isn't set, because it was never
 * implemented.)
 *
 * @param   pFileObj            The file object.
 * @param   pFcb                The FCB.
 * @param   pVBoxFobX           Out file object extension data.
 * @param   cbFileNew           The new file size.
 * @param   cbFileOld           The old file size from the FCB/RDBSS.
 * @param   cbAllocated         The allocated size for the file, -1 if not
 *                              available.
 *
 * @note    Will acquire the paging I/O resource lock in exclusive mode.  Caller
 *          must not be holding it in shared mode.
 */
void vbsfNtUpdateFcbSize(PFILE_OBJECT pFileObj, PMRX_FCB pFcb, PMRX_VBOX_FOBX pVBoxFobX,
                         LONGLONG cbFileNew, LONGLONG cbFileOld, LONGLONG cbAllocated)
{
    Assert(cbFileNew != cbFileOld);
    Assert(cbFileNew >= 0);
    Assert(   !ExIsResourceAcquiredSharedLite(pFcb->Header.PagingIoResource)
           || ExIsResourceAcquiredExclusiveLite(pFcb->Header.PagingIoResource));

    /*
     * Lock the paging I/O resources before trying to modify the header variables.
     *
     * Note! RxAcquirePagingIoResource and RxReleasePagingIoResource are unsafe
     *       macros in need of {} wrappers when used with if statements.
     */
    BOOLEAN fAcquiredLock = RxAcquirePagingIoResource(NULL, pFcb);

    LONGLONG cbFileOldRecheck;
    RxGetFileSizeWithLock((PFCB)pFcb, &cbFileOldRecheck);
    if (cbFileOldRecheck == cbFileOld)
    {
        LONGLONG cbFileNewCopy = cbFileNew;
        RxSetFileSizeWithLock((PFCB)pFcb, &cbFileNewCopy);

        /* The valid data length is the same as the file size for us. */
        if (pFcb->Header.ValidDataLength.QuadPart != cbFileNew)
            pFcb->Header.ValidDataLength.QuadPart = cbFileNew;

        /* The allocation size must be larger or equal to the file size says https://www.osronline.com/article.cfm%5Eid=167.htm . */
        if (cbAllocated >= cbFileNew)
        {
            if (pFcb->Header.AllocationSize.QuadPart != cbAllocated)
                pFcb->Header.AllocationSize.QuadPart = cbAllocated;
        }
        else if (pFcb->Header.AllocationSize.QuadPart < cbFileNew)
            pFcb->Header.AllocationSize.QuadPart = cbFileNew;

        /* Update our copy. */
        pVBoxFobX->Info.cbObject = cbFileNew;
        if (cbAllocated >= 0)
            pVBoxFobX->Info.cbAllocated = cbAllocated;

        /*
         * Tell the cache manager if we can.
         *
         * According to the MSDN documentation, we must update the cache manager when
         * the file size changes, allocation size increases, valid data length descreases,
         * and when a non-cached I/O operation increases the valid data length.
         */
        SECTION_OBJECT_POINTERS *pSectPtrs = pFileObj->SectionObjectPointer;
        if (pSectPtrs)
        {
            LARGE_INTEGER NewSize;
            NewSize.QuadPart = cbFileNew;
            if (   cbFileNew >= cbFileOld
                || MmCanFileBeTruncated(pSectPtrs, &NewSize)) /** @todo do we need to check this? */
            {
                CC_FILE_SIZES FileSizes;
                FileSizes.AllocationSize           = pFcb->Header.AllocationSize;
                FileSizes.FileSize.QuadPart        = cbFileNew;
                FileSizes.ValidDataLength.QuadPart = cbFileNew;


                /* RDBSS leave the lock before calling CcSetFileSizes, so we do that too then.*/
                if (fAcquiredLock)
                {   RxReleasePagingIoResource(NULL, pFcb); /* requires {} */ }

                __try
                {
                    CcSetFileSizes(pFileObj, &FileSizes);
                }
                __except(EXCEPTION_EXECUTE_HANDLER)
                {
#ifdef LOG_ENABLED
                    NTSTATUS rcNt = GetExceptionCode();
                    Log(("vbsfNtUpdateFcbSize: CcSetFileSizes -> %#x\n", rcNt));
#endif
                    return;
                }
                Log2(("vbsfNtUpdateFcbSize: Updated Size+VDL from %#RX64 to %#RX64; Alloc %#RX64\n",
                      cbFileOld, cbFileNew, FileSizes.AllocationSize));
                return;
            }
            /** @todo should we flag this so we can try again later? */
        }

        Log2(("vbsfNtUpdateFcbSize: Updated sizes: cb=%#RX64 VDL=%#RX64 Alloc=%#RX64 (old cb=#RX64)\n",
              pFcb->Header.FileSize.QuadPart, pFcb->Header.ValidDataLength.QuadPart, pFcb->Header.AllocationSize.QuadPart, cbFileOld));
    }
    else
        Log(("vbsfNtUpdateFcbSize: Seems we raced someone updating the file size: old size = %#RX64, new size = %#RX64, current size = %#RX64\n",
             cbFileOld, cbFileNew, cbFileOldRecheck));

    if (fAcquiredLock)
    {   RxReleasePagingIoResource(NULL, pFcb); /* requires {} */ }
}


/**
 * Updates the object info to the VBox file object extension data.
 *
 * @param   pVBoxFobX               The VBox file object extension data.
 * @param   pObjInfo                The fresh data from the host.  Okay to modify.
 * @param   pVBoxFcbX               The VBox FCB extension data.
 * @param   fTimestampsToCopyAnyway VBOX_FOBX_F_INFO_XXX mask of timestamps to
 *                                  copy regardless of their suppressed state.
 *                                  This is used by the info setter function to
 *                                  get current copies of newly modified and
 *                                  suppressed fields.
 * @param   pFileObj                Pointer to the file object if we should
 *                                  update the cache manager, otherwise NULL.
 * @param   pFcb                    Pointer to the FCB if we should update its
 *                                  copy of the file size, NULL if we should
 *                                  leave it be.  Must be NULL when pFileObj is.
 */
static void vbsfNtCopyInfo(PMRX_VBOX_FOBX pVBoxFobX, PSHFLFSOBJINFO pObjInfo, PVBSFNTFCBEXT pVBoxFcbX,
                           uint8_t fTimestampsToCopyAnyway, PFILE_OBJECT pFileObj, PMRX_FCB pFcb)
{
    LogFlow(("vbsfNtCopyInfo: hFile=%#RX64 pVBoxFobX=%p\n", pVBoxFobX->hFile, pVBoxFobX));
    uint64_t const nsNow = RTTimeSystemNanoTS();

    /*
     * Check if the size changed because RDBSS and the cache manager have
     * cached copies of the file and allocation sizes.
     */
    if (pFcb && pFileObj)
    {
        LONGLONG cbFileRdbss;
        RxGetFileSizeWithLock((PFCB)pFcb, &cbFileRdbss);
        if (pObjInfo->cbObject != cbFileRdbss)
            vbsfNtUpdateFcbSize(pFileObj, pFcb, pVBoxFobX, pObjInfo->cbObject, cbFileRdbss, pObjInfo->cbAllocated);
    }

    /*
     * Check if the modified timestamp changed and try guess if it was the host.
     */
    /** @todo use modification timestamp to detect host changes?  We do on linux. */

    /*
     * Copy the object info over.  To simplify preserving the value of timestamps
     * which implict updating is currently disabled, copy them over to the source
     * structure before preforming the copy.
     */
    Assert((pVBoxFobX->fTimestampsSetByUser & ~pVBoxFobX->fTimestampsUpdatingSuppressed) == 0);
    uint8_t fCopyTs = pVBoxFobX->fTimestampsUpdatingSuppressed & ~fTimestampsToCopyAnyway;
    if (fCopyTs)
    {
        if (  (fCopyTs & VBOX_FOBX_F_INFO_LASTACCESS_TIME)
            && pVBoxFcbX->pFobxLastAccessTime == pVBoxFobX)
            pObjInfo->AccessTime        = pVBoxFobX->Info.AccessTime;

        if (   (fCopyTs & VBOX_FOBX_F_INFO_LASTWRITE_TIME)
            && pVBoxFcbX->pFobxLastWriteTime  == pVBoxFobX)
            pObjInfo->ModificationTime  = pVBoxFobX->Info.ModificationTime;

        if (   (fCopyTs & VBOX_FOBX_F_INFO_CHANGE_TIME)
            && pVBoxFcbX->pFobxChangeTime     == pVBoxFobX)
            pObjInfo->ChangeTime        = pVBoxFobX->Info.ChangeTime;
    }
    pVBoxFobX->Info = *pObjInfo;
    pVBoxFobX->nsUpToDate = nsNow;
}

/**
 * Queries the current file stats from the host and updates the RDBSS' copy of
 * the file size if necessary.
 *
 * @returns IPRT status code
 * @param   pNetRootX   Our net root extension data.
 * @param   pFileObj    The file object.
 * @param   pVBoxFobX   Our file object extension data.
 * @param   pFcb        The FCB.
 * @param   pVBoxFcbX   Our FCB extension data.
 */
int vbsfNtQueryAndUpdateFcbSize(PMRX_VBOX_NETROOT_EXTENSION pNetRootX, PFILE_OBJECT pFileObj,
                                PMRX_VBOX_FOBX pVBoxFobX, PMRX_FCB pFcb, PVBSFNTFCBEXT pVBoxFcbX)
{
    VBOXSFOBJINFOREQ *pReq = (VBOXSFOBJINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
    AssertReturn(pReq, VERR_NO_MEMORY);

    int vrc = VbglR0SfHostReqQueryObjInfo(pNetRootX->map.root, pReq, pVBoxFobX->hFile);
    if (RT_SUCCESS(vrc))
        vbsfNtCopyInfo(pVBoxFobX, &pReq->ObjInfo, pVBoxFcbX, 0, pFileObj, pFcb);
    else
        AssertMsgFailed(("vrc=%Rrc\n", vrc));

    VbglR0PhysHeapFree(pReq);
    return vrc;
}

/**
 * Handle NtQueryInformationFile and similar requests.
 *
 * The RDBSS code has done various things before we get here wrt locking and
 * request pre-processing.  Unless this is a paging file (FCB_STATE_PAGING_FILE)
 * or FileNameInformation is being queried, the FCB is locked.  For all except
 * for FileCompressionInformation, a shared FCB access (FCB.Header.Resource) is
 * acquired, where as for FileCompressionInformation it is taken exclusively.
 */
NTSTATUS VBoxMRxQueryFileInfo(IN PRX_CONTEXT RxContext)
{
    RxCaptureFcb;
    RxCaptureFobx;
    NTSTATUS                    Status            = STATUS_SUCCESS;
    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_VBOX_FOBX              pVBoxFobx         = VBoxMRxGetFileObjectExtension(capFobx);
    ULONG                       cbToCopy          = 0;

    Log(("VBOXSF: VBoxMRxQueryFileInfo: Buffer = %p, Length = %x (%d) bytes, FileInformationClass = %d\n",
         RxContext->Info.Buffer, RxContext->Info.Length, RxContext->Info.Length, RxContext->Info.FileInformationClass));

    AssertReturn(pVBoxFobx, STATUS_INVALID_PARAMETER);
    AssertReturn(RxContext->Info.Buffer, STATUS_INVALID_PARAMETER);

#define CHECK_SIZE_BREAK(a_RxContext, a_cbNeeded) \
        /* IO_STACK_LOCATION::Parameters::SetFile::Length is signed, the RxContext bugger is LONG. See end of function for why. */ \
        if ((ULONG)(a_RxContext)->Info.Length >= (a_cbNeeded)) \
        { /*likely */ } \
        else if (1) { Status = STATUS_BUFFER_TOO_SMALL; break; } else do { } while (0)

    switch (RxContext->Info.FileInformationClass)
    {
        /*
         * Queries we can satisfy without calling the host:
         */

        case FileNamesInformation:
        {
            PFILE_NAMES_INFORMATION pInfo    = (PFILE_NAMES_INFORMATION)RxContext->Info.Buffer;
            PUNICODE_STRING         FileName = GET_ALREADY_PREFIXED_NAME_FROM_CONTEXT(RxContext);
            Log(("VBOXSF: VBoxMRxQueryFileInfo: FileNamesInformation\n"));

            cbToCopy = RT_UOFFSETOF_DYN(FILE_NAMES_INFORMATION, FileName[FileName->Length / 2 + 1]);
            CHECK_SIZE_BREAK(RxContext, cbToCopy);

            pInfo->NextEntryOffset = 0;
            pInfo->FileIndex       = 0;
            pInfo->FileNameLength  = FileName->Length;

            RtlCopyMemory(pInfo->FileName, FileName->Buffer, FileName->Length);
            pInfo->FileName[FileName->Length] = 0;
            break;
        }

        case FileInternalInformation:
        {
            PFILE_INTERNAL_INFORMATION pInfo = (PFILE_INTERNAL_INFORMATION)RxContext->Info.Buffer;
            Log(("VBOXSF: VBoxMRxQueryFileInfo: FileInternalInformation\n"));

            cbToCopy = sizeof(FILE_INTERNAL_INFORMATION);
            CHECK_SIZE_BREAK(RxContext, cbToCopy);

            /* A 8-byte file reference number for the file. */
            pInfo->IndexNumber.QuadPart = (ULONG_PTR)capFcb;
            break;
        }

        case FileEaInformation:
        {
            PFILE_EA_INFORMATION pInfo = (PFILE_EA_INFORMATION)RxContext->Info.Buffer;
            Log(("VBOXSF: VBoxMRxQueryFileInfo: FileEaInformation\n"));

            cbToCopy = sizeof(FILE_EA_INFORMATION);
            CHECK_SIZE_BREAK(RxContext, cbToCopy);

            pInfo->EaSize = 0;
            break;
        }

        case FileStreamInformation:
            Log(("VBOXSF: VBoxMRxQueryFileInfo: FileStreamInformation: not supported\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;

        case FileAlternateNameInformation:
            Log(("VBOXSF: VBoxMRxQueryFileInfo: FileStreamInformation: not implemented\n"));
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
            break;

        case FileNumaNodeInformation:
            Log(("VBOXSF: VBoxMRxQueryFileInfo: FileNumaNodeInformation: not supported\n"));
            Status = STATUS_NO_SUCH_DEVICE; /* what's returned on a samba share */
            break;

        case FileStandardLinkInformation:
            Log(("VBOXSF: VBoxMRxQueryFileInfo: FileStandardLinkInformation: not supported\n"));
            Status = STATUS_NOT_SUPPORTED; /* what's returned on a samba share */
            break;

        /*
         * Queries where we need info from the host.
         *
         * For directories we don't necessarily go to the host but use info from when we
         * opened the them, why we do this is a little unclear as all the clues that r9630
         * give is "fixes". Update(bird): Disabled this and lets see if anything breaks.
         *
         * The TTL here works around two issues in particular:
         *
         *  1. We don't want to go to the host three times during a
         *     FileAllInformation query (RDBSS splits it up).
         *
         *  2. There are several filter drivers which will query info at the end of the
         *     IRP_MJ_CREATE processing.  On a W10 guest here, FileFinder.sys (belived to
         *     be related to the prefetcher) first queries FileStandardInformation, then
         *     WdFilter.sys (Windows Defender) will query FileBasicInformation,
         *     FileStandardInformation and (not relevant here) FileInternalInformation.
         *     It would be complete waste of time to requery the data from the host for
         *     each of the three queries.
         *
         * The current hardcoded 100us value was choosen by experimentation with FsPerf
         * on a decent intel system (6700K).  This is however subject to the timer tick
         * granularity on systems without KeQueryInterruptTimePrecise (i.e. pre win8).
         *
         * Note! We verify the buffer size after talking to the host, assuming that there
         *       won't be a problem and saving an extra switch statement.  IIRC the
         *       NtQueryInformationFile code verifies the sizes too.
         */
        /** @todo r=bird: install a hack so we get FileAllInformation directly up here
         *        rather than 5 individual queries.  We may end up going 3 times to
         *        the host (depending on the TTL hack) to fetch the same info over
         *        and over again. */
        case FileEndOfFileInformation:
        case FileAllocationInformation:
        case FileBasicInformation:
        case FileStandardInformation:
        case FileNetworkOpenInformation:
        case FileAttributeTagInformation:
        case FileCompressionInformation:
        {
            /* Query the information if necessary. */
            if (   1 /*!(pVBoxFobx->Info.Attr.fMode & RTFS_DOS_DIRECTORY) - bird: disabled - let's see if anything breaks. */
                && (   !pVBoxFobx->nsUpToDate
                    || RTTimeSystemNanoTS() - pVBoxFobx->nsUpToDate > RT_NS_100US /** @todo implement proper TTL */ ) )
            {
                PVBSFNTFCBEXT pVBoxFcbx = VBoxMRxGetFcbExtension(capFcb);
                AssertReturn(pVBoxFcbx, STATUS_INTERNAL_ERROR);

                VBOXSFOBJINFOREQ *pReq = (VBOXSFOBJINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
                AssertBreakStmt(pReq, Status = STATUS_NO_MEMORY);

                int vrc = VbglR0SfHostReqQueryObjInfo(pNetRootExtension->map.root, pReq, pVBoxFobx->hFile);
                if (RT_SUCCESS(vrc))
                    vbsfNtCopyInfo(pVBoxFobx, &pReq->ObjInfo, pVBoxFcbx, 0,          /* ASSUMES that PageingIoResource is not */
                                   RxContext->pFobx->AssociatedFileObject, capFcb);  /* held in shared mode here! */
                else
                {
                    Status = vbsfNtVBoxStatusToNt(vrc);
                    VbglR0PhysHeapFree(pReq);
                    break;
                }
                VbglR0PhysHeapFree(pReq);
            }

            /* Copy it into the return buffer. */
            switch (RxContext->Info.FileInformationClass)
            {
                case FileBasicInformation:
                {
                    PFILE_BASIC_INFORMATION pInfo = (PFILE_BASIC_INFORMATION)RxContext->Info.Buffer;
                    Log(("VBOXSF: VBoxMRxQueryFileInfo: FileBasicInformation\n"));

                    cbToCopy = sizeof(FILE_BASIC_INFORMATION);
                    CHECK_SIZE_BREAK(RxContext, cbToCopy);

                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pVBoxFobx->Info.BirthTime);
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pVBoxFobx->Info.AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pVBoxFobx->Info.ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pVBoxFobx->Info.ChangeTime);
                    pInfo->FileAttributes          = VBoxToNTFileAttributes(pVBoxFobx->Info.Attr.fMode);
                    Log(("VBOXSF: VBoxMRxQueryFileInfo: FileBasicInformation: File attributes: 0x%x\n",
                         pInfo->FileAttributes));
                    break;
                }

                case FileStandardInformation:
                {
                    PFILE_STANDARD_INFORMATION pInfo = (PFILE_STANDARD_INFORMATION)RxContext->Info.Buffer;
                    Log(("VBOXSF: VBoxMRxQueryFileInfo: FileStandardInformation\n"));

                    cbToCopy = sizeof(FILE_STANDARD_INFORMATION);
                    CHECK_SIZE_BREAK(RxContext, cbToCopy);

                    /* Note! We didn't used to set allocation size and end-of-file for directories.
                             NTFS reports these, though, so why shouldn't we. */
                    pInfo->AllocationSize.QuadPart = pVBoxFobx->Info.cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pVBoxFobx->Info.cbObject;
                    pInfo->NumberOfLinks           = 1; /** @todo 0? */
                    pInfo->DeletePending           = FALSE;
                    pInfo->Directory               = pVBoxFobx->Info.Attr.fMode & RTFS_DOS_DIRECTORY ? TRUE : FALSE;
                    break;
                }

                case FileNetworkOpenInformation:
                {
                    PFILE_NETWORK_OPEN_INFORMATION pInfo = (PFILE_NETWORK_OPEN_INFORMATION)RxContext->Info.Buffer;
                    Log(("VBOXSF: VBoxMRxQueryFileInfo: FileNetworkOpenInformation\n"));

                    cbToCopy = sizeof(FILE_NETWORK_OPEN_INFORMATION);
                    CHECK_SIZE_BREAK(RxContext, cbToCopy);

                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pVBoxFobx->Info.BirthTime);
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pVBoxFobx->Info.AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pVBoxFobx->Info.ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pVBoxFobx->Info.ChangeTime);
                    /* Note! We didn't used to set allocation size and end-of-file for directories.
                             NTFS reports these, though, so why shouldn't we. */
                    pInfo->AllocationSize.QuadPart = pVBoxFobx->Info.cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pVBoxFobx->Info.cbObject;
                    pInfo->FileAttributes          = VBoxToNTFileAttributes(pVBoxFobx->Info.Attr.fMode);
                    break;
                }

                case FileEndOfFileInformation:
                {
                    PFILE_END_OF_FILE_INFORMATION pInfo = (PFILE_END_OF_FILE_INFORMATION)RxContext->Info.Buffer;
                    Log(("VBOXSF: VBoxMRxQueryFileInfo: FileEndOfFileInformation\n"));

                    cbToCopy = sizeof(FILE_END_OF_FILE_INFORMATION);
                    CHECK_SIZE_BREAK(RxContext, cbToCopy);

                    /* Note! We didn't used to set allocation size and end-of-file for directories.
                             NTFS reports these, though, so why shouldn't we. */
                    pInfo->EndOfFile.QuadPart      = pVBoxFobx->Info.cbObject;
                    break;
                }

                case FileAllocationInformation:
                {
                    PFILE_ALLOCATION_INFORMATION pInfo = (PFILE_ALLOCATION_INFORMATION)RxContext->Info.Buffer;
                    Log(("VBOXSF: VBoxMRxQueryFileInfo: FileAllocationInformation\n"));

                    cbToCopy = sizeof(FILE_ALLOCATION_INFORMATION);
                    CHECK_SIZE_BREAK(RxContext, cbToCopy);

                    /* Note! We didn't used to set allocation size and end-of-file for directories.
                             NTFS reports these, though, so why shouldn't we. */
                    pInfo->AllocationSize.QuadPart = pVBoxFobx->Info.cbAllocated;
                    break;
                }

                case FileAttributeTagInformation:
                {
                    PFILE_ATTRIBUTE_TAG_INFORMATION pInfo = (PFILE_ATTRIBUTE_TAG_INFORMATION)RxContext->Info.Buffer;
                    Log(("VBOXSF: VBoxMRxQueryFileInfo: FileAttributeTagInformation\n"));

                    cbToCopy = sizeof(FILE_ATTRIBUTE_TAG_INFORMATION);
                    CHECK_SIZE_BREAK(RxContext, cbToCopy);

                    pInfo->FileAttributes = VBoxToNTFileAttributes(pVBoxFobx->Info.Attr.fMode);
                    pInfo->ReparseTag     = 0;
                    break;
                }

                case FileCompressionInformation:
                {
                    //PFILE_COMPRESSION_INFO pInfo = (PFILE_COMPRESSION_INFO)RxContext->Info.Buffer;
                    struct MY_FILE_COMPRESSION_INFO
                    {
                        LARGE_INTEGER   CompressedFileSize;
                        WORD            CompressionFormat;
                        UCHAR           CompressionUnitShift;
                        UCHAR           ChunkShift;
                        UCHAR           ClusterShift;
                        UCHAR           Reserved[3];
                    } *pInfo = (struct MY_FILE_COMPRESSION_INFO *)RxContext->Info.Buffer;
                    Log(("VBOXSF: VBoxMRxQueryFileInfo: FileCompressionInformation\n"));

                    cbToCopy = sizeof(*pInfo);
                    CHECK_SIZE_BREAK(RxContext, cbToCopy);

                    pInfo->CompressedFileSize.QuadPart = pVBoxFobx->Info.cbObject;
                    pInfo->CompressionFormat           = 0;
                    pInfo->CompressionUnitShift        = 0;
                    pInfo->ChunkShift                  = 0;
                    pInfo->ClusterShift                = 0;
                    pInfo->Reserved[0]                 = 0;
                    pInfo->Reserved[1]                 = 0;
                    pInfo->Reserved[2]                 = 0;
                    AssertCompile(sizeof(pInfo->Reserved) == 3);
                    break;
                }

                default:
                    AssertLogRelMsgFailed(("FileInformationClass=%d\n",
                                           RxContext->Info.FileInformationClass));
                    Status = STATUS_INTERNAL_ERROR;
                    break;
            }
            break;
        }


/** @todo Implement:
 * FileHardLinkInformation: rcNt=0 (STATUS_SUCCESS) Ios.Status=0 (STATUS_SUCCESS) Ios.Information=0000000000000048
 * FileProcessIdsUsingFileInformation: rcNt=0 (STATUS_SUCCESS) Ios.Status=0 (STATUS_SUCCESS) Ios.Information=0000000000000010
 * FileNormalizedNameInformation: rcNt=0 (STATUS_SUCCESS) Ios.Status=0 (STATUS_SUCCESS) Ios.Information=00000000000000AA
 *  => See during MoveFileEx call on W10.
 * FileNetworkPhysicalNameInformation: rcNt=0xc000000d (STATUS_INVALID_PARAMETER) Ios={not modified}
 * FileShortNameInformation?
 * FileNetworkPhysicalNameInformation
 */

        /*
         * Unsupported ones (STATUS_INVALID_PARAMETER is correct here if you
         * go by what fat + ntfs return, however samba mounts generally returns
         * STATUS_INVALID_INFO_CLASS except for pipe info - see queryfileinfo-1).
         */
        default:
            Log(("VBOXSF: VBoxMRxQueryFileInfo: Not supported FileInformationClass: %d!\n",
                 RxContext->Info.FileInformationClass));
            Status = STATUS_INVALID_PARAMETER;
            break;

    }
#undef CHECK_SIZE_BREAK

    /* Note! InformationToReturn doesn't seem to be used, instead Info.LengthRemaining should underflow
             so it can be used together with RxContext->CurrentIrpSp->Parameters.QueryFile.Length
             to calc the Ios.Information value.  This explain the weird LONG type choice.  */
    RxContext->InformationToReturn   = cbToCopy;
    RxContext->Info.LengthRemaining -= cbToCopy;
    AssertStmt(RxContext->Info.LengthRemaining >= 0 || Status != STATUS_SUCCESS, Status = STATUS_BUFFER_TOO_SMALL);

    Log(("VBOXSF: VBoxMRxQueryFileInfo: Returns %#x, Remaining length = %d, cbToCopy = %u (%#x)\n",
         Status, RxContext->Info.Length, cbToCopy));
    return Status;
}


/*********************************************************************************************************************************
*   VBoxMRxSetFileInfo                                                                                                           *
*********************************************************************************************************************************/

/**
 * Worker for VBoxMRxSetFileInfo.
 */
static NTSTATUS vbsfNtSetBasicInfo(PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension, PFILE_OBJECT pFileObj, PMRX_VBOX_FOBX pVBoxFobx,
                                   PMRX_FCB pFcb, PVBSFNTFCBEXT pVBoxFcbx, PFILE_BASIC_INFORMATION pBasicInfo)
{
    Log(("VBOXSF: MRxSetFileInfo: FileBasicInformation: CreationTime   %RX64\n", pBasicInfo->CreationTime.QuadPart));
    Log(("VBOXSF: MRxSetFileInfo: FileBasicInformation: LastAccessTime %RX64\n", pBasicInfo->LastAccessTime.QuadPart));
    Log(("VBOXSF: MRxSetFileInfo: FileBasicInformation: LastWriteTime  %RX64\n", pBasicInfo->LastWriteTime.QuadPart));
    Log(("VBOXSF: MRxSetFileInfo: FileBasicInformation: ChangeTime     %RX64\n", pBasicInfo->ChangeTime.QuadPart));
    Log(("VBOXSF: MRxSetFileInfo: FileBasicInformation: FileAttributes %RX32\n", pBasicInfo->FileAttributes));
    AssertReturn(pVBoxFobx, STATUS_INTERNAL_ERROR);
    AssertReturn(pVBoxFcbx, STATUS_INTERNAL_ERROR);
    AssertReturn(pNetRootExtension, STATUS_INTERNAL_ERROR);

    /** @todo r=bird: The attempt at implementing the disable-timestamp-update
     *        behaviour here needs a little adjusting.  I'll get to that later.
     *
     * Reminders:
     *
     *  X1. Drop VBOX_FOBX_F_INFO_CREATION_TIME.
     *
     *  X2. Drop unused VBOX_FOBX_F_INFO_ATTRIBUTES.
     *
     *  X3. Only act on VBOX_FOBX_F_INFO_CHANGE_TIME if modified attributes or grown
     *     the file (?) so we don't cancel out updates by other parties (like the
     *     host).
     *
     *  X4. Only act on VBOX_FOBX_F_INFO_LASTWRITE_TIME if we've written to the
     *  file.
     *
     *  X5. Only act on VBOX_FOBX_F_INFO_LASTACCESS_TIME if we've read from the file
     *     or done whatever else might modify the access time.
     *
     *  6. Don't bother calling the host if there are only zeros and -1 values.
     *  => Not done / better use it to update FCB info?
     *
     *  X7. Client application should probably be allowed to modify the timestamps
     *     explicitly using this API after disabling updating, given the wording of
     *     the footnote referenced above.
     *  => Only verified via fastfat sample, need FsPerf test.
     *
     *  8. Extend the host interface to let the host handle this crap instead as it
     *     can do a better job, like on windows it's done implicitly if we let -1
     *     pass thru IPRT.
     *  => We're actually better equipped to handle it than the host, given the
     *     FCB/inode.  New plan is to detect windows host and let it implement -1,
     *     but use the old stuff as fallback for non-windows hosts.
     *
     * One worry here is that we hide timestamp updates made by the host or other
     * guest side processes.  This could account for some of the issues we've been
     * having with the guest not noticing host side changes.
     */


    /*
     * The properties that need to be changed are set to something other
     * than zero and -1.  (According to the fastfat sample code, -1 only
     * disable implicit timestamp updating, not explicit thru this code.)
     */

    /*
     * In the host request, zero values are ignored.
     *
     * As for the NT request, the same is true but with a slight twist for the
     * timestamp fields.  If a timestamp value is non-zero, the client disables
     * implicit updating of that timestamp via this handle when reading, writing
     * and * changing attributes.  The special -1 value is used to just disable
     * implicit updating without modifying the timestamp.  While the value is
     * allowed for the CreationTime field, it will be treated as zero.
     *
     * More clues to the NT behaviour can be found here:
     * https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/16023025-8a78-492f-8b96-c873b042ac50
     * https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/d4bc551b-7aaf-4b4f-ba0e-3a75e7c528f0#Appendix_A_86
     *
     * P.S. One of the reasons behind suppressing of timestamp updating after setting
     *      them is likely related to the need of opening objects to modify them. There are
     *      no utimes() or chmod() function in NT, on the futimes() and fchmod() variants.
     */
    VBOXSFOBJINFOREQ *pReq = (VBOXSFOBJINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
    if (pReq)
        RT_ZERO(pReq->ObjInfo);
    else
        return STATUS_INSUFFICIENT_RESOURCES;
    uint32_t fModified   = 0;
    uint32_t fSuppressed = 0;

    /** @todo FsPerf need to check what is supposed to happen if modified
     * against after -1 is specified.  As state above, fastfat will not suppress
     * further setting of the timestamp like we used to do prior to revision
     * r130337 or thereabouts. */

    if (pBasicInfo->CreationTime.QuadPart && pBasicInfo->CreationTime.QuadPart != -1)
        RTTimeSpecSetNtTime(&pReq->ObjInfo.BirthTime, pBasicInfo->CreationTime.QuadPart);

    if (pBasicInfo->LastAccessTime.QuadPart)
    {
        if (pBasicInfo->LastAccessTime.QuadPart != -1)
        {
            RTTimeSpecSetNtTime(&pReq->ObjInfo.AccessTime, pBasicInfo->LastAccessTime.QuadPart);
            fModified |= VBOX_FOBX_F_INFO_LASTACCESS_TIME;
        }
        fSuppressed |= VBOX_FOBX_F_INFO_LASTACCESS_TIME;
    }

    if (pBasicInfo->LastWriteTime.QuadPart)
    {
        if (pBasicInfo->LastWriteTime.QuadPart != -1)
        {
            RTTimeSpecSetNtTime(&pReq->ObjInfo.ModificationTime, pBasicInfo->LastWriteTime.QuadPart);
            fModified |= VBOX_FOBX_F_INFO_LASTWRITE_TIME;
        }
        fSuppressed |= VBOX_FOBX_F_INFO_LASTWRITE_TIME;
    }

    if (pBasicInfo->ChangeTime.QuadPart)
    {
        if (pBasicInfo->ChangeTime.QuadPart != -1)
        {
            RTTimeSpecSetNtTime(&pReq->ObjInfo.ChangeTime, pBasicInfo->ChangeTime.QuadPart);
            fModified |= VBOX_FOBX_F_INFO_CHANGE_TIME;
        }
        fSuppressed |= VBOX_FOBX_F_INFO_CHANGE_TIME;
    }

    if (pBasicInfo->FileAttributes)
    {
        pReq->ObjInfo.Attr.fMode = NTToVBoxFileAttributes(pBasicInfo->FileAttributes);
        Assert(pReq->ObjInfo.Attr.fMode != 0);
    }

    /*
     * Call the host to do the actual updating.
     * Note! This may be a noop, but we want up-to-date info for any -1 timestamp.
     */
    int vrc = VbglR0SfHostReqSetObjInfo(pNetRootExtension->map.root, pReq, pVBoxFobx->hFile);
    NTSTATUS Status;
    if (RT_SUCCESS(vrc))
    {
        /*
         * Update our timestamp state tracking both in the file object and the file
         * control block extensions.
         */
        if (pBasicInfo->FileAttributes || fModified)
        {
            if (   pVBoxFcbx->pFobxChangeTime != pVBoxFobx
                && !(pVBoxFobx->fTimestampsUpdatingSuppressed & VBOX_FOBX_F_INFO_CHANGE_TIME))
                pVBoxFcbx->pFobxChangeTime = NULL;
            pVBoxFobx->fTimestampsImplicitlyUpdated |= VBOX_FOBX_F_INFO_CHANGE_TIME;
        }
        pVBoxFobx->fTimestampsImplicitlyUpdated  &= ~fModified;
        pVBoxFobx->fTimestampsSetByUser          |= fModified;
        pVBoxFobx->fTimestampsUpdatingSuppressed |= fSuppressed;

        if (fSuppressed)
        {
            if (fSuppressed & VBOX_FOBX_F_INFO_LASTACCESS_TIME)
                pVBoxFcbx->pFobxLastAccessTime = pVBoxFobx;
            if (fSuppressed & VBOX_FOBX_F_INFO_LASTWRITE_TIME)
                pVBoxFcbx->pFobxLastWriteTime  = pVBoxFobx;
            if (fSuppressed & VBOX_FOBX_F_INFO_CHANGE_TIME)
                pVBoxFcbx->pFobxChangeTime     = pVBoxFobx;
        }

        vbsfNtCopyInfo(pVBoxFobx, &pReq->ObjInfo, pVBoxFcbx, fSuppressed, pFileObj, pFcb);

        /*
         * Copy timestamps and attributes from the host into the return buffer to let
         * RDBSS update the FCB data when we return.   Not sure if the FCB timestamps
         * are ever used for anything, but caller doesn't check for -1 so there will
         * be some funny/invalid timestamps in the FCB it ever does.  (I seriously
         * doubt -1 is supposed to be there given that the FCB is shared and the -1
         * only applies to a given FILE_OBJECT/HANDLE.)
         */
        if (pBasicInfo->FileAttributes)
            pBasicInfo->FileAttributes          = (pBasicInfo->FileAttributes & FILE_ATTRIBUTE_TEMPORARY)
                                                | VBoxToNTFileAttributes(pReq->ObjInfo.Attr.fMode);
        if (pBasicInfo->CreationTime.QuadPart)
            pBasicInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pReq->ObjInfo.BirthTime);
        if (pBasicInfo->LastAccessTime.QuadPart)
            pBasicInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pReq->ObjInfo.AccessTime);
        if (pBasicInfo->LastWriteTime.QuadPart)
            pBasicInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pReq->ObjInfo.ModificationTime);
        if (pBasicInfo->ChangeTime.QuadPart)
            pBasicInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pReq->ObjInfo.ChangeTime);

        Status = STATUS_SUCCESS;
    }
    else
        Status = vbsfNtVBoxStatusToNt(vrc);

    VbglR0PhysHeapFree(pReq);
    return Status;
}

/**
 * Worker for VBoxMRxSetFileInfo.
 */
static NTSTATUS vbsfNtSetEndOfFile(PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension, PFILE_OBJECT pFileObj,
                                   PMRX_VBOX_FOBX pVBoxFobX, PMRX_FCB pFcb, PVBSFNTFCBEXT pVBoxFcbX, uint64_t cbNewFileSize)
{
    Log(("VBOXSF: vbsfNtSetEndOfFile: New size = %RX64\n", cbNewFileSize));

    /*
     * Allocate a request buffer and call the host with the new file size.
     */
    NTSTATUS          Status;
    VBOXSFOBJINFOREQ *pReq = (VBOXSFOBJINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
    if (pReq)
    {
        RT_ZERO(pReq->ObjInfo);
        pReq->ObjInfo.cbObject = cbNewFileSize;
        int vrc = VbglR0SfHostReqSetFileSizeOld(pNetRootExtension->map.root, pReq, pVBoxFobX->hFile);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Update related data.
             */
            pVBoxFobX->fTimestampsImplicitlyUpdated |= VBOX_FOBX_F_INFO_LASTWRITE_TIME;
            if (pVBoxFcbX->pFobxLastWriteTime != pVBoxFobX)
                pVBoxFcbX->pFobxLastWriteTime = NULL;
            vbsfNtCopyInfo(pVBoxFobX, &pReq->ObjInfo, pVBoxFcbX, 0, pFileObj, pFcb);
            Log(("VBOXSF: vbsfNtSetEndOfFile: VbglR0SfHostReqSetFileSizeOld returns new allocation size = %RX64\n",
                 pReq->ObjInfo.cbAllocated));
            Status = STATUS_SUCCESS;
        }
        else
        {
            Log(("VBOXSF: vbsfNtSetEndOfFile: VbglR0SfHostReqSetFileSizeOld(%#RX64,%#RX64) failed %Rrc\n",
                 pVBoxFobX->hFile, cbNewFileSize, vrc));
            Status = vbsfNtVBoxStatusToNt(vrc);
        }
        VbglR0PhysHeapFree(pReq);
    }
    else
        Status = STATUS_INSUFFICIENT_RESOURCES;
    Log(("VBOXSF: vbsfNtSetEndOfFile: Returns %#010x\n", Status));
    return Status;
}

/**
 * Worker for VBoxMRxSetFileInfo handling FileRenameInformation.
 *
 * @note    Renaming files from the guest is _very_ expensive:
 *              -  52175 ns/call on the host
 *              - 844237 ns/call from the guest
 *
 *          The explanation for this is that RTPathRename translates to a
 *          MoveFileEx call, which ends up doing a lot more than opening the
 *          file and setting rename information on that handle (W10):
 *              - Opens the file.
 *              - Queries FileAllInformation.
 *              - Tries to open the new filename (result: 0x00000000 but not
 *                opened by our code - weird).
 *              - Queries FileNormalizedNameInformation (result: 0xc000000d).
 *              - Does IOCTL_REDIR_QUERY_PATH_EX on \vboxsvr\IPC$.
 *              - Tries to open \vboxsvr\IPC$ (result: 0xc0000016)
 *              - Opens the parent directory.
 *              - Queries directory info with old name as filter.
 *              - Closes parent directory handle.
 *              - Finally does FileRenameInformation.
 *              - Closes the handle to the renamed file.
 */
static NTSTATUS vbsfNtRename(IN PRX_CONTEXT RxContext,
                             IN PFILE_RENAME_INFORMATION pRenameInfo,
                             IN ULONG cbInfo)
{
    RxCaptureFcb;
    RxCaptureFobx;
    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_VBOX_FOBX              pVBoxFobx         = VBoxMRxGetFileObjectExtension(capFobx);
    PMRX_SRV_OPEN               pSrvOpen          = capFobx->pSrvOpen;

    /* Make sure we've got valid buffer and filename sizes: */
    AssertReturn(cbInfo >= RT_UOFFSETOF(FILE_RENAME_INFORMATION, FileName), STATUS_INFO_LENGTH_MISMATCH);
    size_t const cbFilename = pRenameInfo->FileNameLength;
    AssertReturn(cbFilename < _64K - 2, STATUS_INVALID_PARAMETER);
    AssertReturn(cbInfo - RT_UOFFSETOF(FILE_RENAME_INFORMATION, FileName) >= cbFilename, STATUS_INFO_LENGTH_MISMATCH);

    Log(("VBOXSF: vbsfNtRename: FileNameLength = %#x (%d), FileName = %.*ls\n",
         cbFilename, cbFilename, cbFilename / sizeof(WCHAR), &pRenameInfo->FileName[0]));

/** @todo Add new function that also closes the handle, like for remove, saving a host call. */

    /* Must close the file before renaming it! */
    if (pVBoxFobx->hFile != SHFL_HANDLE_NIL)
    {
        Log(("VBOXSF: vbsfNtRename: Closing handle %#RX64...\n", pVBoxFobx->hFile));
        vbsfNtCloseFileHandle(pNetRootExtension, pVBoxFobx, VBoxMRxGetFcbExtension(capFcb));
    }

    /* Mark it as renamed, so we do nothing during close. */
    /** @todo r=bird: Isn't this a bit premature? */
    SetFlag(pSrvOpen->Flags, SRVOPEN_FLAG_FILE_RENAMED);

    /*
     * Allocate a request embedding the destination string.
     */
    NTSTATUS                   Status = STATUS_INSUFFICIENT_RESOURCES;
    size_t const               cbReq  = RT_UOFFSETOF(VBOXSFRENAMEWITHSRCBUFREQ, StrDstPath.String) + cbFilename + sizeof(RTUTF16);
    VBOXSFRENAMEWITHSRCBUFREQ *pReq   = (VBOXSFRENAMEWITHSRCBUFREQ *)VbglR0PhysHeapAlloc((uint32_t)cbReq);
    if (pReq)
    {
        /* The destination path string. */
        pReq->StrDstPath.u16Size   = (uint16_t)(cbFilename + sizeof(RTUTF16));
        pReq->StrDstPath.u16Length = (uint16_t)cbFilename;
        memcpy(&pReq->StrDstPath.String, pRenameInfo->FileName, cbFilename);
        pReq->StrDstPath.String.utf16[cbFilename / sizeof(RTUTF16)] = '\0';

        /* The source path string. */
        PUNICODE_STRING pNtSrcPath   = GET_ALREADY_PREFIXED_NAME(pSrvOpen, capFcb);
        uint16_t const  cbSrcPath    = pNtSrcPath->Length;
        PSHFLSTRING     pShflSrcPath = (PSHFLSTRING)VbglR0PhysHeapAlloc(SHFLSTRING_HEADER_SIZE + cbSrcPath + sizeof(RTUTF16));
        if (pShflSrcPath)
        {
            pShflSrcPath->u16Length = cbSrcPath;
            pShflSrcPath->u16Size   = cbSrcPath + (uint16_t)sizeof(RTUTF16);
            memcpy(&pShflSrcPath->String, pNtSrcPath->Buffer, cbSrcPath);
            pShflSrcPath->String.utf16[cbSrcPath / sizeof(RTUTF16)] = '\0';

            /*
             * Call the host.
             */
            uint32_t fRename = pVBoxFobx->Info.Attr.fMode & RTFS_DOS_DIRECTORY ? SHFL_RENAME_DIR : SHFL_RENAME_FILE;
            if (pRenameInfo->ReplaceIfExists)
                fRename |= SHFL_RENAME_REPLACE_IF_EXISTS;
            Log(("VBOXSF: vbsfNtRename: Calling VbglR0SfHostReqRenameWithSrcBuf fFlags=%#x SrcPath=%.*ls, DstPath=%.*ls\n",
                 fRename, pShflSrcPath->u16Length / sizeof(RTUTF16), pShflSrcPath->String.utf16,
                 pReq->StrDstPath.u16Size / sizeof(RTUTF16), pReq->StrDstPath.String.utf16));
            int vrc = VbglR0SfHostReqRenameWithSrcBuf(pNetRootExtension->map.root, pReq, pShflSrcPath, fRename);
            if (RT_SUCCESS(vrc))
                Status = STATUS_SUCCESS;
            else
            {
                Status = vbsfNtVBoxStatusToNt(vrc);
                Log(("VBOXSF: vbsfNtRename: VbglR0SfHostReqRenameWithSrcBuf failed with %Rrc (Status=%#x)\n", vrc, Status));
            }

            VbglR0PhysHeapFree(pShflSrcPath);
        }
        VbglR0PhysHeapFree(pReq);
    }
    Log(("VBOXSF: vbsfNtRename: Returned 0x%08X\n", Status));
    return Status;
}

/**
 * Handle NtSetInformationFile and similar requests.
 *
 * The RDBSS code has done various things before we get here wrt locking and
 * request pre-processing.  It will normally acquire an exclusive FCB lock, but
 * not if this is related to a page file (FCB_STATE_PAGING_FILE set).
 */
NTSTATUS VBoxMRxSetFileInfo(IN PRX_CONTEXT RxContext)
{
    RxCaptureFcb;
    RxCaptureFobx;
    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_VBOX_FOBX              pVBoxFobx         = VBoxMRxGetFileObjectExtension(capFobx);
    NTSTATUS                    Status            = STATUS_SUCCESS;

    Log(("VBOXSF: MrxSetFileInfo: Buffer = %p, Length = %#x (%d), FileInformationClass = %d\n",
         RxContext->Info.Buffer, RxContext->Info.Length, RxContext->Info.Length, RxContext->Info.FileInformationClass));

    /*
     * The essence of the size validation table for NtSetInformationFile from w10 build 17763:
     * UCHAR IoCheckQuerySetFileInformation[77]:
     *     db 28h                  ; 4       FileBasicInformation,                  w7
     *     db 18h                  ; 10      FileRenameInformation,                 w7
     *     db 18h                  ; 11      FileLinkInformation,                   w7
     *     db 1                    ; 13      FileDispositionInformation,            w7
     *     db 8                    ; 14      FilePositionInformation,               w7
     *     db 4                    ; 16      FileModeInformation,
     *     db 8                    ; 19      FileAllocationInformation,             w7
     *     db 8                    ; 20      FileEndOfFileInformation,              w7
     *     db 8                    ; 23      FilePipeInformation,                   w7
     *     db 10h                  ; 25      FilePipeRemoteInformation,             w7
     *     db 8                    ; 27      FileMailslotSetInformation,
     *     db 48h                  ; 29      FileObjectIdInformation,
     *     db 10h                  ; 30      FileCompletionInformation,                 - "reserved for system use"
     *     db 18h                  ; 31      FileMoveClusterInformation,            w7  - "reserved for system use"
     *     db 38h                  ; 32      FileQuotaInformation,
     *     db 10h                  ; 36      FileTrackingInformation,                   - "reserved for system use"
     *     db 8                    ; 39      FileValidDataLengthInformation,        w7
     *     db 8                    ; 40      FileShortNameInformation,              w7
     *     db 4                    ; 41      FileIoCompletionNotificationInformation,   - "reserved for system use"
     *     db 10h                  ; 42      FileIoStatusBlockRangeInformation,         - "reserved for system use"
     *     db 4                    ; 43      FileIoPriorityHintInformation,
     *     db 14h                  ; 44      FileSfioReserveInformation,                - "reserved for system use"
     *     db 10h                  ; 61      FileReplaceCompletionInformation,
     *     db 4                    ; 64      FileDispositionInformationEx,              - Adds posix semantics and stuff.
     *     db 18h                  ; 65      FileRenameInformationEx,                   - Adds posix semantics and stuff.
     *     db 8                    ; 67      FileDesiredStorageClassInformation,
     *     db 10h                  ; 69      FileMemoryPartitionInformation,            - "reserved for system use", W10-1709
     *     db 4                    ; 71      FileCaseSensitiveInformation,              - Per dir case sensitivity. (For linux?)
     *     db 18h                  ; 72      FileLinkInformationEx,                     - Adds posix semantics and stuff.
     *     db 4                    ; 74      FileStorageReserveIdInformation,
     *     db 4                    ; 75      FileCaseSensitiveInformationForceAccessCheck, - for the i/o manager, w10-1809.
     *
     * Note! Using WDK 7600.16385.1/wnet, we're limited in what gets passed along, unknown
     *       stuff will be rejected with STATUS_INVALID_PARAMETER and never get here.  OTOH,
     *       the 10.00.16299.0 WDK will forward anything it doesn't know from what I can tell.
     *       Not sure exactly when this changed.
     */
    switch ((int)RxContext->Info.FileInformationClass)
    {
        /*
         * This is used to modify timestamps and attributes.
         *
         * Upon successful return, RDBSS will ensure that FILE_ATTRIBUTE_DIRECTORY is set
         * according to the FCB object type (see RxFinishFcbInitialization in path.cpp),
         * and that the  FILE_ATTRIBUTE_TEMPORARY attribute is reflected in  FcbState
         * (FCB_STATE_TEMPORARY) and the file object flags (FO_TEMPORARY_FILE).  It will
         * also copy each non-zero timestamp into the FCB and set the corresponding
         * FOBX_FLAG_USER_SET_xxxx flag in the FOBX.
         *
         * RDBSS behaviour is idential between 16299.0/w10 and 7600.16385.1/wnet.
         */
        case FileBasicInformation:
        {
            Assert(RxContext->Info.Length >= sizeof(FILE_BASIC_INFORMATION));
            Status = vbsfNtSetBasicInfo(pNetRootExtension, RxContext->pFobx->AssociatedFileObject, pVBoxFobx, capFcb,
                                        VBoxMRxGetFcbExtension(capFcb), (PFILE_BASIC_INFORMATION)RxContext->Info.Buffer);
            break;
        }

        /*
         * This is used to rename a file.
         */
        case FileRenameInformation:
        {
#ifdef LOG_ENABLED
            PFILE_RENAME_INFORMATION pInfo = (PFILE_RENAME_INFORMATION)RxContext->Info.Buffer;
            Log(("VBOXSF: MrxSetFileInfo: FileRenameInformation: ReplaceIfExists = %d, RootDirectory = 0x%x = [%.*ls]\n",
                 pInfo->ReplaceIfExists, pInfo->RootDirectory, pInfo->FileNameLength / sizeof(WCHAR), pInfo->FileName));
#endif

            Status = vbsfNtRename(RxContext, (PFILE_RENAME_INFORMATION)RxContext->Info.Buffer, RxContext->Info.Length);
            break;
        }

        /*
         * This is presumably used for hardlinking purposes.  We don't support that.
         */
        case FileLinkInformation:
        {
#ifdef LOG_ENABLED
            PFILE_LINK_INFORMATION pInfo = (PFILE_LINK_INFORMATION )RxContext->Info.Buffer;
            Log(("VBOXSF: MrxSetFileInfo: FileLinkInformation: ReplaceIfExists = %d, RootDirectory = 0x%x = [%.*ls]. Not implemented!\n",
                 pInfo->ReplaceIfExists, pInfo->RootDirectory, pInfo->FileNameLength / sizeof(WCHAR), pInfo->FileName));
#endif

            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }

        /*
         * This is used to delete file.
         */
        case FileDispositionInformation:
        {
            PFILE_DISPOSITION_INFORMATION pInfo = (PFILE_DISPOSITION_INFORMATION)RxContext->Info.Buffer;
            Log(("VBOXSF: MrxSetFileInfo: FileDispositionInformation: Delete = %d\n",
                 pInfo->DeleteFile));

            if (pInfo->DeleteFile && capFcb->OpenCount == 1)
                Status = vbsfNtRemove(RxContext);
            else
                Status = STATUS_SUCCESS;
            break;
        }

        /*
         * The file position is handled by the RDBSS library (RxSetPositionInfo)
         * and we should never see this request.
         */
        case FilePositionInformation:
            AssertMsgFailed(("VBOXSF: MrxSetFileInfo: FilePositionInformation: CurrentByteOffset = 0x%RX64. Unsupported!\n",
                             ((PFILE_POSITION_INFORMATION)RxContext->Info.Buffer)->CurrentByteOffset.QuadPart));
            Status = STATUS_INTERNAL_ERROR;
            break;

        /*
         * Change the allocation size, leaving the EOF alone unless the file shrinks.
         *
         * There is no shared folder operation for this, so we only need to care
         * about adjusting EOF if the file shrinks.
         *
         * https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/d4bc551b-7aaf-4b4f-ba0e-3a75e7c528f0#Appendix_A_83
         *
         * Note! The RDBSS caller, RxSetAllocationInfo, will always update the
         *       AllocationSize field of the FCB header before calling us.  If
         *       the change is perceived to be truncating the file (new alloc
         *       size smaller than cached file size from header), the FileSize
         *       and (probably also the) ValidateDataLength FCB fields will be
         *       modified as well _before_ we're called.
         *
         *       Therefore, we cannot use the file size from the FCB to determin
         *       whether it's okay to skip the EOF setting host call or not, we
         *       must use our own cached file size value.  (Cause of broken test
         *       of opening w/ truncation.)
         *
         * P.S.  When opening a file with the TRUNCATE_EXISTING disposition,
         *       kernel32.dll translate it to FILE_OPEN and do the truncating
         *       separately with a set FileAllocationInformation operation (no
         *       EOF or VDL setting).
         */
        case FileAllocationInformation:
        {
            PFILE_ALLOCATION_INFORMATION pInfo = (PFILE_ALLOCATION_INFORMATION)RxContext->Info.Buffer;
            Log(("VBOXSF: MrxSetFileInfo: FileAllocationInformation: new AllocSize = 0x%RX64, FileSize = 0x%RX64\n",
                 pInfo->AllocationSize.QuadPart, capFcb->Header.FileSize.QuadPart));

            if (pInfo->AllocationSize.QuadPart >= pVBoxFobx->Info.cbObject)
                Status = STATUS_SUCCESS;
            else
            {
                /** @todo get up to date EOF from host?  We may risk accidentally growing the
                 *        file here if the host (or someone else) truncated it. */
                Status = vbsfNtSetEndOfFile(pNetRootExtension, RxContext->pFobx->AssociatedFileObject, pVBoxFobx,
                                            capFcb, VBoxMRxGetFcbExtension(capFcb), pInfo->AllocationSize.QuadPart);
            }
            break;
        }

        /*
         * Prior to calling us, RxSetEndOfFileInfo will have updated the FCB fields space.FileSize,
         * Header.AllocationSize and (if old value was larger) Header.ValidDataLength.  On success
         * it will inform the cache manager, while on failure the old values will be restored.
         *
         * Note! RxSetEndOfFileInfo assumes that the old Header.FileSize value is up to date and
         *       will hide calls which does not change the size from us.  This is of course not
         *       the case for non-local file systems, as the server is the only which up-to-date
         *       information.
         *
         *       We work around this either by modifying FCB.Header.FileSize slightly when it equals
         *       the new size.  This is either done below in the FileEndOfFileInformation + 4096 case,
         *       or when using older WDK libs in VBoxHookMjSetInformation.  The FCB is locked
         *       exclusivly while we operate with the incorrect Header.FileSize value, which should
         *       prevent anyone else from making use of it till it has been updated again.
         *
         */
        case FileEndOfFileInformation:
        {
            PFILE_END_OF_FILE_INFORMATION pInfo = (PFILE_END_OF_FILE_INFORMATION)RxContext->Info.Buffer;
            Log(("VBOXSF: MrxSetFileInfo: FileEndOfFileInformation: new EndOfFile 0x%RX64, FileSize = 0x%RX64\n",
                 pInfo->EndOfFile.QuadPart, capFcb->Header.FileSize.QuadPart));

            Status = vbsfNtSetEndOfFile(pNetRootExtension, RxContext->pFobx->AssociatedFileObject, pVBoxFobx,
                                        capFcb, VBoxMRxGetFcbExtension(capFcb), pInfo->EndOfFile.QuadPart);

            Log(("VBOXSF: MrxSetFileInfo: FileEndOfFileInformation: Status 0x%08X\n",
                 Status));
            break;
        }

#if 0 /* This only works for more recent versions of the RDBSS library, not for the one we're using (WDK 7600.16385.1). */
        /*
         * HACK ALERT! This is FileEndOfFileInformation after it passed thru
         * VBoxHookMjSetInformation so we can twiddle the cached file size in
         * the FCB to ensure the set EOF request always reaches the host.
         *
         * Note! We have to call thru RxSetEndOfFileInfo to benefit from its
         *       update logic and avoid needing to replicate that code.
         */
        case FileEndOfFileInformation + 4096:
        {
            PFILE_END_OF_FILE_INFORMATION pInfo = (PFILE_END_OF_FILE_INFORMATION)RxContext->Info.Buffer;
            Log(("VBOXSF: MrxSetFileInfo: FileEndOfFileInformation+4096: new EndOfFile 0x%RX64, FileSize = 0x%RX64\n",
                 pInfo->EndOfFile.QuadPart, capFcb->Header.FileSize.QuadPart));

            /* Undo the change from VBoxHookMjSetInformation:  */
            Assert(RxContext->CurrentIrpSp);
            RxContext->CurrentIrpSp->Parameters.SetFile.FileInformationClass = FileEndOfFileInformation;
            RxContext->Info.FileInformationClass                             = FileEndOfFileInformation;

            /* Tweak the size if necessary and forward the call. */
            int64_t const cbOldSize = capFcb->Header.FileSize.QuadPart;
            if (   pInfo->EndOfFile.QuadPart != cbOldSize
                || !(capFcb->FcbState & FCB_STATE_PAGING_FILE))
            {
                Status = RxSetEndOfFileInfo(RxContext, RxContext->CurrentIrp, (PFCB)capFcb, (PFOBX)capFobx);
                Log(("VBOXSF: MrxSetFileInfo: FileEndOfFileInformation+4096: Status 0x%08X\n",
                     Status));
            }
            else
            {
                int64_t const cbHackedSize = cbOldSize ? cbOldSize - 1 : 1;
                capFcb->Header.FileSize.QuadPart = cbHackedSize;
                Status = RxSetEndOfFileInfo(RxContext, RxContext->CurrentIrp, (PFCB)capFcb, (PFOBX)capFobx);
                if (   !NT_SUCCESS(Status)
                    && capFcb->Header.FileSize.QuadPart == cbHackedSize)
                    capFcb->Header.FileSize.QuadPart = cbOldSize;
                else
                    Assert(   capFcb->Header.FileSize.QuadPart != cbHackedSize
                           || pVBoxFobx->Info.cbObject == cbHackedSize);
                Log(("VBOXSF: MrxSetFileInfo: FileEndOfFileInformation+4096: Status 0x%08X (tweaked)\n",
                     Status));
            }
            break;
        }
#endif

        /// @todo FileModeInformation ?
        /// @todo return access denied or something for FileValidDataLengthInformation?

        default:
            Log(("VBOXSF: MrxSetFileInfo: Not supported FileInformationClass: %d!\n",
                 RxContext->Info.FileInformationClass));
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    Log(("VBOXSF: MrxSetFileInfo: Returned 0x%08X\n", Status));
    return Status;
}

/**
 * This is a no-op because we already set the file timestamps before closing,
 * and generally the host takes care of this.
 *
 * RDBSS calls this if it things we might need to update file information as the
 * file is closed.
 */
NTSTATUS VBoxMRxSetFileInfoAtCleanup(IN PRX_CONTEXT RxContext)
{
    RT_NOREF(RxContext);
    Log(("VBOXSF: MRxSetFileInfoAtCleanup\n"));
    return STATUS_SUCCESS;
}

