/* $Id: IEMOpHlp-x86.h 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Opcode Helpers.
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

#ifndef VMM_INCLUDED_SRC_VMMAll_target_x86_IEMOpHlp_x86_h
#define VMM_INCLUDED_SRC_VMMAll_target_x86_IEMOpHlp_x86_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/** @name   Opcode Debug Helpers.
 * @{
 */
#ifdef DEBUG
# define IEMOP_MNEMONIC(a_Stats, a_szMnemonic) \
    do { \
        IEMOP_INC_STATS(a_Stats); \
        Log4(("decode - %04x:%RGv %s%s [#%u]\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, \
              pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK ? "lock " : "", a_szMnemonic, pVCpu->iem.s.cInstructions)); \
    } while (0)

# define IEMOP_MNEMONIC0EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_fDisHints, a_fIemHints) \
    do { \
        IEMOP_MNEMONIC(a_Stats, a_szMnemonic); \
        (void)RT_CONCAT(IEMOPFORM_, a_Form); \
        (void)RT_CONCAT(OP_,a_Upper); \
        (void)(a_fDisHints); \
        (void)(a_fIemHints); \
    } while (0)

# define IEMOP_MNEMONIC1EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_fDisHints, a_fIemHints) \
    do { \
        IEMOP_MNEMONIC(a_Stats, a_szMnemonic); \
        (void)RT_CONCAT(IEMOPFORM_, a_Form); \
        (void)RT_CONCAT(OP_,a_Upper); \
        (void)RT_CONCAT(OP_PARM_,a_Op1); \
        (void)(a_fDisHints); \
        (void)(a_fIemHints); \
    } while (0)

# define IEMOP_MNEMONIC2EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_fDisHints, a_fIemHints) \
    do { \
        IEMOP_MNEMONIC(a_Stats, a_szMnemonic); \
        (void)RT_CONCAT(IEMOPFORM_, a_Form); \
        (void)RT_CONCAT(OP_,a_Upper); \
        (void)RT_CONCAT(OP_PARM_,a_Op1); \
        (void)RT_CONCAT(OP_PARM_,a_Op2); \
        (void)(a_fDisHints); \
        (void)(a_fIemHints); \
    } while (0)

# define IEMOP_MNEMONIC3EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_fDisHints, a_fIemHints) \
    do { \
        IEMOP_MNEMONIC(a_Stats, a_szMnemonic); \
        (void)RT_CONCAT(IEMOPFORM_, a_Form); \
        (void)RT_CONCAT(OP_,a_Upper); \
        (void)RT_CONCAT(OP_PARM_,a_Op1); \
        (void)RT_CONCAT(OP_PARM_,a_Op2); \
        (void)RT_CONCAT(OP_PARM_,a_Op3); \
        (void)(a_fDisHints); \
        (void)(a_fIemHints); \
    } while (0)

# define IEMOP_MNEMONIC4EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_Op4, a_fDisHints, a_fIemHints) \
    do { \
        IEMOP_MNEMONIC(a_Stats, a_szMnemonic); \
        (void)RT_CONCAT(IEMOPFORM_, a_Form); \
        (void)RT_CONCAT(OP_,a_Upper); \
        (void)RT_CONCAT(OP_PARM_,a_Op1); \
        (void)RT_CONCAT(OP_PARM_,a_Op2); \
        (void)RT_CONCAT(OP_PARM_,a_Op3); \
        (void)RT_CONCAT(OP_PARM_,a_Op4); \
        (void)(a_fDisHints); \
        (void)(a_fIemHints); \
    } while (0)

#else
# define IEMOP_MNEMONIC(a_Stats, a_szMnemonic) IEMOP_INC_STATS(a_Stats)

# define IEMOP_MNEMONIC0EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_fDisHints, a_fIemHints) \
         IEMOP_MNEMONIC(a_Stats, a_szMnemonic)
# define IEMOP_MNEMONIC1EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_fDisHints, a_fIemHints) \
         IEMOP_MNEMONIC(a_Stats, a_szMnemonic)
# define IEMOP_MNEMONIC2EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_fDisHints, a_fIemHints) \
         IEMOP_MNEMONIC(a_Stats, a_szMnemonic)
# define IEMOP_MNEMONIC3EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_fDisHints, a_fIemHints) \
         IEMOP_MNEMONIC(a_Stats, a_szMnemonic)
# define IEMOP_MNEMONIC4EX(a_Stats, a_szMnemonic, a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_Op4, a_fDisHints, a_fIemHints) \
         IEMOP_MNEMONIC(a_Stats, a_szMnemonic)

#endif

#define IEMOP_MNEMONIC0(a_Form, a_Upper, a_Lower, a_fDisHints, a_fIemHints) \
    IEMOP_MNEMONIC0EX(a_Lower, \
                      #a_Lower, \
                      a_Form, a_Upper, a_Lower, a_fDisHints, a_fIemHints)
#define IEMOP_MNEMONIC1(a_Form, a_Upper, a_Lower, a_Op1, a_fDisHints, a_fIemHints) \
    IEMOP_MNEMONIC1EX(RT_CONCAT3(a_Lower,_,a_Op1), \
                      #a_Lower " " #a_Op1, \
                      a_Form, a_Upper, a_Lower, a_Op1, a_fDisHints, a_fIemHints)
#define IEMOP_MNEMONIC2(a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_fDisHints, a_fIemHints) \
    IEMOP_MNEMONIC2EX(RT_CONCAT5(a_Lower,_,a_Op1,_,a_Op2), \
                      #a_Lower " " #a_Op1 "," #a_Op2, \
                      a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_fDisHints, a_fIemHints)
#define IEMOP_MNEMONIC3(a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_fDisHints, a_fIemHints) \
    IEMOP_MNEMONIC3EX(RT_CONCAT7(a_Lower,_,a_Op1,_,a_Op2,_,a_Op3), \
                      #a_Lower " " #a_Op1 "," #a_Op2 "," #a_Op3, \
                      a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_fDisHints, a_fIemHints)
#define IEMOP_MNEMONIC4(a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_Op4, a_fDisHints, a_fIemHints) \
    IEMOP_MNEMONIC4EX(RT_CONCAT9(a_Lower,_,a_Op1,_,a_Op2,_,a_Op3,_,a_Op4), \
                      #a_Lower " " #a_Op1 "," #a_Op2 "," #a_Op3 "," #a_Op4, \
                      a_Form, a_Upper, a_Lower, a_Op1, a_Op2, a_Op3, a_Op4, a_fDisHints, a_fIemHints)

/** @} */


/** @name   Opcode Helpers.
 * @{
 */

/** The instruction requires a 186 or later. */
#if IEM_CFG_TARGET_CPU >= IEMTARGETCPU_186
# define IEMOP_HLP_MIN_186() do { } while (0)
#else
# define IEMOP_HLP_MIN_186() IEMOP_HLP_MIN_CPU(IEMTARGETCPU_186, true)
#endif

/** The instruction requires a 286 or later. */
#if IEM_CFG_TARGET_CPU >= IEMTARGETCPU_286
# define IEMOP_HLP_MIN_286() do { } while (0)
#else
# define IEMOP_HLP_MIN_286() IEMOP_HLP_MIN_CPU(IEMTARGETCPU_286, true)
#endif

/** The instruction requires a 386 or later. */
#if IEM_CFG_TARGET_CPU >= IEMTARGETCPU_386
# define IEMOP_HLP_MIN_386() do { } while (0)
#else
# define IEMOP_HLP_MIN_386() IEMOP_HLP_MIN_CPU(IEMTARGETCPU_386, true)
#endif

/** The instruction requires a 386 or later if the given expression is true. */
#if IEM_CFG_TARGET_CPU >= IEMTARGETCPU_386
# define IEMOP_HLP_MIN_386_EX(a_fOnlyIf) do { } while (0)
#else
# define IEMOP_HLP_MIN_386_EX(a_fOnlyIf) IEMOP_HLP_MIN_CPU(IEMTARGETCPU_386, a_fOnlyIf)
#endif

/** The instruction requires a 486 or later. */
#if IEM_CFG_TARGET_CPU >= IEMTARGETCPU_486
# define IEMOP_HLP_MIN_486() do { } while (0)
#else
# define IEMOP_HLP_MIN_486() IEMOP_HLP_MIN_CPU(IEMTARGETCPU_486, true)
#endif

/** The instruction requires a Pentium (586) or later. */
#if IEM_CFG_TARGET_CPU >= IEMTARGETCPU_PENTIUM
# define IEMOP_HLP_MIN_586() do { } while (0)
#else
# define IEMOP_HLP_MIN_586() IEMOP_HLP_MIN_CPU(IEMTARGETCPU_PENTIUM, true)
#endif

/** The instruction requires a PentiumPro (686) or later. */
#if IEM_CFG_TARGET_CPU >= IEMTARGETCPU_PPRO
# define IEMOP_HLP_MIN_686() do { } while (0)
#else
# define IEMOP_HLP_MIN_686() IEMOP_HLP_MIN_CPU(IEMTARGETCPU_PPRO, true)
#endif


/** The instruction raises an \#UD in real and V8086 mode. */
#define IEMOP_HLP_NO_REAL_OR_V86_MODE() \
    do \
    { \
        if (!IEM_IS_REAL_OR_V86_MODE(pVCpu)) { /* likely */ } \
        else IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/** This instruction raises an \#UD in real and V8086 mode or when not using a
 * 64-bit code segment when in long mode (applicable to all VMX instructions
 * except VMCALL).
 *
 * @todo r=bird: This is not recompiler friendly. The scenario with
 *       16-bit/32-bit code running in long mode doesn't fit at all.
 */
# define IEMOP_HLP_VMX_INSTR(a_szInstr, a_InsDiagPrefix) \
    do \
    { \
        if (   !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
            && (  !IEM_IS_LONG_MODE(pVCpu) \
                || IEM_IS_64BIT_CODE(pVCpu))) \
        { /* likely */ } \
        else \
        { \
            if (IEM_IS_REAL_OR_V86_MODE(pVCpu)) \
            { \
                pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = a_InsDiagPrefix##_RealOrV86Mode; \
                Log5((a_szInstr ": Real or v8086 mode -> #UD\n")); \
                IEMOP_RAISE_INVALID_OPCODE_RET(); \
            } \
            if (IEM_IS_LONG_MODE(pVCpu) && !IEM_IS_64BIT_CODE(pVCpu)) \
            { \
                pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = a_InsDiagPrefix##_LongModeCS; \
                Log5((a_szInstr ": Long mode without 64-bit code segment -> #UD\n")); \
                IEMOP_RAISE_INVALID_OPCODE_RUNTIME_RET(); /** @todo This doesn't work. */ \
            } \
        } \
    } while (0)

/** The instruction can only be executed in VMX operation (VMX root mode and
 * non-root mode).
 *
 *  @note Update IEM_VMX_IN_VMX_OPERATION if changes are made here.
 *
 * @todo r=bird: This is absolutely *INCORRECT* since IEM_VMX_IS_ROOT_MODE
 *       is a complicated runtime state (calls CPUMIsGuestInVmxRootMode), and
 *       not something we can decide while decoding.  Convert to an IEM_MC!
 */
# define IEMOP_HLP_IN_VMX_OPERATION(a_szInstr, a_InsDiagPrefix) \
    do \
    { \
        if (IEM_VMX_IS_ROOT_MODE(pVCpu)) { /* likely */ } \
        else \
        { \
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = a_InsDiagPrefix##_VmxRoot; \
            Log5((a_szInstr ": Not in VMX operation (root mode) -> #UD\n")); \
            IEMOP_RAISE_INVALID_OPCODE_RUNTIME_RET(); /** @todo This doesn't work. */ \
        } \
    } while (0)
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */

/** The instruction is not available in 64-bit mode, throw \#UD if we're in
 * 64-bit mode. */
#define IEMOP_HLP_NO_64BIT() \
    do \
    { \
        if (!IEM_IS_64BIT_CODE(pVCpu)) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/** The instruction is only available in 64-bit mode, throw \#UD if we're not in
 * 64-bit mode. */
#define IEMOP_HLP_ONLY_64BIT() \
    do \
    { \
        if (IEM_IS_64BIT_CODE(pVCpu)) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/** The instruction defaults to 64-bit operand size if 64-bit mode. */
#define IEMOP_HLP_DEFAULT_64BIT_OP_SIZE() \
    do \
    { \
        if (IEM_IS_64BIT_CODE(pVCpu)) \
            iemRecalEffOpSize64Default(pVCpu); \
    } while (0)

/** The instruction defaults to 64-bit operand size if 64-bit mode and intel
 *  CPUs ignore the operand size prefix complete (e.g. relative jumps). */
#define IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX() \
    do \
    { \
        if (IEM_IS_64BIT_CODE(pVCpu)) \
            iemRecalEffOpSize64DefaultAndIntelIgnoresOpSizePrefix(pVCpu); \
    } while (0)

/** The instruction has 64-bit operand size if 64-bit mode. */
#define IEMOP_HLP_64BIT_OP_SIZE() \
    do \
    { \
        if (IEM_IS_64BIT_CODE(pVCpu)) \
            pVCpu->iem.s.enmEffOpSize = pVCpu->iem.s.enmDefOpSize = IEMMODE_64BIT; \
    } while (0)

/** Only a REX prefix immediately preceding the first opcode byte takes
 * effect. This macro helps ensuring this as well as logging bad guest code.  */
#define IEMOP_HLP_CLEAR_REX_NOT_BEFORE_OPCODE(a_szPrf) \
    do \
    { \
        if (RT_UNLIKELY(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_REX)) \
        { \
            Log5((a_szPrf ": Overriding REX prefix at %RX16! fPrefixes=%#x\n", pVCpu->cpum.GstCtx.rip, pVCpu->iem.s.fPrefixes)); \
            pVCpu->iem.s.fPrefixes &= ~IEM_OP_PRF_REX_MASK; \
            pVCpu->iem.s.uRexB     = 0; \
            pVCpu->iem.s.uRexIndex = 0; \
            pVCpu->iem.s.uRexReg   = 0; \
            iemRecalEffOpSize(pVCpu); \
        } \
    } while (0)

/** The instruction ignores any REX.W/VEX.W prefix if not in 64-bit mode. */
#define IEMOP_HLP_IGNORE_VEX_W_PREFIX_IF_NOT_IN_64BIT() \
    do \
    { \
        if (!IEM_IS_64BIT_CODE(pVCpu)) \
            pVCpu->iem.s.fPrefixes &= ~IEM_OP_PRF_SIZE_REX_W; \
    } while (0)

/**
 * Done decoding.
 */
#define IEMOP_HLP_DONE_DECODING() \
    do \
    { \
        /*nothing for now, maybe later... */ \
    } while (0)

#define IEMOP_HLP_DONE_DECODING_EX(a_fFeature) \
    do \
    { \
        if (RT_LIKELY(IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature)) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_LOCK_PREFIX_RET(); \
    } while (0)

/**
 * Done decoding, raise \#UD exception if lock prefix present.
 */
#define IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX() \
    do \
    { \
        if (RT_LIKELY(!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK))) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_LOCK_PREFIX_RET(); \
    } while (0)

/**
 * Done decoding, raise \#UD exception if lock prefix present, or if the
 * a_fFeature is present in the guest CPU.
 */
#define IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(a_fFeature) \
    do \
    { \
        if (RT_LIKELY(   !(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK) \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature)) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_LOCK_PREFIX_RET(); \
    } while (0)

/**
 * Done decoding, raise \#UD exception if lock prefix present, or if the
 * a_fFeature is present in the guest CPU.
 */
#define IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX_2_OR(a_fFeature1, a_fFeature2) \
    do \
    { \
        if (RT_LIKELY(   !(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK) \
                      && (   IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature1 \
                          || IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature2) )) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_LOCK_PREFIX_RET(); \
    } while (0)


/**
 * Done decoding VEX instruction, raise \#UD exception if any lock, rex, repz,
 * repnz or size prefixes are present, if in real or v8086 mode, or if the
 * a_fFeature is not present in the guest CPU.
 */
#define IEMOP_HLP_DONE_VEX_DECODING_EX(a_fFeature) \
    do \
    { \
        if (RT_LIKELY(   !(  pVCpu->iem.s.fPrefixes \
                           & (IEM_OP_PRF_LOCK | IEM_OP_PRF_REPZ | IEM_OP_PRF_REPNZ | IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REX)) \
                      && !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature)) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Done decoding VEX instruction, raise \#UD exception if any lock, rex, repz,
 * repnz or size prefixes are present, or if in real or v8086 mode, or if the
 * a_fFeature is not present in the guest CPU.
 */
#define IEMOP_HLP_DONE_VEX_DECODING_L0_EX(a_fFeature) \
    do \
    { \
        if (RT_LIKELY(   !(  pVCpu->iem.s.fPrefixes \
                           & (IEM_OP_PRF_LOCK | IEM_OP_PRF_REPZ | IEM_OP_PRF_REPNZ | IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REX)) \
                      && !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
                      && pVCpu->iem.s.uVexLength == 0 \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature)) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Done decoding VEX instruction, raise \#UD exception if any lock, rex, repz,
 * repnz or size prefixes are present, or if in real or v8086 mode, or if the
 * a_fFeature is not present in the guest CPU.
 */
#define IEMOP_HLP_DONE_VEX_DECODING_L0_EX_2(a_fFeature, a_fFeature2) \
    do \
    { \
        if (RT_LIKELY(   !(  pVCpu->iem.s.fPrefixes \
                           & (IEM_OP_PRF_LOCK | IEM_OP_PRF_REPZ | IEM_OP_PRF_REPNZ | IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REX)) \
                      && !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
                      && pVCpu->iem.s.uVexLength == 0 \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature2)) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Done decoding VEX instruction, raise \#UD exception if any lock, rex, repz,
 * repnz or size prefixes are present, or if in real or v8086 mode, or if the
 * a_fFeature is not present in the guest CPU.
 */
#define IEMOP_HLP_DONE_VEX_DECODING_L1_EX(a_fFeature) \
    do \
    { \
        if (RT_LIKELY(   !(  pVCpu->iem.s.fPrefixes \
                           & (IEM_OP_PRF_LOCK | IEM_OP_PRF_REPZ | IEM_OP_PRF_REPNZ | IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REX)) \
                      && !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
                      && pVCpu->iem.s.uVexLength == 1 \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature)) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Done decoding VEX instruction, raise \#UD exception if any lock, rex, repz,
 * repnz or size prefixes are present, or if VEX.W is one, or if in real or
 * v8086 mode, or if the a_fFeature is not present in the guest CPU.
 */
#define IEMOP_HLP_DONE_VEX_DECODING_W0_EX(a_fFeature) \
    do \
    { \
        if (RT_LIKELY(   !(  pVCpu->iem.s.fPrefixes \
                           & (IEM_OP_PRF_LOCK | IEM_OP_PRF_REPZ | IEM_OP_PRF_REPNZ | IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REX \
                              | IEM_OP_PRF_SIZE_REX_W /*VEX.W*/)) \
                      && !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature)) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Done decoding VEX instruction, raise \#UD exception if any lock, rex, repz,
 * repnz or size prefixes are present, or if the VEX.VVVV field doesn't indicate
 * register 0, if in real or v8086 mode, or if the a_fFeature is not present in
 * the guest CPU.
 */
#define IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(a_fFeature) \
    do \
    { \
        if (RT_LIKELY(   !(  pVCpu->iem.s.fPrefixes \
                           & (IEM_OP_PRF_LOCK | IEM_OP_PRF_REPZ | IEM_OP_PRF_REPNZ | IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REX)) \
                      && !pVCpu->iem.s.uVex3rdReg \
                      && !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature )) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Done decoding VEX instruction, raise \#UD exception if any lock, rex, repz,
 * repnz or size prefixes are present, or if VEX.W is one, or if the VEX.VVVV field doesn't indicate
 * register 0, if in real or v8086 mode, or if the a_fFeature is not present in
 * the guest CPU.
 */
#define IEMOP_HLP_DONE_VEX_DECODING_W0_AND_NO_VVVV_EX(a_fFeature) \
    do \
    { \
        if (RT_LIKELY(   !(  pVCpu->iem.s.fPrefixes \
                           & (IEM_OP_PRF_LOCK | IEM_OP_PRF_REPZ | IEM_OP_PRF_REPNZ | IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REX \
                              | IEM_OP_PRF_SIZE_REX_W /*VEX.W*/)) \
                      && !pVCpu->iem.s.uVex3rdReg \
                      && !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature )) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Done decoding VEX, no V, L=0.
 * Raises \#UD exception if rex, rep, opsize or lock prefixes are present, if
 * we're in real or v8086 mode, if VEX.V!=0xf, if VEX.L!=0, or if the a_fFeature
 * is not present in the guest CPU.
 */
#define IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(a_fFeature) \
    do \
    { \
        if (RT_LIKELY(   !(  pVCpu->iem.s.fPrefixes \
                           & (IEM_OP_PRF_LOCK | IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REPZ | IEM_OP_PRF_REPNZ | IEM_OP_PRF_REX)) \
                      && pVCpu->iem.s.uVexLength == 0 \
                      && pVCpu->iem.s.uVex3rdReg == 0 \
                      && !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature )) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Done decoding VEX, no V, L=0.
 * Raises \#UD exception if rex, rep, opsize or lock prefixes are present, if
 * we're in real or v8086 mode, if VEX.V!=0xf, if VEX.L!=0, or if the a_fFeature or a_fFeature2
 * is not present in the guest CPU.
 */
#define IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX_2(a_fFeature, a_fFeature2) \
    do \
    { \
        if (RT_LIKELY(   !(  pVCpu->iem.s.fPrefixes \
                           & (IEM_OP_PRF_LOCK | IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REPZ | IEM_OP_PRF_REPNZ | IEM_OP_PRF_REX)) \
                      && pVCpu->iem.s.uVexLength == 0 \
                      && pVCpu->iem.s.uVex3rdReg == 0 \
                      && !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature2)) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Done decoding VEX, no V, L=1.
 * Raises \#UD exception if rex, rep, opsize or lock prefixes are present, if
 * we're in real or v8086 mode, if VEX.V!=0xf, if VEX.L!=1, or if the a_fFeature
 * is not present in the guest CPU.
 */
#define IEMOP_HLP_DONE_VEX_DECODING_L1_AND_NO_VVVV_EX(a_fFeature) \
    do \
    { \
        if (RT_LIKELY(   !(  pVCpu->iem.s.fPrefixes \
                           & (IEM_OP_PRF_LOCK | IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REPZ | IEM_OP_PRF_REPNZ | IEM_OP_PRF_REX)) \
                      && pVCpu->iem.s.uVexLength == 1 \
                      && pVCpu->iem.s.uVex3rdReg == 0 \
                      && !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature )) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Done decoding VEX, L=0 and W=0.
 * Raises \#UD exception if rex, rep, opsize or lock prefixes are present,
 * if we're in real or v8086 mode, if VEX.L!=0, if VEX.W!=0, or if the
 * a_fFeature is not present in the guest CPU.
 */
#define IEMOP_HLP_DONE_VEX_DECODING_L0_AND_W0_EX(a_fFeature) \
    do \
    { \
        if (RT_LIKELY(   !(  pVCpu->iem.s.fPrefixes \
                           & (IEM_OP_PRF_LOCK | IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REPZ | IEM_OP_PRF_REPNZ | IEM_OP_PRF_REX \
                              | IEM_OP_PRF_SIZE_REX_W /*VEX.W*/)) \
                      && pVCpu->iem.s.uVexLength == 0 \
                      && !IEM_IS_REAL_OR_V86_MODE(pVCpu) \
                      && IEM_GET_GUEST_CPU_FEATURES(pVCpu)->a_fFeature )) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)


#define IEMOP_HLP_DECODED_NL_1(a_uDisOpNo, a_fIemOpFlags, a_uDisParam0, a_fDisOpType) \
    do \
    { \
        if (RT_LIKELY(!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK))) \
        { /* likely */ } \
        else \
        { \
            NOREF(a_uDisOpNo); NOREF(a_fIemOpFlags); NOREF(a_uDisParam0); NOREF(a_fDisOpType); \
            IEMOP_RAISE_INVALID_LOCK_PREFIX_RET(); \
        } \
    } while (0)
#define IEMOP_HLP_DECODED_NL_2(a_uDisOpNo, a_fIemOpFlags, a_uDisParam0, a_uDisParam1, a_fDisOpType) \
    do \
    { \
        if (RT_LIKELY(!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK))) \
        { /* likely */ } \
        else \
        { \
            NOREF(a_uDisOpNo); NOREF(a_fIemOpFlags); NOREF(a_uDisParam0); NOREF(a_uDisParam1); NOREF(a_fDisOpType); \
            IEMOP_RAISE_INVALID_LOCK_PREFIX_RET(); \
        } \
    } while (0)

/**
 * Done decoding, raise \#UD exception if any lock, repz or repnz prefixes
 * are present.
 */
#define IEMOP_HLP_DONE_DECODING_NO_LOCK_REPZ_OR_REPNZ_PREFIXES() \
    do \
    { \
        if (RT_LIKELY(!(pVCpu->iem.s.fPrefixes & (IEM_OP_PRF_LOCK | IEM_OP_PRF_REPNZ | IEM_OP_PRF_REPZ)))) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Done decoding, raise \#UD exception if any operand-size override, repz or repnz
 * prefixes are present.
 */
#define IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES() \
    do \
    { \
        if (RT_LIKELY(!(pVCpu->iem.s.fPrefixes & (IEM_OP_PRF_SIZE_OP | IEM_OP_PRF_REPNZ | IEM_OP_PRF_REPZ)))) \
        { /* likely */ } \
        else \
            IEMOP_RAISE_INVALID_OPCODE_RET(); \
    } while (0)

/**
 * Used the threaded code generator to check if a jump stays within the same
 * page in 64-bit code.
 */
#define IEMOP_HLP_PC64_IS_JMP_REL_WITHIN_PAGE(a_offDisp) \
     (   ((pVCpu->cpum.GstCtx.rip + IEM_GET_INSTR_LEN(pVCpu) + (a_offDisp)) >> GUEST_PAGE_SHIFT) \
      == (pVCpu->cpum.GstCtx.rip >> GUEST_PAGE_SHIFT))

RTGCPTR iemOpHlpCalcRmEffAddrJmp(PVMCPUCC pVCpu, uint8_t bRm, uint32_t cbImmAndRspOffset) IEM_NOEXCEPT_MAY_LONGJMP;
RTGCPTR iemOpHlpCalcRmEffAddrJmpEx(PVMCPUCC pVCpu, uint8_t bRm, uint32_t cbImmAndRspOffset, uint64_t *puInfo) IEM_NOEXCEPT_MAY_LONGJMP;

/** @}  */

#endif /* !VMM_INCLUDED_SRC_VMMAll_target_x86_IEMOpHlp_x86_h */
