/* $Id: RTCrRandBytes-openssl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - RTCrRandBytes implementation using OpenSSL.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#ifdef IPRT_WITH_OPENSSL
# include "internal/iprt.h"
# include <iprt/crypto/misc.h>

# include <iprt/rand.h>
# include <iprt/assert.h>
# include <iprt/err.h>

# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/rand.h>
# include "internal/openssl-post.h"


RTDECL(int) RTCrRandBytes(void *pvDst, size_t cbDst)
{
    /* Make sure the return buffer is always fully initialized in case the caller
       doesn't properly check the return value. */
    RTRandBytes(pvDst, cbDst); /* */

    /* Get cryptographically strong random. */
    rtCrOpenSslInit();
    Assert((size_t)(int)cbDst == cbDst);
    int rcOpenSsl = RAND_bytes((uint8_t *)pvDst, (int)cbDst);
    return rcOpenSsl > 0 ? VINF_SUCCESS : rcOpenSsl == 0 ? VERR_CR_RANDOM_FAILED : VERR_CR_RANDOM_SETUP_FAILED;
}

#endif /* IPRT_WITH_OPENSSL */

