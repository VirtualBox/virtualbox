/* $Id: crc16ccitt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - CRC-16-CCITT.
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
#include <iprt/crc.h>
#include "internal/iprt.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/**
 * CRC-16-CCTII / CRC-CCTII / CRC 010041 table.
 */
static uint16_t const g_au16Crc16Cctii[0x100] =
{
    UINT16_C(0x0000), UINT16_C(0x1021), UINT16_C(0x2042), UINT16_C(0x3063), UINT16_C(0x4084), UINT16_C(0x50a5), UINT16_C(0x60c6), UINT16_C(0x70e7),
    UINT16_C(0x8108), UINT16_C(0x9129), UINT16_C(0xa14a), UINT16_C(0xb16b), UINT16_C(0xc18c), UINT16_C(0xd1ad), UINT16_C(0xe1ce), UINT16_C(0xf1ef),
    UINT16_C(0x1231), UINT16_C(0x0210), UINT16_C(0x3273), UINT16_C(0x2252), UINT16_C(0x52b5), UINT16_C(0x4294), UINT16_C(0x72f7), UINT16_C(0x62d6),
    UINT16_C(0x9339), UINT16_C(0x8318), UINT16_C(0xb37b), UINT16_C(0xa35a), UINT16_C(0xd3bd), UINT16_C(0xc39c), UINT16_C(0xf3ff), UINT16_C(0xe3de),
    UINT16_C(0x2462), UINT16_C(0x3443), UINT16_C(0x0420), UINT16_C(0x1401), UINT16_C(0x64e6), UINT16_C(0x74c7), UINT16_C(0x44a4), UINT16_C(0x5485),
    UINT16_C(0xa56a), UINT16_C(0xb54b), UINT16_C(0x8528), UINT16_C(0x9509), UINT16_C(0xe5ee), UINT16_C(0xf5cf), UINT16_C(0xc5ac), UINT16_C(0xd58d),
    UINT16_C(0x3653), UINT16_C(0x2672), UINT16_C(0x1611), UINT16_C(0x0630), UINT16_C(0x76d7), UINT16_C(0x66f6), UINT16_C(0x5695), UINT16_C(0x46b4),
    UINT16_C(0xb75b), UINT16_C(0xa77a), UINT16_C(0x9719), UINT16_C(0x8738), UINT16_C(0xf7df), UINT16_C(0xe7fe), UINT16_C(0xd79d), UINT16_C(0xc7bc),
    UINT16_C(0x48c4), UINT16_C(0x58e5), UINT16_C(0x6886), UINT16_C(0x78a7), UINT16_C(0x0840), UINT16_C(0x1861), UINT16_C(0x2802), UINT16_C(0x3823),
    UINT16_C(0xc9cc), UINT16_C(0xd9ed), UINT16_C(0xe98e), UINT16_C(0xf9af), UINT16_C(0x8948), UINT16_C(0x9969), UINT16_C(0xa90a), UINT16_C(0xb92b),
    UINT16_C(0x5af5), UINT16_C(0x4ad4), UINT16_C(0x7ab7), UINT16_C(0x6a96), UINT16_C(0x1a71), UINT16_C(0x0a50), UINT16_C(0x3a33), UINT16_C(0x2a12),
    UINT16_C(0xdbfd), UINT16_C(0xcbdc), UINT16_C(0xfbbf), UINT16_C(0xeb9e), UINT16_C(0x9b79), UINT16_C(0x8b58), UINT16_C(0xbb3b), UINT16_C(0xab1a),
    UINT16_C(0x6ca6), UINT16_C(0x7c87), UINT16_C(0x4ce4), UINT16_C(0x5cc5), UINT16_C(0x2c22), UINT16_C(0x3c03), UINT16_C(0x0c60), UINT16_C(0x1c41),
    UINT16_C(0xedae), UINT16_C(0xfd8f), UINT16_C(0xcdec), UINT16_C(0xddcd), UINT16_C(0xad2a), UINT16_C(0xbd0b), UINT16_C(0x8d68), UINT16_C(0x9d49),
    UINT16_C(0x7e97), UINT16_C(0x6eb6), UINT16_C(0x5ed5), UINT16_C(0x4ef4), UINT16_C(0x3e13), UINT16_C(0x2e32), UINT16_C(0x1e51), UINT16_C(0x0e70),
    UINT16_C(0xff9f), UINT16_C(0xefbe), UINT16_C(0xdfdd), UINT16_C(0xcffc), UINT16_C(0xbf1b), UINT16_C(0xaf3a), UINT16_C(0x9f59), UINT16_C(0x8f78),
    UINT16_C(0x9188), UINT16_C(0x81a9), UINT16_C(0xb1ca), UINT16_C(0xa1eb), UINT16_C(0xd10c), UINT16_C(0xc12d), UINT16_C(0xf14e), UINT16_C(0xe16f),
    UINT16_C(0x1080), UINT16_C(0x00a1), UINT16_C(0x30c2), UINT16_C(0x20e3), UINT16_C(0x5004), UINT16_C(0x4025), UINT16_C(0x7046), UINT16_C(0x6067),
    UINT16_C(0x83b9), UINT16_C(0x9398), UINT16_C(0xa3fb), UINT16_C(0xb3da), UINT16_C(0xc33d), UINT16_C(0xd31c), UINT16_C(0xe37f), UINT16_C(0xf35e),
    UINT16_C(0x02b1), UINT16_C(0x1290), UINT16_C(0x22f3), UINT16_C(0x32d2), UINT16_C(0x4235), UINT16_C(0x5214), UINT16_C(0x6277), UINT16_C(0x7256),
    UINT16_C(0xb5ea), UINT16_C(0xa5cb), UINT16_C(0x95a8), UINT16_C(0x8589), UINT16_C(0xf56e), UINT16_C(0xe54f), UINT16_C(0xd52c), UINT16_C(0xc50d),
    UINT16_C(0x34e2), UINT16_C(0x24c3), UINT16_C(0x14a0), UINT16_C(0x0481), UINT16_C(0x7466), UINT16_C(0x6447), UINT16_C(0x5424), UINT16_C(0x4405),
    UINT16_C(0xa7db), UINT16_C(0xb7fa), UINT16_C(0x8799), UINT16_C(0x97b8), UINT16_C(0xe75f), UINT16_C(0xf77e), UINT16_C(0xc71d), UINT16_C(0xd73c),
    UINT16_C(0x26d3), UINT16_C(0x36f2), UINT16_C(0x0691), UINT16_C(0x16b0), UINT16_C(0x6657), UINT16_C(0x7676), UINT16_C(0x4615), UINT16_C(0x5634),
    UINT16_C(0xd94c), UINT16_C(0xc96d), UINT16_C(0xf90e), UINT16_C(0xe92f), UINT16_C(0x99c8), UINT16_C(0x89e9), UINT16_C(0xb98a), UINT16_C(0xa9ab),
    UINT16_C(0x5844), UINT16_C(0x4865), UINT16_C(0x7806), UINT16_C(0x6827), UINT16_C(0x18c0), UINT16_C(0x08e1), UINT16_C(0x3882), UINT16_C(0x28a3),
    UINT16_C(0xcb7d), UINT16_C(0xdb5c), UINT16_C(0xeb3f), UINT16_C(0xfb1e), UINT16_C(0x8bf9), UINT16_C(0x9bd8), UINT16_C(0xabbb), UINT16_C(0xbb9a),
    UINT16_C(0x4a75), UINT16_C(0x5a54), UINT16_C(0x6a37), UINT16_C(0x7a16), UINT16_C(0x0af1), UINT16_C(0x1ad0), UINT16_C(0x2ab3), UINT16_C(0x3a92),
    UINT16_C(0xfd2e), UINT16_C(0xed0f), UINT16_C(0xdd6c), UINT16_C(0xcd4d), UINT16_C(0xbdaa), UINT16_C(0xad8b), UINT16_C(0x9de8), UINT16_C(0x8dc9),
    UINT16_C(0x7c26), UINT16_C(0x6c07), UINT16_C(0x5c64), UINT16_C(0x4c45), UINT16_C(0x3ca2), UINT16_C(0x2c83), UINT16_C(0x1ce0), UINT16_C(0x0cc1),
    UINT16_C(0xef1f), UINT16_C(0xff3e), UINT16_C(0xcf5d), UINT16_C(0xdf7c), UINT16_C(0xaf9b), UINT16_C(0xbfba), UINT16_C(0x8fd9), UINT16_C(0x9ff8),
    UINT16_C(0x6e17), UINT16_C(0x7e36), UINT16_C(0x4e55), UINT16_C(0x5e74), UINT16_C(0x2e93), UINT16_C(0x3eb2), UINT16_C(0x0ed1), UINT16_C(0x1ef0),
};



RTDECL(uint16_t)    RTCrc16Ccitt(const void *pv, size_t cb)
{
    uint16_t        uCrc = 0;
    const uint8_t  *pb   = (const uint8_t *)pv;
    while (cb-- > 0)
        uCrc = g_au16Crc16Cctii[(uint8_t)(uCrc >> 8) ^ *pb++] ^ (uCrc << 8);
    return uCrc;
}


RTDECL(uint16_t)    RTCrc16CcittStart(void)
{
    return 0;
}


RTDECL(uint16_t)    RTCrc16CcittProcess(uint16_t uCrc, const void *pv, size_t cb)
{
    const uint8_t  *pb   = (const uint8_t *)pv;
    while (cb-- > 0)
        uCrc = g_au16Crc16Cctii[(uint8_t)(uCrc >> 8) ^ *pb++] ^ (uCrc << 8);
    return uCrc;
}


RTDECL(uint16_t)    RTCrc16CcittFinish(uint16_t uCrc)
{
    return uCrc;
}

