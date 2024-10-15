/* $Id: GuestOSTypeImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
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

#define LOG_GROUP LOG_GROUP_MAIN_GUESTOSTYPE
#include "GuestOSTypeImpl.h"
#include "AutoCaller.h"
#include "LoggingNew.h"
#include <iprt/cpp/utils.h>

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

GuestOSType::GuestOSType()
    : mOSType(VBOXOSTYPE_Unknown)
    , mOSHint(VBOXOSHINT_NONE)
    , mRAMSize(0)
    , mCPUCount(1)
    , mGraphicsControllerType(GraphicsControllerType_Null)
    , mVRAMSize(0)
    , mHDDSize(0)
    , mNetworkAdapterType(NetworkAdapterType_Am79C973)
    , mNumSerialEnabled(0)
    , mDVDStorageControllerType(StorageControllerType_PIIX3)
    , mDVDStorageBusType(StorageBus_IDE)
    , mHDStorageControllerType(StorageControllerType_PIIX3)
    , mHDStorageBusType(StorageBus_IDE)
    , mChipsetType(ChipsetType_PIIX3)
    , mIommuType(IommuType_None)
    , mAudioControllerType(AudioControllerType_AC97)
    , mAudioCodecType(AudioCodecType_STAC9700)
{
}

GuestOSType::~GuestOSType()
{
}

HRESULT GuestOSType::FinalConstruct()
{
    return BaseFinalConstruct();
}

void GuestOSType::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the guest OS type object.
 *
 * @returns COM result indicator
 * @param ostype containing the following parts:
 * @a aFamilyId          os family short name string
 * @a aFamilyDescription os family name string
 * @a aId                os short name string
 * @a aDescription       os name string
 * @a aOSType            global OS type ID
 * @a aOSHint            os configuration hint
 * @a aRAMSize           recommended RAM size in megabytes
 * @a aVRAMSize          recommended video memory size in megabytes
 * @a aHDDSize           recommended HDD size in bytes
 */
HRESULT GuestOSType::init(const Global::OSType &ostype)
{
    ComAssertRet(ostype.familyId && ostype.familyDescription && ostype.id && ostype.description, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mFamilyID)                          = ostype.familyId;
    unconst(mFamilyDescription)                 = ostype.familyDescription;
    unconst(mOSSubtype)                         = ostype.subtype;
    unconst(mID)                                = ostype.id;
    unconst(mDescription)                       = ostype.description;
    unconst(mOSType)                            = ostype.osType;
    unconst(mOSHint)                            = ostype.osHint;
    unconst(mRAMSize)                           = ostype.recommendedRAM;
    unconst(mCPUCount)                          = ostype.recommendedCPUCount;
    unconst(mHDDSize)                           = ostype.recommendedHDD;
    unconst(mGraphicsControllerType)            = ostype.graphicsControllerType;
    unconst(mVRAMSize)                          = ostype.recommendedVRAM;
    unconst(mNetworkAdapterType)                = ostype.networkAdapterType;
    unconst(mNumSerialEnabled)                  = ostype.numSerialEnabled;
    unconst(mDVDStorageControllerType)          = ostype.dvdStorageControllerType;
    unconst(mDVDStorageBusType)                 = ostype.dvdStorageBusType;
    unconst(mHDStorageControllerType)           = ostype.hdStorageControllerType;
    unconst(mHDStorageBusType)                  = ostype.hdStorageBusType;
    unconst(mChipsetType)                       = ostype.chipsetType;
    unconst(mIommuType)                         = ostype.iommuType;
    unconst(mAudioControllerType)               = ostype.audioControllerType;
    unconst(mAudioCodecType)                    = ostype.audioCodecType;
    unconst(mGuestAdditionsInstallPackageName)  = ostype.guestAdditionsInstallPkgName;

    /* Confirm a successful initialization when it's the case */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void GuestOSType::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;
}

// IGuestOSType properties
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestOSType::getFamilyId(com::Utf8Str &aFamilyId)
{
    /* mFamilyID is constant during life time, no need to lock */
    aFamilyId = mFamilyID;
    return S_OK;
}


HRESULT GuestOSType::getFamilyDescription(com::Utf8Str &aFamilyDescription)
{
    /* mFamilyDescription is constant during life time, no need to lock */
    aFamilyDescription = mFamilyDescription;

    return S_OK;
}


HRESULT GuestOSType::getSubtype(com::Utf8Str &aSubtype)
{
    /* mOSSubtype is constant during life time, no need to lock */
    aSubtype = mOSSubtype;

    return S_OK;
}


HRESULT GuestOSType::getId(com::Utf8Str &aId)
{
    /* mID is constant during life time, no need to lock */
    aId = mID;

    return S_OK;
}


HRESULT GuestOSType::getDescription(com::Utf8Str &aDescription)
{
    /* mDescription is constant during life time, no need to lock */
    aDescription = mDescription;

    return S_OK;
}

HRESULT GuestOSType::getIs64Bit(BOOL *aIs64Bit)
{
    /* mIs64Bit is constant during life time, no need to lock */
    *aIs64Bit = !!(mOSHint & VBOXOSHINT_64BIT);

    return S_OK;
}

PlatformArchitecture_T GuestOSType::i_platformArchitecture() const
{
    /* mOSType constant during life time, no need to lock */
    VBOXOSTYPE const osTypePlatformArchitectureMasked = VBOXOSTYPE(mOSType & VBOXOSTYPE_ArchitectureMask);
    if (   osTypePlatformArchitectureMasked == VBOXOSTYPE_x86
        || osTypePlatformArchitectureMasked == VBOXOSTYPE_x64)
        return PlatformArchitecture_x86;
    else if (   osTypePlatformArchitectureMasked == VBOXOSTYPE_arm32
             || osTypePlatformArchitectureMasked == VBOXOSTYPE_arm64)
        return PlatformArchitecture_ARM;

    /* Will happen when called before being properly initialized(). */
    return PlatformArchitecture_None;
}

HRESULT GuestOSType::getPlatformArchitecture(PlatformArchitecture_T *aPlatformArchitecture)
{
    *aPlatformArchitecture = i_platformArchitecture();
    return S_OK;
}

HRESULT GuestOSType::getRecommendedIOAPIC(BOOL *aRecommendedIOAPIC)
{
    /* mRecommendedIOAPIC is constant during life time, no need to lock */
    *aRecommendedIOAPIC = !!(mOSHint & VBOXOSHINT_X86_IOAPIC);

    return S_OK;
}


HRESULT GuestOSType::getRecommendedVirtEx(BOOL *aRecommendedVirtEx)
{
    /* mRecommendedVirtEx is constant during life time, no need to lock */
    *aRecommendedVirtEx = !!(mOSHint & VBOXOSHINT_X86_HWVIRTEX);

    return S_OK;
}


HRESULT GuestOSType::getRecommendedRAM(ULONG *aRAMSize)
{
    /* mRAMSize is constant during life time, no need to lock */
    *aRAMSize = mRAMSize;

    return S_OK;
}


HRESULT GuestOSType::getRecommendedGraphicsController(GraphicsControllerType_T *aRecommendedGraphicsController)
{
    /* mGraphicsController is constant during life time, no need to lock */
    *aRecommendedGraphicsController = mGraphicsControllerType;

    return S_OK;
}


HRESULT GuestOSType::getRecommendedVRAM(ULONG *aVRAMSize)
{
    /* mVRAMSize is constant during life time, no need to lock */
    *aVRAMSize = mVRAMSize;

    return S_OK;
}


HRESULT GuestOSType::getRecommended2DVideoAcceleration(BOOL *aRecommended2DVideoAcceleration)
{
    /* Constant during life time, no need to lock */
    *aRecommended2DVideoAcceleration = !!(mOSHint & VBOXOSHINT_ACCEL2D);

    return S_OK;
}


HRESULT GuestOSType::getRecommended3DAcceleration(BOOL *aRecommended3DAcceleration)
{
    /* Constant during life time, no need to lock */
    *aRecommended3DAcceleration = !!(mOSHint & VBOXOSHINT_ACCEL3D);

    return S_OK;
}


HRESULT GuestOSType::getRecommendedHDD(LONG64 *aHDDSize)
{
    /* mHDDSize is constant during life time, no need to lock */
    *aHDDSize = (LONG64)mHDDSize;

    return S_OK;
}


HRESULT GuestOSType::getAdapterType(NetworkAdapterType_T *aNetworkAdapterType)
{
    /* mNetworkAdapterType is constant during life time, no need to lock */
    *aNetworkAdapterType = mNetworkAdapterType;

    return S_OK;
}

HRESULT GuestOSType::getRecommendedPAE(BOOL *aRecommendedPAE)
{
    /* recommended PAE is constant during life time, no need to lock */
    *aRecommendedPAE = !!(mOSHint & VBOXOSHINT_X86_PAE);

    return S_OK;
}


HRESULT GuestOSType::getRecommendedFirmware(FirmwareType_T *aFirmwareType)
{
    /* firmware type is constant during life time, no need to lock */
    if (mOSHint & VBOXOSHINT_EFI)
        *aFirmwareType = FirmwareType_EFI;
    else
        *aFirmwareType = FirmwareType_BIOS;

    return S_OK;
}


HRESULT GuestOSType::getRecommendedDVDStorageController(StorageControllerType_T *aStorageControllerType)
{
    /* storage controller type is constant during life time, no need to lock */
    *aStorageControllerType = mDVDStorageControllerType;

    return S_OK;
}


HRESULT GuestOSType::getRecommendedDVDStorageBus(StorageBus_T *aStorageBusType)
{
    /* storage controller type is constant during life time, no need to lock */
    *aStorageBusType = mDVDStorageBusType;

    return S_OK;
}


HRESULT GuestOSType::getRecommendedHDStorageController(StorageControllerType_T *aStorageControllerType)
{
    /* storage controller type is constant during life time, no need to lock */
    *aStorageControllerType = mHDStorageControllerType;

    return S_OK;
}


HRESULT GuestOSType::getRecommendedHDStorageBus(StorageBus_T *aStorageBusType)
{
    /* storage controller type is constant during life time, no need to lock */
    *aStorageBusType = mHDStorageBusType;

    return S_OK;
}


HRESULT GuestOSType::getRecommendedUSBHID(BOOL *aRecommendedUSBHID)
{
    /* HID type is constant during life time, no need to lock */
    *aRecommendedUSBHID = !!(mOSHint & VBOXOSHINT_USBHID);

    return S_OK;
}


HRESULT GuestOSType::getRecommendedHPET(BOOL *aRecommendedHPET)
{
    /* HPET recommendation is constant during life time, no need to lock */
    *aRecommendedHPET = !!(mOSHint & VBOXOSHINT_X86_HPET);

    return S_OK;
}


HRESULT GuestOSType::getRecommendedUSBTablet(BOOL *aRecommendedUSBTablet)
{
    /* HID type is constant during life time, no need to lock */
    *aRecommendedUSBTablet = !!(mOSHint & VBOXOSHINT_USBTABLET);

    return S_OK;
}


HRESULT GuestOSType::getRecommendedRTCUseUTC(BOOL *aRecommendedRTCUseUTC)
{
    /* Value is constant during life time, no need to lock */
    *aRecommendedRTCUseUTC = !!(mOSHint & VBOXOSHINT_RTCUTC);

    return S_OK;
}


HRESULT GuestOSType::getRecommendedChipset(ChipsetType_T *aChipsetType)
{
    /* chipset type is constant during life time, no need to lock */
    *aChipsetType = mChipsetType;

    return S_OK;
}


HRESULT GuestOSType::getRecommendedIommuType(IommuType_T *aIommuType)
{
    /* IOMMU type is constant during life time, no need to lock */
    *aIommuType = mIommuType;

    return S_OK;
}


HRESULT GuestOSType::getRecommendedAudioController(AudioControllerType_T *aAudioController)
{
    *aAudioController = mAudioControllerType;

    return S_OK;
}


HRESULT GuestOSType::getRecommendedAudioCodec(AudioCodecType_T *aAudioCodec)
{
    *aAudioCodec = mAudioCodecType;

    return S_OK;
}


HRESULT GuestOSType::getRecommendedFloppy(BOOL *aRecommendedFloppy)
{
    /* Value is constant during life time, no need to lock */
    *aRecommendedFloppy = !!(mOSHint & VBOXOSHINT_FLOPPY);

    return S_OK;
}


HRESULT GuestOSType::getRecommendedUSB(BOOL *aRecommendedUSB)
{
    /* Value is constant during life time, no need to lock */
    *aRecommendedUSB = !(mOSHint & VBOXOSHINT_NOUSB);

    return S_OK;
}

HRESULT GuestOSType::getRecommendedUSB3(BOOL *aRecommendedUSB3)
{
    /* Value is constant during life time, no need to lock */
    *aRecommendedUSB3 = !!(mOSHint & VBOXOSHINT_USB3);

    return S_OK;
}

HRESULT GuestOSType::getRecommendedTFReset(BOOL *aRecommendedTFReset)
{
    /* recommended triple fault behavior is constant during life time, no need to lock */
    *aRecommendedTFReset = !!(mOSHint & VBOXOSHINT_TFRESET);

    return S_OK;
}

HRESULT GuestOSType::getRecommendedX2APIC(BOOL *aRecommendedX2APIC)
{
    /* mRecommendedX2APIC is constant during life time, no need to lock */
    *aRecommendedX2APIC = !!(mOSHint & VBOXOSHINT_X86_X2APIC);

    return S_OK;
}

HRESULT GuestOSType::getRecommendedCPUCount(ULONG *aRecommendedCPUCount)
{
    /* mCPUCount is constant during life time, no need to lock */
    *aRecommendedCPUCount = mCPUCount;

    return S_OK;
}

HRESULT GuestOSType::getRecommendedTpmType(TpmType_T *aRecommendedTpmType)
{
    /* Value is constant during life time, no need to lock */
    if (mOSHint & VBOXOSHINT_TPM2)
        *aRecommendedTpmType = TpmType_v2_0;
    else if (mOSHint & VBOXOSHINT_TPM)
        *aRecommendedTpmType = TpmType_v1_2;
    else
        *aRecommendedTpmType = TpmType_None;

    return S_OK;
}

HRESULT GuestOSType::getRecommendedSecureBoot(BOOL *aRecommendedSecureBoot)
{
    /* Value is constant during life time, no need to lock */
    *aRecommendedSecureBoot = !!(mOSHint & VBOXOSHINT_EFI_SECUREBOOT);

    return S_OK;
}

HRESULT GuestOSType::getRecommendedWDDMGraphics(BOOL *aRecommendedWDDMGraphics)
{
    /* Value is constant during life time, no need to lock */
    *aRecommendedWDDMGraphics = !!(mOSHint & VBOXOSHINT_WDDM_GRAPHICS);

    return S_OK;
}

HRESULT GuestOSType::getGuestAdditionsInstallPackageName(com::Utf8Str &aGuestAdditionsInstallPkgName)
{
    /* mGuestAdditionsInstallPackageName is constant during life time, no need to lock */
    aGuestAdditionsInstallPkgName = mGuestAdditionsInstallPackageName;
    return S_OK;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
