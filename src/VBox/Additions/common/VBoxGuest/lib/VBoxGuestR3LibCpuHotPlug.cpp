/* $Id: VBoxGuestR3LibCpuHotPlug.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, CPU Hot Plugging.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include "VBoxGuestR3LibInternal.h"
#include <iprt/assert.h>
#include <iprt/errcore.h>


/**
 * Initialize CPU hot plugging.
 *
 * This will enable the CPU hot plugging events.
 *
 * @returns VBox status code.
 */
VBGLR3DECL(int) VbglR3CpuHotPlugInit(void)
{
    int rc = VbglR3CtlFilterMask(VMMDEV_EVENT_CPU_HOTPLUG, 0);
    if (RT_FAILURE(rc))
        return rc;

    VMMDevCpuHotPlugStatusRequest Req;
    vmmdevInitRequest(&Req.header, VMMDevReq_SetCpuHotPlugStatus);
    Req.enmStatusType = VMMDevCpuStatusType_Enable;
    rc = vbglR3GRPerform(&Req.header);
    if (RT_FAILURE(rc))
        VbglR3CtlFilterMask(0, VMMDEV_EVENT_CPU_HOTPLUG);

    return rc;
}


/**
 * Terminate CPU hot plugging.
 *
 * This will disable the CPU hot plugging events.
 *
 * @returns VBox status code.
 */
VBGLR3DECL(int) VbglR3CpuHotPlugTerm(void)
{
    /* Clear the events. */
    VbglR3CtlFilterMask(0, VMMDEV_EVENT_CPU_HOTPLUG);

    VMMDevCpuHotPlugStatusRequest Req;
    vmmdevInitRequest(&Req.header, VMMDevReq_SetCpuHotPlugStatus);
    Req.enmStatusType = VMMDevCpuStatusType_Disable;
    return vbglR3GRPerform(&Req.header);
}


/**
 * Waits for a CPU hot plugging event and retrieve the data associated with it.
 *
 * @returns VBox status code.
 * @param   penmEventType   Where to store the event type on success.
 * @param   pidCpuCore      Where to store the CPU core ID on success.
 * @param   pidCpuPackage   Where to store the CPU package ID on success.
 */
VBGLR3DECL(int) VbglR3CpuHotPlugWaitForEvent(VMMDevCpuEventType *penmEventType, uint32_t *pidCpuCore, uint32_t *pidCpuPackage)
{
    AssertPtrReturn(penmEventType, VERR_INVALID_POINTER);
    AssertPtrReturn(pidCpuCore, VERR_INVALID_POINTER);
    AssertPtrReturn(pidCpuPackage, VERR_INVALID_POINTER);

    uint32_t fEvents = 0;
    int rc = VbglR3WaitEvent(VMMDEV_EVENT_CPU_HOTPLUG, RT_INDEFINITE_WAIT, &fEvents);
    if (RT_SUCCESS(rc))
    {
        /* did we get the right event? */
        if (fEvents & VMMDEV_EVENT_CPU_HOTPLUG)
        {
            VMMDevGetCpuHotPlugRequest Req;

            /* get the CPU hot plugging request */
            vmmdevInitRequest(&Req.header, VMMDevReq_GetCpuHotPlugRequest);
            Req.idCpuCore    = UINT32_MAX;
            Req.idCpuPackage = UINT32_MAX;
            Req.enmEventType = VMMDevCpuEventType_None;
            rc = vbglR3GRPerform(&Req.header);
            if (RT_SUCCESS(rc))
            {
                *penmEventType = Req.enmEventType;
                *pidCpuCore    = Req.idCpuCore;
                *pidCpuPackage = Req.idCpuPackage;
                return VINF_SUCCESS;
            }
        }
        else
            rc = VERR_TRY_AGAIN;
    }
    else if (rc == VERR_TIMEOUT) /* just in case */
        rc = VERR_TRY_AGAIN;
    return rc;
}

