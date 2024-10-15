/* $Id: the-os2-kernel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Ring-0 Driver, The OS/2 Kernel Headers.
 */

/*
 * Contributed by knut st. osmundsen.
 *
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
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef IPRT_INCLUDED_SRC_r0drv_os2_the_os2_kernel_h
#define IPRT_INCLUDED_SRC_r0drv_os2_the_os2_kernel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

#define INCL_ERRORS
#define INCL_DOSSEMAPHORES /* for SEM_INDEFINITE_WAIT */
#undef RT_MAX
#include <os2ddk/bsekee.h>
#include <os2ddk/devhlp.h>
#undef RT_MAX

RT_C_DECLS_BEGIN

extern PCDOSTABLE   g_pDosTable;
extern PCDOSTABLE2  g_pDosTable2;
extern PGINFOSEG    g_pGIS;
extern RTFAR16      g_fpLIS;

RTR0DECL(void *) RTR0Os2Virt2Flat(RTFAR16 fp);
DECLASM(int) RTR0Os2DHQueryDOSVar(uint8_t iVar, uint16_t iSub, PRTFAR16 pfp);
DECLASM(int) RTR0Os2DHVMGlobalToProcess(ULONG fFlags, PVOID pvR0, ULONG cb, PPVOID ppvR3);

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_SRC_r0drv_os2_the_os2_kernel_h */
