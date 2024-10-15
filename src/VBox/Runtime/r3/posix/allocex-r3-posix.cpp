/* $Id: allocex-r3-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, Extended Alloc Workers, posix.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define RTMEM_NO_WRAP_TO_EF_APIS
#include <iprt/mem.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include "../allocex.h"

#include <sys/mman.h>


DECLHIDDEN(int) rtMemAllocEx16BitReach(size_t cbAlloc, uint32_t fFlags, void **ppv)
{
    AssertReturn(cbAlloc < _64K, VERR_NO_MEMORY);

    /*
     * Try with every possible address hint since the possible range is very limited.
     */
    int       fProt     = PROT_READ | PROT_WRITE | (fFlags & RTMEMALLOCEX_FLAGS_EXEC ? PROT_EXEC : 0);
    uintptr_t uAddr     = 0x1000;
    uintptr_t uAddrLast = _64K - uAddr - cbAlloc;
    while (uAddr <= uAddrLast)
    {
        void *pv = mmap((void *)uAddr, cbAlloc, fProt, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (pv && (uintptr_t)pv <= uAddrLast)
        {
            *ppv = pv;
            return VINF_SUCCESS;
        }

        if (pv)
        {
            munmap(pv, cbAlloc);
            pv = NULL;
        }
        uAddr += _4K;
    }

    return VERR_NO_MEMORY;
}


DECLHIDDEN(int) rtMemAllocEx32BitReach(size_t cbAlloc, uint32_t fFlags, void **ppv)
{
    int     fProt = PROT_READ | PROT_WRITE | (fFlags & RTMEMALLOCEX_FLAGS_EXEC ? PROT_EXEC : 0);
#if ARCH_BITS == 32
    void   *pv = mmap(NULL, cbAlloc, fProt, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pv)
    {
        *ppv = pv;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;

#elif defined(RT_OS_LINUX)
# ifdef MAP_32BIT
    void *pv = mmap(NULL, cbAlloc, fProt, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (pv)
    {
        *ppv = pv;
        return VINF_SUCCESS;
    }
# else
    RT_NOREF(fProt, cbAlloc, ppv);
# endif

    /** @todo On linux, we need an accurate hint. Since I don't need this branch of
     *        the code right now, I won't bother starting to parse
     *        /proc/curproc/mmap right now... */
#else
    RT_NOREF(fProt, cbAlloc, ppv);
#endif
    return VERR_NOT_SUPPORTED;
}


DECLHIDDEN(void) rtMemFreeExYyBitReach(void *pv, size_t cb, uint32_t fFlags)
{
    RT_NOREF_PV(fFlags);
    munmap(pv, cb);
}

