/* $Id: bs3-cmn-pit.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - PIT Setup and Disable code.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include "bs3kit-template-header.h"
#include <iprt/asm-amd64-x86.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define BS3_PIT_PORT_CMD        0x43
#define BS3_PIT_PORT_CH0_DATA   0x40
#define BS3_PIT_HZ              UINT32_C(1193182)


/*********************************************************************************************************************************
*   External Symbols                                                                                                             *
*********************************************************************************************************************************/
extern FNBS3TRAPHANDLER16 bs3PitIrqHandler_c16;
extern FNBS3TRAPHANDLER32 bs3PitIrqHandler_c32;
extern FNBS3TRAPHANDLER64 bs3PitIrqHandler_c64;


#undef Bs3PitSetupAndEnablePeriodTimer
BS3_CMN_DEF(void, Bs3PitSetupAndEnablePeriodTimer,(uint16_t cHzDesired))
{
    RTCCUINTREG fSaved;
    uint16_t    cCount;
    uint16_t    cMsInterval;
    uint32_t    cNsInterval;

    /*
     * Disable the PIT and make sure we've configured the IRQ handlers.
     */
    Bs3PitDisable();
    Bs3PicSetup(false /*fForcedReInit*/);
    Bs3TrapSetHandlerEx(0x70, bs3PitIrqHandler_c16, bs3PitIrqHandler_c32, bs3PitIrqHandler_c64);

    /*
     * Reset the counters and IRQ PC.
     */
    g_cBs3PitNs         = 0;
    g_cBs3PitMs         = 0;
    g_cBs3PitTicks      = 0;
    g_Bs3PitIrqRip.u    = 0;

    /*
     * Calculate an interval.
     */
    if (cHzDesired <= 18)
    {
        cCount      = 0;                    /* 1193182 / 65536 = 18.206512451171875  Hz */
        cHzDesired  = 18;
        cNsInterval = UINT32_C(54925401);   /* 65536 / 1193182 = 0.054925401154224586022920225078823 seconds */
        cMsInterval = 55;
    }
    else
    {
        cCount      = BS3_PIT_HZ / cHzDesired;
        cHzDesired  = BS3_PIT_HZ / cCount;
        /* 1s/1193182 = 0.000 000 838 095 110 38550698887512550474278 */
#if ARCH_BITS == 64
        cNsInterval = cCount * UINT64_C(838095110) / 1000000;
#elif ARCH_BITS == 32
        cNsInterval = cCount * UINT32_C(8381) / 10;
#else
        cNsInterval = cCount * 838;
#endif
        if (cCount <= 1194)
            cMsInterval = 1; /* Must not be zero! */
        else
            cMsInterval = cCount / 1194;
    }


    /*
     * Do the reprogramming.
     */
    fSaved = ASMIntDisableFlags();
    ASMOutU8(BS3_PIT_PORT_CMD,
               (0 << 6) /* select:      channel 0 */
             | (3 << 4) /* access mode: lobyte/hibyte */
             | (2 << 1) /* operation:   Mode 2 */
             | 0        /* binary mode */
             );
    ASMOutU8(BS3_PIT_PORT_CH0_DATA, (uint8_t)cCount);
    ASMOutU8(BS3_PIT_PORT_CH0_DATA, (uint8_t)(cCount >> 8));

    g_cBs3PitIntervalNs = cNsInterval;
    g_cBs3PitIntervalHz = cHzDesired;
    g_cBs3PitIntervalMs = cMsInterval;

    Bs3PicUpdateMask(UINT16_C(0xfffe), 0);

    ASMSetFlags(fSaved);
}


#undef Bs3PitDisable
BS3_CMN_DEF(void, Bs3PitDisable,(void))
{
    if (g_cBs3PitIntervalHz != 0)
    {
        RTCCUINTREG fSaved = ASMIntDisableFlags();

        /*
         * Not entirely sure what's the best way to do this, but let's try reprogram
         * it to a no-reload mode like 0 and set the count to 1.
         */
        g_cBs3PitIntervalMs = 0;
        ASMOutU8(BS3_PIT_PORT_CMD,
                   (0 << 6) /* select:      channel 0 */
                 | (1 << 4) /* access mode: lobyte */
                 | (0 << 1) /* operation:   Mode 0 */
                 | 0        /* binary mode */
                 );
        ASMOutU8(BS3_PIT_PORT_CH0_DATA, (uint8_t)1);

        /*
         * Then mask the PIT IRQ on the PIC.
         */
        Bs3PicUpdateMask(UINT16_C(0xffff), 1);

        ASMSetFlags(fSaved);
    }

    /*
     * Reset the interval values (leave the ticks and elapsed ns/ms values as-is).
     */
    g_cBs3PitIntervalNs = 0;
    g_cBs3PitIntervalMs = 0;
    g_cBs3PitIntervalHz = 0;
}

