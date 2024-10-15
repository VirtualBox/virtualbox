/* $Id: x509-init.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - X.509, Initialization API.
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
#include <iprt/crypto/x509.h>

#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/uni.h>

#include "x509-internal.h"


static int rtCrX509Extension_ExtnValue_Clone(PRTCRX509EXTENSION pThis, PCRTCRX509EXTENSION pSrc)
{
    pThis->enmValue = pSrc->enmValue;
    return VINF_SUCCESS;
}


RTDECL(int) RTCrX509Name_RecodeAsUtf8(PRTCRX509NAME pThis, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    uint32_t                            cRdns = pThis->cItems;
    PRTCRX509RELATIVEDISTINGUISHEDNAME *ppRdn = pThis->papItems;
    while (cRdns-- > 0)
    {
        PRTCRX509RELATIVEDISTINGUISHEDNAME const pRdn     = *ppRdn;
        uint32_t                                 cAttribs = pRdn->cItems;
        PRTCRX509ATTRIBUTETYPEANDVALUE          *ppAttrib = pRdn->papItems;
        while (cAttribs-- > 0)
        {
            PRTCRX509ATTRIBUTETYPEANDVALUE const pAttrib = *ppAttrib;
            if (pAttrib->Value.enmType == RTASN1TYPE_STRING)
            {
                int rc = RTAsn1String_RecodeAsUtf8(&pAttrib->Value.u.String, pAllocator);
                if (RT_FAILURE(rc))
                    return rc;
            }
            ppAttrib++;
        }
        ppRdn++;
    }
    return VINF_SUCCESS;
}


/*
 * Generate the code.
 */
#include <iprt/asn1-generator-init.h>

