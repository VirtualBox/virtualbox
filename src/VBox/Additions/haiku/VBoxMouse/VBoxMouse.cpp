/* $Id: VBoxMouse.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxMouse; input_server add-on - Haiku Guest Additions, implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

/*
 * This code is based on:
 *
 * VirtualBox Guest Additions for Haiku.
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    François Revol <revol@free.fr>
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <Clipboard.h>
#include <Debug.h>
#include <Message.h>
#include <String.h>

#include "VBoxMouse.h"
#include <VBox/VBoxGuest.h> /** @todo use the VbglR3 interface! */
#include <VBox/VBoxGuestLib.h>
#include <VBoxGuestInternal.h>
#include <VBox/VMMDev.h>
#include <VBox/log.h>
#include <iprt/errcore.h>

/* Export as global symbol with C linkage, RTDECL is necessary. */
RTDECL(BInputServerDevice *)
instantiate_input_device()
{
    return new VBoxMouse();
}


static inline int vboxMouseAcquire()
{
    uint32_t fFeatures = 0;
    int rc = VbglR3GetMouseStatus(&fFeatures, NULL, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = VbglR3SetMouseStatus(fFeatures | VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE | VMMDEV_MOUSE_NEW_PROTOCOL);
        if (RT_FAILURE(rc))
            LogRel(("VbglR3SetMouseStatus failed. rc=%d\n", rc));
    }
    else
        LogRel(("VbglR3GetMouseStatus failed. rc=%d\n", rc));
    return rc;
}


static inline int vboxMouseRelease()
{
    uint32_t fFeatures = 0;
    int rc = VbglR3GetMouseStatus(&fFeatures, NULL, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = VbglR3SetMouseStatus(fFeatures & ~VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE & ~VMMDEV_MOUSE_NEW_PROTOCOL);
        if (RT_FAILURE(rc))
            LogRel(("VbglR3SetMouseStatus failed. rc=%d\n", rc));
    }
    else
        LogRel(("VbglR3GetMouseStatus failed. rc=%d\n", rc));
    return rc;
}


VBoxMouse::VBoxMouse()
     : BInputServerDevice(),
       fDriverFD(-1),
       fServiceThreadID(-1),
       fExiting(false)
{
}


VBoxMouse::~VBoxMouse()
{
}


status_t VBoxMouse::InitCheck()
{
    int rc = VbglR3Init();
    if (!RT_SUCCESS(rc))
        return ENXIO;

    input_device_ref device = { (char *)"VBoxMouse", B_POINTING_DEVICE, (void *)this };
    input_device_ref *deviceList[2] = { &device, NULL };
    RegisterDevices(deviceList);

    return B_OK;
}


status_t VBoxMouse::SystemShuttingDown()
{
    VbglR3Term();

    return B_OK;
}


status_t VBoxMouse::Start(const char *device, void *cookie)
{
#if 0
    status_t err;
    int rc;
    uint32_t fFeatures = 0;
    Log(("VBoxMouse::%s()\n", __FUNCTION__));

    rc = VbglR3GetMouseStatus(&fFeatures, NULL, NULL);
    if (RT_SUCCESS(rc))
        rc = VbglR3SetMouseStatus(fFeatures
                                  | VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE
                                  | VMMDEV_MOUSE_NEW_PROTOCOL);
    if (!RT_SUCCESS(rc))
    {
        LogRel(("VBoxMouse: Error switching guest mouse into absolute mode: %d\n", rc));
        return B_DEVICE_NOT_FOUND;
    }

    err = fServiceThreadID = spawn_thread(_ServiceThreadNub,
                                          "VBoxMouse", B_NORMAL_PRIORITY, this);
    if (err >= B_OK)
    {
        resume_thread(fServiceThreadID);
        return B_OK;
    }
    else
        LogRel(("VBoxMouse: Error starting service thread: 0x%08lx\n",
                err));

    // release the mouse
    rc = VbglR3GetMouseStatus(&fFeatures, NULL, NULL);
    if (RT_SUCCESS(rc))
        rc = VbglR3SetMouseStatus(fFeatures
                                  & ~VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE
                                  & ~VMMDEV_MOUSE_NEW_PROTOCOL);

    return B_ERROR;
#endif

    status_t err = B_OK;
    int rc;
    uint32_t fFeatures = 0;
    LogFlowFunc(("device=%s cookie=%p\n", device, cookie));

    rc = vboxMouseAcquire();
    if (RT_SUCCESS(rc))
    {
        err = fServiceThreadID = spawn_thread(_ServiceThreadNub, "VBoxMouse", B_NORMAL_PRIORITY, this);
        if (err >= B_OK)
        {
            resume_thread(fServiceThreadID);
            return B_OK;
        }
        else
            LogRel(("VBoxMouse::Start Error starting service thread: 0x%08lx\n", err));

        vboxMouseRelease();
        err = B_ERROR;
    }
    else
    {
        LogRel(("VBoxMouse::Start vboxMouseAcquire failed. rc=%d\n", rc));
        err = B_DEVICE_NOT_FOUND;
    }

    return err;
}


status_t VBoxMouse::Stop(const char *device, void *cookie)
{
    status_t status;
    int rc;
    uint32_t fFeatures = 0;
    Log(("VBoxMouse::%s()\n", __FUNCTION__));

    fExiting = true;

    vboxMouseRelease();

    close(fDriverFD);
    fDriverFD = -1;
    //XXX WTF ?
    suspend_thread(fServiceThreadID);
    resume_thread(fServiceThreadID);
    wait_for_thread(fServiceThreadID, &status);
    fServiceThreadID = -1;
    fExiting = false;
    return B_OK;
}


status_t VBoxMouse::Control(const char *device, void *cookie, uint32 code, BMessage *message)
{
    switch (code)
    {
        case B_MOUSE_SPEED_CHANGED:
        case B_CLICK_SPEED_CHANGED:
        case B_MOUSE_ACCELERATION_CHANGED:
        default:
            return BInputServerDevice::Control(device, cookie, code, message);
    }
    return B_OK;
}


status_t VBoxMouse::_ServiceThreadNub(void *_this)
{
    VBoxMouse *service = (VBoxMouse *)_this;
    return service->_ServiceThread();
}


status_t VBoxMouse::_ServiceThread()
{
    Log(("VBoxMouse::%s()\n", __FUNCTION__));

    fDriverFD = open(VBOXGUEST_DEVICE_NAME, O_RDWR);
    if (fDriverFD < 0)
        return ENXIO;

    /* The thread waits for incoming messages from the host. */
    while (!fExiting)
    {
        uint32_t cx, cy, fFeatures;
        int rc;

        fd_set readSet, writeSet, errorSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        FD_SET(fDriverFD, &readSet);
        if (fDriverFD < 0)
            break;
        rc = select(fDriverFD + 1, &readSet, &writeSet, &errorSet, NULL);
        if (rc < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            break;
        }

        rc = VbglR3GetMouseStatus(&fFeatures, &cx, &cy);
        if (   RT_SUCCESS(rc)
            && (fFeatures & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE))
        {
            float x = cx * 1.0 / 65535;
            float y = cy * 1.0 / 65535;

            _debugPrintf("VBoxMouse: at %d,%d %f,%f\n", cx, cy, x, y);

            /* Send absolute movement */
            bigtime_t now = system_time();
            BMessage *event = new BMessage(B_MOUSE_MOVED);
            event->AddInt64("when", now);
            event->AddFloat("x", x);
            event->AddFloat("y", y);
            event->AddFloat("be:tablet_x", x);
            event->AddFloat("be:tablet_y", y);
            //event->PrintToStream();
            EnqueueMessage(event);

            //LogRelFlow(("processed host event rc = %d\n", rc));
        }
    }
    return 0;
}

