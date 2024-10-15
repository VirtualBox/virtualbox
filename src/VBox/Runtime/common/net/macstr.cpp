/* $Id: macstr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Command Line Parsing
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <iprt/cidr.h>
#include <iprt/net.h>                   /* must come before getopt.h */
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/message.h>
#include <iprt/string.h>


/**
 * Converts an stringified Ethernet MAC address into the RTMAC representation.
 *
 * @returns VINF_SUCCESS on success.
 *
 * @param   pszValue        The value to convert.
 * @param   pAddr           Where to store the result.
 */
RTDECL(int) RTNetStrToMacAddr(const char *pszValue, PRTMAC pAddr)
{
    /*
     * First check if it might be a 12 xdigit string without any separators.
     */
    size_t cchValue = strlen(pszValue);
    if (cchValue >= 12 && memchr(pszValue, ':', 12) == NULL)
    {
        bool fOkay = true;
        for (size_t off = 0; off < 12 && fOkay; off++)
            fOkay = RT_C_IS_XDIGIT(pszValue[off]);
        if (fOkay && cchValue > 12)
            for (size_t off = 12; off < cchValue && fOkay; off++)
                fOkay = RT_C_IS_SPACE(pszValue[off]);
        if (fOkay)
        {
            int rc = RTStrConvertHexBytes(pszValue, pAddr, sizeof(*pAddr), 0);
            if (RT_SUCCESS(rc))
                rc = VINF_SUCCESS;
            return rc;
        }
    }

    /*
     * Not quite sure if I should accept stuff like "08::27:::1" here...
     * The code is accepting "::" patterns now, except for for the first
     * and last parts.
     */

    /* first */
    char *pszNext;
    int rc = RTStrToUInt8Ex(RTStrStripL(pszValue), &pszNext, 16, &pAddr->au8[0]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != ':')
        return VERR_INVALID_PARAMETER;

    /* middle */
    for (unsigned i = 1; i < 5; i++)
    {
        if (*pszNext == ':')
            pAddr->au8[i] = 0;
        else
        {
            rc = RTStrToUInt8Ex(pszNext, &pszNext, 16, &pAddr->au8[i]);
            if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
                return rc;
            if (*pszNext != ':')
                return VERR_INVALID_PARAMETER;
        }
        pszNext++;
    }

    /* last */
    rc = RTStrToUInt8Ex(pszNext, &pszNext, 16, &pAddr->au8[5]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES)
        return rc;
    pszNext = RTStrStripL(pszNext);
    if (*pszNext)
        return VERR_INVALID_PARAMETER;

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTNetStrToMacAddr);
