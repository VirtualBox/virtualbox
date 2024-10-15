/* $Id: CPUMAllRegs-armv8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPUM - CPU Monitor(/Manager) - Getters and Setters, ARMv8 variant.
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
#define LOG_GROUP LOG_GROUP_CPUM
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/hm.h>
#include "CPUMInternal-armv8.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>
#include <VBox/dis.h>
#include <VBox/log.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/tm.h>

#include <iprt/armv8.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#ifdef IN_RING3
# include <iprt/thread.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/**
 * Converts a CPUMCPU::Guest pointer into a VMCPU pointer.
 *
 * @returns Pointer to the Virtual CPU.
 * @param   a_pGuestCtx     Pointer to the guest context.
 */
#define CPUM_GUEST_CTX_TO_VMCPU(a_pGuestCtx) RT_FROM_MEMBER(a_pGuestCtx, VMCPU, cpum.s.Guest)

/** @def CPUM_INT_ASSERT_NOT_EXTRN
 * Macro for asserting that @a a_fNotExtrn are present.
 *
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling EMT.
 * @param   a_fNotExtrn     Mask of CPUMCTX_EXTRN_XXX bits to check.
 */
#define CPUM_INT_ASSERT_NOT_EXTRN(a_pVCpu, a_fNotExtrn) \
    AssertMsg(!((a_pVCpu)->cpum.s.Guest.fExtrn & (a_fNotExtrn)), \
              ("%#RX64; a_fNotExtrn=%#RX64\n", (a_pVCpu)->cpum.s.Guest.fExtrn, (a_fNotExtrn)))


/**
 * Queries the pointer to the internal CPUMCTX structure.
 *
 * @returns The CPUMCTX pointer.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(PCPUMCTX) CPUMQueryGuestCtxPtr(PVMCPU pVCpu)
{
    return &pVCpu->cpum.s.Guest;
}


VMMDECL(uint64_t)   CPUMGetGuestFlatPC(PVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_PC);
    return pVCpu->cpum.s.Guest.Pc.u64;
}


VMMDECL(uint64_t)   CPUMGetGuestFlatSP(PVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_SP);
    AssertReleaseFailed(); /** @todo Exception level. */
    return pVCpu->cpum.s.Guest.aSpReg[0].u64;
}


/**
 * Returns whether IRQs are currently masked.
 *
 * @returns true if IRQs are masked as indicated by the PState value.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool)       CPUMGetGuestIrqMasked(PVMCPUCC pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_PSTATE);
    return RT_BOOL(pVCpu->cpum.s.Guest.fPState & ARMV8_SPSR_EL2_AARCH64_I);
}


/**
 * Returns whether FIQs are currently masked.
 *
 * @returns true if FIQs are masked as indicated by the PState value.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool)       CPUMGetGuestFiqMasked(PVMCPUCC pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_PSTATE);
    return RT_BOOL(pVCpu->cpum.s.Guest.fPState & ARMV8_SPSR_EL2_AARCH64_F);
}


/**
 * Gets the host CPU vendor.
 *
 * @returns CPU vendor.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(CPUMCPUVENDOR) CPUMGetHostCpuVendor(PVM pVM)
{
    RT_NOREF(pVM);
    //AssertReleaseFailed();
    return CPUMCPUVENDOR_UNKNOWN;
}


/**
 * Gets the host CPU microarchitecture.
 *
 * @returns CPU microarchitecture.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(CPUMMICROARCH) CPUMGetHostMicroarch(PCVM pVM)
{
    RT_NOREF(pVM);
    AssertReleaseFailed();
    return kCpumMicroarch_Unknown;
}


/**
 * Gets the guest CPU vendor.
 *
 * @returns CPU vendor.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(CPUMCPUVENDOR) CPUMGetGuestCpuVendor(PVM pVM)
{
    RT_NOREF(pVM);
    //AssertReleaseFailed();
    return CPUMCPUVENDOR_UNKNOWN;
}


/**
 * Gets the guest CPU architecture.
 *
 * @returns CPU architecture.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(CPUMARCH) CPUMGetGuestArch(PCVM pVM)
{
    RT_NOREF(pVM);
    return kCpumArch_Arm; /* Static as we are in the ARM VMM module here. */
}


/**
 * Gets the guest CPU microarchitecture.
 *
 * @returns CPU microarchitecture.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(CPUMMICROARCH) CPUMGetGuestMicroarch(PCVM pVM)
{
    RT_NOREF(pVM);
    AssertReleaseFailed();
    return kCpumMicroarch_Unknown;
}


/**
 * Gets the maximum number of physical and linear address bits supported by the
 * guest.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pcPhysAddrWidth     Where to store the physical address width.
 * @param   pcLinearAddrWidth   Where to store the linear address width.
 */
VMMDECL(void) CPUMGetGuestAddrWidths(PCVM pVM, uint8_t *pcPhysAddrWidth, uint8_t *pcLinearAddrWidth)
{
    AssertPtr(pVM);
    AssertReturnVoid(pcPhysAddrWidth);
    AssertReturnVoid(pcLinearAddrWidth);
    *pcPhysAddrWidth   = pVM->cpum.s.GuestFeatures.cMaxPhysAddrWidth;
    *pcLinearAddrWidth = pVM->cpum.s.GuestFeatures.cMaxLinearAddrWidth;
}


/**
 * Tests if the guest has the paging enabled (PG).
 *
 * @returns true if in real mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestPagingEnabled(PCVMCPU pVCpu)
{
    RT_NOREF(pVCpu);
    AssertReleaseFailed();
    return false;
}


/**
 * Tests if the guest is running in 64 bits mode or not.
 *
 * @returns true if in 64 bits protected mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
VMMDECL(bool) CPUMIsGuestIn64BitCode(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_PSTATE);
    return !RT_BOOL(pVCpu->cpum.s.Guest.fPState & ARMV8_SPSR_EL2_AARCH64_M4);
}


/**
 * Helper for CPUMIsGuestIn64BitCodeEx that handles lazy resolving of hidden CS
 * registers.
 *
 * @returns true if in 64 bits protected mode, otherwise false.
 * @param   pCtx        Pointer to the current guest CPU context.
 */
VMM_INT_DECL(bool) CPUMIsGuestIn64BitCodeSlow(PCCPUMCTX pCtx)
{
    return CPUMIsGuestIn64BitCode(CPUM_GUEST_CTX_TO_VMCPU(pCtx));
}


/**
 * Sets the specified changed flags (CPUM_CHANGED_*).
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   fChangedAdd The changed flags to add.
 */
VMMDECL(void) CPUMSetChangedFlags(PVMCPU pVCpu, uint32_t fChangedAdd)
{
    pVCpu->cpum.s.fChanged |= fChangedAdd;
}


/**
 * Checks if the guest debug state is active.
 *
 * @returns boolean
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
VMMDECL(bool) CPUMIsGuestDebugStateActive(PVMCPU pVCpu)
{
    return RT_BOOL(pVCpu->cpum.s.fUseFlags & CPUM_USED_DEBUG_REGS_GUEST);
}


/**
 * Checks if the hyper debug state is active.
 *
 * @returns boolean
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
VMMDECL(bool) CPUMIsHyperDebugStateActive(PVMCPU pVCpu)
{
    return RT_BOOL(pVCpu->cpum.s.fUseFlags & CPUM_USED_DEBUG_REGS_HYPER);
}


/**
 * Mark the guest's debug state as inactive.
 *
 * @returns boolean
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @todo    This API doesn't make sense any more.
 */
VMMDECL(void) CPUMDeactivateGuestDebugState(PVMCPU pVCpu)
{
    Assert(!(pVCpu->cpum.s.fUseFlags & (CPUM_USED_DEBUG_REGS_GUEST | CPUM_USED_DEBUG_REGS_HYPER)));
    NOREF(pVCpu);
}


/**
 * Get the current exception level of the guest.
 *
 * @returns Exception Level 0 - 3
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
VMM_INT_DECL(uint8_t) CPUMGetGuestEL(PVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_PSTATE);
    return ARMV8_SPSR_EL2_AARCH64_GET_EL(pVCpu->cpum.s.Guest.fPState);
}


/**
 * Returns whether the guest has the MMU enabled for address translation.
 *
 * @returns true if address translation is enabled, false if not.
 */
VMM_INT_DECL(bool) CPUMGetGuestMmuEnabled(PVMCPUCC pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_PSTATE | CPUMCTX_EXTRN_SCTLR_TCR_TTBR);
    uint8_t bEl = ARMV8_SPSR_EL2_AARCH64_GET_EL(pVCpu->cpum.s.Guest.fPState);
    if (bEl == ARMV8_AARCH64_EL_2)
    {
        CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_SYSREG_EL2);
        return RT_BOOL(pVCpu->cpum.s.Guest.SctlrEl2.u64 & ARMV8_SCTLR_EL2_M);
    }

    Assert(bEl == ARMV8_AARCH64_EL_0 || bEl == ARMV8_AARCH64_EL_1);
    return RT_BOOL(pVCpu->cpum.s.Guest.Sctlr.u64 & ARMV8_SCTLR_EL2_M);
}


/**
 * Gets the current guest CPU mode.
 *
 * If paging mode is what you need, check out PGMGetGuestMode().
 *
 * @returns The CPU mode.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(CPUMMODE) CPUMGetGuestMode(PVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_PSTATE);
    if (pVCpu->cpum.s.Guest.fPState & ARMV8_SPSR_EL2_AARCH64_M4)
        return CPUMMODE_ARMV8_AARCH32;

    return CPUMMODE_ARMV8_AARCH64;
}


/**
 * Figure whether the CPU is currently executing 32 or 64 bit code.
 *
 * @returns 32 or 64.
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 */
VMMDECL(uint32_t)       CPUMGetGuestCodeBits(PVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_PSTATE);
    if (pVCpu->cpum.s.Guest.fPState & ARMV8_SPSR_EL2_AARCH64_M4)
        return 32;

    return 64;
}


VMMDECL(DISCPUMODE)     CPUMGetGuestDisMode(PVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_PSTATE);
    if (pVCpu->cpum.s.Guest.fPState & ARMV8_SPSR_EL2_AARCH64_M4)
    {
        if (pVCpu->cpum.s.Guest.fPState & ARMV8_SPSR_EL2_AARCH64_T)
            return DISCPUMODE_ARMV8_T32;

        return DISCPUMODE_ARMV8_A32;
    }

    return DISCPUMODE_ARMV8_A64;
}


/**
 * Used to dynamically imports state residing in NEM or HM.
 *
 * This is a worker for the CPUM_IMPORT_EXTRN_RET() macro and various IEM ones.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   fExtrnImport    The fields to import.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(int) CPUMImportGuestStateOnDemand(PVMCPUCC pVCpu, uint64_t fExtrnImport)
{
    VMCPU_ASSERT_EMT(pVCpu);
    if (pVCpu->cpum.s.Guest.fExtrn & fExtrnImport)
    {
        switch (pVCpu->cpum.s.Guest.fExtrn & CPUMCTX_EXTRN_KEEPER_MASK)
        {
            case CPUMCTX_EXTRN_KEEPER_NEM:
            {
                int rc = NEMImportStateOnDemand(pVCpu, fExtrnImport);
                Assert(rc == VINF_SUCCESS || RT_FAILURE_NP(rc));
                return rc;
            }

            default:
                AssertLogRelMsgFailedReturn(("%#RX64 vs %#RX64\n", pVCpu->cpum.s.Guest.fExtrn, fExtrnImport), VERR_CPUM_IPE_2);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Translates a microarchitecture enum value to the corresponding string
 * constant.
 *
 * @returns Read-only string constant (omits "kCpumMicroarch_" prefix). Returns
 *          NULL if the value is invalid.
 *
 * @param   enmMicroarch    The enum value to convert.
 *
 * @todo Doesn't really belong here but for now there is no other Armv8 CPUM source file.
 */
VMMDECL(const char *) CPUMMicroarchName(CPUMMICROARCH enmMicroarch)
{
    switch (enmMicroarch)
    {
#define CASE_RET_STR(enmValue)  case enmValue: return #enmValue + (sizeof("kCpumMicroarch_") - 1)
        CASE_RET_STR(kCpumMicroarch_Apple_M1);
#undef CASE_RET_STR
        default:
            break;
    }

    return NULL;
}
