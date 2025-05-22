/* $Id: UINotificationObjects.cpp 168965 2025-05-22 14:49:38Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - Various UINotificationObjects implementations.
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

/* Qt includes: */
#include <QApplication>
#include <QDir>
#include <QFileInfo>

/* GUI includes: */
#include "UICommon.h"
#include "UIConverter.h"
#include "UIErrorString.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIHostComboEditor.h"
#include "UILocalMachineStuff.h"
#include "UINotificationCenter.h"
#include "UINotificationObjects.h"
#include "UITranslator.h"
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
# include "UIDownloaderExtensionPack.h"
# include "UIDownloaderGuestAdditions.h"
# include "UIDownloaderUserManual.h"
# include "UINewVersionChecker.h"
#endif

/* COM includes: */
#include "CAudioAdapter.h"
#include "CAudioSettings.h"
#include "CBooleanFormValue.h"
#include "CChoiceFormValue.h"
#include "CCloudNetwork.h"
#include "CCloudProfile.h"
#include "CCloudProvider.h"
#include "CCloudProviderManager.h"
#include "CConsole.h"
#include "CDHCPServer.h"
#include "CDisplay.h"
#include "CEmulatedUSB.h"
#include "CExtPack.h"
#include "CGraphicsAdapter.h"
#include "CGuestOSType.h"
#include "CHostNetworkInterface.h"
#include "CHostOnlyNetwork.h"
#include "CKeyboard.h"
#include "CMachineDebugger.h"
#include "CMediumAttachment.h"
#include "CMouse.h"
#include "CNATNetwork.h"
#include "CNetworkAdapter.h"
#include "CPlatform.h"
#include "CPlatformProperties.h"
#include "CRangedIntegerFormValue.h"
#include "CRangedInteger64FormValue.h"
#include "CRecordingSettings.h"
#include "CStringFormValue.h"
#include "CStorageController.h"
#include "CSystemProperties.h"
#include "CUnattended.h"
#include "CUpdateAgent.h"
#include "CVRDEServer.h"
#include "CVRDEServerInfo.h"
#ifdef VBOX_WITH_DRAG_AND_DROP
# include "CDnDTarget.h"
#endif

/* Other VBox stuff: */
#ifdef VBOX_WS_NIX
# include <iprt/env.h>
#endif

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h>


/*********************************************************************************************************************************
*   Class UINotificationMessage implementation.                                                                                  *
*********************************************************************************************************************************/

/* static */
QMap<QString, QUuid> UINotificationMessage::m_messages = QMap<QString, QUuid>();

/* static */
void UINotificationMessage::cannotFindHelpFile(const QString &strLocation)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't find help file ..."),
        QApplication::translate("UIMessageCenter", "Failed to find the following help file: <b>%1</b>")
                                                   .arg(strLocation));
}

/* static */
void UINotificationMessage::cannotOpenURL(const QString &strUrl)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't open URL ..."),
        QApplication::translate("UIMessageCenter", "Failed to open <tt>%1</tt>. "
                                                   "Make sure your desktop environment can properly handle URLs of this type.")
                                                   .arg(strUrl));
}

/* static */
void UINotificationMessage::remindAboutBetaBuild()
{
    createMessage(
        QApplication::translate("UIMessageCenter", "BETA build warning!"),
        QApplication::translate("UIMessageCenter", "You are running a prerelease version of VirtualBox. "
                                                   "This version is not suitable for production use."));
}

/* static */
void UINotificationMessage::remindAboutExperimentalBuild()
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Experimental build warning!"),
        QApplication::translate("UIMessageCenter", "You are running an EXPERIMENTAL build of VirtualBox. "
                                                   "This version is not suitable for production use."));
}

/* static */
void UINotificationMessage::warnAboutInvalidEncryptionPassword(const QString &strPasswordId)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Invalid Password ..."),
        QApplication::translate("UIMessageCenter", "Encryption password for <nobr>ID = '%1'</nobr> is invalid.")
                                                   .arg(strPasswordId));
}

/* static */
void UINotificationMessage::warnAboutClipboardError(const QString &strMsg)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Shared clipboard error ..."),
        strMsg.toUtf8().constData());
}

#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
/* static */
void UINotificationMessage::showUpdateNotFound()
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Nothing to update ..."),
        QApplication::translate("UIMessageCenter", "You are already running the most recent version of VirtualBox."));
}

/* static */
void UINotificationMessage::showUpdateSuccess(const QString &strVersion, const QString &strLink)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "New version found ..."),
        QApplication::translate("UIMessageCenter", "<p>A new version of VirtualBox has been released! Version <b>%1</b> is available "
                                                   "at <a href=\"https://www.virtualbox.org/\">virtualbox.org</a>.</p>"
                                                   "<p>You can download this version using the link:</p>"
                                                   "<p><a href=%2>%3</a></p>").arg(strVersion, strLink, strLink));
}

/* static */
void UINotificationMessage::askUserToDownloadExtensionPack(const QString &strExtPackName,
                                                           const QString &strExtPackVersion,
                                                           const QString &strVBoxVersion)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Update is required ..."),
        QApplication::translate("UIMessageCenter", "<p>You have version %1 of the <b><nobr>%2</nobr></b> installed.</p>"
                                                   "<p>You should download and install version %3 of this extension pack from "
                                                   "Oracle!</p>").arg(strExtPackVersion, strExtPackName, strVBoxVersion));
}

/* static */
void UINotificationMessage::cannotValidateGuestAdditionsSHA256Sum(const QString &strUrl,
                                                                  const QString &strSrc)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Unable to validate guest additions image ..."),
        QApplication::translate("UIMessageCenter", "<p>The <b>VirtualBox Guest Additions</b> disk image file has been "
                                                   "successfully downloaded from <nobr><a href=\"%1\">%1</a></nobr> and saved "
                                                   "locally as <nobr><b>%2</b>, </nobr>but the SHA-256 checksum verification "
                                                   "failed.</p><p>Please do the download, installation and verification "
                                                   "manually.</p>").arg(strUrl, strSrc));
}

/* static */
void UINotificationMessage::warnAboutUserManualDownloaded(const QString &strUrl, const QString &strTarget)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "User guide downloaded ..."),
        QApplication::translate("UIMessageCenter", "<p>The VirtualBox User Guide has been successfully downloaded from "
                                                   "<nobr><a href=\"%1\">%1</a></nobr> and saved locally as "
                                                   "<nobr><b>%2</b>.</nobr></p>").arg(strUrl, strTarget));
}

/* static */
void UINotificationMessage::cannotValidateExtentionPackSHA256Sum(const QString &strExtPackName,
                                                                 const QString &strFrom,
                                                                 const QString &strTo)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Unable to validate extension pack ..."),
        QApplication::translate("UIMessageCenter", "<p>The <b><nobr>%1</nobr></b> has been successfully downloaded "
                                                   "from <nobr><a href=\"%2\">%2</a></nobr> and saved locally as "
                                                   "<nobr><b>%3</b>, </nobr>but the SHA-256 checksum verification failed.</p>"
                                                   "<p>Please do the download, installation and verification manually.</p>")
                                                   .arg(strExtPackName, strFrom, strTo));
}
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */

/* static */
void UINotificationMessage::cannotCreateMachineFolder(const QString &strPath,
                                                      UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create machine folder ..."),
        QApplication::translate("UIMessageCenter", "Failed to create machine folder at <nobr><b>%1</b></nobr>.")
                                                   .arg(strPath),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotOverwriteMachineFolder(const QString &strPath,
                                                         UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't overwrite machine folder ..."),
        QApplication::translate("UIMessageCenter", "Failed to overwrite machine folder at <nobr><b>%1</b></nobr>.")
                                                   .arg(strPath),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotRemoveMachineFolder(const QString &strPath,
                                                      UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't remove machine folder ..."),
        QApplication::translate("UIMessageCenter", "Failed to remove machine folder at <nobr><b>%1</b></nobr>.")
                                                   .arg(strPath),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotReregisterExistingMachine(const QString &strName, const QString &strLocation)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't add machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to add virtual machine <b>%1</b> located in <i>%2</i> because its "
                                                   "already present.")
                                                   .arg(strName, strLocation));
}

/* static */
void UINotificationMessage::cannotResolveCollisionAutomatically(const QString &strCollisionName, const QString &strGroupName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't resolve collision ..."),
        QApplication::translate("UIMessageCenter", "<p>You are trying to move machine <nobr><b>%1</b></nobr> to group "
                                                   "<nobr><b>%2</b></nobr> which already have another item with the same "
                                                   "name.</p><p>Please resolve this name conflict and try again.</p>")
                                                   .arg(strCollisionName, strGroupName));
}

/* static */
void UINotificationMessage::cannotAcquireCloudMachineSettings(const QString &strErrorDetails)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Cloud failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire cloud machine settings.") +
        strErrorDetails);
}

/* static */
void UINotificationMessage::cannotCreateMediumStorageInFAT(const QString &strPath,
                                                           UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create medium ..."),
        QApplication::translate("UIMessageCenter", "Failed to create medium storage at <nobr><b>%1</b></nobr>.")
                                                   .arg(strPath),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotOverwriteMediumStorage(const QString &strPath,
                                                         UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't overwrite medium ..."),
        QApplication::translate("UIMessageCenter", "Failed to overwrite medium storage at <nobr><b>%1</b></nobr>.")
                                                   .arg(strPath),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotOpenLicenseFile(const QString &strPath)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't open license file ..."),
        QApplication::translate("UIMessageCenter", "Failed to open the license file <nobr><b>%1</b></nobr>. Check file "
                                                   "permissions.").arg(strPath));
}

/* static */
void UINotificationMessage::warnAboutPublicKeyFilePathIsEmpty()
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Public key missing ..."),
        QApplication::translate("UIMessageCenter", "Public key file path is empty."));
}

/* static */
void UINotificationMessage::warnAboutPublicKeyFileDoesntExist(const QString &strPath)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Public key missing ..."),
        QApplication::translate("UIMessageCenter", "Failed to open the public key file <nobr><b>%1</b></nobr>. "
                                                   "File doesn't exist.").arg(strPath));
}

/* static */
void UINotificationMessage::warnAboutPublicKeyFileIsOfTooLargeSize(const QString &strPath)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Public key too large ..."),
        QApplication::translate("UIMessageCenter", "Failed to open the public key file <nobr><b>%1</b></nobr>. File is too "
                                                   "large for the key.").arg(strPath));
}

/* static */
void UINotificationMessage::warnAboutPublicKeyFileIsntReadable(const QString &strPath)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Public key isn't readable ..."),
        QApplication::translate("UIMessageCenter", "Failed to open the public key file <nobr><b>%1</b></nobr>. Check file "
                                                   "permissions.").arg(strPath));
}

/* static */
void UINotificationMessage::warnAboutDHCPServerIsNotEnabled(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "DHCP server isn't enabled ..."),
        QApplication::translate("UIMessageCenter", "Network <nobr><b>%1</b></nobr> is set to obtain the address "
                                                   "automatically but the corresponding DHCP server is not enabled.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutInvalidIPv4Address(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Invalid IPv4 address ..."),
        QApplication::translate("UIMessageCenter", "Network <nobr><b>%1</b></nobr> does not "
                                                   "currently have a valid IPv4 address.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutInvalidIPv4Mask(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Invalid IPv4 mask ..."),
        QApplication::translate("UIMessageCenter", "Network <nobr><b>%1</b></nobr> does not "
                                                   "currently have a valid IPv4 mask.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutInvalidIPv6Address(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Invalid IPv6 address ..."),
        QApplication::translate("UIMessageCenter", "Network <nobr><b>%1</b></nobr> does not "
                                                   "currently have a valid IPv6 address.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutInvalidIPv6PrefixLength(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Invalid IPv6 prefix length ..."),
        QApplication::translate("UIMessageCenter", "Network <nobr><b>%1</b></nobr> does not "
                                                   "currently have a valid IPv6 prefix length.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutInvalidDHCPServerAddress(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Invalid DHCP server address ..."),
        QApplication::translate("UIMessageCenter", "Network <nobr><b>%1</b></nobr> does not "
                                                   "currently have a valid DHCP server address.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutInvalidDHCPServerMask(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Invalid DHCP server mask ..."),
        QApplication::translate("UIMessageCenter", "Network <nobr><b>%1</b></nobr> does not "
                                                   "currently have a valid DHCP server mask.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutInvalidDHCPServerLowerAddress(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Invalid DHCP lower address ..."),
        QApplication::translate("UIMessageCenter", "Network <nobr><b>%1</b></nobr> does not "
                                                   "currently have a valid DHCP server lower address bound.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutInvalidDHCPServerUpperAddress(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Invalid DHCP upper address ..."),
        QApplication::translate("UIMessageCenter", "Network <nobr><b>%1</b></nobr> does not "
                                                   "currently have a valid DHCP server upper address bound.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutNoNameSpecified(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "No name specified ..."),
        QApplication::translate("UIMessageCenter", "No new name specified for the network previously called <b>%1</b>.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutNameAlreadyBusy(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Name already busy ..."),
        QApplication::translate("UIMessageCenter", "The name <b>%1</b> is being used for several networks.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutNoIPv4PrefixSpecified(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "No IPv4 prefix specified ..."),
        QApplication::translate("UIMessageCenter", "No IPv4 prefix specified for the NAT network <b>%1</b>.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::warnAboutNoIPv6PrefixSpecified(const QString &strName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "No IPv6 prefix specified ..."),
        QApplication::translate("UIMessageCenter", "No IPv6 prefix specified for the NAT network <b>%1</b>.")
                                                   .arg(strName));
}

/* static */
void UINotificationMessage::cannotMountImage(const QString &strMachineName, const QString &strMediumName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't mount image ..."),
        QApplication::translate("UIMessageCenter", "<p>Could not insert the <b>%1</b> disk image file into the virtual machine "
                                                   "<b>%2</b>, as the machine has no optical drives. Please add a drive using "
                                                   "the storage page of the virtual machine settings window.</p>")
                                                   .arg(strMediumName, strMachineName));
}

/* static */
void UINotificationMessage::cannotSendACPIToMachine()
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't send shutdown signal ..."),
        QApplication::translate("UIMessageCenter", "You are trying to shut down the guest with the ACPI power button. "
                                                   "This is currently not possible because the guest does not support "
                                                   "software shut down."));
}

/* static */
void UINotificationMessage::remindAboutAutoCapture()
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Auto capture keyboard ..."),
        QApplication::translate("UIMessageCenter", "<p>You have the <b>Auto capture keyboard</b> option turned on. "
                                                   "This will cause the Virtual Machine to automatically <b>capture</b> "
                                                   "the keyboard every time the VM window is activated and make it "
                                                   "unavailable to other applications running on your host machine: "
                                                   "when the keyboard is captured, all keystrokes (including system ones "
                                                   "like Alt-Tab) will be directed to the VM.</p>"
                                                   "<p>You can press the <b>host key combo</b> at any time to <b>uncapture</b> the "
                                                   "keyboard and mouse (if it is captured) and return them to normal "
                                                   "operation. The currently assigned host key combo is shown on the status bar "
                                                   "at the bottom of the Virtual Machine window. This icon, together "
                                                   "with the mouse icon placed nearby, indicate the current keyboard "
                                                   "and mouse capture state.</p>") +
        QApplication::translate("UIMessageCenter", "<p>The host key combo is currently defined as <b>%1</b>.</p>",
                                                   "additional message box paragraph")
                                                   .arg(UIHostCombo::toReadableString(gEDataManager->hostKeyCombination())),
        "remindAboutAutoCapture");
}

/* static */
void UINotificationMessage::remindAboutGuestAdditionsAreNotActive()
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Guest additions inactive ..."),
        QApplication::translate("UIMessageCenter", "<p>The VirtualBox Guest Additions do not appear to be available on this "
                                                   "virtual machine, and shared folders cannot be used without them. To use "
                                                   "shared folders inside the virtual machine, please install the Guest "
                                                   "Additions if they are not installed, or re-install them if they are not "
                                                   "working correctly, by selecting <b>Insert Guest Additions CD image</b> from "
                                                   "the <b>Devices</b> menu. If they are installed but the machine is not yet "
                                                   "fully started then shared folders will be available once it is.</p>"),
        "remindAboutGuestAdditionsAreNotActive");
}

/* static */
void UINotificationMessage::remindAboutMouseIntegration(bool fSupportsAbsolute)
{
    if (fSupportsAbsolute)
    {
        createMessage(
            QApplication::translate("UIMessageCenter", "Mouse integration ..."),
            QApplication::translate("UIMessageCenter", "<p>The Virtual Machine reports that the guest OS supports <b>mouse "
                                                       "pointer integration</b>. This means that you do not need to "
                                                       "<i>capture</i> the mouse pointer to be able to use it in your guest "
                                                       "OS -- all mouse actions you perform when the mouse pointer is over the "
                                                       "Virtual Machine's display are directly sent to the guest OS. If the "
                                                       "mouse is currently captured, it will be automatically uncaptured.</p>"
                                                       "<p>The mouse icon on the status bar will look "
                                                       "like&nbsp;<img src=:/mouse_seamless_16px.png/>&nbsp;to inform you that "
                                                       "mouse pointer integration is supported by the guest OS and is currently "
                                                       "turned on.</p><p><b>Note</b>: Some applications may behave incorrectly "
                                                       "in mouse pointer integration mode. You can always disable it for the "
                                                       "current session (and enable it again) by selecting the corresponding "
                                                       "action from the menu bar.</p>"),
            "remindAboutMouseIntegration");
    }
    else
    {
        createMessage(
            QApplication::translate("UIMessageCenter", "Mouse integration ..."),
            QApplication::translate("UIMessageCenter", "<p>The Virtual Machine reports that the guest OS does not support "
                                                       "<b>mouse pointer integration</b> in the current video mode. You need to "
                                                       "capture the mouse (by clicking over the VM display or pressing the host "
                                                       "key) in order to use the mouse inside the guest OS.</p>"),
            "remindAboutMouseIntegration");
    }
}

/* static */
void UINotificationMessage::remindAboutPausedVMInput()
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Paused VM input ..."),
        QApplication::translate("UIMessageCenter", "<p>The Virtual Machine is currently in the <b>Paused</b> state and not able "
                                                   "to see any keyboard or mouse input. If you want to continue to work inside "
                                                   "the VM, you need to resume it by selecting the corresponding action from the "
                                                   "menu bar.</p>"),
        "remindAboutPausedVMInput");
}

/* static */
void UINotificationMessage::forgetAboutPausedVMInput()
{
    destroyMessage("remindAboutPausedVMInput");
}

/* static */
void UINotificationMessage::remindAboutWrongColorDepth(ulong uRealBPP, ulong uWantedBPP)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Wrong color depth ..."),
        QApplication::translate("UIMessageCenter", "<p>The virtual screen is currently set to a <b>%1&nbsp;bit</b> color mode. "
                                                   "For better performance please change this to <b>%2&nbsp;bit</b>. This can "
                                                   "usually be done from the <b>Display</b> section of the guest operating "
                                                   "system's Control Panel or System Settings.</p>")
                                                   .arg(uRealBPP).arg(uWantedBPP),
        "remindAboutWrongColorDepth");
}

/* static */
void UINotificationMessage::forgetAboutWrongColorDepth()
{
    destroyMessage("remindAboutWrongColorDepth");
}

/* static */
void UINotificationMessage::cannotAcquireVirtualBoxParameter(const CVirtualBox &comVBox,
                                                             UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "VirtualBox failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire VirtualBox parameter.") +
        UIErrorString::formatErrorInfo(comVBox),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotAcquireApplianceParameter(const CAppliance &comAppliance,
                                                            UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Appliance failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire appliance parameter.") +
        UIErrorString::formatErrorInfo(comAppliance),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotAcquirePlatformParameter(const CPlatform &comPlatform)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Platform failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire platform parameter.") +
        UIErrorString::formatErrorInfo(comPlatform));
}

/* static */
void UINotificationMessage::cannotAcquirePlatformPropertiesParameter(const CPlatformProperties &comProperties)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Platform properties failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire platform properties parameter.") +
        UIErrorString::formatErrorInfo(comProperties));
}

/* static */
void UINotificationMessage::cannotAcquireSystemPropertiesParameter(const CSystemProperties &comProperties)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "System properties failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire system properties parameter.") +
        UIErrorString::formatErrorInfo(comProperties));
}

/* static */
void UINotificationMessage::cannotAcquireExtensionPackManagerParameter(const CExtPackManager &comEPManager)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Extension Pack failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire Extension Pack Manager parameter.") +
        UIErrorString::formatErrorInfo(comEPManager));
}

/* static */
void UINotificationMessage::cannotAcquireExtensionPackParameter(const CExtPack &comPackage)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Extension Pack failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire Extension Pack parameter.") +
        UIErrorString::formatErrorInfo(comPackage));
}

/* static */
void UINotificationMessage::cannotAcquireHostParameter(const CHost &comHost)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Host failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire host parameter.") +
        UIErrorString::formatErrorInfo(comHost));
}

/* static */
void UINotificationMessage::cannotAcquireStorageControllerParameter(const CStorageController &comStorageController)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Storage controller failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire storage controller parameter.") +
        UIErrorString::formatErrorInfo(comStorageController));
}

/* static */
void UINotificationMessage::cannotChangeStorageControllerParameter(const CStorageController &comStorageController)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Storage controller failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change storage controller parameter.") +
        UIErrorString::formatErrorInfo(comStorageController));
}

/* static */
void UINotificationMessage::cannotAcquireMediumAttachmentParameter(const CMediumAttachment &comMediumAttachment)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Medium attachment failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire medium attachment parameter.") +
        UIErrorString::formatErrorInfo(comMediumAttachment));
}

/* static */
void UINotificationMessage::cannotAcquireMediumParameter(const CMedium &comMedium)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Medium failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire medium parameter.") +
        UIErrorString::formatErrorInfo(comMedium));
}

/* static */
void UINotificationMessage::cannotAcquireSessionParameter(const CSession &comSession)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Session failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire session parameter.") +
        UIErrorString::formatErrorInfo(comSession));
}

/* static */
void UINotificationMessage::cannotAcquireMachineParameter(const CMachine &comMachine)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Machine failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire machine parameter.") +
        UIErrorString::formatErrorInfo(comMachine));
}

/* static */
void UINotificationMessage::cannotAcquireMachineDebuggerParameter(const CMachineDebugger &comMachineDebugger)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Debugger failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire machine debugger parameter.") +
        UIErrorString::formatErrorInfo(comMachineDebugger));
}

/* static */
void UINotificationMessage::cannotAcquireGraphicsAdapterParameter(const CGraphicsAdapter &comAdapter)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Graphics adapter failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire graphics adapter parameter.") +
        UIErrorString::formatErrorInfo(comAdapter));
}

/* static */
void UINotificationMessage::cannotAcquireAudioSettingsParameter(const CAudioSettings &comSettings)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Audio settings failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire audio settings parameter.") +
        UIErrorString::formatErrorInfo(comSettings));
}

/* static */
void UINotificationMessage::cannotAcquireAudioAdapterParameter(const CAudioAdapter &comAdapter)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Audio adapter failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire audio adapter parameter.") +
        UIErrorString::formatErrorInfo(comAdapter));
}

/* static */
void UINotificationMessage::cannotAcquireNetworkAdapterParameter(const CNetworkAdapter &comAdapter)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Network adapter failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire network adapter parameter.") +
        UIErrorString::formatErrorInfo(comAdapter));
}

/* static */
void UINotificationMessage::cannotAcquireConsoleParameter(const CConsole &comConsole)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Console failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire console parameter.") +
        UIErrorString::formatErrorInfo(comConsole));
}

/* static */
void UINotificationMessage::cannotAcquireGuestParameter(const CGuest &comGuest)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Guest failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire guest parameter.") +
        UIErrorString::formatErrorInfo(comGuest));
}

/* static */
void UINotificationMessage::cannotAcquireGuestOSTypeParameter(const CGuestOSType &comGuestOSType)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Guest OS type failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire guest OS type parameter.") +
        UIErrorString::formatErrorInfo(comGuestOSType));
}

/* static */
void UINotificationMessage::cannotAcquireSnapshotParameter(const CSnapshot &comSnapshot)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Snapshot failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire snapshot parameter.") +
        UIErrorString::formatErrorInfo(comSnapshot));
}

/* static */
void UINotificationMessage::cannotAcquireDHCPServerParameter(const CDHCPServer &comServer)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "DHCP server failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire DHCP server parameter.") +
        UIErrorString::formatErrorInfo(comServer));
}

/* static */
void UINotificationMessage::cannotAcquireCloudNetworkParameter(const CCloudNetwork &comNetwork)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Cloud failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire cloud network parameter.") +
        UIErrorString::formatErrorInfo(comNetwork));
}

/* static */
void UINotificationMessage::cannotAcquireHostNetworkInterfaceParameter(const CHostNetworkInterface &comInterface)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Host network interface failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire host network interface parameter.") +
        UIErrorString::formatErrorInfo(comInterface));
}

/* static */
void UINotificationMessage::cannotAcquireHostOnlyNetworkParameter(const CHostOnlyNetwork &comNetwork)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Host only network failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire host only network parameter.") +
        UIErrorString::formatErrorInfo(comNetwork));
}

/* static */
void UINotificationMessage::cannotAcquireNATNetworkParameter(const CNATNetwork &comNetwork)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "NAT network failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire NAT network parameter.") +
        UIErrorString::formatErrorInfo(comNetwork));
}

/* static */
void UINotificationMessage::cannotAcquireDisplayParameter(const CDisplay &comDisplay)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Display failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire display parameter.") +
        UIErrorString::formatErrorInfo(comDisplay));
}

/* static */
void UINotificationMessage::cannotAcquireUpdateAgentParameter(const CUpdateAgent &comAgent)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Update failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire update agent parameter.") +
        UIErrorString::formatErrorInfo(comAgent));
}

/* static */
void UINotificationMessage::cannotAcquireMouseParameter(const CMouse &comMouse)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Mouse failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire mouse parameter.") +
        UIErrorString::formatErrorInfo(comMouse));
}

/* static */
void UINotificationMessage::cannotAcquireEmulatedUSBParameter(const CEmulatedUSB &comDispatcher)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Emulated USB failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire emulated USB parameter.") +
        UIErrorString::formatErrorInfo(comDispatcher));
}

/* static */
void UINotificationMessage::cannotAcquireRecordingSettingsParameter(const CRecordingSettings &comSettings)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Recording settings failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire recording settings parameter.") +
        UIErrorString::formatErrorInfo(comSettings));
}

/* static */
void UINotificationMessage::cannotAcquireVRDEServerParameter(const CVRDEServer &comServer)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "VRDE server failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire VRDE server parameter.") +
        UIErrorString::formatErrorInfo(comServer));
}

/* static */
void UINotificationMessage::cannotAcquireVRDEServerInfoParameter(const CVRDEServerInfo &comServerInfo)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "VRDE server info failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire VRDE server info parameter.") +
        UIErrorString::formatErrorInfo(comServerInfo));
}

/* static */
void UINotificationMessage::cannotAcquireVirtualSystemDescriptionParameter(const CVirtualSystemDescription &comVsd,
                                                                           UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "VSD failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire VSD parameter.") +
        UIErrorString::formatErrorInfo(comVsd),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotAcquireVirtualSystemDescriptionFormParameter(const CVirtualSystemDescriptionForm &comVsdForm,
                                                                               UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "VSD form failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire VSD form parameter.") +
        UIErrorString::formatErrorInfo(comVsdForm),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotAcquireCloudProviderManagerParameter(const CCloudProviderManager &comCloudProviderManager,
                                                                       UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Cloud failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire cloud provider manager parameter.") +
        UIErrorString::formatErrorInfo(comCloudProviderManager),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotAcquireCloudProviderParameter(const CCloudProvider &comCloudProvider,
                                                                UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Cloud failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire cloud provider parameter.") +
        UIErrorString::formatErrorInfo(comCloudProvider),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotAcquireCloudProfileParameter(const CCloudProfile &comCloudProfile,
                                                               UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Cloud failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire cloud profile parameter.") +
        UIErrorString::formatErrorInfo(comCloudProfile),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotAcquireCloudMachineParameter(const CCloudMachine &comCloudMachine,
                                                               UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Cloud failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire cloud machine parameter.") +
        UIErrorString::formatErrorInfo(comCloudMachine),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotChangeMediumParameter(const CMedium &comMedium)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Medium failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change the parameter of the medium <b>%1</b>.")
                                                   .arg(CMedium(comMedium).GetLocation()) +
        UIErrorString::formatErrorInfo(comMedium));
}

/* static */
void UINotificationMessage::cannotChangeMachineParameter(const CMachine &comMachine)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Machine failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change the parameter of the virtual machine <b>%1</b>.")
                                                   .arg(CMachine(comMachine).GetName()) +
        UIErrorString::formatErrorInfo(comMachine));
}

/* static */
void UINotificationMessage::cannotChangeMachineDebuggerParameter(const CMachineDebugger &comMachineDebugger)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Debugger failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change machine debugger parameter.") +
        UIErrorString::formatErrorInfo(comMachineDebugger));
}

/* static */
void UINotificationMessage::cannotChangeGraphicsAdapterParameter(const CGraphicsAdapter &comAdapter)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Graphics adapter failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change graphics adapter parameter.") +
        UIErrorString::formatErrorInfo(comAdapter));
}

/* static */
void UINotificationMessage::cannotChangeAudioAdapterParameter(const CAudioAdapter &comAdapter)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Audio adapter failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change audio adapter parameter.") +
        UIErrorString::formatErrorInfo(comAdapter));
}

/* static */
void UINotificationMessage::cannotChangeNetworkAdapterParameter(const CNetworkAdapter &comAdapter)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Network adapter failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change network adapter parameter.") +
        UIErrorString::formatErrorInfo(comAdapter));
}

/* static */
void UINotificationMessage::cannotChangeDHCPServerParameter(const CDHCPServer &comServer)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "DHCP server failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change DHCP server parameter.") +
        UIErrorString::formatErrorInfo(comServer));
}

/* static */
void UINotificationMessage::cannotChangeCloudNetworkParameter(const CCloudNetwork &comNetwork)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Cloud failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change cloud network parameter.") +
        UIErrorString::formatErrorInfo(comNetwork));
}

/* static */
void UINotificationMessage::cannotChangeHostNetworkInterfaceParameter(const CHostNetworkInterface &comInterface)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Host network interface failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change host network interface parameter.") +
        UIErrorString::formatErrorInfo(comInterface));
}

/* static */
void UINotificationMessage::cannotChangeHostOnlyNetworkParameter(const CHostOnlyNetwork &comNetwork)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Host only network failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change host only network parameter.") +
        UIErrorString::formatErrorInfo(comNetwork));
}

/* static */
void UINotificationMessage::cannotChangeNATNetworkParameter(const CNATNetwork &comNetwork)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "NAT network failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change NAT network parameter.") +
        UIErrorString::formatErrorInfo(comNetwork));
}

/* static */
void UINotificationMessage::cannotChangeDisplayParameter(const CDisplay &comDisplay)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Display failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change display parameter.") +
        UIErrorString::formatErrorInfo(comDisplay));
}

/* static */
void UINotificationMessage::cannotChangeCloudProfileParameter(const CCloudProfile &comProfile)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Cloud failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change cloud profile parameter.") +
        UIErrorString::formatErrorInfo(comProfile));
}

/* static */
void UINotificationMessage::cannotChangeUpdateAgentParameter(const CUpdateAgent &comAgent)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Update failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change update agent parameter.") +
        UIErrorString::formatErrorInfo(comAgent));
}

/* static */
void UINotificationMessage::cannotChangeKeyboardParameter(const CKeyboard &comKeyboard)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Keyboard failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change keyboard parameter.") +
        UIErrorString::formatErrorInfo(comKeyboard));
}

/* static */
void UINotificationMessage::cannotChangeVirtualSystemDescriptionParameter(const CVirtualSystemDescription &comVsd,
                                                                          UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "VSD failure ..."),
        QApplication::translate("UIMessageCenter", "Failed to change VSD parameter.") +
        UIErrorString::formatErrorInfo(comVsd),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotEnumerateHostUSBDevices(const CHost &comHost)
{
    /* Refer users to manual's trouble shooting section depending on the host platform: */
    QString strHelpKeyword;
#if defined(RT_OS_LINUX)
    strHelpKeyword = "ts_usb-linux";
#elif defined(RT_OS_WINDOWS)
    strHelpKeyword = "ts_win-guests";
#elif defined(RT_OS_SOLARIS)
    strHelpKeyword = "ts_sol-guests";
#elif defined(RT_OS_DARWIN)
#endif

    createMessage(
        QApplication::translate("UIMessageCenter", "Can't enumerate USB devices ..."),
        QApplication::translate("UIMessageCenter", "Failed to enumerate host USB devices.") +
        UIErrorString::formatErrorInfo(comHost),
        "cannotEnumerateHostUSBDevices",
        strHelpKeyword);
}

/* static */
void UINotificationMessage::cannotOpenMedium(const CVirtualBox &comVBox,
                                             const QString &strLocation,
                                             UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't open medium ..."),
        QApplication::translate("UIMessageCenter", "Failed to open the disk image file <nobr><b>%1</b></nobr>.")
                                                   .arg(strLocation) +
        UIErrorString::formatErrorInfo(comVBox),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotPauseMachine(const CConsole &comConsole)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't pause machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to pause the execution of the virtual machine <b>%1</b>.")
                                                   .arg(CConsole(comConsole).GetMachine().GetName()) +
        UIErrorString::formatErrorInfo(comConsole));
}

/* static */
void UINotificationMessage::cannotResumeMachine(const CConsole &comConsole)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't resume machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to resume the execution of the virtual machine <b>%1</b>.")
                                                   .arg(CConsole(comConsole).GetMachine().GetName()) +
        UIErrorString::formatErrorInfo(comConsole));
}

/* static */
void UINotificationMessage::cannotACPIShutdownMachine(const CConsole &comConsole)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't shut down machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to send the ACPI power button press event to the virtual machine "
                                                   "<b>%1</b>.").arg(CConsole(comConsole).GetMachine().GetName()) +
        UIErrorString::formatErrorInfo(comConsole));
}

/* static */
void UINotificationMessage::cannotResetMachine(const CConsole &comConsole)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't reset machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to reset the virtual machine "
                                                   "<b>%1</b>.").arg(CConsole(comConsole).GetMachine().GetName()) +
        UIErrorString::formatErrorInfo(comConsole));
}

/* static */
void UINotificationMessage::cannotCreateAppliance(const CVirtualBox &comVBox,
                                                  UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create appliance ..."),
        QApplication::translate("UIMessageCenter", "Failed to create appliance.") +
        UIErrorString::formatErrorInfo(comVBox),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotRegisterMachine(const CVirtualBox &comVBox,
                                                  const QString &strName,
                                                  UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't register machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to register machine <b>%1</b>.")
                                                   .arg(strName) +
        UIErrorString::formatErrorInfo(comVBox),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotCreateMachine(const CVirtualBox &comVBox,
                                                UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to create machine.") +
        UIErrorString::formatErrorInfo(comVBox),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotFindMachineById(const CVirtualBox &comVBox,
                                                  const QUuid &uMachineId,
                                                  UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't find machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to find the machine with following ID: <nobr><b>%1</b></nobr>.")
                                                   .arg(uMachineId.toString()) +
        UIErrorString::formatErrorInfo(comVBox),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotOpenMachine(const CVirtualBox &comVBox, const QString &strLocation)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't open machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to open virtual machine located in %1.")
                                                   .arg(strLocation) +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotCreateMediumStorage(const CVirtualBox &comVBox,
                                                      const QString &strPath,
                                                      UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create medium storage ..."),
        QApplication::translate("UIMessageCenter", "Failed to create medium storage at <nobr><b>%1</b></nobr>.")
                                                   .arg(strPath) +
        UIErrorString::formatErrorInfo(comVBox),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotGetExtensionPackManager(const CVirtualBox &comVBox)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't get Extension Pack Manager ..."),
        QApplication::translate("UIMessageCenter", "Failed to acquire Extension Pack Manager.") +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotCreateVfsExplorer(const CAppliance &comAppliance, UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create VFS explorer ..."),
        QApplication::translate("UIMessageCenter", "Failed to create VFS explorer to check files.") +
        UIErrorString::formatErrorInfo(comAppliance),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotAddDiskEncryptionPassword(const CAppliance &comAppliance, UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Bad password ..."),
        QApplication::translate("UIMessageCenter", "Bad password or authentication failure.") +
        UIErrorString::formatErrorInfo(comAppliance),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotInterpretAppliance(const CAppliance &comAppliance, UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't interpret appliance ..."),
        QApplication::translate("UIMessageCenter", "Failed to interpret appliance being imported.") +
        UIErrorString::formatErrorInfo(comAppliance),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotCreateVirtualSystemDescription(const CAppliance &comAppliance, UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create VSD ..."),
        QApplication::translate("UIMessageCenter", "Failed to create VSD.") +
        UIErrorString::formatErrorInfo(comAppliance),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotOpenExtPack(const CExtPackManager &comExtPackManager, const QString &strFilename)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't open extension pack ..."),
        QApplication::translate("UIMessageCenter", "Failed to open the Extension Pack <b>%1</b>.")
                                                   .arg(strFilename) +
        UIErrorString::formatErrorInfo(comExtPackManager));
}

/* static */
void UINotificationMessage::cannotReadExtPack(const CExtPackFile &comExtPackFile, const QString &strFilename)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't read extension pack ..."),
        QApplication::translate("UIMessageCenter", "Failed to read the Extension Pack <b>%1</b>.")
                                                   .arg(strFilename) +
        comExtPackFile.GetWhyUnusable());
}

/* static */
void UINotificationMessage::cannotFindCloudNetwork(const CVirtualBox &comVBox, const QString &strNetworkName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't find cloud network ..."),
        QApplication::translate("UIMessageCenter", "Unable to find the cloud network <b>%1</b>.")
                                                   .arg(strNetworkName) +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotFindHostNetworkInterface(const CHost &comHost, const QString &strInterfaceName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't find host network interface ..."),
        QApplication::translate("UIMessageCenter", "Unable to find the host network interface <b>%1</b>.")
                                                   .arg(strInterfaceName) +
        UIErrorString::formatErrorInfo(comHost));
}

/* static */
void UINotificationMessage::cannotFindHostOnlyNetwork(const CVirtualBox &comVBox, const QString &strNetworkName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't find host only network ..."),
        QApplication::translate("UIMessageCenter", "Unable to find the host only network <b>%1</b>.")
                                                   .arg(strNetworkName) +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotFindNATNetwork(const CVirtualBox &comVBox, const QString &strNetworkName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't find NAT network ..."),
        QApplication::translate("UIMessageCenter", "Unable to find the NAT network <b>%1</b>.")
                                                   .arg(strNetworkName) +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotCreateDHCPServer(const CVirtualBox &comVBox, const QString &strInterfaceName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create DHCP server ..."),
        QApplication::translate("UIMessageCenter", "Failed to create a DHCP server for the network interface <b>%1</b>.")
                                                   .arg(strInterfaceName) +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotRemoveDHCPServer(const CVirtualBox &comVBox, const QString &strInterfaceName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't remove DHCP server ..."),
        QApplication::translate("UIMessageCenter", "Failed to remove the DHCP server for the network interface <b>%1</b>.")
                                                   .arg(strInterfaceName) +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotCreateCloudNetwork(const CVirtualBox &comVBox)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create cloud network ..."),
        QApplication::translate("UIMessageCenter", "Failed to create a cloud network.") +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotRemoveCloudNetwork(const CVirtualBox &comVBox, const QString &strNetworkName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't remove cloud network ..."),
        QApplication::translate("UIMessageCenter", "Failed to remove the cloud network <b>%1</b>.")
                                                   .arg(strNetworkName) +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotCreateHostOnlyNetwork(const CVirtualBox &comVBox)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create host only network ..."),
        QApplication::translate("UIMessageCenter", "Failed to create a host only network.") +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotRemoveHostOnlyNetwork(const CVirtualBox &comVBox, const QString &strNetworkName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't remove host only network ..."),
        QApplication::translate("UIMessageCenter", "Failed to remove the host only network <b>%1</b>.")
                                                   .arg(strNetworkName) +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotCreateNATNetwork(const CVirtualBox &comVBox)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create NAT network ..."),
        QApplication::translate("UIMessageCenter", "Failed to create a NAT network.") +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotRemoveNATNetwork(const CVirtualBox &comVBox, const QString &strNetworkName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't remove NAT network ..."),
        QApplication::translate("UIMessageCenter", "Failed to remove the NAT network <b>%1</b>.")
                                                   .arg(strNetworkName) +
        UIErrorString::formatErrorInfo(comVBox));
}

/* static */
void UINotificationMessage::cannotCreateCloudProfile(const CCloudProvider &comProvider)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create cloud profile ..."),
        QApplication::translate("UIMessageCenter", "Failed to create cloud profile.") +
        UIErrorString::formatErrorInfo(comProvider));
}

/* static */
void UINotificationMessage::cannotRemoveCloudProfile(const CCloudProfile &comProfile)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't remove cloud profile ..."),
        QApplication::translate("UIMessageCenter", "Failed to remove cloud profile.") +
        UIErrorString::formatErrorInfo(comProfile));
}

/* static */
void UINotificationMessage::cannotSaveCloudProfiles(const CCloudProvider &comProvider)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't save cloud profiles ..."),
        QApplication::translate("UIMessageCenter", "Failed to save cloud profiles.") +
        UIErrorString::formatErrorInfo(comProvider));
}

/* static */
void UINotificationMessage::cannotImportCloudProfiles(const CCloudProvider &comProvider)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't import cloud profiles ..."),
        QApplication::translate("UIMessageCenter", "Failed to import cloud profiles.") +
        UIErrorString::formatErrorInfo(comProvider));
}

/* static */
void UINotificationMessage::cannotRefreshCloudMachine(const CCloudMachine &comMachine)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't refresh cloud machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to refresh cloud machine.") +
        UIErrorString::formatErrorInfo(comMachine));
}

/* static */
void UINotificationMessage::cannotRefreshCloudMachine(const CProgress &comProgress)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't refresh cloud machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to refresh cloud machine.") +
        UIErrorString::formatErrorInfo(comProgress));
}

/* static */
void UINotificationMessage::cannotCreateCloudClient(const CCloudProfile &comProfile, UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't create cloud client ..."),
        QApplication::translate("UIMessageCenter", "Failed to create cloud client.") +
        UIErrorString::formatErrorInfo(comProfile),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotCloseMedium(const CMedium &comMedium)
{
    /* Show the error: */
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't close medium ..."),
        QApplication::translate("UIMessageCenter", "Failed to close the disk image file <nobr><b>%1</b></nobr>.")
                                                   .arg(CMedium(comMedium).GetLocation()) +
        UIErrorString::formatErrorInfo(comMedium));
}

/* static */
void UINotificationMessage::cannotDiscardSavedState(const CMachine &comMachine)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't discard saved state ..."),
        QApplication::translate("UIMessageCenter", "Failed to discard the saved state of the virtual machine <b>%1</b>.")
                                                   .arg(CMachine(comMachine).GetName()) +
        UIErrorString::formatErrorInfo(comMachine));
}

/* static */
void UINotificationMessage::cannotRemoveMachine(const CMachine &comMachine, UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't remove machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to remove the virtual machine <b>%1</b>.")
                                                   .arg(CMachine(comMachine).GetName()) +
        UIErrorString::formatErrorInfo(comMachine),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotExportMachine(const CMachine &comMachine, UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't export machine ..."),
        QApplication::translate("UIMessageCenter", "Failed to export virtual machine <b>%1</b>.")
                                                   .arg(CMachine(comMachine).GetName()) +
        UIErrorString::formatErrorInfo(comMachine),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotAttachDevice(const CMachine &comMachine,
                                               UIMediumDeviceType enmType,
                                               const QString &strLocation,
                                               const StorageSlot &storageSlot,
                                               UINotificationCenter *pParent /* = 0 */)
{
    QString strMessage;
    switch (enmType)
    {
        case UIMediumDeviceType_HardDisk:
        {
            strMessage = QApplication::translate("UIMessageCenter", "Failed to attach the hard disk (<nobr><b>%1</b></nobr>) to "
                                                                    "the slot <i>%2</i> of the machine <b>%3</b>.")
                                                                    .arg(strLocation)
                                                                    .arg(gpConverter->toString(storageSlot))
                                                                    .arg(CMachine(comMachine).GetName());
            break;
        }
        case UIMediumDeviceType_DVD:
        {
            strMessage = QApplication::translate("UIMessageCenter", "Failed to attach the optical drive (<nobr><b>%1</b></nobr>) "
                                                                    "to the slot <i>%2</i> of the machine <b>%3</b>.")
                                                                    .arg(strLocation)
                                                                    .arg(gpConverter->toString(storageSlot))
                                                                    .arg(CMachine(comMachine).GetName());
            break;
        }
        case UIMediumDeviceType_Floppy:
        {
            strMessage = QApplication::translate("UIMessageCenter", "Failed to attach the floppy drive (<nobr><b>%1</b></nobr>) "
                                                                    "to the slot <i>%2</i> of the machine <b>%3</b>.")
                                                                    .arg(strLocation)
                                                                    .arg(gpConverter->toString(storageSlot))
                                                                    .arg(CMachine(comMachine).GetName());
            break;
        }
        default:
            break;
    }
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't attach device ..."),
        strMessage + UIErrorString::formatErrorInfo(comMachine),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotFindSnapshotById(const CMachine &comMachine, const QUuid &uId)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't find snapshot ..."),
        QApplication::translate("UIMessageCenter", "Failed to find snapshot with ID=<b>%1</b>.")
                                                   .arg(uId.toString()) +
        UIErrorString::formatErrorInfo(comMachine));
}

/* static */
void UINotificationMessage::cannotFindSnapshotByName(const CMachine &comMachine,
                                                     const QString &strName,
                                                     UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't find snapshot ..."),
        QApplication::translate("UIMessageCenter", "Failed to find snapshot with name=<b>%1</b>.")
                                                   .arg(strName) +
        UIErrorString::formatErrorInfo(comMachine),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotChangeSnapshot(const CSnapshot &comSnapshot,
                                                 const QString &strSnapshotName,
                                                 const QString &strMachineName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't change snapshot ..."),
        QApplication::translate("UIMessageCenter", "Failed to change the snapshot <b>%1</b> of the virtual machine <b>%2</b>.")
                                                   .arg(strSnapshotName, strMachineName) +
        UIErrorString::formatErrorInfo(comSnapshot));
}

/* static */
void UINotificationMessage::cannotRunUnattendedGuestInstall(const CUnattended &comUnattended)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't run guest install ..."),
        QApplication::translate("UIMessageCenter", "Failed to run unattended guest installation.") +
        UIErrorString::formatErrorInfo(comUnattended));
}

/* static */
void UINotificationMessage::cannotAttachUSBDevice(const CConsole &comConsole, const QString &strDevice)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't attach USB device ..."),
        QApplication::translate("UIMessageCenter", "Failed to attach the USB device <b>%1</b> to the virtual machine <b>%2</b>.")
                                .arg(strDevice, CConsole(comConsole).GetMachine().GetName()) +
        UIErrorString::formatErrorInfo(comConsole));
}

/* static */
void UINotificationMessage::cannotAttachUSBDevice(const CVirtualBoxErrorInfo &comErrorInfo,
                                                  const QString &strDevice, const QString &strMachineName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't attach USB device ..."),
        QApplication::translate("UIMessageCenter", "Failed to attach the USB device <b>%1</b> to the virtual machine <b>%2</b>.")
                                .arg(strDevice, strMachineName) +
        UIErrorString::formatErrorInfo(comErrorInfo));
}

/* static */
void UINotificationMessage::cannotDetachUSBDevice(const CConsole &comConsole, const QString &strDevice)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't detach USB device ..."),
        QApplication::translate("UIMessageCenter", "Failed to detach the USB device <b>%1</b> from the virtual machine <b>%2</b>.")
                                .arg(strDevice, CConsole(comConsole).GetMachine().GetName()) +
        UIErrorString::formatErrorInfo(comConsole));
}

/* static */
void UINotificationMessage::cannotDetachUSBDevice(const CVirtualBoxErrorInfo &comErrorInfo,
                                                  const QString &strDevice, const QString &strMachineName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't detach USB device ..."),
        QApplication::translate("UIMessageCenter", "Failed to detach the USB device <b>%1</b> from the virtual machine <b>%2</b>.")
                                .arg(strDevice, strMachineName) +
        UIErrorString::formatErrorInfo(comErrorInfo));
}

/* static */
void UINotificationMessage::cannotAttachWebCam(const CEmulatedUSB &comDispatcher,
                                               const QString &strWebCamName, const QString &strMachineName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't attach webcam ..."),
        QApplication::translate("UIMessageCenter", "Failed to attach the webcam <b>%1</b> to the virtual machine <b>%2</b>.")
                                .arg(strWebCamName, strMachineName) +
        UIErrorString::formatErrorInfo(comDispatcher));
}

/* static */
void UINotificationMessage::cannotDetachWebCam(const CEmulatedUSB &comDispatcher,
                                               const QString &strWebCamName, const QString &strMachineName)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't detach webcam ..."),
        QApplication::translate("UIMessageCenter", "Failed to detach the webcam <b>%1</b> from the virtual machine <b>%2</b>.")
                                .arg(strWebCamName, strMachineName) +
        UIErrorString::formatErrorInfo(comDispatcher));
}

/* static */
void UINotificationMessage::cannotSaveMachineSettings(const CMachine &comMachine, UINotificationCenter *pParent /* = 0 */)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't save machine settings ..."),
        QApplication::translate("UIMessageCenter", "Failed to save the settings of the virtual machine <b>%1</b> to "
                                                   "<b><nobr>%2</nobr></b>.")
                                                   .arg(CMachine(comMachine).GetName(),
                                                        CMachine(comMachine).GetSettingsFilePath()) +
        UIErrorString::formatErrorInfo(comMachine),
        QString(), QString(), pParent);
}

/* static */
void UINotificationMessage::cannotToggleAudioInput(const CAudioAdapter &comAdapter,
                                                   const QString &strMachineName, bool fEnable)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't toggle audio input ..."),
        (  fEnable
         ? QApplication::translate("UIMessageCenter", "Failed to enable the audio adapter input for the virtual machine <b>%1</b>.")
                                   .arg(strMachineName)
         : QApplication::translate("UIMessageCenter", "Failed to disable the audio adapter input for the virtual machine <b>%1</b>.")
                                   .arg(strMachineName)) +
        UIErrorString::formatErrorInfo(comAdapter));
}

/* static */
void UINotificationMessage::cannotToggleAudioOutput(const CAudioAdapter &comAdapter,
                                                    const QString &strMachineName, bool fEnable)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't toggle audio output ..."),
        (  fEnable
         ? QApplication::translate("UIMessageCenter", "Failed to enable the audio adapter output for the virtual machine <b>%1</b>.")
                                   .arg(strMachineName)
         : QApplication::translate("UIMessageCenter", "Failed to disable the audio adapter output for the virtual machine <b>%1</b>.")
                                   .arg(strMachineName)) +
        UIErrorString::formatErrorInfo(comAdapter));
}

/* static */
void UINotificationMessage::cannotToggleNetworkCable(const CNetworkAdapter &comAdapter,
                                                     const QString &strMachineName, bool fConnect)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't toggle network cable ..."),
        (  fConnect
         ? QApplication::translate("UIMessageCenter", "Failed to connect the network adapter cable of the virtual machine <b>%1</b>.")
                                   .arg(strMachineName)
         : QApplication::translate("UIMessageCenter", "Failed to disconnect the network adapter cable of the virtual machine <b>%1</b>.")
                                   .arg(strMachineName)) +
        UIErrorString::formatErrorInfo(comAdapter));
}

/* static */
void UINotificationMessage::cannotToggleRecording(const CRecordingSettings &comRecording, const QString &strMachineName, bool fEnable)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't toggle recording ..."),
        (  fEnable
         ? QApplication::translate("UIMessageCenter", "Failed to enable recording for the virtual machine <b>%1</b>.")
                                   .arg(strMachineName)
         : QApplication::translate("UIMessageCenter", "Failed to disable recording for the virtual machine <b>%1</b>.")
                                   .arg(strMachineName)) +
        UIErrorString::formatErrorInfo(comRecording));
}

/* static */
void UINotificationMessage::cannotToggleVRDEServer(const CVRDEServer &comServer,
                                                   const QString &strMachineName, bool fEnable)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't toggle VRDE server ..."),
        (  fEnable
         ? QApplication::translate("UIMessageCenter", "Failed to enable the remote desktop server for the virtual machine <b>%1</b>.")
                                   .arg(strMachineName)
         : QApplication::translate("UIMessageCenter", "Failed to disable the remote desktop server for the virtual machine <b>%1</b>.")
                                   .arg(strMachineName)) +
        UIErrorString::formatErrorInfo(comServer));
}

#ifdef VBOX_WITH_DRAG_AND_DROP
/* static */
void UINotificationMessage::cannotDropDataToGuest(const CDnDTarget &comDndTarget)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't drop data to guest ..."),
        UIErrorString::formatErrorInfo(comDndTarget));
}

/* static */
void UINotificationMessage::cannotDropDataToGuest(const CProgress &comProgress)
{
    createMessage(
        QApplication::translate("UIMessageCenter", "Can't drop data to guest ..."),
        UIErrorString::formatErrorInfo(comProgress));
}
#endif /* VBOX_WITH_DRAG_AND_DROP */

UINotificationMessage::UINotificationMessage(const QString &strName,
                                             const QString &strDetails,
                                             const QString &strInternalName,
                                             const QString &strHelpKeyword)
    : UINotificationSimple(strName,
                           strDetails,
                           strInternalName,
                           strHelpKeyword)
{
}

UINotificationMessage::~UINotificationMessage()
{
    /* Remove message from known: */
    m_messages.remove(m_strInternalName);
}

/* static */
void UINotificationMessage::createMessage(const QString &strName,
                                          const QString &strDetails,
                                          const QString &strInternalName /* = QString() */,
                                          const QString &strHelpKeyword /* = QString() */,
                                          UINotificationCenter *pParent /* = 0 */)
{
    /* Check if message suppressed: */
    if (isSuppressed(strInternalName))
        return;
    /* Check if message already exists: */
    if (   !strInternalName.isEmpty()
        && m_messages.contains(strInternalName))
        return;

    /* Choose effective parent: */
    UINotificationCenter *pEffectiveParent = pParent ? pParent : gpNotificationCenter;

    /* Create message finally: */
    const QUuid uId = pEffectiveParent->append(new UINotificationMessage(strName,
                                                                         strDetails,
                                                                         strInternalName,
                                                                         strHelpKeyword));
    if (!strInternalName.isEmpty())
        m_messages[strInternalName] = uId;
}

/* static */
void UINotificationMessage::destroyMessage(const QString &strInternalName,
                                           UINotificationCenter *pParent /* = 0 */)
{
    /* Check if message really exists: */
    if (!m_messages.contains(strInternalName))
        return;

    /* Choose effective parent: */
    UINotificationCenter *pEffectiveParent = pParent ? pParent : gpNotificationCenter;

    /* Destroy message finally: */
    pEffectiveParent->revoke(m_messages.value(strInternalName));
    m_messages.remove(strInternalName);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMediumCreate implementation.                                                                     *
*********************************************************************************************************************************/

UINotificationProgressMediumCreate::UINotificationProgressMediumCreate(const CMedium &comTarget,
                                                                       qulonglong uSize,
                                                                       const QVector<KMediumVariant> &variants)
    : m_comTarget(comTarget)
    , m_uSize(uSize)
    , m_variants(variants)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMediumCreate::sltHandleProgressFinished);
}

QString UINotificationProgressMediumCreate::name() const
{
    return UINotificationProgress::tr("Creating medium ...");
}

QString UINotificationProgressMediumCreate::details() const
{
    return UINotificationProgress::tr("<b>Location:</b> %1<br><b>Size:</b> %2").arg(m_strLocation, UITranslator::formatSize(m_uSize));
}

CProgress UINotificationProgressMediumCreate::createProgress(COMResult &comResult)
{
    /* Acquire location: */
    m_strLocation = m_comTarget.GetLocation();
    if (!m_comTarget.isOk())
    {
        /* Store COM result: */
        comResult = m_comTarget;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comTarget.CreateBaseStorage(m_uSize, m_variants);
    /* Store COM result: */
    comResult = m_comTarget;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMediumCreate::sltHandleProgressFinished()
{
    if (m_comTarget.isNotNull() && !m_comTarget.GetId().isNull())
        emit sigMediumCreated(m_comTarget);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMediumCopy implementation.                                                                       *
*********************************************************************************************************************************/

UINotificationProgressMediumCopy::UINotificationProgressMediumCopy(const CMedium &comSource,
                                                                   const CMedium &comTarget,
                                                                   const QVector<KMediumVariant> &variants,
                                                                   qulonglong uMediumSize)
    : m_comSource(comSource)
    , m_comTarget(comTarget)
    , m_variants(variants)
    , m_uMediumSize(uMediumSize)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMediumCopy::sltHandleProgressFinished);
}

QString UINotificationProgressMediumCopy::name() const
{
    return UINotificationProgress::tr("Copying medium ...");
}

QString UINotificationProgressMediumCopy::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1<br><b>To:</b> %2").arg(m_strSourceLocation, m_strTargetLocation);
}

CProgress UINotificationProgressMediumCopy::createProgress(COMResult &comResult)
{
    /* Acquire locations: */
    m_strSourceLocation = m_comSource.GetLocation();
    if (!m_comSource.isOk())
    {
        /* Store COM result: */
        comResult = m_comSource;
        /* Return progress-wrapper: */
        return CProgress();
    }
    m_strTargetLocation = m_comTarget.GetLocation();
    if (!m_comTarget.isOk())
    {
        /* Store COM result: */
        comResult = m_comTarget;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comSource.ResizeAndCloneTo(m_comTarget, m_uMediumSize, m_variants, CMedium());
    /* Store COM result: */
    comResult = m_comSource;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMediumCopy::sltHandleProgressFinished()
{
    if (m_comTarget.isNotNull() && !m_comTarget.GetId().isNull())
        emit sigMediumCopied(m_comTarget);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMediumMove implementation.                                                                       *
*********************************************************************************************************************************/

UINotificationProgressMediumMove::UINotificationProgressMediumMove(const CMedium &comMedium,
                                                                   const QString &strLocation)
    : m_comMedium(comMedium)
    , m_strTo(strLocation)
{
}

QString UINotificationProgressMediumMove::name() const
{
    return UINotificationProgress::tr("Moving medium ...");
}

QString UINotificationProgressMediumMove::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1<br><b>To:</b> %2").arg(m_strFrom, m_strTo);
}

CProgress UINotificationProgressMediumMove::createProgress(COMResult &comResult)
{
    /* Acquire location: */
    m_strFrom = m_comMedium.GetLocation();
    if (!m_comMedium.isOk())
    {
        /* Store COM result: */
        comResult = m_comMedium;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMedium.MoveTo(m_strTo);
    /* Store COM result: */
    comResult = m_comMedium;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMediumResize implementation.                                                                     *
*********************************************************************************************************************************/

UINotificationProgressMediumResize::UINotificationProgressMediumResize(const CMedium &comMedium,
                                                                       qulonglong uOldSize,
                                                                       qulonglong uNewSize)
    : m_comMedium(comMedium)
    , m_uFrom(uOldSize)
    , m_uTo(uNewSize)
{
}

QString UINotificationProgressMediumResize::name() const
{
    return UINotificationProgress::tr("Resizing medium ...");
}

QString UINotificationProgressMediumResize::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1<br><b>To:</b> %2")
                                      .arg(UITranslator::formatSize(m_uFrom),
                                           UITranslator::formatSize(m_uTo));
}

CProgress UINotificationProgressMediumResize::createProgress(COMResult &comResult)
{
    /* Acquire size: */
    m_uFrom = m_comMedium.GetLogicalSize();
    if (!m_comMedium.isOk())
    {
        /* Store COM result: */
        comResult = m_comMedium;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMedium.Resize(m_uTo);
    /* Store COM result: */
    comResult = m_comMedium;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMediumDeletingStorage implementation.                                                            *
*********************************************************************************************************************************/

UINotificationProgressMediumDeletingStorage::UINotificationProgressMediumDeletingStorage(const CMedium &comMedium)
    : m_comMedium(comMedium)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMediumDeletingStorage::sltHandleProgressFinished);
}

QString UINotificationProgressMediumDeletingStorage::name() const
{
    return UINotificationProgress::tr("Deleting medium storage ...");
}

QString UINotificationProgressMediumDeletingStorage::details() const
{
    return UINotificationProgress::tr("<b>Location:</b> %1").arg(m_strLocation);
}

CProgress UINotificationProgressMediumDeletingStorage::createProgress(COMResult &comResult)
{
    /* Acquire location: */
    m_strLocation = m_comMedium.GetLocation();
    if (!m_comMedium.isOk())
    {
        /* Store COM result: */
        comResult = m_comMedium;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMedium.DeleteStorage();
    /* Store COM result: */
    comResult = m_comMedium;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMediumDeletingStorage::sltHandleProgressFinished()
{
    if (!error().isEmpty())
        emit sigMediumStorageDeleted(m_comMedium);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachineCopy implementation.                                                                      *
*********************************************************************************************************************************/

UINotificationProgressMachineCopy::UINotificationProgressMachineCopy(const CMachine &comSource,
                                                                     const CMachine &comTarget,
                                                                     const KCloneMode &enmCloneMode,
                                                                     const QVector<KCloneOptions> &options)
    : m_comSource(comSource)
    , m_comTarget(comTarget)
    , m_enmCloneMode(enmCloneMode)
    , m_options(options)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMachineCopy::sltHandleProgressFinished);
}

QString UINotificationProgressMachineCopy::name() const
{
    return UINotificationProgress::tr("Copying machine ...");
}

QString UINotificationProgressMachineCopy::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1<br><b>To:</b> %2").arg(m_strSourceName, m_strTargetName);
}

CProgress UINotificationProgressMachineCopy::createProgress(COMResult &comResult)
{
    /* Acquire names: */
    m_strSourceName = m_comSource.GetName();
    if (!m_comSource.isOk())
    {
        /* Store COM result: */
        comResult = m_comSource;
        /* Return progress-wrapper: */
        return CProgress();
    }
    m_strTargetName = m_comTarget.GetName();
    if (!m_comTarget.isOk())
    {
        /* Store COM result: */
        comResult = m_comTarget;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comSource.CloneTo(m_comTarget, m_enmCloneMode, m_options);
    /* Store COM result: */
    comResult = m_comSource;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMachineCopy::sltHandleProgressFinished()
{
    if (m_comTarget.isNotNull() && !m_comTarget.GetId().isNull())
    {
        /* Register created machine: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();
        comVBox.RegisterMachine(m_comTarget);
        if (!comVBox.isOk())
            UINotificationMessage::cannotRegisterMachine(comVBox, m_comTarget.GetName());
    }
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachinePowerUp implementation.                                                                   *
*********************************************************************************************************************************/

UINotificationProgressMachinePowerUp::UINotificationProgressMachinePowerUp(const CMachine &comMachine, UILaunchMode enmLaunchMode)
    : m_comMachine(comMachine)
    , m_enmLaunchMode(enmLaunchMode)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMachinePowerUp::sltHandleProgressFinished);
}

QString UINotificationProgressMachinePowerUp::name() const
{
    return UINotificationProgress::tr("Powering VM up ...");
}

QString UINotificationProgressMachinePowerUp::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressMachinePowerUp::createProgress(COMResult &comResult)
{
    /* Acquire VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Open a session thru which we will modify the machine: */
    m_comSession.createInstance(CLSID_Session);
    if (m_comSession.isNull())
    {
        comResult = m_comSession;
        return CProgress();
    }

    /* Configure environment: */
    QVector<QString> astrEnv;
#ifdef VBOX_WS_WIN
    /* Allow started VM process to be foreground window: */
    AllowSetForegroundWindow(ASFW_ANY);
#endif
#ifdef VBOX_WS_NIX
    /* Make sure VM process will start on the same
     * display as the VirtualBox Manager: */
    const char *pDisplay = RTEnvGet("DISPLAY");
    if (pDisplay)
        astrEnv.append(QString("DISPLAY=%1").arg(pDisplay));
    const char *pXauth = RTEnvGet("XAUTHORITY");
    if (pXauth)
        astrEnv.append(QString("XAUTHORITY=%1").arg(pXauth));
#endif
    QString strType;
    switch (m_enmLaunchMode)
    {
        case UILaunchMode_Default:  strType = ""; break;
        case UILaunchMode_Separate: strType = "separate"; break;
        case UILaunchMode_Headless: strType = "headless"; break;
        default: AssertFailedReturn(CProgress());
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.LaunchVMProcess(m_comSession, strType, astrEnv);
//    /* If the VM is started separately and the VM process is already running, then it is OK. */
//    if (m_enmLaunchMode == UILaunchMode_Separate)
//    {
//        const KMachineState enmState = comMachine.GetState();
//        if (   enmState >= KMachineState_FirstOnline
//            && enmState <= KMachineState_LastOnline)
//        {
//            /* Already running: */
//            return;
//        }
//    }
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMachinePowerUp::sltHandleProgressFinished()
{
    /* Unlock session finally: */
    m_comSession.UnlockMachine();
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachineMove implementation.                                                                      *
*********************************************************************************************************************************/

UINotificationProgressMachineMove::UINotificationProgressMachineMove(const QUuid &uId,
                                                                     const QString &strDestination,
                                                                     const QString &strType)
    : m_uId(uId)
    , m_strDestination(QDir::toNativeSeparators(strDestination))
    , m_strType(strType)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMachineMove::sltHandleProgressFinished);
}

QString UINotificationProgressMachineMove::name() const
{
    return UINotificationProgress::tr("Moving machine ...");
}

QString UINotificationProgressMachineMove::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1<br><b>To:</b> %2").arg(m_strSource, m_strDestination);
}

CProgress UINotificationProgressMachineMove::createProgress(COMResult &comResult)
{
    /* Open a session thru which we will modify the machine: */
    m_comSession = openSession(m_uId, KLockType_Write);
    if (m_comSession.isNull())
        return CProgress();

    /* Get session machine: */
    CMachine comMachine = m_comSession.GetMachine();
    if (!m_comSession.isOk())
    {
        comResult = m_comSession;
        m_comSession.UnlockMachine();
        return CProgress();
    }

    /* Acquire VM source: */
    const QString strSettingFilePath = comMachine.GetSettingsFilePath();
    if (!comMachine.isOk())
    {
        comResult = comMachine;
        m_comSession.UnlockMachine();
        return CProgress();
    }
    QDir parentDir = QFileInfo(strSettingFilePath).absoluteDir();
    parentDir.cdUp();
    m_strSource = QDir::toNativeSeparators(parentDir.absolutePath());

    /* Initialize progress-wrapper: */
    CProgress comProgress = comMachine.MoveTo(m_strDestination, m_strType);
    /* Store COM result: */
    comResult = comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMachineMove::sltHandleProgressFinished()
{
    /* Unlock session finally: */
    m_comSession.UnlockMachine();
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachineSaveState implementation.                                                                 *
*********************************************************************************************************************************/

UINotificationProgressMachineSaveState::UINotificationProgressMachineSaveState(const CMachine &comMachine)
    : m_comMachine(comMachine)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMachineSaveState::sltHandleProgressFinished);
}

QString UINotificationProgressMachineSaveState::name() const
{
    return UINotificationProgress::tr("Saving VM state ...");
}

QString UINotificationProgressMachineSaveState::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressMachineSaveState::createProgress(COMResult &comResult)
{
    /* Acquire VM id: */
    const QUuid uId = m_comMachine.GetId();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Acquire VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Prepare machine to save: */
    CMachine comMachine = m_comMachine;

    /* For Manager UI: */
    switch (uiCommon().uiType())
    {
        case UIType_ManagerUI:
        {
            /* Open a session thru which we will modify the machine: */
            m_comSession = openExistingSession(uId);
            if (m_comSession.isNull())
                return CProgress();

            /* Get session machine: */
            comMachine = m_comSession.GetMachine();
            if (!m_comSession.isOk())
            {
                comResult = m_comSession;
                m_comSession.UnlockMachine();
                return CProgress();
            }

            /* Get machine state: */
            const KMachineState enmState = comMachine.GetState();
            if (!comMachine.isOk())
            {
                comResult = comMachine;
                m_comSession.UnlockMachine();
                return CProgress();
            }

            /* If VM isn't yet paused: */
            if (enmState != KMachineState_Paused)
            {
                /* Get session console: */
                CConsole comConsole = m_comSession.GetConsole();
                if (!m_comSession.isOk())
                {
                    comResult = m_comSession;
                    m_comSession.UnlockMachine();
                    return CProgress();
                }

                /* Pause VM first: */
                comConsole.Pause();
                if (!comConsole.isOk())
                {
                    comResult = comConsole;
                    m_comSession.UnlockMachine();
                    return CProgress();
                }
            }

            break;
        }
        default:
            break;
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = comMachine.SaveState();
    /* Store COM result: */
    comResult = comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMachineSaveState::sltHandleProgressFinished()
{
    /* Unlock session finally: */
    if (m_comSession.isNotNull())
        m_comSession.UnlockMachine();

    /* Notifies listeners: */
    emit sigMachineStateSaved(error().isEmpty());
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachinePowerOff implementation.                                                                  *
*********************************************************************************************************************************/

UINotificationProgressMachinePowerOff::UINotificationProgressMachinePowerOff(const CMachine &comMachine,
                                                                             const CConsole &comConsole /* = CConsole() */,
                                                                             bool fIncludingDiscard /* = false */)
    : m_comMachine(comMachine)
    , m_comConsole(comConsole)
    , m_fIncludingDiscard(fIncludingDiscard)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressMachinePowerOff::sltHandleProgressFinished);
}

QString UINotificationProgressMachinePowerOff::name() const
{
    return UINotificationProgress::tr("Powering VM off ...");
}

QString UINotificationProgressMachinePowerOff::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressMachinePowerOff::createProgress(COMResult &comResult)
{
    /* Prepare machine to power off: */
    CMachine comMachine = m_comMachine;
    /* Prepare console to power off: */
    CConsole comConsole = m_comConsole;

    /* For Manager UI: */
    switch (uiCommon().uiType())
    {
        case UIType_ManagerUI:
        {
            /* Acquire VM id: */
            const QUuid uId = comMachine.GetId();
            if (!comMachine.isOk())
            {
                comResult = comMachine;
                return CProgress();
            }

            /* Open a session thru which we will modify the machine: */
            m_comSession = openExistingSession(uId);
            if (m_comSession.isNull())
                return CProgress();

            /* Get session machine: */
            comMachine = m_comSession.GetMachine();
            if (!m_comSession.isOk())
            {
                comResult = m_comSession;
                m_comSession.UnlockMachine();
                return CProgress();
            }

            /* Get session console: */
            comConsole = m_comSession.GetConsole();
            if (!m_comSession.isOk())
            {
                comResult = m_comSession;
                m_comSession.UnlockMachine();
                return CProgress();
            }

            break;
        }
        default:
            break;
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = comConsole.PowerDown();

    /* For Runtime UI: */
    switch (uiCommon().uiType())
    {
        case UIType_RuntimeUI:
        {
            /* Check the console state, it might be already gone: */
            if (!comConsole.isNull())
            {
                /* This can happen if VBoxSVC is not running: */
                COMResult res(comConsole);
                if (FAILED_DEAD_INTERFACE(res.rc()))
                    return CProgress();
            }

            break;
        }
        default:
            break;
    }

    /* Store COM result: */
    comResult = comConsole;

    /* Acquire VM name, no error checks, too late: */
    m_strName = comMachine.GetName();

    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressMachinePowerOff::sltHandleProgressFinished()
{
    /* Unlock session finally: */
    if (m_comSession.isNotNull())
        m_comSession.UnlockMachine();

    /* Notifies listeners: */
    emit sigMachinePoweredOff(error().isEmpty(), m_fIncludingDiscard);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressMachineMediaRemove implementation.                                                               *
*********************************************************************************************************************************/

UINotificationProgressMachineMediaRemove::UINotificationProgressMachineMediaRemove(const CMachine &comMachine,
                                                                                   const CMediumVector &media)
    : m_comMachine(comMachine)
    , m_media(media)
{
}

QString UINotificationProgressMachineMediaRemove::name() const
{
    return UINotificationProgress::tr("Removing machine media ...");
}

QString UINotificationProgressMachineMediaRemove::details() const
{
    return UINotificationProgress::tr("<b>Machine Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressMachineMediaRemove::createProgress(COMResult &comResult)
{
    /* Acquire names: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.DeleteConfig(m_media);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressVFSExplorerUpdate implementation.                                                                *
*********************************************************************************************************************************/

UINotificationProgressVFSExplorerUpdate::UINotificationProgressVFSExplorerUpdate(const CVFSExplorer &comExplorer)
    : m_comExplorer(comExplorer)
{
}

QString UINotificationProgressVFSExplorerUpdate::name() const
{
    return UINotificationProgress::tr("Updating VFS explorer ...");
}

QString UINotificationProgressVFSExplorerUpdate::details() const
{
    return UINotificationProgress::tr("<b>Path:</b> %1").arg(m_strPath);
}

CProgress UINotificationProgressVFSExplorerUpdate::createProgress(COMResult &comResult)
{
    /* Acquire path: */
    m_strPath = m_comExplorer.GetPath();
    if (!m_comExplorer.isOk())
    {
        /* Store COM result: */
        comResult = m_comExplorer;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comExplorer.Update();
    /* Store COM result: */
    comResult = m_comExplorer;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressVFSExplorerFilesRemove implementation.                                                           *
*********************************************************************************************************************************/

UINotificationProgressVFSExplorerFilesRemove::UINotificationProgressVFSExplorerFilesRemove(const CVFSExplorer &comExplorer,
                                                                                           const QVector<QString> &files)
    : m_comExplorer(comExplorer)
    , m_files(files)
{
}

QString UINotificationProgressVFSExplorerFilesRemove::name() const
{
    return UINotificationProgress::tr("Removing VFS explorer files ...");
}

QString UINotificationProgressVFSExplorerFilesRemove::details() const
{
    return UINotificationProgress::tr("<b>Path:</b> %1<br><b>Files:</b> %2")
                                      .arg(m_strPath)
                                      .arg(QStringList(m_files.toList()).join(", "));
}

CProgress UINotificationProgressVFSExplorerFilesRemove::createProgress(COMResult &comResult)
{
    /* Acquire path: */
    m_strPath = m_comExplorer.GetPath();
    if (!m_comExplorer.isOk())
    {
        /* Store COM result: */
        comResult = m_comExplorer;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comExplorer.Remove(m_files);
    /* Store COM result: */
    comResult = m_comExplorer;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressSubnetSelectionVSDFormCreate implementation.                                                     *
*********************************************************************************************************************************/

UINotificationProgressSubnetSelectionVSDFormCreate::UINotificationProgressSubnetSelectionVSDFormCreate(const CCloudClient &comClient,
                                                                                                       const CVirtualSystemDescription &comVSD,
                                                                                                       const QString &strProviderShortName,
                                                                                                       const QString &strProfileName)
    : m_comClient(comClient)
    , m_comVSD(comVSD)
    , m_strProviderShortName(strProviderShortName)
    , m_strProfileName(strProfileName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressSubnetSelectionVSDFormCreate::sltHandleProgressFinished);
}

QString UINotificationProgressSubnetSelectionVSDFormCreate::name() const
{
    return UINotificationProgress::tr("Creating subnet selection VSD form ...");
}

QString UINotificationProgressSubnetSelectionVSDFormCreate::details() const
{
    return UINotificationProgress::tr("<b>Provider:</b> %1<br><b>Profile:</b> %2")
                                      .arg(m_strProviderShortName, m_strProfileName);
}

CProgress UINotificationProgressSubnetSelectionVSDFormCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.GetSubnetSelectionForm(m_comVSD, m_comVSDForm);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressSubnetSelectionVSDFormCreate::sltHandleProgressFinished()
{
    if (m_comVSDForm.isNotNull())
        emit sigVSDFormCreated(m_comVSDForm);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressLaunchVSDFormCreate implementation.                                                              *
*********************************************************************************************************************************/

UINotificationProgressLaunchVSDFormCreate::UINotificationProgressLaunchVSDFormCreate(const CCloudClient &comClient,
                                                                                     const CVirtualSystemDescription &comVSD,
                                                                                     const QString &strProviderShortName,
                                                                                     const QString &strProfileName)
    : m_comClient(comClient)
    , m_comVSD(comVSD)
    , m_strProviderShortName(strProviderShortName)
    , m_strProfileName(strProfileName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressLaunchVSDFormCreate::sltHandleProgressFinished);
}

QString UINotificationProgressLaunchVSDFormCreate::name() const
{
    return UINotificationProgress::tr("Creating launch VSD form ...");
}

QString UINotificationProgressLaunchVSDFormCreate::details() const
{
    return UINotificationProgress::tr("<b>Provider:</b> %1<br><b>Profile:</b> %2")
                                      .arg(m_strProviderShortName, m_strProfileName);
}

CProgress UINotificationProgressLaunchVSDFormCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.GetLaunchDescriptionForm(m_comVSD, m_comVSDForm);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressLaunchVSDFormCreate::sltHandleProgressFinished()
{
    if (m_comVSDForm.isNotNull())
        emit sigVSDFormCreated(m_comVSDForm);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressExportVSDFormCreate implementation.                                                              *
*********************************************************************************************************************************/

UINotificationProgressExportVSDFormCreate::UINotificationProgressExportVSDFormCreate(const CCloudClient &comClient,
                                                                                     const CVirtualSystemDescription &comVSD)
    : m_comClient(comClient)
    , m_comVSD(comVSD)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressExportVSDFormCreate::sltHandleProgressFinished);
}

QString UINotificationProgressExportVSDFormCreate::name() const
{
    return UINotificationProgress::tr("Creating export VSD form ...");
}

QString UINotificationProgressExportVSDFormCreate::details() const
{
    return QString();
}

CProgress UINotificationProgressExportVSDFormCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.GetExportDescriptionForm(m_comVSD, m_comVSDForm);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressExportVSDFormCreate::sltHandleProgressFinished()
{
    if (m_comVSDForm.isNotNull())
        emit sigVSDFormCreated(QVariant::fromValue(m_comVSDForm));
}


/*********************************************************************************************************************************
*   Class UINotificationProgressImportVSDFormCreate implementation.                                                              *
*********************************************************************************************************************************/

UINotificationProgressImportVSDFormCreate::UINotificationProgressImportVSDFormCreate(const CCloudClient &comClient,
                                                                                     const CVirtualSystemDescription &comVSD)
    : m_comClient(comClient)
    , m_comVSD(comVSD)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressImportVSDFormCreate::sltHandleProgressFinished);
}

QString UINotificationProgressImportVSDFormCreate::name() const
{
    return UINotificationProgress::tr("Creating import VSD form ...");
}

QString UINotificationProgressImportVSDFormCreate::details() const
{
    return QString();
}

CProgress UINotificationProgressImportVSDFormCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.GetImportDescriptionForm(m_comVSD, m_comVSDForm);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressImportVSDFormCreate::sltHandleProgressFinished()
{
    if (m_comVSDForm.isNotNull())
        emit sigVSDFormCreated(QVariant::fromValue(m_comVSDForm));
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudImageList implementation.                                                                   *
*********************************************************************************************************************************/

UINotificationProgressCloudImageList::UINotificationProgressCloudImageList(const CCloudClient &comClient,
                                                                           const QVector<KCloudImageState> &cloudImageStates)
    : m_comClient(comClient)
    , m_cloudImageStates(cloudImageStates)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudImageList::sltHandleProgressFinished);
}

QString UINotificationProgressCloudImageList::name() const
{
    return UINotificationProgress::tr("Listing cloud images ...");
}

QString UINotificationProgressCloudImageList::details() const
{
    return QString();
}

CProgress UINotificationProgressCloudImageList::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.ListImages(m_cloudImageStates, m_comNames, m_comIds);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudImageList::sltHandleProgressFinished()
{
    if (m_comNames.isNotNull() && m_comIds.isNotNull())
    {
        emit sigImageNamesReceived(QVariant::fromValue(m_comNames));
        emit sigImageIdsReceived(QVariant::fromValue(m_comIds));
    }
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudSourceBootVolumeList implementation.                                                        *
*********************************************************************************************************************************/

UINotificationProgressCloudSourceBootVolumeList::UINotificationProgressCloudSourceBootVolumeList(const CCloudClient &comClient)
    : m_comClient(comClient)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudSourceBootVolumeList::sltHandleProgressFinished);
}

QString UINotificationProgressCloudSourceBootVolumeList::name() const
{
    return UINotificationProgress::tr("Listing cloud source boot volumes ...");
}

QString UINotificationProgressCloudSourceBootVolumeList::details() const
{
    return QString();
}

CProgress UINotificationProgressCloudSourceBootVolumeList::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.ListSourceBootVolumes(m_comNames, m_comIds);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudSourceBootVolumeList::sltHandleProgressFinished()
{
    if (m_comNames.isNotNull() && m_comIds.isNotNull())
    {
        emit sigImageNamesReceived(QVariant::fromValue(m_comNames));
        emit sigImageIdsReceived(QVariant::fromValue(m_comIds));
    }
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudInstanceList implementation.                                                                *
*********************************************************************************************************************************/

UINotificationProgressCloudInstanceList::UINotificationProgressCloudInstanceList(const CCloudClient &comClient)
    : m_comClient(comClient)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudInstanceList::sltHandleProgressFinished);
}

QString UINotificationProgressCloudInstanceList::name() const
{
    return UINotificationProgress::tr("Listing cloud instances ...");
}

QString UINotificationProgressCloudInstanceList::details() const
{
    return QString();
}

CProgress UINotificationProgressCloudInstanceList::createProgress(COMResult &comResult)
{
    /* Currently we are interested in Running and Stopped VMs only: */
    const QVector<KCloudMachineState> cloudMachineStates  = QVector<KCloudMachineState>()
                                                         << KCloudMachineState_Running
                                                         << KCloudMachineState_Stopped;

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.ListInstances(cloudMachineStates, m_comNames, m_comIds);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudInstanceList::sltHandleProgressFinished()
{
    if (m_comNames.isNotNull() && m_comIds.isNotNull())
    {
        emit sigImageNamesReceived(QVariant::fromValue(m_comNames));
        emit sigImageIdsReceived(QVariant::fromValue(m_comIds));
    }
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudSourceInstanceList implementation.                                                          *
*********************************************************************************************************************************/

UINotificationProgressCloudSourceInstanceList::UINotificationProgressCloudSourceInstanceList(const CCloudClient &comClient)
    : m_comClient(comClient)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudSourceInstanceList::sltHandleProgressFinished);
}

QString UINotificationProgressCloudSourceInstanceList::name() const
{
    return UINotificationProgress::tr("Listing cloud source instances ...");
}

QString UINotificationProgressCloudSourceInstanceList::details() const
{
    return QString();
}

CProgress UINotificationProgressCloudSourceInstanceList::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.ListSourceInstances(m_comNames, m_comIds);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudSourceInstanceList::sltHandleProgressFinished()
{
    if (m_comNames.isNotNull() && m_comIds.isNotNull())
    {
        emit sigImageNamesReceived(QVariant::fromValue(m_comNames));
        emit sigImageIdsReceived(QVariant::fromValue(m_comIds));
    }
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineAdd implementation.                                                                  *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineAdd::UINotificationProgressCloudMachineAdd(const CCloudClient &comClient,
                                                                             const CCloudMachine &comMachine,
                                                                             const QString &strInstanceName,
                                                                             const QString &strProviderShortName,
                                                                             const QString &strProfileName)
    : m_comClient(comClient)
    , m_comMachine(comMachine)
    , m_strInstanceName(strInstanceName)
    , m_strProviderShortName(strProviderShortName)
    , m_strProfileName(strProfileName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudMachineAdd::sltHandleProgressFinished);
}

QString UINotificationProgressCloudMachineAdd::name() const
{
    return UINotificationProgress::tr("Adding cloud VM ...");
}

QString UINotificationProgressCloudMachineAdd::details() const
{
    return UINotificationProgress::tr("<b>Provider:</b> %1<br><b>Profile:</b> %2<br><b>Instance Name:</b> %3")
                                      .arg(m_strProviderShortName, m_strProfileName, m_strInstanceName);
}

CProgress UINotificationProgressCloudMachineAdd::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.AddCloudMachine(m_strInstanceName, m_comMachine);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudMachineAdd::sltHandleProgressFinished()
{
    if (m_comMachine.isNotNull() && !m_comMachine.GetId().isNull())
        emit sigCloudMachineAdded(m_strProviderShortName, m_strProfileName, m_comMachine);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineCreate implementation.                                                               *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineCreate::UINotificationProgressCloudMachineCreate(const CCloudClient &comClient,
                                                                                   const CCloudMachine &comMachine,
                                                                                   const CVirtualSystemDescription &comVSD,
                                                                                   const QString &strProviderShortName,
                                                                                   const QString &strProfileName)
    : m_comClient(comClient)
    , m_comMachine(comMachine)
    , m_comVSD(comVSD)
    , m_strProviderShortName(strProviderShortName)
    , m_strProfileName(strProfileName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudMachineCreate::sltHandleProgressFinished);
}

QString UINotificationProgressCloudMachineCreate::name() const
{
    return UINotificationProgress::tr("Creating cloud VM ...");
}

QString UINotificationProgressCloudMachineCreate::details() const
{
    return UINotificationProgress::tr("<b>Provider:</b> %1<br><b>Profile:</b> %2<br><b>VM Name:</b> %3")
                                      .arg(m_strProviderShortName, m_strProfileName, m_strName);
}

CProgress UINotificationProgressCloudMachineCreate::createProgress(COMResult &comResult)
{
    /* Parse cloud VM name: */
    QVector<KVirtualSystemDescriptionType> types;
    QVector<QString> refs, origValues, configValues, extraConfigValues;
    m_comVSD.GetDescriptionByType(KVirtualSystemDescriptionType_Name, types,
                                  refs, origValues, configValues, extraConfigValues);
    if (!origValues.isEmpty())
        m_strName = origValues.first();

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comClient.CreateCloudMachine(m_comVSD, m_comMachine);
    /* Store COM result: */
    comResult = m_comClient;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudMachineCreate::sltHandleProgressFinished()
{
    if (m_comMachine.isNotNull() && !m_comMachine.GetId().isNull())
        emit sigCloudMachineCreated(m_strProviderShortName, m_strProfileName, m_comMachine);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineRemove implementation.                                                               *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineRemove::UINotificationProgressCloudMachineRemove(const CCloudMachine &comMachine,
                                                                                   bool fFullRemoval,
                                                                                   const QString &strProviderShortName,
                                                                                   const QString &strProfileName)
    : m_comMachine(comMachine)
    , m_fFullRemoval(fFullRemoval)
    , m_strProviderShortName(strProviderShortName)
    , m_strProfileName(strProfileName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudMachineRemove::sltHandleProgressFinished);
}

QString UINotificationProgressCloudMachineRemove::name() const
{
    return   m_fFullRemoval
           ? UINotificationProgress::tr("Deleting cloud VM files ...")
           : UINotificationProgress::tr("Removing cloud VM ...");
}

QString UINotificationProgressCloudMachineRemove::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachineRemove::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_fFullRemoval
                          ? m_comMachine.Remove()
                          : m_comMachine.Unregister();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudMachineRemove::sltHandleProgressFinished()
{
    if (error().isEmpty())
        emit sigCloudMachineRemoved(m_strProviderShortName, m_strProfileName, m_strName);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineClone implementation.                                                                *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineClone::UINotificationProgressCloudMachineClone(const CCloudClient &comClient,
                                                                                 const CCloudMachine &comMachine,
                                                                                 const QString &strCloneName)
    : m_comClient(comClient)
    , m_comMachine(comMachine)
    , m_strCloneName(strCloneName)
{
}

QString UINotificationProgressCloudMachineClone::name() const
{
    return UINotificationProgress::tr("Cloning cloud VM ...");
}

QString UINotificationProgressCloudMachineClone::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachineClone::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM internal id: */
    m_strId = m_comMachine.GetCloudId();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CCloudMachine comCloneMachine;
    CProgress comProgress = m_comClient.CloneInstance(m_strId, m_strCloneName, comCloneMachine);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineReset implementation.                                                                *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineReset::UINotificationProgressCloudMachineReset(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudMachineReset::name() const
{
    return UINotificationProgress::tr("Resetting cloud VM ...");
}

QString UINotificationProgressCloudMachineReset::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachineReset::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.Reset();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachinePowerUp implementation.                                                              *
*********************************************************************************************************************************/

UINotificationProgressCloudMachinePowerUp::UINotificationProgressCloudMachinePowerUp(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudMachinePowerUp::name() const
{
    return   UINotificationProgress::tr("Powering cloud VM up ...");
}

QString UINotificationProgressCloudMachinePowerUp::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachinePowerUp::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.PowerUp();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachinePowerOff implementation.                                                             *
*********************************************************************************************************************************/

UINotificationProgressCloudMachinePowerOff::UINotificationProgressCloudMachinePowerOff(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudMachinePowerOff::name() const
{
    return   UINotificationProgress::tr("Powering cloud VM off ...");
}

QString UINotificationProgressCloudMachinePowerOff::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachinePowerOff::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.PowerDown();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineShutdown implementation.                                                             *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineShutdown::UINotificationProgressCloudMachineShutdown(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudMachineShutdown::name() const
{
    return   UINotificationProgress::tr("Shutting cloud VM down ...");
}

QString UINotificationProgressCloudMachineShutdown::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachineShutdown::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.Shutdown();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineTerminate implementation.                                                            *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineTerminate::UINotificationProgressCloudMachineTerminate(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudMachineTerminate::name() const
{
    return   UINotificationProgress::tr("Terminating cloud VM ...");
}

QString UINotificationProgressCloudMachineTerminate::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudMachineTerminate::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        /* Store COM result: */
        comResult = m_comMachine;
        /* Return progress-wrapper: */
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.Terminate();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineSettingsFormCreate implementation.                                                   *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineSettingsFormCreate::UINotificationProgressCloudMachineSettingsFormCreate(const CCloudMachine &comMachine,
                                                                                                           const QString &strMachineName)
    : m_comMachine(comMachine)
    , m_strMachineName(strMachineName)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudMachineSettingsFormCreate::sltHandleProgressFinished);
}

QString UINotificationProgressCloudMachineSettingsFormCreate::name() const
{
    return UINotificationProgress::tr("Creating cloud VM settings form ...");
}

QString UINotificationProgressCloudMachineSettingsFormCreate::details() const
{
    return UINotificationProgress::tr("<b>Cloud VM Name:</b> %1").arg(m_strMachineName);
}

CProgress UINotificationProgressCloudMachineSettingsFormCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.GetSettingsForm(m_comForm);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudMachineSettingsFormCreate::sltHandleProgressFinished()
{
    if (m_comForm.isNotNull())
        emit sigSettingsFormCreated(QVariant::fromValue(m_comForm));
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudMachineSettingsFormApply implementation.                                                    *
*********************************************************************************************************************************/

UINotificationProgressCloudMachineSettingsFormApply::UINotificationProgressCloudMachineSettingsFormApply(const CForm &comForm,
                                                                                                         const QString &strMachineName)
    : m_comForm(comForm)
    , m_strMachineName(strMachineName)
{
}

QString UINotificationProgressCloudMachineSettingsFormApply::name() const
{
    return UINotificationProgress::tr("Applying cloud VM settings form ...");
}

QString UINotificationProgressCloudMachineSettingsFormApply::details() const
{
    return UINotificationProgress::tr("<b>Cloud VM Name:</b> %1").arg(m_strMachineName);
}

CProgress UINotificationProgressCloudMachineSettingsFormApply::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comForm.Apply();
    /* Store COM result: */
    comResult = m_comForm;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudConsoleConnectionCreate implementation.                                                     *
*********************************************************************************************************************************/

UINotificationProgressCloudConsoleConnectionCreate::UINotificationProgressCloudConsoleConnectionCreate(const CCloudMachine &comMachine,
                                                                                                       const QString &strPublicKey)
    : m_comMachine(comMachine)
    , m_strPublicKey(strPublicKey)
{
}

QString UINotificationProgressCloudConsoleConnectionCreate::name() const
{
    return UINotificationProgress::tr("Creating cloud console connection ...");
}

QString UINotificationProgressCloudConsoleConnectionCreate::details() const
{
    return UINotificationProgress::tr("<b>Cloud VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudConsoleConnectionCreate::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.CreateConsoleConnection(m_strPublicKey);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudConsoleConnectionDelete implementation.                                                     *
*********************************************************************************************************************************/

UINotificationProgressCloudConsoleConnectionDelete::UINotificationProgressCloudConsoleConnectionDelete(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
}

QString UINotificationProgressCloudConsoleConnectionDelete::name() const
{
    return UINotificationProgress::tr("Deleting cloud console connection ...");
}

QString UINotificationProgressCloudConsoleConnectionDelete::details() const
{
    return UINotificationProgress::tr("<b>Cloud VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudConsoleConnectionDelete::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.DeleteConsoleConnection();
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressCloudConsoleLogAcquire implementation.                                                           *
*********************************************************************************************************************************/

UINotificationProgressCloudConsoleLogAcquire::UINotificationProgressCloudConsoleLogAcquire(const CCloudMachine &comMachine)
    : m_comMachine(comMachine)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressCloudConsoleLogAcquire::sltHandleProgressFinished);
}

QString UINotificationProgressCloudConsoleLogAcquire::name() const
{
    return UINotificationProgress::tr("Acquire cloud console log ...");
}

QString UINotificationProgressCloudConsoleLogAcquire::details() const
{
    return UINotificationProgress::tr("<b>Cloud VM Name:</b> %1").arg(m_strName);
}

CProgress UINotificationProgressCloudConsoleLogAcquire::createProgress(COMResult &comResult)
{
    /* Acquire cloud VM name: */
    m_strName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comMachine.GetConsoleHistory(m_comStream);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressCloudConsoleLogAcquire::sltHandleProgressFinished()
{
    /* Read the byte array: */
    QVector<BYTE> byteArray;
    while (true)
    {
        const QVector<BYTE> byteChunk = m_comStream.Read(64 * _1K, 0);
        if (byteChunk.size() == 0)
            break;
        byteArray += byteChunk;
    }
    if (byteArray.size() == 0)
        return;

    /* Convert it to string and send away: */
    const QString strLog = QString::fromUtf8(reinterpret_cast<const char *>(byteArray.data()), byteArray.size());
    emit sigLogRead(m_strName, strLog);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressSnapshotTake implementation.                                                                     *
*********************************************************************************************************************************/

UINotificationProgressSnapshotTake::UINotificationProgressSnapshotTake(const CMachine &comMachine,
                                                                       const QString &strSnapshotName,
                                                                       const QString &strSnapshotDescription)
    : m_comMachine(comMachine)
    , m_strSnapshotName(strSnapshotName)
    , m_strSnapshotDescription(strSnapshotDescription)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressSnapshotTake::sltHandleProgressFinished);
}

QString UINotificationProgressSnapshotTake::name() const
{
    return UINotificationProgress::tr("Taking snapshot ...");
}

QString UINotificationProgressSnapshotTake::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1<br><b>Snapshot Name:</b> %2").arg(m_strMachineName, m_strSnapshotName);
}

CProgress UINotificationProgressSnapshotTake::createProgress(COMResult &comResult)
{
    /* Acquire VM id: */
    const QUuid uId = m_comMachine.GetId();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Acquire VM name: */
    m_strMachineName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Get session machine: */
    CMachine comMachine;

    /* For Manager UI: */
    switch (uiCommon().uiType())
    {
        case UIType_ManagerUI:
        {
            /* Acquire session state: */
            const KSessionState enmSessionState = m_comMachine.GetSessionState();
            if (!m_comMachine.isOk())
            {
                comResult = m_comMachine;
                return CProgress();
            }

            /* Open a session thru which we will modify the machine: */
            if (enmSessionState != KSessionState_Unlocked)
                m_comSession = openExistingSession(uId);
            else
                m_comSession = openSession(uId);
            if (m_comSession.isNull())
                return CProgress();

            /* Get session machine: */
            comMachine = m_comSession.GetMachine();
            if (!m_comSession.isOk())
            {
                comResult = m_comSession;
                m_comSession.UnlockMachine();
                return CProgress();
            }

            break;
        }
        case UIType_RuntimeUI:
        {
            /* Get passed machine: */
            comMachine = m_comMachine;

            break;
        }
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = comMachine.TakeSnapshot(m_strSnapshotName,
                                                    m_strSnapshotDescription,
                                                    true, m_uSnapshotId);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressSnapshotTake::sltHandleProgressFinished()
{
    if (m_comSession.isNotNull())
        m_comSession.UnlockMachine();

    if (!m_uSnapshotId.isNull())
        emit sigSnapshotTaken(QVariant::fromValue(m_uSnapshotId));
}


/*********************************************************************************************************************************
*   Class UINotificationProgressSnapshotRestore implementation.                                                                  *
*********************************************************************************************************************************/

UINotificationProgressSnapshotRestore::UINotificationProgressSnapshotRestore(const QUuid &uMachineId,
                                                                             const CSnapshot &comSnapshot /* = CSnapshot() */)
    : m_uMachineId(uMachineId)
    , m_comSnapshot(comSnapshot)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressSnapshotRestore::sltHandleProgressFinished);
}

UINotificationProgressSnapshotRestore::UINotificationProgressSnapshotRestore(const CMachine &comMachine,
                                                                             const CSnapshot &comSnapshot /* = CSnapshot() */)
    : m_comMachine(comMachine)
    , m_comSnapshot(comSnapshot)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressSnapshotRestore::sltHandleProgressFinished);
}

QString UINotificationProgressSnapshotRestore::name() const
{
    return UINotificationProgress::tr("Restoring snapshot ...");
}

QString UINotificationProgressSnapshotRestore::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1<br><b>Snapshot Name:</b> %2").arg(m_strMachineName, m_strSnapshotName);
}

CProgress UINotificationProgressSnapshotRestore::createProgress(COMResult &comResult)
{
    /* Make sure machine ID defined: */
    if (m_uMachineId.isNull())
    {
        /* Acquire VM id: */
        AssertReturn(m_comMachine.isNotNull(), CProgress());
        m_uMachineId = m_comMachine.GetId();
        if (!m_comMachine.isOk())
        {
            comResult = m_comMachine;
            return CProgress();
        }
    }

    /* Make sure machine defined: */
    if (m_comMachine.isNull())
    {
        /* Acquire VM: */
        AssertReturn(!m_uMachineId.isNull(), CProgress());
        CVirtualBox comVBox = gpGlobalSession->virtualBox();
        m_comMachine = comVBox.FindMachine(m_uMachineId.toString());
        if (!comVBox.isOk())
        {
            comResult = comVBox;
            return CProgress();
        }
    }

    /* Make sure snapshot is defined: */
    if (m_comSnapshot.isNull())
        m_comSnapshot = m_comMachine.GetCurrentSnapshot();

    /* Acquire snapshot name: */
    m_strSnapshotName = m_comSnapshot.GetName();
    if (!m_comSnapshot.isOk())
    {
        comResult = m_comSnapshot;
        return CProgress();
    }

    /* Acquire session state: */
    const KSessionState enmSessionState = m_comMachine.GetSessionState();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Open a session thru which we will modify the machine: */
    if (enmSessionState != KSessionState_Unlocked)
        m_comSession = openExistingSession(m_uMachineId);
    else
        m_comSession = openSession(m_uMachineId);
    if (m_comSession.isNull())
        return CProgress();

    /* Get session machine: */
    CMachine comMachine = m_comSession.GetMachine();
    if (!m_comSession.isOk())
    {
        comResult = m_comSession;
        m_comSession.UnlockMachine();
        return CProgress();
    }

    /* Acquire VM name: */
    m_strMachineName = comMachine.GetName();
    if (!comMachine.isOk())
    {
        comResult = comMachine;
        m_comSession.UnlockMachine();
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = comMachine.RestoreSnapshot(m_comSnapshot);
    /* Store COM result: */
    comResult = comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressSnapshotRestore::sltHandleProgressFinished()
{
    /* Unlock session finally: */
    m_comSession.UnlockMachine();

    /* Notifies listeners: */
    emit sigSnapshotRestored(error().isEmpty());
}


/*********************************************************************************************************************************
*   Class UINotificationProgressSnapshotDelete implementation.                                                                   *
*********************************************************************************************************************************/

UINotificationProgressSnapshotDelete::UINotificationProgressSnapshotDelete(const CMachine &comMachine,
                                                                           const QUuid &uSnapshotId)
    : m_comMachine(comMachine)
    , m_uSnapshotId(uSnapshotId)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressSnapshotDelete::sltHandleProgressFinished);
}

QString UINotificationProgressSnapshotDelete::name() const
{
    return UINotificationProgress::tr("Deleting snapshot ...");
}

QString UINotificationProgressSnapshotDelete::details() const
{
    return UINotificationProgress::tr("<b>VM Name:</b> %1<br><b>Snapshot Name:</b> %2").arg(m_strMachineName, m_strSnapshotName);
}

CProgress UINotificationProgressSnapshotDelete::createProgress(COMResult &comResult)
{
    /* Acquire VM id: */
    const QUuid uId = m_comMachine.GetId();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Acquire VM name: */
    m_strMachineName = m_comMachine.GetName();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Acquire snapshot: */
    CSnapshot comSnapshot = m_comMachine.FindSnapshot(m_uSnapshotId.toString());
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Acquire snapshot name: */
    m_strSnapshotName = comSnapshot.GetName();
    if (!comSnapshot.isOk())
    {
        comResult = comSnapshot;
        return CProgress();
    }

    /* Acquire session state: */
    const KSessionState enmSessionState = m_comMachine.GetSessionState();
    if (!m_comMachine.isOk())
    {
        comResult = m_comMachine;
        return CProgress();
    }

    /* Open a session thru which we will modify the machine: */
    if (enmSessionState != KSessionState_Unlocked)
        m_comSession = openExistingSession(uId);
    else
        m_comSession = openSession(uId);
    if (m_comSession.isNull())
        return CProgress();

    /* Get session machine: */
    CMachine comMachine = m_comSession.GetMachine();
    if (!m_comSession.isOk())
    {
        comResult = m_comSession;
        m_comSession.UnlockMachine();
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = comMachine.DeleteSnapshot(m_uSnapshotId);
    /* Store COM result: */
    comResult = m_comMachine;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressSnapshotDelete::sltHandleProgressFinished()
{
    m_comSession.UnlockMachine();
}


/*********************************************************************************************************************************
*   Class UINotificationProgressApplianceWrite implementation.                                                                   *
*********************************************************************************************************************************/

UINotificationProgressApplianceWrite::UINotificationProgressApplianceWrite(const CAppliance &comAppliance,
                                                                           const QString &strFormat,
                                                                           const QVector<KExportOptions> &options,
                                                                           const QString &strPath)
    : m_comAppliance(comAppliance)
    , m_strFormat(strFormat)
    , m_options(options)
    , m_strPath(strPath)
{
}

QString UINotificationProgressApplianceWrite::name() const
{
    return UINotificationProgress::tr("Writing appliance ...");
}

QString UINotificationProgressApplianceWrite::details() const
{
    return UINotificationProgress::tr("<b>To:</b> %1").arg(m_strPath);
}

CProgress UINotificationProgressApplianceWrite::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comAppliance.Write(m_strFormat, m_options, m_strPath);
    /* Store COM result: */
    comResult = m_comAppliance;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressApplianceRead implementation.                                                                    *
*********************************************************************************************************************************/

UINotificationProgressApplianceRead::UINotificationProgressApplianceRead(const CAppliance &comAppliance,
                                                                         const QString &strPath)
    : m_comAppliance(comAppliance)
    , m_strPath(strPath)
{
}

QString UINotificationProgressApplianceRead::name() const
{
    return UINotificationProgress::tr("Reading appliance ...");
}

QString UINotificationProgressApplianceRead::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1").arg(m_strPath);
}

CProgress UINotificationProgressApplianceRead::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comAppliance.Read(m_strPath);
    /* Store COM result: */
    comResult = m_comAppliance;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressApplianceImport implementation.                                                                  *
*********************************************************************************************************************************/

UINotificationProgressApplianceImport::UINotificationProgressApplianceImport(const CAppliance &comAppliance,
                                                                             const QVector<KImportOptions> &options)
    : m_comAppliance(comAppliance)
    , m_options(options)
{
}

QString UINotificationProgressApplianceImport::name() const
{
    return UINotificationProgress::tr("Importing appliance ...");
}

QString UINotificationProgressApplianceImport::details() const
{
    return UINotificationProgress::tr("<b>From:</b> %1").arg(m_comAppliance.GetPath());
}

CProgress UINotificationProgressApplianceImport::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comAppliance.ImportMachines(m_options);
    /* Store COM result: */
    comResult = m_comAppliance;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressExtensionPackInstall implementation.                                                             *
*********************************************************************************************************************************/

UINotificationProgressExtensionPackInstall::UINotificationProgressExtensionPackInstall(const CExtPackFile &comExtPackFile,
                                                                                       bool fReplace,
                                                                                       const QString &strExtensionPackName,
                                                                                       const QString &strDisplayInfo)
    : m_comExtPackFile(comExtPackFile)
    , m_fReplace(fReplace)
    , m_strExtensionPackName(strExtensionPackName)
    , m_strDisplayInfo(strDisplayInfo)
{
}

QString UINotificationProgressExtensionPackInstall::name() const
{
    return UINotificationProgress::tr("Installing package ...");
}

QString UINotificationProgressExtensionPackInstall::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strExtensionPackName);
}

CProgress UINotificationProgressExtensionPackInstall::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comExtPackFile.Install(m_fReplace, m_strDisplayInfo);
    /* Store COM result: */
    comResult = m_comExtPackFile;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressExtensionPackUninstall implementation.                                                           *
*********************************************************************************************************************************/

UINotificationProgressExtensionPackUninstall::UINotificationProgressExtensionPackUninstall(const CExtPackManager &comExtPackManager,
                                                                                           const QString &strExtensionPackName,
                                                                                           const QString &strDisplayInfo)
    : m_comExtPackManager(comExtPackManager)
    , m_strExtensionPackName(strExtensionPackName)
    , m_strDisplayInfo(strDisplayInfo)
{
}

QString UINotificationProgressExtensionPackUninstall::name() const
{
    return UINotificationProgress::tr("Uninstalling package ...");
}

QString UINotificationProgressExtensionPackUninstall::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strExtensionPackName);
}

CProgress UINotificationProgressExtensionPackUninstall::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comExtPackManager.Uninstall(m_strExtensionPackName,
                                                          false /* forced removal? */,
                                                          m_strDisplayInfo);
    /* Store COM result: */
    comResult = m_comExtPackManager;
    /* Return progress-wrapper: */
    return comProgress;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressGuestAdditionsInstall implementation.                                                            *
*********************************************************************************************************************************/

UINotificationProgressGuestAdditionsInstall::UINotificationProgressGuestAdditionsInstall(const CGuest &comGuest,
                                                                                         const QString &strSource)
    : m_comGuest(comGuest)
    , m_strSource(strSource)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressGuestAdditionsInstall::sltHandleProgressFinished);
}

QString UINotificationProgressGuestAdditionsInstall::name() const
{
    return UINotificationProgress::tr("Installing image ...");
}

QString UINotificationProgressGuestAdditionsInstall::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strSource);
}

CProgress UINotificationProgressGuestAdditionsInstall::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    QVector<QString> args;
    QVector<KAdditionsUpdateFlag> flags;
    CProgress comProgress = m_comGuest.UpdateGuestAdditions(m_strSource, args, flags);
    /* Store COM result: */
    comResult = m_comGuest;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressGuestAdditionsInstall::sltHandleProgressFinished()
{
    if (!error().isEmpty())
        emit sigGuestAdditionsInstallationFailed(m_strSource);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressHostOnlyNetworkInterfaceCreate implementation.                                                   *
*********************************************************************************************************************************/

UINotificationProgressHostOnlyNetworkInterfaceCreate::UINotificationProgressHostOnlyNetworkInterfaceCreate(const CHost &comHost,
                                                                                                           const CHostNetworkInterface &comInterface)
    : m_comHost(comHost)
    , m_comInterface(comInterface)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressHostOnlyNetworkInterfaceCreate::sltHandleProgressFinished);
}

QString UINotificationProgressHostOnlyNetworkInterfaceCreate::name() const
{
    return UINotificationProgress::tr("Creating host-only network interface ...");
}

QString UINotificationProgressHostOnlyNetworkInterfaceCreate::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg("TBD");
}

CProgress UINotificationProgressHostOnlyNetworkInterfaceCreate::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comHost.CreateHostOnlyNetworkInterface(m_comInterface);
    /* Store COM result: */
    comResult = m_comHost;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressHostOnlyNetworkInterfaceCreate::sltHandleProgressFinished()
{
    if (error().isEmpty())
        emit sigHostOnlyNetworkInterfaceCreated(m_comInterface);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressHostOnlyNetworkInterfaceRemove implementation.                                                   *
*********************************************************************************************************************************/

UINotificationProgressHostOnlyNetworkInterfaceRemove::UINotificationProgressHostOnlyNetworkInterfaceRemove(const CHost &comHost,
                                                                                                           const QUuid &uInterfaceId)
    : m_comHost(comHost)
    , m_uInterfaceId(uInterfaceId)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressHostOnlyNetworkInterfaceRemove::sltHandleProgressFinished);
}

QString UINotificationProgressHostOnlyNetworkInterfaceRemove::name() const
{
    return UINotificationProgress::tr("Removing host-only network interface ...");
}

QString UINotificationProgressHostOnlyNetworkInterfaceRemove::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strInterfaceName);
}

CProgress UINotificationProgressHostOnlyNetworkInterfaceRemove::createProgress(COMResult &comResult)
{
    /* Acquire interface: */
    CHostNetworkInterface comInterface = m_comHost.FindHostNetworkInterfaceById(m_uInterfaceId);
    if (!m_comHost.isOk())
    {
        comResult = m_comHost;
        return CProgress();
    }

    /* Acquire interface name: */
    m_strInterfaceName = comInterface.GetName();
    if (!comInterface.isOk())
    {
        comResult = comInterface;
        return CProgress();
    }

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_comHost.RemoveHostOnlyNetworkInterface(m_uInterfaceId);
    /* Store COM result: */
    comResult = m_comHost;
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressHostOnlyNetworkInterfaceRemove::sltHandleProgressFinished()
{
    if (error().isEmpty())
        emit sigHostOnlyNetworkInterfaceRemoved(m_strInterfaceName);
}


/*********************************************************************************************************************************
*   Class UINotificationProgressVsdFormValueSet implementation.                                                                  *
*********************************************************************************************************************************/

UINotificationProgressVsdFormValueSet::UINotificationProgressVsdFormValueSet(const CBooleanFormValue &comValue,
                                                                             bool fBool)
    : m_enmType(KFormValueType_Boolean)
    , m_comValue(comValue)
    , m_fBool(fBool)
    , m_iChoice(0)
    , m_iInteger(0)
    , m_iInteger64(0)
{
}

UINotificationProgressVsdFormValueSet::UINotificationProgressVsdFormValueSet(const CStringFormValue &comValue,
                                                                             const QString &strString)
    : m_enmType(KFormValueType_String)
    , m_comValue(comValue)
    , m_fBool(false)
    , m_strString(strString)
    , m_iChoice(0)
    , m_iInteger(0)
    , m_iInteger64(0)
{
}

UINotificationProgressVsdFormValueSet::UINotificationProgressVsdFormValueSet(const CChoiceFormValue &comValue,
                                                                             int iChoice)
    : m_enmType(KFormValueType_Choice)
    , m_comValue(comValue)
    , m_fBool(false)
    , m_iChoice(iChoice)
    , m_iInteger(0)
    , m_iInteger64(0)
{
}

UINotificationProgressVsdFormValueSet::UINotificationProgressVsdFormValueSet(const CRangedIntegerFormValue &comValue,
                                                                             int iInteger)
    : m_enmType(KFormValueType_RangedInteger)
    , m_comValue(comValue)
    , m_fBool(false)
    , m_iChoice(0)
    , m_iInteger(iInteger)
    , m_iInteger64(0)
{
}

UINotificationProgressVsdFormValueSet::UINotificationProgressVsdFormValueSet(const CRangedInteger64FormValue &comValue,
                                                                             qlonglong iInteger64)
    : m_enmType(KFormValueType_RangedInteger64)
    , m_comValue(comValue)
    , m_fBool(false)
    , m_iChoice(0)
    , m_iInteger(0)
    , m_iInteger64(iInteger64)
{
}

QString UINotificationProgressVsdFormValueSet::name() const
{
    return UINotificationProgress::tr("Set VSD form value ...");
}

QString UINotificationProgressVsdFormValueSet::details() const
{
    /* Handle known types: */
    switch (m_enmType)
    {
        case KFormValueType_Boolean: return UINotificationProgress::tr("<b>Value:</b> %1").arg(m_fBool);
        case KFormValueType_String: return UINotificationProgress::tr("<b>Value:</b> %1").arg(m_strString);
        case KFormValueType_Choice: return UINotificationProgress::tr("<b>Value:</b> %1").arg(m_iChoice);
        case KFormValueType_RangedInteger: return UINotificationProgress::tr("<b>Value:</b> %1").arg(m_iInteger);
        case KFormValueType_RangedInteger64: return UINotificationProgress::tr("<b>Value:</b> %1").arg(m_iInteger64);
        default: break;
    }
    /* Null-string by default: */
    return QString();
}

CProgress UINotificationProgressVsdFormValueSet::createProgress(COMResult &comResult)
{
    /* Initialize progress-wrapper: */
    CProgress comProgress;

    /* Handle known types: */
    switch (m_enmType)
    {
        case KFormValueType_Boolean:
        {
            /* Set value: */
            CBooleanFormValue comValue(m_comValue);
            comProgress = comValue.SetSelected(m_fBool);
            /* Store COM result: */
            comResult = comValue;
            break;
        }
        case KFormValueType_String:
        {
            /* Set value: */
            CStringFormValue comValue(m_comValue);
            comProgress = comValue.SetString(m_strString);
            /* Store COM result: */
            comResult = comValue;
            break;
        }
        case KFormValueType_Choice:
        {
            /* Set value: */
            CChoiceFormValue comValue(m_comValue);
            comProgress = comValue.SetSelectedIndex(m_iChoice);
            /* Store COM result: */
            comResult = comValue;
            break;
        }
        case KFormValueType_RangedInteger:
        {
            /* Set value: */
            CRangedIntegerFormValue comValue(m_comValue);
            comProgress = comValue.SetInteger(m_iInteger);
            /* Store COM result: */
            comResult = comValue;
            break;
        }
        case KFormValueType_RangedInteger64:
        {
            /* Set value: */
            CRangedInteger64FormValue comValue(m_comValue);
            comProgress = comValue.SetInteger(m_iInteger64);
            /* Store COM result: */
            comResult = comValue;
            break;
        }
        default:
            break;
    }

    /* Return progress-wrapper: */
    return comProgress;
}


#ifdef VBOX_GUI_WITH_NETWORK_MANAGER


/*********************************************************************************************************************************
*   Class UINotificationDownloaderExtensionPack implementation.                                                                  *
*********************************************************************************************************************************/

/* static */
UINotificationDownloaderExtensionPack *UINotificationDownloaderExtensionPack::s_pInstance = 0;

/* static */
UINotificationDownloaderExtensionPack *UINotificationDownloaderExtensionPack::instance(const QString &strPackName)
{
    if (!s_pInstance)
        new UINotificationDownloaderExtensionPack(strPackName);
    return s_pInstance;
}

/* static */
bool UINotificationDownloaderExtensionPack::exists()
{
    return !!s_pInstance;
}

UINotificationDownloaderExtensionPack::UINotificationDownloaderExtensionPack(const QString &strPackName)
    : m_strPackName(strPackName)
{
    s_pInstance = this;
}

UINotificationDownloaderExtensionPack::~UINotificationDownloaderExtensionPack()
{
    s_pInstance = 0;
}

QString UINotificationDownloaderExtensionPack::name() const
{
    return UINotificationDownloader::tr("Downloading Extension Pack ...");
}

QString UINotificationDownloaderExtensionPack::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strPackName);
}

UIDownloader *UINotificationDownloaderExtensionPack::createDownloader()
{
    /* Create and configure the Extension Pack downloader: */
    UIDownloaderExtensionPack *pDownloader = new UIDownloaderExtensionPack;
    if (pDownloader)
    {
        connect(pDownloader, &UIDownloaderExtensionPack::sigDownloadFinished,
                this, &UINotificationDownloaderExtensionPack::sigExtensionPackDownloaded);
        return pDownloader;
    }
    return 0;
}


/*********************************************************************************************************************************
*   Class UINotificationDownloaderGuestAdditions implementation.                                                                 *
*********************************************************************************************************************************/

/* static */
UINotificationDownloaderGuestAdditions *UINotificationDownloaderGuestAdditions::s_pInstance = 0;

/* static */
UINotificationDownloaderGuestAdditions *UINotificationDownloaderGuestAdditions::instance(const QString &strFileName)
{
    if (!s_pInstance)
        new UINotificationDownloaderGuestAdditions(strFileName);
    return s_pInstance;
}

/* static */
bool UINotificationDownloaderGuestAdditions::exists()
{
    return !!s_pInstance;
}

UINotificationDownloaderGuestAdditions::UINotificationDownloaderGuestAdditions(const QString &strFileName)
    : m_strFileName(strFileName)
{
    s_pInstance = this;
}

UINotificationDownloaderGuestAdditions::~UINotificationDownloaderGuestAdditions()
{
    s_pInstance = 0;
}

QString UINotificationDownloaderGuestAdditions::name() const
{
    return UINotificationDownloader::tr("Downloading Guest Additions ...");
}

QString UINotificationDownloaderGuestAdditions::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strFileName);
}

UIDownloader *UINotificationDownloaderGuestAdditions::createDownloader()
{
    /* Create and configure the User Manual downloader: */
    UIDownloaderGuestAdditions *pDownloader = new UIDownloaderGuestAdditions;
    if (pDownloader)
    {
        connect(pDownloader, &UIDownloaderGuestAdditions::sigDownloadFinished,
                this, &UINotificationDownloaderGuestAdditions::sigGuestAdditionsDownloaded);
        return pDownloader;
    }
    return 0;
}


/*********************************************************************************************************************************
*   Class UINotificationDownloaderUserManual implementation.                                                                     *
*********************************************************************************************************************************/

/* static */
UINotificationDownloaderUserManual *UINotificationDownloaderUserManual::s_pInstance = 0;

/* static */
UINotificationDownloaderUserManual *UINotificationDownloaderUserManual::instance(const QString &strFileName)
{
    if (!s_pInstance)
        new UINotificationDownloaderUserManual(strFileName);
    return s_pInstance;
}

/* static */
bool UINotificationDownloaderUserManual::exists()
{
    return !!s_pInstance;
}

UINotificationDownloaderUserManual::UINotificationDownloaderUserManual(const QString &strFileName)
    : m_strFileName(strFileName)
{
    s_pInstance = this;
}

UINotificationDownloaderUserManual::~UINotificationDownloaderUserManual()
{
    s_pInstance = 0;
}

QString UINotificationDownloaderUserManual::name() const
{
    return UINotificationDownloader::tr("Downloading User Guide ...");
}

QString UINotificationDownloaderUserManual::details() const
{
    return UINotificationProgress::tr("<b>Name:</b> %1").arg(m_strFileName);
}

UIDownloader *UINotificationDownloaderUserManual::createDownloader()
{
    /* Create and configure the User Manual downloader: */
    UIDownloaderUserManual *pDownloader = new UIDownloaderUserManual;
    if (pDownloader)
    {
        connect(pDownloader, &UIDownloaderUserManual::sigDownloadFinished,
                this, &UINotificationDownloaderUserManual::sigUserManualDownloaded);
        return pDownloader;
    }
    return 0;
}


/*********************************************************************************************************************************
*   Class UINotificationProgressNewVersionChecker implementation.                                                                *
*********************************************************************************************************************************/

UINotificationProgressNewVersionChecker::UINotificationProgressNewVersionChecker(bool fForcedCall)
    : m_fForcedCall(fForcedCall)
{
    connect(this, &UINotificationProgress::sigProgressFinished,
            this, &UINotificationProgressNewVersionChecker::sltHandleProgressFinished);

#ifdef VBOX_WITH_UPDATE_AGENT
    CHost comHost = gpGlobalSession->host();
    if (!comHost.isNull())
       m_comUpdateHost = comHost.GetUpdateHost();
#endif /* VBOX_WITH_UPDATE_AGENT */
}

QString UINotificationProgressNewVersionChecker::name() const
{
#ifdef VBOX_WITH_UPDATE_AGENT
    if (m_comUpdateHost.isOk())
        return UINotificationProgress::tr("Checking for new version of %1 ...").arg(m_comUpdateHost.GetName().toLocal8Bit().data());
#endif /* VBOX_WITH_UPDATE_AGENT */
    return UINotificationProgress::tr("Checking for new version ...");
}

QString UINotificationProgressNewVersionChecker::details() const
{
    return QString();
}

CProgress UINotificationProgressNewVersionChecker::createProgress(COMResult &comResult)
{
#ifdef VBOX_WITH_UPDATE_AGENT
    if (!m_comUpdateHost.isOk())
        return CProgress();

    CProgress comProgress = m_comUpdateHost.CheckFor();
    comResult = m_comUpdateHost;

    return comProgress;
#else
    return CProgress();
#endif /* VBOX_WITH_UPDATE_AGENT */
}

void UINotificationProgressNewVersionChecker::sltHandleProgressFinished()
{
#ifdef VBOX_WITH_UPDATE_AGENT
    if (m_comUpdateHost.isNull() && !m_comUpdateHost.isOk())
        return;

    KUpdateState enmState = m_comUpdateHost.GetState();
    if (!m_comUpdateHost.isOk())
        return;

    switch (enmState)
    {
        case KUpdateState_Available:
        {
            QString strVersion = m_comUpdateHost.GetVersion();
            if (!m_comUpdateHost.isOk())
                return;
            QString strURL = m_comUpdateHost.GetDownloadUrl();
            if (!m_comUpdateHost.isOk())
                return;
            UINotificationMessage::showUpdateSuccess(strVersion, strURL);
            break;
        }
        case KUpdateState_NotAvailable:
        {
            if (m_fForcedCall)
                UINotificationMessage::showUpdateNotFound();
            break;
        }
        case KUpdateState_Invalid:
        case KUpdateState_Error:
        case KUpdateState_Max:
            /* Error cases are handled not here: */
            break;
        case KUpdateState_Downloading:
        case KUpdateState_Downloaded:
        case KUpdateState_Installing:
        case KUpdateState_Installed:
        case KUpdateState_UserInteraction:
        case KUpdateState_Canceled:
        case KUpdateState_Maintenance:
            /* These cases are not yet implemented in Main: */
            break;
        default:
            break;
    }

#endif /* VBOX_WITH_UPDATE_AGENT */
}

#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
