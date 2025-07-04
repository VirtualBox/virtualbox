/* $Id: init-data.cpp 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Init Data Ring-3.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include "internal/initterm.h"
#include "internal/time.h"      /* g_u64ProgramStartNanoTS */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The number of calls to RTR3Init*. */
DECL_HIDDEN_DATA(int32_t volatile)  g_crtR3Users        = 0;
/** Whether we're currently initializing the IPRT. */
DECL_HIDDEN_DATA(bool volatile)     g_frtR3Initializing = false;
/**
 * Set if the atexit callback has been called, i.e. indicating
 * that the process is terminating.
 */
DECL_HIDDEN_DATA(bool volatile)     g_frtAtExitCalled   = false;

/**
 * Program start nanosecond TS.
 */
DECL_HIDDEN_DATA(uint64_t)          g_u64ProgramStartNanoTS = 0;

