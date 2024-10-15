/* $Id: VBoxBs3Linker.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Validation Kit - Boot Sector 3 "linker".
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
#include <stdio.h>
#include <stdlib.h>

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include "bs3kit-linker.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct BS3LNKINPUT
{
    const char     *pszFile;
    FILE           *pFile;
    uint32_t        cbFile;
    uint32_t        cbBits;
    uint32_t        uLoadAddr;
    uint32_t        offInImage;
    uint8_t        *pbBits;
    RTLDRMOD        hLdrMod;
} BS3LNKINPUT;


#define BS3LNK_MAX_SEGMENTS 24

typedef struct BS3LNKIMPORTSTATE
{
    FILE           *pOutput;
    RTSTRSPACE      hImportNames;
    unsigned        cImports;
    unsigned        cExports;
    size_t          cbStrings;
    unsigned        cSegments;
    uint8_t        *pbBits;
    size_t          cbBits;
    BS3HIGHDLLSEGMENT aSegments[BS3LNK_MAX_SEGMENTS];
} BS3LNKIMPORTSTATE;

typedef struct BS3LNKIMPORTNAME
{
    RTSTRSPACECORE  Core;
    size_t          offString;
    RT_FLEXIBLE_ARRAY_EXTENSION
    char            szName[RT_FLEXIBLE_ARRAY];
} BS3LNKIMPORTNAME;



/**
 * @callback_method_impl{FNRTLDRENUMSEGS,
 *  Construct a BS3LNKIMPORTSTATE::aSegments entry and adds it to the assembly.}
 */
static DECLCALLBACK(int) GenerateHighDllAsmOutputSegmentTable(RTLDRMOD hLdrMod, PCRTLDRSEG pSeg, void *pvUser)
{
    BS3LNKIMPORTSTATE * const pState = (BS3LNKIMPORTSTATE *)pvUser;
    RT_NOREF(hLdrMod);

    uint32_t const iSegment = pState->cSegments;
    AssertReturn(iSegment < RT_ELEMENTS(pState->aSegments), VERR_OUT_OF_RANGE);
    pState->cSegments++;

    /* Address and size. */
    if (pSeg->cbMapped != NIL_RTLDRADDR)
    {
        pState->aSegments[iSegment].uAddr = (uint32_t)(pSeg->RVA + BS3HIGHDLL_LOAD_ADDRESS);
        pState->aSegments[iSegment].cb    = (uint32_t)pSeg->cbMapped;
    }
    else
    {
        pState->aSegments[iSegment].uAddr = UINT32_MAX;
        pState->aSegments[iSegment].cb    = 0;
    }

    /* The selector is just the segment index at this point. We'll resolve it during linking. */
    pState->aSegments[iSegment].idxSel = (uint16_t)iSegment;

    /* Determine the flags. */
    pState->aSegments[iSegment].fFlags = 0;
    if (pSeg->fProt & RTMEM_PROT_EXEC)
        pState->aSegments[iSegment].fFlags |= BS3HIGHDLLSEGMENT_F_EXEC;
    if (pSeg->fFlags & RTLDRSEG_FLAG_OS2_CONFORM)
        pState->aSegments[iSegment].fFlags |= BS3HIGHDLLSEGMENT_F_CONFORMING;
    if (pSeg->fFlags & RTLDRSEG_FLAG_16BIT)
        pState->aSegments[iSegment].fFlags |= BS3HIGHDLLSEGMENT_F_16BIT;
    else if (pSeg->RVA != NIL_RTLDRADDR)
    {
        /* Have to check the eyecatcher string to see if it's a 32-bit or 64-bit segment. */
        Assert(pSeg->RVA + 16 < pState->cbBits);
        char * const pchSeg    = (char *)&pState->pbBits[pSeg->RVA];
        char const   chSaved32 = pchSeg[32];
        pchSeg[32] = '\0';
        if (RTStrStr(pchSeg, "32"))
            pState->aSegments[iSegment].fFlags |= BS3HIGHDLLSEGMENT_F_32BIT;
        else if (RTStrStr(pchSeg, "64"))
            pState->aSegments[iSegment].fFlags |= BS3HIGHDLLSEGMENT_F_64BIT;
        pchSeg[32] = chSaved32;
    }
    //if (pSeg->fFlags & RTLDRSEG_FLAG_OS2_ALIAS16) /* 32-bit or 64-bit segment, but allocate a 16-bit alias to it. no wlink sup.  */
    //    pState->aSegments[iSegment].fFlags |= BS3HIGHDLLSEGMENT_F_16BIT;


    // BS3HIGHDLLSEGMENT
    fprintf(pState->pOutput,
            "        dd      %#010x ; %u - %.*s\n"
            "        dd      %#010x\n"
            "        dw      %#x\n"
            "        dw      %#x\n",
            pState->aSegments[iSegment].uAddr, iSegment, pSeg->cchName, pSeg->pszName,
            pState->aSegments[iSegment].cb,
            pState->aSegments[iSegment].idxSel,
            pState->aSegments[iSegment].fFlags);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNRTLDRENUMSYMS, Outputs an export table entry.}
 */
static DECLCALLBACK(int) GenerateHighDllAsmOutputExportTable(RTLDRMOD hLdrMod, const char *pszSymbol, unsigned uSymbol,
                                                             RTLDRADDR Value, void *pvUser)
{
    BS3LNKIMPORTSTATE * const pState = (BS3LNKIMPORTSTATE *)pvUser;
    if (!pszSymbol || !*pszSymbol)
        return RTMsgErrorRc(VERR_LDR_BAD_FIXUP, "All exports must be by name. uSymbol=%#x Value=%RX64", uSymbol, (uint64_t)Value);

    /* Translate it to segment+offset. We popuplate the table with indexes
       here, later we'll replace them with BS3Kit selector values once
       these are handed out during the linking phase. */
    uint32_t  iSeg   = UINT32_MAX;
    RTLDRADDR offSeg = NIL_RTLDRADDR;
    int rc = RTLdrRvaToSegOffset(hLdrMod, Value - BS3HIGHDLL_LOAD_ADDRESS, &iSeg, &offSeg);
    if (RT_SUCCESS(rc))
    {
        if (iSeg >= pState->cSegments || offSeg >= pState->aSegments[iSeg].cb)
            return RTMsgErrorRc(VERR_ADDRESS_TOO_BIG, "Bogus segment + offset translation of '%s' at %#RX64: %x:%RX64",
                                pszSymbol, (uint64_t)Value, iSeg, (uint64_t)offSeg);
    }
    else
        return RTMsgErrorRc(rc, "Failed to translate '%s' at %#RX64 into segment + offset: %Rrc", pszSymbol, (uint64_t)Value);

    // BS3HIGHDLLEXPORTENTRY
    const char *pszCSymbol = *pszSymbol == '_' ? &pszSymbol[1] : pszSymbol;
    if (pState->aSegments[iSeg].fFlags & BS3HIGHDLLSEGMENT_F_EXEC)
        fprintf(pState->pOutput,
                "BS3_GLOBAL_DATA g_pfn%s, 8\n"
                "        dd      0\n"
                "        dd      0\n"
                "BS3_GLOBAL_DATA g_fpfn48%s, 6\n"
                "        dd      %#010x\n"
                "        dw      %#06x\n"
                "        dw      %#08x\n",
                pszCSymbol,
                pszCSymbol,
                (uint32_t)offSeg,
                (uint16_t)iSeg,
                (unsigned)pState->cbStrings);
    else
        fprintf(pState->pOutput,
                "BS3_GLOBAL_DATA g_p%s, 8\n"
                "        dd      0\n"
                "        dd      0\n"
                "BS3_GLOBAL_DATA g_fp48%s, 6\n"
                "        dd      %#010x\n"
                "        dw      %#06x\n"
                "        dw      %#08x\n",
                pszCSymbol,
                pszCSymbol,
                (uint32_t)offSeg,
                (uint16_t)iSeg,
                (unsigned)pState->cbStrings);

    pState->cbStrings += strlen(pszSymbol) + 1;
    pState->cExports  += 1;

    RT_NOREF(hLdrMod);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Outputs an import table entry.}
 */
static DECLCALLBACK(int) GenerateHighDllAsmOutputImportTable(PRTSTRSPACECORE pStr, void *pvUser)
{
    FILE             *pOutput = (FILE *)pvUser;
    BS3LNKIMPORTNAME *pName   = (BS3LNKIMPORTNAME *)pStr;

    // BS3HIGHDLLIMPORTENTRY
    fprintf(pOutput,
            "        dw      %#06x\n"
            "        dw      seg %s\n"
            "        dd      %s\n"
            "        dd      %s wrt BS3FLAT\n"
            , (unsigned)pName->offString, pName->szName, pName->szName, pName->szName);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNRTLDRENUMSYMS, Outputs export name string.}
 */
static DECLCALLBACK(int) GenerateHighDllAsmOutputExportStrings(RTLDRMOD hLdrMod, const char *pszSymbol, unsigned uSymbol,
                                                               RTLDRADDR Value, void *pvUser)
{
    BS3LNKIMPORTSTATE * const pState = (BS3LNKIMPORTSTATE *)pvUser;
    if (!pszSymbol || !*pszSymbol)
        return RTMsgErrorRc(VERR_LDR_BAD_FIXUP, "All exports must be by name. uSymbol=%#x Value=%RX64", uSymbol, (uint64_t)Value);

    fprintf(pState->pOutput, "        db      '%s', 0\n", pszSymbol);
    pState->cbStrings += strlen(pszSymbol) + 1;

    RT_NOREF(hLdrMod);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Outputs import name string.}
 */
static DECLCALLBACK(int) GenerateHighDllAsmOutputImportStrings(PRTSTRSPACECORE pStr, void *pvUser)
{
    BS3LNKIMPORTSTATE * const pState = (BS3LNKIMPORTSTATE *)pvUser;
    BS3LNKIMPORTNAME  * const pName   = (BS3LNKIMPORTNAME *)pStr;

    pName->offString = pState->cbStrings;
    fprintf(pState->pOutput, "        db      '%s', 0\n", pName->szName);
    pState->cbStrings += pName->Core.cchString + 1;

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNRTLDRIMPORT, Adds import to the import strings.}
 *
 * Since LX doesn't have a single import table as such, we collect imported in a
 * string space while doing RTLdrGetBits.
 */
static DECLCALLBACK(int) GenerateHighDllAsmImportCallback(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol,
                                                          unsigned uSymbol, PRTLDRADDR pValue, void *pvUser)
{
    BS3LNKIMPORTSTATE *pState = (BS3LNKIMPORTSTATE *)pvUser;
    if (!pszSymbol)
        return RTMsgErrorRc(VERR_LDR_BAD_FIXUP, "All imports must be by name. pszModule=%s uSymbol=%#x", pszModule, uSymbol);
    if (!RTStrSpaceGet(&pState->hImportNames, pszSymbol))
    {
        size_t const           cchSymbol = strlen(pszSymbol);
        BS3LNKIMPORTNAME * const pName   = (BS3LNKIMPORTNAME *)RTMemAlloc(RT_UOFFSETOF_DYN(BS3LNKIMPORTNAME,
                                                                                           szName[cchSymbol + 1]));
        AssertReturn(pName, VERR_NO_MEMORY);

        pName->Core.cchString = cchSymbol;
        pName->Core.pszString = (char *)memcpy(pName->szName, pszSymbol, cchSymbol + 1);
        pName->offString      = UINT16_MAX;

        AssertReturnStmt(RTStrSpaceInsert(&pState->hImportNames, &pName->Core), RTMemFree(pName),
                         RTMsgErrorRc(VERR_INTERNAL_ERROR, "IPE #1"));
        pState->cImports++;
    }
    *pValue = 0x10042;
    RT_NOREF(hLdrMod);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNRTLDRENUMSEGS, For counting segments.}
 */
static DECLCALLBACK(int) GenerateHighDllAsmCountSegments(RTLDRMOD hLdrMod, PCRTLDRSEG pSeg, void *pvUser)
{
    RT_NOREF(hLdrMod, pSeg);
    *(uint32_t *)pvUser += 1;
    return VINF_SUCCESS;
}


/**
 * Main worker function for --generate-high-dll-import-table.
 *
 * @returns Exit status.
 * @param   pOutput         The assembly output file.
 * @param   pszGenAsmFor    The name of the DLL to generate info for.
 */
static RTEXITCODE GenerateHighDllImportTableAssembly(FILE *pOutput, const char *pszGenAsmFor)
{
    RTERRINFOSTATIC ErrInfo;
    RTLDRMOD        hLdrMod;
    int rc = RTLdrOpenEx(pszGenAsmFor, 0, RTLDRARCH_X86_32, &hLdrMod, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTLdrOpenEx failed to open '%s': %Rrc%#RTeim", pszGenAsmFor, rc, &ErrInfo.Core);

    RTEXITCODE rcExit;
    size_t cbImage = RTLdrSize(hLdrMod);
    if (cbImage != ~(size_t)0)
    {
        uint32_t cSegments = 0;
        rc = RTLdrEnumSegments(hLdrMod, GenerateHighDllAsmCountSegments, &cSegments);
        if (RT_SUCCESS(rc) && cSegments >= 1 && cSegments <= BS3LNK_MAX_SEGMENTS)
        {
            uint8_t *pbBits = (uint8_t *)RTMemAlloc(cbImage);
            if (pbBits)
            {
                BS3LNKIMPORTSTATE State = { pOutput, NULL, 0, 0, 0, 0, pbBits, cbImage };
                rc = RTLdrGetBits(hLdrMod, pbBits, BS3HIGHDLL_LOAD_ADDRESS, GenerateHighDllAsmImportCallback, pOutput);
                if (RT_SUCCESS(rc))
                {
                    /** @todo move more of this to bs3kit*.h?    */
                    fprintf(pOutput,
                            ";\n"
                            "; Automatically generated - DO NOT MODIFY!\n"
                            ";\n"
                            "%%include \"bs3kit.mac\"\n"
                            "\n"
                            "section BS3HIGHDLLSEGMENTS  align=4 CLASS=BS3HIGHDLLCLASS PUBLIC USE32 FLAT\n"
                            "section BS3HIGHDLLEXPORTS   align=4 CLASS=BS3HIGHDLLCLASS PUBLIC USE32 FLAT\n"
                            "section BS3HIGHDLLIMPORTS   align=4 CLASS=BS3HIGHDLLCLASS PUBLIC USE32 FLAT\n"
                            "section BS3HIGHDLLSTRINGS   align=4 CLASS=BS3HIGHDLLCLASS PUBLIC USE32 FLAT\n"
                            "section BS3HIGHDLLTABLE     align=4 CLASS=BS3HIGHDLLCLASS PUBLIC USE32 FLAT\n"
                            "section BS3HIGHDLLTABLE_END align=4 CLASS=BS3HIGHDLLCLASS PUBLIC USE32 FLAT\n"
                            "GROUP BS3HIGHDLLGROUP  BS3HIGHDLLSEGMENTS BS3HIGHDLLEXPORTS BS3HIGHDLLIMPORTS BS3HIGHDLLSTRINGS BS3HIGHDLLTABLE BS3HIGHDLLTABLE_END\n"
                            "\n");

                    /* Populate the string table with imports. */
                    const char *pszFilename = RTPathFilename(pszGenAsmFor);
                    fprintf(pOutput,
                            "section BS3HIGHDLLSTRINGS\n"
                            "start_strings:\n"
                            "        db      0\n"
                            "        db      '%s', 0    ; module name\n"
                            "        ; imports\n",
                            pszFilename);
                    State.cbStrings = 1 + strlen(pszFilename) + 1;
                    rc = RTStrSpaceEnumerate(&State.hImportNames, GenerateHighDllAsmOutputImportStrings, &State);
                    AssertRC(rc);
                    fprintf(pOutput, "        ; exports\n");

                    /* Populate the string table with exports. */
                    size_t const offExportStrings = State.cbStrings;
                    rc = RTLdrEnumSymbols(hLdrMod, 0, pbBits, BS3HIGHDLL_LOAD_ADDRESS, GenerateHighDllAsmOutputExportStrings, &State);
                    size_t const cbStrings        = State.cbStrings;
                    if (RT_SUCCESS(rc) && cbStrings < _64K)
                    {
                        rcExit = RTEXITCODE_SUCCESS;

                        /* Output the import table. */
                        fprintf(pOutput,
                                "section BS3HIGHDLLIMPORTS\n"
                                "start_imports:\n");
                        rc = RTStrSpaceEnumerate(&State.hImportNames, GenerateHighDllAsmOutputImportTable, &State);
                        AssertRCStmt(rc, rcExit = RTEXITCODE_FAILURE);
                        fprintf(pOutput, "\n");

                        /* Output the segment table (before exports, so we get the segment info). */
                        fprintf(pOutput,
                                "section BS3HIGHDLLSEGMENTS\n"
                                "start_segments:\n");
                        rc = RTLdrEnumSegments(hLdrMod, GenerateHighDllAsmOutputSegmentTable, &State);
                        AssertRCStmt(rc, rcExit = RTEXITCODE_FAILURE);

                        /* Output the export table (ASSUMES stable enumeration order). */
                        fprintf(pOutput,
                                "section BS3HIGHDLLEXPORTS\n"
                                "start_exports:\n");
                        State.cbStrings = offExportStrings;
                        rc = RTLdrEnumSymbols(hLdrMod, 0, pbBits, BS3HIGHDLL_LOAD_ADDRESS, GenerateHighDllAsmOutputExportTable, &State);
                        AssertRCStmt(rc, rcExit = RTEXITCODE_FAILURE);
                        fprintf(pOutput, "\n");

                        /* Generate the table entry. */
                        fprintf(pOutput,
                                "section BS3HIGHDLLTABLE\n"
                                "start_entry: ; struct BS3HIGHDLLENTRY\n"
                                "        db      '%s', 0    ; achMagic[8]\n"
                                "        dd      0               ; uLoadAddress\n"
                                "        dd      %#08zx        ; cbLoaded\n"
                                "        dd      0               ; offInImage\n"
                                "        dd      %#08zx        ; cbInImage\n"
                                "        dd      %#04x            ; cImports\n"
                                "        dd      start_imports  - start_entry\n"
                                "        dd      %#04x            ; cExports\n"
                                "        dd      start_exports  - start_entry\n"
                                "        dd      %#04x            ; cSegments\n"
                                "        dd      start_segments - start_entry\n"
                                "        dd      %#05x           ; cbStrings\n"
                                "        dd      start_strings  - start_entry\n"
                                "        dd      1               ; offDllName\n"
                                "        dd      0               ; uChecksum\n"
                                , BS3HIGHDLLENTRY_MAGIC, cbImage, cbImage,
                                State.cImports, State.cExports, State.cSegments,
                                (unsigned)cbStrings);
                    }
                    else if (RT_FAILURE(rc))
                        rcExit = RTMsgErrorExitFailure("RTLdrEnumSymbols failed: %Rrc", rc);
                    else
                        rcExit = RTMsgErrorExitFailure("Too many import/export strings: %#x bytes, max 64KiB", cbStrings);
                }
                else
                    rcExit = RTMsgErrorExitFailure("RTLdrGetBits failed: %Rrc", rc);
                RTMemFree(pbBits);
            }
            else
                rcExit = RTMsgErrorExitFailure("Out of memory!");
        }
        else if (RT_FAILURE(rc))
            rcExit = RTMsgErrorExitFailure("RTLdrEnumSegment failed: %Rrc", rc);
        else
            rcExit = RTMsgErrorExitFailure("Bogus segment count: %#x (min 1, max 24)\n", cSegments);
    }
    else
        rcExit = RTMsgErrorExitFailure("RTLdrSize failed on '%s'", pszGenAsmFor);

    RTLdrClose(hLdrMod);
    return rcExit;
}


static BS3HIGHDLLENTRY *LocateHighDllEntry(uint8_t const *pbBits, uint32_t cbBits, const char *pszFilename)
{
    /*
     * We search backwards for up to 4 KB.
     */
    size_t const offStop = cbBits > _4K ? cbBits - _4K : 0;
    size_t       off     = cbBits >= sizeof(BS3HIGHDLLENTRY) ? cbBits - sizeof(BS3HIGHDLLENTRY) : 0;
    while (off > offStop)
    {
        BS3HIGHDLLENTRY const *pEntry = (BS3HIGHDLLENTRY const *)&pbBits[off];
        if (   pEntry->achMagic[0] == BS3HIGHDLLENTRY_MAGIC[0]
            && memcmp(pEntry->achMagic, BS3HIGHDLLENTRY_MAGIC, sizeof(pEntry->achMagic)) == 0)
        {
            if (pEntry->cbStrings < _64K && pEntry->cbStrings >= 8)
            {
                if (off + pEntry->offStrings > 0 && off + pEntry->offStrings + pEntry->cbStrings <= off)
                {
                    if (off + pEntry->offExports > 0 && off + pEntry->offExports + pEntry->cExports * 8 <= off)
                    {
                        if (off + pEntry->offImports > 0 && off + pEntry->offImports + pEntry->cImports * 8 <= off)
                        {
                            if (pEntry->offFilename > 0 && pEntry->offFilename < pEntry->cbStrings)
                            {
                                const char *psz = (const char *)&pbBits[off + pEntry->offStrings + pEntry->offFilename];
                                if (strcmp(pszFilename, psz) == 0)
                                    return (BS3HIGHDLLENTRY *)pEntry;
                            }
                        }
                    }
                }
            }
        }
        off--;
    }
    RTMsgError("Failed to find the BS3HIGHDLLENTRY structure for '%s'!", pszFilename);
    return NULL;
}


/**
 * @callback_method_impl{FNRTLDRIMPORT}
 */
static DECLCALLBACK(int) ResolveHighDllImportCallback(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol,
                                                      unsigned uSymbol, PRTLDRADDR pValue, void *pvUser)
{
    BS3HIGHDLLENTRY * const pEntry = (BS3HIGHDLLENTRY *)pvUser;
    if (!pszSymbol)
        return RTMsgErrorRc(VERR_LDR_BAD_FIXUP, "All imports must be by name. pszModule=%s uSymbol=%#x", pszModule, uSymbol);

    /* Search the import table: */
    BS3HIGHDLLIMPORTENTRY const *paImports   = (BS3HIGHDLLIMPORTENTRY const *)((uintptr_t)pEntry + pEntry->offImports);
    const char * const           pszzStrings = (const char *)((uintptr_t)pEntry + pEntry->offStrings);
    size_t i = pEntry->cImports;
    while (i-- > 0)
    {
        if (strcmp(pszSymbol, &pszzStrings[paImports[i].offName]) == 0)
        {
            /** @todo the import interface isn't good enough for segmented fixups like LX
             *        uses. So we need to fix that at some point... */
            *pValue = paImports[i].offFlat;
            return VINF_SUCCESS;
        }
    }
    RT_NOREF(hLdrMod);
    return RTMsgErrorRc(VERR_SYMBOL_NOT_FOUND, "Unable to locate import %s (in %s)!", pszSymbol, pszModule);
}


/**
 * Main worker for linking a floppy image.
 */
static RTEXITCODE DoTheLinking(FILE *pOutput, BS3LNKINPUT *paInputs, unsigned cInputs)
{
    if (cInputs < 2)
        return RTMsgErrorExitFailure("Require at least two input files when linking!");

    /*
     * Read all the files into memory.
     *
     * The first two are binary blobs, i.e. the boot sector and the base image.
     * Any additional files are DLLs and we need to do linking.
     */
    uint32_t uHiLoadAddr  = BS3HIGHDLL_LOAD_ADDRESS;
    uint16_t idxHi16BitCs = 0;
    uint16_t idxHi16BitDs = 0;
    uint32_t off          = 0;
    for (unsigned i = 0; i < cInputs; i++)
    {
        paInputs[i].offInImage = off;
        if (i < 2)
        {
            /* Bootsector or the base image. */
            paInputs[i].cbBits = RT_ALIGN_32(paInputs[i].cbFile, 512);
            paInputs[i].pbBits = (uint8_t *)RTMemAllocZ(paInputs[i].cbBits);
            if (!paInputs[i].pbBits)
                return RTMsgErrorExitFailure("Out of memory (%#x)\n", paInputs[i].cbBits);
            size_t cbRead = fread(paInputs[i].pbBits, sizeof(uint8_t), paInputs[i].cbFile, paInputs[i].pFile);
            if (cbRead != paInputs[i].cbFile)
                return RTMsgErrorExitFailure("Error reading '%s' (got %d bytes, wanted %u).",
                                             paInputs[i].pszFile, (int)cbRead, (unsigned)paInputs[i].cbFile);
            paInputs[i].uLoadAddr = i == 90 ? 0x7c00 : 0x10000;
        }
        else
        {
            /*
             * A DLL that will be loaded above 1MB.
             */
            RTERRINFOSTATIC ErrInfo;
            int rc = RTLdrOpenEx(paInputs[i].pszFile, 0, RTLDRARCH_X86_32, &paInputs[i].hLdrMod, RTErrInfoInitStatic(&ErrInfo));
            if (RT_FAILURE(rc))
                return RTMsgErrorExitFailure("RTLdrOpenEx failed to open '%s': %Rrc%#RTeim",
                                             paInputs[i].pszFile, rc, &ErrInfo.Core);

            size_t const cbImage = RTLdrSize(paInputs[i].hLdrMod);
            if (cbImage == ~(size_t)0)
                return RTMsgErrorExitFailure("RTLdrSize failed on '%s'!", paInputs[i].pszFile);
            if (cbImage > _64M)
                return RTMsgErrorExitFailure("Image '%s' is definitely too large: %#zx", paInputs[i].pszFile, cbImage);

            paInputs[i].cbBits = RT_ALIGN_32((uint32_t)cbImage, 4096); /* Bs3InitHighDlls_rm depend on the 4KiB alignment.  */
            paInputs[i].pbBits = (uint8_t *)RTMemAllocZ(paInputs[i].cbBits);
            if (!paInputs[i].pbBits)
                return RTMsgErrorExitFailure("Out of memory (%#x)\n", paInputs[i].cbBits);

            /* Locate the entry for this high dll in the base image. */
            BS3HIGHDLLENTRY *pHighDllEntry = LocateHighDllEntry(paInputs[1].pbBits, paInputs[1].cbFile,
                                                                RTPathFilename(paInputs[i].pszFile));
            AssertReturn(pHighDllEntry, RTEXITCODE_FAILURE);
            if (   pHighDllEntry->cbLoaded  != paInputs[i].cbBits
                || pHighDllEntry->cbInImage != paInputs[i].cbBits)
                return RTMsgErrorExitFailure("HighDllEntry fields cbLoaded=%#x and/or cbInImage=%#x differs from cbBits=%#x!",
                                             pHighDllEntry->cbLoaded, pHighDllEntry->cbInImage, paInputs[i].cbBits);

            /* Update the segment table with actual selectors. */
            uint32_t const     cSegments  = pHighDllEntry->cSegments;
            BS3HIGHDLLSEGMENT *paSegments = (BS3HIGHDLLSEGMENT *)((uintptr_t)pHighDllEntry + pHighDllEntry->offSegments);
            if (cSegments < 1 || cSegments > 32)
                return RTMsgErrorExitFailure("Bogus segment count for '%s': %u", paInputs[i].pszFile, cSegments);
            for (unsigned iSeg = 0; iSeg < cSegments; iSeg++)
            {
                if (paSegments[iSeg].fFlags & BS3HIGHDLLSEGMENT_F_16BIT)
                {
                    if (paSegments[iSeg].fFlags & BS3HIGHDLLSEGMENT_F_EXEC)
                    {
                        if (idxHi16BitCs >= BS3_SEL_HIGH16_CS_COUNT)
                            return RTMsgErrorExitFailure("Out of 16-bit CS selectors ('%s')!", paInputs[i].pszFile);
                        paSegments[iSeg].idxSel = BS3_SEL_HIGH16_CS_FIRST + idxHi16BitCs * 8;
                        idxHi16BitCs++;
                        rc = RTLdrLxSetSegmentSelectors(paInputs[i].hLdrMod, iSeg, paSegments[iSeg].idxSel, BS3_SEL_HIGH32_CS);
                    }
                    else
                    {
                        if (idxHi16BitDs >= BS3_SEL_HIGH16_DS_COUNT)
                            return RTMsgErrorExitFailure("Out of 16-bit DS selectors ('%s')!", paInputs[i].pszFile);
                        paSegments[iSeg].idxSel = BS3_SEL_HIGH16_DS_FIRST + idxHi16BitDs * 8;
                        idxHi16BitDs++;
                        rc = RTLdrLxSetSegmentSelectors(paInputs[i].hLdrMod, iSeg, paSegments[iSeg].idxSel, BS3_SEL_HIGH32_DS);
                    }
                }
                else
                {
                    if (paSegments[iSeg].fFlags & BS3HIGHDLLSEGMENT_F_32BIT)
                        paSegments[iSeg].idxSel = paSegments[iSeg].fFlags & BS3HIGHDLLSEGMENT_F_EXEC
                                                ? BS3_SEL_HIGH32_CS : BS3_SEL_HIGH32_DS;
                    else if (paSegments[iSeg].fFlags & BS3HIGHDLLSEGMENT_F_64BIT)
                        paSegments[iSeg].idxSel = paSegments[iSeg].fFlags & BS3HIGHDLLSEGMENT_F_EXEC
                                                ? BS3_SEL_HIGH64_CS : BS3_SEL_HIGH64_DS;
                    else
                        paSegments[iSeg].idxSel = 0;
                    rc = RTLdrLxSetSegmentSelectors(paInputs[i].hLdrMod, iSeg, 4, paSegments[iSeg].idxSel);
                }
                if (RT_FAILURE(rc))
                    return RTMsgErrorExitFailure("RTLdrLxSetSegmentSelectors failed segment #%u in '%s': %Rrc",
                                                 iSeg, paInputs[i].pszFile, rc);
                paSegments[iSeg].uAddr += uHiLoadAddr - BS3HIGHDLL_LOAD_ADDRESS; /* Was RVA + BS3HIGHDLL_LOAD_ADDRESS. */
            }

            /* Update the export addresses. */
            BS3HIGHDLLEXPORTENTRY *paExports   = (BS3HIGHDLLEXPORTENTRY *)((uintptr_t)pHighDllEntry + pHighDllEntry->offExports);
            const char * const     pszzStrings = (const char *)((uintptr_t)pHighDllEntry + pHighDllEntry->offStrings);
            size_t iExport = pHighDllEntry->cExports;
            while (iExport-- > 0)
            {
                const char * const pszSymbol = (const char *)&pszzStrings[paExports[iExport].offName];
                uint16_t const     idxSeg    = paExports[iExport].idxSel;
                if (idxSeg >= cSegments)
                    return RTMsgErrorExitFailure("Bogus idxSel for '%s' in '%s': %#x(:%#x)",
                                                 pszSymbol, paInputs[i].pszFile, idxSeg, paExports[iExport].offSeg);
                RTLDRADDR Value = 0;
                rc = RTLdrGetSymbolEx(paInputs[i].hLdrMod, paInputs[i].pbBits, uHiLoadAddr, UINT32_MAX, pszSymbol, &Value);
                if (RT_SUCCESS(rc))
                {
                    if (Value != paExports[iExport].offSeg + paSegments[idxSeg].uAddr)
                        return RTMsgErrorExitFailure("Bogus value for '%s' in '%s': %#RX64, expected %#RX64",
                                                     pszSymbol, paInputs[i].pszFile, Value,
                                                     paExports[iExport].offSeg + paSegments[idxSeg].uAddr);
                    paExports[iExport].offFlat = (uint32_t)Value;
                    paExports[iExport].idxSel = paSegments[idxSeg].idxSel;
                    if (paSegments[idxSeg].fFlags & (BS3HIGHDLLSEGMENT_F_32BIT | BS3HIGHDLLSEGMENT_F_64BIT))
                        paExports[iExport].offSeg = (uint32_t)Value; /* 32-bit and 64-bit uses FLAT selectors, so FLAT addresses too. */
                }
                else
                    return RTMsgErrorExitFailure("Failed to resolve '%s' in '%s': %Rrc", pszSymbol, paInputs[i].pszFile, rc);
            }

            /* Get the fixed up image bits. */
            rc = RTLdrGetBits(paInputs[i].hLdrMod, paInputs[i].pbBits, uHiLoadAddr, ResolveHighDllImportCallback, pHighDllEntry);
            if (RT_FAILURE(rc))
                return RTMsgErrorExitFailure("RTLdrGetBits failed on '%s': %Rrc", paInputs[i].pszFile, rc);

            /* Update the DLL entry with the load address and file address: */
            pHighDllEntry->offInImage = off;
            pHighDllEntry->uLoadAddr  = uHiLoadAddr;
            pHighDllEntry->uChecksum  = Bs3CalcChecksum(BS3_CALC_CHECKSUM_INITIAL_VALUE, paInputs[i].pbBits, paInputs[i].cbBits);
            paInputs[i].uLoadAddr     = uHiLoadAddr;
            uHiLoadAddr += paInputs[i].cbBits;
#if 0
            RTMsgInfo("offInImage=%#RX32 / sector #%u LB %#x\n", paInputs[i].offInImage, paInputs[i].offInImage / 512, paInputs[i].cbBits);
            uint32_t uChecksum = BS3_CALC_CHECKSUM_INITIAL_VALUE;
            for (unsigned j = 0, cSectors = paInputs[i].cbBits / 512; j < cSectors; j++)
                RTMsgInfo("sector #%u: %#010RX32 %#010RX32\n", j,
                          uChecksum = Bs3CalcChecksum(uChecksum, &paInputs[i].pbBits[j * 512], 512),
                          Bs3CalcChecksum(BS3_CALC_CHECKSUM_INITIAL_VALUE, &paInputs[i].pbBits[j * 512], 512));
            if (uChecksum != pHighDllEntry->uChecksum)
                return RTMsgErrorExitFailure("Checksum calculation error: %#x, expected %#x", uChecksum, pHighDllEntry->uChecksum);
#endif
        }
        Assert(!(off & 0x1ff));
        off += paInputs[i].cbBits;
        Assert(!(off & 0x1ff));
    }

    /** @todo image size check.   */

    /*
     * Patch the BPB with base image sector count and image checksum.
     */
    PBS3BOOTSECTOR pBs = (PBS3BOOTSECTOR)paInputs[0].pbBits;
    if (   memcmp(pBs->abLabel,  RT_STR_TUPLE(BS3_LABEL))  == 0
        && memcmp(pBs->abFSType, RT_STR_TUPLE(BS3_FSTYPE)) == 0
        && memcmp(pBs->abOemId,  RT_STR_TUPLE(BS3_OEMID))  == 0)
    {
        pBs->cLargeTotalSectors = (paInputs[0].cbBits + paInputs[1].cbBits) / 512;
        pBs->cTotalSectors      = RT_MIN(paInputs[1].cbBits, _1M) / 512;
        pBs->dwSerialNumber     = Bs3CalcChecksum(BS3_CALC_CHECKSUM_INITIAL_VALUE, paInputs[1].pbBits,
                                                  pBs->cTotalSectors * 512); /* dwSerialNumber is misaligned */
    }
    else
        return RTMsgErrorExitFailure("Didn't find magic strings in the first file (%s).", paInputs[0].pszFile);

    /*
     * Write out the image.
     */
    for (unsigned i = 0; i < cInputs; i++)
    {
        Assert(ftell(pOutput) == (int32_t)paInputs[i].offInImage);
        ssize_t cbWritten = fwrite(paInputs[i].pbBits, sizeof(uint8_t), paInputs[i].cbBits, pOutput);
        if (cbWritten != (ssize_t)paInputs[i].cbBits)
            return RTMsgErrorExitFailure("fwrite failed (%zd vs %zu)", cbWritten, paInputs[i].cbBits);
    }

    /*
     * Avoid output sizes that makes the FDC code think it's a single sided
     * floppy.  The BIOS always report double sided floppies, and even if we
     * the bootsector adjust it's bMaxHeads value when getting a 20h error
     * we end up with a garbaged image (seems somewhere in the BIOS/FDC it is
     * still treated as a double sided floppy and we get half the data we want
     * and with gaps).
     *
     * Similarly, if the size is 320KB or 360KB the FDC detects it as a double
     * sided 5.25" floppy with 40 tracks, while the BIOS keeps reporting a
     * 1.44MB 3.5" floppy. So, just avoid those sizes too.
     *
     * There are many problem sizes like 400kB, 820kB; see fd_formats[] in
     * src/VBox/Devices/Storage/DevFdc.cpp.  Every size in the table is an
     * even number of kB; let's just pad to an odd number number of kB.
     *
     * Controller-recognized sizes cause boot failure in bs3-bootsector.asm.
     * When avoiding 64kB DMA boundaries it tries to read a sector outside the
     * guessed geometry, receiving a hard I/O error.
     */
    uint32_t cbOutput = ftell(pOutput);
#if 1
    uint32_t uBoundary = cbOutput % (2 * 1024);
    if (uBoundary != 1024)
    {
        static uint8_t const s_abZeroBytes[2 * 1024] = { 0 };
        uint32_t cbPaddingNeeded = (uBoundary < 1024) ? 1024 - uBoundary : 3 * 1024 - uBoundary;

        if (fwrite(s_abZeroBytes, sizeof(uint8_t), cbPaddingNeeded, pOutput) != cbPaddingNeeded)
            return RTMsgErrorExitFailure("fwrite failed (padding)");
    }
#else
    if (   cbOutput == 512 * 8 * 40 * 1 /* 160kB 5"1/4 SS */
        || cbOutput == 512 * 9 * 40 * 1 /* 180kB 5"1/4 SS */
        || cbOutput == 512 * 8 * 40 * 2 /* 320kB 5"1/4 DS */
        || cbOutput == 512 * 9 * 40 * 2 /* 360kB 5"1/4 DS */ )
    {
        static uint8_t const s_abZeroSector[512] = { 0 };
        if (fwrite(s_abZeroSector, sizeof(uint8_t), sizeof(s_abZeroSector), pOutput) != sizeof(s_abZeroSector))
            return RTMsgErrorExitFailure("fwrite failed (padding)");
    }
#endif

    return RTEXITCODE_SUCCESS;
}

int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Scan the arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--output",                           'o', RTGETOPT_REQ_STRING },
        { "--generate-high-dll-import-table",   'g', RTGETOPT_REQ_STRING },
    };

    const char  *pszOutput      = NULL;
    const char  *pszGenAsmFor   = NULL;
    BS3LNKINPUT  aInputs[3];                /* 3 = bootsector, low image, high image */
    unsigned     cInputs        = 0;
    uint32_t     cSectors       = 0;

    RTGETOPTSTATE   GetState;
    rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_SYNTAX);
    RTGETOPTUNION   ValueUnion;
    int             ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'o':
                if (pszOutput)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Only one output file is allowed. You've specified '%s' and '%s'",
                                          pszOutput, ValueUnion.psz);
                pszOutput = ValueUnion.psz;
                break;

            case 'g':
                if (pszGenAsmFor)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "--generate-high-dll-import-table can only be used once (first for %s, now for %s)",
                                          pszGenAsmFor, ValueUnion.psz);
                pszGenAsmFor = ValueUnion.psz;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                if (pszGenAsmFor)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "--generate-high-dll-import-table don't take any non-option arguments!");

                /*
                 * Add to input file collection.
                 */
                if (cInputs >= RT_ELEMENTS(aInputs))
                    return RTMsgErrorExitFailure("Too many input files!");
                aInputs[cInputs].pszFile = ValueUnion.psz;
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
                FILE *pFile = fopen(aInputs[cInputs].pszFile, "rb");
#else
                FILE *pFile = fopen(aInputs[cInputs].pszFile, "r");
#endif
                if (pFile)
                {
                    if (fseek(pFile, 0, SEEK_END) == 0)
                    {
                        aInputs[cInputs].cbFile = (uint32_t)ftell(pFile);
                        if (fseek(pFile, 0, SEEK_SET) == 0)
                        {
                            if (cInputs != 0 || aInputs[cInputs].cbFile == 512)
                            {
                                cSectors += RT_ALIGN_32(aInputs[cInputs].cbFile, 512) / 512;
                                if (cSectors <= BS3_MAX_SIZE / 512 || cInputs > 0)
                                {
                                    if (cSectors > 0)
                                    {
                                        aInputs[cInputs].pbBits  = NULL;
                                        aInputs[cInputs].cbBits  = 0;
                                        aInputs[cInputs].hLdrMod = NIL_RTLDRMOD;
                                        aInputs[cInputs++].pFile = pFile;
                                        pFile = NULL;
                                        break;
                                    }
                                    RTMsgError("empty input file: '%s'", aInputs[cInputs].pszFile);
                                }
                                else
                                    RTMsgError("input is too big: %u bytes, %u sectors (max %u bytes, %u sectors)\n"
                                               "detected loading '%s'",
                                               cSectors * 512, cSectors, BS3_MAX_SIZE, BS3_MAX_SIZE / 512,
                                               aInputs[cInputs].pszFile);
                            }
                            else
                                RTMsgError("first input file (%s) must be exactly 512 bytes", aInputs[cInputs].pszFile);
                        }
                        else
                            RTMsgError("seeking to start of '%s' failed", aInputs[cInputs].pszFile);
                    }
                    else
                        RTMsgError("seeking to end of '%s' failed", aInputs[cInputs].pszFile);
                }
                else
                    RTMsgError("Failed to open input file '%s' for reading", aInputs[cInputs].pszFile);
                if (pFile)
                    fclose(pFile);
                return RTEXITCODE_FAILURE;
            }

            case 'V':
                printf("%s\n", "$Revision: 106320 $");
                return RTEXITCODE_SUCCESS;

            case 'h':
                printf("usage: %s --output <output> <basemod> [high-dll1... [high-dllN]]\n"
                       "   or: %s --output <high-dll.asm> --generate-high-dll-import-table <some.high-dll>\n"
                       "   or: %s --help\n"
                       "   or: %s --version\n"
                       , argv[0], argv[0], argv[0], argv[0]);
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    if (!pszOutput)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No output file was specified (-o or --output).");

    if (cInputs == 0 && !pszGenAsmFor)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No input files was specified.");

    /*
     * Do the job.
     */
    /* Open the output file */
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
    FILE *pOutput = fopen(pszOutput, !pszGenAsmFor ? "wb" : "w");
#else
    FILE *pOutput = fopen(pszOutput, "w");
#endif
    if (!pOutput)
        RTMsgErrorExitFailure("Failed to open output file '%s' for writing!", pszOutput);

    RTEXITCODE rcExit;
    if (pszGenAsmFor)
        rcExit = GenerateHighDllImportTableAssembly(pOutput, pszGenAsmFor);
    else
        rcExit = DoTheLinking(pOutput, aInputs, cInputs);

    /* Finally, close the output file (can fail because of buffered data). */
    if (fclose(pOutput) != 0)
        rcExit = RTMsgErrorExitFailure("Error closing '%s'!", pszOutput);

    /* Delete the output file on failure. */
    if (rcExit != RTEXITCODE_SUCCESS)
        RTFileDelete(pszOutput);

    /* Close the input files and free memory associated with them. */
    for (unsigned i = 0; i < cInputs && rcExit == 0; i++)
    {
        if (aInputs[i].pFile)
            fclose(aInputs[i].pFile);
        if (aInputs[i].hLdrMod != NIL_RTLDRMOD)
            RTLdrClose(aInputs[i].hLdrMod);
        if (aInputs[i].pbBits)
            RTMemFree(aInputs[i].pbBits);
    }

    /* Close stderr to make sure it's flushed properly. */
    fclose(stderr);
    return rcExit;
}

