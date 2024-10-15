/* $Id: tstIEMAImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM Assembly Instruction Helper Testcase.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "../include/IEMInternal.h"

#include <iprt/errcore.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/mp.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include <iprt/vfs.h>
#include <iprt/zip.h>
#include <VBox/version.h>

#include "tstIEMAImpl.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define ENTRY_BIN_FIX(a_Name)                   ENTRY_BIN_FIX_EX(a_Name, 0)
#ifdef TSTIEMAIMPL_WITH_GENERATOR
# define ENTRY_BIN_FIX_EX(a_Name, a_uExtra) \
    { RT_XSTR(a_Name), iemAImpl_ ## a_Name, NULL, \
      g_abTests_ ## a_Name, &g_cbTests_ ## a_Name, \
      a_uExtra, IEMTARGETCPU_EFL_BEHAVIOR_NATIVE /* means same for all here */, \
      RT_ELEMENTS(g_aFixedTests_ ## a_Name), g_aFixedTests_ ## a_Name }
#else
# define ENTRY_BIN_FIX_EX(a_Name, a_uExtra)     ENTRY_BIN_EX(a_Name, a_uExtra)
#endif

#define ENTRY_BIN_PFN_CAST(a_Name, a_pfnType)   ENTRY_BIN_PFN_CAST_EX(a_Name, a_pfnType, 0)
#define ENTRY_BIN_PFN_CAST_EX(a_Name, a_pfnType, a_uExtra) \
    { RT_XSTR(a_Name), (a_pfnType)iemAImpl_ ## a_Name, NULL, \
      g_abTests_ ## a_Name, &g_cbTests_ ## a_Name, \
      a_uExtra, IEMTARGETCPU_EFL_BEHAVIOR_NATIVE /* means same for all here */  }

#define ENTRY_BIN(a_Name)                       ENTRY_BIN_EX(a_Name, 0)
#define ENTRY_BIN_EX(a_Name, a_uExtra) \
    { RT_XSTR(a_Name), iemAImpl_ ## a_Name, NULL, \
      g_abTests_ ## a_Name, &g_cbTests_ ## a_Name, \
      a_uExtra, IEMTARGETCPU_EFL_BEHAVIOR_NATIVE /* means same for all here */  }

#define ENTRY_BIN_AVX(a_Name)                   ENTRY_BIN_AVX_EX(a_Name, 0)
#ifndef IEM_WITHOUT_ASSEMBLY
# define ENTRY_BIN_AVX_EX(a_Name, a_uExtra) \
    { RT_XSTR(a_Name), iemAImpl_ ## a_Name, NULL, \
      g_abTests_ ## a_Name, &g_cbTests_ ## a_Name, \
      a_uExtra, IEMTARGETCPU_EFL_BEHAVIOR_NATIVE /* means same for all here */ }
#else
# define ENTRY_BIN_AVX_EX(a_Name, a_uExtra) \
    { RT_XSTR(a_Name), iemAImpl_ ## a_Name ## _fallback, NULL, \
      g_abTests_ ## a_Name, &g_cbTests_ ## a_Name, \
      a_uExtra, IEMTARGETCPU_EFL_BEHAVIOR_NATIVE /* means same for all here */ }
#endif

#define ENTRY_BIN_SSE_OPT(a_Name)               ENTRY_BIN_SSE_OPT_EX(a_Name, 0)
#ifndef IEM_WITHOUT_ASSEMBLY
# define ENTRY_BIN_SSE_OPT_EX(a_Name, a_uExtra) \
    { RT_XSTR(a_Name), iemAImpl_ ## a_Name, NULL, \
      g_abTests_ ## a_Name, &g_cbTests_ ## a_Name, \
      a_uExtra, IEMTARGETCPU_EFL_BEHAVIOR_NATIVE /* means same for all here */ }
#else
# define ENTRY_BIN_SSE_OPT_EX(a_Name, a_uExtra) \
    { RT_XSTR(a_Name), iemAImpl_ ## a_Name ## _fallback, NULL, \
      g_abTests_ ## a_Name, &g_cbTests_ ## a_Name, \
      a_uExtra, IEMTARGETCPU_EFL_BEHAVIOR_NATIVE /* means same for all here */ }
#endif

#define ENTRY_BIN_INTEL(a_Name, a_fEflUndef)    ENTRY_BIN_INTEL_EX(a_Name, a_fEflUndef, 0)
#define ENTRY_BIN_INTEL_EX(a_Name, a_fEflUndef, a_uExtra) \
    { RT_XSTR(a_Name) "_intel", iemAImpl_ ## a_Name ## _intel, iemAImpl_ ## a_Name, \
      g_abTests_ ## a_Name ## _intel, &g_cbTests_ ## a_Name ## _intel, \
      a_uExtra, IEMTARGETCPU_EFL_BEHAVIOR_INTEL  }

#define ENTRY_BIN_AMD(a_Name, a_fEflUndef)      ENTRY_BIN_AMD_EX(a_Name, a_fEflUndef, 0)
#define ENTRY_BIN_AMD_EX(a_Name, a_fEflUndef, a_uExtra) \
    { RT_XSTR(a_Name) "_amd", iemAImpl_ ## a_Name ## _amd,   iemAImpl_ ## a_Name, \
      g_abTests_ ## a_Name ## _amd, &g_cbTests_ ## a_Name ## _amd, \
      a_uExtra, IEMTARGETCPU_EFL_BEHAVIOR_AMD  }

#define ENTRY_BIN_FIX_INTEL(a_Name, a_fEflUndef) ENTRY_BIN_FIX_INTEL_EX(a_Name, a_fEflUndef, 0)
#ifdef TSTIEMAIMPL_WITH_GENERATOR
# define ENTRY_BIN_FIX_INTEL_EX(a_Name, a_fEflUndef, a_uExtra) \
    { RT_XSTR(a_Name) "_intel", iemAImpl_ ## a_Name ## _intel, iemAImpl_ ## a_Name, \
      g_abTests_ ## a_Name ## _intel, &g_cbTests_ ## a_Name ## _intel, \
      a_uExtra, IEMTARGETCPU_EFL_BEHAVIOR_INTEL, \
      RT_ELEMENTS(g_aFixedTests_ ## a_Name), g_aFixedTests_ ## a_Name }
#else
# define ENTRY_BIN_FIX_INTEL_EX(a_Name, a_fEflUndef, a_uExtra)  ENTRY_BIN_INTEL_EX(a_Name, a_fEflUndef, a_uExtra)
#endif

#define ENTRY_BIN_FIX_AMD(a_Name, a_fEflUndef)                  ENTRY_BIN_FIX_AMD_EX(a_Name, a_fEflUndef, 0)
#ifdef TSTIEMAIMPL_WITH_GENERATOR
# define ENTRY_BIN_FIX_AMD_EX(a_Name, a_fEflUndef, a_uExtra) \
    { RT_XSTR(a_Name) "_amd", iemAImpl_ ## a_Name ## _amd,   iemAImpl_ ## a_Name, \
      g_abTests_ ## a_Name ## _amd, &g_cbTests_ ## a_Name ## _amd, \
      a_uExtra, IEMTARGETCPU_EFL_BEHAVIOR_AMD, \
      RT_ELEMENTS(g_aFixedTests_ ## a_Name), g_aFixedTests_ ## a_Name }
#else
# define ENTRY_BIN_FIX_AMD_EX(a_Name, a_fEflUndef, a_uExtra)    ENTRY_BIN_AMD_EX(a_Name, a_fEflUndef, a_uExtra)
#endif


#define TYPEDEF_SUBTEST_TYPE(a_TypeName, a_TestType, a_FunctionPtrType) \
    typedef struct a_TypeName \
    { \
        const char                 *pszName; \
        const a_FunctionPtrType     pfn; \
        const a_FunctionPtrType     pfnNative; \
        void const * const          pvCompressedTests; \
        uint32_t const             *pcbCompressedTests; \
        uint32_t const              uExtra; \
        uint8_t const               idxCpuEflFlavour; \
        uint16_t const              cFixedTests; \
        a_TestType const * const    paFixedTests; \
        a_TestType const           *paTests; /**< The decompressed info. */ \
        uint32_t                    cTests;  /**< The decompressed info. */ \
        IEMTESTENTRYINFO            Info; \
    } a_TypeName

#define COUNT_VARIATIONS(a_SubTest) \
        (1 + ((a_SubTest).idxCpuEflFlavour == g_idxCpuEflFlavour && (a_SubTest).pfnNative) )


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct IEMBINARYHEADER
{
    char        szMagic[16];
    uint32_t    cbEntry;
    uint32_t    uSvnRev;
    uint32_t    auUnused[6];
    char        szCpuDesc[80];
} IEMBINARYHEADER;
AssertCompileSize(IEMBINARYHEADER, 128);

                            // 01234567890123456
#define IEMBINARYHEADER_MAGIC "IEMAImpl Bin v1"
AssertCompile(sizeof(IEMBINARYHEADER_MAGIC) == 16);


typedef struct IEMBINARYFOOTER
{
    char        szMagic[24];
    uint32_t    cbEntry;
    uint32_t    cEntries;
} IEMBINARYFOOTER;
AssertCompileSize(IEMBINARYFOOTER, 32);
                             // 012345678901234567890123
#define IEMBINARYFOOTER_MAGIC "\nIEMAImpl Bin Footer v1"
AssertCompile(sizeof(IEMBINARYFOOTER_MAGIC) == 24);


/** Fixed part of TYPEDEF_SUBTEST_TYPE and friends. */
typedef struct IEMTESTENTRYINFO
{
    void       *pvUncompressed;
    uint32_t    cbUncompressed;
    const char *pszCpuDesc;
    uint32_t    uSvnRev;
} IEMTESTENTRYINFO;


#ifdef TSTIEMAIMPL_WITH_GENERATOR
typedef struct IEMBINARYOUTPUT
{
    /** The output file. */
    RTVFSFILE       hVfsFile;
    /** The stream we write uncompressed binary test data to. */
    RTVFSIOSTREAM   hVfsUncompressed;
    /** The number of bytes written (ignoring write failures). */
    size_t          cbWritten;
    /** The entry size. */
    uint32_t        cbEntry;
    /** Write status. */
    int             rcWrite;
    /** Set if NULL. */
    bool            fNull;
    /** Set if we wrote a header and should write a footer as well. */
    bool            fWroteHeader;
    /** Filename.   */
    char            szFilename[94];
} IEMBINARYOUTPUT;
typedef IEMBINARYOUTPUT *PIEMBINARYOUTPUT;
#endif /* TSTIEMAIMPL_WITH_GENERATOR */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST       g_hTest;
static uint8_t      g_idxCpuEflFlavour = IEMTARGETCPU_EFL_BEHAVIOR_INTEL;
#ifdef TSTIEMAIMPL_WITH_GENERATOR
static uint32_t     g_cZeroDstTests = 2;
static uint32_t     g_cZeroSrcTests = 4;
#endif
static uint8_t     *g_pu8,   *g_pu8Two;
static uint16_t    *g_pu16,  *g_pu16Two;
static uint32_t    *g_pu32,  *g_pu32Two,  *g_pfEfl;
static uint64_t    *g_pu64,  *g_pu64Two;
static RTUINT128U  *g_pu128, *g_pu128Two;

static char         g_aszBuf[32][256];
static unsigned     g_idxBuf = 0;

static uint32_t     g_cIncludeTestPatterns;
static uint32_t     g_cExcludeTestPatterns;
static const char  *g_apszIncludeTestPatterns[64];
static const char  *g_apszExcludeTestPatterns[64];

/** Higher value, means longer benchmarking. */
static uint64_t     g_cPicoSecBenchmark = 0;

static unsigned     g_cVerbosity = 0;
static bool         g_fVerboseSkipping = true;


#ifdef TSTIEMAIMPL_WITH_GENERATOR
/** The SVN revision (for use in the binary headers). */
static uint32_t     g_uSvnRev = 0;
/** The CPU description (for use in the binary headers). */
static char         g_szCpuDesc[80] = "";
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static const char *FormatR80(PCRTFLOAT80U pr80);
static const char *FormatR64(PCRTFLOAT64U pr64);
static const char *FormatR32(PCRTFLOAT32U pr32);


/*
 * Random helpers.
 */

static uint32_t RandEFlags(void)
{
    uint32_t fEfl = RTRandU32();
    return (fEfl & X86_EFL_LIVE_MASK) | X86_EFL_RA1_MASK;
}

#ifdef TSTIEMAIMPL_WITH_GENERATOR

static uint8_t  RandU8(void)
{
    return RTRandU32Ex(0, 0xff);
}


static uint16_t  RandU16(void)
{
    return RTRandU32Ex(0, 0xffff);
}


static uint32_t  RandU32(void)
{
    return RTRandU32();
}

#endif

static uint64_t  RandU64(void)
{
    return RTRandU64();
}


static RTUINT128U RandU128(void)
{
    RTUINT128U Ret;
    Ret.s.Hi = RTRandU64();
    Ret.s.Lo = RTRandU64();
    return Ret;
}

#ifdef TSTIEMAIMPL_WITH_GENERATOR

static uint8_t  RandU8Dst(uint32_t iTest)
{
    if (iTest < g_cZeroDstTests)
        return 0;
    return RandU8();
}


static uint8_t  RandU8Src(uint32_t iTest)
{
    if (iTest < g_cZeroSrcTests)
        return 0;
    return RandU8();
}


static uint16_t  RandU16Dst(uint32_t iTest)
{
    if (iTest < g_cZeroDstTests)
        return 0;
    return RandU16();
}


static uint16_t  RandU16Src(uint32_t iTest)
{
    if (iTest < g_cZeroSrcTests)
        return 0;
    return RandU16();
}


static uint32_t  RandU32Dst(uint32_t iTest)
{
    if (iTest < g_cZeroDstTests)
        return 0;
    return RandU32();
}


static uint32_t  RandU32Src(uint32_t iTest)
{
    if (iTest < g_cZeroSrcTests)
        return 0;
    return RandU32();
}


static uint64_t  RandU64Dst(uint32_t iTest)
{
    if (iTest < g_cZeroDstTests)
        return 0;
    return RandU64();
}


static uint64_t  RandU64Src(uint32_t iTest)
{
    if (iTest < g_cZeroSrcTests)
        return 0;
    return RandU64();
}


/** 2nd operand for and FPU instruction, pairing with RandR80Src1. */
static int16_t  RandI16Src2(uint32_t iTest)
{
    if (iTest < 18 * 4)
        switch (iTest % 4)
        {
            case 0: return 0;
            case 1: return INT16_MAX;
            case 2: return INT16_MIN;
            case 3: break;
        }
    return (int16_t)RandU16();
}


/** 2nd operand for and FPU instruction, pairing with RandR80Src1. */
static int32_t  RandI32Src2(uint32_t iTest)
{
    if (iTest < 18 * 4)
        switch (iTest % 4)
        {
            case 0: return 0;
            case 1: return INT32_MAX;
            case 2: return INT32_MIN;
            case 3: break;
        }
    return (int32_t)RandU32();
}


static int64_t  RandI64Src(uint32_t iTest)
{
    RT_NOREF(iTest);
    return (int64_t)RandU64();
}


static uint16_t  RandFcw(void)
{
    return RandU16() & ~X86_FCW_ZERO_MASK;
}


static uint16_t  RandFsw(void)
{
    AssertCompile((X86_FSW_C_MASK | X86_FSW_XCPT_ES_MASK | X86_FSW_TOP_MASK | X86_FSW_B) == 0xffff);
    return RandU16();
}


static uint32_t  RandMxcsr(void)
{
    return RandU32() & ~X86_MXCSR_ZERO_MASK;
}


static void SafeR80FractionShift(PRTFLOAT80U pr80, uint8_t cShift)
{
    if (pr80->sj64.uFraction >= RT_BIT_64(cShift))
        pr80->sj64.uFraction >>= cShift;
    else
        pr80->sj64.uFraction = (cShift % 19) + 1;
}



static RTFLOAT80U RandR80Ex(uint8_t bType, unsigned cTarget = 80, bool fIntTarget = false)
{
    Assert(cTarget == (!fIntTarget ? 80U : 16U) || cTarget == 64U || cTarget == 32U || (cTarget == 59U && fIntTarget));

    RTFLOAT80U r80;
    r80.au64[0] = RandU64();
    r80.au16[4] = RandU16();

    /*
     * Adjust the random stuff according to bType.
     */
    bType &= 0x1f;
    if (bType == 0 || bType == 1 || bType == 2 || bType == 3)
    {
        /* Zero (0), Pseudo-Infinity (1), Infinity (2), Indefinite (3). We only keep fSign here. */
        r80.sj64.uExponent     = bType == 0 ? 0 : 0x7fff;
        r80.sj64.uFraction     = bType <= 2 ? 0 : RT_BIT_64(62);
        r80.sj64.fInteger      = bType >= 2 ? 1 : 0;
        AssertMsg(bType != 0 || RTFLOAT80U_IS_ZERO(&r80),       ("%s\n", FormatR80(&r80)));
        AssertMsg(bType != 1 || RTFLOAT80U_IS_PSEUDO_INF(&r80), ("%s\n", FormatR80(&r80)));
        Assert(   bType != 1 || RTFLOAT80U_IS_387_INVALID(&r80));
        AssertMsg(bType != 2 || RTFLOAT80U_IS_INF(&r80),        ("%s\n", FormatR80(&r80)));
        AssertMsg(bType != 3 || RTFLOAT80U_IS_INDEFINITE(&r80), ("%s\n", FormatR80(&r80)));
    }
    else if (bType == 4 || bType == 5 || bType == 6 || bType == 7)
    {
        /* Denormals (4,5) and Pseudo denormals (6,7) */
        if (bType & 1)
            SafeR80FractionShift(&r80, r80.sj64.uExponent % 62);
        else if (r80.sj64.uFraction == 0 && bType < 6)
            r80.sj64.uFraction = RTRandU64Ex(1, RT_BIT_64(RTFLOAT80U_FRACTION_BITS) - 1);
        r80.sj64.uExponent = 0;
        r80.sj64.fInteger  = bType >= 6;
        AssertMsg(bType >= 6 || RTFLOAT80U_IS_DENORMAL(&r80),        ("%s bType=%#x\n", FormatR80(&r80), bType));
        AssertMsg(bType < 6  || RTFLOAT80U_IS_PSEUDO_DENORMAL(&r80), ("%s bType=%#x\n", FormatR80(&r80), bType));
    }
    else if (bType == 8 || bType == 9)
    {
        /* Pseudo NaN. */
        if (bType & 1)
            SafeR80FractionShift(&r80, r80.sj64.uExponent % 62);
        else if (r80.sj64.uFraction == 0 && !r80.sj64.fInteger)
            r80.sj64.uFraction = RTRandU64Ex(1, RT_BIT_64(RTFLOAT80U_FRACTION_BITS) - 1);
        r80.sj64.uExponent = 0x7fff;
        if (r80.sj64.fInteger)
            r80.sj64.uFraction |= RT_BIT_64(62);
        else
            r80.sj64.uFraction &= ~RT_BIT_64(62);
        r80.sj64.fInteger  = 0;
        AssertMsg(RTFLOAT80U_IS_PSEUDO_NAN(&r80), ("%s bType=%#x\n", FormatR80(&r80), bType));
        AssertMsg(RTFLOAT80U_IS_NAN(&r80),        ("%s bType=%#x\n", FormatR80(&r80), bType));
        Assert(RTFLOAT80U_IS_387_INVALID(&r80));
    }
    else if (bType == 10 || bType == 11 || bType == 12 || bType == 13)
    {
        /* Quiet and signalling NaNs. */
        if (bType & 1)
            SafeR80FractionShift(&r80, r80.sj64.uExponent % 62);
        else if (r80.sj64.uFraction == 0)
            r80.sj64.uFraction = RTRandU64Ex(1, RT_BIT_64(RTFLOAT80U_FRACTION_BITS) - 1);
        r80.sj64.uExponent = 0x7fff;
        if (bType < 12)
            r80.sj64.uFraction |= RT_BIT_64(62);  /* quiet */
        else
            r80.sj64.uFraction &= ~RT_BIT_64(62); /* signaling */
        r80.sj64.fInteger  = 1;
        AssertMsg(bType >= 12 || RTFLOAT80U_IS_QUIET_NAN(&r80), ("%s\n", FormatR80(&r80)));
        AssertMsg(bType <  12 || RTFLOAT80U_IS_SIGNALLING_NAN(&r80), ("%s\n", FormatR80(&r80)));
        AssertMsg(RTFLOAT80U_IS_SIGNALLING_NAN(&r80) || RTFLOAT80U_IS_QUIET_NAN(&r80), ("%s\n", FormatR80(&r80)));
        AssertMsg(RTFLOAT80U_IS_QUIET_OR_SIGNALLING_NAN(&r80), ("%s\n", FormatR80(&r80)));
        AssertMsg(RTFLOAT80U_IS_NAN(&r80), ("%s\n", FormatR80(&r80)));
    }
    else if (bType == 14 || bType == 15)
    {
        /* Unnormals */
        if (bType & 1)
            SafeR80FractionShift(&r80, RandU8() % 62);
        r80.sj64.fInteger  = 0;
        if (r80.sj64.uExponent == RTFLOAT80U_EXP_MAX || r80.sj64.uExponent == 0)
            r80.sj64.uExponent = (uint16_t)RTRandU32Ex(1, RTFLOAT80U_EXP_MAX - 1);
        AssertMsg(RTFLOAT80U_IS_UNNORMAL(&r80), ("%s\n", FormatR80(&r80)));
        Assert(RTFLOAT80U_IS_387_INVALID(&r80));
    }
    else if (bType < 26)
    {
        /* Make sure we have lots of normalized values. */
        if (!fIntTarget)
        {
            const unsigned uMinExp = cTarget == 64 ? RTFLOAT80U_EXP_BIAS - RTFLOAT64U_EXP_BIAS
                                   : cTarget == 32 ? RTFLOAT80U_EXP_BIAS - RTFLOAT32U_EXP_BIAS : 0;
            const unsigned uMaxExp = cTarget == 64 ? uMinExp + RTFLOAT64U_EXP_MAX
                                   : cTarget == 32 ? uMinExp + RTFLOAT32U_EXP_MAX              : RTFLOAT80U_EXP_MAX;
            r80.sj64.fInteger = 1;
            if (r80.sj64.uExponent <= uMinExp)
                r80.sj64.uExponent = uMinExp + 1;
            else if (r80.sj64.uExponent >= uMaxExp)
                r80.sj64.uExponent = uMaxExp - 1;

            if (bType == 16)
            {   /* All 1s is useful to testing rounding. Also try trigger special
                   behaviour by sometimes rounding out of range, while we're at it. */
                r80.sj64.uFraction = RT_BIT_64(63) - 1;
                uint8_t bExp = RandU8();
                if ((bExp & 3) == 0)
                    r80.sj64.uExponent = uMaxExp - 1;
                else if ((bExp & 3) == 1)
                    r80.sj64.uExponent = uMinExp + 1;
                else if ((bExp & 3) == 2)
                    r80.sj64.uExponent = uMinExp - (bExp & 15); /* (small numbers are mapped to subnormal values) */
            }
        }
        else
        {
            /* integer target: */
            const unsigned uMinExp = RTFLOAT80U_EXP_BIAS;
            const unsigned uMaxExp = RTFLOAT80U_EXP_BIAS + cTarget - 2;
            r80.sj64.fInteger = 1;
            if (r80.sj64.uExponent < uMinExp)
                r80.sj64.uExponent = uMinExp;
            else if (r80.sj64.uExponent > uMaxExp)
                r80.sj64.uExponent = uMaxExp;

            if (bType == 16)
            {   /* All 1s is useful to testing rounding. Also try trigger special
                   behaviour by sometimes rounding out of range, while we're at it. */
                r80.sj64.uFraction = RT_BIT_64(63) - 1;
                uint8_t bExp = RandU8();
                if ((bExp & 3) == 0)
                    r80.sj64.uExponent = uMaxExp;
                else if ((bExp & 3) == 1)
                    r80.sj64.uFraction &= ~(RT_BIT_64(cTarget - 1 - r80.sj64.uExponent) - 1); /* no rounding */
            }
        }

        AssertMsg(RTFLOAT80U_IS_NORMAL(&r80), ("%s\n", FormatR80(&r80)));
    }
    return r80;
}


static RTFLOAT80U RandR80(unsigned cTarget = 80, bool fIntTarget = false)
{
    /*
     * Make it more likely that we get a good selection of special values.
     */
    return RandR80Ex(RandU8(), cTarget, fIntTarget);

}


static RTFLOAT80U RandR80Src(uint32_t iTest, unsigned cTarget = 80, bool fIntTarget = false)
{
    /* Make sure we cover all the basic types first before going for random selection: */
    if (iTest <= 18)
        return RandR80Ex(18 - iTest, cTarget, fIntTarget); /* Starting with 3 normals. */
    return RandR80(cTarget, fIntTarget);
}


/**
 * Helper for RandR80Src1 and RandR80Src2 that converts bType from a 0..11 range
 * to a 0..17, covering all basic value types.
 */
static uint8_t RandR80Src12RemapType(uint8_t bType)
{
    switch (bType)
    {
        case 0:  return 18; /* normal */
        case 1:  return 16; /* normal extreme rounding */
        case 2:  return 14; /* unnormal */
        case 3:  return 12; /* Signalling NaN */
        case 4:  return 10; /* Quiet NaN */
        case 5:  return 8;  /* PseudoNaN */
        case 6:  return 6;  /* Pseudo Denormal */
        case 7:  return 4;  /* Denormal */
        case 8:  return 3;  /* Indefinite */
        case 9:  return 2;  /* Infinity */
        case 10: return 1;  /* Pseudo-Infinity */
        case 11: return 0;  /* Zero */
        default: AssertFailedReturn(18);
    }
}


/**
 * This works in tandem with RandR80Src2 to make sure we cover all operand
 * type mixes first before we venture into regular random testing.
 *
 * There are 11 basic variations, when we leave out the five odd ones using
 * SafeR80FractionShift. Because of the special normalized value targetting at
 * rounding, we make it an even 12.  So 144 combinations for two operands.
 */
static RTFLOAT80U RandR80Src1(uint32_t iTest, unsigned cPartnerBits = 80, bool fPartnerInt = false)
{
    if (cPartnerBits == 80)
    {
        Assert(!fPartnerInt);
        if (iTest < 12 * 12)
            return RandR80Ex(RandR80Src12RemapType(iTest / 12));
    }
    else if ((cPartnerBits == 64 || cPartnerBits == 32) && !fPartnerInt)
    {
        if (iTest < 12 * 10)
            return RandR80Ex(RandR80Src12RemapType(iTest / 10));
    }
    else if (iTest < 18 * 4 && fPartnerInt)
        return RandR80Ex(iTest / 4);
    return RandR80();
}


/** Partner to RandR80Src1. */
static RTFLOAT80U RandR80Src2(uint32_t iTest)
{
    if (iTest < 12 * 12)
        return RandR80Ex(RandR80Src12RemapType(iTest % 12));
    return RandR80();
}


static void SafeR64FractionShift(PRTFLOAT64U pr64, uint8_t cShift)
{
    if (pr64->s64.uFraction >= RT_BIT_64(cShift))
        pr64->s64.uFraction >>= cShift;
    else
        pr64->s64.uFraction = (cShift % 19) + 1;
}


static RTFLOAT64U RandR64Ex(uint8_t bType)
{
    RTFLOAT64U r64;
    r64.u = RandU64();

    /*
     * Make it more likely that we get a good selection of special values.
     * On average 6 out of 16 calls should return a special value.
     */
    bType &= 0xf;
    if (bType == 0 || bType == 1)
    {
        /* 0 or Infinity. We only keep fSign here. */
        r64.s.uExponent     = bType == 0 ? 0 : 0x7ff;
        r64.s.uFractionHigh = 0;
        r64.s.uFractionLow  = 0;
        AssertMsg(bType != 0 || RTFLOAT64U_IS_ZERO(&r64), ("%s bType=%#x\n", FormatR64(&r64), bType));
        AssertMsg(bType != 1 || RTFLOAT64U_IS_INF(&r64),  ("%s bType=%#x\n", FormatR64(&r64), bType));
    }
    else if (bType == 2 || bType == 3)
    {
        /* Subnormals */
        if (bType == 3)
            SafeR64FractionShift(&r64, r64.s64.uExponent % 51);
        else if (r64.s64.uFraction == 0)
            r64.s64.uFraction = RTRandU64Ex(1, RT_BIT_64(RTFLOAT64U_FRACTION_BITS) - 1);
        r64.s64.uExponent = 0;
        AssertMsg(RTFLOAT64U_IS_SUBNORMAL(&r64), ("%s bType=%#x\n", FormatR64(&r64), bType));
    }
    else if (bType == 4 || bType == 5 || bType == 6 || bType == 7)
    {
        /* NaNs */
        if (bType & 1)
            SafeR64FractionShift(&r64, r64.s64.uExponent % 51);
        else if (r64.s64.uFraction == 0)
            r64.s64.uFraction = RTRandU64Ex(1, RT_BIT_64(RTFLOAT64U_FRACTION_BITS) - 1);
        r64.s64.uExponent = 0x7ff;
        if (bType < 6)
            r64.s64.uFraction |= RT_BIT_64(RTFLOAT64U_FRACTION_BITS - 1); /* quiet */
        else
            r64.s64.uFraction &= ~RT_BIT_64(RTFLOAT64U_FRACTION_BITS - 1); /* signalling */
        AssertMsg(bType >= 6 || RTFLOAT64U_IS_QUIET_NAN(&r64),      ("%s bType=%#x\n", FormatR64(&r64), bType));
        AssertMsg(bType <  6 || RTFLOAT64U_IS_SIGNALLING_NAN(&r64), ("%s bType=%#x\n", FormatR64(&r64), bType));
        AssertMsg(RTFLOAT64U_IS_NAN(&r64), ("%s bType=%#x\n", FormatR64(&r64), bType));
    }
    else if (bType < 12)
    {
        /* Make sure we have lots of normalized values. */
        if (r64.s.uExponent == 0)
            r64.s.uExponent = 1;
        else if (r64.s.uExponent == 0x7ff)
            r64.s.uExponent = 0x7fe;
        AssertMsg(RTFLOAT64U_IS_NORMAL(&r64), ("%s bType=%#x\n", FormatR64(&r64), bType));
    }
    return r64;
}


static RTFLOAT64U RandR64Src(uint32_t iTest)
{
    if (iTest < 16)
        return RandR64Ex(iTest);
    return RandR64Ex(RandU8());
}


/** Pairing with a 80-bit floating point arg. */
static RTFLOAT64U RandR64Src2(uint32_t iTest)
{
    if (iTest < 12 * 10)
        return RandR64Ex(9 - iTest % 10); /* start with normal values */
    return RandR64Ex(RandU8());
}


static void SafeR32FractionShift(PRTFLOAT32U pr32, uint8_t cShift)
{
    if (pr32->s.uFraction >= RT_BIT_32(cShift))
        pr32->s.uFraction >>= cShift;
    else
        pr32->s.uFraction = (cShift % 19) + 1;
}


static RTFLOAT32U RandR32Ex(uint8_t bType)
{
    RTFLOAT32U r32;
    r32.u = RandU32();

    /*
     * Make it more likely that we get a good selection of special values.
     * On average 6 out of 16 calls should return a special value.
     */
    bType &= 0xf;
    if (bType == 0 || bType == 1)
    {
        /* 0 or Infinity. We only keep fSign here. */
        r32.s.uExponent = bType == 0 ? 0 : 0xff;
        r32.s.uFraction = 0;
        AssertMsg(bType != 0 || RTFLOAT32U_IS_ZERO(&r32), ("%s\n", FormatR32(&r32)));
        AssertMsg(bType != 1 || RTFLOAT32U_IS_INF(&r32), ("%s\n", FormatR32(&r32)));
    }
    else if (bType == 2 || bType == 3)
    {
        /* Subnormals */
        if (bType == 3)
            SafeR32FractionShift(&r32, r32.s.uExponent % 22);
        else if (r32.s.uFraction == 0)
            r32.s.uFraction = RTRandU32Ex(1, RT_BIT_32(RTFLOAT32U_FRACTION_BITS) - 1);
        r32.s.uExponent = 0;
        AssertMsg(RTFLOAT32U_IS_SUBNORMAL(&r32), ("%s bType=%#x\n", FormatR32(&r32), bType));
    }
    else if (bType == 4 || bType == 5 || bType == 6 || bType == 7)
    {
        /* NaNs */
        if (bType & 1)
            SafeR32FractionShift(&r32, r32.s.uExponent % 22);
        else if (r32.s.uFraction == 0)
            r32.s.uFraction = RTRandU32Ex(1, RT_BIT_32(RTFLOAT32U_FRACTION_BITS) - 1);
        r32.s.uExponent = 0xff;
        if (bType < 6)
            r32.s.uFraction |= RT_BIT_32(RTFLOAT32U_FRACTION_BITS - 1);  /* quiet */
        else
            r32.s.uFraction &= ~RT_BIT_32(RTFLOAT32U_FRACTION_BITS - 1); /* signalling */
        AssertMsg(bType >= 6 || RTFLOAT32U_IS_QUIET_NAN(&r32),      ("%s bType=%#x\n", FormatR32(&r32), bType));
        AssertMsg(bType <  6 || RTFLOAT32U_IS_SIGNALLING_NAN(&r32), ("%s bType=%#x\n", FormatR32(&r32), bType));
        AssertMsg(RTFLOAT32U_IS_NAN(&r32), ("%s bType=%#x\n", FormatR32(&r32), bType));
    }
    else if (bType < 12)
    {
        /* Make sure we have lots of normalized values. */
        if (r32.s.uExponent == 0)
            r32.s.uExponent = 1;
        else if (r32.s.uExponent == 0xff)
            r32.s.uExponent = 0xfe;
        AssertMsg(RTFLOAT32U_IS_NORMAL(&r32), ("%s bType=%#x\n", FormatR32(&r32), bType));
    }
    return r32;
}


static RTFLOAT32U RandR32Src(uint32_t iTest)
{
    if (iTest < 16)
        return RandR32Ex(iTest);
    return RandR32Ex(RandU8());
}


/** Pairing with a 80-bit floating point arg. */
static RTFLOAT32U RandR32Src2(uint32_t iTest)
{
    if (iTest < 12 * 10)
        return RandR32Ex(9 - iTest % 10); /* start with normal values */
    return RandR32Ex(RandU8());
}


static RTPBCD80U RandD80Src(uint32_t iTest)
{
    if (iTest < 3)
    {
        RTPBCD80U d80Zero = RTPBCD80U_INIT_ZERO(!(iTest & 1));
        return d80Zero;
    }
    if (iTest < 5)
    {
        RTPBCD80U d80Ind = RTPBCD80U_INIT_INDEFINITE();
        return d80Ind;
    }

    RTPBCD80U d80;
    uint8_t b = RandU8();
    d80.s.fSign = b & 1;

    if ((iTest & 7) >= 6)
    {
        /* Illegal */
        d80.s.uPad = (iTest & 7) == 7 ? b >> 1 : 0;
        for (size_t iPair = 0; iPair < RT_ELEMENTS(d80.s.abPairs); iPair++)
            d80.s.abPairs[iPair] = RandU8();
    }
    else
    {
        /* Normal */
        d80.s.uPad = 0;
        for (size_t iPair = 0; iPair < RT_ELEMENTS(d80.s.abPairs); iPair++)
        {
            uint8_t const uLo = (uint8_t)RTRandU32Ex(0, 9);
            uint8_t const uHi = (uint8_t)RTRandU32Ex(0, 9);
            d80.s.abPairs[iPair] = RTPBCD80U_MAKE_PAIR(uHi, uLo);
        }
    }
    return d80;
}

# if 0 /* unused */

static const char *GenFormatR80(PCRTFLOAT80U plrd)
{
    if (RTFLOAT80U_IS_ZERO(plrd))
        return plrd->s.fSign ? "RTFLOAT80U_INIT_ZERO(1)" : "RTFLOAT80U_INIT_ZERO(0)";
    if (RTFLOAT80U_IS_INF(plrd))
        return plrd->s.fSign ? "RTFLOAT80U_INIT_INF(1)"  : "RTFLOAT80U_INIT_INF(0)";
    if (RTFLOAT80U_IS_INDEFINITE(plrd))
        return plrd->s.fSign ? "RTFLOAT80U_INIT_IND(1)"  : "RTFLOAT80U_INIT_IND(0)";
    if (RTFLOAT80U_IS_QUIET_NAN(plrd) && (plrd->s.uMantissa & (RT_BIT_64(62) - 1)) == 1)
        return plrd->s.fSign ? "RTFLOAT80U_INIT_QNAN(1)" : "RTFLOAT80U_INIT_QNAN(0)";
    if (RTFLOAT80U_IS_SIGNALLING_NAN(plrd) && (plrd->s.uMantissa & (RT_BIT_64(62) - 1)) == 1)
        return plrd->s.fSign ? "RTFLOAT80U_INIT_SNAN(1)" : "RTFLOAT80U_INIT_SNAN(0)";

    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrPrintf(pszBuf, sizeof(g_aszBuf[0]), "RTFLOAT80U_INIT_C(%d,%#RX64,%u)",
                plrd->s.fSign, plrd->s.uMantissa, plrd->s.uExponent);
    return pszBuf;
}

static const char *GenFormatR64(PCRTFLOAT64U prd)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrPrintf(pszBuf, sizeof(g_aszBuf[0]), "RTFLOAT64U_INIT_C(%d,%#RX64,%u)",
                prd->s.fSign, RT_MAKE_U64(prd->s.uFractionLow, prd->s.uFractionHigh), prd->s.uExponent);
    return pszBuf;
}


static const char *GenFormatR32(PCRTFLOAT32U pr)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrPrintf(pszBuf, sizeof(g_aszBuf[0]), "RTFLOAT32U_INIT_C(%d,%#RX32,%u)", pr->s.fSign, pr->s.uFraction, pr->s.uExponent);
    return pszBuf;
}


static const char *GenFormatD80(PCRTPBCD80U pd80)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    size_t off;
    if (pd80->s.uPad == 0)
        off = RTStrPrintf(pszBuf, sizeof(g_aszBuf[0]), "RTPBCD80U_INIT_C(%d", pd80->s.fSign);
    else
        off = RTStrPrintf(pszBuf, sizeof(g_aszBuf[0]), "RTPBCD80U_INIT_EX_C(%#x,%d", pd80->s.uPad, pd80->s.fSign);
    size_t iPair = RT_ELEMENTS(pd80->s.abPairs);
    while (iPair-- > 0)
        off += RTStrPrintf(&pszBuf[off], sizeof(g_aszBuf[0]) - off, ",%d,%d",
                           RTPBCD80U_HI_DIGIT(pd80->s.abPairs[iPair]),
                           RTPBCD80U_LO_DIGIT(pd80->s.abPairs[iPair]));
    pszBuf[off++] = ')';
    pszBuf[off++] = '\0';
    return pszBuf;
}


static const char *GenFormatI64(int64_t i64)
{
    if (i64 == INT64_MIN) /* This one is problematic */
        return "INT64_MIN";
    if (i64 == INT64_MAX)
        return "INT64_MAX";
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrPrintf(pszBuf, sizeof(g_aszBuf[0]), "INT64_C(%RI64)", i64);
    return pszBuf;
}

#  if 0 /* unused */
static const char *GenFormatI64(int64_t const *pi64)
{
    return GenFormatI64(*pi64);
}
#  endif

static const char *GenFormatI32(int32_t i32)
{
    if (i32 == INT32_MIN) /* This one is problematic */
        return "INT32_MIN";
    if (i32 == INT32_MAX)
        return "INT32_MAX";
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrPrintf(pszBuf, sizeof(g_aszBuf[0]), "INT32_C(%RI32)", i32);
    return pszBuf;
}


const char *GenFormatI32(int32_t const *pi32)
{
    return GenFormatI32(*pi32);
}


const char *GenFormatI16(int16_t i16)
{
    if (i16 == INT16_MIN) /* This one is problematic */
        return "INT16_MIN";
    if (i16 == INT16_MAX)
        return "INT16_MAX";
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrPrintf(pszBuf, sizeof(g_aszBuf[0]), "INT16_C(%RI16)", i16);
    return pszBuf;
}


const char *GenFormatI16(int16_t const *pi16)
{
    return GenFormatI16(*pi16);
}


static void GenerateHeader(PRTSTREAM pOut, const char *pszCpuDesc, const char *pszCpuType)
{
    /* We want to tag the generated source code with the revision that produced it. */
    static char s_szRev[] = "$Revision: 106320 $";
    const char *pszRev = RTStrStripL(strchr(s_szRev, ':') + 1);
    size_t      cchRev = 0;
    while (RT_C_IS_DIGIT(pszRev[cchRev]))
        cchRev++;

    RTStrmPrintf(pOut,
                 "/* $Id: tstIEMAImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */\n"
                 "/** @file\n"
                 " * IEM Assembly Instruction Helper Testcase Data%s%s - r%.*s on %s.\n"
                 " */\n"
                 "\n"
                 "/*\n"
                 " * Copyright (C) 2022-" VBOX_C_YEAR " Oracle and/or its affiliates.\n"
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
                 " * SPDX-License-Identifier: GPL-3.0-only\n"
                 " */\n"
                 "\n"
                 "#include \"tstIEMAImpl.h\"\n"
                 "\n"
                 ,
                 pszCpuType ? " " : "", pszCpuType ? pszCpuType : "", cchRev, pszRev, pszCpuDesc);
}


static PRTSTREAM GenerateOpenWithHdr(const char *pszFilename, const char *pszCpuDesc, const char *pszCpuType)
{
    PRTSTREAM pOut = NULL;
    int rc = RTStrmOpen(pszFilename, "w", &pOut);
    if (RT_SUCCESS(rc))
    {
        GenerateHeader(pOut, pszCpuDesc, pszCpuType);
        return pOut;
    }
    RTMsgError("Failed to open %s for writing: %Rrc", pszFilename, rc);
    return NULL;
}


static RTEXITCODE GenerateFooterAndClose(PRTSTREAM pOut, const char *pszFilename, RTEXITCODE rcExit)
{
    RTStrmPrintf(pOut,
                 "\n"
                 "/* end of file */\n");
    int rc = RTStrmClose(pOut);
    if (RT_SUCCESS(rc))
        return rcExit;
    return RTMsgErrorExitFailure("RTStrmClose failed on %s: %Rrc", pszFilename, rc);
}


static void GenerateArrayStart(PRTSTREAM pOut, const char *pszName, const char *pszType)
{
    RTStrmPrintf(pOut, "%s const g_aTests_%s[] =\n{\n", pszType, pszName);
}


static void GenerateArrayEnd(PRTSTREAM pOut, const char *pszName)
{
    RTStrmPrintf(pOut,
                 "};\n"
                 "uint32_t const g_cTests_%s = RT_ELEMENTS(g_aTests_%s);\n"
                 "\n",
                 pszName, pszName);
}

# endif  /* unused */

static void GenerateBinaryWrite(PIEMBINARYOUTPUT pBinOut, const void *pvData, size_t cbData)
{
    pBinOut->cbWritten += cbData; /* ignore errors - makes entry calculation simpler */
    if (RT_SUCCESS_NP(pBinOut->rcWrite))
    {
        pBinOut->rcWrite = RTVfsIoStrmWrite(pBinOut->hVfsUncompressed, pvData, cbData, true /*fBlocking*/, NULL);
        if (RT_SUCCESS(pBinOut->rcWrite))
            return;
        RTMsgError("Error writing '%s': %Rrc", pBinOut->szFilename, pBinOut->rcWrite);
    }
}

static bool GenerateBinaryOpen(PIEMBINARYOUTPUT pBinOut, const char *pszFilenameFmt, const char *pszName,
                               IEMTESTENTRYINFO const *pInfoToPreserve, uint32_t cbEntry)
{
    pBinOut->cbEntry          = cbEntry;
    pBinOut->cbWritten        = 0;
    pBinOut->hVfsFile         = NIL_RTVFSFILE;
    pBinOut->hVfsUncompressed = NIL_RTVFSIOSTREAM;
    if (pszFilenameFmt)
    {
        pBinOut->fNull = false;
        if (RTStrPrintf2(pBinOut->szFilename, sizeof(pBinOut->szFilename), pszFilenameFmt, pszName) > 0)
        {
            RTMsgInfo("GenerateBinaryOpen: %s...\n", pBinOut->szFilename);
            pBinOut->rcWrite = RTVfsFileOpenNormal(pBinOut->szFilename,
                                                   RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_READWRITE,
                                                   &pBinOut->hVfsFile);
            if (RT_SUCCESS(pBinOut->rcWrite))
            {
                RTVFSIOSTREAM hVfsIoFile = RTVfsFileToIoStream(pBinOut->hVfsFile);
                if (hVfsIoFile != NIL_RTVFSIOSTREAM)
                {
                    pBinOut->rcWrite = RTZipGzipCompressIoStream(hVfsIoFile, 0 /*fFlags*/, 9, &pBinOut->hVfsUncompressed);
                    RTVfsIoStrmRelease(hVfsIoFile);
                    if (RT_SUCCESS(pBinOut->rcWrite))
                    {
                        pBinOut->rcWrite      = VINF_SUCCESS;
                        pBinOut->fWroteHeader = false;

                        /* Write the header if applicable. */
                        if (   !pInfoToPreserve
                            || (pInfoToPreserve->uSvnRev != 0 && *pInfoToPreserve->pszCpuDesc))
                        {
                            IEMBINARYHEADER Hdr;
                            RT_ZERO(Hdr);
                            memcpy(Hdr.szMagic, IEMBINARYHEADER_MAGIC, sizeof(IEMBINARYHEADER_MAGIC));
                            Hdr.cbEntry = cbEntry;
                            Hdr.uSvnRev = pInfoToPreserve ? pInfoToPreserve->uSvnRev : g_uSvnRev;
                            RTStrCopy(Hdr.szCpuDesc, sizeof(Hdr.szCpuDesc),
                                      pInfoToPreserve ? pInfoToPreserve->pszCpuDesc : g_szCpuDesc);
                            GenerateBinaryWrite(pBinOut, &Hdr, sizeof(Hdr));
                            pBinOut->fWroteHeader = true;
                        }

                        return true;
                    }

                    RTMsgError("RTZipGzipCompressIoStream: %Rrc", pBinOut->rcWrite);
                }
                else
                {
                    RTMsgError("RTVfsFileToIoStream failed!");
                    pBinOut->rcWrite = VERR_VFS_CHAIN_CAST_FAILED;
                }
                RTVfsFileRelease(pBinOut->hVfsFile);
                RTFileDelete(pBinOut->szFilename);
            }
            else
                RTMsgError("Failed to open '%s' for writing: %Rrc", pBinOut->szFilename, pBinOut->rcWrite);
        }
        else
        {
            RTMsgError("filename too long: %s + %s", pszFilenameFmt, pszName);
            pBinOut->rcWrite = VERR_BUFFER_OVERFLOW;
        }
        return false;
    }
    RTMsgInfo("GenerateBinaryOpen: %s -> /dev/null\n", pszName);
    pBinOut->rcWrite       = VERR_IGNORED;
    pBinOut->fNull         = true;
    pBinOut->fWroteHeader  = false;
    pBinOut->szFilename[0] = '\0';
    return true;
}

# define GENERATE_BINARY_OPEN(a_pBinOut, a_papszNameFmts, a_Entry) \
        GenerateBinaryOpen((a_pBinOut), a_papszNameFmts[(a_Entry).idxCpuEflFlavour], (a_Entry).pszName, \
                           NULL /*pInfo*/, sizeof((a_Entry).paTests[0]))

static bool GenerateBinaryClose(PIEMBINARYOUTPUT pBinOut)
{
    if (!pBinOut->fNull)
    {
        /* Write footer if we've written a header. */
        if (pBinOut->fWroteHeader)
        {
            IEMBINARYFOOTER Ftr;
            RT_ZERO(Ftr);
            memcpy(Ftr.szMagic, IEMBINARYFOOTER_MAGIC, sizeof(IEMBINARYFOOTER_MAGIC));
            Ftr.cbEntry  = pBinOut->cbEntry;
            Ftr.cEntries = (uint32_t)((pBinOut->cbWritten - sizeof(IEMBINARYHEADER)) / pBinOut->cbEntry);
            Assert(Ftr.cEntries * pBinOut->cbEntry + sizeof(IEMBINARYHEADER) == pBinOut->cbWritten);
            GenerateBinaryWrite(pBinOut, &Ftr, sizeof(Ftr));
        }

        /* This is rather jovial about rcWrite. */
        int const rc1 = RTVfsIoStrmFlush(pBinOut->hVfsUncompressed);
        RTVfsIoStrmRelease(pBinOut->hVfsUncompressed);
        pBinOut->hVfsUncompressed = NIL_RTVFSIOSTREAM;
        if (RT_FAILURE(rc1))
            RTMsgError("Error flushing '%s' (uncompressed stream): %Rrc", pBinOut->szFilename, rc1);

        int const rc2 = RTVfsFileFlush(pBinOut->hVfsFile);
        RTVfsFileRelease(pBinOut->hVfsFile);
        pBinOut->hVfsFile = NIL_RTVFSFILE;
        if (RT_FAILURE(rc2))
            RTMsgError("Error flushing '%s' (compressed file): %Rrc", pBinOut->szFilename, rc2);

        return RT_SUCCESS(rc2) && RT_SUCCESS(rc1) && RT_SUCCESS(pBinOut->rcWrite);
    }
    return true;
}

/* Helper for DumpAll. */
# define DUMP_ALL_FN(a_FnBaseName, a_aSubTests) \
    static RTEXITCODE a_FnBaseName ## DumpAll(const char * const * papszNameFmts) \
    { \
        for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
        { \
            AssertReturn(DECOMPRESS_TESTS(a_aSubTests[iFn]), RTEXITCODE_FAILURE); \
            IEMBINARYOUTPUT BinOut; \
            AssertReturn(GenerateBinaryOpen(&BinOut, papszNameFmts[a_aSubTests[iFn].idxCpuEflFlavour], \
                                            a_aSubTests[iFn].pszName, &a_aSubTests[iFn].Info, \
                                            sizeof(a_aSubTests[iFn].paTests[0])), \
                         RTEXITCODE_FAILURE); \
            GenerateBinaryWrite(&BinOut, a_aSubTests[iFn].paTests, a_aSubTests[iFn].cTests); \
            FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
            AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
        } \
        return RTEXITCODE_SUCCESS; \
    }
#endif /* TSTIEMAIMPL_WITH_GENERATOR */


/*
 * Test helpers.
 */
static bool IsTestEnabled(const char *pszName)
{
    /* Process excludes first: */
    uint32_t i = g_cExcludeTestPatterns;
    while (i-- > 0)
        if (RTStrSimplePatternMultiMatch(g_apszExcludeTestPatterns[i], RTSTR_MAX, pszName, RTSTR_MAX, NULL))
            return false;

    /* If no include patterns, everything is included: */
    i = g_cIncludeTestPatterns;
    if (!i)
        return true;

    /* Otherwise only tests in the include patters gets tested: */
    while (i-- > 0)
        if (RTStrSimplePatternMultiMatch(g_apszIncludeTestPatterns[i], RTSTR_MAX, pszName, RTSTR_MAX, NULL))
            return true;

    return false;
}


static bool SubTestAndCheckIfEnabled(const char *pszName)
{
    bool const fEnabled = IsTestEnabled(pszName);
    if (g_fVerboseSkipping || fEnabled)
    {
        RTTestSub(g_hTest, pszName);
        if (fEnabled)
            return true;
        RTTestSkipped(g_hTest, g_cVerbosity > 0 ? "excluded" : NULL);
    }
    return false;
}


/** Decompresses test data before use as required. */
static int DecompressBinaryTest(void const *pvCompressed, uint32_t cbCompressed, size_t cbEntry, const char *pszWhat,
                                void **ppvTests, uint32_t *pcTests, IEMTESTENTRYINFO *pInfo)
{
    /* Don't do it again. */
    if (pInfo->pvUncompressed && *ppvTests)
        return VINF_SUCCESS;

    /* Open a memory stream for the compressed binary data. */
    RTVFSIOSTREAM  hVfsIos      = NIL_RTVFSIOSTREAM;
    int rc = RTVfsIoStrmFromBuffer(RTFILE_O_READ, pvCompressed, cbCompressed, &hVfsIos);
    RTTESTI_CHECK_RC_OK_RET(rc, rc);

    /* Open a decompressed stream for it. */
    RTVFSIOSTREAM hVfsIosDecomp = NIL_RTVFSIOSTREAM;
    rc = RTZipGzipDecompressIoStream(hVfsIos, RTZIPGZIPDECOMP_F_ALLOW_ZLIB_HDR, &hVfsIosDecomp);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
    {
        /* Initial output buffer allocation. */
        size_t   cbDecompressedAlloc = cbCompressed <= _16M ? (size_t)cbCompressed * 16 : (size_t)cbCompressed * 4;
        uint8_t *pbDecompressed      = (uint8_t *)RTMemAllocZ(cbDecompressedAlloc);
        if (pbDecompressed)
        {
            size_t off = 0;
            for (;;)
            {
                size_t cbRead = 0;
                rc = RTVfsIoStrmRead(hVfsIosDecomp, &pbDecompressed[off], cbDecompressedAlloc - off,  true /*fBlocking*/, &cbRead);
                if (RT_FAILURE(rc))
                    break;
                if (rc == VINF_EOF && cbRead == 0)
                    break;
                off += cbRead;

                if (cbDecompressedAlloc < off + 256)
                {
                    size_t const cbNew = cbDecompressedAlloc < _128M ? cbDecompressedAlloc * 2 : cbDecompressedAlloc + _32M;
                    void * const pvNew = RTMemRealloc(pbDecompressed, cbNew);
                    AssertBreakStmt(pvNew, rc = VERR_NO_MEMORY);
                    cbDecompressedAlloc = cbNew;
                    pbDecompressed = (uint8_t *)pvNew;
                }
            }
            if (RT_SUCCESS(rc))
            {
                size_t          const  cbUncompressed = off;

                /* Validate the header and footer if present and subtract them from 'off'. */
                IEMBINARYHEADER const *pHdr = NULL;
                if (   off >= sizeof(IEMTESTENTRYINFO)
                    && memcmp(pbDecompressed, IEMBINARYHEADER_MAGIC, sizeof(IEMBINARYHEADER_MAGIC)) == 0)
                {
                    pHdr = (IEMBINARYHEADER const *)pbDecompressed;
                    IEMBINARYFOOTER const *pFtr = (IEMBINARYFOOTER const *)&pbDecompressed[off - sizeof(IEMBINARYFOOTER)];

                    off -= sizeof(*pHdr) + sizeof(*pFtr);
                    rc = VERR_IO_BAD_UNIT;
                    if (pHdr->cbEntry != cbEntry)
                        RTTestIFailed("Test entry size differs for '%s': %#x (header r%u), expected %#zx (uncompressed size %#zx)",
                                      pszWhat, pHdr->cbEntry, pHdr->uSvnRev, cbEntry, off + sizeof(*pHdr) + sizeof(*pFtr));
                    else if (memcmp(pFtr->szMagic, IEMBINARYFOOTER_MAGIC, sizeof(IEMBINARYFOOTER_MAGIC)) != 0)
                        RTTestIFailed("Wrong footer magic for '%s': %.*Rhxs\n", pszWhat, sizeof(pFtr->szMagic), pFtr->szMagic);
                    else if (pFtr->cbEntry != cbEntry)
                        RTTestIFailed("Wrong footer entry size for '%s': %#x, expected %#x\n", pszWhat, pFtr->cbEntry, cbEntry);
                    else if (pFtr->cEntries != off / cbEntry)
                        RTTestIFailed("Wrong footer entry count for '%s': %#x, expected %#x\n",
                                      pszWhat, pFtr->cEntries, off / cbEntry);
                    else
                        rc = VINF_SUCCESS;
                }

                /* Validate the decompressed size wrt entry size. */
                if ((off % cbEntry) != 0 && RT_SUCCESS(rc))
                {
                    RTTestIFailed("Uneven decompressed data size for '%s': %#zx vs entry size %#zx -> %#zx",
                                  pszWhat, off, cbEntry, off % cbEntry);
                    rc = VERR_IO_BAD_LENGTH;
                }

                if (RT_SUCCESS(rc))
                {
                    /*
                     * We're good.
                     */
                    /* Reallocate the block if it's way to big. */
                    if (cbDecompressedAlloc - cbUncompressed > _512K)
                    {
                        void * const pvNew = RTMemRealloc(pbDecompressed, cbUncompressed);
                        if (pvNew)
                        {
                            pbDecompressed = (uint8_t *)pvNew;
                            if (pHdr)
                                pHdr = (IEMBINARYHEADER const *)pbDecompressed;
                        }
                    }
                    RTMEM_MAY_LEAK(pbDecompressed);

                    /* Fill in the info and other return values.  */
                    pInfo->cbUncompressed = (uint32_t)cbUncompressed;
                    pInfo->pvUncompressed = pbDecompressed;
                    pInfo->pszCpuDesc     = pHdr ? pHdr->szCpuDesc : NULL;
                    pInfo->uSvnRev        = pHdr ? pHdr->uSvnRev   : 0;
                    *pcTests  = (uint32_t)(off / cbEntry);
                    *ppvTests = pHdr ? (uint8_t *)(pHdr + 1) : pbDecompressed;

                    pbDecompressed = NULL;
                    rc = VINF_SUCCESS;
                }
            }
            else
                RTTestIFailed("Failed to decompress binary stream '%s': %Rrc (off=%#zx, cbCompressed=%#x)",
                              pszWhat, rc, off, cbCompressed);
            RTMemFree(pbDecompressed);
        }
        else
        {
            RTTestIFailed("Out of memory decompressing test data '%s'", pszWhat);
            rc = VERR_NO_MEMORY;
        }
        RTVfsIoStrmRelease(hVfsIosDecomp);
    }
    RTVfsIoStrmRelease(hVfsIos);
    return rc;
}

#define DECOMPRESS_TESTS(a_Entry) \
    RT_SUCCESS(DecompressBinaryTest((a_Entry).pvCompressedTests, *(a_Entry).pcbCompressedTests, \
                                    sizeof((a_Entry).paTests[0]), (a_Entry).pszName, \
                                    (void **)&(a_Entry).paTests, &(a_Entry).cTests, &(a_Entry).Info))

/** Frees the decompressed test data. */
static void FreeDecompressedTests(void **ppvTests, uint32_t *pcTests, IEMTESTENTRYINFO *pInfo)
{
    RTMemFree(pInfo->pvUncompressed);
    pInfo->pvUncompressed = NULL;
    pInfo->cbUncompressed = 0;
    *ppvTests             = NULL;
    *pcTests              = 0;
}

#define FREE_DECOMPRESSED_TESTS(a_Entry) \
    FreeDecompressedTests((void **)&(a_Entry).paTests, &(a_Entry).cTests, &(a_Entry).Info)


/** Check if the test is enabled and decompresses test data. */
static int SubTestAndCheckIfEnabledAndDecompress(const char *pszName, void const *pvCompressed, uint32_t cbCompressed,
                                                 size_t cbEntry, void **ppvTests, uint32_t *pcTests, IEMTESTENTRYINFO *pInfo)
{
    if (SubTestAndCheckIfEnabled(pszName))
    {
        int const rc = DecompressBinaryTest(pvCompressed, cbCompressed, cbEntry, pszName, ppvTests, pcTests, pInfo);
        if (RT_SUCCESS(rc))
            return true;
    }
    return false;
}

#define SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_Entry) \
    SubTestAndCheckIfEnabledAndDecompress((a_Entry).pszName, (a_Entry).pvCompressedTests, *(a_Entry).pcbCompressedTests, \
                                          sizeof((a_Entry).paTests[0]), \
                                          (void **)&(a_Entry).paTests, &(a_Entry).cTests, &(a_Entry).Info)


static const char *EFlagsDiff(uint32_t fActual, uint32_t fExpected)
{
    if (fActual == fExpected)
        return "";

    uint32_t const fXor = fActual ^ fExpected;
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    size_t cch = RTStrPrintf(pszBuf, sizeof(g_aszBuf[0]), " - %#x", fXor);

    static struct
    {
        const char *pszName;
        uint32_t    fFlag;
    } const s_aFlags[] =
    {
#define EFL_ENTRY(a_Flags) { #a_Flags, X86_EFL_ ## a_Flags }
        EFL_ENTRY(CF),
        EFL_ENTRY(PF),
        EFL_ENTRY(AF),
        EFL_ENTRY(ZF),
        EFL_ENTRY(SF),
        EFL_ENTRY(TF),
        EFL_ENTRY(IF),
        EFL_ENTRY(DF),
        EFL_ENTRY(OF),
        EFL_ENTRY(IOPL),
        EFL_ENTRY(NT),
        EFL_ENTRY(RF),
        EFL_ENTRY(VM),
        EFL_ENTRY(AC),
        EFL_ENTRY(VIF),
        EFL_ENTRY(VIP),
        EFL_ENTRY(ID),
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
        if (s_aFlags[i].fFlag & fXor)
            cch += RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch,
                               s_aFlags[i].fFlag & fActual ? "/%s" : "/!%s", s_aFlags[i].pszName);
    RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch, "");
    return pszBuf;
}


static const char *FswDiff(uint16_t fActual, uint16_t fExpected)
{
    if (fActual == fExpected)
        return "";

    uint16_t const fXor = fActual ^ fExpected;
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    size_t cch = RTStrPrintf(pszBuf, sizeof(g_aszBuf[0]), " - %#x", fXor);

    static struct
    {
        const char *pszName;
        uint32_t    fFlag;
    } const s_aFlags[] =
    {
#define FSW_ENTRY(a_Flags) { #a_Flags, X86_FSW_ ## a_Flags }
        FSW_ENTRY(IE),
        FSW_ENTRY(DE),
        FSW_ENTRY(ZE),
        FSW_ENTRY(OE),
        FSW_ENTRY(UE),
        FSW_ENTRY(PE),
        FSW_ENTRY(SF),
        FSW_ENTRY(ES),
        FSW_ENTRY(C0),
        FSW_ENTRY(C1),
        FSW_ENTRY(C2),
        FSW_ENTRY(C3),
        FSW_ENTRY(B),
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
        if (s_aFlags[i].fFlag & fXor)
            cch += RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch,
                               s_aFlags[i].fFlag & fActual ? "/%s" : "/!%s", s_aFlags[i].pszName);
    if (fXor & X86_FSW_TOP_MASK)
        cch += RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch, "/TOP%u!%u",
                           X86_FSW_TOP_GET(fActual), X86_FSW_TOP_GET(fExpected));
#if 0 /* For debugging fprem & fprem1 */
    cch += RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch, " - Q=%d (vs %d)",
                       X86_FSW_CX_TO_QUOTIENT(fActual), X86_FSW_CX_TO_QUOTIENT(fExpected));
#endif
    RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch, "");
    return pszBuf;
}


static const char *MxcsrDiff(uint32_t fActual, uint32_t fExpected)
{
    if (fActual == fExpected)
        return "";

    uint16_t const fXor = fActual ^ fExpected;
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    size_t cch = RTStrPrintf(pszBuf, sizeof(g_aszBuf[0]), " - %#x", fXor);

    static struct
    {
        const char *pszName;
        uint32_t    fFlag;
    } const s_aFlags[] =
    {
#define MXCSR_ENTRY(a_Flags) { #a_Flags, X86_MXCSR_ ## a_Flags }
        MXCSR_ENTRY(IE),
        MXCSR_ENTRY(DE),
        MXCSR_ENTRY(ZE),
        MXCSR_ENTRY(OE),
        MXCSR_ENTRY(UE),
        MXCSR_ENTRY(PE),

        MXCSR_ENTRY(IM),
        MXCSR_ENTRY(DM),
        MXCSR_ENTRY(ZM),
        MXCSR_ENTRY(OM),
        MXCSR_ENTRY(UM),
        MXCSR_ENTRY(PM),

        MXCSR_ENTRY(DAZ),
        MXCSR_ENTRY(FZ),
#undef MXCSR_ENTRY
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
        if (s_aFlags[i].fFlag & fXor)
            cch += RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch,
                               s_aFlags[i].fFlag & fActual ? "/%s" : "/!%s", s_aFlags[i].pszName);
    RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch, "");
    return pszBuf;
}


static const char *FormatFcw(uint16_t fFcw)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];

    const char *pszPC = NULL; /* (msc+gcc are too stupid) */
    switch (fFcw & X86_FCW_PC_MASK)
    {
        case X86_FCW_PC_24:     pszPC = "PC24"; break;
        case X86_FCW_PC_RSVD:   pszPC = "PCRSVD!"; break;
        case X86_FCW_PC_53:     pszPC = "PC53"; break;
        case X86_FCW_PC_64:     pszPC = "PC64"; break;
    }

    const char *pszRC = NULL; /* (msc+gcc are too stupid) */
    switch (fFcw & X86_FCW_RC_MASK)
    {
        case X86_FCW_RC_NEAREST:    pszRC = "NEAR"; break;
        case X86_FCW_RC_DOWN:       pszRC = "DOWN"; break;
        case X86_FCW_RC_UP:         pszRC = "UP"; break;
        case X86_FCW_RC_ZERO:       pszRC = "ZERO"; break;
    }
    size_t cch = RTStrPrintf(&pszBuf[0], sizeof(g_aszBuf[0]), "%s %s", pszPC, pszRC);

    static struct
    {
        const char *pszName;
        uint32_t    fFlag;
    } const s_aFlags[] =
    {
#define FCW_ENTRY(a_Flags) { #a_Flags, X86_FCW_ ## a_Flags }
        FCW_ENTRY(IM),
        FCW_ENTRY(DM),
        FCW_ENTRY(ZM),
        FCW_ENTRY(OM),
        FCW_ENTRY(UM),
        FCW_ENTRY(PM),
        { "6M", 64 },
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
        if (fFcw & s_aFlags[i].fFlag)
            cch += RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch, " %s", s_aFlags[i].pszName);

    RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch, "");
    return pszBuf;
}


static const char *FormatMxcsr(uint32_t fMxcsr)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];

    const char *pszRC = NULL; /* (msc+gcc are too stupid) */
    switch (fMxcsr & X86_MXCSR_RC_MASK)
    {
        case X86_MXCSR_RC_NEAREST:    pszRC = "NEAR"; break;
        case X86_MXCSR_RC_DOWN:       pszRC = "DOWN"; break;
        case X86_MXCSR_RC_UP:         pszRC = "UP"; break;
        case X86_MXCSR_RC_ZERO:       pszRC = "ZERO"; break;
    }

    const char *pszDAZ = fMxcsr & X86_MXCSR_DAZ ? " DAZ" : "";
    const char *pszFZ  = fMxcsr & X86_MXCSR_FZ  ? " FZ" : "";
    size_t cch = RTStrPrintf(&pszBuf[0], sizeof(g_aszBuf[0]), "%s%s%s", pszRC, pszDAZ, pszFZ);

    static struct
    {
        const char *pszName;
        uint32_t    fFlag;
    } const s_aFlags[] =
    {
#define MXCSR_ENTRY(a_Flags) { #a_Flags, X86_MXCSR_ ## a_Flags }
        MXCSR_ENTRY(IE),
        MXCSR_ENTRY(DE),
        MXCSR_ENTRY(ZE),
        MXCSR_ENTRY(OE),
        MXCSR_ENTRY(UE),
        MXCSR_ENTRY(PE),

        MXCSR_ENTRY(IM),
        MXCSR_ENTRY(DM),
        MXCSR_ENTRY(ZM),
        MXCSR_ENTRY(OM),
        MXCSR_ENTRY(UM),
        MXCSR_ENTRY(PM),
        { "6M", 64 },
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
        if (fMxcsr & s_aFlags[i].fFlag)
            cch += RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch, " %s", s_aFlags[i].pszName);

    RTStrPrintf(&pszBuf[cch], sizeof(g_aszBuf[0]) - cch, "");
    return pszBuf;
}


static const char *FormatR80(PCRTFLOAT80U pr80)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrFormatR80(pszBuf, sizeof(g_aszBuf[0]), pr80, 0, 0, RTSTR_F_SPECIAL);
    return pszBuf;
}


static const char *FormatR64(PCRTFLOAT64U pr64)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrFormatR64(pszBuf, sizeof(g_aszBuf[0]), pr64, 0, 0, RTSTR_F_SPECIAL);
    return pszBuf;
}


static const char *FormatR32(PCRTFLOAT32U pr32)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrFormatR32(pszBuf, sizeof(g_aszBuf[0]), pr32, 0, 0, RTSTR_F_SPECIAL);
    return pszBuf;
}


static const char *FormatD80(PCRTPBCD80U pd80)
{
    /* There is only one indefinite endcoding (same as for 80-bit
       floating point), so get it out of the way first: */
    if (RTPBCD80U_IS_INDEFINITE(pd80))
        return "Ind";

    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    size_t off = 0;
    pszBuf[off++] = pd80->s.fSign ? '-' : '+';
    unsigned cBadDigits = 0;
    size_t   iPair      = RT_ELEMENTS(pd80->s.abPairs);
    while (iPair-- > 0)
    {
        static const char    s_szDigits[]   = "0123456789abcdef";
        static const uint8_t s_bBadDigits[] = { 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 1, 1,  1, 1, 1, 1 };
        pszBuf[off++] = s_szDigits[RTPBCD80U_HI_DIGIT(pd80->s.abPairs[iPair])];
        pszBuf[off++] = s_szDigits[RTPBCD80U_LO_DIGIT(pd80->s.abPairs[iPair])];
        cBadDigits += s_bBadDigits[RTPBCD80U_HI_DIGIT(pd80->s.abPairs[iPair])]
                    + s_bBadDigits[RTPBCD80U_LO_DIGIT(pd80->s.abPairs[iPair])];
    }
    if (cBadDigits || pd80->s.uPad != 0)
        off += RTStrPrintf(&pszBuf[off], sizeof(g_aszBuf[0]) - off, "[%u,%#x]", cBadDigits, pd80->s.uPad);
    pszBuf[off] = '\0';
    return pszBuf;
}


#if 0
static const char *FormatI64(int64_t const *piVal)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrFormatU64(pszBuf, sizeof(g_aszBuf[0]), *piVal, 16, 0, 0, RTSTR_F_SPECIAL | RTSTR_F_VALSIGNED);
    return pszBuf;
}
#endif


static const char *FormatI32(int32_t const *piVal)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrFormatU32(pszBuf, sizeof(g_aszBuf[0]), *piVal, 16, 0, 0, RTSTR_F_SPECIAL | RTSTR_F_VALSIGNED);
    return pszBuf;
}


static const char *FormatI16(int16_t const *piVal)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrFormatU16(pszBuf, sizeof(g_aszBuf[0]), *piVal, 16, 0, 0, RTSTR_F_SPECIAL | RTSTR_F_VALSIGNED);
    return pszBuf;
}


static const char *FormatU128(PCRTUINT128U puVal)
{
    char *pszBuf = g_aszBuf[g_idxBuf++ % RT_ELEMENTS(g_aszBuf)];
    RTStrFormatU128(pszBuf, sizeof(g_aszBuf[0]), puVal, 16, 0, 0, RTSTR_F_SPECIAL);
    return pszBuf;
}


/*
 * Binary operations.
 */
TYPEDEF_SUBTEST_TYPE(BINU8_T,  BINU8_TEST_T,  PFNIEMAIMPLBINU8);
TYPEDEF_SUBTEST_TYPE(BINU16_T, BINU16_TEST_T, PFNIEMAIMPLBINU16);
TYPEDEF_SUBTEST_TYPE(BINU32_T, BINU32_TEST_T, PFNIEMAIMPLBINU32);
TYPEDEF_SUBTEST_TYPE(BINU64_T, BINU64_TEST_T, PFNIEMAIMPLBINU64);

#ifdef TSTIEMAIMPL_WITH_GENERATOR
# define GEN_BINARY_TESTS(a_cBits, a_Fmt, a_TestType) \
static RTEXITCODE BinU ## a_cBits ## Generate(uint32_t cTests, const char * const * papszNameFmts) \
{ \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aBinU ## a_cBits); iFn++) \
    { \
        PFNIEMAIMPLBINU ## a_cBits const pfn    = g_aBinU ## a_cBits[iFn].pfnNative \
                                                ? g_aBinU ## a_cBits[iFn].pfnNative : g_aBinU ## a_cBits[iFn].pfn; \
        IEMBINARYOUTPUT                  BinOut; \
        if (   g_aBinU ## a_cBits[iFn].idxCpuEflFlavour != IEMTARGETCPU_EFL_BEHAVIOR_NATIVE \
            && g_aBinU ## a_cBits[iFn].idxCpuEflFlavour != g_idxCpuEflFlavour) \
            continue; \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aBinU ## a_cBits[iFn]), RTEXITCODE_FAILURE); \
        for (uint32_t iTest = 0; iTest < cTests; iTest++ ) \
        { \
            a_TestType Test; \
            Test.fEflIn    = RandEFlags(); \
            Test.uDstIn    = RandU ## a_cBits ## Dst(iTest); \
            Test.uDstOut   = Test.uDstIn; \
            Test.uSrcIn    = RandU ## a_cBits ## Src(iTest); \
            if (g_aBinU ## a_cBits[iFn].uExtra) \
                Test.uSrcIn &= a_cBits - 1; /* Restrict bit index according to operand width */ \
            Test.uMisc     = 0; \
            Test.fEflOut   = pfn(Test.fEflIn, &Test.uDstOut, Test.uSrcIn); \
            GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
        } \
        for (uint32_t iTest = 0; iTest < g_aBinU ## a_cBits[iFn].cFixedTests; iTest++ ) \
        { \
            a_TestType Test; \
            Test.fEflIn    = g_aBinU ## a_cBits[iFn].paFixedTests[iTest].fEflIn == UINT32_MAX ? RandEFlags() \
                           : g_aBinU ## a_cBits[iFn].paFixedTests[iTest].fEflIn; \
            Test.uDstIn    = g_aBinU ## a_cBits[iFn].paFixedTests[iTest].uDstIn; \
            Test.uDstOut   = Test.uDstIn; \
            Test.uSrcIn    = g_aBinU ## a_cBits[iFn].paFixedTests[iTest].uSrcIn; \
            Test.uMisc     = g_aBinU ## a_cBits[iFn].paFixedTests[iTest].uMisc; \
            Test.fEflOut   = pfn(Test.fEflIn, &Test.uDstOut, Test.uSrcIn); \
            GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
        } \
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
    } \
    return RTEXITCODE_SUCCESS; \
} \
DUMP_ALL_FN(BinU ## a_cBits, g_aBinU ## a_cBits)

#else
# define GEN_BINARY_TESTS(a_cBits, a_Fmt, a_TestType)
#endif


/** Based on a quick probe run, guess how long to run the benchmark. */
static uint32_t EstimateIterations(uint32_t cProbeIterations, uint64_t cNsProbe)
{
    uint64_t cPicoSecPerIteration = cNsProbe * 1000 / cProbeIterations;
    uint64_t cIterations = g_cPicoSecBenchmark / cPicoSecPerIteration;
    if (cIterations > _2G)
        return _2G;
    if (cIterations < _4K)
        return _4K;
    return RT_ALIGN_32((uint32_t)cIterations, _4K);
}


#define TEST_BINARY_OPS(a_cBits, a_uType, a_Fmt, a_TestType, a_aSubTests) \
GEN_BINARY_TESTS(a_cBits, a_Fmt, a_TestType) \
\
static uint64_t BinU ## a_cBits ## Bench(uint32_t cIterations, PFNIEMAIMPLBINU ## a_cBits pfn, a_TestType const *pEntry) \
{ \
    uint32_t const fEflIn = pEntry->fEflIn; \
    a_uType  const uDstIn = pEntry->uDstIn; \
    a_uType  const uSrcIn = pEntry->uSrcIn; \
    cIterations /= 4; \
    RTThreadYield(); \
    uint64_t const nsStart = RTTimeNanoTS(); \
    for (uint32_t i = 0; i < cIterations; i++) \
    { \
        a_uType uBenchDst = uDstIn;  \
        pfn(fEflIn, &uBenchDst, uSrcIn); \
        \
        uBenchDst = uDstIn;  \
        pfn(fEflIn, &uBenchDst, uSrcIn); \
        \
        uBenchDst = uDstIn;  \
        pfn(fEflIn, &uBenchDst, uSrcIn); \
        \
        uBenchDst = uDstIn;  \
        pfn(fEflIn, &uBenchDst, uSrcIn); \
    } \
    return RTTimeNanoTS() - nsStart; \
} \
\
static void BinU ## a_cBits ## Test(void) \
{ \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_aSubTests[iFn])) \
            continue; \
        a_TestType const * const   paTests = a_aSubTests[iFn].paTests; \
        uint32_t const             cTests  = a_aSubTests[iFn].cTests; \
        PFNIEMAIMPLBINU ## a_cBits pfn     = a_aSubTests[iFn].pfn; \
        uint32_t const             cVars   = COUNT_VARIATIONS(a_aSubTests[iFn]); \
        if (!cTests) { RTTestSkipped(g_hTest, "no tests"); continue; } \
        for (uint32_t iVar = 0; iVar < cVars; iVar++) \
        { \
            for (uint32_t iTest = 0; iTest < cTests; iTest++ ) \
            { \
                a_uType  uDst = paTests[iTest].uDstIn; \
                uint32_t fEfl = pfn(paTests[iTest].fEflIn, &uDst, paTests[iTest].uSrcIn); \
                if (   uDst != paTests[iTest].uDstOut \
                    || fEfl != paTests[iTest].fEflOut) \
                    RTTestFailed(g_hTest, "#%u%s: efl=%#08x dst=" a_Fmt " src=" a_Fmt " -> efl=%#08x dst=" a_Fmt ", expected %#08x & " a_Fmt "%s - %s\n", \
                                 iTest, !iVar ? "" : "/n", paTests[iTest].fEflIn, paTests[iTest].uDstIn, paTests[iTest].uSrcIn, \
                                 fEfl, uDst, paTests[iTest].fEflOut, paTests[iTest].uDstOut, \
                                 EFlagsDiff(fEfl, paTests[iTest].fEflOut), \
                                 uDst == paTests[iTest].uDstOut ? "eflags" : fEfl == paTests[iTest].fEflOut ? "dst" : "both"); \
                else \
                { \
                     *g_pu ## a_cBits  = paTests[iTest].uDstIn; \
                     fEfl = pfn(paTests[iTest].fEflIn, g_pu ## a_cBits, paTests[iTest].uSrcIn); \
                     RTTEST_CHECK(g_hTest, *g_pu ## a_cBits  == paTests[iTest].uDstOut); \
                     RTTEST_CHECK(g_hTest, fEfl == paTests[iTest].fEflOut); \
                } \
            } \
            \
            /* Benchmark if all succeeded. */ \
            if (g_cPicoSecBenchmark && RTTestSubErrorCount(g_hTest) == 0) \
            { \
                uint32_t const iTest       = cTests / 2; \
                uint32_t const cIterations = EstimateIterations(_64K, BinU ## a_cBits ## Bench(_64K, pfn, &paTests[iTest])); \
                uint64_t const cNsRealRun  = BinU ## a_cBits ## Bench(cIterations, pfn, &paTests[iTest]); \
                RTTestValueF(g_hTest, cNsRealRun * 1000 / cIterations, RTTESTUNIT_PS_PER_CALL, \
                             "%s%s", a_aSubTests[iFn].pszName, iVar ? "-native" : ""); \
            } \
            \
            /* Next variation is native. */ \
            pfn = a_aSubTests[iFn].pfnNative; \
        } \
        FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
    } \
}


/*
 * 8-bit binary operations.
 */
static BINU8_T g_aBinU8[] =
{
    ENTRY_BIN(add_u8),
    ENTRY_BIN(add_u8_locked),
    ENTRY_BIN(adc_u8),
    ENTRY_BIN(adc_u8_locked),
    ENTRY_BIN(sub_u8),
    ENTRY_BIN(sub_u8_locked),
    ENTRY_BIN(sbb_u8),
    ENTRY_BIN(sbb_u8_locked),
    ENTRY_BIN(or_u8),
    ENTRY_BIN(or_u8_locked),
    ENTRY_BIN(xor_u8),
    ENTRY_BIN(xor_u8_locked),
    ENTRY_BIN(and_u8),
    ENTRY_BIN(and_u8_locked),
    ENTRY_BIN_PFN_CAST(cmp_u8,  PFNIEMAIMPLBINU8),
    ENTRY_BIN_PFN_CAST(test_u8, PFNIEMAIMPLBINU8),
};
TEST_BINARY_OPS(8, uint8_t, "%#04x", BINU8_TEST_T, g_aBinU8)


/*
 * 16-bit binary operations.
 */
#ifdef TSTIEMAIMPL_WITH_GENERATOR
static const BINU16_TEST_T g_aFixedTests_add_u16[] =
{
    /* efl in, efl out, uDstIn, uDstOut,       uSrc, uExtra */
    { UINT32_MAX,    0,      1,       0, UINT16_MAX,      0 },
};
#endif
static BINU16_T g_aBinU16[] =
{
    ENTRY_BIN_FIX(add_u16),
    ENTRY_BIN(add_u16_locked),
    ENTRY_BIN(adc_u16),
    ENTRY_BIN(adc_u16_locked),
    ENTRY_BIN(sub_u16),
    ENTRY_BIN(sub_u16_locked),
    ENTRY_BIN(sbb_u16),
    ENTRY_BIN(sbb_u16_locked),
    ENTRY_BIN(or_u16),
    ENTRY_BIN(or_u16_locked),
    ENTRY_BIN(xor_u16),
    ENTRY_BIN(xor_u16_locked),
    ENTRY_BIN(and_u16),
    ENTRY_BIN(and_u16_locked),
    ENTRY_BIN_PFN_CAST(cmp_u16,   PFNIEMAIMPLBINU16),
    ENTRY_BIN_PFN_CAST(test_u16,  PFNIEMAIMPLBINU16),
    ENTRY_BIN_PFN_CAST_EX(bt_u16, PFNIEMAIMPLBINU16, 1),
    ENTRY_BIN_EX(btc_u16, 1),
    ENTRY_BIN_EX(btc_u16_locked, 1),
    ENTRY_BIN_EX(btr_u16, 1),
    ENTRY_BIN_EX(btr_u16_locked, 1),
    ENTRY_BIN_EX(bts_u16, 1),
    ENTRY_BIN_EX(bts_u16_locked, 1),
    ENTRY_BIN_AMD(  bsf_u16, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_INTEL(bsf_u16, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_AMD(  bsr_u16, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_INTEL(bsr_u16, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_AMD(  imul_two_u16, X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF),
    ENTRY_BIN_INTEL(imul_two_u16, X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF),
    ENTRY_BIN(arpl),
};
TEST_BINARY_OPS(16, uint16_t, "%#06x", BINU16_TEST_T, g_aBinU16)


/*
 * 32-bit binary operations.
 */
#ifdef TSTIEMAIMPL_WITH_GENERATOR
static const BINU32_TEST_T g_aFixedTests_add_u32[] =
{
    /* efl in, efl out, uDstIn, uDstOut,       uSrc, uExtra */
    { UINT32_MAX,    0,      1,       0, UINT32_MAX,      0 },
};
#endif
static BINU32_T g_aBinU32[] =
{
    ENTRY_BIN_FIX(add_u32),
    ENTRY_BIN(add_u32_locked),
    ENTRY_BIN(adc_u32),
    ENTRY_BIN(adc_u32_locked),
    ENTRY_BIN(sub_u32),
    ENTRY_BIN(sub_u32_locked),
    ENTRY_BIN(sbb_u32),
    ENTRY_BIN(sbb_u32_locked),
    ENTRY_BIN(or_u32),
    ENTRY_BIN(or_u32_locked),
    ENTRY_BIN(xor_u32),
    ENTRY_BIN(xor_u32_locked),
    ENTRY_BIN(and_u32),
    ENTRY_BIN(and_u32_locked),
    ENTRY_BIN_PFN_CAST(cmp_u32,   PFNIEMAIMPLBINU32),
    ENTRY_BIN_PFN_CAST(test_u32,  PFNIEMAIMPLBINU32),
    ENTRY_BIN_PFN_CAST_EX(bt_u32, PFNIEMAIMPLBINU32, 1),
    ENTRY_BIN_EX(btc_u32, 1),
    ENTRY_BIN_EX(btc_u32_locked, 1),
    ENTRY_BIN_EX(btr_u32, 1),
    ENTRY_BIN_EX(btr_u32_locked, 1),
    ENTRY_BIN_EX(bts_u32, 1),
    ENTRY_BIN_EX(bts_u32_locked, 1),
    ENTRY_BIN_AMD(  bsf_u32, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_INTEL(bsf_u32, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_AMD(  bsr_u32, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_INTEL(bsr_u32, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_AMD(  imul_two_u32, X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF),
    ENTRY_BIN_INTEL(imul_two_u32, X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF),
    ENTRY_BIN(adcx_u32),
    ENTRY_BIN(adox_u32),
};
TEST_BINARY_OPS(32, uint32_t, "%#010RX32", BINU32_TEST_T, g_aBinU32)


/*
 * 64-bit binary operations.
 */
#ifdef TSTIEMAIMPL_WITH_GENERATOR
static const BINU64_TEST_T g_aFixedTests_add_u64[] =
{
    /* efl in, efl out, uDstIn, uDstOut,       uSrc, uExtra */
    { UINT32_MAX,    0,      1,       0, UINT64_MAX,      0 },
};
#endif
static BINU64_T g_aBinU64[] =
{
    ENTRY_BIN_FIX(add_u64),
    ENTRY_BIN(add_u64_locked),
    ENTRY_BIN(adc_u64),
    ENTRY_BIN(adc_u64_locked),
    ENTRY_BIN(sub_u64),
    ENTRY_BIN(sub_u64_locked),
    ENTRY_BIN(sbb_u64),
    ENTRY_BIN(sbb_u64_locked),
    ENTRY_BIN(or_u64),
    ENTRY_BIN(or_u64_locked),
    ENTRY_BIN(xor_u64),
    ENTRY_BIN(xor_u64_locked),
    ENTRY_BIN(and_u64),
    ENTRY_BIN(and_u64_locked),
    ENTRY_BIN_PFN_CAST(cmp_u64,   PFNIEMAIMPLBINU64),
    ENTRY_BIN_PFN_CAST(test_u64,  PFNIEMAIMPLBINU64),
    ENTRY_BIN_PFN_CAST_EX(bt_u64, PFNIEMAIMPLBINU64, 1),
    ENTRY_BIN_EX(btc_u64, 1),
    ENTRY_BIN_EX(btc_u64_locked, 1),
    ENTRY_BIN_EX(btr_u64, 1),
    ENTRY_BIN_EX(btr_u64_locked, 1),
    ENTRY_BIN_EX(bts_u64, 1),
    ENTRY_BIN_EX(bts_u64_locked, 1),
    ENTRY_BIN_AMD(  bsf_u64, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_INTEL(bsf_u64, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_AMD(  bsr_u64, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_INTEL(bsr_u64, X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF),
    ENTRY_BIN_AMD(  imul_two_u64, X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF),
    ENTRY_BIN_INTEL(imul_two_u64, X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF),
    ENTRY_BIN(adcx_u64),
    ENTRY_BIN(adox_u64),
/** @todo popcnt */
/** @todo tzcnt */
/** @todo lzcnt */
};
TEST_BINARY_OPS(64, uint64_t, "%#018RX64", BINU64_TEST_T, g_aBinU64)


/*
 * XCHG
 */
static void XchgTest(void)
{
    if (!SubTestAndCheckIfEnabled("xchg"))
        return;
    typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLXCHGU8, (uint8_t  *pu8Mem,  uint8_t  *pu8Reg));
    typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLXCHGU16,(uint16_t *pu16Mem, uint16_t *pu16Reg));
    typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLXCHGU32,(uint32_t *pu32Mem, uint32_t *pu32Reg));
    typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLXCHGU64,(uint64_t *pu64Mem, uint64_t *pu64Reg));

    static struct
    {
        uint8_t cb; uint64_t fMask;
        union
        {
            uintptr_t           pfn;
            FNIEMAIMPLXCHGU8   *pfnU8;
            FNIEMAIMPLXCHGU16  *pfnU16;
            FNIEMAIMPLXCHGU32  *pfnU32;
            FNIEMAIMPLXCHGU64  *pfnU64;
        } u;
    }
    s_aXchgWorkers[] =
    {
        { 1, UINT8_MAX,  { (uintptr_t)iemAImpl_xchg_u8_locked    } },
        { 2, UINT16_MAX, { (uintptr_t)iemAImpl_xchg_u16_locked   } },
        { 4, UINT32_MAX, { (uintptr_t)iemAImpl_xchg_u32_locked   } },
        { 8, UINT64_MAX, { (uintptr_t)iemAImpl_xchg_u64_locked   } },
        { 1, UINT8_MAX,  { (uintptr_t)iemAImpl_xchg_u8_unlocked  } },
        { 2, UINT16_MAX, { (uintptr_t)iemAImpl_xchg_u16_unlocked } },
        { 4, UINT32_MAX, { (uintptr_t)iemAImpl_xchg_u32_unlocked } },
        { 8, UINT64_MAX, { (uintptr_t)iemAImpl_xchg_u64_unlocked } },
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aXchgWorkers); i++)
    {
        RTUINT64U uIn1, uIn2, uMem, uDst;
        uMem.u = uIn1.u = RTRandU64Ex(0, s_aXchgWorkers[i].fMask);
        uDst.u = uIn2.u = RTRandU64Ex(0, s_aXchgWorkers[i].fMask);
        if (uIn1.u == uIn2.u)
            uDst.u = uIn2.u = ~uIn2.u;

        switch (s_aXchgWorkers[i].cb)
        {
            case 1:
                s_aXchgWorkers[i].u.pfnU8(g_pu8, g_pu8Two);
                s_aXchgWorkers[i].u.pfnU8(&uMem.au8[0], &uDst.au8[0]);
                break;
            case 2:
                s_aXchgWorkers[i].u.pfnU16(g_pu16, g_pu16Two);
                s_aXchgWorkers[i].u.pfnU16(&uMem.Words.w0, &uDst.Words.w0);
                break;
            case 4:
                s_aXchgWorkers[i].u.pfnU32(g_pu32, g_pu32Two);
                s_aXchgWorkers[i].u.pfnU32(&uMem.DWords.dw0, &uDst.DWords.dw0);
                break;
            case 8:
                s_aXchgWorkers[i].u.pfnU64(g_pu64, g_pu64Two);
                s_aXchgWorkers[i].u.pfnU64(&uMem.u, &uDst.u);
                break;
            default: RTTestFailed(g_hTest, "%d\n", s_aXchgWorkers[i].cb); break;
        }

        if (uMem.u != uIn2.u || uDst.u != uIn1.u)
            RTTestFailed(g_hTest, "i=%u: %#RX64, %#RX64 -> %#RX64, %#RX64\n", i,  uIn1.u, uIn2.u, uMem.u, uDst.u);
    }
}


/*
 * XADD
 */
static void XaddTest(void)
{
#define TEST_XADD(a_cBits, a_Type, a_Fmt) do { \
        typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLXADDU ## a_cBits, (a_Type *, a_Type *, uint32_t *)); \
        static struct \
        { \
            const char * const                  pszName; \
            FNIEMAIMPLXADDU ## a_cBits * const  pfn; \
            void const * const                  pvCompressedTests; \
            uint32_t const * const              pcbCompressedTests; \
            BINU ## a_cBits ## _TEST_T const   *paTests; \
            uint32_t                            cTests; \
            IEMTESTENTRYINFO                    Info; \
        } s_aFuncs[] = \
        { \
            { "xadd_u" # a_cBits,            iemAImpl_xadd_u ## a_cBits, \
              g_abTests_add_u ## a_cBits, &g_cbTests_add_u ## a_cBits }, \
            { "xadd_u" # a_cBits "8_locked", iemAImpl_xadd_u ## a_cBits ## _locked, \
              g_abTests_add_u ## a_cBits, &g_cbTests_add_u ## a_cBits }, \
        }; \
        for (size_t iFn = 0; iFn < RT_ELEMENTS(s_aFuncs); iFn++) \
        { \
            if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(s_aFuncs[iFn])) continue; \
            BINU ## a_cBits ## _TEST_T const * const paTests = s_aFuncs[iFn].paTests; \
            uint32_t const                           cTests  = s_aFuncs[iFn].cTests; \
            if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
            for (uint32_t iTest = 0; iTest < cTests; iTest++) \
            { \
                uint32_t fEfl = paTests[iTest].fEflIn; \
                a_Type   uSrc = paTests[iTest].uSrcIn; \
                *g_pu ## a_cBits = paTests[iTest].uDstIn; \
                s_aFuncs[iFn].pfn(g_pu ## a_cBits, &uSrc, &fEfl); \
                if (   fEfl             != paTests[iTest].fEflOut \
                    || *g_pu ## a_cBits != paTests[iTest].uDstOut \
                    || uSrc             != paTests[iTest].uDstIn) \
                    RTTestFailed(g_hTest, "%s/#%u: efl=%#08x dst=" a_Fmt " src=" a_Fmt " -> efl=%#08x dst=" a_Fmt " src=" a_Fmt ", expected %#08x, " a_Fmt ", " a_Fmt "%s\n", \
                                 s_aFuncs[iFn].pszName, iTest, paTests[iTest].fEflIn, paTests[iTest].uDstIn, paTests[iTest].uSrcIn, \
                                 fEfl, *g_pu ## a_cBits, uSrc, paTests[iTest].fEflOut, paTests[iTest].uDstOut, paTests[iTest].uDstIn, \
                                 EFlagsDiff(fEfl, paTests[iTest].fEflOut)); \
            } \
           FREE_DECOMPRESSED_TESTS(s_aFuncs[iFn]); \
        } \
    } while(0)
    TEST_XADD(8, uint8_t, "%#04x");
    TEST_XADD(16, uint16_t, "%#06x");
    TEST_XADD(32, uint32_t, "%#010RX32");
    TEST_XADD(64, uint64_t, "%#010RX64");
}


/*
 * CMPXCHG
 */

static void CmpXchgTest(void)
{
#define TEST_CMPXCHG(a_cBits, a_Type, a_Fmt) do {\
        typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLCMPXCHGU ## a_cBits, (a_Type *, a_Type *, a_Type, uint32_t *)); \
        static struct \
        { \
            const char * const                      pszName; \
            FNIEMAIMPLCMPXCHGU ## a_cBits * const   pfn; \
            PFNIEMAIMPLBINU ## a_cBits const        pfnSub; \
            void const * const                      pvCompressedTests; \
            uint32_t const * const                  pcbCompressedTests; \
            BINU ## a_cBits ## _TEST_T const       *paTests; \
            uint32_t                                cTests; \
            IEMTESTENTRYINFO                        Info; \
        } s_aFuncs[] = \
        { \
            { "cmpxchg_u" # a_cBits,           iemAImpl_cmpxchg_u ## a_cBits, iemAImpl_sub_u ## a_cBits, \
              g_abTests_cmp_u ## a_cBits, &g_cbTests_cmp_u ## a_cBits }, \
            { "cmpxchg_u" # a_cBits "_locked", iemAImpl_cmpxchg_u ## a_cBits ## _locked, iemAImpl_sub_u ## a_cBits, \
              g_abTests_cmp_u ## a_cBits, &g_cbTests_cmp_u ## a_cBits }, \
        }; \
        for (size_t iFn = 0; iFn < RT_ELEMENTS(s_aFuncs); iFn++) \
        { \
            if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(s_aFuncs[iFn])) continue; \
            BINU ## a_cBits ## _TEST_T const * const paTests = s_aFuncs[iFn].paTests; \
            uint32_t const                           cTests  = s_aFuncs[iFn].cTests; \
            if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
            for (uint32_t iTest = 0; iTest < cTests; iTest++) \
            { \
                /* as is (99% likely to be negative). */ \
                uint32_t      fEfl    = paTests[iTest].fEflIn; \
                a_Type const  uNew    = paTests[iTest].uSrcIn + 0x42; \
                a_Type        uA      = paTests[iTest].uDstIn; \
                *g_pu ## a_cBits      = paTests[iTest].uSrcIn; \
                a_Type const  uExpect = uA != paTests[iTest].uSrcIn ? paTests[iTest].uSrcIn : uNew; \
                s_aFuncs[iFn].pfn(g_pu ## a_cBits, &uA, uNew, &fEfl); \
                if (   fEfl             != paTests[iTest].fEflOut \
                    || *g_pu ## a_cBits != uExpect \
                    || uA               != paTests[iTest].uSrcIn) \
                    RTTestFailed(g_hTest, "%s/#%ua: efl=%#08x dst=" a_Fmt " cmp=" a_Fmt " new=" a_Fmt " -> efl=%#08x dst=" a_Fmt " old=" a_Fmt ", expected %#08x, " a_Fmt ", " a_Fmt "%s\n", \
                                 s_aFuncs[iFn].pszName, iTest, paTests[iTest].fEflIn, paTests[iTest].uSrcIn, paTests[iTest].uDstIn, \
                                 uNew, fEfl, *g_pu ## a_cBits, uA, paTests[iTest].fEflOut, uExpect, paTests[iTest].uSrcIn, \
                                 EFlagsDiff(fEfl, paTests[iTest].fEflOut)); \
                /* positive */ \
                uA                  = paTests[iTest].uDstIn; \
                uint32_t fEflExpect = s_aFuncs[iFn].pfnSub(paTests[iTest].fEflIn, &uA, uA); \
                fEfl                = paTests[iTest].fEflIn; \
                uA                  = paTests[iTest].uDstIn; \
                *g_pu ## a_cBits    = uA; \
                s_aFuncs[iFn].pfn(g_pu ## a_cBits, &uA, uNew, &fEfl); \
                if (   fEfl             != fEflExpect \
                    || *g_pu ## a_cBits != uNew \
                    || uA               != paTests[iTest].uDstIn) \
                    RTTestFailed(g_hTest, "%s/#%ua: efl=%#08x dst=" a_Fmt " cmp=" a_Fmt " new=" a_Fmt " -> efl=%#08x dst=" a_Fmt " old=" a_Fmt ", expected %#08x, " a_Fmt ", " a_Fmt "%s\n", \
                                 s_aFuncs[iFn].pszName, iTest, paTests[iTest].fEflIn, paTests[iTest].uDstIn, paTests[iTest].uDstIn, \
                                 uNew, fEfl, *g_pu ## a_cBits, uA, fEflExpect, uNew, paTests[iTest].uDstIn, \
                                 EFlagsDiff(fEfl, fEflExpect)); \
            } \
            FREE_DECOMPRESSED_TESTS(s_aFuncs[iFn]); \
        } \
    } while(0)
    TEST_CMPXCHG(8, uint8_t, "%#04RX8");
    TEST_CMPXCHG(16, uint16_t, "%#06x");
    TEST_CMPXCHG(32, uint32_t, "%#010RX32");
#if ARCH_BITS != 32 /* calling convension issue, skipping as it's an unsupported host  */
    TEST_CMPXCHG(64, uint64_t, "%#010RX64");
#endif
}


typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLCMPXCHG8B,(uint64_t *, PRTUINT64U, PRTUINT64U, uint32_t *));

static uint64_t CmpXchg8bBench(uint32_t cIterations, FNIEMAIMPLCMPXCHG8B *pfn, uint64_t const uDstValue,
                               uint64_t const uOldValue, uint64_t const uNewValue, uint32_t const fEflIn)
{
    cIterations /= 4;
    RTThreadYield();
    uint64_t const nsStart = RTTimeNanoTS();
    for (uint32_t i = 0; i < cIterations; i++)
    {
        RTUINT64U uA, uB;
        uint32_t fEfl    = fEflIn;
        uint64_t uDst    = uDstValue;
        uB.u             = uNewValue;
        uA.u             = uOldValue;
        pfn(&uDst, &uA, &uB, &fEfl);

        fEfl = fEflIn;
        uDst = uDstValue;
        uB.u = uNewValue;
        uA.u = uOldValue;
        pfn(&uDst, &uA, &uB, &fEfl);

        fEfl = fEflIn;
        uDst = uDstValue;
        uB.u = uNewValue;
        uA.u = uOldValue;
        pfn(&uDst, &uA, &uB, &fEfl);

        fEfl = fEflIn;
        uDst = uDstValue;
        uB.u = uNewValue;
        uA.u = uOldValue;
        pfn(&uDst, &uA, &uB, &fEfl);
    }
    return RTTimeNanoTS() - nsStart;
}

static void CmpXchg8bTest(void)
{
    static struct
    {
        const char           *pszName;
        FNIEMAIMPLCMPXCHG8B  *pfn;
    } const s_aFuncs[] =
    {
        { "cmpxchg8b",        iemAImpl_cmpxchg8b },
        { "cmpxchg8b_locked", iemAImpl_cmpxchg8b_locked },
    };
    for (size_t iFn = 0; iFn < RT_ELEMENTS(s_aFuncs); iFn++)
    {
        if (!SubTestAndCheckIfEnabled(s_aFuncs[iFn].pszName))
            continue;
        for (uint32_t iTest = 0; iTest < 4; iTest += 2)
        {
            uint64_t const uOldValue = RandU64();
            uint64_t const uNewValue = RandU64();

            /* positive test. */
            RTUINT64U uA, uB;
            uB.u             = uNewValue;
            uA.u             = uOldValue;
            *g_pu64          = uOldValue;
            uint32_t fEflIn  = RandEFlags();
            uint32_t fEfl    = fEflIn;
            s_aFuncs[iFn].pfn(g_pu64, &uA, &uB, &fEfl);
            if (   fEfl    != (fEflIn | X86_EFL_ZF)
                || *g_pu64 != uNewValue
                || uA.u    != uOldValue)
                RTTestFailed(g_hTest, "#%u: efl=%#08x dst=%#018RX64 cmp=%#018RX64 new=%#018RX64\n -> efl=%#08x dst=%#018RX64 old=%#018RX64,\n wanted %#08x,    %#018RX64,    %#018RX64%s\n",
                             iTest, fEflIn, uOldValue, uOldValue, uNewValue,
                             fEfl, *g_pu64, uA.u,
                             (fEflIn | X86_EFL_ZF), uNewValue, uOldValue, EFlagsDiff(fEfl, fEflIn | X86_EFL_ZF));
            RTTEST_CHECK(g_hTest, uB.u == uNewValue);

            /* negative */
            uint64_t const uExpect = ~uOldValue;
            *g_pu64 = uExpect;
            uA.u = uOldValue;
            uB.u = uNewValue;
            fEfl = fEflIn = RandEFlags();
            s_aFuncs[iFn].pfn(g_pu64, &uA, &uB, &fEfl);
            if (   fEfl    != (fEflIn & ~X86_EFL_ZF)
                || *g_pu64 != uExpect
                || uA.u    != uExpect)
                RTTestFailed(g_hTest, "#%u: efl=%#08x dst=%#018RX64 cmp=%#018RX64 new=%#018RX64\n -> efl=%#08x dst=%#018RX64 old=%#018RX64,\n wanted %#08x,    %#018RX64,    %#018RX64%s\n",
                             iTest + 1, fEflIn, uExpect, uOldValue, uNewValue,
                             fEfl, *g_pu64, uA.u,
                             (fEflIn & ~X86_EFL_ZF), uExpect, uExpect, EFlagsDiff(fEfl, fEflIn & ~X86_EFL_ZF));
            RTTEST_CHECK(g_hTest, uB.u == uNewValue);

            if (iTest == 2 && g_cPicoSecBenchmark && RTTestSubErrorCount(g_hTest) == 0)
            {
                uint32_t cIterations = EstimateIterations(_64K, CmpXchg8bBench(_64K, s_aFuncs[iFn].pfn,
                                                                               uOldValue, uOldValue, uNewValue, fEflIn));
                uint64_t cNsRealRun  = CmpXchg8bBench(cIterations, s_aFuncs[iFn].pfn, uOldValue, uOldValue, uNewValue, fEflIn);
                RTTestValueF(g_hTest, cNsRealRun * 1000 / cIterations, RTTESTUNIT_PS_PER_CALL,
                             "%s-positive", s_aFuncs[iFn].pszName);

                cIterations = EstimateIterations(_64K, CmpXchg8bBench(_64K, s_aFuncs[iFn].pfn,
                                                                      ~uOldValue, uOldValue, uNewValue, fEflIn));
                cNsRealRun  = CmpXchg8bBench(cIterations, s_aFuncs[iFn].pfn, ~uOldValue, uOldValue, uNewValue, fEflIn);
                RTTestValueF(g_hTest, cNsRealRun * 1000 / cIterations, RTTESTUNIT_PS_PER_CALL,
                             "%s-negative", s_aFuncs[iFn].pszName);
            }
        }
    }
}

static void CmpXchg16bTest(void)
{
    typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLCMPXCHG16B,(PRTUINT128U, PRTUINT128U, PRTUINT128U, uint32_t *));
    static struct
    {
        const char           *pszName;
        FNIEMAIMPLCMPXCHG16B *pfn;
    } const s_aFuncs[] =
    {
        { "cmpxchg16b",          iemAImpl_cmpxchg16b },
        { "cmpxchg16b_locked",   iemAImpl_cmpxchg16b_locked },
#if !defined(RT_ARCH_ARM64)
        { "cmpxchg16b_fallback", iemAImpl_cmpxchg16b_fallback },
#endif
    };
    for (size_t iFn = 0; iFn < RT_ELEMENTS(s_aFuncs); iFn++)
    {
        if (!SubTestAndCheckIfEnabled(s_aFuncs[iFn].pszName))
            continue;
#if !defined(IEM_WITHOUT_ASSEMBLY) && defined(RT_ARCH_AMD64)
        if (!(ASMCpuId_ECX(1) & X86_CPUID_FEATURE_ECX_CX16))
        {
            RTTestSkipped(g_hTest, "no hardware cmpxchg16b");
            continue;
        }
#endif
        for (uint32_t iTest = 0; iTest < 4; iTest += 2)
        {
            RTUINT128U const uOldValue = RandU128();
            RTUINT128U const uNewValue = RandU128();

            /* positive test. */
            RTUINT128U uA, uB;
            uB               = uNewValue;
            uA               = uOldValue;
            *g_pu128         = uOldValue;
            uint32_t fEflIn  = RandEFlags();
            uint32_t fEfl    = fEflIn;
            s_aFuncs[iFn].pfn(g_pu128, &uA, &uB, &fEfl);
            if (   fEfl    != (fEflIn | X86_EFL_ZF)
                || g_pu128->s.Lo != uNewValue.s.Lo
                || g_pu128->s.Hi != uNewValue.s.Hi
                || uA.s.Lo       != uOldValue.s.Lo
                || uA.s.Hi       != uOldValue.s.Hi)
                RTTestFailed(g_hTest, "#%u: efl=%#08x dst=%#018RX64'%016RX64 cmp=%#018RX64'%016RX64 new=%#018RX64'%016RX64\n"
                                      " -> efl=%#08x dst=%#018RX64'%016RX64 old=%#018RX64'%016RX64,\n"
                                      " wanted %#08x,    %#018RX64'%016RX64,    %#018RX64'%016RX64%s\n",
                             iTest, fEflIn, uOldValue.s.Hi, uOldValue.s.Lo, uOldValue.s.Hi, uOldValue.s.Lo, uNewValue.s.Hi, uNewValue.s.Lo,
                             fEfl, g_pu128->s.Hi, g_pu128->s.Lo, uA.s.Hi, uA.s.Lo,
                             (fEflIn | X86_EFL_ZF), uNewValue.s.Hi, uNewValue.s.Lo, uOldValue.s.Hi, uOldValue.s.Lo,
                             EFlagsDiff(fEfl, fEflIn | X86_EFL_ZF));
            RTTEST_CHECK(g_hTest, uB.s.Lo == uNewValue.s.Lo && uB.s.Hi == uNewValue.s.Hi);

            /* negative */
            RTUINT128U const uExpect = RTUINT128_INIT(~uOldValue.s.Hi, ~uOldValue.s.Lo);
            *g_pu128 = uExpect;
            uA       = uOldValue;
            uB       = uNewValue;
            fEfl = fEflIn = RandEFlags();
            s_aFuncs[iFn].pfn(g_pu128, &uA, &uB, &fEfl);
            if (   fEfl          != (fEflIn & ~X86_EFL_ZF)
                || g_pu128->s.Lo != uExpect.s.Lo
                || g_pu128->s.Hi != uExpect.s.Hi
                || uA.s.Lo       != uExpect.s.Lo
                || uA.s.Hi       != uExpect.s.Hi)
                RTTestFailed(g_hTest, "#%u: efl=%#08x dst=%#018RX64'%016RX64 cmp=%#018RX64'%016RX64 new=%#018RX64'%016RX64\n"
                                      " -> efl=%#08x dst=%#018RX64'%016RX64 old=%#018RX64'%016RX64,\n"
                                      " wanted %#08x,    %#018RX64'%016RX64,    %#018RX64'%016RX64%s\n",
                             iTest + 1, fEflIn, uExpect.s.Hi, uExpect.s.Lo, uOldValue.s.Hi, uOldValue.s.Lo, uNewValue.s.Hi, uNewValue.s.Lo,
                             fEfl, g_pu128->s.Hi, g_pu128->s.Lo, uA.s.Hi, uA.s.Lo,
                             (fEflIn & ~X86_EFL_ZF), uExpect.s.Hi, uExpect.s.Lo, uExpect.s.Hi, uExpect.s.Lo,
                             EFlagsDiff(fEfl, fEflIn & ~X86_EFL_ZF));
            RTTEST_CHECK(g_hTest, uB.s.Lo == uNewValue.s.Lo && uB.s.Hi == uNewValue.s.Hi);
        }
    }
}


/*
 * Double shifts.
 *
 * Note! We use BINUxx_TEST_T with the shift value in the uMisc field.
 */
#ifdef TSTIEMAIMPL_WITH_GENERATOR
# define GEN_SHIFT_DBL(a_cBits, a_Fmt, a_TestType, a_aSubTests) \
static RTEXITCODE ShiftDblU ## a_cBits ## Generate(uint32_t cTests, const char * const * papszNameFmts) \
{ \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (   a_aSubTests[iFn].idxCpuEflFlavour != IEMTARGETCPU_EFL_BEHAVIOR_NATIVE \
            && a_aSubTests[iFn].idxCpuEflFlavour != g_idxCpuEflFlavour) \
            continue; \
        IEMBINARYOUTPUT BinOut; \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, a_aSubTests[iFn]), RTEXITCODE_FAILURE); \
        for (uint32_t iTest = 0; iTest < cTests; iTest++ ) \
        { \
            a_TestType Test; \
            Test.fEflIn    = RandEFlags(); \
            Test.fEflOut   = Test.fEflIn; \
            Test.uDstIn    = RandU ## a_cBits ## Dst(iTest); \
            Test.uDstOut   = Test.uDstIn; \
            Test.uSrcIn    = RandU ## a_cBits ## Src(iTest); \
            Test.uMisc     = RandU8() & (a_cBits * 4 - 1); /* need to go way beyond the a_cBits limit */ \
            a_aSubTests[iFn].pfnNative(&Test.uDstOut, Test.uSrcIn, Test.uMisc, &Test.fEflOut); \
            GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
        } \
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
    } \
    return RTEXITCODE_SUCCESS; \
} \
DUMP_ALL_FN(ShiftDblU ## a_cBits, a_aSubTests)

#else
# define GEN_SHIFT_DBL(a_cBits, a_Fmt, a_TestType, a_aSubTests)
#endif

#define TEST_SHIFT_DBL(a_cBits, a_uType, a_Fmt, a_TestType, a_SubTestType, a_aSubTests) \
TYPEDEF_SUBTEST_TYPE(a_SubTestType, a_TestType, PFNIEMAIMPLSHIFTDBLU ## a_cBits); \
\
static a_SubTestType a_aSubTests[] = \
{ \
    ENTRY_BIN_AMD(shld_u ## a_cBits,   X86_EFL_OF | X86_EFL_CF), \
    ENTRY_BIN_INTEL(shld_u ## a_cBits, X86_EFL_OF | X86_EFL_CF), \
    ENTRY_BIN_AMD(shrd_u ## a_cBits,   X86_EFL_OF | X86_EFL_CF), \
    ENTRY_BIN_INTEL(shrd_u ## a_cBits, X86_EFL_OF | X86_EFL_CF), \
}; \
\
GEN_SHIFT_DBL(a_cBits, a_Fmt, a_TestType, a_aSubTests) \
\
static uint64_t ShiftDblU ## a_cBits ## Bench(uint32_t cIterations, PFNIEMAIMPLSHIFTDBLU ## a_cBits pfn, a_TestType const *pEntry) \
{ \
    uint32_t const fEflIn = pEntry->fEflIn; \
    a_uType  const uDstIn = pEntry->uDstIn; \
    a_uType  const uSrcIn = pEntry->uSrcIn; \
    a_uType  const cShift = pEntry->uMisc; \
    cIterations /= 4; \
    RTThreadYield(); \
    uint64_t const nsStart     = RTTimeNanoTS(); \
    for (uint32_t i = 0; i < cIterations; i++) \
    { \
        uint32_t fBenchEfl = fEflIn; \
        a_uType  uBenchDst = uDstIn;  \
        pfn(&uBenchDst, uSrcIn, cShift, &fBenchEfl); \
        \
        fBenchEfl = fEflIn; \
        uBenchDst = uDstIn;  \
        pfn(&uBenchDst, uSrcIn, cShift, &fBenchEfl); \
        \
        fBenchEfl = fEflIn; \
        uBenchDst = uDstIn;  \
        pfn(&uBenchDst, uSrcIn, cShift, &fBenchEfl); \
        \
        fBenchEfl = fEflIn; \
        uBenchDst = uDstIn;  \
        pfn(&uBenchDst, uSrcIn, cShift, &fBenchEfl); \
    } \
    return RTTimeNanoTS() - nsStart; \
} \
\
static void ShiftDblU ## a_cBits ## Test(void) \
{ \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_aSubTests[iFn])) \
            continue; \
        a_TestType const * const        paTests = a_aSubTests[iFn].paTests; \
        uint32_t const                  cTests  = a_aSubTests[iFn].cTests; \
        PFNIEMAIMPLSHIFTDBLU ## a_cBits pfn     = a_aSubTests[iFn].pfn; \
        uint32_t const                  cVars   = COUNT_VARIATIONS(a_aSubTests[iFn]); \
        if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
        for (uint32_t iVar = 0; iVar < cVars; iVar++) \
        { \
            for (uint32_t iTest = 0; iTest < cTests; iTest++ ) \
            { \
                uint32_t fEfl = paTests[iTest].fEflIn; \
                a_uType  uDst = paTests[iTest].uDstIn; \
                pfn(&uDst, paTests[iTest].uSrcIn, paTests[iTest].uMisc, &fEfl); \
                if (   uDst != paTests[iTest].uDstOut \
                    || fEfl != paTests[iTest].fEflOut) \
                    RTTestFailed(g_hTest, "#%03u%s: efl=%#08x dst=" a_Fmt " src=" a_Fmt " shift=%-2u -> efl=%#08x dst=" a_Fmt ", expected %#08x & " a_Fmt "%s%s\n", \
                                 iTest, iVar == 0 ? "" : "/n", paTests[iTest].fEflIn, \
                                 paTests[iTest].uDstIn, paTests[iTest].uSrcIn, (unsigned)paTests[iTest].uMisc, \
                                 fEfl, uDst, paTests[iTest].fEflOut, paTests[iTest].uDstOut, \
                                 EFlagsDiff(fEfl, paTests[iTest].fEflOut), uDst == paTests[iTest].uDstOut ? "" : " dst!"); \
                else \
                { \
                     *g_pu ## a_cBits  = paTests[iTest].uDstIn; \
                     *g_pfEfl          = paTests[iTest].fEflIn; \
                     pfn(g_pu ## a_cBits, paTests[iTest].uSrcIn, paTests[iTest].uMisc, g_pfEfl); \
                     RTTEST_CHECK(g_hTest, *g_pu ## a_cBits == paTests[iTest].uDstOut); \
                     RTTEST_CHECK(g_hTest, *g_pfEfl == paTests[iTest].fEflOut); \
                } \
            } \
            \
            /* Benchmark if all succeeded. */ \
            if (g_cPicoSecBenchmark && RTTestSubErrorCount(g_hTest) == 0) \
            { \
                uint32_t const iTest       = cTests / 2; \
                uint32_t const cIterations = EstimateIterations(_64K, ShiftDblU ## a_cBits ## Bench(_64K, pfn, &paTests[iTest])); \
                uint64_t const cNsRealRun  = ShiftDblU ## a_cBits ## Bench(cIterations, pfn, &paTests[iTest]); \
                RTTestValueF(g_hTest, cNsRealRun * 1000 / cIterations, RTTESTUNIT_PS_PER_CALL, \
                             "%s%s", a_aSubTests[iFn].pszName, iVar ? "-native" : ""); \
            } \
            \
            /* Next variation is native. */ \
            pfn = a_aSubTests[iFn].pfnNative; \
        } \
        FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
    } \
}
TEST_SHIFT_DBL(16, uint16_t, "%#06RX16",  BINU16_TEST_T, SHIFT_DBL_U16_T, g_aShiftDblU16)
TEST_SHIFT_DBL(32, uint32_t, "%#010RX32", BINU32_TEST_T, SHIFT_DBL_U32_T, g_aShiftDblU32)
TEST_SHIFT_DBL(64, uint64_t, "%#018RX64", BINU64_TEST_T, SHIFT_DBL_U64_T, g_aShiftDblU64)

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE ShiftDblGenerate(uint32_t cTests, const char * const * papszNameFmts)
{
    RTEXITCODE rcExit = ShiftDblU16Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = ShiftDblU32Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = ShiftDblU64Generate(cTests, papszNameFmts);
    return rcExit;
}

static RTEXITCODE ShiftDblDumpAll(const char * const * papszNameFmts)
{
    RTEXITCODE rcExit = ShiftDblU16DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = ShiftDblU32DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = ShiftDblU64DumpAll(papszNameFmts);
    return rcExit;
}
#endif

static void ShiftDblTest(void)
{
    ShiftDblU16Test();
    ShiftDblU32Test();
    ShiftDblU64Test();
}


/*
 * Unary operators.
 *
 * Note! We use BINUxx_TEST_T ignoreing uSrcIn and uMisc.
 */
#ifdef TSTIEMAIMPL_WITH_GENERATOR
# define GEN_UNARY(a_cBits, a_Type, a_Fmt, a_TestType, a_SubTestType) \
static RTEXITCODE UnaryU ## a_cBits ## Generate(uint32_t cTests, const char * const * papszNameFmts) \
{ \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aUnaryU ## a_cBits); iFn++) \
    { \
        IEMBINARYOUTPUT BinOut; \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aUnaryU ## a_cBits[iFn]), RTEXITCODE_FAILURE); \
        for (uint32_t iTest = 0; iTest < cTests; iTest++ ) \
        { \
            a_TestType Test; \
            Test.fEflIn    = RandEFlags(); \
            Test.fEflOut   = Test.fEflIn; \
            Test.uDstIn    = RandU ## a_cBits(); \
            Test.uDstOut   = Test.uDstIn; \
            Test.uSrcIn    = 0; \
            Test.uMisc     = 0; \
            g_aUnaryU ## a_cBits[iFn].pfn(&Test.uDstOut, &Test.fEflOut); \
            GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
        } \
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
    } \
    return RTEXITCODE_SUCCESS; \
} \
DUMP_ALL_FN(UnaryU ## a_cBits, g_aUnaryU ## a_cBits)
#else
# define GEN_UNARY(a_cBits, a_Type, a_Fmt, a_TestType, a_SubTestType)
#endif

#define TEST_UNARY(a_cBits, a_uType, a_Fmt, a_TestType, a_SubTestType, a_aSubTests) \
TYPEDEF_SUBTEST_TYPE(a_SubTestType, a_TestType, PFNIEMAIMPLUNARYU ## a_cBits); \
static a_SubTestType a_aSubTests[] = \
{ \
    ENTRY_BIN(inc_u ## a_cBits), \
    ENTRY_BIN(inc_u ## a_cBits ## _locked), \
    ENTRY_BIN(dec_u ## a_cBits), \
    ENTRY_BIN(dec_u ## a_cBits ## _locked), \
    ENTRY_BIN(not_u ## a_cBits), \
    ENTRY_BIN(not_u ## a_cBits ## _locked), \
    ENTRY_BIN(neg_u ## a_cBits), \
    ENTRY_BIN(neg_u ## a_cBits ## _locked), \
}; \
\
GEN_UNARY(a_cBits, a_uType, a_Fmt, a_TestType, a_SubTestType) \
\
static uint64_t UnaryU ## a_cBits ## Bench(uint32_t cIterations, PFNIEMAIMPLUNARYU ## a_cBits pfn, a_TestType const *pEntry) \
{ \
    uint32_t const fEflIn  = pEntry->fEflIn; \
    a_uType  const uDstIn  = pEntry->uDstIn; \
    cIterations /= 4; \
    RTThreadYield(); \
    uint64_t const nsStart     = RTTimeNanoTS(); \
    for (uint32_t i = 0; i < cIterations; i++) \
    { \
        uint32_t fBenchEfl = fEflIn; \
        a_uType  uBenchDst = uDstIn;  \
        pfn(&uBenchDst, &fBenchEfl); \
        \
        fBenchEfl = fEflIn; \
        uBenchDst = uDstIn;  \
        pfn(&uBenchDst, &fBenchEfl); \
        \
        fBenchEfl = fEflIn; \
        uBenchDst = uDstIn;  \
        pfn(&uBenchDst, &fBenchEfl); \
        \
        fBenchEfl = fEflIn; \
        uBenchDst = uDstIn;  \
        pfn(&uBenchDst, &fBenchEfl); \
    } \
    return RTTimeNanoTS() - nsStart; \
} \
\
static void UnaryU ## a_cBits ## Test(void) \
{ \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_aSubTests[iFn])) \
            continue; \
        PFNIEMAIMPLUNARYU ## a_cBits const pfn     = a_aSubTests[iFn].pfn; \
        a_TestType const * const           paTests = a_aSubTests[iFn].paTests; \
        uint32_t const                     cTests  = a_aSubTests[iFn].cTests; \
        if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
        for (uint32_t iTest = 0; iTest < cTests; iTest++ ) \
        { \
            uint32_t fEfl = paTests[iTest].fEflIn; \
            a_uType  uDst = paTests[iTest].uDstIn; \
            pfn(&uDst, &fEfl); \
            if (   uDst != paTests[iTest].uDstOut \
                || fEfl != paTests[iTest].fEflOut) \
                RTTestFailed(g_hTest, "#%u: efl=%#08x dst=" a_Fmt " -> efl=%#08x dst=" a_Fmt ", expected %#08x & " a_Fmt "%s\n", \
                             iTest, paTests[iTest].fEflIn, paTests[iTest].uDstIn, \
                             fEfl, uDst, paTests[iTest].fEflOut, paTests[iTest].uDstOut, \
                             EFlagsDiff(fEfl, paTests[iTest].fEflOut)); \
            else \
            { \
                 *g_pu ## a_cBits  = paTests[iTest].uDstIn; \
                 *g_pfEfl          = paTests[iTest].fEflIn; \
                 pfn(g_pu ## a_cBits, g_pfEfl); \
                 RTTEST_CHECK(g_hTest, *g_pu ## a_cBits == paTests[iTest].uDstOut); \
                 RTTEST_CHECK(g_hTest, *g_pfEfl == paTests[iTest].fEflOut); \
            } \
        } \
        \
        if (g_cPicoSecBenchmark && RTTestSubErrorCount(g_hTest) == 0) \
        { \
            uint32_t const iTest       = cTests / 2; \
            uint32_t const cIterations = EstimateIterations(_64K, UnaryU ## a_cBits ## Bench(_64K, pfn, &paTests[iTest])); \
            uint64_t const cNsRealRun  = UnaryU ## a_cBits ## Bench(cIterations, pfn, &paTests[iTest]); \
            RTTestValueF(g_hTest, cNsRealRun * 1000 / cIterations, RTTESTUNIT_PS_PER_CALL, "%s", a_aSubTests[iFn].pszName); \
        } \
        \
        FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
    } \
}
TEST_UNARY(8,  uint8_t,  "%#04RX8",   BINU8_TEST_T,  INT_UNARY_U8_T,  g_aUnaryU8)
TEST_UNARY(16, uint16_t, "%#06RX16",  BINU16_TEST_T, INT_UNARY_U16_T, g_aUnaryU16)
TEST_UNARY(32, uint32_t, "%#010RX32", BINU32_TEST_T, INT_UNARY_U32_T, g_aUnaryU32)
TEST_UNARY(64, uint64_t, "%#018RX64", BINU64_TEST_T, INT_UNARY_U64_T, g_aUnaryU64)

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE UnaryGenerate(uint32_t cTests, const char * const * papszNameFmts)
{
    RTEXITCODE rcExit = UnaryU8Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = UnaryU16Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = UnaryU32Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = UnaryU64Generate(cTests, papszNameFmts);
    return rcExit;
}

static RTEXITCODE UnaryDumpAll(const char * const * papszNameFmts)
{
    RTEXITCODE rcExit = UnaryU8DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = UnaryU16DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = UnaryU32DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = UnaryU64DumpAll(papszNameFmts);
    return rcExit;
}
#endif

static void UnaryTest(void)
{
    UnaryU8Test();
    UnaryU16Test();
    UnaryU32Test();
    UnaryU64Test();
}


/*
 * Shifts.
 *
 * Note! We use BINUxx_TEST_T with the shift count in uMisc and uSrcIn unused.
 */
#ifdef TSTIEMAIMPL_WITH_GENERATOR
# define GEN_SHIFT(a_cBits, a_Fmt, a_TestType, a_aSubTests) \
static RTEXITCODE ShiftU ## a_cBits ## Generate(uint32_t cTests, const char * const * papszNameFmts) \
{ \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (   a_aSubTests[iFn].idxCpuEflFlavour != IEMTARGETCPU_EFL_BEHAVIOR_NATIVE \
            && a_aSubTests[iFn].idxCpuEflFlavour != g_idxCpuEflFlavour) \
            continue; \
        IEMBINARYOUTPUT BinOut; \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, a_aSubTests[iFn]), RTEXITCODE_FAILURE); \
        for (uint32_t iTest = 0; iTest < cTests; iTest++ ) \
        { \
            a_TestType Test; \
            Test.fEflIn    = RandEFlags(); \
            Test.uDstIn    = RandU ## a_cBits ## Dst(iTest); \
            Test.uDstOut   = Test.uDstIn; \
            Test.uSrcIn    = 0; \
            Test.uMisc     = RandU8() & (a_cBits * 4 - 1); /* need to go way beyond the a_cBits limit */ \
            Test.fEflOut   = a_aSubTests[iFn].pfnNative(Test.fEflIn, &Test.uDstOut, Test.uMisc); \
            GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
            \
            Test.fEflIn    = (~Test.fEflIn & X86_EFL_LIVE_MASK) | X86_EFL_RA1_MASK; \
            Test.uDstOut   = Test.uDstIn; \
            Test.fEflOut   = a_aSubTests[iFn].pfnNative(Test.fEflIn, &Test.uDstOut, Test.uMisc); \
            GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
        } \
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
    } \
    return RTEXITCODE_SUCCESS; \
} \
DUMP_ALL_FN(ShiftU ## a_cBits, a_aSubTests)
#else
# define GEN_SHIFT(a_cBits, a_Fmt, a_TestType, a_aSubTests)
#endif

#define TEST_SHIFT(a_cBits, a_uType, a_Fmt, a_TestType, a_SubTestType, a_aSubTests) \
TYPEDEF_SUBTEST_TYPE(a_SubTestType, a_TestType, PFNIEMAIMPLSHIFTU ## a_cBits); \
static a_SubTestType a_aSubTests[] = \
{ \
    ENTRY_BIN_AMD(  rol_u ## a_cBits, X86_EFL_OF), \
    ENTRY_BIN_INTEL(rol_u ## a_cBits, X86_EFL_OF), \
    ENTRY_BIN_AMD(  ror_u ## a_cBits, X86_EFL_OF), \
    ENTRY_BIN_INTEL(ror_u ## a_cBits, X86_EFL_OF), \
    ENTRY_BIN_AMD(  rcl_u ## a_cBits, X86_EFL_OF), \
    ENTRY_BIN_INTEL(rcl_u ## a_cBits, X86_EFL_OF), \
    ENTRY_BIN_AMD(  rcr_u ## a_cBits, X86_EFL_OF), \
    ENTRY_BIN_INTEL(rcr_u ## a_cBits, X86_EFL_OF),  \
    ENTRY_BIN_AMD(  shl_u ## a_cBits, X86_EFL_OF | X86_EFL_AF), \
    ENTRY_BIN_INTEL(shl_u ## a_cBits, X86_EFL_OF | X86_EFL_AF), \
    ENTRY_BIN_AMD(  shr_u ## a_cBits, X86_EFL_OF | X86_EFL_AF), \
    ENTRY_BIN_INTEL(shr_u ## a_cBits, X86_EFL_OF | X86_EFL_AF), \
    ENTRY_BIN_AMD(  sar_u ## a_cBits, X86_EFL_OF | X86_EFL_AF), \
    ENTRY_BIN_INTEL(sar_u ## a_cBits, X86_EFL_OF | X86_EFL_AF), \
}; \
\
GEN_SHIFT(a_cBits, a_Fmt, a_TestType, a_aSubTests) \
\
static uint64_t ShiftU ## a_cBits ## Bench(uint32_t cIterations, PFNIEMAIMPLSHIFTU ## a_cBits pfn, a_TestType const *pEntry) \
{ \
    uint32_t const fEflIn = pEntry->fEflIn; \
    a_uType  const uDstIn = pEntry->uDstIn; \
    a_uType  const cShift = pEntry->uMisc; \
    cIterations /= 4; \
    RTThreadYield(); \
    uint64_t const nsStart     = RTTimeNanoTS(); \
    for (uint32_t i = 0; i < cIterations; i++) \
    { \
        a_uType  uBenchDst = uDstIn;  \
        pfn(fEflIn, &uBenchDst, cShift); \
        \
        uBenchDst = uDstIn;  \
        pfn(fEflIn, &uBenchDst, cShift); \
        \
        uBenchDst = uDstIn;  \
        pfn(fEflIn, &uBenchDst, cShift); \
        \
        uBenchDst = uDstIn;  \
        pfn(fEflIn, &uBenchDst, cShift); \
    } \
    return RTTimeNanoTS() - nsStart; \
} \
\
static void ShiftU ## a_cBits ## Test(void) \
{ \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_aSubTests[iFn])) \
            continue; \
        PFNIEMAIMPLSHIFTU ## a_cBits pfn     = a_aSubTests[iFn].pfn; \
        a_TestType const * const     paTests = a_aSubTests[iFn].paTests; \
        uint32_t const               cTests  = a_aSubTests[iFn].cTests; \
        uint32_t const               cVars   = COUNT_VARIATIONS(a_aSubTests[iFn]); \
        if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
        for (uint32_t iVar = 0; iVar < cVars; iVar++) \
        { \
            for (uint32_t iTest = 0; iTest < cTests; iTest++ ) \
            { \
                a_uType  uDst    = paTests[iTest].uDstIn; \
                uint32_t fEflOut = pfn(paTests[iTest].fEflIn, &uDst, paTests[iTest].uMisc); \
                if (   uDst    != paTests[iTest].uDstOut \
                    || fEflOut != paTests[iTest].fEflOut ) \
                    RTTestFailed(g_hTest, "#%u%s: efl=%#08x dst=" a_Fmt " shift=%2u -> efl=%#08x dst=" a_Fmt ", expected %#08x & " a_Fmt "%s\n", \
                                 iTest, iVar == 0 ? "" : "/n", \
                                 paTests[iTest].fEflIn, paTests[iTest].uDstIn, paTests[iTest].uMisc, \
                                 fEflOut, uDst, paTests[iTest].fEflOut, paTests[iTest].uDstOut, \
                                 EFlagsDiff(fEflOut, paTests[iTest].fEflOut)); \
                else \
                { \
                     *g_pu ## a_cBits  = paTests[iTest].uDstIn; \
                     fEflOut = pfn(paTests[iTest].fEflIn, g_pu ## a_cBits, paTests[iTest].uMisc); \
                     RTTEST_CHECK(g_hTest, *g_pu ## a_cBits == paTests[iTest].uDstOut); \
                     RTTEST_CHECK(g_hTest, fEflOut == paTests[iTest].fEflOut); \
                } \
            } \
            \
            /* Benchmark if all succeeded. */ \
            if (g_cPicoSecBenchmark && RTTestSubErrorCount(g_hTest) == 0) \
            { \
                uint32_t const iTest       = cTests / 2; \
                uint32_t const cIterations = EstimateIterations(_64K, ShiftU ## a_cBits ## Bench(_64K, pfn, &paTests[iTest])); \
                uint64_t const cNsRealRun  = ShiftU ## a_cBits ## Bench(cIterations, pfn, &paTests[iTest]); \
                RTTestValueF(g_hTest, cNsRealRun * 1000 / cIterations, RTTESTUNIT_PS_PER_CALL, \
                             "%s%s", a_aSubTests[iFn].pszName, iVar ? "-native" : ""); \
            } \
            \
            /* Next variation is native. */ \
            pfn = a_aSubTests[iFn].pfnNative; \
        } \
        FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
    } \
}
TEST_SHIFT(8,  uint8_t,  "%#04RX8",   BINU8_TEST_T,  INT_BINARY_U8_T,  g_aShiftU8)
TEST_SHIFT(16, uint16_t, "%#06RX16",  BINU16_TEST_T, INT_BINARY_U16_T, g_aShiftU16)
TEST_SHIFT(32, uint32_t, "%#010RX32", BINU32_TEST_T, INT_BINARY_U32_T, g_aShiftU32)
TEST_SHIFT(64, uint64_t, "%#018RX64", BINU64_TEST_T, INT_BINARY_U64_T, g_aShiftU64)

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE ShiftGenerate(uint32_t cTests, const char * const * papszNameFmts)
{
    RTEXITCODE rcExit = ShiftU8Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = ShiftU16Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = ShiftU32Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = ShiftU64Generate(cTests, papszNameFmts);
    return rcExit;
}

static RTEXITCODE ShiftDumpAll(const char * const * papszNameFmts)
{
    RTEXITCODE rcExit = ShiftU8DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = ShiftU16DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = ShiftU32DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = ShiftU64DumpAll(papszNameFmts);
    return rcExit;
}
#endif

static void ShiftTest(void)
{
    ShiftU8Test();
    ShiftU16Test();
    ShiftU32Test();
    ShiftU64Test();
}


/*
 * Multiplication and division.
 *
 * Note! The 8-bit functions has a different format, so we need to duplicate things.
 * Note! Currently ignoring undefined bits.
 */

/* U8 */
#ifdef TSTIEMAIMPL_WITH_GENERATOR
static const MULDIVU8_TEST_T g_aFixedTests_idiv_u8[] =
{
    /* efl in, efl out, uDstIn, uDstOut, uSrcIn,  rc (0 or -1 for actual; -128 for auto) */
    { UINT32_MAX,    0, 0x8000,        0,  0xc7,  -1   }, /* -32768 / -57 = #DE (574.8771929824...)  */
    { UINT32_MAX,    0, 0x8000,        0,  0xdd,  -128 }, /* -32768 / -35 = #DE (936.2285714285...) */
    { UINT32_MAX,    0, 0x7f00,        0,  0x7f,  -1   }, /* 0x7f00 / 0x7f = #DE (0x100)  */
    { UINT32_MAX,    0, 0x3f80,        0,  0x7f,  -1   }, /* 0x3F80 / 0x7f = #DE (0x80)  */
    { UINT32_MAX,    0, 0x3f7f,        0,  0x7f,   0   }, /* 0x3F7F / 0x7f = 127.992125984... */
    { UINT32_MAX,    0, 0xc000,        0,  0x80,  -1   }, /* -16384 / -128 = #DE (0x80) */
    { UINT32_MAX,    0, 0xc001,        0,  0x80,   0   }, /* -16383 / -128 =  127.9921875 */
};
#endif
TYPEDEF_SUBTEST_TYPE(INT_MULDIV_U8_T, MULDIVU8_TEST_T, PFNIEMAIMPLMULDIVU8);
static INT_MULDIV_U8_T g_aMulDivU8[] =
{
    ENTRY_BIN_AMD_EX(mul_u8,        X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF,
                                    X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF),
    ENTRY_BIN_INTEL_EX(mul_u8,      X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF, 0),
    ENTRY_BIN_AMD_EX(imul_u8,       X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF,
                                    X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF),
    ENTRY_BIN_INTEL_EX(imul_u8,     X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF, 0),
    ENTRY_BIN_AMD_EX(div_u8,        X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF | X86_EFL_OF, 0),
    ENTRY_BIN_INTEL_EX(div_u8,      X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF | X86_EFL_OF, 0),
    ENTRY_BIN_FIX_AMD_EX(idiv_u8,   X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF | X86_EFL_OF, 0),
    ENTRY_BIN_FIX_INTEL_EX(idiv_u8, X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF | X86_EFL_OF, 0),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(MulDivU8, g_aMulDivU8)
static RTEXITCODE MulDivU8Generate(uint32_t cTests, const char * const * papszNameFmts)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aMulDivU8); iFn++)
    {
        if (   g_aMulDivU8[iFn].idxCpuEflFlavour != IEMTARGETCPU_EFL_BEHAVIOR_NATIVE
            && g_aMulDivU8[iFn].idxCpuEflFlavour != g_idxCpuEflFlavour)
            continue;
        IEMBINARYOUTPUT BinOut; \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aMulDivU8[iFn]), RTEXITCODE_FAILURE); \
        for (uint32_t iTest = 0; iTest < cTests; iTest++ )
        {
            MULDIVU8_TEST_T Test;
            Test.fEflIn    = RandEFlags();
            Test.uDstIn    = RandU16Dst(iTest);
            Test.uDstOut   = Test.uDstIn;
            Test.uSrcIn    = RandU8Src(iTest);
            uint32_t const fEflRet = g_aMulDivU8[iFn].pfnNative(&Test.uDstOut, Test.uSrcIn, Test.fEflIn);
            Test.fEflOut   = fEflRet ? fEflRet : Test.fEflIn;
            Test.rc        = fEflRet ? 0       : -1;
            GenerateBinaryWrite(&BinOut, &Test, sizeof(Test));
        }
        for (uint32_t iTest = 0; iTest < g_aMulDivU8[iFn].cFixedTests; iTest++)
        {
            MULDIVU8_TEST_T Test;
            Test.fEflIn    = g_aMulDivU8[iFn].paFixedTests[iTest].fEflIn == UINT32_MAX ? RandEFlags()
                           : g_aMulDivU8[iFn].paFixedTests[iTest].fEflIn;
            Test.uDstIn    = g_aMulDivU8[iFn].paFixedTests[iTest].uDstIn;
            Test.uDstOut   = Test.uDstIn;
            Test.uSrcIn    = g_aMulDivU8[iFn].paFixedTests[iTest].uSrcIn;
            uint32_t const fEflRet = g_aMulDivU8[iFn].pfnNative(&Test.uDstOut, Test.uSrcIn, Test.fEflIn);
            Test.fEflOut   = fEflRet ? fEflRet : Test.fEflIn;
            Test.rc        = fEflRet ? 0       : -1;
            if (g_aMulDivU8[iFn].paFixedTests[iTest].rc == 0 || g_aMulDivU8[iFn].paFixedTests[iTest].rc == -1)
                Test.rc = g_aMulDivU8[iFn].paFixedTests[iTest].rc;
            GenerateBinaryWrite(&BinOut, &Test, sizeof(Test));
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }
    return RTEXITCODE_SUCCESS;
}
#endif

static uint64_t MulDivU8Bench(uint32_t cIterations, PFNIEMAIMPLMULDIVU8 pfn, MULDIVU8_TEST_T const *pEntry)
{
    uint32_t const fEflIn = pEntry->fEflIn;
    uint16_t const uDstIn = pEntry->uDstIn;
    uint8_t  const uSrcIn = pEntry->uSrcIn;
    cIterations /= 4;
    RTThreadYield();
    uint64_t const nsStart = RTTimeNanoTS();
    for (uint32_t i = 0; i < cIterations; i++)
    {
        uint16_t uBenchDst = uDstIn;
        pfn(&uBenchDst, uSrcIn, fEflIn);

        uBenchDst = uDstIn;
        pfn(&uBenchDst, uSrcIn, fEflIn);

        uBenchDst = uDstIn;
        pfn(&uBenchDst, uSrcIn, fEflIn);

        uBenchDst = uDstIn;
        pfn(&uBenchDst, uSrcIn, fEflIn);
    }
    return RTTimeNanoTS() - nsStart;
}

static void MulDivU8Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aMulDivU8); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aMulDivU8[iFn]))
            continue;
        MULDIVU8_TEST_T const * const paTests = g_aMulDivU8[iFn].paTests;
        uint32_t const                cTests  = g_aMulDivU8[iFn].cTests;
        uint32_t const                fEflIgn = g_aMulDivU8[iFn].uExtra;
        PFNIEMAIMPLMULDIVU8           pfn     = g_aMulDivU8[iFn].pfn;
        uint32_t const                cVars   = COUNT_VARIATIONS(g_aMulDivU8[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++ )
            {
                uint16_t uDst = paTests[iTest].uDstIn;
                uint32_t fEfl = pfn(&uDst, paTests[iTest].uSrcIn, paTests[iTest].fEflIn);
                int      rc   = fEfl ? 0    : -1;
                fEfl          = fEfl ? fEfl : paTests[iTest].fEflIn;
                if (   uDst != paTests[iTest].uDstOut
                    || (fEfl | fEflIgn) != (paTests[iTest].fEflOut | fEflIgn)
                    || rc != paTests[iTest].rc)
                    RTTestFailed(g_hTest, "#%02u%s: efl=%#08x dst=%#06RX16 src=%#04RX8\n"
                                          "  %s-> efl=%#08x dst=%#06RX16 rc=%d\n"
                                          "%sexpected %#08x     %#06RX16    %d%s\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fEflIn, paTests[iTest].uDstIn, paTests[iTest].uSrcIn,
                                 iVar ? "  " : "", fEfl, uDst, rc,
                                 iVar ? "  " : "", paTests[iTest].fEflOut, paTests[iTest].uDstOut, paTests[iTest].rc,
                                 EFlagsDiff(fEfl | fEflIgn, paTests[iTest].fEflOut | fEflIgn));
                else
                {
                     *g_pu16 = paTests[iTest].uDstIn;
                     fEfl = pfn(g_pu16, paTests[iTest].uSrcIn, paTests[iTest].fEflIn);
                     rc   = fEfl ? 0    : -1;
                     fEfl = fEfl ? fEfl : paTests[iTest].fEflIn;
                     RTTEST_CHECK(g_hTest, *g_pu16 == paTests[iTest].uDstOut);
                     RTTEST_CHECK(g_hTest, (fEfl | fEflIgn) == (paTests[iTest].fEflOut | fEflIgn));
                     RTTEST_CHECK(g_hTest, rc == paTests[iTest].rc);
                }
            }

            /* Benchmark if all succeeded. */
            if (g_cPicoSecBenchmark && RTTestSubErrorCount(g_hTest) == 0)
            {
                uint32_t const iTest       = cTests / 2;
                uint32_t const cIterations = EstimateIterations(_64K, MulDivU8Bench(_64K, pfn, &paTests[iTest]));
                uint64_t const cNsRealRun  = MulDivU8Bench(cIterations, pfn, &paTests[iTest]);
                RTTestValueF(g_hTest, cNsRealRun * 1000 / cIterations, RTTESTUNIT_PS_PER_CALL,
                             "%s%s", g_aMulDivU8[iFn].pszName, iVar ? "-native" : "");
            }

            /* Next variation is native. */
            pfn = g_aMulDivU8[iFn].pfnNative;
        }
        FREE_DECOMPRESSED_TESTS(g_aMulDivU8[iFn]);
    }
}

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static const MULDIVU16_TEST_T g_aFixedTests_idiv_u16[] =
{
    /*                     low         high  */
    /* --- eflags ---, -- uDst1 --, -- uDst2 --, */
    /*        in, out,    in , out,    in , out, uSrcIn,  rc (0 or -1 for actual; -128 for auto) */
    { UINT32_MAX,   0, 0x0000,   0, 0x8000,   0, 0xc004,  -1   }, /* -2147483648 /-16380 = #DE (131104.00781...) */
    { UINT32_MAX,   0, 0xffff,   0, 0x7fff,   0, 0x7fff,  -1   }, /*  2147483647 / 32767 = #DE (65538.000030...) */
    { UINT32_MAX,   0, 0x8000,   0, 0x3fff,   0, 0x7fff,  -1   }, /* 0x3fff8000 / 0x7fff = #DE (0x8000) */
    { UINT32_MAX,   0, 0x7fff,   0, 0x3fff,   0, 0x7fff,   0   }, /* 0x3fff7fff / 0x7fff = 32767.99996948... */
    { UINT32_MAX,   0, 0x0000,   0, 0xc000,   0, 0x8000,  -1   }, /* -1073741824 / -32768 = #DE (0x8000) */
    { UINT32_MAX,   0, 0x0001,   0, 0xc000,   0, 0x8000,   0   }, /* -1073741823 / -32768 =  32767.999969482421875 */
};

static const MULDIVU32_TEST_T g_aFixedTests_idiv_u32[] =
{
    /*                       low              high  */
    /* --- eflags ---, ---- uDst1 ----, ---- uDst2 ----, */
    /*        in, out,        in , out,        in , out,     uSrcIn,  rc (0 or -1 for actual; -128 for auto) */
    { UINT32_MAX,   0, 0x00000000,   0, 0x80000000,   0, 0xc0000004,  -1   },
    { UINT32_MAX,   0, 0xffffffff,   0, 0x7fffffff,   0, 0x7fffffff,  -1   },
    { UINT32_MAX,   0, 0x80000000,   0, 0x3fffffff,   0, 0x7fffffff,  -1   },
    { UINT32_MAX,   0, 0x7fffffff,   0, 0x3fffffff,   0, 0x7fffffff,   0   },
    { UINT32_MAX,   0, 0x00000000,   0, 0xc0000000,   0, 0x80000000,  -1   },
    { UINT32_MAX,   0, 0x00000001,   0, 0xc0000000,   0, 0x80000000,   0   },
};

static const MULDIVU64_TEST_T g_aFixedTests_idiv_u64[] =
{
    /*                       low              high  */
    /* --- eflags ---, -------- uDst1 --------, -------- uDst2 --------, */
    /*        in, out,                in , out,                in , out,             uSrcIn,  rc (0 or -1 for actual; -128 for auto) */
    { UINT32_MAX,   0, 0x0000000000000000,   0, 0x8000000000000000,   0, 0xc000000000000004,  -1   },
    { UINT32_MAX,   0, 0xffffffffffffffff,   0, 0x7fffffffffffffff,   0, 0x7fffffffffffffff,  -1   },
    { UINT32_MAX,   0, 0x8000000000000000,   0, 0x3fffffffffffffff,   0, 0x7fffffffffffffff,  -1   },
    { UINT32_MAX,   0, 0x7fffffffffffffff,   0, 0x3fffffffffffffff,   0, 0x7fffffffffffffff,   0   },
    { UINT32_MAX,   0, 0x0000000000000000,   0, 0xc000000000000000,   0, 0x8000000000000000,  -1   },
    { UINT32_MAX,   0, 0x0000000000000001,   0, 0xc000000000000000,   0, 0x8000000000000000,   0   },
};

# define GEN_MULDIV(a_cBits, a_Fmt, a_TestType, a_aSubTests) \
DUMP_ALL_FN(MulDivU ## a_cBits, a_aSubTests) \
static RTEXITCODE MulDivU ## a_cBits ## Generate(uint32_t cTests, const char * const * papszNameFmts) \
{ \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (   a_aSubTests[iFn].idxCpuEflFlavour != IEMTARGETCPU_EFL_BEHAVIOR_NATIVE \
            && a_aSubTests[iFn].idxCpuEflFlavour != g_idxCpuEflFlavour) \
            continue; \
        IEMBINARYOUTPUT BinOut; \
        a_TestType      Test; \
        RT_ZERO(Test); /* 64-bit variant contains alignment padding */ \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, a_aSubTests[iFn]), RTEXITCODE_FAILURE); \
        for (uint32_t iTest = 0; iTest < cTests; iTest++ ) \
        { \
            Test.fEflIn    = RandEFlags(); \
            Test.uDst1In   = RandU ## a_cBits ## Dst(iTest); \
            Test.uDst1Out  = Test.uDst1In; \
            Test.uDst2In   = RandU ## a_cBits ## Dst(iTest); \
            Test.uDst2Out  = Test.uDst2In; \
            Test.uSrcIn    = RandU ## a_cBits ## Src(iTest); \
            uint32_t const fEflRet = a_aSubTests[iFn].pfnNative(&Test.uDst1Out, &Test.uDst2Out, Test.uSrcIn, Test.fEflIn); \
            Test.fEflOut   = fEflRet ? fEflRet : Test.fEflIn; \
            Test.rc        = fEflRet ? 0       : -1; \
            GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
        } \
        for (uint32_t iTest = 0; iTest < a_aSubTests[iFn].cFixedTests; iTest++ ) \
        { \
            Test.fEflIn    = a_aSubTests[iFn].paFixedTests[iTest].fEflIn == UINT32_MAX ? RandEFlags() \
                           : a_aSubTests[iFn].paFixedTests[iTest].fEflIn; \
            Test.uDst1In   = a_aSubTests[iFn].paFixedTests[iTest].uDst1In; \
            Test.uDst1Out  = Test.uDst1In; \
            Test.uDst2In   = a_aSubTests[iFn].paFixedTests[iTest].uDst2In; \
            Test.uDst2Out  = Test.uDst2In; \
            Test.uSrcIn    = a_aSubTests[iFn].paFixedTests[iTest].uSrcIn; \
            uint32_t const fEflRet = a_aSubTests[iFn].pfnNative(&Test.uDst1Out, &Test.uDst2Out, Test.uSrcIn, Test.fEflIn); \
            Test.fEflOut   = fEflRet ? fEflRet : Test.fEflIn; \
            Test.rc        = fEflRet ? 0       : -1; \
            if (a_aSubTests[iFn].paFixedTests[iTest].rc == 0 || a_aSubTests[iFn].paFixedTests[iTest].rc == -1) \
                Test.rc = a_aSubTests[iFn].paFixedTests[iTest].rc; \
            GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
        } \
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
    } \
    return RTEXITCODE_SUCCESS; \
}
#else
# define GEN_MULDIV(a_cBits, a_Fmt, a_TestType, a_aSubTests)
#endif

#define TEST_MULDIV(a_cBits, a_uType, a_Fmt, a_TestType, a_SubTestType, a_aSubTests) \
TYPEDEF_SUBTEST_TYPE(a_SubTestType, a_TestType, PFNIEMAIMPLMULDIVU ## a_cBits); \
static a_SubTestType a_aSubTests [] = \
{ \
    ENTRY_BIN_AMD_EX(mul_u ## a_cBits,        X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF, 0), \
    ENTRY_BIN_INTEL_EX(mul_u ## a_cBits,      X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF, 0), \
    ENTRY_BIN_AMD_EX(imul_u ## a_cBits,       X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF, 0), \
    ENTRY_BIN_INTEL_EX(imul_u ## a_cBits,     X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF, 0), \
    ENTRY_BIN_AMD_EX(div_u ## a_cBits,        X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF | X86_EFL_OF, 0), \
    ENTRY_BIN_INTEL_EX(div_u ## a_cBits,      X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF | X86_EFL_OF, 0), \
    ENTRY_BIN_FIX_AMD_EX(idiv_u ## a_cBits,   X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF | X86_EFL_OF, 0), \
    ENTRY_BIN_FIX_INTEL_EX(idiv_u ## a_cBits, X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF | X86_EFL_OF, 0), \
}; \
\
GEN_MULDIV(a_cBits, a_Fmt, a_TestType, a_aSubTests) \
\
static uint64_t MulDivU ## a_cBits ## Bench(uint32_t cIterations, PFNIEMAIMPLMULDIVU ## a_cBits pfn, a_TestType const *pEntry) \
{ \
    uint32_t const fEflIn  = pEntry->fEflIn; \
    a_uType  const uDst1In = pEntry->uDst1In; \
    a_uType  const uDst2In = pEntry->uDst2In; \
    a_uType  const uSrcIn  = pEntry->uSrcIn; \
    cIterations /= 4; \
    RTThreadYield(); \
    uint64_t const nsStart = RTTimeNanoTS(); \
    for (uint32_t i = 0; i < cIterations; i++) \
    { \
        a_uType  uBenchDst1 = uDst1In;  \
        a_uType  uBenchDst2 = uDst2In;  \
        pfn(&uBenchDst1, &uBenchDst2, uSrcIn, fEflIn); \
        \
        uBenchDst1 = uDst1In; \
        uBenchDst2 = uDst2In; \
        pfn(&uBenchDst1, &uBenchDst2, uSrcIn, fEflIn); \
        \
        uBenchDst1 = uDst1In; \
        uBenchDst2 = uDst2In; \
        pfn(&uBenchDst1, &uBenchDst2, uSrcIn, fEflIn); \
        \
        uBenchDst1 = uDst1In; \
        uBenchDst2 = uDst2In; \
        pfn(&uBenchDst1, &uBenchDst2, uSrcIn, fEflIn); \
    } \
    return RTTimeNanoTS() - nsStart; \
} \
\
static void MulDivU ## a_cBits ## Test(void) \
{ \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_aSubTests[iFn])) \
            continue; \
        a_TestType const * const      paTests = a_aSubTests[iFn].paTests; \
        uint32_t const                cTests  = a_aSubTests[iFn].cTests; \
        uint32_t const                fEflIgn = a_aSubTests[iFn].uExtra; \
        PFNIEMAIMPLMULDIVU ## a_cBits pfn     = a_aSubTests[iFn].pfn; \
        uint32_t const                cVars   = COUNT_VARIATIONS(a_aSubTests[iFn]); \
        if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
        for (uint32_t iVar = 0; iVar < cVars; iVar++) \
        { \
            for (uint32_t iTest = 0; iTest < cTests; iTest++ ) \
            { \
                a_uType  uDst1 = paTests[iTest].uDst1In; \
                a_uType  uDst2 = paTests[iTest].uDst2In; \
                uint32_t fEfl = pfn(&uDst1, &uDst2, paTests[iTest].uSrcIn, paTests[iTest].fEflIn); \
                int      rc   = fEfl ? 0    : -1; \
                fEfl          = fEfl ? fEfl : paTests[iTest].fEflIn; \
                if (   uDst1 != paTests[iTest].uDst1Out \
                    || uDst2 != paTests[iTest].uDst2Out \
                    || (fEfl | fEflIgn) != (paTests[iTest].fEflOut | fEflIgn)\
                    || rc    != paTests[iTest].rc) \
                    RTTestFailed(g_hTest, "#%04u%s: efl=%#010x dst1=" a_Fmt " dst2=" a_Fmt " src=" a_Fmt "\n" \
                                           "      -> efl=%#010x dst1=" a_Fmt  " dst2=" a_Fmt " rc=%d\n" \
                                           "    expected %#010x      " a_Fmt  "      " a_Fmt "    %d%s -%s%s%s\n", \
                                 iTest, iVar == 0 ? "  " : "/n", \
                                 paTests[iTest].fEflIn, paTests[iTest].uDst1In, paTests[iTest].uDst2In, paTests[iTest].uSrcIn, \
                                 fEfl, uDst1, uDst2, rc, \
                                 paTests[iTest].fEflOut, paTests[iTest].uDst1Out, paTests[iTest].uDst2Out, paTests[iTest].rc, \
                                 EFlagsDiff(fEfl | fEflIgn, paTests[iTest].fEflOut | fEflIgn), \
                                 uDst1 != paTests[iTest].uDst1Out ? " dst1" : "", uDst2 != paTests[iTest].uDst2Out ? " dst2" : "", \
                                 (fEfl | fEflIgn) != (paTests[iTest].fEflOut | fEflIgn) ? " eflags" : ""); \
                else \
                { \
                     *g_pu ## a_cBits        = paTests[iTest].uDst1In; \
                     *g_pu ## a_cBits ## Two = paTests[iTest].uDst2In; \
                     fEfl = pfn(g_pu ## a_cBits, g_pu ## a_cBits ## Two, paTests[iTest].uSrcIn, paTests[iTest].fEflIn); \
                     rc   = fEfl ? 0    : -1; \
                     fEfl = fEfl ? fEfl : paTests[iTest].fEflIn; \
                     RTTEST_CHECK(g_hTest, *g_pu ## a_cBits        == paTests[iTest].uDst1Out); \
                     RTTEST_CHECK(g_hTest, *g_pu ## a_cBits ## Two == paTests[iTest].uDst2Out); \
                     RTTEST_CHECK(g_hTest, (fEfl | fEflIgn)        == (paTests[iTest].fEflOut | fEflIgn)); \
                     RTTEST_CHECK(g_hTest, rc                      == paTests[iTest].rc); \
                } \
            } \
            \
            /* Benchmark if all succeeded. */ \
            if (g_cPicoSecBenchmark && RTTestSubErrorCount(g_hTest) == 0) \
            { \
                uint32_t const iTest       = cTests / 2; \
                uint32_t const cIterations = EstimateIterations(_64K, MulDivU ## a_cBits ## Bench(_64K, pfn, &paTests[iTest])); \
                uint64_t const cNsRealRun  = MulDivU ## a_cBits ## Bench(cIterations, pfn, &paTests[iTest]); \
                RTTestValueF(g_hTest, cNsRealRun * 1000 / cIterations, RTTESTUNIT_PS_PER_CALL, \
                             "%s%s", a_aSubTests[iFn].pszName, iVar ? "-native" : ""); \
            } \
            \
            /* Next variation is native. */ \
            pfn = a_aSubTests[iFn].pfnNative; \
        } \
        FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
    } \
}                   //1068553096 = 0x3FB0D388 (1068553096)
TEST_MULDIV(16, uint16_t, "%#06RX16",  MULDIVU16_TEST_T, INT_MULDIV_U16_T, g_aMulDivU16)
TEST_MULDIV(32, uint32_t, "%#010RX32", MULDIVU32_TEST_T, INT_MULDIV_U32_T, g_aMulDivU32)
TEST_MULDIV(64, uint64_t, "%#018RX64", MULDIVU64_TEST_T, INT_MULDIV_U64_T, g_aMulDivU64)

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE MulDivGenerate(uint32_t cTests, const char * const * papszNameFmts)
{
    RTEXITCODE rcExit = MulDivU8Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = MulDivU16Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = MulDivU32Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = MulDivU64Generate(cTests, papszNameFmts);
    return rcExit;
}

static RTEXITCODE MulDivDumpAll(const char * const * papszNameFmts)
{
    RTEXITCODE rcExit = MulDivU8DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = MulDivU16DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = MulDivU32DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = MulDivU64DumpAll(papszNameFmts);
    return rcExit;
}
#endif

static void MulDivTest(void)
{
    MulDivU8Test();
    MulDivU16Test();
    MulDivU32Test();
    MulDivU64Test();
}


/*
 * BSWAP
 */
static void BswapTest(void)
{
    if (SubTestAndCheckIfEnabled("bswap_u16"))
    {
        *g_pu32 = UINT32_C(0x12345678);
        iemAImpl_bswap_u16(g_pu32);
#if 0
        RTTEST_CHECK_MSG(g_hTest, *g_pu32 == UINT32_C(0x12347856), (g_hTest, "*g_pu32=%#RX32\n", *g_pu32));
#else
        RTTEST_CHECK_MSG(g_hTest, *g_pu32 == UINT32_C(0x12340000), (g_hTest, "*g_pu32=%#RX32\n", *g_pu32));
#endif
        *g_pu32 = UINT32_C(0xffff1122);
        iemAImpl_bswap_u16(g_pu32);
#if 0
        RTTEST_CHECK_MSG(g_hTest, *g_pu32 == UINT32_C(0xffff2211), (g_hTest, "*g_pu32=%#RX32\n", *g_pu32));
#else
        RTTEST_CHECK_MSG(g_hTest, *g_pu32 == UINT32_C(0xffff0000), (g_hTest, "*g_pu32=%#RX32\n", *g_pu32));
#endif
    }

    if (SubTestAndCheckIfEnabled("bswap_u32"))
    {
        *g_pu32 = UINT32_C(0x12345678);
        iemAImpl_bswap_u32(g_pu32);
        RTTEST_CHECK(g_hTest, *g_pu32 == UINT32_C(0x78563412));
    }

    if (SubTestAndCheckIfEnabled("bswap_u64"))
    {
        *g_pu64 = UINT64_C(0x0123456789abcdef);
        iemAImpl_bswap_u64(g_pu64);
        RTTEST_CHECK(g_hTest, *g_pu64 == UINT64_C(0xefcdab8967452301));
    }
}



/*********************************************************************************************************************************
*   Floating point (x87 style)                                                                                                   *
*********************************************************************************************************************************/

/*
 * FPU constant loading.
 */
TYPEDEF_SUBTEST_TYPE(FPU_LD_CONST_T, FPU_LD_CONST_TEST_T, PFNIEMAIMPLFPUR80LDCONST);

static FPU_LD_CONST_T g_aFpuLdConst[] =
{
    ENTRY_BIN(fld1),
    ENTRY_BIN(fldl2t),
    ENTRY_BIN(fldl2e),
    ENTRY_BIN(fldpi),
    ENTRY_BIN(fldlg2),
    ENTRY_BIN(fldln2),
    ENTRY_BIN(fldz),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE FpuLdConstGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuLdConst); iFn++)
    {
        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aFpuLdConst[iFn]), RTEXITCODE_FAILURE);
        for (uint32_t iTest = 0; iTest < cTests; iTest += 4)
        {
            State.FCW = RandFcw();
            State.FSW = RandFsw();

            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
            {
                IEMFPURESULT Res = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                State.FCW = (State.FCW & ~X86_FCW_RC_MASK) | (iRounding << X86_FCW_RC_SHIFT);
                g_aFpuLdConst[iFn].pfn(&State, &Res);
                FPU_LD_CONST_TEST_T const Test = { State.FCW, State.FSW, Res.FSW, Res.r80Result };
                GenerateBinaryWrite(&BinOut, &Test, sizeof(Test));
            }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }
    return RTEXITCODE_SUCCESS;
}
DUMP_ALL_FN(FpuLdConst, g_aFpuLdConst)
#endif

static void FpuLdConstTest(void)
{
    /*
     * Inputs:
     *      - FSW: C0, C1, C2, C3
     *      - FCW: Exception masks, Precision control, Rounding control.
     *
     * C1 set to 1 on stack overflow, zero otherwise.  C0, C2, and C3 are "undefined".
     */
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuLdConst); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aFpuLdConst[iFn]))
            continue;

        FPU_LD_CONST_TEST_T const  *paTests = g_aFpuLdConst[iFn].paTests;
        uint32_t const              cTests  = g_aFpuLdConst[iFn].cTests;
        PFNIEMAIMPLFPUR80LDCONST    pfn     = g_aFpuLdConst[iFn].pfn;
        uint32_t const              cVars   = COUNT_VARIATIONS(g_aFpuLdConst[iFn]); \
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                State.FCW = paTests[iTest].fFcw;
                State.FSW = paTests[iTest].fFswIn;
                IEMFPURESULT Res = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                pfn(&State, &Res);
                if (   Res.FSW != paTests[iTest].fFswOut
                    || !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].rdResult))
                    RTTestFailed(g_hTest, "#%u%s: fcw=%#06x fsw=%#06x -> fsw=%#06x %s, expected %#06x %s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn,
                                 Res.FSW, FormatR80(&Res.r80Result),
                                 paTests[iTest].fFswOut, FormatR80(&paTests[iTest].rdResult),
                                 FswDiff(Res.FSW, paTests[iTest].fFswOut),
                                 !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].rdResult) ? " - val" : "",
                                 FormatFcw(paTests[iTest].fFcw) );
            }
            pfn = g_aFpuLdConst[iFn].pfnNative;
        }

        FREE_DECOMPRESSED_TESTS(g_aFpuLdConst[iFn]);
    }
}


/*
 * Load floating point values from memory.
 */
#ifdef TSTIEMAIMPL_WITH_GENERATOR
# define GEN_FPU_LOAD(a_cBits, a_rdTypeIn, a_aSubTests, a_TestType) \
static RTEXITCODE FpuLdR ## a_cBits ## Generate(uint32_t cTests, const char * const *papszNameFmts) \
{ \
    X86FXSTATE State; \
    RT_ZERO(State); \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        IEMBINARYOUTPUT BinOut; \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, a_aSubTests[iFn]), RTEXITCODE_FAILURE); \
        for (uint32_t iTest = 0; iTest < cTests; iTest++) \
        { \
            State.FCW = RandFcw(); \
            State.FSW = RandFsw(); \
            a_rdTypeIn InVal = RandR ## a_cBits ## Src(iTest); \
            \
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++) \
            { \
                IEMFPURESULT Res = { RTFLOAT80U_INIT(0, 0, 0), 0 }; \
                State.FCW = (State.FCW & ~X86_FCW_RC_MASK) | (iRounding << X86_FCW_RC_SHIFT); \
                a_aSubTests[iFn].pfn(&State, &Res, &InVal); \
                a_TestType const Test = { State.FCW, State.FSW, Res.FSW, Res.r80Result, InVal }; \
                GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
            } \
        } \
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
    } \
    return RTEXITCODE_SUCCESS; \
} \
DUMP_ALL_FN(FpuLdR ## a_cBits, a_aSubTests)
#else
# define GEN_FPU_LOAD(a_cBits, a_rdTypeIn, a_aSubTests, a_TestType)
#endif

#define TEST_FPU_LOAD(a_cBits, a_rdTypeIn, a_SubTestType, a_aSubTests, a_TestType) \
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPULDR80FROM ## a_cBits,(PCX86FXSTATE, PIEMFPURESULT, PC ## a_rdTypeIn)); \
typedef FNIEMAIMPLFPULDR80FROM ## a_cBits *PFNIEMAIMPLFPULDR80FROM ## a_cBits; \
TYPEDEF_SUBTEST_TYPE(a_SubTestType, a_TestType, PFNIEMAIMPLFPULDR80FROM ## a_cBits); \
\
static a_SubTestType a_aSubTests[] = \
{ \
    ENTRY_BIN(RT_CONCAT(fld_r80_from_r,a_cBits)) \
}; \
GEN_FPU_LOAD(a_cBits, a_rdTypeIn, a_aSubTests, a_TestType) \
\
static void FpuLdR ## a_cBits ## Test(void) \
{ \
    X86FXSTATE State; \
    RT_ZERO(State); \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_aSubTests[iFn])) \
            continue; \
        \
        a_TestType const           * const paTests = a_aSubTests[iFn].paTests; \
        uint32_t const                     cTests  = a_aSubTests[iFn].cTests; \
        PFNIEMAIMPLFPULDR80FROM ## a_cBits pfn     = a_aSubTests[iFn].pfn; \
        uint32_t const                     cVars   = COUNT_VARIATIONS(a_aSubTests[iFn]); \
        if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
        for (uint32_t iVar = 0; iVar < cVars; iVar++) \
        { \
            for (uint32_t iTest = 0; iTest < cTests; iTest++) \
            { \
                a_rdTypeIn const InVal = paTests[iTest].InVal; \
                State.FCW = paTests[iTest].fFcw; \
                State.FSW = paTests[iTest].fFswIn; \
                IEMFPURESULT Res = { RTFLOAT80U_INIT(0, 0, 0), 0 }; \
                pfn(&State, &Res, &InVal); \
                if (   Res.FSW != paTests[iTest].fFswOut \
                    || !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].rdResult)) \
                    RTTestFailed(g_hTest, "#%03u%s: fcw=%#06x fsw=%#06x in=%s\n" \
                                          "%s              -> fsw=%#06x    %s\n" \
                                          "%s            expected %#06x    %s%s%s (%s)\n", \
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn, \
                                 FormatR ## a_cBits(&paTests[iTest].InVal), \
                                 iVar ? "  " : "", Res.FSW, FormatR80(&Res.r80Result), \
                                 iVar ? "  " : "", paTests[iTest].fFswOut, FormatR80(&paTests[iTest].rdResult), \
                                 FswDiff(Res.FSW, paTests[iTest].fFswOut), \
                                 !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].rdResult) ? " - val" : "", \
                                 FormatFcw(paTests[iTest].fFcw) ); \
            } \
            pfn = a_aSubTests[iFn].pfnNative; \
        } \
        \
        FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
    } \
}

TEST_FPU_LOAD(80, RTFLOAT80U, FPU_LD_R80_T, g_aFpuLdR80, FPU_R80_IN_TEST_T)
TEST_FPU_LOAD(64, RTFLOAT64U, FPU_LD_R64_T, g_aFpuLdR64, FPU_R64_IN_TEST_T)
TEST_FPU_LOAD(32, RTFLOAT32U, FPU_LD_R32_T, g_aFpuLdR32, FPU_R32_IN_TEST_T)

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE FpuLdMemGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    RTEXITCODE rcExit = FpuLdR80Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuLdR64Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuLdR32Generate(cTests, papszNameFmts);
    return rcExit;
}

static RTEXITCODE FpuLdMemDumpAll(const char * const *papszNameFmts)
{
    RTEXITCODE rcExit = FpuLdR80DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuLdR64DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuLdR32DumpAll(papszNameFmts);
    return rcExit;
}
#endif

static void FpuLdMemTest(void)
{
    FpuLdR80Test();
    FpuLdR64Test();
    FpuLdR32Test();
}


/*
 * Load integer values from memory.
 */
#ifdef TSTIEMAIMPL_WITH_GENERATOR
# define GEN_FPU_LOAD_INT(a_cBits, a_iTypeIn, a_szFmtIn, a_aSubTests, a_TestType) \
static RTEXITCODE FpuLdI ## a_cBits ## Generate(uint32_t cTests, const char * const *papszNameFmts) \
{ \
    X86FXSTATE State; \
    RT_ZERO(State); \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        IEMBINARYOUTPUT BinOut; \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, a_aSubTests[iFn]), RTEXITCODE_FAILURE); \
        for (uint32_t iTest = 0; iTest < cTests; iTest++) \
        { \
            State.FCW = RandFcw(); \
            State.FSW = RandFsw(); \
            a_iTypeIn InVal = (a_iTypeIn)RandU ## a_cBits ## Src(iTest); \
            \
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++) \
            { \
                IEMFPURESULT Res = { RTFLOAT80U_INIT(0, 0, 0), 0 }; \
                State.FCW = (State.FCW & ~X86_FCW_RC_MASK) | (iRounding << X86_FCW_RC_SHIFT); \
                a_aSubTests[iFn].pfn(&State, &Res, &InVal); \
                a_TestType const Test = { State.FCW, State.FSW, Res.FSW, Res.r80Result }; \
                GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
            } \
        } \
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
    } \
    return RTEXITCODE_SUCCESS; \
} \
DUMP_ALL_FN(FpuLdI ## a_cBits, a_aSubTests)
#else
# define GEN_FPU_LOAD_INT(a_cBits, a_iTypeIn, a_szFmtIn, a_aSubTests, a_TestType)
#endif

#define TEST_FPU_LOAD_INT(a_cBits, a_iTypeIn, a_szFmtIn, a_SubTestType, a_aSubTests, a_TestType) \
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPULDR80FROMI ## a_cBits,(PCX86FXSTATE, PIEMFPURESULT, a_iTypeIn const *)); \
typedef FNIEMAIMPLFPULDR80FROMI ## a_cBits *PFNIEMAIMPLFPULDR80FROMI ## a_cBits; \
TYPEDEF_SUBTEST_TYPE(a_SubTestType, a_TestType, PFNIEMAIMPLFPULDR80FROMI ## a_cBits); \
\
static a_SubTestType a_aSubTests[] = \
{ \
    ENTRY_BIN(RT_CONCAT(fild_r80_from_i,a_cBits)) \
}; \
GEN_FPU_LOAD_INT(a_cBits, a_iTypeIn, a_szFmtIn, a_aSubTests, a_TestType) \
\
static void FpuLdI ## a_cBits ## Test(void) \
{ \
    X86FXSTATE State; \
    RT_ZERO(State); \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_aSubTests[iFn])) \
            continue; \
        \
        a_TestType const            * const paTests = a_aSubTests[iFn].paTests; \
        uint32_t const                      cTests  = a_aSubTests[iFn].cTests; \
        PFNIEMAIMPLFPULDR80FROMI ## a_cBits pfn     = a_aSubTests[iFn].pfn; \
        uint32_t const                      cVars   = COUNT_VARIATIONS(a_aSubTests[iFn]); \
        if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
        for (uint32_t iVar = 0; iVar < cVars; iVar++) \
        { \
            for (uint32_t iTest = 0; iTest < cTests; iTest++) \
            { \
                a_iTypeIn const iInVal = paTests[iTest].iInVal; \
                State.FCW = paTests[iTest].fFcw; \
                State.FSW = paTests[iTest].fFswIn; \
                IEMFPURESULT Res = { RTFLOAT80U_INIT(0, 0, 0), 0 }; \
                pfn(&State, &Res, &iInVal); \
                if (   Res.FSW != paTests[iTest].fFswOut \
                    || !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].rdResult)) \
                    RTTestFailed(g_hTest, "#%03u%s: fcw=%#06x fsw=%#06x in=" a_szFmtIn "\n" \
                                          "%s              -> fsw=%#06x    %s\n" \
                                          "%s            expected %#06x    %s%s%s (%s)\n", \
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn, paTests[iTest].iInVal, \
                                 iVar ? "  " : "", Res.FSW, FormatR80(&Res.r80Result), \
                                 iVar ? "  " : "", paTests[iTest].fFswOut, FormatR80(&paTests[iTest].rdResult), \
                                 FswDiff(Res.FSW, paTests[iTest].fFswOut), \
                                 !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].rdResult) ? " - val" : "", \
                                 FormatFcw(paTests[iTest].fFcw) ); \
            } \
            pfn = a_aSubTests[iFn].pfnNative; \
        } \
        FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
    } \
}

TEST_FPU_LOAD_INT(64, int64_t, "%RI64", FPU_LD_I64_T, g_aFpuLdU64, FPU_I64_IN_TEST_T)
TEST_FPU_LOAD_INT(32, int32_t, "%RI32", FPU_LD_I32_T, g_aFpuLdU32, FPU_I32_IN_TEST_T)
TEST_FPU_LOAD_INT(16, int16_t, "%RI16", FPU_LD_I16_T, g_aFpuLdU16, FPU_I16_IN_TEST_T)

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE FpuLdIntGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    RTEXITCODE rcExit = FpuLdI64Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuLdI32Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuLdI16Generate(cTests, papszNameFmts);
    return rcExit;
}

static RTEXITCODE FpuLdIntDumpAll(const char * const *papszNameFmts)
{
    RTEXITCODE rcExit = FpuLdI64DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuLdI32DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuLdI16DumpAll(papszNameFmts);
    return rcExit;
}
#endif

static void FpuLdIntTest(void)
{
    FpuLdI64Test();
    FpuLdI32Test();
    FpuLdI16Test();
}


/*
 * Load binary coded decimal values from memory.
 */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPULDR80FROMD80,(PCX86FXSTATE, PIEMFPURESULT, PCRTPBCD80U));
typedef FNIEMAIMPLFPULDR80FROMD80 *PFNIEMAIMPLFPULDR80FROMD80;
TYPEDEF_SUBTEST_TYPE(FPU_LD_D80_T, FPU_D80_IN_TEST_T, PFNIEMAIMPLFPULDR80FROMD80);

static FPU_LD_D80_T g_aFpuLdD80[] =
{
    ENTRY_BIN(fld_r80_from_d80)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE FpuLdD80Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuLdD80); iFn++)
    {
        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aFpuLdD80[iFn]), RTEXITCODE_FAILURE);
        for (uint32_t iTest = 0; iTest < cTests; iTest++)
        {
            State.FCW = RandFcw();
            State.FSW = RandFsw();
            RTPBCD80U InVal = RandD80Src(iTest);

            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
            {
                IEMFPURESULT Res = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                State.FCW = (State.FCW & ~X86_FCW_RC_MASK) | (iRounding << X86_FCW_RC_SHIFT);
                g_aFpuLdD80[iFn].pfn(&State, &Res, &InVal);
                FPU_D80_IN_TEST_T const Test = { State.FCW, State.FSW, Res.FSW, Res.r80Result, InVal };
                GenerateBinaryWrite(&BinOut, &Test, sizeof(Test));
            }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }
    return RTEXITCODE_SUCCESS;
}
DUMP_ALL_FN(FpuLdD80, g_aFpuLdD80)
#endif

static void FpuLdD80Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuLdD80); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aFpuLdD80[iFn]))
            continue;

        FPU_D80_IN_TEST_T const * const paTests = g_aFpuLdD80[iFn].paTests;
        uint32_t const                  cTests  = g_aFpuLdD80[iFn].cTests;
        PFNIEMAIMPLFPULDR80FROMD80      pfn     = g_aFpuLdD80[iFn].pfn;
        uint32_t const                  cVars   = COUNT_VARIATIONS(g_aFpuLdD80[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTPBCD80U const InVal = paTests[iTest].InVal;
                State.FCW = paTests[iTest].fFcw;
                State.FSW = paTests[iTest].fFswIn;
                IEMFPURESULT Res = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                pfn(&State, &Res, &InVal);
                if (   Res.FSW != paTests[iTest].fFswOut
                    || !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].rdResult))
                    RTTestFailed(g_hTest, "#%03u%s: fcw=%#06x fsw=%#06x in=%s\n"
                                          "%s              -> fsw=%#06x    %s\n"
                                          "%s            expected %#06x    %s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn,
                                 FormatD80(&paTests[iTest].InVal),
                                 iVar ? "  " : "", Res.FSW, FormatR80(&Res.r80Result),
                                 iVar ? "  " : "", paTests[iTest].fFswOut, FormatR80(&paTests[iTest].rdResult),
                                 FswDiff(Res.FSW, paTests[iTest].fFswOut),
                                 !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].rdResult) ? " - val" : "",
                                 FormatFcw(paTests[iTest].fFcw) );
            }
            pfn = g_aFpuLdD80[iFn].pfnNative;
        }

        FREE_DECOMPRESSED_TESTS(g_aFpuLdD80[iFn]);
    }
}


/*
 * Store values floating point values to memory.
 */
#ifdef TSTIEMAIMPL_WITH_GENERATOR
static const RTFLOAT80U g_aFpuStR32Specials[] =
{
    RTFLOAT80U_INIT_C(0, 0xffffff8000000000, RTFLOAT80U_EXP_BIAS), /* near rounding with carry */
    RTFLOAT80U_INIT_C(1, 0xffffff8000000000, RTFLOAT80U_EXP_BIAS), /* near rounding with carry */
    RTFLOAT80U_INIT_C(0, 0xfffffe8000000000, RTFLOAT80U_EXP_BIAS), /* near rounding */
    RTFLOAT80U_INIT_C(1, 0xfffffe8000000000, RTFLOAT80U_EXP_BIAS), /* near rounding */
};
static const RTFLOAT80U g_aFpuStR64Specials[] =
{
    RTFLOAT80U_INIT_C(0, 0xfffffffffffffc00, RTFLOAT80U_EXP_BIAS), /* near rounding with carry */
    RTFLOAT80U_INIT_C(1, 0xfffffffffffffc00, RTFLOAT80U_EXP_BIAS), /* near rounding with carry */
    RTFLOAT80U_INIT_C(0, 0xfffffffffffff400, RTFLOAT80U_EXP_BIAS), /* near rounding  */
    RTFLOAT80U_INIT_C(1, 0xfffffffffffff400, RTFLOAT80U_EXP_BIAS), /* near rounding  */
    RTFLOAT80U_INIT_C(0, 0xd0b9e6fdda887400, 687 + RTFLOAT80U_EXP_BIAS), /* random example for this */
};
static const RTFLOAT80U g_aFpuStR80Specials[] =
{
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, RTFLOAT80U_EXP_BIAS), /* placeholder */
};
# define GEN_FPU_STORE(a_cBits, a_rdType, a_aSubTests, a_TestType) \
static RTEXITCODE FpuStR ## a_cBits ## Generate(uint32_t cTests, const char * const *papszNameFmts) \
{ \
    uint32_t const cTotalTests = cTests + RT_ELEMENTS(g_aFpuStR ## a_cBits ## Specials); \
    X86FXSTATE State; \
    RT_ZERO(State); \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        IEMBINARYOUTPUT BinOut; \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, a_aSubTests[iFn]), RTEXITCODE_FAILURE); \
        for (uint32_t iTest = 0; iTest < cTotalTests; iTest++) \
        { \
            uint16_t const fFcw = RandFcw(); \
            State.FSW = RandFsw(); \
            RTFLOAT80U const InVal = iTest < cTests ? RandR80Src(iTest, a_cBits) \
                                   : g_aFpuStR ## a_cBits ## Specials[iTest - cTests]; \
            \
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++) \
            { \
                /* PC doesn't influence these, so leave as is. */ \
                AssertCompile(X86_FCW_OM_BIT + 1 == X86_FCW_UM_BIT && X86_FCW_UM_BIT + 1 == X86_FCW_PM_BIT); \
                for (uint16_t iMask = 0; iMask < 16; iMask += 2 /*1*/) \
                { \
                    uint16_t uFswOut = 0; \
                    a_rdType OutVal; \
                    RT_ZERO(OutVal); \
                    memset(&OutVal, 0xfe, sizeof(OutVal)); \
                    State.FCW = (fFcw & ~(X86_FCW_RC_MASK | X86_FCW_OM | X86_FCW_UM | X86_FCW_PM)) \
                              | (iRounding  << X86_FCW_RC_SHIFT); \
                    /*if (iMask & 1) State.FCW ^= X86_FCW_MASK_ALL;*/ \
                    State.FCW |= (iMask >> 1) << X86_FCW_OM_BIT; \
                    a_aSubTests[iFn].pfn(&State, &uFswOut, &OutVal, &InVal); \
                    a_TestType const Test = { State.FCW, State.FSW, uFswOut, InVal, OutVal }; \
                    GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
                } \
            } \
        } \
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
    } \
    return RTEXITCODE_SUCCESS; \
} \
DUMP_ALL_FN(FpuStR ## a_cBits, a_aSubTests)
#else
# define GEN_FPU_STORE(a_cBits, a_rdType, a_aSubTests, a_TestType)
#endif

#define TEST_FPU_STORE(a_cBits, a_rdType, a_SubTestType, a_aSubTests, a_TestType) \
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUSTR80TOR ## a_cBits,(PCX86FXSTATE, uint16_t *, \
                                                                   PRTFLOAT ## a_cBits ## U, PCRTFLOAT80U)); \
typedef FNIEMAIMPLFPUSTR80TOR ## a_cBits *PFNIEMAIMPLFPUSTR80TOR ## a_cBits; \
TYPEDEF_SUBTEST_TYPE(a_SubTestType, a_TestType, PFNIEMAIMPLFPUSTR80TOR ## a_cBits); \
\
static a_SubTestType a_aSubTests[] = \
{ \
    ENTRY_BIN(RT_CONCAT(fst_r80_to_r,a_cBits)) \
}; \
GEN_FPU_STORE(a_cBits, a_rdType, a_aSubTests, a_TestType) \
\
static void FpuStR ## a_cBits ## Test(void) \
{ \
    X86FXSTATE State; \
    RT_ZERO(State); \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_aSubTests[iFn])) \
            continue; \
        \
        a_TestType const          * const paTests = a_aSubTests[iFn].paTests; \
        uint32_t const                    cTests  = a_aSubTests[iFn].cTests; \
        PFNIEMAIMPLFPUSTR80TOR ## a_cBits pfn     = a_aSubTests[iFn].pfn; \
        uint32_t const                    cVars   = COUNT_VARIATIONS(a_aSubTests[iFn]); \
        if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
        for (uint32_t iVar = 0; iVar < cVars; iVar++) \
        { \
            for (uint32_t iTest = 0; iTest < cTests; iTest++) \
            { \
                RTFLOAT80U const InVal   = paTests[iTest].InVal; \
                uint16_t         uFswOut = 0; \
                a_rdType         OutVal; \
                RT_ZERO(OutVal); \
                memset(&OutVal, 0xfe, sizeof(OutVal)); \
                State.FCW = paTests[iTest].fFcw; \
                State.FSW = paTests[iTest].fFswIn; \
                pfn(&State, &uFswOut, &OutVal, &InVal); \
                if (   uFswOut != paTests[iTest].fFswOut \
                    || !RTFLOAT ## a_cBits ## U_ARE_IDENTICAL(&OutVal, &paTests[iTest].OutVal)) \
                    RTTestFailed(g_hTest, "#%04u%s: fcw=%#06x fsw=%#06x in=%s\n" \
                                          "%s               -> fsw=%#06x    %s\n" \
                                          "%s             expected %#06x    %s%s%s (%s)\n", \
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn, \
                                 FormatR80(&paTests[iTest].InVal), \
                                 iVar ? "  " : "", uFswOut, FormatR ## a_cBits(&OutVal), \
                                 iVar ? "  " : "", paTests[iTest].fFswOut, FormatR ## a_cBits(&paTests[iTest].OutVal), \
                                 FswDiff(uFswOut, paTests[iTest].fFswOut), \
                                 !RTFLOAT ## a_cBits ## U_ARE_IDENTICAL(&OutVal, &paTests[iTest].OutVal) ? " - val" : "", \
                                 FormatFcw(paTests[iTest].fFcw) ); \
            } \
            pfn = a_aSubTests[iFn].pfnNative; \
        } \
        FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
    } \
}

TEST_FPU_STORE(80, RTFLOAT80U, FPU_ST_R80_T, g_aFpuStR80, FPU_ST_R80_TEST_T)
TEST_FPU_STORE(64, RTFLOAT64U, FPU_ST_R64_T, g_aFpuStR64, FPU_ST_R64_TEST_T)
TEST_FPU_STORE(32, RTFLOAT32U, FPU_ST_R32_T, g_aFpuStR32, FPU_ST_R32_TEST_T)

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE FpuStMemGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    RTEXITCODE rcExit = FpuStR80Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuStR64Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuStR32Generate(cTests, papszNameFmts);
    return rcExit;
}

static RTEXITCODE FpuStMemDumpAll(const char * const *papszNameFmts)
{
    RTEXITCODE rcExit = FpuStR80DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuStR64DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuStR32DumpAll(papszNameFmts);
    return rcExit;
}
#endif

static void FpuStMemTest(void)
{
    FpuStR80Test();
    FpuStR64Test();
    FpuStR32Test();
}


/*
 * Store integer values to memory or register.
 */
TYPEDEF_SUBTEST_TYPE(FPU_ST_I16_T, FPU_ST_I16_TEST_T, PFNIEMAIMPLFPUSTR80TOI16);
TYPEDEF_SUBTEST_TYPE(FPU_ST_I32_T, FPU_ST_I32_TEST_T, PFNIEMAIMPLFPUSTR80TOI32);
TYPEDEF_SUBTEST_TYPE(FPU_ST_I64_T, FPU_ST_I64_TEST_T, PFNIEMAIMPLFPUSTR80TOI64);

static FPU_ST_I16_T g_aFpuStI16[] =
{
    ENTRY_BIN(fist_r80_to_i16),
    ENTRY_BIN_AMD(  fistt_r80_to_i16, 0),
    ENTRY_BIN_INTEL(fistt_r80_to_i16, 0),
};
static FPU_ST_I32_T g_aFpuStI32[] =
{
    ENTRY_BIN(fist_r80_to_i32),
    ENTRY_BIN(fistt_r80_to_i32),
};
static FPU_ST_I64_T g_aFpuStI64[] =
{
    ENTRY_BIN(fist_r80_to_i64),
    ENTRY_BIN(fistt_r80_to_i64),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static const RTFLOAT80U g_aFpuStI16Specials[] = /* 16-bit variant borrows properties from the 32-bit one, thus all this stuff. */
{
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 13 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xfffffffffffffff0, 13 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000000000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000080000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000080000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000100000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000100000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000200000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000200000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000400000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000400000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000800000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000800000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000ffffffffffff, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8001000000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8001000000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xfffffffffffffff0, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0xfffffffffffffff0, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xffff800000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xffff000000000000, 14 + RTFLOAT80U_EXP_BIAS), /* overflow to min/nan */
    RTFLOAT80U_INIT_C(0, 0xfffe000000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0xffff800000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0xffff000000000000, 14 + RTFLOAT80U_EXP_BIAS), /* min */
    RTFLOAT80U_INIT_C(1, 0xfffe000000000000, 14 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 15 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xfffffffffffffff0, 15 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 16 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 17 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 20 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 24 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 28 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 30 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000000000000000, 30 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xfffffffffffffff0, 30 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0xfffffffffffffff0, 30 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000000000000000, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000001, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000000000000001, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000ffffffffffff, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000ffffffffffff, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8001000000000000, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8001000000000000, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xfffffffffffffff0, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0xfffffffffffffff0, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 32 + RTFLOAT80U_EXP_BIAS),
};
static const RTFLOAT80U g_aFpuStI32Specials[] =
{
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 30 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000000000000000, 30 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xfffffffffffffff0, 30 + RTFLOAT80U_EXP_BIAS), /* overflow to min/nan */
    RTFLOAT80U_INIT_C(1, 0xfffffffffffffff0, 30 + RTFLOAT80U_EXP_BIAS), /* min */
    RTFLOAT80U_INIT_C(0, 0xffffffff80000000, 30 + RTFLOAT80U_EXP_BIAS), /* overflow to min/nan */
    RTFLOAT80U_INIT_C(1, 0xffffffff80000000, 30 + RTFLOAT80U_EXP_BIAS), /* min */
    RTFLOAT80U_INIT_C(0, 0xffffffff00000000, 30 + RTFLOAT80U_EXP_BIAS), /* overflow to min/nan */
    RTFLOAT80U_INIT_C(1, 0xffffffff00000000, 30 + RTFLOAT80U_EXP_BIAS), /* min */
    RTFLOAT80U_INIT_C(0, 0xfffffffe00000000, 30 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0xfffffffe00000000, 30 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000000000000000, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000001, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000000000000001, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xfffffffffffffff0, 31 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0xfffffffffffffff0, 31 + RTFLOAT80U_EXP_BIAS),
};
static const RTFLOAT80U g_aFpuStI64Specials[] =
{
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 61 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xffffffffffffffff, 61 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 62 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000000000000000, 62 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xfffffffffffffff0, 62 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0xfffffffffffffff0, 62 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xffffffffffffffff, 62 + RTFLOAT80U_EXP_BIAS), /* overflow to min/nan */
    RTFLOAT80U_INIT_C(1, 0xffffffffffffffff, 62 + RTFLOAT80U_EXP_BIAS), /* min */
    RTFLOAT80U_INIT_C(0, 0xfffffffffffffffe, 62 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0xfffffffffffffffe, 62 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000000, 63 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000000000000000, 63 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000001, 63 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000000000000001, 63 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0x8000000000000002, 63 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(1, 0x8000000000000002, 63 + RTFLOAT80U_EXP_BIAS),
    RTFLOAT80U_INIT_C(0, 0xfffffffffffffff0, 63 + RTFLOAT80U_EXP_BIAS),
};

# define GEN_FPU_STORE_INT(a_cBits, a_iType, a_szFmt, a_aSubTests, a_TestType) \
static RTEXITCODE FpuStI ## a_cBits ## Generate(uint32_t cTests, const char * const *papszNameFmts) \
{ \
    X86FXSTATE State; \
    RT_ZERO(State); \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        PFNIEMAIMPLFPUSTR80TOI ## a_cBits const pfn    = a_aSubTests[iFn].pfnNative \
                                                       ? a_aSubTests[iFn].pfnNative : a_aSubTests[iFn].pfn; \
        if (   a_aSubTests[iFn].idxCpuEflFlavour != IEMTARGETCPU_EFL_BEHAVIOR_NATIVE \
            && a_aSubTests[iFn].idxCpuEflFlavour != g_idxCpuEflFlavour) \
                continue; \
        \
        IEMBINARYOUTPUT BinOut; \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, a_aSubTests[iFn]), RTEXITCODE_FAILURE); \
        uint32_t const cTotalTests = cTests + RT_ELEMENTS(g_aFpuStI ## a_cBits ## Specials); \
        for (uint32_t iTest = 0; iTest < cTotalTests; iTest++) \
        { \
            uint16_t const fFcw = RandFcw(); \
            State.FSW = RandFsw(); \
            RTFLOAT80U const InVal = iTest < cTests ? RandR80Src(iTest, a_cBits, true) \
                                   : g_aFpuStI ## a_cBits ## Specials[iTest - cTests]; \
            \
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++) \
            { \
                /* PC doesn't influence these, so leave as is. */ \
                AssertCompile(X86_FCW_OM_BIT + 1 == X86_FCW_UM_BIT && X86_FCW_UM_BIT + 1 == X86_FCW_PM_BIT); \
                for (uint16_t iMask = 0; iMask < 16; iMask += 2 /*1*/) \
                { \
                    uint16_t uFswOut = 0; \
                    a_iType  iOutVal = ~(a_iType)2; \
                    State.FCW = (fFcw & ~(X86_FCW_RC_MASK | X86_FCW_OM | X86_FCW_UM | X86_FCW_PM)) \
                              | (iRounding  << X86_FCW_RC_SHIFT); \
                    /*if (iMask & 1) State.FCW ^= X86_FCW_MASK_ALL;*/ \
                    State.FCW |= (iMask >> 1) << X86_FCW_OM_BIT; \
                    pfn(&State, &uFswOut, &iOutVal, &InVal); \
                    a_TestType const Test = { State.FCW, State.FSW, uFswOut, InVal, iOutVal }; \
                    GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
                } \
            } \
        } \
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
    } \
    return RTEXITCODE_SUCCESS; \
} \
DUMP_ALL_FN(FpuStI ## a_cBits, a_aSubTests)
#else
# define GEN_FPU_STORE_INT(a_cBits, a_iType, a_szFmt, a_aSubTests, a_TestType)
#endif

#define TEST_FPU_STORE_INT(a_cBits, a_iType, a_szFmt, a_SubTestType, a_aSubTests, a_TestType) \
GEN_FPU_STORE_INT(a_cBits, a_iType, a_szFmt, a_aSubTests, a_TestType) \
\
static void FpuStI ## a_cBits ## Test(void) \
{ \
    X86FXSTATE State; \
    RT_ZERO(State); \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_aSubTests[iFn])) \
            continue; \
        \
        a_TestType const          * const paTests = a_aSubTests[iFn].paTests; \
        uint32_t const                    cTests  = a_aSubTests[iFn].cTests; \
        PFNIEMAIMPLFPUSTR80TOI ## a_cBits pfn     = a_aSubTests[iFn].pfn; \
        uint32_t const                    cVars   = COUNT_VARIATIONS(a_aSubTests[iFn]); \
        if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
        for (uint32_t iVar = 0; iVar < cVars; iVar++) \
        { \
            for (uint32_t iTest = 0; iTest < cTests; iTest++) \
            { \
                RTFLOAT80U const InVal   = paTests[iTest].InVal; \
                uint16_t         uFswOut = 0; \
                a_iType          iOutVal = ~(a_iType)2; \
                State.FCW = paTests[iTest].fFcw; \
                State.FSW = paTests[iTest].fFswIn; \
                pfn(&State, &uFswOut, &iOutVal, &InVal); \
                if (   uFswOut != paTests[iTest].fFswOut \
                    || iOutVal != paTests[iTest].iOutVal) \
                    RTTestFailed(g_hTest, "#%04u%s: fcw=%#06x fsw=%#06x in=%s\n" \
                                          "%s               -> fsw=%#06x    " a_szFmt "\n" \
                                          "%s             expected %#06x    " a_szFmt "%s%s (%s)\n", \
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn, \
                                 FormatR80(&paTests[iTest].InVal), \
                                 iVar ? "  " : "", uFswOut, iOutVal, \
                                 iVar ? "  " : "", paTests[iTest].fFswOut, paTests[iTest].iOutVal, \
                                 FswDiff(uFswOut, paTests[iTest].fFswOut), \
                                 iOutVal != paTests[iTest].iOutVal ? " - val" : "", FormatFcw(paTests[iTest].fFcw) ); \
            } \
            pfn = a_aSubTests[iFn].pfnNative; \
        } \
        FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
    } \
}

//fistt_r80_to_i16 diffs for AMD, of course :-)

TEST_FPU_STORE_INT(64, int64_t, "%RI64", FPU_ST_I64_T, g_aFpuStI64, FPU_ST_I64_TEST_T)
TEST_FPU_STORE_INT(32, int32_t, "%RI32", FPU_ST_I32_T, g_aFpuStI32, FPU_ST_I32_TEST_T)
TEST_FPU_STORE_INT(16, int16_t, "%RI16", FPU_ST_I16_T, g_aFpuStI16, FPU_ST_I16_TEST_T)

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE FpuStIntGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    RTEXITCODE rcExit = FpuStI64Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuStI32Generate(cTests, papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuStI16Generate(cTests, papszNameFmts);
    return rcExit;
}
static RTEXITCODE FpuStIntDumpAll(const char * const *papszNameFmts)
{
    RTEXITCODE rcExit = FpuStI64DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuStI32DumpAll(papszNameFmts);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = FpuStI16DumpAll(papszNameFmts);
    return rcExit;
}
#endif

static void FpuStIntTest(void)
{
    FpuStI64Test();
    FpuStI32Test();
    FpuStI16Test();
}


/*
 * Store as packed BCD value (memory).
 */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUSTR80TOD80,(PCX86FXSTATE, uint16_t *, PRTPBCD80U, PCRTFLOAT80U));
typedef FNIEMAIMPLFPUSTR80TOD80 *PFNIEMAIMPLFPUSTR80TOD80;
TYPEDEF_SUBTEST_TYPE(FPU_ST_D80_T, FPU_ST_D80_TEST_T, PFNIEMAIMPLFPUSTR80TOD80);

static FPU_ST_D80_T g_aFpuStD80[] =
{
    ENTRY_BIN(fst_r80_to_d80),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE FpuStD80Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    static RTFLOAT80U const s_aSpecials[] =
    {
        RTFLOAT80U_INIT_C(0, 0xde0b6b3a763fffe0, RTFLOAT80U_EXP_BIAS + 59), /* 1 below max */
        RTFLOAT80U_INIT_C(1, 0xde0b6b3a763fffe0, RTFLOAT80U_EXP_BIAS + 59), /* 1 above min */
        RTFLOAT80U_INIT_C(0, 0xde0b6b3a763ffff0, RTFLOAT80U_EXP_BIAS + 59), /* exact max */
        RTFLOAT80U_INIT_C(1, 0xde0b6b3a763ffff0, RTFLOAT80U_EXP_BIAS + 59), /* exact min */
        RTFLOAT80U_INIT_C(0, 0xde0b6b3a763fffff, RTFLOAT80U_EXP_BIAS + 59), /* max & all rounded off bits set */
        RTFLOAT80U_INIT_C(1, 0xde0b6b3a763fffff, RTFLOAT80U_EXP_BIAS + 59), /* min & all rounded off bits set */
        RTFLOAT80U_INIT_C(0, 0xde0b6b3a763ffff8, RTFLOAT80U_EXP_BIAS + 59), /* max & some rounded off bits set */
        RTFLOAT80U_INIT_C(1, 0xde0b6b3a763ffff8, RTFLOAT80U_EXP_BIAS + 59), /* min & some rounded off bits set */
        RTFLOAT80U_INIT_C(0, 0xde0b6b3a763ffff1, RTFLOAT80U_EXP_BIAS + 59), /* max & some other rounded off bits set */
        RTFLOAT80U_INIT_C(1, 0xde0b6b3a763ffff1, RTFLOAT80U_EXP_BIAS + 59), /* min & some other rounded off bits set */
        RTFLOAT80U_INIT_C(0, 0xde0b6b3a76400000, RTFLOAT80U_EXP_BIAS + 59), /* 1 above max */
        RTFLOAT80U_INIT_C(1, 0xde0b6b3a76400000, RTFLOAT80U_EXP_BIAS + 59), /* 1 below min */
    };

    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuStD80); iFn++)
    {
        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aFpuStD80[iFn]), RTEXITCODE_FAILURE);
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            uint16_t const fFcw = RandFcw();
            State.FSW = RandFsw();
            RTFLOAT80U const InVal = iTest < cTests ? RandR80Src(iTest, 59, true) : s_aSpecials[iTest - cTests];

            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
            {
                /* PC doesn't influence these, so leave as is. */
                AssertCompile(X86_FCW_OM_BIT + 1 == X86_FCW_UM_BIT && X86_FCW_UM_BIT + 1 == X86_FCW_PM_BIT);
                for (uint16_t iMask = 0; iMask < 16; iMask += 2 /*1*/)
                {
                    uint16_t  uFswOut = 0;
                    RTPBCD80U OutVal  = RTPBCD80U_INIT_ZERO(0);
                    State.FCW = (fFcw & ~(X86_FCW_RC_MASK | X86_FCW_OM | X86_FCW_UM | X86_FCW_PM))
                              | (iRounding  << X86_FCW_RC_SHIFT);
                    /*if (iMask & 1) State.FCW ^= X86_FCW_MASK_ALL;*/
                    State.FCW |= (iMask >> 1) << X86_FCW_OM_BIT;
                    g_aFpuStD80[iFn].pfn(&State, &uFswOut, &OutVal, &InVal);
                    FPU_ST_D80_TEST_T const Test = { State.FCW, State.FSW, uFswOut, InVal, OutVal };
                    GenerateBinaryWrite(&BinOut, &Test, sizeof(Test));
                }
            }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }
    return RTEXITCODE_SUCCESS;
}
DUMP_ALL_FN(FpuStD80, g_aFpuStD80)
#endif


static void FpuStD80Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuStD80); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aFpuStD80[iFn]))
            continue;

        FPU_ST_D80_TEST_T const * const paTests = g_aFpuStD80[iFn].paTests;
        uint32_t const                  cTests  = g_aFpuStD80[iFn].cTests;
        PFNIEMAIMPLFPUSTR80TOD80        pfn     = g_aFpuStD80[iFn].pfn;
        uint32_t const                  cVars   = COUNT_VARIATIONS(g_aFpuStD80[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTFLOAT80U const InVal   = paTests[iTest].InVal;
                uint16_t         uFswOut = 0;
                RTPBCD80U        OutVal  = RTPBCD80U_INIT_ZERO(0);
                State.FCW = paTests[iTest].fFcw;
                State.FSW = paTests[iTest].fFswIn;
                pfn(&State, &uFswOut, &OutVal, &InVal);
                if (   uFswOut != paTests[iTest].fFswOut
                    || !RTPBCD80U_ARE_IDENTICAL(&OutVal, &paTests[iTest].OutVal))
                    RTTestFailed(g_hTest, "#%04u%s: fcw=%#06x fsw=%#06x in=%s\n"
                                          "%s               -> fsw=%#06x    %s\n"
                                          "%s             expected %#06x    %s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn,
                                 FormatR80(&paTests[iTest].InVal),
                                 iVar ? "  " : "", uFswOut, FormatD80(&OutVal),
                                 iVar ? "  " : "", paTests[iTest].fFswOut, FormatD80(&paTests[iTest].OutVal),
                                 FswDiff(uFswOut, paTests[iTest].fFswOut),
                                 RTPBCD80U_ARE_IDENTICAL(&OutVal, &paTests[iTest].OutVal) ? " - val" : "",
                                 FormatFcw(paTests[iTest].fFcw) );
            }
            pfn = g_aFpuStD80[iFn].pfnNative;
        }

        FREE_DECOMPRESSED_TESTS(g_aFpuStD80[iFn]);
    }
}



/*********************************************************************************************************************************
*   x87 FPU Binary Operations                                                                                                    *
*********************************************************************************************************************************/

/*
 * Binary FPU operations on two 80-bit floating point values.
 */
TYPEDEF_SUBTEST_TYPE(FPU_BINARY_R80_T, FPU_BINARY_R80_TEST_T, PFNIEMAIMPLFPUR80);
enum { kFpuBinaryHint_fprem = 1, };

static FPU_BINARY_R80_T g_aFpuBinaryR80[] =
{
    ENTRY_BIN(fadd_r80_by_r80),
    ENTRY_BIN(fsub_r80_by_r80),
    ENTRY_BIN(fsubr_r80_by_r80),
    ENTRY_BIN(fmul_r80_by_r80),
    ENTRY_BIN(fdiv_r80_by_r80),
    ENTRY_BIN(fdivr_r80_by_r80),
    ENTRY_BIN_EX(fprem_r80_by_r80,  kFpuBinaryHint_fprem),
    ENTRY_BIN_EX(fprem1_r80_by_r80, kFpuBinaryHint_fprem),
    ENTRY_BIN(fscale_r80_by_r80),
    ENTRY_BIN_AMD(  fpatan_r80_by_r80,  0),  // C1 and rounding differs on AMD
    ENTRY_BIN_INTEL(fpatan_r80_by_r80,  0),  // C1 and rounding differs on AMD
    ENTRY_BIN_AMD(  fyl2x_r80_by_r80,   0),  // C1 and rounding differs on AMD
    ENTRY_BIN_INTEL(fyl2x_r80_by_r80,   0),  // C1 and rounding differs on AMD
    ENTRY_BIN_AMD(  fyl2xp1_r80_by_r80, 0),  // C1 and rounding differs on AMD
    ENTRY_BIN_INTEL(fyl2xp1_r80_by_r80, 0),  // C1 and rounding differs on AMD
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE FpuBinaryR80Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT80U Val1, Val2; } const s_aSpecials[] =
    {
        {   RTFLOAT80U_INIT_C(1, 0xdd762f07f2e80eef, 30142),    /* causes weird overflows with DOWN and NEAR rounding. */
            RTFLOAT80U_INIT_C(1, 0xffffffffffffffff, RTFLOAT80U_EXP_MAX - 1) },
        {   RTFLOAT80U_INIT_ZERO(0),    /* causes weird overflows with UP and NEAR rounding when precision is lower than 64. */
            RTFLOAT80U_INIT_C(0, 0xffffffffffffffff, RTFLOAT80U_EXP_MAX - 1) },
        {   RTFLOAT80U_INIT_ZERO(0),    /* minus variant */
            RTFLOAT80U_INIT_C(1, 0xffffffffffffffff, RTFLOAT80U_EXP_MAX - 1) },
        {   RTFLOAT80U_INIT_C(0, 0xcef238bb9a0afd86, 577 + RTFLOAT80U_EXP_BIAS),    /* for fprem and fprem1, max sequence length */
            RTFLOAT80U_INIT_C(0, 0xf11684ec0beaad94,   1 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff, -13396 + RTFLOAT80U_EXP_BIAS), /* for fdiv. We missed PE. */
            RTFLOAT80U_INIT_C(1, 0xffffffffffffffff,  16383 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0x8000000000000000,   1 + RTFLOAT80U_EXP_BIAS),    /* for fprem/fprem1 */
            RTFLOAT80U_INIT_C(0, 0xe000000000000000,   0 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0x8000000000000000,   1 + RTFLOAT80U_EXP_BIAS),    /* for fprem/fprem1 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   0 + RTFLOAT80U_EXP_BIAS) },
        /* fscale: This may seriously increase the exponent, and it turns out overflow and underflow behaviour changes
                   once RTFLOAT80U_EXP_BIAS_ADJUST is exceeded. */
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^1 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   0 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^64 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   6 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^1024 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   10 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^4096 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   12 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^16384 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   14 + RTFLOAT80U_EXP_BIAS) }, /* resulting exponent: 49150 */
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^24576 (RTFLOAT80U_EXP_BIAS_ADJUST) */
            RTFLOAT80U_INIT_C(0, 0xc000000000000000,   14 + RTFLOAT80U_EXP_BIAS) }, /* resulting exponent: 57342 - within 10980XE range */
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^24577 */
            RTFLOAT80U_INIT_C(0, 0xc002000000000000,   14 + RTFLOAT80U_EXP_BIAS) }, /* resulting exponent: 57343 - outside 10980XE range, behaviour changes! */
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^32768 - result is within range on 10980XE */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   15 + RTFLOAT80U_EXP_BIAS) }, /* resulting exponent: 65534 */
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^65536 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   16 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^1048576 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   20 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^16777216 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   24 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0x8000000000000000,   1),                          /* for fscale: min * 2^-24576 (RTFLOAT80U_EXP_BIAS_ADJUST) */
            RTFLOAT80U_INIT_C(1, 0xc000000000000000,   14 + RTFLOAT80U_EXP_BIAS) }, /* resulting exponent: -24575 - within 10980XE range */
        {   RTFLOAT80U_INIT_C(0, 0x8000000000000000,   1),                          /* for fscale: max * 2^-24577 (RTFLOAT80U_EXP_BIAS_ADJUST) */
            RTFLOAT80U_INIT_C(1, 0xc002000000000000,   14 + RTFLOAT80U_EXP_BIAS) }, /* resulting exponent: -24576 - outside 10980XE range, behaviour changes! */
        /* fscale: Negative variants for the essentials of the above. */
        {   RTFLOAT80U_INIT_C(1, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^24576 (RTFLOAT80U_EXP_BIAS_ADJUST) */
            RTFLOAT80U_INIT_C(0, 0xc000000000000000,   14 + RTFLOAT80U_EXP_BIAS) }, /* resulting exponent: 57342 - within 10980XE range */
        {   RTFLOAT80U_INIT_C(1, 0xffffffffffffffff,   RTFLOAT80U_EXP_MAX - 1),     /* for fscale: max * 2^24577 */
            RTFLOAT80U_INIT_C(0, 0xc002000000000000,   14 + RTFLOAT80U_EXP_BIAS) }, /* resulting exponent: 57343 - outside 10980XE range, behaviour changes! */
        {   RTFLOAT80U_INIT_C(1, 0x8000000000000000,   1),                          /* for fscale: min * 2^-24576 (RTFLOAT80U_EXP_BIAS_ADJUST) */
            RTFLOAT80U_INIT_C(1, 0xc000000000000000,   14 + RTFLOAT80U_EXP_BIAS) }, /* resulting exponent: -57342 - within 10980XE range */
        {   RTFLOAT80U_INIT_C(1, 0x8000000000000000,   1),                          /* for fscale: max * 2^-24576 (RTFLOAT80U_EXP_BIAS_ADJUST) */
            RTFLOAT80U_INIT_C(1, 0xc002000000000000,   14 + RTFLOAT80U_EXP_BIAS) }, /* resulting exponent: -57343 - outside 10980XE range, behaviour changes! */
        /* fscale: Some fun with denormals and pseudo-denormals. */
        {   RTFLOAT80U_INIT_C(0, 0x0800000000000000,   0),                          /* for fscale: max * 2^-4 */
            RTFLOAT80U_INIT_C(1, 0x8000000000000000,   2 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0x0800000000000000,   0),                          /* for fscale: max * 2^+1 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   0 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0x0800000000000000,   0), RTFLOAT80U_INIT_ZERO(0) }, /* for fscale: max * 2^+0 */
        {   RTFLOAT80U_INIT_C(0, 0x0000000000000008,   0),                          /* for fscale: max * 2^-4 => underflow */
            RTFLOAT80U_INIT_C(1, 0x8000000000000000,   2 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0x8005000300020001,   0), RTFLOAT80U_INIT_ZERO(0) }, /* pseudo-normal number * 2^+0. */
        {   RTFLOAT80U_INIT_C(1, 0x8005000300020001,   0), RTFLOAT80U_INIT_ZERO(0) }, /* pseudo-normal number * 2^+0. */
        {   RTFLOAT80U_INIT_C(0, 0x8005000300020001,   0),                          /* pseudo-normal number * 2^-4 */
            RTFLOAT80U_INIT_C(1, 0x8000000000000000,   2 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0x8005000300020001,   0),                          /* pseudo-normal number * 2^+0 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   0 + RTFLOAT80U_EXP_BIAS) },
        {   RTFLOAT80U_INIT_C(0, 0x8005000300020001,   0),                          /* pseudo-normal number * 2^+1 */
            RTFLOAT80U_INIT_C(0, 0x8000000000000000,   1 + RTFLOAT80U_EXP_BIAS) },
    };

    X86FXSTATE State;
    RT_ZERO(State);
    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    uint32_t cMinTargetRangeInputs = cMinNormalPairs / 2;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuBinaryR80); iFn++)
    {
        PFNIEMAIMPLFPUR80 const pfn = g_aFpuBinaryR80[iFn].pfnNative ? g_aFpuBinaryR80[iFn].pfnNative : g_aFpuBinaryR80[iFn].pfn;
        if (   g_aFpuBinaryR80[iFn].idxCpuEflFlavour != IEMTARGETCPU_EFL_BEHAVIOR_NATIVE
            && g_aFpuBinaryR80[iFn].idxCpuEflFlavour != g_idxCpuEflFlavour)
            continue;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aFpuBinaryR80[iFn]), RTEXITCODE_FAILURE);
        uint32_t cNormalInputPairs  = 0;
        uint32_t cTargetRangeInputs = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            RTFLOAT80U InVal1 = iTest < cTests ? RandR80Src1(iTest) : s_aSpecials[iTest - cTests].Val1;
            RTFLOAT80U InVal2 = iTest < cTests ? RandR80Src2(iTest) : s_aSpecials[iTest - cTests].Val2;
            bool fTargetRange = false;
            if (RTFLOAT80U_IS_NORMAL(&InVal1) && RTFLOAT80U_IS_NORMAL(&InVal2))
            {
                cNormalInputPairs++;
                if (   g_aFpuBinaryR80[iFn].uExtra == kFpuBinaryHint_fprem
                    && (uint32_t)InVal1.s.uExponent - (uint32_t)InVal2.s.uExponent - (uint32_t)64 <= (uint32_t)512)
                    cTargetRangeInputs += fTargetRange = true;
                else if (cTargetRangeInputs < cMinTargetRangeInputs && iTest < cTests)
                    if (g_aFpuBinaryR80[iFn].uExtra == kFpuBinaryHint_fprem)
                    {   /* The aim is two values with an exponent difference between 64 and 640 so we can do the whole sequence. */
                        InVal2.s.uExponent = RTRandU32Ex(1, RTFLOAT80U_EXP_MAX - 66);
                        InVal1.s.uExponent = RTRandU32Ex(InVal2.s.uExponent + 64, RT_MIN(InVal2.s.uExponent + 512, RTFLOAT80U_EXP_MAX - 1));
                        cTargetRangeInputs += fTargetRange = true;
                    }
            }
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint16_t const fFcwExtra = 0;
            uint16_t const fFcw = RandFcw();
            State.FSW = RandFsw();

            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint16_t iPrecision = 0; iPrecision < 4; iPrecision++)
                {
                    State.FCW = (fFcw & ~(X86_FCW_RC_MASK | X86_FCW_PC_MASK | X86_FCW_MASK_ALL))
                              | (iRounding  << X86_FCW_RC_SHIFT)
                              | (iPrecision << X86_FCW_PC_SHIFT)
                              | X86_FCW_MASK_ALL;
                    IEMFPURESULT ResM = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                    pfn(&State, &ResM, &InVal1, &InVal2);
                    FPU_BINARY_R80_TEST_T const TestM
                        = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, ResM.FSW, InVal1, InVal2, ResM.r80Result };
                    GenerateBinaryWrite(&BinOut, &TestM, sizeof(TestM));

                    State.FCW = State.FCW & ~X86_FCW_MASK_ALL;
                    IEMFPURESULT ResU = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                    pfn(&State, &ResU, &InVal1, &InVal2);
                    FPU_BINARY_R80_TEST_T const TestU
                        = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, ResU.FSW, InVal1, InVal2, ResU.r80Result };
                    GenerateBinaryWrite(&BinOut, &TestU, sizeof(TestU));

                    uint16_t fXcpt = (ResM.FSW | ResU.FSW) & X86_FSW_XCPT_MASK & ~X86_FSW_SF;
                    if (fXcpt)
                    {
                        State.FCW = (State.FCW & ~X86_FCW_MASK_ALL) | fXcpt;
                        IEMFPURESULT Res1 = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                        pfn(&State, &Res1, &InVal1, &InVal2);
                        FPU_BINARY_R80_TEST_T const Test1
                            = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, Res1.FSW, InVal1, InVal2, Res1.r80Result };
                        GenerateBinaryWrite(&BinOut, &Test1, sizeof(Test1));

                        if (((Res1.FSW & X86_FSW_XCPT_MASK) & fXcpt) != (Res1.FSW & X86_FSW_XCPT_MASK))
                        {
                            fXcpt |= Res1.FSW & X86_FSW_XCPT_MASK;
                            State.FCW = (State.FCW & ~X86_FCW_MASK_ALL) | fXcpt;
                            IEMFPURESULT Res2 = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                            pfn(&State, &Res2, &InVal1, &InVal2);
                            FPU_BINARY_R80_TEST_T const Test2
                                = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, Res2.FSW, InVal1, InVal2, Res2.r80Result };
                            GenerateBinaryWrite(&BinOut, &Test2, sizeof(Test2));
                        }
                        if (!RT_IS_POWER_OF_TWO(fXcpt))
                            for (uint16_t fUnmasked = 1; fUnmasked <= X86_FCW_PM; fUnmasked <<= 1)
                                if (fUnmasked & fXcpt)
                                {
                                    State.FCW = (State.FCW & ~X86_FCW_MASK_ALL) | (fXcpt & ~fUnmasked);
                                    IEMFPURESULT Res3 = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                                    pfn(&State, &Res3, &InVal1, &InVal2);
                                    FPU_BINARY_R80_TEST_T const Test3
                                        = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, Res3.FSW, InVal1, InVal2, Res3.r80Result };
                                    GenerateBinaryWrite(&BinOut, &Test3, sizeof(Test3));
                                }
                    }

                    /* If the values are in range and caused no exceptions, do the whole series of
                       partial reminders till we get the non-partial one or run into an exception. */
                    if (fTargetRange && fXcpt == 0 && g_aFpuBinaryR80[iFn].uExtra == kFpuBinaryHint_fprem)
                    {
                        IEMFPURESULT ResPrev = ResM;
                        for (unsigned i = 0; i < 32 && (ResPrev.FSW & (X86_FSW_C2 | X86_FSW_XCPT_MASK)) == X86_FSW_C2; i++)
                        {
                            State.FCW = State.FCW | X86_FCW_MASK_ALL;
                            State.FSW = ResPrev.FSW;
                            IEMFPURESULT ResSeq = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                            pfn(&State, &ResSeq, &ResPrev.r80Result, &InVal2);
                            FPU_BINARY_R80_TEST_T const TestSeq
                                = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, ResSeq.FSW, ResPrev.r80Result, InVal2, ResSeq.r80Result };
                            GenerateBinaryWrite(&BinOut, &TestSeq, sizeof(TestSeq));
                            ResPrev = ResSeq;
                        }
                    }
                }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }
    return RTEXITCODE_SUCCESS;
}
DUMP_ALL_FN(FpuBinaryR80, g_aFpuBinaryR80)
#endif


static void FpuBinaryR80Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuBinaryR80); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aFpuBinaryR80[iFn]))
            continue;

        FPU_BINARY_R80_TEST_T const * const paTests = g_aFpuBinaryR80[iFn].paTests;
        uint32_t const                      cTests  = g_aFpuBinaryR80[iFn].cTests;
        PFNIEMAIMPLFPUR80                   pfn     = g_aFpuBinaryR80[iFn].pfn;
        uint32_t const                      cVars   = COUNT_VARIATIONS(g_aFpuBinaryR80[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTFLOAT80U const InVal1 = paTests[iTest].InVal1;
                RTFLOAT80U const InVal2 = paTests[iTest].InVal2;
                IEMFPURESULT     Res    = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                State.FCW = paTests[iTest].fFcw;
                State.FSW = paTests[iTest].fFswIn;
                pfn(&State, &Res, &InVal1, &InVal2);
                if (   Res.FSW != paTests[iTest].fFswOut
                    || !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].OutVal))
                    RTTestFailed(g_hTest, "#%04u%s: fcw=%#06x fsw=%#06x in1=%s in2=%s\n"
                                          "%s               -> fsw=%#06x    %s\n"
                                          "%s             expected %#06x    %s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn,
                                 FormatR80(&paTests[iTest].InVal1), FormatR80(&paTests[iTest].InVal2),
                                 iVar ? "  " : "", Res.FSW, FormatR80(&Res.r80Result),
                                 iVar ? "  " : "", paTests[iTest].fFswOut, FormatR80(&paTests[iTest].OutVal),
                                 FswDiff(Res.FSW, paTests[iTest].fFswOut),
                                 !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].OutVal) ? " - val" : "",
                                 FormatFcw(paTests[iTest].fFcw) );
            }
            pfn = g_aFpuBinaryR80[iFn].pfnNative;
        }

        FREE_DECOMPRESSED_TESTS(g_aFpuBinaryR80[iFn]);
    }
}


/*
 * Binary FPU operations on one 80-bit floating point value and one 64-bit or 32-bit one.
 */
#define int64_t_IS_NORMAL(a) 1
#define int32_t_IS_NORMAL(a) 1
#define int16_t_IS_NORMAL(a) 1

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static struct { RTFLOAT80U Val1; RTFLOAT64U Val2; } const s_aFpuBinaryR64Specials[] =
{
    {   RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS),
        RTFLOAT64U_INIT_C(0, 0xfeeeeddddcccc,    RTFLOAT64U_EXP_BIAS)   }, /* whatever */
};
static struct { RTFLOAT80U Val1; RTFLOAT32U Val2; } const s_aFpuBinaryR32Specials[] =
{
    {   RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS),
        RTFLOAT32U_INIT_C(0, 0x7fffee, RTFLOAT32U_EXP_BIAS)             }, /* whatever */
};
static struct { RTFLOAT80U Val1; int32_t Val2; } const s_aFpuBinaryI32Specials[] =
{
    {   RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS), INT32_MAX    }, /* whatever */
};
static struct { RTFLOAT80U Val1; int16_t Val2; } const s_aFpuBinaryI16Specials[] =
{
    {   RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS), INT16_MAX    }, /* whatever */
};

# define GEN_FPU_BINARY_SMALL(a_fIntType, a_cBits, a_LoBits, a_UpBits, a_Type2, a_aSubTests, a_TestType) \
static RTEXITCODE FpuBinary ## a_UpBits ## Generate(uint32_t cTests, const char * const *papszNameFmts) \
{ \
    cTests = RT_MAX(160, cTests); /* there are 144 standard input variations for r80 by r80 */ \
    \
    X86FXSTATE State; \
    RT_ZERO(State); \
    uint32_t cMinNormalPairs = (cTests - 144) / 4; \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        IEMBINARYOUTPUT BinOut; \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, a_aSubTests[iFn]), RTEXITCODE_FAILURE); \
        uint32_t cNormalInputPairs = 0; \
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aFpuBinary ## a_UpBits ## Specials); iTest += 1) \
        { \
            RTFLOAT80U const InVal1 = iTest < cTests ? RandR80Src1(iTest, a_cBits, a_fIntType) \
                                    : s_aFpuBinary ## a_UpBits ## Specials[iTest - cTests].Val1; \
            a_Type2    const InVal2 = iTest < cTests ? Rand ## a_UpBits ## Src2(iTest) \
                                    : s_aFpuBinary ## a_UpBits ## Specials[iTest - cTests].Val2; \
            if (RTFLOAT80U_IS_NORMAL(&InVal1) && a_Type2 ## _IS_NORMAL(&InVal2)) \
                cNormalInputPairs++; \
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests) \
            { \
                iTest -= 1; \
                continue; \
            } \
            \
            uint16_t const fFcw = RandFcw(); \
            State.FSW = RandFsw(); \
            \
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++) \
            { \
                for (uint16_t iPrecision = 0; iPrecision < 4; iPrecision++) \
                { \
                    for (uint16_t iMask = 0; iMask <= X86_FCW_MASK_ALL; iMask += X86_FCW_MASK_ALL) \
                    { \
                        State.FCW = (fFcw & ~(X86_FCW_RC_MASK | X86_FCW_PC_MASK | X86_FCW_MASK_ALL)) \
                                  | (iRounding  << X86_FCW_RC_SHIFT) \
                                  | (iPrecision << X86_FCW_PC_SHIFT) \
                                  | iMask; \
                        IEMFPURESULT Res = { RTFLOAT80U_INIT(0, 0, 0), 0 }; \
                        a_aSubTests[iFn].pfn(&State, &Res, &InVal1, &InVal2); \
                        a_TestType const Test = { State.FCW, State.FSW, Res.FSW, InVal1, InVal2, Res.r80Result }; \
                        GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
                    } \
                } \
            } \
        } \
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
    } \
    return RTEXITCODE_SUCCESS; \
} \
DUMP_ALL_FN(FpuBinary ## a_UpBits, a_aSubTests)
#else
# define GEN_FPU_BINARY_SMALL(a_fIntType, a_cBits, a_LoBits, a_UpBits, a_Type2, a_aSubTests, a_TestType)
#endif

#define TEST_FPU_BINARY_SMALL(a_fIntType, a_cBits, a_LoBits, a_UpBits, a_I, a_Type2, a_SubTestType, a_aSubTests, a_TestType) \
TYPEDEF_SUBTEST_TYPE(a_SubTestType, a_TestType, PFNIEMAIMPLFPU ## a_UpBits); \
\
static a_SubTestType a_aSubTests[] = \
{ \
    ENTRY_BIN(RT_CONCAT4(f, a_I, add_r80_by_, a_LoBits)), \
    ENTRY_BIN(RT_CONCAT4(f, a_I, mul_r80_by_, a_LoBits)), \
    ENTRY_BIN(RT_CONCAT4(f, a_I, sub_r80_by_, a_LoBits)), \
    ENTRY_BIN(RT_CONCAT4(f, a_I, subr_r80_by_, a_LoBits)), \
    ENTRY_BIN(RT_CONCAT4(f, a_I, div_r80_by_, a_LoBits)), \
    ENTRY_BIN(RT_CONCAT4(f, a_I, divr_r80_by_, a_LoBits)), \
}; \
\
GEN_FPU_BINARY_SMALL(a_fIntType, a_cBits, a_LoBits, a_UpBits, a_Type2, a_aSubTests, a_TestType) \
\
static void FpuBinary ## a_UpBits ## Test(void) \
{ \
    X86FXSTATE State; \
    RT_ZERO(State); \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_aSubTests[iFn])) \
            continue; \
        \
        a_TestType const * const   paTests = a_aSubTests[iFn].paTests; \
        uint32_t const             cTests  = a_aSubTests[iFn].cTests; \
        PFNIEMAIMPLFPU ## a_UpBits pfn     = a_aSubTests[iFn].pfn; \
        uint32_t const             cVars   = COUNT_VARIATIONS(a_aSubTests[iFn]); \
        if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
        for (uint32_t iVar = 0; iVar < cVars; iVar++) \
        { \
            for (uint32_t iTest = 0; iTest < cTests; iTest++) \
            { \
                RTFLOAT80U const InVal1 = paTests[iTest].InVal1; \
                a_Type2    const InVal2 = paTests[iTest].InVal2; \
                IEMFPURESULT     Res    = { RTFLOAT80U_INIT(0, 0, 0), 0 }; \
                State.FCW = paTests[iTest].fFcw; \
                State.FSW = paTests[iTest].fFswIn; \
                pfn(&State, &Res, &InVal1, &InVal2); \
                if (   Res.FSW != paTests[iTest].fFswOut \
                    || !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].OutVal)) \
                    RTTestFailed(g_hTest, "#%04u%s: fcw=%#06x fsw=%#06x in1=%s in2=%s\n" \
                                          "%s               -> fsw=%#06x    %s\n" \
                                          "%s             expected %#06x    %s%s%s (%s)\n", \
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn, \
                                 FormatR80(&paTests[iTest].InVal1), Format ## a_UpBits(&paTests[iTest].InVal2), \
                                 iVar ? "  " : "", Res.FSW, FormatR80(&Res.r80Result), \
                                 iVar ? "  " : "", paTests[iTest].fFswOut, FormatR80(&paTests[iTest].OutVal), \
                                 FswDiff(Res.FSW, paTests[iTest].fFswOut), \
                                 !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].OutVal) ? " - val" : "", \
                                 FormatFcw(paTests[iTest].fFcw) ); \
            } \
            pfn = a_aSubTests[iFn].pfnNative; \
        } \
        FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
    } \
}

TEST_FPU_BINARY_SMALL(0, 64, r64, R64, RT_NOTHING, RTFLOAT64U, FPU_BINARY_R64_T, g_aFpuBinaryR64, FPU_BINARY_R64_TEST_T)
TEST_FPU_BINARY_SMALL(0, 32, r32, R32, RT_NOTHING, RTFLOAT32U, FPU_BINARY_R32_T, g_aFpuBinaryR32, FPU_BINARY_R32_TEST_T)
TEST_FPU_BINARY_SMALL(1, 32, i32, I32, i,          int32_t,    FPU_BINARY_I32_T, g_aFpuBinaryI32, FPU_BINARY_I32_TEST_T)
TEST_FPU_BINARY_SMALL(1, 16, i16, I16, i,          int16_t,    FPU_BINARY_I16_T, g_aFpuBinaryI16, FPU_BINARY_I16_TEST_T)


/*
 * Binary operations on 80-, 64- and 32-bit floating point only affecting FSW.
 */
#ifdef TSTIEMAIMPL_WITH_GENERATOR
static struct { RTFLOAT80U Val1, Val2; } const s_aFpuBinaryFswR80Specials[] =
{
    {   RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS),
        RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS) }, /* whatever */
};
static struct { RTFLOAT80U Val1; RTFLOAT64U Val2; } const s_aFpuBinaryFswR64Specials[] =
{
    {   RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS),
        RTFLOAT64U_INIT_C(0, 0xfeeeeddddcccc,    RTFLOAT64U_EXP_BIAS) }, /* whatever */
};
static struct { RTFLOAT80U Val1; RTFLOAT32U Val2; } const s_aFpuBinaryFswR32Specials[] =
{
    {   RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS),
        RTFLOAT32U_INIT_C(0, 0x7fffee,           RTFLOAT32U_EXP_BIAS) }, /* whatever */
};
static struct { RTFLOAT80U Val1; int32_t Val2; } const s_aFpuBinaryFswI32Specials[] =
{
    {   RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS), INT32_MAX   }, /* whatever */
};
static struct { RTFLOAT80U Val1; int16_t Val2; } const s_aFpuBinaryFswI16Specials[] =
{
    {   RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS), INT16_MAX   }, /* whatever */
};

# define GEN_FPU_BINARY_FSW(a_fIntType, a_cBits, a_UpBits, a_Type2, a_aSubTests, a_TestType) \
static RTEXITCODE FpuBinaryFsw ## a_UpBits ## Generate(uint32_t cTests, const char * const *papszNameFmts) \
{ \
    cTests = RT_MAX(160, cTests); /* there are 144 standard input variations for r80 by r80 */ \
    \
    X86FXSTATE State; \
    RT_ZERO(State); \
    uint32_t cMinNormalPairs = (cTests - 144) / 4; \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        IEMBINARYOUTPUT BinOut; \
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, a_aSubTests[iFn]), RTEXITCODE_FAILURE); \
        uint32_t cNormalInputPairs = 0; \
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aFpuBinaryFsw ## a_UpBits ## Specials); iTest += 1) \
        { \
            RTFLOAT80U const InVal1 = iTest < cTests ? RandR80Src1(iTest, a_cBits, a_fIntType) \
                                    : s_aFpuBinaryFsw ## a_UpBits ## Specials[iTest - cTests].Val1; \
            a_Type2    const InVal2 = iTest < cTests ? Rand ## a_UpBits ## Src2(iTest) \
                                    : s_aFpuBinaryFsw ## a_UpBits ## Specials[iTest - cTests].Val2; \
            if (RTFLOAT80U_IS_NORMAL(&InVal1) && a_Type2 ## _IS_NORMAL(&InVal2)) \
                cNormalInputPairs++; \
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests) \
            { \
                iTest -= 1; \
                continue; \
            } \
            \
            uint16_t const fFcw = RandFcw(); \
            State.FSW = RandFsw(); \
            \
            /* Guess these aren't affected by precision or rounding, so just flip the exception mask. */ \
            for (uint16_t iMask = 0; iMask <= X86_FCW_MASK_ALL; iMask += X86_FCW_MASK_ALL) \
            { \
                State.FCW = (fFcw & ~(X86_FCW_MASK_ALL)) | iMask; \
                uint16_t fFswOut = 0; \
                a_aSubTests[iFn].pfn(&State, &fFswOut, &InVal1, &InVal2); \
                a_TestType const Test = { State.FCW, State.FSW, fFswOut, InVal1, InVal2 }; \
                GenerateBinaryWrite(&BinOut, &Test, sizeof(Test)); \
            } \
        } \
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE); \
    } \
    return RTEXITCODE_SUCCESS; \
} \
DUMP_ALL_FN(FpuBinaryFsw ## a_UpBits, a_aSubTests)
#else
# define GEN_FPU_BINARY_FSW(a_fIntType, a_cBits, a_UpBits, a_Type2, a_aSubTests, a_TestType)
#endif

#define TEST_FPU_BINARY_FSW(a_fIntType, a_cBits, a_UpBits, a_Type2, a_SubTestType, a_aSubTests, a_TestType, ...) \
TYPEDEF_SUBTEST_TYPE(a_SubTestType, a_TestType, PFNIEMAIMPLFPU ## a_UpBits ## FSW); \
\
static a_SubTestType a_aSubTests[] = \
{ \
    __VA_ARGS__ \
}; \
\
GEN_FPU_BINARY_FSW(a_fIntType, a_cBits, a_UpBits, a_Type2, a_aSubTests, a_TestType) \
\
static void FpuBinaryFsw ## a_UpBits ## Test(void) \
{ \
    X86FXSTATE State; \
    RT_ZERO(State); \
    for (size_t iFn = 0; iFn < RT_ELEMENTS(a_aSubTests); iFn++) \
    { \
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(a_aSubTests[iFn])) \
            continue; \
        \
        a_TestType const * const            paTests = a_aSubTests[iFn].paTests; \
        uint32_t const                      cTests  = a_aSubTests[iFn].cTests; \
        PFNIEMAIMPLFPU ## a_UpBits ## FSW   pfn     = a_aSubTests[iFn].pfn; \
        uint32_t const                      cVars   = COUNT_VARIATIONS(a_aSubTests[iFn]); \
        if (!cTests) RTTestSkipped(g_hTest, "no tests"); \
        for (uint32_t iVar = 0; iVar < cVars; iVar++) \
        { \
            for (uint32_t iTest = 0; iTest < cTests; iTest++) \
            { \
                uint16_t         fFswOut = 0; \
                RTFLOAT80U const InVal1  = paTests[iTest].InVal1; \
                a_Type2    const InVal2  = paTests[iTest].InVal2; \
                State.FCW = paTests[iTest].fFcw; \
                State.FSW = paTests[iTest].fFswIn; \
                pfn(&State, &fFswOut, &InVal1, &InVal2); \
                if (fFswOut != paTests[iTest].fFswOut) \
                    RTTestFailed(g_hTest, "#%04u%s: fcw=%#06x fsw=%#06x in1=%s in2=%s\n" \
                                          "%s               -> fsw=%#06x\n" \
                                          "%s             expected %#06x %s (%s)\n", \
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn, \
                                 FormatR80(&paTests[iTest].InVal1), Format ## a_UpBits(&paTests[iTest].InVal2), \
                                 iVar ? "  " : "", fFswOut, \
                                 iVar ? "  " : "", paTests[iTest].fFswOut, \
                                 FswDiff(fFswOut, paTests[iTest].fFswOut), FormatFcw(paTests[iTest].fFcw) ); \
            } \
            pfn = a_aSubTests[iFn].pfnNative; \
        } \
        FREE_DECOMPRESSED_TESTS(a_aSubTests[iFn]); \
    } \
}

TEST_FPU_BINARY_FSW(0, 80, R80, RTFLOAT80U, FPU_BINARY_FSW_R80_T, g_aFpuBinaryFswR80, FPU_BINARY_R80_TEST_T, ENTRY_BIN(fcom_r80_by_r80), ENTRY_BIN(fucom_r80_by_r80))
TEST_FPU_BINARY_FSW(0, 64, R64, RTFLOAT64U, FPU_BINARY_FSW_R64_T, g_aFpuBinaryFswR64, FPU_BINARY_R64_TEST_T, ENTRY_BIN(fcom_r80_by_r64))
TEST_FPU_BINARY_FSW(0, 32, R32, RTFLOAT32U, FPU_BINARY_FSW_R32_T, g_aFpuBinaryFswR32, FPU_BINARY_R32_TEST_T, ENTRY_BIN(fcom_r80_by_r32))
TEST_FPU_BINARY_FSW(1, 32, I32, int32_t,    FPU_BINARY_FSW_I32_T, g_aFpuBinaryFswI32, FPU_BINARY_I32_TEST_T, ENTRY_BIN(ficom_r80_by_i32))
TEST_FPU_BINARY_FSW(1, 16, I16, int16_t,    FPU_BINARY_FSW_I16_T, g_aFpuBinaryFswI16, FPU_BINARY_I16_TEST_T, ENTRY_BIN(ficom_r80_by_i16))


/*
 * Binary operations on 80-bit floating point that effects only EFLAGS and possibly FSW.
 */
TYPEDEF_SUBTEST_TYPE(FPU_BINARY_EFL_R80_T, FPU_BINARY_EFL_R80_TEST_T, PFNIEMAIMPLFPUR80EFL);

static FPU_BINARY_EFL_R80_T g_aFpuBinaryEflR80[] =
{
    ENTRY_BIN(fcomi_r80_by_r80),
    ENTRY_BIN(fucomi_r80_by_r80),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static struct { RTFLOAT80U Val1, Val2; } const s_aFpuBinaryEflR80Specials[] =
{
    {   RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS),
        RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS) }, /* whatever */
};

static RTEXITCODE FpuBinaryEflR80Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(160, cTests); /* there are 144 standard input variations */

    X86FXSTATE State;
    RT_ZERO(State);
    uint32_t cMinNormalPairs = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuBinaryEflR80); iFn++)
    {
        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aFpuBinaryEflR80[iFn]), RTEXITCODE_FAILURE);
        uint32_t cNormalInputPairs = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aFpuBinaryEflR80Specials); iTest += 1)
        {
            RTFLOAT80U const InVal1 = iTest < cTests ? RandR80Src1(iTest) : s_aFpuBinaryEflR80Specials[iTest - cTests].Val1;
            RTFLOAT80U const InVal2 = iTest < cTests ? RandR80Src2(iTest) : s_aFpuBinaryEflR80Specials[iTest - cTests].Val2;
            if (RTFLOAT80U_IS_NORMAL(&InVal1) && RTFLOAT80U_IS_NORMAL(&InVal2))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint16_t const fFcw = RandFcw();
            State.FSW = RandFsw();

            /* Guess these aren't affected by precision or rounding, so just flip the exception mask. */
            for (uint16_t iMask = 0; iMask <= X86_FCW_MASK_ALL; iMask += X86_FCW_MASK_ALL)
            {
                State.FCW = (fFcw & ~(X86_FCW_MASK_ALL)) | iMask;
                uint16_t uFswOut = 0;
                uint32_t fEflOut = g_aFpuBinaryEflR80[iFn].pfn(&State, &uFswOut, &InVal1, &InVal2);
                FPU_BINARY_EFL_R80_TEST_T const Test = { State.FCW, State.FSW, uFswOut, InVal1, InVal2, fEflOut, };
                GenerateBinaryWrite(&BinOut, &Test, sizeof(Test));
            }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }
    return RTEXITCODE_SUCCESS;
}
DUMP_ALL_FN(FpuBinaryEflR80, g_aFpuBinaryEflR80)
#endif /*TSTIEMAIMPL_WITH_GENERATOR*/

static void FpuBinaryEflR80Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuBinaryEflR80); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aFpuBinaryEflR80[iFn]))
            continue;

        FPU_BINARY_EFL_R80_TEST_T const * const paTests = g_aFpuBinaryEflR80[iFn].paTests;
        uint32_t const                          cTests  = g_aFpuBinaryEflR80[iFn].cTests;
        PFNIEMAIMPLFPUR80EFL                    pfn     = g_aFpuBinaryEflR80[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aFpuBinaryEflR80[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTFLOAT80U const InVal1 = paTests[iTest].InVal1;
                RTFLOAT80U const InVal2 = paTests[iTest].InVal2;
                State.FCW = paTests[iTest].fFcw;
                State.FSW = paTests[iTest].fFswIn;
                uint16_t uFswOut = 0;
                uint32_t fEflOut = pfn(&State, &uFswOut, &InVal1, &InVal2);
                if (   uFswOut != paTests[iTest].fFswOut
                    || fEflOut != paTests[iTest].fEflOut)
                    RTTestFailed(g_hTest, "#%04u%s: fcw=%#06x fsw=%#06x in1=%s in2=%s\n"
                                          "%s               -> fsw=%#06x efl=%#08x\n"
                                          "%s             expected %#06x     %#08x %s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn,
                                 FormatR80(&paTests[iTest].InVal1), FormatR80(&paTests[iTest].InVal2),
                                 iVar ? "  " : "", uFswOut, fEflOut,
                                 iVar ? "  " : "", paTests[iTest].fFswOut, paTests[iTest].fEflOut,
                                 FswDiff(uFswOut, paTests[iTest].fFswOut), EFlagsDiff(fEflOut, paTests[iTest].fEflOut),
                                 FormatFcw(paTests[iTest].fFcw));
            }
            pfn = g_aFpuBinaryEflR80[iFn].pfnNative;
        }

        FREE_DECOMPRESSED_TESTS(g_aFpuBinaryEflR80[iFn]);
    }
}


/*********************************************************************************************************************************
*   x87 FPU Unary Operations                                                                                                     *
*********************************************************************************************************************************/

/*
 * Unary FPU operations on one 80-bit floating point value.
 *
 * Note! The FCW reserved bit 7 is used to indicate whether a test may produce
 *       a rounding error or not.
 */
TYPEDEF_SUBTEST_TYPE(FPU_UNARY_R80_T, FPU_UNARY_R80_TEST_T, PFNIEMAIMPLFPUR80UNARY);

enum { kUnary_Accurate = 0, kUnary_Accurate_Trigonometry /*probably not accurate, but need impl to know*/, kUnary_Rounding_F2xm1 };
static FPU_UNARY_R80_T g_aFpuUnaryR80[] =
{
    ENTRY_BIN_EX(      fabs_r80,     kUnary_Accurate),
    ENTRY_BIN_EX(      fchs_r80,     kUnary_Accurate),
    ENTRY_BIN_AMD_EX(  f2xm1_r80, 0, kUnary_Accurate), // C1 differs for -1m0x3fb263cc2c331e15^-2654 (different ln2 constant?)
    ENTRY_BIN_INTEL_EX(f2xm1_r80, 0, kUnary_Rounding_F2xm1),
    ENTRY_BIN_EX(      fsqrt_r80,    kUnary_Accurate),
    ENTRY_BIN_EX(      frndint_r80,  kUnary_Accurate),
    ENTRY_BIN_AMD_EX(  fsin_r80, 0,  kUnary_Accurate_Trigonometry),  // value & C1 differences for pseudo denormals and others (e.g. -1m0x2b1e5683cbca5725^-3485)
    ENTRY_BIN_INTEL_EX(fsin_r80, 0,  kUnary_Accurate_Trigonometry),
    ENTRY_BIN_AMD_EX(  fcos_r80, 0,  kUnary_Accurate_Trigonometry),  // value & C1 differences
    ENTRY_BIN_INTEL_EX(fcos_r80, 0,  kUnary_Accurate_Trigonometry),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR

static bool FpuUnaryR80MayHaveRoundingError(PCRTFLOAT80U pr80Val, int enmKind)
{
    if (   enmKind == kUnary_Rounding_F2xm1
        && RTFLOAT80U_IS_NORMAL(pr80Val)
        && pr80Val->s.uExponent <  RTFLOAT80U_EXP_BIAS
        && pr80Val->s.uExponent >= RTFLOAT80U_EXP_BIAS - 69)
        return true;
    return false;
}

DUMP_ALL_FN(FpuUnaryR80, g_aFpuUnaryR80)
static RTEXITCODE FpuUnaryR80Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    static RTFLOAT80U const s_aSpecials[] =
    {
        RTFLOAT80U_INIT_C(0, 0x8000000000000000, RTFLOAT80U_EXP_BIAS - 1), /*  0.5 (for f2xm1) */
        RTFLOAT80U_INIT_C(1, 0x8000000000000000, RTFLOAT80U_EXP_BIAS - 1), /* -0.5 (for f2xm1) */
        RTFLOAT80U_INIT_C(0, 0x8000000000000000, RTFLOAT80U_EXP_BIAS),     /*  1.0 (for f2xm1) */
        RTFLOAT80U_INIT_C(1, 0x8000000000000000, RTFLOAT80U_EXP_BIAS),     /* -1.0 (for f2xm1) */
        RTFLOAT80U_INIT_C(0, 0x8000000000000000, 0), /* +1.0^-16382 */
        RTFLOAT80U_INIT_C(1, 0x8000000000000000, 0), /* -1.0^-16382 */
        RTFLOAT80U_INIT_C(0, 0xc000000000000000, 0), /* +1.1^-16382 */
        RTFLOAT80U_INIT_C(1, 0xc000000000000000, 0), /* -1.1^-16382 */
        RTFLOAT80U_INIT_C(0, 0xc000100000000000, 0), /* +1.1xxx1^-16382 */
        RTFLOAT80U_INIT_C(1, 0xc000100000000000, 0), /* -1.1xxx1^-16382 */
    };
    X86FXSTATE State;
    RT_ZERO(State);
    uint32_t cMinNormals = cTests / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuUnaryR80); iFn++)
    {
        PFNIEMAIMPLFPUR80UNARY const pfn = g_aFpuUnaryR80[iFn].pfnNative ? g_aFpuUnaryR80[iFn].pfnNative : g_aFpuUnaryR80[iFn].pfn;
        if (   g_aFpuUnaryR80[iFn].idxCpuEflFlavour != IEMTARGETCPU_EFL_BEHAVIOR_NATIVE
            && g_aFpuUnaryR80[iFn].idxCpuEflFlavour != g_idxCpuEflFlavour)
            continue;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aFpuUnaryR80[iFn]), RTEXITCODE_FAILURE);
        uint32_t cNormalInputs      = 0;
        uint32_t cTargetRangeInputs = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            RTFLOAT80U InVal = iTest < cTests ? RandR80Src(iTest) : s_aSpecials[iTest - cTests];
            if (RTFLOAT80U_IS_NORMAL(&InVal))
            {
                if (g_aFpuUnaryR80[iFn].uExtra == kUnary_Rounding_F2xm1)
                {
                    unsigned uTargetExp = g_aFpuUnaryR80[iFn].uExtra == kUnary_Rounding_F2xm1
                                        ? RTFLOAT80U_EXP_BIAS /* 2^0..2^-69 */ : RTFLOAT80U_EXP_BIAS + 63 + 1 /* 2^64..2^-64 */;
                    unsigned cTargetExp = g_aFpuUnaryR80[iFn].uExtra == kUnary_Rounding_F2xm1 ? 69 : 63*2 + 2;
                    if (InVal.s.uExponent <= uTargetExp && InVal.s.uExponent >= uTargetExp - cTargetExp)
                        cTargetRangeInputs++;
                    else if (cTargetRangeInputs < cMinNormals / 2 && iTest + cMinNormals / 2 >= cTests && iTest < cTests)
                    {
                        InVal.s.uExponent = RTRandU32Ex(uTargetExp - cTargetExp, uTargetExp);
                        cTargetRangeInputs++;
                    }
                }
                cNormalInputs++;
            }
            else if (cNormalInputs < cMinNormals && iTest + cMinNormals >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint16_t const fFcwExtra = FpuUnaryR80MayHaveRoundingError(&InVal, g_aFpuUnaryR80[iFn].uExtra) ? 0x80 : 0;
            uint16_t const fFcw = RandFcw();
            State.FSW = RandFsw();

            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint16_t iPrecision = 0; iPrecision < 4; iPrecision++)
                {
                    State.FCW = (fFcw & ~(X86_FCW_RC_MASK | X86_FCW_PC_MASK | X86_FCW_MASK_ALL))
                              | (iRounding  << X86_FCW_RC_SHIFT)
                              | (iPrecision << X86_FCW_PC_SHIFT)
                              | X86_FCW_MASK_ALL;
                    IEMFPURESULT ResM = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                    pfn(&State, &ResM, &InVal);
                    FPU_UNARY_R80_TEST_T const TestM
                        = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, ResM.FSW, InVal, ResM.r80Result };
                    GenerateBinaryWrite(&BinOut, &TestM, sizeof(TestM));

                    State.FCW = State.FCW & ~X86_FCW_MASK_ALL;
                    IEMFPURESULT ResU = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                    pfn(&State, &ResU, &InVal);
                    FPU_UNARY_R80_TEST_T const TestU
                        = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, ResU.FSW, InVal, ResU.r80Result };
                    GenerateBinaryWrite(&BinOut, &TestU, sizeof(TestU));

                    uint16_t fXcpt = (ResM.FSW | ResU.FSW) & X86_FSW_XCPT_MASK & ~X86_FSW_SF;
                    if (fXcpt)
                    {
                        State.FCW = (State.FCW & ~X86_FCW_MASK_ALL) | fXcpt;
                        IEMFPURESULT Res1 = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                        pfn(&State, &Res1, &InVal);
                        FPU_UNARY_R80_TEST_T const Test1
                            = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, Res1.FSW, InVal, Res1.r80Result };
                        GenerateBinaryWrite(&BinOut, &Test1, sizeof(Test1));
                        if (((Res1.FSW & X86_FSW_XCPT_MASK) & fXcpt) != (Res1.FSW & X86_FSW_XCPT_MASK))
                        {
                            fXcpt |= Res1.FSW & X86_FSW_XCPT_MASK;
                            State.FCW = (State.FCW & ~X86_FCW_MASK_ALL) | fXcpt;
                            IEMFPURESULT Res2 = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                            pfn(&State, &Res2, &InVal);
                            FPU_UNARY_R80_TEST_T const Test2
                                = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, Res2.FSW, InVal, Res2.r80Result };
                            GenerateBinaryWrite(&BinOut, &Test2, sizeof(Test2));
                        }
                        if (!RT_IS_POWER_OF_TWO(fXcpt))
                            for (uint16_t fUnmasked = 1; fUnmasked <= X86_FCW_PM; fUnmasked <<= 1)
                                if (fUnmasked & fXcpt)
                                {
                                    State.FCW = (State.FCW & ~X86_FCW_MASK_ALL) | (fXcpt & ~fUnmasked);
                                    IEMFPURESULT Res3 = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                                    pfn(&State, &Res3, &InVal);
                                    FPU_UNARY_R80_TEST_T const Test3
                                        = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, Res3.FSW, InVal, Res3.r80Result };
                                    GenerateBinaryWrite(&BinOut, &Test3, sizeof(Test3));
                                }
                    }
                }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }
    return RTEXITCODE_SUCCESS;
}
#endif

static bool FpuIsEqualFcwMaybeIgnoreRoundErr(uint16_t fFcw1, uint16_t fFcw2, bool fRndErrOk, bool *pfRndErr)
{
    if (fFcw1 == fFcw2)
        return true;
    if (fRndErrOk && (fFcw1 & ~X86_FSW_C1) == (fFcw2 & ~X86_FSW_C1))
    {
        *pfRndErr = true;
        return true;
    }
    return false;
}

static bool FpuIsEqualR80MaybeIgnoreRoundErr(PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2, bool fRndErrOk, bool *pfRndErr)
{
    if (RTFLOAT80U_ARE_IDENTICAL(pr80Val1, pr80Val2))
        return true;
    if (   fRndErrOk
        && pr80Val1->s.fSign == pr80Val2->s.fSign)
    {
        if (   (   pr80Val1->s.uExponent == pr80Val2->s.uExponent
                && (  pr80Val1->s.uMantissa > pr80Val2->s.uMantissa
                    ? pr80Val1->s.uMantissa - pr80Val2->s.uMantissa == 1
                    : pr80Val2->s.uMantissa - pr80Val1->s.uMantissa == 1))
            ||
               (   pr80Val1->s.uExponent + 1 == pr80Val2->s.uExponent
                && pr80Val1->s.uMantissa == UINT64_MAX
                && pr80Val2->s.uMantissa == RT_BIT_64(63))
            ||
               (   pr80Val1->s.uExponent == pr80Val2->s.uExponent + 1
                && pr80Val2->s.uMantissa == UINT64_MAX
                && pr80Val1->s.uMantissa == RT_BIT_64(63)) )
        {
            *pfRndErr = true;
            return true;
        }
    }
    return false;
}


static void FpuUnaryR80Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuUnaryR80); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aFpuUnaryR80[iFn]))
            continue;

        FPU_UNARY_R80_TEST_T const * const paTests          = g_aFpuUnaryR80[iFn].paTests;
        uint32_t const                     cTests           = g_aFpuUnaryR80[iFn].cTests;
        PFNIEMAIMPLFPUR80UNARY             pfn              = g_aFpuUnaryR80[iFn].pfn;
        uint32_t const                     cVars            = COUNT_VARIATIONS(g_aFpuUnaryR80[iFn]);
        uint32_t                           cRndErrs         = 0;
        uint32_t                           cPossibleRndErrs = 0;
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTFLOAT80U const InVal     = paTests[iTest].InVal;
                IEMFPURESULT     Res       = { RTFLOAT80U_INIT(0, 0, 0), 0 };
                bool const       fRndErrOk = RT_BOOL(paTests[iTest].fFcw & 0x80);
                State.FCW = paTests[iTest].fFcw & ~(uint16_t)0x80;
                State.FSW = paTests[iTest].fFswIn;
                pfn(&State, &Res, &InVal);
                bool fRndErr = false;
                if (   !FpuIsEqualFcwMaybeIgnoreRoundErr(Res.FSW, paTests[iTest].fFswOut, fRndErrOk, &fRndErr)
                    || !FpuIsEqualR80MaybeIgnoreRoundErr(&Res.r80Result, &paTests[iTest].OutVal, fRndErrOk, &fRndErr))
                    RTTestFailed(g_hTest, "#%04u%s: fcw=%#06x fsw=%#06x in=%s\n"
                                          "%s               -> fsw=%#06x    %s\n"
                                          "%s             expected %#06x    %s%s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn,
                                 FormatR80(&paTests[iTest].InVal),
                                 iVar ? "  " : "", Res.FSW, FormatR80(&Res.r80Result),
                                 iVar ? "  " : "", paTests[iTest].fFswOut, FormatR80(&paTests[iTest].OutVal),
                                 FswDiff(Res.FSW, paTests[iTest].fFswOut),
                                 !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result, &paTests[iTest].OutVal) ? " - val" : "",
                                 fRndErrOk ? " - rounding errors ok" : "", FormatFcw(paTests[iTest].fFcw));
                cRndErrs         += fRndErr;
                cPossibleRndErrs += fRndErrOk;
            }
            pfn = g_aFpuUnaryR80[iFn].pfnNative;
        }
        if (cPossibleRndErrs > 0)
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "rounding errors: %u out of %u\n", cRndErrs, cPossibleRndErrs);
        FREE_DECOMPRESSED_TESTS(g_aFpuUnaryR80[iFn]);
    }
}


/*
 * Unary FPU operations on one 80-bit floating point value, but only affects the FSW.
 */
TYPEDEF_SUBTEST_TYPE(FPU_UNARY_FSW_R80_T, FPU_UNARY_R80_TEST_T, PFNIEMAIMPLFPUR80UNARYFSW);

static FPU_UNARY_FSW_R80_T g_aFpuUnaryFswR80[] =
{
    ENTRY_BIN(ftst_r80),
    ENTRY_BIN_EX(fxam_r80, 1),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE FpuUnaryFswR80Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    static RTFLOAT80U const s_aSpecials[] =
    {
        RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS), /* whatever */
    };

    X86FXSTATE State;
    RT_ZERO(State);
    uint32_t cMinNormals = cTests / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuUnaryFswR80); iFn++)
    {
        bool const                  fIsFxam = g_aFpuUnaryFswR80[iFn].uExtra == 1;
        PFNIEMAIMPLFPUR80UNARYFSW const pfn = g_aFpuUnaryFswR80[iFn].pfnNative ? g_aFpuUnaryFswR80[iFn].pfnNative : g_aFpuUnaryFswR80[iFn].pfn;
        if (   g_aFpuUnaryFswR80[iFn].idxCpuEflFlavour != IEMTARGETCPU_EFL_BEHAVIOR_NATIVE
            && g_aFpuUnaryFswR80[iFn].idxCpuEflFlavour != g_idxCpuEflFlavour)
            continue;
        State.FTW = 0;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aFpuUnaryFswR80[iFn]), RTEXITCODE_FAILURE);
        uint32_t cNormalInputs = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            RTFLOAT80U const InVal = iTest < cTests ? RandR80Src(iTest) : s_aSpecials[iTest - cTests];
            if (RTFLOAT80U_IS_NORMAL(&InVal))
                cNormalInputs++;
            else if (cNormalInputs < cMinNormals && iTest + cMinNormals >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint16_t const fFcw = RandFcw();
            State.FSW = RandFsw();
            if (!fIsFxam)
            {
                for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                {
                    for (uint16_t iPrecision = 0; iPrecision < 4; iPrecision++)
                    {
                        for (uint16_t iMask = 0; iMask <= X86_FCW_MASK_ALL; iMask += X86_FCW_MASK_ALL)
                        {
                            State.FCW = (fFcw & ~(X86_FCW_RC_MASK | X86_FCW_PC_MASK | X86_FCW_MASK_ALL))
                                      | (iRounding  << X86_FCW_RC_SHIFT)
                                      | (iPrecision << X86_FCW_PC_SHIFT)
                                      | iMask;
                            uint16_t fFswOut = 0;
                            pfn(&State, &fFswOut, &InVal);
                            FPU_UNARY_R80_TEST_T const Test = { State.FCW, State.FSW, fFswOut, InVal };
                            GenerateBinaryWrite(&BinOut, &Test, sizeof(Test));
                        }
                    }
                }
            }
            else
            {
                uint16_t       fFswOut = 0;
                uint16_t const fEmpty  = RTRandU32Ex(0, 3) == 3 ? 0x80 : 0; /* Using MBZ bit 7 in FCW to indicate empty tag value. */
                State.FTW = !fEmpty ? 1 << X86_FSW_TOP_GET(State.FSW) : 0;
                State.FCW = fFcw;
                pfn(&State, &fFswOut, &InVal);
                FPU_UNARY_R80_TEST_T const Test = { (uint16_t)(fFcw | fEmpty), State.FSW, fFswOut, InVal };
                GenerateBinaryWrite(&BinOut, &Test, sizeof(Test));
            }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }
    return RTEXITCODE_SUCCESS;
}
DUMP_ALL_FN(FpuUnaryFswR80, g_aFpuUnaryFswR80)
#endif


static void FpuUnaryFswR80Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuUnaryFswR80); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aFpuUnaryFswR80[iFn]))
            continue;

        FPU_UNARY_R80_TEST_T const * const paTests = g_aFpuUnaryFswR80[iFn].paTests;
        uint32_t const                     cTests  = g_aFpuUnaryFswR80[iFn].cTests;
        PFNIEMAIMPLFPUR80UNARYFSW          pfn     = g_aFpuUnaryFswR80[iFn].pfn;
        uint32_t const                     cVars   = COUNT_VARIATIONS(g_aFpuUnaryFswR80[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTFLOAT80U const InVal   = paTests[iTest].InVal;
                uint16_t         fFswOut = 0;
                State.FSW = paTests[iTest].fFswIn;
                State.FCW = paTests[iTest].fFcw & ~(uint16_t)0x80; /* see generator code */
                State.FTW = paTests[iTest].fFcw & 0x80 ? 0 : 1 << X86_FSW_TOP_GET(paTests[iTest].fFswIn);
                pfn(&State, &fFswOut, &InVal);
                if (fFswOut != paTests[iTest].fFswOut)
                    RTTestFailed(g_hTest, "#%04u%s: fcw=%#06x fsw=%#06x in=%s\n"
                                          "%s               -> fsw=%#06x\n"
                                          "%s             expected %#06x  %s (%s%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn,
                                 FormatR80(&paTests[iTest].InVal),
                                 iVar ? "  " : "", fFswOut,
                                 iVar ? "  " : "", paTests[iTest].fFswOut,
                                 FswDiff(fFswOut, paTests[iTest].fFswOut), FormatFcw(paTests[iTest].fFcw),
                                 paTests[iTest].fFcw & 0x80 ? " empty" : "");
            }
            pfn = g_aFpuUnaryFswR80[iFn].pfnNative;
        }

        FREE_DECOMPRESSED_TESTS(g_aFpuUnaryFswR80[iFn]);
    }
}

/*
 * Unary FPU operations on one 80-bit floating point value, but with two outputs.
 */
TYPEDEF_SUBTEST_TYPE(FPU_UNARY_TWO_R80_T, FPU_UNARY_TWO_R80_TEST_T, PFNIEMAIMPLFPUR80UNARYTWO);

static FPU_UNARY_TWO_R80_T g_aFpuUnaryTwoR80[] =
{
    ENTRY_BIN(fxtract_r80_r80),
    ENTRY_BIN_AMD(  fptan_r80_r80, 0),   // rounding differences
    ENTRY_BIN_INTEL(fptan_r80_r80, 0),
    ENTRY_BIN_AMD(  fsincos_r80_r80, 0), // C1 differences & value differences (e.g. -1m0x235cf2f580244a27^-1696)
    ENTRY_BIN_INTEL(fsincos_r80_r80, 0),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
static RTEXITCODE FpuUnaryTwoR80Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    static RTFLOAT80U const s_aSpecials[] =
    {
        RTFLOAT80U_INIT_C(0, 0xffffeeeeddddcccc, RTFLOAT80U_EXP_BIAS), /* whatever */
    };

    X86FXSTATE State;
    RT_ZERO(State);
    uint32_t cMinNormals = cTests / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuUnaryTwoR80); iFn++)
    {
        PFNIEMAIMPLFPUR80UNARYTWO const pfn = g_aFpuUnaryTwoR80[iFn].pfnNative ? g_aFpuUnaryTwoR80[iFn].pfnNative : g_aFpuUnaryTwoR80[iFn].pfn;
        if (   g_aFpuUnaryTwoR80[iFn].idxCpuEflFlavour != IEMTARGETCPU_EFL_BEHAVIOR_NATIVE
            && g_aFpuUnaryTwoR80[iFn].idxCpuEflFlavour != g_idxCpuEflFlavour)
            continue;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aFpuUnaryTwoR80[iFn]), RTEXITCODE_FAILURE);
        uint32_t cNormalInputs      = 0;
        uint32_t cTargetRangeInputs = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            RTFLOAT80U InVal = iTest < cTests ? RandR80Src(iTest) : s_aSpecials[iTest - cTests];
            if (RTFLOAT80U_IS_NORMAL(&InVal))
            {
                if (iFn != 0)
                {
                    unsigned uTargetExp = RTFLOAT80U_EXP_BIAS + 63 + 1 /* 2^64..2^-64 */;
                    unsigned cTargetExp = g_aFpuUnaryR80[iFn].uExtra == kUnary_Rounding_F2xm1 ? 69 : 63*2 + 2;
                    if (InVal.s.uExponent <= uTargetExp && InVal.s.uExponent >= uTargetExp - cTargetExp)
                        cTargetRangeInputs++;
                    else if (cTargetRangeInputs < cMinNormals / 2 && iTest + cMinNormals / 2 >= cTests && iTest < cTests)
                    {
                        InVal.s.uExponent = RTRandU32Ex(uTargetExp - cTargetExp, uTargetExp);
                        cTargetRangeInputs++;
                    }
                }
                cNormalInputs++;
            }
            else if (cNormalInputs < cMinNormals && iTest + cMinNormals >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint16_t const fFcwExtra = 0; /* for rounding error indication */
            uint16_t const fFcw = RandFcw();
            State.FSW = RandFsw();

            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint16_t iPrecision = 0; iPrecision < 4; iPrecision++)
                {
                    State.FCW = (fFcw & ~(X86_FCW_RC_MASK | X86_FCW_PC_MASK | X86_FCW_MASK_ALL))
                              | (iRounding  << X86_FCW_RC_SHIFT)
                              | (iPrecision << X86_FCW_PC_SHIFT)
                              | X86_FCW_MASK_ALL;
                    IEMFPURESULTTWO ResM = { RTFLOAT80U_INIT(0, 0, 0), 0, RTFLOAT80U_INIT(0, 0, 0) };
                    pfn(&State, &ResM, &InVal);
                    FPU_UNARY_TWO_R80_TEST_T const TestM
                        = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, ResM.FSW, InVal, ResM.r80Result1, ResM.r80Result2 };
                    GenerateBinaryWrite(&BinOut, &TestM, sizeof(TestM));

                    State.FCW = State.FCW & ~X86_FCW_MASK_ALL;
                    IEMFPURESULTTWO ResU = { RTFLOAT80U_INIT(0, 0, 0), 0, RTFLOAT80U_INIT(0, 0, 0) };
                    pfn(&State, &ResU, &InVal);
                    FPU_UNARY_TWO_R80_TEST_T const TestU
                        = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, ResU.FSW, InVal, ResU.r80Result1, ResU.r80Result2 };
                    GenerateBinaryWrite(&BinOut, &TestU, sizeof(TestU));

                    uint16_t fXcpt = (ResM.FSW | ResU.FSW) & X86_FSW_XCPT_MASK & ~X86_FSW_SF;
                    if (fXcpt)
                    {
                        State.FCW = (State.FCW & ~X86_FCW_MASK_ALL) | fXcpt;
                        IEMFPURESULTTWO Res1 = { RTFLOAT80U_INIT(0, 0, 0), 0, RTFLOAT80U_INIT(0, 0, 0) };
                        pfn(&State, &Res1, &InVal);
                        FPU_UNARY_TWO_R80_TEST_T const Test1
                            = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, Res1.FSW, InVal, Res1.r80Result1, Res1.r80Result2 };
                        GenerateBinaryWrite(&BinOut, &Test1, sizeof(Test1));

                        if (((Res1.FSW & X86_FSW_XCPT_MASK) & fXcpt) != (Res1.FSW & X86_FSW_XCPT_MASK))
                        {
                            fXcpt |= Res1.FSW & X86_FSW_XCPT_MASK;
                            State.FCW = (State.FCW & ~X86_FCW_MASK_ALL) | fXcpt;
                            IEMFPURESULTTWO Res2 = { RTFLOAT80U_INIT(0, 0, 0), 0, RTFLOAT80U_INIT(0, 0, 0) };
                            pfn(&State, &Res2, &InVal);
                            FPU_UNARY_TWO_R80_TEST_T const Test2
                                = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, Res2.FSW, InVal, Res2.r80Result1, Res2.r80Result2 };
                            GenerateBinaryWrite(&BinOut, &Test2, sizeof(Test2));
                        }
                        if (!RT_IS_POWER_OF_TWO(fXcpt))
                            for (uint16_t fUnmasked = 1; fUnmasked <= X86_FCW_PM; fUnmasked <<= 1)
                                if (fUnmasked & fXcpt)
                                {
                                    State.FCW = (State.FCW & ~X86_FCW_MASK_ALL) | (fXcpt & ~fUnmasked);
                                    IEMFPURESULTTWO Res3 = { RTFLOAT80U_INIT(0, 0, 0), 0, RTFLOAT80U_INIT(0, 0, 0) };
                                    pfn(&State, &Res3, &InVal);
                                    FPU_UNARY_TWO_R80_TEST_T const Test3
                                        = { (uint16_t)(State.FCW | fFcwExtra), State.FSW, Res3.FSW, InVal, Res3.r80Result1, Res3.r80Result2 };
                                    GenerateBinaryWrite(&BinOut, &Test3, sizeof(Test3));
                                }
                    }
                }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }
    return RTEXITCODE_SUCCESS;
}
DUMP_ALL_FN(FpuUnaryTwoR80, g_aFpuUnaryTwoR80)
#endif


static void FpuUnaryTwoR80Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aFpuUnaryTwoR80); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aFpuUnaryTwoR80[iFn]))
            continue;

        FPU_UNARY_TWO_R80_TEST_T const * const paTests = g_aFpuUnaryTwoR80[iFn].paTests;
        uint32_t const                         cTests  = g_aFpuUnaryTwoR80[iFn].cTests;
        PFNIEMAIMPLFPUR80UNARYTWO              pfn     = g_aFpuUnaryTwoR80[iFn].pfn;
        uint32_t const                         cVars   = COUNT_VARIATIONS(g_aFpuUnaryTwoR80[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                IEMFPURESULTTWO  Res     = { RTFLOAT80U_INIT(0, 0, 0), 0, RTFLOAT80U_INIT(0, 0, 0) };
                RTFLOAT80U const InVal   = paTests[iTest].InVal;
                State.FCW = paTests[iTest].fFcw;
                State.FSW = paTests[iTest].fFswIn;
                pfn(&State, &Res, &InVal);
                if (   Res.FSW != paTests[iTest].fFswOut
                    || !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result1, &paTests[iTest].OutVal1)
                    || !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result2, &paTests[iTest].OutVal2) )
                    RTTestFailed(g_hTest, "#%04u%s: fcw=%#06x fsw=%#06x in=%s\n"
                                          "%s               -> fsw=%#06x %s %s\n"
                                          "%s             expected %#06x %s %s %s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fFcw, paTests[iTest].fFswIn,
                                 FormatR80(&paTests[iTest].InVal),
                                 iVar ? "  " : "", Res.FSW, FormatR80(&Res.r80Result1), FormatR80(&Res.r80Result2),
                                 iVar ? "  " : "", paTests[iTest].fFswOut,
                                 FormatR80(&paTests[iTest].OutVal1), FormatR80(&paTests[iTest].OutVal2),
                                 !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result1, &paTests[iTest].OutVal1) ? " - val1" : "",
                                 !RTFLOAT80U_ARE_IDENTICAL(&Res.r80Result2, &paTests[iTest].OutVal2) ? " - val2" : "",
                                 FswDiff(Res.FSW, paTests[iTest].fFswOut), FormatFcw(paTests[iTest].fFcw) );
            }
            pfn = g_aFpuUnaryTwoR80[iFn].pfnNative;
        }

        FREE_DECOMPRESSED_TESTS(g_aFpuUnaryTwoR80[iFn]);
    }
}


/*********************************************************************************************************************************
*   SSE floating point Binary Operations                                                                                         *
*********************************************************************************************************************************/

/*
 * Binary SSE operations on packed single precision floating point values.
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_R32_T, SSE_BINARY_TEST_T, PFNIEMAIMPLFPSSEF2U128);

/** Ugly hack to keep it working after changing function arguments! */
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtps2pd_u128x,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);
    return iemAImpl_cvtps2pd_u128(uMxCsrIn, pResult, &puSrc2->au64[0]);
}

static SSE_BINARY_R32_T g_aSseBinaryR32[] =
{
    ENTRY_BIN(addps_u128),
    ENTRY_BIN(mulps_u128),
    ENTRY_BIN(subps_u128),
    ENTRY_BIN(minps_u128),
    ENTRY_BIN(divps_u128),
    ENTRY_BIN(maxps_u128),
    ENTRY_BIN(haddps_u128),
    ENTRY_BIN(hsubps_u128),
    ENTRY_BIN(sqrtps_u128),
    ENTRY_BIN(addsubps_u128),
    ENTRY_BIN(cvtps2pd_u128x), /* conversion hack */
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryR32, g_aSseBinaryR32)
static RTEXITCODE SseBinaryR32Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT32U aVal1[4], aVal2[4]; } const s_aSpecials[] =
    {
        {   { RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0), },
            { RTFLOAT32U_INIT_C(0, 8388607, RTFLOAT32U_EXP_MAX - 1), RTFLOAT32U_INIT_C(0, 8388607, RTFLOAT32U_EXP_MAX - 1), RTFLOAT32U_INIT_C(0, 8388607, RTFLOAT32U_EXP_MAX - 1), RTFLOAT32U_INIT_C(0, 8388607, RTFLOAT32U_EXP_MAX - 1) } },
            /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR32); iFn++)
    {
        PFNIEMAIMPLFPSSEF2U128 const pfn = g_aSseBinaryR32[iFn].pfnNative ? g_aSseBinaryR32[iFn].pfnNative : g_aSseBinaryR32[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryR32[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal1.ar32[0] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[0];
            TestData.InVal1.ar32[1] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[1];
            TestData.InVal1.ar32[2] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[2];
            TestData.InVal1.ar32[3] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[3];

            TestData.InVal2.ar32[0] = iTest < cTests ? RandR32Src2(iTest) : s_aSpecials[iTest - cTests].aVal2[0];
            TestData.InVal2.ar32[1] = iTest < cTests ? RandR32Src2(iTest) : s_aSpecials[iTest - cTests].aVal2[1];
            TestData.InVal2.ar32[2] = iTest < cTests ? RandR32Src2(iTest) : s_aSpecials[iTest - cTests].aVal2[2];
            TestData.InVal2.ar32[3] = iTest < cTests ? RandR32Src2(iTest) : s_aSpecials[iTest - cTests].aVal2[3];

            if (   RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[0]) && RTFLOAT32U_IS_NORMAL(&TestData.InVal2.ar32[0])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[1]) && RTFLOAT32U_IS_NORMAL(&TestData.InVal2.ar32[1])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[2]) && RTFLOAT32U_IS_NORMAL(&TestData.InVal2.ar32[2])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[3]) && RTFLOAT32U_IS_NORMAL(&TestData.InVal2.ar32[3]))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        X86XMMREG ResM; RT_ZERO(ResM);
                        uint32_t uMxCsrOutM = pfn(uMxCsrIn, &ResM, &TestData.InVal1, &TestData.InVal2);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutM;
                        TestData.OutVal    = ResM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        X86XMMREG ResU; RT_ZERO(ResU);
                        uint32_t uMxCsrOutU = pfn(uMxCsrIn, &ResU, &TestData.InVal1, &TestData.InVal2);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutU;
                        TestData.OutVal    = ResU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (uMxCsrOutM | uMxCsrOutU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            X86XMMREG Res1; RT_ZERO(Res1);
                            uint32_t uMxCsrOut1 = pfn(uMxCsrIn, &Res1, &TestData.InVal1, &TestData.InVal2);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = uMxCsrOut1;
                            TestData.OutVal    = Res1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                X86XMMREG Res2; RT_ZERO(Res2);
                                uint32_t uMxCsrOut2 = pfn(uMxCsrIn, &Res2, &TestData.InVal1, &TestData.InVal2);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = uMxCsrOut2;
                                TestData.OutVal    = Res2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        X86XMMREG Res3; RT_ZERO(Res3);
                                        uint32_t uMxCsrOut3 = pfn(uMxCsrIn, &Res3, &TestData.InVal1, &TestData.InVal2);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = uMxCsrOut3;
                                        TestData.OutVal    = Res3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseBinaryR32Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR32); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryR32[iFn]))
            continue;

        SSE_BINARY_TEST_T const * const paTests = g_aSseBinaryR32[iFn].paTests;
        uint32_t const                  cbTests = g_aSseBinaryR32[iFn].cTests;
        PFNIEMAIMPLFPSSEF2U128          pfn     = g_aSseBinaryR32[iFn].pfn;
        uint32_t const                  cVars   = COUNT_VARIATIONS(g_aSseBinaryR32[iFn]);
        if (!cbTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cbTests / sizeof(paTests[0]); iTest++)
            {
                X86XMMREG Res; RT_ZERO(Res);

                uint32_t uMxCsrOut = pfn(paTests[iTest].fMxcsrIn, &Res, &paTests[iTest].InVal1, &paTests[iTest].InVal2);
                bool fValsIdentical =    RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[0], &paTests[iTest].OutVal.ar32[0])
                                      && RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[1], &paTests[iTest].OutVal.ar32[1])
                                      && RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[2], &paTests[iTest].OutVal.ar32[2])
                                      && RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[3], &paTests[iTest].OutVal.ar32[3]);
                if (   uMxCsrOut != paTests[iTest].fMxcsrOut
                    || !fValsIdentical)
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s'%s'%s'%s in2=%s'%s'%s'%s\n"
                                          "%s               -> mxcsr=%#08x    %s'%s'%s'%s\n"
                                          "%s               expected %#08x    %s'%s'%s'%s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR32(&paTests[iTest].InVal1.ar32[0]), FormatR32(&paTests[iTest].InVal1.ar32[1]),
                                 FormatR32(&paTests[iTest].InVal1.ar32[2]), FormatR32(&paTests[iTest].InVal1.ar32[3]),
                                 FormatR32(&paTests[iTest].InVal2.ar32[0]), FormatR32(&paTests[iTest].InVal2.ar32[1]),
                                 FormatR32(&paTests[iTest].InVal2.ar32[2]), FormatR32(&paTests[iTest].InVal2.ar32[3]),
                                 iVar ? "  " : "", uMxCsrOut,
                                 FormatR32(&Res.ar32[0]), FormatR32(&Res.ar32[1]),
                                 FormatR32(&Res.ar32[2]), FormatR32(&Res.ar32[3]),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 FormatR32(&paTests[iTest].OutVal.ar32[0]), FormatR32(&paTests[iTest].OutVal.ar32[1]),
                                 FormatR32(&paTests[iTest].OutVal.ar32[2]), FormatR32(&paTests[iTest].OutVal.ar32[3]),
                                 MxcsrDiff(uMxCsrOut, paTests[iTest].fMxcsrOut),
                                 !fValsIdentical ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
            pfn = g_aSseBinaryR32[iFn].pfnNative;
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryR32[iFn]);
    }
}


/*
 * Binary SSE operations on packed single precision floating point values.
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_R64_T, SSE_BINARY_TEST_T, PFNIEMAIMPLFPSSEF2U128);

static SSE_BINARY_R64_T g_aSseBinaryR64[] =
{
    ENTRY_BIN(addpd_u128),
    ENTRY_BIN(mulpd_u128),
    ENTRY_BIN(subpd_u128),
    ENTRY_BIN(minpd_u128),
    ENTRY_BIN(divpd_u128),
    ENTRY_BIN(maxpd_u128),
    ENTRY_BIN(haddpd_u128),
    ENTRY_BIN(hsubpd_u128),
    ENTRY_BIN(sqrtpd_u128),
    ENTRY_BIN(addsubpd_u128),
    ENTRY_BIN(cvtpd2ps_u128),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryR64, g_aSseBinaryR32)
static RTEXITCODE SseBinaryR64Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT64U aVal1[2], aVal2[2]; } const s_aSpecials[] =
    {
        {   { RTFLOAT64U_INIT_ZERO(0), RTFLOAT64U_INIT_ZERO(0) },
            { RTFLOAT64U_INIT_C(0, 8388607, RTFLOAT64U_EXP_MAX - 1), RTFLOAT64U_INIT_C(0, 8388607, RTFLOAT64U_EXP_MAX - 1) } },
            /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR64); iFn++)
    {
        PFNIEMAIMPLFPSSEF2U128 const pfn = g_aSseBinaryR64[iFn].pfnNative ? g_aSseBinaryR64[iFn].pfnNative : g_aSseBinaryR64[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryR64[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal1.ar64[0] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].aVal1[0];
            TestData.InVal1.ar64[1] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].aVal1[0];
            TestData.InVal2.ar64[0] = iTest < cTests ? RandR64Src2(iTest) : s_aSpecials[iTest - cTests].aVal2[0];
            TestData.InVal2.ar64[1] = iTest < cTests ? RandR64Src2(iTest) : s_aSpecials[iTest - cTests].aVal2[0];

            if (   RTFLOAT64U_IS_NORMAL(&TestData.InVal1.ar64[0]) && RTFLOAT64U_IS_NORMAL(&TestData.InVal1.ar64[1])
                && RTFLOAT64U_IS_NORMAL(&TestData.InVal2.ar64[0]) && RTFLOAT64U_IS_NORMAL(&TestData.InVal2.ar64[1]))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        X86XMMREG ResM; RT_ZERO(ResM);
                        uint32_t uMxCsrOutM = pfn(uMxCsrIn, &ResM, &TestData.InVal1, &TestData.InVal2);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutM;
                        TestData.OutVal    = ResM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        X86XMMREG ResU; RT_ZERO(ResU);
                        uint32_t uMxCsrOutU = pfn(uMxCsrIn, &ResU, &TestData.InVal1, &TestData.InVal2);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutU;
                        TestData.OutVal    = ResU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (uMxCsrOutM | uMxCsrOutU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            X86XMMREG Res1; RT_ZERO(Res1);
                            uint32_t uMxCsrOut1 = pfn(uMxCsrIn, &Res1, &TestData.InVal1, &TestData.InVal2);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = uMxCsrOut1;
                            TestData.OutVal    = Res1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                X86XMMREG Res2; RT_ZERO(Res2);
                                uint32_t uMxCsrOut2 = pfn(uMxCsrIn, &Res2, &TestData.InVal1, &TestData.InVal2);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = uMxCsrOut2;
                                TestData.OutVal    = Res2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        X86XMMREG Res3; RT_ZERO(Res3);
                                        uint32_t uMxCsrOut3 = pfn(uMxCsrIn, &Res3, &TestData.InVal1, &TestData.InVal2);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = uMxCsrOut3;
                                        TestData.OutVal    = Res3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif


static void SseBinaryR64Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR64); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryR64[iFn]))
            continue;

        SSE_BINARY_TEST_T const * const paTests = g_aSseBinaryR64[iFn].paTests;
        uint32_t const                  cTests  = g_aSseBinaryR64[iFn].cTests;
        PFNIEMAIMPLFPSSEF2U128          pfn     = g_aSseBinaryR64[iFn].pfn;
        uint32_t const                  cVars   = COUNT_VARIATIONS(g_aSseBinaryR64[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                X86XMMREG Res; RT_ZERO(Res);

                uint32_t uMxCsrIn = paTests[iTest].fMxcsrIn;
                uint32_t uMxCsrOut = pfn(uMxCsrIn, &Res, &paTests[iTest].InVal1, &paTests[iTest].InVal2);
                if (   uMxCsrOut != paTests[iTest].fMxcsrOut
                    || !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[0], &paTests[iTest].OutVal.ar64[0])
                    || !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[1], &paTests[iTest].OutVal.ar64[1]))
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s'%s in2=%s'%s\n"
                                          "%s               -> mxcsr=%#08x    %s'%s\n"
                                          "%s               expected %#08x    %s'%s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR64(&paTests[iTest].InVal1.ar64[0]), FormatR64(&paTests[iTest].InVal1.ar64[1]),
                                 FormatR64(&paTests[iTest].InVal2.ar64[0]), FormatR64(&paTests[iTest].InVal2.ar64[1]),
                                 iVar ? "  " : "", uMxCsrOut,
                                 FormatR64(&Res.ar64[0]), FormatR64(&Res.ar64[1]),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 FormatR64(&paTests[iTest].OutVal.ar64[0]), FormatR64(&paTests[iTest].OutVal.ar64[1]),
                                 MxcsrDiff(uMxCsrOut, paTests[iTest].fMxcsrOut),
                                   (   !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[0], &paTests[iTest].OutVal.ar64[0])
                                    || !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[1], &paTests[iTest].OutVal.ar64[1]))
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
            pfn = g_aSseBinaryR64[iFn].pfnNative;
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryR64[iFn]);
    }
}


/*
 * Binary SSE operations on packed single precision floating point values.
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_U128_R32_T, SSE_BINARY_U128_R32_TEST_T, PFNIEMAIMPLFPSSEF2U128R32);

static SSE_BINARY_U128_R32_T g_aSseBinaryU128R32[] =
{
    ENTRY_BIN(addss_u128_r32),
    ENTRY_BIN(mulss_u128_r32),
    ENTRY_BIN(subss_u128_r32),
    ENTRY_BIN(minss_u128_r32),
    ENTRY_BIN(divss_u128_r32),
    ENTRY_BIN(maxss_u128_r32),
    ENTRY_BIN(cvtss2sd_u128_r32),
    ENTRY_BIN(sqrtss_u128_r32),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryU128R32, g_aSseBinaryU128R32)
static RTEXITCODE SseBinaryU128R32Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT32U aVal1[4], Val2; } const s_aSpecials[] =
    {
        {   { RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0), }, RTFLOAT32U_INIT_C(0, 8388607, RTFLOAT32U_EXP_MAX - 1) },
            /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryU128R32); iFn++)
    {
        PFNIEMAIMPLFPSSEF2U128R32 const pfn = g_aSseBinaryU128R32[iFn].pfnNative ? g_aSseBinaryU128R32[iFn].pfnNative : g_aSseBinaryU128R32[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryU128R32[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_U128_R32_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal1.ar32[0] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[0];
            TestData.InVal1.ar32[1] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[1];
            TestData.InVal1.ar32[2] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[2];
            TestData.InVal1.ar32[3] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[3];

            TestData.r32Val2 = iTest < cTests ? RandR32Src2(iTest) : s_aSpecials[iTest - cTests].Val2;

            if (   RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[0])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[1])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[2])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[3])
                && RTFLOAT32U_IS_NORMAL(&TestData.r32Val2))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        X86XMMREG ResM; RT_ZERO(ResM);
                        uint32_t uMxCsrOutM = pfn(uMxCsrIn, &ResM, &TestData.InVal1, &TestData.r32Val2);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutM;
                        TestData.OutVal    = ResM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        X86XMMREG ResU; RT_ZERO(ResU);
                        uint32_t uMxCsrOutU = pfn(uMxCsrIn, &ResU, &TestData.InVal1, &TestData.r32Val2);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutU;
                        TestData.OutVal    = ResU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (uMxCsrOutM | uMxCsrOutU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            X86XMMREG Res1; RT_ZERO(Res1);
                            uint32_t uMxCsrOut1 = pfn(uMxCsrIn, &Res1, &TestData.InVal1, &TestData.r32Val2);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = uMxCsrOut1;
                            TestData.OutVal    = Res1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                X86XMMREG Res2; RT_ZERO(Res2);
                                uint32_t uMxCsrOut2 = pfn(uMxCsrIn, &Res2, &TestData.InVal1, &TestData.r32Val2);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = uMxCsrOut2;
                                TestData.OutVal    = Res2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        X86XMMREG Res3; RT_ZERO(Res3);
                                        uint32_t uMxCsrOut3 = pfn(uMxCsrIn, &Res3, &TestData.InVal1, &TestData.r32Val2);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = uMxCsrOut3;
                                        TestData.OutVal    = Res3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseBinaryU128R32Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryU128R32); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryU128R32[iFn]))
            continue;

        SSE_BINARY_U128_R32_TEST_T const * const paTests = g_aSseBinaryU128R32[iFn].paTests;
        uint32_t const                           cTests  = g_aSseBinaryU128R32[iFn].cTests;
        PFNIEMAIMPLFPSSEF2U128R32                pfn     = g_aSseBinaryU128R32[iFn].pfn;
        uint32_t const                           cVars   = COUNT_VARIATIONS(g_aSseBinaryU128R32[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                X86XMMREG Res; RT_ZERO(Res);

                uint32_t uMxCsrIn = paTests[iTest].fMxcsrIn;
                uint32_t uMxCsrOut = pfn(uMxCsrIn, &Res, &paTests[iTest].InVal1, &paTests[iTest].r32Val2);
                bool fValsIdentical =    RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[0], &paTests[iTest].OutVal.ar32[0])
                                      && RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[1], &paTests[iTest].OutVal.ar32[1])
                                      && RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[2], &paTests[iTest].OutVal.ar32[2])
                                      && RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[3], &paTests[iTest].OutVal.ar32[3]);
                if (   uMxCsrOut != paTests[iTest].fMxcsrOut
                    || !fValsIdentical)
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s'%s'%s'%s in2=%s\n"
                                          "%s               -> mxcsr=%#08x    %s'%s'%s'%s\n"
                                          "%s               expected %#08x    %s'%s'%s'%s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR32(&paTests[iTest].InVal1.ar32[0]), FormatR32(&paTests[iTest].InVal1.ar32[1]),
                                 FormatR32(&paTests[iTest].InVal1.ar32[2]), FormatR32(&paTests[iTest].InVal1.ar32[3]),
                                 FormatR32(&paTests[iTest].r32Val2),
                                 iVar ? "  " : "", uMxCsrOut,
                                 FormatR32(&Res.ar32[0]), FormatR32(&Res.ar32[1]),
                                 FormatR32(&Res.ar32[2]), FormatR32(&Res.ar32[3]),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 FormatR32(&paTests[iTest].OutVal.ar32[0]), FormatR32(&paTests[iTest].OutVal.ar32[1]),
                                 FormatR32(&paTests[iTest].OutVal.ar32[2]), FormatR32(&paTests[iTest].OutVal.ar32[3]),
                                 MxcsrDiff(uMxCsrOut, paTests[iTest].fMxcsrOut),
                                 !fValsIdentical ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryU128R32[iFn]);
    }
}


/*
 * Binary SSE operations on packed single precision floating point values (xxxsd xmm1, r/m64).
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_U128_R64_T, SSE_BINARY_U128_R64_TEST_T, PFNIEMAIMPLFPSSEF2U128R64);

static SSE_BINARY_U128_R64_T g_aSseBinaryU128R64[] =
{
    ENTRY_BIN(addsd_u128_r64),
    ENTRY_BIN(mulsd_u128_r64),
    ENTRY_BIN(subsd_u128_r64),
    ENTRY_BIN(minsd_u128_r64),
    ENTRY_BIN(divsd_u128_r64),
    ENTRY_BIN(maxsd_u128_r64),
    ENTRY_BIN(cvtsd2ss_u128_r64),
    ENTRY_BIN(sqrtsd_u128_r64),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryU128R64, g_aSseBinaryU128R64)
static RTEXITCODE SseBinaryU128R64Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT64U aVal1[2], Val2; } const s_aSpecials[] =
    {
        {   { RTFLOAT64U_INIT_ZERO(0), RTFLOAT64U_INIT_ZERO(0) }, RTFLOAT64U_INIT_C(0, 8388607, RTFLOAT64U_EXP_MAX - 1) },
            /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryU128R64); iFn++)
    {
        PFNIEMAIMPLFPSSEF2U128R64 const pfn = g_aSseBinaryU128R64[iFn].pfnNative ? g_aSseBinaryU128R64[iFn].pfnNative : g_aSseBinaryU128R64[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryU128R64[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_U128_R64_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal1.ar64[0] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].aVal1[0];
            TestData.InVal1.ar64[1] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].aVal1[1];
            TestData.r64Val2        = iTest < cTests ? RandR64Src2(iTest) : s_aSpecials[iTest - cTests].Val2;

            if (   RTFLOAT64U_IS_NORMAL(&TestData.InVal1.ar64[0]) && RTFLOAT64U_IS_NORMAL(&TestData.InVal1.ar64[1])
                && RTFLOAT64U_IS_NORMAL(&TestData.r64Val2))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t uMxCsrOutM = pfn(uMxCsrIn, &TestData.OutVal, &TestData.InVal1, &TestData.r64Val2);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t uMxCsrOutU = pfn(uMxCsrIn, &TestData.OutVal, &TestData.InVal1, &TestData.r64Val2);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (uMxCsrOutM | uMxCsrOutU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t uMxCsrOut1 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.InVal1, &TestData.r64Val2);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = uMxCsrOut1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t uMxCsrOut2 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.InVal1, &TestData.r64Val2);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = uMxCsrOut2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t uMxCsrOut3 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.InVal1, &TestData.r64Val2);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = uMxCsrOut3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif


static void SseBinaryU128R64Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryU128R64); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryU128R64[iFn]))
            continue;

        SSE_BINARY_U128_R64_TEST_T const * const paTests = g_aSseBinaryU128R64[iFn].paTests;
        uint32_t const                           cTests  = g_aSseBinaryU128R64[iFn].cTests;
        PFNIEMAIMPLFPSSEF2U128R64                pfn     = g_aSseBinaryU128R64[iFn].pfn;
        uint32_t const                           cVars   = COUNT_VARIATIONS(g_aSseBinaryU128R64[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                X86XMMREG Res; RT_ZERO(Res);

                uint32_t uMxCsrIn = paTests[iTest].fMxcsrIn;
                uint32_t uMxCsrOut = pfn(uMxCsrIn, &Res, &paTests[iTest].InVal1, &paTests[iTest].r64Val2);
                if (   uMxCsrOut != paTests[iTest].fMxcsrOut
                    || !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[0], &paTests[iTest].OutVal.ar64[0])
                    || !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[1], &paTests[iTest].OutVal.ar64[1]))
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s'%s in2=%s\n"
                                          "%s               -> mxcsr=%#08x    %s'%s\n"
                                          "%s               expected %#08x    %s'%s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR64(&paTests[iTest].InVal1.ar64[0]), FormatR64(&paTests[iTest].InVal1.ar64[1]),
                                 FormatR64(&paTests[iTest].r64Val2),
                                 iVar ? "  " : "", uMxCsrOut,
                                 FormatR64(&Res.ar64[0]), FormatR64(&Res.ar64[1]),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 FormatR64(&paTests[iTest].OutVal.ar64[0]), FormatR64(&paTests[iTest].OutVal.ar64[1]),
                                 MxcsrDiff(uMxCsrOut, paTests[iTest].fMxcsrOut),
                                   (   !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[0], &paTests[iTest].OutVal.ar64[0])
                                    || !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[1], &paTests[iTest].OutVal.ar64[1]))
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryU128R64[iFn]);
    }
}


/*
 * SSE operations converting single double-precision floating point values to signed double-word integers (cvttsd2si and friends).
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_I32_R64_T, SSE_BINARY_I32_R64_TEST_T, PFNIEMAIMPLSSEF2I32U64);

static SSE_BINARY_I32_R64_T g_aSseBinaryI32R64[] =
{
    ENTRY_BIN(cvttsd2si_i32_r64),
    ENTRY_BIN(cvtsd2si_i32_r64),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryI32R64, g_aSseBinaryI32R64)
static RTEXITCODE SseBinaryI32R64Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT64U Val; } const s_aSpecials[] =
    {
        { RTFLOAT64U_INIT_C(0, 8388607, RTFLOAT64U_EXP_MAX - 1) },
          /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryI32R64); iFn++)
    {
        PFNIEMAIMPLSSEF2I32U64 const pfn = g_aSseBinaryI32R64[iFn].pfnNative ? g_aSseBinaryI32R64[iFn].pfnNative : g_aSseBinaryI32R64[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryI32R64[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_I32_R64_TEST_T TestData; RT_ZERO(TestData);

            TestData.r64ValIn = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].Val;

            if (RTFLOAT64U_IS_NORMAL(&TestData.r64ValIn))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM; int32_t i32OutM;
                        fMxcsrM = pfn(uMxCsrIn, &i32OutM, &TestData.r64ValIn.u);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrM;
                        TestData.i32ValOut = i32OutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU; int32_t i32OutU;
                        fMxcsrU = pfn(uMxCsrIn, &i32OutU, &TestData.r64ValIn.u);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrU;
                        TestData.i32ValOut = i32OutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1; int32_t i32Out1;
                            fMxcsr1 = pfn(uMxCsrIn, &i32Out1, &TestData.r64ValIn.u);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = fMxcsr1;
                            TestData.i32ValOut = i32Out1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2; int32_t i32Out2;
                                fMxcsr2 = pfn(uMxCsrIn, &i32Out2, &TestData.r64ValIn.u);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = fMxcsr2;
                                TestData.i32ValOut = i32Out2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3; int32_t i32Out3;
                                        fMxcsr3 = pfn(uMxCsrIn, &i32Out3, &TestData.r64ValIn.u);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = fMxcsr3;
                                        TestData.i32ValOut = i32Out3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif


static void SseBinaryI32R64Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryI32R64); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryI32R64[iFn]))
            continue;

        SSE_BINARY_I32_R64_TEST_T const * const  paTests = g_aSseBinaryI32R64[iFn].paTests;
        uint32_t const                           cTests  = g_aSseBinaryI32R64[iFn].cTests;
        PFNIEMAIMPLSSEF2I32U64                   pfn     = g_aSseBinaryI32R64[iFn].pfn;
        uint32_t const                           cVars   = COUNT_VARIATIONS(g_aSseBinaryI32R64[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                int32_t i32Dst = 0;

                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &i32Dst, &paTests[iTest].r64ValIn.u);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || i32Dst != paTests[iTest].i32ValOut)
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s\n"
                                          "%s               -> mxcsr=%#08x    %RI32\n"
                                          "%s               expected %#08x    %RI32%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR64(&paTests[iTest].r64ValIn),
                                 iVar ? "  " : "", fMxcsr, i32Dst,
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut, paTests[iTest].i32ValOut,
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   i32Dst != paTests[iTest].i32ValOut
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryI32R64[iFn]);
    }
}


/*
 * SSE operations converting single double-precision floating point values to signed quad-word integers (cvttsd2si and friends).
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_I64_R64_T, SSE_BINARY_I64_R64_TEST_T, PFNIEMAIMPLSSEF2I64U64);

static SSE_BINARY_I64_R64_T g_aSseBinaryI64R64[] =
{
    ENTRY_BIN(cvttsd2si_i64_r64),
    ENTRY_BIN(cvtsd2si_i64_r64),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryI64R64, g_aSseBinaryI64R64)
static RTEXITCODE SseBinaryI64R64Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT64U Val; } const s_aSpecials[] =
    {
        { RTFLOAT64U_INIT_C(0, 8388607, RTFLOAT64U_EXP_MAX - 1) },
          /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryI64R64); iFn++)
    {
        PFNIEMAIMPLSSEF2I64U64 const pfn = g_aSseBinaryI64R64[iFn].pfnNative ? g_aSseBinaryI64R64[iFn].pfnNative : g_aSseBinaryI64R64[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryI64R64[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_I64_R64_TEST_T TestData; RT_ZERO(TestData);

            TestData.r64ValIn = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].Val;

            if (RTFLOAT64U_IS_NORMAL(&TestData.r64ValIn))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM; int64_t i64OutM;
                        fMxcsrM = pfn(uMxCsrIn, &i64OutM, &TestData.r64ValIn.u);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrM;
                        TestData.i64ValOut = i64OutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU; int64_t i64OutU;
                        fMxcsrU =pfn(uMxCsrIn, &i64OutU, &TestData.r64ValIn.u);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrU;
                        TestData.i64ValOut = i64OutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1; int64_t i64Out1;
                            fMxcsr1 = pfn(uMxCsrIn, &i64Out1, &TestData.r64ValIn.u);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = fMxcsr1;
                            TestData.i64ValOut = i64Out1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2; int64_t i64Out2;
                                fMxcsr2 = pfn(uMxCsrIn, &i64Out2, &TestData.r64ValIn.u);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = fMxcsr2;
                                TestData.i64ValOut = i64Out2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3; int64_t i64Out3;
                                        fMxcsr3 = pfn(uMxCsrIn, &i64Out3, &TestData.r64ValIn.u);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = fMxcsr3;
                                        TestData.i64ValOut = i64Out3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif


static void SseBinaryI64R64Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryI64R64); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryI64R64[iFn]))
            continue;

        SSE_BINARY_I64_R64_TEST_T const * const  paTests = g_aSseBinaryI64R64[iFn].paTests;
        uint32_t const                           cTests  = g_aSseBinaryI64R64[iFn].cTests;
        PFNIEMAIMPLSSEF2I64U64                   pfn     = g_aSseBinaryI64R64[iFn].pfn;
        uint32_t const                           cVars   = COUNT_VARIATIONS(g_aSseBinaryI32R64[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                int64_t i64Dst = 0;
                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &i64Dst, &paTests[iTest].r64ValIn.u);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || i64Dst != paTests[iTest].i64ValOut)
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s\n"
                                          "%s               -> mxcsr=%#08x    %RI64\n"
                                          "%s               expected %#08x    %RI64%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR64(&paTests[iTest].r64ValIn),
                                 iVar ? "  " : "", fMxcsr, i64Dst,
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut, paTests[iTest].i64ValOut,
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   i64Dst != paTests[iTest].i64ValOut
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryI64R64[iFn]);
    }
}


/*
 * SSE operations converting single single-precision floating point values to signed double-word integers (cvttss2si and friends).
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_I32_R32_T, SSE_BINARY_I32_R32_TEST_T, PFNIEMAIMPLSSEF2I32U32);

static SSE_BINARY_I32_R32_T g_aSseBinaryI32R32[] =
{
    ENTRY_BIN(cvttss2si_i32_r32),
    ENTRY_BIN(cvtss2si_i32_r32),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryI32R32, g_aSseBinaryI32R32)
static RTEXITCODE SseBinaryI32R32Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT32U Val; } const s_aSpecials[] =
    {
        { RTFLOAT32U_INIT_C(0, 8388607, RTFLOAT32U_EXP_MAX - 1) },
          /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryI32R32); iFn++)
    {
        PFNIEMAIMPLSSEF2I32U32 const pfn = g_aSseBinaryI32R32[iFn].pfnNative ? g_aSseBinaryI32R32[iFn].pfnNative : g_aSseBinaryI32R32[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryI32R32[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_I32_R32_TEST_T TestData; RT_ZERO(TestData);

            TestData.r32ValIn = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val;

            if (RTFLOAT32U_IS_NORMAL(&TestData.r32ValIn))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM; int32_t i32OutM;
                        fMxcsrM = pfn(uMxCsrIn, &i32OutM, &TestData.r32ValIn.u);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrM;
                        TestData.i32ValOut = i32OutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU; int32_t i32OutU;
                        fMxcsrU = pfn(uMxCsrIn, &i32OutU, &TestData.r32ValIn.u);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrU;
                        TestData.i32ValOut = i32OutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1; int32_t i32Out1;
                            fMxcsr1 = pfn(uMxCsrIn, &i32Out1, &TestData.r32ValIn.u);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = fMxcsr1;
                            TestData.i32ValOut = i32Out1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2; int32_t i32Out2;
                                fMxcsr2 = pfn(uMxCsrIn, &i32Out2, &TestData.r32ValIn.u);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = fMxcsr2;
                                TestData.i32ValOut = i32Out2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3; int32_t i32Out3;
                                        fMxcsr3 = pfn(uMxCsrIn, &i32Out3, &TestData.r32ValIn.u);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = fMxcsr3;
                                        TestData.i32ValOut = i32Out3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif


static void SseBinaryI32R32Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryI32R32); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryI32R32[iFn]))
            continue;

        SSE_BINARY_I32_R32_TEST_T const * const  paTests = g_aSseBinaryI32R32[iFn].paTests;
        uint32_t const                           cTests  = g_aSseBinaryI32R32[iFn].cTests;
        PFNIEMAIMPLSSEF2I32U32                   pfn     = g_aSseBinaryI32R32[iFn].pfn;
        uint32_t const                           cVars   = COUNT_VARIATIONS(g_aSseBinaryI32R32[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                int32_t i32Dst = 0;

                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &i32Dst, &paTests[iTest].r32ValIn.u);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || i32Dst != paTests[iTest].i32ValOut)
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s\n"
                                          "%s               -> mxcsr=%#08x    %RI32\n"
                                          "%s               expected %#08x    %RI32%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR32(&paTests[iTest].r32ValIn),
                                 iVar ? "  " : "", fMxcsr, i32Dst,
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut, paTests[iTest].i32ValOut,
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   i32Dst != paTests[iTest].i32ValOut
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryI32R32[iFn]);
    }
}


/*
 * SSE operations converting single single-precision floating point values to signed quad-word integers (cvttss2si and friends).
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_I64_R32_T, SSE_BINARY_I64_R32_TEST_T, PFNIEMAIMPLSSEF2I64U32);

static SSE_BINARY_I64_R32_T g_aSseBinaryI64R32[] =
{
    ENTRY_BIN(cvttss2si_i64_r32),
    ENTRY_BIN(cvtss2si_i64_r32),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryI64R32, g_aSseBinaryI64R32)
static RTEXITCODE SseBinaryI64R32Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT32U Val; } const s_aSpecials[] =
    {
        { RTFLOAT32U_INIT_C(0, 8388607, RTFLOAT32U_EXP_MAX - 1) },
          /** @todo More specials. */
    };

    X86FXSTATE State;
    RT_ZERO(State);
    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryI64R32); iFn++)
    {
        PFNIEMAIMPLSSEF2I64U32 const pfn = g_aSseBinaryI64R32[iFn].pfnNative ? g_aSseBinaryI64R32[iFn].pfnNative : g_aSseBinaryI64R32[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryI64R32[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_I64_R32_TEST_T TestData; RT_ZERO(TestData);

            TestData.r32ValIn = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val;

            if (RTFLOAT32U_IS_NORMAL(&TestData.r32ValIn))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM; int64_t i64OutM;
                        fMxcsrM = pfn(uMxCsrIn, &i64OutM, &TestData.r32ValIn.u);
                        TestData.fMxcsrIn  = State.MXCSR;
                        TestData.fMxcsrOut = fMxcsrM;
                        TestData.i64ValOut = i64OutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU; int64_t i64OutU;
                        fMxcsrU = pfn(uMxCsrIn, &i64OutU, &TestData.r32ValIn.u);
                        TestData.fMxcsrIn  = State.MXCSR;
                        TestData.fMxcsrOut = fMxcsrU;
                        TestData.i64ValOut = i64OutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1; int64_t i64Out1;
                            fMxcsr1 = pfn(uMxCsrIn, &i64Out1, &TestData.r32ValIn.u);
                            TestData.fMxcsrIn  = State.MXCSR;
                            TestData.fMxcsrOut = fMxcsr1;
                            TestData.i64ValOut = i64Out1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2; int64_t i64Out2;
                                fMxcsr2 = pfn(uMxCsrIn, &i64Out2, &TestData.r32ValIn.u);
                                TestData.fMxcsrIn  = State.MXCSR;
                                TestData.fMxcsrOut = fMxcsr2;
                                TestData.i64ValOut = i64Out2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3; int64_t i64Out3;
                                        fMxcsr3 = pfn(uMxCsrIn, &i64Out3, &TestData.r32ValIn.u);
                                        TestData.fMxcsrIn  = State.MXCSR;
                                        TestData.fMxcsrOut = fMxcsr3;
                                        TestData.i64ValOut = i64Out3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif


static void SseBinaryI64R32Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryI64R32); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryI64R32[iFn]))
            continue;

        SSE_BINARY_I64_R32_TEST_T const * const  paTests = g_aSseBinaryI64R32[iFn].paTests;
        uint32_t const                           cTests  = g_aSseBinaryI64R32[iFn].cTests;
        PFNIEMAIMPLSSEF2I64U32                   pfn     = g_aSseBinaryI64R32[iFn].pfn;
        uint32_t const                           cVars   = COUNT_VARIATIONS(g_aSseBinaryI64R32[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                int64_t i64Dst = 0;

                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &i64Dst, &paTests[iTest].r32ValIn.u);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || i64Dst != paTests[iTest].i64ValOut)
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s\n"
                                          "%s               -> mxcsr=%#08x    %RI64\n"
                                          "%s               expected %#08x    %RI64%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR32(&paTests[iTest].r32ValIn),
                                 iVar ? "  " : "", fMxcsr, i64Dst,
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut, paTests[iTest].i64ValOut,
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   i64Dst != paTests[iTest].i64ValOut
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryI64R32[iFn]);
    }
}


/*
 * SSE operations converting single signed double-word integers to double-precision floating point values (probably only cvtsi2sd).
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_R64_I32_T, SSE_BINARY_R64_I32_TEST_T, PFNIEMAIMPLSSEF2R64I32);

static SSE_BINARY_R64_I32_T g_aSseBinaryR64I32[] =
{
    ENTRY_BIN(cvtsi2sd_r64_i32)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryR64I32, g_aSseBinaryR64I32)
static RTEXITCODE SseBinaryR64I32Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static int32_t const s_aSpecials[] =
    {
        INT32_MIN,
        INT32_MAX,
        /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR64I32); iFn++)
    {
        PFNIEMAIMPLSSEF2R64I32 const pfn = g_aSseBinaryR64I32[iFn].pfnNative ? g_aSseBinaryR64I32[iFn].pfnNative : g_aSseBinaryR64I32[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryR64I32[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_R64_I32_TEST_T TestData; RT_ZERO(TestData);

            TestData.i32ValIn = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests];

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM; RTFLOAT64U r64OutM;
                        fMxcsrM = pfn(uMxCsrIn, &r64OutM, &TestData.i32ValIn);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrM;
                        TestData.r64ValOut = r64OutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU; RTFLOAT64U r64OutU;
                        fMxcsrU = pfn(uMxCsrIn, &r64OutU, &TestData.i32ValIn);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrU;
                        TestData.r64ValOut = r64OutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1; RTFLOAT64U r64Out1;
                            fMxcsr1 = pfn(uMxCsrIn, &r64Out1, &TestData.i32ValIn);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = fMxcsr1;
                            TestData.r64ValOut = r64Out1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2; RTFLOAT64U r64Out2;
                                fMxcsr2 = pfn(uMxCsrIn, &r64Out2, &TestData.i32ValIn);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = fMxcsr2;
                                TestData.r64ValOut = r64Out2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3; RTFLOAT64U r64Out3;
                                        fMxcsr3 = pfn(uMxCsrIn, &r64Out3, &TestData.i32ValIn);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = fMxcsr3;
                                        TestData.r64ValOut = r64Out3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif


static void SseBinaryR64I32Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR64I32); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryR64I32[iFn]))
            continue;

        SSE_BINARY_R64_I32_TEST_T const * const  paTests = g_aSseBinaryR64I32[iFn].paTests;
        uint32_t const                           cTests  = g_aSseBinaryR64I32[iFn].cTests;
        PFNIEMAIMPLSSEF2R64I32                   pfn     = g_aSseBinaryR64I32[iFn].pfn;
        uint32_t const                           cVars   = COUNT_VARIATIONS(g_aSseBinaryR64I32[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTFLOAT64U r64Dst; RT_ZERO(r64Dst);

                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &r64Dst, &paTests[iTest].i32ValIn);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || !RTFLOAT64U_ARE_IDENTICAL(&r64Dst, &paTests[iTest].r64ValOut))
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%RI32\n"
                                          "%s               -> mxcsr=%#08x    %s\n"
                                          "%s               expected %#08x    %s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 &paTests[iTest].i32ValIn,
                                 iVar ? "  " : "", fMxcsr, FormatR64(&r64Dst),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut, FormatR64(&paTests[iTest].r64ValOut),
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   !RTFLOAT64U_ARE_IDENTICAL(&r64Dst, &paTests[iTest].r64ValOut)
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryR64I32[iFn]);
    }
}


/*
 * SSE operations converting single signed quad-word integers to double-precision floating point values (probably only cvtsi2sd).
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_R64_I64_T, SSE_BINARY_R64_I64_TEST_T, PFNIEMAIMPLSSEF2R64I64);

static SSE_BINARY_R64_I64_T g_aSseBinaryR64I64[] =
{
    ENTRY_BIN(cvtsi2sd_r64_i64),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryR64I64, g_aSseBinaryR64I64)
static RTEXITCODE SseBinaryR64I64Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static int64_t const s_aSpecials[] =
    {
        INT64_MIN,
        INT64_MAX
        /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR64I64); iFn++)
    {
        PFNIEMAIMPLSSEF2R64I64 const pfn = g_aSseBinaryR64I64[iFn].pfnNative ? g_aSseBinaryR64I64[iFn].pfnNative : g_aSseBinaryR64I64[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryR64I64[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_R64_I64_TEST_T TestData; RT_ZERO(TestData);

            TestData.i64ValIn = iTest < cTests ? RandI64Src(iTest) : s_aSpecials[iTest - cTests];

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM; RTFLOAT64U r64OutM;
                        fMxcsrM = pfn(uMxCsrIn, &r64OutM, &TestData.i64ValIn);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrM;
                        TestData.r64ValOut = r64OutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU; RTFLOAT64U r64OutU;
                        fMxcsrU = pfn(uMxCsrIn, &r64OutU, &TestData.i64ValIn);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrU;
                        TestData.r64ValOut = r64OutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1; RTFLOAT64U r64Out1;
                            fMxcsr1 = pfn(uMxCsrIn, &r64Out1, &TestData.i64ValIn);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = fMxcsr1;
                            TestData.r64ValOut = r64Out1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2; RTFLOAT64U r64Out2;
                                fMxcsr2 = pfn(uMxCsrIn, &r64Out2, &TestData.i64ValIn);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = fMxcsr2;
                                TestData.r64ValOut = r64Out2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3; RTFLOAT64U r64Out3;
                                        fMxcsr3 = pfn(uMxCsrIn, &r64Out3, &TestData.i64ValIn);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = fMxcsr3;
                                        TestData.r64ValOut = r64Out3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif


static void SseBinaryR64I64Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR64I64); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryR64I64[iFn]))
            continue;

        SSE_BINARY_R64_I64_TEST_T const * const  paTests = g_aSseBinaryR64I64[iFn].paTests;
        uint32_t const                           cTests  = g_aSseBinaryR64I64[iFn].cTests;
        PFNIEMAIMPLSSEF2R64I64                   pfn     = g_aSseBinaryR64I64[iFn].pfn;
        uint32_t const                           cVars   = COUNT_VARIATIONS(g_aSseBinaryR64I64[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTFLOAT64U r64Dst; RT_ZERO(r64Dst);

                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &r64Dst, &paTests[iTest].i64ValIn);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || !RTFLOAT64U_ARE_IDENTICAL(&r64Dst, &paTests[iTest].r64ValOut))
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%RI64\n"
                                          "%s               -> mxcsr=%#08x    %s\n"
                                          "%s               expected %#08x    %s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 &paTests[iTest].i64ValIn,
                                 iVar ? "  " : "", fMxcsr, FormatR64(&r64Dst),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut, FormatR64(&paTests[iTest].r64ValOut),
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   !RTFLOAT64U_ARE_IDENTICAL(&r64Dst, &paTests[iTest].r64ValOut)
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryR64I64[iFn]);
    }
}


/*
 * SSE operations converting single signed double-word integers to single-precision floating point values (probably only cvtsi2ss).
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_R32_I32_T, SSE_BINARY_R32_I32_TEST_T, PFNIEMAIMPLSSEF2R32I32);

static SSE_BINARY_R32_I32_T g_aSseBinaryR32I32[] =
{
    ENTRY_BIN(cvtsi2ss_r32_i32),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryR32I32, g_aSseBinaryR32I32)
static RTEXITCODE SseBinaryR32I32Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static int32_t const s_aSpecials[] =
    {
        INT32_MIN,
        INT32_MAX,
        /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR32I32); iFn++)
    {
        PFNIEMAIMPLSSEF2R32I32 const pfn = g_aSseBinaryR32I32[iFn].pfnNative ? g_aSseBinaryR32I32[iFn].pfnNative : g_aSseBinaryR32I32[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryR32I32[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_R32_I32_TEST_T TestData; RT_ZERO(TestData);

            TestData.i32ValIn = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests];

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM; RTFLOAT32U r32OutM;
                        fMxcsrM = pfn(uMxCsrIn, &r32OutM, &TestData.i32ValIn);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrM;
                        TestData.r32ValOut = r32OutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU; RTFLOAT32U r32OutU;
                        fMxcsrU = pfn(uMxCsrIn, &r32OutU, &TestData.i32ValIn);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrU;
                        TestData.r32ValOut = r32OutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1; RTFLOAT32U r32Out1;
                            fMxcsr1 = pfn(uMxCsrIn, &r32Out1, &TestData.i32ValIn);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = fMxcsr1;
                            TestData.r32ValOut = r32Out1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2; RTFLOAT32U r32Out2;
                                fMxcsr2 = pfn(uMxCsrIn, &r32Out2, &TestData.i32ValIn);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = fMxcsr2;
                                TestData.r32ValOut = r32Out2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3; RTFLOAT32U r32Out3;
                                        fMxcsr3 = pfn(uMxCsrIn, &r32Out3, &TestData.i32ValIn);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = fMxcsr3;
                                        TestData.r32ValOut = r32Out3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif


static void SseBinaryR32I32Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR32I32); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryR32I32[iFn]))
            continue;

        SSE_BINARY_R32_I32_TEST_T const * const  paTests = g_aSseBinaryR32I32[iFn].paTests;
        uint32_t const                           cTests  = g_aSseBinaryR32I32[iFn].cTests;
        PFNIEMAIMPLSSEF2R32I32                   pfn     = g_aSseBinaryR32I32[iFn].pfn;
        uint32_t const                           cVars   = COUNT_VARIATIONS(g_aSseBinaryR32I32[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTFLOAT32U r32Dst; RT_ZERO(r32Dst);

                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &r32Dst, &paTests[iTest].i32ValIn);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || !RTFLOAT32U_ARE_IDENTICAL(&r32Dst, &paTests[iTest].r32ValOut))
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%RI32\n"
                                          "%s               -> mxcsr=%#08x    %RI32\n"
                                          "%s               expected %#08x    %RI32%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 &paTests[iTest].i32ValIn,
                                 iVar ? "  " : "", fMxcsr, FormatR32(&r32Dst),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut, FormatR32(&paTests[iTest].r32ValOut),
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   !RTFLOAT32U_ARE_IDENTICAL(&r32Dst, &paTests[iTest].r32ValOut)
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryR32I32[iFn]);
    }
}


/*
 * SSE operations converting single signed quad-word integers to single-precision floating point values (probably only cvtsi2ss).
 */
TYPEDEF_SUBTEST_TYPE(SSE_BINARY_R32_I64_T, SSE_BINARY_R32_I64_TEST_T, PFNIEMAIMPLSSEF2R32I64);

static SSE_BINARY_R32_I64_T g_aSseBinaryR32I64[] =
{
    ENTRY_BIN(cvtsi2ss_r32_i64),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseBinaryR32I64, g_aSseBinaryR32I64)
static RTEXITCODE SseBinaryR32I64Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static int64_t const s_aSpecials[] =
    {
        INT64_MIN,
        INT64_MAX
        /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR32I64); iFn++)
    {
        PFNIEMAIMPLSSEF2R32I64 const pfn = g_aSseBinaryR32I64[iFn].pfnNative ? g_aSseBinaryR32I64[iFn].pfnNative : g_aSseBinaryR32I64[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseBinaryR32I64[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_BINARY_R32_I64_TEST_T TestData; RT_ZERO(TestData);

            TestData.i64ValIn = iTest < cTests ? RandI64Src(iTest) : s_aSpecials[iTest - cTests];

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM; RTFLOAT32U r32OutM;
                        fMxcsrM = pfn(uMxCsrIn, &r32OutM, &TestData.i64ValIn);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrM;
                        TestData.r32ValOut = r32OutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU; RTFLOAT32U r32OutU;
                        fMxcsrU = pfn(uMxCsrIn, &r32OutU, &TestData.i64ValIn);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = fMxcsrU;
                        TestData.r32ValOut = r32OutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1; RTFLOAT32U r32Out1;
                            fMxcsr1 = pfn(uMxCsrIn, &r32Out1, &TestData.i64ValIn);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = fMxcsr1;
                            TestData.r32ValOut = r32Out1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2; RTFLOAT32U r32Out2;
                                fMxcsr2 = pfn(uMxCsrIn, &r32Out2, &TestData.i64ValIn);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = fMxcsr2;
                                TestData.r32ValOut = r32Out2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3; RTFLOAT32U r32Out3;
                                        fMxcsr3 = pfn(uMxCsrIn, &r32Out3, &TestData.i64ValIn);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = fMxcsr3;
                                        TestData.r32ValOut = r32Out3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif


static void SseBinaryR32I64Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseBinaryR32I64); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseBinaryR32I64[iFn]))
            continue;

        SSE_BINARY_R32_I64_TEST_T const * const  paTests = g_aSseBinaryR32I64[iFn].paTests;
        uint32_t const                           cTests  = g_aSseBinaryR32I64[iFn].cTests;
        PFNIEMAIMPLSSEF2R32I64                   pfn     = g_aSseBinaryR32I64[iFn].pfn;
        uint32_t const                           cVars   = COUNT_VARIATIONS(g_aSseBinaryR32I64[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTFLOAT32U r32Dst; RT_ZERO(r32Dst);

                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &r32Dst, &paTests[iTest].i64ValIn);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || !RTFLOAT32U_ARE_IDENTICAL(&r32Dst, &paTests[iTest].r32ValOut))
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%RI64\n"
                                          "%s               -> mxcsr=%#08x    %RI32\n"
                                          "%s               expected %#08x    %RI32%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 &paTests[iTest].i64ValIn,
                                 iVar ? "  " : "", fMxcsr, FormatR32(&r32Dst),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut, FormatR32(&paTests[iTest].r32ValOut),
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   !RTFLOAT32U_ARE_IDENTICAL(&r32Dst, &paTests[iTest].r32ValOut)
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn) );
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseBinaryR32I64[iFn]);
    }
}


/*
 * Compare SSE operations on single single-precision floating point values - outputting only EFLAGS.
 */
TYPEDEF_SUBTEST_TYPE(SSE_COMPARE_EFL_R32_R32_T, SSE_COMPARE_EFL_R32_R32_TEST_T, PFNIEMAIMPLF2EFLMXCSRR32R32);

static SSE_COMPARE_EFL_R32_R32_T g_aSseCompareEflR32R32[] =
{
    ENTRY_BIN(ucomiss_u128),
    ENTRY_BIN(comiss_u128),
    ENTRY_BIN_AVX(vucomiss_u128),
    ENTRY_BIN_AVX(vcomiss_u128),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseCompareEflR32R32, g_aSseCompareEflR32R32)
static RTEXITCODE SseCompareEflR32R32Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT32U Val1, Val2; } const s_aSpecials[] =
    {
        { RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0) },
        { RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(1) },
        { RTFLOAT32U_INIT_ZERO(1), RTFLOAT32U_INIT_ZERO(0) },
        { RTFLOAT32U_INIT_ZERO(1), RTFLOAT32U_INIT_ZERO(1) },
        { RTFLOAT32U_INIT_INF(0),  RTFLOAT32U_INIT_INF(0)  },
        { RTFLOAT32U_INIT_INF(0),  RTFLOAT32U_INIT_INF(1)  },
        { RTFLOAT32U_INIT_INF(1),  RTFLOAT32U_INIT_INF(0)  },
        { RTFLOAT32U_INIT_INF(1),  RTFLOAT32U_INIT_INF(1)  },
        /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseCompareEflR32R32); iFn++)
    {
        PFNIEMAIMPLF2EFLMXCSRR32R32 const pfn = g_aSseCompareEflR32R32[iFn].pfnNative ? g_aSseCompareEflR32R32[iFn].pfnNative : g_aSseCompareEflR32R32[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseCompareEflR32R32[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_COMPARE_EFL_R32_R32_TEST_T TestData; RT_ZERO(TestData);

            TestData.r32ValIn1 = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val1;
            TestData.r32ValIn2 = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val2;

            if (   RTFLOAT32U_IS_NORMAL(&TestData.r32ValIn1)
                && RTFLOAT32U_IS_NORMAL(&TestData.r32ValIn2))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            uint32_t const fEFlags = RandEFlags();
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t fMxcsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM  = fMxcsrIn;
                        uint32_t fEFlagsM = fEFlags;
                        fMxcsrM = pfn(fMxcsrIn, &fEFlagsM, TestData.r32ValIn1, TestData.r32ValIn2);
                        TestData.fMxcsrIn   = fMxcsrIn;
                        TestData.fMxcsrOut  = fMxcsrM;
                        TestData.fEflIn     = fEFlags;
                        TestData.fEflOut    = fEFlagsM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        fMxcsrIn &= ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU  = fMxcsrIn;
                        uint32_t fEFlagsU = fEFlags;
                        fMxcsrU = pfn(fMxcsrIn, &fEFlagsU, TestData.r32ValIn1, TestData.r32ValIn2);
                        TestData.fMxcsrIn   = fMxcsrIn;
                        TestData.fMxcsrOut  = fMxcsrU;
                        TestData.fEflIn     = fEFlags;
                        TestData.fEflOut    = fEFlagsU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1  = fMxcsrIn;
                            uint32_t fEFlags1 = fEFlags;
                            fMxcsr1 = pfn(fMxcsrIn, &fEFlags1, TestData.r32ValIn1, TestData.r32ValIn2);
                            TestData.fMxcsrIn   = fMxcsrIn;
                            TestData.fMxcsrOut  = fMxcsr1;
                            TestData.fEflIn     = fEFlags;
                            TestData.fEflOut    = fEFlags1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2  = fMxcsrIn;
                                uint32_t fEFlags2 = fEFlags;
                                fMxcsr2 = pfn(fMxcsrIn, &fEFlags2, TestData.r32ValIn1, TestData.r32ValIn2);
                                TestData.fMxcsrIn   = fMxcsrIn;
                                TestData.fMxcsrOut  = fMxcsr2;
                                TestData.fEflIn     = fEFlags;
                                TestData.fEflOut    = fEFlags2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3  = fMxcsrIn;
                                        uint32_t fEFlags3 = fEFlags;
                                        fMxcsr3 = pfn(fMxcsrIn, &fEFlags3, TestData.r32ValIn1, TestData.r32ValIn2);
                                        TestData.fMxcsrIn   = fMxcsrIn;
                                        TestData.fMxcsrOut  = fMxcsr3;
                                        TestData.fEflIn     = fEFlags;
                                        TestData.fEflOut    = fEFlags3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseCompareEflR32R32Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseCompareEflR32R32); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseCompareEflR32R32[iFn]))
            continue;

        SSE_COMPARE_EFL_R32_R32_TEST_T const * const    paTests = g_aSseCompareEflR32R32[iFn].paTests;
        uint32_t const                                  cTests  = g_aSseCompareEflR32R32[iFn].cTests;
        PFNIEMAIMPLF2EFLMXCSRR32R32                     pfn     = g_aSseCompareEflR32R32[iFn].pfn;
        uint32_t const                                  cVars   = COUNT_VARIATIONS(g_aSseCompareEflR32R32[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                uint32_t fEFlags = paTests[iTest].fEflIn;
                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &fEFlags, paTests[iTest].r32ValIn1, paTests[iTest].r32ValIn2);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || fEFlags != paTests[iTest].fEflOut)
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x efl=%#08x in1=%s in2=%s\n"
                                          "%s               -> mxcsr=%#08x    %#08x\n"
                                          "%s               expected %#08x    %#08x%s (%s) (EFL: %s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn, paTests[iTest].fEflIn,
                                 FormatR32(&paTests[iTest].r32ValIn1), FormatR32(&paTests[iTest].r32ValIn2),
                                 iVar ? "  " : "", fMxcsr, fEFlags,
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut, paTests[iTest].fEflOut,
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                 FormatMxcsr(paTests[iTest].fMxcsrIn),
                                 EFlagsDiff(fEFlags, paTests[iTest].fEflOut));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseCompareEflR32R32[iFn]);
    }
}


/*
 * Compare SSE operations on single single-precision floating point values - outputting only EFLAGS.
 */
TYPEDEF_SUBTEST_TYPE(SSE_COMPARE_EFL_R64_R64_T, SSE_COMPARE_EFL_R64_R64_TEST_T, PFNIEMAIMPLF2EFLMXCSRR64R64);

static SSE_COMPARE_EFL_R64_R64_T g_aSseCompareEflR64R64[] =
{
    ENTRY_BIN(ucomisd_u128),
    ENTRY_BIN(comisd_u128),
    ENTRY_BIN_AVX(vucomisd_u128),
    ENTRY_BIN_AVX(vcomisd_u128)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseCompareEflR64R64, g_aSseCompareEflR64R64)
static RTEXITCODE SseCompareEflR64R64Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT64U Val1, Val2; } const s_aSpecials[] =
    {
        { RTFLOAT64U_INIT_ZERO(0), RTFLOAT64U_INIT_ZERO(0) },
        { RTFLOAT64U_INIT_ZERO(0), RTFLOAT64U_INIT_ZERO(1) },
        { RTFLOAT64U_INIT_ZERO(1), RTFLOAT64U_INIT_ZERO(0) },
        { RTFLOAT64U_INIT_ZERO(1), RTFLOAT64U_INIT_ZERO(1) },
        { RTFLOAT64U_INIT_INF(0),  RTFLOAT64U_INIT_INF(0)  },
        { RTFLOAT64U_INIT_INF(0),  RTFLOAT64U_INIT_INF(1)  },
        { RTFLOAT64U_INIT_INF(1),  RTFLOAT64U_INIT_INF(0)  },
        { RTFLOAT64U_INIT_INF(1),  RTFLOAT64U_INIT_INF(1)  },
        /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseCompareEflR64R64); iFn++)
    {
        PFNIEMAIMPLF2EFLMXCSRR64R64 const pfn = g_aSseCompareEflR64R64[iFn].pfnNative ? g_aSseCompareEflR64R64[iFn].pfnNative : g_aSseCompareEflR64R64[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseCompareEflR64R64[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_COMPARE_EFL_R64_R64_TEST_T TestData; RT_ZERO(TestData);

            TestData.r64ValIn1 = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].Val1;
            TestData.r64ValIn2 = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].Val2;

            if (   RTFLOAT64U_IS_NORMAL(&TestData.r64ValIn1)
                && RTFLOAT64U_IS_NORMAL(&TestData.r64ValIn2))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            uint32_t const fEFlags = RandEFlags();
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t fMxcsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM  = fMxcsrIn;
                        uint32_t fEFlagsM = fEFlags;
                        fMxcsrM = pfn(fMxcsrIn, &fEFlagsM, TestData.r64ValIn1, TestData.r64ValIn2);
                        TestData.fMxcsrIn   = fMxcsrIn;
                        TestData.fMxcsrOut  = fMxcsrM;
                        TestData.fEflIn     = fEFlags;
                        TestData.fEflOut    = fEFlagsM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        fMxcsrIn &= ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU  = fMxcsrIn;
                        uint32_t fEFlagsU = fEFlags;
                        fMxcsrU = pfn(fMxcsrIn, &fEFlagsU, TestData.r64ValIn1, TestData.r64ValIn2);
                        TestData.fMxcsrIn   = fMxcsrIn;
                        TestData.fMxcsrOut  = fMxcsrU;
                        TestData.fEflIn     = fEFlags;
                        TestData.fEflOut    = fEFlagsU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1  = fMxcsrIn;
                            uint32_t fEFlags1 = fEFlags;
                            fMxcsr1 = pfn(fMxcsrIn, &fEFlags1, TestData.r64ValIn1, TestData.r64ValIn2);
                            TestData.fMxcsrIn   = fMxcsrIn;
                            TestData.fMxcsrOut  = fMxcsr1;
                            TestData.fEflIn     = fEFlags;
                            TestData.fEflOut    = fEFlags1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2  = fMxcsrIn;
                                uint32_t fEFlags2 = fEFlags;
                                fMxcsr2 = pfn(fMxcsrIn, &fEFlags2, TestData.r64ValIn1, TestData.r64ValIn2);
                                TestData.fMxcsrIn   = fMxcsrIn;
                                TestData.fMxcsrOut  = fMxcsr2;
                                TestData.fEflIn     = fEFlags;
                                TestData.fEflOut    = fEFlags2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3  = fMxcsrIn;
                                        uint32_t fEFlags3 = fEFlags;
                                        fMxcsr3 = pfn(fMxcsrIn, &fEFlags3, TestData.r64ValIn1, TestData.r64ValIn2);
                                        TestData.fMxcsrIn   = fMxcsrIn;
                                        TestData.fMxcsrOut  = fMxcsr3;
                                        TestData.fEflIn     = fEFlags;
                                        TestData.fEflOut    = fEFlags3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseCompareEflR64R64Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseCompareEflR64R64); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseCompareEflR64R64[iFn]))
            continue;

        SSE_COMPARE_EFL_R64_R64_TEST_T const * const    paTests = g_aSseCompareEflR64R64[iFn].paTests;
        uint32_t const                                  cTests  = g_aSseCompareEflR64R64[iFn].cTests;
        PFNIEMAIMPLF2EFLMXCSRR64R64                     pfn     = g_aSseCompareEflR64R64[iFn].pfn;
        uint32_t const                                  cVars   = COUNT_VARIATIONS(g_aSseCompareEflR64R64[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                uint32_t fEFlags = paTests[iTest].fEflIn;
                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &fEFlags, paTests[iTest].r64ValIn1, paTests[iTest].r64ValIn2);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || fEFlags != paTests[iTest].fEflOut)
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x efl=%#08x in1=%s in2=%s\n"
                                          "%s               -> mxcsr=%#08x    %#08x\n"
                                          "%s               expected %#08x    %#08x%s (%s) (EFL: %s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn, paTests[iTest].fEflIn,
                                 FormatR64(&paTests[iTest].r64ValIn1), FormatR64(&paTests[iTest].r64ValIn2),
                                 iVar ? "  " : "", fMxcsr, fEFlags,
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut, paTests[iTest].fEflOut,
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                 FormatMxcsr(paTests[iTest].fMxcsrIn),
                                 EFlagsDiff(fEFlags, paTests[iTest].fEflOut));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseCompareEflR64R64[iFn]);
    }
}


/*
 * Compare SSE operations on packed and single single-precision floating point values - outputting a mask.
 */
/** Maximum immediate to try to keep the testdata size under control (at least a little bit)- */
#define SSE_COMPARE_F2_XMM_IMM8_MAX 0x1f

TYPEDEF_SUBTEST_TYPE(SSE_COMPARE_F3_XMM_IMM8_T, SSE_COMPARE_F3_XMM_IMM8_TEST_T, PFNIEMAIMPLMEDIAF3XMMIMM8);

static SSE_COMPARE_F3_XMM_IMM8_T g_aSseCompareF3XmmR32Imm8[] =
{
    ENTRY_BIN(cmpps_u128),
    ENTRY_BIN(cmpss_u128)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseCompareF3XmmR32Imm8, g_aSseCompareF3XmmR32Imm8)
static RTEXITCODE SseCompareF3XmmR32Imm8Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT32U Val1, Val2; } const s_aSpecials[] =
    {
        { RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0) },
        { RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(1) },
        { RTFLOAT32U_INIT_ZERO(1), RTFLOAT32U_INIT_ZERO(0) },
        { RTFLOAT32U_INIT_ZERO(1), RTFLOAT32U_INIT_ZERO(1) },
        { RTFLOAT32U_INIT_INF(0),  RTFLOAT32U_INIT_INF(0)  },
        { RTFLOAT32U_INIT_INF(0),  RTFLOAT32U_INIT_INF(1)  },
        { RTFLOAT32U_INIT_INF(1),  RTFLOAT32U_INIT_INF(0)  },
        { RTFLOAT32U_INIT_INF(1),  RTFLOAT32U_INIT_INF(1)  },
        /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseCompareF3XmmR32Imm8); iFn++)
    {
        PFNIEMAIMPLMEDIAF3XMMIMM8 const pfn = g_aSseCompareF3XmmR32Imm8[iFn].pfnNative ? g_aSseCompareF3XmmR32Imm8[iFn].pfnNative : g_aSseCompareF3XmmR32Imm8[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseCompareF3XmmR32Imm8[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_COMPARE_F3_XMM_IMM8_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal1.ar32[0] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val1;
            TestData.InVal1.ar32[1] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val1;
            TestData.InVal1.ar32[2] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val1;
            TestData.InVal1.ar32[3] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val1;

            TestData.InVal2.ar32[0] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val2;
            TestData.InVal2.ar32[1] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val2;
            TestData.InVal2.ar32[2] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val2;
            TestData.InVal2.ar32[3] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].Val2;

            if (   RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[0])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[1])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[2])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal1.ar32[3])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal2.ar32[0])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal2.ar32[1])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal2.ar32[2])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal2.ar32[3]))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            IEMMEDIAF2XMMSRC Src;
            Src.uSrc1 = TestData.InVal1;
            Src.uSrc2 = TestData.InVal2;
            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint8_t bImm = 0; bImm <= SSE_COMPARE_F2_XMM_IMM8_MAX; bImm++)
                for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                    for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                        for (uint8_t iFz = 0; iFz < 2; iFz++)
                        {
                            uint32_t fMxcsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                              | (iRounding  << X86_MXCSR_RC_SHIFT)
                                              | (iDaz ? X86_MXCSR_DAZ : 0)
                                              | (iFz  ? X86_MXCSR_FZ  : 0)
                                              | X86_MXCSR_XCPT_MASK;
                            X86XMMREG ResM;
                            uint32_t fMxcsrM    = pfn(fMxcsrIn, &ResM, &Src, bImm);
                            TestData.fMxcsrIn   = fMxcsrIn;
                            TestData.fMxcsrOut  = fMxcsrM;
                            TestData.bImm       = bImm;
                            TestData.OutVal     = ResM;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            fMxcsrIn &= ~X86_MXCSR_XCPT_MASK;
                            X86XMMREG ResU;
                            uint32_t fMxcsrU    = pfn(fMxcsrIn, &ResU, &Src, bImm);
                            TestData.fMxcsrIn   = fMxcsrIn;
                            TestData.fMxcsrOut  = fMxcsrU;
                            TestData.bImm       = bImm;
                            TestData.OutVal     = ResU;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                            if (fXcpt)
                            {
                                fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                                X86XMMREG Res1;
                                uint32_t fMxcsr1    = pfn(fMxcsrIn, &Res1, &Src, bImm);
                                TestData.fMxcsrIn   = fMxcsrIn;
                                TestData.fMxcsrOut  = fMxcsr1;
                                TestData.bImm       = bImm;
                                TestData.OutVal     = Res1;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                                if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                                {
                                    fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                    fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                    X86XMMREG Res2;
                                    uint32_t fMxcsr2    = pfn(fMxcsrIn, &Res2, &Src, bImm);
                                    TestData.fMxcsrIn   = fMxcsrIn;
                                    TestData.fMxcsrOut  = fMxcsr2;
                                    TestData.bImm       = bImm;
                                    TestData.OutVal     = Res2;
                                    GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                }
                                if (!RT_IS_POWER_OF_TWO(fXcpt))
                                    for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                        if (fUnmasked & fXcpt)
                                        {
                                            fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                            X86XMMREG Res3;
                                            uint32_t fMxcsr3    = pfn(fMxcsrIn, &Res3, &Src, bImm);
                                            TestData.fMxcsrIn   = fMxcsrIn;
                                            TestData.fMxcsrOut  = fMxcsr3;
                                            TestData.bImm       = bImm;
                                            TestData.OutVal     = Res3;
                                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                        }
                            }
                        }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseCompareF3XmmR32Imm8Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseCompareF3XmmR32Imm8); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseCompareF3XmmR32Imm8[iFn]))
            continue;

        SSE_COMPARE_F3_XMM_IMM8_TEST_T const * const    paTests = g_aSseCompareF3XmmR32Imm8[iFn].paTests;
        uint32_t const                                  cTests  = g_aSseCompareF3XmmR32Imm8[iFn].cTests;
        PFNIEMAIMPLMEDIAF3XMMIMM8                       pfn     = g_aSseCompareF3XmmR32Imm8[iFn].pfn;
        uint32_t const                                  cVars   = COUNT_VARIATIONS(g_aSseCompareF3XmmR32Imm8[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                IEMMEDIAF2XMMSRC Src;
                X86XMMREG ValOut;

                Src.uSrc1 = paTests[iTest].InVal1;
                Src.uSrc2 = paTests[iTest].InVal2;
                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &ValOut, &Src, paTests[iTest].bImm);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || ValOut.au32[0] != paTests[iTest].OutVal.au32[0]
                    || ValOut.au32[1] != paTests[iTest].OutVal.au32[1]
                    || ValOut.au32[2] != paTests[iTest].OutVal.au32[2]
                    || ValOut.au32[3] != paTests[iTest].OutVal.au32[3])
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s'%s'%s'%s in2=%s'%s'%s'%s imm8=%x\n"
                                          "%s               -> mxcsr=%#08x    %RX32'%RX32'%RX32'%RX32\n"
                                          "%s               expected %#08x    %RX32'%RX32'%RX32'%RX32%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR32(&paTests[iTest].InVal1.ar32[0]), FormatR32(&paTests[iTest].InVal1.ar32[1]),
                                 FormatR32(&paTests[iTest].InVal1.ar32[2]), FormatR32(&paTests[iTest].InVal1.ar32[3]),
                                 FormatR32(&paTests[iTest].InVal2.ar32[0]), FormatR32(&paTests[iTest].InVal2.ar32[1]),
                                 FormatR32(&paTests[iTest].InVal2.ar32[2]), FormatR32(&paTests[iTest].InVal2.ar32[3]),
                                 paTests[iTest].bImm,
                                 iVar ? "  " : "", fMxcsr, ValOut.au32[0], ValOut.au32[1], ValOut.au32[2], ValOut.au32[3],
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 paTests[iTest].OutVal.au32[0], paTests[iTest].OutVal.au32[1],
                                 paTests[iTest].OutVal.au32[2], paTests[iTest].OutVal.au32[3],
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   (   ValOut.au32[0] != paTests[iTest].OutVal.au32[0]
                                    || ValOut.au32[1] != paTests[iTest].OutVal.au32[1]
                                    || ValOut.au32[2] != paTests[iTest].OutVal.au32[2]
                                    || ValOut.au32[3] != paTests[iTest].OutVal.au32[3])
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseCompareF3XmmR32Imm8[iFn]);
    }
}


/*
 * Compare SSE operations on packed and single double-precision floating point values - outputting a mask.
 */
static SSE_COMPARE_F3_XMM_IMM8_T g_aSseCompareF3XmmR64Imm8[] =
{
    ENTRY_BIN(cmppd_u128),
    ENTRY_BIN(cmpsd_u128)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseCompareF3XmmR64Imm8, g_aSseCompareF3XmmR64Imm8)
static RTEXITCODE SseCompareF3XmmR64Imm8Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT64U Val1, Val2; } const s_aSpecials[] =
    {
        { RTFLOAT64U_INIT_ZERO(0), RTFLOAT64U_INIT_ZERO(0) },
        { RTFLOAT64U_INIT_ZERO(0), RTFLOAT64U_INIT_ZERO(1) },
        { RTFLOAT64U_INIT_ZERO(1), RTFLOAT64U_INIT_ZERO(0) },
        { RTFLOAT64U_INIT_ZERO(1), RTFLOAT64U_INIT_ZERO(1) },
        { RTFLOAT64U_INIT_INF(0),  RTFLOAT64U_INIT_INF(0)  },
        { RTFLOAT64U_INIT_INF(0),  RTFLOAT64U_INIT_INF(1)  },
        { RTFLOAT64U_INIT_INF(1),  RTFLOAT64U_INIT_INF(0)  },
        { RTFLOAT64U_INIT_INF(1),  RTFLOAT64U_INIT_INF(1)  },
        /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseCompareF3XmmR64Imm8); iFn++)
    {
        PFNIEMAIMPLMEDIAF3XMMIMM8 const pfn = g_aSseCompareF3XmmR64Imm8[iFn].pfnNative ? g_aSseCompareF3XmmR64Imm8[iFn].pfnNative : g_aSseCompareF3XmmR64Imm8[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseCompareF3XmmR64Imm8[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_COMPARE_F3_XMM_IMM8_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal1.ar64[0] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].Val1;
            TestData.InVal1.ar64[1] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].Val1;

            TestData.InVal2.ar64[0] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].Val2;
            TestData.InVal2.ar64[1] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].Val2;

            if (   RTFLOAT64U_IS_NORMAL(&TestData.InVal1.ar64[0])
                && RTFLOAT64U_IS_NORMAL(&TestData.InVal1.ar64[1])
                && RTFLOAT64U_IS_NORMAL(&TestData.InVal2.ar64[0])
                && RTFLOAT64U_IS_NORMAL(&TestData.InVal2.ar64[1]))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            IEMMEDIAF2XMMSRC Src;
            Src.uSrc1 = TestData.InVal1;
            Src.uSrc2 = TestData.InVal2;
            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint8_t bImm = 0; bImm <= SSE_COMPARE_F2_XMM_IMM8_MAX; bImm++)
                for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                    for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                        for (uint8_t iFz = 0; iFz < 2; iFz++)
                        {
                            uint32_t fMxcsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                              | (iRounding  << X86_MXCSR_RC_SHIFT)
                                              | (iDaz ? X86_MXCSR_DAZ : 0)
                                              | (iFz  ? X86_MXCSR_FZ  : 0)
                                              | X86_MXCSR_XCPT_MASK;
                            X86XMMREG ResM;
                            uint32_t fMxcsrM    = pfn(fMxcsrIn, &ResM, &Src, bImm);
                            TestData.fMxcsrIn   = fMxcsrIn;
                            TestData.fMxcsrOut  = fMxcsrM;
                            TestData.bImm       = bImm;
                            TestData.OutVal     = ResM;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            fMxcsrIn &= ~X86_MXCSR_XCPT_MASK;
                            X86XMMREG ResU;
                            uint32_t fMxcsrU    = pfn(fMxcsrIn, &ResU, &Src, bImm);
                            TestData.fMxcsrIn   = fMxcsrIn;
                            TestData.fMxcsrOut  = fMxcsrU;
                            TestData.bImm       = bImm;
                            TestData.OutVal     = ResU;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                            if (fXcpt)
                            {
                                fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                                X86XMMREG Res1;
                                uint32_t fMxcsr1    = pfn(fMxcsrIn, &Res1, &Src, bImm);
                                TestData.fMxcsrIn   = fMxcsrIn;
                                TestData.fMxcsrOut  = fMxcsr1;
                                TestData.bImm       = bImm;
                                TestData.OutVal     = Res1;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                                if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                                {
                                    fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                    fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                    X86XMMREG Res2;
                                    uint32_t fMxcsr2    = pfn(fMxcsrIn, &Res2, &Src, bImm);
                                    TestData.fMxcsrIn   = fMxcsrIn;
                                    TestData.fMxcsrOut  = fMxcsr2;
                                    TestData.bImm       = bImm;
                                    TestData.OutVal     = Res2;
                                    GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                }
                                if (!RT_IS_POWER_OF_TWO(fXcpt))
                                    for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                        if (fUnmasked & fXcpt)
                                        {
                                            fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                            X86XMMREG Res3;
                                            uint32_t fMxcsr3    = pfn(fMxcsrIn, &Res3, &Src, bImm);
                                            TestData.fMxcsrIn   = fMxcsrIn;
                                            TestData.fMxcsrOut  = fMxcsr3;
                                            TestData.bImm       = bImm;
                                            TestData.OutVal     = Res3;
                                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                        }
                            }
                        }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseCompareF3XmmR64Imm8Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseCompareF3XmmR64Imm8); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseCompareF3XmmR64Imm8[iFn]))
            continue;

        SSE_COMPARE_F3_XMM_IMM8_TEST_T const * const    paTests = g_aSseCompareF3XmmR64Imm8[iFn].paTests;
        uint32_t const                                  cTests  = g_aSseCompareF3XmmR64Imm8[iFn].cTests;
        PFNIEMAIMPLMEDIAF3XMMIMM8                       pfn     = g_aSseCompareF3XmmR64Imm8[iFn].pfn;
        uint32_t const                                  cVars   = COUNT_VARIATIONS(g_aSseCompareF3XmmR64Imm8[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                IEMMEDIAF2XMMSRC Src;
                X86XMMREG ValOut;

                Src.uSrc1 = paTests[iTest].InVal1;
                Src.uSrc2 = paTests[iTest].InVal2;
                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &ValOut, &Src, paTests[iTest].bImm);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || ValOut.au64[0] != paTests[iTest].OutVal.au64[0]
                    || ValOut.au64[1] != paTests[iTest].OutVal.au64[1])
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s'%s in2=%s'%s imm8=%x\n"
                                          "%s               -> mxcsr=%#08x    %RX64'%RX64\n"
                                          "%s               expected %#08x    %RX64'%RX64%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR64(&paTests[iTest].InVal1.ar64[0]), FormatR64(&paTests[iTest].InVal1.ar64[1]),
                                 FormatR64(&paTests[iTest].InVal2.ar64[0]), FormatR64(&paTests[iTest].InVal2.ar64[1]),
                                 paTests[iTest].bImm,
                                 iVar ? "  " : "", fMxcsr, ValOut.au64[0], ValOut.au64[1],
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 paTests[iTest].OutVal.au64[0], paTests[iTest].OutVal.au64[1],
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   (   ValOut.au64[0] != paTests[iTest].OutVal.au64[0]
                                    || ValOut.au64[1] != paTests[iTest].OutVal.au64[1])
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseCompareF3XmmR64Imm8[iFn]);
    }
}


/*
 * Convert SSE operations converting signed double-words to single-precision floating point values.
 */
TYPEDEF_SUBTEST_TYPE(SSE_CONVERT_XMM_T, SSE_CONVERT_XMM_TEST_T, PFNIEMAIMPLFPSSEF2U128);

static SSE_CONVERT_XMM_T g_aSseConvertXmmI32R32[] =
{
    ENTRY_BIN(cvtdq2ps_u128)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseConvertXmmI32R32, g_aSseConvertXmmI32R32)
static RTEXITCODE SseConvertXmmI32R32Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static int32_t const s_aSpecials[] =
    {
        INT32_MIN,
        INT32_MIN / 2,
        0,
        INT32_MAX / 2,
        INT32_MAX,
        (int32_t)0x80000000
        /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmI32R32); iFn++)
    {
        PFNIEMAIMPLFPSSEF2U128 const pfn = g_aSseConvertXmmI32R32[iFn].pfnNative ? g_aSseConvertXmmI32R32[iFn].pfnNative : g_aSseConvertXmmI32R32[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseConvertXmmI32R32[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_CONVERT_XMM_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal.ai32[0] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests];
            TestData.InVal.ai32[1] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests];
            TestData.InVal.ai32[2] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests];
            TestData.InVal.ai32[3] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests];

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t uMxCsrOutM = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t uMxCsrOutU = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (uMxCsrOutM | uMxCsrOutU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t uMxCsrOut1 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = uMxCsrOut1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t uMxCsrOut2 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = uMxCsrOut2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t uMxCsrOut3 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = uMxCsrOut3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseConvertXmmI32R32Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmI32R32); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseConvertXmmI32R32[iFn]))
            continue;

        SSE_CONVERT_XMM_TEST_T const * const    paTests = g_aSseConvertXmmI32R32[iFn].paTests;
        uint32_t const                          cTests  = g_aSseConvertXmmI32R32[iFn].cTests;
        PFNIEMAIMPLFPSSEF2U128                  pfn     = g_aSseConvertXmmI32R32[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSseConvertXmmI32R32[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                X86XMMREG Res; RT_ZERO(Res);

                uint32_t fMxCsr = pfn(paTests[iTest].fMxcsrIn, &Res, &Res, &paTests[iTest].InVal);
                if (   fMxCsr != paTests[iTest].fMxcsrOut
                    || !RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[0], &paTests[iTest].OutVal.ar32[0])
                    || !RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[1], &paTests[iTest].OutVal.ar32[1])
                    || !RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[2], &paTests[iTest].OutVal.ar32[2])
                    || !RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[3], &paTests[iTest].OutVal.ar32[3]))
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%RI32'%RI32'%RI32'%RI32 \n"
                                          "%s               -> mxcsr=%#08x    %s'%s'%s'%s\n"
                                          "%s               expected %#08x    %s'%s'%s'%s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 paTests[iTest].InVal.ai32[0], paTests[iTest].InVal.ai32[1],
                                 paTests[iTest].InVal.ai32[2], paTests[iTest].InVal.ai32[3],
                                 iVar ? "  " : "", fMxCsr,
                                 FormatR32(&Res.ar32[0]), FormatR32(&Res.ar32[1]),
                                 FormatR32(&Res.ar32[2]), FormatR32(&Res.ar32[3]),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 FormatR32(&paTests[iTest].OutVal.ar32[0]), FormatR32(&paTests[iTest].OutVal.ar32[1]),
                                 FormatR32(&paTests[iTest].OutVal.ar32[2]), FormatR32(&paTests[iTest].OutVal.ar32[3]),
                                 MxcsrDiff(fMxCsr, paTests[iTest].fMxcsrOut),
                                   (   !RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[0], &paTests[iTest].OutVal.ar32[0])
                                    || !RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[1], &paTests[iTest].OutVal.ar32[1])
                                    || !RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[2], &paTests[iTest].OutVal.ar32[2])
                                    || !RTFLOAT32U_ARE_IDENTICAL(&Res.ar32[3], &paTests[iTest].OutVal.ar32[3]))
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseConvertXmmI32R32[iFn]);
    }
}


/*
 * Convert SSE operations converting signed double-words to single-precision floating point values.
 */
static SSE_CONVERT_XMM_T g_aSseConvertXmmR32I32[] =
{
    ENTRY_BIN(cvtps2dq_u128),
    ENTRY_BIN(cvttps2dq_u128)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseConvertXmmR32I32, g_aSseConvertXmmR32I32)
static RTEXITCODE SseConvertXmmR32I32Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT32U aVal1[4]; } const s_aSpecials[] =
    {
        { { RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0) } },
        { { RTFLOAT32U_INIT_ZERO(1), RTFLOAT32U_INIT_ZERO(1), RTFLOAT32U_INIT_ZERO(1), RTFLOAT32U_INIT_ZERO(1) } },
        { { RTFLOAT32U_INIT_INF(0),  RTFLOAT32U_INIT_INF(0),  RTFLOAT32U_INIT_INF(0),  RTFLOAT32U_INIT_INF(0)  } },
        { { RTFLOAT32U_INIT_INF(1),  RTFLOAT32U_INIT_INF(1),  RTFLOAT32U_INIT_INF(1),  RTFLOAT32U_INIT_INF(1)  } }
          /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmR32I32); iFn++)
    {
        PFNIEMAIMPLFPSSEF2U128 const pfn = g_aSseConvertXmmR32I32[iFn].pfnNative ? g_aSseConvertXmmR32I32[iFn].pfnNative : g_aSseConvertXmmR32I32[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseConvertXmmR32I32[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_CONVERT_XMM_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal.ar32[0] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[0];
            TestData.InVal.ar32[1] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[1];
            TestData.InVal.ar32[2] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[2];
            TestData.InVal.ar32[3] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[3];

            if (   RTFLOAT32U_IS_NORMAL(&TestData.InVal.ar32[0])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal.ar32[1])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal.ar32[2])
                && RTFLOAT32U_IS_NORMAL(&TestData.InVal.ar32[3]))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t uMxCsrOutM = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t uMxCsrOutU = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (uMxCsrOutM | uMxCsrOutU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t uMxCsrOut1 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = uMxCsrOut1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t uMxCsrOut2 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = uMxCsrOut2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t uMxCsrOut3 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = uMxCsrOut3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseConvertXmmR32I32Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmR32I32); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseConvertXmmR32I32[iFn]))
            continue;

        SSE_CONVERT_XMM_TEST_T const * const    paTests = g_aSseConvertXmmR32I32[iFn].paTests;
        uint32_t const                          cTests  = g_aSseConvertXmmR32I32[iFn].cTests;
        PFNIEMAIMPLFPSSEF2U128                  pfn     = g_aSseConvertXmmR32I32[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSseConvertXmmR32I32[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                X86XMMREG Res; RT_ZERO(Res);

                uint32_t fMxCsr = pfn(paTests[iTest].fMxcsrIn, &Res, &Res, &paTests[iTest].InVal);
                if (   fMxCsr != paTests[iTest].fMxcsrOut
                    || Res.ai32[0] != paTests[iTest].OutVal.ai32[0]
                    || Res.ai32[1] != paTests[iTest].OutVal.ai32[1]
                    || Res.ai32[2] != paTests[iTest].OutVal.ai32[2]
                    || Res.ai32[3] != paTests[iTest].OutVal.ai32[3])
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s'%s'%s'%s \n"
                                          "%s               -> mxcsr=%#08x    %RI32'%RI32'%RI32'%RI32\n"
                                          "%s               expected %#08x    %RI32'%RI32'%RI32'%RI32%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR32(&paTests[iTest].InVal.ar32[0]), FormatR32(&paTests[iTest].InVal.ar32[1]),
                                 FormatR32(&paTests[iTest].InVal.ar32[2]), FormatR32(&paTests[iTest].InVal.ar32[3]),
                                 iVar ? "  " : "", fMxCsr,
                                 Res.ai32[0], Res.ai32[1],
                                 Res.ai32[2], Res.ai32[3],
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 paTests[iTest].OutVal.ai32[0], paTests[iTest].OutVal.ai32[1],
                                 paTests[iTest].OutVal.ai32[2], paTests[iTest].OutVal.ai32[3],
                                 MxcsrDiff(fMxCsr, paTests[iTest].fMxcsrOut),
                                   (   Res.ai32[0] != paTests[iTest].OutVal.ai32[0]
                                    || Res.ai32[1] != paTests[iTest].OutVal.ai32[1]
                                    || Res.ai32[2] != paTests[iTest].OutVal.ai32[2]
                                    || Res.ai32[3] != paTests[iTest].OutVal.ai32[3])
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseConvertXmmR32I32[iFn]);
    }
}


/*
 * Convert SSE operations converting signed double-words to double-precision floating point values.
 */
static SSE_CONVERT_XMM_T g_aSseConvertXmmI32R64[] =
{
    ENTRY_BIN(cvtdq2pd_u128)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseConvertXmmI32R64, g_aSseConvertXmmI32R64)
static RTEXITCODE SseConvertXmmI32R64Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static int32_t const s_aSpecials[] =
    {
        INT32_MIN,
        INT32_MIN / 2,
        0,
        INT32_MAX / 2,
        INT32_MAX,
        (int32_t)0x80000000
        /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmI32R64); iFn++)
    {
        PFNIEMAIMPLFPSSEF2U128 const pfn = g_aSseConvertXmmI32R64[iFn].pfnNative ? g_aSseConvertXmmI32R64[iFn].pfnNative : g_aSseConvertXmmI32R64[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseConvertXmmI32R64[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_CONVERT_XMM_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal.ai32[0] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests];
            TestData.InVal.ai32[1] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests];
            TestData.InVal.ai32[2] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests];
            TestData.InVal.ai32[3] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests];

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t uMxCsrOutM = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t uMxCsrOutU = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (uMxCsrOutM | uMxCsrOutU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t uMxCsrOut1 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = uMxCsrOut1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t uMxCsrOut2 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = uMxCsrOut2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t uMxCsrOut3 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = uMxCsrOut3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseConvertXmmI32R64Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmI32R64); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseConvertXmmI32R64[iFn]))
            continue;

        SSE_CONVERT_XMM_TEST_T const * const    paTests = g_aSseConvertXmmI32R64[iFn].paTests;
        uint32_t const                          cTests  = g_aSseConvertXmmI32R64[iFn].cTests;
        PFNIEMAIMPLFPSSEF2U128                  pfn     = g_aSseConvertXmmI32R64[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSseConvertXmmI32R64[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                X86XMMREG Res; RT_ZERO(Res);

                uint32_t fMxCsr = pfn(paTests[iTest].fMxcsrIn, &Res, &Res, &paTests[iTest].InVal);
                if (   fMxCsr != paTests[iTest].fMxcsrOut
                    || !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[0], &paTests[iTest].OutVal.ar64[0])
                    || !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[1], &paTests[iTest].OutVal.ar64[1]))
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%RI32'%RI32'%RI32'%RI32 \n"
                                          "%s               -> mxcsr=%#08x    %s'%s\n"
                                          "%s               expected %#08x    %s'%s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 paTests[iTest].InVal.ai32[0], paTests[iTest].InVal.ai32[1],
                                 paTests[iTest].InVal.ai32[2], paTests[iTest].InVal.ai32[3],
                                 iVar ? "  " : "", fMxCsr,
                                 FormatR64(&Res.ar64[0]), FormatR64(&Res.ar64[1]),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 FormatR64(&paTests[iTest].OutVal.ar64[0]), FormatR64(&paTests[iTest].OutVal.ar64[1]),
                                 MxcsrDiff(fMxCsr, paTests[iTest].fMxcsrOut),
                                   (   !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[0], &paTests[iTest].OutVal.ar64[0])
                                    || !RTFLOAT64U_ARE_IDENTICAL(&Res.ar64[1], &paTests[iTest].OutVal.ar64[1]))
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseConvertXmmI32R64[iFn]);
    }
}


/*
 * Convert SSE operations converting signed double-words to double-precision floating point values.
 */
static SSE_CONVERT_XMM_T g_aSseConvertXmmR64I32[] =
{
    ENTRY_BIN(cvtpd2dq_u128),
    ENTRY_BIN(cvttpd2dq_u128)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseConvertXmmR64I32, g_aSseConvertXmmR64I32)
static RTEXITCODE SseConvertXmmR64I32Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT64U aVal1[2]; } const s_aSpecials[] =
    {
        { { RTFLOAT64U_INIT_ZERO(0), RTFLOAT64U_INIT_ZERO(0) } },
        { { RTFLOAT64U_INIT_ZERO(1), RTFLOAT64U_INIT_ZERO(1) } },
        { { RTFLOAT64U_INIT_INF(0),  RTFLOAT64U_INIT_INF(0)  } },
        { { RTFLOAT64U_INIT_INF(1),  RTFLOAT64U_INIT_INF(1)  } }
          /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmR64I32); iFn++)
    {
        PFNIEMAIMPLFPSSEF2U128 const pfn = g_aSseConvertXmmR64I32[iFn].pfnNative ? g_aSseConvertXmmR64I32[iFn].pfnNative : g_aSseConvertXmmR64I32[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseConvertXmmR64I32[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_CONVERT_XMM_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal.ar64[0] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].aVal1[0];
            TestData.InVal.ar64[1] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].aVal1[1];

            if (   RTFLOAT64U_IS_NORMAL(&TestData.InVal.ar64[0])
                && RTFLOAT64U_IS_NORMAL(&TestData.InVal.ar64[1]))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t uMxCsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t uMxCsrOutM = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uMxCsrIn = uMxCsrIn & ~X86_MXCSR_XCPT_MASK;
                        uint32_t uMxCsrOutU = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                        TestData.fMxcsrIn  = uMxCsrIn;
                        TestData.fMxcsrOut = uMxCsrOutU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (uMxCsrOutM | uMxCsrOutU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t uMxCsrOut1 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                            TestData.fMxcsrIn  = uMxCsrIn;
                            TestData.fMxcsrOut = uMxCsrOut1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= uMxCsrOut1 & X86_MXCSR_XCPT_FLAGS;
                                uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t uMxCsrOut2 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                                TestData.fMxcsrIn  = uMxCsrIn;
                                TestData.fMxcsrOut = uMxCsrOut2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        uMxCsrIn = (uMxCsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t uMxCsrOut3 = pfn(uMxCsrIn, &TestData.OutVal, &TestData.OutVal, &TestData.InVal);
                                        TestData.fMxcsrIn  = uMxCsrIn;
                                        TestData.fMxcsrOut = uMxCsrOut3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseConvertXmmR64I32Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmR64I32); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseConvertXmmR64I32[iFn]))
            continue;

        SSE_CONVERT_XMM_TEST_T const * const    paTests = g_aSseConvertXmmR64I32[iFn].paTests;
        uint32_t const                          cTests  = g_aSseConvertXmmR64I32[iFn].cTests;
        PFNIEMAIMPLFPSSEF2U128                  pfn     = g_aSseConvertXmmR64I32[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSseConvertXmmR64I32[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                X86XMMREG Res; RT_ZERO(Res);

                uint32_t fMxCsr = pfn(paTests[iTest].fMxcsrIn, &Res, &Res, &paTests[iTest].InVal);
                if (   fMxCsr != paTests[iTest].fMxcsrOut
                    || Res.ai32[0] != paTests[iTest].OutVal.ai32[0]
                    || Res.ai32[1] != paTests[iTest].OutVal.ai32[1]
                    || Res.ai32[2] != paTests[iTest].OutVal.ai32[2]
                    || Res.ai32[3] != paTests[iTest].OutVal.ai32[3])
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s'%s \n"
                                          "%s               -> mxcsr=%#08x    %RI32'%RI32'%RI32'%RI32\n"
                                          "%s               expected %#08x    %RI32'%RI32'%RI32'%RI32%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR64(&paTests[iTest].InVal.ar64[0]), FormatR64(&paTests[iTest].InVal.ar64[1]),
                                 iVar ? "  " : "", fMxCsr,
                                 Res.ai32[0], Res.ai32[1],
                                 Res.ai32[2], Res.ai32[3],
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 paTests[iTest].OutVal.ai32[0], paTests[iTest].OutVal.ai32[1],
                                 paTests[iTest].OutVal.ai32[2], paTests[iTest].OutVal.ai32[3],
                                 MxcsrDiff(fMxCsr, paTests[iTest].fMxcsrOut),
                                   (   Res.ai32[0] != paTests[iTest].OutVal.ai32[0]
                                    || Res.ai32[1] != paTests[iTest].OutVal.ai32[1]
                                    || Res.ai32[2] != paTests[iTest].OutVal.ai32[2]
                                    || Res.ai32[3] != paTests[iTest].OutVal.ai32[3])
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseConvertXmmR64I32[iFn]);
    }
}


/*
 * Convert SSE operations converting double-precision floating point values to signed double-word values.
 */
TYPEDEF_SUBTEST_TYPE(SSE_CONVERT_MM_XMM_T, SSE_CONVERT_MM_XMM_TEST_T, PFNIEMAIMPLMXCSRU64U128);

static SSE_CONVERT_MM_XMM_T g_aSseConvertMmXmm[] =
{
    ENTRY_BIN(cvtpd2pi_u128),
    ENTRY_BIN(cvttpd2pi_u128)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseConvertMmXmm, g_aSseConvertMmXmm)
static RTEXITCODE SseConvertMmXmmGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT64U aVal1[2]; } const s_aSpecials[] =
    {
        { { RTFLOAT64U_INIT_ZERO(0), RTFLOAT64U_INIT_ZERO(0) } },
        { { RTFLOAT64U_INIT_ZERO(1), RTFLOAT64U_INIT_ZERO(1) } },
        { { RTFLOAT64U_INIT_INF(0),  RTFLOAT64U_INIT_INF(0)  } },
        { { RTFLOAT64U_INIT_INF(1),  RTFLOAT64U_INIT_INF(1)  } }
          /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertMmXmm); iFn++)
    {
        PFNIEMAIMPLMXCSRU64U128 const pfn = g_aSseConvertMmXmm[iFn].pfnNative ? g_aSseConvertMmXmm[iFn].pfnNative : g_aSseConvertMmXmm[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseConvertMmXmm[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_CONVERT_MM_XMM_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal.ar64[0] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].aVal1[0];
            TestData.InVal.ar64[1] = iTest < cTests ? RandR64Src(iTest) : s_aSpecials[iTest - cTests].aVal1[1];

            if (   RTFLOAT64U_IS_NORMAL(&TestData.InVal.ar64[0])
                && RTFLOAT64U_IS_NORMAL(&TestData.InVal.ar64[1]))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t fMxcsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint64_t u64ResM;
                        uint32_t fMxcsrM    = pfn(fMxcsrIn, &u64ResM, &TestData.InVal);
                        TestData.fMxcsrIn   = fMxcsrIn;
                        TestData.fMxcsrOut  = fMxcsrM;
                        TestData.OutVal.u   = u64ResM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        fMxcsrIn &= ~X86_MXCSR_XCPT_MASK;
                        uint64_t u64ResU;
                        uint32_t fMxcsrU    = pfn(fMxcsrIn, &u64ResU, &TestData.InVal);
                        TestData.fMxcsrIn   = fMxcsrIn;
                        TestData.fMxcsrOut  = fMxcsrU;
                        TestData.OutVal.u   = u64ResU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint64_t u64Res1;
                            uint32_t fMxcsr1    = pfn(fMxcsrIn, &u64Res1, &TestData.InVal);
                            TestData.fMxcsrIn   = fMxcsrIn;
                            TestData.fMxcsrOut  = fMxcsr1;
                            TestData.OutVal.u   = u64Res1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint64_t u64Res2;
                                uint32_t fMxcsr2    = pfn(fMxcsrIn, &u64Res2, &TestData.InVal);
                                TestData.fMxcsrIn   = fMxcsrIn;
                                TestData.fMxcsrOut  = fMxcsr2;
                                TestData.OutVal.u   = u64Res2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint64_t u64Res3;
                                        uint32_t fMxcsr3    = pfn(fMxcsrIn, &u64Res3, &TestData.InVal);
                                        TestData.fMxcsrIn   = fMxcsrIn;
                                        TestData.fMxcsrOut  = fMxcsr3;
                                        TestData.OutVal.u   = u64Res3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseConvertMmXmmTest(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertMmXmm); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseConvertMmXmm[iFn]))
            continue;

        SSE_CONVERT_MM_XMM_TEST_T const * const paTests = g_aSseConvertMmXmm[iFn].paTests;
        uint32_t const                          cTests  = g_aSseConvertMmXmm[iFn].cTests;
        PFNIEMAIMPLMXCSRU64U128                 pfn     = g_aSseConvertMmXmm[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSseConvertMmXmm[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTUINT64U ValOut;
                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &ValOut.u, &paTests[iTest].InVal);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || ValOut.ai32[0] != paTests[iTest].OutVal.ai32[0]
                    || ValOut.ai32[1] != paTests[iTest].OutVal.ai32[1])
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s'%s\n"
                                          "%s               -> mxcsr=%#08x    %RI32'%RI32\n"
                                          "%s               expected %#08x    %RI32'%RI32%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR64(&paTests[iTest].InVal.ar64[0]), FormatR64(&paTests[iTest].InVal.ar64[1]),
                                 iVar ? "  " : "", fMxcsr, ValOut.ai32[0], ValOut.ai32[1],
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 paTests[iTest].OutVal.ai32[0], paTests[iTest].OutVal.ai32[1],
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   (   ValOut.ai32[0] != paTests[iTest].OutVal.ai32[0]
                                    || ValOut.ai32[1] != paTests[iTest].OutVal.ai32[1])
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseConvertMmXmm[iFn]);
    }
}


/*
 * Convert SSE operations converting signed double-word values to double precision floating-point values (probably only cvtpi2pd).
 */
TYPEDEF_SUBTEST_TYPE(SSE_CONVERT_XMM_R64_MM_T, SSE_CONVERT_XMM_MM_TEST_T, PFNIEMAIMPLMXCSRU128U64);

static SSE_CONVERT_XMM_R64_MM_T g_aSseConvertXmmR64Mm[] =
{
    ENTRY_BIN(cvtpi2pd_u128)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseConvertXmmR64Mm, g_aSseConvertXmmR64Mm)
static RTEXITCODE SseConvertXmmR64MmGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { int32_t aVal[2]; } const s_aSpecials[] =
    {
        { { INT32_MIN, INT32_MIN } },
        { { INT32_MAX, INT32_MAX } }
          /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmR64Mm); iFn++)
    {
        PFNIEMAIMPLMXCSRU128U64 const pfn = g_aSseConvertXmmR64Mm[iFn].pfnNative ? g_aSseConvertXmmR64Mm[iFn].pfnNative : g_aSseConvertXmmR64Mm[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseConvertXmmR64Mm[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_CONVERT_XMM_MM_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal.ai32[0] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests].aVal[0];
            TestData.InVal.ai32[1] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests].aVal[1];

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t fMxcsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM    = pfn(fMxcsrIn, &TestData.OutVal, TestData.InVal.u);
                        TestData.fMxcsrIn   = fMxcsrIn;
                        TestData.fMxcsrOut  = fMxcsrM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        fMxcsrIn &= ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU    = pfn(fMxcsrIn, &TestData.OutVal, TestData.InVal.u);
                        TestData.fMxcsrIn   = fMxcsrIn;
                        TestData.fMxcsrOut  = fMxcsrU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1    = pfn(fMxcsrIn, &TestData.OutVal, TestData.InVal.u);
                            TestData.fMxcsrIn   = fMxcsrIn;
                            TestData.fMxcsrOut  = fMxcsr1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2    = pfn(fMxcsrIn, &TestData.OutVal, TestData.InVal.u);
                                TestData.fMxcsrIn   = fMxcsrIn;
                                TestData.fMxcsrOut  = fMxcsr2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3    = pfn(fMxcsrIn, &TestData.OutVal, TestData.InVal.u);
                                        TestData.fMxcsrIn   = fMxcsrIn;
                                        TestData.fMxcsrOut  = fMxcsr3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseConvertXmmR64MmTest(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmR64Mm); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseConvertXmmR64Mm[iFn]))
            continue;

        SSE_CONVERT_XMM_MM_TEST_T const * const paTests = g_aSseConvertXmmR64Mm[iFn].paTests;
        uint32_t const                          cTests  = g_aSseConvertXmmR64Mm[iFn].cTests;
        PFNIEMAIMPLMXCSRU128U64                 pfn     = g_aSseConvertXmmR64Mm[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSseConvertXmmR64Mm[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                X86XMMREG ValOut;
                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &ValOut, paTests[iTest].InVal.u);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || !RTFLOAT64U_ARE_IDENTICAL(&ValOut.ar64[0], &paTests[iTest].OutVal.ar64[0])
                    || !RTFLOAT64U_ARE_IDENTICAL(&ValOut.ar64[1], &paTests[iTest].OutVal.ar64[1]))
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%RI32'%RI32\n"
                                          "%s               -> mxcsr=%#08x    %s'%s\n"
                                          "%s               expected %#08x    %s'%s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 paTests[iTest].InVal.ai32[0], paTests[iTest].InVal.ai32[1],
                                 iVar ? "  " : "", fMxcsr,
                                 FormatR64(&ValOut.ar64[0]), FormatR64(&ValOut.ar64[1]),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 FormatR64(&paTests[iTest].OutVal.ar64[0]), FormatR64(&paTests[iTest].OutVal.ar64[1]),
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   (   !RTFLOAT64U_ARE_IDENTICAL(&ValOut.ar64[0], &paTests[iTest].OutVal.ar64[0])
                                    || !RTFLOAT64U_ARE_IDENTICAL(&ValOut.ar64[1], &paTests[iTest].OutVal.ar64[1]))
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseConvertXmmR64Mm[iFn]);
    }
}


/*
 * Convert SSE operations converting signed double-word values to double precision floating-point values (probably only cvtpi2pd).
 */
TYPEDEF_SUBTEST_TYPE(SSE_CONVERT_XMM_R32_MM_T, SSE_CONVERT_XMM_MM_TEST_T, PFNIEMAIMPLMXCSRU128U64);

static SSE_CONVERT_XMM_R32_MM_T g_aSseConvertXmmR32Mm[] =
{
    ENTRY_BIN(cvtpi2ps_u128)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseConvertXmmR32Mm, g_aSseConvertXmmR32Mm)
static RTEXITCODE SseConvertXmmR32MmGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { int32_t aVal[2]; } const s_aSpecials[] =
    {
        { { INT32_MIN, INT32_MIN } },
        { { INT32_MAX, INT32_MAX } }
          /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmR32Mm); iFn++)
    {
        PFNIEMAIMPLMXCSRU128U64 const pfn = g_aSseConvertXmmR32Mm[iFn].pfnNative ? g_aSseConvertXmmR32Mm[iFn].pfnNative : g_aSseConvertXmmR32Mm[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseConvertXmmR32Mm[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_CONVERT_XMM_MM_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal.ai32[0] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests].aVal[0];
            TestData.InVal.ai32[1] = iTest < cTests ? RandI32Src2(iTest) : s_aSpecials[iTest - cTests].aVal[1];

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t fMxcsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrM    = pfn(fMxcsrIn, &TestData.OutVal, TestData.InVal.u);
                        TestData.fMxcsrIn   = fMxcsrIn;
                        TestData.fMxcsrOut  = fMxcsrM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        fMxcsrIn &= ~X86_MXCSR_XCPT_MASK;
                        uint32_t fMxcsrU    = pfn(fMxcsrIn, &TestData.OutVal, TestData.InVal.u);
                        TestData.fMxcsrIn   = fMxcsrIn;
                        TestData.fMxcsrOut  = fMxcsrU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint32_t fMxcsr1    = pfn(fMxcsrIn, &TestData.OutVal, TestData.InVal.u);
                            TestData.fMxcsrIn   = fMxcsrIn;
                            TestData.fMxcsrOut  = fMxcsr1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint32_t fMxcsr2    = pfn(fMxcsrIn, &TestData.OutVal, TestData.InVal.u);
                                TestData.fMxcsrIn   = fMxcsrIn;
                                TestData.fMxcsrOut  = fMxcsr2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint32_t fMxcsr3    = pfn(fMxcsrIn, &TestData.OutVal, TestData.InVal.u);
                                        TestData.fMxcsrIn   = fMxcsrIn;
                                        TestData.fMxcsrOut  = fMxcsr3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseConvertXmmR32MmTest(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertXmmR32Mm); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseConvertXmmR32Mm[iFn]))
            continue;

        SSE_CONVERT_XMM_MM_TEST_T const * const paTests = g_aSseConvertXmmR32Mm[iFn].paTests;
        uint32_t const                          cTests  = g_aSseConvertXmmR32Mm[iFn].cTests;
        PFNIEMAIMPLMXCSRU128U64                 pfn     = g_aSseConvertXmmR32Mm[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSseConvertXmmR32Mm[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                X86XMMREG ValOut;
                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &ValOut, paTests[iTest].InVal.u);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || !RTFLOAT32U_ARE_IDENTICAL(&ValOut.ar32[0], &paTests[iTest].OutVal.ar32[0])
                    || !RTFLOAT32U_ARE_IDENTICAL(&ValOut.ar32[1], &paTests[iTest].OutVal.ar32[1]))
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%RI32'%RI32\n"
                                          "%s               -> mxcsr=%#08x    %s'%s\n"
                                          "%s               expected %#08x    %s'%s%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 paTests[iTest].InVal.ai32[0], paTests[iTest].InVal.ai32[1],
                                 iVar ? "  " : "", fMxcsr,
                                 FormatR32(&ValOut.ar32[0]), FormatR32(&ValOut.ar32[1]),
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 FormatR32(&paTests[iTest].OutVal.ar32[0]), FormatR32(&paTests[iTest].OutVal.ar32[1]),
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   (   !RTFLOAT32U_ARE_IDENTICAL(&ValOut.ar32[0], &paTests[iTest].OutVal.ar32[0])
                                    || !RTFLOAT32U_ARE_IDENTICAL(&ValOut.ar32[1], &paTests[iTest].OutVal.ar32[1]))
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseConvertXmmR32Mm[iFn]);
    }
}


/*
 * Convert SSE operations converting single-precision floating point values to signed double-word values.
 */
TYPEDEF_SUBTEST_TYPE(SSE_CONVERT_MM_I32_XMM_R32_T, SSE_CONVERT_MM_R32_TEST_T, PFNIEMAIMPLMXCSRU64U64);

static SSE_CONVERT_MM_I32_XMM_R32_T g_aSseConvertMmI32XmmR32[] =
{
    ENTRY_BIN(cvtps2pi_u128),
    ENTRY_BIN(cvttps2pi_u128)
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseConvertMmI32XmmR32, g_aSseConvertMmI32XmmR32)
static RTEXITCODE SseConvertMmI32XmmR32Generate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTFLOAT32U aVal1[2]; } const s_aSpecials[] =
    {
        { { RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(0)  } },
        { { RTFLOAT32U_INIT_ZERO(1), RTFLOAT32U_INIT_ZERO(1)  } },
        { { RTFLOAT32U_INIT_INF(0),  RTFLOAT32U_INIT_INF(0)   } },
        { { RTFLOAT32U_INIT_INF(1),  RTFLOAT32U_INIT_INF(1)   } }
          /** @todo More specials. */
    };

    uint32_t cMinNormalPairs       = (cTests - 144) / 4;
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertMmI32XmmR32); iFn++)
    {
        PFNIEMAIMPLMXCSRU64U64 const pfn = g_aSseConvertMmI32XmmR32[iFn].pfnNative ? g_aSseConvertMmI32XmmR32[iFn].pfnNative : g_aSseConvertMmI32XmmR32[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSseConvertMmI32XmmR32[iFn]), RTEXITCODE_FAILURE);

        uint32_t cNormalInputPairs  = 0;
        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_CONVERT_MM_R32_TEST_T TestData; RT_ZERO(TestData);

            TestData.ar32InVal[0] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[0];
            TestData.ar32InVal[1] = iTest < cTests ? RandR32Src(iTest) : s_aSpecials[iTest - cTests].aVal1[1];

            if (   RTFLOAT32U_IS_NORMAL(&TestData.ar32InVal[0])
                && RTFLOAT32U_IS_NORMAL(&TestData.ar32InVal[1]))
                cNormalInputPairs++;
            else if (cNormalInputPairs < cMinNormalPairs && iTest + cMinNormalPairs >= cTests && iTest < cTests)
            {
                iTest -= 1;
                continue;
            }

            RTFLOAT64U TestVal;
            TestVal.au32[0] = TestData.ar32InVal[0].u;
            TestVal.au32[1] = TestData.ar32InVal[1].u;

            uint32_t const fMxcsr = RandMxcsr() & X86_MXCSR_XCPT_FLAGS;
            for (uint16_t iRounding = 0; iRounding < 4; iRounding++)
                for (uint8_t iDaz = 0; iDaz < 2; iDaz++)
                    for (uint8_t iFz = 0; iFz < 2; iFz++)
                    {
                        uint32_t fMxcsrIn = (fMxcsr & ~X86_MXCSR_RC_MASK)
                                          | (iRounding  << X86_MXCSR_RC_SHIFT)
                                          | (iDaz ? X86_MXCSR_DAZ : 0)
                                          | (iFz  ? X86_MXCSR_FZ  : 0)
                                          | X86_MXCSR_XCPT_MASK;
                        uint64_t u64ResM;
                        uint32_t fMxcsrM   = pfn(fMxcsrIn, &u64ResM, TestVal.u);
                        TestData.fMxcsrIn  = fMxcsrIn;
                        TestData.fMxcsrOut = fMxcsrM;
                        TestData.OutVal.u  = u64ResM;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        fMxcsrIn &= ~X86_MXCSR_XCPT_MASK;
                        uint64_t u64ResU;
                        uint32_t fMxcsrU   = pfn(fMxcsrIn, &u64ResU, TestVal.u);
                        TestData.fMxcsrIn  = fMxcsrIn;
                        TestData.fMxcsrOut = fMxcsrU;
                        TestData.OutVal.u  = u64ResU;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                        uint16_t fXcpt = (fMxcsrM | fMxcsrU) & X86_MXCSR_XCPT_FLAGS;
                        if (fXcpt)
                        {
                            fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | fXcpt;
                            uint64_t u64Res1;
                            uint32_t fMxcsr1   = pfn(fMxcsrIn, &u64Res1, TestVal.u);
                            TestData.fMxcsrIn  = fMxcsrIn;
                            TestData.fMxcsrOut = fMxcsr1;
                            TestData.OutVal.u  = u64Res1;
                            GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));

                            if (((fMxcsr1 & X86_MXCSR_XCPT_FLAGS) & fXcpt) != (fMxcsr1 & X86_MXCSR_XCPT_FLAGS))
                            {
                                fXcpt |= fMxcsr1 & X86_MXCSR_XCPT_FLAGS;
                                fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | (fXcpt << X86_MXCSR_XCPT_MASK_SHIFT);
                                uint64_t u64Res2;
                                uint32_t fMxcsr2   = pfn(fMxcsrIn, &u64Res2, TestVal.u);
                                TestData.fMxcsrIn  = fMxcsrIn;
                                TestData.fMxcsrOut = fMxcsr2;
                                TestData.OutVal.u  = u64Res2;
                                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                            }
                            if (!RT_IS_POWER_OF_TWO(fXcpt))
                                for (uint16_t fUnmasked = 1; fUnmasked <= X86_MXCSR_PE; fUnmasked <<= 1)
                                    if (fUnmasked & fXcpt)
                                    {
                                        fMxcsrIn = (fMxcsrIn & ~X86_MXCSR_XCPT_MASK) | ((fXcpt & ~fUnmasked) << X86_MXCSR_XCPT_MASK_SHIFT);
                                        uint64_t u64Res3;
                                        uint32_t fMxcsr3   = pfn(fMxcsrIn, &u64Res3, TestVal.u);
                                        TestData.fMxcsrIn  = fMxcsrIn;
                                        TestData.fMxcsrOut = fMxcsr3;
                                        TestData.OutVal.u  = u64Res3;
                                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                                    }
                        }
                    }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseConvertMmI32XmmR32Test(void)
{
    X86FXSTATE State;
    RT_ZERO(State);

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSseConvertMmI32XmmR32); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSseConvertMmI32XmmR32[iFn]))
            continue;

        SSE_CONVERT_MM_R32_TEST_T const * const paTests = g_aSseConvertMmI32XmmR32[iFn].paTests;
        uint32_t const                          cTests  = g_aSseConvertMmI32XmmR32[iFn].cTests;
        PFNIEMAIMPLMXCSRU64U64                  pfn     = g_aSseConvertMmI32XmmR32[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSseConvertMmI32XmmR32[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                RTUINT64U ValOut;
                RTUINT64U ValIn;

                ValIn.au32[0] = paTests[iTest].ar32InVal[0].u;
                ValIn.au32[1] = paTests[iTest].ar32InVal[1].u;

                uint32_t fMxcsr = pfn(paTests[iTest].fMxcsrIn, &ValOut.u, ValIn.u);
                if (   fMxcsr != paTests[iTest].fMxcsrOut
                    || ValOut.ai32[0] != paTests[iTest].OutVal.ai32[0]
                    || ValOut.ai32[1] != paTests[iTest].OutVal.ai32[1])
                    RTTestFailed(g_hTest, "#%04u%s: mxcsr=%#08x in1=%s'%s \n"
                                          "%s               -> mxcsr=%#08x    %RI32'%RI32\n"
                                          "%s               expected %#08x    %RI32'%RI32%s%s (%s)\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fMxcsrIn,
                                 FormatR32(&paTests[iTest].ar32InVal[0]), FormatR32(&paTests[iTest].ar32InVal[1]),
                                 iVar ? "  " : "", fMxcsr,
                                 ValOut.ai32[0], ValOut.ai32[1],
                                 iVar ? "  " : "", paTests[iTest].fMxcsrOut,
                                 paTests[iTest].OutVal.ai32[0], paTests[iTest].OutVal.ai32[1],
                                 MxcsrDiff(fMxcsr, paTests[iTest].fMxcsrOut),
                                   (   ValOut.ai32[0] != paTests[iTest].OutVal.ai32[0]
                                    || ValOut.ai32[1] != paTests[iTest].OutVal.ai32[1])
                                 ? " - val" : "",
                                 FormatMxcsr(paTests[iTest].fMxcsrIn));
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSseConvertMmI32XmmR32[iFn]);
    }
}


/*
 * SSE 4.2 pcmpxstrx instructions.
 */
TYPEDEF_SUBTEST_TYPE(SSE_PCMPISTRI_T, SSE_PCMPISTRI_TEST_T, PFNIEMAIMPLPCMPISTRIU128IMM8);

static SSE_PCMPISTRI_T g_aSsePcmpistri[] =
{
    ENTRY_BIN_SSE_OPT(pcmpistri_u128),
    ENTRY_BIN_SSE_OPT(vpcmpistri_u128),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseComparePcmpistri, g_aSsePcmpistri)
static RTEXITCODE SseComparePcmpistriGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTUINT128U uSrc1; RTUINT128U uSrc2; } const s_aSpecials[] =
    {
        { RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 0) },
        /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSsePcmpistri); iFn++)
    {
        PFNIEMAIMPLPCMPISTRIU128IMM8 const pfn = g_aSsePcmpistri[iFn].pfnNative ? g_aSsePcmpistri[iFn].pfnNative : g_aSsePcmpistri[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSsePcmpistri[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_PCMPISTRI_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal1.uXmm = iTest < cTests ? RandU128() : s_aSpecials[iTest - cTests].uSrc1;
            TestData.InVal2.uXmm = iTest < cTests ? RandU128() : s_aSpecials[iTest - cTests].uSrc2;

            uint32_t const fEFlagsIn = RandEFlags();
            for (uint16_t u16Imm = 0; u16Imm < 256; u16Imm++)
            {
                uint32_t fEFlagsOut = fEFlagsIn;
                TestData.u32EcxOut  = pfn(&fEFlagsOut, &TestData.InVal1.uXmm,  &TestData.InVal2.uXmm, (uint8_t)u16Imm);
                TestData.fEFlagsIn  = fEFlagsIn;
                TestData.fEFlagsOut = fEFlagsOut;
                TestData.bImm       = (uint8_t)u16Imm;
                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
            }

            /* Repeat the test with the input value being the same. */
            for (uint16_t u16Imm = 0; u16Imm < 256; u16Imm++)
            {
                uint32_t fEFlagsOut = fEFlagsIn;
                TestData.u32EcxOut  = pfn(&fEFlagsOut, &TestData.InVal1.uXmm,  &TestData.InVal2.uXmm, (uint8_t)u16Imm);
                TestData.fEFlagsIn  = fEFlagsIn;
                TestData.fEFlagsOut = fEFlagsOut;
                TestData.bImm       = (uint8_t)u16Imm;
                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
            }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseComparePcmpistriTest(void)
{
    X86FXSTATE State;
    RT_ZERO(State);

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSsePcmpistri); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSsePcmpistri[iFn]))
            continue;

        SSE_PCMPISTRI_TEST_T const * const      paTests = g_aSsePcmpistri[iFn].paTests;
        uint32_t const                          cTests  = g_aSsePcmpistri[iFn].cTests;
        PFNIEMAIMPLPCMPISTRIU128IMM8            pfn     = g_aSsePcmpistri[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSsePcmpistri[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                uint32_t fEFlags = paTests[iTest].fEFlagsIn;
                uint32_t u32EcxOut = pfn(&fEFlags, &paTests[iTest].InVal1.uXmm, &paTests[iTest].InVal2.uXmm, paTests[iTest].bImm);
                if (   fEFlags != paTests[iTest].fEFlagsOut
                    || u32EcxOut != paTests[iTest].u32EcxOut)
                    RTTestFailed(g_hTest, "#%04u%s: efl=%#08x in1=%s in2=%s bImm=%#x\n"
                                          "%s                 -> efl=%#08x    %RU32\n"
                                          "%s               expected %#08x    %RU32%s%s\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fEFlagsIn,
                                 FormatU128(&paTests[iTest].InVal1.uXmm), FormatU128(&paTests[iTest].InVal2.uXmm), paTests[iTest].bImm,
                                 iVar ? "  " : "", fEFlags, u32EcxOut,
                                 iVar ? "  " : "", paTests[iTest].fEFlagsOut, paTests[iTest].u32EcxOut,
                                 EFlagsDiff(fEFlags, paTests[iTest].fEFlagsOut),
                                 (u32EcxOut != paTests[iTest].u32EcxOut) ? " - val" : "");
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSsePcmpistri[iFn]);
    }
}


TYPEDEF_SUBTEST_TYPE(SSE_PCMPISTRM_T, SSE_PCMPISTRM_TEST_T, PFNIEMAIMPLPCMPISTRMU128IMM8);

static SSE_PCMPISTRM_T g_aSsePcmpistrm[] =
{
    ENTRY_BIN_SSE_OPT(pcmpistrm_u128),
    ENTRY_BIN_SSE_OPT(vpcmpistrm_u128),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseComparePcmpistrm, g_aSsePcmpistrm)
static RTEXITCODE SseComparePcmpistrmGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTUINT128U uSrc1; RTUINT128U uSrc2; } const s_aSpecials[] =
    {
        { RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 0) },
        /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSsePcmpistrm); iFn++)
    {
        PFNIEMAIMPLPCMPISTRMU128IMM8 const pfn = g_aSsePcmpistrm[iFn].pfnNative ? g_aSsePcmpistrm[iFn].pfnNative : g_aSsePcmpistrm[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSsePcmpistrm[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_PCMPISTRM_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal1.uXmm = iTest < cTests ? RandU128() : s_aSpecials[iTest - cTests].uSrc1;
            TestData.InVal2.uXmm = iTest < cTests ? RandU128() : s_aSpecials[iTest - cTests].uSrc2;

            IEMPCMPISTRXSRC TestVal;
            TestVal.uSrc1 = TestData.InVal1.uXmm;
            TestVal.uSrc2 = TestData.InVal2.uXmm;

            uint32_t const fEFlagsIn = RandEFlags();
            for (uint16_t u16Imm = 0; u16Imm < 256; u16Imm++)
            {
                uint32_t fEFlagsOut = fEFlagsIn;
                pfn(&TestData.OutVal.uXmm, &fEFlagsOut, &TestVal, (uint8_t)u16Imm);
                TestData.fEFlagsIn  = fEFlagsIn;
                TestData.fEFlagsOut = fEFlagsOut;
                TestData.bImm       = (uint8_t)u16Imm;
                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
            }

            /* Repeat the test with the input value being the same. */
            TestData.InVal2.uXmm = TestData.InVal1.uXmm;
            TestVal.uSrc1 = TestData.InVal1.uXmm;
            TestVal.uSrc2 = TestData.InVal2.uXmm;

            for (uint16_t u16Imm = 0; u16Imm < 256; u16Imm++)
            {
                uint32_t fEFlagsOut = fEFlagsIn;
                pfn(&TestData.OutVal.uXmm, &fEFlagsOut, &TestVal, (uint8_t)u16Imm);
                TestData.fEFlagsIn  = fEFlagsIn;
                TestData.fEFlagsOut = fEFlagsOut;
                TestData.bImm       = (uint8_t)u16Imm;
                GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
            }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseComparePcmpistrmTest(void)
{
    X86FXSTATE State;
    RT_ZERO(State);

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSsePcmpistrm); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSsePcmpistrm[iFn]))
            continue;

        SSE_PCMPISTRM_TEST_T const * const      paTests = g_aSsePcmpistrm[iFn].paTests;
        uint32_t const                          cTests  = g_aSsePcmpistrm[iFn].cTests;
        PFNIEMAIMPLPCMPISTRMU128IMM8            pfn     = g_aSsePcmpistrm[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSsePcmpistrm[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                IEMPCMPISTRXSRC TestVal;
                TestVal.uSrc1 = paTests[iTest].InVal1.uXmm;
                TestVal.uSrc2 = paTests[iTest].InVal2.uXmm;

                uint32_t fEFlags = paTests[iTest].fEFlagsIn;
                RTUINT128U OutVal;
                pfn(&OutVal, &fEFlags, &TestVal, paTests[iTest].bImm);
                if (   fEFlags != paTests[iTest].fEFlagsOut
                    || OutVal.s.Hi != paTests[iTest].OutVal.uXmm.s.Hi
                    || OutVal.s.Lo != paTests[iTest].OutVal.uXmm.s.Lo)
                    RTTestFailed(g_hTest, "#%04u%s: efl=%#08x in1=%s in2=%s bImm=%#x\n"
                                          "%s                 -> efl=%#08x    %s\n"
                                          "%s               expected %#08x    %s%s%s\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fEFlagsIn,
                                 FormatU128(&paTests[iTest].InVal1.uXmm), FormatU128(&paTests[iTest].InVal2.uXmm), paTests[iTest].bImm,
                                 iVar ? "  " : "", fEFlags, FormatU128(&OutVal),
                                 iVar ? "  " : "", paTests[iTest].fEFlagsOut, FormatU128(&paTests[iTest].OutVal.uXmm),
                                 EFlagsDiff(fEFlags, paTests[iTest].fEFlagsOut),
                                 (   OutVal.s.Hi != paTests[iTest].OutVal.uXmm.s.Hi
                                  || OutVal.s.Lo != paTests[iTest].OutVal.uXmm.s.Lo) ? " - val" : "");
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSsePcmpistrm[iFn]);
    }
}


TYPEDEF_SUBTEST_TYPE(SSE_PCMPESTRI_T, SSE_PCMPESTRI_TEST_T, PFNIEMAIMPLPCMPESTRIU128IMM8);

static SSE_PCMPESTRI_T g_aSsePcmpestri[] =
{
    ENTRY_BIN_SSE_OPT(pcmpestri_u128),
    ENTRY_BIN_SSE_OPT(vpcmpestri_u128),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseComparePcmpestri, g_aSsePcmpestri)
static RTEXITCODE SseComparePcmpestriGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTUINT128U uSrc1; RTUINT128U uSrc2; } const s_aSpecials[] =
    {
        { RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 0) },
        /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSsePcmpestri); iFn++)
    {
        PFNIEMAIMPLPCMPESTRIU128IMM8 const pfn = g_aSsePcmpestri[iFn].pfnNative ? g_aSsePcmpestri[iFn].pfnNative : g_aSsePcmpestri[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSsePcmpestri[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_PCMPESTRI_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal1.uXmm = iTest < cTests ? RandU128() : s_aSpecials[iTest - cTests].uSrc1;
            TestData.InVal2.uXmm = iTest < cTests ? RandU128() : s_aSpecials[iTest - cTests].uSrc2;

            for (int64_t i64Rax = -20; i64Rax < 20; i64Rax += 20)
                for (int64_t i64Rdx = -20; i64Rdx < 20; i64Rdx += 20)
                {
                    TestData.u64Rax = (uint64_t)i64Rax;
                    TestData.u64Rdx = (uint64_t)i64Rdx;

                    IEMPCMPESTRXSRC TestVal;
                    TestVal.uSrc1  = TestData.InVal1.uXmm;
                    TestVal.uSrc2  = TestData.InVal2.uXmm;
                    TestVal.u64Rax = TestData.u64Rax;
                    TestVal.u64Rdx = TestData.u64Rdx;

                    uint32_t const fEFlagsIn = RandEFlags();
                    for (uint16_t u16Imm = 0; u16Imm < 256; u16Imm++)
                    {
                        uint32_t fEFlagsOut = fEFlagsIn;
                        pfn(&TestData.u32EcxOut, &fEFlagsOut, &TestVal, (uint8_t)u16Imm);
                        TestData.fEFlagsIn  = fEFlagsIn;
                        TestData.fEFlagsOut = fEFlagsOut;
                        TestData.bImm       = (uint8_t)u16Imm;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                    }

                    /* Repeat the test with the input value being the same. */
                    TestData.InVal2.uXmm = TestData.InVal1.uXmm;
                    TestVal.uSrc1 = TestData.InVal1.uXmm;
                    TestVal.uSrc2 = TestData.InVal2.uXmm;

                    for (uint16_t u16Imm = 0; u16Imm < 256; u16Imm++)
                    {
                        uint32_t fEFlagsOut = fEFlagsIn;
                        pfn(&TestData.u32EcxOut, &fEFlagsOut, &TestVal, (uint8_t)u16Imm);
                        TestData.fEFlagsIn  = fEFlagsIn;
                        TestData.fEFlagsOut = fEFlagsOut;
                        TestData.bImm       = (uint8_t)u16Imm;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                    }
                }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseComparePcmpestriTest(void)
{
    X86FXSTATE State;
    RT_ZERO(State);

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSsePcmpestri); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSsePcmpestri[iFn]))
            continue;

        SSE_PCMPESTRI_TEST_T const * const      paTests = g_aSsePcmpestri[iFn].paTests;
        uint32_t const                          cTests  = g_aSsePcmpestri[iFn].cTests;
        PFNIEMAIMPLPCMPESTRIU128IMM8            pfn     = g_aSsePcmpestri[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSsePcmpestri[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                IEMPCMPESTRXSRC TestVal;
                TestVal.uSrc1  = paTests[iTest].InVal1.uXmm;
                TestVal.uSrc2  = paTests[iTest].InVal2.uXmm;
                TestVal.u64Rax = paTests[iTest].u64Rax;
                TestVal.u64Rdx = paTests[iTest].u64Rdx;

                uint32_t fEFlags = paTests[iTest].fEFlagsIn;
                uint32_t u32EcxOut = 0;
                pfn(&u32EcxOut, &fEFlags, &TestVal, paTests[iTest].bImm);
                if (   fEFlags != paTests[iTest].fEFlagsOut
                    || u32EcxOut != paTests[iTest].u32EcxOut)
                    RTTestFailed(g_hTest, "#%04u%s: efl=%#08x in1=%s rax1=%RI64 in2=%s rdx2=%RI64 bImm=%#x\n"
                                          "%s                 -> efl=%#08x    %RU32\n"
                                          "%s               expected %#08x    %RU32%s%s\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fEFlagsIn,
                                 FormatU128(&paTests[iTest].InVal1.uXmm), paTests[iTest].u64Rax,
                                 FormatU128(&paTests[iTest].InVal2.uXmm), paTests[iTest].u64Rdx,
                                 paTests[iTest].bImm,
                                 iVar ? "  " : "", fEFlags, u32EcxOut,
                                 iVar ? "  " : "", paTests[iTest].fEFlagsOut, paTests[iTest].u32EcxOut,
                                 EFlagsDiff(fEFlags, paTests[iTest].fEFlagsOut),
                                 (u32EcxOut != paTests[iTest].u32EcxOut) ? " - val" : "");
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSsePcmpestri[iFn]);
    }
}


TYPEDEF_SUBTEST_TYPE(SSE_PCMPESTRM_T, SSE_PCMPESTRM_TEST_T, PFNIEMAIMPLPCMPESTRMU128IMM8);

static SSE_PCMPESTRM_T g_aSsePcmpestrm[] =
{
    ENTRY_BIN_SSE_OPT(pcmpestrm_u128),
    ENTRY_BIN_SSE_OPT(vpcmpestrm_u128),
};

#ifdef TSTIEMAIMPL_WITH_GENERATOR
DUMP_ALL_FN(SseComparePcmpestrm, g_aSsePcmpestrm)
static RTEXITCODE SseComparePcmpestrmGenerate(uint32_t cTests, const char * const *papszNameFmts)
{
    cTests = RT_MAX(192, cTests); /* there are 144 standard input variations */

    static struct { RTUINT128U uSrc1; RTUINT128U uSrc2; } const s_aSpecials[] =
    {
        { RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 0) },
        /** @todo More specials. */
    };

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSsePcmpestrm); iFn++)
    {
        PFNIEMAIMPLPCMPESTRMU128IMM8 const pfn = g_aSsePcmpestrm[iFn].pfnNative ? g_aSsePcmpestrm[iFn].pfnNative : g_aSsePcmpestrm[iFn].pfn;

        IEMBINARYOUTPUT BinOut;
        AssertReturn(GENERATE_BINARY_OPEN(&BinOut, papszNameFmts, g_aSsePcmpestrm[iFn]), RTEXITCODE_FAILURE);

        for (uint32_t iTest = 0; iTest < cTests + RT_ELEMENTS(s_aSpecials); iTest += 1)
        {
            SSE_PCMPESTRM_TEST_T TestData; RT_ZERO(TestData);

            TestData.InVal1.uXmm = iTest < cTests ? RandU128() : s_aSpecials[iTest - cTests].uSrc1;
            TestData.InVal2.uXmm = iTest < cTests ? RandU128() : s_aSpecials[iTest - cTests].uSrc2;

            for (int64_t i64Rax = -20; i64Rax < 20; i64Rax += 20)
                for (int64_t i64Rdx = -20; i64Rdx < 20; i64Rdx += 20)
                {
                    TestData.u64Rax = (uint64_t)i64Rax;
                    TestData.u64Rdx = (uint64_t)i64Rdx;

                    IEMPCMPESTRXSRC TestVal;
                    TestVal.uSrc1  = TestData.InVal1.uXmm;
                    TestVal.uSrc2  = TestData.InVal2.uXmm;
                    TestVal.u64Rax = TestData.u64Rax;
                    TestVal.u64Rdx = TestData.u64Rdx;

                    uint32_t const fEFlagsIn = RandEFlags();
                    for (uint16_t u16Imm = 0; u16Imm < 256; u16Imm++)
                    {
                        uint32_t fEFlagsOut = fEFlagsIn;
                        pfn(&TestData.OutVal.uXmm, &fEFlagsOut, &TestVal, (uint8_t)u16Imm);
                        TestData.fEFlagsIn  = fEFlagsIn;
                        TestData.fEFlagsOut = fEFlagsOut;
                        TestData.bImm       = (uint8_t)u16Imm;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                    }

                    /* Repeat the test with the input value being the same. */
                    TestData.InVal2.uXmm = TestData.InVal1.uXmm;
                    TestVal.uSrc1 = TestData.InVal1.uXmm;
                    TestVal.uSrc2 = TestData.InVal2.uXmm;

                    for (uint16_t u16Imm = 0; u16Imm < 256; u16Imm++)
                    {
                        uint32_t fEFlagsOut = fEFlagsIn;
                        pfn(&TestData.OutVal.uXmm, &fEFlagsOut, &TestVal, (uint8_t)u16Imm);
                        TestData.fEFlagsIn  = fEFlagsIn;
                        TestData.fEFlagsOut = fEFlagsOut;
                        TestData.bImm       = (uint8_t)u16Imm;
                        GenerateBinaryWrite(&BinOut, &TestData, sizeof(TestData));
                    }
                }
        }
        AssertReturn(GenerateBinaryClose(&BinOut), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}
#endif

static void SseComparePcmpestrmTest(void)
{
    X86FXSTATE State;
    RT_ZERO(State);

    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aSsePcmpestrm); iFn++)
    {
        if (!SUBTEST_CHECK_IF_ENABLED_AND_DECOMPRESS(g_aSsePcmpestrm[iFn]))
            continue;

        SSE_PCMPESTRM_TEST_T const * const      paTests = g_aSsePcmpestrm[iFn].paTests;
        uint32_t const                          cTests  = g_aSsePcmpestrm[iFn].cTests;
        PFNIEMAIMPLPCMPESTRMU128IMM8            pfn     = g_aSsePcmpestrm[iFn].pfn;
        uint32_t const                          cVars   = COUNT_VARIATIONS(g_aSsePcmpestrm[iFn]);
        if (!cTests) RTTestSkipped(g_hTest, "no tests");
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            for (uint32_t iTest = 0; iTest < cTests; iTest++)
            {
                IEMPCMPESTRXSRC TestVal;
                TestVal.uSrc1  = paTests[iTest].InVal1.uXmm;
                TestVal.uSrc2  = paTests[iTest].InVal2.uXmm;
                TestVal.u64Rax = paTests[iTest].u64Rax;
                TestVal.u64Rdx = paTests[iTest].u64Rdx;

                uint32_t fEFlags = paTests[iTest].fEFlagsIn;
                RTUINT128U OutVal;
                pfn(&OutVal, &fEFlags, &TestVal, paTests[iTest].bImm);
                if (   fEFlags != paTests[iTest].fEFlagsOut
                    || OutVal.s.Hi != paTests[iTest].OutVal.uXmm.s.Hi
                    || OutVal.s.Lo != paTests[iTest].OutVal.uXmm.s.Lo)
                    RTTestFailed(g_hTest, "#%04u%s: efl=%#08x in1=%s rax1=%RI64 in2=%s rdx2=%RI64 bImm=%#x\n"
                                          "%s                 -> efl=%#08x    %s\n"
                                          "%s               expected %#08x    %s%s%s\n",
                                 iTest, iVar ? "/n" : "", paTests[iTest].fEFlagsIn,
                                 FormatU128(&paTests[iTest].InVal1.uXmm), paTests[iTest].u64Rax,
                                 FormatU128(&paTests[iTest].InVal2.uXmm), paTests[iTest].u64Rdx,
                                 paTests[iTest].bImm,
                                 iVar ? "  " : "", fEFlags, FormatU128(&OutVal),
                                 iVar ? "  " : "", paTests[iTest].fEFlagsOut, FormatU128(&paTests[iTest].OutVal.uXmm),
                                 EFlagsDiff(fEFlags, paTests[iTest].fEFlagsOut),
                                 (   OutVal.s.Hi != paTests[iTest].OutVal.uXmm.s.Hi
                                  || OutVal.s.Lo != paTests[iTest].OutVal.uXmm.s.Lo) ? " - val" : "");
            }
        }

        FREE_DECOMPRESSED_TESTS(g_aSsePcmpestrm[iFn]);
    }
}



int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Determin the host CPU.
     * If not using the IEMAllAImpl.asm code, this will be set to Intel.
     */
#if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
    g_idxCpuEflFlavour = ASMIsAmdCpu() || ASMIsHygonCpu()
                       ? IEMTARGETCPU_EFL_BEHAVIOR_AMD
                       : IEMTARGETCPU_EFL_BEHAVIOR_INTEL;
#else
    g_idxCpuEflFlavour = IEMTARGETCPU_EFL_BEHAVIOR_INTEL;
#endif

    /*
     * Parse arguments.
     */
    enum { kModeNotSet, kModeTest, kModeGenerate, kModeDump }
                        enmMode       = kModeNotSet;
#define CATEGORY_INT            RT_BIT_32(0)
#define CATEGORY_FPU_LD_ST      RT_BIT_32(1)
#define CATEGORY_FPU_BINARY_1   RT_BIT_32(2)
#define CATEGORY_FPU_BINARY_2   RT_BIT_32(3)
#define CATEGORY_FPU_OTHER      RT_BIT_32(4)
#define CATEGORY_SSE_FP_BINARY  RT_BIT_32(5)
#define CATEGORY_SSE_FP_OTHER   RT_BIT_32(6)
#define CATEGORY_SSE_PCMPXSTRX  RT_BIT_32(7)
    uint32_t            fCategories   = UINT32_MAX;
    bool                fCpuData      = true;
    bool                fCommonData   = true;
    uint32_t const      cDefaultTests = 96;
    uint32_t            cTests        = cDefaultTests;

    RTGETOPTDEF const   s_aOptions[]  =
    {
        // mode:
        { "--generate",             'g', RTGETOPT_REQ_NOTHING },
        { "--dump",                 'G', RTGETOPT_REQ_NOTHING },
        { "--test",                 't', RTGETOPT_REQ_NOTHING },
        { "--benchmark",            'b', RTGETOPT_REQ_NOTHING },
        // test selection (both)
        { "--all",                  'a', RTGETOPT_REQ_NOTHING },
        { "--none",                 'z', RTGETOPT_REQ_NOTHING },
        { "--zap",                  'z', RTGETOPT_REQ_NOTHING },
        { "--fpu-ld-st",            'F', RTGETOPT_REQ_NOTHING },  /* FPU stuff is upper case */
        { "--fpu-load-store",       'F', RTGETOPT_REQ_NOTHING },
        { "--fpu-binary-1",         'B', RTGETOPT_REQ_NOTHING },
        { "--fpu-binary-2",         'P', RTGETOPT_REQ_NOTHING },
        { "--fpu-other",            'O', RTGETOPT_REQ_NOTHING },
        { "--sse-fp-binary",        'S', RTGETOPT_REQ_NOTHING },
        { "--sse-fp-other",         'T', RTGETOPT_REQ_NOTHING },
        { "--sse-pcmpxstrx",        'C', RTGETOPT_REQ_NOTHING },
        { "--int",                  'i', RTGETOPT_REQ_NOTHING },
        { "--include",              'I', RTGETOPT_REQ_STRING },
        { "--exclude",              'X', RTGETOPT_REQ_STRING },
        // generation parameters
        { "--common",               'm', RTGETOPT_REQ_NOTHING },
        { "--cpu",                  'c', RTGETOPT_REQ_NOTHING },
        { "--number-of-tests",      'n', RTGETOPT_REQ_UINT32  },
        { "--verbose",              'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",                'q', RTGETOPT_REQ_NOTHING },
        { "--quiet-skipping",       'Q', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE State;
    rc = RTGetOptInit(&State, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);

    RTGETOPTUNION ValueUnion;
    while ((rc = RTGetOpt(&State, &ValueUnion)))
    {
        switch (rc)
        {
            case 'g':
                enmMode                 = kModeGenerate;
                g_cPicoSecBenchmark     = 0;
                break;
            case 'G':
                enmMode                 = kModeDump;
                g_cPicoSecBenchmark     = 0;
                break;
            case 't':
                enmMode                 = kModeTest;
                g_cPicoSecBenchmark     = 0;
                break;
            case 'b':
                enmMode                 = kModeTest;
                g_cPicoSecBenchmark    += RT_NS_1SEC / 2 * UINT64_C(1000); /* half a second in pico seconds */
                break;

            case 'a':
                fCpuData    = true;
                fCommonData = true;
                fCategories = UINT32_MAX;
                break;
            case 'z':
                fCpuData    = false;
                fCommonData = false;
                fCategories = 0;
                break;

            case 'F':
                fCategories |= CATEGORY_FPU_LD_ST;
                break;
            case 'O':
                fCategories |= CATEGORY_FPU_OTHER;
                break;
            case 'B':
                fCategories |= CATEGORY_FPU_BINARY_1;
                break;
            case 'P':
                fCategories |= CATEGORY_FPU_BINARY_2;
                break;
            case 'S':
                fCategories |= CATEGORY_SSE_FP_BINARY;
                break;
            case 'T':
                fCategories |= CATEGORY_SSE_FP_OTHER;
                break;
            case 'C':
                fCategories |= CATEGORY_SSE_PCMPXSTRX;
                break;
            case 'i':
                fCategories |= CATEGORY_INT;
                break;

            case 'I':
                if (g_cIncludeTestPatterns >= RT_ELEMENTS(g_apszIncludeTestPatterns))
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many include patterns (max %zu)",
                                          RT_ELEMENTS(g_apszIncludeTestPatterns));
                g_apszIncludeTestPatterns[g_cIncludeTestPatterns++] = ValueUnion.psz;
                break;
            case 'X':
                if (g_cExcludeTestPatterns >= RT_ELEMENTS(g_apszExcludeTestPatterns))
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many exclude patterns (max %zu)",
                                          RT_ELEMENTS(g_apszExcludeTestPatterns));
                g_apszExcludeTestPatterns[g_cExcludeTestPatterns++] = ValueUnion.psz;
                break;

            case 'm':
                fCommonData = true;
                break;
            case 'c':
                fCpuData    = true;
                break;
            case 'n':
                cTests      = ValueUnion.u32;
                break;

            case 'q':
                g_cVerbosity = 0;
                break;
            case 'v':
                g_cVerbosity++;
                break;
            case 'Q':
                g_fVerboseSkipping = false;
                break;

            case 'h':
                RTPrintf("usage: %Rbn <-g|-t> [options]\n"
                         "\n"
                         "Mode:\n"
                         "  -g, --generate\n"
                         "    Generate test data.\n"
                         "  -t, --test\n"
                         "    Execute tests.\n"
                         "  -b, --benchmark\n"
                         "    Execute tests and do 1/2 seconds of benchmarking.\n"
                         "    Repeating the option increases the benchmark duration by 0.5 seconds.\n"
                         "\n"
                         "Test selection (both modes):\n"
                         "  -a, --all\n"
                         "    Enable all tests and generated test data. (default)\n"
                         "  -z, --zap, --none\n"
                         "    Disable all tests and test data types.\n"
                         "  -i, --int\n"
                         "    Enable non-FPU tests.\n"
                         "  -F, --fpu-ld-st\n"
                         "    Enable FPU load and store tests.\n"
                         "  -B, --fpu-binary-1\n"
                         "    Enable FPU binary 80-bit FP tests.\n"
                         "  -P, --fpu-binary-2\n"
                         "    Enable FPU binary 64- and 32-bit FP tests.\n"
                         "  -O, --fpu-other\n"
                         "    Enable FPU binary 64- and 32-bit FP tests.\n"
                         "  -S, --sse-fp-binary\n"
                         "    Enable SSE binary 64- and 32-bit FP tests.\n"
                         "  -T, --sse-fp-other\n"
                         "    Enable misc SSE 64- and 32-bit FP tests.\n"
                         "  -C, --sse-pcmpxstrx\n"
                         "    Enable SSE pcmpxstrx tests.\n"
                         "  -I,--include=<test-patter>\n"
                         "    Enable tests matching the given pattern.\n"
                         "  -X,--exclude=<test-patter>\n"
                         "    Skip tests matching the given pattern (overrides --include).\n"
                         "\n"
                         "Generation:\n"
                         "  -m, --common\n"
                         "    Enable generating common test data.\n"
                         "  -c, --only-cpu\n"
                         "    Enable generating CPU specific test data.\n"
                         "  -n, --number-of-test <count>\n"
                         "    Number of tests to generate. Default: %u\n"
                         "\n"
                         "Other:\n"
                         "  -v, --verbose\n"
                         "  -q, --quiet\n"
                         "    Noise level.  Default: --quiet\n"
                         "  -Q, --quiet-skipping\n"
                         "    Don't display skipped tests.\n"
                         "\n"
                         "Tip! When working on a single instruction, use the the -I and -Q options to\n"
                         "     restrict the testing: %Rbn -tiQI \"shr_*\"\n"
                         , argv[0], cDefaultTests, argv[0]);
                return RTEXITCODE_SUCCESS;
            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    static const struct
    {
        uint32_t        fCategory;
        void          (*pfnTest)(void);
#ifdef TSTIEMAIMPL_WITH_GENERATOR
        const char     *pszFilenameFmt;
        RTEXITCODE    (*pfnGenerate)(uint32_t cTests, const char * const *papszNameFmts);
        RTEXITCODE    (*pfnDumpAll)(const char * const *papszNameFmts);
        uint32_t        cMinTests;
# define GROUP_ENTRY(a_fCategory, a_BaseNm, a_szFilenameFmt, a_cMinTests) \
            { a_fCategory, a_BaseNm ## Test, a_szFilenameFmt, a_BaseNm ## Generate, a_BaseNm ## DumpAll, a_cMinTests }
#else
# define GROUP_ENTRY(a_fCategory, a_BaseNm, a_szFilenameFmt, a_cMinTests) \
            { a_fCategory, a_BaseNm ## Test }
#endif
#define GROUP_ENTRY_MANUAL(a_fCategory, a_BaseNm) \
            { a_fCategory, a_BaseNm ## Test }
    } s_aGroups[] =
    {
        GROUP_ENTRY(CATEGORY_INT,           BinU8,                  "tstIEMAImplDataInt-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_INT,           BinU16,                 "tstIEMAImplDataInt-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_INT,           BinU32,                 "tstIEMAImplDataInt-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_INT,           BinU64,                 "tstIEMAImplDataInt-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_INT,           ShiftDbl,               "tstIEMAImplDataInt-%s.bin.gz", 128),
        GROUP_ENTRY(CATEGORY_INT,           Unary,                  "tstIEMAImplDataInt-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_INT,           Shift,                  "tstIEMAImplDataInt-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_INT,           MulDiv,                 "tstIEMAImplDataInt-%s.bin.gz", 0),
        GROUP_ENTRY_MANUAL(CATEGORY_INT,    Xchg),
        GROUP_ENTRY_MANUAL(CATEGORY_INT,    Xadd),
        GROUP_ENTRY_MANUAL(CATEGORY_INT,    CmpXchg),
        GROUP_ENTRY_MANUAL(CATEGORY_INT,    CmpXchg8b),
        GROUP_ENTRY_MANUAL(CATEGORY_INT,    CmpXchg16b),
        GROUP_ENTRY_MANUAL(CATEGORY_INT,    Bswap),

        GROUP_ENTRY(CATEGORY_FPU_LD_ST,     FpuLdConst,             "tstIEMAImplDataFpuLdSt-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_LD_ST,     FpuLdInt,               "tstIEMAImplDataFpuLdSt-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_LD_ST,     FpuLdD80,               "tstIEMAImplDataFpuLdSt-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_LD_ST,     FpuLdMem,               "tstIEMAImplDataFpuLdSt-%s.bin.gz", 384), /* needs better coverage */

        GROUP_ENTRY(CATEGORY_FPU_LD_ST,     FpuStInt,               "tstIEMAImplDataFpuLdSt-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_LD_ST,     FpuStD80,               "tstIEMAImplDataFpuLdSt-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_LD_ST,     FpuStMem,               "tstIEMAImplDataFpuLdSt-%s.bin.gz", 384), /* needs better coverage */

        GROUP_ENTRY(CATEGORY_FPU_BINARY_1,  FpuBinaryR80,           "tstIEMAImplDataFpuBinary1-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_BINARY_1,  FpuBinaryFswR80,        "tstIEMAImplDataFpuBinary1-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_BINARY_1,  FpuBinaryEflR80,        "tstIEMAImplDataFpuBinary1-%s.bin.gz", 0),

        GROUP_ENTRY(CATEGORY_FPU_BINARY_2,  FpuBinaryR64,           "tstIEMAImplDataFpuBinary2-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_BINARY_2,  FpuBinaryR32,           "tstIEMAImplDataFpuBinary2-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_BINARY_2,  FpuBinaryI32,           "tstIEMAImplDataFpuBinary2-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_BINARY_2,  FpuBinaryI16,           "tstIEMAImplDataFpuBinary2-%s.bin.gz", 0),

        GROUP_ENTRY(CATEGORY_FPU_BINARY_2,  FpuBinaryFswR64,        "tstIEMAImplDataFpuBinary2-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_BINARY_2,  FpuBinaryFswR32,        "tstIEMAImplDataFpuBinary2-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_BINARY_2,  FpuBinaryFswI32,        "tstIEMAImplDataFpuBinary2-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_BINARY_2,  FpuBinaryFswI16,        "tstIEMAImplDataFpuBinary2-%s.bin.gz", 0),

        GROUP_ENTRY(CATEGORY_FPU_OTHER,     FpuUnaryR80,            "tstIEMAImplDataFpuOther-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_OTHER,     FpuUnaryFswR80,         "tstIEMAImplDataFpuOther-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_FPU_OTHER,     FpuUnaryTwoR80,         "tstIEMAImplDataFpuOther-%s.bin.gz", 0),

        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryR32,           "tstIEMAImplDataSseBinary-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryR64,           "tstIEMAImplDataSseBinary-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryU128R32,       "tstIEMAImplDataSseBinary-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryU128R64,       "tstIEMAImplDataSseBinary-%s.bin.gz", 0),

        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryI32R64,        "tstIEMAImplDataSseBinary-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryI64R64,        "tstIEMAImplDataSseBinary-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryI32R32,        "tstIEMAImplDataSseBinary-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryI64R32,        "tstIEMAImplDataSseBinary-%s.bin.gz", 0),

        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryR64I32,        "tstIEMAImplDataSseBinary-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryR64I64,        "tstIEMAImplDataSseBinary-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryR32I32,        "tstIEMAImplDataSseBinary-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_BINARY, SseBinaryR32I64,        "tstIEMAImplDataSseBinary-%s.bin.gz", 0),

        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseCompareEflR32R32,    "tstIEMAImplDataSseCompare-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseCompareEflR64R64,    "tstIEMAImplDataSseCompare-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseCompareF3XmmR32Imm8, "tstIEMAImplDataSseCompare-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseCompareF3XmmR64Imm8, "tstIEMAImplDataSseCompare-%s.bin.gz", 0),

        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseConvertXmmI32R32,    "tstIEMAImplDataSseConvert-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseConvertXmmR32I32,    "tstIEMAImplDataSseConvert-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseConvertXmmI32R64,    "tstIEMAImplDataSseConvert-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseConvertXmmR64I32,    "tstIEMAImplDataSseConvert-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseConvertMmXmm,        "tstIEMAImplDataSseConvert-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseConvertXmmR32Mm,     "tstIEMAImplDataSseConvert-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseConvertXmmR64Mm,     "tstIEMAImplDataSseConvert-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_FP_OTHER,  SseConvertMmI32XmmR32,  "tstIEMAImplDataSseConvert-%s.bin.gz", 0),

        GROUP_ENTRY(CATEGORY_SSE_PCMPXSTRX, SseComparePcmpistri,    "tstIEMAImplDataSsePcmpxstrx-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_PCMPXSTRX, SseComparePcmpistrm,    "tstIEMAImplDataSsePcmpxstrx-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_PCMPXSTRX, SseComparePcmpestri,    "tstIEMAImplDataSsePcmpxstrx-%s.bin.gz", 0),
        GROUP_ENTRY(CATEGORY_SSE_PCMPXSTRX, SseComparePcmpestrm,    "tstIEMAImplDataSsePcmpxstrx-%s.bin.gz", 0),
    };

    /*
     * Generate data?
     */
    if (enmMode == kModeGenerate)
    {
#ifdef TSTIEMAIMPL_WITH_GENERATOR
        if (cTests == 0)
            cTests = cDefaultTests;
        g_cZeroDstTests = RT_MIN(cTests / 16, 32);
        g_cZeroSrcTests = g_cZeroDstTests * 2;

        RTMpGetDescription(NIL_RTCPUID, g_szCpuDesc, sizeof(g_szCpuDesc));

        /* For the revision, use the highest for this file and VBoxRT. */
        static const char s_szRev[] = "$Revision: 106320 $";
        const char *pszRev = s_szRev;
        while (*pszRev && !RT_C_IS_DIGIT(*pszRev))
            pszRev++;
        g_uSvnRev = RTStrToUInt32(pszRev);
        g_uSvnRev = RT_MAX(g_uSvnRev, RTBldCfgRevision());

        /* Loop thru the groups and call the generate for any that's enabled. */
        for (size_t i = 0; i < RT_ELEMENTS(s_aGroups); i++)
            if ((s_aGroups[i].fCategory & fCategories) && s_aGroups[i].pfnGenerate)
            {
                const char * const apszNameFmts[] =
                {
                    /*[IEMTARGETCPU_EFL_BEHAVIOR_NATIVE] =*/ fCommonData ? s_aGroups[i].pszFilenameFmt : NULL,
                    /*[IEMTARGETCPU_EFL_BEHAVIOR_INTEL]  =*/ fCpuData    ? s_aGroups[i].pszFilenameFmt : NULL,
                    /*[IEMTARGETCPU_EFL_BEHAVIOR_AMD]    =*/ fCpuData    ? s_aGroups[i].pszFilenameFmt : NULL,
                };
                RTEXITCODE rcExit = s_aGroups[i].pfnGenerate(RT_MAX(cTests, s_aGroups[i].cMinTests), apszNameFmts);
                if (rcExit != RTEXITCODE_SUCCESS)
                    return rcExit;
            }
        return RTEXITCODE_SUCCESS;
#else
        return RTMsgErrorExitFailure("Test data generator not compiled in!");
#endif
    }

    /*
     * Dump tables (used for the conversion, mostly useless now).
     */
    if (enmMode == kModeDump)
    {
#ifdef TSTIEMAIMPL_WITH_GENERATOR
        /* Loop thru the groups and call the generate for any that's enabled. */
        for (size_t i = 0; i < RT_ELEMENTS(s_aGroups); i++)
            if ((s_aGroups[i].fCategory & fCategories) && s_aGroups[i].pfnDumpAll)
            {
                const char * const apszNameFmts[] =
                {
                    /*[IEMTARGETCPU_EFL_BEHAVIOR_NATIVE] =*/ fCommonData ? s_aGroups[i].pszFilenameFmt : NULL,
                    /*[IEMTARGETCPU_EFL_BEHAVIOR_INTEL]  =*/ fCpuData    ? s_aGroups[i].pszFilenameFmt : NULL,
                    /*[IEMTARGETCPU_EFL_BEHAVIOR_AMD]    =*/ fCpuData    ? s_aGroups[i].pszFilenameFmt : NULL,
                };
                RTEXITCODE rcExit = s_aGroups[i].pfnGenerate(RT_MAX(cTests, s_aGroups[i].cMinTests), apszNameFmts);
                if (rcExit != RTEXITCODE_SUCCESS)
                    return rcExit;
            }
        return RTEXITCODE_SUCCESS;
#else
        return RTMsgErrorExitFailure("Test data generator not compiled in!");
#endif
    }


    /*
     * Do testing.  Currrently disabled by default as data needs to be checked
     * on both intel and AMD systems first.
     */
    rc = RTTestCreate("tstIEMAImpl", &g_hTest);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    if (enmMode == kModeTest)
    {
        RTTestBanner(g_hTest);

        /* Allocate guarded memory for use in the tests. */
#define ALLOC_GUARDED_VAR(a_puVar) do { \
            rc = RTTestGuardedAlloc(g_hTest, sizeof(*a_puVar), sizeof(*a_puVar), false /*fHead*/, (void **)&a_puVar); \
            if (RT_FAILURE(rc)) RTTestFailed(g_hTest, "Failed to allocate guarded mem: " #a_puVar); \
        } while (0)
        ALLOC_GUARDED_VAR(g_pu8);
        ALLOC_GUARDED_VAR(g_pu16);
        ALLOC_GUARDED_VAR(g_pu32);
        ALLOC_GUARDED_VAR(g_pu64);
        ALLOC_GUARDED_VAR(g_pu128);
        ALLOC_GUARDED_VAR(g_pu8Two);
        ALLOC_GUARDED_VAR(g_pu16Two);
        ALLOC_GUARDED_VAR(g_pu32Two);
        ALLOC_GUARDED_VAR(g_pu64Two);
        ALLOC_GUARDED_VAR(g_pu128Two);
        ALLOC_GUARDED_VAR(g_pfEfl);
        if (RTTestErrorCount(g_hTest) == 0)
        {
            /* Loop thru the groups and call test function for anything that's enabled. */
            for (size_t i = 0; i < RT_ELEMENTS(s_aGroups); i++)
                if ((s_aGroups[i].fCategory & fCategories))
                    s_aGroups[i].pfnTest();
        }
        return RTTestSummaryAndDestroy(g_hTest);
    }
    return RTTestSkipAndDestroy(g_hTest, "unfinished testcase");
}

