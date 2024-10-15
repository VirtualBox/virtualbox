/* $Id: bs3-cmn-ExtCtxSetYmm.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3ExtCtxSetYmm
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


#undef Bs3ExtCtxSetYmm
BS3_CMN_DEF(bool, Bs3ExtCtxSetYmm,(PBS3EXTCTX pExtCtx, uint8_t iReg, PCRTUINT256U pValue, uint8_t cbValue))
{
#ifdef _MSC_VER /* No-SSE hack */
    BS3EXTCTX volatile RT_FAR *pExtCtxNoSseHack = (BS3EXTCTX volatile RT_FAR *)pExtCtx;
# define pExtCtx pExtCtxNoSseHack
#endif
    BS3_ASSERT(cbValue == 16 || cbValue == 32);
    switch (pExtCtx->enmMethod)
    {
        case BS3EXTCTXMETHOD_FXSAVE:
            if (iReg < RT_ELEMENTS(pExtCtx->Ctx.x87.aXMM))
            {
                pExtCtx->Ctx.x87.aXMM[iReg].au64[0] = pValue->DQWords.dqw0.au64[0];
                pExtCtx->Ctx.x87.aXMM[iReg].au64[1] = pValue->DQWords.dqw0.au64[1];
                return true;
            }
            break;

        case BS3EXTCTXMETHOD_XSAVE:
            if (iReg < RT_ELEMENTS(pExtCtx->Ctx.x.x87.aXMM))
            {
                pExtCtx->Ctx.x87.aXMM[iReg].au64[0] = pValue->DQWords.dqw0.au64[0];
                pExtCtx->Ctx.x87.aXMM[iReg].au64[1] = pValue->DQWords.dqw0.au64[1];
                if (pExtCtx->fXcr0Nominal & XSAVE_C_YMM)
                {
                    if (cbValue >= 32)
                    {
                        pExtCtx->Ctx.x.u.YmmHi.aYmmHi[iReg].au64[0] = pValue->DQWords.dqw1.au64[0];
                        pExtCtx->Ctx.x.u.YmmHi.aYmmHi[iReg].au64[1] = pValue->DQWords.dqw1.au64[1];
                    }
                    else
                    {
                        pExtCtx->Ctx.x.u.YmmHi.aYmmHi[iReg].au64[0] = 0;
                        pExtCtx->Ctx.x.u.YmmHi.aYmmHi[iReg].au64[1] = 0;
                    }
                    /** @todo zero high ZMM part. */
                }
                return true;
            }
            break;
    }
    return false;
}

