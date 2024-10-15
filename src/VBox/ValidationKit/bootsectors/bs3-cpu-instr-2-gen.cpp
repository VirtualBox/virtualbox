/* $Id: bs3-cpu-instr-2-gen.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - bs3-cpu-instr-2, Test Data Generator.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/x86.h>
#include <VBox/disopcode-x86-amd64.h>
#include "bs3-cpu-instr-2.h"


/*********************************************************************************************************************************
*   External Functions                                                                                                           *
*********************************************************************************************************************************/
#define PROTOTYPE_BINARY(a_Ins) \
    DECLASM(uint32_t) RT_CONCAT(GenU8_,a_Ins)( uint8_t,  uint8_t,  uint32_t, uint8_t *); \
    DECLASM(uint32_t) RT_CONCAT(GenU16_,a_Ins)(uint16_t, uint16_t, uint32_t, uint16_t *); \
    DECLASM(uint32_t) RT_CONCAT(GenU32_,a_Ins)(uint32_t, uint32_t, uint32_t, uint32_t *); \
    DECLASM(uint32_t) RT_CONCAT(GenU64_,a_Ins)(uint64_t, uint64_t, uint32_t, uint64_t *)

PROTOTYPE_BINARY(and);
PROTOTYPE_BINARY(or);
PROTOTYPE_BINARY(xor);
PROTOTYPE_BINARY(test);

PROTOTYPE_BINARY(add);
PROTOTYPE_BINARY(adc);
PROTOTYPE_BINARY(sub);
PROTOTYPE_BINARY(sbb);
PROTOTYPE_BINARY(cmp);

PROTOTYPE_BINARY(bt);
PROTOTYPE_BINARY(btc);
PROTOTYPE_BINARY(btr);
PROTOTYPE_BINARY(bts);


#define PROTOTYPE_SHIFT(a_Ins) \
    DECLASM(uint32_t) RT_CONCAT(GenU8_,a_Ins)( uint8_t,  uint8_t, uint32_t, uint8_t *); \
    DECLASM(uint32_t) RT_CONCAT(GenU16_,a_Ins)(uint16_t, uint8_t, uint32_t, uint16_t *); \
    DECLASM(uint32_t) RT_CONCAT(GenU32_,a_Ins)(uint32_t, uint8_t, uint32_t, uint32_t *); \
    DECLASM(uint32_t) RT_CONCAT(GenU64_,a_Ins)(uint64_t, uint8_t, uint32_t, uint64_t *); \
    DECLASM(uint32_t) RT_CONCAT3(GenU8_,a_Ins,_Ib)( uint8_t,  uint8_t, uint32_t, uint8_t *); \
    DECLASM(uint32_t) RT_CONCAT3(GenU16_,a_Ins,_Ib)(uint16_t, uint8_t, uint32_t, uint16_t *); \
    DECLASM(uint32_t) RT_CONCAT3(GenU32_,a_Ins,_Ib)(uint32_t, uint8_t, uint32_t, uint32_t *); \
    DECLASM(uint32_t) RT_CONCAT3(GenU64_,a_Ins,_Ib)(uint64_t, uint8_t, uint32_t, uint64_t *)

PROTOTYPE_SHIFT(shl);
PROTOTYPE_SHIFT(shr);
PROTOTYPE_SHIFT(sar);
PROTOTYPE_SHIFT(rol);
PROTOTYPE_SHIFT(ror);
PROTOTYPE_SHIFT(rcl);
PROTOTYPE_SHIFT(rcr);


static uint32_t RandEflStatus(void)
{
    return RTRandU32Ex(0, X86_EFL_STATUS_BITS) & X86_EFL_STATUS_BITS;
}


static uint8_t RandU8Shift(unsigned i, unsigned cTests, uint32_t *puTracker, unsigned cBits)
{
    uint8_t cShift = (uint8_t)RTRandU32Ex(0, 0xff);
    if ((cShift % 11) != 0)
    {
        if ((cShift % 3) == 0)
            cShift &= (cBits * 2) - 1;
        else
            cShift &= cBits - 1;
    }

    /* Make sure we've got the following counts: 0, 1, cBits, cBits + 1.
       Note! Missing shift count 1 will cause the 'shl reg, 1' tests to spin forever. */
    if (cShift == 0)
        *puTracker |= RT_BIT_32(0);
    else if (cShift == 1)
        *puTracker |= RT_BIT_32(1);
    else if (cShift == cBits)
        *puTracker |= RT_BIT_32(2);
    else if (cShift == cBits + 1)
        *puTracker |= RT_BIT_32(3);
    else if (*puTracker != 15 && i > cTests / 2)
    {
        if (!(*puTracker & RT_BIT_32(0)))
        {
            *puTracker |= RT_BIT_32(0);
            cShift = 0;
        }
        else if (!(*puTracker & RT_BIT_32(1)))
        {
            *puTracker |= RT_BIT_32(1);
            cShift = 1;
        }
        else if (!(*puTracker & RT_BIT_32(2)))
        {
            *puTracker |= RT_BIT_32(2);
            cShift = cBits;
        }
        else if (!(*puTracker & RT_BIT_32(3)))
        {
            *puTracker |= RT_BIT_32(2);
            cShift = cBits + 1;
        }
        else
            RT_BREAKPOINT();
    }

    return cShift;
}


static uint8_t RandU8(unsigned i, unsigned iOp, unsigned iOuter = 0, enum OPCODESX86 enmOp = OP_INVALID)
{
    RT_NOREF(iOuter, enmOp);
    if (i == 0)
        return 0;
    if (i == 1)
        return UINT8_MAX;
    if (i == 2)
        return iOp == 1 ? 0 : UINT8_MAX;
    return (uint8_t)RTRandU32Ex(0, UINT8_MAX);
}


static uint16_t RandU16(unsigned i, unsigned iOp, unsigned iOuter, enum OPCODESX86 enmOp)
{
    RT_NOREF(enmOp);
    Assert(iOuter <= 1);
    if (i == 0)
        return 0;
    if (i == 1)
        return UINT16_MAX;
    if (i == 2)
        return iOp == 1 ? 0 : UINT16_MAX;
    if (iOuter == 1 && iOp == 2)
        return (uint16_t)(int16_t)(int8_t)RandU8(i, iOp);
    if ((i % 3) == 0)
        return (uint16_t)RTRandU32Ex(0, UINT16_MAX >> RTRandU32Ex(1, 11));
    return (uint16_t)RTRandU32Ex(0, UINT16_MAX);
}


static uint32_t RandU32(unsigned i, unsigned iOp, unsigned iOuter, enum OPCODESX86 enmOp)
{
    RT_NOREF(enmOp);
    Assert(iOuter <= 1);
    if (i == 0)
        return 0;
    if (i == 1)
        return UINT32_MAX;
    if (i == 2)
        return iOp == 1 ? 0 : UINT32_MAX;
    if (i == 3)
    {
        /* Triggering OF with an 8-bit immediate operand is seriously difficult with pure
           randomness, so cheat. */
        switch (enmOp)
        {
            /* Both ops must have the same sign and the sign change. Pick positive to negative */
            case OP_ADD:
            case OP_ADC:
            {
                if (iOuter == 0)
                    return RTRandU32Ex(UINT32_C(0x40000000),  UINT32_C(0x7fffffff));
                if (iOp != 2)
                    return RTRandU32Ex(UINT32_C(0x7ffffff0),  UINT32_C(0x7fffffff));
                return RTRandU32Ex(0x10, 0x7f);
            }

            /* Pick positive left, negative right. */
            case OP_SUB:
            case OP_SBB:
            case OP_CMP:
            {
                if (iOuter == 0)
                    return iOp != 2
                         ? RTRandU32Ex(UINT32_C(0x40000000),  UINT32_C(0x7fffffff))
                         : RTRandU32Ex(UINT32_C(0x80000002),  UINT32_C(0xc0000000));
                if (iOp != 2)
                    return RTRandU32Ex(UINT32_C(0x7ffffff0),  UINT32_C(0x7fffffff));
                return RTRandU32Ex(UINT32_C(0xffffff81), UINT32_C(0xffffffef));
            }

            default:
                break;
        }
    }
    if (iOuter == 1 && iOp == 2)
        return (uint32_t)(int32_t)(int8_t)RandU8(i, iOp);

    if ((i % 5) == 0)
        return RTRandU32Ex(0, UINT32_MAX >> RTRandU32Ex(1, 23));
    return RTRandU32();
}


static uint64_t RandU64(unsigned i, unsigned iOp, unsigned iOuter, enum OPCODESX86 enmOp)
{
    RT_NOREF(enmOp);
    if (i == 0)
        return 0;
    if (i == 1)
        return UINT64_MAX;
    if (i == 2)
        return iOp == 1 ? 0 : UINT64_MAX;
    if (i == 3)
    {
        /* Triggering OF with an 8-bit immediate operand is seriously difficult with pure
           randomness, so cheat. */
        switch (enmOp)
        {
            /* Both ops must have the same sign and the sign change. Pick positive to negative */
            case OP_ADD:
            case OP_ADC:
            {
                if (iOuter == 0)
                    return RTRandU64Ex(UINT64_C(0x4000000000000000), UINT64_C(0x7fffffffffffffff));
                if (iOuter == 2) /* signed 32-bit immediate */
                    return iOp != 2
                         ? RTRandU64Ex(UINT64_C(0x7fffffffc0000000), UINT64_C(0x7fffffffffffffff))
                         : RTRandU64Ex(UINT64_C(0x0000000040000000), UINT64_C(0x000000007f000000));
                /* signed 8-bit immediate */
                return iOp != 2
                     ? RTRandU64Ex(UINT64_C(0x7ffffffffffffff0), UINT64_C(0x7fffffffffffffff))
                     : RTRandU64Ex(UINT64_C(0x0000000000000010), UINT64_C(0x000000000000007f));
            }

            /* Pick positive left, negative right. */
            case OP_SUB:
            case OP_SBB:
            case OP_CMP:
            {
                if (iOuter == 0)
                    return iOp != 2
                         ? RTRandU64Ex(UINT64_C(0x4000000000000000), UINT64_C(0x7fffffffffffffff))
                         : RTRandU64Ex(UINT64_C(0x8000000000000002), UINT64_C(0xc000000000000000));
                if (iOuter == 2) /* signed 32-bit immediate */
                    return iOp != 2
                         ? RTRandU64Ex(UINT64_C(0x7fffffffc0000002), UINT64_C(0x7fffffffffffffff))
                         : RTRandU64Ex(UINT64_C(0xffffffff80000002), UINT64_C(0xffffffffc0000000));
                /* signed 8-bit immediate */
                return iOp != 2
                     ? RTRandU64Ex(UINT64_C(0x7ffffffffffffff0), UINT64_C(0x7fffffffffffffff))
                     : RTRandU64Ex(UINT64_C(0xffffffffffffff81), UINT64_C(0xffffffffffffffef));
            }

            default:
                break;
        }
    }
    if (iOuter != 0 && iOp == 2)
    {
        Assert(iOuter <= 2);
        if (iOuter == 1)
            return (uint64_t)(int64_t)(int8_t)RTRandU32Ex(0, UINT8_MAX);
        if ((i % 2) != 0)
            return (uint64_t)(int32_t)RTRandU32();
        int32_t i32 = (int32_t)RTRandU32Ex(0, UINT32_MAX >> RTRandU32Ex(1, 23));
        if (RTRandU32Ex(0, 1) & 1)
            i32 = -i32;
        return (uint64_t)(int64_t)i32;
    }
    if ((i % 5) == 0)
        return RTRandU64Ex(0, UINT64_MAX >> RTRandU32Ex(1, 55));
    return RTRandU64();
}


DECL_FORCE_INLINE(uint32_t)
EnsureEflCoverage(unsigned iTest, unsigned cTests, unsigned cActiveEfls, uint32_t fActiveEfl,
                  uint32_t fSet, uint32_t fClear, uint32_t *pfMustBeClear)
{
    *pfMustBeClear = 0;
    unsigned cLeft = cTests - iTest;
    if (cLeft > cActiveEfls * 2)
        return 0;

    /* Find out which flag we're checking for now. */
    unsigned iBit = ASMBitFirstSetU32(fActiveEfl) - 1;
    while (cLeft >= 2)
    {
        cLeft -= 2;
        fActiveEfl &= ~RT_BIT_32(iBit);
        iBit = ASMBitFirstSetU32(fActiveEfl) - 1;
    }

    if (cLeft & 1)
    {
        if (!(fSet & RT_BIT_32(iBit)))
            return RT_BIT_32(iBit);
    }
    else if (!(fClear & RT_BIT_32(iBit)))
        *pfMustBeClear = RT_BIT_32(iBit);
    return 0;
}


static void FileHeader(PRTSTREAM pOut, const char *pszFilename, const char *pszIncludeBlocker)
{
    RTStrmPrintf(pOut,
                 "// ##### BEGINFILE \"%s\"\n"
                 "/* $" "Id$ */\n"
                 "/** @file\n"
                 " * BS3Kit - bs3-cpu-instr-2, %s - auto generated (do not edit).\n"
                 " */\n"
                 "\n"
                 "/*\n"
                 " * Copyright (C) 2024 Oracle and/or its affiliates.\n"
                 " *\n"
                 " * This file is part of VirtualBox base platform packages, as\n"
                 " * available from https://www.virtualbox.org.\n"
                 " *\n"
                 " * This program is free software; you can redistribute it and/or\n"
                 " * modify it under the terms of the GNU General Public License\n"
                 " * as published by the Free Software Foundation, in version 3 of the\n"
                 " * License.\n"
                 " *\n"
                 " * This program is distributed in the hope that it will be useful, but\n"
                 " * WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                 " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
                 " * General Public License for more details.\n"
                 " *\n"
                 " * You should have received a copy of the GNU General Public License\n"
                 " * along with this program; if not, see <https://www.gnu.org/licenses>.\n"
                 " *\n"
                 " * The contents of this file may alternatively be used under the terms\n"
                 " * of the Common Development and Distribution License Version 1.0\n"
                 " * (CDDL), a copy of it is provided in the \"COPYING.CDDL\" file included\n"
                 " * in the VirtualBox distribution, in which case the provisions of the\n"
                 " * CDDL are applicable instead of those of the GPL.\n"
                 " *\n"
                 " * You may elect to license modified versions of this file under the\n"
                 " * terms and conditions of either the GPL or the CDDL or both.\n"
                 " *\n"
                 " * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0\n"
                 " */\n"
                 "\n"
                 , pszFilename, pszFilename);
    if (!pszIncludeBlocker)
        RTStrmPrintf(pOut,
                     "#include <bs3kit.h>\n"
                     "#include \"bs3-cpu-instr-2.h\"\n");
    else
        RTStrmPrintf(pOut,
                     "#ifndef %s\n"
                     "#define %s\n"
                     "#ifndef RT_WITHOUT_PRAGMA_ONCE\n"
                     "# pragma once\n"
                     "#endif\n",
                     pszIncludeBlocker, pszIncludeBlocker);
}

int main(int argc, char **argv)
{
    RTR3InitExe(argc,  &argv, 0);

    /*
     * Constants.
     */
    enum { kEflBeaviour_Intel = 0, kEflBeaviour_Amd }
        const                   enmEflBehaviour = ASMIsAmdCpu() || ASMIsHygonCpu() ? kEflBeaviour_Amd : kEflBeaviour_Intel;
    static const char * const   s_apszEflBehaviourTabNm[] = { "intel_", "amd_" };

    /*
     * Parse arguments.
     */
    PRTSTREAM   pOut        = g_pStdOut;
    unsigned    cTestsU8    = 32;
    unsigned    cTestsU16   = 32;
    unsigned    cTestsU32   = 36;
    unsigned    cTestsU64   = 48;

    /** @todo  */
    if (argc != 1)
    {
        RTMsgSyntax("No arguments expected");
        return RTEXITCODE_SYNTAX;
    }


    /*
     * Generate the test data.
     */
    static struct
    {
        const char *pszName;
        enum OPCODESX86 enmOp;
        DECLCALLBACKMEMBER(uint32_t, pfnU8, ( uint8_t uSrc1,  uint8_t uSrc2, uint32_t fCarry,  uint8_t *puResult));
        DECLCALLBACKMEMBER(uint32_t, pfnU16,(uint16_t uSrc1, uint16_t uSrc2, uint32_t fCarry, uint16_t *puResult));
        DECLCALLBACKMEMBER(uint32_t, pfnU32,(uint32_t uSrc1, uint32_t uSrc2, uint32_t fCarry, uint32_t *puResult));
        DECLCALLBACKMEMBER(uint32_t, pfnU64,(uint64_t uSrc1, uint64_t uSrc2, uint32_t fCarry, uint64_t *puResult));
        uint8_t     cActiveEfls;
        uint16_t    fActiveEfls;
        bool        fCarryIn;
        bool        fImmVars;
    } const s_aBinary[] =
    {
        { "and",    OP_AND,  GenU8_and,  GenU16_and,  GenU32_and,  GenU64_and,   3, X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF, false, true  },
        { "or",     OP_OR,   GenU8_or,   GenU16_or,   GenU32_or,   GenU64_or,    3, X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF, false, true  },
        { "xor",    OP_XOR,  GenU8_xor,  GenU16_xor,  GenU32_xor,  GenU64_xor,   3, X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF, false, true  },
        { "test",   OP_TEST, GenU8_test, GenU16_test, GenU32_test, GenU64_test,  3, X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF, false, true  },

        { "add",    OP_ADD,  GenU8_add,  GenU16_add,  GenU32_add,  GenU64_add,   6, X86_EFL_STATUS_BITS,                  false, true  },
        { "adc",    OP_ADC,  GenU8_adc,  GenU16_adc,  GenU32_adc,  GenU64_adc,   6, X86_EFL_STATUS_BITS,                  true,  true  },
        { "sub",    OP_SUB,  GenU8_sub,  GenU16_sub,  GenU32_sub,  GenU64_sub,   6, X86_EFL_STATUS_BITS,                  false, true  },
        { "sbb",    OP_SBB,  GenU8_sbb,  GenU16_sbb,  GenU32_sbb,  GenU64_sbb,   6, X86_EFL_STATUS_BITS,                  true,  true  },
        { "cmp",    OP_CMP,  GenU8_cmp,  GenU16_cmp,  GenU32_cmp,  GenU64_cmp,   6, X86_EFL_STATUS_BITS,                  false, true  },

        { "bt",     OP_BT,   NULL,       GenU16_bt,   GenU32_bt,   GenU64_bt,    1, X86_EFL_CF,                           false, false },
        { "btc",    OP_BTC,  NULL,       GenU16_btc,  GenU32_btc,  GenU64_btc,   1, X86_EFL_CF,                           false, false },
        { "btr",    OP_BTR,  NULL,       GenU16_btr,  GenU32_btr,  GenU64_btr,   1, X86_EFL_CF,                           false, false },
        { "bts",    OP_BTS,  NULL,       GenU16_bts,  GenU32_bts,  GenU64_bts,   1, X86_EFL_CF,                           false, false },
    };

    static struct
    {
        const char *pszName;
        enum OPCODESX86 enmOp;
        DECLCALLBACKMEMBER(uint32_t, pfnU8, ( uint8_t uSrc1, uint8_t uSrc2, uint32_t fCarry,  uint8_t *puResult));
        DECLCALLBACKMEMBER(uint32_t, pfnU16,(uint16_t uSrc1, uint8_t uSrc2, uint32_t fCarry, uint16_t *puResult));
        DECLCALLBACKMEMBER(uint32_t, pfnU32,(uint32_t uSrc1, uint8_t uSrc2, uint32_t fCarry, uint32_t *puResult));
        DECLCALLBACKMEMBER(uint32_t, pfnU64,(uint64_t uSrc1, uint8_t uSrc2, uint32_t fCarry, uint64_t *puResult));
        DECLCALLBACKMEMBER(uint32_t, pfnU8Ib, ( uint8_t uSrc1, uint8_t uSrc2, uint32_t fCarry,  uint8_t *puResult));
        DECLCALLBACKMEMBER(uint32_t, pfnU16Ib,(uint16_t uSrc1, uint8_t uSrc2, uint32_t fCarry, uint16_t *puResult));
        DECLCALLBACKMEMBER(uint32_t, pfnU32Ib,(uint32_t uSrc1, uint8_t uSrc2, uint32_t fCarry, uint32_t *puResult));
        DECLCALLBACKMEMBER(uint32_t, pfnU64Ib,(uint64_t uSrc1, uint8_t uSrc2, uint32_t fCarry, uint64_t *puResult));
        uint8_t     cActiveEfls;
        uint16_t    fActiveEfls;
        bool        fCarryIn;
    } const s_aShifters[] =
    {
        { "shl", OP_SHL, GenU8_shl, GenU16_shl, GenU32_shl, GenU64_shl,   GenU8_shl_Ib, GenU16_shl_Ib, GenU32_shl_Ib, GenU64_shl_Ib,   6, X86_EFL_STATUS_BITS,     false  },
        { "shr", OP_SHR, GenU8_shr, GenU16_shr, GenU32_shr, GenU64_shr,   GenU8_shr_Ib, GenU16_shr_Ib, GenU32_shr_Ib, GenU64_shr_Ib,   6, X86_EFL_STATUS_BITS,     false  },
        { "sar", OP_SAR, GenU8_sar, GenU16_sar, GenU32_sar, GenU64_sar,   GenU8_sar_Ib, GenU16_sar_Ib, GenU32_sar_Ib, GenU64_sar_Ib,   6, X86_EFL_STATUS_BITS,     false  },
        { "rol", OP_ROL, GenU8_rol, GenU16_rol, GenU32_rol, GenU64_rol,   GenU8_rol_Ib, GenU16_rol_Ib, GenU32_rol_Ib, GenU64_rol_Ib,   2, X86_EFL_CF | X86_EFL_OF, false  },
        { "ror", OP_ROR, GenU8_ror, GenU16_ror, GenU32_ror, GenU64_ror,   GenU8_ror_Ib, GenU16_ror_Ib, GenU32_ror_Ib, GenU64_ror_Ib,   2, X86_EFL_CF | X86_EFL_OF, false  },
        { "rcl", OP_RCL, GenU8_rcl, GenU16_rcl, GenU32_rcl, GenU64_rcl,   GenU8_rcl_Ib, GenU16_rcl_Ib, GenU32_rcl_Ib, GenU64_rcl_Ib,   2, X86_EFL_CF | X86_EFL_OF, true   },
        { "rcr", OP_RCR, GenU8_rcr, GenU16_rcr, GenU32_rcr, GenU64_rcr,   GenU8_rcr_Ib, GenU16_rcr_Ib, GenU32_rcr_Ib, GenU64_rcr_Ib,   2, X86_EFL_CF | X86_EFL_OF, true   },
    };

    RTStrmPrintf(pOut, "\n"); /* filesplitter requires this. */

    /*
     * Header:
     */
    FileHeader(pOut, "bs3-cpu-instr-2-data.h", "VBOX_INCLUDED_SRC_bootsectors_bs3_cpu_instr_2_data_h");
#define DO_ONE_TYPE(a_Entry, a_pszExtraNm, a_szTypeBaseNm, a_cBits) do { \
                RTStrmPrintf(pOut, \
                             "\n" \
                             "extern const uint16_t g_cBs3CpuInstr2_%s_%sTestDataU" #a_cBits ";\n" \
                             "extern const " a_szTypeBaseNm #a_cBits " g_aBs3CpuInstr2_%s_%sTestDataU" #a_cBits "[];\n", \
                             a_Entry.pszName, a_pszExtraNm, a_Entry.pszName, a_pszExtraNm); \
            } while (0)
    for (unsigned iInstr = 0; iInstr < RT_ELEMENTS(s_aBinary); iInstr++)
    {
        if (s_aBinary[iInstr].pfnU8)
            DO_ONE_TYPE(s_aBinary[iInstr], "", "BS3CPUINSTR2BIN", 8);
        if (s_aBinary[iInstr].pfnU16)
            DO_ONE_TYPE(s_aBinary[iInstr], "", "BS3CPUINSTR2BIN", 16);
        if (s_aBinary[iInstr].pfnU32)
            DO_ONE_TYPE(s_aBinary[iInstr], "", "BS3CPUINSTR2BIN", 32);
        if (s_aBinary[iInstr].pfnU64)
            DO_ONE_TYPE(s_aBinary[iInstr], "", "BS3CPUINSTR2BIN", 64);
    }

    for (unsigned iInstr = 0; iInstr < RT_ELEMENTS(s_aShifters); iInstr++)
        for (unsigned iEflBehaviour = 0; iEflBehaviour < RT_ELEMENTS(s_apszEflBehaviourTabNm); iEflBehaviour++)
        {
            DO_ONE_TYPE(s_aShifters[iInstr], s_apszEflBehaviourTabNm[iEflBehaviour], "BS3CPUINSTR2SHIFT", 8);
            DO_ONE_TYPE(s_aShifters[iInstr], s_apszEflBehaviourTabNm[iEflBehaviour], "BS3CPUINSTR2SHIFT", 16);
            DO_ONE_TYPE(s_aShifters[iInstr], s_apszEflBehaviourTabNm[iEflBehaviour], "BS3CPUINSTR2SHIFT", 32);
            DO_ONE_TYPE(s_aShifters[iInstr], s_apszEflBehaviourTabNm[iEflBehaviour], "BS3CPUINSTR2SHIFT", 64);
        }
#undef DO_ONE_TYPE

    RTStrmPrintf(pOut,
                 "\n"
                 "#endif /* !VBOX_INCLUDED_SRC_bootsectors_bs3_cpu_instr_2_data_h */\n"
                 "\n// ##### ENDFILE\n");

#define DO_ONE_TYPE(a_Entry, a_pszExtraNm, a_szTypeBaseNm, a_ValueType, a_cBits, a_szFmt, a_pfnMember, a_cTests, a_fImmVars, a_fShift, a_pfnMemberIb) do { \
                unsigned const cOuterLoops =  1 + a_fImmVars * (a_cBits == 64 ? 2 : a_cBits != 8 ? 1 : 0); \
                unsigned const cTestFactor = !a_Entry.fCarryIn ? 1 : 2; \
                unsigned       cFunnyImm   = 0; \
                RTStrmPrintf(pOut, \
                             "\n" \
                             "const uint16_t g_cBs3CpuInstr2_%s_%sTestDataU" #a_cBits " = %u;\n" \
                             "const " a_szTypeBaseNm #a_cBits " g_aBs3CpuInstr2_%s_%sTestDataU" #a_cBits "[%u] =\n" \
                             "{\n", \
                             a_Entry.pszName, a_pszExtraNm, a_cTests * cTestFactor * cOuterLoops, \
                             a_Entry.pszName, a_pszExtraNm, a_cTests * cTestFactor * cOuterLoops); \
                for (unsigned iOuter = 0; iOuter < cOuterLoops; iOuter++) \
                { \
                    if (iOuter != 0) \
                        RTStrmPrintf(pOut, "    /* r/m" #a_cBits", imm%u: */\n", iOuter == 1 ? 8 : 32); \
                    uint32_t fSet   = 0; \
                    uint32_t fClear = 0; \
                    uint32_t uShiftTracker = 0; \
                    for (unsigned iTest = 0; iTest < a_cTests; iTest++) \
                    { \
                        uint32_t fMustBeClear = 0; \
                        uint32_t fMustBeSet   = EnsureEflCoverage(iTest, a_cTests, a_Entry.cActiveEfls, \
                                                                  a_Entry.fActiveEfls, fSet, fClear, &fMustBeClear); \
                        for (unsigned iTry = 0;; iTry++) \
                        { \
                            a_ValueType const uSrc1     = RandU##a_cBits(iTest + iTry, 1, iOuter, a_Entry.enmOp); \
                            a_ValueType const uSrc2     = !(a_fShift) ? RandU##a_cBits(iTest + iTry, 2, iOuter, a_Entry.enmOp) \
                                                        : RandU8Shift(iTest + iTry, a_cTests, &uShiftTracker, a_cBits); \
                            a_ValueType       uResult   = 0; \
                            uint32_t const    fEflIn    = !(a_fShift) ? 0 /*fCarry*/ : a_Entry.fCarryIn \
                                                        ? RandEflStatus() & ~(uint32_t)X86_EFL_CF : RandEflStatus(); \
                            uint32_t          fEflOut   = a_Entry.a_pfnMember(uSrc1, uSrc2, fEflIn, &uResult) \
                                                        & X86_EFL_STATUS_BITS; \
                            if ((fEflOut & fMustBeClear) || (~fEflOut & fMustBeSet)) \
                            { \
                                if (iTry < _1M) \
                                    continue; \
                                RTMsgWarning("Giving up: " #a_cBits "-bit %s - fMustBeClear=%#x fMustBeSet=%#x iTest=%#x iTry=%#x iOuter=%d\n", \
                                             a_Entry.pszName, fMustBeClear, fMustBeSet, iTest, iTry, iOuter); \
                            } \
                            fSet   |= fEflOut; \
                            fClear |= ~fEflOut; \
                            if (!(a_fShift)) \
                                RTStrmPrintf(pOut,  "    { " a_szFmt ", " a_szFmt ", " a_szFmt ", %#05RX16 },\n", \
                                             uSrc1, uSrc2, uResult, fEflOut); \
                            else \
                            { \
                                /* Seems that 'rol reg,Ib' & 'ror reg,Ib' produces different OF results on intel. \
                                   Observed on 8700B, 9980HK, 10980xe, 1260p, ++. */ \
                                a_ValueType    uResultIb = 0; \
                                uint32_t const fEflOutIb = a_Entry.a_pfnMemberIb(uSrc1, uSrc2, fEflIn, &uResultIb) \
                                                         & X86_EFL_STATUS_BITS; \
                                if ((fEflOutIb & X86_EFL_OF) != (fEflOut & X86_EFL_OF)) cFunnyImm += 1; \
                                if (uResultIb != uResult) RT_BREAKPOINT(); \
                                if ((fEflOutIb ^ fEflOut) & (X86_EFL_STATUS_BITS & ~X86_EFL_OF)) RT_BREAKPOINT(); \
                                if (fEflOutIb & X86_EFL_OF) fEflOut |= RT_BIT_32(BS3CPUINSTR2SHIFT_EFL_IB_OVERFLOW_OUT_BIT); \
                                RTStrmPrintf(pOut,  "    { " a_szFmt ", %#04RX8, %#05RX16, " a_szFmt ", %#05RX16 },%s\n", \
                                             uSrc1, (uint8_t)uSrc2, (uint16_t)fEflIn, uResult, (uint16_t)fEflOut, \
                                             (fEflOut ^ fEflOutIb) & X86_EFL_OF ? " /* OF/Ib */" : ""); \
                            } \
                            if (a_Entry.fCarryIn) \
                            { \
                                uResult = 0; \
                                fEflOut = a_Entry.a_pfnMember(uSrc1, uSrc2, fEflIn | X86_EFL_CF, &uResult) & X86_EFL_STATUS_BITS; \
                                fSet   |= fEflOut; \
                                fClear |= ~fEflOut; \
                                if (!(a_fShift)) \
                                    RTStrmPrintf(pOut,  "    { " a_szFmt ", " a_szFmt ", " a_szFmt ", %#05RX16 },\n", uSrc1, uSrc2, \
                                                 uResult, (uint16_t)(fEflOut | RT_BIT_32(BS3CPUINSTR2BIN_EFL_CARRY_IN_BIT))); \
                                else \
                                { \
                                    /* Seems that 'rol reg,Ib' & 'ror reg,Ib' produces different OF results on intel. \
                                       Observed on 8700B, 9980HK, 10980xe, 1260p, ++. */ \
                                    a_ValueType    uResultIb = 0; \
                                    uint32_t const fEflOutIb = a_Entry.a_pfnMemberIb(uSrc1, uSrc2, fEflIn | X86_EFL_CF, &uResultIb) \
                                                             & X86_EFL_STATUS_BITS; \
                                    if ((fEflOutIb & X86_EFL_OF) != (fEflOut & X86_EFL_OF)) cFunnyImm += 1; \
                                    if (uResultIb != uResult) RT_BREAKPOINT(); \
                                    if ((fEflOutIb ^ fEflOut) & (X86_EFL_STATUS_BITS & ~X86_EFL_OF)) RT_BREAKPOINT(); \
                                    if (fEflOutIb & X86_EFL_OF) fEflOut |= RT_BIT_32(BS3CPUINSTR2SHIFT_EFL_IB_OVERFLOW_OUT_BIT); \
                                    RTStrmPrintf(pOut,  "    { " a_szFmt ", %#04RX8, %#05RX16, " a_szFmt ", %#05RX16 },%s\n", \
                                                 uSrc1, (uint8_t)uSrc2, (uint16_t)(fEflIn | X86_EFL_CF), uResult, \
                                                 (uint16_t)fEflOut, (fEflOut ^ fEflOutIb) & X86_EFL_OF ? " /* OF/Ib */" : ""); \
                                } \
                            } \
                            break; \
                        } \
                    } \
                } \
                if (cFunnyImm == 0) \
                    RTStrmPrintf(pOut, "};\n"); \
                else \
                    RTStrmPrintf(pOut, "}; /* Note! " #a_cBits "-bit %s reg,imm8 results differed %u times from the other form */\n", \
                                 a_Entry.pszName, cFunnyImm); \
            } while (0)

    /*
     * Source: 8, 16 & 32 bit data.
     */
    FileHeader(pOut, "bs3-cpu-instr-2-data16.c16", NULL);
    for (unsigned iInstr = 0; iInstr < RT_ELEMENTS(s_aBinary); iInstr++)
    {
        if (s_aBinary[iInstr].pfnU8)
            DO_ONE_TYPE(s_aBinary[iInstr], "", "BS3CPUINSTR2BIN", uint8_t,  8,   "%#04RX8",  pfnU8,  cTestsU8,  s_aBinary[iInstr].fImmVars, 0, pfnU8);
        if (s_aBinary[iInstr].pfnU16)
            DO_ONE_TYPE(s_aBinary[iInstr], "", "BS3CPUINSTR2BIN", uint16_t, 16, "%#06RX16",  pfnU16, cTestsU16, s_aBinary[iInstr].fImmVars, 0, pfnU16);
        if (s_aBinary[iInstr].pfnU32)
            DO_ONE_TYPE(s_aBinary[iInstr], "", "BS3CPUINSTR2BIN", uint32_t, 32, "%#010RX32", pfnU32, cTestsU32, s_aBinary[iInstr].fImmVars, 0, pfnU32);
    }
    RTStrmPrintf(pOut, "\n// ##### ENDFILE\n");

    /*
     * Source: 64 bit data (goes in different data segment).
     */
    FileHeader(pOut, "bs3-cpu-instr-2-data64.c64", NULL);
    for (unsigned iInstr = 0; iInstr < RT_ELEMENTS(s_aBinary); iInstr++)
        if (s_aBinary[iInstr].pfnU64)
            DO_ONE_TYPE(s_aBinary[iInstr], "", "BS3CPUINSTR2BIN", uint64_t, 64, "%#018RX64", pfnU64, cTestsU64, s_aBinary[iInstr].fImmVars, 0, pfnU64);
    RTStrmPrintf(pOut, "\n// ##### ENDFILE\n");


    /*
     * Source: 8, 16 & 32 bit data for the host CPU.
     */
    const char * const pszEflTabNm = s_apszEflBehaviourTabNm[enmEflBehaviour];
    FileHeader(pOut, enmEflBehaviour == kEflBeaviour_Amd ? "bs3-cpu-instr-2-data16-amd.c16" : "bs3-cpu-instr-2-data16-intel.c16", NULL);
    for (unsigned iInstr = 0; iInstr < RT_ELEMENTS(s_aShifters); iInstr++)
    {
        DO_ONE_TYPE(s_aShifters[iInstr], pszEflTabNm, "BS3CPUINSTR2SHIFT", uint8_t,  8,   "%#04RX8",  pfnU8,  cTestsU8,  0, 1, pfnU8Ib);
        DO_ONE_TYPE(s_aShifters[iInstr], pszEflTabNm, "BS3CPUINSTR2SHIFT", uint16_t, 16, "%#06RX16",  pfnU16, cTestsU16, 0, 1, pfnU16Ib);
        DO_ONE_TYPE(s_aShifters[iInstr], pszEflTabNm, "BS3CPUINSTR2SHIFT", uint32_t, 32, "%#010RX32", pfnU32, cTestsU32, 0, 1, pfnU32Ib);
    }
    RTStrmPrintf(pOut, "\n// ##### ENDFILE\n");

    /*
     * Source: 64 bit data for the host CPU (goes in different data segment).
     */
    FileHeader(pOut, enmEflBehaviour == kEflBeaviour_Amd ? "bs3-cpu-instr-2-data64-amd.c64" : "bs3-cpu-instr-2-data64-intel.c64", NULL);
    for (unsigned iInstr = 0; iInstr < RT_ELEMENTS(s_aShifters); iInstr++)
        DO_ONE_TYPE(s_aShifters[iInstr], pszEflTabNm, "BS3CPUINSTR2SHIFT", uint64_t, 64, "%#018RX64", pfnU64, cTestsU64, 0, 1, pfnU64Ib);
    RTStrmPrintf(pOut, "\n// ##### ENDFILE\n");

#undef DO_ONE_TYPE

    return 0;
}

