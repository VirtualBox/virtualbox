/* $Id: fileio.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File I/O.
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
#include "internal/iprt.h"
#include <iprt/file.h>

#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/alloca.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include "internal/file.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Set of forced set open flags for files opened read-only. */
static unsigned g_fOpenReadSet = 0;

/** Set of forced cleared open flags for files opened read-only. */
static unsigned g_fOpenReadMask = 0;

/** Set of forced set open flags for files opened write-only. */
static unsigned g_fOpenWriteSet = 0;

/** Set of forced cleared open flags for files opened write-only. */
static unsigned g_fOpenWriteMask = 0;

/** Set of forced set open flags for files opened read-write. */
static unsigned g_fOpenReadWriteSet = 0;

/** Set of forced cleared open flags for files opened read-write. */
static unsigned g_fOpenReadWriteMask = 0;


/**
 * Force the use of open flags for all files opened after the setting is
 * changed. The caller is responsible for not causing races with RTFileOpen().
 *
 * @returns iprt status code.
 * @param   fOpenForAccess  Access mode to which the set/mask settings apply.
 * @param   fSet            Open flags to be forced set.
 * @param   fMask           Open flags to be masked out.
 */
RTR3DECL(int)  RTFileSetForceFlags(unsigned fOpenForAccess, unsigned fSet, unsigned fMask)
{
    /*
     * For now allow only RTFILE_O_WRITE_THROUGH. The other flags either
     * make no sense in this context or are not useful to apply to all files.
     */
    if ((fSet | fMask) & ~RTFILE_O_WRITE_THROUGH)
        return VERR_INVALID_PARAMETER;
    switch (fOpenForAccess)
    {
        case RTFILE_O_READ:
            g_fOpenReadSet = fSet;
            g_fOpenReadMask = fMask;
            break;
        case RTFILE_O_WRITE:
            g_fOpenWriteSet = fSet;
            g_fOpenWriteMask = fMask;
            break;
        case RTFILE_O_READWRITE:
            g_fOpenReadWriteSet = fSet;
            g_fOpenReadWriteMask = fMask;
            break;
        default:
            AssertMsgFailed(("Invalid access mode %d\n", fOpenForAccess));
            return VERR_INVALID_PARAMETER;
    }
    return VINF_SUCCESS;
}


/**
 * Adjusts and validates the flags.
 *
 * The adjustments are made according to the wishes specified using the RTFileSetForceFlags API.
 *
 * @returns IPRT status code.
 * @param   pfOpen      Pointer to the user specified flags on input.
 *                      Updated on successful return.
 * @internal
 */
int rtFileRecalcAndValidateFlags(uint64_t *pfOpen)
{
    /*
     * Recalc.
     */
    uint32_t fOpen = *pfOpen;
    switch (fOpen & RTFILE_O_ACCESS_MASK)
    {
        case RTFILE_O_READ:
            fOpen |= g_fOpenReadSet;
            fOpen &= ~g_fOpenReadMask;
            break;
        case RTFILE_O_WRITE:
            fOpen |= g_fOpenWriteSet;
            fOpen &= ~g_fOpenWriteMask;
            break;
        case RTFILE_O_READWRITE:
            fOpen |= g_fOpenReadWriteSet;
            fOpen &= ~g_fOpenReadWriteMask;
            break;
#ifdef RT_OS_WINDOWS
        case RTFILE_O_ATTR_ONLY:
            if (fOpen & RTFILE_O_ACCESS_ATTR_MASK)
                break;
#endif
        default:
            AssertMsgFailed(("Invalid access mode value, fOpen=%#llx\n", fOpen));
            return VERR_INVALID_PARAMETER;
    }

    /*
     * Validate                                                                                                                                       .
     */
#ifdef RT_OS_WINDOWS
    AssertMsgReturn((fOpen & RTFILE_O_ACCESS_MASK) || (fOpen & RTFILE_O_ACCESS_ATTR_MASK),
                    ("Missing RTFILE_O_READ/WRITE/ACCESS_ATTR: fOpen=%#llx\n", fOpen), VERR_INVALID_PARAMETER);
#else
    AssertMsgReturn(fOpen & RTFILE_O_ACCESS_MASK, ("Missing RTFILE_O_READ/WRITE: fOpen=%#llx\n", fOpen), VERR_INVALID_PARAMETER);
#endif
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    AssertMsgReturn(!(fOpen & (~(uint64_t)RTFILE_O_VALID_MASK | RTFILE_O_NON_BLOCK)), ("%#llx\n", fOpen), VERR_INVALID_PARAMETER);
#else
    AssertMsgReturn(!(fOpen & ~(uint64_t)RTFILE_O_VALID_MASK), ("%#llx\n", fOpen), VERR_INVALID_PARAMETER);
#endif
    AssertMsgReturn((fOpen & (RTFILE_O_TRUNCATE | RTFILE_O_WRITE)) != RTFILE_O_TRUNCATE, ("%#llx\n", fOpen), VERR_INVALID_PARAMETER);

    switch (fOpen & RTFILE_O_ACTION_MASK)
    {
        case 0: /* temporarily */
            AssertMsgFailed(("Missing RTFILE_O_OPEN/CREATE*! (continuable assertion)\n"));
            fOpen |= RTFILE_O_OPEN;
            break;
        case RTFILE_O_OPEN:
            AssertMsgReturn(!(RTFILE_O_NOT_CONTENT_INDEXED & fOpen), ("%#llx\n", fOpen), VERR_INVALID_PARAMETER);
        case RTFILE_O_OPEN_CREATE:
        case RTFILE_O_CREATE:
        case RTFILE_O_CREATE_REPLACE:
            break;
        default:
            AssertMsgFailed(("Invalid action value: fOpen=%#llx\n", fOpen));
            return VERR_INVALID_PARAMETER;
    }

    switch (fOpen & RTFILE_O_DENY_MASK)
    {
        case 0: /* temporarily */
            AssertMsgFailed(("Missing RTFILE_O_DENY_*! (continuable assertion)\n"));
            fOpen |= RTFILE_O_DENY_NONE;
            break;
        case RTFILE_O_DENY_NONE:
        case RTFILE_O_DENY_READ:
        case RTFILE_O_DENY_WRITE:
        case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_READ:
        case RTFILE_O_DENY_NOT_DELETE:
        case RTFILE_O_DENY_NOT_DELETE | RTFILE_O_DENY_READ:
        case RTFILE_O_DENY_NOT_DELETE | RTFILE_O_DENY_WRITE:
        case RTFILE_O_DENY_NOT_DELETE | RTFILE_O_DENY_WRITE | RTFILE_O_DENY_READ:
            break;
        default:
            AssertMsgFailed(("Invalid deny value: fOpen=%#llx\n", fOpen));
            return VERR_INVALID_PARAMETER;
    }

    /* done */
    *pfOpen = fOpen;
    return VINF_SUCCESS;
}


RTR3DECL(uint64_t)  RTFileTell(RTFILE File)
{
    /*
     * Call the seek api to query the stuff.
     */
    uint64_t off = 0;
    int rc = RTFileSeek(File, 0, RTFILE_SEEK_CURRENT, &off);
    if (RT_SUCCESS(rc))
        return off;
    AssertMsgFailed(("RTFileSeek(%d) -> %d\n", File, rc));
    return ~0ULL;
}


RTR3DECL(RTFOFF) RTFileGetMaxSize(RTFILE File)
{
    RTFOFF cbMax;
    int rc = RTFileQueryMaxSizeEx(File, &cbMax);
    return RT_SUCCESS(rc) ? cbMax : -1;
}


RTDECL(int) RTFileCopyByHandles(RTFILE FileSrc, RTFILE FileDst)
{
    return RTFileCopyByHandlesEx(FileSrc, FileDst, NULL, NULL);
}


RTDECL(int) RTFileCompare(const char *pszFile1, const char *pszFile2)
{
    return RTFileCompareEx(pszFile1, pszFile2, 0 /*fFlags*/, NULL, NULL);
}


RTDECL(int) RTFileCompareByHandles(RTFILE hFile1, RTFILE hFile2)
{
    return RTFileCompareByHandlesEx(hFile1, hFile2, 0 /*fFlags*/, NULL, NULL);
}


RTDECL(int) RTFileCompareEx(const char *pszFile1, const char *pszFile2, uint32_t fFlags, PFNRTPROGRESS pfnProgress, void *pvUser)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszFile1, VERR_INVALID_POINTER);
    AssertReturn(*pszFile1, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFile2, VERR_INVALID_POINTER);
    AssertReturn(*pszFile2, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pfnProgress, VERR_INVALID_POINTER);
    AssertMsgReturn(!(fFlags & ~RTFILECOMP_FLAGS_MASK), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Open the files.
     */
    RTFILE hFile1;
    int rc = RTFileOpen(&hFile1, pszFile1,
                        RTFILE_O_READ | RTFILE_O_OPEN
                        | (fFlags & RTFILECOMP_FLAGS_NO_DENY_WRITE_FILE1 ? RTFILE_O_DENY_NONE : RTFILE_O_DENY_WRITE));
    if (RT_SUCCESS(rc))
    {
        RTFILE hFile2;
        rc = RTFileOpen(&hFile2, pszFile2,
                        RTFILE_O_READ | RTFILE_O_OPEN
                        | (fFlags & RTFILECOMP_FLAGS_NO_DENY_WRITE_FILE2 ? RTFILE_O_DENY_NONE : RTFILE_O_DENY_WRITE));
        if (RT_SUCCESS(rc))
        {
            /*
             * Call the ByHandles version and let it do the job.
             */
            rc = RTFileCompareByHandlesEx(hFile1, hFile2, fFlags,  pfnProgress, pvUser);

            /* Clean up */
            int rc2 = RTFileClose(hFile2);
            AssertRC(rc2);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }

        int rc2 = RTFileClose(hFile1);
        AssertRC(rc2);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }
    return rc;
}


RTDECL(int) RTFileCompareByHandlesEx(RTFILE hFile1, RTFILE hFile2, uint32_t fFlags, PFNRTPROGRESS pfnProgress, void *pvUser)
{
    /*
     * Validate input.
     */
    AssertReturn(RTFileIsValid(hFile1), VERR_INVALID_HANDLE);
    AssertReturn(RTFileIsValid(hFile1), VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pfnProgress, VERR_INVALID_POINTER);
    AssertMsgReturn(!(fFlags & ~RTFILECOMP_FLAGS_MASK), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Compare the file sizes first.
     */
    uint64_t cbFile1;
    int rc = RTFileQuerySize(hFile1, &cbFile1);
    if (RT_FAILURE(rc))
        return rc;

    uint64_t cbFile2;
    rc = RTFileQuerySize(hFile1, &cbFile2);
    if (RT_FAILURE(rc))
        return rc;

    if (cbFile1 != cbFile2)
        return VERR_NOT_EQUAL;


    /*
     * Allocate buffer.
     */
    size_t      cbBuf;
    uint8_t    *pbBuf1Free = NULL;
    uint8_t    *pbBuf1;
    uint8_t    *pbBuf2Free = NULL;
    uint8_t    *pbBuf2;
    if (cbFile1 < _512K)
    {
        cbBuf  = 8*_1K;
        pbBuf1 = (uint8_t *)alloca(cbBuf);
        pbBuf2 = (uint8_t *)alloca(cbBuf);
    }
    else
    {
        cbBuf = _128K;
        pbBuf1 = pbBuf1Free = (uint8_t *)RTMemTmpAlloc(cbBuf);
        pbBuf2 = pbBuf2Free = (uint8_t *)RTMemTmpAlloc(cbBuf);
    }
    if (pbBuf1 && pbBuf2)
    {
        /*
         * Seek to the start of each file
         * and set the size of the destination file.
         */
        rc = RTFileSeek(hFile1, 0, RTFILE_SEEK_BEGIN, NULL);
        if (RT_SUCCESS(rc))
        {
            rc = RTFileSeek(hFile2, 0, RTFILE_SEEK_BEGIN, NULL);
            if (RT_SUCCESS(rc) && pfnProgress)
                rc = pfnProgress(0, pvUser);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Compare loop.
                 */
                unsigned    uPercentage    = 0;
                RTFOFF      off            = 0;
                RTFOFF      cbPercent      = cbFile1 / 100;
                RTFOFF      offNextPercent = cbPercent;
                while (off < (RTFOFF)cbFile1)
                {
                    /* read the blocks */
                    RTFOFF cbLeft = cbFile1 - off;
                    size_t cbBlock = cbLeft >= (RTFOFF)cbBuf ? cbBuf : (size_t)cbLeft;
                    rc = RTFileRead(hFile1, pbBuf1, cbBlock, NULL);
                    if (RT_FAILURE(rc))
                        break;
                    rc = RTFileRead(hFile2, pbBuf2, cbBlock, NULL);
                    if (RT_FAILURE(rc))
                        break;

                    /* compare */
                    if (memcmp(pbBuf1, pbBuf2, cbBlock))
                    {
                        rc = VERR_NOT_EQUAL;
                        break;
                    }

                    /* advance */
                    off += cbBlock;
                    if (pfnProgress && offNextPercent < off)
                    {
                        while (offNextPercent < off)
                        {
                            uPercentage++;
                            offNextPercent += cbPercent;
                        }
                        rc = pfnProgress(uPercentage, pvUser);
                        if (RT_FAILURE(rc))
                            break;
                    }
                }

#if 0
                /*
                 * Compare OS specific data (EAs and stuff).
                 */
                if (RT_SUCCESS(rc))
                    rc = rtFileCompareOSStuff(hFile1, hFile2);
#endif

                /* 100% */
                if (pfnProgress && uPercentage < 100 && RT_SUCCESS(rc))
                    rc = pfnProgress(100, pvUser);
            }
        }
    }
    else
        rc = VERR_NO_MEMORY;
    RTMemTmpFree(pbBuf2Free);
    RTMemTmpFree(pbBuf1Free);

    return rc;
}

