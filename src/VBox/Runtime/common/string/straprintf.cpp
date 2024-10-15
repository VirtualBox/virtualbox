/* $Id: straprintf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Allocating String Formatters.
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

#include <iprt/assert.h>
#include <iprt/alloc.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** strallocoutput() argument structure. */
typedef struct STRALLOCARG
{
    /** Pointer to current buffer position. */
    char       *psz;
    /** Number of bytes left in the buffer - not including the trailing zero. */
    size_t      cch;
    /** Pointer to the start of the buffer. */
    char       *pszBuffer;
    /** The number of bytes in the buffer. */
    size_t      cchBuffer;
    /** Set if the buffer was allocated using RTMemRealloc(). If clear
     * pszBuffer points to the initial stack buffer. */
    bool        fAllocated;
    /** Allocation tag used for statistics and such. */
    const char *pszTag;
} STRALLOCARG;
/** Pointer to a strallocoutput() argument structure. */
typedef STRALLOCARG *PSTRALLOCARG;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(size_t) strallocoutput(void *pvArg, const char *pachChars, size_t cbChars);


/**
 * Output callback.
 *
 * @returns number of bytes written.
 * @param   pvArg       Pointer to a STRBUFARG structure.
 * @param   pachChars   Pointer to an array of utf-8 characters.
 * @param   cbChars     Number of bytes in the character array pointed to by pachChars.
 */
static DECLCALLBACK(size_t) strallocoutput(void *pvArg, const char *pachChars, size_t cbChars)
{
    PSTRALLOCARG    pArg = (PSTRALLOCARG)pvArg;
    if (pArg->psz)
    {
        /*
         * The fast path
         */
        if (cbChars <= pArg->cch)
        {
            if (cbChars)
            {
                memcpy(pArg->psz, pachChars, cbChars);
                pArg->cch -= cbChars;
                pArg->psz += cbChars;
            }
            *pArg->psz = '\0';
            return cbChars;
        }

        /*
         * Need to (re)allocate the buffer.
         */
        size_t cbAdded = RT_MIN(pArg->cchBuffer, _1M);
        if (cbAdded <= cbChars)
            cbAdded = RT_ALIGN_Z(cbChars, _4K);
        if (cbAdded <= _1G)
        {
            char *pszBuffer = (char *)RTMemReallocTag(pArg->fAllocated ? pArg->pszBuffer : NULL,
                                                      cbAdded + pArg->cchBuffer, pArg->pszTag);
            if (pszBuffer)
            {
                size_t off = pArg->psz - pArg->pszBuffer;
                if (!pArg->fAllocated)
                {
                    memcpy(pszBuffer, pArg->pszBuffer, off);
                    pArg->fAllocated = true;
                }

                pArg->pszBuffer = pszBuffer;
                pArg->cchBuffer += cbAdded;
                pArg->psz = pszBuffer + off;
                pArg->cch += cbAdded;

                if (cbChars)
                {
                    memcpy(pArg->psz, pachChars, cbChars);
                    pArg->cch -= cbChars;
                    pArg->psz += cbChars;
                }
                *pArg->psz = '\0';
                return cbChars;
            }
            /* else allocation failure */
        }
        /* else wrap around */

        /* failure */
        pArg->psz = NULL;
    }
    return 0;
}


RTDECL(int) RTStrAPrintfVTag(char **ppszBuffer, const char *pszFormat, va_list args, const char *pszTag)
{
#ifdef IN_RING3
    char            szBuf[2048];
#else
    char            szBuf[256];
#endif
    STRALLOCARG     Arg;
    Arg.fAllocated  = false;
    Arg.cchBuffer   = sizeof(szBuf);
    Arg.pszBuffer   = szBuf;
    Arg.cch         = sizeof(szBuf) - 1;
    Arg.psz         = szBuf;
    Arg.pszTag      = pszTag;
    szBuf[0] = '\0';
    int cbRet = (int)RTStrFormatV(strallocoutput, &Arg, NULL, NULL, pszFormat, args);
    if (Arg.psz)
    {
        if (!Arg.fAllocated)
        {
            /* duplicate the string in szBuf */
            Assert(Arg.pszBuffer == szBuf);
            char *psz = (char *)RTMemAllocTag(cbRet + 1, pszTag);
            if (psz)
                memcpy(psz, szBuf, cbRet + 1);
            *ppszBuffer = psz;
        }
        else
        {
            /* adjust the allocated buffer */
            char *psz = (char *)RTMemReallocTag(Arg.pszBuffer, cbRet + 1, pszTag);
            *ppszBuffer = psz ? psz : Arg.pszBuffer;
        }
    }
    else
    {
        /* allocation error */
        *ppszBuffer = NULL;
        cbRet = -1;

        /* free any allocated buffer */
        if (Arg.fAllocated)
            RTMemFree(Arg.pszBuffer);
    }

    return cbRet;
}
RT_EXPORT_SYMBOL(RTStrAPrintfVTag);


RTDECL(char *) RTStrAPrintf2VTag(const char *pszFormat, va_list args, const char *pszTag)
{
    char *pszBuffer;
    RTStrAPrintfVTag(&pszBuffer, pszFormat, args, pszTag);
    return pszBuffer;
}
RT_EXPORT_SYMBOL(RTStrAPrintf2VTag);

