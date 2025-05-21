/** @file
 * IPRT - ARM Specific Assembly Functions.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef IPRT_INCLUDED_asm_arm_h
#define IPRT_INCLUDED_asm_arm_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#if !defined(RT_ARCH_ARM64) && !defined(RT_ARCH_ARM32)
# error "Not on ARM64 or ARM32"
#endif

#if defined(_MSC_VER) && RT_INLINE_ASM_USES_INTRIN
/* Emit the intrinsics at all optimization levels. */
# include <iprt/sanitized/intrin.h>
# pragma intrinsic(_ReadStatusReg)
# pragma intrinsic(_WriteStatusReg)
# pragma intrinsic(_disable)
# pragma intrinsic(_enable)
# pragma intrinsic(__hvc)

/*
 * MSVC insists on having these defined using ARM64_SYSREG or it will
 * fail to compile with "error C2284: "_ReadStatusReg": invalid argument for internal function, parameter 1"
 * if we use our own definitions from iprt/armv8.h
 *
 * The reason for this, is that ARM64_SYSREG masks off the top bit (bit 15)
 * whereas our macro doesn't.  So the reason is probably the implicitness
 * of the top bit in the MRS/MSR encoding.
 */
# define ARM64_SYSREG_DAIF          ARM64_SYSREG(3, 3,  4, 2, 1)
# define ARM64_SYSREG_CNTFRQ_EL0    ARM64_SYSREG(3, 3, 14, 0, 0)
# define ARM64_SYSREG_CNTCVT_EL0    ARM64_SYSREG(3, 3, 14, 0, 2)
# define ARM64_SYSREG_TPIDRRO_EL0   ARM64_SYSREG(3, 3, 13, 0, 3)
#endif

/** @defgroup grp_rt_asm_arm  ARM Specific ASM Routines
 * @ingroup grp_rt_asm
 * @{
 */

/**
 * Gets the content of the CNTVCT_EL0 (or CNTPCT) register.
 *
 * @returns CNTVCT_EL0 value.
 * @note    We call this TSC to better fit in with existing x86/amd64 based code.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(uint64_t) ASMReadTSC(void);
#else
DECLINLINE(uint64_t) ASMReadTSC(void)
{
# if RT_INLINE_ASM_GNU_STYLE
    uint64_t u64;
#  ifdef RT_ARCH_ARM64
    __asm__ __volatile__("Lstart_ASMReadTSC_%=:\n\t"
                         "isb\n\t"
                         "mrs %0, CNTVCT_EL0\n\t"
                         : "=r" (u64));
#  else
    uint32_t u32Spill;
    uint32_t u32Comp;
    __asm__ __volatile__("Lstart_ASMReadTSC_%=:\n\t"
                         "isb\n"
                         "Ltry_again_ASMReadTSC_%=:\n\t"
                         "mrrc p15, 0, %[uSpill], %H[uRet],   c14\n\t"  /* CNTPCT high into uRet.hi */
                         "mrrc p15, 0, %[uRet],   %[uSpill],  c14\n\t"  /* CNTPCT low  into uRet.lo */
                         "mrrc p15, 0, %[uSpill], %[uHiComp], c14\n\t"  /* CNTPCT high into uHiComp */
                         "cmp  %H[uRet], %[uHiComp]\n\t"
                         "b.eq Ltry_again_ASMReadTSC_%=\n\t"            /* Redo if high value changed. */
                         : [uRet] "=r" (u64)
                         , "=r" (uHiComp)
                         , "=r" (uSpill));
#  endif
    return u64;

#elif RT_INLINE_ASM_USES_INTRIN
    return (uint64_t)_ReadStatusReg(ARM64_SYSREG_CNTCVT_EL0);
# else
#  error "Unsupported compiler"
# endif
}
#endif


/**
 * Gets the content of the CNTFRQ_EL0 register.
 *
 * @returns CNTFRQ_EL0 value.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(uint64_t) ASMReadCntFrqEl0(void);
#else
DECLINLINE(uint64_t) ASMReadCntFrqEl0(void)
{
# if RT_INLINE_ASM_GNU_STYLE
    uint64_t u64;
#  ifdef RT_ARCH_ARM64
    __asm__ __volatile__("Lstart_ASMReadCntFrqEl0_%=:\n\t"
                         "isb\n\t"
                         "mrs %0, CNTFRQ_EL0\n\t"
                         : "=r" (u64));
#  else
    u64 = 0;
    __asm__ __volatile__("Lstart_ASMReadCntFrqEl0_%=:\n\t"
                         "isb\n\t"
                         "mrc p15, 0, %[uRet], c14, 0, 0\n\t"  /* CNTFRQ */
                         : [uRet] "=r" (u64));
#  endif
    return u64;

#elif RT_INLINE_ASM_USES_INTRIN
    return (uint64_t)_ReadStatusReg(ARM64_SYSREG_CNTFRQ_EL0);
# else
#  error "Unsupported compiler"
# endif
}
#endif


/**
 * Enables interrupts (IRQ and FIQ).
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMIntEnable(void);
#else
DECLINLINE(void) ASMIntEnable(void)
{
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef RT_ARCH_ARM64
    __asm__ __volatile__("Lstart_ASMIntEnable_%=:\n\t"
                         "msr daifclr, #0xf\n\t");
#  else
    RTCCUINTREG uFlags;
    __asm__ __volatile__("Lstart_ASMIntEnable_%=:\n\t"
                         "mrs %0, cpsr\n\t"
                         "bic %0, %0, #0xc0\n\t"
                         "msr cpsr_c, %0\n\t"
                         : "=r" (uFlags));
#  endif
# elif RT_INLINE_ASM_USES_INTRIN
    _enable();
# else
#  error "Unsupported compiler"
# endif
}
#endif


/**
 * Disables interrupts (IRQ and FIQ).
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMIntDisable(void);
#else
DECLINLINE(void) ASMIntDisable(void)
{
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef RT_ARCH_ARM64
    __asm__ __volatile__("Lstart_ASMIntDisable_%=:\n\t"
                         "msr daifset, #0xf\n\t" :); /* The ':' is to make clang 16 happy. */
#  else
    RTCCUINTREG uFlags;
    __asm__ __volatile__("Lstart_ASMIntDisable_%=:\n\t"
                         "mrs %0, cpsr\n\t"
                         "orr %0, %0, #0xc0\n\t"
                         "msr cpsr_c, %0\n\t"
                         : "=r" (uFlags));
#  endif
# elif RT_INLINE_ASM_USES_INTRIN
    _disable();
# else
#  error "Unsupported compiler"
# endif
}
#endif


/**
 * Disables interrupts and returns previous uFLAGS.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTCCUINTREG) ASMIntDisableFlags(void);
#else
DECLINLINE(RTCCUINTREG) ASMIntDisableFlags(void)
{
    RTCCUINTREG uFlags;
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef RT_ARCH_ARM64
    __asm__ __volatile__("Lstart_ASMIntDisableFlags_%=:\n\t"
                         "mrs %[uRet], daif\n\t"
                         "msr daifset, #0xf\n\t"
                         : [uRet] "=r" (uFlags));
#  else
    RTCCUINTREG uNewFlags;
    __asm__ __volatile__("Lstart_ASMIntDisableFlags_%=:\n\t"
                         "mrs %0, cpsr\n\t"
                         "orr %1, %0, #0xc0\n\t"
                         "msr cpsr_c, %1\n\t"
                         : "=r" (uFlags)
                         , "=r" (uNewFlags));
#  endif
# elif RT_INLINE_ASM_USES_INTRIN
    uFlags = _ReadStatusReg(ARM64_SYSREG_DAIF);
    _disable();
# else
#  error "Unsupported compiler"
# endif
    return uFlags;
}
#endif


/**
 * Get the CPSR/PSTATE register.
 * @returns CPSR/PSTATE.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTCCUINTREG) ASMGetFlags(void);
#else
DECLINLINE(RTCCUINTREG) ASMGetFlags(void)
{
    RTCCUINTREG uFlags;
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef RT_ARCH_ARM64
    __asm__ __volatile__("Lstart_ASMGetFlags_%=:\n\t"
                         "isb\n\t"
                         "mrs %0, daif\n\t"
                         : "=r" (uFlags));
#  else
#   error "Implementation required for arm32"
#  endif
# elif RT_INLINE_ASM_USES_INTRIN
    uFlags = _ReadStatusReg(ARM64_SYSREG_DAIF);
# else
#  error "Unsupported compiler"
# endif
    return uFlags;
}
#endif


/**
 * Get the CPSR/PSTATE register.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMSetFlags(RTCCUINTREG uFlags);
#else
DECLINLINE(void) ASMSetFlags(RTCCUINTREG uFlags)
{
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef RT_ARCH_ARM64
    __asm__ __volatile__("Lstart_ASMSetFlags_%=:\n\t"
                         "isb\n\t"
                         "msr daif, %[uFlags]\n\t"
                         : : [uFlags] "r" (uFlags));
#  else
#   error "Implementation required for arm32"
#  endif
# elif RT_INLINE_ASM_USES_INTRIN
    _WriteStatusReg(ARM64_SYSREG_DAIF, uFlags);
# else
#  error "Unsupported compiler"
# endif
}
#endif


/**
 * Are interrupts enabled?
 *
 * @returns true / false.
 * @arm     This is ambigious, as there are two interrupt related masks on arm,
 *          one for IRQ and one for FIQ.  This function currently considers
 *          interrupts enabled if either IRQ or FIQ are unmasked (windows seems
 *          to leave FIQ masked).
 */
DECLINLINE(bool) ASMIntAreEnabled(void)
{
    /** @todo should we perhaps just check the IRQ bit? */
    return (ASMGetFlags() & 0xc0 /* IRQ and FIQ bits */) != 0xc0;
}


#if 0 /* Later */
/**
 * Issue HVC call with a single argument.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMHvc(uint16_t u16Imm, uint32_t u32Arg0);
#else
DECLINLINE(void) ASMHvc(uint16_t u16Imm, uint32_t u32Arg0)
{
# if RT_INLINE_ASM_GNU_STYLE
#  error "Later"
# elif RT_INLINE_ASM_USES_INTRIN
    __hvc(u16Imm, u32Val);
# else
#  error "Unsupported compiler"
# endif
}
#endif
#endif


/**
 * Halts the CPU until interrupted.
 */
#if RT_INLINE_ASM_EXTERNAL || defined(_MSC_VER)
DECLASM(void) ASMHalt(void);
#else
DECLINLINE(void) ASMHalt(void)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("Lstart_ASMHalt_%=:\n\t"
                          "wfi\n\t"); /* wait for interrupt */
# else
#  error "Unsupported compiler"
# endif
}
#endif

#if 0
/**
 * Gets the CPU ID of the current CPU.
 *
 * @returns the CPU ID.
 * @note the name of this method is a bit misleading but serves the purpose
 *       and prevents #ifdef orgies in other places.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(uint8_t) ASMGetApicId(void);
#else
DECLINLINE(uint8_t) ASMGetApicId(void)
{
# if RT_INLINE_ASM_GNU_STYLE
    RTCCUINTREG uCpuId;
    __asm__ ("Lstart_ASMGetApicId_%=:\n\t"
             "mrc p15, 0, %0, c0, c0, 5\n\t" /*  CPU ID Register, privileged */
             : "=r" (uCpuId));
    return uCpuId;
# else
#  error "Unsupported compiler"
# endif
}
#endif
#endif

#if 0

/**
 * Invalidate page.
 *
 * @param   pv      Address of the page to invalidate.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMInvalidatePage(void *pv);
#else
DECLINLINE(void) ASMInvalidatePage(void *pv)
{
# if RT_INLINE_ASM_GNU_STYLE

# else
#  error "Unsupported compiler"
# endif
}
#endif


/**
 * Write back the internal caches and invalidate them.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMWriteBackAndInvalidateCaches(void);
#else
DECLINLINE(void) ASMWriteBackAndInvalidateCaches(void)
{
# if RT_INLINE_ASM_GNU_STYLE

# else
#  error "Unsupported compiler"
# endif
}
#endif


/**
 * Invalidate internal and (perhaps) external caches without first
 * flushing dirty cache lines. Use with extreme care.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMInvalidateInternalCaches(void);
#else
DECLINLINE(void) ASMInvalidateInternalCaches(void)
{
# if RT_INLINE_ASM_GNU_STYLE

# else
#  error "Unsupported compiler"
# endif
}
#endif

#endif


/**
 * Get the TPIDRRO_EL0 register.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTCCUINTREG) ASMGetThreadIdRoEL0(void);
#else
DECLINLINE(RTCCUINTREG) ASMGetThreadIdRoEL0(void)
{
# if RT_INLINE_ASM_GNU_STYLE
    RTCCUINTREG uRet;
#  ifdef RT_ARCH_ARM64
    __asm__ __volatile__("Lstart_ASMGetThreadIdEl0_%=:\n\t"
                         "mrs %[uRet], TPIDRRO_EL0\n\t"
                         : [uRet] "=r" (uRet));
#  else
    __asm__ __volatile__("Lstart_ASMGetThreadIdEl0_%=:\n\t"
                         "mrc p15, 0, %[uRet], c13, c0, 3\n\t" /* TPIDRURO */
                         : [uRet] "=r" (uRet));
#  endif
    return uRet;
# elif RT_INLINE_ASM_USES_INTRIN
    return _ReadStatusReg(ARM64_SYSREG_TPIDRRO_EL0);
# else
#  error "Unsupported compiler"
# endif
}
#endif


/** @} */
#endif /* !IPRT_INCLUDED_asm_arm_h */

