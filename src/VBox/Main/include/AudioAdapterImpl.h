/* $Id: AudioAdapterImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * VirtualBox COM class implementation
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

#ifndef MAIN_INCLUDED_AudioAdapterImpl_h
#define MAIN_INCLUDED_AudioAdapterImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

class AudioSettings;

#include "AudioAdapterWrap.h"
namespace settings
{
    struct AudioAdapter;
}

class ATL_NO_VTABLE AudioAdapter :
    public AudioAdapterWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS (AudioAdapter)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(AudioSettings *aParent);
    HRESULT init(AudioSettings *aParent, AudioAdapter *aThat);
    HRESULT initCopy(AudioSettings *aParent, AudioAdapter *aThat);
    void uninit();

    // public methods only for internal purposes
    HRESULT i_loadSettings(const settings::AudioAdapter &data);
    HRESULT i_saveSettings(settings::AudioAdapter &data);

    void i_rollback();
    void i_commit();
    void i_copyFrom(AudioAdapter *aThat);

private:

    // wrapped IAudioAdapter properties
    HRESULT getEnabled(BOOL *aEnabled);
    HRESULT setEnabled(BOOL aEnabled);
    HRESULT getEnabledIn(BOOL *aEnabled);
    HRESULT setEnabledIn(BOOL aEnabled);
    HRESULT getEnabledOut(BOOL *aEnabled);
    HRESULT setEnabledOut(BOOL aEnabled);
    HRESULT getAudioDriver(AudioDriverType_T *aAudioDriver);
    HRESULT setAudioDriver(AudioDriverType_T aAudioDriver);
    HRESULT getAudioController(AudioControllerType_T *aAudioController);
    HRESULT setAudioController(AudioControllerType_T aAudioController);
    HRESULT getAudioCodec(AudioCodecType_T *aAudioCodec);
    HRESULT setAudioCodec(AudioCodecType_T aAudioCodec);
    HRESULT getPropertiesList(std::vector<com::Utf8Str>& aProperties);
    HRESULT getProperty(const com::Utf8Str &aKey, com::Utf8Str &aValue);
    HRESULT setProperty(const com::Utf8Str &aKey, const com::Utf8Str &aValue);

private:

    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_AudioAdapterImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
