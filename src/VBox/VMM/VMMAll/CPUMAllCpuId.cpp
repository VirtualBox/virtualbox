/* $Id: CPUMAllCpuId.cpp 168941 2025-05-20 21:23:52Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - CPU ID part, common bits.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_CPUM
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/ssm.h>
#include "CPUMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/sup.h>

#include <VBox/err.h>
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/x86-helpers.h>
#if defined(RT_ARCH_ARM64) || defined(VBOX_VMM_TARGET_ARMV8)
# include <iprt/armv8.h>
# include <iprt/sort.h>
# if defined(RT_OS_LINUX)
#  include <sys/auxv.h>
# elif defined(RT_OS_WINDOWS)
#  include <iprt/win/windows.h>
# endif
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32) || defined(VBOX_VMM_TARGET_ARMV8)
typedef struct PARTNUMINFO
{
    uint32_t        uPartNum;
    CPUMMICROARCH   enmMicroarch;
    const char     *pszName;
    const char     *pszFullName;
    CPUMCORETYPE    enmCoreType;
} PARTNUMINFO;
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32) || defined(VBOX_VMM_TARGET_ARMV8)
/** ARM CPU info by part number. */
static PARTNUMINFO const g_aPartNumDbArm[] =
{
    { 0xfff,    kCpumMicroarch_Unknown,             "TODO",                 "TODO" },
};

/** Broadcom CPU info by part number. */
static PARTNUMINFO const g_aPartNumDbBroadcom[] =
{
    { 0xfff,    kCpumMicroarch_Unknown,             "TODO",                 "TODO" },
};

/** Qualcomm CPU info by part number. */
static PARTNUMINFO const g_aPartNumDbQualcomm[] =
{
    { 0x0d4b,   kCpumMicroarch_Qualcomm_Kyro,       "Qualcomm Snapdragon 8cx Gen 3",    "Qualcomm Snapdragon 8cx Gen 3 (Kryo Prime)",   kCpumCoreType_Efficiency  }, /* Guessing which part */    /*MIDR_EL1=0x410FD4B0*/
    { 0x0d4c,   kCpumMicroarch_Qualcomm_Kyro,       "Qualcomm Snapdragon 8cx Gen 3",    "Qualcomm Snapdragon 8cx Gen 3 (Kryo Gold)",    kCpumCoreType_Performance }, /* is for which core... */   /*MIDR_EL1=0x410FD4C0*/
    { 0x1001,   kCpumMicroarch_Qualcomm_Oryon,      "Qualcomm Snapdragon X",            "Qualcomm Snapdragon X (Oryon var 1)",          kCpumCoreType_Unknown     }, /*MIDR_EL1=0x511f0011 (perf?)*/
    { 0x2001,   kCpumMicroarch_Qualcomm_Oryon,      "Qualcomm Snapdragon X",            "Qualcomm Snapdragon X (Oryon var 2)",          kCpumCoreType_Unknown     }, /*MIDR_EL1=0x512f0011 (eff?)*/
};

/** Apple CPU info by part number. */
static PARTNUMINFO const g_aPartNumDbApple[] =
{
    { 0x022,    kCpumMicroarch_Apple_M1,            "Apple M1",             "Apple M1 (Icestorm)",          kCpumCoreType_Efficiency  },
    { 0x023,    kCpumMicroarch_Apple_M1,            "Apple M1",             "Apple M1 (Firestorm)",         kCpumCoreType_Performance },
    { 0x024,    kCpumMicroarch_Apple_M1,            "Apple M1 Pro",         "Apple M1 Pro (Icestorm)",      kCpumCoreType_Efficiency  },
    { 0x025,    kCpumMicroarch_Apple_M1,            "Apple M1 Pro",         "Apple M1 Pro (Firestorm)",     kCpumCoreType_Performance },
    { 0x028,    kCpumMicroarch_Apple_M1,            "Apple M1 Max",         "Apple M1 Max (Icestorm)",      kCpumCoreType_Efficiency  },
    { 0x029,    kCpumMicroarch_Apple_M1,            "Apple M1 Max",         "Apple M1 Max (Firestorm)",     kCpumCoreType_Performance },
    /** @todo some sources lists 0x30/31 as plain m2...   */
    { 0x032,    kCpumMicroarch_Apple_M2,            "Apple M2",             "Apple M2 (Blizzard)",          kCpumCoreType_Efficiency  },
    { 0x033,    kCpumMicroarch_Apple_M2,            "Apple M2",             "Apple M2 (Avalanche)",         kCpumCoreType_Performance },
    { 0x034,    kCpumMicroarch_Apple_M2,            "Apple M2 Pro",         "Apple M2 Pro (Blizzard)",      kCpumCoreType_Efficiency  },
    { 0x035,    kCpumMicroarch_Apple_M2,            "Apple M2 Pro",         "Apple M2 Pro (Avalanche)",     kCpumCoreType_Performance },
    { 0x038,    kCpumMicroarch_Apple_M2,            "Apple M2 Max",         "Apple M2 Max (Blizzard)",      kCpumCoreType_Efficiency  },
    { 0x039,    kCpumMicroarch_Apple_M2,            "Apple M2 Max",         "Apple M2 Max (Avalanche)",     kCpumCoreType_Performance },
    { 0x048,    kCpumMicroarch_Apple_M3,            "Apple M3 Max",         "Apple M3 Max (Sawtooth)",      kCpumCoreType_Efficiency  }, /** @todo code names */
    { 0x049,    kCpumMicroarch_Apple_M3,            "Apple M3 Max",         "Apple M3 Max (Everest)",       kCpumCoreType_Performance }, /** @todo code names */
};

/** Ampere CPU info by part number. */
static PARTNUMINFO const g_aPartNumDbAmpere[] =
{
    { 0xfff,    kCpumMicroarch_Unknown,             "TODO",                 "TODO" },
};

#endif /*if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32) || defined(VBOX_VMM_TARGET_ARMV8) */
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86)

/**
 * The intel pentium family.
 */
static const CPUMMICROARCH g_aenmIntelFamily06[] =
{
    /* [ 0(0x00)] = */ kCpumMicroarch_Intel_P6,           /* Pentium Pro A-step (says sandpile.org). */
    /* [ 1(0x01)] = */ kCpumMicroarch_Intel_P6,           /* Pentium Pro */
    /* [ 2(0x02)] = */ kCpumMicroarch_Intel_Unknown,
    /* [ 3(0x03)] = */ kCpumMicroarch_Intel_P6_II,        /* PII Klamath */
    /* [ 4(0x04)] = */ kCpumMicroarch_Intel_Unknown,
    /* [ 5(0x05)] = */ kCpumMicroarch_Intel_P6_II,        /* PII Deschutes */
    /* [ 6(0x06)] = */ kCpumMicroarch_Intel_P6_II,        /* Celeron Mendocino. */
    /* [ 7(0x07)] = */ kCpumMicroarch_Intel_P6_III,       /* PIII Katmai. */
    /* [ 8(0x08)] = */ kCpumMicroarch_Intel_P6_III,       /* PIII Coppermine (includes Celeron). */
    /* [ 9(0x09)] = */ kCpumMicroarch_Intel_P6_M_Banias,  /* Pentium/Celeron M Banias. */
    /* [10(0x0a)] = */ kCpumMicroarch_Intel_P6_III,       /* PIII Xeon */
    /* [11(0x0b)] = */ kCpumMicroarch_Intel_P6_III,       /* PIII Tualatin (includes Celeron). */
    /* [12(0x0c)] = */ kCpumMicroarch_Intel_Unknown,
    /* [13(0x0d)] = */ kCpumMicroarch_Intel_P6_M_Dothan,  /* Pentium/Celeron M Dothan. */
    /* [14(0x0e)] = */ kCpumMicroarch_Intel_Core_Yonah,   /* Core Yonah (Enhanced Pentium M). */
    /* [15(0x0f)] = */ kCpumMicroarch_Intel_Core2_Merom,  /* Merom */
    /* [16(0x10)] = */ kCpumMicroarch_Intel_Unknown,
    /* [17(0x11)] = */ kCpumMicroarch_Intel_Unknown,
    /* [18(0x12)] = */ kCpumMicroarch_Intel_Unknown,
    /* [19(0x13)] = */ kCpumMicroarch_Intel_Unknown,
    /* [20(0x14)] = */ kCpumMicroarch_Intel_Unknown,
    /* [21(0x15)] = */ kCpumMicroarch_Intel_P6_M_Dothan,  /* Tolapai - System-on-a-chip. */
    /* [22(0x16)] = */ kCpumMicroarch_Intel_Core2_Merom,
    /* [23(0x17)] = */ kCpumMicroarch_Intel_Core2_Penryn,
    /* [24(0x18)] = */ kCpumMicroarch_Intel_Unknown,
    /* [25(0x19)] = */ kCpumMicroarch_Intel_Unknown,
    /* [26(0x1a)] = */ kCpumMicroarch_Intel_Core7_Nehalem, /* Nehalem-EP */
    /* [27(0x1b)] = */ kCpumMicroarch_Intel_Unknown,
    /* [28(0x1c)] = */ kCpumMicroarch_Intel_Atom_Bonnell, /* Diamonville, Pineview, */
    /* [29(0x1d)] = */ kCpumMicroarch_Intel_Core2_Penryn,
    /* [30(0x1e)] = */ kCpumMicroarch_Intel_Core7_Nehalem, /* Clarksfield, Lynnfield, Jasper Forest. */
    /* [31(0x1f)] = */ kCpumMicroarch_Intel_Core7_Nehalem, /* Only listed by sandpile.org.  2 cores ABD/HVD, whatever that means. */
    /* [32(0x20)] = */ kCpumMicroarch_Intel_Unknown,
    /* [33(0x21)] = */ kCpumMicroarch_Intel_Unknown,
    /* [34(0x22)] = */ kCpumMicroarch_Intel_Unknown,
    /* [35(0x23)] = */ kCpumMicroarch_Intel_Unknown,
    /* [36(0x24)] = */ kCpumMicroarch_Intel_Unknown,
    /* [37(0x25)] = */ kCpumMicroarch_Intel_Core7_Westmere, /* Arrandale, Clarksdale. */
    /* [38(0x26)] = */ kCpumMicroarch_Intel_Atom_Lincroft,
    /* [39(0x27)] = */ kCpumMicroarch_Intel_Atom_Saltwell,
    /* [40(0x28)] = */ kCpumMicroarch_Intel_Unknown,
    /* [41(0x29)] = */ kCpumMicroarch_Intel_Unknown,
    /* [42(0x2a)] = */ kCpumMicroarch_Intel_Core7_SandyBridge,
    /* [43(0x2b)] = */ kCpumMicroarch_Intel_Unknown,
    /* [44(0x2c)] = */ kCpumMicroarch_Intel_Core7_Westmere, /* Gulftown, Westmere-EP. */
    /* [45(0x2d)] = */ kCpumMicroarch_Intel_Core7_SandyBridge, /* SandyBridge-E, SandyBridge-EN, SandyBridge-EP. */
    /* [46(0x2e)] = */ kCpumMicroarch_Intel_Core7_Nehalem,  /* Beckton (Xeon). */
    /* [47(0x2f)] = */ kCpumMicroarch_Intel_Core7_Westmere, /* Westmere-EX. */
    /* [48(0x30)] = */ kCpumMicroarch_Intel_Unknown,
    /* [49(0x31)] = */ kCpumMicroarch_Intel_Unknown,
    /* [50(0x32)] = */ kCpumMicroarch_Intel_Unknown,
    /* [51(0x33)] = */ kCpumMicroarch_Intel_Unknown,
    /* [52(0x34)] = */ kCpumMicroarch_Intel_Unknown,
    /* [53(0x35)] = */ kCpumMicroarch_Intel_Atom_Saltwell, /* ?? */
    /* [54(0x36)] = */ kCpumMicroarch_Intel_Atom_Saltwell, /* Cedarview, ++ */
    /* [55(0x37)] = */ kCpumMicroarch_Intel_Atom_Silvermont,
    /* [56(0x38)] = */ kCpumMicroarch_Intel_Unknown,
    /* [57(0x39)] = */ kCpumMicroarch_Intel_Unknown,
    /* [58(0x3a)] = */ kCpumMicroarch_Intel_Core7_IvyBridge,
    /* [59(0x3b)] = */ kCpumMicroarch_Intel_Unknown,
    /* [60(0x3c)] = */ kCpumMicroarch_Intel_Core7_Haswell,
    /* [61(0x3d)] = */ kCpumMicroarch_Intel_Core7_Broadwell,
    /* [62(0x3e)] = */ kCpumMicroarch_Intel_Core7_IvyBridge,
    /* [63(0x3f)] = */ kCpumMicroarch_Intel_Core7_Haswell,
    /* [64(0x40)] = */ kCpumMicroarch_Intel_Unknown,
    /* [65(0x41)] = */ kCpumMicroarch_Intel_Unknown,
    /* [66(0x42)] = */ kCpumMicroarch_Intel_Unknown,
    /* [67(0x43)] = */ kCpumMicroarch_Intel_Unknown,
    /* [68(0x44)] = */ kCpumMicroarch_Intel_Unknown,
    /* [69(0x45)] = */ kCpumMicroarch_Intel_Core7_Haswell,
    /* [70(0x46)] = */ kCpumMicroarch_Intel_Core7_Haswell,
    /* [71(0x47)] = */ kCpumMicroarch_Intel_Core7_Broadwell,    /* i7-5775C */
    /* [72(0x48)] = */ kCpumMicroarch_Intel_Unknown,
    /* [73(0x49)] = */ kCpumMicroarch_Intel_Unknown,
    /* [74(0x4a)] = */ kCpumMicroarch_Intel_Atom_Silvermont,
    /* [75(0x4b)] = */ kCpumMicroarch_Intel_Unknown,
    /* [76(0x4c)] = */ kCpumMicroarch_Intel_Atom_Airmount,
    /* [77(0x4d)] = */ kCpumMicroarch_Intel_Atom_Silvermont,
    /* [78(0x4e)] = */ kCpumMicroarch_Intel_Core7_Skylake,
    /* [79(0x4f)] = */ kCpumMicroarch_Intel_Core7_Broadwell,    /* Broadwell-E */
    /* [80(0x50)] = */ kCpumMicroarch_Intel_Unknown,
    /* [81(0x51)] = */ kCpumMicroarch_Intel_Unknown,
    /* [82(0x52)] = */ kCpumMicroarch_Intel_Unknown,
    /* [83(0x53)] = */ kCpumMicroarch_Intel_Unknown,
    /* [84(0x54)] = */ kCpumMicroarch_Intel_Unknown,
    /* [85(0x55)] = */ kCpumMicroarch_Intel_Core7_Skylake,      /* server cpu; skylake <= 4, cascade lake > 5 */
    /* [86(0x56)] = */ kCpumMicroarch_Intel_Core7_Broadwell,    /* Xeon D-1540, Broadwell-DE */
    /* [87(0x57)] = */ kCpumMicroarch_Intel_Phi_KnightsLanding,
    /* [88(0x58)] = */ kCpumMicroarch_Intel_Unknown,
    /* [89(0x59)] = */ kCpumMicroarch_Intel_Unknown,
    /* [90(0x5a)] = */ kCpumMicroarch_Intel_Atom_Silvermont,    /* Moorefield */
    /* [91(0x5b)] = */ kCpumMicroarch_Intel_Unknown,
    /* [92(0x5c)] = */ kCpumMicroarch_Intel_Atom_Goldmont,      /* Apollo Lake */
    /* [93(0x5d)] = */ kCpumMicroarch_Intel_Atom_Silvermont,    /* x3-C3230 */
    /* [94(0x5e)] = */ kCpumMicroarch_Intel_Core7_Skylake,      /* i7-6700K */
    /* [95(0x5f)] = */ kCpumMicroarch_Intel_Atom_Goldmont,      /* Denverton */
    /* [96(0x60)] = */ kCpumMicroarch_Intel_Unknown,
    /* [97(0x61)] = */ kCpumMicroarch_Intel_Unknown,
    /* [98(0x62)] = */ kCpumMicroarch_Intel_Unknown,
    /* [99(0x63)] = */ kCpumMicroarch_Intel_Unknown,
    /*[100(0x64)] = */ kCpumMicroarch_Intel_Unknown,
    /*[101(0x65)] = */ kCpumMicroarch_Intel_Atom_Silvermont,    /* SoFIA */
    /*[102(0x66)] = */ kCpumMicroarch_Intel_Core7_CannonLake, /* unconfirmed */
    /*[103(0x67)] = */ kCpumMicroarch_Intel_Unknown,
    /*[104(0x68)] = */ kCpumMicroarch_Intel_Unknown,
    /*[105(0x69)] = */ kCpumMicroarch_Intel_Unknown,
    /*[106(0x6a)] = */ kCpumMicroarch_Intel_Core7_IceLake,      /* unconfirmed server */
    /*[107(0x6b)] = */ kCpumMicroarch_Intel_Unknown,
    /*[108(0x6c)] = */ kCpumMicroarch_Intel_Core7_IceLake,      /* unconfirmed server */
    /*[109(0x6d)] = */ kCpumMicroarch_Intel_Unknown,
    /*[110(0x6e)] = */ kCpumMicroarch_Intel_Atom_Airmount,      /* or silvermount? */
    /*[111(0x6f)] = */ kCpumMicroarch_Intel_Unknown,
    /*[112(0x70)] = */ kCpumMicroarch_Intel_Unknown,
    /*[113(0x71)] = */ kCpumMicroarch_Intel_Unknown,
    /*[114(0x72)] = */ kCpumMicroarch_Intel_Unknown,
    /*[115(0x73)] = */ kCpumMicroarch_Intel_Unknown,
    /*[116(0x74)] = */ kCpumMicroarch_Intel_Unknown,
    /*[117(0x75)] = */ kCpumMicroarch_Intel_Atom_Airmount,      /* or silvermount? */
    /*[118(0x76)] = */ kCpumMicroarch_Intel_Unknown,
    /*[119(0x77)] = */ kCpumMicroarch_Intel_Unknown,
    /*[120(0x78)] = */ kCpumMicroarch_Intel_Unknown,
    /*[121(0x79)] = */ kCpumMicroarch_Intel_Unknown,
    /*[122(0x7a)] = */ kCpumMicroarch_Intel_Atom_GoldmontPlus,
    /*[123(0x7b)] = */ kCpumMicroarch_Intel_Unknown,
    /*[124(0x7c)] = */ kCpumMicroarch_Intel_Unknown,
    /*[125(0x7d)] = */ kCpumMicroarch_Intel_Core7_IceLake,      /* unconfirmed */
    /*[126(0x7e)] = */ kCpumMicroarch_Intel_Core7_IceLake,      /* unconfirmed */
    /*[127(0x7f)] = */ kCpumMicroarch_Intel_Unknown,
    /*[128(0x80)] = */ kCpumMicroarch_Intel_Unknown,
    /*[129(0x81)] = */ kCpumMicroarch_Intel_Unknown,
    /*[130(0x82)] = */ kCpumMicroarch_Intel_Unknown,
    /*[131(0x83)] = */ kCpumMicroarch_Intel_Unknown,
    /*[132(0x84)] = */ kCpumMicroarch_Intel_Unknown,
    /*[133(0x85)] = */ kCpumMicroarch_Intel_Phi_KnightsMill,
    /*[134(0x86)] = */ kCpumMicroarch_Intel_Unknown,
    /*[135(0x87)] = */ kCpumMicroarch_Intel_Unknown,
    /*[136(0x88)] = */ kCpumMicroarch_Intel_Unknown,
    /*[137(0x89)] = */ kCpumMicroarch_Intel_Unknown,
    /*[138(0x8a)] = */ kCpumMicroarch_Intel_Unknown,
    /*[139(0x8b)] = */ kCpumMicroarch_Intel_Unknown,
    /*[140(0x8c)] = */ kCpumMicroarch_Intel_Core7_TigerLake,    /* 11th Gen Intel(R) Core(TM) i7-1185G7 @ 3.00GHz (bird) */
    /*[141(0x8d)] = */ kCpumMicroarch_Intel_Core7_TigerLake,    /* unconfirmed */
    /*[142(0x8e)] = */ kCpumMicroarch_Intel_Core7_KabyLake,     /* Stepping >= 0xB is Whiskey Lake, 0xA is CoffeeLake. */
    /*[143(0x8f)] = */ kCpumMicroarch_Intel_Core7_SapphireRapids,
    /*[144(0x90)] = */ kCpumMicroarch_Intel_Unknown,
    /*[145(0x91)] = */ kCpumMicroarch_Intel_Unknown,
    /*[146(0x92)] = */ kCpumMicroarch_Intel_Unknown,
    /*[147(0x93)] = */ kCpumMicroarch_Intel_Unknown,
    /*[148(0x94)] = */ kCpumMicroarch_Intel_Unknown,
    /*[149(0x95)] = */ kCpumMicroarch_Intel_Unknown,
    /*[150(0x96)] = */ kCpumMicroarch_Intel_Unknown,
    /*[151(0x97)] = */ kCpumMicroarch_Intel_Core7_AlderLake,    /* unconfirmed, unreleased */
    /*[152(0x98)] = */ kCpumMicroarch_Intel_Unknown,
    /*[153(0x99)] = */ kCpumMicroarch_Intel_Unknown,
    /*[154(0x9a)] = */ kCpumMicroarch_Intel_Core7_AlderLake,    /* unconfirmed, unreleased */
    /*[155(0x9b)] = */ kCpumMicroarch_Intel_Unknown,
    /*[156(0x9c)] = */ kCpumMicroarch_Intel_Unknown,
    /*[157(0x9d)] = */ kCpumMicroarch_Intel_Unknown,
    /*[158(0x9e)] = */ kCpumMicroarch_Intel_Core7_KabyLake, /* Stepping >= 0xB is Whiskey Lake, 0xA is CoffeeLake. */
    /*[159(0x9f)] = */ kCpumMicroarch_Intel_Unknown,
    /*[160(0xa0)] = */ kCpumMicroarch_Intel_Unknown,
    /*[161(0xa1)] = */ kCpumMicroarch_Intel_Unknown,
    /*[162(0xa2)] = */ kCpumMicroarch_Intel_Unknown,
    /*[163(0xa3)] = */ kCpumMicroarch_Intel_Unknown,
    /*[164(0xa4)] = */ kCpumMicroarch_Intel_Unknown,
    /*[165(0xa5)] = */ kCpumMicroarch_Intel_Core7_CometLake,    /* unconfirmed */
    /*[166(0xa6)] = */ kCpumMicroarch_Intel_Unknown,
    /*[167(0xa7)] = */ kCpumMicroarch_Intel_Core7_CypressCove, /* 14nm backport, unconfirmed */
};
AssertCompile(RT_ELEMENTS(g_aenmIntelFamily06) == 0xa7+1);


/**
 * Figures out the (sub-)micro architecture given a bit of CPUID info.
 *
 * @returns Micro architecture.
 * @param   enmVendor           The CPU vendor.
 * @param   bFamily             The CPU family.
 * @param   bModel              The CPU model.
 * @param   bStepping           The CPU stepping.
 */
VMMDECL(CPUMMICROARCH) CPUMCpuIdDetermineX86MicroarchEx(CPUMCPUVENDOR enmVendor, uint8_t bFamily,
                                                        uint8_t bModel, uint8_t bStepping)
{
    if (enmVendor == CPUMCPUVENDOR_AMD)
    {
        switch (bFamily)
        {
            case 0x02:  return kCpumMicroarch_AMD_Am286; /* Not really kosher... */
            case 0x03:  return kCpumMicroarch_AMD_Am386;
            case 0x23:  return kCpumMicroarch_AMD_Am386; /* SX*/
            case 0x04:  return bModel < 14 ? kCpumMicroarch_AMD_Am486 : kCpumMicroarch_AMD_Am486Enh;
            case 0x05:  return bModel <  6 ? kCpumMicroarch_AMD_K5    : kCpumMicroarch_AMD_K6; /* Genode LX is 0x0a, lump it with K6. */
            case 0x06:
                switch (bModel)
                {
                    case  0: return kCpumMicroarch_AMD_K7_Palomino;
                    case  1: return kCpumMicroarch_AMD_K7_Palomino;
                    case  2: return kCpumMicroarch_AMD_K7_Palomino;
                    case  3: return kCpumMicroarch_AMD_K7_Spitfire;
                    case  4: return kCpumMicroarch_AMD_K7_Thunderbird;
                    case  6: return kCpumMicroarch_AMD_K7_Palomino;
                    case  7: return kCpumMicroarch_AMD_K7_Morgan;
                    case  8: return kCpumMicroarch_AMD_K7_Thoroughbred;
                    case 10: return kCpumMicroarch_AMD_K7_Barton; /* Thorton too. */
                }
                return kCpumMicroarch_AMD_K7_Unknown;
            case 0x0f:
                /*
                 * This family is a friggin mess. Trying my best to make some
                 * sense out of it. Too much happened in the 0x0f family to
                 * lump it all together as K8 (130nm->90nm->65nm, AMD-V, ++).
                 *
                 * Emperical CPUID.01h.EAX evidence from revision guides, wikipedia,
                 * cpu-world.com, and other places:
                 *  - 130nm:
                 *     - ClawHammer:    F7A/SH-CG, F5A/-CG, F4A/-CG, F50/-B0, F48/-C0, F58/-C0,
                 *     - SledgeHammer:  F50/SH-B0, F48/-C0, F58/-C0, F4A/-CG, F5A/-CG, F7A/-CG, F51/-B3
                 *     - Newcastle:     FC0/DH-CG (erratum #180: FE0/DH-CG), FF0/DH-CG
                 *     - Dublin:        FC0/-CG, FF0/-CG, F82/CH-CG, F4A/-CG, F48/SH-C0,
                 *     - Odessa:        FC0/DH-CG (erratum #180: FE0/DH-CG)
                 *     - Paris:         FF0/DH-CG, FC0/DH-CG (erratum #180: FE0/DH-CG),
                 *  - 90nm:
                 *     - Winchester:    10FF0/DH-D0, 20FF0/DH-E3.
                 *     - Oakville:      10FC0/DH-D0.
                 *     - Georgetown:    10FC0/DH-D0.
                 *     - Sonora:        10FC0/DH-D0.
                 *     - Venus:         20F71/SH-E4
                 *     - Troy:          20F51/SH-E4
                 *     - Athens:        20F51/SH-E4
                 *     - San Diego:     20F71/SH-E4.
                 *     - Lancaster:     20F42/SH-E5
                 *     - Newark:        20F42/SH-E5.
                 *     - Albany:        20FC2/DH-E6.
                 *     - Roma:          20FC2/DH-E6.
                 *     - Venice:        20FF0/DH-E3, 20FC2/DH-E6, 20FF2/DH-E6.
                 *     - Palermo:       10FC0/DH-D0, 20FF0/DH-E3, 20FC0/DH-E3, 20FC2/DH-E6, 20FF2/DH-E6
                 *  - 90nm introducing Dual core:
                 *     - Denmark:       20F30/JH-E1, 20F32/JH-E6
                 *     - Italy:         20F10/JH-E1, 20F12/JH-E6
                 *     - Egypt:         20F10/JH-E1, 20F12/JH-E6
                 *     - Toledo:        20F32/JH-E6, 30F72/DH-E6 (single code variant).
                 *     - Manchester:    20FB1/BH-E4, 30FF2/BH-E4.
                 *  - 90nm 2nd gen opteron ++, AMD-V introduced (might be missing in some cheaper models):
                 *     - Santa Ana:     40F32/JH-F2, /-F3
                 *     - Santa Rosa:    40F12/JH-F2, 40F13/JH-F3
                 *     - Windsor:       40F32/JH-F2, 40F33/JH-F3, C0F13/JH-F3, 40FB2/BH-F2, ??20FB1/BH-E4??.
                 *     - Manila:        50FF2/DH-F2, 40FF2/DH-F2
                 *     - Orleans:       40FF2/DH-F2, 50FF2/DH-F2, 50FF3/DH-F3.
                 *     - Keene:         40FC2/DH-F2.
                 *     - Richmond:      40FC2/DH-F2
                 *     - Taylor:        40F82/BH-F2
                 *     - Trinidad:      40F82/BH-F2
                 *
                 *  - 65nm:
                 *     - Brisbane:      60FB1/BH-G1, 60FB2/BH-G2.
                 *     - Tyler:         60F81/BH-G1, 60F82/BH-G2.
                 *     - Sparta:        70FF1/DH-G1, 70FF2/DH-G2.
                 *     - Lima:          70FF1/DH-G1, 70FF2/DH-G2.
                 *     - Sherman:       /-G1, 70FC2/DH-G2.
                 *     - Huron:         70FF2/DH-G2.
                 */
                if (bModel < 0x10)
                    return kCpumMicroarch_AMD_K8_130nm;
                if (bModel >= 0x60 && bModel < 0x80)
                    return kCpumMicroarch_AMD_K8_65nm;
                if (bModel >= 0x40)
                    return kCpumMicroarch_AMD_K8_90nm_AMDV;
                switch (bModel)
                {
                    case 0x21:
                    case 0x23:
                    case 0x2b:
                    case 0x2f:
                    case 0x37:
                    case 0x3f:
                        return kCpumMicroarch_AMD_K8_90nm_DualCore;
                }
                return kCpumMicroarch_AMD_K8_90nm;
            case 0x10:
                return kCpumMicroarch_AMD_K10;
            case 0x11:
                return kCpumMicroarch_AMD_K10_Lion;
            case 0x12:
                return kCpumMicroarch_AMD_K10_Llano;
            case 0x14:
                return kCpumMicroarch_AMD_Bobcat;
            case 0x15:
                switch (bModel)
                {
                    case 0x00:  return kCpumMicroarch_AMD_15h_Bulldozer;    /* Any? prerelease? */
                    case 0x01:  return kCpumMicroarch_AMD_15h_Bulldozer;    /* Opteron 4200, FX-81xx. */
                    case 0x02:  return kCpumMicroarch_AMD_15h_Piledriver;   /* Opteron 4300, FX-83xx. */
                    case 0x10:  return kCpumMicroarch_AMD_15h_Piledriver;   /* A10-5800K for e.g. */
                    case 0x11:  /* ?? */
                    case 0x12:  /* ?? */
                    case 0x13:  return kCpumMicroarch_AMD_15h_Piledriver;   /* A10-6800K for e.g. */
                }
                return kCpumMicroarch_AMD_15h_Unknown;
            case 0x16:
                return kCpumMicroarch_AMD_Jaguar;
            case 0x17:
                return kCpumMicroarch_AMD_Zen_Ryzen;
        }
        return kCpumMicroarch_AMD_Unknown;
    }

    if (enmVendor == CPUMCPUVENDOR_INTEL)
    {
        switch (bFamily)
        {
            case 3:
                return kCpumMicroarch_Intel_80386;
            case 4:
                return kCpumMicroarch_Intel_80486;
            case 5:
                return kCpumMicroarch_Intel_P5;
            case 6:
                if (bModel < RT_ELEMENTS(g_aenmIntelFamily06))
                {
                    CPUMMICROARCH enmMicroArch = g_aenmIntelFamily06[bModel];
                    if (enmMicroArch == kCpumMicroarch_Intel_Core7_KabyLake)
                    {
                        if (bStepping >= 0xa && bStepping <= 0xc)
                            enmMicroArch = kCpumMicroarch_Intel_Core7_CoffeeLake;
                        else if (bStepping >= 0xc)
                            enmMicroArch = kCpumMicroarch_Intel_Core7_WhiskeyLake;
                    }
                    else if (   enmMicroArch == kCpumMicroarch_Intel_Core7_Skylake
                             && bModel == 0x55
                             && bStepping >= 5)
                        enmMicroArch = kCpumMicroarch_Intel_Core7_CascadeLake;
                    return enmMicroArch;
                }
                return kCpumMicroarch_Intel_Atom_Unknown;
            case 15:
                switch (bModel)
                {
                    case 0:     return kCpumMicroarch_Intel_NB_Willamette;
                    case 1:     return kCpumMicroarch_Intel_NB_Willamette;
                    case 2:     return kCpumMicroarch_Intel_NB_Northwood;
                    case 3:     return kCpumMicroarch_Intel_NB_Prescott;
                    case 4:     return kCpumMicroarch_Intel_NB_Prescott2M; /* ?? */
                    case 5:     return kCpumMicroarch_Intel_NB_Unknown; /*??*/
                    case 6:     return kCpumMicroarch_Intel_NB_CedarMill;
                    case 7:     return kCpumMicroarch_Intel_NB_Gallatin;
                    default:    return kCpumMicroarch_Intel_NB_Unknown;
                }
                break;
            /* The following are not kosher but kind of follow intuitively from 6, 5 & 4. */
            case 0:
                return kCpumMicroarch_Intel_8086;
            case 1:
                return kCpumMicroarch_Intel_80186;
            case 2:
                return kCpumMicroarch_Intel_80286;
        }
        return kCpumMicroarch_Intel_Unknown;
    }

    if (enmVendor == CPUMCPUVENDOR_VIA)
    {
        switch (bFamily)
        {
            case 5:
                switch (bModel)
                {
                    case 1: return kCpumMicroarch_Centaur_C6;
                    case 4: return kCpumMicroarch_Centaur_C6;
                    case 8: return kCpumMicroarch_Centaur_C2;
                    case 9: return kCpumMicroarch_Centaur_C3;
                }
                break;

            case 6:
                switch (bModel)
                {
                    case  5: return kCpumMicroarch_VIA_C3_M2;
                    case  6: return kCpumMicroarch_VIA_C3_C5A;
                    case  7: return bStepping < 8 ? kCpumMicroarch_VIA_C3_C5B : kCpumMicroarch_VIA_C3_C5C;
                    case  8: return kCpumMicroarch_VIA_C3_C5N;
                    case  9: return bStepping < 8 ? kCpumMicroarch_VIA_C3_C5XL : kCpumMicroarch_VIA_C3_C5P;
                    case 10: return kCpumMicroarch_VIA_C7_C5J;
                    case 15: return kCpumMicroarch_VIA_Isaiah;
                }
                break;
        }
        return kCpumMicroarch_VIA_Unknown;
    }

    if (enmVendor == CPUMCPUVENDOR_SHANGHAI)
    {
        switch (bFamily)
        {
            case 6:
            case 7:
                return kCpumMicroarch_Shanghai_Wudaokou;
            default:
                break;
        }
        return kCpumMicroarch_Shanghai_Unknown;
    }

    if (enmVendor == CPUMCPUVENDOR_CYRIX)
    {
        switch (bFamily)
        {
            case 4:
                switch (bModel)
                {
                    case 9: return kCpumMicroarch_Cyrix_5x86;
                }
                break;

            case 5:
                switch (bModel)
                {
                    case 2: return kCpumMicroarch_Cyrix_M1;
                    case 4: return kCpumMicroarch_Cyrix_MediaGX;
                    case 5: return kCpumMicroarch_Cyrix_MediaGXm;
                }
                break;

            case 6:
                switch (bModel)
                {
                    case 0: return kCpumMicroarch_Cyrix_M2;
                }
                break;

        }
        return kCpumMicroarch_Cyrix_Unknown;
    }

    if (enmVendor == CPUMCPUVENDOR_HYGON)
    {
        switch (bFamily)
        {
            case 0x18:
                return kCpumMicroarch_Hygon_Dhyana;
            default:
                break;
        }
        return kCpumMicroarch_Hygon_Unknown;
    }

    return kCpumMicroarch_Unknown;
}

#endif /* if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86) */

#if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32) || defined(VBOX_VMM_TARGET_ARMV8)
/**
 * Gets the microarch, vendor, coretype and names from a MIDR value or CPU name.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on direct @a idMain match.
 * @retval  VINF_CPUM_MATCHED_BY_NAME on match via @a pszCpuName.
 * @retval  VERR_UNSUPPORTED_CPU if not found.
 * @param   idMain          The ARM Main ID register value.
 * @param   pszCpuName      The CPU name to match with if @a idMain doesn't
 *                          produce an immediate result.
 * @param   penmMicroarch   Where to return the microarchitecture. Optional.
 * @param   penmVendor      Where to return the CPU vendor. Optional.
 * @param   penmCoreType    Where to return the core type. Optional.
 * @param   ppszName        Where to return the CPU name. Optional.
 * @param   ppszFullName    Where to return the CPU/Core full name. Optional.
 *
 */
VMMR3DECL(int) CPUMCpuIdDetermineArmV8MicroarchEx(uint64_t idMain, const char *pszCpuName,
                                                  CPUMMICROARCH *penmMicroarch,
                                                  CPUMCPUVENDOR *penmVendor,
                                                  CPUMCORETYPE  *penmCoreType,
                                                  const char   **ppszName,
                                                  const char   **ppszFullName)
{
    /*
     * Set the return values to failure values.
     */
    if (penmMicroarch)
        *penmMicroarch = kCpumMicroarch_Invalid;
    if (penmVendor)
        *penmVendor    = CPUMCPUVENDOR_INVALID;
    if (penmCoreType)
        *penmCoreType  = kCpumCoreType_Invalid;
    if (ppszName)
        *ppszName      = NULL;
    if (ppszFullName)
        *ppszFullName  = NULL;

    /*
     * Unpack the main ID register value.
     */
    uint8_t const       bImplementer = (uint8_t )((idMain >> 24) & 0xff);
    uint8_t  const      bVariant     = (uint8_t )((idMain >> 20) & 0xf);
    uint16_t const      uPartNum     = (uint16_t)((idMain >>  4) & 0xfff);
    //uint8_t const       bRevision    = (uint8_t )( idMain        & 0x7);
    uint16_t const      uPartNumEx   = uPartNum | ((uint16_t)bVariant << 12);

    /*
     * Process the implementer field, determining the vendor and part number
     * table with its lookup key.
     */
    CPUMCPUVENDOR       enmVendor;
    PARTNUMINFO const  *paPartNums;
    size_t              cPartNums;
    uint32_t            uPartNumSearch = uPartNum;
    switch (bImplementer)
    {
        case 0x41:
            enmVendor  = CPUMCPUVENDOR_ARM;
            paPartNums = g_aPartNumDbArm;
            cPartNums  = RT_ELEMENTS(g_aPartNumDbArm);
            break;

        case 0x42:
            enmVendor  = CPUMCPUVENDOR_BROADCOM;
            paPartNums = g_aPartNumDbBroadcom;
            cPartNums  = RT_ELEMENTS(g_aPartNumDbBroadcom);
            break;

        case 0x51:
            enmVendor  = CPUMCPUVENDOR_QUALCOMM;
            paPartNums = g_aPartNumDbQualcomm;
            cPartNums  = RT_ELEMENTS(g_aPartNumDbQualcomm);
            uPartNumSearch = uPartNumEx; /* include the variant in the search */
            break;

        case 0x61:
            enmVendor  = CPUMCPUVENDOR_APPLE;
            paPartNums = g_aPartNumDbApple;
            cPartNums  = RT_ELEMENTS(g_aPartNumDbApple);
            break;

        case 0xc0:
            enmVendor  = CPUMCPUVENDOR_AMPERE;
            paPartNums = g_aPartNumDbAmpere;
            cPartNums  = RT_ELEMENTS(g_aPartNumDbAmpere);
            break;

        default:
            return VERR_UNSUPPORTED_CPU;
    }
    if (penmVendor)
        *penmVendor = enmVendor;

    /*
     * Look up the part number in the vendor table:
     */
    for (size_t i = 0; i < cPartNums; i++)
        if (paPartNums[i].uPartNum == uPartNumSearch)
        {
            if (penmMicroarch)
                *penmMicroarch = paPartNums[i].enmMicroarch;
            if (penmCoreType)
                *penmCoreType  = paPartNums[i].enmCoreType;
            if (ppszName)
                *ppszName      = paPartNums[i].pszName;
            if (ppszFullName)
                *ppszFullName  = paPartNums[i].pszFullName;
            return VINF_SUCCESS;
        }

    /*
     * Search by CPU name (mainly a fallback for apple systems):
     */
    if (pszCpuName && *pszCpuName)
        for (size_t i = 0; i < cPartNums; i++)
            if (   strcmp(paPartNums[i].pszName, pszCpuName) == 0
                || strcmp(paPartNums[i].pszFullName, pszCpuName) == 0)
            {
                if (penmMicroarch)
                    *penmMicroarch = paPartNums[i].enmMicroarch;
                if (penmCoreType)
                    *penmCoreType  = paPartNums[i].enmCoreType;
                if (ppszName)
                    *ppszName      = paPartNums[i].pszName;
                if (ppszFullName)
                    *ppszFullName  = paPartNums[i].pszFullName;
                return VINF_CPUM_MATCHED_BY_NAME;

            }

    return VERR_UNSUPPORTED_CPU;
}
#endif /* if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32) || defined(VBOX_VMM_TARGET_ARMV8) */


/**
 * Translates a microarchitecture enum value to the corresponding string
 * constant.
 *
 * @returns Read-only string constant (omits "kCpumMicroarch_" prefix). Returns
 *          NULL if the value is invalid.
 *
 * @param   enmMicroarch    The enum value to convert.
 */
VMMDECL(const char *) CPUMMicroarchName(CPUMMICROARCH enmMicroarch)
{
    switch (enmMicroarch)
    {
#define CASE_RET_STR(enmValue)  case enmValue: return #enmValue + (sizeof("kCpumMicroarch_") - 1)
        CASE_RET_STR(kCpumMicroarch_Intel_8086);
        CASE_RET_STR(kCpumMicroarch_Intel_80186);
        CASE_RET_STR(kCpumMicroarch_Intel_80286);
        CASE_RET_STR(kCpumMicroarch_Intel_80386);
        CASE_RET_STR(kCpumMicroarch_Intel_80486);
        CASE_RET_STR(kCpumMicroarch_Intel_P5);

        CASE_RET_STR(kCpumMicroarch_Intel_P6);
        CASE_RET_STR(kCpumMicroarch_Intel_P6_II);
        CASE_RET_STR(kCpumMicroarch_Intel_P6_III);

        CASE_RET_STR(kCpumMicroarch_Intel_P6_M_Banias);
        CASE_RET_STR(kCpumMicroarch_Intel_P6_M_Dothan);
        CASE_RET_STR(kCpumMicroarch_Intel_Core_Yonah);

        CASE_RET_STR(kCpumMicroarch_Intel_Core2_Merom);
        CASE_RET_STR(kCpumMicroarch_Intel_Core2_Penryn);

        CASE_RET_STR(kCpumMicroarch_Intel_Core7_Nehalem);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_Westmere);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_SandyBridge);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_IvyBridge);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_Haswell);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_Broadwell);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_Skylake);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_KabyLake);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_CoffeeLake);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_WhiskeyLake);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_CascadeLake);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_CannonLake);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_CometLake);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_IceLake);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_RocketLake);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_TigerLake);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_AlderLake);
        CASE_RET_STR(kCpumMicroarch_Intel_Core7_SapphireRapids);

        CASE_RET_STR(kCpumMicroarch_Intel_Atom_Bonnell);
        CASE_RET_STR(kCpumMicroarch_Intel_Atom_Lincroft);
        CASE_RET_STR(kCpumMicroarch_Intel_Atom_Saltwell);
        CASE_RET_STR(kCpumMicroarch_Intel_Atom_Silvermont);
        CASE_RET_STR(kCpumMicroarch_Intel_Atom_Airmount);
        CASE_RET_STR(kCpumMicroarch_Intel_Atom_Goldmont);
        CASE_RET_STR(kCpumMicroarch_Intel_Atom_GoldmontPlus);
        CASE_RET_STR(kCpumMicroarch_Intel_Atom_Unknown);

        CASE_RET_STR(kCpumMicroarch_Intel_Phi_KnightsFerry);
        CASE_RET_STR(kCpumMicroarch_Intel_Phi_KnightsCorner);
        CASE_RET_STR(kCpumMicroarch_Intel_Phi_KnightsLanding);
        CASE_RET_STR(kCpumMicroarch_Intel_Phi_KnightsHill);
        CASE_RET_STR(kCpumMicroarch_Intel_Phi_KnightsMill);

        CASE_RET_STR(kCpumMicroarch_Intel_NB_Willamette);
        CASE_RET_STR(kCpumMicroarch_Intel_NB_Northwood);
        CASE_RET_STR(kCpumMicroarch_Intel_NB_Prescott);
        CASE_RET_STR(kCpumMicroarch_Intel_NB_Prescott2M);
        CASE_RET_STR(kCpumMicroarch_Intel_NB_CedarMill);
        CASE_RET_STR(kCpumMicroarch_Intel_NB_Gallatin);
        CASE_RET_STR(kCpumMicroarch_Intel_NB_Unknown);

        CASE_RET_STR(kCpumMicroarch_Intel_Unknown);

        CASE_RET_STR(kCpumMicroarch_AMD_Am286);
        CASE_RET_STR(kCpumMicroarch_AMD_Am386);
        CASE_RET_STR(kCpumMicroarch_AMD_Am486);
        CASE_RET_STR(kCpumMicroarch_AMD_Am486Enh);
        CASE_RET_STR(kCpumMicroarch_AMD_K5);
        CASE_RET_STR(kCpumMicroarch_AMD_K6);

        CASE_RET_STR(kCpumMicroarch_AMD_K7_Palomino);
        CASE_RET_STR(kCpumMicroarch_AMD_K7_Spitfire);
        CASE_RET_STR(kCpumMicroarch_AMD_K7_Thunderbird);
        CASE_RET_STR(kCpumMicroarch_AMD_K7_Morgan);
        CASE_RET_STR(kCpumMicroarch_AMD_K7_Thoroughbred);
        CASE_RET_STR(kCpumMicroarch_AMD_K7_Barton);
        CASE_RET_STR(kCpumMicroarch_AMD_K7_Unknown);

        CASE_RET_STR(kCpumMicroarch_AMD_K8_130nm);
        CASE_RET_STR(kCpumMicroarch_AMD_K8_90nm);
        CASE_RET_STR(kCpumMicroarch_AMD_K8_90nm_DualCore);
        CASE_RET_STR(kCpumMicroarch_AMD_K8_90nm_AMDV);
        CASE_RET_STR(kCpumMicroarch_AMD_K8_65nm);

        CASE_RET_STR(kCpumMicroarch_AMD_K10);
        CASE_RET_STR(kCpumMicroarch_AMD_K10_Lion);
        CASE_RET_STR(kCpumMicroarch_AMD_K10_Llano);
        CASE_RET_STR(kCpumMicroarch_AMD_Bobcat);
        CASE_RET_STR(kCpumMicroarch_AMD_Jaguar);

        CASE_RET_STR(kCpumMicroarch_AMD_15h_Bulldozer);
        CASE_RET_STR(kCpumMicroarch_AMD_15h_Piledriver);
        CASE_RET_STR(kCpumMicroarch_AMD_15h_Steamroller);
        CASE_RET_STR(kCpumMicroarch_AMD_15h_Excavator);
        CASE_RET_STR(kCpumMicroarch_AMD_15h_Unknown);

        CASE_RET_STR(kCpumMicroarch_AMD_16h_First);

        CASE_RET_STR(kCpumMicroarch_AMD_Zen_Ryzen);

        CASE_RET_STR(kCpumMicroarch_AMD_Unknown);

        CASE_RET_STR(kCpumMicroarch_Hygon_Dhyana);
        CASE_RET_STR(kCpumMicroarch_Hygon_Unknown);

        CASE_RET_STR(kCpumMicroarch_Centaur_C6);
        CASE_RET_STR(kCpumMicroarch_Centaur_C2);
        CASE_RET_STR(kCpumMicroarch_Centaur_C3);
        CASE_RET_STR(kCpumMicroarch_VIA_C3_M2);
        CASE_RET_STR(kCpumMicroarch_VIA_C3_C5A);
        CASE_RET_STR(kCpumMicroarch_VIA_C3_C5B);
        CASE_RET_STR(kCpumMicroarch_VIA_C3_C5C);
        CASE_RET_STR(kCpumMicroarch_VIA_C3_C5N);
        CASE_RET_STR(kCpumMicroarch_VIA_C3_C5XL);
        CASE_RET_STR(kCpumMicroarch_VIA_C3_C5P);
        CASE_RET_STR(kCpumMicroarch_VIA_C7_C5J);
        CASE_RET_STR(kCpumMicroarch_VIA_Isaiah);
        CASE_RET_STR(kCpumMicroarch_VIA_Unknown);

        CASE_RET_STR(kCpumMicroarch_Shanghai_Wudaokou);
        CASE_RET_STR(kCpumMicroarch_Shanghai_Unknown);

        CASE_RET_STR(kCpumMicroarch_Cyrix_5x86);
        CASE_RET_STR(kCpumMicroarch_Cyrix_M1);
        CASE_RET_STR(kCpumMicroarch_Cyrix_MediaGX);
        CASE_RET_STR(kCpumMicroarch_Cyrix_MediaGXm);
        CASE_RET_STR(kCpumMicroarch_Cyrix_M2);
        CASE_RET_STR(kCpumMicroarch_Cyrix_Unknown);

        CASE_RET_STR(kCpumMicroarch_NEC_V20);
        CASE_RET_STR(kCpumMicroarch_NEC_V30);

        CASE_RET_STR(kCpumMicroarch_Apple_M1);
        CASE_RET_STR(kCpumMicroarch_Apple_M2);
        CASE_RET_STR(kCpumMicroarch_Apple_M3);
        CASE_RET_STR(kCpumMicroarch_Apple_M4);

        CASE_RET_STR(kCpumMicroarch_Qualcomm_Kyro);
        CASE_RET_STR(kCpumMicroarch_Qualcomm_Oryon);

        CASE_RET_STR(kCpumMicroarch_Unknown);

#undef CASE_RET_STR
        case kCpumMicroarch_Invalid:
        case kCpumMicroarch_Intel_End:
        case kCpumMicroarch_Intel_Core2_End:
        case kCpumMicroarch_Intel_Core7_End:
        case kCpumMicroarch_Intel_Atom_End:
        case kCpumMicroarch_Intel_P6_Core_Atom_End:
        case kCpumMicroarch_Intel_Phi_End:
        case kCpumMicroarch_Intel_NB_End:
        case kCpumMicroarch_AMD_K7_End:
        case kCpumMicroarch_AMD_K8_End:
        case kCpumMicroarch_AMD_15h_End:
        case kCpumMicroarch_AMD_16h_End:
        case kCpumMicroarch_AMD_Zen_End:
        case kCpumMicroarch_AMD_End:
        case kCpumMicroarch_Hygon_End:
        case kCpumMicroarch_VIA_End:
        case kCpumMicroarch_Shanghai_End:
        case kCpumMicroarch_Cyrix_End:
        case kCpumMicroarch_NEC_End:
        case kCpumMicroarch_Apple_End:
        case kCpumMicroarch_Qualcomm_End:
        case kCpumMicroarch_32BitHack:
            break;
        /* no default! */
    }

    return NULL;
}

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86)

/**
 * Gets a matching leaf in the CPUID leaf array.
 *
 * @returns Pointer to the matching leaf, or NULL if not found.
 * @param   paLeaves            The CPUID leaves to search.  This is sorted.
 * @param   cLeaves             The number of leaves in the array.
 * @param   uLeaf               The leaf to locate.
 * @param   uSubLeaf            The subleaf to locate.  Pass 0 if no sub-leaves.
 */
PCPUMCPUIDLEAF cpumCpuIdGetLeafInt(PCPUMCPUIDLEAF paLeaves, uint32_t cLeaves, uint32_t uLeaf, uint32_t uSubLeaf)
{
    /* Lazy bird does linear lookup here since this is only used for the
       occational CPUID overrides. */
    for (uint32_t i = 0; i < cLeaves; i++)
        if (   paLeaves[i].uLeaf    == uLeaf
            && paLeaves[i].uSubLeaf == (uSubLeaf & paLeaves[i].fSubLeafMask))
            return &paLeaves[i];
    return NULL;
}


/**
 * Ensures that the CPUID leaf array can hold one more leaf.
 *
 * @returns Pointer to the CPUID leaf array (*ppaLeaves) on success.  NULL on
 *          failure.
 * @param   pVM         The cross context VM structure.  If NULL, use
 *                      the process heap, otherwise the VM's hyper heap.
 * @param   ppaLeaves   Pointer to the variable holding the array pointer
 *                      (input/output).
 * @param   cLeaves     The current array size.
 *
 * @remarks This function will automatically update the R0 and RC pointers when
 *          using the hyper heap, which means @a ppaLeaves and @a cLeaves must
 *          be the corresponding VM's CPUID arrays (which is asserted).
 */
PCPUMCPUIDLEAF cpumCpuIdEnsureSpace(PVM pVM, PCPUMCPUIDLEAF *ppaLeaves, uint32_t cLeaves)
{
    /*
     * If pVM is not specified, we're on the regular heap and can waste a
     * little space to speed things up.
     */
    uint32_t cAllocated;
    if (!pVM)
    {
        cAllocated = RT_ALIGN(cLeaves, 16);
        if (cLeaves + 1 > cAllocated)
        {
            void *pvNew = RTMemRealloc(*ppaLeaves, (cAllocated + 16) * sizeof(**ppaLeaves));
            if (pvNew)
                *ppaLeaves = (PCPUMCPUIDLEAF)pvNew;
            else
            {
                RTMemFree(*ppaLeaves);
                *ppaLeaves = NULL;
            }
        }
    }
    /*
     * Otherwise, we're on the hyper heap and are probably just inserting
     * one or two leaves and should conserve space.
     */
    else
    {
# if defined(IN_VBOX_CPU_REPORT) || !defined(VBOX_VMM_TARGET_X86)
        AssertReleaseFailed();
# else
#  ifdef IN_RING3
        Assert(ppaLeaves == &pVM->cpum.s.GuestInfo.paCpuIdLeavesR3);
        Assert(*ppaLeaves == pVM->cpum.s.GuestInfo.aCpuIdLeaves);
        Assert(cLeaves == pVM->cpum.s.GuestInfo.cCpuIdLeaves);

        if (cLeaves + 1 <= RT_ELEMENTS(pVM->cpum.s.GuestInfo.aCpuIdLeaves))
        { }
        else
#  endif
        {
            *ppaLeaves = NULL;
            LogRel(("CPUM: cpumR3CpuIdEnsureSpace: Out of CPUID space!\n"));
        }
# endif
    }
    return *ppaLeaves;
}


# ifdef VBOX_STRICT
/**
 * Checks that we've updated the CPUID leaves array correctly.
 *
 * This is a no-op in non-strict builds.
 *
 * @param   paLeaves            The leaves array.
 * @param   cLeaves             The number of leaves.
 */
void cpumCpuIdAssertOrder(PCPUMCPUIDLEAF paLeaves, uint32_t cLeaves)
{
    for (uint32_t i = 1; i < cLeaves; i++)
        if (paLeaves[i].uLeaf != paLeaves[i - 1].uLeaf)
            AssertMsg(paLeaves[i].uLeaf > paLeaves[i - 1].uLeaf, ("%#x vs %#x\n", paLeaves[i].uLeaf, paLeaves[i - 1].uLeaf));
        else
        {
            AssertMsg(paLeaves[i].uSubLeaf > paLeaves[i - 1].uSubLeaf,
                      ("%#x: %#x vs %#x\n", paLeaves[i].uLeaf, paLeaves[i].uSubLeaf, paLeaves[i - 1].uSubLeaf));
            AssertMsg(paLeaves[i].fSubLeafMask == paLeaves[i - 1].fSubLeafMask,
                      ("%#x/%#x: %#x vs %#x\n", paLeaves[i].uLeaf, paLeaves[i].uSubLeaf, paLeaves[i].fSubLeafMask, paLeaves[i - 1].fSubLeafMask));
            AssertMsg(paLeaves[i].fFlags == paLeaves[i - 1].fFlags,
                      ("%#x/%#x: %#x vs %#x\n", paLeaves[i].uLeaf, paLeaves[i].uSubLeaf, paLeaves[i].fFlags, paLeaves[i - 1].fFlags));
        }
}
# endif

#endif /* defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86) */

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)

/**
 * Append a CPUID leaf or sub-leaf.
 *
 * ASSUMES linear insertion order, so we'll won't need to do any searching or
 * replace anything.  Use cpumR3CpuIdInsert() for those cases.
 *
 * @returns VINF_SUCCESS or VERR_NO_MEMORY.  On error, *ppaLeaves is freed, so
 *          the caller need do no more work.
 * @param   ppaLeaves       Pointer to the pointer to the array of sorted
 *                          CPUID leaves and sub-leaves.
 * @param   pcLeaves        Where we keep the leaf count for *ppaLeaves.
 * @param   uLeaf           The leaf we're adding.
 * @param   uSubLeaf        The sub-leaf number.
 * @param   fSubLeafMask    The sub-leaf mask.
 * @param   uEax            The EAX value.
 * @param   uEbx            The EBX value.
 * @param   uEcx            The ECX value.
 * @param   uEdx            The EDX value.
 * @param   fFlags          The flags.
 */
static int cpumCollectCpuIdInfoAddOne(PCPUMCPUIDLEAF *ppaLeaves, uint32_t *pcLeaves,
                                      uint32_t uLeaf, uint32_t uSubLeaf, uint32_t fSubLeafMask,
                                      uint32_t uEax, uint32_t uEbx, uint32_t uEcx, uint32_t uEdx, uint32_t fFlags)
{
    if (!cpumCpuIdEnsureSpace(NULL /* pVM */, ppaLeaves, *pcLeaves))
        return VERR_NO_MEMORY;

    PCPUMCPUIDLEAF pNew = &(*ppaLeaves)[*pcLeaves];
    Assert(   *pcLeaves == 0
           || pNew[-1].uLeaf < uLeaf
           || (pNew[-1].uLeaf == uLeaf && pNew[-1].uSubLeaf < uSubLeaf) );

    pNew->uLeaf        = uLeaf;
    pNew->uSubLeaf     = uSubLeaf;
    pNew->fSubLeafMask = fSubLeafMask;
    pNew->uEax         = uEax;
    pNew->uEbx         = uEbx;
    pNew->uEcx         = uEcx;
    pNew->uEdx         = uEdx;
    pNew->fFlags       = fFlags;

    *pcLeaves += 1;
    return VINF_SUCCESS;
}


/**
 * Checks if ECX make a difference when reading a given CPUID leaf.
 *
 * @returns @c true if it does, @c false if it doesn't.
 * @param   uLeaf               The leaf we're reading.
 * @param   pcSubLeaves         Number of sub-leaves accessible via ECX.
 * @param   pfFinalEcxUnchanged Whether ECX is passed thru when going beyond the
 *                              final sub-leaf (for leaf 0xb only).
 */
static bool cpumIsEcxRelevantForCpuIdLeaf(uint32_t uLeaf, uint32_t *pcSubLeaves, bool *pfFinalEcxUnchanged)
{
    *pfFinalEcxUnchanged = false;

    uint32_t auCur[4];
    uint32_t auPrev[4];
    ASMCpuIdExSlow(uLeaf, 0, 0, 0, &auPrev[0], &auPrev[1], &auPrev[2], &auPrev[3]);

    /* Look for sub-leaves. */
    uint32_t uSubLeaf = 1;
    for (;;)
    {
        ASMCpuIdExSlow(uLeaf, 0, uSubLeaf, 0, &auCur[0], &auCur[1], &auCur[2], &auCur[3]);
        if (memcmp(auCur, auPrev, sizeof(auCur)))
            break;

        /* Advance / give up. */
        uSubLeaf++;
        if (uSubLeaf >= 64)
        {
            *pcSubLeaves = 1;
            return false;
        }
    }

    /* Count sub-leaves. */
    uint32_t cMinLeaves = uLeaf == 0xd ? 64 : uLeaf == 7 ? 2 : 0;
    uint32_t cRepeats = 0;
    uSubLeaf = 0;
    for (;;)
    {
        ASMCpuIdExSlow(uLeaf, 0, uSubLeaf, 0, &auCur[0], &auCur[1], &auCur[2], &auCur[3]);

        /* Figuring out when to stop isn't entirely straight forward as we need
           to cover undocumented behavior up to a point and implementation shortcuts. */

        /* 1. Look for more than 4 repeating value sets. */
        if (   auCur[0] == auPrev[0]
            && auCur[1] == auPrev[1]
            && (    auCur[2] == auPrev[2]
                || (   auCur[2]  == uSubLeaf
                    && auPrev[2] == uSubLeaf - 1) )
            && auCur[3] == auPrev[3])
        {
            if (   uLeaf != 0xd
                || uSubLeaf >= 64
                || (   auCur[0] == 0
                    && auCur[1] == 0
                    && auCur[2] == 0
                    && auCur[3] == 0
                    && auPrev[2] == 0) )
                cRepeats++;
            if (cRepeats > 4 && uSubLeaf >= cMinLeaves)
                break;
        }
        else
            cRepeats = 0;

        /* 2. Look for zero values. */
        if (   auCur[0] == 0
            && auCur[1] == 0
            && (auCur[2] == 0 || auCur[2] == uSubLeaf)
            && (auCur[3] == 0 || uLeaf == 0xb /* edx is fixed */)
            && uSubLeaf >= cMinLeaves)
        {
            cRepeats = 0;
            break;
        }

        /* 3. Leaf 0xb level type 0 check. */
        if (   uLeaf == 0xb
            && (auCur[2]  & 0xff00) == 0
            && (auPrev[2] & 0xff00) == 0)
        {
            cRepeats = 0;
            break;
        }

        /* 99. Give up. */
        if (uSubLeaf >= 128)
        {
# ifndef IN_VBOX_CPU_REPORT
            /* Ok, limit it according to the documentation if possible just to
               avoid annoying users with these detection issues. */
            uint32_t cDocLimit = UINT32_MAX;
            if (uLeaf == 0x4)
                cDocLimit = 4;
            else if (uLeaf == 0x7)
                cDocLimit = 1;
            else if (uLeaf == 0xd)
                cDocLimit = 63;
            else if (uLeaf == 0xf)
                cDocLimit = 2;
            if (cDocLimit != UINT32_MAX)
            {
                *pfFinalEcxUnchanged = auCur[2] == uSubLeaf && uLeaf == 0xb;
                *pcSubLeaves = cDocLimit + 3;
                return true;
            }
# endif
            *pcSubLeaves = UINT32_MAX;
            return true;
        }

        /* Advance. */
        uSubLeaf++;
        memcpy(auPrev, auCur, sizeof(auCur));
    }

    /* Standard exit. */
    *pfFinalEcxUnchanged = auCur[2] == uSubLeaf && uLeaf == 0xb;
    *pcSubLeaves = uSubLeaf + 1 - cRepeats;
    if (*pcSubLeaves == 0)
        *pcSubLeaves = 1;
    return true;
}


/**
 * Collects CPUID leaves and sub-leaves, returning a sorted array of them.
 *
 * @returns VBox status code.
 * @param   ppaLeaves           Where to return the array pointer on success.
 *                              Use RTMemFree to release.
 * @param   pcLeaves            Where to return the size of the array on
 *                              success.
 */
VMMDECL(int) CPUMCpuIdCollectLeavesFromX86Host(PCPUMCPUIDLEAF *ppaLeaves, uint32_t *pcLeaves)
{
    *ppaLeaves = NULL;
    *pcLeaves = 0;

    /*
     * Try out various candidates. This must be sorted!
     */
    static struct { uint32_t uMsr; bool fSpecial; } const s_aCandidates[] =
    {
        { UINT32_C(0x00000000), false },
        { UINT32_C(0x10000000), false },
        { UINT32_C(0x20000000), false },
        { UINT32_C(0x30000000), false },
        { UINT32_C(0x40000000), false },
        { UINT32_C(0x50000000), false },
        { UINT32_C(0x60000000), false },
        { UINT32_C(0x70000000), false },
        { UINT32_C(0x80000000), false },
        { UINT32_C(0x80860000), false },
        { UINT32_C(0x8ffffffe), true  },
        { UINT32_C(0x8fffffff), true  },
        { UINT32_C(0x90000000), false },
        { UINT32_C(0xa0000000), false },
        { UINT32_C(0xb0000000), false },
        { UINT32_C(0xc0000000), false },
        { UINT32_C(0xd0000000), false },
        { UINT32_C(0xe0000000), false },
        { UINT32_C(0xf0000000), false },
    };

    for (uint32_t iOuter = 0; iOuter < RT_ELEMENTS(s_aCandidates); iOuter++)
    {
        uint32_t uLeaf = s_aCandidates[iOuter].uMsr;
        uint32_t uEax, uEbx, uEcx, uEdx;
        ASMCpuIdExSlow(uLeaf, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);

        /*
         * Does EAX look like a typical leaf count value?
         */
        if (   uEax         > uLeaf
            && uEax - uLeaf < UINT32_C(0xff)) /* Adjust 0xff limit when exceeded by real HW. */
        {
            /* Yes, dump them. */
            uint32_t cLeaves = uEax - uLeaf + 1;
            while (cLeaves-- > 0)
            {
                ASMCpuIdExSlow(uLeaf, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);

                uint32_t fFlags = 0;

                /* There are currently three known leaves containing an APIC ID
                   that needs EMT specific attention */
                if (uLeaf == 1)
                    fFlags |= CPUMCPUIDLEAF_F_CONTAINS_APIC_ID;
                else if (uLeaf == 0xb && uEcx != 0)
                    fFlags |= CPUMCPUIDLEAF_F_CONTAINS_APIC_ID;
                else if (   uLeaf == UINT32_C(0x8000001e)
                         && (   uEax
                             || uEbx
                             || uEdx
                             || RTX86IsAmdCpu((*ppaLeaves)[0].uEbx, (*ppaLeaves)[0].uEcx, (*ppaLeaves)[0].uEdx)
                             || RTX86IsHygonCpu((*ppaLeaves)[0].uEbx, (*ppaLeaves)[0].uEcx, (*ppaLeaves)[0].uEdx)) )
                    fFlags |= CPUMCPUIDLEAF_F_CONTAINS_APIC_ID;

                /* The APIC bit is per-VCpu and needs flagging. */
                if (uLeaf == 1)
                    fFlags |= CPUMCPUIDLEAF_F_CONTAINS_APIC;
                else if (   uLeaf == UINT32_C(0x80000001)
                         && (   (uEdx & X86_CPUID_AMD_FEATURE_EDX_APIC)
                             || RTX86IsAmdCpu((*ppaLeaves)[0].uEbx, (*ppaLeaves)[0].uEcx, (*ppaLeaves)[0].uEdx)
                             || RTX86IsHygonCpu((*ppaLeaves)[0].uEbx, (*ppaLeaves)[0].uEcx, (*ppaLeaves)[0].uEdx)) )
                    fFlags |= CPUMCPUIDLEAF_F_CONTAINS_APIC;

                /* Check three times here to reduce the chance of CPU migration
                   resulting in false positives with things like the APIC ID. */
                uint32_t cSubLeaves;
                bool fFinalEcxUnchanged;
                if (   cpumIsEcxRelevantForCpuIdLeaf(uLeaf, &cSubLeaves, &fFinalEcxUnchanged)
                    && cpumIsEcxRelevantForCpuIdLeaf(uLeaf, &cSubLeaves, &fFinalEcxUnchanged)
                    && cpumIsEcxRelevantForCpuIdLeaf(uLeaf, &cSubLeaves, &fFinalEcxUnchanged))
                {
                    if (cSubLeaves > (uLeaf == 0xd ? 68U : 16U))
                    {
                        /* This shouldn't happen.  But in case it does, file all
                           relevant details in the release log. */
                        LogRel(("CPUM: VERR_CPUM_TOO_MANY_CPUID_SUBLEAVES! uLeaf=%#x cSubLeaves=%#x\n", uLeaf, cSubLeaves));
                        LogRel(("------------------ dump of problematic sub-leaves -----------------\n"));
                        for (uint32_t uSubLeaf = 0; uSubLeaf < 128; uSubLeaf++)
                        {
                            uint32_t auTmp[4];
                            ASMCpuIdExSlow(uLeaf, 0, uSubLeaf, 0, &auTmp[0], &auTmp[1], &auTmp[2], &auTmp[3]);
                            LogRel(("CPUM: %#010x, %#010x => %#010x %#010x %#010x %#010x\n",
                                    uLeaf, uSubLeaf, auTmp[0], auTmp[1], auTmp[2], auTmp[3]));
                        }
                        LogRel(("----------------- dump of what we've found so far -----------------\n"));
                        for (uint32_t i = 0 ; i < *pcLeaves; i++)
                            LogRel(("CPUM: %#010x, %#010x/%#010x => %#010x %#010x %#010x %#010x\n",
                                    (*ppaLeaves)[i].uLeaf, (*ppaLeaves)[i].uSubLeaf,  (*ppaLeaves)[i].fSubLeafMask,
                                    (*ppaLeaves)[i].uEax, (*ppaLeaves)[i].uEbx, (*ppaLeaves)[i].uEcx, (*ppaLeaves)[i].uEdx));
                        LogRel(("\nPlease create a defect on virtualbox.org and attach this log file!\n\n"));
                        return VERR_CPUM_TOO_MANY_CPUID_SUBLEAVES;
                    }

                    if (fFinalEcxUnchanged)
                        fFlags |= CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES;

                    for (uint32_t uSubLeaf = 0; uSubLeaf < cSubLeaves; uSubLeaf++)
                    {
                        ASMCpuIdExSlow(uLeaf, 0, uSubLeaf, 0, &uEax, &uEbx, &uEcx, &uEdx);
                        int rc = cpumCollectCpuIdInfoAddOne(ppaLeaves, pcLeaves,
                                                            uLeaf, uSubLeaf, UINT32_MAX, uEax, uEbx, uEcx, uEdx, fFlags);
                        if (RT_FAILURE(rc))
                            return rc;
                    }
                }
                else
                {
                    int rc = cpumCollectCpuIdInfoAddOne(ppaLeaves, pcLeaves, uLeaf, 0, 0, uEax, uEbx, uEcx, uEdx, fFlags);
                    if (RT_FAILURE(rc))
                        return rc;
                }

                /* next */
                uLeaf++;
            }
        }
        /*
         * Special CPUIDs needs special handling as they don't follow the
         * leaf count principle used above.
         */
        else if (s_aCandidates[iOuter].fSpecial)
        {
            bool fKeep = false;
            if (uLeaf == 0x8ffffffe && uEax == UINT32_C(0x00494544))
                fKeep = true;
            else if (   uLeaf == 0x8fffffff
                     && RT_C_IS_PRINT(RT_BYTE1(uEax))
                     && RT_C_IS_PRINT(RT_BYTE2(uEax))
                     && RT_C_IS_PRINT(RT_BYTE3(uEax))
                     && RT_C_IS_PRINT(RT_BYTE4(uEax))
                     && RT_C_IS_PRINT(RT_BYTE1(uEbx))
                     && RT_C_IS_PRINT(RT_BYTE2(uEbx))
                     && RT_C_IS_PRINT(RT_BYTE3(uEbx))
                     && RT_C_IS_PRINT(RT_BYTE4(uEbx))
                     && RT_C_IS_PRINT(RT_BYTE1(uEcx))
                     && RT_C_IS_PRINT(RT_BYTE2(uEcx))
                     && RT_C_IS_PRINT(RT_BYTE3(uEcx))
                     && RT_C_IS_PRINT(RT_BYTE4(uEcx))
                     && RT_C_IS_PRINT(RT_BYTE1(uEdx))
                     && RT_C_IS_PRINT(RT_BYTE2(uEdx))
                     && RT_C_IS_PRINT(RT_BYTE3(uEdx))
                     && RT_C_IS_PRINT(RT_BYTE4(uEdx)) )
                fKeep = true;
            if (fKeep)
            {
                int rc = cpumCollectCpuIdInfoAddOne(ppaLeaves, pcLeaves, uLeaf, 0, 0, uEax, uEbx, uEcx, uEdx, 0);
                if (RT_FAILURE(rc))
                    return rc;
            }
        }
    }

# ifdef VBOX_STRICT
    cpumCpuIdAssertOrder(*ppaLeaves, *pcLeaves);
# endif
    return VINF_SUCCESS;
}

#endif /* RT_ARCH_X86 || RT_ARCH_AMD64 */

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86)
/**
 * Detect the CPU vendor give n the
 *
 * @returns The vendor.
 * @param   uEAX                EAX from CPUID(0).
 * @param   uEBX                EBX from CPUID(0).
 * @param   uECX                ECX from CPUID(0).
 * @param   uEDX                EDX from CPUID(0).
 */
VMMDECL(CPUMCPUVENDOR) CPUMCpuIdDetectX86VendorEx(uint32_t uEAX, uint32_t uEBX, uint32_t uECX, uint32_t uEDX)
{
    if (RTX86IsValidStdRange(uEAX))
    {
        if (RTX86IsAmdCpu(uEBX, uECX, uEDX))
            return CPUMCPUVENDOR_AMD;

        if (RTX86IsIntelCpu(uEBX, uECX, uEDX))
            return CPUMCPUVENDOR_INTEL;

        if (RTX86IsViaCentaurCpu(uEBX, uECX, uEDX))
            return CPUMCPUVENDOR_VIA;

        if (RTX86IsShanghaiCpu(uEBX, uECX, uEDX))
            return CPUMCPUVENDOR_SHANGHAI;

        if (   uEBX == UINT32_C(0x69727943) /* CyrixInstead */
            && uECX == UINT32_C(0x64616574)
            && uEDX == UINT32_C(0x736E4978))
            return CPUMCPUVENDOR_CYRIX;

        if (RTX86IsHygonCpu(uEBX, uECX, uEDX))
            return CPUMCPUVENDOR_HYGON;

        /* "Geode by NSC", example: family 5, model 9.  */

        /** @todo detect the other buggers... */
    }

    return CPUMCPUVENDOR_UNKNOWN;
}
#endif /* defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86) */


/**
 * Translates a CPU vendor enum value into the corresponding string constant.
 *
 * The named can be prefixed with 'CPUMCPUVENDOR_' to construct a valid enum
 * value name.  This can be useful when generating code.
 *
 * @returns Read only name string.
 * @param   enmVendor           The CPU vendor value.
 */
VMMDECL(const char *) CPUMCpuVendorName(CPUMCPUVENDOR enmVendor)
{
    switch (enmVendor)
    {
        case CPUMCPUVENDOR_INTEL:       return "INTEL";
        case CPUMCPUVENDOR_AMD:         return "AMD";
        case CPUMCPUVENDOR_VIA:         return "VIA";
        case CPUMCPUVENDOR_CYRIX:       return "CYRIX";
        case CPUMCPUVENDOR_SHANGHAI:    return "SHANGHAI";
        case CPUMCPUVENDOR_HYGON:       return "HYGON";

        case CPUMCPUVENDOR_ARM:         return "ARM";
        case CPUMCPUVENDOR_BROADCOM:    return "Broadcom";
        case CPUMCPUVENDOR_QUALCOMM:    return "Qualecomm";
        case CPUMCPUVENDOR_APPLE:       return "Apple";
        case CPUMCPUVENDOR_AMPERE:      return "Ampere";

        case CPUMCPUVENDOR_UNKNOWN:     return "UNKNOWN";

        case CPUMCPUVENDOR_INVALID:
        case CPUMCPUVENDOR_32BIT_HACK:
            break;
    }
    return "Invalid-cpu-vendor";
}

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86)

static PCCPUMCPUIDLEAF cpumCpuIdFindLeaf(PCCPUMCPUIDLEAF paLeaves, uint32_t cLeaves, uint32_t uLeaf)
{
    /* Could do binary search, doing linear now because I'm lazy. */
    PCCPUMCPUIDLEAF pLeaf = paLeaves;
    while (cLeaves-- > 0)
    {
        if (pLeaf->uLeaf == uLeaf)
            return pLeaf;
        pLeaf++;
    }
    return NULL;
}


static PCCPUMCPUIDLEAF cpumCpuIdFindLeafEx(PCCPUMCPUIDLEAF paLeaves, uint32_t cLeaves, uint32_t uLeaf, uint32_t uSubLeaf)
{
    PCCPUMCPUIDLEAF pLeaf = cpumCpuIdFindLeaf(paLeaves, cLeaves, uLeaf);
    if (   !pLeaf
        || pLeaf->uSubLeaf != (uSubLeaf & pLeaf->fSubLeafMask))
        return pLeaf;

    /* Linear sub-leaf search. Lazy as usual. */
    cLeaves -= pLeaf - paLeaves;
    while (   cLeaves-- > 0
           && pLeaf->uLeaf == uLeaf)
    {
        if (pLeaf->uSubLeaf == (uSubLeaf & pLeaf->fSubLeafMask))
            return pLeaf;
        pLeaf++;
    }

    return NULL;
}


/**
 * This explodes the VMX MSRs into the feature structure.
 *
 * The feature structure must be otherwise fully populated.
 */
DECLHIDDEN(void) cpumCpuIdExplodeFeaturesX86Vmx(PCVMXMSRS pVmxMsrs, CPUMFEATURESX86 *pFeatures)
{
    Assert(pVmxMsrs);
    Assert(pFeatures);
    Assert(pFeatures->fVmx);

    /* Basic information. */
    bool const fVmxTrueMsrs = RT_BOOL(pVmxMsrs->u64Basic & VMX_BF_BASIC_TRUE_CTLS_MASK);
    {
        uint64_t const u64Basic = pVmxMsrs->u64Basic;
        pFeatures->fVmxInsOutInfo            = RT_BF_GET(u64Basic, VMX_BF_BASIC_VMCS_INS_OUTS);
    }

    /* Pin-based VM-execution controls. */
    {
        uint32_t const fPinCtls = fVmxTrueMsrs ? pVmxMsrs->TruePinCtls.n.allowed1 : pVmxMsrs->PinCtls.n.allowed1;
        pFeatures->fVmxExtIntExit            = RT_BOOL(fPinCtls & VMX_PIN_CTLS_EXT_INT_EXIT);
        pFeatures->fVmxNmiExit               = RT_BOOL(fPinCtls & VMX_PIN_CTLS_NMI_EXIT);
        pFeatures->fVmxVirtNmi               = RT_BOOL(fPinCtls & VMX_PIN_CTLS_VIRT_NMI);
        pFeatures->fVmxPreemptTimer          = RT_BOOL(fPinCtls & VMX_PIN_CTLS_PREEMPT_TIMER);
        pFeatures->fVmxPostedInt             = RT_BOOL(fPinCtls & VMX_PIN_CTLS_POSTED_INT);
    }

    /* Processor-based VM-execution controls. */
    {
        uint32_t const fProcCtls = fVmxTrueMsrs ? pVmxMsrs->TrueProcCtls.n.allowed1 : pVmxMsrs->ProcCtls.n.allowed1;
        pFeatures->fVmxIntWindowExit         = RT_BOOL(fProcCtls & VMX_PROC_CTLS_INT_WINDOW_EXIT);
        pFeatures->fVmxTscOffsetting         = RT_BOOL(fProcCtls & VMX_PROC_CTLS_USE_TSC_OFFSETTING);
        pFeatures->fVmxHltExit               = RT_BOOL(fProcCtls & VMX_PROC_CTLS_HLT_EXIT);
        pFeatures->fVmxInvlpgExit            = RT_BOOL(fProcCtls & VMX_PROC_CTLS_INVLPG_EXIT);
        pFeatures->fVmxMwaitExit             = RT_BOOL(fProcCtls & VMX_PROC_CTLS_MWAIT_EXIT);
        pFeatures->fVmxRdpmcExit             = RT_BOOL(fProcCtls & VMX_PROC_CTLS_RDPMC_EXIT);
        pFeatures->fVmxRdtscExit             = RT_BOOL(fProcCtls & VMX_PROC_CTLS_RDTSC_EXIT);
        pFeatures->fVmxCr3LoadExit           = RT_BOOL(fProcCtls & VMX_PROC_CTLS_CR3_LOAD_EXIT);
        pFeatures->fVmxCr3StoreExit          = RT_BOOL(fProcCtls & VMX_PROC_CTLS_CR3_STORE_EXIT);
        pFeatures->fVmxTertiaryExecCtls      = RT_BOOL(fProcCtls & VMX_PROC_CTLS_USE_TERTIARY_CTLS);
        pFeatures->fVmxCr8LoadExit           = RT_BOOL(fProcCtls & VMX_PROC_CTLS_CR8_LOAD_EXIT);
        pFeatures->fVmxCr8StoreExit          = RT_BOOL(fProcCtls & VMX_PROC_CTLS_CR8_STORE_EXIT);
        pFeatures->fVmxUseTprShadow          = RT_BOOL(fProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW);
        pFeatures->fVmxNmiWindowExit         = RT_BOOL(fProcCtls & VMX_PROC_CTLS_NMI_WINDOW_EXIT);
        pFeatures->fVmxMovDRxExit            = RT_BOOL(fProcCtls & VMX_PROC_CTLS_MOV_DR_EXIT);
        pFeatures->fVmxUncondIoExit          = RT_BOOL(fProcCtls & VMX_PROC_CTLS_UNCOND_IO_EXIT);
        pFeatures->fVmxUseIoBitmaps          = RT_BOOL(fProcCtls & VMX_PROC_CTLS_USE_IO_BITMAPS);
        pFeatures->fVmxMonitorTrapFlag       = RT_BOOL(fProcCtls & VMX_PROC_CTLS_MONITOR_TRAP_FLAG);
        pFeatures->fVmxUseMsrBitmaps         = RT_BOOL(fProcCtls & VMX_PROC_CTLS_USE_MSR_BITMAPS);
        pFeatures->fVmxMonitorExit           = RT_BOOL(fProcCtls & VMX_PROC_CTLS_MONITOR_EXIT);
        pFeatures->fVmxPauseExit             = RT_BOOL(fProcCtls & VMX_PROC_CTLS_PAUSE_EXIT);
        pFeatures->fVmxSecondaryExecCtls     = RT_BOOL(fProcCtls & VMX_PROC_CTLS_USE_SECONDARY_CTLS);
    }

    /* Secondary processor-based VM-execution controls. */
    {
        uint32_t const fProcCtls2 = pFeatures->fVmxSecondaryExecCtls ? pVmxMsrs->ProcCtls2.n.allowed1 : 0;
        pFeatures->fVmxVirtApicAccess        = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_VIRT_APIC_ACCESS);
        pFeatures->fVmxEpt                   = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_EPT);
        pFeatures->fVmxDescTableExit         = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_DESC_TABLE_EXIT);
        pFeatures->fVmxRdtscp                = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_RDTSCP);
        pFeatures->fVmxVirtX2ApicMode        = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_VIRT_X2APIC_MODE);
        pFeatures->fVmxVpid                  = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_VPID);
        pFeatures->fVmxWbinvdExit            = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_WBINVD_EXIT);
        pFeatures->fVmxUnrestrictedGuest     = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_UNRESTRICTED_GUEST);
        pFeatures->fVmxApicRegVirt           = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_APIC_REG_VIRT);
        pFeatures->fVmxVirtIntDelivery       = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY);
        pFeatures->fVmxPauseLoopExit         = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_PAUSE_LOOP_EXIT);
        pFeatures->fVmxRdrandExit            = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_RDRAND_EXIT);
        pFeatures->fVmxInvpcid               = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_INVPCID);
        pFeatures->fVmxVmFunc                = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_VMFUNC);
        pFeatures->fVmxVmcsShadowing         = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_VMCS_SHADOWING);
        pFeatures->fVmxRdseedExit            = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_RDSEED_EXIT);
        pFeatures->fVmxPml                   = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_PML);
        pFeatures->fVmxEptXcptVe             = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_EPT_XCPT_VE);
        pFeatures->fVmxConcealVmxFromPt      = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_CONCEAL_VMX_FROM_PT);
        pFeatures->fVmxXsavesXrstors         = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_XSAVES_XRSTORS);
        pFeatures->fVmxPasidTranslate        = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_PASID_TRANSLATE);
        pFeatures->fVmxModeBasedExecuteEpt   = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_MODE_BASED_EPT_PERM);
        pFeatures->fVmxSppEpt                = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_SPP_EPT);
        pFeatures->fVmxPtEpt                 = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_PT_EPT);
        pFeatures->fVmxUseTscScaling         = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_TSC_SCALING);
        pFeatures->fVmxUserWaitPause         = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_USER_WAIT_PAUSE);
        pFeatures->fVmxPconfig               = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_PCONFIG);
        pFeatures->fVmxEnclvExit             = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_ENCLV_EXIT);
        pFeatures->fVmxBusLockDetect         = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_BUS_LOCK_DETECT);
        pFeatures->fVmxInstrTimeout          = RT_BOOL(fProcCtls2 & VMX_PROC_CTLS2_INSTR_TIMEOUT);
    }

    /* Tertiary processor-based VM-execution controls. */
    {
        uint64_t const fProcCtls3 = pFeatures->fVmxTertiaryExecCtls ? pVmxMsrs->u64ProcCtls3 : 0;
        pFeatures->fVmxLoadIwKeyExit         = RT_BOOL(fProcCtls3 & VMX_PROC_CTLS3_LOADIWKEY_EXIT);
        pFeatures->fVmxHlat                  = RT_BOOL(fProcCtls3 & VMX_PROC_CTLS3_HLAT);
        pFeatures->fVmxEptPagingWrite        = RT_BOOL(fProcCtls3 & VMX_PROC_CTLS3_EPT_PAGING_WRITE);
        pFeatures->fVmxGstPagingVerify       = RT_BOOL(fProcCtls3 & VMX_PROC_CTLS3_GST_PAGING_VERIFY);
        pFeatures->fVmxIpiVirt               = RT_BOOL(fProcCtls3 & VMX_PROC_CTLS3_IPI_VIRT);
        pFeatures->fVmxVirtSpecCtrl          = RT_BOOL(fProcCtls3 & VMX_PROC_CTLS3_VIRT_SPEC_CTRL);
    }

    /* VM-exit controls. */
    {
        uint32_t const fExitCtls = fVmxTrueMsrs ? pVmxMsrs->TrueExitCtls.n.allowed1 : pVmxMsrs->ExitCtls.n.allowed1;
        pFeatures->fVmxExitSaveDebugCtls     = RT_BOOL(fExitCtls & VMX_EXIT_CTLS_SAVE_DEBUG);
        pFeatures->fVmxHostAddrSpaceSize     = RT_BOOL(fExitCtls & VMX_EXIT_CTLS_HOST_ADDR_SPACE_SIZE);
        pFeatures->fVmxExitAckExtInt         = RT_BOOL(fExitCtls & VMX_EXIT_CTLS_ACK_EXT_INT);
        pFeatures->fVmxExitSavePatMsr        = RT_BOOL(fExitCtls & VMX_EXIT_CTLS_SAVE_PAT_MSR);
        pFeatures->fVmxExitLoadPatMsr        = RT_BOOL(fExitCtls & VMX_EXIT_CTLS_LOAD_PAT_MSR);
        pFeatures->fVmxExitSaveEferMsr       = RT_BOOL(fExitCtls & VMX_EXIT_CTLS_SAVE_EFER_MSR);
        pFeatures->fVmxExitLoadEferMsr       = RT_BOOL(fExitCtls & VMX_EXIT_CTLS_LOAD_EFER_MSR);
        pFeatures->fVmxSavePreemptTimer      = RT_BOOL(fExitCtls & VMX_EXIT_CTLS_SAVE_PREEMPT_TIMER);
        pFeatures->fVmxSecondaryExitCtls     = RT_BOOL(fExitCtls & VMX_EXIT_CTLS_USE_SECONDARY_CTLS);
    }

    /* VM-entry controls. */
    {
        uint32_t const fEntryCtls = fVmxTrueMsrs ? pVmxMsrs->TrueEntryCtls.n.allowed1 : pVmxMsrs->EntryCtls.n.allowed1;
        pFeatures->fVmxEntryLoadDebugCtls    = RT_BOOL(fEntryCtls & VMX_ENTRY_CTLS_LOAD_DEBUG);
        pFeatures->fVmxIa32eModeGuest        = RT_BOOL(fEntryCtls & VMX_ENTRY_CTLS_IA32E_MODE_GUEST);
        pFeatures->fVmxEntryLoadEferMsr      = RT_BOOL(fEntryCtls & VMX_ENTRY_CTLS_LOAD_EFER_MSR);
        pFeatures->fVmxEntryLoadPatMsr       = RT_BOOL(fEntryCtls & VMX_ENTRY_CTLS_LOAD_PAT_MSR);
    }

    /* Miscellaneous data. */
    {
        uint32_t const fMiscData = pVmxMsrs->u64Misc;
        pFeatures->fVmxExitSaveEferLma       = RT_BOOL(fMiscData & VMX_MISC_EXIT_SAVE_EFER_LMA);
        pFeatures->fVmxPt                    = RT_BOOL(fMiscData & VMX_MISC_INTEL_PT);
        pFeatures->fVmxVmwriteAll            = RT_BOOL(fMiscData & VMX_MISC_VMWRITE_ALL);
        pFeatures->fVmxEntryInjectSoftInt    = RT_BOOL(fMiscData & VMX_MISC_ENTRY_INJECT_SOFT_INT);
    }
}


/**
 * Call cpumCpuIdExplodeFeaturesX86Vmx with SUPHWVIRTMSRS instead of VMXMSRS.
 */
DECLHIDDEN(void) cpumCpuIdExplodeFeaturesX86VmxFromSupMsrs(PCSUPHWVIRTMSRS pMsrs, CPUMFEATURESX86 *pFeatures)
{
    /* This is a bit stupid as the structures are almost identical
       (SUPHWVIRTMSRS has one extra member, u64FeatCtrl). */
    VMXMSRS VmxMsrs;
    VmxMsrs.u64Basic        = pMsrs->u.vmx.u64Basic;
    VmxMsrs.PinCtls.u       = pMsrs->u.vmx.PinCtls.u;
    VmxMsrs.ProcCtls.u      = pMsrs->u.vmx.ProcCtls.u;
    VmxMsrs.ProcCtls2.u     = pMsrs->u.vmx.ProcCtls2.u;
    VmxMsrs.ExitCtls.u      = pMsrs->u.vmx.ExitCtls.u;
    VmxMsrs.EntryCtls.u     = pMsrs->u.vmx.EntryCtls.u;
    VmxMsrs.TruePinCtls.u   = pMsrs->u.vmx.TruePinCtls.u;
    VmxMsrs.TrueProcCtls.u  = pMsrs->u.vmx.TrueProcCtls.u;
    VmxMsrs.TrueEntryCtls.u = pMsrs->u.vmx.TrueEntryCtls.u;
    VmxMsrs.TrueExitCtls.u  = pMsrs->u.vmx.TrueExitCtls.u;
    VmxMsrs.u64Misc         = pMsrs->u.vmx.u64Misc;
    VmxMsrs.u64Cr0Fixed0    = pMsrs->u.vmx.u64Cr0Fixed0;
    VmxMsrs.u64Cr0Fixed1    = pMsrs->u.vmx.u64Cr0Fixed1;
    VmxMsrs.u64Cr4Fixed0    = pMsrs->u.vmx.u64Cr4Fixed0;
    VmxMsrs.u64Cr4Fixed1    = pMsrs->u.vmx.u64Cr4Fixed1;
    VmxMsrs.u64VmcsEnum     = pMsrs->u.vmx.u64VmcsEnum;
    VmxMsrs.u64VmFunc       = pMsrs->u.vmx.u64VmFunc;
    VmxMsrs.u64EptVpidCaps  = pMsrs->u.vmx.u64EptVpidCaps;
    VmxMsrs.u64ProcCtls3    = pMsrs->u.vmx.u64ProcCtls3;
    VmxMsrs.u64ExitCtls2    = pMsrs->u.vmx.u64ExitCtls2;
    RT_ZERO(VmxMsrs.a_u64Reserved);

    cpumCpuIdExplodeFeaturesX86Vmx(&VmxMsrs, pFeatures);
}


void cpumCpuIdExplodeFeaturesX86SetSummaryBits(CPUMFEATURESX86 *pFeatures)
{
    /* Summary or all bits indicating the presence of the IA32_SPEC_CTRL MSR. */
    pFeatures->fSpecCtrlMsr = pFeatures->fIbrs
                            | pFeatures->fStibp
                            | pFeatures->fSsbd
                            | pFeatures->fPsfd
                            | pFeatures->fIpredCtrl
                            | pFeatures->fRrsbaCtrl
                            | pFeatures->fDdpdU
                            | pFeatures->fBhiCtrl
                            ;
}


VMMDECL(int) CPUMCpuIdExplodeFeaturesX86(PCCPUMCPUIDLEAF paLeaves, uint32_t cLeaves, CPUMFEATURESX86 *pFeatures)
{
    RT_ZERO(*pFeatures);
    if (cLeaves >= 2)
    {
        AssertLogRelReturn(paLeaves[0].uLeaf == 0, VERR_CPUM_IPE_1);
        AssertLogRelReturn(paLeaves[1].uLeaf == 1, VERR_CPUM_IPE_1);
        PCCPUMCPUIDLEAF const pStd0Leaf = cpumCpuIdFindLeafEx(paLeaves, cLeaves, 0, 0);
        AssertLogRelReturn(pStd0Leaf, VERR_CPUM_IPE_1);
        PCCPUMCPUIDLEAF const pStd1Leaf = cpumCpuIdFindLeafEx(paLeaves, cLeaves, 1, 0);
        AssertLogRelReturn(pStd1Leaf, VERR_CPUM_IPE_1);

        pFeatures->enmCpuVendor = CPUMCpuIdDetectX86VendorEx(pStd0Leaf->uEax,
                                                             pStd0Leaf->uEbx,
                                                             pStd0Leaf->uEcx,
                                                             pStd0Leaf->uEdx);
        pFeatures->uFamily      = RTX86GetCpuFamily(pStd1Leaf->uEax);
        pFeatures->uModel       = RTX86GetCpuModel(pStd1Leaf->uEax, pFeatures->enmCpuVendor == CPUMCPUVENDOR_INTEL);
        pFeatures->uStepping    = RTX86GetCpuStepping(pStd1Leaf->uEax);
        pFeatures->enmMicroarch = CPUMCpuIdDetermineX86MicroarchEx((CPUMCPUVENDOR)pFeatures->enmCpuVendor,
                                                                   pFeatures->uFamily,
                                                                   pFeatures->uModel,
                                                                   pFeatures->uStepping);

        PCCPUMCPUIDLEAF const pExtLeaf8 = cpumCpuIdFindLeaf(paLeaves, cLeaves, 0x80000008);
        if (pExtLeaf8)
        {
            pFeatures->cMaxPhysAddrWidth   = pExtLeaf8->uEax & 0xff;
            pFeatures->cMaxLinearAddrWidth = (pExtLeaf8->uEax >> 8) & 0xff;
        }
        else if (pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_PSE36)
        {
            pFeatures->cMaxPhysAddrWidth   = 36;
            pFeatures->cMaxLinearAddrWidth = 36;
        }
        else
        {
            pFeatures->cMaxPhysAddrWidth   = 32;
            pFeatures->cMaxLinearAddrWidth = 32;
        }

        /* Standard features. */
        pFeatures->fMsr                 = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_MSR);
        pFeatures->fApic                = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_APIC);
        pFeatures->fX2Apic              = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_X2APIC);
        pFeatures->fPse                 = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_PSE);
        pFeatures->fPse36               = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_PSE36);
        pFeatures->fPae                 = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_PAE);
        pFeatures->fPge                 = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_PGE);
        pFeatures->fPat                 = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_PAT);
        pFeatures->fFxSaveRstor         = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_FXSR);
        pFeatures->fXSaveRstor          = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_XSAVE);
        pFeatures->fOpSysXSaveRstor     = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_OSXSAVE);
        pFeatures->fMmx                 = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_MMX);
        pFeatures->fSse                 = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_SSE);
        pFeatures->fSse2                = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_SSE2);
        pFeatures->fSse3                = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_SSE3);
        pFeatures->fSsse3               = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_SSSE3);
        pFeatures->fFma                 = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_FMA);
        pFeatures->fSse41               = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_SSE4_1);
        pFeatures->fSse42               = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_SSE4_2);
        pFeatures->fAesNi               = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_AES);
        pFeatures->fAvx                 = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_AVX);
        pFeatures->fTsc                 = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_TSC);
        pFeatures->fSysEnter            = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_SEP);
        pFeatures->fMtrr                = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_MTRR);
        pFeatures->fHypervisorPresent   = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_HVP);
        pFeatures->fMonitorMWait        = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_MONITOR);
        pFeatures->fCmpXchg8b           = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_CX8);
        pFeatures->fCmpXchg16b          = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_CX16);
        pFeatures->fClFlush             = RT_BOOL(pStd1Leaf->uEdx & X86_CPUID_FEATURE_EDX_CLFSH);
        pFeatures->fPcid                = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_PCID);
        pFeatures->fPopCnt              = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_POPCNT);
        pFeatures->fRdRand              = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_RDRAND);
        pFeatures->fVmx                 = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_VMX);
        pFeatures->fPclMul              = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_PCLMUL);
        pFeatures->fMovBe               = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_MOVBE);
        pFeatures->fF16c                = RT_BOOL(pStd1Leaf->uEcx & X86_CPUID_FEATURE_ECX_F16C);

        /* Structured extended features. */
        PCCPUMCPUIDLEAF const pSxfLeaf0 = cpumCpuIdFindLeafEx(paLeaves, cLeaves, 7, 0);
        if (pSxfLeaf0)
        {
            pFeatures->fFsGsBase            = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_FSGSBASE);
            pFeatures->fAvx2                = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_AVX2);
            pFeatures->fAvx512Foundation    = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_AVX512F);
            pFeatures->fClFlushOpt          = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_CLFLUSHOPT);
            pFeatures->fInvpcid             = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_INVPCID);
            pFeatures->fBmi1                = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_BMI1);
            pFeatures->fBmi2                = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_BMI2);
            pFeatures->fRdSeed              = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_RDSEED);
            pFeatures->fHle                 = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_HLE);
            pFeatures->fRtm                 = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_RTM);
            pFeatures->fSha                 = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_SHA);
            pFeatures->fAdx                 = RT_BOOL(pSxfLeaf0->uEbx & X86_CPUID_STEXT_FEATURE_EBX_ADX);

            pFeatures->fIbpb                = RT_BOOL(pSxfLeaf0->uEdx & X86_CPUID_STEXT_FEATURE_EDX_IBRS_IBPB);
            pFeatures->fIbrs                = pFeatures->fIbpb;
            pFeatures->fStibp               = RT_BOOL(pSxfLeaf0->uEdx & X86_CPUID_STEXT_FEATURE_EDX_STIBP);
            pFeatures->fSsbd                = RT_BOOL(pSxfLeaf0->uEdx & X86_CPUID_STEXT_FEATURE_EDX_SSBD);
            pFeatures->fFlushCmd            = RT_BOOL(pSxfLeaf0->uEdx & X86_CPUID_STEXT_FEATURE_EDX_FLUSH_CMD);
            pFeatures->fArchCap             = RT_BOOL(pSxfLeaf0->uEdx & X86_CPUID_STEXT_FEATURE_EDX_ARCHCAP);
            pFeatures->fCoreCap             = RT_BOOL(pSxfLeaf0->uEdx & X86_CPUID_STEXT_FEATURE_EDX_CORECAP);
            pFeatures->fMdsClear            = RT_BOOL(pSxfLeaf0->uEdx & X86_CPUID_STEXT_FEATURE_EDX_MD_CLEAR);
        }
        PCCPUMCPUIDLEAF const pSxfLeaf2 = cpumCpuIdFindLeafEx(paLeaves, cLeaves, 7, 2);
        if (pSxfLeaf2)
        {
            pFeatures->fPsfd                = RT_BOOL(pSxfLeaf2->uEdx & X86_CPUID_STEXT_FEATURE_2_EDX_PSFD);
            pFeatures->fIpredCtrl           = RT_BOOL(pSxfLeaf2->uEdx & X86_CPUID_STEXT_FEATURE_2_EDX_IPRED_CTRL);
            pFeatures->fRrsbaCtrl           = RT_BOOL(pSxfLeaf2->uEdx & X86_CPUID_STEXT_FEATURE_2_EDX_RRSBA_CTRL);
            pFeatures->fDdpdU               = RT_BOOL(pSxfLeaf2->uEdx & X86_CPUID_STEXT_FEATURE_2_EDX_DDPD_U);
            pFeatures->fBhiCtrl             = RT_BOOL(pSxfLeaf2->uEdx & X86_CPUID_STEXT_FEATURE_2_EDX_BHI_CTRL);
            pFeatures->fMcdtNo              = RT_BOOL(pSxfLeaf2->uEdx & X86_CPUID_STEXT_FEATURE_2_EDX_MCDT_NO);
            pFeatures->fUcLockDis           = RT_BOOL(pSxfLeaf2->uEdx & X86_CPUID_STEXT_FEATURE_2_EDX_UC_LOCK_DIS);
            pFeatures->fMonitorMitgNo       = RT_BOOL(pSxfLeaf2->uEdx & X86_CPUID_STEXT_FEATURE_2_EDX_MONITOR_MITG_NO);
        }

        /* MWAIT/MONITOR leaf. */
        PCCPUMCPUIDLEAF const pMWaitLeaf = cpumCpuIdFindLeaf(paLeaves, cLeaves, 5);
        if (pMWaitLeaf)
            pFeatures->fMWaitExtensions = (pMWaitLeaf->uEcx & (X86_CPUID_MWAIT_ECX_EXT | X86_CPUID_MWAIT_ECX_BREAKIRQIF0))
                                        ==                    (X86_CPUID_MWAIT_ECX_EXT | X86_CPUID_MWAIT_ECX_BREAKIRQIF0);

        /* Extended features. */
        PCCPUMCPUIDLEAF const pExtLeaf  = cpumCpuIdFindLeaf(paLeaves, cLeaves, 0x80000001);
        if (pExtLeaf)
        {
            pFeatures->fLongMode        = RT_BOOL(pExtLeaf->uEdx & X86_CPUID_EXT_FEATURE_EDX_LONG_MODE);
            pFeatures->fSysCall         = RT_BOOL(pExtLeaf->uEdx & X86_CPUID_EXT_FEATURE_EDX_SYSCALL);
            pFeatures->fNoExecute       = RT_BOOL(pExtLeaf->uEdx & X86_CPUID_EXT_FEATURE_EDX_NX);
            pFeatures->fLahfSahf        = RT_BOOL(pExtLeaf->uEcx & X86_CPUID_EXT_FEATURE_ECX_LAHF_SAHF);
            pFeatures->fRdTscP          = RT_BOOL(pExtLeaf->uEdx & X86_CPUID_EXT_FEATURE_EDX_RDTSCP);
            pFeatures->fMovCr8In32Bit   = RT_BOOL(pExtLeaf->uEcx & X86_CPUID_AMD_FEATURE_ECX_CMPL);
            pFeatures->f3DNow           = RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_3DNOW);
            pFeatures->f3DNowPrefetch   = (pExtLeaf->uEcx & X86_CPUID_AMD_FEATURE_ECX_3DNOWPRF)
                                       || (pExtLeaf->uEdx & (  X86_CPUID_EXT_FEATURE_EDX_LONG_MODE
                                                             | X86_CPUID_AMD_FEATURE_EDX_3DNOW));
            pFeatures->fAbm             = RT_BOOL(pExtLeaf->uEcx & X86_CPUID_AMD_FEATURE_ECX_ABM);
        }

        /* VMX (VMXON, VMCS region and related data structures) physical address width (depends on long-mode). */
        pFeatures->cVmxMaxPhysAddrWidth = pFeatures->fLongMode ? pFeatures->cMaxPhysAddrWidth : 32;

        if (   pExtLeaf
            && (   pFeatures->enmCpuVendor == CPUMCPUVENDOR_AMD
                || pFeatures->enmCpuVendor == CPUMCPUVENDOR_HYGON))
        {
            /* AMD features. */
            pFeatures->fMsr            |= RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_MSR);
            pFeatures->fApic           |= RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_APIC);
            pFeatures->fPse            |= RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_PSE);
            pFeatures->fPse36          |= RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_PSE36);
            pFeatures->fPae            |= RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_PAE);
            pFeatures->fPge            |= RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_PGE);
            pFeatures->fPat            |= RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_PAT);
            pFeatures->fFxSaveRstor    |= RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_FXSR);
            pFeatures->fMmx            |= RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_MMX);
            pFeatures->fTsc            |= RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_TSC);
            pFeatures->fAmdMmxExts      = RT_BOOL(pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_AXMMX);
            pFeatures->fXop             = RT_BOOL(pExtLeaf->uEcx & X86_CPUID_AMD_FEATURE_ECX_XOP);
            pFeatures->fTbm             = RT_BOOL(pExtLeaf->uEcx & X86_CPUID_AMD_FEATURE_ECX_TBM);
            pFeatures->fSvm             = RT_BOOL(pExtLeaf->uEcx & X86_CPUID_AMD_FEATURE_ECX_SVM);

            if (pExtLeaf8)
            {
                pFeatures->fIbpb      |= RT_BOOL(pExtLeaf8->uEbx & X86_CPUID_AMD_EFEID_EBX_IBPB);
                pFeatures->fIbrs      |= RT_BOOL(pExtLeaf8->uEbx & X86_CPUID_AMD_EFEID_EBX_IBRS);
                pFeatures->fStibp     |= RT_BOOL(pExtLeaf8->uEbx & X86_CPUID_AMD_EFEID_EBX_STIBP);
                pFeatures->fSsbd      |= RT_BOOL(pExtLeaf8->uEbx & X86_CPUID_AMD_EFEID_EBX_SPEC_CTRL_SSBD);
                pFeatures->fPsfd      |= RT_BOOL(pExtLeaf8->uEbx & X86_CPUID_AMD_EFEID_EBX_PSFD);
            }

            PCCPUMCPUIDLEAF pExtLeaf21 = cpumCpuIdFindLeaf(paLeaves, cLeaves, 0x80000021);
            if (pExtLeaf21)
            {
                /** @todo IBPB_BRTYPE is implied on Zen 1 & 2.
                 *  https://www.amd.com/content/dam/amd/en/documents/corporate/cr/speculative-return-stack-overflow-whitepaper.pdf */
            }

            if (pFeatures->fSvm)
            {
                PCCPUMCPUIDLEAF pSvmLeaf = cpumCpuIdFindLeaf(paLeaves, cLeaves, 0x8000000a);
                AssertLogRelReturn(pSvmLeaf, VERR_CPUM_IPE_1);
                pFeatures->fSvmNestedPaging         = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_NESTED_PAGING);
                pFeatures->fSvmLbrVirt              = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_LBR_VIRT);
                pFeatures->fSvmSvmLock              = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_SVM_LOCK);
                pFeatures->fSvmNextRipSave          = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_NRIP_SAVE);
                pFeatures->fSvmTscRateMsr           = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_TSC_RATE_MSR);
                pFeatures->fSvmVmcbClean            = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_VMCB_CLEAN);
                pFeatures->fSvmFlusbByAsid          = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_FLUSH_BY_ASID);
                pFeatures->fSvmDecodeAssists        = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_DECODE_ASSISTS);
                pFeatures->fSvmPauseFilter          = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_PAUSE_FILTER);
                pFeatures->fSvmPauseFilterThreshold = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_PAUSE_FILTER_THRESHOLD);
                pFeatures->fSvmAvic                 = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_AVIC);
                pFeatures->fSvmVirtVmsaveVmload     = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_VIRT_VMSAVE_VMLOAD);
                pFeatures->fSvmVGif                 = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_VGIF);
                pFeatures->fSvmGmet                 = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_GMET);
                pFeatures->fSvmX2Avic               = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_X2AVIC);
                pFeatures->fSvmSSSCheck             = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_SSSCHECK);
                pFeatures->fSvmSpecCtrl             = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_SPEC_CTRL);
                pFeatures->fSvmRoGpt                = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_ROGPT);
                pFeatures->fSvmHostMceOverride      = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_HOST_MCE_OVERRIDE);
                pFeatures->fSvmTlbiCtl              = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_TLBICTL);
                pFeatures->fSvmVNmi                 = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_VNMI);
                pFeatures->fSvmIbsVirt              = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_IBS_VIRT);
                pFeatures->fSvmExtLvtAvicAccessChg  = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_EXT_LVT_AVIC_ACCESS_CHG);
                pFeatures->fSvmNstVirtVmcbAddrChk   = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_NST_VIRT_VMCB_ADDR_CHK);
                pFeatures->fSvmBusLockThreshold     = RT_BOOL(pSvmLeaf->uEdx & X86_CPUID_SVM_FEATURE_EDX_BUS_LOCK_THRESHOLD);
                pFeatures->uSvmMaxAsid              = pSvmLeaf->uEbx;
            }
        }

        /*
         * Quirks.
         */
        pFeatures->fLeakyFxSR = pExtLeaf
                             && (pExtLeaf->uEdx & X86_CPUID_AMD_FEATURE_EDX_FFXSR)
                             && (   (   pFeatures->enmCpuVendor == CPUMCPUVENDOR_AMD
                                     && pFeatures->uFamily >= 6 /* K7 and up */)
                                 || pFeatures->enmCpuVendor == CPUMCPUVENDOR_HYGON);

        /*
         * Max extended (/FPU) state.
         */
        pFeatures->cbMaxExtendedState = pFeatures->fFxSaveRstor ? sizeof(X86FXSTATE) : sizeof(X86FPUSTATE);
        if (pFeatures->fXSaveRstor)
        {
            PCCPUMCPUIDLEAF const pXStateLeaf0 = cpumCpuIdFindLeafEx(paLeaves, cLeaves, 13, 0);
            if (pXStateLeaf0)
            {
                if (   pXStateLeaf0->uEcx >= sizeof(X86FXSTATE)
                    && pXStateLeaf0->uEcx <= CPUM_MAX_XSAVE_AREA_SIZE
                    && RT_ALIGN_32(pXStateLeaf0->uEcx, 8) == pXStateLeaf0->uEcx
                    && pXStateLeaf0->uEbx >= sizeof(X86FXSTATE)
                    && pXStateLeaf0->uEbx <= pXStateLeaf0->uEcx
                    && RT_ALIGN_32(pXStateLeaf0->uEbx, 8) == pXStateLeaf0->uEbx)
                {
                    pFeatures->cbMaxExtendedState = pXStateLeaf0->uEcx;

                    /* (paranoia:) */
                    PCCPUMCPUIDLEAF const pXStateLeaf1 = cpumCpuIdFindLeafEx(paLeaves, cLeaves, 13, 1);
                    if (   pXStateLeaf1
                        && pXStateLeaf1->uEbx > pFeatures->cbMaxExtendedState
                        && pXStateLeaf1->uEbx <= CPUM_MAX_XSAVE_AREA_SIZE
                        && (pXStateLeaf1->uEcx || pXStateLeaf1->uEdx) )
                        pFeatures->cbMaxExtendedState = pXStateLeaf1->uEbx;
                }
                else
                    AssertLogRelMsgFailedStmt(("Unexpected max/cur XSAVE area sizes: %#x/%#x\n", pXStateLeaf0->uEcx, pXStateLeaf0->uEbx),
                                              pFeatures->fXSaveRstor = 0);
            }
            else
                AssertLogRelMsgFailedStmt(("Expected leaf eax=0xd/ecx=0 with the XSAVE/XRSTOR feature!\n"),
                                          pFeatures->fXSaveRstor = 0);
        }

        /*
         * Enable or disable VEX support depending on whether it's needed. Note that AVX,
         * BMI1, and BMI2 all use VEX encoding but are theoretically independent of each other.
         */
        pFeatures->fVex = pFeatures->fAvx | pFeatures->fBmi1 | pFeatures->fBmi2;
    }
    else
        AssertLogRelReturn(cLeaves == 0, VERR_CPUM_IPE_1);

    cpumCpuIdExplodeFeaturesX86SetSummaryBits(pFeatures);
    return VINF_SUCCESS;
}


/**
 * Helper for extracting feature bits from IA32_ARCH_CAPABILITIES.
 */
void cpumCpuIdExplodeArchCapabilities(CPUMFEATURESX86 *pFeatures, bool fHasArchCap, uint64_t fArchVal)
{
    Assert(fHasArchCap || fArchVal == 0);
    pFeatures->fArchCap                = fHasArchCap;
    pFeatures->fArchRdclNo             = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_RDCL_NO);
    pFeatures->fArchIbrsAll            = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_IBRS_ALL);
    pFeatures->fArchRsbOverride        = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_RSBO);
    pFeatures->fArchVmmNeedNotFlushL1d = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_VMM_NEED_NOT_FLUSH_L1D);
    pFeatures->fArchSsbNo              = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_SSB_NO);
    pFeatures->fArchMdsNo              = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_MDS_NO);
    pFeatures->fArchIfPschangeMscNo    = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_IF_PSCHANGE_MC_NO);
    pFeatures->fArchTsxCtrl            = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_TSX_CTRL);
    pFeatures->fArchTaaNo              = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_TAA_NO);
    pFeatures->fArchMiscPackageCtrls   = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_MISC_PACKAGE_CTRLS);
    pFeatures->fArchEnergyFilteringCtl = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_ENERGY_FILTERING_CTL);
    pFeatures->fArchDoitm              = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_DOITM);
    pFeatures->fArchSbdrSsdpNo         = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_SBDR_SSDP_NO);
    pFeatures->fArchFbsdpNo            = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_FBSDP_NO);
    pFeatures->fArchPsdpNo             = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_PSDP_NO);
    pFeatures->fArchFbClear            = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_FB_CLEAR);
    pFeatures->fArchFbClearCtrl        = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_FB_CLEAR_CTRL);
    pFeatures->fArchRrsba              = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_RRSBA);
    pFeatures->fArchBhiNo              = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_BHI_NO);
    pFeatures->fArchXapicDisableStatus = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_XAPIC_DISABLE_STATUS);
    pFeatures->fArchOverclockingStatus = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_OVERCLOCKING_STATUS);
    pFeatures->fArchPbrsbNo            = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_PBRSB_NO);
    pFeatures->fArchGdsCtrl            = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_GDS_CTRL);
    pFeatures->fArchGdsNo              = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_GDS_NO);
    pFeatures->fArchRfdsNo             = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_RFDS_NO);
    pFeatures->fArchRfdsClear          = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_RFDS_CLEAR);
    pFeatures->fArchIgnUmonitorSupport = RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_IGN_UMONITOR_SUPPORT);
    pFeatures->fArchMonUmonMitigSupport= RT_BOOL(fArchVal & MSR_IA32_ARCH_CAP_F_MON_UMON_MITIG_SUPPORT);
}


# if defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_AGNOSTIC)
/**
 * Sets the guest IA32_ARCH_CAPABILITIES value and associated feature bits.
 */
void cpumCpuIdSetGuestArchCapabilities(PVMCC pVM, bool fHasArchCap, uint64_t fArchVal, bool fHasIbrs)
{
    if (!fHasArchCap)
        fArchVal = 0;
    else if (!fHasIbrs)
        fArchVal &= ~MSR_IA32_ARCH_CAP_F_IBRS_ALL;
    fArchVal &= ~(  RT_BIT_64(9)
                  | MSR_IA32_ARCH_CAP_F_MISC_PACKAGE_CTRLS
                  | MSR_IA32_ARCH_CAP_F_ENERGY_FILTERING_CTL
                  | MSR_IA32_ARCH_CAP_F_DOITM
                  | RT_BIT_64(16)
                  | RT_BIT_64(22)
                  | MSR_IA32_ARCH_CAP_F_FB_CLEAR_CTRL
                    /** @todo mask off MSR_IA32_ARCH_CAP_F_RRSBA ? */
                  | MSR_IA32_ARCH_CAP_F_XAPIC_DISABLE_STATUS
                  | MSR_IA32_ARCH_CAP_F_OVERCLOCKING_STATUS /** @todo expose IA32_OVERCLOCKING_STATUS */
                  | MSR_IA32_ARCH_CAP_F_GDS_CTRL
                  | MSR_IA32_ARCH_CAP_F_IGN_UMONITOR_SUPPORT
                  | MSR_IA32_ARCH_CAP_F_MON_UMON_MITIG_SUPPORT
                  | ~(RT_BIT_64(31) - 1U)
                  );
    VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->cpum.s.GuestMsrs.msr.ArchCaps = fArchVal);

    cpumCpuIdExplodeArchCapabilities(&pVM->cpum.s.GuestFeatures, fHasArchCap, fArchVal);
    LogRel(("CPUM: Guest IA32_ARCH_CAPABILITIES = %#RX64\n", fArchVal));
}
# endif


# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
/**
 * Sets host & guest feature bits & MSRs related to IA32_ARCH_CAPABILITIES.
 *
 * ASSUMES this is called after the basic guest features has been exploded.
 */
VMM_INT_DECL(void) CPUMCpuIdApplyX86HostArchCapabilities(PVMCC pVM, bool fHasArchCap, uint64_t fHostArchVal)
{
    cpumCpuIdExplodeArchCapabilities(const_cast<CPUMFEATURESX86 *>(&pVM->cpum.s.HostFeatures.s), fHasArchCap, fHostArchVal);
    LogRel(("CPUM: Host IA32_ARCH_CAPABILITIES  = %#RX64\n", fHostArchVal));

# if defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_AGNOSTIC)
#  ifdef VBOX_VMM_TARGET_AGNOSTIC
    /** @todo arm on x86: check VM target. */
#  endif
    cpumCpuIdSetGuestArchCapabilities(pVM, fHasArchCap && pVM->cpum.s.GuestFeatures.fArchCap,
                                      fHostArchVal, pVM->cpum.s.GuestFeatures.fIbrs);
#  endif
}
# endif /* defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) */

#endif /* defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86) */
#if defined(RT_ARCH_ARM64) && defined(IN_RING3) /** @todo port to ring-0 as needed. */

/** @callback_impl{FNRTSORTCMP} */
static DECLCALLBACK(int) cpumCpuIdSysRegValSortCmp(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PCSUPARMSYSREGVAL const pElm1 = (PCSUPARMSYSREGVAL)pvElement1;
    PCSUPARMSYSREGVAL const pElm2 = (PCSUPARMSYSREGVAL)pvElement2;
    return pElm1->idReg < pElm2->idReg ? -1 : pElm1->idReg > pElm2->idReg ? 1 : 0;
}

/**
 * Used by CPUMCpuIdCollectIdSysRegsFromArmV8Host to lookup @a idReg in the
 * given array.
 *
 * @returns Index if found, UINT32_MAX if not.
 */
static uint32_t cpumCpuIdLookupArmSysReg(PCSUPARMSYSREGVAL paSysRegs, uint32_t cSysRegs, uint32_t const idReg)
{
    for (uint32_t iSysReg = 0; iSysReg < cSysRegs; iSysReg++)
        if (paSysRegs[iSysReg].idReg == idReg)
            return iSysReg;
    return UINT32_MAX;
}


/**
 * Collects the ID registers from an ARMv8 host.
 *
 * This isn't trivial an all hosts when running in userland and there is no
 * support driver handy.
 */
VMMDECL(int) CPUMCpuIdCollectIdSysRegsFromArmV8Host(PSUPARMSYSREGVAL *ppaSysRegs, uint32_t *pcSysRegs)
{
    int rc;

    /*
     * Allocate enough space for anything we might collect directly here.
     */
    uint32_t         cSysRegs      = 0;
    uint32_t         cSysRegsAlloc = 256;
    PSUPARMSYSREGVAL paSysRegs     = (PSUPARMSYSREGVAL)RTMemAllocZ(sizeof(*paSysRegs) * cSysRegsAlloc);
    AssertReturn(paSysRegs, VERR_NO_MEMORY);

    bool             fIncZeroValues = true;
# define ADD_REG_BY_ID(a_idReg, a_uValue, a_fFlags) do { \
            if (((a_uValue) != 0 || fIncZeroValues) && cpumCpuIdLookupArmSysReg(paSysRegs, cSysRegs, (a_idReg)) == UINT32_MAX) \
            { \
                AssertBreak(cSysRegs < cSysRegsAlloc); \
                paSysRegs[cSysRegs].fFlags = a_fFlags; \
                paSysRegs[cSysRegs].idReg  = (a_idReg); \
                paSysRegs[cSysRegs].uValue = (a_uValue); \
                cSysRegs += 1; \
            } \
        } while (0)

    /* From SUPDRrv.cpp: */
    uint64_t uRegVal;
# ifdef _MSC_VER
#  define COMPILER_READ_SYS_REG(a_u64Dst, a_Op0, a_Op1, a_CRn, a_CRm, a_Op2) \
        (a_u64Dst) = (uint64_t)_ReadStatusReg(ARMV8_AARCH64_SYSREG_ID_CREATE(a_Op0, a_Op1, a_CRn, a_CRm, a_Op2) & 0x7fff)
# else
#  define COMPILER_READ_SYS_REG(a_u64Dst, a_Op0, a_Op1, a_CRn, a_CRm, a_Op2) \
        __asm__ __volatile__ ("mrs %0, s" #a_Op0 "_" #a_Op1 "_c" #a_CRn "_c" #a_CRm "_" #a_Op2  : "=r" (a_u64Dst))
# endif
# define READ_SYS_REG_UNDEF(a_Op0, a_Op1, a_CRn, a_CRm, a_Op2) do { \
            uRegVal = 0; \
            COMPILER_READ_SYS_REG(uRegVal, a_Op0, a_Op1, a_CRn, a_CRm, a_Op2); \
            ADD_REG_BY_ID(ARMV8_AARCH64_SYSREG_ID_CREATE(a_Op0, a_Op1, a_CRn, a_CRm, a_Op2), uRegVal, \
                          SUP_ARM_SYS_REG_VAL_F_FROM_USERLAND); \
        } while (0)

# define READ_SYS_REG_NAMED(a_Op0, a_Op1, a_CRn, a_CRm, a_Op2, a_SysRegName) do { \
            AssertCompile(   ARMV8_AARCH64_SYSREG_ID_CREATE(a_Op0, a_Op1, a_CRn, a_CRm, a_Op2) \
                          == RT_CONCAT(ARMV8_AARCH64_SYSREG_,a_SysRegName)); \
            READ_SYS_REG_UNDEF(a_Op0, a_Op1, a_CRn, a_CRm, a_Op2); \
        } while (0)

# define READ_SYS_REG__TODO(a_Op0, a_Op1, a_CRn, a_CRm, a_Op2, a_SysRegName) READ_SYS_REG_UNDEF(a_Op0, a_Op1, a_CRn, a_CRm, a_Op2)

# ifdef IN_RING3
    /*
     * If we might have the support driver handy, it will provide
     * all the info we need.
     */
    if (!SUPR3IsDriverless())
    {
        uint32_t cRegsAvailable = 0;
        rc = SUPR3ArmQuerySysRegs(NIL_RTCPUID, SUP_ARM_SYS_REG_F_EXTENDED | SUP_ARM_SYS_REG_F_INC_ZERO_REG_VAL,
                                  cSysRegsAlloc, &cSysRegs, &cRegsAvailable, paSysRegs);
        if (RT_SUCCESS(rc) && cRegsAvailable > cSysRegsAlloc)
        {
            RTMemFree(paSysRegs);
            cSysRegsAlloc = cRegsAvailable + 1;
            paSysRegs     = (PSUPARMSYSREGVAL)RTMemAllocZ(sizeof(*paSysRegs) * cSysRegsAlloc);
            AssertReturn(paSysRegs, VERR_NO_MEMORY);
            rc = SUPR3ArmQuerySysRegs(NIL_RTCPUID, SUP_ARM_SYS_REG_F_EXTENDED | SUP_ARM_SYS_REG_F_INC_ZERO_REG_VAL,
                                      cSysRegsAlloc, &cSysRegs, &cRegsAvailable, paSysRegs);
        }
        if (RT_SUCCESS(rc))
        {
            *pcSysRegs  = cSysRegs;
            *ppaSysRegs = paSysRegs;
            return rc;
        }
    }

#  ifdef RT_OS_WINDOWS
    /*
     * Windows has a collection of useful register values in the registry.
     */
    HKEY    hKey = NULL;
    LSTATUS lrc  = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                                KEY_QUERY_VALUE, &hKey);
    if (lrc == ERROR_SUCCESS)
    {
        for (uint32_t i = 0; i < 4096; i++)
        {
            WCHAR wszName[256];
            DWORD cwcName = RT_ELEMENTS(wszName);
            DWORD dwType  = 0;
            lrc = RegEnumValueW(hKey, i, wszName, &cwcName, NULL, &dwType, NULL, NULL);
            if (lrc == ERROR_SUCCESS)
            {
                if (dwType == REG_QWORD && wszName[0] == 'C' && wszName[1] == 'P' && RT_C_IS_BLANK(wszName[2]))
                {
                    uint32_t offName = 3;
                    uint32_t idReg   = 0;
                    while (offName < cwcName && RT_C_IS_XDIGIT(wszName[offName]))
                    {
                        idReg *= 16;
                        idReg |= RT_C_IS_DIGIT(wszName[offName]) ? wszName[offName] - '0' : (wszName[offName] & ~0x20) - 'A' + 10;
                        offName++;
                    }
                    if ((offName == cwcName || !wszName[offName]) && idReg != 0)
                    {
                        idReg |= 0x8000; /* implicit top bit (see top of asm-arm.h) */

                        uRegVal = 0;
                        DWORD cbValue = sizeof(uRegVal);
                        lrc = RegQueryValueExW(hKey, wszName, NULL, &dwType, (PBYTE)&uRegVal, &cbValue);
                        if (lrc == ERROR_SUCCESS)
                            ADD_REG_BY_ID(idReg, uRegVal, SUP_ARM_SYS_REG_VAL_F_FROM_REGISTRY);
                        else
                            AssertMsgFailed(("RegQueryValueexW(,%ls,,,) failed: %Rwc\n", wszName, lrc));
                    }
                    else
                        AssertMsgFailed(("Unable to parse 'CP ' prefixed value name: '%ls'\n", wszName));
                }
            }
            else if (lrc == ERROR_NO_MORE_ITEMS)
                break;
            else
                AssertMsgFailed(("lrc=%Rwc\n", lrc));
        }

        RegCloseKey(hKey);
    }

#  elif defined(RT_OS_LINUX)
    /*
     * Read the 2-3 registers exposed via sysfs.
     */
    RTCPUID idCpu  = RTMpCpuId();
    if (idCpu == NIL_RTCPUID)
        idCpu = 0;
    int64_t iValue = 0;
    rc = RTLinuxSysFsReadIntFile(16, &iValue, "/sys/devices/system/cpu/cpu%u/regs/identification/midr", idCpu)
    if (RT_SUCCESS(rc))
    {
        ADD_REG_BY_ID(ARMV8_AARCH64_SYSREG_MIDR_EL1, (uint64_t)iValue, SUP_ARM_SYS_REG_VAL_F_FROM_SYSFS);

        iValue = 0;
        rc = RTLinuxSysFsReadIntFile(16, &iValue, "/sys/devices/system/cpu/cpu%u/regs/identification/revidr", idCpu)
        if (RT_SUCCESS(rc))
            ADD_REG_BY_ID(ARMV8_AARCH64_SYSREG_REVIDR_EL1, (uint64_t)iValue, SUP_ARM_SYS_REG_VAL_F_FROM_SYSFS);

        iValue = 0;
        rc = RTLinuxSysFsReadIntFile(16, &iValue, "/sys/devices/system/cpu/cpu%u/regs/identification/smidr_el1", idCpu)
        if (RT_SUCCESS(rc))
            ADD_REG_BY_ID(ARMV8_AARCH64_SYSREG_SMIDR_EL1, (uint64_t)iValue, SUP_ARM_SYS_REG_VAL_F_FROM_SYSFS);
    }

    /*
     * If the usermode MRS emulation is enabled, try get what is available
     * to us.  The emulated register set is initially limited by is_emulated()
     * to (Op0=3, Op1=0, CRn=0, CRm in {0,2,3,4,5,6,7}, Op2=any).  The CRm
     * values 2 & 3 were added in linux 6.1.
     *
     * But this is further restricted two ways:
     *    - For CRm=0 by emulate_id_reg() to Op2 in {0, 5, 7}, i.e. MIDR_EL1,
     *      MPIDR_EL1, REVIDR_EL1.
     *    - The registers listed in the arm64_ftr_regs table with associated
     *       field sanitizing.
     */
    if (getauxval(AT_HWCAP) & HWCAP_CPUID)
    {
        /* Note! This is a stripped down version of the list in SUPDrv.cpp */
        fSkipZeroRegs = true;

        READ_SYS_REG_NAMED(3, 0, 0, 0, 0, MIDR_EL1);
        READ_SYS_REG_NAMED(3, 0, 0, 0, 5, MPIDR_EL1);
        READ_SYS_REG_NAMED(3, 0, 0, 0, 6, REVIDR_EL1);
#  if 0 /* not exposed  */
        READ_SYS_REG__TODO(3, 1, 0, 0, 0, CCSIDR_EL1); /** @todo CCSIDR_EL1? */
        READ_SYS_REG__TODO(3, 1, 0, 0, 1, CLIDR_EL1);
        READ_SYS_REG__TODO(3, 1, 0, 0, 7, AIDR_EL1);
#  endif
#  if 0 /* read by common code further down */
        READ_SYS_REG_NAMED(3, 3, 0, 0, 1, CTR_EL0);
        READ_SYS_REG_NAMED(3, 3, 0, 0, 7, DCZID_EL0);
        READ_SYS_REG_NAMED(3, 3,14, 0, 0, CNTFRQ_EL0);
#  endif

        READ_SYS_REG_NAMED(3, 0, 0, 4, 0, ID_AA64PFR0_EL1);
        READ_SYS_REG_NAMED(3, 0, 0, 4, 1, ID_AA64PFR1_EL1);
        READ_SYS_REG_UNDEF(3, 0, 0, 4, 2);
        READ_SYS_REG_UNDEF(3, 0, 0, 4, 3);
        READ_SYS_REG_NAMED(3, 0, 0, 4, 4, ID_AA64ZFR0_EL1);
        READ_SYS_REG_NAMED(3, 0, 0, 4, 5, ID_AA64SMFR0_EL1);
        READ_SYS_REG_UNDEF(3, 0, 0, 4, 6);
        READ_SYS_REG_UNDEF(3, 0, 0, 4, 7);

        READ_SYS_REG_NAMED(3, 0, 0, 5, 0, ID_AA64DFR0_EL1);
        READ_SYS_REG_NAMED(3, 0, 0, 5, 1, ID_AA64DFR1_EL1);
        READ_SYS_REG_UNDEF(3, 0, 0, 5, 2);
        READ_SYS_REG_UNDEF(3, 0, 0, 5, 3);
        READ_SYS_REG_NAMED(3, 0, 0, 5, 4, ID_AA64AFR0_EL1);
        READ_SYS_REG_NAMED(3, 0, 0, 5, 5, ID_AA64AFR1_EL1);
        READ_SYS_REG_UNDEF(3, 0, 0, 5, 6);
        READ_SYS_REG_UNDEF(3, 0, 0, 5, 7);

        READ_SYS_REG_NAMED(3, 0, 0, 6, 0, ID_AA64ISAR0_EL1);
        READ_SYS_REG_NAMED(3, 0, 0, 6, 1, ID_AA64ISAR1_EL1);
        READ_SYS_REG_NAMED(3, 0, 0, 6, 2, ID_AA64ISAR2_EL1);
        READ_SYS_REG__TODO(3, 0, 0, 6, 3, ID_AA64ISAR3_EL1);
        READ_SYS_REG_UNDEF(3, 0, 0, 6, 4);
        READ_SYS_REG_UNDEF(3, 0, 0, 6, 5);
        READ_SYS_REG_UNDEF(3, 0, 0, 6, 6);
        READ_SYS_REG_UNDEF(3, 0, 0, 6, 7);

        READ_SYS_REG_NAMED(3, 0, 0, 7, 0, ID_AA64MMFR0_EL1);
        READ_SYS_REG_NAMED(3, 0, 0, 7, 1, ID_AA64MMFR1_EL1);
        READ_SYS_REG_NAMED(3, 0, 0, 7, 2, ID_AA64MMFR2_EL1);
        READ_SYS_REG__TODO(3, 0, 0, 7, 3, ID_AA64MMFR3_EL1);
        READ_SYS_REG__TODO(3, 0, 0, 7, 4, ID_AA64MMFR4_EL1);
        READ_SYS_REG_UNDEF(3, 0, 0, 7, 5);
        READ_SYS_REG_UNDEF(3, 0, 0, 7, 6);
        READ_SYS_REG_UNDEF(3, 0, 0, 7, 7);

        /*
         * AArch32 feature registers (what is accessible in 6.1+).
         */
        char szRelease[256] = {0};
        RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szRelease, sizeof(szRelease));
        if (RTStrVersionCompare(szRelease[0], "6.1") >= 0 && RTStrVersionCompare(szRelease[0], "99.99") < 0)
        {
            READ_SYS_REG_NAMED(3, 0, 0, 2, 0, ID_ISAR0_EL1);
            READ_SYS_REG_NAMED(3, 0, 0, 2, 1, ID_ISAR1_EL1);
            READ_SYS_REG_NAMED(3, 0, 0, 2, 2, ID_ISAR2_EL1);
            READ_SYS_REG_NAMED(3, 0, 0, 2, 3, ID_ISAR3_EL1);
            READ_SYS_REG_NAMED(3, 0, 0, 2, 4, ID_ISAR4_EL1);
            READ_SYS_REG_NAMED(3, 0, 0, 2, 5, ID_ISAR5_EL1);

            READ_SYS_REG_NAMED(3, 0, 0, 2, 6, ID_MMFR4_EL1);

            READ_SYS_REG_NAMED(3, 0, 0, 2, 7, ID_ISAR6_EL1);

            READ_SYS_REG_NAMED(3, 0, 0, 3, 0, MVFR0_EL1);
            READ_SYS_REG_NAMED(3, 0, 0, 3, 1, MVFR1_EL1);
            READ_SYS_REG_NAMED(3, 0, 0, 3, 2, MVFR2_EL1);

            READ_SYS_REG_NAMED(3, 0, 0, 3, 4, ID_PFR2_EL1);

            READ_SYS_REG_NAMED(3, 0, 0, 3, 5, ID_DFR1_EL1);

            READ_SYS_REG_NAMED(3, 0, 0, 3, 6, ID_MMFR5_EL1);
        }
    }
#  endif /* RT_OS_LINUX */

    /*
     * CTR_EL0 can be trapped when executed in L0 (SCTLR_EL0.UCT) and macOS
     * & Windows do so by default.  Linux does seem to expose it.
     */
#  if defined(RT_OS_LINUX)
    READ_SYS_REG_NAMED(3, 3, 0, 0, 1, CTR_EL0);
#  endif
    READ_SYS_REG_NAMED(3, 3, 0, 0, 7, DCZID_EL0);
    READ_SYS_REG_NAMED(3, 3,14, 0, 0, CNTFRQ_EL0); /* should probably make this take preference... */

#  ifndef IN_VBOX_CPU_REPORT
    /*
     * If we've got the main ID register (MIDR) use it to try locate a DB
     * entry from the CPU.  The lookup functions might not find an exact match,
     * but shouldn't return anything that is too far off...
     */
    uint32_t         uScore   = 0;
    PCCPUMDBENTRYARM pDbEntry = NULL;
    int32_t idxMidr = (int32_t)cSysRegs - 1;
    while (idxMidr >= 0 && paSysRegs[idxMidr].idReg != ARMV8_AARCH64_SYSREG_MIDR_EL1)
        idxMidr--;
    if (idxMidr >= 0 && (paSysRegs[idxMidr].uValue & UINT32_C(0x00f0fff0)) != 0)
        pDbEntry = CPUMR3DbGetBestEntryByArm64MainId(paSysRegs[idxMidr].uValue, &uScore);
    else
    {
        idxMidr = -1;
        char szName[256];
        rc = RTMpGetDescription(NIL_RTCPUID, szName, sizeof(szName));
        if (RT_SUCCESS(rc))
            pDbEntry = (PCCPUMDBENTRYARM)CPUMR3DbGetBestEntryByName(szName, CPUMDBENTRYTYPE_ARM, &uScore);
        else
            pDbEntry = NULL;
    }
    if (pDbEntry)
    {
        Assert(pDbEntry->Core.enmEntryType == CPUMDBENTRYTYPE_ARM);

        /*
         * Merge the two.  If there are multiple core variations, use the first one
         * as it's usually the efficiency one.  (We can revisit this if we end up
         * needing to support host systems with cores that have different feature
         * sets.)
         *
         * Note! Don't know the quality of the Windows registry stuff, but for now
         *       we're assuming it's mostly unfiltered and will use it instead of
         *       the DB entry...
         */
        for (uint32_t iRegSet = 0; iRegSet < 2; iRegSet++)
        {
            PCSUPARMSYSREGVAL const paSrcRegs = iRegSet ? pDbEntry->aVariants[0].paSysRegVals : pDbEntry->paSysRegCmnVals;
            uint32_t const          cSrcRegs  = iRegSet ? pDbEntry->aVariants[0].cSysRegVals  : pDbEntry->cSysRegCmnVals;
            for (uint32_t iSrcReg = 0; iSrcReg < cSrcRegs; iSrcReg++)
            {
                /* Check if we've got it already. */
                uint32_t const idxDst = cpumCpuIdLookupArmSysReg(paSysRegs, cSysRegs, paSrcRegs[iSrcReg].idReg);
                if (idxDst < cSysRegs)
                {
                    /* If the DB entry is a great match, use the value from the DB entry. */
                    if (uScore >= 100)
                    {
                        paSysRegs[idxDst].uValue = paSrcRegs[iSrcReg].uValue;
                        paSysRegs[idxDst].fFlags = SUP_ARM_SYS_REG_VAL_F_FROM_DB;
                    }
                }
                else
                {
                    if (cSysRegs >= cSysRegsAlloc)
                    {
                        void * const pvNew = RTMemRealloc(paSysRegs, sizeof(paSysRegs[0]) * cSysRegsAlloc * 2);
                        AssertContinue(pvNew);
                        paSysRegs = (PSUPARMSYSREGVAL)pvNew;
                        cSysRegsAlloc *= 2;
                    }

                    paSysRegs[cSysRegs].idReg  = paSrcRegs[iSrcReg].idReg;
                    paSysRegs[cSysRegs].uValue = paSrcRegs[iSrcReg].uValue;
                    paSysRegs[cSysRegs].fFlags = SUP_ARM_SYS_REG_VAL_F_FROM_DB;
                    cSysRegs++;
                }
            }
        }
    }
#  endif

# else  /* !IN_RING3 */
    /** @todo as needed    */
# endif /* !IN_RING3 */

    RTSortShell(paSysRegs, cSysRegs, sizeof(paSysRegs[0]), cpumCpuIdSysRegValSortCmp, NULL);

    *pcSysRegs  = cSysRegs;
    *ppaSysRegs = paSysRegs;
    return VINF_SUCCESS;
}

#endif /* defined(RT_ARCH_ARM64) && IN_RING3 */
#if defined(RT_ARCH_ARM64) || defined(VBOX_VMM_TARGET_ARMV8)

# if 1
/* Include code generated by bsd-sepc-analyze.py --out-features-hdr . */
#  if RT_CLANG_PREREQ(4, 0)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wconstant-logical-operand"
#   include "CPUMAllCpuIdArmV8.cpp.h"
#   pragma GCC diagnostic pop
#  else
#   include "CPUMAllCpuIdArmV8.cpp.h"
#  endif

# else

/** ID_AA64MMFR0_EL1 features. */
static int cpumCpuIdExplodeFeaturesArmV8_IdAa64Mmfr0El1(CPUMFEATURESARMV8 *pFeatures, uint64_t const u64IdReg) RT_NOEXCEPT
{

    pFeatures->fExs              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR0_EL1_EXS)     >= ARMV8_ID_AA64MMFR0_EL1_EXS_SUPPORTED;
    pFeatures->fFgt              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR0_EL1_FGT)     >= ARMV8_ID_AA64MMFR0_EL1_FGT_SUPPORTED;
    pFeatures->fEcv              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR0_EL1_ECV)     >= ARMV8_ID_AA64MMFR0_EL1_ECV_SUPPORTED;

    pFeatures->fTGran4K          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR0_EL1_TGRAN4)  != ARMV8_ID_AA64MMFR0_EL1_TGRAN4_NOT_IMPL;
    pFeatures->fTGran16K         = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR0_EL1_TGRAN16) != ARMV8_ID_AA64MMFR0_EL1_TGRAN16_NOT_IMPL;
    pFeatures->fTGran64K         = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR0_EL1_TGRAN64) != ARMV8_ID_AA64MMFR0_EL1_TGRAN64_NOT_IMPL;

    static uint8_t s_aPaRange[] = { 32, 36, 40, 42, 44, 48, 52 };
    AssertLogRelMsgReturn(RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR0_EL1_PARANGE) < RT_ELEMENTS(s_aPaRange),
                          ("CPUM: Invalid/Unsupported PARange value in ID_AA64MMFR0_EL1 register: %u\n",
                          RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR0_EL1_PARANGE)),
                          VERR_CPUM_IPE_1);
    pFeatures->cMaxPhysAddrWidth = s_aPaRange[RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR0_EL1_PARANGE)];
    return VINF_SUCCESS;
}


/** ID_AA64ISAR0_EL1 features. */
static int cpumCpuIdExplodeFeaturesArmV8_IdAa64Isar0El1(CPUMFEATURESARMV8 *pFeatures, uint64_t const u64IdReg) RT_NOEXCEPT
{
    pFeatures->fAes              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_AES)     >= ARMV8_ID_AA64ISAR0_EL1_AES_SUPPORTED;
    pFeatures->fPmull            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_AES)     >= ARMV8_ID_AA64ISAR0_EL1_AES_SUPPORTED_PMULL;
    pFeatures->fSha1             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_SHA1)    >= ARMV8_ID_AA64ISAR0_EL1_SHA1_SUPPORTED;
    pFeatures->fSha256           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_SHA2)    >= ARMV8_ID_AA64ISAR0_EL1_SHA2_SUPPORTED_SHA256;
    pFeatures->fSha512           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_SHA2)    >= ARMV8_ID_AA64ISAR0_EL1_SHA2_SUPPORTED_SHA256_SHA512;
    pFeatures->fCrc32            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_CRC32)   >= ARMV8_ID_AA64ISAR0_EL1_CRC32_SUPPORTED;
    pFeatures->fLse              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_ATOMIC)  >= ARMV8_ID_AA64ISAR0_EL1_ATOMIC_SUPPORTED;
    pFeatures->fTme              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_TME)     >= ARMV8_ID_AA64ISAR0_EL1_TME_SUPPORTED;
    pFeatures->fRdm              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_RDM)     >= ARMV8_ID_AA64ISAR0_EL1_RDM_SUPPORTED;
    pFeatures->fSha3             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_SHA3)    >= ARMV8_ID_AA64ISAR0_EL1_SHA3_SUPPORTED;
    pFeatures->fSm3              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_SM3)     >= ARMV8_ID_AA64ISAR0_EL1_SM3_SUPPORTED;
    pFeatures->fSm4              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_SM4)     >= ARMV8_ID_AA64ISAR0_EL1_SM4_SUPPORTED;
    pFeatures->fDotProd          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_DP)      >= ARMV8_ID_AA64ISAR0_EL1_DP_SUPPORTED;
    pFeatures->fFhm              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_FHM)     >= ARMV8_ID_AA64ISAR0_EL1_FHM_SUPPORTED;
    pFeatures->fFlagM            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_TS)      >= ARMV8_ID_AA64ISAR0_EL1_TS_SUPPORTED;
    pFeatures->fFlagM2           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_TS)      >= ARMV8_ID_AA64ISAR0_EL1_TS_SUPPORTED_2;
    pFeatures->fTlbios           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_TLB)     >= ARMV8_ID_AA64ISAR0_EL1_TLB_SUPPORTED;
    pFeatures->fTlbirange        = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_TLB)     >= ARMV8_ID_AA64ISAR0_EL1_TLB_SUPPORTED_RANGE;
    pFeatures->fRng              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_RNDR)    >= ARMV8_ID_AA64ISAR0_EL1_RNDR_SUPPORTED;
    return VINF_SUCCESS;
}


/** AA64ISAR1_EL1 features. */
static int cpumCpuIdExplodeFeaturesArmV8_IdAa64Isar1El1(CPUMFEATURESARMV8 *pFeatures, uint64_t const u64IdReg) RT_NOEXCEPT
{
    pFeatures->fDpb              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_DPB)     >= ARMV8_ID_AA64ISAR1_EL1_DPB_SUPPORTED;
    pFeatures->fDpb2             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_DPB)     >= ARMV8_ID_AA64ISAR1_EL1_DPB_SUPPORTED_2;

    /* PAuth using QARMA5. */
    pFeatures->fPacQarma5        = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_APA)     != ARMV8_ID_AA64ISAR1_EL1_APA_NOT_IMPL;
    if (pFeatures->fPacQarma5)
    {
        pFeatures->fPAuth        = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_APA)     >= ARMV8_ID_AA64ISAR1_EL1_APA_SUPPORTED_PAUTH;
        pFeatures->fEpac         = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_APA)     >= ARMV8_ID_AA64ISAR1_EL1_APA_SUPPORTED_EPAC;
        pFeatures->fPAuth2       = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_APA)     >= ARMV8_ID_AA64ISAR1_EL1_APA_SUPPORTED_PAUTH2;
        pFeatures->fFpac         = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_APA)     >= ARMV8_ID_AA64ISAR1_EL1_APA_SUPPORTED_FPAC;
        pFeatures->fFpacCombine  = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_APA)     >= ARMV8_ID_AA64ISAR1_EL1_APA_SUPPORTED_FPACCOMBINE;
    }

    /* PAuth using implementation defined algorithm. */
    pFeatures->fPacImp           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_API)     != ARMV8_ID_AA64ISAR1_EL1_API_NOT_IMPL;
    if (pFeatures->fPacQarma5)
    {
        pFeatures->fPAuth        = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_API)     >= ARMV8_ID_AA64ISAR1_EL1_API_SUPPORTED_PAUTH;
        pFeatures->fEpac         = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_API)     >= ARMV8_ID_AA64ISAR1_EL1_API_SUPPORTED_EPAC;
        pFeatures->fPAuth2       = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_API)     >= ARMV8_ID_AA64ISAR1_EL1_API_SUPPORTED_PAUTH2;
        pFeatures->fFpac         = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_API)     >= ARMV8_ID_AA64ISAR1_EL1_API_SUPPORTED_FPAC;
        pFeatures->fFpacCombine  = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_API)     >= ARMV8_ID_AA64ISAR1_EL1_API_SUPPORTED_FPACCOMBINE;
    }

    pFeatures->fJscvt            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_FJCVTZS) >= ARMV8_ID_AA64ISAR1_EL1_FJCVTZS_SUPPORTED;
    pFeatures->fFcma             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_FCMA)    >= ARMV8_ID_AA64ISAR1_EL1_FCMA_SUPPORTED;
    pFeatures->fLrcpc            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_LRCPC)   >= ARMV8_ID_AA64ISAR1_EL1_LRCPC_SUPPORTED;
    pFeatures->fLrcpc2           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_LRCPC)   >= ARMV8_ID_AA64ISAR1_EL1_LRCPC_SUPPORTED_2;
    pFeatures->fFrintts          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_FRINTTS) >= ARMV8_ID_AA64ISAR1_EL1_FRINTTS_SUPPORTED;
    pFeatures->fSb               = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_SB)      >= ARMV8_ID_AA64ISAR1_EL1_SB_SUPPORTED;
    pFeatures->fSpecres          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_SPECRES) >= ARMV8_ID_AA64ISAR1_EL1_SPECRES_SUPPORTED;
    pFeatures->fBf16             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_BF16)    >= ARMV8_ID_AA64ISAR1_EL1_BF16_SUPPORTED_BF16;
    pFeatures->fEbf16            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_BF16)    >= ARMV8_ID_AA64ISAR1_EL1_BF16_SUPPORTED_EBF16;
    pFeatures->fDgh              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_DGH)     >= ARMV8_ID_AA64ISAR1_EL1_DGH_SUPPORTED;
    pFeatures->fI8mm             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_I8MM)    >= ARMV8_ID_AA64ISAR1_EL1_I8MM_SUPPORTED;
    pFeatures->fXs               = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_XS)      >= ARMV8_ID_AA64ISAR1_EL1_XS_SUPPORTED;
    pFeatures->fLs64             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_LS64)    >= ARMV8_ID_AA64ISAR1_EL1_LS64_SUPPORTED;
    pFeatures->fLs64V            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_LS64)    >= ARMV8_ID_AA64ISAR1_EL1_LS64_SUPPORTED_V;
    pFeatures->fLs64Accdata      = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR1_EL1_LS64)    >= ARMV8_ID_AA64ISAR1_EL1_LS64_SUPPORTED_ACCDATA;
    return VINF_SUCCESS;
}


/** ID_AA64ISAR2_EL1 features. */
static int cpumCpuIdExplodeFeaturesArmV8_IdAa64Isar2El1(CPUMFEATURESARMV8 *pFeatures, uint64_t const u64IdReg) RT_NOEXCEPT
{
    pFeatures->fWfxt             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_WFXT)    >= ARMV8_ID_AA64ISAR2_EL1_WFXT_SUPPORTED;
    pFeatures->fRpres            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_RPRES)   >= ARMV8_ID_AA64ISAR2_EL1_RPRES_SUPPORTED;

    /* PAuth using QARMA3. */
    pFeatures->fPacQarma3        = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_GPA3)    >= ARMV8_ID_AA64ISAR2_EL1_GPA3_SUPPORTED;
    pFeatures->fPacQarma3        = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_APA3)    != ARMV8_ID_AA64ISAR2_EL1_APA3_NOT_IMPL;
    if (pFeatures->fPacQarma5)
    {
        pFeatures->fPAuth        = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_APA3)    >= ARMV8_ID_AA64ISAR2_EL1_APA3_SUPPORTED_PAUTH;
        pFeatures->fEpac         = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_APA3)    >= ARMV8_ID_AA64ISAR2_EL1_APA3_SUPPORTED_EPAC;
        pFeatures->fPAuth2       = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_APA3)    >= ARMV8_ID_AA64ISAR2_EL1_APA3_SUPPORTED_PAUTH2;
        pFeatures->fFpac         = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_APA3)    >= ARMV8_ID_AA64ISAR2_EL1_APA3_SUPPORTED_FPAC;
        pFeatures->fFpacCombine  = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_APA3)    >= ARMV8_ID_AA64ISAR2_EL1_APA3_SUPPORTED_FPACCOMBINE;
    }

    pFeatures->fMops             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_MOPS)    >= ARMV8_ID_AA64ISAR2_EL1_MOPS_SUPPORTED;
    pFeatures->fHbc              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_BC)      >= ARMV8_ID_AA64ISAR2_EL1_BC_SUPPORTED;
    pFeatures->fConstPacField    = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR2_EL1_PACFRAC) >= ARMV8_ID_AA64ISAR2_EL1_PACFRAC_TRUE;
    return VINF_SUCCESS;
}


/** ID_AA64PFR0_EL1 features. */
static int cpumCpuIdExplodeFeaturesArmV8_IdAa64Pfr0El1(CPUMFEATURESARMV8 *pFeatures, uint64_t const u64IdReg) RT_NOEXCEPT
{
    /* The FP and AdvSIMD field must have the same value. */
    Assert(RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_FP) == RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_ADVSIMD));
    pFeatures->fFp               = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_FP)       != ARMV8_ID_AA64PFR0_EL1_FP_NOT_IMPL;
    pFeatures->fFp16             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_FP)       == ARMV8_ID_AA64PFR0_EL1_FP_IMPL_SP_DP_HP;
    pFeatures->fAdvSimd          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_ADVSIMD)  != ARMV8_ID_AA64PFR0_EL1_ADVSIMD_NOT_IMPL;
    pFeatures->fFp16             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_ADVSIMD)  == ARMV8_ID_AA64PFR0_EL1_ADVSIMD_IMPL_SP_DP_HP;
    pFeatures->fRas              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_RAS)      >= ARMV8_ID_AA64PFR0_EL1_RAS_SUPPORTED;
    pFeatures->fRasV1p1          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_RAS)      >= ARMV8_ID_AA64PFR0_EL1_RAS_V1P1;
    pFeatures->fSve              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_SVE)      >= ARMV8_ID_AA64PFR0_EL1_SVE_SUPPORTED;
    pFeatures->fSecEl2           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_SEL2)     >= ARMV8_ID_AA64PFR0_EL1_SEL2_SUPPORTED;
    pFeatures->fAmuV1            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_AMU)      >= ARMV8_ID_AA64PFR0_EL1_AMU_V1;
    pFeatures->fAmuV1p1          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_AMU)      >= ARMV8_ID_AA64PFR0_EL1_AMU_V1P1;
    pFeatures->fDit              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_DIT)      >= ARMV8_ID_AA64PFR0_EL1_DIT_SUPPORTED;
    pFeatures->fRme              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_RME)      >= ARMV8_ID_AA64PFR0_EL1_RME_SUPPORTED;
    pFeatures->fCsv2             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_CSV2)     >= ARMV8_ID_AA64PFR0_EL1_CSV2_SUPPORTED;
    pFeatures->fCsv2v3           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_CSV2)     >= ARMV8_ID_AA64PFR0_EL1_CSV2_3_SUPPORTED;
    return VINF_SUCCESS;
}


/** ID_AA64PFR1_EL1 features. */
static int  cpumCpuIdExplodeFeaturesArmV8_IdAa64Pfr1El1(CPUMFEATURESARMV8 *pFeatures, uint64_t const u64IdReg) RT_NOEXCEPT
{
    pFeatures->fBti              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR1_EL1_BT)       >= ARMV8_ID_AA64PFR1_EL1_BT_SUPPORTED;
    pFeatures->fSsbs             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR1_EL1_SSBS)     >= ARMV8_ID_AA64PFR1_EL1_SSBS_SUPPORTED;
    pFeatures->fSsbs2            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR1_EL1_SSBS)     >= ARMV8_ID_AA64PFR1_EL1_SSBS_SUPPORTED_MSR_MRS;
    pFeatures->fMte              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR1_EL1_MTE)      >= ARMV8_ID_AA64PFR1_EL1_MTE_INSN_ONLY;
    pFeatures->fMte2             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR1_EL1_MTE)      >= ARMV8_ID_AA64PFR1_EL1_MTE_FULL;
    pFeatures->fMte3             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR1_EL1_MTE)      >= ARMV8_ID_AA64PFR1_EL1_MTE_FULL_ASYM_TAG_FAULT_CHK;
    /** @todo RAS_frac, MPAM_frac, CSV2_frac. */
    pFeatures->fSme              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR1_EL1_SME)      >= ARMV8_ID_AA64PFR1_EL1_SME_SUPPORTED;
    pFeatures->fSme2             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR1_EL1_SME)      >= ARMV8_ID_AA64PFR1_EL1_SME_SME2;
    pFeatures->fRngTrap          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR1_EL1_RNDRTRAP) >= ARMV8_ID_AA64PFR1_EL1_RNDRTRAP_SUPPORTED;
    pFeatures->fNmi              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR1_EL1_NMI)      >= ARMV8_ID_AA64PFR1_EL1_NMI_SUPPORTED;
    return VINF_SUCCESS;
}


/** ID_AA64MMFR1_EL1 features. */
static int cpumCpuIdExplodeFeaturesArmV8_IdAa64Mmfr1El1(CPUMFEATURESARMV8 *pFeatures, uint64_t const u64IdReg) RT_NOEXCEPT
{
    pFeatures->fHafdbs           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_HAFDBS)  >= ARMV8_ID_AA64MMFR1_EL1_HAFDBS_SUPPORTED;
    pFeatures->fVmid16           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_VMIDBITS) >= ARMV8_ID_AA64MMFR1_EL1_VMIDBITS_16;
    pFeatures->fVhe              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_VHE)     >= ARMV8_ID_AA64MMFR1_EL1_VHE_SUPPORTED;
    pFeatures->fHpds             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_HPDS)    >= ARMV8_ID_AA64MMFR1_EL1_HPDS_SUPPORTED;
    pFeatures->fHpds2            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_HPDS)    >= ARMV8_ID_AA64MMFR1_EL1_HPDS_SUPPORTED_2;
    pFeatures->fLor              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_LO)      >= ARMV8_ID_AA64MMFR1_EL1_LO_SUPPORTED;
    pFeatures->fPan              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_PAN)     >= ARMV8_ID_AA64MMFR1_EL1_PAN_SUPPORTED;
    pFeatures->fPan2             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_PAN)     >= ARMV8_ID_AA64MMFR1_EL1_PAN_SUPPORTED_2;
    pFeatures->fPan3             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_PAN)     >= ARMV8_ID_AA64MMFR1_EL1_PAN_SUPPORTED_3;
    pFeatures->fXnx              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_XNX)     >= ARMV8_ID_AA64MMFR1_EL1_XNX_SUPPORTED;
    pFeatures->fTwed             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_TWED)    >= ARMV8_ID_AA64MMFR1_EL1_TWED_SUPPORTED;
    pFeatures->fEts2             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_ETS)     >= ARMV8_ID_AA64MMFR1_EL1_ETS_SUPPORTED;
    pFeatures->fHcx              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_HCX)     >= ARMV8_ID_AA64MMFR1_EL1_HCX_SUPPORTED;
    pFeatures->fAfp              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_AFP)     >= ARMV8_ID_AA64MMFR1_EL1_AFP_SUPPORTED;
    pFeatures->fNTlbpa           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_NTLBPA)  >= ARMV8_ID_AA64MMFR1_EL1_NTLBPA_INCLUDE_COHERENT_ONLY;
    pFeatures->fTidcp1           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_TIDCP1)  >= ARMV8_ID_AA64MMFR1_EL1_TIDCP1_SUPPORTED;
    pFeatures->fCmow             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR1_EL1_CMOW)    >= ARMV8_ID_AA64MMFR1_EL1_CMOW_SUPPORTED;
    return VINF_SUCCESS;
}


/** ID_AA64MMFR2_EL1 features. */
static int cpumCpuIdExplodeFeaturesArmV8_IdAa64Mmfr2El1(CPUMFEATURESARMV8 *pFeatures, uint64_t const u64IdReg) RT_NOEXCEPT
{
    pFeatures->fTtcnp            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_CNP)     >= ARMV8_ID_AA64MMFR2_EL1_CNP_SUPPORTED;
    pFeatures->fUao              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_UAO)     >= ARMV8_ID_AA64MMFR2_EL1_UAO_SUPPORTED;
    pFeatures->fLsmaoc           = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_LSM)     >= ARMV8_ID_AA64MMFR2_EL1_LSM_SUPPORTED;
    pFeatures->fIesb             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_IESB)    >= ARMV8_ID_AA64MMFR2_EL1_IESB_SUPPORTED;
    pFeatures->fLva              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_VARANGE) >= ARMV8_ID_AA64MMFR2_EL1_VARANGE_52BITS_64KB_GRAN;
    pFeatures->fCcidx            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_CCIDX)   >= ARMV8_ID_AA64MMFR2_EL1_CCIDX_64BIT;
    pFeatures->fNv               = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_NV)      >= ARMV8_ID_AA64MMFR2_EL1_NV_SUPPORTED;
    pFeatures->fNv2              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_NV)      >= ARMV8_ID_AA64MMFR2_EL1_NV_SUPPORTED_2;
    pFeatures->fTtst             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_ST)      >= ARMV8_ID_AA64MMFR2_EL1_ST_SUPPORTED;
    pFeatures->fLse2             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_AT)      >= ARMV8_ID_AA64MMFR2_EL1_AT_SUPPORTED;
    pFeatures->fIdst             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_IDS)     >= ARMV8_ID_AA64MMFR2_EL1_IDS_EC_18H;
    pFeatures->fS2Fwb            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_FWB)     >= ARMV8_ID_AA64MMFR2_EL1_FWB_SUPPORTED;
    pFeatures->fTtl              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_TTL)     >= ARMV8_ID_AA64MMFR2_EL1_TTL_SUPPORTED;
    pFeatures->fEvt              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_EVT)     >= ARMV8_ID_AA64MMFR2_EL1_EVT_SUPPORTED;
    pFeatures->fE0Pd             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64MMFR2_EL1_E0PD)    >= ARMV8_ID_AA64MMFR2_EL1_E0PD_SUPPORTED;
    return VINF_SUCCESS;
}


/** ID_AA64DFR0_EL1 features. */
static int cpumCpuIdExplodeFeaturesArmV8_IdAa64Dfr0El1(CPUMFEATURESARMV8 *pFeatures, uint64_t const u64IdReg) RT_NOEXCEPT
{
    pFeatures->fDebugV8p1        = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_DEBUGVER) >= ARMV8_ID_AA64DFR0_EL1_DEBUGVER_ARMV8_VHE;
    pFeatures->fDebugV8p2        = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_DEBUGVER) >= ARMV8_ID_AA64DFR0_EL1_DEBUGVER_ARMV8p2;
    pFeatures->fDebugV8p4        = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_DEBUGVER) >= ARMV8_ID_AA64DFR0_EL1_DEBUGVER_ARMV8p4;
    pFeatures->fDebugV8p8        = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_DEBUGVER) >= ARMV8_ID_AA64DFR0_EL1_DEBUGVER_ARMV8p8;
    pFeatures->fPmuV3            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_PMUVER)   >= ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3;
    pFeatures->fPmuV3p1          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_PMUVER)   >= ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3P1;
    pFeatures->fPmuV3p4          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_PMUVER)   >= ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3P4;
    pFeatures->fPmuV3p5          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_PMUVER)   >= ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3P5;
    pFeatures->fPmuV3p7          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_PMUVER)   >= ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3P7;
    pFeatures->fPmuV3p8          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_PMUVER)   >= ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3P8;
    pFeatures->fSpe              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_PMSVER)   >= ARMV8_ID_AA64DFR0_EL1_PMSVER_SUPPORTED;
    pFeatures->fSpeV1p1          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_PMSVER)   >= ARMV8_ID_AA64DFR0_EL1_PMSVER_SUPPORTED_V1P1;
    pFeatures->fSpeV1p2          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_PMSVER)   >= ARMV8_ID_AA64DFR0_EL1_PMSVER_SUPPORTED_V1P2;
    pFeatures->fSpeV1p3          = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_PMSVER)   >= ARMV8_ID_AA64DFR0_EL1_PMSVER_SUPPORTED_V1P3;
    pFeatures->fDoubleLock       = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_DOUBLELOCK)  == ARMV8_ID_AA64DFR0_EL1_DOUBLELOCK_SUPPORTED;
    pFeatures->fTrf              = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_TRACEFILT)   >= ARMV8_ID_AA64DFR0_EL1_TRACEFILT_SUPPORTED;
    pFeatures->fTrbe             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_TRACEBUFFER) >= ARMV8_ID_AA64DFR0_EL1_TRACEBUFFER_SUPPORTED;
    pFeatures->fMtPmu            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_MTPMU)    == ARMV8_ID_AA64DFR0_EL1_MTPMU_SUPPORTED;
    pFeatures->fBrbe             = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_BRBE)     >= ARMV8_ID_AA64DFR0_EL1_BRBE_SUPPORTED;
    pFeatures->fBrbeV1p1         = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_BRBE)     >= ARMV8_ID_AA64DFR0_EL1_BRBE_SUPPORTED_V1P1;
    pFeatures->fHpmn0            = RT_BF_GET(u64IdReg, ARMV8_ID_AA64DFR0_EL1_HPMN0)    >= ARMV8_ID_AA64DFR0_EL1_HPMN0_SUPPORTED;
    return VINF_SUCCESS;
}


/**
 * ARM system register feature exploder table.
 *
 * This can be use by register ID or CPUMARMV8IDREGS offset.
 */
static struct
{
    uint32_t    idReg;
    uint32_t    offIdRegs;
    int       (*pfnHandler)(CPUMFEATURESARMV8 *pFeatures, uint64_t const u64IdReg);
} const g_aArmv8SysRegFeatures[] =
{
# define ENTRY_BOTH(a_idReg, a_CamelName)  \
        { a_idReg, RT_UOFFSETOF(CPUMARMV8IDREGS, RT_CONCAT(u64Reg,a_CamelName)), \
          RT_CONCAT(cpumCpuIdExplodeFeaturesArmV8_,a_CamelName) }
# define ENTRY_IDREG(a_idReg, a_CamelName) \
        { a_idReg, UINT32_MAX, RT_CONCAT(cpumCpuIdExplodeFeaturesArmV8_,a_CamelName) }

    ENTRY_BOTH(ARMV8_AARCH64_SYSREG_ID_AA64MMFR0_EL1, IdAa64Mmfr0El1),
    ENTRY_BOTH(ARMV8_AARCH64_SYSREG_ID_AA64ISAR0_EL1, IdAa64Isar0El1),
    ENTRY_BOTH(ARMV8_AARCH64_SYSREG_ID_AA64ISAR1_EL1, IdAa64Isar1El1),
    ENTRY_BOTH(ARMV8_AARCH64_SYSREG_ID_AA64ISAR2_EL1, IdAa64Isar2El1),
    ENTRY_BOTH(ARMV8_AARCH64_SYSREG_ID_AA64PFR0_EL1,  IdAa64Pfr0El1),
    ENTRY_BOTH(ARMV8_AARCH64_SYSREG_ID_AA64PFR1_EL1,  IdAa64Pfr1El1),
    ENTRY_BOTH(ARMV8_AARCH64_SYSREG_ID_AA64MMFR0_EL1, IdAa64Mmfr0El1),
    ENTRY_BOTH(ARMV8_AARCH64_SYSREG_ID_AA64MMFR1_EL1, IdAa64Mmfr1El1),
    ENTRY_BOTH(ARMV8_AARCH64_SYSREG_ID_AA64MMFR2_EL1, IdAa64Mmfr2El1),
    ENTRY_BOTH(ARMV8_AARCH64_SYSREG_ID_AA64DFR0_EL1,  IdAa64Dfr0El1),
    /** @todo  ENTRY_BOTH(ARMV8_AARCH64_SYSREG_ID_AA64DFR1_EL1,  IdAa64Dfr1El1),  */
};


/**
 * Explode the CPU features from the given ID registers.
 *
 * @returns VBox status code.
 * @param   pIdRegs             The ID registers to explode the features from.
 * @param   pFeatures           Where to store the features to.
 */
DECLHIDDEN(int) cpumCpuIdExplodeFeaturesArmV8FromIdRegs(PCCPUMARMV8IDREGS pIdRegs, CPUMFEATURESARMV8 *pFeatures)
{
    int rcRet = VINF_SUCCESS;
    for (size_t i = 0; i < RT_ELEMENTS(g_aArmv8SysRegFeatures); i++)
        if (g_aArmv8SysRegFeatures[i].offIdRegs <= sizeof(*pIdRegs) - sizeof(uint64_t))
        {
            uint64_t const uValue = *(uint64_t *)((uintptr_t)pIdRegs + g_aArmv8SysRegFeatures[i].offIdRegs);
            int rc = g_aArmv8SysRegFeatures[i].pfnHandler(pFeatures, uValue);
            if (RT_FAILURE(rc))
                rcRet = rc;
        }

    return rcRet;
}


/**
 * Explode the CPU features from the list of system registers.
 *
 * @returns VBox status code.
 * @param   paSysRegs           The system register collection to explode the
 *                              features from.
 * @param   cSysRegs            The number of system registers in @a paSysRegs.
 * @param   pFeatures           Where to store the features to.
 */
DECLHIDDEN(int) cpumCpuIdExplodeFeaturesArmV8FromSysRegs(PCSUPARMSYSREGVAL paSysRegs, uint32_t cSysRegs,
                                                         CPUMFEATURESARMV8 *pFeatures)
{
    int rcRet = VINF_SUCCESS;
    for (size_t i = 0; i < RT_ELEMENTS(g_aArmv8SysRegFeatures); i++)
    {
        uint32_t const idReg = g_aArmv8SysRegFeatures[i].idReg;
        for (size_t idxSysReg = 0; idxSysReg < cSysRegs; idxSysReg++)
            if (paSysRegs[idxSysReg].idReg == idReg)
            {
                int rc = g_aArmv8SysRegFeatures[i].pfnHandler(pFeatures, paSysRegs[idxSysReg].uValue);
                if (RT_FAILURE(rc))
                    rcRet = rc;
                break;
            }
    }

    return rcRet;
}

# endif
#endif /* defined(RT_ARCH_ARM64) || defined(VBOX_VMM_TARGET_ARMV8) */

