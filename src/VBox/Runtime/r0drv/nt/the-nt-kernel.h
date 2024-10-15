/* $Id: the-nt-kernel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Include all necessary headers for the NT kernel.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_SRC_r0drv_nt_the_nt_kernel_h
#define IPRT_INCLUDED_SRC_r0drv_nt_the_nt_kernel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

#if defined(RT_ARCH_X86) && !defined(NO_INTERLOCKED_INTRINSICS)
# define NO_INTERLOCKED_INTRINSICS /* avoid trouble */
#endif
#if (_MSC_VER >= 1400) && !defined(VBOX_WITH_PATCHED_DDK)
# include <iprt/asm.h>
# define _InterlockedExchange           _InterlockedExchange_StupidDDKVsCompilerCrap
# define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKVsCompilerCrap
# define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKVsCompilerCrap
# define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
# pragma warning(disable : 4163)
RT_C_DECLS_BEGIN
# include <iprt/nt/nt.h>
RT_C_DECLS_END
# pragma warning(default : 4163)
# undef  _InterlockedExchange
# undef  _InterlockedExchangeAdd
# undef  _InterlockedCompareExchange
# undef  _InterlockedAddLargeStatistic
#else
RT_C_DECLS_BEGIN
# include <iprt/nt/nt.h>
RT_C_DECLS_END
#endif

#include <memory.h>
#if !defined(RT_OS_WINDOWS)
# error "RT_OS_WINDOWS must be defined!"
#endif

#include <iprt/param.h>
#ifndef PAGE_OFFSET_MASK
# define PAGE_OFFSET_MASK (PAGE_SIZE - 1)
#endif

/* Missing if we're compiling against older WDKs. */
#ifndef NonPagedPoolNx
# define NonPagedPoolNx     ((POOL_TYPE)512)
#endif

/*
 * When targeting NT4 we have to undo some of the nice macros
 * installed by the later DDKs.
 */
#undef ExAllocatePool
#undef ExFreePool

/** @def IPRT_NT_POOL_TAG
 * Tag to use with the NT Pool APIs.
 * In memory and in the various windbg tools it appears in the reverse order of
 * what it is given as here, so it'll read "IPRT".
 */
#define IPRT_NT_POOL_TAG    'TRPI'

#endif /* !IPRT_INCLUDED_SRC_r0drv_nt_the_nt_kernel_h */

