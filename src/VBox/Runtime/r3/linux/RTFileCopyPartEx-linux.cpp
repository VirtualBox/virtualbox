/* $Id: RTFileCopyPartEx-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTFileCopyPartEx, linux specific implementation.
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

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>

#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>

#ifndef __NR_copy_file_range
# if defined(RT_ARCH_X86)
#  define __NR_copy_file_range      377
# elif defined(RT_ARCH_AMD64)
#  define __NR_copy_file_range      326
# endif
#endif


#ifndef __NR_copy_file_range
# include "../../generic/RTFileCopyPartEx-generic.cpp"
#else  /* __NR_copy_file_range - whole file */
/* Include the generic code as a fallback since copy_file_range is rather new . */
# define IPRT_FALLBACK_VERSION
# include "../../generic/RTFileCopyPartEx-generic.cpp"
# undef  IPRT_FALLBACK_VERSION


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static int32_t volatile g_fCopyFileRangeSupported = -1;


DECLINLINE(loff_t)
MyCopyFileRangeSysCall(int fdIn, loff_t *poffIn, int fdOut, loff_t *poffOut, size_t cbChunk, unsigned int fFlags)
{
    return syscall(__NR_copy_file_range, fdIn, poffIn, fdOut, poffOut, cbChunk, fFlags);
}


DECL_NO_INLINE(static, bool) HasCopyFileRangeSyscallSlow(void)
{
    errno = 0;
    MyCopyFileRangeSysCall(-1, NULL, -1, NULL, 4096, 0);
    if (errno != ENOSYS)
    {
        ASMAtomicWriteS32(&g_fCopyFileRangeSupported, 1);
        return true;
    }
    ASMAtomicWriteS32(&g_fCopyFileRangeSupported, 0);
    return false;
}

DECLINLINE(bool) HasCopyFileRangeSyscall(void)
{
    int32_t i = ASMAtomicUoReadS32(&g_fCopyFileRangeSupported);
    if (i != -1)
        return i == 1;
    return HasCopyFileRangeSyscallSlow();
}



RTDECL(int) RTFileCopyPartPrep(PRTFILECOPYPARTBUFSTATE pBufState, uint64_t cbToCopy)
{
    if (HasCopyFileRangeSyscall())
    {
        pBufState->iAllocType = -42;
        pBufState->pbBuf      = NULL;
        pBufState->cbBuf      = 0;
        pBufState->uMagic     = RTFILECOPYPARTBUFSTATE_MAGIC;
        return VINF_SUCCESS;
    }
    return rtFileCopyPartPrepFallback(pBufState, cbToCopy);
}


RTDECL(void) RTFileCopyPartCleanup(PRTFILECOPYPARTBUFSTATE pBufState)
{
    return rtFileCopyPartCleanupFallback(pBufState);
}


RTDECL(int) RTFileCopyPartEx(RTFILE hFileSrc, RTFOFF offSrc, RTFILE hFileDst, RTFOFF offDst, uint64_t cbToCopy,
                             uint32_t fFlags, PRTFILECOPYPARTBUFSTATE pBufState, uint64_t *pcbCopied)
{
    /*
     * Validate input.
     */
    if (pcbCopied)
        *pcbCopied = 0;
    AssertReturn(pBufState->uMagic == RTFILECOPYPARTBUFSTATE_MAGIC, VERR_INVALID_FLAGS);
    if (pBufState->iAllocType == -42)
    { /* more and more likely as time goes */ }
    else
        return rtFileCopyPartExFallback(hFileSrc, offSrc, hFileDst, offDst, cbToCopy, fFlags, pBufState, pcbCopied);
    AssertReturn(offSrc >= 0, VERR_NEGATIVE_SEEK);
    AssertReturn(offDst >= 0, VERR_NEGATIVE_SEEK);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);

    /*
     * If nothing to copy, return right away.
     */
    if (!cbToCopy)
        return VINF_SUCCESS;

    /*
     * Do the copying.
     */
    uint64_t cbCopied = 0;
    int      rc       = VINF_SUCCESS;
    do
    {
        size_t  cbThisCopy = (size_t)RT_MIN(cbToCopy - cbCopied, _1G);
        loff_t  offThisDst = offSrc + cbCopied;
        loff_t  offThisSrc = offDst + cbCopied;
        ssize_t cbActual   = MyCopyFileRangeSysCall((int)RTFileToNative(hFileSrc), &offThisSrc,
                                                    (int)RTFileToNative(hFileDst), &offThisDst,
                                                    cbThisCopy, 0);
        if (cbActual < 0)
        {
            rc = errno;
            Assert(rc != 0);
            rc = rc != 0 ? RTErrConvertFromErrno(rc) : VERR_READ_ERROR;
            if (rc != VERR_NOT_SAME_DEVICE || cbCopied != 0)
                break;

            /* Fall back to generic implementation if the syscall refuses to handle the case. */
            rc = rtFileCopyPartPrepFallback(pBufState, cbToCopy);
            if (RT_SUCCESS(rc))
                return rtFileCopyPartExFallback(hFileSrc, offSrc, hFileDst, offDst, cbToCopy, fFlags, pBufState, pcbCopied);
            return rc;
        }
        Assert(offThisSrc == offSrc + (int64_t)cbCopied + cbActual);
        Assert(offThisDst == offDst + (int64_t)cbCopied + cbActual);

        if (cbActual == 0)
        {
            if (!pcbCopied)
                rc = VERR_EOF;
            break;
        }

        cbCopied += cbActual;
    } while (cbCopied < cbToCopy);

    if (pcbCopied)
        *pcbCopied = cbCopied;

    return rc;
}

#endif /* __NR_copy_file_range */

