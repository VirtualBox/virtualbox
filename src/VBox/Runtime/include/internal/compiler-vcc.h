/* $Id: compiler-vcc.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal header for the Visual C++ Compiler Support Code.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_INTERNAL_compiler_vcc_h
#define IPRT_INCLUDED_INTERNAL_compiler_vcc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

/** @name Special sections.
 * @{
 */

#ifdef IPRT_COMPILER_VCC_WITH_C_INIT_TERM_SECTIONS
# pragma section(".CRT$XIA",  read, long)   /* start C initializers */
# pragma section(".CRT$XIAA", read, long)
# pragma section(".CRT$XIZ",  read, long)

# pragma section(".CRT$XPA",  read, long)   /* start early C terminators  */
# pragma section(".CRT$XPAA", read, long)
# pragma section(".CRT$XPZ",  read, long)

# pragma section(".CRT$XTA",  read, long)   /* start C terminators  */
# pragma section(".CRT$XTAA", read, long)
# pragma section(".CRT$XTZ",  read, long)
# define IPRT_COMPILER_TERM_CALLBACK(a_fn) \
    __declspec(allocate(".CRT$XTAA")) PFNRT RT_CONCAT(g_rtVccTermCallback_, a_fn) = a_fn
#endif

#ifdef IPRT_COMPILER_VCC_WITH_CPP_INIT_SECTIONS
# pragma warning(disable:5247)  /* warning C5247: section '.CRT$XCA' is reserved for C++ dynamic initialization. Manually creating the section will interfere with C++ dynamic initialization and may lead to undefined behavior */
# pragma warning(disable:5248)  /* warning C5248: section '.CRT$XCA' is reserved for C++ dynamic initialization. Variables manually put into the section may be optimized out and their order relative to compiler generated dynamic initializers is unspecified */
# pragma section(".CRT$XCA",  read, long)   /* start C++ initializers */
# pragma section(".CRT$XCAA", read, long)
# pragma section(".CRT$XCZ",  read, long)
#endif

#ifdef IPRT_COMPILER_VCC_WITH_RTC_INIT_TERM_SECTIONS
# pragma section(".rtc$IAA",  read, long)   /* start RTC initializers */
# pragma section(".rtc$IZZ",  read, long)

# pragma section(".rtc$TAA",  read, long)   /* start RTC terminators */
# pragma section(".rtc$TZZ",  read, long)
#endif

#ifdef IPRT_COMPILER_VCC_WITH_TLS_CALLBACK_SECTIONS
# pragma section(".CRT$XLA",  read, long)   /* start TLS callback */
# pragma section(".CRT$XLAA", read, long)
# pragma section(".CRT$XLZ",  read, long)

/** @todo what about .CRT$XDA? Dynamic TLS initializers. */
#endif

#ifdef IPRT_COMPILER_VCC_WITH_TLS_DATA_SECTIONS
# pragma section(".tls",      read, long)   /* start TLS callback */
# pragma section(".tls$ZZZ",  read, long)

/** @todo what about .CRT$XDA? Dynamic TLS initializers. */
#endif

/** @} */


RT_C_DECLS_BEGIN

extern unsigned _fltused;

void rtVccInitSecurityCookie(void) RT_NOEXCEPT;
void rtVccWinInitBssOnNt3(void *pvImageBase) RT_NOEXCEPT;
void rtVccWinInitProcExecPath(void) RT_NOEXCEPT;
int  rtVccInitializersRunInit(void) RT_NOEXCEPT;
void rtVccInitializersRunTerm(void) RT_NOEXCEPT;
void rtVccTermRunAtExit(void) RT_NOEXCEPT;

struct _CONTEXT;
void rtVccCheckContextFailed(struct _CONTEXT *pCpuCtx);

#ifdef _CONTROL_FLOW_GUARD
DECLASM(void)     __guard_check_icall_nop(uintptr_t); /**< nocrt-guard-win.asm */
#endif
extern uintptr_t  __guard_check_icall_fptr;           /**< nocrt-guard-win.asm */

RT_C_DECLS_END


/**
 * Checks if CFG is currently active.
 *
 * This requires CFG to be enabled at compile time, supported by the host OS
 * version and activated by the module loader.
 *
 * @returns true if CFG is active, false if not.
 */
DECLINLINE(bool) rtVccIsGuardICallChecksActive(void)
{
#ifdef _CONTROL_FLOW_GUARD
    return __guard_check_icall_fptr != (uintptr_t)__guard_check_icall_nop;
#else
    return false;
#endif
}


#ifdef IPRT_INCLUDED_nt_nt_h
/**
 * Checks if a pointer is on the officially registered stack or not.
 *
 * @returns true if on the official stack, false if not.
 * @param   uStackPtr           The pointer to check.
 */
DECLINLINE(bool) rtVccIsPointerOnTheStack(uintptr_t uStackPtr)
{
    PNT_TIB const pTib = (PNT_TIB)RTNtCurrentTeb();
    return uStackPtr <= (uintptr_t)pTib->StackBase
        && uStackPtr >= (uintptr_t)pTib->StackLimit;
}
#endif

#endif /* !IPRT_INCLUDED_INTERNAL_compiler_vcc_h */

