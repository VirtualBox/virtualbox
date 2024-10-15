/* $Id: cidet-instr-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU Instruction Decoding & Execution Tests - First bunch of instructions.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#include "cidet.h"
#include <VBox/err.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*
 * Shorter defines for the EFLAGS to save table space.
 */
#undef  CF
#undef  PF
#undef  AF
#undef  ZF
#undef  SF
#undef  OF

#define CF X86_EFL_CF
#define PF X86_EFL_PF
#define AF X86_EFL_AF
#define ZF X86_EFL_ZF
#define SF X86_EFL_SF
#define OF X86_EFL_OF


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct CIDET2IN1OUTWITHFLAGSU8ENTRY
{
    uint8_t     uIn1;
    uint8_t     uIn2;
    uint16_t    fEFlagsIn;
    uint8_t     uOut;
    uint16_t    fEFlagsOut;
} CIDET2IN1OUTWITHFLAGSU8ENTRY;
typedef CIDET2IN1OUTWITHFLAGSU8ENTRY const *PCCIDET2IN1OUTWITHFLAGSU8ENTRY;

typedef struct CIDET2IN1OUTWITHFLAGSU16ENTRY
{
    uint16_t    uIn1;
    uint16_t    uIn2;
    uint16_t    fEFlagsIn;
    uint16_t    uOut;
    uint16_t    fEFlagsOut;
} CIDET2IN1OUTWITHFLAGSU16ENTRY;
typedef CIDET2IN1OUTWITHFLAGSU16ENTRY const *PCCIDET2IN1OUTWITHFLAGSU16ENTRY;

typedef struct CIDET2IN1OUTWITHFLAGSU32ENTRY
{
    uint32_t    uIn1;
    uint32_t    uIn2;
    uint16_t    fEFlagsIn;
    uint32_t    uOut;
    uint16_t    fEFlagsOut;
} CIDET2IN1OUTWITHFLAGSU32ENTRY;
typedef CIDET2IN1OUTWITHFLAGSU32ENTRY const *PCCIDET2IN1OUTWITHFLAGSU32ENTRY;

typedef struct CIDET2IN1OUTWITHFLAGSU64ENTRY
{
    uint64_t    uIn1;
    uint64_t    uIn2;
    uint16_t    fEFlagsIn;
    uint64_t    uOut;
    uint16_t    fEFlagsOut;
} CIDET2IN1OUTWITHFLAGSU64ENTRY;
typedef CIDET2IN1OUTWITHFLAGSU64ENTRY const *PCCIDET2IN1OUTWITHFLAGSU64ENTRY;

typedef struct CIDET2IN1OUTWITHFLAGS
{
    PCCIDET2IN1OUTWITHFLAGSU8ENTRY  pa8Entries;
    PCCIDET2IN1OUTWITHFLAGSU16ENTRY pa16Entries;
    PCCIDET2IN1OUTWITHFLAGSU32ENTRY pa32Entries;
    PCCIDET2IN1OUTWITHFLAGSU64ENTRY pa64Entries;
    uint16_t c8Entries;
    uint16_t c16Entries;
    uint16_t c32Entries;
    uint16_t c64Entries;
    uint32_t fRelevantEFlags;
} CIDET2IN1OUTWITHFLAGS;

#define CIDET2IN1OUTWITHFLAGS_INITIALIZER(a_fRelevantEFlags) \
    { \
        &s_a8Results[0], &s_a16Results[0], &s_a32Results[0], &s_a64Results[0], \
        RT_ELEMENTS(s_a8Results), RT_ELEMENTS(s_a16Results), RT_ELEMENTS(s_a32Results), RT_ELEMENTS(s_a64Results), \
        (a_fRelevantEFlags) \
    }


/**
 * Generic worker for a FNCIDETSETUPINOUT function with two GPR/MEM registers,
 * storing result in the first and flags.
 *
 * @returns See FNCIDETSETUPINOUT.
 * @param   pThis           The core CIDET state structure.  The InCtx
 *                          and ExpectedCtx members will be modified.
 * @param   fInvalid        When set, get the next invalid operands that will
 *                          cause exceptions/faults.
 * @param   pResults        The result collection.
 */
static int CidetGenericIn2Out1WithFlags(PCIDETCORE pThis, bool fInvalid, CIDET2IN1OUTWITHFLAGS const *pResults)
{
    int rc;

    Assert(pThis->idxMrmRegOp < 2);
    Assert(pThis->idxMrmRmOp  < 2);
    Assert(pThis->idxMrmRmOp != pThis->idxMrmRegOp);
    AssertCompile(RT_ELEMENTS(pThis->aiInOut) >= 4);

    if (!fInvalid)
    {
        if (   !pThis->fHasRegCollisionDirect
            && !pThis->fHasRegCollisionMem)
        {
            pThis->InCtx.rfl       &= ~(uint64_t)pResults->fRelevantEFlags;
            pThis->ExpectedCtx.rfl &= ~(uint64_t)pResults->fRelevantEFlags;
            switch (pThis->aOperands[0].cb)
            {
                case 1:
                {
                    uint16_t                       idx    = ++pThis->aiInOut[0] % pResults->c8Entries;
                    PCCIDET2IN1OUTWITHFLAGSU8ENTRY pEntry = &pResults->pa8Entries[idx];
                    rc = idx ? VINF_SUCCESS : VINF_EOF;

                    *pThis->aOperands[0].In.pu8       = pEntry->uIn1;
                    *pThis->aOperands[1].In.pu8       = pEntry->uIn2;
                    pThis->InCtx.rfl                 |= pEntry->fEFlagsIn;

                    *pThis->aOperands[0].Expected.pu8 = pEntry->uOut;
                    *pThis->aOperands[1].Expected.pu8 = pEntry->uIn2;
                    pThis->ExpectedCtx.rfl           |= pEntry->fEFlagsOut;
                    break;
                }

                case 2:
                {
                    uint16_t                        idx    = ++pThis->aiInOut[1] % pResults->c16Entries;
                    PCCIDET2IN1OUTWITHFLAGSU16ENTRY pEntry = &pResults->pa16Entries[idx];
                    rc = idx ? VINF_SUCCESS : VINF_EOF;

                    *pThis->aOperands[0].In.pu16       = pEntry->uIn1;
                    *pThis->aOperands[1].In.pu16       = pEntry->uIn2;
                    pThis->InCtx.rfl                  |= pEntry->fEFlagsIn;

                    *pThis->aOperands[0].Expected.pu16 = pEntry->uOut;
                    *pThis->aOperands[1].Expected.pu16 = pEntry->uIn2;
                    pThis->ExpectedCtx.rfl            |= pEntry->fEFlagsOut;
                    break;
                }

                case 4:
                {
                    uint16_t                        idx    = ++pThis->aiInOut[2] % pResults->c32Entries;
                    PCCIDET2IN1OUTWITHFLAGSU32ENTRY pEntry = &pResults->pa32Entries[idx];
                    rc = idx ? VINF_SUCCESS : VINF_EOF;

                    *pThis->aOperands[0].In.pu32       = pEntry->uIn1;
                    *pThis->aOperands[1].In.pu32       = pEntry->uIn2;
                    pThis->InCtx.rfl                  |= pEntry->fEFlagsIn;

                    *pThis->aOperands[0].Expected.pu32 = pEntry->uOut;
                    if (!pThis->aOperands[0].fIsMem)
                        pThis->aOperands[0].Expected.pu32[1] = 0;
                    *pThis->aOperands[1].Expected.pu32 = pEntry->uIn2;
                    pThis->ExpectedCtx.rfl            |= pEntry->fEFlagsOut;
                    break;
                }

                case 8:
                {
                    uint16_t                        idx    = ++pThis->aiInOut[3] % pResults->c64Entries;
                    PCCIDET2IN1OUTWITHFLAGSU64ENTRY pEntry = &pResults->pa64Entries[idx];
                    rc = idx ? VINF_SUCCESS : VINF_EOF;

                    *pThis->aOperands[0].In.pu64       = pEntry->uIn1;
                    *pThis->aOperands[1].In.pu64       = pEntry->uIn2;
                    pThis->InCtx.rfl                  |= pEntry->fEFlagsIn;

                    *pThis->aOperands[0].Expected.pu64 = pEntry->uOut;
                    *pThis->aOperands[1].Expected.pu64 = pEntry->uIn2;
                    pThis->ExpectedCtx.rfl            |= pEntry->fEFlagsOut;
                    break;
                }

                default:
                    AssertFailed();
                    rc = VERR_INTERNAL_ERROR_3;
            }
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_NO_DATA;
    return rc;
}


static DECLCALLBACK(int) cidetInOutAdd(PCIDETCORE pThis, bool fInvalid)
{
    static const CIDET2IN1OUTWITHFLAGSU8ENTRY s_a8Results[] =
    {
        { UINT8_C(0x00), UINT8_C(0x00), 0, UINT8_C(0x00), ZF | PF },
        { UINT8_C(0xff), UINT8_C(0x01), 0, UINT8_C(0x00), CF | ZF | AF | PF  },
        { UINT8_C(0x7f), UINT8_C(0x80), 0, UINT8_C(0xff), SF | PF },
        { UINT8_C(0x01), UINT8_C(0x01), 0, UINT8_C(0x02), 0 },
    };
    static const CIDET2IN1OUTWITHFLAGSU16ENTRY s_a16Results[] =
    {
        { UINT16_C(0x0000), UINT16_C(0x0000), 0, UINT16_C(0x0000), ZF | PF },
        { UINT16_C(0xfefd), UINT16_C(0x0103), 0, UINT16_C(0x0000), CF | ZF | AF | PF },
        { UINT16_C(0x8e7d), UINT16_C(0x7182), 0, UINT16_C(0xffff), SF | PF },
        { UINT16_C(0x0001), UINT16_C(0x0001), 0, UINT16_C(0x0002), 0 },
    };
    static const CIDET2IN1OUTWITHFLAGSU32ENTRY s_a32Results[] =
    {
        { UINT32_C(0x00000000), UINT32_C(0x00000000), 0, UINT32_C(0x00000000), ZF | PF },
        { UINT32_C(0xfefdfcfb), UINT32_C(0x01020305), 0, UINT32_C(0x00000000), CF | ZF | AF | PF },
        { UINT32_C(0x8efdfcfb), UINT32_C(0x71020304), 0, UINT32_C(0xffffffff), SF | PF },
        { UINT32_C(0x00000001), UINT32_C(0x00000001), 0, UINT32_C(0x00000002), 0 },
    };
    static const CIDET2IN1OUTWITHFLAGSU64ENTRY s_a64Results[] =
    {
        { UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000), 0, UINT64_C(0x0000000000000000), ZF | PF },
        { UINT64_C(0xfefdfcfbfaf9f8f7), UINT64_C(0x0102030405060709), 0, UINT64_C(0x0000000000000000), CF | ZF | AF | PF },
        { UINT64_C(0x7efdfcfbfaf9f8f7), UINT64_C(0x8102030405060708), 0, UINT64_C(0xffffffffffffffff), SF | PF },
        { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000001), 0, UINT64_C(0x0000000000000002), 0 },
    };
    static const CIDET2IN1OUTWITHFLAGS s_Results = CIDET2IN1OUTWITHFLAGS_INITIALIZER(CF | PF | AF | SF | OF);
    return CidetGenericIn2Out1WithFlags(pThis, fInvalid, &s_Results);
}


/** First bunch of instructions.  */
const CIDETINSTR g_aCidetInstructions1[] =
{
#if 1
    {
        "add Eb,Gb", cidetInOutAdd,  1, {0x00, 0, 0}, 0, 2,
        {   CIDET_OF_K_GPR | CIDET_OF_Z_BYTE | CIDET_OF_M_RM | CIDET_OF_A_RW,
            CIDET_OF_K_GPR | CIDET_OF_Z_BYTE | CIDET_OF_M_REG | CIDET_OF_A_R,
            0, 0 }, CIDET_IF_MODRM
    },
#endif
#if 1
    {
        "add Ev,Gv", cidetInOutAdd,  1, {0x01, 0, 0}, 0, 2,
        {   CIDET_OF_K_GPR | CIDET_OF_Z_VAR_WDQ | CIDET_OF_M_RM | CIDET_OF_A_RW,
            CIDET_OF_K_GPR | CIDET_OF_Z_VAR_WDQ | CIDET_OF_M_REG | CIDET_OF_A_R,
            0, 0 }, CIDET_IF_MODRM
    },
#endif
};
/** Number of instruction in the g_aInstructions1 array. */
const uint32_t g_cCidetInstructions1 = RT_ELEMENTS(g_aCidetInstructions1);

