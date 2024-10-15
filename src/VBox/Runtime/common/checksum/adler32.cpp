/* $Id: adler32.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Adler-32
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
#include <iprt/crc.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define RTCRC_ADLER_32_NUMBER       65521


RTDECL(uint32_t) RTCrcAdler32(void const *pv, size_t cb)
{
    /* Don't want to do the unrolling twice. */
    return RTCrcAdler32Process(RTCrcAdler32Start(), pv, cb);
}


RTDECL(uint32_t) RTCrcAdler32Start(void)
{
    return 1;
}


RTDECL(uint32_t) RTCrcAdler32Process(uint32_t u32Crc, void const *pv, size_t cb)
{
    uint8_t const  *pbSrc = (uint8_t const *)pv;
    uint32_t        a     = u32Crc & 0xffff;
    uint32_t        b     = u32Crc >> 16;
    if (cb < 64 /* randomly selected number */)
    {
        while (cb-- > 0)
        {
            a += *pbSrc++;
            a %= RTCRC_ADLER_32_NUMBER;
            b += a;
            b %= RTCRC_ADLER_32_NUMBER;
        }
    }
    else
    {
        switch (((uintptr_t)pbSrc & 0x3))
        {
            case 0:
                break;

            case 1:
                a += *pbSrc++;
                a %= RTCRC_ADLER_32_NUMBER;
                b += a;
                b %= RTCRC_ADLER_32_NUMBER;
                cb--;
                RT_FALL_THRU();

            case 2:
                a += *pbSrc++;
                a %= RTCRC_ADLER_32_NUMBER;
                b += a;
                b %= RTCRC_ADLER_32_NUMBER;
                cb--;
                RT_FALL_THRU();

            case 3:
                a += *pbSrc++;
                a %= RTCRC_ADLER_32_NUMBER;
                b += a;
                b %= RTCRC_ADLER_32_NUMBER;
                cb--;
                break;
        }

        while (cb >= 4)
        {
            uint32_t u32 = *(uint32_t const *)pbSrc;
            pbSrc += 4;

            a += u32 & 0xff;
            a %= RTCRC_ADLER_32_NUMBER;
            b += a;
            b %= RTCRC_ADLER_32_NUMBER;

            a += (u32 >> 8) & 0xff;
            a %= RTCRC_ADLER_32_NUMBER;
            b += a;
            b %= RTCRC_ADLER_32_NUMBER;

            a += (u32 >> 16) & 0xff;
            a %= RTCRC_ADLER_32_NUMBER;
            b += a;
            b %= RTCRC_ADLER_32_NUMBER;

            a += (u32 >> 24) & 0xff;
            a %= RTCRC_ADLER_32_NUMBER;
            b += a;
            b %= RTCRC_ADLER_32_NUMBER;

            cb -= 4;
        }

        switch (cb)
        {
            case 0:
                break;

            case 3:
                a += *pbSrc++;
                a %= RTCRC_ADLER_32_NUMBER;
                b += a;
                b %= RTCRC_ADLER_32_NUMBER;
                cb--;
                RT_FALL_THRU();

            case 2:
                a += *pbSrc++;
                a %= RTCRC_ADLER_32_NUMBER;
                b += a;
                b %= RTCRC_ADLER_32_NUMBER;
                cb--;
                RT_FALL_THRU();

            case 1:
                a += *pbSrc++;
                a %= RTCRC_ADLER_32_NUMBER;
                b += a;
                b %= RTCRC_ADLER_32_NUMBER;
                cb--;
                break;
        }
    }

    return a | (b << 16);
}


RTDECL(uint32_t) RTCrcAdler32Finish(uint32_t u32Crc)
{
    return u32Crc;
}

