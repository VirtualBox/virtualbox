/* $Id: CPUMR3CpuIdInfo-x86.cpp 168941 2025-05-20 21:23:52Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - CPU ID part.
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
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/ssm.h>
#include "CPUMInternal.h"
#include <VBox/sup.h>

#include <iprt/ctype.h>
#include <iprt/string.h>
#include <iprt/x86-helpers.h>



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define IS_VALID_LEAF_PTR(a_pThis, a_pLeaf)  ((uintptr_t)((a_pLeaf) - (a_pThis)->paLeaves)  < (a_pThis)->cLeaves)
#define IS_VALID_LEAF2_PTR(a_pThis, a_pLeaf) ((uintptr_t)((a_pLeaf) - (a_pThis)->paLeaves2) < (a_pThis)->cLeaves2)



#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) || defined(VBOX_VMM_TARGET_X86)

/**
 * Get L1 cache / TLS associativity.
 */
static const char *getCacheAss(unsigned u, char *pszBuf)
{
    if (u == 0)
        return "res0  ";
    if (u == 1)
        return "direct";
    if (u == 255)
        return "fully";
    if (u >= 256)
        return "???";

    RTStrPrintf(pszBuf, 16, "%d way", u);
    return pszBuf;
}


/**
 * Get L2/L3 cache associativity.
 */
static const char *getL23CacheAss(unsigned u)
{
    switch (u)
    {
        case 0:  return "off   ";
        case 1:  return "direct";
        case 2:  return "2 way ";
        case 3:  return "3 way ";
        case 4:  return "4 way ";
        case 5:  return "6 way ";
        case 6:  return "8 way ";
        case 7:  return "res7  ";
        case 8:  return "16 way";
        case 9:  return "tpoext";   /* Overridden by Fn8000_001D */
        case 10: return "32 way";
        case 11: return "48 way";
        case 12: return "64 way";
        case 13: return "96 way";
        case 14: return "128way";
        case 15: return "fully ";
        default: return "????";
    }
}


/** CPUID(1).EDX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf1EdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("FPU\0"         "x87 FPU on Chip",                            0, 1, 0),
    DBGFREGSUBFIELD_RO("VME\0"         "Virtual 8086 Mode Enhancements",             1, 1, 0),
    DBGFREGSUBFIELD_RO("DE\0"          "Debugging extensions",                       2, 1, 0),
    DBGFREGSUBFIELD_RO("PSE\0"         "Page Size Extension",                        3, 1, 0),
    DBGFREGSUBFIELD_RO("TSC\0"         "Time Stamp Counter",                         4, 1, 0),
    DBGFREGSUBFIELD_RO("MSR\0"         "Model Specific Registers",                   5, 1, 0),
    DBGFREGSUBFIELD_RO("PAE\0"         "Physical Address Extension",                 6, 1, 0),
    DBGFREGSUBFIELD_RO("MCE\0"         "Machine Check Exception",                    7, 1, 0),
    DBGFREGSUBFIELD_RO("CX8\0"         "CMPXCHG8B instruction",                      8, 1, 0),
    DBGFREGSUBFIELD_RO("APIC\0"        "APIC On-Chip",                               9, 1, 0),
    DBGFREGSUBFIELD_RO("SEP\0"         "SYSENTER and SYSEXIT Present",              11, 1, 0),
    DBGFREGSUBFIELD_RO("MTRR\0"        "Memory Type Range Registers",               12, 1, 0),
    DBGFREGSUBFIELD_RO("PGE\0"         "PTE Global Bit",                            13, 1, 0),
    DBGFREGSUBFIELD_RO("MCA\0"         "Machine Check Architecture",                14, 1, 0),
    DBGFREGSUBFIELD_RO("CMOV\0"        "Conditional Move instructions",             15, 1, 0),
    DBGFREGSUBFIELD_RO("PAT\0"         "Page Attribute Table",                      16, 1, 0),
    DBGFREGSUBFIELD_RO("PSE-36\0"      "36-bit Page Size Extension",                17, 1, 0),
    DBGFREGSUBFIELD_RO("PSN\0"         "Processor Serial Number",                   18, 1, 0),
    DBGFREGSUBFIELD_RO("CLFSH\0"       "CLFLUSH instruction",                       19, 1, 0),
    DBGFREGSUBFIELD_RO("DS\0"          "Debug Store",                               21, 1, 0),
    DBGFREGSUBFIELD_RO("ACPI\0"        "Thermal Mon. & Soft. Clock Ctrl.",          22, 1, 0),
    DBGFREGSUBFIELD_RO("MMX\0"         "Intel MMX Technology",                      23, 1, 0),
    DBGFREGSUBFIELD_RO("FXSR\0"        "FXSAVE and FXRSTOR instructions",           24, 1, 0),
    DBGFREGSUBFIELD_RO("SSE\0"         "SSE support",                               25, 1, 0),
    DBGFREGSUBFIELD_RO("SSE2\0"        "SSE2 support",                              26, 1, 0),
    DBGFREGSUBFIELD_RO("SS\0"          "Self Snoop",                                27, 1, 0),
    DBGFREGSUBFIELD_RO("HTT\0"         "Hyper-Threading Technology",                28, 1, 0),
    DBGFREGSUBFIELD_RO("TM\0"          "Therm. Monitor",                            29, 1, 0),
    DBGFREGSUBFIELD_RO("PBE\0"         "Pending Break Enabled",                     31, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(1).ECX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf1EcxSubFields[] =
{
    DBGFREGSUBFIELD_RO("SSE3\0"       "SSE3 support",                                    0, 1, 0),
    DBGFREGSUBFIELD_RO("PCLMUL\0"     "PCLMULQDQ support (for AES-GCM)",                 1, 1, 0),
    DBGFREGSUBFIELD_RO("DTES64\0"     "DS Area 64-bit Layout",                           2, 1, 0),
    DBGFREGSUBFIELD_RO("MONITOR\0"    "MONITOR/MWAIT instructions",                      3, 1, 0),
    DBGFREGSUBFIELD_RO("CPL-DS\0"     "CPL Qualified Debug Store",                       4, 1, 0),
    DBGFREGSUBFIELD_RO("VMX\0"        "Virtual Machine Extensions",                      5, 1, 0),
    DBGFREGSUBFIELD_RO("SMX\0"        "Safer Mode Extensions",                           6, 1, 0),
    DBGFREGSUBFIELD_RO("EST\0"        "Enhanced SpeedStep Technology",                   7, 1, 0),
    DBGFREGSUBFIELD_RO("TM2\0"        "Terminal Monitor 2",                              8, 1, 0),
    DBGFREGSUBFIELD_RO("SSSE3\0"      "Supplemental Streaming SIMD Extensions 3",        9, 1, 0),
    DBGFREGSUBFIELD_RO("CNTX-ID\0"    "L1 Context ID",                                  10, 1, 0),
    DBGFREGSUBFIELD_RO("SDBG\0"       "Silicon Debug interface",                        11, 1, 0),
    DBGFREGSUBFIELD_RO("FMA\0"        "Fused Multiply Add extensions",                  12, 1, 0),
    DBGFREGSUBFIELD_RO("CX16\0"       "CMPXCHG16B instruction",                         13, 1, 0),
    DBGFREGSUBFIELD_RO("TPRUPDATE\0"  "xTPR Update Control",                            14, 1, 0),
    DBGFREGSUBFIELD_RO("PDCM\0"       "Perf/Debug Capability MSR",                      15, 1, 0),
    DBGFREGSUBFIELD_RO("PCID\0"       "Process Context Identifiers",                    17, 1, 0),
    DBGFREGSUBFIELD_RO("DCA\0"        "Direct Cache Access",                            18, 1, 0),
    DBGFREGSUBFIELD_RO("SSE4_1\0"     "SSE4_1 support",                                 19, 1, 0),
    DBGFREGSUBFIELD_RO("SSE4_2\0"     "SSE4_2 support",                                 20, 1, 0),
    DBGFREGSUBFIELD_RO("X2APIC\0"     "x2APIC support",                                 21, 1, 0),
    DBGFREGSUBFIELD_RO("MOVBE\0"      "MOVBE instruction",                              22, 1, 0),
    DBGFREGSUBFIELD_RO("POPCNT\0"     "POPCNT instruction",                             23, 1, 0),
    DBGFREGSUBFIELD_RO("TSCDEADL\0"   "Time Stamp Counter Deadline",                    24, 1, 0),
    DBGFREGSUBFIELD_RO("AES\0"        "AES instructions",                               25, 1, 0),
    DBGFREGSUBFIELD_RO("XSAVE\0"      "XSAVE instruction",                              26, 1, 0),
    DBGFREGSUBFIELD_RO("OSXSAVE\0"    "OSXSAVE instruction",                            27, 1, 0),
    DBGFREGSUBFIELD_RO("AVX\0"        "AVX support",                                    28, 1, 0),
    DBGFREGSUBFIELD_RO("F16C\0"       "16-bit floating point conversion instructions",  29, 1, 0),
    DBGFREGSUBFIELD_RO("RDRAND\0"     "RDRAND instruction",                             30, 1, 0),
    DBGFREGSUBFIELD_RO("HVP\0"        "Hypervisor Present (we're a guest)",             31, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(7,0).EBX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf7Sub0EbxSubFields[] =
{
    DBGFREGSUBFIELD_RO("FSGSBASE\0"         "RDFSBASE/RDGSBASE/WRFSBASE/WRGSBASE instr.",    0, 1, 0),
    DBGFREGSUBFIELD_RO("TSCADJUST\0"        "Supports MSR_IA32_TSC_ADJUST",                  1, 1, 0),
    DBGFREGSUBFIELD_RO("SGX\0"              "Supports Software Guard Extensions",            2, 1, 0),
    DBGFREGSUBFIELD_RO("BMI1\0"             "Advanced Bit Manipulation extension 1",         3, 1, 0),
    DBGFREGSUBFIELD_RO("HLE\0"              "Hardware Lock Elision",                         4, 1, 0),
    DBGFREGSUBFIELD_RO("AVX2\0"             "Advanced Vector Extensions 2",                  5, 1, 0),
    DBGFREGSUBFIELD_RO("FDP_EXCPTN_ONLY\0"  "FPU DP only updated on exceptions",             6, 1, 0),
    DBGFREGSUBFIELD_RO("SMEP\0"             "Supervisor Mode Execution Prevention",          7, 1, 0),
    DBGFREGSUBFIELD_RO("BMI2\0"             "Advanced Bit Manipulation extension 2",         8, 1, 0),
    DBGFREGSUBFIELD_RO("ERMS\0"             "Enhanced REP MOVSB/STOSB instructions",         9, 1, 0),
    DBGFREGSUBFIELD_RO("INVPCID\0"          "INVPCID instruction",                          10, 1, 0),
    DBGFREGSUBFIELD_RO("RTM\0"              "Restricted Transactional Memory",              11, 1, 0),
    DBGFREGSUBFIELD_RO("PQM\0"              "Platform Quality of Service Monitoring",       12, 1, 0),
    DBGFREGSUBFIELD_RO("DEPFPU_CS_DS\0"     "Deprecates FPU CS, FPU DS values if set",      13, 1, 0),
    DBGFREGSUBFIELD_RO("MPE\0"              "Intel Memory Protection Extensions",           14, 1, 0),
    DBGFREGSUBFIELD_RO("PQE\0"              "Platform Quality of Service Enforcement",      15, 1, 0),
    DBGFREGSUBFIELD_RO("AVX512F\0"          "AVX512 Foundation instructions",               16, 1, 0),
    DBGFREGSUBFIELD_RO("RDSEED\0"           "RDSEED instruction",                           18, 1, 0),
    DBGFREGSUBFIELD_RO("ADX\0"              "ADCX/ADOX instructions",                       19, 1, 0),
    DBGFREGSUBFIELD_RO("SMAP\0"             "Supervisor Mode Access Prevention",            20, 1, 0),
    DBGFREGSUBFIELD_RO("CLFLUSHOPT\0"       "CLFLUSHOPT (Cache Line Flush) instruction",    23, 1, 0),
    DBGFREGSUBFIELD_RO("CLWB\0"             "CLWB instruction",                             24, 1, 0),
    DBGFREGSUBFIELD_RO("INTEL_PT\0"         "Intel Processor Trace",                        25, 1, 0),
    DBGFREGSUBFIELD_RO("AVX512PF\0"         "AVX512 Prefetch instructions",                 26, 1, 0),
    DBGFREGSUBFIELD_RO("AVX512ER\0"         "AVX512 Exponential & Reciprocal instructions", 27, 1, 0),
    DBGFREGSUBFIELD_RO("AVX512CD\0"         "AVX512 Conflict Detection instructions",       28, 1, 0),
    DBGFREGSUBFIELD_RO("SHA\0"              "Secure Hash Algorithm extensions",             29, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(7,0).ECX field descriptions.   */
static DBGFREGSUBFIELD const g_aLeaf7Sub0EcxSubFields[] =
{
    DBGFREGSUBFIELD_RO("PREFETCHWT1\0" "PREFETCHWT1 instruction",                        0, 1, 0),
    DBGFREGSUBFIELD_RO("UMIP\0"         "User mode insturction prevention",              2, 1, 0),
    DBGFREGSUBFIELD_RO("PKU\0"          "Protection Key for Usermode pages",             3, 1, 0),
    DBGFREGSUBFIELD_RO("OSPKE\0"        "CR4.PKU mirror",                                4, 1, 0),
    DBGFREGSUBFIELD_RO("MAWAU\0"        "Value used by BNDLDX & BNDSTX",                17, 5, 0),
    DBGFREGSUBFIELD_RO("RDPID\0"        "Read processor ID support",                    22, 1, 0),
    DBGFREGSUBFIELD_RO("SGX_LC\0"       "Supports SGX Launch Configuration",            30, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(7,0).EDX field descriptions.   */
static DBGFREGSUBFIELD const g_aLeaf7Sub0EdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("MCU_OPT_CTRL\0" "Supports IA32_MCU_OPT_CTRL ",                   9, 1, 0),
    DBGFREGSUBFIELD_RO("MD_CLEAR\0"     "Supports MDS related buffer clearing",         10, 1, 0),
    DBGFREGSUBFIELD_RO("TSX_FORCE_ABORT\0" "Supports IA32_TSX_FORCE_ABORT",             11, 1, 0),
    DBGFREGSUBFIELD_RO("CET_IBT\0"      "Supports indirect branch tracking w/ CET",     20, 1, 0),
    DBGFREGSUBFIELD_RO("IBRS_IBPB\0"    "IA32_SPEC_CTRL.IBRS and IA32_PRED_CMD.IBPB",   26, 1, 0),
    DBGFREGSUBFIELD_RO("STIBP\0"        "Supports IA32_SPEC_CTRL.STIBP",                27, 1, 0),
    DBGFREGSUBFIELD_RO("FLUSH_CMD\0"    "Supports IA32_FLUSH_CMD",                      28, 1, 0),
    DBGFREGSUBFIELD_RO("ARCHCAP\0"      "Supports IA32_ARCH_CAP",                       29, 1, 0),
    DBGFREGSUBFIELD_RO("CORECAP\0"      "Supports IA32_CORE_CAP",                       30, 1, 0),
    DBGFREGSUBFIELD_RO("SSBD\0"         "Supports IA32_SPEC_CTRL.SSBD",                 31, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** CPUID(7,2).EBX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf7Sub2EbxSubFields[] =
{
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(7,2).ECX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf7Sub2EcxSubFields[] =
{
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(7,2).EDX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf7Sub2EdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("PSFD\0"         "Supports IA32_SPEC_CTRL[7] (PSFD)",             0, 1, 0),
    DBGFREGSUBFIELD_RO("IPRED_CTRL\0"   "Supports IA32_SPEC_CTRL[4:3] (IPRED_DIS)",      1, 1, 0),
    DBGFREGSUBFIELD_RO("RRSBA_CTRL\0"   "Supports IA32_SPEC_CTRL[6:5] (RRSBA_DIS)",      2, 1, 0),
    DBGFREGSUBFIELD_RO("DDPD_U\0"       "Supports IA32_SPEC_CTRL[8] (DDPD_U)",           3, 1, 0),
    DBGFREGSUBFIELD_RO("BHI_CTRL\0"     "Supports IA32_SPEC_CTRL[10] (BHI_DIS_S) ",      4, 1, 0),
    DBGFREGSUBFIELD_RO("MCDT_NO\0"      "No MXCSR Config Dependent Timing issues",       5, 1, 0),
    DBGFREGSUBFIELD_RO("UC_LOCK_DIS\0"  "Supports UC-lock disable and causing #AC",      6, 1, 0),
    DBGFREGSUBFIELD_RO("MONITOR_MITG_NO\0" "No MONITOR/UMONITOR power issues",           7, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** CPUID(13,0).EAX+EDX, XCR0, ++ bit descriptions. */
static DBGFREGSUBFIELD const g_aXSaveStateBits[] =
{
    DBGFREGSUBFIELD_RO("x87\0"       "Legacy FPU state",                                 0, 1, 0),
    DBGFREGSUBFIELD_RO("SSE\0"       "128-bit SSE state",                                1, 1, 0),
    DBGFREGSUBFIELD_RO("YMM_Hi128\0" "Upper 128 bits of YMM0-15 (AVX)",                  2, 1, 0),
    DBGFREGSUBFIELD_RO("BNDREGS\0"   "MPX bound register state",                         3, 1, 0),
    DBGFREGSUBFIELD_RO("BNDCSR\0"    "MPX bound config and status state",                4, 1, 0),
    DBGFREGSUBFIELD_RO("Opmask\0"    "opmask state",                                     5, 1, 0),
    DBGFREGSUBFIELD_RO("ZMM_Hi256\0" "Upper 256 bits of ZMM0-15 (AVX-512)",              6, 1, 0),
    DBGFREGSUBFIELD_RO("Hi16_ZMM\0"  "512-bits ZMM16-31 state (AVX-512)",                7, 1, 0),
    DBGFREGSUBFIELD_RO("LWP\0"       "Lightweight Profiling (AMD)",                     62, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(13,1).EAX field descriptions.   */
static DBGFREGSUBFIELD const g_aLeaf13Sub1EaxSubFields[] =
{
    DBGFREGSUBFIELD_RO("XSAVEOPT\0"  "XSAVEOPT is available",                            0, 1, 0),
    DBGFREGSUBFIELD_RO("XSAVEC\0"    "XSAVEC and compacted XRSTOR supported",            1, 1, 0),
    DBGFREGSUBFIELD_RO("XGETBC1\0"   "XGETBV with ECX=1 supported",                      2, 1, 0),
    DBGFREGSUBFIELD_RO("XSAVES\0"    "XSAVES/XRSTORS and IA32_XSS supported",            3, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** CPUID(0x80000001,0).EDX field descriptions.   */
static DBGFREGSUBFIELD const g_aExtLeaf1EdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("FPU\0"          "x87 FPU on Chip",                               0, 1, 0),
    DBGFREGSUBFIELD_RO("VME\0"          "Virtual 8086 Mode Enhancements",                1, 1, 0),
    DBGFREGSUBFIELD_RO("DE\0"           "Debugging extensions",                          2, 1, 0),
    DBGFREGSUBFIELD_RO("PSE\0"          "Page Size Extension",                           3, 1, 0),
    DBGFREGSUBFIELD_RO("TSC\0"          "Time Stamp Counter",                            4, 1, 0),
    DBGFREGSUBFIELD_RO("MSR\0"          "K86 Model Specific Registers",                  5, 1, 0),
    DBGFREGSUBFIELD_RO("PAE\0"          "Physical Address Extension",                    6, 1, 0),
    DBGFREGSUBFIELD_RO("MCE\0"          "Machine Check Exception",                       7, 1, 0),
    DBGFREGSUBFIELD_RO("CX8\0"          "CMPXCHG8B instruction",                         8, 1, 0),
    DBGFREGSUBFIELD_RO("APIC\0"         "APIC On-Chip",                                  9, 1, 0),
    DBGFREGSUBFIELD_RO("SEP\0"          "SYSCALL/SYSRET",                               11, 1, 0),
    DBGFREGSUBFIELD_RO("MTRR\0"         "Memory Type Range Registers",                  12, 1, 0),
    DBGFREGSUBFIELD_RO("PGE\0"          "PTE Global Bit",                               13, 1, 0),
    DBGFREGSUBFIELD_RO("MCA\0"          "Machine Check Architecture",                   14, 1, 0),
    DBGFREGSUBFIELD_RO("CMOV\0"         "Conditional Move instructions",                15, 1, 0),
    DBGFREGSUBFIELD_RO("PAT\0"          "Page Attribute Table",                         16, 1, 0),
    DBGFREGSUBFIELD_RO("PSE-36\0"       "36-bit Page Size Extension",                   17, 1, 0),
    DBGFREGSUBFIELD_RO("NX\0"           "No-Execute/Execute-Disable",                   20, 1, 0),
    DBGFREGSUBFIELD_RO("AXMMX\0"        "AMD Extensions to MMX instructions",           22, 1, 0),
    DBGFREGSUBFIELD_RO("MMX\0"          "Intel MMX Technology",                         23, 1, 0),
    DBGFREGSUBFIELD_RO("FXSR\0"         "FXSAVE and FXRSTOR Instructions",              24, 1, 0),
    DBGFREGSUBFIELD_RO("FFXSR\0"        "AMD fast FXSAVE and FXRSTOR instructions",     25, 1, 0),
    DBGFREGSUBFIELD_RO("Page1GB\0"      "1 GB large page",                              26, 1, 0),
    DBGFREGSUBFIELD_RO("RDTSCP\0"       "RDTSCP instruction",                           27, 1, 0),
    DBGFREGSUBFIELD_RO("LM\0"           "AMD64 Long Mode",                              29, 1, 0),
    DBGFREGSUBFIELD_RO("3DNOWEXT\0"     "AMD Extensions to 3DNow",                      30, 1, 0),
    DBGFREGSUBFIELD_RO("3DNOW\0"        "AMD 3DNow",                                    31, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(0x80000001,0).ECX field descriptions.   */
static DBGFREGSUBFIELD const g_aExtLeaf1EcxSubFields[] =
{
    DBGFREGSUBFIELD_RO("LahfSahf\0"     "LAHF/SAHF support in 64-bit mode",              0, 1, 0),
    DBGFREGSUBFIELD_RO("CmpLegacy\0"    "Core multi-processing legacy mode",             1, 1, 0),
    DBGFREGSUBFIELD_RO("SVM\0"          "AMD Secure Virtual Machine extensions",         2, 1, 0),
    DBGFREGSUBFIELD_RO("EXTAPIC\0"      "AMD Extended APIC registers",                   3, 1, 0),
    DBGFREGSUBFIELD_RO("CR8L\0"         "AMD LOCK MOV CR0 means MOV CR8",                4, 1, 0),
    DBGFREGSUBFIELD_RO("ABM\0"          "AMD Advanced Bit Manipulation",                 5, 1, 0),
    DBGFREGSUBFIELD_RO("SSE4A\0"        "SSE4A instructions",                            6, 1, 0),
    DBGFREGSUBFIELD_RO("MISALIGNSSE\0"  "AMD Misaligned SSE mode",                       7, 1, 0),
    DBGFREGSUBFIELD_RO("3DNOWPRF\0"     "AMD PREFETCH and PREFETCHW instructions",       8, 1, 0),
    DBGFREGSUBFIELD_RO("OSVW\0"         "AMD OS Visible Workaround",                     9, 1, 0),
    DBGFREGSUBFIELD_RO("IBS\0"          "Instruct Based Sampling",                      10, 1, 0),
    DBGFREGSUBFIELD_RO("XOP\0"          "Extended Operation support",                   11, 1, 0),
    DBGFREGSUBFIELD_RO("SKINIT\0"       "SKINIT, STGI, and DEV support",                12, 1, 0),
    DBGFREGSUBFIELD_RO("WDT\0"          "AMD Watchdog Timer support",                   13, 1, 0),
    DBGFREGSUBFIELD_RO("LWP\0"          "Lightweight Profiling support",                15, 1, 0),
    DBGFREGSUBFIELD_RO("FMA4\0"         "Four operand FMA instruction support",         16, 1, 0),
    DBGFREGSUBFIELD_RO("TCE\0"          "Translation Cache Extension support",          17, 1, 0),
    DBGFREGSUBFIELD_RO("NodeId\0"       "NodeId in MSR C001_100C",                      19, 1, 0),
    DBGFREGSUBFIELD_RO("TBM\0"          "Trailing Bit Manipulation instructions",       21, 1, 0),
    DBGFREGSUBFIELD_RO("TOPOEXT\0"      "Topology Extensions",                          22, 1, 0),
    DBGFREGSUBFIELD_RO("PRFEXTCORE\0"   "Performance Counter Extensions support",       23, 1, 0),
    DBGFREGSUBFIELD_RO("PRFEXTNB\0"     "NB Performance Counter Extensions support",    24, 1, 0),
    DBGFREGSUBFIELD_RO("DATABPEXT\0"    "Data-access Breakpoint Extension",             26, 1, 0),
    DBGFREGSUBFIELD_RO("PERFTSC\0"      "Performance Time Stamp Counter",               27, 1, 0),
    DBGFREGSUBFIELD_RO("PCX_L2I\0"      "L2I/L3 Performance Counter Extensions",        28, 1, 0),
    DBGFREGSUBFIELD_RO("MONITORX\0"     "MWAITX and MONITORX instructions",             29, 1, 0),
    DBGFREGSUBFIELD_RO("AddrMaskExt\0"  "BP Addressing masking extended to bit 31",     30, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(0x8000000a,0).EDX field descriptions.   */
static DBGFREGSUBFIELD const g_aExtLeafAEdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("NP\0"             "Nested Paging",                               0, 1, 0),
    DBGFREGSUBFIELD_RO("LbrVirt\0"        "Last Branch Record Virtualization",           1, 1, 0),
    DBGFREGSUBFIELD_RO("SVML\0"           "SVM Lock",                                    2, 1, 0),
    DBGFREGSUBFIELD_RO("NRIPS\0"          "NextRIP Save",                                3, 1, 0),
    DBGFREGSUBFIELD_RO("TscRateMsr\0"     "MSR based TSC rate control",                  4, 1, 0),
    DBGFREGSUBFIELD_RO("VmcbClean\0"      "VMCB clean bits",                             5, 1, 0),
    DBGFREGSUBFIELD_RO("FlushByASID\0"    "Flush by ASID",                               6, 1, 0),
    DBGFREGSUBFIELD_RO("DecodeAssists\0"  "Decode Assists",                              7, 1, 0),
    DBGFREGSUBFIELD_RO("PauseFilter\0"    "Pause intercept filter",                     10, 1, 0),
    DBGFREGSUBFIELD_RO("PauseFilterThreshold\0" "Pause filter threshold",               12, 1, 0),
    DBGFREGSUBFIELD_RO("AVIC\0"           "Advanced Virtual Interrupt Controller",      13, 1, 0),
    DBGFREGSUBFIELD_RO("VMSAVEVirt\0"     "VMSAVE and VMLOAD Virtualization",           15, 1, 0),
    DBGFREGSUBFIELD_RO("VGIF\0"           "Virtual Global-Interrupt Flag",              16, 1, 0),
    DBGFREGSUBFIELD_RO("GMET\0"           "Guest Mode Execute Trap Extension",          17, 1, 0),
    DBGFREGSUBFIELD_RO("x2AVIC\0"         "AVIC support for x2APIC mode",               18, 1, 0),
    DBGFREGSUBFIELD_RO("SSSCheck\0"       "SVM supervisor shadow stack restrictions",   19, 1, 0),
    DBGFREGSUBFIELD_RO("SpecCtrl\0"       "SPEC_CTRL virtualization",                   20, 1, 0),
    DBGFREGSUBFIELD_RO("ROGPT\0"          "Read-Only Guest Page Table feature support", 21, 1, 0),
    DBGFREGSUBFIELD_RO("HOST_MCE_OVERRIDE\0"    "Guest #MC can be intercepted",         23, 1, 0),
    DBGFREGSUBFIELD_RO("TlbiCtl\0"        "INVLPGB/TLBSYNC enable and intercept",       24, 1, 0),
    DBGFREGSUBFIELD_RO("VNMI\0"           "NMI Virtualization",                         25, 1, 0),
    DBGFREGSUBFIELD_RO("IbsVirt\0"        "IBS Virtualization",                         26, 1, 0),
    DBGFREGSUBFIELD_RO("ExtLvtAvicAccessChg\0"  "Extended LVT AVIC access changes",     27, 1, 0),
    DBGFREGSUBFIELD_RO("NestedVirtVmcbAddrChk\0""Guest VMCB address check",             28, 1, 0),
    DBGFREGSUBFIELD_RO("BusLockThreshold\0"     "Bus Lock Threshold",                   29, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** CPUID(0x80000007,0).EDX field descriptions.   */
static DBGFREGSUBFIELD const g_aExtLeaf7EdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("TS\0"           "Temperature Sensor",                            0, 1, 0),
    DBGFREGSUBFIELD_RO("FID\0"          "Frequency ID control",                          1, 1, 0),
    DBGFREGSUBFIELD_RO("VID\0"          "Voltage ID control",                            2, 1, 0),
    DBGFREGSUBFIELD_RO("TTP\0"          "Thermal Trip",                                  3, 1, 0),
    DBGFREGSUBFIELD_RO("TM\0"           "Hardware Thermal Control (HTC)",                4, 1, 0),
    DBGFREGSUBFIELD_RO("100MHzSteps\0"  "100 MHz Multiplier control",                    6, 1, 0),
    DBGFREGSUBFIELD_RO("HwPstate\0"     "Hardware P-state control",                      7, 1, 0),
    DBGFREGSUBFIELD_RO("TscInvariant\0" "Invariant Time Stamp Counter",                  8, 1, 0),
    DBGFREGSUBFIELD_RO("CPB\0"          "Core Performance Boost",                        9, 1, 0),
    DBGFREGSUBFIELD_RO("EffFreqRO\0"    "Read-only Effective Frequency Interface",      10, 1, 0),
    DBGFREGSUBFIELD_RO("ProcFdbkIf\0"   "Processor Feedback Interface",                 11, 1, 0),
    DBGFREGSUBFIELD_RO("ProcPwrRep\0"   "Core power reporting interface support",       12, 1, 0),
    DBGFREGSUBFIELD_RO("ConnectedStandby\0" "Connected Standby",                        13, 1, 0),
    DBGFREGSUBFIELD_RO("RAPL\0"         "Running average power limit",                  14, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(0x80000008,0).EBX field descriptions.   */
static DBGFREGSUBFIELD const g_aExtLeaf8EbxSubFields[] =
{
    DBGFREGSUBFIELD_RO("CLZERO\0"       "Clear zero instruction (cacheline)",            0, 1, 0),
    DBGFREGSUBFIELD_RO("IRPerf\0"       "Instructions retired count support",            1, 1, 0),
    DBGFREGSUBFIELD_RO("XSaveErPtr\0"   "Save/restore error pointers (FXSAVE/RSTOR)",    2, 1, 0),
    DBGFREGSUBFIELD_RO("INVLPGB\0"      "INVLPGB and TLBSYNC instructions",              3, 1, 0),
    DBGFREGSUBFIELD_RO("RDPRU\0"        "RDPRU instruction",                             4, 1, 0),
    DBGFREGSUBFIELD_RO("BE\0"           "Bandwidth Enforcement extension",               6, 1, 0),
    DBGFREGSUBFIELD_RO("MCOMMIT\0"      "MCOMMIT instruction",                           8, 1, 0),
    DBGFREGSUBFIELD_RO("WBNOINVD\0"     "WBNOINVD instruction",                          9, 1, 0),
    DBGFREGSUBFIELD_RO("IBPB\0"         "Supports the IBPB command in IA32_PRED_CMD",   12, 1, 0),
    DBGFREGSUBFIELD_RO("INT_WBINVD\0"   "WBINVD/WBNOINVD interruptible",                13, 1, 0),
    DBGFREGSUBFIELD_RO("IBRS\0"         "Indirect Branch Restricted Speculation",       14, 1, 0),
    DBGFREGSUBFIELD_RO("STIBP\0"        "Single Thread Indirect Branch Prediction",     15, 1, 0),
    DBGFREGSUBFIELD_RO("IbrsAlwaysOn\0" "Processor prefers that IBRS be left on",       16, 1, 0),
    DBGFREGSUBFIELD_RO("StibpAlwaysOn\0""Processor prefers that STIBP be left on",      17, 1, 0),
    DBGFREGSUBFIELD_RO("IbrsPreferred\0""IBRS preferred over software solution",        18, 1, 0),
    DBGFREGSUBFIELD_RO("IbrsSameMode\0" "IBRS limits same mode speculation",            19, 1, 0),
    DBGFREGSUBFIELD_RO("EferLmsleUnsupported\0" "EFER.LMSLE is unsupported",            20, 1, 0),
    DBGFREGSUBFIELD_RO("INVLPGBnestedPages\0"   "INVLPGB for nested translation",       21, 1, 0),
    DBGFREGSUBFIELD_RO("PPIN\0"         "Protected processor inventory number",         23, 1, 0),
    DBGFREGSUBFIELD_RO("SSBD\0"         "Speculative Store Bypass Disable",             24, 1, 0),
    DBGFREGSUBFIELD_RO("SsbdVirtSpecCtrl\0"     "Use VIRT_SPEC_CTL for SSBD",           25, 1, 0),
    DBGFREGSUBFIELD_RO("SsbdNotRequired\0"      "SSBD not needed on this processor",    26, 1, 0),
    DBGFREGSUBFIELD_RO("CPPC\0"         "Collaborative Processor Performance Control",  27, 1, 0),
    DBGFREGSUBFIELD_RO("PSFD\0"         "Predictive Store Forward Disable",             28, 1, 0),
    DBGFREGSUBFIELD_RO("BTC_NO\0"       "Unaffected by branch type confusion",          29, 1, 0),
    DBGFREGSUBFIELD_RO("IBPB_RET\0"     "Clears RA predictor when PRED_CMD.IBPB set",   30, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(0xc0000001,0).EDX field descriptions.   */
static DBGFREGSUBFIELD const g_aViaLeaf1EdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("AIS/SM2\0"      "Alternate Instruction Set / GMI instr",          0, 1, 0),
    DBGFREGSUBFIELD_RO("AIS-E/SM2_EN\0" "AIS enabled / SM2 instructions enabled",         1, 1, 0),
    DBGFREGSUBFIELD_RO("RNG\0"          "Random Number Generator",                        2, 1, 0),
    DBGFREGSUBFIELD_RO("RNG-E\0"        "RNG enabled",                                    3, 1, 0),
    DBGFREGSUBFIELD_RO("LH/CCS\0"       "LongHaul MSR 0000_110Ah / CSS_HASH+CSS_ENCRYPT", 4, 1, 0),
    DBGFREGSUBFIELD_RO("FEMMS/CSS-EN\0" "FEMMS / SM3+SM4 instructions enabled ",          5, 1, 0),
    DBGFREGSUBFIELD_RO("ACE\0"          "Advanced Cryptography Engine",                   6, 1, 0),
    DBGFREGSUBFIELD_RO("ACE-E\0"        "ACE enabled",                                    7, 1, 0),
    DBGFREGSUBFIELD_RO("ACE2\0"         "Advanced Cryptography Engine 2",                 8, 1, 0),/* possibly indicating MM/HE and MM/HE-E on older chips... */
    DBGFREGSUBFIELD_RO("ACE2-E\0"       "ACE enabled",                                    9, 1, 0),
    DBGFREGSUBFIELD_RO("PHE\0"          "Padlock Hash Engine",                           10, 1, 0),
    DBGFREGSUBFIELD_RO("PHE-E\0"        "PHE enabled",                                   11, 1, 0),
    DBGFREGSUBFIELD_RO("PMM\0"          "Montgomery Multiplier",                         12, 1, 0),
    DBGFREGSUBFIELD_RO("PMM-E\0"        "PMM enabled",                                   13, 1, 0),
    DBGFREGSUBFIELD_RO("ZX-FMA\0"       "FMA supported",                                 15, 1, 0),
    DBGFREGSUBFIELD_RO("PARALLAX\0"     "Adaptive p-state control",                      16, 1, 0),
    DBGFREGSUBFIELD_RO("PARALLAX-EN\0"  "Parallax enabled",                              17, 1, 0),
    DBGFREGSUBFIELD_RO("OVERSTRESS\0"   "Overstress feature for auto overclock",         18, 1, 0),
    DBGFREGSUBFIELD_RO("OVERSTRESS-EN\0" "Overstress enabled",                           19, 1, 0),
    DBGFREGSUBFIELD_RO("TM3\0"          "Temperature Monitoring 3",                      20, 1, 0),
    DBGFREGSUBFIELD_RO("TM3-E\0"        "TM3 enabled",                                   21, 1, 0),
    DBGFREGSUBFIELD_RO("RNG2\0"         "Random Number Generator 2",                     22, 1, 0),
    DBGFREGSUBFIELD_RO("RNG2-E\0"       "RNG2 enabled",                                  23, 1, 0),
    DBGFREGSUBFIELD_RO("PHE2\0"         "Padlock Hash Engine 2",                         25, 1, 0),
    DBGFREGSUBFIELD_RO("PHE2-E\0"       "PHE2 enabled",                                  26, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** Helper for looking up a primary CPUID leaf. */
static PCCPUMCPUIDLEAF cpumR3CpuIdInfoX86Lookup(PCPUMCPUIDINFOSTATEX86 pThis, uint32_t uLeaf, uint32_t uSubLeaf = 0)
{
    return cpumCpuIdGetLeafInt((PCPUMCPUIDLEAF)pThis->paLeaves, pThis->cLeaves, uLeaf, uSubLeaf);
}


/** Helper for looking up a secondary CPUID leaf. */
static PCCPUMCPUIDLEAF cpumR3CpuIdInfoX86Lookup2(PCPUMCPUIDINFOSTATEX86 pThis, uint32_t uLeaf, uint32_t uSubLeaf = 0)
{
    return cpumCpuIdGetLeafInt((PCPUMCPUIDLEAF)pThis->paLeaves2, pThis->cLeaves2, uLeaf, uSubLeaf);
}


/**
 * Produces a detailed summary of standard leaf 0x00000001.
 *
 * @param   pThis       The info dumper state.
 * @param   pCurLeaf    The 0x00000001 leaf.
 * @param   fIntel      Set if intel CPU.
 */
static void cpumR3CpuIdInfoStdLeaf1Details(PCPUMCPUIDINFOSTATEX86 pThis, PCCPUMCPUIDLEAF pCurLeaf, bool fIntel)
{
    Assert(pCurLeaf); Assert(pCurLeaf->uLeaf == 1);
    static const char * const s_apszTypes[4] = { "primary", "overdrive", "MP", "reserved" };
    uint32_t const            uEAX = pCurLeaf->uEax;
    uint32_t const            uEBX = pCurLeaf->uEbx;
    PCDBGFINFOHLP const       pHlp = pThis->Cmn.pHlp;

    pHlp->pfnPrintf(pHlp,
                    "%36s %2d \tExtended: %d \tEffective: %d\n"
                    "%36s %2d \tExtended: %d \tEffective: %d\n"
                    "%36s %d\n"
                    "%36s %d (%s)\n"
                    "%36s %#04x\n"
                    "%36s %d\n"
                    "%36s %d\n"
                    "%36s %#04x\n"
                    ,
                    "Family:",      (uEAX >> 8) & 0xf, (uEAX >> 20) & 0x7f, RTX86GetCpuFamily(uEAX),
                    "Model:",       (uEAX >> 4) & 0xf, (uEAX >> 16) & 0x0f, RTX86GetCpuModel(uEAX, fIntel),
                    "Stepping:",    RTX86GetCpuStepping(uEAX),
                    "Type:",        (uEAX >> 12) & 3, s_apszTypes[(uEAX >> 12) & 3],
                    "APIC ID:",     (uEBX >> 24) & 0xff,
                    "Logical CPUs:",(uEBX >> 16) & 0xff,
                    "CLFLUSH Size:",(uEBX >>  8) & 0xff,
                    "Brand ID:",    (uEBX >>  0) & 0xff);
    if (pThis->Cmn.iVerbosity > 1)
    {
        PCCPUMCPUIDLEAF const pL2 = cpumR3CpuIdInfoX86Lookup2(pThis, 1, 0);
        cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEdx, pL2 ? pL2->uEdx : 0, g_aLeaf1EdxSubFields, "Features", 56);
        cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEcx, pL2 ? pL2->uEcx : 0, g_aLeaf1EcxSubFields, NULL, 56);
    }
    else
    {
        cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEdx, g_aLeaf1EdxSubFields, "Features EDX", 36);
        cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEcx, g_aLeaf1EcxSubFields, "Features ECX", 36);
    }
}


/**
 * Produces a detailed summary of standard leaf 0x00000007.
 *
 * @param   pThis       The info dumper state.
 * @param   pHlp        The info helper functions.
 * @param   pCurLeaf    The first 0x00000007 leaf.
 */
static void cpumR3CpuIdInfoStdLeaf7Details(PCPUMCPUIDINFOSTATEX86 pThis, PCCPUMCPUIDLEAF pCurLeaf)
{
    Assert(pCurLeaf); Assert(pCurLeaf->uLeaf == 7);
    PCDBGFINFOHLP const pHlp = pThis->Cmn.pHlp;

    pHlp->pfnPrintf(pHlp, "Structured Extended Feature Flags Enumeration (leaf 7):\n");
    for (;;)
    {
        PCCPUMCPUIDLEAF const pL2 = cpumR3CpuIdInfoX86Lookup2(pThis, pCurLeaf->uLeaf, pCurLeaf->uSubLeaf);

        switch (pCurLeaf->uSubLeaf)
        {
            case 0:
                if (pThis->Cmn.iVerbosity > 1)
                {
                    cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEbx, pL2 ? pL2->uEbx : 0, g_aLeaf7Sub0EbxSubFields, "Sub-leaf 0", 56);
                    cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEcx, pL2 ? pL2->uEcx : 0, g_aLeaf7Sub0EcxSubFields, NULL, 56);
                    if (pCurLeaf->uEdx || (pL2 && pL2->uEdx))
                        cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEdx, pL2 ? pL2->uEdx : 0, g_aLeaf7Sub0EdxSubFields, NULL, 56);
                }
                else
                {
                    cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEbx, g_aLeaf7Sub0EbxSubFields, "Ext Features #0 EBX", 36);
                    cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEcx, g_aLeaf7Sub0EcxSubFields, "Ext Features #0 ECX", 36);
                    if (pCurLeaf->uEdx)
                        cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEdx, g_aLeaf7Sub0EdxSubFields, "Ext Features #0 EDX", 36);
                }
                break;

            /** @todo case 1   */

            case 2:
                if (pThis->Cmn.iVerbosity > 1)
                {
                    pHlp->pfnPrintf(pHlp, " Sub-leaf 2\n");
                    pHlp->pfnPrintf(pHlp, "  Mnemonic - Description                                  = %s %s%s%s\n",
                                    pThis->Cmn.pszLabel, pThis->Cmn.pszLabel2 ? "(" : "",
                                    pThis->Cmn.pszLabel2 ? pThis->Cmn.pszLabel2 : "", pThis->Cmn.pszLabel2 ? ")" : "");
                    if (pCurLeaf->uEbx || (pL2 && pL2->uEbx))
                        cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEbx, pL2 ? pL2->uEbx : 0, g_aLeaf7Sub2EbxSubFields, NULL, 56);
                    if (pCurLeaf->uEcx || (pL2 && pL2->uEcx))
                        cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEcx, pL2 ? pL2->uEcx : 0, g_aLeaf7Sub2EcxSubFields, NULL, 56);
                    cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEdx, pL2 ? pL2->uEdx : 0, g_aLeaf7Sub2EdxSubFields, NULL, 56);
                }
                else
                {
                    if (pCurLeaf->uEbx)
                        cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEbx, g_aLeaf7Sub2EbxSubFields, "Ext Features #2 EBX", 36);
                    if (pCurLeaf->uEcx)
                        cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEcx, g_aLeaf7Sub2EcxSubFields, "Ext Features #2 ECX", 36);
                    if (pCurLeaf->uEdx)
                        cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEdx, g_aLeaf7Sub2EdxSubFields, "Ext Features #2 EDX", 36);
                }
                break;

            default:
                if (pCurLeaf->uEdx || pCurLeaf->uEcx || pCurLeaf->uEbx)
                    pHlp->pfnPrintf(pHlp, "Unknown extended feature sub-leaf #%u: EAX=%#x EBX=%#x ECX=%#x EDX=%#x\n",
                                    pCurLeaf->uSubLeaf, pCurLeaf->uEax, pCurLeaf->uEbx, pCurLeaf->uEcx, pCurLeaf->uEdx);
                break;

        }

        /* advance. */
        pCurLeaf++;
        if (   !IS_VALID_LEAF_PTR(pThis, pCurLeaf)
            || pCurLeaf->uLeaf != 0x7)
            break;
    }
}


/**
 * Produces a detailed summary of standard leaf 0x0000000d.
 *
 * @param   pThis       The info dumper state.
 * @param   pCurLeaf    The first 0x00000007 leaf.
 */
static void cpumR3CpuIdInfoStdLeaf13Details(PCPUMCPUIDINFOSTATEX86 pThis, PCCPUMCPUIDLEAF pCurLeaf)
{
    Assert(pCurLeaf); Assert(pCurLeaf->uLeaf == 13);
    PCDBGFINFOHLP const pHlp = pThis->Cmn.pHlp;

    pHlp->pfnPrintf(pHlp, "Processor Extended State Enumeration (leaf 0xd):\n");
    for (uint32_t uSubLeaf = 0; uSubLeaf < 64; uSubLeaf++)
    {
        PCCPUMCPUIDLEAF const pL2 = cpumR3CpuIdInfoX86Lookup2(pThis, UINT32_C(0x0000000d), uSubLeaf);

        switch (uSubLeaf)
        {
            case 0:
                if (pCurLeaf && pCurLeaf->uSubLeaf == uSubLeaf)
                    pHlp->pfnPrintf(pHlp, "%36s%*s: %#x/%#x\n", "XSAVE area cur/max size by XCR0, ",
                                    pThis->Cmn.cchLabelMax, pThis->Cmn.pszLabel, pCurLeaf->uEbx, pCurLeaf->uEcx);
                if (pL2)
                    pHlp->pfnPrintf(pHlp, "%36s%*s: %#x/%#x\n", "XSAVE area cur/max size by XCR0, ",
                                    pThis->Cmn.cchLabelMax, pThis->Cmn.pszLabel2, pL2->uEbx, pL2->uEcx);

                if (pCurLeaf && pCurLeaf->uSubLeaf == uSubLeaf)
                    cpumR3CpuIdInfoValueWithMnemonicListU64(&pThis->Cmn, RT_MAKE_U64(pCurLeaf->uEax, pCurLeaf->uEdx),
                                                            g_aXSaveStateBits,
                                                            "Valid XCR0 bits, ", 36, pThis->Cmn.pszLabel, pThis->Cmn.cchLabelMax);
                if (pL2)
                    cpumR3CpuIdInfoValueWithMnemonicListU64(&pThis->Cmn, RT_MAKE_U64(pL2->uEax, pL2->uEdx), g_aXSaveStateBits,
                                                            "Valid XCR0 bits, ", 36, pThis->Cmn.pszLabel2, pThis->Cmn.cchLabelMax);
                break;

            case 1:
                if (pCurLeaf && pCurLeaf->uSubLeaf == uSubLeaf)
                    cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEax, g_aLeaf13Sub1EaxSubFields,
                                                   "XSAVE features, ", 36, pThis->Cmn.pszLabel, pThis->Cmn.cchLabelMax);
                if (pL2)
                    cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pL2->uEax, g_aLeaf13Sub1EaxSubFields,
                                                   "XSAVE features, ", 36, pThis->Cmn.pszLabel2, pThis->Cmn.cchLabelMax);

                if (pCurLeaf && pCurLeaf->uSubLeaf == uSubLeaf)
                    pHlp->pfnPrintf(pHlp, "%36s%*s: %#x\n", "XSAVE area cur size XCR0|XSS, ",
                                    pThis->Cmn.cchLabelMax, pThis->Cmn.pszLabel, pCurLeaf->uEbx);
                if (pL2)
                    pHlp->pfnPrintf(pHlp, "%36s%*s: %#x\n", "XSAVE area cur size XCR0|XSS, ",
                                    pThis->Cmn.cchLabelMax, pThis->Cmn.pszLabel2, pL2->uEbx);

                if (pCurLeaf && pCurLeaf->uSubLeaf == uSubLeaf)
                    cpumR3CpuIdInfoValueWithMnemonicListU64(&pThis->Cmn, RT_MAKE_U64(pCurLeaf->uEcx, pCurLeaf->uEdx),
                                                            g_aXSaveStateBits, "  Valid IA32_XSS bits, ", 36,
                                                            pThis->Cmn.pszLabel, pThis->Cmn.cchLabelMax);
                if (pL2)
                    cpumR3CpuIdInfoValueWithMnemonicListU64(&pThis->Cmn, RT_MAKE_U64(pL2->uEdx, pL2->uEcx),
                                                            g_aXSaveStateBits, "  Valid IA32_XSS bits, ", 36,
                                                            pThis->Cmn.pszLabel2, pThis->Cmn.cchLabelMax);
                break;

            default:
                if (   pCurLeaf
                    && pCurLeaf->uSubLeaf == uSubLeaf
                    && (pCurLeaf->uEax || pCurLeaf->uEbx || pCurLeaf->uEcx || pCurLeaf->uEdx) )
                {
                    pHlp->pfnPrintf(pHlp, "  State #%u, %*s: off=%#06x, cb=%#06x %s",
                                    uSubLeaf, pThis->Cmn.cchLabelMax, pThis->Cmn.pszLabel,
                                    pCurLeaf->uEbx, pCurLeaf->uEax, pCurLeaf->uEcx & RT_BIT_32(0) ? "XCR0-bit" : "IA32_XSS-bit");
                    if (pCurLeaf->uEcx & ~RT_BIT_32(0))
                        pHlp->pfnPrintf(pHlp, " ECX[reserved]=%#x\n", pCurLeaf->uEcx & ~RT_BIT_32(0));
                    if (pCurLeaf->uEdx)
                        pHlp->pfnPrintf(pHlp, " EDX[reserved]=%#x\n", pCurLeaf->uEdx);
                    pHlp->pfnPrintf(pHlp, " --");
                    cpumR3CpuIdInfoMnemonicListU64(&pThis->Cmn, RT_BIT_64(uSubLeaf), g_aXSaveStateBits, NULL, 0);
                    pHlp->pfnPrintf(pHlp, "\n");
                }
                if (pL2 && (pL2->uEax || pL2->uEbx || pL2->uEcx || pL2->uEdx))
                {
                    pHlp->pfnPrintf(pHlp, "  State #%u, %*s:  off=%#06x, cb=%#06x %s",
                                    uSubLeaf, pThis->Cmn.cchLabelMax, pThis->Cmn.pszLabel2,
                                    pL2->uEbx, pL2->uEax, pL2->uEcx & RT_BIT_32(0) ? "XCR0-bit" : "IA32_XSS-bit");
                    if (pL2->uEcx & ~RT_BIT_32(0))
                        pHlp->pfnPrintf(pHlp, " ECX[reserved]=%#x\n", pL2->uEcx & ~RT_BIT_32(0));
                    if (pL2->uEdx)
                        pHlp->pfnPrintf(pHlp, " EDX[reserved]=%#x\n", pL2->uEdx);
                    pHlp->pfnPrintf(pHlp, " --");
                    cpumR3CpuIdInfoMnemonicListU64(&pThis->Cmn, RT_BIT_64(uSubLeaf), g_aXSaveStateBits, NULL, 0);
                    pHlp->pfnPrintf(pHlp, "\n");
                }
                break;

        }

        /* advance. */
        if (pCurLeaf)
        {
            while (   IS_VALID_LEAF_PTR(pThis, pCurLeaf)
                   && pCurLeaf->uSubLeaf <= uSubLeaf
                   && pCurLeaf->uLeaf == UINT32_C(0x0000000d))
                pCurLeaf++;
            if (   !IS_VALID_LEAF_PTR(pThis, pCurLeaf)
                || pCurLeaf->uLeaf != UINT32_C(0x0000000d))
                pCurLeaf = NULL;
        }
    }
}

static PCCPUMCPUIDLEAF
cpumCpuIdGetFirstLeafInRange(PCCPUMCPUIDLEAF paLeaves, uint32_t cLeaves, uint32_t uFromLeaf, uint32_t uUpToLeaf)
{
    /*
     * Assuming the leaves are sorted, do a binary lookup.
     */
    if (cLeaves > 0)
    {
        uint32_t idxLow  = 0;
        uint32_t idxHigh = cLeaves;
        for (;;)
        {
            uint32_t idx = idxLow + (idxHigh - idxLow) / 2;
            if (paLeaves[idx].uLeaf < uFromLeaf)
            {
                idx += 1;
                if (idx < idxHigh)
                    idxLow = idx;
                else if (   idx < cLeaves
                         && paLeaves[idx].uLeaf <= uUpToLeaf
                         && paLeaves[idx].uLeaf >= uFromLeaf)
                    return &paLeaves[idx];
                else
                    return NULL;
            }
            else if (paLeaves[idx].uLeaf > uFromLeaf)
            {
                if (idx > idxLow)
                    idxHigh = idx;
                else if (paLeaves[idx].uLeaf < uUpToLeaf)
                    return &paLeaves[idx];
                else
                    return NULL;
            }
            else
                return &paLeaves[idx];
        }
    }
    return NULL;
}


static void cpumR3CpuIdInfoRawRange(PCPUMCPUIDINFOSTATEX86 pThis, uint32_t uFromLeaf, uint32_t uUpToLeaf, const char *pszTitle)
{
    PCDBGFINFOHLP const pHlp = pThis->Cmn.pHlp;

    /*
     * Lookup the start leaves.
     */
    PCCPUMCPUIDLEAF pCurLeaf = cpumCpuIdGetFirstLeafInRange(pThis->paLeaves,  pThis->cLeaves,  uFromLeaf, uUpToLeaf);
    PCCPUMCPUIDLEAF pL2      = cpumCpuIdGetFirstLeafInRange(pThis->paLeaves2, pThis->cLeaves2, uFromLeaf, uUpToLeaf);
    if (pCurLeaf || pL2)
    {
        pHlp->pfnPrintf(pHlp,
                        "         %s\n"
                        "     Leaf/sub-leaf  eax      ebx      ecx      edx\n", pszTitle);
        for (;;)
        {
            /* Figure out the current (sub-)leaf. */
            uint32_t uLeaf    = UINT32_MAX;
            uint32_t uSubLeaf = 0;
            if (IS_VALID_LEAF_PTR(pThis, pCurLeaf))
            {
                uLeaf    = pCurLeaf->uLeaf;
                uSubLeaf = pCurLeaf->uSubLeaf;
            }
            else
                pCurLeaf = NULL;
            if (IS_VALID_LEAF2_PTR(pThis, pL2))
            {
                if (pL2->uLeaf < uLeaf)
                {
                    uLeaf = pL2->uLeaf;
                    uSubLeaf = pL2->uSubLeaf;
                }
                else if (pL2->uSubLeaf < uSubLeaf)
                    uSubLeaf = pL2->uSubLeaf;
            }
            else
                pL2 = NULL;
            if (!pCurLeaf && !pL2)
                break;
            if (uLeaf > uUpToLeaf)
                break;

            if (pCurLeaf && pCurLeaf->uLeaf == uLeaf && pCurLeaf->uSubLeaf == uSubLeaf)
            {
                pHlp->pfnPrintf(pHlp, "%s: %08x/%04x  %08x %08x %08x %08x\n", pThis->Cmn.pszShort, uLeaf, uSubLeaf,
                                pCurLeaf->uEax, pCurLeaf->uEbx, pCurLeaf->uEcx, pCurLeaf->uEdx);
                pCurLeaf++;
                if (pL2 && pL2->uLeaf == uLeaf && pL2->uSubLeaf == uSubLeaf)
                {
                    pHlp->pfnPrintf(pHlp, "%s:                %08x %08x %08x %08x\n",
                                    pThis->Cmn.pszShort2, pL2->uEax, pL2->uEbx, pL2->uEcx, pL2->uEdx);
                    pL2++;
                }
            }
            else
            {
                Assert(pL2 && pL2->uLeaf == uLeaf && pL2->uSubLeaf == uSubLeaf);
                pHlp->pfnPrintf(pHlp, "%s: %08x/%04x  %08x %08x %08x %08x\n", pThis->Cmn.pszShort2, uLeaf, uSubLeaf,
                                pL2->uEax, pL2->uEbx, pL2->uEcx, pL2->uEdx);
                pL2++;
            }
        }
    }
}


static PCCPUMCPUIDLEAF cpumR3CpuIdInfoRawSubLeafs(PCPUMCPUIDINFOSTATEX86 pThis, PCCPUMCPUIDLEAF pCurLeaf,
                                                  uint32_t uLeaf, uint32_t cMaxSubLeaves)
{
    PCDBGFINFOHLP const pHlp = pThis->Cmn.pHlp;
    for (uint32_t uSubLeaf = 0; uSubLeaf < cMaxSubLeaves; uSubLeaf++)
    {
        PCCPUMCPUIDLEAF pL2 = cpumR3CpuIdInfoX86Lookup2(pThis, uLeaf, uSubLeaf);
        if (   IS_VALID_LEAF_PTR(pThis, pCurLeaf)
            && pCurLeaf->uLeaf    == uLeaf
            && pCurLeaf->uSubLeaf == uSubLeaf)
        {
            pHlp->pfnPrintf(pHlp, "%s: %08x/%04x  %08x %08x %08x %08x\n", pThis->Cmn.pszShort, uLeaf, uSubLeaf,
                            pCurLeaf->uEax, pCurLeaf->uEbx, pCurLeaf->uEcx, pCurLeaf->uEdx);
            if (pL2)
            {
                pHlp->pfnPrintf(pHlp, "%s:                %08x %08x %08x %08x\n",
                                pThis->Cmn.pszShort2, pL2->uEax, pL2->uEbx, pL2->uEcx, pL2->uEdx);
                pL2++;
            }
            pCurLeaf++;
        }
        else if (pL2)
        {
            pHlp->pfnPrintf(pHlp, "%s: %08x/%04x  %08x %08x %08x %08x\n", pThis->Cmn.pszShort2, uLeaf, uSubLeaf,
                            pL2->uEax, pL2->uEbx, pL2->uEcx, pL2->uEdx);
            pL2++;
        }

        /* Done? */
        if (   (   !IS_VALID_LEAF_PTR(pThis, pCurLeaf)
                || pCurLeaf->uLeaf != uLeaf)
            && (   !IS_VALID_LEAF2_PTR(pThis, pL2)
                || pL2->uLeaf != uLeaf) )
            break;
    }
    return pCurLeaf;
}


/**
 * Display the guest CpuId leaves.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     "terse", "default" or "verbose".
 */
VMMR3DECL(void) CPUMR3CpuIdInfoX86(PCPUMCPUIDINFOSTATEX86 pThis)
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
    AssertReturnVoid(   (pThis->paLeaves2 && pThis->cLeaves2 && pThis->Cmn.pszShort2 && pThis->Cmn.pszLabel2)
                     || (!pThis->paLeaves2 && !pThis->cLeaves2 && !pThis->Cmn.pszShort2 && !pThis->Cmn.pszLabel2));

    uint32_t        uLeaf;
    PCCPUMCPUIDLEAF pCurLeaf;
    PCCPUMCPUIDLEAF pL2;
    bool const      fIntel   = pThis->pFeatures->enmCpuVendor == CPUMCPUVENDOR_INTEL;

    /*
     * Standard leaves.  Custom raw dump here due to ECX sub-leaves host handling.
     */
    uint32_t        cMax2 = pThis->cLeaves2 && pThis->paLeaves2[0].uLeaf == 0 ? pThis->paLeaves2[0].uEax : 0;
    uint32_t        cMax  = pThis->cLeaves  && pThis->paLeaves[0].uLeaf  == 0 ? pThis->paLeaves[0].uEax  : 0;
    cMax = RT_MAX(cMax, cMax2);
    pHlp->pfnPrintf(pHlp,
                    "         Raw Standard CPUID Leaves\n"
                    "     Leaf/sub-leaf  eax      ebx      ecx      edx\n");
    for (uLeaf = 0, pCurLeaf = pThis->paLeaves; uLeaf <= cMax; uLeaf++)
    {
        uint32_t cMaxSubLeaves = 1;
        if (uLeaf == 4 || uLeaf == 7 || uLeaf == 0xb)
            cMaxSubLeaves = 16;
        else if (uLeaf == 0xd)
            cMaxSubLeaves = 128;
        pCurLeaf = cpumR3CpuIdInfoRawSubLeafs(pThis, pCurLeaf, uLeaf, cMaxSubLeaves);
    }

    /*
     * If verbose, decode it.
     */
    if (pThis->Cmn.iVerbosity && (pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x00000000))) != NULL)
        pHlp->pfnPrintf(pHlp,
                        "%36s %.04s%.04s%.04s\n"
                        "%36s 0x00000000-%#010x\n"
                        ,
                        "Name:", &pCurLeaf->uEbx, &pCurLeaf->uEdx, &pCurLeaf->uEcx,
                        "Supports:", pCurLeaf->uEax);

    if (pThis->Cmn.iVerbosity && (pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x00000001))) != NULL)
        cpumR3CpuIdInfoStdLeaf1Details(pThis, pCurLeaf, fIntel);

    if (pThis->Cmn.iVerbosity && (pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x00000007))) != NULL)
        cpumR3CpuIdInfoStdLeaf7Details(pThis, pCurLeaf);

    if (pThis->Cmn.iVerbosity && (pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x0000000d))) != NULL)
        cpumR3CpuIdInfoStdLeaf13Details(pThis, pCurLeaf);

    /*
     * Hypervisor leaves.
     *
     * Unlike most of the other leaves reported, the guest hypervisor leaves
     * aren't a subset of the host CPUID bits.
     */
    cpumR3CpuIdInfoRawRange(pThis, uLeaf, UINT32_C(0x3fffffff), "Unknown CPUID Leaves");

    uLeaf    = UINT32_C(0x40000000);
    pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis,  uLeaf);
    pL2      = cpumR3CpuIdInfoX86Lookup2(pThis, uLeaf);
    cMax2    = pL2      && pL2->uEax      >= UINT32_C(0x40000001) && pL2->uEax      <= UINT32_C(0x40000fff) ? pL2->uEax : 0;
    cMax     = pCurLeaf && pCurLeaf->uEax >= UINT32_C(0x40000001) && pCurLeaf->uEax <= UINT32_C(0x40000fff) ? pCurLeaf->uEax : 0;
    cMax     = RT_MAX(cMax, cMax2);
    if (cMax >= UINT32_C(0x40000000))
    {
        cpumR3CpuIdInfoRawRange(pThis, uLeaf, cMax, "Raw Hypervisor CPUID Leaves");
        /** @todo dump these in more detail. */

        uLeaf = cMax + 1;
    }


    /*
     * Extended.  Custom raw dump here due to ECX sub-leaves host handling.
     * Implemented after AMD specs.
     */
    cpumR3CpuIdInfoRawRange(pThis, uLeaf, UINT32_C(0x7fffffff), "Unknown CPUID Leaves");

    uLeaf    = UINT32_C(0x80000000);
    pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis,  uLeaf);
    pL2      = cpumR3CpuIdInfoX86Lookup2(pThis, uLeaf);
    cMax2    = pL2 && RTX86IsValidExtRange(pL2->uEax) ? RT_MIN(pL2->uEax, UINT32_C(0x80000fff)) : 0;
    cMax     = pCurLeaf ? RT_MIN(pCurLeaf->uEax, UINT32_C(0x80000fff)) : 0;
    cMax     = RT_MAX(cMax, cMax2);
    if (cMax >= uLeaf)
    {
        pHlp->pfnPrintf(pHlp,
                        "         Raw Extended CPUID Leaves\n"
                        "     Leaf/sub-leaf  eax      ebx      ecx      edx\n");
        PCCPUMCPUIDLEAF const pExtLeaf = pCurLeaf;
        for (; uLeaf <= cMax; uLeaf++)
        {
            uint32_t cMaxSubLeaves = 1;
            if (uLeaf == UINT32_C(0x8000001d))
                cMaxSubLeaves = 16;
            pCurLeaf = cpumR3CpuIdInfoRawSubLeafs(pThis, pCurLeaf, uLeaf, cMaxSubLeaves);
        }

        /*
         * Understandable output
         */
        if (pThis->Cmn.iVerbosity)
            pHlp->pfnPrintf(pHlp,
                            "Ext Name:                        %.4s%.4s%.4s\n"
                            "Ext Supports:                    0x80000000-%#010x\n",
                            &pExtLeaf->uEbx, &pExtLeaf->uEdx, &pExtLeaf->uEcx, pExtLeaf->uEax);

        if (pThis->Cmn.iVerbosity && (pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x80000001))) != NULL)
        {
            uint32_t uEAX = pCurLeaf->uEax;
            pHlp->pfnPrintf(pHlp,
                            "Family:                          %d  \tExtended: %d \tEffective: %d\n"
                            "Model:                           %d  \tExtended: %d \tEffective: %d\n"
                            "Stepping:                        %d\n"
                            "Brand ID:                        %#05x\n",
                            (uEAX >> 8) & 0xf, (uEAX >> 20) & 0x7f, RTX86GetCpuFamily(uEAX),
                            (uEAX >> 4) & 0xf, (uEAX >> 16) & 0x0f, RTX86GetCpuModel(uEAX, fIntel),
                            RTX86GetCpuStepping(uEAX),
                            pCurLeaf->uEbx & 0xfff);

            if (pThis->Cmn.iVerbosity == 1)
            {
                cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEdx, g_aExtLeaf1EdxSubFields, "Ext Features EDX", 34);
                cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEcx, g_aExtLeaf1EdxSubFields, "Ext Features ECX", 34);
            }
            else
            {
                pL2 = cpumR3CpuIdInfoX86Lookup2(pThis, UINT32_C(0x80000001));
                cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEdx, pL2 ? pL2->uEdx : 0, g_aExtLeaf1EdxSubFields, "Ext Features", 56);
                cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEcx, pL2 ? pL2->uEcx : 0, g_aExtLeaf1EcxSubFields, NULL, 56);
                if (   (pCurLeaf->uEcx & X86_CPUID_AMD_FEATURE_ECX_SVM)
                    || (pL2 && (pL2->uEcx & X86_CPUID_AMD_FEATURE_ECX_SVM)))
                {
                    pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis,  UINT32_C(0x8000000a));
                    pL2      = cpumR3CpuIdInfoX86Lookup2(pThis, UINT32_C(0x8000000a));
                    cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf ? pCurLeaf->uEdx : 0, pL2 ? pL2->uEdx : 0,
                                                         g_aExtLeafAEdxSubFields, "SVM Feature Identification (leaf A)", 56);
                }
            }
        }

        if (pThis->Cmn.iVerbosity && (pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x80000002))) != NULL)
        {
            char szString[4*4*3+1] = {0};
            uint32_t *pu32 = (uint32_t *)szString;
            *pu32++ = pCurLeaf->uEax;
            *pu32++ = pCurLeaf->uEbx;
            *pu32++ = pCurLeaf->uEcx;
            *pu32++ = pCurLeaf->uEdx;
            pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x80000003));
            if (pCurLeaf)
            {
                *pu32++ = pCurLeaf->uEax;
                *pu32++ = pCurLeaf->uEbx;
                *pu32++ = pCurLeaf->uEcx;
                *pu32++ = pCurLeaf->uEdx;
            }
            pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x80000004));
            if (pCurLeaf)
            {
                *pu32++ = pCurLeaf->uEax;
                *pu32++ = pCurLeaf->uEbx;
                *pu32++ = pCurLeaf->uEcx;
                *pu32++ = pCurLeaf->uEdx;
            }
            pHlp->pfnPrintf(pHlp, "Full Name:                       \"%s\"\n", szString);
        }

        if (pThis->Cmn.iVerbosity && (pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x80000005))) != NULL)
        {
            uint32_t uEAX = pCurLeaf->uEax;
            uint32_t uEBX = pCurLeaf->uEbx;
            uint32_t uECX = pCurLeaf->uEcx;
            uint32_t uEDX = pCurLeaf->uEdx;
            char sz1[32];
            char sz2[32];

            pHlp->pfnPrintf(pHlp,
                            "TLB 2/4M Instr/Uni:              %s %3d entries\n"
                            "TLB 2/4M Data:                   %s %3d entries\n",
                            getCacheAss((uEAX >>  8) & 0xff, sz1), (uEAX >>  0) & 0xff,
                            getCacheAss((uEAX >> 24) & 0xff, sz2), (uEAX >> 16) & 0xff);
            pHlp->pfnPrintf(pHlp,
                            "TLB 4K Instr/Uni:                %s %3d entries\n"
                            "TLB 4K Data:                     %s %3d entries\n",
                            getCacheAss((uEBX >>  8) & 0xff, sz1), (uEBX >>  0) & 0xff,
                            getCacheAss((uEBX >> 24) & 0xff, sz2), (uEBX >> 16) & 0xff);
            pHlp->pfnPrintf(pHlp, "L1 Instr Cache Line Size:        %d bytes\n"
                            "L1 Instr Cache Lines Per Tag:    %d\n"
                            "L1 Instr Cache Associativity:    %s\n"
                            "L1 Instr Cache Size:             %d KB\n",
                            (uEDX >> 0) & 0xff,
                            (uEDX >> 8) & 0xff,
                            getCacheAss((uEDX >> 16) & 0xff, sz1),
                            (uEDX >> 24) & 0xff);
            pHlp->pfnPrintf(pHlp,
                            "L1 Data Cache Line Size:         %d bytes\n"
                            "L1 Data Cache Lines Per Tag:     %d\n"
                            "L1 Data Cache Associativity:     %s\n"
                            "L1 Data Cache Size:              %d KB\n",
                            (uECX >> 0) & 0xff,
                            (uECX >> 8) & 0xff,
                            getCacheAss((uECX >> 16) & 0xff, sz1),
                            (uECX >> 24) & 0xff);
        }

        if (pThis->Cmn.iVerbosity && (pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x80000006))) != NULL)
        {
            uint32_t uEAX = pCurLeaf->uEax;
            uint32_t uEBX = pCurLeaf->uEbx;
            uint32_t uECX = pCurLeaf->uEcx;
            uint32_t uEDX = pCurLeaf->uEdx;

            pHlp->pfnPrintf(pHlp,
                            "L2 TLB 2/4M Instr/Uni:           %s %4d entries\n"
                            "L2 TLB 2/4M Data:                %s %4d entries\n",
                            getL23CacheAss((uEAX >> 12) & 0xf), (uEAX >>  0) & 0xfff,
                            getL23CacheAss((uEAX >> 28) & 0xf), (uEAX >> 16) & 0xfff);
            pHlp->pfnPrintf(pHlp,
                            "L2 TLB 4K Instr/Uni:             %s %4d entries\n"
                            "L2 TLB 4K Data:                  %s %4d entries\n",
                            getL23CacheAss((uEBX >> 12) & 0xf), (uEBX >>  0) & 0xfff,
                            getL23CacheAss((uEBX >> 28) & 0xf), (uEBX >> 16) & 0xfff);
            pHlp->pfnPrintf(pHlp,
                            "L2 Cache Line Size:              %d bytes\n"
                            "L2 Cache Lines Per Tag:          %d\n"
                            "L2 Cache Associativity:          %s\n"
                            "L2 Cache Size:                   %d KB\n",
                            (uECX >> 0) & 0xff,
                            (uECX >> 8) & 0xf,
                            getL23CacheAss((uECX >> 12) & 0xf),
                            (uECX >> 16) & 0xffff);
            pHlp->pfnPrintf(pHlp,
                            "L3 Cache Line Size:              %d bytes\n"
                            "L3 Cache Lines Per Tag:          %d\n"
                            "L3 Cache Associativity:          %s\n"
                            "L3 Cache Size:                   %d KB\n",
                            (uEDX >> 0) & 0xff,
                            (uEDX >> 8) & 0xf,
                            getL23CacheAss((uEDX >> 12) & 0xf),
                            ((uEDX >> 18) & 0x3fff) * 512);
        }

        if ((pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x80000007))) != NULL)
        {
            pL2 = cpumR3CpuIdInfoX86Lookup2(pThis, UINT32_C(0x80000007));
            if (pCurLeaf->uEdx || (pL2 && pL2->uEdx && pThis->Cmn.iVerbosity))
            {
                if (pThis->Cmn.iVerbosity < 1)
                    cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEdx, g_aExtLeaf7EdxSubFields, "APM Features EDX", 34);
                else
                    cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEdx, pL2 ? pL2->uEdx : 0, g_aExtLeaf7EdxSubFields,
                                                         "APM Features EDX", 56);
            }
        }

        pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0x80000008));
        if (pCurLeaf != NULL)
        {
            pL2 = cpumR3CpuIdInfoX86Lookup2(pThis, UINT32_C(0x80000008));
            if (pCurLeaf->uEbx || (pL2 && pL2->uEbx && pThis->Cmn.iVerbosity))
            {
                if (pThis->Cmn.iVerbosity < 1)
                    cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEbx, g_aExtLeaf8EbxSubFields, "Ext Features ext IDs EBX", 34);
                else
                    cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEbx, pL2 ? pL2->uEbx : 0, g_aExtLeaf8EbxSubFields,
                                                         "Ext Features ext IDs EBX", 56);
            }

            if (pThis->Cmn.iVerbosity)
            {
                uint32_t const uEAX = pCurLeaf->uEax;
                pHlp->pfnPrintf(pHlp,
                                "Physical Address Width:          %d bits\n"
                                "Virtual Address Width:           %d bits\n",
                                (uEAX >> 0) & 0xff,
                                (uEAX >> 8) & 0xff);
                if (   ((uEAX >> 16) & 0xff) != 0
                    || pThis->pFeatures->enmCpuVendor == CPUMCPUVENDOR_AMD
                    || pThis->pFeatures->enmCpuVendor == CPUMCPUVENDOR_HYGON)
                    pHlp->pfnPrintf(pHlp, "Guest Physical Address Width:    %d bits%s\n",
                                    (uEAX >> 16) & 0xff ? (uEAX >> 16) & 0xff : (uEAX >> 0) & 0xff,
                                    (uEAX >> 16) & 0xff ? "" : " (0)");

                uint32_t const uECX = pCurLeaf->uEcx;
                if (   ((uECX >> 0) & 0xff) != 0
                    || pThis->pFeatures->enmCpuVendor == CPUMCPUVENDOR_AMD
                    || pThis->pFeatures->enmCpuVendor == CPUMCPUVENDOR_HYGON)
                {
                    uint32_t const cPhysCoreCount = ((uECX >> 0) & 0xff) + 1;
                    uint32_t const cApicIdSize    = (uECX >> 12) & 0xf ? RT_BIT_32((uECX >> 12) & 0xf) : cPhysCoreCount;
                    pHlp->pfnPrintf(pHlp,
                                    "Physical Core Count:             %d\n"
                                    "APIC ID size:                    %u (%#x)\n"
                                    "Performance TSC size:            %u bits\n",
                                    cPhysCoreCount,
                                    cApicIdSize, cApicIdSize,
                                    (((uECX >> 16) & 0x3) << 3) + 40);
                }
                uint32_t const uEDX = pCurLeaf->uEax;
                if (uEDX)
                    pHlp->pfnPrintf(pHlp,
                                    "Max page count for INVLPGB:      %#x\n"
                                    "Max ECX for RDPRU:               %#x\n",
                                    (uEDX & 0xffff), uEDX >> 16);
            }
        }
    }


    /*
     * Centaur.
     */
    cpumR3CpuIdInfoRawRange(pThis, uLeaf, UINT32_C(0xbfffffff), "Unknown CPUID Leaves");

    uLeaf = UINT32_C(0xc0000000);
    pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis,  uLeaf);
    pL2      = cpumR3CpuIdInfoX86Lookup2(pThis, uLeaf);
    cMax2    = pL2 && pL2->uEax >= UINT32_C(0xc0000001) && pL2->uEax <= UINT32_C(0xc0000fff) ? pL2->uEax : 0;
    cMax     = pCurLeaf ? RT_MIN(pCurLeaf->uEax, UINT32_C(0xc0000fff)) : 0;
    cMax     = RT_MAX(cMax, cMax2);
    if (cMax >= UINT32_C(0xc0000000))
    {
        cpumR3CpuIdInfoRawRange(pThis, uLeaf, cMax, "Raw Centaur CPUID Leaves");

        /*
         * Understandable output
         */
        if (pThis->Cmn.iVerbosity && (pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0xc0000000))) != NULL)
            pHlp->pfnPrintf(pHlp,
                            "Centaur Supports:                0xc0000000-%#010x\n",
                            pCurLeaf->uEax);

        if (pThis->Cmn.iVerbosity && (pCurLeaf = cpumR3CpuIdInfoX86Lookup(pThis, UINT32_C(0xc0000001))) != NULL)
        {
            pL2 = cpumR3CpuIdInfoX86Lookup2(pThis, UINT32_C(0xc0000001));
            if (pCurLeaf->uEdx || (pL2 && pL2->uEdx && pThis->Cmn.iVerbosity))
            {
                if (pThis->Cmn.iVerbosity < 1)
                    cpumR3CpuIdInfoMnemonicListU32(&pThis->Cmn, pCurLeaf->uEdx, g_aViaLeaf1EdxSubFields, "Centaur Features EDX", 34);
                else
                    cpumR3CpuIdInfoVerboseCompareListU32(&pThis->Cmn, pCurLeaf->uEdx, pL2 ? pL2->uEdx : 0, g_aViaLeaf1EdxSubFields,
                                                         "Centaur Features EDX", 56);
            }
        }

        uLeaf = cMax + 1;
    }

    /*
     * The remainder.
     */
    cpumR3CpuIdInfoRawRange(pThis, uLeaf, UINT32_C(0xfffffffe), "Unknown CPUID Leaves");
}

#endif /* defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) || defined(VBOX_VMM_TARGET_X86) */

