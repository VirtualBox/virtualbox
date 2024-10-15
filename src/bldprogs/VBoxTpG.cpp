/* $Id: VBoxTpG.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Build Tool - VBox Tracepoint Generator.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/VBoxTpG.h>

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/env.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/uuid.h>

#include "scmstream.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct VTGPROBE *PVTGPROBE;

typedef struct VTGATTRS
{
    kVTGStability   enmCode;
    kVTGStability   enmData;
    kVTGClass       enmDataDep;
} VTGATTRS;
typedef VTGATTRS *PVTGATTRS;


typedef struct VTGARG
{
    RTLISTNODE      ListEntry;
    /** The argument name. (heap) */
    char           *pszName;
    /** The type presented to the tracer (in string table). */
    const char     *pszTracerType;
    /** The argument type used in the probe method in that context. (heap) */
    char           *pszCtxType;
    /** Argument passing format string.  First and only argument is the name.
     *  (const string) */
    const char     *pszArgPassingFmt;
    /** The type flags. */
    uint32_t        fType;
    /** The argument number (0-based) for complaining/whatever. */
    uint16_t        iArgNo;
    /** The probe the argument belongs to (for complaining/whatever). */
    PVTGPROBE       pProbe;
    /** The absolute source position. */
    size_t          offSrc;
} VTGARG;
typedef VTGARG *PVTGARG;

typedef struct VTGPROBE
{
    RTLISTNODE      ListEntry;
    char           *pszMangledName;
    const char     *pszUnmangledName;
    RTLISTANCHOR    ArgHead;
    uint32_t        cArgs;
    bool            fHaveLargeArgs;
    uint32_t        offArgList;
    uint32_t        iProbe;
    size_t          iLine;
} VTGPROBE;

typedef struct VTGPROVIDER
{
    RTLISTNODE      ListEntry;
    const char     *pszName;

    uint16_t        iFirstProbe;
    uint16_t        cProbes;

    VTGATTRS        AttrSelf;
    VTGATTRS        AttrModules;
    VTGATTRS        AttrFunctions;
    VTGATTRS        AttrName;
    VTGATTRS        AttrArguments;

    RTLISTANCHOR    ProbeHead;
} VTGPROVIDER;
typedef VTGPROVIDER *PVTGPROVIDER;

/**
 * A string table string.
 */
typedef struct VTGSTRING
{
    /** The string space core. */
    RTSTRSPACECORE  Core;
    /** The string table offset. */
    uint32_t        offStrTab;
    /** The actual string. */
    char            szString[1];
} VTGSTRING;
typedef VTGSTRING *PVTGSTRING;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The string space organizing the string table strings. Each node is a VTGSTRING. */
static RTSTRSPACE       g_StrSpace = NULL;
/** Used by the string table enumerator to set VTGSTRING::offStrTab. */
static uint32_t         g_offStrTab;
/** List of providers created by the parser. */
static RTLISTANCHOR     g_ProviderHead;
/** The number of type errors. */
static uint32_t         g_cTypeErrors = 0;


/** @name Options
 * @{ */
static enum
{
    kVBoxTpGAction_Nothing,
    kVBoxTpGAction_GenerateHeader,
    kVBoxTpGAction_GenerateWrapperHeader,
    kVBoxTpGAction_GenerateObject
}                           g_enmAction                 = kVBoxTpGAction_Nothing;
static uint32_t             g_cBits                     = HC_ARCH_BITS;
static uint32_t             g_cHostBits                 = HC_ARCH_BITS;
static uint32_t             g_fTypeContext              = VTG_TYPE_CTX_R0;
static const char          *g_pszContextDefine          = "IN_RING0";
static const char          *g_pszContextDefine2         = NULL;
static bool                 g_fApplyCpp                 = false;
static uint32_t             g_cVerbosity                = 0;
static const char          *g_pszOutput                 = NULL;
static const char          *g_pszScript                 = NULL;
static const char          *g_pszTempAsm                = NULL;
#ifdef RT_OS_DARWIN
static const char          *g_pszAssembler              = "yasm";
static const char          *g_pszAssemblerFmtOpt        = "-f";
static const char           g_szAssemblerFmtVal32[]     = "macho32";
static const char           g_szAssemblerFmtVal64[]     = "macho64";
static const char           g_szAssemblerOsDef[]        = "RT_OS_DARWIN";
#elif defined(RT_OS_OS2)
static const char          *g_pszAssembler              = "nasm.exe";
static const char          *g_pszAssemblerFmtOpt        = "-f";
static const char           g_szAssemblerFmtVal32[]     = "obj";
static const char           g_szAssemblerFmtVal64[]     = "elf64";
static const char           g_szAssemblerOsDef[]        = "RT_OS_OS2";
#elif defined(RT_OS_WINDOWS)
static const char          *g_pszAssembler              = "yasm.exe";
static const char          *g_pszAssemblerFmtOpt        = "-f";
static const char           g_szAssemblerFmtVal32[]     = "win32";
static const char           g_szAssemblerFmtVal64[]     = "win64";
static const char           g_szAssemblerOsDef[]        = "RT_OS_WINDOWS";
#else
static const char          *g_pszAssembler              = "yasm";
static const char          *g_pszAssemblerFmtOpt        = "-f";
static const char           g_szAssemblerFmtVal32[]     = "elf32";
static const char           g_szAssemblerFmtVal64[]     = "elf64";
# ifdef RT_OS_FREEBSD
static const char           g_szAssemblerOsDef[]        = "RT_OS_FREEBSD";
# elif  defined(RT_OS_NETBSD)
static const char           g_szAssemblerOsDef[]        = "RT_OS_NETBSD";
# elif  defined(RT_OS_OPENBSD)
static const char           g_szAssemblerOsDef[]        = "RT_OS_OPENBSD";
# elif  defined(RT_OS_LINUX)
static const char           g_szAssemblerOsDef[]        = "RT_OS_LINUX";
# elif  defined(RT_OS_SOLARIS)
static const char           g_szAssemblerOsDef[]        = "RT_OS_SOLARIS";
# else
#  error "Port me!"
# endif
#endif
static const char          *g_pszAssemblerFmtVal        = RT_CONCAT(g_szAssemblerFmtVal, HC_ARCH_BITS);
static const char          *g_pszAssemblerDefOpt        = "-D";
static const char          *g_pszAssemblerIncOpt        = "-I";
static char                 g_szAssemblerIncVal[RTPATH_MAX];
static const char          *g_pszAssemblerIncVal        = __FILE__ "/../../../include/";
static const char          *g_pszAssemblerOutputOpt     = "-o";
static unsigned             g_cAssemblerOptions         = 0;
static const char          *g_apszAssemblerOptions[32];
static const char          *g_pszProbeFnName            = "SUPR0TracerFireProbe";
static bool                 g_fProbeFnImported          = true;
static bool                 g_fPic                      = false;
/** @} */




/**
 * Inserts a string into the string table, reusing any matching existing string
 * if possible.
 *
 * @returns Read only string.
 * @param   pch                 The string to insert (need not be terminated).
 * @param   cch                 The length of the string.
 */
static const char *strtabInsertN(const char *pch, size_t cch)
{
    PVTGSTRING pStr = (PVTGSTRING)RTStrSpaceGetN(&g_StrSpace, pch, cch);
    if (pStr)
        return pStr->szString;

    /*
     * Create a new entry.
     */
    pStr = (PVTGSTRING)RTMemAlloc(RT_UOFFSETOF_DYN(VTGSTRING, szString[cch + 1]));
    if (!pStr)
        return NULL;

    pStr->Core.pszString = pStr->szString;
    memcpy(pStr->szString, pch, cch);
    pStr->szString[cch]  = '\0';
    pStr->offStrTab      = UINT32_MAX;

    bool fRc = RTStrSpaceInsert(&g_StrSpace, &pStr->Core);
    Assert(fRc); NOREF(fRc);
    return pStr->szString;
}


/**
 * Retrieves the string table offset of the given string table string.
 *
 * @returns String table offset.
 * @param   pszStrTabString     The string table string.
 */
static uint32_t strtabGetOff(const char *pszStrTabString)
{
    PVTGSTRING pStr = RT_FROM_MEMBER(pszStrTabString, VTGSTRING, szString[0]);
    Assert(pStr->Core.pszString == pszStrTabString);
    return pStr->offStrTab;
}


/**
 * Invokes the assembler.
 *
 * @returns Exit code.
 * @param   pszOutput           The output file.
 * @param   pszTempAsm          The source file.
 */
static RTEXITCODE generateInvokeAssembler(const char *pszOutput, const char *pszTempAsm)
{
    const char     *apszArgs[64];
    unsigned        iArg = 0;

    apszArgs[iArg++] = g_pszAssembler;
    apszArgs[iArg++] = g_pszAssemblerFmtOpt;
    apszArgs[iArg++] = g_pszAssemblerFmtVal;
    apszArgs[iArg++] = g_pszAssemblerDefOpt;
    if (!strcmp(g_pszAssemblerFmtVal, "macho32") || !strcmp(g_pszAssemblerFmtVal, "macho64"))
        apszArgs[iArg++] = "ASM_FORMAT_MACHO";
    else if (!strcmp(g_pszAssemblerFmtVal, "obj") || !strcmp(g_pszAssemblerFmtVal, "omf"))
        apszArgs[iArg++] = "ASM_FORMAT_OMF";
    else if (   !strcmp(g_pszAssemblerFmtVal, "win32")
             || !strcmp(g_pszAssemblerFmtVal, "win64")
             || !strcmp(g_pszAssemblerFmtVal, "pe32")
             || !strcmp(g_pszAssemblerFmtVal, "pe64")
             || !strcmp(g_pszAssemblerFmtVal, "pe") )
        apszArgs[iArg++] = "ASM_FORMAT_PE";
    else if (   !strcmp(g_pszAssemblerFmtVal, "elf32")
             || !strcmp(g_pszAssemblerFmtVal, "elf64")
             || !strcmp(g_pszAssemblerFmtVal, "elf"))
        apszArgs[iArg++] = "ASM_FORMAT_ELF";
    else
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Unknown assembler format '%s'", g_pszAssemblerFmtVal);
    apszArgs[iArg++] = g_pszAssemblerDefOpt;
    if (g_cBits == 32)
        apszArgs[iArg++] = "ARCH_BITS=32";
    else
        apszArgs[iArg++] = "ARCH_BITS=64";
    apszArgs[iArg++] = g_pszAssemblerDefOpt;
    if (g_cHostBits == 32)
        apszArgs[iArg++] = "HC_ARCH_BITS=32";
    else
        apszArgs[iArg++] = "HC_ARCH_BITS=64";
    apszArgs[iArg++] = g_pszAssemblerDefOpt;
    if (g_cBits == 32)
        apszArgs[iArg++] = "RT_ARCH_X86";
    else
        apszArgs[iArg++] = "RT_ARCH_AMD64";
    apszArgs[iArg++] = g_pszAssemblerDefOpt;
    apszArgs[iArg++] = g_pszContextDefine;
    if (g_pszContextDefine2)
    {
        apszArgs[iArg++] = g_pszAssemblerDefOpt;
        apszArgs[iArg++] = g_pszContextDefine2;
    }
    if (g_szAssemblerOsDef[0])
    {
        apszArgs[iArg++] = g_pszAssemblerDefOpt;
        apszArgs[iArg++] = g_szAssemblerOsDef;
    }
    apszArgs[iArg++] = g_pszAssemblerIncOpt;
    apszArgs[iArg++] = g_pszAssemblerIncVal;
    apszArgs[iArg++] = g_pszAssemblerOutputOpt;
    apszArgs[iArg++] = pszOutput;
    for (unsigned i = 0; i < g_cAssemblerOptions; i++)
        apszArgs[iArg++] = g_apszAssemblerOptions[i];
    apszArgs[iArg++] = pszTempAsm;
    apszArgs[iArg]   = NULL;
    Assert(iArg <= RT_ELEMENTS(apszArgs));

    if (g_cVerbosity > 1)
    {
        RTMsgInfo("Starting assmbler '%s' with arguments:\n",  g_pszAssembler);
        for (unsigned i = 0; i < iArg; i++)
            RTMsgInfo("  #%02u: '%s'\n",  i, apszArgs[i]);
    }

    RTPROCESS hProc;
    int rc = RTProcCreate(apszArgs[0], apszArgs, RTENV_DEFAULT, RTPROC_FLAGS_SEARCH_PATH, &hProc);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to start '%s' (assembler): %Rrc", apszArgs[0], rc);

    RTPROCSTATUS Status;
    rc = RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &Status);
    if (RT_FAILURE(rc))
    {
        RTProcTerminate(hProc);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTProcWait failed: %Rrc", rc);
    }
    if (Status.enmReason == RTPROCEXITREASON_SIGNAL)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "The assembler failed: signal %d", Status.iStatus);
    if (Status.enmReason != RTPROCEXITREASON_NORMAL)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "The assembler failed: abend");
    if (Status.iStatus != 0)
        return RTMsgErrorExit((RTEXITCODE)Status.iStatus, "The assembler failed: exit code %d", Status.iStatus);

    return RTEXITCODE_SUCCESS;
}


/**
 * Worker that does the boring bits when generating a file.
 *
 * @returns Exit code.
 * @param   pszOutput           The name of the output file.
 * @param   pszWhat             What kind of file it is.
 * @param   pfnGenerator        The callback function that provides the contents
 *                              of the file.
 */
static RTEXITCODE generateFile(const char *pszOutput, const char *pszWhat,
                               RTEXITCODE (*pfnGenerator)(PSCMSTREAM))
{
    SCMSTREAM Strm;
    int rc = ScmStreamInitForWriting(&Strm, NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "ScmStreamInitForWriting returned %Rrc when generating the %s file",
                              rc, pszWhat);

    RTEXITCODE rcExit = pfnGenerator(&Strm);
    if (RT_FAILURE(ScmStreamGetStatus(&Strm)))
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Stream error %Rrc generating the %s file",
                                ScmStreamGetStatus(&Strm), pszWhat);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        rc = ScmStreamWriteToFile(&Strm, "%s", pszOutput);
        if (RT_FAILURE(rc))
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "ScmStreamWriteToFile returned %Rrc when writing '%s' (%s)",
                                    rc, pszOutput, pszWhat);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            if (g_cVerbosity > 0)
                RTMsgInfo("Successfully generated '%s'.", pszOutput);
            if (g_cVerbosity > 1)
            {
                RTMsgInfo("================ %s - start ================", pszWhat);
                ScmStreamRewindForReading(&Strm);
                const char *pszLine;
                size_t      cchLine;
                SCMEOL      enmEol;
                while ((pszLine = ScmStreamGetLine(&Strm, &cchLine, &enmEol)) != NULL)
                    RTPrintf("%.*s\n", cchLine, pszLine);
                RTMsgInfo("================ %s - end   ================", pszWhat);
            }
        }
    }
    ScmStreamDelete(&Strm);
    return rcExit;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Writes the string table strings.}
 */
static DECLCALLBACK(int) generateAssemblyStrTabCallback(PRTSTRSPACECORE pStr, void *pvUser)
{
    PVTGSTRING pVtgStr = (PVTGSTRING)pStr;
    PSCMSTREAM pStrm   = (PSCMSTREAM)pvUser;

    pVtgStr->offStrTab = g_offStrTab;
    g_offStrTab += (uint32_t)pVtgStr->Core.cchString + 1;

    ScmStreamPrintf(pStrm,
                    "    db '%s', 0 ; off=%u len=%zu\n",
                    pVtgStr->szString, pVtgStr->offStrTab, pVtgStr->Core.cchString);
    return VINF_SUCCESS;
}


/**
 * Generate assembly source that can be turned into an object file.
 *
 * (This is a generateFile callback.)
 *
 * @returns Exit code.
 * @param   pStrm               The output stream.
 */
static RTEXITCODE generateAssembly(PSCMSTREAM pStrm)
{
    PVTGPROVIDER    pProvider;
    PVTGPROBE       pProbe;
    PVTGARG         pArg;


    if (g_cVerbosity > 0)
        RTMsgInfo("Generating assembly code...");

    /*
     * Write the file header.
     */
    ScmStreamPrintf(pStrm,
                    "; $Id: VBoxTpG.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ \n"
                    ";; @file\n"
                    "; Automatically generated from %s. Do NOT edit!\n"
                    ";\n"
                    "\n"
                    "%%include \"iprt/asmdefs.mac\"\n"
                    "\n"
                    "\n"
                    ";"
                    "; We put all the data in a dedicated section / segment.\n"
                    ";\n"
                    "; In order to find the probe location specifiers, we do the necessary\n"
                    "; trickery here, ASSUMING that this object comes in first in the link\n"
                    "; editing process.\n"
                    ";\n"
                    "%%ifdef ASM_FORMAT_OMF\n"
                    " %%macro VTG_GLOBAL 2\n"
                    "  global NAME(%%1)\n"
                    "  NAME(%%1):\n"
                    " %%endmacro\n"
                    " segment VTG.Obj public CLASS=VTG align=4096 use32\n"
                    "\n"
                    "%%elifdef ASM_FORMAT_MACHO\n"
                    " %%macro VTG_GLOBAL 2\n"
                    "  global NAME(%%1)\n"
                    "  NAME(%%1):\n"
                    " %%endmacro\n"
                    "  %%ifdef IN_RING3\n"
                    "   %%define VTG_NEW_MACHO_LINKER\n"
                    "  %%elif ARCH_BITS == 64\n"
                    "   %%define VTG_NEW_MACHO_LINKER\n"
                    "  %%elifdef IN_RING0_AGNOSTIC\n"
                    "   %%define VTG_NEW_MACHO_LINKER\n"
                    "  %%endif\n"
                    " %%ifdef VTG_NEW_MACHO_LINKER\n"
                    "  ; Section order hack!\n"
                    "  ; With the ld64-97.17 linker there was a problem with it determining the section\n"
                    "  ; order based on symbol references. The references to the start and end of the\n"
                    "  ; __VTGPrLc section forced it in front of __VTGObj, we want __VTGObj first.\n"
                    "  extern section$start$__VTG$__VTGObj\n"
                    "  extern section$end$__VTG$__VTGObj\n"
                    " %%else\n"
                    "  ; Creating 32-bit kext of the type MH_OBJECT. No fancy section end/start symbols handy.\n"
                    "  [section __VTG __VTGObj        align=16]\n"
                    "VTG_GLOBAL g_aVTGObj_LinkerPleaseNoticeMe, data\n"
                    "  [section __VTG __VTGPrLc.Begin align=16]\n"
                    "  dq 0, 0 ; Paranoia, related to the fudge below.\n"
                    "VTG_GLOBAL g_aVTGPrLc, data\n"
                    "  [section __VTG __VTGPrLc align=16]\n"
                    "VTG_GLOBAL g_aVTGPrLc_LinkerPleaseNoticeMe, data\n"
                    "  [section __VTG __VTGPrLc.End   align=16]\n"
                    "VTG_GLOBAL g_aVTGPrLc_End, data\n"
                    "  dq 0, 0 ; Fudge to work around unidentified linker where it would otherwise generate\n"
                    "          ; a fix up of the first dword in __VTGPrLc.Begin despite the fact that it were\n"
                    "          ; an empty section with nothing whatsoever to fix up.\n"
                    " %%endif\n"
                    " [section __VTG __VTGObj]\n"
                    "\n"
                    "%%elifdef ASM_FORMAT_PE\n"
                    " %%macro VTG_GLOBAL 2\n"
                    "  global NAME(%%1)\n"
                    "  NAME(%%1):\n"
                    " %%endmacro\n"
                    " [section VTGPrLc.Begin data align=64]\n"
                    /*"   times 16 db 0xcc\n"*/
                    "VTG_GLOBAL g_aVTGPrLc, data\n"
                    " [section VTGPrLc.Data  data align=4]\n"
                    " [section VTGPrLc.End   data align=4]\n"
                    "VTG_GLOBAL g_aVTGPrLc_End, data\n"
                    /*"   times 16 db 0xcc\n"*/
                    " [section VTGObj   data align=32]\n"
                    "\n"
                    "%%elifdef ASM_FORMAT_ELF\n"
                    " %%macro VTG_GLOBAL 2\n"
                    "  global NAME(%%1):%%2 hidden\n"
                    "  NAME(%%1):\n"
                    " %%endmacro\n"
                    " [section .VTGData progbits alloc noexec write align=4096]\n"
                    " [section .VTGPrLc.Begin progbits alloc noexec write align=32]\n"
                    " dd 0,0,0,0, 0,0,0,0\n"
                    "VTG_GLOBAL g_aVTGPrLc, data\n"
                    " [section .VTGPrLc       progbits alloc noexec write align=1]\n"
                    " [section .VTGPrLc.End   progbits alloc noexec write align=1]\n"
                    "VTG_GLOBAL g_aVTGPrLc_End, data\n"
                    " dd 0,0,0,0, 0,0,0,0\n"
                    " [section .VTGData]\n"
                    "\n"
                    "%%else\n"
                    " %%error \"ASM_FORMAT_XXX is not defined\"\n"
                    "%%endif\n"
                    "\n"
                    "\n"
                    "VTG_GLOBAL g_VTGObjHeader, data\n"
                    "                ;0         1         2         3\n"
                    "                ;012345678901234567890123456789012\n"
                    "    db          'VTG Object Header v1.7', 0, 0\n"
                    "    dd          %u\n"
                    "    dd          NAME(g_acVTGProbeEnabled_End) - NAME(g_VTGObjHeader)\n"
                    "    dd          NAME(g_achVTGStringTable)     - NAME(g_VTGObjHeader)\n"
                    "    dd          NAME(g_achVTGStringTable_End) - NAME(g_achVTGStringTable)\n"
                    "    dd          NAME(g_aVTGArgLists)          - NAME(g_VTGObjHeader)\n"
                    "    dd          NAME(g_aVTGArgLists_End)      - NAME(g_aVTGArgLists)\n"
                    "    dd          NAME(g_aVTGProbes)            - NAME(g_VTGObjHeader)\n"
                    "    dd          NAME(g_aVTGProbes_End)        - NAME(g_aVTGProbes)\n"
                    "    dd          NAME(g_aVTGProviders)         - NAME(g_VTGObjHeader)\n"
                    "    dd          NAME(g_aVTGProviders_End)     - NAME(g_aVTGProviders)\n"
                    "    dd          NAME(g_acVTGProbeEnabled)     - NAME(g_VTGObjHeader)\n"
                    "    dd          NAME(g_acVTGProbeEnabled_End) - NAME(g_acVTGProbeEnabled)\n"
                    "    dd          0\n"
                    "    dd          0\n"
                    "%%ifdef VTG_NEW_MACHO_LINKER\n"
                    " extern section$start$__VTG$__VTGPrLc\n"
                    "    RTCCPTR_DEF section$start$__VTG$__VTGPrLc\n"
                    " %%if ARCH_BITS == 32\n"
                    "    dd          0\n"
                    " %%endif\n"
                    " extern section$end$__VTG$__VTGPrLc\n"
                    "    RTCCPTR_DEF section$end$__VTG$__VTGPrLc\n"
                    " %%if ARCH_BITS == 32\n"
                    "    dd          0\n"
                    " %%endif\n"
                    "%%else\n"
                    "    RTCCPTR_DEF NAME(g_aVTGPrLc)\n"
                    " %%if ARCH_BITS == 32\n"
                    "    dd          0\n"
                    " %%endif\n"
                    "    RTCCPTR_DEF NAME(g_aVTGPrLc_End)\n"
                    " %%if ARCH_BITS == 32\n"
                    "    dd          0\n"
                    " %%endif\n"
                    "%%endif\n"
                    ,
                    g_pszScript, g_cBits);
    RTUUID Uuid;
    int rc = RTUuidCreate(&Uuid);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTUuidCreate failed: %Rrc", rc);
    ScmStreamPrintf(pStrm,
                    "    dd 0%08xh, 0%08xh, 0%08xh, 0%08xh\n"
                    "%%ifdef VTG_NEW_MACHO_LINKER\n"
                    "    RTCCPTR_DEF section$start$__VTG$__VTGObj\n"
                    " %%if ARCH_BITS == 32\n"
                    "    dd          0\n"
                    " %%endif\n"
                    "%%else\n"
                    "    dd 0, 0\n"
                    "%%endif\n"
                    "    dd 0, 0\n"
                    , Uuid.au32[0], Uuid.au32[1], Uuid.au32[2], Uuid.au32[3]);

    /*
     * Dump the string table before we start using the strings.
     */
    ScmStreamPrintf(pStrm,
                    "\n"
                    ";\n"
                    "; The string table.\n"
                    ";\n"
                    "VTG_GLOBAL g_achVTGStringTable, data\n");
    g_offStrTab = 0;
    RTStrSpaceEnumerate(&g_StrSpace, generateAssemblyStrTabCallback, pStrm);
    ScmStreamPrintf(pStrm,
                    "VTG_GLOBAL g_achVTGStringTable_End, data\n");

    /*
     * Write out the argument lists before we use them.
     */
    ScmStreamPrintf(pStrm,
                    "\n"
                    ";\n"
                    "; The argument lists.\n"
                    ";\n"
                    "ALIGNDATA(16)\n"
                    "VTG_GLOBAL g_aVTGArgLists, data\n");
    uint32_t off = 0;
    RTListForEach(&g_ProviderHead, pProvider, VTGPROVIDER, ListEntry)
    {
        RTListForEach(&pProvider->ProbeHead, pProbe, VTGPROBE, ListEntry)
        {
            if (pProbe->offArgList != UINT32_MAX)
                continue;

            /* Write it. */
            pProbe->offArgList = off;
            ScmStreamPrintf(pStrm,
                            "    ; off=%u\n"
                            "    db        %2u  ; Argument count\n"
                            "    db         %u  ; fHaveLargeArgs\n"
                            "    db      0, 0  ; Reserved\n"
                            , off, pProbe->cArgs, (int)pProbe->fHaveLargeArgs);
            off += 4;
            RTListForEach(&pProbe->ArgHead, pArg, VTGARG, ListEntry)
            {
                ScmStreamPrintf(pStrm,
                                "    dd  %8u  ; type '%s' (name '%s')\n"
                                "    dd 0%08xh ; type flags\n",
                                strtabGetOff(pArg->pszTracerType), pArg->pszTracerType, pArg->pszName,
                                pArg->fType);
                off += 8;
            }

            /* Look for matching argument lists (lazy bird walks the whole list). */
            PVTGPROVIDER pProv2;
            RTListForEach(&g_ProviderHead, pProv2, VTGPROVIDER, ListEntry)
            {
                PVTGPROBE pProbe2;
                RTListForEach(&pProvider->ProbeHead, pProbe2, VTGPROBE, ListEntry)
                {
                    if (pProbe2->offArgList != UINT32_MAX)
                        continue;
                    if (pProbe2->cArgs != pProbe->cArgs)
                        continue;

                    PVTGARG pArg2;
                    pArg  = RTListNodeGetNext(&pProbe->ArgHead, VTGARG, ListEntry);
                    pArg2 = RTListNodeGetNext(&pProbe2->ArgHead, VTGARG, ListEntry);
                    int32_t cArgs = pProbe->cArgs;
                    while (   cArgs-- > 0
                           && pArg2->pszTracerType == pArg->pszTracerType
                           && pArg2->fType         == pArg->fType)
                    {
                        pArg  = RTListNodeGetNext(&pArg->ListEntry, VTGARG, ListEntry);
                        pArg2 = RTListNodeGetNext(&pArg2->ListEntry, VTGARG, ListEntry);
                    }
                    if (cArgs >= 0)
                        continue;
                    pProbe2->offArgList = pProbe->offArgList;
                }
            }
        }
    }
    ScmStreamPrintf(pStrm,
                    "VTG_GLOBAL g_aVTGArgLists_End, data\n");


    /*
     * Probe definitions.
     */
    ScmStreamPrintf(pStrm,
                    "\n"
                    ";\n"
                    "; Prob definitions.\n"
                    ";\n"
                    "ALIGNDATA(16)\n"
                    "VTG_GLOBAL g_aVTGProbes, data\n"
                    "\n");
    uint32_t iProvider = 0;
    uint32_t iProbe = 0;
    RTListForEach(&g_ProviderHead, pProvider, VTGPROVIDER, ListEntry)
    {
        pProvider->iFirstProbe = iProbe;
        RTListForEach(&pProvider->ProbeHead, pProbe, VTGPROBE, ListEntry)
        {
            ScmStreamPrintf(pStrm,
                            "VTG_GLOBAL g_VTGProbeData_%s_%s, data ; idx=#%4u\n"
                            "    dd %6u  ; offName\n"
                            "    dd %6u  ; offArgList\n"
                            "    dw (NAME(g_cVTGProbeEnabled_%s_%s) - NAME(g_acVTGProbeEnabled)) / 4 ; idxEnabled\n"
                            "    dw %6u  ; idxProvider\n"
                            "    dd NAME(g_VTGObjHeader) - NAME(g_VTGProbeData_%s_%s) ; offObjHdr\n"
                            ,
                            pProvider->pszName, pProbe->pszMangledName, iProbe,
                            strtabGetOff(pProbe->pszUnmangledName),
                            pProbe->offArgList,
                            pProvider->pszName, pProbe->pszMangledName,
                            iProvider,
                            pProvider->pszName, pProbe->pszMangledName
                            );
            pProbe->iProbe = iProbe;
            iProbe++;
        }
        pProvider->cProbes = iProbe - pProvider->iFirstProbe;
        iProvider++;
    }
    ScmStreamPrintf(pStrm, "VTG_GLOBAL g_aVTGProbes_End, data\n");

    /*
     * The provider data.
     */
    ScmStreamPrintf(pStrm,
                    "\n"
                    ";\n"
                    "; Provider data.\n"
                    ";\n"
                    "ALIGNDATA(16)\n"
                    "VTG_GLOBAL g_aVTGProviders, data\n");
    iProvider = 0;
    RTListForEach(&g_ProviderHead, pProvider, VTGPROVIDER, ListEntry)
    {
        ScmStreamPrintf(pStrm,
                        "    ; idx=#%4u - %s\n"
                        "    dd %6u  ; name\n"
                        "    dw %6u  ; index of first probe\n"
                        "    dw %6u  ; count of probes\n"
                        "    db %d, %d, %d ; AttrSelf\n"
                        "    db %d, %d, %d ; AttrModules\n"
                        "    db %d, %d, %d ; AttrFunctions\n"
                        "    db %d, %d, %d ; AttrName\n"
                        "    db %d, %d, %d ; AttrArguments\n"
                        "    db 0       ; reserved\n"
                        "VTG_GLOBAL g_cVTGProviderProbesEnabled_%s, data\n"
                        "    dd 0\n"
                        "VTG_GLOBAL g_cVTGProviderSettingsSeqNo_%s, data\n"
                        "    dd 0\n"
                        ,
                        iProvider, pProvider->pszName,
                        strtabGetOff(pProvider->pszName),
                        pProvider->iFirstProbe,
                        pProvider->cProbes,
                        pProvider->AttrSelf.enmCode,        pProvider->AttrSelf.enmData,        pProvider->AttrSelf.enmDataDep,
                        pProvider->AttrModules.enmCode,     pProvider->AttrModules.enmData,     pProvider->AttrModules.enmDataDep,
                        pProvider->AttrFunctions.enmCode,   pProvider->AttrFunctions.enmData,   pProvider->AttrFunctions.enmDataDep,
                        pProvider->AttrName.enmCode,        pProvider->AttrName.enmData,        pProvider->AttrName.enmDataDep,
                        pProvider->AttrArguments.enmCode,   pProvider->AttrArguments.enmData,   pProvider->AttrArguments.enmDataDep,
                        pProvider->pszName,
                        pProvider->pszName);
        iProvider++;
    }
    ScmStreamPrintf(pStrm, "VTG_GLOBAL g_aVTGProviders_End, data\n");

    /*
     * Declare the probe enable flags.
     *
     * These must be placed at the end so they'll end up adjacent to the probe
     * locations.  This is important for reducing the amount of memory we need
     * to lock down for user mode modules.
     */
    ScmStreamPrintf(pStrm,
                    ";\n"
                    "; Probe enabled flags.\n"
                    ";\n"
                    "ALIGNDATA(16)\n"
                    "VTG_GLOBAL g_acVTGProbeEnabled, data\n"
                    );
    uint32_t        cProbes = 0;
    RTListForEach(&g_ProviderHead, pProvider, VTGPROVIDER, ListEntry)
    {
        RTListForEach(&pProvider->ProbeHead, pProbe, VTGPROBE, ListEntry)
        {
            ScmStreamPrintf(pStrm,
                            "VTG_GLOBAL g_cVTGProbeEnabled_%s_%s, data\n"
                            "    dd 0\n",
                            pProvider->pszName, pProbe->pszMangledName);
            cProbes++;
        }
    }
    ScmStreamPrintf(pStrm, "VTG_GLOBAL g_acVTGProbeEnabled_End, data\n");
    if (cProbes >= _32K)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Too many probes: %u (max %u)", cProbes, _32K - 1);


    /*
     * Emit code for the stub functions.
     */
    bool const fWin64   = g_cBits == 64 && (!strcmp(g_pszAssemblerFmtVal, "win64") || !strcmp(g_pszAssemblerFmtVal, "pe64"));
    bool const fElf     = !strcmp(g_pszAssemblerFmtVal, "elf32") || !strcmp(g_pszAssemblerFmtVal, "elf64");
    ScmStreamPrintf(pStrm,
                    "\n"
                    ";\n"
                    "; Prob stubs.\n"
                    ";\n"
                    "BEGINCODE\n"
                    );
    if (g_fProbeFnImported)
        ScmStreamPrintf(pStrm,
                        "EXTERN_IMP2 %s\n"
                        "BEGINCODE ; EXTERN_IMP2 changes section\n",
                        g_pszProbeFnName);
    else
        ScmStreamPrintf(pStrm, "extern NAME(%s)\n", g_pszProbeFnName);

    RTListForEach(&g_ProviderHead, pProvider, VTGPROVIDER, ListEntry)
    {
        RTListForEach(&pProvider->ProbeHead, pProbe, VTGPROBE, ListEntry)
        {
            ScmStreamPrintf(pStrm,
                            "\n"
                            "VTG_GLOBAL VTGProbeStub_%s_%s, function; (VBOXTPGPROBELOC pVTGProbeLoc",
                            pProvider->pszName, pProbe->pszMangledName);
            RTListForEach(&pProbe->ArgHead, pArg, VTGARG, ListEntry)
            {
                ScmStreamPrintf(pStrm, ", %s %s", pArg->pszTracerType, pArg->pszName);
            }
            ScmStreamPrintf(pStrm,
                            ");\n");

            /*
             * Check if the probe in question is enabled.
             */
            if (g_cBits == 32)
                ScmStreamPrintf(pStrm,
                                "        mov     eax, [esp + 4]\n"
                                "        test    byte [eax+3], 0x80 ; fEnabled == true?\n"
                                "        jz      .return            ; jump on false\n");
            else if (fWin64)
                ScmStreamPrintf(pStrm,
                                "        test    byte [rcx+3], 0x80 ; fEnabled == true?\n"
                                "        jz      .return            ; jump on false\n");
            else
                ScmStreamPrintf(pStrm,
                                "        test    byte [rdi+3], 0x80 ; fEnabled == true?\n"
                                "        jz      .return            ; jump on false\n");

            /*
             * Jump to the fire-probe function.
             */
            if (g_cBits == 32)
                ScmStreamPrintf(pStrm, g_fPic && fElf ?
                                "        jmp     %s wrt ..plt\n"
                                : g_fProbeFnImported ?
                                "        mov     ecx, IMP2(%s)\n"
                                "        jmp     ecx\n"
                                :
                                "        jmp     NAME(%s)\n"
                                , g_pszProbeFnName);
            else
                ScmStreamPrintf(pStrm, g_fPic && fElf ?
                                "        jmp     [rel %s wrt ..got]\n"
                                : g_fProbeFnImported ?
                                "        jmp     IMP2(%s)\n"
                                :
                                "        jmp     NAME(%s)\n"
                                , g_pszProbeFnName);

            ScmStreamPrintf(pStrm,
                            ".return:\n"
                            "        ret                        ; The probe was disabled, return\n"
                            "\n");
        }
    }

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE generateObject(const char *pszOutput, const char *pszTempAsm)
{
    if (!pszTempAsm)
    {
        size_t cch = strlen(pszOutput);
        char  *psz = (char *)alloca(cch + sizeof(".asm"));
        memcpy(psz, pszOutput, cch);
        memcpy(psz + cch, ".asm", sizeof(".asm"));
        pszTempAsm = psz;
    }

    RTEXITCODE rcExit = generateFile(pszTempAsm, "assembly", generateAssembly);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = generateInvokeAssembler(pszOutput, pszTempAsm);
    RTFileDelete(pszTempAsm);
    return rcExit;
}


static RTEXITCODE generateProbeDefineName(char *pszBuf, size_t cbBuf, const char *pszProvider, const char *pszProbe)
{
    size_t cbMax = strlen(pszProvider) + 1 + strlen(pszProbe) + 1;
    if (cbMax > cbBuf || cbMax > 80)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Probe '%s' in provider '%s' ends up with a too long defined\n", pszProbe, pszProvider);

    while (*pszProvider)
        *pszBuf++ = RT_C_TO_UPPER(*pszProvider++);

    *pszBuf++ = '_';

    while (*pszProbe)
    {
        if (pszProbe[0] == '_' && pszProbe[1] == '_')
            pszProbe++;
        *pszBuf++ = RT_C_TO_UPPER(*pszProbe++);
    }

    *pszBuf = '\0';
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE generateProviderDefineName(char *pszBuf, size_t cbBuf, const char *pszProvider)
{
    size_t cbMax = strlen(pszProvider) + 1;
    if (cbMax > cbBuf || cbMax > 80)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Provider '%s' ends up with a too long defined\n", pszProvider);

    while (*pszProvider)
        *pszBuf++ = RT_C_TO_UPPER(*pszProvider++);

    *pszBuf = '\0';
    return RTEXITCODE_SUCCESS;
}


/**
 * Called via generateFile to generate the header file.
 *
 * @returns Exit code status.
 * @param   pStrm               The output stream.
 */
static RTEXITCODE generateHeader(PSCMSTREAM pStrm)
{
    /*
     * Calc the double inclusion blocker define and then write the file header.
     */
    char szTmp[4096];
    const char *pszName = RTPathFilename(g_pszScript);
    size_t      cchName = strlen(pszName);
    if (cchName >= sizeof(szTmp) - 64)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "File name is too long '%s'", pszName);
    szTmp[0] = '_';
    szTmp[1] = '_';
    szTmp[2] = '_';
    memcpy(&szTmp[3], pszName, cchName);
    szTmp[3 + cchName + 0] = '_';
    szTmp[3 + cchName + 1] = '_';
    szTmp[3 + cchName + 2] = '_';
    szTmp[3 + cchName + 3] = '\0';
    char *psz = &szTmp[3];
    while (*psz)
    {
        if (!RT_C_IS_ALNUM(*psz) && *psz != '_')
            *psz = '_';
        psz++;
    }

    ScmStreamPrintf(pStrm,
                    "/* $Id: VBoxTpG.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */\n"
                    "/** @file\n"
                    " * Automatically generated from %s.  Do NOT edit!\n"
                    " */\n"
                    "\n"
                    "#ifndef %s\n"
                    "#define %s\n"
                    "#ifndef RT_WITHOUT_PRAGMA_ONCE\n"
                    "# pragma once\n"
                    "#endif\n"
                    "\n"
                    "#include <VBox/VBoxTpG.h>\n"
                    "\n"
                    "#ifndef %s\n"
                    "# error \"Expected '%s' to be defined\"\n"
                    "#endif\n"
                    "\n"
                    "RT_C_DECLS_BEGIN\n"
                    "\n"
                    "#ifdef VBOX_WITH_DTRACE\n"
                    "\n"
                    "# ifdef _MSC_VER\n"
                    "#  pragma data_seg(VTG_LOC_SECT)\n"
                    "#  pragma data_seg()\n"
                    "# endif\n"
                    "\n"
                    ,
                    g_pszScript,
                    szTmp,
                    szTmp,
                    g_pszContextDefine,
                    g_pszContextDefine);

    /*
     * Declare data, code and macros for each probe.
     */
    PVTGPROVIDER pProv;
    PVTGPROBE    pProbe;
    PVTGARG      pArg;
    RTListForEach(&g_ProviderHead, pProv, VTGPROVIDER, ListEntry)
    {
        /* This macro is not available in ring-3 because we don't have
           anything similar available for native dtrace. */
        ScmStreamPrintf(pStrm, "\n\n");
        if (g_fTypeContext != VTG_TYPE_CTX_R3)
        {
            generateProviderDefineName(szTmp, sizeof(szTmp), pProv->pszName);
            ScmStreamPrintf(pStrm,
                            "extern uint32_t const volatile g_cVTGProviderProbesEnabled_%s;\n"
                            "# define %s_ANY_PROBES_ENABLED() \\\n"
                            "    (RT_UNLIKELY(g_cVTGProviderProbesEnabled_%s != 0))\n"
                            "extern uint32_t const volatile g_cVTGProviderSettingsSeqNo_%s;\n"
                            "# define %s_GET_SETTINGS_SEQ_NO() (g_cVTGProviderSettingsSeqNo_%s)\n"
                            "\n",
                            pProv->pszName,
                            szTmp, pProv->pszName,
                            pProv->pszName,
                            szTmp, pProv->pszName);
        }

        RTListForEach(&pProv->ProbeHead, pProbe, VTGPROBE, ListEntry)
        {
            ScmStreamPrintf(pStrm,
                            "extern uint32_t const volatile g_cVTGProbeEnabled_%s_%s;\n"
                            "extern VTGDESCPROBE            g_VTGProbeData_%s_%s;\n"
                            "DECLASM(void)                  VTGProbeStub_%s_%s(PVTGPROBELOC",
                            pProv->pszName, pProbe->pszMangledName,
                            pProv->pszName, pProbe->pszMangledName,
                            pProv->pszName, pProbe->pszMangledName);
            RTListForEach(&pProbe->ArgHead, pArg, VTGARG, ListEntry)
            {
                ScmStreamPrintf(pStrm, ", %s", pArg->pszCtxType);
            }
            generateProbeDefineName(szTmp, sizeof(szTmp), pProv->pszName, pProbe->pszMangledName);
            ScmStreamPrintf(pStrm,
                            ");\n"
                            "# define %s_ENABLED() (RT_UNLIKELY(g_cVTGProbeEnabled_%s_%s != 0))\n"
                            "# define %s_ENABLED_RAW() (g_cVTGProbeEnabled_%s_%s)\n"
                            "# define %s("
                            ,
                            szTmp, pProv->pszName, pProbe->pszMangledName,
                            szTmp, pProv->pszName, pProbe->pszMangledName,
                            szTmp);
            RTListForEach(&pProbe->ArgHead, pArg, VTGARG, ListEntry)
            {
                if (RTListNodeIsFirst(&pProbe->ArgHead, &pArg->ListEntry))
                    ScmStreamPrintf(pStrm, "%s", pArg->pszName);
                else
                    ScmStreamPrintf(pStrm, ", %s", pArg->pszName);
            }
            ScmStreamPrintf(pStrm,
                            ") \\\n"
                            "    do { \\\n"
                            "        if (RT_UNLIKELY(g_cVTGProbeEnabled_%s_%s)) \\\n"
                            "        { \\\n"
                            "            VTG_DECL_VTGPROBELOC(s_VTGProbeLoc) = \\\n"
                            "            { __LINE__, 0, 0, __FUNCTION__, &g_VTGProbeData_%s_%s }; \\\n"
                            "            VTGProbeStub_%s_%s(&s_VTGProbeLoc",
                            pProv->pszName, pProbe->pszMangledName,
                            pProv->pszName, pProbe->pszMangledName,
                            pProv->pszName, pProbe->pszMangledName);
            RTListForEach(&pProbe->ArgHead, pArg, VTGARG, ListEntry)
            {
                ScmStreamPrintf(pStrm, pArg->pszArgPassingFmt, pArg->pszName);
            }
            ScmStreamPrintf(pStrm,
                            "); \\\n"
                            "        } \\\n"
                            "        { \\\n" );
            RTListForEach(&pProbe->ArgHead, pArg, VTGARG, ListEntry)
            {
                if ((pArg->fType & (VTG_TYPE_FIXED_SIZED | VTG_TYPE_AUTO_CONV_PTR)) == VTG_TYPE_FIXED_SIZED)
                    ScmStreamPrintf(pStrm,
                                    "        AssertCompile(sizeof(%s) == %u); \\\n"
                                    "        AssertCompile(sizeof(%s) <= %u); \\\n",
                                    pArg->pszTracerType, pArg->fType & VTG_TYPE_SIZE_MASK,
                                    pArg->pszName, pArg->fType & VTG_TYPE_SIZE_MASK);
                else if (pArg->fType & (VTG_TYPE_POINTER | VTG_TYPE_HC_ARCH_SIZED))
                    ScmStreamPrintf(pStrm,
                                    "        AssertCompile(sizeof(%s) <= sizeof(uintptr_t)); \\\n"
                                    "        AssertCompile(sizeof(%s) <= sizeof(uintptr_t)); \\\n",
                                    pArg->pszName,
                                    pArg->pszTracerType);
            }
            ScmStreamPrintf(pStrm,
                            "        } \\\n"
                            "    } while (0)\n"
                            "\n");
        }
    }

    ScmStreamPrintf(pStrm,
                    "\n"
                    "#else\n"
                    "\n");
    RTListForEach(&g_ProviderHead, pProv, VTGPROVIDER, ListEntry)
    {
        if (g_fTypeContext != VTG_TYPE_CTX_R3)
        {
            generateProviderDefineName(szTmp, sizeof(szTmp), pProv->pszName);
            ScmStreamPrintf(pStrm,
                            "# define %s_ANY_PROBES_ENABLED() (false)\n"
                            "# define %s_GET_SETTINGS_SEQ_NO() UINT32_C(0)\n"
                            "\n",
                            szTmp, szTmp);
        }

        RTListForEach(&pProv->ProbeHead, pProbe, VTGPROBE, ListEntry)
        {
            generateProbeDefineName(szTmp, sizeof(szTmp), pProv->pszName, pProbe->pszMangledName);
            ScmStreamPrintf(pStrm,
                            "# define %s_ENABLED() (false)\n"
                            "# define %s_ENABLED_RAW() UINT32_C(0)\n"
                            "# define %s("
                            , szTmp, szTmp, szTmp);
            RTListForEach(&pProbe->ArgHead, pArg, VTGARG, ListEntry)
            {
                if (RTListNodeIsFirst(&pProbe->ArgHead, &pArg->ListEntry))
                    ScmStreamPrintf(pStrm, "%s", pArg->pszName);
                else
                    ScmStreamPrintf(pStrm, ", %s", pArg->pszName);
            }
            ScmStreamPrintf(pStrm,
                            ") do { } while (0)\n");
        }
    }

    ScmStreamWrite(pStrm, RT_STR_TUPLE("\n"
                                       "#endif\n"
                                       "\n"
                                       "RT_C_DECLS_END\n"
                                       "#endif\n"));
    return RTEXITCODE_SUCCESS;
}


/**
 * Called via generateFile to generate the wrapper header file.
 *
 * @returns Exit code status.
 * @param   pStrm               The output stream.
 */
static RTEXITCODE generateWrapperHeader(PSCMSTREAM pStrm)
{
    /*
     * Calc the double inclusion blocker define and then write the file header.
     */
    char szTmp[4096];
    const char *pszName = RTPathFilename(g_pszScript);
    size_t      cchName = strlen(pszName);
    if (cchName >= sizeof(szTmp) - 64)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "File name is too long '%s'", pszName);
    szTmp[0] = '_';
    szTmp[1] = '_';
    szTmp[2] = '_';
    memcpy(&szTmp[3], pszName, cchName);
    strcpy(&szTmp[3 + cchName ], "___WRAPPER___");
    char *psz = &szTmp[3];
    while (*psz)
    {
        if (!RT_C_IS_ALNUM(*psz) && *psz != '_')
            *psz = '_';
        psz++;
    }

    ScmStreamPrintf(pStrm,
                    "/* $Id: VBoxTpG.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */\n"
                    "/** @file\n"
                    " * Automatically generated from %s.  Do NOT edit!\n"
                    " */\n"
                    "\n"
                    "#ifndef %s\n"
                    "#define %s\n"
                    "\n"
                    "#include <VBox/VBoxTpG.h>\n"
                    "\n"
                    "#ifndef %s\n"
                    "# error \"Expected '%s' to be defined\"\n"
                    "#endif\n"
                    "\n"
                    "#ifdef VBOX_WITH_DTRACE\n"
                    "\n"
                    ,
                    g_pszScript,
                    szTmp,
                    szTmp,
                    g_pszContextDefine,
                    g_pszContextDefine);

    /*
     * Declare macros for each probe.
     */
    PVTGPROVIDER pProv;
    PVTGPROBE    pProbe;
    PVTGARG      pArg;
    RTListForEach(&g_ProviderHead, pProv, VTGPROVIDER, ListEntry)
    {
        RTListForEach(&pProv->ProbeHead, pProbe, VTGPROBE, ListEntry)
        {
            generateProbeDefineName(szTmp, sizeof(szTmp), pProv->pszName, pProbe->pszMangledName);
            ScmStreamPrintf(pStrm,
                            "# define %s("
                            , szTmp);
            RTListForEach(&pProbe->ArgHead, pArg, VTGARG, ListEntry)
            {
                if (RTListNodeIsFirst(&pProbe->ArgHead, &pArg->ListEntry))
                    ScmStreamPrintf(pStrm, "%s", pArg->pszName);
                else
                    ScmStreamPrintf(pStrm, ", %s", pArg->pszName);
            }
            ScmStreamPrintf(pStrm,
                            ") \\\n"
                            "    do { \\\n"
                            "        if (RT_UNLIKELY(%s_ENABLED())) \\\n"
                            "        { \\\n"
                            "            %s_ORIGINAL("
                            , szTmp, szTmp);
            RTListForEach(&pProbe->ArgHead, pArg, VTGARG, ListEntry)
            {
                const char *pszFmt = pArg->pszArgPassingFmt;
                if (pArg->fType & VTG_TYPE_AUTO_CONV_PTR)
                {
                    /* Casting is required.  ASSUMES sizeof(RTR0PTR) == sizeof(RTR3PTR) - safe! */
                    pszFmt += sizeof(", ") - 1;
                    if (RTListNodeIsFirst(&pProbe->ArgHead, &pArg->ListEntry))
                        ScmStreamPrintf(pStrm, "(%s)%M", pArg->pszTracerType, pszFmt, pArg->pszName);
                    else
                        ScmStreamPrintf(pStrm, ", (%s)%M", pArg->pszTracerType, pszFmt, pArg->pszName);
                }
                else if (pArg->fType & VTG_TYPE_CONST_CHAR_PTR)
                {
                    /* Casting from 'const char *' (probe) to 'char *' (dtrace) is required to shut up warnings. */
                    pszFmt += sizeof(", ") - 1;
                    if (RTListNodeIsFirst(&pProbe->ArgHead, &pArg->ListEntry))
                        ScmStreamPrintf(pStrm, "(char *)%M", pszFmt, pArg->pszName);
                    else
                        ScmStreamPrintf(pStrm, ", (char *)%M", pszFmt, pArg->pszName);
                }
                else
                {
                    if (RTListNodeIsFirst(&pProbe->ArgHead, &pArg->ListEntry))
                        ScmStreamPrintf(pStrm, pArg->pszArgPassingFmt + sizeof(", ") - 1, pArg->pszName);
                    else
                        ScmStreamPrintf(pStrm, pArg->pszArgPassingFmt, pArg->pszName);
                }
            }
            ScmStreamPrintf(pStrm,
                            "); \\\n"
                            "        } \\\n"
                            "    } while (0)\n"
                            "\n");
        }
    }

    ScmStreamPrintf(pStrm,
                    "\n"
                    "#else\n"
                    "\n");
    RTListForEach(&g_ProviderHead, pProv, VTGPROVIDER, ListEntry)
    {
        RTListForEach(&pProv->ProbeHead, pProbe, VTGPROBE, ListEntry)
        {
            generateProbeDefineName(szTmp, sizeof(szTmp), pProv->pszName, pProbe->pszMangledName);
            ScmStreamPrintf(pStrm,
                            "# define %s("
                            , szTmp);
            RTListForEach(&pProbe->ArgHead, pArg, VTGARG, ListEntry)
            {
                if (RTListNodeIsFirst(&pProbe->ArgHead, &pArg->ListEntry))
                    ScmStreamPrintf(pStrm, "%s", pArg->pszName);
                else
                    ScmStreamPrintf(pStrm, ", %s", pArg->pszName);
            }
            ScmStreamPrintf(pStrm,
                            ") do { } while (0)\n");
        }
    }

    ScmStreamWrite(pStrm, RT_STR_TUPLE("\n"
                                       "#endif\n"
                                       "\n"
                                       "#endif\n"));
    return RTEXITCODE_SUCCESS;
}


/**
 * Parser error with line and position.
 *
 * @returns RTEXITCODE_FAILURE.
 * @param   pStrm       The stream.
 * @param   fAbs        Absolute or relative offset.
 * @param   offSeek     When @a fAbs is @c false, the offset from the current
 *                      position to the point of failure.  When @a fAbs is @c
 *                      true, it's the absolute position.
 * @param   pszFormat   The message format string.
 * @param   va          Format arguments.
 */
static RTEXITCODE parseErrorExV(PSCMSTREAM pStrm, bool fAbs, size_t offSeek, const char *pszFormat, va_list va)
{
    if (fAbs)
        ScmStreamSeekAbsolute(pStrm, offSeek);
    else if (offSeek != 0)
        ScmStreamSeekRelative(pStrm, -(ssize_t)offSeek);
    size_t const off     = ScmStreamTell(pStrm);
    size_t const iLine   = ScmStreamTellLine(pStrm);
    ScmStreamSeekByLine(pStrm, iLine);
    size_t const offLine = ScmStreamTell(pStrm);

    va_list va2;
    va_copy(va2, va);
    RTPrintf("%s:%d:%zd: error: %N.\n", g_pszScript, iLine + 1, off - offLine + 1, pszFormat, &va2);
    va_end(va2);

    size_t cchLine;
    SCMEOL enmEof;
    const char *pszLine = ScmStreamGetLineByNo(pStrm, iLine, &cchLine, &enmEof);
    if (pszLine)
        RTPrintf("  %.*s\n"
                 "  %*s^\n",
                 cchLine, pszLine, off - offLine, "");
    return RTEXITCODE_FAILURE;
}


/**
 * Parser error with line and position.
 *
 * @returns RTEXITCODE_FAILURE.
 * @param   pStrm       The stream.
 * @param   off         The offset from the current position to the point of
 *                      failure.
 * @param   pszFormat   The message format string.
 * @param   ...         Format arguments.
 */
static RTEXITCODE parseError(PSCMSTREAM pStrm, size_t off, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTEXITCODE rcExit = parseErrorExV(pStrm, false, off, pszFormat, va);
    va_end(va);
    return rcExit;
}


/**
 * Parser error with line and position, absolute version.
 *
 * @returns RTEXITCODE_FAILURE.
 * @param   pStrm       The stream.
 * @param   off         The offset from the current position to the point of
 *                      failure.
 * @param   pszFormat   The message format string.
 * @param   ...         Format arguments.
 */
static RTEXITCODE parseErrorAbs(PSCMSTREAM pStrm, size_t off, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTEXITCODE rcExit = parseErrorExV(pStrm, true /*fAbs*/, off, pszFormat, va);
    va_end(va);
    return rcExit;
}


/**
 * Parser warning with line and position.
 *
 * @param   pStrm       The stream.
 * @param   fAbs        Absolute or relative offset.
 * @param   offSeek     When @a fAbs is @c false, the offset from the current
 *                      position to the point of failure.  When @a fAbs is @c
 *                      true, it's the absolute position.
 * @param   pszFormat   The message format string.
 * @param   va          Format arguments.
 */
static void parseWarnExV(PSCMSTREAM pStrm, bool fAbs, size_t offSeek, const char *pszFormat, va_list va)
{
    /* Save the stream position. */
    size_t const offOrg = ScmStreamTell(pStrm);

    if (fAbs)
        ScmStreamSeekAbsolute(pStrm, offSeek);
    else if (offSeek != 0)
        ScmStreamSeekRelative(pStrm, -(ssize_t)offSeek);
    size_t const off     = ScmStreamTell(pStrm);
    size_t const iLine   = ScmStreamTellLine(pStrm);
    ScmStreamSeekByLine(pStrm, iLine);
    size_t const offLine = ScmStreamTell(pStrm);

    va_list va2;
    va_copy(va2, va);
    RTPrintf("%s:%d:%zd: warning: %N.\n", g_pszScript, iLine + 1, off - offLine + 1, pszFormat, &va2);
    va_end(va2);

    size_t cchLine;
    SCMEOL enmEof;
    const char *pszLine = ScmStreamGetLineByNo(pStrm, iLine, &cchLine, &enmEof);
    if (pszLine)
        RTPrintf("  %.*s\n"
                 "  %*s^\n",
                 cchLine, pszLine, off - offLine, "");

    /* restore the position. */
    int rc = ScmStreamSeekAbsolute(pStrm, offOrg);
    AssertRC(rc);
}

#if 0 /* unused */
/**
 * Parser warning with line and position.
 *
 * @param   pStrm       The stream.
 * @param   off         The offset from the current position to the point of
 *                      failure.
 * @param   pszFormat   The message format string.
 * @param   ...         Format arguments.
 */
static void parseWarn(PSCMSTREAM pStrm, size_t off, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    parseWarnExV(pStrm, false, off, pszFormat, va);
    va_end(va);
}
#endif /* unused */

/**
 * Parser warning with line and position, absolute version.
 *
 * @param   pStrm       The stream.
 * @param   off         The offset from the current position to the point of
 *                      failure.
 * @param   pszFormat   The message format string.
 * @param   ...         Format arguments.
 */
static void parseWarnAbs(PSCMSTREAM pStrm, size_t off, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    parseWarnExV(pStrm, true /*fAbs*/, off, pszFormat, va);
    va_end(va);
}


/**
 * Handles a C++ one line comment.
 *
 * @returns Exit code.
 * @param   pStrm               The stream.
 */
static RTEXITCODE parseOneLineComment(PSCMSTREAM pStrm)
{
    ScmStreamSeekByLine(pStrm, ScmStreamTellLine(pStrm) + 1);
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles a multi-line C/C++ comment.
 *
 * @returns Exit code.
 * @param   pStrm               The stream.
 */
static RTEXITCODE parseMultiLineComment(PSCMSTREAM pStrm)
{
    unsigned ch;
    while ((ch = ScmStreamGetCh(pStrm)) != ~(unsigned)0)
    {
        if (ch == '*')
        {
            do
                ch = ScmStreamGetCh(pStrm);
            while (ch == '*');
            if (ch == '/')
                return RTEXITCODE_SUCCESS;
        }
    }

    parseError(pStrm, 1, "Expected end of comment, got end of file");
    return RTEXITCODE_FAILURE;
}


/**
 * Skips spaces and comments.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE.
 * @param   pStrm               The stream..
 */
static RTEXITCODE parseSkipSpacesAndComments(PSCMSTREAM pStrm)
{
    unsigned ch;
    while ((ch = ScmStreamPeekCh(pStrm)) != ~(unsigned)0)
    {
        if (!RT_C_IS_SPACE(ch) && ch != '/')
            return RTEXITCODE_SUCCESS;
        unsigned ch2 = ScmStreamGetCh(pStrm); AssertBreak(ch == ch2); NOREF(ch2);
        if (ch == '/')
        {
            ch = ScmStreamGetCh(pStrm);
            RTEXITCODE rcExit;
            if (ch == '*')
                rcExit = parseMultiLineComment(pStrm);
            else if (ch == '/')
                rcExit = parseOneLineComment(pStrm);
            else
                rcExit = parseError(pStrm, 2, "Unexpected character");
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;
        }
    }

    return parseError(pStrm, 0, "Unexpected end of file");
}


/**
 * Skips spaces and comments, returning the next character.
 *
 * @returns Next non-space-non-comment character. ~(unsigned)0 on EOF or
 *          failure.
 * @param   pStrm               The stream.
 */
static unsigned parseGetNextNonSpaceNonCommentCh(PSCMSTREAM pStrm)
{
    unsigned ch;
    while ((ch = ScmStreamGetCh(pStrm)) != ~(unsigned)0)
    {
        if (!RT_C_IS_SPACE(ch) && ch != '/')
            return ch;
        if (ch == '/')
        {
            ch = ScmStreamGetCh(pStrm);
            RTEXITCODE rcExit;
            if (ch == '*')
                rcExit = parseMultiLineComment(pStrm);
            else if (ch == '/')
                rcExit = parseOneLineComment(pStrm);
            else
                rcExit = parseError(pStrm, 2, "Unexpected character");
            if (rcExit != RTEXITCODE_SUCCESS)
                return ~(unsigned)0;
        }
    }

    parseError(pStrm, 0, "Unexpected end of file");
    return ~(unsigned)0;
}


/**
 * Get the next non-space-non-comment character on a preprocessor line.
 *
 * @returns The next character. On error message and ~(unsigned)0.
 * @param   pStrm               The stream.
 */
static unsigned parseGetNextNonSpaceNonCommentChOnPpLine(PSCMSTREAM pStrm)
{
    size_t   off = ScmStreamTell(pStrm) - 1;
    unsigned ch;
    while ((ch = ScmStreamGetCh(pStrm)) != ~(unsigned)0)
    {
        if (RT_C_IS_SPACE(ch))
        {
            if (ch == '\n' || ch == '\r')
            {
                parseErrorAbs(pStrm, off, "Invalid preprocessor statement");
                break;
            }
        }
        else if (ch == '\\')
        {
            size_t off2 = ScmStreamTell(pStrm) - 1;
            ch = ScmStreamGetCh(pStrm);
            if (ch == '\r')
                ch = ScmStreamGetCh(pStrm);
            if (ch != '\n')
            {
                parseErrorAbs(pStrm, off2, "Expected new line");
                break;
            }
        }
        else
            return ch;
    }
    return ~(unsigned)0;
}



/**
 * Skips spaces and comments.
 *
 * @returns Same as ScmStreamCGetWord
 * @param   pStrm               The stream..
 * @param   pcchWord            Where to return the length.
 */
static const char *parseGetNextCWord(PSCMSTREAM pStrm, size_t *pcchWord)
{
    if (parseSkipSpacesAndComments(pStrm) != RTEXITCODE_SUCCESS)
        return NULL;
    return ScmStreamCGetWord(pStrm, pcchWord);
}



/**
 * Parses interface stability.
 *
 * @returns Interface stability if parsed correctly, otherwise error message and
 *          kVTGStability_Invalid.
 * @param   pStrm               The stream.
 * @param   ch                  The first character in the stability spec.
 */
static kVTGStability parseStability(PSCMSTREAM pStrm, unsigned ch)
{
    switch (ch)
    {
        case 'E':
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("External")))
                return kVTGStability_External;
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Evolving")))
                return kVTGStability_Evolving;
            break;
        case 'I':
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Internal")))
                return kVTGStability_Internal;
            break;
        case 'O':
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Obsolete")))
                return kVTGStability_Obsolete;
            break;
        case 'P':
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Private")))
                return kVTGStability_Private;
            break;
        case 'S':
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Stable")))
                return kVTGStability_Stable;
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Standard")))
                return kVTGStability_Standard;
            break;
        case 'U':
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Unstable")))
                return kVTGStability_Unstable;
            break;
    }
    parseError(pStrm, 1, "Unknown stability specifier");
    return kVTGStability_Invalid;
}


/**
 * Parses data depndency class.
 *
 * @returns Data dependency class if parsed correctly, otherwise error message
 *          and kVTGClass_Invalid.
 * @param   pStrm               The stream.
 * @param   ch                  The first character in the stability spec.
 */
static kVTGClass parseDataDepClass(PSCMSTREAM pStrm, unsigned ch)
{
    switch (ch)
    {
        case 'C':
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Common")))
                return kVTGClass_Common;
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Cpu")))
                return kVTGClass_Cpu;
            break;
        case 'G':
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Group")))
                return kVTGClass_Group;
            break;
        case 'I':
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Isa")))
                return kVTGClass_Isa;
            break;
        case 'P':
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Platform")))
                return kVTGClass_Platform;
            break;
        case 'U':
            if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("Unknown")))
                return kVTGClass_Unknown;
            break;
    }
    parseError(pStrm, 1, "Unknown data dependency class specifier");
    return kVTGClass_Invalid;
}

/**
 * Parses a pragma D attributes statement.
 *
 * @returns Suitable exit code, errors message already written on failure.
 * @param   pStrm               The stream.
 */
static RTEXITCODE parsePragmaDAttributes(PSCMSTREAM pStrm)
{
    /*
     * "CodeStability/DataStability/DataDepClass" - no spaces allowed.
     */
    unsigned ch = parseGetNextNonSpaceNonCommentChOnPpLine(pStrm);
    if (ch == ~(unsigned)0)
        return RTEXITCODE_FAILURE;

    kVTGStability enmCode = parseStability(pStrm, ch);
    if (enmCode == kVTGStability_Invalid)
        return RTEXITCODE_FAILURE;
    ch = ScmStreamGetCh(pStrm);
    if (ch != '/')
        return parseError(pStrm, 1, "Expected '/' following the code stability specifier");

    kVTGStability enmData = parseStability(pStrm, ScmStreamGetCh(pStrm));
    if (enmData == kVTGStability_Invalid)
        return RTEXITCODE_FAILURE;
    ch = ScmStreamGetCh(pStrm);
    if (ch != '/')
        return parseError(pStrm, 1, "Expected '/' following the data stability specifier");

    kVTGClass enmDataDep =  parseDataDepClass(pStrm, ScmStreamGetCh(pStrm));
    if (enmDataDep == kVTGClass_Invalid)
        return RTEXITCODE_FAILURE;

    /*
     * Expecting 'provider' followed by the name of an provider defined earlier.
     */
    ch = parseGetNextNonSpaceNonCommentChOnPpLine(pStrm);
    if (ch == ~(unsigned)0)
        return RTEXITCODE_FAILURE;
    if (ch != 'p' || !ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("provider")))
        return parseError(pStrm, 1, "Expected 'provider'");

    size_t      cchName;
    const char *pszName = parseGetNextCWord(pStrm, &cchName);
    if (!pszName)
        return parseError(pStrm, 1, "Expected provider name");

    PVTGPROVIDER pProv;
    RTListForEach(&g_ProviderHead, pProv, VTGPROVIDER, ListEntry)
    {
        if (   !strncmp(pProv->pszName, pszName, cchName)
            && pProv->pszName[cchName] == '\0')
            break;
    }
    if (RTListNodeIsDummy(&g_ProviderHead, pProv, VTGPROVIDER, ListEntry))
        return parseError(pStrm, cchName, "Provider not found");

    /*
     * Which aspect of the provider?
     */
    size_t      cchAspect;
    const char *pszAspect = parseGetNextCWord(pStrm, &cchAspect);
    if (!pszAspect)
        return parseError(pStrm, 1, "Expected provider aspect");

    PVTGATTRS pAttrs;
    if (cchAspect == 8 && !memcmp(pszAspect, "provider", 8))
        pAttrs = &pProv->AttrSelf;
    else if (cchAspect == 8 && !memcmp(pszAspect, "function", 8))
        pAttrs = &pProv->AttrFunctions;
    else if (cchAspect == 6 && !memcmp(pszAspect, "module", 6))
        pAttrs = &pProv->AttrModules;
    else if (cchAspect == 4 && !memcmp(pszAspect, "name", 4))
        pAttrs = &pProv->AttrName;
    else if (cchAspect == 4 && !memcmp(pszAspect, "args", 4))
        pAttrs = &pProv->AttrArguments;
    else
        return parseError(pStrm, cchAspect, "Unknown aspect");

    if (pAttrs->enmCode != kVTGStability_Invalid)
        return parseError(pStrm, cchAspect, "You have already specified these attributes");

    pAttrs->enmCode     = enmCode;
    pAttrs->enmData     = enmData;
    pAttrs->enmDataDep  = enmDataDep;
    return RTEXITCODE_SUCCESS;
}

/**
 * Parses a D pragma statement.
 *
 * @returns Suitable exit code, errors message already written on failure.
 * @param   pStrm               The stream.
 */
static RTEXITCODE parsePragma(PSCMSTREAM pStrm)
{
    RTEXITCODE rcExit;
    unsigned   ch = parseGetNextNonSpaceNonCommentChOnPpLine(pStrm);
    if (ch == ~(unsigned)0)
        rcExit = RTEXITCODE_FAILURE;
    else if (ch == 'D' && ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("D")))
    {
        ch = parseGetNextNonSpaceNonCommentChOnPpLine(pStrm);
        if (ch == ~(unsigned)0)
            rcExit = RTEXITCODE_FAILURE;
        else if (ch == 'a' && ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("attributes")))
            rcExit = parsePragmaDAttributes(pStrm);
        else
            rcExit = parseError(pStrm, 1, "Unknown pragma D");
    }
    else
        rcExit = parseError(pStrm, 1, "Unknown pragma");
    return rcExit;
}


/**
 * Classifies the given type expression.
 *
 * @return  Type flags.
 * @param   pszType         The type expression.
 * @param   pStrm           The input stream (for errors + warnings).
 * @param   offSrc          The absolute source position of this expression (for
 *                          warnings).
 */
static uint32_t parseTypeExpression(const char *pszType, PSCMSTREAM pStrm, size_t offSrc)
{
    size_t cchType = strlen(pszType);
#define MY_STRMATCH(a_sz)  (cchType == sizeof(a_sz) - 1 && !memcmp(a_sz, pszType, sizeof(a_sz) - 1))

    /*
     * Try detect pointers.
     */
    if (pszType[cchType - 1] == '*')
    {
        if (MY_STRMATCH("const char *")) return VTG_TYPE_POINTER | VTG_TYPE_CONST_CHAR_PTR;
        return VTG_TYPE_POINTER;
    }
    if (pszType[cchType - 1] == '&')
    {
        parseWarnAbs(pStrm, offSrc, "Please avoid using references like '%s' for probe arguments!", pszType);
        return VTG_TYPE_POINTER;
    }

    /*
     * Standard integer types and IPRT variants.
     * It's important that we catch all types larger than 32-bit here or we'll
     * screw up the probe argument handling.
     */
    if (MY_STRMATCH("int"))             return VTG_TYPE_FIXED_SIZED | sizeof(int)   | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("uintptr_t"))       return VTG_TYPE_HC_ARCH_SIZED | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("intptr_t"))        return VTG_TYPE_HC_ARCH_SIZED | VTG_TYPE_SIGNED;

    //if (MY_STRMATCH("uint128_t"))       return VTG_TYPE_FIXED_SIZED | sizeof(uint128_t) | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("uint64_t"))        return VTG_TYPE_FIXED_SIZED | sizeof(uint64_t)  | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("uint32_t"))        return VTG_TYPE_FIXED_SIZED | sizeof(uint32_t)  | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("uint16_t"))        return VTG_TYPE_FIXED_SIZED | sizeof(uint16_t)  | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("uint8_t"))         return VTG_TYPE_FIXED_SIZED | sizeof(uint8_t)   | VTG_TYPE_UNSIGNED;

    //if (MY_STRMATCH("int128_t"))        return VTG_TYPE_FIXED_SIZED | sizeof(int128_t)  | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("int64_t"))         return VTG_TYPE_FIXED_SIZED | sizeof(int64_t)   | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("int32_t"))         return VTG_TYPE_FIXED_SIZED | sizeof(int32_t)   | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("int16_t"))         return VTG_TYPE_FIXED_SIZED | sizeof(int16_t)   | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("int8_t"))          return VTG_TYPE_FIXED_SIZED | sizeof(int8_t)    | VTG_TYPE_SIGNED;

    if (MY_STRMATCH("RTUINT64U"))       return VTG_TYPE_FIXED_SIZED | sizeof(uint64_t)  | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("RTUINT32U"))       return VTG_TYPE_FIXED_SIZED | sizeof(uint32_t)  | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("RTUINT16U"))       return VTG_TYPE_FIXED_SIZED | sizeof(uint16_t)  | VTG_TYPE_UNSIGNED;

    if (MY_STRMATCH("RTMSINTERVAL"))    return VTG_TYPE_FIXED_SIZED | sizeof(RTMSINTERVAL) | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("RTTIMESPEC"))      return VTG_TYPE_FIXED_SIZED | sizeof(RTTIMESPEC)   | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("RTPROCESS"))       return VTG_TYPE_FIXED_SIZED | sizeof(RTPROCESS)    | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("RTHCPHYS"))        return VTG_TYPE_FIXED_SIZED | sizeof(RTHCPHYS)     | VTG_TYPE_UNSIGNED | VTG_TYPE_PHYS;

    if (MY_STRMATCH("RTR3PTR"))         return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R3;
    if (MY_STRMATCH("RTR0PTR"))         return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R0;
    if (MY_STRMATCH("RTRCPTR"))         return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_RC;
    if (MY_STRMATCH("RTHCPTR"))         return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R3 | VTG_TYPE_CTX_R0;

    if (MY_STRMATCH("RTR3UINTPTR"))     return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R3 | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("RTR0UINTPTR"))     return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R0 | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("RTRCUINTPTR"))     return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_RC | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("RTHCUINTPTR"))     return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R3 | VTG_TYPE_CTX_R0 | VTG_TYPE_UNSIGNED;

    if (MY_STRMATCH("RTR3INTPTR"))      return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R3 | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("RTR0INTPTR"))      return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R0 | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("RTRCINTPTR"))      return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_RC | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("RTHCINTPTR"))      return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R3 | VTG_TYPE_CTX_R0 | VTG_TYPE_SIGNED;

    if (MY_STRMATCH("RTUINTPTR"))       return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R3 | VTG_TYPE_CTX_R0 | VTG_TYPE_CTX_RC | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("RTINTPTR"))        return VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R3 | VTG_TYPE_CTX_R0 | VTG_TYPE_CTX_RC | VTG_TYPE_SIGNED;

    if (MY_STRMATCH("RTHCUINTREG"))     return VTG_TYPE_HC_ARCH_SIZED | VTG_TYPE_CTX_R3 | VTG_TYPE_CTX_R0 | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("RTR3UINTREG"))     return VTG_TYPE_HC_ARCH_SIZED | VTG_TYPE_CTX_R3 | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("RTR0UINTREG"))     return VTG_TYPE_HC_ARCH_SIZED | VTG_TYPE_CTX_R3 | VTG_TYPE_UNSIGNED;

    if (MY_STRMATCH("RTGCUINTREG"))     return VTG_TYPE_FIXED_SIZED | sizeof(RTGCUINTREG) | VTG_TYPE_UNSIGNED | VTG_TYPE_CTX_GST;
    if (MY_STRMATCH("RTGCPTR"))         return VTG_TYPE_FIXED_SIZED | sizeof(RTGCPTR)     | VTG_TYPE_UNSIGNED | VTG_TYPE_CTX_GST;
    if (MY_STRMATCH("RTGCINTPTR"))      return VTG_TYPE_FIXED_SIZED | sizeof(RTGCUINTPTR) | VTG_TYPE_SIGNED   | VTG_TYPE_CTX_GST;
    if (MY_STRMATCH("RTGCPTR32"))       return VTG_TYPE_FIXED_SIZED | sizeof(RTGCPTR32)   | VTG_TYPE_SIGNED   | VTG_TYPE_CTX_GST;
    if (MY_STRMATCH("RTGCPTR64"))       return VTG_TYPE_FIXED_SIZED | sizeof(RTGCPTR64)   | VTG_TYPE_SIGNED   | VTG_TYPE_CTX_GST;
    if (MY_STRMATCH("RTGCPHYS"))        return VTG_TYPE_FIXED_SIZED | sizeof(RTGCPHYS)    | VTG_TYPE_UNSIGNED | VTG_TYPE_PHYS | VTG_TYPE_CTX_GST;
    if (MY_STRMATCH("RTGCPHYS32"))      return VTG_TYPE_FIXED_SIZED | sizeof(RTGCPHYS32)  | VTG_TYPE_UNSIGNED | VTG_TYPE_PHYS | VTG_TYPE_CTX_GST;
    if (MY_STRMATCH("RTGCPHYS64"))      return VTG_TYPE_FIXED_SIZED | sizeof(RTGCPHYS64)  | VTG_TYPE_UNSIGNED | VTG_TYPE_PHYS | VTG_TYPE_CTX_GST;

    /*
     * The special VBox types.
     */
    if (MY_STRMATCH("PVM"))             return VTG_TYPE_POINTER;
    if (MY_STRMATCH("PVMCPU"))          return VTG_TYPE_POINTER;
    if (MY_STRMATCH("PCPUMCTX"))        return VTG_TYPE_POINTER;

    /*
     * Preaching time.
     */
    if (   MY_STRMATCH("unsigned long")
        || MY_STRMATCH("unsigned long long")
        || MY_STRMATCH("signed long")
        || MY_STRMATCH("signed long long")
        || MY_STRMATCH("long")
        || MY_STRMATCH("long long")
        || MY_STRMATCH("char")
        || MY_STRMATCH("signed char")
        || MY_STRMATCH("unsigned char")
        || MY_STRMATCH("double")
        || MY_STRMATCH("long double")
        || MY_STRMATCH("float")
       )
    {
        RTMsgError("Please do NOT use the type '%s' for probe arguments!", pszType);
        g_cTypeErrors++;
        return 0;
    }

    if (   MY_STRMATCH("unsigned")
        || MY_STRMATCH("signed")
        || MY_STRMATCH("signed int")
        || MY_STRMATCH("unsigned int")
        || MY_STRMATCH("short")
        || MY_STRMATCH("signed short")
        || MY_STRMATCH("unsigned short")
       )
        parseWarnAbs(pStrm, offSrc, "Please avoid using the type '%s' for probe arguments!", pszType);
    if (MY_STRMATCH("unsigned"))        return VTG_TYPE_FIXED_SIZED | sizeof(int)   | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("unsigned int"))    return VTG_TYPE_FIXED_SIZED | sizeof(int)   | VTG_TYPE_UNSIGNED;
    if (MY_STRMATCH("signed"))          return VTG_TYPE_FIXED_SIZED | sizeof(int)   | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("signed int"))      return VTG_TYPE_FIXED_SIZED | sizeof(int)   | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("short"))           return VTG_TYPE_FIXED_SIZED | sizeof(short) | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("signed short"))    return VTG_TYPE_FIXED_SIZED | sizeof(short) | VTG_TYPE_SIGNED;
    if (MY_STRMATCH("unsigned short"))  return VTG_TYPE_FIXED_SIZED | sizeof(short) | VTG_TYPE_UNSIGNED;

    /*
     * What we haven't caught by now is either unknown to us or wrong.
     */
    if (pszType[0] == 'P')
    {
        RTMsgError("Type '%s' looks like a pointer typedef, please do NOT use those "
                   "but rather the non-pointer typedef or struct with '*'",
                   pszType);
        g_cTypeErrors++;
        return VTG_TYPE_POINTER;
    }

    RTMsgError("Don't know '%s' - please change or fix VBoxTpG", pszType);
    g_cTypeErrors++;

#undef MY_STRCMP
    return 0;
}


/**
 * Initializes the members of an argument.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pProbe              The probe.
 * @param   pArg                The argument.
 * @param   pStrm               The input stream (for errors + warnings).
 * @param   pchType             The type.
 * @param   cchType             The type length.
 * @param   pchName             The name.
 * @param   cchName             The name length.
 */
static RTEXITCODE parseInitArgument(PVTGPROBE pProbe, PVTGARG pArg, PSCMSTREAM pStrm,
                                    char *pchType, size_t cchType, char *pchName, size_t cchName)
{
    Assert(!pArg->pszName); Assert(!pArg->pszTracerType); Assert(!pArg->pszCtxType); Assert(!pArg->fType);

    pArg->pszArgPassingFmt  = ", %s";
    pArg->pszName           = RTStrDupN(pchName, cchName);
    pArg->pszTracerType     = strtabInsertN(pchType, cchType);
    if (!pArg->pszTracerType || !pArg->pszName)
        return parseError(pStrm, 1, "Out of memory");
    pArg->fType             = parseTypeExpression(pArg->pszTracerType, pStrm, pArg->offSrc);

    if (   (pArg->fType & VTG_TYPE_POINTER)
        && !(g_fTypeContext & VTG_TYPE_CTX_R0) )
    {
        pArg->fType &= ~VTG_TYPE_POINTER;
        if (   !strcmp(pArg->pszTracerType, "struct VM *")          || !strcmp(pArg->pszTracerType, "PVM")
            || !strcmp(pArg->pszTracerType, "struct VMCPU *")       || !strcmp(pArg->pszTracerType, "PVMCPU")
            || !strcmp(pArg->pszTracerType, "struct CPUMCTX *")     || !strcmp(pArg->pszTracerType, "PCPUMCTX")
            )
        {
            pArg->fType |= VTG_TYPE_CTX_POINTER | VTG_TYPE_CTX_R0
                         | VTG_TYPE_FIXED_SIZED | (g_cHostBits / 8)
                         | VTG_TYPE_AUTO_CONV_PTR;
            pArg->pszCtxType = RTStrDup("RTR0PTR");

            if (!strcmp(pArg->pszTracerType, "struct VM *")         || !strcmp(pArg->pszTracerType, "PVM"))
                pArg->pszArgPassingFmt = ", VTG_VM_TO_R0(%s)";
            else if (!strcmp(pArg->pszTracerType, "struct VMCPU *") || !strcmp(pArg->pszTracerType, "PVMCPU"))
                pArg->pszArgPassingFmt = ", VTG_VMCPU_TO_R0(%s)";
            else
            {
                PVTGARG pFirstArg = RTListGetFirst(&pProbe->ArgHead, VTGARG, ListEntry);
                if (   !pFirstArg
                    || pFirstArg == pArg
                    || strcmp(pFirstArg->pszName, "a_pVCpu")
                    || (   strcmp(pFirstArg->pszTracerType, "struct VMCPU *")
                        && strcmp(pFirstArg->pszTracerType, "PVMCPU *")) )
                    return parseError(pStrm, 1, "The automatic ring-0 pointer conversion requires 'a_pVCpu' with type 'struct VMCPU *' as the first argument");

                if (!strcmp(pArg->pszTracerType, "struct CPUMCTX *")|| !strcmp(pArg->pszTracerType, "PCPUMCTX"))
                    pArg->pszArgPassingFmt = ", VTG_CPUMCTX_TO_R0(a_pVCpu, %s)";
                else
                    pArg->pszArgPassingFmt = ", VBoxTpG-Is-Buggy!!";
            }
        }
        else
        {
            pArg->fType |= VTG_TYPE_CTX_POINTER | g_fTypeContext | VTG_TYPE_FIXED_SIZED | (g_cBits / 8);
            pArg->pszCtxType = RTStrDupN(pchType, cchType);
        }
    }
    else
        pArg->pszCtxType = RTStrDupN(pchType, cchType);
    if (!pArg->pszCtxType)
        return parseError(pStrm, 1, "Out of memory");

    return RTEXITCODE_SUCCESS;
}


/**
 * Unmangles the probe name.
 *
 * This involves translating double underscore to dash.
 *
 * @returns Pointer to the unmangled name in the string table.
 * @param   pszMangled          The mangled name.
 */
static const char *parseUnmangleProbeName(const char *pszMangled)
{
    size_t      cchMangled = strlen(pszMangled);
    char       *pszTmp     = (char *)alloca(cchMangled + 2);
    const char *pszSrc     = pszMangled;
    char       *pszDst     = pszTmp;

    while (*pszSrc)
    {
        if (pszSrc[0] == '_' && pszSrc[1] == '_' && pszSrc[2] != '_')
        {
            *pszDst++ = '-';
            pszSrc   += 2;
        }
        else
            *pszDst++ = *pszSrc++;
    }
    *pszDst = '\0';

    return strtabInsertN(pszTmp, pszDst - pszTmp);
}


/**
 * Parses a D probe statement.
 *
 * @returns Suitable exit code, errors message already written on failure.
 * @param   pStrm               The stream.
 * @param   pProv               The provider being parsed.
 */
static RTEXITCODE parseProbe(PSCMSTREAM pStrm, PVTGPROVIDER pProv)
{
    size_t const iProbeLine = ScmStreamTellLine(pStrm);

    /*
     * Next up is a name followed by an opening parenthesis.
     */
    size_t      cchProbe;
    const char *pszProbe = parseGetNextCWord(pStrm, &cchProbe);
    if (!pszProbe)
        return parseError(pStrm, 1, "Expected a probe name starting with an alphabetical character");
    unsigned ch = parseGetNextNonSpaceNonCommentCh(pStrm);
    if (ch != '(')
        return parseError(pStrm, 1, "Expected '(' after the probe name");

    /*
     * Create a probe instance.
     */
    PVTGPROBE pProbe = (PVTGPROBE)RTMemAllocZ(sizeof(*pProbe));
    if (!pProbe)
        return parseError(pStrm, 0, "Out of memory");
    RTListInit(&pProbe->ArgHead);
    RTListAppend(&pProv->ProbeHead, &pProbe->ListEntry);
    pProbe->offArgList     = UINT32_MAX;
    pProbe->iLine          = iProbeLine;
    pProbe->pszMangledName = RTStrDupN(pszProbe, cchProbe);
    if (!pProbe->pszMangledName)
        return parseError(pStrm, 0, "Out of memory");
    pProbe->pszUnmangledName = parseUnmangleProbeName(pProbe->pszMangledName);
    if (!pProbe->pszUnmangledName)
        return parseError(pStrm, 0, "Out of memory");

    /*
     * Parse loop for the argument.
     */
    PVTGARG pArg    = NULL;
    size_t  cchName = 0;
    size_t  cchArg  = 0;
    char    szArg[4096];
    for (;;)
    {
        ch = parseGetNextNonSpaceNonCommentCh(pStrm);
        switch (ch)
        {
            case ')':
            case ',':
            {
                /* commit the argument */
                if (pArg)
                {
                    if (!cchName)
                        return parseError(pStrm, 1, "Argument has no name");
                    if (cchArg - cchName - 1 >= 128)
                        return parseError(pStrm, 1, "Argument type too long");
                    RTEXITCODE rcExit = parseInitArgument(pProbe, pArg, pStrm,
                                                          szArg, cchArg - cchName - 1,
                                                          &szArg[cchArg - cchName], cchName);
                    if (rcExit != RTEXITCODE_SUCCESS)
                        return rcExit;
                    if (VTG_TYPE_IS_LARGE(pArg->fType))
                        pProbe->fHaveLargeArgs = true;
                    pArg = NULL;
                    cchName = cchArg = 0;
                }
                if (ch == ')')
                {
                    size_t off = ScmStreamTell(pStrm);
                    ch = parseGetNextNonSpaceNonCommentCh(pStrm);
                    if (ch != ';')
                        return parseErrorAbs(pStrm, off, "Expected ';'");
                    return RTEXITCODE_SUCCESS;
                }
                break;
            }

            default:
            {
                size_t      cchWord;
                const char *pszWord = ScmStreamCGetWordM1(pStrm, &cchWord);
                if (!pszWord)
                    return parseError(pStrm, 0, "Expected argument");
                if (!pArg)
                {
                    pArg = (PVTGARG)RTMemAllocZ(sizeof(*pArg));
                    if (!pArg)
                        return parseError(pStrm, 1, "Out of memory");
                    RTListAppend(&pProbe->ArgHead, &pArg->ListEntry);
                    pArg->iArgNo = pProbe->cArgs++;
                    pArg->pProbe = pProbe;
                    pArg->offSrc = ScmStreamTell(pStrm) - cchWord;

                    if (cchWord + 1 > sizeof(szArg))
                        return parseError(pStrm, 1, "Too long parameter declaration");
                    memcpy(szArg, pszWord, cchWord);
                    szArg[cchWord] = '\0';
                    cchArg  = cchWord;
                    cchName = 0;
                }
                else
                {
                    if (cchArg + 1 + cchWord + 1 > sizeof(szArg))
                        return parseError(pStrm, 1, "Too long parameter declaration");

                    szArg[cchArg++] = ' ';
                    memcpy(&szArg[cchArg], pszWord, cchWord);
                    cchArg += cchWord;
                    szArg[cchArg] = '\0';
                    cchName = cchWord;
                }
                break;
            }

            case '*':
            {
                if (!pArg)
                    return parseError(pStrm, 1, "A parameter type does not start with an asterix");
                if (cchArg + sizeof(" *") >= sizeof(szArg))
                    return parseError(pStrm, 1, "Too long parameter declaration");
                szArg[cchArg++] = ' ';
                szArg[cchArg++] = '*';
                szArg[cchArg  ] = '\0';
                cchName = 0;
                break;
            }

            case ~(unsigned)0:
                return parseError(pStrm, 0, "Missing closing ')' on probe");
        }
    }
}

/**
 * Parses a D provider statement.
 *
 * @returns Suitable exit code, errors message already written on failure.
 * @param   pStrm               The stream.
 */
static RTEXITCODE parseProvider(PSCMSTREAM pStrm)
{
    /*
     * Next up is a name followed by a curly bracket. Ignore comments.
     */
    RTEXITCODE rcExit = parseSkipSpacesAndComments(pStrm);
    if (rcExit != RTEXITCODE_SUCCESS)
        return parseError(pStrm, 1, "Expected a provider name starting with an alphabetical character");
    size_t      cchName;
    const char *pszName = ScmStreamCGetWord(pStrm, &cchName);
    if (!pszName)
        return parseError(pStrm, 0, "Bad provider name");
    if (RT_C_IS_DIGIT(pszName[cchName - 1]))
        return parseError(pStrm, 1, "A provider name cannot end with digit");

    unsigned ch = parseGetNextNonSpaceNonCommentCh(pStrm);
    if (ch != '{')
        return parseError(pStrm, 1, "Expected '{' after the provider name");

    /*
     * Create a provider instance.
     */
    PVTGPROVIDER pProv = (PVTGPROVIDER)RTMemAllocZ(sizeof(*pProv));
    if (!pProv)
        return parseError(pStrm, 0, "Out of memory");
    RTListInit(&pProv->ProbeHead);
    RTListAppend(&g_ProviderHead, &pProv->ListEntry);
    pProv->pszName = strtabInsertN(pszName, cchName);
    if (!pProv->pszName)
        return parseError(pStrm, 0, "Out of memory");

    /*
     * Parse loop.
     */
    for (;;)
    {
        ch = parseGetNextNonSpaceNonCommentCh(pStrm);
        switch (ch)
        {
            case 'p':
                if (ScmStreamCMatchingWordM1(pStrm, RT_STR_TUPLE("probe")))
                    rcExit = parseProbe(pStrm, pProv);
                else
                    rcExit = parseError(pStrm, 1, "Unexpected character");
                break;

            case '}':
            {
                size_t off = ScmStreamTell(pStrm);
                ch = parseGetNextNonSpaceNonCommentCh(pStrm);
                if (ch == ';')
                    return RTEXITCODE_SUCCESS;
                rcExit = parseErrorAbs(pStrm, off, "Expected ';'");
                break;
            }

            case ~(unsigned)0:
                rcExit = parseError(pStrm, 0, "Missing closing '}' on provider");
                break;

            default:
                rcExit = parseError(pStrm, 1, "Unexpected character");
                break;
        }
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
    }
}


static RTEXITCODE parseScript(const char *pszScript)
{
    SCMSTREAM Strm;
    int rc = ScmStreamInitForReading(&Strm, pszScript);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to open & read '%s' into memory: %Rrc", pszScript, rc);
    if (g_cVerbosity > 0)
        RTMsgInfo("Parsing '%s'...", pszScript);

    RTEXITCODE  rcExit = RTEXITCODE_SUCCESS;
    unsigned    ch;
    while ((ch = ScmStreamGetCh(&Strm)) != ~(unsigned)0)
    {
        if (RT_C_IS_SPACE(ch))
            continue;
        switch (ch)
        {
            case '/':
                ch = ScmStreamGetCh(&Strm);
                if (ch == '*')
                    rcExit = parseMultiLineComment(&Strm);
                else if (ch == '/')
                    rcExit = parseOneLineComment(&Strm);
                else
                    rcExit = parseError(&Strm, 2, "Unexpected character");
                break;

            case 'p':
                if (ScmStreamCMatchingWordM1(&Strm, RT_STR_TUPLE("provider")))
                    rcExit = parseProvider(&Strm);
                else
                    rcExit = parseError(&Strm, 1, "Unexpected character");
                break;

            case '#':
            {
                ch = parseGetNextNonSpaceNonCommentChOnPpLine(&Strm);
                if (ch == ~(unsigned)0)
                    rcExit = RTEXITCODE_FAILURE;
                else if (ch == 'p' && ScmStreamCMatchingWordM1(&Strm, RT_STR_TUPLE("pragma")))
                    rcExit = parsePragma(&Strm);
                else
                    rcExit = parseError(&Strm, 1, "Unsupported preprocessor directive");
                break;
            }

            default:
                rcExit = parseError(&Strm, 1, "Unexpected character");
                break;
        }
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
    }

    ScmStreamDelete(&Strm);
    if (g_cVerbosity > 0 && rcExit == RTEXITCODE_SUCCESS)
        RTMsgInfo("Successfully parsed '%s'.", pszScript);
    return rcExit;
}


/**
 * Parses the arguments.
 */
static RTEXITCODE parseArguments(int argc,  char **argv)
{
    /*
     * Set / Adjust defaults.
     */
    int rc = RTPathAbs(g_pszAssemblerIncVal, g_szAssemblerIncVal, sizeof(g_szAssemblerIncVal) - 1);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathAbs failed: %Rrc", rc);
    strcat(g_szAssemblerIncVal, "/");
    g_pszAssemblerIncVal = g_szAssemblerIncVal;

    /*
     * Option config.
     */
    enum
    {
        kVBoxTpGOpt_32Bit = 1000,
        kVBoxTpGOpt_64Bit,
        kVBoxTpGOpt_GenerateWrapperHeader,
        kVBoxTpGOpt_Assembler,
        kVBoxTpGOpt_AssemblerFmtOpt,
        kVBoxTpGOpt_AssemblerFmtVal,
        kVBoxTpGOpt_AssemblerOutputOpt,
        kVBoxTpGOpt_AssemblerOption,
        kVBoxTpGOpt_Pic,
        kVBoxTpGOpt_ProbeFnName,
        kVBoxTpGOpt_ProbeFnImported,
        kVBoxTpGOpt_ProbeFnNotImported,
        kVBoxTpGOpt_Host32Bit,
        kVBoxTpGOpt_Host64Bit,
        kVBoxTpGOpt_RawModeContext,
        kVBoxTpGOpt_Ring0Context,
        kVBoxTpGOpt_Ring0ContextAgnostic,
        kVBoxTpGOpt_Ring3Context,
        kVBoxTpGOpt_End
    };

    static RTGETOPTDEF const s_aOpts[] =
    {
        /* dtrace w/ long options */
        { "-32",                                kVBoxTpGOpt_32Bit,                      RTGETOPT_REQ_NOTHING },
        { "-64",                                kVBoxTpGOpt_64Bit,                      RTGETOPT_REQ_NOTHING },
        { "--apply-cpp",                        'C',                                    RTGETOPT_REQ_NOTHING },
        { "--generate-obj",                     'G',                                    RTGETOPT_REQ_NOTHING },
        { "--generate-header",                  'h',                                    RTGETOPT_REQ_NOTHING },
        { "--output",                           'o',                                    RTGETOPT_REQ_STRING  },
        { "--script",                           's',                                    RTGETOPT_REQ_STRING  },
        { "--verbose",                          'v',                                    RTGETOPT_REQ_NOTHING },
        /* our stuff */
        { "--generate-wrapper-header",          kVBoxTpGOpt_GenerateWrapperHeader,      RTGETOPT_REQ_NOTHING },
        { "--assembler",                        kVBoxTpGOpt_Assembler,                  RTGETOPT_REQ_STRING  },
        { "--assembler-fmt-opt",                kVBoxTpGOpt_AssemblerFmtOpt,            RTGETOPT_REQ_STRING  },
        { "--assembler-fmt-val",                kVBoxTpGOpt_AssemblerFmtVal,            RTGETOPT_REQ_STRING  },
        { "--assembler-output-opt",             kVBoxTpGOpt_AssemblerOutputOpt,         RTGETOPT_REQ_STRING  },
        { "--assembler-option",                 kVBoxTpGOpt_AssemblerOption,            RTGETOPT_REQ_STRING  },
        { "--pic",                              kVBoxTpGOpt_Pic,                        RTGETOPT_REQ_NOTHING },
        { "--probe-fn-name",                    kVBoxTpGOpt_ProbeFnName,                RTGETOPT_REQ_STRING  },
        { "--probe-fn-imported",                kVBoxTpGOpt_ProbeFnImported,            RTGETOPT_REQ_NOTHING },
        { "--probe-fn-not-imported",            kVBoxTpGOpt_ProbeFnNotImported,         RTGETOPT_REQ_NOTHING },
        { "--host-32-bit",                      kVBoxTpGOpt_Host32Bit,                  RTGETOPT_REQ_NOTHING },
        { "--host-64-bit",                      kVBoxTpGOpt_Host64Bit,                  RTGETOPT_REQ_NOTHING },
        { "--raw-mode-context",                 kVBoxTpGOpt_RawModeContext,             RTGETOPT_REQ_NOTHING },
        { "--ring-0-context",                   kVBoxTpGOpt_Ring0Context,               RTGETOPT_REQ_NOTHING },
        { "--ring-0-context-agnostic",          kVBoxTpGOpt_Ring0ContextAgnostic,       RTGETOPT_REQ_NOTHING },
        { "--ring-3-context",                   kVBoxTpGOpt_Ring3Context,               RTGETOPT_REQ_NOTHING },
        /** @todo We're missing a bunch of assembler options! */
    };

    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetOptState;
    rc = RTGetOptInit(&GetOptState, argc, argv, &s_aOpts[0], RT_ELEMENTS(s_aOpts), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertReleaseRCReturn(rc, RTEXITCODE_FAILURE);

    /*
     * Process the options.
     */
    while ((rc = RTGetOpt(&GetOptState, &ValueUnion)) != 0)
    {
        switch (rc)
        {
            /*
             * DTrace compatible options.
             */
            case kVBoxTpGOpt_32Bit:
                g_cHostBits = g_cBits = 32;
                g_pszAssemblerFmtVal = g_szAssemblerFmtVal32;
                break;

            case kVBoxTpGOpt_64Bit:
                g_cHostBits = g_cBits = 64;
                g_pszAssemblerFmtVal = g_szAssemblerFmtVal64;
                break;

            case 'C':
                g_fApplyCpp = true;
                RTMsgWarning("Ignoring the -C option - no preprocessing of the D script will be performed");
                break;

            case 'G':
                if (   g_enmAction != kVBoxTpGAction_Nothing
                    && g_enmAction != kVBoxTpGAction_GenerateObject)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "-G does not mix with -h or --generate-wrapper-header");
                g_enmAction = kVBoxTpGAction_GenerateObject;
                break;

            case 'h':
                if (!strcmp(GetOptState.pDef->pszLong, "--generate-header"))
                {
                    if (   g_enmAction != kVBoxTpGAction_Nothing
                        && g_enmAction != kVBoxTpGAction_GenerateHeader)
                        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "-h does not mix with -G or --generate-wrapper-header");
                    g_enmAction = kVBoxTpGAction_GenerateHeader;
                }
                else
                {
                    /* --help or similar */
                    RTPrintf("VirtualBox Tracepoint Generator\n"
                             "\n"
                             "Usage: %s [options]\n"
                             "\n"
                             "Options:\n", RTProcShortName());
                    for (size_t i = 0; i < RT_ELEMENTS(s_aOpts); i++)
                        if ((unsigned)s_aOpts[i].iShort < 128)
                            RTPrintf("   -%c,%s\n", s_aOpts[i].iShort, s_aOpts[i].pszLong);
                        else
                            RTPrintf("   %s\n", s_aOpts[i].pszLong);
                    return RTEXITCODE_SUCCESS;
                }
                break;

            case 'o':
                if (g_pszOutput)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Output file is already set to '%s'", g_pszOutput);
                g_pszOutput = ValueUnion.psz;
                break;

            case 's':
                if (g_pszScript)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Script file is already set to '%s'", g_pszScript);
                g_pszScript = ValueUnion.psz;
                break;

            case 'v':
                g_cVerbosity++;
                break;

            case 'V':
            {
                /* The following is assuming that svn does it's job here. */
                static const char s_szRev[] = "$Revision: 106320 $";
                const char *psz = RTStrStripL(strchr(s_szRev, ' '));
                RTPrintf("r%.*s\n", strchr(psz, ' ') - psz, psz);
                return RTEXITCODE_SUCCESS;
            }

            case VINF_GETOPT_NOT_OPTION:
                if (g_enmAction == kVBoxTpGAction_GenerateObject)
                    break; /* object files, ignore them. */
                return RTGetOptPrintError(rc, &ValueUnion);


            /*
             * Our options.
             */
            case kVBoxTpGOpt_GenerateWrapperHeader:
                if (   g_enmAction != kVBoxTpGAction_Nothing
                    && g_enmAction != kVBoxTpGAction_GenerateWrapperHeader)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "--generate-wrapper-header does not mix with -h or -G");
                g_enmAction = kVBoxTpGAction_GenerateWrapperHeader;
                break;

            case kVBoxTpGOpt_Assembler:
                g_pszAssembler = ValueUnion.psz;
                break;

            case kVBoxTpGOpt_AssemblerFmtOpt:
                g_pszAssemblerFmtOpt = ValueUnion.psz;
                break;

            case kVBoxTpGOpt_AssemblerFmtVal:
                g_pszAssemblerFmtVal = ValueUnion.psz;
                break;

            case kVBoxTpGOpt_AssemblerOutputOpt:
                g_pszAssemblerOutputOpt = ValueUnion.psz;
                break;

            case kVBoxTpGOpt_AssemblerOption:
                if (g_cAssemblerOptions >= RT_ELEMENTS(g_apszAssemblerOptions))
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many assembly options (max %u)", RT_ELEMENTS(g_apszAssemblerOptions));
                g_apszAssemblerOptions[g_cAssemblerOptions] = ValueUnion.psz;
                g_cAssemblerOptions++;
                break;

            case kVBoxTpGOpt_Pic:
                g_fPic = true;
                break;

            case kVBoxTpGOpt_ProbeFnName:
                g_pszProbeFnName = ValueUnion.psz;
                break;

            case kVBoxTpGOpt_ProbeFnImported:
                g_fProbeFnImported = true;
                break;

            case kVBoxTpGOpt_ProbeFnNotImported:
                g_fProbeFnImported = false;
                break;

            case kVBoxTpGOpt_Host32Bit:
                g_cHostBits = 32;
                break;

            case kVBoxTpGOpt_Host64Bit:
                g_cHostBits = 64;
                break;

            case kVBoxTpGOpt_RawModeContext:
                g_fTypeContext = VTG_TYPE_CTX_RC;
                g_pszContextDefine = "IN_RC";
                g_pszContextDefine2 = NULL;
                break;

            case kVBoxTpGOpt_Ring0Context:
                g_fTypeContext = VTG_TYPE_CTX_R0;
                g_pszContextDefine = "IN_RING0";
                g_pszContextDefine2 = NULL;
                break;

            case kVBoxTpGOpt_Ring0ContextAgnostic:
                g_fTypeContext = VTG_TYPE_CTX_R0;
                g_pszContextDefine = "IN_RING0_AGNOSTIC";
                g_pszContextDefine2 = "IN_RING0";
                break;

            case kVBoxTpGOpt_Ring3Context:
                g_fTypeContext = VTG_TYPE_CTX_R3;
                g_pszContextDefine = "IN_RING3";
                g_pszContextDefine2 = NULL;
                break;


            /*
             * Errors and bugs.
             */
            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    /*
     * Check that we've got all we need.
     */
    if (g_enmAction == kVBoxTpGAction_Nothing)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No action specified (-h, -G or --generate-wrapper-header)");
    if (!g_pszScript)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No script file specified (-s)");
    if (!g_pszOutput)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No output file specified (-o)");

    return RTEXITCODE_SUCCESS;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return 1;

    RTEXITCODE rcExit = parseArguments(argc, argv);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Parse the script.
         */
        RTListInit(&g_ProviderHead);
        rcExit = parseScript(g_pszScript);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /*
             * Take action.
             */
            if (g_enmAction == kVBoxTpGAction_GenerateHeader)
                rcExit = generateFile(g_pszOutput, "header", generateHeader);
            else if (g_enmAction == kVBoxTpGAction_GenerateWrapperHeader)
                rcExit = generateFile(g_pszOutput, "wrapper header", generateWrapperHeader);
            else
                rcExit = generateObject(g_pszOutput, g_pszTempAsm);
        }
    }

    if (rcExit == RTEXITCODE_SUCCESS && g_cTypeErrors > 0)
        rcExit = RTEXITCODE_FAILURE;
    return rcExit;
}

