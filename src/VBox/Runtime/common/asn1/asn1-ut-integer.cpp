/* $Id: asn1-ut-integer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, INTEGER Type.
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
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/asn1.h>

#include <iprt/bignum.h>
#include <iprt/err.h>
#include <iprt/string.h>

#include <iprt/formats/asn1.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Fixed on-byte constants for small numbers.
 * Good for structure version values and such. */
static const uint8_t g_abSmall[] =
{
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
};



/*
 * ASN.1 INTEGER - Special Methods.
 */


/**
 * Updates the native value we keep in RTASN1INTEGER::uValue.
 *
 * @param   pThis               The integer.
 */
static void rtAsn1Integer_UpdateNativeValue(PRTASN1INTEGER pThis)
{
    uint32_t offLast = pThis->Asn1Core.cb - 1;
    switch (pThis->Asn1Core.cb)
    {
        default: AssertBreak(pThis->Asn1Core.cb > 8); /* paranoia */ RT_FALL_THRU();
        case 8: pThis->uValue.u |= (uint64_t)pThis->Asn1Core.uData.pu8[offLast - 7] << 56; RT_FALL_THRU();
        case 7: pThis->uValue.u |= (uint64_t)pThis->Asn1Core.uData.pu8[offLast - 6] << 48; RT_FALL_THRU();
        case 6: pThis->uValue.u |= (uint64_t)pThis->Asn1Core.uData.pu8[offLast - 5] << 40; RT_FALL_THRU();
        case 5: pThis->uValue.u |= (uint64_t)pThis->Asn1Core.uData.pu8[offLast - 4] << 32; RT_FALL_THRU();
        case 4: pThis->uValue.u |= (uint32_t)pThis->Asn1Core.uData.pu8[offLast - 3] << 24; RT_FALL_THRU();
        case 3: pThis->uValue.u |= (uint32_t)pThis->Asn1Core.uData.pu8[offLast - 2] << 16; RT_FALL_THRU();
        case 2: pThis->uValue.u |= (uint16_t)pThis->Asn1Core.uData.pu8[offLast - 1] <<  8; RT_FALL_THRU();
        case 1: pThis->uValue.u |=           pThis->Asn1Core.uData.pu8[offLast];
    }
}


RTDECL(int) RTAsn1Integer_InitU64(PRTASN1INTEGER pThis, uint64_t uValue, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    /*
     * Initialize the core and the native value.
     */
    RTAsn1Core_InitEx(&pThis->Asn1Core,
                      ASN1_TAG_INTEGER,
                      ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                      &g_RTAsn1Integer_Vtable,
                      RTASN1CORE_F_PRESENT | RTASN1CORE_F_PRIMITE_TAG_STRUCT);
    pThis->uValue.u = uValue;

    /*
     * Use one of the constants if possible.
     */
    if (uValue < RT_ELEMENTS(g_abSmall))
    {
        pThis->Asn1Core.cb = 1;
        pThis->Asn1Core.uData.pv = (void *)&g_abSmall[0];
    }
    else
    {
        /*
         * Need to turn uValue into a big endian number without any
         * unnecessary leading zero bytes.
         */
        /* Figure the size. */
        uint32_t cb = 0;
        if (uValue <= UINT32_MAX)
        {
            if (uValue <= UINT16_MAX)
            {
                if (uValue <= UINT8_MAX)
                    cb = 1;
                else
                    cb = 2;
            }
            else
            {
                if (uValue <= UINT32_C(0xffffff))
                    cb = 3;
                else
                    cb = 4;
            }
        }
        else
        {
            if (uValue <= UINT64_C(0x0000FfffFfffFfff))
            {
                if (uValue <= UINT64_C(0x000000ffFfffFfff))
                    cb = 5;
                else
                    cb = 6;
            }
            else
            {
                if (uValue <= UINT64_C(0x00ffFfffFfffFfff))
                    cb = 7;
                else
                    cb = 8;
            }
        }

        /* Allocate space. */
        int rc = RTAsn1ContentAllocZ(&pThis->Asn1Core, cb, pAllocator);
        if (RT_FAILURE(rc))
        {
            RT_ZERO(*pThis);
            return rc;
        }

        /* Serialize the number in MSB order. */
        uint8_t *pb = (uint8_t *)pThis->Asn1Core.uData.pu8;
        while (cb-- > 0)
        {
            pb[cb] = (uint8_t)uValue;
            uValue >>= 8;
        }
        Assert(uValue == 0);
    }
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Integer_InitDefault(PRTASN1INTEGER pThis, uint64_t uValue, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    int rc = RTAsn1Integer_InitU64(pThis, uValue, pAllocator);
    if (RT_SUCCESS(rc))
    {
        pThis->Asn1Core.fFlags &= ~RTASN1CORE_F_PRESENT;
        pThis->Asn1Core.fFlags |= RTASN1CORE_F_DEFAULT;
    }
    return rc;
}


RTDECL(int32_t) RTAsn1Integer_UnsignedLastBit(PCRTASN1INTEGER pThis)
{
    AssertReturn(pThis->Asn1Core.fFlags, -1);
    uint8_t const *pb = pThis->Asn1Core.uData.pu8;
    AssertReturn(pb, -1);
    uint32_t cb = pThis->Asn1Core.cb;
    AssertReturn(pThis->Asn1Core.cb < (uint32_t)INT32_MAX / 8, -1);

    while (cb-- > 0)
    {
        uint8_t b = *pb++;
        if (b)
        {
            int32_t iRet = cb * 8;
            if (b & 0x80)       iRet += 7;
            else if (b & 0x40)  iRet += 6;
            else if (b & 0x20)  iRet += 5;
            else if (b & 0x10)  iRet += 4;
            else if (b & 0x08)  iRet += 3;
            else if (b & 0x04)  iRet += 2;
            else if (b & 0x02)  iRet += 1;
            else Assert(b == 0x01);
            return iRet;
        }
    }
    return -1;
}


RTDECL(int) RTAsn1Integer_UnsignedCompare(PCRTASN1INTEGER pLeft, PCRTASN1INTEGER pRight)
{
    Assert(pLeft  && (!RTAsn1Integer_IsPresent(pLeft)  || pLeft->Asn1Core.pOps  == &g_RTAsn1Integer_Vtable));
    Assert(pRight && (!RTAsn1Integer_IsPresent(pRight) || pRight->Asn1Core.pOps == &g_RTAsn1Integer_Vtable));

    int iDiff;
    if (RTAsn1Integer_IsPresent(pLeft))
    {
        if (RTAsn1Integer_IsPresent(pRight))
        {
            if (   pLeft->Asn1Core.cb > 8
                || pRight->Asn1Core.cb > 8)
            {
                uint32_t iLeft  = RTAsn1Integer_UnsignedLastBit(pLeft);
                uint32_t iRight = RTAsn1Integer_UnsignedLastBit(pRight);
                if (iLeft != iRight)
                    return iLeft < iRight ? -1 : 1;
                if ((int32_t)iLeft < 0)
                    return 0; /* Both are all zeros. */

                uint32_t i = iLeft / 8;
                if (i > 8)
                {
                    uint8_t const *pbLeft  = &pLeft->Asn1Core.uData.pu8[pLeft->Asn1Core.cb - i - 1];
                    uint8_t const *pbRight = &pRight->Asn1Core.uData.pu8[pRight->Asn1Core.cb - i - 1];
                    for (;;)
                    {
                        if (*pbLeft != *pbRight)
                            return *pbLeft < *pbRight ? -1 : 1;
                        if (--i <= 8)
                            break;
                        pbLeft++;
                        pbRight++;
                    }
                }
            }

            if (pLeft->uValue.u == pRight->uValue.u)
                iDiff = 0;
            else
                iDiff = pLeft->uValue.u < pRight->uValue.u ? -1 : 1;
        }
        else
            iDiff = -1;
    }
    else
        iDiff = 0 - (int)RTAsn1Integer_IsPresent(pRight);
    return iDiff;
}


RTDECL(int) RTAsn1Integer_UnsignedCompareWithU64(PCRTASN1INTEGER pThis, uint64_t u64Const)
{
    int iDiff;
    if (RTAsn1Integer_IsPresent(pThis))
    {
        if (pThis->Asn1Core.cb > 8)
        {
            int32_t iLast = RTAsn1Integer_UnsignedLastBit(pThis);
            if (iLast >= 64)
                return 1;
        }

        if (pThis->uValue.u == u64Const)
            iDiff = 0;
        else
            iDiff = pThis->uValue.u < u64Const ? -1 : 1;
    }
    else
        iDiff = 1;
    return iDiff;
}


RTDECL(int) RTAsn1Integer_UnsignedCompareWithU32(PCRTASN1INTEGER pThis, uint32_t u32Const)
{
    int iDiff;
    if (RTAsn1Integer_IsPresent(pThis))
    {
        if (pThis->Asn1Core.cb > 8)
        {
            int32_t iLast = RTAsn1Integer_UnsignedLastBit(pThis);
            if (iLast >= 32)
                return 1;
        }

        if (pThis->uValue.u == u32Const)
            iDiff = 0;
        else
            iDiff = pThis->uValue.u < u32Const ? -1 : 1;
    }
    else
        iDiff = 1;
    return iDiff;
}


RTDECL(int) RTAsn1Integer_ToBigNum(PCRTASN1INTEGER pThis, PRTBIGNUM pBigNum, uint32_t fBigNumInit)
{
    AssertReturn(!(fBigNumInit & ~(  RTBIGNUMINIT_F_SENSITIVE | RTBIGNUMINIT_F_UNSIGNED | RTBIGNUMINIT_F_SIGNED
                                   | RTBIGNUMINIT_F_ENDIAN_LITTLE | RTBIGNUMINIT_F_ENDIAN_BIG)),
                 VERR_INVALID_PARAMETER);
    AssertReturn(RTAsn1Integer_IsPresent(pThis), VERR_INVALID_PARAMETER);

    if (!(fBigNumInit & (RTBIGNUMINIT_F_UNSIGNED | RTBIGNUMINIT_F_SIGNED)))
        fBigNumInit |= RTBIGNUMINIT_F_SIGNED;

    if (!(fBigNumInit & (RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_ENDIAN_LITTLE)))
        fBigNumInit |= RTBIGNUMINIT_F_ENDIAN_BIG;

    return RTBigNumInit(pBigNum, fBigNumInit, pThis->Asn1Core.uData.pv, pThis->Asn1Core.cb);
}


RTDECL(int) RTAsn1Integer_FromBigNum(PRTASN1INTEGER pThis, PCRTBIGNUM pBigNum, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertPtr(pThis); AssertPtr(pBigNum); AssertPtr(pAllocator);

    /* Be nice and auto init the object. */
    if (!RTAsn1Integer_IsPresent(pThis))
        RTAsn1Integer_Init(pThis, NULL);

    uint32_t cb = RTBigNumByteWidth(pBigNum); Assert(cb > 0);
    int rc = RTAsn1ContentReallocZ(&pThis->Asn1Core, cb, pAllocator);
    if (RT_SUCCESS(rc))
    {
        Assert(cb == pThis->Asn1Core.cb);
        rc = RTBigNumToBytesBigEndian(pBigNum, (void *)pThis->Asn1Core.uData.pv, cb);
        if (RT_SUCCESS(rc))
            rtAsn1Integer_UpdateNativeValue(pThis);
    }
    return rc;
}


RTDECL(int) RTAsn1Integer_ToString(PCRTASN1INTEGER pThis, char *pszBuf, size_t cbBuf, uint32_t fFlags, size_t *pcbActual)
{
    AssertReturn(RTAsn1Integer_IsPresent(pThis), VERR_INVALID_PARAMETER);
    AssertReturn(fFlags == 0, VERR_INVALID_FLAGS);

    /*
     * We only do hex conversions via this API.
     * Currently we consider all numbers to be unsigned.
     */
    /** @todo Signed ASN.1 INTEGER. */
    int rc;
    size_t cbActual;
    if (pThis->Asn1Core.cb <= 8)
    {
        cbActual = 2 + pThis->Asn1Core.cb*2 + 1;
        if (cbActual <= cbBuf)
        {
            ssize_t cchFormat = RTStrFormatU64(pszBuf, cbBuf, pThis->uValue.u, 16, (int)cbActual - 1 /*cchWidth*/, 0,
                                               RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD);
            rc = VINF_SUCCESS;
            AssertStmt(cchFormat == (ssize_t)cbActual - 1, rc = VERR_INTERNAL_ERROR_3);
        }
        else
            rc = VERR_BUFFER_OVERFLOW;
    }
    else
    {
        cbActual = pThis->Asn1Core.cb * 3 - 1 /* save one separator */ + 1 /* terminator */;
        if (cbActual <= cbBuf)
        {
            rc = RTStrPrintHexBytes(pszBuf, cbBuf, pThis->Asn1Core.uData.pv, pThis->Asn1Core.cb, RTSTRPRINTHEXBYTES_F_SEP_SPACE);
            Assert(rc == VINF_SUCCESS);
        }
        else
            rc = VERR_BUFFER_OVERFLOW;
    }
    if (pcbActual)
        *pcbActual = cbActual;
    return rc;
}


/*
 * ASN.1 INTEGER - Standard Methods.
 */

RT_DECL_DATA_CONST(RTASN1COREVTABLE const) g_RTAsn1Integer_Vtable =
{
    "RTAsn1Integer",
    sizeof(RTASN1INTEGER),
    ASN1_TAG_INTEGER,
    ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
    0,
    (PFNRTASN1COREVTDTOR)RTAsn1Integer_Delete,
    NULL,
    (PFNRTASN1COREVTCLONE)RTAsn1Integer_Clone,
    (PFNRTASN1COREVTCOMPARE)RTAsn1Integer_Compare,
    (PFNRTASN1COREVTCHECKSANITY)RTAsn1Integer_CheckSanity,
    NULL,
    NULL
};


RTDECL(int) RTAsn1Integer_Init(PRTASN1INTEGER pThis, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RT_NOREF_PV(pAllocator);
    RTAsn1Core_InitEx(&pThis->Asn1Core,
                      ASN1_TAG_INTEGER,
                      ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                      &g_RTAsn1Integer_Vtable,
                      RTASN1CORE_F_PRESENT | RTASN1CORE_F_PRIMITE_TAG_STRUCT);
    pThis->uValue.u = 1;
    pThis->Asn1Core.cb = 1;
    pThis->Asn1Core.uData.pv = (void *)&g_abSmall[0];
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Integer_Clone(PRTASN1INTEGER pThis, PCRTASN1INTEGER pSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertPtr(pSrc); AssertPtr(pThis); AssertPtr(pAllocator);
    RT_ZERO(*pThis);
    if (RTAsn1Integer_IsPresent(pSrc))
    {
        AssertReturn(pSrc->Asn1Core.pOps == &g_RTAsn1Integer_Vtable, VERR_INTERNAL_ERROR_3);

        int rc;
        if (   pSrc->Asn1Core.cb != 1
            || pSrc->uValue.u >= RT_ELEMENTS(g_abSmall))
        {
            /* Value is too large, copy it. */
            rc = RTAsn1Core_CloneContent(&pThis->Asn1Core, &pSrc->Asn1Core, pAllocator);
            if (RT_FAILURE(rc))
                return rc;
        }
        else
        {
            /* Use one of the const values. */
            rc = RTAsn1Core_CloneNoContent(&pThis->Asn1Core, &pSrc->Asn1Core);
            if (RT_FAILURE(rc))
                return rc;
            Assert(g_abSmall[pSrc->uValue.u] == pSrc->uValue.u);
            pThis->Asn1Core.uData.pv = (void *)&g_abSmall[pSrc->uValue.u];
        }
        pThis->uValue.u = pSrc->uValue.u;
    }
    return VINF_SUCCESS;
}


RTDECL(void) RTAsn1Integer_Delete(PRTASN1INTEGER pThis)
{
    if (   pThis
        && RTAsn1Integer_IsPresent(pThis))
    {
        Assert(pThis->Asn1Core.pOps == &g_RTAsn1Integer_Vtable);

        RTAsn1ContentFree(&pThis->Asn1Core);
        RT_ZERO(*pThis);
    }
}


RTDECL(int) RTAsn1Integer_Enum(PRTASN1INTEGER pThis, PFNRTASN1ENUMCALLBACK pfnCallback, uint32_t uDepth, void *pvUser)
{
    RT_NOREF_PV(pThis); RT_NOREF_PV(pfnCallback); RT_NOREF_PV(uDepth); RT_NOREF_PV(pvUser);
    Assert(pThis && (!RTAsn1Integer_IsPresent(pThis) || pThis->Asn1Core.pOps == &g_RTAsn1Integer_Vtable));

    /* No children to enumerate. */
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Integer_Compare(PCRTASN1INTEGER pLeft, PCRTASN1INTEGER pRight)
{
    return RTAsn1Integer_UnsignedCompare(pLeft, pRight);
}


RTDECL(int) RTAsn1Integer_CheckSanity(PCRTASN1INTEGER pThis, uint32_t fFlags, PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    RT_NOREF_PV(fFlags);
    if (RT_UNLIKELY(!RTAsn1Integer_IsPresent(pThis)))
        return RTErrInfoSetF(pErrInfo, VERR_ASN1_NOT_PRESENT, "%s: Missing (INTEGER).", pszErrorTag);
    return VINF_SUCCESS;
}


/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-integer-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-core.h>
#include <iprt/asn1-generator-init.h>
#include <iprt/asn1-generator-sanity.h>

