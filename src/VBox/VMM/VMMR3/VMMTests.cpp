/* $Id: VMMTests.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM - The Virtual Machine Monitor Core, Tests.
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

//#define NO_SUPCALLR0VMM


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_VMM
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/cpum.h>
#include <VBox/dbg.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/selm.h>
#include "VMMInternal.h"
#include <VBox/vmm/vm.h>
#include <iprt/errcore.h>
#include <VBox/param.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/time.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/x86.h>


#define SYNC_SEL(pHyperCtx, reg)                                                        \
        if (pHyperCtx->reg.Sel)                                                         \
        {                                                                               \
            DBGFSELINFO selInfo;                                                        \
            int rc2 = SELMR3GetShadowSelectorInfo(pVM, pHyperCtx->reg.Sel, &selInfo);   \
            AssertRC(rc2);                                                              \
                                                                                        \
            pHyperCtx->reg.u64Base              = selInfo.GCPtrBase;                    \
            pHyperCtx->reg.u32Limit             = selInfo.cbLimit;                      \
            pHyperCtx->reg.Attr.n.u1Present     = selInfo.u.Raw.Gen.u1Present;          \
            pHyperCtx->reg.Attr.n.u1DefBig      = selInfo.u.Raw.Gen.u1DefBig;           \
            pHyperCtx->reg.Attr.n.u1Granularity = selInfo.u.Raw.Gen.u1Granularity;      \
            pHyperCtx->reg.Attr.n.u4Type        = selInfo.u.Raw.Gen.u4Type;             \
            pHyperCtx->reg.Attr.n.u2Dpl         = selInfo.u.Raw.Gen.u2Dpl;              \
            pHyperCtx->reg.Attr.n.u1DescType    = selInfo.u.Raw.Gen.u1DescType;         \
            pHyperCtx->reg.Attr.n.u1Long        = selInfo.u.Raw.Gen.u1Long;             \
        }

/* execute the switch. */
VMMR3DECL(int) VMMDoHmTest(PVM pVM)
{
#if 1
    RTPrintf("FIXME!\n");
    RT_NOREF(pVM);
    return 0;
#else

    uint32_t i;
    int      rc;
    PCPUMCTX pHyperCtx, pGuestCtx;
    RTGCPHYS CR3Phys = 0x0; /* fake address */
    PVMCPU   pVCpu = &pVM->aCpus[0];

    if (!HMIsEnabled(pVM))
    {
        RTPrintf("VMM: Hardware accelerated test not available!\n");
        return VERR_ACCESS_DENIED;
    }

    /* Enable mapping of the hypervisor into the shadow page table. */
    uint32_t cb;
    rc = PGMR3MappingsSize(pVM, &cb);
    AssertRCReturn(rc, rc);

    /* Pretend the mappings are now fixed; to force a refresh of the reserved PDEs. */
    rc = PGMR3MappingsFix(pVM, MM_HYPER_AREA_ADDRESS, cb);
    AssertRCReturn(rc, rc);

    pHyperCtx = CPUMGetHyperCtxPtr(pVCpu);

    pHyperCtx->cr0 = X86_CR0_PE | X86_CR0_WP | X86_CR0_PG | X86_CR0_TS | X86_CR0_ET | X86_CR0_NE | X86_CR0_MP;
    pHyperCtx->cr4 = X86_CR4_PGE | X86_CR4_OSFXSR | X86_CR4_OSXMMEEXCPT;
    PGMChangeMode(pVCpu, pHyperCtx->cr0, pHyperCtx->cr4, pHyperCtx->msrEFER);
    PGMSyncCR3(pVCpu, pHyperCtx->cr0, CR3Phys, pHyperCtx->cr4, true);

    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_TO_R3);
    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_TIMER);
    VM_FF_CLEAR(pVM, VM_FF_TM_VIRTUAL_SYNC);
    VM_FF_CLEAR(pVM, VM_FF_REQUEST);

    /*
     * Setup stack for calling VMMRCEntry().
     */
    RTRCPTR RCPtrEP;
    rc = PDMR3LdrGetSymbolRC(pVM, VMMRC_MAIN_MODULE_NAME, "VMMRCEntry", &RCPtrEP);
    if (RT_SUCCESS(rc))
    {
        RTPrintf("VMM: VMMRCEntry=%RRv\n", RCPtrEP);

        pHyperCtx = CPUMGetHyperCtxPtr(pVCpu);

        /* Fill in hidden selector registers for the hypervisor state. */
        SYNC_SEL(pHyperCtx, cs);
        SYNC_SEL(pHyperCtx, ds);
        SYNC_SEL(pHyperCtx, es);
        SYNC_SEL(pHyperCtx, fs);
        SYNC_SEL(pHyperCtx, gs);
        SYNC_SEL(pHyperCtx, ss);
        SYNC_SEL(pHyperCtx, tr);

        /*
         * Profile switching.
         */
        RTPrintf("VMM: profiling switcher...\n");
        Log(("VMM: profiling switcher...\n"));
        uint64_t TickMin = UINT64_MAX;
        uint64_t tsBegin = RTTimeNanoTS();
        uint64_t TickStart = ASMReadTSC();
        for (i = 0; i < 1000000; i++)
        {
            CPUMSetHyperState(pVCpu, pVM->vmm.s.pfnCallTrampolineRC, pVCpu->vmm.s.pbEMTStackBottomRC, 0, 0);
            CPUMPushHyper(pVCpu, 0);
            CPUMPushHyper(pVCpu, VMMRC_DO_TESTCASE_HM_NOP);
            CPUMPushHyper(pVCpu, pVM->pVMRC);
            CPUMPushHyper(pVCpu, 3 * sizeof(RTRCPTR));    /* stack frame size */
            CPUMPushHyper(pVCpu, RCPtrEP);                /* what to call */

            pHyperCtx = CPUMGetHyperCtxPtr(pVCpu);
            pGuestCtx = CPUMQueryGuestCtxPtr(pVCpu);

            /* Copy the hypervisor context to make sure we have a valid guest context. */
            *pGuestCtx = *pHyperCtx;
            pGuestCtx->cr3 = CR3Phys;

            VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_TO_R3);
            VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_TIMER);
            VM_FF_CLEAR(pVM, VM_FF_TM_VIRTUAL_SYNC);

            uint64_t TickThisStart = ASMReadTSC();
            rc = SUPR3CallVMMR0Fast(pVM->pVMR0, VMMR0_DO_HM_RUN, 0);
            uint64_t TickThisElapsed = ASMReadTSC() - TickThisStart;
            if (RT_FAILURE(rc))
            {
                Log(("VMM: R0 returned fatal %Rrc in iteration %d\n", rc, i));
                VMMR3FatalDump(pVM, pVCpu, rc);
                return rc;
            }
            if (TickThisElapsed < TickMin)
                TickMin = TickThisElapsed;
        }
        uint64_t TickEnd = ASMReadTSC();
        uint64_t tsEnd = RTTimeNanoTS();

        uint64_t Elapsed = tsEnd - tsBegin;
        uint64_t PerIteration = Elapsed / (uint64_t)i;
        uint64_t cTicksElapsed = TickEnd - TickStart;
        uint64_t cTicksPerIteration = cTicksElapsed / (uint64_t)i;

        RTPrintf("VMM: %8d cycles     in %11llu ns (%11lld ticks),  %10llu ns/iteration (%11lld ticks)  Min %11lld ticks\n",
                 i, Elapsed, cTicksElapsed, PerIteration, cTicksPerIteration, TickMin);
        Log(("VMM: %8d cycles     in %11llu ns (%11lld ticks),  %10llu ns/iteration (%11lld ticks)  Min %11lld ticks\n",
             i, Elapsed, cTicksElapsed, PerIteration, cTicksPerIteration, TickMin));

        rc = VINF_SUCCESS;
    }
    else
        AssertMsgFailed(("Failed to resolved VMMRC.rc::VMMRCEntry(), rc=%Rrc\n", rc));

    return rc;
#endif
}

