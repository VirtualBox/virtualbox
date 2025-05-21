/* $Id: UIConverterBackendCOM.cpp 168951 2025-05-21 10:21:17Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIConverterBackendCOM implementation.
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

/* GUI includes: */
#include "UIConverter.h"
#include "UIIconPool.h"

/* Other VBox includes: */
#include "iprt/assert.h"

/* COM includes: */
#include "KAudioControllerType.h"
#include "KAuthType.h"
#include "KAudioDriverType.h"
#include "KChipsetType.h"
#include "KClipboardMode.h"
#include "KCloudMachineState.h"
#include "KDeviceType.h"
#include "KDnDMode.h"
#include "KGraphicsControllerType.h"
#include "KGuestSessionStatus.h"
#include "KMachineState.h"
#include "KMediumType.h"
#include "KMediumVariant.h"
#include "KMetricType.h"
#include "KNATProtocol.h"
#include "KNetworkAdapterType.h"
#include "KNetworkAttachmentType.h"
#include "KNetworkAdapterPromiscModePolicy.h"
#include "KParavirtProvider.h"
#include "KPointingHIDType.h"
#include "KAudioDriverType.h"
#include "KPortMode.h"
#include "KProcessStatus.h"
#include "KSessionState.h"
#include "KStorageBus.h"
#include "KStorageControllerType.h"
#include "KTpmType.h"
#include "KUSBControllerType.h"
#include "KUSBDeviceFilterAction.h"
#include "KUSBDeviceState.h"

/* QIcon <= KCloudMachineState: */
template<> SHARED_LIBRARY_STUFF QIcon UIConverter::toIcon(const KCloudMachineState &state) const
{
    switch (state)
    {
        case KCloudMachineState_Provisioning:  return UIIconPool::iconSet(":/state_running_16px.png");
        case KCloudMachineState_Running:       return UIIconPool::iconSet(":/state_running_16px.png");
        case KCloudMachineState_Starting:      return UIIconPool::iconSet(":/state_running_16px.png");
        case KCloudMachineState_Stopping:      return UIIconPool::iconSet(":/state_saving_16px.png");
        case KCloudMachineState_Stopped:       return UIIconPool::iconSet(":/state_saved_16px.png");
        case KCloudMachineState_CreatingImage: return UIIconPool::iconSet(":/state_saved_16px.png");
        case KCloudMachineState_Terminating:   return UIIconPool::iconSet(":/state_discarding_16px.png");
        case KCloudMachineState_Terminated:    return UIIconPool::iconSet(":/state_powered_off_16px.png");
        default: AssertMsgFailed(("No icon for %d", state)); break;
    }
    return QIcon();
}

/* QString <= KCloudMachineState: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KCloudMachineState &state) const
{
    switch (state)
    {
        case KCloudMachineState_Provisioning:  return QApplication::translate("UICommon", "Provisioning", "CloudMachineState");
        case KCloudMachineState_Running:       return QApplication::translate("UICommon", "Running", "CloudMachineState");
        case KCloudMachineState_Starting:      return QApplication::translate("UICommon", "Starting", "CloudMachineState");
        case KCloudMachineState_Stopping:      return QApplication::translate("UICommon", "Stopping", "CloudMachineState");
        case KCloudMachineState_Stopped:       return QApplication::translate("UICommon", "Stopped", "CloudMachineState");
        case KCloudMachineState_CreatingImage: return QApplication::translate("UICommon", "Creating Image", "CloudMachineState");
        case KCloudMachineState_Terminating:   return QApplication::translate("UICommon", "Terminating", "CloudMachineState");
        case KCloudMachineState_Terminated:    return QApplication::translate("UICommon", "Terminated", "CloudMachineState");
        default: AssertMsgFailed(("No text for %d", state)); break;
    }
    return QString();
}

/* QColor <= KMachineState: */
template<> SHARED_LIBRARY_STUFF QColor UIConverter::toColor(const KMachineState &state) const
{
    switch (state)
    {
        case KMachineState_PoweredOff:             return QColor(Qt::gray);
        case KMachineState_Saved:                  return QColor(Qt::yellow);
        case KMachineState_Teleported:             return QColor(Qt::red);
        case KMachineState_Aborted:                return QColor(Qt::darkRed);
        case KMachineState_AbortedSaved:           return QColor(Qt::yellow);
        case KMachineState_Running:                return QColor(Qt::green);
        case KMachineState_Paused:                 return QColor(Qt::darkGreen);
        case KMachineState_Stuck:                  return QColor(Qt::darkMagenta);
        case KMachineState_Teleporting:            return QColor(Qt::blue);
        case KMachineState_Snapshotting:           return QColor(Qt::green);
        case KMachineState_OnlineSnapshotting:     return QColor(Qt::green);
        case KMachineState_LiveSnapshotting:       return QColor(Qt::green);
        case KMachineState_Starting:               return QColor(Qt::green);
        case KMachineState_Stopping:               return QColor(Qt::green);
        case KMachineState_Saving:                 return QColor(Qt::green);
        case KMachineState_Restoring:              return QColor(Qt::green);
        case KMachineState_TeleportingPausedVM:    return QColor(Qt::blue);
        case KMachineState_TeleportingIn:          return QColor(Qt::blue);
        case KMachineState_DeletingSnapshotOnline: return QColor(Qt::green);
        case KMachineState_DeletingSnapshotPaused: return QColor(Qt::darkGreen);
        case KMachineState_RestoringSnapshot:      return QColor(Qt::green);
        case KMachineState_DeletingSnapshot:       return QColor(Qt::green);
        case KMachineState_SettingUp:              return QColor(Qt::green);
        // case KMachineState_FirstOnline:
        // case KMachineState_LastOnline:
        // case KMachineState_FirstTransient:
        // case KMachineState_LastTransient:
        default: AssertMsgFailed(("No color for %d", state)); break;
    }
    return QColor();
}

/* QIcon <= KMachineState: */
template<> SHARED_LIBRARY_STUFF QIcon UIConverter::toIcon(const KMachineState &state) const
{
    switch (state)
    {
        case KMachineState_PoweredOff:             return UIIconPool::iconSet(":/state_powered_off_16px.png");
        case KMachineState_Saved:                  return UIIconPool::iconSet(":/state_saved_16px.png");
        case KMachineState_Teleported:             return UIIconPool::iconSet(":/state_saved_16px.png");
        case KMachineState_Aborted:                return UIIconPool::iconSet(":/state_aborted_16px.png");
        case KMachineState_AbortedSaved:           return UIIconPool::iconSet(":/state_aborted_saved_16px.png");
        case KMachineState_Running:                return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_Paused:                 return UIIconPool::iconSet(":/state_paused_16px.png");
        case KMachineState_Stuck:                  return UIIconPool::iconSet(":/state_stuck_16px.png");
        case KMachineState_Teleporting:            return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_Snapshotting:           return UIIconPool::iconSet(":/state_saving_16px.png");
        case KMachineState_OnlineSnapshotting:     return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_LiveSnapshotting:       return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_Starting:               return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_Stopping:               return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_Saving:                 return UIIconPool::iconSet(":/state_saving_16px.png");
        case KMachineState_Restoring:              return UIIconPool::iconSet(":/state_restoring_16px.png");
        case KMachineState_TeleportingPausedVM:    return UIIconPool::iconSet(":/state_saving_16px.png");
        case KMachineState_TeleportingIn:          return UIIconPool::iconSet(":/state_restoring_16px.png");
        case KMachineState_DeletingSnapshotOnline: return UIIconPool::iconSet(":/state_discarding_16px.png");
        case KMachineState_DeletingSnapshotPaused: return UIIconPool::iconSet(":/state_discarding_16px.png");
        case KMachineState_RestoringSnapshot:      return UIIconPool::iconSet(":/state_discarding_16px.png");
        case KMachineState_DeletingSnapshot:       return UIIconPool::iconSet(":/state_discarding_16px.png");
        case KMachineState_SettingUp:              return UIIconPool::iconSet(":/vm_settings_16px.png"); /// @todo Change icon!
        // case KMachineState_FirstOnline:
        // case KMachineState_LastOnline:
        // case KMachineState_FirstTransient:
        // case KMachineState_LastTransient:
        default: AssertMsgFailed(("No icon for %d", state)); break;
    }
    return QIcon();
}

/* QString <= KMachineState: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KMachineState &state) const
{
    switch (state)
    {
        case KMachineState_PoweredOff:             return QApplication::translate("UICommon", "Powered Off", "MachineState");
        case KMachineState_Saved:                  return QApplication::translate("UICommon", "Saved", "MachineState");
        case KMachineState_Teleported:             return QApplication::translate("UICommon", "Teleported", "MachineState");
        case KMachineState_Aborted:                return QApplication::translate("UICommon", "Aborted", "MachineState");
        case KMachineState_AbortedSaved:           return QApplication::translate("UICommon", "Aborted-Saved", "MachineState");
        case KMachineState_Running:                return QApplication::translate("UICommon", "Running", "MachineState");
        case KMachineState_Paused:                 return QApplication::translate("UICommon", "Paused", "MachineState");
        case KMachineState_Stuck:                  return QApplication::translate("UICommon", "Guru Meditation", "MachineState");
        case KMachineState_Teleporting:            return QApplication::translate("UICommon", "Teleporting", "MachineState");
        case KMachineState_Snapshotting:           return QApplication::translate("UICommon", "Taking Snapshot", "MachineState");
        case KMachineState_OnlineSnapshotting:     return QApplication::translate("UICommon", "Taking Online Snapshot", "MachineState");
        case KMachineState_LiveSnapshotting:       return QApplication::translate("UICommon", "Taking Live Snapshot", "MachineState");
        case KMachineState_Starting:               return QApplication::translate("UICommon", "Starting", "MachineState");
        case KMachineState_Stopping:               return QApplication::translate("UICommon", "Stopping", "MachineState");
        case KMachineState_Saving:                 return QApplication::translate("UICommon", "Saving", "MachineState");
        case KMachineState_Restoring:              return QApplication::translate("UICommon", "Restoring", "MachineState");
        case KMachineState_TeleportingPausedVM:    return QApplication::translate("UICommon", "Teleporting Paused VM", "MachineState");
        case KMachineState_TeleportingIn:          return QApplication::translate("UICommon", "Teleporting", "MachineState");
        case KMachineState_DeletingSnapshotOnline: return QApplication::translate("UICommon", "Deleting Snapshot", "MachineState");
        case KMachineState_DeletingSnapshotPaused: return QApplication::translate("UICommon", "Deleting Snapshot", "MachineState");
        case KMachineState_RestoringSnapshot:      return QApplication::translate("UICommon", "Restoring Snapshot", "MachineState");
        case KMachineState_DeletingSnapshot:       return QApplication::translate("UICommon", "Deleting Snapshot", "MachineState");
        case KMachineState_SettingUp:              return QApplication::translate("UICommon", "Setting Up", "MachineState");
        // case KMachineState_FirstOnline:
        // case KMachineState_LastOnline:
        // case KMachineState_FirstTransient:
        // case KMachineState_LastTransient:
        default: AssertMsgFailed(("No text for %d", state)); break;
    }
    return QString();
}

/* QString <= KSessionState: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KSessionState &state) const
{
    switch (state)
    {
        case KSessionState_Unlocked:  return QApplication::translate("UICommon", "Unlocked", "SessionState");
        case KSessionState_Locked:    return QApplication::translate("UICommon", "Locked", "SessionState");
        case KSessionState_Spawning:  return QApplication::translate("UICommon", "Spawning", "SessionState");
        case KSessionState_Unlocking: return QApplication::translate("UICommon", "Unlocking", "SessionState");
        default: AssertMsgFailed(("No text for %d", state)); break;
    }
    return QString();
}

/* QString <= KParavirtProvider: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KParavirtProvider &type) const
{
    switch (type)
    {
        case KParavirtProvider_None:    return QApplication::translate("UICommon", "None", "ParavirtProvider");
        case KParavirtProvider_Default: return QApplication::translate("UICommon", "Default", "ParavirtProvider");
        case KParavirtProvider_Legacy:  return QApplication::translate("UICommon", "Legacy", "ParavirtProvider");
        case KParavirtProvider_Minimal: return QApplication::translate("UICommon", "Minimal", "ParavirtProvider");
        case KParavirtProvider_HyperV:  return QApplication::translate("UICommon", "Hyper-V", "ParavirtProvider");
        case KParavirtProvider_KVM:     return QApplication::translate("UICommon", "KVM", "ParavirtProvider");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KDeviceType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KDeviceType &type) const
{
    switch (type)
    {
        case KDeviceType_Null:         return QApplication::translate("UICommon", "None", "DeviceType");
        case KDeviceType_Floppy:       return QApplication::translate("UICommon", "Floppy", "DeviceType");
        case KDeviceType_DVD:          return QApplication::translate("UICommon", "Optical", "DeviceType");
        case KDeviceType_HardDisk:     return QApplication::translate("UICommon", "Hard Disk", "DeviceType");
        case KDeviceType_Network:      return QApplication::translate("UICommon", "Network", "DeviceType");
        case KDeviceType_USB:          return QApplication::translate("UICommon", "USB", "DeviceType");
        case KDeviceType_SharedFolder: return QApplication::translate("UICommon", "Shared Folder", "DeviceType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KClipboardMode: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KClipboardMode &mode) const
{
    switch (mode)
    {
        case KClipboardMode_Disabled:      return QApplication::translate("UICommon", "Disabled", "ClipboardType");
        case KClipboardMode_HostToGuest:   return QApplication::translate("UICommon", "Host To Guest", "ClipboardType");
        case KClipboardMode_GuestToHost:   return QApplication::translate("UICommon", "Guest To Host", "ClipboardType");
        case KClipboardMode_Bidirectional: return QApplication::translate("UICommon", "Bidirectional", "ClipboardType");
        default: AssertMsgFailed(("No text for %d", mode)); break;
    }
    return QString();
}

/* QString <= KDnDMode: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KDnDMode &mode) const
{
    switch (mode)
    {
        case KDnDMode_Disabled:      return QApplication::translate("UICommon", "Disabled", "DragAndDropType");
        case KDnDMode_HostToGuest:   return QApplication::translate("UICommon", "Host To Guest", "DragAndDropType");
        case KDnDMode_GuestToHost:   return QApplication::translate("UICommon", "Guest To Host", "DragAndDropType");
        case KDnDMode_Bidirectional: return QApplication::translate("UICommon", "Bidirectional", "DragAndDropType");
        default: AssertMsgFailed(("No text for %d", mode)); break;
    }
    return QString();
}

/* QString <= KPointingHIDType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KPointingHIDType &type) const
{
    switch (type)
    {
        case KPointingHIDType_None:                       return QApplication::translate("UICommon", "None", "PointingHIDType");
        case KPointingHIDType_PS2Mouse:                   return QApplication::translate("UICommon", "PS/2 Mouse", "PointingHIDType");
        case KPointingHIDType_USBMouse:                   return QApplication::translate("UICommon", "USB Mouse", "PointingHIDType");
        case KPointingHIDType_USBTablet:                  return QApplication::translate("UICommon", "USB Tablet", "PointingHIDType");
        case KPointingHIDType_ComboMouse:                 return QApplication::translate("UICommon", "PS/2 and USB Mouse", "PointingHIDType");
        case KPointingHIDType_USBMultiTouch:              return QApplication::translate("UICommon", "USB Multi-Touch Tablet", "PointingHIDType");
        case KPointingHIDType_USBMultiTouchScreenPlusPad: return QApplication::translate("UICommon", "USB Multi-Touch TouchScreen and TouchPad", "PointingHIDType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KGraphicsControllerType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KGraphicsControllerType &type) const
{
    switch (type)
    {
        case KGraphicsControllerType_Null:      return QApplication::translate("UICommon", "None",      "GraphicsControllerType");
        case KGraphicsControllerType_VBoxVGA:   return QApplication::translate("UICommon", "VBoxVGA",   "GraphicsControllerType");
        case KGraphicsControllerType_VMSVGA:    return QApplication::translate("UICommon", "VMSVGA",    "GraphicsControllerType");
        case KGraphicsControllerType_VBoxSVGA:  return QApplication::translate("UICommon", "VBoxSVGA",  "GraphicsControllerType");
        case KGraphicsControllerType_QemuRamFB: return QApplication::translate("UICommon", "QemuRamFB", "GraphicsControllerType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* KGraphicsControllerType <= QString: */
template<> SHARED_LIBRARY_STUFF KGraphicsControllerType UIConverter::fromString<KGraphicsControllerType>(const QString &strType) const
{
    QHash<QString, KGraphicsControllerType> list;
    list.insert(QApplication::translate("UICommon", "None",      "GraphicsControllerType"), KGraphicsControllerType_Null);
    list.insert(QApplication::translate("UICommon", "VBoxVGA",   "GraphicsControllerType"), KGraphicsControllerType_VBoxVGA);
    list.insert(QApplication::translate("UICommon", "VMSVGA",    "GraphicsControllerType"), KGraphicsControllerType_VMSVGA);
    list.insert(QApplication::translate("UICommon", "VBoxSVGA",  "GraphicsControllerType"), KGraphicsControllerType_VBoxSVGA);
    list.insert(QApplication::translate("UICommon", "QemuRamFB", "GraphicsControllerType"), KGraphicsControllerType_QemuRamFB);
    if (!list.contains(strType))
    {
        AssertMsgFailed(("No value for '%s'", strType.toUtf8().constData()));
    }
    return list.value(strType, KGraphicsControllerType_Null);
}

/* QString <= KMediumType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KMediumType &type) const
{
    switch (type)
    {
        case KMediumType_Normal:       return QApplication::translate("UICommon", "Normal", "MediumType");
        case KMediumType_Immutable:    return QApplication::translate("UICommon", "Immutable", "MediumType");
        case KMediumType_Writethrough: return QApplication::translate("UICommon", "Writethrough", "MediumType");
        case KMediumType_Shareable:    return QApplication::translate("UICommon", "Shareable", "MediumType");
        case KMediumType_Readonly:     return QApplication::translate("UICommon", "Readonly", "MediumType");
        case KMediumType_MultiAttach:  return QApplication::translate("UICommon", "Multi-attach", "MediumType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KMediumVariant: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KMediumVariant &variant) const
{
    /* Note: KMediumVariant_Diff and KMediumVariant_Fixed are so far mutually exclusive: */
    switch ((int)variant)
    {
        case KMediumVariant_Standard:
            return QApplication::translate("UICommon", "Dynamically allocated storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_VdiZeroExpand):
            return QApplication::translate("UICommon", "New dynamically allocated storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_Diff):
            return QApplication::translate("UICommon", "Dynamically allocated differencing storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_Fixed):
            return QApplication::translate("UICommon", "Fixed size storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_VmdkSplit2G):
            return QApplication::translate("UICommon", "Dynamically allocated storage split into files of less than 2GB", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_VmdkSplit2G | KMediumVariant_Diff):
            return QApplication::translate("UICommon", "Dynamically allocated differencing storage split into files of less than 2GB", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_Fixed | KMediumVariant_VmdkSplit2G):
            return QApplication::translate("UICommon", "Fixed size storage split into files of less than 2GB", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_VmdkStreamOptimized):
            return QApplication::translate("UICommon", "Dynamically allocated compressed storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_VmdkStreamOptimized | KMediumVariant_Diff):
            return QApplication::translate("UICommon", "Dynamically allocated differencing compressed storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_Fixed | KMediumVariant_VmdkESX):
            return QApplication::translate("UICommon", "Fixed size ESX storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_Fixed | KMediumVariant_VmdkRawDisk):
            return QApplication::translate("UICommon", "Fixed size storage on raw disk", "MediumVariant");
        default:
            AssertMsgFailed(("No text for %d", variant)); break;
    }
    return QString();
}

/* QString <= KNetworkAttachmentType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KNetworkAttachmentType &type) const
{
    switch (type)
    {
        case KNetworkAttachmentType_Null:            return QApplication::translate("UICommon", "Not attached", "NetworkAttachmentType");
        case KNetworkAttachmentType_NAT:             return QApplication::translate("UICommon", "NAT", "NetworkAttachmentType");
        case KNetworkAttachmentType_Bridged:         return QApplication::translate("UICommon", "Bridged Adapter", "NetworkAttachmentType");
        case KNetworkAttachmentType_Internal:        return QApplication::translate("UICommon", "Internal Network", "NetworkAttachmentType");
#ifndef VBOX_WITH_VMNET
        case KNetworkAttachmentType_HostOnly:        return QApplication::translate("UICommon", "Host-only Adapter", "NetworkAttachmentType");
#else /* VBOX_WITH_VMNET */
        case KNetworkAttachmentType_HostOnly:        return QApplication::translate("UICommon", "Host-only Adapter [DEPRECATED]", "NetworkAttachmentType");
        case KNetworkAttachmentType_HostOnlyNetwork: return QApplication::translate("UICommon", "Host-only Network", "NetworkAttachmentType");
#endif /* VBOX_WITH_VMNET */
        case KNetworkAttachmentType_Generic:         return QApplication::translate("UICommon", "Generic Driver", "NetworkAttachmentType");
        case KNetworkAttachmentType_NATNetwork:      return QApplication::translate("UICommon", "NAT Network", "NetworkAttachmentType");
#ifdef VBOX_WITH_CLOUD_NET
        case KNetworkAttachmentType_Cloud:           return QApplication::translate("UICommon", "Cloud Network [EXPERIMENTAL]", "NetworkAttachmentType");
#endif /* VBOX_WITH_CLOUD_NET */
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KNetworkAdapterType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KNetworkAdapterType &type) const
{
    switch (type)
    {
        case KNetworkAdapterType_Am79C970A:  return QApplication::translate("UICommon", "PCnet-PCI II (Am79C970A)", "NetworkAdapterType");
        case KNetworkAdapterType_Am79C973:   return QApplication::translate("UICommon", "PCnet-FAST III (Am79C973)", "NetworkAdapterType");
        case KNetworkAdapterType_I82540EM:   return QApplication::translate("UICommon", "Intel PRO/1000 MT Desktop (82540EM)", "NetworkAdapterType");
        case KNetworkAdapterType_I82543GC:   return QApplication::translate("UICommon", "Intel PRO/1000 T Server (82543GC)", "NetworkAdapterType");
        case KNetworkAdapterType_I82545EM:   return QApplication::translate("UICommon", "Intel PRO/1000 MT Server (82545EM)", "NetworkAdapterType");
        case KNetworkAdapterType_Virtio:     return QApplication::translate("UICommon", "Paravirtualized Network (virtio-net)", "NetworkAdapterType");
        case KNetworkAdapterType_Am79C960:   return QApplication::translate("UICommon", "PCnet-ISA (Am79C960)", "NetworkAdapterType");
        case KNetworkAdapterType_NE2000:     return QApplication::translate("UICommon", "Novell NE2000 (NE2000)", "NetworkAdapterType");
        case KNetworkAdapterType_NE1000:     return QApplication::translate("UICommon", "Novell NE1000 (NE1000)", "NetworkAdapterType");
        case KNetworkAdapterType_WD8013:     return QApplication::translate("UICommon", "WD EtherCard Plus 16 (WD8013EBT)", "NetworkAdapterType");
        case KNetworkAdapterType_WD8003:     return QApplication::translate("UICommon", "WD EtherCard Plus (WD8013E)", "NetworkAdapterType");
        case KNetworkAdapterType_ELNK2:      return QApplication::translate("UICommon", "3Com EtherLink II (3C503)", "NetworkAdapterType");
        case KNetworkAdapterType_ELNK1:      return QApplication::translate("UICommon", "3Com EtherLink (3C501)", "NetworkAdapterType");
        case KNetworkAdapterType_UsbNet:     return QApplication::translate("UICommon", "Ethernet over USB (usbnet)", "NetworkAdapterType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KNetworkAdapterPromiscModePolicy: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KNetworkAdapterPromiscModePolicy &policy) const
{
    switch (policy)
    {
        case KNetworkAdapterPromiscModePolicy_Deny:
            return QApplication::translate("UICommon", "Deny", "NetworkAdapterPromiscModePolicy");
        case KNetworkAdapterPromiscModePolicy_AllowNetwork:
            return QApplication::translate("UICommon", "Allow VMs", "NetworkAdapterPromiscModePolicy");
        case KNetworkAdapterPromiscModePolicy_AllowAll:
            return QApplication::translate("UICommon", "Allow All", "NetworkAdapterPromiscModePolicy");
        default:
            AssertMsgFailed(("No text for %d", policy)); break;
    }
    return QString();
}

/* QString <= KPortMode: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KPortMode &mode) const
{
    switch (mode)
    {
        case KPortMode_Disconnected: return QApplication::translate("UICommon", "Disconnected", "PortMode");
        case KPortMode_HostPipe:     return QApplication::translate("UICommon", "Host Pipe", "PortMode");
        case KPortMode_HostDevice:   return QApplication::translate("UICommon", "Host Device", "PortMode");
        case KPortMode_RawFile:      return QApplication::translate("UICommon", "Raw File", "PortMode");
        case KPortMode_TCP:          return QApplication::translate("UICommon", "TCP", "PortMode");
        default: AssertMsgFailed(("No text for %d", mode)); break;
    }
    return QString();
}

/* KPortMode <= QString: */
template<> SHARED_LIBRARY_STUFF KPortMode UIConverter::fromString<KPortMode>(const QString &strMode) const
{
    QHash<QString, KPortMode> list;
    list.insert(QApplication::translate("UICommon", "Disconnected", "PortMode"), KPortMode_Disconnected);
    list.insert(QApplication::translate("UICommon", "Host Pipe", "PortMode"),    KPortMode_HostPipe);
    list.insert(QApplication::translate("UICommon", "Host Device", "PortMode"),  KPortMode_HostDevice);
    list.insert(QApplication::translate("UICommon", "Raw File", "PortMode"),     KPortMode_RawFile);
    list.insert(QApplication::translate("UICommon", "TCP", "PortMode"),          KPortMode_TCP);
    if (!list.contains(strMode))
    {
        AssertMsgFailed(("No value for '%s'", strMode.toUtf8().constData()));
    }
    return list.value(strMode, KPortMode_Disconnected);
}

/* QString <= KUSBControllerType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KUSBControllerType &type) const
{
    switch (type)
    {
        case KUSBControllerType_OHCI: return QApplication::translate("UICommon", "OHCI", "USBControllerType");
        case KUSBControllerType_EHCI: return QApplication::translate("UICommon", "EHCI", "USBControllerType");
        case KUSBControllerType_XHCI: return QApplication::translate("UICommon", "xHCI", "USBControllerType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KUSBDeviceState: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KUSBDeviceState &state) const
{
    switch (state)
    {
        case KUSBDeviceState_NotSupported: return QApplication::translate("UICommon", "Not supported", "USBDeviceState");
        case KUSBDeviceState_Unavailable:  return QApplication::translate("UICommon", "Unavailable", "USBDeviceState");
        case KUSBDeviceState_Busy:         return QApplication::translate("UICommon", "Busy", "USBDeviceState");
        case KUSBDeviceState_Available:    return QApplication::translate("UICommon", "Available", "USBDeviceState");
        case KUSBDeviceState_Held:         return QApplication::translate("UICommon", "Held", "USBDeviceState");
        case KUSBDeviceState_Captured:     return QApplication::translate("UICommon", "Captured", "USBDeviceState");
        default: AssertMsgFailed(("No text for %d", state)); break;
    }
    return QString();
}

/* QString <= KUSBDeviceFilterAction: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KUSBDeviceFilterAction &action) const
{
    switch (action)
    {
        case KUSBDeviceFilterAction_Ignore: return QApplication::translate("UICommon", "Ignore", "USBDeviceFilterAction");
        case KUSBDeviceFilterAction_Hold:   return QApplication::translate("UICommon", "Hold", "USBDeviceFilterAction");
        default: AssertMsgFailed(("No text for %d", action)); break;
    }
    return QString();
}

/* KUSBDeviceFilterAction <= QString: */
template<> SHARED_LIBRARY_STUFF KUSBDeviceFilterAction UIConverter::fromString<KUSBDeviceFilterAction>(const QString &strAction) const
{
    QHash<QString, KUSBDeviceFilterAction> list;
    list.insert(QApplication::translate("UICommon", "Ignore", "USBDeviceFilterAction"), KUSBDeviceFilterAction_Ignore);
    list.insert(QApplication::translate("UICommon", "Hold", "USBDeviceFilterAction"),   KUSBDeviceFilterAction_Hold);
    if (!list.contains(strAction))
    {
        AssertMsgFailed(("No value for '%s'", strAction.toUtf8().constData()));
    }
    return list.value(strAction, KUSBDeviceFilterAction_Null);
}

/* QString <= KAudioDriverType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KAudioDriverType &type) const
{
    switch (type)
    {
        case KAudioDriverType_Default:     return QApplication::translate("UICommon", "Default", "AudioDriverType");
        case KAudioDriverType_Null:        return QApplication::translate("UICommon", "Null Audio", "AudioDriverType");
        case KAudioDriverType_OSS:         return QApplication::translate("UICommon", "OSS Audio", "AudioDriverType");
        case KAudioDriverType_ALSA:        return QApplication::translate("UICommon", "ALSA Audio", "AudioDriverType");
        case KAudioDriverType_Pulse:       return QApplication::translate("UICommon", "PulseAudio", "AudioDriverType");
        /* Deprecated; not (ever) supported; leave this in for backwards compatibility. See @bugref{10845} */
        case KAudioDriverType_WinMM:       return QApplication::translate("UICommon", "Windows Multimedia", "AudioDriverType");
        case KAudioDriverType_DirectSound: return QApplication::translate("UICommon", "Windows DirectSound", "AudioDriverType");
        case KAudioDriverType_WAS:         return QApplication::translate("UICommon", "Windows Audio Session", "AudioDriverType");
        case KAudioDriverType_CoreAudio:   return QApplication::translate("UICommon", "Core Audio", "AudioDriverType");
        // case KAudioDriverType_MMPM:
        case KAudioDriverType_SolAudio:    return QApplication::translate("UICommon", "Solaris Audio", "AudioDriverType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* KAudioDriverType <= QString: */
template<> SHARED_LIBRARY_STUFF KAudioDriverType UIConverter::fromString<KAudioDriverType>(const QString &strType) const
{
    QHash<QString, KAudioDriverType> list;
    list.insert(QApplication::translate("UICommon", "Default", "AudioDriverType"),              KAudioDriverType_Default);
    list.insert(QApplication::translate("UICommon", "Null Audio", "AudioDriverType"),            KAudioDriverType_Null);
    list.insert(QApplication::translate("UICommon", "OSS Audio", "AudioDriverType"),             KAudioDriverType_OSS);
    list.insert(QApplication::translate("UICommon", "ALSA Audio", "AudioDriverType"),            KAudioDriverType_ALSA);
    list.insert(QApplication::translate("UICommon", "PulseAudio", "AudioDriverType"),            KAudioDriverType_Pulse);
    /* Deprecated; not (ever) supported; leave this in for backwards compatibility. See @bugref{10845} */
    list.insert(QApplication::translate("UICommon", "Windows Multimedia", "AudioDriverType"),    KAudioDriverType_WinMM);
    list.insert(QApplication::translate("UICommon", "Windows DirectSound", "AudioDriverType"),   KAudioDriverType_DirectSound);
    list.insert(QApplication::translate("UICommon", "Windows Audio Session", "AudioDriverType"), KAudioDriverType_WAS);
    list.insert(QApplication::translate("UICommon", "Core Audio", "AudioDriverType"),            KAudioDriverType_CoreAudio);
    // list.insert(..., KAudioDriverType_MMPM);
    list.insert(QApplication::translate("UICommon", "Solaris Audio", "AudioDriverType"),         KAudioDriverType_SolAudio);
    if (!list.contains(strType))
    {
        AssertMsgFailed(("No value for '%s'", strType.toUtf8().constData()));
    }
    return list.value(strType, KAudioDriverType_Null);
}

/* QString <= KAudioControllerType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KAudioControllerType &type) const
{
    switch (type)
    {
        case KAudioControllerType_AC97:        return QApplication::translate("UICommon", "ICH AC97", "AudioControllerType");
        case KAudioControllerType_SB16:        return QApplication::translate("UICommon", "SoundBlaster 16", "AudioControllerType");
        case KAudioControllerType_HDA:         return QApplication::translate("UICommon", "Intel HD Audio", "AudioControllerType");
        case KAudioControllerType_VirtioSound: return QApplication::translate("UICommon", "Virtio-Sound", "AudioControllerType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* KAudioControllerType <= QString: */
template<> SHARED_LIBRARY_STUFF KAudioControllerType UIConverter::fromString<KAudioControllerType>(const QString &strType) const
{
    QHash<QString, KAudioControllerType> list;
    list.insert(QApplication::translate("UICommon", "ICH AC97", "AudioControllerType"),        KAudioControllerType_AC97);
    list.insert(QApplication::translate("UICommon", "SoundBlaster 16", "AudioControllerType"), KAudioControllerType_SB16);
    list.insert(QApplication::translate("UICommon", "Intel HD Audio", "AudioControllerType"),  KAudioControllerType_HDA);
    list.insert(QApplication::translate("UICommon", "Virtio-Sound", "AudioControllerType"),    KAudioControllerType_VirtioSound);
    if (!list.contains(strType))
    {
        AssertMsgFailed(("No value for '%s'", strType.toUtf8().constData()));
    }
    return list.value(strType, KAudioControllerType_AC97);
}

/* QString <= KAuthType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KAuthType &type) const
{
    switch (type)
    {
        case KAuthType_Null:     return QApplication::translate("UICommon", "Null", "AuthType");
        case KAuthType_External: return QApplication::translate("UICommon", "External", "AuthType");
        case KAuthType_Guest:    return QApplication::translate("UICommon", "Guest", "AuthType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* KAuthType <= QString: */
template<> SHARED_LIBRARY_STUFF KAuthType UIConverter::fromString<KAuthType>(const QString &strType) const
{
    QHash<QString, KAuthType> list;
    list.insert(QApplication::translate("UICommon", "Null", "AuthType"),     KAuthType_Null);
    list.insert(QApplication::translate("UICommon", "External", "AuthType"), KAuthType_External);
    list.insert(QApplication::translate("UICommon", "Guest", "AuthType"),    KAuthType_Guest);
    if (!list.contains(strType))
    {
        AssertMsgFailed(("No value for '%s'", strType.toUtf8().constData()));
    }
    return list.value(strType, KAuthType_Null);
}

/* QString <= KStorageBus: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KStorageBus &bus) const
{
    switch (bus)
    {
        case KStorageBus_IDE:        return QApplication::translate("UICommon", "IDE", "StorageBus");
        case KStorageBus_SATA:       return QApplication::translate("UICommon", "SATA", "StorageBus");
        case KStorageBus_SCSI:       return QApplication::translate("UICommon", "SCSI", "StorageBus");
        case KStorageBus_Floppy:     return QApplication::translate("UICommon", "Floppy", "StorageBus");
        case KStorageBus_SAS:        return QApplication::translate("UICommon", "SAS", "StorageBus");
        case KStorageBus_USB:        return QApplication::translate("UICommon", "USB", "StorageBus");
        case KStorageBus_PCIe:       return QApplication::translate("UICommon", "PCIe", "StorageBus");
        case KStorageBus_VirtioSCSI: return QApplication::translate("UICommon", "virtio-scsi", "StorageBus");
        default:                     AssertMsgFailed(("No text for %d", bus)); break;
    }
    return QString();
}

/* KStorageBus <= QString: */
template<> SHARED_LIBRARY_STUFF KStorageBus UIConverter::fromString<KStorageBus>(const QString &strType) const
{
    QHash<QString, KStorageBus> list;
    list.insert(QApplication::translate("UICommon", "IDE", "StorageBus"),         KStorageBus_IDE);
    list.insert(QApplication::translate("UICommon", "SATA", "StorageBus"),        KStorageBus_SATA);
    list.insert(QApplication::translate("UICommon", "SCSI", "StorageBus"),        KStorageBus_SCSI);
    list.insert(QApplication::translate("UICommon", "Floppy", "StorageBus"),      KStorageBus_Floppy);
    list.insert(QApplication::translate("UICommon", "SAS", "StorageBus"),         KStorageBus_SAS);
    list.insert(QApplication::translate("UICommon", "USB", "StorageBus"),         KStorageBus_USB);
    list.insert(QApplication::translate("UICommon", "PCIe", "StorageBus"),        KStorageBus_PCIe);
    list.insert(QApplication::translate("UICommon", "virtio-scsi", "StorageBus"), KStorageBus_VirtioSCSI);
    if (!list.contains(strType))
    {
        AssertMsgFailed(("No value for '%s'", strType.toUtf8().constData()));
    }
    return list.value(strType, KStorageBus_Null);
}

/* QString <= KStorageControllerType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KStorageControllerType &type) const
{
    switch (type)
    {
        case KStorageControllerType_LsiLogic:    return QApplication::translate("UICommon", "LsiLogic", "StorageControllerType");
        case KStorageControllerType_BusLogic:    return QApplication::translate("UICommon", "BusLogic", "StorageControllerType");
        case KStorageControllerType_IntelAhci:   return QApplication::translate("UICommon", "AHCI", "StorageControllerType");
        case KStorageControllerType_PIIX3:       return QApplication::translate("UICommon", "PIIX3", "StorageControllerType");
        case KStorageControllerType_PIIX4:       return QApplication::translate("UICommon", "PIIX4", "StorageControllerType");
        case KStorageControllerType_ICH6:        return QApplication::translate("UICommon", "ICH6", "StorageControllerType");
        case KStorageControllerType_I82078:      return QApplication::translate("UICommon", "I82078", "StorageControllerType");
        case KStorageControllerType_LsiLogicSas: return QApplication::translate("UICommon", "LsiLogic SAS", "StorageControllerType");
        case KStorageControllerType_USB:         return QApplication::translate("UICommon", "USB", "StorageControllerType");
        case KStorageControllerType_NVMe:        return QApplication::translate("UICommon", "NVMe", "StorageControllerType");
        case KStorageControllerType_VirtioSCSI:  return QApplication::translate("UICommon", "virtio-scsi", "StorageControllerType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* KStorageControllerType <= QString: */
template<> SHARED_LIBRARY_STUFF KStorageControllerType UIConverter::fromString<KStorageControllerType>(const QString &strType) const
{
    QHash<QString, KStorageControllerType> list;
    list.insert(QApplication::translate("UICommon", "LsiLogic", "StorageControllerType"),     KStorageControllerType_LsiLogic);
    list.insert(QApplication::translate("UICommon", "BusLogic", "StorageControllerType"),     KStorageControllerType_BusLogic);
    list.insert(QApplication::translate("UICommon", "AHCI", "StorageControllerType"),         KStorageControllerType_IntelAhci);
    list.insert(QApplication::translate("UICommon", "PIIX3", "StorageControllerType"),        KStorageControllerType_PIIX3);
    list.insert(QApplication::translate("UICommon", "PIIX4", "StorageControllerType"),        KStorageControllerType_PIIX4);
    list.insert(QApplication::translate("UICommon", "ICH6", "StorageControllerType"),         KStorageControllerType_ICH6);
    list.insert(QApplication::translate("UICommon", "I82078", "StorageControllerType"),       KStorageControllerType_I82078);
    list.insert(QApplication::translate("UICommon", "LsiLogic SAS", "StorageControllerType"), KStorageControllerType_LsiLogicSas);
    list.insert(QApplication::translate("UICommon", "USB", "StorageControllerType"),          KStorageControllerType_USB);
    list.insert(QApplication::translate("UICommon", "NVMe", "StorageControllerType"),         KStorageControllerType_NVMe);
    list.insert(QApplication::translate("UICommon", "virtio-scsi", "StorageControllerType"),  KStorageControllerType_VirtioSCSI);
    if (!list.contains(strType))
    {
        AssertMsgFailed(("No value for '%s'", strType.toUtf8().constData()));
    }
    return list.value(strType, KStorageControllerType_Null);
}

/* QString <= KChipsetType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KChipsetType &type) const
{
    switch (type)
    {
        case KChipsetType_PIIX3:        return QApplication::translate("UICommon", "PIIX3", "ChipsetType");
        case KChipsetType_ICH9:         return QApplication::translate("UICommon", "ICH9", "ChipsetType");
        case KChipsetType_ARMv8Virtual: return QApplication::translate("UICommon", "ARMv8Virtual", "ChipsetType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KTpmType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KTpmType &type) const
{
    switch (type)
    {
        case KTpmType_None:  return QApplication::translate("UICommon", "None", "TpmType");
        case KTpmType_v1_2:  return QApplication::translate("UICommon", "1.2", "TpmType");
        case KTpmType_v2_0:  return QApplication::translate("UICommon", "2.0", "TpmType");
        case KTpmType_Host:  return QApplication::translate("UICommon", "Host", "TpmType");
        case KTpmType_Swtpm: return QApplication::translate("UICommon", "SWTPM", "TpmType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KNATProtocol: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KNATProtocol &protocol) const
{
    switch (protocol)
    {
        case KNATProtocol_UDP: return QApplication::translate("UICommon", "UDP", "NATProtocol");
        case KNATProtocol_TCP: return QApplication::translate("UICommon", "TCP", "NATProtocol");
        default: AssertMsgFailed(("No text for %d", protocol)); break;
    }
    return QString();
}

/* QString <= KNATProtocol: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const KNATProtocol &protocol) const
{
    QString strResult;
    switch (protocol)
    {
        case KNATProtocol_UDP: strResult = "udp"; break;
        case KNATProtocol_TCP: strResult = "tcp"; break;
        default: AssertMsgFailed(("No text for protocol type=%d", protocol)); break;
    }
    return strResult;
}

/* KNATProtocol <= QString: */
template<> SHARED_LIBRARY_STUFF KNATProtocol UIConverter::fromInternalString<KNATProtocol>(const QString &strProtocol) const
{
    if (strProtocol.compare("udp", Qt::CaseInsensitive) == 0)
        return KNATProtocol_UDP;
    if (strProtocol.compare("tcp", Qt::CaseInsensitive) == 0)
        return KNATProtocol_TCP;
    AssertMsgFailedReturn(("No value for '%s'", strProtocol.toUtf8().constData()), KNATProtocol_UDP);
}

/* QString <= KGuestSessionStatus: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KGuestSessionStatus &status) const
{
    switch (status)
    {
        case KGuestSessionStatus_Undefined:          return QApplication::translate("UICommon", "Undefined", "GuestSessionStatus");
        case KGuestSessionStatus_Starting:           return QApplication::translate("UICommon", "Starting", "GuestSessionStatus");
        case KGuestSessionStatus_Started:            return QApplication::translate("UICommon", "Started", "GuestSessionStatus");
        case KGuestSessionStatus_Terminating:        return QApplication::translate("UICommon", "Terminating", "GuestSessionStatus");
        case KGuestSessionStatus_Terminated:         return QApplication::translate("UICommon", "Terminated", "GuestSessionStatus");
        case KGuestSessionStatus_TimedOutKilled:     return QApplication::translate("UICommon", "Timed Out (Killed)", "GuestSessionStatus");
        case KGuestSessionStatus_TimedOutAbnormally: return QApplication::translate("UICommon", "Timed Out (Abnormally)", "GuestSessionStatus");
        case KGuestSessionStatus_Down:               return QApplication::translate("UICommon", "Down", "GuestSessionStatus");
        case KGuestSessionStatus_Error:              return QApplication::translate("UICommon", "Error", "GuestSessionStatus");
        default: AssertMsgFailed(("No text for %d", status)); break;
    }
    return QString();
}

/* KGuestSessionStatus <= QString: */
template<> SHARED_LIBRARY_STUFF KGuestSessionStatus UIConverter::fromString<KGuestSessionStatus>(const QString &strStatus) const
{
    QHash<QString, KGuestSessionStatus> list;
    list.insert(QApplication::translate("UICommon", "Undefined", "GuestSessionStatus"),              KGuestSessionStatus_Undefined);
    list.insert(QApplication::translate("UICommon", "Starting", "GuestSessionStatus"),               KGuestSessionStatus_Starting);
    list.insert(QApplication::translate("UICommon", "Started", "GuestSessionStatus"),                KGuestSessionStatus_Started);
    list.insert(QApplication::translate("UICommon", "Terminating", "GuestSessionStatus"),            KGuestSessionStatus_Terminating);
    list.insert(QApplication::translate("UICommon", "Terminated", "GuestSessionStatus"),             KGuestSessionStatus_Terminated);
    list.insert(QApplication::translate("UICommon", "Timed Out (Killed)", "GuestSessionStatus"),     KGuestSessionStatus_TimedOutKilled);
    list.insert(QApplication::translate("UICommon", "Timed Out (Abnormally)", "GuestSessionStatus"), KGuestSessionStatus_TimedOutAbnormally);
    list.insert(QApplication::translate("UICommon", "Down", "GuestSessionStatus"),                   KGuestSessionStatus_Down);
    list.insert(QApplication::translate("UICommon", "Error", "GuestSessionStatus"),                  KGuestSessionStatus_Error);
    if (!list.contains(strStatus))
    {
        AssertMsgFailed(("No value for '%s'", strStatus.toUtf8().constData()));
    }
    return list.value(strStatus, KGuestSessionStatus_Undefined);
}

/* QString <= KProcessStatus: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toString(const KProcessStatus &status) const
{
    switch (status)
    {
        case KProcessStatus_Undefined:            return QApplication::translate("UICommon", "Undefined", "ProcessStatus");
        case KProcessStatus_Starting:             return QApplication::translate("UICommon", "Starting", "ProcessStatus");
        case KProcessStatus_Started:              return QApplication::translate("UICommon", "Started", "ProcessStatus");
        case KProcessStatus_Paused:               return QApplication::translate("UICommon", "Paused", "ProcessStatus");
        case KProcessStatus_Terminating:          return QApplication::translate("UICommon", "Terminating", "ProcessStatus");
        case KProcessStatus_TerminatedNormally:   return QApplication::translate("UICommon", "Terminated (Normally)", "ProcessStatus");
        case KProcessStatus_TerminatedSignal:     return QApplication::translate("UICommon", "Terminated (Signal)", "ProcessStatus");
        case KProcessStatus_TerminatedAbnormally: return QApplication::translate("UICommon", "Terminated (Abnormally)", "ProcessStatus");
        case KProcessStatus_TimedOutKilled:       return QApplication::translate("UICommon", "Timed Out (Killed)", "ProcessStatus");
        case KProcessStatus_TimedOutAbnormally:   return QApplication::translate("UICommon", "Timed Out (Abnormally)", "ProcessStatus");
        case KProcessStatus_Down:                 return QApplication::translate("UICommon", "Down", "ProcessStatus");
        case KProcessStatus_Error:                return QApplication::translate("UICommon", "Error", "ProcessStatus");
        default: AssertMsgFailed(("No text for %d", status)); break;
    }
    return QString();
}

/* KProcessStatus <= QString: */
template<> SHARED_LIBRARY_STUFF KProcessStatus UIConverter::fromString<KProcessStatus>(const QString &strStatus) const
{
    QHash<QString, KProcessStatus> list;
    list.insert(QApplication::translate("UICommon", "Undefined", "ProcessStatus"),               KProcessStatus_Undefined);
    list.insert(QApplication::translate("UICommon", "Starting", "ProcessStatus"),                KProcessStatus_Starting);
    list.insert(QApplication::translate("UICommon", "Started", "ProcessStatus"),                 KProcessStatus_Started);
    list.insert(QApplication::translate("UICommon", "Paused", "ProcessStatus"),                  KProcessStatus_Paused);
    list.insert(QApplication::translate("UICommon", "Terminating", "ProcessStatus"),             KProcessStatus_Terminating);
    list.insert(QApplication::translate("UICommon", "Terminated (Normally)", "ProcessStatus"),   KProcessStatus_TerminatedNormally);
    list.insert(QApplication::translate("UICommon", "Terminated (Signal)", "ProcessStatus"),     KProcessStatus_TerminatedSignal);
    list.insert(QApplication::translate("UICommon", "Terminated (Abnormally)", "ProcessStatus"), KProcessStatus_TerminatedAbnormally);
    list.insert(QApplication::translate("UICommon", "Timed Out (Killed)", "ProcessStatus"),      KProcessStatus_TimedOutKilled);
    list.insert(QApplication::translate("UICommon", "Timed Out (Abnormally)", "ProcessStatus"),  KProcessStatus_TimedOutAbnormally);
    list.insert(QApplication::translate("UICommon", "Down", "ProcessStatus"),                    KProcessStatus_Down);
    list.insert(QApplication::translate("UICommon", "Error", "ProcessStatus"),                   KProcessStatus_Error);
    if (!list.contains(strStatus))
    {
        AssertMsgFailed(("No value for '%s'", strStatus.toUtf8().constData()));
    }
    return list.value(strStatus, KProcessStatus_Undefined);
}

/* QString <= KMetricType: */
template<> SHARED_LIBRARY_STUFF QString UIConverter::toInternalString(const KMetricType &metricType) const
{
    QString strResult;
    switch (metricType)
    {
        case KMetricType_Invalid:           strResult = "Invalid"; break;
        case KMetricType_CpuUtilization:    strResult = "CpuUtilization"; break;
        case KMetricType_MemoryUtilization: strResult = "MemoryUtilization"; break;
        case KMetricType_DiskBytesRead:     strResult = "DiskBytesRead"; break;
        case KMetricType_DiskBytesWritten:  strResult = "DiskBytesWritten"; break;
        case KMetricType_NetworksBytesIn:   strResult = "NetworksBytesIn"; break;
        case KMetricType_NetworksBytesOut:  strResult = "NetworksBytesOut"; break;
        default: AssertMsgFailed(("No text for metric type=%d", metricType)); break;
    }
    return strResult;
}

/* KMetricType <= QString: */
template<> SHARED_LIBRARY_STUFF KMetricType UIConverter::fromInternalString<KMetricType>(const QString &strMetricType) const
{
    if (strMetricType.compare("Invalid", Qt::CaseInsensitive) == 0)
        return KMetricType_Invalid;
    if (strMetricType.compare("CpuUtilization", Qt::CaseInsensitive) == 0)
        return KMetricType_CpuUtilization;
    if (strMetricType.compare("MemoryUtilization", Qt::CaseInsensitive) == 0)
        return KMetricType_MemoryUtilization;
    if (strMetricType.compare("DiskBytesRead", Qt::CaseInsensitive) == 0)
        return KMetricType_DiskBytesRead;
    if (strMetricType.compare("DiskBytesWritten", Qt::CaseInsensitive) == 0)
        return KMetricType_DiskBytesWritten;
    if (strMetricType.compare("NetworksBytesIn", Qt::CaseInsensitive) == 0)
        return KMetricType_NetworksBytesIn;
    if (strMetricType.compare("NetworksBytesOut", Qt::CaseInsensitive) == 0)
        return KMetricType_NetworksBytesOut;
    AssertMsgFailedReturn(("No value for '%s'", strMetricType.toUtf8().constData()), KMetricType_Invalid);
}
