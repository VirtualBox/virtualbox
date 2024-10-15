/* $Id: VBoxGuestR3LibVrdp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, VRDP.
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
#include <iprt/time.h>
#include <iprt/string.h>
#include "VBoxGuestR3LibInternal.h"


VBGLR3DECL(int) VbglR3VrdpGetChangeRequest(bool *pfActive, uint32_t *puExperienceLevel)
{
    VMMDevVRDPChangeRequest Req;
    RT_ZERO(Req); /* implicit padding */
    vmmdevInitRequest(&Req.header, VMMDevReq_GetVRDPChangeRequest); //VMMDEV_REQ_HDR_INIT(&Req.header, sizeof(Req), VMMDevReq_GetVRDPChangeRequest);
    int rc = vbglR3GRPerform(&Req.header);
    if (RT_SUCCESS(rc))
    {
        *pfActive          = Req.u8VRDPActive != 0;
        *puExperienceLevel = Req.u32VRDPExperienceLevel;
    }
    else
    {
        *pfActive          = false;
        *puExperienceLevel = 0;
    }
    return rc;
}

