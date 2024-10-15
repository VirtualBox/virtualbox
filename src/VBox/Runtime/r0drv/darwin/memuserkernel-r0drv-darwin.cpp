/* $Id: memuserkernel-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - User & Kernel Memory, Ring-0 Driver, Darwin.
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
#include "the-darwin-kernel.h"
#include "internal/iprt.h"
#include <iprt/mem.h>
#include <iprt/assert.h>

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/errcore.h>


RTR0DECL(int) RTR0MemUserCopyFrom(void *pvDst, RTR3PTR R3PtrSrc, size_t cb)
{
    RT_ASSERT_INTS_ON();
    IPRT_DARWIN_SAVE_EFL_AC();
    int rc = copyin((const user_addr_t)R3PtrSrc, pvDst, cb);
    IPRT_DARWIN_RESTORE_EFL_AC();
    if (RT_LIKELY(rc == 0))
        return VINF_SUCCESS;
    return VERR_ACCESS_DENIED;
}


RTR0DECL(int) RTR0MemUserCopyTo(RTR3PTR R3PtrDst, void const *pvSrc, size_t cb)
{
    RT_ASSERT_INTS_ON();
    IPRT_DARWIN_SAVE_EFL_AC();
    int rc = copyout(pvSrc, R3PtrDst, cb);
    IPRT_DARWIN_RESTORE_EFL_AC();
    if (RT_LIKELY(rc == 0))
        return VINF_SUCCESS;
    return VERR_ACCESS_DENIED;
}


RTR0DECL(bool) RTR0MemUserIsValidAddr(RTR3PTR R3Ptr)
{
    /* the commpage is above this. */
#ifdef RT_ARCH_X86
    return R3Ptr < VM_MAX_ADDRESS;
#else
    return R3Ptr < VM_MAX_PAGE_ADDRESS;
#endif
}


RTR0DECL(bool) RTR0MemKernelIsValidAddr(void *pv)
{
    /* Found no public #define or symbol for checking this, so we'll
       have to make do with thing found in the debugger and the sources. */
#ifdef RT_ARCH_X86
    NOREF(pv);
    return true;    /* Almost anything is a valid kernel address here. */

#elif defined(RT_ARCH_AMD64)
    return (uintptr_t)pv >= UINT64_C(0xffff800000000000);

#else
# error "PORTME"
#endif
}


RTR0DECL(bool) RTR0MemAreKrnlAndUsrDifferent(void)
{
    /* As mentioned in RTR0MemKernelIsValidAddr, found no way of checking
       this at compiler or runtime. */
#ifdef RT_ARCH_X86
    return false;
#else
    return true;
#endif
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

