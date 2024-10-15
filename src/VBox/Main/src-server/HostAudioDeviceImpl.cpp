/* $Id: HostAudioDeviceImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - Host audio device implementation.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_HOSTAUDIODEVICE
#include "HostAudioDeviceImpl.h"
#include "VirtualBoxImpl.h"

#include <iprt/cpp/utils.h>

#include <VBox/settings.h>

#include "AutoCaller.h"
#include "LoggingNew.h"


// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

HostAudioDevice::HostAudioDevice()
{
}

HostAudioDevice::~HostAudioDevice()
{
}

HRESULT HostAudioDevice::FinalConstruct()
{
    return BaseFinalConstruct();
}

void HostAudioDevice::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


// public initializer/uninitializer for internal purposes only
////////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the audio device object.
 *
 * @returns HRESULT
 */
HRESULT HostAudioDevice::init(void)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Uninitializes the instance and sets the ready flag to FALSE.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void HostAudioDevice::uninit(void)
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;
}


// IHostAudioDevice properties
////////////////////////////////////////////////////////////////////////////////
