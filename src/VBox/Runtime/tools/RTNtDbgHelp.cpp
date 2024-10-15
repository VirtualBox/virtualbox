/* $Id: RTNtDbgHelp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTNtDbgHelp -  Tool for working/exploring DbgHelp.dll.
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
#include <iprt/win/windows.h>
#include <iprt/win/dbghelp.h>

#include <iprt/alloca.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/env.h>
#include <iprt/initterm.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/errcore.h>

#include <iprt/win/lazy-dbghelp.h>

#include <iprt/ldrlazy.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Debug module record.
 *
 * Used for dumping the whole context.
 */
typedef struct RTNTDBGHELPMOD
{
    /** The list bits. */
    RTLISTNODE      ListEntry;
    /** The module address. */
    uint64_t        uModAddr;
    /** Pointer to the name part of szFullName. */
    char           *pszName;
    /** The module name. */
    char            szFullName[1];
} RTNTDBGHELPMOD;
/** Pointer to a debug module. */
typedef RTNTDBGHELPMOD *PRTNTDBGHELPMOD;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Verbosity level. */
static int          g_iOptVerbose = 1;

/** Fake process handle. */
static HANDLE       g_hFake = (HANDLE)0x1234567;
/** Number of modules in the list. */
static uint32_t     g_cModules = 0;
/** Module list. */
static RTLISTANCHOR g_ModuleList;
/** Set when initialized, clear until then. Lazy init on first operation. */
static bool         g_fInitialized = false;

/** The current address register. */
static uint64_t     g_uCurAddress = 0;



/**
 * For debug/verbose output.
 *
 * @param   iMin                The minimum verbosity level for this message.
 * @param   pszFormat           The format string.
 * @param   ...                 The arguments referenced in the format string.
 */
static void infoPrintf(int iMin, const char *pszFormat, ...)
{
    if (g_iOptVerbose >= iMin)
    {
        va_list va;
        va_start(va, pszFormat);
        RTPrintf("info: ");
        RTPrintfV(pszFormat, va);
        va_end(va);
    }
}

static BOOL CALLBACK symDebugCallback64(HANDLE hProcess, ULONG uAction, ULONG64 ullData, ULONG64 ullUserCtx)
{
    NOREF(hProcess); NOREF(ullUserCtx);
    switch (uAction)
    {
        case CBA_DEBUG_INFO:
        {
            const char *pszMsg = (const char *)(uintptr_t)ullData;
            size_t      cchMsg = strlen(pszMsg);
            if (cchMsg > 0 && pszMsg[cchMsg - 1] == '\n')
                RTPrintf("cba_debug_info: %s", pszMsg);
            else
                RTPrintf("cba_debug_info: %s\n", pszMsg);
            return TRUE;
        }

        case CBA_DEFERRED_SYMBOL_LOAD_CANCEL:
            return FALSE;

        case CBA_EVENT:
            return FALSE;

        default:
            RTPrintf("cba_???: uAction=%#x ullData=%#llx\n", uAction, ullData);
            break;
    }

    return FALSE;
}

/**
 * Lazy initialization.
 * @returns Exit code with any relevant complaints printed.
 */
static RTEXITCODE ensureInitialized(void)
{
    if (!g_fInitialized)
    {
        if (!SymInitialize(g_hFake, NULL, FALSE))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "SymInitialied failed: %u\n", GetLastError());
        if (!SymRegisterCallback64(g_hFake, symDebugCallback64, 0))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "SymRegisterCallback64 failed: %u\n", GetLastError());
        g_fInitialized = true;
        infoPrintf(2, "SymInitialized(,,)\n");
    }
    return RTEXITCODE_SUCCESS;
}


/**
 * Loads the given module, the address is either automatic or a previously given
 * one.
 *
 * @returns Exit code with any relevant complaints printed.
 * @param   pszFile             The file to load.
 */
static RTEXITCODE loadModule(const char *pszFile)
{
    RTEXITCODE rcExit = ensureInitialized();
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    uint64_t uModAddrReq = g_uCurAddress == 0 ? UINT64_C(0x1000000) * g_cModules : g_uCurAddress;
    uint64_t uModAddrGot = SymLoadModuleEx(g_hFake, NULL /*hFile*/, pszFile, NULL /*pszModuleName*/,
                                           uModAddrReq, 0, NULL /*pData*/, 0 /*fFlags*/);
    if (uModAddrGot == 0)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "SymLoadModuleEx failed: %u\n", GetLastError());

    size_t cbFullName = strlen(pszFile) + 1;
    PRTNTDBGHELPMOD pMod = (PRTNTDBGHELPMOD)RTMemAlloc(RT_UOFFSETOF_DYN(RTNTDBGHELPMOD, szFullName[cbFullName + 1]));
    memcpy(pMod->szFullName, pszFile, cbFullName);
    pMod->pszName  = RTPathFilename(pMod->szFullName);
    pMod->uModAddr = uModAddrGot;
    RTListAppend(&g_ModuleList, &pMod->ListEntry);
    infoPrintf(1, "%#018RX64 %s\n", pMod->uModAddr, pMod->pszName);

    return RTEXITCODE_SUCCESS;
}


/**
 * Translates SYM_TYPE to string.
 *
 * @returns String.
 * @param   enmType             The symbol type value.
 */
static const char *symTypeName(SYM_TYPE enmType)
{
    switch (enmType)
    {
        case SymCoff:       return "SymCoff";
        case SymCv:         return "SymCv";
        case SymPdb:        return "SymPdb";
        case SymExport:     return "SymExport";
        case SymDeferred:   return "SymDeferred";
        case SymSym:        return "SymSym";
        case SymDia:        return "SymDia";
        case SymVirtual:    return "SymVirtual";
        default:
        {
            static char s_szBuf[32];
            RTStrPrintf(s_szBuf, sizeof(s_szBuf), "Unknown-%#x", enmType);
            return s_szBuf;
        }
    }
}


/**
 * Symbol enumeration callback.
 *
 * @returns TRUE (continue enum).
 * @param   pSymInfo            The symbol info.
 * @param   cbSymbol            The symbol length (calculated).
 * @param   pvUser              NULL.
 */
static BOOL CALLBACK dumpSymbolCallback(PSYMBOL_INFO pSymInfo, ULONG cbSymbol, PVOID pvUser)
{
    NOREF(pvUser);
    RTPrintf("  %#018RX64 LB %#07x  %s\n", pSymInfo->Address, cbSymbol, pSymInfo->Name);
    return TRUE;
}

/**
 * Dumps all info.
 * @returns Exit code with any relevant complaints printed.
 */
static RTEXITCODE dumpAll(void)
{
    RTEXITCODE      rcExit = RTEXITCODE_SUCCESS;
    PRTNTDBGHELPMOD pMod;
    RTListForEach(&g_ModuleList, pMod, RTNTDBGHELPMOD, ListEntry)
    {
        RTPrintf("*** %#018RX64 - %s ***\n", pMod->uModAddr, pMod->szFullName);

        static const int8_t s_acbVariations[]  = { 0, -4, -8, -12, -16, -20, -24, -28, -32, 4, 8, 12, 16, 20, 24, 28, 32 };
        unsigned            iVariation = 0;
        union
        {
            IMAGEHLP_MODULE64   ModInfo;
            uint8_t             abPadding[sizeof(IMAGEHLP_MODULE64) + 64];
        } u;

        BOOL fRc;
        do
        {
            RT_ZERO(u.ModInfo);
            u.ModInfo.SizeOfStruct = sizeof(u.ModInfo) + s_acbVariations[iVariation++];
            fRc = SymGetModuleInfo64(g_hFake, pMod->uModAddr, &u.ModInfo);
        } while (!fRc && GetLastError() == ERROR_INVALID_PARAMETER && iVariation < RT_ELEMENTS(s_acbVariations));

        if (fRc)
        {
            RTPrintf("    BaseOfImage     = %#018llx\n", u.ModInfo.BaseOfImage);
            RTPrintf("    ImageSize       = %#010x\n", u.ModInfo.ImageSize);
            RTPrintf("    TimeDateStamp   = %#010x\n", u.ModInfo.TimeDateStamp);
            RTPrintf("    CheckSum        = %#010x\n", u.ModInfo.CheckSum);
            RTPrintf("    NumSyms         = %#010x (%u)\n", u.ModInfo.NumSyms, u.ModInfo.NumSyms);
            RTPrintf("    SymType         = %s\n", symTypeName(u.ModInfo.SymType));
            RTPrintf("    ModuleName      = %.32s\n", u.ModInfo.ModuleName);
            RTPrintf("    ImageName       = %.256s\n", u.ModInfo.ImageName);
            RTPrintf("    LoadedImageName = %.256s\n", u.ModInfo.LoadedImageName);
            RTPrintf("    LoadedPdbName   = %.256s\n", u.ModInfo.LoadedPdbName);
            RTPrintf("    CVSig           = %#010x\n", u.ModInfo.CVSig);
            /** @todo CVData. */
            RTPrintf("    PdbSig          = %#010x\n", u.ModInfo.PdbSig);
            RTPrintf("    PdbSig70        = %RTuuid\n", &u.ModInfo.PdbSig70);
            RTPrintf("    PdbAge          = %#010x\n", u.ModInfo.PdbAge);
            RTPrintf("    PdbUnmatched    = %RTbool\n", u.ModInfo.PdbUnmatched);
            RTPrintf("    DbgUnmatched    = %RTbool\n", u.ModInfo.DbgUnmatched);
            RTPrintf("    LineNumbers     = %RTbool\n", u.ModInfo.LineNumbers);
            RTPrintf("    GlobalSymbols   = %RTbool\n", u.ModInfo.GlobalSymbols);
            RTPrintf("    TypeInfo        = %RTbool\n", u.ModInfo.TypeInfo);
            RTPrintf("    SourceIndexed   = %RTbool\n", u.ModInfo.SourceIndexed);
            RTPrintf("    Publics         = %RTbool\n", u.ModInfo.Publics);
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "SymGetModuleInfo64 failed: %u\n", GetLastError());

        if (!SymEnumSymbols(g_hFake, pMod->uModAddr, NULL, dumpSymbolCallback, NULL))
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "SymEnumSymbols failed: %u\n", GetLastError());

    }
    return rcExit;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    RTListInit(&g_ModuleList);

    /*
     * Parse options.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--dump-all",         'd', RTGETOPT_REQ_NOTHING },
        { "--load",             'l', RTGETOPT_REQ_STRING  },
        { "--set-address",      'a', RTGETOPT_REQ_UINT64  },
#define OPT_SET_DEBUG_INFO  0x1000
        { "--set-debug-info",   OPT_SET_DEBUG_INFO, RTGETOPT_REQ_NOTHING  },
        { "--verbose",          'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",            'q', RTGETOPT_REQ_NOTHING },
    };

    RTEXITCODE  rcExit      = RTEXITCODE_SUCCESS;
    //const char *pszOutput   = "-";

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                 RTGETOPTINIT_FLAGS_OPTS_FIRST);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'v':
                g_iOptVerbose++;
                break;

            case 'q':
                g_iOptVerbose++;
                break;

            case 'l':
                rcExit = loadModule(ValueUnion.psz);
                break;

            case 'a':
                g_uCurAddress = ValueUnion.u64;
                break;

            case 'd':
                rcExit = dumpAll();
                break;

            case OPT_SET_DEBUG_INFO:
                rcExit = ensureInitialized();
                if (rcExit == RTEXITCODE_SUCCESS && !SymSetOptions(SymGetOptions() | SYMOPT_DEBUG))
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "SymSetOptions failed: %u\n", GetLastError());
                break;


            case 'V':
                RTPrintf("$Revision: 106320 $");
                break;

            case 'h':
                RTPrintf("usage: %s [-v|--verbose] [-q|--quiet] [--set-debug-info] [-a <addr>] [-l <file>] [-d] [...]\n"
                         "   or: %s [-V|--version]\n"
                         "   or: %s [-h|--help]\n",
                         argv[0], argv[0], argv[0]);
                return RTEXITCODE_SUCCESS;

            case VINF_GETOPT_NOT_OPTION:
            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
        if (rcExit != RTEXITCODE_SUCCESS)
            break;
    }
    return rcExit;
}

