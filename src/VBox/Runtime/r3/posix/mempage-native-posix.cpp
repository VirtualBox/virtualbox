/* $Id: mempage-native-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - rtMemPageNative*, POSIX implementation.
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

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/param.h>
#include "internal/mem.h"

#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
# define MAP_ANONYMOUS MAP_ANON
#endif



DECLHIDDEN(int) rtMemPageNativeAlloc(size_t cb, uint32_t fFlags, void **ppvRet)
{
    void *pvRet = mmap(NULL, cb,
                       PROT_READ | PROT_WRITE | (fFlags & RTMEMPAGEALLOC_F_EXECUTABLE ? PROT_EXEC : 0),
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
    if (pvRet != MAP_FAILED)
    {
        *ppvRet = pvRet;
        return VINF_SUCCESS;
    }
    *ppvRet = NULL;
    return RTErrConvertFromErrno(errno);
}


DECLHIDDEN(int) rtMemPageNativeFree(void *pv, size_t cb)
{
    int rc = munmap(pv, cb);
    AssertMsgReturn(rc == 0, ("rc=%d pv=%p cb=%#zx errno=%d\n", rc, pv, cb, errno), RTErrConvertFromErrno(errno));
    return VINF_SUCCESS;
}


DECLHIDDEN(uint32_t) rtMemPageNativeApplyFlags(void *pv, size_t cb, uint32_t fFlags)
{
    uint32_t fRet = 0;
    if (fFlags & RTMEMPAGEALLOC_F_ADVISE_LOCKED)
    {
        int rc = mlock(pv, cb);
#ifndef RT_OS_SOLARIS /* mlock(3C) on Solaris requires the priv_lock_memory privilege */
        AssertMsg(rc == 0, ("mlock %p LB %#zx -> %d errno=%d\n", pv, cb, rc, errno));
#endif
        if (rc == 0)
            fRet |= RTMEMPAGEALLOC_F_ADVISE_LOCKED;
    }

#ifdef MADV_DONTDUMP
    if (fFlags & RTMEMPAGEALLOC_F_ADVISE_NO_DUMP)
    {
        int rc = madvise(pv, cb, MADV_DONTDUMP);
        AssertMsg(rc == 0, ("madvice %p LB %#zx MADV_DONTDUMP -> %d errno=%d\n", pv, cb, rc, errno));
        if (rc == 0)
            fRet |= RTMEMPAGEALLOC_F_ADVISE_NO_DUMP;
    }
#endif
    return fRet;
}


DECLHIDDEN(void) rtMemPageNativeRevertFlags(void *pv, size_t cb, uint32_t fFlags)
{
    if (fFlags & RTMEMPAGEALLOC_F_ADVISE_LOCKED)
    {
        int rc = munlock(pv, cb);
        AssertMsg(rc == 0, ("munlock %p LB %#zx -> %d errno=%d\n", pv, cb, rc, errno));
        RT_NOREF(rc);
    }

#if defined(MADV_DONTDUMP) && defined(MADV_DODUMP)
    if (fFlags & RTMEMPAGEALLOC_F_ADVISE_NO_DUMP)
    {
        int rc = madvise(pv, cb, MADV_DODUMP);
        AssertMsg(rc == 0, ("madvice %p LB %#zx MADV_DODUMP -> %d errno=%d\n", pv, cb, rc, errno));
        RT_NOREF(rc);
    }
#endif
}

