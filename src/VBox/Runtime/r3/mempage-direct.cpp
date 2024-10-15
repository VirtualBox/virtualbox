/* $Id: mempage-direct.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTMemPage*, Minimal version w/o hea.
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
#include <iprt/string.h>
#include <iprt/system.h>
#include "internal/mem.h"


RTDECL(void *) RTMemPageAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return RTMemPageAllocExTag(cb, 0, pszTag);
}


RTDECL(void *) RTMemPageAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return RTMemPageAllocExTag(cb, RTMEMPAGEALLOC_F_ZERO, pszTag);
}


RTDECL(void *) RTMemPageAllocExTag(size_t cb, uint32_t fFlags, const char *pszTag) RT_NO_THROW_DEF
{
    AssertReturn(!(fFlags & ~RTMEMPAGEALLOC_F_VALID_MASK), NULL);
    cb = RTSystemPageAlignSize(cb);

    void *pv = NULL;
    int   rc = rtMemPageNativeAlloc(cb, fFlags, &pv);
    if (RT_SUCCESS(rc))
    {
        if (fFlags & RTMEMPAGEALLOC_F_ZERO)
            RT_BZERO(pv, cb);
        if (fFlags & (RTMEMPAGEALLOC_F_ADVISE_LOCKED | RTMEMPAGEALLOC_F_ADVISE_NO_DUMP))
            rtMemPageNativeApplyFlags(pv, cb, fFlags);
        return pv;
    }
    RT_NOREF(pszTag);
    return NULL;
}


RTDECL(void) RTMemPageFree(void *pv, size_t cb) RT_NO_THROW_DEF
{
    if (pv)
    {
        AssertPtr(pv);
        Assert(!((uintptr_t)pv & RTSystemGetPageOffsetMask()));
        Assert(cb > 0);

        cb = RTSystemPageAlignSize(cb);
        rtMemPageNativeFree(pv, cb);
    }
}

