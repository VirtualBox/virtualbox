/* $Id: tstRTArmv8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - armv8.h inline functions.
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
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/asm.h> /** @todo fix me! */
#include <iprt/armv8.h>

#include <iprt/test.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/sort.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
static unsigned g_cVerbosity = 0;


static const char *tobin(uint64_t uValue, unsigned cchWidth, char *pszBuf)
{
    char *psz = pszBuf;
    while (cchWidth-- > 0)
        *psz++ = (uValue & RT_BIT_64(cchWidth)) ? '1' : '0';
    *psz = '\0';
    return pszBuf;
}


/** @callback_method_impl{FNRTSORTCMP}   */
static DECLCALLBACK(int) cmpu32(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    if (*(uint32_t const *)pvElement1 < *(uint32_t const *)pvElement2)
        return -1;
    if (*(uint32_t const *)pvElement1 > *(uint32_t const *)pvElement2)
        return 1;
    return 0;
}


/** @callback_method_impl{FNRTSORTCMP}   */
static DECLCALLBACK(int) cmpu64(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    if (*(uint64_t const *)pvElement1 < *(uint64_t const *)pvElement2)
        return -1;
    if (*(uint64_t const *)pvElement1 > *(uint64_t const *)pvElement2)
        return 1;
    return 0;
}


static unsigned BinarySearchU64(uint64_t uValue, uint64_t const *pauEntries, unsigned idxEnd)
{
    unsigned idxStart = 0;
    for (;;)
    {
        unsigned const idx    = (idxEnd - idxStart) / 2 + idxStart;
        uint64_t const uEntry = pauEntries[idx];
        if (uValue < uEntry)
        {
            if (idx > idxStart)
                idxEnd = idx;
            else
                return ~0U;
        }
        else if (uValue > uEntry)
        {
            if (idx + 1 < idxEnd)
                idxStart = idx + 1;
            else
                return ~0U;
        }
        else
            return idx;
    }
}


static void tstLogicalMask32(void)
{
    RTTestISub("32-bit logical masks");
    static uint32_t s_auValidMasks[1312];
    unsigned        cValidMasks = 0;

    /* Test all legal combinations, both directions. */
    char     szMask[128];
    char     szImmR[32];
    char     szImmS[32];
    uint32_t uImmSElmnLength = 0x3c;
    for (unsigned cBitsElementLog2 = 1; cBitsElementLog2 <= 5; cBitsElementLog2++)
    {
        unsigned const cBitsElement = 1U << cBitsElementLog2;
        for (unsigned cOneBits = 0; cOneBits < cBitsElement - 1; cOneBits++)
        {
            for (unsigned cRotations = 0; cRotations < cBitsElement; cRotations++)
            {
                uint32_t const uImmS = cOneBits | uImmSElmnLength;
                uint32_t const uMask = Armv8A64ConvertImmRImmS2Mask32(uImmS, cRotations);
                RTTESTI_CHECK_RETV(cValidMasks < RT_ELEMENTS(s_auValidMasks));
                s_auValidMasks[cValidMasks++] = uMask;

                if (g_cVerbosity > 1)
                    RTPrintf("%08x %s size=%02u length=%02u rotation=%02u N=%u immr=%s imms=%s\n",
                             uMask, tobin(uMask, 32, szMask), cBitsElement, cOneBits, cRotations, 0,
                             tobin(cRotations, 6, szImmR), tobin(uImmS, 6, szImmS));

                uint32_t uImmSRev = UINT32_MAX;
                uint32_t uImmRRev = UINT32_MAX;
                if (!Armv8A64ConvertMask32ToImmRImmS(uMask, &uImmSRev, &uImmRRev))
                    RTTestIFailed("Armv8A64ConvertMask32ToImmRImmS: failed for uMask=%#x (expected immS=%#x immR=%#x)\n",
                                  uMask, uImmS, cRotations);
                else if (uImmSRev != uImmS || uImmRRev != cRotations)
                    RTTestIFailed("Armv8A64ConvertMask32ToImmRImmS: incorrect results for uMask=%#x: immS=%#x immR=%#x, expected %#x & %#x\n",
                                  uMask, uImmSRev, uImmRRev, uImmS, cRotations);
            }
        }
        uImmSElmnLength = (uImmSElmnLength << 1) & 0x3f;
    }

    /* Now the other way around. */
    RTSortShell(s_auValidMasks, cValidMasks, sizeof(s_auValidMasks[0]), cmpu32, NULL);
    unsigned iValidMask     = 0;
    uint32_t uNextValidMask = s_auValidMasks[iValidMask];
    uint32_t uMask          = 0;
    do
    {
        uint32_t uImmSRev = UINT32_MAX;
        uint32_t uImmRRev = UINT32_MAX;
        if (!Armv8A64ConvertMask32ToImmRImmS(uMask, &uImmSRev, &uImmRRev))
        {
            if (RT_LIKELY(uMask < uNextValidMask || iValidMask >= cValidMasks))
            { }
            else
            {
                RTTestIFailed("uMask=%#x - false negative\n", uMask);
                break;
            }
        }
        else if (RT_LIKELY(uMask == uNextValidMask && iValidMask < cValidMasks))
        {
            if (iValidMask + 1 < cValidMasks)
                uNextValidMask = s_auValidMasks[++iValidMask];
            else
            {
                iValidMask     = cValidMasks;
                uNextValidMask = UINT32_MAX;
            }
        }
        else
        {
            RTTestIFailed("uMask=%#x - false positive\n", uMask);
            break;
        }
    } while (uMask++ < UINT32_MAX);
}


static void tstLogicalMask64(void)
{
    RTTestISub("64-bit logical masks");
    static uint64_t s_auValidMasks[5376];
    unsigned        cValidMasks = 0;

    /* Test all legal combinations, both directions. */
    char     szMask[128];
    char     szImmR[32];
    char     szImmS[32];
    uint32_t uImmSElmnLength = 0x3c;
    for (unsigned cBitsElementLog2 = 1; cBitsElementLog2 <= 6; cBitsElementLog2++)
    {
        unsigned const cBitsElement = 1U << cBitsElementLog2;
        for (unsigned cOneBits = 0; cOneBits < cBitsElement - 1; cOneBits++)
        {
            for (unsigned cRotations = 0; cRotations < cBitsElement; cRotations++)
            {
                uint32_t const uImmS = cOneBits | uImmSElmnLength;
                uint64_t const uMask = Armv8A64ConvertImmRImmS2Mask64(uImmS, cRotations);
                RTTESTI_CHECK_RETV(cValidMasks < RT_ELEMENTS(s_auValidMasks));
                s_auValidMasks[cValidMasks++] = uMask;

                if (g_cVerbosity > 1)
                    RTPrintf("%016llx %s size=%02u length=%02u rotation=%02u N=%u immr=%s imms=%s\n",
                             uMask, tobin(uMask, 64, szMask), cBitsElement, cOneBits, cRotations, !!(uImmSElmnLength & 0x40),
                             tobin(cRotations, 6, szImmR), tobin(uImmS, 6, szImmS));

                uint32_t uImmSRev = UINT32_MAX;
                uint32_t uImmRRev = UINT32_MAX;
                if (!Armv8A64ConvertMask64ToImmRImmS(uMask, &uImmSRev, &uImmRRev))
                    RTTestIFailed("Armv8A64ConvertMask64ToImmRImmS: failed for uMask=%#llx (expected immS=%#x immR=%#x)\n",
                                  uMask, uImmS, cRotations);
                else if (uImmSRev != uImmS || uImmRRev != cRotations)
                    RTTestIFailed("Armv8A64ConvertMask64ToImmRImmS: incorrect results for uMask=%#llx: immS=%#x immR=%#x, expected %#x & %#x\n",
                                  uMask, uImmSRev, uImmRRev, uImmS, cRotations);
            }
        }
        uImmSElmnLength = (uImmSElmnLength << 1) & 0x3f;
        if (cBitsElementLog2 == 5)
            uImmSElmnLength = 0x40;
    }

    /* Now the other way around, using random masks. */
    RTSortShell(s_auValidMasks, cValidMasks, sizeof(s_auValidMasks[0]), cmpu64, NULL);

    for (unsigned iRand = 0; iRand < _32M; iRand++)
    {
        uint64_t const uMask    = RTRandU64();
        unsigned const idxMask  = BinarySearchU64(uMask, s_auValidMasks, cValidMasks);
        uint32_t       uImmSRev = UINT32_MAX;
        uint32_t       uImmRRev = UINT32_MAX;
        if (!Armv8A64ConvertMask64ToImmRImmS(uMask, &uImmSRev, &uImmRRev))
        {
            if (RT_LIKELY(idxMask == ~0U))
            { }
            else
            {
                RTTestIFailed("uMask=%#018llx - false negative\n", uMask);
                break;
            }
        }
        else if (RT_LIKELY(idxMask < cValidMasks))
        { /* likely */ }
        else
        {
            RTTestIFailed("uMask=%#018llx - false positive\n", uMask);
            break;
        }
    }
}


int main()
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTArmv8", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    tstLogicalMask32();
    tstLogicalMask64();

    return RTTestSummaryAndDestroy(hTest);
}

