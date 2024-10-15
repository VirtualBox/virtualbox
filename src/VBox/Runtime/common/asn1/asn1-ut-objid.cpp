/* $Id: asn1-ut-objid.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, OBJECT IDENTIFIER Type.
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

#include <iprt/alloca.h>
#include <iprt/bignum.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/uni.h>

#include <iprt/formats/asn1.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static char const       g_szDefault[] = "2.16.840.1.113894";
static uint32_t const   g_auDefault[] = { 2, 16, 840, 1, 113894 };
static uint8_t const    g_abDefault[] =
{
    2*40 + 16,  0x80 | (840 >> 7), 840 & 0x7f,  1,  0x80 | (113894 >> 14), 0x80 | ((113894 >> 7) & 0x7f), 113894 & 0x7f
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLHIDDEN(int) rtAsn1ObjId_InternalFormatComponent(uint32_t uValue, char **ppszObjId, size_t *pcbObjId); /* asn1-ut-objid.cpp */
/** @todo check if we really need this. */



/*
 * ASN.1 OBJECT IDENTIFIER - Special Methods.
 */

/**
 * Encodes the ASN.1 byte sequence for a set of components.
 *
 * @returns IPRT status code.
 * @param   cComponents     The number of components. Must be at least two.
 * @param   pauComponents   The components array.
 * @param   pbEncoded       The output buffer.
 * @param   pcbEncoded      On input, this holds the size of the output buffer.
 *                          On successful return it's the encoded size in bytes.
 */
static int rtAsn1ObjId_EncodeComponents(uint32_t cComponents, uint32_t const *pauComponents,
                                        uint8_t *pbEncoded, uint32_t *pcbEncoded)
{
    uint8_t *pbCur  = pbEncoded;
    uint32_t cbLeft = *pcbEncoded;

    /* The first two componets are encoded together to save a byte, so the loop
       organization is a little special. */
    AssertReturn(cComponents >= 2, VERR_ASN1_INTERNAL_ERROR_1);
    AssertReturn(pauComponents[0] <= 2, VERR_ASN1_INTERNAL_ERROR_1);
    AssertReturn(pauComponents[1] <= (pauComponents[0] < 2 ? 39 : UINT32_MAX - 80), VERR_ASN1_INTERNAL_ERROR_1);
    uint32_t i = 1;
    uint32_t uValue = pauComponents[0] * 40 + pauComponents[1];

    for (;;)
    {
        if (uValue < 0x80)
        {
            if (RT_UNLIKELY(cbLeft < 1))
                return VERR_BUFFER_OVERFLOW;
            cbLeft  -= 1;
            *pbCur++ = (uint8_t)uValue;
        }
        else if (uValue < 0x4000)
        {
            if (RT_UNLIKELY(cbLeft < 2))
                return VERR_BUFFER_OVERFLOW;
            cbLeft  -= 2;
            pbCur[0] = (uValue >> 7) | 0x80;
            pbCur[1] = uValue & 0x7f;
            pbCur   += 2;
        }
        else if (uValue < 0x200000)
        {
            if (RT_UNLIKELY(cbLeft < 3))
                return VERR_BUFFER_OVERFLOW;
            cbLeft  -= 3;
            pbCur[0] =  (uValue >> 14)         | 0x80;
            pbCur[1] = ((uValue >>  7) & 0x7f) | 0x80;
            pbCur[2] = uValue & 0x7f;
            pbCur   += 3;
        }
        else if (uValue < 0x10000000)
        {
            if (RT_UNLIKELY(cbLeft < 4))
                return VERR_BUFFER_OVERFLOW;
            cbLeft  -= 4;
            pbCur[0] =  (uValue >> 21)         | 0x80;
            pbCur[1] = ((uValue >> 14) & 0x7f) | 0x80;
            pbCur[2] = ((uValue >>  7) & 0x7f) | 0x80;
            pbCur[3] = uValue & 0x7f;
            pbCur   += 4;
        }
        else
        {
            if (RT_UNLIKELY(cbLeft < 5))
                return VERR_BUFFER_OVERFLOW;
            cbLeft  -= 5;
            pbCur[0] =  (uValue >> 28)         | 0x80;
            pbCur[1] = ((uValue >> 21) & 0x7f) | 0x80;
            pbCur[2] = ((uValue >> 14) & 0x7f) | 0x80;
            pbCur[3] = ((uValue >>  7) & 0x7f) | 0x80;
            pbCur[4] = uValue & 0x7f;
            pbCur   += 5;
        }

        /* Advance / return. */
        i++;
        if (i >= cComponents)
        {
            *pcbEncoded = (uint32_t)(pbCur - pbEncoded);
            return VINF_SUCCESS;
        }
        uValue = pauComponents[i];
    }
}


RTDECL(int) RTAsn1ObjId_InitFromString(PRTASN1OBJID pThis, const char *pszObjId, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RT_ZERO(*pThis);

    /*
     * Check the string, counting the number of components and checking their validity.
     */
    size_t cbObjId = strlen(pszObjId) + 1;
    AssertReturn(cbObjId < sizeof(pThis->szObjId), VERR_ASN1_OBJID_TOO_LONG_STRING_FORM);

    const char *psz = pszObjId;

    /* Special checking of the first component. It has only three valid values: 0,1,2. */
    char ch = *psz++;
    if (RT_UNLIKELY(ch < '0' || ch > '2'))
        return VERR_ASN1_OBJID_INVALID_DOTTED_STRING;
    char const chFirst = ch;
    ch = *psz++;
    if (RT_UNLIKELY(ch != '.'))
        return VERR_ASN1_OBJID_INVALID_DOTTED_STRING;

    /* The 2nd component.  It the first is 0 or 1, it has a max of 39. */
    uint32_t cComponents = 1;
    if (chFirst < '2')
    {
        ch = *psz++;
        if (*psz == '.')
        {
            if (RT_UNLIKELY(!RT_C_IS_DIGIT(ch)))
                return VERR_ASN1_OBJID_INVALID_DOTTED_STRING;
        }
        else
        {
            if (RT_UNLIKELY(ch < '0' || ch > '3'))
                return VERR_ASN1_OBJID_INVALID_DOTTED_STRING;
            ch = *psz++;
            if (RT_UNLIKELY(!RT_C_IS_DIGIT(ch)))
                return VERR_ASN1_OBJID_INVALID_DOTTED_STRING;
            if (*psz != '.')
                return VERR_ASN1_OBJID_INVALID_DOTTED_STRING;
        }
        cComponents++;
    }
    else
        psz--;

    /* Subsequent components have max values of UINT32_MAX - 80. */
    while ((ch = *psz++) != '\0')
    {
        if (RT_UNLIKELY(ch != '.'))
            return VERR_ASN1_OBJID_INVALID_DOTTED_STRING;
        const char *pszStart = psz;

        /* Special treatment of the first digit. Need to make sure it isn't an
           unnecessary leading 0. */
        ch = *psz++;
        if (RT_UNLIKELY(!RT_C_IS_DIGIT(ch)))
            return VERR_ASN1_OBJID_INVALID_DOTTED_STRING;
        if (RT_UNLIKELY(ch == '0' && RT_C_IS_DIGIT(*psz)))
            return VERR_ASN1_OBJID_INVALID_DOTTED_STRING;

        /* The rest of the digits. */
        while ((ch = *psz) != '.' && ch != '\0')
        {
            if (RT_UNLIKELY(!RT_C_IS_DIGIT(ch)))
                return VERR_ASN1_OBJID_INVALID_DOTTED_STRING;
            psz++;
        }

        /* Check the value range. */
        if (RT_UNLIKELY(psz - pszStart >= 9))
            if (   psz - pszStart > 9
                || strncmp(pszStart, "4294967216", 9) >= 0) /* 2^32 - 80 */
                return VERR_ASN1_OBJID_INVALID_DOTTED_STRING;

        cComponents++;
    }

    if (RT_UNLIKELY(cComponents >= 128))
        return VERR_ASN1_OBJID_TOO_MANY_COMPONENTS;
    pThis->cComponents = (uint8_t)cComponents;

    /*
     * Find space for the component array, either at the unused end of szObjId
     * or on the heap.
     */
    int rc;
    RTAsn1MemInitAllocation(&pThis->Allocation, pAllocator);
#if 0 /** @todo breaks with arrays of ObjIds or structs containing them. They get resized and repositioned in memory, thus invalidating the pointer. Add recall-pointers callback, or just waste memory? Or maybe make all arrays pointer-arrays? */
    size_t cbLeft = sizeof(pThis->szObjId) - cbObjId;
    if (cbLeft >= cComponents * sizeof(uint32_t))
    {
        pThis->pauComponents = (uint32_t *)&pThis->szObjId[sizeof(pThis->szObjId) - cComponents * sizeof(uint32_t)];
        cbLeft -= cComponents * sizeof(uint32_t);
        rc = VINF_SUCCESS;
    }
    else
#endif
        rc = RTAsn1MemAllocZ(&pThis->Allocation, (void **)&pThis->pauComponents, cComponents * sizeof(uint32_t));
    if (RT_SUCCESS(rc))
    {
        /*
         * Fill the elements array.
         */
        uint32_t *pauComponents = (uint32_t *)pThis->pauComponents;
        rc  = VINF_SUCCESS;
        psz = pszObjId;
        for (uint32_t i = 0; i < cComponents; i++)
        {
            uint32_t uValue = 0;
            rc = RTStrToUInt32Ex(psz, (char **)&psz, 10, &uValue);
            if (rc == VWRN_TRAILING_CHARS)
            {
                pauComponents[i] = uValue;
                AssertBreakStmt(*psz == '.', rc = VERR_TRAILING_CHARS);
                psz++;
            }
            else if (rc == VINF_SUCCESS)
            {
                pauComponents[i] = uValue;
                Assert(*psz == '\0');
            }
            else if (RT_FAILURE(rc))
                break;
            else
            {
                rc = -rc;
                break;
            }
        }
        if (rc == VINF_SUCCESS && *psz == '\0')
        {
            /*
             * Initialize the core structure before we start on the encoded bytes.
             */
            RTAsn1Core_InitEx(&pThis->Asn1Core,
                              ASN1_TAG_OID,
                              ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                              &g_RTAsn1ObjId_Vtable,
                              RTASN1CORE_F_PRESENT | RTASN1CORE_F_PRIMITE_TAG_STRUCT);

            /*
             * Encode the value into the string buffer. This will NOT overflow
             * because the string representation is much less efficient than the
             * binary ASN.1 representation (base-10 + separators vs. base-128).
             */
            pThis->Asn1Core.cb = (uint32_t)cbObjId;
            rc = rtAsn1ObjId_EncodeComponents(cComponents, pThis->pauComponents,
                                              (uint8_t *)&pThis->szObjId[0], &pThis->Asn1Core.cb);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Now, find a place for the encoded bytes.  There might be
                 * enough room left in the szObjId for it if we're lucky.
                 */
#if 0 /** @todo breaks with arrays of ObjIds or structs containing them. They get resized and repositioned in memory, thus invalidating the pointer. Add recall-pointers callback, or just waste memory? Or maybe make all arrays pointer-arrays? */
                if (pThis->Asn1Core.cb >= cbLeft)
                    pThis->Asn1Core.uData.pv = memmove(&pThis->szObjId[cbObjId], &pThis->szObjId[0], pThis->Asn1Core.cb);
                else
#endif
                    rc = RTAsn1ContentDup(&pThis->Asn1Core, pThis->szObjId, pThis->Asn1Core.cb, pAllocator);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Finally, copy the dotted string.
                     */
                    memcpy(pThis->szObjId, pszObjId, cbObjId);
                    return VINF_SUCCESS;
                }
            }
            else
            {
                AssertMsgFailed(("%Rrc\n", rc));
                rc = VERR_ASN1_INTERNAL_ERROR_3;
            }
        }
        else
            rc = VERR_ASN1_OBJID_INVALID_DOTTED_STRING;
    }
    RT_ZERO(*pThis);
    return rc;
}


RTDECL(int) RTAsn1ObjId_SetFromString(PRTASN1OBJID pThis, const char *pszObjId, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RTAsn1ObjId_Delete(pThis);
    int rc = RTAsn1ObjId_InitFromString(pThis, pszObjId, pAllocator);
    if (RT_FAILURE(rc))
        RTAsn1ObjId_Init(pThis, pAllocator);
    return rc;
}


RTDECL(int) RTAsn1ObjId_CompareWithString(PCRTASN1OBJID pThis, const char *pszRight)
{
    return strcmp(pThis->szObjId, pszRight);
}


RTDECL(bool) RTAsn1ObjId_StartsWith(PCRTASN1OBJID pThis, const char *pszStartsWith)
{
    size_t cchStartsWith = strlen(pszStartsWith);
    return !strncmp(pThis->szObjId, pszStartsWith, cchStartsWith)
        && (   pszStartsWith[cchStartsWith] == '.'
            || pszStartsWith[cchStartsWith] == '\0');
}


RTDECL(uint8_t) RTAsn1ObjIdCountComponents(PCRTASN1OBJID pThis)
{
    return pThis->cComponents;
}


RTDECL(uint32_t) RTAsn1ObjIdGetComponentsAsUInt32(PCRTASN1OBJID pThis, uint8_t iComponent)
{
    if (iComponent < pThis->cComponents)
        return pThis->pauComponents[iComponent];
    return UINT32_MAX;
}


RTDECL(uint32_t) RTAsn1ObjIdGetLastComponentsAsUInt32(PCRTASN1OBJID pThis)
{
    return pThis->pauComponents[pThis->cComponents - 1];
}


/*
 * ASN.1 OBJECT IDENTIFIER - Standard Methods.
 */

RT_DECL_DATA_CONST(RTASN1COREVTABLE const) g_RTAsn1ObjId_Vtable =
{
    "RTAsn1ObjId",
    sizeof(RTASN1OBJID),
    ASN1_TAG_OID,
    ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
    0,
    (PFNRTASN1COREVTDTOR)RTAsn1ObjId_Delete,
    NULL,
    (PFNRTASN1COREVTCLONE)RTAsn1ObjId_Clone,
    (PFNRTASN1COREVTCOMPARE)RTAsn1ObjId_Compare,
    (PFNRTASN1COREVTCHECKSANITY)RTAsn1ObjId_CheckSanity,
    NULL,
    NULL
};


RTDECL(int) RTAsn1ObjId_Init(PRTASN1OBJID pThis, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RT_NOREF_PV(pAllocator);
    RTAsn1Core_InitEx(&pThis->Asn1Core,
                      ASN1_TAG_OID,
                      ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                      &g_RTAsn1ObjId_Vtable,
                      RTASN1CORE_F_PRESENT | RTASN1CORE_F_PRIMITE_TAG_STRUCT);
    pThis->Asn1Core.cb       = sizeof(g_abDefault);
    pThis->Asn1Core.uData.pv = (void *)&g_abDefault[0];
    pThis->cComponents       = RT_ELEMENTS(g_auDefault);
    pThis->pauComponents     = g_auDefault;
    AssertCompile(sizeof(g_szDefault) <= sizeof(pThis->szObjId));
    memcpy(pThis->szObjId, g_szDefault, sizeof(g_szDefault));
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1ObjId_Clone(PRTASN1OBJID pThis, PCRTASN1OBJID pSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertPtr(pSrc); AssertPtr(pThis); AssertPtr(pAllocator);
    RT_ZERO(*pThis);
    if (RTAsn1ObjId_IsPresent(pSrc))
    {
        AssertReturn(pSrc->Asn1Core.pOps == &g_RTAsn1ObjId_Vtable, VERR_INTERNAL_ERROR_3);

        /* Copy the dotted string representation. */
        size_t cbObjId = strlen(pSrc->szObjId) + 1;
        AssertReturn(cbObjId <= sizeof(pThis->szObjId), VERR_INTERNAL_ERROR_5);
        memcpy(pThis->szObjId, pSrc->szObjId, cbObjId);

        /* Copy the integer component array. Try fit it in the unused space of
           the dotted object string buffer. We place it at the end of the
           buffer as that is simple alignment wise and avoid wasting bytes that
           could be used to sequueze in the content bytes (see below). */
        int rc;
        RTAsn1MemInitAllocation(&pThis->Allocation, pAllocator);
        pThis->cComponents = pSrc->cComponents;
#if 0 /** @todo breaks with arrays of ObjIds or structs containing them. They get resized and repositioned in memory, thus invalidating the pointer. Add recall-pointers callback, or just waste memory? Or maybe make all arrays pointer-arrays? */
        size_t cbLeft = sizeof(pThis->szObjId);
        if (pSrc->cComponents * sizeof(uint32_t) <= cbLeft)
        {
            pThis->pauComponents = (uint32_t *)&pThis->szObjId[sizeof(pThis->szObjId) - pSrc->cComponents * sizeof(uint32_t)];
            memcpy((uint32_t *)pThis->pauComponents, pSrc->pauComponents, pSrc->cComponents * sizeof(uint32_t));
            cbLeft -= pSrc->cComponents * sizeof(uint32_t);
            rc = VINF_SUCCESS;
        }
        else
#endif
        {
            rc = RTAsn1MemDup(&pThis->Allocation, (void **)&pThis->pauComponents, pSrc->pauComponents,
                              pSrc->cComponents * sizeof(uint32_t));
        }
        if (RT_SUCCESS(rc))
        {
            /* See if we can fit the content value into the szObjId as well.
               It will follow immediately after the string as the component
               array is the end of the string buffer, when present. */
#if 0 /** @todo breaks with arrays of ObjIds or structs containing them. They get resized and repositioned in memory, thus invalidating the pointer. Add recall-pointers callback, or just waste memory? Or maybe make all arrays pointer-arrays? */
            uint32_t cbContent = pSrc->Asn1Core.cb;
            if (cbContent <= cbLeft)
            {
                rc = RTAsn1Core_CloneNoContent(&pThis->Asn1Core, &pSrc->Asn1Core);
                if (RT_SUCCESS(rc))
                {
                    pThis->Asn1Core.uData.pv = memcpy(&pThis->szObjId[cbObjId], pSrc->Asn1Core.uData.pv, cbContent);
                    return VINF_SUCCESS;
                }
            }
            else
#endif
            {
                rc = RTAsn1Core_CloneContent(&pThis->Asn1Core, &pSrc->Asn1Core, pAllocator);
                if (RT_SUCCESS(rc))
                    return VINF_SUCCESS;
            }
        }

        /* failed, clean up. */
        if (pThis->Allocation.cbAllocated)
            RTAsn1MemFree(&pThis->Allocation, (uint32_t *)pThis->pauComponents);
        RT_ZERO(*pThis);
        return rc;
    }
    return VINF_SUCCESS;
}


RTDECL(void) RTAsn1ObjId_Delete(PRTASN1OBJID pThis)
{
    if (   pThis
        && RTAsn1ObjId_IsPresent(pThis))
    {
        Assert(pThis->Asn1Core.pOps == &g_RTAsn1ObjId_Vtable);

        if (pThis->Allocation.cbAllocated)
            RTAsn1MemFree(&pThis->Allocation, (uint32_t *)pThis->pauComponents);
        RTAsn1ContentFree(&pThis->Asn1Core);
        RT_ZERO(*pThis);
    }
}


RTDECL(int) RTAsn1ObjId_Enum(PRTASN1OBJID pThis, PFNRTASN1ENUMCALLBACK pfnCallback, uint32_t uDepth, void *pvUser)
{
    RT_NOREF_PV(pThis); RT_NOREF_PV(pfnCallback); RT_NOREF_PV(uDepth); RT_NOREF_PV(pvUser);
    Assert(pThis && (!RTAsn1ObjId_IsPresent(pThis) || pThis->Asn1Core.pOps == &g_RTAsn1ObjId_Vtable));

    /* No children to enumerate. */
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1ObjId_Compare(PCRTASN1OBJID pLeft, PCRTASN1OBJID pRight)
{
    if (RTAsn1ObjId_IsPresent(pLeft))
    {
        if (RTAsn1ObjId_IsPresent(pRight))
        {
            uint8_t cComponents = RT_MIN(pLeft->cComponents, pRight->cComponents);
            for (uint32_t i = 0; i < cComponents; i++)
                if (pLeft->pauComponents[i] != pRight->pauComponents[i])
                    return pLeft->pauComponents[i] < pRight->pauComponents[i] ? -1 : 1;

            if (pLeft->cComponents == pRight->cComponents)
                return 0;
            return pLeft->cComponents < pRight->cComponents ? -1 : 1;
        }
        return 1;
    }
    return 0 - (int)RTAsn1ObjId_IsPresent(pRight);
}


RTDECL(int) RTAsn1ObjId_CheckSanity(PCRTASN1OBJID pThis, uint32_t fFlags, PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    RT_NOREF_PV(fFlags);
    if (RT_UNLIKELY(!RTAsn1ObjId_IsPresent(pThis)))
        return RTErrInfoSetF(pErrInfo, VERR_ASN1_NOT_PRESENT, "%s: Missing (OBJID).", pszErrorTag);
    return VINF_SUCCESS;
}


/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-objid-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-core.h>
#include <iprt/asn1-generator-init.h>
#include <iprt/asn1-generator-sanity.h>

