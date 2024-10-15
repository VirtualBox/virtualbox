/* $Id: CPUMAllRegs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPUM - CPU Monitor(/Manager) - Getters and Setters.
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
#define LOG_GROUP LOG_GROUP_CPUM
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/hm.h>
#include "CPUMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>
#include <VBox/dis.h>
#include <VBox/log.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/tm.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#ifdef IN_RING3
# include <iprt/thread.h>
#endif

/** Disable stack frame pointer generation here. */
#if defined(_MSC_VER) && !defined(DEBUG) && defined(RT_ARCH_X86)
# pragma optimize("y", off)
#endif

AssertCompile2MemberOffsets(VM, cpum.s.GuestFeatures, cpum.ro.GuestFeatures);


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

/**
 * Lazily loads the hidden parts of a selector register when using raw-mode.
 */
#define CPUMSELREG_LAZY_LOAD_HIDDEN_PARTS(a_pVCpu, a_pSReg) \
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(a_pVCpu, a_pSReg))

/** @def CPUM_INT_ASSERT_NOT_EXTRN
 * Macro for asserting that @a a_fNotExtrn are present.
 *
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling EMT.
 * @param   a_fNotExtrn     Mask of CPUMCTX_EXTRN_XXX bits to check.
 */
#define CPUM_INT_ASSERT_NOT_EXTRN(a_pVCpu, a_fNotExtrn) \
    AssertMsg(!((a_pVCpu)->cpum.s.Guest.fExtrn & (a_fNotExtrn)), \
              ("%#RX64; a_fNotExtrn=%#RX64\n", (a_pVCpu)->cpum.s.Guest.fExtrn, (a_fNotExtrn)))


VMMDECL(void) CPUMSetHyperCR3(PVMCPU pVCpu, uint32_t cr3)
{
    pVCpu->cpum.s.Hyper.cr3 = cr3;
}

VMMDECL(uint32_t) CPUMGetHyperCR3(PVMCPU pVCpu)
{
    return pVCpu->cpum.s.Hyper.cr3;
}


/** @def MAYBE_LOAD_DRx
 * Macro for updating DRx values in raw-mode and ring-0 contexts.
 */
#ifdef IN_RING0
# define MAYBE_LOAD_DRx(a_pVCpu, a_fnLoad, a_uValue) do { \
        if ((a_pVCpu)->cpum.s.fUseFlags & (CPUM_USED_DEBUG_REGS_GUEST | CPUM_USED_DEBUG_REGS_HYPER)) \
            a_fnLoad(a_uValue); \
    } while (0)
#else
# define MAYBE_LOAD_DRx(a_pVCpu, a_fnLoad, a_uValue) do { } while (0)
#endif

static void cpumSetHyperDR0(PVMCPU pVCpu, RTGCUINTREG uDr0)
{
    pVCpu->cpum.s.Hyper.dr[0] = uDr0;
    MAYBE_LOAD_DRx(pVCpu, ASMSetDR0, uDr0);
}


static void cpumSetHyperDR1(PVMCPU pVCpu, RTGCUINTREG uDr1)
{
    pVCpu->cpum.s.Hyper.dr[1] = uDr1;
    MAYBE_LOAD_DRx(pVCpu, ASMSetDR1, uDr1);
}


static void cpumSetHyperDR2(PVMCPU pVCpu, RTGCUINTREG uDr2)
{
    pVCpu->cpum.s.Hyper.dr[2] = uDr2;
    MAYBE_LOAD_DRx(pVCpu, ASMSetDR2, uDr2);
}


static void cpumSetHyperDR3(PVMCPU pVCpu, RTGCUINTREG uDr3)
{
    pVCpu->cpum.s.Hyper.dr[3] = uDr3;
    MAYBE_LOAD_DRx(pVCpu, ASMSetDR3, uDr3);
}


VMMDECL(void) CPUMSetHyperDR6(PVMCPU pVCpu, RTGCUINTREG uDr6)
{
    pVCpu->cpum.s.Hyper.dr[6] = uDr6;
}


VMMDECL(void) CPUMSetHyperDR7(PVMCPU pVCpu, RTGCUINTREG uDr7)
{
    pVCpu->cpum.s.Hyper.dr[7] = uDr7;
}


VMMDECL(RTGCUINTREG) CPUMGetHyperDR0(PVMCPU pVCpu)
{
    return pVCpu->cpum.s.Hyper.dr[0];
}


VMMDECL(RTGCUINTREG) CPUMGetHyperDR1(PVMCPU pVCpu)
{
    return pVCpu->cpum.s.Hyper.dr[1];
}


VMMDECL(RTGCUINTREG) CPUMGetHyperDR2(PVMCPU pVCpu)
{
    return pVCpu->cpum.s.Hyper.dr[2];
}


VMMDECL(RTGCUINTREG) CPUMGetHyperDR3(PVMCPU pVCpu)
{
    return pVCpu->cpum.s.Hyper.dr[3];
}


VMMDECL(RTGCUINTREG) CPUMGetHyperDR6(PVMCPU pVCpu)
{
    return pVCpu->cpum.s.Hyper.dr[6];
}


VMMDECL(RTGCUINTREG) CPUMGetHyperDR7(PVMCPU pVCpu)
{
    return pVCpu->cpum.s.Hyper.dr[7];
}


/**
 * Checks that the special cookie stored in unused reserved RFLAGS bits
 *
 * @retval  true if cookie is ok.
 * @retval  false if cookie is not ok.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMM_INT_DECL(bool) CPUMAssertGuestRFlagsCookie(PVM pVM, PVMCPU pVCpu)
{
    AssertLogRelMsgReturn(      (  pVCpu->cpum.s.Guest.rflags.uBoth
                                 & ~(uint64_t)(CPUMX86EFLAGS_HW_MASK_64 | CPUMX86EFLAGS_INT_MASK_64))
                             == pVM->cpum.s.fReservedRFlagsCookie
                          && (pVCpu->cpum.s.Guest.rflags.uBoth & X86_EFL_RA1_MASK) == X86_EFL_RA1_MASK
                          && (pVCpu->cpum.s.Guest.rflags.uBoth & X86_EFL_RAZ_MASK & CPUMX86EFLAGS_HW_MASK_64) == 0,
                          ("rflags=%#RX64 vs fReservedRFlagsCookie=%#RX64\n",
                           pVCpu->cpum.s.Guest.rflags.uBoth, pVM->cpum.s.fReservedRFlagsCookie),
                          false);
    return true;
}


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


/**
 * Queries the pointer to the internal CPUMCTXMSRS structure.
 *
 * This is for NEM only.
 *
 * @returns The CPUMCTX pointer.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMM_INT_DECL(PCPUMCTXMSRS) CPUMQueryGuestCtxMsrsPtr(PVMCPU pVCpu)
{
    return &pVCpu->cpum.s.GuestMsrs;
}


VMMDECL(int) CPUMSetGuestGDTR(PVMCPU pVCpu, uint64_t GCPtrBase, uint16_t cbLimit)
{
    pVCpu->cpum.s.Guest.gdtr.cbGdt = cbLimit;
    pVCpu->cpum.s.Guest.gdtr.pGdt  = GCPtrBase;
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_GDTR;
    pVCpu->cpum.s.fChanged |= CPUM_CHANGED_GDTR;
    return VINF_SUCCESS; /* formality, consider it void. */
}


VMMDECL(int) CPUMSetGuestIDTR(PVMCPU pVCpu, uint64_t GCPtrBase, uint16_t cbLimit)
{
    pVCpu->cpum.s.Guest.idtr.cbIdt = cbLimit;
    pVCpu->cpum.s.Guest.idtr.pIdt  = GCPtrBase;
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_IDTR;
    pVCpu->cpum.s.fChanged |= CPUM_CHANGED_IDTR;
    return VINF_SUCCESS; /* formality, consider it void. */
}


VMMDECL(int) CPUMSetGuestTR(PVMCPU pVCpu, uint16_t tr)
{
    pVCpu->cpum.s.Guest.tr.Sel  = tr;
    pVCpu->cpum.s.fChanged |= CPUM_CHANGED_TR;
    return VINF_SUCCESS; /* formality, consider it void. */
}


VMMDECL(int) CPUMSetGuestLDTR(PVMCPU pVCpu, uint16_t ldtr)
{
    pVCpu->cpum.s.Guest.ldtr.Sel      = ldtr;
    /* The caller will set more hidden bits if it has them. */
    pVCpu->cpum.s.Guest.ldtr.ValidSel = 0;
    pVCpu->cpum.s.Guest.ldtr.fFlags   = 0;
    pVCpu->cpum.s.fChanged  |= CPUM_CHANGED_LDTR;
    return VINF_SUCCESS; /* formality, consider it void. */
}


/**
 * Set the guest CR0.
 *
 * When called in GC, the hyper CR0 may be updated if that is
 * required. The caller only has to take special action if AM,
 * WP, PG or PE changes.
 *
 * @returns VINF_SUCCESS (consider it void).
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   cr0     The new CR0 value.
 */
VMMDECL(int) CPUMSetGuestCR0(PVMCPUCC pVCpu, uint64_t cr0)
{
    /*
     * Check for changes causing TLB flushes (for REM).
     * The caller is responsible for calling PGM when appropriate.
     */
    if (    (cr0                     & (X86_CR0_PG | X86_CR0_WP | X86_CR0_PE))
        !=  (pVCpu->cpum.s.Guest.cr0 & (X86_CR0_PG | X86_CR0_WP | X86_CR0_PE)))
        pVCpu->cpum.s.fChanged |= CPUM_CHANGED_GLOBAL_TLB_FLUSH;
    pVCpu->cpum.s.fChanged |= CPUM_CHANGED_CR0;

    /*
     * Let PGM know if the WP goes from 0 to 1 (netware WP0+RO+US hack)
     */
    if (((cr0 ^ pVCpu->cpum.s.Guest.cr0) & X86_CR0_WP) && (cr0 & X86_CR0_WP))
        PGMCr0WpEnabled(pVCpu);

    /* The ET flag is settable on a 386 and hardwired on 486+. */
    if (   !(cr0 & X86_CR0_ET)
        && pVCpu->CTX_SUFF(pVM)->cpum.s.GuestFeatures.enmMicroarch != kCpumMicroarch_Intel_80386)
        cr0 |= X86_CR0_ET;

    pVCpu->cpum.s.Guest.cr0 = cr0;
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_CR0;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestCR2(PVMCPU pVCpu, uint64_t cr2)
{
    pVCpu->cpum.s.Guest.cr2 = cr2;
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_CR2;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestCR3(PVMCPU pVCpu, uint64_t cr3)
{
    pVCpu->cpum.s.Guest.cr3 = cr3;
    pVCpu->cpum.s.fChanged |= CPUM_CHANGED_CR3;
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_CR3;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestCR4(PVMCPU pVCpu, uint64_t cr4)
{
    /* Note! We don't bother with OSXSAVE and legacy CPUID patches. */

    if (   (cr4                     & (X86_CR4_PGE | X86_CR4_PAE | X86_CR4_PSE))
        != (pVCpu->cpum.s.Guest.cr4 & (X86_CR4_PGE | X86_CR4_PAE | X86_CR4_PSE)))
        pVCpu->cpum.s.fChanged |= CPUM_CHANGED_GLOBAL_TLB_FLUSH;

    pVCpu->cpum.s.fChanged |= CPUM_CHANGED_CR4;
    pVCpu->cpum.s.Guest.cr4 = cr4;
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_CR4;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestEFlags(PVMCPU pVCpu, uint32_t eflags)
{
    pVCpu->cpum.s.Guest.eflags.u = eflags;
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_RFLAGS;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestEIP(PVMCPU pVCpu, uint32_t eip)
{
    pVCpu->cpum.s.Guest.eip = eip;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestEAX(PVMCPU pVCpu, uint32_t eax)
{
    pVCpu->cpum.s.Guest.eax = eax;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestEBX(PVMCPU pVCpu, uint32_t ebx)
{
    pVCpu->cpum.s.Guest.ebx = ebx;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestECX(PVMCPU pVCpu, uint32_t ecx)
{
    pVCpu->cpum.s.Guest.ecx = ecx;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestEDX(PVMCPU pVCpu, uint32_t edx)
{
    pVCpu->cpum.s.Guest.edx = edx;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestESP(PVMCPU pVCpu, uint32_t esp)
{
    pVCpu->cpum.s.Guest.esp = esp;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestEBP(PVMCPU pVCpu, uint32_t ebp)
{
    pVCpu->cpum.s.Guest.ebp = ebp;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestESI(PVMCPU pVCpu, uint32_t esi)
{
    pVCpu->cpum.s.Guest.esi = esi;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestEDI(PVMCPU pVCpu, uint32_t edi)
{
    pVCpu->cpum.s.Guest.edi = edi;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestSS(PVMCPU pVCpu, uint16_t ss)
{
    pVCpu->cpum.s.Guest.ss.Sel = ss;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestCS(PVMCPU pVCpu, uint16_t cs)
{
    pVCpu->cpum.s.Guest.cs.Sel = cs;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestDS(PVMCPU pVCpu, uint16_t ds)
{
    pVCpu->cpum.s.Guest.ds.Sel = ds;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestES(PVMCPU pVCpu, uint16_t es)
{
    pVCpu->cpum.s.Guest.es.Sel = es;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestFS(PVMCPU pVCpu, uint16_t fs)
{
    pVCpu->cpum.s.Guest.fs.Sel = fs;
    return VINF_SUCCESS;
}


VMMDECL(int) CPUMSetGuestGS(PVMCPU pVCpu, uint16_t gs)
{
    pVCpu->cpum.s.Guest.gs.Sel = gs;
    return VINF_SUCCESS;
}


VMMDECL(void) CPUMSetGuestEFER(PVMCPU pVCpu, uint64_t val)
{
    pVCpu->cpum.s.Guest.msrEFER = val;
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_EFER;
}


VMMDECL(RTGCPTR) CPUMGetGuestIDTR(PCVMCPU pVCpu, uint16_t *pcbLimit)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_IDTR);
    if (pcbLimit)
        *pcbLimit = pVCpu->cpum.s.Guest.idtr.cbIdt;
    return pVCpu->cpum.s.Guest.idtr.pIdt;
}


VMMDECL(RTSEL) CPUMGetGuestTR(PCVMCPU pVCpu, PCPUMSELREGHID pHidden)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_TR);
    if (pHidden)
        *pHidden = pVCpu->cpum.s.Guest.tr;
    return pVCpu->cpum.s.Guest.tr.Sel;
}


VMMDECL(RTSEL) CPUMGetGuestCS(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CS);
    return pVCpu->cpum.s.Guest.cs.Sel;
}


VMMDECL(RTSEL) CPUMGetGuestDS(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_DS);
    return pVCpu->cpum.s.Guest.ds.Sel;
}


VMMDECL(RTSEL) CPUMGetGuestES(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_ES);
    return pVCpu->cpum.s.Guest.es.Sel;
}


VMMDECL(RTSEL) CPUMGetGuestFS(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_FS);
    return pVCpu->cpum.s.Guest.fs.Sel;
}


VMMDECL(RTSEL) CPUMGetGuestGS(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_GS);
    return pVCpu->cpum.s.Guest.gs.Sel;
}


VMMDECL(RTSEL) CPUMGetGuestSS(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_SS);
    return pVCpu->cpum.s.Guest.ss.Sel;
}


VMMDECL(uint64_t)   CPUMGetGuestFlatPC(PVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_EFER);
    CPUMSELREG_LAZY_LOAD_HIDDEN_PARTS(pVCpu, &pVCpu->cpum.s.Guest.cs);
    if (   !CPUMIsGuestInLongMode(pVCpu)
        || !pVCpu->cpum.s.Guest.cs.Attr.n.u1Long)
        return pVCpu->cpum.s.Guest.eip + (uint32_t)pVCpu->cpum.s.Guest.cs.u64Base;
    return pVCpu->cpum.s.Guest.rip + pVCpu->cpum.s.Guest.cs.u64Base;
}


VMMDECL(uint64_t)   CPUMGetGuestFlatSP(PVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RSP | CPUMCTX_EXTRN_SS | CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_EFER);
    CPUMSELREG_LAZY_LOAD_HIDDEN_PARTS(pVCpu, &pVCpu->cpum.s.Guest.ss);
    if (   !CPUMIsGuestInLongMode(pVCpu)
        || !pVCpu->cpum.s.Guest.cs.Attr.n.u1Long)
        return pVCpu->cpum.s.Guest.eip + (uint32_t)pVCpu->cpum.s.Guest.ss.u64Base;
    return pVCpu->cpum.s.Guest.rip + pVCpu->cpum.s.Guest.ss.u64Base;
}


VMMDECL(RTSEL) CPUMGetGuestLDTR(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_LDTR);
    return pVCpu->cpum.s.Guest.ldtr.Sel;
}


VMMDECL(RTSEL) CPUMGetGuestLdtrEx(PCVMCPU pVCpu, uint64_t *pGCPtrBase, uint32_t *pcbLimit)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_LDTR);
    *pGCPtrBase = pVCpu->cpum.s.Guest.ldtr.u64Base;
    *pcbLimit   = pVCpu->cpum.s.Guest.ldtr.u32Limit;
    return pVCpu->cpum.s.Guest.ldtr.Sel;
}


VMMDECL(uint64_t) CPUMGetGuestCR0(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0);
    return pVCpu->cpum.s.Guest.cr0;
}


VMMDECL(uint64_t) CPUMGetGuestCR2(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR2);
    return pVCpu->cpum.s.Guest.cr2;
}


VMMDECL(uint64_t) CPUMGetGuestCR3(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR3);
    return pVCpu->cpum.s.Guest.cr3;
}


VMMDECL(uint64_t) CPUMGetGuestCR4(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR4);
    return pVCpu->cpum.s.Guest.cr4;
}


VMMDECL(uint64_t) CPUMGetGuestCR8(PCVMCPUCC pVCpu)
{
    uint64_t u64;
    int rc = CPUMGetGuestCRx(pVCpu, DISCREG_CR8, &u64);
    if (RT_FAILURE(rc))
        u64 = 0;
    return u64;
}


VMMDECL(void) CPUMGetGuestGDTR(PCVMCPU pVCpu, PVBOXGDTR pGDTR)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_GDTR);
    *pGDTR = pVCpu->cpum.s.Guest.gdtr;
}


VMMDECL(uint32_t) CPUMGetGuestEIP(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RIP);
    return pVCpu->cpum.s.Guest.eip;
}


VMMDECL(uint64_t) CPUMGetGuestRIP(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RIP);
    return pVCpu->cpum.s.Guest.rip;
}


VMMDECL(uint32_t) CPUMGetGuestEAX(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RAX);
    return pVCpu->cpum.s.Guest.eax;
}


VMMDECL(uint32_t) CPUMGetGuestEBX(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RBX);
    return pVCpu->cpum.s.Guest.ebx;
}


VMMDECL(uint32_t) CPUMGetGuestECX(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RCX);
    return pVCpu->cpum.s.Guest.ecx;
}


VMMDECL(uint32_t) CPUMGetGuestEDX(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RDX);
    return pVCpu->cpum.s.Guest.edx;
}


VMMDECL(uint32_t) CPUMGetGuestESI(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RSI);
    return pVCpu->cpum.s.Guest.esi;
}


VMMDECL(uint32_t) CPUMGetGuestEDI(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RDI);
    return pVCpu->cpum.s.Guest.edi;
}


VMMDECL(uint32_t) CPUMGetGuestESP(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RSP);
    return pVCpu->cpum.s.Guest.esp;
}


VMMDECL(uint32_t) CPUMGetGuestEBP(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RBP);
    return pVCpu->cpum.s.Guest.ebp;
}


VMMDECL(uint32_t) CPUMGetGuestEFlags(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RFLAGS);
    return pVCpu->cpum.s.Guest.eflags.u;
}


VMMDECL(int) CPUMGetGuestCRx(PCVMCPUCC pVCpu, unsigned iReg, uint64_t *pValue)
{
    switch (iReg)
    {
        case DISCREG_CR0:
            CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0);
            *pValue = pVCpu->cpum.s.Guest.cr0;
            break;

        case DISCREG_CR2:
            CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR2);
            *pValue = pVCpu->cpum.s.Guest.cr2;
            break;

        case DISCREG_CR3:
            CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR3);
            *pValue = pVCpu->cpum.s.Guest.cr3;
            break;

        case DISCREG_CR4:
            CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR4);
            *pValue = pVCpu->cpum.s.Guest.cr4;
            break;

        case DISCREG_CR8:
        {
            CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_APIC_TPR);
            uint8_t u8Tpr;
            int rc = APICGetTpr(pVCpu, &u8Tpr, NULL /* pfPending */, NULL /* pu8PendingIrq */);
            if (RT_FAILURE(rc))
            {
                AssertMsg(rc == VERR_PDM_NO_APIC_INSTANCE, ("%Rrc\n", rc));
                *pValue = 0;
                return rc;
            }
            *pValue = u8Tpr >> 4; /* bits 7-4 contain the task priority that go in cr8, bits 3-0 */
            break;
        }

        default:
            return VERR_INVALID_PARAMETER;
    }
    return VINF_SUCCESS;
}


VMMDECL(uint64_t) CPUMGetGuestDR0(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_DR0_DR3);
    return pVCpu->cpum.s.Guest.dr[0];
}


VMMDECL(uint64_t) CPUMGetGuestDR1(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_DR0_DR3);
    return pVCpu->cpum.s.Guest.dr[1];
}


VMMDECL(uint64_t) CPUMGetGuestDR2(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_DR0_DR3);
    return pVCpu->cpum.s.Guest.dr[2];
}


VMMDECL(uint64_t) CPUMGetGuestDR3(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_DR0_DR3);
    return pVCpu->cpum.s.Guest.dr[3];
}


VMMDECL(uint64_t) CPUMGetGuestDR6(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_DR6);
    return pVCpu->cpum.s.Guest.dr[6];
}


VMMDECL(uint64_t) CPUMGetGuestDR7(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_DR7);
    return pVCpu->cpum.s.Guest.dr[7];
}


VMMDECL(int) CPUMGetGuestDRx(PCVMCPU pVCpu, uint32_t iReg, uint64_t *pValue)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_DR_MASK);
    AssertReturn(iReg <= DISDREG_DR7, VERR_INVALID_PARAMETER);
    /* DR4 is an alias for DR6, and DR5 is an alias for DR7. */
    if (iReg == 4 || iReg == 5)
        iReg += 2;
    *pValue = pVCpu->cpum.s.Guest.dr[iReg];
    return VINF_SUCCESS;
}


VMMDECL(uint64_t) CPUMGetGuestEFER(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_EFER);
    return pVCpu->cpum.s.Guest.msrEFER;
}


/**
 * Looks up a CPUID leaf in the CPUID leaf array, no subleaf.
 *
 * @returns Pointer to the leaf if found, NULL if not.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   uLeaf               The leaf to get.
 */
PCPUMCPUIDLEAF cpumCpuIdGetLeaf(PVM pVM, uint32_t uLeaf)
{
    unsigned            iEnd     = RT_MIN(pVM->cpum.s.GuestInfo.cCpuIdLeaves, RT_ELEMENTS(pVM->cpum.s.GuestInfo.aCpuIdLeaves));
    if (iEnd)
    {
        unsigned        iStart   = 0;
        PCPUMCPUIDLEAF  paLeaves = pVM->cpum.s.GuestInfo.aCpuIdLeaves;
        for (;;)
        {
            unsigned i = iStart + (iEnd - iStart) / 2U;
            if (uLeaf < paLeaves[i].uLeaf)
            {
                if (i <= iStart)
                    return NULL;
                iEnd = i;
            }
            else if (uLeaf > paLeaves[i].uLeaf)
            {
                i += 1;
                if (i >= iEnd)
                    return NULL;
                iStart = i;
            }
            else
            {
                if (RT_LIKELY(paLeaves[i].fSubLeafMask == 0 && paLeaves[i].uSubLeaf == 0))
                    return &paLeaves[i];

                /* This shouldn't normally happen. But in case the it does due
                   to user configuration overrids or something, just return the
                   first sub-leaf. */
                AssertMsgFailed(("uLeaf=%#x fSubLeafMask=%#x uSubLeaf=%#x\n",
                                 uLeaf, paLeaves[i].fSubLeafMask, paLeaves[i].uSubLeaf));
                while (   paLeaves[i].uSubLeaf != 0
                       && i > 0
                       && uLeaf == paLeaves[i - 1].uLeaf)
                    i--;
                return &paLeaves[i];
            }
        }
    }

    return NULL;
}


/**
 * Looks up a CPUID leaf in the CPUID leaf array.
 *
 * @returns Pointer to the leaf if found, NULL if not.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   uLeaf               The leaf to get.
 * @param   uSubLeaf            The subleaf, if applicable.  Just pass 0 if it
 *                              isn't.
 * @param   pfExactSubLeafHit   Whether we've got an exact subleaf hit or not.
 */
PCPUMCPUIDLEAF cpumCpuIdGetLeafEx(PVM pVM, uint32_t uLeaf, uint32_t uSubLeaf, bool *pfExactSubLeafHit)
{
    unsigned            iEnd     = RT_MIN(pVM->cpum.s.GuestInfo.cCpuIdLeaves, RT_ELEMENTS(pVM->cpum.s.GuestInfo.aCpuIdLeaves));
    if (iEnd)
    {
        unsigned        iStart   = 0;
        PCPUMCPUIDLEAF  paLeaves = pVM->cpum.s.GuestInfo.aCpuIdLeaves;
        for (;;)
        {
            unsigned i = iStart + (iEnd - iStart) / 2U;
            if (uLeaf < paLeaves[i].uLeaf)
            {
                if (i <= iStart)
                    return NULL;
                iEnd = i;
            }
            else if (uLeaf > paLeaves[i].uLeaf)
            {
                i += 1;
                if (i >= iEnd)
                    return NULL;
                iStart = i;
            }
            else
            {
                uSubLeaf &= paLeaves[i].fSubLeafMask;
                if (uSubLeaf == paLeaves[i].uSubLeaf)
                    *pfExactSubLeafHit = true;
                else
                {
                    /* Find the right subleaf.  We return the last one before
                       uSubLeaf if we don't find an exact match. */
                    if (uSubLeaf < paLeaves[i].uSubLeaf)
                        while (   i > 0
                               && uLeaf    == paLeaves[i - 1].uLeaf
                               && uSubLeaf <= paLeaves[i - 1].uSubLeaf)
                            i--;
                    else
                        while (   i + 1 < pVM->cpum.s.GuestInfo.cCpuIdLeaves
                               && uLeaf    == paLeaves[i + 1].uLeaf
                               && uSubLeaf >= paLeaves[i + 1].uSubLeaf)
                            i++;
                    *pfExactSubLeafHit = uSubLeaf == paLeaves[i].uSubLeaf;
                }
                return &paLeaves[i];
            }
        }
    }

    *pfExactSubLeafHit = false;
    return NULL;
}


/**
 * Gets a CPUID leaf.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uLeaf       The CPUID leaf to get.
 * @param   uSubLeaf    The CPUID sub-leaf to get, if applicable.
 * @param   f64BitMode  A tristate indicate if the caller is in 64-bit mode or
 *                      not: 1=true, 0=false, 1=whatever.  This affect how the
 *                      X86_CPUID_EXT_FEATURE_EDX_SYSCALL flag is returned on
 *                      Intel CPUs, where it's only returned in 64-bit mode.
 * @param   pEax        Where to store the EAX value.
 * @param   pEbx        Where to store the EBX value.
 * @param   pEcx        Where to store the ECX value.
 * @param   pEdx        Where to store the EDX value.
 */
VMMDECL(void) CPUMGetGuestCpuId(PVMCPUCC pVCpu, uint32_t uLeaf, uint32_t uSubLeaf, int f64BitMode,
                                uint32_t *pEax, uint32_t *pEbx, uint32_t *pEcx, uint32_t *pEdx)
{
    bool            fExactSubLeafHit;
    PVM             pVM   = pVCpu->CTX_SUFF(pVM);
    PCCPUMCPUIDLEAF pLeaf = cpumCpuIdGetLeafEx(pVM, uLeaf, uSubLeaf, &fExactSubLeafHit);
    if (pLeaf)
    {
        AssertMsg(pLeaf->uLeaf == uLeaf, ("%#x %#x\n", pLeaf->uLeaf, uLeaf));
        if (fExactSubLeafHit)
        {
            *pEax = pLeaf->uEax;
            *pEbx = pLeaf->uEbx;
            *pEcx = pLeaf->uEcx;
            *pEdx = pLeaf->uEdx;

            /*
             * Deal with CPU specific information.
             */
            if (pLeaf->fFlags & (  CPUMCPUIDLEAF_F_CONTAINS_APIC_ID
                                 | CPUMCPUIDLEAF_F_CONTAINS_OSXSAVE
                                 | CPUMCPUIDLEAF_F_CONTAINS_APIC ))
            {
                if (uLeaf == 1)
                {
                    /* EBX: Bits 31-24: Initial APIC ID. */
                    Assert(pVCpu->idCpu <= 255);
                    AssertMsg((pLeaf->uEbx >> 24) == 0, ("%#x\n", pLeaf->uEbx)); /* raw-mode assumption */
                    *pEbx = (pLeaf->uEbx & UINT32_C(0x00ffffff)) | (pVCpu->idCpu << 24);

                    /* EDX: Bit 9: AND with APICBASE.EN. */
                    if (!pVCpu->cpum.s.fCpuIdApicFeatureVisible && (pLeaf->fFlags & CPUMCPUIDLEAF_F_CONTAINS_APIC))
                        *pEdx &= ~X86_CPUID_FEATURE_EDX_APIC;

                    /* ECX: Bit 27: CR4.OSXSAVE mirror. */
                    *pEcx = (pLeaf->uEcx & ~X86_CPUID_FEATURE_ECX_OSXSAVE)
                          | (pVCpu->cpum.s.Guest.cr4 & X86_CR4_OSXSAVE ? X86_CPUID_FEATURE_ECX_OSXSAVE : 0);
                }
                else if (uLeaf == 0xb)
                {
                    /* EDX: Initial extended APIC ID. */
                    AssertMsg(pLeaf->uEdx == 0, ("%#x\n", pLeaf->uEdx)); /* raw-mode assumption */
                    *pEdx = pVCpu->idCpu;
                    Assert(!(pLeaf->fFlags & ~(CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES)));
                }
                else if (uLeaf == UINT32_C(0x8000001e))
                {
                    /* EAX: Initial extended APIC ID. */
                    AssertMsg(pLeaf->uEax == 0, ("%#x\n", pLeaf->uEax)); /* raw-mode assumption */
                    *pEax = pVCpu->idCpu;
                    Assert(!(pLeaf->fFlags & ~CPUMCPUIDLEAF_F_CONTAINS_APIC_ID));
                }
                else if (uLeaf == UINT32_C(0x80000001))
                {
                    /* EDX: Bit 9: AND with APICBASE.EN. */
                    if (!pVCpu->cpum.s.fCpuIdApicFeatureVisible)
                        *pEdx &= ~X86_CPUID_AMD_FEATURE_EDX_APIC;
                    Assert(!(pLeaf->fFlags & ~CPUMCPUIDLEAF_F_CONTAINS_APIC));
                }
                else
                    AssertMsgFailed(("uLeaf=%#x\n", uLeaf));
            }

            /* Intel CPUs suppress the SYSCALL bit when not executing in 64-bit mode: */
            if (   uLeaf == UINT32_C(0x80000001)
                && f64BitMode == false
                && (*pEdx & X86_CPUID_EXT_FEATURE_EDX_SYSCALL)
                && (   pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_INTEL
                    || pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_VIA /*?*/
                    || pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_SHANGHAI /*?*/ ) )
                *pEdx &= ~X86_CPUID_EXT_FEATURE_EDX_SYSCALL;

        }
        /*
         * Out of range sub-leaves aren't quite as easy and pretty as we emulate
         * them here, but we do the best we can here...
         */
        else
        {
            *pEax = *pEbx = *pEcx = *pEdx = 0;
            if (pLeaf->fFlags & CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES)
            {
                *pEcx = uSubLeaf & 0xff;
                *pEdx = pVCpu->idCpu;
            }
        }
    }
    else
    {
        /*
         * Different CPUs have different ways of dealing with unknown CPUID leaves.
         */
        switch (pVM->cpum.s.GuestInfo.enmUnknownCpuIdMethod)
        {
            default:
                AssertFailed();
                RT_FALL_THRU();
            case CPUMUNKNOWNCPUID_DEFAULTS:
            case CPUMUNKNOWNCPUID_LAST_STD_LEAF: /* ASSUME this is executed */
            case CPUMUNKNOWNCPUID_LAST_STD_LEAF_WITH_ECX: /** @todo Implement CPUMUNKNOWNCPUID_LAST_STD_LEAF_WITH_ECX */
                *pEax = pVM->cpum.s.GuestInfo.DefCpuId.uEax;
                *pEbx = pVM->cpum.s.GuestInfo.DefCpuId.uEbx;
                *pEcx = pVM->cpum.s.GuestInfo.DefCpuId.uEcx;
                *pEdx = pVM->cpum.s.GuestInfo.DefCpuId.uEdx;
                break;
            case CPUMUNKNOWNCPUID_PASSTHRU:
                *pEax = uLeaf;
                *pEbx = 0;
                *pEcx = uSubLeaf;
                *pEdx = 0;
                break;
        }
    }
    Log2(("CPUMGetGuestCpuId: uLeaf=%#010x/%#010x %RX32 %RX32 %RX32 %RX32\n", uLeaf, uSubLeaf, *pEax, *pEbx, *pEcx, *pEdx));
}


/**
 * Sets the visibility of the X86_CPUID_FEATURE_EDX_APIC and
 * X86_CPUID_AMD_FEATURE_EDX_APIC CPUID bits.
 *
 * @returns Previous value.
 * @param   pVCpu       The cross context virtual CPU structure to make the
 *                      change on.  Usually the calling EMT.
 * @param   fVisible    Whether to make it visible (true) or hide it (false).
 *
 * @remarks This is "VMMDECL" so that it still links with
 *          the old APIC code which is in VBoxDD2 and not in
 *          the VMM module.
 */
VMMDECL(bool) CPUMSetGuestCpuIdPerCpuApicFeature(PVMCPU pVCpu, bool fVisible)
{
    bool fOld = pVCpu->cpum.s.fCpuIdApicFeatureVisible;
    pVCpu->cpum.s.fCpuIdApicFeatureVisible = fVisible;
    return fOld;
}


/**
 * Gets the host CPU vendor.
 *
 * @returns CPU vendor.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(CPUMCPUVENDOR) CPUMGetHostCpuVendor(PVM pVM)
{
    return (CPUMCPUVENDOR)pVM->cpum.s.HostFeatures.enmCpuVendor;
}


/**
 * Gets the host CPU microarchitecture.
 *
 * @returns CPU microarchitecture.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(CPUMMICROARCH) CPUMGetHostMicroarch(PCVM pVM)
{
    return pVM->cpum.s.HostFeatures.enmMicroarch;
}


/**
 * Gets the guest CPU vendor.
 *
 * @returns CPU vendor.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(CPUMCPUVENDOR) CPUMGetGuestCpuVendor(PVM pVM)
{
    return (CPUMCPUVENDOR)pVM->cpum.s.GuestFeatures.enmCpuVendor;
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
    return kCpumArch_X86; /* Static as we are in the x86 VMM module here. */
}


/**
 * Gets the guest CPU microarchitecture.
 *
 * @returns CPU microarchitecture.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(CPUMMICROARCH) CPUMGetGuestMicroarch(PCVM pVM)
{
    return pVM->cpum.s.GuestFeatures.enmMicroarch;
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


VMMDECL(int) CPUMSetGuestDR0(PVMCPUCC pVCpu, uint64_t uDr0)
{
    pVCpu->cpum.s.Guest.dr[0] = uDr0;
    return CPUMRecalcHyperDRx(pVCpu, 0);
}


VMMDECL(int) CPUMSetGuestDR1(PVMCPUCC pVCpu, uint64_t uDr1)
{
    pVCpu->cpum.s.Guest.dr[1] = uDr1;
    return CPUMRecalcHyperDRx(pVCpu, 1);
}


VMMDECL(int) CPUMSetGuestDR2(PVMCPUCC pVCpu, uint64_t uDr2)
{
    pVCpu->cpum.s.Guest.dr[2] = uDr2;
    return CPUMRecalcHyperDRx(pVCpu, 2);
}


VMMDECL(int) CPUMSetGuestDR3(PVMCPUCC pVCpu, uint64_t uDr3)
{
    pVCpu->cpum.s.Guest.dr[3] = uDr3;
    return CPUMRecalcHyperDRx(pVCpu, 3);
}


VMMDECL(int) CPUMSetGuestDR6(PVMCPU pVCpu, uint64_t uDr6)
{
    pVCpu->cpum.s.Guest.dr[6] = uDr6;
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_DR6;
    return VINF_SUCCESS; /* No need to recalc. */
}


VMMDECL(int) CPUMSetGuestDR7(PVMCPUCC pVCpu, uint64_t uDr7)
{
    pVCpu->cpum.s.Guest.dr[7] = uDr7;
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_DR7;
    return CPUMRecalcHyperDRx(pVCpu, 7);
}


VMMDECL(int) CPUMSetGuestDRx(PVMCPUCC pVCpu, uint32_t iReg, uint64_t Value)
{
    AssertReturn(iReg <= DISDREG_DR7, VERR_INVALID_PARAMETER);
    /* DR4 is an alias for DR6, and DR5 is an alias for DR7. */
    if (iReg == 4 || iReg == 5)
        iReg += 2;
    pVCpu->cpum.s.Guest.dr[iReg] = Value;
    return CPUMRecalcHyperDRx(pVCpu, iReg);
}


/**
 * Recalculates the hypervisor DRx register values based on current guest
 * registers and DBGF breakpoints, updating changed registers depending on the
 * context.
 *
 * This is called whenever a guest DRx register is modified (any context) and
 * when DBGF sets a hardware breakpoint (ring-3 only, rendezvous).
 *
 * In raw-mode context this function will reload any (hyper) DRx registers which
 * comes out with a different value.  It may also have to save the host debug
 * registers if that haven't been done already.  In this context though, we'll
 * be intercepting and emulating all DRx accesses, so the hypervisor DRx values
 * are only important when breakpoints are actually enabled.
 *
 * In ring-0 (HM) context DR0-3 will be relocated by us, while DR7 will be
 * reloaded by the HM code if it changes.  Further more, we will only use the
 * combined register set when the VBox debugger is actually using hardware BPs,
 * when it isn't we'll keep the guest DR0-3 + (maybe) DR6 loaded (DR6 doesn't
 * concern us here).
 *
 * In ring-3 we won't be loading anything, so well calculate hypervisor values
 * all the time.
 *
 * @returns VINF_SUCCESS.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   iGstReg     The guest debug register number that was modified.
 *                      UINT8_MAX if not guest register.
 */
VMMDECL(int) CPUMRecalcHyperDRx(PVMCPUCC pVCpu, uint8_t iGstReg)
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);
#ifndef IN_RING0
    RT_NOREF_PV(iGstReg);
#endif

    /*
     * Compare the DR7s first.
     *
     * We only care about the enabled flags.  GD is virtualized when we
     * dispatch the #DB, we never enable it.  The DBGF DR7 value is will
     * always have the LE and GE bits set, so no need to check and disable
     * stuff if they're cleared like we have to for the guest DR7.
     */
    const RTGCUINTREG uDbgfDr7 = DBGFBpGetDR7(pVM);
    if (uDbgfDr7 & X86_DR7_ENABLED_MASK)
    {
        Assert(!CPUMIsGuestDebugStateActive(pVCpu));

        RTGCUINTREG uGstDr7 = CPUMGetGuestDR7(pVCpu);
        /** @todo This isn't correct. BPs work without setting LE and GE under AMD-V.  They are also documented as unsupported by P6+. */
        if (!(uGstDr7 & (X86_DR7_LE | X86_DR7_GE)))
            uGstDr7 = 0;
        else if (!(uGstDr7 & X86_DR7_LE))
            uGstDr7 &= ~X86_DR7_LE_ALL;
        else if (!(uGstDr7 & X86_DR7_GE))
            uGstDr7 &= ~X86_DR7_GE_ALL;

        /*
         * Ok, something is enabled.  Recalc each of the breakpoints, taking
         * the VM debugger ones of the guest ones.  In raw-mode context we will
         * not allow breakpoints with values inside the hypervisor area.
         */
        RTGCUINTREG uNewDr7 = X86_DR7_GE | X86_DR7_LE | X86_DR7_RA1_MASK;

        /* bp 0 */
        RTGCUINTREG uNewDr0;
        if (uDbgfDr7 & (X86_DR7_L0 | X86_DR7_G0))
        {
            uNewDr7 |= uDbgfDr7 & (X86_DR7_L0 | X86_DR7_G0 | X86_DR7_RW0_MASK | X86_DR7_LEN0_MASK);
            uNewDr0 = DBGFBpGetDR0(pVM);
        }
        else if (uGstDr7 & (X86_DR7_L0 | X86_DR7_G0))
        {
            uNewDr0 = CPUMGetGuestDR0(pVCpu);
            uNewDr7 |= uGstDr7 & (X86_DR7_L0 | X86_DR7_G0 | X86_DR7_RW0_MASK | X86_DR7_LEN0_MASK);
        }
        else
            uNewDr0 = 0;

        /* bp 1 */
        RTGCUINTREG uNewDr1;
        if (uDbgfDr7 & (X86_DR7_L1 | X86_DR7_G1))
        {
            uNewDr7 |= uDbgfDr7 & (X86_DR7_L1 | X86_DR7_G1 | X86_DR7_RW1_MASK | X86_DR7_LEN1_MASK);
            uNewDr1 = DBGFBpGetDR1(pVM);
        }
        else if (uGstDr7 & (X86_DR7_L1 | X86_DR7_G1))
        {
            uNewDr1 = CPUMGetGuestDR1(pVCpu);
            uNewDr7 |= uGstDr7 & (X86_DR7_L1 | X86_DR7_G1 | X86_DR7_RW1_MASK | X86_DR7_LEN1_MASK);
        }
        else
            uNewDr1 = 0;

        /* bp 2 */
        RTGCUINTREG uNewDr2;
        if (uDbgfDr7 & (X86_DR7_L2 | X86_DR7_G2))
        {
            uNewDr7 |= uDbgfDr7 & (X86_DR7_L2 | X86_DR7_G2 | X86_DR7_RW2_MASK | X86_DR7_LEN2_MASK);
            uNewDr2 = DBGFBpGetDR2(pVM);
        }
        else if (uGstDr7 & (X86_DR7_L2 | X86_DR7_G2))
        {
            uNewDr2 = CPUMGetGuestDR2(pVCpu);
            uNewDr7 |= uGstDr7 & (X86_DR7_L2 | X86_DR7_G2 | X86_DR7_RW2_MASK | X86_DR7_LEN2_MASK);
        }
        else
            uNewDr2 = 0;

        /* bp 3 */
        RTGCUINTREG uNewDr3;
        if (uDbgfDr7 & (X86_DR7_L3 | X86_DR7_G3))
        {
            uNewDr7 |= uDbgfDr7 & (X86_DR7_L3 | X86_DR7_G3 | X86_DR7_RW3_MASK | X86_DR7_LEN3_MASK);
            uNewDr3 = DBGFBpGetDR3(pVM);
        }
        else if (uGstDr7 & (X86_DR7_L3 | X86_DR7_G3))
        {
            uNewDr3 = CPUMGetGuestDR3(pVCpu);
            uNewDr7 |= uGstDr7 & (X86_DR7_L3 | X86_DR7_G3 | X86_DR7_RW3_MASK | X86_DR7_LEN3_MASK);
        }
        else
            uNewDr3 = 0;

        /*
         * Apply the updates.
         */
        pVCpu->cpum.s.fUseFlags |= CPUM_USE_DEBUG_REGS_HYPER;
        if (uNewDr3 != pVCpu->cpum.s.Hyper.dr[3])
            cpumSetHyperDR3(pVCpu, uNewDr3);
        if (uNewDr2 != pVCpu->cpum.s.Hyper.dr[2])
            cpumSetHyperDR2(pVCpu, uNewDr2);
        if (uNewDr1 != pVCpu->cpum.s.Hyper.dr[1])
            cpumSetHyperDR1(pVCpu, uNewDr1);
        if (uNewDr0 != pVCpu->cpum.s.Hyper.dr[0])
            cpumSetHyperDR0(pVCpu, uNewDr0);
        if (uNewDr7 != pVCpu->cpum.s.Hyper.dr[7])
            CPUMSetHyperDR7(pVCpu, uNewDr7);
    }
#ifdef IN_RING0
    else if (CPUMIsGuestDebugStateActive(pVCpu))
    {
        /*
         * Reload the register that was modified.  Normally this won't happen
         * as we won't intercept DRx writes when not having the hyper debug
         * state loaded, but in case we do for some reason we'll simply deal
         * with it.
         */
        switch (iGstReg)
        {
            case 0: ASMSetDR0(CPUMGetGuestDR0(pVCpu)); break;
            case 1: ASMSetDR1(CPUMGetGuestDR1(pVCpu)); break;
            case 2: ASMSetDR2(CPUMGetGuestDR2(pVCpu)); break;
            case 3: ASMSetDR3(CPUMGetGuestDR3(pVCpu)); break;
            default:
                AssertReturn(iGstReg != UINT8_MAX, VERR_INTERNAL_ERROR_3);
        }
    }
#endif
    else
    {
        /*
         * No active debug state any more.  In raw-mode this means we have to
         * make sure DR7 has everything disabled now, if we armed it already.
         * In ring-0 we might end up here when just single stepping.
         */
#ifdef IN_RING0
        if (pVCpu->cpum.s.fUseFlags & CPUM_USED_DEBUG_REGS_HYPER)
        {
            if (pVCpu->cpum.s.Hyper.dr[0])
                ASMSetDR0(0);
            if (pVCpu->cpum.s.Hyper.dr[1])
                ASMSetDR1(0);
            if (pVCpu->cpum.s.Hyper.dr[2])
                ASMSetDR2(0);
            if (pVCpu->cpum.s.Hyper.dr[3])
                ASMSetDR3(0);
            pVCpu->cpum.s.fUseFlags &= ~CPUM_USED_DEBUG_REGS_HYPER;
        }
#endif
        pVCpu->cpum.s.fUseFlags &= ~CPUM_USE_DEBUG_REGS_HYPER;

        /* Clear all the registers. */
        pVCpu->cpum.s.Hyper.dr[7] = X86_DR7_RA1_MASK;
        pVCpu->cpum.s.Hyper.dr[3] = 0;
        pVCpu->cpum.s.Hyper.dr[2] = 0;
        pVCpu->cpum.s.Hyper.dr[1] = 0;
        pVCpu->cpum.s.Hyper.dr[0] = 0;

    }
    Log2(("CPUMRecalcHyperDRx: fUseFlags=%#x %RGr %RGr %RGr %RGr  %RGr %RGr\n",
          pVCpu->cpum.s.fUseFlags, pVCpu->cpum.s.Hyper.dr[0], pVCpu->cpum.s.Hyper.dr[1],
          pVCpu->cpum.s.Hyper.dr[2], pVCpu->cpum.s.Hyper.dr[3], pVCpu->cpum.s.Hyper.dr[6],
          pVCpu->cpum.s.Hyper.dr[7]));

    return VINF_SUCCESS;
}


/**
 * Set the guest XCR0 register.
 *
 * Will load additional state if the FPU state is already loaded (in ring-0 &
 * raw-mode context).
 *
 * @returns VINF_SUCCESS on success, VERR_CPUM_RAISE_GP_0 on invalid input
 *          value.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uNewValue   The new value.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(int)   CPUMSetGuestXcr0(PVMCPUCC pVCpu, uint64_t uNewValue)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_XCRx);
    if (   (uNewValue & ~pVCpu->CTX_SUFF(pVM)->cpum.s.fXStateGuestMask) == 0
        /* The X87 bit cannot be cleared. */
        && (uNewValue & XSAVE_C_X87)
        /* AVX requires SSE. */
        && (uNewValue & (XSAVE_C_SSE | XSAVE_C_YMM)) != XSAVE_C_YMM
        /* AVX-512 requires YMM, SSE and all of its three components to be enabled. */
        && (   (uNewValue & (XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI)) == 0
            ||    (uNewValue & (XSAVE_C_SSE | XSAVE_C_YMM | XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI))
               ==              (XSAVE_C_SSE | XSAVE_C_YMM | XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI) )
       )
    {
        pVCpu->cpum.s.Guest.aXcr[0] = uNewValue;

        /* If more state components are enabled, we need to take care to load
           them if the FPU/SSE state is already loaded.  May otherwise leak
           host state to the guest. */
        uint64_t fNewComponents = ~pVCpu->cpum.s.Guest.fXStateMask & uNewValue;
        if (fNewComponents)
        {
#ifdef IN_RING0
            if (pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST)
            {
                if (pVCpu->cpum.s.Guest.fXStateMask != 0)
                    /* Adding more components. */
                    ASMXRstor(&pVCpu->cpum.s.Guest.XState, fNewComponents);
                else
                {
                    /* We're switching from FXSAVE/FXRSTOR to XSAVE/XRSTOR. */
                    pVCpu->cpum.s.Guest.fXStateMask |= XSAVE_C_X87 | XSAVE_C_SSE;
                    if (uNewValue & ~(XSAVE_C_X87 | XSAVE_C_SSE))
                        ASMXRstor(&pVCpu->cpum.s.Guest.XState, uNewValue & ~(XSAVE_C_X87 | XSAVE_C_SSE));
                }
            }
#endif
            pVCpu->cpum.s.Guest.fXStateMask |= uNewValue;
        }
        return VINF_SUCCESS;
    }
    return VERR_CPUM_RAISE_GP_0;
}


/**
 * Tests if the guest has No-Execute Page Protection Enabled (NXE).
 *
 * @returns true if in real mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestNXEnabled(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_EFER);
    return !!(pVCpu->cpum.s.Guest.msrEFER & MSR_K6_EFER_NXE);
}


/**
 * Tests if the guest has the Page Size Extension enabled (PSE).
 *
 * @returns true if in real mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestPageSizeExtEnabled(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR4);
    /* PAE or AMD64 implies support for big pages regardless of CR4.PSE */
    return !!(pVCpu->cpum.s.Guest.cr4 & (X86_CR4_PSE | X86_CR4_PAE));
}


/**
 * Tests if the guest has the paging enabled (PG).
 *
 * @returns true if in real mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestPagingEnabled(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0);
    return !!(pVCpu->cpum.s.Guest.cr0 & X86_CR0_PG);
}


/**
 * Tests if the guest has the paging enabled (PG).
 *
 * @returns true if in real mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestR0WriteProtEnabled(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0);
    return !!(pVCpu->cpum.s.Guest.cr0 & X86_CR0_WP);
}


/**
 * Tests if the guest is running in real mode or not.
 *
 * @returns true if in real mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestInRealMode(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0);
    return !(pVCpu->cpum.s.Guest.cr0 & X86_CR0_PE);
}


/**
 * Tests if the guest is running in real or virtual 8086 mode.
 *
 * @returns @c true if it is, @c false if not.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestInRealOrV86Mode(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_RFLAGS);
    return !(pVCpu->cpum.s.Guest.cr0 & X86_CR0_PE)
        || pVCpu->cpum.s.Guest.eflags.Bits.u1VM; /** @todo verify that this cannot be set in long mode. */
}


/**
 * Tests if the guest is running in protected or not.
 *
 * @returns true if in protected mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestInProtectedMode(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0);
    return !!(pVCpu->cpum.s.Guest.cr0 & X86_CR0_PE);
}


/**
 * Tests if the guest is running in paged protected or not.
 *
 * @returns true if in paged protected mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestInPagedProtectedMode(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0);
    return (pVCpu->cpum.s.Guest.cr0 & (X86_CR0_PE | X86_CR0_PG)) == (X86_CR0_PE | X86_CR0_PG);
}


/**
 * Tests if the guest is running in long mode or not.
 *
 * @returns true if in long mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestInLongMode(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_EFER);
    return (pVCpu->cpum.s.Guest.msrEFER & MSR_K6_EFER_LMA) == MSR_K6_EFER_LMA;
}


/**
 * Tests if the guest is running in PAE mode or not.
 *
 * @returns true if in PAE mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestInPAEMode(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_EFER);
    /* Intel mentions EFER.LMA and EFER.LME in different parts of their spec. We shall use EFER.LMA rather
       than EFER.LME as it reflects if the CPU has entered paging with EFER.LME set.  */
    return (pVCpu->cpum.s.Guest.cr4 & X86_CR4_PAE)
        && (pVCpu->cpum.s.Guest.cr0 & X86_CR0_PG)
        && !(pVCpu->cpum.s.Guest.msrEFER & MSR_K6_EFER_LMA);
}


/**
 * Tests if the guest is running in 64 bits mode or not.
 *
 * @returns true if in 64 bits protected mode, otherwise false.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
VMMDECL(bool) CPUMIsGuestIn64BitCode(PCVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_EFER);
    if (!CPUMIsGuestInLongMode(pVCpu))
        return false;
    CPUMSELREG_LAZY_LOAD_HIDDEN_PARTS(pVCpu, &pVCpu->cpum.s.Guest.cs);
    return pVCpu->cpum.s.Guest.cs.Attr.n.u1Long;
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
 * Checks if the CPU supports the XSAVE and XRSTOR instruction.
 *
 * @returns true if supported.
 * @returns false if not supported.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(bool) CPUMSupportsXSave(PVM pVM)
{
    return pVM->cpum.s.HostFeatures.fXSaveRstor != 0;
}


/**
 * Checks if the host OS uses the SYSENTER / SYSEXIT instructions.
 * @returns true if used.
 * @returns false if not used.
 * @param   pVM       The cross context VM structure.
 */
VMMDECL(bool) CPUMIsHostUsingSysEnter(PVM pVM)
{
    return RT_BOOL(pVM->cpum.s.fHostUseFlags & CPUM_USE_SYSENTER);
}


/**
 * Checks if the host OS uses the SYSCALL / SYSRET instructions.
 * @returns true if used.
 * @returns false if not used.
 * @param   pVM       The cross context VM structure.
 */
VMMDECL(bool) CPUMIsHostUsingSysCall(PVM pVM)
{
    return RT_BOOL(pVM->cpum.s.fHostUseFlags & CPUM_USE_SYSCALL);
}


/**
 * Checks if we activated the FPU/XMM state of the guest OS.
 *
 * Obsolete: This differs from CPUMIsGuestFPUStateLoaded() in that it refers to
 * the next time we'll be executing guest code, so it may return true for
 * 64-on-32 when we still haven't actually loaded the FPU status, just scheduled
 * it to be loaded the next time we go thru the world switcher
 * (CPUM_SYNC_FPU_STATE).
 *
 * @returns true / false.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestFPUStateActive(PVMCPU pVCpu)
{
    bool fRet = RT_BOOL(pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST);
    AssertMsg(fRet == pVCpu->cpum.s.Guest.fUsedFpuGuest, ("fRet=%d\n", fRet));
    return fRet;
}


/**
 * Checks if we've really loaded the FPU/XMM state of the guest OS.
 *
 * @returns true / false.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsGuestFPUStateLoaded(PVMCPU pVCpu)
{
    bool fRet = RT_BOOL(pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST);
    AssertMsg(fRet == pVCpu->cpum.s.Guest.fUsedFpuGuest, ("fRet=%d\n", fRet));
    return fRet;
}


/**
 * Checks if we saved the FPU/XMM state of the host OS.
 *
 * @returns true / false.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMMDECL(bool) CPUMIsHostFPUStateSaved(PVMCPU pVCpu)
{
    return RT_BOOL(pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_HOST);
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
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @todo    This API doesn't make sense any more.
 */
VMMDECL(void) CPUMDeactivateGuestDebugState(PVMCPU pVCpu)
{
    Assert(!(pVCpu->cpum.s.fUseFlags & (CPUM_USED_DEBUG_REGS_GUEST | CPUM_USED_DEBUG_REGS_HYPER | CPUM_USED_DEBUG_REGS_HOST)));
    NOREF(pVCpu);
}


/**
 * Get the current privilege level of the guest.
 *
 * @returns CPL
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
VMMDECL(uint32_t) CPUMGetGuestCPL(PVMCPU pVCpu)
{
    /*
     * CPL can reliably be found in SS.DPL (hidden regs valid) or SS if not.
     *
     * Note! We used to check CS.DPL here, assuming it was always equal to
     * CPL even if a conforming segment was loaded.  But this turned out to
     * only apply to older AMD-V.  With VT-x we had an ACP2 regression
     * during install after a far call to ring 2 with VT-x.  Then on newer
     * AMD-V CPUs we have to move the VMCB.guest.u8CPL into cs.Attr.n.u2Dpl
     * as well as ss.Attr.n.u2Dpl to make this (and other) code work right.
     *
     * So, forget CS.DPL, always use SS.DPL.
     *
     * Note! The SS RPL is always equal to the CPL, while the CS RPL
     * isn't necessarily equal if the segment is conforming.
     * See section 4.11.1 in the AMD manual.
     *
     * Update: Where the heck does it say CS.RPL can differ from CPL other than
     *         right after real->prot mode switch and when in V8086 mode?  That
     *         section says the RPL specified in a direct transfere (call, jmp,
     *         ret) is not the one loaded into CS. Besides, if CS.RPL != CPL
     *         it would be impossible for an exception handle or the iret
     *         instruction to figure out whether SS:ESP are part of the frame
     *         or not.  VBox or qemu bug must've lead to this misconception.
     *
     * Update2: On an AMD bulldozer system here, I've no trouble loading a null
     *         selector into SS with an RPL other than the CPL when CPL != 3 and
     *         we're in 64-bit mode.  The intel dev box doesn't allow this, on
     *         RPL = CPL.  Weird.
     */
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_SS);
    uint32_t uCpl;
    if (pVCpu->cpum.s.Guest.cr0 & X86_CR0_PE)
    {
        if (!pVCpu->cpum.s.Guest.eflags.Bits.u1VM)
        {
            if (CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.s.Guest.ss))
                uCpl = pVCpu->cpum.s.Guest.ss.Attr.n.u2Dpl;
            else
                uCpl = (pVCpu->cpum.s.Guest.ss.Sel & X86_SEL_RPL);
        }
        else
            uCpl = 3; /* V86 has CPL=3; REM doesn't set DPL=3 in V8086 mode. See @bugref{5130}. */
    }
    else
        uCpl = 0;     /* Real mode is zero; CPL set to 3 for VT-x real-mode emulation. */
    return uCpl;
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
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_EFER);
    CPUMMODE enmMode;
    if (!(pVCpu->cpum.s.Guest.cr0 & X86_CR0_PE))
        enmMode = CPUMMODE_REAL;
    else if (!(pVCpu->cpum.s.Guest.msrEFER & MSR_K6_EFER_LMA))
        enmMode = CPUMMODE_PROTECTED;
    else
        enmMode = CPUMMODE_LONG;

    return enmMode;
}


/**
 * Figure whether the CPU is currently executing 16, 32 or 64 bit code.
 *
 * @returns 16, 32 or 64.
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 */
VMMDECL(uint32_t)       CPUMGetGuestCodeBits(PVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_EFER | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_CS);

    if (!(pVCpu->cpum.s.Guest.cr0 & X86_CR0_PE))
        return 16;

    if (pVCpu->cpum.s.Guest.eflags.Bits.u1VM)
    {
        Assert(!(pVCpu->cpum.s.Guest.msrEFER & MSR_K6_EFER_LMA));
        return 16;
    }

    CPUMSELREG_LAZY_LOAD_HIDDEN_PARTS(pVCpu, &pVCpu->cpum.s.Guest.cs);
    if (   pVCpu->cpum.s.Guest.cs.Attr.n.u1Long
        && (pVCpu->cpum.s.Guest.msrEFER & MSR_K6_EFER_LMA))
        return 64;

    if (pVCpu->cpum.s.Guest.cs.Attr.n.u1DefBig)
        return 32;

    return 16;
}


VMMDECL(DISCPUMODE)     CPUMGetGuestDisMode(PVMCPU pVCpu)
{
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_EFER | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_CS);

    if (!(pVCpu->cpum.s.Guest.cr0 & X86_CR0_PE))
        return DISCPUMODE_16BIT;

    if (pVCpu->cpum.s.Guest.eflags.Bits.u1VM)
    {
        Assert(!(pVCpu->cpum.s.Guest.msrEFER & MSR_K6_EFER_LMA));
        return DISCPUMODE_16BIT;
    }

    CPUMSELREG_LAZY_LOAD_HIDDEN_PARTS(pVCpu, &pVCpu->cpum.s.Guest.cs);
    if (   pVCpu->cpum.s.Guest.cs.Attr.n.u1Long
        && (pVCpu->cpum.s.Guest.msrEFER & MSR_K6_EFER_LMA))
        return DISCPUMODE_64BIT;

    if (pVCpu->cpum.s.Guest.cs.Attr.n.u1DefBig)
        return DISCPUMODE_32BIT;

    return DISCPUMODE_16BIT;
}


/**
 * Gets the guest MXCSR_MASK value.
 *
 * This does not access the x87 state, but the value we determined at VM
 * initialization.
 *
 * @returns MXCSR mask.
 * @param   pVM                 The cross context VM structure.
 */
VMMDECL(uint32_t) CPUMGetGuestMxCsrMask(PVM pVM)
{
    return pVM->cpum.s.GuestInfo.fMxCsrMask;
}


/**
 * Returns whether the guest has physical interrupts enabled.
 *
 * @returns @c true if interrupts are enabled, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 *
 * @remarks Warning! This function does -not- take into account the global-interrupt
 *          flag (GIF).
 */
VMM_INT_DECL(bool) CPUMIsGuestPhysIntrEnabled(PVMCPU pVCpu)
{
    switch (CPUMGetGuestInNestedHwvirtMode(&pVCpu->cpum.s.Guest))
    {
        case CPUMHWVIRT_NONE:
        default:
            return pVCpu->cpum.s.Guest.eflags.Bits.u1IF;
        case CPUMHWVIRT_VMX:
            return CPUMIsGuestVmxPhysIntrEnabled(&pVCpu->cpum.s.Guest);
        case CPUMHWVIRT_SVM:
            return CPUMIsGuestSvmPhysIntrEnabled(pVCpu, &pVCpu->cpum.s.Guest);
    }
}


/**
 * Returns whether the nested-guest has virtual interrupts enabled.
 *
 * @returns @c true if interrupts are enabled, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 *
 * @remarks Warning! This function does -not- take into account the global-interrupt
 *          flag (GIF).
 */
VMM_INT_DECL(bool) CPUMIsGuestVirtIntrEnabled(PVMCPU pVCpu)
{
    PCCPUMCTX pCtx = &pVCpu->cpum.s.Guest;
    Assert(CPUMIsGuestInNestedHwvirtMode(pCtx));

    if (CPUMIsGuestInVmxNonRootMode(pCtx))
        return CPUMIsGuestVmxVirtIntrEnabled(pCtx);

    Assert(CPUMIsGuestInSvmNestedHwVirtMode(pCtx));
    return CPUMIsGuestSvmVirtIntrEnabled(pVCpu, pCtx);
}


/**
 * Calculates the interruptiblity of the guest.
 *
 * @returns Interruptibility level.
 * @param   pVCpu               The cross context virtual CPU structure.
 */
VMM_INT_DECL(CPUMINTERRUPTIBILITY) CPUMGetGuestInterruptibility(PVMCPU pVCpu)
{
#if 1
    /* Global-interrupt flag blocks pretty much everything we care about here. */
    if (CPUMGetGuestGif(&pVCpu->cpum.s.Guest))
    {
        /*
         * Physical interrupts are primarily blocked using EFLAGS. However, we cannot access
         * it directly here. If and how EFLAGS are used depends on the context (nested-guest
         * or raw-mode). Hence we use the function below which handles the details.
         */
        if (   !(pVCpu->cpum.s.Guest.eflags.uBoth & CPUMCTX_INHIBIT_ALL_MASK)
            || (   !(pVCpu->cpum.s.Guest.eflags.uBoth & CPUMCTX_INHIBIT_NMI)
                && pVCpu->cpum.s.Guest.uRipInhibitInt != pVCpu->cpum.s.Guest.rip))
        {
            /** @todo OPT: this next call should be inlined! */
            if (CPUMIsGuestPhysIntrEnabled(pVCpu))
            {
                /** @todo OPT: type this out as it repeats tests. */
                if (   !CPUMIsGuestInNestedHwvirtMode(&pVCpu->cpum.s.Guest)
                    || CPUMIsGuestVirtIntrEnabled(pVCpu))
                    return CPUMINTERRUPTIBILITY_UNRESTRAINED;

                /* Physical interrupts are enabled, but nested-guest virtual interrupts are disabled. */
                return CPUMINTERRUPTIBILITY_VIRT_INT_DISABLED;
            }
            return CPUMINTERRUPTIBILITY_INT_DISABLED;
        }

        /*
         * Blocking the delivery of NMIs during an interrupt shadow is CPU implementation
         * specific. Therefore, in practice, we can't deliver an NMI in an interrupt shadow.
         * However, there is some uncertainity regarding the converse, i.e. whether
         * NMI-blocking until IRET blocks delivery of physical interrupts.
         *
         * See Intel spec. 25.4.1 "Event Blocking".
         */
        /** @todo r=bird: The above comment mixes up VMX root-mode and non-root. Section
         *        25.4.1 is only applicable to VMX non-root mode.  In root mode /
         *        non-VMX mode, I have not see any evidence in the intel manuals that
         *        NMIs are not blocked when in an interrupt shadow. Section "6.7
         *        NONMASKABLE INTERRUPT (NMI)" in SDM 3A seems pretty clear to me.
         */
        if (!(pVCpu->cpum.s.Guest.eflags.uBoth & CPUMCTX_INHIBIT_NMI))
            return CPUMINTERRUPTIBILITY_INT_INHIBITED;
        return CPUMINTERRUPTIBILITY_NMI_INHIBIT;
    }
    return CPUMINTERRUPTIBILITY_GLOBAL_INHIBIT;
#else
    if (pVCpu->cpum.s.Guest.rflags.Bits.u1IF)
    {
        if (pVCpu->cpum.s.Guest.hwvirt.fGif)
        {
            if (!VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_BLOCK_NMIS | VMCPU_FF_INHIBIT_INTERRUPTS))
                return CPUMINTERRUPTIBILITY_UNRESTRAINED;

            /** @todo does blocking NMIs mean interrupts are also inhibited? */
            if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INHIBIT_INTERRUPTS))
            {
                if (!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_BLOCK_NMIS))
                    return CPUMINTERRUPTIBILITY_INT_INHIBITED;
                return CPUMINTERRUPTIBILITY_NMI_INHIBIT;
            }
            AssertFailed();
            return CPUMINTERRUPTIBILITY_NMI_INHIBIT;
        }
        return CPUMINTERRUPTIBILITY_GLOBAL_INHIBIT;
    }
    else
    {
        if (pVCpu->cpum.s.Guest.hwvirt.fGif)
        {
            if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_BLOCK_NMIS))
                return CPUMINTERRUPTIBILITY_NMI_INHIBIT;
            return CPUMINTERRUPTIBILITY_INT_DISABLED;
        }
        return CPUMINTERRUPTIBILITY_GLOBAL_INHIBIT;
    }
#endif
}


/**
 * Checks whether the SVM nested-guest has physical interrupts enabled.
 *
 * @returns true if interrupts are enabled, false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx    The guest-CPU context.
 *
 * @remarks This does -not- take into account the global-interrupt flag.
 */
VMM_INT_DECL(bool) CPUMIsGuestSvmPhysIntrEnabled(PCVMCPU pVCpu, PCCPUMCTX pCtx)
{
    /** @todo Optimization: Avoid this function call and use a pointer to the
     *        relevant eflags instead (setup during VMRUN instruction emulation). */
    Assert(CPUMIsGuestInSvmNestedHwVirtMode(pCtx));

    X86EFLAGS fEFlags;
    if (CPUMIsGuestSvmVirtIntrMasking(pVCpu, pCtx))
        fEFlags.u = pCtx->hwvirt.svm.HostState.rflags.u;
    else
        fEFlags.u = pCtx->eflags.u;

    return fEFlags.Bits.u1IF;
}


/**
 * Checks whether the SVM nested-guest is in a state to receive virtual (setup
 * for injection by VMRUN instruction) interrupts.
 *
 * @returns VBox status code.
 * @retval  true if it's ready, false otherwise.
 *
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx    The guest-CPU context.
 */
VMM_INT_DECL(bool) CPUMIsGuestSvmVirtIntrEnabled(PCVMCPU pVCpu, PCCPUMCTX pCtx)
{
    RT_NOREF(pVCpu);
    Assert(CPUMIsGuestInSvmNestedHwVirtMode(pCtx));

    PCSVMVMCBCTRL pVmcbCtrl    = &pCtx->hwvirt.svm.Vmcb.ctrl;
    PCSVMINTCTRL  pVmcbIntCtrl = &pVmcbCtrl->IntCtrl;
    Assert(!pVmcbIntCtrl->n.u1VGifEnable);      /* We don't support passing virtual-GIF feature to the guest yet. */
    if (   !pVmcbIntCtrl->n.u1IgnoreTPR
        &&  pVmcbIntCtrl->n.u4VIntrPrio <= pVmcbIntCtrl->n.u8VTPR)
        return false;

    return RT_BOOL(pCtx->eflags.u & X86_EFL_IF);
}


/**
 * Gets the pending SVM nested-guest interruptvector.
 *
 * @returns The nested-guest interrupt to inject.
 * @param   pCtx            The guest-CPU context.
 */
VMM_INT_DECL(uint8_t) CPUMGetGuestSvmVirtIntrVector(PCCPUMCTX pCtx)
{
    return pCtx->hwvirt.svm.Vmcb.ctrl.IntCtrl.n.u8VIntrVector;
}


/**
 * Restores the host-state from the host-state save area as part of a \#VMEXIT.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx        The guest-CPU context.
 */
VMM_INT_DECL(void) CPUMSvmVmExitRestoreHostState(PVMCPUCC pVCpu, PCPUMCTX pCtx)
{
    /*
     * Reload the guest's "host state".
     */
    PSVMHOSTSTATE pHostState = &pCtx->hwvirt.svm.HostState;
    pCtx->es         = pHostState->es;
    pCtx->cs         = pHostState->cs;
    pCtx->ss         = pHostState->ss;
    pCtx->ds         = pHostState->ds;
    pCtx->gdtr       = pHostState->gdtr;
    pCtx->idtr       = pHostState->idtr;
    CPUMSetGuestEferMsrNoChecks(pVCpu, pCtx->msrEFER, pHostState->uEferMsr);
    CPUMSetGuestCR0(pVCpu, pHostState->uCr0 | X86_CR0_PE);
    pCtx->cr3        = pHostState->uCr3;
    CPUMSetGuestCR4(pVCpu, pHostState->uCr4);
    pCtx->rflags.u   = pHostState->rflags.u;
    pCtx->rflags.Bits.u1VM = 0;
    pCtx->rip        = pHostState->uRip;
    pCtx->rsp        = pHostState->uRsp;
    pCtx->rax        = pHostState->uRax;
    pCtx->dr[7]     &= ~(X86_DR7_ENABLED_MASK | X86_DR7_RAZ_MASK | X86_DR7_MBZ_MASK);
    pCtx->dr[7]     |= X86_DR7_RA1_MASK;
    Assert(pCtx->ss.Attr.n.u2Dpl == 0);

    /** @todo if RIP is not canonical or outside the CS segment limit, we need to
     *        raise \#GP(0) in the guest. */

    /** @todo check the loaded host-state for consistency. Figure out what
     *        exactly this involves? */
}


/**
 * Saves the host-state to the host-state save area as part of a VMRUN.
 *
 * @param   pCtx        The guest-CPU context.
 * @param   cbInstr     The length of the VMRUN instruction in bytes.
 */
VMM_INT_DECL(void) CPUMSvmVmRunSaveHostState(PCPUMCTX pCtx, uint8_t cbInstr)
{
    PSVMHOSTSTATE pHostState = &pCtx->hwvirt.svm.HostState;
    pHostState->es       = pCtx->es;
    pHostState->cs       = pCtx->cs;
    pHostState->ss       = pCtx->ss;
    pHostState->ds       = pCtx->ds;
    pHostState->gdtr     = pCtx->gdtr;
    pHostState->idtr     = pCtx->idtr;
    pHostState->uEferMsr = pCtx->msrEFER;
    pHostState->uCr0     = pCtx->cr0;
    pHostState->uCr3     = pCtx->cr3;
    pHostState->uCr4     = pCtx->cr4;
    pHostState->rflags.u = pCtx->rflags.u;
    pHostState->uRip     = pCtx->rip + cbInstr;
    pHostState->uRsp     = pCtx->rsp;
    pHostState->uRax     = pCtx->rax;
}


/**
 * Applies the TSC offset of a nested-guest if any and returns the TSC value for the
 * nested-guest.
 *
 * @returns The TSC offset after applying any nested-guest TSC offset.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uTscValue   The guest TSC.
 *
 * @sa      CPUMRemoveNestedGuestTscOffset.
 */
VMM_INT_DECL(uint64_t) CPUMApplyNestedGuestTscOffset(PCVMCPU pVCpu, uint64_t uTscValue)
{
    PCCPUMCTX pCtx = &pVCpu->cpum.s.Guest;
    if (CPUMIsGuestInVmxNonRootMode(pCtx))
    {
        if (CPUMIsGuestVmxProcCtlsSet(pCtx, VMX_PROC_CTLS_USE_TSC_OFFSETTING))
            return uTscValue + pCtx->hwvirt.vmx.Vmcs.u64TscOffset.u;
        return uTscValue;
    }

    if (CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
    {
        uint64_t offTsc;
        if (!HMGetGuestSvmTscOffset(pVCpu, &offTsc))
            offTsc = pCtx->hwvirt.svm.Vmcb.ctrl.u64TSCOffset;
        return uTscValue + offTsc;
    }
    return uTscValue;
}


/**
 * Removes the TSC offset of a nested-guest if any and returns the TSC value for the
 * guest.
 *
 * @returns The TSC offset after removing any nested-guest TSC offset.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uTscValue   The nested-guest TSC.
 *
 * @sa      CPUMApplyNestedGuestTscOffset.
 */
VMM_INT_DECL(uint64_t) CPUMRemoveNestedGuestTscOffset(PCVMCPU pVCpu, uint64_t uTscValue)
{
    PCCPUMCTX pCtx = &pVCpu->cpum.s.Guest;
    if (CPUMIsGuestInVmxNonRootMode(pCtx))
    {
        if (CPUMIsGuestVmxProcCtlsSet(pCtx, VMX_PROC_CTLS_USE_TSC_OFFSETTING))
            return uTscValue - pCtx->hwvirt.vmx.Vmcs.u64TscOffset.u;
        return uTscValue;
    }

    if (CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
    {
        uint64_t offTsc;
        if (!HMGetGuestSvmTscOffset(pVCpu, &offTsc))
            offTsc = pCtx->hwvirt.svm.Vmcb.ctrl.u64TSCOffset;
        return uTscValue - offTsc;
    }
    return uTscValue;
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

            case CPUMCTX_EXTRN_KEEPER_HM:
            {
#ifdef IN_RING0
                int rc = HMR0ImportStateOnDemand(pVCpu, fExtrnImport);
                Assert(rc == VINF_SUCCESS || RT_FAILURE_NP(rc));
                return rc;
#else
                AssertLogRelMsgFailed(("TODO Fetch HM state: %#RX64 vs %#RX64\n", pVCpu->cpum.s.Guest.fExtrn, fExtrnImport));
                return VINF_SUCCESS;
#endif
            }
            default:
                AssertLogRelMsgFailedReturn(("%#RX64 vs %#RX64\n", pVCpu->cpum.s.Guest.fExtrn, fExtrnImport), VERR_CPUM_IPE_2);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Gets valid CR4 bits for the guest.
 *
 * @returns Valid CR4 bits.
 * @param   pVM     The cross context VM structure.
 */
VMM_INT_DECL(uint64_t) CPUMGetGuestCR4ValidMask(PVM pVM)
{
    PCCPUMFEATURES pGuestFeatures = &pVM->cpum.s.GuestFeatures;
    uint64_t fMask = X86_CR4_VME | X86_CR4_PVI
                   | X86_CR4_TSD | X86_CR4_DE
                   | X86_CR4_MCE | X86_CR4_PCE;
    if (pGuestFeatures->fPae)
        fMask |= X86_CR4_PAE;
    if (pGuestFeatures->fPge)
        fMask |= X86_CR4_PGE;
    if (pGuestFeatures->fPse)
        fMask |= X86_CR4_PSE;
    if (pGuestFeatures->fFxSaveRstor)
        fMask |= X86_CR4_OSFXSR;
    if (pGuestFeatures->fVmx)
        fMask |= X86_CR4_VMXE;
    if (pGuestFeatures->fXSaveRstor)
        fMask |= X86_CR4_OSXSAVE;
    if (pGuestFeatures->fPcid)
        fMask |= X86_CR4_PCIDE;
    if (pGuestFeatures->fFsGsBase)
        fMask |= X86_CR4_FSGSBASE;
    if (pGuestFeatures->fSse)
        fMask |= X86_CR4_OSXMMEEXCPT;
    return fMask;
}


/**
 * Sets the PAE PDPEs for the guest.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling thread.
 * @param   paPaePdpes  The PAE PDPEs to set.
 */
VMM_INT_DECL(void) CPUMSetGuestPaePdpes(PVMCPU pVCpu, PCX86PDPE paPaePdpes)
{
    Assert(paPaePdpes);
    for (unsigned i = 0; i < RT_ELEMENTS(pVCpu->cpum.s.Guest.aPaePdpes); i++)
        pVCpu->cpum.s.Guest.aPaePdpes[i].u = paPaePdpes[i].u;
    pVCpu->cpum.s.Guest.fExtrn &= ~CPUMCTX_EXTRN_CR3;
}


/**
 * Gets the PAE PDPTEs for the guest.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling thread.
 * @param   paPaePdpes  Where to store the PAE PDPEs.
 */
VMM_INT_DECL(void) CPUMGetGuestPaePdpes(PVMCPU pVCpu, PX86PDPE paPaePdpes)
{
    Assert(paPaePdpes);
    CPUM_INT_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_CR3);
    for (unsigned i = 0; i < RT_ELEMENTS(pVCpu->cpum.s.Guest.aPaePdpes); i++)
        paPaePdpes[i].u = pVCpu->cpum.s.Guest.aPaePdpes[i].u;
}


/**
 * Starts a VMX-preemption timer to expire as specified by the nested hypervisor.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   uTimer          The VMCS preemption timer value.
 * @param   cShift          The VMX-preemption timer shift (usually based on guest
 *                          VMX MSR rate).
 * @param   pu64EntryTick   Where to store the current tick when the timer is
 *                          programmed.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(int) CPUMStartGuestVmxPremptTimer(PVMCPUCC pVCpu, uint32_t uTimer, uint8_t cShift, uint64_t *pu64EntryTick)
{
    Assert(uTimer);
    Assert(cShift <= 31);
    Assert(pu64EntryTick);
    VMCPU_ASSERT_EMT(pVCpu);
    uint64_t const cTicksToNext = uTimer << cShift;
    return TMTimerSetRelative(pVCpu->CTX_SUFF(pVM), pVCpu->cpum.s.hNestedVmxPreemptTimer, cTicksToNext, pu64EntryTick);
}


/**
 * Stops the VMX-preemption timer from firing.
 *
 * @returns VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure of the calling thread.
 * @thread  EMT.
 *
 * @remarks This can be called during VM reset, so we cannot assume it will be on
 *          the EMT corresponding to @c pVCpu.
 */
VMM_INT_DECL(int) CPUMStopGuestVmxPremptTimer(PVMCPUCC pVCpu)
{
    /*
     * CPUM gets initialized before TM, so we defer creation of timers till CPUMR3InitCompleted().
     * However, we still get called during CPUMR3Init() and hence we need to check if we have
     * a valid timer object before trying to stop it.
     */
    int rc;
    TMTIMERHANDLE hTimer = pVCpu->cpum.s.hNestedVmxPreemptTimer;
    if (hTimer != NIL_TMTIMERHANDLE)
    {
        PVMCC pVM = pVCpu->CTX_SUFF(pVM);
        rc = TMTimerLock(pVM, hTimer, VERR_IGNORED);
        if (rc == VINF_SUCCESS)
        {
            if (TMTimerIsActive(pVM, hTimer))
                TMTimerStop(pVM, hTimer);
            TMTimerUnlock(pVM, hTimer);
        }
    }
    else
        rc = VERR_NOT_FOUND;
    return rc;
}


/**
 * Gets the read and write permission bits for an MSR in an MSR bitmap.
 *
 * @returns VMXMSRPM_XXX - the MSR permission.
 * @param   pvMsrBitmap     Pointer to the MSR bitmap.
 * @param   idMsr           The MSR to get permissions for.
 *
 * @sa      hmR0VmxSetMsrPermission.
 */
VMM_INT_DECL(uint32_t) CPUMGetVmxMsrPermission(void const *pvMsrBitmap, uint32_t idMsr)
{
    AssertPtrReturn(pvMsrBitmap, VMXMSRPM_EXIT_RD | VMXMSRPM_EXIT_WR);

    uint8_t const * const pbMsrBitmap = (uint8_t const * const)pvMsrBitmap;

    /*
     * MSR Layout:
     *   Byte index            MSR range            Interpreted as
     * 0x000 - 0x3ff    0x00000000 - 0x00001fff    Low MSR read bits.
     * 0x400 - 0x7ff    0xc0000000 - 0xc0001fff    High MSR read bits.
     * 0x800 - 0xbff    0x00000000 - 0x00001fff    Low MSR write bits.
     * 0xc00 - 0xfff    0xc0000000 - 0xc0001fff    High MSR write bits.
     *
     * A bit corresponding to an MSR within the above range causes a VM-exit
     * if the bit is 1 on executions of RDMSR/WRMSR.  If an MSR falls out of
     * the MSR range, it always cause a VM-exit.
     *
     * See Intel spec. 24.6.9 "MSR-Bitmap Address".
     */
    uint32_t const offBitmapRead  = 0;
    uint32_t const offBitmapWrite = 0x800;
    uint32_t       offMsr;
    uint32_t       iBit;
    if (idMsr <= UINT32_C(0x00001fff))
    {
        offMsr = 0;
        iBit   = idMsr;
    }
    else if (idMsr - UINT32_C(0xc0000000) <= UINT32_C(0x00001fff))
    {
        offMsr = 0x400;
        iBit   = idMsr - UINT32_C(0xc0000000);
    }
    else
    {
        LogFunc(("Warning! Out of range MSR %#RX32\n", idMsr));
        return VMXMSRPM_EXIT_RD | VMXMSRPM_EXIT_WR;
    }

    /*
     * Get the MSR read permissions.
     */
    uint32_t fRet;
    uint32_t const offMsrRead = offBitmapRead + offMsr;
    Assert(offMsrRead + (iBit >> 3) < offBitmapWrite);
    if (ASMBitTest(pbMsrBitmap, (offMsrRead << 3) + iBit))
        fRet = VMXMSRPM_EXIT_RD;
    else
        fRet = VMXMSRPM_ALLOW_RD;

    /*
     * Get the MSR write permissions.
     */
    uint32_t const offMsrWrite = offBitmapWrite + offMsr;
    Assert(offMsrWrite + (iBit >> 3) < X86_PAGE_4K_SIZE);
    if (ASMBitTest(pbMsrBitmap, (offMsrWrite << 3) + iBit))
        fRet |= VMXMSRPM_EXIT_WR;
    else
        fRet |= VMXMSRPM_ALLOW_WR;

    Assert(VMXMSRPM_IS_FLAG_VALID(fRet));
    return fRet;
}


/**
 * Checks the permission bits for the specified I/O port from the given I/O bitmap
 * to see if causes a VM-exit.
 *
 * @returns @c true if the I/O port access must cause a VM-exit, @c false otherwise.
 * @param   pbIoBitmap  Pointer to I/O bitmap.
 * @param   uPort       The I/O port being accessed.
 * @param   cbAccess    e size of the I/O access in bytes (1, 2 or 4 bytes).
 */
static bool cpumGetVmxIoBitmapPermission(uint8_t const *pbIoBitmap, uint16_t uPort, uint8_t cbAccess)
{
    Assert(cbAccess == 1 || cbAccess == 2 || cbAccess == 4);

    /*
     * If the I/O port access wraps around the 16-bit port I/O space, we must cause a
     * VM-exit.
     *
     * Reading 1, 2, 4 bytes at ports 0xffff, 0xfffe and 0xfffc are valid and do not
     * constitute a wrap around. However, reading 2 bytes at port 0xffff or 4 bytes
     * from port 0xffff/0xfffe/0xfffd constitute a wrap around. In other words, any
     * access to -both- ports 0xffff and port 0 is a wrap around.
     *
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    uint32_t const uPortLast = uPort + cbAccess;
    if (uPortLast > 0x10000)
        return true;

    /*
     * If any bit corresponding to the I/O access is set, we must cause a VM-exit.
     */
    uint16_t const offPerm    = uPort >> 3;                         /* Byte offset of the port. */
    uint16_t const idxPermBit = uPort - (offPerm << 3);             /* Bit offset within byte. */
    Assert(idxPermBit < 8);
    static const uint8_t s_afMask[] = { 0x0, 0x1, 0x3, 0x7, 0xf };  /* Bit-mask for all access sizes. */
    uint16_t const fMask = s_afMask[cbAccess] << idxPermBit;        /* Bit-mask of the access. */

    /* Fetch 8 or 16-bits depending on whether the access spans 8-bit boundary. */
    RTUINT16U uPerm;
    uPerm.s.Lo = pbIoBitmap[offPerm];
    if (idxPermBit + cbAccess > 8)
        uPerm.s.Hi = pbIoBitmap[offPerm + 1];
    else
        uPerm.s.Hi = 0;

    /* If any bit for the access is 1, we must cause a VM-exit. */
    if (uPerm.u & fMask)
        return true;

    return false;
}


/**
 * Returns whether the given VMCS field is valid and supported for the guest.
 *
 * @param   pVM             The cross context VM structure.
 * @param   u64VmcsField    The VMCS field.
 *
 * @remarks This takes into account the CPU features exposed to the guest.
 */
VMM_INT_DECL(bool) CPUMIsGuestVmxVmcsFieldValid(PVMCC pVM, uint64_t u64VmcsField)
{
    uint32_t const uFieldEncHi = RT_HI_U32(u64VmcsField);
    uint32_t const uFieldEncLo = RT_LO_U32(u64VmcsField);
    if (!uFieldEncHi)
    { /* likely */ }
    else
        return false;

    PCCPUMFEATURES pFeat = &pVM->cpum.s.GuestFeatures;
    switch (uFieldEncLo)
    {
        /*
         * 16-bit fields.
         */
        /* Control fields. */
        case VMX_VMCS16_VPID:                             return pFeat->fVmxVpid;
        case VMX_VMCS16_POSTED_INT_NOTIFY_VECTOR:         return pFeat->fVmxPostedInt;
        case VMX_VMCS16_EPTP_INDEX:                       return pFeat->fVmxEptXcptVe;

        /* Guest-state fields. */
        case VMX_VMCS16_GUEST_ES_SEL:
        case VMX_VMCS16_GUEST_CS_SEL:
        case VMX_VMCS16_GUEST_SS_SEL:
        case VMX_VMCS16_GUEST_DS_SEL:
        case VMX_VMCS16_GUEST_FS_SEL:
        case VMX_VMCS16_GUEST_GS_SEL:
        case VMX_VMCS16_GUEST_LDTR_SEL:
        case VMX_VMCS16_GUEST_TR_SEL:                     return true;
        case VMX_VMCS16_GUEST_INTR_STATUS:                return pFeat->fVmxVirtIntDelivery;
        case VMX_VMCS16_GUEST_PML_INDEX:                  return pFeat->fVmxPml;

        /* Host-state fields. */
        case VMX_VMCS16_HOST_ES_SEL:
        case VMX_VMCS16_HOST_CS_SEL:
        case VMX_VMCS16_HOST_SS_SEL:
        case VMX_VMCS16_HOST_DS_SEL:
        case VMX_VMCS16_HOST_FS_SEL:
        case VMX_VMCS16_HOST_GS_SEL:
        case VMX_VMCS16_HOST_TR_SEL:                      return true;

        /*
         * 64-bit fields.
         */
        /* Control fields. */
        case VMX_VMCS64_CTRL_IO_BITMAP_A_FULL:
        case VMX_VMCS64_CTRL_IO_BITMAP_A_HIGH:
        case VMX_VMCS64_CTRL_IO_BITMAP_B_FULL:
        case VMX_VMCS64_CTRL_IO_BITMAP_B_HIGH:            return pFeat->fVmxUseIoBitmaps;
        case VMX_VMCS64_CTRL_MSR_BITMAP_FULL:
        case VMX_VMCS64_CTRL_MSR_BITMAP_HIGH:             return pFeat->fVmxUseMsrBitmaps;
        case VMX_VMCS64_CTRL_EXIT_MSR_STORE_FULL:
        case VMX_VMCS64_CTRL_EXIT_MSR_STORE_HIGH:
        case VMX_VMCS64_CTRL_EXIT_MSR_LOAD_FULL:
        case VMX_VMCS64_CTRL_EXIT_MSR_LOAD_HIGH:
        case VMX_VMCS64_CTRL_ENTRY_MSR_LOAD_FULL:
        case VMX_VMCS64_CTRL_ENTRY_MSR_LOAD_HIGH:
        case VMX_VMCS64_CTRL_EXEC_VMCS_PTR_FULL:
        case VMX_VMCS64_CTRL_EXEC_VMCS_PTR_HIGH:          return true;
        case VMX_VMCS64_CTRL_EXEC_PML_ADDR_FULL:
        case VMX_VMCS64_CTRL_EXEC_PML_ADDR_HIGH:          return pFeat->fVmxPml;
        case VMX_VMCS64_CTRL_TSC_OFFSET_FULL:
        case VMX_VMCS64_CTRL_TSC_OFFSET_HIGH:             return true;
        case VMX_VMCS64_CTRL_VIRT_APIC_PAGEADDR_FULL:
        case VMX_VMCS64_CTRL_VIRT_APIC_PAGEADDR_HIGH:     return pFeat->fVmxUseTprShadow;
        case VMX_VMCS64_CTRL_APIC_ACCESSADDR_FULL:
        case VMX_VMCS64_CTRL_APIC_ACCESSADDR_HIGH:        return pFeat->fVmxVirtApicAccess;
        case VMX_VMCS64_CTRL_POSTED_INTR_DESC_FULL:
        case VMX_VMCS64_CTRL_POSTED_INTR_DESC_HIGH:       return pFeat->fVmxPostedInt;
        case VMX_VMCS64_CTRL_VMFUNC_CTRLS_FULL:
        case VMX_VMCS64_CTRL_VMFUNC_CTRLS_HIGH:           return pFeat->fVmxVmFunc;
        case VMX_VMCS64_CTRL_EPTP_FULL:
        case VMX_VMCS64_CTRL_EPTP_HIGH:                   return pFeat->fVmxEpt;
        case VMX_VMCS64_CTRL_EOI_BITMAP_0_FULL:
        case VMX_VMCS64_CTRL_EOI_BITMAP_0_HIGH:
        case VMX_VMCS64_CTRL_EOI_BITMAP_1_FULL:
        case VMX_VMCS64_CTRL_EOI_BITMAP_1_HIGH:
        case VMX_VMCS64_CTRL_EOI_BITMAP_2_FULL:
        case VMX_VMCS64_CTRL_EOI_BITMAP_2_HIGH:
        case VMX_VMCS64_CTRL_EOI_BITMAP_3_FULL:
        case VMX_VMCS64_CTRL_EOI_BITMAP_3_HIGH:           return pFeat->fVmxVirtIntDelivery;
        case VMX_VMCS64_CTRL_EPTP_LIST_FULL:
        case VMX_VMCS64_CTRL_EPTP_LIST_HIGH:
        {
            PCVMCPU pVCpu = pVM->CTX_SUFF(apCpus)[0];
            uint64_t const uVmFuncMsr = pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64VmFunc;
            return RT_BOOL(RT_BF_GET(uVmFuncMsr, VMX_BF_VMFUNC_EPTP_SWITCHING));
        }
        case VMX_VMCS64_CTRL_VMREAD_BITMAP_FULL:
        case VMX_VMCS64_CTRL_VMREAD_BITMAP_HIGH:
        case VMX_VMCS64_CTRL_VMWRITE_BITMAP_FULL:
        case VMX_VMCS64_CTRL_VMWRITE_BITMAP_HIGH:         return pFeat->fVmxVmcsShadowing;
        case VMX_VMCS64_CTRL_VE_XCPT_INFO_ADDR_FULL:
        case VMX_VMCS64_CTRL_VE_XCPT_INFO_ADDR_HIGH:      return pFeat->fVmxEptXcptVe;
        case VMX_VMCS64_CTRL_XSS_EXITING_BITMAP_FULL:
        case VMX_VMCS64_CTRL_XSS_EXITING_BITMAP_HIGH:     return pFeat->fVmxXsavesXrstors;
        case VMX_VMCS64_CTRL_TSC_MULTIPLIER_FULL:
        case VMX_VMCS64_CTRL_TSC_MULTIPLIER_HIGH:         return pFeat->fVmxUseTscScaling;
        case VMX_VMCS64_CTRL_PROC_EXEC3_FULL:
        case VMX_VMCS64_CTRL_PROC_EXEC3_HIGH:             return pFeat->fVmxTertiaryExecCtls;

        /* Read-only data fields. */
        case VMX_VMCS64_RO_GUEST_PHYS_ADDR_FULL:
        case VMX_VMCS64_RO_GUEST_PHYS_ADDR_HIGH:          return pFeat->fVmxEpt;

        /* Guest-state fields. */
        case VMX_VMCS64_GUEST_VMCS_LINK_PTR_FULL:
        case VMX_VMCS64_GUEST_VMCS_LINK_PTR_HIGH:
        case VMX_VMCS64_GUEST_DEBUGCTL_FULL:
        case VMX_VMCS64_GUEST_DEBUGCTL_HIGH:              return true;
        case VMX_VMCS64_GUEST_PAT_FULL:
        case VMX_VMCS64_GUEST_PAT_HIGH:                   return pFeat->fVmxEntryLoadPatMsr || pFeat->fVmxExitSavePatMsr;
        case VMX_VMCS64_GUEST_EFER_FULL:
        case VMX_VMCS64_GUEST_EFER_HIGH:                  return pFeat->fVmxEntryLoadEferMsr || pFeat->fVmxExitSaveEferMsr;
        case VMX_VMCS64_GUEST_PDPTE0_FULL:
        case VMX_VMCS64_GUEST_PDPTE0_HIGH:
        case VMX_VMCS64_GUEST_PDPTE1_FULL:
        case VMX_VMCS64_GUEST_PDPTE1_HIGH:
        case VMX_VMCS64_GUEST_PDPTE2_FULL:
        case VMX_VMCS64_GUEST_PDPTE2_HIGH:
        case VMX_VMCS64_GUEST_PDPTE3_FULL:
        case VMX_VMCS64_GUEST_PDPTE3_HIGH:                return pFeat->fVmxEpt;

        /* Host-state fields. */
        case VMX_VMCS64_HOST_PAT_FULL:
        case VMX_VMCS64_HOST_PAT_HIGH:                    return pFeat->fVmxExitLoadPatMsr;
        case VMX_VMCS64_HOST_EFER_FULL:
        case VMX_VMCS64_HOST_EFER_HIGH:                   return pFeat->fVmxExitLoadEferMsr;

        /*
         * 32-bit fields.
         */
        /* Control fields. */
        case VMX_VMCS32_CTRL_PIN_EXEC:
        case VMX_VMCS32_CTRL_PROC_EXEC:
        case VMX_VMCS32_CTRL_EXCEPTION_BITMAP:
        case VMX_VMCS32_CTRL_PAGEFAULT_ERROR_MASK:
        case VMX_VMCS32_CTRL_PAGEFAULT_ERROR_MATCH:
        case VMX_VMCS32_CTRL_CR3_TARGET_COUNT:
        case VMX_VMCS32_CTRL_EXIT:
        case VMX_VMCS32_CTRL_EXIT_MSR_STORE_COUNT:
        case VMX_VMCS32_CTRL_EXIT_MSR_LOAD_COUNT:
        case VMX_VMCS32_CTRL_ENTRY:
        case VMX_VMCS32_CTRL_ENTRY_MSR_LOAD_COUNT:
        case VMX_VMCS32_CTRL_ENTRY_INTERRUPTION_INFO:
        case VMX_VMCS32_CTRL_ENTRY_EXCEPTION_ERRCODE:
        case VMX_VMCS32_CTRL_ENTRY_INSTR_LENGTH:          return true;
        case VMX_VMCS32_CTRL_TPR_THRESHOLD:               return pFeat->fVmxUseTprShadow;
        case VMX_VMCS32_CTRL_PROC_EXEC2:                  return pFeat->fVmxSecondaryExecCtls;
        case VMX_VMCS32_CTRL_PLE_GAP:
        case VMX_VMCS32_CTRL_PLE_WINDOW:                  return pFeat->fVmxPauseLoopExit;

        /* Read-only data fields. */
        case VMX_VMCS32_RO_VM_INSTR_ERROR:
        case VMX_VMCS32_RO_EXIT_REASON:
        case VMX_VMCS32_RO_EXIT_INTERRUPTION_INFO:
        case VMX_VMCS32_RO_EXIT_INTERRUPTION_ERROR_CODE:
        case VMX_VMCS32_RO_IDT_VECTORING_INFO:
        case VMX_VMCS32_RO_IDT_VECTORING_ERROR_CODE:
        case VMX_VMCS32_RO_EXIT_INSTR_LENGTH:
        case VMX_VMCS32_RO_EXIT_INSTR_INFO:               return true;

        /* Guest-state fields. */
        case VMX_VMCS32_GUEST_ES_LIMIT:
        case VMX_VMCS32_GUEST_CS_LIMIT:
        case VMX_VMCS32_GUEST_SS_LIMIT:
        case VMX_VMCS32_GUEST_DS_LIMIT:
        case VMX_VMCS32_GUEST_FS_LIMIT:
        case VMX_VMCS32_GUEST_GS_LIMIT:
        case VMX_VMCS32_GUEST_LDTR_LIMIT:
        case VMX_VMCS32_GUEST_TR_LIMIT:
        case VMX_VMCS32_GUEST_GDTR_LIMIT:
        case VMX_VMCS32_GUEST_IDTR_LIMIT:
        case VMX_VMCS32_GUEST_ES_ACCESS_RIGHTS:
        case VMX_VMCS32_GUEST_CS_ACCESS_RIGHTS:
        case VMX_VMCS32_GUEST_SS_ACCESS_RIGHTS:
        case VMX_VMCS32_GUEST_DS_ACCESS_RIGHTS:
        case VMX_VMCS32_GUEST_FS_ACCESS_RIGHTS:
        case VMX_VMCS32_GUEST_GS_ACCESS_RIGHTS:
        case VMX_VMCS32_GUEST_LDTR_ACCESS_RIGHTS:
        case VMX_VMCS32_GUEST_TR_ACCESS_RIGHTS:
        case VMX_VMCS32_GUEST_INT_STATE:
        case VMX_VMCS32_GUEST_ACTIVITY_STATE:
        case VMX_VMCS32_GUEST_SMBASE:
        case VMX_VMCS32_GUEST_SYSENTER_CS:                return true;
        case VMX_VMCS32_PREEMPT_TIMER_VALUE:              return pFeat->fVmxPreemptTimer;

        /* Host-state fields. */
        case VMX_VMCS32_HOST_SYSENTER_CS:                 return true;

        /*
         * Natural-width fields.
         */
        /* Control fields. */
        case VMX_VMCS_CTRL_CR0_MASK:
        case VMX_VMCS_CTRL_CR4_MASK:
        case VMX_VMCS_CTRL_CR0_READ_SHADOW:
        case VMX_VMCS_CTRL_CR4_READ_SHADOW:
        case VMX_VMCS_CTRL_CR3_TARGET_VAL0:
        case VMX_VMCS_CTRL_CR3_TARGET_VAL1:
        case VMX_VMCS_CTRL_CR3_TARGET_VAL2:
        case VMX_VMCS_CTRL_CR3_TARGET_VAL3:               return true;

        /* Read-only data fields. */
        case VMX_VMCS_RO_EXIT_QUALIFICATION:
        case VMX_VMCS_RO_IO_RCX:
        case VMX_VMCS_RO_IO_RSI:
        case VMX_VMCS_RO_IO_RDI:
        case VMX_VMCS_RO_IO_RIP:
        case VMX_VMCS_RO_GUEST_LINEAR_ADDR:               return true;

        /* Guest-state fields. */
        case VMX_VMCS_GUEST_CR0:
        case VMX_VMCS_GUEST_CR3:
        case VMX_VMCS_GUEST_CR4:
        case VMX_VMCS_GUEST_ES_BASE:
        case VMX_VMCS_GUEST_CS_BASE:
        case VMX_VMCS_GUEST_SS_BASE:
        case VMX_VMCS_GUEST_DS_BASE:
        case VMX_VMCS_GUEST_FS_BASE:
        case VMX_VMCS_GUEST_GS_BASE:
        case VMX_VMCS_GUEST_LDTR_BASE:
        case VMX_VMCS_GUEST_TR_BASE:
        case VMX_VMCS_GUEST_GDTR_BASE:
        case VMX_VMCS_GUEST_IDTR_BASE:
        case VMX_VMCS_GUEST_DR7:
        case VMX_VMCS_GUEST_RSP:
        case VMX_VMCS_GUEST_RIP:
        case VMX_VMCS_GUEST_RFLAGS:
        case VMX_VMCS_GUEST_PENDING_DEBUG_XCPTS:
        case VMX_VMCS_GUEST_SYSENTER_ESP:
        case VMX_VMCS_GUEST_SYSENTER_EIP:                 return true;

        /* Host-state fields. */
        case VMX_VMCS_HOST_CR0:
        case VMX_VMCS_HOST_CR3:
        case VMX_VMCS_HOST_CR4:
        case VMX_VMCS_HOST_FS_BASE:
        case VMX_VMCS_HOST_GS_BASE:
        case VMX_VMCS_HOST_TR_BASE:
        case VMX_VMCS_HOST_GDTR_BASE:
        case VMX_VMCS_HOST_IDTR_BASE:
        case VMX_VMCS_HOST_SYSENTER_ESP:
        case VMX_VMCS_HOST_SYSENTER_EIP:
        case VMX_VMCS_HOST_RSP:
        case VMX_VMCS_HOST_RIP:                           return true;
    }

    return false;
}


/**
 * Checks whether the given I/O access should cause a nested-guest VM-exit.
 *
 * @returns @c true if it causes a VM-exit, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   u16Port     The I/O port being accessed.
 * @param   cbAccess    The size of the I/O access in bytes (1, 2 or 4 bytes).
 */
VMM_INT_DECL(bool) CPUMIsGuestVmxIoInterceptSet(PCVMCPU pVCpu, uint16_t u16Port, uint8_t cbAccess)
{
    PCCPUMCTX pCtx = &pVCpu->cpum.s.Guest;
    if (CPUMIsGuestVmxProcCtlsSet(pCtx, VMX_PROC_CTLS_UNCOND_IO_EXIT))
        return true;

    if (CPUMIsGuestVmxProcCtlsSet(pCtx, VMX_PROC_CTLS_USE_IO_BITMAPS))
        return cpumGetVmxIoBitmapPermission(pCtx->hwvirt.vmx.abIoBitmap, u16Port, cbAccess);

    return false;
}


/**
 * Checks whether the Mov-to-CR3 instruction causes a nested-guest VM-exit.
 *
 * @returns @c true if it causes a VM-exit, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uNewCr3     The CR3 value being written.
 */
VMM_INT_DECL(bool) CPUMIsGuestVmxMovToCr3InterceptSet(PVMCPU pVCpu, uint64_t uNewCr3)
{
    /*
     * If the CR3-load exiting control is set and the new CR3 value does not
     * match any of the CR3-target values in the VMCS, we must cause a VM-exit.
     *
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    PCCPUMCTX const pCtx = &pVCpu->cpum.s.Guest;
    if (CPUMIsGuestVmxProcCtlsSet(pCtx, VMX_PROC_CTLS_CR3_LOAD_EXIT))
    {
        uint32_t const uCr3TargetCount = pCtx->hwvirt.vmx.Vmcs.u32Cr3TargetCount;
        Assert(uCr3TargetCount <= VMX_V_CR3_TARGET_COUNT);

        /* If the CR3-target count is 0, cause a VM-exit. */
        if (uCr3TargetCount == 0)
            return true;

        /* If the CR3 being written doesn't match any of the target values, cause a VM-exit. */
        AssertCompile(VMX_V_CR3_TARGET_COUNT == 4);
        if (   uNewCr3 != pCtx->hwvirt.vmx.Vmcs.u64Cr3Target0.u
            && uNewCr3 != pCtx->hwvirt.vmx.Vmcs.u64Cr3Target1.u
            && uNewCr3 != pCtx->hwvirt.vmx.Vmcs.u64Cr3Target2.u
            && uNewCr3 != pCtx->hwvirt.vmx.Vmcs.u64Cr3Target3.u)
            return true;
    }
    return false;
}


/**
 * Checks whether a VMREAD or VMWRITE instruction for the given VMCS field causes a
 * VM-exit or not.
 *
 * @returns @c true if the VMREAD/VMWRITE is intercepted, @c false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VM-exit reason (VMX_EXIT_VMREAD or
 *                          VMX_EXIT_VMREAD).
 * @param   u64VmcsField    The VMCS field.
 */
VMM_INT_DECL(bool) CPUMIsGuestVmxVmreadVmwriteInterceptSet(PCVMCPU pVCpu, uint32_t uExitReason, uint64_t u64VmcsField)
{
    Assert(CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.s.Guest));
    Assert(   uExitReason == VMX_EXIT_VMREAD
           || uExitReason == VMX_EXIT_VMWRITE);

    /*
     * Without VMCS shadowing, all VMREAD and VMWRITE instructions are intercepted.
     */
    if (!CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.s.Guest, VMX_PROC_CTLS2_VMCS_SHADOWING))
        return true;

    /*
     * If any reserved bit in the 64-bit VMCS field encoding is set, the VMREAD/VMWRITE
     * is intercepted. This excludes any reserved bits in the valid parts of the field
     * encoding (i.e. bit 12).
     */
    if (u64VmcsField & VMX_VMCSFIELD_RSVD_MASK)
        return true;

    /*
     * Finally, consult the VMREAD/VMWRITE bitmap whether to intercept the instruction or not.
     */
    uint32_t const        u32VmcsField = RT_LO_U32(u64VmcsField);
    uint8_t const * const pbBitmap     = uExitReason == VMX_EXIT_VMREAD
                                       ? &pVCpu->cpum.s.Guest.hwvirt.vmx.abVmreadBitmap[0]
                                       : &pVCpu->cpum.s.Guest.hwvirt.vmx.abVmwriteBitmap[0];
    Assert(pbBitmap);
    Assert(u32VmcsField >> 3 < VMX_V_VMREAD_VMWRITE_BITMAP_SIZE);
    return ASMBitTest(pbBitmap, (u32VmcsField << 3) + (u32VmcsField & 7));
}



/**
 * Determines whether the given I/O access should cause a nested-guest \#VMEXIT.
 *
 * @param   pvIoBitmap      Pointer to the nested-guest IO bitmap.
 * @param   u16Port         The IO port being accessed.
 * @param   enmIoType       The type of IO access.
 * @param   cbReg           The IO operand size in bytes.
 * @param   cAddrSizeBits   The address size bits (for 16, 32 or 64).
 * @param   iEffSeg         The effective segment number.
 * @param   fRep            Whether this is a repeating IO instruction (REP prefix).
 * @param   fStrIo          Whether this is a string IO instruction.
 * @param   pIoExitInfo     Pointer to the SVMIOIOEXITINFO struct to be filled.
 *                          Optional, can be NULL.
 */
VMM_INT_DECL(bool) CPUMIsSvmIoInterceptSet(void *pvIoBitmap, uint16_t u16Port, SVMIOIOTYPE enmIoType, uint8_t cbReg,
                                           uint8_t cAddrSizeBits, uint8_t iEffSeg, bool fRep, bool fStrIo,
                                           PSVMIOIOEXITINFO pIoExitInfo)
{
    Assert(cAddrSizeBits == 16 || cAddrSizeBits == 32 || cAddrSizeBits == 64);
    Assert(cbReg == 1 || cbReg == 2 || cbReg == 4 || cbReg == 8);

    /*
     * The IOPM layout:
     * Each bit represents one 8-bit port. That makes a total of 0..65535 bits or
     * two 4K pages.
     *
     * For IO instructions that access more than a single byte, the permission bits
     * for all bytes are checked; if any bit is set to 1, the IO access is intercepted.
     *
     * Since it's possible to do a 32-bit IO access at port 65534 (accessing 4 bytes),
     * we need 3 extra bits beyond the second 4K page.
     */
    static const uint16_t s_auSizeMasks[] = { 0, 1, 3, 0, 0xf, 0, 0, 0 };

    uint16_t const offIopm   = u16Port >> 3;
    uint16_t const fSizeMask = s_auSizeMasks[(cAddrSizeBits >> SVM_IOIO_OP_SIZE_SHIFT) & 7];
    uint8_t  const cShift    = u16Port - (offIopm << 3);
    uint16_t const fIopmMask = (1 << cShift) | (fSizeMask << cShift);

    uint8_t const *pbIopm = (uint8_t *)pvIoBitmap;
    Assert(pbIopm);
    pbIopm += offIopm;
    uint16_t const u16Iopm = *(uint16_t *)pbIopm;
    if (u16Iopm & fIopmMask)
    {
        if (pIoExitInfo)
        {
            static const uint32_t s_auIoOpSize[] =
            { SVM_IOIO_32_BIT_OP, SVM_IOIO_8_BIT_OP, SVM_IOIO_16_BIT_OP, 0, SVM_IOIO_32_BIT_OP, 0, 0, 0 };

            static const uint32_t s_auIoAddrSize[] =
            { 0, SVM_IOIO_16_BIT_ADDR, SVM_IOIO_32_BIT_ADDR, 0, SVM_IOIO_64_BIT_ADDR, 0, 0, 0 };

            pIoExitInfo->u         = s_auIoOpSize[cbReg & 7];
            pIoExitInfo->u        |= s_auIoAddrSize[(cAddrSizeBits >> 4) & 7];
            pIoExitInfo->n.u1Str   = fStrIo;
            pIoExitInfo->n.u1Rep   = fRep;
            pIoExitInfo->n.u3Seg   = iEffSeg & 7;
            pIoExitInfo->n.u1Type  = enmIoType;
            pIoExitInfo->n.u16Port = u16Port;
        }
        return true;
    }

    /** @todo remove later (for debugging as VirtualBox always traps all IO
     *        intercepts). */
    AssertMsgFailed(("CPUMSvmIsIOInterceptActive: We expect an IO intercept here!\n"));
    return false;
}


/**
 * Gets the MSR permission bitmap byte and bit offset for the specified MSR.
 *
 * @returns VBox status code.
 * @param   idMsr       The MSR being requested.
 * @param   pbOffMsrpm  Where to store the byte offset in the MSR permission
 *                      bitmap for @a idMsr.
 * @param   puMsrpmBit  Where to store the bit offset starting at the byte
 *                      returned in @a pbOffMsrpm.
 */
VMM_INT_DECL(int) CPUMGetSvmMsrpmOffsetAndBit(uint32_t idMsr, uint16_t *pbOffMsrpm, uint8_t *puMsrpmBit)
{
    Assert(pbOffMsrpm);
    Assert(puMsrpmBit);

    /*
     * MSRPM Layout:
     * Byte offset          MSR range
     * 0x000  - 0x7ff       0x00000000 - 0x00001fff
     * 0x800  - 0xfff       0xc0000000 - 0xc0001fff
     * 0x1000 - 0x17ff      0xc0010000 - 0xc0011fff
     * 0x1800 - 0x1fff              Reserved
     *
     * Each MSR is represented by 2 permission bits (read and write).
     */
    if (idMsr <= 0x00001fff)
    {
        /* Pentium-compatible MSRs. */
        uint32_t const bitoffMsr = idMsr << 1;
        *pbOffMsrpm = bitoffMsr >> 3;
        *puMsrpmBit = bitoffMsr & 7;
        return VINF_SUCCESS;
    }

    if (   idMsr >= 0xc0000000
        && idMsr <= 0xc0001fff)
    {
        /* AMD Sixth Generation x86 Processor MSRs. */
        uint32_t const bitoffMsr = (idMsr - 0xc0000000) << 1;
        *pbOffMsrpm = 0x800 + (bitoffMsr >> 3);
        *puMsrpmBit = bitoffMsr & 7;
        return VINF_SUCCESS;
    }

    if (   idMsr >= 0xc0010000
        && idMsr <= 0xc0011fff)
    {
        /* AMD Seventh and Eighth Generation Processor MSRs. */
        uint32_t const bitoffMsr = (idMsr - 0xc0010000) << 1;
        *pbOffMsrpm = 0x1000 + (bitoffMsr >> 3);
        *puMsrpmBit = bitoffMsr & 7;
        return VINF_SUCCESS;
    }

    *pbOffMsrpm = 0;
    *puMsrpmBit = 0;
    return VERR_OUT_OF_RANGE;
}


/**
 * Checks whether the guest is in VMX non-root mode and using EPT paging.
 *
 * @returns @c true if in VMX non-root operation with EPT, @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMM_INT_DECL(bool) CPUMIsGuestVmxEptPagingEnabled(PCVMCPUCC pVCpu)
{
    return CPUMIsGuestVmxEptPagingEnabledEx(&pVCpu->cpum.s.Guest);
}


/**
 * Checks whether the guest is in VMX non-root mode and using EPT paging and the
 * nested-guest is in PAE mode.
 *
 * @returns @c true if in VMX non-root operation with EPT, @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMM_INT_DECL(bool) CPUMIsGuestVmxEptPaePagingEnabled(PCVMCPUCC pVCpu)
{
    return    CPUMIsGuestVmxEptPagingEnabledEx(&pVCpu->cpum.s.Guest)
           && CPUMIsGuestInPAEModeEx(&pVCpu->cpum.s.Guest);
}


/**
 * Returns the guest-physical address of the APIC-access page when executing a
 * nested-guest.
 *
 * @returns The APIC-access page guest-physical address.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMM_INT_DECL(uint64_t) CPUMGetGuestVmxApicAccessPageAddr(PCVMCPUCC pVCpu)
{
    return CPUMGetGuestVmxApicAccessPageAddrEx(&pVCpu->cpum.s.Guest);
}

