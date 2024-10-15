/* $Id: RTDirCreateUniqueNumbered-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTDirCreateUniqueNumbered, generic implementation.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/dir.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/rand.h>
#include <iprt/string.h>


RTDECL(int) RTDirCreateUniqueNumbered(char *pszPath, size_t cbSize, RTFMODE fMode, size_t cchDigits, char chSep)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cbSize, VERR_BUFFER_OVERFLOW);
    AssertReturn(cchDigits > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cchDigits < 64, VERR_INVALID_PARAMETER);

    /* Check that there is sufficient space.  */
    char *pszEnd = RTStrEnd(pszPath, cbSize);
    AssertReturn(pszEnd, VERR_BUFFER_OVERFLOW);
    size_t cbLeft = cbSize - (pszEnd - pszPath);
    AssertReturn(cbLeft > (chSep ? 1U : 0U) + cchDigits, VERR_BUFFER_OVERFLOW);

    /*
     * First try the pretty name without any numbers appended.
     */
    int rc = RTDirCreate(pszPath, fMode, 0);
    if (RT_SUCCESS(rc))
        return rc;
    if (rc == VERR_ALREADY_EXISTS)
    {
        /*
         * Already exist, apply template specification.
         */

        /* Max 10000 tries (like RTDirCreateTemp), but stop earlier if we haven't got enough digits to work with.  */
        uint32_t cMaxTries;
        switch (cchDigits)
        {
            case 1:  cMaxTries =    40; break;
            case 2:  cMaxTries =   400; break;
            case 3:  cMaxTries =  4000; break;
            default: cMaxTries = 10000; break;
        }

        static uint64_t const s_aEndSeqs[] =
        {
            UINT64_C(0),
            UINT64_C(9),
            UINT64_C(99),
            UINT64_C(999),
            UINT64_C(9999),
            UINT64_C(99999),
            UINT64_C(999999),
            UINT64_C(9999999),
            UINT64_C(99999999),
            UINT64_C(999999999),
            UINT64_C(9999999999),
            UINT64_C(99999999999),
            UINT64_C(999999999999),
            UINT64_C(9999999999999),
            UINT64_C(99999999999999),
            UINT64_C(999999999999999),
            UINT64_C(9999999999999999),
            UINT64_C(99999999999999999),
            UINT64_C(999999999999999999),
            UINT64_C(9999999999999999999),
        };
        uint64_t const uEndSeq = cchDigits < RT_ELEMENTS(s_aEndSeqs) ? s_aEndSeqs[cchDigits] : UINT64_MAX;

        /* Add separator if requested. */
        if (chSep != '\0')
        {
            *pszEnd++ = chSep;
            *pszEnd   = '\0';
            cbLeft--;
        }

        Assert(cbLeft > cchDigits);
        for (uint32_t iTry = 0; iTry <= cMaxTries; iTry++)
        {
            /* Try sequentially first for a little bit, then switch to random numbers. */
            uint64_t iSeq;
            if (iTry > 20)
                iSeq = RTRandU64Ex(0, uEndSeq);
            else
            {
                iSeq = iTry;
                if (uEndSeq < UINT64_MAX)
                    iSeq %= uEndSeq + 1;
            }
            ssize_t cchRet = RTStrFormatU64(pszEnd, cbLeft, iSeq, 10 /*uiBase*/,
                                            (int)cchDigits /*cchWidth*/, 0 /*cchPrecision*/, RTSTR_F_WIDTH | RTSTR_F_ZEROPAD);
            Assert((size_t)cchRet == cchDigits); NOREF(cchRet);

            rc = RTDirCreate(pszPath, fMode, 0);
            if (RT_SUCCESS(rc))
                return rc;
            if (rc != VERR_ALREADY_EXISTS)
                break;
        }
    }

    /* We've given up or failed. */
    *pszPath = '\0';
    return rc;
}
RT_EXPORT_SYMBOL(RTDirCreateUniqueNumbered);

