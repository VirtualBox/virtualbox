/* $Id: bs3-cmn-SelSetup16BitData.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3SelSetup16BitData
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include <bs3kit.h>


#undef Bs3SelSetup16BitData
BS3_CMN_DEF(void, Bs3SelSetup16BitData,(X86DESC BS3_FAR *pDesc, uint32_t uBaseAddr))
{
    pDesc->Gen.u16LimitLow = UINT16_C(0xffff);
    pDesc->Gen.u16BaseLow  = (uint16_t)uBaseAddr;
    pDesc->Gen.u8BaseHigh1 = (uint8_t)(uBaseAddr >> 16);
    pDesc->Gen.u4Type      = X86_SEL_TYPE_RW_ACC;
    pDesc->Gen.u1DescType  = 1; /* data/code */
    pDesc->Gen.u2Dpl       = 3;
    pDesc->Gen.u1Present   = 1;
    pDesc->Gen.u4LimitHigh = 0;
    pDesc->Gen.u1Available = 0;
    pDesc->Gen.u1Long      = 0;
    pDesc->Gen.u1DefBig    = 0;
    pDesc->Gen.u1Granularity = 0;
    pDesc->Gen.u8BaseHigh2 = (uint8_t)(uBaseAddr >> 24);
}

