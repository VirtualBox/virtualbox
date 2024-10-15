/* $Id: SrvIntNetWrapper.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Internal networking - Wrapper for the R0 network service.
 *
 * This is a bit hackish as we're mixing context here, however it is
 * very useful when making changes to the internal networking service.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "IntNetSwitchInternal.h"

#include <iprt/asm.h>
#include <iprt/mp.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/* Fake non-existing ring-0 APIs. */
#define RTThreadIsInInterrupt(hThread)      false
#define RTThreadPreemptIsEnabled(hThread)   true
#define RTMpCpuId()                         0

/* No CLI/POPF, please. */
#include <iprt/spinlock.h>
#undef  RTSPINLOCK_FLAGS_INTERRUPT_SAFE
#define RTSPINLOCK_FLAGS_INTERRUPT_SAFE     RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE


/* ugly but necessary for making R0 code compilable for R3. */
#undef LOG_GROUP
#include "../../Devices/Network/SrvIntNetR0.cpp"
