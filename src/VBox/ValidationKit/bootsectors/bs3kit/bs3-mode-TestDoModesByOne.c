/* $Id: bs3-mode-TestDoModesByOne.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3TestDoModesByOne
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
#if TMPL_MODE == BS3_MODE_RM
# define BS3_USE_RM_TEXT_SEG 1 /* Real mode version in RMTEXT16 segment to save space. */
# include "bs3kit-template-header.h"
# include "bs3-cmn-test.h"
#else
# include "bs3kit-template-header.h"
# include "bs3-cmn-test.h"
#endif
#include "bs3-mode-TestDoModes.h"


/*********************************************************************************************************************************
*   Assembly Symbols                                                                                                             *
*********************************************************************************************************************************/
/* Assembly helpers for switching to the work bitcount and calling it. */
BS3_DECL_FAR(uint8_t) Bs3TestCallDoerTo16_f16(uint8_t bMode);
BS3_DECL_FAR(uint8_t) Bs3TestCallDoerTo16_c32(uint8_t bMode);
BS3_DECL_FAR(uint8_t) Bs3TestCallDoerTo16_c64(uint8_t bMode);
BS3_DECL_FAR(uint8_t) Bs3TestCallDoerTo32_f16(uint8_t bMode);
BS3_DECL_FAR(uint8_t) Bs3TestCallDoerTo32_c32(uint8_t bMode);
BS3_DECL_FAR(uint8_t) Bs3TestCallDoerTo32_c64(uint8_t bMode);
BS3_DECL_FAR(uint8_t) Bs3TestCallDoerTo64_f16(uint8_t bMode);
BS3_DECL_FAR(uint8_t) Bs3TestCallDoerTo64_c32(uint8_t bMode);
BS3_DECL_FAR(uint8_t) Bs3TestCallDoerTo64_c64(uint8_t bMode);


/** The current worker function, picked up by our assembly helpers. */
#ifndef DOXYGEN_RUNNING
# define g_pfnBs3TestDoModesByOneCurrent BS3_CMN_NM(g_pfnBs3TestDoModesByOneCurrent)
#endif
extern PFNBS3TESTDOMODE g_pfnBs3TestDoModesByOneCurrent;

#include <iprt/asm-amd64-x86.h>


#undef Bs3TestDoModesByOne
BS3_MODE_DEF(void, Bs3TestDoModesByOne,(PCBS3TESTMODEBYONEENTRY paEntries, size_t cEntries, uint32_t fFlags))
{
    bool const      fVerbose         = true;
    bool const      fDoV86Modes      = true;
    bool const      fDoWeirdV86Modes = true;
    uint16_t const  uCpuDetected     = g_uBs3CpuDetected;
    uint8_t const   bCpuType         = uCpuDetected & BS3CPU_TYPE_MASK;
    bool const      fHavePae         = RT_BOOL(uCpuDetected & BS3CPU_F_PAE);
    bool const      fHaveLongMode    = RT_BOOL(uCpuDetected & BS3CPU_F_LONG_MODE);
    unsigned        i;

#if 1 /* debug. */
    Bs3Printf("Bs3TestDoModesByOne: uCpuDetected=%#x fHavePae=%d fHaveLongMode=%d\n", uCpuDetected, fHavePae, fHaveLongMode);
#endif

    /*
     * Inform about modes we won't test (if any).
     */
    if (bCpuType < BS3CPU_80286)
        Bs3Printf("Only executing real-mode tests as no 80286+ CPU was detected.\n");
    else if (bCpuType < BS3CPU_80386)
        Bs3Printf("80286 CPU: Only executing 16-bit protected and real mode tests.\n");
    else if (!fHavePae)
        Bs3Printf("PAE and long mode tests will be skipped.\n");
    else if (!fHaveLongMode)
        Bs3Printf("Long mode tests will be skipped.\n");
#if ARCH_BITS != 16
    Bs3Printf("Real-mode tests will be skipped.\n");
#endif

    /*
     * The real run.
     */
    for (i = 0; i < cEntries; i++)
    {
        const char *pszFmtStr   = "Error #%u (%#x) in %s!\n";
        bool        fSkipped    = true;
        bool const  fOnlyPaging = RT_BOOL((paEntries[i].fFlags | fFlags) & BS3TESTMODEBYONEENTRY_F_ONLY_PAGING);
        bool const  fMinimal    = RT_BOOL((paEntries[i].fFlags | fFlags) & BS3TESTMODEBYONEENTRY_F_MINIMAL);
        bool const  fSkipV8086  = RT_BOOL((paEntries[i].fFlags | fFlags) & BS3TESTMODEBYONEENTRY_F_SKIP_V8086);
        bool const  fCurDoV86Modes      = fDoV86Modes && !fMinimal && !fSkipV8086;
        bool const  fCurDoWeirdV86Modes = fDoWeirdV86Modes && fCurDoV86Modes;
        uint8_t     bErrNo;
        Bs3TestSub(paEntries[i].pszSubTest);

#define PRE_DO_CALL(a_szModeName) do { if (fVerbose) Bs3TestPrintf("...%s\n", a_szModeName); } while (0)
#define CHECK_RESULT(a_szModeName) \
            do { \
                if (bErrNo != BS3TESTDOMODE_SKIPPED) \
                { \
                    /*Bs3Printf("bErrNo=%#x %s\n", bErrNo, a_szModeName);*/ \
                    fSkipped = false; \
                    if (bErrNo != 0) \
                        Bs3TestFailedF(pszFmtStr, bErrNo, bErrNo, a_szModeName); \
                } \
            } while (0)

        g_pfnBs3TestDoModesByOneCurrent = paEntries[i].pfnWorker;

#if ARCH_BITS != 64

# if ARCH_BITS == 16
        if (!fOnlyPaging)
        {
            PRE_DO_CALL(g_szBs3ModeName_rm);
            bErrNo = TMPL_NM(Bs3TestCallDoerInRM)(CONV_TO_RM_FAR16(paEntries[i].pfnWorker));
            CHECK_RESULT(g_szBs3ModeName_rm);
        }
# else
        if (!fOnlyPaging && (paEntries[i].fFlags | fFlags) & BS3TESTMODEBYONEENTRY_F_REAL_MODE_READY)
        {
            PRE_DO_CALL(g_szBs3ModeName_rm);
            bErrNo = TMPL_NM(Bs3TestCallDoerInPE32)(CONV_TO_FLAT(paEntries[i].pfnWorker), BS3_MODE_RM);
            CHECK_RESULT(g_szBs3ModeName_rm);
        }
# endif

        if (bCpuType < BS3CPU_80286)
        {
            if (fSkipped)
                Bs3TestSkipped(NULL);
            continue;
        }

        /*
         * Unpaged prot mode.
         */
        if (!fOnlyPaging && (!fMinimal || bCpuType < BS3CPU_80386))
        {
            PRE_DO_CALL(g_szBs3ModeName_pe16);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPE16)(CONV_TO_PROT_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPE16)(CONV_TO_PROT_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_pe16);
        }
        if (bCpuType < BS3CPU_80386)
        {
            if (fSkipped)
                Bs3TestSkipped(NULL);
            continue;
        }

        if (!fOnlyPaging)
        {
            PRE_DO_CALL(g_szBs3ModeName_pe16_32);
# if ARCH_BITS == 32
            bErrNo = TMPL_NM(Bs3TestCallDoerInPE16_32)(CONV_TO_FLAT(paEntries[i].pfnWorker), BS3_MODE_PE16_32);
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPE16_32)(CONV_TO_FLAT(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_c32)), BS3_MODE_PE16_32);
# endif
            CHECK_RESULT(g_szBs3ModeName_pe16_32);
        }

        if (fCurDoWeirdV86Modes && !fOnlyPaging)
        {
            PRE_DO_CALL(g_szBs3ModeName_pe16_v86);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPE16_V86)(CONV_TO_RM_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPE16_V86)(CONV_TO_RM_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_pe16_v86);
        }

        if (!fOnlyPaging)
        {
            PRE_DO_CALL(g_szBs3ModeName_pe32);
# if ARCH_BITS == 32
            bErrNo = TMPL_NM(Bs3TestCallDoerInPE32)(CONV_TO_FLAT(paEntries[i].pfnWorker), BS3_MODE_PE32);
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPE32)(CONV_TO_FLAT(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_c32)), BS3_MODE_PE32);
# endif
            CHECK_RESULT(g_szBs3ModeName_pe32);
        }

        if (!fOnlyPaging && !fMinimal)
        {
            PRE_DO_CALL(g_szBs3ModeName_pe32_16);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPE32_16)(CONV_TO_PROT_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPE32_16)(CONV_TO_PROT_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_pe32_16);
        }

        if (fCurDoV86Modes && !fOnlyPaging)
        {
            PRE_DO_CALL(g_szBs3ModeName_pev86);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPEV86)(CONV_TO_RM_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPEV86)(CONV_TO_RM_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_pev86);
        }

        /*
         * Paged protected mode.
         */
        if (!fMinimal)
        {
            PRE_DO_CALL(g_szBs3ModeName_pp16);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPP16)(CONV_TO_PROT_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPP16)(CONV_TO_PROT_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_pp16);
        }

        if (!fMinimal)
        {
            PRE_DO_CALL(g_szBs3ModeName_pp16_32);
# if ARCH_BITS == 32
            bErrNo = TMPL_NM(Bs3TestCallDoerInPP16_32)(CONV_TO_FLAT(paEntries[i].pfnWorker), BS3_MODE_PP16_32);
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPP16_32)(CONV_TO_FLAT(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_c32)), BS3_MODE_PP16_32);
# endif
            CHECK_RESULT(g_szBs3ModeName_pp16_32);
        }

        if (fCurDoWeirdV86Modes)
        {
            PRE_DO_CALL(g_szBs3ModeName_pp16_v86);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPP16_V86)(CONV_TO_RM_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPP16_V86)(CONV_TO_RM_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_pp16_v86);
        }

        if (true)
        {
            PRE_DO_CALL(g_szBs3ModeName_pp32);
# if ARCH_BITS == 32
            bErrNo = TMPL_NM(Bs3TestCallDoerInPP32)(CONV_TO_FLAT(paEntries[i].pfnWorker), BS3_MODE_PP32);
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPP32)(CONV_TO_FLAT(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_c32)), BS3_MODE_PP32);
# endif
            CHECK_RESULT(g_szBs3ModeName_pp32);
        }

        if (!fMinimal)
        {
            PRE_DO_CALL(g_szBs3ModeName_pp32_16);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPP32_16)(CONV_TO_PROT_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPP32_16)(CONV_TO_PROT_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_pp32_16);
        }

        if (fCurDoV86Modes)
        {
            PRE_DO_CALL(g_szBs3ModeName_ppv86);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPPV86)(CONV_TO_RM_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPPV86)(CONV_TO_RM_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_ppv86);
        }


        /*
         * Protected mode with PAE paging.
         */
        if (!fMinimal)
        {
            PRE_DO_CALL(g_szBs3ModeName_pae16);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAE16)(CONV_TO_PROT_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAE16)(CONV_TO_PROT_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_pae16);
        }

        if (!fMinimal)
        {
            PRE_DO_CALL(g_szBs3ModeName_pae16_32);
# if ARCH_BITS == 32
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAE16_32)(CONV_TO_FLAT(paEntries[i].pfnWorker), BS3_MODE_PAE16_32);
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAE16_32)(CONV_TO_FLAT(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_c32)), BS3_MODE_PAE16_32);
# endif
            CHECK_RESULT(g_szBs3ModeName_pae16_32);
        }

        if (fCurDoWeirdV86Modes)
        {
            PRE_DO_CALL(g_szBs3ModeName_pae16_v86);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAE16_V86)(CONV_TO_RM_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAE16_V86)(CONV_TO_RM_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_pae16_v86);
        }

        if (true)
        {
            PRE_DO_CALL(g_szBs3ModeName_pae32);
# if ARCH_BITS == 32
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAE32)(CONV_TO_FLAT(paEntries[i].pfnWorker), BS3_MODE_PAE32);
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAE32)(CONV_TO_FLAT(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_c32)), BS3_MODE_PAE32);
# endif
            CHECK_RESULT(g_szBs3ModeName_pae32);
        }

        if (!fMinimal)
        {
            PRE_DO_CALL(g_szBs3ModeName_pae32_16);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAE32_16)(CONV_TO_PROT_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAE32_16)(CONV_TO_PROT_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_pae32_16);
        }

        if (fCurDoV86Modes)
        {
            PRE_DO_CALL(g_szBs3ModeName_paev86);
# if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAEV86)(CONV_TO_RM_FAR16(paEntries[i].pfnWorker));
# else
            bErrNo = TMPL_NM(Bs3TestCallDoerInPAEV86)(CONV_TO_RM_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
# endif
            CHECK_RESULT(g_szBs3ModeName_paev86);
        }

#endif /* ARCH_BITS != 64 */

        /*
         * Long mode.
         */
        if (!fHaveLongMode)
        {
            if (fSkipped)
                Bs3TestSkipped(NULL);
            continue;
        }

        if (!fMinimal)
        {
            PRE_DO_CALL(g_szBs3ModeName_lm16);
#if ARCH_BITS == 16
            bErrNo = TMPL_NM(Bs3TestCallDoerInLM16)(CONV_TO_PROT_FAR16(paEntries[i].pfnWorker));
#else
            bErrNo = TMPL_NM(Bs3TestCallDoerInLM16)(CONV_TO_PROT_FAR16(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_f16)));
#endif
            CHECK_RESULT(g_szBs3ModeName_lm16);
        }

        if (!fMinimal)
        {
            PRE_DO_CALL(g_szBs3ModeName_lm32);
#if ARCH_BITS == 32
            bErrNo = TMPL_NM(Bs3TestCallDoerInLM32)(CONV_TO_FLAT(paEntries[i].pfnWorker));
#else
            bErrNo = TMPL_NM(Bs3TestCallDoerInLM32)(CONV_TO_FLAT(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_c32)));
#endif
            CHECK_RESULT(g_szBs3ModeName_lm32);
        }

        if (true)
        {
            PRE_DO_CALL(g_szBs3ModeName_lm64);
#if ARCH_BITS == 64
            bErrNo = TMPL_NM(Bs3TestCallDoerInLM64)(CONV_TO_FLAT(paEntries[i].pfnWorker), BS3_MODE_LM64);
#else
            bErrNo = TMPL_NM(Bs3TestCallDoerInLM64)(CONV_TO_FLAT(RT_CONCAT3(Bs3TestCallDoerTo,ARCH_BITS,_c64)), BS3_MODE_LM64);
#endif
            CHECK_RESULT(g_szBs3ModeName_lm64);
        }

        if (fSkipped)
            Bs3TestSkipped("skipped\n");
    }
    Bs3TestSubDone();
}

