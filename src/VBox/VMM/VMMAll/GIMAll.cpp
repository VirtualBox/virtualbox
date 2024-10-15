/* $Id: GIMAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIM - Guest Interface Manager - All Contexts.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#include <VBox/vmm/em.h>    /* For EMInterpretDisasCurrent */
#include "GIMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/dis.h>       /* For DISSTATE */
#include <VBox/err.h>
#include <iprt/string.h>

/* Include all the providers. */
#include "GIMHvInternal.h"
#include "GIMMinimalInternal.h"


/**
 * Checks whether GIM is being used by this VM.
 *
 * @retval  true if used.
 * @retval  false if no GIM provider ("none") is used.
 *
 * @param   pVM       The cross context VM structure.
 */
VMMDECL(bool) GIMIsEnabled(PVM pVM)
{
    return pVM->gim.s.enmProviderId != GIMPROVIDERID_NONE;
}


/**
 * Gets the GIM provider configured for this VM.
 *
 * @returns The GIM provider Id.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(GIMPROVIDERID) GIMGetProvider(PVM pVM)
{
    return pVM->gim.s.enmProviderId;
}


/**
 * Returns the array of MMIO2 regions that are expected to be registered and
 * later mapped into the guest-physical address space for the GIM provider
 * configured for the VM.
 *
 * @returns Pointer to an array of GIM MMIO2 regions, may return NULL.
 * @param   pVM         The cross context VM structure.
 * @param   pcRegions   Where to store the number of items in the array.
 *
 * @remarks The caller does not own and therefore must -NOT- try to free the
 *          returned pointer.
 */
VMMDECL(PGIMMMIO2REGION) GIMGetMmio2Regions(PVMCC pVM, uint32_t *pcRegions)
{
    Assert(pVM);
    Assert(pcRegions);

    *pcRegions = 0;
    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            return gimHvGetMmio2Regions(pVM, pcRegions);
#endif
        default:
            break;
    }

    return NULL;
}


/**
 * Returns whether the guest has configured and enabled calls to the hypervisor.
 *
 * @returns true if hypercalls are enabled and usable, false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
VMM_INT_DECL(bool) GIMAreHypercallsEnabled(PVMCPUCC pVCpu)
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);
    if (!GIMIsEnabled(pVM))
        return false;

    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            return gimHvAreHypercallsEnabled(pVM);

        case GIMPROVIDERID_KVM:
            return gimKvmAreHypercallsEnabled(pVCpu);
#endif
        default:
            return false;
    }
}


/**
 * Implements a GIM hypercall with the provider configured for the VM.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_SUCCESS if the hypercall succeeded (even if its operation
 *          failed).
 * @retval  VINF_GIM_HYPERCALL_CONTINUING continue hypercall without updating
 *          RIP.
 * @retval  VINF_GIM_R3_HYPERCALL re-start the hypercall from ring-3.
 * @retval  VERR_GIM_HYPERCALL_ACCESS_DENIED CPL is insufficient.
 * @retval  VERR_GIM_HYPERCALLS_NOT_AVAILABLE hypercalls unavailable.
 * @retval  VERR_GIM_NOT_ENABLED GIM is not enabled (shouldn't really happen)
 * @retval  VERR_GIM_HYPERCALL_MEMORY_READ_FAILED hypercall failed while reading
 *          memory.
 * @retval  VERR_GIM_HYPERCALL_MEMORY_WRITE_FAILED hypercall failed while
 *          writing memory.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pCtx        Pointer to the guest-CPU context.
 *
 * @remarks The caller of this function needs to advance RIP as required.
 * @thread  EMT.
 */
VMM_INT_DECL(VBOXSTRICTRC) GIMHypercall(PVMCPUCC pVCpu, PCPUMCTX pCtx)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    RT_NOREF(pCtx);
#endif

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    VMCPU_ASSERT_EMT(pVCpu);

    if (RT_UNLIKELY(!GIMIsEnabled(pVM)))
        return VERR_GIM_NOT_ENABLED;

    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            return gimHvHypercall(pVCpu, pCtx);

        case GIMPROVIDERID_KVM:
            return gimKvmHypercall(pVCpu, pCtx);
#endif
        default:
            AssertMsgFailed(("GIMHypercall: for provider %u not available/implemented\n", pVM->gim.s.enmProviderId));
            return VERR_GIM_HYPERCALLS_NOT_AVAILABLE;
    }
}


/**
 * Same as GIMHypercall, except with disassembler opcode and instruction length.
 *
 * This is the interface used by IEM.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_SUCCESS if the hypercall succeeded (even if its operation
 *          failed).
 * @retval  VINF_GIM_HYPERCALL_CONTINUING continue hypercall without updating
 *          RIP.
 * @retval  VINF_GIM_R3_HYPERCALL re-start the hypercall from ring-3.
 * @retval  VERR_GIM_HYPERCALL_ACCESS_DENIED CPL is insufficient.
 * @retval  VERR_GIM_HYPERCALLS_NOT_AVAILABLE hypercalls unavailable.
 * @retval  VERR_GIM_NOT_ENABLED GIM is not enabled (shouldn't really happen)
 * @retval  VERR_GIM_HYPERCALL_MEMORY_READ_FAILED hypercall failed while reading
 *          memory.
 * @retval  VERR_GIM_HYPERCALL_MEMORY_WRITE_FAILED hypercall failed while
 *          writing memory.
 * @retval  VERR_GIM_INVALID_HYPERCALL_INSTR if uDisOpcode is the wrong one; raise \#UD.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pCtx        Pointer to the guest-CPU context.
 * @param   uDisOpcode  The disassembler opcode.
 * @param   cbInstr     The instruction length.
 *
 * @remarks The caller of this function needs to advance RIP as required.
 * @thread  EMT.
 */
VMM_INT_DECL(VBOXSTRICTRC) GIMHypercallEx(PVMCPUCC pVCpu, PCPUMCTX pCtx, unsigned uDisOpcode, uint8_t cbInstr)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    RT_NOREF(pCtx, uDisOpcode, cbInstr);
#endif

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    VMCPU_ASSERT_EMT(pVCpu);

    if (RT_UNLIKELY(!GIMIsEnabled(pVM)))
        return VERR_GIM_NOT_ENABLED;

    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            return gimHvHypercallEx(pVCpu, pCtx, uDisOpcode, cbInstr);

        case GIMPROVIDERID_KVM:
            return gimKvmHypercallEx(pVCpu, pCtx, uDisOpcode, cbInstr);
#endif
        default:
            AssertMsgFailedReturn(("enmProviderId=%u\n", pVM->gim.s.enmProviderId), VERR_GIM_HYPERCALLS_NOT_AVAILABLE);
    }
}


/**
 * Disassembles the instruction at RIP and if it's a hypercall
 * instruction, performs the hypercall.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pCtx        Pointer to the guest-CPU context.
 * @param   pcbInstr    Where to store the disassembled instruction length.
 *                      Optional, can be NULL.
 *
 * @todo    This interface should disappear when IEM/REM execution engines
 *          handle VMCALL/VMMCALL instructions to call into GIM when
 *          required. See @bugref{7270#c168}.
 */
VMM_INT_DECL(VBOXSTRICTRC) GIMExecHypercallInstr(PVMCPUCC pVCpu, PCPUMCTX pCtx, uint8_t *pcbInstr)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    RT_NOREF(pCtx);
#endif

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    VMCPU_ASSERT_EMT(pVCpu);

    if (RT_UNLIKELY(!GIMIsEnabled(pVM)))
        return VERR_GIM_NOT_ENABLED;

    unsigned cbInstr;
    DISSTATE Dis;
    int rc = EMInterpretDisasCurrent(pVCpu, &Dis, &cbInstr);
    if (RT_SUCCESS(rc))
    {
        if (pcbInstr)
            *pcbInstr = (uint8_t)cbInstr;
        switch (pVM->gim.s.enmProviderId)
        {
#if !defined(VBOX_VMM_TARGET_ARMV8)
            case GIMPROVIDERID_HYPERV:
                return gimHvHypercallEx(pVCpu, pCtx, Dis.pCurInstr->uOpcode, Dis.cbInstr);

            case GIMPROVIDERID_KVM:
                return gimKvmHypercallEx(pVCpu, pCtx, Dis.pCurInstr->uOpcode, Dis.cbInstr);
#endif
            default:
                AssertMsgFailed(("GIMExecHypercallInstr: for provider %u not available/implemented\n", pVM->gim.s.enmProviderId));
                return VERR_GIM_HYPERCALLS_NOT_AVAILABLE;
        }
    }

#if !defined(VBOX_VMM_TARGET_ARMV8)
    Log(("GIM: GIMExecHypercallInstr: Failed to disassemble CS:RIP=%04x:%08RX64. rc=%Rrc\n", pCtx->cs.Sel, pCtx->rip, rc));
#endif
    return rc;
}


/**
 * Returns whether the guest has configured and setup the use of paravirtualized
 * TSC.
 *
 * Paravirtualized TSCs are per-VM and the rest of the execution engine logic
 * relies on that.
 *
 * @returns true if enabled and usable, false otherwise.
 * @param   pVM         The cross context VM structure.
 */
VMM_INT_DECL(bool) GIMIsParavirtTscEnabled(PVMCC pVM)
{
    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            return gimHvIsParavirtTscEnabled(pVM);

        case GIMPROVIDERID_KVM:
            return gimKvmIsParavirtTscEnabled(pVM);
#endif
        default:
            break;
    }
    return false;
}


/**
 * Whether \#UD exceptions in the guest needs to be intercepted by the GIM
 * provider.
 *
 * At the moment, the reason why this isn't a more generic interface wrt to
 * exceptions is because of performance (each VM-exit would have to manually
 * check whether or not GIM needs to be notified). Left as a todo for later if
 * really required.
 *
 * @returns true if needed, false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMM_INT_DECL(bool) GIMShouldTrapXcptUD(PVMCPUCC pVCpu)
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);
    if (!GIMIsEnabled(pVM))
        return false;

    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_KVM:
            return gimKvmShouldTrapXcptUD(pVM);

        case GIMPROVIDERID_HYPERV:
            return gimHvShouldTrapXcptUD(pVCpu);
#endif
        default:
            return false;
    }
}


/**
 * Exception handler for \#UD when requested by the GIM provider.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_SUCCESS if the hypercall succeeded (even if its operation
 *          failed).
 * @retval  VINF_GIM_R3_HYPERCALL restart the hypercall from ring-3.
 * @retval  VINF_GIM_HYPERCALL_CONTINUING continue hypercall without updating
 *          RIP.
 * @retval  VERR_GIM_HYPERCALL_ACCESS_DENIED CPL is insufficient.
 * @retval  VERR_GIM_INVALID_HYPERCALL_INSTR instruction at RIP is not a valid
 *          hypercall instruction.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pCtx        Pointer to the guest-CPU context.
 * @param   pDis        Pointer to the disassembled instruction state at RIP.
 *                      If NULL is passed, it implies the disassembly of the
 *                      the instruction at RIP is the responsibility of the
 *                      GIM provider.
 * @param   pcbInstr    Where to store the instruction length of the hypercall
 *                      instruction. Optional, can be NULL.
 *
 * @thread  EMT(pVCpu).
 */
VMM_INT_DECL(VBOXSTRICTRC) GIMXcptUD(PVMCPUCC pVCpu, PCPUMCTX pCtx, PDISSTATE pDis, uint8_t *pcbInstr)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    RT_NOREF(pCtx, pDis, pcbInstr);
#endif

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    Assert(GIMIsEnabled(pVM));
    Assert(pDis || pcbInstr);

    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_KVM:
            return gimKvmXcptUD(pVM, pVCpu, pCtx, pDis, pcbInstr);

        case GIMPROVIDERID_HYPERV:
            return gimHvXcptUD(pVCpu, pCtx, pDis, pcbInstr);
#endif
        default:
            return VERR_GIM_OPERATION_FAILED;
    }
}


#if !defined(VBOX_VMM_TARGET_ARMV8)
/**
 * Invokes the read-MSR handler for the GIM provider configured for the VM.
 *
 * @returns Strict VBox status code like CPUMQueryGuestMsr.
 * @retval  VINF_CPUM_R3_MSR_READ
 * @retval  VERR_CPUM_RAISE_GP_0
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR to read.
 * @param   pRange      The range this MSR belongs to.
 * @param   puValue     Where to store the MSR value read.
 */
VMM_INT_DECL(VBOXSTRICTRC) GIMReadMsr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue)
{
    Assert(pVCpu);
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    Assert(GIMIsEnabled(pVM));
    VMCPU_ASSERT_EMT(pVCpu);

    switch (pVM->gim.s.enmProviderId)
    {
        case GIMPROVIDERID_HYPERV:
            return gimHvReadMsr(pVCpu, idMsr, pRange, puValue);

        case GIMPROVIDERID_KVM:
            return gimKvmReadMsr(pVCpu, idMsr, pRange, puValue);

        default:
            AssertMsgFailed(("GIMReadMsr: for unknown provider %u idMsr=%#RX32 -> #GP(0)", pVM->gim.s.enmProviderId, idMsr));
            return VERR_CPUM_RAISE_GP_0;
    }
}


/**
 * Invokes the write-MSR handler for the GIM provider configured for the VM.
 *
 * @returns Strict VBox status code like CPUMSetGuestMsr.
 * @retval  VINF_CPUM_R3_MSR_WRITE
 * @retval  VERR_CPUM_RAISE_GP_0
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR to write.
 * @param   pRange      The range this MSR belongs to.
 * @param   uValue      The value to set, ignored bits masked.
 * @param   uRawValue   The raw value with the ignored bits not masked.
 */
VMM_INT_DECL(VBOXSTRICTRC) GIMWriteMsr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    AssertPtr(pVCpu);
    NOREF(uValue);

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    Assert(GIMIsEnabled(pVM));
    VMCPU_ASSERT_EMT(pVCpu);

    switch (pVM->gim.s.enmProviderId)
    {
        case GIMPROVIDERID_HYPERV:
            return gimHvWriteMsr(pVCpu, idMsr, pRange, uRawValue);

        case GIMPROVIDERID_KVM:
            return gimKvmWriteMsr(pVCpu, idMsr, pRange, uRawValue);

        default:
            AssertMsgFailed(("GIMWriteMsr: for unknown provider %u idMsr=%#RX32 -> #GP(0)", pVM->gim.s.enmProviderId, idMsr));
            return VERR_CPUM_RAISE_GP_0;
    }
}


/**
 * Queries the opcode bytes for a native hypercall.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pvBuf           The destination buffer.
 * @param   cbBuf           The size of the buffer.
 * @param   pcbWritten      Where to return the number of bytes written.  This is
 *                          reliably updated only on successful return.  Optional.
 * @param   puDisOpcode     Where to return the disassembler opcode.  Optional.
 */
VMM_INT_DECL(int) GIMQueryHypercallOpcodeBytes(PVM pVM, void *pvBuf, size_t cbBuf, size_t *pcbWritten, uint16_t *puDisOpcode)
{
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    CPUMCPUVENDOR  enmCpuVendor = CPUMGetHostCpuVendor(pVM);
#else
    CPUMCPUVENDOR  enmCpuVendor = CPUMGetGuestCpuVendor(pVM); /* Use what is presented to the guest. */
#endif
    uint8_t const *pbSrc;
    size_t         cbSrc;
    switch (enmCpuVendor)
    {
        case CPUMCPUVENDOR_AMD:
        case CPUMCPUVENDOR_HYGON:
        {
            if (puDisOpcode)
                *puDisOpcode = OP_VMMCALL;
            static uint8_t const s_abHypercall[] = { 0x0F, 0x01, 0xD9 };   /* VMMCALL */
            pbSrc = s_abHypercall;
            cbSrc = sizeof(s_abHypercall);
            break;
        }

        case CPUMCPUVENDOR_INTEL:
        case CPUMCPUVENDOR_VIA:
        case CPUMCPUVENDOR_SHANGHAI:
        {
            if (puDisOpcode)
                *puDisOpcode = OP_VMCALL;
            static uint8_t const s_abHypercall[] = { 0x0F, 0x01, 0xC1 };   /* VMCALL */
            pbSrc = s_abHypercall;
            cbSrc = sizeof(s_abHypercall);
            break;
        }

        default:
            AssertMsgFailedReturn(("%d\n", enmCpuVendor), VERR_UNSUPPORTED_CPU);
    }
    if (RT_LIKELY(cbBuf >= cbSrc))
    {
        memcpy(pvBuf, pbSrc, cbSrc);
        if (pcbWritten)
            *pcbWritten = cbSrc;
        return VINF_SUCCESS;
    }
    return VERR_BUFFER_OVERFLOW;
}
#endif

