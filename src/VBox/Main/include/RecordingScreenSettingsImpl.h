/* $Id: RecordingScreenSettingsImpl.h 110427 2025-07-28 10:43:42Z andreas.loeffler@oracle.com $ */

/** @file
 *
 * VirtualBox COM class implementation - Recording settings of one virtual screen.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_RecordingScreenSettingsImpl_h
#define MAIN_INCLUDED_RecordingScreenSettingsImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "RecordingContext.h"
#include "RecordingScreenSettingsWrap.h"

class RecordingSettings;

namespace settings
{
    struct RecordingScreen;
}

class ATL_NO_VTABLE RecordingScreenSettings :
    public RecordingScreenSettingsWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(RecordingScreenSettings)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(RecordingSettings *aParent, uint32_t uScreenId, const settings::RecordingScreen& aThat);
    HRESULT init(RecordingSettings *aParent, RecordingScreenSettings *aThat);
    HRESULT initCopy(RecordingSettings *aParent, RecordingScreenSettings *aThat);
    void uninit(void);

    // public methods only for internal purposes
    HRESULT i_loadSettings(const settings::RecordingScreen &Settings);
    HRESULT i_saveSettings(settings::RecordingScreen &Settings);

    void i_rollback(void);
    void i_commit(void);
    void i_copyFrom(RecordingScreenSettings *aThat);
    void i_applyDefaults(void);

    settings::RecordingScreen &i_getData(void);

    int32_t i_reference(void);
    int32_t i_release(void);
    int32_t i_getReferences(void);

private:

    // wrapped IRecordingScreenSettings methods
    HRESULT isFeatureEnabled(RecordingFeature_T aFeature, BOOL *aEnabled);

    // wrapped IRecordingScreenSettings properties
    HRESULT getId(ULONG *id);
    HRESULT getEnabled(BOOL *enabled);
    HRESULT setEnabled(BOOL enabled);
    HRESULT getFeatures(std::vector<RecordingFeature_T> &aFeatures);
    HRESULT setFeatures(const std::vector<RecordingFeature_T> &aFeatures);
    HRESULT getDestination(RecordingDestination_T *aDestination);
    HRESULT setDestination(RecordingDestination_T aDestination);

    HRESULT getFilename(com::Utf8Str &aFilename);
    HRESULT setFilename(const com::Utf8Str &aFilename);
    HRESULT getMaxTime(ULONG *aMaxTimeS);
    HRESULT setMaxTime(ULONG aMaxTimeS);
    HRESULT getMaxFileSize(ULONG *aMaxFileSizeMB);
    HRESULT setMaxFileSize(ULONG aMaxFileSizeMB);
    HRESULT getOptions(com::Utf8Str &aOptions);
    HRESULT setOptions(const com::Utf8Str &aOptions);

    HRESULT getAudioCodec(RecordingAudioCodec_T *aCodec);
    HRESULT setAudioCodec(RecordingAudioCodec_T aCodec);
    HRESULT getAudioDeadline(RecordingCodecDeadline_T *aDeadline);
    HRESULT setAudioDeadline(RecordingCodecDeadline_T aDeadline);
    HRESULT getAudioRateControlMode(RecordingRateControlMode_T *aMode);
    HRESULT setAudioRateControlMode(RecordingRateControlMode_T aMode);
    HRESULT getAudioHz(ULONG *aHz);
    HRESULT setAudioHz(ULONG aHz);
    HRESULT getAudioBits(ULONG *aBits);
    HRESULT setAudioBits(ULONG aBits);
    HRESULT getAudioChannels(ULONG *aChannels);
    HRESULT setAudioChannels(ULONG aChannels);

    HRESULT getVideoCodec(RecordingVideoCodec_T *aCodec);
    HRESULT setVideoCodec(RecordingVideoCodec_T aCodec);
    HRESULT getVideoDeadline(RecordingCodecDeadline_T *aDeadline);
    HRESULT setVideoDeadline(RecordingCodecDeadline_T aDeadline);
    HRESULT getVideoWidth(ULONG *aVideoWidth);
    HRESULT setVideoWidth(ULONG aVideoWidth);
    HRESULT getVideoHeight(ULONG *aVideoHeight);
    HRESULT setVideoHeight(ULONG aVideoHeight);
    HRESULT getVideoRate(ULONG *aVideoRate);
    HRESULT setVideoRate(ULONG aVideoRate);
    HRESULT getVideoRateControlMode(RecordingRateControlMode_T *aMode);
    HRESULT setVideoRateControlMode(RecordingRateControlMode_T aMode);
    HRESULT getVideoFPS(ULONG *aVideoFPS);
    HRESULT setVideoFPS(ULONG aVideoFPS);
    HRESULT getVideoScalingMode(RecordingVideoScalingMode_T *aMode);
    HRESULT setVideoScalingMode(RecordingVideoScalingMode_T aMode);

private:

    // internal methods
    int i_initInternal();

private:

    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_RecordingScreenSettingsImpl_h */

