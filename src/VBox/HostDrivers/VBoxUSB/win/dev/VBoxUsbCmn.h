/* $Id: VBoxUsbCmn.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxUsmCmn.h - USB device. Common defs
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

#ifndef VBOX_INCLUDED_SRC_VBoxUSB_win_dev_VBoxUsbCmn_h
#define VBOX_INCLUDED_SRC_VBoxUSB_win_dev_VBoxUsbCmn_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "../cmn/VBoxDrvTool.h"
#include "../cmn/VBoxUsbTool.h"

#include <iprt/cdefs.h>
#include <iprt/asm.h>

#include <VBox/usblib-win.h>

#define VBOXUSB_CFG_IDLE_TIME_MS 5000

typedef struct VBOXUSBDEV_EXT *PVBOXUSBDEV_EXT;

RT_C_DECLS_BEGIN

#ifdef _WIN64
#define DECLSPEC_USBIMPORT                      DECLSPEC_IMPORT
#else
#define DECLSPEC_USBIMPORT

#define USBD_ParseDescriptors                   _USBD_ParseDescriptors
#define USBD_ParseConfigurationDescriptorEx     _USBD_ParseConfigurationDescriptorEx
#define USBD_CreateConfigurationRequestEx       _USBD_CreateConfigurationRequestEx
#endif

DECLSPEC_USBIMPORT PUSB_COMMON_DESCRIPTOR
USBD_ParseDescriptors(
    IN PVOID DescriptorBuffer,
    IN ULONG TotalLength,
    IN PVOID StartPosition,
    IN LONG DescriptorType
    );

DECLSPEC_USBIMPORT PUSB_INTERFACE_DESCRIPTOR
USBD_ParseConfigurationDescriptorEx(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PVOID StartPosition,
    IN LONG InterfaceNumber,
    IN LONG AlternateSetting,
    IN LONG InterfaceClass,
    IN LONG InterfaceSubClass,
    IN LONG InterfaceProtocol
    );

DECLSPEC_USBIMPORT PURB
USBD_CreateConfigurationRequestEx(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSBD_INTERFACE_LIST_ENTRY InterfaceList
    );

RT_C_DECLS_END

DECLHIDDEN(PVOID) vboxUsbMemAlloc(SIZE_T cbBytes);
DECLHIDDEN(PVOID) vboxUsbMemAllocZ(SIZE_T cbBytes);
DECLHIDDEN(VOID) vboxUsbMemFree(PVOID pvMem);

#include "VBoxUsbRt.h"
#include "VBoxUsbPnP.h"
#include "VBoxUsbPwr.h"
#include "VBoxUsbDev.h"


#endif /* !VBOX_INCLUDED_SRC_VBoxUSB_win_dev_VBoxUsbCmn_h */
