/* $Id: digest-vfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Cryptographic Hash / Message Digest API, VFS related interfaces
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
#include <iprt/crypto/digest.h>

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/vfs.h>


RTDECL(int) RTCrDigestUpdateFromVfsFile(RTCRDIGEST hDigest, RTVFSFILE hVfsFile, bool fRewindFile)
{
    int rc;
    if (fRewindFile)
        rc = RTVfsFileSeek(hVfsFile, 0, RTFILE_SEEK_BEGIN, NULL);
    else
        rc = VINF_SUCCESS;
    if (RT_SUCCESS(rc))
    {
        for (;;)
        {
            char   abBuf[_16K];
            size_t cbRead;
            rc = RTVfsFileRead(hVfsFile, abBuf, sizeof(abBuf), &cbRead);
            if (RT_SUCCESS(rc))
            {
                bool const fEof = rc == VINF_EOF;
                rc = RTCrDigestUpdate(hDigest, abBuf, cbRead);
                if (fEof || RT_FAILURE(rc))
                    break;
            }
            else
            {
                Assert(rc != VERR_EOF);
                break;
            }
        }
    }
    return rc;
}

