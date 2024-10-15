/* $Id: bs3-cmn-PitIrqHandler.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - The PIT IRQ Handler and associated data.
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
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if ARCH_BITS == 16
/** The RIP/EIP value of where the PIT IRQ handle will return to. */
BS3REG volatile     g_Bs3PitIrqRip;
/** Nano seconds (approx) since last the PIT timer was started. */
uint64_t volatile   g_cBs3PitNs    = 0;
/** Milliseconds seconds (very approx) since last the PIT timer was started. */
uint64_t volatile   g_cBs3PitMs    = 0;
/** Number of ticks since last the PIT timer was started.  */
uint32_t volatile   g_cBs3PitTicks = 0;
/** The current interval in nanon seconds.  */
uint32_t            g_cBs3PitIntervalNs = 0;
/** The current interval in milliseconds (approximately).
 * This is 0 if not yet started (used for checking the state internally). */
uint16_t            g_cBs3PitIntervalMs = 0;
/** The current PIT frequency (approximately).  0 if not yet started.  */
uint16_t volatile   g_cBs3PitIntervalHz = 0;
#endif


BS3_DECL_NEAR_CALLBACK(void) BS3_CMN_NM(bs3PitIrqHandler)(PBS3TRAPFRAME pTrapFrame)
{
    if (g_cBs3PitIntervalHz)
    {
        g_cBs3PitMs += g_cBs3PitIntervalMs;
        g_cBs3PitNs += g_cBs3PitIntervalNs;
        g_cBs3PitTicks++;
        g_Bs3PitIrqRip.u = pTrapFrame->Ctx.rip.u;
    }
    NOREF(pTrapFrame);
    ASMOutU8(0x20, 0x20); /** @todo function! */
}

