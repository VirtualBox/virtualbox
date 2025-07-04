/* $Id: mem.h 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Memory Management.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_INTERNAL_mem_h
#define IPRT_INCLUDED_INTERNAL_mem_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN

/**
 * Special allocation method that does not have any IPRT dependencies.
 *
 * This is suitable for allocating memory for IPRT heaps, pools, caches, memory
 * trackers, semaphores and similar that end up in bootstrap depencency hell
 * otherwise.
 *
 * @returns Pointer to the allocated memory, NULL on failure.  Must be freed by
 *          calling rtMemBaseFree().
 * @param   cb          The number of bytes to allocate.
 */
DECLHIDDEN(void *)  rtMemBaseAlloc(size_t cb);

/**
 * Frees memory allocated by rtInitAlloc().
 *
 * @param   pv          What rtInitAlloc() returned.
 */
DECLHIDDEN(void)    rtMemBaseFree(void *pv);


#ifdef IN_RING0
/** @def RTR0MEM_WITH_EF_APIS
 * Enables the electrict fence APIs.
 *
 * Requires working rtR0MemObjNativeProtect implementation, thus the current
 * OS restrictions.
 */
# if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD) || defined(DOXYGEN_RUNNING)
#  define RTR0MEM_WITH_EF_APIS
# endif
# ifdef RTR0MEM_WITH_EF_APIS
DECLHIDDEN(void)    rtR0MemEfInit(void);
DECLHIDDEN(void)    rtR0MemEfTerm(void);
# endif
#endif

#ifdef IN_RING3

/**
 * Native allocation worker for the heap-based RTMemPage implementation.
 */
DECLHIDDEN(int) rtMemPageNativeAlloc(size_t cb, uint32_t fFlags, void **ppvRet);

/**
 * Native allocation worker for the heap-based RTMemPage implementation.
 */
DECLHIDDEN(int) rtMemPageNativeFree(void *pv, size_t cb);

/**
 * Native page allocator worker that applies advisory flags to the memory.
 *
 * @returns Set of flags succesfully applied
 * @param   pv      The memory block address.
 * @param   cb      The size of the memory block.
 * @param   fFlags  The flags to apply (may include other flags too, ignore).
 */
DECLHIDDEN(uint32_t) rtMemPageNativeApplyFlags(void *pv, size_t cb, uint32_t fFlags);

/**
 * Reverts flags previously applied by rtMemPageNativeApplyFlags().
 *
 * @param   pv      The memory block address.
 * @param   cb      The size of the memory block.
 * @param   fFlags  The flags to revert.
 */
DECLHIDDEN(void) rtMemPageNativeRevertFlags(void *pv, size_t cb, uint32_t fFlags);

#endif /* IN_RING3 */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_mem_h */

