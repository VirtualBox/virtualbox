/* $Id: UnattendedImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Unattended class header
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

#ifndef MAIN_INCLUDED_UnattendedImpl_h
#define MAIN_INCLUDED_UnattendedImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/ostypes.h>
#include <iprt/time.h>
#include "UnattendedWrap.h"

/* Forward declarations. */
class UnattendedInstaller;
struct UnattendedInstallationDisk;
struct ControllerSlot;

/**
 * A data type to store image data which is read from intall.wim file.
 * Currently relevant only for Windows OSs.
 */
struct WIMImage
{
    Utf8Str  mName;
    Utf8Str  mVersion;
    Utf8Str  mArch;
    Utf8Str  mFlavor;
    RTCList<RTCString, RTCString *> mLanguages;
    Utf8Str  mDefaultLanguage;
    uint32_t mImageIndex;
    VBOXOSTYPE mOSType;
    WIMImage() : mImageIndex(0), mOSType(VBOXOSTYPE_Unknown) { }
    const Utf8Str &formatName(Utf8Str &r_strName) const;
};

/**
 * Class implementing the IUnattended interface.
 *
 * This class is instantiated on the request by IMachine::getUnattended.
 */
class ATL_NO_VTABLE Unattended
    : public UnattendedWrap
{
public:
    DECLARE_COMMON_CLASS_METHODS(Unattended)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT initUnattended(VirtualBox *aParent);
    void uninit();

    // public methods for internal purposes
    Utf8Str const &i_getIsoPath() const;
    Utf8Str const &i_getUser() const;
    Utf8Str const &i_getUserPassword() const;
    Utf8Str const &i_getAdminPassword() const;
    Utf8Str const &i_getFullUserName() const;
    Utf8Str const &i_getProductKey() const;
    Utf8Str const &i_getProxy() const;
    Utf8Str const &i_getAdditionsIsoPath() const;
    bool           i_getInstallGuestAdditions() const;
    Utf8Str const &i_getValidationKitIsoPath() const;
    bool           i_getInstallTestExecService() const;
    Utf8Str const &i_getUserPayloadIsoPath() const;
    bool           i_getInstallUserPayload() const;
    Utf8Str const &i_getTimeZone() const;
    PCRTTIMEZONEINFO i_getTimeZoneInfo() const;
    Utf8Str const &i_getLocale() const;
    Utf8Str const &i_getLanguage() const;
    Utf8Str const &i_getCountry() const;
    bool           i_isMinimalInstallation() const;
    Utf8Str const &i_getHostname() const;
    Utf8Str const &i_getAuxiliaryBasePath() const;
    ULONG          i_getImageIndex() const;
    Utf8Str const &i_getScriptTemplatePath() const;
    Utf8Str const &i_getPostInstallScriptTemplatePath() const;
    Utf8Str const &i_getPostInstallCommand() const;
    /** The directory where the unattended install config and script is
     * located, from the perspective of the running unattended install. */
    Utf8Str const &i_getAuxiliaryInstallDir() const;
    Utf8Str const &i_getExtraInstallKernelParameters() const;
    Utf8Str const &i_getAdditionsInstallPackage() const;

    bool           i_isRtcUsingUtc() const;
    bool           i_isGuestOs64Bit() const;
    bool           i_isFirmwareEFI() const;
    Utf8Str const &i_getDetectedOSVersion();
    bool           i_getAvoidUpdatesOverNetwork() const;

private:
    ComPtr<VirtualBox> const mParent;       /**< Strong reference to the parent object (VirtualBox/IMachine). */
    ComPtr<Machine> mMachine;               /**< Strong reference to the machine object (Machine/IMachine). */
    Guid            mMachineUuid;           /**< The machine UUID. */
    RTNATIVETHREAD  mhThreadReconfigureVM;  /**< Set when reconfigureVM is running. */
    Utf8Str         mStrGuestOsTypeId;      /**< Guest OS type ID (set by prepare). */
    bool            mfRtcUseUtc;            /**< Copy of IMachine::RTCUseUTC (locking reasons). */
    bool            mfGuestOs64Bit;         /**< 64-bit (true) or 32-bit guest OS (set by prepare). */
    FirmwareType_T  menmFirmwareType;       /**< Firmware type BIOS/EFI (set by prepare). */
    UnattendedInstaller *mpInstaller;       /**< The installer instance (set by prepare, deleted by done). */

    /** @name Values of the IUnattended attributes.
     * @{ */
    Utf8Str         mStrUser;
    Utf8Str         mStrUserPassword;
    Utf8Str         mStrAdminPassword;
    Utf8Str         mStrFullUserName;
    Utf8Str         mStrProductKey;
    Utf8Str         mStrIsoPath;
    Utf8Str         mStrAdditionsIsoPath;
    bool            mfInstallGuestAdditions;
    bool            mfInstallTestExecService;
    Utf8Str         mStrValidationKitIsoPath;
    Utf8Str         mStrUserPayloadIsoPath;
    bool            mfInstallUserPayload;
    Utf8Str         mStrTimeZone;
    PCRTTIMEZONEINFO mpTimeZoneInfo;
    Utf8Str         mStrLocale;
    Utf8Str         mStrLanguage;           /**< (only relevant for windows at the moment) */
    Utf8Str         mStrCountry;
    RTCList<RTCString, RTCString *> mPackageSelectionAdjustments;
    Utf8Str         mStrHostname;
    Utf8Str         mStrAuxiliaryBasePath;
    bool            mfIsDefaultAuxiliaryBasePath;
    ULONG           midxImage;
    Utf8Str         mStrScriptTemplatePath;
    Utf8Str         mStrPostInstallScriptTemplatePath;
    Utf8Str         mStrPostInstallCommand;
    Utf8Str         mStrExtraInstallKernelParameters;
    Utf8Str         mStrProxy;
    Utf8Str         mStrAdditionsInstallPackage;

    bool            mfDoneDetectIsoOS;         /**< Set by detectIsoOS(), cleared by setIsoPath(). */
    Utf8Str         mStrDetectedOSTypeId;
    Utf8Str         mStrDetectedOSVersion;
    Utf8Str         mStrDetectedOSFlavor;
    VBOXOSTYPE      mEnmOsType;
    RTCList<RTCString, RTCString *> mDetectedOSLanguages; /**< (only relevant for windows at the moment) */
    Utf8Str         mStrDetectedOSHints;
    RTCList<WIMImage> mDetectedImages;
    bool            mfAvoidUpdatesOverNetwork;
    com::SafeIfaceArray<IGuestOSType> mSupportedGuestOSTypes;
    bool           mfDoneSupportedGuestOSList;
    /** @} */

    // wrapped IUnattended functions:

    /**
     * Checks what mStrIsoPath points to and sets the detectedOS* properties.
     */
    HRESULT detectIsoOS();

    /**
     * Prepare any data, environment, etc.
     */
    HRESULT prepare();

    /**
     * Prepare installation ISO/floppy.
     */
    HRESULT constructMedia();

    /**
     * Prepare a VM to run an unattended installation
     */
    HRESULT reconfigureVM();

    /**
     * Done with all media construction and VM configuration and stuff.
     */
    HRESULT done();

    // wrapped IUnattended attributes:
    HRESULT getIsoPath(com::Utf8Str &isoPath);
    HRESULT setIsoPath(const com::Utf8Str &isoPath);
    HRESULT getUser(com::Utf8Str &user);
    HRESULT setUser(const com::Utf8Str &user);
    HRESULT getUserPassword(com::Utf8Str &password);
    HRESULT setUserPassword(const com::Utf8Str &password);
    HRESULT getAdminPassword(com::Utf8Str &password);
    HRESULT setAdminPassword(const com::Utf8Str &password);
    HRESULT getFullUserName(com::Utf8Str &user);
    HRESULT setFullUserName(const com::Utf8Str &user);
    HRESULT getProductKey(com::Utf8Str &productKey);
    HRESULT setProductKey(const com::Utf8Str &productKey);
    HRESULT getAdditionsIsoPath(com::Utf8Str &additionsIsoPath);
    HRESULT setAdditionsIsoPath(const com::Utf8Str &additionsIsoPath);
    HRESULT getInstallGuestAdditions(BOOL *installGuestAdditions);
    HRESULT setInstallGuestAdditions(BOOL installGuestAdditions);
    HRESULT getValidationKitIsoPath(com::Utf8Str &aValidationKitIsoPath);
    HRESULT setValidationKitIsoPath(const com::Utf8Str &aValidationKitIsoPath);
    HRESULT getInstallTestExecService(BOOL *aInstallTestExecService);
    HRESULT setInstallTestExecService(BOOL aInstallTestExecService);
    HRESULT getUserPayloadIsoPath(com::Utf8Str &aUserPayloadIsoPath);
    HRESULT setUserPayloadIsoPath(const com::Utf8Str &aUserPayloadIsoPath);
    HRESULT getInstallUserPayload(BOOL *aInstallUserPayload);
    HRESULT setInstallUserPayload(BOOL aInstallUserPayload);
    HRESULT getTimeZone(com::Utf8Str &aTimezone);
    HRESULT setTimeZone(const com::Utf8Str &aTimezone);
    HRESULT getKeyboardLayout(com::Utf8Str &aKeyboardLayout);
    HRESULT setKeyboardLayout(const com::Utf8Str &aKeyboardLayout);
    HRESULT getKeyboardVariant(com::Utf8Str &aKeyboardVariant);
    HRESULT setKeyboardVariant(const com::Utf8Str &aKeyboardVariant);
    HRESULT getLocale(com::Utf8Str &aLocale);
    HRESULT setLocale(const com::Utf8Str &aLocale);
    HRESULT getLanguage(com::Utf8Str &aLanguage);
    HRESULT setLanguage(const com::Utf8Str &aLanguage);
    HRESULT getCountry(com::Utf8Str &aCountry);
    HRESULT setCountry(const com::Utf8Str &aCountry);
    HRESULT getProxy(com::Utf8Str &aProxy);
    HRESULT setProxy(const com::Utf8Str &aProxy);
    HRESULT getPackageSelectionAdjustments(com::Utf8Str &aPackageSelectionAdjustments);
    HRESULT setPackageSelectionAdjustments(const com::Utf8Str &aPackageSelectionAdjustments);
    HRESULT getHostname(com::Utf8Str &aHostname);
    HRESULT setHostname(const com::Utf8Str &aHostname);
    HRESULT getAuxiliaryBasePath(com::Utf8Str &aAuxiliaryBasePath);
    HRESULT setAuxiliaryBasePath(const com::Utf8Str &aAuxiliaryBasePath);
    HRESULT getImageIndex(ULONG *index);
    HRESULT setImageIndex(ULONG index);
    HRESULT getMachine(ComPtr<IMachine> &aMachine);
    HRESULT setMachine(const ComPtr<IMachine> &aMachine);
    HRESULT getScriptTemplatePath(com::Utf8Str &aScriptTemplatePath);
    HRESULT setScriptTemplatePath(const com::Utf8Str &aScriptTemplatePath);
    HRESULT getPostInstallScriptTemplatePath(com::Utf8Str &aPostInstallScriptTemplatePath);
    HRESULT setPostInstallScriptTemplatePath(const com::Utf8Str &aPostInstallScriptTemplatePath);
    HRESULT getPostInstallCommand(com::Utf8Str &aPostInstallCommand);
    HRESULT setPostInstallCommand(const com::Utf8Str &aPostInstallCommand);
    HRESULT getExtraInstallKernelParameters(com::Utf8Str &aExtraInstallKernelParameters);
    HRESULT setExtraInstallKernelParameters(const com::Utf8Str &aExtraInstallKernelParameters);
    HRESULT getDetectedOSTypeId(com::Utf8Str &aDetectedOSTypeId);
    HRESULT getDetectedOSVersion(com::Utf8Str &aDetectedOSVersion);
    HRESULT getDetectedOSLanguages(com::Utf8Str &aDetectedOSLanguages);
    HRESULT getDetectedOSFlavor(com::Utf8Str &aDetectedOSFlavor);
    HRESULT getDetectedOSHints(com::Utf8Str &aDetectedOSHints);
    HRESULT getDetectedImageNames(std::vector<com::Utf8Str> &aDetectedImageNames);
    HRESULT getDetectedImageIndices(std::vector<ULONG> &aDetectedImageIndices);
    HRESULT getIsUnattendedInstallSupported(BOOL *aIsUnattendedInstallSupported);
    HRESULT getAvoidUpdatesOverNetwork(BOOL *aAvoidUpdatesOverNetwork);
    HRESULT setAvoidUpdatesOverNetwork(BOOL aAvoidUpdatesOverNetwork);
    //internal functions

    /**
     * Worker for detectIsoOs().
     *
     * @returns COM status code.
     * @retval  S_OK if detected.
     * @retval  S_FALSE if not detected.
     *
     * @param   hVfsIso     The ISO file system handle.
     */
    HRESULT i_innerDetectIsoOS(RTVFS hVfsIso);
    typedef union DETECTBUFFER
    {
        char        sz[4096];
        char        ach[4096];
        uint8_t     ab[4096];
        uint32_t    au32[1024];
    } DETECTBUFFER;
    HRESULT i_innerDetectIsoOSWindows(RTVFS hVfsIso, DETECTBUFFER *puBuf);
    HRESULT i_innerDetectIsoOSLinux(RTVFS hVfsIso, DETECTBUFFER *puBuf);
    HRESULT i_innerDetectIsoOSLinuxFedora(RTVFS hVfsIso, DETECTBUFFER *puBuf, char *pszVolId);
    HRESULT i_innerDetectIsoOSOs2(RTVFS hVfsIso, DETECTBUFFER *puBuf);
    HRESULT i_innerDetectIsoOSFreeBsd(RTVFS hVfsIso, DETECTBUFFER *puBuf);

    /**
     * Worker for reconfigureVM().
     * The caller makes sure to close the session whatever happens.
     */
    HRESULT i_innerReconfigureVM(AutoMultiWriteLock2 &rAutoLock, StorageBus_T enmRecommendedStorageBus,
                                 ComPtr<IMachine> const &rPtrSessionMachine);
    HRESULT i_reconfigureFloppy(com::SafeIfaceArray<IStorageController> &rControllers,
                                std::vector<UnattendedInstallationDisk> &rVecInstallatationDisks,
                                ComPtr<IMachine> const &rPtrSessionMachine,
                                AutoMultiWriteLock2 &rAutoLock);
    HRESULT i_reconfigureIsos(com::SafeIfaceArray<IStorageController> &rControllers,
                              std::vector<UnattendedInstallationDisk> &rVecInstallatationDisks,
                              ComPtr<IMachine> const &rPtrSessionMachine,
                              AutoMultiWriteLock2 &rAutoLock, StorageBus_T enmRecommendedStorageBus);

    /**
     * Adds all free slots on the controller to @a rOutput.
     */
    HRESULT i_findOrCreateNeededFreeSlots(const Utf8Str &rStrControllerName, StorageBus_T enmStorageBus,
                                          ComPtr<IMachine> const &rPtrSessionMachine, uint32_t cSlotsNeeded,
                                          std::list<ControllerSlot> &rDvdSlots);

    /**
     * Attach to VM a disk
     */
    HRESULT i_attachImage(UnattendedInstallationDisk const *pImage, ComPtr<IMachine> const &rPtrSessionMachine,
                          AutoMultiWriteLock2 &rLock);

    /*
     * Wrapper functions
     */

    /**
     * Check whether guest is 64bit platform or not
     */
    bool i_isGuestOSArchX64(Utf8Str const &rStrGuestOsTypeId);

    /**
     * Updates the detected attributes when the image index or image list changes.
     *
     * @returns true if we've got all necessary stuff for a successful detection.
     */
    bool i_updateDetectedAttributeForImage(WIMImage const &rImage);

    /**
     * Gets the list of guest OS type IDs supported by the current host.
     */
    HRESULT i_getListOfSupportedGuestOS();

};

#endif /* !MAIN_INCLUDED_UnattendedImpl_h */
