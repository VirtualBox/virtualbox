/* $Id: asn1-ut-string-decode.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, XXX STRING Types, Decoding.
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


RTDECL(int) RTAsn1String_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1STRING pThis, const char *pszErrorTag)
{
    RT_ZERO(*pThis);
    AssertReturn(!(fFlags & RTASN1CURSOR_GET_F_IMPLICIT), VERR_INVALID_PARAMETER);

    int rc = RTAsn1CursorReadHdr(pCursor, &pThis->Asn1Core, pszErrorTag);
    if (RT_SUCCESS(rc))
    {
        /*
         * Do tag matching.
         */
        switch (pThis->Asn1Core.uTag)
        {
            case ASN1_TAG_UTF8_STRING:
            case ASN1_TAG_NUMERIC_STRING:
            case ASN1_TAG_PRINTABLE_STRING:
            case ASN1_TAG_T61_STRING:
            case ASN1_TAG_VIDEOTEX_STRING:
            case ASN1_TAG_IA5_STRING:
            case ASN1_TAG_GENERALIZED_TIME:
            case ASN1_TAG_GRAPHIC_STRING:
            case ASN1_TAG_VISIBLE_STRING:
            case ASN1_TAG_GENERAL_STRING:
            case ASN1_TAG_UNIVERSAL_STRING:
            case ASN1_TAG_BMP_STRING:
                rc = VINF_SUCCESS;
                break;
            default:
                rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_TAG_MISMATCH,
                                         "%s: Not a string object: fClass=%#x / uTag=%#x",
                                         pszErrorTag, pThis->Asn1Core.fClass, pThis->Asn1Core.uTag);
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Match flags. CER/DER makes it complicated.
             */
            if (pThis->Asn1Core.fClass == (ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE))
            {
                /*
                 * Primitive strings are simple.
                 */
                RTAsn1CursorSkip(pCursor, pThis->Asn1Core.cb);
                pThis->Asn1Core.pOps    = &g_RTAsn1String_Vtable;
                pThis->Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
                RTAsn1CursorInitAllocation(pCursor, &pThis->Allocation);

                /* UTF-8 conversion is done lazily, upon request. */
                return VINF_SUCCESS;
            }

            if (pThis->Asn1Core.fClass == (ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_CONSTRUCTED))
            {
                /*
                 * Constructed strings are not yet fully implemented.
                 */
                if (pCursor->fFlags & RTASN1CURSOR_FLAGS_DER)
                    rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_ILLEGAL_CONSTRUCTED_STRING,
                                             "%s: DER encoding does not allow constructed strings (cb=%#x uTag=%#x fClass=%#x)",
                                             pszErrorTag, pThis->Asn1Core.cb, pThis->Asn1Core.uTag, pThis->Asn1Core.fClass);
                else if (pCursor->fFlags & RTASN1CURSOR_FLAGS_CER)
                {
                    if (pThis->Asn1Core.cb > 1000)
                        rc = VINF_SUCCESS;
                    else
                        rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_ILLEGAL_CONSTRUCTED_STRING,
                                                 "%s: Constructed strings only allowed for >1000 byte in CER encoding: cb=%#x uTag=%#x fClass=%#x",
                                                 pszErrorTag, pThis->Asn1Core.cb,
                                                 pThis->Asn1Core.uTag, pThis->Asn1Core.fClass);
                }
                /** @todo implement constructed strings. */
                if (RT_SUCCESS(rc))
                    rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CONSTRUCTED_STRING_NOT_IMPL,
                                             "%s: Support for constructed strings is not implemented", pszErrorTag);
            }
            else
                rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_TAG_FLAG_CLASS_MISMATCH,
                                         "%s: Not a valid string object: fClass=%#x / uTag=%#x",
                                         pszErrorTag, pThis->Asn1Core.fClass, pThis->Asn1Core.uTag);
        }
    }
    RT_ZERO(*pThis);
    return rc;
}


/**
 * Common worker for the specific string type getters.
 *
 * @returns IPRT status code
 * @param   pCursor             The cursor.
 * @param   fFlags              The RTASN1CURSOR_GET_F_XXX flags.
 * @param   uTag                The string tag.
 * @param   pThis               The output object.
 * @param   pszErrorTag         The error tag.
 * @param   pszWhat             The string type name.
 */
static int rtAsn1XxxString_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags, uint8_t uTag, PRTASN1STRING pThis,
                                      const char *pszErrorTag, const char *pszWhat)
{
    pThis->cchUtf8 = 0;
    pThis->pszUtf8 = NULL;

    int rc = RTAsn1CursorReadHdr(pCursor, &pThis->Asn1Core, pszErrorTag);
    if (RT_SUCCESS(rc))
    {
        rc = RTAsn1CursorMatchTagClassFlagsString(pCursor, &pThis->Asn1Core, uTag,
                                                  ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                                                  fFlags, pszErrorTag, pszWhat);
        if (RT_SUCCESS(rc))
        {
            if (!(pThis->Asn1Core.fClass & ASN1_TAGFLAG_CONSTRUCTED))
            {
                RTAsn1CursorSkip(pCursor, pThis->Asn1Core.cb);
                pThis->Asn1Core.pOps    = &g_RTAsn1String_Vtable;
                pThis->Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
                RTAsn1CursorInitAllocation(pCursor, &pThis->Allocation);
                /* UTF-8 conversion is done lazily, upon request. */
                return VINF_SUCCESS;
            }
            rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CONSTRUCTED_STRING_NOT_IMPL,
                                     "%s: Constructed %s not implemented.", pszErrorTag, pszWhat);
        }
    }
    RT_ZERO(*pThis);
    return rc;
}


/*
 * Generate code for the tag specific decoders.
 */
#define RTASN1STRING_IMPL(a_uTag, a_szTag, a_Api) \
    RTDECL(int) RT_CONCAT(a_Api,_DecodeAsn1)(PRTASN1CURSOR pCursor, uint32_t fFlags, \
                                             PRTASN1STRING pThis, const char *pszErrorTag) \
    { \
        return rtAsn1XxxString_DecodeAsn1(pCursor, fFlags, a_uTag, pThis, pszErrorTag, a_szTag); \
    }
#include "asn1-ut-string-template2.h"


/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-string-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-asn1-decoder.h>

