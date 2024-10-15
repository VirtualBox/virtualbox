/* $Id: utf-8-case2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - UTF-8 Case Sensitivity and Folding, Part 2 (requires unidata-flags.cpp).
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
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/uni.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include "internal/string.h"


RTDECL(bool) RTStrIsCaseFoldable(const char *psz)
{
    /*
     * Loop the code points in the string, checking them one by one until we
     * find something that can be folded.
     */
    RTUNICP uc;
    do
    {
        int rc = RTStrGetCpEx(&psz, &uc);
        if (RT_SUCCESS(rc))
        {
            if (RTUniCpIsFoldable(uc))
                return true;
        }
        else
        {
            /* bad encoding, just skip it quietly (uc == RTUNICP_INVALID (!= 0)). */
            AssertRC(rc);
        }
    } while (uc != 0);

    return false;
}
RT_EXPORT_SYMBOL(RTStrIsCaseFoldable);


RTDECL(bool) RTStrIsUpperCased(const char *psz)
{
    /*
     * Check that there are no lower case chars in the string.
     */
    RTUNICP uc;
    do
    {
        int rc = RTStrGetCpEx(&psz, &uc);
        if (RT_SUCCESS(rc))
        {
            if (RTUniCpIsLower(uc))
                return false;
        }
        else
        {
            /* bad encoding, just skip it quietly (uc == RTUNICP_INVALID (!= 0)). */
            AssertRC(rc);
        }
    } while (uc != 0);

    return true;
}
RT_EXPORT_SYMBOL(RTStrIsUpperCased);


RTDECL(bool) RTStrIsLowerCased(const char *psz)
{
    /*
     * Check that there are no lower case chars in the string.
     */
    RTUNICP uc;
    do
    {
        int rc = RTStrGetCpEx(&psz, &uc);
        if (RT_SUCCESS(rc))
        {
            if (RTUniCpIsUpper(uc))
                return false;
        }
        else
        {
            /* bad encoding, just skip it quietly (uc == RTUNICP_INVALID (!= 0)). */
            AssertRC(rc);
        }
    } while (uc != 0);

    return true;
}
RT_EXPORT_SYMBOL(RTStrIsLowerCased);

