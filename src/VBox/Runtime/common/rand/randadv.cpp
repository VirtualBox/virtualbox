/* $Id: randadv.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Random Numbers, Generic Glue.
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

#include <iprt/mem.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include "internal/magics.h"
#include "internal/rand.h"


RTDECL(int) RTRandAdvDestroy(RTRAND hRand) RT_NO_THROW_DEF
{
    /* Validate. */
    if (hRand == NIL_RTRAND)
        return VINF_SUCCESS;
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, VERR_INVALID_HANDLE);

    /* forward the call */
    return pThis->pfnDestroy(pThis);
}
RT_EXPORT_SYMBOL(RTRandAdvDestroy);


RTDECL(int) RTRandAdvSeed(RTRAND hRand, uint64_t u64Seed) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, VERR_INVALID_HANDLE);

    /* forward the call */
    return pThis->pfnSeed(pThis, u64Seed);
}
RT_EXPORT_SYMBOL(RTRandAdvSeed);


RTDECL(int) RTRandAdvSaveState(RTRAND hRand, char *pszState, size_t *pcbState) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrNull(pszState);
    AssertPtr(pcbState);

    /* forward the call */
    return pThis->pfnSaveState(pThis, pszState, pcbState);
}
RT_EXPORT_SYMBOL(RTRandAdvSaveState);


RTDECL(int) RTRandAdvRestoreState(RTRAND hRand, char const *pszState) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pszState);

    /* forward the call */
    return pThis->pfnRestoreState(pThis, pszState);
}
RT_EXPORT_SYMBOL(RTRandAdvRestoreState);


RTDECL(void) RTRandAdvBytes(RTRAND hRand, void *pv, size_t cb) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTRANDINT_MAGIC);
    AssertPtr(pv);

    /* forward the call */
    return pThis->pfnGetBytes(pThis, (uint8_t *)pv, cb);
}
RT_EXPORT_SYMBOL(RTRandAdvBytes);


RTDECL(int32_t) RTRandAdvS32Ex(RTRAND hRand, int32_t i32First, int32_t i32Last) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, INT32_MAX);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, INT32_MAX);

    /* wrap the call */
    return pThis->pfnGetU32(pThis, 0, i32Last - i32First) + i32First;
}
RT_EXPORT_SYMBOL(RTRandAdvS32Ex);


RTDECL(int32_t) RTRandAdvS32(RTRAND hRand) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, INT32_MAX);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, INT32_MAX);

    /* wrap the call */
    return pThis->pfnGetU32(pThis, 0, UINT32_MAX) + INT32_MAX;
}
RT_EXPORT_SYMBOL(RTRandAdvS32);


RTDECL(uint32_t) RTRandAdvU32Ex(RTRAND hRand, uint32_t u32First, uint32_t u32Last) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, UINT32_MAX);

    /* forward the call */
    return pThis->pfnGetU32(pThis, u32First, u32Last);
}
RT_EXPORT_SYMBOL(RTRandAdvU32Ex);


RTDECL(uint32_t) RTRandAdvU32(RTRAND hRand) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, UINT32_MAX);

    /* forward the call */
    return pThis->pfnGetU32(pThis, 0, UINT32_MAX);
}
RT_EXPORT_SYMBOL(RTRandAdvU32);


RTDECL(int64_t) RTRandAdvS64Ex(RTRAND hRand, int64_t i64First, int64_t i64Last) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, INT64_MAX);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, INT64_MAX);

    /* wrap the call */
    return pThis->pfnGetU64(pThis, 0, i64Last - i64First) + i64First;
}
RT_EXPORT_SYMBOL(RTRandAdvS64Ex);


RTDECL(int64_t) RTRandAdvS64(RTRAND hRand) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, INT64_MAX);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, INT64_MAX);

    /* wrap the call */
    return pThis->pfnGetU64(pThis, 0, UINT64_MAX) + INT64_MAX;
}
RT_EXPORT_SYMBOL(RTRandAdvS64);


RTDECL(uint64_t) RTRandAdvU64Ex(RTRAND hRand, uint64_t u64First, uint64_t u64Last) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, UINT64_MAX);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, UINT64_MAX);

    /* forward the call */
    return pThis->pfnGetU64(pThis, u64First, u64Last);
}
RT_EXPORT_SYMBOL(RTRandAdvU64Ex);


RTDECL(uint64_t) RTRandAdvU64(RTRAND hRand) RT_NO_THROW_DEF
{
    /* Validate. */
    PRTRANDINT pThis = hRand;
    AssertPtrReturn(pThis, UINT64_MAX);
    AssertReturn(pThis->u32Magic == RTRANDINT_MAGIC, UINT64_MAX);

    /* forward the call */
    return pThis->pfnGetU64(pThis, 0, UINT64_MAX);
}
RT_EXPORT_SYMBOL(RTRandAdvU64);


DECL_HIDDEN_CALLBACK(void)  rtRandAdvSynthesizeBytesFromU32(PRTRANDINT pThis, uint8_t *pb, size_t cb)
{
    while (cb > 0)
    {
        uint32_t u32 = pThis->pfnGetU32(pThis, 0, UINT32_MAX);
        switch (cb)
        {
            case 4:
                pb[3] = (uint8_t)(u32 >> 24); RT_FALL_THRU();
            case 3:
                pb[2] = (uint8_t)(u32 >> 16); RT_FALL_THRU();
            case 2:
                pb[1] = (uint8_t)(u32 >> 8);  RT_FALL_THRU();
            case 1:
                pb[0] = (uint8_t)u32;
            return; /* done */

            default:
                pb[0] = (uint8_t)u32;
                pb[1] = (uint8_t)(u32 >> 8);
                pb[2] = (uint8_t)(u32 >> 16);
                pb[3] = (uint8_t)(u32 >> 24);
                break;
        }

        /* advance */
        cb -= 4;
        pb += 4;
    }
}


DECL_HIDDEN_CALLBACK(void)  rtRandAdvSynthesizeBytesFromU64(PRTRANDINT pThis, uint8_t *pb, size_t cb)
{
    while (cb > 0)
    {
        uint64_t u64 = pThis->pfnGetU64(pThis, 0, UINT64_MAX);
        switch (cb)
        {
            case 8:
                pb[7] = (uint8_t)(u64 >> 56); RT_FALL_THRU();
            case 7:
                pb[6] = (uint8_t)(u64 >> 48); RT_FALL_THRU();
            case 6:
                pb[5] = (uint8_t)(u64 >> 40); RT_FALL_THRU();
            case 5:
                pb[4] = (uint8_t)(u64 >> 32); RT_FALL_THRU();
            case 4:
                pb[3] = (uint8_t)(u64 >> 24); RT_FALL_THRU();
            case 3:
                pb[2] = (uint8_t)(u64 >> 16); RT_FALL_THRU();
            case 2:
                pb[1] = (uint8_t)(u64 >> 8);  RT_FALL_THRU();
            case 1:
                pb[0] = (uint8_t)u64;
            return; /* done */

            default:
                pb[0] = (uint8_t)u64;
                pb[1] = (uint8_t)(u64 >> 8);
                pb[2] = (uint8_t)(u64 >> 16);
                pb[3] = (uint8_t)(u64 >> 24);
                pb[4] = (uint8_t)(u64 >> 32);
                pb[5] = (uint8_t)(u64 >> 40);
                pb[6] = (uint8_t)(u64 >> 48);
                pb[7] = (uint8_t)(u64 >> 56);
                break;
        }

        /* advance */
        cb -= 8;
        pb += 8;
    }
}


DECL_HIDDEN_CALLBACK(uint32_t)  rtRandAdvSynthesizeU32FromBytes(PRTRANDINT pThis, uint32_t u32First, uint32_t u32Last)
{
    union
    {
        uint32_t    off;
        uint8_t     ab[5];
    } u;

    const uint32_t offLast = u32Last - u32First;
    if (offLast == UINT32_MAX)
        /* get 4 random bytes and return them raw. */
        pThis->pfnGetBytes(pThis, &u.ab[0], sizeof(u.off));
    else if (!(offLast & UINT32_C(0xf0000000)))
    {
        /* get 4 random bytes and do simple squeeze. */
        pThis->pfnGetBytes(pThis, &u.ab[0], sizeof(u.off));
        u.off %= offLast + 1;
        u.off += u32First;
    }
    else
    {
        /* get 5 random bytes and do shifted squeeze. (this ain't perfect) */
        pThis->pfnGetBytes(pThis, &u.ab[0], sizeof(u.ab));
        u.off %= (offLast >> 4) + 1;
        u.off <<= 4;
        u.off |= u.ab[4] & 0xf;
        if (u.off > offLast)
            u.off = offLast;
        u.off += u32First;
    }
    return u.off;
}


DECL_HIDDEN_CALLBACK(uint32_t)  rtRandAdvSynthesizeU32FromU64(PRTRANDINT pThis, uint32_t u32First, uint32_t u32Last)
{
    return (uint32_t)pThis->pfnGetU64(pThis, u32First, u32Last);
}


DECL_HIDDEN_CALLBACK(uint64_t)  rtRandAdvSynthesizeU64FromBytes(PRTRANDINT pThis, uint64_t u64First, uint64_t u64Last)
{
    union
    {
        uint64_t    off;
        uint32_t    off32;
        uint8_t     ab[9];
    } u;

    const uint64_t offLast = u64Last - u64First;
    if (offLast == UINT64_MAX)
        /* get 8 random bytes and return them raw. */
        pThis->pfnGetBytes(pThis, &u.ab[0], sizeof(u.off));
    else if (!(offLast & UINT64_C(0xf000000000000000)))
    {
        /* get 8 random bytes and do simple squeeze. */
        pThis->pfnGetBytes(pThis, &u.ab[0], sizeof(u.off));
        u.off %= offLast + 1;
        u.off += u64First;
    }
    else
    {
        /* get 9 random bytes and do shifted squeeze. (this ain't perfect) */
        pThis->pfnGetBytes(pThis, &u.ab[0], sizeof(u.ab));
        u.off %= (offLast >> 4) + 1;
        u.off <<= 4;
        u.off |= u.ab[8] & 0xf;
        if (u.off > offLast)
            u.off = offLast;
        u.off += u64First;
    }
    return u.off;
}


DECL_HIDDEN_CALLBACK(uint64_t)  rtRandAdvSynthesizeU64FromU32(PRTRANDINT pThis, uint64_t u64First, uint64_t u64Last)
{
    uint64_t off = u64Last - u64First;
    if (off <= UINT32_MAX)
        return (uint64_t)pThis->pfnGetU32(pThis, 0, (uint32_t)off) + u64First;

    return (    (uint64_t)pThis->pfnGetU32(pThis, 0, UINT32_MAX)
            |  ((uint64_t)pThis->pfnGetU32(pThis, 0, (uint32_t)(off >> 32)) << 32))
         + u64First;
}


/** @copydoc RTRANDINT::pfnSeed */
DECL_HIDDEN_CALLBACK(int) rtRandAdvStubSeed(PRTRANDINT pThis, uint64_t u64Seed)
{
    NOREF(pThis);
    NOREF(u64Seed);
    return VERR_NOT_SUPPORTED;
}


/** @copydoc RTRANDINT::pfnSaveState */
DECL_HIDDEN_CALLBACK(int) rtRandAdvStubSaveState(PRTRANDINT pThis, char *pszState, size_t *pcbState)
{
    NOREF(pThis);
    NOREF(pszState);
    NOREF(pcbState);
    return VERR_NOT_SUPPORTED;
}


/** @copydoc RTRANDINT::pfnRestoreState */
DECL_HIDDEN_CALLBACK(int) rtRandAdvStubRestoreState(PRTRANDINT pThis, char const *pszState)
{
    NOREF(pThis);
    NOREF(pszState);
    return VERR_NOT_SUPPORTED;
}


/** @copydoc RTRANDINT::pfnDestroy */
DECL_HIDDEN_CALLBACK(int) rtRandAdvDefaultDestroy(PRTRANDINT pThis)
{
    pThis->u32Magic = ~RTRANDINT_MAGIC;
    RTMemFree(pThis);
    return VINF_SUCCESS;
}

