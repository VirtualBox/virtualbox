/* $Id: bs3-cmn-TestCheckRegCtxEx.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - TestCheckRegCtxEx
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


#undef Bs3TestCheckRegCtxEx
BS3_CMN_DEF(bool, Bs3TestCheckRegCtxEx,(PCBS3REGCTX pActualCtx, PCBS3REGCTX pExpectedCtx, uint16_t cbPcAdjust, int16_t cbSpAcjust,
                                        uint32_t fExtraEfl, const char BS3_FAR *pszMode, uint16_t idTestStep))
{
    uint16_t const cErrorsBefore = Bs3TestSubErrorCount();
    uint8_t  const fbFlags       = pActualCtx->fbFlags | pExpectedCtx->fbFlags;

#define CHECK_MEMBER(a_szName, a_szFmt, a_Actual, a_Expected) \
    do { \
        if ((a_Actual) == (a_Expected)) { /* likely */ } \
        else Bs3TestFailedF("%u - %s: " a_szName "=" a_szFmt " expected " a_szFmt, idTestStep, pszMode, (a_Actual), (a_Expected)); \
    } while (0)

    CHECK_MEMBER("rax",     "%08RX64",  pActualCtx->rax.u,    pExpectedCtx->rax.u);
    CHECK_MEMBER("rcx",     "%08RX64",  pActualCtx->rcx.u,    pExpectedCtx->rcx.u);
    CHECK_MEMBER("rdx",     "%08RX64",  pActualCtx->rdx.u,    pExpectedCtx->rdx.u);
    CHECK_MEMBER("rbx",     "%08RX64",  pActualCtx->rbx.u,    pExpectedCtx->rbx.u);
    CHECK_MEMBER("rsp",     "%08RX64",  pActualCtx->rsp.u,    pExpectedCtx->rsp.u + cbSpAcjust);
    CHECK_MEMBER("rbp",     "%08RX64",  pActualCtx->rbp.u,    pExpectedCtx->rbp.u);
    CHECK_MEMBER("rsi",     "%08RX64",  pActualCtx->rsi.u,    pExpectedCtx->rsi.u);
    CHECK_MEMBER("rdi",     "%08RX64",  pActualCtx->rdi.u,    pExpectedCtx->rdi.u);
    if (!(fbFlags & BS3REG_CTX_F_NO_AMD64))
    {
        CHECK_MEMBER("r8",      "%08RX64",  pActualCtx->r8.u,     pExpectedCtx->r8.u);
        CHECK_MEMBER("r9",      "%08RX64",  pActualCtx->r9.u,     pExpectedCtx->r9.u);
        CHECK_MEMBER("r10",     "%08RX64",  pActualCtx->r10.u,    pExpectedCtx->r10.u);
        CHECK_MEMBER("r11",     "%08RX64",  pActualCtx->r11.u,    pExpectedCtx->r11.u);
        CHECK_MEMBER("r12",     "%08RX64",  pActualCtx->r12.u,    pExpectedCtx->r12.u);
        CHECK_MEMBER("r13",     "%08RX64",  pActualCtx->r13.u,    pExpectedCtx->r13.u);
        CHECK_MEMBER("r14",     "%08RX64",  pActualCtx->r14.u,    pExpectedCtx->r14.u);
        CHECK_MEMBER("r15",     "%08RX64",  pActualCtx->r15.u,    pExpectedCtx->r15.u);
    }
    CHECK_MEMBER("rflags",  "%08RX64",  pActualCtx->rflags.u, pExpectedCtx->rflags.u | fExtraEfl);
    CHECK_MEMBER("rip",     "%08RX64",  pActualCtx->rip.u,    pExpectedCtx->rip.u + cbPcAdjust);
    CHECK_MEMBER("cs",      "%04RX16",  pActualCtx->cs,       pExpectedCtx->cs);
    CHECK_MEMBER("ds",      "%04RX16",  pActualCtx->ds,       pExpectedCtx->ds);
    CHECK_MEMBER("es",      "%04RX16",  pActualCtx->es,       pExpectedCtx->es);
    CHECK_MEMBER("fs",      "%04RX16",  pActualCtx->fs,       pExpectedCtx->fs);
    CHECK_MEMBER("gs",      "%04RX16",  pActualCtx->gs,       pExpectedCtx->gs);

    if (!(fbFlags & BS3REG_CTX_F_NO_TR_LDTR))
    {
        CHECK_MEMBER("tr",      "%04RX16",  pActualCtx->tr,       pExpectedCtx->tr);
        CHECK_MEMBER("ldtr",    "%04RX16",  pActualCtx->ldtr,     pExpectedCtx->ldtr);
    }
    CHECK_MEMBER("bMode",   "%#04x",    pActualCtx->bMode,    pExpectedCtx->bMode);
    CHECK_MEMBER("bCpl",    "%u",       pActualCtx->bCpl,     pExpectedCtx->bCpl);

    if (!(fbFlags & BS3REG_CTX_F_NO_CR0_IS_MSW))
        CHECK_MEMBER("cr0", "%08RX64",  pActualCtx->cr0.u,    pExpectedCtx->cr0.u);
    else
        CHECK_MEMBER("msw", "%08RX16",  pActualCtx->cr0.u16,  pExpectedCtx->cr0.u16);
    if (!(fbFlags & BS3REG_CTX_F_NO_CR2_CR3))
    {
        /* cr2Range is always zero except if changed by the test worker */
        if ((pActualCtx->cr2.u < pExpectedCtx->cr2.u ||
             pActualCtx->cr2.u > pExpectedCtx->cr2.u + pExpectedCtx->cr2Range))
        {
            CHECK_MEMBER("cr2", "%08RX64",  pActualCtx->cr2.u,    pExpectedCtx->cr2.u);
        }
        CHECK_MEMBER("cr3", "%08RX64",  pActualCtx->cr3.u,    pExpectedCtx->cr3.u);
    }
    if (!(fbFlags & BS3REG_CTX_F_NO_CR4))
        CHECK_MEMBER("cr4", "%08RX64",  pActualCtx->cr4.u,    pExpectedCtx->cr4.u);
#undef CHECK_MEMBER

    return Bs3TestSubErrorCount() == cErrorsBefore;
}

