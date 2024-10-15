/* $Id: VBoxGuestR3LibStat.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, Statistics.
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
#include "VBoxGuestR3LibInternal.h"


/**
 * Query the current statistics update interval.
 *
 * @returns IPRT status code.
 * @param   pcMsInterval    Update interval in ms (out).
 */
VBGLR3DECL(int) VbglR3StatQueryInterval(PRTMSINTERVAL pcMsInterval)
{
    VMMDevGetStatisticsChangeRequest Req;

    vmmdevInitRequest(&Req.header, VMMDevReq_GetStatisticsChangeRequest);
    Req.eventAck = VMMDEV_EVENT_STATISTICS_INTERVAL_CHANGE_REQUEST;
    Req.u32StatInterval = 1;
    int rc = vbglR3GRPerform(&Req.header);
    if (RT_SUCCESS(rc))
    {
        *pcMsInterval = Req.u32StatInterval * 1000;
        if (*pcMsInterval / 1000 != Req.u32StatInterval)
            *pcMsInterval = ~(RTMSINTERVAL)0;
    }
    return rc;
}


/**
 * Report guest statistics.
 *
 * @returns IPRT status code.
 * @param   pReq        Request packet with statistics.
 */
VBGLR3DECL(int) VbglR3StatReport(VMMDevReportGuestStats *pReq)
{
    vmmdevInitRequest(&pReq->header, VMMDevReq_ReportGuestStats);
    return vbglR3GRPerform(&pReq->header);
}

