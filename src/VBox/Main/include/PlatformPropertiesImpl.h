/* $Id: PlatformPropertiesImpl.h 110370 2025-07-23 09:14:02Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - Platform properties.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_PlatformPropertiesImpl_h
#define MAIN_INCLUDED_PlatformPropertiesImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "PlatformPropertiesWrap.h"

namespace settings
{
    struct PlatformProperties;
}

class ATL_NO_VTABLE PlatformProperties :
    public PlatformPropertiesWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(PlatformProperties)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(VirtualBox *aParent, bool fIsHost = false);
    void uninit() RT_OVERRIDE;

    // public internal methods
    //HRESULT i_loadSettings(const settings::PlatformProperties &data);
    //HRESULT i_saveSettings(settings::PlatformProperties &data);
    //void i_rollback();
    //void i_commit();
    //void i_copyFrom(PlatformProperties *aThat);
    HRESULT i_setArchitecture(PlatformArchitecture_T aArchitecture);

    // public static helper functions
    static PlatformArchitecture_T s_getHostPlatformArchitecture(void);

    // public static methods, for stuff which does not have a state
    static ULONG s_getMaxNetworkAdapters(ChipsetType_T aChipset);
    static ULONG s_getMaxNetworkAdaptersOfType(ChipsetType_T aChipset, NetworkAttachmentType_T aType);
    static HRESULT s_getSupportedVRAMRange(GraphicsControllerType_T aGraphicsControllerType, BOOL fAccelerate3DEnabled, ULONG *aMinMB, ULONG *aMaxMB, ULONG *aStrideSizeMB);
    static int  s_getSupportedGraphicsControllerFeatures(PlatformArchitecture_T enmArchitecture, GraphicsControllerType_T enmController, std::vector<GraphicsFeature_T> &vecSupportedGraphicsFeatures);
    static bool s_isGraphicsControllerFeatureSupported(PlatformArchitecture_T enmArchitecture, GraphicsControllerType_T enmController, GraphicsFeature_T enmFeature);

private:

    // wrapped IPlatformProperties properties
    HRESULT getSerialPortCount(ULONG *aSerialPortCount) RT_OVERRIDE;
    HRESULT getParallelPortCount(ULONG *aParallelPortCount) RT_OVERRIDE;
    HRESULT getMaxBootPosition(ULONG *aMaxBootPosition) RT_OVERRIDE;
    HRESULT getRawModeSupported(BOOL *aRawModeSupported) RT_OVERRIDE;
    HRESULT getExclusiveHwVirt(BOOL *aExclusiveHwVirt) RT_OVERRIDE;
    HRESULT setExclusiveHwVirt(BOOL aExclusiveHwVirt) RT_OVERRIDE;
    HRESULT getSupportedParavirtProviders(std::vector<ParavirtProvider_T> &aSupportedParavirtProviders) RT_OVERRIDE;
    HRESULT getSupportedFirmwareTypes(std::vector<FirmwareType_T> &aSupportedFirmwareTypes) RT_OVERRIDE;
    HRESULT getSupportedGfxControllerTypes(std::vector<GraphicsControllerType_T> &aSupportedGraphicsControllerTypes) RT_OVERRIDE;
    HRESULT getSupportedGuestOSTypes(std::vector<ComPtr<IGuestOSType> > &aSupportedGuestOSTypes) RT_OVERRIDE;
    HRESULT getSupportedNetAdpPromiscModePols(std::vector<NetworkAdapterPromiscModePolicy_T> &aSupportedNetworkAdapterPromiscModePolicies) RT_OVERRIDE;
    HRESULT getSupportedNetworkAdapterTypes(std::vector<NetworkAdapterType_T> &aSupportedNetworkAdapterTypes) RT_OVERRIDE;
    HRESULT getSupportedUartTypes(std::vector<UartType_T> &aSupportedUartTypes) RT_OVERRIDE;
    HRESULT getSupportedUSBControllerTypes(std::vector<USBControllerType_T> &aSupportedUSBControllerTypes) RT_OVERRIDE;
    HRESULT getSupportedAudioControllerTypes(std::vector<AudioControllerType_T> &aSupportedAudioControllerTypes) RT_OVERRIDE;
    HRESULT getSupportedBootDevices(std::vector<DeviceType_T> &aSupportedBootDevices) RT_OVERRIDE;
    HRESULT getSupportedStorageBuses(std::vector<StorageBus_T> &aSupportedStorageBuses) RT_OVERRIDE;
    HRESULT getSupportedStorageControllerTypes(std::vector<StorageControllerType_T> &aSupportedStorageControllerTypes) RT_OVERRIDE;
    HRESULT getSupportedChipsetTypes(std::vector<ChipsetType_T> &aSupportedChipsetTypes) RT_OVERRIDE;
    HRESULT getSupportedIommuTypes(std::vector<IommuType_T> &aSupportedIommuTypes) RT_OVERRIDE;
    HRESULT getSupportedTpmTypes(std::vector<TpmType_T> &aSupportedTpmTypes) RT_OVERRIDE;

    // wrapped IPlatformProperties methods
    HRESULT getMaxNetworkAdapters(ChipsetType_T aChipset,
                                  ULONG *aMaxNetworkAdapters) RT_OVERRIDE;
    HRESULT getMaxNetworkAdaptersOfType(ChipsetType_T aChipset,
                                        NetworkAttachmentType_T aType,
                                        ULONG *aMaxNetworkAdapters) RT_OVERRIDE;
    HRESULT getMaxDevicesPerPortForStorageBus(StorageBus_T aBus,
                                              ULONG *aMaxDevicesPerPort) RT_OVERRIDE;
    HRESULT getMinPortCountForStorageBus(StorageBus_T aBus,
                                         ULONG *aMinPortCount) RT_OVERRIDE;
    HRESULT getMaxPortCountForStorageBus(StorageBus_T aBus,
                                         ULONG *aMaxPortCount) RT_OVERRIDE;
    HRESULT getMaxInstancesOfStorageBus(ChipsetType_T aChipset,
                                        StorageBus_T aBus,
                                        ULONG *aMaxInstances) RT_OVERRIDE;
    HRESULT getSupportedVRAMRange(GraphicsControllerType_T aGraphicsControllerType, BOOL fAccelerate3DEnabled,
                                  ULONG *aMinMB, ULONG *aMaxMB, ULONG *aStrideSizeMB) RT_OVERRIDE;
    HRESULT getSupportedGfxFeaturesForType(GraphicsControllerType_T aGraphicsControllerType,
                                           std::vector<GraphicsFeature_T> &aSupportedGraphicsFeatures) RT_OVERRIDE;
    HRESULT getDeviceTypesForStorageBus(StorageBus_T aBus,
                                        std::vector<DeviceType_T> &aDeviceTypes) RT_OVERRIDE;
    HRESULT getStorageBusForControllerType(StorageControllerType_T aStorageControllerType,
                                           StorageBus_T *aStorageBus) RT_OVERRIDE;
    HRESULT getStorageControllerTypesForBus(StorageBus_T aStorageBus,
                                            std::vector<StorageControllerType_T> &aStorageControllerTypes) RT_OVERRIDE;
    HRESULT getStorageControllerHotplugCapable(StorageControllerType_T aControllerType,
                                               BOOL *aHotplugCapable) RT_OVERRIDE;
    HRESULT getMaxInstancesOfUSBControllerType(ChipsetType_T aChipset,
                                               USBControllerType_T aType,
                                               ULONG *aMaxInstances) RT_OVERRIDE;

    VirtualBox * const     mParent;
    /** Platform architecture the properties are for. */
    PlatformArchitecture_T mPlatformArchitecture;
    /** Flag set to \c true if this instance handles platform properties
     *  for the host, or set to \c false for guests. */
    bool const             mfIsHost;
    /** Configurable data. */
    struct
    {
        bool               fExclusiveHwVirt;
    } mData;
};

#endif /* !MAIN_INCLUDED_PlatformPropertiesImpl_h */

