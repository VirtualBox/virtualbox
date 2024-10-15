/* $Id: memsafer-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocate for Sensitive Data, generic heap-based implementation.
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
#include <iprt/memsafer.h>

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Allocation size alignment. */
#define RTMEMSAFER_ALIGN        16
/** Padding after the block to avoid small overruns. */
#define RTMEMSAFER_PAD_BEFORE   96
/** Padding after the block to avoid small underruns. */
#define RTMEMSAFER_PAD_AFTER    32


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** XOR scrabler value.
 * @todo determine this at runtime */
#if ARCH_BITS == 32
static uintptr_t g_uScramblerXor = UINT32_C(0x867af88d);
#elif ARCH_BITS == 64
static uintptr_t g_uScramblerXor = UINT64_C(0xed95ecc99416d312);
#else
# error "Bad ARCH_BITS value"
#endif



RTDECL(int) RTMemSaferScramble(void *pv, size_t cb)
{

    AssertMsg(*(size_t *)((char *)pv - RTMEMSAFER_PAD_BEFORE) == cb,
              ("*pvStart=%#zx cb=%#zx\n", *(size_t *)((char *)pv- RTMEMSAFER_PAD_BEFORE), cb));

    /* Note! This isn't supposed to be safe, just less obvious. */
    uintptr_t *pu = (uintptr_t *)pv;
    cb = RT_ALIGN_Z(cb, RTMEMSAFER_ALIGN);
    while (cb > 0)
    {
        *pu ^= g_uScramblerXor;
        pu++;
        cb -= sizeof(*pu);
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTMemSaferScramble);


RTDECL(int) RTMemSaferUnscramble(void *pv, size_t cb)
{
    AssertMsg(*(size_t *)((char *)pv - RTMEMSAFER_PAD_BEFORE) == cb,
              ("*pvStart=%#zx cb=%#zx\n", *(size_t *)((char *)pv - RTMEMSAFER_PAD_BEFORE), cb));

    /* Note! This isn't supposed to be safe, just less obvious. */
    uintptr_t *pu = (uintptr_t *)pv;
    cb = RT_ALIGN_Z(cb, RTMEMSAFER_ALIGN);
    while (cb > 0)
    {
        *pu ^= g_uScramblerXor;
        pu++;
        cb -= sizeof(*pu);
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTMemSaferUnscramble);


RTDECL(int) RTMemSaferAllocZExTag(void **ppvNew, size_t cb, uint32_t fFlags, const char *pszTag) RT_NO_THROW_DEF
{
    AssertPtrReturn(ppvNew, VERR_INVALID_PARAMETER);
    *ppvNew = NULL;
    AssertReturn(cb, VERR_INVALID_PARAMETER);
    RT_NOREF_PV(pszTag);

    /*
     * We support none of the hard requirements passed thru flags.
     */
    if (fFlags == 0)
    {
        /*
         * Don't request zeroed memory.  We want random heap garbage in the
         * padding zones, nothing that makes our allocations easier to find.
         */
        size_t cbUser = RT_ALIGN_Z(cb, RTMEMSAFER_ALIGN);
        void *pvNew = RTMemAlloc(cbUser + RTMEMSAFER_PAD_BEFORE + RTMEMSAFER_PAD_AFTER);
        if (pvNew)
        {
#ifdef RT_STRICT /* For checking input in string builds. */
            memset(pvNew, 0xad, RTMEMSAFER_PAD_BEFORE);
            memset((char *)pvNew + RTMEMSAFER_PAD_BEFORE + cb, 0xda, RTMEMSAFER_PAD_AFTER + (cbUser - cb));
            *(size_t *)pvNew = cb;
#endif

            void *pvUser = (char *)pvNew + RTMEMSAFER_PAD_BEFORE;
            *ppvNew = pvUser;

            /* You don't use this API for performance, so we always clean memory. */
            RT_BZERO(pvUser, cb);

            return VINF_SUCCESS;
        }
        return VERR_NO_MEMORY;
    }
    AssertReturn(!(fFlags & ~RTMEMSAFER_F_VALID_MASK), VERR_INVALID_FLAGS);
    return VWRN_UNABLE_TO_SATISFY_REQUIREMENTS;
}
RT_EXPORT_SYMBOL(RTMemSaferAllocZExTag);


RTDECL(void) RTMemSaferFree(void *pv, size_t cb) RT_NO_THROW_DEF
{
    if (pv)
    {
        Assert(cb); /* does not support openssl. */
        void *pvStart = (char *)pv - RTMEMSAFER_PAD_BEFORE;
        AssertMsg(*(size_t *)pvStart == cb, ("*pvStart=%#zx cb=%#zx\n", *(size_t *)pvStart, cb));
        RTMemWipeThoroughly(pv, RT_ALIGN_Z(cb, RTMEMSAFER_ALIGN), 3);
        RTMemFree(pvStart);
    }
    else
        Assert(cb == 0);
}
RT_EXPORT_SYMBOL(RTMemSaferFree);


RTDECL(int) RTMemSaferReallocZExTag(size_t cbOld, void *pvOld, size_t cbNew, void **ppvNew, uint32_t fFlags, const char *pszTag) RT_NO_THROW_DEF
{
    /*
     * We cannot let the heap move us around because we will be failing in our
     * duty to clean things up.  So, allocate a new block, copy over the old
     * content, and free the old one.
     */
    int rc;
    /* Real realloc. */
    if (cbNew && cbOld)
    {
        AssertPtr(pvOld);
        AssertMsg(*(size_t *)((char *)pvOld - RTMEMSAFER_PAD_BEFORE) == cbOld,
                  ("*pvStart=%#zx cbOld=%#zx\n", *(size_t *)((char *)pvOld - RTMEMSAFER_PAD_BEFORE), cbOld));

        /*
         * We support none of the hard requirements passed thru flags.
         */
        void *pvNew;
        rc = RTMemSaferAllocZExTag(&pvNew, cbNew, fFlags, pszTag);
        if (RT_SUCCESS(rc))
        {
            memcpy(pvNew, pvOld, RT_MIN(cbNew, cbOld));
            RTMemSaferFree(pvOld, cbOld);
            *ppvNew = pvNew;
        }
    }
    /* First allocation. */
    else if (!cbOld)
    {
        Assert(pvOld == NULL);
        rc = RTMemSaferAllocZExTag(ppvNew, cbNew, fFlags, pszTag);
    }
    /* Free operation*/
    else
    {
        RTMemSaferFree(pvOld, cbOld);
        rc = VINF_SUCCESS;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTMemSaferReallocZExTag);


RTDECL(void *) RTMemSaferAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvNew = NULL;
    int rc = RTMemSaferAllocZExTag(&pvNew, cb, 0 /*fFlags*/, pszTag);
    if (RT_SUCCESS(rc))
        return pvNew;
    return NULL;
}
RT_EXPORT_SYMBOL(RTMemSaferAllocZTag);


RTDECL(void *) RTMemSaferReallocZTag(size_t cbOld, void *pvOld, size_t cbNew, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvNew = NULL;
    int rc = RTMemSaferReallocZExTag(cbOld, pvOld, cbNew, &pvNew, 0 /*fFlags*/, pszTag);
    if (RT_SUCCESS(rc))
        return pvNew;
    return NULL;
}
RT_EXPORT_SYMBOL(RTMemSaferReallocZTag);

