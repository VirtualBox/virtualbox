/* $Id: RTTimerCreate-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Timers, Generic RTTimerCreate() Implementation.
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
#include <iprt/timer.h>
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include <iprt/assert.h>


RTDECL(int) RTTimerCreate(PRTTIMER *ppTimer, unsigned uMilliesInterval, PFNRTTIMER pfnTimer, void *pvUser)
{
    int rc = RTTimerCreateEx(ppTimer, uMilliesInterval * RT_NS_1MS_64, 0 /* fFlags */, pfnTimer, pvUser);
    if (RT_SUCCESS(rc))
    {
        rc = RTTimerStart(*ppTimer, 0 /* u64First */);
        if (RT_FAILURE(rc))
        {
            int rc2 = RTTimerDestroy(*ppTimer); AssertRC(rc2);
            *ppTimer = NULL;
        }
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTTimerCreate);

