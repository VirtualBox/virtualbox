/* $Id: rtmempage-exec-mmap-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTMemPage*, POSIX with mmap only.
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
#include "internal/iprt.h"
#include <iprt/mem.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/system.h>

#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
# define MAP_ANONYMOUS MAP_ANON
#endif


/**
 * Applies flags to an allocation.
 *
 * @param   pv              The allocation.
 * @param   cb              The size of the allocation (page aligned).
 * @param   fFlags          RTMEMPAGEALLOC_F_XXX.
 */
DECLINLINE(void) rtMemPagePosixApplyFlags(void *pv, size_t cb, uint32_t fFlags)
{
#ifndef RT_OS_OS2
    if (fFlags & RTMEMPAGEALLOC_F_ADVISE_LOCKED)
    {
        int rc = mlock(pv, cb);
# ifndef RT_OS_SOLARIS /* mlock(3C) on Solaris requires the priv_lock_memory privilege */
        AssertMsg(rc == 0, ("mlock %p LB %#zx -> %d errno=%d\n", pv, cb, rc, errno));
# endif
        NOREF(rc);
    }

# ifdef MADV_DONTDUMP
    if (fFlags & RTMEMPAGEALLOC_F_ADVISE_NO_DUMP)
    {
        int rc = madvise(pv, cb, MADV_DONTDUMP);
        AssertMsg(rc == 0, ("madvice %p LB %#zx MADV_DONTDUMP -> %d errno=%d\n", pv, cb, rc, errno));
        NOREF(rc);
    }
# endif
#endif

    if (fFlags & RTMEMPAGEALLOC_F_ZERO)
        RT_BZERO(pv, cb);
}


/**
 * Allocates memory from the specified heap.
 *
 * @returns Address of the allocated memory.
 * @param   cb                  The number of bytes to allocate.
 * @param   pszTag              The tag.
 * @param   fFlags              RTMEMPAGEALLOC_F_XXX.
 * @param   fProtExec           PROT_EXEC or 0.
 */
static void *rtMemPagePosixAlloc(size_t cb, const char *pszTag, uint32_t fFlags, int fProtExec)
{
    /*
     * Validate & adjust the input.
     */
    Assert(cb > 0);
    NOREF(pszTag);
    cb = RTSystemPageAlignSize(cb);

    /*
     * Do the allocation.
     */
    void *pv = mmap(NULL, cb,
                    PROT_READ | PROT_WRITE | fProtExec,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pv != MAP_FAILED)
    {
        AssertPtr(pv);

        if (fFlags)
            rtMemPagePosixApplyFlags(pv, cb, fFlags);
    }
    else
        pv = NULL;

    return pv;
}


/**
 * Free memory allocated by rtMemPagePosixAlloc.
 *
 * @param   pv                  The address of the memory to free.
 * @param   cb                  The size.
 */
static void rtMemPagePosixFree(void *pv, size_t cb)
{
    /*
     * Validate & adjust the input.
     */
    if (!pv)
        return;
    AssertPtr(pv);
    Assert(cb > 0);
    Assert(!((uintptr_t)pv & RTSystemGetPageOffsetMask()));
    cb = RTSystemPageAlignSize(cb);

    /*
     * Free the memory.
     */
    int rc = munmap(pv, cb);
    AssertMsg(rc == 0, ("rc=%d pv=%p cb=%#zx\n", rc, pv, cb)); NOREF(rc);
}





RTDECL(void *) RTMemPageAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return rtMemPagePosixAlloc(cb, pszTag, 0, 0);
}


RTDECL(void *) RTMemPageAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return rtMemPagePosixAlloc(cb, pszTag, RTMEMPAGEALLOC_F_ZERO, 0);
}


RTDECL(void *) RTMemPageAllocExTag(size_t cb, uint32_t fFlags, const char *pszTag) RT_NO_THROW_DEF
{
    AssertReturn(!(fFlags & ~RTMEMPAGEALLOC_F_VALID_MASK), NULL);
    return rtMemPagePosixAlloc(cb, pszTag, fFlags, !(fFlags & RTMEMPAGEALLOC_F_EXECUTABLE) ? 0 : PROT_EXEC);
}


RTDECL(void) RTMemPageFree(void *pv, size_t cb) RT_NO_THROW_DEF
{
    return rtMemPagePosixFree(pv, cb);
}

