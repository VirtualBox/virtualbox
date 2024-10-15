/* $Id: ntFlushVirtualMemory.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Memory mapped files testcase - NT.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include <iprt/nt/nt.h>

#include <iprt/alloca.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/err.h>
#include <iprt/x86.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Create page signature. */
#define MAKE_PAGE_SIGNATURE(a_iPage) ((a_iPage) | UINT32_C(0x42000000) )

/** Number history entries on the page. */
#define NUM_ROUND_HISTORY 16


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** How chatty we should be. */
static uint32_t g_cVerbosity = 0;


/**
 * Checks if the on-disk file matches our expectations.
 *
 * @returns IPRT status code, fully bitched.
 * @param   pszFilename         The name of the file.
 * @param   pu32BufChk          Buffer to read the file into
 * @param   pu32BufOrg          Expected file content.
 * @param   cbBuf               The buffer size.
 * @param   iRound              The update round.
 */
static int CheckFile(const char *pszFilename, uint32_t *pu32BufChk, uint32_t const *pu32BufOrg, size_t cbBuf, uint32_t iRound)
{
    /*
     * Open and read the file into memory.
     */
    HANDLE hFile;
    int rc = RTNtPathOpen(pszFilename,
                          GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NO_INTERMEDIATE_BUFFERING,
                          OBJ_CASE_INSENSITIVE,
                          &hFile,
                          NULL);
    if (RT_SUCCESS(rc))
    {
        IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        NTSTATUS rcNt = NtReadFile(hFile, NULL /*hEvent*/, NULL /*pfnApc*/,  NULL /*pvApcCtx*/,
                                   &Ios, pu32BufChk, (ULONG)cbBuf, NULL /*poffFile*/, NULL /*pvKey*/);
        if (NT_SUCCESS(rcNt) || Ios.Information != cbBuf)
        {
            /*
             * See if the content of the file matches our expectations.
             */
            if (memcmp(pu32BufChk, pu32BufOrg, cbBuf) == 0)
            { /* matches - likely */ }
            else
            {
                RTMsgError("Round %u: Buffer mismatch!\n", iRound);

                /* Try figure where the differences are. */
                size_t const cPages        = cbBuf / X86_PAGE_SIZE;
                size_t const cItemsPerPage = X86_PAGE_SIZE / sizeof(pu32BufOrg[0]);
                for (uint32_t iPage = 0; iPage < cPages; iPage++)
                    for (uint32_t iItem = 0; iItem < cItemsPerPage; iItem++)
                    {
                        uint32_t uValue    = pu32BufChk[iPage * cItemsPerPage + iItem];
                        uint32_t uExpected = pu32BufOrg[iPage * cItemsPerPage + iItem];
                        if (uValue != uExpected)
                            RTMsgError("Round %u: page #%u, index #%u: %#x, expected %#x\n",
                                       iRound, iPage, iItem, uValue, uExpected);
                    }

                rc = VERR_MISMATCH;
            }
        }
        else if (NT_SUCCESS(rcNt))
        {
            RTMsgError("Round %u: NtReadFile return %zu bytes intead of %zu!\n", iRound, Ios.Information, cbBuf);
            rc = VERR_READ_ERROR;
        }
        else
        {
            RTMsgError("Round %u: NtReadFile(%#x) failed: %#x (%#x)\n", iRound, cbBuf, rcNt, Ios.Status);
            rc = RTErrConvertFromNtStatus(rcNt);
        }

        /*
         * Close the file and return.
         */
        rcNt = NtClose(hFile);
        if (!NT_SUCCESS(rcNt))
        {
            RTMsgError("Round %u: NtCloseFile() failed: %#x\n", iRound, rcNt);
            rc = RTErrConvertFromNtStatus(rcNt);
        }
    }
    else
        RTMsgError("Round %u: RTNtPathOpen() failed: %Rrc\n", iRound, rc);
    return rc;
}


/**
 * Manually checks whether the buffer matches up to our expectations.
 *
 * @returns IPRT status code, fully bitched.
 * @param   pu32Buf             The buffer/mapping to check.
 * @param   cbBuf               The buffer size.
 * @param   iRound              The update round.
 * @param   cFlushesLeft        Number of flushes left in the round.
 */
static int CheckBuffer(uint32_t const *pu32Buf, size_t cbBuf, uint32_t iRound, uint32_t cFlushesLeft)
{
    size_t const cPages        = cbBuf / X86_PAGE_SIZE;
    size_t const cItemsPerPage = X86_PAGE_SIZE / sizeof(pu32Buf[0]);
    size_t const offPage       = iRound & (NUM_ROUND_HISTORY - 1);
    uint32_t const uValue      = iRound | (cFlushesLeft << 20);
//RTPrintf("debug: CheckBuffer: %p %u/%u\n", pu32Buf, iRound, cFlushesLeft);

    for (uint32_t iPage = 0; iPage < cPages; iPage++)
    {
        uint32_t uActual = pu32Buf[iPage * cItemsPerPage + offPage];
        if (uActual != uValue)
        {
            RTMsgError("Round %u/%u: page #%u: last entry is corrupted: %#x, expected %#x\n",
                       iRound, cFlushesLeft, iPage, uActual, uValue);
            return VERR_MISMATCH;
        }

        uActual = pu32Buf[iPage * cItemsPerPage + cItemsPerPage - 1];
        if (uActual != MAKE_PAGE_SIGNATURE(iPage))
        {
            RTMsgError("Round %u/%u: page #%u magic corrupted: %#x, expected %#x\n",
                       iRound, cFlushesLeft, iPage, uActual, MAKE_PAGE_SIGNATURE(iPage));
            return VERR_INVALID_MAGIC;
        }
    }

    /*
     * Check previous rounds.
     */
    for (uint32_t cRoundsAgo = 1; cRoundsAgo < NUM_ROUND_HISTORY - 1 && cRoundsAgo <= iRound; cRoundsAgo++)
    {
        uint32_t        iOldRound  = iRound - cRoundsAgo;
        size_t const    offOldPage = iOldRound & (NUM_ROUND_HISTORY - 1);
        for (uint32_t iPage = 0; iPage < cPages; iPage++)
        {
            uint32_t    uActual    = pu32Buf[iPage * cItemsPerPage + offOldPage];
            if (uActual != iOldRound)
            {
                RTMsgError("Round %u/%u: page #%u: entry from %u rounds ago is corrupted: %#x, expected %#x\n",
                           iRound, cFlushesLeft, iPage, cRoundsAgo, uActual, uValue);
                return VERR_MISMATCH;
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Updates the buffer.
 *
 * @param   pu32Buf             The buffer/mapping to update.
 * @param   cbBuf               The buffer size.
 * @param   iRound              The update round.
 * @param   cFlushesLeft        Number of flushes left in this round.
 */
static void UpdateBuffer(uint32_t *pu32Buf, size_t cbBuf, uint32_t iRound, uint32_t cFlushesLeft)
{
    size_t const cPages        = cbBuf / X86_PAGE_SIZE;
    size_t const cItemsPerPage = X86_PAGE_SIZE / sizeof(pu32Buf[0]);
    size_t const offPage       = iRound & (NUM_ROUND_HISTORY - 1);
    uint32_t const uValue      = iRound | (cFlushesLeft << 20);
//RTPrintf("debug: UpdateBuffer: %p %u/%u\n", pu32Buf, iRound, cFlushesLeft);

    for (uint32_t iPage = 0; iPage < cPages; iPage++)
        pu32Buf[iPage * cItemsPerPage + offPage] = uValue;
}



/**
 * Modifies the file via memory mapping.
 *
 * @returns IPRT status code, fully bitched.
 * @param   pszFilename         The file we're using as a test bed.
 * @param   pu32BufOrg          The sane copy of the file that gets updated in
 *                              parallel.
 * @param   cbBuf               The size of the file and bufer.
 * @param   iRound              The current round number.
 * @param   fCheckFirst         Whether to read from the mapping the mapping
 *                              before dirtying it the first time around.
 * @param   fCheckAfterFlush    Whether to read from the mapping the mapping
 *                              before dirtying it after a flush.
 * @param   cFlushes            How many times we modify the mapping and flush
 *                              it before one final modification and unmapping.
 * @param   fLargePages         Whether to use large pages.
 */
static int MakeModifications(const char *pszFilename, uint32_t *pu32BufOrg, size_t cbBuf, uint32_t iRound,
                             bool fCheckFirst, bool fCheckAfterFlush, uint32_t cFlushes, bool fLargePages)
{

    HANDLE hFile = RTNT_INVALID_HANDLE_VALUE;
    int rc = RTNtPathOpen(pszFilename,
                          GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NO_INTERMEDIATE_BUFFERING,
                          OBJ_CASE_INSENSITIVE,
                          &hFile,
                          NULL);
    if (RT_SUCCESS(rc))
    {
        HANDLE hSection;
        NTSTATUS rcNt = NtCreateSection(&hSection,
                                        SECTION_ALL_ACCESS,
                                        NULL, /*pObjAttrs*/
                                        NULL, /*pcbMax*/
                                        PAGE_READWRITE,
                                        SEC_COMMIT,
                                        hFile);
        NtClose(hFile);
        if (NT_SUCCESS(rcNt))
        {
            PVOID  pvMapping = NULL;
            SIZE_T cbMapping = 0;
            rcNt = NtMapViewOfSection(hSection, NtCurrentProcess(),
                                      &pvMapping,
                                      0, /* ZeroBits */
                                      0, /* CommitSize */
                                      NULL, /* SectionOffset */
                                      &cbMapping,
                                      ViewUnmap,
                                      fLargePages ? MEM_LARGE_PAGES : 0,
                                      PAGE_READWRITE);
            if (NT_SUCCESS(rcNt))
            {
                /*
                 * Make the modifications.
                 */
                if (g_cVerbosity >= 2)
                    RTPrintf("debug: pvMapping=%p LB %#x\n", pvMapping, cbBuf);

                for (uint32_t iInner = 0;; iInner++)
                {
                    if (iInner ? fCheckAfterFlush : fCheckFirst)
                    {
                        if (iInner == 0)
                            rc = CheckBuffer((uint32_t *)pvMapping, cbBuf, iRound - 1, 0);
                        else
                            rc = CheckBuffer((uint32_t *)pvMapping, cbBuf, iRound, cFlushes - iInner + 1);
                        if (RT_FAILURE(rc))
                        {
                            RTMsgError("Round %u/%u: NtUnmapViewOfSection failed: %#x\n", iRound, rcNt);
                            break;
                        }
                    }

                    UpdateBuffer((uint32_t *)pvMapping, cbBuf, iRound, cFlushes - iInner);
                    UpdateBuffer(pu32BufOrg, cbBuf, iRound, cFlushes - iInner);

                    if (iInner >= cFlushes)
                        break;

                    IO_STATUS_BLOCK Ios        = RTNT_IO_STATUS_BLOCK_INITIALIZER;
                    SIZE_T          cbBuf2     = cbBuf;
                    PVOID           pvMapping2 = pvMapping;
                    rcNt = NtFlushVirtualMemory(NtCurrentProcess(), &pvMapping2, &cbBuf2, &Ios);
                    if (!NT_SUCCESS(rcNt))
                    {
                        RTMsgError("Round %u: NtFlushVirtualMemory failed: %#x\n", iRound, rcNt);
                        rc = RTErrConvertFromNtStatus(rcNt);
                        break;
                    }
                }

                /*
                 * Cleanup.
                 */
                rcNt = NtUnmapViewOfSection(NtCurrentProcess(), pvMapping);
                if (!NT_SUCCESS(rcNt))
                {
                    RTMsgError("Round %u: NtUnmapViewOfSection failed: %#x\n", iRound, rcNt);
                    rc = RTErrConvertFromNtStatus(rcNt);
                }
            }
            else
            {
                RTMsgError("Round %u: NtMapViewOfSection failed: %#x\n", iRound, rcNt);
                rc = RTErrConvertFromNtStatus(rcNt);
            }

            rcNt = NtClose(hSection);
            if (!NT_SUCCESS(rcNt))
            {
                RTMsgError("Round %u: NtClose(hSection) failed: %#x\n", iRound, rcNt);
                rc = RTErrConvertFromNtStatus(rcNt);
            }
        }
        else
        {
            RTMsgError("Round %u: NtCreateSection failed: %#x\n", iRound, rcNt);
            rc = RTErrConvertFromNtStatus(rcNt);
        }
    }
    else
        RTMsgError("Round %u: Error opening file '%s' for memory mapping: %Rrc\n", iRound, pszFilename, rc);
    return rc;
}


int main(int argc, char **argv)
{
    /*
     * Init IPRT.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse arguments.
     */
    const char *pszFilename = NULL;
    uint32_t    cRounds     = 4096;
    uint32_t    cPages      = 128;
    bool        fLargePages = false;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--rounds",       'r',  RTGETOPT_REQ_UINT32 },
        { "--pages",        'p',  RTGETOPT_REQ_UINT32 },
        { "--filename",     'f',  RTGETOPT_REQ_STRING },
        { "--large-pages",  'l',  RTGETOPT_REQ_NOTHING },
        { "--quiet",        'q',  RTGETOPT_REQ_NOTHING },
        { "--verbose",      'v',  RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE State;
    RTGetOptInit(&State, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    RTGETOPTUNION ValueUnion;
    int chOpt;
    while ((chOpt = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (chOpt)
        {
            case 'r': cRounds = ValueUnion.u32; break;
            case 'p': cPages = ValueUnion.u32; break;
            case 'f': pszFilename = ValueUnion.psz; break;
            case 'l': fLargePages = true; break;
            case 'q': g_cVerbosity = 0; break;
            case 'v': g_cVerbosity += 1; break;
            case 'h':
                RTPrintf("usage: ntFlushVirtualMemory [-c <times>] [-p <pages>] [-l|--large-pages] [-f <filename>]\n"
                         "\n"
                         "Aims at testing memory mapped files on NT w/ NtFlushVirtualMemory / FlushViewOfFile.\n");
                return 0;

            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }

    /*
     * Allocate buffers and initialize the original with page numbers.
     *
     * We keep a original copy that gets updated in parallel to the memory
     * mapping, allowing for simple file initialization and memcpy checking.
     *
     * The second buffer is for reading the file from disk and check.
     */
    size_t const cbBuf = cPages * X86_PAGE_SIZE;
    size_t const cItemsPerPage = X86_PAGE_SIZE / sizeof(uint32_t);
    uint32_t *pu32BufOrg = (uint32_t *)RTMemPageAllocZ(cbBuf);
    uint32_t *pu32BufChk = (uint32_t *)RTMemPageAllocZ(cbBuf);
    if (pu32BufOrg == NULL || pu32BufChk == NULL)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate two %zu sized buffers!\n", cbBuf);

    for (uint32_t iPage = 0; iPage < cPages; iPage++)
        pu32BufOrg[iPage * cItemsPerPage + cItemsPerPage - 1] = MAKE_PAGE_SIGNATURE(iPage);

    rc = CheckBuffer(pu32BufOrg, cbBuf, 0, 0);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Internal error: CheckBuffer failed on virgin buffer: %Rrc\n", rc);

    /*
     * Open the file and write out the orignal one.
     */
    RTFILE hFile;
    if (!pszFilename)
    {
        char *pszBuf = (char *)alloca(RTPATH_MAX);
        rc = RTFileOpenTemp(&hFile, pszBuf, RTPATH_MAX, RTFILE_O_READWRITE | RTFILE_O_CREATE | RTFILE_O_DENY_NONE);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create temporary file: %Rrc\n", rc);
        pszFilename = pszBuf;
    }
    else
    {
        rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_READWRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to open '%s': %Rrc\n", rc);
    }

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

    rc = RTFileWrite(hFile, pu32BufOrg, cbBuf, NULL);
    if (RT_SUCCESS(rc))
    {
        RTFileClose(hFile);

        /*
         * Do the rounds.  We count from 1 here to make verifying the previous round simpler.
         */
        for (uint32_t iRound = 1; iRound <= cRounds; iRound++)
        {
            rc = MakeModifications(pszFilename, pu32BufOrg, cbBuf, iRound,
                                   ((iRound >> 5) & 1) == 1, ((iRound >> 5) & 3) == 3, (iRound >> 3) & 31, fLargePages);
            if (RT_SUCCESS(rc))
            {
                rc = CheckBuffer(pu32BufOrg, cbBuf, iRound, 0);
                if (RT_SUCCESS(rc))
                {
                    rc = CheckFile(pszFilename, pu32BufChk, pu32BufOrg, cbBuf, iRound);
                    if (RT_SUCCESS(rc))
                        continue;
                }
            }
            break;
        }
    }
    else
    {
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Error writing initial %zu bytes to '%s': %Rrc\n", cbBuf, rc);
        RTFileClose(hFile);
    }
    RTFileDelete(pszFilename);
    return rcExit;
}

