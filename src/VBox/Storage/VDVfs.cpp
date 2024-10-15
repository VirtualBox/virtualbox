/* $Id: VDVfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual Disk Container implementation. - VFS glue.
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
#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/file.h>
#include <iprt/sg.h>
#include <iprt/vfslowlevel.h>
#include <iprt/poll.h>
#include <VBox/vd.h>

#include "VDInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * The internal data of a DVM volume I/O stream.
 */
typedef struct VDVFSFILE
{
    /** The volume the VFS file belongs to. */
    PVDISK          pDisk;
    /** Current position. */
    uint64_t        offCurPos;
    /** Flags given during creation. */
    uint32_t        fFlags;
} VDVFSFILE;
/** Pointer to a the internal data of a DVM volume file. */
typedef VDVFSFILE *PVDVFSFILE;

/**
 * VD read helper taking care of unaligned accesses.
 *
 * @return  VBox status code.
 * @param   pDisk    VD disk container.
 * @param   off      Offset to start reading from.
 * @param   pvBuf    Pointer to the buffer to read into.
 * @param   cbRead   Amount of bytes to read.
 */
static int vdReadHelper(PVDISK pDisk, uint64_t off, void *pvBuf, size_t cbRead)
{
    int rc;

    /* Take direct route if the request is sector aligned. */
    uint64_t const offMisalign = off & 511;
    size_t   const cbMisalign  = (off + cbRead) & 511;
    if (   !offMisalign
        && !cbMisalign)
        rc = VDRead(pDisk, off, pvBuf, cbRead);
    else
    {
        uint8_t *pbBuf = (uint8_t *)pvBuf;
        uint8_t abBuf[512];

        /* Unaligned buffered read of head.  Aligns the offset. */
        if (offMisalign)
        {
            rc = VDRead(pDisk, off - offMisalign, abBuf, 512);
            if (RT_SUCCESS(rc))
            {
                size_t const cbPart = RT_MIN(512 - offMisalign, cbRead);
                memcpy(pbBuf, &abBuf[offMisalign], cbPart);
                pbBuf  += cbPart;
                off    += cbPart;
                cbRead -= cbPart;
            }
        }
        else
            rc = VINF_SUCCESS;

        /* Aligned direct read. */
        if (   RT_SUCCESS(rc)
            && cbRead >= 512)
        {
            Assert(!(off % 512));

            size_t cbPart = cbRead - cbMisalign;
            Assert(!(cbPart % 512));
            rc = VDRead(pDisk, off, pbBuf, cbPart);
            if (RT_SUCCESS(rc))
            {
                pbBuf  += cbPart;
                off    += cbPart;
                cbRead -= cbPart;
            }
        }

        /* Unaligned buffered read of tail. */
        if (   RT_SUCCESS(rc)
            && cbRead)
        {
            Assert(cbRead == cbMisalign);
            Assert(cbRead < 512);
            Assert(!(off % 512));

            rc = VDRead(pDisk, off, abBuf, 512);
            if (RT_SUCCESS(rc))
                memcpy(pbBuf, abBuf, cbRead);
        }
    }

    return rc;
}


/**
 * VD write helper taking care of unaligned accesses.
 *
 * @return  VBox status code.
 * @param   pDisk    VD disk container.
 * @param   off      Offset to start writing to.
 * @param   pvSrc    Pointer to the buffer to read from.
 * @param   cbWrite  Amount of bytes to write.
 */
static int vdWriteHelper(PVDISK pDisk, uint64_t off, const void *pvSrc, size_t cbWrite)
{
    uint8_t const *pbSrc = (uint8_t const *)pvSrc;
    uint8_t        abBuf[4096];
    int rc;

    /*
     * Take direct route if the request is sector aligned.
     */
    uint64_t const offMisalign = off & 511;
    size_t   const cbMisalign  = (off + cbWrite) & 511;
    if (   !offMisalign
        && !cbMisalign)
    {
        if (RTListIsEmpty(&pDisk->ListFilterChainWrite))
            rc = VDWrite(pDisk, off, pbSrc, cbWrite);
        else
        {
            /* Filtered writes must be double buffered as the filter may need to modify the input buffer directly. */
            do
            {
                size_t cbThisWrite = RT_MIN(cbWrite, sizeof(abBuf));
                rc = VDWrite(pDisk, off, memcpy(abBuf, pbSrc, cbThisWrite), cbThisWrite);
                if (RT_SUCCESS(rc))
                {
                    pbSrc   += cbThisWrite;
                    off     += cbThisWrite;
                    cbWrite -= cbThisWrite;
                }
                else
                    break;
            } while (cbWrite > 0);
        }
    }
    else
    {

        /*
         * Unaligned buffered read+write of head.  Aligns the offset.
         */
        if (offMisalign)
        {
            rc = VDRead(pDisk, off - offMisalign, abBuf, 512);
            if (RT_SUCCESS(rc))
            {
                size_t const cbPart = RT_MIN(512 - offMisalign, cbWrite);
                memcpy(&abBuf[offMisalign], pbSrc, cbPart);
                rc = VDWrite(pDisk, off - offMisalign, abBuf, 512);
                if (RT_SUCCESS(rc))
                {
                    pbSrc   += cbPart;
                    off     += cbPart;
                    cbWrite -= cbPart;
                }
            }
        }
        else
            rc = VINF_SUCCESS;

        /*
         * Aligned direct write.
         */
        if (   RT_SUCCESS(rc)
            && cbWrite >= 512)
        {
            Assert(!(off % 512));
            size_t cbPart = cbWrite - cbMisalign;
            Assert(!(cbPart % 512));

            if (RTListIsEmpty(&pDisk->ListFilterChainWrite))
            {
                rc = VDWrite(pDisk, off, pbSrc, cbPart);
                if (RT_SUCCESS(rc))
                {
                    pbSrc   += cbPart;
                    off     += cbPart;
                    cbWrite -= cbPart;
                }
            }
            else
            {
                /* Filtered writes must be double buffered as the filter may need to modify the input buffer directly. */
                do
                {
                    size_t cbThisWrite = RT_MIN(cbPart, sizeof(abBuf));
                    rc = VDWrite(pDisk, off, memcpy(abBuf, pbSrc, cbThisWrite), cbThisWrite);
                    if (RT_SUCCESS(rc))
                    {
                        pbSrc   += cbThisWrite;
                        off     += cbThisWrite;
                        cbWrite -= cbThisWrite;
                        cbPart  -= cbThisWrite;
                    }
                    else
                        break;
                } while (cbPart > 0);
            }
        }

        /*
         * Unaligned buffered read+write of tail.
         */
        if (   RT_SUCCESS(rc)
            && cbWrite > 0)
        {
            Assert(cbWrite == cbMisalign);
            Assert(cbWrite < 512);
            Assert(!(off % 512));

            rc = VDRead(pDisk, off, abBuf, 512);
            if (RT_SUCCESS(rc))
            {
                memcpy(abBuf, pbSrc, cbWrite);
                rc = VDWrite(pDisk, off, abBuf, 512);
            }
        }
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) vdVfsFile_Close(void *pvThis)
{
    PVDVFSFILE pThis = (PVDVFSFILE)pvThis;

    if (pThis->fFlags & VD_VFSFILE_DESTROY_ON_RELEASE)
        VDDestroy(pThis->pDisk);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) vdVfsFile_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PVDVFSFILE pThis = (PVDVFSFILE)pvThis;
    unsigned const cOpenImages = VDGetCount(pThis->pDisk);

    pObjInfo->cbObject    = VDGetSize(pThis->pDisk, cOpenImages - 1);
    pObjInfo->cbAllocated = 0;
    for (unsigned iImage = 0; iImage < cOpenImages; iImage++)
        pObjInfo->cbAllocated += VDGetFileSize(pThis->pDisk, iImage);

    /** @todo enumerate the disk images directly...   */
    RTTimeNow(&pObjInfo->AccessTime);
    pObjInfo->BirthTime        = pObjInfo->AccessTime;
    pObjInfo->ChangeTime       = pObjInfo->AccessTime;
    pObjInfo->ModificationTime = pObjInfo->AccessTime;

    pObjInfo->Attr.fMode       = RTFS_DOS_NT_NORMAL | RTFS_TYPE_FILE | 0644;
    pObjInfo->Attr.enmAdditional = enmAddAttr;
    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_UNIX:
            pObjInfo->Attr.u.Unix.uid           = NIL_RTUID;
            pObjInfo->Attr.u.Unix.gid           = NIL_RTGID;
            pObjInfo->Attr.u.Unix.cHardlinks    = 1;
            pObjInfo->Attr.u.Unix.INodeIdDevice = 0;
            pObjInfo->Attr.u.Unix.INodeId       = 0;
            pObjInfo->Attr.u.Unix.fFlags        = 0;
            pObjInfo->Attr.u.Unix.GenerationId  = 0;
            pObjInfo->Attr.u.Unix.Device        = 0;
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
            pObjInfo->Attr.u.UnixOwner.uid = NIL_RTUID;
            pObjInfo->Attr.u.UnixOwner.szName[0] = '\0';
            break;
        case RTFSOBJATTRADD_UNIX_GROUP:
            pObjInfo->Attr.u.UnixGroup.gid = NIL_RTGID;
            pObjInfo->Attr.u.UnixGroup.szName[0] = '\0';
            break;
        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.u.EASize.cb = 0;
            break;

        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) vdVfsFile_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PVDVFSFILE const pThis = (PVDVFSFILE)pvThis;

    Assert(pSgBuf->cSegs == 1);
    NOREF(fBlocking);

    /*
     * Find the current position and check if it's within the volume.
     */
    uint64_t offUnsigned = off < 0 ? pThis->offCurPos : (uint64_t)off;
    uint64_t const cbImage = VDGetSize(pThis->pDisk, VD_LAST_IMAGE);
    if (offUnsigned >= cbImage)
    {
        if (pcbRead)
        {
            *pcbRead = 0;
            pThis->offCurPos = cbImage;
            return VINF_EOF;
        }
        return VERR_EOF;
    }

    size_t       cbSeg = 0;
    void * const pvSeg = RTSgBufGetCurrentSegment(pSgBuf, ~(size_t)0, &cbSeg);

    int    rcRet = VINF_SUCCESS;
    size_t cbToRead;
    if (cbSeg <= cbImage - offUnsigned)
        cbToRead = cbSeg;
    else if (pcbRead)
    {
        cbToRead = (size_t)(cbImage - offUnsigned);
        rcRet = VINF_EOF;
    }
    else
        return VERR_EOF;

    /*
     * Ok, we've got a valid stretch within the file.  Do the reading.
     */
    if (cbToRead > 0)
    {
        int rc2 = vdReadHelper(pThis->pDisk, offUnsigned, pvSeg, cbToRead);
        if (RT_SUCCESS(rc2))
        {
            offUnsigned += cbToRead;
            RTSgBufAdvance(pSgBuf, cbToRead);
        }
        else
        {
            cbToRead = 0;
            rcRet = rc2;
        }
    }

    pThis->offCurPos = offUnsigned;
    if (pcbRead)
        *pcbRead = cbToRead;
    return rcRet;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) vdVfsFile_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    PVDVFSFILE const pThis = (PVDVFSFILE)pvThis;

    Assert(pSgBuf->cSegs == 1);
    NOREF(fBlocking);

    /*
     * Find the current position and check if it's within the volume.
     * Writing beyond the end of a volume is not supported.
     */
    uint64_t offUnsigned = off < 0 ? pThis->offCurPos : (uint64_t)off;
    uint64_t const cbImage = VDGetSize(pThis->pDisk, VD_LAST_IMAGE);
    if (offUnsigned >= cbImage)
    {
        if (pcbWritten)
        {
            *pcbWritten = 0;
            pThis->offCurPos = cbImage;
        }
        return VERR_EOF;
    }

    size_t             cbSeg = 0;
    void const * const pvSeg = RTSgBufGetCurrentSegment(pSgBuf, ~(size_t)0, &cbSeg);

    size_t cbToWrite;
    if (cbSeg <= cbImage - offUnsigned)
        cbToWrite = cbSeg;
    else if (pcbWritten)
        cbToWrite = (size_t)(cbImage - offUnsigned);
    else
        return VERR_EOF;

    /*
     * Ok, we've got a valid stretch within the file.  Do the reading.
     */
    int rc = VINF_SUCCESS;
    if (cbToWrite > 0)
    {
        rc = vdWriteHelper(pThis->pDisk, offUnsigned, pvSeg, cbToWrite);
        if (RT_SUCCESS(rc))
        {
            offUnsigned += cbToWrite;
            RTSgBufAdvance(pSgBuf, cbToWrite);
        }
        else
            cbToWrite = 0;
    }

    pThis->offCurPos = offUnsigned;
    if (pcbWritten)
        *pcbWritten = cbToWrite;
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) vdVfsFile_Flush(void *pvThis)
{
    PVDVFSFILE pThis = (PVDVFSFILE)pvThis;
    return VDFlush(pThis->pDisk);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) vdVfsFile_Tell(void *pvThis, PRTFOFF poffActual)
{
    PVDVFSFILE pThis = (PVDVFSFILE)pvThis;
    *poffActual = pThis->offCurPos;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetMode}
 */
static DECLCALLBACK(int) vdVfsFile_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    NOREF(pvThis);
    NOREF(fMode);
    NOREF(fMask);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) vdVfsFile_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                              PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    NOREF(pvThis);
    NOREF(pAccessTime);
    NOREF(pModificationTime);
    NOREF(pChangeTime);
    NOREF(pBirthTime);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) vdVfsFile_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    NOREF(pvThis);
    NOREF(uid);
    NOREF(gid);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) vdVfsFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PVDVFSFILE pThis = (PVDVFSFILE)pvThis;

    /*
     * Seek relative to which position.
     */
    uint64_t offWrt;
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            offWrt = 0;
            break;

        case RTFILE_SEEK_CURRENT:
            offWrt = pThis->offCurPos;
            break;

        case RTFILE_SEEK_END:
            offWrt = VDGetSize(pThis->pDisk, VD_LAST_IMAGE);
            break;

        default:
            return VERR_INTERNAL_ERROR_5;
    }

    /*
     * Calc new position, take care to stay without bounds.
     */
    uint64_t offNew;
    if (offSeek == 0)
        offNew = offWrt;
    else if (offSeek > 0)
    {
        offNew = offWrt + offSeek;
        if (   offNew < offWrt
            || offNew > RTFOFF_MAX)
            offNew = RTFOFF_MAX;
    }
    else if ((uint64_t)-offSeek < offWrt)
        offNew = offWrt + offSeek;
    else
        offNew = 0;

    /*
     * Update the state and set return value.
     */
    pThis->offCurPos = offNew;

    *poffActual = offNew;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) vdVfsFile_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PVDVFSFILE pThis = (PVDVFSFILE)pvThis;
    *pcbFile = VDGetSize(pThis->pDisk, VD_LAST_IMAGE);
    return VINF_SUCCESS;
}


/**
 * Standard file operations.
 */
DECL_HIDDEN_CONST(const RTVFSFILEOPS) g_vdVfsStdFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "VDFile",
            vdVfsFile_Close,
            vdVfsFile_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        vdVfsFile_Read,
        vdVfsFile_Write,
        vdVfsFile_Flush,
        NULL /*PollOne*/,
        vdVfsFile_Tell,
        NULL /*Skip*/,
        NULL /*ZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    /*RTVFSIOFILEOPS_FEAT_NO_AT_OFFSET*/ 0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        vdVfsFile_SetMode,
        vdVfsFile_SetTimes,
        vdVfsFile_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    vdVfsFile_Seek,
    vdVfsFile_QuerySize,
    NULL /*SetSize*/,
    NULL /*QueryMaxSize*/,
    RTVFSFILEOPS_VERSION
};


VBOXDDU_DECL(int) VDCreateVfsFileFromDisk(PVDISK pDisk, uint32_t fFlags,
                                          PRTVFSFILE phVfsFile)
{
    AssertPtrReturn(pDisk, VERR_INVALID_HANDLE);
    AssertPtrReturn(phVfsFile, VERR_INVALID_POINTER);
    AssertReturn((fFlags & ~VD_VFSFILE_FLAGS_MASK) == 0, VERR_INVALID_PARAMETER);

    /*
     * Create the volume file.
     */
    RTVFSFILE  hVfsFile;
    PVDVFSFILE pThis;
    int rc = RTVfsNewFile(&g_vdVfsStdFileOps, sizeof(*pThis), RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_WRITE,
                          NIL_RTVFS, NIL_RTVFSLOCK, &hVfsFile, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->offCurPos        = 0;
        pThis->pDisk            = pDisk;
        pThis->fFlags           = fFlags;

        *phVfsFile = hVfsFile;
        return VINF_SUCCESS;
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) vdVfsChain_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
                                             PRTVFSCHAINELEMSPEC pElement, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec);

    /*
     * Basic checks.
     */
    if (pElement->enmTypeIn != RTVFSOBJTYPE_INVALID)
        return VERR_VFS_CHAIN_MUST_BE_FIRST_ELEMENT;
    if (   pElement->enmType != RTVFSOBJTYPE_FILE
        && pElement->enmType != RTVFSOBJTYPE_IO_STREAM)
        return VERR_VFS_CHAIN_ONLY_FILE_OR_IOS;

    if (pElement->cArgs < 1)
        return VERR_VFS_CHAIN_AT_LEAST_ONE_ARG;

    /*
     * Parse the flag if present, save in pElement->uProvider.
     */
    uint32_t fFlags = (pSpec->fOpenFile & RTFILE_O_ACCESS_MASK) == RTFILE_O_READ
                    ? VD_OPEN_FLAGS_READONLY : VD_OPEN_FLAGS_NORMAL;
    if (pElement->cArgs > 1)
    {
        pElement->paArgs[pElement->cArgs - 1].uProvider = true; /* indicates flags  */
        const char *psz = pElement->paArgs[pElement->cArgs - 1].psz;
        if (*psz)
        {
            if (   !strcmp(psz, "ro")
                || !strcmp(psz, "r"))
            {
                fFlags &= ~(VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_NORMAL);
                fFlags |= VD_OPEN_FLAGS_READONLY;
            }
            else if (!strcmp(psz, "rw"))
            {
                fFlags &= ~(VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_NORMAL);
                fFlags |= VD_OPEN_FLAGS_NORMAL;
            }
            else if (strlen(psz) <= 4)
            {
                *poffError = pElement->paArgs[pElement->cArgs - 1].offSpec;
                return RTErrInfoSet(pErrInfo, VERR_VFS_CHAIN_INVALID_ARGUMENT, "Expected 'ro' or 'rw' as argument");
            }
            else
                pElement->paArgs[pElement->cArgs - 1].uProvider = false; /* indicates no flags  */
        }
    }

    pElement->uProvider = fFlags;
    if (   pElement->cArgs > 2
        || (pElement->cArgs == 2 && !pElement->paArgs[pElement->cArgs - 1].uProvider))
           pElement->uProvider |= RT_BIT_64(63);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnInstantiate}
 */
static DECLCALLBACK(int) vdVfsChain_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, poffError, pErrInfo);
    AssertReturn(hPrevVfsObj == NIL_RTVFSOBJ, VERR_VFS_CHAIN_IPE);

    /* Determin the format. */
    char  *pszFormat = NULL;
    VDTYPE enmType   = VDTYPE_INVALID;
    int rc = VDGetFormat(NULL, NULL, pElement->paArgs[0].psz, VDTYPE_INVALID, &pszFormat, &enmType);
    if (RT_SUCCESS(rc))
    {
        PVDISK pDisk = NULL;
        rc = VDCreate(NULL, enmType, &pDisk);
        if (RT_SUCCESS(rc))
        {
            if (!(pElement->uProvider & RT_BIT_64(63)))
                rc = VDOpen(pDisk, pszFormat, pElement->paArgs[0].psz, (uint32_t)pElement->uProvider, NULL);
            else
            {
                uint32_t cChain = pElement->cArgs;
                if (pElement->cArgs >= 2 && pElement->paArgs[pElement->cArgs - 1].uProvider != 0)
                    cChain--;
                uint32_t const  fFinal    = (uint32_t)pElement->uProvider;
                uint32_t const  fReadOnly = (fFinal & ~(VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_NORMAL)) | VD_OPEN_FLAGS_READONLY;
                uint32_t        iChain;
                for (iChain = 0; iChain < cChain && RT_SUCCESS(rc); iChain++)
                    rc = VDOpen(pDisk, pszFormat, pElement->paArgs[iChain].psz, iChain + 1 >= cChain ? fFinal : fReadOnly, NULL);
            }
            if (RT_SUCCESS(rc))
            {
                RTVFSFILE hVfsFile;
                rc = VDCreateVfsFileFromDisk(pDisk, VD_VFSFILE_DESTROY_ON_RELEASE, &hVfsFile);
                if (RT_SUCCESS(rc))
                {
                    RTStrFree(pszFormat);

                    *phVfsObj = RTVfsObjFromFile(hVfsFile);
                    RTVfsFileRelease(hVfsFile);

                    if (*phVfsObj != NIL_RTVFSOBJ)
                        return VINF_SUCCESS;
                    return VERR_VFS_CHAIN_CAST_FAILED;
                }
            }
            VDDestroy(pDisk);
        }
        RTStrFree(pszFormat);
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnCanReuseElement}
 */
static DECLCALLBACK(bool) vdVfsChain_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                     PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                     PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pElement, pReuseSpec, pReuseElement);
    return false;
}


/** VFS chain element 'file'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainIsoFsVolReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "vd",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Opens a container image using the VD API.\n"
                                "To open a snapshot chain, start with the root image and end with the more recent diff image.\n"
                                "The final argument can be a flag 'ro' or 'r' for read-only, 'rw' for read-write.",
    /* pfnValidate = */         vdVfsChain_Validate,
    /* pfnInstantiate = */      vdVfsChain_Instantiate,
    /* pfnCanReuseElement = */  vdVfsChain_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainIsoFsVolReg, rtVfsChainIsoFsVolReg);

