/* $Id: bs3-cmn-TestCheckExtCtx.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3TestCheckExtCtx
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


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Field descriptors. */
static struct
{
    uint8_t             enmMethod;
    uint8_t             cb;
    uint16_t            off;
    const char BS3_FAR *pszName;
} const g_aFields[] =
{
#define BS3EXTCTX_FIELD_ENTRY(a_enmMethod, a_Member) \
        { a_enmMethod, RT_SIZEOFMEMB(BS3EXTCTX, a_Member), RT_OFFSETOF(BS3EXTCTX, a_Member), #a_Member }
    BS3EXTCTX_FIELD_ENTRY(BS3EXTCTXMETHOD_END,      fXcr0Saved),

#define BS3EXTCTX_FIELD_ENTRY_CTX(a_enmMethod, a_Member) \
        { a_enmMethod, RT_SIZEOFMEMB(BS3EXTCTX, Ctx.a_Member), RT_OFFSETOF(BS3EXTCTX, Ctx.a_Member), #a_Member }
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.FCW),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.Dummy1),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.FSW),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.Dummy2),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.FTW),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.Dummy3),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.FPUIP),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.CS),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.FOP),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.FPUOO),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.FPUOS),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.regs[0]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.regs[1]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.regs[2]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.regs[3]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.regs[4]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.regs[5]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.regs[6]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_ANCIENT,  Ancient.regs[7]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.FCW),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.FSW),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.FTW),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.FOP),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.FPUIP),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.CS),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.Rsrvd1),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.FPUDP),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.DS),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.Rsrvd2),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.MXCSR),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.MXCSR_MASK),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aRegs[0]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aRegs[1]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aRegs[2]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aRegs[3]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aRegs[4]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aRegs[5]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aRegs[6]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aRegs[7]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[0]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[1]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[2]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[3]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[4]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[5]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[6]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[7]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[8]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[9]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[10]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[11]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[12]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[13]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[14]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_FXSAVE,   x87.aXMM[15]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.FCW),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.FSW),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.FTW),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.FOP),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.FPUIP),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.CS),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.Rsrvd1),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.FPUDP),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.DS),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.Rsrvd2),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.MXCSR),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.MXCSR_MASK),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aRegs[0]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aRegs[1]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aRegs[2]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aRegs[3]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aRegs[4]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aRegs[5]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aRegs[6]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aRegs[7]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[0]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[1]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[2]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[3]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[4]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[5]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[6]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[7]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[8]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[9]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[10]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[11]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[12]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[13]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[14]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.x87.aXMM[15]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.Hdr.bmXState),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.Hdr.bmXComp),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[0]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[1]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[2]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[3]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[4]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[5]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[6]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[7]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[8]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[9]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[10]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[11]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[12]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[13]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[14]),
    BS3EXTCTX_FIELD_ENTRY_CTX(BS3EXTCTXMETHOD_XSAVE,    x.u.YmmHi.aYmmHi[15]),
};


#undef Bs3TestCheckExtCtx
BS3_CMN_DEF(bool, Bs3TestCheckExtCtx,(PCBS3EXTCTX pActualExtCtx, PCBS3EXTCTX pExpectedExtCtx, uint16_t fFlags,
                                      const char BS3_FAR *pszMode, uint16_t idTestStep))
{
    /*
     * Make sure the context of a similar and valid before starting.
     */
    if (!pActualExtCtx || pActualExtCtx->u16Magic != BS3EXTCTX_MAGIC)
        return Bs3TestFailedF("%u - %s: invalid actual context pointer: %p", idTestStep, pszMode, pActualExtCtx);
    if (!pExpectedExtCtx || pExpectedExtCtx->u16Magic != BS3EXTCTX_MAGIC)
        return Bs3TestFailedF("%u - %s: invalid expected context pointer: %p", idTestStep, pszMode, pExpectedExtCtx);
    if (   pActualExtCtx->enmMethod != pExpectedExtCtx->enmMethod
        || pActualExtCtx->enmMethod == BS3EXTCTXMETHOD_INVALID
        || pActualExtCtx->enmMethod >= BS3EXTCTXMETHOD_END)
        return Bs3TestFailedF("%u - %s: mismatching or/and invalid context methods: %d vs %d",
                              idTestStep, pszMode, pActualExtCtx->enmMethod, pExpectedExtCtx->enmMethod);
    if (pActualExtCtx->cb != pExpectedExtCtx->cb)
        return Bs3TestFailedF("%u - %s: mismatching context sizes: %#x vs %#x",
                              idTestStep, pszMode, pActualExtCtx->cb, pExpectedExtCtx->cb);

    /*
     * Try get the job done quickly with a memory compare.
     */
    if (Bs3MemCmp(pActualExtCtx, pExpectedExtCtx, pActualExtCtx->cb) == 0)
        return true;

    Bs3TestFailedF("%u - %s: context memory differs", idTestStep, pszMode); // debug
    {
        uint8_t const BS3_FAR *pb1 = (uint8_t const BS3_FAR *)pActualExtCtx;
        uint8_t const BS3_FAR *pb2 = (uint8_t const BS3_FAR *)pExpectedExtCtx;
        unsigned const         cb  = pActualExtCtx->cb;
        unsigned               off;
        for (off = 0; off < cb; off++)
            if (pb1[off] != pb2[off])
            {
                unsigned            offStart = off++;
                const char BS3_FAR *pszName  = NULL;
                unsigned            cbDiff   = 0;
                unsigned            idxField;
                for (idxField = 0; idxField < RT_ELEMENTS(g_aFields); idxField++)
                    if (   offStart - g_aFields[idxField].off < g_aFields[idxField].cb
                        && (   g_aFields[idxField].enmMethod == BS3EXTCTXMETHOD_END
                            || g_aFields[idxField].enmMethod == pActualExtCtx->enmMethod))
                    {
                        pszName  = g_aFields[idxField].pszName;
                        cbDiff   = g_aFields[idxField].cb;
                        offStart = g_aFields[idxField].off;
                        off      = offStart + cbDiff;
                        break;
                    }
                if (!pszName)
                {
                    while (off < cb && pb1[off] != pb2[off])
                        off++;
                    cbDiff = off - offStart;
                    pszName = "unknown";
                }
                switch (cbDiff)
                {
                    case 1:
                        Bs3TestFailedF("%u - %s: Byte difference at %#x (%s): %#04x, expected %#04x",
                                       idTestStep, pszMode, offStart, pszName, pb1[offStart], pb2[offStart]);
                        break;
                    case 2:
                        Bs3TestFailedF("%u - %s: Word difference at %#x (%s): %#06x, expected %#06x",
                                       idTestStep, pszMode, offStart, pszName,
                                       RT_MAKE_U16(pb1[offStart], pb1[offStart + 1]),
                                       RT_MAKE_U16(pb2[offStart], pb2[offStart + 1]));
                        break;
                    case 4:
                        Bs3TestFailedF("%u - %s: DWord difference at %#x (%s): %#010RX32, expected %#010RX32",
                                       idTestStep, pszMode, offStart, pszName,
                                       RT_MAKE_U32_FROM_U8(pb1[offStart], pb1[offStart + 1], pb1[offStart + 2], pb1[offStart + 3]),
                                       RT_MAKE_U32_FROM_U8(pb2[offStart], pb2[offStart + 1], pb2[offStart + 2], pb2[offStart + 3]));
                        break;
                    case 8:
                        Bs3TestFailedF("%u - %s: QWord difference at %#x (%s): %#018RX64, expected %#018RX64",
                                       idTestStep, pszMode, offStart, pszName,
                                       RT_MAKE_U64_FROM_U8(pb1[offStart], pb1[offStart + 1], pb1[offStart + 2], pb1[offStart + 3],
                                                           pb1[offStart + 4], pb1[offStart + 5], pb1[offStart + 6], pb1[offStart + 7]),
                                       RT_MAKE_U64_FROM_U8(pb2[offStart], pb2[offStart + 1], pb2[offStart + 2], pb2[offStart + 3],
                                                           pb2[offStart + 4], pb2[offStart + 5], pb2[offStart + 6], pb2[offStart + 7]));
                        break;
                    case 16:
                        Bs3TestFailedF("%u - %s: DQword difference at %#x (%s): \n"
                                       "got      %#018RX64'%#018RX64\n"
                                       "expected %#018RX64'%#018RX64",
                                       idTestStep, pszMode, offStart, pszName,
                                       RT_MAKE_U64_FROM_U8(pb1[offStart + 8], pb1[offStart + 9], pb1[offStart + 10], pb1[offStart + 11],
                                                           pb1[offStart + 12], pb1[offStart + 13], pb1[offStart + 14], pb1[offStart + 15]),
                                       RT_MAKE_U64_FROM_U8(pb1[offStart], pb1[offStart + 1], pb1[offStart + 2], pb1[offStart + 3],
                                                           pb1[offStart + 4], pb1[offStart + 5], pb1[offStart + 6], pb1[offStart + 7]),

                                       RT_MAKE_U64_FROM_U8(pb2[offStart + 8], pb2[offStart + 9], pb2[offStart + 10], pb2[offStart + 11],
                                                           pb2[offStart + 12], pb2[offStart + 13], pb2[offStart + 14], pb2[offStart + 15]),
                                       RT_MAKE_U64_FROM_U8(pb2[offStart], pb2[offStart + 1], pb2[offStart + 2], pb2[offStart + 3],
                                                           pb2[offStart + 4], pb2[offStart + 5], pb2[offStart + 6], pb2[offStart + 7])
                                       );
                        break;

                    default:
                        Bs3TestFailedF("%u - %s: %#x..%#x differs (%s)\n"
                                       "got      %.*Rhxs\n"
                                       "expected %.*Rhxs",
                                       idTestStep, pszMode, offStart, off - 1, pszName,
                                       off - offStart, &pb1[offStart],
                                       off - offStart, &pb2[offStart]);
                        break;
                }
            }
    }
    return false;
}

