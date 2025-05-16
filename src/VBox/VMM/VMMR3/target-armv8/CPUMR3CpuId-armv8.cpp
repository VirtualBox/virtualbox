/* $Id: CPUMR3CpuId-armv8.cpp 168915 2025-05-16 21:41:53Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - CPU ID part for ARMv8 hypervisor.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#define CPUM_WITH_NONCONST_HOST_FEATURES /* required for initializing parts of the g_CpumHostFeatures structure here. */
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/ssm.h>
#include "CPUMInternal-armv8.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/gic.h>

#include <VBox/err.h>
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/*
 *
 * Init related code.
 * Init related code.
 * Init related code.
 *
 *
 */

/** @name Instruction Set Extension Options
 * @{  */
/** Configuration option type (extended boolean, really). */
typedef uint8_t CPUMISAEXTCFG;
/** Always disable the extension. */
#define CPUMISAEXTCFG_DISABLED              false
/** Enable the extension if it's supported by the host CPU. */
#define CPUMISAEXTCFG_ENABLED_SUPPORTED     true
/** Enable the extension if it's supported by the host CPU, but don't let
 * the portable CPUID feature disable it. */
#define CPUMISAEXTCFG_ENABLED_PORTABLE      UINT8_C(127)
/** Always enable the extension. */
#define CPUMISAEXTCFG_ENABLED_ALWAYS        UINT8_C(255)
/** @} */

/**
 * CPUID Configuration (from CFGM).
 *
 * @remarks  The members aren't document since we would only be duplicating the
 *           \@cfgm entries in cpumR3CpuIdReadConfig.
 */
typedef struct CPUMCPUIDCONFIG
{
    CPUMISAEXTCFG   enmAes;
    CPUMISAEXTCFG   enmPmull;
    CPUMISAEXTCFG   enmSha1;
    CPUMISAEXTCFG   enmSha256;
    CPUMISAEXTCFG   enmSha512;
    CPUMISAEXTCFG   enmCrc32;
    CPUMISAEXTCFG   enmSha3;

    char            szCpuName[128];
} CPUMCPUIDCONFIG;
/** Pointer to CPUID config (from CFGM). */
typedef CPUMCPUIDCONFIG *PCPUMCPUIDCONFIG;


/**
 * Sanitizes and adjust the CPUID leaves.
 *
 * Drop features that aren't virtualized (or virtualizable).  Adjust information
 * and capabilities to fit the virtualized hardware.  Remove information the
 * guest shouldn't have (because it's wrong in the virtual world or because it
 * gives away host details) or that we don't have documentation for and no idea
 * what means.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure (for cCpus).
 * @param   pCpum       The CPUM instance data.
 * @param   pConfig     The CPUID configuration we've read from CFGM.
 * @param   pCpumCfg    The CPUM CFGM config.
 */
static int cpumR3CpuIdSanitize(PVM pVM, PCPUM pCpum, PCPUMCPUIDCONFIG pConfig, PCFGMNODE pCpumCfg)
{
#define PORTABLE_CLEAR_BITS_WHEN(Lvl, a_pLeafReg, FeatNm, fMask, uValue) \
        if ( pCpum->u8PortableCpuIdLevel >= (Lvl) && ((a_pLeafReg) & (fMask)) == (uValue) ) \
        { \
            LogRel(("PortableCpuId: " #a_pLeafReg "[" #FeatNm "]: %#x -> 0\n", (a_pLeafReg) & (fMask))); \
            (a_pLeafReg) &= ~(uint32_t)(fMask); \
        }
#define PORTABLE_DISABLE_FEATURE_BIT(Lvl, a_pLeafReg, FeatNm, fBitMask) \
        if ( pCpum->u8PortableCpuIdLevel >= (Lvl) && ((a_pLeafReg) & (fBitMask)) ) \
        { \
            LogRel(("PortableCpuId: " #a_pLeafReg "[" #FeatNm "]: 1 -> 0\n")); \
            (a_pLeafReg) &= ~(uint32_t)(fBitMask); \
        }
#define PORTABLE_DISABLE_FEATURE_BIT_CFG(Lvl, a_IdReg, a_FeatNm, a_IdRegValCheck, enmConfig, a_IdRegValNotSup) \
        if (   pCpum->u8PortableCpuIdLevel >= (Lvl) \
            && (RT_BF_GET(a_IdReg, a_FeatNm) >= a_IdRegValCheck) \
            && (enmConfig) != CPUMISAEXTCFG_ENABLED_PORTABLE ) \
        { \
            LogRel(("PortableCpuId: [" #a_FeatNm "]: 1 -> 0\n")); \
            (a_IdReg) = RT_BF_SET(a_IdReg, a_FeatNm, a_IdRegValNotSup); \
        }
    //Assert(pCpum->GuestFeatures.enmCpuVendor != CPUMCPUVENDOR_INVALID);

    /* The CPUID entries we start with here isn't necessarily the ones of the host, so we
       must consult HostFeatures when processing CPUMISAEXTCFG variables. */
#ifdef RT_ARCH_ARM64
    PCCPUMFEATURES pHstFeat = &pCpum->HostFeatures.s;
# define PASSTHRU_FEATURE(a_IdReg, enmConfig, fHostFeature, a_IdRegNm, a_IdRegValSup, a_IdRegValNotSup) do { \
            (a_IdReg) = (enmConfig) && ((enmConfig) == CPUMISAEXTCFG_ENABLED_ALWAYS || (fHostFeature)) \
                      ? RT_BF_SET(a_IdReg, a_IdRegNm, a_IdRegValSup) \
                      : RT_BF_SET(a_IdReg, a_IdRegNm, a_IdRegValNotSup); \
        } while (0)
#else
# define PASSTHRU_FEATURE(a_IdReg, enmConfig, fHostFeature, a_IdRegNm, a_IdRegValSup, a_IdRegValNotSup) do { \
            (a_IdReg) = (enmConfig) != CPUMISAEXTCFG_DISABLED \
                      ? RT_BF_SET(a_IdReg, a_IdRegNm, a_IdRegValSup) \
                      : RT_BF_SET(a_IdReg, a_IdRegNm, a_IdRegValNotSup); \
        } while (0)
#endif

    /* ID_AA64ISAR0_EL1 */
    uint64_t u64ValTmp = 0;
    uint64_t u64IdReg = pVM->cpum.s.GuestIdRegs.u64RegIdAa64Isar0El1;

    PASSTHRU_FEATURE(u64IdReg, pConfig->enmAes,     pHstFeat->fAes,     ARMV8_ID_AA64ISAR0_EL1_AES,     ARMV8_ID_AA64ISAR0_EL1_AES_SUPPORTED,                   ARMV8_ID_AA64ISAR0_EL1_AES_NOT_IMPL);
    u64ValTmp = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_AES) == ARMV8_ID_AA64ISAR0_EL1_AES_SUPPORTED ? ARMV8_ID_AA64ISAR0_EL1_AES_SUPPORTED : ARMV8_ID_AA64ISAR0_EL1_AES_NOT_IMPL;
    PASSTHRU_FEATURE(u64IdReg, pConfig->enmPmull,   pHstFeat->fPmull,   ARMV8_ID_AA64ISAR0_EL1_AES,     ARMV8_ID_AA64ISAR0_EL1_AES_SUPPORTED_PMULL,             u64ValTmp);
    PASSTHRU_FEATURE(u64IdReg, pConfig->enmSha1,    pHstFeat->fSha1,    ARMV8_ID_AA64ISAR0_EL1_SHA1,    ARMV8_ID_AA64ISAR0_EL1_SHA1_SUPPORTED,                  ARMV8_ID_AA64ISAR0_EL1_SHA1_NOT_IMPL);
    PASSTHRU_FEATURE(u64IdReg, pConfig->enmSha256,  pHstFeat->fSha256,  ARMV8_ID_AA64ISAR0_EL1_SHA2,    ARMV8_ID_AA64ISAR0_EL1_SHA2_SUPPORTED_SHA256,           ARMV8_ID_AA64ISAR0_EL1_SHA2_NOT_IMPL);
    u64ValTmp = RT_BF_GET(u64IdReg, ARMV8_ID_AA64ISAR0_EL1_SHA2) == ARMV8_ID_AA64ISAR0_EL1_SHA2_SUPPORTED_SHA256 ? ARMV8_ID_AA64ISAR0_EL1_SHA2_SUPPORTED_SHA256 : ARMV8_ID_AA64ISAR0_EL1_SHA2_NOT_IMPL;
    PASSTHRU_FEATURE(u64IdReg, pConfig->enmSha512,  pHstFeat->fSha512,  ARMV8_ID_AA64ISAR0_EL1_SHA2,    ARMV8_ID_AA64ISAR0_EL1_SHA2_SUPPORTED_SHA256_SHA512,    u64ValTmp);
    PASSTHRU_FEATURE(u64IdReg, pConfig->enmCrc32,   pHstFeat->fCrc32,   ARMV8_ID_AA64ISAR0_EL1_CRC32,   ARMV8_ID_AA64ISAR0_EL1_CRC32_SUPPORTED,                 ARMV8_ID_AA64ISAR0_EL1_CRC32_NOT_IMPL);
    PASSTHRU_FEATURE(u64IdReg, pConfig->enmSha3,    pHstFeat->fSha3,    ARMV8_ID_AA64ISAR0_EL1_SHA3,    ARMV8_ID_AA64ISAR0_EL1_SHA3_SUPPORTED,                  ARMV8_ID_AA64ISAR0_EL1_SHA3_NOT_IMPL);

    if (pCpum->u8PortableCpuIdLevel > 0)
    {
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, u64IdReg, ARMV8_ID_AA64ISAR0_EL1_AES,   ARMV8_ID_AA64ISAR0_EL1_AES_SUPPORTED,                   pConfig->enmAes,    ARMV8_ID_AA64ISAR0_EL1_AES_NOT_IMPL);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, u64IdReg, ARMV8_ID_AA64ISAR0_EL1_AES,   ARMV8_ID_AA64ISAR0_EL1_AES_SUPPORTED_PMULL,             pConfig->enmPmull,  ARMV8_ID_AA64ISAR0_EL1_AES_NOT_IMPL);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, u64IdReg, ARMV8_ID_AA64ISAR0_EL1_SHA1,  ARMV8_ID_AA64ISAR0_EL1_SHA1_SUPPORTED,                  pConfig->enmSha1,   ARMV8_ID_AA64ISAR0_EL1_SHA1_NOT_IMPL);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, u64IdReg, ARMV8_ID_AA64ISAR0_EL1_SHA2,  ARMV8_ID_AA64ISAR0_EL1_SHA2_SUPPORTED_SHA256,           pConfig->enmSha256, ARMV8_ID_AA64ISAR0_EL1_SHA2_NOT_IMPL);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, u64IdReg, ARMV8_ID_AA64ISAR0_EL1_SHA2,  ARMV8_ID_AA64ISAR0_EL1_SHA2_SUPPORTED_SHA256_SHA512,    pConfig->enmSha512, ARMV8_ID_AA64ISAR0_EL1_SHA2_NOT_IMPL);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, u64IdReg, ARMV8_ID_AA64ISAR0_EL1_CRC32, ARMV8_ID_AA64ISAR0_EL1_CRC32_SUPPORTED,                 pConfig->enmCrc32,  ARMV8_ID_AA64ISAR0_EL1_CRC32_NOT_IMPL);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, u64IdReg, ARMV8_ID_AA64ISAR0_EL1_SHA3,  ARMV8_ID_AA64ISAR0_EL1_SHA3_SUPPORTED,                  pConfig->enmSha3,   ARMV8_ID_AA64ISAR0_EL1_SHA3_NOT_IMPL);
    }

    /* Write ID_AA64ISAR0_EL1 register back. */
    pVM->cpum.s.GuestIdRegs.u64RegIdAa64Isar0El1 = u64IdReg;

    /* ID_AA64PFR0_EL1 */
    u64IdReg = pVM->cpum.s.GuestIdRegs.u64RegIdAa64Pfr0El1;

    uint8_t uArchRev;
    int rc = CFGMR3QueryU8(pCpumCfg, "GicArchRev", &uArchRev);
    AssertRCReturn(rc, rc);
    if (uArchRev == GIC_DIST_REG_PIDR2_ARCHREV_GICV3)
        u64IdReg = RT_BF_SET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_GIC, ARMV8_ID_AA64PFR0_EL1_GIC_V3_V4); /* 3.0 */
    else if (uArchRev == GIC_DIST_REG_PIDR2_ARCHREV_GICV4)
    {
        uint8_t uArchRevMinor = 0;
        rc = CFGMR3QueryU8Def(pCpumCfg, "GicArchRevMinor", &uArchRevMinor, 0);
        u64IdReg = uArchRevMinor == 0
                 ? RT_BF_SET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_GIC, ARMV8_ID_AA64PFR0_EL1_GIC_V3_V4)  /* 4.0 */
                 : RT_BF_SET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_GIC, ARMV8_ID_AA64PFR0_EL1_GIC_V4_1);  /* 4.1 */
    }
    else
        Assert(RT_BF_GET(u64IdReg, ARMV8_ID_AA64PFR0_EL1_GIC) == ARMV8_ID_AA64PFR0_EL1_GIC_NOT_IMPL);

    /* Write ID_AA64PFR0_EL1 register back. */
    pVM->cpum.s.GuestIdRegs.u64RegIdAa64Pfr0El1 = u64IdReg;

    /** @todo Other ID and feature registers. */

    return VINF_SUCCESS;
#undef PORTABLE_DISABLE_FEATURE_BIT
#undef PORTABLE_CLEAR_BITS_WHEN
}


/**
 * Reads a value in /CPUM/IsaExts/ node.
 *
 * @returns VBox status code (error message raised).
 * @param   pVM             The cross context VM structure. (For errors.)
 * @param   pIsaExts        The /CPUM/IsaExts node (can be NULL).
 * @param   pszValueName    The value / extension name.
 * @param   penmValue       Where to return the choice.
 * @param   enmDefault      The default choice.
 */
static int cpumR3CpuIdReadIsaExtCfg(PVM pVM, PCFGMNODE pIsaExts, const char *pszValueName,
                                    CPUMISAEXTCFG *penmValue, CPUMISAEXTCFG enmDefault)
{
    /*
     * Try integer encoding first.
     */
    uint64_t uValue;
    int rc = CFGMR3QueryInteger(pIsaExts, pszValueName, &uValue);
    if (RT_SUCCESS(rc))
        switch (uValue)
        {
            case 0: *penmValue = CPUMISAEXTCFG_DISABLED; break;
            case 1: *penmValue = CPUMISAEXTCFG_ENABLED_SUPPORTED; break;
            case 2: *penmValue = CPUMISAEXTCFG_ENABLED_ALWAYS; break;
            case 9: *penmValue = CPUMISAEXTCFG_ENABLED_PORTABLE; break;
            default:
                return VMSetError(pVM, VERR_CPUM_INVALID_CONFIG_VALUE, RT_SRC_POS,
                                  "Invalid config value for '/CPUM/IsaExts/%s': %llu (expected 0/'disabled', 1/'enabled', 2/'portable', or 9/'forced')",
                                  pszValueName, uValue);
        }
    /*
     * If missing, use default.
     */
    else if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
        *penmValue = enmDefault;
    else
    {
        if (rc == VERR_CFGM_NOT_INTEGER)
        {
            /*
             * Not an integer, try read it as a string.
             */
            char szValue[32];
            rc = CFGMR3QueryString(pIsaExts, pszValueName, szValue, sizeof(szValue));
            if (RT_SUCCESS(rc))
            {
                RTStrToLower(szValue);
                size_t cchValue = strlen(szValue);
#define EQ(a_str) (cchValue == sizeof(a_str) - 1U && memcmp(szValue, a_str, sizeof(a_str) - 1))
                if (     EQ("disabled") || EQ("disable") || EQ("off") || EQ("no"))
                    *penmValue = CPUMISAEXTCFG_DISABLED;
                else if (EQ("enabled")  || EQ("enable")  || EQ("on")  || EQ("yes"))
                    *penmValue = CPUMISAEXTCFG_ENABLED_SUPPORTED;
                else if (EQ("forced")   || EQ("force")   || EQ("always"))
                    *penmValue = CPUMISAEXTCFG_ENABLED_ALWAYS;
                else if (EQ("portable"))
                    *penmValue = CPUMISAEXTCFG_ENABLED_PORTABLE;
                else if (EQ("default")  || EQ("def"))
                    *penmValue = enmDefault;
                else
                    return VMSetError(pVM, VERR_CPUM_INVALID_CONFIG_VALUE, RT_SRC_POS,
                                      "Invalid config value for '/CPUM/IsaExts/%s': '%s' (expected 0/'disabled', 1/'enabled', 2/'portable', or 9/'forced')",
                                      pszValueName, uValue);
#undef EQ
            }
        }
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Error reading config value '/CPUM/IsaExts/%s': %Rrc", pszValueName, rc);
    }
    return VINF_SUCCESS;
}


#if 0
/**
 * Reads a value in /CPUM/IsaExts/ node, forcing it to DISABLED if wanted.
 *
 * @returns VBox status code (error message raised).
 * @param   pVM             The cross context VM structure. (For errors.)
 * @param   pIsaExts        The /CPUM/IsaExts node (can be NULL).
 * @param   pszValueName    The value / extension name.
 * @param   penmValue       Where to return the choice.
 * @param   enmDefault      The default choice.
 * @param   fAllowed        Allowed choice.  Applied both to the result and to
 *                          the default value.
 */
static int cpumR3CpuIdReadIsaExtCfgEx(PVM pVM, PCFGMNODE pIsaExts, const char *pszValueName,
                                      CPUMISAEXTCFG *penmValue, CPUMISAEXTCFG enmDefault, bool fAllowed)
{
    int rc;
    if (fAllowed)
        rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, pszValueName, penmValue, enmDefault);
    else
    {
        rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, pszValueName, penmValue, false /*enmDefault*/);
        if (RT_SUCCESS(rc) && *penmValue == CPUMISAEXTCFG_ENABLED_ALWAYS)
            LogRel(("CPUM: Ignoring forced '%s'\n", pszValueName));
        *penmValue = CPUMISAEXTCFG_DISABLED;
    }
    return rc;
}
#endif

static int cpumR3CpuIdReadConfig(PVM pVM, PCPUMCPUIDCONFIG pConfig, PCFGMNODE pCpumCfg)
{
    /** @cfgm{/CPUM/PortableCpuIdLevel, 8-bit, 0, 3, 0}
     * When non-zero CPUID features that could cause portability issues will be
     * stripped.  The higher the value the more features gets stripped.  Higher
     * values should only be used when older CPUs are involved since it may
     * harm performance and maybe also cause problems with specific guests. */
    int rc = CFGMR3QueryU8Def(pCpumCfg, "PortableCpuIdLevel", &pVM->cpum.s.u8PortableCpuIdLevel, 0);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/GuestCpuName, string}
     * The name of the CPU we're to emulate.  The default is the host CPU.
     * Note! CPUs other than "host" one is currently unsupported. */
    rc = CFGMR3QueryStringDef(pCpumCfg, "GuestCpuName", pConfig->szCpuName, sizeof(pConfig->szCpuName), "host");
    AssertLogRelRCReturn(rc, rc);

    /*
     * Instruction Set Architecture (ISA) Extensions.
     */
    PCFGMNODE pIsaExts = CFGMR3GetChild(pCpumCfg, "IsaExts");
    if (pIsaExts)
    {
        rc = CFGMR3ValidateConfig(pIsaExts, "/CPUM/IsaExts/",
                                   "AES"
                                   "|PMULL"
                                   "|SHA1"
                                   "|SHA256"
                                   "|SHA512"
                                   "|CRC32"
                                   "|SHA3"
                                  , "" /*pszValidNodes*/, "CPUM" /*pszWho*/, 0 /*uInstance*/);
        if (RT_FAILURE(rc))
            return rc;
    }

    /** @cfgm{/CPUM/IsaExts/AES, boolean, depends}
     * Expose FEAT_AES instruction set extension to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "AES", &pConfig->enmAes, true /*enmDefault*/);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/AES, boolean, depends}
     * Expose FEAT_AES and FEAT_PMULL instruction set extension to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "PMULL", &pConfig->enmPmull, true /*enmDefault*/);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/SHA1, boolean, depends}
     * Expose FEAT_SHA1 instruction set extension to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "SHA1", &pConfig->enmSha1, true /*enmDefault*/);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/SHA256, boolean, depends}
     * Expose FEAT_SHA256 instruction set extension to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "SHA256", &pConfig->enmSha256, true /*enmDefault*/);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/SHA512, boolean, depends}
     * Expose FEAT_SHA256 and FEAT_SHA512 instruction set extension to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "SHA512", &pConfig->enmSha512, true /*enmDefault*/);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/CRC32, boolean, depends}
     * Expose FEAT_CRC32 instruction set extension to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "CRC32", &pConfig->enmCrc32, true /*enmDefault*/);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/SHA3, boolean, depends}
     * Expose FEAT_SHA3 instruction set extension to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "SHA3", &pConfig->enmSha3, true /*enmDefault*/);
    AssertLogRelRCReturn(rc, rc);

    /** @todo Add more options for other extensions. */

    return VINF_SUCCESS;
}


/**
 * Populates the host and guest features by the given ID registers.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pIdRegs             Pointer to the ID register struct.
 *
 * @note Unlike on x86 there is no cross platform usermode accessible way to get at the CPU features.
 *       On ARM there are some ID_AA64*_EL1 system registers accessible by EL1 and higher only so we have to
 *       rely on the host/NEM backend to query those and hand them to CPUM where they will be parsed and modified based
 *       on the VM config.
 */
VMMR3DECL(int) CPUMR3PopulateFeaturesByIdRegisters(PVM pVM, PCCPUMARMV8IDREGS pIdRegs)
{
    /* Set the host features from the given ID registers. */
#ifdef RT_ARCH_ARM64
    int rcHost = cpumCpuIdExplodeFeaturesArmV8FromIdRegs(pIdRegs, &g_CpumHostFeatures.s);
    AssertRCReturn(rcHost, rcHost);
    pVM->cpum.s.HostFeatures.s             = g_CpumHostFeatures.s;
    pVM->cpum.s.HostIdRegs                 = *pIdRegs;
#endif

    pVM->cpum.s.GuestFeatures.enmCpuVendor = pVM->cpum.s.HostFeatures.Common.enmCpuVendor;
    pVM->cpum.s.GuestIdRegs                = *pIdRegs;

    PCPUM       pCpum    = &pVM->cpum.s;
    PCFGMNODE   pCpumCfg = CFGMR3GetChild(CFGMR3GetRoot(pVM), "CPUM");

    /*
     * Read the configuration.
     */
    CPUMCPUIDCONFIG Config;
    RT_ZERO(Config);

    int rc = cpumR3CpuIdReadConfig(pVM, &Config, pCpumCfg);
    AssertRCReturn(rc, rc);

#if 0
    /*
     * Get the guest CPU data from the database and/or the host.
     *
     * The CPUID and MSRs are currently living on the regular heap to avoid
     * fragmenting the hyper heap (and because there isn't/wasn't any realloc
     * API for the hyper heap).  This means special cleanup considerations.
     */
    rc = cpumR3DbGetCpuInfo(Config.szCpuName, &pCpum->GuestInfo);
    if (RT_FAILURE(rc))
        return rc == VERR_CPUM_DB_CPU_NOT_FOUND
             ? VMSetError(pVM, rc, RT_SRC_POS,
                          "Info on guest CPU '%s' could not be found. Please, select a different CPU.", Config.szCpuName)
             : rc;
#endif

    /*
     * Pre-explode the CPUID info.
     */
    if (RT_SUCCESS(rc))
        rc = cpumCpuIdExplodeFeaturesArmV8FromIdRegs(pIdRegs, &pCpum->GuestFeatures);

    /*
     * Sanitize the cpuid information passed on to the guest.
     */
    if (RT_SUCCESS(rc))
        rc = cpumR3CpuIdSanitize(pVM, pCpum, &Config, pCpumCfg);

    return rc;
}


/**
 * Queries the pointer to the VM wide ID registers exposing configured features to the guest.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   ppIdRegs            Where to store the pointer to the guest ID register struct.
 */
VMMR3_INT_DECL(int) CPUMR3QueryGuestIdRegs(PVM pVM, PCCPUMARMV8IDREGS *ppIdRegs)
{
    AssertPtrReturn(ppIdRegs, VERR_INVALID_POINTER);

    *ppIdRegs = &pVM->cpum.s.GuestIdRegs;
    return VINF_SUCCESS;
}


/*
 *
 *
 * Saved state related code.
 * Saved state related code.
 * Saved state related code.
 *
 *
 */
/** Saved state field descriptors for CPUMARMV8IDREGS. */
static const SSMFIELD g_aCpumArmV8IdRegsFields[] =
{
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Pfr0El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Pfr1El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Dfr0El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Dfr1El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Afr0El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Afr1El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Isar0El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Isar1El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Isar2El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Mmfr0El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Mmfr1El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegIdAa64Mmfr2El1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegClidrEl1),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegCtrEl0),
    SSMFIELD_ENTRY(CPUMARMV8IDREGS, u64RegDczidEl0),
    SSMFIELD_ENTRY_TERM()
};


/**
 * Called both in pass 0 and the final pass.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 */
void cpumR3SaveCpuId(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Save all the CPU ID leaves.
     */
    SSMR3PutStructEx(pSSM, &pVM->cpum.s.GuestIdRegs, sizeof(pVM->cpum.s.GuestIdRegs), 0, g_aCpumArmV8IdRegsFields, NULL);
}


/**
 * Loads the CPU ID leaves saved by pass 0, inner worker.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 * @param   uVersion            The format version.
 * @param   pGuestIdRegs        The guest ID register as loaded from the saved state.
 */
static int cpumR3LoadCpuIdInner(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, PCCPUMARMV8IDREGS pGuestIdRegs)
{
    /*
     * This can be skipped.
     */
    bool fStrictCpuIdChecks;
    CFGMR3QueryBoolDef(CFGMR3GetChild(CFGMR3GetRoot(pVM), "CPUM"), "StrictCpuIdChecks", &fStrictCpuIdChecks, true);

    /*
     * Define a bunch of macros for simplifying the santizing/checking code below.
     */
    /* For checking guest features. */
#define CPUID_GST_FEATURE_RET(a_IdReg, a_Field) \
    do { \
        if (RT_BF_GET(pGuestIdRegs->a_IdReg, a_Field) > RT_BF_GET(pVM->cpum.s.GuestIdRegs.a_IdReg, a_Field)) \
        { \
            if (fStrictCpuIdChecks) \
                return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS, \
                                         N_(#a_Field " is not supported by the host but has already exposed to the guest")); \
            LogRel(("CPUM: " #a_Field " is not supported by the host but has already been exposed to the guest\n")); \
        } \
    } while (0)
#define CPUID_GST_FEATURE_RET_NOT_IMPL(a_IdReg, a_Field, a_NotImpl) \
    do { \
        if (   (   RT_BF_GET(pGuestIdRegs->a_IdReg, a_Field) != (a_NotImpl) \
                && RT_BF_GET(pVM->cpum.s.GuestIdRegs.a_IdReg, a_Field) == (a_NotImpl)) \
            || RT_BF_GET(pGuestIdRegs->a_IdReg, a_Field) > RT_BF_GET(pVM->cpum.s.GuestIdRegs.a_IdReg, a_Field)) \
        { \
            if (fStrictCpuIdChecks) \
                return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS, \
                                         N_(#a_Field " is not supported by the host but has already exposed to the guest")); \
            LogRel(("CPUM: " #a_Field " is not supported by the host but has already been exposed to the guest\n")); \
        } \
    } while (0)
#define CPUID_GST_FEATURE_WRN(a_IdReg, a_Field) \
    do { \
        if (RT_BF_GET(pGuestIdRegs->a_IdReg, a_Field) > RT_BF_GET(pVM->cpum.s.GuestIdRegs.a_IdReg, a_Field)) \
            LogRel(("CPUM: " #a_Field " is not supported by the host but has already been exposed to the guest\n")); \
    } while (0)
#define CPUID_GST_FEATURE_EMU(a_IdReg, a_Field) \
    do { \
        if (RT_BF_GET(pGuestIdRegs->a_IdReg, a_Field) > RT_BF_GET(pVM->cpum.s.GuestIdRegs.a_IdReg, a_Field)) \
            LogRel(("CPUM: Warning - " #a_Field " is not supported by the host but has already been exposed to the guest. This may impact performance.\n")); \
    } while (0)
#define CPUID_GST_FEATURE_IGN(a_IdReg, a_Field) do { } while (0)

    RT_NOREF(uVersion);
    /*
     * Verify that we can support the features already exposed to the guest on
     * this host.
     *
     * Most of the features we're emulating requires intercepting instruction
     * and doing it the slow way, so there is no need to warn when they aren't
     * present in the host CPU.  Thus we use IGN instead of EMU on these.
     *
     * Trailing comments:
     *      "EMU"  - Possible to emulate, could be lots of work and very slow.
     *      "EMU?" - Can this be emulated?
     */
    /* ID_AA64ISAR0_EL1 */
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_AES);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_SHA1);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_SHA2);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_CRC32);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_ATOMIC);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_TME);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_RDM);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_SHA3);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_SM3);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_SM4);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_DP);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_FHM);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_TS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_TLB);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar0El1, ARMV8_ID_AA64ISAR0_EL1_RNDR);

    /* ID_AA64ISAR1_EL1 */
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_DPB);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_APA);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_API);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_FJCVTZS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_LRCPC);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_GPA);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_GPI);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_FRINTTS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_SB);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_SPECRES);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_BF16);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_DGH);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_I8MM);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_XS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar1El1, ARMV8_ID_AA64ISAR1_EL1_LS64);

    /* ID_AA64ISAR2_EL1 */
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar2El1, ARMV8_ID_AA64ISAR2_EL1_WFXT);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar2El1, ARMV8_ID_AA64ISAR2_EL1_RPRES);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar2El1, ARMV8_ID_AA64ISAR2_EL1_GPA3);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar2El1, ARMV8_ID_AA64ISAR2_EL1_APA3);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar2El1, ARMV8_ID_AA64ISAR2_EL1_MOPS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar2El1, ARMV8_ID_AA64ISAR2_EL1_BC);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Isar2El1, ARMV8_ID_AA64ISAR2_EL1_PACFRAC);

    /* ID_AA64PFR0_EL1 */
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_EL0);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_EL1);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_EL2);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_EL3);
    CPUID_GST_FEATURE_RET_NOT_IMPL(u64RegIdAa64Pfr0El1, ARMV8_ID_AA64PFR0_EL1_FP,      ARMV8_ID_AA64PFR0_EL1_FP_NOT_IMPL);      /* Special not implemented value. */
    CPUID_GST_FEATURE_RET_NOT_IMPL(u64RegIdAa64Pfr0El1, ARMV8_ID_AA64PFR0_EL1_ADVSIMD, ARMV8_ID_AA64PFR0_EL1_ADVSIMD_NOT_IMPL); /* Special not implemented value. */
    CPUID_GST_FEATURE_IGN(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_GIC);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_RAS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_SVE);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_SEL2);
    /** @todo MPAM */
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_AMU);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_DIT);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_RME);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_CSV2);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr0El1,  ARMV8_ID_AA64PFR0_EL1_CSV3);

    /* ID_AA64PFR1_EL1 */
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr1El1,  ARMV8_ID_AA64PFR1_EL1_BT);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr1El1,  ARMV8_ID_AA64PFR1_EL1_SSBS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr1El1,  ARMV8_ID_AA64PFR1_EL1_MTE);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr1El1,  ARMV8_ID_AA64PFR1_EL1_RASFRAC);
    /** @todo MPAM. */
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr1El1,  ARMV8_ID_AA64PFR1_EL1_SME);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr1El1,  ARMV8_ID_AA64PFR1_EL1_RNDRTRAP);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr1El1,  ARMV8_ID_AA64PFR1_EL1_CSV2FRAC);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Pfr1El1,  ARMV8_ID_AA64PFR1_EL1_NMI);

    /* ID_AA64MMFR0_EL1 */
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_PARANGE);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_ASIDBITS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_BIGEND);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_SNSMEM);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_BIGENDEL0);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_TGRAN16);
    CPUID_GST_FEATURE_RET_NOT_IMPL(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_TGRAN64, ARMV8_ID_AA64MMFR0_EL1_TGRAN64_NOT_IMPL);
    CPUID_GST_FEATURE_RET_NOT_IMPL(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_TGRAN4,  ARMV8_ID_AA64MMFR0_EL1_TGRAN4_NOT_IMPL);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_TGRAN16_2);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_TGRAN64_2);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_TGRAN4_2);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_EXS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_FGT);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr0El1, ARMV8_ID_AA64MMFR0_EL1_ECV);

    /* ID_AA64MMFR1_EL1 */
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_HAFDBS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_VMIDBITS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_VHE);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_HPDS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_LO);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_PAN);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_SPECSEI);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_XNX);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_TWED);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_ETS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_HCX);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_AFP);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_NTLBPA);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_TIDCP1);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr1El1, ARMV8_ID_AA64MMFR1_EL1_CMOW);

    /* ID_AA64MMFR2_EL1 */
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_CNP);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_UAO);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_LSM);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_IESB);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_VARANGE);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_CCIDX);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_CNP);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_NV);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_ST);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_AT);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_IDS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_FWB);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_TTL);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_BBM);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_EVT);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Mmfr2El1, ARMV8_ID_AA64MMFR2_EL1_E0PD);

    /* ID_AA64DFR0_EL1 */
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_DEBUGVER);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_TRACEVER);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_PMUVER);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_BRPS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_WRPS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_CTXCMPS);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_PMSVER);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_DOUBLELOCK);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_TRACEFILT);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_TRACEBUFFER);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_MTPMU);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_BRBE);
    CPUID_GST_FEATURE_RET(u64RegIdAa64Dfr0El1, ARMV8_ID_AA64DFR0_EL1_HPMN0);

#undef CPUID_GST_FEATURE_RET
#undef CPUID_GST_FEATURE_RET_NOT_IMPL
#undef CPUID_GST_FEATURE_WRN
#undef CPUID_GST_FEATURE_EMU
#undef CPUID_GST_FEATURE_IGN

    /*
     * We're good, commit the CPU ID registers.
     */
    pVM->cpum.s.GuestIdRegs = *pGuestIdRegs;
    return VINF_SUCCESS;
}


/**
 * Loads the CPU ID leaves saved by pass 0, ARMv8 targets.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 * @param   uVersion            The format version.
 */
int cpumR3LoadCpuIdArmV8(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion)
{
    CPUMARMV8IDREGS GuestIdRegs;
    int rc = SSMR3GetStructEx(pSSM, &GuestIdRegs, sizeof(GuestIdRegs), 0, g_aCpumArmV8IdRegsFields, NULL);
    AssertRCReturn(rc, rc);

    return cpumR3LoadCpuIdInner(pVM, pSSM, uVersion, &GuestIdRegs);
}


/*
 *
 *
 * CPUID Info Handler.
 * CPUID Info Handler.
 * CPUID Info Handler.
 *
 *
 */

/** CLIDR_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aClidrEl1Fields[] =
{
    DBGFREGSUBFIELD_RO("Ctype1\0"     "Cache 1 type field",                                          0, 3, 0),
    DBGFREGSUBFIELD_RO("Ctype2\0"     "Cache 2 type field",                                          3, 3, 0),
    DBGFREGSUBFIELD_RO("Ctype3\0"     "Cache 3 type field",                                          6, 3, 0),
    DBGFREGSUBFIELD_RO("Ctype4\0"     "Cache 4 type field",                                          9, 3, 0),
    DBGFREGSUBFIELD_RO("Ctype5\0"     "Cache 5 type field",                                         12, 3, 0),
    DBGFREGSUBFIELD_RO("Ctype6\0"     "Cache 6 type field",                                         15, 3, 0),
    DBGFREGSUBFIELD_RO("Ctype7\0"     "Cache 7 type field",                                         18, 3, 0),
    DBGFREGSUBFIELD_RO("LoUIS\0"      "Level of Unification Inner Shareable",                       21, 3, 0),
    DBGFREGSUBFIELD_RO("LoC\0"        "Level of Coherence for the cache hierarchy",                 24, 3, 0),
    DBGFREGSUBFIELD_RO("LoUU\0"       "Level of Unification Uniprocessor",                          27, 3, 0),
    DBGFREGSUBFIELD_RO("ICB\0"        "Inner cache boundary",                                       30, 3, 0),
    DBGFREGSUBFIELD_RO("Ttype1\0"     "Cache 1 - Tag cache type",                                   33, 2, 0),
    DBGFREGSUBFIELD_RO("Ttype2\0"     "Cache 2 - Tag cache type",                                   35, 2, 0),
    DBGFREGSUBFIELD_RO("Ttype3\0"     "Cache 3 - Tag cache type",                                   37, 2, 0),
    DBGFREGSUBFIELD_RO("Ttype4\0"     "Cache 4 - Tag cache type",                                   39, 2, 0),
    DBGFREGSUBFIELD_RO("Ttype5\0"     "Cache 5 - Tag cache type",                                   41, 2, 0),
    DBGFREGSUBFIELD_RO("Ttype6\0"     "Cache 6 - Tag cache type",                                   43, 2, 0),
    DBGFREGSUBFIELD_RO("Ttype7\0"     "Cache 7 - Tag cache type",                                   45, 2, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   47, 17, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64PFR0_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64PfR0Fields[] =
{
    DBGFREGSUBFIELD_RO("EL0\0"       "EL0 Exception level handling",                                 0, 4, 0),
    DBGFREGSUBFIELD_RO("EL1\0"       "EL1 Exception level handling",                                 4, 4, 0),
    DBGFREGSUBFIELD_RO("EL2\0"       "EL2 Exception level handling",                                 8, 4, 0),
    DBGFREGSUBFIELD_RO("EL3\0"       "EL3 Exception level handling",                                12, 4, 0),
    DBGFREGSUBFIELD_RO("FP\0"        "Floating-point",                                              16, 4, 0),
    DBGFREGSUBFIELD_RO("AdvSIMD\0"   "Advanced SIMD",                                               20, 4, 0),
    DBGFREGSUBFIELD_RO("GIC\0"       "System register GIC CPU interface",                           24, 4, 0),
    DBGFREGSUBFIELD_RO("RAS\0"       "RAS Extension version",                                       28, 4, 0),
    DBGFREGSUBFIELD_RO("SVE\0"       "Scalable Vector Extension",                                   32, 4, 0),
    DBGFREGSUBFIELD_RO("SEL2\0"      "Secure EL2",                                                  36, 4, 0),
    DBGFREGSUBFIELD_RO("MPAM\0"      "MPAM Extension major version number",                         40, 4, 0),
    DBGFREGSUBFIELD_RO("AMU\0"       "Activity Monitors Extension support",                         44, 4, 0),
    DBGFREGSUBFIELD_RO("DIT\0"       "Data Independent Timing",                                     48, 4, 0),
    DBGFREGSUBFIELD_RO("RME\0"       "Realm Management Extension",                                  52, 4, 0),
    DBGFREGSUBFIELD_RO("CSV2\0"      "Speculative use of out of branch targets",                    56, 4, 0),
    DBGFREGSUBFIELD_RO("CSV3\0"      "Speculative use of faulting data",                            60, 4, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64PFR1_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64PfR1Fields[] =
{
    DBGFREGSUBFIELD_RO("BT\0"        "Branch Target Identification mechanism",                       0, 4, 0),
    DBGFREGSUBFIELD_RO("SSBS\0"      "Speculative Store Bypassing controls",                         4, 4, 0),
    DBGFREGSUBFIELD_RO("MTE\0"       "Memory Tagging Extension support",                             8, 4, 0),
    DBGFREGSUBFIELD_RO("RAS_frac\0"  "RAS Extension fractional field",                              12, 4, 0),
    DBGFREGSUBFIELD_RO("MPAM_frac\0" "MPAM Extension minor version",                                16, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    20, 4, 0),
    DBGFREGSUBFIELD_RO("SME\0"       "Scalable Matrix Extension",                                   24, 4, 0),
    DBGFREGSUBFIELD_RO("RNDR_trap\0" "Random Number trap to EL3",                                   28, 4, 0),
    DBGFREGSUBFIELD_RO("CSV2_frac\0" "CSV2 fractional version field",                               32, 4, 0),
    DBGFREGSUBFIELD_RO("NMI\0"       "Non-maskable Interrupt support",                              36, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    40, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    44, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    48, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    52, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    56, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    60, 4, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64ISAR0_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64IsaR0Fields[] =
{
    DBGFREGSUBFIELD_RO("AES\0"       "AES instruction support in AArch64",                           4, 4, 0),
    DBGFREGSUBFIELD_RO("SHA1\0"      "SHA1 instruction support in AArch64",                          8, 4, 0),
    DBGFREGSUBFIELD_RO("SHA2\0"      "SHA256/512 instruction support in AArch64",                   12, 4, 0),
    DBGFREGSUBFIELD_RO("CRC32\0"     "CRC32 instruction support in AArch64",                        16, 4, 0),
    DBGFREGSUBFIELD_RO("ATOMIC\0"    "Atomic instruction support in AArch64",                       20, 4, 0),
    DBGFREGSUBFIELD_RO("TME\0"       "TME instruction support in AArch64",                          24, 4, 0),
    DBGFREGSUBFIELD_RO("RDM\0"       "SQRDMLAH/SQRDMLSH instruction support in AArch64",            28, 4, 0),
    DBGFREGSUBFIELD_RO("SHA3\0"      "SHA3 instruction support in AArch64",                         32, 4, 0),
    DBGFREGSUBFIELD_RO("SM3\0"       "SM3 instruction support in AArch64",                          36, 4, 0),
    DBGFREGSUBFIELD_RO("SM4\0"       "SM4 instruction support in AArch64",                          40, 4, 0),
    DBGFREGSUBFIELD_RO("DP\0"        "Dot Product instruction support in AArch64",                  44, 4, 0),
    DBGFREGSUBFIELD_RO("FHM\0"       "FMLAL/FMLSL instruction support in AArch64",                  48, 4, 0),
    DBGFREGSUBFIELD_RO("TS\0"        "Flag manipulation instruction support in AArch64",            52, 4, 0),
    DBGFREGSUBFIELD_RO("TLB\0"       "TLB maintenance instruction support in AArch64",              56, 4, 0),
    DBGFREGSUBFIELD_RO("RNDR\0"      "Random number instruction support in AArch64",                60, 4, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64ISAR1_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64IsaR1Fields[] =
{
    DBGFREGSUBFIELD_RO("DPB\0"       "Data Persistance writeback support in AArch64",                0, 4, 0),
    DBGFREGSUBFIELD_RO("APA\0"       "QARMA5 PAuth support in AArch64",                              4, 4, 0),
    DBGFREGSUBFIELD_RO("API\0"       "Impl defined PAuth support in AArch64",                        8, 4, 0),
    DBGFREGSUBFIELD_RO("JSCVT\0"     "FJCVTZS instruction support in AArch64",                      12, 4, 0),
    DBGFREGSUBFIELD_RO("FCMA\0"      "FCMLA/FCADD instruction support in AArch64",                  16, 4, 0),
    DBGFREGSUBFIELD_RO("LRCPC\0"     "RCpc instruction support in AArch64",                         20, 4, 0),
    DBGFREGSUBFIELD_RO("GPA\0"       "QARMA5 code authentication support in AArch64",               24, 4, 0),
    DBGFREGSUBFIELD_RO("GPI\0"       "Impl defined code authentication support in AArch64",         28, 4, 0),
    DBGFREGSUBFIELD_RO("FRINTTS\0"   "FRINT{32,64}{Z,X} instruction support in AArch64",            32, 4, 0),
    DBGFREGSUBFIELD_RO("SB\0"        "SB instruction support in AArch64",                           36, 4, 0),
    DBGFREGSUBFIELD_RO("SPECRES\0"   "Prediction invalidation support in AArch64",                  40, 4, 0),
    DBGFREGSUBFIELD_RO("BF16\0"      "BFloat16 support in AArch64",                                 44, 4, 0),
    DBGFREGSUBFIELD_RO("DGH\0"       "Data Gathering Hint support in AArch64",                      48, 4, 0),
    DBGFREGSUBFIELD_RO("I8MM\0"      "Int8 matrix mul instruction support in AArch64",              52, 4, 0),
    DBGFREGSUBFIELD_RO("XS\0"        "XS attribute support in AArch64",                             56, 4, 0),
    DBGFREGSUBFIELD_RO("LS64\0"      "LD64B and ST64B* instruction support in AArch64",             60, 4, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64ISAR2_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64IsaR2Fields[] =
{
    DBGFREGSUBFIELD_RO("WFxT\0"      "WFET/WFIT intruction support in AArch64",                      0, 4, 0),
    DBGFREGSUBFIELD_RO("RPRES\0"     "Reciprocal 12 bit mantissa support in AArch64",                4, 4, 0),
    DBGFREGSUBFIELD_RO("GPA3\0"      "QARMA3 code authentication support in AArch64",                8, 4, 0),
    DBGFREGSUBFIELD_RO("APA3\0"      "QARMA3 PAuth support in AArch64",                             12, 4, 0),
    DBGFREGSUBFIELD_RO("MOPS\0"      "Memory Copy and Set instruction support in AArch64",          16, 4, 0),
    DBGFREGSUBFIELD_RO("BC\0"        "BC instruction support in AArch64",                           20, 4, 0),
    DBGFREGSUBFIELD_RO("PAC_frac\0"  "ConstPACField() returns TRUE",                                24, 4, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64MMFR0_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64MmfR0Fields[] =
{
    DBGFREGSUBFIELD_RO("PARange\0"   "Physical address width",                                       0, 4, 0),
    DBGFREGSUBFIELD_RO("ASIDBits\0"  "Number of ASID bits",                                          4, 4, 0),
    DBGFREGSUBFIELD_RO("BigEnd\0"    "Mixed-endian configuration support",                           8, 4, 0),
    DBGFREGSUBFIELD_RO("SNSMem\0"    "Secure and Non-secure memory distinction",                    12, 4, 0),
    DBGFREGSUBFIELD_RO("BigEndEL0\0" "Mixed-endian support in EL0 only",                            16, 4, 0),
    DBGFREGSUBFIELD_RO("TGran16\0"   "16KiB memory granule size",                                   20, 4, 0),
    DBGFREGSUBFIELD_RO("TGran64\0"   "64KiB memory granule size",                                   24, 4, 0),
    DBGFREGSUBFIELD_RO("TGran4\0"    "4KiB memory granule size",                                    28, 4, 0),
    DBGFREGSUBFIELD_RO("TGran16_2\0" "16KiB memory granule size at stage 2",                        32, 4, 0),
    DBGFREGSUBFIELD_RO("TGran64_2\0" "64KiB memory granule size at stage 2",                        36, 4, 0),
    DBGFREGSUBFIELD_RO("TGran4_2\0"  "4KiB memory granule size at stage 2",                         40, 4, 0),
    DBGFREGSUBFIELD_RO("ExS\0"       "Disabling context synchronizing exception",                   44, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    48, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    52, 4, 0),
    DBGFREGSUBFIELD_RO("FGT\0"       "Fine-grained trap controls support",                          56, 4, 0),
    DBGFREGSUBFIELD_RO("ECV\0"       "Enhanced Counter Virtualization support",                     60, 4, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64MMFR1_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64MmfR1Fields[] =
{
    DBGFREGSUBFIELD_RO("HAFDBS\0"    "Hardware updates to Access/Dirty state",                       0, 4, 0),
    DBGFREGSUBFIELD_RO("VMIDBit\0"   "Number of VMID bits",                                          4, 4, 0),
    DBGFREGSUBFIELD_RO("VH\0"        "Virtualization Host Extensions",                               8, 4, 0),
    DBGFREGSUBFIELD_RO("HPDS\0"      "Hierarchical Permission Disables",                            12, 4, 0),
    DBGFREGSUBFIELD_RO("LO\0"        "LORegions support",                                           16, 4, 0),
    DBGFREGSUBFIELD_RO("PAN\0"       "Privileged Access Never",                                     20, 4, 0),
    DBGFREGSUBFIELD_RO("SpecSEI\0"   "SError interrupt exception for speculative reads",            24, 4, 0),
    DBGFREGSUBFIELD_RO("XNX\0"       "Execute-never control support",                               28, 4, 0),
    DBGFREGSUBFIELD_RO("TWED\0"      "Configurable delayed WFE trapping",                           32, 4, 0),
    DBGFREGSUBFIELD_RO("ETS\0"       "Enhanced Translation Synchronization support",                36, 4, 0),
    DBGFREGSUBFIELD_RO("HCX\0"       "HCRX_EL2 support",                                            40, 4, 0),
    DBGFREGSUBFIELD_RO("AFP\0"       "FPCR.{AH,FIZ,NEP} support",                                   44, 4, 0),
    DBGFREGSUBFIELD_RO("nTLBPA\0"    "Caching of translation table walks",                          48, 4, 0),
    DBGFREGSUBFIELD_RO("TIDCP1\0"    "FEAT_TIDCP1 support",                                         52, 4, 0),
    DBGFREGSUBFIELD_RO("CMOW\0"      "Cache maintenance instruction permission",                    56, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    60, 4, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64MMFR2_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64MmfR2Fields[] =
{
    DBGFREGSUBFIELD_RO("CnP\0"       "Common not Private translation support",                       0, 4, 0),
    DBGFREGSUBFIELD_RO("UAO\0"       "User Access Override",                                         4, 4, 0),
    DBGFREGSUBFIELD_RO("LSM\0"       "LSMAOE/nTLSMD bit support",                                    8, 4, 0),
    DBGFREGSUBFIELD_RO("IESB\0"      "IESB bit support in SCTLR_ELx",                               12, 4, 0),
    DBGFREGSUBFIELD_RO("VARange\0"   "Large virtual address space support",                         16, 4, 0),
    DBGFREGSUBFIELD_RO("CCIDX\0"     "64-bit CCSIDR_EL1 format",                                    20, 4, 0),
    DBGFREGSUBFIELD_RO("NV\0"        "Nested Virtualization support",                               24, 4, 0),
    DBGFREGSUBFIELD_RO("ST\0"        "Small translation table support",                             28, 4, 0),
    DBGFREGSUBFIELD_RO("AT\0"        "Unaligned single-copy atomicity support",                     32, 4, 0),
    DBGFREGSUBFIELD_RO("IDS\0"       "FEAT_IDST support",                                           36, 4, 0),
    DBGFREGSUBFIELD_RO("FWB\0"       "HCR_EL2.FWB support",                                         40, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    44, 4, 0),
    DBGFREGSUBFIELD_RO("TTL\0"       "TTL field support in address operations",                     48, 4, 0),
    DBGFREGSUBFIELD_RO("BBM\0"       "FEAT_BBM support",                                            52, 4, 0),
    DBGFREGSUBFIELD_RO("EVT\0"       "Enhanced Virtualization Traps support",                       56, 4, 0),
    DBGFREGSUBFIELD_RO("E0PD\0"      "E0PD mechanism support",                                      60, 4, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64DFR0_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64DfR0Fields[] =
{
    DBGFREGSUBFIELD_RO("DebugVer\0"  "Debug architecture version",                                   0, 4, 0),
    DBGFREGSUBFIELD_RO("TraceVer\0"  "Trace support",                                                4, 4, 0),
    DBGFREGSUBFIELD_RO("PMUVer\0"    "Performance Monitors Extension version",                       8, 4, 0),
    DBGFREGSUBFIELD_RO("BRPs\0"      "Number of breakpoints minus 1",                               12, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    16, 4, 0),
    DBGFREGSUBFIELD_RO("WRPs\0"      "Number of watchpoints minus 1",                               20, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    24, 4, 0),
    DBGFREGSUBFIELD_RO("CTX_CMPs\0"  "Number of context-aware breakpoints minus 1",                 28, 4, 0),
    DBGFREGSUBFIELD_RO("PMSVer\0"    "Statistical Profiling Extension version",                     32, 4, 0),
    DBGFREGSUBFIELD_RO("DoubleLock\0"  "OS Double Lock support",                                    36, 4, 0),
    DBGFREGSUBFIELD_RO("TraceFilt\0" "Armv8.4 Self-hosted Trace Extension version",                 40, 4, 0),
    DBGFREGSUBFIELD_RO("TraceBuffer\0" "Trace Buffer Extension",                                    44, 4, 0),
    DBGFREGSUBFIELD_RO("MTPMU\0"     "Multi-threaded PMU extension",                                48, 4, 0),
    DBGFREGSUBFIELD_RO("BRBE\0"      "Branch Record Buffer Extension",                              52, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"      "Reserved",                                                    56, 4, 0),
    DBGFREGSUBFIELD_RO("HPMN0\0"     "Zero PMU event counters for guest",                           60, 4, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64DFR1_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64DfR1Fields[] =
{
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                    0, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                    4, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                    8, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   12, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   16, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   20, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   24, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   28, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   32, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   36, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   40, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   44, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   48, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   52, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   56, 4, 0),
    DBGFREGSUBFIELD_RO("Res0\0"       "Reserved",                                                   60, 4, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64AFR0_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64AfR0Fields[] =
{
    DBGFREGSUBFIELD_RO("ImpDef\0"     "Implementation defined",                                     0, 32, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** ID_AA64AFR1_EL1 field descriptions.   */
static DBGFREGSUBFIELD const g_aIdAa64AfR1Fields[] =
{
    DBGFREGSUBFIELD_RO("ImpDef\0"     "Implementation defined",                                     0, 32, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


static void cpumR3CpuIdInfoMnemonicListU64(PCDBGFINFOHLP pHlp, uint64_t uVal, PCDBGFREGSUBFIELD pDesc,
                                           const char *pszLeadIn, uint32_t cchWidth)
{
    if (pszLeadIn)
        pHlp->pfnPrintf(pHlp, "%*s", cchWidth, pszLeadIn);

    for (uint32_t iBit = 0; iBit < 64; iBit++)
        if (RT_BIT_64(iBit) & uVal)
        {
            while (   pDesc->pszName != NULL
                   && iBit >= (uint32_t)pDesc->iFirstBit + pDesc->cBits)
                pDesc++;
            if (   pDesc->pszName != NULL
                && iBit - (uint32_t)pDesc->iFirstBit < (uint32_t)pDesc->cBits)
            {
                if (pDesc->cBits == 1)
                    pHlp->pfnPrintf(pHlp, " %s", pDesc->pszName);
                else
                {
                    uint64_t uFieldValue = uVal >> pDesc->iFirstBit;
                    if (pDesc->cBits < 64)
                        uFieldValue &= RT_BIT_64(pDesc->cBits) - UINT64_C(1);
                    pHlp->pfnPrintf(pHlp, pDesc->cBits < 4 ? " %s=%llu" : " %s=%#llx", pDesc->pszName, uFieldValue);
                    iBit = pDesc->iFirstBit + pDesc->cBits - 1;
                }
            }
            else
                pHlp->pfnPrintf(pHlp, " %u", iBit);
        }
    if (pszLeadIn)
        pHlp->pfnPrintf(pHlp, "\n");
}


static void cpumR3CpuIdInfoVerboseCompareListU64(PCDBGFINFOHLP pHlp, uint64_t uVal1, uint64_t uVal2, PCDBGFREGSUBFIELD pDesc,
                                                 uint32_t cchWidth)
{
    uint32_t uCombined = uVal1 | uVal2;
    for (uint32_t iBit = 0; iBit < 64; iBit++)
        if (   (RT_BIT_64(iBit) & uCombined)
            || (iBit == pDesc->iFirstBit && pDesc->pszName) )
        {
            while (   pDesc->pszName != NULL
                   && iBit >= (uint32_t)pDesc->iFirstBit + pDesc->cBits)
                pDesc++;

            if (   pDesc->pszName != NULL
                && iBit - (uint32_t)pDesc->iFirstBit < (uint32_t)pDesc->cBits)
            {
                size_t      cchMnemonic  = strlen(pDesc->pszName);
                const char *pszDesc      = pDesc->pszName + cchMnemonic + 1;
                size_t      cchDesc      = strlen(pszDesc);
                uint32_t    uFieldValue1 = uVal1 >> pDesc->iFirstBit;
                uint32_t    uFieldValue2 = uVal2 >> pDesc->iFirstBit;
                if (pDesc->cBits < 64)
                {
                    uFieldValue1 &= RT_BIT_64(pDesc->cBits) - UINT64_C(1);
                    uFieldValue2 &= RT_BIT_64(pDesc->cBits) - UINT64_C(1);
                }

                pHlp->pfnPrintf(pHlp,  pDesc->cBits < 5 ? "  %s - %s%*s= %u (%u)\n" : "  %s - %s%*s= %#x (%#x)\n",
                                pDesc->pszName, pszDesc,
                                cchMnemonic + 3 + cchDesc < cchWidth ? cchWidth - (cchMnemonic + 3 + cchDesc) : 1, "",
                                uFieldValue1, uFieldValue2);

                iBit = pDesc->iFirstBit + pDesc->cBits - 1U;
                pDesc++;
            }
            else
                pHlp->pfnPrintf(pHlp, "  %2u - Reserved%*s= %u (%u)\n", iBit, 13 < cchWidth ? cchWidth - 13 : 1, "",
                                RT_BOOL(uVal1 & RT_BIT_64(iBit)), RT_BOOL(uVal2 & RT_BIT_64(iBit)));
        }
}


/**
 * Produces a detailed summary of standard leaf 0x00000007.
 *
 * @param   pHlp            The info helper functions.
 * @param   pszIdReg        The ID register name.
 * @param   u64IdRegHost    The host value of the ID register.
 * @param   u64IdRegGuest   The guest value of the ID register.
 * @param   pDesc           The field descriptor.
 * @param   fVerbose        Whether to be very verbose or not.
 */
static void cpumR3CpuIdInfoIdRegDetails(PCDBGFINFOHLP pHlp, const char *pszIdReg, uint64_t u64IdRegHost, uint64_t u64IdRegGuest,
                                        PCDBGFREGSUBFIELD pDesc, bool fVerbose)
{
    pHlp->pfnPrintf(pHlp, "ID register %s: Host %#RX64 Guest %#RX64\n", pszIdReg, u64IdRegHost, u64IdRegGuest);
    if (fVerbose)
    {
        pHlp->pfnPrintf(pHlp, "  Mnemonic - Description                                  = guest (host)\n");
        cpumR3CpuIdInfoVerboseCompareListU64(pHlp, u64IdRegGuest, u64IdRegHost, pDesc, 56);
    }
    else
        cpumR3CpuIdInfoMnemonicListU64(pHlp, u64IdRegGuest, pDesc, "Features:", 36);
}


/**
 * Display the guest CpuId leaves.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     "terse", "default" or "verbose".
 */
DECLCALLBACK(void) cpumR3CpuIdInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /*
     * Parse the argument.
     */
    unsigned iVerbosity = 1;
    if (pszArgs)
    {
        pszArgs = RTStrStripL(pszArgs);
        if (!strcmp(pszArgs, "terse"))
            iVerbosity--;
        else if (!strcmp(pszArgs, "verbose"))
            iVerbosity++;
    }

#ifdef RT_ARCH_ARM64
    CPUMARMV8IDREGS const * const pHostIdRegs     = &pVM->cpum.s.HostIdRegs;
#else
    CPUMARMV8IDREGS const         DummyHostIdRegs = { };
    CPUMARMV8IDREGS const * const pHostIdRegs     = &DummyHostIdRegs;
#endif

    /** @todo MIDR_EL1. */

    cpumR3CpuIdInfoIdRegDetails(pHlp, "CLIDR_EL1",
                                pHostIdRegs->u64RegClidrEl1,
                                pVM->cpum.s.GuestIdRegs.u64RegClidrEl1,
                                g_aClidrEl1Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64PFR0_EL1",
                                pHostIdRegs->u64RegIdAa64Pfr0El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Pfr0El1,
                                g_aIdAa64PfR0Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64PFR1_EL1",
                                pHostIdRegs->u64RegIdAa64Pfr1El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Pfr1El1,
                                g_aIdAa64PfR1Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64ISAR0_EL1",
                                pHostIdRegs->u64RegIdAa64Isar0El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Isar0El1,
                                g_aIdAa64IsaR0Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64ISAR1_EL1",
                                pHostIdRegs->u64RegIdAa64Isar1El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Isar1El1,
                                g_aIdAa64IsaR1Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64ISAR2_EL1",
                                pHostIdRegs->u64RegIdAa64Isar2El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Isar2El1,
                                g_aIdAa64IsaR2Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64MMFR0_EL1",
                                pHostIdRegs->u64RegIdAa64Mmfr0El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Mmfr0El1,
                                g_aIdAa64MmfR0Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64MMFR1_EL1",
                                pHostIdRegs->u64RegIdAa64Mmfr1El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Mmfr1El1,
                                g_aIdAa64MmfR1Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64MMFR2_EL1",
                                pHostIdRegs->u64RegIdAa64Mmfr2El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Mmfr2El1,
                                g_aIdAa64MmfR2Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64DFR0_EL1",
                                pHostIdRegs->u64RegIdAa64Dfr0El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Dfr0El1,
                                g_aIdAa64DfR0Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64DFR1_EL1",
                                pHostIdRegs->u64RegIdAa64Dfr1El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Dfr1El1,
                                g_aIdAa64DfR1Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64AFR0_EL1",
                                pHostIdRegs->u64RegIdAa64Afr0El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Afr0El1,
                                g_aIdAa64AfR0Fields, iVerbosity > 1);

    cpumR3CpuIdInfoIdRegDetails(pHlp, "ID_AA64AFR1_EL1",
                                pHostIdRegs->u64RegIdAa64Afr1El1,
                                pVM->cpum.s.GuestIdRegs.u64RegIdAa64Afr1El1,
                                g_aIdAa64AfR1Fields, iVerbosity > 1);
}


/**
 * Display the guest CPU features.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     "default" or "verbose".
 */
DECLCALLBACK(void) cpumR3CpuFeatInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /*
     * Parse the argument.
     */
    bool fVerbose = false;
    if (pszArgs)
    {
        pszArgs = RTStrStripL(pszArgs);
        if (!strcmp(pszArgs, "verbose"))
            fVerbose = true;
    }

#if 1
    /*
     * Call generated code to do the actual printing.
     */
#ifdef RT_ARCH_ARM64
    if (fVerbose)
        CPUMR3CpuIdPrintArmV8Features(pHlp, &pVM->cpum.s.GuestFeatures, "guest", &pVM->cpum.s.HostFeatures.s, "host");
    else
#endif
        CPUMR3CpuIdPrintArmV8Features(pHlp, &pVM->cpum.s.GuestFeatures, "guest", NULL, NULL);
#else
#ifdef RT_ARCH_ARM64
    if (fVerbose)
        pHlp->pfnPrintf(pHlp, "  Features                                  = guest (host)\n");
    else
#endif
        pHlp->pfnPrintf(pHlp, "  Features                                  = guest\n");


#ifdef RT_ARCH_ARM64
# define LOG_CPU_FEATURE(a_FeatNm, a_Flag) \
        do { \
            if (fVerbose) \
                pHlp->pfnPrintf(pHlp, "  %*s = %u (%u)\n", 41, #a_FeatNm, pVM->cpum.s.GuestFeatures.a_Flag, pVM->cpum.s.HostFeatures.s.a_Flag); \
            else \
                pHlp->pfnPrintf(pHlp, "  %*s = %u\n", 41, #a_FeatNm, pVM->cpum.s.GuestFeatures.a_Flag); \
        } while (0)
#else
# define LOG_CPU_FEATURE(a_FeatNm, a_Flag) \
        do { \
            pHlp->pfnPrintf(pHlp, "  %*s = %u\n", 41, #a_FeatNm, pVM->cpum.s.GuestFeatures.a_Flag); \
        } while (0)
    RT_NOREF_PV(fVerbose);
#endif

    /* Not really features. */
    LOG_CPU_FEATURE(FEAT_TGRAN4K,   fTGran4K);
    LOG_CPU_FEATURE(FEAT_TGRAN16K,  fTGran16K);
    LOG_CPU_FEATURE(FEAT_TGRAN64K,  fTGran64K);

    /* Offical ARM FEAT_* definitions start here. */
    LOG_CPU_FEATURE(FEAT_AdvSIMD,       fAdvSimd);
    LOG_CPU_FEATURE(FEAT_AES,           fAes);
    LOG_CPU_FEATURE(FEAT_PMULL,         fPmull);
#if 0
    LOG_CPU_FEATURE(FEAT_CP15SDISABLE2, fCp15SDisable2); /* impossible to detect & of aarch32 relevance only */
#endif
    LOG_CPU_FEATURE(FEAT_CSV2,          fCsv2);
    LOG_CPU_FEATURE(FEAT_CSV2_1p1,      fCsv21p1);
    LOG_CPU_FEATURE(FEAT_CSV2_1p2,      fCsv21p2);
    LOG_CPU_FEATURE(FEAT_CSV3,          fCsv3);
    LOG_CPU_FEATURE(FEAT_DGH,           fDgh);
    LOG_CPU_FEATURE(FEAT_DOUBLELOCK,    fDoubleLock);
    LOG_CPU_FEATURE(FEAT_ETS2,          fEts2);
    LOG_CPU_FEATURE(FEAT_FP,            fFp);
    LOG_CPU_FEATURE(FEAT_IVIPT,         fIvipt);
    LOG_CPU_FEATURE(FEAT_PCSRv8,        fPcsrV8);
    LOG_CPU_FEATURE(FEAT_SPECRES,       fSpecres);
    LOG_CPU_FEATURE(FEAT_RAS,           fRas);
    LOG_CPU_FEATURE(FEAT_SB,            fSb);
    LOG_CPU_FEATURE(FEAT_SHA1,          fSha1);
    LOG_CPU_FEATURE(FEAT_SHA256,        fSha256);
    LOG_CPU_FEATURE(FEAT_SSBS,          fSsbs);
    LOG_CPU_FEATURE(FEAT_SSBS2,         fSsbs2);
    LOG_CPU_FEATURE(FEAT_CRC32,         fCrc32);
    LOG_CPU_FEATURE(FEAT_nTLBPA,        fNTlbpa);
    LOG_CPU_FEATURE(FEAT_Debugv8p1,     fDebugV8p1);
    LOG_CPU_FEATURE(FEAT_HPDS,          fHpds);
    LOG_CPU_FEATURE(FEAT_LOR,           fLor);
    LOG_CPU_FEATURE(FEAT_LSE,           fLse);
    LOG_CPU_FEATURE(FEAT_PAN,           fPan);
    LOG_CPU_FEATURE(FEAT_PMUv3p1,       fPmuV3p1);
    LOG_CPU_FEATURE(FEAT_RDM,           fRdm);
    LOG_CPU_FEATURE(FEAT_HAFDBS,        fHafdbs);
    LOG_CPU_FEATURE(FEAT_VHE,           fVhe);
    LOG_CPU_FEATURE(FEAT_VMID16,        fVmid16);
    LOG_CPU_FEATURE(FEAT_AA32BF16,      fAa32Bf16);
    LOG_CPU_FEATURE(FEAT_AA32HPD,       fAa32Hpd);
    LOG_CPU_FEATURE(FEAT_AA32I8MM,      fAa32I8mm);
    LOG_CPU_FEATURE(FEAT_PAN2,          fPan2);
    LOG_CPU_FEATURE(FEAT_BF16,          fBf16);
    LOG_CPU_FEATURE(FEAT_DPB2,          fDpb2);
    LOG_CPU_FEATURE(FEAT_DPB,           fDpb);
    LOG_CPU_FEATURE(FEAT_Debugv8p2,     fDebugV8p2);
    LOG_CPU_FEATURE(FEAT_DotProd,       fDotProd);
    LOG_CPU_FEATURE(FEAT_EVT,           fEvt);
    LOG_CPU_FEATURE(FEAT_F32MM,         fF32mm);
    LOG_CPU_FEATURE(FEAT_F64MM,         fF64mm);
    LOG_CPU_FEATURE(FEAT_FHM,           fFhm);
    LOG_CPU_FEATURE(FEAT_FP16,          fFp16);
    LOG_CPU_FEATURE(FEAT_I8MM,          fI8mm);
    LOG_CPU_FEATURE(FEAT_IESB,          fIesb);
    LOG_CPU_FEATURE(FEAT_LPA,           fLpa);
    LOG_CPU_FEATURE(FEAT_LSMAOC,        fLsmaoc);
    LOG_CPU_FEATURE(FEAT_LVA,           fLva);
    LOG_CPU_FEATURE(FEAT_MPAM,          fMpam);
    LOG_CPU_FEATURE(FEAT_PCSRv8p2,      fPcsrV8p2);
    LOG_CPU_FEATURE(FEAT_SHA3,          fSha3);
    LOG_CPU_FEATURE(FEAT_SHA512,        fSha512);
    LOG_CPU_FEATURE(FEAT_SM3,           fSm3);
    LOG_CPU_FEATURE(FEAT_SM4,           fSm4);
    LOG_CPU_FEATURE(FEAT_SPE,           fSpe);
    LOG_CPU_FEATURE(FEAT_SVE,           fSve);
    LOG_CPU_FEATURE(FEAT_TTCNP,         fTtcnp);
    LOG_CPU_FEATURE(FEAT_HPDS2,         fHpds2);
    LOG_CPU_FEATURE(FEAT_XNX,           fXnx);
    LOG_CPU_FEATURE(FEAT_UAO,           fUao);
    LOG_CPU_FEATURE(FEAT_VPIPT,         fVpipt);
    LOG_CPU_FEATURE(FEAT_CCIDX,         fCcidx);
    LOG_CPU_FEATURE(FEAT_FCMA,          fFcma);
    LOG_CPU_FEATURE(FEAT_DoPD,          fDopd);
    LOG_CPU_FEATURE(FEAT_EPAC,          fEpac);
    LOG_CPU_FEATURE(FEAT_FPAC,          fFpac);
    LOG_CPU_FEATURE(FEAT_FPACCOMBINE,   fFpacCombine);
    LOG_CPU_FEATURE(FEAT_JSCVT,         fJscvt);
    LOG_CPU_FEATURE(FEAT_LRCPC,         fLrcpc);
    LOG_CPU_FEATURE(FEAT_NV,            fNv);
    LOG_CPU_FEATURE(FEAT_PACQARMA5,     fPacQarma5);
    LOG_CPU_FEATURE(FEAT_PACIMP,        fPacImp);
    LOG_CPU_FEATURE(FEAT_PAuth,         fPAuth);
    LOG_CPU_FEATURE(FEAT_PAuth2,        fPAuth2);
    LOG_CPU_FEATURE(FEAT_SPEv1p1,       fSpeV1p1);
    LOG_CPU_FEATURE(FEAT_AMUv1,         fAmuV1);
    LOG_CPU_FEATURE(FEAT_CNTSC,         fCntsc);
    LOG_CPU_FEATURE(FEAT_Debugv8p4,     fDebugV8p4);
    LOG_CPU_FEATURE(FEAT_DoubleFault,   fDoubleFault);
    LOG_CPU_FEATURE(FEAT_DIT,           fDit);
    LOG_CPU_FEATURE(FEAT_FlagM,         fFlagM);
    LOG_CPU_FEATURE(FEAT_IDST,          fIdst);
    LOG_CPU_FEATURE(FEAT_LRCPC2,        fLrcpc2);
    LOG_CPU_FEATURE(FEAT_LSE2,          fLse2);
    LOG_CPU_FEATURE(FEAT_NV2,           fNv2);
    LOG_CPU_FEATURE(FEAT_PMUv3p4,       fPmuV3p4);
    LOG_CPU_FEATURE(FEAT_RASv1p1,       fRasV1p1);
    LOG_CPU_FEATURE(FEAT_RASSAv1p1,     fRassaV1p1);
    LOG_CPU_FEATURE(FEAT_S2FWB,         fS2Fwb);
    LOG_CPU_FEATURE(FEAT_SEL2,          fSecEl2);
    LOG_CPU_FEATURE(FEAT_TLBIOS,        fTlbios);
    LOG_CPU_FEATURE(FEAT_TLBIRANGE,     fTlbirange);
    LOG_CPU_FEATURE(FEAT_TRF,           fTrf);
    LOG_CPU_FEATURE(FEAT_TTL,           fTtl);
    LOG_CPU_FEATURE(FEAT_BBM,           fBbm);
    LOG_CPU_FEATURE(FEAT_TTST,          fTtst);
    LOG_CPU_FEATURE(FEAT_BTI,           fBti);
    LOG_CPU_FEATURE(FEAT_FlagM2,        fFlagM2);
    LOG_CPU_FEATURE(FEAT_ExS,           fExs);
    LOG_CPU_FEATURE(FEAT_E0PD,          fE0Pd);
    LOG_CPU_FEATURE(FEAT_FRINTTS,       fFrintts);
    LOG_CPU_FEATURE(FEAT_GTG,           fGtg);
    LOG_CPU_FEATURE(FEAT_MTE,           fMte);
    LOG_CPU_FEATURE(FEAT_MTE2,          fMte2);
    LOG_CPU_FEATURE(FEAT_PMUv3p5,       fPmuV3p5);
    LOG_CPU_FEATURE(FEAT_RNG,           fRng);
    LOG_CPU_FEATURE(FEAT_AMUv1p1,       fAmuV1p1);
    LOG_CPU_FEATURE(FEAT_ECV,           fEcv);
    LOG_CPU_FEATURE(FEAT_FGT,           fFgt);
    LOG_CPU_FEATURE(FEAT_MPAMv0p1,      fMpamV0p1);
    LOG_CPU_FEATURE(FEAT_MPAMv1p1,      fMpamV1p1);
    LOG_CPU_FEATURE(FEAT_MTPMU,         fMtPmu);
    LOG_CPU_FEATURE(FEAT_TWED,          fTwed);
    LOG_CPU_FEATURE(FEAT_ETMv4,         fEtmV4);
    LOG_CPU_FEATURE(FEAT_ETMv4p1,       fEtmV4p1);
    LOG_CPU_FEATURE(FEAT_ETMv4p2,       fEtmV4p2);
    LOG_CPU_FEATURE(FEAT_ETMv4p3,       fEtmV4p3);
    LOG_CPU_FEATURE(FEAT_ETMv4p4,       fEtmV4p4);
    LOG_CPU_FEATURE(FEAT_ETMv4p5,       fEtmV4p5);
    LOG_CPU_FEATURE(FEAT_ETMv4p6,       fEtmV4p6);
    LOG_CPU_FEATURE(FEAT_GICv3,         fGicV3);
    LOG_CPU_FEATURE(FEAT_GICv3p1,       fGicV3p1);
    LOG_CPU_FEATURE(FEAT_GICv3_TDIR,    fGicV3Tdir);
    LOG_CPU_FEATURE(FEAT_GICv4,         fGicV4);
    LOG_CPU_FEATURE(FEAT_GICv4p1,       fGicV4p1);
    LOG_CPU_FEATURE(FEAT_PMUv3,         fPmuV3);
    LOG_CPU_FEATURE(FEAT_ETE,           fEte);
    LOG_CPU_FEATURE(FEAT_ETEv1p1,       fEteV1p1);
    LOG_CPU_FEATURE(FEAT_ETEv1p2,       fEteV1p2);
    LOG_CPU_FEATURE(FEAT_SVE2,          fSve2);
    LOG_CPU_FEATURE(FEAT_SVE_AES,       fSveAes);
    LOG_CPU_FEATURE(FEAT_SVE_PMULL128,  fSvePmull128);
    LOG_CPU_FEATURE(FEAT_SVE_BitPerm,   fSveBitPerm);
    LOG_CPU_FEATURE(FEAT_SVE_SHA3,      fSveSha3);
    LOG_CPU_FEATURE(FEAT_SVE_SM4,       fSveSm4);
    LOG_CPU_FEATURE(FEAT_TME,           fTme);
    LOG_CPU_FEATURE(FEAT_TRBE,          fTrbe);
    LOG_CPU_FEATURE(FEAT_SME,           fSme);

    LOG_CPU_FEATURE(FEAT_AFP,           fAfp);
    LOG_CPU_FEATURE(FEAT_HCX,           fHcx);
    LOG_CPU_FEATURE(FEAT_LPA2,          fLpa2);
    LOG_CPU_FEATURE(FEAT_LS64,          fLs64);
    LOG_CPU_FEATURE(FEAT_LS64_V,        fLs64V);
    LOG_CPU_FEATURE(FEAT_LS64_ACCDATA,  fLs64Accdata);
    LOG_CPU_FEATURE(FEAT_MTE3,          fMte3);
    LOG_CPU_FEATURE(FEAT_PAN3,          fPan3);
    LOG_CPU_FEATURE(FEAT_PMUv3p7,       fPmuV3p7);
    LOG_CPU_FEATURE(FEAT_RPRES,         fRpres);
    LOG_CPU_FEATURE(FEAT_RME,           fRme);
    LOG_CPU_FEATURE(FEAT_SME_FA64,      fSmeFA64);
    LOG_CPU_FEATURE(FEAT_SME_F64F64,    fSmeF64F64);
    LOG_CPU_FEATURE(FEAT_SME_I16I64,    fSmeI16I64);
    LOG_CPU_FEATURE(FEAT_SPEv1p2,       fSpeV1p2);
    LOG_CPU_FEATURE(FEAT_EBF16,         fEbf16);
    LOG_CPU_FEATURE(FEAT_WFxT,          fWfxt);
    LOG_CPU_FEATURE(FEAT_XS,            fXs);
    LOG_CPU_FEATURE(FEAT_BRBE,          fBrbe);

    LOG_CPU_FEATURE(FEAT_CMOW,          fCmow);
    LOG_CPU_FEATURE(FEAT_CONSTPACFIELD, fConstPacField);
    LOG_CPU_FEATURE(FEAT_Debugv8p8,     fDebugV8p8);
    LOG_CPU_FEATURE(FEAT_HBC,           fHbc);
    LOG_CPU_FEATURE(FEAT_HPMN0,         fHpmn0);
    LOG_CPU_FEATURE(FEAT_NMI,           fNmi);
    LOG_CPU_FEATURE(FEAT_GICv3_NMI,     fGicV3Nmi);
    LOG_CPU_FEATURE(FEAT_MOPS,          fMops);
    LOG_CPU_FEATURE(FEAT_PACQARMA3,     fPacQarma3);
    LOG_CPU_FEATURE(FEAT_PMUv3_TH,      fPmuV3Th);
    LOG_CPU_FEATURE(FEAT_PMUv3p8,       fPmuV3p8);
#if 0
    LOG_CPU_FEATURE(FEAT_PMUv3_EXT64,   fPmuV3Ext64);   /* difficult to detect */
    LOG_CPU_FEATURE(FEAT_PMUv3_EXT32,   fPmuV3Ext32);   /* difficult to detect */
    LOG_CPU_FEATURE(FEAT_PMUv3_EXT,     fPmuV3Ext);     /* difficult to detect */
#endif
    LOG_CPU_FEATURE(FEAT_RNG_TRAP,      fRngTrap);
    LOG_CPU_FEATURE(FEAT_SPEv1p3,       fSpeV1p3);
    LOG_CPU_FEATURE(FEAT_TIDCP1,        fTidcp1);
    LOG_CPU_FEATURE(FEAT_BRBEv1p1,      fBrbeV1p1);

    LOG_CPU_FEATURE(FEAT_ABLE,          fAble);
    LOG_CPU_FEATURE(FEAT_ADERR,         fAderr);
    LOG_CPU_FEATURE(FEAT_AIE,           fAie);
    LOG_CPU_FEATURE(FEAT_ANERR,         fAnerr);
    LOG_CPU_FEATURE(FEAT_BWE,           fBwe);
    LOG_CPU_FEATURE(FEAT_CLRBHB,        fClrBhb);
    LOG_CPU_FEATURE(FEAT_CHK,           fChk);
    LOG_CPU_FEATURE(FEAT_CSSC,          fCssc);
    LOG_CPU_FEATURE(FEAT_CSV2_3,        fCsv2v3);
    LOG_CPU_FEATURE(FEAT_D128,          fD128);
    LOG_CPU_FEATURE(FEAT_Debugv8p9,     fDebugV8p9);
    LOG_CPU_FEATURE(FEAT_DoubleFault2,  fDoubleFault2);
    LOG_CPU_FEATURE(FEAT_EBEP,          fEbep);
    LOG_CPU_FEATURE(FEAT_ECBHB,         fEcBhb);
    LOG_CPU_FEATURE(FEAT_EDHSR,         fEdhsr);
    LOG_CPU_FEATURE(FEAT_ETEv1p3,       fEteV1p3);
    LOG_CPU_FEATURE(FEAT_FGT2,          fFgt2);
    LOG_CPU_FEATURE(FEAT_GCS,           fGcs);
    LOG_CPU_FEATURE(FEAT_HAFT,          fHaft);
    LOG_CPU_FEATURE(FEAT_ITE,           fIte);
    LOG_CPU_FEATURE(FEAT_LRCPC3,        fLrcpc3);
    LOG_CPU_FEATURE(FEAT_LSE128,        fLse128);
    LOG_CPU_FEATURE(FEAT_LVA3,          fLva3);
    LOG_CPU_FEATURE(FEAT_MEC,           fMec);
    LOG_CPU_FEATURE(FEAT_MTE4,          fMte4);
    LOG_CPU_FEATURE(FEAT_MTE_CANONICAL_TAGS,    fMteCanonicalTags);
    LOG_CPU_FEATURE(FEAT_MTE_TAGGED_FAR,        fMteTaggedFar);
    LOG_CPU_FEATURE(FEAT_MTE_STORE_ONLY,        fMteStoreOnly);
    LOG_CPU_FEATURE(FEAT_MTE_NO_ADDRESS_TAGS,   fMteNoAddressTags);
    LOG_CPU_FEATURE(FEAT_MTE_ASYM_FAULT,        fMteAsymFault);
    LOG_CPU_FEATURE(FEAT_MTE_ASYNC,             fMteAsync);
#if 0
    LOG_CPU_FEATURE(FEAT_MTE_PERM_S1,           fMtePermS1); /* removed? */
#endif
    LOG_CPU_FEATURE(FEAT_PCSRv8p9,              fPcsrV8p9);
    LOG_CPU_FEATURE(FEAT_S1PIE,                 fS1Pie);
    LOG_CPU_FEATURE(FEAT_S2PIE,                 fS2Pie);
    LOG_CPU_FEATURE(FEAT_S1POE,                 fS1Poe);
    LOG_CPU_FEATURE(FEAT_S2POE,                 fS2Poe);
    LOG_CPU_FEATURE(FEAT_PFAR,                  fPfar);
    LOG_CPU_FEATURE(FEAT_PMUv3p9,               fPmuV3p9);
    LOG_CPU_FEATURE(FEAT_PMUv3_EDGE,            fPmuV3Edge);
    LOG_CPU_FEATURE(FEAT_PMUv3_ICNTR,           fPmuV3Icntr);
    LOG_CPU_FEATURE(FEAT_PMUv3_SS,              fPmuV3Ss);
    LOG_CPU_FEATURE(FEAT_PRFMSLC,               fPrfmSlc);
    LOG_CPU_FEATURE(FEAT_RASv2,                 fRasV2);
    LOG_CPU_FEATURE(FEAT_RASSAv2,               fRasSaV2);
    LOG_CPU_FEATURE(FEAT_RPRFM,                 fRprfm);
    LOG_CPU_FEATURE(FEAT_SCTLR2,                fSctlr2);
    LOG_CPU_FEATURE(FEAT_SEBEP,                 fSebep);
    LOG_CPU_FEATURE(FEAT_SME_F16F16,            fSmeF16F16);
    LOG_CPU_FEATURE(FEAT_SME2,                  fSme2);
    LOG_CPU_FEATURE(FEAT_SME2p1,                fSme2p1);
    LOG_CPU_FEATURE(FEAT_SPECRES2,              fSpecres2);
    LOG_CPU_FEATURE(FEAT_SPMU,                  fSpmu);
    LOG_CPU_FEATURE(FEAT_SPEv1p4,               fSpeV1p4);
    LOG_CPU_FEATURE(FEAT_SPE_CRR,               fSpeCrr);
    LOG_CPU_FEATURE(FEAT_SPE_FDS,               fSpeFds);
    LOG_CPU_FEATURE(FEAT_SVE2p1,                fSve2p1);
    LOG_CPU_FEATURE(FEAT_SVE_B16B16,            fSveB16B16);
    LOG_CPU_FEATURE(FEAT_SYSINSTR128,           fSysInstr128);
    LOG_CPU_FEATURE(FEAT_SYSREG128,             fSysReg128);
    LOG_CPU_FEATURE(FEAT_TCR2,                  fTcr2);
    LOG_CPU_FEATURE(FEAT_THE,                   fThe);
    LOG_CPU_FEATURE(FEAT_TRBE_EXT,              fTrbeExt);
    LOG_CPU_FEATURE(FEAT_TRBE_MPAM,             fTrbeMpam);
#undef LOG_CPU_FEATURE
#endif
}
