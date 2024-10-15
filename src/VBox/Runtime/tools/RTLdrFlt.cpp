/* $Id: RTLdrFlt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Utility for translating addresses into symbols+offset.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/dbg.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>



/** Worker for ProduceKAllSyms. */
static void PrintSymbolForKAllSyms(const char *pszModule, PCRTDBGSYMBOL pSymInfo, PCRTDBGSEGMENT pSegInfo,
                                   RTUINTPTR uBaseAddr, bool fOneSeg)
{
    RTUINTPTR uAddr;
    char chType = 't';
    if (pSymInfo->iSeg < RTDBGSEGIDX_SPECIAL_FIRST)
    {
        uAddr = uBaseAddr + pSymInfo->offSeg;
        if (!fOneSeg)
            uAddr += pSegInfo->uRva;
        if (pSegInfo->szName[0])
        {
            if (strstr(pSegInfo->szName, "rodata") != NULL)
                chType = 'r';
            else if (strstr(pSegInfo->szName, "bss") != NULL)
                chType = 'b';
            else if (strstr(pSegInfo->szName, "data") != NULL)
                chType = 'd';
        }
    }
    else if (pSymInfo->iSeg == RTDBGSEGIDX_ABS)
    {
        chType = 'a';
        uAddr = pSymInfo->offSeg;
    }
    else if (pSymInfo->iSeg == RTDBGSEGIDX_RVA)
    {
        Assert(!fOneSeg);
        uAddr = uBaseAddr + pSymInfo->offSeg;
    }
    else
    {
        RTMsgError("Unsupported special segment %#x for %s in %s!", pSymInfo->iSeg, pSymInfo->szName, pszModule);
        return;
    }

    RTPrintf("%RTptr %c %s\t[%s]\n", uAddr, chType, pSymInfo->szName, pszModule);
}


/**
 * Produces a /proc/kallsyms compatible symbol listing of @a hDbgAs on standard
 * output.
 *
 * @returns Exit code.
 * @param   hDbgAs              The address space to dump.
 */
static RTEXITCODE ProduceKAllSyms(RTDBGAS hDbgAs)
{
    /*
     * Iterate modules.
     */
    uint32_t cModules = RTDbgAsModuleCount(hDbgAs);
    for (uint32_t iModule = 0; iModule < cModules; iModule++)
    {
        RTDBGMOD const     hDbgMod   = RTDbgAsModuleByIndex(hDbgAs, iModule);
        const char * const pszModule = RTDbgModName(hDbgMod);

        /*
         * Iterate mappings of the module.
         */
        RTDBGASMAPINFO  aMappings[128];
        uint32_t        cMappings = RT_ELEMENTS(aMappings);
        int rc = RTDbgAsModuleQueryMapByIndex(hDbgAs, iModule, &aMappings[0], &cMappings, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            for (uint32_t iMapping = 0; iMapping < cMappings; iMapping++)
            {
                RTDBGSEGMENT SegInfo = {0};
                if (aMappings[iMapping].iSeg == NIL_RTDBGSEGIDX)
                {
                    /*
                     * Flat mapping of the entire module.
                     */
                    SegInfo.iSeg = NIL_RTDBGSEGIDX;
                    uint32_t cSymbols = RTDbgModSymbolCount(hDbgMod);
                    for (uint32_t iSymbol = 0; iSymbol < cSymbols; iSymbol++)
                    {
                        RTDBGSYMBOL SymInfo;
                        rc = RTDbgModSymbolByOrdinal(hDbgMod, iSymbol, &SymInfo);
                        if (RT_SUCCESS(rc))
                        {
                            if (   SymInfo.iSeg != SegInfo.iSeg
                                && SymInfo.iSeg < RTDBGSEGIDX_SPECIAL_FIRST)
                            {
                                rc = RTDbgModSegmentByIndex(hDbgMod, SymInfo.iSeg, &SegInfo);
                                if (RT_FAILURE(rc))
                                {
                                    RTMsgError("RTDbgModSegmentByIndex(%s, %u) failed: %Rrc", pszModule, SymInfo.iSeg, rc);
                                    continue;
                                }
                            }
                            PrintSymbolForKAllSyms(pszModule, &SymInfo, &SegInfo, aMappings[iMapping].Address, false);
                        }
                        else
                            RTMsgError("RTDbgModSymbolByOrdinal(%s, %u) failed: %Rrc", pszModule, iSymbol, rc);
                    }
                }
                else
                {
                    /*
                     * Just one segment.
                     */
                    rc = RTDbgModSegmentByIndex(hDbgMod, aMappings[iMapping].iSeg, &SegInfo);
                    if (RT_SUCCESS(rc))
                    {
                        /** @todo    */
                    }
                    else
                        RTMsgError("RTDbgModSegmentByIndex(%s, %u) failed: %Rrc", pszModule, aMappings[iMapping].iSeg, rc);
                }
            }
        }
        else
            RTMsgError("RTDbgAsModuleQueryMapByIndex failed: %Rrc", rc);
        RTDbgModRelease(hDbgMod);
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Dumps the address space.
 */
static void DumpAddressSpace(RTDBGAS hDbgAs, unsigned cVerbosityLevel)
{
    RTPrintf("*** Address Space Dump ***\n");
    uint32_t cModules = RTDbgAsModuleCount(hDbgAs);
    for (uint32_t iModule = 0; iModule < cModules; iModule++)
    {
        RTDBGMOD        hDbgMod = RTDbgAsModuleByIndex(hDbgAs, iModule);
        RTPrintf("Module #%u: %s\n", iModule, RTDbgModName(hDbgMod));

        RTDBGASMAPINFO  aMappings[128];
        uint32_t        cMappings = RT_ELEMENTS(aMappings);
        int rc = RTDbgAsModuleQueryMapByIndex(hDbgAs, iModule, &aMappings[0], &cMappings, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            for (uint32_t iMapping = 0; iMapping < cMappings; iMapping++)
            {
                if (aMappings[iMapping].iSeg == NIL_RTDBGSEGIDX)
                {
                    RTPrintf("  mapping #%u: %RTptr-%RTptr\n",
                             iMapping,
                             aMappings[iMapping].Address,
                             aMappings[iMapping].Address + RTDbgModImageSize(hDbgMod) - 1);
                    if (cVerbosityLevel > 2)
                    {
                        uint32_t cSegments = RTDbgModSegmentCount(hDbgMod);
                        for (uint32_t iSeg = 0; iSeg < cSegments; iSeg++)
                        {
                            RTDBGSEGMENT SegInfo;
                            rc = RTDbgModSegmentByIndex(hDbgMod, iSeg, &SegInfo);
                            if (RT_SUCCESS(rc))
                                RTPrintf("      seg #%u: %RTptr LB %RTptr '%s'\n",
                                         iSeg, SegInfo.uRva, SegInfo.cb, SegInfo.szName);
                            else
                                RTPrintf("      seg #%u: %Rrc\n", iSeg, rc);
                        }
                    }
                }
                else
                {
                    RTDBGSEGMENT SegInfo;
                    rc = RTDbgModSegmentByIndex(hDbgMod, aMappings[iMapping].iSeg, &SegInfo);
                    if (RT_SUCCESS(rc))
                        RTPrintf("  mapping #%u: %RTptr-%RTptr (segment #%u - '%s')\n",
                                 iMapping,
                                 aMappings[iMapping].Address,
                                 aMappings[iMapping].Address + SegInfo.cb,
                                 SegInfo.iSeg, SegInfo.szName);
                    else
                        RTPrintf("  mapping #%u: %RTptr-???????? (segment #%u) rc=%Rrc\n",
                                 iMapping, aMappings[iMapping].Address, aMappings[iMapping].iSeg, rc);
                }

                if (cVerbosityLevel > 1)
                {
                    uint32_t cSymbols = RTDbgModSymbolCount(hDbgMod);
                    RTPrintf("    %u symbols\n", cSymbols);
                    for (uint32_t iSymbol = 0; iSymbol < cSymbols; iSymbol++)
                    {
                        RTDBGSYMBOL SymInfo;
                        rc = RTDbgModSymbolByOrdinal(hDbgMod, iSymbol, &SymInfo);
                        if (RT_SUCCESS(rc))
                            RTPrintf("    #%04u at %08x:%RTptr (%RTptr) %05llx %s\n",
                                     SymInfo.iOrdinal, SymInfo.iSeg, SymInfo.offSeg, SymInfo.Value,
                                     (uint64_t)SymInfo.cb, SymInfo.szName);
                    }
                }
            }
        }
        else
            RTMsgError("RTDbgAsModuleQueryMapByIndex failed: %Rrc", rc);
        RTDbgModRelease(hDbgMod);
    }
    RTPrintf("*** End of Address Space Dump ***\n");
}


/**
 * Tries to parse out an address at the head of the string.
 *
 * @returns true if found address, false if not.
 * @param   psz                 Where to start parsing.
 * @param   pcchAddress         Where to store the address length.
 * @param   pu64Address         Where to store the address value.
 */
static bool TryParseAddress(const char *psz, size_t *pcchAddress, uint64_t *pu64Address)
{
    const char *pszStart = psz;

    /*
     * Hex prefix?
     */
    if (psz[0] == '0' && (psz[1] == 'x' || psz[1] == 'X'))
        psz += 2;

    /*
     * How many hex digits?  We want at least 4 and at most 16.
     */
    size_t off = 0;
    while (RT_C_IS_XDIGIT(psz[off]))
        off++;
    if (off < 4 || off > 16)
        return false;

    /*
     * Check for separator (xxxxxxxx'yyyyyyyy).
     */
    bool fHave64bitSep = off <= 8
                      && psz[off] == '\''
                      && RT_C_IS_XDIGIT(psz[off + 1])
                      && RT_C_IS_XDIGIT(psz[off + 2])
                      && RT_C_IS_XDIGIT(psz[off + 3])
                      && RT_C_IS_XDIGIT(psz[off + 4])
                      && RT_C_IS_XDIGIT(psz[off + 5])
                      && RT_C_IS_XDIGIT(psz[off + 6])
                      && RT_C_IS_XDIGIT(psz[off + 7])
                      && RT_C_IS_XDIGIT(psz[off + 8])
                      && !RT_C_IS_XDIGIT(psz[off + 9]);
    if (fHave64bitSep)
    {
        uint32_t u32High;
        int rc = RTStrToUInt32Ex(psz, NULL, 16, &u32High);
        if (rc != VWRN_TRAILING_CHARS)
            return false;

        uint32_t u32Low;
        rc = RTStrToUInt32Ex(&psz[off + 1], NULL, 16, &u32Low);
        if (   rc != VINF_SUCCESS
            && rc != VWRN_TRAILING_SPACES
            && rc != VWRN_TRAILING_CHARS)
            return false;

        *pu64Address = RT_MAKE_U64(u32Low, u32High);
        off += 1 + 8;
    }
    else
    {
        int rc = RTStrToUInt64Ex(psz, NULL, 16, pu64Address);
        if (   rc != VINF_SUCCESS
            && rc != VWRN_TRAILING_SPACES
            && rc != VWRN_TRAILING_CHARS)
            return false;
    }

    *pcchAddress = psz + off - pszStart;
    return true;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Create an empty address space that we can load modules and stuff into
     * as we parse the parameters.
     */
    RTDBGAS hDbgAs;
    rc = RTDbgAsCreate(&hDbgAs, 0, RTUINTPTR_MAX, "");
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTDBgAsCreate -> %Rrc", rc);

    /*
     * Create a debugging configuration instance to work with so that we can
     * make use of (i.e. test) path searching and such.
     */
    RTDBGCFG hDbgCfg;
    rc = RTDbgCfgCreate(&hDbgCfg, "IPRT", true /*fNativePaths*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTDbgCfgCreate -> %Rrc", rc);

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--input",        'i', RTGETOPT_REQ_STRING },
        { "--local-file",   'l', RTGETOPT_REQ_NOTHING },
        { "--cache-file",   'c', RTGETOPT_REQ_NOTHING },
        { "--pe-image",     'p', RTGETOPT_REQ_NOTHING },
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING },
        { "--x86",          '8', RTGETOPT_REQ_NOTHING },
        { "--amd64",        '6', RTGETOPT_REQ_NOTHING },
        { "--whatever",     '*', RTGETOPT_REQ_NOTHING },
        { "--kallsyms",     'k', RTGETOPT_REQ_NOTHING },
    };

    PRTSTREAM       pInput          = g_pStdIn;
    PRTSTREAM       pOutput         = g_pStdOut;
    unsigned        cVerbosityLevel = 0;
    enum {
        kOpenMethod_FromImage,
        kOpenMethod_FromPeImage
    }               enmOpenMethod   = kOpenMethod_FromImage;
    bool            fCacheFile      = false;
    RTLDRARCH       enmArch         = RTLDRARCH_WHATEVER;
    bool            fKAllSyms       = false;

    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            case 'i':
                rc = RTStrmOpen(ValueUnion.psz, "r", &pInput);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to open '%s' for reading: %Rrc", ValueUnion.psz, rc);
                break;

            case 'c':
                fCacheFile = true;
                break;

            case 'k':
                fKAllSyms = true;
                break;

            case 'l':
                fCacheFile = false;
                break;

            case 'p':
                enmOpenMethod = kOpenMethod_FromPeImage;
                break;

            case 'v':
                cVerbosityLevel++;
                break;

            case '8':
                enmArch = RTLDRARCH_X86_32;
                break;

            case '6':
                enmArch = RTLDRARCH_AMD64;
                break;

            case '*':
                enmArch = RTLDRARCH_WHATEVER;
                break;

            case 'h':
                RTPrintf("Usage: %s [options] <module> <address> [<module> <address> [..]]\n"
                         "\n"
                         "Options:\n"
                         "  -i,--input=file\n"
                         "      Specify a input file instead of standard input.\n"
                         "  --pe-image\n"
                         "      Use RTDbgModCreateFromPeImage to open the file."
                         "  -v, --verbose\n"
                         "      Display the address space before doing the filtering.\n"
                         "  --amd64,--x86,--whatever\n"
                         "      Selects the desired architecture.\n"
                         "  -k,--kallsyms\n"
                         "      Produce a /proc/kallsyms compatible symbol listing and quit.\n"
                         "  -h, -?, --help\n"
                         "      Display this help text and exit successfully.\n"
                         "  -V, --version\n"
                         "      Display the revision and exit successfully.\n"
                         , RTPathFilename(argv[0]));
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                return RTEXITCODE_SUCCESS;

            case VINF_GETOPT_NOT_OPTION:
            {
                /* <module> <address> */
                const char *pszModule = ValueUnion.psz;

                rc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_HEX);
                if (RT_FAILURE(rc))
                    return RTGetOptPrintError(rc, &ValueUnion);
                uint64_t u64Address = ValueUnion.u64;

                uint32_t cbImage    = 0;
                uint32_t uTimestamp = 0;
                if (fCacheFile)
                {
                    rc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_HEX);
                    if (RT_FAILURE(rc))
                        return RTGetOptPrintError(rc, &ValueUnion);
                    cbImage = ValueUnion.u32;

                    rc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_HEX);
                    if (RT_FAILURE(rc))
                        return RTGetOptPrintError(rc, &ValueUnion);
                    uTimestamp = ValueUnion.u32;
                }

                RTDBGMOD hMod;
                if (enmOpenMethod == kOpenMethod_FromImage)
                    rc = RTDbgModCreateFromImage(&hMod, pszModule, NULL, enmArch, hDbgCfg);
                else
                    rc = RTDbgModCreateFromPeImage(&hMod, pszModule, NULL, NULL, cbImage, uTimestamp, hDbgCfg);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTDbgModCreateFromImage(,%s,,) -> %Rrc", pszModule, rc);

                rc = RTDbgAsModuleLink(hDbgAs, hMod, u64Address, 0 /* fFlags */);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTDbgAsModuleLink(,%s,%llx,) -> %Rrc", pszModule, u64Address, rc);
                break;
            }

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    /*
     * Display the address space.
     */
    if (cVerbosityLevel)
        DumpAddressSpace(hDbgAs, cVerbosityLevel);

    /*
     * Produce the /proc/kallsyms output.
     */
    if (fKAllSyms)
        return ProduceKAllSyms(hDbgAs);

    /*
     * Read text from standard input and see if there is anything we can translate.
     */
    for (;;)
    {
        /* Get a line. */
        char szLine[_64K];
        rc = RTStrmGetLine(pInput, szLine, sizeof(szLine));
        if (rc == VERR_EOF)
            break;
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTStrmGetLine() -> %Rrc\n", rc);

        /*
         * Search the line for potential addresses and replace them with
         * symbols+offset.
         */
        const char *pszStart = szLine;
        const char *psz      = szLine;
        char        ch;
        while ((ch = *psz) != '\0')
        {
            size_t      cchAddress;
            uint64_t    u64Address;

            if (   (   ch == '0'
                    && (psz[1] == 'x' || psz[1] == 'X')
                    && TryParseAddress(psz, &cchAddress, &u64Address))
                || (   RT_C_IS_XDIGIT(ch)
                    && TryParseAddress(psz, &cchAddress, &u64Address))
               )
            {
                /* Print. */
                psz += cchAddress;
                if (pszStart != psz)
                    RTStrmWrite(pOutput, pszStart, psz - pszStart);
                pszStart = psz;

                /* Try get the module. */
                RTUINTPTR   uAddr;
                RTDBGSEGIDX iSeg;
                RTDBGMOD    hDbgMod;
                rc = RTDbgAsModuleByAddr(hDbgAs, u64Address, &hDbgMod, &uAddr, &iSeg);
                if (RT_SUCCESS(rc))
                {
                    if (iSeg != UINT32_MAX)
                        RTStrmPrintf(pOutput, "=[%s:%u", RTDbgModName(hDbgMod), iSeg);
                    else
                        RTStrmPrintf(pOutput, "=[%s", RTDbgModName(hDbgMod));

                    /*
                     * Do we have symbols?
                     */
                    RTDBGSYMBOL Symbol;
                    RTINTPTR    offSym;
                    rc = RTDbgAsSymbolByAddr(hDbgAs, u64Address, RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL, &offSym, &Symbol, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        if (!offSym)
                            RTStrmPrintf(pOutput, "!%s", Symbol.szName);
                        else if (offSym > 0)
                            RTStrmPrintf(pOutput, "!%s+%#llx", Symbol.szName, offSym);
                        else
                            RTStrmPrintf(pOutput, "!%s-%#llx", Symbol.szName, -offSym);
                    }
                    else
                        RTStrmPrintf(pOutput, "+%#llx", u64Address - uAddr);

                    /*
                     * Do we have line numbers?
                     */
                    RTDBGLINE   Line;
                    RTINTPTR    offLine;
                    rc = RTDbgAsLineByAddr(hDbgAs, u64Address, &offLine, &Line, NULL);
                    if (RT_SUCCESS(rc))
                        RTStrmPrintf(pOutput, " %Rbn(%u)", Line.szFilename, Line.uLineNo);

                    RTStrmPrintf(pOutput, "]");
                    RTDbgModRelease(hDbgMod);
                }
            }
            else
                psz++;
        }

        if (pszStart != psz)
            RTStrmWrite(pOutput, pszStart, psz - pszStart);
        RTStrmPutCh(pOutput, '\n');
    }

    return RTEXITCODE_SUCCESS;
}

