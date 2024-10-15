/* $Id: vfsmemory.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Virtual File System, Memory Backed VFS.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include "internal/iprt.h"
#include <iprt/vfs.h>

#include <iprt/asm-mem.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/list.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/vfslowlevel.h>



/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/vfs.h>

#include <iprt/err.h>
#include <iprt/mem.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The max extent size. */
#define RTVFSMEM_MAX_EXTENT_SIZE    _2M


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Memory base object info.
 */
typedef struct RTVFSMEMBASE
{
    /** The basic object info. */
    RTFSOBJINFO         ObjInfo;
} RTVFSMEMBASE;


/**
 * Memory file extent.
 *
 * This stores part of the file content.
 */
typedef struct RTVFSMEMEXTENT
{
    /** Extent list entry. */
    RTLISTNODE          Entry;
    /** The offset of this extent within the file. */
    uint64_t            off;
    /** The size of the this extent. */
    uint32_t            cb;
    /** The data. */
    uint8_t             abData[1];
} RTVFSMEMEXTENT;
/** Pointer to a memory file extent. */
typedef RTVFSMEMEXTENT *PRTVFSMEMEXTENT;

/**
 * Memory file.
 */
typedef struct RTVFSMEMFILE
{
    /** The base info. */
    RTVFSMEMBASE        Base;
    /** The current file position. */
    uint64_t            offCurPos;
    /** Pointer to the current file extent. */
    PRTVFSMEMEXTENT     pCurExt;
    /** Linked list of file extents - RTVFSMEMEXTENT. */
    RTLISTANCHOR        ExtentHead;
    /** The current extent size.
     * This is slowly grown to RTVFSMEM_MAX_EXTENT_SIZE as the file grows.  */
    uint32_t            cbExtent;
} RTVFSMEMFILE;
/** Pointer to a memory file. */
typedef RTVFSMEMFILE *PRTVFSMEMFILE;



/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtVfsMemFile_Close(void *pvThis)
{
    PRTVFSMEMFILE pThis = (PRTVFSMEMFILE)pvThis;

    /*
     * Free the extent list.
     */
    PRTVFSMEMEXTENT pCur, pNext;
    RTListForEachSafe(&pThis->ExtentHead, pCur, pNext, RTVFSMEMEXTENT, Entry)
    {
        pCur->off       = RTFOFF_MAX;
        pCur->cb        = UINT32_MAX;
        RTListNodeRemove(&pCur->Entry);
        RTMemFree(pCur);
    }
    pThis->pCurExt = NULL;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtVfsMemFile_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTVFSMEMFILE pThis = (PRTVFSMEMFILE)pvThis;
    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_NOTHING:
        case RTFSOBJATTRADD_UNIX:
            *pObjInfo = pThis->Base.ObjInfo;
            return VINF_SUCCESS;

        default:
            return VERR_NOT_SUPPORTED;
    }
}


/**
 * The slow paths of rtVfsMemFile_LocateExtent.
 *
 * @copydoc rtVfsMemFile_LocateExtent
 */
static PRTVFSMEMEXTENT rtVfsMemFile_LocateExtentSlow(PRTVFSMEMFILE pThis, uint64_t off, bool *pfHit)
{
    /*
     * Search from the start or the previously used extent.  The heuristics
     * are very very simple, but whatever.
     */
    PRTVFSMEMEXTENT pExtent = pThis->pCurExt;
    if (!pExtent || off < pExtent->off)
    {
        /* Consider the last entry first (for writes). */
        pExtent = RTListGetLast(&pThis->ExtentHead, RTVFSMEMEXTENT, Entry);
        if (!pExtent)
        {
            *pfHit = false;
            return NULL;
        }
        if (off - pExtent->off < pExtent->cb)
        {
            *pfHit = true;
            pThis->pCurExt = pExtent;
            return pExtent;
        }

        /* Otherwise, start from the head after making sure it is not an
           offset before the first extent. */
        pExtent = RTListGetFirst(&pThis->ExtentHead, RTVFSMEMEXTENT, Entry);
        if (off < pExtent->off)
        {
            *pfHit = false;
            return pExtent;
        }
    }

    while (off - pExtent->off >= pExtent->cb)
    {
        Assert(pExtent->off <= off);
        PRTVFSMEMEXTENT pNext = RTListGetNext(&pThis->ExtentHead, pExtent, RTVFSMEMEXTENT, Entry);
        if (   !pNext
            || pNext->off > off)
        {
            *pfHit = false;
            return pNext;
        }

        pExtent = pNext;
    }

    *pfHit = true;
    pThis->pCurExt = pExtent;
    return pExtent;
}


/**
 * Locates the extent covering the specified offset, or the one after it.
 *
 * @returns The closest extent.  NULL if off is 0 and there are no extent
 *          covering byte 0 yet.
 * @param   pThis               The memory file.
 * @param   off                 The offset (0-positive).
 * @param   pfHit               Where to indicate whether the extent is a
 *                              direct hit (@c true) or just a closest match
 *                              (@c false).
 */
DECLINLINE(PRTVFSMEMEXTENT) rtVfsMemFile_LocateExtent(PRTVFSMEMFILE pThis, uint64_t off, bool *pfHit)
{
    /*
     * The most likely case is that we're hitting the extent we used in the
     * previous access or the one immediately following it.
     */
    PRTVFSMEMEXTENT pExtent = pThis->pCurExt;
    if (!pExtent)
        return rtVfsMemFile_LocateExtentSlow(pThis, off, pfHit);

    if (off - pExtent->off >= pExtent->cb)
    {
        pExtent = RTListGetNext(&pThis->ExtentHead, pExtent, RTVFSMEMEXTENT, Entry);
        if (   !pExtent
            || off - pExtent->off >= pExtent->cb)
            return rtVfsMemFile_LocateExtentSlow(pThis, off, pfHit);
        pThis->pCurExt = pExtent;
    }

    *pfHit = true;
    return pExtent;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtVfsMemFile_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTVFSMEMFILE pThis = (PRTVFSMEMFILE)pvThis;

    Assert(pSgBuf->cSegs == 1);
    NOREF(fBlocking);

    /*
     * Find the current position and check if it's within the file.
     */
    uint64_t offUnsigned = off < 0 ? pThis->offCurPos : (uint64_t)off;
    if (offUnsigned >= (uint64_t)pThis->Base.ObjInfo.cbObject)
    {
        if (pcbRead)
        {
            *pcbRead = 0;
            pThis->offCurPos = offUnsigned;
            return VINF_EOF;
        }
        return VERR_EOF;
    }

    size_t cbLeftToRead;
    if (offUnsigned + pSgBuf->paSegs[0].cbSeg > (uint64_t)pThis->Base.ObjInfo.cbObject)
    {
        if (!pcbRead)
            return VERR_EOF;
        *pcbRead = cbLeftToRead = (size_t)((uint64_t)pThis->Base.ObjInfo.cbObject - offUnsigned);
    }
    else
    {
        cbLeftToRead = pSgBuf->paSegs[0].cbSeg;
        if (pcbRead)
            *pcbRead = cbLeftToRead;
    }

    /*
     * Ok, we've got a valid stretch within the file.  Do the reading.
     */
    if (cbLeftToRead > 0)
    {
        RTSgBufAdvance(pSgBuf, cbLeftToRead);

        uint8_t        *pbDst   = (uint8_t *)pSgBuf->paSegs[0].pvSeg;
        bool            fHit;
        PRTVFSMEMEXTENT pExtent = rtVfsMemFile_LocateExtent(pThis, offUnsigned, &fHit);
        for (;;)
        {
            size_t      cbThisRead;

            /*
             * Do we hit an extent covering the current file surface?
             */
            if (fHit)
            {
                /* Yes, copy the data. */
                Assert(offUnsigned - pExtent->off < pExtent->cb);
                size_t const offExtent = (size_t)(offUnsigned - pExtent->off);
                cbThisRead = pExtent->cb - offExtent;
                if (cbThisRead >= cbLeftToRead)
                    cbThisRead = cbLeftToRead;

                memcpy(pbDst, &pExtent->abData[offUnsigned - pExtent->off], cbThisRead);

                offUnsigned  += cbThisRead;
                cbLeftToRead -= cbThisRead;
                if (!cbLeftToRead)
                    break;
                pbDst        += cbThisRead;

                /* Advance, looping immediately if not sparse. */
                PRTVFSMEMEXTENT pNext = RTListGetNext(&pThis->ExtentHead, pExtent, RTVFSMEMEXTENT, Entry);
                if (   pNext
                    && pNext->off == pExtent->off + pExtent->cb)
                {
                    pExtent = pNext;
                    continue;
                }

                Assert(!pNext || pNext->off > pExtent->off);
                pExtent = pNext;
                fHit = false;
            }
            else
                Assert(!pExtent || pExtent->off > offUnsigned);

            /*
             * No extent of this portion (sparse file) - Read zeros.
             */
            if (   !pExtent
                || offUnsigned + cbLeftToRead <= pExtent->off)
                cbThisRead = cbLeftToRead;
            else
                cbThisRead = (size_t)(pExtent->off - offUnsigned);

            RT_BZERO(pbDst, cbThisRead);

            offUnsigned  += cbThisRead;
            cbLeftToRead -= cbThisRead;
            if (!cbLeftToRead)
                break;
            pbDst        += cbThisRead;

            /* Go on and read content from the next extent. */
            fHit = true;
        }
    }

    pThis->offCurPos = offUnsigned;
    return VINF_SUCCESS;
}


/**
 * Allocates a new extent covering the ground at @a offUnsigned.
 *
 * @returns Pointer to the new extent on success, NULL if we're out of memory.
 * @param   pThis               The memory file.
 * @param   offUnsigned         The location to allocate the extent at.
 * @param   cbToWrite           The number of bytes we're interested in writing
 *                              starting at @a offUnsigned.
 * @param   pNext               The extention after @a offUnsigned.  NULL if
 *                              none, i.e. we're allocating space at the end of
 *                              the file.
 */
static PRTVFSMEMEXTENT rtVfsMemFile_AllocExtent(PRTVFSMEMFILE pThis, uint64_t offUnsigned, size_t cbToWrite,
                                                PRTVFSMEMEXTENT pNext)
{
    /*
     * Adjust the extent size if we haven't reached the max size yet.
     */
    if (pThis->cbExtent != RTVFSMEM_MAX_EXTENT_SIZE)
    {
        if (cbToWrite >= RTVFSMEM_MAX_EXTENT_SIZE)
            pThis->cbExtent = RTVFSMEM_MAX_EXTENT_SIZE;
        else if (!RTListIsEmpty(&pThis->ExtentHead))
        {
            uint32_t cbNextExtent = pThis->cbExtent;
            if (RT_IS_POWER_OF_TWO(cbNextExtent))
                cbNextExtent *= 2;
            else
            {
                /* Make it a power of two (seeRTVfsMemorizeIoStreamAsFile). */
                cbNextExtent = _4K;
                while (cbNextExtent < pThis->cbExtent)
                    cbNextExtent *= 2;
            }
            if (((pThis->Base.ObjInfo.cbAllocated + cbNextExtent) & (cbNextExtent - 1)) == 0)
                pThis->cbExtent = cbNextExtent;
        }
    }

    /*
     * Figure out the size and position of the extent we're adding.
     */
    uint64_t        offExtent = offUnsigned & ~(uint64_t)(pThis->cbExtent - 1);
    uint32_t        cbExtent  = pThis->cbExtent;

    PRTVFSMEMEXTENT pPrev     = pNext
                              ? RTListGetPrev(&pThis->ExtentHead, pNext, RTVFSMEMEXTENT, Entry)
                              : RTListGetLast(&pThis->ExtentHead, RTVFSMEMEXTENT, Entry);
    uint64_t const  offPrev   = pPrev ? pPrev->off + pPrev->cb : 0;
    if (offExtent < offPrev)
        offExtent = offPrev;

    if (pNext)
    {
        uint64_t cbMaxExtent = pNext->off - offExtent;
        if (cbMaxExtent < cbExtent)
            cbExtent = (uint32_t)cbMaxExtent;
    }

    /*
     * Allocate, initialize and insert the new extent.
     */
    PRTVFSMEMEXTENT pNew = (PRTVFSMEMEXTENT)RTMemAllocZ(RT_UOFFSETOF_DYN(RTVFSMEMEXTENT, abData[cbExtent]));
    if (pNew)
    {
        pNew->off = offExtent;
        pNew->cb  = cbExtent;
        if (pPrev)
            RTListNodeInsertAfter(&pPrev->Entry, &pNew->Entry);
        else
            RTListPrepend(&pThis->ExtentHead, &pNew->Entry);

        pThis->Base.ObjInfo.cbAllocated += cbExtent;
    }
    /** @todo retry with minimum size. */

    return pNew;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtVfsMemFile_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    PRTVFSMEMFILE pThis = (PRTVFSMEMFILE)pvThis;

    Assert(pSgBuf->cSegs == 1);
    NOREF(fBlocking);

    /*
     * Validate the write and set up the write loop.
     */
    size_t          cbLeftToWrite = pSgBuf->paSegs[0].cbSeg;
    if (!cbLeftToWrite)
        return VINF_SUCCESS; /* pcbWritten is already 0. */
    uint64_t        offUnsigned = off < 0 ? pThis->offCurPos : (uint64_t)off;
    if (offUnsigned + cbLeftToWrite >= (uint64_t)RTFOFF_MAX)
        return VERR_OUT_OF_RANGE;

    int             rc      = VINF_SUCCESS;
    uint8_t const  *pbSrc   = (uint8_t const *)pSgBuf->paSegs[0].pvSeg;
    bool            fHit;
    PRTVFSMEMEXTENT pExtent = rtVfsMemFile_LocateExtent(pThis, offUnsigned, &fHit);
    for (;;)
    {
        /*
         * If we didn't hit an extent, allocate one (unless it's all zeros).
         */
        if (!fHit)
        {
            Assert(!pExtent || pExtent->off > offUnsigned);

            /* Skip leading zeros if there is a whole bunch of them. */
            uint8_t const *pbSrcNZ = (uint8_t const *)ASMMemFirstNonZero(pbSrc, cbLeftToWrite);
            size_t         cbZeros = pbSrcNZ ? pbSrcNZ - pbSrc            : cbLeftToWrite;
            if (cbZeros)
            {
                uint64_t const cbToNext = pExtent ? pExtent->off - offUnsigned : UINT64_MAX;
                if (cbZeros > cbToNext)
                    cbZeros = (size_t)cbToNext;
                offUnsigned   += cbZeros;
                cbLeftToWrite -= cbZeros;
                if (!cbLeftToWrite)
                    break;
                pbSrc += cbZeros;

                Assert(!pExtent || offUnsigned <= pExtent->off);
                if (pExtent && pExtent->off == offUnsigned)
                {
                    fHit = true;
                    continue;
                }
            }

            fHit    = true;
            pExtent = rtVfsMemFile_AllocExtent(pThis, offUnsigned, cbLeftToWrite, pExtent);
            if (!pExtent)
            {
                rc = VERR_NO_MEMORY;
                break;
            }
        }
        Assert(offUnsigned - pExtent->off < pExtent->cb);

        /*
         * Copy the source data into the current extent.
         */
        uint32_t const  offDst      = (uint32_t)(offUnsigned - pExtent->off);
        uint32_t        cbThisWrite = pExtent->cb - offDst;
        if (cbThisWrite > cbLeftToWrite)
            cbThisWrite = (uint32_t)cbLeftToWrite;
        memcpy(&pExtent->abData[offDst], pbSrc, cbThisWrite);

        offUnsigned   += cbThisWrite;
        cbLeftToWrite -= cbThisWrite;
        if (!cbLeftToWrite)
            break;
        pbSrc += cbThisWrite;
        Assert(offUnsigned == pExtent->off + pExtent->cb);

        /*
         * Advance to the next extent (emulate the lookup).
         */
        pExtent = RTListGetNext(&pThis->ExtentHead, pExtent, RTVFSMEMEXTENT, Entry);
        fHit = pExtent && (offUnsigned - pExtent->off < pExtent->cb);
    }

    /*
     * Update the state, set return value and return.
     * Note! There must be no alternative exit path from the loop above.
     */
    pThis->offCurPos = offUnsigned;
    if ((uint64_t)pThis->Base.ObjInfo.cbObject < offUnsigned)
        pThis->Base.ObjInfo.cbObject = offUnsigned;

    size_t const cbWritten = pSgBuf->paSegs[0].cbSeg - cbLeftToWrite;
    if (pcbWritten)
        *pcbWritten = cbWritten;
    RTSgBufAdvance(pSgBuf, cbWritten);
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtVfsMemFile_Flush(void *pvThis)
{
    NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtVfsMemFile_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTVFSMEMFILE pThis = (PRTVFSMEMFILE)pvThis;
    *poffActual = pThis->offCurPos;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtVfsMemFile_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    PRTVFSMEMFILE pThis = (PRTVFSMEMFILE)pvThis;
    pThis->Base.ObjInfo.Attr.fMode = (pThis->Base.ObjInfo.Attr.fMode & ~fMask) | fMode;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtVfsMemFile_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                               PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    PRTVFSMEMFILE pThis = (PRTVFSMEMFILE)pvThis;

    if (pAccessTime)
        pThis->Base.ObjInfo.AccessTime          = *pAccessTime;
    if (pModificationTime)
        pThis->Base.ObjInfo.ModificationTime    = *pModificationTime;
    if (pChangeTime)
        pThis->Base.ObjInfo.ChangeTime          = *pChangeTime;
    if (pBirthTime)
        pThis->Base.ObjInfo.BirthTime           = *pBirthTime;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtVfsMemFile_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    PRTVFSMEMFILE pThis = (PRTVFSMEMFILE)pvThis;

    if (uid != NIL_RTUID)
        pThis->Base.ObjInfo.Attr.u.Unix.uid = uid;
    if (gid != NIL_RTUID)
        pThis->Base.ObjInfo.Attr.u.Unix.gid = gid;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtVfsMemFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTVFSMEMFILE pThis = (PRTVFSMEMFILE)pvThis;

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
            offWrt = pThis->Base.ObjInfo.cbObject;
            break;

        default:
            return VERR_INTERNAL_ERROR_5;
    }

    /*
     * Calc new position, take care to stay within RTFOFF type bounds.
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
    if (   pThis->pCurExt
        && pThis->pCurExt->off - offNew >= pThis->pCurExt->cb)
        pThis->pCurExt = NULL;
    pThis->offCurPos = offNew;

    *poffActual = offNew;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) rtVfsMemFile_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTVFSMEMFILE pThis = (PRTVFSMEMFILE)pvThis;
    *pcbFile = pThis->Base.ObjInfo.cbObject;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSetSize}
 */
static DECLCALLBACK(int) rtVfsMemFile_SetSize(void *pvThis, uint64_t cbFile, uint32_t fFlags)
{
    AssertReturn(RTVFSFILE_SIZE_F_IS_VALID(fFlags), VERR_INVALID_PARAMETER);

    PRTVFSMEMFILE pThis = (PRTVFSMEMFILE)pvThis;
    if ((fFlags & RTVFSFILE_SIZE_F_ACTION_MASK) == RTVFSFILE_SIZE_F_NORMAL)
    {
        if ((RTFOFF)cbFile < pThis->Base.ObjInfo.cbObject)
        {
            /* Remove any extent beyond the file size. */
            bool            fHit;
            PRTVFSMEMEXTENT pExtent = rtVfsMemFile_LocateExtent(pThis, cbFile, &fHit);
            if (   fHit
                && cbFile < (pExtent->off + pExtent->cb))
            {
                /* Clear the data in this extent. */
                uint64_t cbRemaining = cbFile - pExtent->off;
                memset(&pExtent->abData[cbRemaining], 0, pExtent->cb - cbRemaining);
                pExtent = RTListGetNext(&pThis->ExtentHead, pExtent, RTVFSMEMEXTENT, Entry);
            }

            while (pExtent)
            {
                PRTVFSMEMEXTENT pFree = pExtent;
                pExtent = RTListGetNext(&pThis->ExtentHead, pExtent, RTVFSMEMEXTENT, Entry);

                RTListNodeRemove(&pFree->Entry);
                RTMemFree(pFree);
            }

            pThis->pCurExt = NULL;
        }
        /* else: Growing is just a matter of increasing the size of the object. */

        pThis->Base.ObjInfo.cbObject = cbFile;
        return VINF_SUCCESS;
    }

    AssertMsgFailed(("Lucky you! You get to implement this (or bug bird about it).\n"));
    return VERR_NOT_IMPLEMENTED;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQueryMaxSize}
 */
static DECLCALLBACK(int) rtVfsMemFile_QueryMaxSize(void *pvThis, uint64_t *pcbMax)
{
    RT_NOREF(pvThis);
    *pcbMax = ~(size_t)0 >> 1;
    return VINF_SUCCESS;
}


/**
 * Memory file operations.
 */
DECL_HIDDEN_CONST(const RTVFSFILEOPS) g_rtVfsMemFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "MemFile",
            rtVfsMemFile_Close,
            rtVfsMemFile_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        rtVfsMemFile_Read,
        rtVfsMemFile_Write,
        rtVfsMemFile_Flush,
        NULL /*PollOne*/,
        rtVfsMemFile_Tell,
        NULL /*Skip*/,
        NULL /*ZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    /*RTVFSIOFILEOPS_FEAT_NO_AT_OFFSET*/ 0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        rtVfsMemFile_SetMode,
        rtVfsMemFile_SetTimes,
        rtVfsMemFile_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtVfsMemFile_Seek,
    rtVfsMemFile_QuerySize,
    rtVfsMemFile_SetSize,
    rtVfsMemFile_QueryMaxSize,
    RTVFSFILEOPS_VERSION
};


/**
 * Initialize the RTVFSMEMFILE::Base.ObjInfo  specific members.
 *
 * @param   pObjInfo        The object info to init.
 * @param   cbObject        The object size set.
 */
static void rtVfsMemInitObjInfo(PRTFSOBJINFO pObjInfo, uint64_t cbObject)
{
    pObjInfo->cbObject                  = cbObject;
    pObjInfo->cbAllocated               = cbObject;
    pObjInfo->Attr.fMode                = RTFS_DOS_NT_NORMAL | RTFS_TYPE_FILE | RTFS_UNIX_IRWXU;
    pObjInfo->Attr.enmAdditional        = RTFSOBJATTRADD_UNIX;
    pObjInfo->Attr.u.Unix.uid           = NIL_RTUID;
    pObjInfo->Attr.u.Unix.gid           = NIL_RTGID;
    pObjInfo->Attr.u.Unix.cHardlinks    = 1;
    pObjInfo->Attr.u.Unix.INodeIdDevice = 0;
    pObjInfo->Attr.u.Unix.INodeId       = 0;
    pObjInfo->Attr.u.Unix.fFlags        = 0;
    pObjInfo->Attr.u.Unix.GenerationId  = 0;
    pObjInfo->Attr.u.Unix.Device        = 0;
    RTTimeNow(&pObjInfo->AccessTime);
    pObjInfo->ModificationTime          = pObjInfo->AccessTime;
    pObjInfo->ChangeTime                = pObjInfo->AccessTime;
    pObjInfo->BirthTime                 = pObjInfo->AccessTime;
}


/**
 * Initialize the RTVFSMEMFILE specific members.
 *
 * @param   pThis           The memory file to initialize.
 * @param   cbObject        The object size for estimating extent size.
 * @param   fFlags          The user specified flags.
 */
static void rtVfsMemFileInit(PRTVFSMEMFILE pThis, RTFOFF cbObject, uint32_t fFlags)
{
    pThis->offCurPos    = 0;
    pThis->pCurExt      = NULL;
    RTListInit(&pThis->ExtentHead);
    if (cbObject <= 0)
        pThis->cbExtent = _4K;
    else if (cbObject < RTVFSMEM_MAX_EXTENT_SIZE)
        pThis->cbExtent = fFlags & RTFILE_O_WRITE ? _4K : cbObject;
    else
        pThis->cbExtent = RTVFSMEM_MAX_EXTENT_SIZE;
}


/**
 * Rewinds the file to position 0 and clears the WRITE flag if necessary.
 *
 * @param   pThis           The memory file instance.
 * @param   fFlags          The user specified flags.
 */
static void rtVfsMemFileResetAndFixWriteFlag(PRTVFSMEMFILE pThis, uint32_t fFlags)
{
    pThis->pCurExt   = RTListGetFirst(&pThis->ExtentHead, RTVFSMEMEXTENT, Entry);
    pThis->offCurPos = 0;

    if (!(fFlags & RTFILE_O_WRITE))
    {
        /** @todo clear RTFILE_O_WRITE from the resulting. */
    }
}


RTDECL(int) RTVfsMemFileCreate(RTVFSIOSTREAM hVfsIos, size_t cbEstimate, PRTVFSFILE phVfsFile)
{
    /*
     * Create a memory file instance and set the extension size according to the
     * buffer size.  Add the WRITE flag so we can use normal write APIs for
     * copying the buffer.
     */
    RTVFSFILE       hVfsFile;
    PRTVFSMEMFILE   pThis;
    int rc = RTVfsNewFile(&g_rtVfsMemFileOps, sizeof(*pThis), RTFILE_O_READ | RTFILE_O_WRITE, NIL_RTVFS, NIL_RTVFSLOCK,
                          &hVfsFile, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        rtVfsMemInitObjInfo(&pThis->Base.ObjInfo, 0);
        rtVfsMemFileInit(pThis, cbEstimate, RTFILE_O_READ | RTFILE_O_WRITE);

        if (hVfsIos != NIL_RTVFSIOSTREAM)
        {
            RTVFSIOSTREAM hVfsIosDst = RTVfsFileToIoStream(hVfsFile);
            rc = RTVfsUtilPumpIoStreams(hVfsIos, hVfsIosDst, pThis->cbExtent);
            RTVfsIoStrmRelease(hVfsIosDst);
        }

        if (RT_SUCCESS(rc))
        {
            *phVfsFile = hVfsFile;
            return VINF_SUCCESS;
        }

        RTVfsFileRelease(hVfsFile);
    }
    return rc;
}


RTDECL(int) RTVfsMemIoStrmCreate(RTVFSIOSTREAM hVfsIos, size_t cbEstimate, PRTVFSIOSTREAM phVfsIos)
{
    RTVFSFILE hVfsFile;
    int rc = RTVfsMemFileCreate(hVfsIos, cbEstimate, &hVfsFile);
    if (RT_SUCCESS(rc))
    {
        *phVfsIos = RTVfsFileToIoStream(hVfsFile);
        AssertStmt(*phVfsIos != NIL_RTVFSIOSTREAM, rc = VERR_INTERNAL_ERROR_2);
        RTVfsFileRelease(hVfsFile);
    }
    return rc;
}


RTDECL(int) RTVfsFileFromBuffer(uint32_t fFlags, void const *pvBuf, size_t cbBuf, PRTVFSFILE phVfsFile)
{
    /*
     * Create a memory file instance and set the extension size according to the
     * buffer size.  Add the WRITE flag so we can use normal write APIs for
     * copying the buffer.
     */
    RTVFSFILE       hVfsFile;
    PRTVFSMEMFILE   pThis;
    int rc = RTVfsNewFile(&g_rtVfsMemFileOps, sizeof(*pThis), fFlags | RTFILE_O_WRITE, NIL_RTVFS, NIL_RTVFSLOCK,
                          &hVfsFile, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        rtVfsMemInitObjInfo(&pThis->Base.ObjInfo, cbBuf);
        rtVfsMemFileInit(pThis, cbBuf, fFlags);

        /*
         * Copy the buffer and reposition the file pointer to the start.
         */
        rc = RTVfsFileWrite(hVfsFile, pvBuf, cbBuf, NULL);
        if (RT_SUCCESS(rc))
        {
            rtVfsMemFileResetAndFixWriteFlag(pThis, fFlags);
            *phVfsFile = hVfsFile;
            return VINF_SUCCESS;
        }
        RTVfsFileRelease(hVfsFile);
    }
    return rc;
}


RTDECL(int) RTVfsIoStrmFromBuffer(uint32_t fFlags, void const *pvBuf, size_t cbBuf, PRTVFSIOSTREAM phVfsIos)
{
    RTVFSFILE hVfsFile;
    int rc = RTVfsFileFromBuffer(fFlags, pvBuf, cbBuf, &hVfsFile);
    if (RT_SUCCESS(rc))
    {
        *phVfsIos = RTVfsFileToIoStream(hVfsFile);
        RTVfsFileRelease(hVfsFile);
    }
    return rc;
}


RTDECL(int) RTVfsMemorizeIoStreamAsFile(RTVFSIOSTREAM hVfsIos, uint32_t fFlags, PRTVFSFILE phVfsFile)
{
    /*
     * Create a memory file instance and try set the extension size to match
     * the length of the I/O stream.
     */
    RTFSOBJINFO ObjInfo;
    int rc = RTVfsIoStrmQueryInfo(hVfsIos, &ObjInfo, RTFSOBJATTRADD_UNIX);
    if (RT_SUCCESS(rc))
    {
        RTVFSFILE       hVfsFile;
        PRTVFSMEMFILE   pThis;
        rc = RTVfsNewFile(&g_rtVfsMemFileOps, sizeof(*pThis), fFlags | RTFILE_O_WRITE, NIL_RTVFS, NIL_RTVFSLOCK,
                          &hVfsFile, (void **)&pThis);
        if (RT_SUCCESS(rc))
        {
            pThis->Base.ObjInfo = ObjInfo;
            rtVfsMemFileInit(pThis, ObjInfo.cbObject, fFlags);

            /*
             * Copy the stream.
             */
            RTVFSIOSTREAM hVfsIosDst = RTVfsFileToIoStream(hVfsFile);
            rc = RTVfsUtilPumpIoStreams(hVfsIos, hVfsIosDst, pThis->cbExtent);
            RTVfsIoStrmRelease(hVfsIosDst);
            if (RT_SUCCESS(rc))
            {
                rtVfsMemFileResetAndFixWriteFlag(pThis, fFlags);
                *phVfsFile = hVfsFile;
                return VINF_SUCCESS;
            }
            RTVfsFileRelease(hVfsFile);
        }
    }
    return rc;
}

