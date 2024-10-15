/* $Id: bs3-fpustate-1-template.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - bs3-fpustate-1, C code template.
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
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <VBox/VMMDevTesting.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


#ifdef BS3_INSTANTIATING_CMN

/**
 * Displays the differences between the two states.
 */
# define bs3FpuState1_Diff BS3_CMN_NM(bs3FpuState1_Diff)
BS3_DECL_NEAR(void) bs3FpuState1_Diff(X86FXSTATE const BS3_FAR *pExpected, X86FXSTATE const BS3_FAR *pChecking)
{
    unsigned i;

# define CHECK(a_Member, a_Fmt) \
        if (pExpected->a_Member != pChecking->a_Member) \
            Bs3TestPrintf("  " #a_Member ": " a_Fmt ", expected " a_Fmt "\n", pChecking->a_Member, pExpected->a_Member); \
        else do { } while (0)
    CHECK(FCW,          "%#RX16");
    CHECK(FSW,          "%#RX16");
    CHECK(FTW,          "%#RX16");
    CHECK(FOP,          "%#RX16");
    CHECK(FPUIP,        "%#RX32");
    CHECK(CS,           "%#RX16");
    CHECK(Rsrvd1,       "%#RX16");
    CHECK(FPUDP,        "%#RX32");
    CHECK(DS,           "%#RX16");
    CHECK(Rsrvd2,       "%#RX16");
    CHECK(MXCSR,        "%#RX32");
    CHECK(MXCSR_MASK,   "%#RX32");
# undef CHECK
    for (i = 0; i < RT_ELEMENTS(pExpected->aRegs); i++)
        if (   pChecking->aRegs[i].au64[0] != pExpected->aRegs[i].au64[0]
            || pChecking->aRegs[i].au64[1] != pExpected->aRegs[i].au64[1])
            Bs3TestPrintf("st%u: %.16Rhxs\n"
                          "exp: %.16Rhxs\n",
                          i, &pChecking->aRegs[i], &pExpected->aRegs[i]);
    for (i = 0; i < RT_ELEMENTS(pExpected->aXMM); i++)
        if (   pChecking->aXMM[i].au64[0] != pExpected->aXMM[i].au64[0]
            || pChecking->aXMM[i].au64[1] != pExpected->aXMM[i].au64[1])
            Bs3TestPrintf("xmm%u: %.16Rhxs\n"
                          " %sexp: %.16Rhxs\n",
                          i, &pChecking->aRegs[i], &pExpected->aRegs[i], i >= 10 ? " " : "");
}


#endif /* BS3_INSTANTIATING_CMN */


/*
 * Mode specific code.
 * Mode specific code.
 * Mode specific code.
 */
#ifdef BS3_INSTANTIATING_MODE
# if TMPL_MODE == BS3_MODE_PE32 \
  || TMPL_MODE == BS3_MODE_PP32 \
  || TMPL_MODE == BS3_MODE_PAE32 \
  || TMPL_MODE == BS3_MODE_LM64 \
  || TMPL_MODE == BS3_MODE_RM

/* Assembly helpers: */
BS3_DECL_NEAR(void) TMPL_NM(bs3FpuState1_InitState)(X86FXSTATE BS3_FAR *pFxState, void BS3_FAR *pvMmioReg);
BS3_DECL_NEAR(void) TMPL_NM(bs3FpuState1_Restore)(X86FXSTATE const BS3_FAR *pFxState);
BS3_DECL_NEAR(void) TMPL_NM(bs3FpuState1_Save)(X86FXSTATE BS3_FAR *pFxState);

BS3_DECL_NEAR(void) TMPL_NM(bs3FpuState1_FNStEnv)(void BS3_FAR *pvMmioReg);
BS3_DECL_NEAR(void) TMPL_NM(bs3FpuState1_MovDQU_Read)(void BS3_FAR *pvMmioReg, void BS3_FAR *pvResult);
BS3_DECL_NEAR(void) TMPL_NM(bs3FpuState1_MovDQU_Write)(void BS3_FAR *pvMmioReg);
BS3_DECL_NEAR(void) TMPL_NM(bs3FpuState1_MovUPS_Read)(void BS3_FAR *pvMmioReg, void BS3_FAR *pvResult);
BS3_DECL_NEAR(void) TMPL_NM(bs3FpuState1_MovUPS_Write)(void BS3_FAR *pvMmioReg);
BS3_DECL_NEAR(void) TMPL_NM(bs3FpuState1_FMul)(void BS3_FAR *pvMmioReg, void BS3_FAR *pvNoResult);


/**
 * Checks if we're seeing a problem with fnstenv saving zero selectors when
 * running on the compare area.
 *
 * This triggers in NEM mode if the native hypervisor doesn't do a good enough
 * job at save the FPU state for 16-bit and 32-bit guests.  We have heuristics
 * in CPUMInternal.mac (SAVE_32_OR_64_FPU) for this.
 *
 * @returns true if this the zero selector issue.
 * @param   pabReadback The MMIO read buffer containing the fnstenv result
 *                      typically produced by IEM.
 * @param   pabCompare  The buffer containing the fnstenv result typcially
 *                      produced by the CPU itself.
 */
static bool TMPL_NM(bs3FpuState1_IsZeroFnStEnvSelectorsProblem)(const uint8_t BS3_FAR *pabReadback,
                                                                const uint8_t BS3_FAR *pabCompare)
{
    unsigned const offCs = ARCH_BITS == 16 ?  8 : 16;
    unsigned const offDs = ARCH_BITS == 16 ? 12 : 24;
    if (   *(const uint16_t BS3_FAR *)&pabCompare[offCs] == 0
        && *(const uint16_t BS3_FAR *)&pabCompare[offDs] == 0)
    {
        /* Check the stuff before the CS register: */
        if (Bs3MemCmp(pabReadback, pabCompare, offCs) == 0)
        {
            /* Check the stuff between the DS and CS registers:*/
            if (Bs3MemCmp(&pabReadback[offCs + 2], &pabCompare[offCs + 2], offDs - offCs - 2) == 0)
            {
#if ARCH_BITS != 16
                /* Check the stuff after the DS register if 32-bit mode: */
                if (   *(const uint16_t BS3_FAR *)&pabReadback[offDs + 2]
                    == *(const uint16_t BS3_FAR *)&pabCompare[offDs + 2])
#endif
                    return true;
            }
        }
    }
    return false;
}


/**
 * Tests for FPU state corruption.
 *
 * First we don't do anything to quit guest context for a while.
 * Then we start testing weird MMIO accesses, some which amonger other things
 * forces the use of the FPU state or host FPU to do the emulation.  Both are a
 * little complicated in raw-mode and ring-0 contexts.
 *
 * We ASSUME FXSAVE/FXRSTOR support here.
 */
BS3_DECL_FAR(uint8_t) TMPL_NM(bs3FpuState1_Corruption)(uint8_t bMode)
{
    /* We don't need to test that many modes, probably.  */

    uint8_t             abBuf[sizeof(X86FXSTATE)*2 + 32];
    uint8_t BS3_FAR    *pbTmp                   = &abBuf[0x10 - (((uintptr_t)abBuf) & 0x0f)];
    X86FXSTATE BS3_FAR *pExpected               = (X86FXSTATE BS3_FAR *)pbTmp;
    X86FXSTATE BS3_FAR *pChecking               = pExpected + 1;
    uint32_t            iLoop;
    uint32_t            uStartTick;
    bool                fMmioReadback;
    bool                fReadBackError          = false;
    bool                fReadError              = false;
    uint32_t            cFnStEnvSelectorsZero   = 0;
    BS3PTRUNION         MmioReg;
    BS3CPUVENDOR const  enmCpuVendor            = Bs3GetCpuVendor();
    bool const          fSkipStorIdt            = Bs3TestQueryCfgBool(VMMDEV_TESTING_CFG_IS_NEM_LINUX, false);
    bool const          fMayHaveZeroStEnvSels   = Bs3TestQueryCfgBool(VMMDEV_TESTING_CFG_IS_NEM_LINUX, false);
    bool const          fFastFxSaveRestore      = RT_BOOL(ASMCpuId_EDX(0x80000001) & X86_CPUID_AMD_FEATURE_EDX_FFXSR);
    //bool const          fFdpXcptOnly       = (ASMCpuIdEx_EBX(7, 0) & X86_CPUID_STEXT_FEATURE_EBX_FDP_EXCPTN_ONLY)
    //                                      && ASMCpuId_EAX(0) >= 7;
    RT_NOREF(bMode);

    if (fSkipStorIdt)
        Bs3TestPrintf("NEM/linux - skipping SIDT\n");

# undef  CHECK_STATE
# define CHECK_STATE(a_Instr, a_fIsFnStEnv) \
        do { \
            TMPL_NM(bs3FpuState1_Save)(pChecking); \
            if (Bs3MemCmp(pExpected, pChecking, sizeof(*pExpected)) != 0) \
            { \
                Bs3TestFailedF("State differs after " #a_Instr " (write) in loop #%RU32\n", iLoop); \
                bs3FpuState1_Diff(pExpected, pChecking); \
                Bs3PitDisable(); \
                return 1; \
            } \
        } while (0)


    /* Make this code executable in raw-mode.  A bit tricky. */
    ASMSetCR0(ASMGetCR0() | X86_CR0_WP);
    Bs3PitSetupAndEnablePeriodTimer(20);
    ASMIntEnable();
# if ARCH_BITS != 64
    ASMHalt();
# endif

    /* Figure out which MMIO region we'll be using so we can correctly initialize FPUDS. */
# if BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
    MmioReg.pv = BS3_FP_MAKE(VMMDEV_TESTING_MMIO_RM_SEL, VMMDEV_TESTING_MMIO_RM_OFF2(0));
# elif BS3_MODE_IS_16BIT_CODE(TMPL_MODE)
    MmioReg.pv = BS3_FP_MAKE(BS3_SEL_VMMDEV_MMIO16, 0);
# else
    MmioReg.pv = (uint8_t *)VMMDEV_TESTING_MMIO_BASE;
# endif
    if (MmioReg.pu32[VMMDEV_TESTING_MMIO_OFF_NOP / sizeof(uint32_t)] == VMMDEV_TESTING_NOP_RET)
    {
        fMmioReadback = true;
        MmioReg.pb += VMMDEV_TESTING_MMIO_OFF_READBACK;
    }
    else
    {
        Bs3TestPrintf("VMMDev MMIO not found, using VGA instead\n");
        fMmioReadback = false;
        MmioReg.pv = Bs3XptrFlatToCurrent(0xa7800);
    }

    /* Make 100% sure we don't trap accessing the FPU state and that we can use fxsave/fxrstor. */
    g_usBs3TestStep = 1;
    ASMSetCR0((ASMGetCR0() & ~(X86_CR0_TS | X86_CR0_EM)) | X86_CR0_MP);
    ASMSetCR4(ASMGetCR4() | X86_CR4_OSFXSR /*| X86_CR4_OSXMMEEXCPT*/);

    /* Come up with a distinct state. We do that from assembly (will do FPU in R0/RC). */
    g_usBs3TestStep = 2;
    Bs3MemSet(abBuf, 0x42, sizeof(abBuf));
    TMPL_NM(bs3FpuState1_InitState)(pExpected, MmioReg.pb);


    /*
     * Test #1: Check that we can keep it consistent for a while.
     */
    g_usBs3TestStep = 3;
    uStartTick = g_cBs3PitTicks;
    for (iLoop = 0; iLoop < _16M; iLoop++)
    {
        CHECK_STATE(nop, false);
        if (   (iLoop & 0xffff) == 0xffff
            && g_cBs3PitTicks - uStartTick >= 20 * 20) /* 20 seconds*/
            break;
    }

    /*
     * Test #2: Use various FPU, SSE and weird instructions to do MMIO writes.
     *
     * We'll use the VMMDev readback register if possible, but make do
     * with VGA if not configured.
     */
# ifdef __WATCOMC__
#  pragma DISABLE_MESSAGE(201) /* Warning! W201: Unreachable code */
# endif
    g_usBs3TestStep = 4;
    uStartTick = g_cBs3PitTicks;
    for (iLoop = 0; iLoop < _1M; iLoop++)
    {
        unsigned off;
        uint8_t  abCompare[64];
        uint8_t  abReadback[64];

        /* Macros  */
# undef  CHECK_READBACK_WRITE_RUN
# define CHECK_READBACK_WRITE_RUN(a_Instr, a_Worker, a_Type, a_fIsFnStEnv) \
            do { \
                off = (unsigned)(iLoop & (VMMDEV_TESTING_READBACK_SIZE / 2 - 1)); \
                if (off + sizeof(a_Type) > VMMDEV_TESTING_READBACK_SIZE) \
                    off = VMMDEV_TESTING_READBACK_SIZE - sizeof(a_Type); \
                a_Worker((a_Type *)&MmioReg.pb[off]); \
                if (fMmioReadback && (!fReadBackError || iLoop == 0)) \
                { \
                    a_Worker((a_Type *)&abCompare[0]); \
                    Bs3MemCpy(abReadback, &MmioReg.pb[off], sizeof(a_Type)); \
                    if (Bs3MemCmp(abReadback, abCompare, sizeof(a_Type)) == 0) \
                    { /* likely */ } \
                    else if (   (a_fIsFnStEnv) \
                             && fMayHaveZeroStEnvSels \
                             && TMPL_NM(bs3FpuState1_IsZeroFnStEnvSelectorsProblem)(abReadback, abCompare)) \
                        cFnStEnvSelectorsZero += 1; \
                    else \
                    { \
                        Bs3TestFailedF("Read back error for " #a_Instr " in loop #%RU32:\n%.*Rhxs expected:\n%.*Rhxs\n", \
                                       iLoop, sizeof(a_Type), abReadback, sizeof(a_Type), abCompare); \
                        fReadBackError = true; \
                    } \
                } \
            } while (0)

# undef  CHECK_READBACK_WRITE
# define CHECK_READBACK_WRITE(a_Instr, a_Worker, a_Type, a_fIsFnStEnv) \
            CHECK_READBACK_WRITE_RUN(a_Instr, a_Worker, a_Type, a_fIsFnStEnv); \
            CHECK_STATE(a_Instr, a_fIsFnStEnv)
# undef  CHECK_READBACK_WRITE_Z
# define CHECK_READBACK_WRITE_Z(a_Instr, a_Worker, a_Type, a_fIsFnStEnv) \
            do { \
                if (fMmioReadback && (!fReadBackError || iLoop == 0)) \
                { \
                    Bs3MemZero(&abCompare[0], sizeof(a_Type)); \
                    off = (unsigned)(iLoop & (VMMDEV_TESTING_READBACK_SIZE / 2 - 1)); \
                    if (off + sizeof(a_Type) > VMMDEV_TESTING_READBACK_SIZE) \
                        off = VMMDEV_TESTING_READBACK_SIZE - sizeof(a_Type); \
                    Bs3MemZero(&MmioReg.pb[off], sizeof(a_Type)); \
                } \
                CHECK_READBACK_WRITE(a_Instr, a_Worker, a_Type, a_fIsFnStEnv); \
            } while (0)

# undef  CHECK_READBACK_READ_RUN
#define CHECK_READBACK_READ_RUN(a_Instr, a_Worker, a_Type) \
            do { \
                off = (unsigned)(iLoop & (VMMDEV_TESTING_READBACK_SIZE / 2 - 1)); \
                if (off + sizeof(a_Type) > VMMDEV_TESTING_READBACK_SIZE) \
                    off = VMMDEV_TESTING_READBACK_SIZE - sizeof(a_Type); \
                a_Worker((a_Type *)&MmioReg.pb[off], (a_Type *)&abReadback[0]); \
                TMPL_NM(bs3FpuState1_Save)(pChecking); \
            } while (0)
# undef  CHECK_READBACK_READ
# define CHECK_READBACK_READ(a_Instr, a_Worker, a_Type) \
            do { \
                Bs3MemSet(&abReadback[0], 0xcc, sizeof(abReadback)); \
                CHECK_READBACK_READ_RUN(a_Instr, a_Worker, a_Type); \
                CHECK_STATE(a_Instr, false); \
                if (!fReadError || iLoop == 0) \
                { \
                    Bs3MemZero(&abCompare[0], sizeof(abCompare)); \
                    Bs3MemCpy(&abCompare[0], &MmioReg.pb[off], sizeof(a_Type)); \
                    if (Bs3MemCmp(abReadback, abCompare, sizeof(a_Type)) != 0) \
                    { \
                        Bs3TestFailedF("Read result check for " #a_Instr " in loop #%RU32:\n%.*Rhxs expected:\n%.*Rhxs\n", \
                                       iLoop, sizeof(a_Type), abReadback, sizeof(a_Type), abCompare); \
                        fReadError = true; \
                    } \
                } \
            } while (0)

        /* The tests. */
        if (!fSkipStorIdt) /* KVM doesn't advance RIP executing a SIDT [MMIO-memory], it seems. (Linux 5.13.1) */
            CHECK_READBACK_WRITE_Z(SIDT, ASMGetIDTR,                         RTIDTR,       false);
        CHECK_READBACK_WRITE_Z(FNSTENV,  TMPL_NM(bs3FpuState1_FNStEnv),      X86FSTENV32P, true); /** @todo x86.h is missing types */
        CHECK_READBACK_WRITE(  MOVDQU,   TMPL_NM(bs3FpuState1_MovDQU_Write), X86XMMREG,    false);
        CHECK_READBACK_READ(   MOVDQU,   TMPL_NM(bs3FpuState1_MovDQU_Read),  X86XMMREG);
        CHECK_READBACK_WRITE(  MOVUPS,   TMPL_NM(bs3FpuState1_MovUPS_Write), X86XMMREG,    false);
        CHECK_READBACK_READ(   MOVUPS,   TMPL_NM(bs3FpuState1_MovUPS_Read),  X86XMMREG);

        /* Using the FPU is a little complicated, but we really need to check these things. */
        CHECK_READBACK_READ_RUN(FMUL,    TMPL_NM(bs3FpuState1_FMul),         uint64_t);
        if (enmCpuVendor == BS3CPUVENDOR_INTEL)
# if BS3_MODE_IS_16BIT_CODE(TMPL_MODE)
            pExpected->FOP    = 0x040f; // skylake 6700k
# else
            pExpected->FOP    = 0x040b; // skylake 6700k
# endif
        else if (enmCpuVendor == BS3CPUVENDOR_AMD && fFastFxSaveRestore)
            pExpected->FOP    = 0x0000; // Zen2 (3990x)
        else
            pExpected->FOP    = 0x07dc; // dunno where we got this.
# if ARCH_BITS == 64
        pExpected->FPUDP  = (uint32_t) (uintptr_t)&MmioReg.pb[off];
        pExpected->DS     = (uint16_t)((uintptr_t)&MmioReg.pb[off] >> 32);
        pExpected->Rsrvd2 = (uint16_t)((uintptr_t)&MmioReg.pb[off] >> 48);
# elif BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        pExpected->FPUDP  = Bs3SelPtrToFlat(&MmioReg.pb[off]);
# else
        pExpected->FPUDP  = BS3_FP_OFF(&MmioReg.pb[off]);
# endif
        if (enmCpuVendor == BS3CPUVENDOR_AMD && fFastFxSaveRestore)
            pExpected->FPUDP = 0; // Zen2 (3990x)
        CHECK_STATE(FMUL, false);

        /* check for timeout every now an then. */
        if (   (iLoop & 0xfff) == 0xfff
            && g_cBs3PitTicks - uStartTick >= 20 * 20) /* 20 seconds*/
            break;
    }

    Bs3PitDisable();

# ifdef __WATCOMC__
#  pragma ENABLE_MESSAGE(201) /* Warning! W201: Unreachable code */
# endif

    /*
     * Warn if selectors are borked (for real VBox we'll fail and not warn).
     */
    if (cFnStEnvSelectorsZero > 0)
        Bs3TestPrintf("Warning! NEM borked the FPU selectors %u times.\n", cFnStEnvSelectorsZero);
    return 0;
}
# endif
#endif /* BS3_INSTANTIATING_MODE */

