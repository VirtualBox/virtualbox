/* $Id: bs3-rm-InitGdt.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3InitGdt
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
#define BS3_USE_RM_TEXT_SEG 1
#include "bs3kit-template-header.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/


BS3_DECL_FAR(void) Bs3InitGdt_rm_far(void)
{
#if 0 /* This totaly messes us up when going back to raw-mode for BIOS work. */
    Bs3Gdte_R0_CS16.Gen.u16LimitLow     = Bs3Text16_Size - 1;
    Bs3Gdte_R1_CS16.Gen.u16LimitLow     = Bs3Text16_Size - 1;
    Bs3Gdte_R2_CS16.Gen.u16LimitLow     = Bs3Text16_Size - 1;
    Bs3Gdte_R3_CS16.Gen.u16LimitLow     = Bs3Text16_Size - 1;
#endif
    Bs3Gdte_RMTEXT16_CS.Gen.u16LimitLow = Bs3RmText16_Size - 1;
    Bs3Gdte_X0TEXT16_CS.Gen.u16LimitLow = Bs3X0Text16_Size - 1;
    Bs3Gdte_X1TEXT16_CS.Gen.u16LimitLow = Bs3X1Text16_Size - 1;

    Bs3Gdte_RMTEXT16_CS.Gen.u16BaseLow  = (uint16_t)Bs3RmText16_FlatAddr;
    Bs3Gdte_X0TEXT16_CS.Gen.u16BaseLow  = (uint16_t)Bs3X0Text16_FlatAddr;
    Bs3Gdte_X1TEXT16_CS.Gen.u16BaseLow  = (uint16_t)Bs3X1Text16_FlatAddr;

    Bs3Gdte_RMTEXT16_CS.Gen.u8BaseHigh1 = (uint8_t)(Bs3RmText16_FlatAddr >> 16);
    Bs3Gdte_X0TEXT16_CS.Gen.u8BaseHigh1 = (uint8_t)(Bs3X0Text16_FlatAddr >> 16);
    Bs3Gdte_X1TEXT16_CS.Gen.u8BaseHigh1 = (uint8_t)(Bs3X1Text16_FlatAddr >> 16);
}

