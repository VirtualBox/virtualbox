/* $Id: bs3-cmn-ExtCtxInit.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3ExtCtxInit
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


#undef Bs3ExtCtxInit
BS3_CMN_DEF(PBS3EXTCTX, Bs3ExtCtxInit,(PBS3EXTCTX pExtCtx, uint16_t cbExtCtx, uint64_t fFlags))
{
    Bs3MemSet(pExtCtx, 0, cbExtCtx);

    if (cbExtCtx >= RT_UOFFSETOF(BS3EXTCTX, Ctx) + sizeof(X86FXSTATE) + sizeof(X86XSAVEHDR))
    {
        BS3_ASSERT(fFlags & XSAVE_C_X87);
        pExtCtx->enmMethod = BS3EXTCTXMETHOD_XSAVE;
        pExtCtx->Ctx.x.Hdr.bmXState = fFlags;

        /* Setting bit 6 (0x40) here as it kept sneaking in when loading/saving state in 16-bit and v8086 mode. */
        pExtCtx->Ctx.x.x87.FCW        = X86_FCW_RC_NEAREST | X86_FCW_PC_64 /* go figure:*/ | RT_BIT(6);
        pExtCtx->Ctx.x.x87.MXCSR      = X86_MXCSR_RC_NEAREST;
        pExtCtx->Ctx.x.x87.MXCSR_MASK = 0xffff;
    }
    else if (cbExtCtx >= RT_UOFFSETOF(BS3EXTCTX, Ctx) + sizeof(X86FXSTATE))
    {
        BS3_ASSERT(fFlags == 0);
        pExtCtx->enmMethod = BS3EXTCTXMETHOD_FXSAVE;
        pExtCtx->Ctx.x87.FCW          = X86_FCW_RC_NEAREST | X86_FCW_PC_64 /* go figure:*/ | RT_BIT(6);
        pExtCtx->Ctx.x87.MXCSR        = X86_MXCSR_RC_NEAREST;
        pExtCtx->Ctx.x87.MXCSR_MASK   = 0xffff;
    }
    else
    {
        BS3_ASSERT(fFlags == 0);
        BS3_ASSERT(cbExtCtx >= RT_UOFFSETOF(BS3EXTCTX, Ctx) + sizeof(X86FPUSTATE));
        pExtCtx->enmMethod = BS3EXTCTXMETHOD_ANCIENT;
        pExtCtx->Ctx.Ancient.FCW      = X86_FCW_RC_NEAREST | X86_FCW_PC_64 /* go figure:*/ | RT_BIT(6);
        pExtCtx->Ctx.Ancient.FTW      = UINT16_MAX;  /* all registers empty */
    }

    pExtCtx->cb             = cbExtCtx;
    pExtCtx->u16Magic       = BS3EXTCTX_MAGIC;
    pExtCtx->fXcr0Nominal   = fFlags;
    pExtCtx->fXcr0Saved     = fFlags;
    return pExtCtx;
}

