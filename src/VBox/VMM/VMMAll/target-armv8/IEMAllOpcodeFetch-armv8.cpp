/* $Id: IEMAllOpcodeFetch-armv8.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Opcode Fetching, ARMv8.
 */

/*
 * Copyright (C) 2011-2025 Oracle and/or its affiliates.
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
#define LOG_GROUP   LOG_GROUP_IEM
#define VMCPU_INCL_CPUM_GST_CTX
#ifdef IN_RING0
# define VBOX_VMM_TARGET_ARMV8
#endif
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/dbgf.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/armv8.h>

#include "IEMInline.h"
#include "IEMInline-armv8.h"
#include "IEMAllTlbInline-armv8.h"


#ifdef IEM_WITH_CODE_TLB

/**
 * Tries to fetches a @a a_RetType, raise the appropriate exception on failure
 * and jumps.
 *
 * We end up here for a number of reasons:
 *      - pbInstrBuf isn't yet initialized.
 *      - Advancing beyond the buffer boundrary (e.g. crossing to new page).
 *      - Fetching from non-mappable page (e.g. MMIO).
 *      - TLB loading in the recompiler (@a a_fTlbLoad = true).
 *
 * @returns The fetched opcode for non-TLB loads, zero for TLB loads.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 *
 * @tparam  a_RetType           The return type - uint32_t or uint16_t.
 * @tparam  a_fTlbLoad          Set if this a TLB load that and should just set
 *                              pbInstrBuf, cbInstrBuf and friends before
 *                              returning zero.
 * @tparam  a_cbPrevInstrHalf   Number of instruction bytes preceding the
 *                              fetch.  This is non-zero when fetching the 2nd
 *                              16-bit word in a 32-bit T32 instruction.
 *                              Otherwise it will always be zero.
 */
template<typename a_RetType, bool const a_fTlbLoad = false, unsigned const a_cbPrevInstrHalf = 0>
DECLINLINE(a_RetType) iemOpcodeFetchBytesJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    AssertCompile(sizeof(a_RetType) == sizeof(uint16_t) || sizeof(a_RetType) == sizeof(uint32_t));

# ifdef IN_RING3
    /*
     * We expect the fetches to be naturally aligned on ARM, so there is
     * no need for partial fetching or such fun.
     */
    if RT_CONSTEXPR_IF(a_fTlbLoad)
        Assert(!pVCpu->iem.s.pbInstrBuf); /* pbInstrBuf shall be NULL in case of a TLB load */
    else
        Assert(!pVCpu->iem.s.pbInstrBuf || pVCpu->iem.s.offInstrNextByte >= pVCpu->iem.s.cbInstrBufTotal);

    /*
     * Calculate the virtual address of the instruction.
     *
     * ASSUMES that PC contains a stripped (no PAuth or tags) and
     * fully signextended address.
     *
     * ASSUMES that the code advancing 32-bit PC register makes sure
     * to do so without going above the 32-bit space.
     */
    RTGCPTR  const GCPtrFirst = pVCpu->cpum.GstCtx.Pc.u64 + a_cbPrevInstrHalf;
    uint32_t const cbMaxRead  = GUEST_MIN_PAGE_SIZE - ((uint32_t)GCPtrFirst & GUEST_MIN_PAGE_OFFSET_MASK);
    Assert(!(pVCpu->iem.s.fExec & IEM_F_MODE_ARM_32BIT) || GCPtrFirst < _4G);
    Assert((GCPtrFirst & (sizeof(a_RetType) - 1)) == 0); /* ASSUMES PC is aligned correctly */

    /*
     * Get the TLB entry for this piece of code.
     */
    uint64_t const     uTagNoRev = IEMTLB_CALC_TAG_NO_REV(pVCpu, GCPtrFirst);
    PIEMTLBENTRY const pTlbe     = IEMTLB_TAG_TO_ENTRY(&pVCpu->iem.s.CodeTlb, uTagNoRev);

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
    uint64_t * const puTlbPhysRevAndStuff = IEMARM_IS_POSITIVE_64BIT_ADDR(GCPtrFirst)
                                          ? &pVCpu->iem.s.CodeTlb.uTlbPhysRevAndStuff0
                                          : &pVCpu->iem.s.CodeTlb.uTlbPhysRevAndStuff1;
    uint64_t const   uTlbPhysRevAndStuff  = *puTlbPhysRevAndStuff;
    Assert(   (uTlbPhysRevAndStuff & IEMTLBE_F_REGIME_MASK)
           == ((pVCpu->iem.s.fExec & IEM_F_ARM_REGIME_MASK) >> (IEM_F_ARM_REGIME_SHIFT - IEMTLBE_F_REGIME_SHIFT)) );
    Assert(uTlbPhysRevAndStuff & IEMTLBE_F_NG);
    if (   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.CodeTlb.uTlbRevision)
        && (      (pTlbe->fFlagsAndPhysRev  & (IEMTLBE_F_REGIME_MASK | IEMTLBE_F_NG | IEMTLBE_F_S1_ASID | IEMTLBE_F_S2_VMID))
               == (uTlbPhysRevAndStuff      & (IEMTLBE_F_REGIME_MASK | IEMTLBE_F_NG | IEMTLBE_F_S1_ASID | IEMTLBE_F_S2_VMID))
            ||    (pTlbe->fFlagsAndPhysRev  & (IEMTLBE_F_REGIME_MASK | IEMTLBE_F_NG | IEMTLBE_F_S1_ASID | IEMTLBE_F_S2_VMID))
               == (uTlbPhysRevAndStuff      & (IEMTLBE_F_REGIME_MASK |                                    IEMTLBE_F_S2_VMID))
           )
       )
    {
        /* likely when executing lots of code, otherwise unlikely */
#  ifdef IEM_WITH_TLB_STATISTICS
        pVCpu->iem.s.CodeTlb.cTlbCoreHits++;
#  endif

        /* Check TLB page table level access flags. */
        uint64_t const fTlbeNoExec = IEM_F_MODE_ARM_GET_EL(pVCpu->iem.s.fExec) == 0
                                   ? IEMTLBE_F_EFF_U_NO_EXEC : IEMTLBE_F_EFF_P_NO_EXEC;
        if ((pTlbe->fFlagsAndPhysRev & fTlbeNoExec) == 0)
        { /* likely */ }
        else
        {
            Log(("iemOpcodeFetchBytesJmp: %RGv - noexec EL%u\n", GCPtrFirst, IEM_F_MODE_ARM_GET_EL(pVCpu->iem.s.fExec) ));
            iemRaiseInstructionAbortTlbPermisionJmp(pVCpu, GCPtrFirst, sizeof(a_RetType), pTlbe);
        }

        /* Look up the physical page info if necessary. */
        if ((pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PHYS_REV) == (uTlbPhysRevAndStuff & IEMTLBE_F_PHYS_REV))
        { /* not necessary */ }
        else
        {
            if (RT_LIKELY(uTlbPhysRevAndStuff >= IEMTLB_PHYS_REV_INCR * 2U))
            { /* likely */ }
            else
                iemTlbInvalidateAllPhysicalSlow(pVCpu);
            pTlbe->fFlagsAndPhysRev &= ~IEMTLBE_GCPHYS2PTR_MASK;
            int rc = PGMPhysIemGCPhys2PtrNoLock(pVCpu->CTX_SUFF(pVM), pVCpu, pTlbe->GCPhys & IEMTLBE_GCPHYS_F_PHYS_MASK,
                                                puTlbPhysRevAndStuff, &pTlbe->pbMappingR3, &pTlbe->fFlagsAndPhysRev);
            AssertRCStmt(rc, IEM_DO_LONGJMP(pVCpu, rc));
        }
    }
    else
    {
        /*
         * The TLB entry didn't match, so we have to preform a translation
         * table walk.
         *
         * This walking will set A bits as required by the access while
         * performing the walk.  ASSUMES these are set when the address is
         * translated rather than on instruction commit...
         */
        /** @todo testcase: check when A bits are actually set by the CPU for code.  */
        pVCpu->iem.s.CodeTlb.cTlbCoreMisses++;
/** @todo should we specify the current translation regime to ensure PGM and
*        IEM has the same idea about it? */
/** @todo NS + NSE   */
        PGMPTWALKFAST WalkFast;
        int rc = PGMGstQueryPageFast(pVCpu, GCPtrFirst,
                                     IEM_F_MODE_ARM_GET_EL(pVCpu->iem.s.fExec) == 0
                                     ? PGMQPAGE_F_EXECUTE | PGMQPAGE_F_USER_MODE : PGMQPAGE_F_EXECUTE,
                                     &WalkFast);
        if (RT_SUCCESS(rc))
            Assert((WalkFast.fInfo & PGM_WALKINFO_SUCCEEDED) && WalkFast.fFailed == PGM_WALKFAIL_SUCCESS);
        else
        {
            Log(("iemOpcodeFetchMoreBytes: %RGv - rc=%Rrc\n", GCPtrFirst, rc));
            iemRaiseInstructionAbortFromWalkJmp(pVCpu, GCPtrFirst, sizeof(a_RetType), IEM_ACCESS_INSTRUCTION, rc, &WalkFast);
        }

        uint64_t const fEff    = WalkFast.fEffective;
        uint64_t const fEffInv = ~fEff;
        /** @todo we're doing way too much here for a code TLB here... */

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

        if (WalkFast.fInfo & PGM_WALKINFO_IS_SLAT) /** @todo hope this is correct use of the flag... */
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
        Assert(!(fTlbe & (IEM_F_MODE_ARM_GET_EL(pVCpu->iem.s.fExec) == 0 ? IEMTLBE_F_EFF_U_NO_EXEC : IEMTLBE_F_EFF_P_NO_EXEC)));

        if (WalkFast.fInfo & PGM_PTATTRS_NG_MASK)
            fTlbe |= uTlbPhysRevAndStuff & (IEMTLBE_F_REGIME_MASK | IEMTLBE_F_S2_VMID | IEMTLBE_F_NG | IEMTLBE_F_S1_ASID);
        else
            fTlbe |= uTlbPhysRevAndStuff & (IEMTLBE_F_REGIME_MASK | IEMTLBE_F_S2_VMID);

        /* Assemble the flags we stuff in with GCPhys: */
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

        /*
         * Initialize the TLB entry:
         */
        pTlbe->uTag             = uTagNoRev | pVCpu->iem.s.CodeTlb.uTlbRevision;
        pTlbe->fFlagsAndPhysRev = fTlbe;
        RTGCPHYS const GCPhysPg = WalkFast.GCPhys & ~(RTGCPHYS)GUEST_MIN_PAGE_OFFSET_MASK;
        Assert(!(GCPhysPg & ~IEMTLBE_GCPHYS_F_PHYS_MASK));
        pTlbe->GCPhys           = GCPhysPg | fGCPhysFlags;
        pTlbe->pbMappingR3      = NULL;
/// @todo large pages
//            if (WalkFast.fInfo & PGM_WALKINFO_BIG_PAGE)
//                iemTlbLoadedLargePage<false>(pVCpu, &pVCpu->iem.s.CodeTlb, uTagNoRev, RT_BOOL(pVCpu->cpum.GstCtx.cr4 & X86_CR4_PAE));
#  ifdef IEMTLB_WITH_LARGE_PAGE_BITMAP
//            else
//                ASMBitClear(pVCpu->iem.s.CodeTlb.bmLargePage, IEMTLB_TAG_TO_EVEN_INDEX(uTagNoRev));
#  endif

        IEMTLBTRACE_LOAD(pVCpu, GCPtrFirst, pTlbe->GCPhys, (uint32_t)pTlbe->fFlagsAndPhysRev, false);

        /* Resolve the physical address. */
        if (RT_LIKELY(*puTlbPhysRevAndStuff >= IEMTLB_PHYS_REV_INCR * 2U))
        { /* likely */ }
        else
            iemTlbInvalidateAllPhysicalSlow(pVCpu);
        Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_GCPHYS2PTR_MASK));
        rc = PGMPhysIemGCPhys2PtrNoLock(pVCpu->CTX_SUFF(pVM), pVCpu, GCPhysPg, puTlbPhysRevAndStuff,
                                        &pTlbe->pbMappingR3, &pTlbe->fFlagsAndPhysRev);
        AssertRCStmt(rc, IEM_DO_LONGJMP(pVCpu, rc));
    }
    Assert(cbMaxRead >= sizeof(a_RetType));

    /*
     * Try do a direct read using the pbMappingR3 pointer.
     * Note! Do not recheck the physical TLB revision number here as we have the
     *       wrong response to changes in the else case.  If someone is updating
     *       pVCpu->iem.s.CodeTlb.uTlbPhysRev in parallel to us, we should be fine
     *       pretending we always won the race.
     */
    if (RT_LIKELY((pTlbe->fFlagsAndPhysRev & (IEMTLBE_F_NO_MAPPINGR3 | IEMTLBE_F_PG_NO_READ)) == 0U))
    {
        uint32_t const offPg = GCPtrFirst & GUEST_MIN_PAGE_OFFSET_MASK;
        if RT_CONSTEXPR_IF(sizeof(a_RetType) == sizeof(uint16_t) && a_cbPrevInstrHalf > 0)
            pVCpu->iem.s.fTbCrossedPage |= offPg == 0;

        pVCpu->iem.s.cbInstrBufTotal  = GUEST_MIN_PAGE_SIZE;
        pVCpu->iem.s.offInstrNextByte = offPg + sizeof(a_RetType);
        pVCpu->iem.s.uInstrBufPc      = GCPtrFirst & ~(RTGCPTR)GUEST_MIN_PAGE_OFFSET_MASK;
        pVCpu->iem.s.GCPhysInstrBuf   = pTlbe->GCPhys & IEMTLBE_GCPHYS_F_PHYS_MASK;
        pVCpu->iem.s.pbInstrBuf       = pTlbe->pbMappingR3;
/** @todo Need to record GuardedPage bit for the current page! */
        if RT_CONSTEXPR_IF(a_fTlbLoad)
            return 0;
        return *(a_RetType const *)&pTlbe->pbMappingR3[offPg];
    }

    /*
     * Special read handling, so only read exactly what's needed.
     * This is a highly unlikely scenario.
     */
    pVCpu->iem.s.CodeTlb.cTlbSlowCodeReadPath++;

    /* Do the reading. */
    a_RetType          uRetValue = 0;
    RTGCPHYS const     GCPhys    = (pTlbe->GCPhys & IEMTLBE_GCPHYS_F_PHYS_MASK) + (GCPtrFirst & X86_PAGE_OFFSET_MASK);
    VBOXSTRICTRC       rcStrict  = PGMPhysRead(pVCpu->CTX_SUFF(pVM), GCPhys, &uRetValue, sizeof(a_RetType), PGMACCESSORIGIN_IEM);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    { /* likely */ }
    else if (PGM_PHYS_RW_IS_SUCCESS(rcStrict))
    {
        Log(("iemOpcodeFetchBytesJmp: %RGv/%RGp LB %#x - read status -  rcStrict=%Rrc\n",
             GCPtrFirst, GCPhys, VBOXSTRICTRC_VAL(rcStrict), sizeof(a_RetType)));
        rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
        AssertStmt(rcStrict == VINF_SUCCESS, IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict)));
    }
    else
    {
        Log((RT_SUCCESS(rcStrict)
             ? "iemOpcodeFetchBytesJmp: %RGv/%RGp LB %#x - read status - rcStrict=%Rrc\n"
             : "iemOpcodeFetchBytesJmp: %RGv/%RGp LB %#x - read error - rcStrict=%Rrc (!!)\n",
             GCPtrFirst, GCPhys, VBOXSTRICTRC_VAL(rcStrict), sizeof(a_RetType)));
        IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
    }

    /* Update the state and probably return. */
    if RT_CONSTEXPR_IF(sizeof(a_RetType) == sizeof(uint16_t) && a_cbPrevInstrHalf > 0)
        pVCpu->iem.s.fTbCrossedPage |= offPg == 0;

    pVCpu->iem.s.cbInstrBufTotal  = GUEST_MIN_PAGE_SIZE; /** @todo ??? */
    pVCpu->iem.s.offInstrNextByte = (GCPtrFirst & GUEST_MIN_PAGE_OFFSET_MASK) + sizeof(a_RetType);
    pVCpu->iem.s.uInstrBufPc      = GCPtrFirst & ~(RTGCPTR)GUEST_MIN_PAGE_OFFSET_MASK;
    pVCpu->iem.s.GCPhysInstrBuf   = pTlbe->GCPhys & IEMTLBE_GCPHYS_F_PHYS_MASK;
    pVCpu->iem.s.pbInstrBuf       = NULL;
/** @todo Need to record GuardedPage bit for the current page! */

    return uRetValue;

# else  /* !IN_RING3 */
    if (pVCpu)
        IEM_DO_LONGJMP(pVCpu, VERR_INTERNAL_ERROR);
    return 0;
# endif /* !IN_RING3 */
}

#else /* !IEM_WITH_CODE_TLB */

/**
 * Try fetch at least @a cbMin bytes more opcodes, raise the appropriate
 * exception if it fails.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   cbMin               The minimum number of bytes relative offOpcode
 *                              that must be read.
 */
VBOXSTRICTRC iemOpcodeFetchMoreBytes(PVMCPUCC pVCpu, size_t cbMin) RT_NOEXCEPT
{
# if 0 /** @todo later */
    /*
     * What we're doing here is very similar to iemMemMap/iemMemBounceBufferMap.
     *
     * First translate CS:rIP to a physical address.
     */
    uint8_t const   cbOpcode  = pVCpu->iem.s.cbOpcode;
    uint8_t const   offOpcode = pVCpu->iem.s.offOpcode;
    uint8_t const   cbLeft    = cbOpcode - offOpcode;
    Assert(cbLeft < cbMin);
    Assert(cbOpcode <= sizeof(pVCpu->iem.s.abOpcode));

    uint32_t        cbToTryRead;
    RTGCPTR         GCPtrNext;
    if (IEM_IS_64BIT_CODE(pVCpu))
    {
        GCPtrNext   = pVCpu->cpum.GstCtx.rip + cbOpcode;
        if (!IEM_IS_CANONICAL(GCPtrNext))
            return iemRaiseGeneralProtectionFault0(pVCpu);
        cbToTryRead = GUEST_PAGE_SIZE - (GCPtrNext & GUEST_PAGE_OFFSET_MASK);
    }
    else
    {
        uint32_t GCPtrNext32 = pVCpu->cpum.GstCtx.eip;
        /* Assert(!(GCPtrNext32 & ~(uint32_t)UINT16_MAX) || IEM_IS_32BIT_CODE(pVCpu)); - this is allowed */
        GCPtrNext32 += cbOpcode;
        if (GCPtrNext32 > pVCpu->cpum.GstCtx.cs.u32Limit)
            /** @todo For CPUs older than the 386, we should not generate \#GP here but wrap around! */
            return iemRaiseSelectorBounds(pVCpu, X86_SREG_CS, IEM_ACCESS_INSTRUCTION);
        cbToTryRead = pVCpu->cpum.GstCtx.cs.u32Limit - GCPtrNext32 + 1;
        if (!cbToTryRead) /* overflowed */
        {
            Assert(GCPtrNext32 == 0); Assert(pVCpu->cpum.GstCtx.cs.u32Limit == UINT32_MAX);
            cbToTryRead = UINT32_MAX;
            /** @todo check out wrapping around the code segment.  */
        }
        if (cbToTryRead < cbMin - cbLeft)
            return iemRaiseSelectorBounds(pVCpu, X86_SREG_CS, IEM_ACCESS_INSTRUCTION);
        GCPtrNext = (uint32_t)pVCpu->cpum.GstCtx.cs.u64Base + GCPtrNext32;

        uint32_t cbLeftOnPage = GUEST_PAGE_SIZE - (GCPtrNext & GUEST_PAGE_OFFSET_MASK);
        if (cbToTryRead > cbLeftOnPage)
            cbToTryRead = cbLeftOnPage;
    }

    /* Restrict to opcode buffer space.

       We're making ASSUMPTIONS here based on work done previously in
       iemInitDecoderAndPrefetchOpcodes, where bytes from the first page will
       be fetched in case of an instruction crossing two pages. */
    if (cbToTryRead > sizeof(pVCpu->iem.s.abOpcode) - cbOpcode)
        cbToTryRead = sizeof(pVCpu->iem.s.abOpcode) - cbOpcode;
    if (RT_LIKELY(cbToTryRead + cbLeft >= cbMin))
    { /* likely */ }
    else
    {
        Log(("iemOpcodeFetchMoreBytes: %04x:%08RX64 LB %#x + %#zx -> #GP(0)\n",
             pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, offOpcode, cbMin));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    PGMPTWALKFAST WalkFast;
    int rc = PGMGstQueryPageFast(pVCpu, GCPtrNext,
                                 IEM_GET_CPL(pVCpu) == 3 ? PGMQPAGE_F_EXECUTE | PGMQPAGE_F_USER_MODE : PGMQPAGE_F_EXECUTE,
                                 &WalkFast);
    if (RT_SUCCESS(rc))
        Assert((WalkFast.fInfo & PGM_WALKINFO_SUCCEEDED) && WalkFast.fFailed == PGM_WALKFAIL_SUCCESS);
    else
    {
        Log(("iemOpcodeFetchMoreBytes: %RGv - rc=%Rrc\n", GCPtrNext, rc));
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        if (WalkFast.fFailed & PGM_WALKFAIL_EPT)
            IEM_VMX_VMEXIT_EPT_RET(pVCpu, &WalkFast, IEM_ACCESS_INSTRUCTION, IEM_SLAT_FAIL_LINEAR_TO_PHYS_ADDR, 0 /* cbInstr */);
#endif
        return iemRaisePageFault(pVCpu, GCPtrNext, 1, IEM_ACCESS_INSTRUCTION, rc);
    }
    Assert((WalkFast.fEffective & X86_PTE_US) || IEM_GET_CPL(pVCpu) != 3);
    Assert(!(WalkFast.fEffective & X86_PTE_PAE_NX) || !(pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_NXE));

    RTGCPHYS const GCPhys = WalkFast.GCPhys;
    Log5(("GCPtrNext=%RGv GCPhys=%RGp cbOpcodes=%#x\n",  GCPtrNext,  GCPhys, cbOpcode));

    /*
     * Read the bytes at this address.
     *
     * We read all unpatched bytes in iemInitDecoderAndPrefetchOpcodes already,
     * and since PATM should only patch the start of an instruction there
     * should be no need to check again here.
     */
    if (!(pVCpu->iem.s.fExec & IEM_F_BYPASS_HANDLERS))
    {
        VBOXSTRICTRC rcStrict = PGMPhysRead(pVCpu->CTX_SUFF(pVM), GCPhys, &pVCpu->iem.s.abOpcode[cbOpcode],
                                            cbToTryRead, PGMACCESSORIGIN_IEM);
        if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        { /* likely */ }
        else if (PGM_PHYS_RW_IS_SUCCESS(rcStrict))
        {
            Log(("iemOpcodeFetchMoreBytes: %RGv/%RGp LB %#x - read status -  rcStrict=%Rrc\n",
                 GCPtrNext, GCPhys, VBOXSTRICTRC_VAL(rcStrict), cbToTryRead));
            rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
        }
        else
        {
            Log((RT_SUCCESS(rcStrict)
                 ? "iemOpcodeFetchMoreBytes: %RGv/%RGp LB %#x - read status - rcStrict=%Rrc\n"
                 : "iemOpcodeFetchMoreBytes: %RGv/%RGp LB %#x - read error - rcStrict=%Rrc (!!)\n",
                 GCPtrNext, GCPhys, VBOXSTRICTRC_VAL(rcStrict), cbToTryRead));
            return rcStrict;
        }
    }
    else
    {
        rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &pVCpu->iem.s.abOpcode[cbOpcode], GCPhys, cbToTryRead);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
        {
            Log(("iemOpcodeFetchMoreBytes: %RGv - read error - rc=%Rrc (!!)\n", GCPtrNext, rc));
            return rc;
        }
    }
    pVCpu->iem.s.cbOpcode = cbOpcode + cbToTryRead;
    Log5(("%.*Rhxs\n", pVCpu->iem.s.cbOpcode, pVCpu->iem.s.abOpcode));

    return VINF_SUCCESS;
# else
    RT_NOREF(pVCpu, cbMin);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
# endif
}

#endif /* !IEM_WITH_CODE_TLB */

/**
 * Deals with the problematic cases that iemOpcodeGetNextU16Jmp doesn't like, longjmp on error
 *
 * @returns The opcode word.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
uint16_t iemOpcodeGetU16SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
#ifdef IEM_WITH_CODE_TLB
    return iemOpcodeFetchBytesJmp<uint16_t>(pVCpu);
#else
    VBOXSTRICTRC rcStrict = iemOpcodeFetchMoreBytes(pVCpu, 2);
    if (rcStrict == VINF_SUCCESS)
    {
        uint8_t offOpcode = pVCpu->iem.s.offOpcode;
        pVCpu->iem.s.offOpcode += 2;
# ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        return *(uint16_t const *)&pVCpu->iem.s.abOpcode[offOpcode];
# else
        return RT_MAKE_U16(pVCpu->iem.s.abOpcode[offOpcode], pVCpu->iem.s.abOpcode[offOpcode + 1]);
# endif
    }
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
#endif
}


/**
 * Deals with the problematic cases that iemOpcodeGetNextU32Jmp doesn't like, longjmp on error.
 *
 * @returns The opcode dword.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
uint32_t iemOpcodeGetU32SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
#ifdef IEM_WITH_CODE_TLB
    return iemOpcodeFetchBytesJmp<uint32_t>(pVCpu);
#else
    VBOXSTRICTRC rcStrict = iemOpcodeFetchMoreBytes(pVCpu, 4);
    if (rcStrict == VINF_SUCCESS)
    {
        uint8_t offOpcode = pVCpu->iem.s.offOpcode;
        pVCpu->iem.s.offOpcode = offOpcode + 4;
# ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        return *(uint32_t const *)&pVCpu->iem.s.abOpcode[offOpcode];
# else
        return RT_MAKE_U32_FROM_U8(pVCpu->iem.s.abOpcode[offOpcode],
                                   pVCpu->iem.s.abOpcode[offOpcode + 1],
                                   pVCpu->iem.s.abOpcode[offOpcode + 2],
                                   pVCpu->iem.s.abOpcode[offOpcode + 3]);
# endif
    }
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
#endif
}

