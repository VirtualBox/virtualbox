/* $Id: tstRTNtPath-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTNtPath*.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/nt/nt-and-windows.h>

#include <iprt/errcore.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct TSTRAVERSE
{
    uint32_t        cHits;
    uint32_t        cFirstClassHits;
    uint32_t        cEntries;
    uint32_t        cDirs;
    UNICODE_STRING  UniStr;
    char            szLongPath[RTPATH_MAX];
    char            szLongPathNt[RTPATH_MAX];
    char            szShortPath[RTPATH_MAX];
    union
    {
        RTDIRENTRYEX    EntryEx;
        uint8_t         abBuf[RTPATH_MAX + sizeof(RTDIRENTRYEX)];
    } u;
    size_t          cbDirEntry;
} TSTRAVERSE;


void tstTraverse8dot3(TSTRAVERSE *pThis, size_t cchLong, size_t cchShort, uint32_t cShortNames)
{
    pThis->cDirs++;

    uint32_t cLeftToTest = 2;
    RTDIR  hDir;
    int rc = RTDirOpen(&hDir, pThis->szLongPath);
    if (RT_FAILURE(rc))
        return;
    while (pThis->cFirstClassHits < 256)
    {
        pThis->cbDirEntry = sizeof(pThis->u);
        rc = RTDirReadEx(hDir, &pThis->u.EntryEx, &pThis->cbDirEntry,  RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
        if (RT_FAILURE(rc))
            break;
        pThis->cEntries++;

        if (RTDirEntryExIsStdDotLink(&pThis->u.EntryEx))
            continue;

        if (   cchLong + pThis->u.EntryEx.cbName + 1 >= sizeof(pThis->szLongPath)
            || cchShort + RT_MAX(pThis->u.EntryEx.cbName, pThis->u.EntryEx.cwcShortName * 3) + 1 >= sizeof(pThis->szShortPath) )
            continue; /* ignore obvious overflows */

        bool fHave8Dot3 = pThis->u.EntryEx.cwcShortName
                       && RTUtf16ICmpUtf8(pThis->u.EntryEx.wszShortName, pThis->u.EntryEx.szName) != 0;
        if (   fHave8Dot3
            || RTFS_IS_DIRECTORY(pThis->u.EntryEx.Info.Attr.fMode)
            || cLeftToTest > 0)
        {
            if (!fHave8Dot3)
                memcpy(&pThis->szShortPath[cchShort], pThis->u.EntryEx.szName, pThis->u.EntryEx.cbName + 1);
            else
            {
                char *pszDst = &pThis->szShortPath[cchShort];
                rc = RTUtf16ToUtf8Ex(pThis->u.EntryEx.wszShortName, pThis->u.EntryEx.cwcShortName, &pszDst,
                                     sizeof(pThis->szShortPath) - cchShort, NULL);
                if (RT_FAILURE(rc))
                    continue;
            }
            memcpy(&pThis->szLongPath[cchLong], pThis->u.EntryEx.szName, pThis->u.EntryEx.cbName + 1);

            /*
             * Check it out.
             */
            HANDLE hRoot;
            RTTESTI_CHECK_RC(rc = RTNtPathFromWinUtf8(&pThis->UniStr, &hRoot, pThis->szShortPath), VINF_SUCCESS);
            if (RT_SUCCESS(rc))
            {
                RTTESTI_CHECK(pThis->UniStr.MaximumLength > pThis->UniStr.Length);
                RTTESTI_CHECK(pThis->UniStr.Length == RTUtf16Len(pThis->UniStr.Buffer) * sizeof(RTUTF16));

                RTTESTI_CHECK_RC(rc = RTNtPathEnsureSpace(&pThis->UniStr, RTPATH_MAX + 256), VINF_SUCCESS);
                if (RT_SUCCESS(rc))
                {
                    RTTESTI_CHECK_RC(rc = RTNtPathExpand8dot3Path(&pThis->UniStr, false /*fPathOnly*/), VINF_SUCCESS);
                    if (RT_SUCCESS(rc))
                    {
                        RTTESTI_CHECK(pThis->UniStr.Length == RTUtf16Len(pThis->UniStr.Buffer) * sizeof(RTUTF16));

                        /* Skip the win32 path prefix (it is usually \??\) so we can compare. Crude but works */
                        size_t offPrefix = 0;
                        while (   pThis->UniStr.Buffer[offPrefix] != pThis->szLongPath[0]
                               && pThis->UniStr.Buffer[offPrefix] != '\0')
                            offPrefix++;
                        if (!pThis->UniStr.Buffer[offPrefix])
                            offPrefix = 0;

                        if (RTUtf16CmpUtf8(&pThis->UniStr.Buffer[offPrefix], pThis->szLongPath) == 0)
                        { /* ok */ }
                        else if (RTUtf16ICmpUtf8(&pThis->UniStr.Buffer[offPrefix], pThis->szLongPath) == 0)
                            RTTestIFailed("case mismatch: '%ls' vs '%s'", pThis->UniStr.Buffer, pThis->szLongPath);
                        else
                            RTTestIFailed("mismatch: '%ls' vs '%s'", pThis->UniStr.Buffer, pThis->szLongPath);

                        /*
                         * Update test efficiency hits.
                         */
                        if (   cLeftToTest > 0
                            && !pThis->u.EntryEx.cwcShortName
                            && !RTFS_IS_DIRECTORY(pThis->u.EntryEx.Info.Attr.fMode))
                        {
                            cLeftToTest--;
                            if (cShortNames >= 3)
                                pThis->cFirstClassHits++;
                        }
                        pThis->cHits++;
                    }
                }
                RTNtPathFree(&pThis->UniStr, &hRoot);
            }
            //RTTestIPrintf(RTTESTLVL_ALWAYS, "debug: %u %u/%u %u/%u %s\n",
            //              cShortNames, pThis->cFirstClassHits, pThis->cHits, pThis->cDirs, pThis->cEntries, pThis->szShortPath);

            /*
             * Decend into sub-directories.  Must add slash first.
             */
            if (RTFS_IS_DIRECTORY(pThis->u.EntryEx.Info.Attr.fMode))
            {
                pThis->szLongPath[cchLong + pThis->u.EntryEx.cbName]     = '\\';
                pThis->szLongPath[cchLong + pThis->u.EntryEx.cbName + 1] = '\0';
                strcat(&pThis->szShortPath[cchShort], "\\");

                tstTraverse8dot3(pThis,
                                 cchLong + pThis->u.EntryEx.cbName + 1,
                                 cchShort + strlen(&pThis->szShortPath[cchShort]),
                                 cShortNames + (pThis->u.EntryEx.cwcShortName != 0));
            }
        }
    }
    RTDirClose(hDir);
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTNtPath-1", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * Traverse the boot file system looking for short named and try locate an instance
     * where we have at least 3 in a row.
     */
    RTTestSub(hTest, "8dot3");

    TSTRAVERSE This;
    RT_ZERO(This);
    rc = RTEnvGetEx(RTENV_DEFAULT, "SystemDrive", This.szLongPath, 64, NULL);
    if (RT_SUCCESS(rc))
    {
        RTStrCat(This.szLongPath, sizeof(This.szLongPath), "\\");
        size_t cch = strlen(This.szLongPath);
        memcpy(This.szShortPath, This.szLongPath, cch + 1);

        tstTraverse8dot3(&This, cch, cch, 0);
        RTTestIPrintf(RTTESTLVL_ALWAYS, "info: cEntries=%u cHits=%u cFirstClassHits=%u\n",
                      This.cEntries, This.cHits, This.cFirstClassHits);
    }
    else
        RTTestSkipped(hTest, "failed to resolve %SystemDrive%: %Rrc",  rc);

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

