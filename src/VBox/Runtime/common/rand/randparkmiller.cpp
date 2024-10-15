/* $Id: randparkmiller.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Random Numbers, Park-Miller Pseudo Random.
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
#include <iprt/rand.h>
#include "internal/iprt.h"

#include <iprt/asm-math.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/errcore.h>
#include "internal/rand.h"
#include "internal/magics.h"



DECLINLINE(uint32_t) rtRandParkMillerU31(uint32_t *pu32Ctx)
{
    /*
     * Park-Miller random number generator:
     *      X2 = X1 * g mod n.
     *
     * We use the constants suggested by Park and Miller:
     *      n = 2^31 - 1 = INT32_MAX
     *      g = 7^5 = 16807
     *
     * This will produce numbers in the range [0..INT32_MAX-1], which is
     * almost 31-bits. We'll ignore the missing number for now and settle
     * for just filling in the missing bit instead (the caller does this).
     */
    uint32_t x1 = *pu32Ctx;
    if (!x1)
        x1 = 20080806;
    /*uint32_t x2 = ((uint64_t)x1 * 16807) % INT32_MAX;*/
    uint32_t x2 = ASMModU64ByU32RetU32(ASMMult2xU32RetU64(x1, 16807), INT32_MAX);
    return *pu32Ctx = x2;
}


/** @copydoc RTRANDINT::pfnGetU32 */
static DECLCALLBACK(uint32_t) rtRandParkMillerGetU32(PRTRANDINT pThis, uint32_t u32First, uint32_t u32Last)
{
    uint32_t off;
    uint32_t offLast = u32Last - u32First;
    if (offLast == UINT32_MAX)
    {
        /* 30 + 2 bit (make up for the missing INT32_MAX value) */
        off = rtRandParkMillerU31(&pThis->u.ParkMiller.u32Ctx);
        if (pThis->u.ParkMiller.cBits < 2)
        {
            pThis->u.ParkMiller.u32Bits = rtRandParkMillerU31(&pThis->u.ParkMiller.u32Ctx);
            pThis->u.ParkMiller.cBits = 30;
        }
        off >>= 1;
        off |= (pThis->u.ParkMiller.u32Bits & 3) << 30;
        pThis->u.ParkMiller.u32Bits >>= 2;
        pThis->u.ParkMiller.cBits -= 2;
    }
    else if (offLast == (uint32_t)INT32_MAX - 1)
        /* The exact range. */
        off = rtRandParkMillerU31(&pThis->u.ParkMiller.u32Ctx);
    else if (offLast < UINT32_C(0x07ffffff))
    {
        /* Requested 23 or fewer bits, just lose the lower bit. */
        off = rtRandParkMillerU31(&pThis->u.ParkMiller.u32Ctx);
        off >>= 1;
        off %= (offLast + 1);
    }
    else
    {
        /*
         * 30 + 6 bits.
         */
        uint64_t off64 = rtRandParkMillerU31(&pThis->u.ParkMiller.u32Ctx);
        if (pThis->u.ParkMiller.cBits < 6)
        {
            pThis->u.ParkMiller.u32Bits = rtRandParkMillerU31(&pThis->u.ParkMiller.u32Ctx);
            pThis->u.ParkMiller.cBits = 30;
        }
        off64 >>= 1;
        off64 |= (uint64_t)(pThis->u.ParkMiller.u32Bits & 0x3f) << 30;
        pThis->u.ParkMiller.u32Bits >>= 6;
        pThis->u.ParkMiller.cBits -= 6;
        off = ASMModU64ByU32RetU32(off64, offLast + 1);
    }
    return off + u32First;
}


/** @copydoc RTRANDINT::pfnSeed */
static DECLCALLBACK(int) rtRandParkMillerSeed(PRTRANDINT pThis, uint64_t u64Seed)
{
    pThis->u.ParkMiller.u32Ctx = (uint32_t)u64Seed;
    pThis->u.ParkMiller.u32Bits = 0;
    pThis->u.ParkMiller.cBits = 0;
    return VINF_SUCCESS;
}


/** @copydoc RTRANDINT::pfnSaveState */
static DECLCALLBACK(int) rtRandParkMillerSaveState(PRTRANDINT pThis, char *pszState, size_t *pcbState)
{
#define RTRAND_PARKMILLER_STATE_SIZE (3+8+1+8+1+2+1+1)

    if (*pcbState < RTRAND_PARKMILLER_STATE_SIZE)
    {
        *pcbState = RTRAND_PARKMILLER_STATE_SIZE;
        return VERR_BUFFER_OVERFLOW;
    }
    RTStrPrintf(pszState, *pcbState, "PM:%08RX32,%08RX32,%02x;",
                pThis->u.ParkMiller.u32Ctx,
                pThis->u.ParkMiller.u32Bits,
                pThis->u.ParkMiller.cBits);
    return VINF_SUCCESS;
}


/** @copydoc RTRANDINT::pfnRestoreState */
static DECLCALLBACK(int) rtRandParkMillerRestoreState(PRTRANDINT pThis, char const *pszState)
{
    /* marker */
    if (    pszState[0] != 'P'
        ||  pszState[1] != 'M'
        ||  pszState[2] != ':')
        return VERR_PARSE_ERROR;
    pszState += 3;

    /* u32Ctx */
    char *pszNext = NULL;
    uint32_t u32Ctx;
    int rc = RTStrToUInt32Ex(pszState, &pszNext, 16, &u32Ctx);
    if (    rc != VWRN_TRAILING_CHARS
        ||  pszNext !=  pszState + 8
        ||  *pszNext != ',')
        return VERR_PARSE_ERROR;
    pszState += 8 + 1;

    /* u32Bits */
    uint32_t u32Bits;
    rc = RTStrToUInt32Ex(pszState, &pszNext, 16, &u32Bits);
    if (    rc != VWRN_TRAILING_CHARS
        ||  pszNext !=  pszState + 8
        ||  *pszNext != ',')
        return VERR_PARSE_ERROR;
    pszState += 8 + 1;

    /* cBits */
    uint32_t cBits;
    rc = RTStrToUInt32Ex(pszState, &pszNext, 16, &cBits);
    if (    rc != VWRN_TRAILING_CHARS
        ||  pszNext !=  pszState + 2
        ||  *pszNext != ';'
        ||  pszNext[1] != '\0')
        return VERR_PARSE_ERROR;

    /* commit */
    pThis->u.ParkMiller.u32Ctx  = u32Ctx;
    pThis->u.ParkMiller.u32Bits = u32Bits;
    pThis->u.ParkMiller.cBits   = cBits;
    return VINF_SUCCESS;
}


RTDECL(int) RTRandAdvCreateParkMiller(PRTRAND phRand) RT_NO_THROW_DEF
{
    PRTRANDINT pThis = (PRTRANDINT)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;
    pThis->u32Magic = RTRANDINT_MAGIC;
    pThis->pfnGetBytes= rtRandAdvSynthesizeBytesFromU32;
    pThis->pfnGetU32  = rtRandParkMillerGetU32;
    pThis->pfnGetU64  = rtRandAdvSynthesizeU64FromU32;
    pThis->pfnSeed    = rtRandParkMillerSeed;
    pThis->pfnSaveState = rtRandParkMillerSaveState;
    pThis->pfnRestoreState = rtRandParkMillerRestoreState;
    pThis->pfnDestroy = rtRandAdvDefaultDestroy;
    pThis->u.ParkMiller.u32Ctx = 0x20080806;
    pThis->u.ParkMiller.u32Bits = 0;
    pThis->u.ParkMiller.cBits = 0;
    *phRand = pThis;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTRandAdvCreateParkMiller);

