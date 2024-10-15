/* $Id: DrvAudioVRDE.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox driver interface to VRDE backend.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_DrvAudioVRDE_h
#define MAIN_INCLUDED_DrvAudioVRDE_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/ptr.h>
#include <VBox/com/string.h>

#include <VBox/RemoteDesktop/VRDE.h>

#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmifs.h>

#include "AudioDriver.h"

using namespace com;

class Console;

class AudioVRDE : public AudioDriver
{

public:

    AudioVRDE(Console *pConsole);
    virtual ~AudioVRDE(void);

public:

    static const PDMDRVREG DrvReg;

public:

    void onVRDEClientConnect(uint32_t uClientID);
    void onVRDEClientDisconnect(uint32_t uClientID);
    int onVRDEControl(bool fEnable, uint32_t uFlags);
    int onVRDEInputBegin(void *pvContext, PVRDEAUDIOINBEGIN pVRDEAudioBegin);
    int onVRDEInputData(void *pvContext, const void *pvData, uint32_t cbData);
    int onVRDEInputEnd(void *pvContext);
    int onVRDEInputIntercept(bool fIntercept);

public:

    static DECLCALLBACK(int) drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
    static DECLCALLBACK(void) drvDestruct(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(void) drvPowerOff(PPDMDRVINS pDrvIns);

private:

    virtual int configureDriver(PCFGMNODE pLunCfg, PCVMMR3VTABLE pVMM) RT_OVERRIDE;

    /** Pointer to the associated VRDE audio driver. */
    struct DRVAUDIOVRDE *mpDrv;
    /** Protects accesses to mpDrv from racing driver destruction. */
    RTCRITSECT mCritSect;
};

#endif /* !MAIN_INCLUDED_DrvAudioVRDE_h */

