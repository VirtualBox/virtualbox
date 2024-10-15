/* $Id: bs3-cmn-ExtCtxGetSize.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3ExtCtxGetSize
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include "bs3kit-template-header.h"
#include <iprt/asm-amd64-x86.h>


#undef Bs3ExtCtxGetSize
BS3_CMN_DEF(uint16_t, Bs3ExtCtxGetSize,(uint64_t BS3_FAR *pfFlags))
{
    uint32_t fEcx, fEdx;
    *pfFlags = 0;

    ASMCpuIdExSlow(1, 0, 0, 0, NULL, NULL, &fEcx, &fEdx);
#if 1 /* To disable xsave/xrstor till IEM groks it... */
    if (fEcx & X86_CPUID_FEATURE_ECX_XSAVE)
    {
        uint32_t fEax;
        ASMCpuIdExSlow(13, 0, 0, 0, &fEax, NULL, &fEcx, &fEdx);
        if (   fEcx >= sizeof(X86FXSTATE) + sizeof(X86XSAVEHDR)
            && fEcx < _32K)
        {
            *pfFlags = fEax | ((uint64_t)fEdx << 32);
            return RT_UOFFSETOF(BS3EXTCTX, Ctx) + RT_ALIGN(fEcx, 256);
        }
    }
#endif
    if (fEdx & X86_CPUID_FEATURE_EDX_FXSR)
        return RT_UOFFSETOF(BS3EXTCTX, Ctx) + sizeof(X86FXSTATE);
    return RT_UOFFSETOF(BS3EXTCTX, Ctx) + sizeof(X86FPUSTATE);
}

