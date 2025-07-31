/* $Id: RecordingSettingsImpl.h 110489 2025-07-31 08:32:38Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - Machine recording screen settings.
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

#ifndef MAIN_INCLUDED_RecordingSettingsImpl_h
#define MAIN_INCLUDED_RecordingSettingsImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "RecordingSettingsWrap.h"

namespace settings
{
    struct Recording;
    struct RecordingScreen;
}

class RecordingScreenSettings;

class ATL_NO_VTABLE RecordingSettings
    : public RecordingSettingsWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(RecordingSettings)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *parent);
    HRESULT init(Machine *parent, RecordingSettings *aThat);
    HRESULT initCopy(Machine *parent, RecordingSettings *aThat);
    void uninit();

    // public methods only for internal purposes
    HRESULT i_loadSettings(const settings::Recording &Settings);
    HRESULT i_saveSettings(settings::Recording &Settings);

    void    i_rollback(void);
    void    i_commit(void);
    HRESULT i_copyFrom(RecordingSettings *aThat);
    void    i_applyDefaults(void);

    int i_getDefaultFilename(Utf8Str &strFile, uint32_t idScreen, bool fWithFileExtension);
    int i_getFilename(Utf8Str &strFile, uint32_t idScreen, const Utf8Str &strTemplate);
    bool i_canChangeSettings(void);
    void i_onSettingsChanged(void);

private:

    /** Map of screen settings objects. The key specifies the screen ID. */
    typedef std::map <uint32_t, ComObjPtr<RecordingScreenSettings> > RecordingScreenSettingsObjMap;

    void i_reset(void);
    int i_progressSet(uint64_t msTimestamp);
    int i_start(void);
    int i_stop(void);
    int i_syncToMachineDisplays(uint32_t cDisplays);
    int i_createScreenObj(RecordingScreenSettingsObjMap &screenSettingsMap, uint32_t idScreen, const settings::RecordingScreen &data);
    int i_destroyScreenObj(RecordingScreenSettingsObjMap &screenSettingsMap, uint32_t idScreen);
    int i_destroyAllScreenObj(RecordingScreenSettingsObjMap &screenSettingsMap);

private:

    // wrapped IRecordingSettings properties
    HRESULT getEnabled(BOOL *enabled);
    HRESULT setEnabled(BOOL enable);
    HRESULT getPaused(BOOL *paused);
    HRESULT setPaused(BOOL paused);
    HRESULT getScreens(std::vector<ComPtr<IRecordingScreenSettings> > &aRecordScreenSettings);
    HRESULT getProgress(ComPtr<IProgress> &aProgress);

    // wrapped IRecordingSettings methods
    HRESULT getScreenSettings(ULONG uScreenId, ComPtr<IRecordingScreenSettings> &aRecordScreenSettings);
    HRESULT resume(void);
    HRESULT start(ComPtr<IProgress> &aProgress);

private:

    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_RecordingSettingsImpl_h */

