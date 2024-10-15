/* $Id: SUPR3HardenedNoCrt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Library - Hardened main() no-crt routines.
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
#if RT_OS_WINDOWS
# include <iprt/win/windows.h>
#endif

#include <VBox/sup.h>

#include "SUPLibInternal.h"


#ifdef SUP_HARDENED_NEED_CRT_FUNCTIONS /** @todo this crap is obsolete. */

/** memcmp */
DECLHIDDEN(int) suplibHardenedMemComp(void const *pvDst, const void *pvSrc, size_t cbToComp)
{
    size_t const   *puDst = (size_t const *)pvDst;
    size_t const   *puSrc = (size_t const *)pvSrc;
    while (cbToComp >= sizeof(size_t))
    {
        if (*puDst != *puSrc)
            break;
        puDst++;
        puSrc++;
        cbToComp -= sizeof(size_t);
    }

    uint8_t const  *pbDst = (uint8_t const *)puDst;
    uint8_t const  *pbSrc = (uint8_t const *)puSrc;
    while (cbToComp > 0)
    {
        if (*pbDst != *pbSrc)
        {
            if (*pbDst < *pbSrc)
                return -1;
            return 1;
        }

        pbDst++;
        pbSrc++;
        cbToComp--;
    }

    return 0;
}


/** memcpy */
DECLHIDDEN(void *) suplibHardenedMemCopy(void *pvDst, const void *pvSrc, size_t cbToCopy)
{
    size_t         *puDst = (size_t *)pvDst;
    size_t const   *puSrc = (size_t const *)pvSrc;
    while (cbToCopy >= sizeof(size_t))
    {
        *puDst++ = *puSrc++;
        cbToCopy -= sizeof(size_t);
    }

    uint8_t        *pbDst = (uint8_t *)puDst;
    uint8_t const  *pbSrc = (uint8_t const *)puSrc;
    while (cbToCopy > 0)
    {
        *pbDst++ = *pbSrc++;
        cbToCopy--;
    }

    return pvDst;
}


/** memset */
DECLHIDDEN(void *) suplibHardenedMemSet(void *pvDst, int ch, size_t cbToSet)
{
    uint8_t *pbDst = (uint8_t *)pvDst;
    while (cbToSet > 0)
    {
        *pbDst++ = (uint8_t)ch;
        cbToSet--;
    }

    return pvDst;
}


/** strcpy */
DECLHIDDEN(char *) suplibHardenedStrCopy(char *pszDst, const char *pszSrc)
{
    char *pszRet = pszDst;
    char ch;
    do
    {
        ch = *pszSrc++;
        *pszDst++ = ch;
    } while (ch);
    return pszRet;
}


/** strlen */
DECLHIDDEN(size_t) suplibHardenedStrLen(const char *psz)
{
    const char *pszStart = psz;
    while (*psz)
        psz++;
    return psz - pszStart;
}


/** strcat */
DECLHIDDEN(char *) suplibHardenedStrCat(char *pszDst, const char *pszSrc)
{
    char *pszRet = pszDst;
    while (*pszDst)
        pszDst++;
    suplibHardenedStrCopy(pszDst, pszSrc);
    return pszRet;
}


/** strcmp */
DECLHIDDEN(int) suplibHardenedStrCmp(const char *psz1, const char *psz2)
{
    for (;;)
    {
        char ch1 = *psz1++;
        char ch2 = *psz2++;
        if (ch1 != ch2)
            return ch1 < ch2 ? -1 : 1;
        if (ch1 == 0)
            return 0;
    }
}


/** strncmp */
DECLHIDDEN(int) suplibHardenedStrNCmp(const char *psz1, const char *psz2, size_t cchMax)
{
    while (cchMax-- > 0)
    {
        char ch1 = *psz1++;
        char ch2 = *psz2++;
        if (ch1 != ch2)
            return ch1 < ch2 ? -1 : 1;
        if (ch1 == 0)
            break;
    }
    return 0;
}

#endif /* SUP_HARDENED_NEED_CRT_FUNCTIONS */

