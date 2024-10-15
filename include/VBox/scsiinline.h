/* $Id: scsiinline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox: SCSI inline helpers used by devices, drivers, etc.
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

#ifndef VBOX_INCLUDED_scsiinline_h
#define VBOX_INCLUDED_scsiinline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/stdint.h>

/** @defgroup grp_scsi_inline    The SCSI inline helpers
 * @{
 */


/**
 * Converts a given 16bit value to big endian and stores it in the given buffer.
 *
 * @param   pbBuf               The buffer to store the value into.
 * @param   u16Val              The value to store.
 */
DECLINLINE(void) scsiH2BE_U16(uint8_t *pbBuf, uint16_t u16Val)
{
    pbBuf[0] = u16Val >> 8;
    pbBuf[1] = u16Val;
}


/**
 * Converts a given 24bit value to big endian and stores it in the given buffer.
 *
 * @param   pbBuf               The buffer to store the value into.
 * @param   u32Val              The value to store.
 */
DECLINLINE(void) scsiH2BE_U24(uint8_t *pbBuf, uint32_t u32Val)
{
    pbBuf[0] = u32Val >> 16;
    pbBuf[1] = u32Val >> 8;
    pbBuf[2] = u32Val;
}


/**
 * Converts a given 32bit value to big endian and stores it in the given buffer.
 *
 * @param   pbBuf               The buffer to store the value into.
 * @param   u32Val              The value to store.
 */
DECLINLINE(void) scsiH2BE_U32(uint8_t *pbBuf, uint32_t u32Val)
{
    pbBuf[0] = u32Val >> 24;
    pbBuf[1] = u32Val >> 16;
    pbBuf[2] = u32Val >> 8;
    pbBuf[3] = u32Val;
}


/**
 * Converts a given 64bit value to big endian and stores it in the given buffer.
 *
 * @param   pbBuf               The buffer to store the value into.
 * @param   u64Val              The value to store.
 */
DECLINLINE(void) scsiH2BE_U64(uint8_t *pbBuf, uint64_t u64Val)
{
    pbBuf[0] = u64Val >> 56;
    pbBuf[1] = u64Val >> 48;
    pbBuf[2] = u64Val >> 40;
    pbBuf[3] = u64Val >> 32;
    pbBuf[4] = u64Val >> 24;
    pbBuf[5] = u64Val >> 16;
    pbBuf[6] = u64Val >> 8;
    pbBuf[7] = u64Val;
}

/**
 * Returns a 16bit value read from the given buffer converted to host endianess.
 *
 * @returns The converted 16bit value.
 * @param   pbBuf               The buffer to read the value from.
 */
DECLINLINE(uint16_t) scsiBE2H_U16(const uint8_t *pbBuf)
{
    return (pbBuf[0] << 8) | pbBuf[1];
}


/**
 * Returns a 24bit value read from the given buffer converted to host endianess.
 *
 * @returns The converted 24bit value as a 32bit unsigned integer.
 * @param   pbBuf               The buffer to read the value from.
 */
DECLINLINE(uint32_t) scsiBE2H_U24(const uint8_t *pbBuf)
{
    return (pbBuf[0] << 16) | (pbBuf[1] << 8) | pbBuf[2];
}


/**
 * Returns a 32bit value read from the given buffer converted to host endianess.
 *
 * @returns The converted 32bit value.
 * @param   pbBuf               The buffer to read the value from.
 */
DECLINLINE(uint32_t) scsiBE2H_U32(const uint8_t *pbBuf)
{
    return (pbBuf[0] << 24) | (pbBuf[1] << 16) | (pbBuf[2] << 8) | pbBuf[3];
}


/**
 * Returns a 64bit value read from the given buffer converted to host endianess.
 *
 * @returns The converted 64bit value.
 * @param   pbBuf               The buffer to read the value from.
 */
DECLINLINE(uint64_t) scsiBE2H_U64(const uint8_t *pbBuf)
{
    return   ((uint64_t)pbBuf[0] << 56)
           | ((uint64_t)pbBuf[1] << 48)
           | ((uint64_t)pbBuf[2] << 40)
           | ((uint64_t)pbBuf[3] << 32)
           | ((uint64_t)pbBuf[4] << 24)
           | ((uint64_t)pbBuf[5] << 16)
           | ((uint64_t)pbBuf[6] << 8)
           | (uint64_t)pbBuf[7];
}


/**
 * Converts the given LBA number to the MSF (Minutes:Seconds:Frames) format
 * and stores it in the given buffer.
 *
 * @param   pbBuf               The buffer to store the value into.
 * @param   iLBA                The LBA to convert.
 */
DECLINLINE(void) scsiLBA2MSF(uint8_t *pbBuf, uint32_t iLBA)
{
    iLBA += 150;
    pbBuf[0] = (iLBA / 75) / 60;
    pbBuf[1] = (iLBA / 75) % 60;
    pbBuf[2] = iLBA % 75;
}


/**
 * Converts a MSF formatted address value read from the given buffer
 * to an LBA number.
 *
 * @returns The LBA number.
 * @param   pbBuf               The buffer to read the MSF formatted address
 *                              from.
 */
DECLINLINE(uint32_t) scsiMSF2LBA(const uint8_t *pbBuf)
{
    return (pbBuf[0] * 60 + pbBuf[1]) * 75 + pbBuf[2] - 150;
}


/**
 * Copies a given string to the given destination padding all unused space
 * in the destination with spaces.
 *
 * @param   pbDst               Where to store the string padded with spaces.
 * @param   pbSrc               The string to copy.
 * @param   cbSize              Size of the destination buffer.
 */
DECLINLINE(void) scsiPadStr(uint8_t *pbDst, const char *pbSrc, uint32_t cbSize)
{
    uint32_t i;
    for (i = 0; i < cbSize; i++)
    {
        if (*pbSrc)
            pbDst[i] = *pbSrc++;
        else
            pbDst[i] = ' ';
    }
}


/**
 * Copies a given string to the given destination padding all unused space
 * in the destination with spaces.
 *
 * @param   pbDst               Where to store the string padded with spaces.
 * @param   pbSrc               The string to copy.
 * @param   cbSize              Size of the destination buffer.
 */
DECLINLINE(void) scsiPadStrS(int8_t *pbDst, const char *pbSrc, uint32_t cbSize)
{
    uint32_t i;
    for (i = 0; i < cbSize; i++)
    {
        if (*pbSrc)
            pbDst[i] = *pbSrc++;
        else
            pbDst[i] = ' ';
    }
}

/** @} */

#endif /* !VBOX_INCLUDED_scsiinline_h */

