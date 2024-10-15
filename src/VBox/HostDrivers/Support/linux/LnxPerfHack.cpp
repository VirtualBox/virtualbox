/* $Id: LnxPerfHack.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * LnxPerfHack - Dirty hack to make perf find our .r0 modules.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/getopt.h>
#include <iprt/ldr.h>
#include <iprt/sort.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/mem.h>
#include <iprt/message.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define LNXPERFILEHDR_MAGIC RT_MAKE_U64_FROM_U8('P','E','R','F','I','L','E','2')

#define LNXPERF_RECORD_MMAP     1
#define LNXPERF_RECORD_MMAP2    10

#define LNXPERF_RECORD_MISC_CPUMODE_MASK        UINT16_C(0x0007)
#define LNXPERF_RECORD_MISC_CPUMODE_UNKNOWN     UINT16_C(0x0000)
#define LNXPERF_RECORD_MISC_KERNEL              UINT16_C(0x0001)
#define LNXPERF_RECORD_MISC_USER                UINT16_C(0x0002)
#define LNXPERF_RECORD_MISC_HYPERVISOR          UINT16_C(0x0003)
#define LNXPERF_RECORD_MISC_GUEST_KERNEL        UINT16_C(0x0004)
#define LNXPERF_RECORD_MISC_GUEST_USER          UINT16_C(0x0005)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** The file header. */
typedef struct LNXPERFFILEHDR
{
    uint64_t        uMagic;     /**< LNXPERFILEHDR_MAGIC */
    uint64_t        cbHdr;
    uint64_t        cbAttr;
    struct LNXPERFFILESECTION
    {
        uint64_t    off, cb;
    }               Attrs, Data, EventTypes;
    uint64_t        bmAddsFeatures[256/64];
} LNXPERFFILEHDR;
typedef LNXPERFFILEHDR *PLNXPERFFILEHDR;


typedef struct LNXPERFRECORDHEADER
{
    uint32_t        uType;
    uint16_t        fMisc;
    uint16_t        cb;
} LNXPERFRECORDHEADER;
AssertCompileSize(LNXPERFRECORDHEADER, 8);
typedef LNXPERFRECORDHEADER *PLNXPERFRECORDHEADER;

typedef struct LNXPERFRECORDMMAP
{
    LNXPERFRECORDHEADER Hdr;
    uint32_t            pid;
    uint32_t            tid;
    uint64_t            uAddress;
    uint64_t            cbMapping;
    uint64_t            offFile;
    RT_FLEXIBLE_ARRAY_EXTENSION
    char                szFilename[RT_FLEXIBLE_ARRAY];
} LNXPERFRECORDMMAP;
typedef LNXPERFRECORDMMAP *PLNXPERFRECORDMMAP;

typedef struct MYMODULE
{
    uint64_t    uAddress;
    uint64_t    cbMapping;
    uint64_t    offFile;
    const char *pszName;
    uint32_t    cchName;
    uint16_t    cbRecord;
    uint64_t    offRecord;
} MYMODULE;
typedef MYMODULE *PMYMODULE;


static DECLCALLBACK(int) CompModuleRecordOffset(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    PMYMODULE pLeft  = (PMYMODULE)pvElement1;
    PMYMODULE pRight = (PMYMODULE)pvElement2;
    RT_NOREF(pvUser);
    return pLeft->offRecord < pRight->offRecord ? -1
         : pLeft->offRecord > pRight->offRecord ? 1 : 0;

}


static DECLCALLBACK(int) CompModuleNameLengthDesc(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    PMYMODULE pLeft  = (PMYMODULE)pvElement1;
    PMYMODULE pRight = (PMYMODULE)pvElement2;
    RT_NOREF(pvUser);
    return pLeft->cchName < pRight->cchName ? 1
         : pLeft->cchName > pRight->cchName ? -1 : 0;
}


/** @callback_method_impl{FNRTLDRENUMSEGS} */
static DECLCALLBACK(int) SegmentEnumCallback(RTLDRMOD hLdrMod, PCRTLDRSEG pSeg, void *pvUser)
{
    RT_NOREF(hLdrMod);
    if (pSeg->pszName && RTStrStartsWith(pSeg->pszName, ".text"))
    {
        PMYMODULE pModEntry = (PMYMODULE)pvUser;
        //pModEntry->offFile   = pModEntry->uAddress;
        pModEntry->uAddress += pSeg->RVA;
        pModEntry->cbMapping = pSeg->cbMapped;
        //pModEntry->offFile   = pModEntry->uAddress - pSeg->LinkAddress;
        pModEntry->offFile   = RT_MAX(pSeg->offFile, 0);
        return VINF_CALLBACK_RETURN;
    }
    return VINF_SUCCESS;
}



int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--input",    'i', RTGETOPT_REQ_STRING },
        { "--output",   'o', RTGETOPT_REQ_STRING },
        { "--module",   'm', RTGETOPT_REQ_STRING },
        { "--quiet",    'q', RTGETOPT_REQ_NOTHING },
        { "--verbose",  'v', RTGETOPT_REQ_NOTHING },
    };
    const char *pszInput   = NULL;
    const char *pszOutput  = NULL;
    unsigned    cVerbosity = 0;
    unsigned    cModules   = 0;
    MYMODULE    aModules[10];
    unsigned    cSkipPatterns = 1;
    const char *apszSkipPatterns[10] = { "*kallsyms*", };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRCReturn(rc, RTMsgErrorExitFailure("RTGetOptInit failed: %Rrc", rc));
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'i':
                pszInput = ValueUnion.psz;
                break;

            case 'o':
                pszOutput = ValueUnion.psz;
                break;

            case 'm':
            {
                if (cModules >= RT_ELEMENTS(aModules))
                    return RTMsgErrorExitFailure("Too many modules (max %u)", RT_ELEMENTS(aModules));
                aModules[cModules].pszName   = ValueUnion.psz;
                aModules[cModules].cchName   = strlen(ValueUnion.psz);

                rc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_HEX);
                if (RT_FAILURE(rc))
                    return RTGetOptPrintError(rc, &ValueUnion); /*??*/
                aModules[cModules].uAddress  = ValueUnion.u64;

                /* We need to find the .text section as that's what we'll be creating an mmap record for. */
                RTERRINFOSTATIC ErrInfo;
                RTLDRMOD        hLdrMod;
                rc = RTLdrOpenEx(aModules[cModules].pszName, RTLDR_O_FOR_DEBUG, RTLDRARCH_WHATEVER,
                                 &hLdrMod, RTErrInfoInitStatic(&ErrInfo));
                if (RT_FAILURE(rc))
                {
                    return RTMsgErrorExitFailure("RTLdrOpenEx failed on '%s': %Rrc%#RTeim",
                                                 aModules[cModules].pszName, rc, &ErrInfo.Core);
                }
                rc = RTLdrEnumSegments(hLdrMod, SegmentEnumCallback, &aModules[cModules]);
                if (rc != VINF_CALLBACK_RETURN)
                    return RTMsgErrorExitFailure("Failed to locate the .text section in '%s'!", aModules[cModules].pszName);

                aModules[cModules].cbRecord  = 0;
                aModules[cModules].offRecord = UINT64_MAX;

                cModules++;
                break;
            }

            case 'q':
                cVerbosity = 0;
                break;

            case 'v':
                cVerbosity++;
                break;

            case 'h':
                RTPrintf("usage: %s -i <perf.in> -o <perf.out> -m vmmr0.r0 <loadaddress> [-m ..] [-v]\n"
                         "\n"
                         "It is recommended to use eu-unstrip to combine the VMMR0.r0 and\n"
                         "VMMR0.debug files into a single file again.\n"
                         "\n"
                         "For the 'annotation' feature of perf to work, it is necessary to patch\n"
                         "machine__process_kernel_mmap_event() in tools/perf/utils/machine.c, adding"
                         "the following after 'map->end = map->start + ...:\n"
                         "\n"
                         "/* bird: Transfer pgoff to reloc as dso__process_kernel_symbol overwrites\n"
                         "         map->pgoff with sh_offset later.  Kind of ASSUMES sh_offset == sh_addr. */\n"
                         "if (event->mmap.pgoff && map->dso && !map->dso->rel)\n"
                         "        map->reloc = map->start - event->mmap.pgoff;\n"
                         , argv[0]);
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszInput)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No input file specified");
    if (!pszOutput)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No output file specified");
    if (RTFileExists(pszOutput))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Output file exists: %s", pszOutput);


    /*
     * Open the input file and check the header.
     */
    RTFILE hFile;
    rc = RTFileOpen(&hFile, pszInput, RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Failed to open '%s': %Rrc", pszInput, rc);

    union
    {
        LNXPERFFILEHDR  FileHdr;
        uint8_t         ab[_64K]; /* max record size */
    } u;
    rc = RTFileRead(hFile, &u.FileHdr, sizeof(u.FileHdr), NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Error reading file header: %Rrc", rc);
    if (u.FileHdr.uMagic != LNXPERFILEHDR_MAGIC)
        return RTMsgErrorExitFailure("Invalid file header magic: %.8Rhxs", &u.FileHdr.uMagic);
    if (u.FileHdr.cbHdr != sizeof(u.FileHdr))
        return RTMsgErrorExitFailure("Invalid file header size: %RU64, expected %zu", u.FileHdr.cbHdr, sizeof(u.FileHdr));
    uint64_t const offData = u.FileHdr.Data.off;
    uint64_t const cbData  = u.FileHdr.Data.cb;

    /*
     * Jump to the data portion and look for suitable kmod mmap
     * records to replace.
     *
     * We sort the modules in descreasing name length first to make sure
     * not to waste voluminous records on short replacement names.
     */
    RTSortShell(aModules, cModules, sizeof(aModules[0]), CompModuleNameLengthDesc, NULL);

    unsigned cModulesLeft = cModules ? cModules : cVerbosity > 0;
    uint64_t offRecord    = 0;
    while (offRecord + 32 < cbData && cModulesLeft > 0)
    {
        size_t cbToRead = (size_t)RT_MIN(cbData - offRecord, sizeof(u));
        rc = RTFileReadAt(hFile, offData + offRecord, &u, cbToRead, NULL);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTFileReadAt(,%llu,,%zu,) failed: %Rrc", offData + offRecord, cbToRead, rc);

        uint64_t const offEnd = offRecord + cbToRead;
        uint8_t       *pb     = u.ab;
        while (offRecord + 32 < offEnd)
        {
            PLNXPERFRECORDHEADER pRecHdr = (PLNXPERFRECORDHEADER)pb;
            uint64_t const offNext = offRecord + pRecHdr->cb;
            if (offNext > offEnd)
                break;
            if (   pRecHdr->uType == LNXPERF_RECORD_MMAP
                && (pRecHdr->fMisc & LNXPERF_RECORD_MISC_CPUMODE_MASK) == LNXPERF_RECORD_MISC_KERNEL)
            {
                PLNXPERFRECORDMMAP pMmapRec = (PLNXPERFRECORDMMAP)pRecHdr;
                if (cVerbosity > 0)
                    RTMsgInfo("MMAP: %016RX64 (%016RX64) LB %012RX64 %s\n",
                              pMmapRec->uAddress, pMmapRec->offFile, pMmapRec->cbMapping, pMmapRec->szFilename);

                bool fSkip = false;
                for (unsigned i = 0; i < cSkipPatterns && !fSkip; i++)
                    fSkip = RTStrSimplePatternMatch(apszSkipPatterns[i], pMmapRec->szFilename);

                if (!fSkip)
                {
                    /* Figure the max filename length we dare to put here. */
                    size_t cchFilename = strlen(pMmapRec->szFilename);
                    cchFilename = RT_ALIGN_Z(cchFilename + 1, 8) - 1;

                    for (unsigned i = 0; i < cModules; i++)
                        if (   aModules[i].offRecord == UINT64_MAX
                            && aModules[i].cchName <= cchFilename)
                        {
                            aModules[i].cbRecord  = pRecHdr->cb;
                            aModules[i].offRecord = offData + offRecord;
                            cModulesLeft--;
                            if (cVerbosity > 0)
                                RTMsgInfo("Will replace module %s at offset %RU64 with %s\n",
                                          pMmapRec->szFilename, offRecord, aModules[i].pszName);
                            break;
                        }
                }
            }

            /* Advance */
            pb       += pRecHdr->cb;
            offRecord = offNext;
        }
    }

    /*
     * Only proceed if we found insertion points for all specified modules.
     */
    if (cModulesLeft)
    {
        if (cModules)
        {
            RTMsgError("Unable to find suitable targets for:\n");
            for (unsigned i = 0; i < cModules; i++)
                if (aModules[i].offRecord == UINT64_MAX)
                    RTMsgError("   %s\n", aModules[i].pszName);
        }
        else
            RTMsgError("No modules given, so nothing to do.\n");
        return RTEXITCODE_FAILURE;
    }

    /*
     * Sort the modules by record offset to simplify the copying.
     */
    RTSortShell(aModules, cModules, sizeof(aModules[0]), CompModuleRecordOffset, NULL);

    RTFILE hOutFile;
    rc = RTFileOpen(&hOutFile, pszOutput, RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Failed to creating '%s' for the output: %Rrc", pszOutput, rc);

    unsigned iModule = 0;
    uint64_t offNext = aModules[0].offRecord;
    uint64_t off     = 0;
    for (;;)
    {
        Assert(off <= offNext);

        /* Read a chunk of data. Records we modify are read separately. */
        size_t cbToRead = RT_MIN(offNext - off, sizeof(u));
        if (cbToRead == 0)
            cbToRead = aModules[iModule].cbRecord;
        size_t cbActual = 0;
        rc = RTFileReadAt(hFile, off, &u, cbToRead, &cbActual);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("Error reading %zu bytes at %RU64 in '%s': %Rrc", cbToRead, off, pszInput, rc);

        /* EOF? */
        if (cbActual == 0)
            break;

        /* A record we wish to modify? */
        if (off == offNext)
        {
            if (cbActual != aModules[iModule].cbRecord)
                return RTMsgErrorExitFailure("Internal error: cbActual=%zu cbRecord=%u off=%RU64\n",
                                             cbActual, aModules[iModule].cbRecord, off);

            PLNXPERFRECORDMMAP pMmapRec = (PLNXPERFRECORDMMAP)&u.ab[0];
            strcpy(pMmapRec->szFilename, aModules[iModule].pszName);
            pMmapRec->uAddress  = aModules[iModule].uAddress;
            pMmapRec->cbMapping = aModules[iModule].cbMapping;
            pMmapRec->offFile   = aModules[iModule].offFile;
            RTMsgInfo("Done: %s\n", pMmapRec->szFilename);

            iModule++;
            if (iModule < cModules)
                offNext = aModules[iModule].offRecord;
            else
                offNext = UINT64_MAX;
        }

        /* Write out the data. */
        rc = RTFileWrite(hOutFile, &u, cbActual, NULL);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("Error writing %zu bytes at %RU64 to '%s': %Rrc", cbActual, off, pszOutput, rc);

        /* Advance.*/
        off += cbActual;
    }

    if (iModule != cModules)
        return RTMsgErrorExitFailure("Internal error: iModule=%u cModules=%u\n", iModule, cModules);

    rc = RTFileClose(hOutFile);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Error closing output file '%s': %Rrc", pszOutput, rc);

    return RTEXITCODE_SUCCESS;
}

