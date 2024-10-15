/* $Id: bs3cpudt.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Tests Bs3CpuDetect_rm.
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

#include "bs3kit.h"
#include <stdio.h>


unsigned StoreMsw(void);
#pragma aux StoreMsw = \
    ".286" \
    "smsw ax" \
    value [ax];

void LoadMsw(unsigned);
#pragma aux LoadMsw = \
    ".286p" \
    "lmsw ax" \
    parm [ax];

int main()
{
    uint16_t volatile usCpu = Bs3CpuDetect_rm();
    printf("usCpu=%#x\n", usCpu);
    if ((usCpu & BS3CPU_TYPE_MASK) >= BS3CPU_80286)
    {
        printf("(42=%d) msw=%#x (42=%d)\n", 42, StoreMsw(), 42);
        LoadMsw(0);
        printf("lmsw 0 => msw=%#x (42=%d)\n", StoreMsw(), 42);
    }
    return 0;
}

