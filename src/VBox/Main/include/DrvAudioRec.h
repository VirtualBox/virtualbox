/* $Id: DrvAudioRec.h 110427 2025-07-28 10:43:42Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox driver interface video recording audio backend.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_DrvAudioRec_h
#define MAIN_INCLUDED_DrvAudioRec_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/ptr.h>
#include <VBox/settings.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmifs.h>

#include "AudioDriver.h"
#include "RecordingContext.h"

using namespace com;

class Console;

class AudioVideoRec : public AudioDriver
{

public:

    AudioVideoRec(Console *pConsole);
    virtual ~AudioVideoRec(void);

public:

    static const PDMDRVREG DrvReg;

public:

    int applyConfiguration(const ComPtr<IRecordingSettings> & Settings);

public:

    static DECLCALLBACK(int)  drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
    static DECLCALLBACK(void) drvDestruct(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(void) drvPowerOff(PPDMDRVINS pDrvIns);

private:

    virtual int configureDriver(PCFGMNODE pLunCfg, PCVMMR3VTABLE pVMM) RT_OVERRIDE;

    /** Pointer to the associated video recording audio driver. */
    struct DRVAUDIORECORDING         *m_pDrv;
    /** Recording settings used for configuring the driver. */
    ComPtr<IRecordingSettings>        m_Settings;
};

#endif /* !MAIN_INCLUDED_DrvAudioRec_h */

