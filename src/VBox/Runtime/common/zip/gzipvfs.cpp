/* $Id: gzipvfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - GZIP Compressor and Decompressor I/O Stream.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/zip.h>

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/vfslowlevel.h>

#include <zlib.h>

#if defined(RT_OS_OS2) || defined(RT_OS_SOLARIS) || defined(RT_OS_WINDOWS)
/**
 * Drag in the missing zlib symbols.
 */
PFNRT g_apfnRTZlibDeps[] =
{
    (PFNRT)gzrewind,
    (PFNRT)gzread,
    (PFNRT)gzopen,
    (PFNRT)gzwrite,
    (PFNRT)gzclose,
    (PFNRT)gzdopen,
    NULL
};
#endif /* RT_OS_OS2 || RT_OS_SOLARIS || RT_OS_WINDOWS */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#pragma pack(1)
typedef struct RTZIPGZIPHDR
{
    /** RTZIPGZIPHDR_ID1. */
    uint8_t     bId1;
    /** RTZIPGZIPHDR_ID2. */
    uint8_t     bId2;
    /** CM - The compression method. */
    uint8_t     bCompressionMethod;
    /** FLG - Flags. */
    uint8_t     fFlags;
    /** Modification time of the source file or the timestamp at the time the
     * compression took place. Can also be zero.  Is the number of seconds since
     * unix epoch. */
    uint32_t    u32ModTime;
    /** Flags specific to the compression method. */
    uint8_t     bXtraFlags;
    /** An ID indicating which OS or FS gzip ran on. */
    uint8_t     bOS;
} RTZIPGZIPHDR;
#pragma pack()
AssertCompileSize(RTZIPGZIPHDR, 10);
/** Pointer to a const gzip header. */
typedef RTZIPGZIPHDR const *PCRTZIPGZIPHDR;

/** gzip header identification no 1. */
#define RTZIPGZIPHDR_ID1            0x1f
/** gzip header identification no 2. */
#define RTZIPGZIPHDR_ID2            0x8b
/** gzip deflate compression method. */
#define RTZIPGZIPHDR_CM_DEFLATE     8

/** @name gzip header flags
 * @{ */
/** Probably a text file */
#define RTZIPGZIPHDR_FLG_TEXT       UINT8_C(0x01)
/** Header CRC present (crc32 of header cast to uint16_t). */
#define RTZIPGZIPHDR_FLG_HDR_CRC    UINT8_C(0x02)
/** Length prefixed xtra field is present. */
#define RTZIPGZIPHDR_FLG_EXTRA      UINT8_C(0x04)
/** A name field is present (latin-1). */
#define RTZIPGZIPHDR_FLG_NAME       UINT8_C(0x08)
/** A comment field is present (latin-1). */
#define RTZIPGZIPHDR_FLG_COMMENT    UINT8_C(0x10)
/** Mask of valid flags. */
#define RTZIPGZIPHDR_FLG_VALID_MASK UINT8_C(0x1f)
/** @}  */

/** @name gzip default xtra flag values
 * @{ */
#define RTZIPGZIPHDR_XFL_DEFLATE_MAX        UINT8_C(0x02)
#define RTZIPGZIPHDR_XFL_DEFLATE_FASTEST    UINT8_C(0x04)
/** @} */

/** @name Operating system / Filesystem IDs
 * @{ */
#define RTZIPGZIPHDR_OS_FAT             UINT8_C(0x00)
#define RTZIPGZIPHDR_OS_AMIGA           UINT8_C(0x01)
#define RTZIPGZIPHDR_OS_VMS             UINT8_C(0x02)
#define RTZIPGZIPHDR_OS_UNIX            UINT8_C(0x03)
#define RTZIPGZIPHDR_OS_VM_CMS          UINT8_C(0x04)
#define RTZIPGZIPHDR_OS_ATARIS_TOS      UINT8_C(0x05)
#define RTZIPGZIPHDR_OS_HPFS            UINT8_C(0x06)
#define RTZIPGZIPHDR_OS_MACINTOSH       UINT8_C(0x07)
#define RTZIPGZIPHDR_OS_Z_SYSTEM        UINT8_C(0x08)
#define RTZIPGZIPHDR_OS_CPM             UINT8_C(0x09)
#define RTZIPGZIPHDR_OS_TOPS_20         UINT8_C(0x0a)
#define RTZIPGZIPHDR_OS_NTFS            UINT8_C(0x0b)
#define RTZIPGZIPHDR_OS_QDOS            UINT8_C(0x0c)
#define RTZIPGZIPHDR_OS_ACORN_RISCOS    UINT8_C(0x0d)
#define RTZIPGZIPHDR_OS_UNKNOWN         UINT8_C(0xff)
/** @}  */


/**
 * The internal data of a GZIP I/O stream.
 */
typedef struct RTZIPGZIPSTREAM
{
    /** The stream we're reading or writing the compressed data from or to. */
    RTVFSIOSTREAM       hVfsIos;
    /** Set if it's a decompressor, clear if it's a compressor. */
    bool                fDecompress;
    /** Set if zlib reported a fatal error. */
    bool                fFatalError;
    /** Set if we've reached the end of the zlib stream. */
    bool                fEndOfStream;
    /** The stream offset for pfnTell, always the uncompressed data. */
    RTFOFF              offStream;
    /** The zlib stream.  */
    z_stream            Zlib;
    /** The data buffer.  */
    uint8_t             abBuffer[_64K];
    /** Scatter gather segment describing abBuffer. */
    RTSGSEG             SgSeg;
    /** Scatter gather buffer describing abBuffer. */
    RTSGBUF             SgBuf;
    /** The original file name (decompressor only). */
    char               *pszOrgName;
    /** The comment (decompressor only). */
    char               *pszComment;
    /** The gzip header. */
    RTZIPGZIPHDR        Hdr;
} RTZIPGZIPSTREAM;
/** Pointer to a the internal data of a GZIP I/O stream. */
typedef RTZIPGZIPSTREAM *PRTZIPGZIPSTREAM;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtZipGzip_FlushIt(PRTZIPGZIPSTREAM pThis, uint8_t fFlushType);


/**
 * Convert from zlib to IPRT status codes.
 *
 * This will also set the fFatalError flag when appropriate.
 *
 * @returns IPRT status code.
 * @param   pThis           The gzip I/O stream instance data.
 * @param   rc              Zlib error code.
 */
static int rtZipGzipConvertErrFromZlib(PRTZIPGZIPSTREAM pThis, int rc)
{
    switch (rc)
    {
        case Z_OK:
            return VINF_SUCCESS;

        case Z_BUF_ERROR:
            /* This isn't fatal. */
            return VINF_SUCCESS; /** @todo The code in zip.cpp treats Z_BUF_ERROR as fatal... */

        case Z_STREAM_ERROR:
            pThis->fFatalError = true;
            return VERR_ZIP_CORRUPTED;

        case Z_DATA_ERROR:
            pThis->fFatalError = true;
            return pThis->fDecompress ? VERR_ZIP_CORRUPTED : VERR_ZIP_ERROR;

        case Z_MEM_ERROR:
            pThis->fFatalError = true;
            return VERR_ZIP_NO_MEMORY;

        case Z_VERSION_ERROR:
            pThis->fFatalError = true;
            return VERR_ZIP_UNSUPPORTED_VERSION;

        case Z_ERRNO: /* We shouldn't see this status! */
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
static DECLCALLBACK(int) rtZipGzip_Close(void *pvThis)
{
    PRTZIPGZIPSTREAM pThis = (PRTZIPGZIPSTREAM)pvThis;

    int rc;
    if (pThis->fDecompress)
    {
        rc = inflateEnd(&pThis->Zlib);
        if (rc != Z_OK)
            rc = rtZipGzipConvertErrFromZlib(pThis, rc);
    }
    else
    {
        /* Flush the compression stream before terminating it. */
        rc = VINF_SUCCESS;
        if (!pThis->fFatalError)
            rc = rtZipGzip_FlushIt(pThis, Z_FINISH);

        int rc2 = deflateEnd(&pThis->Zlib);
        if (RT_SUCCESS(rc) && rc2 != Z_OK)
            rc = rtZipGzipConvertErrFromZlib(pThis, rc);
    }

    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;
    RTStrFree(pThis->pszOrgName);
    pThis->pszOrgName = NULL;
    RTStrFree(pThis->pszComment);
    pThis->pszComment = NULL;

    return rc;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipGzip_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPGZIPSTREAM pThis = (PRTZIPGZIPSTREAM)pvThis;
    return RTVfsIoStrmQueryInfo(pThis->hVfsIos, pObjInfo, enmAddAttr);
}


/**
 * Reads one segment.
 *
 * @returns IPRT status code.
 * @param   pThis           The gzip I/O stream instance data.
 * @param   pvBuf           Where to put the read bytes.
 * @param   cbToRead        The number of bytes to read.
 * @param   fBlocking       Whether to block or not.
 * @param   pcbRead         Where to store the number of bytes actually read.
 * @param   pSgBuf          The segment buffer descriptor, for advancing.
 */
static int rtZipGzip_ReadOneSeg(PRTZIPGZIPSTREAM pThis, void *pvBuf, size_t cbToRead, bool fBlocking,
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
    pThis->Zlib.next_out  = (Bytef *)pvBuf;
    pThis->Zlib.avail_out = (uInt)cbToRead;
    AssertReturn(pThis->Zlib.avail_out == cbToRead, VERR_OUT_OF_RANGE);

    /*
     * Be greedy reading input, even if no output buffer is left. It's possible
     * that it's just the end of stream marker which needs to be read. Happens
     * for incompressible blocks just larger than the input buffer size.
     */
    int rc = VINF_SUCCESS;
    while (   pThis->Zlib.avail_out > 0
           || pThis->Zlib.avail_in == 0 /* greedy */)
    {
        /*
         * Read more input?
         *
         * N.B. The assertions here validate the RTVfsIoStrmSgRead behavior
         *      since the API is new and untested.  They could be removed later
         *      but, better leaving them in.
         */
        if (pThis->Zlib.avail_in == 0)
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
                AssertMsg(rc == VINF_EOF, ("%Rrc\n", rc));
            }
            AssertMsgBreakStmt(cbReadIn > 0 && cbReadIn <= sizeof(pThis->abBuffer), ("%zu %Rrc\n", cbReadIn, rc),
                               rc = VERR_INTERNAL_ERROR_4);

            pThis->Zlib.avail_in = (uInt)cbReadIn;
            pThis->Zlib.next_in  = &pThis->abBuffer[0];
        }

        /*
         * Pass it on to zlib.
         */
        rc = inflate(&pThis->Zlib, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_BUF_ERROR)
        {
            if (rc == Z_STREAM_END)
            {
                pThis->fEndOfStream = true;
                if (pThis->Zlib.avail_out == 0)
                    rc = VINF_SUCCESS;
                else
                    rc = pcbRead ? VINF_EOF : VERR_EOF;
            }
            else
                rc = rtZipGzipConvertErrFromZlib(pThis, rc);
            break;
        }
        rc = VINF_SUCCESS;
    }

    /*
     * Update the read counters before returning.
     */
    size_t const cbRead = cbToRead - pThis->Zlib.avail_out;
    pThis->offStream += cbRead;
    if (pcbRead)
        *pcbRead      = cbRead;
    RTSgBufAdvance(pSgBuf, cbRead);

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtZipGzip_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTZIPGZIPSTREAM pThis = (PRTZIPGZIPSTREAM)pvThis;

    Assert(pSgBuf->cSegs == 1);
    if (!pThis->fDecompress)
        return VERR_ACCESS_DENIED;
    AssertReturn(off == -1 || off == pThis->offStream , VERR_INVALID_PARAMETER);

    return rtZipGzip_ReadOneSeg(pThis, pSgBuf->paSegs[0].pvSeg, pSgBuf->paSegs[0].cbSeg, fBlocking, pcbRead, pSgBuf);
}


/**
 * Internal helper for rtZipGzip_Write, rtZipGzip_Flush and rtZipGzip_Close.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS
 * @retval  VINF_TRY_AGAIN - the only informational status.
 * @retval  VERR_INTERRUPTED - call again.
 *
 * @param   pThis           The gzip I/O stream instance data.
 * @param   fBlocking       Whether to block or not.
 */
static int rtZipGzip_WriteOutputBuffer(PRTZIPGZIPSTREAM pThis, bool fBlocking)
{
    /*
     * Anything to write?  No, then just return immediately.
     */
    size_t cbToWrite = sizeof(pThis->abBuffer) - pThis->Zlib.avail_out;
    if (cbToWrite == 0)
    {
        Assert(pThis->Zlib.next_out == &pThis->abBuffer[0]);
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
     * Adjust the Zlib output buffer members.
     */
    if (cbWrittenOut == pThis->SgBuf.paSegs[0].cbSeg)
    {
        pThis->Zlib.avail_out = sizeof(pThis->abBuffer);
        pThis->Zlib.next_out  = &pThis->abBuffer[0];
    }
    else
    {
        Assert(cbWrittenOut <= pThis->SgBuf.paSegs[0].cbSeg);
        size_t cbLeft = pThis->SgBuf.paSegs[0].cbSeg - cbWrittenOut;
        memmove(&pThis->abBuffer[0], &pThis->abBuffer[cbWrittenOut], cbLeft);
        pThis->Zlib.avail_out += (uInt)cbWrittenOut;
        pThis->Zlib.next_out  = &pThis->abBuffer[cbWrittenOut];
    }

    return VINF_SUCCESS;
}


/**
 * Processes all available input.
 *
 * @returns IPRT status code.
 *
 * @param   pThis           The gzip I/O stream instance data.
 * @param   fBlocking       Whether to block or not.
 */
static int rtZipGzip_CompressIt(PRTZIPGZIPSTREAM pThis, bool fBlocking)
{
    /*
     * Processes all the intput currently lined up for us.
     */
    while (pThis->Zlib.avail_in > 0)
    {
        /* Make sure there is some space in the output buffer before calling
           deflate() so we don't waste time filling up the corners. */
        static const size_t s_cbFlushThreshold = 4096;
        AssertCompile(sizeof(pThis->abBuffer) >= s_cbFlushThreshold * 4);
        if (pThis->Zlib.avail_out < s_cbFlushThreshold)
        {
            int rc = rtZipGzip_WriteOutputBuffer(pThis, fBlocking);
            if (rc != VINF_SUCCESS)
                return rc;
            Assert(pThis->Zlib.avail_out >= s_cbFlushThreshold);
        }

        int rcZlib = deflate(&pThis->Zlib, Z_NO_FLUSH);
        if (rcZlib != Z_OK)
            return rtZipGzipConvertErrFromZlib(pThis, rcZlib);
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtZipGzip_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    PRTZIPGZIPSTREAM pThis = (PRTZIPGZIPSTREAM)pvThis;

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
            size_t cbThis = cbLeft < ~(uInt)0 ? cbLeft : ~(uInt)0 / 2;
            pThis->Zlib.next_in  = (Bytef * )pbSrc;
            pThis->Zlib.avail_in = (uInt)cbThis;
            rc = rtZipGzip_CompressIt(pThis, fBlocking);

            Assert(cbThis >= pThis->Zlib.avail_in);
            cbThis -= pThis->Zlib.avail_in;
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
 * @param   pThis           The gzip I/O stream instance data.
 * @param   fFlushType      The flush type to pass to deflate().
 */
static int rtZipGzip_FlushIt(PRTZIPGZIPSTREAM pThis, uint8_t fFlushType)
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
            rc = rtZipGzip_WriteOutputBuffer(pThis, true /*fBlocking*/);
            if (RT_FAILURE(rc))
                return rc;
            Assert(rc == VINF_SUCCESS);
        } while (pThis->Zlib.avail_out < sizeof(pThis->abBuffer));

        if (!fMaybeMore)
            return VINF_SUCCESS;

        /* Do the flushing. */
        pThis->Zlib.next_in  = NULL;
        pThis->Zlib.avail_in = 0;
        int rcZlib = deflate(&pThis->Zlib, fFlushType);
        if (rcZlib == Z_OK)
            fMaybeMore = pThis->Zlib.avail_out < 64 || fFlushType == Z_FINISH;
        else if (rcZlib == Z_STREAM_END)
            fMaybeMore = false;
        else
        {
            rtZipGzip_WriteOutputBuffer(pThis, true /*fBlocking*/);
            return rtZipGzipConvertErrFromZlib(pThis, rcZlib);
        }
    }
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtZipGzip_Flush(void *pvThis)
{
    PRTZIPGZIPSTREAM pThis = (PRTZIPGZIPSTREAM)pvThis;
    if (!pThis->fDecompress)
    {
        int rc = rtZipGzip_FlushIt(pThis, Z_SYNC_FLUSH);
        if (RT_FAILURE(rc))
            return rc;
    }

    return RTVfsIoStrmFlush(pThis->hVfsIos);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int) rtZipGzip_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                           uint32_t *pfRetEvents)
{
    PRTZIPGZIPSTREAM pThis = (PRTZIPGZIPSTREAM)pvThis;

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
        if (pThis->Zlib.avail_in > 0)
            fRetEvents = RTPOLL_EVT_READ;
    }
    else
    {
        fEvents &= ~RTPOLL_EVT_READ;
        if (pThis->Zlib.avail_out > 0)
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
static DECLCALLBACK(int) rtZipGzip_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTZIPGZIPSTREAM pThis = (PRTZIPGZIPSTREAM)pvThis;
    *poffActual = pThis->offStream;
    return VINF_SUCCESS;
}


/**
 * The GZIP I/O stream vtable.
 */
static RTVFSIOSTREAMOPS g_rtZipGzipOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_IO_STREAM,
        "gzip",
        rtZipGzip_Close,
        rtZipGzip_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSIOSTREAMOPS_VERSION,
    RTVFSIOSTREAMOPS_FEAT_NO_SG,
    rtZipGzip_Read,
    rtZipGzip_Write,
    rtZipGzip_Flush,
    rtZipGzip_PollOne,
    rtZipGzip_Tell,
    NULL /* Skip */,
    NULL /*ZeroFill*/,
    RTVFSIOSTREAMOPS_VERSION,
};


RTDECL(int) RTZipGzipDecompressIoStream(RTVFSIOSTREAM hVfsIosIn, uint32_t fFlags, PRTVFSIOSTREAM phVfsIosOut)
{
    AssertPtrReturn(hVfsIosIn, VERR_INVALID_HANDLE);
    AssertReturn(!(fFlags & ~RTZIPGZIPDECOMP_F_ALLOW_ZLIB_HDR), VERR_INVALID_PARAMETER);
    AssertPtrReturn(phVfsIosOut, VERR_INVALID_POINTER);

    uint32_t cRefs = RTVfsIoStrmRetain(hVfsIosIn);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create the decompression I/O stream.
     */
    RTVFSIOSTREAM    hVfsIos;
    PRTZIPGZIPSTREAM pThis;
    int rc = RTVfsNewIoStream(&g_rtZipGzipOps, sizeof(RTZIPGZIPSTREAM), RTFILE_O_READ, NIL_RTVFS, NIL_RTVFSLOCK,
                              &hVfsIos, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVfsIos      = hVfsIosIn;
        pThis->offStream    = 0;
        pThis->fDecompress  = true;
        pThis->SgSeg.pvSeg  = &pThis->abBuffer[0];
        pThis->SgSeg.cbSeg  = sizeof(pThis->abBuffer);
        RTSgBufInit(&pThis->SgBuf, &pThis->SgSeg, 1);

        memset(&pThis->Zlib, 0, sizeof(pThis->Zlib));
        pThis->Zlib.opaque  = pThis;
        rc = inflateInit2(&pThis->Zlib, MAX_WBITS | RT_BIT(5) /* autodetect gzip header */);
        if (rc >= 0)
        {
            /*
             * Read the gzip header from the input stream to check that it's
             * a gzip stream as specified by the user.
             *
             * Note! Since we've told zlib to check for the gzip header, we
             *       prebuffer what we read in the input buffer so it can
             *       be handed on to zlib later on.
             */
            rc = RTVfsIoStrmRead(pThis->hVfsIos, pThis->abBuffer, sizeof(RTZIPGZIPHDR), true /*fBlocking*/, NULL /*pcbRead*/);
            if (RT_SUCCESS(rc))
            {
                /* Validate the header and make a copy of it. */
                PCRTZIPGZIPHDR pHdr = (PCRTZIPGZIPHDR)pThis->abBuffer;
                if (   pHdr->bId1 == RTZIPGZIPHDR_ID1
                    && pHdr->bId2 == RTZIPGZIPHDR_ID2
                    && !(pHdr->fFlags & ~RTZIPGZIPHDR_FLG_VALID_MASK))
                {
                    if (pHdr->bCompressionMethod == RTZIPGZIPHDR_CM_DEFLATE)
                        rc = VINF_SUCCESS;
                    else
                        rc = VERR_ZIP_UNSUPPORTED_METHOD;
                }
                else if (   (fFlags & RTZIPGZIPDECOMP_F_ALLOW_ZLIB_HDR)
                         && (RT_MAKE_U16(pHdr->bId2, pHdr->bId1) % 31) == 0
                         && (pHdr->bId1 & 0xf) == RTZIPGZIPHDR_CM_DEFLATE )
                {
                    pHdr = NULL;
                    rc = VINF_SUCCESS;
                }
                else
                    rc = VERR_ZIP_BAD_HEADER;
                if (RT_SUCCESS(rc))
                {
                    pThis->Zlib.avail_in = sizeof(RTZIPGZIPHDR);
                    pThis->Zlib.next_in  = &pThis->abBuffer[0];
                    if (pHdr)
                    {
                        pThis->Hdr = *pHdr;
                        /* Parse on if there are names or comments. */
                        if (pHdr->fFlags & (RTZIPGZIPHDR_FLG_NAME | RTZIPGZIPHDR_FLG_COMMENT))
                        {
                            /** @todo Can implement this when someone needs the
                             *        name or comment for something useful. */
                        }
                    }
                    if (RT_SUCCESS(rc))
                    {
                        *phVfsIosOut = hVfsIos;
                        return VINF_SUCCESS;
                    }
                }
            }
        }
        else
            rc = rtZipGzipConvertErrFromZlib(pThis, rc); /** @todo cleaning up in this situation is going to go wrong. */
        RTVfsIoStrmRelease(hVfsIos);
    }
    else
        RTVfsIoStrmRelease(hVfsIosIn);
    return rc;
}


RTDECL(int) RTZipGzipCompressIoStream(RTVFSIOSTREAM hVfsIosDst, uint32_t fFlags, uint8_t uLevel, PRTVFSIOSTREAM phVfsIosZip)
{
    AssertPtrReturn(hVfsIosDst, VERR_INVALID_HANDLE);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phVfsIosZip, VERR_INVALID_POINTER);
    AssertReturn(uLevel > 0 && uLevel <= 9, VERR_INVALID_PARAMETER);

    uint32_t cRefs = RTVfsIoStrmRetain(hVfsIosDst);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create the compression I/O stream.
     */
    RTVFSIOSTREAM    hVfsIos;
    PRTZIPGZIPSTREAM pThis;
    int rc = RTVfsNewIoStream(&g_rtZipGzipOps, sizeof(RTZIPGZIPSTREAM), RTFILE_O_WRITE, NIL_RTVFS, NIL_RTVFSLOCK,
                              &hVfsIos, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVfsIos      = hVfsIosDst;
        pThis->offStream    = 0;
        pThis->fDecompress  = false;
        pThis->SgSeg.pvSeg  = &pThis->abBuffer[0];
        pThis->SgSeg.cbSeg  = sizeof(pThis->abBuffer);
        RTSgBufInit(&pThis->SgBuf, &pThis->SgSeg, 1);

        RT_ZERO(pThis->Zlib);
        pThis->Zlib.opaque    = pThis;
        pThis->Zlib.next_out  = &pThis->abBuffer[0];
        pThis->Zlib.avail_out = sizeof(pThis->abBuffer);

        rc = deflateInit2(&pThis->Zlib,
                          uLevel,
                          Z_DEFLATED,
                          15 /* Windows Size */ + 16 /* GZIP header */,
                          9 /* Max memory level for optimal speed */,
                          Z_DEFAULT_STRATEGY);

        if (rc >= 0)
        {
            *phVfsIosZip = hVfsIos;
            return VINF_SUCCESS;
        }

        rc = rtZipGzipConvertErrFromZlib(pThis, rc); /** @todo cleaning up in this situation is going to go wrong. */
        RTVfsIoStrmRelease(hVfsIos);
    }
    else
        RTVfsIoStrmRelease(hVfsIosDst);
    return rc;
}



/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainGunzip_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
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
static DECLCALLBACK(int) rtVfsChainGunzip_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                      PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                      PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, pElement, poffError, pErrInfo);
    AssertReturn(hPrevVfsObj != NIL_RTVFSOBJ, VERR_VFS_CHAIN_IPE);

    RTVFSIOSTREAM hVfsIosIn = RTVfsObjToIoStream(hPrevVfsObj);
    if (hVfsIosIn == NIL_RTVFSIOSTREAM)
        return VERR_VFS_CHAIN_CAST_FAILED;

    RTVFSIOSTREAM hVfsIos = NIL_RTVFSIOSTREAM;
    int rc = RTZipGzipDecompressIoStream(hVfsIosIn, 0 /*fFlags*/, &hVfsIos);
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
static DECLCALLBACK(bool) rtVfsChainGunzip_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                           PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                           PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pElement, pReuseSpec, pReuseElement);
    return false;
}


/** VFS chain element 'gunzip'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainGunzipReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "gunzip",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Takes an I/O stream and gunzips it. No arguments.",
    /* pfnValidate = */         rtVfsChainGunzip_Validate,
    /* pfnInstantiate = */      rtVfsChainGunzip_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainGunzip_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainGunzipReg, rtVfsChainGunzipReg);



/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainGzip_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
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
static DECLCALLBACK(int) rtVfsChainGzip_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                    PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                    PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, pElement, poffError, pErrInfo);
    AssertReturn(hPrevVfsObj != NIL_RTVFSOBJ, VERR_VFS_CHAIN_IPE);

    RTVFSIOSTREAM hVfsIosOut = RTVfsObjToIoStream(hPrevVfsObj);
    if (hVfsIosOut == NIL_RTVFSIOSTREAM)
        return VERR_VFS_CHAIN_CAST_FAILED;

    RTVFSIOSTREAM hVfsIos = NIL_RTVFSIOSTREAM;
    int rc = RTZipGzipCompressIoStream(hVfsIosOut, 0 /*fFlags*/, pElement->uProvider, &hVfsIos);
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
static DECLCALLBACK(bool) rtVfsChainGzip_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                         PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                         PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pElement, pReuseSpec, pReuseElement);
    return false;
}


/** VFS chain element 'gzip'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainGzipReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "gzip",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Takes an I/O stream and gzips it.\n"
                                "Optional argument specifying compression level: 1-9, default, fast",
    /* pfnValidate = */         rtVfsChainGzip_Validate,
    /* pfnInstantiate = */      rtVfsChainGzip_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainGzip_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainGzipReg, rtVfsChainGzipReg);

