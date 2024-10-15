/* $Id: memuserkernel-r0drv-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - User & Kernel Memory, Ring-0 Driver, OS/2.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include "the-os2-kernel.h"

#include <iprt/mem.h>
#include <iprt/errcore.h>


RTR0DECL(int) RTR0MemUserCopyFrom(void *pvDst, RTR3PTR R3PtrSrc, size_t cb)
{
    int rc = KernCopyIn(pvDst, (void *)R3PtrSrc, cb);
    if (RT_LIKELY(rc == 0))
        return VINF_SUCCESS;
    return VERR_ACCESS_DENIED;
}


RTR0DECL(int) RTR0MemUserCopyTo(RTR3PTR R3PtrDst, void const *pvSrc, size_t cb)
{
    int rc = KernCopyOut((void *)R3PtrDst, (void *)pvSrc, cb);
    if (RT_LIKELY(rc == 0))
        return VINF_SUCCESS;
    return VERR_ACCESS_DENIED;
}


RTR0DECL(bool) RTR0MemUserIsValidAddr(RTR3PTR R3Ptr)
{
    /** @todo this is all wrong, but I'm too lazy to figure out how to make it
     *        correct. Checking the user DS limit would work if it wasn't maxed
     *        out by SDD, VPC or someone. The version (+SMP) would help on older
     *        OS/2 versions where the limit is 512MB. */
    return R3Ptr < UINT32_C(0xc0000000); /* 3GB */
}


RTR0DECL(bool) RTR0MemKernelIsValidAddr(void *pv)
{
    /** @todo this is all wrong, see RTR0MemUserIsValidAddr. */
    return (uintptr_t)pv >= UINT32_C(0x20000000); /* 512MB */
}


RTR0DECL(bool) RTR0MemAreKrnlAndUsrDifferent(void)
{
    /** @todo this is all wrong, see RTR0MemUserIsValidAddr. */
    return false;
}


RTR0DECL(int) RTR0MemKernelCopyFrom(void *pvDst, void const *pvSrc, size_t cb)
{
    RT_NOREF(pvDst, pvSrc, cb);
    return VERR_NOT_SUPPORTED;
}


RTR0DECL(int) RTR0MemKernelCopyTo(void *pvDst, void const *pvSrc, size_t cb)
{
    RT_NOREF(pvDst, pvSrc, cb);
    return VERR_NOT_SUPPORTED;
}

