/* $Id: IEMAllMem-armv8.cpp 110422 2025-07-27 22:59:43Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - ARMV8 target, memory.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_IEM_MEM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/dbgf.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/armv8.h>

#include "IEMInline.h"
#include "IEMInline-armv8.h"
#include "IEMInlineMem-armv8.h"
#include "IEMAllTlbInline-armv8.h"


/** @name   Memory access.
 *
 * @{
 */

/**
 * Converts IEM_ACCESS_XXX + fExec to PGMQPAGE_F_XXX.
 */
DECL_FORCE_INLINE(uint32_t) iemMemArmAccessToQPage(PVMCPUCC pVCpu, uint32_t fAccess)
{
    AssertCompile(IEM_ACCESS_TYPE_READ  == PGMQPAGE_F_READ);
    AssertCompile(IEM_ACCESS_TYPE_WRITE == PGMQPAGE_F_WRITE);
    AssertCompile(IEM_ACCESS_TYPE_EXEC  == PGMQPAGE_F_EXECUTE);
    /** @todo IEMTLBE_F_EFF_U_NO_GCS / IEMTLBE_F_EFF_P_NO_GCS,
     *  IEMTLBE_F_S1_NS/NSE, IEMTLBE_F_S2_NO_LIM_WRITE/TL0/TL1. */
    return (fAccess & (PGMQPAGE_F_READ | IEM_ACCESS_TYPE_WRITE | PGMQPAGE_F_EXECUTE))
         | (!(fAccess & IEM_ACCESS_WHAT_SYS) && IEM_F_MODE_ARM_GET_EL(pVCpu->iem.s.fExec) == 0 ? PGMQPAGE_F_USER_MODE : 0);
}



/**
 * Translates a virtual address to a physical physical address and checks if we
 * can access the page as specified.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   GCPtrMem            The virtual address.
 * @param   cbAccess            The access size, for raising \#PF correctly for
 *                              FXSAVE and such.
 * @param   fAccess             The intended access.
 * @param   pGCPhysMem          Where to return the physical address.
 */
VBOXSTRICTRC iemMemPageTranslateAndCheckAccess(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t cbAccess,
                                               uint32_t fAccess, PRTGCPHYS pGCPhysMem) RT_NOEXCEPT
{
    Assert(!(fAccess & IEM_ACCESS_TYPE_EXEC));
    PGMPTWALKFAST WalkFast;
    int rc = PGMGstQueryPageFast(pVCpu, GCPtrMem, iemMemArmAccessToQPage(pVCpu, fAccess), &WalkFast);
    if (RT_SUCCESS(rc))
    {
        Assert((WalkFast.fInfo & PGM_WALKINFO_SUCCEEDED) && WalkFast.fFailed == PGM_WALKFAIL_SUCCESS);

        /* If the page is writable and does not have the no-exec bit set, all
           access is allowed.  Otherwise we'll have to check more carefully... */
#if 0 /** @todo rewrite to arm */
        Assert(   (WalkFast.fEffective & (X86_PTE_RW | X86_PTE_US | X86_PTE_PAE_NX)) == (X86_PTE_RW | X86_PTE_US)
               || (   (   !(fAccess & IEM_ACCESS_TYPE_WRITE)
                       || (WalkFast.fEffective & X86_PTE_RW)
                       || (   (    IEM_GET_CPL(pVCpu) != 3
                               || (fAccess & IEM_ACCESS_WHAT_SYS))
                           && !(pVCpu->cpum.GstCtx.cr0 & X86_CR0_WP)) )
                    && (   (WalkFast.fEffective & X86_PTE_US)
                        || IEM_GET_CPL(pVCpu) != 3
                        || (fAccess & IEM_ACCESS_WHAT_SYS) )
                    && (   !(fAccess & IEM_ACCESS_TYPE_EXEC)
                        || !(WalkFast.fEffective & X86_PTE_PAE_NX)
                        || !(pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_NXE) )
                  )
              );

        /* PGMGstQueryPageFast sets the A & D bits. */
        /** @todo testcase: check when A and D bits are actually set by the CPU.  */
        Assert(!(~WalkFast.fEffective & (fAccess & IEM_ACCESS_TYPE_WRITE ? X86_PTE_D | X86_PTE_A : X86_PTE_A)));
#endif

        *pGCPhysMem = WalkFast.GCPhys;
        return VINF_SUCCESS;
    }

    LogEx(LOG_GROUP_IEM,("iemMemPageTranslateAndCheckAccess: GCPtrMem=%RGv - failed to fetch page -> #PF\n", GCPtrMem));
    /** @todo Check unassigned memory in unpaged mode. */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
    if (WalkFast.fFailed & PGM_WALKFAIL_EPT)
        IEM_VMX_VMEXIT_EPT_RET(pVCpu, &WalkFast, fAccess, IEM_SLAT_FAIL_LINEAR_TO_PHYS_ADDR, 0 /* cbInstr */);
#endif
    *pGCPhysMem = NIL_RTGCPHYS;
    return iemRaiseDataAbortFromWalk(pVCpu, GCPtrMem, cbAccess, fAccess, rc, &WalkFast);
}


/**
 * Converts PGM_PTATTRS_XXX to IEMTLBE_F_XXX.
 */
DECL_FORCE_INLINE(uint64_t)
iemMemArmPtAttrsToTlbeFlags(uint64_t const fEff, uint64_t const fInfo, uint64_t const uTlbPhysRevAndStuff)
{
    uint64_t const fEffInv = ~fEff;

    /* Assemble the TLBE flags: */
    AssertCompile(   PGM_PTATTRS_PR_SHIFT + 1 == PGM_PTATTRS_PW_SHIFT
                  && PGM_PTATTRS_PR_SHIFT + 2 == PGM_PTATTRS_PX_SHIFT
                  && PGM_PTATTRS_PR_SHIFT + 3 == PGM_PTATTRS_PGCS_SHIFT
                  && PGM_PTATTRS_PR_SHIFT + 4 == PGM_PTATTRS_UR_SHIFT
                  && PGM_PTATTRS_PR_SHIFT + 5 == PGM_PTATTRS_UW_SHIFT
                  && PGM_PTATTRS_PR_SHIFT + 6 == PGM_PTATTRS_UX_SHIFT
                  && PGM_PTATTRS_PR_SHIFT + 7 == PGM_PTATTRS_UGCS_SHIFT);
    AssertCompile(   IEMTLBE_F_EFF_P_NO_READ_BIT + 1 == IEMTLBE_F_EFF_P_NO_WRITE_BIT
                  && IEMTLBE_F_EFF_P_NO_READ_BIT + 2 == IEMTLBE_F_EFF_P_NO_EXEC_BIT
                  && IEMTLBE_F_EFF_P_NO_READ_BIT + 3 == IEMTLBE_F_EFF_P_NO_GCS_BIT
                  && IEMTLBE_F_EFF_P_NO_READ_BIT + 4 == IEMTLBE_F_EFF_U_NO_READ_BIT
                  && IEMTLBE_F_EFF_P_NO_READ_BIT + 5 == IEMTLBE_F_EFF_U_NO_WRITE_BIT
                  && IEMTLBE_F_EFF_P_NO_READ_BIT + 6 == IEMTLBE_F_EFF_U_NO_EXEC_BIT
                  && IEMTLBE_F_EFF_P_NO_READ_BIT + 7 == IEMTLBE_F_EFF_U_NO_GCS_BIT);
    uint64_t       fTlbe = fEffInv >> PGM_PTATTRS_PR_SHIFT;
    fTlbe  &= RT_BIT_32(8) - 1;
    fTlbe <<= IEMTLBE_F_EFF_P_NO_READ_BIT;
    Assert(!(fEff & PGM_PTATTRS_PWXN_MASK) || (fTlbe & IEMTLBE_F_EFF_P_NO_EXEC) || (fTlbe & IEMTLBE_F_EFF_P_NO_WRITE));
    Assert(!(fEff & PGM_PTATTRS_UWXN_MASK) || (fTlbe & IEMTLBE_F_EFF_U_NO_EXEC) || (fTlbe & IEMTLBE_F_EFF_U_NO_WRITE));

    fTlbe |= (   (fEff & PGM_PTATTRS_ND_MASK)
              << (IEMTLBE_F_EFF_NO_DIRTY_BIT - PGM_PTATTRS_ND_SHIFT));
    AssertCompile(IEMTLBE_F_EFF_NO_DIRTY_BIT > PGM_PTATTRS_ND_SHIFT);

    fTlbe |= (   (fEff & PGM_PTATTRS_AMEC_MASK)
              << (PGM_PTATTRS_AMEC_SHIFT - IEMTLBE_F_EFF_AMEC_BIT));
    AssertCompile(PGM_PTATTRS_AMEC_SHIFT > IEMTLBE_F_EFF_AMEC_BIT);

    fTlbe |= (   (fEff & PGM_PTATTRS_DEVICE_MASK)
              << (PGM_PTATTRS_DEVICE_SHIFT - IEMTLBE_F_EFF_DEVICE_BIT));
    AssertCompile(PGM_PTATTRS_DEVICE_SHIFT > IEMTLBE_F_EFF_DEVICE_BIT);

    fTlbe |= (   (fEff & PGM_PTATTRS_GP_MASK)
              << (PGM_PTATTRS_GP_SHIFT - IEMTLBE_F_GP_BIT));
    AssertCompile(PGM_PTATTRS_GP_SHIFT > IEMTLBE_F_GP_BIT);

    if (fInfo & PGM_WALKINFO_IS_SLAT) /** @todo hope this is correct use of the flag... */
    {
        AssertCompile(   PGM_PTATTRS_S2_R_SHIFT + 1 == PGM_PTATTRS_S2_W_SHIFT
                      && PGM_PTATTRS_S2_R_SHIFT + 2 == PGM_PTATTRS_S2_PX_SHIFT);
        fTlbe |= (   (fEffInv & (PGM_PTATTRS_S2_R_MASK | PGM_PTATTRS_S2_W_MASK | PGM_PTATTRS_S2_PX_MASK))
                  >> (PGM_PTATTRS_S2_R_SHIFT - IEMTLBE_F_EFF_P_NO_READ_BIT));
        AssertCompile(PGM_PTATTRS_S2_R_SHIFT > IEMTLBE_F_EFF_P_NO_READ_BIT);
        fTlbe |= (   (fEffInv & (PGM_PTATTRS_S2_R_MASK | PGM_PTATTRS_S2_W_MASK))
                  >> (PGM_PTATTRS_S2_R_SHIFT - IEMTLBE_F_EFF_U_NO_READ_BIT));
        AssertCompile(PGM_PTATTRS_S2_R_SHIFT > IEMTLBE_F_EFF_U_NO_READ_BIT);
        fTlbe |= (   (fEffInv & PGM_PTATTRS_S2_UX_MASK)
                  >> (PGM_PTATTRS_S2_UX_SHIFT - IEMTLBE_F_EFF_U_NO_EXEC_BIT));
        AssertCompile(PGM_PTATTRS_S2_UX_SHIFT > IEMTLBE_F_EFF_U_NO_EXEC_BIT);

        fTlbe |= (   (fEffInv & PGM_PTATTRS_S2_D_MASK)
                  << (IEMTLBE_F_EFF_NO_DIRTY_BIT - PGM_PTATTRS_S2_D_SHIFT));
        AssertCompile(IEMTLBE_F_EFF_NO_DIRTY_BIT > PGM_PTATTRS_S2_D_SHIFT);

        fTlbe |= (   (fEff & PGM_PTATTRS_S2_AMEC_MASK)
                  << (PGM_PTATTRS_S2_AMEC_SHIFT - IEMTLBE_F_EFF_AMEC_BIT));
        AssertCompile(PGM_PTATTRS_S2_AMEC_SHIFT > IEMTLBE_F_EFF_AMEC_BIT);

        fTlbe |= (   (fEff & PGM_PTATTRS_S2_DEVICE_MASK)
                  << (PGM_PTATTRS_S2_DEVICE_SHIFT - IEMTLBE_F_EFF_DEVICE_BIT));
        AssertCompile(PGM_PTATTRS_S2_DEVICE_SHIFT > IEMTLBE_F_EFF_DEVICE_BIT);
    }

    if (fInfo & PGM_PTATTRS_NG_MASK)
        fTlbe |= uTlbPhysRevAndStuff & (IEMTLBE_F_REGIME_MASK | IEMTLBE_F_S2_VMID | IEMTLBE_F_NG | IEMTLBE_F_S1_ASID);
    else
        fTlbe |= uTlbPhysRevAndStuff & (IEMTLBE_F_REGIME_MASK | IEMTLBE_F_S2_VMID);

    return fTlbe;
}


/**
 * Converts PGM_PTATTRS_XXX to IEMTLBE_GCPHYS_F_XXX.
 */
DECL_FORCE_INLINE(uint64_t) iemMemArmPtAttrsToGCPhysFlags(uint64_t const fEff)
{
    uint64_t fGCPhysFlags = 0;
    /** @todo  IEMTLBE_GCPHYS_F_GRANULE_MASK, IEMTLBE_GCPHYS_F_TTL_MASK */

    AssertCompile(PGM_PTATTRS_NS_SHIFT + 1 == PGM_PTATTRS_NSE_SHIFT);
    AssertCompile(IEMTLBE_GCPHYS_F_NS_BIT + 1 == IEMTLBE_GCPHYS_F_NSE_BIT);
    fGCPhysFlags |= (   (fEff & (PGM_PTATTRS_NS_MASK | PGM_PTATTRS_NSE_MASK))
                     >> (PGM_PTATTRS_NS_SHIFT - IEMTLBE_GCPHYS_F_NS_BIT));
    AssertCompile(       PGM_PTATTRS_NS_SHIFT > IEMTLBE_GCPHYS_F_NS_BIT);

        /** @todo PGM_PTATTRS_NT_MASK + PGM_PTATTRS_S2_NT_MASK. */
        /** @todo PGM_PTATTRS_S2_AO_MASK. */
        /** @todo page sizes (WalkFast.fInfo & PGM_WALKINFO_BIG_PAGE, ++). */
        /** @todo disalow instruction fetching from device memory? */

    return fGCPhysFlags;
}


/**
 * Maps the specified guest memory for the given kind of access.
 *
 * This may be using bounce buffering of the memory if it's crossing a page
 * boundary or if there is an access handler installed for any of it.  Because
 * of lock prefix guarantees, we're in for some extra clutter when this
 * happens.
 *
 * This may raise a \#GP, \#SS, \#PF or \#AC.
 *
 * @returns VBox strict status code.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling thread.
 * @param   ppvMem      Where to return the pointer to the mapped memory.
 * @param   pbUnmapInfo Where to return unmap info to be passed to
 *                      iemMemCommitAndUnmap or iemMemRollbackAndUnmap when
 *                      done.
 * @param   cbMem       The number of bytes to map.  This is usually 1, 2, 4, 6,
 *                      8, 12, 16, 32 or 64.  When used by string operations
 *                      it can be up to a page.
 * @param   GCPtrMem    The address of the guest memory.
 * @param   fAccess     How the memory is being accessed.  The
 *                      IEM_ACCESS_TYPE_XXX part is used to figure out how to
 *                      map the memory, while the IEM_ACCESS_WHAT_XXX part is
 *                      used when raising exceptions.  The IEM_ACCESS_ATOMIC and
 *                      IEM_ACCESS_PARTIAL_WRITE bits are also allowed to be
 *                      set.
 * @param   uAlignCtl   Alignment control:
 *                          - Bits 15:0 is the alignment mask.
 *                          - Bits 31:16 for flags like IEM_MEMMAP_F_ALIGN_GP,
 *                            IEM_MEMMAP_F_ALIGN_SSE, and
 *                            IEM_MEMMAP_F_ALIGN_GP_OR_AC.
 *                      Pass zero to skip alignment.
 */
VBOXSTRICTRC iemMemMap(PVMCPUCC pVCpu, void **ppvMem, uint8_t *pbUnmapInfo, size_t cbMem, RTGCPTR GCPtrMem,
                       uint32_t fAccess, uint32_t uAlignCtl) RT_NOEXCEPT
{
    STAM_COUNTER_INC(&pVCpu->iem.s.StatMemMapNoJmp);

    /*
     * Check the input and figure out which mapping entry to use.
     */
    Assert(cbMem <= 64);
    Assert(cbMem <= sizeof(pVCpu->iem.s.aBounceBuffers[0]));
    Assert(!(fAccess & ~(IEM_ACCESS_TYPE_MASK | IEM_ACCESS_WHAT_MASK | IEM_ACCESS_ATOMIC | IEM_ACCESS_PARTIAL_WRITE)));
    Assert(pVCpu->iem.s.cActiveMappings < RT_ELEMENTS(pVCpu->iem.s.aMemMappings));

    /* Find mapping entry. We only have two, so this is simple. */
    AssertCompile(RT_ELEMENTS(pVCpu->iem.s.aMemMappings) == 2);
    unsigned iMemMap;
    if (pVCpu->iem.s.aMemMappings[0].fAccess == IEM_ACCESS_INVALID)
        iMemMap = 0;
    else if (pVCpu->iem.s.aMemMappings[1].fAccess == IEM_ACCESS_INVALID)
        iMemMap = 1;
    else
        AssertLogRelMsgFailedReturn(("active=%d [].fAccess = {%#x, %#x}\n", pVCpu->iem.s.cActiveMappings,
                                     pVCpu->iem.s.aMemMappings[0].fAccess, pVCpu->iem.s.aMemMappings[1].fAccess),
                                    VERR_IEM_IPE_9);

    /*
     * Map the memory, checking that we can actually access it.  If something
     * slightly complicated happens, fall back on bounce buffering.
     */
    if ((GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK) + cbMem <= GUEST_MIN_PAGE_SIZE) /* Crossing a possible page/tlb boundary? */
    { /* likely */ }
    else
        return iemMemBounceBufferMapCrossPage(pVCpu, iMemMap, ppvMem, pbUnmapInfo, cbMem, GCPtrMem, fAccess);

    /*
     * Alignment check.
     */
    if (   (GCPtrMem & (uAlignCtl & UINT16_MAX)) == 0
        || !(pVCpu->iem.s.fExec & (IEM_F_ARM_A | IEM_F_ARM_AA)) )
    { /* likely */ }
    else
    {
        if (!(fAccess & IEM_ACCESS_ATOMIC))
        {
            if (pVCpu->iem.s.fExec & IEM_F_ARM_A)
                return iemRaiseDataAbortFromAlignmentCheck(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess);
        }
        else
        {
            if (pVCpu->iem.s.fExec & IEM_F_ARM_AA)
                return iemRaiseDataAbortFromAlignmentCheck(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess);

#if (defined(RT_ARCH_AMD64) && defined(RT_OS_LINUX)) || defined(RT_ARCH_ARM64)
            /* If the access is atomic there are host platform alignmnet restrictions
               we need to conform with. */
# if defined(RT_ARCH_AMD64)
            if (64U - (GCPtrMem & 63U) >= cbMem) /* split-lock detection. ASSUMES 64 byte cache line. */
# elif defined(RT_ARCH_ARM64)
            if (16U - (GCPtrMem & 15U) >= cbMem) /* LSE2 allows atomics anywhere within a 16 byte sized & aligned block. */
# else
#  error port me
# endif
            { /* okay */ }
            else
            {
                LogEx(LOG_GROUP_IEM, ("iemMemMap: GCPtrMem=%RGv LB %u - misaligned atomic fallback.\n", GCPtrMem, cbMem));
                pVCpu->iem.s.cMisalignedAtomics += 1;
                return VINF_EM_EMULATE_SPLIT_LOCK;
            }
#endif
        }
    }

#ifdef IEM_WITH_DATA_TLB
    Assert(!(fAccess & IEM_ACCESS_TYPE_EXEC));

    /*
     * Get the TLB entry for this access.
     */
    uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(pVCpu, GCPtrMem);
    PIEMTLBENTRY   pTlbe     = IEMTLB_TAG_TO_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);

    /*
     * Check if it matches and is valid.
     *
     * The first check is for non-global entry with ASID, the alternative
     * is a global one with the ASID set to zero.  The VMID will be zero if
     * not in use by the current translation regime.
     *
     * Note! The NSE+NS state shouldn't need checking in the TLBE, since the
     *       translation regime match makes sure we've allowed to access it.
     *       (We wouldn't have loaded the TLBE if if the walk resulted in a
     *       fault of any kind.)
     */
    uint64_t * const puTlbPhysRevAndStuff = IEMARM_IS_POSITIVE_64BIT_ADDR(GCPtrMem)
                                          ? &pVCpu->iem.s.DataTlb.uTlbPhysRevAndStuff0
                                          : &pVCpu->iem.s.DataTlb.uTlbPhysRevAndStuff1;
    uint64_t const   uTlbPhysRevAndStuff  = *puTlbPhysRevAndStuff;
    Assert(   (uTlbPhysRevAndStuff & IEMTLBE_F_REGIME_MASK)
           == ((pVCpu->iem.s.fExec & IEM_F_ARM_REGIME_MASK) >> (IEM_F_ARM_REGIME_SHIFT - IEMTLBE_F_REGIME_SHIFT)) );
    Assert(uTlbPhysRevAndStuff & IEMTLBE_F_NG);

    bool const       fPrivileged = IEM_F_MODE_ARM_GET_EL(pVCpu->iem.s.fExec) > 0 || (fAccess & IEM_ACCESS_WHAT_SYS);
    uint64_t const   fTlbeAcc    = fPrivileged
                                 ?   (fAccess & IEM_ACCESS_TYPE_READ  ? IEMTLBE_F_EFF_P_NO_READ : 0)
                                   | (fAccess & IEM_ACCESS_TYPE_WRITE ? IEMTLBE_F_EFF_P_NO_WRITE | IEMTLBE_F_EFF_NO_DIRTY : 0)
                                 :   (fAccess & IEM_ACCESS_TYPE_READ  ? IEMTLBE_F_EFF_U_NO_READ : 0)
                                   | (fAccess & IEM_ACCESS_TYPE_WRITE ? IEMTLBE_F_EFF_U_NO_WRITE | IEMTLBE_F_EFF_NO_DIRTY : 0);
    uint8_t         *pbMem       = NULL;
    /** @todo  IEMTLBE_F_EFF_U_NO_GCS / IEMTLBE_F_EFF_P_NO_GCS,
     *  IEMTLBE_F_S1_NS/NSE, IEMTLBE_F_S2_NO_LIM_WRITE/TL0/TL1. */
    /** @todo If the access incompatible, we currently trigger a PT walk,
     *        which isn't necessarily correct... */
    if (   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
        && (      (pTlbe->fFlagsAndPhysRev & (IEMTLBE_F_REGIME_MASK | IEMTLBE_F_NG | IEMTLBE_F_S1_ASID | IEMTLBE_F_S2_VMID | fTlbeAcc))
               == (uTlbPhysRevAndStuff     & (IEMTLBE_F_REGIME_MASK | IEMTLBE_F_NG | IEMTLBE_F_S1_ASID | IEMTLBE_F_S2_VMID))
            ||    (pTlbe->fFlagsAndPhysRev & (IEMTLBE_F_REGIME_MASK | IEMTLBE_F_NG | IEMTLBE_F_S1_ASID | IEMTLBE_F_S2_VMID | fTlbeAcc))
               == (uTlbPhysRevAndStuff     & (IEMTLBE_F_REGIME_MASK |                                    IEMTLBE_F_S2_VMID))
           )
       )
    {
# ifdef IEM_WITH_TLB_STATISTICS
        pVCpu->iem.s.DataTlb.cTlbCoreHits++;
# endif

        /* Look up the physical page info if necessary. */
        if ((pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PHYS_REV) == (uTlbPhysRevAndStuff & IEMTLBE_F_PHYS_REV))
# ifdef IN_RING3
            pbMem = pTlbe->pbMappingR3;
# else
            pbMem = NULL;
# endif
        else
        {
            if (RT_LIKELY(uTlbPhysRevAndStuff >= IEMTLB_PHYS_REV_INCR * 2U))
            { /* likely */ }
            else
                iemTlbInvalidateAllPhysicalSlow(pVCpu);
            pTlbe->pbMappingR3       = NULL;
            pTlbe->fFlagsAndPhysRev &= ~IEMTLBE_GCPHYS2PTR_MASK;
            int rc = PGMPhysIemGCPhys2PtrNoLock(pVCpu->CTX_SUFF(pVM), pVCpu, pTlbe->GCPhys & IEMTLBE_GCPHYS_F_PHYS_MASK,
                                                puTlbPhysRevAndStuff, &pbMem, &pTlbe->fFlagsAndPhysRev);
            AssertRCReturn(rc, rc);
# ifdef IN_RING3
            pTlbe->pbMappingR3 = pbMem;
# endif
        }
    }
    else
    {
        /*
         * The TLB entry didn't match, so we have to preform a translation
         * table walk.
         *
         * This walking will set A & D bits as required by the access while
         * performing the walk.  ASSUMES these are set when the address is
         * translated rather than on instruction commit...
         */
        /** @todo testcase: check when A & D bits are actually set by the CPU for code. */

        pVCpu->iem.s.DataTlb.cTlbCoreMisses++;

        /* This page table walking will set A & D bits as required by the access while performing the walk.
           ASSUMES these are set when the address is translated rather than on commit... */
        /** @todo testcase: check when A bits are actually set by the CPU for code.  */
        PGMPTWALKFAST WalkFast;
        int rc = PGMGstQueryPageFast(pVCpu, GCPtrMem, iemMemArmAccessToQPage(pVCpu, fAccess), &WalkFast);
        if (RT_SUCCESS(rc))
            Assert((WalkFast.fInfo & PGM_WALKINFO_SUCCEEDED) && WalkFast.fFailed == PGM_WALKFAIL_SUCCESS);
        else
        {
            LogEx(LOG_GROUP_IEM, ("iemMemMap: GCPtrMem=%RGv - failed to fetch page -> #PF\n", GCPtrMem));
            /** @todo stage 2 exceptions. */
            return iemRaiseDataAbortFromWalk(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess, rc, &WalkFast);
        }

        uint32_t fDataBps;
        if (   RT_LIKELY(!(pVCpu->iem.s.fExec & IEM_F_PENDING_BRK_DATA))
            || RT_LIKELY(!(fDataBps = iemMemCheckDataBreakpoint(pVCpu->CTX_SUFF(pVM), pVCpu, GCPtrMem, cbMem, fAccess))))
        { /* likely */ }
        else if (fDataBps == 1)
        {
            /* There is one or more data breakpionts in the current page, so we use a dummy
               TLBE to force all accesses to the page with the data access breakpoint armed
               on it to pass thru here. */
            pTlbe = &pVCpu->iem.s.DataBreakpointTlbe;
            //pTlbe->uTag = uTagNoRev;
        }
        else
        {
            LogEx(LOG_GROUP_IEM, ("iemMemMap: Data breakpoint: fDataBps=%#x for %RGv LB %zx; fAccess=%#x PC=%016RX64\n",
                                  fDataBps, GCPtrMem, cbMem, fAccess, pVCpu->cpum.GstCtx.Pc.u64));
            return iemRaiseDebugDataAccessOrInvokeDbgf(pVCpu, fDataBps, GCPtrMem, cbMem, fAccess);
        }

        /* Calc flags. */
        uint64_t const fTlbe        = iemMemArmPtAttrsToTlbeFlags(WalkFast.fEffective, WalkFast.fInfo, uTlbPhysRevAndStuff);
        uint64_t const fGCPhysFlags = iemMemArmPtAttrsToGCPhysFlags(WalkFast.fEffective);
        AssertMsg(!(fTlbe & fTlbeAcc), ("%#RX64 vs %#RX64\n", fTlbe, fTlbeAcc));

        /*
         * Initialize the TLB entry.
         */
        pTlbe->uTag             = uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision;
        pTlbe->fFlagsAndPhysRev = fTlbe;
        RTGCPHYS const GCPhysPg = WalkFast.GCPhys & ~(RTGCPHYS)GUEST_MIN_PAGE_OFFSET_MASK;
        Assert(!(GCPhysPg & ~IEMTLBE_GCPHYS_F_PHYS_MASK));
        pTlbe->GCPhys           = GCPhysPg | fGCPhysFlags;
        pTlbe->pbMappingR3      = NULL;
        Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_NO_DIRTY) || !(fAccess & IEM_ACCESS_TYPE_WRITE));

        /** @todo arm: PGM_WALKINFO_BIG_PAGE++ & large/giant page fun */
        //if (WalkFast.fInfo & PGM_WALKINFO_BIG_PAGE)
        //    iemTlbLoadedLargePage<false>(pVCpu, &pVCpu->iem.s.DataTlb, uTagNoRev, RT_BOOL(pVCpu->cpum.GstCtx.cr4 & X86_CR4_PAE));
# ifdef IEMTLB_WITH_LARGE_PAGE_BITMAP
        //else
            ASMBitClear(pVCpu->iem.s.DataTlb.bmLargePage, IEMTLB_TAG_TO_EVEN_INDEX(uTagNoRev));
# endif

        if (pTlbe != &pVCpu->iem.s.DataBreakpointTlbe)
            IEMTLBTRACE_LOAD(pVCpu, GCPtrMem, pTlbe->GCPhys, (uint32_t)pTlbe->fFlagsAndPhysRev, true);

        /* Resolve the physical address. */
        Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_GCPHYS2PTR_MASK));
        rc = PGMPhysIemGCPhys2PtrNoLock(pVCpu->CTX_SUFF(pVM), pVCpu, GCPhysPg, puTlbPhysRevAndStuff,
                                        &pbMem, &pTlbe->fFlagsAndPhysRev);
        AssertRCReturn(rc, rc);
# ifdef IN_RING3
        pTlbe->pbMappingR3 = pbMem;
# endif
    }

    /*
     * Check the physical page level access and mapping.
     */
    if (   !(pTlbe->fFlagsAndPhysRev & (IEMTLBE_F_PG_NO_WRITE | IEMTLBE_F_PG_NO_READ))
        || !(pTlbe->fFlagsAndPhysRev & (  (fAccess & IEM_ACCESS_TYPE_WRITE ? IEMTLBE_F_PG_NO_WRITE : 0)
                                        | (fAccess & IEM_ACCESS_TYPE_READ  ? IEMTLBE_F_PG_NO_READ  : 0))) )
    { /* probably likely */ }
    else
        return iemMemBounceBufferMapPhys(pVCpu, iMemMap, ppvMem, pbUnmapInfo, cbMem,
                                         pTlbe->GCPhys | (GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK),
                                         fAccess,
                                           pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_UNASSIGNED ? VERR_PGM_PHYS_TLB_UNASSIGNED
                                         : pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_NO_READ    ? VERR_PGM_PHYS_TLB_CATCH_ALL
                                                                                             : VERR_PGM_PHYS_TLB_CATCH_WRITE);
    Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_NO_MAPPINGR3)); /* ASSUMPTIONS about PGMPhysIemGCPhys2PtrNoLock behaviour. */

    if (pbMem)
    {
        Assert(!((uintptr_t)pbMem & GUEST_MIN_PAGE_OFFSET_MASK));
        pbMem    = pbMem + (GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK);
        fAccess |= IEM_ACCESS_NOT_LOCKED;
    }
    else
    {
        Assert(!(fAccess & IEM_ACCESS_NOT_LOCKED));
        RTGCPHYS const GCPhysFirst = pTlbe->GCPhys | (GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK);
        VBOXSTRICTRC rcStrict = iemMemPageMap(pVCpu, GCPhysFirst, fAccess,
                                              (void **)&pbMem, &pVCpu->iem.s.aMemMappingLocks[iMemMap].Lock);
        if (rcStrict != VINF_SUCCESS)
            return iemMemBounceBufferMapPhys(pVCpu, iMemMap, ppvMem, pbUnmapInfo, cbMem, GCPhysFirst, fAccess, rcStrict);
    }

    void * const pvMem = pbMem;

    if (fAccess & IEM_ACCESS_TYPE_WRITE)
        Log6(("IEM WR %RGv (%RGp) LB %#zx\n", GCPtrMem, pTlbe->GCPhys | (GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK), cbMem));
    if (fAccess & IEM_ACCESS_TYPE_READ)
        Log2(("IEM RD %RGv (%RGp) LB %#zx\n", GCPtrMem, pTlbe->GCPhys | (GCPtrMem & GUEST_MIN_PAGE_OFFSET_MASK), cbMem));

#else  /* !IEM_WITH_DATA_TLB */

    RTGCPHYS GCPhysFirst;
    VBOXSTRICTRC rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess, &GCPhysFirst);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    if (fAccess & IEM_ACCESS_TYPE_WRITE)
        Log6(("IEM WR %RGv (%RGp) LB %#zx\n", GCPtrMem, GCPhysFirst, cbMem));
    if (fAccess & IEM_ACCESS_TYPE_READ)
        Log2(("IEM RD %RGv (%RGp) LB %#zx\n", GCPtrMem, GCPhysFirst, cbMem));

    void *pvMem;
    rcStrict = iemMemPageMap(pVCpu, GCPhysFirst, fAccess, &pvMem, &pVCpu->iem.s.aMemMappingLocks[iMemMap].Lock);
    if (rcStrict != VINF_SUCCESS)
        return iemMemBounceBufferMapPhys(pVCpu, iMemMap, ppvMem, pbUnmapInfo, cbMem, GCPhysFirst, fAccess, rcStrict);

#endif /* !IEM_WITH_DATA_TLB */

    /*
     * Fill in the mapping table entry.
     */
    pVCpu->iem.s.aMemMappings[iMemMap].pv      = pvMem;
    pVCpu->iem.s.aMemMappings[iMemMap].fAccess = fAccess;
    pVCpu->iem.s.iNextMapping     = iMemMap + 1;
    pVCpu->iem.s.cActiveMappings += 1;

    *ppvMem = pvMem;
    *pbUnmapInfo = iMemMap | 0x08 | ((fAccess & IEM_ACCESS_TYPE_MASK) << 4);
    AssertCompile(IEM_ACCESS_TYPE_MASK <= 0xf);
    AssertCompile(RT_ELEMENTS(pVCpu->iem.s.aMemMappings) < 8);

    return VINF_SUCCESS;
}


/**
 * Maps the specified guest memory for the given kind of access, longjmp on
 * error.
 *
 * This may be using bounce buffering of the memory if it's crossing a page
 * boundary or if there is an access handler installed for any of it.  Because
 * of lock prefix guarantees, we're in for some extra clutter when this
 * happens.
 *
 * This may raise a \#GP, \#SS, \#PF or \#AC.
 *
 * @returns Pointer to the mapped memory.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling thread.
 * @param   bUnmapInfo  Where to return unmap info to be passed to
 *                      iemMemCommitAndUnmapJmp, iemMemCommitAndUnmapRwSafeJmp,
 *                      iemMemCommitAndUnmapWoSafeJmp,
 *                      iemMemCommitAndUnmapRoSafeJmp,
 *                      iemMemRollbackAndUnmapWoSafe or iemMemRollbackAndUnmap
 *                      when done.
 * @param   cbMem       The number of bytes to map.  This is usually 1,
 *                      2, 4, 6, 8, 12, 16, 32 or 512.  When used by
 *                      string operations it can be up to a page.
 * @param   iSegReg     The index of the segment register to use for
 *                      this access.  The base and limits are checked.
 *                      Use UINT8_MAX to indicate that no segmentation
 *                      is required (for IDT, GDT and LDT accesses).
 * @param   GCPtrMem    The address of the guest memory.
 * @param   fAccess     How the memory is being accessed. The
 *                      IEM_ACCESS_TYPE_XXX part is used to figure out how to
 *                      map the memory, while the IEM_ACCESS_WHAT_XXX part is
 *                      used when raising exceptions. The IEM_ACCESS_ATOMIC and
 *                      IEM_ACCESS_PARTIAL_WRITE bits are also allowed to be
 *                      set.
 * @param   uAlignCtl   Alignment control:
 *                          - Bits 15:0 is the alignment mask.
 *                          - Bits 31:16 for flags like IEM_MEMMAP_F_ALIGN_GP,
 *                            IEM_MEMMAP_F_ALIGN_SSE, and
 *                            IEM_MEMMAP_F_ALIGN_GP_OR_AC.
 *                      Pass zero to skip alignment.
 * @tparam  a_fSafe     Whether this is a call from "safe" fallback function in
 *                      IEMAllMemRWTmpl.cpp.h (@c true) or a generic one that
 *                      needs counting as such in the statistics.
 */
template<bool a_fSafeCall = false>
static void *iemMemMapJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, size_t cbMem, RTGCPTR GCPtrMem,
                          uint32_t fAccess, uint32_t uAlignCtl) IEM_NOEXCEPT_MAY_LONGJMP
{
    STAM_COUNTER_INC(&pVCpu->iem.s.StatMemMapJmp);
#if 1 /** @todo redo this according when iemMemMap() has been fully debugged. */
    void        *pvMem    = NULL;
    VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMem, pbUnmapInfo, cbMem, GCPtrMem, fAccess, uAlignCtl);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
    return pvMem;

#else /* later */

    /*
     * Check the input, check segment access and adjust address
     * with segment base.
     */
    Assert(cbMem <= 64 || cbMem == 512 || cbMem == 108 || cbMem == 104 || cbMem == 94); /* 512 is the max! */
    Assert(!(fAccess & ~(IEM_ACCESS_TYPE_MASK | IEM_ACCESS_WHAT_MASK | IEM_ACCESS_ATOMIC | IEM_ACCESS_PARTIAL_WRITE)));
    Assert(pVCpu->iem.s.cActiveMappings < RT_ELEMENTS(pVCpu->iem.s.aMemMappings));

    VBOXSTRICTRC rcStrict = iemMemApplySegment(pVCpu, fAccess, iSegReg, cbMem, &GCPtrMem);
    if (rcStrict == VINF_SUCCESS) { /*likely*/ }
    else IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));

    /*
     * Alignment check.
     */
    if ( (GCPtrMem & (uAlignCtl & UINT16_MAX)) == 0 )
    { /* likelyish */ }
    else
    {
        /* Misaligned access. */
        if ((fAccess & IEM_ACCESS_WHAT_MASK) != IEM_ACCESS_WHAT_SYS)
        {
            if (   !(uAlignCtl & IEM_MEMMAP_F_ALIGN_GP)
                || (   (uAlignCtl & IEM_MEMMAP_F_ALIGN_SSE)
                    && (pVCpu->cpum.GstCtx.XState.x87.MXCSR & X86_MXCSR_MM)) )
            {
                AssertCompile(X86_CR0_AM == X86_EFL_AC);

                if (iemMemAreAlignmentChecksEnabled(pVCpu))
                    iemRaiseAlignmentCheckExceptionJmp(pVCpu);
            }
            else if (   (uAlignCtl & IEM_MEMMAP_F_ALIGN_GP_OR_AC)
                     && (GCPtrMem & 3) /* The value 4 matches 10980xe's FXSAVE and helps make bs3-cpu-basic2 work. */
                    /** @todo may only apply to 2, 4 or 8 byte misalignments depending on the CPU
                     * implementation. See FXSAVE/FRSTOR/XSAVE/XRSTOR/++.  Using 4 for now as
                     * that's what FXSAVE does on a 10980xe. */
                     && iemMemAreAlignmentChecksEnabled(pVCpu))
                iemRaiseAlignmentCheckExceptionJmp(pVCpu);
            else
                iemRaiseGeneralProtectionFault0Jmp(pVCpu);
        }

#if (defined(RT_ARCH_AMD64) && defined(RT_OS_LINUX)) || defined(RT_ARCH_ARM64)
        /* If the access is atomic there are host platform alignmnet restrictions
           we need to conform with. */
        if (   !(fAccess & IEM_ACCESS_ATOMIC)
# if defined(RT_ARCH_AMD64)
            || (64U - (GCPtrMem & 63U) >= cbMem) /* split-lock detection. ASSUMES 64 byte cache line. */
# elif defined(RT_ARCH_ARM64)
            || (16U - (GCPtrMem & 15U) >= cbMem) /* LSE2 allows atomics anywhere within a 16 byte sized & aligned block. */
# else
#  error port me
# endif
           )
        { /* okay */ }
        else
        {
            LogEx(LOG_GROUP_IEM, ("iemMemMap: GCPtrMem=%RGv LB %u - misaligned atomic fallback.\n", GCPtrMem, cbMem));
            pVCpu->iem.s.cMisalignedAtomics += 1;
            IEM_DO_LONGJMP(pVCpu, VINF_EM_EMULATE_SPLIT_LOCK);
        }
#endif
    }

    /*
     * Figure out which mapping entry to use.
     */
    unsigned iMemMap = pVCpu->iem.s.iNextMapping;
    if (   iMemMap >= RT_ELEMENTS(pVCpu->iem.s.aMemMappings)
        || pVCpu->iem.s.aMemMappings[iMemMap].fAccess != IEM_ACCESS_INVALID)
    {
        iMemMap = iemMemMapFindFree(pVCpu);
        AssertLogRelMsgStmt(iMemMap < RT_ELEMENTS(pVCpu->iem.s.aMemMappings),
                            ("active=%d fAccess[0] = {%#x, %#x, %#x}\n", pVCpu->iem.s.cActiveMappings,
                             pVCpu->iem.s.aMemMappings[0].fAccess, pVCpu->iem.s.aMemMappings[1].fAccess,
                             pVCpu->iem.s.aMemMappings[2].fAccess),
                            IEM_DO_LONGJMP(pVCpu, VERR_IEM_IPE_9));
    }

    /*
     * Crossing a page boundary?
     */
    if ((GCPtrMem & GUEST_PAGE_OFFSET_MASK) + cbMem <= GUEST_PAGE_SIZE)
    { /* No (likely). */ }
    else
    {
        void *pvMem;
        rcStrict = iemMemBounceBufferMapCrossPage(pVCpu, iMemMap, &pvMem, pbUnmapInfo, cbMem, GCPtrMem, fAccess);
        if (rcStrict == VINF_SUCCESS)
            return pvMem;
        IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
    }

#ifdef IEM_WITH_DATA_TLB
    Assert(!(fAccess & IEM_ACCESS_TYPE_EXEC));

    /*
     * Get the TLB entry for this page checking that it has the A & D bits
     * set as per fAccess flags.
     */
    /** @todo make the caller pass these in with fAccess. */
    uint64_t const     fNoUser          = (fAccess & IEM_ACCESS_WHAT_MASK) != IEM_ACCESS_WHAT_SYS && IEM_GET_CPL(pVCpu) == 3
                                        ? IEMTLBE_F_PT_NO_USER : 0;
    uint64_t const     fNoWriteNoDirty  = fAccess & IEM_ACCESS_TYPE_WRITE
                                        ? IEMTLBE_F_PG_NO_WRITE | IEMTLBE_F_PT_NO_DIRTY
                                          | (   (pVCpu->cpum.GstCtx.cr0 & X86_CR0_WP)
                                             || (IEM_GET_CPL(pVCpu) == 3 && (fAccess & IEM_ACCESS_WHAT_MASK) != IEM_ACCESS_WHAT_SYS)
                                             ? IEMTLBE_F_PT_NO_WRITE : 0)
                                        : 0;
    uint64_t const     fNoRead          = fAccess & IEM_ACCESS_TYPE_READ ? IEMTLBE_F_PG_NO_READ : 0;
    uint64_t const     uTagNoRev        = IEMTLB_CALC_TAG_NO_REV(pVCpu, GCPtrMem);
    PIEMTLBENTRY       pTlbe            = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
    uint64_t const     fTlbeAD          = IEMTLBE_F_PT_NO_ACCESSED | (fNoWriteNoDirty & IEMTLBE_F_PT_NO_DIRTY);
    if (   (   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
            && !(pTlbe->fFlagsAndPhysRev & fTlbeAD) )
        || (   (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)
            && !(pTlbe->fFlagsAndPhysRev & fTlbeAD) ) )
    {
# ifdef IEM_WITH_TLB_STATISTICS
        if (a_fSafeCall)
            pVCpu->iem.s.DataTlb.cTlbSafeHits++;
        else
            pVCpu->iem.s.DataTlb.cTlbCoreHits++;
# endif
    }
    else
    {
        if (a_fSafeCall)
            pVCpu->iem.s.DataTlb.cTlbSafeMisses++;
        else
            pVCpu->iem.s.DataTlb.cTlbCoreMisses++;

        /* This page table walking will set A and D bits as required by the
           access while performing the walk.
           ASSUMES these are set when the address is translated rather than on commit... */
        /** @todo testcase: check when A and D bits are actually set by the CPU.  */
        PGMPTWALKFAST WalkFast;
        AssertCompile(IEM_ACCESS_TYPE_READ  == PGMQPAGE_F_READ);
        AssertCompile(IEM_ACCESS_TYPE_WRITE == PGMQPAGE_F_WRITE);
        AssertCompile(IEM_ACCESS_TYPE_EXEC  == PGMQPAGE_F_EXECUTE);
        AssertCompile(X86_CR0_WP            == PGMQPAGE_F_CR0_WP0);
        uint32_t fQPage = (fAccess & (PGMQPAGE_F_READ | IEM_ACCESS_TYPE_WRITE | PGMQPAGE_F_EXECUTE))
                        | (((uint32_t)pVCpu->cpum.GstCtx.cr0 & X86_CR0_WP) ^ X86_CR0_WP);
        if (IEM_GET_CPL(pVCpu) == 3 && !(fAccess & IEM_ACCESS_WHAT_SYS))
            fQPage |= PGMQPAGE_F_USER_MODE;
        int rc = PGMGstQueryPageFast(pVCpu, GCPtrMem, fQPage, &WalkFast);
        if (RT_SUCCESS(rc))
            Assert((WalkFast.fInfo & PGM_WALKINFO_SUCCEEDED) && WalkFast.fFailed == PGM_WALKFAIL_SUCCESS);
        else
        {
            LogEx(LOG_GROUP_IEM, ("iemMemMap: GCPtrMem=%RGv - failed to fetch page -> #PF\n", GCPtrMem));
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
            if (WalkFast.fFailed & PGM_WALKFAIL_EPT)
                IEM_VMX_VMEXIT_EPT_RET(pVCpu, &Walk, fAccess, IEM_SLAT_FAIL_LINEAR_TO_PHYS_ADDR, 0 /* cbInstr */);
# endif
            iemRaisePageFaultJmp(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess, rc);
        }

        uint32_t fDataBps;
        if (   RT_LIKELY(!(pVCpu->iem.s.fExec & IEM_F_PENDING_BRK_DATA))
            || RT_LIKELY(!(fDataBps = iemMemCheckDataBreakpoint(pVCpu->CTX_SUFF(pVM), pVCpu, GCPtrMem, cbMem, fAccess))))
        {
            if (   !(WalkFast.fEffective & PGM_PTATTRS_G_MASK)
                || IEM_GET_CPL(pVCpu) != 0) /* optimization: Only use the PTE.G=1 entries in ring-0. */
            {
                pTlbe--;
                pTlbe->uTag         = uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision;
                if (WalkFast.fInfo & PGM_WALKINFO_BIG_PAGE)
                    iemTlbLoadedLargePage<false>(pVCpu, &pVCpu->iem.s.DataTlb, uTagNoRev, RT_BOOL(pVCpu->cpum.GstCtx.cr4 & X86_CR4_PAE));
# ifdef IEMTLB_WITH_LARGE_PAGE_BITMAP
                else
                    ASMBitClear(pVCpu->iem.s.DataTlb.bmLargePage, IEMTLB_TAG_TO_EVEN_INDEX(uTagNoRev));
# endif
            }
            else
            {
                if (a_fSafeCall)
                    pVCpu->iem.s.DataTlb.cTlbSafeGlobalLoads++;
                else
                    pVCpu->iem.s.DataTlb.cTlbCoreGlobalLoads++;
                pTlbe->uTag         = uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal;
                if (WalkFast.fInfo & PGM_WALKINFO_BIG_PAGE)
                    iemTlbLoadedLargePage<true>(pVCpu, &pVCpu->iem.s.DataTlb, uTagNoRev, RT_BOOL(pVCpu->cpum.GstCtx.cr4 & X86_CR4_PAE));
# ifdef IEMTLB_WITH_LARGE_PAGE_BITMAP
                else
                    ASMBitClear(pVCpu->iem.s.DataTlb.bmLargePage, IEMTLB_TAG_TO_EVEN_INDEX(uTagNoRev) + 1);
# endif
            }
        }
        else
        {
            /* If we hit a data breakpoint, we use a dummy TLBE to force all accesses
               to the page with the data access breakpoint armed on it to pass thru here. */
            if (fDataBps > 1)
                LogEx(LOG_GROUP_IEM, ("iemMemMapJmp<%d>: Data breakpoint: fDataBps=%#x for %RGv LB %zx; fAccess=%#x cs:rip=%04x:%08RX64\n",
                                      a_fSafeCall, fDataBps, GCPtrMem, cbMem, fAccess, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
            pVCpu->cpum.GstCtx.eflags.uBoth |= fDataBps & (CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_DBG_DBGF_MASK);
            pTlbe = &pVCpu->iem.s.DataBreakpointTlbe;
            pTlbe->uTag = uTagNoRev;
        }
        pTlbe->fFlagsAndPhysRev = (~WalkFast.fEffective & (X86_PTE_US | X86_PTE_RW | X86_PTE_D | X86_PTE_A) /* skipping NX */)
                                | (WalkFast.fInfo & PGM_WALKINFO_BIG_PAGE);
        RTGCPHYS const GCPhysPg = WalkFast.GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
        pTlbe->GCPhys           = GCPhysPg;
        pTlbe->pbMappingR3      = NULL;
        Assert(!(pTlbe->fFlagsAndPhysRev & ((fNoWriteNoDirty & IEMTLBE_F_PT_NO_DIRTY) | IEMTLBE_F_PT_NO_ACCESSED)));
        Assert(   !(pTlbe->fFlagsAndPhysRev & fNoWriteNoDirty & IEMTLBE_F_PT_NO_WRITE)
               || (fQPage & (PGMQPAGE_F_CR0_WP0 | PGMQPAGE_F_USER_MODE)) == PGMQPAGE_F_CR0_WP0);
        Assert(!(pTlbe->fFlagsAndPhysRev & fNoUser & IEMTLBE_F_PT_NO_USER));

        if (pTlbe != &pVCpu->iem.s.DataBreakpointTlbe)
        {
            if (!IEMTLBE_IS_GLOBAL(pTlbe))
                IEMTLBTRACE_LOAD(       pVCpu, GCPtrMem, pTlbe->GCPhys, (uint32_t)pTlbe->fFlagsAndPhysRev, true);
            else
                IEMTLBTRACE_LOAD_GLOBAL(pVCpu, GCPtrMem, pTlbe->GCPhys, (uint32_t)pTlbe->fFlagsAndPhysRev, true);
        }

        /* Resolve the physical address. */
        Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_GCPHYS2PTR_MASK));
        uint8_t *pbMemFullLoad = NULL;
        rc = PGMPhysIemGCPhys2PtrNoLock(pVCpu->CTX_SUFF(pVM), pVCpu, GCPhysPg, &pVCpu->iem.s.DataTlb.uTlbPhysRev,
                                        &pbMemFullLoad, &pTlbe->fFlagsAndPhysRev);
        AssertRCStmt(rc, IEM_DO_LONGJMP(pVCpu, rc));
# ifdef IN_RING3
        pTlbe->pbMappingR3 = pbMemFullLoad;
# endif
    }

    /*
     * Check the flags and physical revision.
     * Note! This will revalidate the uTlbPhysRev after a full load.  This is
     *       just to keep the code structure simple (i.e. avoid gotos or similar).
     */
    uint8_t *pbMem;
    if (   (pTlbe->fFlagsAndPhysRev & (IEMTLBE_F_PHYS_REV | IEMTLBE_F_PT_NO_ACCESSED | fNoRead | fNoWriteNoDirty | fNoUser))
        == pVCpu->iem.s.DataTlb.uTlbPhysRev)
# ifdef IN_RING3
        pbMem = pTlbe->pbMappingR3;
# else
        pbMem = NULL;
# endif
    else
    {
        Assert(!(pTlbe->fFlagsAndPhysRev & ((fNoWriteNoDirty & IEMTLBE_F_PT_NO_DIRTY) | IEMTLBE_F_PT_NO_ACCESSED)));

        /*
         * Okay, something isn't quite right or needs refreshing.
         */
        /* Write to read only memory? */
        if (pTlbe->fFlagsAndPhysRev & fNoWriteNoDirty & IEMTLBE_F_PT_NO_WRITE)
        {
            LogEx(LOG_GROUP_IEM, ("iemMemMapJmp: GCPtrMem=%RGv - read-only page -> #PF\n", GCPtrMem));
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/** @todo TLB: EPT isn't integrated into the TLB stuff, so we don't know whether
 *        to trigger an \#PG or a VM nested paging exit here yet! */
            if (Walk.fFailed & PGM_WALKFAIL_EPT)
                IEM_VMX_VMEXIT_EPT_RET(pVCpu, &Walk, fAccess, IEM_SLAT_FAIL_LINEAR_TO_PAGE_TABLE, 0 /* cbInstr */);
# endif
            iemRaisePageFaultJmp(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess & ~IEM_ACCESS_TYPE_READ, VERR_ACCESS_DENIED);
        }

        /* Kernel memory accessed by userland? */
        if (pTlbe->fFlagsAndPhysRev & fNoUser & IEMTLBE_F_PT_NO_USER)
        {
            LogEx(LOG_GROUP_IEM, ("iemMemMapJmp: GCPtrMem=%RGv - user access to kernel page -> #PF\n", GCPtrMem));
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/** @todo TLB: See above. */
            if (Walk.fFailed & PGM_WALKFAIL_EPT)
                IEM_VMX_VMEXIT_EPT_RET(pVCpu, &Walk, fAccess, IEM_SLAT_FAIL_LINEAR_TO_PAGE_TABLE, 0 /* cbInstr */);
# endif
            iemRaisePageFaultJmp(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess, VERR_ACCESS_DENIED);
        }

        /*
         * Check if the physical page info needs updating.
         */
        if ((pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PHYS_REV) == pVCpu->iem.s.DataTlb.uTlbPhysRev)
# ifdef IN_RING3
            pbMem = pTlbe->pbMappingR3;
# else
            pbMem = NULL;
# endif
        else
        {
            pTlbe->pbMappingR3       = NULL;
            pTlbe->fFlagsAndPhysRev &= ~IEMTLBE_GCPHYS2PTR_MASK;
            pbMem = NULL;
            int rc = PGMPhysIemGCPhys2PtrNoLock(pVCpu->CTX_SUFF(pVM), pVCpu, pTlbe->GCPhys, &pVCpu->iem.s.DataTlb.uTlbPhysRev,
                                                &pbMem, &pTlbe->fFlagsAndPhysRev);
            AssertRCStmt(rc, IEM_DO_LONGJMP(pVCpu, rc));
# ifdef IN_RING3
            pTlbe->pbMappingR3 = pbMem;
# endif
        }

        /*
         * Check the physical page level access and mapping.
         */
        if (!(pTlbe->fFlagsAndPhysRev & ((fNoWriteNoDirty | fNoRead) & (IEMTLBE_F_PG_NO_WRITE | IEMTLBE_F_PG_NO_READ))))
        { /* probably likely */ }
        else
        {
            rcStrict = iemMemBounceBufferMapPhys(pVCpu, iMemMap, (void **)&pbMem, pbUnmapInfo, cbMem,
                                                 pTlbe->GCPhys | (GCPtrMem & GUEST_PAGE_OFFSET_MASK), fAccess,
                                                   pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_UNASSIGNED ? VERR_PGM_PHYS_TLB_UNASSIGNED
                                                 : pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_NO_READ    ? VERR_PGM_PHYS_TLB_CATCH_ALL
                                                                                                     : VERR_PGM_PHYS_TLB_CATCH_WRITE);
            if (rcStrict == VINF_SUCCESS)
                return pbMem;
            IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
        }
    }
    Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_NO_MAPPINGR3)); /* ASSUMPTIONS about PGMPhysIemGCPhys2PtrNoLock behaviour. */

    if (pbMem)
    {
        Assert(!((uintptr_t)pbMem & GUEST_PAGE_OFFSET_MASK));
        pbMem    = pbMem + (GCPtrMem & GUEST_PAGE_OFFSET_MASK);
        fAccess |= IEM_ACCESS_NOT_LOCKED;
    }
    else
    {
        Assert(!(fAccess & IEM_ACCESS_NOT_LOCKED));
        RTGCPHYS const GCPhysFirst = pTlbe->GCPhys | (GCPtrMem & GUEST_PAGE_OFFSET_MASK);
        rcStrict = iemMemPageMap(pVCpu, GCPhysFirst, fAccess, (void **)&pbMem, &pVCpu->iem.s.aMemMappingLocks[iMemMap].Lock);
        if (rcStrict == VINF_SUCCESS)
        {
            *pbUnmapInfo = iMemMap | 0x08 | ((fAccess & IEM_ACCESS_TYPE_MASK) << 4);
            return pbMem;
        }
        IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
    }

    void * const pvMem = pbMem;

    if (fAccess & IEM_ACCESS_TYPE_WRITE)
        Log6(("IEM WR %RGv (%RGp) LB %#zx\n", GCPtrMem, pTlbe->GCPhys | (GCPtrMem & GUEST_PAGE_OFFSET_MASK), cbMem));
    if (fAccess & IEM_ACCESS_TYPE_READ)
        Log2(("IEM RD %RGv (%RGp) LB %#zx\n", GCPtrMem, pTlbe->GCPhys | (GCPtrMem & GUEST_PAGE_OFFSET_MASK), cbMem));

#else  /* !IEM_WITH_DATA_TLB */


    RTGCPHYS GCPhysFirst;
    rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess, &GCPhysFirst);
    if (rcStrict == VINF_SUCCESS) { /*likely*/ }
    else IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));

    if (fAccess & IEM_ACCESS_TYPE_WRITE)
        Log6(("IEM WR %RGv (%RGp) LB %#zx\n", GCPtrMem, GCPhysFirst, cbMem));
    if (fAccess & IEM_ACCESS_TYPE_READ)
        Log2(("IEM RD %RGv (%RGp) LB %#zx\n", GCPtrMem, GCPhysFirst, cbMem));

    void *pvMem;
    rcStrict = iemMemPageMap(pVCpu, GCPhysFirst, fAccess, &pvMem, &pVCpu->iem.s.aMemMappingLocks[iMemMap].Lock);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
    {
        rcStrict = iemMemBounceBufferMapPhys(pVCpu, iMemMap, &pvMem, pbUnmapInfo, cbMem, GCPhysFirst, fAccess, rcStrict);
        if (rcStrict == VINF_SUCCESS)
            return pvMem;
        IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
    }

#endif /* !IEM_WITH_DATA_TLB */

    /*
     * Fill in the mapping table entry.
     */
    pVCpu->iem.s.aMemMappings[iMemMap].pv      = pvMem;
    pVCpu->iem.s.aMemMappings[iMemMap].fAccess = fAccess;
    pVCpu->iem.s.iNextMapping = iMemMap + 1;
    pVCpu->iem.s.cActiveMappings++;

    *pbUnmapInfo = iMemMap | 0x08 | ((fAccess & IEM_ACCESS_TYPE_MASK) << 4);
    return pvMem;
#endif /* later */
}


/** @see iemMemMapJmp */
static void *iemMemMapSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, size_t cbMem, RTGCPTR GCPtrMem,
                              uint32_t fAccess, uint32_t uAlignCtl) IEM_NOEXCEPT_MAY_LONGJMP
{
    return iemMemMapJmp<true /*a_fSafeCall*/>(pVCpu, pbUnmapInfo, cbMem, GCPtrMem, fAccess, uAlignCtl);
}



/*
 * Instantiate R/W templates.
 */
#define TMPL_MEM_NO_PAIR

#define TMPL_MEM_TYPE           uint8_t
#define TMPL_MEM_FN_SUFF        U8
#define TMPL_MEM_FMT_TYPE       "%#04x"
#define TMPL_MEM_FMT_DESC       "byte"
#include "IEMAllMemRWTmpl-armv8.cpp.h"

#define TMPL_MEM_TYPE           uint16_t
#define TMPL_MEM_FN_SUFF        U16
#define TMPL_MEM_FMT_TYPE       "%#06x"
#define TMPL_MEM_FMT_DESC       "word"
#include "IEMAllMemRWTmpl-armv8.cpp.h"

#undef  TMPL_MEM_NO_PAIR

#define TMPL_MEM_TYPE           uint32_t
#define TMPL_MEM_FN_SUFF        U32
#define TMPL_MEM_FMT_TYPE       "%#010x"
#define TMPL_MEM_FMT_DESC       "dword"
#include "IEMAllMemRWTmpl-armv8.cpp.h"

#define TMPL_MEM_TYPE           uint64_t
#define TMPL_MEM_FN_SUFF        U64
#define TMPL_MEM_FMT_TYPE       "%#018RX64"
#define TMPL_MEM_FMT_DESC       "qword"
#include "IEMAllMemRWTmpl-armv8.cpp.h"

#define TMPL_MEM_TYPE           RTUINT128U
#define TMPL_MEM_FN_SUFF        U128
#define TMPL_MEM_FMT_TYPE       "%.16Rhxs"
#define TMPL_MEM_FMT_DESC       "dqword"
#include "IEMAllMemRWTmpl-armv8.cpp.h"

#define TMPL_MEM_NO_PAIR

#define TMPL_MEM_TYPE           RTUINT256U
#define TMPL_MEM_FN_SUFF        U256
#define TMPL_MEM_FMT_TYPE       "%.16Rhxs"
#define TMPL_MEM_FMT_DESC       "dqword"
#include "IEMAllMemRWTmpl-armv8.cpp.h"

#undef  TMPL_MEM_NO_PAIR


#if 0 /** @todo ARM: more memory stuff... */
/**
 * Fetches a system table byte.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pbDst               Where to return the byte.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 */
VBOXSTRICTRC iemMemFetchSysU8(PVMCPUCC pVCpu, uint8_t *pbDst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT
{
    /* The lazy approach for now... */
    uint8_t        bUnmapInfo;
    uint8_t const *pbSrc;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&pbSrc, &bUnmapInfo, sizeof(*pbSrc), iSegReg, GCPtrMem, IEM_ACCESS_SYS_R, 0);
    if (rc == VINF_SUCCESS)
    {
        *pbDst = *pbSrc;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    }
    return rc;
}


/**
 * Fetches a system table word.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu16Dst             Where to return the word.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 */
VBOXSTRICTRC iemMemFetchSysU16(PVMCPUCC pVCpu, uint16_t *pu16Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT
{
    /* The lazy approach for now... */
    uint8_t         bUnmapInfo;
    uint16_t const *pu16Src;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&pu16Src, &bUnmapInfo, sizeof(*pu16Src), iSegReg, GCPtrMem, IEM_ACCESS_SYS_R, 0);
    if (rc == VINF_SUCCESS)
    {
        *pu16Dst = *pu16Src;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    }
    return rc;
}


/**
 * Fetches a system table dword.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu32Dst             Where to return the dword.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 */
VBOXSTRICTRC iemMemFetchSysU32(PVMCPUCC pVCpu, uint32_t *pu32Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT
{
    /* The lazy approach for now... */
    uint8_t         bUnmapInfo;
    uint32_t const *pu32Src;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&pu32Src, &bUnmapInfo, sizeof(*pu32Src), iSegReg, GCPtrMem, IEM_ACCESS_SYS_R, 0);
    if (rc == VINF_SUCCESS)
    {
        *pu32Dst = *pu32Src;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    }
    return rc;
}


/**
 * Fetches a system table qword.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu64Dst             Where to return the qword.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 */
VBOXSTRICTRC iemMemFetchSysU64(PVMCPUCC pVCpu, uint64_t *pu64Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT
{
    /* The lazy approach for now... */
    uint8_t         bUnmapInfo;
    uint64_t const *pu64Src;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&pu64Src, &bUnmapInfo, sizeof(*pu64Src), iSegReg, GCPtrMem, IEM_ACCESS_SYS_R, 0);
    if (rc == VINF_SUCCESS)
    {
        *pu64Dst = *pu64Src;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    }
    return rc;
}

#endif /* work in progress */

/** @} */

