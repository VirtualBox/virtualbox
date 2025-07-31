/* $Id: IEMAllMem-x86.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - x86 target, memory.
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
#ifdef IN_RING0
# define VBOX_VMM_TARGET_X86
#endif
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
#include <iprt/x86.h>

#include "IEMInline.h"
#include "IEMInline-x86.h"
#include "IEMInlineMem-x86.h"
#include "IEMAllTlbInline-x86.h"


/** @name   Memory access.
 *
 * @{
 */

/**
 * Applies the segment limit, base and attributes.
 *
 * This may raise a \#GP or \#SS.
 *
 * @returns VBox strict status code.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   fAccess             The kind of access which is being performed.
 * @param   iSegReg             The index of the segment register to apply.
 *                              This is UINT8_MAX if none (for IDT, GDT, LDT,
 *                              TSS, ++).
 * @param   cbMem               The access size.
 * @param   pGCPtrMem           Pointer to the guest memory address to apply
 *                              segmentation to.  Input and output parameter.
 */
VBOXSTRICTRC iemMemApplySegment(PVMCPUCC pVCpu, uint32_t fAccess, uint8_t iSegReg, size_t cbMem, PRTGCPTR pGCPtrMem) RT_NOEXCEPT
{
    if (iSegReg == UINT8_MAX)
        return VINF_SUCCESS;

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));
    PCPUMSELREGHID pSel = iemSRegGetHid(pVCpu, iSegReg);
    switch (IEM_GET_CPU_MODE(pVCpu))
    {
        case IEMMODE_16BIT:
        case IEMMODE_32BIT:
        {
            RTGCPTR32 GCPtrFirst32 = (RTGCPTR32)*pGCPtrMem;
            RTGCPTR32 GCPtrLast32  = GCPtrFirst32 + (uint32_t)cbMem - 1;

            if (   pSel->Attr.n.u1Present
                && !pSel->Attr.n.u1Unusable)
            {
                Assert(pSel->Attr.n.u1DescType);
                if (!(pSel->Attr.n.u4Type & X86_SEL_TYPE_CODE))
                {
                    if (   (fAccess & IEM_ACCESS_TYPE_WRITE)
                        && !(pSel->Attr.n.u4Type & X86_SEL_TYPE_WRITE) )
                        return iemRaiseSelectorInvalidAccess(pVCpu, iSegReg, fAccess);

                    if (!IEM_IS_REAL_OR_V86_MODE(pVCpu))
                    {
                        /** @todo CPL check. */
                    }

                    /*
                     * There are two kinds of data selectors, normal and expand down.
                     */
                    if (!(pSel->Attr.n.u4Type & X86_SEL_TYPE_DOWN))
                    {
                        if (   GCPtrFirst32 > pSel->u32Limit
                            || GCPtrLast32  > pSel->u32Limit) /* yes, in real mode too (since 80286). */
                            return iemRaiseSelectorBounds(pVCpu, iSegReg, fAccess);
                    }
                    else
                    {
                       /*
                        * The upper boundary is defined by the B bit, not the G bit!
                        */
                       if (   GCPtrFirst32 < pSel->u32Limit + UINT32_C(1)
                           || GCPtrLast32  > (pSel->Attr.n.u1DefBig ? UINT32_MAX : UINT32_C(0xffff)))
                          return iemRaiseSelectorBounds(pVCpu, iSegReg, fAccess);
                    }
                    *pGCPtrMem = GCPtrFirst32 += (uint32_t)pSel->u64Base;
                }
                else
                {
                    /*
                     * Code selector and usually be used to read thru, writing is
                     * only permitted in real and V8086 mode.
                     */
                    if (   (   (fAccess & IEM_ACCESS_TYPE_WRITE)
                            || (   (fAccess & IEM_ACCESS_TYPE_READ)
                               && !(pSel->Attr.n.u4Type & X86_SEL_TYPE_READ)) )
                        && !IEM_IS_REAL_OR_V86_MODE(pVCpu) )
                        return iemRaiseSelectorInvalidAccess(pVCpu, iSegReg, fAccess);

                    if (   GCPtrFirst32 > pSel->u32Limit
                        || GCPtrLast32  > pSel->u32Limit) /* yes, in real mode too (since 80286). */
                        return iemRaiseSelectorBounds(pVCpu, iSegReg, fAccess);

                    if (!IEM_IS_REAL_OR_V86_MODE(pVCpu))
                    {
                        /** @todo CPL check. */
                    }

                    *pGCPtrMem  = GCPtrFirst32 += (uint32_t)pSel->u64Base;
                }
            }
            else
                return iemRaiseGeneralProtectionFault0(pVCpu);
            return VINF_SUCCESS;
        }

        case IEMMODE_64BIT:
        {
            RTGCPTR GCPtrMem = *pGCPtrMem;
            if (iSegReg == X86_SREG_GS || iSegReg == X86_SREG_FS)
                *pGCPtrMem = GCPtrMem + pSel->u64Base;

            Assert(cbMem >= 1);
            if (RT_LIKELY(X86_IS_CANONICAL(GCPtrMem) && X86_IS_CANONICAL(GCPtrMem + cbMem - 1)))
                return VINF_SUCCESS;
            /** @todo We should probably raise \#SS(0) here if segment is SS; see AMD spec.
             *        4.12.2 "Data Limit Checks in 64-bit Mode". */
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }

        default:
            AssertFailedReturn(VERR_IEM_IPE_7);
    }
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
    /** @todo Need a different PGM interface here.  We're currently using
     *        generic / REM interfaces. this won't cut it for R0. */
    /** @todo If/when PGM handles paged real-mode, we can remove the hack in
     *        iemSvmWorldSwitch/iemVmxWorldSwitch to work around raising a page-fault
     *        here. */
    Assert(!(fAccess & IEM_ACCESS_TYPE_EXEC));
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
    {
        Assert((WalkFast.fInfo & PGM_WALKINFO_SUCCEEDED) && WalkFast.fFailed == PGM_WALKFAIL_SUCCESS);

        /* If the page is writable and does not have the no-exec bit set, all
           access is allowed.  Otherwise we'll have to check more carefully... */
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
    return iemRaisePageFault(pVCpu, GCPtrMem, cbAccess, fAccess, rc);
}


/**
 * Finds a free memmap entry when using iNextMapping doesn't work.
 *
 * @returns Memory mapping index, 1024 on failure.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
static unsigned iemMemMapFindFree(PVMCPUCC pVCpu)
{
    /*
     * The easy case.
     */
    if (pVCpu->iem.s.cActiveMappings == 0)
    {
        pVCpu->iem.s.iNextMapping = 1;
        return 0;
    }

    /* There should be enough mappings for all instructions. */
    AssertReturn(pVCpu->iem.s.cActiveMappings < RT_ELEMENTS(pVCpu->iem.s.aMemMappings), 1024);

    for (unsigned i = 0; i < RT_ELEMENTS(pVCpu->iem.s.aMemMappings); i++)
        if (pVCpu->iem.s.aMemMappings[i].fAccess == IEM_ACCESS_INVALID)
            return i;

    AssertFailedReturn(1024);
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
 *                      8, 12, 16, 32 or 512.  When used by string operations
 *                      it can be up to a page.
 * @param   iSegReg     The index of the segment register to use for this
 *                      access.  The base and limits are checked. Use UINT8_MAX
 *                      to indicate that no segmentation is required (for IDT,
 *                      GDT and LDT accesses).
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
VBOXSTRICTRC iemMemMap(PVMCPUCC pVCpu, void **ppvMem, uint8_t *pbUnmapInfo, size_t cbMem, uint8_t iSegReg, RTGCPTR GCPtrMem,
                       uint32_t fAccess, uint32_t uAlignCtl) RT_NOEXCEPT
{
    STAM_COUNTER_INC(&pVCpu->iem.s.StatMemMapNoJmp);

    /*
     * Check the input and figure out which mapping entry to use.
     */
    Assert(cbMem <= sizeof(pVCpu->iem.s.aBounceBuffers[0]));
    Assert(   cbMem <= 64 || cbMem == 512 || cbMem == 256 || cbMem == 108 || cbMem == 104 || cbMem == 102 || cbMem == 94
           || (iSegReg == UINT8_MAX && uAlignCtl == 0 && fAccess == IEM_ACCESS_DATA_R /* for the CPUID logging interface */) );
    Assert(!(fAccess & ~(IEM_ACCESS_TYPE_MASK | IEM_ACCESS_WHAT_MASK | IEM_ACCESS_ATOMIC | IEM_ACCESS_PARTIAL_WRITE)));
    Assert(pVCpu->iem.s.cActiveMappings < RT_ELEMENTS(pVCpu->iem.s.aMemMappings));

    unsigned iMemMap = pVCpu->iem.s.iNextMapping;
    if (   iMemMap >= RT_ELEMENTS(pVCpu->iem.s.aMemMappings)
        || pVCpu->iem.s.aMemMappings[iMemMap].fAccess != IEM_ACCESS_INVALID)
    {
        iMemMap = iemMemMapFindFree(pVCpu);
        AssertLogRelMsgReturn(iMemMap < RT_ELEMENTS(pVCpu->iem.s.aMemMappings),
                              ("active=%d fAccess[0] = {%#x, %#x, %#x}\n", pVCpu->iem.s.cActiveMappings,
                               pVCpu->iem.s.aMemMappings[0].fAccess, pVCpu->iem.s.aMemMappings[1].fAccess,
                               pVCpu->iem.s.aMemMappings[2].fAccess),
                              VERR_IEM_IPE_9);
    }

    /*
     * Map the memory, checking that we can actually access it.  If something
     * slightly complicated happens, fall back on bounce buffering.
     */
    VBOXSTRICTRC rcStrict = iemMemApplySegment(pVCpu, fAccess, iSegReg, cbMem, &GCPtrMem);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    if ((GCPtrMem & GUEST_PAGE_OFFSET_MASK) + cbMem <= GUEST_PAGE_SIZE) /* Crossing a page boundary? */
    { /* likely */ }
    else
        return iemMemBounceBufferMapCrossPage(pVCpu, iMemMap, ppvMem, pbUnmapInfo, cbMem, GCPtrMem, fAccess);

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

                if (!iemMemAreAlignmentChecksEnabled(pVCpu))
                { /* likely */ }
                else
                    return iemRaiseAlignmentCheckException(pVCpu);
            }
            else if (   (uAlignCtl & IEM_MEMMAP_F_ALIGN_GP_OR_AC)
                     && (GCPtrMem & 3) /* The value 4 matches 10980xe's FXSAVE and helps make bs3-cpu-basic2 work. */
                    /** @todo may only apply to 2, 4 or 8 byte misalignments depending on the CPU
                     * implementation. See FXSAVE/FRSTOR/XSAVE/XRSTOR/++.  Using 4 for now as
                     * that's what FXSAVE does on a 10980xe. */
                     && iemMemAreAlignmentChecksEnabled(pVCpu))
                return iemRaiseAlignmentCheckException(pVCpu);
            else
                return iemRaiseGeneralProtectionFault0(pVCpu);
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
            return VINF_EM_EMULATE_SPLIT_LOCK;
        }
#endif
    }

#ifdef IEM_WITH_DATA_TLB
    Assert(!(fAccess & IEM_ACCESS_TYPE_EXEC));

    /*
     * Get the TLB entry for this page and check PT flags.
     *
     * We reload the TLB entry if we need to set the dirty bit (accessed
     * should in theory always be set).
     */
    uint8_t           *pbMem     = NULL;
    uint64_t const     uTagNoRev = IEMTLB_CALC_TAG_NO_REV(pVCpu, GCPtrMem);
    PIEMTLBENTRY       pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&pVCpu->iem.s.DataTlb, uTagNoRev);
    uint64_t const     fTlbeAD   = IEMTLBE_F_PT_NO_ACCESSED | (fAccess & IEM_ACCESS_TYPE_WRITE ? IEMTLBE_F_PT_NO_DIRTY : 0);
    if (   (   pTlbe->uTag               == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevision)
            && !(pTlbe->fFlagsAndPhysRev & fTlbeAD) )
        || (   (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | pVCpu->iem.s.DataTlb.uTlbRevisionGlobal)
            && !(pTlbe->fFlagsAndPhysRev & fTlbeAD) ) )
    {
# ifdef IEM_WITH_TLB_STATISTICS
        pVCpu->iem.s.DataTlb.cTlbCoreHits++;
# endif

        /* If the page is either supervisor only or non-writable, we need to do
           more careful access checks. */
        if (pTlbe->fFlagsAndPhysRev & (IEMTLBE_F_PT_NO_USER | IEMTLBE_F_PT_NO_WRITE))
        {
            /* Write to read only memory? */
            if (   (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_WRITE)
                && (fAccess & IEM_ACCESS_TYPE_WRITE)
                && (   (    IEM_GET_CPL(pVCpu) == 3
                        && !(fAccess & IEM_ACCESS_WHAT_SYS))
                    || (pVCpu->cpum.GstCtx.cr0 & X86_CR0_WP)))
            {
                LogEx(LOG_GROUP_IEM, ("iemMemMap: GCPtrMem=%RGv - read-only page -> #PF\n", GCPtrMem));
                return iemRaisePageFault(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess & ~IEM_ACCESS_TYPE_READ, VERR_ACCESS_DENIED);
            }

            /* Kernel memory accessed by userland? */
            if (   (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_USER)
                && IEM_GET_CPL(pVCpu) == 3
                && !(fAccess & IEM_ACCESS_WHAT_SYS))
            {
                LogEx(LOG_GROUP_IEM, ("iemMemMap: GCPtrMem=%RGv - user access to kernel page -> #PF\n", GCPtrMem));
                return iemRaisePageFault(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess, VERR_ACCESS_DENIED);
            }
        }

        /* Look up the physical page info if necessary. */
        if ((pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PHYS_REV) == pVCpu->iem.s.DataTlb.uTlbPhysRev)
# ifdef IN_RING3
            pbMem = pTlbe->pbMappingR3;
# else
            pbMem = NULL;
# endif
        else
        {
            if (RT_LIKELY(pVCpu->iem.s.CodeTlb.uTlbPhysRev > IEMTLB_PHYS_REV_INCR))
            { /* likely */ }
            else
                iemTlbInvalidateAllPhysicalSlow(pVCpu);
            pTlbe->pbMappingR3       = NULL;
            pTlbe->fFlagsAndPhysRev &= ~IEMTLBE_GCPHYS2PTR_MASK;
            int rc = PGMPhysIemGCPhys2PtrNoLock(pVCpu->CTX_SUFF(pVM), pVCpu, pTlbe->GCPhys, &pVCpu->iem.s.DataTlb.uTlbPhysRev,
                                                &pbMem, &pTlbe->fFlagsAndPhysRev);
            AssertRCReturn(rc, rc);
# ifdef IN_RING3
            pTlbe->pbMappingR3 = pbMem;
# endif
        }
    }
    else
    {
        pVCpu->iem.s.DataTlb.cTlbCoreMisses++;

        /* This page table walking will set A bits as required by the access while performing the walk.
           ASSUMES these are set when the address is translated rather than on commit... */
        /** @todo testcase: check when A bits are actually set by the CPU for code.  */
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
                IEM_VMX_VMEXIT_EPT_RET(pVCpu, &WalkFast, fAccess, IEM_SLAT_FAIL_LINEAR_TO_PHYS_ADDR, 0 /* cbInstr */);
# endif
            return iemRaisePageFault(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess, rc);
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
                LogEx(LOG_GROUP_IEM, ("iemMemMap: Data breakpoint: fDataBps=%#x for %RGv LB %zx; fAccess=%#x cs:rip=%04x:%08RX64\n",
                                      fDataBps, GCPtrMem, cbMem, fAccess, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
            pVCpu->cpum.GstCtx.eflags.uBoth |= fDataBps & (CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_DBG_DBGF_MASK);
            pTlbe = &pVCpu->iem.s.DataBreakpointTlbe;
            pTlbe->uTag = uTagNoRev;
        }
        pTlbe->fFlagsAndPhysRev = (~WalkFast.fEffective & (X86_PTE_US | X86_PTE_RW | X86_PTE_D | X86_PTE_A) /* skipping NX */)
                                | (WalkFast.fInfo & PGM_WALKINFO_BIG_PAGE);
        RTGCPHYS const GCPhysPg = WalkFast.GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
        pTlbe->GCPhys           = GCPhysPg;
        pTlbe->pbMappingR3      = NULL;
        Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_ACCESSED));
        Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_DIRTY) || !(fAccess & IEM_ACCESS_TYPE_WRITE));
        Assert(   !(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_WRITE)
               || !(fAccess & IEM_ACCESS_TYPE_WRITE)
               || (fQPage & (PGMQPAGE_F_CR0_WP0 | PGMQPAGE_F_USER_MODE)) == PGMQPAGE_F_CR0_WP0);
        Assert(   !(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_USER)
               || IEM_GET_CPL(pVCpu) != 3
               || (fAccess & IEM_ACCESS_WHAT_SYS));

        if (pTlbe != &pVCpu->iem.s.DataBreakpointTlbe)
        {
            if (!IEMTLBE_IS_GLOBAL(pTlbe))
                IEMTLBTRACE_LOAD(       pVCpu, GCPtrMem, pTlbe->GCPhys, (uint32_t)pTlbe->fFlagsAndPhysRev, true);
            else
                IEMTLBTRACE_LOAD_GLOBAL(pVCpu, GCPtrMem, pTlbe->GCPhys, (uint32_t)pTlbe->fFlagsAndPhysRev, true);
        }

        /* Resolve the physical address. */
        Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_GCPHYS2PTR_MASK));
        rc = PGMPhysIemGCPhys2PtrNoLock(pVCpu->CTX_SUFF(pVM), pVCpu, GCPhysPg, &pVCpu->iem.s.DataTlb.uTlbPhysRev,
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
                                         pTlbe->GCPhys | (GCPtrMem & GUEST_PAGE_OFFSET_MASK), fAccess,
                                           pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_UNASSIGNED ? VERR_PGM_PHYS_TLB_UNASSIGNED
                                         : pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_NO_READ    ? VERR_PGM_PHYS_TLB_CATCH_ALL
                                                                                             : VERR_PGM_PHYS_TLB_CATCH_WRITE);
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
        if (rcStrict != VINF_SUCCESS)
            return iemMemBounceBufferMapPhys(pVCpu, iMemMap, ppvMem, pbUnmapInfo, cbMem, GCPhysFirst, fAccess, rcStrict);
    }

    void * const pvMem = pbMem;

    if (fAccess & IEM_ACCESS_TYPE_WRITE)
        Log6(("IEM WR %RGv (%RGp) LB %#zx\n", GCPtrMem, pTlbe->GCPhys | (GCPtrMem & GUEST_PAGE_OFFSET_MASK), cbMem));
    if (fAccess & IEM_ACCESS_TYPE_READ)
        Log2(("IEM RD %RGv (%RGp) LB %#zx\n", GCPtrMem, pTlbe->GCPhys | (GCPtrMem & GUEST_PAGE_OFFSET_MASK), cbMem));

#else  /* !IEM_WITH_DATA_TLB */

    RTGCPHYS GCPhysFirst;
    rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, GCPtrMem, (uint32_t)cbMem, fAccess, &GCPhysFirst);
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
static void *iemMemMapJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, size_t cbMem, uint8_t iSegReg, RTGCPTR GCPtrMem,
                          uint32_t fAccess, uint32_t uAlignCtl) IEM_NOEXCEPT_MAY_LONGJMP
{
    STAM_COUNTER_INC(&pVCpu->iem.s.StatMemMapJmp);

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
}


/** @see iemMemMapJmp */
static void *iemMemMapSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, size_t cbMem, uint8_t iSegReg, RTGCPTR GCPtrMem,
                              uint32_t fAccess, uint32_t uAlignCtl) IEM_NOEXCEPT_MAY_LONGJMP
{
    return iemMemMapJmp<true /*a_fSafeCall*/>(pVCpu, pbUnmapInfo, cbMem, iSegReg, GCPtrMem, fAccess, uAlignCtl);
}



/*
 * Instantiate R/W templates.
 */
#define TMPL_MEM_WITH_STACK

#define TMPL_MEM_TYPE       uint8_t
#define TMPL_MEM_FN_SUFF    U8
#define TMPL_MEM_FMT_TYPE   "%#04x"
#define TMPL_MEM_FMT_DESC   "byte"
#include "IEMAllMemRWTmpl-x86.cpp.h"

#define TMPL_MEM_TYPE       uint16_t
#define TMPL_MEM_FN_SUFF    U16
#define TMPL_MEM_FMT_TYPE   "%#06x"
#define TMPL_MEM_FMT_DESC   "word"
#include "IEMAllMemRWTmpl-x86.cpp.h"

#define TMPL_WITH_PUSH_SREG
#define TMPL_MEM_TYPE       uint32_t
#define TMPL_MEM_FN_SUFF    U32
#define TMPL_MEM_FMT_TYPE   "%#010x"
#define TMPL_MEM_FMT_DESC   "dword"
#include "IEMAllMemRWTmpl-x86.cpp.h"
#undef TMPL_WITH_PUSH_SREG

#define TMPL_MEM_TYPE       uint64_t
#define TMPL_MEM_FN_SUFF    U64
#define TMPL_MEM_FMT_TYPE   "%#018RX64"
#define TMPL_MEM_FMT_DESC   "qword"
#include "IEMAllMemRWTmpl-x86.cpp.h"

#undef TMPL_MEM_WITH_STACK

#define TMPL_MEM_TYPE       uint32_t
#define TMPL_MEM_TYPE_ALIGN 0
#define TMPL_MEM_FN_SUFF    U32NoAc
#define TMPL_MEM_FMT_TYPE   "%#010x"
#define TMPL_MEM_FMT_DESC   "dword"
#include "IEMAllMemRWTmpl-x86.cpp.h"
#undef TMPL_WITH_PUSH_SREG

#define TMPL_MEM_TYPE       uint64_t
#define TMPL_MEM_TYPE_ALIGN 0
#define TMPL_MEM_FN_SUFF    U64NoAc
#define TMPL_MEM_FMT_TYPE   "%#018RX64"
#define TMPL_MEM_FMT_DESC   "qword"
#include "IEMAllMemRWTmpl-x86.cpp.h"

#define TMPL_MEM_TYPE       uint64_t
#define TMPL_MEM_TYPE_ALIGN (sizeof(uint64_t) * 2 - 1)
#define TMPL_MEM_FN_SUFF    U64AlignedU128
#define TMPL_MEM_FMT_TYPE   "%#018RX64"
#define TMPL_MEM_FMT_DESC   "qword"
#include "IEMAllMemRWTmpl-x86.cpp.h"

/* See IEMAllMemRWTmplInline.cpp.h */
#define TMPL_MEM_BY_REF

#define TMPL_MEM_TYPE       RTFLOAT80U
#define TMPL_MEM_TYPE_ALIGN (sizeof(uint64_t) - 1)
#define TMPL_MEM_FN_SUFF    R80
#define TMPL_MEM_FMT_TYPE   "%.10Rhxs"
#define TMPL_MEM_FMT_DESC   "tword"
#include "IEMAllMemRWTmpl-x86.cpp.h"

#define TMPL_MEM_TYPE       RTPBCD80U
#define TMPL_MEM_TYPE_ALIGN (sizeof(uint64_t) - 1) /** @todo testcase: 80-bit BCD alignment */
#define TMPL_MEM_FN_SUFF    D80
#define TMPL_MEM_FMT_TYPE   "%.10Rhxs"
#define TMPL_MEM_FMT_DESC   "tword"
#include "IEMAllMemRWTmpl-x86.cpp.h"

#define TMPL_MEM_TYPE       RTUINT128U
#define TMPL_MEM_TYPE_ALIGN (sizeof(RTUINT128U) - 1)
#define TMPL_MEM_FN_SUFF    U128
#define TMPL_MEM_FMT_TYPE   "%.16Rhxs"
#define TMPL_MEM_FMT_DESC   "dqword"
#include "IEMAllMemRWTmpl-x86.cpp.h"

#define TMPL_MEM_TYPE           RTUINT128U
#define TMPL_MEM_TYPE_ALIGN     (sizeof(RTUINT128U) - 1)
#define TMPL_MEM_MAP_FLAGS_ADD  (IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE)
#define TMPL_MEM_FN_SUFF        U128AlignedSse
#define TMPL_MEM_FMT_TYPE       "%.16Rhxs"
#define TMPL_MEM_FMT_DESC       "dqword"
#include "IEMAllMemRWTmpl-x86.cpp.h"

#define TMPL_MEM_TYPE       RTUINT128U
#define TMPL_MEM_TYPE_ALIGN 0
#define TMPL_MEM_FN_SUFF    U128NoAc
#define TMPL_MEM_FMT_TYPE   "%.16Rhxs"
#define TMPL_MEM_FMT_DESC   "dqword"
#include "IEMAllMemRWTmpl-x86.cpp.h"

#define TMPL_MEM_TYPE       RTUINT256U
#define TMPL_MEM_TYPE_ALIGN 0
#define TMPL_MEM_FN_SUFF    U256NoAc
#define TMPL_MEM_FMT_TYPE   "%.32Rhxs"
#define TMPL_MEM_FMT_DESC   "qqword"
#include "IEMAllMemRWTmpl-x86.cpp.h"

#define TMPL_MEM_TYPE           RTUINT256U
#define TMPL_MEM_TYPE_ALIGN     (sizeof(RTUINT256U) - 1)
#define TMPL_MEM_MAP_FLAGS_ADD  IEM_MEMMAP_F_ALIGN_GP
#define TMPL_MEM_FN_SUFF        U256AlignedAvx
#define TMPL_MEM_FMT_TYPE       "%.32Rhxs"
#define TMPL_MEM_FMT_DESC       "qqword"
#include "IEMAllMemRWTmpl-x86.cpp.h"


/**
 * Fetches a data dword and zero extends it to a qword.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu64Dst             Where to return the qword.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 */
VBOXSTRICTRC iemMemFetchDataU32_ZX_U64(PVMCPUCC pVCpu, uint64_t *pu64Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT
{
    /* The lazy approach for now... */
    uint8_t         bUnmapInfo;
    uint32_t const *pu32Src;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&pu32Src, &bUnmapInfo, sizeof(*pu32Src), iSegReg, GCPtrMem,
                                IEM_ACCESS_DATA_R, sizeof(*pu32Src) - 1);
    if (rc == VINF_SUCCESS)
    {
        *pu64Dst = *pu32Src;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        Log(("IEM RD dword %d|%RGv: %#010RX64\n", iSegReg, GCPtrMem, *pu64Dst));
    }
    return rc;
}


#ifdef SOME_UNUSED_FUNCTION
/**
 * Fetches a data dword and sign extends it to a qword.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu64Dst             Where to return the sign extended value.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 */
VBOXSTRICTRC iemMemFetchDataS32SxU64(PVMCPUCC pVCpu, uint64_t *pu64Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT
{
    /* The lazy approach for now... */
    uint8_t        bUnmapInfo;
    int32_t const *pi32Src;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&pi32Src, &bUnmapInfo, sizeof(*pi32Src), iSegReg, GCPtrMem,
                                IEM_ACCESS_DATA_R, sizeof(*pi32Src) - 1);
    if (rc == VINF_SUCCESS)
    {
        *pu64Dst = *pi32Src;
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        Log(("IEM RD dword %d|%RGv: %#010x\n", iSegReg, GCPtrMem, (uint32_t)*pu64Dst));
    }
#ifdef __GNUC__ /* warning: GCC may be a royal pain */
    else
        *pu64Dst = 0;
#endif
    return rc;
}
#endif


/**
 * Fetches a descriptor register (lgdt, lidt).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pcbLimit            Where to return the limit.
 * @param   pGCPtrBase          Where to return the base.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 * @param   enmOpSize           The effective operand size.
 */
VBOXSTRICTRC iemMemFetchDataXdtr(PVMCPUCC pVCpu, uint16_t *pcbLimit, PRTGCPTR pGCPtrBase, uint8_t iSegReg,
                                 RTGCPTR GCPtrMem, IEMMODE enmOpSize) RT_NOEXCEPT
{
    /*
     * Just like SIDT and SGDT, the LIDT and LGDT instructions are a
     * little special:
     *      - The two reads are done separately.
     *      - Operand size override works in 16-bit and 32-bit code, but 64-bit.
     *      - We suspect the 386 to actually commit the limit before the base in
     *        some cases (search for 386 in  bs3CpuBasic2_lidt_lgdt_One).  We
     *        don't try emulate this eccentric behavior, because it's not well
     *        enough understood and rather hard to trigger.
     *      - The 486 seems to do a dword limit read when the operand size is 32-bit.
     */
    VBOXSTRICTRC rcStrict;
    if (IEM_IS_64BIT_CODE(pVCpu))
    {
        rcStrict = iemMemFetchDataU16(pVCpu, pcbLimit, iSegReg, GCPtrMem);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemFetchDataU64(pVCpu, pGCPtrBase, iSegReg, GCPtrMem + 2);
    }
    else
    {
        uint32_t uTmp = 0; /* (Visual C++ maybe used uninitialized) */
        if (enmOpSize == IEMMODE_32BIT)
        {
            if (IEM_GET_TARGET_CPU(pVCpu) != IEMTARGETCPU_486)
            {
                rcStrict = iemMemFetchDataU16(pVCpu, pcbLimit, iSegReg, GCPtrMem);
                if (rcStrict == VINF_SUCCESS)
                    rcStrict = iemMemFetchDataU32(pVCpu, &uTmp, iSegReg, GCPtrMem + 2);
            }
            else
            {
                rcStrict = iemMemFetchDataU32(pVCpu, &uTmp, iSegReg, GCPtrMem);
                if (rcStrict == VINF_SUCCESS)
                {
                    *pcbLimit = (uint16_t)uTmp;
                    rcStrict = iemMemFetchDataU32(pVCpu, &uTmp, iSegReg, GCPtrMem + 2);
                }
            }
            if (rcStrict == VINF_SUCCESS)
                *pGCPtrBase = uTmp;
        }
        else
        {
            rcStrict = iemMemFetchDataU16(pVCpu, pcbLimit, iSegReg, GCPtrMem);
            if (rcStrict == VINF_SUCCESS)
            {
                rcStrict = iemMemFetchDataU32(pVCpu, &uTmp, iSegReg, GCPtrMem + 2);
                if (rcStrict == VINF_SUCCESS)
                    *pGCPtrBase = uTmp & UINT32_C(0x00ffffff);
            }
        }
    }
    return rcStrict;
}


/**
 * Stores a data dqword, SSE aligned.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 * @param   u128Value           The value to store.
 */
VBOXSTRICTRC iemMemStoreDataU128AlignedSse(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) RT_NOEXCEPT
{
    /* The lazy approach for now... */
    uint8_t      bUnmapInfo;
    PRTUINT128U  pu128Dst;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&pu128Dst, &bUnmapInfo, sizeof(*pu128Dst), iSegReg, GCPtrMem, IEM_ACCESS_DATA_W,
                                (sizeof(*pu128Dst) - 1) | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE);
    if (rc == VINF_SUCCESS)
    {
        pu128Dst->au64[0] = u128Value.au64[0];
        pu128Dst->au64[1] = u128Value.au64[1];
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        Log5(("IEM WR dqword %d|%RGv: %.16Rhxs\n", iSegReg, GCPtrMem, pu128Dst));
    }
    return rc;
}


/**
 * Stores a data dqword, SSE aligned.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 * @param   u128Value           The value to store.
 */
void iemMemStoreDataU128AlignedSseJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem,
                                      RTUINT128U u128Value) IEM_NOEXCEPT_MAY_LONGJMP
{
    /* The lazy approach for now... */
    uint8_t     bUnmapInfo;
    PRTUINT128U pu128Dst = (PRTUINT128U)iemMemMapJmp(pVCpu, &bUnmapInfo, sizeof(*pu128Dst), iSegReg, GCPtrMem, IEM_ACCESS_DATA_W,
                                                     (sizeof(*pu128Dst) - 1) | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE);
    pu128Dst->au64[0] = u128Value.au64[0];
    pu128Dst->au64[1] = u128Value.au64[1];
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
    Log5(("IEM WR dqword %d|%RGv: %.16Rhxs\n", iSegReg, GCPtrMem, pu128Dst));
}


/**
 * Stores a data dqword.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 * @param   pu256Value          Pointer to the value to store.
 */
VBOXSTRICTRC iemMemStoreDataU256(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) RT_NOEXCEPT
{
    /* The lazy approach for now... */
    uint8_t      bUnmapInfo;
    PRTUINT256U  pu256Dst;
    VBOXSTRICTRC rc = iemMemMap(pVCpu, (void **)&pu256Dst, &bUnmapInfo, sizeof(*pu256Dst), iSegReg, GCPtrMem,
                                IEM_ACCESS_DATA_W, 0 /* NO_AC variant */);
    if (rc == VINF_SUCCESS)
    {
        pu256Dst->au64[0] = pu256Value->au64[0];
        pu256Dst->au64[1] = pu256Value->au64[1];
        pu256Dst->au64[2] = pu256Value->au64[2];
        pu256Dst->au64[3] = pu256Value->au64[3];
        rc = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        Log5(("IEM WR qqword %d|%RGv: %.32Rhxs\n", iSegReg, GCPtrMem, pu256Dst));
    }
    return rc;
}


/**
 * Stores a data dqword, longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 * @param   pu256Value          Pointer to the value to store.
 */
void iemMemStoreDataU256Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP
{
    /* The lazy approach for now... */
    uint8_t     bUnmapInfo;
    PRTUINT256U pu256Dst = (PRTUINT256U)iemMemMapJmp(pVCpu, &bUnmapInfo, sizeof(*pu256Dst), iSegReg, GCPtrMem,
                                                     IEM_ACCESS_DATA_W, 0 /* NO_AC variant */);
    pu256Dst->au64[0] = pu256Value->au64[0];
    pu256Dst->au64[1] = pu256Value->au64[1];
    pu256Dst->au64[2] = pu256Value->au64[2];
    pu256Dst->au64[3] = pu256Value->au64[3];
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
    Log5(("IEM WR qqword %d|%RGv: %.32Rhxs\n", iSegReg, GCPtrMem, pu256Dst));
}


/**
 * Stores a descriptor register (sgdt, sidt).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbLimit             The limit.
 * @param   GCPtrBase           The base address.
 * @param   iSegReg             The index of the segment register to use for
 *                              this access.  The base and limits are checked.
 * @param   GCPtrMem            The address of the guest memory.
 */
VBOXSTRICTRC iemMemStoreDataXdtr(PVMCPUCC pVCpu, uint16_t cbLimit, RTGCPTR GCPtrBase, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT
{
    /*
     * The SIDT and SGDT instructions actually stores the data using two
     * independent writes (see bs3CpuBasic2_sidt_sgdt_One).  The instructions
     * does not respond to opsize prefixes.
     */
    VBOXSTRICTRC rcStrict = iemMemStoreDataU16(pVCpu, iSegReg, GCPtrMem, cbLimit);
    if (rcStrict == VINF_SUCCESS)
    {
        if (IEM_IS_16BIT_CODE(pVCpu))
            rcStrict = iemMemStoreDataU32(pVCpu, iSegReg, GCPtrMem + 2,
                                          IEM_GET_TARGET_CPU(pVCpu) <= IEMTARGETCPU_286
                                          ? (uint32_t)GCPtrBase | UINT32_C(0xff000000) : (uint32_t)GCPtrBase);
        else if (IEM_IS_32BIT_CODE(pVCpu))
            rcStrict = iemMemStoreDataU32(pVCpu, iSegReg, GCPtrMem + 2, (uint32_t)GCPtrBase);
        else
            rcStrict = iemMemStoreDataU64(pVCpu, iSegReg, GCPtrMem + 2, GCPtrBase);
    }
    return rcStrict;
}


/**
 * Begin a special stack push (used by interrupt, exceptions and such).
 *
 * This will raise \#SS or \#PF if appropriate.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbMem               The number of bytes to push onto the stack.
 * @param   cbAlign             The alignment mask (7, 3, 1).
 * @param   ppvMem              Where to return the pointer to the stack memory.
 *                              As with the other memory functions this could be
 *                              direct access or bounce buffered access, so
 *                              don't commit register until the commit call
 *                              succeeds.
 * @param   pbUnmapInfo         Where to store unmap info for
 *                              iemMemStackPushCommitSpecial.
 * @param   puNewRsp            Where to return the new RSP value.  This must be
 *                              passed unchanged to
 *                              iemMemStackPushCommitSpecial().
 */
VBOXSTRICTRC iemMemStackPushBeginSpecial(PVMCPUCC pVCpu, size_t cbMem, uint32_t cbAlign,
                                         void **ppvMem, uint8_t *pbUnmapInfo, uint64_t *puNewRsp) RT_NOEXCEPT
{
    Assert(cbMem < UINT8_MAX);
    RTGCPTR GCPtrTop = iemRegGetRspForPush(pVCpu, (uint8_t)cbMem, puNewRsp);
    return iemMemMap(pVCpu, ppvMem, pbUnmapInfo, cbMem, X86_SREG_SS, GCPtrTop, IEM_ACCESS_STACK_W, cbAlign);
}


/**
 * Commits a special stack push (started by iemMemStackPushBeginSpecial).
 *
 * This will update the rSP.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   bUnmapInfo          Unmap info set by iemMemStackPushBeginSpecial.
 * @param   uNewRsp             The new RSP value returned by
 *                              iemMemStackPushBeginSpecial().
 */
VBOXSTRICTRC iemMemStackPushCommitSpecial(PVMCPUCC pVCpu, uint8_t bUnmapInfo, uint64_t uNewRsp) RT_NOEXCEPT
{
    VBOXSTRICTRC rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    if (rcStrict == VINF_SUCCESS)
        pVCpu->cpum.GstCtx.rsp = uNewRsp;
    return rcStrict;
}


/**
 * Begin a special stack pop (used by iret, retf and such).
 *
 * This will raise \#SS or \#PF if appropriate.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbMem               The number of bytes to pop from the stack.
 * @param   cbAlign             The alignment mask (7, 3, 1).
 * @param   ppvMem              Where to return the pointer to the stack memory.
 * @param   pbUnmapInfo         Where to store unmap info for
 *                              iemMemStackPopDoneSpecial.
 * @param   puNewRsp            Where to return the new RSP value.  This must be
 *                              assigned to CPUMCTX::rsp manually some time
 *                              after iemMemStackPopDoneSpecial() has been
 *                              called.
 */
VBOXSTRICTRC iemMemStackPopBeginSpecial(PVMCPUCC pVCpu, size_t cbMem, uint32_t cbAlign,
                                        void const **ppvMem, uint8_t *pbUnmapInfo, uint64_t *puNewRsp) RT_NOEXCEPT
{
    Assert(cbMem < UINT8_MAX);
    RTGCPTR     GCPtrTop = iemRegGetRspForPop(pVCpu, (uint8_t)cbMem, puNewRsp);
    return iemMemMap(pVCpu, (void **)ppvMem, pbUnmapInfo, cbMem, X86_SREG_SS, GCPtrTop, IEM_ACCESS_STACK_R, cbAlign);
}


/**
 * Continue a special stack pop (used by iret and retf), for the purpose of
 * retrieving a new stack pointer.
 *
 * This will raise \#SS or \#PF if appropriate.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   off                 Offset from the top of the stack. This is zero
 *                              except in the retf case.
 * @param   cbMem               The number of bytes to pop from the stack.
 * @param   ppvMem              Where to return the pointer to the stack memory.
 * @param   pbUnmapInfo         Where to store unmap info for
 *                              iemMemStackPopDoneSpecial.
 * @param   uCurNewRsp          The current uncommitted RSP value.  (No need to
 *                              return this because all use of this function is
 *                              to retrieve a new value and anything we return
 *                              here would be discarded.)
 */
VBOXSTRICTRC iemMemStackPopContinueSpecial(PVMCPUCC pVCpu, size_t off, size_t cbMem,
                                           void const **ppvMem, uint8_t *pbUnmapInfo, uint64_t uCurNewRsp) RT_NOEXCEPT
{
    Assert(cbMem < UINT8_MAX);

    /* The essense of iemRegGetRspForPopEx and friends: */ /** @todo put this into a inlined function? */
    RTGCPTR GCPtrTop;
    if (IEM_IS_64BIT_CODE(pVCpu))
        GCPtrTop = uCurNewRsp;
    else if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
        GCPtrTop = (uint32_t)uCurNewRsp;
    else
        GCPtrTop = (uint16_t)uCurNewRsp;

    return iemMemMap(pVCpu, (void **)ppvMem, pbUnmapInfo, cbMem, X86_SREG_SS, GCPtrTop + off, IEM_ACCESS_STACK_R,
                     0 /* checked in iemMemStackPopBeginSpecial */);
}


/**
 * Done with a special stack pop (started by iemMemStackPopBeginSpecial or
 * iemMemStackPopContinueSpecial).
 *
 * The caller will manually commit the rSP.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   bUnmapInfo          Unmap information returned by
 *                              iemMemStackPopBeginSpecial() or
 *                              iemMemStackPopContinueSpecial().
 */
VBOXSTRICTRC iemMemStackPopDoneSpecial(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT
{
    return iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
}


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


/**
 * Fetches a descriptor table entry with caller specified error code.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pDesc               Where to return the descriptor table entry.
 * @param   uSel                The selector which table entry to fetch.
 * @param   uXcpt               The exception to raise on table lookup error.
 * @param   uErrorCode          The error code associated with the exception.
 */
VBOXSTRICTRC iemMemFetchSelDescWithErr(PVMCPUCC pVCpu, PIEMSELDESC pDesc, uint16_t uSel,
                                       uint8_t uXcpt, uint16_t uErrorCode)  RT_NOEXCEPT
{
    AssertPtr(pDesc);
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_GDTR | CPUMCTX_EXTRN_LDTR);

    /** @todo did the 286 require all 8 bytes to be accessible? */
    /*
     * Get the selector table base and check bounds.
     */
    RTGCPTR GCPtrBase;
    if (uSel & X86_SEL_LDT)
    {
        if (   !pVCpu->cpum.GstCtx.ldtr.Attr.n.u1Present
            || (uSel | X86_SEL_RPL_LDT) > pVCpu->cpum.GstCtx.ldtr.u32Limit )
        {
            LogEx(LOG_GROUP_IEM, ("iemMemFetchSelDesc: LDT selector %#x is out of bounds (%3x) or ldtr is NP (%#x)\n",
                   uSel, pVCpu->cpum.GstCtx.ldtr.u32Limit, pVCpu->cpum.GstCtx.ldtr.Sel));
            return iemRaiseXcptOrInt(pVCpu, 0, uXcpt, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR,
                                     uErrorCode, 0);
        }

        Assert(pVCpu->cpum.GstCtx.ldtr.Attr.n.u1Present);
        GCPtrBase = pVCpu->cpum.GstCtx.ldtr.u64Base;
    }
    else
    {
        if ((uSel | X86_SEL_RPL_LDT) > pVCpu->cpum.GstCtx.gdtr.cbGdt)
        {
            LogEx(LOG_GROUP_IEM, ("iemMemFetchSelDesc: GDT selector %#x is out of bounds (%3x)\n", uSel, pVCpu->cpum.GstCtx.gdtr.cbGdt));
            return iemRaiseXcptOrInt(pVCpu, 0, uXcpt, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR,
                                     uErrorCode, 0);
        }
        GCPtrBase = pVCpu->cpum.GstCtx.gdtr.pGdt;
    }

    /*
     * Read the legacy descriptor and maybe the long mode extensions if
     * required.
     */
    VBOXSTRICTRC rcStrict;
    if (IEM_GET_TARGET_CPU(pVCpu) > IEMTARGETCPU_286)
        rcStrict = iemMemFetchSysU64(pVCpu, &pDesc->Legacy.u, UINT8_MAX, GCPtrBase + (uSel & X86_SEL_MASK));
    else
    {
        rcStrict     = iemMemFetchSysU16(pVCpu, &pDesc->Legacy.au16[0], UINT8_MAX, GCPtrBase + (uSel & X86_SEL_MASK) + 0);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemFetchSysU16(pVCpu, &pDesc->Legacy.au16[1], UINT8_MAX, GCPtrBase + (uSel & X86_SEL_MASK) + 2);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemFetchSysU16(pVCpu, &pDesc->Legacy.au16[2], UINT8_MAX, GCPtrBase + (uSel & X86_SEL_MASK) + 4);
        if (rcStrict == VINF_SUCCESS)
            pDesc->Legacy.au16[3] = 0;
        else
            return rcStrict;
    }

    if (rcStrict == VINF_SUCCESS)
    {
        if (   !IEM_IS_LONG_MODE(pVCpu)
            || pDesc->Legacy.Gen.u1DescType)
            pDesc->Long.au64[1] = 0;
        else if (   (uint32_t)(uSel | X86_SEL_RPL_LDT) + 8
                 <= (uSel & X86_SEL_LDT ? pVCpu->cpum.GstCtx.ldtr.u32Limit : pVCpu->cpum.GstCtx.gdtr.cbGdt))
            rcStrict = iemMemFetchSysU64(pVCpu, &pDesc->Long.au64[1], UINT8_MAX, GCPtrBase + (uSel | X86_SEL_RPL_LDT) + 1);
        else
        {
            LogEx(LOG_GROUP_IEM,("iemMemFetchSelDesc: system selector %#x is out of bounds\n", uSel));
            /** @todo is this the right exception? */
            return iemRaiseXcptOrInt(pVCpu, 0, uXcpt, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, uErrorCode, 0);
        }
    }
    return rcStrict;
}


/**
 * Fetches a descriptor table entry.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pDesc               Where to return the descriptor table entry.
 * @param   uSel                The selector which table entry to fetch.
 * @param   uXcpt               The exception to raise on table lookup error.
 */
VBOXSTRICTRC iemMemFetchSelDesc(PVMCPUCC pVCpu, PIEMSELDESC pDesc, uint16_t uSel, uint8_t uXcpt) RT_NOEXCEPT
{
    return iemMemFetchSelDescWithErr(pVCpu, pDesc, uSel, uXcpt, uSel & X86_SEL_MASK_OFF_RPL);
}


/**
 * Marks the selector descriptor as accessed (only non-system descriptors).
 *
 * This function ASSUMES that iemMemFetchSelDesc has be called previously and
 * will therefore skip the limit checks.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   uSel                The selector.
 */
VBOXSTRICTRC iemMemMarkSelDescAccessed(PVMCPUCC pVCpu, uint16_t uSel) RT_NOEXCEPT
{
    /*
     * Get the selector table base and calculate the entry address.
     */
    RTGCPTR GCPtr = uSel & X86_SEL_LDT
                  ? pVCpu->cpum.GstCtx.ldtr.u64Base
                  : pVCpu->cpum.GstCtx.gdtr.pGdt;
    GCPtr += uSel & X86_SEL_MASK;

    /*
     * ASMAtomicBitSet will assert if the address is misaligned, so do some
     * ugly stuff to avoid this.  This will make sure it's an atomic access
     * as well more or less remove any question about 8-bit or 32-bit accesss.
     */
    VBOXSTRICTRC        rcStrict;
    uint8_t             bUnmapInfo;
    uint32_t volatile  *pu32;
    if ((GCPtr & 3) == 0)
    {
        /* The normal case, map the 32-bit bits around the accessed bit (40). */
        GCPtr += 2 + 2;
        rcStrict = iemMemMap(pVCpu, (void **)&pu32, &bUnmapInfo, 4, UINT8_MAX, GCPtr, IEM_ACCESS_SYS_RW, 0);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        ASMAtomicBitSet(pu32, 8); /* X86_SEL_TYPE_ACCESSED is 1, but it is preceded by u8BaseHigh1. */
    }
    else
    {
        /* The misaligned GDT/LDT case, map the whole thing. */
        rcStrict = iemMemMap(pVCpu, (void **)&pu32, &bUnmapInfo, 8, UINT8_MAX, GCPtr, IEM_ACCESS_SYS_RW, 0);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        switch ((uintptr_t)pu32 & 3)
        {
            case 0: ASMAtomicBitSet(pu32,                         40 + 0 -  0); break;
            case 1: ASMAtomicBitSet((uint8_t volatile *)pu32 + 3, 40 + 0 - 24); break;
            case 2: ASMAtomicBitSet((uint8_t volatile *)pu32 + 2, 40 + 0 - 16); break;
            case 3: ASMAtomicBitSet((uint8_t volatile *)pu32 + 1, 40 + 0 -  8); break;
        }
    }

    return iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
}

/** @} */

