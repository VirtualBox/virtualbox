/* $Id: tstCompressionBenchmark.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Compression Benchmark for SSM and PGM.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/asm-mem.h>
#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/crc.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/md5.h>
#include <iprt/sha.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/zip.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define MY_BLOCK_SIZE       _4K     /**< Same as SSM uses. */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static size_t   g_cBlocks = 20*_1M / MY_BLOCK_SIZE;
static size_t   g_cbBlocks;
static uint8_t *g_pabSrc;

/** Buffer for the decompressed data (g_cbBlocks). */
static uint8_t *g_pabDecompr;

/** Buffer for the compressed data (g_cbComprAlloc). */
static uint8_t *g_pabCompr;
/** The current size of the compressed data, ComprOutCallback */
static size_t   g_cbCompr;
/** The current offset into the compressed data, DecomprInCallback. */
static size_t   g_offComprIn;
/** The amount of space allocated for compressed data. */
static size_t   g_cbComprAlloc;


/**
 * Store compressed data in the g_pabCompr buffer.
 */
static DECLCALLBACK(int) ComprOutCallback(void *pvUser, const void *pvBuf, size_t cbBuf)
{
    NOREF(pvUser);
    AssertReturn(g_cbCompr + cbBuf <= g_cbComprAlloc, VERR_BUFFER_OVERFLOW);
    memcpy(&g_pabCompr[g_cbCompr], pvBuf, cbBuf);
    g_cbCompr += cbBuf;
    return VINF_SUCCESS;
}

/**
 * Read compressed data from g_pabComrp.
 */
static DECLCALLBACK(int) DecomprInCallback(void *pvUser, void *pvBuf, size_t cbBuf, size_t *pcbBuf)
{
    NOREF(pvUser);
    size_t cb = RT_MIN(cbBuf, g_cbCompr - g_offComprIn);
    if (pcbBuf)
        *pcbBuf = cb;
//    AssertReturn(cb > 0, VERR_EOF);
    memcpy(pvBuf, &g_pabCompr[g_offComprIn], cb);
    g_offComprIn += cb;
    return VINF_SUCCESS;
}


/**
 * Benchmark RTCrc routines potentially relevant for SSM or PGM - All in one go.
 *
 * @param  pabSrc   Pointer to the test data.
 * @param  cbSrc    The size of the test data.
 */
static void tstBenchmarkCRCsAllInOne(uint8_t const *pabSrc, size_t cbSrc)
{
    RTPrintf("Algorithm     Speed                  Time      Digest\n"
             "------------------------------------------------------------------------------\n");

    uint64_t NanoTS = RTTimeNanoTS();
    uint32_t u32Crc = RTCrc32(pabSrc, cbSrc);
    NanoTS = RTTimeNanoTS() - NanoTS;
    unsigned uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTPrintf("CRC-32    %'9u KB/s  %'15llu ns - %08x\n", uSpeed, NanoTS, u32Crc);


    NanoTS = RTTimeNanoTS();
    uint64_t u64Crc = RTCrc64(pabSrc, cbSrc);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTPrintf("CRC-64    %'9u KB/s  %'15llu ns - %016llx\n", uSpeed, NanoTS, u64Crc);

    NanoTS = RTTimeNanoTS();
    u32Crc = RTCrcAdler32(pabSrc, cbSrc);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTPrintf("Adler-32  %'9u KB/s  %'15llu ns - %08x\n", uSpeed, NanoTS, u32Crc);

    NanoTS = RTTimeNanoTS();
    uint8_t abMd5Hash[RTMD5HASHSIZE];
    RTMd5(pabSrc, cbSrc, abMd5Hash);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    char szDigest[257];
    RTMd5ToString(abMd5Hash, szDigest, sizeof(szDigest));
    RTPrintf("MD5       %'9u KB/s  %'15llu ns - %s\n", uSpeed, NanoTS, szDigest);

    NanoTS = RTTimeNanoTS();
    uint8_t abSha1Hash[RTSHA1_HASH_SIZE];
    RTSha1(pabSrc, cbSrc, abSha1Hash);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTSha1ToString(abSha1Hash, szDigest, sizeof(szDigest));
    RTPrintf("SHA-1     %'9u KB/s  %'15llu ns - %s\n", uSpeed, NanoTS, szDigest);

    NanoTS = RTTimeNanoTS();
    uint8_t abSha256Hash[RTSHA256_HASH_SIZE];
    RTSha256(pabSrc, cbSrc, abSha256Hash);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTSha256ToString(abSha256Hash, szDigest, sizeof(szDigest));
    RTPrintf("SHA-256   %'9u KB/s  %'15llu ns - %s\n", uSpeed, NanoTS, szDigest);

    NanoTS = RTTimeNanoTS();
    uint8_t abSha512Hash[RTSHA512_HASH_SIZE];
    RTSha512(pabSrc, cbSrc, abSha512Hash);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTSha512ToString(abSha512Hash, szDigest, sizeof(szDigest));
    RTPrintf("SHA-512   %'9u KB/s  %'15llu ns - %s\n", uSpeed, NanoTS, szDigest);
}


/**
 * Benchmark RTCrc routines potentially relevant for SSM or PGM - Bage by block.
 *
 * @param  pabSrc   Pointer to the test data.
 * @param  cbSrc    The size of the test data.
 */
static void tstBenchmarkCRCsBlockByBlock(uint8_t const *pabSrc, size_t cbSrc)
{
    RTPrintf("Algorithm     Speed                  Time     \n"
             "----------------------------------------------\n");

    size_t const cBlocks = cbSrc / MY_BLOCK_SIZE;

    uint64_t NanoTS = RTTimeNanoTS();
    for (uint32_t iBlock = 0; iBlock < cBlocks; iBlock++)
        RTCrc32(&pabSrc[iBlock * MY_BLOCK_SIZE], MY_BLOCK_SIZE);
    NanoTS = RTTimeNanoTS() - NanoTS;
    unsigned uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTPrintf("CRC-32    %'9u KB/s  %'15llu ns\n", uSpeed, NanoTS);


    NanoTS = RTTimeNanoTS();
    for (uint32_t iBlock = 0; iBlock < cBlocks; iBlock++)
        RTCrc64(&pabSrc[iBlock * MY_BLOCK_SIZE], MY_BLOCK_SIZE);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTPrintf("CRC-64    %'9u KB/s  %'15llu ns\n", uSpeed, NanoTS);

    NanoTS = RTTimeNanoTS();
    for (uint32_t iBlock = 0; iBlock < cBlocks; iBlock++)
        RTCrcAdler32(&pabSrc[iBlock * MY_BLOCK_SIZE], MY_BLOCK_SIZE);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTPrintf("Adler-32  %'9u KB/s  %'15llu ns\n", uSpeed, NanoTS);

    NanoTS = RTTimeNanoTS();
    uint8_t abMd5Hash[RTMD5HASHSIZE];
    for (uint32_t iBlock = 0; iBlock < cBlocks; iBlock++)
        RTMd5(&pabSrc[iBlock * MY_BLOCK_SIZE], MY_BLOCK_SIZE, abMd5Hash);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTPrintf("MD5       %'9u KB/s  %'15llu ns\n", uSpeed, NanoTS);

    NanoTS = RTTimeNanoTS();
    uint8_t abSha1Hash[RTSHA1_HASH_SIZE];
    for (uint32_t iBlock = 0; iBlock < cBlocks; iBlock++)
        RTSha1(&pabSrc[iBlock * MY_BLOCK_SIZE], MY_BLOCK_SIZE, abSha1Hash);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTPrintf("SHA-1     %'9u KB/s  %'15llu ns\n", uSpeed, NanoTS);

    NanoTS = RTTimeNanoTS();
    uint8_t abSha256Hash[RTSHA256_HASH_SIZE];
    for (uint32_t iBlock = 0; iBlock < cBlocks; iBlock++)
        RTSha256(&pabSrc[iBlock * MY_BLOCK_SIZE], MY_BLOCK_SIZE, abSha256Hash);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTPrintf("SHA-256   %'9u KB/s  %'15llu ns\n", uSpeed, NanoTS);

    NanoTS = RTTimeNanoTS();
    uint8_t abSha512Hash[RTSHA512_HASH_SIZE];
    for (uint32_t iBlock = 0; iBlock < cBlocks; iBlock++)
        RTSha512(&pabSrc[iBlock * MY_BLOCK_SIZE], MY_BLOCK_SIZE, abSha512Hash);
    NanoTS = RTTimeNanoTS() - NanoTS;
    uSpeed = (unsigned)((long double)cbSrc / (long double)NanoTS * 1000000000.0 / 1024);
    RTPrintf("SHA-512   %'9u KB/s  %'15llu ns\n", uSpeed, NanoTS);
}


/** Prints an error message and returns 1 for quick return from main use. */
static int Error(const char *pszMsgFmt, ...)
{
    RTStrmPrintf(g_pStdErr, "\nerror: ");
    va_list va;
    va_start(va, pszMsgFmt);
    RTStrmPrintfV(g_pStdErr, pszMsgFmt, va);
    va_end(va);
    return 1;
}


int main(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, 0);

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF    s_aOptions[] =
    {
        { "--iterations",      'i', RTGETOPT_REQ_UINT32 },
        { "--num-blocks",      'n', RTGETOPT_REQ_UINT32 },
        { "--block-at-a-time", 'c', RTGETOPT_REQ_UINT32 },
        { "--block-file",      'f', RTGETOPT_REQ_STRING },
        { "--offset",          'o', RTGETOPT_REQ_UINT64 },
    };

    const char     *pszBlockFile = NULL;
    uint64_t        offBlockFile = 0;
    uint32_t        cIterations = 1;
    uint32_t        cBlocksAtATime = 1;
    RTGETOPTUNION   Val;
    RTGETOPTSTATE   State;
    int rc = RTGetOptInit(&State, argc, argv, &s_aOptions[0], RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRCReturn(rc, 1);

    while ((rc = RTGetOpt(&State, &Val)))
    {
        switch (rc)
        {
            case 'n':
                g_cBlocks = Val.u32;
                if (g_cBlocks * MY_BLOCK_SIZE * 4 / (MY_BLOCK_SIZE * 4) != g_cBlocks)
                    return Error("The specified block count is too high: %#x (%#llx bytes)\n",
                                 g_cBlocks, (uint64_t)g_cBlocks * MY_BLOCK_SIZE);
                if (g_cBlocks < 1)
                    return Error("The specified block count is too low: %#x\n", g_cBlocks);
                break;

            case 'i':
                cIterations = Val.u32;
                if (cIterations < 1)
                    return Error("The number of iterations must be 1 or higher\n");
                break;

            case 'c':
                cBlocksAtATime = Val.u32;
                if (cBlocksAtATime < 1 || cBlocksAtATime > 10240)
                    return Error("The specified blocks-at-a-time count is out of range: %#x\n", cBlocksAtATime);
                break;

            case 'f':
                pszBlockFile = Val.psz;
                break;

            case 'o':
                offBlockFile = Val.u64;
                break;

            case 'O':
                offBlockFile = Val.u64 * MY_BLOCK_SIZE;
                break;

            case 'h':
                RTPrintf("syntax: tstCompressionBenchmark [options]\n"
                         "\n"
                         "Options:\n"
                         "  -h, --help\n"
                         "    Show this help page\n"
                         "  -i, --iterations <num>\n"
                         "    The number of iterations.\n"
                         "  -n, --num-blocks <blocks>\n"
                         "    The number of blocks.\n"
                         "  -c, --blocks-at-a-time <blocks>\n"
                         "    Number of blocks at a time.\n"
                         "  -f, --block-file <filename>\n"
                         "    File or device to read the block from. The default\n"
                         "    is to generate some garbage.\n"
                         "  -o, --offset <file-offset>\n"
                         "    Offset into the block file to start reading at.\n");
                return 0;

            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return 0;

            default:
                return RTGetOptPrintError(rc, &Val);
        }
    }

    g_cbBlocks = g_cBlocks * MY_BLOCK_SIZE;
    uint64_t cbTotal = (uint64_t)g_cBlocks * MY_BLOCK_SIZE * cIterations;
    uint64_t cbTotalKB = cbTotal / _1K;
    if (cbTotal / cIterations != g_cbBlocks)
        return Error("cBlocks * cIterations -> overflow\n");

    /*
     * Gather the test memory.
     */
    if (pszBlockFile)
    {
        size_t cbFile;
        rc = RTFileReadAllEx(pszBlockFile, offBlockFile, g_cbBlocks, RTFILE_RDALL_O_DENY_NONE, (void **)&g_pabSrc, &cbFile);
        if (RT_FAILURE(rc))
            return Error("Error reading %zu bytes from %s at %llu: %Rrc\n", g_cbBlocks, pszBlockFile, offBlockFile, rc);
        if (cbFile != g_cbBlocks)
            return Error("Error reading %zu bytes from %s at %llu: got %zu bytes\n", g_cbBlocks, pszBlockFile, offBlockFile, cbFile);
    }
    else
    {
        g_pabSrc = (uint8_t *)RTMemAlloc(g_cbBlocks);
        if (g_pabSrc)
        {
            /* Just fill it with something - warn about the low quality of the something. */
            RTPrintf("tstCompressionBenchmark: WARNING! No input file was specified so the source\n"
                     "buffer will be filled with generated data of questionable quality.\n");
#ifdef RT_OS_LINUX
            RTPrintf("To get real RAM on linux: sudo dd if=/dev/mem ... \n");
#endif
            uint8_t *pb    = g_pabSrc;
            uint8_t *pbEnd = &g_pabSrc[g_cbBlocks];
            for (; pb != pbEnd; pb += 16)
            {
                char szTmp[17];
                RTStrPrintf(szTmp, sizeof(szTmp), "aaaa%08Xzzzz", (uint32_t)(uintptr_t)pb);
                memcpy(pb, szTmp, 16);
            }
        }
    }

    g_pabDecompr = (uint8_t *)RTMemAlloc(g_cbBlocks);
    g_cbComprAlloc = RT_MAX(g_cbBlocks * 2, 256 * MY_BLOCK_SIZE);
    g_pabCompr   = (uint8_t *)RTMemAlloc(g_cbComprAlloc);
    if (!g_pabSrc || !g_pabDecompr || !g_pabCompr)
        return Error("failed to allocate memory buffers (g_cBlocks=%#x)\n", g_cBlocks);

    /*
     * Double loop compressing and uncompressing the data, where the outer does
     * the specified number of iterations while the inner applies the different
     * compression algorithms.
     */
    struct
    {
        /** The time spent decompressing. */
        uint64_t    cNanoDecompr;
        /** The time spent compressing. */
        uint64_t    cNanoCompr;
        /** The size of the compressed data. */
        uint64_t    cbCompr;
        /** First error. */
        int         rc;
        /** The compression style: block or stream. */
        bool        fBlock;
        /** Compression type.  */
        RTZIPTYPE   enmType;
        /** Compression level.  */
        RTZIPLEVEL  enmLevel;
        /** Method name. */
        const char *pszName;
    } aTests[] =
    {
        { 0, 0, 0, VINF_SUCCESS, false, RTZIPTYPE_STORE, RTZIPLEVEL_DEFAULT, "RTZip/Store"      },
        { 0, 0, 0, VINF_SUCCESS, false, RTZIPTYPE_LZF,   RTZIPLEVEL_DEFAULT, "RTZip/LZF"        },
/*      { 0, 0, 0, VINF_SUCCESS, false, RTZIPTYPE_ZLIB,  RTZIPLEVEL_DEFAULT, "RTZip/zlib"       }, - slow plus it randomly hits VERR_GENERAL_FAILURE atm. */
        { 0, 0, 0, VINF_SUCCESS, true,  RTZIPTYPE_STORE, RTZIPLEVEL_DEFAULT, "RTZipBlock/Store" },
        { 0, 0, 0, VINF_SUCCESS, true,  RTZIPTYPE_LZF,   RTZIPLEVEL_DEFAULT, "RTZipBlock/LZF"   },
        { 0, 0, 0, VINF_SUCCESS, true,  RTZIPTYPE_LZJB,  RTZIPLEVEL_DEFAULT, "RTZipBlock/LZJB"  },
        { 0, 0, 0, VINF_SUCCESS, true,  RTZIPTYPE_LZO,   RTZIPLEVEL_DEFAULT, "RTZipBlock/LZO"   },
    };
    RTPrintf("tstCompressionBenchmark: TESTING..");
    for (uint32_t i = 0; i < cIterations; i++)
    {
        for (uint32_t j = 0; j < RT_ELEMENTS(aTests); j++)
        {
            if (RT_FAILURE(aTests[j].rc))
                continue;
            memset(g_pabCompr,   0xaa, g_cbComprAlloc);
            memset(g_pabDecompr, 0xcc, g_cbBlocks);
            g_cbCompr = 0;
            g_offComprIn = 0;
            RTPrintf("."); RTStrmFlush(g_pStdOut);

            /*
             * Compress it.
             */
            uint64_t NanoTS = RTTimeNanoTS();
            if (aTests[j].fBlock)
            {
                size_t          cbLeft     = g_cbComprAlloc;
                uint8_t const  *pbSrcBlock = g_pabSrc;
                uint8_t        *pbDstBlock = g_pabCompr;
                for (size_t iBlock = 0; iBlock < g_cBlocks; iBlock += cBlocksAtATime)
                {
                    AssertBreakStmt(cbLeft > MY_BLOCK_SIZE * 4, aTests[j].rc = rc = VERR_BUFFER_OVERFLOW);
                    uint32_t *pcb = (uint32_t *)pbDstBlock;
                    pbDstBlock   += sizeof(uint32_t);
                    cbLeft       -= sizeof(uint32_t);
                    size_t  cbSrc = RT_MIN(g_cBlocks - iBlock, cBlocksAtATime) * MY_BLOCK_SIZE;
                    size_t  cbDst;
                    rc = RTZipBlockCompress(aTests[j].enmType, aTests[j].enmLevel, 0 /*fFlags*/,
                                            pbSrcBlock, cbSrc,
                                            pbDstBlock, cbLeft, &cbDst);
                    if (RT_FAILURE(rc))
                    {
                        Error("RTZipBlockCompress failed for '%s' (#%u): %Rrc\n", aTests[j].pszName, j, rc);
                        aTests[j].rc = rc;
                        break;
                    }
                    *pcb        = (uint32_t)cbDst;
                    cbLeft     -= cbDst;
                    pbDstBlock += cbDst;
                    pbSrcBlock += cbSrc;
                }
                if (RT_FAILURE(rc))
                    continue;
                g_cbCompr = pbDstBlock - g_pabCompr;
            }
            else
            {
                PRTZIPCOMP pZipComp;
                rc = RTZipCompCreate(&pZipComp, NULL, ComprOutCallback, aTests[j].enmType, aTests[j].enmLevel);
                if (RT_FAILURE(rc))
                {
                    Error("Failed to create the compressor for '%s' (#%u): %Rrc\n", aTests[j].pszName, j, rc);
                    aTests[j].rc = rc;
                    continue;
                }

                uint8_t const  *pbSrcBlock = g_pabSrc;
                for (size_t iBlock = 0; iBlock < g_cBlocks; iBlock += cBlocksAtATime)
                {
                    size_t cb = RT_MIN(g_cBlocks - iBlock, cBlocksAtATime) * MY_BLOCK_SIZE;
                    rc = RTZipCompress(pZipComp, pbSrcBlock, cb);
                    if (RT_FAILURE(rc))
                    {
                        Error("RTZipCompress failed for '%s' (#%u): %Rrc\n", aTests[j].pszName, j, rc);
                        aTests[j].rc = rc;
                        break;
                    }
                    pbSrcBlock += cb;
                }
                if (RT_FAILURE(rc))
                    continue;
                rc = RTZipCompFinish(pZipComp);
                if (RT_FAILURE(rc))
                {
                    Error("RTZipCompFinish failed for '%s' (#%u): %Rrc\n", aTests[j].pszName, j, rc);
                    aTests[j].rc = rc;
                    break;
                }
                RTZipCompDestroy(pZipComp);
            }
            NanoTS = RTTimeNanoTS() - NanoTS;
            aTests[j].cbCompr    += g_cbCompr;
            aTests[j].cNanoCompr += NanoTS;

            /*
             * Decompress it.
             */
            NanoTS = RTTimeNanoTS();
            if (aTests[j].fBlock)
            {
                uint8_t const  *pbSrcBlock = g_pabCompr;
                size_t          cbLeft     = g_cbCompr;
                uint8_t        *pbDstBlock = g_pabDecompr;
                for (size_t iBlock = 0; iBlock < g_cBlocks; iBlock += cBlocksAtATime)
                {
                    size_t   cbDst = RT_MIN(g_cBlocks - iBlock, cBlocksAtATime) * MY_BLOCK_SIZE;
                    size_t   cbSrc = *(uint32_t *)pbSrcBlock;
                    pbSrcBlock    += sizeof(uint32_t);
                    cbLeft        -= sizeof(uint32_t);
                    rc = RTZipBlockDecompress(aTests[j].enmType, 0 /*fFlags*/,
                                              pbSrcBlock, cbSrc, &cbSrc,
                                              pbDstBlock, cbDst, &cbDst);
                    if (RT_FAILURE(rc))
                    {
                        Error("RTZipBlockDecompress failed for '%s' (#%u): %Rrc\n", aTests[j].pszName, j, rc);
                        aTests[j].rc = rc;
                        break;
                    }
                    pbDstBlock += cbDst;
                    cbLeft     -= cbSrc;
                    pbSrcBlock += cbSrc;
                }
                if (RT_FAILURE(rc))
                    continue;
            }
            else
            {
                PRTZIPDECOMP pZipDecomp;
                rc = RTZipDecompCreate(&pZipDecomp, NULL, DecomprInCallback);
                if (RT_FAILURE(rc))
                {
                    Error("Failed to create the decompressor for '%s' (#%u): %Rrc\n", aTests[j].pszName, j, rc);
                    aTests[j].rc = rc;
                    continue;
                }

                uint8_t *pbDstBlock = g_pabDecompr;
                for (size_t iBlock = 0; iBlock < g_cBlocks; iBlock += cBlocksAtATime)
                {
                    size_t cb = RT_MIN(g_cBlocks - iBlock, cBlocksAtATime) * MY_BLOCK_SIZE;
                    rc = RTZipDecompress(pZipDecomp, pbDstBlock, cb, NULL);
                    if (RT_FAILURE(rc))
                    {
                        Error("RTZipDecompress failed for '%s' (#%u): %Rrc\n", aTests[j].pszName, j, rc);
                        aTests[j].rc = rc;
                        break;
                    }
                    pbDstBlock += cb;
                }
                RTZipDecompDestroy(pZipDecomp);
                if (RT_FAILURE(rc))
                    continue;
            }
            NanoTS = RTTimeNanoTS() - NanoTS;
            aTests[j].cNanoDecompr += NanoTS;

            if (memcmp(g_pabDecompr, g_pabSrc, g_cbBlocks))
            {
                Error("The compressed data doesn't match the source for '%s' (%#u)\n", aTests[j].pszName, j);
                aTests[j].rc = VERR_BAD_EXE_FORMAT;
                continue;
            }
        }
    }
    if (RT_SUCCESS(rc))
        RTPrintf("\n");

    /*
     * Report the results.
     */
    rc = 0;
    RTPrintf("tstCompressionBenchmark: BEGIN RESULTS\n");
    RTPrintf("%-20s           Compression                                             Decompression\n", "");
    RTPrintf("%-20s        In             Out      Ratio         Size                In             Out\n", "Method");
    RTPrintf("%.20s-----------------------------------------------------------------------------------------\n", "---------------------------------------------");
    for (uint32_t j = 0; j < RT_ELEMENTS(aTests); j++)
    {
        if (RT_SUCCESS(aTests[j].rc))
        {
            unsigned uComprSpeedIn    = (unsigned)((long double)cbTotalKB         / (long double)aTests[j].cNanoCompr   * 1000000000.0);
            unsigned uComprSpeedOut   = (unsigned)((long double)aTests[j].cbCompr / (long double)aTests[j].cNanoCompr   * 1000000000.0 / 1024);
            unsigned uDecomprSpeedIn  = (unsigned)((long double)aTests[j].cbCompr / (long double)aTests[j].cNanoDecompr * 1000000000.0 / 1024);
            unsigned uDecomprSpeedOut = (unsigned)((long double)cbTotalKB         / (long double)aTests[j].cNanoDecompr * 1000000000.0);
            unsigned uRatio           = (unsigned)(aTests[j].cbCompr / cIterations * 100 / g_cbBlocks);
            RTPrintf("%-20s %'9u KB/s  %'9u KB/s  %3u%%  %'11llu bytes   %'9u KB/s  %'9u KB/s",
                     aTests[j].pszName,
                     uComprSpeedIn,   uComprSpeedOut, uRatio, aTests[j].cbCompr / cIterations,
                     uDecomprSpeedIn, uDecomprSpeedOut);
#if 0
            RTPrintf("  [%'14llu / %'14llu ns]\n",
                     aTests[j].cNanoCompr / cIterations,
                     aTests[j].cNanoDecompr / cIterations);
#else
            RTPrintf("\n");
#endif
        }
        else
        {
            RTPrintf("%-20s: %Rrc\n", aTests[j].pszName, aTests[j].rc);
            rc = 1;
        }
    }
    if (pszBlockFile)
        RTPrintf("Input: %'10zu Blocks from '%s' starting at offset %'lld (%#llx)\n"
                 "                                                           %'11zu bytes\n",
                 g_cBlocks, pszBlockFile, offBlockFile, offBlockFile, g_cbBlocks);
    else
        RTPrintf("Input: %'10zu Blocks of generated rubbish              %'11zu bytes\n",
                 g_cBlocks, g_cbBlocks);

    /*
     * Count zero blocks in the data set.
     */
    size_t cZeroBlocks = 0;
    for (size_t iBlock = 0; iBlock < g_cBlocks; iBlock++)
    {
        if (ASMMemIsZero(&g_pabSrc[iBlock * MY_BLOCK_SIZE], MY_BLOCK_SIZE))
            cZeroBlocks++;
    }
    RTPrintf("       %'10zu zero Blocks (%u %%)\n", cZeroBlocks, cZeroBlocks * 100 / g_cBlocks);

    /*
     * A little extension to the test, benchmark relevant CRCs.
     */
    RTPrintf("\n"
             "tstCompressionBenchmark: Hash/CRC - All In One\n");
    tstBenchmarkCRCsAllInOne(g_pabSrc, g_cbBlocks);

    RTPrintf("\n"
             "tstCompressionBenchmark: Hash/CRC - Block by Block\n");
    tstBenchmarkCRCsBlockByBlock(g_pabSrc, g_cbBlocks);

    RTPrintf("\n"
             "tstCompressionBenchmark: Hash/CRC - Zero Block Digest\n");
    static uint8_t s_abZeroPg[MY_BLOCK_SIZE];
    RT_ZERO(s_abZeroPg);
    tstBenchmarkCRCsAllInOne(s_abZeroPg, MY_BLOCK_SIZE);

    RTPrintf("\n"
             "tstCompressionBenchmark: Hash/CRC - Zero Half Block Digest\n");
    tstBenchmarkCRCsAllInOne(s_abZeroPg, MY_BLOCK_SIZE / 2);

    RTPrintf("tstCompressionBenchmark: END RESULTS\n");

    return rc;
}

