/* $Id: iprt.h 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Internal header for miscellaneous global defs and types.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_INTERNAL_iprt_h
#define IPRT_INCLUDED_INTERNAL_iprt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>

/** @def RT_EXPORT_SYMBOL
 * This define is really here just for the linux kernel.
 * @param   Name        The symbol name.
 */
#if defined(RT_OS_LINUX) \
 && defined(IN_RING0) \
 && defined(MODULE) \
 && !defined(RT_NO_EXPORT_SYMBOL)
# define bool linux_bool /* see r0drv/linux/the-linux-kernel.h */
# include <linux/version.h>
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
#  include <generated/autoconf.h>
# else
#  ifndef AUTOCONF_INCLUDED
#   include <linux/autoconf.h>
#  endif
# endif
# if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#  if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 71)
#   include <linux/modversions.h>
#  endif
# endif
# include <linux/module.h>
# undef bool
# define RT_EXPORT_SYMBOL(Name) EXPORT_SYMBOL(Name)
#else
# define RT_EXPORT_SYMBOL(Name) extern int g_rtExportSymbolDummyVariable
#endif

/** @def RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL
 * Creates symbol alias and export a RT_NOCRT symbol.
 *
 * For targets using ELF this macro generates weak symbol aliases, for the other
 * object formats the makefile scans source files for this macro and produces
 * separate object files with the aliases.
 *
 * @param   a_Name      The CRT function or variable name.
 *
 * @todo    Does not support ELF targets using underscore prefixed symbols.
 */
#if defined(__ELF__) && defined(RT_WITH_NOCRT_ALIASES) && !defined(RT_WITHOUT_NOCRT_ALIASES)
# ifdef RT_WITH_NOCRT_UNDERSCORE_ALIASES
#  define RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(a_Name) RT_EXPORT_SYMBOL(a_Name) \
    __asm__(".weak " #a_Name "\n\t" \
            ".set  " #a_Name "," RT_NOCRT_STR(a_Name) "\n\t" \
            ".weak _" #a_Name "\n\t" \
            ".set  _" #a_Name "," RT_NOCRT_STR(a_Name) "\n\t")
# else
#  define RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(a_Name) RT_EXPORT_SYMBOL(a_Name) \
    __asm__(".weak " #a_Name "\n\t" \
            ".set  " #a_Name "," RT_NOCRT_STR(a_Name) "\n\t")
# endif
#else
# define RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(a_Name) RT_EXPORT_SYMBOL(a_Name)
#endif

/** @def RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL_WITHOUT_UNDERSCORE
 * Variant of RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL that omits the
 * underscore-prefixed variant of the symbol.
 *
 * @param   a_Name      The CRT function or variable name.
 */
#if defined(__ELF__) && defined(RT_WITH_NOCRT_ALIASES) && !defined(RT_WITHOUT_NOCRT_ALIASES)
# define RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL_WITHOUT_UNDERSCORE(a_Name) RT_EXPORT_SYMBOL(a_Name) \
    __asm__(".weak " #a_Name "\n\t" \
            ".set  " #a_Name "," RT_NOCRT_STR(a_Name) "\n\t")
#else
# define RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL_WITHOUT_UNDERSCORE(a_Name) RT_EXPORT_SYMBOL(a_Name)
#endif


/** @def RT_MORE_STRICT
 * Enables more assertions in IPRT. */
#if !defined(RT_MORE_STRICT) && (defined(DEBUG) || defined(RT_STRICT) || defined(DOXYGEN_RUNNING)) && !defined(RT_OS_WINDOWS) /** @todo enable on windows after testing */
# define RT_MORE_STRICT
#endif

/** @def RT_ASSERT_PREEMPT_CPUID_VAR
 * Partner to RT_ASSERT_PREEMPT_CPUID_VAR. Declares and initializes a variable
 * idAssertCpu to NIL_RTCPUID if preemption is enabled and to RTMpCpuId if
 * disabled.  When RT_MORE_STRICT isn't defined it declares an uninitialized
 * dummy variable.
 *
 * Requires iprt/mp.h and iprt/asm.h.
 */
/** @def RT_ASSERT_PREEMPT_CPUID
 * Asserts that we didn't change CPU since RT_ASSERT_PREEMPT_CPUID_VAR if
 * preemption is disabled.  Will also detect changes in preemption
 * disable/enable status.  This is a noop when RT_MORE_STRICT isn't defined. */
#ifdef RT_MORE_STRICT
# define RT_ASSERT_PREEMPT_CPUID_VAR() \
    RTCPUID const idAssertCpu = RTThreadPreemptIsEnabled(NIL_RTTHREAD) ? NIL_RTCPUID : RTMpCpuId()
# define RT_ASSERT_PREEMPT_CPUID() \
   do \
   { \
        RTCPUID const idAssertCpuNow = RTThreadPreemptIsEnabled(NIL_RTTHREAD) ? NIL_RTCPUID : RTMpCpuId(); \
        AssertMsg(idAssertCpu == idAssertCpuNow,  ("%#x, %#x\n", idAssertCpu, idAssertCpuNow)); \
   } while (0)

#else
# define RT_ASSERT_PREEMPT_CPUID_VAR()  RTCPUID idAssertCpuDummy
# define RT_ASSERT_PREEMPT_CPUID()      NOREF(idAssertCpuDummy)
#endif

/** @def RT_ASSERT_PREEMPT_CPUID_SPIN_ACQUIRED
 * Extended version of RT_ASSERT_PREEMPT_CPUID for use before
 * RTSpinlockAcquired* returns.  This macro works the idCpuOwner and idAssertCpu
 * members of the spinlock instance data. */
#ifdef RT_MORE_STRICT
# define RT_ASSERT_PREEMPT_CPUID_SPIN_ACQUIRED(pThis) \
    do \
    { \
        RTCPUID const idAssertCpuNow = RTMpCpuId(); \
        AssertMsg(idAssertCpu == idAssertCpuNow || idAssertCpu == NIL_RTCPUID,  ("%#x, %#x\n", idAssertCpu, idAssertCpuNow)); \
        (pThis)->idAssertCpu = idAssertCpu; \
        (pThis)->idCpuOwner  = idAssertCpuNow; \
    } while (0)
#else
# define RT_ASSERT_PREEMPT_CPUID_SPIN_ACQUIRED(pThis)   NOREF(idAssertCpuDummy)
#endif

/** @def RT_ASSERT_PREEMPT_CPUID_SPIN_RELEASE_VARS
 * Extended version of RT_ASSERT_PREEMPT_CPUID_VAR for use with
 * RTSpinlockRelease* returns. */
#ifdef RT_MORE_STRICT
# define RT_ASSERT_PREEMPT_CPUID_SPIN_RELEASE_VARS()    RTCPUID idAssertCpu
#else
# define RT_ASSERT_PREEMPT_CPUID_SPIN_RELEASE_VARS()    RTCPUID idAssertCpuDummy
#endif

/** @def RT_ASSERT_PREEMPT_CPUID_SPIN_RELEASE
 * Extended version of RT_ASSERT_PREEMPT_CPUID for use in RTSpinlockRelease*
 * before calling the native API for releasing the spinlock.  It must be
 * teamed up with RT_ASSERT_PREEMPT_CPUID_SPIN_ACQUIRED. */
#ifdef RT_MORE_STRICT
# define RT_ASSERT_PREEMPT_CPUID_SPIN_RELEASE(pThis) \
    do \
    { \
        RTCPUID const idCpuOwner     = (pThis)->idCpuOwner; \
        RTCPUID const idAssertCpuNow = RTMpCpuId(); \
        AssertMsg(idCpuOwner == idAssertCpuNow,  ("%#x, %#x\n", idCpuOwner, idAssertCpuNow)); \
        (pThis)->idCpuOwner  = NIL_RTCPUID; \
        idAssertCpu = (pThis)->idAssertCpu; \
        (pThis)->idAssertCpu = NIL_RTCPUID; \
    } while (0)
#else
# define RT_ASSERT_PREEMPT_CPUID_SPIN_RELEASE(pThis)    NOREF(idAssertCpuDummy)
#endif

/** @def RT_ASSERT_PREEMPT_CPUID_DISABLE
 * For use in RTThreadPreemptDisable implementations after having disabled
 * preemption.  Requires iprt/mp.h. */
#ifdef RT_MORE_STRICT
# define RT_ASSERT_PREEMPT_CPUID_DISABLE(pStat) \
    do \
    { \
        Assert((pStat)->idCpu == NIL_RTCPUID); \
        (pStat)->idCpu = RTMpCpuId(); \
    } while (0)
#else
# define RT_ASSERT_PREEMPT_CPUID_DISABLE(pStat) \
    Assert((pStat)->idCpu == NIL_RTCPUID)
#endif

/** @def RT_ASSERT_PREEMPT_CPUID_RESTORE
 * For use in RTThreadPreemptRestore implementations before restoring
 * preemption.  Requires iprt/mp.h. */
#ifdef RT_MORE_STRICT
# define RT_ASSERT_PREEMPT_CPUID_RESTORE(pStat) \
    do \
    { \
        RTCPUID const idAssertCpuNow = RTMpCpuId(); \
        AssertMsg((pStat)->idCpu == idAssertCpuNow,  ("%#x, %#x\n", (pStat)->idCpu, idAssertCpuNow)); \
        (pStat)->idCpu = NIL_RTCPUID; \
    } while (0)
#else
# define RT_ASSERT_PREEMPT_CPUID_RESTORE(pStat)         do { } while (0)
#endif


/** @def RT_ASSERT_INTS_ON
 * Asserts that interrupts are disabled when RT_MORE_STRICT is defined. */
#ifdef RT_MORE_STRICT
# if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
#  define RT_ASSERT_INTS_ON()           Assert(ASMIntAreEnabled())
# else /* PORTME: Add architecture/platform specific test. */
#  define RT_ASSERT_INTS_ON()           Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD))
# endif
#else
# define RT_ASSERT_INTS_ON()            do { } while (0)
#endif

/** @def RT_ASSERT_PREEMPTIBLE
 * Asserts that preemption hasn't been disabled (using
 * RTThreadPreemptDisable) when RT_MORE_STRICT is defined. */
#ifdef RT_MORE_STRICT
# define RT_ASSERT_PREEMPTIBLE()        Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD))
#else
# define RT_ASSERT_PREEMPTIBLE()        do { } while (0)
#endif


RT_C_DECLS_BEGIN

#ifdef RT_OS_OS2
uint32_t rtR0SemWaitOs2ConvertTimeout(uint32_t fFlags, uint64_t uTimeout);
#endif

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_iprt_h */

