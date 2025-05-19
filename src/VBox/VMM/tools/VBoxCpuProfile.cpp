/* $Id: VBoxCpuProfile.cpp 168936 2025-05-19 19:47:17Z knut.osmundsen@oracle.com $ */
/** @file
 * VBoxCpuProfile - For testing and poking at the CPU profile DB.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
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
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/vmmr3vtable.h>

#include <iprt/errcore.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/stream.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
static RTLDRMOD                                 g_hModVMM = NIL_RTLDRMOD;
static PCVMMR3VTABLE                            g_pVMM    = NULL;
static PFNVMMGETVTABLE                          g_pfnVMMR3GetVTable;
static PFNCPUMDBGETENTRIES                      g_pfnCPUMR3DbGetEntries;
static PFNCPUMDBGETENTRYBYINDEX                 g_pfnCPUMR3DbGetEntryByIndex;
static PFNCPUMDBGETENTRYBYNAME                  g_pfnCPUMR3DbGetEntryByName;
static PFNCPUMDBGETBESTENTRYBYNAME              g_pfnCPUMR3DbGetBestEntryByName;
static PFNCPUMDBGETBESTENTRYBYARM64MAINID       g_pfnCPUMR3DbGetBestEntryByArm64MainId;
static PFNCPUMCPUIDPRINTARMV8FEATURES           g_pfnCPUMR3CpuIdPrintArmV8Features;
static PFNCPUMCPUIDDETERMINEARMV8MICROARCHEX    g_pfnCPUMCpuIdDetermineArmV8MicroarchEx;

static const struct
{
    bool        fOptional;
    const char *pszSymbol;
    /** Note! Should've been 'PFNRT *ppfn', but clang 14.1 on macos complaints
     *        that "exception specifications are not allowed beyond a single
     *        level of indirection".  So, we're using uintptr_t here as a HACK. */
    uintptr_t  *ppfn;
} g_aImports[] =
{
    { false, VMMR3VTABLE_GETTER_NAME,              (uintptr_t *)&g_pfnVMMR3GetVTable                     },
    { false, "CPUMR3DbGetEntries",                 (uintptr_t *)&g_pfnCPUMR3DbGetEntries                 },
    { false, "CPUMR3DbGetEntryByIndex",            (uintptr_t *)&g_pfnCPUMR3DbGetEntryByIndex            },
    { false, "CPUMR3DbGetEntryByName",             (uintptr_t *)&g_pfnCPUMR3DbGetEntryByName             },
    { false, "CPUMR3DbGetBestEntryByName",         (uintptr_t *)&g_pfnCPUMR3DbGetBestEntryByName         },
    { true,  "CPUMR3DbGetBestEntryByArm64MainId",  (uintptr_t *)&g_pfnCPUMR3DbGetBestEntryByArm64MainId  },
    { true,  "CPUMR3CpuIdPrintArmV8Features",      (uintptr_t *)&g_pfnCPUMR3CpuIdPrintArmV8Features      },
    { true,  "CPUMCpuIdDetermineArmV8MicroarchEx", (uintptr_t *)&g_pfnCPUMCpuIdDetermineArmV8MicroarchEx },
};

static unsigned g_cVerbosity = 1;


#define DISPLAY_ENTRY_F_NOTHING     UINT32_C(0x00000000)
#define DISPLAY_ENTRY_F_INDEX       UINT32_C(0x00000001)
#define DISPLAY_ENTRY_F_SCORE       UINT32_C(0x00000002)
void displayEntry(PCCPUMDBENTRY pEntry, uint32_t uInfo, uint32_t fFlags)
{
    if (fFlags & DISPLAY_ENTRY_F_INDEX)
        RTPrintf("#%u: ", uInfo);
    switch (pEntry->enmEntryType)
    {
        case CPUMDBENTRYTYPE_ARM:       RTPrintf("arm"); break;
        case CPUMDBENTRYTYPE_X86:       RTPrintf("x86"); break;
        default:                        RTPrintf("bogus-entry-type=%u", pEntry->enmEntryType); break;
    }
    RTPrintf(" - %s", pEntry->pszName);
    if (fFlags & DISPLAY_ENTRY_F_SCORE)
        RTPrintf(" - score %u%%", uInfo);
    RTPrintf("\n");
    /** @todo more detailed listing if -v is high enough...   */
}


static void echoCommand(const char *pszFormat, ...)
{
    if (g_cVerbosity > 0)
    {
        va_list va;
        va_start(va, pszFormat);
        RTPrintf("cmd> %N\n", pszFormat, &va);
        va_end(va);
    }
}

int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse parameters.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--path",             'p', RTGETOPT_REQ_STRING },
        { "--vmm",              'p', RTGETOPT_REQ_STRING },
        { "--vmm-path",         'p', RTGETOPT_REQ_STRING },
        { "--quiet",            'q', RTGETOPT_REQ_NOTHING },
        { "--verbose",          'v', RTGETOPT_REQ_NOTHING },
    };

    char            szPath[RTPATH_MAX];
    RTERRINFOSTATIC ErrInfo;

    RTEXITCODE      rcExit = RTEXITCODE_SUCCESS;
    int             chOpt;
    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRC(rc);
    while ((chOpt = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (chOpt)
        {
            case 'p':
            {
                if (g_hModVMM != NIL_RTLDRMOD)
                    RTLdrClose(g_hModVMM);
                const char *pszPath = ValueUnion.psz;
                if (!*pszPath)
                    pszPath = "VBoxVMM";
                if (!RTPathHasPath(pszPath))
                {
                    rc = RTPathExecDir(szPath, sizeof(szPath));
                    if (RT_FAILURE(rc))
                        return RTMsgErrorExitFailure("RTPathExecDir failed: %Rrc", rc);
                    RTPathStripTrailingSlash(szPath);
                    RTPathStripFilename(szPath);
                    rc = RTPathAppend(szPath, sizeof(szPath), pszPath);
                    if (RT_FAILURE(rc))
                        return RTMsgErrorExitFailure("RTPathAppend failed: %Rrc", rc);
                    pszPath = szPath;
                }
                if (!RTPathHasSuffix(pszPath))
                {
                    if (pszPath != szPath)
                    {
                        rc = RTStrCopy(szPath, sizeof(szPath), pszPath);
                        if (RT_FAILURE(rc))
                            return RTMsgErrorExitFailure("VMM path is too long or smth: %Rrc - %s", rc, pszPath);
                        pszPath = szPath;
                    }
                    rc = RTStrCat(szPath, sizeof(szPath), RTLdrGetSuff());
                    if (RT_FAILURE(rc))
                        return RTMsgErrorExitFailure("VMM path is too long: %Rrc", rc);
                }

                rc = RTLdrLoadEx(pszPath, &g_hModVMM, RTLDRLOAD_FLAGS_LOCAL, RTErrInfoInitStatic(&ErrInfo));
                if (RT_FAILURE(rc))
                    return RTMsgErrorExitFailure("RTLdrLoadEx failed on '%s': %Rrc%-RTeim", pszPath, rc, &ErrInfo.Core);
                for (unsigned i = 0; i < RT_ELEMENTS(g_aImports); i++)
                {
                    void *pv = NULL;
                    rc = RTLdrGetSymbol(g_hModVMM, g_aImports[i].pszSymbol, &pv);
                    if (RT_FAILURE(rc))
                    {
                        if (!g_aImports[i].fOptional)
                            return RTMsgErrorExitFailure("Unable to resolve %s in %s: %Rrc", g_aImports[i].pszSymbol, pszPath, rc);
                        pv = NULL;
                    }
                    *g_aImports[i].ppfn = (uintptr_t)pv;
                }
                g_pVMM = g_pfnVMMR3GetVTable();
                if (!RT_VALID_PTR(g_pVMM))
                    return RTMsgErrorExitFailure("VMMR3GetVTable in %s returns a bogus pointer: %p", pszPath, g_pVMM);
                if (!VMMR3VTABLE_IS_COMPATIBLE(g_pVMM->uMagicVersion))
                    return RTMsgErrorExitFailure("Incompatible VMM '%s': magic+ver is %#RX64, expected something compatible with %#RX64",
                                                 pszPath, g_pVMM->uMagicVersion, VMMR3VTABLE_MAGIC_VERSION);
                RTMsgInfo("Loaded '%s' - vtable v%u.%u, target %u (%s), description '%s'.\n",
                          pszPath,
                          (unsigned)((g_pVMM->uMagicVersion >> 48) & 0xffff), (unsigned)((g_pVMM->uMagicVersion >> 32) & 0xffff),
                          g_pVMM->fFlags & VMMR3VTABLE_F_TARGET_MASK,
                          (g_pVMM->fFlags & VMMR3VTABLE_F_TARGET_MASK) == VMMR3VTABLE_F_TARGET_X86 ? "X86"
                          : (g_pVMM->fFlags & VMMR3VTABLE_F_TARGET_MASK) == VMMR3VTABLE_F_TARGET_ARMV8 ? "ARMv8" : "unknown",
                          g_pVMM->pszDescription);
                break;
            }

            case 'q':
                g_cVerbosity = 0;
                break;

            case 'v':
                g_cVerbosity += 1;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                if (g_hModVMM == NIL_RTLDRMOD)
                    return RTMsgErrorExitFailure("No VMM module has been specified!");

                const char * const pszCmd = ValueUnion.psz;
                if (strcmp(pszCmd, "list") == 0)
                {
                    echoCommand("%s", pszCmd);

                    uint32_t const cEntries = g_pfnCPUMR3DbGetEntries();
                    for (uint32_t i = 0; i < cEntries; i++)
                        displayEntry(g_pfnCPUMR3DbGetEntryByIndex(i), i, DISPLAY_ENTRY_F_INDEX);
                }
                else if (   strcmp(pszCmd, "best-by-name") == 0
                         || strcmp(pszCmd, "best-arm-by-name") == 0
                         || strcmp(pszCmd, "best-x86-by-name") == 0)
                {
                    CPUMDBENTRYTYPE const enmEntryType = strstr(pszCmd, "arm") != NULL ? CPUMDBENTRYTYPE_ARM
                                                       : strstr(pszCmd, "x86") != NULL ? CPUMDBENTRYTYPE_X86
                                                       :                                 CPUMDBENTRYTYPE_INVALID;
                    rc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_STRING);
                    if (RT_FAILURE(rc))
                        return RTMsgSyntax("The '%s' command requires a name string.", pszCmd);
                    echoCommand("%s '%s'", pszCmd, ValueUnion.psz);

                    uint32_t      uScore = 0;
                    PCCPUMDBENTRY pEntry = g_pfnCPUMR3DbGetBestEntryByName(ValueUnion.psz, enmEntryType, &uScore);
                    if (pEntry)
                        displayEntry(pEntry, uScore, DISPLAY_ENTRY_F_SCORE);
                    else
                        rcExit = RTMsgErrorExitFailure("%s: No match for '%s'", pszCmd, ValueUnion.psz);
                }
                else if (strcmp(pszCmd, "best-by-midr") == 0)
                {
                    rc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_HEX);
                    if (RT_FAILURE(rc))
                        return RTMsgSyntax("The '%s' command requires a MIDR_EL1 value (64-bit, defaults to hex).", pszCmd);
                    echoCommand("%s %#RX64", pszCmd, ValueUnion.u64);

                    if (g_pfnCPUMR3DbGetBestEntryByArm64MainId)
                    {
                        uint32_t         uScore = 0;
                        PCCPUMDBENTRYARM pEntry = g_pfnCPUMR3DbGetBestEntryByArm64MainId(ValueUnion.u64, &uScore);
                        if (pEntry)
                            displayEntry(&pEntry->Core, uScore, DISPLAY_ENTRY_F_SCORE);
                        else
                            rcExit = RTMsgErrorExitFailure("%s: No match for midr %#RX64", pszCmd, ValueUnion.u64);
                    }
                    else
                        rcExit = RTMsgErrorExitFailure("%s: CPUMR3DbGetBestEntryByArm64MainId missing from the current VMM",
                                                       pszCmd);
                }
                else
                    return RTMsgSyntax("Unknown command: %s", pszCmd);
                break;
            }

            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }

    return rcExit;
}

