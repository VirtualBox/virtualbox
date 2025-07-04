/* $Id: nocrt-setvbuf.cpp 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - No-CRT - setvbuf().
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#define IPRT_NO_CRT_FOR_3RD_PARTY
#include "internal/nocrt.h"
#include <iprt/nocrt/stdio.h>
#include <iprt/nocrt/errno.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/stream.h>


#undef setvbuf
int RT_NOCRT(setvbuf)(FILE *pFile, char *pchBuf, int iBufferingType, size_t cbBuf)
{
    Assert(!pchBuf); RT_NOREF(pchBuf); /* ignored */
    Assert(!cbBuf);  RT_NOREF(cbBuf);  /* ignored */

    RTSTRMBUFMODE enmBufMode;
    switch (iBufferingType)
    {
        case _IOFBF: enmBufMode = RTSTRMBUFMODE_FULL; break;
        case _IOLBF: enmBufMode = RTSTRMBUFMODE_LINE; break;
        case _IONBF: enmBufMode = RTSTRMBUFMODE_UNBUFFERED; break;
        default: AssertFailedReturnStmt(errno = EINVAL, -1);
    }

    int rc = RTStrmSetBufferingMode(pFile, enmBufMode);
    if (RT_SUCCESS(rc))
        return 0;
    errno = RTErrConvertToErrno(rc);
    return -1;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(setvbuf);

