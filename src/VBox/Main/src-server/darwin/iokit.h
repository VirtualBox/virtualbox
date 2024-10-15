/* $Id: iokit.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - Darwin IOKit Routines.
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
 */

#ifndef MAIN_INCLUDED_SRC_src_server_darwin_iokit_h
#define MAIN_INCLUDED_SRC_src_server_darwin_iokit_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/cpp/ministring.h>
#ifdef VBOX_WITH_USB
# include <VBox/usb.h>
#endif

/**
 * Darwin DVD descriptor as returned by DarwinGetDVDDrives().
 */
typedef struct DARWINDVD
{
    /** Pointer to the next DVD. */
    struct DARWINDVD *pNext;
    /** Variable length name / identifier. */
    char szName[1];
} DARWINDVD;
/** Pointer to a Darwin DVD descriptor. */
typedef DARWINDVD *PDARWINDVD;

/** Darwin fixed drive (SSD, HDD, ++) descriptor as returned by
 *  DarwinGetFixedDrives(). */
typedef struct DARWINFIXEDDRIVE
{
    /** Pointer to the next DVD. */
    struct DARWINFIXEDDRIVE *pNext;
    /** Pointer to the model name, NULL if none.
     * This points after szName and needs not be freed separately. */
    const char *pszModel;
    /** Variable length name / identifier. */
    char szName[1];
} DARWINFIXEDDRIVE;
/** Pointer to a Darwin fixed drive. */
typedef DARWINFIXEDDRIVE *PDARWINFIXEDDRIVE;


/**
 * Darwin ethernet controller descriptor as returned by DarwinGetEthernetControllers().
 */
typedef struct DARWINETHERNIC
{
    /** Pointer to the next NIC. */
    struct DARWINETHERNIC *pNext;
    /** The BSD name. (like en0)*/
    char szBSDName[16];
    /** The fake unique identifier. */
    RTUUID Uuid;
    /** The MAC address. */
    RTMAC Mac;
    /** Whether it's wireless (true) or wired (false). */
    bool fWireless;
    /** Whether it is an AirPort device. */
    bool fAirPort;
    /** Whether it's built in or not. */
    bool fBuiltin;
    /** Whether it's a USB device or not. */
    bool fUSB;
    /** Whether it's the primary interface. */
    bool fPrimaryIf;
    /** A variable length descriptive name if possible. */
    char szName[1];
} DARWINETHERNIC;
/** Pointer to a Darwin ethernet controller descriptor.  */
typedef DARWINETHERNIC *PDARWINETHERNIC;


/** The run loop mode string used by iokit.cpp when it registers
 * notifications events. */
#define VBOX_IOKIT_MODE_STRING "VBoxIOKitMode"

RT_C_DECLS_BEGIN
#ifdef VBOX_WITH_USB
void *          DarwinSubscribeUSBNotifications(void);
void            DarwinUnsubscribeUSBNotifications(void *pvOpaque);
PUSBDEVICE      DarwinGetUSBDevices(void);
void            DarwinFreeUSBDeviceFromIOKit(PUSBDEVICE pCur);
int             DarwinReEnumerateUSBDevice(PCUSBDEVICE pCur);
#endif /* VBOX_WITH_USB */
PDARWINDVD      DarwinGetDVDDrives(void);
PDARWINFIXEDDRIVE DarwinGetFixedDrives(void);
PDARWINETHERNIC DarwinGetEthernetControllers(void);
RT_C_DECLS_END

#endif /* !MAIN_INCLUDED_SRC_src_server_darwin_iokit_h */
