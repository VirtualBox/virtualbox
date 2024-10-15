/* $Id: allocex.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, Extended Alloc and Free Functions for Ring-3.
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

#ifndef IPRT_INCLUDED_SRC_r3_allocex_h
#define IPRT_INCLUDED_SRC_r3_allocex_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

/**
 * Heading for extended memory allocations in ring-3.
 */
typedef struct RTMEMHDRR3
{
    /** Magic (RTMEMHDR_MAGIC). */
    uint32_t    u32Magic;
    /** Block flags (RTMEMALLOCEX_FLAGS_*). */
    uint32_t    fFlags;
    /** The actual size of the block, header not included. */
    uint32_t    cb;
    /** The requested allocation size. */
    uint32_t    cbReq;
} RTMEMHDRR3;
/** Pointer to a ring-3 extended memory header. */
typedef RTMEMHDRR3 *PRTMEMHDRR3;


/**
 * Allocate memory in below 64KB.
 *
 * @returns IPRT status code.
 * @param   cbAlloc             Number of bytes to allocate (including the
 *                              header if the caller needs one).
 * @param   fFlags              Allocation flags.
 * @param   ppv                 Where to return the pointer to the memory.
 */
DECLHIDDEN(int) rtMemAllocEx16BitReach(size_t cbAlloc, uint32_t fFlags, void **ppv);

/**
 * Allocate memory in below 4GB.
 *
 * @returns IPRT status code.
 * @param   cbAlloc             Number of bytes to allocate (including the
 *                              header if the caller needs one).
 * @param   fFlags              Allocation flags.
 * @param   ppv                 Where to return the pointer to the memory.
 */
DECLHIDDEN(int) rtMemAllocEx32BitReach(size_t cbAlloc, uint32_t fFlags, void **ppv);


/**
 * Frees memory allocated by rtMemAllocEx16BitReach and rtMemAllocEx32BitReach.
 *
 * @param   pv                  Start of allocation.
 * @param   cb                  Allocation size.
 * @param   fFlags              Allocation flags.
 */
DECLHIDDEN(void) rtMemFreeExYyBitReach(void *pv, size_t cb, uint32_t fFlags);

#endif /* !IPRT_INCLUDED_SRC_r3_allocex_h */

