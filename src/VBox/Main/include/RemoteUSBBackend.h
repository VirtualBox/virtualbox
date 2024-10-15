/* $Id: RemoteUSBBackend.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VirtualBox Remote USB backend
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

#ifndef MAIN_INCLUDED_RemoteUSBBackend_h
#define MAIN_INCLUDED_RemoteUSBBackend_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "RemoteUSBDeviceImpl.h"

#include <VBox/RemoteDesktop/VRDE.h>
#include <VBox/vrdpusb.h>

#include <iprt/critsect.h>

//typedef enum
//{
//    RDLIdle = 0,
//    RDLReqSent,
//    RDLObtained
//} RDLState;

class Console;
class ConsoleVRDPServer;

DECLCALLBACK(int) USBClientResponseCallback (void *pv, uint32_t u32ClientId, uint8_t code, const void *pvRet, uint32_t cbRet);


/* How many remote devices can be attached to a remote client.
 * Normally a client computer has 2-8 physical USB ports, so 16 devices
 * should be usually enough.
 */
#define VRDP_MAX_USB_DEVICES_PER_CLIENT (16)

class RemoteUSBBackendListable
{
    public:
        RemoteUSBBackendListable *pNext;
        RemoteUSBBackendListable *pPrev;

        RemoteUSBBackendListable() : pNext (NULL), pPrev (NULL) {};
};

class RemoteUSBBackend: public RemoteUSBBackendListable
{
    public:
        RemoteUSBBackend(Console *console, ConsoleVRDPServer *server, uint32_t u32ClientId);
        ~RemoteUSBBackend();

        uint32_t ClientId (void) { return mu32ClientId; }

        void AddRef (void);
        void Release (void);

        REMOTEUSBCALLBACK *GetBackendCallbackPointer (void) { return &mCallback; }

        void NotifyDelete (void);

        void PollRemoteDevices (void);

    public: /* Functions for internal use. */
        ConsoleVRDPServer *VRDPServer (void) { return mServer; };

        bool pollingEnabledURB (void) { return mfPollURB; }

        int saveDeviceList (const void *pvList, uint32_t cbList);

        int negotiateResponse (const VRDEUSBREQNEGOTIATERET *pret, uint32_t cbRet);

        int reapURB (const void *pvBody, uint32_t cbBody);

        void request (void);
        void release (void);

        PREMOTEUSBDEVICE deviceFromId (VRDEUSBDEVID id);

        void addDevice (PREMOTEUSBDEVICE pDevice);
        void removeDevice (PREMOTEUSBDEVICE pDevice);

        bool addUUID (const Guid *pUuid);
        bool findUUID (const Guid *pUuid);
        void removeUUID (const Guid *pUuid);

    private:
        Console *mConsole;
        ConsoleVRDPServer *mServer;

        int cRefs;

        uint32_t mu32ClientId;

        RTCRITSECT mCritsect;

        REMOTEUSBCALLBACK mCallback;

        bool mfHasDeviceList;

        void *mpvDeviceList;
        uint32_t mcbDeviceList;

        typedef enum {
            PollRemoteDevicesStatus_Negotiate,
            PollRemoteDevicesStatus_WaitNegotiateResponse,
            PollRemoteDevicesStatus_SendRequest,
            PollRemoteDevicesStatus_WaitResponse,
            PollRemoteDevicesStatus_Dereferenced
        } PollRemoteDevicesStatus;

        PollRemoteDevicesStatus menmPollRemoteDevicesStatus;

        bool mfPollURB;

        PREMOTEUSBDEVICE mpDevices;

        bool mfWillBeDeleted;

        Guid aGuids[VRDP_MAX_USB_DEVICES_PER_CLIENT];

        /* VRDP_USB_VERSION_2: the client version. */
        uint32_t mClientVersion;

        /* VRDP_USB_VERSION_3: the client sends VRDE_USB_REQ_DEVICE_LIST_EXT_RET. */
        bool mfDescExt;
};

#endif /* !MAIN_INCLUDED_RemoteUSBBackend_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
