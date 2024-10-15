/* $Id: memuserkernel-r0drv-linux.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - User & Kernel Memory, Ring-0 Driver, Linux.
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
#include "the-linux-kernel.h"
#include "internal/iprt.h"

#include <iprt/mem.h>
#include <iprt/errcore.h>


RTR0DECL(int) RTR0MemUserCopyFrom(void *pvDst, RTR3PTR R3PtrSrc, size_t cb)
{
    IPRT_LINUX_SAVE_EFL_AC();
    if (RT_LIKELY(copy_from_user(pvDst, (void *)R3PtrSrc, cb) == 0))
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VINF_SUCCESS;
    }
    IPRT_LINUX_RESTORE_EFL_AC();
    return VERR_ACCESS_DENIED;
}
RT_EXPORT_SYMBOL(RTR0MemUserCopyFrom);


RTR0DECL(int) RTR0MemUserCopyTo(RTR3PTR R3PtrDst, void const *pvSrc, size_t cb)
{
    IPRT_LINUX_SAVE_EFL_AC();
    if (RT_LIKELY(copy_to_user((void *)R3PtrDst, pvSrc, cb) == 0))
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VINF_SUCCESS;
    }
    IPRT_LINUX_RESTORE_EFL_AC();
    return VERR_ACCESS_DENIED;
}
RT_EXPORT_SYMBOL(RTR0MemUserCopyTo);


RTR0DECL(bool) RTR0MemUserIsValidAddr(RTR3PTR R3Ptr)
{
    IPRT_LINUX_SAVE_EFL_AC();
#if RTLNX_VER_MIN(5,0,0) || RTLNX_RHEL_MIN(8,1)
    bool fRc = access_ok((void *)R3Ptr, 1);
#else
    bool fRc = access_ok(VERIFY_READ, (void *)R3Ptr, 1);
#endif
    IPRT_LINUX_RESTORE_EFL_AC();
    return fRc;
}
RT_EXPORT_SYMBOL(RTR0MemUserIsValidAddr);


RTR0DECL(bool) RTR0MemKernelIsValidAddr(void *pv)
{
    /* Couldn't find a straight forward way of doing this... */
#if defined(RT_ARCH_X86) && defined(CONFIG_X86_HIGH_ENTRY)
    return true; /* ?? */
#elif defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
    return (uintptr_t)pv >= PAGE_OFFSET;
#else
# error "PORT ME"
#if RTLNX_VER_MIN(5,0,0) || RTLNX_RHEL_MIN(8,1)
    return !access_ok(pv, 1);
#else
    return !access_ok(VERIFY_READ, pv, 1);
#endif
#endif
}
RT_EXPORT_SYMBOL(RTR0MemKernelIsValidAddr);


RTR0DECL(bool) RTR0MemAreKrnlAndUsrDifferent(void)
{
#if defined(RT_ARCH_X86) && defined(CONFIG_X86_HIGH_ENTRY) /* ?? */
    return false;
#else
    return true;
#endif
}
RT_EXPORT_SYMBOL(RTR0MemAreKrnlAndUsrDifferent);


#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
/**
 * Treats both source and destination as unsafe buffers.
 */
static int rtR0MemKernelCopyLnxWorker(void *pvDst, void const *pvSrc, size_t cb)
{
# if RTLNX_VER_MIN(2,5,55)
/* _ASM_EXTABLE was introduced in 2.6.25 from what I can tell. Using #ifndef
   here since it has to be a macro and you never know what someone might have
   backported to an earlier kernel release. */
#  ifndef _ASM_EXTABLE
#   if ARCH_BITS == 32
#    define _ASM_EXTABLE(a_Instr, a_Resume) \
     ".section __ex_table,\"a\"\n" \
     ".balign 4\n" \
     ".long   " #a_Instr "\n" \
     ".long   " #a_Resume "\n" \
     ".previous\n"
#   else
#    define _ASM_EXTABLE(a_Instr, a_Resume) \
     ".section __ex_table,\"a\"\n" \
     ".balign 8\n" \
     ".quad   " #a_Instr "\n" \
     ".quad   " #a_Resume "\n" \
     ".previous\n"
#   endif
#  endif /* !_ASM_EXTABLE */
    int rc;
    IPRT_LINUX_SAVE_EFL_AC(); /* paranoia */
    if (!cb)
        return VINF_SUCCESS;

    __asm__ __volatile__ ("cld\n"
                          "1:\n\t"
                          "rep; movsb\n"
                          "2:\n\t"
                          ".section .fixup,\"ax\"\n"
                          "3:\n\t"
                          "movl %4, %0\n\t"
                          "jmp 2b\n\t"
                          ".previous\n"
                          _ASM_EXTABLE(1b, 3b)
                          : "=r" (rc),
                            "=D" (pvDst),
                            "=S" (pvSrc),
                            "=c" (cb)
                          : "i" (VERR_ACCESS_DENIED),
                            "0" (VINF_SUCCESS),
                            "1" (pvDst),
                            "2" (pvSrc),
                            "3" (cb)
                          : "memory");
    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
# else
    return VERR_NOT_SUPPORTED;
# endif
}


RTR0DECL(int) RTR0MemKernelCopyFrom(void *pvDst, void const *pvSrc, size_t cb)
{
    return rtR0MemKernelCopyLnxWorker(pvDst, pvSrc, cb);
}
RT_EXPORT_SYMBOL(RTR0MemKernelCopyFrom);


RTR0DECL(int) RTR0MemKernelCopyTo(void *pvDst, void const *pvSrc, size_t cb)
{
    return rtR0MemKernelCopyLnxWorker(pvDst, pvSrc, cb);
}
RT_EXPORT_SYMBOL(RTR0MemKernelCopyTo);
#endif /* !RT_ARCH_AMD64 && !RT_ARCH_X86 */

