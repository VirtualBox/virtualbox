/* $Id: CPUMAllMsrs.cpp 107840 2025-01-15 09:52:16Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - CPU MSR Registers.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_CPUM
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/hm_vmx.h>
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
# include <VBox/vmm/iem.h>
#endif
#include <VBox/vmm/tm.h>
#include <VBox/vmm/gim.h>
#include "CPUMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/**
 * Validates the CPUMMSRRANGE::offCpumCpu value and declares a local variable
 * pointing to it.
 *
 * ASSUMES sizeof(a_Type) is a power of two and that the member is aligned
 * correctly.
 */
#define CPUM_MSR_ASSERT_CPUMCPU_OFFSET_RETURN(a_pVCpu, a_pRange, a_Type, a_VarName) \
    AssertMsgReturn(   (a_pRange)->offCpumCpu >= 8 \
                    && (a_pRange)->offCpumCpu < sizeof(CPUMCPU) \
                    && !((a_pRange)->offCpumCpu & (RT_MIN(sizeof(a_Type), 8) - 1)) \
                    , ("offCpumCpu=%#x %s\n", (a_pRange)->offCpumCpu, (a_pRange)->szName), \
                    VERR_CPUM_MSR_BAD_CPUMCPU_OFFSET); \
    a_Type *a_VarName = (a_Type *)((uintptr_t)&(a_pVCpu)->cpum.s + (a_pRange)->offCpumCpu)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Implements reading one or more MSRs.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_CPUM_R3_MSR_READ if the MSR read could not be serviced in the
 *          current context (raw-mode or ring-0).
 * @retval  VERR_CPUM_RAISE_GP_0 on failure (invalid MSR).
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR we're reading.
 * @param   pRange      The MSR range descriptor.
 * @param   puValue     Where to return the value.
 */
typedef DECLCALLBACKTYPE(VBOXSTRICTRC, FNCPUMRDMSR,(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue));
/** Pointer to a RDMSR worker for a specific MSR or range of MSRs.  */
typedef FNCPUMRDMSR *PFNCPUMRDMSR;


/**
 * Implements writing one or more MSRs.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_CPUM_R3_MSR_WRITE if the MSR write could not be serviced in the
 *          current context (raw-mode or ring-0).
 * @retval  VERR_CPUM_RAISE_GP_0 on failure.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR we're writing.
 * @param   pRange      The MSR range descriptor.
 * @param   uValue      The value to set, ignored bits masked.
 * @param   uRawValue   The raw value with the ignored bits not masked.
 */
typedef DECLCALLBACKTYPE(VBOXSTRICTRC, FNCPUMWRMSR,(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange,
                                                    uint64_t uValue, uint64_t uRawValue));
/** Pointer to a WRMSR worker for a specific MSR or range of MSRs.  */
typedef FNCPUMWRMSR *PFNCPUMWRMSR;



/*
 * Generic functions.
 * Generic functions.
 * Generic functions.
 */


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_FixedValue(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IgnoreWrite(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    Log(("CPUM: Ignoring WRMSR %#x (%s), %#llx\n", idMsr, pRange->szName, uValue));
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_WriteOnly(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(puValue);
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_ReadOnly(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    Assert(pRange->fWrGpMask == UINT64_MAX);
    return VERR_CPUM_RAISE_GP_0;
}




/*
 * IA32
 * IA32
 * IA32
 */

/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32P5McAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = 0; /** @todo implement machine check injection. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32P5McAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement machine check injection. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32P5McType(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = 0; /** @todo implement machine check injection. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32P5McType(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement machine check injection. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32TimestampCounter(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = TMCpuTickGet(pVCpu);
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    *puValue = CPUMApplyNestedGuestTscOffset(pVCpu, *puValue);
#endif
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32TimestampCounter(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    TMCpuTickSet(pVCpu->CTX_SUFF(pVM), pVCpu, uValue);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32PlatformId(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    uint64_t uValue = pRange->uValue;
    if (uValue & 0x1f00)
    {
        /* Max allowed bus ratio present. */
        /** @todo Implement scaled BUS frequency. */
    }

    *puValue = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32ApicBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    return APICGetBaseMsr(pVCpu, puValue);
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32ApicBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    return APICSetBaseMsr(pVCpu, uValue);
}


/**
 * Gets IA32_FEATURE_CONTROL value for IEM, NEM and cpumMsrRd_Ia32FeatureControl.
 *
 * @returns IA32_FEATURE_CONTROL value.
 * @param   pVCpu           The cross context per CPU structure.
 */
VMM_INT_DECL(uint64_t) CPUMGetGuestIa32FeatCtrl(PCVMCPUCC pVCpu)
{
    uint64_t uFeatCtrlMsr = MSR_IA32_FEATURE_CONTROL_LOCK;
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        uFeatCtrlMsr |= MSR_IA32_FEATURE_CONTROL_VMXON;
    return uFeatCtrlMsr;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32FeatureControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = CPUMGetGuestIa32FeatCtrl(pVCpu);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32FeatureControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32BiosSignId(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo fake microcode update. */
    PVM const pVM = pVCpu->CTX_SUFF(pVM);
    if (pVM->cpum.s.GuestInfo.uMicrocodeRevision != UINT32_MAX)
        *puValue = RT_MAKE_U64(RT_LO_U32(pRange->uValue), pVM->cpum.s.GuestInfo.uMicrocodeRevision);
    else
        *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32BiosSignId(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /* Normally, zero is written to Ia32BiosSignId before reading it in order
       to select the signature instead of the BBL_CR_D3 behaviour.  The GP mask
       of the database entry should take care of most illegal writes for now, so
       just ignore all writes atm. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32BiosUpdateTrigger(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);

    /* Microcode updates cannot be loaded in VMX non-root mode. */
    if (CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.s.Guest))
        return VINF_SUCCESS;

    /** @todo Fake bios update trigger better.  The value is the address to an
     *        update package, I think.  We should probably GP if it's invalid. */
    return VINF_SUCCESS;
}


/**
 * Get MSR_IA32_SMM_MONITOR_CTL value for IEM and cpumMsrRd_Ia32SmmMonitorCtl.
 *
 * @returns The MSR_IA32_SMM_MONITOR_CTL value.
 * @param   pVCpu           The cross context per CPU structure.
 */
VMM_INT_DECL(uint64_t) CPUMGetGuestIa32SmmMonitorCtl(PCVMCPUCC pVCpu)
{
    /* We do not support dual-monitor treatment for SMI and SMM. */
    /** @todo SMM. */
    RT_NOREF(pVCpu);
    return 0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32SmmMonitorCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = CPUMGetGuestIa32SmmMonitorCtl(pVCpu);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32SmmMonitorCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo SMM. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32PmcN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo check CPUID leaf 0ah. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32PmcN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo check CPUID leaf 0ah. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32MonitorFilterLineSize(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo return 0x1000 if we try emulate mwait 100% correctly. */
    *puValue = 0x40; /** @todo Change to CPU cache line size. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32MonitorFilterLineSize(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo should remember writes, though it's supposedly something only a BIOS
     * would write so, it's not extremely important. */
    return VINF_SUCCESS;
}

/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32MPerf(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Read MPERF: Adjust against previously written MPERF value.  Is TSC
     *        what we want? */
    *puValue = TMCpuTickGet(pVCpu);
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    *puValue = CPUMApplyNestedGuestTscOffset(pVCpu, *puValue);
#endif
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32MPerf(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Write MPERF: Calc adjustment. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32APerf(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Read APERF: Adjust against previously written MPERF value.  Is TSC
     *        what we want? */
    *puValue = TMCpuTickGet(pVCpu);
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    *puValue = CPUMApplyNestedGuestTscOffset(pVCpu, *puValue);
#endif
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32APerf(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Write APERF: Calc adjustment. */
    return VINF_SUCCESS;
}


/**
 * Get fixed IA32_MTRR_CAP value for NEM and cpumMsrRd_Ia32MtrrCap.
 *
 * @returns Fixed IA32_MTRR_CAP value.
 * @param   pVCpu           The cross context per CPU structure.
 */
VMM_INT_DECL(uint64_t) CPUMGetGuestIa32MtrrCap(PCVMCPUCC pVCpu)
{
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.fMtrrRead)
        return pVCpu->cpum.s.GuestMsrs.msr.MtrrCap;

    /* This is currently a bit weird. :-) */
    uint8_t const   cVariableRangeRegs              = 0;
    bool const      fSystemManagementRangeRegisters = false;
    bool const      fFixedRangeRegisters            = false;
    bool const      fWriteCombiningType             = false;
    bool const      fProcRsvdRangeRegisters         = false;
    return cVariableRangeRegs
         | (fFixedRangeRegisters            ? MSR_IA32_MTRR_CAP_FIX   : 0)
         | (fWriteCombiningType             ? MSR_IA32_MTRR_CAP_WC    : 0)
         | (fSystemManagementRangeRegisters ? MSR_IA32_MTRR_CAP_SMRR  : 0)
         | (fProcRsvdRangeRegisters         ? MSR_IA32_MTRR_CAP_PRMRR : 0);
}

/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32MtrrCap(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = CPUMGetGuestIa32MtrrCap(pVCpu);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32MtrrPhysBaseN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    Assert(pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fMtrr);
    Assert(pRange->uValue == (idMsr -  MSR_IA32_MTRR_PHYSBASE0) / 2);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.fMtrrRead)
    {
        AssertLogRelMsgReturn(pRange->uValue < RT_ELEMENTS(pVCpu->cpum.s.GuestMsrs.msr.aMtrrVarMsrs),
                              ("MTRR MSR (%#RX32) out-of-bounds, must be <= %#RX32\n", idMsr, CPUMCTX_MAX_MTRRVAR_COUNT),
                              VERR_CPUM_RAISE_GP_0);
        AssertLogRelMsgReturn(!(idMsr % 2),
                              ("MTRR MSR (%#RX32) invalid, must be at even offset\n", idMsr), VERR_CPUM_RAISE_GP_0);
        *puValue = pVCpu->cpum.s.GuestMsrs.msr.aMtrrVarMsrs[pRange->uValue].MtrrPhysBase;
    }
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32MtrrPhysBaseN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    /*
     * Validate the value.
     */
    Assert(pRange->uValue == (idMsr - MSR_IA32_MTRR_PHYSBASE0) / 2);
    RT_NOREF_PV(uRawValue);
    Assert(pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fMtrr);

    uint8_t uType = uValue & 0xff;
    if ((uType >= 7) || (uType == 2) || (uType == 3))
    {
        Log(("CPUM: Invalid type set writing MTRR PhysBase MSR %#x: %#llx (%#llx)\n", idMsr, uValue, uType));
        return VERR_CPUM_RAISE_GP_0;
    }

    uint64_t fInvPhysMask = ~(RT_BIT_64(pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.cMaxPhysAddrWidth) - 1U);
    if (fInvPhysMask & uValue)
    {
        Log(("CPUM: Invalid physical address bits set writing MTRR PhysBase MSR %#x: %#llx (%#llx)\n",
             idMsr, uValue, uValue & fInvPhysMask));
        return VERR_CPUM_RAISE_GP_0;
    }

    /*
     * Store it.
     */
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.fMtrrWrite)
    {
        AssertCompile(CPUMCTX_MAX_MTRRVAR_COUNT == RT_ELEMENTS(pVCpu->cpum.s.GuestMsrs.msr.aMtrrVarMsrs));
        AssertLogRelMsgReturn(pRange->uValue < CPUMCTX_MAX_MTRRVAR_COUNT,
                              ("MTRR MSR (%#RX32) out-of-bounds, must be <= %#RX32\n", idMsr, CPUMCTX_MAX_MTRRVAR_COUNT),
                              VERR_CPUM_RAISE_GP_0);
        AssertLogRelMsgReturn(!(idMsr % 2),
                              ("MTRR MSR (%#RX32) invalid, must be at even offset\n", idMsr), VERR_CPUM_RAISE_GP_0);
        pVCpu->cpum.s.GuestMsrs.msr.aMtrrVarMsrs[pRange->uValue].MtrrPhysBase = uValue;
        /** @todo Act on the potential memory type change. */
    }
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32MtrrPhysMaskN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr);
    Assert(pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fMtrr);
    Assert(pRange->uValue == (idMsr - MSR_IA32_MTRR_PHYSBASE0) / 2);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.fMtrrRead)
    {
        AssertLogRelMsgReturn(pRange->uValue < RT_ELEMENTS(pVCpu->cpum.s.GuestMsrs.msr.aMtrrVarMsrs),
                              ("MTRR MSR (%#RX32) out-of-bounds, must be <= %#RX32\n", idMsr, CPUMCTX_MAX_MTRRVAR_COUNT),
                              VERR_CPUM_RAISE_GP_0);
        AssertLogRelMsgReturn(idMsr % 2,
                              ("MTRR MSR (%#RX32) invalid, must be at odd offset\n", idMsr), VERR_CPUM_RAISE_GP_0);
        *puValue = pVCpu->cpum.s.GuestMsrs.msr.aMtrrVarMsrs[pRange->uValue].MtrrPhysMask;
    }
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32MtrrPhysMaskN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    /*
     * Validate the value.
     */
    Assert(pRange->uValue == (idMsr - MSR_IA32_MTRR_PHYSBASE0) / 2);
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(uRawValue); RT_NOREF_PV(pRange);
    Assert(pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fMtrr);

    uint64_t fInvPhysMask = ~(RT_BIT_64(pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.cMaxPhysAddrWidth) - 1U);
    if (fInvPhysMask & uValue)
    {
        Log(("CPUM: Invalid physical address bits set writing MTRR PhysMask MSR %#x: %#llx (%#llx)\n",
             idMsr, uValue, uValue & fInvPhysMask));
        return VERR_CPUM_RAISE_GP_0;
    }

    /*
     * Store it.
     */
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.fMtrrWrite)
    {
        AssertLogRelMsgReturn(pRange->uValue < RT_ELEMENTS(pVCpu->cpum.s.GuestMsrs.msr.aMtrrVarMsrs),
                              ("MTRR MSR (%#RX32) out-of-bounds, must be <= %#RX32\n", idMsr, CPUMCTX_MAX_MTRRVAR_COUNT),
                              VERR_CPUM_RAISE_GP_0);
        AssertLogRelMsgReturn(idMsr % 2,
                              ("MTRR MSR (%#RX32) invalid, must be at odd offset\n", idMsr), VERR_CPUM_RAISE_GP_0);
        pVCpu->cpum.s.GuestMsrs.msr.aMtrrVarMsrs[pRange->uValue].MtrrPhysMask = uValue;
        /** @todo Act on the potential memory type change. */
    }
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32MtrrFixed(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    CPUM_MSR_ASSERT_CPUMCPU_OFFSET_RETURN(pVCpu, pRange, uint64_t, puFixedMtrr);
    Assert(pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fMtrr);
    *puValue = *puFixedMtrr;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32MtrrFixed(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    CPUM_MSR_ASSERT_CPUMCPU_OFFSET_RETURN(pVCpu, pRange, uint64_t, puFixedMtrr);
    RT_NOREF_PV(idMsr); RT_NOREF_PV(uRawValue);
    Assert(pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fMtrr);

    for (uint32_t cShift = 0; cShift < 63; cShift += 8)
    {
        uint8_t uType = (uint8_t)(uValue >> cShift);
        if ((uType >= 7) || (uType == 2) || (uType == 3))
        {
            Log(("CPUM: Invalid MTRR type at %u:%u in fixed range (%#x/%s): %#llx (%#llx)\n",
                 cShift + 7, cShift, idMsr, pRange->szName, uValue, uType));
            return VERR_CPUM_RAISE_GP_0;
        }
    }
    *puFixedMtrr = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32MtrrDefType(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    Assert(pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fMtrr);
    *puValue = pVCpu->cpum.s.GuestMsrs.msr.MtrrDefType;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32MtrrDefType(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    Assert(pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fMtrr);

    uint8_t uType = uValue & MSR_IA32_MTRR_DEF_TYPE_DEF_MT_MASK;
    if ((uType >= 7) || (uType == 2) || (uType == 3))
    {
        Log(("CPUM: Invalid MTRR default type value on %s: %#llx (%#llx)\n", pRange->szName, uValue, uType));
        return VERR_CPUM_RAISE_GP_0;
    }

    pVCpu->cpum.s.GuestMsrs.msr.MtrrDefType = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32Pat(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.msrPAT;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32Pat(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    if (CPUMIsPatMsrValid(uValue))
    {
        pVCpu->cpum.s.Guest.msrPAT = uValue;
        return VINF_SUCCESS;
    }
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32SysEnterCs(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.SysEnter.cs;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32SysEnterCs(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);

    /* Note! We used to mask this by 0xffff, but turns out real HW doesn't and
             there are generally 32-bit working bits backing this register. */
    pVCpu->cpum.s.Guest.SysEnter.cs = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32SysEnterEsp(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.SysEnter.esp;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32SysEnterEsp(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    if (X86_IS_CANONICAL(uValue))
    {
        pVCpu->cpum.s.Guest.SysEnter.esp = uValue;
        return VINF_SUCCESS;
    }
    Log(("CPUM: IA32_SYSENTER_ESP not canonical! %#llx\n", uValue));
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32SysEnterEip(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.SysEnter.eip;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32SysEnterEip(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    if (X86_IS_CANONICAL(uValue))
    {
        pVCpu->cpum.s.Guest.SysEnter.eip = uValue;
        return VINF_SUCCESS;
    }
    LogRel(("CPUM: IA32_SYSENTER_EIP not canonical! %#llx\n", uValue));
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32McgCap(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
#if 0 /** @todo implement machine checks. */
    *puValue = pRange->uValue & (RT_BIT_64(8) | 0);
#else
    *puValue = 0;
#endif
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32McgStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement machine checks. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32McgStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement machine checks. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32McgCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement machine checks. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32McgCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement machine checks. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32DebugCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement IA32_DEBUGCTL. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32DebugCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement IA32_DEBUGCTL. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32SmrrPhysBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement intel SMM. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32SmrrPhysBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement intel SMM. */
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32SmrrPhysMask(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement intel SMM. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32SmrrPhysMask(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement intel SMM. */
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32PlatformDcaCap(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement intel direct cache access (DCA)?? */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32PlatformDcaCap(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement intel direct cache access (DCA)?? */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32CpuDcaCap(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement intel direct cache access (DCA)?? */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32Dca0Cap(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement intel direct cache access (DCA)?? */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32Dca0Cap(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement intel direct cache access (DCA)?? */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32PerfEvtSelN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement IA32_PERFEVTSEL0+. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32PerfEvtSelN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement IA32_PERFEVTSEL0+. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32PerfStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr);
    uint64_t uValue = pRange->uValue;

    /* Always provide the max bus ratio for now.  XNU expects it. */
    uValue &= ~((UINT64_C(0x1f) << 40) | RT_BIT_64(46));

    PVMCC    pVM            = pVCpu->CTX_SUFF(pVM);
    uint64_t uScalableBusHz = CPUMGetGuestScalableBusFrequency(pVM);
    uint64_t uTscHz         = TMCpuTicksPerSecond(pVM);
    uint8_t  uTscRatio      = (uint8_t)((uTscHz + uScalableBusHz / 2) / uScalableBusHz);
    if (uTscRatio > 0x1f)
        uTscRatio = 0x1f;
    uValue |= (uint64_t)uTscRatio << 40;

    *puValue = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32PerfStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /* Pentium4 allows writing, but all bits are ignored. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32PerfCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement IA32_PERFCTL. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32PerfCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement IA32_PERFCTL. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32FixedCtrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement IA32_FIXED_CTRn (fixed performance counters). */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32FixedCtrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement IA32_FIXED_CTRn (fixed performance counters). */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32PerfCapabilities(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement performance counters. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32PerfCapabilities(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement performance counters. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32FixedCtrCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement performance counters. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32FixedCtrCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement performance counters. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32PerfGlobalStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement performance counters. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32PerfGlobalStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement performance counters. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32PerfGlobalCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement performance counters. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32PerfGlobalCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement performance counters. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32PerfGlobalOvfCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement performance counters. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32PerfGlobalOvfCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement performance counters. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32PebsEnable(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement performance counters. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32PebsEnable(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement performance counters. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32ClockModulation(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement IA32_CLOCK_MODULATION. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32ClockModulation(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement IA32_CLOCK_MODULATION. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32ThermInterrupt(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement IA32_THERM_INTERRUPT. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32ThermInterrupt(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement IA32_THERM_STATUS. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32ThermStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement IA32_THERM_STATUS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32ThermStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement IA32_THERM_INTERRUPT. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32Therm2Ctl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement IA32_THERM2_CTL. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32Therm2Ctl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement IA32_THERM2_CTL. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32MiscEnable(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.GuestMsrs.msr.MiscEnable;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32MiscEnable(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
#ifdef LOG_ENABLED
    uint64_t const uOld = pVCpu->cpum.s.GuestMsrs.msr.MiscEnable;
#endif

    /* Unsupported bits are generally ignored and stripped by the MSR range
       entry that got us here. So, we just need to preserve fixed bits. */
    pVCpu->cpum.s.GuestMsrs.msr.MiscEnable = uValue
                                           | MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL
                                           | MSR_IA32_MISC_ENABLE_BTS_UNAVAIL;

    Log(("CPUM: IA32_MISC_ENABLE; old=%#llx written=%#llx => %#llx\n",
         uOld, uValue,  pVCpu->cpum.s.GuestMsrs.msr.MiscEnable));

    /** @todo Wire IA32_MISC_ENABLE bit 22 to our NT 4 CPUID trick. */
    /** @todo Wire up MSR_IA32_MISC_ENABLE_XD_DISABLE. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32McCtlStatusAddrMiscN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(pRange);

    /** @todo Implement machine check exception injection. */
    switch (idMsr & 3)
    {
        case 0:
        case 1:
            *puValue = 0;
            break;

        /* The ADDR and MISC registers aren't accessible since the
           corresponding STATUS bits are zero. */
        case 2:
            Log(("CPUM: Reading IA32_MCi_ADDR %#x -> #GP\n", idMsr));
            return VERR_CPUM_RAISE_GP_0;
        case 3:
            Log(("CPUM: Reading IA32_MCi_MISC %#x -> #GP\n", idMsr));
            return VERR_CPUM_RAISE_GP_0;
    }
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32McCtlStatusAddrMiscN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    switch (idMsr & 3)
    {
        case 0:
            /* Ignore writes to the CTL register. */
            break;

        case 1:
            /* According to specs, the STATUS register can only be written to
               with the value 0.  VBoxCpuReport thinks different for a
               Pentium M Dothan, but implementing according to specs now. */
            if (uValue != 0)
            {
                Log(("CPUM: Writing non-zero value (%#llx) to IA32_MCi_STATUS %#x -> #GP\n", uValue, idMsr));
                return VERR_CPUM_RAISE_GP_0;
            }
            break;

        /* Specs states that ADDR and MISC can be cleared by writing zeros.
           Writing 1s will GP.  Need to figure out how this relates to the
           ADDRV and MISCV status flags.  If writing is independent of those
           bits, we need to know whether the CPU really implements them since
           that is exposed by writing 0 to them.
           Implementing the solution with the fewer GPs for now. */
        case 2:
            if (uValue != 0)
            {
                Log(("CPUM: Writing non-zero value (%#llx) to IA32_MCi_ADDR %#x -> #GP\n", uValue, idMsr));
                return VERR_CPUM_RAISE_GP_0;
            }
            break;
        case 3:
            if (uValue != 0)
            {
                Log(("CPUM: Writing non-zero value (%#llx) to IA32_MCi_MISC %#x -> #GP\n", uValue, idMsr));
                return VERR_CPUM_RAISE_GP_0;
            }
            break;
    }
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32McNCtl2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Implement machine check exception injection. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32McNCtl2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Implement machine check exception injection. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32DsArea(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement IA32_DS_AREA. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32DsArea(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32TscDeadline(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement TSC deadline timer. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32TscDeadline(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement TSC deadline timer. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32X2ApicN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pRange);
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (   CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.s.Guest)
        && CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.s.Guest, VMX_PROC_CTLS2_VIRT_X2APIC_MODE))
    {
        VBOXSTRICTRC rcStrict = IEMExecVmxVirtApicAccessMsr(pVCpu, idMsr, puValue, false /* fWrite */);
        if (rcStrict == VINF_VMX_MODIFIES_BEHAVIOR)
            return VINF_SUCCESS;
        if (rcStrict == VERR_OUT_OF_RANGE)
            return VERR_CPUM_RAISE_GP_0;
        Assert(rcStrict == VINF_VMX_INTERCEPT_NOT_ACTIVE);
    }
#endif
    return APICReadMsr(pVCpu, idMsr, puValue);
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32X2ApicN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (   CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.s.Guest)
        && CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.s.Guest, VMX_PROC_CTLS2_VIRT_X2APIC_MODE))
    {
        VBOXSTRICTRC rcStrict = IEMExecVmxVirtApicAccessMsr(pVCpu, idMsr, &uValue, true /* fWrite */);
        if (rcStrict == VINF_VMX_MODIFIES_BEHAVIOR)
            return VINF_SUCCESS;
        if (rcStrict == VERR_OUT_OF_RANGE)
            return VERR_CPUM_RAISE_GP_0;
        Assert(rcStrict == VINF_VMX_INTERCEPT_NOT_ACTIVE);
    }
#endif
    return APICWriteMsr(pVCpu, idMsr, uValue);
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32DebugInterface(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo IA32_DEBUG_INTERFACE (no docs)  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32DebugInterface(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo IA32_DEBUG_INTERFACE (no docs)  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxBasic(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64Basic;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxPinbasedCtls(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.PinCtls.u;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}

/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxProcbasedCtls(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.ProcCtls.u;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxExitCtls(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.ExitCtls.u;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxEntryCtls(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.EntryCtls.u;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}



/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxMisc(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64Misc;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxCr0Fixed0(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64Cr0Fixed0;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxCr0Fixed1(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64Cr0Fixed1;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxCr4Fixed0(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64Cr4Fixed0;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxCr4Fixed1(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64Cr4Fixed1;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxVmcsEnum(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64VmcsEnum;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxProcBasedCtls2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.ProcCtls2.u;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/**
 * Get fixed IA32_VMX_EPT_VPID_CAP value for PGM and cpumMsrRd_Ia32VmxEptVpidCap.
 *
 * @returns Fixed IA32_VMX_EPT_VPID_CAP value.
 * @param   pVCpu           The cross context per CPU structure.
 */
VMM_INT_DECL(uint64_t) CPUMGetGuestIa32VmxEptVpidCap(PCVMCPUCC pVCpu)
{
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        return pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64EptVpidCaps;
    return 0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxEptVpidCap(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = CPUMGetGuestIa32VmxEptVpidCap(pVCpu);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxTruePinbasedCtls(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.TruePinCtls.u;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxTrueProcbasedCtls(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.TrueProcCtls.u;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxTrueExitCtls(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.TrueExitCtls.u;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxTrueEntryCtls(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.TrueEntryCtls.u;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32VmxVmFunc(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    if (pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.fVmx)
        *puValue = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64VmFunc;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32SpecCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.GuestMsrs.msr.SpecCtrl;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32SpecCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);

    /* NB: The STIBP bit can be set even when IBRS is present, regardless of whether STIBP is actually implemented. */
    if (uValue & ~(MSR_IA32_SPEC_CTRL_F_IBRS | MSR_IA32_SPEC_CTRL_F_STIBP))
    {
        Log(("CPUM: Invalid IA32_SPEC_CTRL bits (trying to write %#llx)\n", uValue));
        return VERR_CPUM_RAISE_GP_0;
    }

    pVCpu->cpum.s.GuestMsrs.msr.SpecCtrl = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32PredCmd(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Ia32ArchCapabilities(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.GuestMsrs.msr.ArchCaps;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Ia32FlushCmd(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    if ((uValue & ~MSR_IA32_FLUSH_CMD_F_L1D) == 0)
        return VINF_SUCCESS;
    Log(("CPUM: Invalid MSR_IA32_FLUSH_CMD_ bits (trying to write %#llx)\n", uValue));
    return VERR_CPUM_RAISE_GP_0;
}



/*
 * AMD64
 * AMD64
 * AMD64
 */


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Amd64Efer(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.msrEFER;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Amd64Efer(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    uint64_t uValidatedEfer;
    uint64_t const uOldEfer = pVCpu->cpum.s.Guest.msrEFER;
    int rc = CPUMIsGuestEferMsrWriteValid(pVCpu->CTX_SUFF(pVM), pVCpu->cpum.s.Guest.cr0, uOldEfer, uValue, &uValidatedEfer);
    if (RT_FAILURE(rc))
        return VERR_CPUM_RAISE_GP_0;

    CPUMSetGuestEferMsrNoChecks(pVCpu, uOldEfer, uValidatedEfer);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Amd64SyscallTarget(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.msrSTAR;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Amd64SyscallTarget(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    pVCpu->cpum.s.Guest.msrSTAR = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Amd64LongSyscallTarget(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.msrLSTAR;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Amd64LongSyscallTarget(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    if (!X86_IS_CANONICAL(uValue))
    {
        Log(("CPUM: wrmsr %s(%#x), %#llx -> #GP - not canonical\n", pRange->szName, idMsr, uValue));
        return VERR_CPUM_RAISE_GP_0;
    }
    pVCpu->cpum.s.Guest.msrLSTAR = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Amd64CompSyscallTarget(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.msrCSTAR;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Amd64CompSyscallTarget(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    if (!X86_IS_CANONICAL(uValue))
    {
        Log(("CPUM: wrmsr %s(%#x), %#llx -> #GP - not canonical\n", pRange->szName, idMsr, uValue));
        return VERR_CPUM_RAISE_GP_0;
    }
    pVCpu->cpum.s.Guest.msrCSTAR = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Amd64SyscallFlagMask(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.msrSFMASK;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Amd64SyscallFlagMask(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    /* The high bits are ignored and read-as-zero, writing to them does not raise #GP. See @bugref{10610}.*/
    pVCpu->cpum.s.Guest.msrSFMASK = uValue & UINT32_MAX;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Amd64FsBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.fs.u64Base;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Amd64FsBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    if (X86_IS_CANONICAL(uValue))
    {
        pVCpu->cpum.s.Guest.fs.u64Base = uValue;
        return VINF_SUCCESS;
    }
    Log(("CPUM: wrmsr %s(%#x), %#llx -> #GP - not canonical\n", pRange->szName, idMsr, uValue));
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Amd64GsBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.gs.u64Base;
    return VINF_SUCCESS;
}

/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Amd64GsBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    if (X86_IS_CANONICAL(uValue))
    {
        pVCpu->cpum.s.Guest.gs.u64Base = uValue;
        return VINF_SUCCESS;
    }
    Log(("CPUM: wrmsr %s(%#x), %#llx -> #GP - not canonical\n", pRange->szName, idMsr, uValue));
    return VERR_CPUM_RAISE_GP_0;
}



/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Amd64KernelGsBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.msrKERNELGSBASE;
    return VINF_SUCCESS;
}

/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Amd64KernelGsBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    if (X86_IS_CANONICAL(uValue))
    {
        pVCpu->cpum.s.Guest.msrKERNELGSBASE = uValue;
        return VINF_SUCCESS;
    }
    Log(("CPUM: wrmsr %s(%#x), %#llx -> #GP - not canonical\n", pRange->szName, idMsr, uValue));
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Amd64TscAux(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.GuestMsrs.msr.TscAux;
    return VINF_SUCCESS;
}

/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Amd64TscAux(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    pVCpu->cpum.s.GuestMsrs.msr.TscAux = uValue;
    return VINF_SUCCESS;
}


/*
 * Intel specific
 * Intel specific
 * Intel specific
 */

/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelEblCrPowerOn(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo recalc clock frequency ratio? */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelEblCrPowerOn(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Write EBL_CR_POWERON: Remember written bits. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7CoreThreadCount(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);

    /* Note! According to cpuid_set_info in XNU (10.7.0), Westmere CPU only
             have a 4-bit core count. */
    uint16_t cCores   = pVCpu->CTX_SUFF(pVM)->cCpus;
    uint16_t cThreads = cCores; /** @todo hyper-threading. */
    *puValue = RT_MAKE_U32(cThreads, cCores);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelP4EbcHardPowerOn(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo P4 hard power on config */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelP4EbcHardPowerOn(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo P4 hard power on config */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelP4EbcSoftPowerOn(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo P4 soft power on config  */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelP4EbcSoftPowerOn(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo P4 soft power on config */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelP4EbcFrequencyId(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr);

    uint64_t uValue;
    PVMCC    pVM            = pVCpu->CTX_SUFF(pVM);
    uint64_t uScalableBusHz = CPUMGetGuestScalableBusFrequency(pVM);
    if (pVM->cpum.s.GuestFeatures.uModel >= 2)
    {
        if (uScalableBusHz <= CPUM_SBUSFREQ_100MHZ && pVM->cpum.s.GuestFeatures.uModel <= 2)
        {
            uScalableBusHz = CPUM_SBUSFREQ_100MHZ;
            uValue = 0;
        }
        else if (uScalableBusHz <= CPUM_SBUSFREQ_133MHZ)
        {
            uScalableBusHz = CPUM_SBUSFREQ_133MHZ;
            uValue = 1;
        }
        else if (uScalableBusHz <= CPUM_SBUSFREQ_167MHZ)
        {
            uScalableBusHz = CPUM_SBUSFREQ_167MHZ;
            uValue = 3;
        }
        else if (uScalableBusHz <= CPUM_SBUSFREQ_200MHZ)
        {
            uScalableBusHz = CPUM_SBUSFREQ_200MHZ;
            uValue = 2;
        }
        else if (uScalableBusHz <= CPUM_SBUSFREQ_267MHZ && pVM->cpum.s.GuestFeatures.uModel > 2)
        {
            uScalableBusHz = CPUM_SBUSFREQ_267MHZ;
            uValue = 0;
        }
        else
        {
            uScalableBusHz = CPUM_SBUSFREQ_333MHZ;
            uValue = 6;
        }
        uValue <<= 16;

        uint64_t uTscHz    = TMCpuTicksPerSecond(pVM);
        uint8_t  uTscRatio = (uint8_t)((uTscHz + uScalableBusHz / 2) / uScalableBusHz);
        uValue |= (uint32_t)uTscRatio << 24;

        uValue |= pRange->uValue & ~UINT64_C(0xff0f0000);
    }
    else
    {
        /* Probably more stuff here, but intel doesn't want to tell us. */
        uValue = pRange->uValue;
        uValue &= ~(RT_BIT_64(21) | RT_BIT_64(22) | RT_BIT_64(23)); /* 100 MHz is only documented value */
    }

    *puValue = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelP4EbcFrequencyId(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo P4 bus frequency config  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelP6FsbFrequency(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr);

    /* Convert the scalable bus frequency to the encoding in the intel manual (for core+). */
    uint64_t uScalableBusHz = CPUMGetGuestScalableBusFrequency(pVCpu->CTX_SUFF(pVM));
    if (uScalableBusHz <= CPUM_SBUSFREQ_100MHZ)
        *puValue = 5;
    else if (uScalableBusHz <= CPUM_SBUSFREQ_133MHZ)
        *puValue = 1;
    else if (uScalableBusHz <= CPUM_SBUSFREQ_167MHZ)
        *puValue = 3;
    else if (uScalableBusHz <= CPUM_SBUSFREQ_200MHZ)
        *puValue = 2;
    else if (uScalableBusHz <= CPUM_SBUSFREQ_267MHZ)
        *puValue = 0;
    else if (uScalableBusHz <= CPUM_SBUSFREQ_333MHZ)
        *puValue = 4;
    else /*if (uScalableBusHz <= CPUM_SBUSFREQ_400MHZ)*/
        *puValue = 6;

    *puValue |= pRange->uValue & ~UINT64_C(0x7);

    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelPlatformInfo(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);

    /* Just indicate a fixed TSC, no turbo boost, no programmable anything. */
    PVMCC    pVM            = pVCpu->CTX_SUFF(pVM);
    uint64_t uScalableBusHz = CPUMGetGuestScalableBusFrequency(pVM);
    uint64_t uTscHz         = TMCpuTicksPerSecond(pVM);
    uint8_t  uTscRatio      = (uint8_t)((uTscHz + uScalableBusHz / 2) / uScalableBusHz);
    uint64_t uValue         = ((uint32_t)uTscRatio << 8)   /* TSC invariant frequency. */
                            | ((uint64_t)uTscRatio << 40); /* The max turbo frequency. */

    /* Ivy bridge has a minimum operating ratio as well. */
    if (true) /** @todo detect sandy bridge. */
        uValue |= (uint64_t)uTscRatio << 48;

    *puValue = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelFlexRatio(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr);

    uint64_t uValue = pRange->uValue & ~UINT64_C(0x1ff00);

    PVMCC    pVM            = pVCpu->CTX_SUFF(pVM);
    uint64_t uScalableBusHz = CPUMGetGuestScalableBusFrequency(pVM);
    uint64_t uTscHz         = TMCpuTicksPerSecond(pVM);
    uint8_t  uTscRatio      = (uint8_t)((uTscHz + uScalableBusHz / 2) / uScalableBusHz);
    uValue |= (uint32_t)uTscRatio << 8;

    *puValue = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelFlexRatio(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement writing MSR_FLEX_RATIO. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelPkgCStConfigControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.GuestMsrs.msr.PkgCStateCfgCtrl;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelPkgCStConfigControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);

    if (pVCpu->cpum.s.GuestMsrs.msr.PkgCStateCfgCtrl & RT_BIT_64(15))
    {
        Log(("CPUM: WRMSR %#x (%s), %#llx: Write protected -> #GP\n", idMsr, pRange->szName, uValue));
        return VERR_CPUM_RAISE_GP_0;
    }
#if 0 /** @todo check what real (old) hardware does. */
    if ((uValue & 7) >= 5)
    {
        Log(("CPUM: WRMSR %#x (%s), %#llx: Invalid limit (%d) -> #GP\n", idMsr, pRange->szName, uValue, (uint32_t)(uValue & 7)));
        return VERR_CPUM_RAISE_GP_0;
    }
#endif
    pVCpu->cpum.s.GuestMsrs.msr.PkgCStateCfgCtrl = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelPmgIoCaptureBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement I/O mwait wakeup. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelPmgIoCaptureBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement I/O mwait wakeup. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelLastBranchFromToN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement last branch records. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelLastBranchFromToN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement last branch records. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelLastBranchFromN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement last branch records. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelLastBranchFromN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    /** @todo implement last branch records. */
    /** @todo Probing indicates that bit 63 is settable on SandyBridge, at least
     *        if the rest of the bits are zero.  Automatic sign extending?
     *        Investigate! */
    if (!X86_IS_CANONICAL(uValue))
    {
        Log(("CPUM: wrmsr %s(%#x), %#llx -> #GP - not canonical\n", pRange->szName, idMsr, uValue));
        return VERR_CPUM_RAISE_GP_0;
    }
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelLastBranchToN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement last branch records. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelLastBranchToN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement last branch records. */
    /** @todo Probing indicates that bit 63 is settable on SandyBridge, at least
     *        if the rest of the bits are zero.  Automatic sign extending?
     *        Investigate! */
    if (!X86_IS_CANONICAL(uValue))
    {
        Log(("CPUM: wrmsr %s(%#x), %#llx -> #GP - not canonical\n", pRange->szName, idMsr, uValue));
        return VERR_CPUM_RAISE_GP_0;
    }
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelLastBranchTos(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement last branch records. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelLastBranchTos(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement last branch records. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelBblCrCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelBblCrCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelBblCrCtl3(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelBblCrCtl3(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7TemperatureTarget(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7TemperatureTarget(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7MsrOffCoreResponseN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo machine check. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7MsrOffCoreResponseN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo machine check. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7MiscPwrMgmt(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7MiscPwrMgmt(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelP6CrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr);
    int rc = CPUMGetGuestCRx(pVCpu, pRange->uValue, puValue);
    AssertRC(rc);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelP6CrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /* This CRx interface differs from the MOV CRx, GReg interface in that
       #GP(0) isn't raised if unsupported bits are written to.  Instead they
       are simply ignored and masked off. (Pentium M Dothan)  */
    /** @todo Implement MSR_P6_CRx writing.  Too much effort for very little, if
     *        any, gain. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelCpuId1FeatureMaskEcdx(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement CPUID masking.  */
    *puValue = UINT64_MAX;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelCpuId1FeatureMaskEcdx(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement CPUID masking.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelCpuId1FeatureMaskEax(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement CPUID masking.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelCpuId1FeatureMaskEax(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement CPUID masking.  */
    return VINF_SUCCESS;
}



/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelCpuId80000001FeatureMaskEcdx(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement CPUID masking.  */
    *puValue = UINT64_MAX;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelCpuId80000001FeatureMaskEcdx(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement CPUID masking.  */
    return VINF_SUCCESS;
}



/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7SandyAesNiCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement AES-NI.  */
    *puValue = 3;  /* Bit 0 is lock bit, bit 1 disables AES-NI. That's what they say. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7SandyAesNiCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement AES-NI.  */
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7TurboRatioLimit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo implement intel C states.  */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7TurboRatioLimit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement intel C states.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7LbrSelect(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement last-branch-records.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7LbrSelect(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement last-branch-records.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7SandyErrorControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement memory error injection (MSR_ERROR_CONTROL).  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7SandyErrorControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement memory error injection (MSR_ERROR_CONTROL).  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7VirtualLegacyWireCap(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo implement memory VLW?  */
    *puValue = pRange->uValue;
    /* Note: A20M is known to be bit 1 as this was disclosed in spec update
       AAJ49/AAK51/????, which documents the inversion of this bit.  The
       Sandy bridge CPU here has value 0x74, so it probably doesn't have a BIOS
       that correct things.  Some guesses at the other bits:
                 bit 2 = INTR
                 bit 4 = SMI
                 bit 5 = INIT
                 bit 6 = NMI */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7PowerCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7PowerCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel power management  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7SandyPebsNumAlt(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel performance counters.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7SandyPebsNumAlt(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel performance counters.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7PebsLdLat(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel performance counters.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7PebsLdLat(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel performance counters.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7PkgCnResidencyN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7CoreCnResidencyN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7SandyVrCurrentConfig(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Figure out what MSR_VR_CURRENT_CONFIG & MSR_VR_MISC_CONFIG are.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7SandyVrCurrentConfig(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Figure out what MSR_VR_CURRENT_CONFIG & MSR_VR_MISC_CONFIG are.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7SandyVrMiscConfig(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Figure out what MSR_VR_CURRENT_CONFIG & MSR_VR_MISC_CONFIG are.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7SandyVrMiscConfig(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Figure out what MSR_VR_CURRENT_CONFIG & MSR_VR_MISC_CONFIG are.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7SandyRaplPowerUnit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo intel RAPL.  */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7SandyRaplPowerUnit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /* Note! This is documented as read only and except for a Silvermont sample has
             always been classified as read only.  This is just here to make it compile. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7SandyPkgCnIrtlN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7SandyPkgCnIrtlN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel power management.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7SandyPkgC2Residency(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7SandyPkgC2Residency(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /* Note! This is documented as read only and except for a Silvermont sample has
             always been classified as read only.  This is just here to make it compile. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplPkgPowerLimit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel RAPL.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7RaplPkgPowerLimit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel RAPL.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplPkgEnergyStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplPkgPerfStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplPkgPowerInfo(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplDramPowerLimit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel RAPL.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7RaplDramPowerLimit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel RAPL.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplDramEnergyStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplDramPerfStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplDramPowerInfo(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplPp0PowerLimit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel RAPL.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7RaplPp0PowerLimit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel RAPL.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplPp0EnergyStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplPp0Policy(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel RAPL.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7RaplPp0Policy(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel RAPL.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplPp0PerfStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplPp1PowerLimit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel RAPL.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7RaplPp1PowerLimit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel RAPL.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplPp1EnergyStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7RaplPp1Policy(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel RAPL.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7RaplPp1Policy(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel RAPL.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7IvyConfigTdpNominal(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo intel power management.  */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7IvyConfigTdpLevel1(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo intel power management.  */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7IvyConfigTdpLevel2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo intel power management.  */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7IvyConfigTdpControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7IvyConfigTdpControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel power management.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7IvyTurboActivationRatio(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo intel power management.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7IvyTurboActivationRatio(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo intel power management.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7UncPerfGlobalCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo uncore msrs.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7UncPerfGlobalCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo uncore msrs.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7UncPerfGlobalStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo uncore msrs.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7UncPerfGlobalStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo uncore msrs.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7UncPerfGlobalOvfCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo uncore msrs.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7UncPerfGlobalOvfCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo uncore msrs.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7UncPerfFixedCtrCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo uncore msrs.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7UncPerfFixedCtrCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo uncore msrs.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7UncPerfFixedCtr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo uncore msrs.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7UncPerfFixedCtr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo uncore msrs.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7UncCBoxConfig(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo uncore msrs.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7UncArbPerfCtrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo uncore msrs.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7UncArbPerfCtrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo uncore msrs.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7UncArbPerfEvtSelN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo uncore msrs.  */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelI7UncArbPerfEvtSelN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo uncore msrs.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelI7SmiCount(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);

    /*
     * 31:0 is SMI count (read only), 63:32 reserved.
     * Since we don't do SMI, the count is always zero.
     */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelCore2EmttmCrTablesN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo implement enhanced multi thread termal monitoring? */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelCore2EmttmCrTablesN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement enhanced multi thread termal monitoring? */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelCore2SmmCStMiscInfo(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo SMM & C-states? */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelCore2SmmCStMiscInfo(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo SMM & C-states? */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelCore1ExtConfig(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Core1&2 EXT_CONFIG (whatever that is)? */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelCore1ExtConfig(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Core1&2 EXT_CONFIG (whatever that is)? */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelCore1DtsCalControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Core1&2(?) DTS_CAL_CTRL (whatever that is)? */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelCore1DtsCalControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Core1&2(?) DTS_CAL_CTRL (whatever that is)? */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelCore2PeciControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Core2+ platform environment control interface control register? */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_IntelCore2PeciControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Core2+ platform environment control interface control register? */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_IntelAtSilvCoreC1Recidency(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = 0;
    return VINF_SUCCESS;
}


/*
 * Multiple vendor P6 MSRs.
 * Multiple vendor P6 MSRs.
 * Multiple vendor P6 MSRs.
 *
 * These MSRs were introduced with the P6 but not elevated to architectural
 * MSRs, despite other vendors implementing them.
 */


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_P6LastBranchFromIp(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /* AMD seems to just record RIP, while intel claims to record RIP+CS.BASE
       if I read the docs correctly, thus the need for separate functions. */
    /** @todo implement last branch records. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_P6LastBranchToIp(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement last branch records. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_P6LastIntFromIp(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement last exception records. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_P6LastIntFromIp(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement last exception records. */
    /* Note! On many CPUs, the high bit of the 0x000001dd register is always writable, even when the result is
             a non-cannonical address. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_P6LastIntToIp(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo implement last exception records. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_P6LastIntToIp(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo implement last exception records. */
    return VINF_SUCCESS;
}



/*
 * AMD specific
 * AMD specific
 * AMD specific
 */


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hTscRate(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Implement TscRateMsr */
    *puValue = RT_MAKE_U64(0, 1); /* 1.0 = reset value. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hTscRate(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Implement TscRateMsr */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hLwpCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Implement AMD LWP? (Instructions: LWPINS, LWPVAL, LLWPCB, SLWPCB) */
    /* Note: Only listes in BKDG for Family 15H. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hLwpCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Implement AMD LWP? (Instructions: LWPINS, LWPVAL, LLWPCB, SLWPCB) */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hLwpCbAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Implement AMD LWP? (Instructions: LWPINS, LWPVAL, LLWPCB, SLWPCB) */
    /* Note: Only listes in BKDG for Family 15H. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hLwpCbAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Implement AMD LWP? (Instructions: LWPINS, LWPVAL, LLWPCB, SLWPCB) */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hMc4MiscN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo machine check. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hMc4MiscN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo machine check. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8PerfCtlN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD performance events. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8PerfCtlN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD performance events. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8PerfCtrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD performance events. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8PerfCtrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD performance events. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8SysCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo AMD SYS_CFG */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8SysCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD SYS_CFG */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8HwCr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD HW_CFG */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8HwCr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD HW_CFG */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8IorrBaseN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IorrMask/IorrBase */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8IorrBaseN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IorrMask/IorrBase */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8IorrMaskN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IorrMask/IorrBase */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8IorrMaskN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IorrMask/IorrBase */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8TopOfMemN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = 0;
    /** @todo return 4GB - RamHoleSize here for TOPMEM. Figure out what to return
     *        for TOPMEM2. */
    //if (pRange->uValue == 0)
    //    *puValue = _4G - RamHoleSize;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8TopOfMemN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD TOPMEM and TOPMEM2/TOM2. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8NbCfg1(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD NB_CFG1 */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8NbCfg1(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD NB_CFG1 */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8McXcptRedir(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo machine check. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8McXcptRedir(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo machine check. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8CpuNameN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr);
    PCPUMCPUIDLEAF pLeaf = cpumCpuIdGetLeaf(pVCpu->CTX_SUFF(pVM), pRange->uValue / 2 + 0x80000001);
    if (pLeaf)
    {
        if (!(pRange->uValue & 1))
            *puValue = RT_MAKE_U64(pLeaf->uEax, pLeaf->uEbx);
        else
            *puValue = RT_MAKE_U64(pLeaf->uEcx, pLeaf->uEdx);
    }
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8CpuNameN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Remember guest programmed CPU name. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8HwThermalCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo AMD HTC. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8HwThermalCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD HTC. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8SwThermalCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD STC. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8SwThermalCtrl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD STC. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8FidVidControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo AMD FIDVID_CTL. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8FidVidControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD FIDVID_CTL. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8FidVidStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo AMD FIDVID_STATUS. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8McCtlMaskN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD MC. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8McCtlMaskN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD MC. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8SmiOnIoTrapN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD SMM/SMI and I/O trap. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8SmiOnIoTrapN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD SMM/SMI and I/O trap. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8SmiOnIoTrapCtlSts(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD SMM/SMI and I/O trap. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8SmiOnIoTrapCtlSts(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD SMM/SMI and I/O trap. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8IntPendingMessage(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Interrupt pending message. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8IntPendingMessage(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Interrupt pending message. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8SmiTriggerIoCycle(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD SMM/SMI and trigger I/O cycle. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8SmiTriggerIoCycle(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD SMM/SMI and trigger I/O cycle. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hMmioCfgBaseAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD MMIO Configuration base address. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hMmioCfgBaseAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD MMIO Configuration base address. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hTrapCtlMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD 0xc0010059. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hTrapCtlMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD 0xc0010059. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hPStateCurLimit(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo AMD P-states. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hPStateControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo AMD P-states. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hPStateControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD P-states. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hPStateStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo AMD P-states. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hPStateStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD P-states. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hPStateN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo AMD P-states. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hPStateN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD P-states. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hCofVidControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo AMD P-states. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hCofVidControl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD P-states. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hCofVidStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo AMD P-states. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hCofVidStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /* Note! Writing 0 seems to not GP, not sure if it does anything to the value... */
    /** @todo AMD P-states. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hCStateIoBaseAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD C-states. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hCStateIoBaseAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD C-states. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hCpuWatchdogTimer(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD machine checks. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hCpuWatchdogTimer(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD machine checks. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8SmmBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD SMM. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8SmmBase(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD SMM. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8SmmAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD SMM. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8SmmAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD SMM. */
    return VINF_SUCCESS;
}



/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8SmmMask(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD SMM. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8SmmMask(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD SMM. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8VmCr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    PVM pVM = pVCpu->CTX_SUFF(pVM);
    if (pVM->cpum.s.GuestFeatures.fSvm)
        *puValue = MSR_K8_VM_CR_LOCK;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8VmCr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    PVM pVM = pVCpu->CTX_SUFF(pVM);
    if (pVM->cpum.s.GuestFeatures.fSvm)
    {
        /* Silently ignore writes to LOCK and SVM_DISABLE bit when the LOCK bit is set (see cpumMsrRd_AmdK8VmCr). */
        if (uValue & (MSR_K8_VM_CR_DPD | MSR_K8_VM_CR_R_INIT | MSR_K8_VM_CR_DIS_A20M))
            return VERR_CPUM_RAISE_GP_0;
        return VINF_SUCCESS;
    }
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8IgnNe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IGNNE\# control. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8IgnNe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IGNNE\# control. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8SmmCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD SMM. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8SmmCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD SMM. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8VmHSavePa(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    *puValue = pVCpu->cpum.s.Guest.hwvirt.svm.uMsrHSavePa;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8VmHSavePa(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    if (uValue & UINT64_C(0xfff))
    {
        Log(("CPUM: Invalid setting of low 12 bits set writing host-state save area MSR %#x: %#llx\n", idMsr, uValue));
        return VERR_CPUM_RAISE_GP_0;
    }

    uint64_t fInvPhysMask = ~(RT_BIT_64(pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.cMaxPhysAddrWidth) - 1U);
    if (fInvPhysMask & uValue)
    {
        Log(("CPUM: Invalid physical address bits set writing host-state save area MSR %#x: %#llx (%#llx)\n",
             idMsr, uValue, uValue & fInvPhysMask));
        return VERR_CPUM_RAISE_GP_0;
    }

    pVCpu->cpum.s.Guest.hwvirt.svm.uMsrHSavePa = uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hVmLockKey(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD SVM. */
    *puValue = 0; /* RAZ */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hVmLockKey(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD SVM. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hSmmLockKey(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD SMM. */
    *puValue = 0; /* RAZ */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hSmmLockKey(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD SMM. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hLocalSmiStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD SMM/SMI. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hLocalSmiStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD SMM/SMI. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hOsVisWrkIdLength(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo AMD OS visible workaround. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hOsVisWrkIdLength(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD OS visible workaround. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hOsVisWrkStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD OS visible workaround. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hOsVisWrkStatus(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD OS visible workaround. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam16hL2IPerfCtlN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD L2I performance counters. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam16hL2IPerfCtlN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD L2I performance counters. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam16hL2IPerfCtrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD L2I performance counters. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam16hL2IPerfCtrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD L2I performance counters. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hNorthbridgePerfCtlN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD Northbridge performance counters. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hNorthbridgePerfCtlN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD Northbridge performance counters. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hNorthbridgePerfCtrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD Northbridge performance counters. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hNorthbridgePerfCtrN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD Northbridge performance counters. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7MicrocodeCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus. Need to be explored and verify K7 presence. */
    /** @todo Undocumented register only seen mentioned in fam15h erratum \#608. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7MicrocodeCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo Undocumented register only seen mentioned in fam15h erratum \#608. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7ClusterIdMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus. Need to be explored and verify K7 presence. */
    /** @todo Undocumented register only seen mentioned in fam16h BKDG r3.00 when
     *        describing EBL_CR_POWERON. */
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7ClusterIdMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo Undocumented register only seen mentioned in fam16h BKDG r3.00 when
     *        describing EBL_CR_POWERON. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8CpuIdCtlStd07hEbax(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    bool           fIgnored;
    PCPUMCPUIDLEAF pLeaf = cpumCpuIdGetLeafEx(pVCpu->CTX_SUFF(pVM), 0x00000007, 0, &fIgnored);
    if (pLeaf)
        *puValue = RT_MAKE_U64(pLeaf->uEbx, pLeaf->uEax);
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8CpuIdCtlStd07hEbax(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Changing CPUID leaf 7/0. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8CpuIdCtlStd06hEcx(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    PCPUMCPUIDLEAF pLeaf = cpumCpuIdGetLeaf(pVCpu->CTX_SUFF(pVM), 0x00000006);
    if (pLeaf)
        *puValue = pLeaf->uEcx;
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8CpuIdCtlStd06hEcx(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Changing CPUID leaf 6. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8CpuIdCtlStd01hEdcx(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    PCPUMCPUIDLEAF pLeaf = cpumCpuIdGetLeaf(pVCpu->CTX_SUFF(pVM), 0x00000001);
    if (pLeaf)
        *puValue = RT_MAKE_U64(pLeaf->uEdx, pLeaf->uEcx);
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8CpuIdCtlStd01hEdcx(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Changing CPUID leaf 0x80000001. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8CpuIdCtlExt01hEdcx(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    PCPUMCPUIDLEAF pLeaf = cpumCpuIdGetLeaf(pVCpu->CTX_SUFF(pVM), 0x80000001);
    if (pLeaf)
        *puValue = RT_MAKE_U64(pLeaf->uEdx, pLeaf->uEcx);
    else
        *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8CpuIdCtlExt01hEdcx(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Changing CPUID leaf 0x80000001. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK8PatchLevel(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr);
    /** @todo Fake AMD microcode patching.  */
    PVM const pVM = pVCpu->CTX_SUFF(pVM);
    if (pVM->cpum.s.GuestInfo.uMicrocodeRevision != UINT32_MAX)
        *puValue = RT_MAKE_U64(pVM->cpum.s.GuestInfo.uMicrocodeRevision, RT_HI_U32(pRange->uValue));
    else
        *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK8PatchLoader(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Fake AMD microcode patching.  */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7DebugStatusMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7DebugStatusMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7BHTraceBaseMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7BHTraceBaseMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7BHTracePtrMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7BHTracePtrMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7BHTraceLimitMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7BHTraceLimitMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7HardwareDebugToolCfgMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7HardwareDebugToolCfgMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7FastFlushCountMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7FastFlushCountMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo undocumented */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7NodeId(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD node ID and bios scratch. */
    *puValue = 0; /* nodeid = 0; nodes-per-cpu = 1 */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7NodeId(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD node ID and bios scratch. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7DrXAddrMaskN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD DRx address masking (range breakpoints). */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7DrXAddrMaskN(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD DRx address masking (range breakpoints). */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7Dr0DataMatchMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD undocument debugging features. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7Dr0DataMatchMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD undocument debugging features. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7Dr0DataMaskMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD undocument debugging features. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7Dr0DataMaskMaybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD undocument debugging features. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7LoadStoreCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD load-store config. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7LoadStoreCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD load-store config. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7InstrCacheCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD instruction cache config. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7InstrCacheCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD instruction cache config. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7DataCacheCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD data cache config. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7DataCacheCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD data cache config. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7BusUnitCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD bus unit config. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7BusUnitCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo AMD bus unit config. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdK7DebugCtl2Maybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo Undocument AMD debug control register \#2. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdK7DebugCtl2Maybe(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo Allegedly requiring edi=0x9c5a203a when execuing rdmsr/wrmsr on older
     *  cpus.  Need to be explored and verify K7 presence.  */
    /** @todo Undocument AMD debug control register \#2. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hFpuCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD FPU config. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hFpuCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD FPU config. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hDecoderCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD decoder config. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hDecoderCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD decoder config. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hBusUnitCfg2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /* Note! 10h and 16h */
    /** @todo AMD bus unit  config. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hBusUnitCfg2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /* Note! 10h and 16h */
    /** @todo AMD bus unit config. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hCombUnitCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD unit config. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hCombUnitCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD unit config. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hCombUnitCfg2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD unit config 2. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hCombUnitCfg2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD unit config 2. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hCombUnitCfg3(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD combined unit config 3. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hCombUnitCfg3(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD combined unit config 3. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hExecUnitCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD execution unit config. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hExecUnitCfg(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD execution unit config. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam15hLoadStoreCfg2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD load-store config 2. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam15hLoadStoreCfg2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD load-store config 2. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hIbsFetchCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hIbsFetchCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hIbsFetchLinAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hIbsFetchLinAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hIbsFetchPhysAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hIbsFetchPhysAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hIbsOpExecCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hIbsOpExecCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hIbsOpRip(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hIbsOpRip(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    if (!X86_IS_CANONICAL(uValue))
    {
        Log(("CPUM: wrmsr %s(%#x), %#llx -> #GP - not canonical\n", pRange->szName, idMsr, uValue));
        return VERR_CPUM_RAISE_GP_0;
    }
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hIbsOpData(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hIbsOpData(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hIbsOpData2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hIbsOpData2(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hIbsOpData3(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hIbsOpData3(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hIbsDcLinAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hIbsDcLinAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    if (!X86_IS_CANONICAL(uValue))
    {
        Log(("CPUM: wrmsr %s(%#x), %#llx -> #GP - not canonical\n", pRange->szName, idMsr, uValue));
        return VERR_CPUM_RAISE_GP_0;
    }
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hIbsDcPhysAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hIbsDcPhysAddr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam10hIbsCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam10hIbsCtl(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_AmdFam14hIbsBrTarget(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange);
    /** @todo AMD IBS. */
    *puValue = 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_AmdFam14hIbsBrTarget(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idMsr); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    /** @todo AMD IBS. */
    if (!X86_IS_CANONICAL(uValue))
    {
        Log(("CPUM: wrmsr %s(%#x), %#llx -> #GP - not canonical\n", pRange->szName, idMsr, uValue));
        return VERR_CPUM_RAISE_GP_0;
    }
    return VINF_SUCCESS;
}



/*
 * GIM MSRs.
 * GIM MSRs.
 * GIM MSRs.
 */


/** @callback_method_impl{FNCPUMRDMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrRd_Gim(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
#if defined(VBOX_WITH_NESTED_HWVIRT_SVM) || defined(VBOX_WITH_NESTED_HWVIRT_VMX)
    /* Raise #GP(0) like a physical CPU would since the nested-hypervisor hasn't intercept these MSRs. */
    if (   CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.s.Guest)
        || CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.s.Guest))
        return VERR_CPUM_RAISE_GP_0;
#endif
    return GIMReadMsr(pVCpu, idMsr, pRange, puValue);
}


/** @callback_method_impl{FNCPUMWRMSR} */
static DECLCALLBACK(VBOXSTRICTRC) cpumMsrWr_Gim(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
#if defined(VBOX_WITH_NESTED_HWVIRT_SVM) || defined(VBOX_WITH_NESTED_HWVIRT_VMX)
    /* Raise #GP(0) like a physical CPU would since the nested-hypervisor hasn't intercept these MSRs. */
    if (   CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.s.Guest)
        || CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.s.Guest))
        return VERR_CPUM_RAISE_GP_0;
#endif
    return GIMWriteMsr(pVCpu, idMsr, pRange, uValue, uRawValue);
}


/**
 * MSR read function table.
 */
static const struct READMSRCLANG11WEIRDNOTHROW { PFNCPUMRDMSR pfnRdMsr; } g_aCpumRdMsrFns[kCpumMsrRdFn_End] =
{
    { NULL }, /* Invalid */
    { cpumMsrRd_FixedValue },
    { NULL }, /* Alias */
    { cpumMsrRd_WriteOnly },
    { cpumMsrRd_Ia32P5McAddr },
    { cpumMsrRd_Ia32P5McType },
    { cpumMsrRd_Ia32TimestampCounter },
    { cpumMsrRd_Ia32PlatformId },
    { cpumMsrRd_Ia32ApicBase },
    { cpumMsrRd_Ia32FeatureControl },
    { cpumMsrRd_Ia32BiosSignId },
    { cpumMsrRd_Ia32SmmMonitorCtl },
    { cpumMsrRd_Ia32PmcN },
    { cpumMsrRd_Ia32MonitorFilterLineSize },
    { cpumMsrRd_Ia32MPerf },
    { cpumMsrRd_Ia32APerf },
    { cpumMsrRd_Ia32MtrrCap },
    { cpumMsrRd_Ia32MtrrPhysBaseN },
    { cpumMsrRd_Ia32MtrrPhysMaskN },
    { cpumMsrRd_Ia32MtrrFixed },
    { cpumMsrRd_Ia32MtrrDefType },
    { cpumMsrRd_Ia32Pat },
    { cpumMsrRd_Ia32SysEnterCs },
    { cpumMsrRd_Ia32SysEnterEsp },
    { cpumMsrRd_Ia32SysEnterEip },
    { cpumMsrRd_Ia32McgCap },
    { cpumMsrRd_Ia32McgStatus },
    { cpumMsrRd_Ia32McgCtl },
    { cpumMsrRd_Ia32DebugCtl },
    { cpumMsrRd_Ia32SmrrPhysBase },
    { cpumMsrRd_Ia32SmrrPhysMask },
    { cpumMsrRd_Ia32PlatformDcaCap },
    { cpumMsrRd_Ia32CpuDcaCap },
    { cpumMsrRd_Ia32Dca0Cap },
    { cpumMsrRd_Ia32PerfEvtSelN },
    { cpumMsrRd_Ia32PerfStatus },
    { cpumMsrRd_Ia32PerfCtl },
    { cpumMsrRd_Ia32FixedCtrN },
    { cpumMsrRd_Ia32PerfCapabilities },
    { cpumMsrRd_Ia32FixedCtrCtrl },
    { cpumMsrRd_Ia32PerfGlobalStatus },
    { cpumMsrRd_Ia32PerfGlobalCtrl },
    { cpumMsrRd_Ia32PerfGlobalOvfCtrl },
    { cpumMsrRd_Ia32PebsEnable },
    { cpumMsrRd_Ia32ClockModulation },
    { cpumMsrRd_Ia32ThermInterrupt },
    { cpumMsrRd_Ia32ThermStatus },
    { cpumMsrRd_Ia32Therm2Ctl },
    { cpumMsrRd_Ia32MiscEnable },
    { cpumMsrRd_Ia32McCtlStatusAddrMiscN },
    { cpumMsrRd_Ia32McNCtl2 },
    { cpumMsrRd_Ia32DsArea },
    { cpumMsrRd_Ia32TscDeadline },
    { cpumMsrRd_Ia32X2ApicN },
    { cpumMsrRd_Ia32DebugInterface },
    { cpumMsrRd_Ia32VmxBasic },
    { cpumMsrRd_Ia32VmxPinbasedCtls },
    { cpumMsrRd_Ia32VmxProcbasedCtls },
    { cpumMsrRd_Ia32VmxExitCtls },
    { cpumMsrRd_Ia32VmxEntryCtls },
    { cpumMsrRd_Ia32VmxMisc },
    { cpumMsrRd_Ia32VmxCr0Fixed0 },
    { cpumMsrRd_Ia32VmxCr0Fixed1 },
    { cpumMsrRd_Ia32VmxCr4Fixed0 },
    { cpumMsrRd_Ia32VmxCr4Fixed1 },
    { cpumMsrRd_Ia32VmxVmcsEnum },
    { cpumMsrRd_Ia32VmxProcBasedCtls2 },
    { cpumMsrRd_Ia32VmxEptVpidCap },
    { cpumMsrRd_Ia32VmxTruePinbasedCtls },
    { cpumMsrRd_Ia32VmxTrueProcbasedCtls },
    { cpumMsrRd_Ia32VmxTrueExitCtls },
    { cpumMsrRd_Ia32VmxTrueEntryCtls },
    { cpumMsrRd_Ia32VmxVmFunc },
    { cpumMsrRd_Ia32SpecCtrl },
    { cpumMsrRd_Ia32ArchCapabilities },

    { cpumMsrRd_Amd64Efer },
    { cpumMsrRd_Amd64SyscallTarget },
    { cpumMsrRd_Amd64LongSyscallTarget },
    { cpumMsrRd_Amd64CompSyscallTarget },
    { cpumMsrRd_Amd64SyscallFlagMask },
    { cpumMsrRd_Amd64FsBase },
    { cpumMsrRd_Amd64GsBase },
    { cpumMsrRd_Amd64KernelGsBase },
    { cpumMsrRd_Amd64TscAux },

    { cpumMsrRd_IntelEblCrPowerOn },
    { cpumMsrRd_IntelI7CoreThreadCount },
    { cpumMsrRd_IntelP4EbcHardPowerOn },
    { cpumMsrRd_IntelP4EbcSoftPowerOn },
    { cpumMsrRd_IntelP4EbcFrequencyId },
    { cpumMsrRd_IntelP6FsbFrequency },
    { cpumMsrRd_IntelPlatformInfo },
    { cpumMsrRd_IntelFlexRatio },
    { cpumMsrRd_IntelPkgCStConfigControl },
    { cpumMsrRd_IntelPmgIoCaptureBase },
    { cpumMsrRd_IntelLastBranchFromToN },
    { cpumMsrRd_IntelLastBranchFromN },
    { cpumMsrRd_IntelLastBranchToN },
    { cpumMsrRd_IntelLastBranchTos },
    { cpumMsrRd_IntelBblCrCtl },
    { cpumMsrRd_IntelBblCrCtl3 },
    { cpumMsrRd_IntelI7TemperatureTarget },
    { cpumMsrRd_IntelI7MsrOffCoreResponseN },
    { cpumMsrRd_IntelI7MiscPwrMgmt },
    { cpumMsrRd_IntelP6CrN },
    { cpumMsrRd_IntelCpuId1FeatureMaskEcdx },
    { cpumMsrRd_IntelCpuId1FeatureMaskEax },
    { cpumMsrRd_IntelCpuId80000001FeatureMaskEcdx },
    { cpumMsrRd_IntelI7SandyAesNiCtl },
    { cpumMsrRd_IntelI7TurboRatioLimit },
    { cpumMsrRd_IntelI7LbrSelect },
    { cpumMsrRd_IntelI7SandyErrorControl },
    { cpumMsrRd_IntelI7VirtualLegacyWireCap },
    { cpumMsrRd_IntelI7PowerCtl },
    { cpumMsrRd_IntelI7SandyPebsNumAlt },
    { cpumMsrRd_IntelI7PebsLdLat },
    { cpumMsrRd_IntelI7PkgCnResidencyN },
    { cpumMsrRd_IntelI7CoreCnResidencyN },
    { cpumMsrRd_IntelI7SandyVrCurrentConfig },
    { cpumMsrRd_IntelI7SandyVrMiscConfig },
    { cpumMsrRd_IntelI7SandyRaplPowerUnit },
    { cpumMsrRd_IntelI7SandyPkgCnIrtlN },
    { cpumMsrRd_IntelI7SandyPkgC2Residency },
    { cpumMsrRd_IntelI7RaplPkgPowerLimit },
    { cpumMsrRd_IntelI7RaplPkgEnergyStatus },
    { cpumMsrRd_IntelI7RaplPkgPerfStatus },
    { cpumMsrRd_IntelI7RaplPkgPowerInfo },
    { cpumMsrRd_IntelI7RaplDramPowerLimit },
    { cpumMsrRd_IntelI7RaplDramEnergyStatus },
    { cpumMsrRd_IntelI7RaplDramPerfStatus },
    { cpumMsrRd_IntelI7RaplDramPowerInfo },
    { cpumMsrRd_IntelI7RaplPp0PowerLimit },
    { cpumMsrRd_IntelI7RaplPp0EnergyStatus },
    { cpumMsrRd_IntelI7RaplPp0Policy },
    { cpumMsrRd_IntelI7RaplPp0PerfStatus },
    { cpumMsrRd_IntelI7RaplPp1PowerLimit },
    { cpumMsrRd_IntelI7RaplPp1EnergyStatus },
    { cpumMsrRd_IntelI7RaplPp1Policy },
    { cpumMsrRd_IntelI7IvyConfigTdpNominal },
    { cpumMsrRd_IntelI7IvyConfigTdpLevel1 },
    { cpumMsrRd_IntelI7IvyConfigTdpLevel2 },
    { cpumMsrRd_IntelI7IvyConfigTdpControl },
    { cpumMsrRd_IntelI7IvyTurboActivationRatio },
    { cpumMsrRd_IntelI7UncPerfGlobalCtrl },
    { cpumMsrRd_IntelI7UncPerfGlobalStatus },
    { cpumMsrRd_IntelI7UncPerfGlobalOvfCtrl },
    { cpumMsrRd_IntelI7UncPerfFixedCtrCtrl },
    { cpumMsrRd_IntelI7UncPerfFixedCtr },
    { cpumMsrRd_IntelI7UncCBoxConfig },
    { cpumMsrRd_IntelI7UncArbPerfCtrN },
    { cpumMsrRd_IntelI7UncArbPerfEvtSelN },
    { cpumMsrRd_IntelI7SmiCount },
    { cpumMsrRd_IntelCore2EmttmCrTablesN },
    { cpumMsrRd_IntelCore2SmmCStMiscInfo },
    { cpumMsrRd_IntelCore1ExtConfig },
    { cpumMsrRd_IntelCore1DtsCalControl },
    { cpumMsrRd_IntelCore2PeciControl },
    { cpumMsrRd_IntelAtSilvCoreC1Recidency },

    { cpumMsrRd_P6LastBranchFromIp },
    { cpumMsrRd_P6LastBranchToIp },
    { cpumMsrRd_P6LastIntFromIp },
    { cpumMsrRd_P6LastIntToIp },

    { cpumMsrRd_AmdFam15hTscRate },
    { cpumMsrRd_AmdFam15hLwpCfg },
    { cpumMsrRd_AmdFam15hLwpCbAddr },
    { cpumMsrRd_AmdFam10hMc4MiscN },
    { cpumMsrRd_AmdK8PerfCtlN },
    { cpumMsrRd_AmdK8PerfCtrN },
    { cpumMsrRd_AmdK8SysCfg },
    { cpumMsrRd_AmdK8HwCr },
    { cpumMsrRd_AmdK8IorrBaseN },
    { cpumMsrRd_AmdK8IorrMaskN },
    { cpumMsrRd_AmdK8TopOfMemN },
    { cpumMsrRd_AmdK8NbCfg1 },
    { cpumMsrRd_AmdK8McXcptRedir },
    { cpumMsrRd_AmdK8CpuNameN },
    { cpumMsrRd_AmdK8HwThermalCtrl },
    { cpumMsrRd_AmdK8SwThermalCtrl },
    { cpumMsrRd_AmdK8FidVidControl },
    { cpumMsrRd_AmdK8FidVidStatus },
    { cpumMsrRd_AmdK8McCtlMaskN },
    { cpumMsrRd_AmdK8SmiOnIoTrapN },
    { cpumMsrRd_AmdK8SmiOnIoTrapCtlSts },
    { cpumMsrRd_AmdK8IntPendingMessage },
    { cpumMsrRd_AmdK8SmiTriggerIoCycle },
    { cpumMsrRd_AmdFam10hMmioCfgBaseAddr },
    { cpumMsrRd_AmdFam10hTrapCtlMaybe },
    { cpumMsrRd_AmdFam10hPStateCurLimit },
    { cpumMsrRd_AmdFam10hPStateControl },
    { cpumMsrRd_AmdFam10hPStateStatus },
    { cpumMsrRd_AmdFam10hPStateN },
    { cpumMsrRd_AmdFam10hCofVidControl },
    { cpumMsrRd_AmdFam10hCofVidStatus },
    { cpumMsrRd_AmdFam10hCStateIoBaseAddr },
    { cpumMsrRd_AmdFam10hCpuWatchdogTimer },
    { cpumMsrRd_AmdK8SmmBase },
    { cpumMsrRd_AmdK8SmmAddr },
    { cpumMsrRd_AmdK8SmmMask },
    { cpumMsrRd_AmdK8VmCr },
    { cpumMsrRd_AmdK8IgnNe },
    { cpumMsrRd_AmdK8SmmCtl },
    { cpumMsrRd_AmdK8VmHSavePa },
    { cpumMsrRd_AmdFam10hVmLockKey },
    { cpumMsrRd_AmdFam10hSmmLockKey },
    { cpumMsrRd_AmdFam10hLocalSmiStatus },
    { cpumMsrRd_AmdFam10hOsVisWrkIdLength },
    { cpumMsrRd_AmdFam10hOsVisWrkStatus },
    { cpumMsrRd_AmdFam16hL2IPerfCtlN },
    { cpumMsrRd_AmdFam16hL2IPerfCtrN },
    { cpumMsrRd_AmdFam15hNorthbridgePerfCtlN },
    { cpumMsrRd_AmdFam15hNorthbridgePerfCtrN },
    { cpumMsrRd_AmdK7MicrocodeCtl },
    { cpumMsrRd_AmdK7ClusterIdMaybe },
    { cpumMsrRd_AmdK8CpuIdCtlStd07hEbax },
    { cpumMsrRd_AmdK8CpuIdCtlStd06hEcx },
    { cpumMsrRd_AmdK8CpuIdCtlStd01hEdcx },
    { cpumMsrRd_AmdK8CpuIdCtlExt01hEdcx },
    { cpumMsrRd_AmdK8PatchLevel },
    { cpumMsrRd_AmdK7DebugStatusMaybe },
    { cpumMsrRd_AmdK7BHTraceBaseMaybe },
    { cpumMsrRd_AmdK7BHTracePtrMaybe },
    { cpumMsrRd_AmdK7BHTraceLimitMaybe },
    { cpumMsrRd_AmdK7HardwareDebugToolCfgMaybe },
    { cpumMsrRd_AmdK7FastFlushCountMaybe },
    { cpumMsrRd_AmdK7NodeId },
    { cpumMsrRd_AmdK7DrXAddrMaskN },
    { cpumMsrRd_AmdK7Dr0DataMatchMaybe },
    { cpumMsrRd_AmdK7Dr0DataMaskMaybe },
    { cpumMsrRd_AmdK7LoadStoreCfg },
    { cpumMsrRd_AmdK7InstrCacheCfg },
    { cpumMsrRd_AmdK7DataCacheCfg },
    { cpumMsrRd_AmdK7BusUnitCfg },
    { cpumMsrRd_AmdK7DebugCtl2Maybe },
    { cpumMsrRd_AmdFam15hFpuCfg },
    { cpumMsrRd_AmdFam15hDecoderCfg },
    { cpumMsrRd_AmdFam10hBusUnitCfg2 },
    { cpumMsrRd_AmdFam15hCombUnitCfg },
    { cpumMsrRd_AmdFam15hCombUnitCfg2 },
    { cpumMsrRd_AmdFam15hCombUnitCfg3 },
    { cpumMsrRd_AmdFam15hExecUnitCfg },
    { cpumMsrRd_AmdFam15hLoadStoreCfg2 },
    { cpumMsrRd_AmdFam10hIbsFetchCtl },
    { cpumMsrRd_AmdFam10hIbsFetchLinAddr },
    { cpumMsrRd_AmdFam10hIbsFetchPhysAddr },
    { cpumMsrRd_AmdFam10hIbsOpExecCtl },
    { cpumMsrRd_AmdFam10hIbsOpRip },
    { cpumMsrRd_AmdFam10hIbsOpData },
    { cpumMsrRd_AmdFam10hIbsOpData2 },
    { cpumMsrRd_AmdFam10hIbsOpData3 },
    { cpumMsrRd_AmdFam10hIbsDcLinAddr },
    { cpumMsrRd_AmdFam10hIbsDcPhysAddr },
    { cpumMsrRd_AmdFam10hIbsCtl },
    { cpumMsrRd_AmdFam14hIbsBrTarget },

    { cpumMsrRd_Gim },
};


/**
 * MSR write function table.
 */
static const struct WRITEMSRCLANG11WEIRDNOTHROW { PFNCPUMWRMSR pfnWrMsr; } g_aCpumWrMsrFns[kCpumMsrWrFn_End] =
{
    { NULL }, /* Invalid */
    { cpumMsrWr_IgnoreWrite },
    { cpumMsrWr_ReadOnly },
    { NULL }, /* Alias */
    { cpumMsrWr_Ia32P5McAddr },
    { cpumMsrWr_Ia32P5McType },
    { cpumMsrWr_Ia32TimestampCounter },
    { cpumMsrWr_Ia32ApicBase },
    { cpumMsrWr_Ia32FeatureControl },
    { cpumMsrWr_Ia32BiosSignId },
    { cpumMsrWr_Ia32BiosUpdateTrigger },
    { cpumMsrWr_Ia32SmmMonitorCtl },
    { cpumMsrWr_Ia32PmcN },
    { cpumMsrWr_Ia32MonitorFilterLineSize },
    { cpumMsrWr_Ia32MPerf },
    { cpumMsrWr_Ia32APerf },
    { cpumMsrWr_Ia32MtrrPhysBaseN },
    { cpumMsrWr_Ia32MtrrPhysMaskN },
    { cpumMsrWr_Ia32MtrrFixed },
    { cpumMsrWr_Ia32MtrrDefType },
    { cpumMsrWr_Ia32Pat },
    { cpumMsrWr_Ia32SysEnterCs },
    { cpumMsrWr_Ia32SysEnterEsp },
    { cpumMsrWr_Ia32SysEnterEip },
    { cpumMsrWr_Ia32McgStatus },
    { cpumMsrWr_Ia32McgCtl },
    { cpumMsrWr_Ia32DebugCtl },
    { cpumMsrWr_Ia32SmrrPhysBase },
    { cpumMsrWr_Ia32SmrrPhysMask },
    { cpumMsrWr_Ia32PlatformDcaCap },
    { cpumMsrWr_Ia32Dca0Cap },
    { cpumMsrWr_Ia32PerfEvtSelN },
    { cpumMsrWr_Ia32PerfStatus },
    { cpumMsrWr_Ia32PerfCtl },
    { cpumMsrWr_Ia32FixedCtrN },
    { cpumMsrWr_Ia32PerfCapabilities },
    { cpumMsrWr_Ia32FixedCtrCtrl },
    { cpumMsrWr_Ia32PerfGlobalStatus },
    { cpumMsrWr_Ia32PerfGlobalCtrl },
    { cpumMsrWr_Ia32PerfGlobalOvfCtrl },
    { cpumMsrWr_Ia32PebsEnable },
    { cpumMsrWr_Ia32ClockModulation },
    { cpumMsrWr_Ia32ThermInterrupt },
    { cpumMsrWr_Ia32ThermStatus },
    { cpumMsrWr_Ia32Therm2Ctl },
    { cpumMsrWr_Ia32MiscEnable },
    { cpumMsrWr_Ia32McCtlStatusAddrMiscN },
    { cpumMsrWr_Ia32McNCtl2 },
    { cpumMsrWr_Ia32DsArea },
    { cpumMsrWr_Ia32TscDeadline },
    { cpumMsrWr_Ia32X2ApicN },
    { cpumMsrWr_Ia32DebugInterface },
    { cpumMsrWr_Ia32SpecCtrl },
    { cpumMsrWr_Ia32PredCmd },
    { cpumMsrWr_Ia32FlushCmd },

    { cpumMsrWr_Amd64Efer },
    { cpumMsrWr_Amd64SyscallTarget },
    { cpumMsrWr_Amd64LongSyscallTarget },
    { cpumMsrWr_Amd64CompSyscallTarget },
    { cpumMsrWr_Amd64SyscallFlagMask },
    { cpumMsrWr_Amd64FsBase },
    { cpumMsrWr_Amd64GsBase },
    { cpumMsrWr_Amd64KernelGsBase },
    { cpumMsrWr_Amd64TscAux },

    { cpumMsrWr_IntelEblCrPowerOn },
    { cpumMsrWr_IntelP4EbcHardPowerOn },
    { cpumMsrWr_IntelP4EbcSoftPowerOn },
    { cpumMsrWr_IntelP4EbcFrequencyId },
    { cpumMsrWr_IntelFlexRatio },
    { cpumMsrWr_IntelPkgCStConfigControl },
    { cpumMsrWr_IntelPmgIoCaptureBase },
    { cpumMsrWr_IntelLastBranchFromToN },
    { cpumMsrWr_IntelLastBranchFromN },
    { cpumMsrWr_IntelLastBranchToN },
    { cpumMsrWr_IntelLastBranchTos },
    { cpumMsrWr_IntelBblCrCtl },
    { cpumMsrWr_IntelBblCrCtl3 },
    { cpumMsrWr_IntelI7TemperatureTarget },
    { cpumMsrWr_IntelI7MsrOffCoreResponseN },
    { cpumMsrWr_IntelI7MiscPwrMgmt },
    { cpumMsrWr_IntelP6CrN },
    { cpumMsrWr_IntelCpuId1FeatureMaskEcdx },
    { cpumMsrWr_IntelCpuId1FeatureMaskEax },
    { cpumMsrWr_IntelCpuId80000001FeatureMaskEcdx },
    { cpumMsrWr_IntelI7SandyAesNiCtl },
    { cpumMsrWr_IntelI7TurboRatioLimit },
    { cpumMsrWr_IntelI7LbrSelect },
    { cpumMsrWr_IntelI7SandyErrorControl },
    { cpumMsrWr_IntelI7PowerCtl },
    { cpumMsrWr_IntelI7SandyPebsNumAlt },
    { cpumMsrWr_IntelI7PebsLdLat },
    { cpumMsrWr_IntelI7SandyVrCurrentConfig },
    { cpumMsrWr_IntelI7SandyVrMiscConfig },
    { cpumMsrWr_IntelI7SandyRaplPowerUnit },
    { cpumMsrWr_IntelI7SandyPkgCnIrtlN },
    { cpumMsrWr_IntelI7SandyPkgC2Residency },
    { cpumMsrWr_IntelI7RaplPkgPowerLimit },
    { cpumMsrWr_IntelI7RaplDramPowerLimit },
    { cpumMsrWr_IntelI7RaplPp0PowerLimit },
    { cpumMsrWr_IntelI7RaplPp0Policy },
    { cpumMsrWr_IntelI7RaplPp1PowerLimit },
    { cpumMsrWr_IntelI7RaplPp1Policy },
    { cpumMsrWr_IntelI7IvyConfigTdpControl },
    { cpumMsrWr_IntelI7IvyTurboActivationRatio },
    { cpumMsrWr_IntelI7UncPerfGlobalCtrl },
    { cpumMsrWr_IntelI7UncPerfGlobalStatus },
    { cpumMsrWr_IntelI7UncPerfGlobalOvfCtrl },
    { cpumMsrWr_IntelI7UncPerfFixedCtrCtrl },
    { cpumMsrWr_IntelI7UncPerfFixedCtr },
    { cpumMsrWr_IntelI7UncArbPerfCtrN },
    { cpumMsrWr_IntelI7UncArbPerfEvtSelN },
    { cpumMsrWr_IntelCore2EmttmCrTablesN },
    { cpumMsrWr_IntelCore2SmmCStMiscInfo },
    { cpumMsrWr_IntelCore1ExtConfig },
    { cpumMsrWr_IntelCore1DtsCalControl },
    { cpumMsrWr_IntelCore2PeciControl },

    { cpumMsrWr_P6LastIntFromIp },
    { cpumMsrWr_P6LastIntToIp },

    { cpumMsrWr_AmdFam15hTscRate },
    { cpumMsrWr_AmdFam15hLwpCfg },
    { cpumMsrWr_AmdFam15hLwpCbAddr },
    { cpumMsrWr_AmdFam10hMc4MiscN },
    { cpumMsrWr_AmdK8PerfCtlN },
    { cpumMsrWr_AmdK8PerfCtrN },
    { cpumMsrWr_AmdK8SysCfg },
    { cpumMsrWr_AmdK8HwCr },
    { cpumMsrWr_AmdK8IorrBaseN },
    { cpumMsrWr_AmdK8IorrMaskN },
    { cpumMsrWr_AmdK8TopOfMemN },
    { cpumMsrWr_AmdK8NbCfg1 },
    { cpumMsrWr_AmdK8McXcptRedir },
    { cpumMsrWr_AmdK8CpuNameN },
    { cpumMsrWr_AmdK8HwThermalCtrl },
    { cpumMsrWr_AmdK8SwThermalCtrl },
    { cpumMsrWr_AmdK8FidVidControl },
    { cpumMsrWr_AmdK8McCtlMaskN },
    { cpumMsrWr_AmdK8SmiOnIoTrapN },
    { cpumMsrWr_AmdK8SmiOnIoTrapCtlSts },
    { cpumMsrWr_AmdK8IntPendingMessage },
    { cpumMsrWr_AmdK8SmiTriggerIoCycle },
    { cpumMsrWr_AmdFam10hMmioCfgBaseAddr },
    { cpumMsrWr_AmdFam10hTrapCtlMaybe },
    { cpumMsrWr_AmdFam10hPStateControl },
    { cpumMsrWr_AmdFam10hPStateStatus },
    { cpumMsrWr_AmdFam10hPStateN },
    { cpumMsrWr_AmdFam10hCofVidControl },
    { cpumMsrWr_AmdFam10hCofVidStatus },
    { cpumMsrWr_AmdFam10hCStateIoBaseAddr },
    { cpumMsrWr_AmdFam10hCpuWatchdogTimer },
    { cpumMsrWr_AmdK8SmmBase },
    { cpumMsrWr_AmdK8SmmAddr },
    { cpumMsrWr_AmdK8SmmMask },
    { cpumMsrWr_AmdK8VmCr },
    { cpumMsrWr_AmdK8IgnNe },
    { cpumMsrWr_AmdK8SmmCtl },
    { cpumMsrWr_AmdK8VmHSavePa },
    { cpumMsrWr_AmdFam10hVmLockKey },
    { cpumMsrWr_AmdFam10hSmmLockKey },
    { cpumMsrWr_AmdFam10hLocalSmiStatus },
    { cpumMsrWr_AmdFam10hOsVisWrkIdLength },
    { cpumMsrWr_AmdFam10hOsVisWrkStatus },
    { cpumMsrWr_AmdFam16hL2IPerfCtlN },
    { cpumMsrWr_AmdFam16hL2IPerfCtrN },
    { cpumMsrWr_AmdFam15hNorthbridgePerfCtlN },
    { cpumMsrWr_AmdFam15hNorthbridgePerfCtrN },
    { cpumMsrWr_AmdK7MicrocodeCtl },
    { cpumMsrWr_AmdK7ClusterIdMaybe },
    { cpumMsrWr_AmdK8CpuIdCtlStd07hEbax },
    { cpumMsrWr_AmdK8CpuIdCtlStd06hEcx },
    { cpumMsrWr_AmdK8CpuIdCtlStd01hEdcx },
    { cpumMsrWr_AmdK8CpuIdCtlExt01hEdcx },
    { cpumMsrWr_AmdK8PatchLoader },
    { cpumMsrWr_AmdK7DebugStatusMaybe },
    { cpumMsrWr_AmdK7BHTraceBaseMaybe },
    { cpumMsrWr_AmdK7BHTracePtrMaybe },
    { cpumMsrWr_AmdK7BHTraceLimitMaybe },
    { cpumMsrWr_AmdK7HardwareDebugToolCfgMaybe },
    { cpumMsrWr_AmdK7FastFlushCountMaybe },
    { cpumMsrWr_AmdK7NodeId },
    { cpumMsrWr_AmdK7DrXAddrMaskN },
    { cpumMsrWr_AmdK7Dr0DataMatchMaybe },
    { cpumMsrWr_AmdK7Dr0DataMaskMaybe },
    { cpumMsrWr_AmdK7LoadStoreCfg },
    { cpumMsrWr_AmdK7InstrCacheCfg },
    { cpumMsrWr_AmdK7DataCacheCfg },
    { cpumMsrWr_AmdK7BusUnitCfg },
    { cpumMsrWr_AmdK7DebugCtl2Maybe },
    { cpumMsrWr_AmdFam15hFpuCfg },
    { cpumMsrWr_AmdFam15hDecoderCfg },
    { cpumMsrWr_AmdFam10hBusUnitCfg2 },
    { cpumMsrWr_AmdFam15hCombUnitCfg },
    { cpumMsrWr_AmdFam15hCombUnitCfg2 },
    { cpumMsrWr_AmdFam15hCombUnitCfg3 },
    { cpumMsrWr_AmdFam15hExecUnitCfg },
    { cpumMsrWr_AmdFam15hLoadStoreCfg2 },
    { cpumMsrWr_AmdFam10hIbsFetchCtl },
    { cpumMsrWr_AmdFam10hIbsFetchLinAddr },
    { cpumMsrWr_AmdFam10hIbsFetchPhysAddr },
    { cpumMsrWr_AmdFam10hIbsOpExecCtl },
    { cpumMsrWr_AmdFam10hIbsOpRip },
    { cpumMsrWr_AmdFam10hIbsOpData },
    { cpumMsrWr_AmdFam10hIbsOpData2 },
    { cpumMsrWr_AmdFam10hIbsOpData3 },
    { cpumMsrWr_AmdFam10hIbsDcLinAddr },
    { cpumMsrWr_AmdFam10hIbsDcPhysAddr },
    { cpumMsrWr_AmdFam10hIbsCtl },
    { cpumMsrWr_AmdFam14hIbsBrTarget },

    { cpumMsrWr_Gim },
};


/**
 * Looks up the range for the given MSR.
 *
 * @returns Pointer to the range if found, NULL if not.
 * @param   pVM                The cross context VM structure.
 * @param   idMsr              The MSR to look up.
 */
# ifndef IN_RING3
static
# endif
PCPUMMSRRANGE cpumLookupMsrRange(PVM pVM, uint32_t idMsr)
{
    /*
     * Binary lookup.
     */
    uint32_t        cRanges   = RT_MIN(pVM->cpum.s.GuestInfo.cMsrRanges, RT_ELEMENTS(pVM->cpum.s.GuestInfo.aMsrRanges));
    if (!cRanges)
        return NULL;
# ifdef IN_RING3 /* Must use paMsrRangesR3 for it to work during early init (fudging). */
    PCPUMMSRRANGE   paRanges  = pVM->cpum.s.GuestInfo.paMsrRangesR3;
# else
    PCPUMMSRRANGE   paRanges  = pVM->cpum.s.GuestInfo.aMsrRanges;
# endif
    for (;;)
    {
        uint32_t i = cRanges / 2;
        if (idMsr < paRanges[i].uFirst)
        {
            if (i == 0)
                break;
            cRanges = i;
        }
        else if (idMsr > paRanges[i].uLast)
        {
            i++;
            if (i >= cRanges)
                break;
            cRanges -= i;
            paRanges = &paRanges[i];
        }
        else
        {
            if (paRanges[i].enmRdFn == kCpumMsrRdFn_MsrAlias)
                return cpumLookupMsrRange(pVM, paRanges[i].uValue);
            return &paRanges[i];
        }
    }

# ifdef VBOX_STRICT
    /*
     * Linear lookup to verify the above binary search.
     */
    uint32_t        cLeft = RT_MIN(pVM->cpum.s.GuestInfo.cMsrRanges, RT_ELEMENTS(pVM->cpum.s.GuestInfo.aMsrRanges));
    PCPUMMSRRANGE   pCur  = pVM->cpum.s.GuestInfo.aMsrRanges;
    while (cLeft-- > 0)
    {
        if (idMsr >= pCur->uFirst && idMsr <= pCur->uLast)
        {
            AssertFailed();
            if (pCur->enmRdFn == kCpumMsrRdFn_MsrAlias)
                return cpumLookupMsrRange(pVM, pCur->uValue);
            return pCur;
        }
        pCur++;
    }
# endif
    return NULL;
}


/**
 * Query a guest MSR.
 *
 * The caller is responsible for checking privilege if the call is the result of
 * a RDMSR instruction.  We'll do the rest.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_CPUM_R3_MSR_READ if the MSR read could not be serviced in the
 *          current context (raw-mode or ring-0).
 * @retval  VERR_CPUM_RAISE_GP_0 on failure (invalid MSR), the caller is
 *          expected to take the appropriate actions. @a *puValue is set to 0.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   idMsr               The MSR.
 * @param   puValue             Where to return the value.
 *
 * @remarks This will always return the right values, even when we're in the
 *          recompiler.
 */
VMMDECL(VBOXSTRICTRC) CPUMQueryGuestMsr(PVMCPUCC pVCpu, uint32_t idMsr, uint64_t *puValue)
{
    *puValue = 0;

    VBOXSTRICTRC    rcStrict;
    PVM             pVM    = pVCpu->CTX_SUFF(pVM);
    PCPUMMSRRANGE   pRange = cpumLookupMsrRange(pVM, idMsr);
    if (pRange)
    {
        CPUMMSRRDFN  enmRdFn = (CPUMMSRRDFN)pRange->enmRdFn;
        AssertReturn(enmRdFn > kCpumMsrRdFn_Invalid && enmRdFn < kCpumMsrRdFn_End, VERR_CPUM_IPE_1);

        PFNCPUMRDMSR pfnRdMsr = g_aCpumRdMsrFns[enmRdFn].pfnRdMsr;
        AssertReturn(pfnRdMsr, VERR_CPUM_IPE_2);

        STAM_COUNTER_INC(&pRange->cReads);
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cMsrReads);

        rcStrict = pfnRdMsr(pVCpu, idMsr, pRange, puValue);
        if (rcStrict == VINF_SUCCESS)
            Log2(("CPUM: RDMSR %#x (%s) -> %#llx\n", idMsr, pRange->szName, *puValue));
        else if (rcStrict == VERR_CPUM_RAISE_GP_0)
        {
            Log(("CPUM: RDMSR %#x (%s) -> #GP(0)\n", idMsr, pRange->szName));
            STAM_COUNTER_INC(&pRange->cGps);
            STAM_REL_COUNTER_INC(&pVM->cpum.s.cMsrReadsRaiseGp);
        }
#ifndef IN_RING3
        else if (rcStrict == VINF_CPUM_R3_MSR_READ)
            Log(("CPUM: RDMSR %#x (%s) -> ring-3\n", idMsr, pRange->szName));
#endif
        else
        {
            Log(("CPUM: RDMSR %#x (%s) -> rcStrict=%Rrc\n", idMsr, pRange->szName, VBOXSTRICTRC_VAL(rcStrict)));
            AssertMsgStmt(RT_FAILURE_NP(rcStrict), ("%Rrc idMsr=%#x\n", VBOXSTRICTRC_VAL(rcStrict), idMsr),
                          rcStrict = VERR_IPE_UNEXPECTED_INFO_STATUS);
            Assert(rcStrict != VERR_EM_INTERPRETER);
        }
    }
    else
    {
        Log(("CPUM: Unknown RDMSR %#x -> #GP(0)\n", idMsr));
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cMsrReads);
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cMsrReadsUnknown);
        rcStrict = VERR_CPUM_RAISE_GP_0;
    }
    return rcStrict;
}


/**
 * Writes to a guest MSR.
 *
 * The caller is responsible for checking privilege if the call is the result of
 * a WRMSR instruction.  We'll do the rest.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_CPUM_R3_MSR_WRITE if the MSR write could not be serviced in the
 *          current context (raw-mode or ring-0).
 * @retval  VERR_CPUM_RAISE_GP_0 on failure, the caller is expected to take the
 *          appropriate actions.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR id.
 * @param   uValue      The value to set.
 *
 * @remarks Everyone changing MSR values, including the recompiler, shall do it
 *          by calling this method.  This makes sure we have current values and
 *          that we trigger all the right actions when something changes.
 *
 *          For performance reasons, this actually isn't entirely true for some
 *          MSRs when in HM mode.  The code here and in HM must be aware of
 *          this.
 */
VMMDECL(VBOXSTRICTRC) CPUMSetGuestMsr(PVMCPUCC pVCpu, uint32_t idMsr, uint64_t uValue)
{
    VBOXSTRICTRC    rcStrict;
    PVM             pVM    = pVCpu->CTX_SUFF(pVM);
    PCPUMMSRRANGE   pRange = cpumLookupMsrRange(pVM, idMsr);
    if (pRange)
    {
        STAM_COUNTER_INC(&pRange->cWrites);
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cMsrWrites);

        if (!(uValue & pRange->fWrGpMask))
        {
            CPUMMSRWRFN  enmWrFn = (CPUMMSRWRFN)pRange->enmWrFn;
            AssertReturn(enmWrFn > kCpumMsrWrFn_Invalid && enmWrFn < kCpumMsrWrFn_End, VERR_CPUM_IPE_1);

            PFNCPUMWRMSR pfnWrMsr = g_aCpumWrMsrFns[enmWrFn].pfnWrMsr;
            AssertReturn(pfnWrMsr, VERR_CPUM_IPE_2);

            uint64_t uValueAdjusted = uValue & ~pRange->fWrIgnMask;
            if (uValueAdjusted != uValue)
            {
                STAM_COUNTER_INC(&pRange->cIgnoredBits);
                STAM_REL_COUNTER_INC(&pVM->cpum.s.cMsrWritesToIgnoredBits);
            }

            rcStrict = pfnWrMsr(pVCpu, idMsr, pRange, uValueAdjusted, uValue);
            if (rcStrict == VINF_SUCCESS)
                Log2(("CPUM: WRMSR %#x (%s), %#llx [%#llx]\n", idMsr, pRange->szName, uValueAdjusted, uValue));
            else if (rcStrict == VERR_CPUM_RAISE_GP_0)
            {
                Log(("CPUM: WRMSR %#x (%s), %#llx [%#llx] -> #GP(0)\n", idMsr, pRange->szName, uValueAdjusted, uValue));
                STAM_COUNTER_INC(&pRange->cGps);
                STAM_REL_COUNTER_INC(&pVM->cpum.s.cMsrWritesRaiseGp);
            }
#ifndef IN_RING3
            else if (rcStrict == VINF_CPUM_R3_MSR_WRITE)
                Log(("CPUM: WRMSR %#x (%s), %#llx [%#llx] -> ring-3\n", idMsr, pRange->szName, uValueAdjusted, uValue));
#endif
            else
            {
                Log(("CPUM: WRMSR %#x (%s), %#llx [%#llx] -> rcStrict=%Rrc\n",
                     idMsr, pRange->szName, uValueAdjusted, uValue, VBOXSTRICTRC_VAL(rcStrict)));
                AssertMsgStmt(RT_FAILURE_NP(rcStrict), ("%Rrc idMsr=%#x\n", VBOXSTRICTRC_VAL(rcStrict), idMsr),
                              rcStrict = VERR_IPE_UNEXPECTED_INFO_STATUS);
                Assert(rcStrict != VERR_EM_INTERPRETER);
            }
        }
        else
        {
            Log(("CPUM: WRMSR %#x (%s), %#llx -> #GP(0) - invalid bits %#llx\n",
                 idMsr, pRange->szName, uValue, uValue & pRange->fWrGpMask));
            STAM_COUNTER_INC(&pRange->cGps);
            STAM_REL_COUNTER_INC(&pVM->cpum.s.cMsrWritesRaiseGp);
            rcStrict = VERR_CPUM_RAISE_GP_0;
        }
    }
    else
    {
        Log(("CPUM: Unknown WRMSR %#x, %#llx -> #GP(0)\n", idMsr, uValue));
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cMsrWrites);
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cMsrWritesUnknown);
        rcStrict = VERR_CPUM_RAISE_GP_0;
    }
    return rcStrict;
}


#if defined(VBOX_STRICT) && defined(IN_RING3)
/**
 * Performs some checks on the static data related to MSRs.
 *
 * @returns VINF_SUCCESS on success, error on failure.
 */
int cpumR3MsrStrictInitChecks(void)
{
#define CPUM_ASSERT_RD_MSR_FN(a_Register) \
        AssertReturn(g_aCpumRdMsrFns[kCpumMsrRdFn_##a_Register].pfnRdMsr == cpumMsrRd_##a_Register, VERR_CPUM_IPE_2);
#define CPUM_ASSERT_WR_MSR_FN(a_Register) \
        AssertReturn(g_aCpumWrMsrFns[kCpumMsrWrFn_##a_Register].pfnWrMsr == cpumMsrWr_##a_Register, VERR_CPUM_IPE_2);

    AssertReturn(g_aCpumRdMsrFns[kCpumMsrRdFn_Invalid].pfnRdMsr == NULL, VERR_CPUM_IPE_2);
    CPUM_ASSERT_RD_MSR_FN(FixedValue);
    CPUM_ASSERT_RD_MSR_FN(WriteOnly);
    CPUM_ASSERT_RD_MSR_FN(Ia32P5McAddr);
    CPUM_ASSERT_RD_MSR_FN(Ia32P5McType);
    CPUM_ASSERT_RD_MSR_FN(Ia32TimestampCounter);
    CPUM_ASSERT_RD_MSR_FN(Ia32PlatformId);
    CPUM_ASSERT_RD_MSR_FN(Ia32ApicBase);
    CPUM_ASSERT_RD_MSR_FN(Ia32FeatureControl);
    CPUM_ASSERT_RD_MSR_FN(Ia32BiosSignId);
    CPUM_ASSERT_RD_MSR_FN(Ia32SmmMonitorCtl);
    CPUM_ASSERT_RD_MSR_FN(Ia32PmcN);
    CPUM_ASSERT_RD_MSR_FN(Ia32MonitorFilterLineSize);
    CPUM_ASSERT_RD_MSR_FN(Ia32MPerf);
    CPUM_ASSERT_RD_MSR_FN(Ia32APerf);
    CPUM_ASSERT_RD_MSR_FN(Ia32MtrrCap);
    CPUM_ASSERT_RD_MSR_FN(Ia32MtrrPhysBaseN);
    CPUM_ASSERT_RD_MSR_FN(Ia32MtrrPhysMaskN);
    CPUM_ASSERT_RD_MSR_FN(Ia32MtrrFixed);
    CPUM_ASSERT_RD_MSR_FN(Ia32MtrrDefType);
    CPUM_ASSERT_RD_MSR_FN(Ia32Pat);
    CPUM_ASSERT_RD_MSR_FN(Ia32SysEnterCs);
    CPUM_ASSERT_RD_MSR_FN(Ia32SysEnterEsp);
    CPUM_ASSERT_RD_MSR_FN(Ia32SysEnterEip);
    CPUM_ASSERT_RD_MSR_FN(Ia32McgCap);
    CPUM_ASSERT_RD_MSR_FN(Ia32McgStatus);
    CPUM_ASSERT_RD_MSR_FN(Ia32McgCtl);
    CPUM_ASSERT_RD_MSR_FN(Ia32DebugCtl);
    CPUM_ASSERT_RD_MSR_FN(Ia32SmrrPhysBase);
    CPUM_ASSERT_RD_MSR_FN(Ia32SmrrPhysMask);
    CPUM_ASSERT_RD_MSR_FN(Ia32PlatformDcaCap);
    CPUM_ASSERT_RD_MSR_FN(Ia32CpuDcaCap);
    CPUM_ASSERT_RD_MSR_FN(Ia32Dca0Cap);
    CPUM_ASSERT_RD_MSR_FN(Ia32PerfEvtSelN);
    CPUM_ASSERT_RD_MSR_FN(Ia32PerfStatus);
    CPUM_ASSERT_RD_MSR_FN(Ia32PerfCtl);
    CPUM_ASSERT_RD_MSR_FN(Ia32FixedCtrN);
    CPUM_ASSERT_RD_MSR_FN(Ia32PerfCapabilities);
    CPUM_ASSERT_RD_MSR_FN(Ia32FixedCtrCtrl);
    CPUM_ASSERT_RD_MSR_FN(Ia32PerfGlobalStatus);
    CPUM_ASSERT_RD_MSR_FN(Ia32PerfGlobalCtrl);
    CPUM_ASSERT_RD_MSR_FN(Ia32PerfGlobalOvfCtrl);
    CPUM_ASSERT_RD_MSR_FN(Ia32PebsEnable);
    CPUM_ASSERT_RD_MSR_FN(Ia32ClockModulation);
    CPUM_ASSERT_RD_MSR_FN(Ia32ThermInterrupt);
    CPUM_ASSERT_RD_MSR_FN(Ia32ThermStatus);
    CPUM_ASSERT_RD_MSR_FN(Ia32MiscEnable);
    CPUM_ASSERT_RD_MSR_FN(Ia32McCtlStatusAddrMiscN);
    CPUM_ASSERT_RD_MSR_FN(Ia32McNCtl2);
    CPUM_ASSERT_RD_MSR_FN(Ia32DsArea);
    CPUM_ASSERT_RD_MSR_FN(Ia32TscDeadline);
    CPUM_ASSERT_RD_MSR_FN(Ia32X2ApicN);
    CPUM_ASSERT_RD_MSR_FN(Ia32DebugInterface);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxBasic);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxPinbasedCtls);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxProcbasedCtls);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxExitCtls);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxEntryCtls);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxMisc);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxCr0Fixed0);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxCr0Fixed1);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxCr4Fixed0);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxCr4Fixed1);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxVmcsEnum);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxProcBasedCtls2);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxEptVpidCap);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxTruePinbasedCtls);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxTrueProcbasedCtls);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxTrueExitCtls);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxTrueEntryCtls);
    CPUM_ASSERT_RD_MSR_FN(Ia32VmxVmFunc);
    CPUM_ASSERT_RD_MSR_FN(Ia32SpecCtrl);
    CPUM_ASSERT_RD_MSR_FN(Ia32ArchCapabilities);

    CPUM_ASSERT_RD_MSR_FN(Amd64Efer);
    CPUM_ASSERT_RD_MSR_FN(Amd64SyscallTarget);
    CPUM_ASSERT_RD_MSR_FN(Amd64LongSyscallTarget);
    CPUM_ASSERT_RD_MSR_FN(Amd64CompSyscallTarget);
    CPUM_ASSERT_RD_MSR_FN(Amd64SyscallFlagMask);
    CPUM_ASSERT_RD_MSR_FN(Amd64FsBase);
    CPUM_ASSERT_RD_MSR_FN(Amd64GsBase);
    CPUM_ASSERT_RD_MSR_FN(Amd64KernelGsBase);
    CPUM_ASSERT_RD_MSR_FN(Amd64TscAux);

    CPUM_ASSERT_RD_MSR_FN(IntelEblCrPowerOn);
    CPUM_ASSERT_RD_MSR_FN(IntelI7CoreThreadCount);
    CPUM_ASSERT_RD_MSR_FN(IntelP4EbcHardPowerOn);
    CPUM_ASSERT_RD_MSR_FN(IntelP4EbcSoftPowerOn);
    CPUM_ASSERT_RD_MSR_FN(IntelP4EbcFrequencyId);
    CPUM_ASSERT_RD_MSR_FN(IntelP6FsbFrequency);
    CPUM_ASSERT_RD_MSR_FN(IntelPlatformInfo);
    CPUM_ASSERT_RD_MSR_FN(IntelFlexRatio);
    CPUM_ASSERT_RD_MSR_FN(IntelPkgCStConfigControl);
    CPUM_ASSERT_RD_MSR_FN(IntelPmgIoCaptureBase);
    CPUM_ASSERT_RD_MSR_FN(IntelLastBranchFromToN);
    CPUM_ASSERT_RD_MSR_FN(IntelLastBranchFromN);
    CPUM_ASSERT_RD_MSR_FN(IntelLastBranchToN);
    CPUM_ASSERT_RD_MSR_FN(IntelLastBranchTos);
    CPUM_ASSERT_RD_MSR_FN(IntelBblCrCtl);
    CPUM_ASSERT_RD_MSR_FN(IntelBblCrCtl3);
    CPUM_ASSERT_RD_MSR_FN(IntelI7TemperatureTarget);
    CPUM_ASSERT_RD_MSR_FN(IntelI7MsrOffCoreResponseN);
    CPUM_ASSERT_RD_MSR_FN(IntelI7MiscPwrMgmt);
    CPUM_ASSERT_RD_MSR_FN(IntelP6CrN);
    CPUM_ASSERT_RD_MSR_FN(IntelCpuId1FeatureMaskEcdx);
    CPUM_ASSERT_RD_MSR_FN(IntelCpuId1FeatureMaskEax);
    CPUM_ASSERT_RD_MSR_FN(IntelCpuId80000001FeatureMaskEcdx);
    CPUM_ASSERT_RD_MSR_FN(IntelI7SandyAesNiCtl);
    CPUM_ASSERT_RD_MSR_FN(IntelI7TurboRatioLimit);
    CPUM_ASSERT_RD_MSR_FN(IntelI7LbrSelect);
    CPUM_ASSERT_RD_MSR_FN(IntelI7SandyErrorControl);
    CPUM_ASSERT_RD_MSR_FN(IntelI7VirtualLegacyWireCap);
    CPUM_ASSERT_RD_MSR_FN(IntelI7PowerCtl);
    CPUM_ASSERT_RD_MSR_FN(IntelI7SandyPebsNumAlt);
    CPUM_ASSERT_RD_MSR_FN(IntelI7PebsLdLat);
    CPUM_ASSERT_RD_MSR_FN(IntelI7PkgCnResidencyN);
    CPUM_ASSERT_RD_MSR_FN(IntelI7CoreCnResidencyN);
    CPUM_ASSERT_RD_MSR_FN(IntelI7SandyVrCurrentConfig);
    CPUM_ASSERT_RD_MSR_FN(IntelI7SandyVrMiscConfig);
    CPUM_ASSERT_RD_MSR_FN(IntelI7SandyRaplPowerUnit);
    CPUM_ASSERT_RD_MSR_FN(IntelI7SandyPkgCnIrtlN);
    CPUM_ASSERT_RD_MSR_FN(IntelI7SandyPkgC2Residency);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplPkgPowerLimit);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplPkgEnergyStatus);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplPkgPerfStatus);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplPkgPowerInfo);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplDramPowerLimit);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplDramEnergyStatus);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplDramPerfStatus);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplDramPowerInfo);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplPp0PowerLimit);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplPp0EnergyStatus);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplPp0Policy);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplPp0PerfStatus);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplPp1PowerLimit);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplPp1EnergyStatus);
    CPUM_ASSERT_RD_MSR_FN(IntelI7RaplPp1Policy);
    CPUM_ASSERT_RD_MSR_FN(IntelI7IvyConfigTdpNominal);
    CPUM_ASSERT_RD_MSR_FN(IntelI7IvyConfigTdpLevel1);
    CPUM_ASSERT_RD_MSR_FN(IntelI7IvyConfigTdpLevel2);
    CPUM_ASSERT_RD_MSR_FN(IntelI7IvyConfigTdpControl);
    CPUM_ASSERT_RD_MSR_FN(IntelI7IvyTurboActivationRatio);
    CPUM_ASSERT_RD_MSR_FN(IntelI7UncPerfGlobalCtrl);
    CPUM_ASSERT_RD_MSR_FN(IntelI7UncPerfGlobalStatus);
    CPUM_ASSERT_RD_MSR_FN(IntelI7UncPerfGlobalOvfCtrl);
    CPUM_ASSERT_RD_MSR_FN(IntelI7UncPerfFixedCtrCtrl);
    CPUM_ASSERT_RD_MSR_FN(IntelI7UncPerfFixedCtr);
    CPUM_ASSERT_RD_MSR_FN(IntelI7UncCBoxConfig);
    CPUM_ASSERT_RD_MSR_FN(IntelI7UncArbPerfCtrN);
    CPUM_ASSERT_RD_MSR_FN(IntelI7UncArbPerfEvtSelN);
    CPUM_ASSERT_RD_MSR_FN(IntelI7SmiCount);
    CPUM_ASSERT_RD_MSR_FN(IntelCore2EmttmCrTablesN);
    CPUM_ASSERT_RD_MSR_FN(IntelCore2SmmCStMiscInfo);
    CPUM_ASSERT_RD_MSR_FN(IntelCore1ExtConfig);
    CPUM_ASSERT_RD_MSR_FN(IntelCore1DtsCalControl);
    CPUM_ASSERT_RD_MSR_FN(IntelCore2PeciControl);
    CPUM_ASSERT_RD_MSR_FN(IntelAtSilvCoreC1Recidency);

    CPUM_ASSERT_RD_MSR_FN(P6LastBranchFromIp);
    CPUM_ASSERT_RD_MSR_FN(P6LastBranchToIp);
    CPUM_ASSERT_RD_MSR_FN(P6LastIntFromIp);
    CPUM_ASSERT_RD_MSR_FN(P6LastIntToIp);

    CPUM_ASSERT_RD_MSR_FN(AmdFam15hTscRate);
    CPUM_ASSERT_RD_MSR_FN(AmdFam15hLwpCfg);
    CPUM_ASSERT_RD_MSR_FN(AmdFam15hLwpCbAddr);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hMc4MiscN);
    CPUM_ASSERT_RD_MSR_FN(AmdK8PerfCtlN);
    CPUM_ASSERT_RD_MSR_FN(AmdK8PerfCtrN);
    CPUM_ASSERT_RD_MSR_FN(AmdK8SysCfg);
    CPUM_ASSERT_RD_MSR_FN(AmdK8HwCr);
    CPUM_ASSERT_RD_MSR_FN(AmdK8IorrBaseN);
    CPUM_ASSERT_RD_MSR_FN(AmdK8IorrMaskN);
    CPUM_ASSERT_RD_MSR_FN(AmdK8TopOfMemN);
    CPUM_ASSERT_RD_MSR_FN(AmdK8NbCfg1);
    CPUM_ASSERT_RD_MSR_FN(AmdK8McXcptRedir);
    CPUM_ASSERT_RD_MSR_FN(AmdK8CpuNameN);
    CPUM_ASSERT_RD_MSR_FN(AmdK8HwThermalCtrl);
    CPUM_ASSERT_RD_MSR_FN(AmdK8SwThermalCtrl);
    CPUM_ASSERT_RD_MSR_FN(AmdK8FidVidControl);
    CPUM_ASSERT_RD_MSR_FN(AmdK8FidVidStatus);
    CPUM_ASSERT_RD_MSR_FN(AmdK8McCtlMaskN);
    CPUM_ASSERT_RD_MSR_FN(AmdK8SmiOnIoTrapN);
    CPUM_ASSERT_RD_MSR_FN(AmdK8SmiOnIoTrapCtlSts);
    CPUM_ASSERT_RD_MSR_FN(AmdK8IntPendingMessage);
    CPUM_ASSERT_RD_MSR_FN(AmdK8SmiTriggerIoCycle);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hMmioCfgBaseAddr);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hTrapCtlMaybe);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hPStateCurLimit);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hPStateControl);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hPStateStatus);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hPStateN);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hCofVidControl);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hCofVidStatus);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hCStateIoBaseAddr);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hCpuWatchdogTimer);
    CPUM_ASSERT_RD_MSR_FN(AmdK8SmmBase);
    CPUM_ASSERT_RD_MSR_FN(AmdK8SmmAddr);
    CPUM_ASSERT_RD_MSR_FN(AmdK8SmmMask);
    CPUM_ASSERT_RD_MSR_FN(AmdK8VmCr);
    CPUM_ASSERT_RD_MSR_FN(AmdK8IgnNe);
    CPUM_ASSERT_RD_MSR_FN(AmdK8SmmCtl);
    CPUM_ASSERT_RD_MSR_FN(AmdK8VmHSavePa);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hVmLockKey);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hSmmLockKey);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hLocalSmiStatus);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hOsVisWrkIdLength);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hOsVisWrkStatus);
    CPUM_ASSERT_RD_MSR_FN(AmdFam16hL2IPerfCtlN);
    CPUM_ASSERT_RD_MSR_FN(AmdFam16hL2IPerfCtrN);
    CPUM_ASSERT_RD_MSR_FN(AmdFam15hNorthbridgePerfCtlN);
    CPUM_ASSERT_RD_MSR_FN(AmdFam15hNorthbridgePerfCtrN);
    CPUM_ASSERT_RD_MSR_FN(AmdK7MicrocodeCtl);
    CPUM_ASSERT_RD_MSR_FN(AmdK7ClusterIdMaybe);
    CPUM_ASSERT_RD_MSR_FN(AmdK8CpuIdCtlStd07hEbax);
    CPUM_ASSERT_RD_MSR_FN(AmdK8CpuIdCtlStd06hEcx);
    CPUM_ASSERT_RD_MSR_FN(AmdK8CpuIdCtlStd01hEdcx);
    CPUM_ASSERT_RD_MSR_FN(AmdK8CpuIdCtlExt01hEdcx);
    CPUM_ASSERT_RD_MSR_FN(AmdK8PatchLevel);
    CPUM_ASSERT_RD_MSR_FN(AmdK7DebugStatusMaybe);
    CPUM_ASSERT_RD_MSR_FN(AmdK7BHTraceBaseMaybe);
    CPUM_ASSERT_RD_MSR_FN(AmdK7BHTracePtrMaybe);
    CPUM_ASSERT_RD_MSR_FN(AmdK7BHTraceLimitMaybe);
    CPUM_ASSERT_RD_MSR_FN(AmdK7HardwareDebugToolCfgMaybe);
    CPUM_ASSERT_RD_MSR_FN(AmdK7FastFlushCountMaybe);
    CPUM_ASSERT_RD_MSR_FN(AmdK7NodeId);
    CPUM_ASSERT_RD_MSR_FN(AmdK7DrXAddrMaskN);
    CPUM_ASSERT_RD_MSR_FN(AmdK7Dr0DataMatchMaybe);
    CPUM_ASSERT_RD_MSR_FN(AmdK7Dr0DataMaskMaybe);
    CPUM_ASSERT_RD_MSR_FN(AmdK7LoadStoreCfg);
    CPUM_ASSERT_RD_MSR_FN(AmdK7InstrCacheCfg);
    CPUM_ASSERT_RD_MSR_FN(AmdK7DataCacheCfg);
    CPUM_ASSERT_RD_MSR_FN(AmdK7BusUnitCfg);
    CPUM_ASSERT_RD_MSR_FN(AmdK7DebugCtl2Maybe);
    CPUM_ASSERT_RD_MSR_FN(AmdFam15hFpuCfg);
    CPUM_ASSERT_RD_MSR_FN(AmdFam15hDecoderCfg);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hBusUnitCfg2);
    CPUM_ASSERT_RD_MSR_FN(AmdFam15hCombUnitCfg);
    CPUM_ASSERT_RD_MSR_FN(AmdFam15hCombUnitCfg2);
    CPUM_ASSERT_RD_MSR_FN(AmdFam15hCombUnitCfg3);
    CPUM_ASSERT_RD_MSR_FN(AmdFam15hExecUnitCfg);
    CPUM_ASSERT_RD_MSR_FN(AmdFam15hLoadStoreCfg2);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hIbsFetchCtl);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hIbsFetchLinAddr);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hIbsFetchPhysAddr);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hIbsOpExecCtl);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hIbsOpRip);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hIbsOpData);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hIbsOpData2);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hIbsOpData3);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hIbsDcLinAddr);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hIbsDcPhysAddr);
    CPUM_ASSERT_RD_MSR_FN(AmdFam10hIbsCtl);
    CPUM_ASSERT_RD_MSR_FN(AmdFam14hIbsBrTarget);

    CPUM_ASSERT_RD_MSR_FN(Gim)

    AssertReturn(g_aCpumWrMsrFns[kCpumMsrWrFn_Invalid].pfnWrMsr == NULL, VERR_CPUM_IPE_2);
    CPUM_ASSERT_WR_MSR_FN(Ia32P5McAddr);
    CPUM_ASSERT_WR_MSR_FN(Ia32P5McType);
    CPUM_ASSERT_WR_MSR_FN(Ia32TimestampCounter);
    CPUM_ASSERT_WR_MSR_FN(Ia32ApicBase);
    CPUM_ASSERT_WR_MSR_FN(Ia32FeatureControl);
    CPUM_ASSERT_WR_MSR_FN(Ia32BiosSignId);
    CPUM_ASSERT_WR_MSR_FN(Ia32BiosUpdateTrigger);
    CPUM_ASSERT_WR_MSR_FN(Ia32SmmMonitorCtl);
    CPUM_ASSERT_WR_MSR_FN(Ia32PmcN);
    CPUM_ASSERT_WR_MSR_FN(Ia32MonitorFilterLineSize);
    CPUM_ASSERT_WR_MSR_FN(Ia32MPerf);
    CPUM_ASSERT_WR_MSR_FN(Ia32APerf);
    CPUM_ASSERT_WR_MSR_FN(Ia32MtrrPhysBaseN);
    CPUM_ASSERT_WR_MSR_FN(Ia32MtrrPhysMaskN);
    CPUM_ASSERT_WR_MSR_FN(Ia32MtrrFixed);
    CPUM_ASSERT_WR_MSR_FN(Ia32MtrrDefType);
    CPUM_ASSERT_WR_MSR_FN(Ia32Pat);
    CPUM_ASSERT_WR_MSR_FN(Ia32SysEnterCs);
    CPUM_ASSERT_WR_MSR_FN(Ia32SysEnterEsp);
    CPUM_ASSERT_WR_MSR_FN(Ia32SysEnterEip);
    CPUM_ASSERT_WR_MSR_FN(Ia32McgStatus);
    CPUM_ASSERT_WR_MSR_FN(Ia32McgCtl);
    CPUM_ASSERT_WR_MSR_FN(Ia32DebugCtl);
    CPUM_ASSERT_WR_MSR_FN(Ia32SmrrPhysBase);
    CPUM_ASSERT_WR_MSR_FN(Ia32SmrrPhysMask);
    CPUM_ASSERT_WR_MSR_FN(Ia32PlatformDcaCap);
    CPUM_ASSERT_WR_MSR_FN(Ia32Dca0Cap);
    CPUM_ASSERT_WR_MSR_FN(Ia32PerfEvtSelN);
    CPUM_ASSERT_WR_MSR_FN(Ia32PerfStatus);
    CPUM_ASSERT_WR_MSR_FN(Ia32PerfCtl);
    CPUM_ASSERT_WR_MSR_FN(Ia32FixedCtrN);
    CPUM_ASSERT_WR_MSR_FN(Ia32PerfCapabilities);
    CPUM_ASSERT_WR_MSR_FN(Ia32FixedCtrCtrl);
    CPUM_ASSERT_WR_MSR_FN(Ia32PerfGlobalStatus);
    CPUM_ASSERT_WR_MSR_FN(Ia32PerfGlobalCtrl);
    CPUM_ASSERT_WR_MSR_FN(Ia32PerfGlobalOvfCtrl);
    CPUM_ASSERT_WR_MSR_FN(Ia32PebsEnable);
    CPUM_ASSERT_WR_MSR_FN(Ia32ClockModulation);
    CPUM_ASSERT_WR_MSR_FN(Ia32ThermInterrupt);
    CPUM_ASSERT_WR_MSR_FN(Ia32ThermStatus);
    CPUM_ASSERT_WR_MSR_FN(Ia32MiscEnable);
    CPUM_ASSERT_WR_MSR_FN(Ia32McCtlStatusAddrMiscN);
    CPUM_ASSERT_WR_MSR_FN(Ia32McNCtl2);
    CPUM_ASSERT_WR_MSR_FN(Ia32DsArea);
    CPUM_ASSERT_WR_MSR_FN(Ia32TscDeadline);
    CPUM_ASSERT_WR_MSR_FN(Ia32X2ApicN);
    CPUM_ASSERT_WR_MSR_FN(Ia32DebugInterface);
    CPUM_ASSERT_WR_MSR_FN(Ia32SpecCtrl);
    CPUM_ASSERT_WR_MSR_FN(Ia32PredCmd);
    CPUM_ASSERT_WR_MSR_FN(Ia32FlushCmd);

    CPUM_ASSERT_WR_MSR_FN(Amd64Efer);
    CPUM_ASSERT_WR_MSR_FN(Amd64SyscallTarget);
    CPUM_ASSERT_WR_MSR_FN(Amd64LongSyscallTarget);
    CPUM_ASSERT_WR_MSR_FN(Amd64CompSyscallTarget);
    CPUM_ASSERT_WR_MSR_FN(Amd64SyscallFlagMask);
    CPUM_ASSERT_WR_MSR_FN(Amd64FsBase);
    CPUM_ASSERT_WR_MSR_FN(Amd64GsBase);
    CPUM_ASSERT_WR_MSR_FN(Amd64KernelGsBase);
    CPUM_ASSERT_WR_MSR_FN(Amd64TscAux);

    CPUM_ASSERT_WR_MSR_FN(IntelEblCrPowerOn);
    CPUM_ASSERT_WR_MSR_FN(IntelP4EbcHardPowerOn);
    CPUM_ASSERT_WR_MSR_FN(IntelP4EbcSoftPowerOn);
    CPUM_ASSERT_WR_MSR_FN(IntelP4EbcFrequencyId);
    CPUM_ASSERT_WR_MSR_FN(IntelFlexRatio);
    CPUM_ASSERT_WR_MSR_FN(IntelPkgCStConfigControl);
    CPUM_ASSERT_WR_MSR_FN(IntelPmgIoCaptureBase);
    CPUM_ASSERT_WR_MSR_FN(IntelLastBranchFromToN);
    CPUM_ASSERT_WR_MSR_FN(IntelLastBranchFromN);
    CPUM_ASSERT_WR_MSR_FN(IntelLastBranchToN);
    CPUM_ASSERT_WR_MSR_FN(IntelLastBranchTos);
    CPUM_ASSERT_WR_MSR_FN(IntelBblCrCtl);
    CPUM_ASSERT_WR_MSR_FN(IntelBblCrCtl3);
    CPUM_ASSERT_WR_MSR_FN(IntelI7TemperatureTarget);
    CPUM_ASSERT_WR_MSR_FN(IntelI7MsrOffCoreResponseN);
    CPUM_ASSERT_WR_MSR_FN(IntelI7MiscPwrMgmt);
    CPUM_ASSERT_WR_MSR_FN(IntelP6CrN);
    CPUM_ASSERT_WR_MSR_FN(IntelCpuId1FeatureMaskEcdx);
    CPUM_ASSERT_WR_MSR_FN(IntelCpuId1FeatureMaskEax);
    CPUM_ASSERT_WR_MSR_FN(IntelCpuId80000001FeatureMaskEcdx);
    CPUM_ASSERT_WR_MSR_FN(IntelI7SandyAesNiCtl);
    CPUM_ASSERT_WR_MSR_FN(IntelI7TurboRatioLimit);
    CPUM_ASSERT_WR_MSR_FN(IntelI7LbrSelect);
    CPUM_ASSERT_WR_MSR_FN(IntelI7SandyErrorControl);
    CPUM_ASSERT_WR_MSR_FN(IntelI7PowerCtl);
    CPUM_ASSERT_WR_MSR_FN(IntelI7SandyPebsNumAlt);
    CPUM_ASSERT_WR_MSR_FN(IntelI7PebsLdLat);
    CPUM_ASSERT_WR_MSR_FN(IntelI7SandyVrCurrentConfig);
    CPUM_ASSERT_WR_MSR_FN(IntelI7SandyVrMiscConfig);
    CPUM_ASSERT_WR_MSR_FN(IntelI7SandyPkgCnIrtlN);
    CPUM_ASSERT_WR_MSR_FN(IntelI7SandyPkgC2Residency);
    CPUM_ASSERT_WR_MSR_FN(IntelI7RaplPkgPowerLimit);
    CPUM_ASSERT_WR_MSR_FN(IntelI7RaplDramPowerLimit);
    CPUM_ASSERT_WR_MSR_FN(IntelI7RaplPp0PowerLimit);
    CPUM_ASSERT_WR_MSR_FN(IntelI7RaplPp0Policy);
    CPUM_ASSERT_WR_MSR_FN(IntelI7RaplPp1PowerLimit);
    CPUM_ASSERT_WR_MSR_FN(IntelI7RaplPp1Policy);
    CPUM_ASSERT_WR_MSR_FN(IntelI7IvyConfigTdpControl);
    CPUM_ASSERT_WR_MSR_FN(IntelI7IvyTurboActivationRatio);
    CPUM_ASSERT_WR_MSR_FN(IntelI7UncPerfGlobalCtrl);
    CPUM_ASSERT_WR_MSR_FN(IntelI7UncPerfGlobalStatus);
    CPUM_ASSERT_WR_MSR_FN(IntelI7UncPerfGlobalOvfCtrl);
    CPUM_ASSERT_WR_MSR_FN(IntelI7UncPerfFixedCtrCtrl);
    CPUM_ASSERT_WR_MSR_FN(IntelI7UncPerfFixedCtr);
    CPUM_ASSERT_WR_MSR_FN(IntelI7UncArbPerfCtrN);
    CPUM_ASSERT_WR_MSR_FN(IntelI7UncArbPerfEvtSelN);
    CPUM_ASSERT_WR_MSR_FN(IntelCore2EmttmCrTablesN);
    CPUM_ASSERT_WR_MSR_FN(IntelCore2SmmCStMiscInfo);
    CPUM_ASSERT_WR_MSR_FN(IntelCore1ExtConfig);
    CPUM_ASSERT_WR_MSR_FN(IntelCore1DtsCalControl);
    CPUM_ASSERT_WR_MSR_FN(IntelCore2PeciControl);

    CPUM_ASSERT_WR_MSR_FN(P6LastIntFromIp);
    CPUM_ASSERT_WR_MSR_FN(P6LastIntToIp);

    CPUM_ASSERT_WR_MSR_FN(AmdFam15hTscRate);
    CPUM_ASSERT_WR_MSR_FN(AmdFam15hLwpCfg);
    CPUM_ASSERT_WR_MSR_FN(AmdFam15hLwpCbAddr);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hMc4MiscN);
    CPUM_ASSERT_WR_MSR_FN(AmdK8PerfCtlN);
    CPUM_ASSERT_WR_MSR_FN(AmdK8PerfCtrN);
    CPUM_ASSERT_WR_MSR_FN(AmdK8SysCfg);
    CPUM_ASSERT_WR_MSR_FN(AmdK8HwCr);
    CPUM_ASSERT_WR_MSR_FN(AmdK8IorrBaseN);
    CPUM_ASSERT_WR_MSR_FN(AmdK8IorrMaskN);
    CPUM_ASSERT_WR_MSR_FN(AmdK8TopOfMemN);
    CPUM_ASSERT_WR_MSR_FN(AmdK8NbCfg1);
    CPUM_ASSERT_WR_MSR_FN(AmdK8McXcptRedir);
    CPUM_ASSERT_WR_MSR_FN(AmdK8CpuNameN);
    CPUM_ASSERT_WR_MSR_FN(AmdK8HwThermalCtrl);
    CPUM_ASSERT_WR_MSR_FN(AmdK8SwThermalCtrl);
    CPUM_ASSERT_WR_MSR_FN(AmdK8FidVidControl);
    CPUM_ASSERT_WR_MSR_FN(AmdK8McCtlMaskN);
    CPUM_ASSERT_WR_MSR_FN(AmdK8SmiOnIoTrapN);
    CPUM_ASSERT_WR_MSR_FN(AmdK8SmiOnIoTrapCtlSts);
    CPUM_ASSERT_WR_MSR_FN(AmdK8IntPendingMessage);
    CPUM_ASSERT_WR_MSR_FN(AmdK8SmiTriggerIoCycle);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hMmioCfgBaseAddr);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hTrapCtlMaybe);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hPStateControl);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hPStateStatus);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hPStateN);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hCofVidControl);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hCofVidStatus);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hCStateIoBaseAddr);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hCpuWatchdogTimer);
    CPUM_ASSERT_WR_MSR_FN(AmdK8SmmBase);
    CPUM_ASSERT_WR_MSR_FN(AmdK8SmmAddr);
    CPUM_ASSERT_WR_MSR_FN(AmdK8SmmMask);
    CPUM_ASSERT_WR_MSR_FN(AmdK8VmCr);
    CPUM_ASSERT_WR_MSR_FN(AmdK8IgnNe);
    CPUM_ASSERT_WR_MSR_FN(AmdK8SmmCtl);
    CPUM_ASSERT_WR_MSR_FN(AmdK8VmHSavePa);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hVmLockKey);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hSmmLockKey);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hLocalSmiStatus);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hOsVisWrkIdLength);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hOsVisWrkStatus);
    CPUM_ASSERT_WR_MSR_FN(AmdFam16hL2IPerfCtlN);
    CPUM_ASSERT_WR_MSR_FN(AmdFam16hL2IPerfCtrN);
    CPUM_ASSERT_WR_MSR_FN(AmdFam15hNorthbridgePerfCtlN);
    CPUM_ASSERT_WR_MSR_FN(AmdFam15hNorthbridgePerfCtrN);
    CPUM_ASSERT_WR_MSR_FN(AmdK7MicrocodeCtl);
    CPUM_ASSERT_WR_MSR_FN(AmdK7ClusterIdMaybe);
    CPUM_ASSERT_WR_MSR_FN(AmdK8CpuIdCtlStd07hEbax);
    CPUM_ASSERT_WR_MSR_FN(AmdK8CpuIdCtlStd06hEcx);
    CPUM_ASSERT_WR_MSR_FN(AmdK8CpuIdCtlStd01hEdcx);
    CPUM_ASSERT_WR_MSR_FN(AmdK8CpuIdCtlExt01hEdcx);
    CPUM_ASSERT_WR_MSR_FN(AmdK8PatchLoader);
    CPUM_ASSERT_WR_MSR_FN(AmdK7DebugStatusMaybe);
    CPUM_ASSERT_WR_MSR_FN(AmdK7BHTraceBaseMaybe);
    CPUM_ASSERT_WR_MSR_FN(AmdK7BHTracePtrMaybe);
    CPUM_ASSERT_WR_MSR_FN(AmdK7BHTraceLimitMaybe);
    CPUM_ASSERT_WR_MSR_FN(AmdK7HardwareDebugToolCfgMaybe);
    CPUM_ASSERT_WR_MSR_FN(AmdK7FastFlushCountMaybe);
    CPUM_ASSERT_WR_MSR_FN(AmdK7NodeId);
    CPUM_ASSERT_WR_MSR_FN(AmdK7DrXAddrMaskN);
    CPUM_ASSERT_WR_MSR_FN(AmdK7Dr0DataMatchMaybe);
    CPUM_ASSERT_WR_MSR_FN(AmdK7Dr0DataMaskMaybe);
    CPUM_ASSERT_WR_MSR_FN(AmdK7LoadStoreCfg);
    CPUM_ASSERT_WR_MSR_FN(AmdK7InstrCacheCfg);
    CPUM_ASSERT_WR_MSR_FN(AmdK7DataCacheCfg);
    CPUM_ASSERT_WR_MSR_FN(AmdK7BusUnitCfg);
    CPUM_ASSERT_WR_MSR_FN(AmdK7DebugCtl2Maybe);
    CPUM_ASSERT_WR_MSR_FN(AmdFam15hFpuCfg);
    CPUM_ASSERT_WR_MSR_FN(AmdFam15hDecoderCfg);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hBusUnitCfg2);
    CPUM_ASSERT_WR_MSR_FN(AmdFam15hCombUnitCfg);
    CPUM_ASSERT_WR_MSR_FN(AmdFam15hCombUnitCfg2);
    CPUM_ASSERT_WR_MSR_FN(AmdFam15hCombUnitCfg3);
    CPUM_ASSERT_WR_MSR_FN(AmdFam15hExecUnitCfg);
    CPUM_ASSERT_WR_MSR_FN(AmdFam15hLoadStoreCfg2);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hIbsFetchCtl);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hIbsFetchLinAddr);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hIbsFetchPhysAddr);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hIbsOpExecCtl);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hIbsOpRip);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hIbsOpData);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hIbsOpData2);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hIbsOpData3);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hIbsDcLinAddr);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hIbsDcPhysAddr);
    CPUM_ASSERT_WR_MSR_FN(AmdFam10hIbsCtl);
    CPUM_ASSERT_WR_MSR_FN(AmdFam14hIbsBrTarget);

    CPUM_ASSERT_WR_MSR_FN(Gim);

    return VINF_SUCCESS;
}
#endif /* VBOX_STRICT && IN_RING3 */


/**
 * Gets the scalable bus frequency.
 *
 * The bus frequency is used as a base in several MSRs that gives the CPU and
 * other frequency ratios.
 *
 * @returns Scalable bus frequency in Hz. Will not return CPUM_SBUSFREQ_UNKNOWN.
 * @param   pVM                 The cross context VM structure.
 */
VMMDECL(uint64_t) CPUMGetGuestScalableBusFrequency(PVM pVM)
{
    uint64_t uFreq = pVM->cpum.s.GuestInfo.uScalableBusFreq;
    if (uFreq == CPUM_SBUSFREQ_UNKNOWN)
        uFreq = CPUM_SBUSFREQ_100MHZ;
    return uFreq;
}


/**
 * Sets the guest EFER MSR without performing any additional checks.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uOldEfer    The previous EFER MSR value.
 * @param   uValidEfer  The new, validated EFER MSR value.
 *
 * @remarks One would normally call CPUMIsGuestEferMsrWriteValid() before calling
 *          this function to change the EFER in order to perform an EFER transition.
 */
VMMDECL(void) CPUMSetGuestEferMsrNoChecks(PVMCPUCC pVCpu, uint64_t uOldEfer, uint64_t uValidEfer)
{
    pVCpu->cpum.s.Guest.msrEFER = uValidEfer;

    /* AMD64 Architecture Programmer's Manual: 15.15 TLB Control; flush the TLB
       if MSR_K6_EFER_NXE, MSR_K6_EFER_LME or MSR_K6_EFER_LMA are changed. */
    if (   (uOldEfer                    & (MSR_K6_EFER_NXE | MSR_K6_EFER_LME | MSR_K6_EFER_LMA))
        != (pVCpu->cpum.s.Guest.msrEFER & (MSR_K6_EFER_NXE | MSR_K6_EFER_LME | MSR_K6_EFER_LMA)))
    {
        /// @todo PGMFlushTLB(pVCpu, cr3, true /*fGlobal*/);
        HMFlushTlb(pVCpu);

        /* Notify PGM about NXE changes. */
        if (   (uOldEfer                    & MSR_K6_EFER_NXE)
            != (pVCpu->cpum.s.Guest.msrEFER & MSR_K6_EFER_NXE))
            PGMNotifyNxeChanged(pVCpu, !(uOldEfer & MSR_K6_EFER_NXE));
    }
}


/**
 * Checks if a guest PAT MSR write is valid.
 *
 * @returns @c true if the PAT bit combination is valid, @c false otherwise.
 * @param   uValue      The PAT MSR value.
 */
VMMDECL(bool) CPUMIsPatMsrValid(uint64_t uValue)
{
    for (uint32_t cShift = 0; cShift < 63; cShift += 8)
    {
        /* Check all eight bits because the top 5 bits of each byte are reserved. */
        uint8_t uType = (uint8_t)(uValue >> cShift);
        if ((uType >= 8) || (uType == 2) || (uType == 3))
        {
            Log(("CPUM: Invalid PAT type at %u:%u in IA32_PAT: %#llx (%#llx)\n", cShift + 7, cShift, uValue, uType));
            return false;
        }
    }
    return true;
}


/**
 * Validates an EFER MSR write and provides the new, validated EFER MSR.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   uCr0            The CR0 of the CPU corresponding to the EFER MSR.
 * @param   uOldEfer        Value of the previous EFER MSR on the CPU if any.
 * @param   uNewEfer        The new EFER MSR value being written.
 * @param   puValidEfer     Where to store the validated EFER (only updated if
 *                          this function returns VINF_SUCCESS).
 */
VMMDECL(int) CPUMIsGuestEferMsrWriteValid(PVM pVM, uint64_t uCr0, uint64_t uOldEfer, uint64_t uNewEfer, uint64_t *puValidEfer)
{
    /* #GP(0) If anything outside the allowed bits is set. */
    uint64_t fMask = CPUMGetGuestEferMsrValidMask(pVM);
    if (uNewEfer & ~fMask)
    {
        Log(("CPUM: Settings disallowed EFER bit. uNewEfer=%#RX64 fAllowed=%#RX64 -> #GP(0)\n", uNewEfer, fMask));
        return VERR_CPUM_RAISE_GP_0;
    }

    /* Check for illegal MSR_K6_EFER_LME transitions: not allowed to change LME if
       paging is enabled. (AMD Arch. Programmer's Manual Volume 2: Table 14-5) */
    if (   (uOldEfer & MSR_K6_EFER_LME) != (uNewEfer & MSR_K6_EFER_LME)
        && (uCr0 & X86_CR0_PG))
    {
        Log(("CPUM: Illegal MSR_K6_EFER_LME change: paging is enabled!!\n"));
        return VERR_CPUM_RAISE_GP_0;
    }

    /* There are a few more: e.g. MSR_K6_EFER_LMSLE. */
    AssertMsg(!(uNewEfer & ~(  MSR_K6_EFER_NXE
                             | MSR_K6_EFER_LME
                             | MSR_K6_EFER_LMA /* ignored anyway */
                             | MSR_K6_EFER_SCE
                             | MSR_K6_EFER_FFXSR
                             | MSR_K6_EFER_SVME)),
              ("Unexpected value %#RX64\n", uNewEfer));

    /* Ignore EFER.LMA, it's updated when setting CR0. */
    fMask &= ~MSR_K6_EFER_LMA;

    *puValidEfer = (uOldEfer & ~fMask) | (uNewEfer & fMask);
    return VINF_SUCCESS;
}


/**
 * Gets the mask of valid EFER bits depending on supported guest-CPU features.
 *
 * @returns Mask of valid EFER bits.
 * @param   pVM     The cross context VM structure.
 *
 * @remarks EFER.LMA is included as part of the valid mask. It's not invalid but
 *          rather a read-only bit.
 */
VMMDECL(uint64_t) CPUMGetGuestEferMsrValidMask(PVM pVM)
{
    uint32_t const  fExtFeatures = pVM->cpum.s.aGuestCpuIdPatmExt[0].uEax >= 0x80000001
                                 ? pVM->cpum.s.aGuestCpuIdPatmExt[1].uEdx
                                 : 0;
    uint64_t        fMask        = 0;
    uint64_t const  fIgnoreMask  = MSR_K6_EFER_LMA;

    /* Filter out those bits the guest is allowed to change. (e.g. LMA is read-only) */
    if (fExtFeatures & X86_CPUID_EXT_FEATURE_EDX_NX)
        fMask |= MSR_K6_EFER_NXE;
    if (fExtFeatures & X86_CPUID_EXT_FEATURE_EDX_LONG_MODE)
        fMask |= MSR_K6_EFER_LME;
    if (fExtFeatures & X86_CPUID_EXT_FEATURE_EDX_SYSCALL)
        fMask |= MSR_K6_EFER_SCE;
    if (fExtFeatures & X86_CPUID_AMD_FEATURE_EDX_FFXSR)
        fMask |= MSR_K6_EFER_FFXSR;
    if (pVM->cpum.s.GuestFeatures.fSvm)
        fMask |= MSR_K6_EFER_SVME;

    return (fIgnoreMask | fMask);
}


/**
 * Fast way for HM to access the MSR_K8_TSC_AUX register.
 *
 * @returns The register value.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(uint64_t) CPUMGetGuestTscAux(PVMCPUCC pVCpu)
{
    Assert(!(pVCpu->cpum.s.Guest.fExtrn & CPUMCTX_EXTRN_TSC_AUX));
    return pVCpu->cpum.s.GuestMsrs.msr.TscAux;
}


/**
 * Fast way for HM to access the MSR_K8_TSC_AUX register.
 *
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   uValue  The new value.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(void) CPUMSetGuestTscAux(PVMCPUCC pVCpu, uint64_t uValue)
{
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_TSC_AUX;
    pVCpu->cpum.s.GuestMsrs.msr.TscAux = uValue;
}


/**
 * Fast way for HM to access the IA32_SPEC_CTRL register.
 *
 * @returns The register value.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(uint64_t) CPUMGetGuestSpecCtrl(PVMCPUCC pVCpu)
{
    return pVCpu->cpum.s.GuestMsrs.msr.SpecCtrl;
}


/**
 * Fast way for HM to access the IA32_SPEC_CTRL register.
 *
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   uValue  The new value.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(void) CPUMSetGuestSpecCtrl(PVMCPUCC pVCpu, uint64_t uValue)
{
    pVCpu->cpum.s.GuestMsrs.msr.SpecCtrl = uValue;
}

