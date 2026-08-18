/* $Id: APICR3Nem-linux-x86.cpp 113859 2026-04-14 11:43:54Z alexander.eichner@oracle.com $ */
/** @file
 * APIC - Advanced Programmable Interrupt Controller - NEM KVM backend.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <linux/kvm.h>

#include <iprt/mem.h>

#include "APICKvmInternal.h"
#include <VBox/log.h>
#include <VBox/msi.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/vmcpuset.h>

#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The number of IO-APIC pins generating interrupts. */
#define IOAPIC_NUM_PINS 24

#ifndef KVM_CAP_X86_APIC_BUS_CYCLES_NS
# define KVM_CAP_X86_APIC_BUS_CYCLES_NS 237
#endif

#ifdef VBOX_WITH_STATISTICS
# define X2APIC_MSRRANGE(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_Ia32X2ApicN, kCpumMsrWrFn_Ia32X2ApicN, 0, 0, 0, 0, 0, a_szName, { 0 }, { 0 }, { 0 }, { 0 } }
# define X2APIC_MSRRANGE_INVALID(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_WriteOnly, kCpumMsrWrFn_ReadOnly, 0, 0, 0, 0, UINT64_MAX /*fWrGpMask*/, a_szName, { 0 }, { 0 }, { 0 }, { 0 } }
#else
# define X2APIC_MSRRANGE(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_Ia32X2ApicN, kCpumMsrWrFn_Ia32X2ApicN, 0, 0, 0, 0, 0, a_szName }
# define X2APIC_MSRRANGE_INVALID(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_WriteOnly, kCpumMsrWrFn_ReadOnly, 0, 0, 0, 0, UINT64_MAX /*fWrGpMask*/, a_szName }
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * KVM APIC PDM instance data (per-VM).
 */
typedef struct KVMAPICDEV
{
    /** Pointer to the PDM device instance. */
    PPDMDEVINSR3            pDevIns;
} KVMAPICDEV;
/** Pointer to a KVM APIC PDM instance data. */
typedef KVMAPICDEV *PKVMAPICDEV;
/** Pointer to a const KVM APIC PDM instance data. */
typedef KVMAPICDEV const *PCKVMAPICDEV;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * MSR range supported by the x2APIC.
 * See Intel spec. 10.12.2 "x2APIC Register Availability".
 */
static CPUMMSRRANGE const g_MsrRange_x2Apic = X2APIC_MSRRANGE(MSR_IA32_X2APIC_START, MSR_IA32_X2APIC_END, "x2APIC range");
static CPUMMSRRANGE const g_MsrRange_x2Apic_Invalid = X2APIC_MSRRANGE_INVALID(MSR_IA32_X2APIC_START, MSR_IA32_X2APIC_END, "x2APIC range invalid");
#undef X2APIC_MSRRANGE
#undef X2APIC_MSRRANGE_GP


/*
 * Instantiate the APIC all-context common code.
 */
#define VMM_APIC_TEMPLATE_ALL_COMMON
#define VMM_APIC_TEMPLATE_R3_COMMON
#include "../VMMAll/target-x86/APICAllCommon-x86.cpp.h"
#undef VMM_APIC_TEMPLATE_ALL_COMMON
#undef VMM_APIC_TEMPLATE_R3_COMMON


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnIsEnabled}
 */
static DECLCALLBACK(bool) apicR3KvmIsEnabled(PCVMCPUCC pVCpu)
{
    /*
     * We should never end up here as this is called only from the VMX and SVM
     * code in R0 which we don't run if this is active. However, we still call
     * this statically (within this file).
     */
    PCKVMAPICCPU pKvmApicCpu = VMCPU_TO_KVMAPICCPU(pVCpu);
    return RT_BOOL(pKvmApicCpu->uApicBaseMsr & MSR_IA32_APICBASE_EN);
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnInitIpi}
 */
static DECLCALLBACK(void) apicR3KvmInitIpi(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    apicCommonInitIpi(pVCpu);
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetBaseMsr}
 */
static DECLCALLBACK(int) apicR3KvmSetBaseMsr(PVMCPUCC pVCpu, uint64_t u64BaseMsr)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    PKVMAPICCPU pKvmApicCpu = VMCPU_TO_KVMAPICCPU(pVCpu);
    pKvmApicCpu->uApicBaseMsr = u64BaseMsr;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetBaseMsrNoCheck}
 */
static DECLCALLBACK(uint64_t) apicR3KvmGetBaseMsrNoCheck(PCVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    PCKVMAPICCPU pKvmApicCpu = VMCPU_TO_KVMAPICCPU(pVCpu);
    return pKvmApicCpu->uApicBaseMsr;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetBaseMsr}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3KvmGetBaseMsr(PVMCPUCC pVCpu, uint64_t *pu64Value)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    PCKVMAPIC pKvmApic = VM_TO_KVMAPIC(pVCpu->CTX_SUFF(pVM));
    if (pKvmApic->enmMaxMode != PDMAPICMODE_NONE)
    {
        *pu64Value = apicR3KvmGetBaseMsrNoCheck(pVCpu);
        return VINF_SUCCESS;
    }
    return VERR_CPUM_RAISE_GP_0;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnReadRaw32}
 */
static DECLCALLBACK(uint32_t) apicR3KvmReadRaw32(PCVMCPUCC pVCpu, uint16_t offReg)
{
    RT_NOREF(pVCpu, offReg);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return 0;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnReadMsr}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3KvmReadMsr(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t *pu64Value)
{
    /*
     * Validate.
     */
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(u32Reg >= MSR_IA32_X2APIC_ID && u32Reg <= MSR_IA32_X2APIC_SELF_IPI);
    Assert(pu64Value);

    RT_NOREF(pVCpu, u32Reg, pu64Value);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnWriteMsr}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3KvmWriteMsr(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t u64Value)
{
    /*
     * Validate.
     */
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(u32Reg >= MSR_IA32_X2APIC_ID && u32Reg <= MSR_IA32_X2APIC_SELF_IPI);

    RT_NOREF(pVCpu, u32Reg, u64Value);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetTpr}
 */
static DECLCALLBACK(int) apicR3KvmSetTpr(PVMCPUCC pVCpu, uint8_t u8Tpr, bool fForceX2ApicBehaviour)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    RT_NOREF(fForceX2ApicBehaviour);
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    pXApicPage->tpr.u8Tpr = u8Tpr;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetTpr}
 */
static DECLCALLBACK(int) apicR3KvmGetTpr(PCVMCPUCC pVCpu, uint8_t *pu8Tpr, bool *pfPending, uint8_t *pu8PendingIntr)
{
    VMCPU_ASSERT_EMT(pVCpu);

    RT_NOREF(pfPending, pu8PendingIntr);
    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    *pu8Tpr = pXApicPage->tpr.u8Tpr;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetIcrNoCheck}
 */
static DECLCALLBACK(uint64_t) apicR3KvmGetIcrNoCheck(PVMCPUCC pVCpu)
{
    RT_NOREF(pVCpu);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return 0;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetIcr}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3KvmSetIcr(PVMCPUCC pVCpu, uint64_t u64Icr, int rcRZ)
{
    VMCPU_ASSERT_EMT(pVCpu);
    RT_NOREF(pVCpu, rcRZ, u64Icr);

    /** @todo */
    AssertMsgFailed(("Unexpected ICR write failed (%#RX64) in CPU %u\n", u64Icr, pVCpu->idCpu));
    return VERR_APIC_WRITE_INVALID;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetTimerFreq}
 */
static DECLCALLBACK(int) apicR3KvmGetTimerFreq(PVMCC pVM, uint64_t *pu64Value)
{
    /*
     * Validate.
     */
    Assert(pVM);
    AssertPtrReturn(pu64Value, VERR_INVALID_PARAMETER);

    PKVMAPIC pKvmApic = VM_TO_KVMAPIC(pVM);

    int rcLnx = ioctl(pKvmApic->iFdVm, KVM_CHECK_EXTENSION, KVM_CAP_X86_APIC_BUS_CYCLES_NS);
    if (rcLnx <= 0)
    {
        LogRel(("APIC/KVM: Querying KVM_CAP_X86_APIC_BUS_CYCLES_NS failed, assuming default of 1GHz"));
        *pu64Value = 1000000000;
    }
    else
    {
        Assert(rcLnx > 0);
        *pu64Value = RT_NS_1SEC / (uint64_t)rcLnx; /* The value is the period, the default should be 1ns translating to 1GHz. */
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetLocalInterrupt}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3KvmSetLocalInterrupt(PVMCPUCC pVCpu, uint8_t u8Pin, uint8_t u8Level, int rcRZ)
{
    AssertReturn(u8Pin <= 1, VERR_INVALID_PARAMETER);
    AssertReturn(u8Level <= 1, VERR_INVALID_PARAMETER);
    RT_NOREF(rcRZ);

    /* The rest is handled in the NEM backend. */
    if (u8Level)
        VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_PIC);
    else
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_PIC);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetInterrupt}
 */
static DECLCALLBACK(int) apicR3KvmGetInterrupt(PVMCPUCC pVCpu, uint8_t *pu8Vector, uint32_t *puSrcTag)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pu8Vector);

    RT_NOREF(pVCpu, pu8Vector, puSrcTag);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return VERR_APIC_INTR_NOT_PENDING;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnPostInterrupt}
 */
static DECLCALLBACK(bool) apicR3KvmPostInterrupt(PVMCPUCC pVCpu, uint8_t uVector, XAPICTRIGGERMODE enmTriggerMode, bool fAutoEoi,
                                                 uint32_t uSrcTag)
{
    Assert(pVCpu);
    Assert(uVector > XAPIC_ILLEGAL_VECTOR_END);
    RT_NOREF(fAutoEoi);

    RT_NOREF(pVCpu, uVector, enmTriggerMode, fAutoEoi, uSrcTag);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return false;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnUpdatePendingInterrupts}
 */
static DECLCALLBACK(void) apicR3KvmUpdatePendingInterrupts(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    RT_NOREF(pVCpu);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnBusDeliver}
 */
static DECLCALLBACK(int) apicR3KvmBusDeliver(PVMCC pVM, uint8_t uDest, uint8_t uDestMode, uint8_t uDeliveryMode, uint8_t uVector,
                                             uint8_t uPolarity, uint8_t uTriggerMode, uint8_t uIoApicPin, uint32_t uSrcTag)
{
    RT_NOREF(uPolarity, uSrcTag);

    Log2(("APIC/KVM: apicR3KvmBusDeliver: uDest=%u enmDestMode=%s enmTriggerMode=%s enmDeliveryMode=%s uVector=%#x uSrcTag=%#x\n",
          uDest, apicCommonGetDestModeName((XAPICDESTMODE)uDestMode), apicCommonGetTriggerModeName((XAPICTRIGGERMODE)uTriggerMode),
          apicCommonGetDeliveryModeName((XAPICDELIVERYMODE)uDeliveryMode), uVector, uSrcTag));

    PKVMAPIC pKvmApic = VM_TO_KVMAPIC(pVM);

    struct kvm_msi KvmMsi; RT_ZERO(KvmMsi);
    KvmMsi.address_lo =   VBOX_MSI_ADDR_BASE
                        | (uint32_t)uDest << 12
                        | (uint32_t)uDestMode << 2;
    KvmMsi.data       =   (uint32_t)uTriggerMode << 15
                        | (uint32_t)uDeliveryMode << 8
                        | (uint32_t)uVector;

    if (uIoApicPin < IOAPIC_NUM_PINS)
    {
        /*
         * Update the installed MSI routes if one of the parameters of the currently installed routes
         * change for an IO-APIC pin based interrupt (guest configures a different vector for example).
         * Otherwise KVM will not generate an IO-APIC EOI broadcast VM exit breaking interrupt delivery to
         * the guest.
         */
        if (   pKvmApic->pMsiRoutes->entries[uIoApicPin].u.msi.address_lo != KvmMsi.address_lo
            || pKvmApic->pMsiRoutes->entries[uIoApicPin].u.msi.data       != KvmMsi.data)
        {
            pKvmApic->pMsiRoutes->entries[uIoApicPin].u.msi.address_lo = KvmMsi.address_lo;
            pKvmApic->pMsiRoutes->entries[uIoApicPin].u.msi.data       = KvmMsi.data;

            int rcLnx = ioctl(pKvmApic->iFdVm, KVM_SET_GSI_ROUTING, pKvmApic->pMsiRoutes);
            if (rcLnx == -1)
            {
                int rc = RTErrConvertFromErrno(errno);
                AssertMsgFailed(("APIC/KVM: Setting IO-APIC routes failed: %Rrc", rc));
                LogRelMax(10, ("APIC/KVM: Setting IO-APIC routes failed: %Rrc", rc));
                return rc;
            }
        }
    }

    //KvmMsi.address_lo |= RT_BIT_32(3);
    KvmMsi.data       |= RT_BIT_32(14);
    int rcLnx = ioctl(pKvmApic->iFdVm, KVM_SIGNAL_MSI, &KvmMsi);
    if (rcLnx == -1)
    {
        int rc = RTErrConvertFromErrno(errno);
        AssertMsgFailed(("APIC/KVM: Delivering interrupt failed: %Rrc", rc));
        LogRelMax(10, ("APIC/KVM: Delivering interrupt failed: %Rrc", rc));
        return VERR_APIC_INTR_DISCARDED;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetEoi}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3KvmSetEoi(PVMCPUCC pVCpu, uint32_t uEoi, bool fForceX2ApicBehaviour)
{
    VMCPU_ASSERT_EMT(pVCpu);

    RT_NOREF(pVCpu, uEoi, fForceX2ApicBehaviour);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetHvCompatMode}
 */
static DECLCALLBACK(int) apicR3KvmSetHvCompatMode(PVM pVM, bool fHyperVCompatMode)
{
    RT_NOREF(pVM, fHyperVCompatMode);
    AssertReleaseFailed();
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnImportState}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3KvmImportState(PVMCPUCC pVCpu)
{
    AssertCompile(KVM_APIC_REG_SIZE == 1024);

    PKVMAPICCPU pKvmApicCpu  = VMCPU_TO_KVMAPICCPU(pVCpu);
    PXAPICPAGE pKvmApicPage = (PXAPICPAGE)pVCpu->apic.s.pvKvmPageR3;

    int rcLnx = ioctl(pKvmApicCpu->iFdVCpu, KVM_GET_LAPIC, pKvmApicPage);
    if (RT_UNLIKELY(rcLnx == -1))
    {
        int rc = RTErrConvertFromErrno(errno);
        AssertMsgFailed(("APIC/KVM: Calling KVM_GET_LAPIC failed: %Rrc", rc));
        LogRelMax(10, ("APIC/KVM: Calling KVM_GET_LAPIC failed failed: %Rrc", rc));
        return rc;
    }

    memcpy(pVCpu->apic.s.pvApicPageR3, pKvmApicPage, sizeof(XAPICPAGE));
    return VINF_SUCCESS;
}


/**
* @interface_method_impl{PDMAPICBACKEND,pfnExportState}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3KvmExportState(PVMCPUCC pVCpu)
{
#if 0
    PKVMAPICCPU pKvmApicCpu = VMCPU_TO_KVMAPICCPU(pVCpu);
    PXAPICPAGE pKvmApicPage = (PXAPICPAGE)pVCpu->apic.s.pvKvmPageR3;

    memcpy(pKvmApicPage, pVCpu->apic.s.pvApicPageR3, sizeof(XAPICPAGE));
    int rcLnx = ioctl(pKvmApicCpu->iFdVCpu, KVM_SET_LAPIC, pKvmApicPage);
    if (RT_UNLIKELY(rcLnx == -1))
    {
        int rc = RTErrConvertFromErrno(errno);
        AssertMsgFailed(("APIC/KVM: Calling KVM_SET_LAPIC failed: %Rrc", rc));
        LogRelMax(10, ("APIC/KVM: Calling KVM_SET_LAPIC failed failed: %Rrc", rc));
        return rc;
    }
#else
    RT_NOREF(pVCpu);
#endif

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnUpdateStateAfterWrite}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3KvmUpdateStateAfterWrite(PVMCPUCC pVCpu, uint16_t offApicReg)
{
    RT_NOREF(pVCpu, offApicReg);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetEoiFast}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3KvmSetEoiFast(PVMCPUCC pVCpu, uint8_t uVector)
{
    RT_NOREF(pVCpu, uVector);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return VERR_NOT_SUPPORTED;
}


/**
 * Dumps basic APIC state.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) apicR3KvmInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];
    apicR3CommonDbgInfo(pVCpu, pHlp, pVCpu->apic.s.uApicBaseMsr);
}


/**
 * Dumps APIC Local Vector Table (LVT) information.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) apicR3KvmInfoLvt(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];
    apicR3CommonDbgInfoLvt(pVCpu, pHlp);
}


/**
 * Dumps the APIC timer information.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) apicR3KvmInfoTimer(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];
    apicR3CommonDbgInfoLvtTimer(pVCpu, pHlp);
}


/**
 * Worker for saving per-VM APIC data.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pVM     The cross context VM structure.
 * @param   pSSM    The SSM handle.
 *
 * @sa apicR3SaveVMData, apicR3HvSaveVMData
 */
static int apicR3KvmSaveVMData(PPDMDEVINS pDevIns, PVM pVM, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp     = pDevIns->pHlpR3;
    PKVMAPIC      pKvmApic = VM_TO_KVMAPIC(pVM);
    pHlp->pfnSSMPutU32(pSSM,  pVM->cCpus);
    pHlp->pfnSSMPutBool(pSSM, pKvmApic->fIoApicPresent);
    return pHlp->pfnSSMPutU32(pSSM, pKvmApic->enmMaxMode);
}


/**
 * Worker for loading per-VM APIC data.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pVM     The cross context VM structure.
 * @param   pSSM    The SSM handle.
 *
 * @sa apicR3LoadVMData, apicR3HvLoadVMData
 */
static int apicR3KvmLoadVMData(PPDMDEVINS pDevIns, PVM pVM, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp     = pDevIns->pHlpR3;
    PKVMAPIC      pKvmApic = VM_TO_KVMAPIC(pVM);

    /* Load and verify number of CPUs. */
    uint32_t cCpus;
    int rc = pHlp->pfnSSMGetU32(pSSM, &cCpus);
    AssertRCReturn(rc, rc);
    if (cCpus != pVM->cCpus)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - cCpus: saved=%u config=%u"), cCpus, pVM->cCpus);

    /* Load and verify I/O APIC presence. */
    bool fIoApicPresent;
    rc = pHlp->pfnSSMGetBool(pSSM, &fIoApicPresent);
    AssertRCReturn(rc, rc);
    if (fIoApicPresent != pKvmApic->fIoApicPresent)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - fIoApicPresent: saved=%RTbool config=%RTbool"),
                                       fIoApicPresent, pKvmApic->fIoApicPresent);

    /* Load and verify configured max APIC mode. */
    uint32_t uSavedMaxApicMode;
    rc = pHlp->pfnSSMGetU32(pSSM, &uSavedMaxApicMode);
    AssertRCReturn(rc, rc);
    if (uSavedMaxApicMode != (uint32_t)pKvmApic->enmMaxMode)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - uApicMode: saved=%u config=%u"),
                                       uSavedMaxApicMode, pKvmApic->enmMaxMode);
    return VINF_SUCCESS;
}


#if 0
/**
 * Worker for loading per-VCPU APIC data for legacy (old) saved-states.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pSSM        The SSM handle.
 * @param   uVersion    Data layout version.
 */
static int apicR3LoadLegacyVCpuData(PPDMDEVINS pDevIns, PVMCPU pVCpu, PSSMHANDLE pSSM, uint32_t uVersion)
{
    AssertReturn(uVersion <= APIC_SAVED_STATE_VERSION_VBOX_50, VERR_NOT_SUPPORTED);

    PCPDMDEVHLPR3   pHlp       = pDevIns->pHlpR3;
    PAPICCPU        pApicCpu   = VMCPU_TO_APICCPU(pVCpu);
    PXAPICPAGE      pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);

    uint32_t uApicBaseLo;
    int rc = pHlp->pfnSSMGetU32(pSSM, &uApicBaseLo);
    AssertRCReturn(rc, rc);
    pApicCpu->uApicBaseMsr = uApicBaseLo;
    Log2(("APIC%u: apicR3LoadLegacyVCpuData: uApicBaseMsr=%#RX64\n", pVCpu->idCpu, pApicCpu->uApicBaseMsr));

    switch (uVersion)
    {
        case APIC_SAVED_STATE_VERSION_VBOX_50:
        case APIC_SAVED_STATE_VERSION_VBOX_30:
        {
            uint32_t uApicId, uPhysApicId, uArbId;
            pHlp->pfnSSMGetU32(pSSM, &uApicId);      pXApicPage->id.u8ApicId = uApicId;
            pHlp->pfnSSMGetU32(pSSM, &uPhysApicId);  NOREF(uPhysApicId); /* PhysId == pVCpu->idCpu */
            pHlp->pfnSSMGetU32(pSSM, &uArbId);       NOREF(uArbId);      /* ArbID is & was unused. */
            break;
        }

        case APIC_SAVED_STATE_VERSION_ANCIENT:
        {
            uint8_t uPhysApicId;
            pHlp->pfnSSMGetU8(pSSM, &pXApicPage->id.u8ApicId);
            pHlp->pfnSSMGetU8(pSSM, &uPhysApicId);   NOREF(uPhysApicId); /* PhysId == pVCpu->idCpu */
            break;
        }

        default:
            return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    uint32_t u32Tpr;
    pHlp->pfnSSMGetU32(pSSM, &u32Tpr);
    pXApicPage->tpr.u8Tpr = u32Tpr & XAPIC_TPR_VALID;

    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->svr.all.u32Svr);
    pHlp->pfnSSMGetU8(pSSM,  &pXApicPage->ldr.u.u8LogicalApicId);

    uint8_t uDfr;
    pHlp->pfnSSMGetU8(pSSM,  &uDfr);
    pXApicPage->dfr.u.u4Model = uDfr >> 4;

    AssertCompile(RT_ELEMENTS(pXApicPage->isr.u) == 8);
    AssertCompile(RT_ELEMENTS(pXApicPage->tmr.u) == 8);
    AssertCompile(RT_ELEMENTS(pXApicPage->irr.u) == 8);
    for (size_t i = 0; i < 8; i++)
    {
        pHlp->pfnSSMGetU32(pSSM, &pXApicPage->isr.u[i].u32Reg);
        pHlp->pfnSSMGetU32(pSSM, &pXApicPage->tmr.u[i].u32Reg);
        pHlp->pfnSSMGetU32(pSSM, &pXApicPage->irr.u[i].u32Reg);
    }

    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_timer.all.u32LvtTimer);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_thermal.all.u32LvtThermal);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_perf.all.u32LvtPerf);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_lint0.all.u32LvtLint0);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_lint1.all.u32LvtLint1);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_error.all.u32LvtError);

    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->esr.all.u32Errors);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->icr_lo.all.u32IcrLo);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->icr_hi.all.u32IcrHi);

    uint32_t u32TimerShift;
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->timer_dcr.all.u32DivideValue);
    pHlp->pfnSSMGetU32(pSSM, &u32TimerShift);
    /*
     * Old implementation may have left the timer shift uninitialized until
     * the timer configuration register was written. Unfortunately zero is
     * also a valid timer shift value, so we're just going to ignore it
     * completely. The shift count can always be derived from the DCR.
     * See @bugref{8245#c98}.
     */
    uint8_t const uTimerShift = apicCommonGetTimerShift(pXApicPage);

    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->timer_icr.u32InitialCount);
    pHlp->pfnSSMGetU64(pSSM, &pApicCpu->u64TimerInitial);
    uint64_t uNextTS;
    rc = pHlp->pfnSSMGetU64(pSSM, &uNextTS);       AssertRCReturn(rc, rc);
    if (uNextTS >= pApicCpu->u64TimerInitial + ((pXApicPage->timer_icr.u32InitialCount + 1) << uTimerShift))
        pXApicPage->timer_ccr.u32CurrentCount = pXApicPage->timer_icr.u32InitialCount;

    rc = PDMDevHlpTimerLoad(pDevIns, pApicCpu->hTimer, pSSM);
    AssertRCReturn(rc, rc);
    Assert(pApicCpu->uHintedTimerInitialCount == 0);
    Assert(pApicCpu->uHintedTimerShift == 0);
    if (PDMDevHlpTimerIsActive(pDevIns, pApicCpu->hTimer))
    {
        uint32_t const uInitialCount = pXApicPage->timer_icr.u32InitialCount;
        apicHintTimerFreq(pDevIns, pApicCpu, uInitialCount, uTimerShift);
    }

    return rc;
}
#endif


/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
static DECLCALLBACK(int) apicR3KvmSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVM             pVM  = PDMDevHlpGetVM(pDevIns);
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;

    AssertReturn(pVM, VERR_INVALID_VM_HANDLE);

    LogFlow(("APIC: apicR3SaveExec\n"));

    /* Save per-VM data. */
    int rc = apicR3KvmSaveVMData(pDevIns, pVM, pSSM);
    AssertRCReturn(rc, rc);

    /* Save per-VCPU data.*/
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU       pVCpu       = pVM->apCpusR3[idCpu];
        PCKVMAPICCPU pKvmApicCpu = VMCPU_TO_KVMAPICCPU(pVCpu);

        /* Ensure the state is up to data. */
        VBOXSTRICTRC rcStrict = apicR3KvmImportState(pVCpu);
        AssertRCReturn(VBOXSTRICTRC_VAL(rcStrict), VBOXSTRICTRC_VAL(rcStrict));

        /* Save the auxiliary data. */
        pHlp->pfnSSMPutU64(pSSM, pKvmApicCpu->uApicBaseMsr);
        pHlp->pfnSSMPutU32(pSSM, pKvmApicCpu->uEsrInternal);

        /* Save the APIC page. */
        if (XAPIC_IN_X2APIC_MODE(pKvmApicCpu->uApicBaseMsr))
            pHlp->pfnSSMPutStruct(pSSM, (const void *)pKvmApicCpu->pvApicPageR3, &g_aX2ApicPageFields[0]);
        else
            pHlp->pfnSSMPutStruct(pSSM, (const void *)pKvmApicCpu->pvApicPageR3, &g_aXApicPageFields[0]);

        /*
         * Make sure the timer state is compatible with our APIC emulation in order to be able to
         * switch between those two.
         */
        PCXAPICPAGE    pXApicPage   = VMCPU_TO_CXAPICPAGE(pVCpu);
        uint8_t  const uTimerShift  = apicCommonGetTimerShift(pXApicPage);
        uint64_t const cTicksToNext = (uint64_t)pXApicPage->timer_icr.u32InitialCount << uTimerShift;
        bool     const fActive      =    pXApicPage->timer_icr.u32InitialCount != 0
                                      && pXApicPage->lvt_timer.u.u2TimerMode != XAPIC_TIMER_MODE_TSC_DEADLINE;
        pHlp->pfnTimerSaveFakeForSsm(pDevIns, pSSM, TMCLOCK_VIRTUAL_SYNC, fActive, cTicksToNext, true /*fSaveNowTsBeforeTimer*/);

        /*
         * Save the LINT0, LINT1 interrupt line states, these are always false in our APIC emulation
         * currently and would need changing as soon as these are actually used.
         */
        pHlp->pfnSSMPutBool(pSSM, false);
        pHlp->pfnSSMPutBool(pSSM, false);

    }

    return rc;
}


/**
 * @copydoc FNSSMDEVLOADEXEC
 */
static DECLCALLBACK(int) apicR3KvmLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PVM             pVM  = PDMDevHlpGetVM(pDevIns);
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;

    AssertReturn(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(uPass == SSM_PASS_FINAL, VERR_WRONG_ORDER);

    LogFlow(("APIC: apicR3KvmLoadExec: uVersion=%u uPass=%#x\n", uVersion, uPass));

    /* Weed out invalid versions. */
    if (   uVersion != APIC_SAVED_STATE_VERSION
        && uVersion != APIC_SAVED_STATE_VERSION_VBOX_51_BETA2
        && uVersion != APIC_SAVED_STATE_VERSION_VBOX_50
        && uVersion != APIC_SAVED_STATE_VERSION_VBOX_30
        && uVersion != APIC_SAVED_STATE_VERSION_ANCIENT)
    {
        LogRel(("APIC: apicR3KvmLoadExec: Invalid/unrecognized saved-state version %u (%#x)\n", uVersion, uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    int rc = VINF_SUCCESS;
    if (uVersion > APIC_SAVED_STATE_VERSION_VBOX_30)
    {
        rc = apicR3KvmLoadVMData(pDevIns, pVM, pSSM);
        AssertRCReturn(rc, rc);

        if (uVersion == APIC_SAVED_STATE_VERSION)
        { /* Load any new additional per-VM data. */ }
    }

    /*
     * Restore per CPU state.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU      pVCpu       = pVM->apCpusR3[idCpu];
        PKVMAPICCPU pKvmApicCpu = VMCPU_TO_KVMAPICCPU(pVCpu);

        if (uVersion > APIC_SAVED_STATE_VERSION_VBOX_50)
        {
            /* Load the auxiliary data. */
            pHlp->pfnSSMGetU64V(pSSM, &pKvmApicCpu->uApicBaseMsr);
            pHlp->pfnSSMGetU32(pSSM, &pKvmApicCpu->uEsrInternal);

            /* Load the APIC page. */
            if (XAPIC_IN_X2APIC_MODE(pKvmApicCpu->uApicBaseMsr))
                pHlp->pfnSSMGetStruct(pSSM, pKvmApicCpu->pvApicPageR3, &g_aX2ApicPageFields[0]);
            else
                pHlp->pfnSSMGetStruct(pSSM, pKvmApicCpu->pvApicPageR3, &g_aXApicPageFields[0]);

            /*
             * Load the timer data, because the in kernel APIC handles the timer there
             * is nothing we have to do here as everything should be in the APIC state.
             */
            uint64_t u64Skip = 0;
            rc = pHlp->pfnSSMGetU64(pSSM, &u64Skip);     AssertRCReturn(rc, rc);

            bool fActive = false;
            rc = pHlp->pfnTimerSkipLoad(pSSM, &fActive); AssertRCReturn(rc, rc);

            /* Load the LINT0, LINT1 interrupt line states, nothing we act on here. */
            if (uVersion > APIC_SAVED_STATE_VERSION_VBOX_51_BETA2)
            {
                bool fSkip = false;
                pHlp->pfnSSMGetBoolV(pSSM, &fSkip);
                pHlp->pfnSSMGetBoolV(pSSM, &fSkip);
            }
        }
        else
        {
#if 0
            rc = apicR3LoadLegacyVCpuData(pDevIns, pVCpu, pSSM, uVersion);
            AssertRCReturn(rc, rc);
#else
            AssertFailedReturn(VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION); /** @todo */
#endif
        }

        /*
         * Check that we're still good wrt restored data, then tell CPUM about the current CPUID[1].EDX[9] visibility.
         */
        rc = pHlp->pfnSSMHandleGetStatus(pSSM);
        AssertRCReturn(rc, rc);
        CPUMSetGuestCpuIdPerCpuApicFeature(pVCpu, RT_BOOL(pKvmApicCpu->uApicBaseMsr & MSR_IA32_APICBASE_EN));

        /* Sync the state now. */
        PXAPICPAGE pKvmApicPage = (PXAPICPAGE)pVCpu->apic.s.pvKvmPageR3;

        memcpy(pKvmApicPage, pVCpu->apic.s.pvApicPageR3, sizeof(XAPICPAGE));
        int rcLnx = ioctl(pKvmApicCpu->iFdVCpu, KVM_SET_LAPIC, pKvmApicPage);
        if (RT_UNLIKELY(rcLnx == -1))
        {
            rc = RTErrConvertFromErrno(errno);
            AssertMsgFailed(("APIC/KVM: Calling KVM_SET_LAPIC failed: %Rrc", rc));
            LogRelMax(10, ("APIC/KVM: Calling KVM_SET_LAPIC failed failed: %Rrc", rc));
            return rc;
        }
    }

    return rc;
}


/**
 * @copydoc FNSSMDEVLOADDONE
 */
static DECLCALLBACK(int) apicR3KvmLoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVM pVM  = PDMDevHlpGetVM(pDevIns);

    RT_NOREF(pSSM);
    AssertReturn(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * Ensure the interrupt APIC FF is clear which might be set
     * when a saved state using our own APIC emulation was loaded,
     * it is completely unused with the KVM APIC emulation.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];

        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_APIC);
    }

    return VINF_SUCCESS;
}


/**
 * Initializes per-VCPU APIC to the state following a power-up or hardware
 * reset.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   fResetApicBaseMsr   Whether to reset the APIC base MSR.
 */
static void apicR3HKvmResetCpu(PVMCPUCC pVCpu, bool fResetApicBaseMsr)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);

    LogFlow(("APIC/KVM%u: apicR3ResetCpu: fResetApicBaseMsr=%RTbool\n", pVCpu->idCpu, fResetApicBaseMsr));

#ifdef VBOX_STRICT
    /* Verify that the initial APIC ID reported via CPUID matches our VMCPU ID assumption. */
    uint32_t uEax, uEbx, uEcx, uEdx;
    uEax = uEbx = uEcx = uEdx = UINT32_MAX;
    CPUMGetGuestCpuId(pVCpu, 1, 0, -1 /*f64BitMode*/, &uEax, &uEbx, &uEcx, &uEdx);
    Assert(((uEbx >> 24) & 0xff) == pVCpu->idCpu);
#endif

    /*
     * The state following a power-up or reset is a superset of the INIT state.
     * See Intel spec. 10.4.7.3 "Local APIC State After an INIT Reset ('Wait-for-SIPI' State)"
     */
    apicR3KvmInitIpi(pVCpu);

    /*
     * The APIC version register is read-only, so just initialize it here.
     * It is not clear from the specs, where exactly it is initialized.
     * The version determines the number of LVT entries and size of the APIC ID (8 bits for P4).
     */
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    pXApicPage->version.u.u8MaxLvtEntry = XAPIC_MAX_LVT_ENTRIES_P4 - 1;
    pXApicPage->version.u.u8Version     = XAPIC_HARDWARE_VERSION_P4;
    AssertCompile(sizeof(pXApicPage->id.u8ApicId) >= XAPIC_APIC_ID_BIT_COUNT_P4 / 8);
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif

    /** @todo It isn't clear in the spec. where exactly the default base address
     *        is (re)initialized, atm we do it here in Reset. */
    if (fResetApicBaseMsr)
        apicCommonResetBaseMsr(pVCpu);

    /*
     * Initialize the APIC ID register to xAPIC format.
     */
    RT_BZERO(&pXApicPage->id, sizeof(pXApicPage->id));
    pXApicPage->id.u8ApicId = pVCpu->idCpu;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnInitComplete}
 */
DECLCALLBACK(int) apicR3KvmInitComplete(PPDMDEVINS pDevIns)
{
    PVM     pVM     = PDMDevHlpGetVM(pDevIns);
    PKVMAPIC pKvmApic = VM_TO_KVMAPIC(pVM);

    /*
     * Init APIC settings that rely on HM and CPUM configurations.
     */
    CPUMCPUIDLEAF CpuLeaf;
    int rc = CPUMR3CpuIdGetLeaf(pVM, &CpuLeaf, 1, 0);
    AssertRCReturn(rc, rc);

    pKvmApic->fSupportsTscDeadline = RT_BOOL(CpuLeaf.uEcx & X86_CPUID_FEATURE_ECX_TSCDEADL);

    LogRel(("APIC/KVM: fSupportsTscDeadline=%RTbool\n", pKvmApic->fSupportsTscDeadline));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
DECLCALLBACK(void) apicR3KvmReset(PPDMDEVINS pDevIns)
{
    LogFlowFunc(("pDevIns=%p\n", pDevIns));

    PVM pVM = PDMDevHlpGetVM(pDevIns);
    VM_ASSERT_EMT0(pVM);
    VM_ASSERT_IS_NOT_RUNNING(pVM);

    VMCC_FOR_EACH_VMCPU(pVM)
    {
        apicR3HKvmResetCpu(pVCpu, true /*fResetApicBaseMsr*/);

        PKVMAPICCPU pKvmApicCpu = VMCPU_TO_KVMAPICCPU(pVCpu);

        memcpy(pVCpu->apic.s.pvKvmPageR3, pVCpu->apic.s.pvApicPageR3, sizeof(XAPICPAGE));
        int rcLnx = ioctl(pKvmApicCpu->iFdVCpu, KVM_SET_LAPIC, pVCpu->apic.s.pvKvmPageR3);
        if (RT_UNLIKELY(rcLnx == -1))
        {
            int rc = RTErrConvertFromErrno(errno);
            LogRel(("APIC/KVM%u: Failed to set the KVM APIC state during reset (rc=%Rrc errno=%d)", pVCpu->idCpu, rc, errno));
        }
    }
    VMCC_FOR_EACH_VMCPU_END(pVM);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
DECLCALLBACK(int) apicR3KvmDestruct(PPDMDEVINS pDevIns)
{
    LogFlowFunc(("pDevIns=%p\n", pDevIns));
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);

    PVM      pVM      = PDMDevHlpGetVM(pDevIns);
    PKVMAPIC pKvmApic = VM_TO_KVMAPIC(pVM);

    if (pKvmApic->pMsiRoutes)
    {
        RTMemFree(pKvmApic->pMsiRoutes);
        pKvmApic->pMsiRoutes = NULL;
    }

    /* Free the Virtual-APIC pages for all VCPUs. */
    {
        PVMCPU     pVCpu0      = pVM->apCpusR3[0];
        PKVMAPICCPU pKvmApicCpu0 = VMCPU_TO_KVMAPICCPU(pVCpu0);

        if (pKvmApicCpu0->pvApicPageR3 != NIL_RTR3PTR)
            SUPR3PageFree(pKvmApicCpu0->pvApicPageR3, pVM->cCpus);
    }

    /* Reset the pointers for each VCPUs. */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU      pVCpu       = pVM->apCpusR3[idCpu];
        PKVMAPICCPU pKvmApicCpu = VMCPU_TO_KVMAPICCPU(pVCpu);

        pKvmApicCpu->pvApicPageR3 = NIL_RTR3PTR;
        pKvmApicCpu->pvKvmPageR3  = NIL_RTR3PTR;
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
DECLCALLBACK(int) apicR3KvmConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    LogFlowFunc(("pDevIns=%p\n", pDevIns));
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    Assert(iInstance == 0); NOREF(iInstance);

    PKVMAPICDEV   pThis    = PDMDEVINS_2_DATA(pDevIns, PKVMAPICDEV);
    PCPDMDEVHLPR3 pHlp     = pDevIns->pHlpR3;
    PVM           pVM      = PDMDevHlpGetVM(pDevIns);
    PKVMAPIC      pKvmApic = VM_TO_KVMAPIC(pVM);

    /*
     * Init the data.
     */
    pThis->pDevIns     = pDevIns;
    pKvmApic->pDevInsR3 = pDevIns;
    int rc = NEMR3LinuxGetKvmVmFd(pVM, &pKvmApic->iFdVm);
    if (RT_FAILURE(rc))
        return VMR3SetError(pVM->pUVM, VERR_APIC_IPE_2, RT_SRC_POS,
                            "Failed to get the KVM VM handle. rc=%Rrc", rc);

    /*
     * Validate APIC settings.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Mode|IOAPIC|NumCPUs|MacOSWorkaround", "");

    /** @devcfgm{apic, IOAPIC, bool, true}
     * Indicates whether an I/O APIC is present in the system. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "IOAPIC", &pKvmApic->fIoApicPresent, true);
    AssertLogRelRCReturn(rc, rc);

    /** @devcfgm{apic, Mode, PDMAPICMODE, APIC(2)}
     * Max APIC feature level. */
    uint8_t uMaxMode;
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Mode", &uMaxMode, PDMAPICMODE_APIC);
    AssertLogRelRCReturn(rc, rc);
    switch ((PDMAPICMODE)uMaxMode)
    {
        case PDMAPICMODE_NONE:
            LogRel(("APIC/KVM: APIC maximum mode configured as 'None', effectively disabled/not-present!\n"));
            RT_FALL_THROUGH();
        case PDMAPICMODE_APIC:
        case PDMAPICMODE_X2APIC:
            break;
        default:
            return VMR3SetError(pVM->pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS, "APIC mode %d unknown.", uMaxMode);
    }
    pKvmApic->enmMaxMode = (PDMAPICMODE)uMaxMode;

    /*
     * Disable automatic PDM locking for this device.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Register the APIC with PDM.
     */
    rc = PDMDevHlpIcRegister(pDevIns);
    AssertLogRelRCReturn(rc, rc);

    rc = PDMApicRegisterBackend(pVM, PDMAPICBACKENDTYPE_KVM, &g_ApicNemBackend);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Initialize the APIC CPUID state.
     */
    if (pKvmApic->enmMaxMode == PDMAPICMODE_X2APIC)
    {
        /* Enable x2APIC support in KVM. */
        struct kvm_enable_cap CapEn =
        {
            KVM_CAP_X2APIC_API, 0,
            { /** @todo KVM_X2APIC_API_USE_32BIT_IDS | */ KVM_X2APIC_API_DISABLE_BROADCAST_QUIRK, 0, 0, 0}
        };

        int rcLnx = ioctl(pKvmApic->iFdVm, KVM_ENABLE_CAP, &CapEn);
        if (rcLnx == -1)
        {
            rc = RTErrConvertFromErrno(errno);
            LogRel(("NEM: Failed enabling the KVM x2APIC emulation: %Rrc\n", rc));
            return rc;
        }

        rc = CPUMR3MsrRangesInsert(pVM, &g_MsrRange_x2Apic);
        AssertLogRelRCReturn(rc, rc);
    }
    else
    {
        /* We currently don't have a function to remove the range, so we register an range which will cause a #GP. */
        rc = CPUMR3MsrRangesInsert(pVM, &g_MsrRange_x2Apic_Invalid);
        AssertLogRelRCReturn(rc, rc);
    }

    /* Tell CPUM about the APIC feature level so it can adjust APICBASE MSR GP mask and CPUID bits. */
    apicR3CommonSetCpuIdFeatureLevel(pVM, pKvmApic->enmMaxMode);

    /*
     * Allocate all the virtual-APIC pages.
     */
    AssertCompile(sizeof(XAPICPAGE) <= HOST_PAGE_SIZE);
    AssertCompile((sizeof(XAPICPAGE) % HOST_PAGE_SIZE) == 0);
    size_t const cPages      = pVM->cCpus * 2;
    void        *pvApicPages = NULL;
    rc = SUPR3PageAlloc(cPages, 0 /* fFlags */, &pvApicPages);
    AssertLogRelMsgRCReturn(rc, ("Failed to allocate %u page(s) for the virtual-APIC page(s), rc=%Rrc\n", cPages, rc), rc);

    /* Zero the virtual-APIC pages here, later partially initialized by apicR3HKvmResetCpu. */
    AssertPtr(pvApicPages);
    RT_BZERO(pvApicPages, cPages * sizeof(XAPICPAGE));

    /*
     * Map the virtual-APIC pages to Hyper-V (for syncing the state).
     */
    VMCC_FOR_EACH_VMCPU(pVM)
    {
        PKVMAPICCPU pKvmApicCpu = VMCPU_TO_KVMAPICCPU(pVCpu);
        Assert(pKvmApicCpu->pvApicPageR3 == NIL_RTR3PTR);
        Assert(pKvmApicCpu->pvKvmPageR3 == NIL_RTR3PTR);

        size_t const offPage     = 2 * idCpu * sizeof(XAPICPAGE);
        pKvmApicCpu->pvKvmPageR3  = (void *)((uintptr_t)pvApicPages + offPage);
        pKvmApicCpu->pvApicPageR3 = (void *)((uintptr_t)pvApicPages + offPage + sizeof(XAPICPAGE));

        Assert(pKvmApicCpu->pvKvmPageR3 != NIL_RTR3PTR);
        Assert(pKvmApicCpu->pvApicPageR3 != NIL_RTR3PTR);

        /* Initialize the APIC page and the APIC base MSR and copy it over to the Hyper-V APIC page. */
        PXAPICPAGE pKvmApicPage = (PXAPICPAGE)pKvmApicCpu->pvKvmPageR3;
        {
            apicR3HKvmResetCpu(pVCpu, true /*fResetApicBaseMsr*/);
            PCXAPICPAGE pXApicPage = (PCXAPICPAGE)pKvmApicCpu->pvApicPageR3;
            Assert(pKvmApicPage != pXApicPage);
            memcpy(pKvmApicPage, pXApicPage, sizeof(XAPICPAGE));
        }

        rc = NEMR3LinuxGetKvmVCpuFd(pVCpu, &pKvmApicCpu->iFdVCpu);
        AssertRC(rc);

        int rcLnx = ioctl(pKvmApicCpu->iFdVCpu, KVM_SET_LAPIC, pKvmApicPage);
        if (RT_UNLIKELY(rcLnx == -1))
        {
            rc = RTErrConvertFromErrno(errno);
            return PDMDEV_SET_ERROR(pDevIns, rc,
                                    N_("APIC/KVM: Failed to set the initial KVM APIC state"));
        }
    }
    VMCC_FOR_EACH_VMCPU_END(pVM);

    /*
     * Install dummy MSI routes for the IO-APIC interrupts. These are required
     * so KVM can properly deduce for which interrupts it needs to generate IO-APIC
     * EOI broadcast VM exits.
     */
    struct kvm_irq_routing *pMsiRoutes = (struct kvm_irq_routing *)RTMemAllocZ(RT_UOFFSETOF_DYN(struct kvm_irq_routing, entries[IOAPIC_NUM_PINS]));
    if (!pMsiRoutes)
        return PDMDEV_SET_ERROR(pDevIns, VERR_NO_MEMORY,
                                N_("APIC/KVM: Out of memory trying to allocate the IO-APIC MSI routes"));

    pKvmApic->pMsiRoutes = pMsiRoutes;

    pMsiRoutes->nr = IOAPIC_NUM_PINS;

    for (uint32_t i = 0; i < IOAPIC_NUM_PINS; i++)
    {
        pMsiRoutes->entries[i].gsi              = i;
        pMsiRoutes->entries[i].type             = KVM_IRQ_ROUTING_MSI;
        pMsiRoutes->entries[i].flags            = 0;
        pMsiRoutes->entries[i].u.msi.address_lo = 0;
        pMsiRoutes->entries[i].u.msi.address_hi = 0;
        pMsiRoutes->entries[i].u.msi.data       = 0;
    }

    int rcLnx = ioctl(pKvmApic->iFdVm, KVM_SET_GSI_ROUTING, pMsiRoutes);
    if (rcLnx == -1)
    {
        rc = RTErrConvertFromErrno(errno);
        AssertMsgFailed(("APIC/KVM: Setting IO-APIC routes failed: %Rrc", rc));
        LogRelMax(10, ("APIC/KVM: Setting IO-APIC routes failed: %Rrc", rc));
        return rc;
    }

    /*
     * Register debugger info callbacks.
     */
    DBGFR3InfoRegisterInternalEx(pVM, "apic",      "Dumps APIC basic information.", apicR3KvmInfo,      DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "apiclvt",   "Dumps APIC LVT information.",   apicR3KvmInfoLvt,   DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "apictimer", "Dumps APIC timer information.", apicR3KvmInfoTimer, DBGFINFO_FLAGS_ALL_EMTS);

    /*
     * Register saved state callbacks.
     */
    rc = PDMDevHlpSSMRegisterAsImposter(pDevIns, "apic", APIC_SAVED_STATE_VERSION, cPages * HOST_PAGE_SIZE,
                                        NULL /*pfnLivePrep*/, NULL /*pfnLiveExec*/, NULL /*pfnLiveDone*/,
                                        NULL /*pfnSavePrep*/, apicR3KvmSaveExec,    NULL /*pfnSaveDone*/,
                                        NULL /*pfnLoadPrep*/, apicR3KvmLoadExec,    apicR3KvmLoadDone);

    AssertRCReturn(rc, rc);

    /*
     * Statistics.
     */
    /** @todo */

    return VINF_SUCCESS;
}


/**
 * APIC device registration structure.
 */
const PDMDEVREG g_DeviceAPICNem =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "apic-nem",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_PIC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(KVMAPICDEV),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Advanced Programmable Interrupt Controller - KVM variant",
#if defined(IN_RING3)
    /* .szRCMod = */                "",
    /* .szR0Mod = */                "",
    /* .pfnConstruct = */           apicR3KvmConstruct,
    /* .pfnDestruct = */            apicR3KvmDestruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               apicR3KvmReset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        apicR3KvmInitComplete,
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
#else
# error "Not in IN_RING3!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

/**
 * The KVM APIC backend.
 */
const PDMAPICBACKEND g_ApicNemBackend =
{
    /* .pfnIsEnabled = */               apicR3KvmIsEnabled,
    /* .pfnInitIpi = */                 apicR3KvmInitIpi,
    /* .pfnGetBaseMsrNoCheck = */       apicR3KvmGetBaseMsrNoCheck,
    /* .pfnGetBaseMsr = */              apicR3KvmGetBaseMsr,
    /* .pfnSetBaseMsr = */              apicR3KvmSetBaseMsr,
    /* .pfnReadRaw32 = */               apicR3KvmReadRaw32,
    /* .pfnReadMsr = */                 apicR3KvmReadMsr,
    /* .pfnWriteMsr = */                apicR3KvmWriteMsr,
    /* .pfnGetTpr = */                  apicR3KvmGetTpr,
    /* .pfnSetTpr = */                  apicR3KvmSetTpr,
    /* .pfnGetIcrNoCheck = */           apicR3KvmGetIcrNoCheck,
    /* .pfnSetIcr = */                  apicR3KvmSetIcr,
    /* .pfnGetTimerFreq = */            apicR3KvmGetTimerFreq,
    /* .pfnSetLocalInterrupt = */       apicR3KvmSetLocalInterrupt,
    /* .pfnGetInterrupt = */            apicR3KvmGetInterrupt,
    /* .pfnPostInterrupt = */           apicR3KvmPostInterrupt,
    /* .pfnUpdatePendingInterrupts = */ apicR3KvmUpdatePendingInterrupts,
    /* .pfnBusDeliver = */              apicR3KvmBusDeliver,
    /* .pfnSetEoi = */                  apicR3KvmSetEoi,
    /* .pfnSetHvCompatMode = */         apicR3KvmSetHvCompatMode,
    /* .pfnImportState = */             apicR3KvmImportState,
    /* .pfnExportState = */             apicR3KvmExportState,
    /* .pfnUpdateStateAfterWrite = */   apicR3KvmUpdateStateAfterWrite,
    /* .pfnSetEoiFast = */              apicR3KvmSetEoiFast,
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

