/* $Id: UINotificationObjects.h 168965 2025-05-22 14:49:38Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - Various UINotificationObjects declarations.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_notificationcenter_UINotificationObjects_h
#define FEQT_INCLUDED_SRC_notificationcenter_UINotificationObjects_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QUuid>

/* GUI includes: */
#include "UIDefs.h"
#include "UIMediumDefs.h"
#include "UINotificationObject.h"

/* COM includes: */
#include "CAppliance.h"
#include "CCloudClient.h"
#include "CCloudMachine.h"
#include "CConsole.h"
#include "CDataStream.h"
#include "CExtPackFile.h"
#include "CExtPackManager.h"
#include "CForm.h"
#include "CFormValue.h"
#include "CGuest.h"
#include "CHost.h"
#include "CHostNetworkInterface.h"
#include "CMachine.h"
#include "CMedium.h"
#include "CSession.h"
#include "CSnapshot.h"
#include "CStringArray.h"
#ifdef VBOX_WITH_UPDATE_AGENT
# include "CUpdateAgent.h"
#endif
#include "CVFSExplorer.h"
#include "CVirtualSystemDescription.h"
#include "CVirtualSystemDescriptionForm.h"

/* Forward declarations: */
class UINotificationCenter;
class CAudioAdapter;
class CCloudProviderManager;
class CCloudProvider;
class CCloudProfile;
class CEmulatedUSB;
class CGuestOSType;
class CKeyboard;
class CMachineDebugger;
class CMediumAttachment;
class CMouse;
class CNetworkAdapter;
class CPlatformProperties;
class CStorageController;
class CSystemProperties;
class CVirtualBox;
class CVirtualBoxErrorInfo;
class CVRDEServer;
class CVRDEServerInfo;
class CUnattended;
#ifdef VBOX_WITH_DRAG_AND_DROP
class CDnDTarget;
#endif

/** UINotificationObject extension for message functionality. */
class SHARED_LIBRARY_STUFF UINotificationMessage : public UINotificationSimple
{
    Q_OBJECT;

public:

    /** @name Simple general warnings.
      * @{ */
        /** Notifies about inability to find help file at certain @a strLocation. */
        static void cannotFindHelpFile(const QString &strLocation);

        /** Notifies about inability to open @a strUrl. */
        static void cannotOpenURL(const QString &strUrl);

        /** Reminds about BETA build. */
        static void remindAboutBetaBuild();
        /** Reminds about BETA build. */
        static void remindAboutExperimentalBuild();
        /** Notifies about invalid encryption password.
          * @param  strPasswordId  Brings password ID. */
        static void warnAboutInvalidEncryptionPassword(const QString &strPasswordId);
        /** Notifies about a clipboard error. */
        static void warnAboutClipboardError(const QString &strMsg);

#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
        /** Notifies about update not found. */
        static void showUpdateNotFound();
        /** Notifies about update successful.
          * @param  strVersion  Brings the found version.
          * @param  strLink     Brings the link to found package. */
        static void showUpdateSuccess(const QString &strVersion, const QString &strLink);
        /** Notifies about extension pack needs to be updated.
          * @param  strExtPackName     Brings the package name.
          * @param  strExtPackVersion  Brings the package version.
          * @param  strVBoxVersion     Brings VBox version. */
        static void askUserToDownloadExtensionPack(const QString &strExtPackName,
                                                   const QString &strExtPackVersion,
                                                   const QString &strVBoxVersion);

        /** Notifies about inability to validate guest additions.
          * @param  strUrl  Brings the GA URL.
          * @param  strSrc  Brings the GA source. */
        static void cannotValidateGuestAdditionsSHA256Sum(const QString &strUrl,
                                                          const QString &strSrc);

        /** Notifies about user manual downloded.
          * @param  strUrl  Brings the GA URL.
          * @param  strSrc  Brings the GA source. */
        static void warnAboutUserManualDownloaded(const QString &strUrl,
                                                  const QString &strTarget);

        /** Notifies about inability to validate guest additions.
          * @param  strUrl  Brings the GA URL.
          * @param  strSrc  Brings the GA source. */
        static void cannotValidateExtentionPackSHA256Sum(const QString &strExtPackName,
                                                         const QString &strFrom,
                                                         const QString &strTo);
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
    /** @} */

    /** @name Simple VirtualBox Manager warnings.
      * @{ */
        /** Notifies about inability to create machine folder.
          * @param  strPath  Brings the machine folder path. */
        static void cannotCreateMachineFolder(const QString &strPath,
                                              UINotificationCenter *pParent = 0);
        /** Notifies about inability to overwrite machine folder.
          * @param  strPath  Brings the machine folder path. */
        static void cannotOverwriteMachineFolder(const QString &strPath,
                                                 UINotificationCenter *pParent = 0);
        /** Notifies about inability to remove machine folder.
          * @param  strPath  Brings the machine folder path. */
        static void cannotRemoveMachineFolder(const QString &strPath,
                                              UINotificationCenter *pParent = 0);

        /** Notifies about inability to register existing machine.
          * @param  streName     Brings the machine name.
          * @param  strLocation  Brings the machine location. */
        static void cannotReregisterExistingMachine(const QString &strName, const QString &strLocation);

        /** Notifies about inability to resolve collision automatically.
          * @param  strCollisionName  Brings the collision VM name.
          * @param  strGroupName      Brings the group name. */
        static void cannotResolveCollisionAutomatically(const QString &strCollisionName, const QString &strGroupName);

        /** Notifies about inability to acquire cloud machine settings.
          * @param  strErrorDetails  Brings the error details. */
        static void cannotAcquireCloudMachineSettings(const QString &strErrorDetails);

        /** Notifies about inability to create medium storage in FAT.
          * @param  strPath  Brings the medium path. */
        static void cannotCreateMediumStorageInFAT(const QString &strPath,
                                                   UINotificationCenter *pParent = 0);
        /** Notifies about inability to overwrite medium storage.
          * @param  strPath  Brings the medium path. */
        static void cannotOverwriteMediumStorage(const QString &strPath,
                                                 UINotificationCenter *pParent = 0);

        /** Notifies about inability to open license file.
          * @param  strPath  Brings the license file path. */
        static void cannotOpenLicenseFile(const QString &strPath);

        /** Notifies about public key path is empty. */
        static void warnAboutPublicKeyFilePathIsEmpty();
        /** Notifies about public key file doesn't exist.
          * @param  strPath  Brings the path being checked. */
        static void warnAboutPublicKeyFileDoesntExist(const QString &strPath);
        /** Notifies about public key file is of too large size.
          * @param  strPath  Brings the path being checked. */
        static void warnAboutPublicKeyFileIsOfTooLargeSize(const QString &strPath);
        /** Notifies about public key file isn't readable.
          * @param  strPath  Brings the path being checked. */
        static void warnAboutPublicKeyFileIsntReadable(const QString &strPath);

        /** Notifies about DHCP server isn't enabled.
          * @param  strName  Brings the interface name. */
        static void warnAboutDHCPServerIsNotEnabled(const QString &strName);
        /** Notifies about invalid IPv4 address.
          * @param  strName  Brings the interface name. */
        static void warnAboutInvalidIPv4Address(const QString &strName);
        /** Notifies about invalid IPv4 mask.
          * @param  strName  Brings the interface name. */
        static void warnAboutInvalidIPv4Mask(const QString &strName);
        /** Notifies about invalid IPv6 address.
          * @param  strName  Brings the interface name. */
        static void warnAboutInvalidIPv6Address(const QString &strName);
        /** Notifies about invalid IPv6 prefix length.
          * @param  strName  Brings the interface name. */
        static void warnAboutInvalidIPv6PrefixLength(const QString &strName);
        /** Notifies about invalid DHCP server address.
          * @param  strName  Brings the interface name. */
        static void warnAboutInvalidDHCPServerAddress(const QString &strName);
        /** Notifies about invalid DHCP server mask.
          * @param  strName  Brings the interface name. */
        static void warnAboutInvalidDHCPServerMask(const QString &strName);
        /** Notifies about invalid DHCP server lower address.
          * @param  strName  Brings the interface name. */
        static void warnAboutInvalidDHCPServerLowerAddress(const QString &strName);
        /** Notifies about invalid DHCP server upper address.
          * @param  strName  Brings the interface name. */
        static void warnAboutInvalidDHCPServerUpperAddress(const QString &strName);
        /** Notifies about no name specified.
          * @param  strName  Brings the interface name. */
        static void warnAboutNoNameSpecified(const QString &strName);
        /** Notifies about name already busy.
          * @param  strName  Brings the interface name. */
        static void warnAboutNameAlreadyBusy(const QString &strName);
        /** Notifies about no IPv4 prefix specified.
          * @param  strName  Brings the interface name. */
        static void warnAboutNoIPv4PrefixSpecified(const QString &strName);
        /** Notifies about no IPv6 prefix specified.
          * @param  strName  Brings the interface name. */
        static void warnAboutNoIPv6PrefixSpecified(const QString &strName);
    /** @} */

    /** @name Simple Runtime UI warnings.
      * @{ */
        /** Notifies about inability to mount image.
          * @param  strMachineName  Brings the machine name.
          * @param  strMediumName   Brings the medium name. */
        static void cannotMountImage(const QString &strMachineName, const QString &strMediumName);

        /** Notifies about inability to send ACPI shutdown. */
        static void cannotSendACPIToMachine();

        /** Reminds about keyboard auto capturing. */
        static void remindAboutAutoCapture();

        /** Reminds about GA not affected. */
        static void remindAboutGuestAdditionsAreNotActive();

        /** Reminds about mouse integration.
          * @param  fSupportsAbsolute  Brings whether mouse supports absolute pointing. */
        static void remindAboutMouseIntegration(bool fSupportsAbsolute);

        /** Reminds about paused VM input. */
        static void remindAboutPausedVMInput();
        /** Revokes message about paused VM input. */
        static void forgetAboutPausedVMInput();

        /** Reminds about wrong color depth.
          * @param  uRealBPP    Brings real bit per pixel value.
          * @param  uWantedBPP  Brings wanted bit per pixel value. */
        static void remindAboutWrongColorDepth(ulong uRealBPP, ulong uWantedBPP);
        /** Revokes message about wrong color depth. */
        static void forgetAboutWrongColorDepth();
    /** @} */

    /** @name COM general warnings.
      * @{ */
        /** Notifies about inability to acquire IVirtualBox parameter.
          * @param  comVBox  Brings the object parameter get acquired from. */
        static void cannotAcquireVirtualBoxParameter(const CVirtualBox &comVBox,
                                                     UINotificationCenter *pParent = 0);
        /** Notifies about inability to acquire IAppliance parameter.
          * @param  comVBox  Brings the object parameter get acquired from. */
        static void cannotAcquireApplianceParameter(const CAppliance &comAppliance,
                                                    UINotificationCenter *pParent = 0);
        /** Notifies about inability to acquire IPlatform parameter.
          * @param  comPlatform  Brings the object parameter get acquired from. */
        static void cannotAcquirePlatformParameter(const CPlatform &comPlatform);
        /** Notifies about inability to acquire IPlatformProperties parameter.
          * @param  comProperties  Brings the object parameter get acquired from. */
        static void cannotAcquirePlatformPropertiesParameter(const CPlatformProperties &comProperties);
        /** Notifies about inability to acquire ISystemProperties parameter.
          * @param  comProperties  Brings the object parameter get acquired from. */
        static void cannotAcquireSystemPropertiesParameter(const CSystemProperties &comProperties);
        /** Notifies about inability to acquire IExtPackManager parameter.
          * @param  comVBox  Brings the object parameter get acquired from. */
        static void cannotAcquireExtensionPackManagerParameter(const CExtPackManager &comEPManager);
        /** Notifies about inability to acquire IExtPack parameter.
          * @param  comPackage  Brings the object parameter get acquired from. */
        static void cannotAcquireExtensionPackParameter(const CExtPack &comPackage);
        /** Notifies about inability to acquire IHost parameter.
          * @param  comHost  Brings the object parameter get acquired from. */
        static void cannotAcquireHostParameter(const CHost &comHost);
        /** Notifies about inability to acquire IStorageController parameter.
          * @param  comStorageController  Brings the object parameter get acquired from. */
        static void cannotAcquireStorageControllerParameter(const CStorageController &comStorageController);
        /** Notifies about inability to change IStorageController parameter.
          * @param  comStorageController  Brings the object parameter being changed for. */
        static void cannotChangeStorageControllerParameter(const CStorageController &comStorageController);
        /** Notifies about inability to acquire IMediumAttachment parameter.
          * @param  comMediumAttachment  Brings the object parameter get acquired from. */
        static void cannotAcquireMediumAttachmentParameter(const CMediumAttachment &comMediumAttachment);
        /** Notifies about inability to acquire IMedium parameter.
          * @param  comMedium  Brings the object parameter get acquired from. */
        static void cannotAcquireMediumParameter(const CMedium &comMedium);
        /** Notifies about inability to acquire ISession parameter.
          * @param  comSession  Brings the object parameter get acquired from. */
        static void cannotAcquireSessionParameter(const CSession &comSession);
        /** Notifies about inability to acquire IMachine parameter.
          * @param  comMachine  Brings the object parameter get acquired from. */
        static void cannotAcquireMachineParameter(const CMachine &comMachine);
        /** Notifies about inability to acquire IMachineDebugger parameter.
          * @param  comMachineDebugger  Brings the object parameter get acquired from. */
        static void cannotAcquireMachineDebuggerParameter(const CMachineDebugger &comMachineDebugger);
        /** Notifies about inability to acquire IGraphicsAdapter parameter.
          * @param  comAdapter  Brings the object parameter get acquired from. */
        static void cannotAcquireGraphicsAdapterParameter(const CGraphicsAdapter &comAdapter);
        /** Notifies about inability to acquire IAudioSettings parameter.
          * @param  comSettings  Brings the object parameter get acquired from. */
        static void cannotAcquireAudioSettingsParameter(const CAudioSettings &comSettings);
        /** Notifies about inability to acquire IAudioAdapter parameter.
          * @param  comAdapter  Brings the object parameter get acquired from. */
        static void cannotAcquireAudioAdapterParameter(const CAudioAdapter &comAdapter);
        /** Notifies about inability to acquire INetworkAdapter parameter.
          * @param  comAdapter  Brings the object parameter get acquired from. */
        static void cannotAcquireNetworkAdapterParameter(const CNetworkAdapter &comAdapter);
        /** Notifies about inability to acquire IConsole parameter.
          * @param  comConsole  Brings the object parameter get acquired from. */
        static void cannotAcquireConsoleParameter(const CConsole &comConsole);
        /** Notifies about inability to acquire IGuest parameter.
          * @param  comGuest  Brings the object parameter get acquired from. */
        static void cannotAcquireGuestParameter(const CGuest &comGuest);
        /** Notifies about inability to acquire IGuestOSType parameter.
          * @param  comGuestOSType  Brings the object parameter get acquired from. */
        static void cannotAcquireGuestOSTypeParameter(const CGuestOSType &comGuestOSType);
        /** Notifies about inability to acquire ISnapshot parameter.
          * @param  comSnapshot  Brings the object parameter get acquired from. */
        static void cannotAcquireSnapshotParameter(const CSnapshot &comSnapshot);
        /** Notifies about inability to acquire IDHCPServer parameter.
          * @param  comServer  Brings the object parameter get acquired from. */
        static void cannotAcquireDHCPServerParameter(const CDHCPServer &comServer);
        /** Notifies about inability to acquire ICloudNetwork parameter.
          * @param  comNetwork  Brings the object parameter get acquired from. */
        static void cannotAcquireCloudNetworkParameter(const CCloudNetwork &comNetwork);
        /** Notifies about inability to acquire IHostNetworkInterface parameter.
          * @param  comInterface  Brings the object parameter get acquired from. */
        static void cannotAcquireHostNetworkInterfaceParameter(const CHostNetworkInterface &comInterface);
        /** Notifies about inability to acquire IHostOnlyNetwork parameter.
          * @param  comNetwork  Brings the object parameter get acquired from. */
        static void cannotAcquireHostOnlyNetworkParameter(const CHostOnlyNetwork &comNetwork);
        /** Notifies about inability to acquire INATNetwork parameter.
          * @param  comNetwork  Brings the object parameter get acquired from. */
        static void cannotAcquireNATNetworkParameter(const CNATNetwork &comNetwork);
        /** Notifies about inability to acquire IDisplay parameter.
          * @param  comDisplay  Brings the object parameter get acquired from. */
        static void cannotAcquireDisplayParameter(const CDisplay &comDisplay);
        /** Notifies about inability to acquire IUpdateAgent parameter.
          * @param  comAgent  Brings the object parameter get acquired from. */
        static void cannotAcquireUpdateAgentParameter(const CUpdateAgent &comAgent);
        /** Notifies about inability to acquire IMouse parameter.
          * @param  comMouse  Brings the object parameter get acquired from. */
        static void cannotAcquireMouseParameter(const CMouse &comMouse);
        /** Notifies about inability to acquire IEmulatedUSB parameter.
          * @param  comDispatcher  Brings the object parameter get acquired from. */
        static void cannotAcquireEmulatedUSBParameter(const CEmulatedUSB &comDispatcher);
        /** Notifies about inability to acquire IRecordingSettings parameter.
          * @param  comSettings  Brings the object parameter get acquired from. */
        static void cannotAcquireRecordingSettingsParameter(const CRecordingSettings &comSettings);
        /** Notifies about inability to acquire IVRDEServer parameter.
          * @param  comServer  Brings the object parameter get acquired from. */
        static void cannotAcquireVRDEServerParameter(const CVRDEServer &comServer);
        /** Notifies about inability to acquire IVRDEServerInfo parameter.
          * @param  comVRDEServerInfo  Brings the object parameter get acquired from. */
        static void cannotAcquireVRDEServerInfoParameter(const CVRDEServerInfo &comVRDEServerInfo);
        /** Notifies about inability to acquire IVirtualSystemDescription parameter.
          * @param  comVsd  Brings the object parameter get acquired from. */
        static void cannotAcquireVirtualSystemDescriptionParameter(const CVirtualSystemDescription &comVsd,
                                                                   UINotificationCenter *pParent = 0);
        /** Notifies about inability to acquire IVirtualSystemDescriptionForm parameter.
          * @param  comVsdForm  Brings the object parameter get acquired from. */
        static void cannotAcquireVirtualSystemDescriptionFormParameter(const CVirtualSystemDescriptionForm &comVsdForm,
                                                                       UINotificationCenter *pParent = 0);
        /** Notifies about inability to acquire ICloudProviderManager parameter.
          * @param  comCloudProviderManager  Brings the object parameter get acquired from. */
        static void cannotAcquireCloudProviderManagerParameter(const CCloudProviderManager &comCloudProviderManager,
                                                               UINotificationCenter *pParent = 0);
        /** Notifies about inability to acquire ICloudProvider parameter.
          * @param  comCloudProvider  Brings the object parameter get acquired from. */
        static void cannotAcquireCloudProviderParameter(const CCloudProvider &comCloudProvider,
                                                        UINotificationCenter *pParent = 0);
        /** Notifies about inability to acquire ICloudProfile parameter.
          * @param  comCloudProfile  Brings the object parameter get acquired from. */
        static void cannotAcquireCloudProfileParameter(const CCloudProfile &comCloudProfile,
                                                       UINotificationCenter *pParent = 0);
        /** Notifies about inability to acquire ICloudMachine parameter.
          * @param  comCloudMachine  Brings the object parameter get acquired from. */
        static void cannotAcquireCloudMachineParameter(const CCloudMachine &comCloudMachine,
                                                       UINotificationCenter *pParent = 0);

        /** Notifies about inability to change IMedium parameter.
          * @param  comMedium  Brings the object parameter being changed for. */
        static void cannotChangeMediumParameter(const CMedium &comMedium);
        /** Notifies about inability to change IMachine parameter.
          * @param  comMachine  Brings the object parameter being changed for. */
        static void cannotChangeMachineParameter(const CMachine &comMachine);
        /** Notifies about inability to change IMachineDebugger parameter.
          * @param  comMachineDebugger  Brings the object parameter being changed for. */
        static void cannotChangeMachineDebuggerParameter(const CMachineDebugger &comMachineDebugger);
        /** Notifies about inability to change IGraphicsAdapter parameter.
          * @param  comAdapter  Brings the object parameter being changed for. */
        static void cannotChangeGraphicsAdapterParameter(const CGraphicsAdapter &comAdapter);
        /** Notifies about inability to change IAudioAdapter parameter.
          * @param  comAdapter  Brings the object parameter being changed for. */
        static void cannotChangeAudioAdapterParameter(const CAudioAdapter &comAdapter);
        /** Notifies about inability to change INetworkAdapter parameter.
          * @param  comAdapter  Brings the object parameter being changed for. */
        static void cannotChangeNetworkAdapterParameter(const CNetworkAdapter &comAdapter);
        /** Notifies about inability to change IDHCPServer parameter.
          * @param  comServer  Brings the object parameter being changed for. */
        static void cannotChangeDHCPServerParameter(const CDHCPServer &comServer);
        /** Notifies about inability to change ICloudNetwork parameter.
          * @param  comNetwork  Brings the object parameter being changed for. */
        static void cannotChangeCloudNetworkParameter(const CCloudNetwork &comNetwork);
        /** Notifies about inability to change IHostNetworkInterface parameter.
          * @param  comInterface  Brings the object parameter being changed for. */
        static void cannotChangeHostNetworkInterfaceParameter(const CHostNetworkInterface &comInterface);
        /** Notifies about inability to change IHostOnlyNetwork parameter.
          * @param  comNetwork  Brings the object parameter being changed for. */
        static void cannotChangeHostOnlyNetworkParameter(const CHostOnlyNetwork &comNetwork);
        /** Notifies about inability to change INATNetwork parameter.
          * @param  comNetwork  Brings the object parameter being changed for. */
        static void cannotChangeNATNetworkParameter(const CNATNetwork &comNetwork);
        /** Notifies about inability to change IDisplay parameter.
          * @param  comDisplay  Brings the object parameter being changed for. */
        static void cannotChangeDisplayParameter(const CDisplay &comDisplay);
        /** Notifies about inability to change ICloudProfile parameter.
          * @param  comProfile  Brings the object parameter being changed for. */
        static void cannotChangeCloudProfileParameter(const CCloudProfile &comProfile);
        /** Notifies about inability to change IUpdateAgent parameter.
          * @param  comAgent  Brings the object parameter being changed for. */
        static void cannotChangeUpdateAgentParameter(const CUpdateAgent &comAgent);
        /** Notifies about inability to change IKeyboard parameter.
          * @param  comKeyboard  Brings the object parameter being changed for. */
        static void cannotChangeKeyboardParameter(const CKeyboard &comKeyboard);
        /** Notifies about inability to change IVirtualSystemDescription parameter.
          * @param  comVsd  Brings the object parameter being changed for. */
        static void cannotChangeVirtualSystemDescriptionParameter(const CVirtualSystemDescription &comVsd,
                                                                  UINotificationCenter *pParent = 0);

        /** Notifies about inability to enumerate host USB devices.
          * @param  comHost  Brings the host devices enumerated for. */
        static void cannotEnumerateHostUSBDevices(const CHost &comHost);
        /** Notifies about inability to open medium.
          * @param  comVBox      Brings common VBox object trying to open medium.
          * @param  strLocation  Brings the medium location. */
        static void cannotOpenMedium(const CVirtualBox &comVBox, const QString &strLocation,
                                     UINotificationCenter *pParent = 0);

        /** Notifies about inability to pause machine.
          * @param  comConsole  Brings console trying to pause machine. */
        static void cannotPauseMachine(const CConsole &comConsole);
        /** Notifies about inability to resume machine.
          * @param  comConsole  Brings console trying to resume machine. */
        static void cannotResumeMachine(const CConsole &comConsole);
        /** Notifies about inability to ACPI shutdown machine.
          * @param  comConsole  Brings console trying to shutdown machine. */
        static void cannotACPIShutdownMachine(const CConsole &comConsole);
        /** Notifies about inability to reset machine.
          * @param  comConsole  Brings console trying to reset machine. */
        static void cannotResetMachine(const CConsole &comConsole);
    /** @} */

    /** @name COM VirtualBox Manager warnings.
      * @{ */
        /** Notifies about inability to create appliance.
          * @param  comVBox  Brings common VBox object trying to create appliance. */
        static void cannotCreateAppliance(const CVirtualBox &comVBox, UINotificationCenter *pParent = 0);
        /** Notifies about inability to register machine.
          * @param  comVBox  Brings common VBox object trying to register machine.
          * @param  strName  Brings the name of VM being registered. */
        static void cannotRegisterMachine(const CVirtualBox &comVBox, const QString &strName, UINotificationCenter *pParent = 0);
        /** Notifies about inability to create machine.
          * @param  comVBox  Brings common VBox object trying to create machine. */
        static void cannotCreateMachine(const CVirtualBox &comVBox, UINotificationCenter *pParent = 0);
        /** Notifies about inability to find machine by ID.
          * @param  comVBox     Brings common VBox object trying to find machine.
          * @param  uMachineId  Brings the machine ID. */
        static void cannotFindMachineById(const CVirtualBox &comVBox,
                                          const QUuid &uMachineId,
                                          UINotificationCenter *pParent = 0);
        /** Notifies about inability to open machine.
          * @param  comVBox      Brings common VBox object trying to open machine.
          * @param  strLocation  Brings the machine location. */
        static void cannotOpenMachine(const CVirtualBox &comVBox, const QString &strLocation);
        /** Notifies about inability to create medium storage.
          * @param  comVBox  Brings common VBox object trying to create medium storage.
          * @param  strPath  Brings the medium path. */
        static void cannotCreateMediumStorage(const CVirtualBox &comVBox,
                                              const QString &strPath,
                                              UINotificationCenter *pParent = 0);
        /** Notifies about inability to get ext pack manager.
          * @param  comVBox      Brings common VBox object trying to open machine. */
        static void cannotGetExtensionPackManager(const CVirtualBox &comVBox);

        /** Notifies about inability to create VFS explorer.
          * @param  comAppliance  Brings appliance trying to create VFS explorer. */
        static void cannotCreateVfsExplorer(const CAppliance &comAppliance, UINotificationCenter *pParent = 0);
        /** Notifies about inability to add disk scryption password.
          * @param  comAppliance  Brings appliance trying to add disk scryption password. */
        static void cannotAddDiskEncryptionPassword(const CAppliance &comAppliance, UINotificationCenter *pParent = 0);
        /** Notifies about inability to interpret appliance.
          * @param  comAppliance  Brings appliance we are trying to interpret. */
        static void cannotInterpretAppliance(const CAppliance &comAppliance, UINotificationCenter *pParent = 0);
        /** Notifies about inability to create VSD.
          * @param  comAppliance  Brings appliance trying to create VSD. */
        static void cannotCreateVirtualSystemDescription(const CAppliance &comAppliance, UINotificationCenter *pParent = 0);

        /** Notifies about inability to open extension pack.
          * @param  comExtPackManager  Brings extension pack manager trying to open extension pack.
          * @param  strFilename        Brings extension pack file name. */
        static void cannotOpenExtPack(const CExtPackManager &comExtPackManager, const QString &strFilename);
        /** Notifies about inability to read extpack file.
          * @param  comExtPackFile  Brings extension pack manager trying to open extension pack.
          * @param  strFilename     Brings extension pack file name. */
        static void cannotReadExtPack(const CExtPackFile &comExtPackFile, const QString &strFilename);

        /** Notifies about inability to find cloud network.
          * @param  comVBox         Brings common VBox object being search through.
          * @param  strNetworkName  Brings network name. */
        static void cannotFindCloudNetwork(const CVirtualBox &comVBox, const QString &strNetworkName);
        /** Notifies about inability to find host network interface.
          * @param  comHost           Brings the host being search through.
          * @param  strInterfaceName  Brings interface name. */
        static void cannotFindHostNetworkInterface(const CHost &comHost, const QString &strInterfaceName);
        /** Notifies about inability to find host only network.
          * @param  comVBox         Brings the common VBox object being search through.
          * @param  strNetworkName  Brings interface name. */
        static void cannotFindHostOnlyNetwork(const CVirtualBox &comVBox, const QString &strNetworkName);
        /** Notifies about inability to find NAT network.
          * @param  comVBox         Brings common VBox object being search through.
          * @param  strNetworkName  Brings network name. */
        static void cannotFindNATNetwork(const CVirtualBox &comVBox, const QString &strNetworkName);
        /** Notifies about inability to create DHCP server.
          * @param  comVBox           Brings common VBox object trying to create DHCP server.
          * @param  strInterfaceName  Brings the interface name. */
        static void cannotCreateDHCPServer(const CVirtualBox &comVBox, const QString &strInterfaceName);
        /** Notifies about inability to remove DHCP server.
          * @param  comVBox           Brings common VBox object trying to remove DHCP server.
          * @param  strInterfaceName  Brings the interface name. */
        static void cannotRemoveDHCPServer(const CVirtualBox &comVBox, const QString &strInterfaceName);
        /** Notifies about inability to create cloud network.
          * @param  comVBox  Brings common VBox object trying to create cloud network. */
        static void cannotCreateCloudNetwork(const CVirtualBox &comVBox);
        /** Notifies about inability to remove cloud network.
          * @param  comVBox         Brings common VBox object trying to remove cloud network.
          * @param  strNetworkName  Brings the network name. */
        static void cannotRemoveCloudNetwork(const CVirtualBox &comVBox, const QString &strNetworkName);
        /** Notifies about inability to create host only network.
          * @param  comVBox  Brings common VBox object trying to create host only network. */
        static void cannotCreateHostOnlyNetwork(const CVirtualBox &comVBox);
        /** Notifies about inability to remove host only network.
          * @param  comVBox         Brings common VBox object trying to remove host only network.
          * @param  strNetworkName  Brings the network name. */
        static void cannotRemoveHostOnlyNetwork(const CVirtualBox &comVBox, const QString &strNetworkName);
        /** Notifies about inability to create NAT network.
          * @param  comVBox  Brings common VBox object trying to create NAT network. */
        static void cannotCreateNATNetwork(const CVirtualBox &comVBox);
        /** Notifies about inability to remove NAT network.
          * @param  comVBox         Brings common VBox object trying to remove NAT network.
          * @param  strNetworkName  Brings the network name. */
        static void cannotRemoveNATNetwork(const CVirtualBox &comVBox, const QString &strNetworkName);

        /** Notifies about inability to create cloud profile.
          * @param  comProvider  Brings the provider profile being created for. */
        static void cannotCreateCloudProfile(const CCloudProvider &comProvider);
        /** Notifies about inability to remove cloud profile.
          * @param  comProvider  Brings the provider profile being removed from. */
        static void cannotRemoveCloudProfile(const CCloudProfile &comProfile);
        /** Notifies about inability to save cloud profiles.
          * @param  comProvider  Brings the provider profiles being saved for. */
        static void cannotSaveCloudProfiles(const CCloudProvider &comProvider);
        /** Notifies about inability to import cloud profiles.
          * @param  comProvider  Brings the provider profiles being imported for. */
        static void cannotImportCloudProfiles(const CCloudProvider &comProvider);
        /** Notifies about inability to refresh cloud machine.
          * @param  comMachine  Brings the machine being refreshed. */
        static void cannotRefreshCloudMachine(const CCloudMachine &comMachine);
        /** Notifies about inability to refresh cloud machine.
          * @param  comProgress  Brings the progress of machine being refreshed. */
        static void cannotRefreshCloudMachine(const CProgress &comProgress);
        /** Notifies about inability to create cloud client.
          * @param  comProfile  Brings the profile client being created for. */
        static void cannotCreateCloudClient(const CCloudProfile &comProfile, UINotificationCenter *pParent = 0);

        /** Notifies about inability to open machine.
          * @param  comMedium  Brings the medium being closed. */
        static void cannotCloseMedium(const CMedium &comMedium);

        /** Notifies about inability to discard saved state.
          * @param  comMachine  Brings the collision VM name. */
        static void cannotDiscardSavedState(const CMachine &comMachine);
        /** Notifies about inability to remove machine.
          * @param  comMachine  Brings machine being removed. */
        static void cannotRemoveMachine(const CMachine &comMachine, UINotificationCenter *pParent = 0);
        /** Notifies about inability to export appliance.
          * @param  comMachine  Brings machine trying to export appliance. */
        static void cannotExportMachine(const CMachine &comMachine, UINotificationCenter *pParent = 0);
        /** Notifies about inability to attach device.
          * @param  comMachine  Brings machine trying to attach device. */
        static void cannotAttachDevice(const CMachine &comMachine,
                                       UIMediumDeviceType enmType,
                                       const QString &strLocation,
                                       const StorageSlot &storageSlot,
                                       UINotificationCenter *pParent = 0);

        /** Notifies about inability to find snapshot by ID.
          * @param  comMachine  Brings the machine being searched for particular snapshot.
          * @param  uId         Brings the required snapshot ID. */
        static void cannotFindSnapshotById(const CMachine &comMachine, const QUuid &uId);
        /** Notifies about inability to find snapshot by name.
          * @param  comMachine  Brings the machine being searched for particular snapshot.
          * @param  strName     Brings the required snapshot name. */
        static void cannotFindSnapshotByName(const CMachine &comMachine, const QString &strName, UINotificationCenter *pParent = 0);
        /** Notifies about inability to change snapshot.
          * @param  comSnapshot      Brings the snapshot being changed.
          * @param  strSnapshotName  Brings snapshot name.
          * @param  strMachineName   Brings machine name. */
        static void cannotChangeSnapshot(const CSnapshot &comSnapshot, const QString &strSnapshotName, const QString &strMachineName);

        /** Notifies about inability to run unattended guest install.
          * @param  comUnattended  Brings the unattended being running guest install. */
        static void cannotRunUnattendedGuestInstall(const CUnattended &comUnattended);
    /** @} */

    /** @name COM Runtime UI warnings.
      * @{ */
        /** Notifies about inability to attach USB device.
          * @param  comConsole  Brings console USB device belongs to.
          * @param  strDevice   Brings the device name. */
        static void cannotAttachUSBDevice(const CConsole &comConsole, const QString &strDevice);
        /** Notifies about inability to attach USB device.
          * @param  comErrorInfo    Brings info about error happened.
          * @param  strDevice       Brings the device name.
          * @param  strMachineName  Brings the machine name. */
        static void cannotAttachUSBDevice(const CVirtualBoxErrorInfo &comErrorInfo,
                                          const QString &strDevice, const QString &strMachineName);
        /** Notifies about inability to detach USB device.
          * @param  comConsole  Brings console USB device belongs to.
          * @param  strDevice   Brings the device name. */
        static void cannotDetachUSBDevice(const CConsole &comConsole, const QString &strDevice);
        /** Notifies about inability to detach USB device.
          * @param  comErrorInfo    Brings info about error happened.
          * @param  strDevice       Brings the device name.
          * @param  strMachineName  Brings the machine name. */
        static void cannotDetachUSBDevice(const CVirtualBoxErrorInfo &comErrorInfo,
                                          const QString &strDevice, const QString &strMachineName);

        /** Notifies about inability to attach webcam.
          * @param  comDispatcher   Brings emulated USB dispatcher webcam being attached to.
          * @param  strWebCamName   Brings the webcam name.
          * @param  strMachineName  Brings the machine name. */
        static void cannotAttachWebCam(const CEmulatedUSB &comDispatcher,
                                       const QString &strWebCamName, const QString &strMachineName);
        /** Notifies about inability to detach webcam.
          * @param  comDispatcher   Brings emulated USB dispatcher webcam being detached from.
          * @param  strWebCamName   Brings the webcam name.
          * @param  strMachineName  Brings the machine name. */
        static void cannotDetachWebCam(const CEmulatedUSB &comDispatcher,
                                       const QString &strWebCamName, const QString &strMachineName);

        /** Notifies about inability to save machine settings.
          * @param  comMachine  Brings the machine trying to save settings. */
        static void cannotSaveMachineSettings(const CMachine &comMachine, UINotificationCenter *pParent = 0);

        /** Notifies about inability to toggle audio input.
          * @param  comAdapter      Brings the adapter input being toggled for.
          * @param  strMachineName  Brings the machine name.
          * @param  fEnable         Brings whether adapter input is enabled or not. */
        static void cannotToggleAudioInput(const CAudioAdapter &comAdapter,
                                           const QString &strMachineName, bool fEnable);
        /** Notifies about inability to toggle audio output.
          * @param  comAdapter      Brings the adapter output being toggled for.
          * @param  strMachineName  Brings the machine name.
          * @param  fEnable         Brings whether adapter output is enabled or not. */
        static void cannotToggleAudioOutput(const CAudioAdapter &comAdapter,
                                            const QString &strMachineName, bool fEnable);

        /** Notifies about inability to toggle network cable.
          * @param  comAdapter      Brings the adapter network cable being toggled for.
          * @param  strMachineName  Brings the machine name.
          * @param  fConnect        Brings whether network cable is connected or not. */
        static void cannotToggleNetworkCable(const CNetworkAdapter &comAdapter,
                                             const QString &strMachineName, bool fConnect);

        /** Notifies about inability to toggle recording.
          * @param  comRecording    Brings the recording settings being toggled for.
          * @param  strMachineName  Brings the machine name.
          * @param  fEnable         Brings whether recording is enabled or not. */
        static void cannotToggleRecording(const CRecordingSettings &comRecording, const QString &strMachineName, bool fEnable);

        /** Notifies about inability to toggle VRDE server.
          * @param  comServer       Brings the server being toggled.
          * @param  strMachineName  Brings the machine name.
          * @param  fEnable         Brings whether server is enabled or not. */
        static void cannotToggleVRDEServer(const CVRDEServer &comServer,
                                           const QString &strMachineName, bool fEnable);

#ifdef VBOX_WITH_DRAG_AND_DROP
        /** Notifies about inability to drop data to guest.
          * @param  comDndTarget  Brings the data being dropped. */
        static void cannotDropDataToGuest(const CDnDTarget &comDndTarget);
        /** Notifies about inability to drop data to guest.
          * @param  comProgress  Brings the drop-progress being executed. */
        static void cannotDropDataToGuest(const CProgress &comProgress);
#endif /* VBOX_WITH_DRAG_AND_DROP */
    /** @} */

protected:

    /** Constructs message notification-object.
      * @param  strName          Brings the message name.
      * @param  strDetails       Brings the message details.
      * @param  strInternalName  Brings the message internal name.
      * @param  strHelpKeyword   Brings the message help keyword. */
    UINotificationMessage(const QString &strName,
                          const QString &strDetails,
                          const QString &strInternalName,
                          const QString &strHelpKeyword);
    /** Destructs message notification-object. */
    virtual ~UINotificationMessage() RT_OVERRIDE RT_FINAL;

private:

    /** Creates message.
      * @param  strName          Brings the message name.
      * @param  strDetails       Brings the message details.
      * @param  strInternalName  Brings the message internal name.
      * @param  strHelpKeyword   Brings the message help keyword.
      * @param  pParent          Brings the local notification-center reference. */
    static void createMessage(const QString &strName,
                              const QString &strDetails,
                              const QString &strInternalName = QString(),
                              const QString &strHelpKeyword = QString(),
                              UINotificationCenter *pParent = 0);
    /** Destroys message.
      * @param  strInternalName  Brings the message internal name.
      * @param  pParent          Brings the local notification-center reference. */
    static void destroyMessage(const QString &strInternalName,
                               UINotificationCenter *pParent = 0);

    /** Holds the IDs of messages registered. */
    static QMap<QString, QUuid>  m_messages;

    /** Holds the message name. */
    QString  m_strName;
    /** Holds the message details. */
    QString  m_strDetails;
    /** Holds the message internal name. */
    QString  m_strInternalName;
};

/** UINotificationProgress extension for medium create functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressMediumCreate : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about @a comMedium was created. */
    void sigMediumCreated(const CMedium &comMedium);

public:

    /** Constructs medium create notification-progress.
      * @param  comTarget  Brings the medium being the target.
      * @param  uSize      Brings the target medium size.
      * @param  variants   Brings the target medium options. */
    UINotificationProgressMediumCreate(const CMedium &comTarget,
                                       qulonglong uSize,
                                       const QVector<KMediumVariant> &variants);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the medium being the target. */
    CMedium                  m_comTarget;
    /** Holds the target location. */
    QString                  m_strLocation;
    /** Holds the target medium size. */
    qulonglong               m_uSize;
    /** Holds the target medium options. */
    QVector<KMediumVariant>  m_variants;
};

/** UINotificationProgress extension for medium copy functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressMediumCopy : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about @a comMedium was copied. */
    void sigMediumCopied(const CMedium &comMedium);

public:

    /** Constructs medium copy notification-progress.
      * @param  comSource    Brings the medium being copied.
      * @param  comTarget    Brings the medium being the target.
      * @param  variants     Brings the target medium options.
      * @param  uMediumSize  Brings the target medium size.
      */
    UINotificationProgressMediumCopy(const CMedium &comSource,
                                     const CMedium &comTarget,
                                     const QVector<KMediumVariant> &variants,
                                     qulonglong uMediumSize);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the medium being copied. */
    CMedium                  m_comSource;
    /** Holds the medium being the target. */
    CMedium                  m_comTarget;
    /** Holds the source location. */
    QString                  m_strSourceLocation;
    /** Holds the target location. */
    QString                  m_strTargetLocation;
    /** Holds the target medium options. */
    QVector<KMediumVariant>  m_variants;
    /** Holds the target medium size. */
    qulonglong m_uMediumSize;
};

/** UINotificationProgress extension for medium move functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressMediumMove : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs medium move notification-progress.
      * @param  comMedium    Brings the medium being moved.
      * @param  strLocation  Brings the desired location. */
    UINotificationProgressMediumMove(const CMedium &comMedium,
                                     const QString &strLocation);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the medium being moved. */
    CMedium  m_comMedium;
    /** Holds the initial location. */
    QString  m_strFrom;
    /** Holds the desired location. */
    QString  m_strTo;
};

/** UINotificationProgress extension for medium resize functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressMediumResize : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs medium resize notification-progress.
      * @param  comMedium  Brings the medium being resized.
      * @param  uOldSize   Brings previous medium size.
      * @param  uNewSize   Brings desired medium size. */
    UINotificationProgressMediumResize(const CMedium &comMedium,
                                       qulonglong uOldSize,
                                       qulonglong uNewSize);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the medium being resized. */
    CMedium     m_comMedium;
    /** Holds the initial size. */
    qulonglong  m_uFrom;
    /** Holds the desired size. */
    qulonglong  m_uTo;
};

/** UINotificationProgress extension for deleting medium storage functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressMediumDeletingStorage : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about @a comMedium storage was deleted. */
    void sigMediumStorageDeleted(const CMedium &comMedium);

public:

    /** Constructs deleting medium storage notification-progress.
      * @param  comMedium  Brings the medium which storage being deleted. */
    UINotificationProgressMediumDeletingStorage(const CMedium &comMedium);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the medium which storage being deleted. */
    CMedium  m_comMedium;
    /** Holds the medium location. */
    QString  m_strLocation;
};

/** UINotificationProgress extension for machine copy functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressMachineCopy : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs machine copy notification-progress.
      * @param  comSource     Brings the machine being copied.
      * @param  comTarget     Brings the machine being the target.
      * @param  enmCloneMode  Brings the cloning mode.
      * @param  options       Brings the cloning options. */
    UINotificationProgressMachineCopy(const CMachine &comSource,
                                      const CMachine &comTarget,
                                      const KCloneMode &enmCloneMode,
                                      const QVector<KCloneOptions> &options);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the machine being copied. */
    CMachine                m_comSource;
    /** Holds the machine being the target. */
    CMachine                m_comTarget;
    /** Holds the source name. */
    QString                 m_strSourceName;
    /** Holds the target name. */
    QString                 m_strTargetName;
    /** Holds the machine cloning mode. */
    KCloneMode              m_enmCloneMode;
    /** Holds the target machine options. */
    QVector<KCloneOptions>  m_options;
};

/** UINotificationProgress extension for machine move functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressMachineMove : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs machine move notification-progress.
      * @param  uId             Brings the machine id.
      * @param  strDestination  Brings the move destination.
      * @param  strType         Brings the moving type. */
    UINotificationProgressMachineMove(const QUuid &uId,
                                      const QString &strDestination,
                                      const QString &strType);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the machine id. */
    QUuid     m_uId;
    /** Holds the session being opened. */
    CSession  m_comSession;
    /** Holds the machine source. */
    QString   m_strSource;
    /** Holds the machine destination. */
    QString   m_strDestination;
    /** Holds the moving type. */
    QString   m_strType;
};

/** UINotificationProgress extension for machine power-up functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressMachinePowerUp : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs machine power-up notification-progress.
      * @param  comMachine  Brings the machine being powered-up. */
    UINotificationProgressMachinePowerUp(const CMachine &comMachine, UILaunchMode enmLaunchMode);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the machine being powered-up. */
    CMachine      m_comMachine;
    /** Holds the launch mode. */
    UILaunchMode  m_enmLaunchMode;
    /** Holds the session being opened. */
    CSession      m_comSession;
    /** Holds the machine name. */
    QString       m_strName;
};

/** UINotificationProgress extension for machine save-state functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressMachineSaveState : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about machine state saved.
      * @param  fSuccess  Brings whether state was saved successfully. */
    void sigMachineStateSaved(bool fSuccess);

public:

    /** Constructs machine save-state notification-progress.
      * @param  comMachine  Brings the machine being saved. */
    UINotificationProgressMachineSaveState(const CMachine &comMachine);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the machine being saved. */
    CMachine  m_comMachine;
    /** Holds the session being opened. */
    CSession  m_comSession;
    /** Holds the machine name. */
    QString   m_strName;
};

/** UINotificationProgress extension for machine power-off functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressMachinePowerOff : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about machine powered off.
      * @param  fSuccess           Brings whether power off sequence successfully.
      * @param  fIncludingDiscard  Brings whether machine state should be discarded. */
    void sigMachinePoweredOff(bool fSuccess, bool fIncludingDiscard);

public:

    /** Constructs machine power-off notification-progress.
      * @param  comMachine         Brings the machine being powered off.
      * @param  comConsole         Brings the console of machine being powered off.
      * @param  fIncludingDiscard  Brings whether machine state should be discarded. */
    UINotificationProgressMachinePowerOff(const CMachine &comMachine,
                                          const CConsole &comConsole = CConsole(),
                                          bool fIncludingDiscard = false);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the machine being powered off. */
    CMachine  m_comMachine;
    /** Holds the console of machine being powered off. */
    CConsole  m_comConsole;
    /** Holds whether machine state should be discarded. */
    bool      m_fIncludingDiscard;
    /** Holds the session being opened. */
    CSession  m_comSession;
    /** Holds the machine name. */
    QString   m_strName;
};

/** UINotificationProgress extension for machine media remove functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressMachineMediaRemove : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs machine media remove notification-progress.
      * @param  comMachine  Brings the machine being removed.
      * @param  media       Brings the machine media being removed. */
    UINotificationProgressMachineMediaRemove(const CMachine &comMachine,
                                             const CMediumVector &media);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the machine being removed. */
    CMachine       m_comMachine;
    /** Holds the machine name. */
    QString        m_strName;
    /** Holds the machine media being removed. */
    CMediumVector  m_media;
};

/** UINotificationProgress extension for VFS explorer update functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressVFSExplorerUpdate : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs VFS explorer update notification-progress.
      * @param  comExplorer  Brings the VFS explorer being updated. */
    UINotificationProgressVFSExplorerUpdate(const CVFSExplorer &comExplorer);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the VFS explorer being updated. */
    CVFSExplorer  m_comExplorer;
    /** Holds the VFS explorer path. */
    QString       m_strPath;
};

/** UINotificationProgress extension for VFS explorer files remove functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressVFSExplorerFilesRemove : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs VFS explorer files remove notification-progress.
      * @param  comExplorer  Brings the VFS explorer removing the files.
      * @param  files        Brings a vector of files to be removed. */
    UINotificationProgressVFSExplorerFilesRemove(const CVFSExplorer &comExplorer,
                                                 const QVector<QString> &files);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the VFS explorer removing the files. */
    CVFSExplorer      m_comExplorer;
    /** Holds a vector of files to be removed. */
    QVector<QString>  m_files;
    /** Holds the VFS explorer path. */
    QString           m_strPath;
};

/** UINotificationProgress extension for subnet selection VSD form create functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressSubnetSelectionVSDFormCreate : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about VSD @a comForm created.
      * @param  comForm  Brings created VSD form. */
    void sigVSDFormCreated(const CVirtualSystemDescriptionForm &comForm);

public:

    /** Constructs subnet selection VSD form create notification-progress.
      * @param  comClient             Brings the cloud client being creating VSD form.
      * @param  comVsd                Brings the VSD, form being created for.
      * @param  strProviderShortName  Brings the short provider name.
      * @param  strProfileName        Brings the profile name. */
    UINotificationProgressSubnetSelectionVSDFormCreate(const CCloudClient &comClient,
                                                       const CVirtualSystemDescription &comVSD,
                                                       const QString &strProviderShortName,
                                                       const QString &strProfileName);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud client being creating VSD form. */
    CCloudClient                   m_comClient;
    /** Holds the VSD, form being created for. */
    CVirtualSystemDescription      m_comVSD;
    /** Holds the VSD form being created. */
    CVirtualSystemDescriptionForm  m_comVSDForm;
    /** Holds the short provider name. */
    QString                        m_strProviderShortName;
    /** Holds the profile name. */
    QString                        m_strProfileName;
};

/** UINotificationProgress extension for launch VSD form create functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressLaunchVSDFormCreate : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about VSD @a comForm created.
      * @param  comForm  Brings created VSD form. */
    void sigVSDFormCreated(const CVirtualSystemDescriptionForm &comForm);

public:

    /** Constructs launch VSD form create notification-progress.
      * @param  comClient             Brings the cloud client being creating VSD form.
      * @param  comVsd                Brings the VSD, form being created for.
      * @param  strProviderShortName  Brings the short provider name.
      * @param  strProfileName        Brings the profile name. */
    UINotificationProgressLaunchVSDFormCreate(const CCloudClient &comClient,
                                              const CVirtualSystemDescription &comVSD,
                                              const QString &strProviderShortName,
                                              const QString &strProfileName);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud client being creating VSD form. */
    CCloudClient                   m_comClient;
    /** Holds the VSD, form being created for. */
    CVirtualSystemDescription      m_comVSD;
    /** Holds the VSD form being created. */
    CVirtualSystemDescriptionForm  m_comVSDForm;
    /** Holds the short provider name. */
    QString                        m_strProviderShortName;
    /** Holds the profile name. */
    QString                        m_strProfileName;
};

/** UINotificationProgress extension for export VSD form create functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressExportVSDFormCreate : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about VSD @a comForm created.
      * @param  form  Brings created VSD form. */
    void sigVSDFormCreated(const QVariant &form);

public:

    /** Constructs export VSD form create notification-progress.
      * @param  comClient  Brings the cloud client being creating VSD form.
      * @param  comVsd     Brings the VSD, form being created for. */
    UINotificationProgressExportVSDFormCreate(const CCloudClient &comClient,
                                              const CVirtualSystemDescription &comVSD);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud client being creating VSD form. */
    CCloudClient                   m_comClient;
    /** Holds the VSD, form being created for. */
    CVirtualSystemDescription      m_comVSD;
    /** Holds the VSD form being created. */
    CVirtualSystemDescriptionForm  m_comVSDForm;
};

/** UINotificationProgress extension for import VSD form create functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressImportVSDFormCreate : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about VSD @a comForm created.
      * @param  form  Brings created VSD form. */
    void sigVSDFormCreated(const QVariant &form);

public:

    /** Constructs import VSD form create notification-progress.
      * @param  comClient  Brings the cloud client being creating VSD form.
      * @param  comVsd     Brings the VSD, form being created for. */
    UINotificationProgressImportVSDFormCreate(const CCloudClient &comClient,
                                              const CVirtualSystemDescription &comVSD);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud client being creating VSD form. */
    CCloudClient                   m_comClient;
    /** Holds the VSD, form being created for. */
    CVirtualSystemDescription      m_comVSD;
    /** Holds the VSD form being created. */
    CVirtualSystemDescriptionForm  m_comVSDForm;
};

/** UINotificationProgress extension for cloud image list functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudImageList : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about image @a names received. */
    void sigImageNamesReceived(const QVariant &names);
    /** Notifies listeners about image @a ids received. */
    void sigImageIdsReceived(const QVariant &ids);

public:

    /** Constructs cloud images list notification-progress.
      * @param  comClient         Brings the cloud client being listing images.
      * @param  cloudImageStates  Brings the image states we are interested in. */
    UINotificationProgressCloudImageList(const CCloudClient &comClient,
                                         const QVector<KCloudImageState> &cloudImageStates);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud client being listing images. */
    CCloudClient               m_comClient;
    /** Holds the image states we are interested in. */
    QVector<KCloudImageState>  m_cloudImageStates;
    /** Holds the listed names. */
    CStringArray               m_comNames;
    /** Holds the listed ids. */
    CStringArray               m_comIds;
};

/** UINotificationProgress extension for cloud source boot volume list functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudSourceBootVolumeList : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about source boot volume @a names received. */
    void sigImageNamesReceived(const QVariant &names);
    /** Notifies listeners about source boot volume @a ids received. */
    void sigImageIdsReceived(const QVariant &ids);

public:

    /** Constructs cloud source boot volumes list notification-progress.
      * @param  comClient  Brings the cloud client being listing source boot volumes. */
    UINotificationProgressCloudSourceBootVolumeList(const CCloudClient &comClient);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud client being listing source boot volumes. */
    CCloudClient  m_comClient;
    /** Holds the listed names. */
    CStringArray  m_comNames;
    /** Holds the listed ids. */
    CStringArray  m_comIds;
};

/** UINotificationProgress extension for cloud instance list functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudInstanceList : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about instance @a names received. */
    void sigImageNamesReceived(const QVariant &names);
    /** Notifies listeners about instance @a ids received. */
    void sigImageIdsReceived(const QVariant &ids);

public:

    /** Constructs cloud instances list notification-progress.
      * @param  comClient  Brings the cloud client being listing instances. */
    UINotificationProgressCloudInstanceList(const CCloudClient &comClient);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud client being listing instances. */
    CCloudClient  m_comClient;
    /** Holds the listed names. */
    CStringArray  m_comNames;
    /** Holds the listed ids. */
    CStringArray  m_comIds;
};

/** UINotificationProgress extension for cloud source instance list functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudSourceInstanceList : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about source instance @a names received. */
    void sigImageNamesReceived(const QVariant &names);
    /** Notifies listeners about source instance @a ids received. */
    void sigImageIdsReceived(const QVariant &ids);

public:

    /** Constructs cloud source instances list notification-progress.
      * @param  comClient  Brings the cloud client being listing source instances. */
    UINotificationProgressCloudSourceInstanceList(const CCloudClient &comClient);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud client being listing source instances. */
    CCloudClient  m_comClient;
    /** Holds the listed names. */
    CStringArray  m_comNames;
    /** Holds the listed ids. */
    CStringArray  m_comIds;
};

/** UINotificationProgress extension for cloud machine add functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudMachineAdd : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about cloud @a comMachine was added.
      * @param  strProviderShortName  Brings the short provider name.
      * @param  strProfileName        Brings the profile name. */
    void sigCloudMachineAdded(const QString &strProviderShortName,
                              const QString &strProfileName,
                              const CCloudMachine &comMachine);

public:

    /** Constructs cloud machine add notification-progress.
      * @param  comClient             Brings the cloud client being adding machine.
      * @param  comMachine            Brings the cloud machine being added.
      * @param  strInstanceName       Brings the instance name.
      * @param  strProviderShortName  Brings the short provider name.
      * @param  strProfileName        Brings the profile name. */
    UINotificationProgressCloudMachineAdd(const CCloudClient &comClient,
                                          const CCloudMachine &comMachine,
                                          const QString &strInstanceName,
                                          const QString &strProviderShortName,
                                          const QString &strProfileName);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud client being adding machine. */
    CCloudClient   m_comClient;
    /** Holds the cloud machine being added. */
    CCloudMachine  m_comMachine;
    /** Holds the instance name. */
    QString        m_strInstanceName;
    /** Holds the short provider name. */
    QString        m_strProviderShortName;
    /** Holds the profile name. */
    QString        m_strProfileName;
};

/** UINotificationProgress extension for cloud machine create functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudMachineCreate : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about cloud @a comMachine was created.
      * @param  strProviderShortName  Brings the short provider name.
      * @param  strProfileName        Brings the profile name. */
    void sigCloudMachineCreated(const QString &strProviderShortName,
                                const QString &strProfileName,
                                const CCloudMachine &comMachine);

public:

    /** Constructs cloud machine create notification-progress.
      * @param  comClient             Brings the cloud client being adding machine.
      * @param  comMachine            Brings the cloud machine being added.
      * @param  comVSD                Brings the virtual system description.
      * @param  strProviderShortName  Brings the short provider name.
      * @param  strProfileName        Brings the profile name. */
    UINotificationProgressCloudMachineCreate(const CCloudClient &comClient,
                                             const CCloudMachine &comMachine,
                                             const CVirtualSystemDescription &comVSD,
                                             const QString &strProviderShortName,
                                             const QString &strProfileName);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud client being adding machine. */
    CCloudClient               m_comClient;
    /** Holds the cloud machine being added. */
    CCloudMachine              m_comMachine;
    /** Holds the the virtual system description. */
    CVirtualSystemDescription  m_comVSD;
    /** Holds the cloud machine name. */
    QString                    m_strName;
    /** Holds the short provider name. */
    QString                    m_strProviderShortName;
    /** Holds the profile name. */
    QString                    m_strProfileName;
};

/** UINotificationProgress extension for cloud machine remove functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudMachineRemove : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about cloud machine was removed.
      * @param  strProviderShortName  Brings the short provider name.
      * @param  strProfileName        Brings the profile name.
      * @param  strName               Brings the machine name. */
    void sigCloudMachineRemoved(const QString &strProviderShortName,
                                const QString &strProfileName,
                                const QString &strName);

public:

    /** Constructs cloud machine remove notification-progress.
      * @param  comMachine    Brings the cloud machine being removed.
      * @param  fFullRemoval  Brings whether cloud machine should be removed fully. */
    UINotificationProgressCloudMachineRemove(const CCloudMachine &comMachine,
                                             bool fFullRemoval,
                                             const QString &strProviderShortName,
                                             const QString &strProfileName);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud machine being removed. */
    CCloudMachine  m_comMachine;
    /** Holds the cloud machine name. */
    QString        m_strName;
    /** Holds whether cloud machine should be removed fully. */
    bool           m_fFullRemoval;
    /** Holds the short provider name. */
    QString        m_strProviderShortName;
    /** Holds the profile name. */
    QString        m_strProfileName;
};

/** UINotificationProgress extension for cloud machine power-up functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudMachinePowerUp : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs cloud machine power-up notification-progress.
      * @param  comMachine  Brings the machine being powered-up. */
    UINotificationProgressCloudMachinePowerUp(const CCloudMachine &comMachine);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the machine being powered-up. */
    CCloudMachine  m_comMachine;
    /** Holds the machine name. */
    QString        m_strName;
};

/** UINotificationProgress extension for cloud machine clone functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudMachineClone : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs cloud machine clone notification-progress.
      * @param  comClient     Brings the cloud client to clone cloud machine.
      * @param  comMachine    Brings the cloud machine to be cloned.
      * @param  strCloneName  Brings the clone name. */
    UINotificationProgressCloudMachineClone(const CCloudClient &comClient,
                                            const CCloudMachine &comMachine,
                                            const QString &strCloneName);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the client cloning machine. */
    CCloudClient   m_comClient;
    /** Holds the machine being reset. */
    CCloudMachine  m_comMachine;
    /** Holds the clone name. */
    QString        m_strCloneName;
    /** Holds the machine internal id. */
    QString        m_strId;
    /** Holds the machine name. */
    QString        m_strName;
};

/** UINotificationProgress extension for cloud machine reset functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudMachineReset : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs cloud machine reset notification-progress.
      * @param  comMachine  Brings the machine being reset. */
    UINotificationProgressCloudMachineReset(const CCloudMachine &comMachine);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the machine being reset. */
    CCloudMachine  m_comMachine;
    /** Holds the machine name. */
    QString        m_strName;
};

/** UINotificationProgress extension for cloud machine power-off functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudMachinePowerOff : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs cloud machine power-off notification-progress.
      * @param  comMachine  Brings the machine being powered-off. */
    UINotificationProgressCloudMachinePowerOff(const CCloudMachine &comMachine);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the machine being powered-off. */
    CCloudMachine  m_comMachine;
    /** Holds the machine name. */
    QString        m_strName;
};

/** UINotificationProgress extension for cloud machine shutdown functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudMachineShutdown : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs cloud machine shutdown notification-progress.
      * @param  comMachine  Brings the machine being shutdown. */
    UINotificationProgressCloudMachineShutdown(const CCloudMachine &comMachine);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the machine being shutdown. */
    CCloudMachine  m_comMachine;
    /** Holds the machine name. */
    QString        m_strName;
};

/** UINotificationProgress extension for cloud machine terminate functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudMachineTerminate : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs cloud machine terminate notification-progress.
      * @param  comMachine  Brings the machine being terminate. */
    UINotificationProgressCloudMachineTerminate(const CCloudMachine &comMachine);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the machine being terminated. */
    CCloudMachine  m_comMachine;
    /** Holds the machine name. */
    QString        m_strName;
};

/** UINotificationProgress extension for cloud machine settings form create functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudMachineSettingsFormCreate : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about settings @a comForm created.
      * @param  form  Brings created VSD form. */
    void sigSettingsFormCreated(const QVariant &form);

public:

    /** Constructs cloud machine settings form create notification-progress.
      * @param  comMachine      Brings the machine form being created for.
      * @param  strMachineName  Brings the machine name. */
    UINotificationProgressCloudMachineSettingsFormCreate(const CCloudMachine &comMachine,
                                                         const QString &strMachineName);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the machine form being created for. */
    CCloudMachine  m_comMachine;
    /** Holds the machine name. */
    QString        m_strMachineName;
    /** Holds the form being created. */
    CForm          m_comForm;
};

/** UINotificationProgress extension for cloud machine settings form apply functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudMachineSettingsFormApply : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs cloud machine settings form apply notification-progress.
      * @param  comForm         Brings the form being applied.
      * @param  strMachineName  Brings the machine name. */
    UINotificationProgressCloudMachineSettingsFormApply(const CForm &comForm,
                                                        const QString &strMachineName);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the machine form being created for. */
    CForm    m_comForm;
    /** Holds the machine name. */
    QString  m_strMachineName;
};

/** UINotificationProgress extension for cloud console connection create functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudConsoleConnectionCreate : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs cloud console connection create notification-progress.
      * @param  comMachine    Brings the cloud machine for which console connection being created.
      * @param  strPublicKey  Brings the public key used for console connection being created. */
    UINotificationProgressCloudConsoleConnectionCreate(const CCloudMachine &comMachine,
                                                       const QString &strPublicKey);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the cloud machine for which console connection being created. */
    CCloudMachine  m_comMachine;
    /** Holds the cloud machine name. */
    QString        m_strName;
    /** Holds the public key used for console connection being created. */
    QString        m_strPublicKey;
};

/** UINotificationProgress extension for cloud console connection delete functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudConsoleConnectionDelete : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs cloud console connection delete notification-progress.
      * @param  comMachine  Brings the cloud machine for which console connection being deleted. */
    UINotificationProgressCloudConsoleConnectionDelete(const CCloudMachine &comMachine);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the cloud machine for which console connection being deleted. */
    CCloudMachine  m_comMachine;
    /** Holds the cloud machine name. */
    QString        m_strName;
};

/** UINotificationProgress extension for cloud console log acquire functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressCloudConsoleLogAcquire : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about console @a strLog for cloud VM with @a strName read. */
    void sigLogRead(const QString &strName, const QString &strLog);

public:

    /** Constructs cloud console log acquire notification-progress.
      * @param  comMachine  Brings the cloud machine for which console log being acquired. */
    UINotificationProgressCloudConsoleLogAcquire(const CCloudMachine &comMachine);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the cloud machine for which console log being acquired. */
    CCloudMachine  m_comMachine;
    /** Holds the cloud machine name. */
    QString        m_strName;
    /** Holds the stream log being read to. */
    CDataStream    m_comStream;
};

/** UINotificationProgress extension for snapshot take functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressSnapshotTake : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about snapshot with @a id taken. */
    void sigSnapshotTaken(const QVariant &id);

public:

    /** Constructs snapshot take notification-progress.
      * @param  comMachine              Brings the machine we are taking snapshot for.
      * @param  strSnapshotName         Brings the name of snapshot being taken.
      * @param  strSnapshotDescription  Brings the description of snapshot being taken. */
    UINotificationProgressSnapshotTake(const CMachine &comMachine,
                                       const QString &strSnapshotName,
                                       const QString &strSnapshotDescription);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the machine we are taking snapshot for. */
    CMachine  m_comMachine;
    /** Holds the name of snapshot being taken. */
    QString   m_strSnapshotName;
    /** Holds the description of snapshot being taken. */
    QString   m_strSnapshotDescription;
    /** Holds the machine name. */
    QString   m_strMachineName;
    /** Holds the session being opened. */
    CSession  m_comSession;
    /** Holds the taken snapshot id. */
    QUuid     m_uSnapshotId;
};

/** UINotificationProgress extension for snapshot restore functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressSnapshotRestore : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about snapshot restored.
      * @param  fSuccess  Brings whether snapshot restored successfully. */
    void sigSnapshotRestored(bool fSuccess);

public:

    /** Constructs snapshot restore notification-progress.
      * @param  uMachineId   Brings the ID of machine we are restoring snapshot for.
      * @param  comSnapshot  Brings the snapshot being restored. */
    UINotificationProgressSnapshotRestore(const QUuid &uMachineId,
                                          const CSnapshot &comSnapshot = CSnapshot());
    /** Constructs snapshot restore notification-progress.
      * @param  comMachine   Brings the machine we are restoring snapshot for.
      * @param  comSnapshot  Brings the snapshot being restored. */
    UINotificationProgressSnapshotRestore(const CMachine &comMachine,
                                          const CSnapshot &comSnapshot = CSnapshot());

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the ID of machine we are restoring snapshot for. */
    QUuid      m_uMachineId;
    /** Holds the machine we are restoring snapshot for. */
    CMachine   m_comMachine;
    /** Holds the snapshot being restored. */
    CSnapshot  m_comSnapshot;
    /** Holds the machine name. */
    QString    m_strMachineName;
    /** Holds the snapshot name. */
    QString    m_strSnapshotName;
    /** Holds the session being opened. */
    CSession   m_comSession;
};

/** UINotificationProgress extension for snapshot delete functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressSnapshotDelete : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs snapshot delete notification-progress.
      * @param  comMachine   Brings the machine we are deleting snapshot from.
      * @param  uSnapshotId  Brings the ID of snapshot being deleted. */
    UINotificationProgressSnapshotDelete(const CMachine &comMachine,
                                         const QUuid &uSnapshotId);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the machine we are deleting snapshot from. */
    CMachine  m_comMachine;
    /** Holds the ID of snapshot being deleted. */
    QUuid     m_uSnapshotId;
    /** Holds the machine name. */
    QString   m_strMachineName;
    /** Holds the snapshot name. */
    QString   m_strSnapshotName;
    /** Holds the session being opened. */
    CSession  m_comSession;
};

/** UINotificationProgress extension for appliance write functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressApplianceWrite : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs appliance write notification-progress.
      * @param  comAppliance  Brings the appliance being written.
      * @param  strFormat     Brings the appliance format.
      * @param  options       Brings the export options to be taken into account.
      * @param  strPath       Brings the appliance path. */
    UINotificationProgressApplianceWrite(const CAppliance &comAppliance,
                                         const QString &strFormat,
                                         const QVector<KExportOptions> &options,
                                         const QString &strPath);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the appliance being written. */
    CAppliance               m_comAppliance;
    /** Holds the appliance format. */
    QString                  m_strFormat;
    /** Holds the export options to be taken into account. */
    QVector<KExportOptions>  m_options;
    /** Holds the appliance path. */
    QString                  m_strPath;
};

/** UINotificationProgress extension for appliance read functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressApplianceRead : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs appliance read notification-progress.
      * @param  comAppliance  Brings the appliance being read.
      * @param  strPath       Brings the appliance path. */
    UINotificationProgressApplianceRead(const CAppliance &comAppliance,
                                        const QString &strPath);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the appliance being read. */
    CAppliance  m_comAppliance;
    /** Holds the appliance path. */
    QString     m_strPath;
};

/** UINotificationProgress extension for import appliance functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressApplianceImport : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs import appliance notification-progress.
      * @param  comAppliance  Brings the appliance being imported.
      * @param  options       Brings the import options to be taken into account. */
    UINotificationProgressApplianceImport(const CAppliance &comAppliance,
                                          const QVector<KImportOptions> &options);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the appliance being imported. */
    CAppliance               m_comAppliance;
    /** Holds the import options to be taken into account. */
    QVector<KImportOptions>  m_options;
};

/** UINotificationProgress extension for extension pack install functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressExtensionPackInstall : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs extension pack install notification-progress.
      * @param  comExtPackFile        Brings the extension pack file to install.
      * @param  fReplace              Brings whether extension pack should be replaced.
      * @param  strExtensionPackName  Brings the extension pack name.
      * @param  strDisplayInfo        Brings the display info. */
    UINotificationProgressExtensionPackInstall(const CExtPackFile &comExtPackFile,
                                               bool fReplace,
                                               const QString &strExtensionPackName,
                                               const QString &strDisplayInfo);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the extension pack file to install. */
    CExtPackFile     m_comExtPackFile;
    /** Holds whether extension pack should be replaced. */
    bool             m_fReplace;
    /** Holds the extension pack name. */
    QString          m_strExtensionPackName;
    /** Holds the display info. */
    QString          m_strDisplayInfo;
};

/** UINotificationProgress extension for extension pack uninstall functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressExtensionPackUninstall : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs extension pack uninstall notification-progress.
      * @param  comExtPackManager     Brings the extension pack manager.
      * @param  strExtensionPackName  Brings the extension pack name.
      * @param  strDisplayInfo        Brings the display info. */
    UINotificationProgressExtensionPackUninstall(const CExtPackManager &comExtPackManager,
                                                 const QString &strExtensionPackName,
                                                 const QString &strDisplayInfo);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the extension pack manager. */
    CExtPackManager  m_comExtPackManager;
    /** Holds the extension pack name. */
    QString          m_strExtensionPackName;
    /** Holds the display info. */
    QString          m_strDisplayInfo;
};

/** UINotificationProgress extension for guest additions install functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressGuestAdditionsInstall : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about guest additions installation failed.
      * @param  strSource  Brings the guest additions file path. */
    void sigGuestAdditionsInstallationFailed(const QString &strSource);

public:

    /** Constructs guest additions install notification-progress.
      * @param  comGuest   Brings the guest additions being installed to.
      * @param  strSource  Brings the guest additions file path. */
    UINotificationProgressGuestAdditionsInstall(const CGuest &comGuest,
                                                const QString &strSource);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the guest additions being installed to. */
    CGuest   m_comGuest;
    /** Holds the guest additions file path. */
    QString  m_strSource;
};

/** UINotificationProgress extension for host-only network interface create functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressHostOnlyNetworkInterfaceCreate : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about host-only network interface created.
      * @param  comInterface  Brings network interface created. */
    void sigHostOnlyNetworkInterfaceCreated(const CHostNetworkInterface &comInterface);

public:

    /** Constructs host-only network interface create notification-progress.
      * @param  comHost       Brings the host network interface being created for.
      * @param  comInterface  Brings the network interface being created. */
    UINotificationProgressHostOnlyNetworkInterfaceCreate(const CHost &comHost,
                                                         const CHostNetworkInterface &comInterface);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the host network interface being created for. */
    CHost                  m_comHost;
    /** Holds the network interface being created. */
    CHostNetworkInterface  m_comInterface;
};

/** UINotificationProgress extension for host-only network interface remove functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressHostOnlyNetworkInterfaceRemove : public UINotificationProgress
{
    Q_OBJECT;

signals:

    /** Notifies listeners about host-only network interface removed.
      * @param  strInterfaceName  Brings the name of network interface removed. */
    void sigHostOnlyNetworkInterfaceRemoved(const QString &strInterfaceName);

public:

    /** Constructs host-only network interface remove notification-progress.
      * @param  comHost       Brings the host network interface being removed for.
      * @param  uInterfaceId  Brings the ID of network interface being removed. */
    UINotificationProgressHostOnlyNetworkInterfaceRemove(const CHost &comHost,
                                                         const QUuid &uInterfaceId);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds the host network interface being removed for. */
    CHost    m_comHost;
    /** Holds the ID of network interface being removed. */
    QUuid    m_uInterfaceId;
    /** Holds the network interface name. */
    QString  m_strInterfaceName;
};

/** UINotificationProgress extension for virtual system description form value set functionality. */
class SHARED_LIBRARY_STUFF UINotificationProgressVsdFormValueSet : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs virtual system description form value set notification-progress.
      * @param  comValue  Brings our value being set.
      * @param  fBool     Brings the value our value being set to. */
    UINotificationProgressVsdFormValueSet(const CBooleanFormValue &comValue, bool fBool);

    /** Constructs virtual system description form value set notification-progress.
      * @param  comValue   Brings our value being set.
      * @param  strString  Brings the value our value being set to. */
    UINotificationProgressVsdFormValueSet(const CStringFormValue &comValue, const QString &strString);

    /** Constructs virtual system description form value set notification-progress.
      * @param  comValue  Brings our value being set.
      * @param  iChoice   Brings the value our value being set to. */
    UINotificationProgressVsdFormValueSet(const CChoiceFormValue &comValue, int iChoice);

    /** Constructs virtual system description form value set notification-progress.
      * @param  comValue  Brings our value being set.
      * @param  iInteger  Brings the value our value being set to. */
    UINotificationProgressVsdFormValueSet(const CRangedIntegerFormValue &comValue, int iInteger);

    /** Constructs virtual system description form value set notification-progress.
      * @param  comValue    Brings our value being set.
      * @param  iInteger64  Brings the value our value being set to. */
    UINotificationProgressVsdFormValueSet(const CRangedInteger64FormValue &comValue, qlonglong iInteger64);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private:

    /** Value type. */
    KFormValueType  m_enmType;

    /** Holds our value being set. */
    CFormValue  m_comValue;

    /** Holds the bool value. */
    bool       m_fBool;
    /** Holds the string value. */
    QString    m_strString;
    /** Holds the choice value. */
    int        m_iChoice;
    /** Holds the integer value. */
    int        m_iInteger;
    /** Holds the integer64 value. */
    qlonglong  m_iInteger64;
};

#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
/** UINotificationDownloader extension for extension pack downloading functionality. */
class SHARED_LIBRARY_STUFF UINotificationDownloaderExtensionPack : public UINotificationDownloader
{
    Q_OBJECT;

signals:

    /** Notifies listeners about extension pack downloaded.
      * @param  strSource  Brings the EP source.
      * @param  strTarget  Brings the EP target.
      * @param  strDigest  Brings the EP digest. */
    void sigExtensionPackDownloaded(const QString &strSource,
                                    const QString &strTarget,
                                    const QString &strDigest);

public:

    /** Returns singleton instance, creates if necessary.
      * @param  strPackName  Brings the package name. */
    static UINotificationDownloaderExtensionPack *instance(const QString &strPackName);
    /** Returns whether singleton instance already created. */
    static bool exists();

    /** Destructs extension pack downloading notification-downloader.
      * @note  Notification-center can destroy us at any time. */
    virtual ~UINotificationDownloaderExtensionPack() RT_OVERRIDE RT_FINAL;

protected:

    /** Constructs extension pack downloading notification-downloader.
      * @param  strPackName  Brings the package name. */
    UINotificationDownloaderExtensionPack(const QString &strPackName);

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started downloader. */
    virtual UIDownloader *createDownloader() RT_OVERRIDE RT_FINAL;

private:

    /** Holds the singleton instance. */
    static UINotificationDownloaderExtensionPack *s_pInstance;

    /** Holds the name of pack being dowloaded. */
    QString  m_strPackName;
};

/** UINotificationDownloader extension for guest additions downloading functionality. */
class SHARED_LIBRARY_STUFF UINotificationDownloaderGuestAdditions : public UINotificationDownloader
{
    Q_OBJECT;

signals:

    /** Notifies listeners about guest additions downloaded.
      * @param  strLocation  Brings the UM location. */
    void sigGuestAdditionsDownloaded(const QString &strLocation);

public:

    /** Returns singleton instance, creates if necessary.
      * @param  strFileName  Brings the file name. */
    static UINotificationDownloaderGuestAdditions *instance(const QString &strFileName);
    /** Returns whether singleton instance already created. */
    static bool exists();

    /** Destructs guest additions downloading notification-downloader.
      * @note  Notification-center can destroy us at any time. */
    virtual ~UINotificationDownloaderGuestAdditions() RT_OVERRIDE RT_FINAL;

protected:

    /** Constructs guest additions downloading notification-downloader.
      * @param  strFileName  Brings the file name. */
    UINotificationDownloaderGuestAdditions(const QString &strFileName);

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started downloader. */
    virtual UIDownloader *createDownloader() RT_OVERRIDE RT_FINAL;

private:

    /** Holds the singleton instance. */
    static UINotificationDownloaderGuestAdditions *s_pInstance;

    /** Holds the name of file being dowloaded. */
    QString  m_strFileName;
};

/** UINotificationDownloader extension for user manual downloading functionality. */
class SHARED_LIBRARY_STUFF UINotificationDownloaderUserManual : public UINotificationDownloader
{
    Q_OBJECT;

signals:

    /** Notifies listeners about user manual downloaded.
      * @param  strLocation  Brings the UM location. */
    void sigUserManualDownloaded(const QString &strLocation);

public:

    /** Returns singleton instance, creates if necessary.
      * @param  strFileName  Brings the file name. */
    static UINotificationDownloaderUserManual *instance(const QString &strFileName);
    /** Returns whether singleton instance already created. */
    static bool exists();

    /** Destructs user manual downloading notification-downloader.
      * @note  Notification-center can destroy us at any time. */
    virtual ~UINotificationDownloaderUserManual() RT_OVERRIDE RT_FINAL;

protected:

    /** Constructs user manual downloading notification-downloader.
      * @param  strFileName  Brings the file name. */
    UINotificationDownloaderUserManual(const QString &strFileName);

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started downloader. */
    virtual UIDownloader *createDownloader() RT_OVERRIDE RT_FINAL;

private:

    /** Holds the singleton instance. */
    static UINotificationDownloaderUserManual *s_pInstance;

    /** Holds the name of file being dowloaded. */
    QString  m_strFileName;
};

/** UINotificationProgress extension for checking a new VirtualBox version. */
class SHARED_LIBRARY_STUFF UINotificationProgressNewVersionChecker : public UINotificationProgress
{
    Q_OBJECT;

public:

    /** Constructs new version check notification-progress.
      * @param  fForcedCall  Brings whether even negative result should be reflected. */
    UINotificationProgressNewVersionChecker(bool fForcedCall);

protected:

    /** Returns object name. */
    virtual QString name() const RT_OVERRIDE RT_FINAL;
    /** Returns object details. */
    virtual QString details() const RT_OVERRIDE RT_FINAL;
    /** Creates and returns started progress-wrapper. */
    virtual CProgress createProgress(COMResult &comResult) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles signal about progress being finished. */
    void sltHandleProgressFinished();

private:

    /** Holds whether this customer has forced privelegies. */
    bool          m_fForcedCall;
# ifdef VBOX_WITH_UPDATE_AGENT
    /** Holds the host update agent reference. */
    CUpdateAgent  m_comUpdateHost;
# endif
};
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */

#endif /* !FEQT_INCLUDED_SRC_notificationcenter_UINotificationObjects_h */
