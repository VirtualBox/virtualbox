/* $Id: tstNoCrt-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Testcase for the No-CRT assembly bits.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <iprt/nocrt/string.h>
#include <iprt/stream.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#ifdef RT_WITHOUT_NOCRT_WRAPPERS
# error "Build error."
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#define TSTBUF_SIZE 8192
typedef struct TSTBUF
{
    uint8_t abHeadFence[2048];
    uint8_t abBuf[TSTBUF_SIZE];
    uint8_t abTailFence[2048];
} TSTBUF, *PTSTBUF;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static unsigned g_cErrors = 0;


static void my_memset(void *pv, int ch, size_t cb)
{
    uint8_t *pb = (uint8_t *)pv;
    while (cb--)
        *pb++ = ch;
}

static void my_memcheck(const void *pv, int ch, size_t cb, const char *pszDesc)
{
    uint8_t *pb = (uint8_t *)pv;
    while (cb--)
    {
        if (*pb != (uint8_t)ch)
        {
            g_cErrors++;
            size_t off;
            for (off = 1; off < cb && pb[off] != (uint8_t)ch; off++)
                /* nandemonai */;
            off--;
            pb += off;
            cb -= off;
            if (off)
                RTPrintf("tstNoCrt-1: %s: %p:%p - %02x instead of %02x\n",
                         pszDesc, (uintptr_t)pb - (uintptr_t)pv,
                         (uint8_t *)pb - (uint8_t *)pv + off, *pb, (uint8_t)ch);
            else
                RTPrintf("tstNoCrt-1: %s: %p - %02x instead of %02x\n",
                         pszDesc, (uint8_t *)pb - (uint8_t *)pv, *pb, (uint8_t)ch);
        }

        /* next*/
        pb++;
    }
}


static void TstBufInit(PTSTBUF pBuf, int ch)
{
    my_memset(pBuf->abHeadFence, 0x55, sizeof(pBuf->abHeadFence));
    my_memset(pBuf->abBuf,         ch, sizeof(pBuf->abBuf));
    my_memset(pBuf->abTailFence, 0x77, sizeof(pBuf->abHeadFence));
}


static void TstBufCheck(PTSTBUF pBuf, const char *pszDesc)
{
    my_memcheck(pBuf->abHeadFence, 0x55, sizeof(pBuf->abHeadFence), pszDesc);
    my_memcheck(pBuf->abTailFence, 0x77, sizeof(pBuf->abTailFence), pszDesc);
}


#if 0 /* enable this to test the testcase. */
# undef RT_NOCRT
# define RT_NOCRT(a) a
# ifdef _MSC_VER
#  define mempcpy nocrt_mempcpy
# endif
#endif

int main()
{
    /*
     * Prologue.
     */
    RTR3InitExeNoArguments(0);
    RTPrintf("tstNoCrt-1: TESTING...\n");

    /*
     * Sanity.
     */
    TSTBUF Buf1;
    TstBufInit(&Buf1, 1);
    my_memcheck(Buf1.abBuf, 1, TSTBUF_SIZE, "sanity buf1");
    TstBufCheck(&Buf1, "sanity buf1");

    TSTBUF Buf2;
    TstBufInit(&Buf2, 2);
    my_memcheck(Buf2.abBuf, 2, TSTBUF_SIZE, "sanity buf2");
    TstBufCheck(&Buf2, "sanity buf2");
    if (g_cErrors)
    {
        RTPrintf("tstNoCrt-1: FAILED - fatal sanity error\n");
        return 1;
    }

#define CHECK_CCH(expect)  \
        do \
        { \
            if (cch != (expect)) \
            { \
                RTPrintf("tstNoCrt-1(%d): cb=%zu expected=%zu\n", __LINE__, cch, (expect)); \
                g_cErrors++; \
            } \
         } while (0)
    size_t cch;

#define CHECK_PV(expect)  \
        do \
        { \
            if (pv != (expect)) \
            { \
                RTPrintf("tstNoCrt-1(%d): pv=%p expected=%p\n", __LINE__, pv, (expect)); \
                g_cErrors++; \
            } \
         } while (0)
    void *pv;

#define CHECK_DIFF(op)  \
        do \
        { \
            if (!(iDiff op 0)) \
            { \
                RTPrintf("tstNoCrt-1(%d): iDiff=%d expected: %s 0\n", __LINE__, iDiff, #op); \
                g_cErrors++; \
            } \
         } while (0)
    int iDiff;

    static char s_szTest1[] = "0123456789abcdef";
    static char s_szTest2[] = "0123456789abcdef";
    static char s_szTest3[] = "fedcba9876543210";

    /*
     * memcpy.
     */
    RTPrintf("tstNoCrt-1: memcpy\n");
    TstBufInit(&Buf1, 1);
    TstBufInit(&Buf2, 2);
    pv = RT_NOCRT(memcpy)(Buf1.abBuf, Buf2.abBuf, TSTBUF_SIZE); CHECK_PV(Buf1.abBuf);
    my_memcheck(Buf1.abBuf, 2, TSTBUF_SIZE, "memcpy1-dst");
    my_memcheck(Buf2.abBuf, 2, TSTBUF_SIZE, "memcpy1-src");
    TstBufCheck(&Buf1, "memcpy1");
    TstBufCheck(&Buf2, "memcpy1");

    TstBufInit(&Buf1, 3);
    TstBufInit(&Buf2, 4);
    pv = RT_NOCRT(memcpy)(Buf2.abBuf, Buf1.abBuf, TSTBUF_SIZE); CHECK_PV(Buf2.abBuf);
    my_memcheck(Buf1.abBuf, 3, TSTBUF_SIZE, "memcpy2-dst");
    my_memcheck(Buf2.abBuf, 3, TSTBUF_SIZE, "memcpy2-src");
    TstBufCheck(&Buf1, "memcpy2");
    TstBufCheck(&Buf2, "memcpy2");

    TstBufInit(&Buf1, 5);
    TstBufInit(&Buf2, 6);
    pv = RT_NOCRT(memcpy)(Buf2.abBuf, Buf1.abBuf, 0); CHECK_PV(Buf2.abBuf);
    my_memcheck(Buf1.abBuf, 5, TSTBUF_SIZE, "memcpy3-dst");
    my_memcheck(Buf2.abBuf, 6, TSTBUF_SIZE, "memcpy3-src");
    TstBufCheck(&Buf1, "memcpy3-dst");
    TstBufCheck(&Buf2, "memcpy3-src");

    for (unsigned off1 = 0; off1 <= 128; off1++)
    {
        for (unsigned off2 = 0; off2 <= 256; off2++)
        {
            char sz[32];
            RTStrPrintf(sz, sizeof(sz), "memcpy4-%d-%d", off1, off2);
            TstBufInit(&Buf1, 1);
            TstBufInit(&Buf2, 2);
            size_t cb = off2;
            pv = RT_NOCRT(memcpy)(&Buf2.abBuf[off2], &Buf1.abBuf[off1], cb); CHECK_PV(&Buf2.abBuf[off2]);
            my_memcheck(Buf1.abBuf, 1, TSTBUF_SIZE, sz);
            my_memcheck(Buf2.abBuf, 2, off2, sz);
            my_memcheck(&Buf2.abBuf[off2], 1, cb, sz);
            my_memcheck(&Buf2.abBuf[off2 + cb], 2, TSTBUF_SIZE - cb - off2, sz);
            TstBufCheck(&Buf1, sz);
            TstBufCheck(&Buf2, sz);
        }
    }

    /*
     * mempcpy.
     */
    RTPrintf("tstNoCrt-1: mempcpy\n");
    TstBufInit(&Buf1, 1);
    TstBufInit(&Buf2, 2);
    pv = RT_NOCRT(mempcpy)(Buf1.abBuf, Buf2.abBuf, TSTBUF_SIZE); CHECK_PV(&Buf1.abBuf[TSTBUF_SIZE]);
    my_memcheck(Buf1.abBuf, 2, TSTBUF_SIZE, "mempcpy1-dst");
    my_memcheck(Buf2.abBuf, 2, TSTBUF_SIZE, "mempcpy1-src");
    TstBufCheck(&Buf1, "mempcpy1");
    TstBufCheck(&Buf2, "mempcpy1");

    TstBufInit(&Buf1, 3);
    TstBufInit(&Buf2, 4);
    pv = RT_NOCRT(mempcpy)(Buf2.abBuf, Buf1.abBuf, TSTBUF_SIZE); CHECK_PV(&Buf2.abBuf[TSTBUF_SIZE]);
    my_memcheck(Buf1.abBuf, 3, TSTBUF_SIZE, "mempcpy2-dst");
    my_memcheck(Buf2.abBuf, 3, TSTBUF_SIZE, "mempcpy2-src");
    TstBufCheck(&Buf1, "mempcpy2");
    TstBufCheck(&Buf2, "mempcpy2");

    TstBufInit(&Buf1, 5);
    TstBufInit(&Buf2, 6);
    pv = RT_NOCRT(mempcpy)(Buf2.abBuf, Buf1.abBuf, 0); CHECK_PV(Buf2.abBuf);
    my_memcheck(Buf1.abBuf, 5, TSTBUF_SIZE, "mempcpy3-dst");
    my_memcheck(Buf2.abBuf, 6, TSTBUF_SIZE, "mempcpy3-src");
    TstBufCheck(&Buf1, "mempcpy3-dst");
    TstBufCheck(&Buf2, "mempcpy3-src");

    for (unsigned off1 = 0; off1 <= 128; off1++)
    {
        for (unsigned off2 = 0; off2 <= 256; off2++)
        {
            char sz[32];
            RTStrPrintf(sz, sizeof(sz), "mempcpy4-%d-%d", off1, off2);
            TstBufInit(&Buf1, 1);
            TstBufInit(&Buf2, 2);
            size_t cb = off2;
            pv = RT_NOCRT(mempcpy)(&Buf2.abBuf[off2], &Buf1.abBuf[off1], cb); CHECK_PV(&Buf2.abBuf[off2 + cb]);
            my_memcheck(Buf1.abBuf, 1, TSTBUF_SIZE, sz);
            my_memcheck(Buf2.abBuf, 2, off2, sz);
            my_memcheck(&Buf2.abBuf[off2], 1, cb, sz);
            my_memcheck(&Buf2.abBuf[off2 + cb], 2, TSTBUF_SIZE - cb - off2, sz);
            TstBufCheck(&Buf1, sz);
            TstBufCheck(&Buf2, sz);
        }
    }

    /*
     * memmove.
     */
    RTPrintf("tstNoCrt-1: memmove\n");
    TstBufInit(&Buf1, 1);
    TstBufInit(&Buf2, 2);
    pv = RT_NOCRT(memmove)(Buf1.abBuf, Buf2.abBuf, TSTBUF_SIZE); CHECK_PV(Buf1.abBuf);
    my_memcheck(Buf1.abBuf, 2, TSTBUF_SIZE, "memmove1-dst");
    my_memcheck(Buf2.abBuf, 2, TSTBUF_SIZE, "memmove1-src");
    TstBufCheck(&Buf1, "memmove1");
    TstBufCheck(&Buf2, "memmove1");

    TstBufInit(&Buf1, 3);
    TstBufInit(&Buf2, 4);
    pv = RT_NOCRT(memmove)(Buf2.abBuf, Buf1.abBuf, TSTBUF_SIZE); CHECK_PV(Buf2.abBuf);
    my_memcheck(Buf1.abBuf, 3, TSTBUF_SIZE, "memmove2-dst");
    my_memcheck(Buf2.abBuf, 3, TSTBUF_SIZE, "memmove2-src");
    TstBufCheck(&Buf1, "memmove2");
    TstBufCheck(&Buf2, "memmove2");

    TstBufInit(&Buf1, 5);
    TstBufInit(&Buf2, 6);
    pv = RT_NOCRT(memmove)(Buf2.abBuf, Buf1.abBuf, 0); CHECK_PV(Buf2.abBuf);
    my_memcheck(Buf1.abBuf, 5, TSTBUF_SIZE, "memmove3-dst");
    my_memcheck(Buf2.abBuf, 6, TSTBUF_SIZE, "memmove3-src");
    TstBufCheck(&Buf1, "memmove3-dst");
    TstBufCheck(&Buf2, "memmove3-src");

    for (unsigned off1 = 1; off1 <= 128; off1++)
    {
        for (unsigned off2 = 0; off2 <= 256; off2++)
        {
            /* forward */
            char sz[32];
            RTStrPrintf(sz, sizeof(sz), "memmove4-%d-%d", off1, off2);
            TstBufInit(&Buf1, off1 + 1);
            my_memset(Buf1.abBuf, off1, off1);
            pv = RT_NOCRT(memmove)(Buf1.abBuf, &Buf1.abBuf[off2], TSTBUF_SIZE - off2); CHECK_PV(Buf1.abBuf);
            if (off2 < off1)
            {
                unsigned cbLead = off1 - off2;
                my_memcheck(Buf1.abBuf, off1, cbLead, sz);
                my_memcheck(&Buf1.abBuf[cbLead], off1 + 1, TSTBUF_SIZE - cbLead, sz);
            }
            else
                my_memcheck(Buf1.abBuf, off1 + 1, TSTBUF_SIZE, sz);
            TstBufCheck(&Buf1, sz);

            /* backward */
            RTStrPrintf(sz, sizeof(sz), "memmove5-%d-%d", off1, off2);
            TstBufInit(&Buf1, off1 + 1);
            my_memset(&Buf1.abBuf[TSTBUF_SIZE - off1], off1, off1);
            pv = RT_NOCRT(memmove)(&Buf1.abBuf[off2], Buf1.abBuf, TSTBUF_SIZE - off2); CHECK_PV(&Buf1.abBuf[off2]);
            if (off2 < off1)
            {
                unsigned cbLead = off1 - off2;
                my_memcheck(&Buf1.abBuf[TSTBUF_SIZE - cbLead], off1, cbLead, sz);
                my_memcheck(Buf1.abBuf, off1 + 1, TSTBUF_SIZE - cbLead, sz);
            }
            else
                my_memcheck(Buf1.abBuf, off1 + 1, TSTBUF_SIZE, sz);
            TstBufCheck(&Buf1, sz);
                                   /* small unaligned */
            RTStrPrintf(sz, sizeof(sz), "memmove6-%d-%d", off1, off2);
            TstBufInit(&Buf1, 1);
            TstBufInit(&Buf2, 2);
            size_t cb = off2;
            pv = RT_NOCRT(memmove)(&Buf2.abBuf[off2], &Buf1.abBuf[off1], cb); CHECK_PV(&Buf2.abBuf[off2]);
            my_memcheck(Buf1.abBuf, 1, TSTBUF_SIZE, sz);
            my_memcheck(Buf2.abBuf, 2, off2, sz);
            my_memcheck(&Buf2.abBuf[off2], 1, cb, sz);
            my_memcheck(&Buf2.abBuf[off2 + cb], 2, TSTBUF_SIZE - cb - off2, sz);
            TstBufCheck(&Buf1, sz);
            TstBufCheck(&Buf2, sz);
        }
    }

    /*
     * memset
     */
    RTPrintf("tstNoCrt-1: memset\n");
    TstBufInit(&Buf1, 1);
    pv = RT_NOCRT(memset)(Buf1.abBuf, 0, TSTBUF_SIZE); CHECK_PV(Buf1.abBuf);
    my_memcheck(Buf1.abBuf, 0, TSTBUF_SIZE, "memset-1");
    TstBufCheck(&Buf1, "memset-1");

    TstBufInit(&Buf1, 1);
    pv = RT_NOCRT(memset)(Buf1.abBuf, 0xff, TSTBUF_SIZE); CHECK_PV(Buf1.abBuf);
    my_memcheck(Buf1.abBuf, 0xff, TSTBUF_SIZE, "memset-2");
    TstBufCheck(&Buf1, "memset-2");

    TstBufInit(&Buf1, 1);
    pv = RT_NOCRT(memset)(Buf1.abBuf, 0xff, 0); CHECK_PV(Buf1.abBuf);
    my_memcheck(Buf1.abBuf, 1, TSTBUF_SIZE, "memset-3");
    TstBufCheck(&Buf1, "memset-3");

    for (unsigned off = 0; off < 256; off++)
    {
        /* move start byte by byte. */
        char sz[32];
        RTStrPrintf(sz, sizeof(sz), "memset4-%d", off);
        TstBufInit(&Buf1, 0);
        pv = RT_NOCRT(memset)(&Buf1.abBuf[off], off, TSTBUF_SIZE - off); CHECK_PV(&Buf1.abBuf[off]);
        my_memcheck(Buf1.abBuf, 0, off, sz);
        my_memcheck(&Buf1.abBuf[off], off, TSTBUF_SIZE - off, sz);
        TstBufCheck(&Buf1, sz);

        /* move end byte by byte. */
        RTStrPrintf(sz, sizeof(sz), "memset5-%d", off);
        TstBufInit(&Buf1, 0);
        pv = RT_NOCRT(memset)(Buf1.abBuf, off, TSTBUF_SIZE - off); CHECK_PV(Buf1.abBuf);
        my_memcheck(Buf1.abBuf, off, TSTBUF_SIZE - off, sz);
        my_memcheck(&Buf1.abBuf[TSTBUF_SIZE - off], 0, off, sz);
        TstBufCheck(&Buf1, sz);

        /* move both start and size byte by byte. */
        RTStrPrintf(sz, sizeof(sz), "memset6-%d", off);
        TstBufInit(&Buf1, 0);
        pv = RT_NOCRT(memset)(&Buf1.abBuf[off], off, off); CHECK_PV(&Buf1.abBuf[off]);
        my_memcheck(Buf1.abBuf, 0, off, sz);
        my_memcheck(&Buf1.abBuf[off], off, off, sz);
        my_memcheck(&Buf1.abBuf[off * 2], 0, TSTBUF_SIZE - off * 2, sz);
        TstBufCheck(&Buf1, sz);
    }

    /*
     * strcpy (quick smoke testing).
     */
    RTPrintf("tstNoCrt-1: strcpy\n");
    TstBufInit(&Buf1, 1);
    const char *pszSrc = s_szTest1;
    char *pszDst = (char *)&Buf1.abBuf[0];
    pv = RT_NOCRT(strcpy)(pszDst, pszSrc);
    CHECK_PV(pszDst);
    TstBufCheck(&Buf1, "strcpy 1");
    iDiff = RT_NOCRT(strcmp)(pszDst, pszSrc); CHECK_DIFF( == );

    pszSrc = s_szTest1;
    for (unsigned i = 0; i < sizeof(s_szTest1) / 2; i++)
    {
        pszSrc++;
        TstBufInit(&Buf1, 2);
        pszDst = (char *)&Buf1.abBuf[sizeof(Buf1.abBuf) - strlen(pszSrc) - 1];
        pv = RT_NOCRT(strcpy)(pszDst, pszSrc);
        CHECK_PV(pszDst);
        TstBufCheck(&Buf1, "strcpy 3");
        iDiff = RT_NOCRT(strcmp)(pszDst, pszSrc); CHECK_DIFF( == );
    }


    /*
     * memchr & strchr.
     */
    RTPrintf("tstNoCrt-1: memchr\n");
    pv = RT_NOCRT(memchr)(&s_szTest1[0x00], 'f', sizeof(s_szTest1)); CHECK_PV(&s_szTest1[0xf]);
    pv = RT_NOCRT(memchr)(&s_szTest1[0x0f], 'f', sizeof(s_szTest1)); CHECK_PV(&s_szTest1[0xf]);
    pv = RT_NOCRT(memchr)(&s_szTest1[0x03],   0, sizeof(s_szTest1)); CHECK_PV(&s_szTest1[0x10]);
    pv = RT_NOCRT(memchr)(&s_szTest1[0x10],   0, sizeof(s_szTest1)); CHECK_PV(&s_szTest1[0x10]);
    pv = RT_NOCRT(memchr)(&s_szTest1,         0, ~(size_t)0);        CHECK_PV(&s_szTest1[0x10]);
    pv = RT_NOCRT(memchr)(&s_szTest1,         0, ~(size_t)1);        CHECK_PV(&s_szTest1[0x10]);
    pv = RT_NOCRT(memchr)(&s_szTest1,         0, ~(size_t)16);       CHECK_PV(&s_szTest1[0x10]);
    for (unsigned i = 0; i < sizeof(s_szTest1); i++)
        for (unsigned j = 0; j <= i; j++)
        {
            pv = RT_NOCRT(memchr)(&s_szTest1[j], s_szTest1[i], sizeof(s_szTest1));
            CHECK_PV(&s_szTest1[i]);
        }

    RTPrintf("tstNoCrt-1: strchr\n");
    pv = RT_NOCRT(strchr)(&s_szTest1[0x00], 'f'); CHECK_PV(&s_szTest1[0xf]);
    pv = RT_NOCRT(strchr)(&s_szTest1[0x0f], 'f'); CHECK_PV(&s_szTest1[0xf]);
    pv = RT_NOCRT(strchr)(&s_szTest1[0x03], 0); CHECK_PV(&s_szTest1[0x10]);
    pv = RT_NOCRT(strchr)(&s_szTest1[0x10], 0); CHECK_PV(&s_szTest1[0x10]);
    for (unsigned i = 0; i < sizeof(s_szTest1); i++)
        for (unsigned j = 0; j <= i; j++)
        {
            pv = RT_NOCRT(strchr)(&s_szTest1[j], s_szTest1[i]);
            CHECK_PV(&s_szTest1[i]);
        }

    /*
     * Some simple memcmp/strcmp checks.
     */
    RTPrintf("tstNoCrt-1: memcmp\n");
    iDiff = RT_NOCRT(memcmp)(s_szTest1, s_szTest1, sizeof(s_szTest1)); CHECK_DIFF( == );
    iDiff = RT_NOCRT(memcmp)(s_szTest1, s_szTest2, sizeof(s_szTest1)); CHECK_DIFF( == );
    iDiff = RT_NOCRT(memcmp)(s_szTest2, s_szTest1, sizeof(s_szTest1)); CHECK_DIFF( == );
    iDiff = RT_NOCRT(memcmp)(s_szTest3, s_szTest3, sizeof(s_szTest1)); CHECK_DIFF( == );
    iDiff = RT_NOCRT(memcmp)(s_szTest1, s_szTest3, sizeof(s_szTest1)); CHECK_DIFF( < );
    iDiff = RT_NOCRT(memcmp)(s_szTest3, s_szTest1, sizeof(s_szTest1)); CHECK_DIFF( > );
    iDiff = RT_NOCRT(memcmp)("1234", "1a34", 4); CHECK_DIFF( < );

    RTPrintf("tstNoCrt-1: strcmp\n");
    iDiff = RT_NOCRT(strcmp)(s_szTest1, s_szTest1); CHECK_DIFF( == );
    iDiff = RT_NOCRT(strcmp)(s_szTest1, s_szTest2); CHECK_DIFF( == );
    iDiff = RT_NOCRT(strcmp)(s_szTest2, s_szTest1); CHECK_DIFF( == );
    iDiff = RT_NOCRT(strcmp)(s_szTest3, s_szTest3); CHECK_DIFF( == );
    iDiff = RT_NOCRT(strcmp)(s_szTest1, s_szTest3); CHECK_DIFF( < );
    iDiff = RT_NOCRT(strcmp)(s_szTest3, s_szTest1); CHECK_DIFF( > );

    /*
     * Some simple strlen checks.
     */
    RTPrintf("tstNoCrt-1: strlen\n");
    cch = RT_NOCRT(strlen)("");             CHECK_CCH(0);
    cch = RT_NOCRT(strlen)("1");            CHECK_CCH(1);
    cch = RT_NOCRT(strlen)("12");           CHECK_CCH(2);
    cch = RT_NOCRT(strlen)("123");          CHECK_CCH(3);
    cch = RT_NOCRT(strlen)("1234");         CHECK_CCH(4);
    cch = RT_NOCRT(strlen)("12345");        CHECK_CCH(5);
    cch = RT_NOCRT(strlen)(s_szTest1);      CHECK_CCH(sizeof(s_szTest1) - 1);
    cch = RT_NOCRT(strlen)(&s_szTest1[1]);  CHECK_CCH(sizeof(s_szTest1) - 1 - 1);
    cch = RT_NOCRT(strlen)(&s_szTest1[2]);  CHECK_CCH(sizeof(s_szTest1) - 1 - 2);
    cch = RT_NOCRT(strlen)(&s_szTest1[3]);  CHECK_CCH(sizeof(s_szTest1) - 1 - 3);
    cch = RT_NOCRT(strlen)(&s_szTest1[4]);  CHECK_CCH(sizeof(s_szTest1) - 1 - 4);
    cch = RT_NOCRT(strlen)(&s_szTest1[5]);  CHECK_CCH(sizeof(s_szTest1) - 1 - 5);
    cch = RT_NOCRT(strlen)(&s_szTest1[6]);  CHECK_CCH(sizeof(s_szTest1) - 1 - 6);
    cch = RT_NOCRT(strlen)(&s_szTest1[7]);  CHECK_CCH(sizeof(s_szTest1) - 1 - 7);
    cch = RT_NOCRT(strlen)(s_szTest2);      CHECK_CCH(sizeof(s_szTest2) - 1);
    cch = RT_NOCRT(strlen)(s_szTest3);      CHECK_CCH(sizeof(s_szTest3) - 1);

#ifdef _MSC_VER
    /*
     * Some simple wcslen checks.
     */
    RTPrintf("tstNoCrt-1: wcslen\n");
    cch = RT_NOCRT(wcslen)(L"");             CHECK_CCH(0);
    cch = RT_NOCRT(wcslen)(L"1");            CHECK_CCH(1);
    cch = RT_NOCRT(wcslen)(L"12");           CHECK_CCH(2);
    cch = RT_NOCRT(wcslen)(L"123");          CHECK_CCH(3);
    cch = RT_NOCRT(wcslen)(L"1234");         CHECK_CCH(4);
    cch = RT_NOCRT(wcslen)(L"12345");        CHECK_CCH(5);
#endif

    /*
     * Summary.
     */
    if (!g_cErrors)
        RTPrintf("tstNoCrt-1: SUCCESS\n");
    else
        RTPrintf("tstNoCrt-1: FAILURE - %d errors\n", g_cErrors);
    return !!g_cErrors;
}

