/* $Id: zip.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Compression.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define RTZIP_USE_STORE 1
#define RTZIP_USE_ZLIB 1
//#define RTZIP_USE_BZLIB 1
#if !defined(IN_GUEST) && !defined(IPRT_NO_CRT)
# define RTZIP_USE_LZF 1
#endif
#define RTZIP_LZF_BLOCK_BY_BLOCK
//#define RTZIP_USE_LZJB 1
//#define RTZIP_USE_LZO 1

/** @todo FastLZ? QuickLZ? Others? */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/cdefs.h>
#ifdef RTZIP_USE_BZLIB
# include <bzlib.h>
#endif
#ifdef RTZIP_USE_ZLIB
# include <zlib.h>
#endif
#ifdef RTZIP_USE_LZF
 RT_C_DECLS_BEGIN
#  include <lzf.h>
 RT_C_DECLS_END
# include <iprt/crc.h>
#endif
#ifdef RTZIP_USE_LZJB
# include "lzjb.h"
#endif
#ifdef RTZIP_USE_LZO
# include <lzo/lzo1x.h>
#endif

#include <iprt/zip.h>
#include "internal/iprt.h"

/*#include <iprt/asm.h>*/
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/string.h>

#ifndef IPRT_NO_CRT
# include <errno.h>
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

#ifdef RTZIP_USE_LZF

/**
 * LZF block header.
 */
#pragma pack(1)                         /* paranoia */
typedef struct RTZIPLZFHDR
{
    /** Magic word (RTZIPLZFHDR_MAGIC). */
    uint16_t    u16Magic;
    /** The number of bytes of data following this header. */
    uint16_t    cbData;
    /** The CRC32 of the block. */
    uint32_t    u32CRC;
    /** The size of the uncompressed data in bytes. */
    uint16_t    cbUncompressed;
} RTZIPLZFHDR;
#pragma pack()
/** Pointer to a LZF block header. */
typedef RTZIPLZFHDR *PRTZIPLZFHDR;
/** Pointer to a const LZF block header. */
typedef const RTZIPLZFHDR *PCRTZIPLZFHDR;

/** The magic of a LZF block header. */
#define RTZIPLZFHDR_MAGIC                       ('Z' | ('V' << 8))

/** The max compressed data size.
 * The maximum size of a block is currently 16KB.
 * This is very important so we don't have to move input buffers around. */
#define RTZIPLZF_MAX_DATA_SIZE                  (16384 - sizeof(RTZIPLZFHDR))

/** The max uncompressed data size.
 * This is important so we don't overflow the spill buffer in the decompressor. */
#define RTZIPLZF_MAX_UNCOMPRESSED_DATA_SIZE     (32*_1K)

#endif /* RTZIP_USE_LZF */


/**
 * Compressor/Decompressor instance data.
 */
typedef struct RTZIPCOMP
{
    /** Output buffer. */
    uint8_t             abBuffer[_128K];
    /** Compression output consumer. */
    PFNRTZIPOUT         pfnOut;
    /** User argument for the callback. */
    void               *pvUser;

    /**
     * @copydoc RTZipCompress
     */
    DECLCALLBACKMEMBER(int, pfnCompress,(PRTZIPCOMP pZip, const void *pvBuf, size_t cbBuf));

    /**
     * @copydoc RTZipCompFinish
     */
    DECLCALLBACKMEMBER(int, pfnFinish,(PRTZIPCOMP pZip));

    /**
     * @copydoc RTZipCompDestroy
     */
    DECLCALLBACKMEMBER(int, pfnDestroy,(PRTZIPCOMP pZip));

    /** Compression type. */
    RTZIPTYPE           enmType;
    /** Type specific data. */
    union
    {
#ifdef RTZIP_USE_STORE
        /** Simple storing. */
        struct
        {
            /** Current buffer position. (where to start write) */
            uint8_t    *pb;
        } Store;
#endif
#ifdef RTZIP_USE_ZLIB
        /** Zlib stream. */
        z_stream        Zlib;
#endif
#ifdef RTZIP_USE_BZLIB
        /** BZlib stream. */
        bz_stream       BZlib;
#endif
#ifdef RTZIP_USE_LZF
        /** LZF stream. */
        struct
        {
            /** Current output buffer position. */
            uint8_t    *pbOutput;
            /** The input buffer position. */
            uint8_t    *pbInput;
            /** The number of free bytes in the input buffer. */
            size_t      cbInputFree;
            /** The input buffer. */
            uint8_t     abInput[RTZIPLZF_MAX_UNCOMPRESSED_DATA_SIZE];
        } LZF;
#endif

    } u;
} RTZIPCOMP;



/**
 * Decompressor instance data.
 */
typedef struct RTZIPDECOMP
{
    /** Input buffer. */
    uint8_t             abBuffer[_128K];
    /** Decompression input producer. */
    PFNRTZIPIN          pfnIn;
    /** User argument for the callback. */
    void               *pvUser;

    /**
     * @copydoc RTZipDecompress
     */
    DECLCALLBACKMEMBER(int, pfnDecompress,(PRTZIPDECOMP pZip, void *pvBuf, size_t cbBuf, size_t *pcbWritten));

    /**
     * @copydoc RTZipDecompDestroy
     */
    DECLCALLBACKMEMBER(int, pfnDestroy,(PRTZIPDECOMP pZip));

    /** Compression type. */
    RTZIPTYPE           enmType;
    /** Type specific data. */
    union
    {
#ifdef RTZIP_USE_STORE
        /** Simple storing. */
        struct
        {
            /** Current buffer position. (where to start read) */
            uint8_t    *pb;
            /** Number of bytes left in the buffer. */
            size_t      cbBuffer;
        } Store;
#endif
#ifdef RTZIP_USE_ZLIB
        /** Zlib stream. */
        z_stream        Zlib;
#endif
#ifdef RTZIP_USE_BZLIB
        /** BZlib stream. */
        bz_stream       BZlib;
#endif
#ifdef RTZIP_USE_LZF
        /** LZF 'stream'. */
        struct
        {
# ifndef RTZIP_LZF_BLOCK_BY_BLOCK
            /** Current input buffer position. */
            uint8_t    *pbInput;
            /** The number of bytes left in the input buffer. */
            size_t      cbInput;
# endif
            /** The spill buffer.
             * LZF is a block based compressor and not a stream compressor. So,
             * we have to decompress full blocks if we want to get any of the data.
             * This buffer is to store the spill after decompressing a block. */
            uint8_t     abSpill[RTZIPLZF_MAX_UNCOMPRESSED_DATA_SIZE];
            /** The number of bytes left spill buffer. */
            unsigned    cbSpill;
            /** The current spill buffer position. */
            uint8_t    *pbSpill;
        } LZF;
#endif

    } u;
} RTZIPDECOM;



#ifdef RTZIP_USE_STORE

/**
 * @copydoc RTZipCompress
 */
static DECLCALLBACK(int) rtZipStoreCompress(PRTZIPCOMP pZip, const void *pvBuf, size_t cbBuf)
{
    uint8_t *pbDst = pZip->u.Store.pb;
    while (cbBuf)
    {
        /*
         * Flush.
         */
        size_t cb = sizeof(pZip->abBuffer) - (size_t)(pbDst - &pZip->abBuffer[0]); /* careful here, g++ 4.1.2 screws up easily */
        if (cb == 0)
        {
            int rc = pZip->pfnOut(pZip->pvUser, &pZip->abBuffer[0], sizeof(pZip->abBuffer));
            if (RT_FAILURE(rc))
                return rc;

            cb = sizeof(pZip->abBuffer);
            pbDst = &pZip->abBuffer[0];
        }

        /*
         * Add to the buffer and advance.
         */
        if (cbBuf < cb)
            cb = cbBuf;
        memcpy(pbDst, pvBuf, cb);

        pbDst += cb;
        cbBuf -= cb;
        pvBuf = (uint8_t *)pvBuf + cb;
    }
    pZip->u.Store.pb = pbDst;
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipCompFinish
 */
static DECLCALLBACK(int) rtZipStoreCompFinish(PRTZIPCOMP pZip)
{
    size_t cb = (uintptr_t)pZip->u.Store.pb - (uintptr_t)&pZip->abBuffer[0];
    if (cb > 0)
    {
        int rc = pZip->pfnOut(pZip->pvUser, &pZip->abBuffer[0], cb);
        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipCompDestroy
 */
static DECLCALLBACK(int) rtZipStoreCompDestroy(PRTZIPCOMP pZip)
{
    NOREF(pZip);
    return VINF_SUCCESS;
}


/**
 * Initializes the compressor instance.
 * @returns iprt status code.
 * @param   pZip        The compressor instance.
 * @param   enmLevel    The desired compression level.
 */
static DECLCALLBACK(int) rtZipStoreCompInit(PRTZIPCOMP pZip, RTZIPLEVEL enmLevel)
{
    NOREF(enmLevel);
    pZip->pfnCompress = rtZipStoreCompress;
    pZip->pfnFinish   = rtZipStoreCompFinish;
    pZip->pfnDestroy  = rtZipStoreCompDestroy;

    pZip->u.Store.pb = &pZip->abBuffer[1];
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipDecompress
 */
static DECLCALLBACK(int) rtZipStoreDecompress(PRTZIPDECOMP pZip, void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    size_t cbWritten = 0;
    while (cbBuf)
    {
        /*
         * Fill buffer.
         */
        size_t cb = pZip->u.Store.cbBuffer;
        if (cb <= 0)
        {
            int rc = pZip->pfnIn(pZip->pvUser, &pZip->abBuffer[0], sizeof(pZip->abBuffer), &cb);
            if (RT_FAILURE(rc))
                return rc;
            pZip->u.Store.cbBuffer = cb;
            pZip->u.Store.pb = &pZip->abBuffer[0];
        }

        /*
         * No more data?
         */
        if (cb == 0)
        {
            if (pcbWritten)
            {
                *pcbWritten = cbWritten;
                return VINF_SUCCESS;
            }
            return VERR_NO_DATA;
        }

        /*
         * Add to the buffer and advance.
         */
        if (cbBuf < cb)
            cb = cbBuf;
        memcpy(pvBuf, pZip->u.Store.pb, cb);
        pZip->u.Store.pb += cb;
        pZip->u.Store.cbBuffer -= cb;
        cbBuf -= cb;
        pvBuf = (char *)pvBuf + cb;
        cbWritten += cb;
    }
    if (pcbWritten)
        *pcbWritten = cbWritten;
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipDecompDestroy
 */
static DECLCALLBACK(int) rtZipStoreDecompDestroy(PRTZIPDECOMP pZip)
{
    NOREF(pZip);
    return VINF_SUCCESS;
}


/**
 * Initialize the decompressor instance.
 * @returns iprt status code.
 * @param   pZip        The decompressor instance.
 */
static DECLCALLBACK(int) rtZipStoreDecompInit(PRTZIPDECOMP pZip)
{
    pZip->pfnDecompress = rtZipStoreDecompress;
    pZip->pfnDestroy = rtZipStoreDecompDestroy;

    pZip->u.Store.pb = &pZip->abBuffer[0];
    pZip->u.Store.cbBuffer = 0;
    return VINF_SUCCESS;
}

#endif /* RTZIP_USE_STORE */


#ifdef RTZIP_USE_ZLIB

/*
 * Missing definitions from zutil.h. We need these constants for calling
 * inflateInit2() / deflateInit2().
 */
# ifndef Z_DEF_WBITS
#  define Z_DEF_WBITS        MAX_WBITS
# endif
# ifndef Z_DEF_MEM_LEVEL
#  define Z_DEF_MEM_LEVEL    8
# endif

/**
 * Convert from zlib errno to iprt status code.
 * @returns iprt status code.
 * @param   rc              Zlib error code.
 * @param   fCompressing    Set if we're compressing, clear if decompressing.
 */
static int zipErrConvertFromZlib(int rc, bool fCompressing)
{
    switch (rc)
    {
        case Z_OK:
            return VINF_SUCCESS;

        case Z_STREAM_ERROR:
            return VERR_ZIP_CORRUPTED;

        case Z_DATA_ERROR:
            return fCompressing ? VERR_ZIP_ERROR : VERR_ZIP_CORRUPTED;

        case Z_MEM_ERROR:
            return VERR_ZIP_NO_MEMORY;

        case Z_BUF_ERROR:
            return VERR_ZIP_ERROR;

        case Z_VERSION_ERROR:
            return VERR_ZIP_UNSUPPORTED_VERSION;

        case Z_ERRNO: /* We shouldn't see this status! */
        default:
            AssertMsgFailed(("%d\n", rc));
            if (rc >= 0)
                return VINF_SUCCESS;
            return VERR_ZIP_ERROR;
    }
}


/**
 * @copydoc RTZipCompress
 */
static DECLCALLBACK(int) rtZipZlibCompress(PRTZIPCOMP pZip, const void *pvBuf, size_t cbBuf)
{
    pZip->u.Zlib.next_in  = (Bytef *)pvBuf;
    pZip->u.Zlib.avail_in = (uInt)cbBuf;                    Assert(pZip->u.Zlib.avail_in == cbBuf);
    while (pZip->u.Zlib.avail_in > 0)
    {
        /*
         * Flush output buffer?
         */
        if (pZip->u.Zlib.avail_out <= 0)
        {
            int rc = pZip->pfnOut(pZip->pvUser, &pZip->abBuffer[0], sizeof(pZip->abBuffer) - pZip->u.Zlib.avail_out);
            if (RT_FAILURE(rc))
                return rc;
            pZip->u.Zlib.avail_out = sizeof(pZip->abBuffer);
            pZip->u.Zlib.next_out = &pZip->abBuffer[0];
        }

        /*
         * Pass it on to zlib.
         */
        int rc = deflate(&pZip->u.Zlib, Z_NO_FLUSH);
        if (rc != Z_OK)
            return zipErrConvertFromZlib(rc, true /*fCompressing*/);
    }
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipCompFinish
 */
static DECLCALLBACK(int) rtZipZlibCompFinish(PRTZIPCOMP pZip)
{
    int rc = Z_OK;
    for (;;)
    {
        /*
         * Flush outstanding stuff. writes.
         */
        if (rc == Z_STREAM_END || pZip->u.Zlib.avail_out <= 0)
        {
            int rc2 = pZip->pfnOut(pZip->pvUser, &pZip->abBuffer[0], sizeof(pZip->abBuffer) - pZip->u.Zlib.avail_out);
            if (RT_FAILURE(rc2))
                return rc2;
            pZip->u.Zlib.avail_out = sizeof(pZip->abBuffer);
            pZip->u.Zlib.next_out = &pZip->abBuffer[0];
            if (rc == Z_STREAM_END)
                return VINF_SUCCESS;
        }

        /*
         * Tell zlib to flush.
         */
        rc = deflate(&pZip->u.Zlib, Z_FINISH);
        if (rc != Z_OK && rc != Z_STREAM_END)
            return zipErrConvertFromZlib(rc, true /*fCompressing*/);
    }
}


/**
 * @copydoc RTZipCompDestroy
 */
static DECLCALLBACK(int) rtZipZlibCompDestroy(PRTZIPCOMP pZip)
{
    /*
     * Terminate the deflate instance.
     */
    int rc = deflateEnd(&pZip->u.Zlib);
    if (rc != Z_OK)
        rc = zipErrConvertFromZlib(rc, true /*fCompressing*/);
    return rc;
}


/**
 * Initializes the compressor instance.
 * @returns iprt status code.
 * @param   pZip        The compressor instance.
 * @param   enmLevel    The desired compression level.
 * @param   fZlibHeader If true, write the Zlib header.
 */
static DECLCALLBACK(int) rtZipZlibCompInit(PRTZIPCOMP pZip, RTZIPLEVEL enmLevel, bool fZlibHeader)
{
    pZip->pfnCompress = rtZipZlibCompress;
    pZip->pfnFinish   = rtZipZlibCompFinish;
    pZip->pfnDestroy  = rtZipZlibCompDestroy;

    int iLevel = Z_DEFAULT_COMPRESSION;
    switch (enmLevel)
    {
        case RTZIPLEVEL_STORE:      iLevel = 0; break;
        case RTZIPLEVEL_FAST:       iLevel = 2; break;
        case RTZIPLEVEL_DEFAULT:    iLevel = Z_DEFAULT_COMPRESSION; break;
        case RTZIPLEVEL_MAX:        iLevel = 9; break;
    }

    memset(&pZip->u.Zlib, 0, sizeof(pZip->u.Zlib));
    pZip->u.Zlib.next_out  = &pZip->abBuffer[1];
    pZip->u.Zlib.avail_out = sizeof(pZip->abBuffer) - 1;
    pZip->u.Zlib.opaque    = pZip;

    int rc = deflateInit2(&pZip->u.Zlib, iLevel, Z_DEFLATED, fZlibHeader ? Z_DEF_WBITS : -Z_DEF_WBITS,
                          Z_DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY);
    return rc >= 0 ? rc = VINF_SUCCESS : zipErrConvertFromZlib(rc, true /*fCompressing*/);
}


/**
 * @copydoc RTZipDecompress
 */
static DECLCALLBACK(int) rtZipZlibDecompress(PRTZIPDECOMP pZip, void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    pZip->u.Zlib.next_out = (Bytef *)pvBuf;
    pZip->u.Zlib.avail_out = (uInt)cbBuf;
    Assert(pZip->u.Zlib.avail_out == cbBuf);

    /*
     * Be greedy reading input, even if no output buffer is left. It's possible
     * that it's just the end of stream marker which needs to be read. Happens
     * for incompressible blocks just larger than the input buffer size.
     */
    while (pZip->u.Zlib.avail_out > 0 || pZip->u.Zlib.avail_in <= 0)
    {
        /*
         * Read more input?
         */
        if (pZip->u.Zlib.avail_in <= 0)
        {
            size_t cb = sizeof(pZip->abBuffer);
            int rc = pZip->pfnIn(pZip->pvUser, &pZip->abBuffer[0], sizeof(pZip->abBuffer), &cb);
            if (RT_FAILURE(rc))
                return rc;
            pZip->u.Zlib.avail_in = (uInt)cb;               Assert(pZip->u.Zlib.avail_in == cb);
            pZip->u.Zlib.next_in = &pZip->abBuffer[0];
        }

        /*
         * Pass it on to zlib.
         */
        int rc = inflate(&pZip->u.Zlib, Z_NO_FLUSH);
        if (rc == Z_STREAM_END)
        {
            if (pcbWritten)
                *pcbWritten = cbBuf - pZip->u.Zlib.avail_out;
            else if (pZip->u.Zlib.avail_out > 0)
                return VERR_NO_DATA;
            break;
        }
        if (rc != Z_OK)
            return zipErrConvertFromZlib(rc, false /*fCompressing*/);
    }
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipDecompDestroy
 */
static DECLCALLBACK(int) rtZipZlibDecompDestroy(PRTZIPDECOMP pZip)
{
    /*
     * Terminate the deflate instance.
     */
    int rc = inflateEnd(&pZip->u.Zlib);
    if (rc != Z_OK)
        rc = zipErrConvertFromZlib(rc, false /*fCompressing*/);
    return rc;
}


/**
 * Initialize the decompressor instance.
 * @returns iprt status code.
 * @param   pZip        The decompressor instance.
 * @param   fZlibHeader If true, expect the Zlib header.
 */
static DECLCALLBACK(int) rtZipZlibDecompInit(PRTZIPDECOMP pZip, bool fZlibHeader)
{
    pZip->pfnDecompress = rtZipZlibDecompress;
    pZip->pfnDestroy = rtZipZlibDecompDestroy;

    memset(&pZip->u.Zlib, 0, sizeof(pZip->u.Zlib));
    pZip->u.Zlib.opaque    = pZip;

    int rc = inflateInit2(&pZip->u.Zlib, fZlibHeader ? Z_DEF_WBITS : -Z_DEF_WBITS);
    return rc >= 0 ? VINF_SUCCESS : zipErrConvertFromZlib(rc, false /*fCompressing*/);
}

#endif /* RTZIP_USE_ZLIB */


#ifdef RTZIP_USE_BZLIB
/**
 * Convert from BZlib errno to iprt status code.
 * @returns iprt status code.
 * @param   rc      BZlib error code.
 */
static int zipErrConvertFromBZlib(int rc)
{
    /** @todo proper bzlib error conversion. */
    switch (rc)
    {
        case BZ_SEQUENCE_ERROR:
            AssertMsgFailed(("BZ_SEQUENCE_ERROR shall not happen!\n"));
            return VERR_GENERAL_FAILURE;
        case BZ_PARAM_ERROR:
            return VERR_INVALID_PARAMETER;
        case BZ_MEM_ERROR:
            return VERR_NO_MEMORY;
        case BZ_DATA_ERROR:
        case BZ_DATA_ERROR_MAGIC:
        case BZ_IO_ERROR:
        case BZ_UNEXPECTED_EOF:
        case BZ_CONFIG_ERROR:
            return VERR_GENERAL_FAILURE;
        case BZ_OUTBUFF_FULL:
            AssertMsgFailed(("BZ_OUTBUFF_FULL shall not happen!\n"));
            return VERR_GENERAL_FAILURE;
        default:
            if (rc >= 0)
                return VINF_SUCCESS;
            return VERR_GENERAL_FAILURE;
    }
}


/**
 * @copydoc RTZipCompress
 */
static DECLCALLBACK(int) rtZipBZlibCompress(PRTZIPCOMP pZip, const void *pvBuf, size_t cbBuf)
{
    pZip->u.BZlib.next_in  = (char *)pvBuf;
    pZip->u.BZlib.avail_in = cbBuf;
    while (pZip->u.BZlib.avail_in > 0)
    {
        /*
         * Flush output buffer?
         */
        if (pZip->u.BZlib.avail_out <= 0)
        {
            int rc = pZip->pfnOut(pZip->pvUser, &pZip->abBuffer[0], sizeof(pZip->abBuffer) - pZip->u.BZlib.avail_out);
            if (RT_FAILURE(rc))
                return rc;
            pZip->u.BZlib.avail_out = sizeof(pZip->abBuffer);
            pZip->u.BZlib.next_out = (char *)&pZip->abBuffer[0];
        }

        /*
         * Pass it on to zlib.
         */
        int rc = BZ2_bzCompress(&pZip->u.BZlib, BZ_RUN);
        if (rc < 0 && rc != BZ_OUTBUFF_FULL)
            return zipErrConvertFromBZlib(rc);
    }
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipCompFinish
 */
static DECLCALLBACK(int) rtZipBZlibCompFinish(PRTZIPCOMP pZip)
{
    int rc = BZ_FINISH_OK;
    for (;;)
    {
        /*
         * Flush output buffer?
         */
        if (rc == BZ_STREAM_END || pZip->u.BZlib.avail_out <= 0)
        {
            int rc2 = pZip->pfnOut(pZip->pvUser, &pZip->abBuffer[0], sizeof(pZip->abBuffer) - pZip->u.BZlib.avail_out);
            if (RT_FAILURE(rc2))
                return rc2;
            pZip->u.BZlib.avail_out = sizeof(pZip->abBuffer);
            pZip->u.BZlib.next_out = (char *)&pZip->abBuffer[0];
            if (rc == BZ_STREAM_END)
                return VINF_SUCCESS;
        }

        /*
         * Tell BZlib to finish it.
         */
        rc = BZ2_bzCompress(&pZip->u.BZlib, BZ_FINISH);
        if (rc < 0 && rc != BZ_OUTBUFF_FULL)
            return zipErrConvertFromBZlib(rc);
    }
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipCompDestroy
 */
static DECLCALLBACK(int) rtZipBZlibCompDestroy(PRTZIPCOMP pZip)
{
    /*
     * Terminate the deflate instance.
     */
    int rc = BZ2_bzCompressEnd(&pZip->u.BZlib);
    if (rc != BZ_OK)
        rc = zipErrConvertFromBZlib(rc);
    return rc;
}


/**
 * Initializes the compressor instance.
 * @returns iprt status code.
 * @param   pZip        The compressor instance.
 * @param   enmLevel    The desired compression level.
 */
static DECLCALLBACK(int) rtZipBZlibCompInit(PRTZIPCOMP pZip, RTZIPLEVEL enmLevel)
{
    pZip->pfnCompress = rtZipBZlibCompress;
    pZip->pfnFinish   = rtZipBZlibCompFinish;
    pZip->pfnDestroy  = rtZipBZlibCompDestroy;

    int iSize = 6;
    int iWork = 0;
    switch (enmLevel)
    {
        case RTZIPLEVEL_STORE:      iSize = 1; iWork = 2; break;
        case RTZIPLEVEL_FAST:       iSize = 2; iWork = 0; break;
        case RTZIPLEVEL_DEFAULT:    iSize = 5; iWork = 0; break;
        case RTZIPLEVEL_MAX:        iSize = 9; iWork = 0; break;
    }

    memset(&pZip->u.BZlib, 0, sizeof(pZip->u.BZlib));
    pZip->u.BZlib.next_out  = (char *)&pZip->abBuffer[1];
    pZip->u.BZlib.avail_out = sizeof(pZip->abBuffer) - 1;
    pZip->u.BZlib.opaque    = pZip;

    int rc = BZ2_bzCompressInit(&pZip->u.BZlib, iSize, 0, iWork);
    return rc >= 0 ? VINF_SUCCESS : zipErrConvertFromBZlib(rc);;
}


/**
 * @copydoc RTZipDecompress
 */
static DECLCALLBACK(int) rtZipBZlibDecompress(PRTZIPDECOMP pZip, void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    pZip->u.BZlib.next_out  = (char *)pvBuf;
    pZip->u.BZlib.avail_out = cbBuf;
    while (pZip->u.BZlib.avail_out > 0)
    {
        /*
         * Read more output buffer?
         */
        if (pZip->u.BZlib.avail_in <= 0)
        {
            size_t cb;
            int rc = pZip->pfnIn(pZip->pvUser, &pZip->abBuffer[0], sizeof(pZip->abBuffer), &cb);
            if (RT_FAILURE(rc))
                return rc;
            pZip->u.BZlib.avail_in = cb;
            pZip->u.BZlib.next_in = (char *)&pZip->abBuffer[0];
        }

        /*
         * Pass it on to zlib.
         */
        int rc = BZ2_bzDecompress(&pZip->u.BZlib);
        if (rc == BZ_STREAM_END || rc == BZ_OUTBUFF_FULL)
        {
            if (pcbWritten)
                *pcbWritten = cbBuf - pZip->u.BZlib.avail_out;
            else if (pZip->u.BZlib.avail_out > 0)
                return VERR_NO_DATA;
            break;
        }
        if (rc < 0)
            return zipErrConvertFromBZlib(rc);
    }
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipDecompDestroy
 */
static DECLCALLBACK(int) rtZipBZlibDecompDestroy(PRTZIPDECOMP pZip)
{
    /*
     * Terminate the deflate instance.
     */
    int rc = BZ2_bzDecompressEnd(&pZip->u.BZlib);
    if (rc != BZ_OK)
        rc = zipErrConvertFromBZlib(rc);
    return rc;
}


/**
 * Initialize the decompressor instance.
 * @returns iprt status code.
 * @param   pZip        The decompressor instance.
 */
static DECLCALLBACK(int) rtZipBZlibDecompInit(PRTZIPDECOMP pZip)
{
    pZip->pfnDecompress = rtZipBZlibDecompress;
    pZip->pfnDestroy = rtZipBZlibDecompDestroy;

    memset(&pZip->u.BZlib, 0, sizeof(pZip->u.BZlib));
    pZip->u.BZlib.opaque    = pZip;

    int rc = BZ2_bzDecompressInit(&pZip->u.BZlib, 0, 0);
    return rc >= 0 ? VINF_SUCCESS : zipErrConvertFromBZlib(rc);
}

#endif /* RTZIP_USE_BZLIB */


#ifdef RTZIP_USE_LZF

/**
 * Flushes the output buffer.
 * @returns iprt status code.
 * @param   pZip        The compressor instance.
 */
static int rtZipLZFCompFlushOutput(PRTZIPCOMP pZip)
{
    size_t      cb = pZip->u.LZF.pbOutput - &pZip->abBuffer[0];
    pZip->u.LZF.pbOutput = &pZip->abBuffer[0];
    return pZip->pfnOut(pZip->pvUser, &pZip->abBuffer[0], cb);
}


/**
 * Compresses a buffer using LZF.
 *
 * @returns VBox status code.
 * @param   pZip        The compressor instance.
 * @param   pbBuf       What to compress.
 * @param   cbBuf       How much to compress.
 */
static int rtZipLZFCompressBuffer(PRTZIPCOMP pZip, const uint8_t *pbBuf, size_t cbBuf)
{
    bool fForceFlush = false;
    while (cbBuf > 0)
    {
        /*
         * Flush output buffer?
         */
        unsigned cbFree = (unsigned)(sizeof(pZip->abBuffer) - (pZip->u.LZF.pbOutput - &pZip->abBuffer[0]));
        if (    fForceFlush
            ||  cbFree < RTZIPLZF_MAX_DATA_SIZE + sizeof(RTZIPLZFHDR))
        {
            int rc = rtZipLZFCompFlushOutput(pZip);
            if (RT_FAILURE(rc))
                return rc;
            fForceFlush = false;
            cbFree = sizeof(pZip->abBuffer);
        }

        /*
         * Setup the block header.
         */
        PRTZIPLZFHDR pHdr = (PRTZIPLZFHDR)pZip->u.LZF.pbOutput; /* warning: This might be unaligned! */
        pHdr->u16Magic = RTZIPLZFHDR_MAGIC;
        pHdr->cbData = 0;
        pHdr->u32CRC = 0;
        pHdr->cbUncompressed = 0;
        cbFree -= sizeof(*pHdr);
        pZip->u.LZF.pbOutput += sizeof(*pHdr);

        /*
         * Compress data for the block.
         *
         * We try compress as much as we have freespace for at first,
         * but if it turns out the compression is inefficient, we'll
         * reduce the size of data we try compress till it fits the
         * output space.
         */
        cbFree = RT_MIN(cbFree, RTZIPLZF_MAX_DATA_SIZE);
        unsigned cbInput = (unsigned)RT_MIN(RTZIPLZF_MAX_UNCOMPRESSED_DATA_SIZE, cbBuf);
        unsigned cbOutput = lzf_compress(pbBuf, cbInput, pZip->u.LZF.pbOutput, cbFree);
        if (!cbOutput)
        {
            /** @todo add an alternative method which stores the raw data if bad compression. */
            do
            {
                cbInput /= 2;
                if (!cbInput)
                {
                    AssertMsgFailed(("lzf_compress bug! cbFree=%zu\n", cbFree));
                    return VERR_INTERNAL_ERROR;
                }
                cbOutput = lzf_compress(pbBuf, cbInput, pZip->u.LZF.pbOutput, cbFree);
            } while (!cbOutput);
            fForceFlush = true;
        }

        /*
         * Update the header and advance the input buffer.
         */
        pHdr->cbData = cbOutput;
        //pHdr->u32CRC = RTCrc32(pbBuf, cbInput); - too slow
        pHdr->cbUncompressed = cbInput;

        pZip->u.LZF.pbOutput += cbOutput;
        cbBuf -= cbInput;
        pbBuf += cbInput;
    }
    return VINF_SUCCESS;
}


/**
 * Flushes the input buffer.
 * @returns iprt status code.
 * @param   pZip        The compressor instance.
 */
static int rtZipLZFCompFlushInput(PRTZIPCOMP pZip)
{
    size_t cb = pZip->u.LZF.pbInput - &pZip->u.LZF.abInput[0];
    pZip->u.LZF.pbInput = &pZip->u.LZF.abInput[0];
    pZip->u.LZF.cbInputFree = sizeof(pZip->u.LZF.abInput);
    if (cb)
        return rtZipLZFCompressBuffer(pZip, pZip->u.LZF.abInput, cb);
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipCompress
 */
static DECLCALLBACK(int) rtZipLZFCompress(PRTZIPCOMP pZip, const void *pvBuf, size_t cbBuf)
{
#define RTZIPLZF_SMALL_CHUNK (128)

    /*
     * Flush the input buffer if necessary.
     */
    if (    (   cbBuf <= RTZIPLZF_SMALL_CHUNK
             && cbBuf > pZip->u.LZF.cbInputFree)
        ||  (   cbBuf > RTZIPLZF_SMALL_CHUNK
             && pZip->u.LZF.cbInputFree != sizeof(pZip->u.LZF.abInput))
       )
    {
        int rc = rtZipLZFCompFlushInput(pZip);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * If it's a relativly small block put it in the input buffer, elsewise
     * compress directly it.
     */
    if (cbBuf <= RTZIPLZF_SMALL_CHUNK)
    {
        Assert(pZip->u.LZF.cbInputFree >= cbBuf);
        memcpy(pZip->u.LZF.pbInput, pvBuf, cbBuf);
        pZip->u.LZF.pbInput += cbBuf;
        pZip->u.LZF.cbInputFree -= cbBuf;
    }
    else
    {
        Assert(pZip->u.LZF.cbInputFree == sizeof(pZip->u.LZF.abInput));
        int rc = rtZipLZFCompressBuffer(pZip, (const uint8_t *)pvBuf, cbBuf);
        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipCompFinish
 */
static DECLCALLBACK(int) rtZipLZFCompFinish(PRTZIPCOMP pZip)
{
    int rc = rtZipLZFCompFlushInput(pZip);
    if (RT_SUCCESS(rc))
        rc = rtZipLZFCompFlushOutput(pZip);
    return rc;
}


/**
 * @copydoc RTZipCompDestroy
 */
static DECLCALLBACK(int) rtZipLZFCompDestroy(PRTZIPCOMP pZip)
{
    NOREF(pZip);
    return VINF_SUCCESS;
}


/**
 * Initializes the compressor instance.
 * @returns iprt status code.
 * @param   pZip        The compressor instance.
 * @param   enmLevel    The desired compression level.
 */
static DECLCALLBACK(int) rtZipLZFCompInit(PRTZIPCOMP pZip, RTZIPLEVEL enmLevel)
{
    NOREF(enmLevel);
    pZip->pfnCompress = rtZipLZFCompress;
    pZip->pfnFinish   = rtZipLZFCompFinish;
    pZip->pfnDestroy  = rtZipLZFCompDestroy;

    pZip->u.LZF.pbOutput = &pZip->abBuffer[1];
    pZip->u.LZF.pbInput  = &pZip->u.LZF.abInput[0];
    pZip->u.LZF.cbInputFree = sizeof(pZip->u.LZF.abInput);
    return VINF_SUCCESS;
}


/**
 * This will validate a header and to all the necessary bitching if it's invalid.
 * @returns true if valid.
 * @returns false if invalid.
 * @param   pHdr        Pointer to the header.\
 */
static bool rtZipLZFValidHeader(PCRTZIPLZFHDR pHdr)
{
    if (    pHdr->u16Magic != RTZIPLZFHDR_MAGIC
        ||  !pHdr->cbData
        ||  pHdr->cbData > RTZIPLZF_MAX_DATA_SIZE
        ||  !pHdr->cbUncompressed
        ||  pHdr->cbUncompressed > RTZIPLZF_MAX_UNCOMPRESSED_DATA_SIZE
       )
    {
        AssertMsgFailed(("Invalid LZF header! %.*Rhxs\n", sizeof(*pHdr), pHdr));
        return false;
    }
    return true;
}


/**
 * @copydoc RTZipDecompress
 */
static DECLCALLBACK(int) rtZipLZFDecompress(PRTZIPDECOMP pZip, void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    /*
     * Decompression loop.
     *
     * This a bit ugly because we have to deal with reading block...
     * To simplify matters we've put a max block size and will never
     * fill the input buffer with more than allows us to complete
     * any partially read blocks.
     *
     * When possible we decompress directly to the user buffer, when
     * not possible we'll use the spill buffer.
     */
# ifdef RTZIP_LZF_BLOCK_BY_BLOCK
    size_t cbWritten = 0;
    while (cbBuf > 0)
    {
        /*
         * Anything in the spill buffer?
         */
        if (pZip->u.LZF.cbSpill > 0)
        {
            unsigned cb = (unsigned)RT_MIN(pZip->u.LZF.cbSpill, cbBuf);
            memcpy(pvBuf, pZip->u.LZF.pbSpill, cb);
            pZip->u.LZF.pbSpill += cb;
            pZip->u.LZF.cbSpill -= cb;
            cbWritten += cb;
            cbBuf -= cb;
            if (!cbBuf)
                break;
            pvBuf = (uint8_t *)pvBuf + cb;
        }

        /*
         * We always read and work one block at a time.
         */
        RTZIPLZFHDR Hdr;
        int rc = pZip->pfnIn(pZip->pvUser, &Hdr, sizeof(Hdr), NULL);
        if (RT_FAILURE(rc))
            return rc;
        if (!rtZipLZFValidHeader(&Hdr))
            return VERR_GENERAL_FAILURE; /** @todo Get better error codes for RTZip! */
        if (Hdr.cbData > 0)
        {
            rc = pZip->pfnIn(pZip->pvUser, &pZip->abBuffer[0], Hdr.cbData, NULL);
            if (RT_FAILURE(rc))
                return rc;
        }

        /*
         * Does the uncompressed data fit into the supplied buffer?
         * If so we uncompress it directly into the user buffer, else we'll have to use the spill buffer.
         */
        unsigned cbUncompressed = Hdr.cbUncompressed;
        if (cbUncompressed <= cbBuf)
        {
            unsigned cbOutput = lzf_decompress(&pZip->abBuffer[0], Hdr.cbData, pvBuf, cbUncompressed);
            if (cbOutput != cbUncompressed)
            {
# ifndef IPRT_NO_CRT /* no errno */
                AssertMsgFailed(("Decompression error, errno=%d. cbOutput=%#x cbUncompressed=%#x\n",
                                 errno, cbOutput, cbUncompressed));
# endif
                return VERR_GENERAL_FAILURE; /** @todo Get better error codes for RTZip! */
            }
            cbBuf -= cbUncompressed;
            pvBuf = (uint8_t *)pvBuf + cbUncompressed;
            cbWritten += cbUncompressed;
        }
        else
        {
            unsigned cbOutput = lzf_decompress(&pZip->abBuffer[0], Hdr.cbData, pZip->u.LZF.abSpill, cbUncompressed);
            if (cbOutput != cbUncompressed)
            {
# ifndef IPRT_NO_CRT /* no errno */
                AssertMsgFailed(("Decompression error, errno=%d. cbOutput=%#x cbUncompressed=%#x\n",
                                 errno, cbOutput, cbUncompressed));
# endif
                return VERR_GENERAL_FAILURE; /** @todo Get better error codes for RTZip! */
            }
            pZip->u.LZF.pbSpill = &pZip->u.LZF.abSpill[0];
            pZip->u.LZF.cbSpill = cbUncompressed;
        }
    }

    if (pcbWritten)
        *pcbWritten = cbWritten;
# else  /* !RTZIP_LZF_BLOCK_BY_BLOCK */
    while (cbBuf > 0)
    {
        /*
         * Anything in the spill buffer?
         */
        if (pZip->u.LZF.cbSpill > 0)
        {
            unsigned cb = (unsigned)RT_MIN(pZip->u.LZF.cbSpill, cbBuf);
            memcpy(pvBuf, pZip->u.LZF.pbSpill, cb);
            pZip->u.LZF.pbSpill += cb;
            pZip->u.LZF.cbSpill -= cb;
            cbBuf -= cb;
            if (pcbWritten)
                *pcbWritten = cb;
            if (!cbBuf)
                break;
            pvBuf = (uint8_t *)pvBuf + cb;
        }

        /*
         * Incomplete header or nothing at all.
         */
        PCRTZIPLZFHDR pHdr;
        if (pZip->u.LZF.cbInput < sizeof(RTZIPLZFHDR))
        {
            if (pZip->u.LZF.cbInput <= 0)
            {
                /* empty, fill the buffer. */
                size_t cb = 0;
                int rc = pZip->pfnIn(pZip->pvUser, &pZip->abBuffer[0],
                                     sizeof(pZip->abBuffer) - RTZIPLZF_MAX_DATA_SIZE, &cb);
                if (RT_FAILURE(rc))
                    return rc;
                pZip->u.LZF.pbInput = &pZip->abBuffer[0];
                pZip->u.LZF.cbInput = cb;
                pHdr = (PCRTZIPLZFHDR)pZip->u.LZF.pbInput;
            }
            else
            {
                /* move the header up and fill the buffer. */
                size_t cbCur = pZip->u.LZF.cbInput;
                memmove(&pZip->abBuffer[0], pZip->u.LZF.pbInput, cbCur);
                pZip->u.LZF.pbInput = &pZip->abBuffer[0];

                size_t cb = 0;
                int rc = pZip->pfnIn(pZip->pvUser, &pZip->abBuffer[cbCur],
                                     sizeof(pZip->abBuffer) - RTZIPLZF_MAX_DATA_SIZE - cbCur, &cb);
                if (RT_FAILURE(rc))
                    return rc;
                pHdr = (PCRTZIPLZFHDR)pZip->u.LZF.pbInput;
                pZip->u.LZF.cbInput += cb;
            }

            /*
             * Validate the header.
             */
            if (!rtZipLZFValidHeader(pHdr))
                return VERR_GENERAL_FAILURE; /** @todo Get better error codes for RTZip! */
        }
        else
        {
            /*
             * Validate the header and check if it's an incomplete block.
             */
            pHdr = (PCRTZIPLZFHDR)pZip->u.LZF.pbInput;
            if (!rtZipLZFValidHeader(pHdr))
                return VERR_GENERAL_FAILURE; /** @todo Get better error codes for RTZip! */

            if (pHdr->cbData > pZip->u.LZF.cbInput - sizeof(*pHdr))
            {
                /* read the remainder of the block. */
                size_t cbToRead = pHdr->cbData - (pZip->u.LZF.cbInput - sizeof(*pHdr));
                Assert(&pZip->u.LZF.pbInput[pZip->u.LZF.cbInput + cbToRead] <= &pZip->u.LZF.pbInput[sizeof(pZip->abBuffer)]);
                int rc = pZip->pfnIn(pZip->pvUser, &pZip->u.LZF.pbInput[pZip->u.LZF.cbInput],
                                     cbToRead, NULL);
                if (RT_FAILURE(rc))
                    return rc;
                pZip->u.LZF.cbInput += cbToRead;
            }
        }
        AssertMsgReturn(sizeof(*pHdr) + pHdr->cbData <= pZip->u.LZF.cbInput,
                        ("cbData=%#x cbInput=%#x\n", pHdr->cbData, pZip->u.LZF.cbInput),
                        VERR_GENERAL_FAILURE); /** @todo Get better error codes for RTZip! */

        /*
         * Does the uncompressed data fit into the supplied buffer?
         * If so we uncompress it directly into the user buffer, else we'll have to use the spill buffer.
         */
        unsigned cbUncompressed = pHdr->cbUncompressed;
        if (cbUncompressed <= cbBuf)
        {
            unsigned cbOutput = lzf_decompress(pHdr + 1, pHdr->cbData, pvBuf, cbUncompressed);
            if (cbOutput != cbUncompressed)
            {
                AssertMsgFailed(("Decompression error, errno=%d. cbOutput=%#x cbUncompressed=%#x\n",
                                 errno, cbOutput, cbUncompressed));
                return VERR_GENERAL_FAILURE; /** @todo Get better error codes for RTZip! */
            }
            cbBuf -= cbUncompressed;
            pvBuf = (uint8_t *)pvBuf + cbUncompressed;
        }
        else
        {
            unsigned cbOutput = lzf_decompress(pHdr + 1, pHdr->cbData, pZip->u.LZF.abSpill, cbUncompressed);
            if (cbOutput != cbUncompressed)
            {
                AssertMsgFailed(("Decompression error, errno=%d. cbOutput=%#x cbUncompressed=%#x\n",
                                 errno, cbOutput, cbUncompressed));
                return VERR_GENERAL_FAILURE; /** @todo Get better error codes for RTZip! */
            }
            pZip->u.LZF.pbSpill = &pZip->u.LZF.abSpill[0];
            pZip->u.LZF.cbSpill = cbUncompressed;
        }

        /* advance the input buffer */
        pZip->u.LZF.cbInput -= pHdr->cbData + sizeof(*pHdr);
        pZip->u.LZF.pbInput += pHdr->cbData + sizeof(*pHdr);
        if (pcbWritten)
            *pcbWritten += cbUncompressed;
    }
# endif /* !RTZIP_LZF_BLOCK_BY_BLOCK */
    return VINF_SUCCESS;
}


/**
 * @copydoc RTZipDecompDestroy
 */
static DECLCALLBACK(int) rtZipLZFDecompDestroy(PRTZIPDECOMP pZip)
{
    NOREF(pZip);
    return VINF_SUCCESS;
}


/**
 * Initialize the decompressor instance.
 * @returns iprt status code.
 * @param   pZip        The decompressor instance.
 */
static DECLCALLBACK(int) rtZipLZFDecompInit(PRTZIPDECOMP pZip)
{
    pZip->pfnDecompress = rtZipLZFDecompress;
    pZip->pfnDestroy = rtZipLZFDecompDestroy;

# ifndef RTZIP_LZF_BLOCK_BY_BLOCK
    pZip->u.LZF.pbInput    = NULL;
    pZip->u.LZF.cbInput    = 0;
# endif
    pZip->u.LZF.cbSpill    = 0;
    pZip->u.LZF.pbSpill    = NULL;

    return VINF_SUCCESS;
}

#endif /* RTZIP_USE_LZF */


/**
 * Create a compressor instance.
 *
 * @returns iprt status code.
 * @param   ppZip       Where to store the instance handle.
 * @param   pvUser      User argument which will be passed on to pfnOut and pfnIn.
 * @param   pfnOut      Callback for consuming output of compression.
 * @param   enmType     Type of compressor to create.
 * @param   enmLevel    Compression level.
 */
RTDECL(int)     RTZipCompCreate(PRTZIPCOMP *ppZip, void *pvUser, PFNRTZIPOUT pfnOut, RTZIPTYPE enmType, RTZIPLEVEL enmLevel)
{
    /*
     * Validate input.
     */
    AssertReturn(enmType >= RTZIPTYPE_INVALID && enmType < RTZIPTYPE_END, VERR_INVALID_PARAMETER);
    AssertReturn(enmLevel >= RTZIPLEVEL_STORE && enmLevel <= RTZIPLEVEL_MAX, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfnOut, VERR_INVALID_POINTER);
    AssertPtrReturn(ppZip, VERR_INVALID_POINTER);

    /*
     * Allocate memory for the instance data.
     */
    PRTZIPCOMP pZip = (PRTZIPCOMP)RTMemAlloc(sizeof(RTZIPCOMP));
    if (!pZip)
        return VERR_NO_MEMORY;

    /*
     * Determine auto type.
     */
    if (enmType == RTZIPTYPE_AUTO)
    {
        if (enmLevel == RTZIPLEVEL_STORE)
            enmType = RTZIPTYPE_STORE;
        else
        {
#if defined(RTZIP_USE_ZLIB) && defined(RTZIP_USE_BZLIB)
            if (enmLevel == RTZIPLEVEL_MAX)
                enmType = RTZIPTYPE_BZLIB;
            else
                enmType = RTZIPTYPE_ZLIB;
#elif defined(RTZIP_USE_ZLIB)
            enmType = RTZIPTYPE_ZLIB;
#elif defined(RTZIP_USE_BZLIB)
            enmType = RTZIPTYPE_BZLIB;
#else
            enmType = RTZIPTYPE_STORE;
#endif
        }
    }

    /*
     * Init instance.
     */
    pZip->pfnOut  = pfnOut;
    pZip->enmType = enmType;
    pZip->pvUser  = pvUser;
    pZip->abBuffer[0] = enmType;       /* first byte is the compression type. */
    int rc = VERR_NOT_IMPLEMENTED;
    switch (enmType)
    {
        case RTZIPTYPE_STORE:
#ifdef RTZIP_USE_STORE
            rc = rtZipStoreCompInit(pZip, enmLevel);
#endif
            break;

        case RTZIPTYPE_ZLIB:
        case RTZIPTYPE_ZLIB_NO_HEADER:
#ifdef RTZIP_USE_ZLIB
            rc = rtZipZlibCompInit(pZip, enmLevel, enmType == RTZIPTYPE_ZLIB /*fZlibHeader*/);
#endif
            break;

        case RTZIPTYPE_BZLIB:
#ifdef RTZIP_USE_BZLIB
            rc = rtZipBZlibCompInit(pZip, enmLevel);
#endif
            break;

        case RTZIPTYPE_LZF:
#ifdef RTZIP_USE_LZF
            rc = rtZipLZFCompInit(pZip, enmLevel);
#endif
            break;

        case RTZIPTYPE_LZJB:
        case RTZIPTYPE_LZO:
            break;

        default:
            AssertFailedBreak();
    }

    if (RT_SUCCESS(rc))
        *ppZip = pZip;
    else
        RTMemFree(pZip);
    return rc;
}
RT_EXPORT_SYMBOL(RTZipCompCreate);


/**
 * Compresses a chunk of memory.
 *
 * @returns iprt status code.
 * @param   pZip        The compressor instance.
 * @param   pvBuf       Pointer to buffer containing the bits to compress.
 * @param   cbBuf       Number of bytes to compress.
 */
RTDECL(int)     RTZipCompress(PRTZIPCOMP pZip, const void *pvBuf, size_t cbBuf)
{
    if (!cbBuf)
        return VINF_SUCCESS;
    return pZip->pfnCompress(pZip, pvBuf, cbBuf);
}
RT_EXPORT_SYMBOL(RTZipCompress);


/**
 * Finishes the compression.
 * This will flush all data and terminate the compression data stream.
 *
 * @returns iprt status code.
 * @param   pZip        The compressor instance.
 */
RTDECL(int)     RTZipCompFinish(PRTZIPCOMP pZip)
{
    return pZip->pfnFinish(pZip);
}
RT_EXPORT_SYMBOL(RTZipCompFinish);


/**
 * Destroys the compressor instance.
 *
 * @returns iprt status code.
 * @param   pZip        The compressor instance.
 */
RTDECL(int)     RTZipCompDestroy(PRTZIPCOMP pZip)
{
    /*
     * Compressor specific destruction attempt first.
     */
    int rc = pZip->pfnDestroy(pZip);
    AssertRCReturn(rc, rc);

    /*
     * Free the instance memory.
     */
    pZip->enmType = RTZIPTYPE_INVALID;
    RTMemFree(pZip);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTZipCompDestroy);


/**
 * @copydoc RTZipDecompress
 */
static DECLCALLBACK(int) rtZipStubDecompress(PRTZIPDECOMP pZip, void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    NOREF(pZip); NOREF(pvBuf); NOREF(cbBuf); NOREF(pcbWritten);
    return VERR_NOT_SUPPORTED;
}


/**
 * @copydoc RTZipDecompDestroy
 */
static DECLCALLBACK(int) rtZipStubDecompDestroy(PRTZIPDECOMP pZip)
{
    NOREF(pZip);
    return VINF_SUCCESS;
}


/**
 * Create a decompressor instance.
 *
 * @returns iprt status code.
 * @param   ppZip       Where to store the instance handle.
 * @param   pvUser      User argument which will be passed on to pfnOut and pfnIn.
 * @param   pfnIn       Callback for producing input for decompression.
 */
RTDECL(int)     RTZipDecompCreate(PRTZIPDECOMP *ppZip, void *pvUser, PFNRTZIPIN pfnIn)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pfnIn, VERR_INVALID_POINTER);
    AssertPtrReturn(ppZip, VERR_INVALID_POINTER);

    /*
     * Allocate memory for the instance data.
     */
    PRTZIPDECOMP pZip = (PRTZIPDECOMP)RTMemAlloc(sizeof(RTZIPDECOMP));
    if (!pZip)
        return VERR_NO_MEMORY;

    /*
     * Init instance.
     */
    pZip->pfnIn   = pfnIn;
    pZip->enmType = RTZIPTYPE_INVALID;
    pZip->pvUser  = pvUser;
    pZip->pfnDecompress = NULL;
    pZip->pfnDestroy = rtZipStubDecompDestroy;

    *ppZip = pZip;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTZipDecompCreate);


/**
 * Lazy init of the decompressor.
 * @returns iprt status code.
 * @param   pZip        The decompressor instance.
 */
static int rtzipDecompInit(PRTZIPDECOMP pZip)
{
    /*
     * Read the first byte from the stream so we can determine the type.
     */
    uint8_t u8Type;
    int rc = pZip->pfnIn(pZip->pvUser, &u8Type, sizeof(u8Type), NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Determine type and do type specific init.
     */
    pZip->enmType = (RTZIPTYPE)u8Type;
    rc = VERR_NOT_SUPPORTED;
    switch (pZip->enmType)
    {
        case RTZIPTYPE_STORE:
#ifdef RTZIP_USE_STORE
            rc = rtZipStoreDecompInit(pZip);
#else
            AssertMsgFailed(("Store is not include in this build!\n"));
#endif
            break;

        case RTZIPTYPE_ZLIB:
        case RTZIPTYPE_ZLIB_NO_HEADER:
#ifdef RTZIP_USE_ZLIB
            rc = rtZipZlibDecompInit(pZip, pZip->enmType == RTZIPTYPE_ZLIB /*fHeader*/);
#else
            AssertMsgFailed(("Zlib is not include in this build!\n"));
#endif
            break;

        case RTZIPTYPE_BZLIB:
#ifdef RTZIP_USE_BZLIB
            rc = rtZipBZlibDecompInit(pZip);
#else
            AssertMsgFailed(("BZlib is not include in this build!\n"));
#endif
            break;

        case RTZIPTYPE_LZF:
#ifdef RTZIP_USE_LZF
            rc = rtZipLZFDecompInit(pZip);
#else
            AssertMsgFailed(("LZF is not include in this build!\n"));
#endif
            break;

        case RTZIPTYPE_LZJB:
#ifdef RTZIP_USE_LZJB
            AssertMsgFailed(("LZJB streaming support is not implemented yet!\n"));
#else
            AssertMsgFailed(("LZJB is not include in this build!\n"));
#endif
            break;

        case RTZIPTYPE_LZO:
#ifdef RTZIP_USE_LZJB
            AssertMsgFailed(("LZO streaming support is not implemented yet!\n"));
#else
            AssertMsgFailed(("LZO is not include in this build!\n"));
#endif
            break;

        default:
            AssertMsgFailed(("Invalid compression type %d (%#x)!\n", pZip->enmType, pZip->enmType));
            rc = VERR_INVALID_MAGIC;
            break;
    }
    if (RT_FAILURE(rc))
    {
        pZip->pfnDecompress = rtZipStubDecompress;
        pZip->pfnDestroy = rtZipStubDecompDestroy;
    }

    return rc;
}


/**
 * Decompresses a chunk of memory.
 *
 * @returns iprt status code.
 * @param   pZip        The decompressor instance.
 * @param   pvBuf       Where to store the decompressed data.
 * @param   cbBuf       Number of bytes to produce. If pcbWritten is set
 *                      any number of bytes up to cbBuf might be returned.
 * @param   pcbWritten  Number of bytes actually written to the buffer. If NULL
 *                      cbBuf number of bytes must be written.
 */
RTDECL(int)     RTZipDecompress(PRTZIPDECOMP pZip, void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    /*
     * Skip empty requests.
     */
    if (!cbBuf)
        return VINF_SUCCESS;

    /*
     * Lazy init.
     */
    if (!pZip->pfnDecompress)
    {
        int rc = rtzipDecompInit(pZip);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * 'Read' the decompressed stream.
     */
    return pZip->pfnDecompress(pZip, pvBuf, cbBuf, pcbWritten);
}
RT_EXPORT_SYMBOL(RTZipDecompress);


/**
 * Destroys the decompressor instance.
 *
 * @returns iprt status code.
 * @param   pZip        The decompressor instance.
 */
RTDECL(int)     RTZipDecompDestroy(PRTZIPDECOMP pZip)
{
    /*
     * Destroy compressor instance and flush the output buffer.
     */
    int rc = pZip->pfnDestroy(pZip);
    AssertRCReturn(rc, rc);

    /*
     * Free the instance memory.
     */
    pZip->enmType = RTZIPTYPE_INVALID;
    RTMemFree(pZip);
    return rc;
}
RT_EXPORT_SYMBOL(RTZipDecompDestroy);


RTDECL(int) RTZipBlockCompress(RTZIPTYPE enmType, RTZIPLEVEL enmLevel, uint32_t fFlags,
                               void const *pvSrc, size_t cbSrc,
                               void *pvDst, size_t cbDst, size_t *pcbDstActual) RT_NO_THROW_DEF
{
    /* input validation - the crash and burn approach as speed is essential here. */
    Assert(enmLevel <= RTZIPLEVEL_MAX && enmLevel >= RTZIPLEVEL_STORE); RT_NOREF_PV(enmLevel);
    Assert(!fFlags);                                                    RT_NOREF_PV(fFlags);

    /*
     * Deal with flags involving prefixes.
     */
    /** @todo later: type and/or compressed length prefix. */

    /*
     * The type specific part.
     */
    switch (enmType)
    {
        case RTZIPTYPE_LZF:
        {
#ifdef RTZIP_USE_LZF
# if 0
            static const uint8_t s_abZero4K[] =
            {
                0x01, 0x00, 0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff,
                0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xe0,
                0xff, 0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff, 0x00,
                0xe0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff,
                0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xe0,
                0xff, 0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff, 0x00,
                0xe0, 0x7d, 0x00
            };
            if (    cbSrc == _4K
                &&  !((uintptr_t)pvSrc & 15)
                &&  ASMMemIsZero(pvSrc, _4K))
            {
                if (RT_UNLIKELY(cbDst < sizeof(s_abZero4K)))
                    return VERR_BUFFER_OVERFLOW;
                memcpy(pvDst, s_abZero4K, sizeof(s_abZero4K));
                *pcbDstActual = sizeof(s_abZero4K);
                break;
            }
# endif

            unsigned cbDstActual = lzf_compress(pvSrc, (unsigned)cbSrc, pvDst, (unsigned)cbDst);    /** @todo deal with size type overflows */
            if (RT_UNLIKELY(cbDstActual < 1))
                return VERR_BUFFER_OVERFLOW;
            *pcbDstActual = cbDstActual;
            break;
#else
            return VERR_NOT_SUPPORTED;
#endif
        }

        case RTZIPTYPE_STORE:
        {
            if (cbDst < cbSrc)
                return VERR_BUFFER_OVERFLOW;
            memcpy(pvDst, pvSrc, cbSrc);
            *pcbDstActual = cbSrc;
            break;
        }

        case RTZIPTYPE_LZJB:
        {
#ifdef RTZIP_USE_LZJB
            AssertReturn(cbDst > cbSrc, VERR_BUFFER_OVERFLOW);
            size_t cbDstActual = lzjb_compress((void *)pvSrc, (uint8_t *)pvDst + 1, cbSrc, cbSrc, 0 /*??*/);
            if (cbDstActual == cbSrc)
                *(uint8_t *)pvDst = 0;
            else
                *(uint8_t *)pvDst = 1;
            *pcbDstActual = cbDstActual + 1;
            break;
#else
            return VERR_NOT_SUPPORTED;
#endif
        }

        case RTZIPTYPE_LZO:
        {
#ifdef RTZIP_USE_LZO
            uint64_t Scratch[RT_ALIGN(LZO1X_1_MEM_COMPRESS, sizeof(uint64_t)) / sizeof(uint64_t)];
            int rc = lzo_init();
            if (RT_UNLIKELY(rc != LZO_E_OK))
                return VERR_INTERNAL_ERROR;

            lzo_uint cbDstInOut = cbDst;
            rc = lzo1x_1_compress((const lzo_bytep)pvSrc, cbSrc, (lzo_bytep )pvDst, &cbDstInOut, &Scratch[0]);
            if (RT_UNLIKELY(rc != LZO_E_OK))
                switch (rc)
                {
                    case LZO_E_OUTPUT_OVERRUN:  return VERR_BUFFER_OVERFLOW;
                    default:                    return VERR_GENERAL_FAILURE;
                }
            *pcbDstActual = cbDstInOut;
            break;
#else
            return VERR_NOT_SUPPORTED;
#endif
        }

        case RTZIPTYPE_ZLIB:
        case RTZIPTYPE_BZLIB:
            return VERR_NOT_SUPPORTED;

        default:
            AssertMsgFailed(("%d\n", enmType));
            return VERR_INVALID_PARAMETER;
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTZipBlockCompress);


RTDECL(int) RTZipBlockDecompress(RTZIPTYPE enmType, uint32_t fFlags,
                                 void const *pvSrc, size_t cbSrc, size_t *pcbSrcActual,
                                 void *pvDst, size_t cbDst, size_t *pcbDstActual) RT_NO_THROW_DEF
{
    /* input validation - the crash and burn approach as speed is essential here. */
    Assert(!fFlags); RT_NOREF_PV(fFlags);

    /*
     * Deal with flags involving prefixes.
     */
    /** @todo later: type and/or compressed length prefix. */

    /*
     * The type specific part.
     */
    switch (enmType)
    {
        case RTZIPTYPE_LZF:
        {
#ifdef RTZIP_USE_LZF
            unsigned cbDstActual = lzf_decompress(pvSrc, (unsigned)cbSrc, pvDst, (unsigned)cbDst);  /** @todo deal with size type overflows */
            if (RT_UNLIKELY(cbDstActual < 1))
            {
# ifndef IPRT_NO_CRT /* no errno */
                if (errno == E2BIG)
                    return VERR_BUFFER_OVERFLOW;
                Assert(errno == EINVAL);
# endif
                return VERR_GENERAL_FAILURE;
            }
            if (pcbDstActual)
                *pcbDstActual = cbDstActual;
            if (pcbSrcActual)
                *pcbSrcActual = cbSrc;
            break;
#else
            return VERR_NOT_SUPPORTED;
#endif
        }

        case RTZIPTYPE_STORE:
        {
            if (cbDst < cbSrc)
                return VERR_BUFFER_OVERFLOW;
            memcpy(pvDst, pvSrc, cbSrc);
            if (pcbDstActual)
                *pcbDstActual = cbSrc;
            if (pcbSrcActual)
                *pcbSrcActual = cbSrc;
            break;
        }

        case RTZIPTYPE_LZJB:
        {
#ifdef RTZIP_USE_LZJB
            if (*(uint8_t *)pvSrc == 1)
            {
                int rc = lzjb_decompress((uint8_t *)pvSrc + 1, pvDst, cbSrc - 1, cbDst, 0 /*??*/);
                if (RT_UNLIKELY(rc != 0))
                    return VERR_GENERAL_FAILURE;
                if (pcbDstActual)
                    *pcbDstActual = cbDst;
            }
            else
            {
                AssertReturn(cbDst >= cbSrc - 1, VERR_BUFFER_OVERFLOW);
                memcpy(pvDst, (uint8_t *)pvSrc + 1, cbSrc - 1);
                if (pcbDstActual)
                    *pcbDstActual = cbSrc - 1;
            }
            if (pcbSrcActual)
                *pcbSrcActual = cbSrc;
            break;
#else
            return VERR_NOT_SUPPORTED;
#endif
        }

        case RTZIPTYPE_LZO:
        {
#ifdef RTZIP_USE_LZO
            int rc = lzo_init();
            if (RT_UNLIKELY(rc != LZO_E_OK))
                return VERR_INTERNAL_ERROR;
            lzo_uint cbDstInOut = cbDst;
            rc = lzo1x_decompress((const lzo_bytep)pvSrc, cbSrc, (lzo_bytep)pvDst, &cbDstInOut, NULL);
            if (RT_UNLIKELY(rc != LZO_E_OK))
                switch (rc)
                {
                    case LZO_E_OUTPUT_OVERRUN:  return VERR_BUFFER_OVERFLOW;
                    default:
                    case LZO_E_INPUT_OVERRUN:   return VERR_GENERAL_FAILURE;
                }
            if (pcbSrcActual)
                *pcbSrcActual = cbSrc;
            if (pcbDstActual)
                *pcbDstActual = cbDstInOut;
            break;
#else
            return VERR_NOT_SUPPORTED;
#endif
        }

        case RTZIPTYPE_ZLIB:
        case RTZIPTYPE_ZLIB_NO_HEADER:
        {
#ifdef RTZIP_USE_ZLIB
            AssertReturn(cbSrc == (uInt)cbSrc, VERR_TOO_MUCH_DATA);
            AssertReturn(cbDst == (uInt)cbDst, VERR_OUT_OF_RANGE);

            z_stream ZStrm;
            RT_ZERO(ZStrm);
            ZStrm.next_in   = (Bytef *)pvSrc;
            ZStrm.avail_in  = (uInt)cbSrc;
            ZStrm.next_out  = (Bytef *)pvDst;
            ZStrm.avail_out = (uInt)cbDst;

            int rc;
            if (enmType == RTZIPTYPE_ZLIB)
                rc = inflateInit(&ZStrm);
            else if (enmType == RTZIPTYPE_ZLIB_NO_HEADER)
                rc = inflateInit2(&ZStrm, -Z_DEF_WBITS);
            else
                AssertFailedReturn(VERR_INTERNAL_ERROR);

            if (RT_UNLIKELY(rc != Z_OK))
                return zipErrConvertFromZlib(rc, false /*fCompressing*/);
            rc = inflate(&ZStrm, Z_FINISH);
            if (rc != Z_STREAM_END)
            {
                inflateEnd(&ZStrm);
                if ((rc == Z_BUF_ERROR && ZStrm.avail_in == 0) || rc == Z_NEED_DICT)
                    return VERR_ZIP_CORRUPTED;
                if (rc == Z_BUF_ERROR)
                    return VERR_BUFFER_OVERFLOW;
                AssertReturn(rc < Z_OK, VERR_GENERAL_FAILURE);
                return zipErrConvertFromZlib(rc, false /*fCompressing*/);
            }
            rc = inflateEnd(&ZStrm);
            if (rc != Z_OK)
                return zipErrConvertFromZlib(rc, false /*fCompressing*/);

            if (pcbSrcActual)
                *pcbSrcActual = cbSrc - ZStrm.avail_in;
            if (pcbDstActual)
                *pcbDstActual = ZStrm.total_out;
            break;
#else
            return VERR_NOT_SUPPORTED;
#endif
        }

        case RTZIPTYPE_BZLIB:
            return VERR_NOT_SUPPORTED;

        default:
            AssertMsgFailed(("%d\n", enmType));
            return VERR_INVALID_PARAMETER;
    }
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTZipBlockDecompress);

