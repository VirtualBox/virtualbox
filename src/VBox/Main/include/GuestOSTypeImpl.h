/* $Id: GuestOSTypeImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef MAIN_INCLUDED_GuestOSTypeImpl_h
#define MAIN_INCLUDED_GuestOSTypeImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "Global.h"
#include "GuestOSTypeWrap.h"

class ATL_NO_VTABLE GuestOSType :
    public GuestOSTypeWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(GuestOSType)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(const Global::OSType &ostype);
    void uninit();

    // public methods only for internal purposes
    const Utf8Str &i_id() const { return mID; }
    const Utf8Str &i_familyId() const { return mFamilyID; }
    const Utf8Str &i_subtype() const { return mOSSubtype; }
    const Utf8Str &i_description() const { return mDescription; }
    bool i_is64Bit() const { return !!(mOSHint & VBOXOSHINT_64BIT); }
    PlatformArchitecture_T i_platformArchitecture() const;
    bool i_recommendedIOAPIC() const { return !!(mOSHint & VBOXOSHINT_X86_IOAPIC); }
    bool i_recommendedX2APIC() const { return !!(mOSHint & VBOXOSHINT_X86_X2APIC); }
    bool i_recommendedVirtEx() const { return !!(mOSHint & VBOXOSHINT_X86_HWVIRTEX); }
    bool i_recommendedEFI() const { return !!(mOSHint & VBOXOSHINT_EFI); }
    bool i_recommendedEFISecureBoot() const { return !!(mOSHint & VBOXOSHINT_EFI_SECUREBOOT); }
    bool i_recommendedTpm2() const { return !!(mOSHint & VBOXOSHINT_TPM2); }
    NetworkAdapterType_T i_networkAdapterType() const { return mNetworkAdapterType; }
    uint32_t i_numSerialEnabled() const { return mNumSerialEnabled; }

private:

    // Wrapped IGuestOSType properties
    HRESULT getFamilyId(com::Utf8Str &aFamilyId);
    HRESULT getFamilyDescription(com::Utf8Str &aFamilyDescription);
    HRESULT getSubtype(com::Utf8Str &aSubtype);
    HRESULT getId(com::Utf8Str &aId);
    HRESULT getDescription(com::Utf8Str &aDescription);
    HRESULT getIs64Bit(BOOL *aIs64Bit);
    HRESULT getPlatformArchitecture(PlatformArchitecture_T *aPlatformArchitecture);
    HRESULT getRecommendedIOAPIC(BOOL *aRecommendedIOAPIC);
    HRESULT getRecommendedVirtEx(BOOL *aRecommendedVirtEx);
    HRESULT getRecommendedRAM(ULONG *RAMSize);
    HRESULT getRecommendedGraphicsController(GraphicsControllerType_T *aRecommendedGraphicsController);
    HRESULT getRecommendedVRAM(ULONG *aVRAMSize);
    HRESULT getRecommended2DVideoAcceleration(BOOL *aRecommended2DVideoAcceleration);
    HRESULT getRecommended3DAcceleration(BOOL *aRecommended3DAcceleration);
    HRESULT getRecommendedHDD(LONG64 *aHDDSize);
    HRESULT getAdapterType(NetworkAdapterType_T *aNetworkAdapterType);
    HRESULT getRecommendedPAE(BOOL *aRecommendedPAE);
    HRESULT getRecommendedDVDStorageController(StorageControllerType_T *aStorageControllerType);
    HRESULT getRecommendedFirmware(FirmwareType_T *aFirmwareType);
    HRESULT getRecommendedDVDStorageBus(StorageBus_T *aStorageBusType);
    HRESULT getRecommendedHDStorageController(StorageControllerType_T *aStorageControllerType);
    HRESULT getRecommendedHDStorageBus(StorageBus_T *aStorageBusType);
    HRESULT getRecommendedUSBHID(BOOL *aRecommendedUSBHID);
    HRESULT getRecommendedHPET(BOOL *aRecommendedHPET);
    HRESULT getRecommendedUSBTablet(BOOL *aRecommendedUSBTablet);
    HRESULT getRecommendedRTCUseUTC(BOOL *aRecommendedRTCUseUTC);
    HRESULT getRecommendedChipset(ChipsetType_T *aChipsetType);
    HRESULT getRecommendedIommuType(IommuType_T *aIommuType);
    HRESULT getRecommendedAudioController(AudioControllerType_T *aAudioController);
    HRESULT getRecommendedAudioCodec(AudioCodecType_T *aAudioCodec);
    HRESULT getRecommendedFloppy(BOOL *aRecommendedFloppy);
    HRESULT getRecommendedUSB(BOOL *aRecommendedUSB);
    HRESULT getRecommendedUSB3(BOOL *aRecommendedUSB3);
    HRESULT getRecommendedTFReset(BOOL *aRecommendedTFReset);
    HRESULT getRecommendedX2APIC(BOOL *aRecommendedX2APIC);
    HRESULT getRecommendedCPUCount(ULONG *aRecommendedCPUCount);
    HRESULT getRecommendedTpmType(TpmType_T *aRecommendedTpmType);
    HRESULT getRecommendedSecureBoot(BOOL *aRecommendedSecureBoot);
    HRESULT getRecommendedWDDMGraphics(BOOL *aRecommendedWDDMGraphics);
    HRESULT getGuestAdditionsInstallPackageName(com::Utf8Str &aGuestAdditionsInstallPkgName);


    const Utf8Str mFamilyID;
    const Utf8Str mFamilyDescription;
    const Utf8Str mOSSubtype;
    const Utf8Str mID;
    const Utf8Str mDescription;
    const Utf8Str mGuestAdditionsInstallPackageName;
    const VBOXOSTYPE mOSType;
    const uint32_t mOSHint;
    const uint32_t mRAMSize;
    const uint32_t mCPUCount;
    const GraphicsControllerType_T mGraphicsControllerType;
    const uint32_t mVRAMSize;
    const uint64_t mHDDSize;
    const NetworkAdapterType_T mNetworkAdapterType;
    const uint32_t mNumSerialEnabled;
    const StorageControllerType_T mDVDStorageControllerType;
    const StorageBus_T mDVDStorageBusType;
    const StorageControllerType_T mHDStorageControllerType;
    const StorageBus_T mHDStorageBusType;
    const ChipsetType_T mChipsetType;
    const IommuType_T mIommuType;
    const AudioControllerType_T mAudioControllerType;
    const AudioCodecType_T mAudioCodecType;
};

#endif /* !MAIN_INCLUDED_GuestOSTypeImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
