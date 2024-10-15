/* $Id: UIDetailsGenerator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsGenerator implementation.
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
#include <QDir>
#include <QRegularExpression>

/* GUI includes: */
#include "UIBootOrderEditor.h"
#include "UIConverter.h"
#include "UIDetailsGenerator.h"
#include "UIErrorString.h"
#include "UIGlobalSession.h"
#include "UIGuestOSType.h"
#include "UIMedium.h"
#include "UIMediumTools.h"
#include "UITranslator.h"
#include "UIUSBTools.h"

/* COM includes: */
#include "CAudioAdapter.h"
#include "CAudioSettings.h"
#include "CBooleanFormValue.h"
#include "CChoiceFormValue.h"
#include "CCloudMachine.h"
#include "CConsole.h"
#include "CFirmwareSettings.h"
#include "CForm.h"
#include "CFormValue.h"
#include "CGraphicsAdapter.h"
#include "CGuest.h"
#include "CMachine.h"
#include "CMediumAttachment.h"
#include "CNetworkAdapter.h"
#include "CNvramStore.h"
#include "CPlatform.h"
#include "CPlatformX86.h"
#include "CPlatformProperties.h"
#include "CProgress.h"
#include "CRangedIntegerFormValue.h"
#include "CRangedInteger64FormValue.h"
#include "CRecordingScreenSettings.h"
#include "CRecordingSettings.h"
#include "CSerialPort.h"
#include "CSharedFolder.h"
#include "CStorageController.h"
#include "CStringFormValue.h"
#include "CTrustedPlatformModule.h"
#include "CUefiVariableStore.h"
#include "CUSBController.h"
#include "CUSBDevice.h"
#include "CUSBDeviceFilter.h"
#include "CUSBDeviceFilters.h"
#include "CVRDEServer.h"

/* VirtualBox interface declarations: */
#include <iprt/time.h>
#include <VBox/com/VirtualBox.h>


const QString UIDetailsGenerator::e_strTableRow1 = QString("<tr><td colspan='2'><nobr><b>%1</b></nobr></td></tr>");
const QString UIDetailsGenerator::e_strTableRow2 = QString("<tr><td><nobr>%1:</nobr></td><td><nobr>%2</nobr></td></tr>");
const QString UIDetailsGenerator::e_strTableRow3 = QString("<tr><td><nobr>&nbsp;%1:</nobr></td><td><nobr>%2</nobr></td></tr>");


UITextTable UIDetailsGenerator::generateMachineInformationGeneral(CMachine &comMachine,
                                                                  const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral &fOptions)
{
    UITextTable table;

    if (comMachine.isNull())
        return table;

    if (!comMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    /* Name: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Name)
    {
        /* Configure hovering anchor: */
        const QString strAnchorType = QString("machine_name");
        const QString strName = comMachine.GetName();
        table << UITextTableLine(QApplication::translate("UIDetails", "Name", "details (general)"),
                                 QString("<a href=#%1,%2>%2</a>")
                                     .arg(strAnchorType,
                                          strName));
    }

    /* Operating system: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_OS)
    {
        /* Configure hovering anchor: */
        const QString strAnchorType = QString("os_type");
        const QString strOsTypeId = comMachine.GetOSTypeId();
        table << UITextTableLine(QApplication::translate("UIDetails", "Operating System", "details (general)"),
                                 QString("<a href=#%1,%2>%3</a>")
                                     .arg(strAnchorType,
                                          strOsTypeId,
                                          gpGlobalSession->guestOSTypeManager().getDescription(strOsTypeId)));
    }

    /* Settings file location: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Location)
    {
        /* Configure hovering anchor: */
        const QString strAnchorType = QString("machine_location");
        const QString strMachineLocation = comMachine.GetSettingsFilePath();
        table << UITextTableLine(QApplication::translate("UIDetails", "Settings File Location", "details (general)"),
                                 QString("<a href=#%1,%2>%3</a>")
                                     .arg(strAnchorType,
                                          strMachineLocation,
                                          QDir::toNativeSeparators(QFileInfo(strMachineLocation).absolutePath())));
    }

    /* Groups: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Groups)
    {
        QStringList groups = comMachine.GetGroups().toList();
        /* Do not show groups for machine which is in root group only: */
        if (groups.size() == 1)
            groups.removeAll("/");
        /* If group list still not empty: */
        if (!groups.isEmpty())
        {
            /* For every group: */
            for (int i = 0; i < groups.size(); ++i)
            {
                /* Trim first '/' symbol: */
                QString &strGroup = groups[i];
                if (strGroup.startsWith("/") && strGroup != "/")
                    strGroup.remove(0, 1);
            }
            table << UITextTableLine(QApplication::translate("UIDetails", "Groups", "details (general)"), groups.join(", "));
        }
    }

    return table;
}

UITextTable UIDetailsGenerator::generateMachineInformationGeneral(CCloudMachine &comCloudMachine,
                                                                  const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral &)
{
    UITextTable table;

    if (comCloudMachine.isNull())
        return table;

    if (!comCloudMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    /* Acquire details form: */
    CForm comForm = comCloudMachine.GetDetailsForm();
    /* Ignore cloud machine errors: */
    if (comCloudMachine.isOk())
    {
        /* Common anchor for all fields: */
        const QString strAnchorType = "cloud";

        /* For each form value: */
        const QVector<CFormValue> values = comForm.GetValues();
        foreach (const CFormValue &comIteratedValue, values)
        {
            /* Ignore invisible values: */
            if (!comIteratedValue.GetVisible())
                continue;

            /* Acquire label: */
            const QString strLabel = comIteratedValue.GetLabel();
            /* Generate value: */
            const QString strValue = generateFormValueInformation(comIteratedValue);

            /* Generate table string: */
            table << UITextTableLine(strLabel, QString("<a href=#%1,%2>%3</a>").arg(strAnchorType, strLabel, strValue));
        }
    }

    return table;
}

QString UIDetailsGenerator::generateFormValueInformation(const CFormValue &comFormValue, bool fFull /* = false */)
{
    /* Handle possible form value types: */
    QString strResult;
    switch (comFormValue.GetType())
    {
        case KFormValueType_Boolean:
        {
            CBooleanFormValue comValue(comFormValue);
            const bool fBool = comValue.GetSelected();
            strResult = fBool ? QApplication::translate("UIDetails", "Enabled", "details (cloud value)")
                              : QApplication::translate("UIDetails", "Disabled", "details (cloud value)");
            break;
        }
        case KFormValueType_String:
        {
            CStringFormValue comValue(comFormValue);
            const QString strValue = comValue.GetString();
            const QString strClipboardValue = comValue.GetClipboardString();
            strResult = fFull && !strClipboardValue.isEmpty() ? strClipboardValue : strValue;
            break;
        }
        case KFormValueType_Choice:
        {
            AssertMsgFailed(("Aren't we decided to convert all choices to strings?\n"));
            CChoiceFormValue comValue(comFormValue);
            const QVector<QString> possibleValues = comValue.GetValues();
            const int iCurrentIndex = comValue.GetSelectedIndex();
            strResult = possibleValues.value(iCurrentIndex);
            break;
        }
        case KFormValueType_RangedInteger:
        {
            CRangedIntegerFormValue comValue(comFormValue);
            strResult = QString("%1 %2")
                            .arg(comValue.GetInteger())
                            .arg(QApplication::translate("UITranslator", comValue.GetSuffix().toUtf8().constData()));
            break;
        }
        case KFormValueType_RangedInteger64:
        {
            CRangedInteger64FormValue comValue(comFormValue);
            strResult = QString("%1 %2")
                            .arg(comValue.GetInteger())
                            .arg(QApplication::translate("UITranslator", comValue.GetSuffix().toUtf8().constData()));
            break;
        }
        default:
            break;
    }
    /* Return result: */
    return strResult;
}

UITextTable UIDetailsGenerator::generateMachineInformationSystem(CMachine &comMachine,
                                                                 const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem &fOptions)
{
    UITextTable table;

    if (comMachine.isNull())
        return table;

    if (!comMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    /* Base memory: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_RAM)
    {
        /* Configure hovering anchor: */
        const QString strAnchorType = QString("base_memory");
        const int iBaseMemory = comMachine.GetMemorySize();
        table << UITextTableLine(QApplication::translate("UIDetails", "Base Memory", "details (system)"),
                                 QString("<a href=#%1,%2>%3</a>")
                                     .arg(strAnchorType)
                                     .arg(iBaseMemory)
                                     .arg(QApplication::translate("UIDetails", "%1 MB").arg(iBaseMemory)));
    }

    /* Processors: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_CPUCount)
    {
        const int cCPU = comMachine.GetCPUCount();
        if (cCPU > 1)
            table << UITextTableLine(QApplication::translate("UIDetails", "Processors", "details (system)"),
                                     QString::number(cCPU));
    }

    /* Execution cap: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_CPUExecutionCap)
    {
        const int iCPUExecutionCap = comMachine.GetCPUExecutionCap();
        if (iCPUExecutionCap < 100)
            table << UITextTableLine(QApplication::translate("UIDetails", "Execution Cap", "details (system)"),
                                     QApplication::translate("UIDetails", "%1%", "details").arg(iCPUExecutionCap));
    }

    /* Boot order: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_BootOrder)
    {
        /* Configure hovering anchor: */
        const QString strAnchorType = QString("boot_order");
        const UIBootItemDataList bootItems = loadBootItems(comMachine);
        table << UITextTableLine(QApplication::translate("UIDetails", "Boot Order", "details (system)"),
                                 QString("<a href=#%1,%2>%3</a>")
                                     .arg(strAnchorType,
                                          bootItemsToSerializedString(bootItems),
                                          bootItemsToReadableString(bootItems)));
    }

    /* Chipset type: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_ChipsetType)
    {
        CPlatform comPlatform = comMachine.GetPlatform();
        const KChipsetType enmChipsetType = comPlatform.GetChipsetType();
        if (enmChipsetType == KChipsetType_ICH9)
            table << UITextTableLine(QApplication::translate("UIDetails", "Chipset Type", "details (system)"),
                                     gpConverter->toString(enmChipsetType));
    }

    /* TPM type: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_TpmType)
    {
        CTrustedPlatformModule comModule = comMachine.GetTrustedPlatformModule();
        const KTpmType enmTpmType = comModule.GetType();
        if (enmTpmType != KTpmType_None)
            table << UITextTableLine(QApplication::translate("UIDetails", "TPM Type", "details (system)"),
                                     gpConverter->toString(enmTpmType));
    }

    /* EFI: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Firmware)
    {
        CFirmwareSettings comFirmwareSettings = comMachine.GetFirmwareSettings();
        switch (comFirmwareSettings.GetFirmwareType())
        {
            case KFirmwareType_EFI:
            case KFirmwareType_EFI32:
            case KFirmwareType_EFI64:
            case KFirmwareType_EFIDUAL:
            {
                table << UITextTableLine(QApplication::translate("UIDetails", "EFI", "details (system)"),
                                         QApplication::translate("UIDetails", "Enabled", "details (system/EFI)"));
                break;
            }
            default:
                break;
        }
    }

    /* Secure Boot: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_SecureBoot)
    {
        CNvramStore comStoreLvl1 = comMachine.GetNonVolatileStore();
        if (comStoreLvl1.isNotNull())
        {
            CUefiVariableStore comStoreLvl2 = comStoreLvl1.GetUefiVariableStore();
            /// @todo this comStoreLvl2.isNotNull() will never work for
            ///       now since VM reference is immutable in Details pane
            if (   comStoreLvl2.isNotNull()
                && comStoreLvl2.GetSecureBootEnabled())
            {
                table << UITextTableLine(QApplication::translate("UIDetails", "Secure Boot", "details (system)"),
                                         QApplication::translate("UIDetails", "Enabled", "details (system/secure boot)"));
            }
        }
    }

    /* Acceleration: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Acceleration)
    {
        const CPlatform comPlatform = comMachine.GetPlatform();
        switch (comPlatform.GetArchitecture())
        {
            case KPlatformArchitecture_x86:
            {
                CPlatformX86 comPlatformX86 = comPlatform.GetX86();
                QStringList acceleration;
                if (gpGlobalSession->virtualBox().GetHost().GetProcessorFeature(KProcessorFeature_HWVirtEx))
                {
                    /* Nested Paging: */
                    if (comPlatformX86.GetHWVirtExProperty(KHWVirtExPropertyType_NestedPaging))
                        acceleration << QApplication::translate("UIDetails", "Nested Paging", "details (system)");
                }
                /* Nested VT-x/AMD-V: */
                if (comPlatformX86.GetCPUProperty(KCPUPropertyTypeX86_HWVirt))
                    acceleration << QApplication::translate("UIDetails", "Nested VT-x/AMD-V", "details (system)");
                /* PAE/NX: */
                if (comPlatformX86.GetCPUProperty(KCPUPropertyTypeX86_PAE))
                    acceleration << QApplication::translate("UIDetails", "PAE/NX", "details (system)");

                /* Paravirtualization provider: */
                switch (comMachine.GetEffectiveParavirtProvider())
                {
                    case KParavirtProvider_Minimal: acceleration << QApplication::translate("UIDetails", "Minimal Paravirtualization", "details (system)"); break;
                    case KParavirtProvider_HyperV:  acceleration << QApplication::translate("UIDetails", "Hyper-V Paravirtualization", "details (system)"); break;
                    case KParavirtProvider_KVM:     acceleration << QApplication::translate("UIDetails", "KVM Paravirtualization", "details (system)"); break;
                    default: break;
                }
                if (!acceleration.isEmpty())
                    table << UITextTableLine(QApplication::translate("UIDetails", "Acceleration", "details (system)"),
                                             acceleration.join(", "));
                break;
            }

#ifdef VBOX_WITH_VIRT_ARMV8
            case KPlatformArchitecture_ARM:
            {
                /** @todo BUGBUG ARM stuff goes here. */
                break;
            }
#endif

            default:
                break;
        }
    }

    return table;
}

UITextTable UIDetailsGenerator::generateMachineInformationDisplay(CMachine &comMachine,
                                                                  const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay &fOptions)
{
    UITextTable table;

    if (comMachine.isNull())
        return table;

    if (!comMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    const CGraphicsAdapter comGraphics = comMachine.GetGraphicsAdapter();

    /* Video memory: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_VRAM)
    {
        /* Configure hovering anchor: */
        const QString strAnchorType = QString("video_memory");
        const int iVideoMemory = comGraphics.GetVRAMSize();
        table << UITextTableLine(QApplication::translate("UIDetails", "Video Memory", "details (display)"),
                                 QString("<a href=#%1,%2>%3</a>")
                                     .arg(strAnchorType)
                                     .arg(iVideoMemory)
                                     .arg(QApplication::translate("UIDetails", "%1 MB").arg(iVideoMemory)));
    }

    /* Screens: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_ScreenCount)
    {
        const int cGuestScreens = comGraphics.GetMonitorCount();
        if (cGuestScreens > 1)
            table << UITextTableLine(QApplication::translate("UIDetails", "Screens", "details (display)"),
                                     QString::number(cGuestScreens));
    }

    /* Scale-factor: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_ScaleFactor)
    {
        const QString strScaleFactor = comMachine.GetExtraData(UIExtraDataDefs::GUI_ScaleFactor);
        {
            /* Try to convert loaded data to double: */
            bool fOk = false;
            double dValue = strScaleFactor.toDouble(&fOk);
            /* Invent the default value: */
            if (!fOk || !dValue)
                dValue = 1.0;
            /* Append information: */
            if (dValue != 1.0)
                table << UITextTableLine(QApplication::translate("UIDetails", "Scale-factor", "details (display)"),
                                         QString::number(dValue, 'f', 2));
        }
    }

    /* Graphics Controller: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_GraphicsController)
    {
        const QString strAnchorType = QString("graphics_controller_type");
        const KGraphicsControllerType enmType = comGraphics.GetGraphicsControllerType();
        table << UITextTableLine(QApplication::translate("UIDetails", "Graphics Controller", "details (display)"),
                                 QString("<a href=#%1,%2>%3</a>")
                                     .arg(strAnchorType)
                                     .arg((int)enmType)
                                     .arg(gpConverter->toString(enmType)));
    }

    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Acceleration)
    {
        /* 3D acceleration: */
        if (comGraphics.IsFeatureEnabled(KGraphicsFeature_Acceleration3D))
            table << UITextTableLine(QApplication::translate("UIDetails", "3D Acceleration", "details (display)"),
                                     QApplication::translate("UIDetails", "Enabled", "details (display/3D Acceleration)"));
    }

    /* Remote desktop server: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_VRDE)
    {
        const CVRDEServer comServer = comMachine.GetVRDEServer();
        if (!comServer.isNull())
        {
            if (comServer.GetEnabled())
                table << UITextTableLine(QApplication::translate("UIDetails", "Remote Desktop Server Port", "details (display/vrde)"),
                                         comServer.GetVRDEProperty("TCP/Ports"));
            else
                table << UITextTableLine(QApplication::translate("UIDetails", "Remote Desktop Server", "details (display/vrde)"),
                                         QApplication::translate("UIDetails", "Disabled", "details (display/vrde/VRDE server)"));
        }
    }

    /* Recording: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Recording)
    {
        CRecordingSettings comRecordingSettings = comMachine.GetRecordingSettings();
        CProgress comProgress = comRecordingSettings.GetProgress(); /** @todo r=andy Revamp this. */
        /** @r=andy Check if recording is running: if not completed AND not canceled. */
        if (comProgress.isOk() && !comProgress.GetCompleted() && !comProgress.GetCanceled())
        {
            /* For now all screens have the same config: */
            const CRecordingScreenSettings comRecordingScreen0Settings = comRecordingSettings.GetScreenSettings(0);

            /** @todo r=andy Refine these texts (wrt audio and/or video). */
            table << UITextTableLine(QApplication::translate("UIDetails", "Recording File", "details (display/recording)"),
                                     comRecordingScreen0Settings.GetFilename());
            table << UITextTableLine(QApplication::translate("UIDetails", "Recording Attributes", "details (display/recording)"),
                                     QApplication::translate("UIDetails", "Frame Size: %1x%2, Frame Rate: %3fps, Bit Rate: %4kbps")
                                     .arg(comRecordingScreen0Settings.GetVideoWidth()).arg(comRecordingScreen0Settings.GetVideoHeight())
                                     .arg(comRecordingScreen0Settings.GetVideoFPS()).arg(comRecordingScreen0Settings.GetVideoRate()));
        }
        else
            table << UITextTableLine(QApplication::translate("UIDetails", "Recording", "details (display/recording)"),
                                     QApplication::translate("UIDetails", "Disabled", "details (display/recording)"));
    }

    return table;
}

UITextTable UIDetailsGenerator::generateMachineInformationStorage(CMachine &comMachine,
                                                                  const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage &fOptions,
                                                                  bool fLink /* = true */)
{
    UITextTable table;

    if (comMachine.isNull())
        return table;

    if (!comMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    /* Iterate over all the machine controllers: */
    foreach (const CStorageController &comController, comMachine.GetStorageControllers())
    {
        /* Add controller information: */
        const QString strControllerName = QApplication::translate("UIStorageSettingsEditor", "Controller: %1");
        table << UITextTableLine(strControllerName.arg(comController.GetName()), QString());
        /* Populate map (its sorted!): */
        QMap<StorageSlot, QString> attachmentsMap;
        foreach (const CMediumAttachment &attachment, comMachine.GetMediumAttachmentsOfController(comController.GetName()))
        {
            /* Acquire device type first of all: */
            const KDeviceType enmDeviceType = attachment.GetType();

            /* Ignore restricted device types: */
            if (   (   !(fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_HardDisks)
                       && enmDeviceType == KDeviceType_HardDisk)
                   || (   !(fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_OpticalDevices)
                          && enmDeviceType == KDeviceType_DVD)
                   || (   !(fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_FloppyDevices)
                          && enmDeviceType == KDeviceType_Floppy))
                continue;

            /* Prepare current storage slot: */
            const StorageSlot attachmentSlot(comController.GetBus(), attachment.GetPort(), attachment.GetDevice());
            AssertMsg(comController.isOk(),
                      ("Unable to acquire controller data: %s\n",
                       UIErrorString::formatRC(comController.lastRC()).toUtf8().constData()));
            if (!comController.isOk())
                continue;

            /* Prepare attachment information: */
            QString strAttachmentInfo = UIMediumTools::storageDetails(attachment.GetMedium(), false, false);
            /* That hack makes sure 'Inaccessible' word is always bold: */
            { // hack
                const QString strInaccessibleString(QApplication::translate("UIMedium", "Inaccessible", "medium"));
                const QString strBoldInaccessibleString(QString("<b>%1</b>").arg(strInaccessibleString));
                strAttachmentInfo.replace(strInaccessibleString, strBoldInaccessibleString);
            } // hack

            /* Append 'device slot name' with 'device type name' for optical devices only: */
            QString strDeviceType = enmDeviceType == KDeviceType_DVD
                ? QApplication::translate("UIDetails", "[Optical Drive]", "details (storage)")
                : QString();
            if (!strDeviceType.isNull())
                strDeviceType.append(' ');

            /* Insert that attachment information into the map: */
            if (!strAttachmentInfo.isNull())
            {
                /* Configure hovering anchors: */
                const QString strAnchorType = enmDeviceType == KDeviceType_DVD || enmDeviceType == KDeviceType_Floppy ? QString("mount") :
                    enmDeviceType == KDeviceType_HardDisk ? QString("attach") : QString();
                const CMedium medium = attachment.GetMedium();
                const QString strMediumLocation = medium.isNull() ? QString() : medium.GetLocation();
                if (fLink)
                    attachmentsMap.insert(attachmentSlot,
                                          QString("<a href=#%1,%2,%3,%4>%5</a>")
                                              .arg(strAnchorType,
                                                   comController.GetName(),
                                                   gpConverter->toString(attachmentSlot),
                                                   strMediumLocation,
                                                   strDeviceType + strAttachmentInfo));
                else
                    attachmentsMap.insert(attachmentSlot,
                                          QString("%1")
                                              .arg(strDeviceType + strAttachmentInfo));
            }
        }

        /* Iterate over the sorted map: */
        const QList<StorageSlot> storageSlots = attachmentsMap.keys();
        const QList<QString> storageInfo = attachmentsMap.values();
        for (int i = 0; i < storageSlots.size(); ++i)
            table << UITextTableLine(QString("  ") + gpConverter->toString(storageSlots[i]), storageInfo[i]);
    }
    if (table.isEmpty())
        table << UITextTableLine(QApplication::translate("UIDetails", "Not Attached", "details (storage)"), QString());

    return table;
}

UITextTable UIDetailsGenerator::generateMachineInformationAudio(CMachine &comMachine,
                                                                const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio &fOptions)
{
    UITextTable table;

    if (comMachine.isNull())
        return table;

    if (!comMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    const CAudioSettings comAudioSettings = comMachine.GetAudioSettings();
    const CAudioAdapter  comAdapter       = comAudioSettings.GetAdapter();
    if (comAdapter.GetEnabled())
    {
        /* Host driver: */
        if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Driver)
        {
            const QString strAnchorType = QString("audio_host_driver_type");
            const KAudioDriverType enmType = comAdapter.GetAudioDriver();
            table << UITextTableLine(QApplication::translate("UIDetails", "Host Driver", "details (audio)"),
                                     QString("<a href=#%1,%2>%3</a>")
                                         .arg(strAnchorType)
                                         .arg((int)enmType)
                                         .arg(gpConverter->toString(enmType)));
        }

        /* Controller: */
        if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Controller)
        {
            const QString strAnchorType = QString("audio_controller_type");
            const KAudioControllerType enmType = comAdapter.GetAudioController();
            table << UITextTableLine(QApplication::translate("UIDetails", "Controller", "details (audio)"),
                                     QString("<a href=#%1,%2>%3</a>")
                                         .arg(strAnchorType)
                                         .arg((int)enmType)
                                         .arg(gpConverter->toString(enmType)));
        }

#ifdef VBOX_WITH_AUDIO_INOUT_INFO
        /* Audio I/O: */
        if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_IO)
        {
            table << UITextTableLine(QApplication::translate("UIDetails", "Audio Input", "details (audio)"),
                                     comAdapter.GetEnabledIn() ?
                                     QApplication::translate("UIDetails", "Enabled", "details (audio/input)") :
                                     QApplication::translate("UIDetails", "Disabled", "details (audio/input)"));
            table << UITextTableLine(QApplication::translate("UIDetails", "Audio Output", "details (audio)"),
                                     comAdapter.GetEnabledOut() ?
                                     QApplication::translate("UIDetails", "Enabled", "details (audio/output)") :
                                     QApplication::translate("UIDetails", "Disabled", "details (audio/output)"));
        }
#endif /* VBOX_WITH_AUDIO_INOUT_INFO */
    }
    else
        table << UITextTableLine(QApplication::translate("UIDetails", "Disabled", "details (audio)"),
                                 QString());

    return table;
}

UITextTable UIDetailsGenerator::generateMachineInformationNetwork(CMachine &comMachine,
                                                                  const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork &fOptions)
{
    UITextTable table;

    if (comMachine.isNull())
        return table;

    if (!comMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    /* Iterate over all the adapters: */
    CPlatform comPlatform = comMachine.GetPlatform();
    const KPlatformArchitecture enmArch = comPlatform.GetArchitecture();
    const KChipsetType enmChipsetType = comPlatform.GetChipsetType();
    CPlatformProperties comProperties = gpGlobalSession->virtualBox().GetPlatformProperties(enmArch);
    const ulong cMaxNetworkAdapters = comProperties.GetMaxNetworkAdapters(enmChipsetType);
    for (ulong uSlot = 0; uSlot < cMaxNetworkAdapters; ++uSlot)
    {
        const QString strAnchorType = QString("network_attachment_type");
        const CNetworkAdapter comAdapter = comMachine.GetNetworkAdapter(uSlot);

        /* Skip disabled adapters: */
        if (!comAdapter.GetEnabled())
            continue;

        /* Gather adapter information: */
        const KNetworkAttachmentType enmAttachmentType = comAdapter.GetAttachmentType();
        const QString strAttachmentTemplate = gpConverter->toString(comAdapter.GetAdapterType()).replace(QRegularExpression("\\s\\(.+\\)"),
                                                                                                         " (<a href=#%1,%2;%3;%4>%5</a>)");
        QString strAttachmentType;
        switch (enmAttachmentType)
        {
            case KNetworkAttachmentType_NAT:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NAT)
                    strAttachmentType = strAttachmentTemplate
                                            .arg(strAnchorType)
                                            .arg(uSlot)
                                            .arg((int)KNetworkAttachmentType_NAT)
                                            .arg(QString())
                                            .arg(gpConverter->toString(KNetworkAttachmentType_NAT));
                break;
            }
            case KNetworkAttachmentType_Bridged:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_BridgedAdapter)
                {
                    const QString strName = comAdapter.GetBridgedInterface();
                    strAttachmentType = strAttachmentTemplate
                                            .arg(strAnchorType)
                                            .arg(uSlot)
                                            .arg((int)KNetworkAttachmentType_Bridged)
                                            .arg(strName)
                                            .arg(QApplication::translate("UIDetails", "Bridged Adapter, %1", "details (network)")
                                                 .arg(strName));
                }
                break;
            }
            case KNetworkAttachmentType_Internal:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_InternalNetwork)
                {
                    const QString strName = comAdapter.GetInternalNetwork();
                    strAttachmentType = strAttachmentTemplate
                                            .arg(strAnchorType)
                                            .arg(uSlot)
                                            .arg((int)KNetworkAttachmentType_Internal)
                                            .arg(strName)
                                            .arg(QApplication::translate("UIDetails", "Internal Network, '%1'", "details (network)")
                                                 .arg(strName));
                }
                break;
            }
            case KNetworkAttachmentType_HostOnly:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_HostOnlyAdapter)
                {
                    const QString strName = comAdapter.GetHostOnlyInterface();
                    strAttachmentType = strAttachmentTemplate
                                            .arg(strAnchorType)
                                            .arg(uSlot)
                                            .arg((int)KNetworkAttachmentType_HostOnly)
                                            .arg(strName)
                                            .arg(QApplication::translate("UIDetails", "Host-only Adapter, '%1'", "details (network)")
                                                 .arg(strName));
                }
                break;
            }
#ifdef VBOX_WITH_VMNET
            case KNetworkAttachmentType_HostOnlyNetwork:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_HostOnlyNetwork)
                {
                    const QString strName = comAdapter.GetHostOnlyNetwork();
                    strAttachmentType = strAttachmentTemplate
                                            .arg(strAnchorType)
                                            .arg(uSlot)
                                            .arg((int)KNetworkAttachmentType_HostOnly)
                                            .arg(strName)
                                            .arg(QApplication::translate("UIDetails", "Host-only Network, '%1'", "details (network)")
                                                 .arg(strName));
                }
                break;
            }
#endif /* VBOX_WITH_VMNET */
            case KNetworkAttachmentType_Generic:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_GenericDriver)
                {
                    const QString strName = comAdapter.GetGenericDriver();
                    const QString strGenericDriverProperties(summarizeGenericProperties(comAdapter));
                    strAttachmentType = strGenericDriverProperties.isNull()
                                      ? strAttachmentTemplate
                                            .arg(strAnchorType)
                                            .arg(uSlot)
                                            .arg((int)KNetworkAttachmentType_Generic)
                                            .arg(strName)
                                            .arg(QApplication::translate("UIDetails", "Generic Driver, '%1'", "details (network)")
                                                 .arg(strName))
                                      : strAttachmentTemplate
                                            .arg(strAnchorType)
                                            .arg(uSlot)
                                            .arg((int)KNetworkAttachmentType_Generic)
                                            .arg(strName)
                                            .arg(QApplication::translate("UIDetails", "Generic Driver, '%1' { %2 }", "details (network)")
                                                 .arg(strName, strGenericDriverProperties));
                }
                break;
            }
            case KNetworkAttachmentType_NATNetwork:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NATNetwork)
                {
                    const QString strName = comAdapter.GetNATNetwork();
                    strAttachmentType = strAttachmentTemplate
                                            .arg(strAnchorType)
                                            .arg(uSlot)
                                            .arg((int)KNetworkAttachmentType_NATNetwork)
                                            .arg(strName)
                                            .arg(QApplication::translate("UIDetails", "NAT Network, '%1'", "details (network)")
                                                 .arg(strName));
                }
                break;
            }
#ifdef VBOX_WITH_CLOUD_NET
            case KNetworkAttachmentType_Cloud:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_CloudNetwork)
                {
                    const QString strName = comAdapter.GetCloudNetwork();
                    strAttachmentType = strAttachmentTemplate
                                            .arg(strAnchorType)
                                            .arg(uSlot)
                                            .arg((int)KNetworkAttachmentType_Cloud)
                                            .arg(strName)
                                            .arg(QApplication::translate("UIDetails", "Cloud Network, '%1'", "details (network)")
                                                 .arg(strName));
                }
                break;
            }
#endif /* VBOX_WITH_CLOUD_NET */
            default:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NotAttached)
                    strAttachmentType = strAttachmentTemplate
                                            .arg(strAnchorType)
                                            .arg(uSlot)
                                            .arg((int)enmAttachmentType)
                                            .arg(QString())
                                            .arg(gpConverter->toString(enmAttachmentType));
                break;
            }
        }
        if (!strAttachmentType.isNull())
            table << UITextTableLine(QApplication::translate("UIDetails", "Adapter %1", "details (network)").arg(comAdapter.GetSlot() + 1), strAttachmentType);
    }
    if (table.isEmpty())
        table << UITextTableLine(QApplication::translate("UIDetails", "Disabled", "details (network/adapter)"), QString());

    return table;
}

UITextTable UIDetailsGenerator::generateMachineInformationSerial(CMachine &comMachine,
                                                                 const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial &fOptions)
{
    UITextTable table;

    if (comMachine.isNull())
        return table;

    if (!comMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    /* Iterate over all the ports: */
    CPlatform comPlatform = comMachine.GetPlatform();
    const KPlatformArchitecture enmArch = comPlatform.GetArchitecture();
    CPlatformProperties comProperties = gpGlobalSession->virtualBox().GetPlatformProperties(enmArch);
    const ulong cMaxSerialPorts = comProperties.GetSerialPortCount();
    for (ulong uSlot = 0; uSlot < cMaxSerialPorts; ++uSlot)
    {
        const CSerialPort comPort = comMachine.GetSerialPort(uSlot);

        /* Skip disabled adapters: */
        if (!comPort.GetEnabled())
            continue;

        /* Gather port information: */
        const KPortMode enmMode = comPort.GetHostMode();
        const QString strModeTemplate = UITranslator::toCOMPortName(comPort.GetIRQ(), comPort.GetIOAddress()) + ", ";
        QString strModeType;
        switch (enmMode)
        {
            case KPortMode_HostPipe:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_HostPipe)
                    strModeType = strModeTemplate + QString("%1 (%2)").arg(gpConverter->toString(enmMode)).arg(QDir::toNativeSeparators(comPort.GetPath()));
                break;
            }
            case KPortMode_HostDevice:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_HostDevice)
                    strModeType = strModeTemplate + QString("%1 (%2)").arg(gpConverter->toString(enmMode)).arg(QDir::toNativeSeparators(comPort.GetPath()));
                break;
            }
            case KPortMode_RawFile:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_RawFile)
                    strModeType = strModeTemplate + QString("%1 (%2)").arg(gpConverter->toString(enmMode)).arg(QDir::toNativeSeparators(comPort.GetPath()));
                break;
            }
            case KPortMode_TCP:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_TCP)
                    strModeType = strModeTemplate + QString("%1 (%2)").arg(gpConverter->toString(enmMode)).arg(QDir::toNativeSeparators(comPort.GetPath()));
                break;
            }
            default:
            {
                if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Disconnected)
                    strModeType = strModeTemplate + gpConverter->toString(enmMode);
                break;
            }
        }
        if (!strModeType.isNull())
            table << UITextTableLine(QApplication::translate("UIDetails", "Port %1", "details (serial)").arg(comPort.GetSlot() + 1), strModeType);
    }
    if (table.isEmpty())
        table << UITextTableLine(QApplication::translate("UIDetails", "Disabled", "details (serial)"), QString());

    return table;
}

UITextTable UIDetailsGenerator::generateMachineInformationUSB(CMachine &comMachine,
                                                              const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb &fOptions)
{
    UITextTable table;

    if (comMachine.isNull())
        return table;

    if (!comMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    /* Iterate over all the USB filters: */
    const CUSBDeviceFilters comFilterObject = comMachine.GetUSBDeviceFilters();
    if (!comFilterObject.isNull() && comMachine.GetUSBProxyAvailable())
    {
        const QString strAnchorType = QString("usb_controller_type");
        const CUSBControllerVector controllers = comMachine.GetUSBControllers();
        if (!controllers.isEmpty())
        {
            /* USB controllers: */
            if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Controller)
            {
                QStringList controllerInternal;
                QStringList controllersReadable;
                foreach (const CUSBController &comController, controllers)
                {
                    const KUSBControllerType enmType = comController.GetType();
                    controllerInternal << QString::number((int)enmType);
                    controllersReadable << gpConverter->toString(enmType);
                }
                table << UITextTableLine(QApplication::translate("UIDetails", "USB Controller", "details (usb)"),
                                         QString("<a href=#%1,%2>%3</a>")
                                             .arg(strAnchorType)
                                             .arg(controllerInternal.join(';'))
                                             .arg(controllersReadable.join(", ")));
            }

            /* Device filters: */
            if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_DeviceFilters)
            {
                const CUSBDeviceFilterVector filters = comFilterObject.GetDeviceFilters();
                uint uActive = 0;
                for (int i = 0; i < filters.size(); ++i)
                    if (filters.at(i).GetActive())
                        ++uActive;
                table << UITextTableLine(QApplication::translate("UIDetails", "Device Filters", "details (usb)"),
                                         QApplication::translate("UIDetails", "%1 (%2 active)", "details (usb)").arg(filters.size()).arg(uActive));
            }
        }
        else
            table << UITextTableLine(QString("<a href=#%1,%2>%3</a>")
                                         .arg(strAnchorType)
                                         .arg(QString::number((int)KUSBControllerType_Null))
                                         .arg(QApplication::translate("UIDetails", "Disabled", "details (usb)")),
                                     QString());
    }
    else
        table << UITextTableLine(QApplication::translate("UIDetails", "USB Controller Inaccessible", "details (usb)"), QString());

    return table;
}

UITextTable UIDetailsGenerator::generateMachineInformationSharedFolders(CMachine &comMachine,
                                                                        const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders &fOptions)
{
    Q_UNUSED(fOptions);

    UITextTable table;

    if (comMachine.isNull())
        return table;

    if (!comMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    /* Summary: */
    const ulong uCount = comMachine.GetSharedFolders().size();
    if (uCount > 0)
        table << UITextTableLine(QApplication::translate("UIDetails", "Shared Folders", "details (shared folders)"), QString::number(uCount));
    else
        table << UITextTableLine(QApplication::translate("UIDetails", "None", "details (shared folders)"), QString());

    return table;
}

UITextTable UIDetailsGenerator::generateMachineInformationUI(CMachine &comMachine,
                                                             const UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface &fOptions)
{
    UITextTable table;

    if (comMachine.isNull())
        return table;

    if (!comMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    /* Visual state: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_VisualState)
    {
        const QString strAnchorType = QString("visual_state");
        const QString strEnabledFullscreen = comMachine.GetExtraData(UIExtraDataDefs::GUI_Fullscreen);
        const QString strEnabledSeamless = comMachine.GetExtraData(UIExtraDataDefs::GUI_Seamless);
        const QString strEnabledScale = comMachine.GetExtraData(UIExtraDataDefs::GUI_Scale);
        UIVisualStateType enmType = UIVisualStateType_Normal;
        if (   strEnabledFullscreen.compare("true", Qt::CaseInsensitive) == 0
            || strEnabledFullscreen.compare("yes", Qt::CaseInsensitive) == 0
            || strEnabledFullscreen.compare("on", Qt::CaseInsensitive) == 0
            || strEnabledFullscreen == "1")
            enmType = UIVisualStateType_Fullscreen;
        else
        if (   strEnabledSeamless.compare("true", Qt::CaseInsensitive) == 0
            || strEnabledSeamless.compare("yes", Qt::CaseInsensitive) == 0
            || strEnabledSeamless.compare("on", Qt::CaseInsensitive) == 0
            || strEnabledSeamless == "1")
            enmType = UIVisualStateType_Seamless;
        else
        if (   strEnabledScale.compare("true", Qt::CaseInsensitive) == 0
            || strEnabledScale.compare("yes", Qt::CaseInsensitive) == 0
            || strEnabledScale.compare("on", Qt::CaseInsensitive) == 0
            || strEnabledScale == "1")
            enmType = UIVisualStateType_Scale;
        const QString strVisualState = gpConverter->toString(enmType);
        table << UITextTableLine(QApplication::translate("UIDetails", "Visual State", "details (user interface)"),
                                 QString("<a href=#%1,%2>%3</a>")
                                     .arg(strAnchorType)
                                     .arg(enmType)
                                     .arg(strVisualState));
    }

#ifndef VBOX_WS_MAC
    /* Menu-bar: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_MenuBar)
    {
        const QString strAnchorType = QString("menu_bar");
        const QString strMenubarEnabled = comMachine.GetExtraData(UIExtraDataDefs::GUI_MenuBar_Enabled);
        const bool fEnabled = !(   strMenubarEnabled.compare("false", Qt::CaseInsensitive) == 0
                                || strMenubarEnabled.compare("no", Qt::CaseInsensitive) == 0
                                || strMenubarEnabled.compare("off", Qt::CaseInsensitive) == 0
                                || strMenubarEnabled == "0");
        table << UITextTableLine(QApplication::translate("UIDetails", "Menu-bar", "details (user interface)"),
                                 QString("<a href=#%1,%2>%3</a>")
                                     .arg(strAnchorType)
                                     .arg((int)fEnabled)
                                     .arg(fEnabled ? QApplication::translate("UIDetails", "Enabled", "details (user interface/menu-bar)")
                                                   : QApplication::translate("UIDetails", "Disabled", "details (user interface/menu-bar)")));
    }
#endif /* !VBOX_WS_MAC */

    /* Status-bar: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_StatusBar)
    {
        const QString strAnchorType = QString("status_bar");
        const QString strStatusbarEnabled = comMachine.GetExtraData(UIExtraDataDefs::GUI_StatusBar_Enabled);
        const bool fEnabled = !(   strStatusbarEnabled.compare("false", Qt::CaseInsensitive) == 0
                                || strStatusbarEnabled.compare("no", Qt::CaseInsensitive) == 0
                                || strStatusbarEnabled.compare("off", Qt::CaseInsensitive) == 0
                                || strStatusbarEnabled == "0");
        table << UITextTableLine(QApplication::translate("UIDetails", "Status-bar", "details (user interface)"),
                                 QString("<a href=#%1,%2>%3</a>")
                                     .arg(strAnchorType)
                                     .arg((int)fEnabled)
                                     .arg(fEnabled ? QApplication::translate("UIDetails", "Enabled", "details (user interface/status-bar)")
                                                   : QApplication::translate("UIDetails", "Disabled", "details (user interface/status-bar)")));
    }

#ifndef VBOX_WS_MAC
    /* Mini-toolbar: */
    if (fOptions & UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_MiniToolbar)
    {
        const QString strAnchorType = QString("mini_toolbar");
        const QString strMiniToolbarEnabled = comMachine.GetExtraData(UIExtraDataDefs::GUI_ShowMiniToolBar);
        const bool fEnabled = !(   strMiniToolbarEnabled.compare("false", Qt::CaseInsensitive) == 0
                                || strMiniToolbarEnabled.compare("no", Qt::CaseInsensitive) == 0
                                || strMiniToolbarEnabled.compare("off", Qt::CaseInsensitive) == 0
                                || strMiniToolbarEnabled == "0");
        if (fEnabled)
        {
            /* Get mini-toolbar position: */
            const QString strMiniToolbarPosition = comMachine.GetExtraData(UIExtraDataDefs::GUI_MiniToolBarAlignment);
            {
                /* Try to convert loaded data to alignment: */
                switch (gpConverter->fromInternalString<MiniToolbarAlignment>(strMiniToolbarPosition))
                {
                    case MiniToolbarAlignment_Top:
                        table << UITextTableLine(QApplication::translate("UIDetails", "Mini-toolbar Position", "details (user interface)"),
                                                 QString("<a href=#%1,%2>%3</a>")
                                                     .arg(strAnchorType)
                                                     .arg((int)MiniToolbarAlignment_Top)
                                                     .arg(QApplication::translate("UIDetails", "Top", "details (user interface/mini-toolbar position)")));
                        break;
                    case MiniToolbarAlignment_Bottom:
                        table << UITextTableLine(QApplication::translate("UIDetails", "Mini-toolbar Position", "details (user interface)"),
                                                 QString("<a href=#%1,%2>%3</a>")
                                                     .arg(strAnchorType)
                                                     .arg((int)MiniToolbarAlignment_Bottom)
                                                     .arg(QApplication::translate("UIDetails", "Bottom", "details (user interface/mini-toolbar position)")));
                        break;
                    default:
                        break;
                }
            }
        }
        else
            table << UITextTableLine(QApplication::translate("UIDetails", "Mini-toolbar", "details (user interface)"),
                                     QString("<a href=#%1,%2>%3</a>")
                                         .arg(strAnchorType)
                                         .arg((int)MiniToolbarAlignment_Disabled)
                                         .arg(QApplication::translate("UIDetails", "Disabled", "details (user interface/mini-toolbar)")));
    }
#endif /* !VBOX_WS_MAC */

    return table;
}

UITextTable UIDetailsGenerator::generateMachineInformationDescription(CMachine &comMachine,
                                                                      const UIExtraDataMetaDefs::DetailsElementOptionTypeDescription &fOptions)
{
    Q_UNUSED(fOptions);

    UITextTable table;

    if (comMachine.isNull())
        return table;

    if (!comMachine.GetAccessible())
    {
        table << UITextTableLine(QApplication::translate("UIDetails", "Information Inaccessible", "details"), QString());
        return table;
    }

    /* Summary: */
    const QString strDescription = comMachine.GetDescription();
    if (!strDescription.isEmpty())
        table << UITextTableLine(strDescription, QString());
    else
        table << UITextTableLine(QApplication::translate("UIDetails", "None", "details (description)"), QString());

    return table;
}

void UIDetailsGenerator::acquireHardDiskStatusInfo(CMachine &comMachine, QString &strInfo,
                                                   uint &cAttachmentsCount)
{
    /* Enumerate all the controllers: */
    foreach (const CStorageController &comController, comMachine.GetStorageControllers())
    {
        /* Enumerate all the attachments: */
        QString strAttData;
        foreach (const CMediumAttachment &comAttachment, comMachine.GetMediumAttachmentsOfController(comController.GetName()))
        {
            /* Skip unrelated attachments: */
            if (comAttachment.GetType() != KDeviceType_HardDisk)
                continue;
            /* Append attachment data: */
            strAttData += e_strTableRow3
                .arg(gpConverter->toString(StorageSlot(comController.GetBus(), comAttachment.GetPort(), comAttachment.GetDevice())))
                .arg(UIMedium(comAttachment.GetMedium(), UIMediumDeviceType_HardDisk).location());
            ++cAttachmentsCount;
        }
        /* Append controller data: */
        if (!strAttData.isNull())
            strInfo += e_strTableRow1.arg(comController.GetName()) + strAttData;
    }
}

void UIDetailsGenerator::acquireOpticalDiskStatusInfo(CMachine &comMachine, QString &strInfo,
                                                      uint &cAttachmentsCount, uint &cAttachmentsMountedCount)
{
    /* Enumerate all the controllers: */
    foreach (const CStorageController &comController, comMachine.GetStorageControllers())
    {
        QString strAttData;
        /* Enumerate all the attachments: */
        foreach (const CMediumAttachment &comAttachment, comMachine.GetMediumAttachmentsOfController(comController.GetName()))
        {
            /* Skip unrelated attachments: */
            if (comAttachment.GetType() != KDeviceType_DVD)
                continue;
            /* Append attachment data: */
            UIMedium vboxMedium(comAttachment.GetMedium(), UIMediumDeviceType_DVD);
            strAttData += e_strTableRow3
                .arg(gpConverter->toString(StorageSlot(comController.GetBus(), comAttachment.GetPort(), comAttachment.GetDevice())))
                .arg(vboxMedium.isNull() || vboxMedium.isHostDrive() ? vboxMedium.name() : vboxMedium.location());
            ++cAttachmentsCount;
            if (!vboxMedium.isNull())
                ++cAttachmentsMountedCount;
        }
        /* Append controller data: */
        if (!strAttData.isNull())
            strInfo += e_strTableRow1.arg(comController.GetName()) + strAttData;
    }
}

void UIDetailsGenerator::acquireFloppyDiskStatusInfo(CMachine &comMachine, QString &strInfo,
                                                     uint &cAttachmentsCount, uint &cAttachmentsMountedCount)
{
    /* Enumerate all the controllers: */
    foreach (const CStorageController &comController, comMachine.GetStorageControllers())
    {
        QString strAttData;
        /* Enumerate all the attachments: */
        foreach (const CMediumAttachment &comAttachment, comMachine.GetMediumAttachmentsOfController(comController.GetName()))
        {
            /* Skip unrelated attachments: */
            if (comAttachment.GetType() != KDeviceType_Floppy)
                continue;
            /* Append attachment data: */
            UIMedium vboxMedium(comAttachment.GetMedium(), UIMediumDeviceType_Floppy);
            strAttData += e_strTableRow3
                .arg(gpConverter->toString(StorageSlot(comController.GetBus(), comAttachment.GetPort(), comAttachment.GetDevice())))
                .arg(vboxMedium.isNull() || vboxMedium.isHostDrive() ? vboxMedium.name() : vboxMedium.location());
            ++cAttachmentsCount;
            if (!vboxMedium.isNull())
                ++cAttachmentsMountedCount;
        }
        /* Append controller data: */
        if (!strAttData.isNull())
            strInfo += e_strTableRow1.arg(comController.GetName()) + strAttData;
    }
}

void UIDetailsGenerator::acquireAudioStatusInfo(CMachine &comMachine, QString &strInfo,
                                                bool &fAudioEnabled, bool &fEnabledOutput, bool &fEnabledInput)
{
    /* Get audio settings & adapter: */
    const CAudioSettings comAudioSettings = comMachine.GetAudioSettings();
    const CAudioAdapter comAdapter = comAudioSettings.GetAdapter();
    fAudioEnabled = comAdapter.GetEnabled();
    if (fAudioEnabled)
    {
        fEnabledOutput = comAdapter.GetEnabledOut();
        fEnabledInput = comAdapter.GetEnabledIn();
        strInfo = QString(e_strTableRow2).arg(QApplication::translate("UIDetails", "Audio Output", "details (audio)"),
                                              fEnabledOutput ?
                                              QApplication::translate("UIDetails", "Enabled", "details (audio/output)") :
                                              QApplication::translate("UIDetails", "Disabled", "details (audio/output)"))
                + QString(e_strTableRow2).arg(QApplication::translate("UIDetails", "Audio Input", "details (audio)"),
                                              fEnabledInput ?
                                              QApplication::translate("UIDetails", "Enabled", "details (audio/input)") :
                                              QApplication::translate("UIDetails", "Disabled", "details (audio/input)"));
    }
}

void UIDetailsGenerator::acquireNetworkStatusInfo(CMachine &comMachine, QString &strInfo,
                                                  bool &fAdaptersPresent, bool &fCablesDisconnected)
{
    /* Determine max amount of network adapters: */
    CPlatform comPlatform = comMachine.GetPlatform();
    const KPlatformArchitecture enmArch = comPlatform.GetArchitecture();
    const KChipsetType enmChipsetType = comPlatform.GetChipsetType();
    CPlatformProperties comProperties = gpGlobalSession->virtualBox().GetPlatformProperties(enmArch);
    const ulong cMaxNetworkAdapters = comProperties.GetMaxNetworkAdapters(enmChipsetType);

    /* Gather adapter properties: */
    RTTIMESPEC time;
    uint64_t u64Now = RTTimeSpecGetNano(RTTimeNow(&time));
    QString strFlags, strCount;
    LONG64 iTimestamp;
    comMachine.GetGuestProperty("/VirtualBox/GuestInfo/Net/Count", strCount, iTimestamp, strFlags);
    bool fPropsValid = (u64Now - iTimestamp < UINT64_C(60000000000)); /* timeout beacon */
    QStringList ipList, macList;
    if (fPropsValid)
    {
        const ulong cAdapters = qMin(strCount.toULong(), cMaxNetworkAdapters);
        for (ulong i = 0; i < cAdapters; ++i)
        {
            ipList << comMachine.GetGuestPropertyValue(QString("/VirtualBox/GuestInfo/Net/%1/V4/IP").arg(i));
            macList << comMachine.GetGuestPropertyValue(QString("/VirtualBox/GuestInfo/Net/%1/MAC").arg(i));
        }
    }

    /* Enumerate up to cMaxNetworkAdapters adapters: */
    for (ulong uSlot = 0; uSlot < cMaxNetworkAdapters; ++uSlot)
    {
        const CNetworkAdapter &comAdapter = comMachine.GetNetworkAdapter(uSlot);
        if (comMachine.isOk() && !comAdapter.isNull() && comAdapter.GetEnabled())
        {
            fAdaptersPresent = true;
            QString strGuestIp;
            if (fPropsValid)
            {
                const QString strGuestMac = comAdapter.GetMACAddress();
                const int iIp = macList.indexOf(strGuestMac);
                if (iIp >= 0)
                    strGuestIp = ipList.at(iIp);
            }
            /* Check if the adapter's cable is connected: */
            const bool fCableConnected = comAdapter.GetCableConnected();
            if (fCablesDisconnected && fCableConnected)
                fCablesDisconnected = false;
            /* Append adapter data: */
            strInfo += e_strTableRow1
                .arg(QApplication::translate("UIIndicatorNetwork", "Adapter %1 (%2)")
                        .arg(uSlot + 1).arg(gpConverter->toString(comAdapter.GetAttachmentType())));
            if (!strGuestIp.isEmpty())
                strInfo += e_strTableRow3
                    .arg(QApplication::translate("UIIndicatorNetwork", "IP"), strGuestIp);
            strInfo += e_strTableRow3
                .arg(QApplication::translate("UIIndicatorNetwork", "Cable"))
                .arg(fCableConnected ?
                     QApplication::translate("UIIndicatorNetwork", "Connected", "cable") :
                     QApplication::translate("UIIndicatorNetwork", "Disconnected", "cable"));
        }
    }
}

void UIDetailsGenerator::acquireUsbStatusInfo(CMachine &comMachine, CConsole &comConsole,
                                              QString &strInfo, bool &fUsbEnabled, uint &cUsbFilterCount)
{
    /* Check whether there is at least one USB controller with an available proxy: */
    fUsbEnabled =    !comMachine.GetUSBDeviceFilters().isNull()
                  && !comMachine.GetUSBControllers().isEmpty()
                  && comMachine.GetUSBProxyAvailable();
    if (fUsbEnabled)
    {
        /* Enumerate all the USB devices: */
        foreach (const CUSBDevice &comUsbDevice, comConsole.GetUSBDevices())
        {
            ++cUsbFilterCount;
            strInfo += e_strTableRow1.arg(usbDetails(comUsbDevice));
        }
        /* Handle 'no-usb-devices' case: */
        if (strInfo.isNull())
            strInfo = e_strTableRow1
                .arg(QApplication::translate("UIIndicatorUSB", "No USB devices attached"));
    }
}

void UIDetailsGenerator::acquireSharedFoldersStatusInfo(CMachine &comMachine, CConsole &comConsole, CGuest &comGuest,
                                                        QString &strInfo, uint &cFoldersCount)
{
    /* Enumerate all the folders: */
    QMap<QString, QString> folders;
    foreach (const CSharedFolder &comPermanentFolder, comMachine.GetSharedFolders())
        folders.insert(comPermanentFolder.GetName(), comPermanentFolder.GetHostPath());
    foreach (const CSharedFolder &comTemporaryFolder, comConsole.GetSharedFolders())
        folders.insert(comTemporaryFolder.GetName(), comTemporaryFolder.GetHostPath());
    cFoldersCount = folders.size();

    /* Append attachment data: */
    for (QMap<QString, QString>::const_iterator it = folders.constBegin(); it != folders.constEnd(); ++it)
    {
        /* Select slashes depending on the OS type: */
        if (UIGuestOSTypeManager::isDOSType(comGuest.GetOSTypeId()))
            strInfo += e_strTableRow2.arg(QString("<b>\\\\vboxsvr\\%1</b>").arg(it.key()), it.value());
        else
            strInfo += e_strTableRow2.arg(QString("<b>%1</b>").arg(it.key()), it.value());
    }

    /* Handle 'no-folders' case: */
    if (!cFoldersCount)
        strInfo = e_strTableRow1
            .arg(QApplication::translate("UIIndicatorSharedFolders", "No shared folders"));
}

void UIDetailsGenerator::acquireDisplayStatusInfo(CMachine &comMachine, QString &strInfo,
                                                  uint &uVRAMSize, uint &cMonitorCount, bool &fAcceleration3D)
{
    /* Get graphics adapter: */
    CGraphicsAdapter comGraphics = comMachine.GetGraphicsAdapter();

    /* Video Memory: */
    uVRAMSize = comGraphics.GetVRAMSize();
    const QString strVRAMSize = QApplication::translate("UIIndicatorDisplay", "%1 MB").arg(uVRAMSize);
    strInfo += e_strTableRow2
        .arg(QApplication::translate("UIDetails", "Video Memory", "details (display)"), strVRAMSize);

    /* Monitor Count: */
    cMonitorCount = comGraphics.GetMonitorCount();
    if (cMonitorCount > 1)
        strInfo += e_strTableRow2
            .arg(QApplication::translate("UIDetails", "Screens", "details (display)"), QString::number(cMonitorCount));

    /* 3D acceleration: */
    fAcceleration3D = comGraphics.IsFeatureEnabled(KGraphicsFeature_Acceleration3D);
    if (fAcceleration3D)
        strInfo += e_strTableRow2
            .arg(QApplication::translate("UIDetails", "3D Acceleration", "details (display)"),
                 QApplication::translate("UIDetails", "Enabled", "details (display/3D Acceleration)"));
}

void UIDetailsGenerator::acquireRecordingStatusInfo(CMachine &comMachine, QString &strInfo,
                                                    bool &fRecordingEnabled)
{
    /* Get recording settings: */
    CRecordingSettings comRecordingSettings = comMachine.GetRecordingSettings();
    CProgress comProgress = comRecordingSettings.GetProgress(); /** r=andy Revamp this. */
    /** @r=andy Check if recording is running: if not completed AND not canceled. */
    fRecordingEnabled = comProgress.isOk() && !comProgress.GetCompleted() && !comProgress.GetCanceled();
    if (fRecordingEnabled)
    {
        /* For now all screens have the same config: */
        CRecordingScreenSettings comRecordingScreen0Settings = comRecordingSettings.GetScreenSettings(0);
        const bool fVideoEnabled = comRecordingScreen0Settings.IsFeatureEnabled(KRecordingFeature_Video);
        const bool fAudioEnabled = comRecordingScreen0Settings.IsFeatureEnabled(KRecordingFeature_Audio);

        /* Compose tool-tip: */
        QString strToolTip;
        if (fVideoEnabled && fAudioEnabled)
            strToolTip = QApplication::translate("UIIndicatorRecording", "Video/audio recording file");
        else if (fAudioEnabled)
            strToolTip = QApplication::translate("UIIndicatorRecording", "Audio recording file");
        else if (fVideoEnabled)
            strToolTip = QApplication::translate("UIIndicatorRecording", "Video recording file");
        strInfo += e_strTableRow2
            .arg(strToolTip)
            .arg(comRecordingScreen0Settings.GetFilename());
    }
    /* Handle 'no-recording' case: */
    else
    {
        strInfo += e_strTableRow1
            .arg(QApplication::translate("UIIndicatorRecording", "Recording disabled"));
    }
}

void UIDetailsGenerator::acquireFeaturesStatusInfo(CMachine &comMachine, QString &strInfo,
                                                   KVMExecutionEngine &enmEngine,
                                                   bool fNestedPagingEnabled, bool fUxEnabled,
                                                   KParavirtProvider enmProvider)
{
    /* VT-x/AMD-V feature: */
    QString strExecutionEngine;
    switch (enmEngine)
    {
        case KVMExecutionEngine_Interpreter:
            strExecutionEngine = "IEM (Interpreter)"; /* no translation */
            break;
        case KVMExecutionEngine_Recompiler:
            strExecutionEngine = "IEM (Recompiler)"; /* no translation */
            break;
        case KVMExecutionEngine_HwVirt:
            strExecutionEngine = "VT-x/AMD-V";  /* no translation */
            break;
        case KVMExecutionEngine_NativeApi:
            strExecutionEngine = "native API";  /* no translation */
            break;
        default:
            AssertFailed();
            enmEngine = KVMExecutionEngine_NotSet;
            RT_FALL_THRU();
        case KVMExecutionEngine_NotSet:
            strExecutionEngine = QApplication::translate("UIIndicatorFeatures", "not set", "Execution engine");
            break;
    }

    /* Nested Paging feature: */
    const QString strNestedPaging = fNestedPagingEnabled
                                  ? QApplication::translate("UIIndicatorFeatures", "Active", "Nested paging")
                                  : QApplication::translate("UIIndicatorFeatures", "Inactive", "Nested paging");

    /* Unrestricted Execution feature: */
    const QString strUnrestrictExec = fUxEnabled
                                    ? QApplication::translate("UIIndicatorFeatures", "Active", "Unrestricted execution")
                                    : QApplication::translate("UIIndicatorFeatures", "Inactive", "Unrestricted execution");

    /* CPU Execution Cap feature: */
    const QString strCPUExecCap = QString::number(comMachine.GetCPUExecutionCap());

    /* Paravirtualization feature: */
    const QString strParavirt = gpConverter->toString(enmProvider);

    /* Compose tool-tip: */
    strInfo += e_strTableRow2.arg(QApplication::translate("UIIndicatorFeatures", "Execution Engine"), strExecutionEngine);
    strInfo += e_strTableRow2.arg(QApplication::translate("UIDetails", "Nested Paging", "details (system)"), strNestedPaging);
    strInfo += e_strTableRow2.arg(QApplication::translate("UIIndicatorFeatures", "Unrestricted Execution"), strUnrestrictExec);
    strInfo += e_strTableRow2.arg(QApplication::translate("UIDetails", "Execution Cap", "details (system)"), strCPUExecCap);
    strInfo += e_strTableRow2.arg(QApplication::translate("UIIndicatorFeatures", "Paravirtualization Interface"), strParavirt);

    /* Add CPU count optional info: */
    const int cCpuCount = comMachine.GetCPUCount();
    if (cCpuCount > 1)
        strInfo += e_strTableRow2.arg(QApplication::translate("UIDetails", "Processors", "details (system)"), QString::number(cCpuCount));
}

QString UIDetailsGenerator::summarizeGenericProperties(const CNetworkAdapter &comAdapter)
{
    QVector<QString> names;
    QVector<QString> props;
    props = comAdapter.GetProperties(QString(), names);
    QString strResult;
    for (int i = 0; i < names.size(); ++i)
    {
        strResult += names[i] + "=" + props[i];
        if (i < names.size() - 1)
            strResult += ", ";
    }
    return strResult;
}
