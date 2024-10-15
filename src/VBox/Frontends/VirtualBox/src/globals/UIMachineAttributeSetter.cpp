/* $Id: UIMachineAttributeSetter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineAttributeSetter namespace implementation.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#include <QVariant>

/* GUI includes: */
#include "UIBootOrderEditor.h"
#include "UILocalMachineStuff.h"
#include "UIMachineAttributeSetter.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"

/* COM includes: */
#include "CAudioAdapter.h"
#include "CAudioSettings.h"
#include "CGraphicsAdapter.h"
#include "CNetworkAdapter.h"
#include "CUSBController.h"


void removeUSBControllers(CMachine &comMachine, const UIUSBControllerTypeSet &controllerSet = UIUSBControllerTypeSet())
{
    /* Get controllers for further activities: */
    const CUSBControllerVector &controllers = comMachine.GetUSBControllers();
    if (!comMachine.isOk())
        return;

    /* For each controller: */
    foreach (const CUSBController &comController, controllers)
    {
        /* Get controller type&name for further activities: */
        const KUSBControllerType enmType = comController.GetType();
        const QString strName = comController.GetName();

        /* Pass only if requested types were not defined or contains the one we found: */
        if (!controllerSet.isEmpty() && !controllerSet.contains(enmType))
            continue;

        /* Remove controller: */
        comMachine.RemoveUSBController(strName);
        if (!comMachine.isOk())
            break;
    }
}

void createUSBControllers(CMachine &comMachine, const UIUSBControllerTypeSet &controllerSet)
{
    /* For each requested USB controller type: */
    foreach (const KUSBControllerType &enmType, controllerSet)
    {
        switch (enmType)
        {
            case KUSBControllerType_OHCI: comMachine.AddUSBController("OHCI", KUSBControllerType_OHCI); break;
            case KUSBControllerType_EHCI: comMachine.AddUSBController("EHCI", KUSBControllerType_EHCI); break;
            case KUSBControllerType_XHCI: comMachine.AddUSBController("xHCI", KUSBControllerType_XHCI); break;
            default: break;
        }
    }
}

void UIMachineAttributeSetter::setMachineAttribute(const CMachine &comConstMachine,
                                                   const MachineAttribute &enmType,
                                                   const QVariant &guiAttribute)
{
    /* Get editable machine & session: */
    CMachine comMachine = comConstMachine;
    CSession comSession = tryToOpenSessionFor(comMachine);

    /* Main API block: */
    do
    {
        /* Save machine settings? */
        bool fSaveSettings = true;
        /* Error happened? */
        bool fErrorHappened = false;

        /* Assign attribute depending on passed type: */
        switch (enmType)
        {
            case MachineAttribute_Name:
            {
                /* Change machine name: */
                comMachine.SetName(guiAttribute.toString());
                if (!comMachine.isOk())
                {
                    UINotificationMessage::cannotChangeMachineParameter(comMachine);
                    fErrorHappened = true;
                }
                break;
            }
            case MachineAttribute_OSType:
            {
                /* Change machine OS type: */
                comMachine.SetOSTypeId(guiAttribute.toString());
                if (!comMachine.isOk())
                {
                    UINotificationMessage::cannotChangeMachineParameter(comMachine);
                    fErrorHappened = true;
                }
                break;
            }
            case MachineAttribute_BaseMemory:
            {
                /* Change machine base memory (RAM): */
                comMachine.SetMemorySize(guiAttribute.toInt());
                if (!comMachine.isOk())
                {
                    UINotificationMessage::cannotChangeMachineParameter(comMachine);
                    fErrorHappened = true;
                }
                break;
            }
            case MachineAttribute_BootOrder:
            {
                /* Change machine boot order: */
                saveBootItems(guiAttribute.value<UIBootItemDataList>(), comMachine);
                if (!comMachine.isOk())
                {
                    UINotificationMessage::cannotChangeMachineParameter(comMachine);
                    fErrorHappened = true;
                }
                break;
            }
            case MachineAttribute_VideoMemory:
            {
                /* Acquire graphics adapter: */
                CGraphicsAdapter comGraphics = comMachine.GetGraphicsAdapter();
                if (!comMachine.isOk())
                {
                    UINotificationMessage::cannotAcquireMachineParameter(comMachine);
                    fErrorHappened = true;
                    break;
                }
                /* Change machine video memory (VRAM): */
                comGraphics.SetVRAMSize(guiAttribute.toInt());
                if (!comGraphics.isOk())
                {
                    UINotificationMessage::cannotChangeGraphicsAdapterParameter(comGraphics);
                    fErrorHappened = true;
                }
                break;
            }
            case MachineAttribute_GraphicsControllerType:
            {
                /* Acquire graphics adapter: */
                CGraphicsAdapter comGraphics = comMachine.GetGraphicsAdapter();
                if (!comMachine.isOk())
                {
                    UINotificationMessage::cannotAcquireMachineParameter(comMachine);
                    fErrorHappened = true;
                    break;
                }
                /* Change machine graphics controller type: */
                comGraphics.SetGraphicsControllerType(guiAttribute.value<KGraphicsControllerType>());
                if (!comGraphics.isOk())
                {
                    UINotificationMessage::cannotChangeGraphicsAdapterParameter(comGraphics);
                    fErrorHappened = true;
                }
                break;
            }
            case MachineAttribute_AudioHostDriverType:
            {
                /* Acquire audio adapter: */
                CAudioSettings const comAudioSettings = comMachine.GetAudioSettings();
                CAudioAdapter        comAdapter       = comAudioSettings.GetAdapter();
                if (!comAudioSettings.isOk())
                {
                    UINotificationMessage::cannotAcquireMachineParameter(comMachine);
                    fErrorHappened = true;
                    break;
                }
                /* Change audio host driver type: */
                comAdapter.SetAudioDriver(guiAttribute.value<KAudioDriverType>());
                if (!comAdapter.isOk())
                {
                    UINotificationMessage::cannotChangeAudioAdapterParameter(comAdapter);
                    fErrorHappened = true;
                }
                break;
            }
            case MachineAttribute_AudioControllerType:
            {
                /* Acquire audio adapter: */
                CAudioSettings const comAudioSettings = comMachine.GetAudioSettings();
                CAudioAdapter        comAdapter       = comAudioSettings.GetAdapter();
                if (!comAudioSettings.isOk())
                {
                    UINotificationMessage::cannotAcquireMachineParameter(comMachine);
                    fErrorHappened = true;
                    break;
                }
                /* Change audio controller type: */
                comAdapter.SetAudioController(guiAttribute.value<KAudioControllerType>());
                if (!comAdapter.isOk())
                {
                    UINotificationMessage::cannotChangeAudioAdapterParameter(comAdapter);
                    fErrorHappened = true;
                }
                break;
            }
            case MachineAttribute_NetworkAttachmentType:
            {
                /* Acquire value itself: */
                const UINetworkAdapterDescriptor nad = guiAttribute.value<UINetworkAdapterDescriptor>();
                /* Acquire network adapter: */
                CNetworkAdapter comAdapter = comMachine.GetNetworkAdapter(nad.m_iSlot);
                if (!comMachine.isOk())
                {
                    UINotificationMessage::cannotAcquireMachineParameter(comMachine);
                    fErrorHappened = true;
                    break;
                }
                /* Change network adapter attachment type: */
                comAdapter.SetAttachmentType(nad.m_enmType);
                if (!comAdapter.isOk())
                {
                    UINotificationMessage::cannotChangeNetworkAdapterParameter(comAdapter);
                    fErrorHappened = true;
                    break;
                }
                /* Change network adapter name: */
                switch (nad.m_enmType)
                {
                    case KNetworkAttachmentType_Bridged: comAdapter.SetBridgedInterface(nad.m_strName); break;
                    case KNetworkAttachmentType_Internal: comAdapter.SetInternalNetwork(nad.m_strName); break;
                    case KNetworkAttachmentType_HostOnly: comAdapter.SetHostOnlyInterface(nad.m_strName); break;
                    case KNetworkAttachmentType_Generic: comAdapter.SetGenericDriver(nad.m_strName); break;
                    case KNetworkAttachmentType_NATNetwork: comAdapter.SetNATNetwork(nad.m_strName); break;
#ifdef VBOX_WITH_CLOUD_NET
                    case KNetworkAttachmentType_Cloud: comAdapter.SetCloudNetwork(nad.m_strName); break;
#endif
#ifdef VBOX_WITH_VMNET
                    case KNetworkAttachmentType_HostOnlyNetwork: comAdapter.SetHostOnlyNetwork(nad.m_strName); break;
#endif
                    default: break;
                }
                if (!comAdapter.isOk())
                {
                    UINotificationMessage::cannotChangeNetworkAdapterParameter(comAdapter);
                    fErrorHappened = true;
                }
                break;
            }
            case MachineAttribute_USBControllerType:
            {
                /* Remove all existing controller first of all: */
                removeUSBControllers(comMachine);
                if (!comMachine.isOk())
                {
                    UINotificationMessage::cannotChangeMachineParameter(comMachine);
                    fErrorHappened = true;
                    break;
                }
                /* Add new controllers afterwards: */
                const UIUSBControllerTypeSet controllerSet = guiAttribute.value<UIUSBControllerTypeSet>();
                if (!controllerSet.contains(KUSBControllerType_Null))
                {
                    createUSBControllers(comMachine, controllerSet);
                    if (!comMachine.isOk())
                    {
                        UINotificationMessage::cannotChangeMachineParameter(comMachine);
                        fErrorHappened = true;
                    }
                }
                break;
            }
            default:
                break;
        }

        /* Error happened? */
        if (fErrorHappened)
            break;
        /* Save machine settings? */
        if (!fSaveSettings)
            break;

        /* Save machine settings: */
        comMachine.SaveSettings();
        if (!comMachine.isOk())
        {
            msgCenter().cannotSaveMachineSettings(comMachine);
            break;
        }
    }
    while (0);

    /* Close session to editable comMachine if necessary: */
    if (!comSession.isNull())
        comSession.UnlockMachine();
}

void UIMachineAttributeSetter::setMachineLocation(const QUuid &uMachineId,
                                                  const QString &strLocation)
{
    /* Move machine: */
    UINotificationProgressMachineMove *pNotification = new UINotificationProgressMachineMove(uMachineId,
                                                                                             strLocation,
                                                                                             "basic");
    gpNotificationCenter->append(pNotification);
}
