/* $Id: fma.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - fma().
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
#define IPRT_NO_CRT_FOR_3RD_PARTY
#include "internal/nocrt.h"
#include <iprt/nocrt/math.h>
#include <iprt/assertcompile.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
# include <iprt/x86.h>
#endif
#include <softfloat.h>


/*********************************************************************************************************************************
*   External Symbols                                                                                                             *
*********************************************************************************************************************************/
DECLASM(double) rtNoCrtMathFma3(double rdFactor1, double rdFactor2, double rdAddend);
DECLASM(double) rtNoCrtMathFma4(double rdFactor1, double rdFactor2, double rdAddend);


#undef fma
double RT_NOCRT(fma)(double rdFactor1, double rdFactor2, double rdAddend)
{
    /*
     * We prefer using native FMA instructions when available.
     */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    typedef enum { kCpuDetect = 0, kCpuWithFma3, kCpuWithFma4, kCpuWithoutFma } CPUFMASUPPORT;
    static CPUFMASUPPORT volatile s_enmSup = kCpuDetect;
    CPUFMASUPPORT enmSup = s_enmSup;
    if (enmSup != kCpuDetect)
    { }
    else
    {
        if (ASMCpuId_ECX(1) & X86_CPUID_FEATURE_ECX_FMA)
            enmSup = kCpuWithFma3;
        else if (ASMCpuId_ECX(UINT32_C(0x80000001)) & X86_CPUID_AMD_FEATURE_ECX_FMA4)
            enmSup = kCpuWithFma4;
        else
            enmSup = kCpuWithoutFma;
        s_enmSup = enmSup;
    }
    if (enmSup == kCpuWithFma3)
        return rtNoCrtMathFma3(rdFactor1, rdFactor2, rdAddend);
    if (enmSup == kCpuWithFma4)
        return rtNoCrtMathFma4(rdFactor1, rdFactor2, rdAddend);
#endif

    /*
     * Fall back on SoftFloat.
     */
    AssertCompile(sizeof(rdFactor1) == sizeof(RTFLOAT64U));
    softfloat_state_t State = SOFTFLOAT_STATE_INIT_DEFAULTS(); /** @todo init from MXCSR/FCW */
    union { RTFLOAT64U Iprt; float64_t SoftFloat; } uFactor1, uFactor2, uAddend, uResult;
    uFactor1.Iprt.rd = rdFactor1;
    uFactor2.Iprt.rd = rdFactor2;
    uAddend.Iprt.rd  = rdAddend;
    uResult.SoftFloat = f64_mulAdd(uFactor1.SoftFloat, uFactor2.SoftFloat, uAddend.SoftFloat, &State);
    return uResult.Iprt.rd;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(fma);

