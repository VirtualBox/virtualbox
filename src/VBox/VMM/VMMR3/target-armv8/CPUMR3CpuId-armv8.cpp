/* $Id: CPUMR3CpuId-armv8.cpp 168941 2025-05-20 21:23:52Z knut.osmundsen@oracle.com $ */
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

