/* $Id: IEMMc-armv8.h 110472 2025-07-30 10:14:37Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - IEM_MC_XXX, ARMv8 target.
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

#ifndef VMM_INCLUDED_SRC_VMMAll_target_armv8_IEMMc_armv8_h
#define VMM_INCLUDED_SRC_VMMAll_target_armv8_IEMMc_armv8_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/** @name   "Microcode" macros, ARMv8 specifics and overrides.
 * @{
 */

 /** Set 64-bit PC to uAddr (local) and PSTATE.BTYPE to a_uBType (constant). */
#define IEM_MC_BRANCH_TO_WITH_BTYPE_AND_FINISH(a_uNewPc, a_uBType); \
    return iemRegPcA64BranchToAndFinishClearingFlags((pVCpu), (a_uNewPc), (a_uBType))

#define IEM_MC_FETCH_GREG_SP_U32(a_u32Dst, a_iGReg)     (a_u32Dst) = iemGRegFetchU32(pVCpu, (a_iGReg), true /*fSp*/)
#define IEM_MC_FETCH_GREG_SP_U64(a_u64Dst, a_iGReg)     (a_u64Dst) = iemGRegFetchU64(pVCpu, (a_iGReg), true /*fSp*/)
#define IEM_MC_FETCH_GREG_SP_CHECK_ALIGN_U64(a_u64Dst, a_iGReg) \
    (a_u64Dst) = iemGRegFetchU64WithSpAlignCheck(pVCpu, (a_iGReg))

#define IEM_MC_STORE_GREG_SP_U32(a_iGReg, a_u32Value)   iemGRegStoreU32(pVCpu, (a_iGReg), (a_u32Value), true /*fSp*/)
#define IEM_MC_STORE_GREG_SP_U64(a_iGReg, a_u64Value)   iemGRegStoreU64(pVCpu, (a_iGReg), (a_u64Value), true /*fSp*/)

/** Fetches and extends the register value according to the extended SUB/ADD
 *  option and shift values. */
#define IEM_MC_FETCH_AND_SHIFT_GREG_U32(a_u32Dst, a_iGReg, a_u2Shift, a_cShiftCount) do { \
        (a_u32Dst) = iemGRegFetchU32(pVCpu, (a_iGReg), false /*fSp*/); \
        switch (a_u2Shift) \
        { \
            case 0: (a_u32Dst) <<= (a_cShiftCount);                              break; /* LSL */ \
            case 1: (a_u32Dst) >>= (a_cShiftCount);                              break; /* LSR */ \
            case 2: (a_u32Dst) = (int32_t)(a_u32Dst) >> (a_cShiftCount);         break; /* ASR */ \
            case 3: (a_u32Dst) = ASMRotateRightU32((a_u32Dst), (a_cShiftCount)); break; /* ROR */ \
        } \
    } while (0)

/** Fetches and extends the register value according to the extended SUB/ADD
 *  option and shift values. */
#define IEM_MC_FETCH_AND_SHIFT_GREG_U64(a_u64Dst, a_iGReg, a_u2Shift, a_cShiftCount) do { \
        (a_u64Dst) = iemGRegFetchU64(pVCpu, (a_iGReg), false /*fSp*/); \
        switch (a_u2Shift) \
        { \
            case 0: (a_u64Dst) <<= (a_cShiftCount);                              break; /* LSL */ \
            case 1: (a_u64Dst) >>= (a_cShiftCount);                              break; /* LSR */ \
            case 2: (a_u64Dst) = (int64_t)(a_u64Dst) >> (a_cShiftCount);         break; /* ASR */ \
            case 3: (a_u64Dst) = ASMRotateRightU64((a_u64Dst), (a_cShiftCount)); break; /* ROR */ \
        } \
    } while (0)

/** Fetches and extends the register value according to the extended SUB/ADD
 *  option and shift values. */
#define IEM_MC_FETCH_AND_EXTEND_GREG_U32(a_u32Dst, a_iGReg, a_u3ExtendOption, a_cShiftLeft) do { \
        (a_u32Dst) = iemGRegFetchU32(pVCpu, (a_iGReg), false /*fSp*/); \
        switch (a_u3ExtendOption) \
        { \
            case 2: /* nothing to do */                         break; /* UXTW/LSL */ \
            case 0: (a_u32Dst) = (uint8_t)(a_u32Dst);           break; /* UXTB */ \
            case 1: (a_u32Dst) = (uint16_t)(a_u32Dst);          break; /* UXTH */ \
            case 3: /* nothing to do */                         break; /* UXTX */ \
            case 4: (a_u32Dst) = (int32_t)(int8_t)(a_u32Dst);   break; /* SXTB */ \
            case 5: (a_u32Dst) = (int32_t)(int16_t)(a_u32Dst);  break; /* SXTH */ \
            case 6: /* nothing to do */                         break; /* SXTW */ \
            case 7: /* nothing to do */                         break; /* SXTX */ \
        } \
        (a_u32Dst) <<= (a_cShiftLeft); \
    } while (0)

/** Fetches and extends the register value according to the extended SUB/ADD
 *  option and shift values. */
#define IEM_MC_FETCH_AND_EXTEND_GREG_U64(a_u64Dst, a_iGReg, a_u3ExtendOption, a_cShiftLeft) do { \
        (a_u64Dst) = iemGRegFetchU64(pVCpu, (a_iGReg), false /*fSp*/); \
        switch (a_u3ExtendOption) \
        { \
            case 3: /* nothing to do */                         break; /* UXTX/LSL */ \
            case 0: (a_u64Dst) = (uint8_t)(a_u64Dst);           break; /* UXTB */ \
            case 1: (a_u64Dst) = (uint16_t)(a_u64Dst);          break; /* UXTH */ \
            case 2: (a_u64Dst) = (uint32_t)(a_u64Dst);          break; /* UXTW */ \
            case 4: (a_u64Dst) = (int64_t)(int8_t)(a_u64Dst);   break; /* SXTB */ \
            case 5: (a_u64Dst) = (int64_t)(int16_t)(a_u64Dst);  break; /* SXTH */ \
            case 6: (a_u64Dst) = (int64_t)(int32_t)(a_u64Dst);  break; /* SXTW */ \
            case 7: /* nothing to do */                         break; /* SXTX */ \
        } \
        (a_u64Dst) <<= (a_cShiftLeft); \
    } while (0)


#define IEM_MC_STORE_MEM_FLAT_U32_PAIR(a_GCPtrMem, a_u32Value1, a_u32Value2) \
    iemMemFlatStoreDataPairU32Jmp(pVCpu, (a_GCPtrMem), (a_u32Value1), (a_u32Value2))
#define IEM_MC_STORE_MEM_FLAT_U64_PAIR(a_GCPtrMem, a_u64Value1, a_u64Value2) \
    iemMemFlatStoreDataPairU64Jmp(pVCpu, (a_GCPtrMem), (a_u64Value1), (a_u64Value2))

/** Fetched PC (for PC relative addressing). */
#define IEM_MC_FETCH_PC_U64(a_GCPtrMem)  (a_GCPtrMem) = pVCpu->cpum.GstCtx.Pc.u64

/** Adds a constant to an address (64-bit), applying checked
 *  pointer arithmetic at the current EL. */
#define IEM_MC_ADD_CONST_U64_TO_ADDR(a_EffAddr, a_u64Const) do { \
        /*uint64_t const OldEffAddr = (a_EffAddr);*/ \
        (a_EffAddr) += (uint64_t)(a_u64Const); \
        AssertReturn(!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fCpa2, VERR_IEM_ASPECT_NOT_IMPLEMENTED); /** @todo CPA2 */ \
    } while (0)



#define IEM_MC_A64_SUBS_U64(a_uDifference, a_uMinuend, a_uSubtrahend) \
    (a_uDifference) = iemMcA64SubsU64(a_uMinuend, a_uSubtrahend, &pVCpu->cpum.GstCtx.fPState)

/** Helper that implements IEM_MC_A64_SUBS_U64. */
DECL_FORCE_INLINE(uint64_t) iemMcA64SubsU64(uint64_t uMinuend, uint64_t uSubtrahend, uint64_t *pfPState)
{
    uint64_t const uDiff     = uMinuend - uSubtrahend;
    uint64_t const fNewFlags = ((uDiff >> (63 - ARMV8_SPSR_EL2_AARCH64_N_BIT)) & ARMV8_SPSR_EL2_AARCH64_N)
                             | (uDiff                  ? 0 : ARMV8_SPSR_EL2_AARCH64_Z)
                             | (uMinuend < uSubtrahend ? 0 : ARMV8_SPSR_EL2_AARCH64_C) /* inverted for subtractions */
                             | (((((uMinuend ^ uSubtrahend) & (uDiff ^ uMinuend)) >> 63) & 1) << ARMV8_SPSR_EL2_AARCH64_V_BIT);
    *pfPState = (*pfPState & ~ARMV8_SPSR_EL2_AARCH64_NZCV) | fNewFlags;
    return uDiff;
}

#define IEM_MC_A64_ANDS_U32(a_uDst, a_fMask) (a_uDst) = iemMcA64Ands<uint32_t>(a_uDst, a_fMask, &pVCpu->cpum.GstCtx.fPState)
#define IEM_MC_A64_ANDS_U64(a_uDst, a_fMask) (a_uDst) = iemMcA64Ands<uint64_t>(a_uDst, a_fMask, &pVCpu->cpum.GstCtx.fPState)

/** Helper that implements IEM_MC_A64_ANDS_U32 and IEM_MC_A64_ANDS_U64. */
template<typename a_Type>
DECL_FORCE_INLINE(a_Type) iemMcA64Ands(a_Type uValue, a_Type fMask, uint64_t *pfPState)
{
    uValue &= fMask;
    AssertCompile(sizeof(a_Type) * 8 - 1 >= ARMV8_SPSR_EL2_AARCH64_N_BIT);
    uint64_t const fNewFlags = ((uValue >> ((sizeof(a_Type) * 8 - 1) - ARMV8_SPSR_EL2_AARCH64_N_BIT)) & ARMV8_SPSR_EL2_AARCH64_N)
                             | (uValue ? 0 : ARMV8_SPSR_EL2_AARCH64_Z);
    *pfPState = (*pfPState & ~ARMV8_SPSR_EL2_AARCH64_NZCV) | fNewFlags;
    return uValue;
}


/*
 * Synchronizing primitives and memory barriers.
 * Treat these as all being in the outer domain for now.
 */
#if defined(RT_ARCH_ARM64)
# if defined(_MSC_VER)
#  define IEM_MC_A64_DSB_READS()    __dsb(_ARM64_BARRIER_OSHLD)
#  define IEM_MC_A64_DSB_WRITES()   __dsb(_ARM64_BARRIER_OSHST)
#  define IEM_MC_A64_DSB_ALL()      __dsb(_ARM64_BARRIER_OSH)

#  define IEM_MC_A64_DMB_READS()    __dmb(_ARM64_BARRIER_OSHLD)
#  define IEM_MC_A64_DMB_WRITES()   __dmb(_ARM64_BARRIER_OSHST)
#  define IEM_MC_A64_DMB_ALL()      __dmb(_ARM64_BARRIER_OSH)
# else
#  define IEM_MC_A64_DSB_READS()    __asm__ __volatile__("dsb oshld")
#  define IEM_MC_A64_DSB_WRITES()   __asm__ __volatile__("dsb oshst")
#  define IEM_MC_A64_DSB_ALL()      __asm__ __volatile__("dsb osh")

#  define IEM_MC_A64_DMB_READS()    __asm__ __volatile__("dmb oshld")
#  define IEM_MC_A64_DMB_WRITES()   __asm__ __volatile__("dmb oshst")
#  define IEM_MC_A64_DMB_ALL()      __asm__ __volatile__("dmb osh")
# endif
#else
# define IEM_MC_A64_DSB_READS()     ASMReadFence()
# define IEM_MC_A64_DSB_WRITES()    ASMWriteFence()
# define IEM_MC_A64_DSB_ALL()       ASMMemoryFence()

# define IEM_MC_A64_DMB_READS()     ASMReadFence()
# define IEM_MC_A64_DMB_WRITES()    ASMWriteFence()
# define IEM_MC_A64_DMB_ALL()       ASMMemoryFence()
#endif

/** Instruction boundrary is a NOP in the interpreter for now. */
#define IEM_MC_A64_ISB()            ((void)0)


/** @}  */

#endif /* !VMM_INCLUDED_SRC_VMMAll_target_armv8_IEMMc_armv8_h */

