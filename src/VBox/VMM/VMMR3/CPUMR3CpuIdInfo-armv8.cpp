/* $Id: CPUMR3CpuIdInfo-armv8.cpp 168941 2025-05-20 21:23:52Z knut.osmundsen@oracle.com $ */
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

#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/string.h>



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
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


static PCSUPARMSYSREGVAL cpumR3CpuIdInfoArmLookupInner(PCSUPARMSYSREGVAL paIdRegs, uint32_t cIdRegs, uint32_t idReg)
{
    for (uint32_t i = 0; i < cIdRegs; i++)
        if (paIdRegs[i].idReg == idReg)
            return &paIdRegs[i];
    return NULL;
}


static PCSUPARMSYSREGVAL cpumR3CpuIdInfoArmLookup(PCPUMCPUIDINFOSTATEARMV8 pThis, uint32_t idReg)
{
    return cpumR3CpuIdInfoArmLookupInner(pThis->paIdRegs, pThis->cIdRegs, idReg);
}


static PCSUPARMSYSREGVAL cpumR3CpuIdInfoArmLookup2(PCPUMCPUIDINFOSTATEARMV8 pThis, uint32_t idReg)
{
    return cpumR3CpuIdInfoArmLookupInner(pThis->paIdRegs2, pThis->cIdRegs2, idReg);
}


/**
 * Display the guest CpuId leaves.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     "terse", "default" or "verbose".
 */
VMMR3DECL(void) CPUMR3CpuIdInfoArmV8(PCPUMCPUIDINFOSTATEARMV8 pThis)
{
    /*
     * Check input.
     */
    AssertPtrReturnVoid(pThis);
    PCDBGFINFOHLP const pHlp = pThis->Cmn.pHlp;
    AssertPtrReturnVoid(pHlp);
    AssertPtrReturnVoid(pThis->Cmn.pszShort);
    AssertReturnVoid(*pThis->Cmn.pszShort);
    AssertPtrReturnVoid(pThis->Cmn.pszLabel);
    AssertReturnVoid(*pThis->Cmn.pszLabel);
    AssertPtrNullReturnVoid(pThis->Cmn.pszShort);
    AssertPtrNullReturnVoid(pThis->Cmn.pszLabel2);
    AssertPtrNullReturnVoid(pThis->paIdRegs2);

    AssertReturnVoid(   (pThis->paIdRegs2 && pThis->cIdRegs2 && pThis->Cmn.pszShort2 && pThis->Cmn.pszLabel2)
                     || (!pThis->paIdRegs2 && !pThis->cIdRegs2 && !pThis->Cmn.pszShort2 && !pThis->Cmn.pszLabel2));


    /** @todo MIDR_EL1. */

    /*
     * Generic register dumping.
     */
    static struct
    {
        uint32_t            idReg;
        const char         *pszName;
        PCDBGFREGSUBFIELD   paFields;
    } const s_aDescs[] =
    {
#define ENTRY(a_RegNm, a_Desc)  {  RT_CONCAT(ARMV8_AARCH64_SYSREG_,a_RegNm), #a_RegNm, a_Desc }
        ENTRY(CLIDR_EL1,        g_aClidrEl1Fields),
        ENTRY(ID_AA64PFR0_EL1,  g_aIdAa64PfR0Fields),
        ENTRY(ID_AA64PFR1_EL1,  g_aIdAa64PfR1Fields),
        ENTRY(ID_AA64ISAR0_EL1, g_aIdAa64IsaR0Fields),
        ENTRY(ID_AA64ISAR1_EL1, g_aIdAa64IsaR1Fields),
        ENTRY(ID_AA64ISAR2_EL1, g_aIdAa64IsaR2Fields),
        ENTRY(ID_AA64MMFR0_EL1, g_aIdAa64MmfR0Fields),
        ENTRY(ID_AA64MMFR1_EL1, g_aIdAa64MmfR1Fields),
        ENTRY(ID_AA64MMFR2_EL1, g_aIdAa64MmfR2Fields),
        ENTRY(ID_AA64DFR0_EL1,  g_aIdAa64DfR0Fields),
        ENTRY(ID_AA64DFR1_EL1,  g_aIdAa64DfR1Fields),
        ENTRY(ID_AA64AFR0_EL1,  g_aIdAa64AfR0Fields),
        ENTRY(ID_AA64AFR1_EL1,  g_aIdAa64AfR1Fields),
#undef  ENTRY
    };
    for (unsigned i = 0; i < RT_ELEMENTS(s_aDescs); i++)
    {
        PCSUPARMSYSREGVAL const pIdReg = cpumR3CpuIdInfoArmLookup(pThis, s_aDescs[i].idReg);
        if (pIdReg)
        {
            if (pThis->Cmn.iVerbosity > 1)
            {
                PCSUPARMSYSREGVAL const pIdReg2 = cpumR3CpuIdInfoArmLookup2(pThis, s_aDescs[i].idReg);
                if (pIdReg2)
                    pHlp->pfnPrintf(pHlp, "%s %16s: %#018RX64 (%s %#018RX64)\n",
                                    pThis->Cmn.pszLabel, s_aDescs[i].pszName, pIdReg->uValue,
                                    pThis->Cmn.pszLabel2, pIdReg2->uValue);
                else
                    pHlp->pfnPrintf(pHlp, "%s %16s: %#018RX64\n",
                                    pThis->Cmn.pszLabel, s_aDescs[i].pszName, pIdReg->uValue);
                cpumR3CpuIdInfoVerboseCompareListU64(&pThis->Cmn, pIdReg->uValue, pIdReg2 ? pIdReg2->uValue : 0,
                                                     s_aDescs[i].paFields, 60, true /*fColumnHeaders*/);
            }
            else
            {
                pHlp->pfnPrintf(pHlp, "%16s = %#018RX64",
                                s_aDescs[i].pszName, pIdReg->uValue);
                cpumR3CpuIdInfoMnemonicListU64(&pThis->Cmn, pIdReg->uValue, s_aDescs[i].paFields, " :", 0);
            }
        }
    }
}


#ifdef VBOX_VMM_TARGET_ARMV8
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

    /*
     * Call generated code to do the actual printing.
     */
# ifdef RT_ARCH_ARM64
    if (fVerbose)
        CPUMR3CpuIdPrintArmV8Features(pHlp, &pVM->cpum.s.GuestFeatures, "guest", &pVM->cpum.s.HostFeatures.s, "host");
    else
# endif
        CPUMR3CpuIdPrintArmV8Features(pHlp, &pVM->cpum.s.GuestFeatures, "guest", NULL, NULL);
}
#endif /* VBOX_VMM_TARGET_ARMV8 */
