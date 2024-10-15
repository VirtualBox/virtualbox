/* $Id: alloc-r0drv.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, Ring-0 Driver.
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

#ifndef IPRT_INCLUDED_SRC_r0drv_alloc_r0drv_h
#define IPRT_INCLUDED_SRC_r0drv_alloc_r0drv_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/mem.h>
#include "internal/magics.h"

RT_C_DECLS_BEGIN

/**
 * Header which heading all memory blocks.
 */
typedef struct RTMEMHDR
{
    /** Magic (RTMEMHDR_MAGIC). */
    uint32_t    u32Magic;
    /** Block flags (RTMEMHDR_FLAG_*). */
    uint32_t    fFlags;
    /** The actual size of the block, header not included. */
    uint32_t    cb;
    /** The requested allocation size. */
    uint32_t    cbReq;
} RTMEMHDR, *PRTMEMHDR;


/** @name RTMEMHDR::fFlags.
 * @{ */
/** Clear the allocated memory. */
#define RTMEMHDR_FLAG_ZEROED        RT_BIT(0)
/** Use allocation method suitable for any context. */
#define RTMEMHDR_FLAG_ANY_CTX_ALLOC RT_BIT(1)
/** Use allocation method which allow for freeing in any context. */
#define RTMEMHDR_FLAG_ANY_CTX_FREE  RT_BIT(2)
/** Both alloc and free in any context (or we're just darn lazy). */
#define RTMEMHDR_FLAG_ANY_CTX       (RTMEMHDR_FLAG_ANY_CTX_ALLOC | RTMEMHDR_FLAG_ANY_CTX_FREE)
/** Indicate that it was allocated by rtR0MemAllocExTag. */
#define RTMEMHDR_FLAG_ALLOC_EX      RT_BIT(3)
#ifdef RT_OS_LINUX
/** Linux: Allocated by kmalloc() instead of vmalloc(). */
# define RTMEMHDR_FLAG_KMALLOC      RT_BIT(31)
#elif defined(RT_OS_WINDOWS)
/** Windows: Untagged allocation by ExAllocatePool, freed using ExFreePool. */
# define RTMEMHDR_FLAG_UNTAGGED     RT_BIT(31)
#endif
/** @} */


/**
 * Heap allocation back end for ring-0.
 *
 * @returns IPRT status code.  VERR_NO_MEMORY suffices for RTMEMHDR_FLAG_EXEC,
 *          the caller will change it to VERR_NO_EXEC_MEMORY when appropriate.
 *
 * @param   cb          The amount of memory requested by the user.  This does
 *                      not include the header.
 * @param   fFlags      The allocation flags and more.  These should be
 *                      assigned to RTMEMHDR::fFlags together with any flags
 *                      the backend might be using.
 * @param   ppHdr       Where to return the memory header on success.
 */
DECLHIDDEN(int)     rtR0MemAllocEx(size_t cb, uint32_t fFlags, PRTMEMHDR *ppHdr);

/**
 * Free memory allocated by rtR0MemAllocEx.
 * @param   pHdr        The memory block to free.  (Never NULL.)
 */
DECLHIDDEN(void)    rtR0MemFree(PRTMEMHDR pHdr);

RT_C_DECLS_END
#endif /* !IPRT_INCLUDED_SRC_r0drv_alloc_r0drv_h */

