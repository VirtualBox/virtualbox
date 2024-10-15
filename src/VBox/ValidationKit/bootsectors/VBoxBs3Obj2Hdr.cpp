/* $Id: VBoxBs3Obj2Hdr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Validation Kit - Boot Sector 3 Assembly Object file to C-Header converter.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iprt/formats/omf.h>
#include <iprt/types.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
unsigned g_cVerbose = 0;


static RTEXITCODE ProcessObjectFile(FILE *pOutput, uint8_t const *pbFile, size_t cbFile, const char *pszFile)
{
    size_t   off      = 0;
    unsigned cPubDefs = 0;
    while (off + sizeof(OMFRECHDR) <= cbFile)
    {
        PCOMFRECHDR const pHdr = (PCOMFRECHDR)&pbFile[off];
        if (pHdr->cbLen + sizeof(*pHdr) > cbFile)
        {
            fprintf(stderr, "error: %s: bogus record header lenght: %#x LB %#x, max length %#zx\n",
                    pszFile, (unsigned)pHdr->bType, (unsigned)pHdr->cbLen, cbFile - off);
            return RTEXITCODE_FAILURE;
        }

        uint8_t const * const pbRec     = (uint8_t const *)(pHdr + 1);
        unsigned              cbRec     = pHdr->cbLen;
        unsigned              offRec    = 0;
#define OMF_CHECK_RET(a_cbReq, a_Name) /* Not taking the checksum into account, so we're good with 1 or 2 byte fields. */ \
            if (offRec + (a_cbReq) <= cbRec) {/*likely*/} \
            else \
            { \
                fprintf(stderr, "error: %s: Malformed " #a_Name "! off=%#zx offRec=%#x cbRec=%#x cbNeeded=%#x line=%d\n", \
                        pszFile, off, offRec, cbRec, (a_cbReq), __LINE__); \
                return RTEXITCODE_FAILURE; \
            }
#define OMF_READ_IDX(a_idx, a_Name) \
            do { \
                OMF_CHECK_RET(2, a_Name); \
                a_idx = pbRec[offRec++]; \
                if ((a_idx) & 0x80) \
                    a_idx = (((a_idx) & 0x7f) << 8) | pbRec[offRec++]; \
            } while (0)

        switch (pHdr->bType)
        {
            case OMF_PUBDEF16:
            case OMF_PUBDEF32:
            {
                const char *pszRec = "PUBDEF";
                char        chType = 'T';

                uint16_t idxGrp;
                OMF_READ_IDX(idxGrp, PUBDEF);

                uint16_t idxSeg;
                OMF_READ_IDX(idxSeg, PUBDEF);

                uint16_t uFrameBase = 0;
                if (idxSeg == 0)
                {
                    OMF_CHECK_RET(2, PUBDEF);
                    uFrameBase = RT_MAKE_U16(pbRec[offRec], pbRec[offRec + 1]);
                    offRec += 2;
                }
                if (g_cVerbose > 2)
                    printf("  %s: idxGrp=%#x idxSeg=%#x uFrameBase=%#x\n", pszRec, idxGrp, idxSeg, uFrameBase);
                uint16_t const uSeg = idxSeg ? idxSeg : uFrameBase;

                while (offRec + 1 < cbRec)
                {
                    uint8_t     cch = pbRec[offRec++];
                    OMF_CHECK_RET(cch, PUBDEF);
                    const char *pchName = (const char *)&pbRec[offRec];
                    offRec += cch;

                    uint32_t offSeg;
                    if (pHdr->bType & OMF_REC32)
                    {
                        OMF_CHECK_RET(4, PUBDEF);
                        offSeg = RT_MAKE_U32_FROM_U8(pbRec[offRec], pbRec[offRec + 1], pbRec[offRec + 2], pbRec[offRec + 3]);
                        offRec += 4;
                    }
                    else
                    {
                        OMF_CHECK_RET(2, PUBDEF);
                        offSeg = RT_MAKE_U16(pbRec[offRec], pbRec[offRec + 1]);
                        offRec += 2;
                    }

                    uint16_t idxType;
                    OMF_READ_IDX(idxType, PUBDEF);

                    if (g_cVerbose > 2)
                        printf("  %s[%u]: off=%#010x type=%#x %-*.*s\n", pszRec, cPubDefs, offSeg, idxType, cch, cch, pchName);
                    else if (g_cVerbose > 0)
                        printf("%04x:%08x %c %-*.*s\n", uSeg, offSeg, chType, cch, cch, pchName);

                    /* Produce the headerfile output. */
                    if (*pchName == '_') /** @todo add more filtering */
                    {
                        cch     -= 1;
                        pchName += 1;

                        static const char s_szEndProc[] = "_EndProc";
                        if (   cch < sizeof(s_szEndProc)
                            || memcmp(&pchName[cch - sizeof(s_szEndProc) + 1], RT_STR_TUPLE(s_szEndProc)) != 0)
                            fprintf(pOutput,  "extern FNBS3FAR %-*.*s;\n", cch, cch, pchName);
                    }
                }
                cPubDefs++;
                break;
            }
        }

        off += pHdr->cbLen + sizeof(*pHdr);
    }
    return RTEXITCODE_SUCCESS;
}


/**
 * Opens a file for binary reading or writing.
 *
 * @returns File stream handle.
 * @param   pszFile             The name of the file.
 * @param   fWrite              Whether to open for writing or reading.
 */
static FILE *openfile(const char *pszFile,  bool fWrite)
{
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    FILE *pFile = fopen(pszFile, fWrite ? "wb" : "rb");
#else
    FILE *pFile = fopen(pszFile, fWrite ? "w" : "r");
#endif
    if (!pFile)
        fprintf(stderr, "error: Failed to open '%s' for %s: %s (%d)\n",
                pszFile, fWrite ? "writing" : "reading", strerror(errno), errno);
    return pFile;
}


/**
 * Read the given file into memory.
 *
 * @returns true on success, false on failure.
 * @param   pszFile     The file to read.
 * @param   ppvFile     Where to return the memory.
 * @param   pcbFile     Where to return the size.
 */
static bool readfile(const char *pszFile, void **ppvFile, size_t *pcbFile)
{
    FILE *pFile = openfile(pszFile, false);
    if (pFile)
    {
        /*
         * Figure the size.
         */
        if (fseek(pFile, 0, SEEK_END) == 0)
        {
            long cbFile = ftell(pFile);
            if (cbFile > 0)
            {
                if (fseek(pFile, SEEK_SET, 0) == 0)
                {
                    /*
                     * Allocate and read content.
                     */
                    void *pvFile = malloc((size_t)cbFile);
                    if (pvFile)
                    {
                        if (fread(pvFile, cbFile, 1, pFile) == 1)
                        {
                            *ppvFile = pvFile;
                            *pcbFile = (size_t)cbFile;
                            fclose(pFile);
                            return true;
                        }
                        free(pvFile);
                        fprintf(stderr, "error: fread failed in '%s': %s (%d)\n", pszFile, strerror(errno), errno);
                    }
                    else
                        fprintf(stderr, "error: failed to allocate %ld bytes of memory for '%s'\n", cbFile, pszFile);
                }
                else
                    fprintf(stderr, "error: fseek #2 failed in '%s': %s (%d)\n", pszFile, strerror(errno), errno);
            }
            else
                fprintf(stderr, "error: ftell failed in '%s': %s (%d)\n", pszFile, strerror(errno), errno);
        }
        else
            fprintf(stderr, "error: fseek #1 failed in '%s': %s (%d)\n", pszFile, strerror(errno), errno);
        fclose(pFile);
    }
    return false;
}


static RTEXITCODE makeParfaitHappy(int argc, char **argv, FILE **ppOutput, const char **ppszOutput)
{
    /*
     * Parse arguments.
     */
    bool        fDashDash = false;
    *ppszOutput = NULL;
    *ppOutput   = NULL;
    for (int i = 1; i < argc; i++)
    {
        const char *pszArg = argv[i];
        if (*pszArg == '-' && !fDashDash)
        {
            if (*++pszArg == '-')
            {
                pszArg++;
                if (!*pszArg)
                {
                    fDashDash = true;
                    continue;
                }
                if (strcmp(pszArg, "output") == 0)
                    pszArg = "o";
                else if (strcmp(pszArg, "help") == 0)
                    pszArg = "h";
                else if (strcmp(pszArg, "version") == 0)
                    pszArg = "V";
                else
                {
                    fprintf(stderr, "syntax error: Unknown parameter: %s\n", argv[i]);
                    return RTEXITCODE_SYNTAX;
                }
            }
            else if (*pszArg)
            {
                fprintf(stderr, "syntax error: Unknown parameter: %s\n", argv[i]);
                return RTEXITCODE_SYNTAX;
            }

            for (;;)
            {
                const char *pszValue = NULL;
                char const  chOpt    = *pszArg++;
                switch (chOpt)
                {
                    case 'o':
                        if (*pszArg)
                            pszValue = pszArg;
                        else if (i + 1 < argc)
                            pszValue = argv[++i];
                        else
                        {
                            fprintf(stderr, "syntax error: Expected value after option '%c'.\n", chOpt);
                            return RTEXITCODE_SYNTAX;
                        }
                        break;
                }


                switch (chOpt)
                {
                    case '\0':
                        break;

                    case 'o':
                    {
                        FILE * const pOldOutput = *ppOutput;
                        *ppOutput = NULL;
                        if (pOldOutput && pOldOutput != stdout)
                        {
                            if (fclose(pOldOutput))
                            {
                                fprintf(stderr, "error: Write/close error on '%s'\n", *ppszOutput);
                                return RTEXITCODE_FAILURE;
                            }
                        }
                        *ppszOutput = pszValue;
                        continue;
                    }

                    case 'q':
                        g_cVerbose = 0;
                        break;

                    case 'v':
                        g_cVerbose++;
                        break;

                    case 'h':
                        printf("usage: %s --output <hdr> <obj1> [<obj2> []...]\n", argv[0]);
                        return RTEXITCODE_SUCCESS;

                    case 'V':
                        printf("0.0.0");
                        return RTEXITCODE_SUCCESS;

                    default:
                        fprintf(stderr, "syntax error: Unknown option: %c (%s)\n", chOpt, argv[i]);
                        return RTEXITCODE_SYNTAX;
                }
                break;
            }
        }
        else
        {
            /* Make sure we've got an output file. */
            if (!*ppOutput)
            {
                if (!*ppszOutput || strcmp(*ppszOutput, "-") == 0)
                    *ppOutput = stdout;
                else
                {
                    *ppOutput = fopen(*ppszOutput, "w");
                    if (!*ppOutput)
                    {
                        fprintf(stderr, "error: Failed to open '%s' for writing!\n", *ppszOutput);
                        return RTEXITCODE_FAILURE;
                    }
                }
            }

            /* Read in the object file and process it. */
            size_t cbFile;
            void  *pvFile;
            if (!readfile(pszArg, &pvFile, &cbFile))
                return RTEXITCODE_FAILURE;

            RTEXITCODE rcExit = ProcessObjectFile(*ppOutput, (uint8_t const *)pvFile, cbFile, pszArg);

            free(pvFile);
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;
        }
    }

    return RTEXITCODE_SUCCESS;
}


int main(int argc, char **argv)
{
    /*
     * Use helper function to do the actual main work so we can perform
     * cleanup to make pedantic static analysers happy.
     */
    const char *pszOutput = NULL;
    FILE       *pOutput = NULL;
    RTEXITCODE  rcExit = makeParfaitHappy(argc, argv, &pOutput, &pszOutput);

    /*
     * Flush+close output before we exit.
     */
    if (pOutput)
    {
        if (   fflush(pOutput)
            || fclose(pOutput))
        {
            fprintf(stderr, "error: Write error on '%s'\n", pszOutput ? pszOutput : "-");
            rcExit = RTEXITCODE_FAILURE;
        }
    }
    return rcExit;
}

