/* $Id: VDIoBackend.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox HDD container test utility, I/O backend API
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#define LOGGROUP LOGGROUP_DEFAULT /** @todo Log group */
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/file.h>
#include <iprt/string.h>

#include "VDIoBackend.h"
#include "VDMemDisk.h"
#include "VDIoBackendMem.h"

typedef struct VDIOBACKEND
{
    /** Memory I/O backend handle. */
    PVDIOBACKENDMEM   pIoMem;
    /** Users of the memory backend. */
    volatile uint32_t cRefsIoMem;
    /** Users of the file backend. */
    volatile uint32_t cRefsFile;
} VDIOBACKEND;

typedef struct VDIOSTORAGE
{
    /** Pointer to the I/O backend parent. */
    PVDIOBACKEND      pIoBackend;
    /** Completion callback. */
    PFNVDIOCOMPLETE   pfnComplete;
    /** Flag whether this storage is backed by a file or memory.disk. */
    bool              fMemory;
    /** Type dependent data. */
    union
    {
        /** Memory disk handle. */
        PVDMEMDISK    pMemDisk;
        /** file handle. */
        RTFILE        hFile;
    } u;
} VDIOSTORAGE;


int VDIoBackendCreate(PPVDIOBACKEND ppIoBackend)
{
    int rc = VINF_SUCCESS;
    PVDIOBACKEND pIoBackend;

    pIoBackend = (PVDIOBACKEND)RTMemAllocZ(sizeof(VDIOBACKEND));
    if (pIoBackend)
        *ppIoBackend = pIoBackend;
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

void VDIoBackendDestroy(PVDIOBACKEND pIoBackend)
{
    if (pIoBackend->pIoMem)
        VDIoBackendMemDestroy(pIoBackend->pIoMem);
    RTMemFree(pIoBackend);
}

int VDIoBackendStorageCreate(PVDIOBACKEND pIoBackend, const char *pszBackend,
                             const char *pszName, PFNVDIOCOMPLETE pfnComplete,
                             PPVDIOSTORAGE ppIoStorage)
{
    int rc = VINF_SUCCESS;
    PVDIOSTORAGE pIoStorage = (PVDIOSTORAGE)RTMemAllocZ(sizeof(VDIOSTORAGE));

    if (pIoStorage)
    {
        pIoStorage->pIoBackend = pIoBackend;
        pIoStorage->pfnComplete = pfnComplete;
        if (!strcmp(pszBackend, "memory"))
        {
            pIoStorage->fMemory = true;
            rc = VDMemDiskCreate(&pIoStorage->u.pMemDisk, 0 /* Growing */);
            if (RT_SUCCESS(rc))
            {
                uint32_t cRefs = ASMAtomicIncU32(&pIoBackend->cRefsIoMem);
                if (   cRefs == 1
                    && !pIoBackend->pIoMem)
                {
                    rc = VDIoBackendMemCreate(&pIoBackend->pIoMem);
                    if (RT_FAILURE(rc))
                        VDMemDiskDestroy(pIoStorage->u.pMemDisk);
                }
            }
        }
        else if (!strcmp(pszBackend, "file"))
        {
            /* Create file. */
            rc = RTFileOpen(&pIoStorage->u.hFile, pszName,
                            RTFILE_O_READWRITE | RTFILE_O_CREATE | RTFILE_O_ASYNC_IO | RTFILE_O_NO_CACHE | RTFILE_O_DENY_NONE);

            if (RT_FAILURE(rc))
                ASMAtomicDecU32(&pIoBackend->cRefsFile);
        }
        else
            rc = VERR_NOT_SUPPORTED;

        if (RT_FAILURE(rc))
            RTMemFree(pIoStorage);
        else
            *ppIoStorage = pIoStorage;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

void VDIoBackendStorageDestroy(PVDIOSTORAGE pIoStorage)
{
    if (pIoStorage->fMemory)
    {
        VDMemDiskDestroy(pIoStorage->u.pMemDisk);
        ASMAtomicDecU32(&pIoStorage->pIoBackend->cRefsIoMem);
    }
    else
    {
        RTFileClose(pIoStorage->u.hFile);
        ASMAtomicDecU32(&pIoStorage->pIoBackend->cRefsFile);
    }
    RTMemFree(pIoStorage);
}

int VDIoBackendTransfer(PVDIOSTORAGE pIoStorage, VDIOTXDIR enmTxDir, uint64_t off,
                        size_t cbTransfer, PRTSGBUF pSgBuf, void *pvUser, bool fSync)
{
    int rc = VINF_SUCCESS;

    if (pIoStorage->fMemory)
    {
        if (!fSync)
        {
            rc = VDIoBackendMemTransfer(pIoStorage->pIoBackend->pIoMem, pIoStorage->u.pMemDisk,
                                        enmTxDir, off, cbTransfer, pSgBuf, pIoStorage->pfnComplete,
                                        pvUser);
        }
        else
        {
            switch (enmTxDir)
            {
                case VDIOTXDIR_READ:
                    rc = VDMemDiskRead(pIoStorage->u.pMemDisk, off, cbTransfer, pSgBuf);
                    break;
                case VDIOTXDIR_WRITE:
                    rc = VDMemDiskWrite(pIoStorage->u.pMemDisk, off, cbTransfer, pSgBuf);
                    break;
                case VDIOTXDIR_FLUSH:
                    break;
                default:
                    AssertMsgFailed(("Invalid transfer type %d\n", enmTxDir));
            }
        }
    }
    else
    {
        if (!fSync)
            rc = VERR_NOT_IMPLEMENTED;
        else
        {
            switch (enmTxDir)
            {
                case VDIOTXDIR_READ:
                    rc = RTFileSgReadAt(pIoStorage->u.hFile, off, pSgBuf, cbTransfer, NULL);
                    break;
                case VDIOTXDIR_WRITE:
                    rc = RTFileSgWriteAt(pIoStorage->u.hFile, off, pSgBuf, cbTransfer, NULL);
                    break;
                case VDIOTXDIR_FLUSH:
                    rc = RTFileFlush(pIoStorage->u.hFile);
                    break;
                default:
                    AssertMsgFailed(("Invalid transfer type %d\n", enmTxDir));
            }
        }
    }

    return rc;
}

int VDIoBackendStorageSetSize(PVDIOSTORAGE pIoStorage, uint64_t cbSize)
{
    int rc = VINF_SUCCESS;

    if (pIoStorage->fMemory)
    {
        rc = VDMemDiskSetSize(pIoStorage->u.pMemDisk, cbSize);
    }
    else
        rc = RTFileSetSize(pIoStorage->u.hFile, cbSize);

    return rc;
}

int VDIoBackendStorageGetSize(PVDIOSTORAGE pIoStorage, uint64_t *pcbSize)
{
    int rc = VINF_SUCCESS;

    if (pIoStorage->fMemory)
    {
        rc = VDMemDiskGetSize(pIoStorage->u.pMemDisk, pcbSize);
    }
    else
        rc = RTFileQuerySize(pIoStorage->u.hFile, pcbSize);

    return rc;
}

DECLHIDDEN(int) VDIoBackendDumpToFile(PVDIOSTORAGE pIoStorage, const char *pszPath)
{
    int rc = VINF_SUCCESS;

    if (pIoStorage->fMemory)
        rc = VDMemDiskWriteToFile(pIoStorage->u.pMemDisk, pszPath);
    else
        rc = VERR_NOT_IMPLEMENTED;

    return rc;
}

