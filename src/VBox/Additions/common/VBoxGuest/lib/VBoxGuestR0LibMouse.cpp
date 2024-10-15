/* $Id: VBoxGuestR0LibMouse.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestLibR0 - Mouse Integration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "VBoxGuestR0LibInternal.h"
#ifdef VBGL_VBOXGUEST
# error "This file shouldn't be part of the VBoxGuestR0LibBase library that is linked into VBoxGuest.  It's client code."
#endif


/**
 * Sets the function which is called back on each mouse pointer event.  Only
 * one callback can be active at once, so if you need several for any reason
 * you must multiplex yourself.  Call backs can be disabled by passing NULL
 * as the function pointer.
 *
 * @remarks Ring-0.
 * @returns iprt status code.
 * @returns VERR_TRY_AGAIN if the main guest driver hasn't finished
 *          initialising.
 *
 * @param   pfnNotify  the function to call back.  NULL to disable call backs.
 * @param   pvUser     user supplied data/cookie to be passed to the function.
 */
DECLR0VBGL(int) VbglR0SetMouseNotifyCallback(PFNVBOXGUESTMOUSENOTIFY pfnNotify, void *pvUser)
{
    PVBGLIDCHANDLE pIdcHandle;
    int rc = vbglR0QueryIdcHandle(&pIdcHandle);
    if (RT_SUCCESS(rc))
    {
        VBGLIOCSETMOUSENOTIFYCALLBACK NotifyCallback;
        VBGLREQHDR_INIT(&NotifyCallback.Hdr, SET_MOUSE_NOTIFY_CALLBACK);
        NotifyCallback.u.In.pfnNotify = pfnNotify;
        NotifyCallback.u.In.pvUser    = pvUser;
        rc = VbglR0IdcCall(pIdcHandle, VBGL_IOCTL_SET_MOUSE_NOTIFY_CALLBACK, &NotifyCallback.Hdr, sizeof(NotifyCallback));
    }
    return rc;
}


/**
 * Retrieve mouse coordinates and features from the host.
 *
 * @remarks Ring-0.
 * @returns VBox status code.
 *
 * @param   pfFeatures  Where to store the mouse features.
 * @param   px          Where to store the X co-ordinate.
 * @param   py          Where to store the Y co-ordinate.
 */
DECLR0VBGL(int) VbglR0GetMouseStatus(uint32_t *pfFeatures, uint32_t *px, uint32_t *py)
{
    PVBGLIDCHANDLE pIdcHandle;
    int rc = vbglR0QueryIdcHandle(&pIdcHandle);
    if (RT_SUCCESS(rc))
    {
        VMMDevReqMouseStatus Req;
        VMMDEV_REQ_HDR_INIT(&Req.header, sizeof(Req), VMMDevReq_GetMouseStatus);
        Req.mouseFeatures = 0;
        Req.pointerXPos = 0;
        Req.pointerYPos = 0;
        rc = VbglR0IdcCall(pIdcHandle, VBGL_IOCTL_VMMDEV_REQUEST(sizeof(Req)), (PVBGLREQHDR)&Req.header, sizeof(Req));
        if (RT_SUCCESS(rc))
        {
            if (pfFeatures)
                *pfFeatures = Req.mouseFeatures;
            if (px)
                *px = Req.pointerXPos;
            if (py)
                *py = Req.pointerYPos;
        }
    }
    return rc;
}


/**
 * Send mouse features to the host.
 *
 * @remarks Ring-0.
 * @returns VBox status code.
 *
 * @param   fFeatures  Supported mouse pointer features.  The main guest driver
 *                     will mediate different callers and show the host any
 *                     feature enabled by any guest caller.
 */
DECLR0VBGL(int) VbglR0SetMouseStatus(uint32_t fFeatures)
{
    PVBGLIDCHANDLE pIdcHandle;
    int rc = vbglR0QueryIdcHandle(&pIdcHandle);
    if (RT_SUCCESS(rc))
    {
        VBGLIOCSETMOUSESTATUS Req;
        VBGLREQHDR_INIT(&Req.Hdr, SET_MOUSE_STATUS);
        Req.u.In.fStatus = fFeatures;
        rc = VbglR0IdcCall(pIdcHandle, VBGL_IOCTL_SET_MOUSE_STATUS, &Req.Hdr, sizeof(Req));
    }
    return rc;
}

