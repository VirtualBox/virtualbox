/* $Id: RTFileReadAllByHandleEx-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTFileReadAllByHandleEx, generic implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <iprt/file.h>
#include "internal/iprt.h"

#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/errcore.h>


RTDECL(int) RTFileReadAllByHandleEx(RTFILE File, RTFOFF off, RTFOFF cbMax, uint32_t fFlags, void **ppvFile, size_t *pcbFile)
{
    AssertReturn(!(fFlags & ~RTFILE_RDALL_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Save the current offset first.
     */
    RTFOFF offOrg;
    int rc = RTFileSeek(File, 0, RTFILE_SEEK_CURRENT, (uint64_t *)&offOrg);
    if (RT_SUCCESS(rc))
    {
        /*
         * Get the file size, adjust it and check that it might fit into memory.
         */
        RTFOFF cbFile;
        AssertCompile(sizeof(cbFile) == sizeof(uint64_t));
        rc = RTFileSeek(File, 0, RTFILE_SEEK_END, (uint64_t *)&cbFile);
        if (RT_SUCCESS(rc))
        {
            RTFOFF cbAllocFile = cbFile > off ? cbFile - off : 0;
            if (cbAllocFile <= cbMax)
            { /* likely */ }
            else if (!(fFlags & RTFILE_RDALL_F_FAIL_ON_MAX_SIZE))
                cbAllocFile = cbMax;
            else
                rc = VERR_OUT_OF_RANGE;
            if (RT_SUCCESS(rc))
            {
                size_t cbAllocMem = (size_t)cbAllocFile;
                if ((RTFOFF)cbAllocMem == cbAllocFile)
                {
                    /*
                     * Try allocate the required memory and initialize the header (hardcoded fun).
                     */
                    void *pvHdr = RTMemAlloc(cbAllocMem + 32 + (fFlags & RTFILE_RDALL_F_TRAILING_ZERO_BYTE ? 1 : 0));
                    if (pvHdr)
                    {
                        memset(pvHdr, 0xff, 32);
                        *(size_t *)pvHdr = cbAllocMem;

                        /*
                         * Seek and read.
                         */
                        rc = RTFileSeek(File, off, RTFILE_SEEK_BEGIN, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            void *pvFile = (uint8_t *)pvHdr + 32;
                            rc = RTFileRead(File, pvFile, cbAllocMem, NULL);
                            if (RT_SUCCESS(rc))
                            {
                                if (fFlags & RTFILE_RDALL_F_TRAILING_ZERO_BYTE)
                                    ((uint8_t *)pvFile)[cbAllocFile] = '\0';

                                /*
                                 * Success - fill in the return values.
                                 */
                                *ppvFile = pvFile;
                                *pcbFile = cbAllocMem;
                            }
                        }

                        if (RT_FAILURE(rc))
                            RTMemFree(pvHdr);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }
                else
                    rc = VERR_TOO_MUCH_DATA;
            }
        }
        /* restore the position. */
        RTFileSeek(File, offOrg, RTFILE_SEEK_BEGIN, NULL);
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTFileReadAllByHandleEx);

