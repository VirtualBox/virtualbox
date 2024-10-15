/* $Id: VBoxCpuReport.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxCpuReport - Produces the basis for a CPU DB entry.
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
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/symlink.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include <VBox/err.h>
#include <VBox/vmm/cpum.h>
#include <VBox/sup.h>
#include <VBox/version.h>

#include "VBoxCpuReport.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Write only register. */
#define VBCPUREPMSR_F_WRITE_ONLY      RT_BIT(0)

typedef struct VBCPUREPMSR
{
    /** The first MSR register number. */
    uint32_t        uMsr;
    /** Flags (MSRREPORT_F_XXX). */
    uint32_t        fFlags;
    /** The value we read, unless write-only.  */
    uint64_t        uValue;
} VBCPUREPMSR;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The CPU vendor.  Used by the MSR code. */
static CPUMCPUVENDOR    g_enmVendor = CPUMCPUVENDOR_INVALID;
/** The CPU microarchitecture.  Used by the MSR code. */
static CPUMMICROARCH    g_enmMicroarch = kCpumMicroarch_Invalid;
/** Set if g_enmMicroarch indicates an Intel NetBurst CPU. */
static bool             g_fIntelNetBurst = false;
/** The alternative report stream. */
static PRTSTREAM        g_pReportOut;
/** The alternative debug stream. */
static PRTSTREAM        g_pDebugOut;
/** Whether to skip MSR collection.   */
static bool             g_fNoMsrs = false;

/** Snooping info storage for vbCpuRepGuessScalableBusFrequencyName. */
static uint64_t         g_uMsrIntelP6FsbFrequency = UINT64_MAX;

/** The MSR accessors interface. */
static VBCPUREPMSRACCESSORS g_MsrAcc;



void vbCpuRepDebug(const char *pszMsg, ...)
{
    va_list va;

    /* Always print a copy of the report to standard error. */
    va_start(va, pszMsg);
    RTStrmPrintfV(g_pStdErr, pszMsg, va);
    va_end(va);
    RTStrmFlush(g_pStdErr);

    /* Alternatively, also print to a log file. */
    if (g_pDebugOut)
    {
        va_start(va, pszMsg);
        RTStrmPrintfV(g_pDebugOut, pszMsg, va);
        va_end(va);
        RTStrmFlush(g_pDebugOut);
    }

    /* Give the output device a chance to write / display it. */
    RTThreadSleep(1);
}


void vbCpuRepPrintf(const char *pszMsg, ...)
{
    va_list va;

    /* Output to report file, if requested. */
    if (g_pReportOut)
    {
        va_start(va, pszMsg);
        RTStrmPrintfV(g_pReportOut, pszMsg, va);
        va_end(va);
        RTStrmFlush(g_pReportOut);
    }

    /* Always print a copy of the report to standard out. */
    va_start(va, pszMsg);
    RTStrmPrintfV(g_pStdOut, pszMsg, va);
    va_end(va);
    RTStrmFlush(g_pStdOut);
}



static int vbCpuRepMsrsAddOne(VBCPUREPMSR **ppaMsrs, uint32_t *pcMsrs,
                              uint32_t uMsr, uint64_t uValue, uint32_t fFlags)
{
    /*
     * Grow the array?
     */
    uint32_t cMsrs = *pcMsrs;
    if ((cMsrs % 64) == 0)
    {
        void *pvNew = RTMemRealloc(*ppaMsrs, (cMsrs + 64) * sizeof(**ppaMsrs));
        if (!pvNew)
        {
            RTMemFree(*ppaMsrs);
            *ppaMsrs = NULL;
            *pcMsrs  = 0;
            return VERR_NO_MEMORY;
        }
        *ppaMsrs = (VBCPUREPMSR *)pvNew;
    }

    /*
     * Add it.
     */
    VBCPUREPMSR *pEntry = *ppaMsrs + cMsrs;
    pEntry->uMsr   = uMsr;
    pEntry->fFlags = fFlags;
    pEntry->uValue = uValue;
    *pcMsrs = cMsrs + 1;

    return VINF_SUCCESS;
}


/**
 * Returns the max physical address width as a number of bits.
 *
 * @returns Bit count.
 */
static uint8_t vbCpuRepGetPhysAddrWidth(void)
{
    uint8_t  cMaxWidth;
    if (!ASMHasCpuId())
        cMaxWidth = 32;
    else
    {
        uint32_t cMaxExt = ASMCpuId_EAX(0x80000000);
        if (RTX86IsValidExtRange(cMaxExt)&& cMaxExt >= 0x80000008)
            cMaxWidth = ASMCpuId_EAX(0x80000008) & 0xff;
        else if (   RTX86IsValidStdRange(ASMCpuId_EAX(0))
                 && (ASMCpuId_EDX(1) & X86_CPUID_FEATURE_EDX_PSE36))
            cMaxWidth = 36;
        else
            cMaxWidth = 32;
    }
    return cMaxWidth;
}


static bool vbCpuRepSupportsPae(void)
{
    return ASMHasCpuId()
        && RTX86IsValidStdRange(ASMCpuId_EAX(0))
        && (ASMCpuId_EDX(1) & X86_CPUID_FEATURE_EDX_PAE);
}


static bool vbCpuRepSupportsLongMode(void)
{
    return ASMHasCpuId()
        && RTX86IsValidExtRange(ASMCpuId_EAX(0x80000000))
        && (ASMCpuId_EDX(0x80000001) & X86_CPUID_EXT_FEATURE_EDX_LONG_MODE);
}


static bool vbCpuRepSupportsNX(void)
{
    return ASMHasCpuId()
        && RTX86IsValidExtRange(ASMCpuId_EAX(0x80000000))
        && (ASMCpuId_EDX(0x80000001) & X86_CPUID_EXT_FEATURE_EDX_NX);
}


static bool vbCpuRepSupportsX2Apic(void)
{
    return ASMHasCpuId()
        && RTX86IsValidStdRange(ASMCpuId_EAX(0))
        && (ASMCpuId_ECX(1) & X86_CPUID_FEATURE_ECX_X2APIC);
}



#if 0 /* unused */
static bool msrProberWrite(uint32_t uMsr, uint64_t uValue)
{
    bool fGp;
    int rc = g_MsrAcc.pfnMsrWrite(uMsr, NIL_RTCPUID, uValue, &fGp);
    AssertRC(rc);
    return RT_SUCCESS(rc) && !fGp;
}
#endif


static bool msrProberRead(uint32_t uMsr, uint64_t *puValue)
{
    *puValue = 0;
    bool fGp;
    int rc = g_MsrAcc.pfnMsrProberRead(uMsr, NIL_RTCPUID, puValue, &fGp);
    AssertRC(rc);
    return RT_SUCCESS(rc) && !fGp;
}


/** Tries to modify the register by writing the original value to it. */
static bool msrProberModifyNoChange(uint32_t uMsr)
{
    SUPMSRPROBERMODIFYRESULT Result;
    int rc = g_MsrAcc.pfnMsrProberModify(uMsr, NIL_RTCPUID, UINT64_MAX, 0, &Result);
    return RT_SUCCESS(rc)
        && !Result.fBeforeGp
        && !Result.fModifyGp
        && !Result.fAfterGp
        && !Result.fRestoreGp;
}


/** Tries to modify the register by writing zero to it. */
static bool msrProberModifyZero(uint32_t uMsr)
{
    SUPMSRPROBERMODIFYRESULT Result;
    int rc = g_MsrAcc.pfnMsrProberModify(uMsr, NIL_RTCPUID, 0, 0, &Result);
    return RT_SUCCESS(rc)
        && !Result.fBeforeGp
        && !Result.fModifyGp
        && !Result.fAfterGp
        && !Result.fRestoreGp;
}


/**
 * Tries to modify each bit in the MSR and see if we can make it change.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR.
 * @param   pfIgnMask           The ignore mask to update.
 * @param   pfGpMask            The GP mask to update.
 * @param   fSkipMask           Mask of bits to skip.
 */
static int msrProberModifyBitChanges(uint32_t uMsr, uint64_t *pfIgnMask, uint64_t *pfGpMask, uint64_t fSkipMask)
{
    for (unsigned iBit = 0; iBit < 64; iBit++)
    {
        uint64_t fBitMask = RT_BIT_64(iBit);
        if (fBitMask & fSkipMask)
            continue;

        /* Set it. */
        SUPMSRPROBERMODIFYRESULT ResultSet;
        int rc = g_MsrAcc.pfnMsrProberModify(uMsr, NIL_RTCPUID, ~fBitMask, fBitMask, &ResultSet);
        if (RT_FAILURE(rc))
            return RTMsgErrorRc(rc, "pfnMsrProberModify(%#x,,%#llx,%#llx,): %Rrc", uMsr, ~fBitMask, fBitMask, rc);

        /* Clear it. */
        SUPMSRPROBERMODIFYRESULT ResultClear;
        rc = g_MsrAcc.pfnMsrProberModify(uMsr, NIL_RTCPUID, ~fBitMask, 0, &ResultClear);
        if (RT_FAILURE(rc))
            return RTMsgErrorRc(rc, "pfnMsrProberModify(%#x,,%#llx,%#llx,): %Rrc", uMsr, ~fBitMask, 0, rc);

        if (ResultSet.fModifyGp || ResultClear.fModifyGp)
            *pfGpMask |= fBitMask;
        else if (   (   ((ResultSet.uBefore   ^ ResultSet.uAfter)   & fBitMask) == 0
                     && !ResultSet.fBeforeGp
                     && !ResultSet.fAfterGp)
                 && (   ((ResultClear.uBefore ^ ResultClear.uAfter) & fBitMask) == 0
                     && !ResultClear.fBeforeGp
                     && !ResultClear.fAfterGp) )
            *pfIgnMask |= fBitMask;
    }

    return VINF_SUCCESS;
}


#if 0 /* currently unused */
/**
 * Tries to modify one bit.
 *
 * @retval  -2 on API error.
 * @retval  -1 on \#GP.
 * @retval  0 if ignored.
 * @retval  1 if it changed.
 *
 * @param   uMsr                The MSR.
 * @param   iBit                The bit to try modify.
 */
static int msrProberModifyBit(uint32_t uMsr, unsigned iBit)
{
    uint64_t fBitMask = RT_BIT_64(iBit);

    /* Set it. */
    SUPMSRPROBERMODIFYRESULT ResultSet;
    int rc = g_MsrAcc.pfnMsrProberModify(uMsr, NIL_RTCPUID, ~fBitMask, fBitMask, &ResultSet);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(-2, "pfnMsrProberModify(%#x,,%#llx,%#llx,): %Rrc", uMsr, ~fBitMask, fBitMask, rc);

    /* Clear it. */
    SUPMSRPROBERMODIFYRESULT ResultClear;
    rc = g_MsrAcc.pfnMsrProberModify(uMsr, NIL_RTCPUID, ~fBitMask, 0, &ResultClear);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(-2, "pfnMsrProberModify(%#x,,%#llx,%#llx,): %Rrc", uMsr, ~fBitMask, 0, rc);

    if (ResultSet.fModifyGp || ResultClear.fModifyGp)
        return -1;

    if (   (   ((ResultSet.uBefore   ^ ResultSet.uAfter)   & fBitMask) != 0
            && !ResultSet.fBeforeGp
            && !ResultSet.fAfterGp)
        || (   ((ResultClear.uBefore ^ ResultClear.uAfter) & fBitMask) != 0
            && !ResultClear.fBeforeGp
            && !ResultClear.fAfterGp) )
        return 1;

    return 0;
}
#endif


/**
 * Tries to do a simple AND+OR change and see if we \#GP or not.
 *
 * @retval  @c true if successfully modified.
 * @retval  @c false if \#GP or other error.
 *
 * @param   uMsr                The MSR.
 * @param   fAndMask            The AND mask.
 * @param   fOrMask             The OR mask.
 */
static bool msrProberModifySimpleGp(uint32_t uMsr, uint64_t fAndMask, uint64_t fOrMask)
{
    SUPMSRPROBERMODIFYRESULT Result;
    int rc = g_MsrAcc.pfnMsrProberModify(uMsr, NIL_RTCPUID, fAndMask, fOrMask, &Result);
    if (RT_FAILURE(rc))
    {
        RTMsgError("g_MsrAcc.pfnMsrProberModify(%#x,,%#llx,%#llx,): %Rrc", uMsr, fAndMask, fOrMask, rc);
        return false;
    }
    return !Result.fBeforeGp
        && !Result.fModifyGp
        && !Result.fAfterGp
        && !Result.fRestoreGp;
}




/**
 * Combination of the basic tests.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR.
 * @param   fSkipMask           Mask of bits to skip.
 * @param   pfReadOnly          Where to return read-only status.
 * @param   pfIgnMask           Where to return the write ignore mask.  Need not
 *                              be initialized.
 * @param   pfGpMask            Where to return the write GP mask.  Need not
 *                              be initialized.
 */
static int msrProberModifyBasicTests(uint32_t uMsr, uint64_t fSkipMask, bool *pfReadOnly, uint64_t *pfIgnMask, uint64_t *pfGpMask)
{
    if (msrProberModifyNoChange(uMsr))
    {
        *pfReadOnly = false;
        *pfIgnMask  = 0;
        *pfGpMask  = 0;
        return msrProberModifyBitChanges(uMsr, pfIgnMask, pfGpMask, fSkipMask);
    }

    *pfReadOnly = true;
    *pfIgnMask  = 0;
    *pfGpMask   = UINT64_MAX;
    return VINF_SUCCESS;
}



/**
 * Determines for the MSR AND mask.
 *
 * Older CPUs doesn't necessiarly implement all bits of the MSR register number.
 * So, we have to approximate how many are used so we don't get an overly large
 * and confusing set of MSRs when probing.
 *
 * @returns The mask.
 */
static uint32_t determineMsrAndMask(void)
{
#define VBCPUREP_MASK_TEST_MSRS     7
    static uint32_t const s_aMsrs[VBCPUREP_MASK_TEST_MSRS] =
    {
        /* Try a bunch of mostly read only registers: */
        MSR_P5_MC_TYPE, MSR_IA32_PLATFORM_ID, MSR_IA32_MTRR_CAP, MSR_IA32_MCG_CAP, MSR_IA32_CR_PAT,
        /* Then some which aren't supposed to be present on any CPU: */
        0x00000015, 0x00000019,
    };

    /* Get the base values. */
    uint64_t auBaseValues[VBCPUREP_MASK_TEST_MSRS];
    for (unsigned i = 0; i < RT_ELEMENTS(s_aMsrs); i++)
    {
        if (!msrProberRead(s_aMsrs[i], &auBaseValues[i]))
            auBaseValues[i] = UINT64_MAX;
        //vbCpuRepDebug("Base: %#x -> %#llx\n", s_aMsrs[i], auBaseValues[i]);
    }

    /* Do the probing. */
    unsigned iBit;
    for (iBit = 31; iBit > 8; iBit--)
    {
        uint64_t fMsrOrMask = RT_BIT_64(iBit);
        for (unsigned iTest = 0; iTest <= 64 && fMsrOrMask < UINT32_MAX; iTest++)
        {
            for (unsigned i = 0; i < RT_ELEMENTS(s_aMsrs); i++)
            {
                uint64_t uValue;
                if (!msrProberRead(s_aMsrs[i] | fMsrOrMask, &uValue))
                    uValue = UINT64_MAX;
                if (uValue != auBaseValues[i])
                {
                    uint32_t fMsrMask = iBit >= 31 ? UINT32_MAX : RT_BIT_32(iBit + 1) - 1;
                    vbCpuRepDebug("MSR AND mask: quit on iBit=%u uMsr=%#x (%#x) %llx != %llx => fMsrMask=%#x\n",
                                  iBit, s_aMsrs[i] | (uint32_t)fMsrOrMask, s_aMsrs[i], uValue, auBaseValues[i], fMsrMask);
                    return fMsrMask;
                }
            }

            /* Advance. */
            if (iBit <= 6)
                fMsrOrMask += RT_BIT_64(iBit);
            else if (iBit <= 11)
                fMsrOrMask += RT_BIT_64(iBit) * 33;
            else if (iBit <= 16)
                fMsrOrMask += RT_BIT_64(iBit) * 1025;
            else if (iBit <= 22)
                fMsrOrMask += RT_BIT_64(iBit) * 65537;
            else
                fMsrOrMask += RT_BIT_64(iBit) * 262145;
        }
    }

    uint32_t fMsrMask = RT_BIT_32(iBit + 1) - 1;
    vbCpuRepDebug("MSR AND mask: less that %u bits that matters?!? => fMsrMask=%#x\n", iBit + 1, fMsrMask);
    return fMsrMask;
}


static int findMsrs(VBCPUREPMSR **ppaMsrs, uint32_t *pcMsrs, uint32_t fMsrMask)
{
    /*
     * Gather them.
     */
    static struct { uint32_t uFirst, cMsrs; } const s_aRanges[] =
    {
        { 0x00000000, 0x00042000 },
        { 0x10000000, 0x00001000 },
        { 0x20000000, 0x00001000 },
        { 0x40000000, 0x00012000 },
        { 0x80000000, 0x00012000 },
        { 0xc0000000, 0x00022000 }, /* Had some trouble here on solaris with the tstVMM setup. */
    };

    *pcMsrs  = 0;
    *ppaMsrs = NULL;

    for (unsigned i = 0; i < RT_ELEMENTS(s_aRanges); i++)
    {
        uint32_t uMsr  = s_aRanges[i].uFirst;
        if ((uMsr & fMsrMask) != uMsr)
            continue;
        uint32_t cLeft = s_aRanges[i].cMsrs;
        while (cLeft-- > 0 && (uMsr & fMsrMask) == uMsr)
        {
            if ((uMsr & 0xfff) == 0)
            {
                vbCpuRepDebug("testing %#x...\n", uMsr);
                RTThreadSleep(22);
            }
#if 0
            else if (uMsr >= 0x00003170 && uMsr <= 0xc0000090)
            {
                vbCpuRepDebug("testing %#x...\n", uMsr);
                RTThreadSleep(250);
            }
#endif
            /* Skip 0xc0011012..13 as it seems to be bad for our health (Phenom II X6 1100T). */
            /* Ditto for 0x0000002ff (MSR_IA32_MTRR_DEF_TYPE) on AMD (Ryzen 7 1800X). */
            /* Ditto for 0x0000002a (EBL_CR_POWERON) and 0x00000277 (MSR_IA32_CR_PAT) on Intel (Atom 330). */
            /* And more of the same for 0x280 on Intel Pentium III. */
            if (   ((uMsr >= 0xc0011012 && uMsr <= 0xc0011013) && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
                || (   uMsr == 0x2ff
                    && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON)
                    && g_enmMicroarch >= kCpumMicroarch_AMD_Zen_First)
                || (   (uMsr == 0x2a || uMsr == 0x277)
                    && g_enmVendor == CPUMCPUVENDOR_INTEL
                    && g_enmMicroarch == kCpumMicroarch_Intel_Atom_Bonnell)
                || (   (uMsr == 0x280)
                    && g_enmMicroarch == kCpumMicroarch_Intel_P6_III))
                vbCpuRepDebug("Skipping %#x\n", uMsr);
            else
            {
                /* Read probing normally does it. */
                uint64_t uValue = 0;
                bool     fGp    = true;
                int rc = g_MsrAcc.pfnMsrProberRead(uMsr, NIL_RTCPUID, &uValue, &fGp);
                if (RT_FAILURE(rc))
                {
                    RTMemFree(*ppaMsrs);
                    *ppaMsrs = NULL;
                    return RTMsgErrorRc(rc, "pfnMsrProberRead failed on %#x: %Rrc\n", uMsr, rc);
                }

                uint32_t fFlags;
                if (!fGp)
                    fFlags = 0;
                /* VIA/Shanghai HACK - writing to 0x0000317e on a quad core make the core unresponsive. */
                else if (uMsr == 0x0000317e && (g_enmVendor == CPUMCPUVENDOR_VIA || g_enmVendor == CPUMCPUVENDOR_SHANGHAI))
                {
                    uValue = 0;
                    fFlags = VBCPUREPMSR_F_WRITE_ONLY;
                    fGp    = *pcMsrs == 0
                          || (*ppaMsrs)[*pcMsrs - 1].uMsr != 0x0000317d
                          || (*ppaMsrs)[*pcMsrs - 1].fFlags != VBCPUREPMSR_F_WRITE_ONLY;
                }
                else
                {
                    /* Is it a write only register? */
#if 0
                    if (uMsr >= 0x00003170 && uMsr <= 0xc0000090)
                    {
                        vbCpuRepDebug("test writing %#x...\n", uMsr);
                        RTThreadSleep(250);
                    }
#endif
                    fGp = true;
                    rc = g_MsrAcc.pfnMsrProberWrite(uMsr, NIL_RTCPUID, 0, &fGp);
                    if (RT_FAILURE(rc))
                    {
                        RTMemFree(*ppaMsrs);
                        *ppaMsrs = NULL;
                        return RTMsgErrorRc(rc, "pfnMsrProberWrite failed on %#x: %Rrc\n", uMsr, rc);
                    }
                    uValue = 0;
                    fFlags = VBCPUREPMSR_F_WRITE_ONLY;

                    /*
                     * Tweaks.  On Intel CPUs we've got trouble detecting
                     * IA32_BIOS_UPDT_TRIG (0x00000079), so we have to add it manually here.
                     * Ditto on AMD with PATCH_LOADER (0xc0010020).
                     */
                    if (   uMsr == 0x00000079
                        && fGp
                        && g_enmMicroarch >= kCpumMicroarch_Intel_P6_Core_Atom_First
                        && g_enmMicroarch <= kCpumMicroarch_Intel_End)
                        fGp = false;
                    if (   uMsr == 0xc0010020
                        && fGp
                        && g_enmMicroarch >= kCpumMicroarch_AMD_K8_First
                        && g_enmMicroarch <= kCpumMicroarch_AMD_End)
                        fGp = false;
                }

                if (!fGp)
                {
                    /* Add it. */
                    rc = vbCpuRepMsrsAddOne(ppaMsrs, pcMsrs, uMsr, uValue, fFlags);
                    if (RT_FAILURE(rc))
                        return RTMsgErrorRc(rc, "Out of memory (uMsr=%#x).\n", uMsr);
                    if (   (g_enmVendor != CPUMCPUVENDOR_VIA && g_enmVendor != CPUMCPUVENDOR_SHANGHAI)
                        || uValue
                        || fFlags)
                        vbCpuRepDebug("%#010x: uValue=%#llx fFlags=%#x\n", uMsr, uValue, fFlags);
                }
            }

            uMsr++;
        }
    }

    return VINF_SUCCESS;
}

/**
 * Get the name of the specified MSR, if we know it and can handle it.
 *
 * Do _NOT_ add any new names here without ALSO at the SAME TIME making sure it
 * is handled correctly by the PROBING CODE and REPORTED correctly!!
 *
 * @returns Pointer to name if handled, NULL if not yet explored.
 * @param   uMsr                The MSR in question.
 */
static const char *getMsrNameHandled(uint32_t uMsr)
{
    /** @todo figure out where NCU_EVENT_CORE_MASK might be... */
    switch (uMsr)
    {
        case 0x00000000: return "IA32_P5_MC_ADDR";
        case 0x00000001: return "IA32_P5_MC_TYPE";
        case 0x00000006:
            if (g_enmMicroarch >= kCpumMicroarch_Intel_First && g_enmMicroarch <= kCpumMicroarch_Intel_P6_Core_Atom_First)
                return NULL; /* TR4 / cache tag on Pentium, but that's for later. */
            return "IA32_MONITOR_FILTER_LINE_SIZE";
        //case 0x0000000e: return "P?_TR12"; /* K6-III docs */
        case 0x00000010: return "IA32_TIME_STAMP_COUNTER";
        case 0x00000017: return "IA32_PLATFORM_ID";
        case 0x00000018: return "P6_UNK_0000_0018"; /* P6_M_Dothan. */
        case 0x0000001b: return "IA32_APIC_BASE";
        case 0x00000021: return "C2_UNK_0000_0021"; /* Core2_Penryn */
        case 0x0000002a: return g_fIntelNetBurst ? "P4_EBC_HARD_POWERON" : "EBL_CR_POWERON";
        case 0x0000002b: return g_fIntelNetBurst ? "P4_EBC_SOFT_POWERON" : NULL;
        case 0x0000002c: return g_fIntelNetBurst ? "P4_EBC_FREQUENCY_ID" : NULL;
        case 0x0000002e: return "I7_UNK_0000_002e"; /* SandyBridge, IvyBridge. */
        case 0x0000002f: return "P6_UNK_0000_002f"; /* P6_M_Dothan. */
        case 0x00000032: return "P6_UNK_0000_0032"; /* P6_M_Dothan. */
        case 0x00000033: return "TEST_CTL";
        case 0x00000034: return CPUMMICROARCH_IS_INTEL_CORE7(g_enmMicroarch)
                             || CPUMMICROARCH_IS_INTEL_SILVERMONT_PLUS(g_enmMicroarch)
                              ? "MSR_SMI_COUNT" : "P6_UNK_0000_0034"; /* P6_M_Dothan. */
        case 0x00000035: return CPUMMICROARCH_IS_INTEL_CORE7(g_enmMicroarch) ? "MSR_CORE_THREAD_COUNT" : "P6_UNK_0000_0035"; /* P6_M_Dothan. */
        case 0x00000036: return "I7_UNK_0000_0036"; /* SandyBridge, IvyBridge. */
        case 0x00000039: return "C2_UNK_0000_0039"; /* Core2_Penryn */
        case 0x0000003a: return "IA32_FEATURE_CONTROL";
        case 0x0000003b: return "P6_UNK_0000_003b"; /* P6_M_Dothan. */
        case 0x0000003e: return "I7_UNK_0000_003e"; /* SandyBridge, IvyBridge. */
        case 0x0000003f: return "P6_UNK_0000_003f"; /* P6_M_Dothan. */
        case 0x00000040: return g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah ? "MSR_LASTBRANCH_0_FROM_IP" : "MSR_LASTBRANCH_0";
        case 0x00000041: return g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah ? "MSR_LASTBRANCH_1_FROM_IP" : "MSR_LASTBRANCH_1";
        case 0x00000042: return g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah ? "MSR_LASTBRANCH_2_FROM_IP" : "MSR_LASTBRANCH_2";
        case 0x00000043: return g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah ? "MSR_LASTBRANCH_3_FROM_IP" : "MSR_LASTBRANCH_3";
        case 0x00000044: return g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah ? "MSR_LASTBRANCH_4_FROM_IP" : "MSR_LASTBRANCH_4";
        case 0x00000045: return g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah ? "MSR_LASTBRANCH_5_FROM_IP" : "MSR_LASTBRANCH_5";
        case 0x00000046: return g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah ? "MSR_LASTBRANCH_6_FROM_IP" : "MSR_LASTBRANCH_6";
        case 0x00000047: return g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah ? "MSR_LASTBRANCH_7_FROM_IP" : "MSR_LASTBRANCH_7";
        case 0x00000048: return "MSR_LASTBRANCH_8"; /*??*/
        case 0x00000049: return "MSR_LASTBRANCH_9"; /*??*/
        case 0x0000004a: return "P6_UNK_0000_004a"; /* P6_M_Dothan. */
        case 0x0000004b: return "P6_UNK_0000_004b"; /* P6_M_Dothan. */
        case 0x0000004c: return "P6_UNK_0000_004c"; /* P6_M_Dothan. */
        case 0x0000004d: return "P6_UNK_0000_004d"; /* P6_M_Dothan. */
        case 0x0000004e: return "P6_UNK_0000_004e"; /* P6_M_Dothan. */
        case 0x0000004f: return "P6_UNK_0000_004f"; /* P6_M_Dothan. */
        case 0x00000050: return "P6_UNK_0000_0050"; /* P6_M_Dothan. */
        case 0x00000051: return "P6_UNK_0000_0051"; /* P6_M_Dothan. */
        case 0x00000052: return "P6_UNK_0000_0052"; /* P6_M_Dothan. */
        case 0x00000053: return "P6_UNK_0000_0053"; /* P6_M_Dothan. */
        case 0x00000054: return "P6_UNK_0000_0054"; /* P6_M_Dothan. */
        case 0x00000060: return "MSR_LASTBRANCH_0_TO_IP"; /* Core2_Penryn */
        case 0x00000061: return "MSR_LASTBRANCH_1_TO_IP"; /* Core2_Penryn */
        case 0x00000062: return "MSR_LASTBRANCH_2_TO_IP"; /* Core2_Penryn */
        case 0x00000063: return "MSR_LASTBRANCH_3_TO_IP"; /* Core2_Penryn */
        case 0x00000064: return "MSR_LASTBRANCH_4_TO_IP"; /* Atom? */
        case 0x00000065: return "MSR_LASTBRANCH_5_TO_IP";
        case 0x00000066: return "MSR_LASTBRANCH_6_TO_IP";
        case 0x00000067: return "MSR_LASTBRANCH_7_TO_IP";
        case 0x0000006c: return "P6_UNK_0000_006c"; /* P6_M_Dothan. */
        case 0x0000006d: return "P6_UNK_0000_006d"; /* P6_M_Dothan. */
        case 0x0000006e: return "P6_UNK_0000_006e"; /* P6_M_Dothan. */
        case 0x0000006f: return "P6_UNK_0000_006f"; /* P6_M_Dothan. */
        case 0x00000079: return "IA32_BIOS_UPDT_TRIG";
        case 0x00000080: return "P4_UNK_0000_0080";
        case 0x00000088: return "BBL_CR_D0";
        case 0x00000089: return "BBL_CR_D1";
        case 0x0000008a: return "BBL_CR_D2";
        case 0x0000008b: return g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON ? "AMD_K8_PATCH_LEVEL"
                              : g_fIntelNetBurst ? "IA32_BIOS_SIGN_ID" : "BBL_CR_D3|BIOS_SIGN";
        case 0x0000008c: return "P6_UNK_0000_008c"; /* P6_M_Dothan. */
        case 0x0000008d: return "P6_UNK_0000_008d"; /* P6_M_Dothan. */
        case 0x0000008e: return "P6_UNK_0000_008e"; /* P6_M_Dothan. */
        case 0x0000008f: return "P6_UNK_0000_008f"; /* P6_M_Dothan. */
        case 0x00000090: return "P6_UNK_0000_0090"; /* P6_M_Dothan. */
        case 0x0000009b: return "IA32_SMM_MONITOR_CTL";
        case 0x000000a8: return "C2_EMTTM_CR_TABLES_0";
        case 0x000000a9: return "C2_EMTTM_CR_TABLES_1";
        case 0x000000aa: return "C2_EMTTM_CR_TABLES_2";
        case 0x000000ab: return "C2_EMTTM_CR_TABLES_3";
        case 0x000000ac: return "C2_EMTTM_CR_TABLES_4";
        case 0x000000ad: return "C2_EMTTM_CR_TABLES_5";
        case 0x000000ae: return "P6_UNK_0000_00ae"; /* P6_M_Dothan. */
        case 0x000000c1: return "IA32_PMC0";
        case 0x000000c2: return "IA32_PMC1";
        case 0x000000c3: return "IA32_PMC2";
        case 0x000000c4: return "IA32_PMC3";
        /* PMC4+ first seen on SandyBridge. The earlier cut off is just to be
           on the safe side as we must avoid P6_M_Dothan and possibly others. */
        case 0x000000c5: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_First ? "IA32_PMC4" : NULL;
        case 0x000000c6: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_First ? "IA32_PMC5" : NULL;
        case 0x000000c7: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_First ? "IA32_PMC6" : "P6_UNK_0000_00c7"; /* P6_M_Dothan. */
        case 0x000000c8: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_First ? "IA32_PMC7" : NULL;
        case 0x000000cd: return "MSR_FSB_FREQ"; /* P6_M_Dothan. */
        case 0x000000ce: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_First ? "IA32_PLATFORM_INFO" : "P6_UNK_0000_00ce"; /* P6_M_Dothan. */
        case 0x000000cf: return "C2_UNK_0000_00cf"; /* Core2_Penryn. */
        case 0x000000e0: return "C2_UNK_0000_00e0"; /* Core2_Penryn. */
        case 0x000000e1: return "C2_UNK_0000_00e1"; /* Core2_Penryn. */
        case 0x000000e2: return "MSR_PKG_CST_CONFIG_CONTROL";
        case 0x000000e3: return "C2_SMM_CST_MISC_INFO"; /* Core2_Penryn. */
        case 0x000000e4: return "MSR_PMG_IO_CAPTURE_BASE";
        case 0x000000e5: return "C2_UNK_0000_00e5"; /* Core2_Penryn. */
        case 0x000000e7: return "IA32_MPERF";
        case 0x000000e8: return "IA32_APERF";
        case 0x000000ee: return "C1_EXT_CONFIG"; /* Core2_Penryn. msrtool lists it for Core1 as well. */
        case 0x000000fe: return "IA32_MTRRCAP";
        case 0x00000102: return "I7_IB_UNK_0000_0102"; /* IvyBridge. */
        case 0x00000103: return "I7_IB_UNK_0000_0103"; /* IvyBridge. */
        case 0x00000104: return "I7_IB_UNK_0000_0104"; /* IvyBridge. */
        case 0x00000116: return "BBL_CR_ADDR";
        case 0x00000118: return "BBL_CR_DECC";
        case 0x00000119: return "BBL_CR_CTL";
        case 0x0000011a: return "BBL_CR_TRIG";
        case 0x0000011b: return "P6_UNK_0000_011b"; /* P6_M_Dothan. */
        case 0x0000011c: return "C2_UNK_0000_011c"; /* Core2_Penryn. */
        case 0x0000011e: return "BBL_CR_CTL3";
        case 0x00000120: return "SILV_UNK_0000_0120"; /* Silvermont */
        case 0x00000130: return g_enmMicroarch == kCpumMicroarch_Intel_Core7_Westmere
                             || g_enmMicroarch == kCpumMicroarch_Intel_Core7_Nehalem
                              ? "CPUID1_FEATURE_MASK" : NULL;
        case 0x00000131: return g_enmMicroarch == kCpumMicroarch_Intel_Core7_Westmere
                             || g_enmMicroarch == kCpumMicroarch_Intel_Core7_Nehalem
                              ? "CPUID80000001_FEATURE_MASK" : "P6_UNK_0000_0131" /* P6_M_Dothan. */;
        case 0x00000132: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_SandyBridge
                              ? "CPUID1_FEATURE_MASK" : NULL;
        case 0x00000133: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_SandyBridge
                              ? "CPUIDD_01_FEATURE_MASK" : NULL;
        case 0x00000134: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_SandyBridge
                              ? "CPUID80000001_FEATURE_MASK" : NULL;
        case 0x0000013c: return "I7_SB_AES_NI_CTL"; /* SandyBridge. Bit 0 is lock bit, bit 1 disables AES-NI. */
        case 0x00000140: return "I7_IB_UNK_0000_0140"; /* IvyBridge. */
        case 0x00000142: return "I7_IB_UNK_0000_0142"; /* IvyBridge. */
        case 0x0000014e: return "P6_UNK_0000_014e"; /* P6_M_Dothan. */
        case 0x0000014f: return "P6_UNK_0000_014f"; /* P6_M_Dothan. */
        case 0x00000150: return "P6_UNK_0000_0150"; /* P6_M_Dothan. */
        case 0x00000151: return "P6_UNK_0000_0151"; /* P6_M_Dothan. */
        case 0x00000154: return "P6_UNK_0000_0154"; /* P6_M_Dothan. */
        case 0x0000015b: return "P6_UNK_0000_015b"; /* P6_M_Dothan. */
        case 0x0000015e: return "C2_UNK_0000_015e"; /* Core2_Penryn. */
        case 0x0000015f: return "C1_DTS_CAL_CTRL"; /* Core2_Penryn. msrtool only docs this for core1! */
        case 0x00000174: return "IA32_SYSENTER_CS";
        case 0x00000175: return "IA32_SYSENTER_ESP";
        case 0x00000176: return "IA32_SYSENTER_EIP";
        case 0x00000179: return "IA32_MCG_CAP";
        case 0x0000017a: return "IA32_MCG_STATUS";
        case 0x0000017b: return "IA32_MCG_CTL";
        case 0x0000017f: return "I7_SB_ERROR_CONTROL"; /* SandyBridge. */
        case 0x00000180: return g_fIntelNetBurst ? "MSR_MCG_RAX"       : NULL;
        case 0x00000181: return g_fIntelNetBurst ? "MSR_MCG_RBX"       : NULL;
        case 0x00000182: return g_fIntelNetBurst ? "MSR_MCG_RCX"       : NULL;
        case 0x00000183: return g_fIntelNetBurst ? "MSR_MCG_RDX"       : NULL;
        case 0x00000184: return g_fIntelNetBurst ? "MSR_MCG_RSI"       : NULL;
        case 0x00000185: return g_fIntelNetBurst ? "MSR_MCG_RDI"       : NULL;
        case 0x00000186: return g_fIntelNetBurst ? "MSR_MCG_RBP"       : "IA32_PERFEVTSEL0";
        case 0x00000187: return g_fIntelNetBurst ? "MSR_MCG_RSP"       : "IA32_PERFEVTSEL1";
        case 0x00000188: return g_fIntelNetBurst ? "MSR_MCG_RFLAGS"    : "IA32_PERFEVTSEL2";
        case 0x00000189: return g_fIntelNetBurst ? "MSR_MCG_RIP"       : "IA32_PERFEVTSEL3";
        case 0x0000018a: return g_fIntelNetBurst ? "MSR_MCG_MISC"      : "IA32_PERFEVTSEL4";
        case 0x0000018b: return g_fIntelNetBurst ? "MSR_MCG_RESERVED1" : "IA32_PERFEVTSEL5";
        case 0x0000018c: return g_fIntelNetBurst ? "MSR_MCG_RESERVED2" : "IA32_PERFEVTSEL6";
        case 0x0000018d: return g_fIntelNetBurst ? "MSR_MCG_RESERVED3" : "IA32_PERFEVTSEL7";
        case 0x0000018e: return g_fIntelNetBurst ? "MSR_MCG_RESERVED4" : "IA32_PERFEVTSEL8";
        case 0x0000018f: return g_fIntelNetBurst ? "MSR_MCG_RESERVED5" : "IA32_PERFEVTSEL9";
        case 0x00000190: return g_fIntelNetBurst ? "MSR_MCG_R8"        : NULL;
        case 0x00000191: return g_fIntelNetBurst ? "MSR_MCG_R9"        : NULL;
        case 0x00000192: return g_fIntelNetBurst ? "MSR_MCG_R10"       : NULL;
        case 0x00000193: return g_fIntelNetBurst ? "MSR_MCG_R11"       : "C2_UNK_0000_0193";
        case 0x00000194: return g_fIntelNetBurst ? "MSR_MCG_R12"       : "CLOCK_FLEX_MAX";
        case 0x00000195: return g_fIntelNetBurst ? "MSR_MCG_R13"       : NULL;
        case 0x00000196: return g_fIntelNetBurst ? "MSR_MCG_R14"       : NULL;
        case 0x00000197: return g_fIntelNetBurst ? "MSR_MCG_R15"       : NULL;
        case 0x00000198: return "IA32_PERF_STATUS";
        case 0x00000199: return "IA32_PERF_CTL";
        case 0x0000019a: return "IA32_CLOCK_MODULATION";
        case 0x0000019b: return "IA32_THERM_INTERRUPT";
        case 0x0000019c: return "IA32_THERM_STATUS";
        case 0x0000019d: return "IA32_THERM2_CTL";
        case 0x0000019e: return "P6_UNK_0000_019e"; /* P6_M_Dothan. */
        case 0x0000019f: return "P6_UNK_0000_019f"; /* P6_M_Dothan. */
        case 0x000001a0: return "IA32_MISC_ENABLE";
        case 0x000001a1: return g_fIntelNetBurst ? "MSR_PLATFORM_BRV" : "P6_UNK_0000_01a1" /* P6_M_Dothan. */;
        case 0x000001a2: return g_fIntelNetBurst ? "P4_UNK_0000_01a2" : "I7_MSR_TEMPERATURE_TARGET" /* SandyBridge, IvyBridge. */;
        case 0x000001a4: return "I7_UNK_0000_01a4"; /* SandyBridge, IvyBridge. */
        case 0x000001a6: return "I7_MSR_OFFCORE_RSP_0";
        case 0x000001a7: return "I7_MSR_OFFCORE_RSP_1";
        case 0x000001a8: return "I7_UNK_0000_01a8"; /* SandyBridge, IvyBridge. */
        case 0x000001aa: return CPUMMICROARCH_IS_INTEL_CORE7(g_enmMicroarch) ? "MSR_MISC_PWR_MGMT" : "P6_PIC_SENS_CFG" /* Pentium M. */;
        case 0x000001ad: return "I7_MSR_TURBO_RATIO_LIMIT"; /* SandyBridge+, Silvermount+ */
        case 0x000001ae: return "P6_UNK_0000_01ae"; /* P6_M_Dothan. */
        case 0x000001af: return "P6_UNK_0000_01af"; /* P6_M_Dothan. */
        case 0x000001b0: return "IA32_ENERGY_PERF_BIAS";
        case 0x000001b1: return "IA32_PACKAGE_THERM_STATUS";
        case 0x000001b2: return "IA32_PACKAGE_THERM_INTERRUPT";
        case 0x000001bf: return "C2_UNK_0000_01bf"; /* Core2_Penryn. */
        case 0x000001c6: return "I7_UNK_0000_01c6"; /* SandyBridge*/
        case 0x000001c8: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_Nehalem ? "MSR_LBR_SELECT" : NULL;
        case 0x000001c9: return    g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah
                                && g_enmMicroarch <= kCpumMicroarch_Intel_P6_Core_Atom_End
                              ? "MSR_LASTBRANCH_TOS" : NULL /* Pentium M Dothan seems to have something else here. */;
        case 0x000001d3: return "P6_UNK_0000_01d3"; /* P6_M_Dothan. */
        case 0x000001d7: return g_fIntelNetBurst ? "MSR_LER_FROM_LIP" : NULL;
        case 0x000001d8: return g_fIntelNetBurst ? "MSR_LER_TO_LIP"   : NULL;
        case 0x000001d9: return "IA32_DEBUGCTL";
        case 0x000001da: return g_fIntelNetBurst ? "MSR_LASTBRANCH_TOS" : NULL;
        case 0x000001db: return g_fIntelNetBurst ? "P6_LASTBRANCH_0" : "P6_LAST_BRANCH_FROM_IP"; /* Not exclusive to P6, also AMD. */
        case 0x000001dc: return g_fIntelNetBurst ? "P6_LASTBRANCH_1" : "P6_LAST_BRANCH_TO_IP";
        case 0x000001dd: return g_fIntelNetBurst ? "P6_LASTBRANCH_2" : "P6_LAST_INT_FROM_IP";
        case 0x000001de: return g_fIntelNetBurst ? "P6_LASTBRANCH_3" : "P6_LAST_INT_TO_IP";
        case 0x000001e0: return "MSR_ROB_CR_BKUPTMPDR6";
        case 0x000001e1: return "I7_SB_UNK_0000_01e1";
        case 0x000001ef: return "I7_SB_UNK_0000_01ef";
        case 0x000001f0: return "I7_VLW_CAPABILITY"; /* SandyBridge.  Bit 1 is A20M and was implemented incorrectly (AAJ49). */
        case 0x000001f2: return "IA32_SMRR_PHYSBASE";
        case 0x000001f3: return "IA32_SMRR_PHYSMASK";
        case 0x000001f8: return "IA32_PLATFORM_DCA_CAP";
        case 0x000001f9: return "IA32_CPU_DCA_CAP";
        case 0x000001fa: return "IA32_DCA_0_CAP";
        case 0x000001fc: return "I7_MSR_POWER_CTL";

        case 0x00000200: return "IA32_MTRR_PHYS_BASE0";
        case 0x00000202: return "IA32_MTRR_PHYS_BASE1";
        case 0x00000204: return "IA32_MTRR_PHYS_BASE2";
        case 0x00000206: return "IA32_MTRR_PHYS_BASE3";
        case 0x00000208: return "IA32_MTRR_PHYS_BASE4";
        case 0x0000020a: return "IA32_MTRR_PHYS_BASE5";
        case 0x0000020c: return "IA32_MTRR_PHYS_BASE6";
        case 0x0000020e: return "IA32_MTRR_PHYS_BASE7";
        case 0x00000210: return "IA32_MTRR_PHYS_BASE8";
        case 0x00000212: return "IA32_MTRR_PHYS_BASE9";
        case 0x00000214: return "IA32_MTRR_PHYS_BASE10";
        case 0x00000216: return "IA32_MTRR_PHYS_BASE11";
        case 0x00000218: return "IA32_MTRR_PHYS_BASE12";
        case 0x0000021a: return "IA32_MTRR_PHYS_BASE13";
        case 0x0000021c: return "IA32_MTRR_PHYS_BASE14";
        case 0x0000021e: return "IA32_MTRR_PHYS_BASE15";

        case 0x00000201: return "IA32_MTRR_PHYS_MASK0";
        case 0x00000203: return "IA32_MTRR_PHYS_MASK1";
        case 0x00000205: return "IA32_MTRR_PHYS_MASK2";
        case 0x00000207: return "IA32_MTRR_PHYS_MASK3";
        case 0x00000209: return "IA32_MTRR_PHYS_MASK4";
        case 0x0000020b: return "IA32_MTRR_PHYS_MASK5";
        case 0x0000020d: return "IA32_MTRR_PHYS_MASK6";
        case 0x0000020f: return "IA32_MTRR_PHYS_MASK7";
        case 0x00000211: return "IA32_MTRR_PHYS_MASK8";
        case 0x00000213: return "IA32_MTRR_PHYS_MASK9";
        case 0x00000215: return "IA32_MTRR_PHYS_MASK10";
        case 0x00000217: return "IA32_MTRR_PHYS_MASK11";
        case 0x00000219: return "IA32_MTRR_PHYS_MASK12";
        case 0x0000021b: return "IA32_MTRR_PHYS_MASK13";
        case 0x0000021d: return "IA32_MTRR_PHYS_MASK14";
        case 0x0000021f: return "IA32_MTRR_PHYS_MASK15";

        case 0x00000250: return "IA32_MTRR_FIX64K_00000";
        case 0x00000258: return "IA32_MTRR_FIX16K_80000";
        case 0x00000259: return "IA32_MTRR_FIX16K_A0000";
        case 0x00000268: return "IA32_MTRR_FIX4K_C0000";
        case 0x00000269: return "IA32_MTRR_FIX4K_C8000";
        case 0x0000026a: return "IA32_MTRR_FIX4K_D0000";
        case 0x0000026b: return "IA32_MTRR_FIX4K_D8000";
        case 0x0000026c: return "IA32_MTRR_FIX4K_E0000";
        case 0x0000026d: return "IA32_MTRR_FIX4K_E8000";
        case 0x0000026e: return "IA32_MTRR_FIX4K_F0000";
        case 0x0000026f: return "IA32_MTRR_FIX4K_F8000";
        case 0x00000277: return "IA32_PAT";
        case 0x00000280: return "IA32_MC0_CTL2";
        case 0x00000281: return "IA32_MC1_CTL2";
        case 0x00000282: return "IA32_MC2_CTL2";
        case 0x00000283: return "IA32_MC3_CTL2";
        case 0x00000284: return "IA32_MC4_CTL2";
        case 0x00000285: return "IA32_MC5_CTL2";
        case 0x00000286: return "IA32_MC6_CTL2";
        case 0x00000287: return "IA32_MC7_CTL2";
        case 0x00000288: return "IA32_MC8_CTL2";
        case 0x00000289: return "IA32_MC9_CTL2";
        case 0x0000028a: return "IA32_MC10_CTL2";
        case 0x0000028b: return "IA32_MC11_CTL2";
        case 0x0000028c: return "IA32_MC12_CTL2";
        case 0x0000028d: return "IA32_MC13_CTL2";
        case 0x0000028e: return "IA32_MC14_CTL2";
        case 0x0000028f: return "IA32_MC15_CTL2";
        case 0x00000290: return "IA32_MC16_CTL2";
        case 0x00000291: return "IA32_MC17_CTL2";
        case 0x00000292: return "IA32_MC18_CTL2";
        case 0x00000293: return "IA32_MC19_CTL2";
        case 0x00000294: return "IA32_MC20_CTL2";
        case 0x00000295: return "IA32_MC21_CTL2";
        //case 0x00000296: return "IA32_MC22_CTL2";
        //case 0x00000297: return "IA32_MC23_CTL2";
        //case 0x00000298: return "IA32_MC24_CTL2";
        //case 0x00000299: return "IA32_MC25_CTL2";
        //case 0x0000029a: return "IA32_MC26_CTL2";
        //case 0x0000029b: return "IA32_MC27_CTL2";
        //case 0x0000029c: return "IA32_MC28_CTL2";
        //case 0x0000029d: return "IA32_MC29_CTL2";
        //case 0x0000029e: return "IA32_MC30_CTL2";
        //case 0x0000029f: return "IA32_MC31_CTL2";
        case 0x000002e0: return "I7_SB_NO_EVICT_MODE"; /* (Bits 1 & 0 are said to have something to do with no-evict cache mode used during early boot.) */
        case 0x000002e6: return "I7_IB_UNK_0000_02e6"; /* IvyBridge */
        case 0x000002e7: return "I7_IB_UNK_0000_02e7"; /* IvyBridge */
        case 0x000002ff: return "IA32_MTRR_DEF_TYPE";
        case 0x00000300: return g_fIntelNetBurst ? "P4_MSR_BPU_COUNTER0"   : "I7_SB_UNK_0000_0300" /* SandyBridge */;
        case 0x00000301: return g_fIntelNetBurst ? "P4_MSR_BPU_COUNTER1"   : NULL;
        case 0x00000302: return g_fIntelNetBurst ? "P4_MSR_BPU_COUNTER2"   : NULL;
        case 0x00000303: return g_fIntelNetBurst ? "P4_MSR_BPU_COUNTER3"   : NULL;
        case 0x00000304: return g_fIntelNetBurst ? "P4_MSR_MS_COUNTER0"    : NULL;
        case 0x00000305: return g_fIntelNetBurst ? "P4_MSR_MS_COUNTER1"    : "I7_SB_UNK_0000_0305" /* SandyBridge, IvyBridge */;
        case 0x00000306: return g_fIntelNetBurst ? "P4_MSR_MS_COUNTER2"    : NULL;
        case 0x00000307: return g_fIntelNetBurst ? "P4_MSR_MS_COUNTER3"    : NULL;
        case 0x00000308: return g_fIntelNetBurst ? "P4_MSR_FLAME_COUNTER0" : NULL;
        case 0x00000309: return g_fIntelNetBurst ? "P4_MSR_FLAME_COUNTER1" : "IA32_FIXED_CTR0";
        case 0x0000030a: return g_fIntelNetBurst ? "P4_MSR_FLAME_COUNTER2" : "IA32_FIXED_CTR1";
        case 0x0000030b: return g_fIntelNetBurst ? "P4_MSR_FLAME_COUNTER3" : "IA32_FIXED_CTR2";
        case 0x0000030c: return g_fIntelNetBurst ? "P4_MSR_IQ_COUNTER0" : NULL;
        case 0x0000030d: return g_fIntelNetBurst ? "P4_MSR_IQ_COUNTER1" : NULL;
        case 0x0000030e: return g_fIntelNetBurst ? "P4_MSR_IQ_COUNTER2" : NULL;
        case 0x0000030f: return g_fIntelNetBurst ? "P4_MSR_IQ_COUNTER3" : NULL;
        case 0x00000310: return g_fIntelNetBurst ? "P4_MSR_IQ_COUNTER4" : NULL;
        case 0x00000311: return g_fIntelNetBurst ? "P4_MSR_IQ_COUNTER5" : NULL;
        case 0x00000345: return "IA32_PERF_CAPABILITIES";
        case 0x00000360: return g_fIntelNetBurst ? "P4_MSR_BPU_CCCR0"   : NULL;
        case 0x00000361: return g_fIntelNetBurst ? "P4_MSR_BPU_CCCR1"   : NULL;
        case 0x00000362: return g_fIntelNetBurst ? "P4_MSR_BPU_CCCR2"   : NULL;
        case 0x00000363: return g_fIntelNetBurst ? "P4_MSR_BPU_CCCR3"   : NULL;
        case 0x00000364: return g_fIntelNetBurst ? "P4_MSR_MS_CCCR0"    : NULL;
        case 0x00000365: return g_fIntelNetBurst ? "P4_MSR_MS_CCCR1"    : NULL;
        case 0x00000366: return g_fIntelNetBurst ? "P4_MSR_MS_CCCR2"    : NULL;
        case 0x00000367: return g_fIntelNetBurst ? "P4_MSR_MS_CCCR3"    : NULL;
        case 0x00000368: return g_fIntelNetBurst ? "P4_MSR_FLAME_CCCR0" : NULL;
        case 0x00000369: return g_fIntelNetBurst ? "P4_MSR_FLAME_CCCR1" : NULL;
        case 0x0000036a: return g_fIntelNetBurst ? "P4_MSR_FLAME_CCCR2" : NULL;
        case 0x0000036b: return g_fIntelNetBurst ? "P4_MSR_FLAME_CCCR3" : NULL;
        case 0x0000036c: return g_fIntelNetBurst ? "P4_MSR_IQ_CCCR0"    : NULL;
        case 0x0000036d: return g_fIntelNetBurst ? "P4_MSR_IQ_CCCR1"    : NULL;
        case 0x0000036e: return g_fIntelNetBurst ? "P4_MSR_IQ_CCCR2"    : NULL;
        case 0x0000036f: return g_fIntelNetBurst ? "P4_MSR_IQ_CCCR3"    : NULL;
        case 0x00000370: return g_fIntelNetBurst ? "P4_MSR_IQ_CCCR4"    : NULL;
        case 0x00000371: return g_fIntelNetBurst ? "P4_MSR_IQ_CCCR5"    : NULL;
        case 0x0000038d: return "IA32_FIXED_CTR_CTRL";
        case 0x0000038e: return "IA32_PERF_GLOBAL_STATUS";
        case 0x0000038f: return "IA32_PERF_GLOBAL_CTRL";
        case 0x00000390: return "IA32_PERF_GLOBAL_OVF_CTRL";
        case 0x00000391: return "I7_UNC_PERF_GLOBAL_CTRL";             /* S,H,X */
        case 0x00000392: return "I7_UNC_PERF_GLOBAL_STATUS";           /* S,H,X */
        case 0x00000393: return "I7_UNC_PERF_GLOBAL_OVF_CTRL";         /* X. ASSUMING this is the same on sandybridge and later. */
        case 0x00000394: return g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "I7_UNC_PERF_FIXED_CTR"  /* X */    : "I7_UNC_PERF_FIXED_CTR_CTRL"; /* >= S,H */
        case 0x00000395: return g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "I7_UNC_PERF_FIXED_CTR_CTRL" /* X*/ : "I7_UNC_PERF_FIXED_CTR";      /* >= S,H */
        case 0x00000396: return g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "I7_UNC_ADDR_OPCODE_MATCH" /* X */  : "I7_UNC_CBO_CONFIG";          /* >= S,H */
        case 0x00000397: return g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? NULL                                : "I7_SB_UNK_0000_0397";
        case 0x0000039c: return "I7_SB_MSR_PEBS_NUM_ALT";
        case 0x000003a0: return g_fIntelNetBurst ? "P4_MSR_BSU_ESCR0"   : NULL;
        case 0x000003a1: return g_fIntelNetBurst ? "P4_MSR_BSU_ESCR1"   : NULL;
        case 0x000003a2: return g_fIntelNetBurst ? "P4_MSR_FSB_ESCR0"   : NULL;
        case 0x000003a3: return g_fIntelNetBurst ? "P4_MSR_FSB_ESCR1"   : NULL;
        case 0x000003a4: return g_fIntelNetBurst ? "P4_MSR_FIRM_ESCR0"  : NULL;
        case 0x000003a5: return g_fIntelNetBurst ? "P4_MSR_FIRM_ESCR1"  : NULL;
        case 0x000003a6: return g_fIntelNetBurst ? "P4_MSR_FLAME_ESCR0" : NULL;
        case 0x000003a7: return g_fIntelNetBurst ? "P4_MSR_FLAME_ESCR1" : NULL;
        case 0x000003a8: return g_fIntelNetBurst ? "P4_MSR_DAC_ESCR0"   : NULL;
        case 0x000003a9: return g_fIntelNetBurst ? "P4_MSR_DAC_ESCR1"   : NULL;
        case 0x000003aa: return g_fIntelNetBurst ? "P4_MSR_MOB_ESCR0"   : NULL;
        case 0x000003ab: return g_fIntelNetBurst ? "P4_MSR_MOB_ESCR1"   : NULL;
        case 0x000003ac: return g_fIntelNetBurst ? "P4_MSR_PMH_ESCR0"   : NULL;
        case 0x000003ad: return g_fIntelNetBurst ? "P4_MSR_PMH_ESCR1"   : NULL;
        case 0x000003ae: return g_fIntelNetBurst ? "P4_MSR_SAAT_ESCR0"  : NULL;
        case 0x000003af: return g_fIntelNetBurst ? "P4_MSR_SAAT_ESCR1"  : NULL;
        case 0x000003b0: return g_fIntelNetBurst ? "P4_MSR_U2L_ESCR0" : g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "I7_UNC_PMC0" /* X */               : "I7_UNC_ARB_PERF_CTR0";       /* >= S,H */
        case 0x000003b1: return g_fIntelNetBurst ? "P4_MSR_U2L_ESCR1" : g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "I7_UNC_PMC1" /* X */               : "I7_UNC_ARB_PERF_CTR1";       /* >= S,H */
        case 0x000003b2: return g_fIntelNetBurst ? "P4_MSR_BPU_ESCR0" : g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "I7_UNC_PMC2" /* X */               : "I7_UNC_ARB_PERF_EVT_SEL0";   /* >= S,H */
        case 0x000003b3: return g_fIntelNetBurst ? "P4_MSR_BPU_ESCR1" : g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "I7_UNC_PMC3" /* X */               : "I7_UNC_ARB_PERF_EVT_SEL1";   /* >= S,H */
        case 0x000003b4: return g_fIntelNetBurst ? "P4_MSR_IS_ESCR0"    : "I7_UNC_PMC4";
        case 0x000003b5: return g_fIntelNetBurst ? "P4_MSR_IS_ESCR1"    : "I7_UNC_PMC5";
        case 0x000003b6: return g_fIntelNetBurst ? "P4_MSR_ITLB_ESCR0"  : "I7_UNC_PMC6";
        case 0x000003b7: return g_fIntelNetBurst ? "P4_MSR_ITLB_ESCR1"  : "I7_UNC_PMC7";
        case 0x000003b8: return g_fIntelNetBurst ? "P4_MSR_CRU_ESCR0"   : NULL;
        case 0x000003b9: return g_fIntelNetBurst ? "P4_MSR_CRU_ESCR1"   : NULL;
        case 0x000003ba: return g_fIntelNetBurst ? "P4_MSR_IQ_ESCR0"    : NULL;
        case 0x000003bb: return g_fIntelNetBurst ? "P4_MSR_IQ_ESCR1"    : NULL;
        case 0x000003bc: return g_fIntelNetBurst ? "P4_MSR_RAT_ESCR0"   : NULL;
        case 0x000003bd: return g_fIntelNetBurst ? "P4_MSR_RAT_ESCR1"   : NULL;
        case 0x000003be: return g_fIntelNetBurst ? "P4_MSR_SSU_ESCR0"   : NULL;
        case 0x000003c0: return g_fIntelNetBurst ? "P4_MSR_MS_ESCR0"    : "I7_UNC_PERF_EVT_SEL0";
        case 0x000003c1: return g_fIntelNetBurst ? "P4_MSR_MS_ESCR1"    : "I7_UNC_PERF_EVT_SEL1";
        case 0x000003c2: return g_fIntelNetBurst ? "P4_MSR_TBPU_ESCR0"  : "I7_UNC_PERF_EVT_SEL2";
        case 0x000003c3: return g_fIntelNetBurst ? "P4_MSR_TBPU_ESCR1"  : "I7_UNC_PERF_EVT_SEL3";
        case 0x000003c4: return g_fIntelNetBurst ? "P4_MSR_TC_ESCR0"    : "I7_UNC_PERF_EVT_SEL4";
        case 0x000003c5: return g_fIntelNetBurst ? "P4_MSR_TC_ESCR1"    : "I7_UNC_PERF_EVT_SEL5";
        case 0x000003c6: return g_fIntelNetBurst ? NULL                 : "I7_UNC_PERF_EVT_SEL6";
        case 0x000003c7: return g_fIntelNetBurst ? NULL                 : "I7_UNC_PERF_EVT_SEL7";
        case 0x000003c8: return g_fIntelNetBurst ? "P4_MSR_IX_ESCR0"    : NULL;
        case 0x000003c9: return g_fIntelNetBurst ? "P4_MSR_IX_ESCR0"    : NULL;
        case 0x000003ca: return g_fIntelNetBurst ? "P4_MSR_ALF_ESCR0"   : NULL;
        case 0x000003cb: return g_fIntelNetBurst ? "P4_MSR_ALF_ESCR1"   : NULL;
        case 0x000003cc: return g_fIntelNetBurst ? "P4_MSR_CRU_ESCR2"   : NULL;
        case 0x000003cd: return g_fIntelNetBurst ? "P4_MSR_CRU_ESCR3"   : NULL;
        case 0x000003e0: return g_fIntelNetBurst ? "P4_MSR_CRU_ESCR4"   : NULL;
        case 0x000003e1: return g_fIntelNetBurst ? "P4_MSR_CRU_ESCR5"   : NULL;
        case 0x000003f0: return g_fIntelNetBurst ? "P4_MSR_TC_PRECISE_EVENT" : NULL;
        case 0x000003f1: return "IA32_PEBS_ENABLE";
        case 0x000003f2: return g_fIntelNetBurst ? "P4_MSR_PEBS_MATRIX_VERT" : "IA32_PEBS_ENABLE";
        case 0x000003f3: return g_fIntelNetBurst ? "P4_UNK_0000_03f3" : NULL;
        case 0x000003f4: return g_fIntelNetBurst ? "P4_UNK_0000_03f4" : NULL;
        case 0x000003f5: return g_fIntelNetBurst ? "P4_UNK_0000_03f5" : NULL;
        case 0x000003f6: return g_fIntelNetBurst ? "P4_UNK_0000_03f6" : "I7_MSR_PEBS_LD_LAT";
        case 0x000003f7: return g_fIntelNetBurst ? "P4_UNK_0000_03f7" : "I7_MSR_PEBS_LD_LAT";
        case 0x000003f8: return g_fIntelNetBurst ? "P4_UNK_0000_03f8" : "I7_MSR_PKG_C3_RESIDENCY";
        case 0x000003f9: return "I7_MSR_PKG_C6_RESIDENCY";
        case 0x000003fa: return "I7_MSR_PKG_C7_RESIDENCY";
        case 0x000003fc: return "I7_MSR_CORE_C3_RESIDENCY";
        case 0x000003fd: return "I7_MSR_CORE_C6_RESIDENCY";
        case 0x000003fe: return "I7_MSR_CORE_C7_RESIDENCY";
        case 0x00000478: return g_enmMicroarch == kCpumMicroarch_Intel_Core2_Penryn ? "CPUID1_FEATURE_MASK" : NULL;
        case 0x00000480: return "IA32_VMX_BASIC";
        case 0x00000481: return "IA32_VMX_PINBASED_CTLS";
        case 0x00000482: return "IA32_VMX_PROCBASED_CTLS";
        case 0x00000483: return "IA32_VMX_EXIT_CTLS";
        case 0x00000484: return "IA32_VMX_ENTRY_CTLS";
        case 0x00000485: return "IA32_VMX_MISC";
        case 0x00000486: return "IA32_VMX_CR0_FIXED0";
        case 0x00000487: return "IA32_VMX_CR0_FIXED1";
        case 0x00000488: return "IA32_VMX_CR4_FIXED0";
        case 0x00000489: return "IA32_VMX_CR4_FIXED1";
        case 0x0000048a: return "IA32_VMX_VMCS_ENUM";
        case 0x0000048b: return "IA32_VMX_PROCBASED_CTLS2";
        case 0x0000048c: return "IA32_VMX_EPT_VPID_CAP";
        case 0x0000048d: return "IA32_VMX_TRUE_PINBASED_CTLS";
        case 0x0000048e: return "IA32_VMX_TRUE_PROCBASED_CTLS";
        case 0x0000048f: return "IA32_VMX_TRUE_EXIT_CTLS";
        case 0x00000490: return "IA32_VMX_TRUE_ENTRY_CTLS";
        case 0x00000491: return "IA32_VMX_VMFUNC";
        case 0x000004c1: return "IA32_A_PMC0";
        case 0x000004c2: return "IA32_A_PMC1";
        case 0x000004c3: return "IA32_A_PMC2";
        case 0x000004c4: return "IA32_A_PMC3";
        case 0x000004c5: return "IA32_A_PMC4";
        case 0x000004c6: return "IA32_A_PMC5";
        case 0x000004c7: return "IA32_A_PMC6";
        case 0x000004c8: return "IA32_A_PMC7";
        case 0x000004f8: return "C2_UNK_0000_04f8"; /* Core2_Penryn. */
        case 0x000004f9: return "C2_UNK_0000_04f9"; /* Core2_Penryn. */
        case 0x000004fa: return "C2_UNK_0000_04fa"; /* Core2_Penryn. */
        case 0x000004fb: return "C2_UNK_0000_04fb"; /* Core2_Penryn. */
        case 0x000004fc: return "C2_UNK_0000_04fc"; /* Core2_Penryn. */
        case 0x000004fd: return "C2_UNK_0000_04fd"; /* Core2_Penryn. */
        case 0x000004fe: return "C2_UNK_0000_04fe"; /* Core2_Penryn. */
        case 0x000004ff: return "C2_UNK_0000_04ff"; /* Core2_Penryn. */
        case 0x00000502: return "I7_SB_UNK_0000_0502";
        case 0x00000590: return "C2_UNK_0000_0590"; /* Core2_Penryn. */
        case 0x00000591: return "C2_UNK_0000_0591"; /* Core2_Penryn. */
        case 0x000005a0: return "C2_PECI_CTL"; /* Core2_Penryn. */
        case 0x000005a1: return "C2_UNK_0000_05a1"; /* Core2_Penryn. */
        case 0x00000600: return "IA32_DS_AREA";
        case 0x00000601: return "I7_SB_MSR_VR_CURRENT_CONFIG"; /* SandyBridge, IvyBridge. */
        case 0x00000602: return "I7_IB_UNK_0000_0602";
        case 0x00000603: return "I7_SB_MSR_VR_MISC_CONFIG"; /* SandyBridge, IvyBridge. */
        case 0x00000604: return "I7_IB_UNK_0000_0602";
        case 0x00000606: return "I7_SB_MSR_RAPL_POWER_UNIT"; /* SandyBridge, IvyBridge. */
        case 0x00000609: return "I7_SB_UNK_0000_0609";  /* SandyBridge (non EP). */
        case 0x0000060a: return "I7_SB_MSR_PKGC3_IRTL"; /* SandyBridge, IvyBridge. */
        case 0x0000060b: return "I7_SB_MSR_PKGC6_IRTL"; /* SandyBridge, IvyBridge. */
        case 0x0000060c: return "I7_SB_MSR_PKGC7_IRTL"; /* SandyBridge, IvyBridge. */
        case 0x0000060d: return "I7_SB_MSR_PKG_C2_RESIDENCY"; /* SandyBridge, IvyBridge. */
        case 0x00000610: return "I7_SB_MSR_PKG_POWER_LIMIT";
        case 0x00000611: return "I7_SB_MSR_PKG_ENERGY_STATUS";
        case 0x00000613: return "I7_SB_MSR_PKG_PERF_STATUS";
        case 0x00000614: return "I7_SB_MSR_PKG_POWER_INFO";
        case 0x00000618: return "I7_SB_MSR_DRAM_POWER_LIMIT";
        case 0x00000619: return "I7_SB_MSR_DRAM_ENERGY_STATUS";
        case 0x0000061b: return "I7_SB_MSR_DRAM_PERF_STATUS";
        case 0x0000061c: return "I7_SB_MSR_DRAM_POWER_INFO";
        case 0x00000638: return "I7_SB_MSR_PP0_POWER_LIMIT";
        case 0x00000639: return "I7_SB_MSR_PP0_ENERGY_STATUS";
        case 0x0000063a: return "I7_SB_MSR_PP0_POLICY";
        case 0x0000063b: return "I7_SB_MSR_PP0_PERF_STATUS";
        case 0x00000640: return "I7_HW_MSR_PP0_POWER_LIMIT";
        case 0x00000641: return "I7_HW_MSR_PP0_ENERGY_STATUS";
        case 0x00000642: return "I7_HW_MSR_PP0_POLICY";
        case 0x00000648: return "I7_IB_MSR_CONFIG_TDP_NOMINAL";
        case 0x00000649: return "I7_IB_MSR_CONFIG_TDP_LEVEL1";
        case 0x0000064a: return "I7_IB_MSR_CONFIG_TDP_LEVEL2";
        case 0x0000064b: return "I7_IB_MSR_CONFIG_TDP_CONTROL";
        case 0x0000064c: return "I7_IB_MSR_TURBO_ACTIVATION_RATIO";
        case 0x00000660: return "SILV_CORE_C1_RESIDENCY";
        case 0x00000661: return "SILV_UNK_0000_0661";
        case 0x00000662: return "SILV_UNK_0000_0662";
        case 0x00000663: return "SILV_UNK_0000_0663";
        case 0x00000664: return "SILV_UNK_0000_0664";
        case 0x00000665: return "SILV_UNK_0000_0665";
        case 0x00000666: return "SILV_UNK_0000_0666";
        case 0x00000667: return "SILV_UNK_0000_0667";
        case 0x00000668: return "SILV_UNK_0000_0668";
        case 0x00000669: return "SILV_UNK_0000_0669";
        case 0x0000066a: return "SILV_UNK_0000_066a";
        case 0x0000066b: return "SILV_UNK_0000_066b";
        case 0x0000066c: return "SILV_UNK_0000_066c";
        case 0x0000066d: return "SILV_UNK_0000_066d";
        case 0x0000066e: return "SILV_UNK_0000_066e";
        case 0x0000066f: return "SILV_UNK_0000_066f";
        case 0x00000670: return "SILV_UNK_0000_0670";
        case 0x00000671: return "SILV_UNK_0000_0671";
        case 0x00000672: return "SILV_UNK_0000_0672";
        case 0x00000673: return "SILV_UNK_0000_0673";
        case 0x00000674: return "SILV_UNK_0000_0674";
        case 0x00000675: return "SILV_UNK_0000_0675";
        case 0x00000676: return "SILV_UNK_0000_0676";
        case 0x00000677: return "SILV_UNK_0000_0677";

        case 0x00000680: return "MSR_LASTBRANCH_0_FROM_IP";
        case 0x00000681: return "MSR_LASTBRANCH_1_FROM_IP";
        case 0x00000682: return "MSR_LASTBRANCH_2_FROM_IP";
        case 0x00000683: return "MSR_LASTBRANCH_3_FROM_IP";
        case 0x00000684: return "MSR_LASTBRANCH_4_FROM_IP";
        case 0x00000685: return "MSR_LASTBRANCH_5_FROM_IP";
        case 0x00000686: return "MSR_LASTBRANCH_6_FROM_IP";
        case 0x00000687: return "MSR_LASTBRANCH_7_FROM_IP";
        case 0x00000688: return "MSR_LASTBRANCH_8_FROM_IP";
        case 0x00000689: return "MSR_LASTBRANCH_9_FROM_IP";
        case 0x0000068a: return "MSR_LASTBRANCH_10_FROM_IP";
        case 0x0000068b: return "MSR_LASTBRANCH_11_FROM_IP";
        case 0x0000068c: return "MSR_LASTBRANCH_12_FROM_IP";
        case 0x0000068d: return "MSR_LASTBRANCH_13_FROM_IP";
        case 0x0000068e: return "MSR_LASTBRANCH_14_FROM_IP";
        case 0x0000068f: return "MSR_LASTBRANCH_15_FROM_IP";
        case 0x000006c0: return "MSR_LASTBRANCH_0_TO_IP";
        case 0x000006c1: return "MSR_LASTBRANCH_1_TO_IP";
        case 0x000006c2: return "MSR_LASTBRANCH_2_TO_IP";
        case 0x000006c3: return "MSR_LASTBRANCH_3_TO_IP";
        case 0x000006c4: return "MSR_LASTBRANCH_4_TO_IP";
        case 0x000006c5: return "MSR_LASTBRANCH_5_TO_IP";
        case 0x000006c6: return "MSR_LASTBRANCH_6_TO_IP";
        case 0x000006c7: return "MSR_LASTBRANCH_7_TO_IP";
        case 0x000006c8: return "MSR_LASTBRANCH_8_TO_IP";
        case 0x000006c9: return "MSR_LASTBRANCH_9_TO_IP";
        case 0x000006ca: return "MSR_LASTBRANCH_10_TO_IP";
        case 0x000006cb: return "MSR_LASTBRANCH_11_TO_IP";
        case 0x000006cc: return "MSR_LASTBRANCH_12_TO_IP";
        case 0x000006cd: return "MSR_LASTBRANCH_13_TO_IP";
        case 0x000006ce: return "MSR_LASTBRANCH_14_TO_IP";
        case 0x000006cf: return "MSR_LASTBRANCH_15_TO_IP";
        case 0x000006e0: return "IA32_TSC_DEADLINE";

        case 0x00000768: return "SILV_UNK_0000_0768";
        case 0x00000769: return "SILV_UNK_0000_0769";
        case 0x0000076a: return "SILV_UNK_0000_076a";
        case 0x0000076b: return "SILV_UNK_0000_076b";
        case 0x0000076c: return "SILV_UNK_0000_076c";
        case 0x0000076d: return "SILV_UNK_0000_076d";
        case 0x0000076e: return "SILV_UNK_0000_076e";

        case 0x00000c80: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_IvyBridge ? "IA32_DEBUG_INTERFACE" : NULL; /* Mentioned in an intel dataskit called 4th-gen-core-family-desktop-vol-1-datasheet.pdf. */
        case 0x00000c81: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_IvyBridge ? "I7_IB_UNK_0000_0c81"  : NULL; /* Probably related to IA32_DEBUG_INTERFACE... */
        case 0x00000c82: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_IvyBridge ? "I7_IB_UNK_0000_0c82"  : NULL; /* Probably related to IA32_DEBUG_INTERFACE... */
        case 0x00000c83: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_IvyBridge ? "I7_IB_UNK_0000_0c83"  : NULL; /* Probably related to IA32_DEBUG_INTERFACE... */

        /* 0x1000..0x1004 seems to have been used by IBM 386 and 486 clones too. */
        case 0x00001000: return "P6_DEBUG_REGISTER_0";
        case 0x00001001: return "P6_DEBUG_REGISTER_1";
        case 0x00001002: return "P6_DEBUG_REGISTER_2";
        case 0x00001003: return "P6_DEBUG_REGISTER_3";
        case 0x00001004: return "P6_DEBUG_REGISTER_4";
        case 0x00001005: return "P6_DEBUG_REGISTER_5";
        case 0x00001006: return "P6_DEBUG_REGISTER_6";
        case 0x00001007: return "P6_DEBUG_REGISTER_7";
        case 0x0000103f: return "P6_UNK_0000_103f"; /* P6_M_Dothan. */
        case 0x000010cd: return "P6_UNK_0000_10cd"; /* P6_M_Dothan. */

        case 0x00001107: return "VIA_UNK_0000_1107";
        case 0x0000110f: return "VIA_UNK_0000_110f";
        case 0x00001153: return "VIA_UNK_0000_1153";
        case 0x00001200: return "VIA_UNK_0000_1200";
        case 0x00001201: return "VIA_UNK_0000_1201";
        case 0x00001202: return "VIA_UNK_0000_1202";
        case 0x00001203: return "VIA_UNK_0000_1203";
        case 0x00001204: return "VIA_UNK_0000_1204";
        case 0x00001205: return "VIA_UNK_0000_1205";
        case 0x00001206: return "VIA_ALT_VENDOR_EBX";
        case 0x00001207: return "VIA_ALT_VENDOR_ECDX";
        case 0x00001208: return "VIA_UNK_0000_1208";
        case 0x00001209: return "VIA_UNK_0000_1209";
        case 0x0000120a: return "VIA_UNK_0000_120a";
        case 0x0000120b: return "VIA_UNK_0000_120b";
        case 0x0000120c: return "VIA_UNK_0000_120c";
        case 0x0000120d: return "VIA_UNK_0000_120d";
        case 0x0000120e: return "VIA_UNK_0000_120e";
        case 0x0000120f: return "VIA_UNK_0000_120f";
        case 0x00001210: return "VIA_UNK_0000_1210";
        case 0x00001211: return "VIA_UNK_0000_1211";
        case 0x00001212: return "VIA_UNK_0000_1212";
        case 0x00001213: return "VIA_UNK_0000_1213";
        case 0x00001214: return "VIA_UNK_0000_1214";
        case 0x00001220: return "VIA_UNK_0000_1220";
        case 0x00001221: return "VIA_UNK_0000_1221";
        case 0x00001230: return "VIA_UNK_0000_1230";
        case 0x00001231: return "VIA_UNK_0000_1231";
        case 0x00001232: return "VIA_UNK_0000_1232";
        case 0x00001233: return "VIA_UNK_0000_1233";
        case 0x00001234: return "VIA_UNK_0000_1234";
        case 0x00001235: return "VIA_UNK_0000_1235";
        case 0x00001236: return "VIA_UNK_0000_1236";
        case 0x00001237: return "VIA_UNK_0000_1237";
        case 0x00001238: return "VIA_UNK_0000_1238";
        case 0x00001239: return "VIA_UNK_0000_1239";
        case 0x00001240: return "VIA_UNK_0000_1240";
        case 0x00001241: return "VIA_UNK_0000_1241";
        case 0x00001243: return "VIA_UNK_0000_1243";
        case 0x00001245: return "VIA_UNK_0000_1245";
        case 0x00001246: return "VIA_UNK_0000_1246";
        case 0x00001247: return "VIA_UNK_0000_1247";
        case 0x00001248: return "VIA_UNK_0000_1248";
        case 0x00001249: return "VIA_UNK_0000_1249";
        case 0x0000124a: return "VIA_UNK_0000_124a";

        case 0x00001301: return "VIA_UNK_0000_1301";
        case 0x00001302: return "VIA_UNK_0000_1302";
        case 0x00001303: return "VIA_UNK_0000_1303";
        case 0x00001304: return "VIA_UNK_0000_1304";
        case 0x00001305: return "VIA_UNK_0000_1305";
        case 0x00001306: return "VIA_UNK_0000_1306";
        case 0x00001307: return "VIA_UNK_0000_1307";
        case 0x00001308: return "VIA_UNK_0000_1308";
        case 0x00001309: return "VIA_UNK_0000_1309";
        case 0x0000130d: return "VIA_UNK_0000_130d";
        case 0x0000130e: return "VIA_UNK_0000_130e";
        case 0x00001312: return "VIA_UNK_0000_1312";
        case 0x00001315: return "VIA_UNK_0000_1315";
        case 0x00001317: return "VIA_UNK_0000_1317";
        case 0x00001318: return "VIA_UNK_0000_1318";
        case 0x0000131a: return "VIA_UNK_0000_131a";
        case 0x0000131b: return "VIA_UNK_0000_131b";
        case 0x00001402: return "VIA_UNK_0000_1402";
        case 0x00001403: return "VIA_UNK_0000_1403";
        case 0x00001404: return "VIA_UNK_0000_1404";
        case 0x00001405: return "VIA_UNK_0000_1405";
        case 0x00001406: return "VIA_UNK_0000_1406";
        case 0x00001407: return "VIA_UNK_0000_1407";
        case 0x00001410: return "VIA_UNK_0000_1410";
        case 0x00001411: return "VIA_UNK_0000_1411";
        case 0x00001412: return "VIA_UNK_0000_1412";
        case 0x00001413: return "VIA_UNK_0000_1413";
        case 0x00001414: return "VIA_UNK_0000_1414";
        case 0x00001415: return "VIA_UNK_0000_1415";
        case 0x00001416: return "VIA_UNK_0000_1416";
        case 0x00001417: return "VIA_UNK_0000_1417";
        case 0x00001418: return "VIA_UNK_0000_1418";
        case 0x00001419: return "VIA_UNK_0000_1419";
        case 0x0000141a: return "VIA_UNK_0000_141a";
        case 0x0000141b: return "VIA_UNK_0000_141b";
        case 0x0000141c: return "VIA_UNK_0000_141c";
        case 0x0000141d: return "VIA_UNK_0000_141d";
        case 0x0000141e: return "VIA_UNK_0000_141e";
        case 0x0000141f: return "VIA_UNK_0000_141f";
        case 0x00001420: return "VIA_UNK_0000_1420";
        case 0x00001421: return "VIA_UNK_0000_1421";
        case 0x00001422: return "VIA_UNK_0000_1422";
        case 0x00001423: return "VIA_UNK_0000_1423";
        case 0x00001424: return "VIA_UNK_0000_1424";
        case 0x00001425: return "VIA_UNK_0000_1425";
        case 0x00001426: return "VIA_UNK_0000_1426";
        case 0x00001427: return "VIA_UNK_0000_1427";
        case 0x00001428: return "VIA_UNK_0000_1428";
        case 0x00001429: return "VIA_UNK_0000_1429";
        case 0x0000142a: return "VIA_UNK_0000_142a";
        case 0x0000142b: return "VIA_UNK_0000_142b";
        case 0x0000142c: return "VIA_UNK_0000_142c";
        case 0x0000142d: return "VIA_UNK_0000_142d";
        case 0x0000142e: return "VIA_UNK_0000_142e";
        case 0x0000142f: return "VIA_UNK_0000_142f";
        case 0x00001434: return "VIA_UNK_0000_1434";
        case 0x00001435: return "VIA_UNK_0000_1435";
        case 0x00001436: return "VIA_UNK_0000_1436";
        case 0x00001437: return "VIA_UNK_0000_1437";
        case 0x00001438: return "VIA_UNK_0000_1438";
        case 0x0000143a: return "VIA_UNK_0000_143a";
        case 0x0000143c: return "VIA_UNK_0000_143c";
        case 0x0000143d: return "VIA_UNK_0000_143d";
        case 0x00001440: return "VIA_UNK_0000_1440";
        case 0x00001441: return "VIA_UNK_0000_1441";
        case 0x00001442: return "VIA_UNK_0000_1442";
        case 0x00001449: return "VIA_UNK_0000_1449";
        case 0x00001450: return "VIA_UNK_0000_1450";
        case 0x00001451: return "VIA_UNK_0000_1451";
        case 0x00001452: return "VIA_UNK_0000_1452";
        case 0x00001453: return "VIA_UNK_0000_1453";
        case 0x00001460: return "VIA_UNK_0000_1460";
        case 0x00001461: return "VIA_UNK_0000_1461";
        case 0x00001462: return "VIA_UNK_0000_1462";
        case 0x00001463: return "VIA_UNK_0000_1463";
        case 0x00001465: return "VIA_UNK_0000_1465";
        case 0x00001466: return "VIA_UNK_0000_1466";
        case 0x00001470: return "VIA_UNK_0000_1470";
        case 0x00001471: return "VIA_UNK_0000_1471";
        case 0x00001480: return "VIA_UNK_0000_1480";
        case 0x00001481: return "VIA_UNK_0000_1481";
        case 0x00001482: return "VIA_UNK_0000_1482";
        case 0x00001483: return "VIA_UNK_0000_1483";
        case 0x00001484: return "VIA_UNK_0000_1484";
        case 0x00001485: return "VIA_UNK_0000_1485";
        case 0x00001486: return "VIA_UNK_0000_1486";
        case 0x00001490: return "VIA_UNK_0000_1490";
        case 0x00001491: return "VIA_UNK_0000_1491";
        case 0x00001492: return "VIA_UNK_0000_1492";
        case 0x00001493: return "VIA_UNK_0000_1493";
        case 0x00001494: return "VIA_UNK_0000_1494";
        case 0x00001495: return "VIA_UNK_0000_1495";
        case 0x00001496: return "VIA_UNK_0000_1496";
        case 0x00001497: return "VIA_UNK_0000_1497";
        case 0x00001498: return "VIA_UNK_0000_1498";
        case 0x00001499: return "VIA_UNK_0000_1499";
        case 0x0000149a: return "VIA_UNK_0000_149a";
        case 0x0000149b: return "VIA_UNK_0000_149b";
        case 0x0000149c: return "VIA_UNK_0000_149c";
        case 0x0000149f: return "VIA_UNK_0000_149f";
        case 0x00001523: return "VIA_UNK_0000_1523";

        case 0x00002000: return g_enmVendor == CPUMCPUVENDOR_INTEL ? "P6_CR0" : NULL;
        case 0x00002002: return g_enmVendor == CPUMCPUVENDOR_INTEL ? "P6_CR2" : NULL;
        case 0x00002003: return g_enmVendor == CPUMCPUVENDOR_INTEL ? "P6_CR3" : NULL;
        case 0x00002004: return g_enmVendor == CPUMCPUVENDOR_INTEL ? "P6_CR4" : NULL;
        case 0x0000203f: return g_enmVendor == CPUMCPUVENDOR_INTEL ? "P6_UNK_0000_203f" /* P6_M_Dothan. */ : NULL;
        case 0x000020cd: return g_enmVendor == CPUMCPUVENDOR_INTEL ? "P6_UNK_0000_20cd" /* P6_M_Dothan. */ : NULL;
        case 0x0000303f: return g_enmVendor == CPUMCPUVENDOR_INTEL ? "P6_UNK_0000_303f" /* P6_M_Dothan. */ : NULL;
        case 0x000030cd: return g_enmVendor == CPUMCPUVENDOR_INTEL ? "P6_UNK_0000_30cd" /* P6_M_Dothan. */ : NULL;

        case 0x0000317a: return "VIA_UNK_0000_317a";
        case 0x0000317b: return "VIA_UNK_0000_317b";
        case 0x0000317d: return "VIA_UNK_0000_317d";
        case 0x0000317e: return "VIA_UNK_0000_317e";
        case 0x0000317f: return "VIA_UNK_0000_317f";
        case 0x80000198: return "VIA_UNK_8000_0198";

        case 0xc0000080: return "AMD64_EFER";
        case 0xc0000081: return "AMD64_STAR";
        case 0xc0000082: return "AMD64_STAR64";
        case 0xc0000083: return "AMD64_STARCOMPAT";
        case 0xc0000084: return "AMD64_SYSCALL_FLAG_MASK";
        case 0xc0000100: return "AMD64_FS_BASE";
        case 0xc0000101: return "AMD64_GS_BASE";
        case 0xc0000102: return "AMD64_KERNEL_GS_BASE";
        case 0xc0000103: return "AMD64_TSC_AUX";
        case 0xc0000104: return "AMD_15H_TSC_RATE";
        case 0xc0000105: return "AMD_15H_LWP_CFG";      /* Only Family 15h? */
        case 0xc0000106: return "AMD_15H_LWP_CBADDR";   /* Only Family 15h? */
        case 0xc0000408: return "AMD_10H_MC4_MISC1";
        case 0xc0000409: return "AMD_10H_MC4_MISC2";
        case 0xc000040a: return "AMD_10H_MC4_MISC3";
        case 0xc000040b: return "AMD_10H_MC4_MISC4";
        case 0xc000040c: return "AMD_10H_MC4_MISC5";
        case 0xc000040d: return "AMD_10H_MC4_MISC6";
        case 0xc000040e: return "AMD_10H_MC4_MISC7";
        case 0xc000040f: return "AMD_10H_MC4_MISC8";
        case 0xc0010000: return "AMD_K8_PERF_CTL_0";
        case 0xc0010001: return "AMD_K8_PERF_CTL_1";
        case 0xc0010002: return "AMD_K8_PERF_CTL_2";
        case 0xc0010003: return "AMD_K8_PERF_CTL_3";
        case 0xc0010004: return "AMD_K8_PERF_CTR_0";
        case 0xc0010005: return "AMD_K8_PERF_CTR_1";
        case 0xc0010006: return "AMD_K8_PERF_CTR_2";
        case 0xc0010007: return "AMD_K8_PERF_CTR_3";
        case 0xc0010010: return "AMD_K8_SYS_CFG";
        case 0xc0010015: return "AMD_K8_HW_CFG";
        case 0xc0010016: return "AMD_K8_IORR_BASE_0";
        case 0xc0010017: return "AMD_K8_IORR_MASK_0";
        case 0xc0010018: return "AMD_K8_IORR_BASE_1";
        case 0xc0010019: return "AMD_K8_IORR_MASK_1";
        case 0xc001001a: return "AMD_K8_TOP_MEM";
        case 0xc001001d: return "AMD_K8_TOP_MEM2";
        case 0xc001001e: return "AMD_K8_MANID";
        case 0xc001001f: return "AMD_K8_NB_CFG1";
        case 0xc0010020: return "AMD_K8_PATCH_LOADER";
        case 0xc0010021: return "AMD_K8_UNK_c001_0021";
        case 0xc0010022: return "AMD_K8_MC_XCPT_REDIR";
        case 0xc0010028: return "AMD_K8_UNK_c001_0028";
        case 0xc0010029: return "AMD_K8_UNK_c001_0029";
        case 0xc001002a: return "AMD_K8_UNK_c001_002a";
        case 0xc001002b: return "AMD_K8_UNK_c001_002b";
        case 0xc001002c: return "AMD_K8_UNK_c001_002c";
        case 0xc001002d: return "AMD_K8_UNK_c001_002d";
        case 0xc0010030: return "AMD_K8_CPU_NAME_0";
        case 0xc0010031: return "AMD_K8_CPU_NAME_1";
        case 0xc0010032: return "AMD_K8_CPU_NAME_2";
        case 0xc0010033: return "AMD_K8_CPU_NAME_3";
        case 0xc0010034: return "AMD_K8_CPU_NAME_4";
        case 0xc0010035: return "AMD_K8_CPU_NAME_5";
        case 0xc001003e: return "AMD_K8_HTC";
        case 0xc001003f: return "AMD_K8_STC";
        case 0xc0010041: return "AMD_K8_FIDVID_CTL";
        case 0xc0010042: return "AMD_K8_FIDVID_STATUS";
        case 0xc0010043: return "AMD_K8_THERMTRIP_STATUS"; /* BDKG says it was removed in K8 revision C.*/
        case 0xc0010044: return "AMD_K8_MC_CTL_MASK_0";
        case 0xc0010045: return "AMD_K8_MC_CTL_MASK_1";
        case 0xc0010046: return "AMD_K8_MC_CTL_MASK_2";
        case 0xc0010047: return "AMD_K8_MC_CTL_MASK_3";
        case 0xc0010048: return "AMD_K8_MC_CTL_MASK_4";
        case 0xc0010049: return "AMD_K8_MC_CTL_MASK_5";
        case 0xc001004a: return "AMD_K8_MC_CTL_MASK_6";
        //case 0xc001004b: return "AMD_K8_MC_CTL_MASK_7";
        case 0xc0010050: return "AMD_K8_SMI_ON_IO_TRAP_0";
        case 0xc0010051: return "AMD_K8_SMI_ON_IO_TRAP_1";
        case 0xc0010052: return "AMD_K8_SMI_ON_IO_TRAP_2";
        case 0xc0010053: return "AMD_K8_SMI_ON_IO_TRAP_3";
        case 0xc0010054: return "AMD_K8_SMI_ON_IO_TRAP_CTL_STS";
        case 0xc0010055: return "AMD_K8_INT_PENDING_MSG";
        case 0xc0010056: return "AMD_K8_SMI_TRIGGER_IO_CYCLE";
        case 0xc0010057: return "AMD_10H_UNK_c001_0057";
        case 0xc0010058: return "AMD_10H_MMIO_CFG_BASE_ADDR";
        case 0xc0010059: return "AMD_10H_TRAP_CTL?"; /* Undocumented, only one google hit. */
        case 0xc001005a: return "AMD_10H_UNK_c001_005a";
        case 0xc001005b: return "AMD_10H_UNK_c001_005b";
        case 0xc001005c: return "AMD_10H_UNK_c001_005c";
        case 0xc001005d: return "AMD_10H_UNK_c001_005d";
        case 0xc0010060: return "AMD_K8_BIST_RESULT";   /* BDKG says it as introduced with revision F. */
        case 0xc0010061: return "AMD_10H_P_ST_CUR_LIM";
        case 0xc0010062: return "AMD_10H_P_ST_CTL";
        case 0xc0010063: return "AMD_10H_P_ST_STS";
        case 0xc0010064: return "AMD_10H_P_ST_0";
        case 0xc0010065: return "AMD_10H_P_ST_1";
        case 0xc0010066: return "AMD_10H_P_ST_2";
        case 0xc0010067: return "AMD_10H_P_ST_3";
        case 0xc0010068: return "AMD_10H_P_ST_4";
        case 0xc0010069: return "AMD_10H_P_ST_5";
        case 0xc001006a: return "AMD_10H_P_ST_6";
        case 0xc001006b: return "AMD_10H_P_ST_7";
        case 0xc0010070: return "AMD_10H_COFVID_CTL";
        case 0xc0010071: return "AMD_10H_COFVID_STS";
        case 0xc0010073: return "AMD_10H_C_ST_IO_BASE_ADDR";
        case 0xc0010074: return "AMD_10H_CPU_WD_TMR_CFG";
        // case 0xc0010075: return "AMD_15H_APML_TDP_LIM";
        // case 0xc0010077: return "AMD_15H_CPU_PWR_IN_TDP";
        // case 0xc0010078: return "AMD_15H_PWR_AVG_PERIOD";
        // case 0xc0010079: return "AMD_15H_DRAM_CTR_CMD_THR";
        // case 0xc0010080: return "AMD_16H_FSFM_ACT_CNT_0";
        // case 0xc0010081: return "AMD_16H_FSFM_REF_CNT_0";
        case 0xc0010111: return "AMD_K8_SMM_BASE";
        case 0xc0010112: return "AMD_K8_SMM_ADDR";
        case 0xc0010113: return "AMD_K8_SMM_MASK";
        case 0xc0010114: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm_AMDV ? "AMD_K8_VM_CR"          : "AMD_K8_UNK_c001_0114";
        case 0xc0010115: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm      ? "AMD_K8_IGNNE"          : "AMD_K8_UNK_c001_0115";
        case 0xc0010116: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm      ? "AMD_K8_SMM_CTL"        : "AMD_K8_UNK_c001_0116";
        case 0xc0010117: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm_AMDV ? "AMD_K8_VM_HSAVE_PA"    : "AMD_K8_UNK_c001_0117";
        case 0xc0010118: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm_AMDV ? "AMD_10H_VM_LOCK_KEY"   : "AMD_K8_UNK_c001_0118";
        case 0xc0010119: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm      ? "AMD_10H_SSM_LOCK_KEY"  : "AMD_K8_UNK_c001_0119";
        case 0xc001011a: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm      ? "AMD_10H_LOCAL_SMI_STS" : "AMD_K8_UNK_c001_011a";
        case 0xc001011b: return "AMD_K8_UNK_c001_011b";
        case 0xc001011c: return "AMD_K8_UNK_c001_011c";
        case 0xc0010140: return "AMD_10H_OSVW_ID_LEN";
        case 0xc0010141: return "AMD_10H_OSVW_STS";
        case 0xc0010200: return "AMD_K8_PERF_CTL_0";
        case 0xc0010202: return "AMD_K8_PERF_CTL_1";
        case 0xc0010204: return "AMD_K8_PERF_CTL_2";
        case 0xc0010206: return "AMD_K8_PERF_CTL_3";
        case 0xc0010208: return "AMD_K8_PERF_CTL_4";
        case 0xc001020a: return "AMD_K8_PERF_CTL_5";
        //case 0xc001020c: return "AMD_K8_PERF_CTL_6";
        //case 0xc001020e: return "AMD_K8_PERF_CTL_7";
        case 0xc0010201: return "AMD_K8_PERF_CTR_0";
        case 0xc0010203: return "AMD_K8_PERF_CTR_1";
        case 0xc0010205: return "AMD_K8_PERF_CTR_2";
        case 0xc0010207: return "AMD_K8_PERF_CTR_3";
        case 0xc0010209: return "AMD_K8_PERF_CTR_4";
        case 0xc001020b: return "AMD_K8_PERF_CTR_5";
        //case 0xc001020d: return "AMD_K8_PERF_CTR_6";
        //case 0xc001020f: return "AMD_K8_PERF_CTR_7";
        case 0xc0010230: return "AMD_16H_L2I_PERF_CTL_0";
        case 0xc0010232: return "AMD_16H_L2I_PERF_CTL_1";
        case 0xc0010234: return "AMD_16H_L2I_PERF_CTL_2";
        case 0xc0010236: return "AMD_16H_L2I_PERF_CTL_3";
        //case 0xc0010238: return "AMD_16H_L2I_PERF_CTL_4";
        //case 0xc001023a: return "AMD_16H_L2I_PERF_CTL_5";
        //case 0xc001030c: return "AMD_16H_L2I_PERF_CTL_6";
        //case 0xc001023e: return "AMD_16H_L2I_PERF_CTL_7";
        case 0xc0010231: return "AMD_16H_L2I_PERF_CTR_0";
        case 0xc0010233: return "AMD_16H_L2I_PERF_CTR_1";
        case 0xc0010235: return "AMD_16H_L2I_PERF_CTR_2";
        case 0xc0010237: return "AMD_16H_L2I_PERF_CTR_3";
        //case 0xc0010239: return "AMD_16H_L2I_PERF_CTR_4";
        //case 0xc001023b: return "AMD_16H_L2I_PERF_CTR_5";
        //case 0xc001023d: return "AMD_16H_L2I_PERF_CTR_6";
        //case 0xc001023f: return "AMD_16H_L2I_PERF_CTR_7";
        case 0xc0010240: return "AMD_15H_NB_PERF_CTL_0";
        case 0xc0010242: return "AMD_15H_NB_PERF_CTL_1";
        case 0xc0010244: return "AMD_15H_NB_PERF_CTL_2";
        case 0xc0010246: return "AMD_15H_NB_PERF_CTL_3";
        //case 0xc0010248: return "AMD_15H_NB_PERF_CTL_4";
        //case 0xc001024a: return "AMD_15H_NB_PERF_CTL_5";
        //case 0xc001024c: return "AMD_15H_NB_PERF_CTL_6";
        //case 0xc001024e: return "AMD_15H_NB_PERF_CTL_7";
        case 0xc0010241: return "AMD_15H_NB_PERF_CTR_0";
        case 0xc0010243: return "AMD_15H_NB_PERF_CTR_1";
        case 0xc0010245: return "AMD_15H_NB_PERF_CTR_2";
        case 0xc0010247: return "AMD_15H_NB_PERF_CTR_3";
        //case 0xc0010249: return "AMD_15H_NB_PERF_CTR_4";
        //case 0xc001024b: return "AMD_15H_NB_PERF_CTR_5";
        //case 0xc001024d: return "AMD_15H_NB_PERF_CTR_6";
        //case 0xc001024f: return "AMD_15H_NB_PERF_CTR_7";
        case 0xc0011000: return "AMD_K7_MCODE_CTL";
        case 0xc0011001: return "AMD_K7_APIC_CLUSTER_ID"; /* Mentioned in BKDG (r3.00) for fam16h when describing EBL_CR_POWERON. */
        case 0xc0011002: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_CPUID_CTL_STD07" : NULL;
        case 0xc0011003: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_CPUID_CTL_STD06" : NULL;
        case 0xc0011004: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_CPUID_CTL_STD01" : NULL;
        case 0xc0011005: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_CPUID_CTL_EXT01" : NULL;
        case 0xc0011006: return "AMD_K7_DEBUG_STS?";
        case 0xc0011007: return "AMD_K7_BH_TRACE_BASE?";
        case 0xc0011008: return "AMD_K7_BH_TRACE_PTR?";
        case 0xc0011009: return "AMD_K7_BH_TRACE_LIM?";
        case 0xc001100a: return "AMD_K7_HDT_CFG?";
        case 0xc001100b: return "AMD_K7_FAST_FLUSH_COUNT?";
        case 0xc001100c: return "AMD_K7_NODE_ID";
        case 0xc001100d: return "AMD_K8_LOGICAL_CPUS_NUM?";
        case 0xc001100e: return "AMD_K8_WRMSR_BP?";
        case 0xc001100f: return "AMD_K8_WRMSR_BP_MASK?";
        case 0xc0011010: return "AMD_K8_BH_TRACE_CTL?";
        case 0xc0011011: return "AMD_K8_BH_TRACE_USRD?";
        case 0xc0011012: return "AMD_K7_UNK_c001_1012";
        case 0xc0011013: return "AMD_K7_UNK_c001_1013";
        case 0xc0011014: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_XCPT_BP_RIP?" : "AMD_K7_MOBIL_DEBUG?";
        case 0xc0011015: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_XCPT_BP_RIP_MASK?" : NULL;
        case 0xc0011016: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_COND_HDT_VAL?" : NULL;
        case 0xc0011017: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_COND_HDT_VAL_MASK?" : NULL;
        case 0xc0011018: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_XCPT_BP_CTL?" : NULL;
        case 0xc0011019: return g_enmMicroarch >= kCpumMicroarch_AMD_15h_Piledriver ? "AMD_16H_DR1_ADDR_MASK" : NULL;
        case 0xc001101a: return g_enmMicroarch >= kCpumMicroarch_AMD_15h_Piledriver ? "AMD_16H_DR2_ADDR_MASK" : NULL;
        case 0xc001101b: return g_enmMicroarch >= kCpumMicroarch_AMD_15h_Piledriver ? "AMD_16H_DR3_ADDR_MASK" : NULL;
        case 0xc001101d: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_NB_BIST?" : NULL;
        case 0xc001101e: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_THERMTRIP_2?" : NULL;
        case 0xc001101f: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AMD_K8_NB_CFG?" : NULL;
        case 0xc0011020: return "AMD_K7_LS_CFG";
        case 0xc0011021: return "AMD_K7_IC_CFG";
        case 0xc0011022: return "AMD_K7_DC_CFG";
        case 0xc0011023: return CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch) ? "AMD_15H_CU_CFG" : "AMD_K7_BU_CFG";
        case 0xc0011024: return "AMD_K7_DEBUG_CTL_2?";
        case 0xc0011025: return "AMD_K7_DR0_DATA_MATCH?";
        case 0xc0011026: return "AMD_K7_DR0_DATA_MATCH?";
        case 0xc0011027: return "AMD_K7_DR0_ADDR_MASK";
        case 0xc0011028: return g_enmMicroarch >= kCpumMicroarch_AMD_15h_First ? "AMD_15H_FP_CFG"
                              : CPUMMICROARCH_IS_AMD_FAM_10H(g_enmMicroarch)   ? "AMD_10H_UNK_c001_1028"
                              : NULL;
        case 0xc0011029: return g_enmMicroarch >= kCpumMicroarch_AMD_15h_First ? "AMD_15H_DC_CFG"
                              : CPUMMICROARCH_IS_AMD_FAM_10H(g_enmMicroarch)   ? "AMD_10H_UNK_c001_1029"
                              : NULL;
        case 0xc001102a: return CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch)   ? "AMD_15H_CU_CFG2"
                              : CPUMMICROARCH_IS_AMD_FAM_10H(g_enmMicroarch) || g_enmMicroarch > kCpumMicroarch_AMD_15h_End
                              ? "AMD_10H_BU_CFG2" /* 10h & 16h */ : NULL;
        case 0xc001102b: return CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch)   ? "AMD_15H_CU_CFG3" : NULL;
        case 0xc001102c: return CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch)   ? "AMD_15H_EX_CFG" : NULL;
        case 0xc001102d: return CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch)   ? "AMD_15H_LS_CFG2" : NULL;
        case 0xc0011030: return "AMD_10H_IBS_FETCH_CTL";
        case 0xc0011031: return "AMD_10H_IBS_FETCH_LIN_ADDR";
        case 0xc0011032: return "AMD_10H_IBS_FETCH_PHYS_ADDR";
        case 0xc0011033: return "AMD_10H_IBS_OP_EXEC_CTL";
        case 0xc0011034: return "AMD_10H_IBS_OP_RIP";
        case 0xc0011035: return "AMD_10H_IBS_OP_DATA";
        case 0xc0011036: return "AMD_10H_IBS_OP_DATA2";
        case 0xc0011037: return "AMD_10H_IBS_OP_DATA3";
        case 0xc0011038: return "AMD_10H_IBS_DC_LIN_ADDR";
        case 0xc0011039: return "AMD_10H_IBS_DC_PHYS_ADDR";
        case 0xc001103a: return "AMD_10H_IBS_CTL";
        case 0xc001103b: return "AMD_14H_IBS_BR_TARGET";

        case 0xc0011040: return "AMD_15H_UNK_c001_1040";
        case 0xc0011041: return "AMD_15H_UNK_c001_1041";
        case 0xc0011042: return "AMD_15H_UNK_c001_1042";
        case 0xc0011043: return "AMD_15H_UNK_c001_1043";
        case 0xc0011044: return "AMD_15H_UNK_c001_1044";
        case 0xc0011045: return "AMD_15H_UNK_c001_1045";
        case 0xc0011046: return "AMD_15H_UNK_c001_1046";
        case 0xc0011047: return "AMD_15H_UNK_c001_1047";
        case 0xc0011048: return "AMD_15H_UNK_c001_1048";
        case 0xc0011049: return "AMD_15H_UNK_c001_1049";
        case 0xc001104a: return "AMD_15H_UNK_c001_104a";
        case 0xc001104b: return "AMD_15H_UNK_c001_104b";
        case 0xc001104c: return "AMD_15H_UNK_c001_104c";
        case 0xc001104d: return "AMD_15H_UNK_c001_104d";
        case 0xc001104e: return "AMD_15H_UNK_c001_104e";
        case 0xc001104f: return "AMD_15H_UNK_c001_104f";
        case 0xc0011050: return "AMD_15H_UNK_c001_1050";
        case 0xc0011051: return "AMD_15H_UNK_c001_1051";
        case 0xc0011052: return "AMD_15H_UNK_c001_1052";
        case 0xc0011053: return "AMD_15H_UNK_c001_1053";
        case 0xc0011054: return "AMD_15H_UNK_c001_1054";
        case 0xc0011055: return "AMD_15H_UNK_c001_1055";
        case 0xc0011056: return "AMD_15H_UNK_c001_1056";
        case 0xc0011057: return "AMD_15H_UNK_c001_1057";
        case 0xc0011058: return "AMD_15H_UNK_c001_1058";
        case 0xc0011059: return "AMD_15H_UNK_c001_1059";
        case 0xc001105a: return "AMD_15H_UNK_c001_105a";
        case 0xc001105b: return "AMD_15H_UNK_c001_105b";
        case 0xc001105c: return "AMD_15H_UNK_c001_105c";
        case 0xc001105d: return "AMD_15H_UNK_c001_105d";
        case 0xc001105e: return "AMD_15H_UNK_c001_105e";
        case 0xc001105f: return "AMD_15H_UNK_c001_105f";
        case 0xc0011060: return "AMD_15H_UNK_c001_1060";
        case 0xc0011061: return "AMD_15H_UNK_c001_1061";
        case 0xc0011062: return "AMD_15H_UNK_c001_1062";
        case 0xc0011063: return "AMD_15H_UNK_c001_1063";
        case 0xc0011064: return "AMD_15H_UNK_c001_1064";
        case 0xc0011065: return "AMD_15H_UNK_c001_1065";
        case 0xc0011066: return "AMD_15H_UNK_c001_1066";
        case 0xc0011067: return "AMD_15H_UNK_c001_1067";
        case 0xc0011068: return "AMD_15H_UNK_c001_1068";
        case 0xc0011069: return "AMD_15H_UNK_c001_1069";
        case 0xc001106a: return "AMD_15H_UNK_c001_106a";
        case 0xc001106b: return "AMD_15H_UNK_c001_106b";
        case 0xc001106c: return "AMD_15H_UNK_c001_106c";
        case 0xc001106d: return "AMD_15H_UNK_c001_106d";
        case 0xc001106e: return "AMD_15H_UNK_c001_106e";
        case 0xc001106f: return "AMD_15H_UNK_c001_106f";
        case 0xc0011070: return "AMD_15H_UNK_c001_1070"; /* coreboot defines this, but with a numerical name. */
        case 0xc0011071: return "AMD_15H_UNK_c001_1071";
        case 0xc0011072: return "AMD_15H_UNK_c001_1072";
        case 0xc0011073: return "AMD_15H_UNK_c001_1073";
        case 0xc0011080: return "AMD_15H_UNK_c001_1080";
    }

    /*
     * Uncore stuff on Sandy. Putting it here to avoid ugly microarch checks for each register.
     * Note! These are found on model 42 (2a) but not 45 (2d), the latter is the EP variant.
     */
    if (g_enmMicroarch == kCpumMicroarch_Intel_Core7_SandyBridge)
        switch (uMsr)
        {
            case 0x00000700: return "MSR_UNC_CBO_0_PERFEVTSEL0";
            case 0x00000701: return "MSR_UNC_CBO_0_PERFEVTSEL1";
            case 0x00000702: return "MSR_UNC_CBO_0_PERFEVTSEL2?";
            case 0x00000703: return "MSR_UNC_CBO_0_PERFEVTSEL3?";
            case 0x00000704: return "MSR_UNC_CBO_0_UNK_4";
            case 0x00000705: return "MSR_UNC_CBO_0_UNK_5";
            case 0x00000706: return "MSR_UNC_CBO_0_PER_CTR0";
            case 0x00000707: return "MSR_UNC_CBO_0_PER_CTR1";
            case 0x00000708: return "MSR_UNC_CBO_0_PER_CTR2?";
            case 0x00000709: return "MSR_UNC_CBO_0_PER_CTR3?";
            case 0x00000710: return "MSR_UNC_CBO_1_PERFEVTSEL0";
            case 0x00000711: return "MSR_UNC_CBO_1_PERFEVTSEL1";
            case 0x00000712: return "MSR_UNC_CBO_1_PERFEVTSEL2?";
            case 0x00000713: return "MSR_UNC_CBO_1_PERFEVTSEL3?";
            case 0x00000714: return "MSR_UNC_CBO_1_UNK_4";
            case 0x00000715: return "MSR_UNC_CBO_1_UNK_5";
            case 0x00000716: return "MSR_UNC_CBO_1_PER_CTR0";
            case 0x00000717: return "MSR_UNC_CBO_1_PER_CTR1";
            case 0x00000718: return "MSR_UNC_CBO_1_PER_CTR2?";
            case 0x00000719: return "MSR_UNC_CBO_1_PER_CTR3?";
            case 0x00000720: return "MSR_UNC_CBO_2_PERFEVTSEL0";
            case 0x00000721: return "MSR_UNC_CBO_2_PERFEVTSEL1";
            case 0x00000722: return "MSR_UNC_CBO_2_PERFEVTSEL2?";
            case 0x00000723: return "MSR_UNC_CBO_2_PERFEVTSEL3?";
            case 0x00000724: return "MSR_UNC_CBO_2_UNK_4";
            case 0x00000725: return "MSR_UNC_CBO_2_UNK_5";
            case 0x00000726: return "MSR_UNC_CBO_2_PER_CTR0";
            case 0x00000727: return "MSR_UNC_CBO_2_PER_CTR1";
            case 0x00000728: return "MSR_UNC_CBO_2_PER_CTR2?";
            case 0x00000729: return "MSR_UNC_CBO_2_PER_CTR3?";
            case 0x00000730: return "MSR_UNC_CBO_3_PERFEVTSEL0";
            case 0x00000731: return "MSR_UNC_CBO_3_PERFEVTSEL1";
            case 0x00000732: return "MSR_UNC_CBO_3_PERFEVTSEL2?";
            case 0x00000733: return "MSR_UNC_CBO_3_PERFEVTSEL3?";
            case 0x00000734: return "MSR_UNC_CBO_3_UNK_4";
            case 0x00000735: return "MSR_UNC_CBO_3_UNK_5";
            case 0x00000736: return "MSR_UNC_CBO_3_PER_CTR0";
            case 0x00000737: return "MSR_UNC_CBO_3_PER_CTR1";
            case 0x00000738: return "MSR_UNC_CBO_3_PER_CTR2?";
            case 0x00000739: return "MSR_UNC_CBO_3_PER_CTR3?";
            case 0x00000740: return "MSR_UNC_CBO_4_PERFEVTSEL0?";
            case 0x00000741: return "MSR_UNC_CBO_4_PERFEVTSEL1?";
            case 0x00000742: return "MSR_UNC_CBO_4_PERFEVTSEL2?";
            case 0x00000743: return "MSR_UNC_CBO_4_PERFEVTSEL3?";
            case 0x00000744: return "MSR_UNC_CBO_4_UNK_4";
            case 0x00000745: return "MSR_UNC_CBO_4_UNK_5";
            case 0x00000746: return "MSR_UNC_CBO_4_PER_CTR0?";
            case 0x00000747: return "MSR_UNC_CBO_4_PER_CTR1?";
            case 0x00000748: return "MSR_UNC_CBO_4_PER_CTR2?";
            case 0x00000749: return "MSR_UNC_CBO_4_PER_CTR3?";

        }

    /*
     * Bunch of unknown sandy bridge registers.  They might seem like the
     * nehalem based xeon stuff, but the layout doesn't match.  I bet it's the
     * same kind of registes though (i.e. uncore (UNC)).
     *
     * Kudos to Intel for keeping these a secret!  Many thanks guys!!
     */
    if (g_enmMicroarch == kCpumMicroarch_Intel_Core7_SandyBridge)
        switch (uMsr)
        {
            case 0x00000a00: return "I7_SB_UNK_0000_0a00"; case 0x00000a01: return "I7_SB_UNK_0000_0a01";
            case 0x00000a02: return "I7_SB_UNK_0000_0a02";
            case 0x00000c00: return "I7_SB_UNK_0000_0c00"; case 0x00000c01: return "I7_SB_UNK_0000_0c01";
            case 0x00000c06: return "I7_SB_UNK_0000_0c06"; case 0x00000c08: return "I7_SB_UNK_0000_0c08";
            case 0x00000c09: return "I7_SB_UNK_0000_0c09"; case 0x00000c10: return "I7_SB_UNK_0000_0c10";
            case 0x00000c11: return "I7_SB_UNK_0000_0c11"; case 0x00000c14: return "I7_SB_UNK_0000_0c14";
            case 0x00000c15: return "I7_SB_UNK_0000_0c15"; case 0x00000c16: return "I7_SB_UNK_0000_0c16";
            case 0x00000c17: return "I7_SB_UNK_0000_0c17"; case 0x00000c24: return "I7_SB_UNK_0000_0c24";
            case 0x00000c30: return "I7_SB_UNK_0000_0c30"; case 0x00000c31: return "I7_SB_UNK_0000_0c31";
            case 0x00000c32: return "I7_SB_UNK_0000_0c32"; case 0x00000c33: return "I7_SB_UNK_0000_0c33";
            case 0x00000c34: return "I7_SB_UNK_0000_0c34"; case 0x00000c35: return "I7_SB_UNK_0000_0c35";
            case 0x00000c36: return "I7_SB_UNK_0000_0c36"; case 0x00000c37: return "I7_SB_UNK_0000_0c37";
            case 0x00000c38: return "I7_SB_UNK_0000_0c38"; case 0x00000c39: return "I7_SB_UNK_0000_0c39";
            case 0x00000d04: return "I7_SB_UNK_0000_0d04";
            case 0x00000d10: return "I7_SB_UNK_0000_0d10"; case 0x00000d11: return "I7_SB_UNK_0000_0d11";
            case 0x00000d12: return "I7_SB_UNK_0000_0d12"; case 0x00000d13: return "I7_SB_UNK_0000_0d13";
            case 0x00000d14: return "I7_SB_UNK_0000_0d14"; case 0x00000d15: return "I7_SB_UNK_0000_0d15";
            case 0x00000d16: return "I7_SB_UNK_0000_0d16"; case 0x00000d17: return "I7_SB_UNK_0000_0d17";
            case 0x00000d18: return "I7_SB_UNK_0000_0d18"; case 0x00000d19: return "I7_SB_UNK_0000_0d19";
            case 0x00000d24: return "I7_SB_UNK_0000_0d24";
            case 0x00000d30: return "I7_SB_UNK_0000_0d30"; case 0x00000d31: return "I7_SB_UNK_0000_0d31";
            case 0x00000d32: return "I7_SB_UNK_0000_0d32"; case 0x00000d33: return "I7_SB_UNK_0000_0d33";
            case 0x00000d34: return "I7_SB_UNK_0000_0d34"; case 0x00000d35: return "I7_SB_UNK_0000_0d35";
            case 0x00000d36: return "I7_SB_UNK_0000_0d36"; case 0x00000d37: return "I7_SB_UNK_0000_0d37";
            case 0x00000d38: return "I7_SB_UNK_0000_0d38"; case 0x00000d39: return "I7_SB_UNK_0000_0d39";
            case 0x00000d44: return "I7_SB_UNK_0000_0d44";
            case 0x00000d50: return "I7_SB_UNK_0000_0d50"; case 0x00000d51: return "I7_SB_UNK_0000_0d51";
            case 0x00000d52: return "I7_SB_UNK_0000_0d52"; case 0x00000d53: return "I7_SB_UNK_0000_0d53";
            case 0x00000d54: return "I7_SB_UNK_0000_0d54"; case 0x00000d55: return "I7_SB_UNK_0000_0d55";
            case 0x00000d56: return "I7_SB_UNK_0000_0d56"; case 0x00000d57: return "I7_SB_UNK_0000_0d57";
            case 0x00000d58: return "I7_SB_UNK_0000_0d58"; case 0x00000d59: return "I7_SB_UNK_0000_0d59";
            case 0x00000d64: return "I7_SB_UNK_0000_0d64";
            case 0x00000d70: return "I7_SB_UNK_0000_0d70"; case 0x00000d71: return "I7_SB_UNK_0000_0d71";
            case 0x00000d72: return "I7_SB_UNK_0000_0d72"; case 0x00000d73: return "I7_SB_UNK_0000_0d73";
            case 0x00000d74: return "I7_SB_UNK_0000_0d74"; case 0x00000d75: return "I7_SB_UNK_0000_0d75";
            case 0x00000d76: return "I7_SB_UNK_0000_0d76"; case 0x00000d77: return "I7_SB_UNK_0000_0d77";
            case 0x00000d78: return "I7_SB_UNK_0000_0d78"; case 0x00000d79: return "I7_SB_UNK_0000_0d79";
            case 0x00000d84: return "I7_SB_UNK_0000_0d84";
            case 0x00000d90: return "I7_SB_UNK_0000_0d90"; case 0x00000d91: return "I7_SB_UNK_0000_0d91";
            case 0x00000d92: return "I7_SB_UNK_0000_0d92"; case 0x00000d93: return "I7_SB_UNK_0000_0d93";
            case 0x00000d94: return "I7_SB_UNK_0000_0d94"; case 0x00000d95: return "I7_SB_UNK_0000_0d95";
            case 0x00000d96: return "I7_SB_UNK_0000_0d96"; case 0x00000d97: return "I7_SB_UNK_0000_0d97";
            case 0x00000d98: return "I7_SB_UNK_0000_0d98"; case 0x00000d99: return "I7_SB_UNK_0000_0d99";
            case 0x00000da4: return "I7_SB_UNK_0000_0da4";
            case 0x00000db0: return "I7_SB_UNK_0000_0db0"; case 0x00000db1: return "I7_SB_UNK_0000_0db1";
            case 0x00000db2: return "I7_SB_UNK_0000_0db2"; case 0x00000db3: return "I7_SB_UNK_0000_0db3";
            case 0x00000db4: return "I7_SB_UNK_0000_0db4"; case 0x00000db5: return "I7_SB_UNK_0000_0db5";
            case 0x00000db6: return "I7_SB_UNK_0000_0db6"; case 0x00000db7: return "I7_SB_UNK_0000_0db7";
            case 0x00000db8: return "I7_SB_UNK_0000_0db8"; case 0x00000db9: return "I7_SB_UNK_0000_0db9";
        }

    /*
     * Ditto for ivy bridge (observed on the i5-3570).  There are some haswell
     * and sandybridge related docs on registers in this ares, but either
     * things are different for ivy or they're very incomplete.  Again, kudos
     * to intel!
     */
    if (g_enmMicroarch == kCpumMicroarch_Intel_Core7_IvyBridge)
        switch (uMsr)
        {
            case 0x00000700: return "I7_IB_UNK_0000_0700"; case 0x00000701: return "I7_IB_UNK_0000_0701";
            case 0x00000702: return "I7_IB_UNK_0000_0702"; case 0x00000703: return "I7_IB_UNK_0000_0703";
            case 0x00000704: return "I7_IB_UNK_0000_0704"; case 0x00000705: return "I7_IB_UNK_0000_0705";
            case 0x00000706: return "I7_IB_UNK_0000_0706"; case 0x00000707: return "I7_IB_UNK_0000_0707";
            case 0x00000708: return "I7_IB_UNK_0000_0708"; case 0x00000709: return "I7_IB_UNK_0000_0709";
            case 0x00000710: return "I7_IB_UNK_0000_0710"; case 0x00000711: return "I7_IB_UNK_0000_0711";
            case 0x00000712: return "I7_IB_UNK_0000_0712"; case 0x00000713: return "I7_IB_UNK_0000_0713";
            case 0x00000714: return "I7_IB_UNK_0000_0714"; case 0x00000715: return "I7_IB_UNK_0000_0715";
            case 0x00000716: return "I7_IB_UNK_0000_0716"; case 0x00000717: return "I7_IB_UNK_0000_0717";
            case 0x00000718: return "I7_IB_UNK_0000_0718"; case 0x00000719: return "I7_IB_UNK_0000_0719";
            case 0x00000720: return "I7_IB_UNK_0000_0720"; case 0x00000721: return "I7_IB_UNK_0000_0721";
            case 0x00000722: return "I7_IB_UNK_0000_0722"; case 0x00000723: return "I7_IB_UNK_0000_0723";
            case 0x00000724: return "I7_IB_UNK_0000_0724"; case 0x00000725: return "I7_IB_UNK_0000_0725";
            case 0x00000726: return "I7_IB_UNK_0000_0726"; case 0x00000727: return "I7_IB_UNK_0000_0727";
            case 0x00000728: return "I7_IB_UNK_0000_0728"; case 0x00000729: return "I7_IB_UNK_0000_0729";
            case 0x00000730: return "I7_IB_UNK_0000_0730"; case 0x00000731: return "I7_IB_UNK_0000_0731";
            case 0x00000732: return "I7_IB_UNK_0000_0732"; case 0x00000733: return "I7_IB_UNK_0000_0733";
            case 0x00000734: return "I7_IB_UNK_0000_0734"; case 0x00000735: return "I7_IB_UNK_0000_0735";
            case 0x00000736: return "I7_IB_UNK_0000_0736"; case 0x00000737: return "I7_IB_UNK_0000_0737";
            case 0x00000738: return "I7_IB_UNK_0000_0738"; case 0x00000739: return "I7_IB_UNK_0000_0739";
            case 0x00000740: return "I7_IB_UNK_0000_0740"; case 0x00000741: return "I7_IB_UNK_0000_0741";
            case 0x00000742: return "I7_IB_UNK_0000_0742"; case 0x00000743: return "I7_IB_UNK_0000_0743";
            case 0x00000744: return "I7_IB_UNK_0000_0744"; case 0x00000745: return "I7_IB_UNK_0000_0745";
            case 0x00000746: return "I7_IB_UNK_0000_0746"; case 0x00000747: return "I7_IB_UNK_0000_0747";
            case 0x00000748: return "I7_IB_UNK_0000_0748"; case 0x00000749: return "I7_IB_UNK_0000_0749";

        }
    return NULL;
}


/**
 * Gets the name of an MSR.
 *
 * This may return a static buffer, so the content should only be considered
 * valid until the next time this function is called!.
 *
 * @returns MSR name.
 * @param   uMsr                The MSR in question.
 */
static const char *getMsrName(uint32_t uMsr)
{
    const char *pszReadOnly = getMsrNameHandled(uMsr);
    if (pszReadOnly)
        return pszReadOnly;

    /*
     * This MSR needs looking into, return a TODO_XXXX_XXXX name.
     */
    static char s_szBuf[32];
    RTStrPrintf(s_szBuf, sizeof(s_szBuf), "TODO_%04x_%04x", RT_HI_U16(uMsr), RT_LO_U16(uMsr));
    return s_szBuf;
}



/**
 * Gets the name of an MSR range.
 *
 * This may return a static buffer, so the content should only be considered
 * valid until the next time this function is called!.
 *
 * @returns MSR name.
 * @param   uMsr                The first MSR in the range.
 */
static const char *getMsrRangeName(uint32_t uMsr)
{
    switch (uMsr)
    {
        case 0x00000040:
            return g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah ? "MSR_LASTBRANCH_n_FROM_IP" : "MSR_LASTBRANCH_n";
        case 0x00000060:
            if (g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah)
                return "MSR_LASTBRANCH_n_TO_IP";
            break;

        case 0x000003f8:
        case 0x000003f9:
        case 0x000003fa:
            return "I7_MSR_PKG_Cn_RESIDENCY";
        case 0x000003fc:
        case 0x000003fd:
        case 0x000003fe:
            return "I7_MSR_CORE_Cn_RESIDENCY";

        case 0x00000400:
            return "IA32_MCi_CTL_STATUS_ADDR_MISC";

        case 0x00000680:
            return "MSR_LASTBRANCH_n_FROM_IP";
        case 0x000006c0:
            return "MSR_LASTBRANCH_n_TO_IP";

        case 0x00000800: case 0x00000801: case 0x00000802: case 0x00000803:
        case 0x00000804: case 0x00000805: case 0x00000806: case 0x00000807:
        case 0x00000808: case 0x00000809: case 0x0000080a: case 0x0000080b:
        case 0x0000080c: case 0x0000080d: case 0x0000080e: case 0x0000080f:
            return "IA32_X2APIC_n";
    }

    static char s_szBuf[96];
    const char *pszReadOnly = getMsrNameHandled(uMsr);
    if (pszReadOnly)
    {
        /*
         * Replace the last char with 'n'.
         */
        RTStrCopy(s_szBuf, sizeof(s_szBuf), pszReadOnly);
        size_t off = strlen(s_szBuf);
        if (off > 0)
            off--;
        if (off + 1 < sizeof(s_szBuf))
        {
            s_szBuf[off] = 'n';
            s_szBuf[off + 1] = '\0';
        }
    }
    else
    {
        /*
         * This MSR needs looking into, return a TODO_XXXX_XXXX_n name.
         */
        RTStrPrintf(s_szBuf, sizeof(s_szBuf), "TODO_%04x_%04x_n", RT_HI_U16(uMsr), RT_LO_U16(uMsr));
    }
    return s_szBuf;
}


/**
 * Returns the function name for MSRs that have one or two.
 *
 * @returns Function name if applicable, NULL if not.
 * @param   uMsr            The MSR in question.
 * @param   pfTakesValue    Whether this MSR function takes a value or not.
 *                          Optional.
 */
static const char *getMsrFnName(uint32_t uMsr, bool *pfTakesValue)
{
    bool fTmp;
    if (!pfTakesValue)
        pfTakesValue = &fTmp;

    *pfTakesValue = false;

    switch (uMsr)
    {
        case 0x00000000: return "Ia32P5McAddr";
        case 0x00000001: return "Ia32P5McType";
        case 0x00000006:
            if (g_enmMicroarch >= kCpumMicroarch_Intel_First && g_enmMicroarch <= kCpumMicroarch_Intel_P6_Core_Atom_First)
                return NULL; /* TR4 / cache tag on Pentium, but that's for later. */
            return "Ia32MonitorFilterLineSize";
        case 0x00000010: return "Ia32TimestampCounter";
        case 0x00000017: *pfTakesValue = true; return "Ia32PlatformId";
        case 0x0000001b: return "Ia32ApicBase";
        case 0x0000002a: *pfTakesValue = true; return g_fIntelNetBurst ? "IntelP4EbcHardPowerOn" : "IntelEblCrPowerOn";
        case 0x0000002b: *pfTakesValue = true; return g_fIntelNetBurst ? "IntelP4EbcSoftPowerOn" : NULL;
        case 0x0000002c: *pfTakesValue = true; return g_fIntelNetBurst ? "IntelP4EbcFrequencyId" : NULL;
        //case 0x00000033: return "IntelTestCtl";
        case 0x00000034: return CPUMMICROARCH_IS_INTEL_CORE7(g_enmMicroarch)
                             || CPUMMICROARCH_IS_INTEL_SILVERMONT_PLUS(g_enmMicroarch)
                              ? "IntelI7SmiCount" : NULL;
        case 0x00000035: return CPUMMICROARCH_IS_INTEL_CORE7(g_enmMicroarch) ? "IntelI7CoreThreadCount" : NULL;
        case 0x0000003a: return "Ia32FeatureControl";

        case 0x00000040:
        case 0x00000041:
        case 0x00000042:
        case 0x00000043:
        case 0x00000044:
        case 0x00000045:
        case 0x00000046:
        case 0x00000047:
            return "IntelLastBranchFromToN";

        case 0x0000008b: return g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON
                              ? "AmdK8PatchLevel" : "Ia32BiosSignId";
        case 0x0000009b: return "Ia32SmmMonitorCtl";

        case 0x000000a8:
        case 0x000000a9:
        case 0x000000aa:
        case 0x000000ab:
        case 0x000000ac:
        case 0x000000ad:
            *pfTakesValue = true;
            return "IntelCore2EmttmCrTablesN";

        case 0x000000c1:
        case 0x000000c2:
        case 0x000000c3:
        case 0x000000c4:
            return "Ia32PmcN";
        case 0x000000c5:
        case 0x000000c6:
        case 0x000000c7:
        case 0x000000c8:
            if (g_enmMicroarch >= kCpumMicroarch_Intel_Core7_First)
                return "Ia32PmcN";
            return NULL;

        case 0x000000cd: *pfTakesValue = true; return "IntelP6FsbFrequency";
        case 0x000000ce: return CPUMMICROARCH_IS_INTEL_CORE7(g_enmMicroarch)  ? "IntelPlatformInfo" : NULL;
        case 0x000000e2: return "IntelPkgCStConfigControl";
        case 0x000000e3: return "IntelCore2SmmCStMiscInfo";
        case 0x000000e4: return "IntelPmgIoCaptureBase";
        case 0x000000e7: return "Ia32MPerf";
        case 0x000000e8: return "Ia32APerf";
        case 0x000000ee: return "IntelCore1ExtConfig";
        case 0x000000fe: *pfTakesValue = true; return "Ia32MtrrCap";
        case 0x00000119: *pfTakesValue = true; return "IntelBblCrCtl";
        case 0x0000011e: *pfTakesValue = true; return "IntelBblCrCtl3";

        case 0x00000130: return g_enmMicroarch == kCpumMicroarch_Intel_Core7_Westmere
                             || g_enmMicroarch == kCpumMicroarch_Intel_Core7_Nehalem
                              ? "IntelCpuId1FeatureMaskEcdx" : NULL;
        case 0x00000131: return g_enmMicroarch == kCpumMicroarch_Intel_Core7_Westmere
                             || g_enmMicroarch == kCpumMicroarch_Intel_Core7_Nehalem
                              ? "IntelCpuId80000001FeatureMaskEcdx" : NULL;
        case 0x00000132: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_SandyBridge
                              ? "IntelCpuId1FeatureMaskEax" : NULL;
        case 0x00000133: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_SandyBridge
                              ? "IntelCpuId1FeatureMaskEcdx" : NULL;
        case 0x00000134: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_SandyBridge
                              ? "IntelCpuId80000001FeatureMaskEcdx" : NULL;
        case 0x0000013c: return "IntelI7SandyAesNiCtl";
        case 0x0000015f: return "IntelCore1DtsCalControl";
        case 0x00000174: return "Ia32SysEnterCs";
        case 0x00000175: return "Ia32SysEnterEsp";
        case 0x00000176: return "Ia32SysEnterEip";
        case 0x00000179: *pfTakesValue = true; return "Ia32McgCap";
        case 0x0000017a: return "Ia32McgStatus";
        case 0x0000017b: return "Ia32McgCtl";
        case 0x0000017f: return "IntelI7SandyErrorControl"; /* SandyBridge. */
        case 0x00000186: return "Ia32PerfEvtSelN";
        case 0x00000187: return "Ia32PerfEvtSelN";
        case 0x00000193: return /*g_fIntelNetBurst ? NULL :*/ NULL /* Core2_Penryn. */;
        case 0x00000194: if (g_fIntelNetBurst) break;   *pfTakesValue = true; return "IntelFlexRatio";
        case 0x00000198: *pfTakesValue = true; return "Ia32PerfStatus";
        case 0x00000199: *pfTakesValue = true; return "Ia32PerfCtl";
        case 0x0000019a: *pfTakesValue = true; return "Ia32ClockModulation";
        case 0x0000019b: *pfTakesValue = true; return "Ia32ThermInterrupt";
        case 0x0000019c: *pfTakesValue = true; return "Ia32ThermStatus";
        case 0x0000019d: *pfTakesValue = true; return "Ia32Therm2Ctl";
        case 0x000001a0: *pfTakesValue = true; return "Ia32MiscEnable";
        case 0x000001a2: *pfTakesValue = true; return "IntelI7TemperatureTarget";
        case 0x000001a6: return "IntelI7MsrOffCoreResponseN";
        case 0x000001a7: return "IntelI7MsrOffCoreResponseN";
        case 0x000001aa: return CPUMMICROARCH_IS_INTEL_CORE7(g_enmMicroarch) ? "IntelI7MiscPwrMgmt" : NULL /*"P6PicSensCfg"*/;
        case 0x000001ad: *pfTakesValue = true; return "IntelI7TurboRatioLimit"; /* SandyBridge+, Silvermount+ */
        case 0x000001c8: return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_Nehalem ? "IntelI7LbrSelect" : NULL;
        case 0x000001c9: return    g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah
                                && g_enmMicroarch <= kCpumMicroarch_Intel_P6_Core_Atom_End
                              ? "IntelLastBranchTos" : NULL /* Pentium M Dothan seems to have something else here. */;
        case 0x000001d7: return g_fIntelNetBurst ? "P6LastIntFromIp" : NULL;
        case 0x000001d8: return g_fIntelNetBurst ? "P6LastIntToIp"   : NULL;
        case 0x000001d9: return "Ia32DebugCtl";
        case 0x000001da: return g_fIntelNetBurst ? "IntelLastBranchTos" : NULL;
        case 0x000001db: return g_fIntelNetBurst ? "IntelLastBranchFromToN" : "P6LastBranchFromIp";
        case 0x000001dc: return g_fIntelNetBurst ? "IntelLastBranchFromToN" : "P6LastBranchToIp";
        case 0x000001dd: return g_fIntelNetBurst ? "IntelLastBranchFromToN" : "P6LastIntFromIp";
        case 0x000001de: return g_fIntelNetBurst ? "IntelLastBranchFromToN" : "P6LastIntToIp";
        case 0x000001f0: return "IntelI7VirtualLegacyWireCap"; /* SandyBridge. */
        case 0x000001f2: return "Ia32SmrrPhysBase";
        case 0x000001f3: return "Ia32SmrrPhysMask";
        case 0x000001f8: return "Ia32PlatformDcaCap";
        case 0x000001f9: return "Ia32CpuDcaCap";
        case 0x000001fa: return "Ia32Dca0Cap";
        case 0x000001fc: return "IntelI7PowerCtl";

        case 0x00000200: case 0x00000202: case 0x00000204: case 0x00000206:
        case 0x00000208: case 0x0000020a: case 0x0000020c: case 0x0000020e:
        case 0x00000210: case 0x00000212: case 0x00000214: case 0x00000216:
        case 0x00000218: case 0x0000021a: case 0x0000021c: case 0x0000021e:
            return "Ia32MtrrPhysBaseN";
        case 0x00000201: case 0x00000203: case 0x00000205: case 0x00000207:
        case 0x00000209: case 0x0000020b: case 0x0000020d: case 0x0000020f:
        case 0x00000211: case 0x00000213: case 0x00000215: case 0x00000217:
        case 0x00000219: case 0x0000021b: case 0x0000021d: case 0x0000021f:
            return "Ia32MtrrPhysMaskN";
        case 0x00000250:
        case 0x00000258: case 0x00000259:
        case 0x00000268: case 0x00000269: case 0x0000026a: case 0x0000026b:
        case 0x0000026c: case 0x0000026d: case 0x0000026e: case 0x0000026f:
            return "Ia32MtrrFixed";
        case 0x00000277: *pfTakesValue = true; return "Ia32Pat";

        case 0x00000280: case 0x00000281:  case 0x00000282: case 0x00000283:
        case 0x00000284: case 0x00000285:  case 0x00000286: case 0x00000287:
        case 0x00000288: case 0x00000289:  case 0x0000028a: case 0x0000028b:
        case 0x0000028c: case 0x0000028d:  case 0x0000028e: case 0x0000028f:
        case 0x00000290: case 0x00000291:  case 0x00000292: case 0x00000293:
        case 0x00000294: case 0x00000295:  //case 0x00000296: case 0x00000297:
        //case 0x00000298: case 0x00000299:  case 0x0000029a: case 0x0000029b:
        //case 0x0000029c: case 0x0000029d:  case 0x0000029e: case 0x0000029f:
            return "Ia32McNCtl2";

        case 0x000002ff: return "Ia32MtrrDefType";
        //case 0x00000305: return g_fIntelNetBurst ? TODO : NULL;
        case 0x00000309: return g_fIntelNetBurst ? NULL /** @todo P4 */ : "Ia32FixedCtrN";
        case 0x0000030a: return g_fIntelNetBurst ? NULL /** @todo P4 */ : "Ia32FixedCtrN";
        case 0x0000030b: return g_fIntelNetBurst ? NULL /** @todo P4 */ : "Ia32FixedCtrN";
        case 0x00000345: *pfTakesValue = true; return "Ia32PerfCapabilities";
        /* Note! Lots of P4 MSR 0x00000360..0x00000371. */
        case 0x0000038d: return "Ia32FixedCtrCtrl";
        case 0x0000038e: *pfTakesValue = true; return "Ia32PerfGlobalStatus";
        case 0x0000038f: return "Ia32PerfGlobalCtrl";
        case 0x00000390: return "Ia32PerfGlobalOvfCtrl";
        case 0x00000391: return "IntelI7UncPerfGlobalCtrl";             /* S,H,X */
        case 0x00000392: return "IntelI7UncPerfGlobalStatus";           /* S,H,X */
        case 0x00000393: return "IntelI7UncPerfGlobalOvfCtrl";          /* X. ASSUMING this is the same on sandybridge and later. */
        case 0x00000394: return g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "IntelI7UncPerfFixedCtr"  /* X */   : "IntelI7UncPerfFixedCtrCtrl"; /* >= S,H */
        case 0x00000395: return g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "IntelI7UncPerfFixedCtrCtrl" /* X*/ : "IntelI7UncPerfFixedCtr";     /* >= S,H */
        case 0x00000396: return g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "IntelI7UncAddrOpcodeMatch" /* X */ : "IntelI7UncCBoxConfig";       /* >= S,H */
        case 0x0000039c: return "IntelI7SandyPebsNumAlt";
         /* Note! Lots of P4 MSR 0x000003a0..0x000003e1. */
        case 0x000003b0: return g_fIntelNetBurst ? NULL : g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "IntelI7UncPmcN" /* X */            : "IntelI7UncArbPerfCtrN";      /* >= S,H */
        case 0x000003b1: return g_fIntelNetBurst ? NULL : g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "IntelI7UncPmcN" /* X */            : "IntelI7UncArbPerfCtrN";      /* >= S,H */
        case 0x000003b2: return g_fIntelNetBurst ? NULL : g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "IntelI7UncPmcN" /* X */            : "IntelI7UncArbPerfEvtSelN";   /* >= S,H */
        case 0x000003b3: return g_fIntelNetBurst ? NULL : g_enmMicroarch < kCpumMicroarch_Intel_Core7_SandyBridge ? "IntelI7UncPmcN" /* X */            : "IntelI7UncArbPerfEvtSelN";   /* >= S,H */
        case 0x000003b4: case 0x000003b5: case 0x000003b6: case 0x000003b7:
            return g_fIntelNetBurst ? NULL : "IntelI7UncPmcN";
        case 0x000003c0: case 0x000003c1: case 0x000003c2: case 0x000003c3:
        case 0x000003c4: case 0x000003c5: case 0x000003c6: case 0x000003c7:
            return g_fIntelNetBurst ? NULL : "IntelI7UncPerfEvtSelN";
        case 0x000003f1: return "Ia32PebsEnable";
        case 0x000003f6: return g_fIntelNetBurst ? NULL /*??*/ : "IntelI7PebsLdLat";
        case 0x000003f8: return g_fIntelNetBurst ? NULL : "IntelI7PkgCnResidencyN";
        case 0x000003f9: return "IntelI7PkgCnResidencyN";
        case 0x000003fa: return "IntelI7PkgCnResidencyN";
        case 0x000003fc: return "IntelI7CoreCnResidencyN";
        case 0x000003fd: return "IntelI7CoreCnResidencyN";
        case 0x000003fe: return "IntelI7CoreCnResidencyN";

        case 0x00000478: return g_enmMicroarch == kCpumMicroarch_Intel_Core2_Penryn ? "IntelCpuId1FeatureMaskEcdx" : NULL;
        case 0x00000480: *pfTakesValue = true; return "Ia32VmxBasic";
        case 0x00000481: *pfTakesValue = true; return "Ia32VmxPinbasedCtls";
        case 0x00000482: *pfTakesValue = true; return "Ia32VmxProcbasedCtls";
        case 0x00000483: *pfTakesValue = true; return "Ia32VmxExitCtls";
        case 0x00000484: *pfTakesValue = true; return "Ia32VmxEntryCtls";
        case 0x00000485: *pfTakesValue = true; return "Ia32VmxMisc";
        case 0x00000486: *pfTakesValue = true; return "Ia32VmxCr0Fixed0";
        case 0x00000487: *pfTakesValue = true; return "Ia32VmxCr0Fixed1";
        case 0x00000488: *pfTakesValue = true; return "Ia32VmxCr4Fixed0";
        case 0x00000489: *pfTakesValue = true; return "Ia32VmxCr4Fixed1";
        case 0x0000048a: *pfTakesValue = true; return "Ia32VmxVmcsEnum";
        case 0x0000048b: *pfTakesValue = true; return "Ia32VmxProcBasedCtls2";
        case 0x0000048c: *pfTakesValue = true; return "Ia32VmxEptVpidCap";
        case 0x0000048d: *pfTakesValue = true; return "Ia32VmxTruePinbasedCtls";
        case 0x0000048e: *pfTakesValue = true; return "Ia32VmxTrueProcbasedCtls";
        case 0x0000048f: *pfTakesValue = true; return "Ia32VmxTrueExitCtls";
        case 0x00000490: *pfTakesValue = true; return "Ia32VmxTrueEntryCtls";
        case 0x00000491: *pfTakesValue = true; return "Ia32VmxVmFunc";

        case 0x000004c1:
        case 0x000004c2:
        case 0x000004c3:
        case 0x000004c4:
        case 0x000004c5:
        case 0x000004c6:
        case 0x000004c7:
        case 0x000004c8:
            return "Ia32PmcN";

        case 0x000005a0: return "IntelCore2PeciControl"; /* Core2_Penryn. */

        case 0x00000600: return "Ia32DsArea";
        case 0x00000601: *pfTakesValue = true; return "IntelI7SandyVrCurrentConfig";
        case 0x00000603: *pfTakesValue = true; return "IntelI7SandyVrMiscConfig";
        case 0x00000606: *pfTakesValue = true; return "IntelI7SandyRaplPowerUnit";
        case 0x0000060a: *pfTakesValue = true; return "IntelI7SandyPkgCnIrtlN";
        case 0x0000060b: *pfTakesValue = true; return "IntelI7SandyPkgCnIrtlN";
        case 0x0000060c: *pfTakesValue = true; return "IntelI7SandyPkgCnIrtlN";
        case 0x0000060d: *pfTakesValue = true; return "IntelI7SandyPkgC2Residency";

        case 0x00000610: *pfTakesValue = true; return "IntelI7RaplPkgPowerLimit";
        case 0x00000611: *pfTakesValue = true; return "IntelI7RaplPkgEnergyStatus";
        case 0x00000613: *pfTakesValue = true; return "IntelI7RaplPkgPerfStatus";
        case 0x00000614: *pfTakesValue = true; return "IntelI7RaplPkgPowerInfo";
        case 0x00000618: *pfTakesValue = true; return "IntelI7RaplDramPowerLimit";
        case 0x00000619: *pfTakesValue = true; return "IntelI7RaplDramEnergyStatus";
        case 0x0000061b: *pfTakesValue = true; return "IntelI7RaplDramPerfStatus";
        case 0x0000061c: *pfTakesValue = true; return "IntelI7RaplDramPowerInfo";
        case 0x00000638: *pfTakesValue = true; return "IntelI7RaplPp0PowerLimit";
        case 0x00000639: *pfTakesValue = true; return "IntelI7RaplPp0EnergyStatus";
        case 0x0000063a: *pfTakesValue = true; return "IntelI7RaplPp0Policy";
        case 0x0000063b: *pfTakesValue = true; return "IntelI7RaplPp0PerfStatus";
        case 0x00000640: *pfTakesValue = true; return "IntelI7RaplPp1PowerLimit";
        case 0x00000641: *pfTakesValue = true; return "IntelI7RaplPp1EnergyStatus";
        case 0x00000642: *pfTakesValue = true; return "IntelI7RaplPp1Policy";
        case 0x00000648: *pfTakesValue = true; return "IntelI7IvyConfigTdpNominal";
        case 0x00000649: *pfTakesValue = true; return "IntelI7IvyConfigTdpLevel1";
        case 0x0000064a: *pfTakesValue = true; return "IntelI7IvyConfigTdpLevel2";
        case 0x0000064b: return "IntelI7IvyConfigTdpControl";
        case 0x0000064c: return "IntelI7IvyTurboActivationRatio";

        case 0x00000660: return "IntelAtSilvCoreC1Recidency";

        case 0x00000680: case 0x00000681: case 0x00000682: case 0x00000683:
        case 0x00000684: case 0x00000685: case 0x00000686: case 0x00000687:
        case 0x00000688: case 0x00000689: case 0x0000068a: case 0x0000068b:
        case 0x0000068c: case 0x0000068d: case 0x0000068e: case 0x0000068f:
        //case 0x00000690: case 0x00000691: case 0x00000692: case 0x00000693:
        //case 0x00000694: case 0x00000695: case 0x00000696: case 0x00000697:
        //case 0x00000698: case 0x00000699: case 0x0000069a: case 0x0000069b:
        //case 0x0000069c: case 0x0000069d: case 0x0000069e: case 0x0000069f:
            return "IntelLastBranchFromN";
        case 0x000006c0: case 0x000006c1: case 0x000006c2: case 0x000006c3:
        case 0x000006c4: case 0x000006c5: case 0x000006c6: case 0x000006c7:
        case 0x000006c8: case 0x000006c9: case 0x000006ca: case 0x000006cb:
        case 0x000006cc: case 0x000006cd: case 0x000006ce: case 0x000006cf:
        //case 0x000006d0: case 0x000006d1: case 0x000006d2: case 0x000006d3:
        //case 0x000006d4: case 0x000006d5: case 0x000006d6: case 0x000006d7:
        //case 0x000006d8: case 0x000006d9: case 0x000006da: case 0x000006db:
        //case 0x000006dc: case 0x000006dd: case 0x000006de: case 0x000006df:
            return "IntelLastBranchToN";
        case 0x000006e0: return "Ia32TscDeadline"; /** @todo detect this correctly! */

        case 0x00000c80: return g_enmMicroarch > kCpumMicroarch_Intel_Core7_Nehalem ? "Ia32DebugInterface" : NULL;

        case 0xc0000080: return "Amd64Efer";
        case 0xc0000081: return "Amd64SyscallTarget";
        case 0xc0000082: return "Amd64LongSyscallTarget";
        case 0xc0000083: return "Amd64CompSyscallTarget";
        case 0xc0000084: return "Amd64SyscallFlagMask";
        case 0xc0000100: return "Amd64FsBase";
        case 0xc0000101: return "Amd64GsBase";
        case 0xc0000102: return "Amd64KernelGsBase";
        case 0xc0000103: return "Amd64TscAux";
        case 0xc0000104: return "AmdFam15hTscRate";
        case 0xc0000105: return "AmdFam15hLwpCfg";
        case 0xc0000106: return "AmdFam15hLwpCbAddr";
        case 0xc0000408: return "AmdFam10hMc4MiscN";
        case 0xc0000409: return "AmdFam10hMc4MiscN";
        case 0xc000040a: return "AmdFam10hMc4MiscN";
        case 0xc000040b: return "AmdFam10hMc4MiscN";
        case 0xc000040c: return "AmdFam10hMc4MiscN";
        case 0xc000040d: return "AmdFam10hMc4MiscN";
        case 0xc000040e: return "AmdFam10hMc4MiscN";
        case 0xc000040f: return "AmdFam10hMc4MiscN";
        case 0xc0010000: return "AmdK8PerfCtlN";
        case 0xc0010001: return "AmdK8PerfCtlN";
        case 0xc0010002: return "AmdK8PerfCtlN";
        case 0xc0010003: return "AmdK8PerfCtlN";
        case 0xc0010004: return "AmdK8PerfCtrN";
        case 0xc0010005: return "AmdK8PerfCtrN";
        case 0xc0010006: return "AmdK8PerfCtrN";
        case 0xc0010007: return "AmdK8PerfCtrN";
        case 0xc0010010: *pfTakesValue = true; return "AmdK8SysCfg";
        case 0xc0010015: return "AmdK8HwCr";
        case 0xc0010016: case 0xc0010018: return "AmdK8IorrBaseN";
        case 0xc0010017: case 0xc0010019: return "AmdK8IorrMaskN";
        case 0xc001001a: case 0xc001001d: return "AmdK8TopOfMemN";
        case 0xc001001f: return "AmdK8NbCfg1";
        case 0xc0010020: return "AmdK8PatchLoader";
        case 0xc0010022: return "AmdK8McXcptRedir";
        case 0xc0010030: case 0xc0010031: case 0xc0010032:
        case 0xc0010033: case 0xc0010034: case 0xc0010035:
            return "AmdK8CpuNameN";
        case 0xc001003e: *pfTakesValue = true; return "AmdK8HwThermalCtrl";
        case 0xc001003f: return "AmdK8SwThermalCtrl";
        case 0xc0010041: *pfTakesValue = true; return "AmdK8FidVidControl";
        case 0xc0010042: *pfTakesValue = true; return "AmdK8FidVidStatus";
        case 0xc0010044: case 0xc0010045: case 0xc0010046: case 0xc0010047:
        case 0xc0010048: case 0xc0010049: case 0xc001004a: //case 0xc001004b:
            return "AmdK8McCtlMaskN";
        case 0xc0010050: case 0xc0010051: case 0xc0010052: case 0xc0010053:
            return "AmdK8SmiOnIoTrapN";
        case 0xc0010054: return "AmdK8SmiOnIoTrapCtlSts";
        case 0xc0010055: return "AmdK8IntPendingMessage";
        case 0xc0010056: return "AmdK8SmiTriggerIoCycle";
        case 0xc0010058: return "AmdFam10hMmioCfgBaseAddr";
        case 0xc0010059: return "AmdFam10hTrapCtlMaybe";
        case 0xc0010061: *pfTakesValue = true; return "AmdFam10hPStateCurLimit";
        case 0xc0010062: *pfTakesValue = true; return "AmdFam10hPStateControl";
        case 0xc0010063: *pfTakesValue = true; return "AmdFam10hPStateStatus";
        case 0xc0010064: case 0xc0010065: case 0xc0010066: case 0xc0010067:
        case 0xc0010068: case 0xc0010069: case 0xc001006a: case 0xc001006b:
            *pfTakesValue = true; return "AmdFam10hPStateN";
        case 0xc0010070: *pfTakesValue = true; return "AmdFam10hCofVidControl";
        case 0xc0010071: *pfTakesValue = true; return "AmdFam10hCofVidStatus";
        case 0xc0010073: return "AmdFam10hCStateIoBaseAddr";
        case 0xc0010074: return "AmdFam10hCpuWatchdogTimer";
        // case 0xc0010075: return "AmdFam15hApmlTdpLimit";
        // case 0xc0010077: return "AmdFam15hCpuPowerInTdp";
        // case 0xc0010078: return "AmdFam15hPowerAveragingPeriod";
        // case 0xc0010079: return "AmdFam15hDramCtrlCmdThrottle";
        // case 0xc0010080: return "AmdFam16hFreqSensFeedbackMonActCnt0";
        // case 0xc0010081: return "AmdFam16hFreqSensFeedbackMonRefCnt0";
        case 0xc0010111: return "AmdK8SmmBase";     /** @todo probably misdetected ign/gp due to locking */
        case 0xc0010112: return "AmdK8SmmAddr";     /** @todo probably misdetected ign/gp due to locking */
        case 0xc0010113: return "AmdK8SmmMask";     /** @todo probably misdetected ign/gp due to locking */
        case 0xc0010114: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm_AMDV ? "AmdK8VmCr" : NULL;        /** @todo probably misdetected due to locking */
        case 0xc0010115: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm      ? "AmdK8IgnNe" : NULL;
        case 0xc0010116: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm      ? "AmdK8SmmCtl" : NULL;
        case 0xc0010117: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm_AMDV ? "AmdK8VmHSavePa" : NULL;   /** @todo probably misdetected due to locking */
        case 0xc0010118: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm_AMDV ? "AmdFam10hVmLockKey" : NULL;
        case 0xc0010119: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm      ? "AmdFam10hSmmLockKey" : NULL; /* Not documented by BKDG, found in netbsd patch. */
        case 0xc001011a: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm      ? "AmdFam10hLocalSmiStatus" : NULL;
        case 0xc0010140: *pfTakesValue = true; return "AmdFam10hOsVisWrkIdLength";
        case 0xc0010141: *pfTakesValue = true; return "AmdFam10hOsVisWrkStatus";
        case 0xc0010200: case 0xc0010202: case 0xc0010204: case 0xc0010206:
        case 0xc0010208: case 0xc001020a: //case 0xc001020c: case 0xc001020e:
            return "AmdK8PerfCtlN";
        case 0xc0010201: case 0xc0010203: case 0xc0010205: case 0xc0010207:
        case 0xc0010209: case 0xc001020b: //case 0xc001020d: case 0xc001020f:
            return "AmdK8PerfCtrN";
        case 0xc0010230: case 0xc0010232: case 0xc0010234: case 0xc0010236:
        //case 0xc0010238: case 0xc001023a: case 0xc001030c: case 0xc001023e:
            return "AmdFam16hL2IPerfCtlN";
        case 0xc0010231: case 0xc0010233: case 0xc0010235: case 0xc0010237:
        //case 0xc0010239: case 0xc001023b: case 0xc001023d: case 0xc001023f:
            return "AmdFam16hL2IPerfCtrN";
        case 0xc0010240: case 0xc0010242: case 0xc0010244: case 0xc0010246:
        //case 0xc0010248: case 0xc001024a: case 0xc001024c: case 0xc001024e:
            return "AmdFam15hNorthbridgePerfCtlN";
        case 0xc0010241: case 0xc0010243: case 0xc0010245: case 0xc0010247:
        //case 0xc0010249: case 0xc001024b: case 0xc001024d: case 0xc001024f:
            return "AmdFam15hNorthbridgePerfCtrN";
        case 0xc0011000: *pfTakesValue = true; return "AmdK7MicrocodeCtl";
        case 0xc0011001: *pfTakesValue = true; return "AmdK7ClusterIdMaybe";
        case 0xc0011002: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AmdK8CpuIdCtlStd07hEbax" : NULL;
        case 0xc0011003: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AmdK8CpuIdCtlStd06hEcx"  : NULL;
        case 0xc0011004: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AmdK8CpuIdCtlStd01hEdcx" : NULL;
        case 0xc0011005: return g_enmMicroarch >= kCpumMicroarch_AMD_K8_First ? "AmdK8CpuIdCtlExt01hEdcx" : NULL;
        case 0xc0011006: return "AmdK7DebugStatusMaybe";
        case 0xc0011007: return "AmdK7BHTraceBaseMaybe";
        case 0xc0011008: return "AmdK7BHTracePtrMaybe";
        case 0xc0011009: return "AmdK7BHTraceLimitMaybe";
        case 0xc001100a: return "AmdK7HardwareDebugToolCfgMaybe";
        case 0xc001100b: return "AmdK7FastFlushCountMaybe";
        case 0xc001100c: return "AmdK7NodeId"; /** @todo dunno if this was there is K7 already. Kinda doubt it. */
        case 0xc0011019: return g_enmMicroarch >= kCpumMicroarch_AMD_15h_Piledriver ? "AmdK7DrXAddrMaskN" : NULL;
        case 0xc001101a: return g_enmMicroarch >= kCpumMicroarch_AMD_15h_Piledriver ? "AmdK7DrXAddrMaskN" : NULL;
        case 0xc001101b: return g_enmMicroarch >= kCpumMicroarch_AMD_15h_Piledriver ? "AmdK7DrXAddrMaskN" : NULL;
        case 0xc0011020: return "AmdK7LoadStoreCfg";
        case 0xc0011021: return "AmdK7InstrCacheCfg";
        case 0xc0011022: return "AmdK7DataCacheCfg";
        case 0xc0011023: return CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch) ? "AmdFam15hCombUnitCfg" : "AmdK7BusUnitCfg";
        case 0xc0011024: return "AmdK7DebugCtl2Maybe";
        case 0xc0011025: return "AmdK7Dr0DataMatchMaybe";
        case 0xc0011026: return "AmdK7Dr0DataMaskMaybe";
        case 0xc0011027: return "AmdK7DrXAddrMaskN";
        case 0xc0011028: return g_enmMicroarch >= kCpumMicroarch_AMD_15h_First ? "AmdFam15hFpuCfg" : NULL;
        case 0xc0011029: return g_enmMicroarch >= kCpumMicroarch_AMD_15h_First ? "AmdFam15hDecoderCfg" : NULL;
        case 0xc001102a: return CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch)   ? "AmdFam15hCombUnitCfg2"
                              : CPUMMICROARCH_IS_AMD_FAM_10H(g_enmMicroarch) || g_enmMicroarch > kCpumMicroarch_AMD_15h_End
                              ? "AmdFam10hBusUnitCfg2" /* 10h & 16h */ : NULL;
        case 0xc001102b: return CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch)   ? "AmdFam15hCombUnitCfg3" : NULL;
        case 0xc001102c: return CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch)   ? "AmdFam15hExecUnitCfg" : NULL;
        case 0xc001102d: return CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch)   ? "AmdFam15hLoadStoreCfg2" : NULL;
        case 0xc0011030: return "AmdFam10hIbsFetchCtl";
        case 0xc0011031: return "AmdFam10hIbsFetchLinAddr";
        case 0xc0011032: return "AmdFam10hIbsFetchPhysAddr";
        case 0xc0011033: return "AmdFam10hIbsOpExecCtl";
        case 0xc0011034: return "AmdFam10hIbsOpRip";
        case 0xc0011035: return "AmdFam10hIbsOpData";
        case 0xc0011036: return "AmdFam10hIbsOpData2";
        case 0xc0011037: return "AmdFam10hIbsOpData3";
        case 0xc0011038: return "AmdFam10hIbsDcLinAddr";
        case 0xc0011039: return "AmdFam10hIbsDcPhysAddr";
        case 0xc001103a: return "AmdFam10hIbsCtl";
        case 0xc001103b: return "AmdFam14hIbsBrTarget";
    }
    return NULL;
}


/**
 * Names CPUMCPU variables that MSRs corresponds to.
 *
 * @returns The variable name @a uMsr corresponds to, NULL if no variable.
 * @param   uMsr                The MSR in question.
 */
static const char *getMsrCpumCpuVarName(uint32_t uMsr)
{
    switch (uMsr)
    {
        case 0x00000250: return "GuestMsrs.msr.MtrrFix64K_00000";
        case 0x00000258: return "GuestMsrs.msr.MtrrFix16K_80000";
        case 0x00000259: return "GuestMsrs.msr.MtrrFix16K_A0000";
        case 0x00000268: return "GuestMsrs.msr.MtrrFix4K_C0000";
        case 0x00000269: return "GuestMsrs.msr.MtrrFix4K_C8000";
        case 0x0000026a: return "GuestMsrs.msr.MtrrFix4K_D0000";
        case 0x0000026b: return "GuestMsrs.msr.MtrrFix4K_D8000";
        case 0x0000026c: return "GuestMsrs.msr.MtrrFix4K_E0000";
        case 0x0000026d: return "GuestMsrs.msr.MtrrFix4K_E8000";
        case 0x0000026e: return "GuestMsrs.msr.MtrrFix4K_F0000";
        case 0x0000026f: return "GuestMsrs.msr.MtrrFix4K_F8000";
        case 0x00000277: return "Guest.msrPAT";
        case 0x000002ff: return "GuestMsrs.msr.MtrrDefType";
    }
    return NULL;
}


/**
 * Checks whether the MSR should read as zero for some reason.
 *
 * @returns true if the register should read as zero, false if not.
 * @param   uMsr                The MSR.
 */
static bool doesMsrReadAsZero(uint32_t uMsr)
{
    switch (uMsr)
    {
        case 0x00000088: return true; // "BBL_CR_D0" - RAZ until understood/needed.
        case 0x00000089: return true; // "BBL_CR_D1" - RAZ until understood/needed.
        case 0x0000008a: return true; // "BBL_CR_D2" - RAZ until understood/needed.

        /* Non-zero, but unknown register. */
        case 0x0000004a:
        case 0x0000004b:
        case 0x0000004c:
        case 0x0000004d:
        case 0x0000004e:
        case 0x0000004f:
        case 0x00000050:
        case 0x00000051:
        case 0x00000052:
        case 0x00000053:
        case 0x00000054:
        case 0x0000008c:
        case 0x0000008d:
        case 0x0000008e:
        case 0x0000008f:
        case 0x00000090:
        case 0xc0011011:
            return true;
    }

    return false;
}


/**
 * Gets the skip mask for the given MSR.
 *
 * @returns Skip mask (0 means skipping nothing).
 * @param   uMsr                The MSR.
 */
static uint64_t getGenericSkipMask(uint32_t uMsr)
{
    switch (uMsr)
    {
        case 0x0000013c: return 3; /* AES-NI lock bit ++. */

        case 0x000001f2: return UINT64_C(0xfffff00f); /* Ia32SmrrPhysBase - Only writable in SMM. */
        case 0x000001f3: return UINT64_C(0xfffff800); /* Ia32SmrrPhysMask - Only writable in SMM. */

        /* these two have lock bits. */
        case 0x0000064b: return UINT64_C(0x80000003);
        case 0x0000064c: return UINT64_C(0x800000ff);

        case 0xc0010015: return 1; /* SmmLock bit */

        /* SmmLock effect: */
        case 0xc0010111: return UINT32_MAX;
        case 0xc0010112: return UINT64_C(0xfffe0000) | ((RT_BIT_64(vbCpuRepGetPhysAddrWidth()) - 1) & ~(uint64_t)UINT32_MAX);
        case 0xc0010113: return UINT64_C(0xfffe773f) | ((RT_BIT_64(vbCpuRepGetPhysAddrWidth()) - 1) & ~(uint64_t)UINT32_MAX);
        case 0xc0010116: return 0x1f;

        case 0xc0010114: return RT_BIT_64(3) /* SVM lock */ | RT_BIT_64(4) /* SvmeDisable */;

        /* Canonical */
        case 0xc0011034:
        case 0xc0011038:
        case 0xc001103b:
            return UINT64_C(0xffff800000000000);

        case 0x00000060: case 0x00000061: case 0x00000062: case 0x00000063:
        case 0x00000064: case 0x00000065: case 0x00000066: case 0x00000067:
        case 0x00000040: case 0x00000041: case 0x00000042: case 0x00000043:
        case 0x00000044: case 0x00000045: case 0x00000046: case 0x00000047:
        case 0x00000600:
            if (g_enmMicroarch >= kCpumMicroarch_Intel_Core2_First)
                return UINT64_C(0xffff800000000000);
            break;


        /* Write only bits. */
        case 0xc0010041: return RT_BIT_64(16); /* FIDVID_CTL.InitFidVid */

        /* Time counters - fudge them to avoid incorrect ignore masks. */
        case 0x00000010:
        case 0x000000e7:
        case 0x000000e8:
            return RT_BIT_32(29) - 1;
    }
    return 0;
}




/** queryMsrWriteBadness return values. */
typedef enum
{
    /** . */
    VBCPUREPBADNESS_MOSTLY_HARMLESS = 0,
    /** Not a problem if accessed with care. */
    VBCPUREPBADNESS_MIGHT_BITE,
    /** Worse than a bad james bond villain. */
    VBCPUREPBADNESS_BOND_VILLAIN
} VBCPUREPBADNESS;


/**
 * Backlisting and graylisting of MSRs which may cause tripple faults.
 *
 * @returns Badness factor.
 * @param   uMsr                The MSR in question.
 */
static VBCPUREPBADNESS queryMsrWriteBadness(uint32_t uMsr)
{
    /** @todo Having trouble in the 0xc0010247,0xc0011006,?? region on Bulldozer. */
    /** @todo Having trouble in the 0xc001100f,0xc001100d,?? region on Opteron
     *        2384. */

    switch (uMsr)
    {
        case 0x00000050:
        case 0x00000051:
        case 0x00000052:
        case 0x00000053:
        case 0x00000054:

        case 0x00001006:
        case 0x00001007:
            return VBCPUREPBADNESS_BOND_VILLAIN;

        case 0x0000120e:
        case 0x00001233:
        case 0x00001239:
        case 0x00001249:
        case 0x0000124a:
        case 0x00001404:
        case 0x00001405:
        case 0x00001413:
        case 0x0000142c: /* Caused rip to be set to 297 or some such weirdness... */
        case 0x0000142e:
        case 0x00001435:
        case 0x00001436:
        case 0x00001438:
        case 0x0000317f:
            if (g_enmVendor == CPUMCPUVENDOR_VIA || g_enmVendor == CPUMCPUVENDOR_SHANGHAI)
                return VBCPUREPBADNESS_BOND_VILLAIN;
            break;

        case 0xc0010010:
        case 0xc0010016:
        case 0xc0010017:
        case 0xc0010018:
        case 0xc0010019:
        case 0xc001001a:
        case 0xc001001d:

        case 0xc0010058: /* MMIO Configuration Base Address on AMD Zen CPUs. */
            if (CPUMMICROARCH_IS_AMD_FAM_ZEN(g_enmMicroarch))
                return VBCPUREPBADNESS_BOND_VILLAIN;
            break;

        case 0xc0010064: /* P-state fequency, voltage, ++. */
        case 0xc0010065: /* P-state fequency, voltage, ++. */
        case 0xc0010066: /* P-state fequency, voltage, ++. */
        case 0xc0010067: /* P-state fequency, voltage, ++. */
        case 0xc0010068: /* P-state fequency, voltage, ++. */
        case 0xc0010069: /* P-state fequency, voltage, ++. */
        case 0xc001006a: /* P-state fequency, voltage, ++. */
        case 0xc001006b: /* P-state fequency, voltage, ++. */
        case 0xc0010070: /* COFVID Control. */
        case 0xc001101e:
        case 0xc0011021: /* IC_CFG (instruction cache configuration) */
        case 0xc0011023: /* CU_CFG (combined unit configuration) */
        case 0xc001102c: /* EX_CFG (execution unit configuration) */
            return VBCPUREPBADNESS_BOND_VILLAIN;

        case 0xc0011012:
            if (CPUMMICROARCH_IS_AMD_FAM_0FH(g_enmMicroarch))
                return VBCPUREPBADNESS_MIGHT_BITE;
            break;

        /* KVM MSRs that are unsafe to touch. */
        case 0x00000011: /* KVM */
        case 0x00000012: /* KVM */
            return VBCPUREPBADNESS_BOND_VILLAIN;

        /*
         * The TSC is tricky -- writing it isn't a problem, but if we put back the original
         * value, we'll throw it out of whack. If we're on an SMP OS that uses the TSC for timing,
         * we'll likely kill it, especially if we can't do the modification very quickly.
         */
        case 0x00000010: /* IA32_TIME_STAMP_COUNTER */
            if (!g_MsrAcc.fAtomic)
                return VBCPUREPBADNESS_BOND_VILLAIN;
            break;

        /*
         * The following MSRs are not safe to modify in a typical OS if we can't do it atomically,
         * i.e. read/modify/restore without allowing any other code to execute. Everything related
         * to syscalls will blow up in our face if we go back to userland with modified MSRs.
         */
//        case 0x0000001b: /* IA32_APIC_BASE */
        case 0xc0000081: /* MSR_K6_STAR */
        case 0xc0000082: /* AMD64_STAR64 */
        case 0xc0000083: /* AMD64_STARCOMPAT */
        case 0xc0000084: /* AMD64_SYSCALL_FLAG_MASK */
        case 0xc0000100: /* AMD64_FS_BASE */
        case 0xc0000101: /* AMD64_GS_BASE */
        case 0xc0000102: /* AMD64_KERNEL_GS_BASE */
            if (!g_MsrAcc.fAtomic)
                return VBCPUREPBADNESS_MIGHT_BITE;
            break;

        case 0x000001a0: /* IA32_MISC_ENABLE */
        case 0x00000199: /* IA32_PERF_CTL */
            return VBCPUREPBADNESS_MIGHT_BITE;

        case 0x000005a0: /* C2_PECI_CTL */
        case 0x000005a1: /* C2_UNK_0000_05a1 */
            if (g_enmVendor == CPUMCPUVENDOR_INTEL)
                return VBCPUREPBADNESS_MIGHT_BITE;
            break;

        case 0x00002000: /* P6_CR0. */
        case 0x00002003: /* P6_CR3. */
        case 0x00002004: /* P6_CR4. */
            if (g_enmVendor == CPUMCPUVENDOR_INTEL)
                return VBCPUREPBADNESS_MIGHT_BITE;
            break;
        case 0xc0000080: /* MSR_K6_EFER */
            return VBCPUREPBADNESS_MIGHT_BITE;
    }
    return VBCPUREPBADNESS_MOSTLY_HARMLESS;
}


/**
 * Checks if this might be a VIA/Shanghai dummy register.
 *
 * @returns true if it's a dummy, false if it isn't.
 * @param   uMsr                The MSR.
 * @param   uValue              The value.
 * @param   fFlags              The flags.
 */
static bool isMsrViaShanghaiDummy(uint32_t uMsr, uint64_t uValue, uint32_t fFlags)
{
    if (g_enmVendor != CPUMCPUVENDOR_VIA && g_enmVendor != CPUMCPUVENDOR_SHANGHAI)
        return false;

    if (uValue)
        return false;

    if (fFlags)
        return false;

    switch (uMsr)
    {
        case 0x00000010:
        case 0x0000001b:
        case 0x000000c1:
        case 0x000000c2:
        case 0x0000011e:
        case 0x00000186:
        case 0x00000187:
        //case 0x00000200 ... (mtrrs will be detected)
            return false;

        case 0xc0000080:
        case 0xc0000081:
        case 0xc0000082:
        case 0xc0000083:
            if (vbCpuRepSupportsLongMode())
                return false;
            break;
    }

    if (uMsr >= 0x00001200 && uMsr <= 0x00003fff && queryMsrWriteBadness(uMsr) != VBCPUREPBADNESS_MOSTLY_HARMLESS)
        return false;

    if (   !msrProberModifyNoChange(uMsr)
        && !msrProberModifyZero(uMsr))
        return false;

    uint64_t fIgnMask  = 0;
    uint64_t fGpMask   = 0;
    int rc = msrProberModifyBitChanges(uMsr, &fIgnMask, &fGpMask, 0);
    if (RT_FAILURE(rc))
        return false;

    if (fIgnMask != UINT64_MAX)
        return false;
    if (fGpMask != 0)
        return false;

    return true;
}


/**
 * Adjusts the ignore and GP masks for MSRs which contains canonical addresses.
 *
 * @param   uMsr                The MSR.
 * @param   pfIgn               Pointer to the ignore mask.
 * @param   pfGp                Pointer to the GP mask.
 */
static void adjustCanonicalIgnAndGpMasks(uint32_t uMsr, uint64_t *pfIgn, uint64_t *pfGp)
{
    RT_NOREF1(pfIgn);
    if (!vbCpuRepSupportsLongMode())
        return;
    switch (uMsr)
    {
        case 0x00000175:
        case 0x00000176:
        case 0x000001da:
        case 0x000001db:
        case 0x000001dc:
        case 0x000001de:
        case 0x00000600:
            if (*pfGp == UINT64_C(0xffff800000000000))
                *pfGp = 0;
            break;
        case 0x000001dd:
            if (*pfGp == UINT64_C(0x7fff800000000000) || *pfGp == UINT64_C(0xffff800000000000)) /* why is the top bit writable? */
                *pfGp = 0;
            break;

        case 0xc0000082:
        case 0xc0000083:
        case 0xc0000100:
        case 0xc0000101:
        case 0xc0000102:
            *pfGp = 0;
            break;
    }
}



/**
 * Prints a 64-bit value in the best way.
 *
 * @param   uValue              The value.
 */
static void printMsrValueU64(uint64_t uValue)
{
    if (uValue == 0)
        vbCpuRepPrintf(", 0");
    else if (uValue == UINT16_MAX)
        vbCpuRepPrintf(", UINT16_MAX");
    else if (uValue == UINT32_MAX)
        vbCpuRepPrintf(", UINT32_MAX");
    else if (uValue == UINT64_MAX)
        vbCpuRepPrintf(", UINT64_MAX");
    else if (uValue == UINT64_C(0xffffffff00000000))
        vbCpuRepPrintf(", ~(uint64_t)UINT32_MAX");
    else if (uValue <= (UINT32_MAX >> 1))
        vbCpuRepPrintf(", %#llx", uValue);
    else if (uValue <= UINT32_MAX)
        vbCpuRepPrintf(", UINT32_C(%#llx)", uValue);
    else
        vbCpuRepPrintf(", UINT64_C(%#llx)", uValue);
}


/**
 * Prints the newline after an MSR line has been printed.
 *
 * This is used as a hook to slow down the output and make sure the remote
 * terminal or/and output file has received the last update before we go and
 * crash probing the next MSR.
 */
static void printMsrNewLine(void)
{
    vbCpuRepPrintf("\n");
#if 1
    RTThreadSleep(8);
#endif
}

static int printMsrWriteOnly(uint32_t uMsr, const char *pszWrFnName, const char *pszAnnotation)
{
    if (!pszWrFnName)
        pszWrFnName = "IgnoreWrite";
    vbCpuRepPrintf(pszAnnotation
                   ? "    MFN(%#010x, \"%s\", WriteOnly, %s), /* %s */"
                   : "    MFN(%#010x, \"%s\", WriteOnly, %s),",
                   uMsr, getMsrName(uMsr), pszWrFnName, pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrValueReadOnly(uint32_t uMsr, uint64_t uValue, const char *pszAnnotation)
{
    vbCpuRepPrintf("    MVO(%#010x, \"%s\"", uMsr, getMsrName(uMsr));
    printMsrValueU64(uValue);
    vbCpuRepPrintf("),");
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}



static int printMsrValueIgnoreWritesNamed(uint32_t uMsr, uint64_t uValue, const char *pszName, const char *pszAnnotation)
{
    vbCpuRepPrintf("    MVI(%#010x, \"%s\"", uMsr, pszName);
    printMsrValueU64(uValue);
    vbCpuRepPrintf("),");
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrValueIgnoreWrites(uint32_t uMsr, uint64_t uValue, const char *pszAnnotation)
{
    return printMsrValueIgnoreWritesNamed(uMsr, uValue, getMsrName(uMsr), pszAnnotation);
}


static int printMsrValueExtended(uint32_t uMsr, uint64_t uValue, uint64_t fIgnMask, uint64_t fGpMask,
                                  const char *pszAnnotation)
{
    vbCpuRepPrintf("    MVX(%#010x, \"%s\"", uMsr, getMsrName(uMsr));
    printMsrValueU64(uValue);
    printMsrValueU64(fIgnMask);
    printMsrValueU64(fGpMask);
    vbCpuRepPrintf("),");
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrRangeValueReadOnly(uint32_t uMsr, uint32_t uLast, uint64_t uValue, const char *pszAnnotation)
{
    vbCpuRepPrintf("    RVO(%#010x, %#010x, \"%s\"", uMsr, uLast, getMsrRangeName(uMsr));
    printMsrValueU64(uValue);
    vbCpuRepPrintf("),");
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrRangeValueIgnoreWritesNamed(uint32_t uMsr, uint32_t uLast, uint64_t uValue, const char *pszName, const char *pszAnnotation)
{
    vbCpuRepPrintf("    RVI(%#010x, %#010x, \"%s\"", uMsr, uLast, pszName);
    printMsrValueU64(uValue);
    vbCpuRepPrintf("),");
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrRangeValueIgnoreWrites(uint32_t uMsr, uint32_t uLast, uint64_t uValue, const char *pszAnnotation)
{
    return printMsrRangeValueIgnoreWritesNamed(uMsr, uLast, uValue, getMsrRangeName(uMsr), pszAnnotation);
}


static int printMsrFunction(uint32_t uMsr, const char *pszRdFnName, const char *pszWrFnName, const char *pszAnnotation)
{
    if (!pszRdFnName)
        pszRdFnName = getMsrFnName(uMsr, NULL);
    if (!pszWrFnName)
        pszWrFnName = pszRdFnName;
    vbCpuRepPrintf("    MFN(%#010x, \"%s\", %s, %s),", uMsr, getMsrName(uMsr), pszRdFnName, pszWrFnName);
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrFunctionReadOnly(uint32_t uMsr, const char *pszRdFnName, const char *pszAnnotation)
{
    if (!pszRdFnName)
        pszRdFnName = getMsrFnName(uMsr, NULL);
    vbCpuRepPrintf("    MFO(%#010x, \"%s\", %s),", uMsr, getMsrName(uMsr), pszRdFnName);
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrFunctionIgnoreWrites(uint32_t uMsr, const char *pszRdFnName, const char *pszAnnotation)
{
    if (!pszRdFnName)
        pszRdFnName = getMsrFnName(uMsr, NULL);
    vbCpuRepPrintf("    MFI(%#010x, \"%s\", %s),", uMsr, getMsrName(uMsr), pszRdFnName);
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrFunctionIgnoreMask(uint32_t uMsr, const char *pszRdFnName, const char *pszWrFnName,
                                      uint64_t fIgnMask, const char *pszAnnotation)
{
    if (!pszRdFnName)
        pszRdFnName = getMsrFnName(uMsr, NULL);
    if (!pszWrFnName)
        pszWrFnName = pszRdFnName;
    vbCpuRepPrintf("    MFW(%#010x, \"%s\", %s, %s", uMsr, getMsrName(uMsr), pszRdFnName, pszWrFnName);
    printMsrValueU64(fIgnMask);
    vbCpuRepPrintf("),");
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrFunctionExtended(uint32_t uMsr, const char *pszRdFnName, const char *pszWrFnName, uint64_t uValue,
                                    uint64_t fIgnMask, uint64_t fGpMask, const char *pszAnnotation)
{
    if (!pszRdFnName)
        pszRdFnName = getMsrFnName(uMsr, NULL);
    if (!pszWrFnName)
        pszWrFnName = pszRdFnName;
    vbCpuRepPrintf("    MFX(%#010x, \"%s\", %s, %s", uMsr, getMsrName(uMsr), pszRdFnName, pszWrFnName);
    printMsrValueU64(uValue);
    printMsrValueU64(fIgnMask);
    printMsrValueU64(fGpMask);
    vbCpuRepPrintf("),");
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrFunctionExtendedIdxVal(uint32_t uMsr, const char *pszRdFnName, const char *pszWrFnName, uint64_t uValue,
                                          uint64_t fIgnMask, uint64_t fGpMask, const char *pszAnnotation)
{
    if (!pszRdFnName)
        pszRdFnName = getMsrFnName(uMsr, NULL);
    if (!pszWrFnName)
        pszWrFnName = pszRdFnName;
    vbCpuRepPrintf("    MFX(%#010x, \"%s\", %s, %s, %#x", uMsr, getMsrName(uMsr), pszRdFnName, pszWrFnName, uValue);
    printMsrValueU64(fIgnMask);
    printMsrValueU64(fGpMask);
    vbCpuRepPrintf("),");
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrFunctionCpumCpu(uint32_t uMsr, const char *pszRdFnName, const char *pszWrFnName,
                                   const char *pszCpumCpuStorage, const char *pszAnnotation)
{
    if (!pszRdFnName)
        pszRdFnName = getMsrFnName(uMsr, NULL);
    if (!pszWrFnName)
        pszWrFnName = pszRdFnName;
    if (!pszCpumCpuStorage)
        pszCpumCpuStorage = getMsrCpumCpuVarName(uMsr);
    if (!pszCpumCpuStorage)
        return RTMsgErrorRc(VERR_NOT_FOUND, "Missing CPUMCPU member for %#s (%#x)\n", getMsrName(uMsr), uMsr);
    vbCpuRepPrintf("    MFS(%#010x, \"%s\", %s, %s, %s),", uMsr, getMsrName(uMsr), pszRdFnName, pszWrFnName, pszCpumCpuStorage);
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrFunctionCpumCpuEx(uint32_t uMsr, const char *pszRdFnName, const char *pszWrFnName,
                                     const char *pszCpumCpuStorage, uint64_t fIgnMask, uint64_t fGpMask,
                                     const char *pszAnnotation)
{
    if (!pszRdFnName)
        pszRdFnName = getMsrFnName(uMsr, NULL);
    if (!pszWrFnName)
        pszWrFnName = pszRdFnName;
    if (!pszCpumCpuStorage)
        pszCpumCpuStorage = getMsrCpumCpuVarName(uMsr);
    if (!pszCpumCpuStorage)
        return RTMsgErrorRc(VERR_NOT_FOUND, "Missing CPUMCPU member for %#s (%#x)\n", getMsrName(uMsr), uMsr);
    vbCpuRepPrintf("    MFZ(%#010x, \"%s\", %s, %s, %s", uMsr, getMsrName(uMsr), pszRdFnName, pszWrFnName, pszCpumCpuStorage);
    printMsrValueU64(fIgnMask);
    printMsrValueU64(fGpMask);
    vbCpuRepPrintf("),");
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrRangeFunction(uint32_t uMsr, uint32_t uLast, const char *pszRdFnName, const char *pszWrFnName,
                                 const char *pszAnnotation)
{
    if (!pszRdFnName)
        pszRdFnName = getMsrFnName(uMsr, NULL);
    if (!pszWrFnName)
        pszWrFnName = pszRdFnName;
    vbCpuRepPrintf("    RFN(%#010x, %#010x, \"%s\", %s, %s),", uMsr, uLast, getMsrRangeName(uMsr), pszRdFnName, pszWrFnName);
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrRangeFunctionEx(uint32_t uMsr, uint32_t uLast, const char *pszRdFnName, const char *pszWrFnName,
                                   uint64_t uValue, uint64_t fIgnMask, uint64_t fGpMask, const char *pszAnnotation)
{
    if (!pszRdFnName)
        pszRdFnName = getMsrFnName(uMsr, NULL);
    if (!pszWrFnName)
        pszWrFnName = pszRdFnName;
    vbCpuRepPrintf("    RSN(%#010x, %#010x, \"%s\", %s, %s", uMsr, uLast, getMsrRangeName(uMsr), pszRdFnName, pszWrFnName);
    printMsrValueU64(uValue);
    printMsrValueU64(fIgnMask);
    printMsrValueU64(fGpMask);
    vbCpuRepPrintf("),");
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrRangeFunctionExIdxVal(uint32_t uMsr, uint32_t uLast, const char *pszRdFnName, const char *pszWrFnName,
                                         uint64_t uValue, uint64_t fIgnMask, uint64_t fGpMask, const char *pszAnnotation)
{
    if (!pszRdFnName)
        pszRdFnName = getMsrFnName(uMsr, NULL);
    if (!pszWrFnName)
        pszWrFnName = pszRdFnName;
    vbCpuRepPrintf("    RSN(%#010x, %#010x, \"%s\", %s, %s, %#x",
                   uMsr, uLast, getMsrRangeName(uMsr), pszRdFnName, pszWrFnName, uValue);
    printMsrValueU64(fIgnMask);
    printMsrValueU64(fGpMask);
    vbCpuRepPrintf("),");
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}


static int printMsrAlias(uint32_t uMsr, uint32_t uTarget, const char *pszAnnotation)
{
    vbCpuRepPrintf("    MAL(%#010x, \"%s\", %#010x),", uMsr, getMsrName(uMsr), uTarget);
    if (pszAnnotation)
        vbCpuRepPrintf(" /* %s */", pszAnnotation);
    printMsrNewLine();
    return VINF_SUCCESS;
}



static const char *annotateValue(uint64_t uValue)
{
    static char s_szBuf[40];
    if (uValue <= UINT32_MAX)
        RTStrPrintf(s_szBuf, sizeof(s_szBuf), "value=%#llx", uValue);
    else
        RTStrPrintf(s_szBuf, sizeof(s_szBuf), "value=%#x`%08x", RT_HI_U32(uValue), RT_LO_U32(uValue));
    return s_szBuf;
}


static const char *annotateValueExtra(const char *pszExtra, uint64_t uValue)
{
    static char s_szBuf[40];
    if (uValue <= UINT32_MAX)
        RTStrPrintf(s_szBuf, sizeof(s_szBuf), "%s value=%#llx", pszExtra, uValue);
    else
        RTStrPrintf(s_szBuf, sizeof(s_szBuf), "%s value=%#x`%08x", pszExtra, RT_HI_U32(uValue), RT_LO_U32(uValue));
    return s_szBuf;
}


static const char *annotateIfMissingBits(uint64_t uValue, uint64_t fBits)
{
    static char s_szBuf[80];
    if ((uValue & fBits) == fBits)
        return annotateValue(uValue);
    RTStrPrintf(s_szBuf, sizeof(s_szBuf), "XXX: Unexpected value %#llx - wanted bits %#llx to be set.", uValue, fBits);
    return s_szBuf;
}


static int reportMsr_Generic(uint32_t uMsr, uint32_t fFlags, uint64_t uValue)
{
    int         rc;
    bool        fTakesValue = false;
    const char *pszFnName   = getMsrFnName(uMsr, &fTakesValue);

    if (fFlags & VBCPUREPMSR_F_WRITE_ONLY)
        rc = printMsrWriteOnly(uMsr, pszFnName, NULL);
    else
    {
        bool    fReadAsZero = doesMsrReadAsZero(uMsr);
        fTakesValue = fTakesValue && !fReadAsZero;


        switch (queryMsrWriteBadness(uMsr))
        {
            /* This is what we're here for... */
            case VBCPUREPBADNESS_MOSTLY_HARMLESS:
            {
                if (   msrProberModifyNoChange(uMsr)
                    || msrProberModifyZero(uMsr))
                {
                    uint64_t fSkipMask = getGenericSkipMask(uMsr);
                    uint64_t fIgnMask  = 0;
                    uint64_t fGpMask   = 0;
                    rc = msrProberModifyBitChanges(uMsr, &fIgnMask, &fGpMask, fSkipMask);
                    if (RT_FAILURE(rc))
                        return rc;
                    adjustCanonicalIgnAndGpMasks(uMsr, &fIgnMask, &fGpMask);

                    if (pszFnName)
                    {
                        if (fGpMask == 0 && fIgnMask == UINT64_MAX && !fTakesValue)
                            rc = printMsrFunctionIgnoreWrites(uMsr, pszFnName, annotateValue(uValue));
                        else if (fGpMask == 0 && fIgnMask == 0 && (!fTakesValue || uValue == 0))
                            rc = printMsrFunction(uMsr, pszFnName, pszFnName, annotateValue(uValue));
                        else
                            rc = printMsrFunctionExtended(uMsr, pszFnName, pszFnName, fTakesValue ? uValue : 0,
                                                          fIgnMask, fGpMask, annotateValue(uValue));
                    }
                    else if (fGpMask == 0 && fIgnMask == UINT64_MAX)
                        rc = printMsrValueIgnoreWrites(uMsr, fReadAsZero ? 0 : uValue, fReadAsZero ? annotateValue(uValue) : NULL);
                    else
                        rc = printMsrValueExtended(uMsr, fReadAsZero ? 0 : uValue, fIgnMask, fGpMask,
                                                   fReadAsZero ? annotateValue(uValue) : NULL);
                }
                /* Most likely read-only. */
                else if (pszFnName && !fTakesValue)
                    rc = printMsrFunctionReadOnly(uMsr, pszFnName, annotateValue(uValue));
                else if (pszFnName)
                    rc = printMsrFunctionExtended(uMsr, pszFnName, "ReadOnly", uValue, 0, 0, annotateValue(uValue));
                else if (fReadAsZero)
                    rc = printMsrValueReadOnly(uMsr, 0, annotateValue(uValue));
                else
                    rc = printMsrValueReadOnly(uMsr, uValue, NULL);
                break;
            }

            /* These should have special handling, so just do a simple
               write back same value check to see if it's writable. */
            case VBCPUREPBADNESS_MIGHT_BITE:
                if (msrProberModifyNoChange(uMsr))
                {
                    if (pszFnName && !fTakesValue)
                        rc = printMsrFunction(uMsr, pszFnName, pszFnName, annotateValueExtra("Might bite.", uValue));
                    else if (pszFnName)
                        rc = printMsrFunctionExtended(uMsr, pszFnName, pszFnName, uValue, 0, 0,
                                                      annotateValueExtra("Might bite.", uValue));
                    else if (fReadAsZero)
                        rc = printMsrValueIgnoreWrites(uMsr, 0, annotateValueExtra("Might bite.", uValue));
                    else
                        rc = printMsrValueIgnoreWrites(uMsr, uValue, "Might bite.");
                }
                else if (pszFnName && !fTakesValue)
                    rc = printMsrFunctionReadOnly(uMsr, pszFnName, annotateValueExtra("Might bite.", uValue));
                else if (pszFnName)
                    rc = printMsrFunctionExtended(uMsr, pszFnName, "ReadOnly", uValue, 0, UINT64_MAX,
                                                  annotateValueExtra("Might bite.", uValue));
                else if (fReadAsZero)
                    rc = printMsrValueReadOnly(uMsr, 0, annotateValueExtra("Might bite.", uValue));
                else
                    rc = printMsrValueReadOnly(uMsr, uValue, "Might bite.");
                break;


            /* Don't try anything with these guys. */
            case VBCPUREPBADNESS_BOND_VILLAIN:
            default:
                if (pszFnName && !fTakesValue)
                    rc = printMsrFunction(uMsr, pszFnName, pszFnName, annotateValueExtra("Villain?", uValue));
                else if (pszFnName)
                    rc = printMsrFunctionExtended(uMsr, pszFnName, pszFnName, uValue, 0, 0,
                                                  annotateValueExtra("Villain?", uValue));
                else if (fReadAsZero)
                    rc = printMsrValueIgnoreWrites(uMsr, 0, annotateValueExtra("Villain?", uValue));
                else
                    rc = printMsrValueIgnoreWrites(uMsr, uValue, "Villain?");
                break;
        }
    }

    return rc;
}


static int reportMsr_GenRangeFunctionEx(VBCPUREPMSR const *paMsrs, uint32_t cMsrs, uint32_t cMax, const char *pszRdWrFnName,
                                        uint32_t uMsrBase, bool fEarlyEndOk, bool fNoIgnMask, uint64_t fSkipMask, uint32_t *pidxLoop)
{
    uint32_t uMsr   = paMsrs[0].uMsr;
    uint32_t iRange = uMsr - uMsrBase;
    Assert(cMax > iRange);
    cMax -= iRange;

    /* Resolve default function name. */
    if (!pszRdWrFnName)
    {
        pszRdWrFnName = getMsrFnName(uMsr, NULL);
        if (!pszRdWrFnName)
            return RTMsgErrorRc(VERR_INVALID_PARAMETER, "uMsr=%#x no function name\n", uMsr);
    }

    /* Figure the possible register count. */
    if (cMax > cMsrs)
        cMax = cMsrs;
    uint32_t cRegs = 1;
    while (   cRegs < cMax
           && paMsrs[cRegs].uMsr == uMsr + cRegs)
        cRegs++;

    /* Probe the first register and check that the others exhibit
       the same characteristics. */
    bool     fReadOnly0;
    uint64_t fIgnMask0, fGpMask0;
    int rc = msrProberModifyBasicTests(uMsr, fSkipMask, &fReadOnly0, &fIgnMask0, &fGpMask0);
    if (RT_FAILURE(rc))
        return rc;

    const char *pszAnnotation = NULL;
    for (uint32_t i = 1; i < cRegs; i++)
    {
        bool     fReadOnlyN;
        uint64_t fIgnMaskN, fGpMaskN;
        rc = msrProberModifyBasicTests(paMsrs[i].uMsr, fSkipMask, &fReadOnlyN, &fIgnMaskN, &fGpMaskN);
        if (RT_FAILURE(rc))
            return rc;
        if (   fReadOnlyN != fReadOnly0
            || (fIgnMaskN != fIgnMask0 && !fNoIgnMask)
            || fGpMaskN   != fGpMask0)
        {
            if (!fEarlyEndOk && !isMsrViaShanghaiDummy(uMsr, paMsrs[i].uValue, paMsrs[i].fFlags))
            {
                vbCpuRepDebug("MSR %s (%#x) range ended unexpectedly early on %#x: ro=%d ign=%#llx/%#llx gp=%#llx/%#llx [N/0]\n",
                              getMsrNameHandled(uMsr), uMsr, paMsrs[i].uMsr,
                              fReadOnlyN, fReadOnly0, fIgnMaskN, fIgnMask0, fGpMaskN, fGpMask0);
                pszAnnotation = "XXX: The range ended earlier than expected!";
            }
            cRegs = i;
            break;
        }
    }

    /*
     * Report the range (or single MSR as it might be).
     */
    *pidxLoop += cRegs - 1;

    if (fNoIgnMask)
        fIgnMask0 = 0;
    bool fSimple = fIgnMask0 == 0
                && (fGpMask0 == 0 || (fGpMask0 == UINT64_MAX && fReadOnly0))
                && iRange == 0;
    if (cRegs == 1)
        return printMsrFunctionExtendedIdxVal(uMsr, pszRdWrFnName, fReadOnly0 ? "ReadOnly" : pszRdWrFnName,
                                              iRange, fIgnMask0, fGpMask0,
                                              pszAnnotation ? pszAnnotation : annotateValue(paMsrs[0].uValue));
    if (fSimple)
        return printMsrRangeFunction(uMsr, uMsr + cRegs - 1,
                                     pszRdWrFnName, fReadOnly0 ? "ReadOnly" : pszRdWrFnName, pszAnnotation);

    return printMsrRangeFunctionExIdxVal(uMsr, uMsr + cRegs - 1, pszRdWrFnName, fReadOnly0 ? "ReadOnly" : pszRdWrFnName,
                                         iRange /*uValue*/, fIgnMask0, fGpMask0, pszAnnotation);
}


static int reportMsr_GenRangeFunction(VBCPUREPMSR const *paMsrs, uint32_t cMsrs, uint32_t cMax, const char *pszRdWrFnName,
                                      uint32_t *pidxLoop)
{
    return reportMsr_GenRangeFunctionEx(paMsrs, cMsrs, cMax, pszRdWrFnName, paMsrs[0].uMsr, false /*fEarlyEndOk*/, false /*fNoIgnMask*/,
                                        getGenericSkipMask(paMsrs[0].uMsr), pidxLoop);
}


/**
 * Generic report for an MSR implemented by functions, extended version.
 *
 * @returns VBox status code.
 * @param   uMsr            The MSR.
 * @param   pszRdWrFnName   The read/write function name, optional.
 * @param   uValue          The MSR range value.
 * @param   fSkipMask       Mask of bits to skip.
 * @param   fNoGpMask       Mask of bits to remove from the GP mask after
 *                          probing
 * @param   pszAnnotate     Annotation.
 */
static int reportMsr_GenFunctionEx(uint32_t uMsr, const char *pszRdWrFnName, uint32_t uValue,
                                   uint64_t fSkipMask, uint64_t fNoGpMask, const char *pszAnnotate)
{
    /* Resolve default function name. */
    if (!pszRdWrFnName)
    {
        pszRdWrFnName = getMsrFnName(uMsr, NULL);
        if (!pszRdWrFnName)
            return RTMsgErrorRc(VERR_INVALID_PARAMETER, "uMsr=%#x no function name\n", uMsr);
    }

    /* Probe the register and report. */
    uint64_t fIgnMask = 0;
    uint64_t fGpMask  = 0;
    int rc = msrProberModifyBitChanges(uMsr, &fIgnMask, &fGpMask, fSkipMask);
    if (RT_SUCCESS(rc))
    {
        fGpMask &= ~fNoGpMask;

        if (fGpMask == UINT64_MAX && uValue == 0 && !msrProberModifyZero(uMsr))
            rc = printMsrFunctionReadOnly(uMsr, pszRdWrFnName, pszAnnotate);
        else if (fIgnMask == UINT64_MAX && fGpMask == 0 && uValue == 0)
            rc = printMsrFunctionIgnoreWrites(uMsr, pszRdWrFnName, pszAnnotate);
        else if (fIgnMask != 0 && fGpMask == 0 && uValue == 0)
            rc = printMsrFunctionIgnoreMask(uMsr, pszRdWrFnName, NULL, fIgnMask, pszAnnotate);
        else if (fIgnMask == 0 && fGpMask == 0 && uValue == 0)
            rc = printMsrFunction(uMsr, pszRdWrFnName, NULL, pszAnnotate);
        else
            rc = printMsrFunctionExtended(uMsr, pszRdWrFnName, NULL, uValue, fIgnMask, fGpMask, pszAnnotate);
    }
    return rc;
}


/**
 * Reports a VIA/Shanghai dummy range.
 *
 * @returns VBox status code.
 * @param   paMsrs              Pointer to the first MSR.
 * @param   cMsrs               The number of MSRs in the array @a paMsr.
 * @param   pidxLoop            Index variable that should be advanced to the
 *                              last MSR entry in the range.
 */
static int reportMsr_ViaShanghaiDummyRange(VBCPUREPMSR const *paMsrs, uint32_t cMsrs, uint32_t *pidxLoop)
{
    /* Figure how many. */
    uint32_t uMsr  = paMsrs[0].uMsr;
    uint32_t cRegs = 1;
    while (   cRegs < cMsrs
           && paMsrs[cRegs].uMsr == uMsr + cRegs
           && isMsrViaShanghaiDummy(paMsrs[cRegs].uMsr, paMsrs[cRegs].uValue, paMsrs[cRegs].fFlags))
    {
        cRegs++;
        if (!(cRegs % 0x80))
            vbCpuRepDebug("VIA dummy detection %#llx..%#llx (%#x regs)...\n", uMsr, uMsr + cRegs - 1, cRegs);
    }

    /* Advance. */
    *pidxLoop += cRegs - 1;

    /* Report it/them. */
    char szName[80];
    if (cRegs == 1)
    {
        RTStrPrintf(szName, sizeof(szName), "ZERO_%04x_%04x", RT_HI_U16(uMsr), RT_LO_U16(uMsr));
        return printMsrValueIgnoreWritesNamed(uMsr, 0, szName, NULL);
    }

    uint32_t uMsrLast = uMsr +  cRegs - 1;
    RTStrPrintf(szName, sizeof(szName), "ZERO_%04x_%04x_THRU_%04x_%04x",
                RT_HI_U16(uMsr), RT_LO_U16(uMsr), RT_HI_U16(uMsrLast), RT_LO_U16(uMsrLast));
    return printMsrRangeValueIgnoreWritesNamed(uMsr, uMsrLast, 0, szName, NULL);
}


/**
 * Special function for reporting the IA32_APIC_BASE register, as it seems to be
 * causing trouble on newer systems.
 *
 * @returns
 * @param   uMsr                The MSR number.
 * @param   uValue              The value.
 */
static int reportMsr_Ia32ApicBase(uint32_t uMsr, uint64_t uValue)
{
    /* Trouble with the generic treatment of both the "APIC Global Enable" and
       "Enable x2APIC mode" bits on an i7-3820QM running OS X 10.8.5.  */
    uint64_t fSkipMask = RT_BIT_64(11);
    if (vbCpuRepSupportsX2Apic())
        fSkipMask |= RT_BIT_64(10);
    /* For some reason, twiddling this bit kills a Tualatin PIII-S. */
    if (g_enmMicroarch == kCpumMicroarch_Intel_P6_III)
        fSkipMask |= RT_BIT(9);

    /* If the OS uses the APIC, we have to be super careful. */
    if (!g_MsrAcc.fAtomic)
        fSkipMask |= UINT64_C(0x0000000ffffff000);

    /** @todo This makes the host unstable on a AMD Ryzen 1800X CPU, skip everything for now.
     * Figure out exactly what causes the issue.
     */
    if (   g_enmMicroarch >= kCpumMicroarch_AMD_Zen_First
        && g_enmMicroarch >= kCpumMicroarch_AMD_Zen_End)
        fSkipMask |= UINT64_C(0xffffffffffffffff);

    return reportMsr_GenFunctionEx(uMsr, "Ia32ApicBase", uValue, fSkipMask, 0, NULL);
}


/**
 * Special function for reporting the IA32_MISC_ENABLE register, as it seems to
 * be causing trouble on newer systems.
 *
 * @returns
 * @param   uMsr                The MSR number.
 * @param   uValue              The value.
 */
static int reportMsr_Ia32MiscEnable(uint32_t uMsr, uint64_t uValue)
{
    uint64_t fSkipMask = 0;

    if (   (   g_enmMicroarch >= kCpumMicroarch_Intel_Core7_Broadwell
            && g_enmMicroarch <= kCpumMicroarch_Intel_Core7_End)
        || (   g_enmMicroarch >= kCpumMicroarch_Intel_Atom_Airmount
            && g_enmMicroarch <= kCpumMicroarch_Intel_Atom_End)
       )
    {
        vbCpuRepPrintf("WARNING: IA32_MISC_ENABLE probing needs hacking on this CPU!\n");
        RTThreadSleep(128);
    }

    /* If the OS is using MONITOR/MWAIT we'd better not disable it! */
    if (!g_MsrAcc.fAtomic)
        fSkipMask |= RT_BIT(18);

    /* The no execute related flag is deadly if clear.  */
    if (   !(uValue & MSR_IA32_MISC_ENABLE_XD_DISABLE)
        && (   g_enmMicroarch <  kCpumMicroarch_Intel_First
            || g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah
            || vbCpuRepSupportsNX() ) )
        fSkipMask |= MSR_IA32_MISC_ENABLE_XD_DISABLE;

    uint64_t fIgnMask = 0;
    uint64_t fGpMask  = 0;
    int rc = msrProberModifyBitChanges(uMsr, &fIgnMask, &fGpMask, fSkipMask);
    if (RT_SUCCESS(rc))
        rc = printMsrFunctionExtended(uMsr, "Ia32MiscEnable", "Ia32MiscEnable", uValue,
                                      fIgnMask, fGpMask, annotateValue(uValue));
    return rc;
}


/**
 * Verifies that MTRR type field works correctly in the given MSR.
 *
 * @returns VBox status code (failure if bad MSR behavior).
 * @param   uMsr                The MSR.
 * @param   iBit                The first bit of the type field (8-bit wide).
 * @param   cExpected           The number of types expected - PAT=8, MTRR=7.
 */
static int msrVerifyMtrrTypeGPs(uint32_t uMsr, uint32_t iBit, uint32_t cExpected)
{
    uint32_t uEndTypes = 0;
    while (uEndTypes < 255)
    {
        bool fGp = !msrProberModifySimpleGp(uMsr, ~(UINT64_C(0xff) << iBit), (uint64_t)uEndTypes << iBit);
        if (!fGp && (uEndTypes == 2 || uEndTypes == 3))
            return RTMsgErrorRc(VERR_INVALID_PARAMETER, "MTRR types %u does not cause a GP as it should. (msr %#x)\n",
                                uEndTypes, uMsr);
        if (fGp && uEndTypes != 2 && uEndTypes != 3)
            break;
        uEndTypes++;
    }
    if (uEndTypes != cExpected)
        return RTMsgErrorRc(VERR_INVALID_PARAMETER, "MTRR types detected to be %#x (msr %#x). Expected %#x.\n",
                            uEndTypes, uMsr, cExpected);
    return VINF_SUCCESS;
}


/**
 * Deals with the variable MTRR MSRs.
 *
 * @returns VBox status code.
 * @param   paMsrs              Pointer to the first variable MTRR MSR (200h).
 * @param   cMsrs               The number of MSRs in the array @a paMsr.
 * @param   pidxLoop            Index variable that should be advanced to the
 *                              last MTRR MSR entry.
 */
static int reportMsr_Ia32MtrrPhysBaseMaskN(VBCPUREPMSR const *paMsrs, uint32_t cMsrs, uint32_t *pidxLoop)
{
    uint32_t uMsr = paMsrs[0].uMsr;

    /* Count them. */
    uint32_t cRegs = 1;
    while (   cRegs < cMsrs
           && paMsrs[cRegs].uMsr == uMsr + cRegs
           && !isMsrViaShanghaiDummy(paMsrs[cRegs].uMsr, paMsrs[cRegs].uValue, paMsrs[cRegs].fFlags) )
        cRegs++;
    if (cRegs & 1)
        return RTMsgErrorRc(VERR_INVALID_PARAMETER, "MTRR variable MSR range is odd: cRegs=%#x\n", cRegs);
    if (cRegs > 0x20)
        return RTMsgErrorRc(VERR_INVALID_PARAMETER, "MTRR variable MSR range is too large: cRegs=%#x\n", cRegs);

    /* Find a disabled register that we can play around with. */
    uint32_t iGuineaPig;
    for (iGuineaPig = 0; iGuineaPig < cRegs; iGuineaPig += 2)
        if (!(paMsrs[iGuineaPig + 1].uValue & RT_BIT_32(11)))
            break;
    if (iGuineaPig >= cRegs)
        iGuineaPig = cRegs - 2;
    vbCpuRepDebug("iGuineaPig=%#x -> %#x\n", iGuineaPig, uMsr + iGuineaPig);

    /* Probe the base.  */
    uint64_t fIgnBase = 0;
    uint64_t fGpBase  = 0;
    int rc = msrProberModifyBitChanges(uMsr + iGuineaPig, &fIgnBase, &fGpBase, 0);
    if (RT_FAILURE(rc))
        return rc;
    rc = msrVerifyMtrrTypeGPs(uMsr + iGuineaPig, 0, 7);
    if (RT_FAILURE(rc))
        return rc;
    vbCpuRepDebug("fIgnBase=%#llx fGpBase=%#llx\n", fIgnBase, fGpBase);

    /* Probing the mask is relatively straight forward. */
    uint64_t fIgnMask = 0;
    uint64_t fGpMask  = 0;
    rc = msrProberModifyBitChanges(uMsr + iGuineaPig + 1, &fIgnMask, &fGpMask, 0x800); /* enabling it may cause trouble */
    if (RT_FAILURE(rc))
        return rc;
    vbCpuRepDebug("fIgnMask=%#llx fGpMask=%#llx\n", fIgnMask, fGpMask);

    /* Validate that the whole range subscribes to the apprimately same GP rules. */
    for (uint32_t i = 0; i < cRegs; i += 2)
    {
        uint64_t fSkipBase = ~fGpBase;
        uint64_t fSkipMask = ~fGpMask;
        if (!(paMsrs[i + 1].uValue & RT_BIT_32(11)))
            fSkipBase = fSkipMask = 0;
        fSkipBase |= 0x7;           /* Always skip the type. */
        fSkipMask |= RT_BIT_32(11); /* Always skip the enable bit. */

        vbCpuRepDebug("i=%#x fSkipBase=%#llx fSkipMask=%#llx\n", i, fSkipBase, fSkipMask);

        if (!(paMsrs[i + 1].uValue & RT_BIT_32(11)))
        {
            rc = msrVerifyMtrrTypeGPs(uMsr + iGuineaPig, 0, 7);
            if (RT_FAILURE(rc))
                return rc;
        }

        uint64_t fIgnBaseN = 0;
        uint64_t fGpBaseN  = 0;
        rc = msrProberModifyBitChanges(uMsr + i, &fIgnBaseN, &fGpBaseN, fSkipBase);
        if (RT_FAILURE(rc))
            return rc;

        if (   fIgnBaseN != (fIgnBase & ~fSkipBase)
            || fGpBaseN  != (fGpBase  & ~fSkipBase) )
            return RTMsgErrorRc(VERR_INVALID_PARAMETER,
                                "MTRR PHYS BASE register %#x behaves differently from %#x: ign=%#llx/%#llx gp=%#llx/%#llx (fSkipBase=%#llx)\n",
                                uMsr + i, uMsr + iGuineaPig,
                                fIgnBaseN, fIgnBase & ~fSkipBase, fGpBaseN, fGpBase & ~fSkipBase, fSkipBase);

        uint64_t fIgnMaskN = 0;
        uint64_t fGpMaskN  = 0;
        rc = msrProberModifyBitChanges(uMsr + i + 1, &fIgnMaskN, &fGpMaskN, fSkipMask);
        if (RT_FAILURE(rc))
            return rc;
        if (   fIgnMaskN != (fIgnMask & ~fSkipMask)
            || fGpMaskN  != (fGpMask  & ~fSkipMask) )
            return RTMsgErrorRc(VERR_INVALID_PARAMETER,
                                "MTRR PHYS MASK register %#x behaves differently from %#x: ign=%#llx/%#llx gp=%#llx/%#llx (fSkipMask=%#llx)\n",
                                uMsr + i + 1, uMsr + iGuineaPig + 1,
                                fIgnMaskN, fIgnMask & ~fSkipMask, fGpMaskN, fGpMask & ~fSkipMask, fSkipMask);
    }

    /* Print the whole range. */
    fGpBase &= ~(uint64_t)0x7; /* Valid type bits, see msrVerifyMtrrTypeGPs(). */
    for (uint32_t i = 0; i < cRegs; i += 2)
    {
        printMsrFunctionExtendedIdxVal(uMsr + i,     "Ia32MtrrPhysBaseN", NULL, i / 2, fIgnBase, fGpBase,
                                       annotateValue(paMsrs[i].uValue));
        printMsrFunctionExtendedIdxVal(uMsr + i + 1, "Ia32MtrrPhysMaskN", NULL, i / 2, fIgnMask, fGpMask,
                                       annotateValue(paMsrs[i + 1].uValue));
    }

    *pidxLoop += cRegs - 1;
    return VINF_SUCCESS;
}


/**
 * Deals with fixed MTRR and PAT MSRs, checking the 8 memory type fields.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR.
 */
static int reportMsr_Ia32MtrrFixedOrPat(uint32_t uMsr)
{
    /* Had a spot of trouble on an old macbook pro with core2 duo T9900 (penryn)
       running 64-bit win81pe. Not giving PAT such a scrutiny fixes it. */
    /* This hangs the host on a AMD Ryzen 1800X CPU */
    if (   uMsr != 0x00000277
        || (  g_enmVendor == CPUMCPUVENDOR_INTEL
            ? g_enmMicroarch >= kCpumMicroarch_Intel_Core7_First
            : g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON
            ? (   g_enmMicroarch != kCpumMicroarch_AMD_K8_90nm_AMDV
               && !CPUMMICROARCH_IS_AMD_FAM_ZEN(g_enmMicroarch))
            : true) )
    {
        /* Every 8 bytes is a type, check the type ranges one by one. */
        for (uint32_t iBit = 0; iBit < 64; iBit += 8)
        {
            int rc = msrVerifyMtrrTypeGPs(uMsr, iBit, 7 + (uMsr == 0x00000277));
            if (RT_FAILURE(rc))
                return rc;
        }
    }

    return printMsrFunctionCpumCpu(uMsr, NULL, NULL, NULL, NULL);
}


/**
 * Deals with IA32_MTRR_DEF_TYPE.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR.
 */
static int reportMsr_Ia32MtrrDefType(uint32_t uMsr)
{
    uint64_t fGpMask  = 0;
    uint64_t fIgnMask = 0;
    if (g_enmMicroarch == kCpumMicroarch_AMD_K8_90nm_AMDV)
    {
        /* Problematic CPU! Fake it for now. */
        fGpMask = ~(uint64_t)0xc07;
        fIgnMask = 0;
    }
    else
    {
        int rc = msrVerifyMtrrTypeGPs(uMsr, 0, 7);
        if (RT_FAILURE(rc))
            return rc;

        rc = msrProberModifyBitChanges(uMsr, &fIgnMask, &fGpMask, 0x7);
        if (RT_FAILURE(rc))
            return rc;
        Assert(!(fGpMask & 7)); Assert(!(fIgnMask & 7));
    }

    return printMsrFunctionCpumCpuEx(uMsr, NULL, NULL, NULL, fIgnMask, fGpMask, NULL);
}


/**
 * Deals with the Machine Check (MC) MSRs in the 400h+ area.
 *
 * @returns VBox status code.
 * @param   paMsrs              Pointer to the first MC MSR (400h).
 * @param   cMsrs               The number of MSRs in the array @a paMsr.
 * @param   pidxLoop            Index variable that should be advanced to the
 *                              last MC MSR entry.
 */
static int reportMsr_Ia32McCtlStatusAddrMiscN(VBCPUREPMSR const *paMsrs, uint32_t cMsrs, uint32_t *pidxLoop)
{
    uint32_t uMsr = paMsrs[0].uMsr;

    /* Count them. */
    uint32_t cRegs = 1;
    uint32_t cDetectedRegs = 1;
    while (   cDetectedRegs < cMsrs
           && (   paMsrs[cDetectedRegs].uMsr == uMsr + cRegs
               || (cRegs & 3) == 2 /* ADDR may or may not be there, depends on STATUS and CPU. */
               || (cRegs & 3) == 3 /* MISC may or may not be there, depends on STATUS and CPU. */
               || cRegs == 0x13 /* MC4_MISC may not be there, depends on CPU. */
               || cRegs == 0x14 /* MC5_CTL may not be there, depends on CPU. */)
           && cRegs < 0x7f )
    {
        if (paMsrs[cDetectedRegs].uMsr == uMsr + cRegs)
            cDetectedRegs++;
        cRegs++;
    }

    /** aeichner: An AMD Ryzen 7 1800X CPU triggers this and I'm too lazy to check the correctness in detail. */
    if (   (cRegs & 3)
        && !CPUMMICROARCH_IS_AMD_FAM_ZEN(g_enmMicroarch))
        return RTMsgErrorRc(VERR_INVALID_PARAMETER, "MC MSR range is odd: cRegs=%#x\n", cRegs);

    /* Just report them.  We don't bother probing here as the CTL format
       and such seems to be a lot of work to test correctly and changes between
       cpu generations.  */
    *pidxLoop += cDetectedRegs - 1;
    return printMsrRangeFunction(uMsr, uMsr + cRegs - 1, "Ia32McCtlStatusAddrMiscN", NULL, NULL);
}



/**
 * Deals with the X2APIC msrs.
 *
 * @returns VBox status code.
 * @param   paMsrs              Pointer to the first X2APIC MSR.
 * @param   cMsrs               The number of MSRs in the array @a paMsr.
 * @param   pidxLoop            Index variable that should be advanced to the
 *                              last X2APIC MSR entry.
 */
static int reportMsr_GenX2Apic(VBCPUREPMSR const *paMsrs, uint32_t cMsrs, uint32_t *pidxLoop)
{
    /* Advance. */
    uint32_t cRegs = 1;
    while (   cRegs < cMsrs
           && paMsrs[cRegs].uMsr <= 0x8ff)
        cRegs++;
    *pidxLoop += cRegs - 1;

    /* Just emit an X2APIC range. */
    return printMsrRangeFunction(0x800, 0x8ff, "Ia32X2ApicN", NULL, NULL);
}


/**
 * Deals carefully with the EFER register.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR number.
 * @param   uValue              The current value.
 */
static int reportMsr_Amd64Efer(uint32_t uMsr, uint64_t uValue)
{
    uint64_t fSkipMask = 0;
    if (vbCpuRepSupportsLongMode())
    {
        fSkipMask |= MSR_K6_EFER_LME;
        if (!g_MsrAcc.fAtomic && (uValue & MSR_K6_EFER_SCE))
            fSkipMask |= MSR_K6_EFER_SCE;
    }
    if (   (uValue & MSR_K6_EFER_NXE)
        || vbCpuRepSupportsNX())
        fSkipMask |= MSR_K6_EFER_NXE;

    /* NetBurst prescott 2MB (model 4) hung or triple faulted here.  The extra
       sleep or something seemed to help for some screwed up reason. */
    if (g_fIntelNetBurst)
    {
        // This doesn't matter:
        //fSkipMask |= MSR_K6_EFER_SCE;
        //if (vbCpuRepSupportsLongMode())
        //    fSkipMask |= MSR_K6_EFER_LMA;
        //vbCpuRepDebug("EFER - netburst workaround - ignore SCE & LMA (fSkipMask=%#llx)\n", fSkipMask);

        vbCpuRepDebug("EFER - netburst sleep fudge - fSkipMask=%#llx\n", fSkipMask);
        RTThreadSleep(1000);
    }

    return reportMsr_GenFunctionEx(uMsr, NULL, uValue, fSkipMask, MSR_K6_EFER_LMA, NULL);
}


/**
 * Deals with the MC4_MISCn (n >= 1) range and the following reserved MSRs.
 *
 * @returns VBox status code.
 * @param   paMsrs              Pointer to the first MSR.
 * @param   cMsrs               The number of MSRs in the array @a paMsr.
 * @param   pidxLoop            Index variable that should be advanced to the
 *                              last MSR entry in the range.
 */
static int reportMsr_AmdFam10hMc4MiscN(VBCPUREPMSR const *paMsrs, uint32_t cMsrs, uint32_t *pidxLoop)
{
    /* Count registers. */
    uint32_t cRegs = 1;
    while (   cRegs < cMsrs
           && cRegs < 8
           && paMsrs[cRegs].uMsr == paMsrs[0].uMsr + cRegs)
        cRegs++;

    /* Probe & report used MSRs. */
    uint64_t fIgnMask = 0;
    uint64_t fGpMask  = 0;
    uint32_t cUsed    = 0;
    while (cUsed < cRegs)
    {
        uint64_t fIgnMaskN = 0;
        uint64_t fGpMaskN  = 0;
        int rc = msrProberModifyBitChanges(paMsrs[cUsed].uMsr, &fIgnMaskN, &fGpMaskN, 0);
        if (RT_FAILURE(rc))
            return rc;
        if (fIgnMaskN == UINT64_MAX || fGpMaskN == UINT64_MAX)
            break;
        if (cUsed == 0)
        {
            fIgnMask = fIgnMaskN;
            fGpMask  = fGpMaskN;
        }
        else if (   fIgnMaskN != fIgnMask
                 || fGpMaskN  != fGpMask)
            return RTMsgErrorRc(VERR_NOT_EQUAL, "AmdFam16hMc4MiscN mismatch: fIgn=%#llx/%#llx fGp=%#llx/%#llx uMsr=%#x\n",
                                fIgnMaskN, fIgnMask, fGpMaskN, fGpMask, paMsrs[cUsed].uMsr);
        cUsed++;
    }
    if (cUsed > 0)
        printMsrRangeFunctionEx(paMsrs[0].uMsr, paMsrs[cUsed - 1].uMsr, "AmdFam10hMc4MiscN", NULL, 0, fIgnMask, fGpMask, NULL);

    /* Probe & report reserved MSRs. */
    uint32_t cReserved = 0;
    while (cUsed + cReserved < cRegs)
    {
        fIgnMask = fGpMask = 0;
        int rc = msrProberModifyBitChanges(paMsrs[cUsed + cReserved].uMsr, &fIgnMask, &fGpMask, 0);
        if (RT_FAILURE(rc))
            return rc;
        if ((fIgnMask != UINT64_MAX && fGpMask != UINT64_MAX) || paMsrs[cUsed + cReserved].uValue)
            return RTMsgErrorRc(VERR_NOT_EQUAL,
                                "Unexpected reserved AmdFam16hMc4MiscN: fIgn=%#llx fGp=%#llx uMsr=%#x uValue=%#llx\n",
                                fIgnMask, fGpMask, paMsrs[cUsed + cReserved].uMsr, paMsrs[cUsed + cReserved].uValue);
        cReserved++;
    }
    if (cReserved > 0 && fIgnMask == UINT64_MAX)
        printMsrRangeValueIgnoreWrites(paMsrs[cUsed].uMsr, paMsrs[cUsed + cReserved - 1].uMsr, 0, NULL);
    else if (cReserved > 0 && fGpMask == UINT64_MAX)
        printMsrRangeValueReadOnly(paMsrs[cUsed].uMsr, paMsrs[cUsed + cReserved - 1].uMsr, 0, NULL);

    *pidxLoop += cRegs - 1;
    return VINF_SUCCESS;
}


/**
 * Deals with the AMD PERF_CTL range.
 *
 * @returns VBox status code.
 * @param   paMsrs              Pointer to the first MSR.
 * @param   cMsrs               The number of MSRs in the array @a paMsr.
 * @param   pidxLoop            Index variable that should be advanced to the
 *                              last MSR entry in the range.
 */
static int reportMsr_AmdK8PerfCtlN(VBCPUREPMSR const *paMsrs, uint32_t cMsrs, uint32_t *pidxLoop)
{
    uint32_t uMsr = paMsrs[0].uMsr;
    Assert(uMsr == 0xc0010000);

    /* Family 15h (bulldozer +) aliases these registers sparsely onto c001020x. */
    if (CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch))
    {
        for (uint32_t i = 0; i < 4; i++)
            printMsrAlias(uMsr + i, 0xc0010200 + i * 2, NULL);
        *pidxLoop += 3;
    }
    else
        return reportMsr_GenRangeFunction(paMsrs, cMsrs, 4, "AmdK8PerfCtlN", pidxLoop);
    return VINF_SUCCESS;
}


/**
 * Deals with the AMD PERF_CTR range.
 *
 * @returns VBox status code.
 * @param   paMsrs              Pointer to the first MSR.
 * @param   cMsrs               The number of MSRs in the array @a paMsr.
 * @param   pidxLoop            Index variable that should be advanced to the
 *                              last MSR entry in the range.
 */
static int reportMsr_AmdK8PerfCtrN(VBCPUREPMSR const *paMsrs, uint32_t cMsrs, uint32_t *pidxLoop)
{
    uint32_t uMsr = paMsrs[0].uMsr;
    Assert(uMsr == 0xc0010004);

    /* Family 15h (bulldozer +) aliases these registers sparsely onto c001020x. */
    if (CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch))
    {
        for (uint32_t i = 0; i < 4; i++)
            printMsrAlias(uMsr + i, 0xc0010201 + i * 2, NULL);
        *pidxLoop += 3;
    }
    else
        return reportMsr_GenRangeFunction(paMsrs, cMsrs, 4, "AmdK8PerfCtrN", pidxLoop);
    return VINF_SUCCESS;
}


/**
 * Deals carefully with the SYS_CFG register.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR number.
 * @param   uValue              The current value.
 */
static int reportMsr_AmdK8SysCfg(uint32_t uMsr, uint64_t uValue)
{
    uint64_t fSkipMask = 0;

    /* Bit 21 (MtrrTom2En) is marked reserved in family 0fh, while in family
       10h BKDG this changes (as does the document style).  Testing this bit
       causes bulldozer running win64 to restart, thus this special treatment. */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_K10)
        fSkipMask |= RT_BIT(21);

    /* Turns out there are more killer bits here, at least on Opteron 2384.
       Skipping all known bits. */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_K8_90nm_AMDV /* Not sure when introduced - harmless? */)
        fSkipMask |= RT_BIT(22); /* Tom2ForceMemTypeWB */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_K8_First)
        fSkipMask |= RT_BIT(21); /* MtrrTom2En */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_K8_First)
        fSkipMask |= RT_BIT(20); /* MtrrVarDramEn*/
    if (g_enmMicroarch >= kCpumMicroarch_AMD_K8_First)
        fSkipMask |= RT_BIT(19); /* MtrrFixDramModEn */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_K8_First)
        fSkipMask |= RT_BIT(18); /* MtrrFixDramEn */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_K8_First)
        fSkipMask |= RT_BIT(17); /* SysUcLockEn */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_K8_First)
        fSkipMask |= RT_BIT(16); /* ChgToDirtyDis */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_K8_First && g_enmMicroarch < kCpumMicroarch_AMD_15h_First)
        fSkipMask |= RT_BIT(10); /* SetDirtyEnO */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_K8_First && g_enmMicroarch < kCpumMicroarch_AMD_15h_First)
        fSkipMask |= RT_BIT(9);  /* SetDirtyEnS */
    if (   CPUMMICROARCH_IS_AMD_FAM_0FH(g_enmMicroarch)
        || CPUMMICROARCH_IS_AMD_FAM_10H(g_enmMicroarch))
        fSkipMask |= RT_BIT(8);  /* SetDirtyEnE */
    if (   CPUMMICROARCH_IS_AMD_FAM_0FH(g_enmMicroarch)
        || CPUMMICROARCH_IS_AMD_FAM_11H(g_enmMicroarch) )
        fSkipMask |= RT_BIT(7)   /* SysVicLimit */
                  |  RT_BIT(6)   /* SysVicLimit */
                  |  RT_BIT(5)   /* SysVicLimit */
                  |  RT_BIT(4)   /* SysAckLimit */
                  |  RT_BIT(3)   /* SysAckLimit */
                  |  RT_BIT(2)   /* SysAckLimit */
                  |  RT_BIT(1)   /* SysAckLimit */
                  |  RT_BIT(0)   /* SysAckLimit */;

    return reportMsr_GenFunctionEx(uMsr, NULL, uValue, fSkipMask, 0, annotateValue(uValue));
}


/**
 * Deals carefully with the HWCR register.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR number.
 * @param   uValue              The current value.
 */
static int reportMsr_AmdK8HwCr(uint32_t uMsr, uint64_t uValue)
{
    uint64_t fSkipMask = 0;

    /* Trouble on Opteron 2384, skip some of the known bits. */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_K10 && !CPUMMICROARCH_IS_AMD_FAM_11H(g_enmMicroarch))
        fSkipMask |= /*RT_BIT(10)*/ 0  /* MonMwaitUserEn */
                  |  RT_BIT(9);  /* MonMwaitDis */
    fSkipMask |= RT_BIT(8);      /* #IGNNE port emulation */
    if (   CPUMMICROARCH_IS_AMD_FAM_0FH(g_enmMicroarch)
        || CPUMMICROARCH_IS_AMD_FAM_11H(g_enmMicroarch) )
        fSkipMask |= RT_BIT(7)   /* DisLock */
                  |  RT_BIT(6);  /* FFDis (TLB flush filter) */
    fSkipMask |= RT_BIT(4);      /* INVD to WBINVD */
    fSkipMask |= RT_BIT(3);      /* TLBCACHEDIS */
    if (   CPUMMICROARCH_IS_AMD_FAM_0FH(g_enmMicroarch)
        || CPUMMICROARCH_IS_AMD_FAM_10H(g_enmMicroarch)
        || CPUMMICROARCH_IS_AMD_FAM_11H(g_enmMicroarch) )
        fSkipMask |= RT_BIT(1);  /* SLOWFENCE */
    fSkipMask |= RT_BIT(0);      /* SMMLOCK */

    return reportMsr_GenFunctionEx(uMsr, NULL, uValue, fSkipMask, 0, annotateValue(uValue));
}


/**
 * Deals carefully with a IORRBasei register.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR number.
 * @param   uValue              The current value.
 */
static int reportMsr_AmdK8IorrBaseN(uint32_t uMsr, uint64_t uValue)
{
    /* Skip know bits here, as harm seems to come from messing with them. */
    uint64_t fSkipMask = RT_BIT(4) | RT_BIT(3);
    fSkipMask |= (RT_BIT_64(vbCpuRepGetPhysAddrWidth()) - 1) & X86_PAGE_4K_BASE_MASK;
    return reportMsr_GenFunctionEx(uMsr, NULL, (uMsr - 0xc0010016) / 2, fSkipMask, 0, annotateValue(uValue));
}


/**
 * Deals carefully with a IORRMaski register.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR number.
 * @param   uValue              The current value.
 */
static int reportMsr_AmdK8IorrMaskN(uint32_t uMsr, uint64_t uValue)
{
    /* Skip know bits here, as harm seems to come from messing with them. */
    uint64_t fSkipMask = RT_BIT(11);
    fSkipMask |= (RT_BIT_64(vbCpuRepGetPhysAddrWidth()) - 1) & X86_PAGE_4K_BASE_MASK;
    return reportMsr_GenFunctionEx(uMsr, NULL, (uMsr - 0xc0010017) / 2, fSkipMask, 0, annotateValue(uValue));
}


/**
 * Deals carefully with a IORRMaski register.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR number.
 * @param   uValue              The current value.
 */
static int reportMsr_AmdK8TopMemN(uint32_t uMsr, uint64_t uValue)
{
    /* Skip know bits here, as harm seems to come from messing with them. */
    uint64_t fSkipMask = (RT_BIT_64(vbCpuRepGetPhysAddrWidth()) - 1) & ~(RT_BIT_64(23) - 1);
    return reportMsr_GenFunctionEx(uMsr, NULL, uMsr == 0xc001001d, fSkipMask, 0, annotateValue(uValue));
}


/**
 * Deals with the AMD P-state config range.
 *
 * @returns VBox status code.
 * @param   paMsrs              Pointer to the first MSR.
 * @param   cMsrs               The number of MSRs in the array @a paMsr.
 * @param   pidxLoop            Index variable that should be advanced to the
 *                              last MSR entry in the range.
 */
static int reportMsr_AmdFam10hPStateN(VBCPUREPMSR const *paMsrs, uint32_t cMsrs, uint32_t *pidxLoop)
{
    uint32_t uMsr = paMsrs[0].uMsr;
    AssertRelease(uMsr == 0xc0010064);

    /* Count them. */
    uint32_t cRegs = 1;
    while (   cRegs < 8
           && cRegs < cMsrs
           && paMsrs[cRegs].uMsr == uMsr + cRegs)
        cRegs++;

    /* Figure out which bits we should skip when probing.  This is based on
       specs and may need adjusting for real life when handy. */
    uint64_t fSkipMask = RT_BIT_64(63);             /* PstateEn */
    fSkipMask |= RT_BIT_64(41) | RT_BIT_64(40);     /* IddDiv */
    fSkipMask |= UINT64_C(0x000000ff00000000);      /* IddValue */
    if (CPUMMICROARCH_IS_AMD_FAM_10H(g_enmMicroarch))
        fSkipMask |= UINT32_C(0xfe000000);          /* NbVid - Northbridge VID */
    if (   CPUMMICROARCH_IS_AMD_FAM_10H(g_enmMicroarch)
        || CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch))
        fSkipMask |= RT_BIT_32(22);                 /* NbDid or NbPstate. */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_15h_Piledriver) /* ?? - listed in 10-1Fh model BDKG as well asFam16h */
        fSkipMask |= RT_BIT_32(16);                 /* CpuVid[7] */
    fSkipMask |= UINT32_C(0x0000fe00);              /* CpuVid[6:0] */
    fSkipMask |= UINT32_C(0x000001c0);              /* CpuDid */
    fSkipMask |= UINT32_C(0x0000003f);              /* CpuFid */

    /* Probe and report them one by one since we're passing values instead of
       register indexes to the functions. */
    for (uint32_t i = 0; i < cRegs; i++)
    {
        uint64_t fIgnMask = 0;
        uint64_t fGpMask = 0;
        int rc = msrProberModifyBitChanges(uMsr + i, &fIgnMask, &fGpMask, fSkipMask);
        if (RT_FAILURE(rc))
            return rc;
        printMsrFunctionExtended(uMsr + i, "AmdFam10hPStateN", NULL, paMsrs[i].uValue, fIgnMask, fGpMask,
                                 annotateValue(paMsrs[i].uValue));
    }

    /* Advance. */
    *pidxLoop += cRegs - 1;
    return VINF_SUCCESS;
}


/**
 * Deals carefully with a COFVID control register.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR number.
 * @param   uValue              The current value.
 */
static int reportMsr_AmdFam10hCofVidControl(uint32_t uMsr, uint64_t uValue)
{
    /* Skip know bits here, as harm seems to come from messing with them. */
    uint64_t fSkipMask = 0;
    if (CPUMMICROARCH_IS_AMD_FAM_10H(g_enmMicroarch))
        fSkipMask |= UINT32_C(0xfe000000);          /* NbVid - Northbridge VID */
    else if (g_enmMicroarch >= kCpumMicroarch_AMD_15h_First) /* Listed in preliminary Fam16h BDKG. */
        fSkipMask |= UINT32_C(0xff000000);          /* NbVid - Northbridge VID - includes bit 24 for Fam15h and Fam16h. Odd... */
    if (   CPUMMICROARCH_IS_AMD_FAM_10H(g_enmMicroarch)
        || g_enmMicroarch >= kCpumMicroarch_AMD_15h_First) /* Listed in preliminary Fam16h BDKG. */
        fSkipMask |= RT_BIT_32(22);                 /* NbDid or NbPstate. */
    if (g_enmMicroarch >= kCpumMicroarch_AMD_15h_Piledriver) /* ?? - listed in 10-1Fh model BDKG as well asFam16h */
        fSkipMask |= RT_BIT_32(20);                 /* CpuVid[7] */
    fSkipMask |= UINT32_C(0x00070000);              /* PstatId */
    fSkipMask |= UINT32_C(0x0000fe00);              /* CpuVid[6:0] */
    fSkipMask |= UINT32_C(0x000001c0);              /* CpuDid */
    fSkipMask |= UINT32_C(0x0000003f);              /* CpuFid */

    return reportMsr_GenFunctionEx(uMsr, NULL, uValue, fSkipMask, 0, annotateValue(uValue));
}


/**
 * Deals with the AMD [|L2I_|NB_]PERF_CT[LR] mixed ranges.
 *
 * Mixed here refers to the control and counter being in mixed in pairs as
 * opposed to them being two separate parallel arrays like in the 0xc0010000
 * area.
 *
 * @returns VBox status code.
 * @param   paMsrs              Pointer to the first MSR.
 * @param   cMsrs               The number of MSRs in the array @a paMsr.
 * @param   cMax                The max number of MSRs (not counters).
 * @param   pidxLoop            Index variable that should be advanced to the
 *                              last MSR entry in the range.
 */
static int reportMsr_AmdGenPerfMixedRange(VBCPUREPMSR const *paMsrs, uint32_t cMsrs, uint32_t cMax, uint32_t *pidxLoop)
{
    uint32_t uMsr = paMsrs[0].uMsr;

    /* Count them. */
    uint32_t cRegs = 1;
    while (   cRegs < cMax
           && cRegs < cMsrs
           && paMsrs[cRegs].uMsr == uMsr + cRegs)
        cRegs++;
    if (cRegs & 1)
        return RTMsgErrorRc(VERR_INVALID_PARAMETER, "PERF range at %#x is odd: cRegs=%#x\n", uMsr, cRegs);

    /* Report them as individual entries, using default names and such. */
    for (uint32_t i = 0; i < cRegs; i++)
    {
        uint64_t fIgnMask = 0;
        uint64_t fGpMask  = 0;
        int rc = msrProberModifyBitChanges(uMsr + i, &fIgnMask, &fGpMask, 0);
        if (RT_FAILURE(rc))
            return rc;
        printMsrFunctionExtendedIdxVal(uMsr + i, NULL, NULL, i / 2, fIgnMask, fGpMask, annotateValue(paMsrs[i].uValue));
    }

    /* Advance. */
    *pidxLoop += cRegs - 1;
    return VINF_SUCCESS;
}


/**
 * Deals carefully with a LS_CFG register.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR number.
 * @param   uValue              The current value.
 */
static int reportMsr_AmdK7InstrCacheCfg(uint32_t uMsr, uint64_t uValue)
{
    /* Skip know bits here, as harm seems to come from messing with them. */
    uint64_t fSkipMask = RT_BIT_64(9) /* DIS_SPEC_TLB_RLD */;
    if (CPUMMICROARCH_IS_AMD_FAM_10H(g_enmMicroarch))
        fSkipMask |= RT_BIT_64(14); /* DIS_IND */
    if (CPUMMICROARCH_IS_AMD_FAM_16H(g_enmMicroarch))
        fSkipMask |= RT_BIT_64(26); /* DIS_WIDEREAD_PWR_SAVE */
    if (CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch))
    {
        fSkipMask |= 0x1e;          /* DisIcWayFilter */
        fSkipMask |= RT_BIT_64(39); /* DisLoopPredictor */
        fSkipMask |= RT_BIT_64(27); /* Unknown killer bit, possibly applicable to other microarchs. */
        fSkipMask |= RT_BIT_64(28); /* Unknown killer bit, possibly applicable to other microarchs. */
    }
    return reportMsr_GenFunctionEx(uMsr, NULL, uValue, fSkipMask, 0, annotateValue(uValue));
}


/**
 * Deals carefully with a CU_CFG register.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR number.
 * @param   uValue              The current value.
 */
static int reportMsr_AmdFam15hCombUnitCfg(uint32_t uMsr, uint64_t uValue)
{
    /* Skip know bits here, as harm seems to come from messing with them. */
    uint64_t fSkipMask = RT_BIT_64(23) /* L2WayLock  */
                       | RT_BIT_64(22) /* L2FirstLockWay */
                       | RT_BIT_64(21) /* L2FirstLockWay */
                       | RT_BIT_64(20) /* L2FirstLockWay */
                       | RT_BIT_64(19) /* L2FirstLockWay */
                       | RT_BIT_64(10) /* DcacheAggressivePriority */;
    fSkipMask |= RT_BIT_64(46) | RT_BIT_64(45); /* Killer field. Seen bit 46 set, 45 clear. Messing with either means reboot/BSOD. */
    return reportMsr_GenFunctionEx(uMsr, NULL, uValue, fSkipMask, 0, annotateValue(uValue));
}


/**
 * Deals carefully with a EX_CFG register.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR number.
 * @param   uValue              The current value.
 */
static int reportMsr_AmdFam15hExecUnitCfg(uint32_t uMsr, uint64_t uValue)
{
    /* Skip know bits here, as harm seems to come from messing with them. */
    uint64_t fSkipMask = RT_BIT_64(54) /* LateSbzResync  */;
    fSkipMask |= RT_BIT_64(35); /* Undocumented killer bit. */
    return reportMsr_GenFunctionEx(uMsr, NULL, uValue, fSkipMask, 0, annotateValue(uValue));
}



static int produceMsrReport(VBCPUREPMSR *paMsrs, uint32_t cMsrs)
{
    vbCpuRepDebug("produceMsrReport\n");
    RTThreadSleep(500);

    for (uint32_t i = 0; i < cMsrs; i++)
    {
        uint32_t    uMsr       = paMsrs[i].uMsr;
        uint32_t    fFlags     = paMsrs[i].fFlags;
        uint64_t    uValue     = paMsrs[i].uValue;
        int         rc;
#if 0
        //if (uMsr < 0x00000000)
        //    continue;
        if (uMsr >= 0x00000277)
        {
            vbCpuRepDebug("produceMsrReport: uMsr=%#x (%s)...\n", uMsr, getMsrNameHandled(uMsr));
            RTThreadSleep(1000);
        }
#endif
        /*
         * Deal with write only regs first to avoid having to avoid them all the time.
         */
        if (fFlags & VBCPUREPMSR_F_WRITE_ONLY)
        {
            if (uMsr == 0x00000079)
                rc = printMsrWriteOnly(uMsr, NULL, NULL);
            else
                rc = reportMsr_Generic(uMsr, fFlags, uValue);
        }
        /*
         * VIA implement MSRs in a interesting way, so we have to select what we
         * want to handle there to avoid making the code below unreadable.
         */
        /** @todo r=klaus check if Shanghai CPUs really are behaving the same */
        else if (isMsrViaShanghaiDummy(uMsr, uValue, fFlags))
            rc = reportMsr_ViaShanghaiDummyRange(&paMsrs[i], cMsrs - i, &i);
        /*
         * This shall be sorted by uMsr as much as possible.
         */
        else if (uMsr == 0x00000000 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON) && g_enmMicroarch >= kCpumMicroarch_AMD_K8_First)
            rc = printMsrAlias(uMsr, 0x00000402, NULL);
        else if (uMsr == 0x00000001 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON) && g_enmMicroarch >= kCpumMicroarch_AMD_K8_First)
            rc = printMsrAlias(uMsr, 0x00000401, NULL); /** @todo not 101% correct on Fam15h and later, 0xc0010015[McstatusWrEn] effect differs. */
        else if (uMsr == 0x0000001b)
            rc = reportMsr_Ia32ApicBase(uMsr, uValue);
        else if (uMsr == 0x00000040 && g_enmMicroarch <= kCpumMicroarch_Intel_P6_M_Dothan)
            rc = reportMsr_GenRangeFunction(&paMsrs[i], cMsrs - i, 8 /*cMax*/, "IntelLastBranchFromToN", &i);
        else if (uMsr == 0x00000040)
            rc = reportMsr_GenRangeFunctionEx(&paMsrs[i], cMsrs - i, 8 /*cMax*/, "IntelLastBranchToN", uMsr, false,
                                              true, getGenericSkipMask(uMsr), &i);
        else if (uMsr == 0x00000060 && g_enmMicroarch >= kCpumMicroarch_Intel_Core_Yonah)
            rc = reportMsr_GenRangeFunctionEx(&paMsrs[i], cMsrs - i, 8 /*cMax*/, "IntelLastBranchFromN", uMsr, false,
                                              true, getGenericSkipMask(uMsr), &i);
        else if (uMsr == 0x000000c1)
            rc = reportMsr_GenRangeFunction(&paMsrs[i], cMsrs - i,
                                            g_enmMicroarch >= kCpumMicroarch_Intel_Core7_First ? 8 : 4 /*cMax*/,
                                            NULL, &i);
        else if (uMsr == 0x00000186 && !g_fIntelNetBurst)
            rc = reportMsr_GenRangeFunction(&paMsrs[i], cMsrs - i, 8 /*cMax*/, "Ia32PerfEvtSelN", &i);
        else if (uMsr == 0x000001a0)
            rc = reportMsr_Ia32MiscEnable(uMsr, uValue);
        else if (uMsr >= 0x000001a6 && uMsr <= 0x000001a7)
            rc = reportMsr_GenRangeFunction(&paMsrs[i], cMsrs - i, 2 /*cMax*/, "IntelI7MsrOffCoreResponseN", &i);
        else if (uMsr == 0x000001db && g_fIntelNetBurst)
            rc = reportMsr_GenRangeFunction(&paMsrs[i], cMsrs - i, 4 /*cMax*/, "IntelLastBranchFromToN", &i);
        else if (uMsr == 0x00000200)
            rc = reportMsr_Ia32MtrrPhysBaseMaskN(&paMsrs[i], cMsrs - i, &i);
        else if (uMsr >= 0x00000250 && uMsr <= 0x00000279)
            rc = reportMsr_Ia32MtrrFixedOrPat(uMsr);
        else if (uMsr >= 0x00000280 && uMsr <= 0x00000295)
            rc = reportMsr_GenRangeFunctionEx(&paMsrs[i], cMsrs - i, 22 /*cMax*/, NULL, 0x00000280, true /*fEarlyEndOk*/, false, 0, &i);
        else if (uMsr == 0x000002ff)
            rc = reportMsr_Ia32MtrrDefType(uMsr);
        else if (uMsr >= 0x00000309 && uMsr <= 0x0000030b && !g_fIntelNetBurst)
            rc = reportMsr_GenRangeFunctionEx(&paMsrs[i], cMsrs - i, 3 /*cMax*/, NULL, 0x00000309, true /*fEarlyEndOk*/, false, 0, &i);
        else if ((uMsr == 0x000003f8 || uMsr == 0x000003fc || uMsr == 0x0000060a) && !g_fIntelNetBurst)
            rc = reportMsr_GenRangeFunctionEx(&paMsrs[i], cMsrs - i, 4, NULL, uMsr - 3, true, false, 0, &i);
        else if ((uMsr == 0x000003f9 || uMsr == 0x000003fd || uMsr == 0x0000060b) && !g_fIntelNetBurst)
            rc = reportMsr_GenRangeFunctionEx(&paMsrs[i], cMsrs - i, 8, NULL, uMsr - 6, true, false, 0, &i);
        else if ((uMsr == 0x000003fa || uMsr == 0x000003fe || uMsr == 0x0000060c) && !g_fIntelNetBurst)
            rc = reportMsr_GenRangeFunctionEx(&paMsrs[i], cMsrs - i, 8, NULL, uMsr - 7, true, false, 0, &i);
        else if (uMsr >= 0x00000400 && uMsr <= 0x00000477)
            rc = reportMsr_Ia32McCtlStatusAddrMiscN(&paMsrs[i], cMsrs - i, &i);
        else if (uMsr == 0x000004c1)
            rc = reportMsr_GenRangeFunction(&paMsrs[i], cMsrs - i, 8, NULL, &i);
        else if (uMsr == 0x00000680 || uMsr == 0x000006c0)
            rc = reportMsr_GenRangeFunctionEx(&paMsrs[i], cMsrs - i, 16, NULL, uMsr, false, false,
                                              g_fIntelNetBurst
                                              ? UINT64_C(0xffffffffffffff00) /* kludge */
                                              : UINT64_C(0xffff800000000000), &i);
        else if (uMsr >= 0x00000800 && uMsr <= 0x000008ff)
            rc = reportMsr_GenX2Apic(&paMsrs[i], cMsrs - i, &i);
        else if (uMsr == 0x00002000 && g_enmVendor == CPUMCPUVENDOR_INTEL)
            rc = reportMsr_GenFunctionEx(uMsr, "IntelP6CrN", 0, X86_CR0_PE | X86_CR0_PG, 0,
                                         annotateIfMissingBits(uValue, X86_CR0_PE | X86_CR0_PE | X86_CR0_ET));
        else if (uMsr == 0x00002002 && g_enmVendor == CPUMCPUVENDOR_INTEL)
            rc = reportMsr_GenFunctionEx(uMsr, "IntelP6CrN", 2, 0, 0, annotateValue(uValue));
        else if (uMsr == 0x00002003 && g_enmVendor == CPUMCPUVENDOR_INTEL)
        {
            uint64_t fCr3Mask = (RT_BIT_64(vbCpuRepGetPhysAddrWidth()) - 1) & (X86_CR3_PAE_PAGE_MASK | X86_CR3_AMD64_PAGE_MASK);
            if (!vbCpuRepSupportsPae())
                fCr3Mask &= X86_CR3_PAGE_MASK | X86_CR3_AMD64_PAGE_MASK;
            rc = reportMsr_GenFunctionEx(uMsr, "IntelP6CrN", 3, fCr3Mask, 0, annotateValue(uValue));
        }
        else if (uMsr == 0x00002004 && g_enmVendor == CPUMCPUVENDOR_INTEL)
            rc = reportMsr_GenFunctionEx(uMsr, "IntelP6CrN", 4,
                                         X86_CR4_PSE | X86_CR4_PAE | X86_CR4_MCE | X86_CR4_SMXE, 0,
                                         annotateValue(uValue));
        else if (uMsr == 0xc0000080)
            rc = reportMsr_Amd64Efer(uMsr, uValue);
        else if (uMsr >= 0xc0000408 && uMsr <= 0xc000040f)
            rc = reportMsr_AmdFam10hMc4MiscN(&paMsrs[i], cMsrs - i, &i);
        else if (uMsr == 0xc0010000 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdK8PerfCtlN(&paMsrs[i], cMsrs - i, &i);
        else if (uMsr == 0xc0010004 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdK8PerfCtrN(&paMsrs[i], cMsrs - i, &i);
        else if (uMsr == 0xc0010010 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdK8SysCfg(uMsr, uValue);
        else if (uMsr == 0xc0010015 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdK8HwCr(uMsr, uValue);
        else if ((uMsr == 0xc0010016 || uMsr == 0xc0010018) && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdK8IorrBaseN(uMsr, uValue);
        else if ((uMsr == 0xc0010017 || uMsr == 0xc0010019) && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdK8IorrMaskN(uMsr, uValue);
        else if ((uMsr == 0xc001001a || uMsr == 0xc001001d) && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdK8TopMemN(uMsr, uValue);
        else if (uMsr == 0xc0010030 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_GenRangeFunction(&paMsrs[i], cMsrs - i, 6, "AmdK8CpuNameN", &i);
        else if (uMsr >= 0xc0010044 && uMsr <= 0xc001004a && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_GenRangeFunctionEx(&paMsrs[i], cMsrs - i, 7, "AmdK8McCtlMaskN", 0xc0010044, true /*fEarlyEndOk*/, false, 0, &i);
        else if (uMsr == 0xc0010050 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_GenRangeFunction(&paMsrs[i], cMsrs - i, 4, "AmdK8SmiOnIoTrapN", &i);
        else if (uMsr == 0xc0010064 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdFam10hPStateN(&paMsrs[i], cMsrs - i, &i);
        else if (uMsr == 0xc0010070 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdFam10hCofVidControl(uMsr, uValue);
        else if ((uMsr == 0xc0010118 || uMsr == 0xc0010119) && getMsrFnName(uMsr, NULL) && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = printMsrFunction(uMsr, NULL, NULL, annotateValue(uValue)); /* RAZ, write key. */
        else if (uMsr == 0xc0010200 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdGenPerfMixedRange(&paMsrs[i], cMsrs - i, 12, &i);
        else if (uMsr == 0xc0010230 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdGenPerfMixedRange(&paMsrs[i], cMsrs - i, 8, &i);
        else if (uMsr == 0xc0010240 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdGenPerfMixedRange(&paMsrs[i], cMsrs - i, 8, &i);
        else if (uMsr == 0xc0011019 && g_enmMicroarch >= kCpumMicroarch_AMD_15h_Piledriver && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_GenRangeFunctionEx(&paMsrs[i], cMsrs - i, 3, "AmdK7DrXAddrMaskN", 0xc0011019 - 1,
                                              false /*fEarlyEndOk*/, false /*fNoIgnMask*/, 0, &i);
        else if (uMsr == 0xc0011021 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_AmdK7InstrCacheCfg(uMsr, uValue);
        else if (uMsr == 0xc0011023 && CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch))
            rc = reportMsr_AmdFam15hCombUnitCfg(uMsr, uValue);
        else if (uMsr == 0xc0011027 && (g_enmVendor == CPUMCPUVENDOR_AMD || g_enmVendor == CPUMCPUVENDOR_HYGON))
            rc = reportMsr_GenRangeFunctionEx(&paMsrs[i], cMsrs - i, 1, "AmdK7DrXAddrMaskN", 0xc0011027,
                                              false /*fEarlyEndOk*/, false /*fNoIgnMask*/, 0, &i);
        else if (uMsr == 0xc001102c && CPUMMICROARCH_IS_AMD_FAM_15H(g_enmMicroarch))
            rc = reportMsr_AmdFam15hExecUnitCfg(uMsr, uValue);
        /* generic handling. */
        else
            rc = reportMsr_Generic(uMsr, fFlags, uValue);

        if (RT_FAILURE(rc))
            return rc;

        /*
         *  A little ugly snooping.
         */
        if (uMsr == 0x000000cd && !(fFlags & VBCPUREPMSR_F_WRITE_ONLY))
            g_uMsrIntelP6FsbFrequency = uValue;
    }

    return VINF_SUCCESS;
}


/**
 * Custom MSR hacking & probing.
 *
 * Called when the '-d' option is given.
 *
 * @returns VBox status code.
 */
static int hackingMsrs(void)
{
#if 0
    vbCpuRepDebug("\nhackingMsrs:\n"); RTStrmFlush(g_pDebugOut); RTThreadSleep(2000);

    uint32_t uMsr = 0xc0000081;
    vbCpuRepDebug("%#x: msrProberModifyNoChange -> %RTbool\n", uMsr, msrProberModifyNoChange(uMsr));
    RTThreadSleep(3000);

    vbCpuRepDebug("%#x: msrProberModifyBit 30 -> %d\n", uMsr, msrProberModifyBit(uMsr, 30));
    RTThreadSleep(3000);

    vbCpuRepDebug("%#x: msrProberModifyZero -> %RTbool\n", uMsr, msrProberModifyZero(uMsr));
    RTThreadSleep(3000);

    for (uint32_t i = 0; i < 63; i++)
    {
        vbCpuRepDebug("%#x: bit=%02u -> %d\n", msrProberModifyBit(uMsr, i));
        RTThreadSleep(500);
    }
#else

    uint32_t uMsr = 0xc0010010;
    uint64_t uValue = 0;
    msrProberRead(uMsr, &uValue);
    reportMsr_AmdK8SysCfg(uMsr, uValue);
#endif
    return VINF_SUCCESS;
}


static int probeMsrs(bool fHacking, const char *pszNameC, const char *pszCpuDesc,
                     char *pszMsrMask, size_t cbMsrMask)
{
    /* Initialize the mask. */
    if (pszMsrMask && cbMsrMask)
        RTStrCopy(pszMsrMask, cbMsrMask, "UINT32_MAX /** @todo */");

    /*
     * Are MSRs supported by the CPU?
     */
    if (   !RTX86IsValidStdRange(ASMCpuId_EAX(0))
        || !(ASMCpuId_EDX(1) & X86_CPUID_FEATURE_EDX_MSR) )
    {
        vbCpuRepDebug("Skipping MSR probing, CPUID indicates there isn't any MSR support.\n");
        return VINF_SUCCESS;
    }
    if (g_fNoMsrs)
    {
        vbCpuRepDebug("Skipping MSR probing (--no-msr).\n");
        return VINF_SUCCESS;
    }

    /*
     * First try the the support library (also checks if we can really read MSRs).
     */
    int rc = VbCpuRepMsrProberInitSupDrv(&g_MsrAcc);
    if (RT_FAILURE(rc))
    {
#ifdef VBCR_HAVE_PLATFORM_MSR_PROBER
        /* Next try a platform-specific interface. */
        rc = VbCpuRepMsrProberInitPlatform(&g_MsrAcc);
#endif
        if (RT_FAILURE(rc))
        {
            vbCpuRepDebug("warning: Unable to initialize any MSR access interface (%Rrc), skipping MSR detection.\n", rc);
            return VINF_SUCCESS;
        }
    }

    uint64_t uValue;
    bool     fGp;
    rc = g_MsrAcc.pfnMsrProberRead(MSR_IA32_TSC, NIL_RTCPUID, &uValue, &fGp);
    if (RT_FAILURE(rc))
    {
        vbCpuRepDebug("warning: MSR probing not supported by the support driver (%Rrc), skipping MSR detection.\n", rc);
        return VINF_SUCCESS;
    }
    vbCpuRepDebug("MSR_IA32_TSC: %#llx fGp=%RTbool\n", uValue, fGp);
    rc = g_MsrAcc.pfnMsrProberRead(0xdeadface, NIL_RTCPUID, &uValue, &fGp);
    vbCpuRepDebug("0xdeadface: %#llx fGp=%RTbool rc=%Rrc\n", uValue, fGp, rc);

    /*
     * Initialize globals we use.
     */
    uint32_t uEax, uEbx, uEcx, uEdx;
    ASMCpuIdExSlow(0, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);
    if (!RTX86IsValidStdRange(uEax))
        return RTMsgErrorRc(VERR_NOT_SUPPORTED, "Invalid std CPUID range: %#x\n", uEax);
    g_enmVendor = CPUMCpuIdDetectX86VendorEx(uEax, uEbx, uEcx, uEdx);

    ASMCpuIdExSlow(1, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);
    g_enmMicroarch = CPUMCpuIdDetermineX86MicroarchEx(g_enmVendor,
                                                      RTX86GetCpuFamily(uEax),
                                                      RTX86GetCpuModel(uEax, g_enmVendor == CPUMCPUVENDOR_INTEL),
                                                      RTX86GetCpuStepping(uEax));
    g_fIntelNetBurst = CPUMMICROARCH_IS_INTEL_NETBURST(g_enmMicroarch);

    /*
     * Do the probing.
     */
    if (fHacking)
        rc = hackingMsrs();
    else
    {
        /* Determine the MSR mask. */
        uint32_t fMsrMask = determineMsrAndMask();
        if (fMsrMask == UINT32_MAX)
            RTStrCopy(pszMsrMask, cbMsrMask, "UINT32_MAX");
        else
            RTStrPrintf(pszMsrMask, cbMsrMask, "UINT32_C(%#x)", fMsrMask);

        /* Detect MSR. */
        VBCPUREPMSR    *paMsrs;
        uint32_t        cMsrs;
        rc = findMsrs(&paMsrs, &cMsrs, fMsrMask);
        if (RT_FAILURE(rc))
            return rc;

        /* Probe the MSRs and spit out the database table. */
        vbCpuRepPrintf("\n"
                       "#ifndef CPUM_DB_STANDALONE\n"
                       "/**\n"
                       " * MSR ranges for %s.\n"
                       " */\n"
                       "static CPUMMSRRANGE const g_aMsrRanges_%s[] =\n{\n",
                       pszCpuDesc,
                       pszNameC);
        rc = produceMsrReport(paMsrs, cMsrs);
        vbCpuRepPrintf("};\n"
                       "#endif /* !CPUM_DB_STANDALONE */\n"
                       "\n"
                       );

        RTMemFree(paMsrs);
        paMsrs = NULL;
    }
    if (g_MsrAcc.pfnTerm)
        g_MsrAcc.pfnTerm();
    RT_ZERO(g_MsrAcc);
    return rc;
}


static int produceCpuIdArray(const char *pszNameC, const char *pszCpuDesc)
{
    /*
     * Collect the data.
     */
    PCPUMCPUIDLEAF  paLeaves;
    uint32_t        cLeaves;
    int rc = CPUMCpuIdCollectLeavesX86(&paLeaves, &cLeaves);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "CPUMR3CollectCpuIdInfo failed: %Rrc\n", rc);

    /*
     * Dump the array.
     */
    vbCpuRepPrintf("\n"
                   "#ifndef CPUM_DB_STANDALONE\n"
                   "/**\n"
                   " * CPUID leaves for %s.\n"
                   " */\n"
                   "static CPUMCPUIDLEAF const g_aCpuIdLeaves_%s[] =\n{\n",
                   pszCpuDesc,
                   pszNameC);
    for (uint32_t i = 0; i < cLeaves; i++)
    {
        vbCpuRepPrintf("    { %#010x, %#010x, ", paLeaves[i].uLeaf, paLeaves[i].uSubLeaf);
        if (paLeaves[i].fSubLeafMask == UINT32_MAX)
            vbCpuRepPrintf("UINT32_MAX, ");
        else
            vbCpuRepPrintf("%#010x, ", paLeaves[i].fSubLeafMask);
        vbCpuRepPrintf("%#010x, %#010x, %#010x, %#010x, ",
                     paLeaves[i].uEax, paLeaves[i].uEbx, paLeaves[i].uEcx, paLeaves[i].uEdx);
        if (paLeaves[i].fFlags == 0)
            vbCpuRepPrintf("0 },\n");
        else
        {
            vbCpuRepPrintf("0");
            uint32_t fFlags = paLeaves[i].fFlags;
            if (paLeaves[i].fFlags & CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES)
            {
                vbCpuRepPrintf(" | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES");
                fFlags &= ~CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES;
            }
            if (paLeaves[i].fFlags & CPUMCPUIDLEAF_F_CONTAINS_APIC_ID)
            {
                vbCpuRepPrintf(" | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID");
                fFlags &= ~CPUMCPUIDLEAF_F_CONTAINS_APIC_ID;
            }
            if (paLeaves[i].fFlags & CPUMCPUIDLEAF_F_CONTAINS_APIC)
            {
                vbCpuRepPrintf(" | CPUMCPUIDLEAF_F_CONTAINS_APIC");
                fFlags &= ~CPUMCPUIDLEAF_F_CONTAINS_APIC;
            }
            if (fFlags)
            {
                RTMemFree(paLeaves);
                return RTMsgErrorRc(rc, "Unknown CPUID flags %#x\n", fFlags);
            }
            vbCpuRepPrintf(" },\n");
        }
    }
    vbCpuRepPrintf("};\n"
                   "#endif /* !CPUM_DB_STANDALONE */\n"
                   "\n");
    RTMemFree(paLeaves);
    return VINF_SUCCESS;
}


static const char *cpuVendorToString(CPUMCPUVENDOR enmCpuVendor)
{
    switch (enmCpuVendor)
    {
        case CPUMCPUVENDOR_INTEL:       return "Intel";
        case CPUMCPUVENDOR_AMD:         return "AMD";
        case CPUMCPUVENDOR_VIA:         return "VIA";
        case CPUMCPUVENDOR_CYRIX:       return "Cyrix";
        case CPUMCPUVENDOR_SHANGHAI:    return "Shanghai";
        case CPUMCPUVENDOR_HYGON:       return "Hygon";
        case CPUMCPUVENDOR_APPLE:       return "Apple";
        case CPUMCPUVENDOR_INVALID:
        case CPUMCPUVENDOR_UNKNOWN:
        case CPUMCPUVENDOR_32BIT_HACK:
            break;
    }
    return "invalid-cpu-vendor";
}


/**
 * Takes a shot a the bus frequency name (last part).
 *
 * @returns Name suffix.
 */
static const char *vbCpuRepGuessScalableBusFrequencyName(void)
{
    if (CPUMMICROARCH_IS_INTEL_CORE7(g_enmMicroarch))
        return g_enmMicroarch >= kCpumMicroarch_Intel_Core7_SandyBridge ? "100MHZ" : "133MHZ";

    if (g_uMsrIntelP6FsbFrequency != UINT64_MAX)
        switch (g_uMsrIntelP6FsbFrequency & 0x7)
        {
            case 5: return "100MHZ";
            case 1: return "133MHZ";
            case 3: return "167MHZ";
            case 2: return "200MHZ";
            case 0: return "267MHZ";
            case 4: return "333MHZ";
            case 6: return "400MHZ";
        }

    return "UNKNOWN";
}


static int produceCpuReport(void)
{
    /*
     * Figure the cpu vendor.
     */
    if (!ASMHasCpuId())
        return RTMsgErrorRc(VERR_NOT_SUPPORTED, "No CPUID support.\n");
    uint32_t uEax, uEbx, uEcx, uEdx;
    ASMCpuIdExSlow(0, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);
    if (!RTX86IsValidStdRange(uEax))
        return RTMsgErrorRc(VERR_NOT_SUPPORTED, "Invalid std CPUID range: %#x\n", uEax);

    CPUMCPUVENDOR enmVendor = CPUMCpuIdDetectX86VendorEx(uEax, uEbx, uEcx, uEdx);
    if (enmVendor == CPUMCPUVENDOR_UNKNOWN)
        return RTMsgErrorRc(VERR_NOT_IMPLEMENTED, "Unknown CPU vendor: %.4s%.4s%.4s\n", &uEbx, &uEdx, &uEcx);
    vbCpuRepDebug("CPU Vendor: %s - %.4s%.4s%.4s\n", CPUMCpuVendorName(enmVendor), &uEbx, &uEdx, &uEcx);

    /*
     * Determine the micro arch.
     */
    ASMCpuIdExSlow(1, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);
    CPUMMICROARCH enmMicroarch = CPUMCpuIdDetermineX86MicroarchEx(enmVendor,
                                                                  RTX86GetCpuFamily(uEax),
                                                                  RTX86GetCpuModel(uEax, enmVendor == CPUMCPUVENDOR_INTEL),
                                                                  RTX86GetCpuStepping(uEax));

    /*
     * Generate a name.
     */
    char  szName[16*3+1];
    char  szNameC[16*3+1];
    char  szNameRaw[16*3+1];
    char *pszName    = szName;
    char *pszCpuDesc = (char *)"";

    ASMCpuIdExSlow(0x80000000, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);
    if (RTX86IsValidExtRange(uEax) && uEax >= UINT32_C(0x80000004))
    {
        /* Get the raw name and strip leading spaces. */
        ASMCpuIdExSlow(0x80000002, 0, 0, 0, &szNameRaw[0 +  0], &szNameRaw[4 +  0], &szNameRaw[8 +  0], &szNameRaw[12 +  0]);
        ASMCpuIdExSlow(0x80000003, 0, 0, 0, &szNameRaw[0 + 16], &szNameRaw[4 + 16], &szNameRaw[8 + 16], &szNameRaw[12 + 16]);
        ASMCpuIdExSlow(0x80000004, 0, 0, 0, &szNameRaw[0 + 32], &szNameRaw[4 + 32], &szNameRaw[8 + 32], &szNameRaw[12 + 32]);
        szNameRaw[48] = '\0';
        pszCpuDesc = RTStrStrip(szNameRaw);
        vbCpuRepDebug("Name2: %s\n", pszCpuDesc);

        /* Reduce the name. */
        pszName = strcpy(szName, pszCpuDesc);

        static const char * const s_apszSuffixes[] =
        {
            "CPU @",
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_apszSuffixes); i++)
        {
            char *pszHit = strstr(pszName, s_apszSuffixes[i]);
            if (pszHit)
                RT_BZERO(pszHit, strlen(pszHit));
        }

        static const char * const s_apszWords[] =
        {
            "(TM)", "(tm)", "(R)", "(r)", "Processor", "CPU", "@",
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_apszWords); i++)
        {
            const char *pszWord = s_apszWords[i];
            size_t      cchWord = strlen(pszWord);
            char       *pszHit;
            while ((pszHit = strstr(pszName, pszWord)) != NULL)
                memset(pszHit, ' ', cchWord);
        }

        RTStrStripR(pszName);
        for (char *psz = pszName; *psz; psz++)
            if (RT_C_IS_BLANK(*psz))
            {
                size_t cchBlanks = 1;
                while (RT_C_IS_BLANK(psz[cchBlanks]))
                    cchBlanks++;
                *psz = ' ';
                if (cchBlanks > 1)
                    memmove(psz + 1, psz + cchBlanks, strlen(psz + cchBlanks) + 1);
            }
        pszName = RTStrStripL(pszName);
        vbCpuRepDebug("Name: %s\n", pszName);

        /* Make it C/C++ acceptable. */
        strcpy(szNameC, pszName);
        unsigned offDst = 0;
        for (unsigned offSrc = 0; ; offSrc++)
        {
            char ch = szNameC[offSrc];
            if (!RT_C_IS_ALNUM(ch) && ch != '_' && ch != '\0')
                ch = '_';
            if (ch == '_' && offDst > 0 && szNameC[offDst - 1] == '_')
                offDst--;
            szNameC[offDst++] = ch;
            if (!ch)
                break;
        }
        while (offDst > 1 && szNameC[offDst - 1] == '_')
            szNameC[--offDst] = '\0';

        vbCpuRepDebug("NameC: %s\n", szNameC);
    }
    else
    {
        ASMCpuIdExSlow(1, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);
        RTStrPrintf(szNameC, sizeof(szNameC), "%s_%u_%u_%u", cpuVendorToString(enmVendor), RTX86GetCpuFamily(uEax),
                    RTX86GetCpuModel(uEax, enmVendor == CPUMCPUVENDOR_INTEL), RTX86GetCpuStepping(uEax));
        pszCpuDesc = pszName = szNameC;
        vbCpuRepDebug("Name/NameC: %s\n", szNameC);
    }

    /*
     * Print a file header, if we're not outputting to stdout (assumption being
     * that stdout is used while hacking the reporter and too much output is
     * unwanted).
     */
    if (g_pReportOut)
    {
        RTTIMESPEC Now;
        char       szNow[64];
        RTTimeSpecToString(RTTimeNow(&Now), szNow, sizeof(szNow));
        char *pchDot = strchr(szNow, '.');
        if (pchDot)
            strcpy(pchDot, "Z");

        vbCpuRepPrintf("/* $" "Id" "$ */\n"
                       "/** @file\n"
                       " * CPU database entry \"%s\".\n"
                       " * Generated at %s by VBoxCpuReport v%sr%s on %s.%s.\n"
                       " */\n"
                       "\n"
                       "/*\n"
                       " * Copyright (C) 2013-" VBOX_C_YEAR " Oracle and/or its affiliates.\n"
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
                       "#ifndef VBOX_CPUDB_%s_h\n"
                       "#define VBOX_CPUDB_%s_h\n"
                       "#ifndef RT_WITHOUT_PRAGMA_ONCE\n"
                       "# pragma once\n"
                       "#endif\n"
                       "\n",
                       pszName,
                       szNow, RTBldCfgVersion(), RTBldCfgRevisionStr(), RTBldCfgTarget(), RTBldCfgTargetArch(),
                       szNameC, szNameC);
    }

    /*
     * Extract CPUID based data.
     */
    int rc = produceCpuIdArray(szNameC, pszCpuDesc);
    if (RT_FAILURE(rc))
        return rc;

    CPUMUNKNOWNCPUID enmUnknownMethod;
    CPUMCPUID        DefUnknown;
    rc = CPUMR3CpuIdDetectUnknownLeafMethod(&enmUnknownMethod, &DefUnknown);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "CPUMR3DetectCpuIdUnknownMethod failed: %Rrc\n", rc);
    vbCpuRepDebug("enmUnknownMethod=%s\n", CPUMR3CpuIdUnknownLeafMethodName(enmUnknownMethod));

    /*
     * Do the MSRs, if we can.
     */
    char szMsrMask[64];
    probeMsrs(false /*fHacking*/, szNameC, pszCpuDesc, szMsrMask, sizeof(szMsrMask));

    /*
     * Emit the CPUMDBENTRY record.
     */
    ASMCpuIdExSlow(1, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);
    vbCpuRepPrintf("\n"
                   "/**\n"
                   " * Database entry for %s.\n"
                   " */\n"
                   "static CPUMDBENTRY const g_Entry_%s = \n"
                   "{\n"
                   "    /*.pszName          = */ \"%s\",\n"
                   "    /*.pszFullName      = */ \"%s\",\n"
                   "    /*.enmVendor        = */ CPUMCPUVENDOR_%s,\n"
                   "    /*.uFamily          = */ %u,\n"
                   "    /*.uModel           = */ %u,\n"
                   "    /*.uStepping        = */ %u,\n"
                   "    /*.enmMicroarch     = */ kCpumMicroarch_%s,\n"
                   "    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_%s,\n"
                   "    /*.fFlags           = */ 0,\n"
                   "    /*.cMaxPhysAddrWidth= */ %u,\n"
                   "    /*.fMxCsrMask       = */ %#010x,\n"
                   "    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_%s),\n"
                   "    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_%s)),\n"
                   "    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_%s,\n"
                   "    /*.DefUnknownCpuId  = */ { %#010x, %#010x, %#010x, %#010x },\n"
                   "    /*.fMsrMask         = */ %s,\n"
                   "    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_%s)),\n"
                   "    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_%s),\n"
                   "};\n"
                   "\n"
                   "#endif /* !VBOX_CPUDB_%s_h */\n"
                   "\n",
                   pszCpuDesc,
                   szNameC,
                   pszName,
                   pszCpuDesc,
                   CPUMCpuVendorName(enmVendor),
                   RTX86GetCpuFamily(uEax),
                   RTX86GetCpuModel(uEax, enmVendor == CPUMCPUVENDOR_INTEL),
                   RTX86GetCpuStepping(uEax),
                   CPUMMicroarchName(enmMicroarch),
                   vbCpuRepGuessScalableBusFrequencyName(),
                   vbCpuRepGetPhysAddrWidth(),
                   CPUMR3DeterminHostMxCsrMask(),
                   szNameC,
                   szNameC,
                   CPUMR3CpuIdUnknownLeafMethodName(enmUnknownMethod),
                   DefUnknown.uEax,
                   DefUnknown.uEbx,
                   DefUnknown.uEcx,
                   DefUnknown.uEdx,
                   szMsrMask,
                   szNameC,
                   szNameC,
                   szNameC
                   );

    return VINF_SUCCESS;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Argument parsing?
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--msrs-only", 'm', RTGETOPT_REQ_NOTHING },
        { "--msrs-dev",  'd', RTGETOPT_REQ_NOTHING },
        { "--no-msrs",   'n', RTGETOPT_REQ_NOTHING },
        { "--output",    'o', RTGETOPT_REQ_STRING  },
        { "--log",       'l', RTGETOPT_REQ_STRING  },
    };
    RTGETOPTSTATE State;
    RTGetOptInit(&State, argc, argv, &s_aOptions[0], RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    enum
    {
        kCpuReportOp_Normal,
        kCpuReportOp_MsrsOnly,
        kCpuReportOp_MsrsHacking
    } enmOp = kCpuReportOp_Normal;
    g_pReportOut = NULL;
    g_pDebugOut  = NULL;
    const char *pszOutput   = NULL;
    const char *pszDebugOut = NULL;

    int iOpt;
    RTGETOPTUNION ValueUnion;
    while ((iOpt = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (iOpt)
        {
            case 'm':
                enmOp = kCpuReportOp_MsrsOnly;
                break;

            case 'd':
                enmOp = kCpuReportOp_MsrsHacking;
                break;

            case 'n':
                g_fNoMsrs = true;
                break;

            case 'o':
                pszOutput = ValueUnion.psz;
                break;

            case 'l':
                pszDebugOut = ValueUnion.psz;
                break;

            case 'h':
                RTPrintf("Usage: VBoxCpuReport [-m|--msrs-only] [-d|--msrs-dev] [-n|--no-msrs] [-h|--help] [-V|--version] [-o filename.h] [-l debug.log]\n");
                RTPrintf("Internal tool for gathering information to the VMM CPU database.\n");
                return RTEXITCODE_SUCCESS;
            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return RTEXITCODE_SUCCESS;
            default:
                return RTGetOptPrintError(iOpt, &ValueUnion);
        }
    }

    /*
     * Open the alternative debug log stream.
     */
    if (pszDebugOut)
    {
        if (RTFileExists(pszDebugOut) && !RTSymlinkExists(pszDebugOut))
        {
            char szOld[RTPATH_MAX];
            rc = RTStrCopy(szOld, sizeof(szOld), pszDebugOut);
            if (RT_SUCCESS(rc))
                rc = RTStrCat(szOld, sizeof(szOld), ".old");
            if (RT_SUCCESS(rc))
                RTFileRename(pszDebugOut, szOld, RTFILEMOVE_FLAGS_REPLACE);
        }
        rc = RTStrmOpen(pszDebugOut, "w", &g_pDebugOut);
        if (RT_FAILURE(rc))
        {
            RTMsgError("Error opening '%s': %Rrc", pszDebugOut, rc);
            g_pDebugOut = NULL;
        }
    }

    /*
     * Do the requested job.
     */
    rc = VERR_INTERNAL_ERROR;
    switch (enmOp)
    {
        case kCpuReportOp_Normal:
            /* switch output file. */
            if (pszOutput)
            {
                if (RTFileExists(pszOutput) && !RTSymlinkExists(pszOutput))
                {
                    char szOld[RTPATH_MAX];
                    rc = RTStrCopy(szOld, sizeof(szOld), pszOutput);
                    if (RT_SUCCESS(rc))
                        rc = RTStrCat(szOld, sizeof(szOld), ".old");
                    if (RT_SUCCESS(rc))
                        RTFileRename(pszOutput, szOld, RTFILEMOVE_FLAGS_REPLACE);
                }
                rc = RTStrmOpen(pszOutput, "w", &g_pReportOut);
                if (RT_FAILURE(rc))
                {
                    RTMsgError("Error opening '%s': %Rrc", pszOutput, rc);
                    break;
                }
            }
            rc = produceCpuReport();
            break;
        case kCpuReportOp_MsrsOnly:
        case kCpuReportOp_MsrsHacking:
            rc = probeMsrs(enmOp == kCpuReportOp_MsrsHacking, NULL, NULL, NULL, 0);
            break;
    }

    /*
     * Close the output files.
     */
    if (g_pReportOut)
    {
        RTStrmClose(g_pReportOut);
        g_pReportOut = NULL;
    }

    if (g_pDebugOut)
    {
        RTStrmClose(g_pDebugOut);
        g_pDebugOut = NULL;
    }

    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

