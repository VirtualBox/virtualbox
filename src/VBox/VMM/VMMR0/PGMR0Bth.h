/* $Id: PGMR0Bth.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox - Page Manager / Monitor, Shadow+Guest Paging Template.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
RT_C_DECLS_BEGIN
PGM_BTH_DECL(int, Trap0eHandler)(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx, RTGCPTR pvFault, bool *pfLockTaken);
PGM_BTH_DECL(int, NestedTrap0eHandler)(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx, RTGCPHYS GCPhysNested,
                                       bool fIsLinearAddrValid, RTGCPTR GCPtrNested, PPGMPTWALK pWalk, bool *pfLockTaken);
RT_C_DECLS_END

