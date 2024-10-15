/* $Id: RTFileCopyEx-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTFileCopyEx, generic implementation.
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

#include <iprt/assert.h>
#include <iprt/errcore.h>


RTDECL(int) RTFileCopyEx(const char *pszSrc, const char *pszDst, uint32_t fFlags, PFNRTPROGRESS pfnProgress, void *pvUser)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszSrc, VERR_INVALID_POINTER);
    AssertMsgReturn(*pszSrc, ("pszSrc=%p\n", pszSrc), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszDst, VERR_INVALID_POINTER);
    AssertMsgReturn(*pszDst, ("pszDst=%p\n", pszDst), VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pfnProgress, VERR_INVALID_POINTER);
    AssertMsgReturn(!(fFlags & ~RTFILECOPY_FLAGS_MASK), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Open the files.
     */
    RTFILE FileSrc;
    int rc = RTFileOpen(&FileSrc, pszSrc,
                        RTFILE_O_READ | RTFILE_O_OPEN
                        | (fFlags & RTFILECOPY_FLAGS_NO_SRC_DENY_WRITE ? RTFILE_O_DENY_NONE : RTFILE_O_DENY_WRITE));
    if (RT_SUCCESS(rc))
    {
        RTFILE FileDst;
        rc = RTFileOpen(&FileDst, pszDst,
                        RTFILE_O_WRITE | RTFILE_O_CREATE
                        | (fFlags & RTFILECOPY_FLAGS_NO_DST_DENY_WRITE ? RTFILE_O_DENY_NONE : RTFILE_O_DENY_WRITE));
        if (RT_SUCCESS(rc))
        {
            /*
             * Call the ByHandles version and let it do the job.
             */
            rc = RTFileCopyByHandlesEx(FileSrc, FileDst, pfnProgress, pvUser);

            /*
             * Close the files regardless of the result.
             * Don't bother cleaning up or anything like that.
             */
            int rc2 = RTFileClose(FileDst);
            AssertRC(rc2);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }

        int rc2 = RTFileClose(FileSrc);
        AssertRC(rc2);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }
    return rc;
}

