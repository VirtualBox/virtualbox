/* $Id: bs3-cmn-PerCpuData.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Per CPU Data.
 *
 * @remarks Not quite sure how to do per-cpu data yet, but this is stuff
 *          that eventually needs to be per CPU.
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
#include "bs3-cmn-test.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if ARCH_BITS == 16

/** Hint for 16-bit trap handlers regarding the high word of EIP. */
uint32_t g_uBs3TrapEipHint = 0;

/** Flat pointer to a BS3TRAPFRAME registered by Bs3TrapSetJmp.
 * When this is non-zero, the setjmp is considered armed. */
uint32_t g_pBs3TrapSetJmpFrame = 0;

/** The current CPU mode. */
uint8_t  g_bBs3CurrentMode = BS3_MODE_RM;

uint8_t  g_bStupidUnalignedCompiler1 = 0xfe;

/** Set to disable special V8086 \#GP and \#UD handling in Bs3TrapDefaultHandler.
 * This is useful for getting   */
bool volatile g_fBs3TrapNoV86Assist = false;

/** The context of the last Bs3TrapSetJmp call.
 * This will have eax set to 1 and need only be restored when it triggers. */
BS3REGCTX g_Bs3TrapSetJmpCtx;

#endif /* ARCH_BITS == 16 */

