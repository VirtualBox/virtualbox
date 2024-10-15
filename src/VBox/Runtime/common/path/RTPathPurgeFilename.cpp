/* $Id: RTPathPurgeFilename.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathPurgeFilename
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
#include <iprt/path.h>

#include <iprt/assert.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Character mappings for translating a string into a valid Windows or OS/2 filename. */
static const unsigned char g_auchWinOs2Map[256] =
{
      0, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95,
     32, 33, 95, 35, 36, 37, 38, 39, 40, 41, 95, 43, 44, 45, 46, 95, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 95, 59, 95, 61, 95, 95,
     64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 95, 93, 94, 95,
     96, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123, 95,125,126, 95,
    128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
    160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
    192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
    224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
};

/* Program for generating the table:
#include <stdio.h>
#include <string.h>
int main()
{
    int i;
    printf("static const unsigned char g_auchWinOs2Map[256] =\n"
           "{");
    for (i = 0; i < 256; i++)
    {
        int ch = i;
        if (   i <= 31
            || i == 127
            || (i < 127 && strchr("/\\*:<>?|\"",  (char)i) != NULL))
            ch = i > 0 ? '_' : 0;
        if (i == 0)
            printf("\n    %3d", ch);
        else if ((i % 32) == 0)
            printf(",\n    %3d", ch);
        else
            printf(",%3d", ch);
    }
    printf("\n"
           "};\n");
    return 0;
}
*/


RTDECL(char *) RTPathPurgeFilename(char *pszString, uint32_t fFlags)
{
    AssertPtrReturn(pszString, NULL);
    Assert(RTPATH_STR_F_IS_VALID(fFlags, 0));

    /*
     * Take action according to the style after first resolving the host style.
     */
    if ((fFlags & RTPATH_STR_F_STYLE_MASK) == RTPATH_STR_F_STYLE_HOST)
        fFlags = (fFlags & ~RTPATH_STR_F_STYLE_MASK) | RTPATH_STYLE;
    if ((fFlags & RTPATH_STR_F_STYLE_MASK) == RTPATH_STR_F_STYLE_DOS)
    {
        /*
         * Produce a filename valid on Windows and OS/2.
         * Here all control characters (including tab) and path separators needs to
         * be replaced, in addition to a scattering of other ones.
         */
        unsigned char *puch = (unsigned char *)pszString;
        uintptr_t      uch;
        while ((uch = *puch))
            *puch++ = g_auchWinOs2Map[uch];
    }
    else
    {
        /*
         * Produce a filename valid on a (typical) Unix system.
         * Here only the '/' needs to be replaced.
         */
        Assert((fFlags & RTPATH_STR_F_STYLE_MASK) == RTPATH_STR_F_STYLE_UNIX);
        char *pszSlash = strchr(pszString, '/');
        while (pszSlash != NULL)
        {
            *pszSlash = '_';
            pszSlash = strchr(pszSlash + 1, '/');
        }
    }
    return pszString;
}

