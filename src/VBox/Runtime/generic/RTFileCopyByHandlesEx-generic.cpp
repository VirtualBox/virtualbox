/* $Id: RTFileCopyByHandlesEx-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTFileCopyByHandlesEx, generic implementation.
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

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/errcore.h>


RTDECL(int) RTFileCopyByHandlesEx(RTFILE hFileSrc, RTFILE hFileDst, PFNRTPROGRESS pfnProgress, void *pvUser)
{
    /*
     * Validate input.
     */
    AssertMsgReturn(RTFileIsValid(hFileSrc), ("hFileSrc=%RTfile\n", hFileSrc), VERR_INVALID_PARAMETER);
    AssertMsgReturn(RTFileIsValid(hFileDst), ("hFileDst=%RTfile\n", hFileDst), VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pfnProgress, VERR_INVALID_POINTER);

    /*
     * Save file offset.
     */
    uint64_t offSrcSaved;
    int rc = RTFileSeek(hFileSrc, 0, RTFILE_SEEK_CURRENT, &offSrcSaved);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Get the file size and figure out how much we'll copy at a time.
     */
    uint64_t cbSrc;
    rc = RTFileQuerySize(hFileSrc, &cbSrc);
    if (RT_FAILURE(rc))
        return rc;

    uint64_t cbChunk = cbSrc;
    if (pfnProgress && cbSrc > _1M)
    {
        cbChunk /= 100;
        if (cbChunk > _64M)
            cbChunk = RT_ALIGN_64(cbChunk, _2M);
        else
            cbChunk = RT_ALIGN_64(cbChunk, _128K);
    }

    /*
     * Prepare buffers.
     */
    RTFILECOPYPARTBUFSTATE BufState;
    rc = RTFileCopyPartPrep(&BufState, cbChunk);
    if (RT_SUCCESS(rc))
    {
        /*
         * Prepare the destination file.
         */
        uint64_t cbDst;
        rc = RTFileQuerySize(hFileDst, &cbDst);
        if (RT_SUCCESS(rc) && cbDst > cbSrc)
            rc = RTFileSetSize(hFileDst, cbSrc);
        if (RT_SUCCESS(rc) && cbDst < cbSrc)
        {
            rc = RTFileSetAllocationSize(hFileDst, cbSrc, RTFILE_ALLOC_SIZE_F_DEFAULT);
            if (rc == VERR_NOT_SUPPORTED)
                rc = RTFileSetSize(hFileDst, cbSrc);
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Copy loop that works till we reach EOF.
             */
            RTFOFF      off            = 0;
            RTFOFF      cbPercent      = cbSrc / 100;
            RTFOFF      offNextPercent = pfnProgress ? cbPercent : RTFOFF_MAX;
            unsigned    uPercentage    = pfnProgress ? 0         : 100;
            for (;;)
            {
                /*
                 * Copy a block.
                 */
                uint64_t cbCopied = 0;
                rc = RTFileCopyPartEx(hFileSrc, off, hFileDst, off, cbChunk, 0 /*fFlags*/, &BufState, &cbCopied);
                if (RT_FAILURE(rc))
                    break;
                if (cbCopied == 0)
                {
                    /*
                     * We reached the EOF.  Complete the copy operation.
                     */
                    rc = RTFileSetSize(hFileDst, off);
                    if (RT_SUCCESS(rc))
                        rc = RTFileCopyAttributes(hFileSrc, hFileDst, 0);
                    break;
                }

                /*
                 * Advance and work the progress callback.
                 */
                off += cbCopied;
                if (   off >= offNextPercent
                    && pfnProgress
                    && uPercentage < 99)
                {
                    do
                    {
                        uPercentage++;
                        offNextPercent += cbPercent;
                    } while (   offNextPercent <= off
                             && uPercentage < 99);
                    rc = pfnProgress(uPercentage, pvUser);
                    if (RT_FAILURE(rc))
                        break;
                }
            }
        }

        RTFileCopyPartCleanup(&BufState);

        /*
         * 100%.
         */
        if (   pfnProgress
            && RT_SUCCESS(rc))
            rc = pfnProgress(100, pvUser);
    }

    /*
     * Restore source position.
     */
    RTFileSeek(hFileSrc, offSrcSaved, RTFILE_SEEK_BEGIN, NULL);
    return rc;
}

