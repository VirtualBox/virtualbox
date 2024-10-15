/* $Id: VBoxGuestR3LibSeamless.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, Seamless mode.
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
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/string.h>

#include <VBox/log.h>

#include "VBoxGuestR3LibInternal.h"

#ifdef VBOX_VBGLR3_XFREE86
/* Rather than try to resolve all the header file conflicts, I will just
   prototype what we need here. */
extern "C" void* xf86memcpy(void*,const void*,xf86size_t);
# undef memcpy
# define memcpy xf86memcpy
#endif /* VBOX_VBGLR3_XFREE86 */

/**
 * Tell the host that we support (or no longer support) seamless mode.
 *
 * @returns IPRT status value
 * @param   fState whether or not we support seamless mode
 */
VBGLR3DECL(int) VbglR3SeamlessSetCap(bool fState)
{
    if (fState)
        return VbglR3SetGuestCaps(VMMDEV_GUEST_SUPPORTS_SEAMLESS, 0);
    return VbglR3SetGuestCaps(0, VMMDEV_GUEST_SUPPORTS_SEAMLESS);
}

/**
 * Wait for a seamless mode change event.
 *
 * @returns IPRT status value.
 * @param[out] pMode    On success, the seamless mode to switch into (i.e.
 *                      disabled, visible region or host window).
 */
VBGLR3DECL(int) VbglR3SeamlessWaitEvent(VMMDevSeamlessMode *pMode)
{
    AssertPtrReturn(pMode, VERR_INVALID_POINTER);

    /** @todo r=andy The (similar / duplicate) Windows code does similar waiting. Merge / fix this. */
    uint32_t fEvent = 0;
    int rc = VbglR3WaitEvent(VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST, 1000 /* ms */, &fEvent);
    if (RT_SUCCESS(rc))
    {
        /* did we get the right event? */
        if (fEvent & VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST)
        {
            VMMDevSeamlessChangeRequest seamlessChangeRequest;

            /* get the seamless change request */
            vmmdevInitRequest(&seamlessChangeRequest.header, VMMDevReq_GetSeamlessChangeRequest);
            seamlessChangeRequest.mode = (VMMDevSeamlessMode)-1;
            seamlessChangeRequest.eventAck = VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST;
            rc = vbglR3GRPerform(&seamlessChangeRequest.header);
            if (RT_SUCCESS(rc))
            {
                *pMode = seamlessChangeRequest.mode;
                return VINF_SUCCESS;
            }
        }
        else
            rc = VERR_TRY_AGAIN;
    }
    else if (   rc == VERR_INTERRUPTED
             || rc == VERR_TIMEOUT /* just in case */)
        rc = VERR_TRY_AGAIN;
    return rc;
}

/**
 * Request the last seamless mode switch from the host again.
 *
 * @returns IPRT status value.
 * @param[out] pMode    On success, the seamless mode that was switched
 *                      into (i.e. disabled, visible region or host window).
 */
VBGLR3DECL(int) VbglR3SeamlessGetLastEvent(VMMDevSeamlessMode *pMode)
{
    VMMDevSeamlessChangeRequest seamlessChangeRequest;
    int rc;

    AssertPtrReturn(pMode, VERR_INVALID_PARAMETER);

    /* get the seamless change request */
    vmmdevInitRequest(&seamlessChangeRequest.header, VMMDevReq_GetSeamlessChangeRequest);
    seamlessChangeRequest.mode = (VMMDevSeamlessMode)-1;
    seamlessChangeRequest.eventAck = VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST;
    rc = vbglR3GRPerform(&seamlessChangeRequest.header);
    if (RT_SUCCESS(rc))
    {
        *pMode = seamlessChangeRequest.mode;
        return VINF_SUCCESS;
    }
    return rc;
}

/**
 * Inform the host about the visible region
 *
 * @returns IPRT status code
 * @param   cRects number of rectangles in the list of visible rectangles
 * @param   pRects list of visible rectangles on the guest display
 *
 * @todo A scatter-gather version of vbglR3GRPerform would be nice, so that we don't have
 *       to copy our rectangle and header data into a single structure and perform an
 *       additional allocation.
 * @todo Would that really gain us much, given that the rectangles may not
 *       be grouped at all, or in the format we need?  Keeping the memory
 *       for our "single structure" around (re-alloc-ing it if necessary)
 *       sounds like a simpler optimisation if we need it.
 */
VBGLR3DECL(int) VbglR3SeamlessSendRects(uint32_t cRects, PRTRECT pRects)
{
    VMMDevVideoSetVisibleRegion *pReq;
    int rc;

    AssertReturn(pRects || cRects == 0, VERR_INVALID_PARAMETER);
    AssertMsgReturn(cRects <= _1M, ("%u\n", cRects), VERR_OUT_OF_RANGE);

    rc = vbglR3GRAlloc((VMMDevRequestHeader **)&pReq,
                         sizeof(VMMDevVideoSetVisibleRegion)
                       + cRects * sizeof(RTRECT)
                       - sizeof(RTRECT),
                       VMMDevReq_VideoSetVisibleRegion);
    if (RT_SUCCESS(rc))
    {
        pReq->cRect = cRects;
        if (cRects)
            memcpy(&pReq->Rect, pRects, cRects * sizeof(RTRECT));
        /* This will fail harmlessly for cRect == 0 and older host code */
        rc = vbglR3GRPerform(&pReq->header);
        LogFunc(("Visible region request returned %Rrc, internal %Rrc.\n",
                 rc, pReq->header.rc));
        if (RT_SUCCESS(rc))
            rc = pReq->header.rc;
        vbglR3GRFree(&pReq->header);
    }
    LogFunc(("Sending %u rectangles to the host: %Rrc\n", cRects, rc));
    return rc;
}

VBGLR3DECL(int) VbglR3SeamlessSendMonitorPositions(uint32_t cPositions, PRTPOINT pPositions)
{
    if (!pPositions || cPositions <= 0)
        return VERR_INVALID_PARAMETER;

    VMMDevVideoUpdateMonitorPositions *pReq;
    int rc;

    rc = vbglR3GRAlloc((VMMDevRequestHeader **)&pReq,
                         sizeof(VMMDevVideoUpdateMonitorPositions)
                       + (cPositions - 1) * sizeof(RTPOINT),
                       VMMDevReq_VideoUpdateMonitorPositions);
    if (RT_SUCCESS(rc))
    {
        pReq->cPositions = cPositions;
        if (cPositions)
            memcpy(&pReq->aPositions, pPositions, cPositions * sizeof(RTPOINT));
        rc = vbglR3GRPerform(&pReq->header);
        LogFunc(("Monitor position update request returned %Rrc, internal %Rrc.\n",
                 rc, pReq->header.rc));
        if (RT_SUCCESS(rc))
            rc = pReq->header.rc;
        vbglR3GRFree(&pReq->header);
    }
    LogFunc(("Sending monitor positions (%u of them)  to the host: %Rrc\n", cPositions, rc));
    return rc;
}
