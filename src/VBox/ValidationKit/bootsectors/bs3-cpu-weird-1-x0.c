/* $Id: bs3-cpu-weird-1-x0.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - bs3-cpu-weird-2, C test driver code (16-bit).
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
#define BS3_USE_X0_TEXT_SEG
#include <bs3kit.h>
#include <bs3-cmn-memory.h>
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#undef  CHECK_MEMBER
#define CHECK_MEMBER(a_szName, a_szFmt, a_Actual, a_Expected) \
    do \
    { \
        if ((a_Actual) == (a_Expected)) { /* likely */ } \
        else bs3CpuWeird1_FailedF(a_szName "=" a_szFmt " expected " a_szFmt, (a_Actual), (a_Expected)); \
    } while (0)

#define BS3_CPU_WEIRD_1_EXTERN_ASM_FN_VARS(a_Name, a_Label) \
    extern FNBS3FAR a_Name##_c16, a_Name##_##a_Label##_c16; \
    extern FNBS3FAR a_Name##_c32, a_Name##_##a_Label##_c32; \
    extern FNBS3FAR a_Name##_c64, a_Name##_##a_Label##_c64

#define PROTO_ALL(a_Template) \
    extern FNBS3FAR a_Template ## _c16, \
                    a_Template ## _c32, \
                    a_Template ## _c64

#define PROTO_ALL_WITH_END(a_Template) \
    extern FNBS3FAR a_Template ## _c16, a_Template ## _c16_EndProc, \
                    a_Template ## _c32, a_Template ## _c32_EndProc, \
                    a_Template ## _c64, a_Template ## _c64_EndProc


/*********************************************************************************************************************************
*   External Symbols                                                                                                             *
*********************************************************************************************************************************/
BS3_CPU_WEIRD_1_EXTERN_ASM_FN_VARS(bs3CpuWeird1_InhibitedMovSsInt80, int80);
BS3_CPU_WEIRD_1_EXTERN_ASM_FN_VARS(bs3CpuWeird1_InhibitedPopSsInt80, int80);

BS3_CPU_WEIRD_1_EXTERN_ASM_FN_VARS(bs3CpuWeird1_InhibitedMovSsInt3, int3);
BS3_CPU_WEIRD_1_EXTERN_ASM_FN_VARS(bs3CpuWeird1_InhibitedPopSsInt3, int3);

BS3_CPU_WEIRD_1_EXTERN_ASM_FN_VARS(bs3CpuWeird1_InhibitedMovSsBp, int3);
BS3_CPU_WEIRD_1_EXTERN_ASM_FN_VARS(bs3CpuWeird1_InhibitedPopSsBp, int3);

BS3_CPU_WEIRD_1_EXTERN_ASM_FN_VARS(bs3CpuWeird1_InhibitedMovSsSyscall, syscall);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char BS3_FAR  *g_pszTestMode = (const char *)1;
static BS3CPUVENDOR         g_enmCpuVendor = BS3CPUVENDOR_INTEL;
static bool                 g_fVME = false;
//static uint8_t              g_bTestMode = 1;
//static bool                 g_f16BitSys = 1;



/**
 * Sets globals according to the mode.
 *
 * @param   bTestMode   The test mode.
 */
static void bs3CpuWeird1_SetGlobals(uint8_t bTestMode)
{
//    g_bTestMode     = bTestMode;
    g_pszTestMode   = Bs3GetModeName(bTestMode);
//    g_f16BitSys     = BS3_MODE_IS_16BIT_SYS(bTestMode);
    g_usBs3TestStep = 0;
    g_enmCpuVendor  = Bs3GetCpuVendor();
    g_fVME = (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80486
          && (Bs3RegGetCr4() & X86_CR4_VME);
}


/**
 * Wrapper around Bs3TestFailedF that prefixes the error with g_usBs3TestStep
 * and g_pszTestMode.
 */
static void bs3CpuWeird1_FailedF(const char *pszFormat, ...)
{
    va_list va;

    char szTmp[168];
    va_start(va, pszFormat);
    Bs3StrPrintfV(szTmp, sizeof(szTmp), pszFormat, va);
    va_end(va);

    Bs3TestFailedF("%u - %s: %s", g_usBs3TestStep, g_pszTestMode, szTmp);
}


/**
 * Compares interrupt stuff.
 */
static void bs3CpuWeird1_CompareDbgInhibitRingXfer(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pStartCtx, uint8_t bXcpt,
                                                   int8_t cbPcAdjust, int8_t cbSpAdjust, uint32_t uDr6Expected,
                                                   uint8_t cbIretFrame, uint64_t uHandlerRsp)
{
    uint32_t uDr6 = (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386 ? Bs3RegGetDr6() : X86_DR6_INIT_VAL;
    uint16_t const cErrorsBefore = Bs3TestSubErrorCount();
    CHECK_MEMBER("bXcpt",       "%#04x",    pTrapCtx->bXcpt,        bXcpt);
    CHECK_MEMBER("bErrCd",      "%#06RX64", pTrapCtx->uErrCd,       0);
    CHECK_MEMBER("cbIretFrame", "%#04x",    pTrapCtx->cbIretFrame,  cbIretFrame);
    CHECK_MEMBER("uHandlerRsp",  "%#06RX64", pTrapCtx->uHandlerRsp,  uHandlerRsp);
    if (uDr6 != uDr6Expected)
        bs3CpuWeird1_FailedF("dr6=%#010RX32 expected %#010RX32", uDr6, uDr6Expected);
    Bs3TestCheckRegCtxEx(&pTrapCtx->Ctx, pStartCtx, cbPcAdjust, cbSpAdjust, 0 /*fExtraEfl*/, g_pszTestMode, g_usBs3TestStep);
    if (Bs3TestSubErrorCount() != cErrorsBefore)
    {
        Bs3TrapPrintFrame(pTrapCtx);
        Bs3TestPrintf("DR6=%#RX32; Handler: CS=%04RX16 SS:ESP=%04RX16:%08RX64 EFL=%RX64 cbIret=%#x\n",
                      uDr6, pTrapCtx->uHandlerCs, pTrapCtx->uHandlerSs, pTrapCtx->uHandlerRsp,
                      pTrapCtx->fHandlerRfl, pTrapCtx->cbIretFrame);
#if 0
        Bs3TestPrintf("Halting in CompareIntCtx: bXcpt=%#x\n", bXcpt);
        ASMHalt();
#endif
    }
}

static uint64_t bs3CpuWeird1_GetTrapHandlerEIP(uint8_t bXcpt, uint8_t bMode, bool fV86)
{
    if (   BS3_MODE_IS_RM_SYS(bMode)
        || (fV86 && BS3_MODE_IS_V86(bMode)))
    {
        PRTFAR16 paIvt = (PRTFAR16)Bs3XptrFlatToCurrent(0);
        return paIvt[bXcpt].off;
    }
    if (BS3_MODE_IS_16BIT_SYS(bMode))
        return Bs3Idt16[bXcpt].Gate.u16OffsetLow;
    if (BS3_MODE_IS_32BIT_SYS(bMode))
        return RT_MAKE_U32(Bs3Idt32[bXcpt].Gate.u16OffsetLow, Bs3Idt32[bXcpt].Gate.u16OffsetHigh);
    return RT_MAKE_U64(RT_MAKE_U32(Bs3Idt64[bXcpt].Gate.u16OffsetLow, Bs3Idt32[bXcpt].Gate.u16OffsetHigh),
                       Bs3Idt64[bXcpt].Gate.u32OffsetTop);
}


static void bs3RegCtxScramble(PBS3REGCTX pCtx, uint8_t bTestMode)
{
    if (BS3_MODE_IS_64BIT_SYS(bTestMode))
    {
        pCtx->r8.au32[0]  ^= UINT32_C(0x2f460cb9);
        pCtx->r8.au32[1]  ^= UINT32_C(0x2dc346ff);
        pCtx->r9.au32[0]  ^= UINT32_C(0x9c50d12e);
        pCtx->r9.au32[1]  ^= UINT32_C(0x60be8859);
        pCtx->r10.au32[0] ^= UINT32_C(0xa45fbe73);
        pCtx->r10.au32[1] ^= UINT32_C(0x094140bf);
        pCtx->r11.au32[0] ^= UINT32_C(0x8200148b);
        pCtx->r11.au32[1] ^= UINT32_C(0x95dfc457);
        pCtx->r12.au32[0] ^= UINT32_C(0xabc885f6);
        pCtx->r12.au32[1] ^= UINT32_C(0xb9af126a);
        pCtx->r13.au32[0] ^= UINT32_C(0xa2c4435c);
        pCtx->r13.au32[1] ^= UINT32_C(0x1692b52e);
        pCtx->r14.au32[0] ^= UINT32_C(0x85a56477);
        pCtx->r14.au32[1] ^= UINT32_C(0x31a44a04);
        pCtx->r15.au32[0] ^= UINT32_C(0x8d5b3072);
        pCtx->r15.au32[1] ^= UINT32_C(0xc2ffce37);
    }
}


typedef enum {
    DbgInhibitRingXferType_SoftInt,
    DbgInhibitRingXferType_Syscall
} DBGINHIBITRINGXFERTYPE;

static int bs3CpuWeird1_DbgInhibitRingXfer_Worker(uint8_t bTestMode, uint8_t bIntGate, uint8_t cbRingInstr, int8_t cbSpAdjust,
                                                  FPFNBS3FAR pfnTestCode, FPFNBS3FAR pfnTestLabel, DBGINHIBITRINGXFERTYPE enmType)
{
    BS3REGCTX               Ctx;
    BS3TRAPFRAME            TrapCtx;
    BS3TRAPFRAME            TrapExpectXfer;     /* Expected registers after transfer (no #DB). */
    BS3TRAPFRAME            TrapExpectXferDb;   /* Expected registers after transfer followed by some #DB. */
    uint8_t                 bSavedDpl;
    uint8_t const           offTestLabel    = BS3_FP_OFF(pfnTestLabel) - BS3_FP_OFF(pfnTestCode);
    uint8_t const           cbIretFrameSame = BS3_MODE_IS_16BIT_SYS(bTestMode) ? 6
                                            : BS3_MODE_IS_32BIT_SYS(bTestMode) ? 12 : 40;
    uint8_t const           cbIretFrameRing = BS3_MODE_IS_16BIT_SYS(bTestMode) ? 10
                                            : BS3_MODE_IS_32BIT_SYS(bTestMode) ? 20 : 40;
    uint8_t const           cbSpAdjSame     = BS3_MODE_IS_64BIT_SYS(bTestMode) ? 48 : cbIretFrameSame;
    bool const              fAlwaysUd       = enmType == DbgInhibitRingXferType_Syscall && bTestMode != BS3_MODE_LM64;
    uint8_t                 bVmeMethod      = 0;
    uint8_t                 cbIretFrameDb;      /* #DB before xfer */
    uint64_t                uHandlerRspDb;      /* #DB before xfer */
    BS3_XPTR_AUTO(uint32_t, StackXptr);

    if (fAlwaysUd)
        bIntGate = X86_XCPT_UD;

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));
    Bs3MemZero(&TrapExpectXfer, sizeof(TrapExpectXfer));
    Bs3MemZero(&TrapExpectXferDb, sizeof(TrapExpectXferDb));

    /*
     * Make INT xx accessible from DPL 3 and create a ring-3 context that we can work with.
     */
    bSavedDpl = Bs3TrapSetDpl(bIntGate, 3);

    Bs3RegCtxSaveEx(&Ctx, bTestMode, 1024);
    bs3RegCtxScramble(&Ctx, bTestMode);
    Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, pfnTestCode);
    if (BS3_MODE_IS_16BIT_SYS(bTestMode))
        g_uBs3TrapEipHint = Ctx.rip.u32;
    Ctx.rflags.u32 &= ~X86_EFL_RF;

    /* Raw-mode enablers. */
    Ctx.rflags.u32 |= X86_EFL_IF;
    if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80486)
        Ctx.cr0.u32 |= X86_CR0_WP;

    /* We put the SS value on the stack so we can easily set breakpoints there. */
    Ctx.rsp.u32 -= 8;
    BS3_XPTR_SET_FLAT(uint32_t, StackXptr, Ctx.rsp.u32); /* ASSUMES SS.BASE == 0!! */

    /* ring-3 */
    if (!BS3_MODE_IS_RM_OR_V86(bTestMode))
        Bs3RegCtxConvertToRingX(&Ctx, 3);

    /* V8086: Set IOPL to 3. */
    if (BS3_MODE_IS_V86(bTestMode) && enmType != DbgInhibitRingXferType_Syscall)
    {
        Ctx.rflags.u32 |= X86_EFL_IOPL;
        if (g_fVME)
        {
            Bs3RegSetTr(BS3_SEL_TSS32_IRB);
#if 0
            /* SDMv3b, 20.3.3 method 5: */
            ASMBitClear(&Bs3SharedIntRedirBm, bIntGate);
            bVmeMethod = 5;
#else
            /* SDMv3b, 20.3.3 method 4 (similar to non-VME): */
            ASMBitSet(&Bs3SharedIntRedirBm, bIntGate);
            bVmeMethod = 4;
        }
#endif
    }

    /* Make BP = SP since 16-bit can't use SP for addressing. */
    Ctx.rbp = Ctx.rsp;

    /*
     * Test #0: Test run.  Calc expected delayed #DB from it.
     */
    if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386)
    {
        Bs3RegSetDr7(0);
        Bs3RegSetDr6(X86_DR6_INIT_VAL);
    }
    *BS3_XPTR_GET(uint32_t, StackXptr) = Ctx.ss;
    Bs3TrapSetJmpAndRestore(&Ctx, &TrapExpectXfer);
    if (TrapExpectXfer.bXcpt != bIntGate)
    {
        Bs3TestFailedF("%u: bXcpt is %#x, expected %#x!\n", g_usBs3TestStep, TrapExpectXfer.bXcpt, bIntGate);
        Bs3TrapPrintFrame(&TrapExpectXfer);
        return 1;
    }
    Bs3MemCpy(&TrapExpectXferDb, &TrapExpectXfer, sizeof(TrapExpectXferDb));

    if (!fAlwaysUd)
    {
        TrapExpectXferDb.Ctx.bCpl         = 0;
        TrapExpectXferDb.Ctx.cs           = TrapExpectXfer.uHandlerCs;
        TrapExpectXferDb.Ctx.ss           = TrapExpectXfer.uHandlerSs;
        TrapExpectXferDb.Ctx.rsp.u64      = TrapExpectXfer.uHandlerRsp;
        TrapExpectXferDb.Ctx.rflags.u64   = TrapExpectXfer.fHandlerRfl;

        if (enmType != DbgInhibitRingXferType_Syscall)
        {
            TrapExpectXferDb.cbIretFrame  = TrapExpectXfer.cbIretFrame + cbIretFrameSame;
            TrapExpectXferDb.uHandlerRsp  = TrapExpectXfer.uHandlerRsp - cbSpAdjSame;
        }
        else
        {
            TrapExpectXfer.cbIretFrame    = 0xff;
            TrapExpectXferDb.cbIretFrame  = cbIretFrameSame;
            TrapExpectXfer.uHandlerRsp    = Ctx.rsp.u - cbSpAdjust;
            TrapExpectXferDb.uHandlerRsp  = (TrapExpectXfer.uHandlerRsp & ~(uint64_t)15) - cbIretFrameSame;
        }
        if (BS3_MODE_IS_V86(bTestMode))
        {
            if (bVmeMethod >= 5)
            {
                TrapExpectXferDb.Ctx.rflags.u32 |= X86_EFL_VM;
                TrapExpectXferDb.Ctx.bCpl = 3;
                TrapExpectXferDb.Ctx.rip.u64 = bs3CpuWeird1_GetTrapHandlerEIP(bIntGate, bTestMode, true);
                TrapExpectXferDb.cbIretFrame = 36;
                if (BS3_MODE_IS_16BIT_SYS(bTestMode))
                    TrapExpectXferDb.uHandlerRsp = Bs3Tss16.sp0  - TrapExpectXferDb.cbIretFrame;
                else
                    TrapExpectXferDb.uHandlerRsp = Bs3Tss32.esp0 - TrapExpectXferDb.cbIretFrame;
            }
            else
            {
                TrapExpectXferDb.Ctx.ds   = 0;
                TrapExpectXferDb.Ctx.es   = 0;
                TrapExpectXferDb.Ctx.fs   = 0;
                TrapExpectXferDb.Ctx.gs   = 0;
            }
        }
    }

    if (enmType != DbgInhibitRingXferType_Syscall)
    {
        cbIretFrameDb = TrapExpectXfer.cbIretFrame;
        uHandlerRspDb = TrapExpectXfer.uHandlerRsp;
    }
    else
    {
        cbIretFrameDb = cbIretFrameRing;
        uHandlerRspDb = BS3_ADDR_STACK_R0 - cbIretFrameRing;
    }


    /*
     * Test #1: Single stepping ring-3.  Ignored except for V8086 w/ VME.
     */
    g_usBs3TestStep++;
    if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386)
    {
        Bs3RegSetDr7(0);
        Bs3RegSetDr6(X86_DR6_INIT_VAL);
    }
    *BS3_XPTR_GET(uint32_t, StackXptr) = Ctx.ss;
//    Ctx.rflags.u32 |= X86_EFL_TF;

    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
    if (   !BS3_MODE_IS_V86(bTestMode)
             || bVmeMethod < 5)
        bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXfer.Ctx, bIntGate, 0, 0, X86_DR6_INIT_VAL,
                                               TrapExpectXfer.cbIretFrame, TrapExpectXfer.uHandlerRsp);
    else
    {
        TrapExpectXferDb.Ctx.rflags.u32 |= X86_EFL_TF;
        bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXferDb.Ctx, X86_XCPT_DB, offTestLabel, -2,
                                               X86_DR6_INIT_VAL | X86_DR6_BS,
                                               TrapExpectXferDb.cbIretFrame, TrapExpectXferDb.uHandlerRsp);
        TrapExpectXferDb.Ctx.rflags.u32 &= ~X86_EFL_TF;
    }

    Ctx.rflags.u32 &= ~X86_EFL_TF;
    if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386)
    {
        uint32_t uDr6Expect;

        /*
         * Test #2: Execution breakpoint on ring transition instruction.
         *          This hits on AMD-V (threadripper) but not on VT-x (skylake).
         */
        g_usBs3TestStep++;
        Bs3RegSetDr0(Bs3SelRealModeCodeToFlat(pfnTestLabel));
        Bs3RegSetDr7(X86_DR7_L0 | X86_DR7_G0 | X86_DR7_RW(0, X86_DR7_RW_EO) | X86_DR7_LEN(0, X86_DR7_LEN_BYTE));
        Bs3RegSetDr6(X86_DR6_INIT_VAL);
        *BS3_XPTR_GET(uint32_t, StackXptr) = Ctx.ss;

        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        Bs3RegSetDr7(0);
        if (g_enmCpuVendor == BS3CPUVENDOR_AMD || g_enmCpuVendor == BS3CPUVENDOR_HYGON)
            bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &Ctx, X86_XCPT_DB, offTestLabel, cbSpAdjust,
                                                   X86_DR6_INIT_VAL | X86_DR6_B0, TrapExpectXfer.cbIretFrame, TrapExpectXfer.uHandlerRsp);
        else
            bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXfer.Ctx, bIntGate, 0, 0, X86_DR6_INIT_VAL,
                                                   TrapExpectXfer.cbIretFrame, TrapExpectXfer.uHandlerRsp);

        /*
         * Test #3: Same as above, but with the LE and GE flags set.
         */
        g_usBs3TestStep++;
        Bs3RegSetDr0(Bs3SelRealModeCodeToFlat(pfnTestLabel));
        Bs3RegSetDr7(X86_DR7_L0 | X86_DR7_G0 | X86_DR7_RW(0, X86_DR7_RW_EO) | X86_DR7_LEN(0, X86_DR7_LEN_BYTE) | X86_DR7_LE | X86_DR7_GE);
        Bs3RegSetDr6(X86_DR6_INIT_VAL);
        *BS3_XPTR_GET(uint32_t, StackXptr) = Ctx.ss;

        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        if (g_enmCpuVendor == BS3CPUVENDOR_AMD || g_enmCpuVendor ==  BS3CPUVENDOR_HYGON)
            bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &Ctx, X86_XCPT_DB, offTestLabel, cbSpAdjust,
                                                   X86_DR6_INIT_VAL | X86_DR6_B0, TrapExpectXfer.cbIretFrame, TrapExpectXfer.uHandlerRsp);
        else
            bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXfer.Ctx, bIntGate, 0, 0, X86_DR6_INIT_VAL,
                                                   TrapExpectXfer.cbIretFrame, TrapExpectXfer.uHandlerRsp);

        /*
         * Test #4: Execution breakpoint on pop ss / mov ss.  Hits.
         *
         * Note! In real mode AMD-V updates the stack pointer, or something else is busted. Totally weird!
         *
         *       Update: see Test #6 update.
         */
        g_usBs3TestStep++;
        Bs3RegSetDr0(Bs3SelRealModeCodeToFlat(pfnTestCode));
        Bs3RegSetDr7(X86_DR7_L0 | X86_DR7_G0 | X86_DR7_RW(0, X86_DR7_RW_EO) | X86_DR7_LEN(0, X86_DR7_LEN_BYTE));
        Bs3RegSetDr6(X86_DR6_INIT_VAL);
        *BS3_XPTR_GET(uint32_t, StackXptr) = Ctx.ss;

        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &Ctx, X86_XCPT_DB, 0, 0, X86_DR6_INIT_VAL | X86_DR6_B0, cbIretFrameDb,
                                               uHandlerRspDb - (BS3_MODE_IS_RM_SYS(bTestMode) ? cbSpAdjust : 0) );

        /*
         * Test #5: Same as above, but with the LE and GE flags set.
         */
        g_usBs3TestStep++;
        Bs3RegSetDr0(Bs3SelRealModeCodeToFlat(pfnTestCode));
        Bs3RegSetDr7(X86_DR7_L0 | X86_DR7_G0 | X86_DR7_RW(0, X86_DR7_RW_EO) | X86_DR7_LEN(0, X86_DR7_LEN_BYTE) | X86_DR7_LE | X86_DR7_GE);
        Bs3RegSetDr6(X86_DR6_INIT_VAL);
        *BS3_XPTR_GET(uint32_t, StackXptr) = Ctx.ss;

        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &Ctx, X86_XCPT_DB, 0, 0, X86_DR6_INIT_VAL | X86_DR6_B0, cbIretFrameDb,
                                               uHandlerRspDb - (BS3_MODE_IS_RM_SYS(bTestMode) ? cbSpAdjust : 0) );
        Bs3RegSetDr7(0);

        /*
         * Test #6: Data breakpoint on SS load.  The #DB is delivered after ring transition.  Weird!
         *
         * Note! Intel loses the B0 status, probably for reasons similar to Pentium Pro errata 3.  Similar
         *       erratum is seen with virtually every march since, e.g. skylake SKL009 & SKL111.
         *       Weirdly enougth, they seem to get this right in real mode.  Go figure.
         *
         *       Update: In real mode there is no ring transition, so we'll be trampling on
         *       breakpoint again (POP SS changes SP) when the INT/whatever instruction writes
         *       the return address.
         */
        g_usBs3TestStep++;
        *BS3_XPTR_GET(uint32_t, StackXptr) = Ctx.ss;
        Bs3RegSetDr0(BS3_XPTR_GET_FLAT(uint32_t, StackXptr));
        Bs3RegSetDr7(X86_DR7_L0 | X86_DR7_G0 | X86_DR7_RW(0, X86_DR7_RW_RW) | X86_DR7_LEN(0, X86_DR7_LEN_WORD));
        Bs3RegSetDr6(X86_DR6_INIT_VAL);

        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        Bs3RegSetDr7(0);
        TrapExpectXferDb.Ctx.rip = TrapCtx.Ctx.rip; /// @todo fixme
        Bs3RegSetDr7(0);
        uDr6Expect = X86_DR6_INIT_VAL | X86_DR6_B0;
        if (g_enmCpuVendor == BS3CPUVENDOR_INTEL && (bTestMode != BS3_MODE_RM || cbSpAdjust == 0))
            uDr6Expect = X86_DR6_INIT_VAL;
        if (!fAlwaysUd)
            bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXferDb.Ctx, X86_XCPT_DB, 0, 0, uDr6Expect,
                                                   cbIretFrameSame, TrapExpectXferDb.uHandlerRsp);
        else
            bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXfer.Ctx, X86_XCPT_UD, 0, 0, uDr6Expect,
                                                   TrapExpectXfer.cbIretFrame, TrapExpectXfer.uHandlerRsp);

        /*
         * Test #7: Same as above, but with the LE and GE flags set.
         */
        g_usBs3TestStep++;
        *BS3_XPTR_GET(uint32_t, StackXptr) = Ctx.ss;
        Bs3RegSetDr0(BS3_XPTR_GET_FLAT(uint32_t, StackXptr));
        Bs3RegSetDr7(X86_DR7_L0 | X86_DR7_G0 | X86_DR7_RW(0, X86_DR7_RW_RW) | X86_DR7_LEN(0, X86_DR7_LEN_WORD) | X86_DR7_LE | X86_DR7_GE);
        Bs3RegSetDr6(X86_DR6_INIT_VAL);

        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        TrapExpectXferDb.Ctx.rip = TrapCtx.Ctx.rip; /// @todo fixme
        Bs3RegSetDr7(0);
        uDr6Expect = X86_DR6_INIT_VAL | X86_DR6_B0;
        if (g_enmCpuVendor == BS3CPUVENDOR_INTEL && (bTestMode != BS3_MODE_RM || cbSpAdjust == 0))
            uDr6Expect = X86_DR6_INIT_VAL;
        if (!fAlwaysUd)
            bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXferDb.Ctx, X86_XCPT_DB, 0, 0, uDr6Expect,
                                                   cbIretFrameSame, TrapExpectXferDb.uHandlerRsp);
        else
            bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXfer.Ctx, X86_XCPT_UD, 0, 0, uDr6Expect,
                                                   TrapExpectXfer.cbIretFrame, TrapExpectXfer.uHandlerRsp);

        if (!BS3_MODE_IS_RM_OR_V86(bTestMode))
        {
            /*
             * Test #8: Data breakpoint on SS GDT entry.  Half weird!
             * Note! Intel loses the B1 status, see test #6.
             */
            g_usBs3TestStep++;
            *BS3_XPTR_GET(uint32_t, StackXptr) = (Ctx.ss & X86_SEL_RPL) | BS3_SEL_SPARE_00;
            Bs3GdteSpare00 = Bs3Gdt[Ctx.ss / sizeof(Bs3Gdt[0])];

            Bs3RegSetDr1(Bs3SelPtrToFlat(&Bs3GdteSpare00));
            Bs3RegSetDr7(X86_DR7_L1 | X86_DR7_G1 | X86_DR7_RW(1, X86_DR7_RW_RW) | X86_DR7_LEN(1, X86_DR7_LEN_DWORD));
            Bs3RegSetDr6(X86_DR6_INIT_VAL);

            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            TrapExpectXferDb.Ctx.rip = TrapCtx.Ctx.rip; /// @todo fixme
            Bs3RegSetDr7(0);
            uDr6Expect = g_enmCpuVendor == BS3CPUVENDOR_INTEL ? X86_DR6_INIT_VAL : X86_DR6_INIT_VAL | X86_DR6_B1;
            if (!fAlwaysUd)
                bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXferDb.Ctx, X86_XCPT_DB, 0, 0, uDr6Expect,
                                                       cbIretFrameSame, TrapExpectXferDb.uHandlerRsp);
            else
                bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXfer.Ctx, X86_XCPT_UD, 0, 0, uDr6Expect,
                                                       TrapExpectXfer.cbIretFrame, TrapExpectXfer.uHandlerRsp);

            /*
             * Test #9: Same as above, but with the LE and GE flags set.
             */
            g_usBs3TestStep++;
            *BS3_XPTR_GET(uint32_t, StackXptr) = (Ctx.ss & X86_SEL_RPL) | BS3_SEL_SPARE_00;
            Bs3GdteSpare00 = Bs3Gdt[Ctx.ss / sizeof(Bs3Gdt[0])];

            Bs3RegSetDr1(Bs3SelPtrToFlat(&Bs3GdteSpare00));
            Bs3RegSetDr7(X86_DR7_L1 | X86_DR7_G1 | X86_DR7_RW(1, X86_DR7_RW_RW) | X86_DR7_LEN(1, X86_DR7_LEN_DWORD) | X86_DR7_LE | X86_DR7_GE);
            Bs3RegSetDr6(X86_DR6_INIT_VAL);

            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            TrapExpectXferDb.Ctx.rip = TrapCtx.Ctx.rip; /// @todo fixme
            Bs3RegSetDr7(0);
            uDr6Expect = g_enmCpuVendor == BS3CPUVENDOR_INTEL ? X86_DR6_INIT_VAL : X86_DR6_INIT_VAL | X86_DR6_B1;
            if (!fAlwaysUd)
                bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXferDb.Ctx, X86_XCPT_DB, 0, 0, uDr6Expect,
                                                       cbIretFrameSame, TrapExpectXferDb.uHandlerRsp);
            else
                bs3CpuWeird1_CompareDbgInhibitRingXfer(&TrapCtx, &TrapExpectXfer.Ctx, X86_XCPT_UD, 0, 0, uDr6Expect,
                                                       TrapExpectXfer.cbIretFrame, TrapExpectXfer.uHandlerRsp);
        }

        /*
         * Cleanup.
         */
        Bs3RegSetDr0(0);
        Bs3RegSetDr1(0);
        Bs3RegSetDr2(0);
        Bs3RegSetDr3(0);
        Bs3RegSetDr6(X86_DR6_INIT_VAL);
        Bs3RegSetDr7(0);
    }
    Bs3TrapSetDpl(bIntGate, bSavedDpl);
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuWeird1_DbgInhibitRingXfer)(uint8_t bMode)
{
    if (BS3_MODE_IS_V86(bMode))
        switch (bMode)
        {
            /** @todo some busted stack stuff with the 16-bit guys.  Also, if VME is
             *        enabled, we're probably not able to do any sensible testing. */
            case BS3_MODE_PP16_V86:
            case BS3_MODE_PE16_V86:
            case BS3_MODE_PAE16_V86:
                return BS3TESTDOMODE_SKIPPED;
        }
    //if (bMode != BS3_MODE_PE16_V86) return BS3TESTDOMODE_SKIPPED;
    //if (bMode != BS3_MODE_PAEV86) return BS3TESTDOMODE_SKIPPED;

    bs3CpuWeird1_SetGlobals(bMode);

    /** @todo test sysenter and syscall too. */
    /** @todo test INTO. */
    /** @todo test all V8086 software INT delivery modes (currently only 4 and 1). */

#define ASM_FN_ARGS(a_Name, a_Label, a_ModeSuff, a_Type) \
        bs3CpuWeird1_##a_Name##_##a_ModeSuff, bs3CpuWeird1_##a_Name##_##a_Label##_##a_ModeSuff, DbgInhibitRingXferType_##a_Type

    /* Note! Both ICEBP and BOUND has be checked cursorily and found not to be affected. */
    if (BS3_MODE_IS_16BIT_CODE(bMode))
    {
        bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x80, 2, 2, ASM_FN_ARGS(InhibitedPopSsInt80, int80, c16, SoftInt));
        bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x80, 2, 0, ASM_FN_ARGS(InhibitedMovSsInt80, int80, c16, SoftInt));
        if (!BS3_MODE_IS_V86(bMode) || !g_fVME)
        {
            /** @todo explain why these GURU     */
            bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x03, 2, 2, ASM_FN_ARGS(InhibitedPopSsInt3, int3, c16, SoftInt));
            bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x03, 2, 0, ASM_FN_ARGS(InhibitedMovSsInt3, int3, c16, SoftInt));
            bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x03, 1, 2, ASM_FN_ARGS(InhibitedPopSsBp,   int3, c16, SoftInt));
            bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x03, 1, 0, ASM_FN_ARGS(InhibitedMovSsBp,   int3, c16, SoftInt));
        }
    }
    else if (BS3_MODE_IS_32BIT_CODE(bMode))
    {
        bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x80, 2, 4, ASM_FN_ARGS(InhibitedPopSsInt80, int80, c32, SoftInt));
        bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x80, 2, 0, ASM_FN_ARGS(InhibitedMovSsInt80, int80, c32, SoftInt));
        bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x03, 2, 4, ASM_FN_ARGS(InhibitedPopSsInt3,  int3,  c32, SoftInt));
        bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x03, 2, 0, ASM_FN_ARGS(InhibitedMovSsInt3,  int3,  c32, SoftInt));
        bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x03, 1, 4, ASM_FN_ARGS(InhibitedPopSsBp,    int3,  c32, SoftInt));
        bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x03, 1, 0, ASM_FN_ARGS(InhibitedMovSsBp,    int3,  c32, SoftInt));
    }
    else
    {
        bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x80, 2, 0, ASM_FN_ARGS(InhibitedMovSsInt80, int80, c64, SoftInt));
        bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x03, 2, 0, ASM_FN_ARGS(InhibitedMovSsInt3,  int3,  c64, SoftInt));
        bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0x03, 1, 0, ASM_FN_ARGS(InhibitedMovSsBp,    int3,  c64, SoftInt));
    }

    /* On intel, syscall only works in long mode. */
/** @todo test this on AMD and extend it to non-64-bit modes */
    if (BS3_MODE_IS_64BIT_SYS(bMode))
    {
        uint64_t const fSavedEfer = ASMRdMsr(MSR_K6_EFER);
        ASMWrMsr(MSR_K8_SF_MASK, X86_EFL_TF);
        ASMWrMsr(MSR_K8_LSTAR, g_pfnBs3Syscall64GenericFlat);
        ASMWrMsr(MSR_K8_CSTAR, g_pfnBs3Syscall64GenericCompatibilityFlat);
        ASMWrMsr(MSR_K6_STAR, (uint64_t)BS3_SEL_R0_CS64 << MSR_K6_STAR_SYSCALL_CS_SS_SHIFT);
        ASMWrMsr(MSR_K6_EFER, fSavedEfer | MSR_K6_EFER_SCE);

        if (BS3_MODE_IS_16BIT_CODE(bMode))
        {
            bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0xfe, 2, 0, ASM_FN_ARGS(InhibitedMovSsSyscall, syscall, c16, Syscall));
        }
        else if (BS3_MODE_IS_32BIT_CODE(bMode))
        {
            bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0xfe, 2, 0, ASM_FN_ARGS(InhibitedMovSsSyscall, syscall, c32, Syscall));
        }
        else
        {
            bs3CpuWeird1_DbgInhibitRingXfer_Worker(bMode, 0xff, 2, 0, ASM_FN_ARGS(InhibitedMovSsSyscall, syscall, c64, Syscall));
        }

        ASMWrMsr(MSR_K6_EFER, fSavedEfer);
        ASMWrMsr(MSR_K6_STAR, 0);
        ASMWrMsr(MSR_K8_LSTAR, 0);
        ASMWrMsr(MSR_K8_CSTAR, 0);
        ASMWrMsr(MSR_K8_SF_MASK, 0);
    }

    return 0;
}


/*********************************************************************************************************************************
*   IP / EIP  Wrapping                                                                                                           *
*********************************************************************************************************************************/
PROTO_ALL_WITH_END(bs3CpuWeird1_PcWrapBenign1);
PROTO_ALL_WITH_END(bs3CpuWeird1_PcWrapBenign2);
PROTO_ALL_WITH_END(bs3CpuWeird1_PcWrapCpuId);
PROTO_ALL_WITH_END(bs3CpuWeird1_PcWrapIn80);
PROTO_ALL_WITH_END(bs3CpuWeird1_PcWrapOut80);
PROTO_ALL_WITH_END(bs3CpuWeird1_PcWrapSmsw);
PROTO_ALL_WITH_END(bs3CpuWeird1_PcWrapRdCr0);
PROTO_ALL_WITH_END(bs3CpuWeird1_PcWrapRdDr0);
PROTO_ALL_WITH_END(bs3CpuWeird1_PcWrapWrDr0);

typedef enum { kPcWrapSetup_None, kPcWrapSetup_ZeroRax } PCWRAPSETUP;

/**
 * Compares pc wraparound result.
 */
static uint8_t bs3CpuWeird1_ComparePcWrap(PCBS3TRAPFRAME pTrapCtx, PCBS3TRAPFRAME pTrapExpect)
{
    uint16_t const cErrorsBefore = Bs3TestSubErrorCount();
    CHECK_MEMBER("bXcpt",       "%#04x",    pTrapCtx->bXcpt,        pTrapExpect->bXcpt);
    CHECK_MEMBER("bErrCd",      "%#06RX64", pTrapCtx->uErrCd,       pTrapExpect->uErrCd);
    Bs3TestCheckRegCtxEx(&pTrapCtx->Ctx, &pTrapExpect->Ctx, 0 /*cbPcAdjust*/, 0 /*cbSpAdjust*/, 0 /*fExtraEfl*/,
                         g_pszTestMode, g_usBs3TestStep);
    if (Bs3TestSubErrorCount() != cErrorsBefore)
    {
        Bs3TrapPrintFrame(pTrapCtx);
        Bs3TestPrintf("CS=%04RX16 SS:ESP=%04RX16:%08RX64 EFL=%RX64 cbIret=%#x\n",
                      pTrapCtx->uHandlerCs, pTrapCtx->uHandlerSs, pTrapCtx->uHandlerRsp,
                      pTrapCtx->fHandlerRfl, pTrapCtx->cbIretFrame);
#if 0
        Bs3TestPrintf("Halting in ComparePcWrap: bXcpt=%#x\n", pTrapCtx->bXcpt);
        ASMHalt();
#endif
        return 1;
    }
    return 0;
}


static uint8_t bs3CpuWeird1_PcWrapping_Worker16(uint8_t bMode, RTSEL SelCode, uint8_t BS3_FAR *pbHead,
                                                uint8_t BS3_FAR *pbTail, uint8_t BS3_FAR *pbAfter,
                                                void const BS3_FAR *pvTemplate, size_t cbTemplate, PCWRAPSETUP enmSetup)
{
    BS3TRAPFRAME            TrapCtx;
    BS3TRAPFRAME            TrapExpect;
    BS3REGCTX               Ctx;
    uint8_t                 bXcpt;

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));
    Bs3MemZero(&TrapExpect, sizeof(TrapExpect));

    /*
     * Create the expected result by first placing the code template
     * at the start of the buffer and giving it a quick run.
     *
     * I cannot think of any instruction always causing #GP(0) right now, so
     * we generate a ud2 and modify it instead.
     */
    Bs3MemCpy(pbHead, pvTemplate, cbTemplate);
    if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) <= BS3CPU_80286)
    {
        pbHead[cbTemplate] = 0xcc;      /* int3 */
        bXcpt = X86_XCPT_BP;
    }
    else
    {
        pbHead[cbTemplate]     = 0x0f;  /* ud2 */
        pbHead[cbTemplate + 1] = 0x0b;
        bXcpt = X86_XCPT_UD;
    }

    Bs3RegCtxSaveEx(&Ctx, bMode, 1024);

    Ctx.cs    = SelCode;
    Ctx.rip.u = 0;
    switch (enmSetup)
    {
        case kPcWrapSetup_None:
            break;
        case kPcWrapSetup_ZeroRax:
            Ctx.rax.u = 0;
            break;
    }

    /* V8086: Set IOPL to 3. */
    if (BS3_MODE_IS_V86(bMode))
        Ctx.rflags.u32 |= X86_EFL_IOPL;

    Bs3TrapSetJmpAndRestore(&Ctx, &TrapExpect);
    if (TrapExpect.bXcpt != bXcpt)
    {

        Bs3TestFailedF("%u: Setup: bXcpt is %#x, expected %#x!\n", g_usBs3TestStep, TrapExpect.bXcpt, bXcpt);
        Bs3TrapPrintFrame(&TrapExpect);
        return 1;
    }

    /*
     * Adjust the contexts for the real test.
     */
    Ctx.cs    = SelCode;
    Ctx.rip.u = (uint32_t)_64K - cbTemplate;

    if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) <= BS3CPU_80286)
        TrapExpect.Ctx.rip.u = 1;
    else
    {
        if (BS3_MODE_IS_16BIT_SYS(bMode))
            TrapExpect.Ctx.rip.u = 0;
        else
            TrapExpect.Ctx.rip.u = UINT32_C(0x10000);
        TrapExpect.bXcpt  = X86_XCPT_GP;
        TrapExpect.uErrCd = 0;
    }

    /*
     * Prepare the buffer for 16-bit wrap around.
     */
    Bs3MemSet(pbHead, 0xcc, 64);        /* int3 */
    if (bXcpt == X86_XCPT_UD)
    {
        pbHead[0] = 0x0f;               /* ud2 */
        pbHead[1] = 0x0b;
    }
    Bs3MemCpy(&pbTail[_4K - cbTemplate], pvTemplate, cbTemplate);
    Bs3MemSet(pbAfter, 0xf1, 64);       /* icebp / int1 */

    /*
     * Do a test run.
     */
    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
    if (!bs3CpuWeird1_ComparePcWrap(&TrapCtx, &TrapExpect))
    {
#if 0 /* needs more work */
        /*
         * Slide the instruction template across the boundrary byte-by-byte and
         * check that it triggers #GP on the initial instruction on 386+.
         */
        unsigned cbTail;
        unsigned cbHead;
        g_usBs3TestStep++;
        for (cbTail = cbTemplate - 1, cbHead = 1; cbTail > 0; cbTail--, cbHead++, g_usBs3TestStep++)
        {
            pbTail[X86_PAGE_SIZE - cbTail - 1] = 0xcc;
            Bs3MemCpy(&pbTail[X86_PAGE_SIZE - cbTail], pvTemplate, cbTail);
            Bs3MemCpy(pbHead, &((uint8_t const *)pvTemplate)[cbTail], cbHead);
            if (bXcpt == X86_XCPT_BP)
                pbHead[cbHead]     = 0xcc; /* int3 */
            else
            {
                pbHead[cbHead]     = 0x0f; /* ud2 */
                pbHead[cbHead + 1] = 0x0b;
            }

            Ctx.rip.u = (uint32_t)_64K - cbTail;
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) <= BS3CPU_80286)
                TrapExpect.Ctx.rip.u = cbHead + 1;
            else
            {
                TrapExpect.Ctx.rip.u = Ctx.rip.u;
            }

            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (bs3CpuWeird1_ComparePcWrap(&TrapCtx, &TrapExpect))
                return 1;
        }
#endif
    }
    return 0;
}


static uint8_t bs3CpuWeird1_PcWrapping_Worker32(uint8_t bMode, RTSEL SelCode, uint8_t BS3_FAR *pbPage1,
                                                uint8_t BS3_FAR *pbPage2, uint32_t uFlatPage2,
                                                void const BS3_FAR *pvTemplate, size_t cbTemplate, PCWRAPSETUP enmSetup)
{
    BS3TRAPFRAME            TrapCtx;
    BS3TRAPFRAME            TrapExpect;
    BS3REGCTX               Ctx;
    unsigned                cbPage1;
    unsigned                cbPage2;

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));
    Bs3MemZero(&TrapExpect, sizeof(TrapExpect));

    //Bs3TestPrintf("SelCode=%#x pbPage1=%p pbPage2=%p uFlatPage2=%RX32 pvTemplate=%p cbTemplate\n",
    //              SelCode, pbPage1, pbPage2, uFlatPage2, pvTemplate, cbTemplate);

    /*
     * Create the expected result by first placing the code template
     * at the start of the buffer and giving it a quick run.
     */
    Bs3MemSet(pbPage1, 0xcc, _4K);
    Bs3MemSet(pbPage2, 0xcc, _4K);
    Bs3MemCpy(&pbPage1[_4K - cbTemplate], pvTemplate, cbTemplate);
    pbPage2[0] = 0x0f;      /* ud2 */
    pbPage2[1] = 0x0b;

    Bs3RegCtxSaveEx(&Ctx, bMode, 1024);

    Ctx.cs    = BS3_SEL_R0_CS32;
    Ctx.rip.u = uFlatPage2 - cbTemplate;
    switch (enmSetup)
    {
        case kPcWrapSetup_None:
            break;
        case kPcWrapSetup_ZeroRax:
            Ctx.rax.u = 0;
            break;
    }

    Bs3TrapSetJmpAndRestore(&Ctx, &TrapExpect);
    if (TrapExpect.bXcpt != X86_XCPT_UD)
    {

        Bs3TestFailedF("%u: Setup: bXcpt is %#x, expected %#x!\n", g_usBs3TestStep, TrapExpect.bXcpt, X86_XCPT_UD);
        Bs3TrapPrintFrame(&TrapExpect);
        return 1;
    }

    /*
     * The real test uses the special CS selector.
     */
    Ctx.cs            = SelCode;
    TrapExpect.Ctx.cs = SelCode;

    /*
     * Unlike 16-bit mode, the instruction may cross the wraparound boundary,
     * so we test by advancing the template across byte-by-byte.
     */
    for (cbPage1 = cbTemplate, cbPage2 = 0; cbPage1 > 0; cbPage1--, cbPage2++, g_usBs3TestStep++)
    {
        pbPage1[X86_PAGE_SIZE - cbPage1 - 1] = 0xcc;
        Bs3MemCpy(&pbPage1[X86_PAGE_SIZE - cbPage1], pvTemplate, cbPage1);
        Bs3MemCpy(pbPage2, &((uint8_t const *)pvTemplate)[cbPage1], cbPage2);
        pbPage2[cbPage2]     = 0x0f;    /* ud2 */
        pbPage2[cbPage2 + 1] = 0x0b;

        Ctx.rip.u = UINT32_MAX - cbPage1 + 1;
        TrapExpect.Ctx.rip.u = cbPage2;

        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        if (bs3CpuWeird1_ComparePcWrap(&TrapCtx, &TrapExpect))
            return 1;
    }
    return 0;
}


static uint8_t bs3CpuWeird1_PcWrapping_Worker64(uint8_t bMode, uint8_t BS3_FAR *pbBuf, uint32_t uFlatBuf,
                                                void const BS3_FAR *pvTemplate, size_t cbTemplate, PCWRAPSETUP enmSetup)
{
    uint8_t BS3_FAR * const pbPage1 = pbBuf;                 /* mapped at 0, 4G and 8G */
    uint8_t BS3_FAR * const pbPage2 = &pbBuf[X86_PAGE_SIZE]; /* mapped at -4K, 4G-4K and 8G-4K. */
    BS3TRAPFRAME            TrapCtx;
    BS3TRAPFRAME            TrapExpect;
    BS3REGCTX               Ctx;
    unsigned                cbStart;
    unsigned                cbEnd;

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));
    Bs3MemZero(&TrapExpect, sizeof(TrapExpect));

    /*
     * Create the expected result by first placing the code template
     * at the start of the buffer and giving it a quick run.
     */
    Bs3MemCpy(pbPage1, pvTemplate, cbTemplate);
    pbPage1[cbTemplate]     = 0x0f; /* ud2 */
    pbPage1[cbTemplate + 1] = 0x0b;

    Bs3RegCtxSaveEx(&Ctx, bMode, 1024);

    Ctx.rip.u = uFlatBuf;
    switch (enmSetup)
    {
        case kPcWrapSetup_None:
            break;
        case kPcWrapSetup_ZeroRax:
            Ctx.rax.u = 0;
            break;
    }

    Bs3TrapSetJmpAndRestore(&Ctx, &TrapExpect);
    if (TrapExpect.bXcpt != X86_XCPT_UD)
    {

        Bs3TestFailedF("%u: Setup: bXcpt is %#x, expected %#x!\n", g_usBs3TestStep, TrapExpect.bXcpt, X86_XCPT_UD);
        Bs3TrapPrintFrame(&TrapExpect);
        return 1;
    }

    /*
     * Unlike 16-bit mode, the instruction may cross the wraparound boundary,
     * so we test by advancing the template across byte-by-byte.
     *
     * Page #1 is mapped at address zero and Page #2 as the last one.
     */
    Bs3MemSet(pbBuf, 0xf1, X86_PAGE_SIZE * 2);
    for (cbStart = cbTemplate, cbEnd = 0; cbStart > 0; cbStart--, cbEnd++)
    {
        pbPage2[X86_PAGE_SIZE - cbStart - 1] = 0xf1;
        Bs3MemCpy(&pbPage2[X86_PAGE_SIZE - cbStart], pvTemplate, cbStart);
        Bs3MemCpy(pbPage1, &((uint8_t const *)pvTemplate)[cbStart], cbEnd);
        pbPage1[cbEnd]     = 0x0f;    /* ud2 */
        pbPage1[cbEnd + 1] = 0x0b;

        Ctx.rip.u            = UINT64_MAX - cbStart + 1;
        TrapExpect.Ctx.rip.u = cbEnd;

        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        if (bs3CpuWeird1_ComparePcWrap(&TrapCtx, &TrapExpect))
            return 1;
        g_usBs3TestStep++;

        /* Also check that crossing 4G isn't buggered up in our code by
           32-bit and 16-bit mode support.*/
        Ctx.rip.u            = _4G - cbStart;
        TrapExpect.Ctx.rip.u = _4G + cbEnd;
        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        if (bs3CpuWeird1_ComparePcWrap(&TrapCtx, &TrapExpect))
            return 1;
        g_usBs3TestStep++;

        Ctx.rip.u            = _4G*2 - cbStart;
        TrapExpect.Ctx.rip.u = _4G*2 + cbEnd;
        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        if (bs3CpuWeird1_ComparePcWrap(&TrapCtx, &TrapExpect))
            return 1;
        g_usBs3TestStep += 2;
    }
    return 0;
}



BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuWeird1_PcWrapping)(uint8_t bMode)
{
    uint8_t bRet = 1;
    size_t  i;

    bs3CpuWeird1_SetGlobals(bMode);

    if (BS3_MODE_IS_16BIT_CODE(bMode))
    {
        /*
         * For 16-bit testing, we need a 68 KB buffer.
         *
         * This is a little annoying to work with from 16-bit bit, so we use
         * separate pointers to each interesting bit of it.
         */
        /** @todo add api for doing this, so we don't need to include bs3-cmn-memory.h. */
        uint8_t BS3_FAR *pbBuf = (uint8_t BS3_FAR *)Bs3SlabAllocEx(&g_Bs3Mem4KLow.Core, 17 /*cPages*/, 0 /*fFlags*/);
        if (pbBuf != NULL)
        {
            uint32_t const   uFlatBuf = Bs3SelPtrToFlat(pbBuf);
            uint8_t BS3_FAR *pbTail   = Bs3XptrFlatToCurrent(uFlatBuf + 0x0f000);
            uint8_t BS3_FAR *pbAfter  = Bs3XptrFlatToCurrent(uFlatBuf + UINT32_C(0x10000));
            RTSEL            SelCode;
            uint32_t         off;
            static struct { FPFNBS3FAR pfnStart, pfnEnd;  PCWRAPSETUP enmSetup; unsigned fNoV86 : 1; }
            const s_aTemplates16[] =
            {
#define ENTRY16(a_Template, a_enmSetup, a_fNoV86)     { a_Template ## _c16, a_Template ## _c16_EndProc, a_enmSetup, a_fNoV86 }
                ENTRY16(bs3CpuWeird1_PcWrapBenign1, kPcWrapSetup_None,    0),
                ENTRY16(bs3CpuWeird1_PcWrapBenign2, kPcWrapSetup_None,    0),
                ENTRY16(bs3CpuWeird1_PcWrapCpuId,   kPcWrapSetup_ZeroRax, 0),
                ENTRY16(bs3CpuWeird1_PcWrapIn80,    kPcWrapSetup_None,    0),
                ENTRY16(bs3CpuWeird1_PcWrapOut80,   kPcWrapSetup_None,    0),
                ENTRY16(bs3CpuWeird1_PcWrapSmsw,    kPcWrapSetup_None,    0),
                ENTRY16(bs3CpuWeird1_PcWrapRdCr0,   kPcWrapSetup_None,    1),
                ENTRY16(bs3CpuWeird1_PcWrapRdDr0,   kPcWrapSetup_None,    1),
                ENTRY16(bs3CpuWeird1_PcWrapWrDr0,   kPcWrapSetup_ZeroRax, 1),
#undef ENTRY16
            };

            /* Fill the buffer with int1 instructions: */
            for (off = 0; off < UINT32_C(0x11000); off += _4K)
            {
                uint8_t BS3_FAR *pbPage = Bs3XptrFlatToCurrent(uFlatBuf + off);
                Bs3MemSet(pbPage, 0xf1, _4K);
            }

            /* Setup the CS for it. */
            SelCode = (uint16_t)(uFlatBuf >> 4);
            if (!BS3_MODE_IS_RM_OR_V86(bMode))
            {
                Bs3SelSetup16BitCode(&Bs3GdteSpare00, uFlatBuf, 0);
                SelCode = BS3_SEL_SPARE_00;
            }

            /* Allow IN and OUT to port 80h from V8086 mode. */
            if (BS3_MODE_IS_V86(bMode))
            {
                Bs3RegSetTr(BS3_SEL_TSS32_IOBP_IRB);
                ASMBitClear(Bs3SharedIobp, 0x80);
            }

            for (i = 0; i < RT_ELEMENTS(s_aTemplates16); i++)
            {
                if (!s_aTemplates16[i].fNoV86 || !BS3_MODE_IS_V86(bMode))
                    bs3CpuWeird1_PcWrapping_Worker16(bMode, SelCode, pbBuf, pbTail, pbAfter, s_aTemplates16[i].pfnStart,
                                                     (uintptr_t)s_aTemplates16[i].pfnEnd - (uintptr_t)s_aTemplates16[i].pfnStart,
                                                     s_aTemplates16[i].enmSetup);
                g_usBs3TestStep = i * 256;
            }

            if (BS3_MODE_IS_V86(bMode))
                ASMBitSet(Bs3SharedIobp, 0x80);

            Bs3SlabFree(&g_Bs3Mem4KLow.Core, uFlatBuf, 17);

            bRet = 0;
        }
        else
            Bs3TestFailed("Failed to allocate 17 pages (68KB)");
    }
    else
    {
        /*
         * For 32-bit and 64-bit mode we just need two pages.
         */
        size_t const     cbBuf = X86_PAGE_SIZE * 2;
        uint8_t BS3_FAR *pbBuf = (uint8_t BS3_FAR *)Bs3MemAlloc(BS3MEMKIND_TILED, cbBuf);
        if (pbBuf)
        {
            uint32_t const uFlatBuf = Bs3SelPtrToFlat(pbBuf);
            Bs3MemSet(pbBuf, 0xf1, cbBuf);

            /*
             * For 32-bit we set up a CS that starts with the 2nd page and
             * ends with the first.
             */
            if (BS3_MODE_IS_32BIT_CODE(bMode))
            {
                static struct { FPFNBS3FAR pfnStart, pfnEnd; PCWRAPSETUP enmSetup; } const s_aTemplates32[] =
                {
#define ENTRY32(a_Template, a_enmSetup)  { a_Template ## _c32, a_Template ## _c32_EndProc, a_enmSetup }
                    ENTRY32(bs3CpuWeird1_PcWrapBenign1, kPcWrapSetup_None),
                    ENTRY32(bs3CpuWeird1_PcWrapBenign2, kPcWrapSetup_None),
                    ENTRY32(bs3CpuWeird1_PcWrapCpuId,   kPcWrapSetup_ZeroRax),
                    ENTRY32(bs3CpuWeird1_PcWrapIn80,    kPcWrapSetup_None),
                    ENTRY32(bs3CpuWeird1_PcWrapOut80,   kPcWrapSetup_None),
                    ENTRY32(bs3CpuWeird1_PcWrapSmsw,    kPcWrapSetup_None),
                    ENTRY32(bs3CpuWeird1_PcWrapRdCr0,   kPcWrapSetup_None),
                    ENTRY32(bs3CpuWeird1_PcWrapRdDr0,   kPcWrapSetup_None),
                    ENTRY32(bs3CpuWeird1_PcWrapWrDr0,   kPcWrapSetup_ZeroRax),
#undef ENTRY32
                };

                Bs3SelSetup32BitCode(&Bs3GdteSpare00, uFlatBuf + X86_PAGE_SIZE, UINT32_MAX, 0);

                for (i = 0; i < RT_ELEMENTS(s_aTemplates32); i++)
                {
                    //Bs3TestPrintf("pfnStart=%p pfnEnd=%p\n", s_aTemplates32[i].pfnStart, s_aTemplates32[i].pfnEnd);
                    bs3CpuWeird1_PcWrapping_Worker32(bMode, BS3_SEL_SPARE_00, pbBuf, &pbBuf[X86_PAGE_SIZE],
                                                     uFlatBuf + X86_PAGE_SIZE, Bs3SelLnkPtrToCurPtr(s_aTemplates32[i].pfnStart),
                                                     (uintptr_t)s_aTemplates32[i].pfnEnd - (uintptr_t)s_aTemplates32[i].pfnStart,
                                                     s_aTemplates32[i].enmSetup);
                    g_usBs3TestStep = i * 256;
                }

                bRet = 0;
            }
            /*
             * For 64-bit we have to alias the two buffer pages to the first and
             * last page in the address space. To test that the 32-bit 4G rollover
             * isn't incorrectly applied to LM64, we repeat this mapping for the
             * 4G  and 8G boundaries too.
             *
             * This ASSUMES there is nothing important in page 0 when in LM64.
             */
            else
            {
                static const struct { uint64_t uDst; uint16_t off; } s_aMappings[] =
                {
                    { UINT64_MAX - X86_PAGE_SIZE + 1, X86_PAGE_SIZE * 1 },
                    { UINT64_C(0),                    X86_PAGE_SIZE * 0 },
#if 1 /* technically not required as we just repeat the same 4G address space in long mode: */
                    { _4G - X86_PAGE_SIZE,            X86_PAGE_SIZE * 1 },
                    { _4G,                            X86_PAGE_SIZE * 0 },
                    { _4G*2 - X86_PAGE_SIZE,          X86_PAGE_SIZE * 1 },
                    { _4G*2,                          X86_PAGE_SIZE * 0 },
#endif
                };
                int      rc = VINF_SUCCESS;
                unsigned iMap;
                BS3_ASSERT(bMode == BS3_MODE_LM64);
                for (iMap = 0; iMap < RT_ELEMENTS(s_aMappings) && RT_SUCCESS(rc); iMap++)
                {
                    rc = Bs3PagingAlias(s_aMappings[iMap].uDst, uFlatBuf + s_aMappings[iMap].off, X86_PAGE_SIZE,
                                        X86_PTE_P | X86_PTE_A | X86_PTE_D | X86_PTE_RW);
                    if (RT_FAILURE(rc))
                        Bs3TestFailedF("Bs3PagingAlias(%#RX64,...) failed: %d", s_aMappings[iMap].uDst, rc);
                }

                if (RT_SUCCESS(rc))
                {
                    static struct { FPFNBS3FAR pfnStart, pfnEnd; PCWRAPSETUP enmSetup; } const s_aTemplates64[] =
                    {
#define ENTRY64(a_Template, a_enmSetup) { a_Template ## _c64, a_Template ## _c64_EndProc, a_enmSetup }
                        ENTRY64(bs3CpuWeird1_PcWrapBenign1, kPcWrapSetup_None),
                        ENTRY64(bs3CpuWeird1_PcWrapBenign2, kPcWrapSetup_None),
                        ENTRY64(bs3CpuWeird1_PcWrapCpuId,   kPcWrapSetup_ZeroRax),
                        ENTRY64(bs3CpuWeird1_PcWrapIn80,    kPcWrapSetup_None),
                        ENTRY64(bs3CpuWeird1_PcWrapOut80,   kPcWrapSetup_None),
                        ENTRY64(bs3CpuWeird1_PcWrapSmsw,    kPcWrapSetup_None),
                        ENTRY64(bs3CpuWeird1_PcWrapRdCr0,   kPcWrapSetup_None),
                        ENTRY64(bs3CpuWeird1_PcWrapRdDr0,   kPcWrapSetup_None),
                        ENTRY64(bs3CpuWeird1_PcWrapWrDr0,   kPcWrapSetup_ZeroRax),
#undef ENTRY64
                    };

                    for (i = 0; i < RT_ELEMENTS(s_aTemplates64); i++)
                    {
                        bs3CpuWeird1_PcWrapping_Worker64(bMode, pbBuf, uFlatBuf,
                                                         Bs3SelLnkPtrToCurPtr(s_aTemplates64[i].pfnStart),
                                                            (uintptr_t)s_aTemplates64[i].pfnEnd
                                                          - (uintptr_t)s_aTemplates64[i].pfnStart,
                                                         s_aTemplates64[i].enmSetup);
                        g_usBs3TestStep = i * 256;
                    }

                    bRet = 0;

                    Bs3PagingUnalias(UINT64_C(0), X86_PAGE_SIZE);
                }

                while (iMap-- > 0)
                    Bs3PagingUnalias(s_aMappings[iMap].uDst, X86_PAGE_SIZE);
            }
            Bs3MemFree(pbBuf, cbBuf);
        }
        else
            Bs3TestFailed("Failed to allocate 2-3 pages for tests.");
    }

    return bRet;
}


/*********************************************************************************************************************************
*   PUSH / POP                                                                                                                   *
*********************************************************************************************************************************/
PROTO_ALL(bs3CpuWeird1_Push_xSP_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_opsize_xSP_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_opsize_xBX_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_xSP_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_opsize_xSP_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_opsize_xBX_Ud2);


/**
 * Compares push/pop result.
 */
static uint8_t bs3CpuWeird1_ComparePushPop(PCBS3TRAPFRAME pTrapCtx, PCBS3TRAPFRAME pTrapExpect)
{
    uint16_t const cErrorsBefore = Bs3TestSubErrorCount();
    CHECK_MEMBER("bXcpt",       "%#04x",    pTrapCtx->bXcpt,        pTrapExpect->bXcpt);
    CHECK_MEMBER("bErrCd",      "%#06RX64", pTrapCtx->uErrCd,       pTrapExpect->uErrCd);
    Bs3TestCheckRegCtxEx(&pTrapCtx->Ctx, &pTrapExpect->Ctx, 0 /*cbPcAdjust*/, 0 /*cbSpAdjust*/, 0 /*fExtraEfl*/,
                         g_pszTestMode, g_usBs3TestStep);
    if (Bs3TestSubErrorCount() != cErrorsBefore)
    {
        Bs3TrapPrintFrame(pTrapCtx);
        Bs3TestPrintf("CS=%04RX16 SS:ESP=%04RX16:%08RX64 EFL=%RX64 cbIret=%#x\n",
                      pTrapCtx->uHandlerCs, pTrapCtx->uHandlerSs, pTrapCtx->uHandlerRsp,
                      pTrapCtx->fHandlerRfl, pTrapCtx->cbIretFrame);
#if 0
        Bs3TestPrintf("Halting in ComparePushPop: bXcpt=%#x\n", pTrapCtx->bXcpt);
        ASMHalt();
#endif
        return 1;
    }
    return 0;
}


/** Initialize the stack around the CS:RSP with fixed values. */
static void bs3CpuWeird1_PushPopInitStack(BS3PTRUNION PtrStack)
{
    PtrStack.pu16[-8] = UINT16_C(0x1e0f);
    PtrStack.pu16[-7] = UINT16_C(0x3c2d);
    PtrStack.pu16[-6] = UINT16_C(0x5a4b);
    PtrStack.pu16[-5] = UINT16_C(0x7869);
    PtrStack.pu16[-4] = UINT16_C(0x9687);
    PtrStack.pu16[-3] = UINT16_C(0xb4a5);
    PtrStack.pu16[-2] = UINT16_C(0xd2c3);
    PtrStack.pu16[-1] = UINT16_C(0xf0e1);
    PtrStack.pu16[0]  = UINT16_C(0xfdec);
    PtrStack.pu16[1]  = UINT16_C(0xdbca);
    PtrStack.pu16[2]  = UINT16_C(0xb9a8);
    PtrStack.pu16[3]  = UINT16_C(0x9786);
    PtrStack.pu16[4]  = UINT16_C(0x7564);
    PtrStack.pu16[5]  = UINT16_C(0x5342);
    PtrStack.pu16[6]  = UINT16_C(0x3120);
}


BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuWeird1_PushPop)(uint8_t bTestMode)
{
    static struct
    {
        FPFNBS3FAR  pfnStart;
        uint8_t     cBits;
        bool        fPush;      /**< true if push, false if pop. */
        int8_t      cbAdjSp;    /**< The SP adjustment value. */
        uint8_t     idxReg;     /**< The X86_GREG_xXX value of the register in question. */
        uint8_t     offUd2;     /**< The UD2 offset into the code. */
    } s_aTests[] =
    {
        { bs3CpuWeird1_Push_opsize_xBX_Ud2_c16, 16, true,  -4, X86_GREG_xBX, 2 },
        { bs3CpuWeird1_Pop_opsize_xBX_Ud2_c16,  16, false, +4, X86_GREG_xBX, 2 },
        { bs3CpuWeird1_Push_xSP_Ud2_c16,        16, true,  -2, X86_GREG_xSP, 1 },
        { bs3CpuWeird1_Push_opsize_xSP_Ud2_c16, 16, true,  -4, X86_GREG_xSP, 2 },
        { bs3CpuWeird1_Pop_xSP_Ud2_c16,         16, false, +2, X86_GREG_xSP, 1 },
        { bs3CpuWeird1_Pop_opsize_xSP_Ud2_c16,  16, false, +4, X86_GREG_xSP, 2 },

        { bs3CpuWeird1_Push_opsize_xBX_Ud2_c32, 32, true,  -2, X86_GREG_xBX, 2 },
        { bs3CpuWeird1_Pop_opsize_xBX_Ud2_c32,  32, false, +2, X86_GREG_xBX, 2 },
        { bs3CpuWeird1_Push_xSP_Ud2_c32,        32, true,  -4, X86_GREG_xSP, 1 },
        { bs3CpuWeird1_Push_opsize_xSP_Ud2_c32, 32, true,  -2, X86_GREG_xSP, 2 },
        { bs3CpuWeird1_Pop_xSP_Ud2_c32,         32, false, +4, X86_GREG_xSP, 1 },
        { bs3CpuWeird1_Pop_opsize_xSP_Ud2_c32,  32, false, +2, X86_GREG_xSP, 2 },

        { bs3CpuWeird1_Push_opsize_xBX_Ud2_c64, 64, true,  -2, X86_GREG_xBX, 2 },
        { bs3CpuWeird1_Pop_opsize_xBX_Ud2_c64,  64, false, +2, X86_GREG_xBX, 2 },
        { bs3CpuWeird1_Push_xSP_Ud2_c64,        64, true,  -8, X86_GREG_xSP, 1 },
        { bs3CpuWeird1_Push_opsize_xSP_Ud2_c64, 64, true,  -2, X86_GREG_xSP, 2 },
        { bs3CpuWeird1_Pop_xSP_Ud2_c64,         64, false, +8, X86_GREG_xSP, 1 },
        { bs3CpuWeird1_Pop_opsize_xSP_Ud2_c64,  64, false, +2, X86_GREG_xSP, 2 },
    };
    BS3TRAPFRAME            TrapCtx;
    BS3TRAPFRAME            TrapExpect;
    BS3REGCTX               Ctx;
    uint8_t const           cTestBits    = BS3_MODE_IS_16BIT_CODE(bTestMode) ? 16
                                         : BS3_MODE_IS_32BIT_CODE(bTestMode) ? 32 : 64;
    uint8_t BS3_FAR        *pbAltStack   = NULL;
    BS3PTRUNION             PtrStack;
    unsigned                i;

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));
    Bs3MemZero(&TrapExpect, sizeof(TrapExpect));

    bs3CpuWeird1_SetGlobals(bTestMode);

    /* Construct a basic context. */
    Bs3RegCtxSaveEx(&Ctx, bTestMode, 1024);
    Ctx.rflags.u32 &= ~X86_EFL_RF;
    if (BS3_MODE_IS_64BIT_CODE(bTestMode))
    {
        Ctx.rbx.au32[1] ^= UINT32_C(0x12305c78);
        Ctx.rcx.au32[1] ^= UINT32_C(0x33447799);
        Ctx.rax.au32[1] ^= UINT32_C(0x9983658a);
        Ctx.r11.au32[1] ^= UINT32_C(0xbbeeffdd);
        Ctx.r12.au32[1] ^= UINT32_C(0x87272728);
    }

    /* ring-3 if possible, since that'll enable automatic stack switching. */
    if (!BS3_MODE_IS_RM_OR_V86(bTestMode))
        Bs3RegCtxConvertToRingX(&Ctx, 3);

    /* Make PtrStack == SS:xSP from Ctx. */
    PtrStack.pv = Bs3RegCtxGetRspSsAsCurPtr(&Ctx);

#if 1
    /* Use our own stack so we can observe the effect of ESP/RSP rolling across
       a 64KB boundrary when just popping SP. */
    if (!BS3_MODE_IS_16BIT_CODE(bTestMode)) /** @todo extend this to 16-bit code as well (except RM ofc). */
    {
        uint32_t uFlatNextSeg;
        pbAltStack = (uint8_t BS3_FAR *)Bs3SlabAllocEx(&g_Bs3Mem4KUpperTiled.Core, 17 /*cPages*/, 0 /*fFlags*/);
        if (!pbAltStack)
        {
            Bs3TestFailed("Failed to allocate 68K for alternative stack!");
            return 1;
        }

        /* Modify RSP to be one byte under the 64KB boundrary. */
        uFlatNextSeg = (Bs3SelPtrToFlat(pbAltStack) + _64K) & ~UINT32_C(0xffff);
        Ctx.rsp.u = uFlatNextSeg - 1;
        //Bs3TestPrintf("uFlatNextSeg=%RX32 rsp=%RX64 ss=%RX16\n", uFlatNextSeg, Ctx.rsp.u, Ctx.ss);

        /* Modify the PtrStack accordingly, using a spare selector for addressing it. */
        Bs3SelSetup16BitData(&Bs3GdteSpare00, uFlatNextSeg - _4K);
        PtrStack.pv = BS3_FP_MAKE(BS3_SEL_SPARE_00 | 3, _4K - 1);
    }
#endif

    /*
     * Iterate the test snippets and run those relevant to the test context.
     */
    for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
    {
        if (s_aTests[i].cBits == cTestBits)
        {
            PBS3REG  const  pReg = &(&Ctx.rax)[s_aTests[i].idxReg];
            unsigned        iRep;          /**< This is to trigger native recompilation. */
            BS3REG          SavedReg;
            BS3REG          SavedRsp;

            /* Save context stuff we may change: */
            SavedReg.u = pReg->u;
            SavedRsp.u = Ctx.rsp.u;

            /* Setup the test context. */
            Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, s_aTests[i].pfnStart);
            if (BS3_MODE_IS_16BIT_SYS(bTestMode))
                g_uBs3TrapEipHint = Ctx.rip.u32;

            if (BS3_MODE_IS_16BIT_CODE(bTestMode))
                Ctx.rsp.u32 |= UINT32_C(0x34560000); /* This part should be ignored, as the stack is also 16-bit. */

            /* The basic expected trap context. */
            TrapExpect.bXcpt  = X86_XCPT_UD;
            Bs3MemCpy(&TrapExpect.Ctx, &Ctx, sizeof(TrapExpect.Ctx));
            TrapExpect.Ctx.rsp.u += s_aTests[i].cbAdjSp;
            TrapExpect.Ctx.rip.u += s_aTests[i].offUd2;
            if (!BS3_MODE_IS_16BIT_SYS(bTestMode))
                TrapExpect.Ctx.rflags.u32 |= X86_EFL_RF;

            g_usBs3TestStep = i;

            if (s_aTests[i].cbAdjSp < 0)
            {
                /*
                 * PUSH
                 */
                RTUINT64U    u64ExpectPushed;
                BS3PTRUNION  PtrStack2;
                PtrStack2.pb = PtrStack.pb + s_aTests[i].cbAdjSp;

                bs3CpuWeird1_PushPopInitStack(PtrStack);
                u64ExpectPushed.u = *PtrStack2.pu64;
                switch (s_aTests[i].cbAdjSp)
                {
                    case -2: u64ExpectPushed.au16[0] = pReg->au16[0]; break;
                    case -4: u64ExpectPushed.au32[0] = pReg->au32[0]; break;
                    case -8: u64ExpectPushed.au64[0] = pReg->u; break;
                }

                for (iRep = 0; iRep < 256; iRep++)
                {
                    bs3CpuWeird1_PushPopInitStack(PtrStack);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (bs3CpuWeird1_ComparePushPop(&TrapCtx, &TrapExpect))
                        break;
                    if (*PtrStack2.pu64 != u64ExpectPushed.u)
                    {
                        Bs3TestFailedF("%u - Unexpected stack value after push: %RX64, expected %RX64",
                                       g_usBs3TestStep, *PtrStack2.pu64, u64ExpectPushed);
                        break;
                    }
                }
            }
            else
            {
                /*
                 * POP.
                 *
                 * This is where it gets interesting.  When popping a partial
                 * SP and the upper part also changes, this is preserved. I.e.
                 * the CPU first writes the updated RSP then the register or
                 * register part that it popped.
                 */
                PBS3REG  const  pExpectReg = &(&TrapExpect.Ctx.rax)[s_aTests[i].idxReg];
                RTUINT64U       u64PopValue;

                bs3CpuWeird1_PushPopInitStack(PtrStack);
                u64PopValue.u = *PtrStack.pu64;
                if (bTestMode != BS3_MODE_RM)
                {
                    /* When in ring-3 we can put whatever we want on the stack, as the UD2 will cause a stack switch. */
                    switch (s_aTests[i].cbAdjSp)
                    {
                        case 2: u64PopValue.au16[0] = ~pReg->au16[0] ^ UINT16_C(0xf394); break;
                        case 4: u64PopValue.au32[0] = ~pReg->au32[0] ^ UINT32_C(0x9e501ab3); break;
                        case 8: u64PopValue.au64[0] = ~pReg->u       ^ UINT64_C(0xbf5fedd520fe9a45); break;
                    }
                }
                else
                {
                    /* In real mode we have to be a little more careful. */
                    if (s_aTests[i].cbAdjSp == 2)
                        u64PopValue.au16[0] = pReg->au16[0] - 382;
                    else
                    {
                        u64PopValue.au16[0] = pReg->au16[0] - 258;
                        u64PopValue.au16[1] = ~pReg->au16[1];
                    }
                }

                switch (s_aTests[i].cbAdjSp)
                {
                    case 2:
                        pExpectReg->au16[0] = u64PopValue.au16[0];
                        break;
                    case 4:
                        pExpectReg->au32[0] = u64PopValue.au32[0];
                        pExpectReg->au32[1] = 0;
                        break;
                    case 8:
                        pExpectReg->u = u64PopValue.u;
                        break;
                }
                //Bs3TestPrintf("iTest=%u/%d: %RX64 -> %RX64\n", i, s_aTests[i].cbAdjSp, pReg->u, pExpectReg->u);

                for (iRep = 0; iRep < 256; iRep++)
                {
                    bs3CpuWeird1_PushPopInitStack(PtrStack);
                    *PtrStack.pu64 = u64PopValue.u;
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (bs3CpuWeird1_ComparePushPop(&TrapCtx, &TrapExpect))
                        break;
                }
            }

            /* Restore context (except cs:rsp): */
            pReg->u   = SavedReg.u;
            Ctx.rsp.u = SavedRsp.u;
        }
    }

    if (pbAltStack)
        Bs3SlabFree(&g_Bs3Mem4KUpperTiled.Core, Bs3SelPtrToFlat(pbAltStack), 17);

    return 0;
}



/*********************************************************************************************************************************
*   PUSH SREG / POP SREG                                                                                                         *
*********************************************************************************************************************************/
PROTO_ALL(bs3CpuWeird1_Push_cs_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_ss_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_ds_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_es_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_fs_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_gs_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_ss_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_ds_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_es_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_fs_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_gs_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_opsize_cs_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_opsize_ss_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_opsize_ds_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_opsize_es_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_opsize_fs_Ud2);
PROTO_ALL(bs3CpuWeird1_Push_opsize_gs_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_opsize_ss_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_opsize_ds_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_opsize_es_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_opsize_fs_Ud2);
PROTO_ALL(bs3CpuWeird1_Pop_opsize_gs_Ud2);


BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuWeird1_PushPopSReg)(uint8_t bTestMode)
{
    static struct
    {
        FPFNBS3FAR  pfnStart;
        uint8_t     cBits;
        bool        fPush;      /**< true if push, false if pop. */
        int8_t      cbAdjSp;    /**< The SP adjustment value. */
        uint8_t     offReg;     /**< The offset of the register in BS3REGCTX. */
        uint8_t     offUd2;     /**< The UD2 offset into the code. */
    } s_aTests[] =
    {
        { bs3CpuWeird1_Push_cs_Ud2_c16,         16, true,  -2, RT_UOFFSETOF(BS3REGCTX, cs), 1 },
        { bs3CpuWeird1_Push_ss_Ud2_c16,         16, true,  -2, RT_UOFFSETOF(BS3REGCTX, ss), 1 },
        { bs3CpuWeird1_Push_ds_Ud2_c16,         16, true,  -2, RT_UOFFSETOF(BS3REGCTX, ds), 1 },
        { bs3CpuWeird1_Push_es_Ud2_c16,         16, true,  -2, RT_UOFFSETOF(BS3REGCTX, es), 1 },
        { bs3CpuWeird1_Push_fs_Ud2_c16,         16, true,  -2, RT_UOFFSETOF(BS3REGCTX, fs), 2 },
        { bs3CpuWeird1_Push_gs_Ud2_c16,         16, true,  -2, RT_UOFFSETOF(BS3REGCTX, gs), 2 },
        { bs3CpuWeird1_Pop_ss_Ud2_c16,          16, false, +2, RT_UOFFSETOF(BS3REGCTX, ss), 1 },
        { bs3CpuWeird1_Pop_ds_Ud2_c16,          16, false, +2, RT_UOFFSETOF(BS3REGCTX, ds), 1 },
        { bs3CpuWeird1_Pop_es_Ud2_c16,          16, false, +2, RT_UOFFSETOF(BS3REGCTX, es), 1 },
        { bs3CpuWeird1_Pop_fs_Ud2_c16,          16, false, +2, RT_UOFFSETOF(BS3REGCTX, fs), 2 },
        { bs3CpuWeird1_Pop_gs_Ud2_c16,          16, false, +2, RT_UOFFSETOF(BS3REGCTX, gs), 2 },
        { bs3CpuWeird1_Push_opsize_cs_Ud2_c16,  16, true,  -4, RT_UOFFSETOF(BS3REGCTX, cs), 2 },
        { bs3CpuWeird1_Push_opsize_ss_Ud2_c16,  16, true,  -4, RT_UOFFSETOF(BS3REGCTX, ss), 2 },
        { bs3CpuWeird1_Push_opsize_ds_Ud2_c16,  16, true,  -4, RT_UOFFSETOF(BS3REGCTX, ds), 2 },
        { bs3CpuWeird1_Push_opsize_es_Ud2_c16,  16, true,  -4, RT_UOFFSETOF(BS3REGCTX, es), 2 },
        { bs3CpuWeird1_Push_opsize_fs_Ud2_c16,  16, true,  -4, RT_UOFFSETOF(BS3REGCTX, fs), 3 },
        { bs3CpuWeird1_Push_opsize_gs_Ud2_c16,  16, true,  -4, RT_UOFFSETOF(BS3REGCTX, gs), 3 },
        { bs3CpuWeird1_Pop_opsize_ss_Ud2_c16,   16, false, +4, RT_UOFFSETOF(BS3REGCTX, ss), 2 },
        { bs3CpuWeird1_Pop_opsize_ds_Ud2_c16,   16, false, +4, RT_UOFFSETOF(BS3REGCTX, ds), 2 },
        { bs3CpuWeird1_Pop_opsize_es_Ud2_c16,   16, false, +4, RT_UOFFSETOF(BS3REGCTX, es), 2 },
        { bs3CpuWeird1_Pop_opsize_fs_Ud2_c16,   16, false, +4, RT_UOFFSETOF(BS3REGCTX, fs), 3 },
        { bs3CpuWeird1_Pop_opsize_gs_Ud2_c16,   16, false, +4, RT_UOFFSETOF(BS3REGCTX, gs), 3 },

        { bs3CpuWeird1_Push_cs_Ud2_c32,         32, true,  -4, RT_UOFFSETOF(BS3REGCTX, cs), 1 },
        { bs3CpuWeird1_Push_ss_Ud2_c32,         32, true,  -4, RT_UOFFSETOF(BS3REGCTX, ss), 1 },
        { bs3CpuWeird1_Push_ds_Ud2_c32,         32, true,  -4, RT_UOFFSETOF(BS3REGCTX, ds), 1 },
        { bs3CpuWeird1_Push_es_Ud2_c32,         32, true,  -4, RT_UOFFSETOF(BS3REGCTX, es), 1 },
        { bs3CpuWeird1_Push_fs_Ud2_c32,         32, true,  -4, RT_UOFFSETOF(BS3REGCTX, fs), 2 },
        { bs3CpuWeird1_Push_gs_Ud2_c32,         32, true,  -4, RT_UOFFSETOF(BS3REGCTX, gs), 2 },
        { bs3CpuWeird1_Pop_ss_Ud2_c32,          32, false, +4, RT_UOFFSETOF(BS3REGCTX, ss), 1 },
        { bs3CpuWeird1_Pop_ds_Ud2_c32,          32, false, +4, RT_UOFFSETOF(BS3REGCTX, ds), 1 },
        { bs3CpuWeird1_Pop_es_Ud2_c32,          32, false, +4, RT_UOFFSETOF(BS3REGCTX, es), 1 },
        { bs3CpuWeird1_Pop_fs_Ud2_c32,          32, false, +4, RT_UOFFSETOF(BS3REGCTX, fs), 2 },
        { bs3CpuWeird1_Pop_gs_Ud2_c32,          32, false, +4, RT_UOFFSETOF(BS3REGCTX, gs), 2 },
        { bs3CpuWeird1_Push_opsize_cs_Ud2_c32,  32, true,  -2, RT_UOFFSETOF(BS3REGCTX, cs), 2 },
        { bs3CpuWeird1_Push_opsize_ss_Ud2_c32,  32, true,  -2, RT_UOFFSETOF(BS3REGCTX, ss), 2 },
        { bs3CpuWeird1_Push_opsize_ds_Ud2_c32,  32, true,  -2, RT_UOFFSETOF(BS3REGCTX, ds), 2 },
        { bs3CpuWeird1_Push_opsize_es_Ud2_c32,  32, true,  -2, RT_UOFFSETOF(BS3REGCTX, es), 2 },
        { bs3CpuWeird1_Push_opsize_fs_Ud2_c32,  32, true,  -2, RT_UOFFSETOF(BS3REGCTX, fs), 3 },
        { bs3CpuWeird1_Push_opsize_gs_Ud2_c32,  32, true,  -2, RT_UOFFSETOF(BS3REGCTX, gs), 3 },
        { bs3CpuWeird1_Pop_opsize_ss_Ud2_c32,   32, false, +2, RT_UOFFSETOF(BS3REGCTX, ss), 2 },
        { bs3CpuWeird1_Pop_opsize_ds_Ud2_c32,   32, false, +2, RT_UOFFSETOF(BS3REGCTX, ds), 2 },
        { bs3CpuWeird1_Pop_opsize_es_Ud2_c32,   32, false, +2, RT_UOFFSETOF(BS3REGCTX, es), 2 },
        { bs3CpuWeird1_Pop_opsize_fs_Ud2_c32,   32, false, +2, RT_UOFFSETOF(BS3REGCTX, fs), 3 },
        { bs3CpuWeird1_Pop_opsize_gs_Ud2_c32,   32, false, +2, RT_UOFFSETOF(BS3REGCTX, gs), 3 },

        { bs3CpuWeird1_Push_fs_Ud2_c64,         64, true,  -8, RT_UOFFSETOF(BS3REGCTX, fs), 2 },
        { bs3CpuWeird1_Push_gs_Ud2_c64,         64, true,  -8, RT_UOFFSETOF(BS3REGCTX, gs), 2 },
        { bs3CpuWeird1_Pop_fs_Ud2_c64,          64, false, +8, RT_UOFFSETOF(BS3REGCTX, fs), 2 },
        { bs3CpuWeird1_Pop_gs_Ud2_c64,          64, false, +8, RT_UOFFSETOF(BS3REGCTX, gs), 2 },
        { bs3CpuWeird1_Push_opsize_fs_Ud2_c64,  64, true,  -2, RT_UOFFSETOF(BS3REGCTX, fs), 3 },
        { bs3CpuWeird1_Push_opsize_gs_Ud2_c64,  64, true,  -2, RT_UOFFSETOF(BS3REGCTX, gs), 3 },
        { bs3CpuWeird1_Pop_opsize_fs_Ud2_c64,   64, false, +2, RT_UOFFSETOF(BS3REGCTX, fs), 3 },
        { bs3CpuWeird1_Pop_opsize_gs_Ud2_c64,   64, false, +2, RT_UOFFSETOF(BS3REGCTX, gs), 3 },
    };
    BS3TRAPFRAME            TrapCtx;
    BS3TRAPFRAME            TrapExpect;
    BS3REGCTX               Ctx;
    uint16_t const          uInitialSel  = bTestMode != BS3_MODE_RM ? BS3_SEL_R3_DS16 : 0x8080;
    uint16_t const          uPopSel      = BS3_SEL_R3_SS16;
    bool const              fFullWrite   = BS3_MODE_IS_64BIT_CODE(bTestMode) /* 64-bit mode writes are full (10980XE). */
                                        || (g_enmCpuVendor = Bs3GetCpuVendor()) != BS3CPUVENDOR_INTEL;
    bool const              fFullRead    = false /* But, 64-bit mode reads are word sized (10980XE). */
                                        || g_enmCpuVendor != BS3CPUVENDOR_INTEL;
    bool const              fInRmWrHiEfl = true /* 10890XE writes EFLAGS[31:16] in the high word of a 'o32 PUSH FS'. */
                                        && !fFullWrite;
    uint8_t const           cTestBits    = BS3_MODE_IS_16BIT_CODE(bTestMode) ? 16
                                         : BS3_MODE_IS_32BIT_CODE(bTestMode) ? 32 : 64;
    unsigned const          cbAltStack   = 2 * X86_PAGE_SIZE;
    uint8_t BS3_FAR        *pbAltStack   = NULL;
    uint32_t                uFlatAltStack;
    uint32_t                uFlatAltStackAlias;
    BS3PTRUNION             PtrStack;
    unsigned                iVariation;

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));
    Bs3MemZero(&TrapExpect, sizeof(TrapExpect));

    bs3CpuWeird1_SetGlobals(bTestMode);

    /* Construct a basic context. */
    Bs3RegCtxSaveEx(&Ctx, bTestMode, 1024);
    Ctx.rflags.u32 &= ~X86_EFL_RF;
    if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
        Ctx.rflags.u32 |= X86_EFL_ID; /* Make sure it's set as it bleeds in in real-mode on my intel 10890XE. */

    if (BS3_MODE_IS_64BIT_CODE(bTestMode))
    {
        Ctx.rbx.au32[1] ^= UINT32_C(0x12305c78);
        Ctx.rcx.au32[1] ^= UINT32_C(0x33447799);
        Ctx.rax.au32[1] ^= UINT32_C(0x9983658a);
        Ctx.r11.au32[1] ^= UINT32_C(0xbbeeffdd);
        Ctx.r12.au32[1] ^= UINT32_C(0x87272728);
    }

    /* ring-3 if possible, since that'll enable automatic stack switching. */
    if (!BS3_MODE_IS_RM_OR_V86(bTestMode))
        Bs3RegCtxConvertToRingX(&Ctx, 3);

    /* Make PtrStack == SS:xSP from Ctx. */
    PtrStack.pv = Bs3RegCtxGetRspSsAsCurPtr(&Ctx);

    /* Use our own stack so we can analyze the PUSH/POP FS behaviour using
       both the SS limit (except 64-bit code) and paging (when enabled).
       Two pages suffices here, but we allocate two more for aliasing the
       first to onto. */
    if (!BS3_MODE_IS_RM_OR_V86(bTestMode)) /** @todo test V86 mode w/ paging */
    {
        pbAltStack = (uint8_t BS3_FAR *)Bs3MemAlloc(BS3MEMKIND_TILED, cbAltStack * 2);
        if (!pbAltStack)
            return !Bs3TestFailed("Failed to allocate 2*2 pages for an alternative stack!");
        uFlatAltStack = Bs3SelPtrToFlat(pbAltStack);
        if (uFlatAltStack & X86_PAGE_OFFSET_MASK)
            return !Bs3TestFailedF("Misaligned allocation: %p / %RX32!", pbAltStack, uFlatAltStack);
    }

    /*
     * The outer loop does setup variations:
     *      - 0: Standard push and pop w/o off default stack w/o any restrictions.
     *      - 1: Apply segment limit as tightly as possible w/o #SS.
     *      - 2: Apply the segment limit too tight and field #SS.
     *      - 3: Put the segment number right next to a page that's not present.
     *           No segment trickery.
     *      - 4: Make the segment number word straddle a page boundrary where
     *           the 2nd page is not present.
     */
    for (iVariation = 0; iVariation <= 4; iVariation++)
    {
        uint16_t const uSavedSs   = Ctx.ss;
        uint64_t const uSavedRsp  = Ctx.rsp.u;
        uint32_t       uNominalEsp;
        unsigned iTest;

        /* Skip variation if not supported by the test mode. */
        if (iVariation >= 1 && BS3_MODE_IS_RM_OR_V86(bTestMode)) /** @todo test V86 mode w/ paging */
            break;

        if ((iVariation == 1 || iVariation == 2) && BS3_MODE_IS_64BIT_CODE(bTestMode))
            continue;
        if ((iVariation == 3 || iVariation == 4) && !BS3_MODE_IS_PAGED(bTestMode))
            continue;

        uFlatAltStackAlias = uFlatAltStack;
        if (iVariation != 0)
        {
            /* Alias the two stack pages for variation #3 and #4 so we can keep
               accessing them via pbAltStack while testing. */
            if (iVariation == 3 || iVariation == 4)
            {
                int rc = Bs3PagingAlias(uFlatAltStackAlias = uFlatAltStack + X86_PAGE_SIZE * 2, uFlatAltStack, X86_PAGE_SIZE,
                                        X86_PTE_P | X86_PTE_RW | X86_PTE_A | X86_PTE_D | X86_PTE_US);
                if (RT_SUCCESS(rc))
                {
                    rc = Bs3PagingAlias(uFlatAltStackAlias + X86_PAGE_SIZE, uFlatAltStack + X86_PAGE_SIZE, X86_PAGE_SIZE, 0);
                    if (RT_FAILURE(rc))
                    {
                        Bs3TestFailedF("Alias of 2nd stack page failed: %d", rc);
                        Bs3PagingUnalias(uFlatAltStackAlias, X86_PAGE_SIZE);
                    }
                }
                else
                    Bs3TestFailedF("Alias of 2nd stack page failed: %d", rc);
                if (RT_FAILURE(rc))
                    break;
            }

            if (iVariation == 1 || iVariation == 2 || BS3_MODE_IS_16BIT_CODE(bTestMode))
            {
                /* Setup a 16-bit stack with two pages and ESP pointing at the last
                   word in the first page.  The SS limit is at 4KB for variation #1
                   (shouldn't fault unless the CPU does full dword writes), one byte
                   lower for variation #2 (must always fault), and max limit for
                   variations #3 and #4. */
                Bs3SelSetup16BitData(&Bs3GdteSpare00, uFlatAltStackAlias);
                if (iVariation <= 2)
                {
                    Bs3GdteSpare00.Gen.u16LimitLow = _4K - 1;
                    if (iVariation == 2)
                        Bs3GdteSpare00.Gen.u16LimitLow -= 1;
                    Bs3GdteSpare00.Gen.u4LimitHigh = 0;
                }
                Ctx.ss     = BS3_SEL_SPARE_00 | 3;
                Ctx.rsp.u  = _4K - sizeof(uint16_t);
            }
            else
            {
                /* Setup flat stack similar to above for variation #3 and #4. */
                Ctx.rsp.u = uFlatAltStackAlias + _4K - sizeof(uint16_t);
            }

            /* Update the stack pointer to match the new ESP. */
            PtrStack.pv = &pbAltStack[_4K - sizeof(uint16_t)];

            /* For variation #4 we move the stack position up by one byte so we'll
               always cross the page boundrary and hit the non-existing page. */
            if (iVariation == 4)
            {
                Ctx.rsp.u   += 1;
                PtrStack.pb += 1;
            }
        }
        uNominalEsp = Ctx.rsp.u32;
        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            if (s_aTests[iTest].cBits == cTestBits)
            {
                uint16_t BS3_FAR   *pRegCtx     = (uint16_t BS3_FAR *)((uint8_t BS3_FAR *)&Ctx            + s_aTests[iTest].offReg);
                uint16_t BS3_FAR   *pRegExpect  = (uint16_t BS3_FAR *)((uint8_t BS3_FAR *)&TrapExpect.Ctx + s_aTests[iTest].offReg);
                uint16_t const      uSavedSel   = *pRegCtx;
                uint8_t const       cbItem      = RT_ABS(s_aTests[iTest].cbAdjSp);
                bool const          fDefaultSel = s_aTests[iTest].offReg == RT_UOFFSETOF(BS3REGCTX, ss)
                                               || s_aTests[iTest].offReg == RT_UOFFSETOF(BS3REGCTX, cs);
                unsigned            iRep;          /**< This is to trigger native recompilation. */
                BS3PTRUNION         PtrStack2;

                if (!fDefaultSel)
                    *pRegCtx = uInitialSel;

                /* Calculate the stack read/write location for this test. PtrStack
                   ASSUMES word writes, so we have to adjust it and RSP if the CPU
                   does full read+writes. */
                PtrStack2.pv = PtrStack.pv;
                if (cbItem != 2 && (s_aTests[iTest].cbAdjSp < 0 ? fFullWrite : fFullRead))
                {
                    PtrStack2.pb -= cbItem - 2;
                    Ctx.rsp.u32  -= cbItem - 2;
                }

                /* Setup the test context. */
                Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, s_aTests[iTest].pfnStart);
                if (BS3_MODE_IS_16BIT_SYS(bTestMode))
                    g_uBs3TrapEipHint = Ctx.rip.u32;

                /* Use the same access location for both PUSH and POP instructions (PtrStack). */
                if (s_aTests[iTest].cbAdjSp < 0)
                    Ctx.rsp.u16 += -s_aTests[iTest].cbAdjSp;

                /* The basic expected trap context. */
                TrapExpect.bXcpt  = iVariation == 2 ? X86_XCPT_SS : iVariation == 4 ? X86_XCPT_PF : X86_XCPT_UD;
                TrapExpect.uErrCd = 0;
                Bs3MemCpy(&TrapExpect.Ctx, &Ctx, sizeof(TrapExpect.Ctx));
                if (TrapExpect.bXcpt == X86_XCPT_UD)
                {
                    TrapExpect.Ctx.rsp.u += s_aTests[iTest].cbAdjSp;
                    TrapExpect.Ctx.rip.u += s_aTests[iTest].offUd2;
                }
                else if (iVariation == 4)
                {
                    TrapExpect.uErrCd    = s_aTests[iTest].cbAdjSp < 0 ? X86_TRAP_PF_RW | X86_TRAP_PF_US : X86_TRAP_PF_US;
                    TrapExpect.Ctx.cr2.u = uFlatAltStackAlias + X86_PAGE_SIZE;
                }
                if (!BS3_MODE_IS_16BIT_SYS(bTestMode))
                    TrapExpect.Ctx.rflags.u32 |= X86_EFL_RF;

                g_usBs3TestStep = iVariation * 1000 + iTest;

                if (s_aTests[iTest].cbAdjSp < 0)
                {
#if 1
                    /*
                     * PUSH
                     */
                    RTUINT64U u64ExpectPushed;

                    bs3CpuWeird1_PushPopInitStack(PtrStack2);
                    u64ExpectPushed.u = *PtrStack2.pu64;
                    if (TrapExpect.bXcpt == X86_XCPT_UD)
                    {
                        u64ExpectPushed.au16[0] = *pRegCtx;
                        if (s_aTests[iTest].cbAdjSp < -2)
                        {
                            if (fFullWrite) /* enable for CPUs that writes more than a word */
                            {
                                u64ExpectPushed.au16[1] = 0;
                                if (s_aTests[iTest].cbAdjSp == -8)
                                    u64ExpectPushed.au32[1] = 0;
                            }
                            /* Intel 10980XE real mode: high word appears to be from EFLAGS. Weird! */
                            else if (bTestMode == BS3_MODE_RM && fInRmWrHiEfl)
                                u64ExpectPushed.au16[1] = Ctx.rflags.au16[1];
                        }
                    }

                    for (iRep = 0; iRep < 256; iRep++)
                    {
                        if (iVariation < 3)
                            bs3CpuWeird1_PushPopInitStack(PtrStack2);
                        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                        if (bs3CpuWeird1_ComparePushPop(&TrapCtx, &TrapExpect))
                            break;

                        if (*PtrStack2.pu64 != u64ExpectPushed.u)
                        {
                            Bs3TestFailedF("%u - Unexpected stack value after push: %RX64, expected %RX64",
                                           g_usBs3TestStep, *PtrStack2.pu64, u64ExpectPushed);
                            break;
                        }
                    }
#endif
                }
                else
                {
#if 1
                    /*
                     * POP.
                     */
                    if (TrapExpect.bXcpt == X86_XCPT_UD)
                        *pRegExpect = !fDefaultSel ? uPopSel : *pRegCtx;

                    for (iRep = 0; iRep < 256; iRep++)
                    {
                        bs3CpuWeird1_PushPopInitStack(PtrStack2);
                        *PtrStack2.pu16 = !fDefaultSel ? uPopSel : *pRegCtx;
                        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                        if (bs3CpuWeird1_ComparePushPop(&TrapCtx, &TrapExpect))
                            break;
                    }
#endif
                }

                /* Restore context (except cs:rip): */
                *pRegCtx    = uSavedSel;
                Ctx.rsp.u32 = uNominalEsp;
            }
        }

        /* Restore original SS:RSP value. */
        Ctx.rsp.u = uSavedRsp;
        Ctx.ss    = uSavedSs;
    }

    if (pbAltStack)
        Bs3MemFree(pbAltStack, cbAltStack);

    return 0;
}



/*********************************************************************************************************************************
*   POPF                                                                                                                         *
*********************************************************************************************************************************/

/**
 * Compares popf result.
 */
static uint8_t bs3CpuWeird1_ComparePopf(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pCtxExpect, uint8_t bXcpt)
{
    uint16_t const cErrorsBefore = Bs3TestSubErrorCount();
    CHECK_MEMBER("bXcpt",       "%#04x",    pTrapCtx->bXcpt,        bXcpt);
    CHECK_MEMBER("bErrCd",      "%#06RX64", pTrapCtx->uErrCd,       0);
    Bs3TestCheckRegCtxEx(&pTrapCtx->Ctx, pCtxExpect, 0 /*cbPcAdjust*/, 0 /*cbSpAdjust*/, 0 /*fExtraEfl*/,
                         g_pszTestMode, g_usBs3TestStep);
    if (Bs3TestSubErrorCount() != cErrorsBefore)
    {
        Bs3TrapPrintFrame(pTrapCtx);
#if 0
        Bs3TestPrintf("Halting in ComparePopf: bXcpt=%#x\n", pTrapCtx->bXcpt);
        ASMHalt();
#endif
        return 1;
    }
    return 0;
}

PROTO_ALL(bs3CpuWeird1_Popf_Nop_Ud2);
PROTO_ALL(bs3CpuWeird1_Popf_opsize_Nop_Ud2);

BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuWeird1_Popf)(uint8_t bTestMode)
{
    static struct
    {
        FPFNBS3FAR  pfnStart;
        uint8_t     cBits;
        int8_t      cbAdjSp;    /**< The SP adjustment value. */
        uint8_t     offNop;     /**< The NOP offset into the code. */
        uint8_t     offUd2;     /**< The UD2 offset into the code. */
    } const s_aTests[] =
    {
        { bs3CpuWeird1_Popf_Nop_Ud2_c16,        16, 2, 1, 2 },
        { bs3CpuWeird1_Popf_opsize_Nop_Ud2_c16, 16, 4, 2, 3 },

        { bs3CpuWeird1_Popf_Nop_Ud2_c32,        32, 4, 1, 2 },
        { bs3CpuWeird1_Popf_opsize_Nop_Ud2_c32, 32, 2, 2, 3 },

        { bs3CpuWeird1_Popf_Nop_Ud2_c64,        64, 8, 1, 2 },
        { bs3CpuWeird1_Popf_opsize_Nop_Ud2_c64, 64, 2, 2, 3 },
    };

    BS3REGCTX               Ctx;
    BS3REGCTX               CtxExpect;
    BS3TRAPFRAME            TrapCtx;
    BS3PTRUNION             PtrStack;
    uint8_t const           cTestBits = BS3_MODE_IS_16BIT_CODE(bTestMode) ? 16 : BS3_MODE_IS_32BIT_CODE(bTestMode) ? 32 : 64;
    uint32_t const          fEflCpu   = X86_EFL_1 | X86_EFL_STATUS_BITS | X86_EFL_DF | X86_EFL_IF | X86_EFL_TF
                                      | ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80286 ? X86_EFL_IOPL | X86_EFL_NT : 0)
                                      | ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386 ? X86_EFL_RF | X86_EFL_VM : 0)
                                      | ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80486 ? X86_EFL_AC | X86_EFL_VIP | X86_EFL_VIF : 0)
                                      | (g_uBs3CpuDetected & BS3CPU_F_CPUID                     ? X86_EFL_ID : 0);
    uint32_t const          fEflClear = X86_EFL_1;
    uint32_t const          fEflSet   = X86_EFL_1 | X86_EFL_STATUS_BITS
                                      | X86_EFL_DF /* safe? */
                                      | X86_EFL_IF /* safe? */
                                      | ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386 ? X86_EFL_RF : 0)
                                      | ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80486 ? X86_EFL_AC | X86_EFL_VIP : 0)
                                      | (g_uBs3CpuDetected & BS3CPU_F_CPUID                     ? X86_EFL_ID : 0);
    uint32_t const          fEflKeep  = X86_EFL_VM;
    uint32_t const          fXcptRf   = BS3_MODE_IS_16BIT_SYS(bTestMode) ? 0 : X86_EFL_RF;
    bool const              fVme      = BS3_MODE_IS_V86(bTestMode)
                                     && (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80486
                                     && (Bs3RegGetCr4() & X86_CR4_VME); /* Can't use Ctx.cr4 here, it's always zero. */
    unsigned                uRing;

    //Bs3TestPrintf("g_uBs3CpuDetected=%#RX16 fEflCpu=%#RX32 fEflSet=%#RX32\n", g_uBs3CpuDetected, fEflCpu, fEflSet);
    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));

    bs3CpuWeird1_SetGlobals(bTestMode);

    /* Construct a basic context. */
    Bs3RegCtxSaveEx(&Ctx, bTestMode, 1024);
    bs3RegCtxScramble(&Ctx, bTestMode);
    Bs3MemCpy(&CtxExpect, &Ctx, sizeof(CtxExpect));

    /* Make PtrStack == SS:xSP from Ctx. */
    PtrStack.pv = Bs3RegCtxGetRspSsAsCurPtr(&Ctx);

    /*
     * The instruction works differently in different rings, so try them all.
     */
    for (uRing = BS3_MODE_IS_V86(bTestMode) ? 3 : 0; ;)
    {
        /*
         * We've got a couple of instruction variations with/without the opsize prefix.
         */
        unsigned iTest;
        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            if (s_aTests[iTest].cBits == cTestBits)
            {
                uint8_t const cbAdjSp = s_aTests[iTest].cbAdjSp;
                unsigned      uIopl;

                /* Setup the test context. */
                Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, s_aTests[iTest].pfnStart);
                if (BS3_MODE_IS_16BIT_SYS(bTestMode))
                    g_uBs3TrapEipHint = Ctx.rip.u32;
                CtxExpect.cs    = Ctx.cs;

                for (uIopl = 0; uIopl <= X86_EFL_IOPL; uIopl += 1 << X86_EFL_IOPL_SHIFT)
                {
                    bool const     fAlwaysUd  = cbAdjSp != 2 && (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) <= BS3CPU_80286;
                    bool const     fAlwaysGp  = uIopl != X86_EFL_IOPL && BS3_MODE_IS_V86(bTestMode) && (cbAdjSp == 4 || !fVme);
                    uint8_t const  bXcptNorm  = fAlwaysGp ? X86_XCPT_GP : X86_XCPT_UD;
                    uint8_t const  offUd      = fAlwaysGp || fAlwaysUd ? 0 : s_aTests[iTest].offUd2;
                    uint32_t const fCleared   = fAlwaysUd ? 0 : fAlwaysGp ? /* 10980xe - go figure: */ X86_EFL_RF
                                              : bTestMode == BS3_MODE_RM
                                              ? X86_EFL_RF | X86_EFL_AC | X86_EFL_VIP /** @todo figure this. iret? bs3kit bug? */
                                              : X86_EFL_RF;
                    uint32_t       fPoppable  = fAlwaysGp || fAlwaysUd ? 0
                                              : uRing == 0                              ? X86_EFL_POPF_BITS
                                              : uRing <= (uIopl >> X86_EFL_IOPL_SHIFT)  ? X86_EFL_POPF_BITS & ~(X86_EFL_IOPL)
                                              : !fAlwaysGp                              ? X86_EFL_POPF_BITS & ~(X86_EFL_IOPL | X86_EFL_IF)
                                              : 0;
                    uint8_t        bXcptExpect;
                    fPoppable &= fEflCpu;
                    if (cbAdjSp == 2)
                        fPoppable &= UINT16_MAX;
                    if (bTestMode == BS3_MODE_RM && (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) == BS3CPU_80286)
                        fPoppable &= ~(X86_EFL_IOPL | X86_EFL_NT);
                    //Bs3TestPrintf("uRing=%u iTest=%u uIopl=%#x fPoppable=%#RX32 fVme=%d cbAdjSp=%d\n", uRing, iTest, uIopl, fPoppable, fVme, cbAdjSp);

                    /* The first steps works with as many EFLAGS bits set as possible. */
                    Ctx.rflags.u32 &= fEflKeep;
                    Ctx.rflags.u32 |= fEflSet | uIopl;
                    CtxExpect.rip.u = Ctx.rip.u + offUd;
                    CtxExpect.rsp.u = fAlwaysGp || fAlwaysUd ? Ctx.rsp.u : Ctx.rsp.u + cbAdjSp;

                    /*
                     * Step 1 - pop zero.
                     */
                    g_usBs3TestStep++;
                    PtrStack.pu32[1]     = UINT32_MAX;
                    if (cbAdjSp >= 4)
                        PtrStack.pu32[0] = 0;
                    else
                    {
                        PtrStack.pu16[1] = UINT16_MAX;
                        PtrStack.pu16[0] = 0;
                    }
                    CtxExpect.rflags.u32 = (Ctx.rflags.u32 & ~fPoppable & ~fCleared) | fXcptRf;
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (bs3CpuWeird1_ComparePopf(&TrapCtx, &CtxExpect, bXcptNorm))
                        break;

                    /*
                     * Step 2 - pop 0xfffffff sans TF.
                     */
                    g_usBs3TestStep++;
                    PtrStack.pu32[1] = UINT32_MAX;
                    PtrStack.pu32[0] = UINT32_MAX & ~X86_EFL_TF;
                    CtxExpect.rflags.u32 = (Ctx.rflags.u32 & ~fPoppable & ~fCleared)
                                         | (UINT32_MAX & ~X86_EFL_TF & fPoppable & ~fCleared)
                                         | fXcptRf;
                    bXcptExpect = bXcptNorm;
                    if (fVme && cbAdjSp == 2 && uIopl != X86_EFL_IOPL)
                    {
                        CtxExpect.rip.u      = Ctx.rip.u;
                        CtxExpect.rsp.u      = Ctx.rsp.u;
                        CtxExpect.rflags.u32 = (Ctx.rflags.u32 & ~fCleared) | fXcptRf;
                        bXcptExpect = X86_XCPT_GP;
                    }
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (bs3CpuWeird1_ComparePopf(&TrapCtx, &CtxExpect, bXcptExpect))
                        break;


                    /* We repeat the steps with max bits cleared before the popf. */
                    Ctx.rflags.u32 &= fEflKeep;
                    Ctx.rflags.u32 |= fEflClear | uIopl;
                    CtxExpect.rip.u = Ctx.rip.u + offUd;
                    CtxExpect.rsp.u = fAlwaysGp || fAlwaysUd ? Ctx.rsp.u : Ctx.rsp.u + cbAdjSp;

                    /*
                     * Step 3 - pop zero.
                     */
                    g_usBs3TestStep++;
                    PtrStack.pu32[1]     = UINT32_MAX;
                    if (cbAdjSp >= 4)
                        PtrStack.pu32[0] = 0;
                    else
                    {
                        PtrStack.pu16[1] = UINT16_MAX;
                        PtrStack.pu16[0] = 0;
                    }
                    CtxExpect.rflags.u32 = (Ctx.rflags.u32 & ~fPoppable & ~fCleared) | fXcptRf;
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (bs3CpuWeird1_ComparePopf(&TrapCtx, &CtxExpect, bXcptNorm))
                        break;

                    /*
                     * Step 4 - pop 0xfffffff sans TF.
                     */
                    g_usBs3TestStep++;
                    PtrStack.pu32[1] = UINT32_MAX;
                    PtrStack.pu32[0] = UINT32_MAX & ~X86_EFL_TF;
                    CtxExpect.rflags.u32 = (Ctx.rflags.u32 & ~fPoppable & ~fCleared)
                                         | (UINT32_MAX & ~X86_EFL_TF & fPoppable & ~fCleared)
                                         | fXcptRf;
                    if (fVme && cbAdjSp == 2 && uIopl != X86_EFL_IOPL)
                        CtxExpect.rflags.u32 |= X86_EFL_VIF;
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (bs3CpuWeird1_ComparePopf(&TrapCtx, &CtxExpect, bXcptNorm))
                        break;

                    /*
                     * Step 5 - Pop TF and get a #DB on the following instruction.
                     */
                    g_usBs3TestStep++;
                    PtrStack.pu32[1] = UINT32_MAX;
                    PtrStack.pu32[0] = UINT32_MAX;
                    if (cbAdjSp != 2 || !fVme || uIopl == X86_EFL_IOPL)
                    {
                        CtxExpect.rflags.u32 = (Ctx.rflags.u32 & ~fPoppable & ~fCleared)
                                             | (UINT32_MAX & fPoppable & ~fCleared)
                                             | fXcptRf;
                        CtxExpect.rip.u      = Ctx.rip.u + offUd;
                        if (CtxExpect.rip.u != Ctx.rip.u)
                        {
                            bXcptExpect = X86_XCPT_DB;
                            CtxExpect.rflags.u32 &= ~X86_EFL_RF;
                        }
                        else
                            bXcptExpect = bXcptNorm;
                    }
                    else
                    {
                        CtxExpect.rflags.u32 = Ctx.rflags.u32 | fXcptRf;
                        CtxExpect.rip.u      = Ctx.rip.u;
                        CtxExpect.rsp.u      = Ctx.rsp.u;
                        PtrStack.pu32[0]     = UINT32_MAX & ~X86_EFL_IF; /* only one trigger */
                        bXcptExpect     = X86_XCPT_GP;
                    }
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (bs3CpuWeird1_ComparePopf(&TrapCtx, &CtxExpect, bXcptExpect))
                        break;

                    /*
                     * Step 6 - Start with TF set and pop zeros.  Should #DB after POPF.
                     */
                    g_usBs3TestStep++;
                    PtrStack.pu32[1]     = UINT32_MAX;
                    if (cbAdjSp >= 4)
                        PtrStack.pu32[0] = 0;
                    else
                    {
                        PtrStack.pu16[1] = UINT16_MAX;
                        PtrStack.pu16[0] = 0;
                    }
                    Ctx.rflags.u32      |= X86_EFL_TF;
                    CtxExpect.rip.u      = Ctx.rip.u + offUd - !!offUd;
                    CtxExpect.rsp.u      = Ctx.rsp.u + (offUd ? cbAdjSp : 0);
                    CtxExpect.rflags.u32 = (Ctx.rflags.u32 & ~fPoppable & ~fCleared) | fXcptRf;
                    if (offUd)
                        CtxExpect.rflags.u32 &= ~X86_EFL_RF;

                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (bs3CpuWeird1_ComparePopf(&TrapCtx, &CtxExpect, offUd ? X86_XCPT_DB : bXcptNorm))
                        break;

                    /** @todo test VIP in VME mode. */
                    g_usBs3TestStep = (g_usBs3TestStep + 9) / 10 * 10;
                }
                g_usBs3TestStep = (g_usBs3TestStep + 99) / 100 * 100;
            }
            g_usBs3TestStep = (g_usBs3TestStep + 999) / 1000 * 1000;
        }

        /*
         * Next ring.
         */
        if (uRing >= 3 || bTestMode == BS3_MODE_RM)
            break;
        uRing++;
        Bs3RegCtxConvertToRingX(&Ctx, uRing);
        CtxExpect.bCpl = Ctx.bCpl;
        CtxExpect.ss   = Ctx.ss;
        CtxExpect.ds   = Ctx.ds;
        CtxExpect.es   = Ctx.es;
        CtxExpect.fs   = Ctx.fs;
        CtxExpect.gs   = Ctx.gs;
        g_usBs3TestStep = (g_usBs3TestStep + 9999) / 10000 * 10000;
    }

    return 0;
}

