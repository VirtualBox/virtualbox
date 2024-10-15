/* $Id: crc32-zlib.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - CRC-32 on top of zlib (very fast).
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/crc.h>

#include <zlib.h>

/** @todo Check if we can't just use the zlib code directly here. */

/**
 * Deal with blocks that are too big for the uInt type.
 */
static uint32_t rtCrc32ProcessTooBig(uint32_t uCRC32, const void *pv, size_t cb)
{
    const Bytef *pb = (const Bytef *)pv;
    do
    {
        uInt const cbChunk = cb <= ~(uInt)0 ? (uInt)cb : ~(uInt)0;
        uCRC32 = crc32(uCRC32, pb, cbChunk);
        pb += cbChunk;
        cb -= cbChunk;
    } while (!cb);
    return uCRC32;
}

RTDECL(uint32_t) RTCrc32(const void *pv, size_t cb)
{
    uint32_t uCrc = crc32(0, NULL, 0);
    if (RT_UNLIKELY((uInt)cb == cb))
        uCrc = crc32(uCrc, (const Bytef *)pv, (uInt)cb);
    else
        uCrc = rtCrc32ProcessTooBig(uCrc, pv, cb);
    return uCrc;
}
RT_EXPORT_SYMBOL(RTCrc32);


RTDECL(uint32_t) RTCrc32Start(void)
{
    return crc32(0, NULL, 0);
}
RT_EXPORT_SYMBOL(RTCrc32Start);


RTDECL(uint32_t) RTCrc32Process(uint32_t uCRC32, const void *pv, size_t cb)
{
    if (RT_UNLIKELY((uInt)cb == cb))
        uCRC32 = crc32(uCRC32, (const Bytef *)pv, (uInt)cb);
    else
        uCRC32 = rtCrc32ProcessTooBig(uCRC32, pv, cb);
    return uCRC32;
}
RT_EXPORT_SYMBOL(RTCrc32Process);


RTDECL(uint32_t) RTCrc32Finish(uint32_t uCRC32)
{
    return uCRC32;
}
RT_EXPORT_SYMBOL(RTCrc32Finish);

