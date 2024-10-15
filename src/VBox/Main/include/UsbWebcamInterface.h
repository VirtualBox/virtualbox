/* $Id: UsbWebcamInterface.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox PDM Driver for Emulated USB Webcam
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MAIN_INCLUDED_UsbWebcamInterface_h
#define MAIN_INCLUDED_UsbWebcamInterface_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/pdmdrv.h>
#define VRDE_VIDEOIN_WITH_VRDEINTERFACE /* Get the VRDE interface definitions. */
#include <VBox/RemoteDesktop/VRDEVideoIn.h>

class ConsoleVRDPServer;
typedef struct EMWEBCAMDRV EMWEBCAMDRV;
typedef struct EMWEBCAMREMOTE EMWEBCAMREMOTE;

class EmWebcam
{
    public:
        EmWebcam(ConsoleVRDPServer *pServer);
        virtual ~EmWebcam();

        static const PDMDRVREG DrvReg;

        void EmWebcamConstruct(EMWEBCAMDRV *pDrv);
        void EmWebcamDestruct(EMWEBCAMDRV *pDrv);

        /* Callbacks. */
        void EmWebcamCbNotify(uint32_t u32Id, const void *pvData, uint32_t cbData);
        void EmWebcamCbDeviceDesc(int rcRequest, void *pDeviceCtx, void *pvUser,
                                  const VRDEVIDEOINDEVICEDESC *pDeviceDesc, uint32_t cbDeviceDesc);
        void EmWebcamCbControl(int rcRequest, void *pDeviceCtx, void *pvUser,
                               const VRDEVIDEOINCTRLHDR *pControl, uint32_t cbControl);
        void EmWebcamCbFrame(int rcRequest, void *pDeviceCtx,
                             const VRDEVIDEOINPAYLOADHDR *pFrame, uint32_t cbFrame);

        /* Methods for the PDM driver. */
        int SendControl(EMWEBCAMDRV *pDrv, void *pvUser, uint64_t u64DeviceId,
                        const VRDEVIDEOINCTRLHDR *pControl, uint32_t cbControl);

    private:
        static DECLCALLBACK(void *) drvQueryInterface(PPDMIBASE pInterface, const char *pszIID);
        static DECLCALLBACK(int)    drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
        static DECLCALLBACK(void)   drvDestruct(PPDMDRVINS pDrvIns);

        ConsoleVRDPServer * const mParent;

        EMWEBCAMDRV *mpDrv;
        EMWEBCAMREMOTE *mpRemote;
        uint64_t volatile mu64DeviceIdSrc;
};

#endif /* !MAIN_INCLUDED_UsbWebcamInterface_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
