/* $Id: asn1-ut-objid-decode.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, OBJECT IDENTIFIER Type, Decoder.
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
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/ctype.h>

#include <iprt/formats/asn1.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static char const g_achDigits[11] = "0123456789";


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLHIDDEN(int) rtAsn1ObjId_InternalFormatComponent(uint32_t uValue, char **ppszObjId, size_t *pcbObjId); /* asn1-ut-objid.cpp */


/**
 * Internal worker for RTAsn1ObjId_DecodeAsn1 that formats a component, with a
 * leading dot.
 *
 * @returns VBox status code (caller complains on failure).
 * @param   uValue      The component ID value.
 * @param   ppszObjId   Pointer to the output buffer pointer.
 * @param   pcbObjId    Pointer to the remaining size of the output buffer.
 */
DECLHIDDEN(int) rtAsn1ObjId_InternalFormatComponent(uint32_t uValue, char **ppszObjId, size_t *pcbObjId)
{
    /*
     * Format the number backwards.
     */
    char szTmp[32];
    char *psz = &szTmp[sizeof(szTmp) - 1];
    *psz = '\0';

    do
    {
        *--psz = g_achDigits[uValue % 10];
        uValue /= 10;
    } while (uValue > 0);

    /*
     * Do we have enough space?
     * We add a dot and save space for the terminator.
     */
    size_t cchNeeded = &szTmp[sizeof(szTmp) - 1] - psz;
    if (1 + cchNeeded < *pcbObjId)
    {
        *pcbObjId  -= cchNeeded + 1;
        char *pszObjId = *ppszObjId;
        *ppszObjId = pszObjId + cchNeeded + 1;

        *pszObjId = '.';
        memcpy(pszObjId + 1, psz, cchNeeded);
        return VINF_SUCCESS;
    }

    AssertFailed();
    return VERR_ASN1_OBJID_TOO_LONG_STRING_FORM;
}


/**
 * Reads one object ID component, returning it's value and encoded length.
 *
 * @returns The encoded length (positive) on success, negative IPRT status code
 *          on failure.
 * @param   pbContent           The start of the component to parse.
 * @param   cbContent           The number of content bytes left.
 * @param   puValue             Where to return the value.
 */
static int rtAsn1ObjId_ReadComponent(uint8_t const *pbContent, uint32_t cbContent, uint32_t *puValue)
{
    if (cbContent >= 1)
    {
        /* The first byte. */
        uint8_t b = *pbContent;
        if (!(b & 0x80))
        {
            *puValue = b;
            return 1;
        }

        /* Encoded as more than one byte.  Make sure that it's efficently
           encoded as 8.19.2 indicates it must. */
        if (b != 0x80)
        {
            uint32_t off    = 1;
            uint32_t uValue = b & 0x7f;
            while (off < cbContent)
            {
                b = pbContent[off++];
                uValue <<= 7;
                uValue |= b & 0x7f;
                if (!(b & 0x80))
                {
                    *puValue = uValue;
                    return (int)off;
                }

                if (RT_UNLIKELY(uValue & UINT32_C(0x0e000000)))
                    return VERR_ASN1_OBJID_COMPONENT_TOO_BIG;
            }
        }
        return VERR_ASN1_INVALID_OBJID_ENCODING;
    }
    return VERR_NO_DATA;
}


/**
 * This function parses the binary content of an OBJECT IDENTIFIER, check the
 * encoding as well as calculating the storage requirements.
 *
 * @returns IPRT status code
 * @param   pbContent       Pointer to the content.
 * @param   cbContent       The length of the content.
 * @param   pCursor         The cursor (for error reporting).
 * @param   pszErrorTag     The error tag.
 * @param   pcComponents    Where to return the component count.
 * @param   pcchObjId       Where to return the length of the dotted string
 *                          representation.
 */
static int rtAsn1ObjId_PreParse(uint8_t const *pbContent, uint32_t cbContent, PRTASN1CURSOR pCursor, const char *pszErrorTag,
                                uint8_t *pcComponents, uint8_t *pcchObjId)
{
    int rc;
    if (cbContent >= 1 && cbContent < _1K)
    {
        /*
         * Decode the first two numbers.  Monkey business: X*40 + Y
         * Where X is the first number, X in {0,1,2}, and Y is the second
         * one.  The range of Y is {0,...,39} for X in {0,1}, but has a
         * free range for X = 2.
         */
        uint32_t cComponents = 1;
        uint32_t uValue;
        rc = rtAsn1ObjId_ReadComponent(pbContent, cbContent, &uValue);
        if (rc > 0)
        {
            uint32_t cchObjId = 1;
            uValue = uValue < 2*40 ? uValue % 40 : uValue - 2*40; /* Y */
            do
            {
                cComponents++;

                /* Figure the encoded string length, binary search fashion. */
                if (uValue < 10000)
                {
                    if (uValue < 100)
                    {
                        if (uValue < 10)
                            cchObjId += 1 + 1;
                        else
                            cchObjId += 1 + 2;
                    }
                    else
                    {
                        if (uValue < 1000)
                            cchObjId += 1 + 3;
                        else
                            cchObjId += 1 + 4;
                    }
                }
                else
                {
                    if (uValue < 1000000)
                    {
                        if (uValue < 100000)
                            cchObjId += 1 + 5;
                        else
                            cchObjId += 1 + 6;
                    }
                    else
                    {
                        if (uValue < 10000000)
                            cchObjId += 1 + 7;
                        else if (uValue < 100000000)
                            cchObjId += 1 + 8;
                        else
                            cchObjId += 1 + 9;
                    }
                }

                /* advance. */
                pbContent += rc;
                cbContent -= rc;
                if (!cbContent)
                {
                    if (cComponents < 128)
                    {
                        if (cchObjId < RT_SIZEOFMEMB(RTASN1OBJID, szObjId))
                        {
                            *pcComponents = cComponents;
                            *pcchObjId    = cchObjId;
                            return VINF_SUCCESS;
                        }
                        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_OBJID_TOO_LONG_STRING_FORM,
                                                   "%s: Object ID has a too long string form: %#x (max %#x)",
                                                   pszErrorTag, cchObjId, RT_SIZEOFMEMB(RTASN1OBJID, szObjId));
                    }
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_OBJID_TOO_MANY_COMPONENTS,
                                               "%s: Object ID has too many components: %#x (max 127)", pszErrorTag, cComponents);
                }

                /* next */
                rc = rtAsn1ObjId_ReadComponent(pbContent, cbContent, &uValue);
            } while (rc > 0);
        }
        rc = RTAsn1CursorSetInfo(pCursor, rc, "%s: Bad object ID component #%u encoding: %.*Rhxs",
                                 pszErrorTag, cComponents, cbContent, pbContent);
    }
    else if (cbContent)
        rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_OBJID_ENCODING, "%s: Object ID content is loo long: %#x",
                                 pszErrorTag, cbContent);
    else
        rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_OBJID_ENCODING, "%s: Zero length object ID content", pszErrorTag);
    return rc;
}



RTDECL(int) RTAsn1ObjId_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1OBJID pThis, const char *pszErrorTag)
{
    int rc = RTAsn1CursorReadHdr(pCursor, &pThis->Asn1Core, pszErrorTag);
    if (RT_SUCCESS(rc))
    {
        rc = RTAsn1CursorMatchTagClassFlags(pCursor, &pThis->Asn1Core, ASN1_TAG_OID,
                                            ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE, fFlags, pszErrorTag, "OID");
        if (RT_SUCCESS(rc))
        {
            /*
             * Validate and count things first.
             */
            uint8_t cComponents = 0; /* gcc maybe-crap */
            uint8_t cchObjId = 0;    /* ditto */
            rc = rtAsn1ObjId_PreParse(pCursor->pbCur, pThis->Asn1Core.cb, pCursor, pszErrorTag, &cComponents, &cchObjId);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Allocate memory for the components array, either out of the
                 * string buffer or off the heap.
                 */
                pThis->cComponents = cComponents;
                RTAsn1CursorInitAllocation(pCursor, &pThis->Allocation);
#if 0 /** @todo breaks with arrays of ObjIds or structs containing them. They get resized and repositioned in memory, thus invalidating the pointer. Add recall-pointers callback, or just waste memory? Or maybe make all arrays pointer-arrays? */
                if (cComponents * sizeof(uint32_t) <= sizeof(pThis->szObjId) - cchObjId - 1)
                    pThis->pauComponents = (uint32_t *)&pThis->szObjId[sizeof(pThis->szObjId) - cComponents * sizeof(uint32_t)];
                else
#endif
                    rc = RTAsn1MemAllocZ(&pThis->Allocation, (void **)&pThis->pauComponents,
                                         cComponents * sizeof(pThis->pauComponents[0]));
                if (RT_SUCCESS(rc))
                {
                    uint32_t *pauComponents = (uint32_t *)pThis->pauComponents;

                    /*
                     * Deal with the two first components first since they are
                     * encoded in a weird way to save a byte.
                     */
                    uint8_t const  *pbContent = pCursor->pbCur;
                    uint32_t        cbContent = pThis->Asn1Core.cb;
                    uint32_t        uValue;
                    rc = rtAsn1ObjId_ReadComponent(pbContent, cbContent, &uValue); AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        pbContent += rc;
                        cbContent -= rc;

                        if (uValue < 80)
                        {
                            pauComponents[0] = uValue / 40;
                            pauComponents[1] = uValue % 40;
                        }
                        else
                        {
                            pauComponents[0] = 2;
                            pauComponents[1] = uValue - 2*40;
                        }

                        char  *pszObjId    = &pThis->szObjId[0];
                        *pszObjId++        = g_achDigits[pauComponents[0]];
                        size_t cbObjIdLeft = cchObjId + 1 - 1;

                        rc = rtAsn1ObjId_InternalFormatComponent(pauComponents[1], &pszObjId, &cbObjIdLeft); AssertRC(rc);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * The other components are encoded in less complicated manner.
                             */
                            for (uint32_t i = 2; i < cComponents; i++)
                            {
                                rc = rtAsn1ObjId_ReadComponent(pbContent, cbContent, &uValue);
                                AssertRCBreak(rc);
                                pbContent += rc;
                                cbContent -= rc;
                                pauComponents[i] = uValue;
                                rc = rtAsn1ObjId_InternalFormatComponent(uValue, &pszObjId, &cbObjIdLeft);
                                AssertRCBreak(rc);
                            }
                            if (RT_SUCCESS(rc))
                            {
                                Assert(cbObjIdLeft == 1);
                                *pszObjId = '\0';

                                RTAsn1CursorSkip(pCursor, pThis->Asn1Core.cb);
                                pThis->Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
                                pThis->Asn1Core.pOps = &g_RTAsn1ObjId_Vtable;
                                return VINF_SUCCESS;
                            }
                        }
                    }
                    RTAsn1MemFree(&pThis->Allocation, (void *)pThis->pauComponents);
                    pThis->pauComponents = NULL;
                }
            }
        }
    }
    RT_ZERO(*pThis);
    return rc;
}



/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-objid-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-asn1-decoder.h>

