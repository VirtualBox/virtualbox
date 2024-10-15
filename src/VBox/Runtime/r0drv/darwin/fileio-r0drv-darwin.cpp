/* $Id: fileio-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File I/O, R0 Driver, Darwin.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include "the-darwin-kernel.h"

#include <iprt/file.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Default file permissions for newly created files. */
#if defined(S_IRUSR) && defined(S_IWUSR)
# define RT_FILE_PERMISSION  (S_IRUSR | S_IWUSR)
#else
# define RT_FILE_PERMISSION  (00600)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Darwin kernel file handle data.
 */
typedef struct RTFILEINT
{
    /** Magic value (RTFILE_MAGIC). */
    uint32_t        u32Magic;
    /** The open mode flags passed to the kernel API. */
    int             fOpenMode;
    /** The open flags passed to RTFileOpen. */
    uint64_t        fOpen;
    /** The VFS context in which the file was opened. */
    vfs_context_t   hVfsCtx;
    /** The vnode returned by vnode_open. */
    vnode_t         hVnode;
    /** The current file offset. */
    uint64_t        offFile;
} RTFILEINT;
/** Magic number for RTFILEINT::u32Magic (To Be Determined). */
#define RTFILE_MAGIC                    UINT32_C(0x01020304)


RTDECL(int) RTFileOpen(PRTFILE phFile, const char *pszFilename, uint64_t fOpen)
{
    AssertReturn(!(fOpen & RTFILE_O_TEMP_AUTO_DELETE), VERR_NOT_SUPPORTED);

    RTFILEINT *pThis = (RTFILEINT *)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;
    IPRT_DARWIN_SAVE_EFL_AC();

    errno_t rc;
    pThis->u32Magic = RTFILE_MAGIC;
    pThis->fOpen    = fOpen;
    pThis->hVfsCtx  = vfs_context_current();
    if (pThis->hVfsCtx != NULL)
    {
        int             fCMode    = (fOpen & RTFILE_O_CREATE_MODE_MASK)
                                  ? (fOpen & RTFILE_O_CREATE_MODE_MASK) >> RTFILE_O_CREATE_MODE_SHIFT
                                  : RT_FILE_PERMISSION;
        int             fVnFlags  = 0; /* VNODE_LOOKUP_XXX */
        int             fOpenMode = 0;
        if (fOpen & RTFILE_O_NON_BLOCK)
            fOpenMode |= O_NONBLOCK;
        if (fOpen & RTFILE_O_WRITE_THROUGH)
            fOpenMode |= O_SYNC;

        /* create/truncate file */
        switch (fOpen & RTFILE_O_ACTION_MASK)
        {
            case RTFILE_O_OPEN:             break;
            case RTFILE_O_OPEN_CREATE:      fOpenMode |= O_CREAT; break;
            case RTFILE_O_CREATE:           fOpenMode |= O_CREAT | O_EXCL; break;
            case RTFILE_O_CREATE_REPLACE:   fOpenMode |= O_CREAT | O_TRUNC; break; /** @todo replacing needs fixing, this is *not* a 1:1 mapping! */
        }
        if (fOpen & RTFILE_O_TRUNCATE)
            fOpenMode |= O_TRUNC;

        switch (fOpen & RTFILE_O_ACCESS_MASK)
        {
            case RTFILE_O_READ:
                fOpenMode |= FREAD;
                break;
            case RTFILE_O_WRITE:
                fOpenMode |= fOpen & RTFILE_O_APPEND ? O_APPEND | FWRITE : FWRITE;
                break;
            case RTFILE_O_READWRITE:
                fOpenMode |= fOpen & RTFILE_O_APPEND ? O_APPEND | FWRITE | FREAD : FWRITE | FREAD;
                break;
            default:
                AssertMsgFailed(("RTFileOpen received an invalid RW value, fOpen=%#x\n", fOpen));
                IPRT_DARWIN_RESTORE_EFL_AC();
                return VERR_INVALID_PARAMETER;
        }

        pThis->fOpenMode = fOpenMode;
        rc = vnode_open(pszFilename, fOpenMode, fCMode, fVnFlags, &pThis->hVnode, pThis->hVfsCtx);
        if (rc == 0)
        {
            *phFile = pThis;
            IPRT_DARWIN_RESTORE_EFL_AC();
            return VINF_SUCCESS;
        }

        rc = RTErrConvertFromErrno(rc);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;
    RTMemFree(pThis);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return rc;
}


RTDECL(int) RTFileClose(RTFILE hFile)
{
    if (hFile == NIL_RTFILE)
        return VINF_SUCCESS;

    RTFILEINT *pThis = hFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTFILE_MAGIC, VERR_INVALID_HANDLE);
    pThis->u32Magic = ~RTFILE_MAGIC;

    IPRT_DARWIN_SAVE_EFL_AC();
    errno_t rc = vnode_close(pThis->hVnode, pThis->fOpenMode & (FREAD | FWRITE), pThis->hVfsCtx);
    IPRT_DARWIN_RESTORE_EFL_AC();

    RTMemFree(pThis);
    return RTErrConvertFromErrno(rc);
}


RTDECL(int) RTFileReadAt(RTFILE hFile, RTFOFF off, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RTFILEINT *pThis = hFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTFILE_MAGIC, VERR_INVALID_HANDLE);

    off_t offNative = (off_t)off;
    AssertReturn((RTFOFF)offNative == off, VERR_OUT_OF_RANGE);
    IPRT_DARWIN_SAVE_EFL_AC();

#if 0 /* Added in 10.6, grr. */
    errno_t rc;
    if (!pcbRead)
        rc = vn_rdwr(UIO_READ, pThis->hVnode, (char *)pvBuf, cbToRead, offNative, UIO_SYSSPACE, 0 /*ioflg*/,
                     vfs_context_ucred(pThis->hVfsCtx), NULL, vfs_context_proc(pThis->hVfsCtx));
    else
    {
        int cbLeft = 0;
        rc = vn_rdwr(UIO_READ, pThis->hVnode, (char *)pvBuf, cbToRead, offNative, UIO_SYSSPACE, 0 /*ioflg*/,
                     vfs_context_ucred(pThis->hVfsCtx), &cbLeft, vfs_context_proc(pThis->hVfsCtx));
        *pcbRead = cbToRead - cbLeft;
    }
    IPRT_DARWIN_RESTORE_EFL_AC();
    return !rc ? VINF_SUCCESS : RTErrConvertFromErrno(rc);

#else
    uio_t hUio = uio_create(1, offNative, UIO_SYSSPACE, UIO_READ);
    if (!hUio)
    {
        IPRT_DARWIN_RESTORE_EFL_AC();
        return VERR_NO_MEMORY;
    }
    errno_t rc;
    if (uio_addiov(hUio, (user_addr_t)(uintptr_t)pvBuf, cbToRead) == 0)
    {
        rc = VNOP_READ(pThis->hVnode, hUio, 0 /*ioflg*/, pThis->hVfsCtx);
        off_t const cbActual = cbToRead - uio_resid(hUio);
        if (pcbRead)
            *pcbRead = cbActual;
        if (rc == 0)
        {
            pThis->offFile += (uint64_t)cbActual;
            if (cbToRead != (uint64_t)cbActual)
                rc = VERR_FILE_IO_ERROR;
        }
        else
            rc = RTErrConvertFromErrno(rc);
    }
    else
        rc = VERR_INTERNAL_ERROR_3;
    uio_free(hUio);
    IPRT_DARWIN_RESTORE_EFL_AC();
    return rc;
#endif
}


RTDECL(int) RTFileRead(RTFILE hFile, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RTFILEINT *pThis = hFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTFILE_MAGIC, VERR_INVALID_HANDLE);

    return RTFileReadAt(hFile, pThis->offFile, pvBuf, cbToRead, pcbRead);
}


RTDECL(int) RTFileQuerySize(RTFILE hFile, uint64_t *pcbSize)
{
    RTFILEINT *pThis = hFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTFILE_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Query the data size attribute.
     * Note! Allocate extra attribute buffer space to be on the safe side.
     */
    union
    {
        struct vnode_attr VAttr;
        uint8_t             abPadding[sizeof(struct vnode_attr) * 2];
    } uBuf;
    RT_ZERO(uBuf);
    struct vnode_attr *pVAttr = &uBuf.VAttr;

    VATTR_INIT(pVAttr);
    VATTR_WANTED(pVAttr, va_data_size);

    errno_t rc = vnode_getattr(pThis->hVnode, pVAttr, pThis->hVfsCtx);
    if (!rc)
    {
        *pcbSize = pVAttr->va_data_size;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(rc);
}


RTDECL(int) RTFileSeek(RTFILE hFile, int64_t offSeek, unsigned uMethod, uint64_t *poffActual)
{
    RTFILEINT *pThis = hFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTFILE_MAGIC, VERR_INVALID_HANDLE);

    uint64_t offNew;
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            AssertReturn(offSeek >= 0, VERR_NEGATIVE_SEEK);
            offNew = offSeek;
            break;

        case RTFILE_SEEK_CURRENT:
            offNew = pThis->offFile + offSeek;
            break;

        case RTFILE_SEEK_END:
        {
            uint64_t cbFile = 0;
            int rc = RTFileQuerySize(hFile, &cbFile);
            if (RT_SUCCESS(rc))
                offNew = cbFile + offSeek;
            else
                return rc;
            break;
        }

        default:
            return VERR_INVALID_PARAMETER;
    }

    if ((RTFOFF)offNew >= 0)
    {
        pThis->offFile = offNew;
        if (poffActual)
            *poffActual = offNew;
        return VINF_SUCCESS;
    }
    return VERR_NEGATIVE_SEEK;
}

