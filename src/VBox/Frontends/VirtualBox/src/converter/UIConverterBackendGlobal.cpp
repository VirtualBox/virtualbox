/* $Id: UIConverterBackendGlobal.cpp 168940 2025-05-20 14:00:56Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIConverterBackendGlobal implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <QHash>
#include <QRegularExpression>

/* GUI includes: */
#include "UIConverter.h"
#include "UIDefs.h"
#include "UIExtraDataDefs.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UIMediumDefs.h"
#include "UISettingsDefs.h"
#include "UITranslator.h"
#include "UIVRDESettingsEditor.h"
#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
# include "UIDesktopWidgetWatchdog.h"
#endif

/* COM includes: */
#include "CPlatformProperties.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/* QString <= Qt::Alignment: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const Qt::Alignment &enmAlignment) const
{
    QString strResult;
    switch (enmAlignment)
    {
        case Qt::AlignTop:    strResult = "Top"; break;
        case Qt::AlignBottom: strResult = "Bottom"; break;
        default:
        {
            AssertMsgFailed(("No text for alignment=%d", enmAlignment));
            break;
        }
    }
    return strResult;
}

/* Qt::Alignment <= QString: */
template<> SHARED_LIBRARY_STUFF Qt::Alignment UIConverter::fromInternalString<Qt::Alignment>(const QString &strAlignment) const
{
    if (strAlignment.compare("Top", Qt::CaseInsensitive) == 0)
        return Qt::AlignTop;
    if (strAlignment.compare("Bottom", Qt::CaseInsensitive) == 0)
        return Qt::AlignBottom;
    return Qt::AlignTop;
}

/* QString <= Qt::SortOrder: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const Qt::SortOrder &enmSortOrder) const
{
    QString strResult;
    switch (enmSortOrder)
    {
        case Qt::AscendingOrder:  strResult = "Ascending"; break;
        case Qt::DescendingOrder: strResult = "Descending"; break;
        default:
        {
            AssertMsgFailed(("No text for sort order=%d", enmSortOrder));
            break;
        }
    }
    return strResult;
}

/* Qt::SortOrder <= QString: */
template<> SHARED_LIBRARY_STUFF Qt::SortOrder UIConverter::fromInternalString<Qt::SortOrder>(const QString &strSortOrder) const
{
    if (strSortOrder.compare("Ascending", Qt::CaseInsensitive) == 0)
        return Qt::AscendingOrder;
    if (strSortOrder.compare("Descending", Qt::CaseInsensitive) == 0)
        return Qt::DescendingOrder;
    return Qt::AscendingOrder;
}

/* QString <= SizeSuffix: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const SizeSuffix &sizeSuffix) const
{
    QString strResult;
    switch (sizeSuffix)
    {
        case SizeSuffix_Byte:     strResult = QApplication::translate("UITranslator", "B", "size suffix Bytes"); break;
        case SizeSuffix_KiloByte: strResult = QApplication::translate("UITranslator", "KB", "size suffix KBytes=1024 Bytes"); break;
        case SizeSuffix_MegaByte: strResult = QApplication::translate("UITranslator", "MB", "size suffix MBytes=1024 KBytes"); break;
        case SizeSuffix_GigaByte: strResult = QApplication::translate("UITranslator", "GB", "size suffix GBytes=1024 MBytes"); break;
        case SizeSuffix_TeraByte: strResult = QApplication::translate("UITranslator", "TB", "size suffix TBytes=1024 GBytes"); break;
        case SizeSuffix_PetaByte: strResult = QApplication::translate("UITranslator", "PB", "size suffix PBytes=1024 TBytes"); break;
        default:
        {
            AssertMsgFailed(("No text for size suffix=%d", sizeSuffix));
            break;
        }
    }
    return strResult;
}

/* SizeSuffix <= QString: */
template<> SHARED_LIBRARY_STUFF SizeSuffix UIConverter::fromString<SizeSuffix>(const QString &strSizeSuffix) const
{
    QHash<QString, SizeSuffix> list;
    list.insert(QApplication::translate("UITranslator", "B", "size suffix Bytes"),               SizeSuffix_Byte);
    list.insert(QApplication::translate("UITranslator", "KB", "size suffix KBytes=1024 Bytes"),  SizeSuffix_KiloByte);
    list.insert(QApplication::translate("UITranslator", "MB", "size suffix MBytes=1024 KBytes"), SizeSuffix_MegaByte);
    list.insert(QApplication::translate("UITranslator", "GB", "size suffix GBytes=1024 MBytes"), SizeSuffix_GigaByte);
    list.insert(QApplication::translate("UITranslator", "TB", "size suffix TBytes=1024 GBytes"), SizeSuffix_TeraByte);
    list.insert(QApplication::translate("UITranslator", "PB", "size suffix PBytes=1024 TBytes"), SizeSuffix_PetaByte);
    if (!list.contains(strSizeSuffix))
    {
        AssertMsgFailed(("No value for '%s'", strSizeSuffix.toUtf8().constData()));
    }
    return list.value(strSizeSuffix);
}

/* QString <= StorageSlot: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const StorageSlot &storageSlot) const
{
    QString strResult;
    switch (storageSlot.bus)
    {
        case KStorageBus_IDE:
        {
            int iMaxPort = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxPortCountForStorageBus(storageSlot.bus);
            int iMaxDevice = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxDevicesPerPortForStorageBus(storageSlot.bus);
            if (storageSlot.port < 0 || storageSlot.port > iMaxPort)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d", storageSlot.bus, storageSlot.port));
                break;
            }
            if (storageSlot.device < 0 || storageSlot.device > iMaxDevice)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d & device=%d", storageSlot.bus, storageSlot.port, storageSlot.device));
                break;
            }
            if (storageSlot.port == 0 && storageSlot.device == 0)
                strResult = QApplication::translate("UICommon", "IDE Primary Device 0", "StorageSlot");
            else if (storageSlot.port == 0 && storageSlot.device == 1)
                strResult = QApplication::translate("UICommon", "IDE Primary Device 1", "StorageSlot");
            else if (storageSlot.port == 1 && storageSlot.device == 0)
                strResult = QApplication::translate("UICommon", "IDE Secondary Device 0", "StorageSlot");
            else if (storageSlot.port == 1 && storageSlot.device == 1)
                strResult = QApplication::translate("UICommon", "IDE Secondary Device 1", "StorageSlot");
            break;
        }
        case KStorageBus_SATA:
        {
            int iMaxPort = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxPortCountForStorageBus(storageSlot.bus);
            if (storageSlot.port < 0 || storageSlot.port > iMaxPort)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d", storageSlot.bus, storageSlot.port));
                break;
            }
            if (storageSlot.device != 0)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d & device=%d", storageSlot.bus, storageSlot.port, storageSlot.device));
                break;
            }
            strResult = QApplication::translate("UICommon", "SATA Port %1", "StorageSlot").arg(storageSlot.port);
            break;
        }
        case KStorageBus_SCSI:
        {
            int iMaxPort = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxPortCountForStorageBus(storageSlot.bus);
            if (storageSlot.port < 0 || storageSlot.port > iMaxPort)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d", storageSlot.bus, storageSlot.port));
                break;
            }
            if (storageSlot.device != 0)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d & device=%d", storageSlot.bus, storageSlot.port, storageSlot.device));
                break;
            }
            strResult = QApplication::translate("UICommon", "SCSI Port %1", "StorageSlot").arg(storageSlot.port);
            break;
        }
        case KStorageBus_SAS:
        {
            int iMaxPort = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxPortCountForStorageBus(storageSlot.bus);
            if (storageSlot.port < 0 || storageSlot.port > iMaxPort)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d", storageSlot.bus, storageSlot.port));
                break;
            }
            if (storageSlot.device != 0)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d & device=%d", storageSlot.bus, storageSlot.port, storageSlot.device));
                break;
            }
            strResult = QApplication::translate("UICommon", "SAS Port %1", "StorageSlot").arg(storageSlot.port);
            break;
        }
        case KStorageBus_Floppy:
        {
            int iMaxDevice = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxDevicesPerPortForStorageBus(storageSlot.bus);
            if (storageSlot.port != 0)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d", storageSlot.bus, storageSlot.port));
                break;
            }
            if (storageSlot.device < 0 || storageSlot.device > iMaxDevice)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d & device=%d", storageSlot.bus, storageSlot.port, storageSlot.device));
                break;
            }
            strResult = QApplication::translate("UICommon", "Floppy Device %1", "StorageSlot").arg(storageSlot.device);
            break;
        }
        case KStorageBus_USB:
        {
            int iMaxPort = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxPortCountForStorageBus(storageSlot.bus);
            if (storageSlot.port < 0 || storageSlot.port > iMaxPort)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d", storageSlot.bus, storageSlot.port));
                break;
            }
            if (storageSlot.device != 0)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d & device=%d", storageSlot.bus, storageSlot.port, storageSlot.device));
                break;
            }
            strResult = QApplication::translate("UICommon", "USB Port %1", "StorageSlot").arg(storageSlot.port);
            break;
        }
        case KStorageBus_PCIe:
        {
            int iMaxPort = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxPortCountForStorageBus(storageSlot.bus);
            if (storageSlot.port < 0 || storageSlot.port > iMaxPort)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d", storageSlot.bus, storageSlot.port));
                break;
            }
            if (storageSlot.device != 0)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d & device=%d", storageSlot.bus, storageSlot.port, storageSlot.device));
                break;
            }
            strResult = QApplication::translate("UICommon", "NVMe Port %1", "StorageSlot").arg(storageSlot.port);
            break;
        }
        case KStorageBus_VirtioSCSI:
        {
            int iMaxPort = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxPortCountForStorageBus(storageSlot.bus);
            if (storageSlot.port < 0 || storageSlot.port > iMaxPort)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d", storageSlot.bus, storageSlot.port));
                break;
            }
            if (storageSlot.device != 0)
            {
                AssertMsgFailed(("No text for bus=%d & port=%d & device=%d", storageSlot.bus, storageSlot.port, storageSlot.device));
                break;
            }
            strResult = QApplication::translate("UICommon", "virtio-scsi Port %1", "StorageSlot").arg(storageSlot.port);
            break;
        }
        default:
        {
            AssertMsgFailed(("No text for bus=%d & port=%d & device=%d", storageSlot.bus, storageSlot.port, storageSlot.device));
            break;
        }
    }
    return strResult;
}

/* StorageSlot <= QString: */
template<> SHARED_LIBRARY_STUFF StorageSlot UIConverter::fromString<StorageSlot>(const QString &strStorageSlot) const
{
    /* Prepare a hash of known port templates: */
    QHash<int, QString> templates;
    templates[0] = QApplication::translate("UICommon", "IDE Primary Device 0", "StorageSlot");
    templates[1] = QApplication::translate("UICommon", "IDE Primary Device 1", "StorageSlot");
    templates[2] = QApplication::translate("UICommon", "IDE Secondary Device 0", "StorageSlot");
    templates[3] = QApplication::translate("UICommon", "IDE Secondary Device 1", "StorageSlot");
    templates[4] = QApplication::translate("UICommon", "SATA Port %1", "StorageSlot");
    templates[5] = QApplication::translate("UICommon", "SCSI Port %1", "StorageSlot");
    templates[6] = QApplication::translate("UICommon", "SAS Port %1", "StorageSlot");
    templates[7] = QApplication::translate("UICommon", "Floppy Device %1", "StorageSlot");
    templates[8] = QApplication::translate("UICommon", "USB Port %1", "StorageSlot");
    templates[9] = QApplication::translate("UICommon", "NVMe Port %1", "StorageSlot");
    templates[10] = QApplication::translate("UICommon", "virtio-scsi Port %1", "StorageSlot");

    /* Search for a template index strStorageSlot corresponds to: */
    int iIndex = -1;
    QRegularExpression re;
    QRegularExpressionMatch mt;
    for (int i = 0; i < templates.size(); ++i)
    {
        re.setPattern(i >= 0 && i <= 3 ? templates.value(i) : templates.value(i).arg("(\\d+)"));
        mt = re.match(strStorageSlot);
        if (mt.hasMatch())
        {
            iIndex = i;
            break;
        }
    }

    /* Compose result: */
    StorageSlot result;

    /* First we determine bus type: */
    switch (iIndex)
    {
        case 0:
        case 1:
        case 2:
        case 3:  result.bus = KStorageBus_IDE; break;
        case 4:  result.bus = KStorageBus_SATA; break;
        case 5:  result.bus = KStorageBus_SCSI; break;
        case 6:  result.bus = KStorageBus_SAS; break;
        case 7:  result.bus = KStorageBus_Floppy; break;
        case 8:  result.bus = KStorageBus_USB; break;
        case 9:  result.bus = KStorageBus_PCIe; break;
        case 10: result.bus = KStorageBus_VirtioSCSI; break;
        default: AssertMsgFailed(("No storage bus for text='%s'", strStorageSlot.toUtf8().constData())); break;
    }

    /* Second we determine port/device pair: */
    switch (iIndex)
    {
        case 0:
        case 1:
        case 2:
        case 3:
        {
            if (result.bus == KStorageBus_Null)
                break;
            const int iMaxPort = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxPortCountForStorageBus(result.bus);
            const int iMaxDevice = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxDevicesPerPortForStorageBus(result.bus);
            const LONG iPort = iIndex / iMaxPort;
            const LONG iDevice = iIndex % iMaxPort;
            if (iPort < 0 || iPort > iMaxPort)
            {
                AssertMsgFailed(("No storage port for text='%s'", strStorageSlot.toUtf8().constData()));
                break;
            }
            if (iDevice < 0 || iDevice > iMaxDevice)
            {
                AssertMsgFailed(("No storage device for text='%s'", strStorageSlot.toUtf8().constData()));
                break;
            }
            result.port = iPort;
            result.device = iDevice;
            break;
        }
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        {
            if (result.bus == KStorageBus_Null)
                break;
            const int iMaxPort = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetMaxPortCountForStorageBus(result.bus);
            const LONG iPort = mt.captured(1).toInt();
            const LONG iDevice = 0;
            if (iPort < 0 || iPort > iMaxPort)
            {
                AssertMsgFailed(("No storage port for text='%s'", strStorageSlot.toUtf8().constData()));
                break;
            }
            result.port = iPort;
            result.device = iDevice;
            break;
        }
        default:
        {
            AssertMsgFailed(("No storage slot for text='%s'", strStorageSlot.toUtf8().constData()));
            break;
        }
    }

    /* Return result: */
    return result;
}

#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
/* DesktopWatchdogPolicy_SynthTest <= QString: */
template<> SHARED_LIBRARY_STUFF DesktopWatchdogPolicy_SynthTest UIConverter::fromInternalString<DesktopWatchdogPolicy_SynthTest>(const QString &strPolicyType) const
{
    if (strPolicyType.compare("Disabled", Qt::CaseInsensitive) == 0)
        return DesktopWatchdogPolicy_SynthTest_Disabled;
    if (strPolicyType.compare("ManagerOnly", Qt::CaseInsensitive) == 0)
        return DesktopWatchdogPolicy_SynthTest_ManagerOnly;
    if (strPolicyType.compare("MachineOnly", Qt::CaseInsensitive) == 0)
        return DesktopWatchdogPolicy_SynthTest_MachineOnly;
    if (strPolicyType.compare("Both", Qt::CaseInsensitive) == 0)
        return DesktopWatchdogPolicy_SynthTest_Both;
    return DesktopWatchdogPolicy_SynthTest_Both;
}
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */

/* QString <= UIExtraDataMetaDefs::DialogType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DialogType &enmDialogType) const
{
    QString strResult;
    switch (enmDialogType)
    {
        case UIExtraDataMetaDefs::DialogType_VISOCreator: strResult = "VISOCreator"; break;
        case UIExtraDataMetaDefs::DialogType_BootFailure: strResult = "BootFailure"; break;
        case UIExtraDataMetaDefs::DialogType_All:         strResult = "All"; break;
        default:
        {
            AssertMsgFailed(("No text for dialog type=%d", enmDialogType));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DialogType <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DialogType UIConverter::fromInternalString<UIExtraDataMetaDefs::DialogType>(const QString &strDialogType) const
{
    if (strDialogType.compare("VISOCreator", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DialogType_VISOCreator;
    if (strDialogType.compare("BootFailure", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DialogType_BootFailure;
    if (strDialogType.compare("All", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DialogType_All;
    return UIExtraDataMetaDefs::DialogType_Invalid;
}

/* QString <= UIExtraDataMetaDefs::MenuType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::MenuType &menuType) const
{
    QString strResult;
    switch (menuType)
    {
        case UIExtraDataMetaDefs::MenuType_Application: strResult = "Application"; break;
        case UIExtraDataMetaDefs::MenuType_Machine:     strResult = "Machine"; break;
        case UIExtraDataMetaDefs::MenuType_View:        strResult = "View"; break;
        case UIExtraDataMetaDefs::MenuType_Input:       strResult = "Input"; break;
        case UIExtraDataMetaDefs::MenuType_Devices:     strResult = "Devices"; break;
#ifdef VBOX_WITH_DEBUGGER_GUI
        case UIExtraDataMetaDefs::MenuType_Debug:       strResult = "Debug"; break;
#endif /* VBOX_WITH_DEBUGGER_GUI */
#ifdef RT_OS_DARWIN
        case UIExtraDataMetaDefs::MenuType_Window:      strResult = "Window"; break;
#endif /* RT_OS_DARWIN */
        case UIExtraDataMetaDefs::MenuType_Help:        strResult = "Help"; break;
        case UIExtraDataMetaDefs::MenuType_All:         strResult = "All"; break;
        default:
        {
            AssertMsgFailed(("No text for indicator type=%d", menuType));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::MenuType <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::MenuType UIConverter::fromInternalString<UIExtraDataMetaDefs::MenuType>(const QString &strMenuType) const
{
    if (strMenuType.compare("Application", Qt::CaseInsensitive) == 0) return UIExtraDataMetaDefs::MenuType_Application;
    if (strMenuType.compare("Machine", Qt::CaseInsensitive) == 0)     return UIExtraDataMetaDefs::MenuType_Machine;
    if (strMenuType.compare("View", Qt::CaseInsensitive) == 0)        return UIExtraDataMetaDefs::MenuType_View;
    if (strMenuType.compare("Input", Qt::CaseInsensitive) == 0)       return UIExtraDataMetaDefs::MenuType_Input;
    if (strMenuType.compare("Devices", Qt::CaseInsensitive) == 0)     return UIExtraDataMetaDefs::MenuType_Devices;
#ifdef VBOX_WITH_DEBUGGER_GUI
    if (strMenuType.compare("Debug", Qt::CaseInsensitive) == 0)       return UIExtraDataMetaDefs::MenuType_Debug;
#endif
#ifdef RT_OS_DARWIN
    if (strMenuType.compare("Window", Qt::CaseInsensitive) == 0)      return UIExtraDataMetaDefs::MenuType_Window;
#endif
    if (strMenuType.compare("Help", Qt::CaseInsensitive) == 0)        return UIExtraDataMetaDefs::MenuType_Help;
    if (strMenuType.compare("All", Qt::CaseInsensitive) == 0)         return UIExtraDataMetaDefs::MenuType_All;
    return UIExtraDataMetaDefs::MenuType_Invalid;
}

/* QString <= UIExtraDataMetaDefs::MenuApplicationActionType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::MenuApplicationActionType &menuApplicationActionType) const
{
    QString strResult;
    switch (menuApplicationActionType)
    {
#ifdef VBOX_WS_MAC
        case UIExtraDataMetaDefs::MenuApplicationActionType_About:                strResult = "About"; break;
#endif /* VBOX_WS_MAC */
        case UIExtraDataMetaDefs::MenuApplicationActionType_Preferences:          strResult = "Preferences"; break;
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
        case UIExtraDataMetaDefs::MenuApplicationActionType_NetworkAccessManager: strResult = "NetworkAccessManager"; break;
        case UIExtraDataMetaDefs::MenuApplicationActionType_CheckForUpdates:      strResult = "CheckForUpdates"; break;
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
        case UIExtraDataMetaDefs::MenuApplicationActionType_ResetWarnings:        strResult = "ResetWarnings"; break;
        case UIExtraDataMetaDefs::MenuApplicationActionType_Close:                strResult = "Close"; break;
        case UIExtraDataMetaDefs::MenuApplicationActionType_All:                  strResult = "All"; break;
        default:
        {
            AssertMsgFailed(("No text for action type=%d", menuApplicationActionType));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::MenuApplicationActionType <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::MenuApplicationActionType
UIConverter::fromInternalString<UIExtraDataMetaDefs::MenuApplicationActionType>(const QString &strMenuApplicationActionType) const
{
#ifdef VBOX_WS_MAC
    if (strMenuApplicationActionType.compare("About", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuApplicationActionType_About;
#endif
    if (strMenuApplicationActionType.compare("Preferences", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuApplicationActionType_Preferences;
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    if (strMenuApplicationActionType.compare("NetworkAccessManager", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuApplicationActionType_NetworkAccessManager;
    if (strMenuApplicationActionType.compare("CheckForUpdates", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuApplicationActionType_CheckForUpdates;
#endif
    if (strMenuApplicationActionType.compare("ResetWarnings", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuApplicationActionType_ResetWarnings;
    if (strMenuApplicationActionType.compare("Close", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuApplicationActionType_Close;
    if (strMenuApplicationActionType.compare("All", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuApplicationActionType_All;
    return UIExtraDataMetaDefs::MenuApplicationActionType_Invalid;
}

/* QString <= UIExtraDataMetaDefs::MenuHelpActionType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::MenuHelpActionType &menuHelpActionType) const
{
    QString strResult;
    switch (menuHelpActionType)
    {
        case UIExtraDataMetaDefs::MenuHelpActionType_Contents:             strResult = "Contents"; break;
        case UIExtraDataMetaDefs::MenuHelpActionType_WebSite:              strResult = "WebSite"; break;
        case UIExtraDataMetaDefs::MenuHelpActionType_BugTracker:           strResult = "BugTracker"; break;
        case UIExtraDataMetaDefs::MenuHelpActionType_Forums:               strResult = "Forums"; break;
        case UIExtraDataMetaDefs::MenuHelpActionType_Oracle:               strResult = "Oracle"; break;
        case UIExtraDataMetaDefs::MenuHelpActionType_OnlineDocumentation:  strResult = "OnlineDocumentation"; break;
#ifndef VBOX_WS_MAC
        case UIExtraDataMetaDefs::MenuHelpActionType_About:                strResult = "About"; break;
#endif /* !VBOX_WS_MAC */
        case UIExtraDataMetaDefs::MenuHelpActionType_All:                  strResult = "All"; break;
        default:
        {
            AssertMsgFailed(("No text for action type=%d", menuHelpActionType));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::MenuHelpActionType <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::MenuHelpActionType
UIConverter::fromInternalString<UIExtraDataMetaDefs::MenuHelpActionType>(const QString &strMenuHelpActionType) const
{
    if (strMenuHelpActionType.compare("Contents", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuHelpActionType_Contents;
    if (strMenuHelpActionType.compare("WebSite", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuHelpActionType_WebSite;
    if (strMenuHelpActionType.compare("BugTracker", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuHelpActionType_BugTracker;
    if (strMenuHelpActionType.compare("Forums", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuHelpActionType_Forums;
    if (strMenuHelpActionType.compare("Oracle", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuHelpActionType_Oracle;
    if (strMenuHelpActionType.compare("OnlineDocumentation", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuHelpActionType_OnlineDocumentation;
#ifndef VBOX_WS_MAC
    if (strMenuHelpActionType.compare("About", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuHelpActionType_About;
#endif
    if (strMenuHelpActionType.compare("All", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuHelpActionType_All;
    return UIExtraDataMetaDefs::MenuHelpActionType_Invalid;
}

/* QString <= UIExtraDataMetaDefs::RuntimeMenuMachineActionType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::RuntimeMenuMachineActionType &runtimeMenuMachineActionType) const
{
    QString strResult;
    switch (runtimeMenuMachineActionType)
    {
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SettingsDialog:                strResult = "SettingsDialog"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_TakeSnapshot:                  strResult = "TakeSnapshot"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_InformationDialog:             strResult = "InformationDialog"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_FileManagerDialog:             strResult = "FileManagerDialog"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_GuestProcessControlDialog:     strResult = "GuestProcessControlDialog"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Pause:                         strResult = "Pause"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Reset:                         strResult = "Reset"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Detach:                        strResult = "Detach"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SaveState:                     strResult = "SaveState"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Shutdown:                      strResult = "Shutdown"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_PowerOff:                      strResult = "PowerOff"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_LogDialog:                     strResult = "LogDialog"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Nothing:                       strResult = "Nothing"; break;
        case UIExtraDataMetaDefs::RuntimeMenuMachineActionType_All:                           strResult = "All"; break;
        default:
        {
            AssertMsgFailed(("No text for action type=%d", runtimeMenuMachineActionType));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::RuntimeMenuMachineActionType <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::RuntimeMenuMachineActionType
UIConverter::fromInternalString<UIExtraDataMetaDefs::RuntimeMenuMachineActionType>(const QString &strRuntimeMenuMachineActionType) const
{
    if (strRuntimeMenuMachineActionType.compare("SettingsDialog", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SettingsDialog;
    if (strRuntimeMenuMachineActionType.compare("TakeSnapshot", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_TakeSnapshot;
    if (strRuntimeMenuMachineActionType.compare("InformationDialog", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_InformationDialog;
    if (strRuntimeMenuMachineActionType.compare("FileManagerDialog", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_FileManagerDialog;
    if (strRuntimeMenuMachineActionType.compare("GuestProcessControlDialog", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_GuestProcessControlDialog;
    if (strRuntimeMenuMachineActionType.compare("Pause", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Pause;
    if (strRuntimeMenuMachineActionType.compare("Reset", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Reset;
    if (strRuntimeMenuMachineActionType.compare("Detach", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Detach;
    if (strRuntimeMenuMachineActionType.compare("SaveState", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SaveState;
    if (strRuntimeMenuMachineActionType.compare("Shutdown", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Shutdown;
    if (strRuntimeMenuMachineActionType.compare("PowerOff", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_PowerOff;
    if (strRuntimeMenuMachineActionType.compare("LogDialog", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_LogDialog;
    if (strRuntimeMenuMachineActionType.compare("Nothing", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Nothing;
    if (strRuntimeMenuMachineActionType.compare("All", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_All;
    return UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Invalid;
}

/* QString <= UIExtraDataMetaDefs::RuntimeMenuViewActionType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::RuntimeMenuViewActionType &runtimeMenuViewActionType) const
{
    QString strResult;
    switch (runtimeMenuViewActionType)
    {
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_Fullscreen:           strResult = "Fullscreen"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_Seamless:             strResult = "Seamless"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_Scale:                strResult = "Scale"; break;
#ifndef VBOX_WS_MAC
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_MinimizeWindow:       strResult = "MinimizeWindow"; break;
#endif /* !VBOX_WS_MAC */
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_AdjustWindow:         strResult = "AdjustWindow"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_GuestAutoresize:      strResult = "GuestAutoresize"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_TakeScreenshot:       strResult = "TakeScreenshot"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_Recording:            strResult = "Recording"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_RecordingSettings:    strResult = "RecordingSettings"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_StartRecording:       strResult = "StartRecording"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_VRDEServer:           strResult = "VRDEServer"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_MenuBar:              strResult = "MenuBar"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_MenuBarSettings:      strResult = "MenuBarSettings"; break;
#ifndef VBOX_WS_MAC
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_ToggleMenuBar:        strResult = "ToggleMenuBar"; break;
#endif /* !VBOX_WS_MAC */
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_StatusBar:            strResult = "StatusBar"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_StatusBarSettings:    strResult = "StatusBarSettings"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_ToggleStatusBar:      strResult = "ToggleStatusBar"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_Resize:               strResult = "Resize"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_Remap:                strResult = "Remap"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_Rescale:              strResult = "Rescale"; break;
        case UIExtraDataMetaDefs::RuntimeMenuViewActionType_All:                  strResult = "All"; break;
        default:
        {
            AssertMsgFailed(("No text for action type=%d", runtimeMenuViewActionType));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::RuntimeMenuViewActionType <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::RuntimeMenuViewActionType
UIConverter::fromInternalString<UIExtraDataMetaDefs::RuntimeMenuViewActionType>(const QString &strRuntimeMenuViewActionType) const
{
    if (strRuntimeMenuViewActionType.compare("Fullscreen", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Fullscreen;
    if (strRuntimeMenuViewActionType.compare("Seamless", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Seamless;
    if (strRuntimeMenuViewActionType.compare("Scale", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Scale;
#ifndef VBOX_WS_MAC
    if (strRuntimeMenuViewActionType.compare("MinimizeWindow", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_MinimizeWindow;
#endif /* !VBOX_WS_MAC */
    if (strRuntimeMenuViewActionType.compare("AdjustWindow", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_AdjustWindow;
    if (strRuntimeMenuViewActionType.compare("GuestAutoresize", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_GuestAutoresize;
    if (strRuntimeMenuViewActionType.compare("TakeScreenshot", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_TakeScreenshot;
    if (strRuntimeMenuViewActionType.compare("Recording", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Recording;
    if (strRuntimeMenuViewActionType.compare("RecordingSettings", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_RecordingSettings;
    if (strRuntimeMenuViewActionType.compare("StartRecording", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_StartRecording;
    if (strRuntimeMenuViewActionType.compare("VRDEServer", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_VRDEServer;
    if (strRuntimeMenuViewActionType.compare("MenuBar", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_MenuBar;
    if (strRuntimeMenuViewActionType.compare("MenuBarSettings", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_MenuBarSettings;
#ifndef VBOX_WS_MAC
    if (strRuntimeMenuViewActionType.compare("ToggleMenuBar", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_ToggleMenuBar;
#endif /* !VBOX_WS_MAC */
    if (strRuntimeMenuViewActionType.compare("StatusBar", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_StatusBar;
    if (strRuntimeMenuViewActionType.compare("StatusBarSettings", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_StatusBarSettings;
    if (strRuntimeMenuViewActionType.compare("ToggleStatusBar", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_ToggleStatusBar;
    if (strRuntimeMenuViewActionType.compare("Resize", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Resize;
    if (strRuntimeMenuViewActionType.compare("Remap", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Remap;
    if (strRuntimeMenuViewActionType.compare("Rescale", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Rescale;
    if (strRuntimeMenuViewActionType.compare("All", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuViewActionType_All;
    return UIExtraDataMetaDefs::RuntimeMenuViewActionType_Invalid;
}

/* QString <= UIExtraDataMetaDefs::RuntimeMenuInputActionType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::RuntimeMenuInputActionType &runtimeMenuInputActionType) const
{
    QString strResult;
    switch (runtimeMenuInputActionType)
    {
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_Keyboard:           strResult = "Keyboard"; break;
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_KeyboardSettings:   strResult = "KeyboardSettings"; break;
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_SoftKeyboard:       strResult = "SoftKeyboard"; break;
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCAD:            strResult = "TypeCAD"; break;
#ifdef VBOX_WS_NIX
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCABS:           strResult = "TypeCABS"; break;
#endif /* VBOX_WS_NIX */
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCtrlBreak:      strResult = "TypeCtrlBreak"; break;
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeInsert:         strResult = "TypeInsert"; break;
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypePrintScreen:    strResult = "TypePrintScreen"; break;
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeAltPrintScreen: strResult = "TypeAltPrintScreen"; break;
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_Mouse:              strResult = "Mouse"; break;
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_MouseIntegration:   strResult = "MouseIntegration"; break;
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeHostKeyCombo:   strResult = "TypeHostKeyCombo"; break;
        case UIExtraDataMetaDefs::RuntimeMenuInputActionType_All:                strResult = "All"; break;
        default:
        {
            AssertMsgFailed(("No text for action type=%d", runtimeMenuInputActionType));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::RuntimeMenuInputActionType <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::RuntimeMenuInputActionType
UIConverter::fromInternalString<UIExtraDataMetaDefs::RuntimeMenuInputActionType>(const QString &strRuntimeMenuInputActionType) const
{
    if (strRuntimeMenuInputActionType.compare("Keyboard", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_Keyboard;
    if (strRuntimeMenuInputActionType.compare("KeyboardSettings", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_KeyboardSettings;
    if (strRuntimeMenuInputActionType.compare("SoftKeyboard", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_SoftKeyboard;
    if (strRuntimeMenuInputActionType.compare("TypeCAD", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCAD;
#ifdef VBOX_WS_NIX
    if (strRuntimeMenuInputActionType.compare("TypeCABS", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCABS;
#endif /* VBOX_WS_NIX */
    if (strRuntimeMenuInputActionType.compare("TypeCtrlBreak", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeCtrlBreak;
    if (strRuntimeMenuInputActionType.compare("TypeInsert", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeInsert;
    if (strRuntimeMenuInputActionType.compare("TypePrintScreen", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypePrintScreen;
    if (strRuntimeMenuInputActionType.compare("TypeAltPrintScreen", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeAltPrintScreen;
    if (strRuntimeMenuInputActionType.compare("Mouse", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_Mouse;
    if (strRuntimeMenuInputActionType.compare("MouseIntegration", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_MouseIntegration;
    if (strRuntimeMenuInputActionType.compare("TypeHostKeyCombo", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_TypeHostKeyCombo;
    if (strRuntimeMenuInputActionType.compare("All", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuInputActionType_All;
    return UIExtraDataMetaDefs::RuntimeMenuInputActionType_Invalid;
}

/* QString <= UIExtraDataMetaDefs::RuntimeMenuDevicesActionType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::RuntimeMenuDevicesActionType &runtimeMenuDevicesActionType) const
{
    QString strResult;
    switch (runtimeMenuDevicesActionType)
    {
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrives:                strResult = "HardDrives"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrivesSettings:        strResult = "HardDrivesSettings"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_OpticalDevices:            strResult = "OpticalDevices"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_FloppyDevices:             strResult = "FloppyDevices"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Audio:                     strResult = "Audio"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_AudioOutput:               strResult = "AudioOutput"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_AudioInput:                strResult = "AudioInput"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Network:                   strResult = "Network"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_NetworkSettings:           strResult = "NetworkSettings"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevices:                strResult = "USBDevices"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevicesSettings:        strResult = "USBDevicesSettings"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_WebCams:                   strResult = "WebCams"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedClipboard:           strResult = "SharedClipboard"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_DragAndDrop:               strResult = "DragAndDrop"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedFolders:             strResult = "SharedFolders"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedFoldersSettings:     strResult = "SharedFoldersSettings"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_InsertGuestAdditionsDisk:  strResult = "InsertGuestAdditionsDisk"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_UpgradeGuestAdditions:     strResult = "UpgradeGuestAdditions"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Nothing:                   strResult = "Nothing"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_All:                       strResult = "All"; break;
        default:
        {
            AssertMsgFailed(("No text for action type=%d", runtimeMenuDevicesActionType));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::RuntimeMenuDevicesActionType <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::RuntimeMenuDevicesActionType
UIConverter::fromInternalString<UIExtraDataMetaDefs::RuntimeMenuDevicesActionType>(const QString &strRuntimeMenuDevicesActionType) const
{
    if (strRuntimeMenuDevicesActionType.compare("HardDrives", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrives;
    if (strRuntimeMenuDevicesActionType.compare("HardDrivesSettings", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrivesSettings;
    if (strRuntimeMenuDevicesActionType.compare("OpticalDevices", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_OpticalDevices;
    if (strRuntimeMenuDevicesActionType.compare("FloppyDevices", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_FloppyDevices;
    if (strRuntimeMenuDevicesActionType.compare("Audio", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Audio;
    if (strRuntimeMenuDevicesActionType.compare("AudioOutput", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_AudioOutput;
    if (strRuntimeMenuDevicesActionType.compare("AudioInput", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_AudioInput;
    if (strRuntimeMenuDevicesActionType.compare("Network", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Network;
    if (strRuntimeMenuDevicesActionType.compare("NetworkSettings", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_NetworkSettings;
    if (strRuntimeMenuDevicesActionType.compare("USBDevices", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevices;
    if (strRuntimeMenuDevicesActionType.compare("USBDevicesSettings", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevicesSettings;
    if (strRuntimeMenuDevicesActionType.compare("WebCams", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_WebCams;
    if (strRuntimeMenuDevicesActionType.compare("SharedClipboard", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedClipboard;
    if (strRuntimeMenuDevicesActionType.compare("DragAndDrop", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_DragAndDrop;
    if (strRuntimeMenuDevicesActionType.compare("SharedFolders", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedFolders;
    if (strRuntimeMenuDevicesActionType.compare("SharedFoldersSettings", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_SharedFoldersSettings;
    if (strRuntimeMenuDevicesActionType.compare("InsertGuestAdditionsDisk", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_InsertGuestAdditionsDisk;
    if (strRuntimeMenuDevicesActionType.compare("UpgradeGuestAdditions", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_UpgradeGuestAdditions;
    if (strRuntimeMenuDevicesActionType.compare("Nothing", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Nothing;
    if (strRuntimeMenuDevicesActionType.compare("All", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_All;
    return UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Invalid;
}

#ifdef VBOX_WITH_DEBUGGER_GUI
/* QString <= UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType &runtimeMenuDebuggerActionType) const
{
    QString strResult;
    switch (runtimeMenuDebuggerActionType)
    {
        case UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Statistics:            strResult = "Statistics"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_CommandLine:           strResult = "CommandLine"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Logging:               strResult = "Logging"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_GuestControlConsole:   strResult = "GuestControlConsole"; break;
        case UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_All:                   strResult = "All"; break;
        default:
        {
            AssertMsgFailed(("No text for action type=%d", runtimeMenuDebuggerActionType));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType
UIConverter::fromInternalString<UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType>(const QString &strRuntimeMenuDebuggerActionType) const
{
    if (strRuntimeMenuDebuggerActionType.compare("Statistics", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Statistics;
    if (strRuntimeMenuDebuggerActionType.compare("CommandLine", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_CommandLine;
    if (strRuntimeMenuDebuggerActionType.compare("Logging", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Logging;
    if (strRuntimeMenuDebuggerActionType.compare("GuestControlConsole", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_GuestControlConsole;
    if (strRuntimeMenuDebuggerActionType.compare("All", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_All;
    return UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Invalid;
}
#endif /* VBOX_WITH_DEBUGGER_GUI */

#ifdef VBOX_WS_MAC
/* QString <= UIExtraDataMetaDefs::MenuWindowActionType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::MenuWindowActionType &menuWindowActionType) const
{
    QString strResult;
    switch (menuWindowActionType)
    {
        case UIExtraDataMetaDefs::MenuWindowActionType_Minimize: strResult = "Minimize"; break;
        case UIExtraDataMetaDefs::MenuWindowActionType_Switch:   strResult = "Switch"; break;
        case UIExtraDataMetaDefs::MenuWindowActionType_All:      strResult = "All"; break;
        default:
        {
            AssertMsgFailed(("No text for action type=%d", menuWindowActionType));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::MenuWindowActionType <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::MenuWindowActionType
UIConverter::fromInternalString<UIExtraDataMetaDefs::MenuWindowActionType>(const QString &strMenuWindowActionType) const
{
    if (strMenuWindowActionType.compare("Minimize", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuWindowActionType_Minimize;
    if (strMenuWindowActionType.compare("Switch", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuWindowActionType_Switch;
    if (strMenuWindowActionType.compare("All", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::MenuWindowActionType_All;
    return UIExtraDataMetaDefs::MenuWindowActionType_Invalid;
}
#endif /* VBOX_WS_MAC */

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral &enmDetailsElementOptionTypeGeneral) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeGeneral)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Name:
            strResult = QApplication::translate("UIDetails", "Name", "details (general)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_OS:
            strResult = QApplication::translate("UIDetails", "Operating System", "details (general)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Location:
            strResult = QApplication::translate("UIDetails", "Settings File Location", "details (general)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Groups:
            strResult = QApplication::translate("UIDetails", "Groups", "details (general)"); break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeGeneral));
            break;
        }
    }
    return strResult;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral &enmDetailsElementOptionTypeGeneral) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeGeneral)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Name:     strResult = "Name"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_OS:       strResult = "OS"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Location: strResult = "Location"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Groups:   strResult = "Groups"; break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeGeneral));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral
UIConverter::fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral>(const QString &strDetailsElementOptionTypeGeneral) const
{
    if (strDetailsElementOptionTypeGeneral.compare("Name", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Name;
    if (strDetailsElementOptionTypeGeneral.compare("OS", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_OS;
    if (strDetailsElementOptionTypeGeneral.compare("Location", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Location;
    if (strDetailsElementOptionTypeGeneral.compare("Groups", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Groups;
    return UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Invalid;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeSystem: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem &enmDetailsElementOptionTypeSystem) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeSystem)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_RAM:
            strResult = QApplication::translate("UIDetails", "Base Memory", "details (system)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_CPUCount:
            strResult = QApplication::translate("UIDetails", "Processors", "details (system)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_CPUExecutionCap:
            strResult = QApplication::translate("UIDetails", "Execution Cap", "details (system)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_BootOrder:
            strResult = QApplication::translate("UIDetails", "Boot Order", "details (system)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_ChipsetType:
            strResult = QApplication::translate("UIDetails", "Chipset Type", "details (system)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_TpmType:
            strResult = QApplication::translate("UIDetails", "TPM Type", "details (system)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Firmware:
            strResult = QApplication::translate("UIDetails", "EFI", "details (system)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_SecureBoot:
            strResult = QApplication::translate("UIDetails", "Secure Boot", "details (system)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Acceleration:
            strResult = QApplication::translate("UIDetails", "Acceleration", "details (system)"); break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeSystem));
            break;
        }
    }
    return strResult;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeSystem: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem &enmDetailsElementOptionTypeSystem) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeSystem)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_RAM:             strResult = "RAM"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_CPUCount:        strResult = "CPUCount"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_CPUExecutionCap: strResult = "CPUExecutionCap"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_BootOrder:       strResult = "BootOrder"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_ChipsetType:     strResult = "ChipsetType"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_TpmType:         strResult = "TPMType"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Firmware:        strResult = "Firmware"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_SecureBoot:      strResult = "SecureBoot"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Acceleration:    strResult = "Acceleration"; break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeSystem));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DetailsElementOptionTypeSystem <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DetailsElementOptionTypeSystem
UIConverter::fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeSystem>(const QString &strDetailsElementOptionTypeSystem) const
{
    if (strDetailsElementOptionTypeSystem.compare("RAM", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_RAM;
    if (strDetailsElementOptionTypeSystem.compare("CPUCount", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_CPUCount;
    if (strDetailsElementOptionTypeSystem.compare("CPUExecutionCap", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_CPUExecutionCap;
    if (strDetailsElementOptionTypeSystem.compare("BootOrder", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_BootOrder;
    if (strDetailsElementOptionTypeSystem.compare("ChipsetType", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_ChipsetType;
    if (strDetailsElementOptionTypeSystem.compare("TPMType", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_TpmType;
    if (strDetailsElementOptionTypeSystem.compare("Firmware", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Firmware;
    if (strDetailsElementOptionTypeSystem.compare("SecureBoot", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_SecureBoot;
    if (strDetailsElementOptionTypeSystem.compare("Acceleration", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Acceleration;
    return UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Invalid;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay &enmDetailsElementOptionTypeDisplay) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeDisplay)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_VRAM:
            strResult = QApplication::translate("UIDetails", "Video Memory", "details (display)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_ScreenCount:
            strResult = QApplication::translate("UIDetails", "Screens", "details (display)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_ScaleFactor:
            strResult = QApplication::translate("UIDetails", "Scale-factor", "details (display)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_GraphicsController:
            strResult = QApplication::translate("UIDetails", "Graphics Controller", "details (display)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Acceleration:
            strResult = QApplication::translate("UIDetails", "Acceleration", "details (display)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_VRDE:
            strResult = QApplication::translate("UIDetails", "Remote Desktop Server", "details (display/vrde)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Recording:
            strResult = QApplication::translate("UIDetails", "Recording", "details (display/recording)"); break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeDisplay));
            break;
        }
    }
    return strResult;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay &enmDetailsElementOptionTypeDisplay) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeDisplay)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_VRAM:               strResult = "VRAM"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_ScreenCount:        strResult = "ScreenCount"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_ScaleFactor:        strResult = "ScaleFactor"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_GraphicsController: strResult = "GraphicsController"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Acceleration:       strResult = "Acceleration"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_VRDE:               strResult = "VRDE"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Recording:          strResult = "Recording"; break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeDisplay));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay
UIConverter::fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay>(const QString &strDetailsElementOptionTypeDisplay) const
{
    if (strDetailsElementOptionTypeDisplay.compare("VRAM", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_VRAM;
    if (strDetailsElementOptionTypeDisplay.compare("ScreenCount", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_ScreenCount;
    if (strDetailsElementOptionTypeDisplay.compare("ScaleFactor", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_ScaleFactor;
    if (strDetailsElementOptionTypeDisplay.compare("GraphicsController", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_GraphicsController;
    if (strDetailsElementOptionTypeDisplay.compare("Acceleration", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Acceleration;
    if (strDetailsElementOptionTypeDisplay.compare("VRDE", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_VRDE;
    if (strDetailsElementOptionTypeDisplay.compare("Recording", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Recording;
    return UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Invalid;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeStorage: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage &enmDetailsElementOptionTypeStorage) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeStorage)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_HardDisks:
            strResult = QApplication::translate("UIDetails", "Hard Disks", "details (storage)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_OpticalDevices:
            strResult = QApplication::translate("UIDetails", "Optical Devices", "details (storage)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_FloppyDevices:
            strResult = QApplication::translate("UIDetails", "Floppy Devices", "details (storage)"); break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeStorage));
            break;
        }
    }
    return strResult;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeStorage: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage &enmDetailsElementOptionTypeStorage) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeStorage)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_HardDisks:      strResult = "HardDisks"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_OpticalDevices: strResult = "OpticalDevices"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_FloppyDevices:  strResult = "FloppyDevices"; break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeStorage));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DetailsElementOptionTypeStorage <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DetailsElementOptionTypeStorage
UIConverter::fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeStorage>(const QString &strDetailsElementOptionTypeStorage) const
{
    if (strDetailsElementOptionTypeStorage.compare("HardDisks", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_HardDisks;
    if (strDetailsElementOptionTypeStorage.compare("OpticalDevices", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_OpticalDevices;
    if (strDetailsElementOptionTypeStorage.compare("FloppyDevices", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_FloppyDevices;
    return UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_Invalid;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeAudio: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio &enmDetailsElementOptionTypeAudio) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeAudio)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Driver:
            strResult = QApplication::translate("UIDetails", "Host Driver", "details (audio)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Controller:
            strResult = QApplication::translate("UIDetails", "Controller", "details (audio)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_IO:
            strResult = QApplication::translate("UIDetails", "Input/Output", "details (audio)"); break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeAudio));
            break;
        }
    }
    return strResult;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeAudio: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio &enmDetailsElementOptionTypeAudio) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeAudio)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Driver:     strResult = "Driver"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Controller: strResult = "Controller"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_IO:         strResult = "IO"; break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeAudio));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DetailsElementOptionTypeAudio <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DetailsElementOptionTypeAudio
UIConverter::fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeAudio>(const QString &strDetailsElementOptionTypeAudio) const
{
    if (strDetailsElementOptionTypeAudio.compare("Driver", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Driver;
    if (strDetailsElementOptionTypeAudio.compare("Controller", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Controller;
    if (strDetailsElementOptionTypeAudio.compare("IO", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_IO;
    return UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Invalid;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork &enmDetailsElementOptionTypeNetwork) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeNetwork)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NotAttached:
            strResult = QApplication::translate("UIDetails", "Not Attached", "details (network adapter)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NAT:
            strResult = QApplication::translate("UIDetails", "NAT", "details (network)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_BridgedAdapter:
            strResult = QApplication::translate("UIDetails", "Bridged Adapter", "details (network)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_InternalNetwork:
            strResult = QApplication::translate("UIDetails", "Internal Network", "details (network)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_HostOnlyAdapter:
            strResult = QApplication::translate("UIDetails", "Host-only Adapter", "details (network)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_GenericDriver:
            strResult = QApplication::translate("UIDetails", "Generic Driver", "details (network)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NATNetwork:
            strResult = QApplication::translate("UIDetails", "NAT Network", "details (network)"); break;
#ifdef VBOX_WITH_CLOUD_NET
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_CloudNetwork:
            strResult = QApplication::translate("UIDetails", "Cloud Network", "details (network)"); break;
#endif
#ifdef VBOX_WITH_VMNET
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_HostOnlyNetwork:
            strResult = QApplication::translate("UIDetails", "Host-only Network", "details (network)"); break;
#endif
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeNetwork));
            break;
        }
    }
    return strResult;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork &enmDetailsElementOptionTypeNetwork) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeNetwork)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NotAttached:     strResult = "NotAttached"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NAT:             strResult = "NAT"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_BridgedAdapter:  strResult = "BridgedAdapter"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_InternalNetwork: strResult = "InternalNetwork"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_HostOnlyAdapter: strResult = "HostOnlyAdapter"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_GenericDriver:   strResult = "GenericDriver"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NATNetwork:      strResult = "NATNetwork"; break;
#ifdef VBOX_WITH_CLOUD_NET
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_CloudNetwork:    strResult = "CloudNetwork"; break;
#endif
#ifdef VBOX_WITH_VMNET
        case UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_HostOnlyNetwork: strResult = "HostOnlyNetwork"; break;
#endif
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeNetwork));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork
UIConverter::fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork>(const QString &strDetailsElementOptionTypeNetwork) const
{
    if (strDetailsElementOptionTypeNetwork.compare("NotAttached", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NotAttached;
    if (strDetailsElementOptionTypeNetwork.compare("NAT", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NAT;
    if (strDetailsElementOptionTypeNetwork.compare("BridgedAdapter", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_BridgedAdapter;
    if (strDetailsElementOptionTypeNetwork.compare("InternalNetwork", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_InternalNetwork;
    if (strDetailsElementOptionTypeNetwork.compare("HostOnlyAdapter", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_HostOnlyAdapter;
    if (strDetailsElementOptionTypeNetwork.compare("GenericDriver", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_GenericDriver;
    if (strDetailsElementOptionTypeNetwork.compare("NATNetwork", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NATNetwork;
#ifdef VBOX_WITH_CLOUD_NET
    if (strDetailsElementOptionTypeNetwork.compare("CloudNetwork", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_CloudNetwork;
#endif
#ifdef VBOX_WITH_VMNET
    if (strDetailsElementOptionTypeNetwork.compare("HostOnlyNetwork", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_HostOnlyNetwork;
#endif
    return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Invalid;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeSerial: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial &enmDetailsElementOptionTypeSerial) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeSerial)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Disconnected:
            strResult = QApplication::translate("UIDetails", "Disconnected", "details (serial port)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_HostPipe:
            strResult = QApplication::translate("UIDetails", "Host Pipe", "details (serial)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_HostDevice:
            strResult = QApplication::translate("UIDetails", "Host Device", "details (serial)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_RawFile:
            strResult = QApplication::translate("UIDetails", "Raw File", "details (serial)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_TCP:
            strResult = QApplication::translate("UIDetails", "TCP", "details (serial)"); break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeSerial));
            break;
        }
    }
    return strResult;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeSerial: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial &enmDetailsElementOptionTypeSerial) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeSerial)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Disconnected: strResult = "Disconnected"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_HostPipe:     strResult = "HostPipe"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_HostDevice:   strResult = "HostDevice"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_RawFile:      strResult = "RawFile"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_TCP:          strResult = "TCP"; break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeSerial));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DetailsElementOptionTypeSerial <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DetailsElementOptionTypeSerial
UIConverter::fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeSerial>(const QString &strDetailsElementOptionTypeSerial) const
{
    if (strDetailsElementOptionTypeSerial.compare("Disconnected", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Disconnected;
    if (strDetailsElementOptionTypeSerial.compare("HostPipe", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_HostPipe;
    if (strDetailsElementOptionTypeSerial.compare("HostDevice", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_HostDevice;
    if (strDetailsElementOptionTypeSerial.compare("RawFile", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_RawFile;
    if (strDetailsElementOptionTypeSerial.compare("TCP", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_TCP;
    return UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Invalid;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeUsb: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb &enmDetailsElementOptionTypeUsb) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeUsb)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Controller:
            strResult = QApplication::translate("UIDetails", "USB Controller", "details (usb)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_DeviceFilters:
            strResult = QApplication::translate("UIDetails", "Device Filters", "details (usb)"); break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeUsb));
            break;
        }
    }
    return strResult;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeUsb: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb &enmDetailsElementOptionTypeUsb) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeUsb)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Controller:    strResult = "Controller"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_DeviceFilters: strResult = "DeviceFilters"; break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeUsb));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DetailsElementOptionTypeUsb <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DetailsElementOptionTypeUsb
UIConverter::fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeUsb>(const QString &strDetailsElementOptionTypeUsb) const
{
    if (strDetailsElementOptionTypeUsb.compare("Controller", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Controller;
    if (strDetailsElementOptionTypeUsb.compare("DeviceFilters", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_DeviceFilters;
    return UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Invalid;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders &enmDetailsElementOptionTypeSharedFolders) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeSharedFolders)
    {
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeSharedFolders));
            break;
        }
    }
    return strResult;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders &enmDetailsElementOptionTypeSharedFolders) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeSharedFolders)
    {
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeSharedFolders));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders
UIConverter::fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders>(const QString &strDetailsElementOptionTypeSharedFolders) const
{
    RT_NOREF(strDetailsElementOptionTypeSharedFolders);
    return UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders_Invalid;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface &enmDetailsElementOptionTypeUserInterface) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeUserInterface)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_VisualState:
            strResult = QApplication::translate("UIDetails", "Visual State", "details (user interface)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_MenuBar:
            strResult = QApplication::translate("UIDetails", "Menu-bar", "details (user interface)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_StatusBar:
            strResult = QApplication::translate("UIDetails", "Status-bar", "details (user interface)"); break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_MiniToolbar:
            strResult = QApplication::translate("UIDetails", "Mini-toolbar", "details (user interface)"); break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeUserInterface));
            break;
        }
    }
    return strResult;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface &enmDetailsElementOptionTypeUserInterface) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeUserInterface)
    {
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_VisualState: strResult = "VisualState"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_MenuBar:     strResult = "MenuBar"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_StatusBar:   strResult = "StatusBar"; break;
        case UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_MiniToolbar: strResult = "MiniToolbar"; break;
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeUserInterface));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface
UIConverter::fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface>(const QString &strDetailsElementOptionTypeUserInterface) const
{
    if (strDetailsElementOptionTypeUserInterface.compare("VisualState", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_VisualState;
    if (strDetailsElementOptionTypeUserInterface.compare("MenuBar", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_MenuBar;
    if (strDetailsElementOptionTypeUserInterface.compare("StatusBar", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_StatusBar;
    if (strDetailsElementOptionTypeUserInterface.compare("MiniToolbar", Qt::CaseInsensitive) == 0)
        return UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_MiniToolbar;
    return UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_Invalid;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeDescription: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIExtraDataMetaDefs::DetailsElementOptionTypeDescription &enmDetailsElementOptionTypeDescription) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeDescription)
    {
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeDescription));
            break;
        }
    }
    return strResult;
}

/* QString <= UIExtraDataMetaDefs::DetailsElementOptionTypeDescription: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIExtraDataMetaDefs::DetailsElementOptionTypeDescription &enmDetailsElementOptionTypeDescription) const
{
    QString strResult;
    switch (enmDetailsElementOptionTypeDescription)
    {
        default:
        {
            AssertMsgFailed(("No text for details element option type=%d", enmDetailsElementOptionTypeDescription));
            break;
        }
    }
    return strResult;
}

/* UIExtraDataMetaDefs::DetailsElementOptionTypeDescription <= QString: */
template<> SHARED_LIBRARY_STUFF UIExtraDataMetaDefs::DetailsElementOptionTypeDescription
UIConverter::fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeDescription>(const QString &strDetailsElementOptionTypeDescription) const
{
    RT_NOREF(strDetailsElementOptionTypeDescription);
    return UIExtraDataMetaDefs::DetailsElementOptionTypeDescription_Invalid;
}

/* QString <= UIColorThemeType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIColorThemeType &colorThemeType) const
{
    QString strResult;
    switch (colorThemeType)
    {
        case UIColorThemeType_Auto:  strResult = QApplication::translate("UICommon", "Follow System Settings", "color theme"); break;
        case UIColorThemeType_Light: strResult = QApplication::translate("UICommon", "Light", "color theme"); break;
        case UIColorThemeType_Dark:  strResult = QApplication::translate("UICommon", "Dark", "color theme"); break;
        default:
        {
            AssertMsgFailed(("No text for color theme type=%d", colorThemeType));
            break;
        }
    }
    return strResult;
}

/* QString <= UIColorThemeType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIColorThemeType &colorThemeType) const
{
    QString strResult;
    switch (colorThemeType)
    {
        case UIColorThemeType_Auto:  break;
        case UIColorThemeType_Light: strResult = "Light"; break;
        case UIColorThemeType_Dark:  strResult = "Dark"; break;
        default:
        {
            AssertMsgFailed(("No text for color theme type=%d", colorThemeType));
            break;
        }
    }
    return strResult;
}

/* UIColorThemeType <= QString: */
template<> UIColorThemeType SHARED_LIBRARY_STUFF UIConverter::fromInternalString<UIColorThemeType>(const QString &strColorThemeType) const
{
    if (strColorThemeType.compare("Light", Qt::CaseInsensitive) == 0)
        return UIColorThemeType_Light;
    if (strColorThemeType.compare("Dark", Qt::CaseInsensitive) == 0)
        return UIColorThemeType_Dark;
    return UIColorThemeType_Auto;
}

/* UILaunchMode <= QString: */
template<> UILaunchMode SHARED_LIBRARY_STUFF UIConverter::fromInternalString<UILaunchMode>(const QString &strDefaultFrontendType) const
{
    if (strDefaultFrontendType.compare("Default", Qt::CaseInsensitive) == 0)
        return UILaunchMode_Default;
    if (strDefaultFrontendType.compare("Headless", Qt::CaseInsensitive) == 0)
        return UILaunchMode_Headless;
    if (strDefaultFrontendType.compare("Separate", Qt::CaseInsensitive) == 0)
        return UILaunchMode_Separate;
    return UILaunchMode_Invalid;
}

/* QString <= UIToolType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIToolType &enmToolType) const
{
    QString strResult;
    switch (enmToolType)
    {
        case UIToolType_Home:         strResult = "Home"; break;
        case UIToolType_Machines:     strResult = "Machines"; break;
        case UIToolType_Extensions:   strResult = "Extensions"; break;
        case UIToolType_Media:        strResult = "Media"; break;
        case UIToolType_Network:      strResult = "Network"; break;
        case UIToolType_Cloud:        strResult = "Cloud"; break;
        case UIToolType_CloudConsole: strResult = "CloudConsole"; break;
        case UIToolType_Activities:   strResult = "Activities"; break;
        case UIToolType_Details:      strResult = "Details"; break;
        case UIToolType_Snapshots:    strResult = "Snapshots"; break;
        case UIToolType_Logs:         strResult = "Logs"; break;
        case UIToolType_VMActivity:   strResult = "Activity"; break;
        case UIToolType_FileManager:  strResult = "FileManager"; break;
        default:
        {
            AssertMsgFailed(("No text for tool type=%d", enmToolType));
            break;
        }
    }
    return strResult;
}

/* UIToolType <= QString: */
template<> SHARED_LIBRARY_STUFF UIToolType UIConverter::fromInternalString<UIToolType>(const QString &strToolType) const
{
    if (strToolType.compare("Home", Qt::CaseInsensitive) == 0)
        return UIToolType_Home;
    if (strToolType.compare("Machines", Qt::CaseInsensitive) == 0)
        return UIToolType_Machines;
    if (strToolType.compare("Extensions", Qt::CaseInsensitive) == 0)
        return UIToolType_Extensions;
    if (strToolType.compare("Media", Qt::CaseInsensitive) == 0)
        return UIToolType_Media;
    if (strToolType.compare("Network", Qt::CaseInsensitive) == 0)
        return UIToolType_Network;
    if (strToolType.compare("Cloud", Qt::CaseInsensitive) == 0)
        return UIToolType_Cloud;
    if (strToolType.compare("CloudConsole", Qt::CaseInsensitive) == 0)
        return UIToolType_CloudConsole;
    if (strToolType.compare("Activities", Qt::CaseInsensitive) == 0)
        return UIToolType_Activities;
    if (strToolType.compare("Details", Qt::CaseInsensitive) == 0)
        return UIToolType_Details;
    if (strToolType.compare("Snapshots", Qt::CaseInsensitive) == 0)
        return UIToolType_Snapshots;
    if (strToolType.compare("Logs", Qt::CaseInsensitive) == 0)
        return UIToolType_Logs;
    if (strToolType.compare("Activity", Qt::CaseInsensitive) == 0)
        return UIToolType_VMActivity;
    if (strToolType.compare("FileManager", Qt::CaseInsensitive) == 0)
        return UIToolType_FileManager;
    return UIToolType_Invalid;
}

/* QString <= UIVisualStateType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIVisualStateType &visualStateType) const
{
    QString strResult;
    switch (visualStateType)
    {
        case UIVisualStateType_Normal:     strResult = QApplication::translate("UICommon", "Normal (window)", "visual state"); break;
        case UIVisualStateType_Fullscreen: strResult = QApplication::translate("UICommon", "Full-screen", "visual state"); break;
        case UIVisualStateType_Seamless:   strResult = QApplication::translate("UICommon", "Seamless", "visual state"); break;
        case UIVisualStateType_Scale:      strResult = QApplication::translate("UICommon", "Scaled", "visual state"); break;
        default:
        {
            AssertMsgFailed(("No text for visual state type=%d", visualStateType));
            break;
        }
    }
    return strResult;
}

/* QString <= UIVisualStateType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIVisualStateType &visualStateType) const
{
    QString strResult;
    switch (visualStateType)
    {
        case UIVisualStateType_Normal:     strResult = "Normal"; break;
        case UIVisualStateType_Fullscreen: strResult = "Fullscreen"; break;
        case UIVisualStateType_Seamless:   strResult = "Seamless"; break;
        case UIVisualStateType_Scale:      strResult = "Scale"; break;
        case UIVisualStateType_All:        strResult = "All"; break;
        default:
        {
            AssertMsgFailed(("No text for visual state type=%d", visualStateType));
            break;
        }
    }
    return strResult;
}

/* UIVisualStateType <= QString: */
template<> SHARED_LIBRARY_STUFF UIVisualStateType UIConverter::fromInternalString<UIVisualStateType>(const QString &strVisualStateType) const
{
    if (strVisualStateType.compare("Normal", Qt::CaseInsensitive) == 0)
        return UIVisualStateType_Normal;
    if (strVisualStateType.compare("Fullscreen", Qt::CaseInsensitive) == 0)
        return UIVisualStateType_Fullscreen;
    if (strVisualStateType.compare("Seamless", Qt::CaseInsensitive) == 0)
        return UIVisualStateType_Seamless;
    if (strVisualStateType.compare("Scale", Qt::CaseInsensitive) == 0)
        return UIVisualStateType_Scale;
    if (strVisualStateType.compare("All", Qt::CaseInsensitive) == 0)
        return UIVisualStateType_All;
    return UIVisualStateType_Invalid;
}

/* QString <= DetailsElementType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const DetailsElementType &detailsElementType) const
{
    QString strResult;
    switch (detailsElementType)
    {
        case DetailsElementType_General:     strResult = QApplication::translate("UICommon", "General", "DetailsElementType"); break;
        case DetailsElementType_Preview:     strResult = QApplication::translate("UICommon", "Preview", "DetailsElementType"); break;
        case DetailsElementType_System:      strResult = QApplication::translate("UICommon", "System", "DetailsElementType"); break;
        case DetailsElementType_Display:     strResult = QApplication::translate("UICommon", "Display", "DetailsElementType"); break;
        case DetailsElementType_Storage:     strResult = QApplication::translate("UICommon", "Storage", "DetailsElementType"); break;
        case DetailsElementType_Audio:       strResult = QApplication::translate("UICommon", "Audio", "DetailsElementType"); break;
        case DetailsElementType_Network:     strResult = QApplication::translate("UICommon", "Network", "DetailsElementType"); break;
        case DetailsElementType_Serial:      strResult = QApplication::translate("UICommon", "Serial ports", "DetailsElementType"); break;
        case DetailsElementType_USB:         strResult = QApplication::translate("UICommon", "USB", "DetailsElementType"); break;
        case DetailsElementType_SF:          strResult = QApplication::translate("UICommon", "Shared folders", "DetailsElementType"); break;
        case DetailsElementType_UI:          strResult = QApplication::translate("UICommon", "User interface", "DetailsElementType"); break;
        case DetailsElementType_Description: strResult = QApplication::translate("UICommon", "Description", "DetailsElementType"); break;
        default:
        {
            AssertMsgFailed(("No text for details element type=%d", detailsElementType));
            break;
        }
    }
    return strResult;
}

/* DetailsElementType <= QString: */
template<> SHARED_LIBRARY_STUFF DetailsElementType UIConverter::fromString<DetailsElementType>(const QString &strDetailsElementType) const
{
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "General", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_General;
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "Preview", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_Preview;
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "System", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_System;
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "Display", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_Display;
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "Storage", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_Storage;
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "Audio", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_Audio;
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "Network", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_Network;
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "Serial ports", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_Serial;
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "USB", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_USB;
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "Shared folders", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_SF;
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "User interface", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_UI;
    if (strDetailsElementType.compare(QApplication::translate("UICommon", "Description", "DetailsElementType"), Qt::CaseInsensitive) == 0)
        return DetailsElementType_Description;
    return DetailsElementType_Invalid;
}

/* QString <= DetailsElementType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const DetailsElementType &detailsElementType) const
{
    QString strResult;
    switch (detailsElementType)
    {
        case DetailsElementType_General:     strResult = "general"; break;
        case DetailsElementType_Preview:     strResult = "preview"; break;
        case DetailsElementType_System:      strResult = "system"; break;
        case DetailsElementType_Display:     strResult = "display"; break;
        case DetailsElementType_Storage:     strResult = "storage"; break;
        case DetailsElementType_Audio:       strResult = "audio"; break;
        case DetailsElementType_Network:     strResult = "network"; break;
        case DetailsElementType_Serial:      strResult = "serialPorts"; break;
        case DetailsElementType_USB:         strResult = "usb"; break;
        case DetailsElementType_SF:          strResult = "sharedFolders"; break;
        case DetailsElementType_UI:          strResult = "userInterface"; break;
        case DetailsElementType_Description: strResult = "description"; break;
        default:
        {
            AssertMsgFailed(("No text for details element type=%d", detailsElementType));
            break;
        }
    }
    return strResult;
}

/* DetailsElementType <= QString: */
template<> SHARED_LIBRARY_STUFF DetailsElementType UIConverter::fromInternalString<DetailsElementType>(const QString &strDetailsElementType) const
{
    if (strDetailsElementType.compare("general", Qt::CaseInsensitive) == 0)
        return DetailsElementType_General;
    if (strDetailsElementType.compare("preview", Qt::CaseInsensitive) == 0)
        return DetailsElementType_Preview;
    if (strDetailsElementType.compare("system", Qt::CaseInsensitive) == 0)
        return DetailsElementType_System;
    if (strDetailsElementType.compare("display", Qt::CaseInsensitive) == 0)
        return DetailsElementType_Display;
    if (strDetailsElementType.compare("storage", Qt::CaseInsensitive) == 0)
        return DetailsElementType_Storage;
    if (strDetailsElementType.compare("audio", Qt::CaseInsensitive) == 0)
        return DetailsElementType_Audio;
    if (strDetailsElementType.compare("network", Qt::CaseInsensitive) == 0)
        return DetailsElementType_Network;
    if (strDetailsElementType.compare("serialPorts", Qt::CaseInsensitive) == 0)
        return DetailsElementType_Serial;
    if (strDetailsElementType.compare("usb", Qt::CaseInsensitive) == 0)
        return DetailsElementType_USB;
    if (strDetailsElementType.compare("sharedFolders", Qt::CaseInsensitive) == 0)
        return DetailsElementType_SF;
    if (strDetailsElementType.compare("userInterface", Qt::CaseInsensitive) == 0)
        return DetailsElementType_UI;
    if (strDetailsElementType.compare("description", Qt::CaseInsensitive) == 0)
        return DetailsElementType_Description;
    return DetailsElementType_Invalid;
}

/* QIcon <= DetailsElementType: */
template<> SHARED_LIBRARY_STUFF QIcon UIConverter::toIcon(const DetailsElementType &detailsElementType) const
{
    switch (detailsElementType)
    {
        case DetailsElementType_General:     return UIIconPool::iconSet(":/machine_16px.png");
        case DetailsElementType_Preview:     return UIIconPool::iconSet(":/machine_16px.png");
        case DetailsElementType_System:      return UIIconPool::iconSet(":/chipset_16px.png");
        case DetailsElementType_Display:     return UIIconPool::iconSet(":/vrdp_16px.png");
        case DetailsElementType_Storage:     return UIIconPool::iconSet(":/hd_16px.png");
        case DetailsElementType_Audio:       return UIIconPool::iconSet(":/sound_16px.png");
        case DetailsElementType_Network:     return UIIconPool::iconSet(":/nw_16px.png");
        case DetailsElementType_Serial:      return UIIconPool::iconSet(":/serial_port_16px.png");
        case DetailsElementType_USB:         return UIIconPool::iconSet(":/usb_16px.png");
        case DetailsElementType_SF:          return UIIconPool::iconSet(":/sf_16px.png");
        case DetailsElementType_UI:          return UIIconPool::iconSet(":/interface_16px.png");
        case DetailsElementType_Description: return UIIconPool::iconSet(":/description_16px.png");
        default:
        {
            AssertMsgFailed(("No icon for details element type=%d", detailsElementType));
            break;
        }
    }
    return QIcon();
}

/* QString <= PreviewUpdateIntervalType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const PreviewUpdateIntervalType &previewUpdateIntervalType) const
{
    /* Return corresponding QString representation for passed enum value: */
    switch (previewUpdateIntervalType)
    {
        case PreviewUpdateIntervalType_Disabled: return "disabled";
        case PreviewUpdateIntervalType_500ms:    return "500";
        case PreviewUpdateIntervalType_1000ms:   return "1000";
        case PreviewUpdateIntervalType_2000ms:   return "2000";
        case PreviewUpdateIntervalType_5000ms:   return "5000";
        case PreviewUpdateIntervalType_10000ms:  return "10000";
        default: AssertMsgFailed(("No text for '%d'", previewUpdateIntervalType)); break;
    }
    /* Return QString() by default: */
    return QString();
}

/* PreviewUpdateIntervalType <= QString: */
template<> SHARED_LIBRARY_STUFF PreviewUpdateIntervalType UIConverter::fromInternalString<PreviewUpdateIntervalType>(const QString &strPreviewUpdateIntervalType) const
{
    if (strPreviewUpdateIntervalType.compare("disabled", Qt::CaseInsensitive) == 0)
        return PreviewUpdateIntervalType_Disabled;
    if (strPreviewUpdateIntervalType.compare("500", Qt::CaseInsensitive) == 0)
        return PreviewUpdateIntervalType_500ms;
    if (strPreviewUpdateIntervalType.compare("1000", Qt::CaseInsensitive) == 0)
        return PreviewUpdateIntervalType_1000ms;
    if (strPreviewUpdateIntervalType.compare("2000", Qt::CaseInsensitive) == 0)
        return PreviewUpdateIntervalType_2000ms;
    if (strPreviewUpdateIntervalType.compare("5000", Qt::CaseInsensitive) == 0)
        return PreviewUpdateIntervalType_5000ms;
    if (strPreviewUpdateIntervalType.compare("10000", Qt::CaseInsensitive) == 0)
        return PreviewUpdateIntervalType_10000ms;
    /* 1000ms type for unknown input: */
    return PreviewUpdateIntervalType_1000ms;
}

/* int <= PreviewUpdateIntervalType: */
template<> SHARED_LIBRARY_STUFF int UIConverter::toInternalInteger(const PreviewUpdateIntervalType &previewUpdateIntervalType) const
{
    /* Return corresponding integer representation for passed enum value: */
    switch (previewUpdateIntervalType)
    {
        case PreviewUpdateIntervalType_Disabled: return 0;
        case PreviewUpdateIntervalType_500ms:    return 500;
        case PreviewUpdateIntervalType_1000ms:   return 1000;
        case PreviewUpdateIntervalType_2000ms:   return 2000;
        case PreviewUpdateIntervalType_5000ms:   return 5000;
        case PreviewUpdateIntervalType_10000ms:  return 10000;
        default: AssertMsgFailed(("No value for '%d'", previewUpdateIntervalType)); break;
    }
    /* Return 0 by default: */
    return 0;
}

/* PreviewUpdateIntervalType <= int: */
template<> SHARED_LIBRARY_STUFF PreviewUpdateIntervalType UIConverter::fromInternalInteger<PreviewUpdateIntervalType>(const int &iPreviewUpdateIntervalType) const
{
    /* Add all the enum values into the hash: */
    QHash<int, PreviewUpdateIntervalType> hash;
    hash.insert(0,     PreviewUpdateIntervalType_Disabled);
    hash.insert(500,   PreviewUpdateIntervalType_500ms);
    hash.insert(1000,  PreviewUpdateIntervalType_1000ms);
    hash.insert(2000,  PreviewUpdateIntervalType_2000ms);
    hash.insert(5000,  PreviewUpdateIntervalType_5000ms);
    hash.insert(10000, PreviewUpdateIntervalType_10000ms);
    /* Make sure hash contains incoming integer representation: */
    if (!hash.contains(iPreviewUpdateIntervalType))
        AssertMsgFailed(("No value for '%d'", iPreviewUpdateIntervalType));
    /* Return corresponding enum value for passed integer representation: */
    return hash.value(iPreviewUpdateIntervalType);
}

/* QString <= UIDiskEncryptionCipherType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIDiskEncryptionCipherType &enmDiskEncryptionCipherType) const
{
    switch (enmDiskEncryptionCipherType)
    {
        case UIDiskEncryptionCipherType_XTS256: return "AES-XTS256-PLAIN64";
        case UIDiskEncryptionCipherType_XTS128: return "AES-XTS128-PLAIN64";
        default:                                break;
    }
    return QString();
}

/* UIDiskEncryptionCipherType <= QString: */
template<> SHARED_LIBRARY_STUFF UIDiskEncryptionCipherType UIConverter::fromInternalString<UIDiskEncryptionCipherType>(const QString &strDiskEncryptionCipherType) const
{
    if (strDiskEncryptionCipherType.compare("AES-XTS256-PLAIN64", Qt::CaseInsensitive) == 0)
        return UIDiskEncryptionCipherType_XTS256;
    if (strDiskEncryptionCipherType.compare("AES-XTS128-PLAIN64", Qt::CaseInsensitive) == 0)
        return UIDiskEncryptionCipherType_XTS128;
    return UIDiskEncryptionCipherType_Unchanged;
}

/* QString <= UIDiskEncryptionCipherType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIDiskEncryptionCipherType &enmDiskEncryptionCipherType) const
{
    switch (enmDiskEncryptionCipherType)
    {
        case UIDiskEncryptionCipherType_XTS256: return "AES-XTS256-PLAIN64";
        case UIDiskEncryptionCipherType_XTS128: return "AES-XTS128-PLAIN64";
        default:                                break;
    }
    return QApplication::translate("UICommon", "Leave Unchanged", "cipher type");
}

/* UIDiskEncryptionCipherType <= QString: */
template<> SHARED_LIBRARY_STUFF UIDiskEncryptionCipherType UIConverter::fromString<UIDiskEncryptionCipherType>(const QString &strDiskEncryptionCipherType) const
{
    if (strDiskEncryptionCipherType.compare("AES-XTS256-PLAIN64", Qt::CaseInsensitive) == 0)
        return UIDiskEncryptionCipherType_XTS256;
    if (strDiskEncryptionCipherType.compare("AES-XTS128-PLAIN64", Qt::CaseInsensitive) == 0)
        return UIDiskEncryptionCipherType_XTS128;
    return UIDiskEncryptionCipherType_Unchanged;
}

/* QString <= GUIFeatureType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const GUIFeatureType &guiFeatureType) const
{
    QString strResult;
    switch (guiFeatureType)
    {
        case GUIFeatureType_NoSelector:     strResult = "noSelector"; break;
#ifdef VBOX_WS_MAC
        case GUIFeatureType_NoUserElements: strResult = "noUserElements"; break;
#else
        case GUIFeatureType_NoMenuBar:      strResult = "noMenuBar"; break;
#endif
        case GUIFeatureType_NoStatusBar:    strResult = "noStatusBar"; break;
        default:
        {
            AssertMsgFailed(("No text for GUI feature type=%d", guiFeatureType));
            break;
        }
    }
    return strResult;
}

/* GUIFeatureType <= QString: */
template<> SHARED_LIBRARY_STUFF GUIFeatureType UIConverter::fromInternalString<GUIFeatureType>(const QString &strGuiFeatureType) const
{
    if (strGuiFeatureType.compare("noSelector", Qt::CaseInsensitive) == 0)
        return GUIFeatureType_NoSelector;
#ifdef VBOX_WS_MAC
    if (strGuiFeatureType.compare("noUserElements", Qt::CaseInsensitive) == 0)
        return GUIFeatureType_NoUserElements;
#else
    if (strGuiFeatureType.compare("noMenuBar", Qt::CaseInsensitive) == 0)
        return GUIFeatureType_NoMenuBar;
#endif
    if (strGuiFeatureType.compare("noStatusBar", Qt::CaseInsensitive) == 0)
        return GUIFeatureType_NoStatusBar;
    return GUIFeatureType_None;
}

/* QString <= GlobalSettingsPageType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const GlobalSettingsPageType &globalSettingsPageType) const
{
    QString strResult;
    switch (globalSettingsPageType)
    {
        case GlobalSettingsPageType_General:    strResult = "General"; break;
        case GlobalSettingsPageType_Input:      strResult = "Input"; break;
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
        case GlobalSettingsPageType_Update:     strResult = "Update"; break;
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
        case GlobalSettingsPageType_Language:   strResult = "Language"; break;
        case GlobalSettingsPageType_Display:    strResult = "Display"; break;
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
        case GlobalSettingsPageType_Proxy:      strResult = "Proxy"; break;
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
        case GlobalSettingsPageType_Interface:  strResult = "Interface"; break;
        default:
        {
            AssertMsgFailed(("No text for settings page type=%d", globalSettingsPageType));
            break;
        }
    }
    return strResult;
}

/* GlobalSettingsPageType <= QString: */
template<> SHARED_LIBRARY_STUFF GlobalSettingsPageType UIConverter::fromInternalString<GlobalSettingsPageType>(const QString &strGlobalSettingsPageType) const
{
    if (strGlobalSettingsPageType.compare("General", Qt::CaseInsensitive) == 0)
        return GlobalSettingsPageType_General;
    if (strGlobalSettingsPageType.compare("Input", Qt::CaseInsensitive) == 0)
        return GlobalSettingsPageType_Input;
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    if (strGlobalSettingsPageType.compare("Update", Qt::CaseInsensitive) == 0)
        return GlobalSettingsPageType_Update;
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
    if (strGlobalSettingsPageType.compare("Language", Qt::CaseInsensitive) == 0)
        return GlobalSettingsPageType_Language;
    if (strGlobalSettingsPageType.compare("Display", Qt::CaseInsensitive) == 0)
        return GlobalSettingsPageType_Display;
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    if (strGlobalSettingsPageType.compare("Proxy", Qt::CaseInsensitive) == 0)
        return GlobalSettingsPageType_Proxy;
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
    if (strGlobalSettingsPageType.compare("Interface", Qt::CaseInsensitive) == 0)
        return GlobalSettingsPageType_Interface;
    return GlobalSettingsPageType_Invalid;
}

/* QPixmap <= GlobalSettingsPageType: */
template<> SHARED_LIBRARY_STUFF QPixmap UIConverter::toWarningPixmap(const GlobalSettingsPageType &type) const
{
    switch (type)
    {
        case GlobalSettingsPageType_General:    return UIIconPool::pixmap(":/machine_warning_16px.png");
        case GlobalSettingsPageType_Input:      return UIIconPool::pixmap(":/hostkey_warning_16px.png");
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
        case GlobalSettingsPageType_Update:     return UIIconPool::pixmap(":/refresh_warning_16px.png");
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
        case GlobalSettingsPageType_Language:   return UIIconPool::pixmap(":/site_warning_16px.png");
        case GlobalSettingsPageType_Display:    return UIIconPool::pixmap(":/vrdp_warning_16px.png");
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
        case GlobalSettingsPageType_Proxy:      return UIIconPool::pixmap(":/proxy_warning_16px.png");
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
        case GlobalSettingsPageType_Interface:  return UIIconPool::pixmap(":/interface_warning_16px.png");
        default: AssertMsgFailed(("No pixmap for %d", type)); break;
    }
    return QPixmap();
}

/* QString <= MachineSettingsPageType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const MachineSettingsPageType &machineSettingsPageType) const
{
    QString strResult;
    switch (machineSettingsPageType)
    {
        case MachineSettingsPageType_General:   strResult = "General"; break;
        case MachineSettingsPageType_System:    strResult = "System"; break;
        case MachineSettingsPageType_Display:   strResult = "Display"; break;
        case MachineSettingsPageType_Storage:   strResult = "Storage"; break;
        case MachineSettingsPageType_Audio:     strResult = "Audio"; break;
        case MachineSettingsPageType_Network:   strResult = "Network"; break;
        case MachineSettingsPageType_Serial:    strResult = "Serial"; break;
        case MachineSettingsPageType_USB:       strResult = "USB"; break;
        case MachineSettingsPageType_SF:        strResult = "SharedFolders"; break;
        case MachineSettingsPageType_Interface: strResult = "Interface"; break;
        default:
        {
            AssertMsgFailed(("No text for settings page type=%d", machineSettingsPageType));
            break;
        }
    }
    return strResult;
}

/* MachineSettingsPageType <= QString: */
template<> SHARED_LIBRARY_STUFF MachineSettingsPageType UIConverter::fromInternalString<MachineSettingsPageType>(const QString &strMachineSettingsPageType) const
{
    if (strMachineSettingsPageType.compare("General", Qt::CaseInsensitive) == 0)
        return MachineSettingsPageType_General;
    if (strMachineSettingsPageType.compare("System", Qt::CaseInsensitive) == 0)
        return MachineSettingsPageType_System;
    if (strMachineSettingsPageType.compare("Display", Qt::CaseInsensitive) == 0)
        return MachineSettingsPageType_Display;
    if (strMachineSettingsPageType.compare("Storage", Qt::CaseInsensitive) == 0)
        return MachineSettingsPageType_Storage;
    if (strMachineSettingsPageType.compare("Audio", Qt::CaseInsensitive) == 0)
        return MachineSettingsPageType_Audio;
    if (strMachineSettingsPageType.compare("Network", Qt::CaseInsensitive) == 0)
        return MachineSettingsPageType_Network;
    if (strMachineSettingsPageType.compare("Serial", Qt::CaseInsensitive) == 0)
        return MachineSettingsPageType_Serial;
    if (strMachineSettingsPageType.compare("USB", Qt::CaseInsensitive) == 0)
        return MachineSettingsPageType_USB;
    if (strMachineSettingsPageType.compare("SharedFolders", Qt::CaseInsensitive) == 0)
        return MachineSettingsPageType_SF;
    if (strMachineSettingsPageType.compare("Interface", Qt::CaseInsensitive) == 0)
        return MachineSettingsPageType_Interface;
    return MachineSettingsPageType_Invalid;
}

/* QPixmap <= MachineSettingsPageType: */
template<> SHARED_LIBRARY_STUFF QPixmap UIConverter::toWarningPixmap(const MachineSettingsPageType &type) const
{
    switch (type)
    {
        case MachineSettingsPageType_General:   return UIIconPool::pixmap(":/machine_warning_16px.png");
        case MachineSettingsPageType_System:    return UIIconPool::pixmap(":/chipset_warning_16px.png");
        case MachineSettingsPageType_Display:   return UIIconPool::pixmap(":/vrdp_warning_16px.png");
        case MachineSettingsPageType_Storage:   return UIIconPool::pixmap(":/hd_warning_16px.png");
        case MachineSettingsPageType_Audio:     return UIIconPool::pixmap(":/sound_warning_16px.png");
        case MachineSettingsPageType_Network:   return UIIconPool::pixmap(":/nw_warning_16px.png");
        case MachineSettingsPageType_Serial:    return UIIconPool::pixmap(":/serial_port_warning_16px.png");
        case MachineSettingsPageType_USB:       return UIIconPool::pixmap(":/usb_warning_16px.png");
        case MachineSettingsPageType_SF:        return UIIconPool::pixmap(":/sf_warning_16px.png");
        case MachineSettingsPageType_Interface: return UIIconPool::pixmap(":/interface_warning_16px.png");
        default: AssertMsgFailed(("No pixmap for %d", type)); break;
    }
    return QPixmap();
}

/* QString <= UIRemoteMode: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIRemoteMode &enmMode) const
{
    QString strResult;
    switch (enmMode)
    {
        case UIRemoteMode_Any: strResult = QApplication::translate("UICommon", "Any", "USB filter remote"); break;
        case UIRemoteMode_On:  strResult = QApplication::translate("UICommon", "Yes", "USB filter remote"); break;
        case UIRemoteMode_Off: strResult = QApplication::translate("UICommon", "No",  "USB filter remote"); break;
        default:
        {
            AssertMsgFailed(("No text for USB filter remote mode=%d", enmMode));
            break;
        }
    }
    return strResult;
}

/* QString <= WizardType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const WizardType &wizardType) const
{
    QString strResult;
    switch (wizardType)
    {
        case WizardType_NewVM:           strResult = "NewVM"; break;
        case WizardType_CloneVM:         strResult = "CloneVM"; break;
        case WizardType_ExportAppliance: strResult = "ExportAppliance"; break;
        case WizardType_ImportAppliance: strResult = "ImportAppliance"; break;
        case WizardType_NewCloudVM:      strResult = "NewCloudVM"; break;
        case WizardType_AddCloudVM:      strResult = "AddCloudVM"; break;
        case WizardType_NewVD:           strResult = "NewVD"; break;
        case WizardType_CloneVD:         strResult = "CloneVD"; break;
        default:
        {
            AssertMsgFailed(("No text for wizard type=%d", wizardType));
            break;
        }
    }
    return strResult;
}

/* WizardType <= QString: */
template<> SHARED_LIBRARY_STUFF WizardType UIConverter::fromInternalString<WizardType>(const QString &strWizardType) const
{
    if (strWizardType.compare("NewVM", Qt::CaseInsensitive) == 0)
        return WizardType_NewVM;
    if (strWizardType.compare("CloneVM", Qt::CaseInsensitive) == 0)
        return WizardType_CloneVM;
    if (strWizardType.compare("ExportAppliance", Qt::CaseInsensitive) == 0)
        return WizardType_ExportAppliance;
    if (strWizardType.compare("ImportAppliance", Qt::CaseInsensitive) == 0)
        return WizardType_ImportAppliance;
    if (strWizardType.compare("NewCloudVM", Qt::CaseInsensitive) == 0)
        return WizardType_NewCloudVM;
    if (strWizardType.compare("AddCloudVM", Qt::CaseInsensitive) == 0)
        return WizardType_AddCloudVM;
    if (strWizardType.compare("NewVD", Qt::CaseInsensitive) == 0)
        return WizardType_NewVD;
    if (strWizardType.compare("CloneVD", Qt::CaseInsensitive) == 0)
        return WizardType_CloneVD;
    return WizardType_Invalid;
}

/* QString <= IndicatorType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const IndicatorType &indicatorType) const
{
    QString strResult;
    switch (indicatorType)
    {
        case IndicatorType_HardDisks:     strResult = "HardDisks"; break;
        case IndicatorType_OpticalDisks:  strResult = "OpticalDisks"; break;
        case IndicatorType_FloppyDisks:   strResult = "FloppyDisks"; break;
        case IndicatorType_Audio:         strResult = "Audio"; break;
        case IndicatorType_Network:       strResult = "Network"; break;
        case IndicatorType_USB:           strResult = "USB"; break;
        case IndicatorType_SharedFolders: strResult = "SharedFolders"; break;
        case IndicatorType_Display:       strResult = "Display"; break;
        case IndicatorType_Recording:     strResult = "Recording"; break;
        case IndicatorType_Features:      strResult = "Features"; break;
        case IndicatorType_Mouse:         strResult = "Mouse"; break;
        case IndicatorType_Keyboard:      strResult = "Keyboard"; break;
        default:
        {
            AssertMsgFailed(("No text for indicator type=%d", indicatorType));
            break;
        }
    }
    return strResult;
}

/* IndicatorType <= QString: */
template<> SHARED_LIBRARY_STUFF IndicatorType UIConverter::fromInternalString<IndicatorType>(const QString &strIndicatorType) const
{
    if (strIndicatorType.compare("HardDisks", Qt::CaseInsensitive) == 0)
        return IndicatorType_HardDisks;
    if (strIndicatorType.compare("OpticalDisks", Qt::CaseInsensitive) == 0)
        return IndicatorType_OpticalDisks;
    if (strIndicatorType.compare("FloppyDisks", Qt::CaseInsensitive) == 0)
        return IndicatorType_FloppyDisks;
    if (strIndicatorType.compare("Audio", Qt::CaseInsensitive) == 0)
        return IndicatorType_Audio;
    if (strIndicatorType.compare("Network", Qt::CaseInsensitive) == 0)
        return IndicatorType_Network;
    if (strIndicatorType.compare("USB", Qt::CaseInsensitive) == 0)
        return IndicatorType_USB;
    if (strIndicatorType.compare("SharedFolders", Qt::CaseInsensitive) == 0)
        return IndicatorType_SharedFolders;
    if (strIndicatorType.compare("Display", Qt::CaseInsensitive) == 0)
        return IndicatorType_Display;
    if (strIndicatorType.compare("Recording", Qt::CaseInsensitive) == 0)
        return IndicatorType_Recording;
    if (strIndicatorType.compare("Features", Qt::CaseInsensitive) == 0)
        return IndicatorType_Features;
    if (strIndicatorType.compare("Mouse", Qt::CaseInsensitive) == 0)
        return IndicatorType_Mouse;
    if (strIndicatorType.compare("Keyboard", Qt::CaseInsensitive) == 0)
        return IndicatorType_Keyboard;
    return IndicatorType_Invalid;
}

/* QString <= IndicatorType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const IndicatorType &indicatorType) const
{
    QString strResult;
    switch (indicatorType)
    {
        case IndicatorType_HardDisks:         strResult = QApplication::translate("UICommon", "Hard Disks", "IndicatorType"); break;
        case IndicatorType_OpticalDisks:      strResult = QApplication::translate("UICommon", "Optical Disks", "IndicatorType"); break;
        case IndicatorType_FloppyDisks:       strResult = QApplication::translate("UICommon", "Floppy Disks", "IndicatorType"); break;
        case IndicatorType_Audio:             strResult = QApplication::translate("UICommon", "Audio", "IndicatorType"); break;
        case IndicatorType_Network:           strResult = QApplication::translate("UICommon", "Network", "IndicatorType"); break;
        case IndicatorType_USB:               strResult = QApplication::translate("UICommon", "USB", "IndicatorType"); break;
        case IndicatorType_SharedFolders:     strResult = QApplication::translate("UICommon", "Shared Folders", "IndicatorType"); break;
        case IndicatorType_Display:           strResult = QApplication::translate("UICommon", "Display", "IndicatorType"); break;
        case IndicatorType_Recording:         strResult = QApplication::translate("UICommon", "Recording", "IndicatorType"); break;
        case IndicatorType_Features:          strResult = QApplication::translate("UICommon", "Features", "IndicatorType"); break;
        case IndicatorType_Mouse:             strResult = QApplication::translate("UICommon", "Mouse", "IndicatorType"); break;
        case IndicatorType_Keyboard:          strResult = QApplication::translate("UICommon", "Keyboard", "IndicatorType"); break;
        case IndicatorType_KeyboardExtension: strResult = QApplication::translate("UICommon", "Keyboard Extension", "IndicatorType"); break;
        default:
        {
            AssertMsgFailed(("No text for indicator type=%d", indicatorType));
            break;
        }
    }
    return strResult;
}

/* QIcon <= IndicatorType: */
template<> SHARED_LIBRARY_STUFF QIcon UIConverter::toIcon(const IndicatorType &indicatorType) const
{
    switch (indicatorType)
    {
        case IndicatorType_HardDisks:     return UIIconPool::iconSet(":/hd_16px.png");
        case IndicatorType_OpticalDisks:  return UIIconPool::iconSet(":/cd_16px.png");
        case IndicatorType_FloppyDisks:   return UIIconPool::iconSet(":/fd_16px.png");
        case IndicatorType_Audio:         return UIIconPool::iconSet(":/audio_16px.png");
        case IndicatorType_Network:       return UIIconPool::iconSet(":/nw_16px.png");
        case IndicatorType_USB:           return UIIconPool::iconSet(":/usb_16px.png");
        case IndicatorType_SharedFolders: return UIIconPool::iconSet(":/sf_16px.png");
        case IndicatorType_Display:       return UIIconPool::iconSet(":/display_software_16px.png");
        case IndicatorType_Recording:     return UIIconPool::iconSet(":/video_capture_16px.png");
        case IndicatorType_Features:      return UIIconPool::iconSet(":/vtx_amdv_16px.png");
        case IndicatorType_Mouse:         return UIIconPool::iconSet(":/mouse_16px.png");
        case IndicatorType_Keyboard:      return UIIconPool::iconSet(":/hostkey_16px.png");
        default:
        {
            AssertMsgFailed(("No icon for indicator type=%d", indicatorType));
            break;
        }
    }
    return QIcon();
}

/* QString <= MachineCloseAction: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const MachineCloseAction &machineCloseAction) const
{
    QString strResult;
    switch (machineCloseAction)
    {
        case MachineCloseAction_Detach:                     strResult = "Detach"; break;
        case MachineCloseAction_SaveState:                  strResult = "SaveState"; break;
        case MachineCloseAction_Shutdown:                   strResult = "Shutdown"; break;
        case MachineCloseAction_PowerOff:                   strResult = "PowerOff"; break;
        case MachineCloseAction_PowerOff_RestoringSnapshot: strResult = "PowerOffRestoringSnapshot"; break;
        default:
        {
            AssertMsgFailed(("No text for indicator type=%d", machineCloseAction));
            break;
        }
    }
    return strResult;
}

/* MachineCloseAction <= QString: */
template<> SHARED_LIBRARY_STUFF MachineCloseAction UIConverter::fromInternalString<MachineCloseAction>(const QString &strMachineCloseAction) const
{
    if (strMachineCloseAction.compare("Detach", Qt::CaseInsensitive) == 0)
        return MachineCloseAction_Detach;
    if (strMachineCloseAction.compare("SaveState", Qt::CaseInsensitive) == 0)
        return MachineCloseAction_SaveState;
    if (strMachineCloseAction.compare("Shutdown", Qt::CaseInsensitive) == 0)
        return MachineCloseAction_Shutdown;
    if (strMachineCloseAction.compare("PowerOff", Qt::CaseInsensitive) == 0)
        return MachineCloseAction_PowerOff;
    if (strMachineCloseAction.compare("PowerOffRestoringSnapshot", Qt::CaseInsensitive) == 0)
        return MachineCloseAction_PowerOff_RestoringSnapshot;
    return MachineCloseAction_Invalid;
}

/* QString <= MouseCapturePolicy: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const MouseCapturePolicy &mouseCapturePolicy) const
{
    /* Return corresponding QString representation for passed enum value: */
    switch (mouseCapturePolicy)
    {
        case MouseCapturePolicy_Default:       return "Default";
        case MouseCapturePolicy_HostComboOnly: return "HostComboOnly";
        case MouseCapturePolicy_Disabled:      return "Disabled";
        default: AssertMsgFailed(("No text for '%d'", mouseCapturePolicy)); break;
    }
    /* Return QString() by default: */
    return QString();
}

/* MouseCapturePolicy <= QString: */
template<> SHARED_LIBRARY_STUFF MouseCapturePolicy UIConverter::fromInternalString<MouseCapturePolicy>(const QString &strMouseCapturePolicy) const
{
    if (strMouseCapturePolicy.compare("Default", Qt::CaseInsensitive) == 0)
        return MouseCapturePolicy_Default;
    if (strMouseCapturePolicy.compare("HostComboOnly", Qt::CaseInsensitive) == 0)
        return MouseCapturePolicy_HostComboOnly;
    if (strMouseCapturePolicy.compare("Disabled", Qt::CaseInsensitive) == 0)
        return MouseCapturePolicy_Disabled;
    return MouseCapturePolicy_Default;
}

/* QString <= GuruMeditationHandlerType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const GuruMeditationHandlerType &guruMeditationHandlerType) const
{
    QString strResult;
    switch (guruMeditationHandlerType)
    {
        case GuruMeditationHandlerType_Default:  strResult = "Default"; break;
        case GuruMeditationHandlerType_PowerOff: strResult = "PowerOff"; break;
        case GuruMeditationHandlerType_Ignore:   strResult = "Ignore"; break;
        default:
        {
            AssertMsgFailed(("No text for indicator type=%d", guruMeditationHandlerType));
            break;
        }
    }
    return strResult;
}

/* GuruMeditationHandlerType <= QString: */
template<> SHARED_LIBRARY_STUFF GuruMeditationHandlerType UIConverter::fromInternalString<GuruMeditationHandlerType>(const QString &strGuruMeditationHandlerType) const
{
    if (strGuruMeditationHandlerType.compare("Default", Qt::CaseInsensitive) == 0)
        return GuruMeditationHandlerType_Default;
    if (strGuruMeditationHandlerType.compare("PowerOff", Qt::CaseInsensitive) == 0)
        return GuruMeditationHandlerType_PowerOff;
    if (strGuruMeditationHandlerType.compare("Ignore", Qt::CaseInsensitive) == 0)
        return GuruMeditationHandlerType_Ignore;
    return GuruMeditationHandlerType_Default;
}

/* QString <= ScalingOptimizationType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const ScalingOptimizationType &optimizationType) const
{
    QString strResult;
    switch (optimizationType)
    {
        case ScalingOptimizationType_None:        strResult = "None"; break;
        case ScalingOptimizationType_Performance: strResult = "Performance"; break;
        default:
        {
            AssertMsgFailed(("No text for type=%d", optimizationType));
            break;
        }
    }
    return strResult;
}

/* ScalingOptimizationType <= QString: */
template<> SHARED_LIBRARY_STUFF ScalingOptimizationType UIConverter::fromInternalString<ScalingOptimizationType>(const QString &strOptimizationType) const
{
    if (strOptimizationType.compare("None", Qt::CaseInsensitive) == 0)
        return ScalingOptimizationType_None;
    if (strOptimizationType.compare("Performance", Qt::CaseInsensitive) == 0)
        return ScalingOptimizationType_Performance;
    return ScalingOptimizationType_None;
}

#ifndef VBOX_WS_MAC

/* QString <= MiniToolbarAlignment: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const MiniToolbarAlignment &miniToolbarAlignment) const
{
    /* Return corresponding QString representation for passed enum value: */
    switch (miniToolbarAlignment)
    {
        case MiniToolbarAlignment_Bottom: return "Bottom";
        case MiniToolbarAlignment_Top:    return "Top";
        default: AssertMsgFailed(("No text for '%d'", miniToolbarAlignment)); break;
    }
    /* Return QString() by default: */
    return QString();
}

/* MiniToolbarAlignment <= QString: */
template<> SHARED_LIBRARY_STUFF MiniToolbarAlignment UIConverter::fromInternalString<MiniToolbarAlignment>(const QString &strMiniToolbarAlignment) const
{
    if (strMiniToolbarAlignment.compare("Bottom", Qt::CaseInsensitive) == 0)
        return MiniToolbarAlignment_Bottom;
    if (strMiniToolbarAlignment.compare("Top", Qt::CaseInsensitive) == 0)
        return MiniToolbarAlignment_Top;
    return MiniToolbarAlignment_Bottom;
}

#endif /* !VBOX_WS_MAC */

/* QString <= InformationElementType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const InformationElementType &informationElementType) const
{
    QString strResult;
    switch (informationElementType)
    {
        case InformationElementType_General:           strResult = QApplication::translate("UICommon", "General", "InformationElementType"); break;
        case InformationElementType_Preview:           strResult = QApplication::translate("UICommon", "Preview", "InformationElementType"); break;
        case InformationElementType_System:            strResult = QApplication::translate("UICommon", "System", "InformationElementType"); break;
        case InformationElementType_Display:           strResult = QApplication::translate("UICommon", "Display", "InformationElementType"); break;
        case InformationElementType_Storage:           strResult = QApplication::translate("UICommon", "Storage", "InformationElementType"); break;
        case InformationElementType_Audio:             strResult = QApplication::translate("UICommon", "Audio", "InformationElementType"); break;
        case InformationElementType_Network:           strResult = QApplication::translate("UICommon", "Network", "InformationElementType"); break;
        case InformationElementType_Serial:            strResult = QApplication::translate("UICommon", "Serial ports", "InformationElementType"); break;
        case InformationElementType_USB:               strResult = QApplication::translate("UICommon", "USB", "InformationElementType"); break;
        case InformationElementType_SharedFolders:     strResult = QApplication::translate("UICommon", "Shared folders", "InformationElementType"); break;
        case InformationElementType_UI:                strResult = QApplication::translate("UICommon", "User interface", "InformationElementType"); break;
        case InformationElementType_Description:       strResult = QApplication::translate("UICommon", "Description", "InformationElementType"); break;
        case InformationElementType_RuntimeAttributes: strResult = QApplication::translate("UICommon", "Runtime attributes", "InformationElementType"); break;
        case InformationElementType_StorageStatistics: strResult = QApplication::translate("UICommon", "Storage statistics", "InformationElementType"); break;
        case InformationElementType_NetworkStatistics: strResult = QApplication::translate("UICommon", "Network statistics", "InformationElementType"); break;
        default:
        {
            AssertMsgFailed(("No text for information element type=%d", informationElementType));
            break;
        }
    }
    return strResult;
}

/* InformationElementType <= QString: */
template<> SHARED_LIBRARY_STUFF InformationElementType UIConverter::fromString<InformationElementType>(const QString &strInformationElementType) const
{
    if (strInformationElementType.compare(QApplication::translate("UICommon", "General", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_General;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "Preview", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_Preview;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "System", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_System;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "Display", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_Display;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "Storage", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_Storage;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "Audio", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_Audio;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "Network", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_Network;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "Serial ports", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_Serial;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "USB", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_USB;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "Shared folders", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_SharedFolders;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "User interface", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_UI;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "Description", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_Description;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "Runtime attributes", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_RuntimeAttributes;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "Storage statistics", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_StorageStatistics;
    if (strInformationElementType.compare(QApplication::translate("UICommon", "Network statistics", "InformationElementType"), Qt::CaseInsensitive) == 0)
        return InformationElementType_NetworkStatistics;
    return InformationElementType_Invalid;
}

/* QString <= InformationElementType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const InformationElementType &informationElementType) const
{
    QString strResult;
    switch (informationElementType)
    {
        case InformationElementType_General:           strResult = "general"; break;
        case InformationElementType_Preview:           strResult = "preview"; break;
        case InformationElementType_System:            strResult = "system"; break;
        case InformationElementType_Display:           strResult = "display"; break;
        case InformationElementType_Storage:           strResult = "storage"; break;
        case InformationElementType_Audio:             strResult = "audio"; break;
        case InformationElementType_Network:           strResult = "network"; break;
        case InformationElementType_Serial:            strResult = "serialPorts"; break;
        case InformationElementType_USB:               strResult = "usb"; break;
        case InformationElementType_SharedFolders:     strResult = "sharedFolders"; break;
        case InformationElementType_UI:                strResult = "userInterface"; break;
        case InformationElementType_Description:       strResult = "description"; break;
        case InformationElementType_RuntimeAttributes: strResult = "runtime-attributes"; break;
        default:
        {
            AssertMsgFailed(("No text for information element type=%d", informationElementType));
            break;
        }
    }
    return strResult;
}

/* InformationElementType <= QString: */
template<> SHARED_LIBRARY_STUFF InformationElementType UIConverter::fromInternalString<InformationElementType>(const QString &strInformationElementType) const
{
    if (strInformationElementType.compare("general", Qt::CaseInsensitive) == 0)
        return InformationElementType_General;
    if (strInformationElementType.compare("preview", Qt::CaseInsensitive) == 0)
        return InformationElementType_Preview;
    if (strInformationElementType.compare("system", Qt::CaseInsensitive) == 0)
        return InformationElementType_System;
    if (strInformationElementType.compare("display", Qt::CaseInsensitive) == 0)
        return InformationElementType_Display;
    if (strInformationElementType.compare("storage", Qt::CaseInsensitive) == 0)
        return InformationElementType_Storage;
    if (strInformationElementType.compare("audio", Qt::CaseInsensitive) == 0)
        return InformationElementType_Audio;
    if (strInformationElementType.compare("network", Qt::CaseInsensitive) == 0)
        return InformationElementType_Network;
    if (strInformationElementType.compare("serialPorts", Qt::CaseInsensitive) == 0)
        return InformationElementType_Serial;
    if (strInformationElementType.compare("usb", Qt::CaseInsensitive) == 0)
        return InformationElementType_USB;
    if (strInformationElementType.compare("sharedFolders", Qt::CaseInsensitive) == 0)
        return InformationElementType_SharedFolders;
    if (strInformationElementType.compare("userInterface", Qt::CaseInsensitive) == 0)
        return InformationElementType_UI;
    if (strInformationElementType.compare("description", Qt::CaseInsensitive) == 0)
        return InformationElementType_Description;
    if (strInformationElementType.compare("runtime-attributes", Qt::CaseInsensitive) == 0)
        return InformationElementType_RuntimeAttributes;
    return InformationElementType_Invalid;
}

/* QIcon <= InformationElementType: */
template<> SHARED_LIBRARY_STUFF QIcon UIConverter::toIcon(const InformationElementType &informationElementType) const
{
    switch (informationElementType)
    {
        case InformationElementType_General:           return UIIconPool::iconSet(":/machine_16px.png");
        case InformationElementType_Preview:           return UIIconPool::iconSet(":/machine_16px.png");
        case InformationElementType_System:            return UIIconPool::iconSet(":/chipset_16px.png");
        case InformationElementType_Display:           return UIIconPool::iconSet(":/vrdp_16px.png");
        case InformationElementType_Storage:           return UIIconPool::iconSet(":/hd_16px.png");
        case InformationElementType_Audio:             return UIIconPool::iconSet(":/sound_16px.png");
        case InformationElementType_Network:           return UIIconPool::iconSet(":/nw_16px.png");
        case InformationElementType_Serial:            return UIIconPool::iconSet(":/serial_port_16px.png");
        case InformationElementType_USB:               return UIIconPool::iconSet(":/usb_16px.png");
        case InformationElementType_SharedFolders:     return UIIconPool::iconSet(":/sf_16px.png");
        case InformationElementType_UI:                return UIIconPool::iconSet(":/interface_16px.png");
        case InformationElementType_Description:       return UIIconPool::iconSet(":/description_16px.png");
        case InformationElementType_RuntimeAttributes: return UIIconPool::iconSet(":/state_running_16px.png");
        case InformationElementType_StorageStatistics: return UIIconPool::iconSet(":/hd_16px.png");
        case InformationElementType_NetworkStatistics: return UIIconPool::iconSet(":/nw_16px.png");
        default:
        {
            AssertMsgFailed(("No icon for information element type=%d", informationElementType));
            break;
        }
    }
    return QIcon();
}

/* QString <= MaximumGuestScreenSizePolicy: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const MaximumGuestScreenSizePolicy &enmMaximumGuestScreenSizePolicy) const
{
    QString strResult;
    switch (enmMaximumGuestScreenSizePolicy)
    {
        case MaximumGuestScreenSizePolicy_Any:       strResult = QApplication::translate("UICommon", "None", "Maximum Guest Screen Size"); break;
        case MaximumGuestScreenSizePolicy_Fixed:     strResult = QApplication::translate("UICommon", "Hint", "Maximum Guest Screen Size"); break;
        case MaximumGuestScreenSizePolicy_Automatic: strResult = QApplication::translate("UICommon", "Automatic", "Maximum Guest Screen Size"); break;
        default:
        {
            AssertMsgFailed(("No text for maximum guest resolution policy=%d", enmMaximumGuestScreenSizePolicy));
            break;
        }
    }
    return strResult;
}

/* QString <= MaximumGuestScreenSizePolicy: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const MaximumGuestScreenSizePolicy &enmMaximumGuestScreenSizePolicy) const
{
    QString strResult;
    switch (enmMaximumGuestScreenSizePolicy)
    {
        case MaximumGuestScreenSizePolicy_Automatic: strResult = ""; break;
        case MaximumGuestScreenSizePolicy_Any:       strResult = "any"; break;
        default:
        {
            AssertMsgFailed(("No text for maximum guest resolution policy=%d", enmMaximumGuestScreenSizePolicy));
            break;
        }
    }
    return strResult;
}

/* MaximumGuestScreenSizePolicy <= QString: */
template<> SHARED_LIBRARY_STUFF MaximumGuestScreenSizePolicy
UIConverter::fromInternalString<MaximumGuestScreenSizePolicy>(const QString &strMaximumGuestScreenSizePolicy) const
{
    if (   strMaximumGuestScreenSizePolicy.isEmpty()
        || strMaximumGuestScreenSizePolicy.compare("auto", Qt::CaseInsensitive) == 0)
        return MaximumGuestScreenSizePolicy_Automatic;
    if (strMaximumGuestScreenSizePolicy.compare("any", Qt::CaseInsensitive) == 0)
        return MaximumGuestScreenSizePolicy_Any;
    /* Fixed type for value which can be parsed: */
    if (QRegularExpression("[1-9]\\d*,[1-9]\\d*").match(strMaximumGuestScreenSizePolicy).hasMatch())
        return MaximumGuestScreenSizePolicy_Fixed;
    return MaximumGuestScreenSizePolicy_Any;
}

/* QString <= UIMediumFormat: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIMediumFormat &enmUIMediumFormat) const
{
    QString strResult;
    switch (enmUIMediumFormat)
    {
        case UIMediumFormat_VDI:       strResult = QApplication::translate("UICommon", "VDI (VirtualBox Disk Image)", "UIMediumFormat"); break;
        case UIMediumFormat_VMDK:      strResult = QApplication::translate("UICommon", "VMDK (Virtual Machine Disk)", "UIMediumFormat"); break;
        case UIMediumFormat_VHD:       strResult = QApplication::translate("UICommon", "VHD (Virtual Hard Disk)", "UIMediumFormat"); break;
        case UIMediumFormat_Parallels: strResult = QApplication::translate("UICommon", "HDD (Parallels Hard Disk)", "UIMediumFormat"); break;
        case UIMediumFormat_QED:       strResult = QApplication::translate("UICommon", "QED (QEMU enhanced disk)", "UIMediumFormat"); break;
        case UIMediumFormat_QCOW:      strResult = QApplication::translate("UICommon", "QCOW (QEMU Copy-On-Write)", "UIMediumFormat"); break;
        default:
        {
            AssertMsgFailed(("No text for medium format=%d", enmUIMediumFormat));
            break;
        }
    }
    return strResult;
}

/* QString <= UIMediumFormat: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIMediumFormat &enmUIMediumFormat) const
{
    QString strResult;
    switch (enmUIMediumFormat)
    {
        case UIMediumFormat_VDI:       strResult = "VDI"; break;
        case UIMediumFormat_VMDK:      strResult = "VMDK"; break;
        case UIMediumFormat_VHD:       strResult = "VHD"; break;
        case UIMediumFormat_Parallels: strResult = "Parallels"; break;
        case UIMediumFormat_QED:       strResult = "QED"; break;
        case UIMediumFormat_QCOW:      strResult = "QCOW"; break;
        default:
        {
            AssertMsgFailed(("No text for medium format=%d", enmUIMediumFormat));
            break;
        }
    }
    return strResult;
}

/* UIMediumFormat <= QString: */
template<> SHARED_LIBRARY_STUFF UIMediumFormat UIConverter::fromInternalString<UIMediumFormat>(const QString &strUIMediumFormat) const
{
    if (strUIMediumFormat.compare("VDI", Qt::CaseInsensitive) == 0)
        return UIMediumFormat_VDI;
    if (strUIMediumFormat.compare("VMDK", Qt::CaseInsensitive) == 0)
        return UIMediumFormat_VMDK;
    if (strUIMediumFormat.compare("VHD", Qt::CaseInsensitive) == 0)
        return UIMediumFormat_VHD;
    if (strUIMediumFormat.compare("Parallels", Qt::CaseInsensitive) == 0)
        return UIMediumFormat_Parallels;
    if (strUIMediumFormat.compare("QED", Qt::CaseInsensitive) == 0)
        return UIMediumFormat_QED;
    if (strUIMediumFormat.compare("QCOW", Qt::CaseInsensitive) == 0)
        return UIMediumFormat_QCOW;
    return UIMediumFormat_VDI;
}

/* QString <= UISettingsDefs::RecordingMode: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UISettingsDefs::RecordingMode &enmRecordingMode) const
{
    QString strResult;
    switch (enmRecordingMode)
    {
        case UISettingsDefs::RecordingMode_None:       strResult = QApplication::translate("UICommon", "None", "UISettingsDefs::RecordingMode"); break;
        case UISettingsDefs::RecordingMode_VideoAudio: strResult = QApplication::translate("UICommon", "Video/Audio", "UISettingsDefs::RecordingMode"); break;
        case UISettingsDefs::RecordingMode_VideoOnly:  strResult = QApplication::translate("UICommon", "Video Only",  "UISettingsDefs::RecordingMode"); break;
        case UISettingsDefs::RecordingMode_AudioOnly:  strResult = QApplication::translate("UICommon", "Audio Only",  "UISettingsDefs::RecordingMode"); break;
        default:
        {
            AssertMsgFailed(("No text for recording mode format=%d", enmRecordingMode));
            break;
        }
    }
    return strResult;
}

/* QString <= VMActivityOverviewColumn: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const VMActivityOverviewColumn &enmVMActivityOverviewColumn) const
{
    QString strResult;
    switch (enmVMActivityOverviewColumn)
    {
        case VMActivityOverviewColumn_Name:              strResult = "VMName"; break;
        case VMActivityOverviewColumn_CPUGuestLoad:      strResult = "CPUGuestLoad"; break;
        case VMActivityOverviewColumn_CPUVMMLoad:        strResult = "CPUVMMLoad"; break;
        case VMActivityOverviewColumn_RAMUsedAndTotal:   strResult = "RAMUsedAndTotal"; break;
        case VMActivityOverviewColumn_RAMUsedPercentage: strResult = "RAMUsedPercentage"; break;
        case VMActivityOverviewColumn_NetworkUpRate:     strResult = "NetworkUpRate"; break;
        case VMActivityOverviewColumn_NetworkDownRate:   strResult = "NetworkDownRate"; break;
        case VMActivityOverviewColumn_NetworkUpTotal:    strResult = "NetworkUpTotal"; break;
        case VMActivityOverviewColumn_NetworkDownTotal:  strResult = "NetworkDownTotal"; break;
        case VMActivityOverviewColumn_DiskIOReadRate:    strResult = "DiskIOReadRate"; break;
        case VMActivityOverviewColumn_DiskIOWriteRate:   strResult = "DiskIOWriteRate"; break;
        case VMActivityOverviewColumn_DiskIOReadTotal:   strResult = "DiskIOReadTotal"; break;
        case VMActivityOverviewColumn_DiskIOWriteTotal:  strResult = "DiskIOWriteTotal"; break;
        case VMActivityOverviewColumn_VMExits:           strResult = "VMExits"; break;
        default:
            {
                AssertMsgFailed(("No text for VM Activity Overview Column=%d", enmVMActivityOverviewColumn));
                break;
            }
    }
    return strResult;
}

/* VMActivityOverviewColumn <= QString: */
template<> SHARED_LIBRARY_STUFF VMActivityOverviewColumn UIConverter::fromInternalString<VMActivityOverviewColumn>(const QString &strVMActivityOverviewColumn) const
{
    if (strVMActivityOverviewColumn.compare("VMName", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_Name;
    if (strVMActivityOverviewColumn.compare("CPUGuestLoad", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_CPUGuestLoad;
    if (strVMActivityOverviewColumn.compare("CPUVMMLoad", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_CPUVMMLoad;
    if (strVMActivityOverviewColumn.compare("RAMUsedAndTotal", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_RAMUsedAndTotal;
    if (strVMActivityOverviewColumn.compare("RAMUsedPercentage", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_RAMUsedPercentage;
    if (strVMActivityOverviewColumn.compare("NetworkUpRate", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_NetworkUpRate;
    if (strVMActivityOverviewColumn.compare("NetworkDownRate", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_NetworkDownRate;
    if (strVMActivityOverviewColumn.compare("NetworkUpTotal", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_NetworkUpTotal;
    if (strVMActivityOverviewColumn.compare("NetworkDownTotal", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_NetworkDownTotal;
    if (strVMActivityOverviewColumn.compare("DiskIOReadRate", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_DiskIOReadRate;
    if (strVMActivityOverviewColumn.compare("DiskIOWriteRate", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_DiskIOWriteRate;
    if (strVMActivityOverviewColumn.compare("DiskIOReadTotal", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_DiskIOReadTotal;
    if (strVMActivityOverviewColumn.compare("DiskIOWriteTotal", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_DiskIOWriteTotal;
    if (strVMActivityOverviewColumn.compare("VMExits", Qt::CaseInsensitive) == 0)
        return VMActivityOverviewColumn_VMExits;
    return VMActivityOverviewColumn_Max;
}

/* QString <= UIVRDESecurityMethod: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const UIVRDESecurityMethod &enmSecurityMethod) const
{
    QString strResult;
    switch (enmSecurityMethod)
    {
        case UIVRDESecurityMethod_TLS:       strResult = QApplication::translate("UICommon", "TLS"); break;
        case UIVRDESecurityMethod_RDP:       strResult = QApplication::translate("UICommon", "RDP"); break;
        case UIVRDESecurityMethod_Negotiate: strResult = QApplication::translate("UICommon", "NEGOTIATE"); break;
        default:
        {
            AssertMsgFailed(("No text for security method=%d", enmSecurityMethod));
            break;
        }
    }
    return strResult;
}

/* QString <= UIVRDESecurityMethod: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const UIVRDESecurityMethod &enmSecurityMethod) const
{
    QString strResult;
    switch (enmSecurityMethod)
    {
        case UIVRDESecurityMethod_TLS:       strResult = QString("TLS"); break;
        case UIVRDESecurityMethod_RDP:       strResult = QString("RDP"); break;
        case UIVRDESecurityMethod_Negotiate: strResult = QString("NEGOTIATE"); break;
        default:
        {
            AssertMsgFailed(("No text for security method=%d", enmSecurityMethod));
            break;
        }
    }
    return strResult;
}

/* UIVRDESecurityMethod <= QString: */
template<> SHARED_LIBRARY_STUFF UIVRDESecurityMethod UIConverter::fromInternalString<UIVRDESecurityMethod>(const QString &strSecurityMethod) const
{
    if (strSecurityMethod.compare("RDP", Qt::CaseInsensitive) == 0)
        return UIVRDESecurityMethod_RDP;
    if (strSecurityMethod.compare("NEGOTIATE", Qt::CaseInsensitive) == 0)
        return UIVRDESecurityMethod_Negotiate;
    return UIVRDESecurityMethod_TLS;
}

#ifdef VBOX_WS_WIN
/* WindowsRelease <= QString: */
template<> SHARED_LIBRARY_STUFF WindowsRelease UIConverter::fromInternalString<WindowsRelease>(const QString &strRelease) const
{
    if (strRelease.compare("Windows 11", Qt::CaseInsensitive) == 0)
        return WindowsRelease_11;
    return WindowsRelease_Unknown;
}
#endif
