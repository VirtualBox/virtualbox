/* $Id: RTFileCopyPartEx-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTFileCopyPartEx, generic implementation.
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
#include <iprt/file.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>


#ifndef IPRT_FALLBACK_VERSION
RTDECL(int) RTFileCopyPartPrep(PRTFILECOPYPARTBUFSTATE pBufState, uint64_t cbToCopy)
#else
static int rtFileCopyPartPrepFallback(PRTFILECOPYPARTBUFSTATE pBufState, uint64_t cbToCopy)
#endif
{
    /*
     * Allocate a fitting buffer.
     *
     * We're a little careful with using RTMemPageAlloc here as it does require
     * kernel calls on some hosts.  If we could be sure the heap was able to
     * handle it most of the time, we would be a lot more aggressive.
     */
    size_t cbBuf = 0;
    void  *pvBuf = NULL;
    if (   cbToCopy >= _512K
        && (pvBuf = RTMemPageAlloc(cbBuf = _128K)) != NULL)
        pBufState->iAllocType = 2;
    else if (   cbToCopy >= _128K
             && (pvBuf = RTMemTmpAlloc(cbBuf = _128K)) != NULL)
        pBufState->iAllocType = 1;
    else if (   cbToCopy < _128K
             && cbToCopy >= _4K
             && (pvBuf = RTMemTmpAlloc(cbBuf = RT_ALIGN_Z(cbToCopy, 32))) != NULL)
        pBufState->iAllocType = 1;
    else if (   cbToCopy >= _4K
             && (pvBuf = RTMemTmpAlloc(cbBuf = _4K)) != NULL)
        pBufState->iAllocType = 1;
    else
        pBufState->iAllocType = 0;
    pBufState->pbBuf  = (uint8_t *)pvBuf;
    pBufState->cbBuf  = cbBuf;
    pBufState->uMagic = RTFILECOPYPARTBUFSTATE_MAGIC;
    return VINF_SUCCESS;
}


#ifndef IPRT_FALLBACK_VERSION
RTDECL(void) RTFileCopyPartCleanup(PRTFILECOPYPARTBUFSTATE pBufState)
#else
static void rtFileCopyPartCleanupFallback(PRTFILECOPYPARTBUFSTATE pBufState)
#endif
{
    AssertReturnVoid(pBufState->uMagic == RTFILECOPYPARTBUFSTATE_MAGIC);
    if (pBufState->iAllocType == 1)
        RTMemTmpFree(pBufState->pbBuf);
    else if (pBufState->iAllocType == 2)
        RTMemPageFree(pBufState->pbBuf, pBufState->cbBuf);
    pBufState->pbBuf  = NULL;
    pBufState->cbBuf  = 0;
    pBufState->uMagic = ~RTFILECOPYPARTBUFSTATE_MAGIC;
}


#ifndef IPRT_FALLBACK_VERSION
RTDECL(int) RTFileCopyPartEx(RTFILE hFileSrc, RTFOFF offSrc, RTFILE hFileDst, RTFOFF offDst, uint64_t cbToCopy,
                             uint32_t fFlags, PRTFILECOPYPARTBUFSTATE pBufState, uint64_t *pcbCopied)
#else
static int rtFileCopyPartExFallback(RTFILE hFileSrc, RTFOFF offSrc, RTFILE hFileDst, RTFOFF offDst, uint64_t cbToCopy,
                                    uint32_t fFlags, PRTFILECOPYPARTBUFSTATE pBufState, uint64_t *pcbCopied)
#endif
{
    /*
     * Validate input.
     */
    if (pcbCopied)
        *pcbCopied = 0;
    AssertReturn(offSrc >= 0, VERR_NEGATIVE_SEEK);
    AssertReturn(offDst >= 0, VERR_NEGATIVE_SEEK);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);
    AssertReturn(pBufState->uMagic == RTFILECOPYPARTBUFSTATE_MAGIC, VERR_INVALID_FLAGS);

    /*
     * If nothing to copy, return right away.
     */
    if (!cbToCopy)
        return VINF_SUCCESS;

    if (pBufState->iAllocType == 0)
    {
        pBufState->cbBuf = (size_t)RT_MIN(_4K, cbToCopy);
        pBufState->pbBuf = (uint8_t *)alloca(pBufState->cbBuf);
        AssertReturn(pBufState->pbBuf, VERR_NO_MEMORY);
    }

    /*
     * Do the copying.
     */
    uint64_t cbCopied = 0;
    int      rc       = VINF_SUCCESS;
    do
    {
        size_t cbThisCopy = (size_t)RT_MIN(cbToCopy - cbCopied, pBufState->cbBuf);
        size_t cbActual   = 0;
        rc = RTFileReadAt(hFileSrc, offSrc + cbCopied, pBufState->pbBuf, cbThisCopy, &cbActual);
        if (RT_FAILURE(rc))
            break;
        if (cbActual == 0)
        {
            if (!pcbCopied)
                rc = VERR_EOF;
            break;
        }

        rc = RTFileWriteAt(hFileDst, offDst + cbCopied, pBufState->pbBuf, cbActual, NULL);
        if (RT_FAILURE(rc))
            break;

        cbCopied += cbActual;
    } while (cbCopied < cbToCopy);

    if (pcbCopied)
        *pcbCopied = cbCopied;

    return rc;
}

