/* $Id: asn1-ut-bitstring-decode.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, BIT STRING Type, Decoding.
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



RTDECL(int) RTAsn1BitString_DecodeAsn1Ex(PRTASN1CURSOR pCursor, uint32_t fFlags, uint32_t cMaxBits, PRTASN1BITSTRING pThis,
                                         const char *pszErrorTag)
{
    pThis->cBits         = 0;
    pThis->cMaxBits      = cMaxBits;
    pThis->uBits.pv      = NULL;
    pThis->pEncapsulated = NULL;
    RTAsn1CursorInitAllocation(pCursor, &pThis->EncapsulatedAllocation);

    int rc = RTAsn1CursorReadHdr(pCursor, &pThis->Asn1Core, pszErrorTag);
    if (RT_SUCCESS(rc))
    {
        rc = RTAsn1CursorMatchTagClassFlagsString(pCursor, &pThis->Asn1Core, ASN1_TAG_BIT_STRING,
                                                  ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                                                  fFlags, pszErrorTag, "BIT STRING");
        if (RT_SUCCESS(rc))
        {
            if (!(pThis->Asn1Core.fClass & ASN1_TAGFLAG_CONSTRUCTED))
            {
                if (   (    cMaxBits == UINT32_MAX
                        || RT_ALIGN(cMaxBits, 8) / 8 + 1 >= pThis->Asn1Core.cb)
                    && pThis->Asn1Core.cb > 0)
                {
                    uint8_t cUnusedBits = pThis->Asn1Core.cb > 0 ? *pThis->Asn1Core.uData.pu8 : 0;
                    if (pThis->Asn1Core.cb < 2)
                    {
                        /* Not bits present. */
                        if (cUnusedBits == 0)
                        {
                            pThis->cBits    = 0;
                            pThis->uBits.pv = NULL;
                            RTAsn1CursorSkip(pCursor, pThis->Asn1Core.cb);
                            pThis->Asn1Core.pOps = &g_RTAsn1BitString_Vtable;
                            pThis->Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
                            return VINF_SUCCESS;
                        }
                        rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_BITSTRING_ENCODING,
                                                 "%s: Bad unused bit count: %#x (cb=%#x)",
                                                 pszErrorTag, cUnusedBits, pThis->Asn1Core.cb);
                    }
                    else if (cUnusedBits < 8)
                    {
                        pThis->cBits  = (pThis->Asn1Core.cb - 1) * 8;
                        pThis->cBits -= cUnusedBits;
                        pThis->uBits.pu8 = pThis->Asn1Core.uData.pu8 + 1;
                        if (   !(pCursor->fFlags & (RTASN1CURSOR_FLAGS_DER | RTASN1CURSOR_FLAGS_CER))
                            || cUnusedBits == 0
                            || !( pThis->uBits.pu8[pThis->Asn1Core.cb - 2] & (((uint8_t)1 << cUnusedBits) - (uint8_t)1) ) )
                        {
                            RTAsn1CursorSkip(pCursor, pThis->Asn1Core.cb);
                            pThis->Asn1Core.pOps = &g_RTAsn1BitString_Vtable;
                            pThis->Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
                            return VINF_SUCCESS;
                        }
                        rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_BITSTRING_ENCODING,
                                                 "%s: Unused bits shall be zero in DER/CER mode: last byte=%#x cUnused=%#x",
                                                 pszErrorTag, pThis->uBits.pu8[pThis->cBits / 8], cUnusedBits);
                    }
                    else
                        rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_BITSTRING_ENCODING,
                                                 "%s: Bad unused bit count: %#x (cb=%#x)",
                                                 pszErrorTag, cUnusedBits, pThis->Asn1Core.cb);
                }
                else
                    rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_BITSTRING_ENCODING,
                                             "%s: Size mismatch: cb=%#x, expected %#x (cMaxBits=%#x)",
                                             pszErrorTag, pThis->Asn1Core.cb,  RT_ALIGN(cMaxBits, 8) / 8 + 1, cMaxBits);
            }
            else
                rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CONSTRUCTED_STRING_NOT_IMPL,
                                         "%s: Constructed BIT STRING not implemented.", pszErrorTag);
        }
    }
    RT_ZERO(*pThis);
    return rc;
}


RTDECL(int) RTAsn1BitString_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1BITSTRING pThis, const char *pszErrorTag)
{
    return RTAsn1BitString_DecodeAsn1Ex(pCursor, fFlags, UINT32_MAX, pThis, pszErrorTag);
}


/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-bitstring-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-asn1-decoder.h>

