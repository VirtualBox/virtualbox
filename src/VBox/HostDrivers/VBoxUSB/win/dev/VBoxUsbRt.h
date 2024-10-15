/* $Id: VBoxUsbRt.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox USB R0 runtime
 */
/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_VBoxUSB_win_dev_VBoxUsbRt_h
#define VBOX_INCLUDED_SRC_VBoxUSB_win_dev_VBoxUsbRt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxUsbCmn.h"
#include "../cmn/VBoxUsbIdc.h"

#define VBOXUSBRT_MAX_CFGS 4

typedef struct VBOXUSB_PIPE_INFO {
    UCHAR       EndpointAddress;
    ULONG       NextScheduledFrame;
} VBOXUSB_PIPE_INFO;

typedef struct VBOXUSB_IFACE_INFO {
    USBD_INTERFACE_INFORMATION      *pInterfaceInfo;
    VBOXUSB_PIPE_INFO               *pPipeInfo;
} VBOXUSB_IFACE_INFO;

typedef struct VBOXUSB_RT
{
    UNICODE_STRING                  IfName;

    HANDLE                          hPipe0;
    HANDLE                          hConfiguration;
    uint32_t                        uConfigValue;

    uint32_t                        uNumInterfaces;
    USB_DEVICE_DESCRIPTOR           *devdescr;
    USB_CONFIGURATION_DESCRIPTOR    *cfgdescr[VBOXUSBRT_MAX_CFGS];

    VBOXUSB_IFACE_INFO              *pVBIfaceInfo;

    uint16_t                        idVendor, idProduct, bcdDevice;
    char                            szSerial[MAX_USB_SERIAL_STRING];
    BOOLEAN                         fIsHighSpeed;

    HVBOXUSBIDCDEV                  hMonDev;
    PFILE_OBJECT                    pOwner;
} VBOXUSB_RT, *PVBOXUSB_RT;

typedef struct VBOXUSBRT_IDC
{
    PDEVICE_OBJECT pDevice;
    PFILE_OBJECT pFile;
} VBOXUSBRT_IDC, *PVBOXUSBRT_IDC;

DECLHIDDEN(NTSTATUS) vboxUsbRtGlobalsInit();
DECLHIDDEN(VOID) vboxUsbRtGlobalsTerm();

DECLHIDDEN(NTSTATUS) vboxUsbRtInit(PVBOXUSBDEV_EXT pDevExt);
DECLHIDDEN(VOID) vboxUsbRtClear(PVBOXUSBDEV_EXT pDevExt);
DECLHIDDEN(NTSTATUS) vboxUsbRtRm(PVBOXUSBDEV_EXT pDevExt);
DECLHIDDEN(NTSTATUS) vboxUsbRtStart(PVBOXUSBDEV_EXT pDevExt);

DECLHIDDEN(NTSTATUS) vboxUsbRtDispatch(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp);
DECLHIDDEN(NTSTATUS) vboxUsbRtCreate(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp);
DECLHIDDEN(NTSTATUS) vboxUsbRtClose(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp);

#endif /* !VBOX_INCLUDED_SRC_VBoxUSB_win_dev_VBoxUsbRt_h */
