/* $Id: IEMN8veRecompilerTlbLookup.h 107184 2024-11-26 22:32:44Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Native Recompiler TLB Lookup Code Emitter.
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

#ifndef VMM_INCLUDED_SRC_include_IEMN8veRecompilerTlbLookup_h
#define VMM_INCLUDED_SRC_include_IEMN8veRecompilerTlbLookup_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "IEMN8veRecompiler.h"
#include "IEMN8veRecompilerEmit.h"


/** @defgroup grp_iem_n8ve_re_tlblookup Native Recompiler TLB Lookup Code Emitter
 * @ingroup grp_iem_n8ve_re
 * @{
 */

/*
 * TLB Lookup config.
 */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_ARM64)
# define IEMNATIVE_WITH_TLB_LOOKUP
#endif
#ifdef IEMNATIVE_WITH_TLB_LOOKUP
# define IEMNATIVE_WITH_TLB_LOOKUP_FETCH
#endif
#ifdef IEMNATIVE_WITH_TLB_LOOKUP
# define IEMNATIVE_WITH_TLB_LOOKUP_STORE
#endif
#ifdef IEMNATIVE_WITH_TLB_LOOKUP
# define IEMNATIVE_WITH_TLB_LOOKUP_MAPPED
#endif
#ifdef IEMNATIVE_WITH_TLB_LOOKUP
# define IEMNATIVE_WITH_TLB_LOOKUP_PUSH
#endif
#ifdef IEMNATIVE_WITH_TLB_LOOKUP
# define IEMNATIVE_WITH_TLB_LOOKUP_POP
#endif


/**
 * This must be instantiate *before* branching off to the lookup code,
 * so that register spilling and whatnot happens for everyone.
 */
typedef struct IEMNATIVEEMITTLBSTATE
{
    bool const      fSkip;
    uint8_t const   idxRegPtrHlp;   /**< We don't support immediate variables with register assignment, so this a tmp reg alloc. */
    uint8_t const   idxRegPtr;
    uint8_t const   idxRegSegBase;
    uint8_t const   idxRegSegLimit;
    uint8_t const   idxRegSegAttrib;
    uint8_t const   idxReg1;
    uint8_t const   idxReg2;
#if defined(RT_ARCH_ARM64)
    uint8_t const   idxReg3;
/** @def IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
 * Use LDP and STDP to reduce number of instructions accessing memory at the
 * cost of using more registers.  This will typically reduce the number of
 * instructions emitted as well.
 * @todo Profile this and ensure that it performs the same or better.
 */
# define IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
# ifdef IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
    uint8_t const   idxReg4;
    uint8_t const   idxReg5;
# endif
#endif
    uint64_t const  uAbsPtr;

    IEMNATIVEEMITTLBSTATE(PIEMRECOMPILERSTATE a_pReNative, uint32_t *a_poff, uint8_t a_idxVarGCPtrMem,
                          uint8_t const a_iSegReg, bool const a_fFlat, uint8_t const a_cbMem, uint8_t const a_offDisp = 0)
#ifdef IEMNATIVE_WITH_TLB_LOOKUP
        /* 32-bit and 64-bit wraparound will require special handling, so skip these for absolute addresses. */
        :           fSkip(      a_pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(a_idxVarGCPtrMem)].enmKind
                             == kIemNativeVarKind_Immediate
                          &&   (  (a_pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) != IEMMODE_64BIT
                                ? (uint64_t)(UINT32_MAX - a_cbMem - a_offDisp)
                                : (uint64_t)(UINT64_MAX - a_cbMem - a_offDisp))
                             < a_pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(a_idxVarGCPtrMem)].u.uValue)
#else
        :           fSkip(true)
#endif
#if defined(RT_ARCH_AMD64) /* got good immediate encoding, otherwise we just load the address in a reg immediately. */
        ,    idxRegPtrHlp(UINT8_MAX)
#else
        ,    idxRegPtrHlp(      a_pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(a_idxVarGCPtrMem)].enmKind
                             != kIemNativeVarKind_Immediate
                          || fSkip
                          ? UINT8_MAX
                          : iemNativeRegAllocTmpImm(a_pReNative, a_poff,
                                                    a_pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(a_idxVarGCPtrMem)].u.uValue))
#endif
        ,       idxRegPtr(      a_pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(a_idxVarGCPtrMem)].enmKind
                             != kIemNativeVarKind_Immediate
                          && !fSkip
                          ? iemNativeVarRegisterAcquireInitedWithPref(a_pReNative, a_idxVarGCPtrMem, a_poff,
                                                                      IEMNATIVE_CALL_ARG2_GREG)
                          : idxRegPtrHlp)
        ,   idxRegSegBase(a_fFlat || a_iSegReg == UINT8_MAX || fSkip
                          ? UINT8_MAX
                          : iemNativeRegAllocTmpForGuestReg(a_pReNative, a_poff, IEMNATIVEGSTREG_SEG_BASE(a_iSegReg)))
        ,  idxRegSegLimit(a_fFlat || a_iSegReg == UINT8_MAX || (a_pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_64BIT || fSkip
                          ? UINT8_MAX
                          : iemNativeRegAllocTmpForGuestReg(a_pReNative, a_poff, IEMNATIVEGSTREG_SEG_LIMIT(a_iSegReg)))
        , idxRegSegAttrib(a_fFlat || a_iSegReg == UINT8_MAX || (a_pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_64BIT || fSkip
                          ? UINT8_MAX
                          : iemNativeRegAllocTmpForGuestReg(a_pReNative, a_poff, IEMNATIVEGSTREG_SEG_ATTRIB(a_iSegReg)))
        ,         idxReg1(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
        ,         idxReg2(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
#if defined(RT_ARCH_ARM64)
        ,         idxReg3(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
# ifdef IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
        ,         idxReg4(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
        ,         idxReg5(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
# endif
#endif
        ,         uAbsPtr(      a_pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(a_idxVarGCPtrMem)].enmKind
                             != kIemNativeVarKind_Immediate
                          || fSkip
                          ? UINT64_MAX
                          : a_pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(a_idxVarGCPtrMem)].u.uValue)

    {
        Assert(a_fFlat ? a_iSegReg == UINT8_MAX : a_iSegReg != UINT8_MAX);
        RT_NOREF(a_offDisp);
    }

    /* Alternative constructor for PUSH and POP where we don't have a GCPtrMem
       variable, only a register derived from the guest RSP. */
    IEMNATIVEEMITTLBSTATE(PIEMRECOMPILERSTATE a_pReNative, uint8_t a_idxRegPtr, uint32_t *a_poff,
                          uint8_t a_iSegReg, uint8_t a_cbMem)
#ifdef IEMNATIVE_WITH_TLB_LOOKUP
        :           fSkip(false)
#else
        :           fSkip(true)
#endif
        ,    idxRegPtrHlp(UINT8_MAX)
        ,       idxRegPtr(a_idxRegPtr)
        ,   idxRegSegBase(a_iSegReg == UINT8_MAX || fSkip
                          ? UINT8_MAX
                          : iemNativeRegAllocTmpForGuestReg(a_pReNative, a_poff, IEMNATIVEGSTREG_SEG_BASE(a_iSegReg)))
        ,  idxRegSegLimit((a_iSegReg == UINT8_MAX || (a_pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_64BIT) || fSkip
                          ? UINT8_MAX
                          : iemNativeRegAllocTmpForGuestReg(a_pReNative, a_poff, IEMNATIVEGSTREG_SEG_LIMIT(a_iSegReg)))
        , idxRegSegAttrib((a_iSegReg == UINT8_MAX || (a_pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_64BIT) || fSkip
                          ? UINT8_MAX
                          : iemNativeRegAllocTmpForGuestReg(a_pReNative, a_poff, IEMNATIVEGSTREG_SEG_ATTRIB(a_iSegReg)))
        ,         idxReg1(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
        ,         idxReg2(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
#if defined(RT_ARCH_ARM64)
        ,         idxReg3(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
# ifdef IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
        ,         idxReg4(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
        ,         idxReg5(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
# endif
#endif
        ,         uAbsPtr(UINT64_MAX)

    {
        RT_NOREF_PV(a_cbMem);
    }

    /* Alternative constructor for the code TLB lookups where we implictly use RIP
       variable, only a register derived from the guest RSP. */
    IEMNATIVEEMITTLBSTATE(PIEMRECOMPILERSTATE a_pReNative, bool a_fFlat, uint32_t *a_poff)
#ifdef IEMNATIVE_WITH_TLB_LOOKUP
        :           fSkip(false)
#else
        :           fSkip(true)
#endif
        ,    idxRegPtrHlp(UINT8_MAX)
        ,       idxRegPtr(iemNativeRegAllocTmpForGuestReg(a_pReNative, a_poff, kIemNativeGstReg_Pc))
        ,   idxRegSegBase(a_fFlat || fSkip
                          ? UINT8_MAX
                          : iemNativeRegAllocTmpForGuestReg(a_pReNative, a_poff, IEMNATIVEGSTREG_SEG_BASE(X86_SREG_CS)))
        ,  idxRegSegLimit(/*a_fFlat || fSkip
                          ? UINT8_MAX
                          : iemNativeRegAllocTmpForGuestReg(a_pReNative, a_poff, IEMNATIVEGSTREG_SEG_LIMIT(X86_SREG_CS))*/
                          UINT8_MAX)
        , idxRegSegAttrib(UINT8_MAX)
        ,         idxReg1(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
        ,         idxReg2(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
#if defined(RT_ARCH_ARM64)
        ,         idxReg3(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
# ifdef IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
        ,         idxReg4(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
        ,         idxReg5(!fSkip ? iemNativeRegAllocTmp(a_pReNative, a_poff) : UINT8_MAX)
# endif
#endif
        ,         uAbsPtr(UINT64_MAX)

    {
    }

    void freeRegsAndReleaseVars(PIEMRECOMPILERSTATE a_pReNative, uint8_t idxVarGCPtrMem = UINT8_MAX, bool fIsCode = false) const
    {
        if (!fIsCode)
        {
            if (idxRegPtr != UINT8_MAX)
            {
                if (idxRegPtrHlp == UINT8_MAX)
                {
                    if (idxVarGCPtrMem != UINT8_MAX)
                        iemNativeVarRegisterRelease(a_pReNative, idxVarGCPtrMem);
                }
                else
                {
                    Assert(idxRegPtrHlp == idxRegPtr);
                    iemNativeRegFreeTmpImm(a_pReNative, idxRegPtrHlp);
                }
            }
            else
                Assert(idxRegPtrHlp == UINT8_MAX);
        }
        else
        {
            Assert(idxVarGCPtrMem == UINT8_MAX);
            Assert(idxRegPtrHlp == UINT8_MAX);
            iemNativeRegFreeTmp(a_pReNative, idxRegPtr); /* RIP */
        }
        if (idxRegSegBase != UINT8_MAX)
            iemNativeRegFreeTmp(a_pReNative, idxRegSegBase);
        if (idxRegSegLimit != UINT8_MAX)
            iemNativeRegFreeTmp(a_pReNative, idxRegSegLimit);
        if (idxRegSegAttrib != UINT8_MAX)
            iemNativeRegFreeTmp(a_pReNative, idxRegSegAttrib);
#if defined(RT_ARCH_ARM64)
# ifdef IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
        iemNativeRegFreeTmp(a_pReNative, idxReg5);
        iemNativeRegFreeTmp(a_pReNative, idxReg4);
# endif
        iemNativeRegFreeTmp(a_pReNative, idxReg3);
#endif
        iemNativeRegFreeTmp(a_pReNative, idxReg2);
        iemNativeRegFreeTmp(a_pReNative, idxReg1);

    }

    uint32_t getRegsNotToSave() const
    {
        if (!fSkip)
            return RT_BIT_32(idxReg1)
                 | RT_BIT_32(idxReg2)
#if defined(RT_ARCH_ARM64)
                 | RT_BIT_32(idxReg3)
# ifdef IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
                 | RT_BIT_32(idxReg4)
                 | RT_BIT_32(idxReg5)
# endif
#endif
                 ;
        return 0;
    }

    /** This is only for avoid assertions. */
    uint32_t getActiveRegsWithShadows(bool fCode = false) const
    {
#ifdef VBOX_STRICT
        if (!fSkip)
            return (idxRegSegBase   != UINT8_MAX ? RT_BIT_32(idxRegSegBase)   : 0)
                 | (idxRegSegLimit  != UINT8_MAX ? RT_BIT_32(idxRegSegLimit)  : 0)
                 | (idxRegSegAttrib != UINT8_MAX ? RT_BIT_32(idxRegSegAttrib) : 0)
                 | (fCode                        ? RT_BIT_32(idxRegPtr)       : 0);
#else
        RT_NOREF_PV(fCode);
#endif
        return 0;
    }
} IEMNATIVEEMITTLBSTATE;

DECLASM(void) iemNativeHlpAsmSafeWrapCheckTlbLookup(void);


#ifdef IEMNATIVE_WITH_TLB_LOOKUP
/**
 *
 * @returns New @a off value.
 * @param   pReNative           .
 * @param   off                 .
 * @param   pTlbState           .
 * @param   iSegReg             .
 * @param   idxLabelTlbLookup   .
 * @param   idxLabelTlbMiss     .
 * @param   idxRegMemResult     .
 * @param   offDisp             .
 * @tparam  a_cbMem               .
 * @tparam  a_fAlignMaskAndCtl  The low 8-bit is the alignment mask, ie. a
 *                              128-bit aligned access passes 15.  This is only
 *                              applied to ring-3 code, when dictated by the
 *                              control bits and for atomic accesses.
 *
 *                              The other bits are used for alignment control:
 *                                  - IEM_MEMMAP_F_ALIGN_GP
 *                                  - IEM_MEMMAP_F_ALIGN_SSE
 *                                  - IEM_MEMMAP_F_ALIGN_GP_OR_AC
 *                              Any non-zero upper bits means we will go to
 *                              tlbmiss on anything out of alignment according
 *                              to the mask in the low 8 bits.
 * @tparam  a_fAccess            .
 * @tparam  a_fDataTlb          .
 * @tparam  a_fNoReturn         .
 */
template<bool const a_fDataTlb, const uint8_t a_cbMem, uint32_t a_fAlignMaskAndCtl, uint32_t a_fAccess,
         bool const a_fNoReturn = false>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTlbLookup(PIEMRECOMPILERSTATE pReNative, uint32_t off, IEMNATIVEEMITTLBSTATE const * const pTlbState,
                       uint8_t iSegReg, uint32_t idxLabelTlbLookup, uint32_t idxLabelTlbMiss, uint8_t idxRegMemResult,
                       uint8_t offDisp = 0)
{
    Assert(!pTlbState->fSkip);
    uint32_t const   offVCpuTlb = a_fDataTlb ? RT_UOFFSETOF(VMCPUCC, iem.s.DataTlb) : RT_UOFFSETOF(VMCPUCC, iem.s.CodeTlb);
# if defined(RT_ARCH_AMD64)
    uint8_t * const  pCodeBuf   = iemNativeInstrBufEnsure(pReNative, off, 512);
# elif defined(RT_ARCH_ARM64)
    uint32_t * const pCodeBuf   = iemNativeInstrBufEnsure(pReNative, off, 96);
# endif

    /*
     * The expand down check isn't use all that much, so we emit here to keep
     * the lookup straighter.
     */
    /* check_expand_down: ; complicted! */
    uint32_t const offCheckExpandDown = off;
    uint32_t       offFixupLimitDone  = 0;
    if (a_fDataTlb && iSegReg != UINT8_MAX && (pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) != IEMMODE_64BIT)
    {
        /* cmp  seglim, regptr */
        if (pTlbState->idxRegPtr != UINT8_MAX && offDisp == 0)
            off = iemNativeEmitCmpGpr32WithGprEx(pCodeBuf, off, pTlbState->idxRegSegLimit, pTlbState->idxRegPtr);
        else if (pTlbState->idxRegPtr == UINT8_MAX)
            off = iemNativeEmitCmpGpr32WithImmEx(pCodeBuf, off, pTlbState->idxRegSegLimit,
                                                 (uint32_t)(pTlbState->uAbsPtr + offDisp));
        else if RT_CONSTEXPR_IF(a_cbMem == 1)
            off = iemNativeEmitCmpGpr32WithGprEx(pCodeBuf, off, pTlbState->idxRegSegLimit, pTlbState->idxReg2);
        else
        {   /* use idxRegMemResult to calc the displaced address. */
            off = iemNativeEmitGpr32EqGprPlusImmEx(pCodeBuf, off, idxRegMemResult, pTlbState->idxRegPtr, offDisp);
            off = iemNativeEmitCmpGpr32WithGprEx(pCodeBuf, off, pTlbState->idxRegSegLimit, idxRegMemResult);
        }
        /* ja  tlbmiss */
        off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss, kIemNativeInstrCond_nbe);

        /* reg1 = segattr & X86DESCATTR_D (0x4000) */
        off = iemNativeEmitGpr32EqGprAndImmEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxRegSegAttrib, X86DESCATTR_D);
        /* xor  reg1, X86DESCATTR_D */
        off = iemNativeEmitXorGpr32ByImmEx(pCodeBuf, off, pTlbState->idxReg1, X86DESCATTR_D);
        /* shl  reg1, 2 (16 - 14) */
        AssertCompile((X86DESCATTR_D << 2) == UINT32_C(0x10000));
        off = iemNativeEmitShiftGpr32LeftEx(pCodeBuf, off, pTlbState->idxReg1, 2);
        /* dec  reg1 (=> 0xffff if D=0; 0xffffffff if D=1) */
        off = iemNativeEmitSubGpr32ImmEx(pCodeBuf, off, pTlbState->idxReg1, 1);
        /* cmp  reg1, reg2 (64-bit) / imm (32-bit) */
        if (pTlbState->idxRegPtr != UINT8_MAX)
            off = iemNativeEmitCmpGprWithGprEx(pCodeBuf, off, pTlbState->idxReg1,
                                               a_cbMem > 1 || offDisp != 0 ? pTlbState->idxReg2 : pTlbState->idxRegPtr);
        else
            off = iemNativeEmitCmpGpr32WithImmEx(pCodeBuf, off, pTlbState->idxReg1,
                                                 (uint32_t)(pTlbState->uAbsPtr + offDisp + a_cbMem - 1)); /* fSkip=true on overflow. */
        /* jbe  tlbmiss */
        off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss, kIemNativeInstrCond_be);
        /* jmp  limitdone */
        offFixupLimitDone = off;
        off = iemNativeEmitJmpToFixedEx(pCodeBuf, off, off + 256 /* force near */);
    }

    /*
     * Snippet for checking whether misaligned accesses are within the
     * page (see step 2).
     *
     * This sequence is 1 instruction longer than the strict alignment test,
     * and since most accesses are correctly aligned it is better to do it
     * this way.  Runs of r163597 seems to indicate there was a regression
     * when placing this code in the main code flow.
     */
    uint8_t const idxRegFlatPtr = iSegReg != UINT8_MAX || pTlbState->idxRegPtr == UINT8_MAX || offDisp != 0
                                ? idxRegMemResult : pTlbState->idxRegPtr; /* (not immediately ready for tlblookup use) */
    RT_CONSTEXPR
    uint8_t const fAlignMask    = a_fDataTlb ? (uint8_t)(a_fAlignMaskAndCtl & 0xff) : 0;
    if (a_fDataTlb)
    {
        AssertCompile(!(a_fAlignMaskAndCtl & ~(UINT32_C(0xff) | IEM_MEMMAP_F_ALIGN_SSE | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_GP_OR_AC)));
        Assert(RT_IS_POWER_OF_TWO(fAlignMask + 1U));
        Assert(a_cbMem == fAlignMask + 1U || !(a_fAccess & IEM_ACCESS_ATOMIC));
        Assert(a_cbMem < 128); /* alignment test assumptions */
    }

    uint32_t offMisalignedAccess             = UINT32_MAX;
    uint32_t offFixupMisalignedAccessJmpBack = UINT32_MAX;
    if (   a_fDataTlb
        && !(a_fAlignMaskAndCtl & ~UINT32_C(0xff))
        && !(a_fAccess & IEM_ACCESS_ATOMIC)
        && a_cbMem > 1
        && RT_IS_POWER_OF_TWO(a_cbMem)
        && !(pReNative->fExec & IEM_F_X86_AC))
    {
        /* tlbmisaligned: */
        offMisalignedAccess = off;
        /* reg1 = regflat & 0xfff */
        off = iemNativeEmitGpr32EqGprAndImmEx(pCodeBuf, off, pTlbState->idxReg1,/*=*/ idxRegFlatPtr,/*&*/ GUEST_PAGE_OFFSET_MASK);
        /* cmp reg1, GUEST_PAGE_SIZE - a_cbMem */
        off = iemNativeEmitCmpGpr32WithImmEx(pCodeBuf, off, pTlbState->idxReg1, GUEST_PAGE_SIZE - a_cbMem);
        /* jbe short jmpback */
        offFixupMisalignedAccessJmpBack = off;
        off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off + 256 /*near*/, kIemNativeInstrCond_be);
# ifdef IEM_WITH_TLB_STATISTICS
        off = iemNativeEmitIncU32CounterInVCpuEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg2,
                                                 offVCpuTlb + RT_UOFFSETOF(IEMTLB, cTlbNativeMissCrossPage));
# endif
        off = iemNativeEmitJmpToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss);
    }

    /* The ODD TLB entry is checked last when CR4.PGE=0 or when not in ring-0. */
    bool const fEvenFirst       = (pReNative->fExec & IEM_F_X86_CPL_MASK) != 0
                               || !(pReNative->pVCpu->cpum.GstCtx.cr4 & X86_CR4_PGE);
    bool const fIncCheckAltTlbe = (pReNative->fExec & IEM_F_X86_CPL_MASK) == 0;

    /*
     * Snippet for checking the alternative TLBE entry when CR4.PGE=1 and
     * for doing statistics.
     *
     * This code assists step 3c, so look down there for register assignments.
     */
    /* checkalttlbe_and_missedtagstats: */
    uint32_t const offCheckAltTlbeAndMissedTagStats = off;
    uint32_t       offFixupCheckAltTlbeJmpBack      = UINT32_MAX / 2;
    if (fIncCheckAltTlbe)
    {
# ifdef RT_ARCH_AMD64
        /* Update pTlbe: reg2 = fEvenFirst ? reg2 + sizeof(IEMTLBENTRY) : reg2 - sizeof(IEMTLBENTRY); */
        pCodeBuf[off++] = X86_OP_REX_W | (pTlbState->idxReg2 < 8 ? 0 : X86_OP_REX_R | X86_OP_REX_B);
        pCodeBuf[off++] = 0x8d; /* LEA r64,m64 */
        off = iemNativeEmitGprByGprDisp(pCodeBuf, off, pTlbState->idxReg2, pTlbState->idxReg2,
                                        fEvenFirst ? (int32_t)sizeof(IEMTLBENTRY) : -(int32_t)sizeof(IEMTLBENTRY));

        /* reg1 = reg1 & ~IEMTLB_REVISION_MASK; */
        off = iemNativeEmitShiftGprLeftEx(pCodeBuf, off, pTlbState->idxReg1, 16 + GUEST_PAGE_SHIFT);
        off = iemNativeEmitShiftGprRightEx(pCodeBuf, off, pTlbState->idxReg1, 16 + GUEST_PAGE_SHIFT);
        /* or  reg1, [qword pVCpu->iem.s.DataTlb.uTlbRevisionGlobal/uTlbRevision] */
        pCodeBuf[off++] = pTlbState->idxReg1 < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_R;
        pCodeBuf[off++] = 0x0b; /* OR r64,r/m64 */
        off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, pTlbState->idxReg1,
                                         fEvenFirst ? offVCpuTlb + RT_UOFFSETOF(IEMTLB, uTlbRevisionGlobal)
                                         :            offVCpuTlb + RT_UOFFSETOF(IEMTLB, uTlbRevision));

        /* cmp reg1, [reg2] */
        pCodeBuf[off++] = X86_OP_REX_W | (pTlbState->idxReg1 < 8 ? 0 : X86_OP_REX_R) | (pTlbState->idxReg2 < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x3b;
        off = iemNativeEmitGprByGprDisp(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg2, RT_UOFFSETOF(IEMTLBENTRY, uTag));

# elif defined(RT_ARCH_ARM64)
        /* reg3 = uTlbRevision/uTlbRevisionGlobal; (We've ditched reg4 already, so have to get it via pVCpu.) */
        off = iemNativeEmitLoadGprFromVCpuU64Ex(pCodeBuf, off, pTlbState->idxReg3,
                                                fEvenFirst ? offVCpuTlb + RT_UOFFSETOF(IEMTLB, uTlbRevisionGlobal)
                                                :            offVCpuTlb + RT_UOFFSETOF(IEMTLB, uTlbRevision));

        /* reg1 = reg1 & ~IEMTLB_REVISION_MASK; */
        AssertCompile(UINT64_C(0x0000000fffffffff) == ~IEMTLB_REVISION_MASK);
        Assert(Armv8A64ConvertImmRImmS2Mask64(0x63, 0) == ~IEMTLB_REVISION_MASK);
        pCodeBuf[off++] = Armv8A64MkInstrAndImm(pTlbState->idxReg1, pTlbState->idxReg1, 0x63, 0);

        /* reg1 |= reg3 (uTlbRevision/uTlbRevisionGlobal); */
        pCodeBuf[off++] = Armv8A64MkInstrOrr(pTlbState->idxReg1, pTlbState->idxReg1, pTlbState->idxReg3);

        /* reg2 = reg2 +/- sizeof(IEMTLBENTRY); via preindexing.
           reg3 = uTag; [pair: reg4 = fFlagsAndPhysRev;] */
        AssertCompileMemberOffset(IEMTLBENTRY, uTag, 0);
#  ifdef IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
        AssertCompileAdjacentMembers(IEMTLBENTRY, uTag, fFlagsAndPhysRev);
        pCodeBuf[off++] = Armv8A64MkInstrLdPairGpr(pTlbState->idxReg3, pTlbState->idxReg4, pTlbState->idxReg2,
                                                   fEvenFirst ? (int)sizeof(IEMTLBENTRY) / 8 : -(int)sizeof(IEMTLBENTRY) / 8,
                                                   kArm64InstrStLdPairType_PreIndex);
#  else
        pCodeBuf[off++] = Armv8A64MkInstrStrLdrPreIndex9(kArmv8A64InstrLdStType_Ld_Dword, pTlbState->idxReg3, pTlbState->idxReg2,
                                                         fEvenFirst ? (int)sizeof(IEMTLBENTRY) / 8 : -(int)sizeof(IEMTLBENTRY) / 8);
#  endif
        /* cmp reg1, reg3; (uRev | Hash(FlatPtr), IEMTLBENTRY::uTag)*/
        off = iemNativeEmitCmpGprWithGprEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg3);

# else
#  error "portme"
# endif
        /* je  near jumpback_checkalttlbe */
        offFixupCheckAltTlbeJmpBack = off;
        off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off + 256, kIemNativeInstrCond_e);
    }

# ifdef IEM_WITH_TLB_STATISTICS
    /* inc stat */
    off = iemNativeEmitIncStamCounterInVCpuEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg2,
                                              offVCpuTlb + RT_UOFFSETOF(IEMTLB, cTlbNativeMissTag));
# endif
# ifndef IEM_WITH_TLB_STATISTICS
    if (fIncCheckAltTlbe)
# endif
        off = iemNativeEmitJmpToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss);
    off = iemNativeEmitBrkEx(pCodeBuf, off, 0x7679);

    /*
     * tlblookup:
     */
    iemNativeLabelDefine(pReNative, idxLabelTlbLookup, off);
# if defined(RT_ARCH_ARM64) && 0
    off = iemNativeEmitBrkEx(pCodeBuf, off, 0);
# endif

    /*
     * 1. Segmentation.
     *
     * 1a. Check segment limit and attributes if non-flat 32-bit code.  This is complicated.
     *
     *     This can be skipped for code TLB lookups because limit is checked by jmp, call,
     *     ret, and iret prior to making it.  It is also checked by the helpers prior to
     *     doing TLB loading.
     */
    if (a_fDataTlb && iSegReg != UINT8_MAX && (pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) != IEMMODE_64BIT)
    {
        /* Check that we've got a segment loaded and that it allows the access.
           For write access this means a writable data segment.
           For read-only accesses this means a readable code segment or any data segment. */
        if RT_CONSTEXPR_IF((a_fAccess & IEM_ACCESS_TYPE_WRITE) != 0)
        {
            uint32_t const fMustBe1 = X86DESCATTR_P        | X86DESCATTR_DT    | X86_SEL_TYPE_WRITE;
            uint32_t const fMustBe0 = X86DESCATTR_UNUSABLE | X86_SEL_TYPE_CODE;
            /* reg1 = segattrs & (must1|must0) */
            off = iemNativeEmitGpr32EqGprAndImmEx(pCodeBuf, off, pTlbState->idxReg1,
                                                  pTlbState->idxRegSegAttrib, fMustBe1 | fMustBe0);
            /* cmp reg1, must1 */
            AssertCompile(fMustBe1 <= UINT16_MAX);
            off = iemNativeEmitCmpGpr32WithImmEx(pCodeBuf, off, pTlbState->idxReg1, fMustBe1);
            /* jne tlbmiss */
            off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss, kIemNativeInstrCond_ne);
        }
        else
        {
            /*  U  | !P |!DT |!CD | RW |
                16 |  8 |  4 |  3 |  1 |
              -------------------------------
                0  |  0 |  0 |  0 |  0 | execute-only code segment. - must be excluded
                0  |  0 |  0 |  0 |  1 | execute-read code segment.
                0  |  0 |  0 |  1 |  0 | read-only data segment.
                0  |  0 |  0 |  1 |  1 | read-write data segment.   - last valid combination
            */
            /* reg1 = segattrs & (relevant attributes) */
            off = iemNativeEmitGpr32EqGprAndImmEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxRegSegAttrib,
                                                    X86DESCATTR_UNUSABLE | X86DESCATTR_P | X86DESCATTR_DT
                                                  | X86_SEL_TYPE_CODE    | X86_SEL_TYPE_WRITE);
            /* xor reg1, X86DESCATTR_P | X86DESCATTR_DT | X86_SEL_TYPE_CODE ; place C=1 RW=0 at the bottom & limit the range.
                                            ; EO-code=0,  ER-code=2, RO-data=8, RW-data=10 */
#ifdef RT_ARCH_ARM64
            off = iemNativeEmitXorGpr32ByImmEx(pCodeBuf, off, pTlbState->idxReg1, X86DESCATTR_DT | X86_SEL_TYPE_CODE);
            off = iemNativeEmitXorGpr32ByImmEx(pCodeBuf, off, pTlbState->idxReg1, X86DESCATTR_P);
#else
            off = iemNativeEmitXorGpr32ByImmEx(pCodeBuf, off, pTlbState->idxReg1,
                                               X86DESCATTR_P | X86DESCATTR_DT | X86_SEL_TYPE_CODE);
#endif
            /* sub reg1, X86_SEL_TYPE_WRITE ; EO-code=-2, ER-code=0, RO-data=6, RW-data=8 */
            off = iemNativeEmitSubGpr32ImmEx(pCodeBuf, off, pTlbState->idxReg1, X86_SEL_TYPE_WRITE /* ER-code */);
            /* cmp reg1, X86_SEL_TYPE_CODE | X86_SEL_TYPE_WRITE */
            AssertCompile(X86_SEL_TYPE_CODE == 8);
            off = iemNativeEmitCmpGpr32WithImmEx(pCodeBuf, off, pTlbState->idxReg1, X86_SEL_TYPE_CODE);
            /* ja  tlbmiss */
            off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss, kIemNativeInstrCond_nbe);
        }

        /* If we're accessing more than one byte or if we're working with a non-zero offDisp,
           put the last address we'll be accessing in idxReg2 (64-bit). */
        if ((a_cbMem > 1 || offDisp != 0) && pTlbState->idxRegPtr != UINT8_MAX)
        {
            if (!offDisp)
                /* reg2 = regptr + a_cbMem - 1; 64-bit result so we can fend of wraparounds/overflows. */
                off = iemNativeEmitGprEqGprPlusImmEx(pCodeBuf, off,
                                                     pTlbState->idxReg2,/*=*/ pTlbState->idxRegPtr,/*+*/ a_cbMem - 1);
            else
            {
                /* reg2 = (uint32_t)(regptr + offDisp) + a_cbMem - 1;. */
                off = iemNativeEmitGpr32EqGprPlusImmEx(pCodeBuf, off,
                                                       pTlbState->idxReg2,/*=*/ pTlbState->idxRegPtr,/*+*/ + offDisp);
                off = iemNativeEmitAddGprImmEx(pCodeBuf, off, pTlbState->idxReg2, a_cbMem - 1);
            }
        }

        /*
         * Check the limit.  If this is a write access, we know that it's a
         * data segment and includes the expand_down bit.  For read-only accesses
         * we need to check that code/data=0 and expanddown=1 before continuing.
         */
        if RT_CONSTEXPR_IF((a_fAccess & IEM_ACCESS_TYPE_WRITE) != 0)
        {
            /* test segattrs, X86_SEL_TYPE_DOWN */
            AssertCompile(X86_SEL_TYPE_DOWN < 128);
            off = iemNativeEmitTestAnyBitsInGpr8Ex(pCodeBuf, off, pTlbState->idxRegSegAttrib, X86_SEL_TYPE_DOWN);
            /* jnz  check_expand_down */
            off = iemNativeEmitJccToFixedEx(pCodeBuf, off, offCheckExpandDown, kIemNativeInstrCond_ne);
        }
        else
        {
            /* reg1 = segattr & (code | down) */
            off = iemNativeEmitGpr32EqGprAndImmEx(pCodeBuf, off, pTlbState->idxReg1,
                                                  pTlbState->idxRegSegAttrib, X86_SEL_TYPE_CODE | X86_SEL_TYPE_DOWN);
            /* cmp reg1, down */
            off = iemNativeEmitCmpGpr32WithImmEx(pCodeBuf, off, pTlbState->idxReg1, X86_SEL_TYPE_DOWN);
            /* je check_expand_down */
            off = iemNativeEmitJccToFixedEx(pCodeBuf, off, offCheckExpandDown, kIemNativeInstrCond_e);
        }

        /* expand_up:
           cmp  seglim, regptr/reg2/imm */
        if (pTlbState->idxRegPtr != UINT8_MAX)
            off = iemNativeEmitCmpGprWithGprEx(pCodeBuf, off, pTlbState->idxRegSegLimit,
                                               a_cbMem > 1 || offDisp != 0 ? pTlbState->idxReg2 : pTlbState->idxRegPtr);
        else
            off = iemNativeEmitCmpGpr32WithImmEx(pCodeBuf, off, pTlbState->idxRegSegLimit,
                                                 (uint32_t)pTlbState->uAbsPtr + offDisp + a_cbMem - 1U); /* fSkip=true on overflow. */
        /* jbe  tlbmiss */
        off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss, kIemNativeInstrCond_be);

        /* limitdone: */
        iemNativeFixupFixedJump(pReNative, offFixupLimitDone, off);
    }

    /* 1b. Add the segment base.  We use idxRegMemResult for the ptr register if
           this step is required or if the address is a constant (simplicity) or
           if offDisp is non-zero. */
    if (iSegReg != UINT8_MAX)
    {
        Assert(idxRegFlatPtr != pTlbState->idxRegPtr);
        /* regflat = segbase + regptr/imm */
        if ((pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT)
        {
            Assert(iSegReg >= X86_SREG_FS);
            if (pTlbState->idxRegPtr != UINT8_MAX)
            {
                off = iemNativeEmitGprEqGprPlusGprEx(pCodeBuf, off, idxRegFlatPtr, pTlbState->idxRegSegBase, pTlbState->idxRegPtr);
                if (offDisp != 0)
                    off = iemNativeEmitAddGprImmEx(pCodeBuf, off, idxRegFlatPtr, offDisp);
            }
            else
                off = iemNativeEmitGprEqGprPlusImmEx(pCodeBuf, off, idxRegFlatPtr, pTlbState->idxRegSegBase,
                                                     pTlbState->uAbsPtr + offDisp);
        }
        else if (pTlbState->idxRegPtr != UINT8_MAX)
        {
            off = iemNativeEmitGpr32EqGprPlusGprEx(pCodeBuf, off, idxRegFlatPtr, pTlbState->idxRegSegBase, pTlbState->idxRegPtr);
            if (offDisp != 0)
                off = iemNativeEmitAddGpr32ImmEx(pCodeBuf, off, idxRegFlatPtr, offDisp);
        }
        else
            off = iemNativeEmitGpr32EqGprPlusImmEx(pCodeBuf, off, idxRegFlatPtr,
                                                   pTlbState->idxRegSegBase, (uint32_t)pTlbState->uAbsPtr + offDisp);
    }
    else if (pTlbState->idxRegPtr == UINT8_MAX)
    {
        if ((pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT)
            off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, idxRegFlatPtr, pTlbState->uAbsPtr + offDisp);
        else
            off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, idxRegFlatPtr, (uint32_t)pTlbState->uAbsPtr + offDisp);
    }
    else if (offDisp != 0)
    {
        Assert(idxRegFlatPtr != pTlbState->idxRegPtr);
        if ((pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT)
            off = iemNativeEmitGprEqGprPlusImmEx(pCodeBuf, off, idxRegFlatPtr, pTlbState->idxRegPtr, offDisp);
        else
            off = iemNativeEmitGpr32EqGprPlusImmEx(pCodeBuf, off, idxRegFlatPtr, pTlbState->idxRegPtr, offDisp);
    }
    else
        Assert(idxRegFlatPtr == pTlbState->idxRegPtr);

    /*
     * 2. Check that the address doesn't cross a page boundrary and doesn't
     *    have alignment issues (not applicable to code).
     *
     *    For regular accesses (non-SSE/AVX & atomic stuff) we only need to
     *    check for #AC in ring-3 code.  To simplify this, the need for AC
     *    checking is indicated by IEM_F_X86_AC in IEMCPU::fExec.
     *
     *    The caller informs us about about SSE/AVX aligned accesses via the
     *    upper bits of a_fAlignMaskAndCtl and atomic accesses via a_fAccess.
     */
    if (a_fDataTlb)
    {
        if (offMisalignedAccess != UINT32_MAX)
        {
#ifdef RT_ARCH_ARM64
            if RT_CONSTEXPR_IF(a_cbMem == 2)
            {
                /* tbnz regflatptr, #0, tlbmiss */
                pCodeBuf[off++] = Armv8A64MkInstrTbnz((int32_t)offMisalignedAccess - (int32_t)off, idxRegFlatPtr, 0);
            }
            else
#endif
            {
                /* test regflat, fAlignMask */
                off = iemNativeEmitTestAnyBitsInGpr8Ex(pCodeBuf, off, idxRegFlatPtr, a_cbMem - 1);
                /* jnz tlbmiss */
                off = iemNativeEmitJccToFixedEx(pCodeBuf, off, offMisalignedAccess, kIemNativeInstrCond_ne);
            }
            /** @todo ARM64: two byte access checks can be reduced to single instruction */
            iemNativeFixupFixedJump(pReNative, offFixupMisalignedAccessJmpBack, off);
        }
        else
        {
            /*
             * 2a. Strict alignment check using fAlignMask for atomic, strictly
             *     aligned stuff (SSE & AVX) and AC=1 (ring-3).
             */
            bool const fStrictAlignmentCheck = fAlignMask
                                            && (   (a_fAlignMaskAndCtl & ~UINT32_C(0xff))
                                                || (a_fAccess & IEM_ACCESS_ATOMIC)
                                                || (pReNative->fExec & IEM_F_X86_AC) );
            if (fStrictAlignmentCheck)
            {
                /* test regflat, fAlignMask */
                off = iemNativeEmitTestAnyBitsInGpr8Ex(pCodeBuf, off, idxRegFlatPtr, fAlignMask);

#ifndef IEM_WITH_TLB_STATISTICS
                /* jnz tlbmiss */
                off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss, kIemNativeInstrCond_ne);
#else
                /* jz  1F; inc stat; jmp tlbmiss */
                uint32_t const offFixup1 = off;
                off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off + 16, kIemNativeInstrCond_e);
                off = iemNativeEmitIncStamCounterInVCpuEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg2,
                                                          offVCpuTlb + RT_UOFFSETOF(IEMTLB, cTlbNativeMissAlignment));
                off = iemNativeEmitJmpToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss);
                iemNativeFixupFixedJump(pReNative, offFixup1, off);
#endif
            }

            /*
             * 2b. Check that it's not crossing page a boundrary if the access is
             *     larger than the aligment mask or if we didn't do the strict
             *     alignment check above.
             */
            if (   a_cbMem > 1
                && (   !fStrictAlignmentCheck
                    || a_cbMem > fAlignMask + 1U))
            {
                /* reg1 = regflat & 0xfff */
                off = iemNativeEmitGpr32EqGprAndImmEx(pCodeBuf, off, pTlbState->idxReg1,/*=*/ idxRegFlatPtr,/*&*/ GUEST_PAGE_OFFSET_MASK);
                /* cmp reg1, GUEST_PAGE_SIZE - a_cbMem */
                off = iemNativeEmitCmpGpr32WithImmEx(pCodeBuf, off, pTlbState->idxReg1, GUEST_PAGE_SIZE - a_cbMem);
#ifndef IEM_WITH_TLB_STATISTICS
                /* ja  tlbmiss */
                off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss, kIemNativeInstrCond_nbe);
#else
                /* jbe 1F; inc stat; jmp tlbmiss */
                uint32_t const offFixup1 = off;
                off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off + 16, kIemNativeInstrCond_be);
                off = iemNativeEmitIncU32CounterInVCpuEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg2,
                                                         offVCpuTlb + RT_UOFFSETOF(IEMTLB, cTlbNativeMissCrossPage));
                off = iemNativeEmitJmpToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss);
                iemNativeFixupFixedJump(pReNative, offFixup1, off);
#endif
            }
        }
    }
    else
        Assert(a_fAlignMaskAndCtl == 0);

    /*
     * 3. TLB lookup.
     *
     * 3a. Calculate the TLB tag value (IEMTLB_CALC_TAG_NO_REV).
     *     In 64-bit mode we will also check for non-canonical addresses here.
     */
    if ((pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT)
    {
# if defined(RT_ARCH_AMD64)
        /* mov reg1, regflat */
        off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, pTlbState->idxReg1, idxRegFlatPtr);
        /* rol reg1, 16 */
        off = iemNativeEmitRotateGprLeftEx(pCodeBuf, off, pTlbState->idxReg1, 16);
        /** @todo Would 'movsx reg2, word reg1' and working on reg2 in dwords be faster? */
        /* inc word reg1 */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        if (pTlbState->idxReg1 >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, pTlbState->idxReg1 & 7);
        /* cmp word reg1, 1 */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        if (pTlbState->idxReg1 >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, pTlbState->idxReg1 & 7);
        pCodeBuf[off++] = 1;
#  ifndef IEM_WITH_TLB_STATISTICS
        /* ja  tlbmiss */
        off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss, kIemNativeInstrCond_nbe);
#  else
        /* jbe 1F; inc stat; jmp tlbmiss */
        uint32_t const offFixup1 = off;
        off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off + 16, kIemNativeInstrCond_be);
        off = iemNativeEmitIncU32CounterInVCpuEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg2,
                                                 offVCpuTlb + RT_UOFFSETOF(IEMTLB, cTlbNativeMissNonCanonical));
        off = iemNativeEmitJmpToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss);
        iemNativeFixupFixedJump(pReNative, offFixup1, off);
#  endif
        /* shr reg1, 16 + GUEST_PAGE_SHIFT */
        off = iemNativeEmitShiftGprRightEx(pCodeBuf, off, pTlbState->idxReg1, 16 + GUEST_PAGE_SHIFT);

# elif defined(RT_ARCH_ARM64)
        /* lsr  reg1, regflat, #48 */
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(pTlbState->idxReg1, idxRegFlatPtr, 48);
        /* add  reg1, reg1, #1 */
        pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(pTlbState->idxReg1, pTlbState->idxReg1, 1, false /*f64Bit*/);
        /* tst  reg1, #0xfffe */
        Assert(Armv8A64ConvertImmRImmS2Mask32(14, 31) == 0xfffe);
        pCodeBuf[off++] = Armv8A64MkInstrTstImm(pTlbState->idxReg1, 14, 31, false /*f64Bit*/);
#  ifndef IEM_WITH_TLB_STATISTICS
        /* b.ne tlbmiss */
        off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss, kIemNativeInstrCond_ne);
#  else
        /* b.eq 1F; inc stat; jmp tlbmiss */
        uint32_t const offFixup1 = off;
        off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off + 16, kIemNativeInstrCond_e);
        off = iemNativeEmitIncU32CounterInVCpuEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg2,
                                                 offVCpuTlb + RT_UOFFSETOF(IEMTLB, cTlbNativeMissNonCanonical));
        off = iemNativeEmitJmpToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss);
        iemNativeFixupFixedJump(pReNative, offFixup1, off);
#  endif

        /* ubfx reg1, regflat, #12, #36 */
        pCodeBuf[off++] = Armv8A64MkInstrUbfx(pTlbState->idxReg1, idxRegFlatPtr, GUEST_PAGE_SHIFT, 48 - GUEST_PAGE_SHIFT);
# else
#  error "Port me"
# endif
    }
    else
    {
        /* reg1 = (uint32_t)(regflat >> 12) */
        off = iemNativeEmitGpr32EqGprShiftRightImmEx(pCodeBuf, off, pTlbState->idxReg1, idxRegFlatPtr, GUEST_PAGE_SHIFT);
    }

    /* or  reg1, [qword pVCpu->iem.s.DataTlb.uTlbRevision] */
# if defined(RT_ARCH_AMD64)
    pCodeBuf[off++] = pTlbState->idxReg1 < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_R;
    pCodeBuf[off++] = 0x0b; /* OR r64,r/m64 */
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, pTlbState->idxReg1,
                                     fEvenFirst ? offVCpuTlb + RT_UOFFSETOF(IEMTLB, uTlbRevision)
                                     :            offVCpuTlb + RT_UOFFSETOF(IEMTLB, uTlbRevisionGlobal));
# else
#  ifdef IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
    /* Load uTlbRevision[Global] into reg3 and uTlbPhysRev into reg5.
       We load the pointer for IEMTLB::aEntries[!fEvenFirst] into reg4 and use
       it for addressing here and later when calculating pTble (saves one
       instruction, simplifies odd-first). */
    AssertCompileMemberAlignment(IEMTLB, uTlbRevision, 16); /* It is said that misaligned pair loads doesn't perform well. */
    AssertCompileAdjacentMembers(IEMTLB, uTlbRevision, uTlbPhysRev);
    AssertCompileAdjacentMembers(IEMTLB, uTlbPhysRev, uTlbRevisionGlobal);
    AssertCompile(RTASSERT_OFFSET_OF(IEMTLB, uTlbPhysRev) < RTASSERT_OFFSET_OF(IEMTLB, aEntries));
    AssertCompile(RTASSERT_OFFSET_OF(VMCPUCC, iem.s.DataTlb.aEntries) < _64K);
    uint32_t const offEntries = offVCpuTlb + RT_UOFFSETOF(IEMTLB, aEntries) + (fEvenFirst ? 0 : sizeof(IEMTLBENTRY));
    if (offEntries < _64K)
    {
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(pTlbState->idxReg4, offEntries);
        pCodeBuf[off++] = Armv8A64MkInstrAddReg(pTlbState->idxReg4, IEMNATIVE_REG_FIXED_PVMCPU, pTlbState->idxReg4);
    }
    else
    {
        AssertCompileMemberAlignment(VMCPUCC, iem.s.CodeTlb.aEntries, 32);
        AssertCompileMemberAlignment(IEMTLB, aEntries, 32);
        AssertCompileSizeAlignment(IEMTLBENTRY, 32);
#   if IEMTLB_ENTRY_COUNT <= 16384 /*?*/
        AssertCompile(RTASSERT_OFFSET_OF(VMCPUCC, iem.s.CodeTlb.aEntries) < _64K*32U);
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(pTlbState->idxReg4, offEntries >> 5);
        pCodeBuf[off++] = Armv8A64MkInstrAddReg(pTlbState->idxReg4, IEMNATIVE_REG_FIXED_PVMCPU, pTlbState->idxReg4,
                                                true /*64Bit*/, false /*fSetFlags*/, 5 /*cShift*/, kArmv8A64InstrShift_Lsl);
#   else
        AssertCompile(RTASSERT_OFFSET_OF(VMCPUCC, iem.s.CodeTlb.aEntries) >= _64K*32U);
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(pTlbState->idxReg4, offEntries & UINT16_MAX);
        pCodeBuf[off++] = Armv8A64MkInstrMovK(pTlbState->idxReg4, offEntries >> 16, 1);
        pCodeBuf[off++] = Armv8A64MkInstrAddReg(pTlbState->idxReg4, IEMNATIVE_REG_FIXED_PVMCPU, pTlbState->idxReg4);
#   endif
    }
    AssertCompile(RTASSERT_OFFSET_OF(IEMTLB, aEntries) < 64U*8U - sizeof(IEMTLBENTRY));
    if (fEvenFirst)
        pCodeBuf[off++] = Armv8A64MkInstrLdPairGpr(pTlbState->idxReg3, pTlbState->idxReg5, pTlbState->idxReg4,
                                                   (RT_OFFSETOF(IEMTLB, uTlbRevision) - RT_OFFSETOF(IEMTLB, aEntries)) / 8);
    else /* This isn't 128-bit aligned, hope that doesn't hurt too much... */
        pCodeBuf[off++] = Armv8A64MkInstrLdPairGpr(pTlbState->idxReg5, pTlbState->idxReg3, pTlbState->idxReg4,
                                                   (  RT_OFFSETOF(IEMTLB, uTlbPhysRev) - RT_OFFSETOF(IEMTLB, aEntries)
                                                    - (int)sizeof(IEMTLBENTRY)) / 8);
#  else
    off = iemNativeEmitLoadGprFromVCpuU64Ex(pCodeBuf, off, pTlbState->idxReg3,
                                            fEvenFirst ? offVCpuTlb + RT_UOFFSETOF(IEMTLB, uTlbRevision)
                                            :            offVCpuTlb + RT_UOFFSETOF(IEMTLB, uTlbRevisionGlobal));
#  endif
    off = iemNativeEmitOrGprByGprEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg3);
# endif

    /*
     * 3b. Calc pTlbe.
     */
# if !defined(RT_ARCH_ARM64) || !defined(IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR)
    uint32_t const offTlbEntriesAdjusted = offVCpuTlb + RT_UOFFSETOF(IEMTLB, aEntries) + (fEvenFirst ? 0 : sizeof(IEMTLBENTRY));
# endif
# if defined(RT_ARCH_AMD64)
#  if IEMTLB_ENTRY_COUNT == 256
    /* movzx reg2, byte reg1 */
    off = iemNativeEmitLoadGprFromGpr8Ex(pCodeBuf, off, pTlbState->idxReg2, pTlbState->idxReg1);
#  else
    /* mov   reg2, reg1 */
    off = iemNativeEmitLoadGprFromGpr32Ex(pCodeBuf, off, pTlbState->idxReg2, pTlbState->idxReg1);
    /* and   reg2, IEMTLB_ENTRY_COUNT - 1U */
    off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off, pTlbState->idxReg2, IEMTLB_ENTRY_COUNT - 1U);
#  endif
    /* shl   reg2, 6 ; reg2 *= sizeof(IEMTLBENTRY) * 2 */
    AssertCompileSize(IEMTLBENTRY, 32);
    off = iemNativeEmitShiftGprLeftEx(pCodeBuf, off, pTlbState->idxReg2, 6);
    /* lea   reg2, [&pVCpu->iem.s.DataTlb.aEntries[!fEvenFirst] + reg2] */
    AssertCompile(IEMNATIVE_REG_FIXED_PVMCPU < 8);
    pCodeBuf[off++] = pTlbState->idxReg2 < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_X | X86_OP_REX_R;
    pCodeBuf[off++] = 0x8d;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, pTlbState->idxReg2 & 7, 4 /*SIB*/);
    pCodeBuf[off++] = X86_SIB_MAKE(IEMNATIVE_REG_FIXED_PVMCPU & 7, pTlbState->idxReg2 & 7, 0);
    pCodeBuf[off++] = RT_BYTE1(offTlbEntriesAdjusted);
    pCodeBuf[off++] = RT_BYTE2(offTlbEntriesAdjusted);
    pCodeBuf[off++] = RT_BYTE3(offTlbEntriesAdjusted);
    pCodeBuf[off++] = RT_BYTE4(offTlbEntriesAdjusted);

# elif defined(RT_ARCH_ARM64)
    /* reg2 = (reg1 & tlbmask) << 6 */
    AssertCompileSize(IEMTLBENTRY, 32);
    pCodeBuf[off++] = Armv8A64MkInstrUbfiz(pTlbState->idxReg2, pTlbState->idxReg1, 6, IEMTLB_ENTRY_COUNT_AS_POWER_OF_TWO);
#  ifdef IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
    /* reg2 += &pVCpu->iem.s.[Data|Code]Tlb.aEntries[!fEvenFirst] */
    pCodeBuf[off++] = Armv8A64MkInstrAddReg(pTlbState->idxReg2, pTlbState->idxReg2, pTlbState->idxReg4);
#  else
    /* reg2 += offsetof(VMCPUCC, iem.s.DataTlb.aEntries[!fEvenFirst]) */
    off = iemNativeEmitAddGprImmEx(pCodeBuf, off, pTlbState->idxReg2, offTlbEntriesAdjusted, pTlbState->idxReg3 /*iGprTmp*/);
    /* reg2 += pVCpu */
    off = iemNativeEmitAddTwoGprsEx(pCodeBuf, off, pTlbState->idxReg2, IEMNATIVE_REG_FIXED_PVMCPU);
#  endif
# else
#  error "Port me"
# endif

    /*
     * 3c. Compare the TLBE.uTag with the one from 2a (reg1).
     */
# if defined(RT_ARCH_AMD64)
    /* cmp reg1, [reg2] */
    pCodeBuf[off++] = X86_OP_REX_W | (pTlbState->idxReg1 < 8 ? 0 : X86_OP_REX_R) | (pTlbState->idxReg2 < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x3b;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg2, RT_UOFFSETOF(IEMTLBENTRY, uTag));
# elif defined(RT_ARCH_ARM64)
    /* reg3 = uTag; [pair: reg4 = fFlagsAndPhysRev;] */
#  ifdef IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
    AssertCompileMemberAlignment(IEMTLBENTRY, uTag, 16); /* It is said that misaligned pair loads doesn't perform well. */
    AssertCompile(RT_UOFFSETOF(IEMTLBENTRY, uTag) + sizeof(uint64_t) == RT_UOFFSETOF(IEMTLBENTRY, fFlagsAndPhysRev));
    pCodeBuf[off++] = Armv8A64MkInstrLdPairGpr(pTlbState->idxReg3, pTlbState->idxReg4,
                                               pTlbState->idxReg2, RT_UOFFSETOF(IEMTLBENTRY, uTag) / 8);
#  else
    off = iemNativeEmitLoadGprByGprU64Ex(pCodeBuf, off, pTlbState->idxReg3, pTlbState->idxReg2, RT_UOFFSETOF(IEMTLBENTRY, uTag));
#  endif
    off = iemNativeEmitCmpGprWithGprEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg3);
# else
#  error "Port me"
# endif
    /* jne checkalttlbe_and_missedtagstats */
# ifndef IEM_WITH_TLB_STATISTICS
    if (!fIncCheckAltTlbe)
        off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss, kIemNativeInstrCond_ne);
    else
# endif
    {
        off = iemNativeEmitJccToFixedEx(pCodeBuf, off, offCheckAltTlbeAndMissedTagStats, kIemNativeInstrCond_ne);
        if (fIncCheckAltTlbe)
            iemNativeFixupFixedJump(pReNative, offFixupCheckAltTlbeJmpBack, off);
    }

    /*
     * 4. Check TLB page table level access flags and physical page revision #.
     */
    /* mov reg1, mask */
    AssertCompile(IEMTLBE_F_PT_NO_USER == 4);
    uint64_t const fNoUser = (((pReNative->fExec >> IEM_F_X86_CPL_SHIFT) & IEM_F_X86_CPL_SMASK) + 1) & IEMTLBE_F_PT_NO_USER;
    uint64_t       fTlbe   = IEMTLBE_F_PHYS_REV | IEMTLBE_F_NO_MAPPINGR3 | IEMTLBE_F_PG_UNASSIGNED | IEMTLBE_F_PT_NO_ACCESSED
                           | fNoUser;
    if RT_CONSTEXPR_IF((a_fAccess & IEM_ACCESS_TYPE_EXEC) != 0)
        fTlbe |= IEMTLBE_F_PT_NO_EXEC /*| IEMTLBE_F_PG_NO_READ?*/;
    if RT_CONSTEXPR_IF((a_fAccess & IEM_ACCESS_TYPE_READ) != 0)
        fTlbe |= IEMTLBE_F_PG_NO_READ;
    if RT_CONSTEXPR_IF((a_fAccess & IEM_ACCESS_TYPE_WRITE) != 0)
        fTlbe |= IEMTLBE_F_PT_NO_WRITE | IEMTLBE_F_PG_NO_WRITE | IEMTLBE_F_PT_NO_DIRTY;
    off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, pTlbState->idxReg1, fTlbe);
# if defined(RT_ARCH_AMD64)
    /* and reg1, [reg2->fFlagsAndPhysRev] */
    pCodeBuf[off++] = X86_OP_REX_W | (pTlbState->idxReg1 < 8 ? 0 : X86_OP_REX_R) | (pTlbState->idxReg2 < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x23;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, pTlbState->idxReg1,
                                    pTlbState->idxReg2, RT_UOFFSETOF(IEMTLBENTRY, fFlagsAndPhysRev));

    /* cmp reg1, [pVCpu->iem.s.DataTlb.uTlbPhysRev] */
    pCodeBuf[off++] = X86_OP_REX_W | (pTlbState->idxReg1 < 8 ? 0 : X86_OP_REX_R);
    pCodeBuf[off++] = 0x3b;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, pTlbState->idxReg1, IEMNATIVE_REG_FIXED_PVMCPU,
                                    offVCpuTlb + RT_UOFFSETOF(IEMTLB, uTlbPhysRev));
# elif defined(RT_ARCH_ARM64)
#  ifdef IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR
    pCodeBuf[off++] = Armv8A64MkInstrAnd(pTlbState->idxReg1, pTlbState->idxReg1, pTlbState->idxReg4);
    off = iemNativeEmitCmpGprWithGprEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg5);
#  else
    off = iemNativeEmitLoadGprByGprU64Ex(pCodeBuf, off, pTlbState->idxReg3,
                                         pTlbState->idxReg2, RT_UOFFSETOF(IEMTLBENTRY, fFlagsAndPhysRev));
    pCodeBuf[off++] = Armv8A64MkInstrAnd(pTlbState->idxReg1, pTlbState->idxReg1, pTlbState->idxReg3);
    off = iemNativeEmitLoadGprFromVCpuU64Ex(pCodeBuf, off, pTlbState->idxReg3, offVCpuTlb + RT_UOFFSETOF(IEMTLB, uTlbPhysRev));
    off = iemNativeEmitCmpGprWithGprEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg3);
#  endif
# else
#  error "Port me"
# endif
# ifndef IEM_WITH_TLB_STATISTICS
    /* jne tlbmiss */
    off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss, kIemNativeInstrCond_ne);
# else
    /* je  2F; inc stat; jmp tlbmiss */
    uint32_t const offFixup2 = off;
    off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off + 16, kIemNativeInstrCond_e);
    off = iemNativeEmitIncStamCounterInVCpuEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->idxReg2,
                                              offVCpuTlb + RT_UOFFSETOF(IEMTLB, cTlbNativeMissFlagsAndPhysRev));
    off = iemNativeEmitJmpToLabelEx(pReNative, pCodeBuf, off, idxLabelTlbMiss);
    iemNativeFixupFixedJump(pReNative, offFixup2, off);
# endif

    /*
     * 5. Check that pbMappingR3 isn't NULL (paranoia) and calculate the
     *    resulting pointer.
     *
     *    For code TLB lookups we have some more work to do here to set various
     *    IEMCPU members and we return a GCPhys address rather than a host pointer.
     */
# if defined(RT_ARCH_ARM64)
    uint8_t const idxRegMappingPtr = a_fDataTlb && idxRegFlatPtr != idxRegMemResult /* See step 1b. */
                                   ? idxRegMemResult /* saves one instruction */ : pTlbState->idxReg1;
# else
    uint8_t const idxRegMappingPtr = pTlbState->idxReg1; /** @todo optimize the AMD64 case as well. */
# endif
# if defined(RT_ARCH_ARM64) && defined(IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR)
    if (!a_fDataTlb)
    {
        /* ldp  reg4, reg1, [reg2->GCPhys+pbMappingR3] */
        AssertCompileMemberAlignment(IEMTLBENTRY, GCPhys, 16);
        AssertCompileAdjacentMembers(IEMTLBENTRY, GCPhys, pbMappingR3);
        pCodeBuf[off++] = Armv8A64MkInstrLdPairGpr(pTlbState->idxReg4, idxRegMappingPtr,
                                                   pTlbState->idxReg2, RT_UOFFSETOF(IEMTLBENTRY, GCPhys) / 8);
    }
    else
# endif
    {
        /* mov  reg1, [reg2->pbMappingR3] */
        off = iemNativeEmitLoadGprByGprU64Ex(pCodeBuf, off, idxRegMappingPtr, pTlbState->idxReg2,
                                             RT_UOFFSETOF(IEMTLBENTRY, pbMappingR3));
    }

    if (a_fDataTlb)
    {
        if (idxRegFlatPtr == idxRegMemResult) /* See step 1b. */
        {
            Assert(idxRegMappingPtr == pTlbState->idxReg1);
            /* and result, 0xfff */
            off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off, idxRegMemResult, GUEST_PAGE_OFFSET_MASK);
            /* add result, reg1 */
            off = iemNativeEmitAddTwoGprsEx(pCodeBuf, off, idxRegMemResult, idxRegMappingPtr);
        }
        else
        {
            Assert(idxRegFlatPtr == pTlbState->idxRegPtr);
# if defined(RT_ARCH_ARM64)
            Assert(idxRegMappingPtr == idxRegMemResult);
            AssertCompile(GUEST_PAGE_SIZE <= HOST_PAGE_SIZE);
            pCodeBuf[off++] = Armv8A64MkInstrBfxil(idxRegMemResult, idxRegFlatPtr, 0, GUEST_PAGE_SHIFT);
# else
            Assert(idxRegMappingPtr == pTlbState->idxReg1);
            /* result = regflat & 0xfff */
            off = iemNativeEmitGpr32EqGprAndImmEx(pCodeBuf, off, idxRegMemResult, idxRegFlatPtr, GUEST_PAGE_OFFSET_MASK);
            /* add result, reg1 */
            off = iemNativeEmitAddTwoGprsEx(pCodeBuf, off, idxRegMemResult, pTlbState->idxReg1);
# endif
        }
    }
    else
    {
        /*
         * Code TLB use a la iemOpcodeFetchBytesJmp - keep reg2 pointing to the TLBE.
         *
         * Note. We do not need to set offCurInstrStart or offInstrNextByte.
         */
# if !defined(RT_ARCH_ARM64) || !defined(IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR)
#  ifdef RT_ARCH_AMD64
        uint8_t const idxReg3 = UINT8_MAX;
#  else
        uint8_t const idxReg3 = pTlbState->idxReg3;
#  endif
        /* Set pbInstrBuf first since we've got it loaded already. */
        off = iemNativeEmitStoreGprToVCpuU64Ex(pCodeBuf, off, pTlbState->idxReg1,
                                               RT_UOFFSETOF(VMCPUCC, iem.s.pbInstrBuf), idxReg3);
        /* Set uInstrBufPc to (FlatPC & ~GUEST_PAGE_OFFSET_MASK). */
        off = iemNativeEmitGprEqGprAndImmEx(pCodeBuf, off, pTlbState->idxReg1, idxRegFlatPtr, ~(RTGCPTR)GUEST_PAGE_OFFSET_MASK);
        off = iemNativeEmitStoreGprToVCpuU64Ex(pCodeBuf, off, pTlbState->idxReg1,
                                               RT_UOFFSETOF(VMCPUCC, iem.s.uInstrBufPc), idxReg3);
        /* Set cbInstrBufTotal to GUEST_PAGE_SIZE. */ /** @todo this is a simplifications. Calc right size using CS.LIM and EIP? */
        off = iemNativeEmitStoreImmToVCpuU16Ex(pCodeBuf, off, GUEST_PAGE_SIZE, RT_UOFFSETOF(VMCPUCC, iem.s.cbInstrBufTotal),
                                               pTlbState->idxReg1, idxReg3);
        /* Now set GCPhysInstrBuf last as we'll be returning it in idxRegMemResult. */
#  if defined(RT_ARCH_ARM64) && defined(IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR)
        off = iemNativeEmitStoreGprToVCpuU64Ex(pCodeBuf, off, pTlbState->idxReg4,
                                               RT_UOFFSETOF(VMCPUCC, iem.s.GCPhysInstrBuf), idxReg3);
#  else
        off = iemNativeEmitLoadGprByGprU64Ex(pCodeBuf, off, pTlbState->idxReg1,
                                             pTlbState->idxReg2, RT_UOFFSETOF(IEMTLBENTRY, GCPhys));
        off = iemNativeEmitStoreGprToVCpuU64Ex(pCodeBuf, off, pTlbState->idxReg1,
                                               RT_UOFFSETOF(VMCPUCC, iem.s.GCPhysInstrBuf), idxReg3);
#  endif
# else
        /* ARM64: Same as above but using STP. This ASSUMES that we can trash
                  the 6 bytes following iem.s.cbInstrBufTotal! */
        AssertCompileMemberAlignment(VMCPUCC, iem.s.pbInstrBuf, 16);
        AssertCompileAdjacentMembers(VMCPUCC, iem.s.pbInstrBuf, iem.s.uInstrBufPc);
        AssertCompile(RT_UOFFSETOF(VMCPUCC, iem.s.GCPhysInstrBuf) < 512);
        /* idxReg1 = reg2->pbMappingR3 (see previous LDP) */
        /* idxReg3 = FlatPC & ~GUEST_PAGE_OFFSET_MASK. */
        off = iemNativeEmitGprEqGprAndImmEx(pCodeBuf, off, pTlbState->idxReg3, idxRegFlatPtr, ~(RTGCPTR)GUEST_PAGE_OFFSET_MASK);
        pCodeBuf[off++] = Armv8A64MkInstrStPairGpr(pTlbState->idxReg1, pTlbState->idxReg3,
                                                   IEMNATIVE_REG_FIXED_PVMCPU, RT_UOFFSETOF(VMCPUCC, iem.s.pbInstrBuf) / 8);

        AssertCompileMemberAlignment(VMCPUCC, iem.s.GCPhysInstrBuf, 16);
        AssertCompileAdjacentMembers(VMCPUCC, iem.s.GCPhysInstrBuf, iem.s.cbInstrBufTotal);
        AssertCompile(RT_UOFFSETOF(VMCPUCC, iem.s.GCPhysInstrBuf) < 512);
#  ifndef IEM_WITH_OPAQUE_DECODER_STATE
        AssertCompileAdjacentMembers(VMCPUCC, iem.s.cbInstrBufTotal, iem.s.offCurInstrStart);
        AssertCompileAdjacentMembers(VMCPUCC, iem.s.offCurInstrStart, iem.s.fPrefixes);       /* these two will be set to ~0. */
#  endif
        /* idxReg4 = reg2->GCPhys (see previous LDP) */
        /* idxReg3 = GUEST_PAGE_SIZE | UINT64_C(0xffffffffffff0000) */
        pCodeBuf[off++] = Armv8A64MkInstrMovN(pTlbState->idxReg3, ~GUEST_PAGE_SIZE & 0xffff);
        pCodeBuf[off++] = Armv8A64MkInstrStPairGpr(pTlbState->idxReg4, pTlbState->idxReg3,
                                                   IEMNATIVE_REG_FIXED_PVMCPU, RT_UOFFSETOF(VMCPUCC, iem.s.GCPhysInstrBuf) / 8);
# endif
        if (!a_fNoReturn) /* (We skip this for iemNativeEmitBltLoadTlbAfterBranch.) */
        {
            /* Set idxRegMemResult. */
            if (idxRegFlatPtr == idxRegMemResult) /* See step 1b. */
                off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off, idxRegMemResult, GUEST_PAGE_OFFSET_MASK);
            else
                off = iemNativeEmitGpr32EqGprAndImmEx(pCodeBuf, off, idxRegMemResult, idxRegFlatPtr, GUEST_PAGE_OFFSET_MASK);
# if defined(RT_ARCH_ARM64) && defined(IEMNATIVE_WITH_TLB_LOOKUP_LOAD_STORE_PAIR)
            off = iemNativeEmitAddTwoGprsEx(pCodeBuf, off, idxRegMemResult, pTlbState->idxReg4);
# else
            off = iemNativeEmitAddTwoGprsEx(pCodeBuf, off, idxRegMemResult, pTlbState->idxReg1);
# endif
        }
    }

# if 0
    /*
     * To verify the result we call iemNativeHlpCheckTlbLookup via a wrapper.
     *
     * It's like the state logging, so parameters are passed on the stack.
     * iemNativeHlpAsmSafeWrapCheckTlbLookup(pVCpu, result, addr, seg | (a_cbMem << 8) | (a_fAccess << 16))
     */
    if (a_fDataTlb)
    {
#  ifdef RT_ARCH_AMD64
        if (!offDisp && !(a_fAccess & 0x8000))
        {
            /* push     seg | (a_cbMem << 8) | (a_fAccess << 16) */
            pCodeBuf[off++] = 0x68;
            pCodeBuf[off++] = iSegReg;
            pCodeBuf[off++] = a_cbMem;
            pCodeBuf[off++] = RT_BYTE1(a_fAccess);
            pCodeBuf[off++] = RT_BYTE2(a_fAccess);
        }
        else
        {
            /* mov   reg1, seg | (a_cbMem << 8) | (a_fAccess << 16) | (offDisp << 32) */
            off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, pTlbState->idxReg1,
                                            iSegReg | ((uint32_t)a_cbMem << 8) | (a_fAccess << 16) | ((uint64_t)offDisp << 32));
            /* push   reg1 */
            if (pTlbState->idxReg1 >= 8)
                pCodeBuf[off++] = X86_OP_REX_B;
            pCodeBuf[off++] = 0x50 + (pTlbState->idxReg1 & 7);
        }
        /* push     pTlbState->idxRegPtr / immediate address. */
        if (pTlbState->idxRegPtr != UINT8_MAX)
        {
            if (pTlbState->idxRegPtr >= 8)
                pCodeBuf[off++] = X86_OP_REX_B;
            pCodeBuf[off++] = 0x50 + (pTlbState->idxRegPtr & 7);
        }
        else
        {
            off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, pTlbState->idxReg1, pTlbState->uAbsPtr);
            if (pTlbState->idxReg1 >= 8)
                pCodeBuf[off++] = X86_OP_REX_B;
            pCodeBuf[off++] = 0x50 + (pTlbState->idxReg1 & 7);
        }
        /* push     idxRegMemResult */
        if (idxRegMemResult >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0x50 + (idxRegMemResult & 7);
        /* push     pVCpu */
        pCodeBuf[off++] = 0x50 + IEMNATIVE_REG_FIXED_PVMCPU;
        /* reg1 = helper; call reg1 */
        off = iemNativeEmitCallImmEx(pCodeBuf, off, (uintptr_t)iemNativeHlpAsmSafeWrapCheckTlbLookup, pTlbState->idxReg1);
        /* The stack is cleaned up by the helper function. */

#  elif defined(RT_ARCH_ARM64)
        /* Use the temporary registers for setting up the "call frame" and making the call. */
        /* reg1 = seg | (a_cbMem << 8) | (a_fAccess << 16) */
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(pTlbState->idxReg1, RT_MAKE_U16(iSegReg, a_cbMem));
        pCodeBuf[off++] = Armv8A64MkInstrMovK(pTlbState->idxReg1, RT_LO_U16(a_fAccess), 1);
        if (offDisp)
            pCodeBuf[off++] = Armv8A64MkInstrMovK(pTlbState->idxReg1, offDisp, 2);
        if (pTlbState->idxRegPtr != UINT8_MAX)
        {
            /* stp idxRegPtr, reg1, [sp, #-16]! */
            pCodeBuf[off++] = Armv8A64MkInstrStPairGpr(pTlbState->idxRegPtr, pTlbState->idxReg1,
                                                       ARMV8_A64_REG_SP, -2, kArm64InstrStLdPairType_PreIndex);
        }
        else
        {
            /* reg2 = immediate address */
            off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, pTlbState->idxReg2, pTlbState->uAbsPtr);
            /* stp reg2, reg1, [sp, #-16]! */
            pCodeBuf[off++] = Armv8A64MkInstrStPairGpr(pTlbState->idxReg2, pTlbState->idxReg1,
                                                       ARMV8_A64_REG_SP, -2, kArm64InstrStLdPairType_PreIndex);
        }
        /* stp pVCpu, idxRegMemResult, [sp, #-16]! (we don't need pVCpu, but push it for stack alignment) */
        pCodeBuf[off++] = Armv8A64MkInstrStPairGpr(IEMNATIVE_REG_FIXED_PVMCPU, idxRegMemResult,
                                                   ARMV8_A64_REG_SP, -2, kArm64InstrStLdPairType_PreIndex);
        /* reg1 = helper; brl reg1 */
        off = iemNativeEmitCallImmEx(pCodeBuf, off, (uintptr_t)iemNativeHlpAsmSafeWrapCheckTlbLookup, pTlbState->idxReg1);
        /* The stack is cleaned up by the helper function. */

#  else
#   error "Port me"
#  endif
    }

# endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    return off;
}
#endif /* IEMNATIVE_WITH_TLB_LOOKUP */


/** @} */

#endif /* !VMM_INCLUDED_SRC_include_IEMN8veRecompilerTlbLookup_h */

