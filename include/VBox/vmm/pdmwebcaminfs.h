/* $Id: pdmwebcaminfs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * webcaminfs - interfaces between dev and driver.
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

#ifndef VBOX_INCLUDED_vmm_pdmwebcaminfs_h
#define VBOX_INCLUDED_vmm_pdmwebcaminfs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>

struct VRDEVIDEOINDEVICEDESC;
struct VRDEVIDEOINPAYLOADHDR;
struct VRDEVIDEOINCTRLHDR;


/** @defgroup grp_pdm_ifs_webcam    PDM Web Camera Interfaces
 * @ingroup grp_pdm_interfaces
 * @{
 */

/** Pointer to the web camera driver (up) interface. */
typedef struct PDMIWEBCAMDRV *PPDMIWEBCAMDRV;
/**
 * Web camera interface driver provided by the driver to the device,
 * i.e. facing upwards.
 */
typedef struct PDMIWEBCAMDRV
{
    /**
     * The PDM device is ready to get webcam notifications.
     *
     * @param pInterface  Pointer to the interface.
     * @param fReady      Whether the device is ready.
     */
    DECLR3CALLBACKMEMBER(void, pfnReady,(PPDMIWEBCAMDRV pInterface, bool fReady));

    /**
     * Send a control request to the webcam.
     *
     * Async response will be returned by pfnWebcamUpControl callback.
     *
     * @returns VBox status code.
     * @param pInterface    Pointer to the interface.
     * @param pvUser        The callers context.
     * @param idDevice      Unique id for the reported webcam assigned by the driver.
     * @param pCtrl         The control data.
     * @param cbCtrl        The size of the control data.
     */
    DECLR3CALLBACKMEMBER(int, pfnControl,(PPDMIWEBCAMDRV pInterface, void *pvUser, uint64_t idDevice,
                                          struct VRDEVIDEOINCTRLHDR const *pCtrl, uint32_t cbCtrl));
} PDMIWEBCAMDRV;
/** Interface ID for PDMIWEBCAMDRV. */
#define PDMIWEBCAMDRV_IID "0d29b9a1-f4cd-4719-a564-38d5634ba9f8"


/** Pointer to the web camera driver/device (down) interface. */
typedef struct PDMIWEBCAMDEV *PPDMIWEBCAMDEV;
/**
 * Web camera interface provided by the device(/driver) interface,
 * i.e. facing downwards.
 */
typedef struct PDMIWEBCAMDEV
{
    /**
     * A webcam is available.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface.
     * @param   idDevice        Unique id for the reported webcam assigned by the driver.
     * @param   pDeviceDesc     The device description.
     * @param   cbDeviceDesc    The size of the device description.
     * @param   uVersion        The remote video input protocol version.
     * @param   fCapabilities   The remote video input protocol capabilities.
     */
    DECLR3CALLBACKMEMBER(int, pfnAttached,(PPDMIWEBCAMDEV pInterface, uint64_t idDevice,
                                           struct VRDEVIDEOINDEVICEDESC const *pDeviceDesc, uint32_t cbDeviceDesc,
                                           uint32_t uVersion, uint32_t fCapabilities));

    /**
     * The webcam is not available anymore.
     *
     * @param   pInterface      Pointer to the interface.
     * @param   idDevice        Unique id for the reported webcam assigned by the
     *                          driver.
     */
    DECLR3CALLBACKMEMBER(void, pfnDetached,(PPDMIWEBCAMDEV pInterface, uint64_t idDevice));

    /**
     * There is a control response or a control change for the webcam.
     *
     * @param   pInterface      Pointer to the interface.
     * @param   fResponse       True if this is a response for a previous pfnWebcamDownControl call.
     * @param   pvUser          The pvUser parameter of the pfnWebcamDownControl call. Undefined if fResponse == false.
     * @param   idDevice        Unique id for the reported webcam assigned by the
     *                          driver.
     * @param   pCtrl           The control data (defined in VRDE).
     * @param   cbCtrl          The size of the control data.
     */
    DECLR3CALLBACKMEMBER(void, pfnControl,(PPDMIWEBCAMDEV pInterface, bool fResponse, void *pvUser,
                                           uint64_t idDevice, struct VRDEVIDEOINCTRLHDR const *pCtrl, uint32_t cbCtrl));

    /**
     * A new frame.
     *
     * @param   pInterface      Pointer to the interface.
     * @param   idDevice        Unique id for the reported webcam assigned by the driver.
     * @param   pHeader         Payload header (defined in VRDE).
     * @param   cbHeader        Size of the payload header.
     * @param   pvFrame         Frame (image) data.
     * @param   cbFrame         Size of the image data.
     */
    DECLR3CALLBACKMEMBER(void, pfnFrame,(PPDMIWEBCAMDEV pInterface, uint64_t idDevice,
                                         struct VRDEVIDEOINPAYLOADHDR const *pHeader, uint32_t cbHeader,
                                         const void *pvFrame, uint32_t cbFrame));
} PDMIWEBCAMDEV;
/** Interface ID for PDMIWEBCAMDEV. */
#define PDMIWEBCAMDEV_IID "6ac03e3c-f56c-4a35-80af-c13ce47a9dd7"

/** @} */

#endif /* !VBOX_INCLUDED_vmm_pdmwebcaminfs_h */

