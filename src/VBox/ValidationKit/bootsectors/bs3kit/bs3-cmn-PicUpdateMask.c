/* $Id: bs3-cmn-PicUpdateMask.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - PIC Setup.
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
#include <iprt/asm-amd64-x86.h>
#include "bs3-cmn-pic.h"


#undef Bs3PicUpdateMask
BS3_CMN_DEF(uint16_t, Bs3PicUpdateMask,(uint16_t fAndMask, uint16_t fOrMask))
{
    uint8_t bPic0Mask = (ASMInU8(BS3_PIC_PORT_MASTER + 1) & (uint8_t)fAndMask) | (uint8_t)fOrMask;
    uint8_t bPic1Mask = (ASMInU8(BS3_PIC_PORT_SLAVE  + 1) & (fAndMask >> 8))   | (fOrMask >> 8);
    ASMOutU8(BS3_PIC_PORT_SLAVE  + 1, bPic1Mask);
    ASMOutU8(BS3_PIC_PORT_MASTER + 1, bPic0Mask);
    return RT_MAKE_U16(bPic0Mask, bPic1Mask);
}

