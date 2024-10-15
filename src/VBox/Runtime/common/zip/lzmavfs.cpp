/* $Id: lzmavfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - XZ/LZMA Compressor and Decompressor I/O Stream.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/zip.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/crc.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/vfslowlevel.h>

#define LZMA_API_STATIC
#include <lzma.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTZIPXZHDR
{
    /** The magic. */
    uint8_t     abMagic[6];
    /** Stream flag byte 1 (always 0). */
    uint8_t     bStrmFlags1;
    /** Stream flag byte 2 (high nibble always 0 for now). */
    uint8_t     bStrmFlags2;
    /** CRC32 checksum of RTZIPXZHDR::bStrmFlags1 and RTZIPXZHDR::bStrmFlags2. */
    uint32_t    u32Crc;
} RTZIPXZHDR;
AssertCompileSize(RTZIPXZHDR, 12);
/** Pointer to an XZ stream header. */
typedef RTZIPXZHDR *PRTZIPXZHDR;
/** Pointer to a constant XZ stream header. */
typedef const RTZIPXZHDR *PCRTZIPXZHDR;

/** The header magic. */
#define RTZIPXZHDR_MAGIC        { 0xfd, '7', 'z', 'X', 'Z', 0x00 }
/** None check */
#define RTZIPXZHDR_CHECK_NONE   0x00
/** CRC32 check */
#define RTZIPXZHDR_CHECK_CRC32  0x01
/** CRC64 check */
#define RTZIPXZHDR_CHECK_CRC64  0x04
/** SHA-256 check */
#define RTZIPXZHDR_CHECK_SHA256 0x0a


/**
 * The internal data of a XZ/LZMA I/O stream.
 */
typedef struct RTZIPLZMASTREAM
{
    /** The stream we're reading or writing the compressed data from or to. */
    RTVFSIOSTREAM       hVfsIos;
    /** Set if it's a decompressor, clear if it's a compressor. */
    bool                fDecompress;
    /** Set if liblzma reported a fatal error. */
    bool                fFatalError;
    /** Set if we've reached the end of the stream. */
    bool                fEndOfStream;
    /** The stream offset for pfnTell, always the uncompressed data. */
    RTFOFF              offStream;
    /** The lzma decoder stream.  */
    lzma_stream         Lzma;
    /** The current lzma action to use as a parameter to lzma_code(). */
    lzma_action         enmLzmaAction;
    /** The data buffer.  */
    uint8_t             abBuffer[_64K];
    /** Scatter gather segment describing abBuffer. */
    RTSGSEG             SgSeg;
    /** Scatter gather buffer describing abBuffer. */
    RTSGBUF             SgBuf;
} RTZIPLZMASTREAM;
/** Pointer to a the internal data of a LZMA I/O stream. */
typedef RTZIPLZMASTREAM *PRTZIPLZMASTREAM;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtZipLzma_FlushIt(PRTZIPLZMASTREAM pThis, lzma_action enmFlushType);


/**
 * Convert from liblzma to IPRT status codes.
 *
 * This will also set the fFatalError flag when appropriate.
 *
 * @returns IPRT status code.
 * @param   pThis           The gzip I/O stream instance data.
 * @param   rc              Zlib error code.
 */
static int rtZipLzmaConvertErrFromLzma(PRTZIPLZMASTREAM pThis, lzma_ret rc)
{
    switch (rc)
    {
        case LZMA_OK:
            return VINF_SUCCESS;

        case LZMA_BUF_ERROR:
            /* This isn't fatal. */
            return VINF_SUCCESS;

        case LZMA_FORMAT_ERROR:
            pThis->fFatalError = true;
            return VERR_ZIP_CORRUPTED;

        case LZMA_DATA_ERROR:
            pThis->fFatalError = true;
            return pThis->fDecompress ? VERR_ZIP_CORRUPTED : VERR_ZIP_ERROR;

        case LZMA_MEM_ERROR:
            pThis->fFatalError = true;
            return VERR_ZIP_NO_MEMORY;

        case LZMA_PROG_ERROR:
            pThis->fFatalError = true;
            return VERR_INTERNAL_ERROR;

        default:
            AssertMsgFailed(("%d\n", rc));
            if (rc >= 0)
                return VINF_SUCCESS;
            pThis->fFatalError = true;
            return VERR_ZIP_ERROR;
    }
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipLzma_Close(void *pvThis)
{
    PRTZIPLZMASTREAM pThis = (PRTZIPLZMASTREAM)pvThis;

    int rc = VINF_SUCCESS;
    if (pThis->fDecompress)
        lzma_end(&pThis->Lzma);
    else
    {
        /* Flush the compression stream before terminating it. */
        if (!pThis->fFatalError)
            rc = rtZipLzma_FlushIt(pThis, LZMA_FINISH);

        lzma_end(&pThis->Lzma);
    }

    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;

    return rc;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipLzma_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPLZMASTREAM pThis = (PRTZIPLZMASTREAM)pvThis;
    return RTVfsIoStrmQueryInfo(pThis->hVfsIos, pObjInfo, enmAddAttr);
}


/**
 * Reads one segment.
 *
 * @returns IPRT status code.
 * @param   pThis           The lzma I/O stream instance data.
 * @param   pvBuf           Where to put the read bytes.
 * @param   cbToRead        The number of bytes to read.
 * @param   fBlocking       Whether to block or not.
 * @param   pcbRead         Where to store the number of bytes actually read.
 * @param   pSgBuf          The S/G buffer descriptor, for advancing.
 */
static int rtZipLzma_ReadOneSeg(PRTZIPLZMASTREAM pThis, void *pvBuf, size_t cbToRead, bool fBlocking,
                                size_t *pcbRead, PRTSGBUF pSgBuf)
{
    /*
     * This simplifies life a wee bit below.
     */
    if (pThis->fEndOfStream)
        return pcbRead ? VINF_EOF : VERR_EOF;

    /*
     * Set up the output buffer.
     */
    pThis->Lzma.next_out  = (uint8_t *)pvBuf;
    pThis->Lzma.avail_out = cbToRead;

    /*
     * Be greedy reading input, even if no output buffer is left. It's possible
     * that it's just the end of stream marker which needs to be read. Happens
     * for incompressible blocks just larger than the input buffer size.
     */
    int rc = VINF_SUCCESS;
    while (   pThis->Lzma.avail_out > 0
           || pThis->Lzma.avail_in == 0 /* greedy */)
    {
        /* Read more input? */
        if (pThis->Lzma.avail_in == 0)
        {
            size_t cbReadIn = ~(size_t)0;
            RTSgBufReset(&pThis->SgBuf);
            rc = RTVfsIoStrmSgRead(pThis->hVfsIos, -1 /*off*/, &pThis->SgBuf, fBlocking, &cbReadIn);
            if (rc != VINF_SUCCESS)
            {
                AssertMsg(RT_FAILURE(rc) || rc == VINF_TRY_AGAIN || rc == VINF_EOF, ("%Rrc\n", rc));
                if (rc == VERR_INTERRUPTED)
                {
                    Assert(cbReadIn == 0);
                    continue;
                }
                if (RT_FAILURE(rc) || rc == VINF_TRY_AGAIN || cbReadIn == 0)
                {
                    Assert(cbReadIn == 0);
                    break;
                }
                else
                {
                    AssertMsg(rc == VINF_EOF, ("%Rrc\n", rc));
                    pThis->enmLzmaAction = LZMA_FINISH;
                }
            }
            AssertMsgBreakStmt(cbReadIn > 0 && cbReadIn <= sizeof(pThis->abBuffer), ("%zu %Rrc\n", cbReadIn, rc),
                               rc = VERR_INTERNAL_ERROR_4);

            pThis->Lzma.avail_in = cbReadIn;
            pThis->Lzma.next_in  = &pThis->abBuffer[0];
        }

        /*
         * Pass it on to lzma.
         */
        lzma_ret rcLzma = lzma_code(&pThis->Lzma, pThis->enmLzmaAction);
        if (rcLzma != LZMA_OK && rcLzma != LZMA_BUF_ERROR)
        {
            if (rcLzma == LZMA_STREAM_END)
            {
                pThis->fEndOfStream = true;
                if (pThis->Lzma.avail_out == 0)
                    rc = VINF_SUCCESS;
                else
                    rc = pcbRead ? VINF_EOF : VERR_EOF;
            }
            else
                rc = rtZipLzmaConvertErrFromLzma(pThis, rcLzma);
            break;
        }
        rc = VINF_SUCCESS;
    }

    /*
     * Update the read counters before returning.
     */
    size_t const cbRead = cbToRead - pThis->Lzma.avail_out;
    pThis->offStream += cbRead;
    if (pcbRead)
        *pcbRead      = cbRead;
    RTSgBufAdvance(pSgBuf, cbRead);

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtZipLzma_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTZIPLZMASTREAM pThis = (PRTZIPLZMASTREAM)pvThis;

    Assert(pSgBuf->cSegs == 1);
    if (!pThis->fDecompress)
        return VERR_ACCESS_DENIED;
    AssertReturn(off == -1 || off == pThis->offStream , VERR_INVALID_PARAMETER);

    return rtZipLzma_ReadOneSeg(pThis, pSgBuf->paSegs[0].pvSeg, pSgBuf->paSegs[0].cbSeg, fBlocking, pcbRead, pSgBuf);
}


/**
 * Internal helper for rtZipLzma_Write, rtZipLzma_Flush and rtZipLzma_Close.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS
 * @retval  VINF_TRY_AGAIN - the only informational status.
 * @retval  VERR_INTERRUPTED - call again.
 *
 * @param   pThis           The lzma I/O stream instance data.
 * @param   fBlocking       Whether to block or not.
 */
static int rtZipLzma_WriteOutputBuffer(PRTZIPLZMASTREAM pThis, bool fBlocking)
{
    /*
     * Anything to write?  No, then just return immediately.
     */
    size_t cbToWrite = sizeof(pThis->abBuffer) - pThis->Lzma.avail_out;
    if (cbToWrite == 0)
    {
        Assert(pThis->Lzma.next_out == &pThis->abBuffer[0]);
        return VINF_SUCCESS;
    }
    Assert(cbToWrite <= sizeof(pThis->abBuffer));

    /*
     * Loop write on VERR_INTERRUPTED.
     *
     * Note! Asserting a bit extra here to make sure the
     *       RTVfsIoStrmSgWrite works correctly.
     */
    int    rc;
    size_t cbWrittenOut;
    for (;;)
    {
        /* Set up the buffer. */
        pThis->SgSeg.cbSeg = cbToWrite;
        Assert(pThis->SgSeg.pvSeg == &pThis->abBuffer[0]);
        RTSgBufReset(&pThis->SgBuf);

        cbWrittenOut = ~(size_t)0;
        rc = RTVfsIoStrmSgWrite(pThis->hVfsIos, -1 /*off*/, &pThis->SgBuf, fBlocking, &cbWrittenOut);
        if (rc != VINF_SUCCESS)
        {
            AssertMsg(RT_FAILURE(rc) || rc == VINF_TRY_AGAIN, ("%Rrc\n", rc));
            if (rc == VERR_INTERRUPTED)
            {
                Assert(cbWrittenOut == 0);
                continue;
            }
            if (RT_FAILURE(rc) || rc == VINF_TRY_AGAIN || cbWrittenOut == 0)
            {
                AssertReturn(cbWrittenOut == 0, VERR_INTERNAL_ERROR_3);
                AssertReturn(rc != VINF_SUCCESS, VERR_IPE_UNEXPECTED_INFO_STATUS);
                return rc;
            }
        }
        break;
    }
    AssertMsgReturn(cbWrittenOut > 0 && cbWrittenOut <= sizeof(pThis->abBuffer),
                    ("%zu %Rrc\n", cbWrittenOut, rc),
                    VERR_INTERNAL_ERROR_4);

    /*
     * Adjust the Lzma output buffer members.
     */
    if (cbWrittenOut == pThis->SgBuf.paSegs[0].cbSeg)
    {
        pThis->Lzma.avail_out = sizeof(pThis->abBuffer);
        pThis->Lzma.next_out  = &pThis->abBuffer[0];
    }
    else
    {
        Assert(cbWrittenOut <= pThis->SgBuf.paSegs[0].cbSeg);
        size_t cbLeft = pThis->SgBuf.paSegs[0].cbSeg - cbWrittenOut;
        memmove(&pThis->abBuffer[0], &pThis->abBuffer[cbWrittenOut], cbLeft);
        pThis->Lzma.avail_out += cbWrittenOut;
        pThis->Lzma.next_out  = &pThis->abBuffer[cbWrittenOut];
    }

    return VINF_SUCCESS;
}


/**
 * Processes all available input.
 *
 * @returns IPRT status code.
 *
 * @param   pThis           The lzma I/O stream instance data.
 * @param   fBlocking       Whether to block or not.
 */
static int rtZipLzma_CompressIt(PRTZIPLZMASTREAM pThis, bool fBlocking)
{
    /*
     * Processes all the input currently lined up for us.
     */
    while (pThis->Lzma.avail_in > 0)
    {
        /* Make sure there is some space in the output buffer before calling
           deflate() so we don't waste time filling up the corners. */
        static const size_t s_cbFlushThreshold = 4096;
        AssertCompile(sizeof(pThis->abBuffer) >= s_cbFlushThreshold * 4);
        if (pThis->Lzma.avail_out < s_cbFlushThreshold)
        {
            int rc = rtZipLzma_WriteOutputBuffer(pThis, fBlocking);
            if (rc != VINF_SUCCESS)
                return rc;
            Assert(pThis->Lzma.avail_out >= s_cbFlushThreshold);
        }

        lzma_ret rcLzma = lzma_code(&pThis->Lzma, LZMA_RUN);
        if (rcLzma != LZMA_OK)
            return rtZipLzmaConvertErrFromLzma(pThis, rcLzma);
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtZipLzma_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    PRTZIPLZMASTREAM pThis = (PRTZIPLZMASTREAM)pvThis;

    Assert(pSgBuf->cSegs == 1); NOREF(fBlocking);
    if (pThis->fDecompress)
        return VERR_ACCESS_DENIED;
    AssertReturn(off == -1 || off == pThis->offStream , VERR_INVALID_PARAMETER);

    /*
     * Write out the input buffer. Using a loop here because of potential
     * integer type overflow since avail_in is uInt and cbSeg is size_t.
     */
    int             rc        = VINF_SUCCESS;
    size_t          cbWritten = 0;
    uint8_t const  *pbSrc     = (uint8_t const *)pSgBuf->paSegs[0].pvSeg;
    size_t          cbLeft    = pSgBuf->paSegs[0].cbSeg;
    if (cbLeft > 0)
        for (;;)
        {
            size_t cbThis = cbLeft < ~(size_t)0 ? cbLeft : ~(size_t)0 / 2;
            pThis->Lzma.next_in  = pbSrc;
            pThis->Lzma.avail_in = cbThis;
            rc = rtZipLzma_CompressIt(pThis, fBlocking);

            Assert(cbThis >= pThis->Lzma.avail_in);
            cbThis -= pThis->Lzma.avail_in;
            cbWritten += cbThis;
            if (cbLeft == cbThis || rc != VINF_SUCCESS)
                break;
            pbSrc  += cbThis;
            cbLeft -= cbThis;
        }

    pThis->offStream += cbWritten;
    if (pcbWritten)
        *pcbWritten = cbWritten;
    RTSgBufAdvance(pSgBuf, cbWritten);

    return rc;
}


/**
 * Processes all available input.
 *
 * @returns IPRT status code.
 *
 * @param   pThis           The lzma I/O stream instance data.
 * @param   enmFlushType    The flush type to pass to lzma_run().
 */
static int rtZipLzma_FlushIt(PRTZIPLZMASTREAM pThis, lzma_action enmFlushType)
{
    /*
     * Tell Zlib to flush until it stops producing more output.
     */
    int rc;
    bool fMaybeMore = true;
    for (;;)
    {
        /* Write the entire output buffer. */
        do
        {
            rc = rtZipLzma_WriteOutputBuffer(pThis, true /*fBlocking*/);
            if (RT_FAILURE(rc))
                return rc;
            Assert(rc == VINF_SUCCESS);
        } while (pThis->Lzma.avail_out < sizeof(pThis->abBuffer));

        if (!fMaybeMore)
            return VINF_SUCCESS;

        /* Do the flushing. */
        pThis->Lzma.next_in  = NULL;
        pThis->Lzma.avail_in = 0;
        lzma_ret rcLzma = lzma_code(&pThis->Lzma, enmFlushType);
        if (rcLzma == LZMA_OK)
            fMaybeMore = pThis->Lzma.avail_out < 64 || enmFlushType == LZMA_FINISH;
        else if (rcLzma == LZMA_STREAM_END)
            fMaybeMore = false;
        else
        {
            rtZipLzma_WriteOutputBuffer(pThis, true /*fBlocking*/);
            return rtZipLzmaConvertErrFromLzma(pThis, rcLzma);
        }
    }
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtZipLzma_Flush(void *pvThis)
{
    PRTZIPLZMASTREAM pThis = (PRTZIPLZMASTREAM)pvThis;
    if (!pThis->fDecompress)
    {
        int rc = rtZipLzma_FlushIt(pThis, LZMA_SYNC_FLUSH);
        if (RT_FAILURE(rc))
            return rc;
    }

    return RTVfsIoStrmFlush(pThis->hVfsIos);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int) rtZipLzma_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                           uint32_t *pfRetEvents)
{
    PRTZIPLZMASTREAM pThis = (PRTZIPLZMASTREAM)pvThis;

    /*
     * Collect our own events first and see if that satisfies the request.  If
     * not forward the call to the compressed stream.
     */
    uint32_t fRetEvents = 0;
    if (pThis->fFatalError)
        fRetEvents |= RTPOLL_EVT_ERROR;
    if (pThis->fDecompress)
    {
        fEvents &= ~RTPOLL_EVT_WRITE;
        if (pThis->Lzma.avail_in > 0)
            fRetEvents = RTPOLL_EVT_READ;
    }
    else
    {
        fEvents &= ~RTPOLL_EVT_READ;
        if (pThis->Lzma.avail_out > 0)
            fRetEvents = RTPOLL_EVT_WRITE;
    }

    int rc = VINF_SUCCESS;
    fRetEvents &= fEvents;
    if (!fRetEvents)
        rc = RTVfsIoStrmPoll(pThis->hVfsIos, fEvents, cMillies, fIntr, pfRetEvents);
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtZipLzma_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTZIPLZMASTREAM pThis = (PRTZIPLZMASTREAM)pvThis;
    *poffActual = pThis->offStream;
    return VINF_SUCCESS;
}


/**
 * The LZMA I/O stream vtable.
 */
static RTVFSIOSTREAMOPS g_rtZipLzmaOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_IO_STREAM,
        "lzma",
        rtZipLzma_Close,
        rtZipLzma_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSIOSTREAMOPS_VERSION,
    RTVFSIOSTREAMOPS_FEAT_NO_SG,
    rtZipLzma_Read,
    rtZipLzma_Write,
    rtZipLzma_Flush,
    rtZipLzma_PollOne,
    rtZipLzma_Tell,
    NULL /* Skip */,
    NULL /*ZeroFill*/,
    RTVFSIOSTREAMOPS_VERSION,
};


RTDECL(int) RTZipXzDecompressIoStream(RTVFSIOSTREAM hVfsIosIn, uint32_t fFlags, PRTVFSIOSTREAM phVfsIosOut)
{
    AssertPtrReturn(hVfsIosIn, VERR_INVALID_HANDLE);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phVfsIosOut, VERR_INVALID_POINTER);

    uint32_t cRefs = RTVfsIoStrmRetain(hVfsIosIn);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create the decompression I/O stream.
     */
    RTVFSIOSTREAM    hVfsIos;
    PRTZIPLZMASTREAM pThis;
    int rc = RTVfsNewIoStream(&g_rtZipLzmaOps, sizeof(*pThis), RTFILE_O_READ, NIL_RTVFS, NIL_RTVFSLOCK,
                              &hVfsIos, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVfsIos       = hVfsIosIn;
        pThis->offStream     = 0;
        pThis->fDecompress   = true;
        pThis->SgSeg.pvSeg   = &pThis->abBuffer[0];
        pThis->SgSeg.cbSeg   = sizeof(pThis->abBuffer);
        pThis->enmLzmaAction = LZMA_RUN;
        RTSgBufInit(&pThis->SgBuf, &pThis->SgSeg, 1);

        memset(&pThis->Lzma, 0, sizeof(pThis->Lzma));
        lzma_ret rcLzma = lzma_stream_decoder(&pThis->Lzma, UINT64_MAX, LZMA_CONCATENATED);
        if (rcLzma == LZMA_OK)
        {
            /*
             * Read the XZ header from the input stream to check that it's
             * a xz stream as specified by the user.
             */
            rc = RTVfsIoStrmRead(pThis->hVfsIos, pThis->abBuffer, sizeof(RTZIPXZHDR), true /*fBlocking*/, NULL /*pcbRead*/);
            if (RT_SUCCESS(rc))
            {
                /* Validate the header. */
                static const uint8_t s_abXzMagic[6] = RTZIPXZHDR_MAGIC;
                PCRTZIPXZHDR pHdr = (PCRTZIPXZHDR)pThis->abBuffer;
                uint8_t bChkMethod = pHdr->bStrmFlags2 & 0xf;

                if (   !memcmp(&pHdr->abMagic[0], &s_abXzMagic[0], sizeof(s_abXzMagic))
                    && pHdr->bStrmFlags1 == 0
                    && (pHdr->bStrmFlags2 & 0xf0) == 0
                    && (   bChkMethod == RTZIPXZHDR_CHECK_NONE
                        || bChkMethod == RTZIPXZHDR_CHECK_CRC32
                        || bChkMethod == RTZIPXZHDR_CHECK_CRC64
                        || bChkMethod == RTZIPXZHDR_CHECK_SHA256)
                    && RTCrc32(&pHdr->bStrmFlags1, 2) == RT_LE2H_U32(pHdr->u32Crc))
                {
                    pThis->Lzma.avail_in = sizeof(RTZIPXZHDR);
                    pThis->Lzma.next_in  = &pThis->abBuffer[0];
                    *phVfsIosOut = hVfsIos;
                    return VINF_SUCCESS;
                }
                else
                    rc = VERR_ZIP_BAD_HEADER;
            }
        }
        else
            rc = rtZipLzmaConvertErrFromLzma(pThis, rcLzma);
        RTVfsIoStrmRelease(hVfsIos);
    }
    else
        RTVfsIoStrmRelease(hVfsIosIn);
    return rc;
}


RTDECL(int) RTZipXzCompressIoStream(RTVFSIOSTREAM hVfsIosDst, uint32_t fFlags, uint8_t uLevel, PRTVFSIOSTREAM phVfsIosXz)
{
    AssertPtrReturn(hVfsIosDst, VERR_INVALID_HANDLE);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phVfsIosXz, VERR_INVALID_POINTER);
    AssertReturn(uLevel > 0 && uLevel <= 9, VERR_INVALID_PARAMETER);

    uint32_t cRefs = RTVfsIoStrmRetain(hVfsIosDst);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create the compression I/O stream.
     */
    RTVFSIOSTREAM    hVfsIos;
    PRTZIPLZMASTREAM pThis;
    int rc = RTVfsNewIoStream(&g_rtZipLzmaOps, sizeof(RTZIPLZMASTREAM), RTFILE_O_WRITE, NIL_RTVFS, NIL_RTVFSLOCK,
                              &hVfsIos, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVfsIos      = hVfsIosDst;
        pThis->offStream    = 0;
        pThis->fDecompress  = false;
        pThis->SgSeg.pvSeg  = &pThis->abBuffer[0];
        pThis->SgSeg.cbSeg  = sizeof(pThis->abBuffer);
        RTSgBufInit(&pThis->SgBuf, &pThis->SgSeg, 1);

        RT_ZERO(pThis->Lzma);
        pThis->Lzma.next_out  = &pThis->abBuffer[0];
        pThis->Lzma.avail_out = sizeof(pThis->abBuffer);

        lzma_ret rcLzma = lzma_easy_encoder(&pThis->Lzma, uLevel, LZMA_CHECK_CRC64);
        if (rcLzma == LZMA_OK)
        {
            *phVfsIosXz = hVfsIos;
            return VINF_SUCCESS;
        }

        rc = rtZipLzmaConvertErrFromLzma(pThis, rcLzma);
        RTVfsIoStrmRelease(hVfsIos);
    }
    else
        RTVfsIoStrmRelease(hVfsIosDst);
    return rc;
}



/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainXzDec_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
                                                  PRTVFSCHAINELEMSPEC pElement, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, poffError, pErrInfo);

    if (pElement->enmType != RTVFSOBJTYPE_IO_STREAM)
        return VERR_VFS_CHAIN_ONLY_IOS;
    if (pElement->enmTypeIn == RTVFSOBJTYPE_INVALID)
        return VERR_VFS_CHAIN_CANNOT_BE_FIRST_ELEMENT;
    if (   pElement->enmTypeIn != RTVFSOBJTYPE_FILE
        && pElement->enmTypeIn != RTVFSOBJTYPE_IO_STREAM)
        return VERR_VFS_CHAIN_TAKES_FILE_OR_IOS;
    if (pSpec->fOpenFile & RTFILE_O_WRITE)
        return VERR_VFS_CHAIN_READ_ONLY_IOS;
    if (pElement->cArgs != 0)
        return VERR_VFS_CHAIN_NO_ARGS;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnInstantiate}
 */
static DECLCALLBACK(int) rtVfsChainXzDec_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                     PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                     PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, pElement, poffError, pErrInfo);
    AssertReturn(hPrevVfsObj != NIL_RTVFSOBJ, VERR_VFS_CHAIN_IPE);

    RTVFSIOSTREAM hVfsIosIn = RTVfsObjToIoStream(hPrevVfsObj);
    if (hVfsIosIn == NIL_RTVFSIOSTREAM)
        return VERR_VFS_CHAIN_CAST_FAILED;

    RTVFSIOSTREAM hVfsIos = NIL_RTVFSIOSTREAM;
    int rc = RTZipXzDecompressIoStream(hVfsIosIn, 0 /*fFlags*/, &hVfsIos);
    RTVfsObjFromIoStream(hVfsIosIn);
    if (RT_SUCCESS(rc))
    {
        *phVfsObj = RTVfsObjFromIoStream(hVfsIos);
        RTVfsIoStrmRelease(hVfsIos);
        if (*phVfsObj != NIL_RTVFSOBJ)
            return VINF_SUCCESS;
        rc = VERR_VFS_CHAIN_CAST_FAILED;
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnCanReuseElement}
 */
static DECLCALLBACK(bool) rtVfsChainXzDec_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                          PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                          PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pElement, pReuseSpec, pReuseElement);
    return false;
}


/** VFS chain element 'lzmadec'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainXzDecReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "xzdec",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Takes an xz compressed I/O stream and decompresses it. No arguments.",
    /* pfnValidate = */         rtVfsChainXzDec_Validate,
    /* pfnInstantiate = */      rtVfsChainXzDec_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainXzDec_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainXzDecReg, rtVfsChainXzDecReg);



/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainXz_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
                                               PRTVFSCHAINELEMSPEC pElement, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg);

    /*
     * Basics.
     */
    if (pElement->enmType != RTVFSOBJTYPE_IO_STREAM)
        return VERR_VFS_CHAIN_ONLY_IOS;
    if (pElement->enmTypeIn == RTVFSOBJTYPE_INVALID)
        return VERR_VFS_CHAIN_CANNOT_BE_FIRST_ELEMENT;
    if (   pElement->enmTypeIn != RTVFSOBJTYPE_FILE
        && pElement->enmTypeIn != RTVFSOBJTYPE_IO_STREAM)
        return VERR_VFS_CHAIN_TAKES_FILE_OR_IOS;
    if (pSpec->fOpenFile & RTFILE_O_READ)
        return VERR_VFS_CHAIN_WRITE_ONLY_IOS;
    if (pElement->cArgs > 1)
        return VERR_VFS_CHAIN_AT_MOST_ONE_ARG;

    /*
     * Optional argument 1..9 indicating the compression level.
     * We store it in pSpec->uProvider.
     */
    if (pElement->cArgs > 0)
    {
        const char *psz = pElement->paArgs[0].psz;
        if (!*psz || !strcmp(psz, "default"))
            pElement->uProvider = 6;
        else if (!strcmp(psz, "fast"))
            pElement->uProvider = 3;
        else if (   RT_C_IS_DIGIT(*psz)
                 && *psz != '0'
                 && *RTStrStripL(psz + 1) == '\0')
            pElement->uProvider = *psz - '0';
        else
        {
            *poffError = pElement->paArgs[0].offSpec;
            return RTErrInfoSet(pErrInfo, VERR_VFS_CHAIN_INVALID_ARGUMENT, "Expected compression level: 1-9, default, or fast");
        }
    }
    else
        pElement->uProvider = 6;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnInstantiate}
 */
static DECLCALLBACK(int) rtVfsChainXz_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                  PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                  PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, pElement, poffError, pErrInfo);
    AssertReturn(hPrevVfsObj != NIL_RTVFSOBJ, VERR_VFS_CHAIN_IPE);

    RTVFSIOSTREAM hVfsIosOut = RTVfsObjToIoStream(hPrevVfsObj);
    if (hVfsIosOut == NIL_RTVFSIOSTREAM)
        return VERR_VFS_CHAIN_CAST_FAILED;

    RTVFSIOSTREAM hVfsIos = NIL_RTVFSIOSTREAM;
    int rc = RTZipXzCompressIoStream(hVfsIosOut, 0 /*fFlags*/, pElement->uProvider, &hVfsIos);
    RTVfsObjFromIoStream(hVfsIosOut);
    if (RT_SUCCESS(rc))
    {
        *phVfsObj = RTVfsObjFromIoStream(hVfsIos);
        RTVfsIoStrmRelease(hVfsIos);
        if (*phVfsObj != NIL_RTVFSOBJ)
            return VINF_SUCCESS;
        rc = VERR_VFS_CHAIN_CAST_FAILED;
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnCanReuseElement}
 */
static DECLCALLBACK(bool) rtVfsChainXz_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                       PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                       PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pElement, pReuseSpec, pReuseElement);
    return false;
}


/** VFS chain element 'xz'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainXzReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "xz",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Takes an I/O stream and compresses it with xz.\n"
                                "Optional argument specifying compression level: 1-9, default, fast",
    /* pfnValidate = */         rtVfsChainXz_Validate,
    /* pfnInstantiate = */      rtVfsChainXz_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainXz_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainXzReg, rtVfsChainXzReg);
