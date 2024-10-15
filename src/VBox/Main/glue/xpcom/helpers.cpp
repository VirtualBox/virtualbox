/* $Id: helpers.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * COM helper functions for XPCOM
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "VBox/com/defs.h"
#include <nsMemory.h>
#include <iprt/assertcompile.h>
#include <iprt/utf16.h>


//
// OLE Automation string APIs
//

// Note: on Windows, every BSTR stores its length in the
// byte just before the pointer you get. If we do it like this,
// the caller cannot just use nsMemory::Free() on our strings.
// Therefore we'll try to implement it differently and hope that
// we don't run into problems.

/**
 * Copies a string into a new memory block including the terminating UCS2 NULL.
 *
 * @param   pwsz    Source string to duplicate.
 * @returns New BSTR string buffer.
 */
BSTR SysAllocString(const OLECHAR *pwszSrc)
{
    AssertCompile(sizeof(*pwszSrc) == sizeof(PRUnichar));
    if (pwszSrc)
        return SysAllocStringLen(pwszSrc, RTUtf16Len((PCRTUTF16)pwszSrc));
    return NULL;
}

/**
 * Duplicates an ANSI string into a BSTR / allocates a BSTR with a size given in
 * bytes.
 *
 * No conversion is done.
 *
 * @param   pszSrc      Source string to copy, optional.
 * @param   cbSrcReq    Length of the source string / memory request in bytes.
 * @returns new BSTR string buffer, NULL on failure.
 */
BSTR SysAllocStringByteLen(char const *pszSrc, unsigned int cbSrcReq)
{
    BSTR pBstrNew = (BSTR)nsMemory::Alloc(RT_ALIGN_Z(cbSrcReq + sizeof(OLECHAR), sizeof(OLECHAR)));
    AssertCompile(sizeof(*pBstrNew) == sizeof(OLECHAR));
    if (pBstrNew)
    {
        if (!pszSrc)
            memset(pBstrNew, 0, cbSrcReq + sizeof(OLECHAR));
        else
        {
            // Copy the string and make sure it is terminated.
            memcpy(pBstrNew, pszSrc, cbSrcReq);
            char *pchTerminator = (char *)pBstrNew;
            pchTerminator[cbSrcReq] = '\0';
            pchTerminator[cbSrcReq + 1] = '\0';
        }
    }
    return pBstrNew;
}

/**
 * Duplicates a UTF-16 string into a BSTR / Allocates a BSTR with a size given
 * in UTF-16 characters.
 *
 * @param   pwszSrc     Pointer to the source string, optional.
 * @param   cwcSrcReq   Length of the source string / memory request in UTF-16
 *                      characters.
 * @returns new BSTR string buffer, NULL on failure.
 */
BSTR SysAllocStringLen(const OLECHAR *pwszSrc, unsigned int cwcSrcReq)
{
    size_t const cbReq = (cwcSrcReq + 1) * sizeof(OLECHAR);
    BSTR pBstrNew = (BSTR)nsMemory::Alloc(cbReq);
    AssertCompile(sizeof(*pBstrNew) == sizeof(OLECHAR));
    if (pBstrNew)
    {
        if (!pwszSrc)
            memset(pBstrNew, 0, cbReq);
        else
        {
            // Copy the string and make sure it is terminated.
            memcpy(pBstrNew, pwszSrc, cbReq - sizeof(OLECHAR));
            pBstrNew[cwcSrcReq] = L'\0';
        }
    }
    return pBstrNew;
}

/**
 * Frees the memory associated with the given BSTR.
 *
 * @param   pBstr  The string to free.  NULL is ignored.
 */
void SysFreeString(BSTR pBstr)
{
    if (pBstr)
        nsMemory::Free(pBstr);
}

/**
 * Duplicates @a pwszSrc into an exsting BSTR, adjust its size to make it fit.
 *
 * @param   ppBstr  The existing BSTR buffer pointer.
 * @param   pwszSrc Source string to copy.  If NULL, the existing BSTR is freed.
 * @returns success indicator (TRUE/FALSE)
 */
int SysReAllocString(BSTR *ppBstr, const OLECHAR *pwszSrc)
{
    if (pwszSrc)
        return SysReAllocStringLen(ppBstr, pwszSrc, RTUtf16Len((PCRTUTF16)pwszSrc));
    SysFreeString(*ppBstr);
    *ppBstr = NULL;
    return 1;
}

/**
 * Duplicates @a pwszSrc into an exsting BSTR / resizing an existing BSTR buffer
 * into the given size (@a cwcSrcReq).
 *
 * @param   ppBstr      The existing BSTR buffer pointer.
 * @param   pwszSrc     Source string to copy into the adjusted pbstr, optional.
 * @param   cwcSrcReq   Length of the source string / request in UCS2
 *                      characters, a zero terminator is always added.
 * @returns success indicator (TRUE/FALSE)
 */
int SysReAllocStringLen(BSTR *ppBstr, const OLECHAR *pwszSrc, unsigned int cwcSrcReq)
{
    BSTR pBstrOld = *ppBstr;
    AssertCompile(sizeof(*pBstrOld) == sizeof(OLECHAR));
    if (pBstrOld)
    {
        if ((BSTR)pwszSrc == pBstrOld)
            pwszSrc = NULL;

        size_t cbReq = (cwcSrcReq + 1) * sizeof(OLECHAR);
        BSTR pBstrNew = (BSTR)nsMemory::Realloc(pBstrOld, cbReq);
        if (pBstrNew)
        {
            if (pwszSrc)
                memcpy(pBstrNew, pwszSrc, cbReq - sizeof(OLECHAR));
            pBstrNew[cwcSrcReq] = L'\0';
            *ppBstr = pBstrNew;
            return 1;
        }
    }
    else
    {
        // allocate a new string
        *ppBstr = SysAllocStringLen(pwszSrc, cwcSrcReq);
        if (*ppBstr)
            return 1;
    }
    return 0;
}

/**
 * Returns the string length in bytes without the terminator.
 *
 * @param   pBstr   The BSTR to get the byte length of.
 * @returns String length in bytes.
 */
unsigned int SysStringByteLen(BSTR pBstr)
{
    AssertCompile(sizeof(OLECHAR) == sizeof(*pBstr));
    return RTUtf16Len(pBstr) * sizeof(OLECHAR);
}

/**
 * Returns the string length in OLECHARs without the terminator.
 *
 * @param   pBstr   The BSTR to get the length of.
 * @returns String length in OLECHARs.
 */
unsigned int SysStringLen(BSTR pBstr)
{
    return RTUtf16Len(pBstr);
}
