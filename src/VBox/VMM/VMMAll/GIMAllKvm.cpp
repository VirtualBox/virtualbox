/* $Id: GIMAllKvm.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIM - Guest Interface Manager, KVM, All Contexts.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_GIM
#include <VBox/vmm/gim.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/tm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmapi.h>
#include "GIMKvmInternal.h"
#include "GIMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/dis.h>
#include <VBox/err.h>
#include <VBox/sup.h>

#include <iprt/time.h>


/**
 * Handles the KVM hypercall.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_SUCCESS if the hypercall succeeded (even if its operation
 *          failed).
 * @retval  VINF_GIM_R3_HYPERCALL re-start the hypercall from ring-3.
 * @retval  VERR_GIM_HYPERCALL_ACCESS_DENIED CPL is insufficient.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pCtx            Pointer to the guest-CPU context.
 *
 * @thread  EMT(pVCpu).
 */
VMM_INT_DECL(VBOXSTRICTRC) gimKvmHypercall(PVMCPUCC pVCpu, PCPUMCTX pCtx)
{
    VMCPU_ASSERT_EMT(pVCpu);

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    STAM_REL_COUNTER_INC(&pVM->gim.s.StatHypercalls);

    /*
     * Get the hypercall operation and arguments.
     */
    bool const fIs64BitMode = CPUMIsGuestIn64BitCodeEx(pCtx);
    uint64_t uHyperOp       = pCtx->rax;
    uint64_t uHyperArg0     = pCtx->rbx;
    uint64_t uHyperArg1     = pCtx->rcx;
    uint64_t uHyperArg2     = pCtx->rdi;
    uint64_t uHyperArg3     = pCtx->rsi;
    uint64_t uHyperRet      = KVM_HYPERCALL_RET_ENOSYS;
    uint64_t uAndMask       = UINT64_C(0xffffffffffffffff);
    if (!fIs64BitMode)
    {
        uAndMask    = UINT64_C(0xffffffff);
        uHyperOp   &= UINT64_C(0xffffffff);
        uHyperArg0 &= UINT64_C(0xffffffff);
        uHyperArg1 &= UINT64_C(0xffffffff);
        uHyperArg2 &= UINT64_C(0xffffffff);
        uHyperArg3 &= UINT64_C(0xffffffff);
        uHyperRet  &= UINT64_C(0xffffffff);
    }

    /*
     * Verify that guest ring-0 is the one making the hypercall.
     */
    uint32_t uCpl = CPUMGetGuestCPL(pVCpu);
    if (RT_UNLIKELY(uCpl))
    {
        pCtx->rax = KVM_HYPERCALL_RET_EPERM & uAndMask;
        return VERR_GIM_HYPERCALL_ACCESS_DENIED;
    }

    /*
     * Do the work.
     */
    int rc = VINF_SUCCESS;
    switch (uHyperOp)
    {
        case KVM_HYPERCALL_OP_KICK_CPU:
        {
            if (uHyperArg1 < pVM->cCpus)
            {
                PVMCPUCC pVCpuDst = VMCC_GET_CPU(pVM, uHyperArg1); /* ASSUMES pVCpu index == ApicId of the VCPU. */
                EMUnhaltAndWakeUp(pVM, pVCpuDst);
                uHyperRet = KVM_HYPERCALL_RET_SUCCESS;
            }
            else
            {
                /* Shouldn't ever happen! If it does, throw a guru, as otherwise it'll lead to deadlocks in the guest anyway! */
                rc = VERR_GIM_HYPERCALL_FAILED;
            }
            break;
        }

        case KVM_HYPERCALL_OP_VAPIC_POLL_IRQ:
            uHyperRet = KVM_HYPERCALL_RET_SUCCESS;
            break;

        default:
            break;
    }

    /*
     * Place the result in rax/eax.
     */
    pCtx->rax = uHyperRet & uAndMask;
    return rc;
}


/**
 * Returns whether the guest has configured and enabled the use of KVM's
 * hypercall interface.
 *
 * @returns true if hypercalls are enabled, false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMM_INT_DECL(bool) gimKvmAreHypercallsEnabled(PVMCPU pVCpu)
{
    NOREF(pVCpu);
    /* KVM paravirt interface doesn't have hypercall control bits (like Hyper-V does)
       that guests can control, i.e. hypercalls are always enabled. */
    return true;
}


/**
 * Returns whether the guest has configured and enabled the use of KVM's
 * paravirtualized TSC.
 *
 * @returns true if paravirt. TSC is enabled, false otherwise.
 * @param   pVM     The cross context VM structure.
 */
VMM_INT_DECL(bool) gimKvmIsParavirtTscEnabled(PVMCC pVM)
{
    uint32_t const cCpus = pVM->cCpus;
    for (uint32_t idCpu = 0; idCpu < cCpus; idCpu++)
    {
        PVMCPUCC   pVCpu      = pVM->CTX_SUFF(apCpus)[idCpu];
        PGIMKVMCPU pGimKvmCpu = &pVCpu->gim.s.u.KvmCpu;
        if (MSR_GIM_KVM_SYSTEM_TIME_IS_ENABLED(pGimKvmCpu->u64SystemTimeMsr))
            return true;
    }
    return false;
}


/**
 * MSR read handler for KVM.
 *
 * @returns Strict VBox status code like CPUMQueryGuestMsr().
 * @retval  VINF_CPUM_R3_MSR_READ
 * @retval  VERR_CPUM_RAISE_GP_0
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR being read.
 * @param   pRange      The range this MSR belongs to.
 * @param   puValue     Where to store the MSR value read.
 */
VMM_INT_DECL(VBOXSTRICTRC) gimKvmReadMsr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    NOREF(pRange);
    PVM     pVM        = pVCpu->CTX_SUFF(pVM);
    PGIMKVM pKvm       = &pVM->gim.s.u.Kvm;
    PGIMKVMCPU pKvmCpu = &pVCpu->gim.s.u.KvmCpu;

    switch (idMsr)
    {
        case MSR_GIM_KVM_SYSTEM_TIME:
        case MSR_GIM_KVM_SYSTEM_TIME_OLD:
            *puValue = pKvmCpu->u64SystemTimeMsr;
            return VINF_SUCCESS;

        case MSR_GIM_KVM_WALL_CLOCK:
        case MSR_GIM_KVM_WALL_CLOCK_OLD:
            *puValue = pKvm->u64WallClockMsr;
            return VINF_SUCCESS;

        default:
        {
#ifdef IN_RING3
            static uint32_t s_cTimes = 0;
            if (s_cTimes++ < 20)
                LogRel(("GIM: KVM: Unknown/invalid RdMsr (%#x) -> #GP(0)\n", idMsr));
#endif
            LogFunc(("Unknown/invalid RdMsr (%#RX32) -> #GP(0)\n", idMsr));
            break;
        }
    }

    return VERR_CPUM_RAISE_GP_0;
}


/**
 * MSR write handler for KVM.
 *
 * @returns Strict VBox status code like CPUMSetGuestMsr().
 * @retval  VINF_CPUM_R3_MSR_WRITE
 * @retval  VERR_CPUM_RAISE_GP_0
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR being written.
 * @param   pRange      The range this MSR belongs to.
 * @param   uRawValue   The raw value with the ignored bits not masked.
 */
VMM_INT_DECL(VBOXSTRICTRC) gimKvmWriteMsr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uRawValue)
{
    NOREF(pRange);
    switch (idMsr)
    {
        case MSR_GIM_KVM_SYSTEM_TIME:
        case MSR_GIM_KVM_SYSTEM_TIME_OLD:
        {
#ifndef IN_RING3
            RT_NOREF2(pVCpu, uRawValue);
            return VINF_CPUM_R3_MSR_WRITE;
#else
            PVMCC      pVM = pVCpu->CTX_SUFF(pVM);
            PGIMKVMCPU pKvmCpu = &pVCpu->gim.s.u.KvmCpu;
            if (uRawValue & MSR_GIM_KVM_SYSTEM_TIME_ENABLE_BIT)
                gimR3KvmEnableSystemTime(pVM, pVCpu, uRawValue);
            else
                gimR3KvmDisableSystemTime(pVM);

            pKvmCpu->u64SystemTimeMsr = uRawValue;
            return VINF_SUCCESS;
#endif /* IN_RING3 */
        }

        case MSR_GIM_KVM_WALL_CLOCK:
        case MSR_GIM_KVM_WALL_CLOCK_OLD:
        {
#ifndef IN_RING3
            RT_NOREF2(pVCpu, uRawValue);
            return VINF_CPUM_R3_MSR_WRITE;
#else
            /* Enable the wall-clock struct. */
            RTGCPHYS GCPhysWallClock = MSR_GIM_KVM_WALL_CLOCK_GUEST_GPA(uRawValue);
            if (RT_LIKELY(RT_ALIGN_64(GCPhysWallClock, 4) == GCPhysWallClock))
            {
                PVMCC pVM = pVCpu->CTX_SUFF(pVM);
                int rc = gimR3KvmEnableWallClock(pVM, GCPhysWallClock);
                if (RT_SUCCESS(rc))
                {
                    PGIMKVM pKvm = &pVM->gim.s.u.Kvm;
                    pKvm->u64WallClockMsr = uRawValue;
                    return VINF_SUCCESS;
                }
            }
            return VERR_CPUM_RAISE_GP_0;
#endif /* IN_RING3 */
        }

        default:
        {
#ifdef IN_RING3
            static uint32_t s_cTimes = 0;
            if (s_cTimes++ < 20)
                LogRel(("GIM: KVM: Unknown/invalid WrMsr (%#x,%#x`%08x) -> #GP(0)\n", idMsr,
                        uRawValue & UINT64_C(0xffffffff00000000), uRawValue & UINT64_C(0xffffffff)));
#endif
            LogFunc(("Unknown/invalid WrMsr (%#RX32,%#RX64) -> #GP(0)\n", idMsr, uRawValue));
            break;
        }
    }

    return VERR_CPUM_RAISE_GP_0;
}


/**
 * Whether we need to trap \#UD exceptions in the guest.
 *
 * On AMD-V we need to trap them because paravirtualized Linux/KVM guests use
 * the Intel VMCALL instruction to make hypercalls and we need to trap and
 * optionally patch them to the AMD-V VMMCALL instruction and handle the
 * hypercall.
 *
 * I guess this was done so that guest teleporation between an AMD and an Intel
 * machine would working without any changes at the time of teleporation.
 * However, this also means we -always- need to intercept \#UD exceptions on one
 * of the two CPU models (Intel or AMD). Hyper-V solves this problem more
 * elegantly by letting the hypervisor supply an opaque hypercall page.
 *
 * For raw-mode VMs, this function will always return true. See gimR3KvmInit().
 *
 * @param   pVM     The cross context VM structure.
 */
VMM_INT_DECL(bool) gimKvmShouldTrapXcptUD(PVM pVM)
{
    return pVM->gim.s.u.Kvm.fTrapXcptUD;
}


/**
 * Checks the instruction and executes the hypercall if it's a valid hypercall
 * instruction.
 *
 * This interface is used by \#UD handlers and IEM.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pCtx        Pointer to the guest-CPU context.
 * @param   uDisOpcode  The disassembler opcode.
 * @param   cbInstr     The instruction length.
 *
 * @thread  EMT(pVCpu).
 */
VMM_INT_DECL(VBOXSTRICTRC) gimKvmHypercallEx(PVMCPUCC pVCpu, PCPUMCTX pCtx, unsigned uDisOpcode, uint8_t cbInstr)
{
    Assert(pVCpu);
    Assert(pCtx);
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * If the instruction at RIP is the Intel VMCALL instruction or
     * the AMD VMMCALL instruction handle it as a hypercall.
     *
     * Linux/KVM guests always uses the Intel VMCALL instruction but we patch
     * it to the host-native one whenever we encounter it so subsequent calls
     * will not require disassembly (when coming from HM).
     */
    if (   uDisOpcode == OP_VMCALL
        || uDisOpcode == OP_VMMCALL)
    {
        /*
         * Perform the hypercall.
         *
         * For HM, we can simply resume guest execution without performing the hypercall now and
         * do it on the next VMCALL/VMMCALL exit handler on the patched instruction.
         *
         * For raw-mode we need to do this now anyway. So we do it here regardless with an added
         * advantage is that it saves one world-switch for the HM case.
         */
        VBOXSTRICTRC rcStrict = gimKvmHypercall(pVCpu, pCtx);
        if (rcStrict == VINF_SUCCESS)
        {
            /*
             * Patch the instruction to so we don't have to spend time disassembling it each time.
             * Makes sense only for HM as with raw-mode we will be getting a #UD regardless.
             */
            PVM      pVM  = pVCpu->CTX_SUFF(pVM);
            PCGIMKVM pKvm = &pVM->gim.s.u.Kvm;
            if (   uDisOpcode != pKvm->uOpcodeNative
                && cbInstr == sizeof(pKvm->abOpcodeNative) )
            {
                /** @todo r=ramshankar: we probably should be doing this in an
                 *        EMT rendezvous. */
                /** @todo Add stats for patching. */
                int rc = PGMPhysSimpleWriteGCPtr(pVCpu, pCtx->rip, pKvm->abOpcodeNative, sizeof(pKvm->abOpcodeNative));
                AssertRC(rc);
            }
        }
        else
        {
            /* The KVM provider doesn't have any concept of continuing hypercalls. */
            Assert(rcStrict != VINF_GIM_HYPERCALL_CONTINUING);
#ifdef IN_RING3
            Assert(rcStrict != VINF_GIM_R3_HYPERCALL);
#endif
        }
        return rcStrict;
    }

    return VERR_GIM_INVALID_HYPERCALL_INSTR;
}


/**
 * Exception handler for \#UD.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_SUCCESS if the hypercall succeeded (even if its operation
 *          failed).
 * @retval  VINF_GIM_R3_HYPERCALL re-start the hypercall from ring-3.
 * @retval  VERR_GIM_HYPERCALL_ACCESS_DENIED CPL is insufficient.
 * @retval  VERR_GIM_INVALID_HYPERCALL_INSTR instruction at RIP is not a valid
 *          hypercall instruction.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pCtx        Pointer to the guest-CPU context.
 * @param   pDis        Pointer to the disassembled instruction state at RIP.
 *                      Optional, can be NULL.
 * @param   pcbInstr    Where to store the instruction length of the hypercall
 *                      instruction. Optional, can be NULL.
 *
 * @thread  EMT(pVCpu).
 */
VMM_INT_DECL(VBOXSTRICTRC) gimKvmXcptUD(PVMCC pVM, PVMCPUCC pVCpu, PCPUMCTX pCtx, PDISSTATE pDis, uint8_t *pcbInstr)
{
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * If we didn't ask for #UD to be trapped, bail.
     */
    if (RT_UNLIKELY(!pVM->gim.s.u.Kvm.fTrapXcptUD))
        return VERR_GIM_IPE_3;

    if (!pDis)
    {
        unsigned cbInstr;
        DISSTATE Dis;
        int rc = EMInterpretDisasCurrent(pVCpu, &Dis, &cbInstr);
        if (RT_SUCCESS(rc))
        {
            if (pcbInstr)
                *pcbInstr = (uint8_t)cbInstr;
            return gimKvmHypercallEx(pVCpu, pCtx, Dis.pCurInstr->uOpcode, Dis.cbInstr);
        }

        Log(("GIM: KVM: Failed to disassemble instruction at CS:RIP=%04x:%08RX64. rc=%Rrc\n", pCtx->cs.Sel, pCtx->rip, rc));
        return rc;
    }

    return gimKvmHypercallEx(pVCpu, pCtx, pDis->pCurInstr->uOpcode, pDis->cbInstr);
}

