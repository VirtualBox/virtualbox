/* $Id: RTLogWriteVmm-amd64-x86.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTLogWriteVmm - AMD64 & X86 for VBox, inline GCC version for drivers.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#include <iprt/log.h>
#include "internal/iprt.h"

#include <VBox/vmm/cpuidcall.h>


RTDECL(void) RTLogWriteVmm(const char *pach, size_t cb, bool fRelease)
{
    RTCCUINTREG uEAX, uEBX, uECX, uEDX, uESI;
#if (defined(PIC) || defined(__PIC__)) && defined(__i386__)
    __asm__ __volatile__ ("xchgl %%ebx, %1\n\t"
                          "cpuid\n\t"
                          "xchgl %%ebx, %1\n\t"
                         : "=a" (uEAX)
                         , "=r" (uEBX)
                         , "=c" (uECX)
                         , "=d" (uEDX)
                         , "=S" (uESI)
                         : "0" (VBOX_CPUID_REQ_EAX_FIXED)
                         , "1" ((uint32_t)fRelease)
                         , "2" (VBOX_CPUID_REQ_ECX_FIXED | VBOX_CPUID_FN_LOG)
                         , "3" (cb)
                         , "4" (pach));
#else
    __asm__ __volatile__ ("cpuid\n\t"
                          : "=a" (uEAX)
                          , "=b" (uEBX)
                          , "=c" (uECX)
                          , "=d" (uEDX)
                          , "=S" (uESI)
                          : "0" (VBOX_CPUID_REQ_EAX_FIXED)
                          , "1" ((uint32_t)fRelease)
                          , "2" (VBOX_CPUID_REQ_ECX_FIXED | VBOX_CPUID_FN_LOG)
                          , "3" (cb)
                          , "4" (pach));
#endif
}
RT_EXPORT_SYMBOL(RTLogWriteVmm);

