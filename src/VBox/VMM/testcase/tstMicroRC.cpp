/* $Id: tstMicroRC.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Micro Testcase, profiling special CPU operations - GC Code (hacks).
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/selm.h>
#include "tstMicro.h"

#include <iprt/errcore.h>
#include <iprt/asm-amd64-x86.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
DECLEXPORT(int) tstMicroRC(PTSTMICRO pTst, unsigned uTestcase);
RT_C_DECLS_END


/**
 * Save and load our IDT.
 *
 * @param   pTst    Pointer to the instance data.
 * @param   iIDT    The index of the IDT entry which should be hooked.
 */
void idtInstall(PTSTMICRO pTst, int iIDT)
{
    RTIDTR Idtr;
    ASMGetIDTR(&Idtr);
    if (Idtr.pIdt == (uintptr_t)&pTst->aIDT[0])
        return;
    pTst->OriginalIDTR.cbIdt = Idtr.cbIdt;
    pTst->OriginalIDTR.pIdt  = Idtr.pIdt;

    /*
     * Copy the IDT.
     */
    if (Idtr.cbIdt >= sizeof(pTst->aIDT))
        Idtr.cbIdt = sizeof(pTst->aIDT) - 1;
    memcpy(&pTst->aIDT[0], (void *)Idtr.pIdt, Idtr.cbIdt + 1);


    /* Hook up IDT entry. */
    if (iIDT >= 0)
    {
        uintptr_t uHandler = (uintptr_t)tstTrapHandlerNoErr;
        if (    iIDT == 8
            ||  iIDT == 0xa
            ||  iIDT == 0xb
            ||  iIDT == 0xc
            ||  iIDT == 0xd
            ||  iIDT == 0xe
            ||  iIDT == 0x11)
            uHandler = (uintptr_t)tstTrapHandler;
        pTst->aIDT[iIDT].Int.u16OffsetHigh  = uHandler >> 16;
        pTst->aIDT[iIDT].Int.u16OffsetLow   = uHandler & 0xffff;
        pTst->aIDT[iIDT].Int.u16SegSel      = SELMGetHyperCS(&g_VM);
        pTst->aIDT[iIDT].Int.u2DPL          = 3;
        pTst->aIDT[iIDT].Int.u1Present      = 1;
        pTst->aIDT[iIDT].Int.u1Fixed0       = 0;
        pTst->aIDT[iIDT].Int.u1Fixed1       = 0;
        pTst->aIDT[iIDT].Int.u1Fixed2       = 0;
        pTst->aIDT[iIDT].Int.u1Fixed3       = 0;
        pTst->aIDT[iIDT].Int.u1Fixed4       = 1;
        pTst->aIDT[iIDT].Int.u1Fixed5       = 1;
        pTst->aIDT[iIDT].Int.u132BitGate    = 1;
        pTst->aIDT[iIDT].Int.u1Fixed6       = 0;
        pTst->aIDT[iIDT].Int.u5Reserved2    = 0;
    }

    /* Install int 42h, R3 gate */
    pTst->aIDT[0x42].Int.u16OffsetHigh  = (uintptr_t)tstInterrupt42 >> 16;
    pTst->aIDT[0x42].Int.u16OffsetLow   = (uintptr_t)tstInterrupt42 & 0xffff;
    pTst->aIDT[0x42].Int.u16SegSel      = SELMGetHyperCS(&g_VM);
    pTst->aIDT[0x42].Int.u2DPL          = 3;
    pTst->aIDT[0x42].Int.u1Present      = 1;
    pTst->aIDT[0x42].Int.u1Fixed0       = 0;
    pTst->aIDT[0x42].Int.u1Fixed1       = 0;
    pTst->aIDT[0x42].Int.u1Fixed2       = 0;
    pTst->aIDT[0x42].Int.u1Fixed3       = 0;
    pTst->aIDT[0x42].Int.u1Fixed4       = 1;
    pTst->aIDT[0x42].Int.u1Fixed5       = 1;
    pTst->aIDT[0x42].Int.u132BitGate    = 1;
    pTst->aIDT[0x42].Int.u1Fixed6       = 0;
    pTst->aIDT[0x42].Int.u5Reserved2    = 0;

    /*
     * Load our IDT.
     */
    Idtr.pIdt = (uintptr_t)&pTst->aIDT[0];
    ASMSetIDTR(&Idtr);

    RTIDTR Idtr2;
    ASMGetIDTR(&Idtr2);
    Assert(Idtr2.pIdt == (uintptr_t)&pTst->aIDT[0]);
}


/**
 * Removes all trap overrides except for gate 42.
 */
DECLASM(void) idtOnly42(PTSTMICRO pTst)
{
    if (pTst->OriginalIDTR.pIdt)
        memcpy(&pTst->aIDT[0], (void *)(uintptr_t)pTst->OriginalIDTR.pIdt, sizeof(VBOXIDTE) * 32);
}



DECLEXPORT(int) tstMicroRC(PTSTMICRO pTst, unsigned uTestcase)
{
    RTLogPrintf("pTst=%p uTestcase=%d\n", pTst, uTestcase);

    /*
     * Validate input.
     */
    if (uTestcase >= TSTMICROTEST_MAX)
        return VERR_INVALID_PARAMETER;

    /*
     * Clear the results.
     */
    pTst->u64TSCR0Start = 0;
    pTst->u64TSCRxStart = 0;
    pTst->u64TSCR0Enter = 0;
    pTst->u64TSCR0Exit = 0;
    pTst->u64TSCRxEnd = 0;
    pTst->u64TSCR0End = 0;
    pTst->cHits = 0;
    pTst->offEIPAdd = 0;
    pTst->u32CR2 = 0;
    pTst->u32EIP = 0;
    pTst->u32ErrCd = 0;
    PTSTMICRORESULT pRes = &pTst->aResults[uTestcase];
    memset(&pTst->aResults[uTestcase], 0, sizeof(pTst->aResults[uTestcase]));


    /*
     * Do the testcase.
     */
    int rc = VINF_SUCCESS;
    switch (uTestcase)
    {
        case TSTMICROTEST_OVERHEAD:
        {
            tstOverhead(pTst);
            break;
        }

        case TSTMICROTEST_INVLPG_0:
        {
            tstInvlpg0(pTst);
            break;
        }

        case TSTMICROTEST_INVLPG_EIP:
        {
            tstInvlpgEIP(pTst);
            break;
        }

        case TSTMICROTEST_INVLPG_ESP:
        {
            tstInvlpgESP(pTst);
            break;
        }

        case TSTMICROTEST_CR3_RELOAD:
        {
            tstCR3Reload(pTst);
            break;
        }

        case TSTMICROTEST_WP_DISABLE:
        {
            tstWPDisable(pTst);
            break;
        }

        case TSTMICROTEST_WP_ENABLE:
        {
            tstWPEnable(pTst);
            break;
        }

        case TSTMICROTEST_PF_R0:
        {
            idtInstall(pTst, 0xe);
            pTst->offEIPAdd = 2;
            rc = tstPFR0(pTst);
            break;
        }

        case TSTMICROTEST_PF_R1:
        {
            idtInstall(pTst, 0xe);
            pTst->offEIPAdd = 2;
            rc = tstPFR1(pTst);
            break;
        }

        case TSTMICROTEST_PF_R2:
        {
            idtInstall(pTst, 0xe);
            pTst->offEIPAdd = 2;
            rc = tstPFR2(pTst);
            break;
        }

        case TSTMICROTEST_PF_R3:
        {
            idtInstall(pTst, 0xe);
            pTst->offEIPAdd = 2;
            rc = tstPFR3(pTst);
            break;
        }

    }

    /*
     * Compute the results.
     */
    if (pTst->u64TSCR0End   && pTst->u64TSCR0Start)
        pRes->cTotalTicks       = pTst->u64TSCR0End     - pTst->u64TSCR0Start   - pTst->u64Overhead;
    if (pTst->u64TSCRxStart && pTst->u64TSCR0Start)
        pRes->cToRxFirstTicks   = pTst->u64TSCRxStart   - pTst->u64TSCR0Start   - pTst->u64Overhead;
    if (pTst->u64TSCR0Enter && pTst->u64TSCRxStart)
        pRes->cTrapTicks        = pTst->u64TSCR0Enter   - pTst->u64TSCRxStart   - pTst->u64Overhead;
    if (pTst->u64TSCRxEnd   && pTst->u64TSCR0Exit)
        pRes->cToRxTrapTicks    = pTst->u64TSCRxEnd     - pTst->u64TSCR0Exit    - pTst->u64Overhead;
    if (pTst->u64TSCR0End   && pTst->u64TSCRxEnd)
        pRes->cToR0Ticks        = pTst->u64TSCR0End     - pTst->u64TSCRxEnd     - pTst->u64Overhead;

    return rc;
}

