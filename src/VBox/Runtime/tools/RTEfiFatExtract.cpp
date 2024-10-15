/* $Id: RTEfiFatExtract.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Utility for extracting single files from a fat EFI binary.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#include <iprt/formats/efi-fat.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>



static int efiFatExtractList(const char *pszInput)
{
    RTFILE hFile = NIL_RTFILE;
    int rc = RTFileOpen(&hFile, pszInput, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        EFI_FATHDR Hdr;
        rc = RTFileReadAt(hFile, 0, &Hdr, sizeof(Hdr), NULL);
        if (RT_SUCCESS(rc))
        {
            if (   RT_LE2H_U32(Hdr.u32Magic) == EFI_FATHDR_MAGIC
                && RT_LE2H_U32(Hdr.cFilesEmbedded) <= 16 /*Arbitrary number*/)
            {
                RTFOFF offRead = sizeof(Hdr);

                for (uint32_t i = 0; i < RT_LE2H_U32(Hdr.cFilesEmbedded); i++)
                {
                    EFI_FATDIRENTRY Entry;
                    rc = RTFileReadAt(hFile, offRead, &Entry, sizeof(Entry), NULL);
                    if (RT_SUCCESS(rc))
                    {
                        RTPrintf("Entry %u:\n", i);
                        RTPrintf("    CPU Type:    %#x\n", RT_LE2H_U32(Entry.u32CpuType));
                        RTPrintf("    CPU Subtype: %#x\n", RT_LE2H_U32(Entry.u32CpuSubType));
                        RTPrintf("    Offset:      %#x\n", RT_LE2H_U32(Entry.u32OffsetStart));
                        RTPrintf("    Size:        %#x\n", RT_LE2H_U32(Entry.cbFile));
                        RTPrintf("    Alignment:   %#x\n", RT_LE2H_U32(Entry.u32Alignment));
                    }
                    else
                    {
                        RTPrintf("Failed to read file entry %u of '%s': %Rrc\n", pszInput, i, rc);
                        break;
                    }

                    offRead += sizeof(Entry);
                }
            }
            else
            {
                rc = VERR_INVALID_MAGIC;
                RTPrintf("The header contains invalid values\n");
            }
        }
        else
            RTPrintf("Failed to read header of '%s': %Rrc\n", pszInput, rc);

        RTFileClose(hFile);
    }
    else
        RTPrintf("Failed to open file '%s': %Rrc\n", pszInput, rc);

    return rc;
}


static int efiFatExtractSave(const char *pszInput, uint32_t idxEntry, const char *pszOut)
{
    RTFILE hFile = NIL_RTFILE;
    int rc = RTFileOpen(&hFile, pszInput, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        EFI_FATHDR Hdr;
        rc = RTFileReadAt(hFile, 0, &Hdr, sizeof(Hdr), NULL);
        if (RT_SUCCESS(rc))
        {
            if (   RT_LE2H_U32(Hdr.u32Magic) == EFI_FATHDR_MAGIC
                && RT_LE2H_U32(Hdr.cFilesEmbedded) <= 16 /*Arbitrary number*/)
            {
                if (idxEntry < RT_LE2H_U32(Hdr.cFilesEmbedded))
                {
                    EFI_FATDIRENTRY Entry;

                    rc = RTFileReadAt(hFile, sizeof(Hdr) + idxEntry * sizeof(Entry), &Entry, sizeof(Entry), NULL);
                    if (RT_SUCCESS(rc))
                    {
                        void *pvFile = RTMemAllocZ(RT_LE2H_U32(Entry.cbFile));
                        if (RT_LIKELY(pvFile))
                        {
                            rc = RTFileReadAt(hFile, RT_LE2H_U32(Entry.u32OffsetStart), pvFile, RT_LE2H_U32(Entry.cbFile), NULL);
                            if (RT_SUCCESS(rc))
                            {
                                RTFILE hFileOut;
                                rc = RTFileOpen(&hFileOut, pszOut, RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE);
                                if (RT_SUCCESS(rc))
                                {
                                    rc = RTFileWrite(hFileOut, pvFile, RT_LE2H_U32(Entry.cbFile), NULL);
                                    if (RT_FAILURE(rc))
                                        RTPrintf("Failed to write output file '%s': %Rrc\n", pszOut, rc);
                                    RTFileClose(hFileOut);
                                }
                                else
                                    RTPrintf("Failed to create output file '%s': %Rrc\n", pszOut, rc);
                            }
                            else
                                RTPrintf("Failed to read embedded file %u: %Rrc\n", idxEntry, rc);

                            RTMemFree(pvFile);
                        }
                        else
                            RTPrintf("Failed to allocate %u bytes of memory\n", RT_LE2H_U32(Entry.cbFile));
                    }
                    else
                        RTPrintf("Failed to read file entry %u of '%s': %Rrc\n", pszInput, idxEntry, rc);
                }
                else
                {
                    rc = VERR_INVALID_PARAMETER;
                    RTPrintf("Given index out of range, maximum is %u\n", RT_LE2H_U32(Hdr.cFilesEmbedded));
                }
            }
            else
            {
                rc = VERR_INVALID_MAGIC;
                RTPrintf("The header contains invalid values\n");
            }
        }
        else
            RTPrintf("Failed to read header of '%s': %Rrc\n", pszInput, rc);

        RTFileClose(hFile);
    }
    else
        RTPrintf("Failed to open file '%s': %Rrc\n", pszInput, rc);

    return rc;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--input",    'i', RTGETOPT_REQ_STRING },
        { "--output",   'o', RTGETOPT_REQ_STRING },
        { "--entry",    'e', RTGETOPT_REQ_UINT32 },
        { "--help",     'h', RTGETOPT_REQ_NOTHING },
        { "--version",  'V', RTGETOPT_REQ_NOTHING },
    };

    RTEXITCODE      rcExit   = RTEXITCODE_SUCCESS;
    const char     *pszInput = NULL;
    const char     *pszOut   = NULL;
    uint32_t       idxEntry  = UINT32_C(0xffffffff);

    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            case 'h':
                RTPrintf("Usage: %s [options]\n"
                         "\n"
                         "Options:\n"
                         "  -i,--input=<file>\n"
                         "      Input file\n"
                         "  -e,--entry=<idx>\n"
                         "      Selects the entry for saving\n"
                         "  -o,--output=file\n"
                         "      Save the specified entry to this file\n"
                         "  -h, -?, --help\n"
                         "      Display this help text and exit successfully.\n"
                         "  -V, --version\n"
                         "      Display the revision and exit successfully.\n"
                         , RTPathFilename(argv[0]));
                return RTEXITCODE_SUCCESS;
            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                return RTEXITCODE_SUCCESS;

            case 'i':
                pszInput = ValueUnion.psz;
                break;
            case 'o':
                pszOut = ValueUnion.psz;
                break;
            case 'e':
                idxEntry = ValueUnion.u32;
                break;
            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    if (!pszInput)
    {
        RTPrintf("An input path must be given\n");
        return RTEXITCODE_FAILURE;
    }

    if (!pszOut || idxEntry == UINT32_C(0xffffffff))
        rc = efiFatExtractList(pszInput);
    else
        rc = efiFatExtractSave(pszInput, idxEntry, pszOut);
    if (RT_FAILURE(rc))
        rcExit = RTEXITCODE_FAILURE;

    return rcExit;
}

