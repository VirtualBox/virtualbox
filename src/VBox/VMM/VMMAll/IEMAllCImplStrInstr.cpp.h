/* $Id: IEMAllCImplStrInstr.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - String Instruction Implementation Code Template.
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


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#if OP_SIZE == 8
# define OP_rAX     al
#elif OP_SIZE == 16
# define OP_rAX     ax
#elif OP_SIZE == 32
# define OP_rAX     eax
#elif OP_SIZE == 64
# define OP_rAX     rax
#else
# error "Bad OP_SIZE."
#endif
#define OP_TYPE                     RT_CONCAT3(uint,OP_SIZE,_t)

#if ADDR_SIZE == 16
# define ADDR_rDI       di
# define ADDR_rSI       si
# define ADDR_rCX       cx
# define ADDR2_TYPE     uint32_t
# define ADDR_VMXSTRIO  0
#elif ADDR_SIZE == 32
# define ADDR_rDI       edi
# define ADDR_rSI       esi
# define ADDR_rCX       ecx
# define ADDR2_TYPE     uint32_t
# define ADDR_VMXSTRIO  1
#elif ADDR_SIZE == 64
# define ADDR_rDI       rdi
# define ADDR_rSI       rsi
# define ADDR_rCX       rcx
# define ADDR2_TYPE     uint64_t
# define ADDR_VMXSTRIO  2
# define IS_64_BIT_CODE(a_pVCpu)    (true)
#else
# error "Bad ADDR_SIZE."
#endif
#define ADDR_TYPE                   RT_CONCAT3(uint,ADDR_SIZE,_t)

#if ADDR_SIZE == 64 || OP_SIZE == 64
# define IS_64_BIT_CODE(a_pVCpu)    (true)
#elif ADDR_SIZE == 32
# define IS_64_BIT_CODE(a_pVCpu)    IEM_IS_64BIT_CODE(a_pVCpu)
#else
# define IS_64_BIT_CODE(a_pVCpu)    (false)
#endif

/** @def IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN
 * Used in the outer (page-by-page) loop to check for reasons for returnning
 * before completing the instruction.   In raw-mode we temporarily enable
 * interrupts to let the host interrupt us.  We cannot let big string operations
 * hog the CPU, especially not in raw-mode.
 */
#define IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(a_pVM, a_pVCpu, a_fEflags) \
    do { \
        if (RT_LIKELY(   !VMCPU_FF_IS_ANY_SET(a_pVCpu, (a_fEflags) & X86_EFL_IF ? VMCPU_FF_YIELD_REPSTR_MASK \
                                                                                : VMCPU_FF_YIELD_REPSTR_NOINT_MASK) \
                      && !VM_FF_IS_ANY_SET(a_pVM, VM_FF_YIELD_REPSTR_MASK) \
                      )) \
        { /* probable */ } \
        else  \
        { \
            LogFlow(("%s: Leaving early (outer)! ffcpu=%#RX64 ffvm=%#x\n", \
                     __FUNCTION__, (uint64_t)(a_pVCpu)->fLocalForcedActions, (a_pVM)->fGlobalForcedActions)); \
            return VINF_IEM_YIELD_PENDING_FF; \
        } \
    } while (0)

/** @def IEM_CHECK_FF_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN
 * This is used in some of the inner loops to make sure we respond immediately
 * to VMCPU_FF_IOM as well as outside requests.  Use this for expensive
 * instructions. Use IEM_CHECK_FF_CPU_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN for
 * ones that are typically cheap. */
#define IEM_CHECK_FF_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN(a_pVM, a_pVCpu, a_fExitExpr) \
    do { \
        if (RT_LIKELY(   (   !VMCPU_FF_IS_ANY_SET(a_pVCpu, VMCPU_FF_HIGH_PRIORITY_POST_REPSTR_MASK) \
                          && !VM_FF_IS_ANY_SET(a_pVM,         VM_FF_HIGH_PRIORITY_POST_REPSTR_MASK)) \
                      || (a_fExitExpr) )) \
        { /* very likely */ } \
        else \
        { \
            LogFlow(("%s: Leaving early (inner)! ffcpu=%#RX64 ffvm=%#x\n", \
                     __FUNCTION__, (uint64_t)(a_pVCpu)->fLocalForcedActions, (a_pVM)->fGlobalForcedActions)); \
            return VINF_IEM_YIELD_PENDING_FF; \
        } \
    } while (0)


/** @def IEM_CHECK_FF_CPU_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN
 * This is used in the inner loops where
 * IEM_CHECK_FF_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN isn't used.  It only
 * checks the CPU FFs so that we respond immediately to the pending IOM FF
 * (status code is hidden in IEMCPU::rcPassUp by IEM memory commit code).
 */
#define IEM_CHECK_FF_CPU_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN(a_pVM, a_pVCpu, a_fExitExpr) \
    do { \
        if (RT_LIKELY(   !VMCPU_FF_IS_ANY_SET(a_pVCpu, VMCPU_FF_HIGH_PRIORITY_POST_REPSTR_MASK) \
                      || (a_fExitExpr) )) \
        { /* very likely */ } \
        else \
        { \
            LogFlow(("%s: Leaving early (inner)! ffcpu=%#RX64 (ffvm=%#x)\n", \
                     __FUNCTION__, (uint64_t)(a_pVCpu)->fLocalForcedActions, (a_pVM)->fGlobalForcedActions)); \
            return VINF_IEM_YIELD_PENDING_FF; \
        } \
    } while (0)


/**
 * Implements 'REPE CMPS'.
 */
IEM_CIMPL_DEF_1(RT_CONCAT4(iemCImpl_repe_cmps_op,OP_SIZE,_addr,ADDR_SIZE), uint8_t, iEffSeg)
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);

    /*
     * Setup.
     */
    ADDR_TYPE       uCounterReg  = pVCpu->cpum.GstCtx.ADDR_rCX;
    if (uCounterReg == 0)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iEffSeg) | CPUMCTX_EXTRN_ES);

    PCCPUMSELREGHID pSrc1Hid     = iemSRegGetHid(pVCpu, iEffSeg);
    uint64_t        uSrc1Base    = 0; /* gcc may not be used uninitialized */
    VBOXSTRICTRC    rcStrict     = iemMemSegCheckReadAccessEx(pVCpu, pSrc1Hid, iEffSeg, &uSrc1Base);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    uint64_t        uSrc2Base    = 0; /* gcc may not be used uninitialized */
    rcStrict = iemMemSegCheckReadAccessEx(pVCpu, iemSRegUpdateHid(pVCpu, &pVCpu->cpum.GstCtx.es), X86_SREG_ES, &uSrc2Base);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    int8_t const    cbIncr       = pVCpu->cpum.GstCtx.eflags.Bits.u1DF ? -(OP_SIZE / 8) : (OP_SIZE / 8);
    ADDR_TYPE       uSrc1AddrReg = pVCpu->cpum.GstCtx.ADDR_rSI;
    ADDR_TYPE       uSrc2AddrReg = pVCpu->cpum.GstCtx.ADDR_rDI;
    uint32_t        uEFlags      = pVCpu->cpum.GstCtx.eflags.u;

    /*
     * The loop.
     */
    for (;;)
    {
        /*
         * Do segmentation and virtual page stuff.
         */
        ADDR2_TYPE  uVirtSrc1Addr = uSrc1AddrReg + (ADDR2_TYPE)uSrc1Base;
        ADDR2_TYPE  uVirtSrc2Addr = uSrc2AddrReg + (ADDR2_TYPE)uSrc2Base;
        uint32_t    cLeftSrc1Page = (GUEST_PAGE_SIZE - (uVirtSrc1Addr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        if (cLeftSrc1Page > uCounterReg)
            cLeftSrc1Page = uCounterReg;
        uint32_t    cLeftSrc2Page = (GUEST_PAGE_SIZE - (uVirtSrc2Addr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        uint32_t    cLeftPage     = RT_MIN(cLeftSrc1Page, cLeftSrc2Page);

        if (   cLeftPage > 0 /* can be null if unaligned, do one fallback round. */
            && cbIncr > 0    /** @todo Optimize reverse direction string ops. */
            && (   IS_64_BIT_CODE(pVCpu)
                || (   uSrc1AddrReg < pSrc1Hid->u32Limit
                    && uSrc1AddrReg + (cLeftPage * (OP_SIZE / 8)) <= pSrc1Hid->u32Limit
                    && uSrc2AddrReg < pVCpu->cpum.GstCtx.es.u32Limit
                    && uSrc2AddrReg + (cLeftPage * (OP_SIZE / 8)) <= pVCpu->cpum.GstCtx.es.u32Limit)
               )
           )
        {
            RTGCPHYS GCPhysSrc1Mem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtSrc1Addr, OP_SIZE / 8, IEM_ACCESS_DATA_R, &GCPhysSrc1Mem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            RTGCPHYS GCPhysSrc2Mem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtSrc2Addr, OP_SIZE / 8, IEM_ACCESS_DATA_R, &GCPhysSrc2Mem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /*
             * If we can map the page without trouble, do a block processing
             * until the end of the current page.
             */
            PGMPAGEMAPLOCK PgLockSrc2Mem;
            OP_TYPE const *puSrc2Mem;
            rcStrict = iemMemPageMap(pVCpu, GCPhysSrc2Mem, IEM_ACCESS_DATA_R, (void **)&puSrc2Mem, &PgLockSrc2Mem);
            if (rcStrict == VINF_SUCCESS)
            {
                PGMPAGEMAPLOCK PgLockSrc1Mem;
                OP_TYPE const *puSrc1Mem;
                rcStrict = iemMemPageMap(pVCpu, GCPhysSrc1Mem, IEM_ACCESS_DATA_R, (void **)&puSrc1Mem, &PgLockSrc1Mem);
                if (rcStrict == VINF_SUCCESS)
                {
                    if (!memcmp(puSrc2Mem, puSrc1Mem, cLeftPage * (OP_SIZE / 8)))
                    {
                        /* All matches, only compare the last itme to get the right eflags. */
                        uEFlags = RT_CONCAT(iemAImpl_cmp_u,OP_SIZE)(uEFlags, (OP_TYPE *)&puSrc1Mem[cLeftPage-1], puSrc2Mem[cLeftPage-1]);
                        uSrc1AddrReg += cLeftPage * cbIncr;
                        uSrc2AddrReg += cLeftPage * cbIncr;
                        uCounterReg  -= cLeftPage;
                    }
                    else
                    {
                        /* Some mismatch, compare each item (and keep volatile
                           memory in mind). */
                        uint32_t off = 0;
                        do
                        {
                            uEFlags = RT_CONCAT(iemAImpl_cmp_u,OP_SIZE)(uEFlags, (OP_TYPE *)&puSrc1Mem[off], puSrc2Mem[off]);
                            off++;
                        } while (   off < cLeftPage
                                 && (uEFlags & X86_EFL_ZF));
                        uSrc1AddrReg += cbIncr * off;
                        uSrc2AddrReg += cbIncr * off;
                        uCounterReg  -= off;
                    }

                    /* Update the registers before looping. */
                    pVCpu->cpum.GstCtx.ADDR_rCX = uCounterReg;
                    pVCpu->cpum.GstCtx.ADDR_rSI = uSrc1AddrReg;
                    pVCpu->cpum.GstCtx.ADDR_rDI = uSrc2AddrReg;
                    pVCpu->cpum.GstCtx.eflags.u = uEFlags;

                    iemMemPageUnmap(pVCpu, GCPhysSrc1Mem, IEM_ACCESS_DATA_R, puSrc1Mem, &PgLockSrc1Mem);
                    iemMemPageUnmap(pVCpu, GCPhysSrc2Mem, IEM_ACCESS_DATA_R, puSrc2Mem, &PgLockSrc2Mem);
                    if (   uCounterReg == 0
                        || !(uEFlags & X86_EFL_ZF))
                        break;
                    IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, uEFlags);
                    continue;
                }
                iemMemPageUnmap(pVCpu, GCPhysSrc2Mem, IEM_ACCESS_DATA_R, puSrc2Mem, &PgLockSrc2Mem);
            }
        }

        /*
         * Fallback - slow processing till the end of the current page.
         * In the cross page boundrary case we will end up here with cLeftPage
         * as 0, we execute one loop then.
         */
        do
        {
            OP_TYPE uValue1;
            rcStrict = RT_CONCAT(iemMemFetchDataU,OP_SIZE)(pVCpu, &uValue1, iEffSeg, uSrc1AddrReg);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            OP_TYPE uValue2;
            rcStrict = RT_CONCAT(iemMemFetchDataU,OP_SIZE)(pVCpu, &uValue2, X86_SREG_ES, uSrc2AddrReg);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            uEFlags = RT_CONCAT(iemAImpl_cmp_u,OP_SIZE)(uEFlags, &uValue1, uValue2);

            pVCpu->cpum.GstCtx.ADDR_rSI = uSrc1AddrReg += cbIncr;
            pVCpu->cpum.GstCtx.ADDR_rDI = uSrc2AddrReg += cbIncr;
            pVCpu->cpum.GstCtx.ADDR_rCX = --uCounterReg;
            pVCpu->cpum.GstCtx.eflags.u = uEFlags;
            cLeftPage--;
            IEM_CHECK_FF_CPU_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN(pVM, pVCpu, uCounterReg == 0 || !(uEFlags & X86_EFL_ZF));
        } while (   (int32_t)cLeftPage > 0
                 && (uEFlags & X86_EFL_ZF));

        /*
         * Next page? Must check for interrupts and stuff here.
         */
        if (   uCounterReg == 0
            || !(uEFlags & X86_EFL_ZF))
            break;
        IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, uEFlags);
    }

    /*
     * Done.
     */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'REPNE CMPS'.
 */
IEM_CIMPL_DEF_1(RT_CONCAT4(iemCImpl_repne_cmps_op,OP_SIZE,_addr,ADDR_SIZE), uint8_t, iEffSeg)
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);

    /*
     * Setup.
     */
    ADDR_TYPE       uCounterReg = pVCpu->cpum.GstCtx.ADDR_rCX;
    if (uCounterReg == 0)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iEffSeg) | CPUMCTX_EXTRN_ES);

    PCCPUMSELREGHID pSrc1Hid     = iemSRegGetHid(pVCpu, iEffSeg);
    uint64_t        uSrc1Base    = 0; /* gcc may not be used uninitialized */;
    VBOXSTRICTRC rcStrict = iemMemSegCheckReadAccessEx(pVCpu, pSrc1Hid, iEffSeg, &uSrc1Base);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    uint64_t        uSrc2Base    = 0; /* gcc may not be used uninitialized */
    rcStrict = iemMemSegCheckReadAccessEx(pVCpu, iemSRegUpdateHid(pVCpu, &pVCpu->cpum.GstCtx.es), X86_SREG_ES, &uSrc2Base);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    int8_t const    cbIncr       = pVCpu->cpum.GstCtx.eflags.Bits.u1DF ? -(OP_SIZE / 8) : (OP_SIZE / 8);
    ADDR_TYPE       uSrc1AddrReg = pVCpu->cpum.GstCtx.ADDR_rSI;
    ADDR_TYPE       uSrc2AddrReg = pVCpu->cpum.GstCtx.ADDR_rDI;
    uint32_t        uEFlags      = pVCpu->cpum.GstCtx.eflags.u;

    /*
     * The loop.
     */
    for (;;)
    {
        /*
         * Do segmentation and virtual page stuff.
         */
        ADDR2_TYPE  uVirtSrc1Addr = uSrc1AddrReg + (ADDR2_TYPE)uSrc1Base;
        ADDR2_TYPE  uVirtSrc2Addr = uSrc2AddrReg + (ADDR2_TYPE)uSrc2Base;
        uint32_t    cLeftSrc1Page = (GUEST_PAGE_SIZE - (uVirtSrc1Addr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        if (cLeftSrc1Page > uCounterReg)
            cLeftSrc1Page = uCounterReg;
        uint32_t    cLeftSrc2Page = (GUEST_PAGE_SIZE - (uVirtSrc2Addr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        uint32_t    cLeftPage = RT_MIN(cLeftSrc1Page, cLeftSrc2Page);

        if (   cLeftPage > 0 /* can be null if unaligned, do one fallback round. */
            && cbIncr > 0    /** @todo Optimize reverse direction string ops. */
            && (   IS_64_BIT_CODE(pVCpu)
                || (   uSrc1AddrReg < pSrc1Hid->u32Limit
                    && uSrc1AddrReg + (cLeftPage * (OP_SIZE / 8)) <= pSrc1Hid->u32Limit
                    && uSrc2AddrReg < pVCpu->cpum.GstCtx.es.u32Limit
                    && uSrc2AddrReg + (cLeftPage * (OP_SIZE / 8)) <= pVCpu->cpum.GstCtx.es.u32Limit)
                )
           )
        {
            RTGCPHYS GCPhysSrc1Mem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtSrc1Addr, OP_SIZE / 8, IEM_ACCESS_DATA_R, &GCPhysSrc1Mem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            RTGCPHYS GCPhysSrc2Mem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtSrc2Addr, OP_SIZE / 8, IEM_ACCESS_DATA_R, &GCPhysSrc2Mem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /*
             * If we can map the page without trouble, do a block processing
             * until the end of the current page.
             */
            OP_TYPE const *puSrc2Mem;
            PGMPAGEMAPLOCK PgLockSrc2Mem;
            rcStrict = iemMemPageMap(pVCpu, GCPhysSrc2Mem, IEM_ACCESS_DATA_R, (void **)&puSrc2Mem, &PgLockSrc2Mem);
            if (rcStrict == VINF_SUCCESS)
            {
                OP_TYPE const *puSrc1Mem;
                PGMPAGEMAPLOCK PgLockSrc1Mem;
                rcStrict = iemMemPageMap(pVCpu, GCPhysSrc1Mem, IEM_ACCESS_DATA_R, (void **)&puSrc1Mem, &PgLockSrc1Mem);
                if (rcStrict == VINF_SUCCESS)
                {
                    if (memcmp(puSrc2Mem, puSrc1Mem, cLeftPage * (OP_SIZE / 8)))
                    {
                        /* All matches, only compare the last item to get the right eflags. */
                        uEFlags = RT_CONCAT(iemAImpl_cmp_u,OP_SIZE)(uEFlags, (OP_TYPE *)&puSrc1Mem[cLeftPage-1], puSrc2Mem[cLeftPage-1]);
                        uSrc1AddrReg += cLeftPage * cbIncr;
                        uSrc2AddrReg += cLeftPage * cbIncr;
                        uCounterReg  -= cLeftPage;
                    }
                    else
                    {
                        /* Some mismatch, compare each item (and keep volatile
                           memory in mind). */
                        uint32_t off = 0;
                        do
                        {
                            uEFlags = RT_CONCAT(iemAImpl_cmp_u,OP_SIZE)(uEFlags, (OP_TYPE *)&puSrc1Mem[off], puSrc2Mem[off]);
                            off++;
                        } while (   off < cLeftPage
                                 && !(uEFlags & X86_EFL_ZF));
                        uSrc1AddrReg += cbIncr * off;
                        uSrc2AddrReg += cbIncr * off;
                        uCounterReg  -= off;
                    }

                    /* Update the registers before looping. */
                    pVCpu->cpum.GstCtx.ADDR_rCX = uCounterReg;
                    pVCpu->cpum.GstCtx.ADDR_rSI = uSrc1AddrReg;
                    pVCpu->cpum.GstCtx.ADDR_rDI = uSrc2AddrReg;
                    pVCpu->cpum.GstCtx.eflags.u = uEFlags;

                    iemMemPageUnmap(pVCpu, GCPhysSrc1Mem, IEM_ACCESS_DATA_R, puSrc1Mem, &PgLockSrc1Mem);
                    iemMemPageUnmap(pVCpu, GCPhysSrc2Mem, IEM_ACCESS_DATA_R, puSrc2Mem, &PgLockSrc2Mem);
                    if (   uCounterReg == 0
                        || (uEFlags & X86_EFL_ZF))
                        break;
                    IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, uEFlags);
                    continue;
                }
                iemMemPageUnmap(pVCpu, GCPhysSrc2Mem, IEM_ACCESS_DATA_R, puSrc2Mem, &PgLockSrc2Mem);
            }
        }

        /*
         * Fallback - slow processing till the end of the current page.
         * In the cross page boundrary case we will end up here with cLeftPage
         * as 0, we execute one loop then.
         */
        do
        {
            OP_TYPE uValue1;
            rcStrict = RT_CONCAT(iemMemFetchDataU,OP_SIZE)(pVCpu, &uValue1, iEffSeg, uSrc1AddrReg);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            OP_TYPE uValue2;
            rcStrict = RT_CONCAT(iemMemFetchDataU,OP_SIZE)(pVCpu, &uValue2, X86_SREG_ES, uSrc2AddrReg);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            uEFlags = RT_CONCAT(iemAImpl_cmp_u,OP_SIZE)(uEFlags, &uValue1, uValue2);

            pVCpu->cpum.GstCtx.ADDR_rSI = uSrc1AddrReg += cbIncr;
            pVCpu->cpum.GstCtx.ADDR_rDI = uSrc2AddrReg += cbIncr;
            pVCpu->cpum.GstCtx.ADDR_rCX = --uCounterReg;
            pVCpu->cpum.GstCtx.eflags.u = uEFlags;
            cLeftPage--;
            IEM_CHECK_FF_CPU_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN(pVM, pVCpu, uCounterReg == 0 || (uEFlags & X86_EFL_ZF));
        } while (   (int32_t)cLeftPage > 0
                 && !(uEFlags & X86_EFL_ZF));

        /*
         * Next page? Must check for interrupts and stuff here.
         */
        if (   uCounterReg == 0
            || (uEFlags & X86_EFL_ZF))
            break;
        IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, uEFlags);
    }

    /*
     * Done.
     */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'REPE SCAS'.
 */
IEM_CIMPL_DEF_0(RT_CONCAT4(iemCImpl_repe_scas_,OP_rAX,_m,ADDR_SIZE))
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);

    /*
     * Setup.
     */
    ADDR_TYPE       uCounterReg = pVCpu->cpum.GstCtx.ADDR_rCX;
    if (uCounterReg == 0)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_ES);
    uint64_t        uBaseAddr   = 0; /* gcc may not be used uninitialized */
    VBOXSTRICTRC rcStrict = iemMemSegCheckReadAccessEx(pVCpu, iemSRegUpdateHid(pVCpu, &pVCpu->cpum.GstCtx.es), X86_SREG_ES, &uBaseAddr);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    int8_t const    cbIncr      = pVCpu->cpum.GstCtx.eflags.Bits.u1DF ? -(OP_SIZE / 8) : (OP_SIZE / 8);
    OP_TYPE const   uValueReg   = pVCpu->cpum.GstCtx.OP_rAX;
    ADDR_TYPE       uAddrReg    = pVCpu->cpum.GstCtx.ADDR_rDI;
    uint32_t        uEFlags     = pVCpu->cpum.GstCtx.eflags.u;

    /*
     * The loop.
     */
    for (;;)
    {
        /*
         * Do segmentation and virtual page stuff.
         */
        ADDR2_TYPE  uVirtAddr = uAddrReg + (ADDR2_TYPE)uBaseAddr;
        uint32_t    cLeftPage = (GUEST_PAGE_SIZE - (uVirtAddr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        if (cLeftPage > uCounterReg)
            cLeftPage = uCounterReg;
        if (   cLeftPage > 0 /* can be null if unaligned, do one fallback round. */
            && cbIncr > 0    /** @todo Implement reverse direction string ops. */
            && (   IS_64_BIT_CODE(pVCpu)
                || (   uAddrReg < pVCpu->cpum.GstCtx.es.u32Limit
                    && uAddrReg + (cLeftPage * (OP_SIZE / 8)) <= pVCpu->cpum.GstCtx.es.u32Limit)
               )
           )
        {
            RTGCPHYS GCPhysMem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtAddr, OP_SIZE / 8, IEM_ACCESS_DATA_R, &GCPhysMem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /*
             * If we can map the page without trouble, do a block processing
             * until the end of the current page.
             */
            PGMPAGEMAPLOCK PgLockMem;
            OP_TYPE const *puMem;
            rcStrict = iemMemPageMap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_R, (void **)&puMem, &PgLockMem);
            if (rcStrict == VINF_SUCCESS)
            {
                /* Search till we find a mismatching item. */
                OP_TYPE  uTmpValue;
                bool     fQuit;
                uint32_t i = 0;
                do
                {
                    uTmpValue = puMem[i++];
                    fQuit = uTmpValue != uValueReg;
                } while (i < cLeftPage && !fQuit);

                /* Update the regs. */
                uEFlags = RT_CONCAT(iemAImpl_cmp_u,OP_SIZE)(uEFlags, (OP_TYPE *)&uValueReg, uTmpValue);
                pVCpu->cpum.GstCtx.ADDR_rCX = uCounterReg -= i;
                pVCpu->cpum.GstCtx.ADDR_rDI = uAddrReg    += i * cbIncr;
                pVCpu->cpum.GstCtx.eflags.u = uEFlags;
                Assert(!(uEFlags & X86_EFL_ZF) == fQuit);
                iemMemPageUnmap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_R, puMem, &PgLockMem);
                if (   fQuit
                    || uCounterReg == 0)
                    break;

                /* If unaligned, we drop thru and do the page crossing access
                   below. Otherwise, do the next page. */
                if (!(uVirtAddr & (OP_SIZE / 8 - 1)))
                {
                    IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, uEFlags);
                    continue;
                }
                cLeftPage = 0;
            }
        }

        /*
         * Fallback - slow processing till the end of the current page.
         * In the cross page boundrary case we will end up here with cLeftPage
         * as 0, we execute one loop then.
         */
        do
        {
            OP_TYPE uTmpValue;
            rcStrict = RT_CONCAT(iemMemFetchDataU,OP_SIZE)(pVCpu, &uTmpValue, X86_SREG_ES, uAddrReg);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            uEFlags = RT_CONCAT(iemAImpl_cmp_u,OP_SIZE)(uEFlags, (OP_TYPE *)&uValueReg, uTmpValue);

            pVCpu->cpum.GstCtx.ADDR_rDI = uAddrReg += cbIncr;
            pVCpu->cpum.GstCtx.ADDR_rCX = --uCounterReg;
            pVCpu->cpum.GstCtx.eflags.u = uEFlags;
            cLeftPage--;
            IEM_CHECK_FF_CPU_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN(pVM, pVCpu, uCounterReg == 0 || !(uEFlags & X86_EFL_ZF));
        } while (   (int32_t)cLeftPage > 0
                 && (uEFlags & X86_EFL_ZF));

        /*
         * Next page? Must check for interrupts and stuff here.
         */
        if (   uCounterReg == 0
            || !(uEFlags & X86_EFL_ZF))
            break;
        IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, uEFlags);
    }

    /*
     * Done.
     */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'REPNE SCAS'.
 */
IEM_CIMPL_DEF_0(RT_CONCAT4(iemCImpl_repne_scas_,OP_rAX,_m,ADDR_SIZE))
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);

    /*
     * Setup.
     */
    ADDR_TYPE       uCounterReg = pVCpu->cpum.GstCtx.ADDR_rCX;
    if (uCounterReg == 0)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_ES);
    uint64_t        uBaseAddr   = 0; /* gcc may not be used uninitialized */
    VBOXSTRICTRC rcStrict = iemMemSegCheckReadAccessEx(pVCpu, iemSRegUpdateHid(pVCpu, &pVCpu->cpum.GstCtx.es), X86_SREG_ES, &uBaseAddr);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    int8_t const    cbIncr      = pVCpu->cpum.GstCtx.eflags.Bits.u1DF ? -(OP_SIZE / 8) : (OP_SIZE / 8);
    OP_TYPE const   uValueReg   = pVCpu->cpum.GstCtx.OP_rAX;
    ADDR_TYPE       uAddrReg    = pVCpu->cpum.GstCtx.ADDR_rDI;
    uint32_t        uEFlags     = pVCpu->cpum.GstCtx.eflags.u;

    /*
     * The loop.
     */
    for (;;)
    {
        /*
         * Do segmentation and virtual page stuff.
         */
        ADDR2_TYPE  uVirtAddr = uAddrReg + (ADDR2_TYPE)uBaseAddr;
        uint32_t    cLeftPage = (GUEST_PAGE_SIZE - (uVirtAddr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        if (cLeftPage > uCounterReg)
            cLeftPage = uCounterReg;
        if (   cLeftPage > 0 /* can be null if unaligned, do one fallback round. */
            && cbIncr > 0    /** @todo Implement reverse direction string ops. */
            && (   IS_64_BIT_CODE(pVCpu)
                || (   uAddrReg < pVCpu->cpum.GstCtx.es.u32Limit
                    && uAddrReg + (cLeftPage * (OP_SIZE / 8)) <= pVCpu->cpum.GstCtx.es.u32Limit)
               )
           )
        {
            RTGCPHYS GCPhysMem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtAddr, OP_SIZE / 8, IEM_ACCESS_DATA_R, &GCPhysMem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /*
             * If we can map the page without trouble, do a block processing
             * until the end of the current page.
             */
            PGMPAGEMAPLOCK PgLockMem;
            OP_TYPE const *puMem;
            rcStrict = iemMemPageMap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_R, (void **)&puMem, &PgLockMem);
            if (rcStrict == VINF_SUCCESS)
            {
                /* Search till we find a mismatching item. */
                OP_TYPE  uTmpValue;
                bool     fQuit;
                uint32_t i = 0;
                do
                {
                    uTmpValue = puMem[i++];
                    fQuit = uTmpValue == uValueReg;
                } while (i < cLeftPage && !fQuit);

                /* Update the regs. */
                uEFlags = RT_CONCAT(iemAImpl_cmp_u,OP_SIZE)(uEFlags, (OP_TYPE *)&uValueReg, uTmpValue);
                pVCpu->cpum.GstCtx.ADDR_rCX = uCounterReg -= i;
                pVCpu->cpum.GstCtx.ADDR_rDI = uAddrReg    += i * cbIncr;
                pVCpu->cpum.GstCtx.eflags.u = uEFlags;
                Assert(!!(uEFlags & X86_EFL_ZF) == fQuit);
                iemMemPageUnmap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_R, puMem, &PgLockMem);
                if (   fQuit
                    || uCounterReg == 0)
                    break;

                /* If unaligned, we drop thru and do the page crossing access
                   below. Otherwise, do the next page. */
                if (!(uVirtAddr & (OP_SIZE / 8 - 1)))
                {
                    IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, uEFlags);
                    continue;
                }
                cLeftPage = 0;
            }
        }

        /*
         * Fallback - slow processing till the end of the current page.
         * In the cross page boundrary case we will end up here with cLeftPage
         * as 0, we execute one loop then.
         */
        do
        {
            OP_TYPE uTmpValue;
            rcStrict = RT_CONCAT(iemMemFetchDataU,OP_SIZE)(pVCpu, &uTmpValue, X86_SREG_ES, uAddrReg);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            uEFlags = RT_CONCAT(iemAImpl_cmp_u,OP_SIZE)(uEFlags, (OP_TYPE *)&uValueReg, uTmpValue);
            pVCpu->cpum.GstCtx.ADDR_rDI = uAddrReg += cbIncr;
            pVCpu->cpum.GstCtx.ADDR_rCX = --uCounterReg;
            pVCpu->cpum.GstCtx.eflags.u = uEFlags;
            cLeftPage--;
            IEM_CHECK_FF_CPU_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN(pVM, pVCpu, uCounterReg == 0 || (uEFlags & X86_EFL_ZF));
        } while (   (int32_t)cLeftPage > 0
                 && !(uEFlags & X86_EFL_ZF));

        /*
         * Next page? Must check for interrupts and stuff here.
         */
        if (   uCounterReg == 0
            || (uEFlags & X86_EFL_ZF))
            break;
        IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, uEFlags);
    }

    /*
     * Done.
     */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}




/**
 * Implements 'REP MOVS'.
 */
IEM_CIMPL_DEF_1(RT_CONCAT4(iemCImpl_rep_movs_op,OP_SIZE,_addr,ADDR_SIZE), uint8_t, iEffSeg)
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);

    /*
     * Setup.
     */
    ADDR_TYPE       uCounterReg = pVCpu->cpum.GstCtx.ADDR_rCX;
    if (uCounterReg == 0)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iEffSeg) | CPUMCTX_EXTRN_ES);

    PCCPUMSELREGHID pSrcHid   = iemSRegGetHid(pVCpu, iEffSeg);
    uint64_t        uSrcBase  = 0; /* gcc may not be used uninitialized */
    VBOXSTRICTRC rcStrict = iemMemSegCheckReadAccessEx(pVCpu, pSrcHid, iEffSeg, &uSrcBase);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    uint64_t        uDstBase  = 0; /* gcc may not be used uninitialized */
    rcStrict = iemMemSegCheckWriteAccessEx(pVCpu, iemSRegUpdateHid(pVCpu, &pVCpu->cpum.GstCtx.es), X86_SREG_ES, &uDstBase);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    int8_t const    cbIncr      = pVCpu->cpum.GstCtx.eflags.Bits.u1DF ? -(OP_SIZE / 8) : (OP_SIZE / 8);
    ADDR_TYPE       uSrcAddrReg = pVCpu->cpum.GstCtx.ADDR_rSI;
    ADDR_TYPE       uDstAddrReg = pVCpu->cpum.GstCtx.ADDR_rDI;

    /*
     * Be careful with handle bypassing.
     */
    if (pVCpu->iem.s.fExec & IEM_F_BYPASS_HANDLERS)
    {
        Log(("%s: declining because we're bypassing handlers\n", __FUNCTION__));
        return VERR_IEM_ASPECT_NOT_IMPLEMENTED;
    }

    /*
     * The loop.
     */
    for (;;)
    {
        /*
         * Do segmentation and virtual page stuff.
         */
        ADDR2_TYPE  uVirtSrcAddr = uSrcAddrReg + (ADDR2_TYPE)uSrcBase;
        ADDR2_TYPE  uVirtDstAddr = uDstAddrReg + (ADDR2_TYPE)uDstBase;
        uint32_t    cLeftSrcPage = (GUEST_PAGE_SIZE - (uVirtSrcAddr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        if (cLeftSrcPage > uCounterReg)
            cLeftSrcPage = uCounterReg;
        uint32_t    cLeftDstPage = (GUEST_PAGE_SIZE - (uVirtDstAddr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        uint32_t    cLeftPage = RT_MIN(cLeftSrcPage, cLeftDstPage);

        if (   cLeftPage > 0 /* can be null if unaligned, do one fallback round. */
            && cbIncr > 0    /** @todo Implement reverse direction string ops. */
            && (   IS_64_BIT_CODE(pVCpu)
                || (   uSrcAddrReg < pSrcHid->u32Limit
                    && uSrcAddrReg + (cLeftPage * (OP_SIZE / 8)) <= pSrcHid->u32Limit
                    && uDstAddrReg < pVCpu->cpum.GstCtx.es.u32Limit
                    && uDstAddrReg + (cLeftPage * (OP_SIZE / 8)) <= pVCpu->cpum.GstCtx.es.u32Limit)
               )
           )
        {
            RTGCPHYS GCPhysSrcMem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtSrcAddr, OP_SIZE / 8, IEM_ACCESS_DATA_R, &GCPhysSrcMem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            RTGCPHYS GCPhysDstMem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtDstAddr, OP_SIZE / 8, IEM_ACCESS_DATA_W, &GCPhysDstMem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /*
             * If we can map the page without trouble, do a block processing
             * until the end of the current page.
             */
            PGMPAGEMAPLOCK PgLockDstMem;
            OP_TYPE *puDstMem;
            rcStrict = iemMemPageMap(pVCpu, GCPhysDstMem, IEM_ACCESS_DATA_W, (void **)&puDstMem, &PgLockDstMem);
            if (rcStrict == VINF_SUCCESS)
            {
                PGMPAGEMAPLOCK PgLockSrcMem;
                OP_TYPE const *puSrcMem;
                rcStrict = iemMemPageMap(pVCpu, GCPhysSrcMem, IEM_ACCESS_DATA_R, (void **)&puSrcMem, &PgLockSrcMem);
                if (rcStrict == VINF_SUCCESS)
                {
                    Assert(   (GCPhysSrcMem         >> GUEST_PAGE_SHIFT) != (GCPhysDstMem         >> GUEST_PAGE_SHIFT)
                           || ((uintptr_t)puSrcMem  >> GUEST_PAGE_SHIFT) == ((uintptr_t)puDstMem  >> GUEST_PAGE_SHIFT));

                    /* Perform the operation exactly (don't use memcpy to avoid
                       having to consider how its implementation would affect
                       any overlapping source and destination area). */
                    OP_TYPE const  *puSrcCur = puSrcMem;
                    OP_TYPE        *puDstCur = puDstMem;
                    uint32_t        cTodo    = cLeftPage;
                    while (cTodo-- > 0)
                        *puDstCur++ = *puSrcCur++;

                    /* Update the registers. */
                    pVCpu->cpum.GstCtx.ADDR_rSI = uSrcAddrReg += cLeftPage * cbIncr;
                    pVCpu->cpum.GstCtx.ADDR_rDI = uDstAddrReg += cLeftPage * cbIncr;
                    pVCpu->cpum.GstCtx.ADDR_rCX = uCounterReg -= cLeftPage;

                    iemMemPageUnmap(pVCpu, GCPhysSrcMem, IEM_ACCESS_DATA_R, puSrcMem, &PgLockSrcMem);
                    iemMemPageUnmap(pVCpu, GCPhysDstMem, IEM_ACCESS_DATA_W, puDstMem, &PgLockDstMem);

                    if (uCounterReg == 0)
                        break;
                    IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, pVCpu->cpum.GstCtx.eflags.u);
                    continue;
                }
                iemMemPageUnmap(pVCpu, GCPhysDstMem, IEM_ACCESS_DATA_W, puDstMem, &PgLockDstMem);
            }
        }

        /*
         * Fallback - slow processing till the end of the current page.
         * In the cross page boundrary case we will end up here with cLeftPage
         * as 0, we execute one loop then.
         */
        do
        {
            OP_TYPE uValue;
            rcStrict = RT_CONCAT(iemMemFetchDataU,OP_SIZE)(pVCpu, &uValue, iEffSeg, uSrcAddrReg);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            rcStrict = RT_CONCAT(iemMemStoreDataU,OP_SIZE)(pVCpu, X86_SREG_ES, uDstAddrReg, uValue);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            pVCpu->cpum.GstCtx.ADDR_rSI = uSrcAddrReg += cbIncr;
            pVCpu->cpum.GstCtx.ADDR_rDI = uDstAddrReg += cbIncr;
            pVCpu->cpum.GstCtx.ADDR_rCX = --uCounterReg;
            cLeftPage--;
            IEM_CHECK_FF_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN(pVM, pVCpu, uCounterReg == 0);
        } while ((int32_t)cLeftPage > 0);

        /*
         * Next page.  Must check for interrupts and stuff here.
         */
        if (uCounterReg == 0)
            break;
        IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, pVCpu->cpum.GstCtx.eflags.u);
    }

    /*
     * Done.
     */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'REP STOS'.
 */
IEM_CIMPL_DEF_0(RT_CONCAT4(iemCImpl_stos_,OP_rAX,_m,ADDR_SIZE))
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);

    /*
     * Setup.
     */
    ADDR_TYPE       uCounterReg = pVCpu->cpum.GstCtx.ADDR_rCX;
    if (uCounterReg == 0)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_ES);

    uint64_t        uBaseAddr   = 0; /* gcc may not be used uninitialized */
    VBOXSTRICTRC rcStrict = iemMemSegCheckWriteAccessEx(pVCpu, iemSRegUpdateHid(pVCpu, &pVCpu->cpum.GstCtx.es), X86_SREG_ES, &uBaseAddr);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    int8_t const    cbIncr      = pVCpu->cpum.GstCtx.eflags.Bits.u1DF ? -(OP_SIZE / 8) : (OP_SIZE / 8);
    OP_TYPE const   uValue      = pVCpu->cpum.GstCtx.OP_rAX;
    ADDR_TYPE       uAddrReg    = pVCpu->cpum.GstCtx.ADDR_rDI;

    /*
     * Be careful with handle bypassing.
     */
    /** @todo Permit doing a page if correctly aligned. */
    if (pVCpu->iem.s.fExec & IEM_F_BYPASS_HANDLERS)
    {
        Log(("%s: declining because we're bypassing handlers\n", __FUNCTION__));
        return VERR_IEM_ASPECT_NOT_IMPLEMENTED;
    }

    /*
     * The loop.
     */
    for (;;)
    {
        /*
         * Do segmentation and virtual page stuff.
         */
        ADDR2_TYPE  uVirtAddr = uAddrReg + (ADDR2_TYPE)uBaseAddr;
        uint32_t    cLeftPage = (GUEST_PAGE_SIZE - (uVirtAddr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        if (cLeftPage > uCounterReg)
            cLeftPage = uCounterReg;
        if (   cLeftPage > 0 /* can be null if unaligned, do one fallback round. */
            && cbIncr > 0    /** @todo Implement reverse direction string ops. */
            && (   IS_64_BIT_CODE(pVCpu)
                || (   uAddrReg < pVCpu->cpum.GstCtx.es.u32Limit
                    && uAddrReg + (cLeftPage * (OP_SIZE / 8)) <= pVCpu->cpum.GstCtx.es.u32Limit)
               )
           )
        {
            RTGCPHYS GCPhysMem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtAddr, OP_SIZE / 8, IEM_ACCESS_DATA_W, &GCPhysMem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /*
             * If we can map the page without trouble, do a block processing
             * until the end of the current page.
             */
            PGMPAGEMAPLOCK PgLockMem;
            OP_TYPE *puMem;
            rcStrict = iemMemPageMap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_W, (void **)&puMem, &PgLockMem);
            if (rcStrict == VINF_SUCCESS)
            {
                /* Update the regs first so we can loop on cLeftPage. */
                pVCpu->cpum.GstCtx.ADDR_rCX = uCounterReg -= cLeftPage;
                pVCpu->cpum.GstCtx.ADDR_rDI = uAddrReg    += cLeftPage * cbIncr;

                /* Do the memsetting. */
#if OP_SIZE == 8
                memset(puMem, uValue, cLeftPage);
/*#elif OP_SIZE == 32
                ASMMemFill32(puMem, cLeftPage * (OP_SIZE / 8), uValue);*/
#else
                while (cLeftPage-- > 0)
                    *puMem++ = uValue;
#endif

                iemMemPageUnmap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_W, puMem, &PgLockMem);

                if (uCounterReg == 0)
                    break;

                /* If unaligned, we drop thru and do the page crossing access
                   below. Otherwise, do the next page. */
                if (!(uVirtAddr & (OP_SIZE / 8 - 1)))
                {
                    IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, pVCpu->cpum.GstCtx.eflags.u);
                    continue;
                }
                cLeftPage = 0;
            }
            /* If we got an invalid physical address in the page table, just skip
               ahead to the next page or the counter reaches zero.  This crazy
               optimization is for a buggy EFI firmware that's driving me nuts. */
            else if (rcStrict == VERR_PGM_PHYS_TLB_UNASSIGNED)
            {
                pVCpu->cpum.GstCtx.ADDR_rCX = uCounterReg -= cLeftPage;
                pVCpu->cpum.GstCtx.ADDR_rDI = uAddrReg    += cLeftPage * cbIncr;
                if (uCounterReg == 0)
                    break;
                if (!(uVirtAddr & (OP_SIZE / 8 - 1)))
                {
                    IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, pVCpu->cpum.GstCtx.eflags.u);
                    continue;
                }
            }
        }

        /*
         * Fallback - slow processing till the end of the current page.
         * In the cross page boundrary case we will end up here with cLeftPage
         * as 0, we execute one loop then.
         */
        do
        {
            rcStrict = RT_CONCAT(iemMemStoreDataU,OP_SIZE)(pVCpu, X86_SREG_ES, uAddrReg, uValue);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            pVCpu->cpum.GstCtx.ADDR_rDI = uAddrReg += cbIncr;
            pVCpu->cpum.GstCtx.ADDR_rCX = --uCounterReg;
            cLeftPage--;
            IEM_CHECK_FF_CPU_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN(pVM, pVCpu, uCounterReg == 0);
        } while ((int32_t)cLeftPage > 0);

        /*
         * Next page.  Must check for interrupts and stuff here.
         */
        if (uCounterReg == 0)
            break;
        IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, pVCpu->cpum.GstCtx.eflags.u);
    }

    /*
     * Done.
     */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'REP LODS'.
 */
IEM_CIMPL_DEF_1(RT_CONCAT4(iemCImpl_lods_,OP_rAX,_m,ADDR_SIZE), int8_t, iEffSeg)
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);

    /*
     * Setup.
     */
    ADDR_TYPE       uCounterReg = pVCpu->cpum.GstCtx.ADDR_rCX;
    if (uCounterReg == 0)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iEffSeg));
    PCCPUMSELREGHID pSrcHid   = iemSRegGetHid(pVCpu, iEffSeg);
    uint64_t        uBaseAddr = 0; /* gcc may not be used uninitialized */
    VBOXSTRICTRC rcStrict = iemMemSegCheckReadAccessEx(pVCpu, pSrcHid, iEffSeg, &uBaseAddr);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    int8_t const    cbIncr      = pVCpu->cpum.GstCtx.eflags.Bits.u1DF ? -(OP_SIZE / 8) : (OP_SIZE / 8);
    ADDR_TYPE       uAddrReg    = pVCpu->cpum.GstCtx.ADDR_rSI;

    /*
     * The loop.
     */
    for (;;)
    {
        /*
         * Do segmentation and virtual page stuff.
         */
        ADDR2_TYPE  uVirtAddr = uAddrReg + (ADDR2_TYPE)uBaseAddr;
        uint32_t    cLeftPage = (GUEST_PAGE_SIZE - (uVirtAddr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        if (cLeftPage > uCounterReg)
            cLeftPage = uCounterReg;
        if (   cLeftPage > 0 /* can be null if unaligned, do one fallback round. */
            && cbIncr > 0    /** @todo Implement reverse direction string ops. */
            && (   IS_64_BIT_CODE(pVCpu)
                || (   uAddrReg < pSrcHid->u32Limit
                    && uAddrReg + (cLeftPage * (OP_SIZE / 8)) <= pSrcHid->u32Limit)
               )
           )
        {
            RTGCPHYS GCPhysMem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtAddr, OP_SIZE / 8, IEM_ACCESS_DATA_R, &GCPhysMem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /*
             * If we can map the page without trouble, we can get away with
             * just reading the last value on the page.
             */
            PGMPAGEMAPLOCK PgLockMem;
            OP_TYPE const *puMem;
            rcStrict = iemMemPageMap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_R, (void **)&puMem, &PgLockMem);
            if (rcStrict == VINF_SUCCESS)
            {
                /* Only get the last byte, the rest doesn't matter in direct access mode. */
#if OP_SIZE == 32
                pVCpu->cpum.GstCtx.rax      = puMem[cLeftPage - 1];
#else
                pVCpu->cpum.GstCtx.OP_rAX   = puMem[cLeftPage - 1];
#endif
                pVCpu->cpum.GstCtx.ADDR_rCX = uCounterReg -= cLeftPage;
                pVCpu->cpum.GstCtx.ADDR_rSI = uAddrReg    += cLeftPage * cbIncr;
                iemMemPageUnmap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_R, puMem, &PgLockMem);

                if (uCounterReg == 0)
                    break;

                /* If unaligned, we drop thru and do the page crossing access
                   below. Otherwise, do the next page. */
                if (!(uVirtAddr & (OP_SIZE / 8 - 1)))
                {
                    IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, pVCpu->cpum.GstCtx.eflags.u);
                    continue;
                }
                cLeftPage = 0;
            }
        }

        /*
         * Fallback - slow processing till the end of the current page.
         * In the cross page boundrary case we will end up here with cLeftPage
         * as 0, we execute one loop then.
         */
        do
        {
            OP_TYPE uTmpValue;
            rcStrict = RT_CONCAT(iemMemFetchDataU,OP_SIZE)(pVCpu, &uTmpValue, iEffSeg, uAddrReg);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
#if OP_SIZE == 32
            pVCpu->cpum.GstCtx.rax      = uTmpValue;
#else
            pVCpu->cpum.GstCtx.OP_rAX   = uTmpValue;
#endif
            pVCpu->cpum.GstCtx.ADDR_rSI = uAddrReg += cbIncr;
            pVCpu->cpum.GstCtx.ADDR_rCX = --uCounterReg;
            cLeftPage--;
            IEM_CHECK_FF_CPU_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN(pVM, pVCpu, uCounterReg == 0);
        } while ((int32_t)cLeftPage > 0);

        /*
         * Next page.  Must check for interrupts and stuff here.
         */
        if (uCounterReg == 0)
            break;
        IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, pVCpu->cpum.GstCtx.eflags.u);
    }

    /*
     * Done.
     */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


#if OP_SIZE != 64

/**
 * Implements 'INS' (no rep)
 */
IEM_CIMPL_DEF_1(RT_CONCAT4(iemCImpl_ins_op,OP_SIZE,_addr,ADDR_SIZE), bool, fIoChecked)
{
    PVMCC           pVM  = pVCpu->CTX_SUFF(pVM);
    VBOXSTRICTRC    rcStrict;

    /*
     * Be careful with handle bypassing.
     */
    if (pVCpu->iem.s.fExec & IEM_F_BYPASS_HANDLERS)
    {
        Log(("%s: declining because we're bypassing handlers\n", __FUNCTION__));
        return VERR_IEM_ASPECT_NOT_IMPLEMENTED;
    }

    /*
     * ASSUMES the #GP for I/O permission is taken first, then any #GP for
     * segmentation and finally any #PF due to virtual address translation.
     * ASSUMES nothing is read from the I/O port before traps are taken.
     */
    if (!fIoChecked)
    {
        rcStrict = iemHlpCheckPortIOPermission(pVCpu, pVCpu->cpum.GstCtx.dx, OP_SIZE / 8);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    /*
     * Check nested-guest I/O intercepts.
     */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        VMXEXITINSTRINFO ExitInstrInfo;
        ExitInstrInfo.u = 0;
        ExitInstrInfo.StrIo.u3AddrSize = ADDR_VMXSTRIO;
        ExitInstrInfo.StrIo.iSegReg    = X86_SREG_ES;
        rcStrict = iemVmxVmexitInstrStrIo(pVCpu, VMXINSTRID_IO_INS, pVCpu->cpum.GstCtx.dx, OP_SIZE / 8, false /* fRep */,
                                          ExitInstrInfo, cbInstr);
        if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }
#endif

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_IOIO_PROT))
    {
        rcStrict = iemSvmHandleIOIntercept(pVCpu, pVCpu->cpum.GstCtx.dx, SVMIOIOTYPE_IN, OP_SIZE / 8, ADDR_SIZE, X86_SREG_ES,
                                           false /* fRep */, true /* fStrIo */, cbInstr);
        if (rcStrict == VINF_SVM_VMEXIT)
            return VINF_SUCCESS;
        if (rcStrict != VINF_SVM_INTERCEPT_NOT_ACTIVE)
        {
            Log(("iemCImpl_ins_op: iemSvmHandleIOIntercept failed (u16Port=%#x, cbReg=%u) rc=%Rrc\n", pVCpu->cpum.GstCtx.dx,
                 OP_SIZE / 8, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }
#endif

    uint8_t         bUnmapInfo;
    OP_TYPE        *puMem;
    rcStrict = iemMemMap(pVCpu, (void **)&puMem, &bUnmapInfo, OP_SIZE / 8, X86_SREG_ES, pVCpu->cpum.GstCtx.ADDR_rDI,
                         IEM_ACCESS_DATA_W, OP_SIZE / 8 - 1);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    uint32_t        u32Value = 0;
    rcStrict = IOMIOPortRead(pVM, pVCpu, pVCpu->cpum.GstCtx.dx, &u32Value, OP_SIZE / 8);
    if (IOM_SUCCESS(rcStrict))
    {
        /**
         * @todo I/O breakpoint support for INS
         */
        *puMem = (OP_TYPE)u32Value;
# ifdef IN_RING3
        VBOXSTRICTRC rcStrict2 = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
# else
        VBOXSTRICTRC rcStrict2 = iemMemCommitAndUnmapPostponeTroubleToR3(pVCpu, bUnmapInfo);
# endif
        if (RT_LIKELY(rcStrict2 == VINF_SUCCESS))
        {
            if (!pVCpu->cpum.GstCtx.eflags.Bits.u1DF)
                pVCpu->cpum.GstCtx.ADDR_rDI += OP_SIZE / 8;
            else
                pVCpu->cpum.GstCtx.ADDR_rDI -= OP_SIZE / 8;

            /** @todo finish: work out how this should work wrt status codes. Not sure we
             * can use iemSetPassUpStatus here, but it depends on what
             * iemRegAddToRipAndFinishingClearingRF may eventually return (if anything)... */
            rcStrict2 = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            if (rcStrict2 != VINF_SUCCESS)
            {
                iemSetPassUpStatus(pVCpu, rcStrict);
                rcStrict = rcStrict2;
            }
            pVCpu->iem.s.cPotentialExits++;
        }
        else
            AssertLogRelMsgFailedReturn(("rcStrict2=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict2)), RT_FAILURE_NP(rcStrict2) ? rcStrict2 : VERR_IEM_IPE_1);
    }
    return rcStrict;
}


/**
 * Implements 'REP INS'.
 */
IEM_CIMPL_DEF_1(RT_CONCAT4(iemCImpl_rep_ins_op,OP_SIZE,_addr,ADDR_SIZE), bool, fIoChecked)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_ES | CPUMCTX_EXTRN_TR);

    /*
     * Setup.
     */
    uint16_t const  u16Port    = pVCpu->cpum.GstCtx.dx;
    VBOXSTRICTRC    rcStrict;
    if (!fIoChecked)
    {
/** @todo check if this is too early for ecx=0. */
        rcStrict = iemHlpCheckPortIOPermission(pVCpu, u16Port, OP_SIZE / 8);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    /*
     * Check nested-guest I/O intercepts.
     */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        VMXEXITINSTRINFO ExitInstrInfo;
        ExitInstrInfo.u = 0;
        ExitInstrInfo.StrIo.u3AddrSize = ADDR_VMXSTRIO;
        ExitInstrInfo.StrIo.iSegReg    = X86_SREG_ES;
        rcStrict = iemVmxVmexitInstrStrIo(pVCpu, VMXINSTRID_IO_INS, pVCpu->cpum.GstCtx.dx, OP_SIZE / 8, true /* fRep */,
                                          ExitInstrInfo, cbInstr);
        if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }
#endif

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_IOIO_PROT))
    {
        rcStrict = iemSvmHandleIOIntercept(pVCpu, u16Port, SVMIOIOTYPE_IN, OP_SIZE / 8, ADDR_SIZE, X86_SREG_ES, true /* fRep */,
                                           true /* fStrIo */, cbInstr);
        if (rcStrict == VINF_SVM_VMEXIT)
            return VINF_SUCCESS;
        if (rcStrict != VINF_SVM_INTERCEPT_NOT_ACTIVE)
        {
            Log(("iemCImpl_rep_ins_op: iemSvmHandleIOIntercept failed (u16Port=%#x, cbReg=%u) rc=%Rrc\n", u16Port, OP_SIZE / 8,
                 VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }
#endif

    ADDR_TYPE       uCounterReg = pVCpu->cpum.GstCtx.ADDR_rCX;
    if (uCounterReg == 0)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);

    uint64_t        uBaseAddr   = 0; /* gcc may not be used uninitialized */
    rcStrict = iemMemSegCheckWriteAccessEx(pVCpu, iemSRegUpdateHid(pVCpu, &pVCpu->cpum.GstCtx.es), X86_SREG_ES, &uBaseAddr);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    int8_t const    cbIncr      = pVCpu->cpum.GstCtx.eflags.Bits.u1DF ? -(OP_SIZE / 8) : (OP_SIZE / 8);
    ADDR_TYPE       uAddrReg    = pVCpu->cpum.GstCtx.ADDR_rDI;

    /*
     * Be careful with handle bypassing.
     */
    if (pVCpu->iem.s.fExec & IEM_F_BYPASS_HANDLERS)
    {
        Log(("%s: declining because we're bypassing handlers\n", __FUNCTION__));
        return VERR_IEM_ASPECT_NOT_IMPLEMENTED;
    }

    /*
     * The loop.
     */
    for (;;)
    {
        /*
         * Do segmentation and virtual page stuff.
         */
        ADDR2_TYPE  uVirtAddr = uAddrReg + (ADDR2_TYPE)uBaseAddr;
        uint32_t    cLeftPage = (GUEST_PAGE_SIZE - (uVirtAddr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        if (cLeftPage > uCounterReg)
            cLeftPage = uCounterReg;
        if (   cLeftPage > 0 /* can be null if unaligned, do one fallback round. */
            && cbIncr > 0    /** @todo Implement reverse direction string ops. */
            && (   IS_64_BIT_CODE(pVCpu)
                || (   uAddrReg < pVCpu->cpum.GstCtx.es.u32Limit
                    && uAddrReg + (cLeftPage * (OP_SIZE / 8)) <= pVCpu->cpum.GstCtx.es.u32Limit)
               )
           )
        {
            RTGCPHYS GCPhysMem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtAddr, OP_SIZE / 8, IEM_ACCESS_DATA_W, &GCPhysMem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /*
             * If we can map the page without trouble, use the IOM
             * string I/O interface to do the work.
             */
            PGMPAGEMAPLOCK PgLockMem;
            OP_TYPE *puMem;
            rcStrict = iemMemPageMap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_W, (void **)&puMem, &PgLockMem);
            if (rcStrict == VINF_SUCCESS)
            {
                uint32_t cTransfers = cLeftPage;
                rcStrict = IOMIOPortReadString(pVM, pVCpu, u16Port, puMem, &cTransfers, OP_SIZE / 8);

                uint32_t cActualTransfers = cLeftPage - cTransfers;
                Assert(cActualTransfers <= cLeftPage);
                pVCpu->cpum.GstCtx.ADDR_rDI = uAddrReg    += cbIncr * cActualTransfers;
                pVCpu->cpum.GstCtx.ADDR_rCX = uCounterReg -= cActualTransfers;
                puMem += cActualTransfers;

                iemMemPageUnmap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_W, puMem, &PgLockMem);

                if (rcStrict != VINF_SUCCESS)
                {
                    if (IOM_SUCCESS(rcStrict))
                    {
                        /** @todo finish: work out how this should work wrt status codes. Not sure we
                         * can use iemSetPassUpStatus here, but it depends on what
                         * iemRegAddToRipAndFinishingClearingRF may eventually return (if anything)... */
                        rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                        if (uCounterReg == 0)
                            rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
                        pVCpu->iem.s.cPotentialExits++;
                    }
                    return rcStrict;
                }

                /* If unaligned, we drop thru and do the page crossing access
                   below. Otherwise, do the next page. */
                if (uCounterReg == 0)
                    break;
                if (!(uVirtAddr & (OP_SIZE / 8 - 1)))
                {
                    IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, pVCpu->cpum.GstCtx.eflags.u);
                    continue;
                }
                cLeftPage = 0;
            }
        }

        /*
         * Fallback - slow processing till the end of the current page.
         * In the cross page boundrary case we will end up here with cLeftPage
         * as 0, we execute one loop then.
         *
         * Note! We ASSUME the CPU will raise #PF or #GP before access the
         *       I/O port, otherwise it wouldn't really be restartable.
         */
        /** @todo investigate what the CPU actually does with \#PF/\#GP
         *        during INS. */
        do
        {
            uint8_t  bUnmapInfo;
            OP_TYPE *puMem;
            rcStrict = iemMemMap(pVCpu, (void **)&puMem, &bUnmapInfo, OP_SIZE / 8, X86_SREG_ES, uAddrReg,
                                 IEM_ACCESS_DATA_W, OP_SIZE / 8 - 1);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            uint32_t u32Value = 0;
            rcStrict = IOMIOPortRead(pVM, pVCpu, u16Port, &u32Value, OP_SIZE / 8);
            if (!IOM_SUCCESS(rcStrict))
            {
                iemMemRollback(pVCpu);
                return rcStrict;
            }

            *puMem = (OP_TYPE)u32Value;
# ifdef IN_RING3
            VBOXSTRICTRC rcStrict2 = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
# else
            VBOXSTRICTRC rcStrict2 = iemMemCommitAndUnmapPostponeTroubleToR3(pVCpu, bUnmapInfo);
# endif
            if (rcStrict2 == VINF_SUCCESS)
            { /* likely */ }
            else
                AssertLogRelMsgFailedReturn(("rcStrict2=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict2)),
                                            RT_FAILURE(rcStrict2) ? rcStrict2 : VERR_IEM_IPE_1);

            pVCpu->cpum.GstCtx.ADDR_rDI = uAddrReg += cbIncr;
            pVCpu->cpum.GstCtx.ADDR_rCX = --uCounterReg;

            cLeftPage--;
            if (rcStrict != VINF_SUCCESS)
            {
                /** @todo finish: work out how this should work wrt status codes. Not sure we
                 * can use iemSetPassUpStatus here, but it depends on what
                 * iemRegAddToRipAndFinishingClearingRF may eventually return (if anything)... */
                rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                if (uCounterReg == 0)
                    rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
                pVCpu->iem.s.cPotentialExits++;
                return rcStrict;
            }

            IEM_CHECK_FF_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN(pVM, pVCpu, uCounterReg == 0);
        } while ((int32_t)cLeftPage > 0);


        /*
         * Next page.  Must check for interrupts and stuff here.
         */
        if (uCounterReg == 0)
            break;
        IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, pVCpu->cpum.GstCtx.eflags.u);
    }

    /*
     * Done.
     */
    pVCpu->iem.s.cPotentialExits++;
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'OUTS' (no rep)
 */
IEM_CIMPL_DEF_2(RT_CONCAT4(iemCImpl_outs_op,OP_SIZE,_addr,ADDR_SIZE), uint8_t, iEffSeg, bool, fIoChecked)
{
    PVMCC           pVM  = pVCpu->CTX_SUFF(pVM);
    VBOXSTRICTRC    rcStrict;

    /*
     * ASSUMES the #GP for I/O permission is taken first, then any #GP for
     * segmentation and finally any #PF due to virtual address translation.
     * ASSUMES nothing is read from the I/O port before traps are taken.
     */
    if (!fIoChecked)
    {
        rcStrict = iemHlpCheckPortIOPermission(pVCpu, pVCpu->cpum.GstCtx.dx, OP_SIZE / 8);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    /*
     * Check nested-guest I/O intercepts.
     */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        VMXEXITINSTRINFO ExitInstrInfo;
        ExitInstrInfo.u = 0;
        ExitInstrInfo.StrIo.u3AddrSize = ADDR_VMXSTRIO;
        ExitInstrInfo.StrIo.iSegReg    = iEffSeg;
        rcStrict = iemVmxVmexitInstrStrIo(pVCpu, VMXINSTRID_IO_OUTS, pVCpu->cpum.GstCtx.dx, OP_SIZE / 8, false /* fRep */,
                                          ExitInstrInfo, cbInstr);
        if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }
#endif

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_IOIO_PROT))
    {
        rcStrict = iemSvmHandleIOIntercept(pVCpu, pVCpu->cpum.GstCtx.dx, SVMIOIOTYPE_OUT, OP_SIZE / 8, ADDR_SIZE, iEffSeg,
                                           false /* fRep */, true /* fStrIo */, cbInstr);
        if (rcStrict == VINF_SVM_VMEXIT)
            return VINF_SUCCESS;
        if (rcStrict != VINF_SVM_INTERCEPT_NOT_ACTIVE)
        {
            Log(("iemCImpl_outs_op: iemSvmHandleIOIntercept failed (u16Port=%#x, cbReg=%u) rc=%Rrc\n", pVCpu->cpum.GstCtx.dx,
                 OP_SIZE / 8, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }
#endif

    OP_TYPE uValue;
    rcStrict = RT_CONCAT(iemMemFetchDataU,OP_SIZE)(pVCpu, &uValue, iEffSeg, pVCpu->cpum.GstCtx.ADDR_rSI);
    if (rcStrict == VINF_SUCCESS)
    {
        rcStrict = IOMIOPortWrite(pVM, pVCpu, pVCpu->cpum.GstCtx.dx, uValue, OP_SIZE / 8);
        if (IOM_SUCCESS(rcStrict))
        {
            if (!pVCpu->cpum.GstCtx.eflags.Bits.u1DF)
                pVCpu->cpum.GstCtx.ADDR_rSI += OP_SIZE / 8;
            else
                pVCpu->cpum.GstCtx.ADDR_rSI -= OP_SIZE / 8;
            /** @todo finish: work out how this should work wrt status codes. Not sure we
             * can use iemSetPassUpStatus here, but it depends on what
             * iemRegAddToRipAndFinishingClearingRF may eventually return (if anything)... */
            if (rcStrict != VINF_SUCCESS)
                rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
            rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            pVCpu->iem.s.cPotentialExits++;
        }
    }
    return rcStrict;
}


/**
 * Implements 'REP OUTS'.
 */
IEM_CIMPL_DEF_2(RT_CONCAT4(iemCImpl_rep_outs_op,OP_SIZE,_addr,ADDR_SIZE), uint8_t, iEffSeg, bool, fIoChecked)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

    /*
     * Setup.
     */
    uint16_t const  u16Port    = pVCpu->cpum.GstCtx.dx;
    VBOXSTRICTRC    rcStrict;
    if (!fIoChecked)
    {
/** @todo check if this is too early for ecx=0. */
        rcStrict = iemHlpCheckPortIOPermission(pVCpu, u16Port, OP_SIZE / 8);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    /*
     * Check nested-guest I/O intercepts.
     */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        VMXEXITINSTRINFO ExitInstrInfo;
        ExitInstrInfo.u = 0;
        ExitInstrInfo.StrIo.u3AddrSize = ADDR_VMXSTRIO;
        ExitInstrInfo.StrIo.iSegReg    = iEffSeg;
        rcStrict = iemVmxVmexitInstrStrIo(pVCpu, VMXINSTRID_IO_OUTS, pVCpu->cpum.GstCtx.dx, OP_SIZE / 8, true /* fRep */,
                                          ExitInstrInfo, cbInstr);
        if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }
#endif

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_IOIO_PROT))
    {
        rcStrict = iemSvmHandleIOIntercept(pVCpu, u16Port, SVMIOIOTYPE_OUT, OP_SIZE / 8, ADDR_SIZE, iEffSeg, true /* fRep */,
                                           true /* fStrIo */, cbInstr);
        if (rcStrict == VINF_SVM_VMEXIT)
            return VINF_SUCCESS;
        if (rcStrict != VINF_SVM_INTERCEPT_NOT_ACTIVE)
        {
            Log(("iemCImpl_rep_outs_op: iemSvmHandleIOIntercept failed (u16Port=%#x, cbReg=%u) rc=%Rrc\n", u16Port, OP_SIZE / 8,
                 VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }
#endif

    ADDR_TYPE       uCounterReg = pVCpu->cpum.GstCtx.ADDR_rCX;
    if (uCounterReg == 0)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iEffSeg) | CPUMCTX_EXTRN_ES);
    PCCPUMSELREGHID pHid      = iemSRegGetHid(pVCpu, iEffSeg);
    uint64_t        uBaseAddr = 0; /* gcc may not be used uninitialized */
    rcStrict = iemMemSegCheckReadAccessEx(pVCpu, pHid, iEffSeg, &uBaseAddr);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    int8_t const    cbIncr      = pVCpu->cpum.GstCtx.eflags.Bits.u1DF ? -(OP_SIZE / 8) : (OP_SIZE / 8);
    ADDR_TYPE       uAddrReg    = pVCpu->cpum.GstCtx.ADDR_rSI;

    /*
     * The loop.
     */
    for (;;)
    {
        /*
         * Do segmentation and virtual page stuff.
         */
        ADDR2_TYPE  uVirtAddr = uAddrReg + (ADDR2_TYPE)uBaseAddr;
        uint32_t    cLeftPage = (GUEST_PAGE_SIZE - (uVirtAddr & GUEST_PAGE_OFFSET_MASK)) / (OP_SIZE / 8);
        if (cLeftPage > uCounterReg)
            cLeftPage = uCounterReg;
        if (   cLeftPage > 0 /* can be null if unaligned, do one fallback round. */
            && cbIncr > 0    /** @todo Implement reverse direction string ops. */
            && (   IS_64_BIT_CODE(pVCpu)
                || (   uAddrReg < pHid->u32Limit
                    && uAddrReg + (cLeftPage * (OP_SIZE / 8)) <= pHid->u32Limit)
               )
           )
        {
            RTGCPHYS GCPhysMem;
            rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, uVirtAddr, OP_SIZE / 8, IEM_ACCESS_DATA_R, &GCPhysMem);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /*
             * If we can map the page without trouble, we use the IOM
             * string I/O interface to do the job.
             */
            PGMPAGEMAPLOCK PgLockMem;
            OP_TYPE const *puMem;
            rcStrict = iemMemPageMap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_R, (void **)&puMem, &PgLockMem);
            if (rcStrict == VINF_SUCCESS)
            {
                uint32_t cTransfers = cLeftPage;
                rcStrict = IOMIOPortWriteString(pVM, pVCpu, u16Port, puMem, &cTransfers, OP_SIZE / 8);

                uint32_t cActualTransfers = cLeftPage - cTransfers;
                Assert(cActualTransfers <= cLeftPage);
                pVCpu->cpum.GstCtx.ADDR_rSI = uAddrReg    += cbIncr * cActualTransfers;
                pVCpu->cpum.GstCtx.ADDR_rCX = uCounterReg -= cActualTransfers;
                puMem += cActualTransfers;

                iemMemPageUnmap(pVCpu, GCPhysMem, IEM_ACCESS_DATA_R, puMem, &PgLockMem);

                if (rcStrict != VINF_SUCCESS)
                {
                    if (IOM_SUCCESS(rcStrict))
                    {
                        /** @todo finish: work out how this should work wrt status codes. Not sure we
                         * can use iemSetPassUpStatus here, but it depends on what
                         * iemRegAddToRipAndFinishingClearingRF may eventually return (if anything)... */
                        rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                        if (uCounterReg == 0)
                            rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
                        pVCpu->iem.s.cPotentialExits++;
                    }
                    return rcStrict;
                }

                if (uCounterReg == 0)
                    break;

                /* If unaligned, we drop thru and do the page crossing access
                   below. Otherwise, do the next page. */
                if (!(uVirtAddr & (OP_SIZE / 8 - 1)))
                {
                    IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, pVCpu->cpum.GstCtx.eflags.u);
                    continue;
                }
                cLeftPage = 0;
            }
        }

        /*
         * Fallback - slow processing till the end of the current page.
         * In the cross page boundrary case we will end up here with cLeftPage
         * as 0, we execute one loop then.
         *
         * Note! We ASSUME the CPU will raise #PF or #GP before access the
         *       I/O port, otherwise it wouldn't really be restartable.
         */
        /** @todo investigate what the CPU actually does with \#PF/\#GP
         *        during INS. */
        do
        {
            OP_TYPE uValue;
            rcStrict = RT_CONCAT(iemMemFetchDataU,OP_SIZE)(pVCpu, &uValue, iEffSeg, uAddrReg);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            rcStrict = IOMIOPortWrite(pVM, pVCpu, u16Port, uValue, OP_SIZE / 8);
            if (IOM_SUCCESS(rcStrict))
            {
                pVCpu->cpum.GstCtx.ADDR_rSI = uAddrReg += cbIncr;
                pVCpu->cpum.GstCtx.ADDR_rCX = --uCounterReg;
                cLeftPage--;
            }
            if (rcStrict != VINF_SUCCESS)
            {
                if (IOM_SUCCESS(rcStrict))
                {
                    /** @todo finish: work out how this should work wrt status codes. Not sure we
                     * can use iemSetPassUpStatus here, but it depends on what
                     * iemRegAddToRipAndFinishingClearingRF may eventually return (if anything)... */
                    rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                    if (uCounterReg == 0)
                        iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
                    pVCpu->iem.s.cPotentialExits++;
                }
                return rcStrict;
            }
            IEM_CHECK_FF_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN(pVM, pVCpu, uCounterReg == 0);
        } while ((int32_t)cLeftPage > 0);


        /*
         * Next page.  Must check for interrupts and stuff here.
         */
        if (uCounterReg == 0)
            break;
        IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN(pVM, pVCpu, pVCpu->cpum.GstCtx.eflags.u);
    }

    /*
     * Done.
     */
    pVCpu->iem.s.cPotentialExits++;
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}

#endif /* OP_SIZE != 64-bit */


#undef OP_rAX
#undef OP_SIZE
#undef ADDR_SIZE
#undef ADDR_rDI
#undef ADDR_rSI
#undef ADDR_rCX
#undef ADDR_rIP
#undef ADDR2_TYPE
#undef ADDR_TYPE
#undef ADDR2_TYPE
#undef ADDR_VMXSTRIO
#undef IS_64_BIT_CODE
#undef IEM_CHECK_FF_YIELD_REPSTR_MAYBE_RETURN
#undef IEM_CHECK_FF_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN
#undef IEM_CHECK_FF_CPU_HIGH_PRIORITY_POST_REPSTR_MAYBE_RETURN

