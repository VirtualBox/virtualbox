/* $Id: RTLdrCheckImports.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Module dependency checker.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/buildconfig.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/vfs.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Import checker options.
 */
typedef struct RTCHECKIMPORTSOPTS
{
    /** Number of paths to search. */
    size_t      cPaths;
    /** Search directories. */
    char      **papszPaths;
    /** The loader architecture. */
    RTLDRARCH   enmLdrArch;
    /** Verbosity level. */
    unsigned    cVerbosity;
    /** Whether to also list orinals in the export listing. */
    bool        fListOrdinals;
} RTCHECKIMPORTSOPTS;
/** Pointer to the checker options. */
typedef RTCHECKIMPORTSOPTS *PRTCHECKIMPORTSOPTS;
/** Pointer to the const checker options. */
typedef RTCHECKIMPORTSOPTS const *PCRTCHECKIMPORTSOPTS;


/**
 * Import module.
 */
typedef struct RTCHECKIMPORTMODULE
{
    /** The module. If NIL, then we've got a export list (papszExports). */
    RTLDRMOD    hLdrMod;
    /** Number of export in the export list.  (Zero if hLdrMod is valid.) */
    size_t      cExports;
    /** Export list. (NULL if hLdrMod is valid.)   */
    char      **papszExports;
    /** The module name. */
    char        szModule[256];
} RTCHECKIMPORTMODULE;
/** Pointer to an import module. */
typedef RTCHECKIMPORTMODULE *PRTCHECKIMPORTMODULE;


/**
 * Import checker state (for each image being checked).
 */
typedef struct RTCHECKIMPORTSTATE
{
    /** The image we're processing. */
    const char             *pszImage;
    /** The image we're processing. */
    PCRTCHECKIMPORTSOPTS    pOpts;
    /** Status code. */
    int                     iRc;
    /** Import hint.   */
    uint32_t                iHint;
    /** Number modules. */
    uint32_t                cImports;
    /** Import modules. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    RTCHECKIMPORTMODULE     aImports[RT_FLEXIBLE_ARRAY];
} RTCHECKIMPORTSTATE;
/** Pointer to the import checker state. */
typedef RTCHECKIMPORTSTATE *PRTCHECKIMPORTSTATE;



/**
 * Looks up a symbol/ordinal in the given import module.
 *
 * @returns IPRT status code.
 * @param   pModule             The import module.
 * @param   pszSymbol           The symbol name (NULL if not used).
 * @param   uSymbol             The ordinal (~0 if unused).
 * @param   pValue              Where to return a fake address.
 */
static int QuerySymbolFromImportModule(PRTCHECKIMPORTMODULE pModule, const char *pszSymbol, unsigned uSymbol, PRTLDRADDR pValue)
{
    if (pModule->hLdrMod != NIL_RTLDRMOD)
        return RTLdrGetSymbolEx(pModule->hLdrMod, NULL, _128M, uSymbol, pszSymbol, pValue);

    /*
     * Search the export list.  Ordinal imports are stringified: #<ordinal>
     */
    char szOrdinal[32];
    if (!pszSymbol)
    {
        RTStrPrintf(szOrdinal, sizeof(szOrdinal), "#%u", uSymbol);
        pszSymbol = szOrdinal;
    }

    size_t i = pModule->cExports;
    while (i-- > 0)
        if (strcmp(pModule->papszExports[i], pszSymbol) == 0)
        {
            *pValue = _128M + i*4;
            return VINF_SUCCESS;
        }
    return VERR_SYMBOL_NOT_FOUND;
}


/**
 * @callback_method_impl{FNRTLDRIMPORT}
 */
static DECLCALLBACK(int) GetImportCallback(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol,
                                           unsigned uSymbol, PRTLDRADDR pValue, void *pvUser)
{
    PRTCHECKIMPORTSTATE pState = (PRTCHECKIMPORTSTATE)pvUser;
    int rc;
    NOREF(hLdrMod);

    /*
     * If a module is given, lookup the symbol/ordinal there.
     */
    if (pszModule)
    {
        uint32_t iModule = pState->iHint;
        if (   iModule > pState->cImports
            || strcmp(pState->aImports[iModule].szModule, pszModule) != 0)
        {
            for (iModule = 0; iModule < pState->cImports; iModule++)
                if (strcmp(pState->aImports[iModule].szModule, pszModule) == 0)
                    break;
            if (iModule >= pState->cImports)
                return RTMsgErrorRc(VERR_MODULE_NOT_FOUND, "%s: Failed to locate import module '%s'", pState->pszImage, pszModule);
            pState->iHint = iModule;
        }

        rc = QuerySymbolFromImportModule(&pState->aImports[iModule], pszSymbol, uSymbol, pValue);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else if (rc == VERR_LDR_FORWARDER)
            rc= VINF_SUCCESS;
        else
        {
            if (pszSymbol)
                RTMsgError("%s: Missing import '%s' from '%s'!", pState->pszImage, pszSymbol, pszModule);
            else
                RTMsgError("%s: Missing import #%u from '%s'!", pState->pszImage, uSymbol, pszModule);
            pState->iRc = rc;
            rc = VINF_SUCCESS;
            *pValue = _128M + _4K;
        }
    }
    /*
     * Otherwise we need to scan all modules.
     */
    else
    {
        Assert(pszSymbol);
        uint32_t iModule = pState->iHint;
        if (iModule < pState->cImports)
            rc = QuerySymbolFromImportModule(&pState->aImports[iModule], pszSymbol, uSymbol, pValue);
        else
            rc = VERR_SYMBOL_NOT_FOUND;
        if (rc == VERR_SYMBOL_NOT_FOUND)
        {
            for (iModule = 0; iModule < pState->cImports; iModule++)
            {
                rc = QuerySymbolFromImportModule(&pState->aImports[iModule], pszSymbol, uSymbol, pValue);
                if (rc != VERR_SYMBOL_NOT_FOUND)
                    break;
            }
        }
        if (RT_FAILURE(rc))
        {
            RTMsgError("%s: Missing import '%s'!", pState->pszImage, pszSymbol);
            pState->iRc = rc;
            rc = VINF_SUCCESS;
            *pValue = _128M + _4K;
        }
    }
    return rc;
}


/**
 * Loads an imported module.
 *
 * @returns IPRT status code.
 * @param   pOpts               The check program options.
 * @param   pModule             The import module.
 * @param   pErrInfo            Error buffer (to avoid wasting stack).
 * @param   pszImage            The image we're processing (for error messages).
 */
static int LoadImportModule(PCRTCHECKIMPORTSOPTS pOpts, PRTCHECKIMPORTMODULE pModule, PRTERRINFO pErrInfo, const char *pszImage)

{
    /*
     * Look for real DLLs.
     */
    for (uint32_t iPath = 0; iPath < pOpts->cPaths; iPath++)
    {
        char        szPath[RTPATH_MAX];
        int rc = RTPathJoin(szPath, sizeof(szPath), pOpts->papszPaths[iPath], pModule->szModule);
        if (RT_SUCCESS(rc))
        {
            uint32_t offError;
            RTFSOBJINFO ObjInfo;
            rc = RTVfsChainQueryInfo(szPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK, &offError, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                if (RTFS_IS_FILE(ObjInfo.Attr.fMode))
                {
                    RTLDRMOD hLdrMod;
                    rc = RTLdrOpenVfsChain(szPath, RTLDR_O_FOR_DEBUG, pOpts->enmLdrArch, &hLdrMod, &offError, pErrInfo);
                    if (RT_SUCCESS(rc))
                    {
                        pModule->hLdrMod = hLdrMod;
                        if (pOpts->cVerbosity > 0)
                            RTMsgInfo("Import '%s' -> '%s'\n", pModule->szModule, szPath);
                    }
                    else if (RTErrInfoIsSet(pErrInfo))
                        RTMsgError("%s: Failed opening import image '%s': %Rrc - %s", pszImage, szPath, rc, pErrInfo->pszMsg);
                    else
                        RTMsgError("%s: Failed opening import image '%s': %Rrc", pszImage, szPath, rc);
                    return rc;
                }
            }
            else if (   rc != VERR_PATH_NOT_FOUND
                     && rc != VERR_FILE_NOT_FOUND)
                RTVfsChainMsgError("RTVfsChainQueryInfo", szPath, rc, offError, pErrInfo);

            /*
             * Check for export file.
             */
            RTStrCat(szPath, sizeof(szPath), ".exports");
            RTVFSFILE hVfsFile;
            rc = RTVfsChainOpenFile(szPath, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &hVfsFile, &offError, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                /* Read it into a memory buffer. */
                uint64_t cbFile;
                rc = RTVfsFileQuerySize(hVfsFile, &cbFile);
                if (RT_SUCCESS(rc))
                {
                    if (cbFile < _4M)
                    {
                        char *pszFile = (char *)RTMemAlloc((size_t)cbFile + 1);
                        if (pszFile)
                        {
                            rc = RTVfsFileRead(hVfsFile, pszFile, (size_t)cbFile, NULL);
                            if (RT_SUCCESS(rc))
                            {
                                pszFile[(size_t)cbFile] = '\0';
                                rc = RTStrValidateEncoding(pszFile);
                                if (RT_SUCCESS(rc))
                                {
                                    /*
                                     * Parse it.
                                     */
                                    size_t iLine = 1;
                                    size_t off   = 0;
                                    while (off < cbFile)
                                    {
                                        size_t const offStartLine = off;

                                        /* skip leading blanks */
                                        while (RT_C_IS_BLANK(pszFile[off]))
                                            off++;

                                        char ch = pszFile[off];
                                        if (   ch != ';' /* comment */
                                            && !RT_C_IS_CNTRL(ch))
                                        {
                                            /* find length of symbol */
                                            size_t const offSymbol = off;
                                            while (  (ch = pszFile[off]) != '\0'
                                                   && !RT_C_IS_SPACE(ch))
                                                off++;
                                            size_t const cchSymbol = off - offSymbol;

                                            /* add it. */
                                            if ((pModule->cExports & 127) == 0)
                                            {
                                                void *pvNew = RTMemRealloc(pModule->papszExports,
                                                                           (pModule->cExports + 128) * sizeof(char *));
                                                if (!pvNew)
                                                {
                                                    rc = RTMsgErrorRc(VERR_NO_MEMORY, "%s: %s:%u: out of memory!", pszImage, szPath, iLine);
                                                    break;
                                                }
                                                pModule->papszExports = (char **)pvNew;
                                            }
                                            pModule->papszExports[pModule->cExports] = RTStrDupN(&pszFile[offSymbol], cchSymbol);
                                            if (pModule->papszExports[pModule->cExports])
                                                pModule->cExports++;
                                            else
                                            {
                                                rc = RTMsgErrorRc(VERR_NO_MEMORY, "%s: %s:%u: out of memory!", pszImage, szPath, iLine);
                                                break;
                                            }

                                            /* check what comes next is a comment or end of line/file */
                                            while (RT_C_IS_BLANK(pszFile[off]))
                                                off++;
                                            ch = pszFile[off];
                                            if (   ch != '\0'
                                                && ch != '\n'
                                                && ch != '\r'
                                                && ch != ';')
                                                rc = RTMsgErrorRc(VERR_PARSE_ERROR, "%s: %s:%u: Unexpected text at position %u!",
                                                                  pszImage, szPath, iLine, off - offStartLine);
                                        }

                                        /* advance to the end of the the line */
                                        while (  (ch = pszFile[off]) != '\0'
                                               && ch != '\n')
                                            off++;
                                        off++;
                                        iLine++;
                                    }

                                    if (pOpts->cVerbosity > 0)
                                        RTMsgInfo("Import '%s' -> '%s' (%u exports)\n",
                                                  pModule->szModule, szPath, pModule->cExports);
                                }
                                else
                                    RTMsgError("%s: %s: Invalid UTF-8 encoding in export file: %Rrc", pszImage, szPath, rc);
                            }
                            RTMemFree(pszFile);
                        }
                        else
                            rc = RTMsgErrorRc(VERR_NO_MEMORY, "%s: %s: Out of memory reading export file (%#RX64 bytes)",
                                              pszImage, szPath, cbFile + 1);
                    }
                    else
                        rc = RTMsgErrorRc(VERR_NO_MEMORY, "%s: %s: Export file is too big: %#RX64 bytes, max 4MiB",
                                          pszImage, szPath, cbFile);
                }
                else
                    RTMsgError("%s: %s: RTVfsFileQuerySize failed on export file: %Rrc", pszImage, szPath, rc);
                RTVfsFileRelease(hVfsFile);
                return rc;
            }
            else if (   rc != VERR_PATH_NOT_FOUND
                     && rc != VERR_FILE_NOT_FOUND)
                RTVfsChainMsgError("RTVfsChainOpenFile", szPath, rc, offError, pErrInfo);
        }
    }

    return RTMsgErrorRc(VERR_MODULE_NOT_FOUND, "%s: Import module '%s' was not found!", pszImage, pModule->szModule);
}


/**
 * Checks the imports for the given image.
 *
 * @returns IPRT status code.
 * @param   pOpts               The check program options.
 * @param   pszImage            The image to check.
 */
static int rtCheckImportsForImage(PCRTCHECKIMPORTSOPTS pOpts, const char *pszImage)
{
    if (pOpts->cVerbosity > 0)
        RTMsgInfo("Checking '%s'...\n", pszImage);

    /*
     * Open the image.
     */
    uint32_t        offError;
    RTERRINFOSTATIC ErrInfo;
    RTLDRMOD        hLdrMod;
    int rc = RTLdrOpenVfsChain(pszImage, RTLDR_O_FOR_DEBUG, RTLDRARCH_WHATEVER,
                               &hLdrMod, &offError, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
    {
        if (RT_FAILURE(rc) && RTErrInfoIsSet(&ErrInfo.Core))
            return RTMsgErrorRc(rc, "Failed opening image '%s': %Rrc - %s", pszImage, rc, ErrInfo.Core.pszMsg);
        return RTMsgErrorRc(rc, "Failed opening image '%s': %Rrc", pszImage, rc);
    }

    /*
     * Do the import modules first.
     */
    uint32_t cImports = 0;
    rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_IMPORT_COUNT, &cImports, sizeof(cImports));
    if (RT_SUCCESS(rc))
    {
        RTCHECKIMPORTSTATE *pState = (RTCHECKIMPORTSTATE *)RTMemAllocZ(RT_UOFFSETOF_DYN(RTCHECKIMPORTSTATE, aImports[cImports + 1]));
        if (pState)
        {
            pState->pszImage = pszImage;
            pState->pOpts    = pOpts;
            pState->cImports = cImports;
            for (uint32_t iImport = 0; iImport < cImports; iImport++)
                pState->aImports[iImport].hLdrMod = NIL_RTLDRMOD;

            for (uint32_t iImport = 0; iImport < cImports; iImport++)
            {
                *(uint32_t *)&pState->aImports[iImport].szModule[0] = iImport;
                rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_IMPORT_MODULE, pState->aImports[iImport].szModule,
                                    sizeof(pState->aImports[iImport].szModule));
                if (RT_FAILURE(rc))
                {
                    RTMsgError("%s: Error querying import #%u: %Rrc", pszImage, iImport, rc);
                    break;
                }
                rc = LoadImportModule(pOpts, &pState->aImports[iImport], &ErrInfo.Core, pszImage);
                if (RT_FAILURE(rc))
                    break;
            }
            if (RT_SUCCESS(rc))
            {
                /*
                 * Get the image bits, indirectly resolving imports.
                 */
                size_t cbImage = RTLdrSize(hLdrMod);
                void  *pvImage = RTMemAllocZ(cbImage);
                if (pvImage)
                {
                    pState->iRc = VINF_SUCCESS;
                    rc = RTLdrGetBits(hLdrMod, pvImage, _4M, GetImportCallback, pState);
                    if (RT_SUCCESS(rc))
                        rc = pState->iRc;
                    else
                        RTMsgError("%s: RTLdrGetBits failed: %Rrc", pszImage, rc);

                    RTMemFree(pvImage);
                }
                else
                    rc = RTMsgErrorRc(VERR_NO_MEMORY, "%s: out of memory", pszImage);
            }

            for (uint32_t iImport = 0; iImport < cImports; iImport++)
                if (pState->aImports[iImport].hLdrMod != NIL_RTLDRMOD)
                {
                    RTLdrClose(pState->aImports[iImport].hLdrMod);

                    size_t i = pState->aImports[iImport].cExports;
                    while (i-- > 0)
                        RTStrFree(pState->aImports[iImport].papszExports[i]);
                    RTMemFree(pState->aImports[iImport].papszExports);
                }
            RTMemFree(pState);
        }
        else
            rc = RTMsgErrorRc(VERR_NO_MEMORY, "%s: out of memory", pszImage);
    }
    else
        RTMsgError("%s: Querying RTLDRPROP_IMPORT_COUNT failed: %Rrc", pszImage, rc);
    RTLdrClose(hLdrMod);
    return rc;
}


/**
 * @callback_method_impl{FNRTLDRENUMSYMS}
 */
static DECLCALLBACK(int) PrintSymbolForExportList(RTLDRMOD hLdrMod, const char *pszSymbol, unsigned uSymbol,
                                                  RTLDRADDR Value, void *pvUser)
{
    if (pszSymbol)
        RTPrintf("%s\n", pszSymbol);
    if (uSymbol != ~(unsigned)0 && (!pszSymbol || ((PCRTCHECKIMPORTSOPTS)pvUser)->fListOrdinals))
        RTPrintf("#%u\n", uSymbol);
    RT_NOREF(hLdrMod, Value, pvUser);
    return VINF_SUCCESS;
}


/**
 * Produces the export list for the given image.
 *
 * @returns IPRT status code.
 * @param   pOpts               The check program options.
 * @param   pszImage            Path to the image.
 */
static int ProduceExportList(PCRTCHECKIMPORTSOPTS pOpts, const char *pszImage)
{
    /*
     * Open the image.
     */
    uint32_t        offError;
    RTERRINFOSTATIC ErrInfo;
    RTLDRMOD        hLdrMod;
    int rc = RTLdrOpenVfsChain(pszImage, RTLDR_O_FOR_DEBUG, RTLDRARCH_WHATEVER, &hLdrMod, &offError, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        /*
         * Some info about the file.
         */
        RTPrintf(";\n"
                 "; Generated from: %s\n", pszImage);

        RTFSOBJINFO ObjInfo;
        rc = RTVfsChainQueryInfo(pszImage, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK, NULL, NULL);
        if (RT_SUCCESS(rc))
            RTPrintf(";      Size file: %#RX64 (%RU64)\n", ObjInfo.cbObject, ObjInfo.cbObject);

        switch (RTLdrGetFormat(hLdrMod))
        {
            case RTLDRFMT_AOUT:     RTPrintf(";         Format: a.out\n"); break;
            case RTLDRFMT_ELF:      RTPrintf(";         Format: ELF\n"); break;
            case RTLDRFMT_LX:       RTPrintf(";         Format: LX\n"); break;
            case RTLDRFMT_MACHO:    RTPrintf(";         Format: Mach-O\n"); break;
            case RTLDRFMT_PE:       RTPrintf(";         Format: PE\n"); break;
            default:                RTPrintf(";         Format: %u\n", RTLdrGetFormat(hLdrMod)); break;

        }

        RTPrintf(";  Size of image: %#x (%u)\n", RTLdrSize(hLdrMod), RTLdrSize(hLdrMod));

        switch (RTLdrGetArch(hLdrMod))
        {
            case RTLDRARCH_AMD64:   RTPrintf(";   Architecture: AMD64\n"); break;
            case RTLDRARCH_X86_32:  RTPrintf(";   Architecture: X86\n"); break;
            default:                RTPrintf(";   Architecture: %u\n", RTLdrGetArch(hLdrMod)); break;
        }

        uint64_t uTimestamp;
        rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_TIMESTAMP_SECONDS, &uTimestamp, sizeof(uTimestamp));
        if (RT_SUCCESS(rc))
        {
            RTTIMESPEC Timestamp;
            char       szTime[128];
            RTTimeSpecToString(RTTimeSpecSetSeconds(&Timestamp, uTimestamp), szTime, sizeof(szTime));
            char *pszEnd = strchr(szTime, '\0');
            while (pszEnd[0] != '.')
                pszEnd--;
            *pszEnd = '\0';
            RTPrintf(";      Timestamp: %#RX64 - %s\n", uTimestamp, szTime);
        }

        RTUUID ImageUuid;
        rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_UUID, &ImageUuid, sizeof(ImageUuid));
        if (RT_SUCCESS(rc))
            RTPrintf(";           UUID: %RTuuid\n", &ImageUuid);

        RTPrintf(";\n");

        /*
         * The list of exports.
         */
        rc = RTLdrEnumSymbols(hLdrMod, 0 /*fFlags*/, NULL, _4M, PrintSymbolForExportList, (void *)pOpts);
        if (RT_FAILURE(rc))
            RTMsgError("%s: RTLdrEnumSymbols failed: %Rrc", pszImage, rc);

        /* done */
        RTLdrClose(hLdrMod);
    }
    else if (RTErrInfoIsSet(&ErrInfo.Core))
        RTMsgError("Failed opening image '%s': %Rrc - %s", pszImage, rc, ErrInfo.Core.pszMsg);
    else
        RTMsgError("Failed opening image '%s': %Rrc", pszImage, rc);
    return rc;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    RTCHECKIMPORTSOPTS Opts;
    Opts.cPaths        = 0;
    Opts.papszPaths    = NULL;
    Opts.enmLdrArch    = RTLDRARCH_WHATEVER;
    Opts.cVerbosity    = 1;
    Opts.fListOrdinals = false;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--path", 'p', RTGETOPT_REQ_STRING },
        { "--export", 'e', RTGETOPT_REQ_STRING },
        { "--list-ordinals", 'O', RTGETOPT_REQ_NOTHING },
        { "--quiet", 'q', RTGETOPT_REQ_NOTHING },
        { "--verbose", 'v', RTGETOPT_REQ_NOTHING },
    };
    RTGETOPTSTATE State;
    rc = RTGetOptInit(&State, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);

    RTEXITCODE    rcExit = RTEXITCODE_SUCCESS;
    RTGETOPTUNION ValueUnion;
    while ((rc = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (rc)
        {
            case 'p':
                if ((Opts.cPaths % 16) == 0)
                {
                    void *pvNew = RTMemRealloc(Opts.papszPaths, sizeof(Opts.papszPaths[0]) * (Opts.cPaths + 16));
                    AssertRCReturn(rc, RTEXITCODE_FAILURE);
                    Opts.papszPaths = (char **)pvNew;
                }
                Opts.papszPaths[Opts.cPaths] = RTStrDup(ValueUnion.psz);
                AssertReturn(Opts.papszPaths[Opts.cPaths], RTEXITCODE_FAILURE);
                Opts.cPaths++;
                break;

            case 'e':
                rc = ProduceExportList(&Opts, ValueUnion.psz);
                if (RT_FAILURE(rc))
                    rcExit = RTEXITCODE_FAILURE;
                break;

            case 'O':
                Opts.fListOrdinals = true;
                break;

            case 'q':
                Opts.cVerbosity = 0;
                break;

            case 'v':
                Opts.cVerbosity = 0;
                break;

            case VINF_GETOPT_NOT_OPTION:
                rc = rtCheckImportsForImage(&Opts, ValueUnion.psz);
                if (RT_FAILURE(rc))
                    rcExit = RTEXITCODE_FAILURE;
                break;

            case 'h':
                RTPrintf("Usage: RTCheckImports [-p|--path <dir>] [-v|--verbose] [-q|--quiet] <image [..]>\n"
                         "   or: RTCheckImports -e <image>\n"
                         "   or: RTCheckImports <-h|--help>\n"
                         "   or: RTCheckImports <-V|--version>\n"
                         "Checks library imports. VFS chain syntax supported.\n"
                         "\n"
                         "Options:\n"
                         "  -p, --path <dir>\n"
                         "    Search the specified directory for imported modules or their export lists.\n"
                         "  -e, --export <image>\n"
                         "    Write export list for the file to stdout.  (Redirect to a .export file.)\n"
                         "  -O, --list-ordinals\n"
                         "    Whether to list ordinals as well as names in the export list.\n"
                         "  -q, --quiet\n"
                         "    Quiet execution.\n"
                         "  -v, --verbose\n"
                         "    Increases verbosity.\n"
                         ""
                         );
                return RTEXITCODE_SUCCESS;

#ifndef IPRT_IN_BUILD_TOOL
            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return RTEXITCODE_SUCCESS;
#endif

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    return rcExit;
}

