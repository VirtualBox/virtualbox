/* $Id: tstLog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Log Groups.
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
#include <iprt/log.h>
#include <iprt/string.h>
#include <iprt/test.h>
#ifdef VBOX
# include <VBox/log.h>
#endif


/** Differs from normal strcmp in that '_' is considered smaller than
 * alphanumerical characters. */
static int CompareLogGroups(const char *psz1, const char *psz2)
{
    for (;;)
    {
        char ch1 = *psz1++;
        char ch2 = *psz2++;
        if (ch1 != ch2)
        {
            if (ch1 == 0)
                return -1;
            if (ch2 == 0)
                return 1;
            if (ch1 == '_')
                ch1 = 1;
            if (ch2 == '_')
                ch2 = 1;
            return ch1 < ch2 ? -1 : 1;
        }
        if (ch1 == 0)
            return 0;
    }
}

int main()
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstLog", &hTest);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
#if 0 /* Old tests: */
        RTTestIPrintf(RTTESTLVL_ALWAYS, "Requires manual inspection of the log output!\n");
        RTLogPrintf("%%Rrc %d: %Rrc\n", VERR_INVALID_PARAMETER, VERR_INVALID_PARAMETER);
        RTLogPrintf("%%Rrs %d: %Rrs\n", VERR_INVALID_PARAMETER, VERR_INVALID_PARAMETER);
        RTLogPrintf("%%Rrf %d: %Rrf\n", VERR_INVALID_PARAMETER, VERR_INVALID_PARAMETER);
        RTLogPrintf("%%Rra %d: %Rra\n", VERR_INVALID_PARAMETER, VERR_INVALID_PARAMETER);

        static uint8_t au8Hex[256];
        for (unsigned iHex = 0; iHex < sizeof(au8Hex); iHex++)
            au8Hex[iHex] = (uint8_t)iHex;
        RTLogPrintf("%%Rhxs   : %Rhxs\n", &au8Hex[0]);
        RTLogPrintf("%%.32Rhxs: %.32Rhxs\n", &au8Hex[0]);

        RTLogPrintf("%%Rhxd   :\n%Rhxd\n", &au8Hex[0]);
        RTLogPrintf("%%.64Rhxd:\n%.64Rhxd\n", &au8Hex[0]);
        RTLogPrintf("%%.*Rhxd:\n%.*Rhxd\n", 64, &au8Hex[0]);
        RTLogPrintf("%%32.256Rhxd : \n%32.256Rhxd\n", &au8Hex[0]);
        RTLogPrintf("%%32.*Rhxd : \n%32.*Rhxd\n", 256, &au8Hex[0]);
        RTLogPrintf("%%7.32Rhxd : \n%7.32Rhxd\n", &au8Hex[0]);
        RTLogPrintf("%%7.*Rhxd : \n%7.*Rhxd\n", 32, &au8Hex[0]);
        RTLogPrintf("%%*.*Rhxd : \n%*.*Rhxd\n", 7, 32, &au8Hex[0]);

        RTLogPrintf("%%RGp: %RGp\n", (RTGCPHYS)0x87654321);
        RTLogPrintf("%%RGv: %RGv\n", (RTGCPTR)0x87654321);
        RTLogPrintf("%%RHp: %RHp\n", (RTGCPHYS)0x87654321);
        RTLogPrintf("%%RHv: %RHv\n", (RTGCPTR)0x87654321);

        RTLogPrintf("%%RI8 : %RI8\n", (uint8_t)88);
        RTLogPrintf("%%RI16: %RI16\n", (uint16_t)16016);
        RTLogPrintf("%%RI32: %RI32\n", _1G);
        RTLogPrintf("%%RI64: %RI64\n", _1E);

        RTLogPrintf("%%RU8 : %RU8\n", (uint8_t)88);
        RTLogPrintf("%%RU16: %RU16\n", (uint16_t)16016);
        RTLogPrintf("%%RU32: %RU32\n", _2G32);
        RTLogPrintf("%%RU64: %RU64\n", _2E);

        RTLogPrintf("%%RX8 : %RX8 %#RX8\n",   (uint8_t)88, (uint8_t)88);
        RTLogPrintf("%%RX16: %RX16 %#RX16\n", (uint16_t)16016, (uint16_t)16016);
        RTLogPrintf("%%RX32: %RX32 %#RX32\n", _2G32, _2G32);
        RTLogPrintf("%%RX64: %RX64 %#RX64\n", _2E, _2E);

        RTLogFlush(NULL);

        /* Flush tests (assumes _4K log buffer). */
        uint32_t const cbLogBuf = _4K;
        static char    s_szBuf[cbLogBuf * 4];
        RTLogChangeFlags(NULL, RTLOGFLAGS_USECRLF, 0);
        for (uint32_t i = cbLogBuf - 512; i < cbLogBuf + 512; i++)
        {
            memset(s_szBuf, '0' + (i % 10), i);
            s_szBuf[i] = '\n';
            s_szBuf[i + 1] = '\0';
            RTLogPrintf("i=%#08x: %s", i, s_szBuf);
            RTLogFlush(NULL);
        }
#endif

        /*
         * Check the groups.
         */
#ifdef VBOX
        static const char                                               *s_apszGroups[] = VBOX_LOGGROUP_NAMES;
        static const struct { uint16_t idGroup; const char *pszGroup;  } s_aGroupEnumValues[] =
        {
# include "tstLogGroups.h"
        };

        for (size_t iVal = 0, iGrp = RTLOGGROUP_FIRST_USER + 1; iVal < RT_ELEMENTS(s_aGroupEnumValues); iVal++, iGrp++)
        {
            if (iGrp >= RT_ELEMENTS(s_apszGroups))
            {
                RTTestIFailed("iGrp=%zu >= RT_ELEMENTS(s_apszGroups)=%zu\n", iGrp, RT_ELEMENTS(s_apszGroups));
                break;
            }
            if (strcmp(s_apszGroups[iGrp], s_aGroupEnumValues[iVal].pszGroup))
                RTTestIFailed("iGrp=%zu mismatch: %s vs %s\n", iGrp, s_apszGroups[iGrp], s_aGroupEnumValues[iVal].pszGroup);
            if (   iVal > 0
                && CompareLogGroups(s_aGroupEnumValues[iVal].pszGroup, s_aGroupEnumValues[iVal - 1].pszGroup) <= 0)
                RTTestIFailed("iGrp=%zu wrong order: %s, prev %s\n",
                              iGrp, s_aGroupEnumValues[iVal].pszGroup, s_aGroupEnumValues[iVal - 1].pszGroup);
            if (   iVal > 0
                && s_aGroupEnumValues[iVal - 1].idGroup + 1 != s_aGroupEnumValues[iVal].idGroup)
                RTTestIFailed("Enum values jumped - bad log.h sed: %u -> %u; %s and %s\n",
                              s_aGroupEnumValues[iVal - 1].idGroup, s_aGroupEnumValues[iVal].idGroup,
                              s_aGroupEnumValues[iVal - 1].pszGroup, s_aGroupEnumValues[iVal].pszGroup);
        }
#endif
        rcExit = RTTestSummaryAndDestroy(hTest);
    }
    return rcExit;
}

