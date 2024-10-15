/* $Id: asn1-ut-dyntype-decode.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, Dynamic Type, Decoding.
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

#include <iprt/err.h>
#include <iprt/string.h>

#include <iprt/formats/asn1.h>



RTDECL(int) RTAsn1DynType_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1DYNTYPE pDynType, const char *pszErrorTag)
{
    RT_ZERO(*pDynType);

    Assert(!(fFlags & RTASN1CURSOR_GET_F_IMPLICIT)); RT_NOREF_PV(fFlags);
    uint32_t        cbSavedLeft = pCursor->cbLeft;
    uint8_t const  *pbSavedCur  = pCursor->pbCur;

    int rc = RTAsn1CursorReadHdr(pCursor, &pDynType->u.Core, pszErrorTag);
    if (RT_SUCCESS(rc))
    {
        pDynType->enmType = RTASN1TYPE_CORE;

        if (pDynType->u.Core.fClass == (ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE))
        {
            switch (pDynType->u.Core.uTag)
            {
                case ASN1_TAG_BOOLEAN:
                    pDynType->enmType = RTASN1TYPE_BOOLEAN;
                    break;
                case ASN1_TAG_INTEGER:
                    pDynType->enmType = RTASN1TYPE_INTEGER;
                    break;
                //case ASN1_TAG_ENUMERATED:
                //    pDynType->enmType = RTASN1TYPE_ENUMERATED;
                //    break;
                //case ASN1_TAG_REAL:
                //    pDynType->enmType = RTASN1TYPE_REAL;
                //    break;
                case ASN1_TAG_BIT_STRING:
                    pDynType->enmType = RTASN1TYPE_BIT_STRING;
                    break;
                case ASN1_TAG_OCTET_STRING:
                    pDynType->enmType = RTASN1TYPE_OCTET_STRING;
                    break;
                case ASN1_TAG_NULL:
                    pDynType->enmType = RTASN1TYPE_NULL;
                    break;
                case ASN1_TAG_SEQUENCE:
                    RT_ZERO(*pDynType);
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_DYNTYPE_BAD_TAG, "ASN.1 SEQUENCE shall be constructed.");
                case ASN1_TAG_SET:
                    RT_ZERO(*pDynType);
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_DYNTYPE_BAD_TAG, "ASN.1 SET shall be constructed.");
                case ASN1_TAG_OID:
                    pDynType->enmType = RTASN1TYPE_OBJID;
                    break;
                //case ASN1_TAG_RELATIVE_OID:
                //    pDynType->enmType = RTASN1TYPE_RELATIVE_OBJID;
                //    break;
                case ASN1_TAG_UTC_TIME:
                case ASN1_TAG_GENERALIZED_TIME:
                    pDynType->enmType = RTASN1TYPE_TIME;
                    break;
                case ASN1_TAG_UTF8_STRING:
                case ASN1_TAG_NUMERIC_STRING:
                case ASN1_TAG_PRINTABLE_STRING:
                case ASN1_TAG_T61_STRING:
                case ASN1_TAG_VIDEOTEX_STRING:
                case ASN1_TAG_IA5_STRING:
                case ASN1_TAG_GRAPHIC_STRING:
                case ASN1_TAG_VISIBLE_STRING:
                case ASN1_TAG_UNIVERSAL_STRING:
                case ASN1_TAG_GENERAL_STRING:
                case ASN1_TAG_BMP_STRING:
                    pDynType->enmType = RTASN1TYPE_STRING;
                    break;
                //case ASN1_TAG_CHARACTER_STRING:
                //    pDynType->enmType = RTASN1TYPE_CHARACTER_STRING;
                //    break;

                default:
                    RT_ZERO(*pDynType);
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_DYNTYPE_TAG_NOT_IMPL,
                                               "Primitive tag %u (%#x) not implemented.",
                                               pDynType->u.Core.uTag, pDynType->u.Core.uTag);
            }
        }
        else if (pDynType->u.Core.fClass == (ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_CONSTRUCTED))
            switch (pDynType->u.Core.uTag)
            {
                case ASN1_TAG_BOOLEAN:
                    RT_ZERO(*pDynType);
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_DYNTYPE_BAD_TAG, "ASN.1 BOOLEAN shall be primitive.");
                case ASN1_TAG_INTEGER:
                    RT_ZERO(*pDynType);
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_DYNTYPE_BAD_TAG, "ASN.1 BOOLEAN shall be primitive.");
                case ASN1_TAG_ENUMERATED:
                    RT_ZERO(*pDynType);
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_DYNTYPE_BAD_TAG, "ASN.1 ENUMERATED shall be primitive.");
                case ASN1_TAG_REAL:
                    RT_ZERO(*pDynType);
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_DYNTYPE_BAD_TAG, "ASN.1 REAL shall be primitive.");
                case ASN1_TAG_BIT_STRING:
                    pDynType->enmType = RTASN1TYPE_BIT_STRING;
                    break;
                case ASN1_TAG_OCTET_STRING:
                    pDynType->enmType = RTASN1TYPE_OCTET_STRING;
                    break;
                case ASN1_TAG_NULL:
                    RT_ZERO(*pDynType);
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_DYNTYPE_BAD_TAG, "ASN.1 NULL shall be primitive.");
                case ASN1_TAG_SEQUENCE:
#if 0
                    pDynType->enmType = RTASN1TYPE_SEQUENCE_CORE;
                    pDynType->u.SeqCore.Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
                    RTAsn1CursorSkip(pCursor, pDynType->u.Core.cb);
                    return VINF_SUCCESS;
#else
                    pDynType->enmType = RTASN1TYPE_CORE;
#endif
                    break;
                case ASN1_TAG_SET:
#if 0
                    pDynType->enmType = RTASN1TYPE_SET_CORE;
                    pDynType->u.SeqCore.Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
                    RTAsn1CursorSkip(pCursor, pDynType->u.Core.cb);
                    return VINF_SUCCESS;
#else
                    pDynType->enmType = RTASN1TYPE_CORE;
#endif
                    break;
                case ASN1_TAG_OID:
                    RT_ZERO(*pDynType);
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_DYNTYPE_BAD_TAG, "ASN.1 OBJECT ID shall be primitive.");
                case ASN1_TAG_RELATIVE_OID:
                    RT_ZERO(*pDynType);
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_DYNTYPE_BAD_TAG, "ASN.1 RELATIVE OID shall be primitive.");

                case ASN1_TAG_UTF8_STRING:
                case ASN1_TAG_NUMERIC_STRING:
                case ASN1_TAG_PRINTABLE_STRING:
                case ASN1_TAG_T61_STRING:
                case ASN1_TAG_VIDEOTEX_STRING:
                case ASN1_TAG_IA5_STRING:
                case ASN1_TAG_GRAPHIC_STRING:
                case ASN1_TAG_VISIBLE_STRING:
                case ASN1_TAG_UNIVERSAL_STRING:
                case ASN1_TAG_GENERAL_STRING:
                case ASN1_TAG_BMP_STRING:
                    pDynType->enmType = RTASN1TYPE_STRING;
                    break;
                //case ASN1_TAG_CHARACTER_STRING:
                //    pDynType->enmType = RTASN1TYPE_CHARACTER_STRING;
                //    break;

                default:
                    RT_ZERO(*pDynType);
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_DYNTYPE_TAG_NOT_IMPL,
                                               "Constructed tag %u (%#x) not implemented.",
                                               pDynType->u.Core.uTag, pDynType->u.Core.uTag);
            }
        else
            Assert(pDynType->enmType == RTASN1TYPE_CORE);

        /*
         * Restore the cursor and redo with specific type.
         */
        pCursor->pbCur  = pbSavedCur;
        pCursor->cbLeft = cbSavedLeft;

        switch (pDynType->enmType)
        {
            case RTASN1TYPE_INTEGER:
                rc = RTAsn1Integer_DecodeAsn1(pCursor, 0, &pDynType->u.Integer, pszErrorTag);
                break;
            case RTASN1TYPE_BOOLEAN:
                rc = RTAsn1Boolean_DecodeAsn1(pCursor, 0, &pDynType->u.Boolean, pszErrorTag);
                break;
            case RTASN1TYPE_OBJID:
                rc = RTAsn1ObjId_DecodeAsn1(pCursor, 0, &pDynType->u.ObjId, pszErrorTag);
                break;
            case RTASN1TYPE_BIT_STRING:
                rc = RTAsn1BitString_DecodeAsn1(pCursor, 0, &pDynType->u.BitString, pszErrorTag);
                break;
            case RTASN1TYPE_OCTET_STRING:
                rc = RTAsn1OctetString_DecodeAsn1(pCursor, 0, &pDynType->u.OctetString, pszErrorTag);
                break;
            case RTASN1TYPE_NULL:
                rc = RTAsn1Null_DecodeAsn1(pCursor, 0, &pDynType->u.Asn1Null, pszErrorTag);
                break;
            case RTASN1TYPE_TIME:
                rc = RTAsn1Time_DecodeAsn1(pCursor, 0, &pDynType->u.Time, pszErrorTag);
                break;
            case RTASN1TYPE_STRING:
                rc = RTAsn1String_DecodeAsn1(pCursor, 0, &pDynType->u.String, pszErrorTag);
                break;
            case RTASN1TYPE_CORE:
                rc = RTAsn1Core_DecodeAsn1(pCursor, 0, &pDynType->u.Core, pszErrorTag);
                break;
            default:
                AssertFailedReturn(VERR_INTERNAL_ERROR_4);
        }
        if (RT_SUCCESS(rc))
            return rc;
    }
    RT_ZERO(*pDynType);
    return rc;
}

