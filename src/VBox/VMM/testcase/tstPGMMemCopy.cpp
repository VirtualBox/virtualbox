/* $Id: tstPGMMemCopy.cpp 114110 2026-05-08 22:22:54Z knut.osmundsen@oracle.com $ */
/** @file
 * Testcase for the memcpy-replacements in PGM.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
#include "../include/PGMMemCopy.h"

#include <iprt/asm-mem.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/test.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST   g_hTest;


template <bool const a_fIsRead, size_t a_cbBuf, typename a_fnGuardedAllocType>
static void doMemCopyTest(a_fnGuardedAllocType *pfnGuardedAlloc)
{
    uint8_t * const pbDstBuf = (uint8_t *)pfnGuardedAlloc(g_hTest, a_cbBuf);
    RTTESTI_CHECK_RETV(pbDstBuf);
    uint8_t * const pbSrcBuf = (uint8_t *)pfnGuardedAlloc(g_hTest, a_cbBuf);
    RTTESTI_CHECK(pbSrcBuf);
    if (pbSrcBuf)
    {
        RTRandBytes(pbSrcBuf, a_cbBuf);
        uint8_t * const pbExpectBuf = (uint8_t *)RTMemAlloc(a_cbBuf);
        RTTESTI_CHECK(pbExpectBuf);
        if (pbExpectBuf)
        {
            for (size_t offSrc = 0; offSrc < a_cbBuf; offSrc++)
            {
                size_t const cbSrcMax = a_cbBuf - offSrc;
                for (size_t offDst = 0; offDst < a_cbBuf; offDst++)
                {
                    size_t const cbDstMax = a_cbBuf - offDst;
                    uint8_t const bFiller = 0xcc;
                    memset(&pbExpectBuf[offDst], bFiller, cbDstMax);

                    size_t const cbMax = RT_MIN(cbDstMax, cbSrcMax);
                    for (size_t cb = 1; cb < cbMax; cb++)
                    {
                        pbExpectBuf[offDst + cb - 1] = pbSrcBuf[offSrc + cb - 1];

                        memset(pbDstBuf, bFiller, a_cbBuf);
#if 0 /* for checking the testcase... */
                        memcpy(&pbDstBuf[offDst], &pbSrcBuf[offSrc], cb);
#else
                        pgmPhysMemCopyWrapper<a_fIsRead>(&pbDstBuf[offDst], &pbSrcBuf[offSrc], cb);
#endif
                        if (memcmp(pbDstBuf, pbExpectBuf, a_cbBuf) != 0)
                        {
                            RTTestFailed(g_hTest, "offSrc=%#zx offDst=%#zx cb=%#zx", offSrc, offDst, cb);
                            for (size_t offCmp = 0; offCmp < a_cbBuf; offCmp++)
                                if (pbDstBuf[offCmp] != pbExpectBuf[offCmp])
                                    RTTestPrintf(g_hTest, RTTESTLVL_INFO, " @%#zx - %#x, expected %#x\n",
                                                 offCmp, pbDstBuf[offCmp], pbExpectBuf[offCmp]);
                            if (RTTestSubErrorCount(g_hTest) > 32)
                                return;
                        }
                    }

                    pbExpectBuf[offDst] = bFiller;
                }
            }

            RTMemFree(pbExpectBuf);
        }
        RTTestGuardedFree(g_hTest, pbSrcBuf);
    }
    RTTestGuardedFree(g_hTest, pbDstBuf);
}


template <bool const a_fIsRead, bool const a_fUseMemCopy, typename a_fnTestType>
static void doMemCopyBenchSub(a_fnTestType *pfnTest, uint8_t *pbDst, uint8_t const *pbSrc, size_t cbBuf, const char *pszName)
{
    uint32_t const cSecBench = 2;

    /*
     * Estimate how many iterations we can do in cSecBench seconds.
     */
    uint64_t nsStart    = RTTimeNanoTS();
    uint64_t cNsElapsed = RT_NS_10MS;
    uint64_t i          = 0;
    while (i < _256K)
    {
        pfnTest(pbDst, pbSrc, cbBuf);
        ASMCompilerBarrier();
        pfnTest(pbDst, pbSrc, cbBuf);
        ASMCompilerBarrier();
        pfnTest(pbDst, pbSrc, cbBuf);
        ASMCompilerBarrier();
        pfnTest(pbDst, pbSrc, cbBuf);
        i++;
        if (i & 255)
        { /* likely */ }
        else
        {
            cNsElapsed = RTTimeNanoTS() - nsStart;
            if (cNsElapsed >= RT_NS_10MS + RT_NS_10MS)
                break;
        }
    }
    uint64_t const cMaxIterations = (RT_NS_1SEC_64 * cSecBench) / RT_MAX(cNsElapsed / i, 2);

    /*
     * Do the real testing.
     */
    nsStart = RTTimeNanoTS();
    for (i = 0; i < cMaxIterations; i++)
    {
        pfnTest(pbDst, pbSrc, cbBuf);
        ASMCompilerBarrier();
        pfnTest(pbDst, pbSrc, cbBuf);
        ASMCompilerBarrier();
        pfnTest(pbDst, pbSrc, cbBuf);
        ASMCompilerBarrier();
        pfnTest(pbDst, pbSrc, cbBuf);
    }
    cNsElapsed = RTTimeNanoTS() - nsStart;

    RTTestValue(g_hTest, pszName, cbBuf * cMaxIterations * 4 / cSecBench, RTTESTUNIT_BYTES_PER_SEC);
    RTTestValueF(g_hTest, cNsElapsed, RTTESTUNIT_NS, "%s - runtime", pszName);
}


template <bool const a_fIsRead, bool const a_fUseMemCopy, typename a_fnTestType>
static void doMemCopyBench(a_fnTestType *pfnTest)
{
    const uint32_t cbPage   = RTSystemGetPageSize();
    RTTestValue(g_hTest, "Page size", cbPage, RTTESTUNIT_BYTES);
    const size_t   cbMaxBuf = RT_MAX(_4K, cbPage);
    uint8_t * const pbDstBuf = (uint8_t *)RTMemPageAlloc(cbMaxBuf);
    RTTESTI_CHECK_RETV(pbDstBuf);
    uint8_t * const pbSrcBuf = (uint8_t *)RTMemPageAlloc(cbMaxBuf);
    RTTESTI_CHECK(pbSrcBuf);
    if (pbSrcBuf)
    {
        RTRandBytes(pbSrcBuf, cbMaxBuf);

#define COMPOSE_NAME(a_szTail) a_fUseMemCopy ? "memcpy/" a_szTail : a_fIsRead ? "CopyFromGuest/" a_szTail : "CopyToGuest/" a_szTail

        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf,    pbSrcBuf,      1, COMPOSE_NAME("1"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf,    pbSrcBuf,      2, COMPOSE_NAME("2/aligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf+1,  pbSrcBuf,      2, COMPOSE_NAME("2/misaligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf,    pbSrcBuf,      4, COMPOSE_NAME("4/aligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf+3,  pbSrcBuf+1,    4, COMPOSE_NAME("4/misaligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf,    pbSrcBuf,      8, COMPOSE_NAME("8/aligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf+5,  pbSrcBuf+1,    8, COMPOSE_NAME("8/misaligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf,    pbSrcBuf,     16, COMPOSE_NAME("16/aligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf+13, pbSrcBuf+3,   16, COMPOSE_NAME("16/misaligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf,    pbSrcBuf,     32, COMPOSE_NAME("32/aligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf+27, pbSrcBuf+5,   32, COMPOSE_NAME("32/misaligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf,    pbSrcBuf,     64, COMPOSE_NAME("64/aligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf+49, pbSrcBuf+19,  64, COMPOSE_NAME("64/misaligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf,    pbSrcBuf,    128, COMPOSE_NAME("128/aligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf+61, pbSrcBuf+17, 128, COMPOSE_NAME("128/misaligned"));
        if (cbPage != _4K)
            doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf, pbSrcBuf, _4K, COMPOSE_NAME("4K/aligned"));
        doMemCopyBenchSub<a_fIsRead, a_fUseMemCopy>(pfnTest, pbDstBuf, pbSrcBuf, cbPage, COMPOSE_NAME("page/aligned"));

        /* misaligned */

        RTMemPageFree(pbSrcBuf, cbMaxBuf);
    }
    RTMemPageFree(pbDstBuf, cbMaxBuf);
}


int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstPGMMemCopy", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    /*
     * Tests.
     */
#if 1 //def DEBUG
//# define MY_BUFFER_SIZE 512
# define MY_BUFFER_SIZE 256
#else
# define MY_BUFFER_SIZE 640
#endif
    RTTestSub(g_hTest, "CopyToGuest");
    doMemCopyTest<0, MY_BUFFER_SIZE>(RTTestGuardedAllocTail);
    doMemCopyTest<0, MY_BUFFER_SIZE>(RTTestGuardedAllocHead);
    RTTestSub(g_hTest, "CopyFromGuest");
    doMemCopyTest<1, MY_BUFFER_SIZE>(RTTestGuardedAllocTail);
    doMemCopyTest<1, MY_BUFFER_SIZE>(RTTestGuardedAllocHead);

    /*
     * Benchmarking.
     */
    RTTestSub(g_hTest, "Benchmarking CopyToGuest");
    doMemCopyBench<false, false>(pgmPhysMemCopyWrapper<false>);
    RTTestSub(g_hTest, "Benchmarking CopyFromGuest");
    doMemCopyBench<true, false>(pgmPhysMemCopyWrapper<true>);
    RTTestSub(g_hTest, "Benchmarking memcpy");
    doMemCopyBench<false, true>(memcpy);


    return RTTestSummaryAndDestroy(g_hTest);
}

