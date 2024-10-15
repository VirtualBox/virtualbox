/* $Id: SystemPropertiesImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

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

#ifndef MAIN_INCLUDED_SystemPropertiesImpl_h
#define MAIN_INCLUDED_SystemPropertiesImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "MediumFormatImpl.h"
#include "SystemPropertiesWrap.h"

class CPUProfile;
class PlatformProperties;

namespace settings
{
    struct SystemProperties;
}

class ATL_NO_VTABLE SystemProperties :
    public SystemPropertiesWrap
{
public:
    typedef std::list<ComObjPtr<MediumFormat> > MediumFormatList;
    typedef std::list<ComObjPtr<CPUProfile> > CPUProfileList_T;

    DECLARE_COMMON_CLASS_METHODS(SystemProperties)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(VirtualBox *aParent);
    void uninit() RT_OVERRIDE;

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)
    HRESULT i_loadSettings(const settings::SystemProperties &data);
    HRESULT i_saveSettings(settings::SystemProperties &data);

    ComObjPtr<MediumFormat> i_mediumFormat(const Utf8Str &aFormat);
    ComObjPtr<MediumFormat> i_mediumFormatFromExtension(const Utf8Str &aExt);

    int i_loadVDPlugin(const char *pszPluginLibrary);
    int i_unloadVDPlugin(const char *pszPluginLibrary);

    HRESULT i_getDefaultAdditionsISO(com::Utf8Str &aDefaultAdditionsISO);

private:

    // wrapped ISystemProperties properties
    HRESULT getMinGuestRAM(ULONG *aMinGuestRAM) RT_OVERRIDE;
    HRESULT getMaxGuestRAM(ULONG *aMaxGuestRAM) RT_OVERRIDE;
    HRESULT getMinGuestVRAM(ULONG *aMinGuestVRAM) RT_OVERRIDE;
    HRESULT getMaxGuestVRAM(ULONG *aMaxGuestVRAM) RT_OVERRIDE;
    HRESULT getMinGuestCPUCount(ULONG *aMinGuestCPUCount) RT_OVERRIDE;
    HRESULT getMaxGuestCPUCount(ULONG *aMaxGuestCPUCount) RT_OVERRIDE;
    HRESULT getMaxGuestMonitors(ULONG *aMaxGuestMonitors) RT_OVERRIDE;
    HRESULT getInfoVDSize(LONG64 *aInfoVDSize) RT_OVERRIDE;
    HRESULT getDefaultMachineFolder(com::Utf8Str &aDefaultMachineFolder) RT_OVERRIDE;
    HRESULT setDefaultMachineFolder(const com::Utf8Str &aDefaultMachineFolder) RT_OVERRIDE;
    HRESULT getLoggingLevel(com::Utf8Str &aLoggingLevel) RT_OVERRIDE;
    HRESULT setLoggingLevel(const com::Utf8Str &aLoggingLevel) RT_OVERRIDE;
    HRESULT getMediumFormats(std::vector<ComPtr<IMediumFormat> > &aMediumFormats) RT_OVERRIDE;
    HRESULT getDefaultHardDiskFormat(com::Utf8Str &aDefaultHardDiskFormat) RT_OVERRIDE;
    HRESULT setDefaultHardDiskFormat(const com::Utf8Str &aDefaultHardDiskFormat) RT_OVERRIDE;
    HRESULT getFreeDiskSpaceWarning(LONG64 *aFreeDiskSpaceWarning) RT_OVERRIDE;
    HRESULT setFreeDiskSpaceWarning(LONG64 aFreeDiskSpaceWarning) RT_OVERRIDE;
    HRESULT getFreeDiskSpacePercentWarning(ULONG *aFreeDiskSpacePercentWarning) RT_OVERRIDE;
    HRESULT setFreeDiskSpacePercentWarning(ULONG aFreeDiskSpacePercentWarning) RT_OVERRIDE;
    HRESULT getFreeDiskSpaceError(LONG64 *aFreeDiskSpaceError) RT_OVERRIDE;
    HRESULT setFreeDiskSpaceError(LONG64 aFreeDiskSpaceError) RT_OVERRIDE;
    HRESULT getFreeDiskSpacePercentError(ULONG *aFreeDiskSpacePercentError) RT_OVERRIDE;
    HRESULT setFreeDiskSpacePercentError(ULONG aFreeDiskSpacePercentError) RT_OVERRIDE;
    HRESULT getVRDEAuthLibrary(com::Utf8Str &aVRDEAuthLibrary) RT_OVERRIDE;
    HRESULT setVRDEAuthLibrary(const com::Utf8Str &aVRDEAuthLibrary) RT_OVERRIDE;
    HRESULT getWebServiceAuthLibrary(com::Utf8Str &aWebServiceAuthLibrary) RT_OVERRIDE;
    HRESULT setWebServiceAuthLibrary(const com::Utf8Str &aWebServiceAuthLibrary) RT_OVERRIDE;
    HRESULT getDefaultVRDEExtPack(com::Utf8Str &aDefaultVRDEExtPack) RT_OVERRIDE;
    HRESULT setDefaultVRDEExtPack(const com::Utf8Str &aDefaultVRDEExtPack) RT_OVERRIDE;
    HRESULT getDefaultCryptoExtPack(com::Utf8Str &aDefaultCryptoExtPack) RT_OVERRIDE;
    HRESULT setDefaultCryptoExtPack(const com::Utf8Str &aDefaultCryptoExtPack) RT_OVERRIDE;
    HRESULT getLogHistoryCount(ULONG *aLogHistoryCount) RT_OVERRIDE;
    HRESULT setLogHistoryCount(ULONG aLogHistoryCount) RT_OVERRIDE;
    HRESULT getDefaultAudioDriver(AudioDriverType_T *aDefaultAudioDriver) RT_OVERRIDE;
    HRESULT getAutostartDatabasePath(com::Utf8Str &aAutostartDatabasePath) RT_OVERRIDE;
    HRESULT setAutostartDatabasePath(const com::Utf8Str &aAutostartDatabasePath) RT_OVERRIDE;
    HRESULT getDefaultAdditionsISO(com::Utf8Str &aDefaultAdditionsISO) RT_OVERRIDE;
    HRESULT setDefaultAdditionsISO(const com::Utf8Str &aDefaultAdditionsISO) RT_OVERRIDE;
    HRESULT getDefaultFrontend(com::Utf8Str &aDefaultFrontend) RT_OVERRIDE;
    HRESULT setDefaultFrontend(const com::Utf8Str &aDefaultFrontend) RT_OVERRIDE;
    HRESULT getScreenShotFormats(std::vector<BitmapFormat_T> &aScreenShotFormats) RT_OVERRIDE;
    HRESULT getPlatform(ComPtr<IPlatformProperties> &aPlatformProperties) RT_OVERRIDE;
    HRESULT getProxyMode(ProxyMode_T *pProxyMode) RT_OVERRIDE;
    HRESULT setProxyMode(ProxyMode_T aProxyMode) RT_OVERRIDE;
    HRESULT getProxyURL(com::Utf8Str &aProxyURL) RT_OVERRIDE;
    HRESULT setProxyURL(const com::Utf8Str &aProxyURL) RT_OVERRIDE;
    HRESULT getSupportedPlatformArchitectures(std::vector<PlatformArchitecture_T> &aSupportedPlatformArchitectures) RT_OVERRIDE;
    HRESULT getSupportedClipboardModes(std::vector<ClipboardMode_T> &aSupportedClipboardModes) RT_OVERRIDE;
    HRESULT getSupportedDnDModes(std::vector<DnDMode_T> &aSupportedDnDModes) RT_OVERRIDE;
    HRESULT getSupportedPointingHIDTypes(std::vector<PointingHIDType_T> &aSupportedPointingHIDTypes) RT_OVERRIDE;
    HRESULT getSupportedKeyboardHIDTypes(std::vector<KeyboardHIDType_T> &aSupportedKeyboardHIDTypes) RT_OVERRIDE;
    HRESULT getSupportedVFSTypes(std::vector<VFSType_T> &aSupportedVFSTypes) RT_OVERRIDE;
    HRESULT getSupportedImportOptions(std::vector<ImportOptions_T> &aSupportedImportOptions) RT_OVERRIDE;
    HRESULT getSupportedExportOptions(std::vector<ExportOptions_T> &aSupportedExportOptions) RT_OVERRIDE;
    HRESULT getSupportedGraphicsFeatures(std::vector<GraphicsFeature_T> &aSupportedGraphicsFeatures) RT_OVERRIDE;
    HRESULT getSupportedRecordingFeatures(std::vector<RecordingFeature_T> &aSupportedRecordingFeatures) RT_OVERRIDE;
    HRESULT getSupportedRecordingAudioCodecs(std::vector<RecordingAudioCodec_T> &aSupportedRecordingAudioCodecs) RT_OVERRIDE;
    HRESULT getSupportedRecordingVideoCodecs(std::vector<RecordingVideoCodec_T> &aSupportedRecordingVideoCodecs) RT_OVERRIDE;
    HRESULT getSupportedRecordingVSModes(std::vector<RecordingVideoScalingMode_T> &aSupportedRecordingVideoScalingModes) RT_OVERRIDE;
    HRESULT getSupportedRecordingARCModes(std::vector<RecordingRateControlMode_T> &aSupportedRecordingAudioRateControlModes) RT_OVERRIDE;
    HRESULT getSupportedRecordingVRCModes(std::vector<RecordingRateControlMode_T> &aSupportedRecordingVideoRateControlModes) RT_OVERRIDE;
    HRESULT getSupportedCloneOptions(std::vector<CloneOptions_T> &aSupportedCloneOptions) RT_OVERRIDE;
    HRESULT getSupportedAutostopTypes(std::vector<AutostopType_T> &aSupportedAutostopTypes) RT_OVERRIDE;
    HRESULT getSupportedVMProcPriorities(std::vector<VMProcPriority_T> &aSupportedVMProcPriorities) RT_OVERRIDE;
    HRESULT getSupportedNetworkAttachmentTypes(std::vector<NetworkAttachmentType_T> &aSupportedNetworkAttachmentTypes) RT_OVERRIDE;
    HRESULT getSupportedPortModes(std::vector<PortMode_T> &aSupportedPortModes) RT_OVERRIDE;
    HRESULT getSupportedAudioDriverTypes(std::vector<AudioDriverType_T> &aSupportedAudioDriverTypes) RT_OVERRIDE;
    HRESULT getLanguageId(com::Utf8Str &aLanguageId) RT_OVERRIDE;
    HRESULT setLanguageId(const com::Utf8Str &aLanguageId) RT_OVERRIDE;

    // wrapped ISystemProperties methods
    HRESULT getDefaultIoCacheSettingForStorageController(StorageControllerType_T aControllerType,
                                                         BOOL *aEnabled) RT_OVERRIDE;
    HRESULT getCPUProfiles(CPUArchitecture_T aArchitecture, const com::Utf8Str &aNamePattern,
                           std::vector<ComPtr<ICPUProfile> > &aProfiles) RT_OVERRIDE;
    HRESULT getExecutionEnginesForVmCpuArchitecture(CPUArchitecture_T aCpuArchitecture,
                                                    std::vector<VMExecutionEngine_T> &aExecutionEngines) RT_OVERRIDE;

    HRESULT i_getUserHomeDirectory(Utf8Str &strPath);
    HRESULT i_setDefaultMachineFolder(const Utf8Str &strPath);
    HRESULT i_setLoggingLevel(const com::Utf8Str &aLoggingLevel);
    HRESULT i_setDefaultHardDiskFormat(const com::Utf8Str &aFormat);
    HRESULT i_setVRDEAuthLibrary(const com::Utf8Str &aPath);

    HRESULT i_setWebServiceAuthLibrary(const com::Utf8Str &aPath);
    HRESULT i_setDefaultVRDEExtPack(const com::Utf8Str &aExtPack);
    HRESULT i_setDefaultCryptoExtPack(const com::Utf8Str &aExtPack);
    HRESULT i_setAutostartDatabasePath(const com::Utf8Str &aPath);
    HRESULT i_setDefaultAdditionsISO(const com::Utf8Str &aPath);
    HRESULT i_setDefaultFrontend(const com::Utf8Str &aDefaultFrontend);

    VirtualBox * const  mParent;

    settings::SystemProperties *m;

    MediumFormatList m_llMediumFormats;

    ComObjPtr<PlatformProperties> const m_platformProperties;       /**< The host's platform properties. */
    bool                                m_fLoadedX86CPUProfiles;    /**< Set if we've loaded the x86 and AMD64 CPU profiles. */
    CPUProfileList_T                    m_llCPUProfiles;            /**< List of loaded CPU profiles. */

    friend class VirtualBox;
};

#endif /* !MAIN_INCLUDED_SystemPropertiesImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
