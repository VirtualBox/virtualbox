/* $Id: TestBoxHelper.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Validation Kit - Testbox C Helper Utility.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <iprt/buildconfig.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/mp.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/system.h>

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/x86.h>
# include <iprt/asm-amd64-x86.h>
#elif defined(RT_ARCH_ARM) || defined(RT_ARCH_ARM64)
# include <iprt/asm-arm.h>
#endif

#ifdef RT_OS_DARWIN
# include <sys/types.h>
# include <sys/sysctl.h>
#elif defined(RT_OS_WINDOWS)
# include <iprt/nt/nt-and-windows.h>
# include <iprt/ldr.h>

extern "C" HRESULT WINAPI
WHvGetCapability(UINT32 CapabilityCode, VOID *CapabilityBuffer, UINT32 CapabilityBufferSizeInBytes, UINT32 *WrittenSizeInBytes);
#elif defined(RT_OS_LINUX)
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
#endif



/**
 * Does one free space wipe, using the given filename.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE on failure (fully
 *          bitched).
 * @param   pszFilename     The filename to use for wiping free space.  Will be
 *                          replaced and afterwards deleted.
 * @param   pvFiller        The filler block buffer.
 * @param   cbFiller        The size of the filler block buffer.
 * @param   cbMinLeftOpt    When to stop wiping.
 */
static RTEXITCODE doOneFreeSpaceWipe(const char *pszFilename, void const *pvFiller, size_t cbFiller, uint64_t cbMinLeftOpt)
{
    /*
     * Open the file.
     */
    RTEXITCODE  rcExit = RTEXITCODE_SUCCESS;
    RTFILE      hFile  = NIL_RTFILE;
    int rc = RTFileOpen(&hFile, pszFilename,
                        RTFILE_O_WRITE | RTFILE_O_DENY_NONE | RTFILE_O_CREATE_REPLACE | (0775 << RTFILE_O_CREATE_MODE_SHIFT));
    if (RT_SUCCESS(rc))
    {
        /*
         * Query the amount of available free space.  Figure out which API we should use.
         */
        RTFOFF cbTotal = 0;
        RTFOFF cbFree = 0;
        rc = RTFileQueryFsSizes(hFile, &cbTotal, &cbFree, NULL, NULL);
        bool const fFileHandleApiSupported = rc != VERR_NOT_SUPPORTED && rc != VERR_NOT_IMPLEMENTED;
        if (!fFileHandleApiSupported)
            rc = RTFsQuerySizes(pszFilename, &cbTotal, &cbFree, NULL, NULL);
        if (RT_SUCCESS(rc))
        {
            RTPrintf("%s: %'9RTfoff MiB out of %'9RTfoff are free\n", pszFilename, cbFree / _1M, cbTotal / _1M);

            /*
             * Start filling up the free space, down to the last 32MB.
             */
            uint64_t const  nsStart       = RTTimeNanoTS();     /* for speed calcs */
            uint64_t        nsStat        = nsStart;            /* for speed calcs */
            uint64_t        cbStatWritten = 0;                  /* for speed calcs */
            RTFOFF const    cbMinLeft     = RT_MAX(cbMinLeftOpt, cbFiller * 2);
            RTFOFF          cbLeftToWrite = cbFree - cbMinLeft;
            uint64_t        cbWritten     = 0;
            uint32_t        iLoop         = 0;
            while (cbLeftToWrite >= (RTFOFF)cbFiller)
            {
                rc = RTFileWrite(hFile, pvFiller, cbFiller, NULL);
                if (RT_FAILURE(rc))
                {
                    if (rc == VERR_DISK_FULL)
                        RTPrintf("%s: Disk full after writing %'9RU64 MiB\n", pszFilename, cbWritten / _1M);
                    else
                        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Write error after %'RU64 bytes: %Rrc\n",
                                                pszFilename, cbWritten, rc);
                    break;
                }

                /* Flush every now and then as we approach a completely full disk. */
                if (cbLeftToWrite <= _1G && (iLoop & (cbLeftToWrite  > _128M ? 15 : 3)) == 0)
                    RTFileFlush(hFile);

                /*
                 * Advance and maybe recheck the amount of free space.
                 */
                cbWritten     += cbFiller;
                cbLeftToWrite -= (ssize_t)cbFiller;
                iLoop++;
                if ((iLoop & (16 - 1)) == 0 || cbLeftToWrite < _256M)
                {
                    RTFOFF cbFreeUpdated;
                    if (fFileHandleApiSupported)
                        rc = RTFileQueryFsSizes(hFile, NULL, &cbFreeUpdated, NULL, NULL);
                    else
                        rc = RTFsQuerySizes(pszFilename, NULL, &cbFreeUpdated, NULL, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        cbFree = cbFreeUpdated;
                        cbLeftToWrite = cbFree - cbMinLeft;
                    }
                    else
                    {
                        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Failed to query free space after %'RU64 bytes: %Rrc\n",
                                                pszFilename, cbWritten, rc);
                        break;
                    }
                    if ((iLoop & (512 - 1)) == 0)
                    {
                        uint64_t const nsNow = RTTimeNanoTS();
                        uint64_t cNsInterval = nsNow - nsStat;
                        uint64_t cbInterval  = cbWritten - cbStatWritten;
                        uint64_t cbIntervalPerSec = !cbInterval ? 0
                                                  : (uint64_t)((double)cbInterval / ((double)cNsInterval / (double)RT_NS_1SEC));

                        RTPrintf("%s: %'9RTfoff MiB out of %'9RTfoff are free after writing %'9RU64 MiB (%'5RU64 MiB/s)\n",
                                 pszFilename, cbFree / _1M, cbTotal  / _1M, cbWritten  / _1M, cbIntervalPerSec / _1M);
                        nsStat        = nsNow;
                        cbStatWritten = cbWritten;
                    }
                }
            }

            /*
             * Now flush the file and then reduce the size a little before closing
             * it so the system won't entirely run out of space.  The flush should
             * ensure the data has actually hit the disk.
             */
            rc = RTFileFlush(hFile);
            if (RT_FAILURE(rc))
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Flush failed at %'RU64 bytes: %Rrc\n", pszFilename, cbWritten, rc);

            uint64_t cbReduced = cbWritten > _512M ? cbWritten - _512M : cbWritten / 2;
            rc = RTFileSetSize(hFile, cbReduced);
            if (RT_FAILURE(rc))
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Failed to reduce file size from %'RU64 to %'RU64 bytes: %Rrc\n",
                                        pszFilename, cbWritten, cbReduced, rc);

            /* Issue a summary statements. */
            uint64_t cNsElapsed = RTTimeNanoTS() - nsStart;
            uint64_t cbPerSec   = cbWritten ? (uint64_t)((double)cbWritten / ((double)cNsElapsed / (double)RT_NS_1SEC)) : 0;
            RTPrintf("%s: Wrote %'RU64 MiB in %'RU64 s, avg %'RU64 MiB/s.\n",
                     pszFilename, cbWritten / _1M, cNsElapsed / RT_NS_1SEC, cbPerSec / _1M);
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Initial free space query failed: %Rrc \n", pszFilename, rc);

        RTFileClose(hFile);

        /*
         * Delete the file.
         */
        rc = RTFileDelete(pszFilename);
        if (RT_FAILURE(rc))
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Delete failed: %Rrc !!\n", pszFilename, rc);
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Open failed: %Rrc\n", pszFilename, rc);
    return rcExit;
}


/**
 * Wipes free space on one or more volumes by creating large files.
 */
static RTEXITCODE handlerWipeFreeSpace(int argc, char **argv)
{
    /*
     * Parse arguments.
     */
    const char *apszDefFiles[2] = { "./wipefree.spc", NULL };
    bool        fAll            = false;
    uint32_t    u32Filler       = UINT32_C(0xf6f6f6f6);
    uint64_t    cbMinLeftOpt    = _32M;

    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--all",      'a', RTGETOPT_REQ_NOTHING },
        { "--filler",   'f', RTGETOPT_REQ_UINT32 },
        { "--min-free", 'm', RTGETOPT_REQ_UINT64 },
    };
    RTGETOPTSTATE State;
    RTGetOptInit(&State, argc, argv, &s_aOptions[0], RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    RTGETOPTUNION ValueUnion;
    int chOpt;
    while (  (chOpt = RTGetOpt(&State, &ValueUnion)) != 0
           && chOpt != VINF_GETOPT_NOT_OPTION)
    {
        switch (chOpt)
        {
            case 'a':
                fAll = true;
                break;
            case 'f':
                u32Filler = ValueUnion.u32;
                break;
            case 'm':
                cbMinLeftOpt = ValueUnion.u64;
                break;
            case 'h':
                RTPrintf("usage: wipefrespace [options] [filename1 [..]]\n"
                         "\n"
                         "Options:\n"
                         "  -a, --all\n"
                         "    Try do the free space wiping on all seemingly relevant file systems.\n"
                         "    Changes the meaning of the filenames  "
                         "    This is not yet implemented\n"
                         "  -p, --filler <32-bit value>\n"
                         "    What to fill the blocks we write with.\n"
                         "    Default: 0xf6f6f6f6\n"
                         "  -m, --min-free <64-bit byte count>\n"
                         "    Specifies when to stop in terms of free disk space (in bytes).\n"
                         "    Default: 32MB\n"
                         "\n"
                         "Zero or more names of files to do the free space wiping thru can be given.\n"
                         "When --all is NOT used, each of the files are used to do free space wiping on\n"
                         "the volume they will live on.  However, when --all is in effect the files are\n"
                         "appended to the volume mountpoints and only the first that can be created will\n"
                         "be used.  Files (used ones) will be removed when done.\n"
                         "\n"
                         "If no filename is given, the default is: %s\n"
                         , apszDefFiles[0]);
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }

    char **papszFiles;
    if (chOpt == 0)
        papszFiles = (char **)apszDefFiles;
    else
        papszFiles = RTGetOptNonOptionArrayPtr(&State);

    /*
     * Allocate and prep a memory which we'll write over and over again.
     */
    uint32_t  cbFiller   = _2M;
    uint32_t *pu32Filler = (uint32_t *)RTMemPageAlloc(cbFiller);
    while (!pu32Filler)
    {
        cbFiller <<= 1;
        if (cbFiller >= _4K)
            pu32Filler = (uint32_t *)RTMemPageAlloc(cbFiller);
        else
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTMemPageAlloc failed for sizes between 4KB and 2MB!\n");
    }
    for (uint32_t i = 0; i < cbFiller / sizeof(pu32Filler[0]); i++)
        pu32Filler[i] = u32Filler;

    /*
     * Do the requested work.
     */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    if (!fAll)
    {
        for (uint32_t iFile = 0; papszFiles[iFile] != NULL; iFile++)
        {
            RTEXITCODE rcExit2 = doOneFreeSpaceWipe(papszFiles[iFile], pu32Filler, cbFiller, cbMinLeftOpt);
            if (rcExit2 != RTEXITCODE_SUCCESS && rcExit == RTEXITCODE_SUCCESS)
                rcExit = rcExit2;
        }
    }
    else
    {
        /*
         * Reject --all for now.
         */
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE,  "The --all option is not yet implemented!\n");
    }

    RTMemPageFree(pu32Filler, cbFiller);
    return rcExit;
}


/**
 * Generates a kind of report of the hardware, software and whatever else we
 * think might be useful to know about the testbox.
 */
static RTEXITCODE handlerReport(int argc, char **argv)
{
    NOREF(argc); NOREF(argv);

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    /*
     * For now, a simple CPUID dump.  Need to figure out how to share code
     * like this with other bits, putting it in IPRT.
     */
    RTPrintf("CPUID Dump\n"
             "Leaf      eax      ebx      ecx      edx\n"
             "---------------------------------------------\n");
    static uint32_t const s_auRanges[] =
    {
        UINT32_C(0x00000000),
        UINT32_C(0x80000000),
        UINT32_C(0x80860000),
        UINT32_C(0xc0000000),
        UINT32_C(0x40000000),
    };
    for (uint32_t iRange = 0; iRange < RT_ELEMENTS(s_auRanges); iRange++)
    {
        uint32_t const uFirst = s_auRanges[iRange];

        uint32_t uEax, uEbx, uEcx, uEdx;
        ASMCpuIdExSlow(uFirst, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);
        if (uEax >= uFirst && uEax < uFirst + 100)
        {
            uint32_t const cLeafs = RT_MIN(uEax - uFirst + 1, 32);
            for (uint32_t iLeaf = 0; iLeaf < cLeafs; iLeaf++)
            {
                uint32_t uLeaf = uFirst + iLeaf;
                ASMCpuIdExSlow(uLeaf, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);

                /* Clear APIC IDs to avoid submitting new reports all the time. */
                if (uLeaf == 1)
                    uEbx &= UINT32_C(0x00ffffff);
                if (uLeaf == 0xb)
                    uEdx  = 0;
                if (uLeaf == 0x8000001e)
                    uEax  = 0;

                /* Clear some other node/cpu/core/thread ids. */
                if (uLeaf == 0x8000001e)
                {
                    uEbx &= UINT32_C(0xffffff00);
                    uEcx &= UINT32_C(0xffffff00);
                }

                RTPrintf("%08x: %08x %08x %08x %08x\n", uLeaf, uEax, uEbx, uEcx, uEdx);
            }
        }
    }
    RTPrintf("\n");
#else
#endif

    /*
     * DMI info.
     */
    RTPrintf("DMI Info\n"
             "--------\n");
    static const struct { const char *pszName; RTSYSDMISTR enmDmiStr; } s_aDmiStrings[] =
    {
        { "Product Name",           RTSYSDMISTR_PRODUCT_NAME },
        { "Product version",        RTSYSDMISTR_PRODUCT_VERSION },
        { "Product UUID",           RTSYSDMISTR_PRODUCT_UUID },
        { "Product Serial",         RTSYSDMISTR_PRODUCT_SERIAL },
        { "System Manufacturer",    RTSYSDMISTR_MANUFACTURER },
    };
    for (uint32_t iDmiString = 0; iDmiString < RT_ELEMENTS(s_aDmiStrings); iDmiString++)
    {
        char szTmp[4096];
        RT_ZERO(szTmp);
        int rc = RTSystemQueryDmiString(s_aDmiStrings[iDmiString].enmDmiStr, szTmp, sizeof(szTmp) - 1);
        if (RT_SUCCESS(rc))
            RTPrintf("%25s: %s\n", s_aDmiStrings[iDmiString].pszName, RTStrStrip(szTmp));
        else
            RTPrintf("%25s: %s [rc=%Rrc]\n", s_aDmiStrings[iDmiString].pszName, RTStrStrip(szTmp), rc);
    }
    RTPrintf("\n");

    /*
     * Dump the environment.
     */
    RTPrintf("Environment\n"
             "-----------\n");
    RTENV hEnv;
    int rc = RTEnvClone(&hEnv, RTENV_DEFAULT);
    if (RT_SUCCESS(rc))
    {
        uint32_t cVars = RTEnvCountEx(hEnv);
        for (uint32_t iVar = 0; iVar < cVars; iVar++)
        {
            char szVar[1024];
            char szValue[16384];
            rc = RTEnvGetByIndexEx(hEnv, iVar, szVar, sizeof(szVar), szValue, sizeof(szValue));

            /* zap the value of variables that are subject to change. */
            if (   (RT_SUCCESS(rc) || rc == VERR_BUFFER_OVERFLOW)
                && (   !strcmp(szVar, "TESTBOX_SCRIPT_REV")
                    || !strcmp(szVar, "TESTBOX_ID")
                    || !strcmp(szVar, "TESTBOX_SCRATCH_SIZE")
                    || !strcmp(szVar, "TESTBOX_TIMEOUT")
                    || !strcmp(szVar, "TESTBOX_TIMEOUT_ABS")
                    || !strcmp(szVar, "TESTBOX_TEST_SET_ID")
                   )
               )
               strcpy(szValue, "<volatile>");

            if (RT_SUCCESS(rc))
                RTPrintf("%25s=%s\n", szVar, szValue);
            else if (rc == VERR_BUFFER_OVERFLOW)
                RTPrintf("%25s=%s [VERR_BUFFER_OVERFLOW]\n", szVar, szValue);
            else
                RTPrintf("rc=%Rrc\n", rc);
        }
        RTEnvDestroy(hEnv);
    }

    /** @todo enumerate volumes and whatnot.  */

    int cch = RTPrintf("\n");
    return cch > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/** Print the total memory size in bytes. */
static RTEXITCODE handlerMemSize(int argc, char **argv)
{
    NOREF(argc); NOREF(argv);

    uint64_t cb;
    int rc = RTSystemQueryTotalRam(&cb);
    if (RT_SUCCESS(rc))
    {
        int cch = RTPrintf("%llu\n", cb);
        return cch > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
    }
    RTPrintf("%Rrc\n", rc);
    return RTEXITCODE_FAILURE;
}


static bool isNativeApiSupported(void)
{
    bool fSupported = false;

#if defined(RT_OS_DARWIN)
    /*
     * The kern.hv_support parameter indicates support for the hypervisor API
     * in the kernel.
     */
    int32_t fHvSupport = 0;
    size_t  cbOld = sizeof(fHvSupport);
    if (sysctlbyname("kern.hv_support", &fHvSupport, &cbOld, NULL, 0) == 0)
    {
        if (fHvSupport != 0)
            fSupported = true;
    }

#elif defined(RT_OS_WINDOWS)
    /*
     * Check whether we can load WinHvPlatform.dll and whether the Hypervisor
     * capability is present.
     */
    RTLDRMOD hLdrMod;
    int rc = RTLdrLoadSystem("WinHvPlatform.dll", false /*fNoUnload*/, &hLdrMod);
    if (RT_SUCCESS(rc))
    {
        decltype(WHvGetCapability) *pfnWHvGetCapability;

        rc = RTLdrGetSymbol(hLdrMod, "WHvGetCapability", (void **)&pfnWHvGetCapability);
        if (RT_SUCCESS(rc))
        {
            BOOL fHypervisorPresent = FALSE;
            SetLastError(0);
            HRESULT hrc = pfnWHvGetCapability(0 /*WHvCapabilityCodeHypervisorPresent*/,
                                              &fHypervisorPresent,
                                              sizeof(fHypervisorPresent),
                                              NULL);
            if (   SUCCEEDED(hrc)
                && fHypervisorPresent)
                fSupported = true;
        }

        RTLdrClose(hLdrMod);
    }

#elif defined(RT_OS_LINUX)
    /* Check by opening /dev/kvm. */
    int fdKvm = open("/dev/kvm", O_RDWR);
    if (fdKvm >= 0)
    {
        close(fdKvm);
        fSupported = true;
    }
#endif

    return fSupported;
}

/** Print the 'true' if native API virtualization is supported, 'false' if not and
 * 'dunno' if we cannot tell. */
static RTEXITCODE handlerNativeApi(int argc, char **argv)
{
    NOREF(argc); NOREF(argv);

    int cch = RTPrintf(isNativeApiSupported() ? "true\n" : "false\n");
    return cch > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


typedef enum { HWVIRTTYPE_NONE, HWVIRTTYPE_VTX, HWVIRTTYPE_AMDV, HVIRTTYPE_ARMV8 } HWVIRTTYPE;
static HWVIRTTYPE getHwVirtSupport(void)
{
    /* No native virtualization supported on macOS anymore (for the VBox versions we care about). */
#if !defined RT_OS_DARWIN
# if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    uint32_t uEax, uEbx, uEcx, uEdx;

    /* VT-x */
    ASMCpuId(0x00000000, &uEax, &uEbx, &uEcx, &uEdx);
    if (RTX86IsValidStdRange(uEax))
    {
        ASMCpuId(0x00000001, &uEax, &uEbx, &uEcx, &uEdx);
        if (uEcx & X86_CPUID_FEATURE_ECX_VMX)
            return HWVIRTTYPE_VTX;
    }

    /* AMD-V */
    ASMCpuId(0x80000000, &uEax, &uEbx, &uEcx, &uEdx);
    if (RTX86IsValidExtRange(uEax))
    {
        ASMCpuId(0x80000001, &uEax, &uEbx, &uEcx, &uEdx);
        if (uEcx & X86_CPUID_AMD_FEATURE_ECX_SVM)
            return HWVIRTTYPE_AMDV;
    }
# endif
#endif

    return HWVIRTTYPE_NONE;
}

/** Print the 'true' if VT-x or AMD-v is supported, 'false' it not. */
static RTEXITCODE handlerCpuHwVirt(int argc, char **argv)
{
    NOREF(argc); NOREF(argv);
    int cch = RTPrintf(getHwVirtSupport() != HWVIRTTYPE_NONE ? "true\n" : "false\n");
    return cch > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/** Print the 'true' if nested paging is supported, 'false' if not and
 * 'dunno' if we cannot tell. */
static RTEXITCODE handlerCpuNestedPaging(int argc, char **argv)
{
    NOREF(argc); NOREF(argv);
    int               fSupported = -1;

    HWVIRTTYPE  const enmHwVirt  = getHwVirtSupport();
    if (enmHwVirt == HWVIRTTYPE_NONE)
        fSupported = 0;
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    else if (enmHwVirt == HWVIRTTYPE_AMDV)
    {
        uint32_t uEax, uEbx, uEcx, uEdx;
        ASMCpuId(0x80000000, &uEax, &uEbx, &uEcx, &uEdx);
        if (RTX86IsValidExtRange(uEax) && uEax >= 0x8000000a)
        {
            ASMCpuId(0x8000000a, &uEax, &uEbx, &uEcx, &uEdx);
            if (uEdx & RT_BIT(0) /* AMD_CPUID_SVM_FEATURE_EDX_NESTED_PAGING */)
                fSupported = 1;
            else
                fSupported = 0;
        }
    }
# if defined(RT_OS_LINUX)
    else if (enmHwVirt == HWVIRTTYPE_VTX)
    {
        /*
         * For Intel there is no generic way to query EPT support but on
         * Linux we can resort to checking for the EPT flag in /proc/cpuinfo
         */
        RTFILE hFileCpu;
        int rc = RTFileOpen(&hFileCpu, "/proc/cpuinfo", RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE);
        if (RT_SUCCESS(rc))
        {
            /*
             * Read enough to fit the first CPU entry in, we only check the first
             * CPU as all the others should have the same features.
             */
            char szBuf[_4K];
            size_t cbRead = 0;

            RT_ZERO(szBuf); /* Ensure proper termination. */
            rc = RTFileRead(hFileCpu, &szBuf[0], sizeof(szBuf) - 1, &cbRead);
            if (RT_SUCCESS(rc))
            {
                /* Look for the start of the flags section. */
                char *pszStrFlags = RTStrStr(&szBuf[0], "flags");
                if (pszStrFlags)
                {
                    /* Look for the end as indicated by new line. */
                    char *pszEnd = pszStrFlags;
                    while (   *pszEnd != '\0'
                           && *pszEnd != '\n')
                        pszEnd++;
                    *pszEnd = '\0'; /* Cut off everything after the flags section. */

                    /*
                     * Search for the ept flag indicating support and the absence meaning
                     * not supported.
                     */
                    if (RTStrStr(pszStrFlags, "ept"))
                        fSupported = 1;
                    else
                        fSupported = 0;
                }
            }
            RTFileClose(hFileCpu);
        }
    }
# endif
#endif

    int cch = RTPrintf(fSupported == 1 ? "true\n" : fSupported == 0 ? "false\n" : "dunno\n");
    return cch > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/** Print the 'true' if 64-bits guests are supported, 'false' if not and
 * 'dunno' if we cannot tell. */
static RTEXITCODE handlerCpu64BitGuest(int argc, char **argv)
{
    NOREF(argc); NOREF(argv);
    int              fSupported    = 0;

    HWVIRTTYPE const enmHwVirt = getHwVirtSupport();
    if (   enmHwVirt != HWVIRTTYPE_NONE
        || isNativeApiSupported())
    {
#if ARCH_BITS == 64
        fSupported = 1; /* We're running in 64-bit mode, so it must be supported. */

#elif defined(RT_ARCH_X86)
# ifdef RT_OS_DARWIN
        /* On darwin, we just ask the kernel via sysctl. Rules are a bit different here. */
        int     f64bitCapable = 0;
        size_t  cbParameter   = sizeof(f64bitCapable);
        int rc = sysctlbyname("hw.cpu64bit_capable", &f64bitCapable, &cbParameter, NULL, 0);
        if (rc != -1)
            fSupported = f64bitCapable != 0;
        else
# endif
        {
            /* PAE and HwVirt are required */
            uint32_t uEax, uEbx, uEcx, uEdx;
            ASMCpuId(0x00000000, &uEax, &uEbx, &uEcx, &uEdx);
            if (RTX86IsValidStdRange(uEax))
            {
                ASMCpuId(0x00000001, &uEax, &uEbx, &uEcx, &uEdx);
                if (uEdx & X86_CPUID_FEATURE_EDX_PAE)
                {
                    /* AMD will usually advertise long mode in 32-bit mode. Intel OTOH,
                       won't necessarily do so. */
                    ASMCpuId(0x80000000, &uEax, &uEbx, &uEcx, &uEdx);
                    if (RTX86IsValidExtRange(uEax))
                    {
                        ASMCpuId(0x80000001, &uEax, &uEbx, &uEcx, &uEdx);
                        if (uEdx & X86_CPUID_EXT_FEATURE_EDX_LONG_MODE)
                            fSupported = 1;
                        else if (enmHwVirt != HWVIRTTYPE_AMDV)
                            fSupported = -1;
                    }
                }
            }
        }
#endif
    }

    int cch = RTPrintf(fSupported == 1 ? "true\n" : fSupported == 0 ? "false\n" : "dunno\n");
    return cch > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/** Print the CPU 'revision', if available. */
static RTEXITCODE handlerCpuRevision(int argc, char **argv)
{
    NOREF(argc); NOREF(argv);

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    uint32_t uEax, uEbx, uEcx, uEdx;
    ASMCpuId(0, &uEax, &uEbx, &uEcx, &uEdx);
    if (RTX86IsValidStdRange(uEax) && uEax >= 1)
    {
        uint32_t uEax1 = ASMCpuId_EAX(1);
        uint32_t uVersion = (RTX86GetCpuFamily(uEax1) << 24)
                          | (RTX86GetCpuModel(uEax1, RTX86IsIntelCpu(uEbx, uEcx, uEdx)) << 8)
                          | RTX86GetCpuStepping(uEax1);
        int cch = RTPrintf("%#x\n", uVersion);
        return cch > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
    }
#elif defined(RT_ARCH_ARM) || defined(RT_ARCH_ARM64)
    /** @todo There is no way to access MIDR_EL1 from userspace except for parsing the various
     * OS dependent ways (/proc/cpuinfo, sysctl, ...). Just fake it for now to get it running. */
    int cch = RTPrintf("%#x\n", 1);
    return cch > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
#endif
    return RTEXITCODE_FAILURE;
}


/** Print the CPU name, if available. */
static RTEXITCODE handlerCpuName(int argc, char **argv)
{
    NOREF(argc); NOREF(argv);

    char szTmp[1024];
    int rc = RTMpGetDescription(NIL_RTCPUID, szTmp, sizeof(szTmp));
    if (RT_SUCCESS(rc))
    {
        int cch = RTPrintf("%s\n", RTStrStrip(szTmp));
        return cch > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
    }
    return RTEXITCODE_FAILURE;
}


/** Print the CPU vendor name, 'GenuineIntel' and such. */
static RTEXITCODE handlerCpuVendor(int argc, char **argv)
{
    NOREF(argc); NOREF(argv);

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    uint32_t uEax, uEbx, uEcx, uEdx;
    ASMCpuId(0, &uEax, &uEbx, &uEcx, &uEdx);
    int cch = RTPrintf("%.04s%.04s%.04s\n", &uEbx, &uEdx, &uEcx);
#elif defined(RT_ARCH_ARM64) && defined(RT_OS_DARWIN)
    /*
     * There is machdep.cpu.brand_string we could query but that identifies
     * the whole CPU and not just the vendor.
     *
     * Running on macOS using the arm64 architecture is a pretty safe bet that
     * we are also running on Apple Silicon (there is the possibility that
     * this runs in a macOS VM on some other hardware but this is highly unlikely).
     */
    int cch = RTPrintf("Apple\n");
#else
    int cch = RTPrintf("%s\n", RTBldCfgTargetArch());
#endif
    return cch > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}



int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * The first argument is a command.  Figure out which and call its handler.
     */
    static const struct
    {
        const char     *pszCommand;
        RTEXITCODE    (*pfnHandler)(int argc, char **argv);
        bool            fNoArgs;
    } s_aHandlers[] =
    {
        { "cpuvendor",      handlerCpuVendor,       true },
        { "cpuname",        handlerCpuName,         true },
        { "cpurevision",    handlerCpuRevision,     true },
        { "cpuhwvirt",      handlerCpuHwVirt,       true },
        { "nestedpaging",   handlerCpuNestedPaging, true },
        { "64bitguest",     handlerCpu64BitGuest,   true },
        { "longmode",       handlerCpu64BitGuest,   true }, /* legacy name */
        { "nativeapi",      handlerNativeApi,       true },
        { "memsize",        handlerMemSize,         true },
        { "report",         handlerReport,          true },
        { "wipefreespace",  handlerWipeFreeSpace,   false }
    };

    if (argc < 2)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "expected command as the first argument");

    for (unsigned i = 0; i < RT_ELEMENTS(s_aHandlers); i++)
    {
        if (!strcmp(argv[1], s_aHandlers[i].pszCommand))
        {
            if (   s_aHandlers[i].fNoArgs
                && argc != 2)
                return RTMsgErrorExit(RTEXITCODE_SYNTAX, "the command '%s' does not take any arguments", argv[1]);
            return s_aHandlers[i].pfnHandler(argc - 1, argv + 1);
        }
    }

    /*
     * Help or version query?
     */
    for (int i = 1; i < argc; i++)
        if (   !strcmp(argv[i], "--help")
            || !strcmp(argv[i], "-h")
            || !strcmp(argv[i], "-?")
            || !strcmp(argv[i], "help") )
        {
            RTPrintf("usage: %s <cmd> [cmd specific args]\n"
                     "\n"
                     "commands:\n", argv[0]);
            for (unsigned j = 0; j < RT_ELEMENTS(s_aHandlers); j++)
                RTPrintf("    %s\n", s_aHandlers[j].pszCommand);
            return RTEXITCODE_FAILURE;
        }
        else if (   !strcmp(argv[i], "--version")
                 || !strcmp(argv[i], "-V") )
        {
            RTPrintf("%sr%u", RTBldCfgVersion(),  RTBldCfgRevision());
            return argc == 2 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
        }

    /*
     * Syntax error.
     */
    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "unknown command '%s'", argv[1]);
}

