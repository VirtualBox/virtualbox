/* $Id: IEMAllOpcodeFetch-x86.cpp 112403 2026-01-11 19:29:08Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - All Contexts.
 */

/*
 * Copyright (C) 2011-2026 Oracle and/or its affiliates.
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
#include <iprt/x86.h>

#include "IEMInline.h"
#include "IEMInline-x86.h"
#include "IEMAllTlbInline-x86.h"


#ifndef IEM_WITH_CODE_TLB_IN_CUR_CTX
/**
 * Prefetch opcodes the first time when starting executing.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure of the calling
 *                  thread.
 */
VBOXSTRICTRC iemOpcodeFetchPrefetch(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * What we're doing here is very similar to iemMemMap/iemMemBounceBufferMap.
     *
     * First translate CS:rIP to a physical address.
     *
     * Note! The iemOpcodeFetchMoreBytes code depends on this here code to fetch
     *       all relevant bytes from the first page, as it ASSUMES it's only ever
     *       called for dealing with CS.LIM, page crossing and instructions that
     *       are too long.
     */
    uint32_t    cbToTryRead;
    RTGCPTR     GCPtrPC;
    if (IEM_IS_64BIT_CODE(pVCpu))
    {
        cbToTryRead = GUEST_PAGE_SIZE;
        GCPtrPC     = pVCpu->cpum.GstCtx.rip;
        if (IEM_IS_CANONICAL(GCPtrPC))
            cbToTryRead = GUEST_PAGE_SIZE - (GCPtrPC & GUEST_PAGE_OFFSET_MASK);
        else
            return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    else
    {
        uint32_t GCPtrPC32 = pVCpu->cpum.GstCtx.eip;
        AssertMsg(!(GCPtrPC32 & ~(uint32_t)UINT16_MAX) || IEM_IS_32BIT_CODE(pVCpu), ("%04x:%RX64\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
        if (GCPtrPC32 <= pVCpu->cpum.GstCtx.cs.u32Limit)
            cbToTryRead = pVCpu->cpum.GstCtx.cs.u32Limit - GCPtrPC32 + 1;
        else
            return iemRaiseSelectorBounds(pVCpu, X86_SREG_CS, IEM_ACCESS_INSTRUCTION);
        if (cbToTryRead) { /* likely */ }
        else /* overflowed */
        {
            Assert(GCPtrPC32 == 0); Assert(pVCpu->cpum.GstCtx.cs.u32Limit == UINT32_MAX);
            cbToTryRead = UINT32_MAX;
        }
        GCPtrPC = (uint32_t)pVCpu->cpum.GstCtx.cs.u64Base + GCPtrPC32;
        Assert(GCPtrPC <= UINT32_MAX);
    }

    PGMPTWALKFAST WalkFast;
    int rc = PGMGstQueryPageFast(pVCpu, GCPtrPC,
                                 IEM_GET_CPL(pVCpu) == 3 ? PGMQPAGE_F_EXECUTE | PGMQPAGE_F_USER_MODE : PGMQPAGE_F_EXECUTE,
                                 &WalkFast);
    if (RT_SUCCESS(rc))
        Assert(WalkFast.fInfo & PGM_WALKINFO_SUCCEEDED);
    else
    {
        Log(("iemInitDecoderAndPrefetchOpcodes: %RGv - rc=%Rrc\n", GCPtrPC, rc));
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/** @todo This isn't quite right yet, as PGM_GST_SLAT_NAME_EPT(Walk) doesn't
 * know about what kind of access we're making! See PGM_GST_NAME(WalkFast). */
        if (WalkFast.fFailed & PGM_WALKFAIL_EPT)
            IEM_VMX_VMEXIT_EPT_RET(pVCpu, &WalkFast, IEM_ACCESS_INSTRUCTION, IEM_SLAT_FAIL_LINEAR_TO_PHYS_ADDR, 0 /* cbInstr */);
# endif
        return iemRaisePageFault(pVCpu, GCPtrPC, 1, IEM_ACCESS_INSTRUCTION, rc);
    }
#if 0
    if ((WalkFast.fEffective & X86_PTE_US) || IEM_GET_CPL(pVCpu) != 3) { /* likely */ }
    else
    {
        Log(("iemInitDecoderAndPrefetchOpcodes: %RGv - supervisor page\n", GCPtrPC));
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/** @todo this is completely wrong for EPT. WalkFast.fFailed is always zero here!*/
#  error completely wrong
        if (WalkFast.fFailed & PGM_WALKFAIL_EPT)
            IEM_VMX_VMEXIT_EPT_RET(pVCpu, &WalkFast, IEM_ACCESS_INSTRUCTION, IEM_SLAT_FAIL_LINEAR_TO_PAGE_TABLE, 0 /* cbInstr */);
# endif
        return iemRaisePageFault(pVCpu, GCPtrPC, 1, IEM_ACCESS_INSTRUCTION, VERR_ACCESS_DENIED);
    }
    if (!(WalkFast.fEffective & X86_PTE_PAE_NX) || !(pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_NXE)) { /* likely */ }
    else
    {
        Log(("iemInitDecoderAndPrefetchOpcodes: %RGv - NX\n", GCPtrPC));
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/** @todo this is completely wrong for EPT. WalkFast.fFailed is always zero here!*/
#  error completely wrong.
        if (WalkFast.fFailed & PGM_WALKFAIL_EPT)
            IEM_VMX_VMEXIT_EPT_RET(pVCpu, &WalkFast, IEM_ACCESS_INSTRUCTION, IEM_SLAT_FAIL_LINEAR_TO_PAGE_TABLE, 0 /* cbInstr */);
# endif
        return iemRaisePageFault(pVCpu, GCPtrPC, 1, IEM_ACCESS_INSTRUCTION, VERR_ACCESS_DENIED);
    }
#else
    Assert((WalkFast.fEffective & X86_PTE_US) || IEM_GET_CPL(pVCpu) != 3);
    Assert(!(WalkFast.fEffective & X86_PTE_PAE_NX) || !(pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_NXE));
#endif
    RTGCPHYS const GCPhys = WalkFast.GCPhys;

    /*
     * Read the bytes at this address.
     */
    uint32_t cbLeftOnPage = GUEST_PAGE_SIZE - (GCPtrPC & GUEST_PAGE_OFFSET_MASK);
    if (cbToTryRead > cbLeftOnPage)
        cbToTryRead = cbLeftOnPage;
    if (cbToTryRead > sizeof(ICORE(pVCpu).abOpcode))
        cbToTryRead = sizeof(ICORE(pVCpu).abOpcode);

    if (!(ICORE(pVCpu).fExec & IEM_F_BYPASS_HANDLERS))
    {
        VBOXSTRICTRC rcStrict = PGMPhysRead(pVCpu->CTX_SUFF(pVM), GCPhys, ICORE(pVCpu).abOpcode, cbToTryRead, PGMACCESSORIGIN_IEM);
        if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        { /* likely */ }
        else if (PGM_PHYS_RW_IS_SUCCESS(rcStrict))
        {
            Log(("iemInitDecoderAndPrefetchOpcodes: %RGv/%RGp LB %#x - read status -  rcStrict=%Rrc\n",
                 GCPtrPC, GCPhys, VBOXSTRICTRC_VAL(rcStrict), cbToTryRead));
            rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
        }
        else
        {
            Log((RT_SUCCESS(rcStrict)
                 ? "iemInitDecoderAndPrefetchOpcodes: %RGv/%RGp LB %#x - read status - rcStrict=%Rrc\n"
                 : "iemInitDecoderAndPrefetchOpcodes: %RGv/%RGp LB %#x - read error - rcStrict=%Rrc (!!)\n",
                 GCPtrPC, GCPhys, VBOXSTRICTRC_VAL(rcStrict), cbToTryRead));
            return rcStrict;
        }
    }
    else
    {
        rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), ICORE(pVCpu).abOpcode, GCPhys, cbToTryRead);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
        {
            Log(("iemInitDecoderAndPrefetchOpcodes: %RGv/%RGp LB %#x - read error - rc=%Rrc (!!)\n",
                 GCPtrPC, GCPhys, rc, cbToTryRead));
            return rc;
        }
    }
    ICORE(pVCpu).cbOpcode = cbToTryRead;
    return VINF_SUCCESS;
}
#endif /* !IEM_WITH_CODE_TLB_IN_CUR_CTX */


/**
 * Flushes the prefetch buffer, light version.
 */
void iemOpcodeFlushLight(PVMCPUCC pVCpu, uint8_t cbInstr)
{
#ifndef IEM_WITH_CODE_TLB_IN_CUR_CTX
    ICORE(pVCpu).cbOpcode = cbInstr;
#else
    RT_NOREF(pVCpu, cbInstr);
#endif
}


/**
 * Flushes the prefetch buffer, heavy version.
 */
void iemOpcodeFlushHeavy(PVMCPUCC pVCpu, uint8_t cbInstr)
{
#ifndef IEM_WITH_CODE_TLB_IN_CUR_CTX
    ICORE(pVCpu).cbOpcode = cbInstr; /* Note! SVM and VT-x may set this to zero on exit, rather than the instruction length. */
#elif 1
    ICORE(pVCpu).cbInstrBufTotal = 0;
    RT_NOREF(cbInstr);
#else
    RT_NOREF(pVCpu, cbInstr);
#endif
}



#ifdef IEM_WITH_CODE_TLB_IN_CUR_CTX

/**
 * Tries to fetches @a cbDst opcode bytes, raise the appropriate exception on
 * failure and jumps.
 *
 * We end up here for a number of reasons:
 *      - pbInstrBuf isn't yet initialized.
 *      - Advancing beyond the buffer boundrary (e.g. cross page).
 *      - Advancing beyond the CS segment limit.
 *      - Fetching from non-mappable page (e.g. MMIO).
 *      - TLB loading in the recompiler (@a pvDst = NULL, @a cbDst = 0).
 *
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   pvDst               Where to return the bytes.
 * @param   cbDst               Number of bytes to read.  A value of zero is
 *                              allowed for initializing pbInstrBuf (the
 *                              recompiler does this).  In this case it is best
 *                              to set pbInstrBuf to NULL prior to the call.
 */
void iemOpcodeFetchBytesJmp(PVMCPUCC pVCpu, size_t cbDst, void *pvDst) IEM_NOEXCEPT_MAY_LONGJMP
{
# ifdef IN_RING3
    for (;;)
    {
        Assert(cbDst <= 8);
        uint32_t offBuf = ICORE(pVCpu).offInstrNextByte;

        /*
         * We might have a partial buffer match, deal with that first to make the
         * rest simpler.  This is the first part of the cross page/buffer case.
         */
        uint8_t const * const pbInstrBuf = ICORE(pVCpu).pbInstrBuf;
        if (pbInstrBuf != NULL)
        {
            Assert(cbDst != 0); /* pbInstrBuf shall be NULL in case of a TLB load */
            uint32_t const cbInstrBuf = ICORE(pVCpu).cbInstrBuf;
            if (offBuf < cbInstrBuf)
            {
                Assert(offBuf + cbDst > cbInstrBuf);
                uint32_t const cbCopy = cbInstrBuf - offBuf;
                memcpy(pvDst, &pbInstrBuf[offBuf], cbCopy);

                cbDst  -= cbCopy;
                pvDst   = (uint8_t *)pvDst + cbCopy;
                offBuf += cbCopy;
            }
        }

        /*
         * Check segment limit, figuring how much we're allowed to access at this point.
         *
         * We will fault immediately if RIP is past the segment limit / in non-canonical
         * territory.  If we do continue, there are one or more bytes to read before we
         * end up in trouble and we need to do that first before faulting.
         */
        RTGCPTR  GCPtrFirst;
        uint32_t cbMaxRead;
        if (IEM_IS_64BIT_CODE(pVCpu))
        {
            GCPtrFirst = pVCpu->cpum.GstCtx.rip + (offBuf - (uint32_t)(int32_t)ICORE(pVCpu).offCurInstrStart);
            if (RT_LIKELY(IEM_IS_CANONICAL(GCPtrFirst)))
            { /* likely */ }
            else
                iemRaiseGeneralProtectionFault0Jmp(pVCpu);
            cbMaxRead = X86_PAGE_SIZE - ((uint32_t)GCPtrFirst & X86_PAGE_OFFSET_MASK);
        }
        else
        {
            GCPtrFirst = pVCpu->cpum.GstCtx.eip + (offBuf - (uint32_t)(int32_t)ICORE(pVCpu).offCurInstrStart);
            /* Assert(!(GCPtrFirst & ~(uint32_t)UINT16_MAX) || IEM_IS_32BIT_CODE(pVCpu)); - this is allowed */
            if (RT_LIKELY((uint32_t)GCPtrFirst <= pVCpu->cpum.GstCtx.cs.u32Limit))
            { /* likely */ }
            else /** @todo For CPUs older than the 386, we should not necessarily generate \#GP here but wrap around! */
                iemRaiseSelectorBoundsJmp(pVCpu, X86_SREG_CS, IEM_ACCESS_INSTRUCTION);
            cbMaxRead = pVCpu->cpum.GstCtx.cs.u32Limit - (uint32_t)GCPtrFirst + 1;
            if (cbMaxRead != 0)
            { /* likely */ }
            else
            {
                /* Overflowed because address is 0 and limit is max. */
                Assert(GCPtrFirst == 0); Assert(pVCpu->cpum.GstCtx.cs.u32Limit == UINT32_MAX);
                cbMaxRead = X86_PAGE_SIZE;
            }
            GCPtrFirst = (uint32_t)GCPtrFirst + (uint32_t)pVCpu->cpum.GstCtx.cs.u64Base;
            uint32_t cbMaxRead2 = X86_PAGE_SIZE - ((uint32_t)GCPtrFirst & X86_PAGE_OFFSET_MASK);
            if (cbMaxRead2 < cbMaxRead)
                cbMaxRead = cbMaxRead2;
            /** @todo testcase: unreal modes, both huge 16-bit and 32-bit. */
        }

        /*
         * Get the TLB entry for this piece of code.
         */
        uint64_t const uTagNoRev = IEMTLB_CALC_TAG_NO_REV(pVCpu, GCPtrFirst);
        PIEMTLBENTRY   pTlbe     = IEMTLB_TAG_TO_EVEN_ENTRY(&ITLBS(pVCpu).Code, uTagNoRev);
        if (   pTlbe->uTag               == (uTagNoRev | ITLBS(pVCpu).Code.uTlbRevision)
            || (pTlbe = pTlbe + 1)->uTag == (uTagNoRev | ITLBS(pVCpu).Code.uTlbRevisionGlobal))
        {
            /* likely when executing lots of code, otherwise unlikely */
#  ifdef IEM_WITH_TLB_STATISTICS
            ITLBS(pVCpu).Code.cTlbCoreHits++;
#  endif
            Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_ACCESSED));

            /* Check TLB page table level access flags. */
            if (pTlbe->fFlagsAndPhysRev & (IEMTLBE_F_PT_NO_USER | IEMTLBE_F_PT_NO_EXEC))
            {
                if ((pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_USER) && IEM_GET_CPL(pVCpu) == 3)
                {
                    Log(("iemOpcodeFetchBytesJmp: %RGv - supervisor page\n", GCPtrFirst));
                    iemRaisePageFaultJmp(pVCpu, GCPtrFirst, 1, IEM_ACCESS_INSTRUCTION, VERR_ACCESS_DENIED);
                }
                if ((pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_EXEC) && (pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_NXE))
                {
                    Log(("iemOpcodeFetchMoreBytes: %RGv - NX\n", GCPtrFirst));
                    iemRaisePageFaultJmp(pVCpu, GCPtrFirst, 1, IEM_ACCESS_INSTRUCTION, VERR_ACCESS_DENIED);
                }
            }

            /* Look up the physical page info if necessary. */
            if ((pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PHYS_REV) == ITLBS(pVCpu).Code.uTlbPhysRev)
            { /* not necessary */ }
            else
            {
                if (RT_LIKELY(ITLBS(pVCpu).Code.uTlbPhysRev > IEMTLB_PHYS_REV_INCR))
                { /* likely */ }
                else
                    iemTlbInvalidateAllPhysicalSlow(pVCpu);
                pTlbe->fFlagsAndPhysRev &= ~IEMTLBE_GCPHYS2PTR_MASK;
                int rc = PGMPhysIemGCPhys2PtrNoLock(pVCpu->CTX_SUFF(pVM), pVCpu, pTlbe->GCPhys, &ITLBS(pVCpu).Code.uTlbPhysRev,
                                                    &pTlbe->pbMappingR3, &pTlbe->fFlagsAndPhysRev);
                AssertRCStmt(rc, IEM_DO_LONGJMP(pVCpu, rc));
            }
        }
        else
        {
            ITLBS(pVCpu).Code.cTlbCoreMisses++;

            /* This page table walking will set A bits as required by the access while performing the walk.
               ASSUMES these are set when the address is translated rather than on commit... */
            /** @todo testcase: check when A bits are actually set by the CPU for code.  */
            PGMPTWALKFAST WalkFast;
            int rc = PGMGstQueryPageFast(pVCpu, GCPtrFirst,
                                         IEM_GET_CPL(pVCpu) == 3 ? PGMQPAGE_F_EXECUTE | PGMQPAGE_F_USER_MODE : PGMQPAGE_F_EXECUTE,
                                         &WalkFast);
            if (RT_SUCCESS(rc))
                Assert((WalkFast.fInfo & PGM_WALKINFO_SUCCEEDED) && WalkFast.fFailed == PGM_WALKFAIL_SUCCESS);
            else
            {
#  ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
                /** @todo Nested VMX: Need to handle EPT violation/misconfig here?  OF COURSE! */
                Assert(!(Walk.fFailed & PGM_WALKFAIL_EPT));
#  endif
                Log(("iemOpcodeFetchMoreBytes: %RGv - rc=%Rrc\n", GCPtrFirst, rc));
                iemRaisePageFaultJmp(pVCpu, GCPtrFirst, 1, IEM_ACCESS_INSTRUCTION, rc);
            }

            AssertCompile(IEMTLBE_F_PT_NO_EXEC == 1);
            if (   !(WalkFast.fEffective & PGM_PTATTRS_G_MASK)
                || IEM_GET_CPL(pVCpu) != 0) /* optimization: Only use the PTE.G=1 entries in ring-0. */
            {
                pTlbe--;
                pTlbe->uTag         = uTagNoRev | ITLBS(pVCpu).Code.uTlbRevision;
                if (WalkFast.fInfo & PGM_WALKINFO_BIG_PAGE)
                    iemTlbLoadedLargePage<false>(pVCpu, &ITLBS(pVCpu).Code, uTagNoRev, RT_BOOL(pVCpu->cpum.GstCtx.cr4 & X86_CR4_PAE));
#  ifdef IEMTLB_WITH_LARGE_PAGE_BITMAP
                else
                    ASMBitClear(ITLBS(pVCpu).Code.bmLargePage, IEMTLB_TAG_TO_EVEN_INDEX(uTagNoRev));
#  endif
            }
            else
            {
                ITLBS(pVCpu).Code.cTlbCoreGlobalLoads++;
                pTlbe->uTag         = uTagNoRev | ITLBS(pVCpu).Code.uTlbRevisionGlobal;
                if (WalkFast.fInfo & PGM_WALKINFO_BIG_PAGE)
                    iemTlbLoadedLargePage<true>(pVCpu, &ITLBS(pVCpu).Code, uTagNoRev, RT_BOOL(pVCpu->cpum.GstCtx.cr4 & X86_CR4_PAE));
#  ifdef IEMTLB_WITH_LARGE_PAGE_BITMAP
                else
                    ASMBitClear(ITLBS(pVCpu).Code.bmLargePage, IEMTLB_TAG_TO_EVEN_INDEX(uTagNoRev) + 1);
#  endif
            }
            pTlbe->fFlagsAndPhysRev = (~WalkFast.fEffective & (X86_PTE_US | X86_PTE_RW | X86_PTE_D | X86_PTE_A))
                                    | (WalkFast.fEffective >> X86_PTE_PAE_BIT_NX) /*IEMTLBE_F_PT_NO_EXEC*/
                                    | (WalkFast.fInfo & PGM_WALKINFO_BIG_PAGE);
            RTGCPHYS const GCPhysPg = WalkFast.GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
            pTlbe->GCPhys           = GCPhysPg;
            pTlbe->pbMappingR3      = NULL;
            Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_EXEC) || !(pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_NXE));
            Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_USER) || IEM_GET_CPL(pVCpu) != 3);
            Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_ACCESSED));

            if (!((uintptr_t)pTlbe & (sizeof(*pTlbe) * 2 - 1)))
                IEMTLBTRACE_LOAD(       pVCpu, GCPtrFirst, pTlbe->GCPhys, (uint32_t)pTlbe->fFlagsAndPhysRev, false);
            else
                IEMTLBTRACE_LOAD_GLOBAL(pVCpu, GCPtrFirst, pTlbe->GCPhys, (uint32_t)pTlbe->fFlagsAndPhysRev, false);

            /* Resolve the physical address. */
            if (RT_LIKELY(ITLBS(pVCpu).Code.uTlbPhysRev > IEMTLB_PHYS_REV_INCR))
            { /* likely */ }
            else
                iemTlbInvalidateAllPhysicalSlow(pVCpu);
            Assert(!(pTlbe->fFlagsAndPhysRev & IEMTLBE_GCPHYS2PTR_MASK));
            rc = PGMPhysIemGCPhys2PtrNoLock(pVCpu->CTX_SUFF(pVM), pVCpu, GCPhysPg, &ITLBS(pVCpu).Code.uTlbPhysRev,
                                            &pTlbe->pbMappingR3, &pTlbe->fFlagsAndPhysRev);
            AssertRCStmt(rc, IEM_DO_LONGJMP(pVCpu, rc));
        }

# if defined(IN_RING3) || defined(IN_RING0) /** @todo fixme */
        /*
         * Try do a direct read using the pbMappingR3 pointer.
         * Note! Do not recheck the physical TLB revision number here as we have the
         *       wrong response to changes in the else case.  If someone is updating
         *       ITLBS(pVCpu).Code.uTlbPhysRev in parallel to us, we should be fine
         *       pretending we always won the race.
         */
        if (    (pTlbe->fFlagsAndPhysRev & (/*IEMTLBE_F_PHYS_REV |*/ IEMTLBE_F_NO_MAPPINGR3 | IEMTLBE_F_PG_NO_READ))
             == /*ITLBS(pVCpu).Code.uTlbPhysRev*/ 0U)
        {
            uint32_t const offPg = (GCPtrFirst & X86_PAGE_OFFSET_MASK);
            ICORE(pVCpu).cbInstrBufTotal = offPg + cbMaxRead;
            if (offBuf == (uint32_t)(int32_t)ICORE(pVCpu).offCurInstrStart)
            {
                ICORE(pVCpu).cbInstrBuf       = offPg + RT_MIN(15, cbMaxRead);
                ICORE(pVCpu).offCurInstrStart = (int16_t)offPg;
            }
            else
            {
                uint32_t const cbInstr = offBuf - (uint32_t)(int32_t)ICORE(pVCpu).offCurInstrStart;
                if (cbInstr + (uint32_t)cbDst <= 15)
                {
                    ICORE(pVCpu).cbInstrBuf       = offPg + RT_MIN(cbMaxRead + cbInstr, 15) - cbInstr;
                    ICORE(pVCpu).offCurInstrStart = (int16_t)(offPg - cbInstr);
                }
                else
                {
                    Log(("iemOpcodeFetchMoreBytes: %04x:%08RX64 LB %#x + %#zx -> #GP(0)\n",
                         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, cbInstr, cbDst));
                    iemRaiseGeneralProtectionFault0Jmp(pVCpu);
                }
            }
            if (cbDst <= cbMaxRead)
            {
                IRECM(pVCpu).fTbCrossedPage     |= offPg == 0 || IRECM(pVCpu).fTbBranched != 0; /** @todo Spurious load effect on branch handling? */
#  if 0 /* unused */
                ICORE(pVCpu).GCPhysInstrBufPrev  = ICORE(pVCpu).GCPhysInstrBuf;
#  endif
                ICORE(pVCpu).offInstrNextByte = offPg + (uint32_t)cbDst;
                ICORE(pVCpu).uInstrBufPc      = GCPtrFirst & ~(RTGCPTR)X86_PAGE_OFFSET_MASK;
                ICORE(pVCpu).GCPhysInstrBuf   = pTlbe->GCPhys;
                ICORE(pVCpu).pbInstrBuf       = pTlbe->pbMappingR3;
                if (cbDst > 0) /* To make ASAN happy in the TLB load case. */
                    memcpy(pvDst, &pTlbe->pbMappingR3[offPg], cbDst);
                else
                    Assert(!pvDst);
                return;
            }
            ICORE(pVCpu).pbInstrBuf = NULL;

            memcpy(pvDst, &pTlbe->pbMappingR3[offPg], cbMaxRead);
            ICORE(pVCpu).offInstrNextByte = offPg + cbMaxRead;
        }
# else
#  error "refactor as needed"
        /*
         * If there is no special read handling, so we can read a bit more and
         * put it in the prefetch buffer.
         */
        if (   cbDst < cbMaxRead
            && (pTlbe->fFlagsAndPhysRev & (IEMTLBE_F_PHYS_REV | IEMTLBE_F_PG_NO_READ)) == ITLBS(pVCpu).Code.uTlbPhysRev)
        {
            VBOXSTRICTRC rcStrict = PGMPhysRead(pVCpu->CTX_SUFF(pVM), pTlbe->GCPhys,
                                                &ICORE(pVCpu).abOpcode[0], cbToTryRead, PGMACCESSORIGIN_IEM);
            if (RT_LIKELY(rcStrict == VINF_SUCCESS))
            { /* likely */ }
            else if (PGM_PHYS_RW_IS_SUCCESS(rcStrict))
            {
                Log(("iemOpcodeFetchMoreBytes: %RGv/%RGp LB %#x - read status -  rcStrict=%Rrc\n",
                     GCPtrNext, GCPhys, VBOXSTRICTRC_VAL(rcStrict), cbToTryRead));
                rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                AssertStmt(rcStrict == VINF_SUCCESS, IEM_DO_LONGJMP(pVCpu, VBOXSTRICRC_VAL(rcStrict)));
            }
            else
            {
                Log((RT_SUCCESS(rcStrict)
                     ? "iemOpcodeFetchMoreBytes: %RGv/%RGp LB %#x - read status - rcStrict=%Rrc\n"
                     : "iemOpcodeFetchMoreBytes: %RGv/%RGp LB %#x - read error - rcStrict=%Rrc (!!)\n",
                     GCPtrNext, GCPhys, VBOXSTRICTRC_VAL(rcStrict), cbToTryRead));
                IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
            }
        }
# endif
        /*
         * Special read handling, so only read exactly what's needed.
         * This is a highly unlikely scenario.
         */
        else
        {
            ITLBS(pVCpu).Code.cTlbSlowCodeReadPath++;

            /* Check instruction length. */
            uint32_t const cbInstr = offBuf - (uint32_t)(int32_t)ICORE(pVCpu).offCurInstrStart;
            if (RT_LIKELY(cbInstr + cbDst <= 15))
            { /* likely */ }
            else
            {
                Log(("iemOpcodeFetchMoreBytes: %04x:%08RX64 LB %#x + %#zx -> #GP(0) [slow]\n",
                     pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, cbInstr, cbDst));
                iemRaiseGeneralProtectionFault0Jmp(pVCpu);
            }

            /* Do the reading. */
            uint32_t const cbToRead = RT_MIN((uint32_t)cbDst, cbMaxRead);
            if (cbToRead > 0)
            {
                VBOXSTRICTRC rcStrict = PGMPhysRead(pVCpu->CTX_SUFF(pVM), pTlbe->GCPhys + (GCPtrFirst & X86_PAGE_OFFSET_MASK),
                                                    pvDst, cbToRead, PGMACCESSORIGIN_IEM);
                if (RT_LIKELY(rcStrict == VINF_SUCCESS))
                { /* likely */ }
                else if (PGM_PHYS_RW_IS_SUCCESS(rcStrict))
                {
                    Log(("iemOpcodeFetchMoreBytes: %RGv/%RGp LB %#x - read status -  rcStrict=%Rrc\n",
                         GCPtrFirst, pTlbe->GCPhys + (GCPtrFirst & X86_PAGE_OFFSET_MASK), VBOXSTRICTRC_VAL(rcStrict), cbToRead));
                    rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                    AssertStmt(rcStrict == VINF_SUCCESS, IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict)));
                }
                else
                {
                    Log((RT_SUCCESS(rcStrict)
                         ? "iemOpcodeFetchMoreBytes: %RGv/%RGp LB %#x - read status - rcStrict=%Rrc\n"
                         : "iemOpcodeFetchMoreBytes: %RGv/%RGp LB %#x - read error - rcStrict=%Rrc (!!)\n",
                         GCPtrFirst, pTlbe->GCPhys + (GCPtrFirst & X86_PAGE_OFFSET_MASK), VBOXSTRICTRC_VAL(rcStrict), cbToRead));
                    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
                }
            }

            /* Update the state and probably return. */
            uint32_t const offPg = (GCPtrFirst & X86_PAGE_OFFSET_MASK);
            IRECM(pVCpu).fTbCrossedPage     |= offPg == 0 || IRECM(pVCpu).fTbBranched != 0;
#  if 0 /* unused */
            ICORE(pVCpu).GCPhysInstrBufPrev  = ICORE(pVCpu).GCPhysInstrBuf;
#  endif
            ICORE(pVCpu).offCurInstrStart = (int16_t)(offPg - cbInstr);
            ICORE(pVCpu).offInstrNextByte = offPg + cbInstr + cbToRead;
            ICORE(pVCpu).cbInstrBuf       = offPg + RT_MIN(15, cbMaxRead + cbInstr) - cbToRead - cbInstr;
            ICORE(pVCpu).cbInstrBufTotal  = X86_PAGE_SIZE; /** @todo ??? */
            ICORE(pVCpu).GCPhysInstrBuf   = pTlbe->GCPhys;
            ICORE(pVCpu).uInstrBufPc      = GCPtrFirst & ~(RTGCPTR)X86_PAGE_OFFSET_MASK;
            ICORE(pVCpu).pbInstrBuf       = NULL;
            if (cbToRead == cbDst)
                return;
            Assert(cbToRead == cbMaxRead);
        }

        /*
         * More to read, loop.
         */
        cbDst -= cbMaxRead;
        pvDst  = (uint8_t *)pvDst + cbMaxRead;
    }
# else  /* !IN_RING3 */
    RT_NOREF(pvDst, cbDst);
    if (pvDst || cbDst)
        IEM_DO_LONGJMP(pVCpu, VERR_INTERNAL_ERROR);
# endif /* !IN_RING3 */
}

#else /* !IEM_WITH_CODE_TLB_IN_CUR_CTX */

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
    /*
     * What we're doing here is very similar to iemMemMap/iemMemBounceBufferMap.
     *
     * First translate CS:rIP to a physical address.
     */
    uint8_t const   cbOpcode  = ICORE(pVCpu).cbOpcode;
    uint8_t const   offOpcode = ICORE(pVCpu).offOpcode;
    uint8_t const   cbLeft    = cbOpcode - offOpcode;
    Assert(cbLeft < cbMin);
    Assert(cbOpcode <= sizeof(ICORE(pVCpu).abOpcode));

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
    if (cbToTryRead > sizeof(ICORE(pVCpu).abOpcode) - cbOpcode)
        cbToTryRead = sizeof(ICORE(pVCpu).abOpcode) - cbOpcode;
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
    if (!(ICORE(pVCpu).fExec & IEM_F_BYPASS_HANDLERS))
    {
        VBOXSTRICTRC rcStrict = PGMPhysRead(pVCpu->CTX_SUFF(pVM), GCPhys, &ICORE(pVCpu).abOpcode[cbOpcode],
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
        rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &ICORE(pVCpu).abOpcode[cbOpcode], GCPhys, cbToTryRead);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
        {
            Log(("iemOpcodeFetchMoreBytes: %RGv - read error - rc=%Rrc (!!)\n", GCPtrNext, rc));
            return rc;
        }
    }
    ICORE(pVCpu).cbOpcode = cbOpcode + cbToTryRead;
    Log5(("%.*Rhxs\n", ICORE(pVCpu).cbOpcode, ICORE(pVCpu).abOpcode));

    return VINF_SUCCESS;
}

#endif /* !IEM_WITH_CODE_TLB_IN_CUR_CTX */

/**
 * Deals with the problematic cases that iemOpcodeGetNextU8Jmp doesn't like, longjmp on error.
 *
 * @returns The opcode byte.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
uint8_t iemOpcodeGetNextU8SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
#ifdef IEM_WITH_CODE_TLB_IN_CUR_CTX
    uint8_t u8;
    iemOpcodeFetchBytesJmp(pVCpu, sizeof(u8), &u8);
    return u8;
#else
    VBOXSTRICTRC rcStrict = iemOpcodeFetchMoreBytes(pVCpu, 1);
    if (rcStrict == VINF_SUCCESS)
        return ICORE(pVCpu).abOpcode[ICORE(pVCpu).offOpcode++];
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
#endif
}


/**
 * Deals with the problematic cases that iemOpcodeGetNextU16Jmp doesn't like, longjmp on error
 *
 * @returns The opcode word.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
uint16_t iemOpcodeGetNextU16SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
#ifdef IEM_WITH_CODE_TLB_IN_CUR_CTX
    uint16_t u16;
    iemOpcodeFetchBytesJmp(pVCpu, sizeof(u16), &u16);
    return u16;
#else
    VBOXSTRICTRC rcStrict = iemOpcodeFetchMoreBytes(pVCpu, 2);
    if (rcStrict == VINF_SUCCESS)
    {
        uint8_t offOpcode = ICORE(pVCpu).offOpcode;
        ICORE(pVCpu).offOpcode += 2;
# ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        return *(uint16_t const *)&ICORE(pVCpu).abOpcode[offOpcode];
# else
        return RT_MAKE_U16(ICORE(pVCpu).abOpcode[offOpcode], ICORE(pVCpu).abOpcode[offOpcode + 1]);
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
uint32_t iemOpcodeGetNextU32SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
#ifdef IEM_WITH_CODE_TLB_IN_CUR_CTX
    uint32_t u32;
    iemOpcodeFetchBytesJmp(pVCpu, sizeof(u32), &u32);
    return u32;
#else
    VBOXSTRICTRC rcStrict = iemOpcodeFetchMoreBytes(pVCpu, 4);
    if (rcStrict == VINF_SUCCESS)
    {
        uint8_t offOpcode = ICORE(pVCpu).offOpcode;
        ICORE(pVCpu).offOpcode = offOpcode + 4;
# ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        return *(uint32_t const *)&ICORE(pVCpu).abOpcode[offOpcode];
# else
        return RT_MAKE_U32_FROM_U8(ICORE(pVCpu).abOpcode[offOpcode],
                                   ICORE(pVCpu).abOpcode[offOpcode + 1],
                                   ICORE(pVCpu).abOpcode[offOpcode + 2],
                                   ICORE(pVCpu).abOpcode[offOpcode + 3]);
# endif
    }
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
#endif
}


/**
 * Deals with the problematic cases that iemOpcodeGetNextU64Jmp doesn't like, longjmp on error.
 *
 * @returns The opcode qword.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
uint64_t iemOpcodeGetNextU64SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
#ifdef IEM_WITH_CODE_TLB_IN_CUR_CTX
    uint64_t u64;
    iemOpcodeFetchBytesJmp(pVCpu, sizeof(u64), &u64);
    return u64;
#else
    VBOXSTRICTRC rcStrict = iemOpcodeFetchMoreBytes(pVCpu, 8);
    if (rcStrict == VINF_SUCCESS)
    {
        uint8_t offOpcode = ICORE(pVCpu).offOpcode;
        ICORE(pVCpu).offOpcode = offOpcode + 8;
# ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        return *(uint64_t const *)&ICORE(pVCpu).abOpcode[offOpcode];
# else
        return RT_MAKE_U64_FROM_U8(ICORE(pVCpu).abOpcode[offOpcode],
                                   ICORE(pVCpu).abOpcode[offOpcode + 1],
                                   ICORE(pVCpu).abOpcode[offOpcode + 2],
                                   ICORE(pVCpu).abOpcode[offOpcode + 3],
                                   ICORE(pVCpu).abOpcode[offOpcode + 4],
                                   ICORE(pVCpu).abOpcode[offOpcode + 5],
                                   ICORE(pVCpu).abOpcode[offOpcode + 6],
                                   ICORE(pVCpu).abOpcode[offOpcode + 7]);
# endif
    }
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
#endif
}

