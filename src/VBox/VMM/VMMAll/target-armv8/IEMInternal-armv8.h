/* $Id: IEMInternal-armv8.h 110476 2025-07-30 11:37:14Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Internal header file, ARMv8 target specifics.
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

#ifndef VMM_INCLUDED_SRC_VMMAll_target_armv8_IEMInternal_armv8_h
#define VMM_INCLUDED_SRC_VMMAll_target_armv8_IEMInternal_armv8_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


RT_C_DECLS_BEGIN


/** @defgroup grp_iem_int_arm   ARM Target Internals
 * @ingroup grp_iem_int
 * @internal
 * @{
 */


/** @name IEM_ARM_REGIME_XXX - Translation regime
 * We've taken the ones listed in DDI0487L section D8.1.2 and added variations
 * with and without stage 2 as appropriate.
 *
 * Overview:
 *    0.  D8.1.2.1:  Non-secure EL1&0.   w/o stage2    two ranges    asid
 *    1.  D8.1.2.1:  Non-secure EL1&0.   with stage2   two ranges    asid    vmid
 *    2.  D8.1.2.2:  Secure EL1&0.       w/o stage2    two ranges    asid
 *    3.  D8.1.2.2:  Secure EL1&0.       with stage2   two ranges    asid    vmid
 *    4.  D8.1.2.3:  Realm EL1&0.        with stage2   two ranges    asid    vmid
 *    5.  D8.1.2.4:  Non-secure EL2&0.   w/o stage2    two ranges    asid
 *    6.  D8.1.2.5:  Secure EL2&0.       w/o stage2    two ranges    asid
 *    7.  D8.1.2.6:  Realm EL2&0.        w/o stage2    two ranges    asid
 *    8.  D8.1.2.7:  Non-secure EL2.     w/o stage2    single range  none
 *    9.  D8.1.2.8:  Secure EL2.         w/o stage2    single range  none
 *    10. D8.1.2.9:  Realm EL2.          w/o stage2    single range  none
 *    11. D8.1.2.10: EL3.                w/o stage2    single range  none
 *
 * @todo There is a EL3&0 regime where EL3 is aarch32.
 *
 * @sa IEM_F_ARM_REGIME
 * @{
 */
#define IEM_ARM_REGIME_EL10_NOSEC       0U   /**< Non-secure EL1&0, no stage 2,   two ranges,   asid. */
#define IEM_ARM_REGIME_EL10_NOSEC_S2    1U   /**< Non-secure EL1&0, with stage 2, two ranges,   asid, vmid. */
#define IEM_ARM_REGIME_EL10_SEC         2U   /**< Secure EL1&0,     no stage 2,   two ranges,   asid. */
#define IEM_ARM_REGIME_EL10_SEC_S2      3U   /**< Secure EL1&0,     with stage 2, two ranges,   asid, vmid. */
#define IEM_ARM_REGIME_EL10_REALM_S2    4U   /**< Realm EL1&0,      with stage 2, two ranges,   asid, vmid. */
#define IEM_ARM_REGIME_EL20_NOSEC       5U   /**< Non-secure EL2&0, no stage 2,   two ranges,   asid. */
#define IEM_ARM_REGIME_EL20_SEC         6U   /**< Secure EL2&0,     no stage 2,   two ranges,   asid. */
#define IEM_ARM_REGIME_EL20_REALM       7U   /**< Realm EL2&0,      no stage 2,   two ranges,   asid. */
#define IEM_ARM_REGIME_EL2_NONSEC       8U   /**< Non-secure EL2,   no stage 2,   single range, no asid. */
#define IEM_ARM_REGIME_EL2_SEC          9U   /**< Secure EL2,       no stage 2,   single range, no asid. */
#define IEM_ARM_REGIME_EL2_REALM        10U  /**< Realm EL2,        no stage 2,   single range, no asid. */
#define IEM_ARM_REGIME_EL3              11U  /**< EL3,              no stage 2,   single range, no asid. */
#define IEM_ARM_REGIME_LAST             IEM_ARM_REGIME_EL3

/** Checks if @a a_uRegime is a secure one. */
#define IEM_ARM_REGIME_IS_SECURE(a_uRegime) \
    (   (a_uRegime) == IEM_ARM_REGIME_EL10_SEC \
     || (a_uRegime) == IEM_ARM_REGIME_EL10_SEC_S2 \
     || (a_uRegime) == IEM_ARM_REGIME_EL20_SEC \
     || (a_uRegime) == IEM_ARM_REGIME_EL2_SEC )

/** Checks if @a a_uRegime is a non-secure one. */
#define IEM_ARM_REGIME_IS_NON_SECURE(a_uRegime) \
    (   (a_uRegime) == IEM_ARM_REGIME_EL10_NOSEC \
     || (a_uRegime) == IEM_ARM_REGIME_EL10_NOSEC_S2 \
     || (a_uRegime) == IEM_ARM_REGIME_EL20_NOSEC \
     || (a_uRegime) == IEM_ARM_REGIME_EL2_NONSEC )

/** Checks if @a a_uRegime is a realm one. */
#define IEM_ARM_REGIME_IS_REALM(a_uRegime) \
    (   (a_uRegime) == IEM_ARM_REGIME_EL10_REALM_S2 \
     || (a_uRegime) == IEM_ARM_REGIME_EL20_REALM \
     || (a_uRegime) == IEM_ARM_REGIME_EL2_REALM )

/** Checks if @a a_uRegime includes stage 2 translation.
 * Implies that EL2 is enabled. */
#define IEM_ARM_REGIME_HAS_STAGE_2(a_uRegime) \
    (   (a_uRegime) == IEM_ARM_REGIME_EL10_NOSEC_S2 \
     || (a_uRegime) == IEM_ARM_REGIME_EL10_SEC_S2 \
     || (a_uRegime) == IEM_ARM_REGIME_EL10_REALM_S2 )

/** Checks if @a a_uRegime may use two translation ranges. */
#define IEM_ARM_REGIME_MAY_HAVE_TWO_RANGES(a_uRegime)   ( (a_uRegime) <= IEM_ARM_REGIME_EL20_REALM )

/** Checks if @a a_uRegime uses ASID. */
#define IEM_ARM_REGIME_USE_ASID(a_uRegime)              ( (a_uRegime) <= IEM_ARM_REGIME_EL20_REALM )

/** Checks if @a a_uRegime uses VMID. */
#define IEM_ARM_REGIME_USE_VMID(a_uRegime)              IEM_ARM_REGIME_HAS_STAGE_2(a_uRegime)

/** Checks if @a a_uRegime includes stage 2 translation. */
#define IEM_ARM_REGIME_HAS_UNPRIVILEGED(a_uRegime)      ( (a_uRegime) <= IEM_ARM_REGIME_EL20_REALM )


/** @} */


/** @name Misc Helpers
 * @{  */
/** For checking whether an address is in the positive (true) or negative
 *  (false) address space.
 * @note ASSUMES aarch64. Will not work if in aarch32 EL1+ mode. */
#define IEMARM_IS_POSITIVE_64BIT_ADDR(a_uAddr)          ( ((a_uAddr) & RT_BIT_64(55)) == 0 )

/** @} */


/** @name  Raising Exceptions.
 * @{ */
//VBOXSTRICTRC            iemRaiseXcptOrInt(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t u8Vector, uint32_t fFlags,
//                                          uint16_t uErr, uint64_t uCr2) RT_NOEXCEPT;
//DECL_NO_RETURN(void)    iemRaiseXcptOrIntJmp(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t u8Vector,
//                                             uint32_t fFlags, uint16_t uErr, uint64_t uCr2) IEM_NOEXCEPT_MAY_LONGJMP;

#define IEM_RAISE_PROTOS(a_Name, ...) \
    DECLHIDDEN(VBOXSTRICTRC) a_Name(__VA_ARGS__) RT_NOEXCEPT; \
    DECL_NO_RETURN(void) RT_CONCAT(a_Name,Jmp)(__VA_ARGS__) IEM_NOEXCEPT_MAY_LONGJMP

IEM_RAISE_PROTOS(iemRaiseUndefined, PVMCPUCC pVCpu);

IEM_RAISE_PROTOS(iemRaiseDataAbortFromWalk,
                 PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t cbMem, uint32_t fAccess, int rc, PCPGMPTWALKFAST pWalkFast);

IEM_RAISE_PROTOS(iemRaiseDataAbortFromAlignmentCheck,
                 PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t cbMem, uint32_t fAccess);

IEM_RAISE_PROTOS(iemRaiseDebugDataAccessOrInvokeDbgf,
                 PVMCPUCC pVCpu, uint32_t fDataBps, RTGCPTR GCPtrMem, size_t cbMem, uint32_t fAccess);

IEM_RAISE_PROTOS(iemRaiseInstructionAbortFromWalk,
                 PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t cbMem, uint32_t fAccess, int rc, PCPGMPTWALKFAST pWalkFast);

IEM_RAISE_PROTOS(iemRaiseInstructionAbortTlbPermision,
                 PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t cbMem, PCIEMTLBENTRY pTlbe);

IEM_RAISE_PROTOS(iemRaisePcAlignmentCheck, PVMCPUCC pVCpu);
IEM_RAISE_PROTOS(iemRaiseSpAlignmentCheck, PVMCPUCC pVCpu);


/** Creates an instruction essence value for MRS, MSR and similar.   */
#define IEM_CIMPL_SYSREG_INSTR_ESSENCE_MAKE(a_idSysReg, a_idxGpr, a_f1Write, a_f4Conditions) \
    ((a_idSysReg) | ((a_idxGpr) << 16) | ((a_f1Write) << 24) | ((a_f4Conditions) << 28))
#define IEM_CIMPL_SYSREG_INSTR_ESSENCE_GET_SYSREG(a_uInstrEssence)  ((a_uInstrEssence) & UINT32_C(0xffff))
#define IEM_CIMPL_SYSREG_INSTR_ESSENCE_GET_GPR(a_uInstrEssence)     (((a_uInstrEssence) >> 16) & UINT32_C(0x1f))
#define IEM_CIMPL_SYSREG_INSTR_ESSENCE_IS_WRITE(a_uInstrEssence)    RT_BOOL((a_uInstrEssence) & RT_BIT_32(24))
#define IEM_CIMPL_SYSREG_INSTR_ESSENCE_IS_READ(a_uInstrEssence)     (!((a_uInstrEssence) & RT_BIT_32(24)))

VBOXSTRICTRC iemRaiseSystemAccessTrap(PVMCPU pVCpu, uint32_t uEl, uint32_t uInstrEssence) RT_NOEXCEPT;
VBOXSTRICTRC iemRaiseSystemAccessTrapSve(PVMCPU pVCpu, uint32_t uEl) RT_NOEXCEPT;
VBOXSTRICTRC iemRaiseSystemAccessTrapSme(PVMCPU pVCpu, uint32_t uEl) RT_NOEXCEPT;
VBOXSTRICTRC iemRaiseSystemAccessTrapAdvSimdFpAccessA64(PVMCPU pVCpu, uint32_t uEl) RT_NOEXCEPT;
VBOXSTRICTRC iemRaiseSystemAccessTrapUnknown(PVMCPU pVCpu, uint32_t uEl) RT_NOEXCEPT;
VBOXSTRICTRC iemRaiseExlockException(PVMCPU pVCpu) RT_NOEXCEPT;

IEM_CIMPL_PROTO_0(iemCImplRaiseInvalidOpcode);

/**
 * Macro for calling iemCImplRaiseInvalidOpcode() for decode/static \#UDs.
 *
 * This is for things that will _always_ decode to an \#UD, taking the
 * recompiler into consideration and everything.
 *
 * @return  Strict VBox status code.
 */
#define IEMOP_RAISE_INVALID_OPCODE_RET()        IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_XCPT, 0, iemCImplRaiseInvalidOpcode)

/** @} */


/** @name Register Access.
 * @{ */
VBOXSTRICTRC    iemRegRipRelativeJumpS8AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int8_t offNextInstr,
                                                           IEMMODE enmEffOpSize) RT_NOEXCEPT;
VBOXSTRICTRC    iemRegRipRelativeJumpS16AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int16_t offNextInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemRegRipRelativeJumpS32AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int32_t offNextInstr,
                                                            IEMMODE enmEffOpSize) RT_NOEXCEPT;
/** @} */


/** @name   Memory access.
 * @{ */

/** XXX */
#define IEM_MEMMAP_F_ALIGN_XXX      RT_BIT_32(16)

VBOXSTRICTRC    iemMemMap(PVMCPUCC pVCpu, void **ppvMem, uint8_t *pbUnmapInfo, size_t cbMem, RTGCPTR GCPtrMem,
                          uint32_t fAccess, uint32_t uAlignCtl) RT_NOEXCEPT;

uint32_t        iemOpcodeGetU32SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t        iemOpcodeGetU16SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;

uint8_t         iemMemFetchDataU8SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t        iemMemFetchDataU16SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemMemFetchDataU32SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchDataU64SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU128SafeJmp(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU256SafeJmp(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemMemFetchDataPairU32SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t *pu32Val2) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchDataPairU64SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint64_t *pu64Val2) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataPairU128SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PRTUINT128U pu128Val1, PRTUINT128U pu128Val2) IEM_NOEXCEPT_MAY_LONGJMP;

void            iemMemStoreDataU8SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t u8Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU16SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint16_t u16Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU32SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t u32Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU64SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint64_t u64Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU128SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PCRTUINT128U u128Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataPairU32SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t u32Value1, uint32_t u32Value2) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataPairU64SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint64_t u64Value1, uint64_t u64Value2) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataPairU128SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PCRTUINT128U pu128Value1, PCRTUINT128U pu128Value2) IEM_NOEXCEPT_MAY_LONGJMP;

#if 0 /* rework this later */
VBOXSTRICTRC    iemMemFetchDataU8(PVMCPUCC pVCpu, uint8_t *pu8Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU16(PVMCPUCC pVCpu, uint16_t *pu16Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU32(PVMCPUCC pVCpu, uint32_t *pu32Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU32NoAc(PVMCPUCC pVCpu, uint32_t *pu32Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU32_ZX_U64(PVMCPUCC pVCpu, uint64_t *pu64Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU64(PVMCPUCC pVCpu, uint64_t *pu64Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU64NoAc(PVMCPUCC pVCpu, uint64_t *pu64Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU64AlignedU128(PVMCPUCC pVCpu, uint64_t *pu64Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataR80(PVMCPUCC pVCpu, PRTFLOAT80U pr80Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataD80(PVMCPUCC pVCpu, PRTPBCD80U pd80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU128(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU128NoAc(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
uint32_t        iemMemFetchDataU32NoAcSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemMemFlatFetchDataU32SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchDataU64NoAcSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchDataU64AlignedU128SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataR80SafeJmp(PVMCPUCC pVCpu, PRTFLOAT80U pr80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataD80SafeJmp(PVMCPUCC pVCpu, PRTPBCD80U pd80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU128NoAcSafeJmp(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU128AlignedSseSafeJmp(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;

VBOXSTRICTRC    iemMemFetchSysU8(PVMCPUCC pVCpu, uint8_t *pu8Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchSysU16(PVMCPUCC pVCpu, uint16_t *pu16Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchSysU32(PVMCPUCC pVCpu, uint32_t *pu32Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchSysU64(PVMCPUCC pVCpu, uint64_t *pu64Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchSysU128(PVMCPUCC pVCpu, uint128_t *pu128Dst, RTGCPTR GCPtrMem) RT_NOEXCEPT;

VBOXSTRICTRC    iemMemStoreDataU8(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint8_t u8Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU16(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint16_t u16Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU32(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint32_t u32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU64(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint64_t u64Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU128(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU128NoAc(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU128AlignedSse(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU256(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU256NoAc(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU256AlignedAvx(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataXdtr(PVMCPUCC pVCpu, uint16_t cbLimit, RTGCPTR GCPtrBase, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
void            iemMemStoreDataU128NoAcSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT128U pu128Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU128AlignedSseSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT128U pu128Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256NoAcSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256AlignedAvxSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataR80SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTFLOAT80U pr80Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataD80SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTPBCD80U pd80Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU128AlignedSseJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;

uint8_t        *iemMemMapDataU8RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint8_t        *iemMemMapDataU8AtSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint8_t        *iemMemMapDataU8WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint8_t const  *iemMemMapDataU8RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t       *iemMemMapDataU16RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t       *iemMemMapDataU16AtSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t       *iemMemMapDataU16WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t const *iemMemMapDataU16RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t       *iemMemMapDataU32RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t       *iemMemMapDataU32AtSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t       *iemMemMapDataU32WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t const *iemMemMapDataU32RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t       *iemMemMapDataU64RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t       *iemMemMapDataU64AtSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t       *iemMemMapDataU64WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t const *iemMemMapDataU64RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTFLOAT80U     iemMemMapDataR80RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTFLOAT80U     iemMemMapDataR80WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PCRTFLOAT80U    iemMemMapDataR80RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTPBCD80U      iemMemMapDataD80RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTPBCD80U      iemMemMapDataD80WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PCRTPBCD80U     iemMemMapDataD80RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTUINT128U     iemMemMapDataU128RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTUINT128U     iemMemMapDataU128AtSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTUINT128U     iemMemMapDataU128WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PCRTUINT128U    iemMemMapDataU128RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;

VBOXSTRICTRC    iemMemStackPushBeginSpecial(PVMCPUCC pVCpu, size_t cbMem, uint32_t cbAlign,
                                            void **ppvMem, uint8_t *pbUnmapInfo, uint64_t *puNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushCommitSpecial(PVMCPUCC pVCpu, uint8_t bUnmapInfo, uint64_t uNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU16(PVMCPUCC pVCpu, uint16_t u16Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU32(PVMCPUCC pVCpu, uint32_t u32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU64(PVMCPUCC pVCpu, uint64_t u64Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU16Ex(PVMCPUCC pVCpu, uint16_t u16Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU32Ex(PVMCPUCC pVCpu, uint32_t u32Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU64Ex(PVMCPUCC pVCpu, uint64_t u64Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU32SReg(PVMCPUCC pVCpu, uint32_t u32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopBeginSpecial(PVMCPUCC pVCpu, size_t cbMem, uint32_t cbAlign,
                                           void const **ppvMem, uint8_t *pbUnmapInfo, uint64_t *puNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopContinueSpecial(PVMCPUCC pVCpu, size_t off, size_t cbMem,
                                              void const **ppvMem, uint8_t *pbUnmapInfo, uint64_t uCurNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopDoneSpecial(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU16(PVMCPUCC pVCpu, uint16_t *pu16Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU32(PVMCPUCC pVCpu, uint32_t *pu32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU64(PVMCPUCC pVCpu, uint64_t *pu64Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU16Ex(PVMCPUCC pVCpu, uint16_t *pu16Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU32Ex(PVMCPUCC pVCpu, uint32_t *pu32Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU64Ex(PVMCPUCC pVCpu, uint64_t *pu64Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;

void            iemMemStackPushU16SafeJmp(PVMCPUCC pVCpu, uint16_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPushU32SafeJmp(PVMCPUCC pVCpu, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPushU32SRegSafeJmp(PVMCPUCC pVCpu, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPushU64SafeJmp(PVMCPUCC pVCpu, uint64_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPopGRegU16SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPopGRegU32SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPopGRegU64SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;

void            iemMemFlat32StackPushU16SafeJmp(PVMCPUCC pVCpu, uint16_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat32StackPushU32SafeJmp(PVMCPUCC pVCpu, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat32StackPushU32SRegSafeJmp(PVMCPUCC pVCpu, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat32StackPopGRegU16SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat32StackPopGRegU32SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;

void            iemMemFlat64StackPushU16SafeJmp(PVMCPUCC pVCpu, uint16_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat64StackPushU64SafeJmp(PVMCPUCC pVCpu, uint64_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat64StackPopGRegU16SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat64StackPopGRegU64SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;

void            iemMemStoreStackU16SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint16_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreStackU32SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreStackU32SRegSafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreStackU64SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint64_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;

uint16_t        iemMemFetchStackU16SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemMemFetchStackU32SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchStackU64SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;

#endif /* later */
/** @} */

/** @name IEMAllCImpl.cpp
 * @note sed -e '/IEM_CIMPL_DEF_/!d' -e 's/IEM_CIMPL_DEF_/IEM_CIMPL_PROTO_/' -e 's/$/;/'
 * @{ */

/*
 * System register related stuff.
 */
IEM_CIMPL_PROTO_2(iemCImplA64_msr, uint32_t, idSysReg, uint32_t, idxGprSrc);
IEM_CIMPL_PROTO_2(iemCImplA64_mrs, uint32_t, idSysReg, uint32_t, idxGprDst);
DECLHIDDEN(VBOXSTRICTRC) iemCImplA64_mrs_novar(PVMCPU pVCpu, uint32_t idSysReg, const char *pszRegName,
                                               uint64_t *puDst, uint32_t idxGprDst) RT_NOEXCEPT;
DECLHIDDEN(VBOXSTRICTRC) iemCImplA64_msr_novar(PVMCPU pVCpu, uint32_t idSysReg, const char *pszRegName,
                                               uint64_t uValue, uint32_t idxGprSrc) RT_NOEXCEPT;
DECLHIDDEN(VBOXSTRICTRC) iemCImplA64_mrs_fallback(PVMCPU pVCpu, uint32_t idxGprDst, uint32_t idSysReg) RT_NOEXCEPT;
DECLHIDDEN(VBOXSTRICTRC) iemCImplA64_msr_fallback(PVMCPU pVCpu, uint32_t idSysReg, uint64_t uValue, uint32_t idxGprSrc) RT_NOEXCEPT;

DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpRecalcFlags(PVMCPU pVCpu, VBOXSTRICTRC rcStrict);
DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpRecalcFlagsAndPgmModeEl1(PVMCPU pVCpu, VBOXSTRICTRC rcStrict);
DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpRecalcFlagsAndPgmModeEl2(PVMCPU pVCpu, VBOXSTRICTRC rcStrict);
DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpRecalcFlagsAndPgmModeEl3(PVMCPU pVCpu, VBOXSTRICTRC rcStrict);

DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpNVMemReadU64(PVMCPU pVCpu, uint32_t off, uint64_t *puDst) RT_NOEXCEPT;
DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpNVMemWriteU64(PVMCPU pVCpu, uint32_t off, uint64_t uValue) RT_NOEXCEPT;
DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpReadDbgDtrEl0U64(PVMCPU pVCpu, uint64_t *puDst) RT_NOEXCEPT;
DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpReadDbgDtrEl0U32(PVMCPU pVCpu, uint64_t *puDst) RT_NOEXCEPT;
DECLHIDDEN(uint64_t)     iemCImplHlpGetIdSysReg(PVMCPU pVCpu, uint32_t idSysReg) RT_NOEXCEPT;
DECLHIDDEN(uint64_t)     iemCImplHlpGetAmUserEnrEl0(PVMCPU pVCpu) RT_NOEXCEPT;
DECLHIDDEN(uint64_t)     iemCImplHlpGetPmUserEnrEl0(PVMCPU pVCpu) RT_NOEXCEPT;
DECLHIDDEN(uint64_t)     iemCImplHlpGetPmSelrEl0(PVMCPU pVCpu) RT_NOEXCEPT;
DECLHIDDEN(uint64_t)     iemCImplHlpGetPmUacrEl1(PVMCPU pVCpu) RT_NOEXCEPT;
DECLHIDDEN(uint64_t)     iemCImplHlpGetPhysicalSystemTimerCount(PVMCPU pVCpu) RT_NOEXCEPT;

/** @} */

/*
 * Recompiler related stuff.
 */


IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_Nop);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_LogCpuState);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_DeferToCImpl0);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckIrq);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckTimers);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckTimersAndIrq);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckMode);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckHwInstrBps);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodes);

/* Branching: */
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLimAndPcAndOpcodes);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckPcAndOpcodes);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckPcAndOpcodesConsiderCsLim);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesLoadingTlbConsiderCsLim);

/* Natural page crossing: */
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesAcrossPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesAcrossPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesAcrossPageLoadingTlbConsiderCsLim);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesOnNextPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesOnNextPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesOnNextPageLoadingTlbConsiderCsLim);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesOnNewPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesOnNewPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesOnNewPageLoadingTlbConsiderCsLim);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_Jump);

bool iemThreadedCompileEmitIrqCheckBefore(PVMCPUCC pVCpu, PIEMTB pTb);
bool iemThreadedCompileBeginEmitCallsComplications(PVMCPUCC pVCpu, PIEMTB pTb);
#ifdef IEM_WITH_INTRA_TB_JUMPS
DECLHIDDEN(int)     iemThreadedCompileBackAtFirstInstruction(PVMCPU pVCpu, PIEMTB pTb) RT_NOEXCEPT;
#endif


/** @} */

RT_C_DECLS_END


#endif /* !VMM_INCLUDED_SRC_VMMAll_target_armv8_IEMInternal_armv8_h */

