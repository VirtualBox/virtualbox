/* $Id: VBoxUsbIdc.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Windows USB Proxy - Monitor Driver communication interface.
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

#ifndef VBOX_INCLUDED_SRC_VBoxUSB_win_cmn_VBoxUsbIdc_h
#define VBOX_INCLUDED_SRC_VBoxUSB_win_cmn_VBoxUsbIdc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define VBOXUSBIDC_VERSION_MAJOR 1
#define VBOXUSBIDC_VERSION_MINOR 0

#define VBOXUSBIDC_INTERNAL_IOCTL_GET_VERSION         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x618, METHOD_NEITHER, FILE_WRITE_ACCESS)
#define VBOXUSBIDC_INTERNAL_IOCTL_PROXY_STARTUP       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x619, METHOD_NEITHER, FILE_WRITE_ACCESS)
#define VBOXUSBIDC_INTERNAL_IOCTL_PROXY_TEARDOWN      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x61A, METHOD_NEITHER, FILE_WRITE_ACCESS)
#define VBOXUSBIDC_INTERNAL_IOCTL_PROXY_STATE_CHANGE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x61B, METHOD_NEITHER, FILE_WRITE_ACCESS)

typedef struct
{
    uint32_t        u32Major;
    uint32_t        u32Minor;
} VBOXUSBIDC_VERSION, *PVBOXUSBIDC_VERSION;

typedef void *HVBOXUSBIDCDEV;

/* the initial device state is USBDEVICESTATE_HELD_BY_PROXY */
typedef struct VBOXUSBIDC_PROXY_STARTUP
{
    union
    {
        /* in: device PDO */
        PDEVICE_OBJECT pPDO;
        /* out: device handle to be used for subsequent USBSUP_PROXY_XXX calls */
        HVBOXUSBIDCDEV hDev;
    } u;
} VBOXUSBIDC_PROXY_STARTUP, *PVBOXUSBIDC_PROXY_STARTUP;

typedef struct VBOXUSBIDC_PROXY_TEARDOWN
{
    HVBOXUSBIDCDEV hDev;
} VBOXUSBIDC_PROXY_TEARDOWN, *PVBOXUSBIDC_PROXY_TEARDOWN;

typedef enum
{
    VBOXUSBIDC_PROXY_STATE_UNKNOWN = 0,
    VBOXUSBIDC_PROXY_STATE_IDLE,
    VBOXUSBIDC_PROXY_STATE_INITIAL = VBOXUSBIDC_PROXY_STATE_IDLE,
    VBOXUSBIDC_PROXY_STATE_USED_BY_GUEST
} VBOXUSBIDC_PROXY_STATE;

typedef struct VBOXUSBIDC_PROXY_STATE_CHANGE
{
    HVBOXUSBIDCDEV hDev;
    VBOXUSBIDC_PROXY_STATE enmState;
} VBOXUSBIDC_PROXY_STATE_CHANGE, *PVBOXUSBIDC_PROXY_STATE_CHANGE;

NTSTATUS VBoxUsbIdcInit();
VOID VBoxUsbIdcTerm();
NTSTATUS VBoxUsbIdcProxyStarted(PDEVICE_OBJECT pPDO, HVBOXUSBIDCDEV *phDev);
NTSTATUS VBoxUsbIdcProxyStopped(HVBOXUSBIDCDEV hDev);

#endif /* !VBOX_INCLUDED_SRC_VBoxUSB_win_cmn_VBoxUsbIdc_h */
