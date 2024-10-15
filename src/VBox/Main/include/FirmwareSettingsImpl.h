/* $Id: FirmwareSettingsImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * VirtualBox COM class implementation - Machine firmware settings.
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

#ifndef MAIN_INCLUDED_FirmwareSettingsImpl_h
#define MAIN_INCLUDED_FirmwareSettingsImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "FirmwareSettingsWrap.h"

class GuestOSType;

namespace settings
{
    struct FirmwareSettings;
}

class ATL_NO_VTABLE FirmwareSettings :
    public FirmwareSettingsWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(FirmwareSettings)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *parent);
    HRESULT init(Machine *parent, FirmwareSettings *that);
    HRESULT initCopy(Machine *parent, FirmwareSettings *that);
    void uninit();

    // public methods for internal purposes only
    HRESULT i_loadSettings(const settings::FirmwareSettings &data);
    HRESULT i_saveSettings(settings::FirmwareSettings &data);
    FirmwareType_T i_getFirmwareType() const;

    void i_rollback();
    void i_commit();
    void i_copyFrom(FirmwareSettings *aThat);
    void i_applyDefaults(GuestOSType *aOsType);

private:

    // wrapped IFirmwareSettings properties
    HRESULT getFirmwareType(FirmwareType_T *aType);
    HRESULT setFirmwareType(FirmwareType_T aType);
    HRESULT getLogoFadeIn(BOOL *enabled);
    HRESULT setLogoFadeIn(BOOL enable);
    HRESULT getLogoFadeOut(BOOL *enabled);
    HRESULT setLogoFadeOut(BOOL enable);
    HRESULT getLogoDisplayTime(ULONG *displayTime);
    HRESULT setLogoDisplayTime(ULONG displayTime);
    HRESULT getLogoImagePath(com::Utf8Str &imagePath);
    HRESULT setLogoImagePath(const com::Utf8Str &imagePath);
    HRESULT getBootMenuMode(FirmwareBootMenuMode_T *bootMenuMode);
    HRESULT setBootMenuMode(FirmwareBootMenuMode_T bootMenuMode);
    HRESULT getACPIEnabled(BOOL *enabled);
    HRESULT setACPIEnabled(BOOL enable);
    HRESULT getIOAPICEnabled(BOOL *aIOAPICEnabled);
    HRESULT setIOAPICEnabled(BOOL aIOAPICEnabled);
    HRESULT getAPICMode(APICMode_T *aAPICMode);
    HRESULT setAPICMode(APICMode_T aAPICMode);
    HRESULT getTimeOffset(LONG64 *offset);
    HRESULT setTimeOffset(LONG64 offset);
    HRESULT getPXEDebugEnabled(BOOL *enabled);
    HRESULT setPXEDebugEnabled(BOOL enable);
    HRESULT getSMBIOSUuidLittleEndian(BOOL *enabled);
    HRESULT setSMBIOSUuidLittleEndian(BOOL enable);
    HRESULT getAutoSerialNumGen(BOOL *enabled);
    HRESULT setAutoSerialNumGen(BOOL enable);

    struct Data;
    Data *m;
};
#endif /* !MAIN_INCLUDED_FirmwareSettingsImpl_h */

