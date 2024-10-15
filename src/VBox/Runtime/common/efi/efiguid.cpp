/* $Id: efiguid.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - EFI GUID conversion helpers.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/efi.h>

#include <iprt/cdefs.h>
#include <iprt/asm.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

RTDECL(PRTUUID) RTEfiGuidToUuid(PRTUUID pUuid, PCEFI_GUID pEfiGuid)
{
    pUuid->Gen.u32TimeLow              = RT_LE2H_U32(pEfiGuid->u32Data1);
    pUuid->Gen.u16TimeMid              = RT_LE2H_U16(pEfiGuid->u16Data2);
    pUuid->Gen.u16TimeHiAndVersion     = RT_LE2H_U16(pEfiGuid->u16Data3);
    pUuid->Gen.u8ClockSeqHiAndReserved = pEfiGuid->abData4[0];
    pUuid->Gen.u8ClockSeqLow           = pEfiGuid->abData4[1];
    pUuid->Gen.au8Node[0]              = pEfiGuid->abData4[2];
    pUuid->Gen.au8Node[1]              = pEfiGuid->abData4[3];
    pUuid->Gen.au8Node[2]              = pEfiGuid->abData4[4];
    pUuid->Gen.au8Node[3]              = pEfiGuid->abData4[5];
    pUuid->Gen.au8Node[4]              = pEfiGuid->abData4[6];
    pUuid->Gen.au8Node[5]              = pEfiGuid->abData4[7];
    return pUuid;
}


RTDECL(PEFI_GUID) RTEfiGuidFromUuid(PEFI_GUID pEfiGuid, PCRTUUID pUuid)
{
    pEfiGuid->u32Data1   = RT_H2LE_U32(pUuid->Gen.u32TimeLow);
    pEfiGuid->u16Data2   = RT_H2LE_U16(pUuid->Gen.u16TimeMid);
    pEfiGuid->u16Data3   = RT_H2LE_U16(pUuid->Gen.u16TimeHiAndVersion);
    pEfiGuid->abData4[0] = pUuid->Gen.u8ClockSeqHiAndReserved;
    pEfiGuid->abData4[1] = pUuid->Gen.u8ClockSeqLow;
    pEfiGuid->abData4[2] = pUuid->Gen.au8Node[0];
    pEfiGuid->abData4[3] = pUuid->Gen.au8Node[1];
    pEfiGuid->abData4[4] = pUuid->Gen.au8Node[2];
    pEfiGuid->abData4[5] = pUuid->Gen.au8Node[3];
    pEfiGuid->abData4[6] = pUuid->Gen.au8Node[4];
    pEfiGuid->abData4[7] = pUuid->Gen.au8Node[5];
    return pEfiGuid;
}


RTDECL(int) RTEfiGuidCompare(PCEFI_GUID pGuid1, PCEFI_GUID pGuid2)
{
    /*
     * Special cases.
     */
    if (pGuid1 == pGuid2)
        return 0;
    AssertPtrReturn(pGuid1, -1);
    AssertPtrReturn(pGuid2, 1);

    /*
     * Standard cases.
     */
    if (pGuid1->u32Data1 != pGuid2->u32Data1)
        return pGuid1->u32Data1 < pGuid2->u32Data1 ? -1 : 1;
    if (pGuid1->u16Data2 != pGuid2->u16Data2)
        return pGuid1->u16Data2 < pGuid2->u16Data2 ? -1 : 1;
    if (pGuid1->u16Data3 != pGuid2->u16Data3)
        return pGuid1->u16Data3 < pGuid2->u16Data3 ? -1 : 1;
    if (pGuid1->abData4[0] != pGuid2->abData4[0])
        return pGuid1->abData4[0] < pGuid2->abData4[0] ? -1 : 1;
    if (pGuid1->abData4[1] != pGuid2->abData4[1])
        return pGuid1->abData4[1] < pGuid2->abData4[1] ? -1 : 1;
    if (pGuid1->abData4[2] != pGuid2->abData4[2])
        return pGuid1->abData4[2] < pGuid2->abData4[2] ? -1 : 1;
    if (pGuid1->abData4[3] != pGuid2->abData4[3])
        return pGuid1->abData4[3] < pGuid2->abData4[3] ? -1 : 1;
    if (pGuid1->abData4[4] != pGuid2->abData4[4])
        return pGuid1->abData4[4] < pGuid2->abData4[4] ? -1 : 1;
    if (pGuid1->abData4[5] != pGuid2->abData4[5])
        return pGuid1->abData4[5] < pGuid2->abData4[5] ? -1 : 1;
    if (pGuid1->abData4[6] != pGuid2->abData4[6])
        return pGuid1->abData4[6] < pGuid2->abData4[6] ? -1 : 1;
    if (pGuid1->abData4[7] != pGuid2->abData4[7])
        return pGuid1->abData4[7] < pGuid2->abData4[7] ? -1 : 1;
    return 0;
}

