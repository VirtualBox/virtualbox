/* $Id: vboxmouse.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox X11 Guest Additions, mouse driver for X.Org server 1.5
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
 * --------------------------------------------------------------------
 *
 * This code is based on evdev.c from X.Org with the following copyright
 * and permission notice:
 *
 * Copyright © 2004-2008 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of Red Hat
 * not be used in advertising or publicity pertaining to distribution
 * of the software without specific, written prior permission.  Red
 * Hat makes no representations about the suitability of this software
 * for any purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE AUTHORS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *      Kristian Høgsberg (krh@redhat.com)
 *      Adam Jackson (ajax@redhat.com)
 */

#include <VBox/VMMDev.h> /* for VMMDEV_MOUSE_XXX */
#include <VBox/VBoxGuestLib.h>
#include <iprt/errcore.h>
#include <xf86.h>
#include <xf86Xinput.h>
#include <mipointer.h>

#include <xf86Module.h>

#ifdef VBOX_GUESTR3XF86MOD
# define _X_EXPORT
#else
# include <errno.h>
# include <fcntl.h>
# include <unistd.h>
#endif

#include "product-generated.h"

static void
VBoxReadInput(InputInfoPtr pInfo)
{
    uint32_t cx, cy, fFeatures;

    /* Read a byte from the device to acknowledge the event */
    char c;
    int res = read(pInfo->fd, &c, 1);
    NOREF(res);
    /* The first test here is a workaround for an apparent bug in Xorg Server 1.5 */
    if (
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 2
           miPointerCurrentScreen() != NULL
#else
           miPointerGetScreen(pInfo->dev) != NULL
#endif
        &&  RT_SUCCESS(VbglR3GetMouseStatus(&fFeatures, &cx, &cy))
        && (fFeatures & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE))
    {
#if ABI_XINPUT_VERSION == SET_ABI_VERSION(2, 0)
        /* Bug in the 1.4 X server series - conversion_proc was no longer
         * called, but the server didn't yet do the conversion itself. */
        cx = (cx * screenInfo.screens[0]->width) / 65535;
        cy = (cy * screenInfo.screens[0]->height) / 65535;
#endif
        /* send absolute movement */
        xf86PostMotionEvent(pInfo->dev, 1, 0, 2, cx, cy);
    }
}

static void
VBoxPtrCtrlProc(DeviceIntPtr device, PtrCtrl *ctrl)
{
    /* Nothing to do, dix handles all settings */
    RT_NOREF(device, ctrl);
}

static int
VBoxInit(DeviceIntPtr device)
{
    CARD8 map[2] = { 0, 1 };
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 7
    Atom axis_labels[2] = { 0, 0 };
    Atom button_labels[2] = { 0, 0 };
#endif
    if (!InitPointerDeviceStruct((DevicePtr)device, map, 2,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 7
                                 button_labels,
#endif
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 2
                                 miPointerGetMotionEvents, VBoxPtrCtrlProc,
                                 miPointerGetMotionBufferSize()
#elif GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 3
                                 GetMotionHistory, VBoxPtrCtrlProc,
                                 GetMotionHistorySize(), 2 /* Number of axes */

#elif GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 3
                                 VBoxPtrCtrlProc, GetMotionHistorySize(),
                                 2 /* Number of axes */
#else
# error Unsupported version of X.Org
#endif
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 7
                                 , axis_labels
#endif
                                 ))
        return !Success;

    /* Tell the server about the range of axis values we report */
#if ABI_XINPUT_VERSION <= SET_ABI_VERSION(2, 0)
    xf86InitValuatorAxisStruct(device, 0, 0, -1, 1, 0, 1);
    xf86InitValuatorAxisStruct(device, 1, 0, -1, 1, 0, 1);
#else
    xf86InitValuatorAxisStruct(device, 0,
# if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 7
                               axis_labels[0],
# endif
                               VMMDEV_MOUSE_RANGE_MIN /* min X */, VMMDEV_MOUSE_RANGE_MAX /* max X */,
                               10000, 0, 10000
# if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 12
                               , Absolute
# endif
                               );

    xf86InitValuatorAxisStruct(device, 1,
# if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 7
                               axis_labels[1],
# endif
                               VMMDEV_MOUSE_RANGE_MIN /* min Y */, VMMDEV_MOUSE_RANGE_MAX /* max Y */,
                               10000, 0, 10000
# if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 12
                               , Absolute
# endif
                               );
#endif
    xf86InitValuatorDefaults(device, 0);
    xf86InitValuatorDefaults(device, 1);
    xf86MotionHistoryAllocate(device->public.devicePrivate);

    return Success;
}

static int
VBoxProc(DeviceIntPtr device, int what)
{
    InputInfoPtr pInfo;
    int rc, xrc;
    uint32_t fFeatures = 0;

    pInfo = device->public.devicePrivate;

    switch (what)
    {
    case DEVICE_INIT:
        xrc = VBoxInit(device);
        if (xrc != Success) {
            VbglR3Term();
            return xrc;
        }
        break;

    case DEVICE_ON:
        xf86Msg(X_INFO, "%s: On.\n", pInfo->name);
        if (device->public.on)
            break;
        /* Tell the host that we want absolute co-ordinates */
        rc = VbglR3GetMouseStatus(&fFeatures, NULL, NULL);
        fFeatures &= VMMDEV_MOUSE_GUEST_NEEDS_HOST_CURSOR;
        if (RT_SUCCESS(rc))
            rc = VbglR3SetMouseStatus(  fFeatures
                                      | VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE
                                      | VMMDEV_MOUSE_NEW_PROTOCOL);
        if (!RT_SUCCESS(rc)) {
            xf86Msg(X_ERROR, "%s: Failed to switch guest mouse into absolute mode\n",
                    pInfo->name);
            return !Success;
        }

        xf86AddEnabledDevice(pInfo);
        device->public.on = TRUE;
        break;

    case DEVICE_OFF:
        xf86Msg(X_INFO, "%s: Off.\n", pInfo->name);
        rc = VbglR3GetMouseStatus(&fFeatures, NULL, NULL);
        fFeatures &= VMMDEV_MOUSE_GUEST_NEEDS_HOST_CURSOR;
        if (RT_SUCCESS(rc))
            rc = VbglR3SetMouseStatus(  fFeatures
                                      & ~VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE
                                      & ~VMMDEV_MOUSE_NEW_PROTOCOL);
        xf86RemoveEnabledDevice(pInfo);
        device->public.on = FALSE;
        break;

    case DEVICE_CLOSE:
        VbglR3Term();
        xf86Msg(X_INFO, "%s: Close\n", pInfo->name);
        break;

    default:
        return BadValue;
    }

    return Success;
}

static int
VBoxProbe(InputInfoPtr pInfo)
{
    int rc = VbglR3Init();
    if (!RT_SUCCESS(rc)) {
        xf86Msg(X_ERROR, "%s: Failed to open the VirtualBox device (error %d)\n",
                pInfo->name, rc);
        return BadMatch;
    }

    return Success;
}

static Bool
VBoxConvert(InputInfoPtr pInfo, int first, int num, int v0, int v1, int v2,
            int v3, int v4, int v5, int *x, int *y)
{
    RT_NOREF(pInfo, num, v2, v3, v4, v5);

    if (first == 0) {
        *x = xf86ScaleAxis(v0, 0, screenInfo.screens[0]->width, 0, 65536);
        *y = xf86ScaleAxis(v1, 0, screenInfo.screens[0]->height, 0, 65536);
        return TRUE;
    }
    return FALSE;
}

static int
VBoxPreInitInfo(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
    const char *device;
    int rc;
    RT_NOREF(drv, flags);

    /* Initialise the InputInfoRec. */
    pInfo->device_control = VBoxProc;
    pInfo->read_input = VBoxReadInput;
    /* Unlike evdev, we set this unconditionally, as we don't handle keyboards. */
    pInfo->type_name = XI_MOUSE;
    pInfo->flags |= XI86_ALWAYS_CORE;

    device = xf86SetStrOption(pInfo->options, "Device",
                                "/dev/vboxguest");

    xf86Msg(X_CONFIG, "%s: Device: \"%s\"\n", pInfo->name, device);
    do {
        pInfo->fd = open(device, O_RDWR, 0);
    }
    while (pInfo->fd < 0 && errno == EINTR);

    if (pInfo->fd < 0) {
        xf86Msg(X_ERROR, "Unable to open VirtualBox device \"%s\".\n", device);
        return BadMatch;
    }

    rc = VBoxProbe(pInfo);
    if (rc != Success)
        return rc;

    return Success;
}

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 12
static InputInfoPtr
VBoxPreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    InputInfoPtr pInfo = xf86AllocateInput(drv, 0);
    if (!pInfo)
        return NULL;

    /* Initialise the InputInfoRec. */
    pInfo->name = dev->identifier;
    pInfo->conf_idev = dev;
    pInfo->conversion_proc = VBoxConvert;
    pInfo->flags = XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS;

    xf86CollectInputOptions(pInfo, NULL, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);

    if (VBoxPreInitInfo(drv, pInfo, flags) != Success) {
        xf86DeleteInput(pInfo, 0);
        return NULL;
    }

    pInfo->flags |= XI86_CONFIGURED;
    return pInfo;
}
#endif

_X_EXPORT InputDriverRec VBOXMOUSE = {
    1,
    "vboxmouse",
    NULL,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 12
    VBoxPreInit,
#else
    VBoxPreInitInfo,
#endif
    NULL,
    NULL,
    0
};

static pointer
VBoxPlug(pointer module, pointer options, int *errmaj, int *errmin)
{
    RT_NOREF(options, errmaj, errmin);
    xf86AddInputDriver(&VBOXMOUSE, module, 0);
    xf86Msg(X_CONFIG, "Load address of symbol \"VBOXMOUSE\" is %p\n",
            (void *)&VBOXMOUSE);
    return module;
}

static XF86ModuleVersionInfo VBoxVersionRec =
{
    "vboxmouse",
    VBOX_VENDOR,
    MODINFOSTRING1,
    MODINFOSTRING2,
    0, /* Missing from SDK: XORG_VERSION_CURRENT, */
    1, 0, 0,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData vboxmouseModuleData =
{
    &VBoxVersionRec,
    VBoxPlug,
    NULL
};
