/* $Id: bs3-cmn-SelSetupGate64.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3SelSetupGate64
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
#include "bs3kit-template-header.h"


#undef Bs3SelSetupGate64
BS3_CMN_DEF(void, Bs3SelSetupGate64,(X86DESC BS3_FAR *pDescPair, uint8_t bType, uint8_t bDpl, uint16_t uSel, uint64_t off))
{
    BS3_ASSERT(bDpl <= 3);
    BS3_ASSERT(bType <= 15);
    pDescPair[0].Gate.u16OffsetLow    = (uint16_t)off;
    pDescPair[0].Gate.u16OffsetHigh   = (uint16_t)(off >> 16);
    pDescPair[0].Gate.u16Sel          = uSel;
    pDescPair[0].Gate.u5ParmCount     = 0;
    pDescPair[0].Gate.u4Type          = bType;
    pDescPair[0].Gate.u2Dpl           = bDpl;
    pDescPair[0].Gate.u3Reserved      = 0;
    pDescPair[0].Gate.u1DescType      = 0; /* system */
    pDescPair[0].Gate.u1Present       = 1;
    pDescPair[1].au32[0]              = (uint32_t)(off >> 32);
    pDescPair[1].au32[1]              = 0;
}

