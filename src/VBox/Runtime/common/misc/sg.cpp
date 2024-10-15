/* $Id: sg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Scatter / Gather buffer handling.
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
#include <iprt/sg.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/asm-mem.h>


/**
 * Gets the next continuous block of buffer data (up to @a *pcbData bytes) and
 * advances the buffer position past it.
 */
static void *rtSgBufGet(PRTSGBUF pSgBuf, size_t *pcbData)
{
    /*
     * Check that the S/G buffer has memory left (!RTSgIsEnd(pSgBuf)).
     */
    unsigned const idxSeg = pSgBuf->idxSeg;
    unsigned const cSegs  = pSgBuf->cSegs;
    if (RT_LIKELY(   idxSeg < cSegs
                  && (   pSgBuf->cbSegLeft > 0 /* paranoia, this condition shouldn't occur. */
                      || idxSeg + 1 < cSegs)))
    {
        /*
         * Grab the requested segment chunk.
         */
        void * const pvBuf     = pSgBuf->pvSegCur;
        size_t const cbSegLeft = pSgBuf->cbSegLeft;
        size_t const cbData    = RT_MIN(*pcbData, cbSegLeft);

#ifndef RDESKTOP
        AssertMsg(   pSgBuf->cbSegLeft <= 128 * _1M
                  && (uintptr_t)pSgBuf->pvSegCur             >= (uintptr_t)pSgBuf->paSegs[pSgBuf->idxSeg].pvSeg
                  && (uintptr_t)pSgBuf->pvSegCur + cbSegLeft <= (uintptr_t)pSgBuf->paSegs[pSgBuf->idxSeg].pvSeg + pSgBuf->paSegs[pSgBuf->idxSeg].cbSeg,
                  ("pSgBuf->idxSeg=%d pSgBuf->cSegs=%d pSgBuf->pvSegCur=%p cbSegLeft=%zd pSgBuf->paSegs[%d].pvSeg=%p pSgBuf->paSegs[%d].cbSeg=%zd\n",
                   pSgBuf->idxSeg, pSgBuf->cSegs, pSgBuf->pvSegCur, cbSegLeft, pSgBuf->idxSeg, pSgBuf->paSegs[pSgBuf->idxSeg].pvSeg, pSgBuf->idxSeg,
                   pSgBuf->paSegs[pSgBuf->idxSeg].cbSeg));
#endif

        /*
         * Advance ...
         */
        if (cbSegLeft == cbData)
        {
            *pcbData = cbData;

            /* ... to the next segment. */
            unsigned idxSegNew = idxSeg + 1;
            if (idxSegNew < pSgBuf->cSegs)
            {
                do
                {
                    PCRTSGSEG const pNewSeg = &pSgBuf->paSegs[idxSegNew];
                    if (pNewSeg->cbSeg > 0)
                    {
                        pSgBuf->idxSeg    = idxSegNew;
                        pSgBuf->cbSegLeft = pNewSeg->cbSeg;
                        pSgBuf->pvSegCur  = pNewSeg->pvSeg;
                        return pvBuf;
                    }
                    /* Empty segment, skip it. */
                    idxSegNew++;
                } while (idxSegNew < pSgBuf->cSegs);
            }
            pSgBuf->idxSeg    = idxSegNew;
            pSgBuf->cbSegLeft = 0;
            pSgBuf->pvSegCur  = NULL;
        }
        else
        {
            /* ... within the current segment. */
            Assert(*pcbData == cbData);
            pSgBuf->cbSegLeft = cbSegLeft - cbData;
            pSgBuf->pvSegCur  = (uint8_t *)pvBuf + cbData;
        }

        return pvBuf;
    }

    Assert(pSgBuf->cbSegLeft == 0);
    *pcbData = 0;
    return NULL;
}


RTDECL(void) RTSgBufInit(PRTSGBUF pSgBuf, PCRTSGSEG paSegs, size_t cSegs)
{
    AssertPtr(pSgBuf);
    Assert(   (cSegs > 0 && RT_VALID_PTR(paSegs))
           || (!cSegs && !paSegs));
    Assert(cSegs < (~(unsigned)0 >> 1));

    pSgBuf->paSegs    = paSegs;
    pSgBuf->cSegs     = (unsigned)cSegs;
    pSgBuf->idxSeg    = 0;
    if (cSegs && paSegs)
    {
        pSgBuf->pvSegCur  = paSegs[0].pvSeg;
        pSgBuf->cbSegLeft = paSegs[0].cbSeg;
    }
    else
    {
        pSgBuf->pvSegCur  = NULL;
        pSgBuf->cbSegLeft = 0;
    }
}


RTDECL(void) RTSgBufReset(PRTSGBUF pSgBuf)
{
    AssertPtrReturnVoid(pSgBuf);

    pSgBuf->idxSeg = 0;
    if (pSgBuf->cSegs)
    {
        pSgBuf->pvSegCur  = pSgBuf->paSegs[0].pvSeg;
        pSgBuf->cbSegLeft = pSgBuf->paSegs[0].cbSeg;
    }
    else
    {
        pSgBuf->pvSegCur  = NULL;
        pSgBuf->cbSegLeft = 0;
    }
}


RTDECL(void) RTSgBufClone(PRTSGBUF pSgBufTo, PCRTSGBUF pSgBufFrom)
{
    AssertPtr(pSgBufTo);
    AssertPtr(pSgBufFrom);

    pSgBufTo->paSegs    = pSgBufFrom->paSegs;
    pSgBufTo->cSegs     = pSgBufFrom->cSegs;
    pSgBufTo->idxSeg    = pSgBufFrom->idxSeg;
    pSgBufTo->pvSegCur  = pSgBufFrom->pvSegCur;
    pSgBufTo->cbSegLeft = pSgBufFrom->cbSegLeft;
}


RTDECL(void *) RTSgBufGetNextSegment(PRTSGBUF pSgBuf, size_t *pcbSeg)
{
    AssertPtrReturn(pSgBuf, NULL);
    AssertPtrReturn(pcbSeg, NULL);

    if (!*pcbSeg)
        *pcbSeg = pSgBuf->cbSegLeft;

    return rtSgBufGet(pSgBuf, pcbSeg);
}


RTDECL(size_t) RTSgBufCopy(PRTSGBUF pSgBufDst, PRTSGBUF pSgBufSrc, size_t cbCopy)
{
    AssertPtrReturn(pSgBufDst, 0);
    AssertPtrReturn(pSgBufSrc, 0);

    size_t cbLeft = cbCopy;
    while (cbLeft)
    {
        size_t cbThisCopy = RT_MIN(RT_MIN(pSgBufDst->cbSegLeft, cbLeft), pSgBufSrc->cbSegLeft);
        if (!cbThisCopy)
            break;

        size_t cbTmp = cbThisCopy;
        void *pvBufDst = rtSgBufGet(pSgBufDst, &cbTmp);
        Assert(cbTmp == cbThisCopy);
        void *pvBufSrc = rtSgBufGet(pSgBufSrc, &cbTmp);
        Assert(cbTmp == cbThisCopy);

        memcpy(pvBufDst, pvBufSrc, cbThisCopy);

        cbLeft -= cbThisCopy;
    }

    return cbCopy - cbLeft;
}


RTDECL(int) RTSgBufCmp(PCRTSGBUF pSgBuf1, PCRTSGBUF pSgBuf2, size_t cbCmp)
{
    AssertPtrReturn(pSgBuf1, 0);
    AssertPtrReturn(pSgBuf2, 0);

    /* Set up the temporary buffers */
    RTSGBUF SgBuf1;
    RTSgBufClone(&SgBuf1, pSgBuf1);
    RTSGBUF SgBuf2;
    RTSgBufClone(&SgBuf2, pSgBuf2);

    size_t cbLeft = cbCmp;
    while (cbLeft)
    {
        size_t cbThisCmp = RT_MIN(RT_MIN(SgBuf1.cbSegLeft, cbLeft), SgBuf2.cbSegLeft);
        if (!cbThisCmp)
            break;

        size_t cbTmp = cbThisCmp;
        void *pvBuf1 = rtSgBufGet(&SgBuf1, &cbTmp);
        Assert(cbTmp == cbThisCmp);
        void *pvBuf2 = rtSgBufGet(&SgBuf2, &cbTmp);
        Assert(cbTmp == cbThisCmp);

        int iDiff = memcmp(pvBuf1, pvBuf2, cbThisCmp);
        if (iDiff)
            return iDiff;

        cbLeft -= cbThisCmp;
    }

    return 0;
}


RTDECL(int) RTSgBufCmpEx(PRTSGBUF pSgBuf1, PRTSGBUF pSgBuf2, size_t cbCmp, size_t *poffDiff, bool fAdvance)
{
    AssertPtrReturn(pSgBuf1, 0);
    AssertPtrReturn(pSgBuf2, 0);

    RTSGBUF SgBuf1Tmp;
    RTSGBUF SgBuf2Tmp;
    PRTSGBUF pSgBuf1Tmp;
    PRTSGBUF pSgBuf2Tmp;

    if (!fAdvance)
    {
        /* Set up the temporary buffers */
        RTSgBufClone(&SgBuf1Tmp, pSgBuf1);
        RTSgBufClone(&SgBuf2Tmp, pSgBuf2);
        pSgBuf1Tmp = &SgBuf1Tmp;
        pSgBuf2Tmp = &SgBuf2Tmp;
    }
    else
    {
        pSgBuf1Tmp = pSgBuf1;
        pSgBuf2Tmp = pSgBuf2;
    }

    size_t cbLeft = cbCmp;
    size_t off    = 0;
    while (cbLeft)
    {
        size_t cbThisCmp = RT_MIN(RT_MIN(pSgBuf1Tmp->cbSegLeft, cbLeft), pSgBuf2Tmp->cbSegLeft);
        if (!cbThisCmp)
            break;

        size_t cbTmp = cbThisCmp;
        uint8_t *pbBuf1 = (uint8_t *)rtSgBufGet(pSgBuf1Tmp, &cbTmp);
        Assert(cbTmp == cbThisCmp);
        uint8_t *pbBuf2 = (uint8_t *)rtSgBufGet(pSgBuf2Tmp, &cbTmp);
        Assert(cbTmp == cbThisCmp);

        int iDiff = memcmp(pbBuf1, pbBuf2, cbThisCmp);
        if (iDiff)
        {
            /* Locate the first byte that differs if the caller requested this. */
            if (poffDiff)
            {
                while (   cbThisCmp-- > 0
                       && *pbBuf1 == *pbBuf2)
                {
                    pbBuf1++;
                    pbBuf2++;
                    off++;
                }

                *poffDiff = off;
            }
            return iDiff;
        }

        cbLeft -= cbThisCmp;
        off    += cbThisCmp;
    }

    return 0;
}


RTDECL(size_t) RTSgBufSet(PRTSGBUF pSgBuf, uint8_t bFill, size_t cbToSet)
{
    AssertPtrReturn(pSgBuf, 0);

    size_t cbLeft = cbToSet;

    while (cbLeft)
    {
        size_t cbThisSet = cbLeft;
        void *pvBuf = rtSgBufGet(pSgBuf, &cbThisSet);
        if (!pvBuf)
            break;

        memset(pvBuf, bFill, cbThisSet);

        cbLeft -= cbThisSet;
    }

    return cbToSet - cbLeft;
}


RTDECL(size_t) RTSgBufCopyToBuf(PRTSGBUF pSgBuf, void *pvBuf, size_t cbCopy)
{
    AssertPtrReturn(pSgBuf, 0);
    AssertPtrReturn(pvBuf, 0);

    size_t cbLeft = cbCopy;

    while (cbLeft)
    {
        size_t cbThisCopy = cbLeft;
        void * const pvSrc = rtSgBufGet(pSgBuf, &cbThisCopy);
        if (!pvSrc)
            break;

        memcpy(pvBuf, pvSrc, cbThisCopy);

        pvBuf = (void *)((uintptr_t)pvBuf + cbThisCopy);
        cbLeft -= cbThisCopy;
    }

    return cbCopy - cbLeft;
}


RTDECL(size_t) RTSgBufCopyFromBuf(PRTSGBUF pSgBuf, const void *pvBuf, size_t cbCopy)
{
    AssertPtrReturn(pSgBuf, 0);
    AssertPtrReturn(pvBuf, 0);

    size_t cbLeft = cbCopy;

    while (cbLeft)
    {
        size_t cbThisCopy = cbLeft;
        void * const pvDst = rtSgBufGet(pSgBuf, &cbThisCopy);
        if (!pvDst)
            break;

        memcpy(pvDst, pvBuf, cbThisCopy);

        pvBuf = (const void *)((uintptr_t)pvBuf + cbThisCopy);
        cbLeft -= cbThisCopy;
    }

    return cbCopy - cbLeft;
}


RTDECL(size_t) RTSgBufCopyToFn(PRTSGBUF pSgBuf, size_t cbCopy, PFNRTSGBUFCOPYTO pfnCopyTo, void *pvUser)
{
    AssertPtrReturn(pSgBuf, 0);
    AssertPtrReturn(pfnCopyTo, 0);

    size_t cbLeft = cbCopy;

    while (cbLeft)
    {
        size_t cbThisCopy = cbLeft;
        void * const pvSrc = rtSgBufGet(pSgBuf, &cbThisCopy);
        if (!pvSrc)
            break;

        size_t cbThisCopied = pfnCopyTo(pSgBuf, pvSrc, cbThisCopy, pvUser);
        cbLeft -= cbThisCopied;
        if (cbThisCopied < cbThisCopy)
            break;
    }

    return cbCopy - cbLeft;
}


RTDECL(size_t) RTSgBufCopyFromFn(PRTSGBUF pSgBuf, size_t cbCopy, PFNRTSGBUFCOPYFROM pfnCopyFrom, void *pvUser)
{
    AssertPtrReturn(pSgBuf, 0);
    AssertPtrReturn(pfnCopyFrom, 0);

    size_t cbLeft = cbCopy;

    while (cbLeft)
    {
        size_t cbThisCopy = cbLeft;
        void * const pvDst = rtSgBufGet(pSgBuf, &cbThisCopy);
        if (!pvDst)
            break;

        size_t cbThisCopied = pfnCopyFrom(pSgBuf, pvDst, cbThisCopy, pvUser);
        cbLeft -= cbThisCopied;
        if (cbThisCopied < cbThisCopy)
            break;
    }

    return cbCopy - cbLeft;
}


RTDECL(size_t) RTSgBufAdvance(PRTSGBUF pSgBuf, size_t cbAdvance)
{
    AssertPtrReturn(pSgBuf, 0);

    size_t cbLeft = cbAdvance;

    while (    cbLeft
           || (   pSgBuf->cbSegLeft == 0
               && pSgBuf->idxSeg > pSgBuf->cSegs))
    {
        size_t cbThisAdvance = cbLeft;
        if (!rtSgBufGet(pSgBuf, &cbThisAdvance))
            break;

        cbLeft -= cbThisAdvance;
    }

    return cbAdvance - cbLeft;
}


RTDECL(size_t) RTSgBufSegArrayCreate(PRTSGBUF pSgBuf, PRTSGSEG paSeg, unsigned *pcSegs, size_t cbData)
{
    AssertPtrReturn(pSgBuf, 0);
    AssertPtrReturn(pcSegs, 0);

    unsigned cSegs = 0;
    size_t   cbRet = 0;

    if (!paSeg)
    {
        if (pSgBuf->cbSegLeft > 0)
        {
            size_t idx = pSgBuf->idxSeg;

            cbRet   = RT_MIN(pSgBuf->cbSegLeft, cbData);
            cSegs   = cbRet != 0;
            cbData -= cbRet;

            while (   cbData
                   && idx < pSgBuf->cSegs - 1)
            {
                idx++;
                size_t cbThisSeg = RT_MIN(pSgBuf->paSegs[idx].cbSeg, cbData);
                if (cbThisSeg)
                {
                    cbRet  += cbThisSeg;
                    cbData -= cbThisSeg;
                    cSegs++;
                }
            }
        }
    }
    else
    {
        while (   cbData
               && cSegs < *pcSegs)
        {
            size_t cbThisSeg = cbData;
            void * const pvSeg = rtSgBufGet(pSgBuf, &cbThisSeg);
            if (!pvSeg)
                break;

            AssertMsg(cbThisSeg <= cbData, ("Impossible!\n"));

            paSeg[cSegs].cbSeg = cbThisSeg;
            paSeg[cSegs].pvSeg = pvSeg;
            cSegs++;
            cbData -= cbThisSeg;
            cbRet  += cbThisSeg;
        }
    }

    *pcSegs = cSegs;

    return cbRet;
}


RTDECL(bool) RTSgBufIsZero(PRTSGBUF pSgBuf, size_t cbCheck)
{
    RTSGBUF SgBufTmp;
    RTSgBufClone(&SgBufTmp, pSgBuf);

    bool   fIsZero = true;
    size_t cbLeft  = cbCheck;
    while (cbLeft)
    {
        size_t cbThisCheck = cbLeft;
        void * const pvBuf = rtSgBufGet(&SgBufTmp, &cbThisCheck);
        if (!pvBuf)
            break;
        if (cbThisCheck > 0)
        {
            fIsZero = ASMMemIsZero(pvBuf, cbThisCheck);
            if (!fIsZero)
                break;
            cbLeft -= cbThisCheck;
        }
    }

    return fIsZero;
}

