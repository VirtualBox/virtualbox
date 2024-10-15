/* $Id: tstDeviceIoFuzz.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstDeviceSsmFuzz - I/O fuzzing testcase.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEFAULT /** @todo */
#include <VBox/types.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/fuzz.h>
#include <iprt/time.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/stream.h>

#include "tstDeviceBuiltin.h"
#include "tstDeviceCfg.h"
#include "tstDeviceInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


static const uint32_t g_aAccWidths[] = { 1, 2, 4, 8 };

static PCTSTDEVCFGITEM tstDevSsmFuzzGetCfgItem(PCTSTDEVCFGITEM paCfg, uint32_t cCfgItems, const char *pszName)
{
    for (uint32_t i = 0; i < cCfgItems; i++)
    {
        if (!RTStrCmp(paCfg[i].pszKey, pszName))
            return &paCfg[i];
    }

    return NULL;
}


static uint64_t tstDevSsmFuzzGetCfgU64(PCTSTDEVCFGITEM paCfg, uint32_t cCfgItems, const char *pszName)
{
    PCTSTDEVCFGITEM pCfgItem = tstDevSsmFuzzGetCfgItem(paCfg, cCfgItems, pszName);
    if (   pCfgItem
        && pCfgItem->enmType == TSTDEVCFGITEMTYPE_INTEGER)
        return (uint64_t)pCfgItem->u.i64;

    return 0;
}


/**
 * Entry point for the SSM fuzzer.
 *
 * @returns VBox status code.
 * @param   hDut                The device under test.
 * @param   paCfg               The testcase config.
 * @param   cCfgItems           Number of config items.
 */
static DECLCALLBACK(int) tstDevIoFuzzEntry(TSTDEVDUT hDut, PCTSTDEVCFGITEM paCfg, uint32_t cCfgItems)
{
    /* Determine the amount of I/O port handlers. */
    uint32_t cIoPortRegs = 0;
    PRTDEVDUTIOPORT pIoPort;
    RTListForEach(&hDut->LstIoPorts, pIoPort, RTDEVDUTIOPORT, NdIoPorts)
    {
        cIoPortRegs++;
    }

    /* Determine the amount of MMIO regions. */
    uint32_t cMmioRegions = 0;
    PRTDEVDUTMMIO pMmio;
    RTListForEach(&hDut->LstMmio, pMmio, RTDEVDUTMMIO, NdMmio)
    {
        cMmioRegions++;
    }

    RTRAND hRnd;
    int rc = RTRandAdvCreateParkMiller(&hRnd);
    if (RT_SUCCESS(rc))
    {
        RTRandAdvSeed(hRnd, 0x123456789);
        uint64_t cRuntimeMs = tstDevSsmFuzzGetCfgU64(paCfg, cCfgItems, "RuntimeSec") * RT_MS_1SEC_64;
        uint64_t tsStart = RTTimeMilliTS();
        uint64_t cFuzzedInputs = 0;
        RTCritSectEnter(&hDut->pDevIns->pCritSectRoR3->s.CritSect);
        do
        {
            bool fMmio = false;

            if (   cMmioRegions
                && !cIoPortRegs)
                fMmio = true;
            else if (   !cMmioRegions
                     && cIoPortRegs)
                fMmio = false;
            else
                fMmio = RT_BOOL(RTRandAdvU32Ex(hRnd, 0, 1));

            if (fMmio)
            {
                uint32_t iMmio = RTRandAdvU32Ex(hRnd, 0, cMmioRegions - 1);
                RTListForEach(&hDut->LstMmio, pMmio, RTDEVDUTMMIO, NdMmio)
                {
                    if (!iMmio)
                        break;
                    iMmio--;
                }

                uint32_t uMin = pMmio->pfnWriteR3 ? 0 : 1;
                uint32_t uMax = pMmio->pfnReadR3  ? 1 : 0;

                RTGCPHYS offRegion = RTRandAdvU64Ex(hRnd, 0, pMmio->cbRegion);
                bool fRead = RT_BOOL(uMin == uMax ? uMin : RTRandAdvU32Ex(hRnd, uMin, uMax));
                bool fRing0 = false;

                if (   (   fRead
                        && pMmio->pfnReadR0)
                    || (   !fRead
                        && pMmio->pfnWriteR0))
                    fRing0 = RT_BOOL(RTRandAdvU32Ex(hRnd, 0, 1));

                uint64_t u64Value = fRead ? 0 : RTRandAdvU64(hRnd);
                uint32_t cbValue = g_aAccWidths[RTRandAdvU32Ex(hRnd, 0, 3)];

                if (fRead)
                {
                    if (fRing0)
                    {
                        VBOXSTRICTRC rcStrict = pMmio->pfnReadR0((PPDMDEVINS)hDut->pDevInsR0, pIoPort->pvUserR0, offRegion, &u64Value, cbValue);
                        if (VBOXSTRICTRC_VAL(rcStrict) == VINF_IOM_R3_MMIO_READ)
                        {
                            AssertRelease(pMmio->pfnReadR3);
                            pMmio->pfnReadR3(hDut->pDevIns, pIoPort->pvUserR3, offRegion, &u64Value, cbValue);
                        }

                    }
                    else
                        pMmio->pfnReadR3(hDut->pDevIns, pMmio->pvUserR3, offRegion, &u64Value, cbValue);
                }
                else
                {
                    if (fRing0)
                    {
                        VBOXSTRICTRC rcStrict = pMmio->pfnWriteR0((PPDMDEVINS)hDut->pDevInsR0, pIoPort->pvUserR0, offRegion, &u64Value, cbValue);
                        if (VBOXSTRICTRC_VAL(rcStrict) == VINF_IOM_R3_MMIO_WRITE)
                        {
                            AssertRelease(pMmio->pfnWriteR3);
                            pMmio->pfnWriteR3(hDut->pDevIns, pIoPort->pvUserR3, offRegion, &u64Value, cbValue);
                        }

                    }
                    else
                        pMmio->pfnWriteR3(hDut->pDevIns, pMmio->pvUserR3, offRegion, &u64Value, cbValue);
                }
            }
            else
            {
                uint32_t iIoPort = RTRandAdvU32Ex(hRnd, 0, cIoPortRegs - 1);
                RTListForEach(&hDut->LstIoPorts, pIoPort, RTDEVDUTIOPORT, NdIoPorts)
                {
                    if (!iIoPort)
                        break;
                    iIoPort--;
                }

                uint32_t uMin = pIoPort->pfnOutR3 ? 0 : 1;
                uint32_t uMax = pIoPort->pfnInR3  ? 1 : 0;

                uint32_t offPort = RTRandAdvU32Ex(hRnd, 0, pIoPort->cPorts);
                bool fRead = RT_BOOL(uMin == uMax ? uMin : RTRandAdvU32Ex(hRnd, uMin, uMax));
                bool fRing0 = false;

                if (   (   fRead
                        && pIoPort->pfnInR0)
                    || (   !fRead
                        && pIoPort->pfnOutR3))
                    fRing0 = RT_BOOL(RTRandAdvU32Ex(hRnd, 0, 1));

                uint32_t u32Value = fRead ? 0 : RTRandAdvU32(hRnd);
                uint32_t cbValue = g_aAccWidths[RTRandAdvU32Ex(hRnd, 0, 2)];

                if (fRead)
                {
                    if (fRing0)
                    {
                        VBOXSTRICTRC rcStrict = pIoPort->pfnInR0((PPDMDEVINS)hDut->pDevInsR0, pIoPort->pvUserR0, offPort, &u32Value, cbValue);
                        if (VBOXSTRICTRC_VAL(rcStrict) == VINF_IOM_R3_IOPORT_READ)
                        {
                            AssertRelease(pIoPort->pfnInR3);
                            pIoPort->pfnInR3(hDut->pDevIns, pIoPort->pvUserR3, offPort, &u32Value, cbValue);
                        }

                    }
                    else
                        pIoPort->pfnInR3(hDut->pDevIns, pIoPort->pvUserR3, offPort, &u32Value, cbValue);
                }
                else
                {
                    if (fRing0)
                    {
                        VBOXSTRICTRC rcStrict = pIoPort->pfnOutR0((PPDMDEVINS)hDut->pDevInsR0, pIoPort->pvUserR0, offPort, u32Value, cbValue);
                        if (VBOXSTRICTRC_VAL(rcStrict) == VINF_IOM_R3_IOPORT_WRITE)
                        {
                            AssertRelease(pIoPort->pfnOutR3);
                            pIoPort->pfnOutR3(hDut->pDevIns, pIoPort->pvUserR3, offPort, u32Value, cbValue);
                        }

                    }
                    else
                        pIoPort->pfnOutR3(hDut->pDevIns, pIoPort->pvUserR3, offPort, u32Value, cbValue);
                }
            }

            cFuzzedInputs++;
        } while (   RT_SUCCESS(rc)
                 && RTTimeMilliTS() - tsStart < cRuntimeMs);
        RTCritSectLeave(&hDut->pDevIns->pCritSectRoR3->s.CritSect);

        RTPrintf("Fuzzed inputs: %u\n", cFuzzedInputs);
        RTRandAdvDestroy(hRnd);
    }

    return rc;
}


const TSTDEVTESTCASEREG g_TestcaseIoFuzz =
{
    /** szName */
    "IoFuzz",
    /** pszDesc */
    "Fuzzes devices I/O handlers",
    /** fFlags */
    0,
    /** pfnTestEntry */
    tstDevIoFuzzEntry
};

