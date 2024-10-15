/* $Id: GICAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIC - Generic Interrupt Controller Architecture (GICv3) - All Contexts.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_APIC
#include "GICInternal.h"
#include <VBox/vmm/gic.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vmcpuset.h>
#ifdef IN_RING0
# include <VBox/vmm/gvmm.h>
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

#ifdef LOG_ENABLED
/**
 * Returns a human readable string of the given exception class.
 *
 * @returns Pointer to the string matching the given EC.
 * @param   u32Ec           The exception class to return the string for.
 */
static const char *gicIccRegisterStringify(uint32_t u32Reg)
{
    switch (u32Reg)
    {
#define GIC_ICC_REG_CASE(a_Reg) case ARMV8_AARCH64_SYSREG_ ## a_Reg: return #a_Reg
        GIC_ICC_REG_CASE(ICC_PMR_EL1);
        GIC_ICC_REG_CASE(ICC_IAR0_EL1);
        GIC_ICC_REG_CASE(ICC_EOIR0_EL1);
        GIC_ICC_REG_CASE(ICC_HPPIR0_EL1);
        GIC_ICC_REG_CASE(ICC_BPR0_EL1);
        GIC_ICC_REG_CASE(ICC_AP0R0_EL1);
        GIC_ICC_REG_CASE(ICC_AP0R1_EL1);
        GIC_ICC_REG_CASE(ICC_AP0R2_EL1);
        GIC_ICC_REG_CASE(ICC_AP0R3_EL1);
        GIC_ICC_REG_CASE(ICC_AP1R0_EL1);
        GIC_ICC_REG_CASE(ICC_AP1R1_EL1);
        GIC_ICC_REG_CASE(ICC_AP1R2_EL1);
        GIC_ICC_REG_CASE(ICC_AP1R3_EL1);
        GIC_ICC_REG_CASE(ICC_DIR_EL1);
        GIC_ICC_REG_CASE(ICC_RPR_EL1);
        GIC_ICC_REG_CASE(ICC_SGI1R_EL1);
        GIC_ICC_REG_CASE(ICC_ASGI1R_EL1);
        GIC_ICC_REG_CASE(ICC_SGI0R_EL1);
        GIC_ICC_REG_CASE(ICC_IAR1_EL1);
        GIC_ICC_REG_CASE(ICC_EOIR1_EL1);
        GIC_ICC_REG_CASE(ICC_HPPIR1_EL1);
        GIC_ICC_REG_CASE(ICC_BPR1_EL1);
        GIC_ICC_REG_CASE(ICC_CTLR_EL1);
        GIC_ICC_REG_CASE(ICC_SRE_EL1);
        GIC_ICC_REG_CASE(ICC_IGRPEN0_EL1);
        GIC_ICC_REG_CASE(ICC_IGRPEN1_EL1);
#undef GIC_ICC_REG_CASE
        default:
            break;
    }

    return "<UNKNOWN>";
}
#endif


/**
 * Sets the interrupt pending force-flag and pokes the EMT if required.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fIrq            Flag whether to assert the IRQ line or leave it alone.
 * @param   fFiq            Flag whether to assert the FIQ line or leave it alone.
 */
static void gicSetInterruptFF(PVMCPUCC pVCpu, bool fIrq, bool fFiq)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} fIrq=%RTbool fFiq=%RTbool\n",
                 pVCpu, pVCpu->idCpu, fIrq, fFiq));

    Assert(fIrq || fFiq);

#ifdef IN_RING3
    /* IRQ state should be loaded as-is by "LoadExec". Changes can be made from LoadDone. */
    Assert(pVCpu->pVMR3->enmVMState != VMSTATE_LOADING || PDMR3HasLoadedState(pVCpu->pVMR3));
#endif

    if (fIrq)
        VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ);
    if (fFiq)
        VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_FIQ);

    /*
     * We need to wake up the target CPU if we're not on EMT.
     */
    /** @todo We could just use RTThreadNativeSelf() here, couldn't we? */
#if defined(IN_RING0)
    PVMCC   pVM   = pVCpu->CTX_SUFF(pVM);
    VMCPUID idCpu = pVCpu->idCpu;
    if (VMMGetCpuId(pVM) != idCpu)
    {
        switch (VMCPU_GET_STATE(pVCpu))
        {
            case VMCPUSTATE_STARTED_EXEC:
                Log7Func(("idCpu=%u VMCPUSTATE_STARTED_EXEC\n", idCpu));
                GVMMR0SchedPokeNoGVMNoLock(pVM, idCpu);
                break;

            case VMCPUSTATE_STARTED_HALTED:
                Log7Func(("idCpu=%u VMCPUSTATE_STARTED_HALTED\n", idCpu));
                GVMMR0SchedWakeUpNoGVMNoLock(pVM, idCpu);
                break;

            default:
                Log7Func(("idCpu=%u enmState=%d\n", idCpu, pVCpu->enmState));
                break; /* nothing to do in other states. */
        }
    }
#elif defined(IN_RING3)
    PVMCC   pVM   = pVCpu->CTX_SUFF(pVM);
    VMCPUID idCpu = pVCpu->idCpu;
    if (VMMGetCpuId(pVM) != idCpu)
    {
        Log7Func(("idCpu=%u enmState=%d\n", idCpu, pVCpu->enmState));
        VMR3NotifyCpuFFU(pVCpu->pUVCpu, VMNOTIFYFF_FLAGS_POKE);
    }
#endif
}


/**
 * Clears the interrupt pending force-flag.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fIrq            Flag whether to clear the IRQ flag.
 * @param   fFiq            Flag whether to clear the FIQ flag.
 */
DECLINLINE(void) gicClearInterruptFF(PVMCPUCC pVCpu, bool fIrq, bool fFiq)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} fIrq=%RTbool fFiq=%RTbool\n",
                 pVCpu, pVCpu->idCpu, fIrq, fFiq));

    Assert(fIrq || fFiq);

#ifdef IN_RING3
    /* IRQ state should be loaded as-is by "LoadExec". Changes can be made from LoadDone. */
    Assert(pVCpu->pVMR3->enmVMState != VMSTATE_LOADING || PDMR3HasLoadedState(pVCpu->pVMR3));
#endif

    if (fIrq)
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_IRQ);
    if (fFiq)
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_FIQ);
}


DECLINLINE(void) gicUpdateInterruptFF(PVMCPUCC pVCpu, bool fIrq, bool fFiq)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} fIrq=%RTbool fFiq=%RTbool\n",
                 pVCpu, pVCpu->idCpu, fIrq, fFiq));

    if (fIrq || fFiq)
        gicSetInterruptFF(pVCpu, fIrq, fFiq);

    if (!fIrq || !fFiq)
        gicClearInterruptFF(pVCpu, !fIrq, !fFiq);
}


DECLINLINE(void) gicReDistHasIrqPending(PGICCPU pThis, bool *pfIrq, bool *pfFiq)
{
    /* Read the interrupt state. */
    uint32_t u32RegIGrp0  = ASMAtomicReadU32(&pThis->u32RegIGrp0);
    uint32_t bmIntEnabled = ASMAtomicReadU32(&pThis->bmIntEnabled);
    uint32_t bmIntPending = ASMAtomicReadU32(&pThis->bmIntPending);
    uint32_t bmIntActive  = ASMAtomicReadU32(&pThis->bmIntActive);
    bool fIrqGrp0Enabled  = ASMAtomicReadBool(&pThis->fIrqGrp0Enabled);
    bool fIrqGrp1Enabled  = ASMAtomicReadBool(&pThis->fIrqGrp1Enabled);

    /* Is anything enabled at all? */
    uint32_t bmIntForward = (bmIntPending & bmIntEnabled) & ~bmIntActive; /* Exclude the currently active interrupt. */

    /* Only allow interrupts with higher priority than the current configured and running one. */
    uint8_t bPriority = RT_MIN(pThis->bInterruptPriority, pThis->abRunningPriorities[pThis->idxRunningPriority]);

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->abIntPriority); i++)
    {
        Log4(("SGI/PPI %u, configured priority %u, running priority %u\n", i, pThis->abIntPriority[i], bPriority));
        if (   (bmIntForward & RT_BIT_32(i))
            && pThis->abIntPriority[i] < bPriority)
            break;
        else
            bmIntForward &= ~RT_BIT_32(i);

        if (!bmIntForward)
            break;
    }

    if (bmIntForward)
    {
        /* Determine whether we have to assert the IRQ or FIQ line. */
        *pfIrq = RT_BOOL(bmIntForward & u32RegIGrp0) && fIrqGrp1Enabled;
        *pfFiq = RT_BOOL(bmIntForward & ~u32RegIGrp0) && fIrqGrp0Enabled;
    }
    else
    {
        *pfIrq = false;
        *pfFiq = false;
    }

    LogFlowFunc(("pThis=%p bPriority=%u bmIntEnabled=%#x bmIntPending=%#x bmIntActive=%#x fIrq=%RTbool fFiq=%RTbool\n",
                 pThis, bPriority, bmIntEnabled, bmIntPending, bmIntActive, *pfIrq, *pfFiq));
}


DECLINLINE(void) gicDistHasIrqPendingForVCpu(PGICDEV pThis, PGICCPU pGicVCpu, bool *pfIrq, bool *pfFiq)
{
    /* Read the interrupt state. */
    uint32_t u32RegIGrp0  = ASMAtomicReadU32(&pThis->u32RegIGrp0);
    uint32_t bmIntEnabled = ASMAtomicReadU32(&pThis->bmIntEnabled);
    uint32_t bmIntPending = ASMAtomicReadU32(&pThis->bmIntPending);
    uint32_t bmIntActive  = ASMAtomicReadU32(&pThis->bmIntActive);
    bool fIrqGrp0Enabled  = ASMAtomicReadBool(&pThis->fIrqGrp0Enabled);
    bool fIrqGrp1Enabled  = ASMAtomicReadBool(&pThis->fIrqGrp1Enabled);

    /* Is anything enabled at all? */
    uint32_t bmIntForward = (bmIntPending & bmIntEnabled) & ~bmIntActive; /* Exclude the currently active interrupt. */

    /* Only allow interrupts with higher priority than the current configured and running one. */
    uint8_t bPriority = RT_MIN(pGicVCpu->bInterruptPriority, pGicVCpu->abRunningPriorities[pGicVCpu->idxRunningPriority]);
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->abIntPriority); i++)
    {
        if (   (bmIntForward & RT_BIT_32(i))
            && pThis->abIntPriority[i] < bPriority)
            break;
        else
            bmIntForward &= ~RT_BIT_32(i);
    }

    if (bmIntForward)
    {
        /* Determine whether we have to assert the IRQ or FIQ line. */
        *pfIrq = RT_BOOL(bmIntForward & u32RegIGrp0) && fIrqGrp1Enabled;
        *pfFiq = RT_BOOL(bmIntForward & ~u32RegIGrp0) && fIrqGrp0Enabled;
    }
    else
    {
        *pfIrq = false;
        *pfFiq = false;
    }

    LogFlowFunc(("pThis=%p bPriority=%u bmIntEnabled=%#x bmIntPending=%#x bmIntActive=%#x fIrq=%RTbool fFiq=%RTbool\n",
                 pThis, bPriority, bmIntEnabled, bmIntPending, bmIntActive, *pfIrq, *pfFiq));
}


/**
 * Updates the internal IRQ state and sets or clears the appropirate force action flags.
 *
 * @returns Strict VBox status code.
 * @param   pThis           The GIC re-distributor state for the associated vCPU.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static VBOXSTRICTRC gicReDistUpdateIrqState(PGICCPU pThis, PVMCPUCC pVCpu)
{
    bool fIrq, fFiq;
    gicReDistHasIrqPending(pThis, &fIrq, &fFiq);

    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    bool fIrqDist, fFiqDist;
    gicDistHasIrqPendingForVCpu(pGicDev, pThis, &fIrqDist, &fFiqDist);
    fIrq |= fIrqDist;
    fFiq |= fFiqDist;

    gicUpdateInterruptFF(pVCpu, fIrq, fFiq);
    return VINF_SUCCESS;
}


/**
 * Updates the internal IRQ state of the distributor and sets or clears the appropirate force action flags.
 *
 * @returns Strict VBox status code.
 * @param   pVM             The cross context VM state.
 * @param   pThis           The GIC distributor state.
 */
static VBOXSTRICTRC gicDistUpdateIrqState(PVMCC pVM, PGICDEV pThis)
{
    for (uint32_t i = 0; i < pVM->cCpus; i++)
    {
        PVMCPUCC pVCpu = pVM->CTX_SUFF(apCpus)[i];
        PGICCPU pGicVCpu = VMCPU_TO_GICCPU(pVCpu);

        bool fIrq, fFiq;
        gicReDistHasIrqPending(pGicVCpu, &fIrq, &fFiq);

        bool fIrqDist, fFiqDist;
        gicDistHasIrqPendingForVCpu(pThis, pGicVCpu, &fIrqDist, &fFiqDist);
        fIrq |= fIrqDist;
        fFiq |= fFiqDist;

        gicUpdateInterruptFF(pVCpu, fIrq, fFiq);
    }
    return VINF_SUCCESS;
}


/**
 * Sets the given SGI/PPI interrupt ID on the re-distributor of the given vCPU.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uIntId          The SGI/PPI interrupt identifier.
 * @param   fAsserted       Flag whether the SGI/PPI interrupt is asserted or not.
 */
static int gicReDistInterruptSet(PVMCPUCC pVCpu, uint32_t uIntId, bool fAsserted)
{
    PGICCPU pThis = VMCPU_TO_GICCPU(pVCpu);

    /* Update the interrupts pending state. */
    if (fAsserted)
        ASMAtomicOrU32(&pThis->bmIntPending, RT_BIT_32(uIntId));
    else
        ASMAtomicAndU32(&pThis->bmIntPending, ~RT_BIT_32(uIntId));

    return VBOXSTRICTRC_VAL(gicReDistUpdateIrqState(pThis, pVCpu));
}


/**
 * Reads a GIC distributor register.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being read.
 * @param   puValue         Where to store the register value.
 */
DECLINLINE(VBOXSTRICTRC) gicDistRegisterRead(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t *puValue)
{
    VMCPU_ASSERT_EMT(pVCpu);
    PGICDEV pThis  = PDMDEVINS_2_DATA(pDevIns, PGICDEV);

    switch (offReg)
    {
        case GIC_DIST_REG_CTLR_OFF:
            *puValue =   (ASMAtomicReadBool(&pThis->fIrqGrp0Enabled) ? GIC_DIST_REG_CTRL_ENABLE_GRP0 : 0)
                       | (ASMAtomicReadBool(&pThis->fIrqGrp1Enabled) ? GIC_DIST_REG_CTRL_ENABLE_GRP1_NS : 0)
                       | GIC_DIST_REG_CTRL_DS;
            break;
        case GIC_DIST_REG_TYPER_OFF:
            *puValue =   GIC_DIST_REG_TYPER_NUM_ITLINES_SET(1)  /** @todo 32 SPIs for now. */
                       | GIC_DIST_REG_TYPER_NUM_PES_SET(0)      /* 1 PE */
                       /*| GIC_DIST_REG_TYPER_ESPI*/            /** @todo */
                       /*| GIC_DIST_REG_TYPER_NMI*/             /** @todo Non-maskable interrupts */
                       /*| GIC_DIST_REG_TYPER_SECURITY_EXTN */  /** @todo */
                       /*| GIC_DIST_REG_TYPER_MBIS */           /** @todo Message based interrupts */
                       /*| GIC_DIST_REG_TYPER_LPIS */           /** @todo Support LPIs */
                       | GIC_DIST_REG_TYPER_IDBITS_SET(16);
            break;
        case GIC_DIST_REG_STATUSR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_IGROUPRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_ISENABLERn_OFF_START + 4: /* Only 32 lines for now. */
        case GIC_DIST_REG_ICENABLERn_OFF_START + 4: /* Only 32 lines for now. */
            *puValue = ASMAtomicReadU32(&pThis->bmIntEnabled);
            break;
        case GIC_DIST_REG_ISPENDRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_ICPENDRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_ISACTIVERn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_ICACTIVERn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_IPRIORITYn_OFF_START:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 4: /* These are banked for the PEs and access the redistributor. */
        {
            PGICCPU pGicVCpu = VMCPU_TO_GICCPU(pVCpu);

            /* Figure out the register which is written. */
            uint8_t idxPrio = offReg - GIC_DIST_REG_IPRIORITYn_OFF_START;
            Assert(idxPrio <= RT_ELEMENTS(pThis->abIntPriority) - sizeof(uint32_t));

            uint32_t u32Value = 0;
            for (uint32_t i = idxPrio; i < idxPrio + sizeof(uint32_t); i++)
                u32Value |= pGicVCpu->abIntPriority[i] << ((i - idxPrio) * 8);

            *puValue = u32Value;
            break;
        }
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 32: /* Only 32 lines for now. */
        {
            /* Figure out the register which is written. */
            uint8_t idxPrio = offReg - GIC_DIST_REG_IPRIORITYn_OFF_START - 32;
            Assert(idxPrio <= RT_ELEMENTS(pThis->abIntPriority) - sizeof(uint32_t));

            uint32_t u32Value = 0;
            for (uint32_t i = idxPrio; i < idxPrio + sizeof(uint32_t); i++)
                u32Value |= pThis->abIntPriority[i] << ((i - idxPrio) * 8);

            *puValue = u32Value;
            break;
        }
        case GIC_DIST_REG_ITARGETSRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_ICFGRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_IGRPMODRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_NSACRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_SGIR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_CPENDSGIRn_OFF_START:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_SPENDSGIRn_OFF_START:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_INMIn_OFF_START:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_IROUTERn_OFF_START: /* Only 32 lines for now. */
            *puValue = 0; /** @todo */
            break;
        case GIC_DIST_REG_PIDR2_OFF:
            *puValue = GIC_REDIST_REG_PIDR2_ARCH_REV_SET(GIC_REDIST_REG_PIDR2_ARCH_REV_GICV3);
            break;
        case GIC_DIST_REG_IIDR_OFF:
        case GIC_DIST_REG_TYPER2_OFF:
            *puValue = 0;
            break;
        default:
            *puValue = 0;
    }
    return VINF_SUCCESS;
}


/**
 * Writes a GIC distributor register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being written.
 * @param   uValue          The register value.
 */
DECLINLINE(VBOXSTRICTRC) gicDistRegisterWrite(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t uValue)
{
    VMCPU_ASSERT_EMT(pVCpu); RT_NOREF(pVCpu);
    PGICDEV pThis  = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    PVMCC    pVM   = PDMDevHlpGetVM(pDevIns);

    if (offReg >= GIC_DIST_REG_IROUTERn_OFF_START && offReg <= GIC_DIST_REG_IROUTERn_OFF_LAST)
    {
        /** @todo Ignored for now, probably make this a MMIO2 region as it begins on 0x6000, see 12.9.22 of the GICv3 architecture
         * reference manual. */
        return VINF_SUCCESS;
    }

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    switch (offReg)
    {
        case GIC_DIST_REG_CTLR_OFF:
            ASMAtomicWriteBool(&pThis->fIrqGrp0Enabled, RT_BOOL(uValue & GIC_DIST_REG_CTRL_ENABLE_GRP0));
            ASMAtomicWriteBool(&pThis->fIrqGrp1Enabled, RT_BOOL(uValue & GIC_DIST_REG_CTRL_ENABLE_GRP1_NS));
            rcStrict = gicDistUpdateIrqState(pVM, pThis);
            break;
        case GIC_DIST_REG_STATUSR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_SETSPI_NSR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_CLRSPI_NSR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_SETSPI_SR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_CLRSPI_SR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_IGROUPRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_IGROUPRn_OFF_START + 4: /* Only 32 lines for now. */
            ASMAtomicOrU32(&pThis->u32RegIGrp0, uValue);
            rcStrict = gicDistUpdateIrqState(pVM, pThis);
            break;
        case GIC_DIST_REG_ISENABLERn_OFF_START + 4: /* Only 32 lines for now. */
            ASMAtomicOrU32(&pThis->bmIntEnabled, uValue);
            rcStrict = gicDistUpdateIrqState(pVM, pThis);
            break;
        case GIC_DIST_REG_ICENABLERn_OFF_START:
            //AssertReleaseFailed();
            break;
        case GIC_DIST_REG_ICENABLERn_OFF_START + 4: /* Only 32 lines for now. */
            ASMAtomicAndU32(&pThis->bmIntEnabled, ~uValue);
            rcStrict = gicDistUpdateIrqState(pVM, pThis);
            break;
        case GIC_DIST_REG_ISPENDRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_ICPENDRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_ISACTIVERn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_ICACTIVERn_OFF_START + 4: /* Only 32 lines for now. */
            ASMAtomicAndU32(&pThis->bmIntActive, ~uValue);
            rcStrict = gicDistUpdateIrqState(pVM, pThis);
            break;
        case GIC_DIST_REG_IPRIORITYn_OFF_START: /* These are banked for the PEs and access the redistributor. */
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 4:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 8:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 12:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 16:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 20:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 24:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 28:
        {
            PGICCPU pGicVCpu = VMCPU_TO_GICCPU(pVCpu);

            /* Figure out the register which is written. */
            uint8_t idxPrio = offReg - GIC_DIST_REG_IPRIORITYn_OFF_START;
            Assert(idxPrio <= RT_ELEMENTS(pGicVCpu->abIntPriority) - sizeof(uint32_t));
            for (uint32_t i = idxPrio; i < idxPrio + sizeof(uint32_t); i++)
            {
                pGicVCpu->abIntPriority[i] = (uint8_t)(uValue & 0xff);
                uValue >>= 8;
            }
            break;
        }
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 32: /* Only 32 lines for now. */
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 36:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 40:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 44:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 48:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 52:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 56:
        case GIC_DIST_REG_IPRIORITYn_OFF_START + 60:
        {
            /* Figure out the register which is written. */
            uint8_t idxPrio = offReg - GIC_DIST_REG_IPRIORITYn_OFF_START - 32;
            Assert(idxPrio <= RT_ELEMENTS(pThis->abIntPriority) - sizeof(uint32_t));
            for (uint32_t i = idxPrio; i < idxPrio + sizeof(uint32_t); i++)
            {
                pThis->abIntPriority[i] = (uint8_t)(uValue & 0xff);
                uValue >>= 8;
            }
            break;
        }
        case GIC_DIST_REG_ITARGETSRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_ICFGRn_OFF_START + 8: /* Only 32 lines for now. */
            ASMAtomicWriteU32(&pThis->u32RegICfg0, uValue);
            break;
        case GIC_DIST_REG_ICFGRn_OFF_START+ 12:
            ASMAtomicWriteU32(&pThis->u32RegICfg1, uValue);
            break;
        case GIC_DIST_REG_IGRPMODRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_NSACRn_OFF_START: /* Only 32 lines for now. */
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_SGIR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_CPENDSGIRn_OFF_START:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_SPENDSGIRn_OFF_START:
            AssertReleaseFailed();
            break;
        case GIC_DIST_REG_INMIn_OFF_START:
            AssertReleaseFailed();
            break;
        default:
            AssertReleaseFailed();
            break;
    }

    return rcStrict;
}


/**
 * Reads a GIC redistributor register.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   idRedist        The redistributor ID.
 * @param   offReg          The offset of the register being read.
 * @param   puValue         Where to store the register value.
 */
DECLINLINE(VBOXSTRICTRC) gicReDistRegisterRead(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint32_t idRedist, uint16_t offReg, uint32_t *puValue)
{
    RT_NOREF(pDevIns);

    switch (offReg)
    {
        case GIC_REDIST_REG_TYPER_OFF:
        {
            PVMCC pVM = PDMDevHlpGetVM(pDevIns);
            *puValue =   ((pVCpu->idCpu == pVM->cCpus - 1) ? GIC_REDIST_REG_TYPER_LAST : 0)
                       | GIC_REDIST_REG_TYPER_CPU_NUMBER_SET(idRedist)
                       | GIC_REDIST_REG_TYPER_CMN_LPI_AFF_SET(GIC_REDIST_REG_TYPER_CMN_LPI_AFF_ALL);
            break;
        }
        case GIC_REDIST_REG_TYPER_AFFINITY_OFF:
            *puValue = idRedist;
            break;
        case GIC_REDIST_REG_PIDR2_OFF:
            *puValue = GIC_REDIST_REG_PIDR2_ARCH_REV_SET(GIC_REDIST_REG_PIDR2_ARCH_REV_GICV3);
            break;
        default:
            *puValue = 0;
    }

    return VINF_SUCCESS;
}


/**
 * Reads a GIC redistributor SGI/PPI frame register.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being read.
 * @param   puValue         Where to store the register value.
 */
DECLINLINE(VBOXSTRICTRC) gicReDistSgiPpiRegisterRead(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t *puValue)
{
    VMCPU_ASSERT_EMT(pVCpu);
    RT_NOREF(pDevIns);

    PGICCPU pThis = VMCPU_TO_GICCPU(pVCpu);
    switch (offReg)
    {
        case GIC_REDIST_SGI_PPI_REG_ISENABLER0_OFF:
        case GIC_REDIST_SGI_PPI_REG_ICENABLER0_OFF:
            *puValue = ASMAtomicReadU32(&pThis->bmIntEnabled);
            break;
        case GIC_REDIST_SGI_PPI_REG_ISPENDR0_OFF:
        case GIC_REDIST_SGI_PPI_REG_ICPENDR0_OFF:
            *puValue = ASMAtomicReadU32(&pThis->bmIntPending);
            break;
        case GIC_REDIST_SGI_PPI_REG_ISACTIVER0_OFF:
        case GIC_REDIST_SGI_PPI_REG_ICACTIVER0_OFF:
            *puValue = ASMAtomicReadU32(&pThis->bmIntActive);
            break;
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START +  4:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START +  8:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START + 12:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START + 16:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START + 20:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START + 24:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START + 28:
        {
            /* Figure out the register which is written. */
            uint8_t idxPrio = offReg - GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START;
            Assert(idxPrio <= RT_ELEMENTS(pThis->abIntPriority) - sizeof(uint32_t));

            uint32_t u32Value = 0;
            for (uint32_t i = idxPrio; i < idxPrio + sizeof(uint32_t); i++)
                u32Value |= pThis->abIntPriority[i] << ((i - idxPrio) * 8);

            *puValue = u32Value;
            break;
        }
        case GIC_REDIST_SGI_PPI_REG_ICFGR0_OFF:
            *puValue = ASMAtomicReadU32(&pThis->u32RegICfg0);
            break;
        case GIC_REDIST_SGI_PPI_REG_ICFGR1_OFF:
            *puValue = ASMAtomicReadU32(&pThis->u32RegICfg1);
            break;
        default:
            AssertReleaseFailed();
            *puValue = 0;
    }

    return VINF_SUCCESS;
}


/**
 * Writes a GIC redistributor frame register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being written.
 * @param   uValue          The register value.
 */
DECLINLINE(VBOXSTRICTRC) gicReDistRegisterWrite(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t uValue)
{
    VMCPU_ASSERT_EMT(pVCpu);
    RT_NOREF(pDevIns, pVCpu, uValue);

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    switch (offReg)
    {
        case GIC_REDIST_REG_STATUSR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_REDIST_REG_WAKER_OFF:
            Assert(uValue == 0);
            break;
        case GIC_REDIST_REG_PARTIDR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_REDIST_REG_SETLPIR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_REDIST_REG_CLRLPIR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_REDIST_REG_PROPBASER_OFF:
            AssertReleaseFailed();
            break;
        case GIC_REDIST_REG_PENDBASER_OFF:
            AssertReleaseFailed();
            break;
        case GIC_REDIST_REG_INVLPIR_OFF:
            AssertReleaseFailed();
            break;
        case GIC_REDIST_REG_INVALLR_OFF:
            AssertReleaseFailed();
            break;
        default:
            AssertReleaseFailed();
            break;
    }

    return rcStrict;
}


/**
 * Writes a GIC redistributor SGI/PPI frame register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being written.
 * @param   uValue          The register value.
 */
DECLINLINE(VBOXSTRICTRC) gicReDistSgiPpiRegisterWrite(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t uValue)
{
    VMCPU_ASSERT_EMT(pVCpu);
    RT_NOREF(pDevIns);

    PGICCPU pThis = VMCPU_TO_GICCPU(pVCpu);
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    switch (offReg)
    {
        case GIC_REDIST_SGI_PPI_REG_IGROUPR0_OFF:
            ASMAtomicOrU32(&pThis->u32RegIGrp0, uValue);
            rcStrict = gicReDistUpdateIrqState(pThis, pVCpu);
            break;
        case GIC_REDIST_SGI_PPI_REG_ISENABLER0_OFF:
            ASMAtomicOrU32(&pThis->bmIntEnabled, uValue);
            rcStrict = gicReDistUpdateIrqState(pThis, pVCpu);
            break;
        case GIC_REDIST_SGI_PPI_REG_ICENABLER0_OFF:
            ASMAtomicAndU32(&pThis->bmIntEnabled, ~uValue);
            rcStrict = gicReDistUpdateIrqState(pThis, pVCpu);
            break;
        case GIC_REDIST_SGI_PPI_REG_ISPENDR0_OFF:
            ASMAtomicOrU32(&pThis->bmIntPending, uValue);
            rcStrict = gicReDistUpdateIrqState(pThis, pVCpu);
            break;
        case GIC_REDIST_SGI_PPI_REG_ICPENDR0_OFF:
            ASMAtomicAndU32(&pThis->bmIntPending, ~uValue);
            rcStrict = gicReDistUpdateIrqState(pThis, pVCpu);
            break;
        case GIC_REDIST_SGI_PPI_REG_ISACTIVER0_OFF:
            ASMAtomicOrU32(&pThis->bmIntActive, uValue);
            rcStrict = gicReDistUpdateIrqState(pThis, pVCpu);
            break;
        case GIC_REDIST_SGI_PPI_REG_ICACTIVER0_OFF:
            ASMAtomicAndU32(&pThis->bmIntActive, ~uValue);
            rcStrict = gicReDistUpdateIrqState(pThis, pVCpu);
            break;
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START +  4:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START +  8:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START + 12:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START + 16:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START + 20:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START + 24:
        case GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START + 28:
        {
            /* Figure out the register whch is written. */
            uint8_t idxPrio = offReg - GIC_REDIST_SGI_PPI_REG_IPRIORITYn_OFF_START;
            Assert(idxPrio <= RT_ELEMENTS(pThis->abIntPriority) - sizeof(uint32_t));
            for (uint32_t i = idxPrio; i < idxPrio + sizeof(uint32_t); i++)
            {
                pThis->abIntPriority[i] = (uint8_t)(uValue & 0xff);
                uValue >>= 8;
            }
            break;
        }
        case GIC_REDIST_SGI_PPI_REG_ICFGR0_OFF:
            ASMAtomicWriteU32(&pThis->u32RegICfg0, uValue);
            break;
        case GIC_REDIST_SGI_PPI_REG_ICFGR1_OFF:
            ASMAtomicWriteU32(&pThis->u32RegICfg1, uValue);
            break;
        default:
            //AssertReleaseFailed();
            break;
    }

    return rcStrict;
}


/**
 * Reads a GIC system register.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   u32Reg          The system register being read.
 * @param   pu64Value       Where to store the read value.
 */
VMM_INT_DECL(VBOXSTRICTRC) GICReadSysReg(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t *pu64Value)
{
    /*
     * Validate.
     */
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pu64Value);

    *pu64Value = 0;
    PGICCPU pThis = VMCPU_TO_GICCPU(pVCpu);
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);

    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    switch (u32Reg)
    {
        case ARMV8_AARCH64_SYSREG_ICC_PMR_EL1:
            *pu64Value = pThis->bInterruptPriority;
            break;
        case ARMV8_AARCH64_SYSREG_ICC_IAR0_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_EOIR0_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_HPPIR0_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_BPR0_EL1:
            *pu64Value = pThis->bBinaryPointGrp0 & 0x7;
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R0_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R1_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R2_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R3_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R0_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R1_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R2_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R3_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_NMIAR1_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_DIR_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_RPR_EL1:
            *pu64Value = pThis->abRunningPriorities[pThis->idxRunningPriority];
            break;
        case ARMV8_AARCH64_SYSREG_ICC_SGI1R_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_ASGI1R_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_SGI0R_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_IAR1_EL1:
        {
            /** @todo Figure out the highest priority interrupt. */
            uint32_t bmIntActive = ASMAtomicReadU32(&pThis->bmIntActive);
            uint32_t bmIntEnabled = ASMAtomicReadU32(&pThis->bmIntEnabled);
            uint32_t bmPending = (ASMAtomicReadU32(&pThis->bmIntPending) & bmIntEnabled) & ~bmIntActive;
            int32_t idxIntPending = ASMBitFirstSet(&bmPending, sizeof(bmPending) * 8);
            if (idxIntPending > -1)
            {
                /* Mark the interrupt as active. */
                ASMAtomicOrU32(&pThis->bmIntActive, RT_BIT_32(idxIntPending));
                /* Drop priority. */
                Assert((uint32_t)idxIntPending < RT_ELEMENTS(pThis->abIntPriority));
                Assert(pThis->idxRunningPriority < RT_ELEMENTS(pThis->abRunningPriorities) - 1);
                pThis->abRunningPriorities[++pThis->idxRunningPriority] = pThis->abIntPriority[idxIntPending];

                /* Clear edge level interrupts like SGIs as pending. */
                if (idxIntPending <= GIC_INTID_RANGE_SGI_LAST)
                    ASMAtomicBitClear(&pThis->bmIntPending, idxIntPending);
                *pu64Value = idxIntPending;
                gicReDistUpdateIrqState(pThis, pVCpu);
            }
            else
            {
                /** @todo This is wrong as the guest might decide to prioritize PPIs and SPIs differently. */
                bmIntActive = ASMAtomicReadU32(&pGicDev->bmIntActive);
                bmIntEnabled = ASMAtomicReadU32(&pGicDev->bmIntEnabled);
                bmPending = (ASMAtomicReadU32(&pGicDev->bmIntPending) & bmIntEnabled) & ~bmIntActive;
                idxIntPending = ASMBitFirstSet(&bmPending, sizeof(bmPending) * 8);
                if (idxIntPending > -1)
                {
                    /* Mark the interrupt as active. */
                    ASMAtomicOrU32(&pGicDev->bmIntActive, RT_BIT_32(idxIntPending));

                    /* Drop priority. */
                    Assert((uint32_t)idxIntPending < RT_ELEMENTS(pThis->abIntPriority));
                    Assert(pThis->idxRunningPriority < RT_ELEMENTS(pThis->abRunningPriorities) - 1);
                    pThis->abRunningPriorities[++pThis->idxRunningPriority] = pThis->abIntPriority[idxIntPending];

                    *pu64Value = idxIntPending + GIC_INTID_RANGE_SPI_START;
                    gicReDistUpdateIrqState(pThis, pVCpu);
                }
                else
                    *pu64Value = GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT;
            }
            break;
        }
        case ARMV8_AARCH64_SYSREG_ICC_EOIR1_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_HPPIR1_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_BPR1_EL1:
            *pu64Value = pThis->bBinaryPointGrp1 & 0x7;
            break;
        case ARMV8_AARCH64_SYSREG_ICC_CTLR_EL1:
            *pu64Value =   ARMV8_ICC_CTLR_EL1_AARCH64_PMHE
                         | ARMV8_ICC_CTLR_EL1_AARCH64_PRIBITS_SET(4)
                         | ARMV8_ICC_CTLR_EL1_AARCH64_IDBITS_SET(ARMV8_ICC_CTLR_EL1_AARCH64_IDBITS_16BITS);
            break;
        case ARMV8_AARCH64_SYSREG_ICC_SRE_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN0_EL1:
            *pu64Value = ASMAtomicReadBool(&pThis->fIrqGrp0Enabled) ? ARMV8_ICC_IGRPEN0_EL1_AARCH64_ENABLE : 0;
            break;
        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN1_EL1:
            *pu64Value = ASMAtomicReadBool(&pThis->fIrqGrp1Enabled) ? ARMV8_ICC_IGRPEN1_EL1_AARCH64_ENABLE : 0;
            break;
        default:
            AssertReleaseFailed();
            break;
    }

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);

    LogFlowFunc(("pVCpu=%p u32Reg=%#x{%s} pu64Value=%RX64\n", pVCpu, u32Reg, gicIccRegisterStringify(u32Reg), *pu64Value));
    return VINF_SUCCESS;
}


/**
 * Writes an GIC system register.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   u32Reg          The system register being written (IPRT system  register identifier).
 * @param   u64Value        The value to write.
 */
VMM_INT_DECL(VBOXSTRICTRC) GICWriteSysReg(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t u64Value)
{
    /*
     * Validate.
     */
    VMCPU_ASSERT_EMT(pVCpu);
    LogFlowFunc(("pVCpu=%p u32Reg=%#x{%s} u64Value=%RX64\n", pVCpu, u32Reg, gicIccRegisterStringify(u32Reg), u64Value));

    PGICCPU pThis = VMCPU_TO_GICCPU(pVCpu);
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);

    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    switch (u32Reg)
    {
        case ARMV8_AARCH64_SYSREG_ICC_PMR_EL1:
            LogFlowFunc(("ICC_PMR_EL1: Interrupt priority now %u\n", (uint8_t)u64Value));
            ASMAtomicWriteU8(&pThis->bInterruptPriority, (uint8_t)u64Value);
            gicReDistUpdateIrqState(pThis, pVCpu);
            break;
        case ARMV8_AARCH64_SYSREG_ICC_IAR0_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_EOIR0_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_HPPIR0_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_BPR0_EL1:
            pThis->bBinaryPointGrp0 = (uint8_t)(u64Value & 0x7);
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R0_EL1:
            /** @todo */
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R1_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R2_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP0R3_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R0_EL1:
            /** @todo */
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R1_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R2_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_AP1R3_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_NMIAR1_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_DIR_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_RPR_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_SGI1R_EL1:
        {
            uint32_t uIntId = ARMV8_ICC_SGI1R_EL1_AARCH64_INTID_GET(u64Value) - GIC_INTID_RANGE_SGI_START;
            if (u64Value & ARMV8_ICC_SGI1R_EL1_AARCH64_IRM)
            {
                /* Route to all but this vCPU. */
                AssertReleaseFailed();
            }
            else
            {
                /* Examine target list. */
                /** @todo Range selector support. */
                VMCPUID idCpu = 0;
                uint16_t uTgtList = ARMV8_ICC_SGI1R_EL1_AARCH64_TARGET_LIST_GET(u64Value);
                /** @todo rewrite using ASMBitFirstSetU16. */
                while (uTgtList)
                {
                    if (uTgtList & 0x1)
                    {
                        PVMCPUCC pVCpuDst = VMMGetCpuById(pVCpu->CTX_SUFF(pVM), idCpu);
                        if (pVCpuDst)
                            GICSgiSet(pVCpuDst, uIntId, true /*fAsserted*/);
                        else
                            AssertFailed();
                    }
                    uTgtList >>= 1;
                    idCpu++;
                }
            }
            break;
        }
        case ARMV8_AARCH64_SYSREG_ICC_ASGI1R_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_SGI0R_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_IAR1_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_EOIR1_EL1:
        {
            /* Mark the interrupt as not active anymore, though it might still be pending. */
            if (u64Value < GIC_INTID_RANGE_SPI_START)
                ASMAtomicAndU32(&pThis->bmIntActive, ~RT_BIT_32((uint32_t)u64Value));
            else
                ASMAtomicAndU32(&pGicDev->bmIntActive, ~RT_BIT_32((uint32_t)(u64Value - GIC_INTID_RANGE_SPI_START)));

            /* Restore previous interrupt priority. */
            Assert(pThis->idxRunningPriority > 0);
            if (RT_LIKELY(pThis->idxRunningPriority))
                pThis->idxRunningPriority--;
            gicReDistUpdateIrqState(pThis, pVCpu);
            break;
        }
        case ARMV8_AARCH64_SYSREG_ICC_HPPIR1_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_BPR1_EL1:
            pThis->bBinaryPointGrp0 = (uint8_t)(u64Value & 0x7);
            break;
        case ARMV8_AARCH64_SYSREG_ICC_CTLR_EL1:
            u64Value &= ARMV8_ICC_CTLR_EL1_RW;
            /** @todo */
            break;
        case ARMV8_AARCH64_SYSREG_ICC_SRE_EL1:
            AssertReleaseFailed();
            break;
        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN0_EL1:
            ASMAtomicWriteBool(&pThis->fIrqGrp0Enabled, RT_BOOL(u64Value & ARMV8_ICC_IGRPEN0_EL1_AARCH64_ENABLE));
            break;
        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN1_EL1:
            ASMAtomicWriteBool(&pThis->fIrqGrp1Enabled, RT_BOOL(u64Value & ARMV8_ICC_IGRPEN1_EL1_AARCH64_ENABLE));
            break;
        default:
            AssertReleaseFailed();
            break;
    }

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}


/**
 * Sets the specified shared peripheral interrupt starting.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context virtual machine structure.
 * @param   uIntId              The SPI ID (minus GIC_INTID_RANGE_SPI_START) to assert/de-assert.
 * @param   fAsserted           Flag whether to mark the interrupt as asserted/de-asserted.
 */
VMM_INT_DECL(int) GICSpiSet(PVMCC pVM, uint32_t uIntId, bool fAsserted)
{
    LogFlowFunc(("pVM=%p uIntId=%u fAsserted=%RTbool\n",
                 pVM, uIntId, fAsserted));

    AssertReturn(uIntId < GIC_SPI_MAX, VERR_INVALID_PARAMETER);

    PGIC pGic = VM_TO_GIC(pVM);

    /** @todo r=aeichner There must be another way to do this better, maybe create some callback interface
     *                   the GIC can register. */
#ifdef IN_RING3
    if (pGic->fNemGic)
        return GICR3NemSpiSet(pVM, uIntId, fAsserted);
#else
# error "Impossible to call the NEM in-kernel GIC from this context!"
#endif

    PPDMDEVINS pDevIns = pGic->CTX_SUFF(pDevIns);
    PGICDEV    pThis   = PDMDEVINS_2_DATA(pDevIns, PGICDEV);

    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    /* Update the interrupts pending state. */
    if (fAsserted)
        ASMAtomicOrU32(&pThis->bmIntPending, RT_BIT_32(uIntId));
    else
        ASMAtomicAndU32(&pThis->bmIntPending, ~RT_BIT_32(uIntId));

    int rc = VBOXSTRICTRC_VAL(gicDistUpdateIrqState(pVM, pThis));
    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return rc;
}


/**
 * Sets the specified private peripheral interrupt starting.
 *
 * @returns VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   uIntId              The PPI ID (minus GIC_INTID_RANGE_PPI_START) to assert/de-assert.
 * @param   fAsserted           Flag whether to mark the interrupt as asserted/de-asserted.
 */
VMM_INT_DECL(int) GICPpiSet(PVMCPUCC pVCpu, uint32_t uIntId, bool fAsserted)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} uIntId=%u fAsserted=%RTbool\n",
                 pVCpu, pVCpu->idCpu, uIntId, fAsserted));

    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);

    /** @todo r=aeichner There must be another way to do this better, maybe create some callback interface
     *                   the GIC can register. */
#ifdef IN_RING3
    PGIC pGic = VM_TO_GIC(pVCpu->pVMR3);
    if (pGic->fNemGic)
        return GICR3NemPpiSet(pVCpu, uIntId, fAsserted);
#else
# error "Impossible to call the NEM in-kernel GIC from this context!"
#endif

    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    AssertReturn(uIntId <= (GIC_INTID_RANGE_PPI_LAST - GIC_INTID_RANGE_PPI_START), VERR_INVALID_PARAMETER);
    int rc = gicReDistInterruptSet(pVCpu, uIntId + GIC_INTID_RANGE_PPI_START, fAsserted);
    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);

    return rc;
}


/**
 * Sets the specified software generated interrupt starting.
 *
 * @returns VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   uIntId              The PPI ID (minus GIC_INTID_RANGE_SGI_START) to assert/de-assert.
 * @param   fAsserted           Flag whether to mark the interrupt as asserted/de-asserted.
 */
VMM_INT_DECL(int) GICSgiSet(PVMCPUCC pVCpu, uint32_t uIntId, bool fAsserted)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} uIntId=%u fAsserted=%RTbool\n",
                 pVCpu, pVCpu->idCpu, uIntId, fAsserted));

    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);

    /** @todo r=aeichner There must be another way to do this better, maybe create some callback interface
     *                   the GIC can register. */
#ifdef IN_RING3
    PGIC pGic = VM_TO_GIC(pVCpu->pVMR3);
    /* These should be handled in the kernel and never be set from here. */
    AssertReturn(!pGic->fNemGic, VERR_NEM_IPE_6);
#else
# error "Impossible to call the in-kernel GIC from this context!"
#endif

    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    AssertReturn(uIntId <= (GIC_INTID_RANGE_SGI_LAST - GIC_INTID_RANGE_SGI_START), VERR_INVALID_PARAMETER);
    int rc = gicReDistInterruptSet(pVCpu, uIntId + GIC_INTID_RANGE_SGI_START, fAsserted);
    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);

    return rc;
}


/**
 * Initializes per-VCPU GIC to the state following a power-up or hardware
 * reset.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 */
DECLHIDDEN(void) gicResetCpu(PVMCPUCC pVCpu)
{
    LogFlowFunc(("GIC%u\n", pVCpu->idCpu));
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);

    memset((void *)&pVCpu->gic.s.abRunningPriorities[0], 0xff, sizeof(pVCpu->gic.s.abRunningPriorities));
    pVCpu->gic.s.idxRunningPriority = 0;
    pVCpu->gic.s.bInterruptPriority = 0; /* Means no interrupt gets through to the PE. */
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicDistMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 4); RT_NOREF_PV(cb);

    PVMCPUCC pVCpu    = PDMDevHlpGetVMCPU(pDevIns);
    uint16_t offReg   = off & 0xfffc;
    uint32_t uValue   = 0;

    STAM_COUNTER_INC(&pVCpu->gic.s.CTX_SUFF_Z(StatMmioRead));

    VBOXSTRICTRC rc = VBOXSTRICTRC_VAL(gicDistRegisterRead(pDevIns, pVCpu, offReg, &uValue));
    *(uint32_t *)pv = uValue;

    Log2(("GIC%u: gicDistMmioRead: offReg=%#RX16 uValue=%#RX32\n", pVCpu->idCpu, offReg, uValue));
    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicDistMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 4); RT_NOREF_PV(cb);

    PVMCPUCC pVCpu    = PDMDevHlpGetVMCPU(pDevIns);
    uint16_t offReg   = off & 0xfffc;
    uint32_t uValue   = *(uint32_t *)pv;

    STAM_COUNTER_INC(&pVCpu->gic.s.CTX_SUFF_Z(StatMmioWrite));

    Log2(("GIC%u: gicDistMmioWrite: offReg=%#RX16 uValue=%#RX32\n", pVCpu->idCpu, offReg, uValue));
    return gicDistRegisterWrite(pDevIns, pVCpu, offReg, uValue);
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicReDistMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 4); RT_NOREF_PV(cb);

    /*
     * Determine the redistributor being targeted. Each redistributor takes GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE bytes
     * and the redistributors are adjacent.
     */
    uint32_t idReDist = off / (GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE);
    off %= (GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE);

    PVMCC pVM = PDMDevHlpGetVM(pDevIns);
    Assert(idReDist < pVM->cCpus);
    PVMCPUCC pVCpu = pVM->apCpusR3[idReDist];

    STAM_COUNTER_INC(&pVCpu->gic.s.CTX_SUFF_Z(StatMmioRead));

    /* Redistributor or SGI/PPI frame? */
    uint16_t offReg = off & 0xfffc;
    uint32_t uValue = 0;
    VBOXSTRICTRC rcStrict;
    if (off < GIC_REDIST_REG_FRAME_SIZE)
        rcStrict = gicReDistRegisterRead(pDevIns, pVCpu, idReDist, offReg, &uValue);
    else
        rcStrict = gicReDistSgiPpiRegisterRead(pDevIns, pVCpu, offReg, &uValue);

    *(uint32_t *)pv = uValue;
    Log2(("GICReDist%u: gicReDistMmioRead: off=%RGp idReDist=%u offReg=%#RX16 uValue=%#RX32 -> %Rrc\n",
          pVCpu->idCpu, off, idReDist, offReg, uValue, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicReDistMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 4); RT_NOREF_PV(cb);

    uint32_t uValue = *(uint32_t *)pv;

    /*
     * Determine the redistributor being targeted. Each redistributor takes GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE bytes
     * and the redistributors are adjacent.
     */
    uint32_t idReDist = off / (GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE);
    off %= (GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE);

    PVMCC pVM = PDMDevHlpGetVM(pDevIns);
    Assert(idReDist < pVM->cCpus);
    PVMCPUCC pVCpu = pVM->apCpusR3[idReDist];

    STAM_COUNTER_INC(&pVCpu->gic.s.CTX_SUFF_Z(StatMmioWrite));

    /* Redistributor or SGI/PPI frame? */
    uint16_t offReg = off & 0xfffc;
    VBOXSTRICTRC rcStrict;
    if (off < GIC_REDIST_REG_FRAME_SIZE)
        rcStrict = gicReDistRegisterWrite(pDevIns, pVCpu, offReg, uValue);
    else
        rcStrict = gicReDistSgiPpiRegisterWrite(pDevIns, pVCpu, offReg, uValue);

    Log2(("GICReDist%u: gicReDistMmioWrite: off=%RGp idReDist=%u offReg=%#RX16 uValue=%#RX32 -> %Rrc\n",
          pVCpu->idCpu, off, idReDist, offReg, uValue, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


#ifndef IN_RING3

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) gicRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    AssertReleaseFailed();
    return VINF_SUCCESS;
}
#endif /* !IN_RING3 */

/**
 * GIC device registration structure.
 */
const PDMDEVREG g_DeviceGIC =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "gic",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_PIC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(GICDEV),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Generic Interrupt Controller",
#if defined(IN_RING3)
    /* .szRCMod = */                "VMMRC.rc",
    /* .szR0Mod = */                "VMMR0.r0",
    /* .pfnConstruct = */           gicR3Construct,
    /* .pfnDestruct = */            gicR3Destruct,
    /* .pfnRelocate = */            gicR3Relocate,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               gicR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           gicRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           gicRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

