/* $Id: AudioSettingsImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * VirtualBox COM class implementation
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

#ifndef MAIN_INCLUDED_AudioSettingsImpl_h
#define MAIN_INCLUDED_AudioSettingsImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "AudioAdapterImpl.h"
#include "GuestOSTypeImpl.h"

#include "AudioSettingsWrap.h"
namespace settings
{
    struct AudioSettings;
}

class ATL_NO_VTABLE AudioSettings :
    public AudioSettingsWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(AudioSettings)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent);
    HRESULT init(Machine *aParent, AudioSettings *aThat);
    HRESULT initCopy(Machine *aParent, AudioSettings *aThat);
    void uninit();

    HRESULT getHostAudioDevice(AudioDirection_T aUsage, ComPtr<IHostAudioDevice> &aDevice);
    HRESULT setHostAudioDevice(const ComPtr<IHostAudioDevice> &aDevice, AudioDirection_T aUsage);
    HRESULT getAdapter(ComPtr<IAudioAdapter> &aAdapter);

    // public methods only for internal purposes
    bool     i_canChangeSettings(void);
    Machine *i_getMachine(void);
    void     i_onAdapterChanged(IAudioAdapter *pAdapter);
    void     i_onHostDeviceChanged(IHostAudioDevice *pDevice, bool fIsNew, AudioDeviceState_T enmState, IVirtualBoxErrorInfo *pErrInfo);
    void     i_onSettingsChanged(void);
    HRESULT  i_loadSettings(const settings::AudioAdapter &data);
    HRESULT  i_saveSettings(settings::AudioAdapter &data);
    void     i_copyFrom(AudioSettings *aThat);
    HRESULT  i_applyDefaults(ComObjPtr<GuestOSType> &aGuestOsType);

    void i_rollback();
    void i_commit();

private:

    struct Data;
    Data *m;
};
#endif /* !MAIN_INCLUDED_AudioSettingsImpl_h */

