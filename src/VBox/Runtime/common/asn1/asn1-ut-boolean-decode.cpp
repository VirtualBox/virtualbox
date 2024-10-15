/* $Id: asn1-ut-boolean-decode.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, BOOLEAN Type, Decoding.
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



RTDECL(int) RTAsn1Boolean_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1BOOLEAN pThis, const char *pszErrorTag)
{
    pThis->fValue = 0;
    int rc = RTAsn1CursorReadHdr(pCursor, &pThis->Asn1Core, pszErrorTag);
    if (RT_SUCCESS(rc))
    {
        rc = RTAsn1CursorMatchTagClassFlags(pCursor, &pThis->Asn1Core, ASN1_TAG_BOOLEAN,
                                            ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE, fFlags, pszErrorTag, "BOOLEAN");
        if (RT_SUCCESS(rc))
        {
            if (pThis->Asn1Core.cb == 1)
            {
                RTAsn1CursorSkip(pCursor, pThis->Asn1Core.cb);
                pThis->Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
                pThis->Asn1Core.pOps    = &g_RTAsn1Boolean_Vtable;

                pThis->fValue = *pThis->Asn1Core.uData.pu8 != 0;
                if (   *pThis->Asn1Core.uData.pu8 == 0
                    || *pThis->Asn1Core.uData.pu8 == 0xff
                    || !(pCursor->fFlags & (RTASN1CURSOR_FLAGS_DER | RTASN1CURSOR_FLAGS_CER)) )
                    return VINF_SUCCESS;

                rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_BOOLEAN_ENCODING,
                                         "%s: Invalid CER/DER boolean value: %#x, valid: 0, 0xff",
                                         pszErrorTag, *pThis->Asn1Core.uData.pu8);
            }
            else
                rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_BOOLEAN_ENCODING, "%s: Invalid boolean length, exepcted 1: %#x",
                                         pszErrorTag, pThis->Asn1Core.cb);
        }
    }
    RT_ZERO(*pThis);
    return rc;
}


/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-boolean-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-asn1-decoder.h>

