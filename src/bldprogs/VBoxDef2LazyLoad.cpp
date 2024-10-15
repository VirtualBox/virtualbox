/* $Id: VBoxDef2LazyLoad.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDef2LazyLoad - Lazy Library Loader Generator.
 *
 * @note Only tested on win.amd64 & darwin.amd64.
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
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <iprt/types.h>
#include <iprt/ldr.h> /* For RTLDRARCH. */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct MYEXPORT
{
    struct MYEXPORT    *pNext;
    /** Pointer to unmangled name for stdcall (after szName), NULL if not. */
    char               *pszUnstdcallName;
    /** Pointer to the exported name. */
    char const         *pszExportedNm;
    unsigned            uOrdinal;
    /** NONAME.  */
    bool                fNoName;
    /** DATA symbol if true, otherwise function. */
    bool                fData;
    char                szName[1];
} MYEXPORT;
typedef MYEXPORT *PMYEXPORT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** @name Options
 * @{ */
static const char  *g_pszOutput   = NULL;
static const char  *g_pszLibrary  = NULL;
static const char  *g_apszInputs[8] = { NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL };
static unsigned     g_cInputs = 0;
static bool         g_fIgnoreData = true;
static bool         g_fWithExplictLoadFunction = false;
static bool         g_fSystemLibrary = false;
#if   defined(RT_ARCH_AMD64)
static RTLDRARCH    g_enmTarget = RTLDRARCH_AMD64;
#elif defined(RT_ARCH_X86)
static RTLDRARCH    g_enmTarget = RTLDRARCH_X86_32;
#elif defined(RT_ARCH_ARM64)
static RTLDRARCH    g_enmTarget = RTLDRARCH_ARM64;
#else
# error "Port me!"
#endif
/** @} */

/** Pointer to the export name list head. */
static PMYEXPORT    g_pExpHead  = NULL;
/** Pointer to the next pointer for insertion. */
static PMYEXPORT   *g_ppExpNext = &g_pExpHead;



#if 0 /* unused */
static const char *leftStrip(const char *psz)
{
    while (isspace(*psz))
        psz++;
    return psz;
}
#endif


static char *leftStrip(char *psz)
{
    while (isspace(*psz))
        psz++;
    return psz;
}


static unsigned wordLength(const char *pszWord)
{
    unsigned off = 0;
    char ch;
    while (   (ch = pszWord[off]) != '\0'
           && ch                  != '='
           && ch                  != ','
           && ch                  != ':'
           && !isspace(ch) )
        off++;
    return off;
}


/**
 * Parses the module definition file, collecting export information.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE, in the latter case full
 *          details has been displayed.
 * @param   pInput              The input stream.
 */
static RTEXITCODE parseInputInner(FILE *pInput, const char *pszInput)
{
    /*
     * Process the file line-by-line.
     */
    bool        fInExports = false;
    unsigned    iLine      = 0;
    char        szLine[16384];
    while (fgets(szLine, sizeof(szLine), pInput))
    {
        iLine++;

        /*
         * Strip leading and trailing spaces from the line as well as
         * trailing comments.
         */
        char *psz = leftStrip(szLine);
        if (*psz == ';')
            continue; /* comment line. */

        char *pszComment = strchr(psz, ';');
        if (pszComment)
            *pszComment = '\0';

        unsigned cch = (unsigned)strlen(psz);
        while (cch > 0 && (isspace(psz[cch - 1]) || psz[cch - 1] == '\r' || psz[cch - 1] == '\n'))
            psz[--cch] = '\0';

        if (!cch)
            continue;

        /*
         * Check for known directives.
         */
        size_t cchWord0 = wordLength(psz);
#define WORD_CMP(pszWord1, cchWord1, szWord2) \
            ( (cchWord1) == sizeof(szWord2) - 1 && memcmp(pszWord1, szWord2, sizeof(szWord2) - 1) == 0 )
        if (WORD_CMP(psz, cchWord0, "EXPORTS"))
        {
            fInExports = true;

            /* In case there is an export on the same line. (Really allowed?) */
            psz = leftStrip(psz + sizeof("EXPORTS") - 1);
            if (!*psz)
                continue;
        }
        /* Directives that we don't care about, but need to catch in order to
           terminate the EXPORTS section in a timely manner. */
        else if (   WORD_CMP(psz, cchWord0, "NAME")
                 || WORD_CMP(psz, cchWord0, "LIBRARY")
                 || WORD_CMP(psz, cchWord0, "DESCRIPTION")
                 || WORD_CMP(psz, cchWord0, "STACKSIZE")
                 || WORD_CMP(psz, cchWord0, "SECTIONS")
                 || WORD_CMP(psz, cchWord0, "SEGMENTS")
                 || WORD_CMP(psz, cchWord0, "VERSION")
                )
        {
            fInExports = false;
        }

        /*
         * Process exports:
         *  entryname[=internalname] [@ordinal[ ][NONAME]] [DATA] [PRIVATE]
         */
        if (fInExports)
        {
            const char *pchName = psz;
            unsigned    cchName = wordLength(psz);

            psz = leftStrip(psz + cchName);
            if (*psz == '=')
            {
                psz = leftStrip(psz + 1);
                psz = leftStrip(psz + wordLength(psz));
            }

            bool     fNoName  = false;
            unsigned uOrdinal = ~0U;
            if (*psz == '@')
            {
                psz++;
                if (!isdigit(*psz))
                {
                    fprintf(stderr, "%s:%u: error: Invalid ordinal spec.\n", pszInput, iLine);
                    return RTEXITCODE_FAILURE;
                }
                uOrdinal = *psz++ - '0';
                while (isdigit(*psz))
                {
                    uOrdinal *= 10;
                    uOrdinal += *psz++ - '0';
                }
                psz = leftStrip(psz);
                cch = wordLength(psz);
                if (WORD_CMP(psz, cch, "NONAME"))
                {
                    fNoName = true;
                    psz = leftStrip(psz + cch);
                }
            }

            bool fData = false;
            while (*psz)
            {
                cch = wordLength(psz);
                if (WORD_CMP(psz, cch, "DATA"))
                {
                    fData = true;
                    if (!g_fIgnoreData)
                    {
                        fprintf(stderr, "%s:%u: error: Cannot process DATA export '%.*s'.\n",
                                pszInput, iLine, cchName, pchName);
                        return RTEXITCODE_SUCCESS;
                    }
                }
                else if (WORD_CMP(psz, cch, "PRIVATE"))
                {
                    fprintf(stderr, "%s:%u: error: Cannot process PRIVATE export '%.*s'.\n",
                            pszInput, iLine, cchName, pchName);
                    return RTEXITCODE_SUCCESS;
                }
                else
                {
                    fprintf(stderr, "%s:%u: error: Unknown keyword: %.*s.\n", pszInput, iLine, cch, psz);
                    return RTEXITCODE_FAILURE;
                }
                psz = leftStrip(psz + cch);
            }

            /*
             * Check for stdcall mangling.
             */
            size_t   cbExp = sizeof(MYEXPORT) + cchName;
            unsigned cchStdcall = 0;
            if (cchName > 3 && *pchName == '_' && isdigit(pchName[cchName - 1]))
            {
                if (cchName > 3 && pchName[cchName - 2] == '@')
                    cchStdcall = 2;
                else if (cchName > 4 && pchName[cchName - 3] == '@' && isdigit(pchName[cchName - 2]))
                    cchStdcall = 3;
                if (cchStdcall)
                    cbExp += cchName - 1 - cchStdcall;
            }

            /*
             * Add the export.
             */
            PMYEXPORT pExp = (PMYEXPORT)malloc(cbExp);
            if (!pExp)
            {
                fprintf(stderr, "%s:%u: error: Out of memory.\n", pszInput, iLine);
                return RTEXITCODE_FAILURE;
            }
            memcpy(pExp->szName, pchName, cchName);
            pExp->szName[cchName] = '\0';
            if (!cchStdcall)
            {
                pExp->pszUnstdcallName = NULL;
                pExp->pszExportedNm = pExp->szName;
            }
            else
            {
                pExp->pszUnstdcallName = &pExp->szName[cchName + 1];
                memcpy(pExp->pszUnstdcallName, pchName + 1, cchName - 1 - cchStdcall);
                pExp->pszUnstdcallName[cchName - 1 - cchStdcall] = '\0';
                pExp->pszExportedNm = pExp->pszUnstdcallName;
            }
            pExp->uOrdinal   = uOrdinal;
            pExp->fNoName    = fNoName;
            pExp->fData      = fData;
            pExp->pNext      = NULL;
            *g_ppExpNext     = pExp;
            g_ppExpNext      = &pExp->pNext;
        }
    }

    /*
     * Why did we quit the loop, EOF or error?
     */
    if (feof(pInput))
        return RTEXITCODE_SUCCESS;
    fprintf(stderr, "error: Incompletely read '%s' (iLine=%u).\n", pszInput, iLine);
    return RTEXITCODE_FAILURE;
}


/**
 * Parses a_apszInputs, populating the list pointed to by g_pExpHead.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE, in the latter case full
 *          details has been displayed.
 */
static RTEXITCODE  parseInputs(void)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    for (unsigned i = 0; i < g_cInputs; i++)
    {
        FILE *pInput = fopen(g_apszInputs[i], "r");
        if (pInput)
        {
            RTEXITCODE rcExit2 = parseInputInner(pInput, g_apszInputs[i]);
            fclose(pInput);
            if (rcExit2 == RTEXITCODE_SUCCESS && !g_pExpHead)
            {
                fprintf(stderr, "error: Found no exports in '%s'.\n", g_apszInputs[i]);
                rcExit2 = RTEXITCODE_FAILURE;
            }
            if (rcExit2 != RTEXITCODE_SUCCESS)
                rcExit = rcExit2;
        }
        else
        {
            fprintf(stderr, "error: Failed to open '%s' for reading.\n", g_apszInputs[i]);
            rcExit = RTEXITCODE_FAILURE;
        }
    }
    return rcExit;
}


/**
 * Generates the assembly source code for AMD64 and x86, writing it
 * to @a pOutput.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE, in the latter case full
 *          details has been displayed.
 * @param   pOutput              The output stream (caller checks it for errors
 *                               when closing).
 */
static RTEXITCODE generateOutputInnerX86AndAMD64(FILE *pOutput)
{
    fprintf(pOutput, ";;\n");
    for (unsigned i = 0; i < g_cInputs; i++)
        fprintf(pOutput, ";; Autogenerated from '%s'.\n", g_apszInputs[i]);

    fprintf(pOutput,
            ";; DO NOT EDIT!\n"
            ";;\n"
            "\n"
            "\n"
            "%%include \"iprt/asmdefs.mac\"\n"
            "\n"
            "\n");

    /*
     * Put the thunks first for alignment and other reasons. It's the hot part of the code.
     */
    fprintf(pOutput,
            ";\n"
            "; Thunks.\n"
            ";\n"
            "BEGINCODE\n");
    for (PMYEXPORT pExp = g_pExpHead; pExp; pExp = pExp->pNext)
        if (pExp->fData)
            fprintf(pOutput,
                    "BEGINPROC LazyGetPtr_%s\n"
                    "    mov   xAX, [NAME(g_LazyPtr_%s) xWrtRIP]\n"
                    "    test  xAX, xAX\n"
                    "    jz    ___LazyLoad___%s\n"
                    "    ret\n"
                    "ENDPROC   LazyGetPtr_%s\n",
                    pExp->szName, pExp->szName, pExp->szName, pExp->szName);
        else if (!pExp->pszUnstdcallName)
            fprintf(pOutput,
                    "BEGINPROC %s\n"
                    "    jmp   RTCCPTR_PRE [NAME(g_pfn%s) xWrtRIP]\n"
                    "ENDPROC   %s\n",
                    pExp->szName, pExp->szName, pExp->szName);
        else
            fprintf(pOutput,
                    "%%ifdef RT_ARCH_X86\n"
                    "global    %s\n"
                    "%s:\n"
                    "    jmp   RTCCPTR_PRE [NAME(g_pfn%s) xWrtRIP]\n"
                    "%%else\n"
                    "BEGINPROC %s\n"
                    "    jmp   RTCCPTR_PRE [NAME(g_pfn%s) xWrtRIP]\n"
                    "ENDPROC   %s\n"
                    "%%endif\n",
                    pExp->szName, pExp->szName, pExp->pszUnstdcallName,
                    pExp->pszUnstdcallName, pExp->pszUnstdcallName, pExp->pszUnstdcallName);

    fprintf(pOutput,
            "\n"
            "\n");

    /*
     * Import pointers
     */
    fprintf(pOutput,
            ";\n"
            "; Import pointers. Initialized to point to lazy loading stubs.\n"
            ";\n"
            "BEGINDATA\n"
            "g_apfnImports:\n");
    for (PMYEXPORT pExp = g_pExpHead; pExp; pExp = pExp->pNext)
        if (pExp->fData)
            fprintf(pOutput,
                    "%%ifdef ASM_FORMAT_PE\n"
                    ";@todo\n"
                    "%%endif\n"
                    "global NAME(g_LazyPtr_%s)\n"
                    "NAME(g_LazyPtr_%s): RTCCPTR_DEF 0\n",
                    pExp->pszExportedNm, pExp->pszExportedNm);
        else if (pExp->pszUnstdcallName)
            fprintf(pOutput,
                    "%%ifdef ASM_FORMAT_PE\n"
                    " %%ifdef RT_ARCH_X86\n"
                    "global __imp_%s\n"
                    "__imp_%s:\n"
                    " %%else\n"
                    "global __imp_%s\n"
                    "__imp_%s:\n"
                    " %%endif\n"
                    "%%endif\n"
                    "NAME(g_pfn%s) RTCCPTR_DEF ___LazyLoad___%s\n"
                    "\n",
                    pExp->szName,
                    pExp->szName,
                    pExp->pszUnstdcallName,
                    pExp->pszUnstdcallName,
                    pExp->pszExportedNm,
                    pExp->pszExportedNm);
        else
            fprintf(pOutput,
                    "%%ifdef ASM_FORMAT_PE\n"
                    "global __imp_%s\n"
                    "__imp_%s:\n"
                    "%%endif\n"
                    "NAME(g_pfn%s) RTCCPTR_DEF ___LazyLoad___%s\n"
                    "\n",
                    pExp->szName,
                    pExp->szName,
                    pExp->pszExportedNm,
                    pExp->pszExportedNm);
    fprintf(pOutput,
            "RTCCPTR_DEF 0 ; Terminator entry for traversal.\n"
            "\n"
            "\n");

    /*
     * Now for the less important stuff, starting with the names.
     *
     * We keep the names separate so we can traverse them in parallel to
     * g_apfnImports in the load-everything routine further down.
     */
    fprintf(pOutput,
            ";\n"
            "; Imported names.\n"
            ";\n"
            "BEGINCODE\n"
            "g_szLibrary:        db '%s',0\n"
            "\n"
            "g_szzNames:\n",
            g_pszLibrary);
    for (PMYEXPORT pExp = g_pExpHead; pExp; pExp = pExp->pNext)
        if (!pExp->fNoName)
            fprintf(pOutput, "  g_sz%s:\n    db '%s',0\n", pExp->pszExportedNm, pExp->pszExportedNm);
        else
            fprintf(pOutput, "  g_sz%s:\n    db '#%u',0\n", pExp->pszExportedNm, pExp->uOrdinal);
    fprintf(pOutput,
            "g_EndOfNames: db 0\n"
            "\n"
            "g_szFailLoadFmt:    db 'Lazy loader failed to load \"%%s\": %%Rrc', 10, 0\n"
            "g_szFailResolveFmt: db 'Lazy loader failed to resolve symbol \"%%s\" in \"%%s\": %%Rrc', 10, 0\n"
            "\n"
            "\n");

    /*
     * The per import lazy load code.
     */
    fprintf(pOutput,
            ";\n"
            "; Lazy load+resolve stubs.\n"
            ";\n"
            "BEGINCODE\n");
    for (PMYEXPORT pExp = g_pExpHead; pExp; pExp = pExp->pNext)
    {
        if (!pExp->fNoName)
            fprintf(pOutput,
                    "___LazyLoad___%s:\n"
                    /* "int3\n" */
                    "%%ifdef RT_ARCH_AMD64\n"
                    "    lea     rax, [g_sz%s wrt rip]\n"
                    "    lea     r10, [NAME(%s%s) wrt rip]\n"
                    "    call    LazyLoadResolver\n"
                    "%%elifdef RT_ARCH_X86\n"
                    "    push    g_sz%s\n"
                    "    push    NAME(%s%s)\n"
                    "    call    LazyLoadResolver\n"
                    "    add     esp, 8h\n"
                    "%%else\n"
                    " %%error \"Unsupported architecture\"\n"
                    "%%endif\n"
                    ,
                    pExp->pszExportedNm,
                    pExp->pszExportedNm,
                    !pExp->fData ? "g_pfn" : "g_LazyPtr_", pExp->pszExportedNm,
                    pExp->pszExportedNm,
                    !pExp->fData ? "g_pfn" : "g_LazyPtr_", pExp->pszExportedNm);
        else
            fprintf(pOutput,
                    "___LazyLoad___%s:\n"
                    /* "int3\n" */
                    "%%ifdef RT_ARCH_AMD64\n"
                    "    mov     eax, %u\n"
                    "    lea     r10, [NAME(%s%s) wrt rip]\n"
                    "    call    LazyLoadResolver\n"
                    "%%elifdef RT_ARCH_X86\n"
                    "    push    %u\n"
                    "    push    NAME(%s%s)\n"
                    "    call    LazyLoadResolver\n"
                    "    add     esp, 8h\n"
                    "%%else\n"
                    " %%error \"Unsupported architecture\"\n"
                    "%%endif\n"
                    ,
                    pExp->pszExportedNm,
                    pExp->uOrdinal,
                    !pExp->fData ? "g_pfn" : "g_LazyPtr_", pExp->pszExportedNm,
                    pExp->uOrdinal,
                    !pExp->fData ? "g_pfn" : "g_LazyPtr_", pExp->pszExportedNm);
        if (pExp->fData)
            fprintf(pOutput, "    jmp     NAME(LazyGetPtr_%s)\n", pExp->szName);
        else if (!pExp->pszUnstdcallName)
            fprintf(pOutput, "    jmp     NAME(%s)\n", pExp->szName);
        else
            fprintf(pOutput,
                    "%%ifdef RT_ARCH_X86\n"
                    "    jmp     %s\n"
                    "%%else\n"
                    "    jmp     NAME(%s)\n"
                    "%%endif\n"
                    ,
                    pExp->szName, pExp->pszUnstdcallName);
        fprintf(pOutput, "\n");
    }
    fprintf(pOutput,
            "\n"
            "\n"
            "\n");

    /*
     * The code that does the loading and resolving.
     */
    fprintf(pOutput,
            ";\n"
            "; The module handle.\n"
            ";\n"
            "BEGINDATA\n"
            "g_hMod RTCCPTR_DEF 0\n"
            "\n"
            "\n"
            "\n");

    /*
     * How we load the module needs to be selectable later on.
     *
     * The LazyLoading routine returns the module handle in RCX/ECX, caller
     * saved all necessary registers.
     */
    if (!g_fSystemLibrary)
        fprintf(pOutput,
                ";\n"
                ";SUPR3DECL(int) SUPR3HardenedLdrLoadAppPriv(const char *pszFilename, PRTLDRMOD phLdrMod,\n"
                ";                                           uint32_t fFlags, PRTERRINFO pErrInfo);\n"
                ";\n"
                "EXTERN_IMP2 SUPR3HardenedLdrLoadAppPriv\n"
                "%%ifdef IN_RT_R3\n"
                "extern NAME(RTAssertMsg2Weak)\n"
                "%%else\n"
                "EXTERN_IMP2 RTAssertMsg2Weak\n"
                "%%endif\n"
                "BEGINCODE\n"
                "\n"
                "LazyLoading:\n"
                "    mov     xCX, [g_hMod xWrtRIP]\n"
                "    or      xCX, xCX\n"
                "    jnz     .return\n"
                "\n"
                "%%ifdef ASM_CALL64_GCC\n"
                "    xor     rcx, rcx               ; pErrInfo\n"
                "    xor     rdx, rdx               ; fFlags (local load)\n"
                "    lea     rsi, [g_hMod wrt rip]  ; phLdrMod\n"
                "    lea     rdi, [g_szLibrary wrt rip] ; pszFilename\n"
                "    sub     rsp, 08h\n"
                "    call    IMP2(SUPR3HardenedLdrLoadAppPriv)\n"
                "    add     rsp, 08h\n"
                "\n"
                "%%elifdef ASM_CALL64_MSC\n"
                "    xor     r9, r9                 ; pErrInfo\n"
                "    xor     r8, r8                 ; fFlags (local load)\n"
                "    lea     rdx, [g_hMod wrt rip]  ; phLdrMod\n"
                "    lea     rcx, [g_szLibrary wrt rip] ; pszFilename\n"
                "    sub     rsp, 28h\n"
                "    call    IMP2(SUPR3HardenedLdrLoadAppPriv)\n"
                "    add     rsp, 28h\n"
                "\n"
                "%%elifdef RT_ARCH_X86\n"
                "    sub     xSP, 0ch\n"
                "    push    0              ; pErrInfo\n"
                "    push    0              ; fFlags (local load)\n"
                "    push    g_hMod         ; phLdrMod\n"
                "    push    g_szLibrary    ; pszFilename\n"
                "    call    IMP2(SUPR3HardenedLdrLoadAppPriv)\n"
                "    add     esp, 1ch\n"
                "%%else\n"
                " %%error \"Unsupported architecture\"\n"
                "%%endif\n");
    else
        fprintf(pOutput,
                ";\n"
                "; RTDECL(int) RTLdrLoadSystem(const char *pszFilename, bool fNoUnload, PRTLDRMOD phLdrMod);\n"
                ";\n"
                "%%ifdef IN_RT_R3\n"
                "extern NAME(RTLdrLoadSystem)\n"
                "extern NAME(RTAssertMsg2Weak)\n"
                "%%else\n"
                "EXTERN_IMP2 RTLdrLoadSystem\n"
                "EXTERN_IMP2 RTAssertMsg2Weak\n"
                "%%endif\n"
                "BEGINCODE\n"
                "\n"
                "LazyLoading:\n"
                "    mov     xCX, [g_hMod xWrtRIP]\n"
                "    or      xCX, xCX\n"
                "    jnz     .return\n"
                "\n"
                "%%ifdef ASM_CALL64_GCC\n"
                "    lea     rdx, [g_hMod wrt rip]  ; phLdrMod\n"
                "    mov     esi, 1                 ; fNoUnload=true\n"
                "    lea     rdi, [g_szLibrary wrt rip] ; pszFilename\n"
                "    sub     rsp, 08h\n"
                " %%ifdef IN_RT_R3\n"
                "    call    NAME(RTLdrLoadSystem)\n"
                " %%else\n"
                "    call    IMP2(RTLdrLoadSystem)\n"
                " %%endif\n"
                "    add     rsp, 08h\n"
                "\n"
                "%%elifdef ASM_CALL64_MSC\n"
                "    lea     r8, [g_hMod wrt rip]   ; phLdrMod\n"
                "    mov     edx, 1                 ; fNoUnload=true\n"
                "    lea     rcx, [g_szLibrary wrt rip] ; pszFilename\n"
                "    sub     rsp, 28h\n"
                " %%ifdef IN_RT_R3\n"
                "    call    NAME(RTLdrLoadSystem)\n"
                " %%else\n"
                "    call    IMP2(RTLdrLoadSystem)\n"
                " %%endif\n"
                "    add     rsp, 28h\n"
                "\n"
                "%%elifdef RT_ARCH_X86\n"
                "    push    g_hMod         ; phLdrMod\n"
                "    push    1              ; fNoUnload=true\n"
                "    push    g_szLibrary    ; pszFilename\n"
                " %%ifdef IN_RT_R3\n"
                "    call    NAME(RTLdrLoadSystem)\n"
                " %%else\n"
                "    call    IMP2(RTLdrLoadSystem)\n"
                " %%endif\n"
                "    add     esp, 0ch\n"
                "%%else\n"
                " %%error \"Unsupported architecture\"\n"
                "%%endif\n");
    fprintf(pOutput,
            "    or      eax, eax\n"
            "    jnz    .badload\n"
            "    mov     xCX, [g_hMod xWrtRIP]\n"
            ".return:\n"
            "    ret\n"
            "\n"
            ".badload:\n"
            "%%ifdef ASM_CALL64_GCC\n"
            "    mov     edx, eax\n"
            "    lea     rsi, [g_szLibrary wrt rip]\n"
            "    lea     rdi, [g_szFailLoadFmt wrt rip]\n"
            "    sub     rsp, 08h\n"
            "%%elifdef ASM_CALL64_MSC\n"
            "    mov     r8d, eax\n"
            "    lea     rdx, [g_szLibrary wrt rip]\n"
            "    lea     rcx, [g_szFailLoadFmt wrt rip]\n"
            "    sub     rsp, 28h\n"
            "%%elifdef RT_ARCH_X86\n"
            "    push    eax\n"
            "    push    g_szLibrary\n"
            "    push    g_szFailLoadFmt\n"
            "%%endif\n"
            "%%ifdef IN_RT_R3\n"
            "    call    NAME(RTAssertMsg2Weak)\n"
            "%%else\n"
            "    call    IMP2(RTAssertMsg2Weak)\n"
            "%%endif\n"
            ".badloadloop:\n"
            "    int3\n"
            "    jmp     .badloadloop\n"
            "LazyLoading_End:\n"
            "\n"
            "\n");


    fprintf(pOutput,
            ";\n"
            ";RTDECL(int) RTLdrGetSymbol(RTLDRMOD hLdrMod, const char *pszSymbol, void **ppvValue);\n"
            ";\n"
            "%%ifdef IN_RT_R3\n"
            "extern NAME(RTLdrGetSymbol)\n"
            "%%else\n"
            "EXTERN_IMP2 RTLdrGetSymbol\n"
            "%%endif\n"
            "BEGINCODE\n"
            "LazyLoadResolver:\n"
            "%%ifdef RT_ARCH_AMD64\n"
            "    push    rbp\n"
            "    mov     rbp, rsp\n"
            "    push    r15\n"
            "    push    r14\n"
            "    mov     r15, rax       ; name\n"
            "    mov     r14, r10       ; ppfn\n"
            "    push    r9\n"
            "    push    r8\n"
            "    push    rcx\n"
            "    push    rdx\n"
            "    push    r12\n"
            " %%ifdef ASM_CALL64_GCC\n"
            "    push    rsi\n"
            "    push    rdi\n"
            "    mov     r12, rsp\n"
            " %%else\n"
            "    mov     r12, rsp\n"
            "    sub     rsp, 20h\n"
            " %%endif\n"
            "    and     rsp, 0fffffff0h ; Try make sure the stack is aligned\n"
            "\n"
            "    call    LazyLoading    ; returns handle in rcx\n"
            " %%ifdef ASM_CALL64_GCC\n"
            "    mov     rdi, rcx       ; hLdrMod\n"
            "    mov     rsi, r15       ; pszSymbol\n"
            "    mov     rdx, r14       ; ppvValue\n"
            " %%else\n"
            "    mov     rdx, r15       ; pszSymbol\n"
            "    mov     r8, r14        ; ppvValue\n"
            " %%endif\n"
            " %%ifdef IN_RT_R3\n"
            "    call    NAME(RTLdrGetSymbol)\n"
            " %%else\n"
            "    call    IMP2(RTLdrGetSymbol)\n"
            " %%endif\n"
            "    or      eax, eax\n"
            "    jnz     .badsym\n"
            "\n"
            "    mov     rsp, r12\n"
            " %%ifdef ASM_CALL64_GCC\n"
            "    pop     rdi\n"
            "    pop     rsi\n"
            " %%endif\n"
            "    pop     r12\n"
            "    pop     rdx\n"
            "    pop     rcx\n"
            "    pop     r8\n"
            "    pop     r9\n"
            "    pop     r14\n"
            "    pop     r15\n"
            "    leave\n"
            "\n"
            "%%elifdef RT_ARCH_X86\n"
            "    push    ebp\n"
            "    mov     ebp, esp\n"
            "    push    eax\n"
            "    push    ecx\n"
            "    push    edx\n"
            "    and     esp, 0fffffff0h\n"
            "\n"
            ".loaded:\n"
            "    call    LazyLoading      ; returns handle in ecx\n"
            "    push    dword [ebp + 8]  ; value addr\n"
            "    push    dword [ebp + 12] ; symbol name\n"
            "    push    ecx\n"
            " %%ifdef IN_RT_R3\n"
            "    call    NAME(RTLdrGetSymbol)\n"
            " %%else\n"
            "    call    IMP2(RTLdrGetSymbol)\n"
            " %%endif\n"
            "    or      eax, eax\n"
            "    jnz     .badsym\n"
            "    lea     esp, [ebp - 0ch]\n"
            "    pop     edx\n"
            "    pop     ecx\n"
            "    pop     eax\n"
            "    leave\n"
            "%%else\n"
            " %%error \"Unsupported architecture\"\n"
            "%%endif\n"
            "    ret\n"
            "\n"
            ".badsym:\n"
            "%%ifdef ASM_CALL64_GCC\n"
            "    mov     ecx, eax\n"
            "    lea     rdx, [g_szLibrary wrt rip]\n"
            "    mov     rsi, r15\n"
            "    lea     rdi, [g_szFailResolveFmt wrt rip]\n"
            "    sub     rsp, 08h\n"
            "%%elifdef ASM_CALL64_MSC\n"
            "    mov     r9d, eax\n"
            "    mov     r8, r15\n"
            "    lea     rdx, [g_szLibrary wrt rip]\n"
            "    lea     rcx, [g_szFailResolveFmt wrt rip]\n"
            "    sub     rsp, 28h\n"
            "%%elifdef RT_ARCH_X86\n"
            "    push    eax\n"
            "    push    dword [ebp + 12]\n"
            "    push    g_szLibrary\n"
            "    push    g_szFailResolveFmt\n"
            "%%endif\n"
            "%%ifdef IN_RT_R3\n"
            "    call    NAME(RTAssertMsg2Weak)\n"
            "%%else\n"
            "    call    IMP2(RTAssertMsg2Weak)\n"
            "%%endif\n"
            ".badsymloop:\n"
            "    int3\n"
            "    jmp     .badsymloop\n"
            "\n"
            "LazyLoadResolver_End:\n"
            "\n"
            "\n"
            );



    /*
     * C callable method for explicitly loading the library and optionally
     * resolving all the imports.
     */
    if (g_fWithExplictLoadFunction)
    {
        int cchLibBaseName = (int)(strchr(g_pszLibrary, '.') ? strchr(g_pszLibrary, '.') - g_pszLibrary : strlen(g_pszLibrary));
        fprintf(pOutput,
                ";;\n"
                "; ExplicitlyLoad%.*s(bool fResolveAllImports, pErrInfo);\n"
                ";\n"
                "%%ifdef IN_RT_R3\n"
                "extern NAME(RTErrInfoSet)\n"
                "%%else\n"
                "EXTERN_IMP2 RTErrInfoSet\n"
                "%%endif\n"
                "BEGINCODE\n"
                "BEGINPROC ExplicitlyLoad%.*s\n"
                "    push    xBP\n"
                "    mov     xBP, xSP\n"
                "    push    xBX\n"
                "%%ifdef ASM_CALL64_GCC\n"
                " %%define pszCurStr r14\n"
                "    push    r14\n"
                "%%else\n"
                " %%define pszCurStr xDI\n"
                "    push    xDI\n"
                "%%endif\n"
                "    sub     xSP, 40h\n"
                "\n"
                "    ;\n"
                "    ; Save parameters on stack (64-bit only).\n"
                "    ;\n"
                "%%ifdef ASM_CALL64_GCC\n"
                "    mov     [xBP - xCB * 3], rdi ; fResolveAllImports\n"
                "    mov     [xBP - xCB * 4], rsi ; pErrInfo\n"
                "%%elifdef ASM_CALL64_MSC\n"
                "    mov     [xBP - xCB * 3], rcx ; fResolveAllImports\n"
                "    mov     [xBP - xCB * 4], rdx ; pErrInfo\n"
                "%%endif\n"
                "\n"
                "    ;\n"
                "    ; Is the module already loaded?\n"
                "    ;\n"
                "    cmp     RTCCPTR_PRE [g_hMod xWrtRIP], 0\n"
                "    jnz     .loaded\n"
                "\n"
                "    ;\n"
                "    ; Load the module.\n"
                "    ;\n"
                ,
                cchLibBaseName, g_pszLibrary,
                cchLibBaseName, g_pszLibrary);
        if (!g_fSystemLibrary)
            fprintf(pOutput,
                    "%%ifdef ASM_CALL64_GCC\n"
                    "    mov     rcx, [xBP - xCB * 4]       ; pErrInfo\n"
                    "    xor     rdx, rdx                   ; fFlags (local load)\n"
                    "    lea     rsi, [g_hMod wrt rip]      ; phLdrMod\n"
                    "    lea     rdi, [g_szLibrary wrt rip] ; pszFilename\n"
                    "    call    IMP2(SUPR3HardenedLdrLoadAppPriv)\n"
                    "\n"
                    "%%elifdef ASM_CALL64_MSC\n"
                    "    mov     r9, [xBP - xCB * 4]        ; pErrInfo\n"
                    "    xor     r8, r8                     ; fFlags (local load)\n"
                    "    lea     rdx, [g_hMod wrt rip]      ; phLdrMod\n"
                    "    lea     rcx, [g_szLibrary wrt rip] ; pszFilename\n"
                    "    call    IMP2(SUPR3HardenedLdrLoadAppPriv)\n"
                    "\n"
                    "%%elifdef RT_ARCH_X86\n"
                    "    sub     xSP, 0ch\n"
                    "    push    dword [xBP + 12]           ; pErrInfo\n"
                    "    push    0                          ; fFlags (local load)\n"
                    "    push    g_hMod                     ; phLdrMod\n"
                    "    push    g_szLibrary                ; pszFilename\n"
                    "    call    IMP2(SUPR3HardenedLdrLoadAppPriv)\n"
                    "    add     esp, 1ch\n"
                    "%%else\n"
                    " %%error \"Unsupported architecture\"\n"
                    "%%endif\n");
        else
            fprintf(pOutput,
                    "%%ifdef ASM_CALL64_GCC\n"
                    "    lea     rdx, [g_hMod wrt rip]      ; phLdrMod\n"
                    "    mov     esi, 1                     ; fNoUnload=true\n"
                    "    lea     rdi, [g_szLibrary wrt rip] ; pszFilename\n"
                    " %%ifdef IN_RT_R3\n"
                    "    call    NAME(RTLdrLoadSystem)\n"
                    " %%else\n"
                    "    call    IMP2(RTLdrLoadSystem)\n"
                    " %%endif\n"
                    "\n"
                    "%%elifdef ASM_CALL64_MSC\n"
                    "    lea     r8, [g_hMod wrt rip]       ; phLdrMod\n"
                    "    mov     edx, 1                     ; fNoUnload=true\n"
                    "    lea     rcx, [g_szLibrary wrt rip] ; pszFilename\n"
                    " %%ifdef IN_RT_R3\n"
                    "    call    NAME(RTLdrLoadSystem)\n"
                    " %%else\n"
                    "    call    IMP2(RTLdrLoadSystem)\n"
                    " %%endif\n"
                    "\n"
                    "%%elifdef RT_ARCH_X86\n"
                    "    push    g_hMod                     ; phLdrMod\n"
                    "    push    1                          ; fNoUnload=true\n"
                    "    push    g_szLibrary                ; pszFilename\n"
                    " %%ifdef IN_RT_R3\n"
                    "    call    NAME(RTLdrLoadSystem)\n"
                    " %%else\n"
                    "    call    IMP2(RTLdrLoadSystem)\n"
                    " %%endif\n"
                    "    add     esp, 0ch\n"
                    "%%else\n"
                    " %%error \"Unsupported architecture\"\n"
                    "%%endif\n");
         fprintf(pOutput,
                "    or      eax, eax\n"
                "    jnz     .return\n"
                "\n"
                "    ;\n"
                "    ; Resolve the imports too if requested to do so.\n"
                "    ;\n"
                ".loaded:\n"
                "%%ifdef ASM_ARCH_X86\n"
                "    cmp     byte [xBP + 8], 0\n"
                "%%else\n"
                "    cmp     byte [xBP - xCB * 3], 0\n"
                "%%endif\n"
                "    je      .return\n"
                "\n"
                "    lea     pszCurStr, [g_szzNames xWrtRIP]\n"
                "    lea     xBX, [g_apfnImports xWrtRIP]\n"
                ".next_import:\n"
                "    cmp     RTCCPTR_PRE [xBX], 0\n"
                "    je      .return\n"
                "%%ifdef ASM_CALL64_GCC\n"
                "    mov     rdx, xBX                  ; ppvValue\n"
                "    mov     rsi, pszCurStr            ; pszSymbol\n"
                "    mov     rdi, [g_hMod wrt rip]     ; hLdrMod\n"
                " %%ifdef IN_RT_R3\n"
                "    call    NAME(RTLdrGetSymbol)\n"
                " %%else\n"
                "    call    IMP2(RTLdrGetSymbol)\n"
                " %%endif\n"
                "%%elifdef ASM_CALL64_MSC\n"
                "    mov     r8, xBX                   ; ppvValue\n"
                "    mov     rdx, pszCurStr            ; pszSymbol\n"
                "    mov     rcx, [g_hMod wrt rip]     ; pszSymbol\n"
                " %%ifdef IN_RT_R3\n"
                "    call    NAME(RTLdrGetSymbol)\n"
                " %%else\n"
                "    call    IMP2(RTLdrGetSymbol)\n"
                " %%endif\n"
                "%%else\n"
                "    push    xBX                       ; ppvValue\n"
                "    push    pszCurStr                 ; pszSymbol\n"
                "    push    RTCCPTR_PRE [g_hMod]      ; hLdrMod\n"
                " %%ifdef IN_RT_R3\n"
                "    call    NAME(RTLdrGetSymbol)\n"
                " %%else\n"
                "    call    IMP2(RTLdrGetSymbol)\n"
                " %%endif\n"
                "    add     xSP, 0ch\n"
                "%%endif\n"
                "    or      eax, eax\n"
                "    jnz     .symbol_error\n"
                "\n"
                "    ; Advance.\n"
                "    add     xBX, RTCCPTR_CB\n"
                "    xor     eax, eax\n"
                "    mov     xCX, 0ffffffffh\n"
                "%%ifdef ASM_CALL64_GCC\n"
                "    mov     xDI, pszCurStr\n"
                "    repne scasb\n"
                "    mov     pszCurStr, xDI\n"
                "%%else\n"
                "    repne scasb\n"
                "%%endif\n"
                "    jmp     .next_import\n"
                "\n"
                "    ;\n"
                "    ; Error loading a symbol. Call RTErrInfoSet on pErrInfo (preserves eax).\n"
                "    ;\n"
                ".symbol_error:\n"
                "%%ifdef ASM_CALL64_GCC\n"
                "    mov     rdx, pszCurStr            ; pszMsg\n"
                "    mov     esi, eax                  ; rc\n"
                "    mov     rdi, [xBP - xCB * 4]      ; pErrInfo\n"
                " %%ifdef IN_RT_R3\n"
                "    call    NAME(RTErrInfoSet)\n"
                " %%else\n"
                "    call    IMP2(RTErrInfoSet)\n"
                " %%endif\n"
                "%%elifdef ASM_CALL64_MSC\n"
                "    mov     r8, pszCurStr             ; pszMsg\n"
                "    mov     edx, eax                  ; rc\n"
                "    mov     rcx, [xBP - xCB * 4]      ; pErrInfo\n"
                " %%ifdef IN_RT_R3\n"
                "    call    NAME(RTErrInfoSet)\n"
                " %%else\n"
                "    call    IMP2(RTErrInfoSet)\n"
                " %%endif\n"
                "%%else\n"
                "    push    pszCurStr                 ; pszMsg\n"
                "    push    eax                       ; pszSymbol\n"
                "    push    dword [xBP + 0ch]         ; pErrInfo\n"
                " %%ifdef IN_RT_R3\n"
                "    call    NAME(RTErrInfoSet)\n"
                " %%else\n"
                "    call    IMP2(RTErrInfoSet)\n"
                " %%endif\n"
                "    add     xSP, 0ch\n"
                "%%endif\n"
                "    "
                "\n"
                ".return:\n"
                "    mov    pszCurStr, [xBP - xCB * 2]\n"
                "    mov    xBX,       [xBP - xCB * 1]\n"
                "    leave\n"
                "    ret\n"
                "ENDPROC   ExplicitlyLoad%.*s\n"
                "\n"
                "\n"
                ,
                cchLibBaseName, g_pszLibrary);
    }


    return RTEXITCODE_SUCCESS;
}


/**
 * Generates the assembly source code for ARM64, writing it
 * to @a pOutput.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE, in the latter case full
 *          details has been displayed.
 * @param   pOutput              The output stream (caller checks it for errors
 *                               when closing).
 */
static RTEXITCODE generateOutputInnerArm64(FILE *pOutput)
{
    fprintf(pOutput, "/*\n");
    for (unsigned i = 0; i < g_cInputs; i++)
        fprintf(pOutput, " * Autogenerated from '%s'.\n", g_apszInputs[i]);

    fprintf(pOutput,
            " * DO NOT EDIT!\n"
            " */\n"
            "\n"
            "\n"
            "#include \"iprt/asmdefs-arm.h\"\n"
            "\n"
            "\n");

    /*
     * Put the thunks first for alignment and other reasons. It's the hot part of the code.
     */
    fprintf(pOutput,
            "/*\n"
            " * Thunks.\n"
            " */\n"
            "BEGINCODE\n");
    for (PMYEXPORT pExp = g_pExpHead; pExp; pExp = pExp->pNext)
        if (!pExp->fData)
            fprintf(pOutput,
                    ".p2align 3\n"
                    ".globl NAME(%s)\n"
                    "NAME(%s):\n"
                    "    adrp    x9, PAGE(NAME(g_pfn%s))\n"
                    "    ldr     x9, [x9, PAGEOFF(NAME(g_pfn%s))]\n"
                    "    br      x9\n",
                    pExp->szName, pExp->szName, pExp->szName, pExp->szName);
        else
            fprintf(pOutput,
                    ".p2align 3\n"
                    ".globl NAME(LazyGetPtr_%s)\n"
                    "NAME(LazyGetPtr_%s):\n"
                    "    adrp    x9, PAGE(NAME(g_LazyPtr_%s))\n"
                    "    ldr     x9, [x9, PAGEOFF(NAME(g_LazyPtr_%s))]\n"
                    "    cmp     x9, #0\n"
                    "    b.eq    ___LazyLoad___%s\n"
                    "    mov     x0, x9\n"
                    "    ret\n",
                    pExp->szName, pExp->szName, pExp->szName, pExp->szName, pExp->pszExportedNm);
    fprintf(pOutput,
            "ENDCODE\n"
            "\n"
            "\n");

    /*
     * Import pointers
     */
    fprintf(pOutput,
            "/*\n"
            " * Import pointers. Initialized to point to lazy loading stubs.\n"
            " */\n"
            "BEGINDATA\n"
            ".p2align 3\n"
            "g_apfnImports:\n");
    for (PMYEXPORT pExp = g_pExpHead; pExp; pExp = pExp->pNext)
        if (!pExp->fData)
            fprintf(pOutput,
                    ".globl __imp_%s\n"
                    "__imp_%s:\n"
                    ".globl NAME(g_pfn%s)\n"
                    "NAME(g_pfn%s):\n"
                    "    .quad ___LazyLoad___%s\n"
                    "\n",
                    pExp->szName, pExp->szName,
                    pExp->szName, pExp->szName,
                    pExp->pszExportedNm);
        else
            fprintf(pOutput,
                    ".globl NAME(g_LazyPtr_%s)\n"
                    "NAME(g_LazyPtr_%s):\n"
                    "    .quad 0\n"
                    "\n",
                    pExp->szName, pExp->szName);
    fprintf(pOutput,
            "    .quad 0 /* Terminator entry for traversal. */\n"
            "ENDDATA\n"
            "\n"
            "\n");

    /*
     * Now for the less important stuff, starting with the names.
     *
     * We keep the names separate so we can traverse them in parallel to
     * g_apfnImports in the load-everything routine further down.
     */
    fprintf(pOutput,
            "/*\n"
            " * Imported names.\n"
            " */\n"
            "BEGINCONSTSTRINGS\n"
            "g_szLibrary:\n"
            "    .asciz \"%s\"\n"
            "\n"
            "g_szzNames:\n",
            g_pszLibrary);
    for (PMYEXPORT pExp = g_pExpHead; pExp; pExp = pExp->pNext)
        if (!pExp->fNoName)
            fprintf(pOutput, "  g_sz%s:\n    .asciz \"%s\"\n", pExp->pszExportedNm, pExp->pszExportedNm);
        else
            fprintf(pOutput, "  g_sz%s:\n    .asciz \"#%u\"\n", pExp->pszExportedNm, pExp->uOrdinal);
    fprintf(pOutput,
            "g_EndOfNames: .byte 0\n"
            "\n"
            "g_szFailLoadFmt:    .asciz \"Lazy loader failed to load \\\"%%s\\\": %%Rrc\\n\"\n"
            "g_szFailResolveFmt: .asciz \"Lazy loader failed to resolve symbol \\\"%%s\\\" in \\\"%%s\\\": %%Rrc\\n\"\n"
            "ENDCONSTSTRINGS\n"
            "\n"
            "\n");

    /*
     * The per import lazy load code.
     */
    fprintf(pOutput,
            "/*\n"
            " * Lazy load+resolve stubs.\n"
            " */\n"
            "BEGINCODE\n"
            ".p2align 3\n");
    for (PMYEXPORT pExp = g_pExpHead; pExp; pExp = pExp->pNext)
    {
        if (!pExp->fNoName)
            fprintf(pOutput,
                    "___LazyLoad___%s:\n"
                    "    adrp    x9, PAGE(g_sz%s)\n"
                    "    add     x9, x9, PAGEOFF(g_sz%s)\n"
                    "    adrp    x10, PAGE(NAME(%s%s))\n"
                    "    add     x10, x10, PAGEOFF(NAME(%s%s))\n"
                    "    mov     x16, x30\n"
                    "    bl      LazyLoadResolver\n"
                    "    mov     x30, x16\n"
                    , pExp->pszExportedNm,
                    pExp->pszExportedNm, pExp->pszExportedNm,
                    !pExp->fData ? "g_pfn" : "g_LazyPtr_", pExp->pszExportedNm,
                    !pExp->fData ? "g_pfn" : "g_LazyPtr_", pExp->pszExportedNm);
        else
            fprintf(pOutput,
                    "___LazyLoad___%s:\n"
                    "    movk    w9, #%u\n"
                    "    adrp    x10, PAGE(NAME(%s%s))\n"
                    "    add     x10, x10, PAGEOFF(NAME(%s%s))\n"
                    , pExp->pszExportedNm,
                    pExp->uOrdinal,
                    !pExp->fData ? "g_pfn" : "g_LazyPtr_", pExp->pszExportedNm,
                    !pExp->fData ? "g_pfn" : "g_LazyPtr_", pExp->pszExportedNm);
        if (!pExp->fData)
            fprintf(pOutput, "    b       NAME(%s)\n", pExp->szName);
        else
            fprintf(pOutput, "    b       NAME(LazyGetPtr_%s)\n", pExp->szName);
        fprintf(pOutput, "\n");
    }
    fprintf(pOutput,
            "ENDCODE\n"
            "\n"
            "\n"
            "\n");

    /*
     * The code that does the loading and resolving.
     */
    fprintf(pOutput,
            "/*\n"
            " * The module handle.\n"
            " */\n"
            "BEGINDATA\n"
            "g_hMod:\n"
            "    .quad 0\n"
            "ENDDATA\n"
            "\n"
            "\n"
            "\n");

    /*
     * Common lazy loader and resolved.
     */
    fprintf(pOutput,
            "/*\n"
            " * The resolver code.\n"
            " */\n"
            "BEGINCODE\n"
            ".p2align 3\n"
            "LazyLoadResolver:\n"
            "    .cfi_startproc\n"
            "    /* Create frame. */\n"
            "    sub     sp, sp, #(16 + 192)\n"
            "    stp     x29, x30, [sp, #192]\n"
            "    add     x29, sp, #192\n"
            "    .cfi_def_cfa x29, 16\n"
            "    .cfi_offset x30, -8\n"
            "    .cfi_offset x29, -16\n"
            "    /* Save all argument registers and a handful of preserved ones. */\n"
            "    stp     x0,   x1, [sp, #(192 - 16)]\n"
            "    .cfi_offset  x0, -32\n"
            "    .cfi_offset  x1, -24\n"
            "    stp     x2,   x3, [sp, #(192 - 32)]\n"
            "    .cfi_offset  x3, -40\n"
            "    .cfi_offset  x2, -48\n"
            "    stp     x4,   x5, [sp, #(192 - 48)]\n"
            "    .cfi_offset  x6, -56\n"
            "    .cfi_offset  x5, -64\n"
            "    stp     x6,   x7, [sp, #(192 - 64)]\n"
            "    .cfi_offset  x7, -72\n"
            "    .cfi_offset  x6, -80\n"
            "    stp     x16, x17, [sp, #(192 - 80)]\n"
            "    .cfi_offset x17, -88\n"
            "    .cfi_offset x16, -96\n"
            "    stp     x18, x19, [sp, #(192 - 96)]\n"
            "    .cfi_offset x19, -104\n"
            "    .cfi_offset x18, -112\n"
            "    stp     x20, x21, [sp, #(192 - 112)]\n"
            "    .cfi_offset x21, -120\n"
            "    .cfi_offset x20, -128\n"
            "    stp     x22, x23, [sp, #(192 - 128)]\n"
            "    .cfi_offset x23, -136\n"
            "    .cfi_offset x22, -144\n"
            "    str     x8,       [sp, #(192 - 144)]\n"
            "\n"
            "    /* Shift the symbol name to x19 and g_pfnXXXX pointer to x20 as these are preserved registers\n"
            "     * (in case we need to call LazyLoadModule/RTLdrLoad) */\n"
            "    mov     x19, x9\n"
            "    mov     x20, x10\n"
            "\n"
            "    /* Get the module handle and call RTLdrGetSymbol(RTLDRMOD hLdrMod, const char *pszSymbol, void **ppvValue) */\n"
            "    adrp    x0, PAGE(g_hMod)\n"
            "    ldr     x0, [x0, PAGEOFF(g_hMod)]\n"
            "    cmp     x0, #0\n"
            "    b.ne    Lloaded\n"
            "    bl      LazyLoading\n"
            "Lloaded:\n"
            "    mov     x1, x19\n"
            "    mov     x2, x20\n"
            "    bl      NAME(RTLdrGetSymbol)\n"
            "\n"
            "    cmp     w0, #0\n"
            "    b.eq    Lreturn\n"
            "\n"
            "Lbadsym: /* Call sRTAssertMsg2Weak. Variadic (...) arguments are passed on the stack it seems. */\n"
            "    mov     x3, x0\n"
            "    adrp    x2, PAGE(g_szLibrary)\n"
            "    add     x2, x2, PAGEOFF(g_szLibrary)\n"
            "    mov     x1, x19\n"
            "    adrp    x0, PAGE(g_szFailLoadFmt)\n"
            "    add     x0, x0, PAGEOFF(g_szFailLoadFmt)\n"
            "    stp     x1, x2, [sp]\n"
            "    str     x3,     [sp, #16]\n"
            "    bl      NAME(RTAssertMsg2Weak)\n"
            "Lbadsymloop:\n"
            "    brk     #0x1\n"
            "    b       Lbadsymloop\n"

            "Lreturn:\n"
            "    /* Restore saved register */\n"
            "    ldr     x8,       [sp, #(192 - 144)]\n"
            "    .cfi_restore x8\n"
            "    ldp     x22, x23, [sp, #(192 - 128)]\n"
            "    .cfi_restore x23\n"
            "    .cfi_restore x22\n"
            "    ldp     x20, x21, [sp, #(192 - 112)]\n"
            "    .cfi_restore x21\n"
            "    .cfi_restore x20\n"
            "    ldp     x18, x19, [sp, #(192 - 96)]\n"
            "    .cfi_restore x19\n"
            "    .cfi_restore x18\n"
            "    ldp     x16, x17, [sp, #(192 - 80)]\n"
            "    .cfi_restore x17\n"
            "    .cfi_restore x18\n"
            "    ldp     x6,   x7, [sp, #(192 - 64)]\n"
            "    .cfi_restore x7\n"
            "    .cfi_restore x6\n"
            "    ldp     x4,   x5, [sp, #(192 - 48)]\n"
            "    .cfi_restore x5\n"
            "    .cfi_restore x4\n"
            "    ldp     x2,   x3, [sp, #(192 - 32)]\n"
            "    .cfi_restore x3\n"
            "    .cfi_restore x2\n"
            "    ldp     x0,   x1, [sp, #(192 - 16)]\n"
            "    .cfi_restore x1\n"
            "    .cfi_restore x0\n"
            "\n"
            "    ldp     x29, x30, [sp, #192]\n"
            "    .cfi_restore x29\n"
            "    .cfi_restore x30\n"
            "    add     sp, sp, #(16 + 192)\n"
            "    ret\n"
            "    .cfi_endproc\n"
            "\n"
            "\n");

    fprintf(pOutput,
            "/*\n"
            " * Loads the module.\n"
            " * ASSUMES called from LazyLoadResolver where all relevant registers are already saved.\n"
            " */\n"
            "LazyLoading:\n"
            "    .cfi_startproc\n"
            "    /* Create frame. */\n"
            "    sub     sp, sp, #(16 + 48)\n"
            "    stp     x29, x30, [sp, #48]\n"
            "    add     x29, sp, #48\n"
            "    .cfi_def_cfa x29, 16\n"
            "    .cfi_offset x30, -8\n"
            "    .cfi_offset x29, -16\n"
            "\n");

    if (!g_fSystemLibrary)
        fprintf(pOutput,
                "    /* Call SUPR3HardenedLdrLoadAppPriv(const char *pszFilename, PRTLDRMOD phLdrMod, uint32_t fFlags, PRTERRINFO pErrInfo); */\n"
                "    mov     x3, #0\n"
                "    mov     x2, #0\n"
                "    adrp    x1, PAGE(g_hMod)\n"
                "    add     x1, x1, PAGEOFF(g_hMod)\n"
                "    adrp    x0, PAGE(g_szLibrary)\n"
                "    add     x0, x0, PAGEOFF(g_szLibrary)\n"
                "    bl      NAME(SUPR3HardenedLdrLoadAppPriv)\n");
    else
        fprintf(pOutput,
                "    /* Call RTLdrLoadSystem(const char *pszFilename, bool fNoUnload, PRTLDRMOD phLdrMod); */\n"
                "    adrp    x2, PAGE(g_hMod)\n"
                "    add     x2, x2, PAGEOFF(g_hMod)\n"
                "    mov     x1, #1\n"
                "    adrp    x0, PAGE(g_szLibrary)\n"
                "    add     x0, x0, PAGEOFF(g_szLibrary)\n"
                "    bl      NAME(RTLdrLoadSystem)\n");

    fprintf(pOutput,
            "    cmp     w0, #0\n"
            "    b.eq    Lload_return\n"
            "\n"
            "Lbadload: /* Call sRTAssertMsg2Weak. Variadic (...) arguments are passed on the stack it seems. */\n"
            "    mov     x2, x0\n"
            "    adrp    x1, PAGE(g_szLibrary)\n"
            "    add     x1, x1, PAGEOFF(g_szLibrary)\n"
            "    adrp    x0, PAGE(g_szFailResolveFmt)\n"
            "    add     x0, x0, PAGEOFF(g_szFailResolveFmt)\n"
            "    stp     x1, x2, [sp]\n"
            "    bl      NAME(RTAssertMsg2Weak)\n"
            "Lbadloadloop:\n"
            "    brk     #0x1\n"
            "    b       Lbadloadloop\n"
            "Lload_return:\n"
            "    adrp    x0, PAGE(g_hMod)\n"
            "    ldr     x0, [x0, PAGEOFF(g_hMod)]\n"
            "    ldp     x29, x30, [sp, #48]\n"
            "    .cfi_restore x29\n"
            "    .cfi_restore x30\n"
            "    add     sp, sp, #(16 + 48)\n"
            "    ret\n"
            "    .cfi_endproc\n"
            "ENDCODE\n"
            "\n"
            "\n");

    /*
     * C callable method for explicitly loading the library and optionally
     * resolving all the imports.
     */
    if (g_fWithExplictLoadFunction)
    {
        int cchLibBaseName = (int)(strchr(g_pszLibrary, '.') ? strchr(g_pszLibrary, '.') - g_pszLibrary : strlen(g_pszLibrary));
        fprintf(pOutput,
                "/**\n"
                " * ExplicitlyLoad%.*s(bool fResolveAllImports, pErrInfo);\n"
                " */\n"
                "BEGINCODE\n"
                ".p2align 3\n"
                ".globl NAME(ExplicitlyLoad%.*s)\n"
                "NAME(ExplicitlyLoad%.*s):\n"
                "    .cfi_startproc\n"
                "    /* Create frame. */\n"
                "    sub     sp, sp, #(16 + 96)\n"
                "    stp     x29, x30, [sp, #96]\n"
                "    add     x29, sp, #96\n"
                "    .cfi_def_cfa x29, 16\n"
                "    .cfi_offset x30, -8\n"
                "    .cfi_offset x29, -16\n"
                "\n"
                "    stp     x20, x21, [sp, #(96 - 16)]\n"
                "    .cfi_offset x21, -24\n"
                "    .cfi_offset x20, -32\n"
                "    stp     x22, x23, [sp, #(96 - 32)]\n"
                "    .cfi_offset x23, -40\n"
                "    .cfi_offset x22, -48\n"

                "    /* Save the input parameters. */\n"
                "    mov     x20, x0\n"
                "    mov     x21, x1\n"
                "\n"
                "    /*\n"
                "     * Is the module already loaded?\n"
                "     */\n"
                "    adrp    x0, PAGE(g_hMod)\n"
                "    ldr     x0, [x0, PAGEOFF(g_hMod)]\n"
                "    cmp     x0, #0\n"
                "    b.ne    Lexplicit_loaded_module\n"
                "\n"
                ,
                cchLibBaseName, g_pszLibrary,
                cchLibBaseName, g_pszLibrary,
                cchLibBaseName, g_pszLibrary);
        fprintf(pOutput,
                "Lexplicit_load_module:\n");
        if (!g_fSystemLibrary)
            fprintf(pOutput,
                    "    /* Call SUPR3HardenedLdrLoadAppPriv(const char *pszFilename, PRTLDRMOD phLdrMod, uint32_t fFlags, PRTERRINFO pErrInfo); */\n"
                    "    mov     x3, x21\n"
                    "    mov     x2, #0\n"
                    "    adrp    x1, PAGE(g_hMod)\n"
                    "    add     x1, x1, PAGEOFF(g_hMod)\n"
                    "    adrp    x0, PAGE(g_szLibrary)\n"
                    "    add     x0, x0, PAGEOFF(g_szLibrary)\n"
                    "    bl      NAME(SUPR3HardenedLdrLoadAppPriv)\n");
        else
            fprintf(pOutput,
                    "    /* Call RTLdrLoadSystem(const char *pszFilename, bool fNoUnload, PRTLDRMOD phLdrMod); */\n"
                    "    adrp    x2, PAGE(g_hMod)\n"
                    "    add     x2, x2, PAGEOFF(g_hMod)\n"
                    "    mov     x1, #1\n"
                    "    adrp    x0, PAGE(g_szLibrary)\n"
                    "    add     x0, x0, PAGEOFF(g_szLibrary)\n"
                    "    bl      NAME(RTLdrLoadSystem)\n");
        fprintf(pOutput,
                "    cmp     x0, #0\n"
                "    b.ne    Lexplicit_load_return\n"
                "\n");

        fprintf(pOutput,
                "    /*\n"
                "     * Resolve the imports too if requested to do so.\n"
                "     */\n"
                "Lexplicit_loaded_module:\n"
                "    cmp     w20, #0\n"
                "    b.eq    Lexplicit_load_return\n"
                "\n"
                "    adrp     x22, PAGE(g_szzNames)\n"
                "    add      x22, x22, PAGEOFF(g_szzNames)\n"
                "    adrp     x23, PAGE(g_apfnImports)\n"
                "    add      x23, x23, PAGEOFF(g_apfnImports)\n"
                "Lexplicit_load_next_import:\n"
                "    ldr     x0, [x23]\n"
                "    cmp     x0, #0\n"
                "    b.eq    Lexplicit_load_return\n"
                "\n"
                "    /* Get the module handle and call RTLdrGetSymbol(RTLDRMOD hLdrMod, const char *pszSymbol, void **ppvValue) */\n"
                "    adrp    x0, PAGE(g_hMod)\n"
                "    ldr     x0, [x0, PAGEOFF(g_hMod)]\n"
                "    mov     x1, x22\n"
                "    mov     x2, x23\n"
                "    bl      NAME(RTLdrGetSymbol)\n"
                "    cmp     x0, #0\n"
                "    b.ne    Lexplicit_load_symbol_error\n"
                "\n"
                "    /* Advance. */\n"
                "    add     x23, x23, #8\n"
                "Lexplict_load_advance_string:\n"
                "    ldrb    w0, [x22]\n"
                "    add     x22, x22, #1\n"
                "    cmp     w0, #0\n"
                "    b.ne    Lexplict_load_advance_string\n"
                "    b       Lexplicit_load_next_import\n"
                "\n"
                "    /*\n"
                "     * Error loading a symbol. Call RTErrInfoSet(PRTERRINFO pErrInfo, int rc, const char *pszMsg) on pErrInfo (preserves x0).\n"
                "     */\n"
                "Lexplicit_load_symbol_error:\n"
                "    mov     x2, x22\n"
                "    mov     x1, x0\n"
                "    mov     x0, x21\n"
                "    bl      NAME(RTErrInfoSet)\n"
                "    b       Lexplicit_load_return"
                "    "
                "\n"
                "Lexplicit_load_return:\n"
                "    ldp     x22,   x23, [sp, #(96 - 32)]\n"
                "    .cfi_restore x23\n"
                "    .cfi_restore x22\n"
                "    ldp     x20,   x21, [sp, #(96 - 16)]\n"
                "    .cfi_restore x21\n"
                "    .cfi_restore x20\n"
                "\n"
                "    ldp     x29, x30, [sp, #96]\n"
                "    .cfi_restore x29\n"
                "    .cfi_restore x30\n"
                "    add     sp, sp, #(16 + 96)\n"
                "    ret\n"
                "    .cfi_endproc\n"
                "ENDCODE\n"
                "\n"
                "\n");
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Generates the assembly source code, writing it to g_pszOutput.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE, in the latter case full
 *          details has been displayed.
 */
static RTEXITCODE generateOutput(void)
{
    RTEXITCODE rcExit = RTEXITCODE_FAILURE;
    FILE *pOutput = fopen(g_pszOutput, "w");
    if (pOutput)
    {
        switch (g_enmTarget)
        {
            case RTLDRARCH_AMD64:
            case RTLDRARCH_X86_32:
                rcExit = generateOutputInnerX86AndAMD64(pOutput);
                break;
            case RTLDRARCH_ARM64:
                rcExit = generateOutputInnerArm64(pOutput);
                break;
            default:
                rcExit = RTEXITCODE_FAILURE;
                break;
        }
        if (fclose(pOutput))
        {
            fprintf(stderr, "error: Error closing '%s'.\n", g_pszOutput);
            rcExit = RTEXITCODE_FAILURE;
        }
    }
    else
        fprintf(stderr, "error: Failed to open '%s' for writing.\n", g_pszOutput);
    return rcExit;
}


/**
 * Displays usage information.
 *
 * @returns RTEXITCODE_SUCCESS.
 * @param   pszArgv0            The argv[0] string.
 */
static int usage(const char *pszArgv0)
{
    const char *pszTmp = strrchr(pszArgv0, '/');
    if (pszTmp)
        pszArgv0 = pszTmp + 1;
    pszTmp = strrchr(pszArgv0, '\\');
    if (pszTmp)
        pszArgv0 = pszTmp + 1;

    /*      0         1         2         3         4         5         6         7         8
            012345678901234567890123456789012345678901234567890123456789012345678901234567890 */
    printf("VBoxDef2LazyLoad - Lazy DLL/SO/DYLIB loader code generator.\n"
           "Copyright (C) 2013-2016 Oracle Corporation\n"
           "\n"
           "Description:\n"
           "------------\n"
           "\n"
           "Takes a Microsoft-style linker definition file for a library (DLL/SO/DYLIB) and\n"
           "generates assembly code which defines stub functions that lazily loads and\n"
           "resolves the real symbols before calling them. This is entirely transparent when\n"
           "used with functions.\n"
           "\n"
           "With data symbols it's more messy since the compiler will not invoke code when\n"
           "using them, but access them directly (ELF executables) or indirectly (ELF SOs,\n"
           "PE, ++). For data symbols use the DATA keyword after the symbol name in the\n"
           "def-file and modify the header definition from 'extern type symbol;' to:\n"
           "\n"
           "    DECLASM(type *) LazyGetPtr_<symbol>(void);\n"
           "    #define <symbol> (*LazyGetPtr_<symbol>())\n"
           "\n"
           "or, if using --explict-load-function this will work as well:\n"
           "\n"
           "    extern type *g_LazyPtr_<symbol>;\n"
           "    #define <symbol> (*g_LazyPtr_)\n"
           "\n"
           "Usage:\n"
           "------\n"
           "%s [options] --libary <loadname> --output <lazyload.asm> <input.def>\n"
           "\n"
           "Options:\n"
           "--------\n"
           "  --library <loadname>, -l <loadname>\n"
           "    The name of the library.  This is what will be passed to RTLdrLoadSystem\n"
           "    or SUPR3HardenedLdrLoadAppPriv.\n"
           "  --output <filename>, -o <filename>\n"
           "    The assembly output file.\n"
           "  --explicit-load-function, --no-explicit-load-function\n"
           "    Whether to include the explicit load function:\n"
           "      DECLASM(int) ExplicitlyLoad<basename>(bool fResolveAllImports, pErrInfo);\n"
           "    The default is not to include it.\n"
           "  --system\n"
           "    The library is a system DLL to be loaded using RTLdrLoadSystem.\n"
           "    The default is to use SUPR3HardenedLdrLoadAppPriv to load it.\n"
           "\n"
           , pszArgv0);

    return RTEXITCODE_SUCCESS;
}


int main(int argc, char **argv)
{
    /*
     * Parse options.
     */
    for (int i = 1; i < argc; i++)
    {
        const char *psz = argv[i];
        if (*psz == '-')
        {
            if (!strcmp(psz, "--output") || !strcmp(psz, "-o"))
            {
                if (++i >= argc)
                {
                    fprintf(stderr, "syntax error: File name expected after '%s'.\n", psz);
                    return RTEXITCODE_SYNTAX;
                }
                g_pszOutput = argv[i];
            }
            else if (!strcmp(psz, "--library") || !strcmp(psz, "-l"))
            {
                if (++i >= argc)
                {
                    fprintf(stderr, "syntax error: Library name expected after '%s'.\n", psz);
                    return RTEXITCODE_SYNTAX;
                }
                g_pszLibrary = argv[i];
            }
            else if (!strcmp(psz, "--explicit-load-function"))
                g_fWithExplictLoadFunction = true;
            else if (!strcmp(psz, "--no-explicit-load-function"))
                g_fWithExplictLoadFunction = false;
            else if (!strcmp(psz, "--system"))
                g_fSystemLibrary = true;
            /** @todo Support different load methods so this can be used on system libs and
             *        such if we like. */
            else if (   !strcmp(psz, "--help")
                     || !strcmp(psz, "-help")
                     || !strcmp(psz, "-h")
                     || !strcmp(psz, "-?") )
                return usage(argv[0]);
            else if (   !strcmp(psz, "--version")
                     || !strcmp(psz, "-V"))
            {
                printf("$Revision: 106320 $\n");
                return RTEXITCODE_SUCCESS;
            }
            else
            {
                fprintf(stderr, "syntax error: Unknown option '%s'.\n", psz);
                return RTEXITCODE_SYNTAX;
            }
        }
        else
        {
            if (g_cInputs >= RT_ELEMENTS(g_apszInputs))
            {
                fprintf(stderr, "syntax error: Too many input files, max is %d.\n", (int)RT_ELEMENTS(g_apszInputs));
                return RTEXITCODE_SYNTAX;
            }
            g_apszInputs[g_cInputs++] = argv[i];
        }
    }
    if (g_cInputs == 0)
    {
        fprintf(stderr, "syntax error: No input file specified.\n");
        return RTEXITCODE_SYNTAX;
    }
    if (!g_pszOutput)
    {
        fprintf(stderr, "syntax error: No output file specified.\n");
        return RTEXITCODE_SYNTAX;
    }
    if (!g_pszLibrary)
    {
        fprintf(stderr, "syntax error: No library name specified.\n");
        return RTEXITCODE_SYNTAX;
    }

    /*
     * Do the job.
     */
    RTEXITCODE rcExit = parseInputs();
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = generateOutput();
    return rcExit;
}

