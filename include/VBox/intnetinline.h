/* $Id: intnetinline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * INTNET - Internal Networking, Inlined Code. (DEV,++)
 *
 * This is all inlined because it's too tedious to create 2-3 libraries to
 * contain it all.  Large parts of this header is only accessible from C++
 * sources because of mixed code and variables.
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

#ifndef VBOX_INCLUDED_intnetinline_h
#define VBOX_INCLUDED_intnetinline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/intnet.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/asm-mem.h>
#include <iprt/asm.h>
#include <VBox/log.h>



/**
 * Valid internal networking frame type.
 *
 * @returns  true / false.
 * @param    u8Type             The frame type to check.
 */
DECLINLINE(bool) IntNetIsValidFrameType(uint8_t u8Type)
{
    if (RT_LIKELY(   u8Type == INTNETHDR_TYPE_FRAME
                  || u8Type == INTNETHDR_TYPE_GSO
                  || u8Type == INTNETHDR_TYPE_PADDING))
        return true;
    return false;
}


/**
 * Partly initializes a scatter / gather buffer, leaving the segments to the
 * caller.
 *
 * @param   pSG         Pointer to the scatter / gather structure.
 * @param   cbTotal     The total size.
 * @param   cSegs       The number of segments.
 * @param   cSegsUsed   The number of used segments.
 */
DECLINLINE(void) IntNetSgInitTempSegs(PINTNETSG pSG, uint32_t cbTotal, unsigned cSegs, unsigned cSegsUsed)
{
    pSG->pvOwnerData    = NULL;
    pSG->pvUserData     = NULL;
    pSG->pvUserData2    = NULL;
    pSG->cbTotal        = cbTotal;
    pSG->cUsers         = 1;
    pSG->fFlags         = INTNETSG_FLAGS_TEMP;
    pSG->GsoCtx.u8Type  = (uint8_t)PDMNETWORKGSOTYPE_INVALID;
    pSG->GsoCtx.cbHdrsTotal = 0;
    pSG->GsoCtx.cbHdrsSeg   = 0;
    pSG->GsoCtx.cbMaxSeg= 0;
    pSG->GsoCtx.offHdr1 = 0;
    pSG->GsoCtx.offHdr2 = 0;
    pSG->GsoCtx.u8Unused= 0;
#if ARCH_BITS == 64
    pSG->uPadding       = 0;
#endif
    pSG->cSegsAlloc     = (uint16_t)cSegs;
    Assert(pSG->cSegsAlloc == cSegs);
    pSG->cSegsUsed      = (uint16_t)cSegsUsed;
    Assert(pSG->cSegsUsed == cSegsUsed);
    Assert(cSegs >= cSegsUsed);
}


/**
 * Partly initializes a scatter / gather buffer w/ GSO, leaving the segments to
 * the caller.
 *
 * @param   pSG         Pointer to the scatter / gather structure.
 * @param   cbTotal     The total size.
 * @param   cSegs       The number of segments.
 * @param   cSegsUsed   The number of used segments.
 * @param   pGso        The GSO context.
 */
DECLINLINE(void) IntNetSgInitTempSegsGso(PINTNETSG pSG, uint32_t cbTotal, unsigned cSegs,
                                         unsigned cSegsUsed, PCPDMNETWORKGSO pGso)
{
    pSG->pvOwnerData    = NULL;
    pSG->pvUserData     = NULL;
    pSG->pvUserData2    = NULL;
    pSG->cbTotal        = cbTotal;
    pSG->cUsers         = 1;
    pSG->fFlags         = INTNETSG_FLAGS_TEMP;
    pSG->GsoCtx.u8Type  = pGso->u8Type;
    pSG->GsoCtx.cbHdrsTotal = pGso->cbHdrsTotal;
    pSG->GsoCtx.cbHdrsSeg   = pGso->cbHdrsSeg;
    pSG->GsoCtx.cbMaxSeg= pGso->cbMaxSeg;
    pSG->GsoCtx.offHdr1 = pGso->offHdr1;
    pSG->GsoCtx.offHdr2 = pGso->offHdr2;
    pSG->GsoCtx.u8Unused= 0;
#if ARCH_BITS == 64
    pSG->uPadding       = 0;
#endif
    pSG->cSegsAlloc     = (uint16_t)cSegs;
    Assert(pSG->cSegsAlloc == cSegs);
    pSG->cSegsUsed      = (uint16_t)cSegsUsed;
    Assert(pSG->cSegsUsed == cSegsUsed);
    Assert(cSegs >= cSegsUsed);
}



/**
 * Initializes a scatter / gather buffer describing a simple linear buffer.
 *
 * @param   pSG         Pointer to the scatter / gather structure.
 * @param   pvFrame     Pointer to the frame
 * @param   cbFrame     The size of the frame.
 */
DECLINLINE(void) IntNetSgInitTemp(PINTNETSG pSG, void *pvFrame, uint32_t cbFrame)
{
    IntNetSgInitTempSegs(pSG, cbFrame, 1, 1);
    pSG->aSegs[0].Phys  = NIL_RTHCPHYS;
    pSG->aSegs[0].pv    = pvFrame;
    pSG->aSegs[0].cb    = cbFrame;
}

/**
 * Initializes a scatter / gather buffer describing a simple linear buffer.
 *
 * @param   pSG         Pointer to the scatter / gather structure.
 * @param   pvFrame     Pointer to the frame
 * @param   cbFrame     The size of the frame.
 * @param   pGso        The GSO context.
 */
DECLINLINE(void) IntNetSgInitTempGso(PINTNETSG pSG, void *pvFrame, uint32_t cbFrame, PCPDMNETWORKGSO pGso)
{
    IntNetSgInitTempSegsGso(pSG, cbFrame, 1, 1, pGso);
    pSG->aSegs[0].Phys  = NIL_RTHCPHYS;
    pSG->aSegs[0].pv    = pvFrame;
    pSG->aSegs[0].cb    = cbFrame;
}


/**
 * Reads an entire SG into a fittingly size buffer.
 *
 * @param   pSG         The SG list to read.
 * @param   pvBuf       The buffer to read into (at least pSG->cbTotal in size).
 */
DECLINLINE(void) IntNetSgRead(PCINTNETSG pSG, void *pvBuf)
{
    memcpy(pvBuf, pSG->aSegs[0].pv, pSG->aSegs[0].cb);
    if (pSG->cSegsUsed == 1)
        Assert(pSG->cbTotal == pSG->aSegs[0].cb);
    else
    {
        uint8_t        *pbDst = (uint8_t *)pvBuf + pSG->aSegs[0].cb;
        unsigned        iSeg  = 0;
        unsigned const  cSegs = pSG->cSegsUsed;
        while (++iSeg < cSegs)
        {
            uint32_t    cbSeg = pSG->aSegs[iSeg].cb;
            Assert((uintptr_t)pbDst - (uintptr_t)pvBuf + cbSeg <= pSG->cbTotal);
            memcpy(pbDst, pSG->aSegs[iSeg].pv, cbSeg);
            pbDst += cbSeg;
        }
    }
}


/**
 * Reads a portion of an SG into a buffer.
 *
 * @param   pSG         The SG list to read.
 * @param   offSrc      The offset to start start copying from.
 * @param   cbToRead    The number of bytes to copy.
 * @param   pvBuf       The buffer to read into, cb or more in size.
 */
DECLINLINE(void) IntNetSgReadEx(PCINTNETSG pSG, uint32_t offSrc, uint32_t cbToRead, void *pvBuf)
{
    uint8_t    *pbDst = (uint8_t *)pvBuf;
    uint32_t    iSeg  = 0;

    /* validate assumptions */
    Assert(cbToRead          <= pSG->cbTotal);
    Assert(offSrc            <= pSG->cbTotal);
    Assert(offSrc + cbToRead <= pSG->cbTotal);

    /* Find the right segment and copy any bits from within the segment. */
    while (offSrc)
    {
        uint32_t cbSeg = pSG->aSegs[iSeg].cb;
        if (offSrc < cbSeg)
        {
            uint32_t cbChunk = cbSeg - offSrc;
            if (cbChunk >= cbToRead)
            {
                memcpy(pbDst, (uint8_t const *)pSG->aSegs[iSeg].pv + offSrc, cbToRead);
                return;
            }

            memcpy(pbDst, (uint8_t const *)pSG->aSegs[iSeg].pv + offSrc, cbChunk);
            pbDst    += cbChunk;
            cbToRead -= cbChunk;
            break;
        }

        /* advance */
        offSrc -= cbSeg;
        iSeg++;
    }

    /* We're not at the start of a segment, copy until we're done. */
    for (;;)
    {
        uint32_t cbSeg = pSG->aSegs[iSeg].cb;
        if (cbSeg >= cbToRead)
        {
            memcpy(pbDst, pSG->aSegs[iSeg].pv, cbToRead);
            return;
        }

        memcpy(pbDst, pSG->aSegs[iSeg].pv, cbSeg);
        pbDst    += cbSeg;
        cbToRead -= cbSeg;
        iSeg++;
        Assert(iSeg < pSG->cSegsUsed);
    }
}

#ifdef __cplusplus

/**
 * Get the amount of space available for writing.
 *
 * @returns Number of available bytes.
 * @param   pRingBuf        The ring buffer.
 */
DECLINLINE(uint32_t) IntNetRingGetWritable(PINTNETRINGBUF pRingBuf)
{
    uint32_t const offRead     = ASMAtomicUoReadU32(&pRingBuf->offReadX);
    uint32_t const offWriteInt = ASMAtomicUoReadU32(&pRingBuf->offWriteInt);
    return offRead <= offWriteInt
        ?  pRingBuf->offEnd  - offWriteInt + offRead - pRingBuf->offStart - 1
        :  offRead - offWriteInt - 1;
}


/**
 * Checks if the ring has more for us to read.
 *
 * @returns Number of ready bytes.
 * @param   pRingBuf        The ring buffer.
 */
DECLINLINE(bool) IntNetRingHasMoreToRead(PINTNETRINGBUF pRingBuf)
{
    uint32_t const offRead     = ASMAtomicUoReadU32(&pRingBuf->offReadX);
    uint32_t const offWriteCom = ASMAtomicUoReadU32(&pRingBuf->offWriteCom);
    return offRead != offWriteCom;
}


/**
 * Gets the next frame to read.
 *
 * @returns Pointer to the next frame.  NULL if done.
 * @param   pRingBuf        The ring buffer.
 */
DECLINLINE(PINTNETHDR) IntNetRingGetNextFrameToRead(PINTNETRINGBUF pRingBuf)
{
    uint32_t const offRead     = ASMAtomicUoReadU32(&pRingBuf->offReadX);
    uint32_t const offWriteCom = ASMAtomicUoReadU32(&pRingBuf->offWriteCom);
    if (offRead == offWriteCom)
        return NULL;
    return (PINTNETHDR)((uint8_t *)pRingBuf + offRead);
}


/**
 * Get the amount of data ready for reading.
 *
 * @returns Number of ready bytes.
 * @param   pRingBuf        The ring buffer.
 */
DECLINLINE(uint32_t) IntNetRingGetReadable(PINTNETRINGBUF pRingBuf)
{
    uint32_t const offRead     = ASMAtomicUoReadU32(&pRingBuf->offReadX);
    uint32_t const offWriteCom = ASMAtomicUoReadU32(&pRingBuf->offWriteCom);
    return offRead <= offWriteCom
        ?  offWriteCom - offRead
        :  pRingBuf->offEnd - offRead + offWriteCom - pRingBuf->offStart;
}


/**
 * Calculates the pointer to the frame.
 *
 * @returns Pointer to the start of the frame.
 * @param   pHdr        Pointer to the packet header
 * @param   pBuf        The buffer the header is within. Only used in strict builds.
 */
DECLINLINE(void *) IntNetHdrGetFramePtr(PCINTNETHDR pHdr, PCINTNETBUF pBuf)
{
    uint8_t *pu8 = (uint8_t *)pHdr + pHdr->offFrame;
#ifdef VBOX_STRICT
    const uintptr_t off = (uintptr_t)pu8 - (uintptr_t)pBuf;
    Assert(IntNetIsValidFrameType(pHdr->u8Type));
    Assert(off < pBuf->cbBuf);
    Assert(off + pHdr->cbFrame <= pBuf->cbBuf);
#endif
    NOREF(pBuf);
    return pu8;
}


/**
 * Calculates the pointer to the GSO context.
 *
 * ASSUMES the frame is a GSO frame.
 *
 * The GSO context is immediately followed by the headers and payload.  The size
 * is INTNETBUF::cbFrame - sizeof(PDMNETWORKGSO).
 *
 * @returns Pointer to the GSO context.
 * @param   pHdr        Pointer to the packet header
 * @param   pBuf        The buffer the header is within. Only used in strict builds.
 */
DECLINLINE(PPDMNETWORKGSO) IntNetHdrGetGsoContext(PCINTNETHDR pHdr, PCINTNETBUF pBuf)
{
    PPDMNETWORKGSO pGso = (PPDMNETWORKGSO)((uint8_t *)pHdr + pHdr->offFrame);
#ifdef VBOX_STRICT
    const uintptr_t off = (uintptr_t)pGso - (uintptr_t)pBuf;
    Assert(pHdr->u8Type == INTNETHDR_TYPE_GSO);
    Assert(off < pBuf->cbBuf);
    Assert(off + pHdr->cbFrame <= pBuf->cbBuf);
#endif
    NOREF(pBuf);
    return pGso;
}


/**
 * Skips to the next (read) frame in the buffer.
 *
 * @param   pRingBuf    The ring buffer in question.
 */
DECLINLINE(void) IntNetRingSkipFrame(PINTNETRINGBUF pRingBuf)
{
    uint32_t const  offReadOld  = ASMAtomicUoReadU32(&pRingBuf->offReadX);
    PINTNETHDR      pHdr        = (PINTNETHDR)((uint8_t *)pRingBuf + offReadOld);
    Assert(offReadOld >= pRingBuf->offStart);
    Assert(offReadOld <  pRingBuf->offEnd);
    Assert(RT_ALIGN_PT(pHdr, INTNETHDR_ALIGNMENT, INTNETHDR *) == pHdr);
    Assert(IntNetIsValidFrameType(pHdr->u8Type));

    /* skip the frame */
    uint32_t        offReadNew  = offReadOld + pHdr->offFrame + pHdr->cbFrame;
    offReadNew = RT_ALIGN_32(offReadNew, INTNETHDR_ALIGNMENT);
    Assert(offReadNew <= pRingBuf->offEnd && offReadNew >= pRingBuf->offStart);
    if (offReadNew >= pRingBuf->offEnd)
        offReadNew = pRingBuf->offStart;
    Log2(("IntNetRingSkipFrame: offReadX: %#x -> %#x (1)\n", offReadOld, offReadNew));
#ifdef INTNET_POISON_READ_FRAMES
    memset((uint8_t *)pHdr + pHdr->offFrame, 0xfe, RT_ALIGN_32(pHdr->cbFrame, INTNETHDR_ALIGNMENT));
    memset(pHdr, 0xef, sizeof(*pHdr));
#endif
    ASMAtomicWriteU32(&pRingBuf->offReadX, offReadNew);
}


/**
 * Allocates a frame in the specified ring.
 *
 * @returns VINF_SUCCESS or VERR_BUFFER_OVERFLOW.
 * @param   pRingBuf            The ring buffer.
 * @param   cbFrame             The frame size.
 * @param   u8Type              The header type.
 * @param   ppHdr               Where to return the frame header.
 *                              Don't touch this!
 * @param   ppvFrame            Where to return the frame pointer.
 */
DECLINLINE(int) intnetRingAllocateFrameInternal(PINTNETRINGBUF pRingBuf, uint32_t cbFrame, uint8_t u8Type,
                                                PINTNETHDR *ppHdr, void **ppvFrame)
{
    /*
     * Validate input and adjust the input.
     */
    INTNETRINGBUF_ASSERT_SANITY(pRingBuf);
    Assert(cbFrame >= sizeof(RTMAC) * 2);

    const uint32_t  cb          = RT_ALIGN_32(cbFrame, INTNETHDR_ALIGNMENT);
    uint32_t        offWriteInt = ASMAtomicUoReadU32(&pRingBuf->offWriteInt);
    uint32_t        offRead     = ASMAtomicUoReadU32(&pRingBuf->offReadX);
    if (offRead <= offWriteInt)
    {
        /*
         * Try fit it all before the end of the buffer.
         */
        if (pRingBuf->offEnd - offWriteInt >= cb + sizeof(INTNETHDR))
        {
            uint32_t offNew = offWriteInt + cb + sizeof(INTNETHDR);
            if (offNew >= pRingBuf->offEnd)
                offNew = pRingBuf->offStart;
            if (RT_LIKELY(ASMAtomicCmpXchgU32(&pRingBuf->offWriteInt, offNew, offWriteInt)))
            { /* likely */ } else return VERR_WRONG_ORDER; /* race */
            Log2(("intnetRingAllocateFrameInternal: offWriteInt: %#x -> %#x (1) (R=%#x T=%#x S=%#x)\n", offWriteInt, offNew, offRead, u8Type, cbFrame));

            PINTNETHDR pHdr = (PINTNETHDR)((uint8_t *)pRingBuf + offWriteInt);
            pHdr->u8Type   = u8Type;
            pHdr->cbFrame  = cbFrame; Assert(pHdr->cbFrame == cbFrame);
            pHdr->offFrame = sizeof(INTNETHDR);

            *ppHdr = pHdr;
            *ppvFrame = pHdr + 1;
            return VINF_SUCCESS;
        }
        /*
         * Try fit the frame at the start of the buffer.
         * (The header fits before the end of the buffer because of alignment.)
         */
        AssertMsg(pRingBuf->offEnd - offWriteInt >= sizeof(INTNETHDR), ("offEnd=%x offWriteInt=%x\n", pRingBuf->offEnd, offWriteInt));
        if (offRead - pRingBuf->offStart > cb) /* not >= ! */
        {
            uint32_t offNew = pRingBuf->offStart + cb;
            if (RT_LIKELY(ASMAtomicCmpXchgU32(&pRingBuf->offWriteInt, offNew, offWriteInt)))
            { /* likely */ } else return VERR_WRONG_ORDER; /* race */
            Log2(("intnetRingAllocateFrameInternal: offWriteInt: %#x -> %#x (2) (R=%#x T=%#x S=%#x)\n", offWriteInt, offNew, offRead, u8Type, cbFrame));

            PINTNETHDR pHdr = (PINTNETHDR)((uint8_t *)pRingBuf + offWriteInt);
            pHdr->u8Type   = u8Type;
            pHdr->cbFrame  = cbFrame; Assert(pHdr->cbFrame == cbFrame);
            pHdr->offFrame = pRingBuf->offStart - offWriteInt;

            *ppHdr = pHdr;
            *ppvFrame = (uint8_t *)pRingBuf + pRingBuf->offStart;
            return VINF_SUCCESS;
        }
    }
    /*
     * The reader is ahead of the writer, try fit it into that space.
     */
    else if (offRead - offWriteInt > cb + sizeof(INTNETHDR)) /* not >= ! */
    {
        uint32_t offNew = offWriteInt + cb + sizeof(INTNETHDR);
        if (RT_LIKELY(ASMAtomicCmpXchgU32(&pRingBuf->offWriteInt, offNew, offWriteInt)))
        { /* likely */ } else return VERR_WRONG_ORDER; /* race */
        Log2(("intnetRingAllocateFrameInternal: offWriteInt: %#x -> %#x (3) (R=%#x T=%#x S=%#x)\n", offWriteInt, offNew, offRead, u8Type, cbFrame));

        PINTNETHDR pHdr = (PINTNETHDR)((uint8_t *)pRingBuf + offWriteInt);
        pHdr->u8Type   = u8Type;
        pHdr->cbFrame  = cbFrame; Assert(pHdr->cbFrame == cbFrame);
        pHdr->offFrame = sizeof(INTNETHDR);

        *ppHdr = pHdr;
        *ppvFrame = pHdr + 1;
        return VINF_SUCCESS;
    }

    /* (it didn't fit) */
    *ppHdr    = NULL;                   /* shut up gcc, */
    *ppvFrame = NULL;                   /* ditto. */
    STAM_REL_COUNTER_INC(&pRingBuf->cOverflows);
    return VERR_BUFFER_OVERFLOW;
}


/**
 * Allocates a normal frame in the specified ring.
 *
 * @returns VINF_SUCCESS or VERR_BUFFER_OVERFLOW.
 * @param   pRingBuf            The ring buffer.
 * @param   cbFrame             The frame size.
 * @param   ppHdr               Where to return the frame header.
 *                              Don't touch this!
 * @param   ppvFrame            Where to return the frame pointer.
 */
DECLINLINE(int) IntNetRingAllocateFrame(PINTNETRINGBUF pRingBuf, uint32_t cbFrame, PINTNETHDR *ppHdr, void **ppvFrame)
{
    return intnetRingAllocateFrameInternal(pRingBuf, cbFrame, INTNETHDR_TYPE_FRAME, ppHdr, ppvFrame);
}


/**
 * Allocates a GSO frame in the specified ring.
 *
 * @returns VINF_SUCCESS or VERR_BUFFER_OVERFLOW.
 * @param   pRingBuf            The ring buffer.
 * @param   cbFrame             The frame size.
 * @param   pGso                Pointer to the GSO context.
 * @param   ppHdr               Where to return the frame header.
 *                              Don't touch this!
 * @param   ppvFrame            Where to return the frame pointer.
 */
DECLINLINE(int) IntNetRingAllocateGsoFrame(PINTNETRINGBUF pRingBuf, uint32_t cbFrame, PCPDMNETWORKGSO pGso,
                                           PINTNETHDR *ppHdr, void **ppvFrame)
{
    void *pvFrame = NULL; /* gcc maybe used uninitialized */
    int rc = intnetRingAllocateFrameInternal(pRingBuf, cbFrame + sizeof(*pGso), INTNETHDR_TYPE_GSO, ppHdr, &pvFrame);
    if (RT_SUCCESS(rc))
    {
        PPDMNETWORKGSO pGsoCopy = (PPDMNETWORKGSO)pvFrame;
        *pGsoCopy = *pGso;
        *ppvFrame = pGsoCopy + 1;
    }
    return rc;
}


/**
 * Commits a frame.
 *
 * Make sure to commit the frames in the order they've been allocated!
 *
 * @param   pRingBuf            The ring buffer.
 * @param   pHdr                The frame header returned by
 *                              IntNetRingAllocateFrame.
 */
DECLINLINE(void) IntNetRingCommitFrame(PINTNETRINGBUF pRingBuf, PINTNETHDR pHdr)
{
    /*
     * Validate input and commit order.
     */
    INTNETRINGBUF_ASSERT_SANITY(pRingBuf);
    INTNETHDR_ASSERT_SANITY(pHdr, pRingBuf);
    Assert(pRingBuf->offWriteCom == ((uintptr_t)pHdr - (uintptr_t)pRingBuf));

    /*
     * Figure out the offWriteCom for this packet and update the ring.
     */
    const uint32_t cbFrame     = pHdr->cbFrame;
    const uint32_t cb          = RT_ALIGN_32(cbFrame, INTNETHDR_ALIGNMENT);
    uint32_t       offWriteCom = (uint32_t)((uintptr_t)pHdr - (uintptr_t)pRingBuf)
                               + pHdr->offFrame
                               + cb;
    if (offWriteCom >= pRingBuf->offEnd)
    {
        Assert(offWriteCom == pRingBuf->offEnd);
        offWriteCom = pRingBuf->offStart;
    }
    Log2(("IntNetRingCommitFrame:   offWriteCom: %#x -> %#x (R=%#x T=%#x S=%#x)\n", pRingBuf->offWriteCom, offWriteCom, pRingBuf->offReadX, pHdr->u8Type, cbFrame));
    ASMAtomicWriteU32(&pRingBuf->offWriteCom, offWriteCom);
    STAM_REL_COUNTER_ADD(&pRingBuf->cbStatWritten, cbFrame);
    STAM_REL_COUNTER_INC(&pRingBuf->cStatFrames);
}


/**
 * Commits a frame and injects a filler frame if not all of the buffer was used.
 *
 * Make sure to commit the frames in the order they've been allocated!
 *
 * @param   pRingBuf            The ring buffer.
 * @param   pHdr                The frame header returned by
 *                              IntNetRingAllocateFrame.
 * @param   cbUsed              The amount of space actually used.  This does
 *                              not include the GSO part.
 */
DECLINLINE(void) IntNetRingCommitFrameEx(PINTNETRINGBUF pRingBuf, PINTNETHDR pHdr, size_t cbUsed)
{
    /*
     * Validate input and commit order.
     */
    INTNETRINGBUF_ASSERT_SANITY(pRingBuf);
    INTNETHDR_ASSERT_SANITY(pHdr, pRingBuf);
    Assert(pRingBuf->offWriteCom == ((uintptr_t)pHdr - (uintptr_t)pRingBuf));

    if (pHdr->u8Type == INTNETHDR_TYPE_GSO)
        cbUsed += sizeof(PDMNETWORKGSO);

    /*
     * Calc the new write commit offset.
     */
    const uint32_t  cbAlignedFrame = RT_ALIGN_32(pHdr->cbFrame, INTNETHDR_ALIGNMENT);
    const uint32_t  cbAlignedUsed  = RT_ALIGN_32(cbUsed, INTNETHDR_ALIGNMENT);
    uint32_t        offWriteCom    = (uint32_t)((uintptr_t)pHdr - (uintptr_t)pRingBuf)
                                   + pHdr->offFrame
                                   + cbAlignedFrame;
    if (offWriteCom >= pRingBuf->offEnd)
    {
        Assert(offWriteCom == pRingBuf->offEnd);
        offWriteCom = pRingBuf->offStart;
    }

    /*
     * Insert a dummy frame to pad any unused space.
     */
    if (cbAlignedFrame != cbAlignedUsed)
    {
        /** @todo Later: Try unallocate the extra memory.  */
        PINTNETHDR pHdrPadding = (PINTNETHDR)((uint8_t *)pHdr + pHdr->offFrame + cbAlignedUsed);
        pHdrPadding->u8Type   = INTNETHDR_TYPE_PADDING;
        pHdrPadding->cbFrame  = cbAlignedFrame - cbAlignedUsed - sizeof(INTNETHDR);
        Assert(pHdrPadding->cbFrame == cbAlignedFrame - cbAlignedUsed - sizeof(INTNETHDR));
        pHdrPadding->offFrame = sizeof(INTNETHDR);
        pHdr->cbFrame = cbUsed; Assert(pHdr->cbFrame == cbUsed);
    }

    Log2(("IntNetRingCommitFrameEx:   offWriteCom: %#x -> %#x (R=%#x T=%#x S=%#x P=%#x)\n", pRingBuf->offWriteCom, offWriteCom, pRingBuf->offReadX, pHdr->u8Type, pHdr->cbFrame, cbAlignedFrame - cbAlignedUsed));
    ASMAtomicWriteU32(&pRingBuf->offWriteCom, offWriteCom);
    STAM_REL_COUNTER_ADD(&pRingBuf->cbStatWritten, cbUsed);
    STAM_REL_COUNTER_INC(&pRingBuf->cStatFrames);
}


/**
 * Writes a frame to the specified ring.
 *
 * Make sure you don't have any uncommitted frames when calling this function!
 *
 * @returns VINF_SUCCESS or VERR_BUFFER_OVERFLOW.
 * @param   pRingBuf            The ring buffer.
 * @param   pvFrame             The bits to write.
 * @param   cbFrame             How much to write.
 */
DECLINLINE(int) IntNetRingWriteFrame(PINTNETRINGBUF pRingBuf, const void *pvFrame, size_t cbFrame)
{
    /*
     * Validate input.
     */
    INTNETRINGBUF_ASSERT_SANITY(pRingBuf);
    Assert(cbFrame >= sizeof(RTMAC) * 2);

    /*
     * Align the size and read the volatile ring buffer variables.
     */
    const uint32_t  cb          = RT_ALIGN_32(cbFrame, INTNETHDR_ALIGNMENT);
    uint32_t        offWriteInt = ASMAtomicUoReadU32(&pRingBuf->offWriteInt);
    uint32_t        offRead     = ASMAtomicUoReadU32(&pRingBuf->offReadX);
    if (offRead <= offWriteInt)
    {
        /*
         * Try fit it all before the end of the buffer.
         */
        if (pRingBuf->offEnd - offWriteInt >= cb + sizeof(INTNETHDR))
        {
            uint32_t offNew = offWriteInt + cb + sizeof(INTNETHDR);
            if (offNew >= pRingBuf->offEnd)
                offNew = pRingBuf->offStart;
            if (RT_LIKELY(ASMAtomicCmpXchgU32(&pRingBuf->offWriteInt, offNew, offWriteInt)))
            { /* likely */ } else return VERR_WRONG_ORDER; /* race */
            Log2(("IntNetRingWriteFrame: offWriteInt: %#x -> %#x (1)\n", offWriteInt, offNew));

            PINTNETHDR pHdr = (PINTNETHDR)((uint8_t *)pRingBuf + offWriteInt);
            pHdr->u8Type   = INTNETHDR_TYPE_FRAME;
            pHdr->cbFrame  = cbFrame; Assert(pHdr->cbFrame == cbFrame);
            pHdr->offFrame = sizeof(INTNETHDR);

            memcpy(pHdr + 1, pvFrame, cbFrame);

            Log2(("IntNetRingWriteFrame: offWriteCom: %#x -> %#x (1)\n", pRingBuf->offWriteCom, offNew));
            ASMAtomicWriteU32(&pRingBuf->offWriteCom, offNew);
            STAM_REL_COUNTER_ADD(&pRingBuf->cbStatWritten, cbFrame);
            STAM_REL_COUNTER_INC(&pRingBuf->cStatFrames);
            return VINF_SUCCESS;
        }
        /*
         * Try fit the frame at the start of the buffer.
         * (The header fits before the end of the buffer because of alignment.)
         */
        AssertMsg(pRingBuf->offEnd - offWriteInt >= sizeof(INTNETHDR), ("offEnd=%x offWriteInt=%x\n", pRingBuf->offEnd, offWriteInt));
        if (offRead - pRingBuf->offStart > cb) /* not >= ! */
        {
            uint32_t offNew = pRingBuf->offStart + cb;
            if (RT_LIKELY(ASMAtomicCmpXchgU32(&pRingBuf->offWriteInt, offNew, offWriteInt)))
            { /* likely */ } else return VERR_WRONG_ORDER; /* race */
            Log2(("IntNetRingWriteFrame: offWriteInt: %#x -> %#x (2)\n", offWriteInt, offNew));

            PINTNETHDR pHdr = (PINTNETHDR)((uint8_t *)pRingBuf + offWriteInt);
            pHdr->u8Type   = INTNETHDR_TYPE_FRAME;
            pHdr->cbFrame  = cbFrame; Assert(pHdr->cbFrame == cbFrame);
            pHdr->offFrame = pRingBuf->offStart - offWriteInt;

            memcpy((uint8_t *)pRingBuf + pRingBuf->offStart, pvFrame, cbFrame);

            Log2(("IntNetRingWriteFrame: offWriteCom: %#x -> %#x (2)\n", pRingBuf->offWriteCom, offNew));
            ASMAtomicWriteU32(&pRingBuf->offWriteCom, offNew);
            STAM_REL_COUNTER_ADD(&pRingBuf->cbStatWritten, cbFrame);
            STAM_REL_COUNTER_INC(&pRingBuf->cStatFrames);
            return VINF_SUCCESS;
        }
    }
    /*
     * The reader is ahead of the writer, try fit it into that space.
     */
    else if (offRead - offWriteInt > cb + sizeof(INTNETHDR)) /* not >= ! */
    {
        uint32_t offNew = offWriteInt + cb + sizeof(INTNETHDR);
        if (RT_LIKELY(ASMAtomicCmpXchgU32(&pRingBuf->offWriteInt, offNew, offWriteInt)))
        { /* likely */ } else return VERR_WRONG_ORDER; /* race */
        Log2(("IntNetRingWriteFrame: offWriteInt: %#x -> %#x (3)\n", offWriteInt, offNew));

        PINTNETHDR pHdr = (PINTNETHDR)((uint8_t *)pRingBuf + offWriteInt);
        pHdr->u8Type   = INTNETHDR_TYPE_FRAME;
        pHdr->cbFrame  = cbFrame; Assert(pHdr->cbFrame == cbFrame);
        pHdr->offFrame = sizeof(INTNETHDR);

        memcpy(pHdr + 1, pvFrame, cbFrame);

        Log2(("IntNetRingWriteFrame: offWriteCom: %#x -> %#x (3)\n", pRingBuf->offWriteCom, offNew));
        ASMAtomicWriteU32(&pRingBuf->offWriteCom, offNew);
        STAM_REL_COUNTER_ADD(&pRingBuf->cbStatWritten, cbFrame);
        STAM_REL_COUNTER_INC(&pRingBuf->cStatFrames);
        return VINF_SUCCESS;
    }

    /* (it didn't fit) */
    STAM_REL_COUNTER_INC(&pRingBuf->cOverflows);
    return VERR_BUFFER_OVERFLOW;
}


/**
 * Reads the next frame in the buffer and moves the read cursor past it.
 *
 * @returns Size of the frame in bytes.  0 is returned if nothing in the buffer.
 * @param   pRingBuf    The ring buffer to read from.
 * @param   pvFrameDst  Where to put the frame.  The caller is responsible for
 *                      ensuring that there is sufficient space for the frame.
 *
 * @deprecated  Bad interface, do NOT use it!  Only for tstIntNetR0.
 */
DECLINLINE(uint32_t) IntNetRingReadAndSkipFrame(PINTNETRINGBUF pRingBuf, void *pvFrameDst)
{
    INTNETRINGBUF_ASSERT_SANITY(pRingBuf);

    uint32_t       offRead     = ASMAtomicUoReadU32(&pRingBuf->offReadX);
    uint32_t const offWriteCom = ASMAtomicUoReadU32(&pRingBuf->offWriteCom);
    if (offRead == offWriteCom)
        return 0;

    PINTNETHDR pHdr = (PINTNETHDR)((uint8_t *)pRingBuf + offRead);
    INTNETHDR_ASSERT_SANITY(pHdr, pRingBuf);

    uint32_t const cbFrame     = pHdr->cbFrame;
    int32_t const  offFrame    = pHdr->offFrame;
    const void    *pvFrameSrc  = (uint8_t *)pHdr + offFrame;
    memcpy(pvFrameDst, pvFrameSrc, cbFrame);
#ifdef INTNET_POISON_READ_FRAMES
    memset((void *)pvFrameSrc, 0xfe, RT_ALIGN_32(cbFrame, INTNETHDR_ALIGNMENT));
    memset(pHdr, 0xef, sizeof(*pHdr));
#endif

    /* skip the frame */
    offRead += offFrame + cbFrame;
    offRead = RT_ALIGN_32(offRead, INTNETHDR_ALIGNMENT);
    Assert(offRead <= pRingBuf->offEnd && offRead >= pRingBuf->offStart);
    if (offRead >= pRingBuf->offEnd)
        offRead = pRingBuf->offStart;
    ASMAtomicWriteU32(&pRingBuf->offReadX, offRead);
    return cbFrame;
}


/**
 * Initializes a buffer structure.
 *
 * @param   pIntBuf             The internal networking interface buffer.  This
 *                              expected to be cleared prior to calling this
 *                              function.
 * @param   cbBuf               The size of the whole buffer.
 * @param   cbRecv              The receive size.
 * @param   cbSend              The send size.
 */
DECLINLINE(void) IntNetBufInit(PINTNETBUF pIntBuf, uint32_t cbBuf, uint32_t cbRecv, uint32_t cbSend)
{
    AssertCompileSizeAlignment(INTNETBUF, INTNETHDR_ALIGNMENT);
    AssertCompileSizeAlignment(INTNETBUF, INTNETRINGBUF_ALIGNMENT);
    Assert(cbBuf >= sizeof(INTNETBUF) + cbRecv + cbSend);
    Assert(RT_ALIGN_32(cbRecv, INTNETRINGBUF_ALIGNMENT) == cbRecv);
    Assert(RT_ALIGN_32(cbSend, INTNETRINGBUF_ALIGNMENT) == cbSend);
    Assert(ASMMemIsZero(pIntBuf, cbBuf));

    pIntBuf->u32Magic  = INTNETBUF_MAGIC;
    pIntBuf->cbBuf     = cbBuf;
    pIntBuf->cbRecv    = cbRecv;
    pIntBuf->cbSend    = cbSend;

    /* receive ring buffer. */
    uint32_t offBuf = RT_ALIGN_32(sizeof(INTNETBUF), INTNETRINGBUF_ALIGNMENT) - RT_UOFFSETOF(INTNETBUF, Recv);
    pIntBuf->Recv.offStart      = offBuf;
    pIntBuf->Recv.offReadX      = offBuf;
    pIntBuf->Recv.offWriteInt   = offBuf;
    pIntBuf->Recv.offWriteCom   = offBuf;
    pIntBuf->Recv.offEnd        = offBuf + cbRecv;

    /* send ring buffer. */
    offBuf += cbRecv + RT_UOFFSETOF(INTNETBUF, Recv) - RT_UOFFSETOF(INTNETBUF, Send);
    pIntBuf->Send.offStart      = offBuf;
    pIntBuf->Send.offReadX      = offBuf;
    pIntBuf->Send.offWriteCom   = offBuf;
    pIntBuf->Send.offWriteInt   = offBuf;
    pIntBuf->Send.offEnd        = offBuf + cbSend;
    Assert(cbBuf >= offBuf + cbSend);
}

#endif /* __cplusplus */

#endif /* !VBOX_INCLUDED_intnetinline_h */

