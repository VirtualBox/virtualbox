/* $Id: UIWizardCloneVM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardCloneVM class implementation.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include "UICommon.h"
#include "UIGlobalSession.h"
#include "UINotificationCenter.h"
#include "UIWizardCloneVM.h"
#include "UIWizardCloneVMNamePathPage.h"
#include "UIWizardCloneVMTypePage.h"
#include "UIWizardCloneVMModePage.h"
#include "UIWizardCloneVMExpertPage.h"

/* COM includes: */
#include "CSystemProperties.h"


UIWizardCloneVM::UIWizardCloneVM(QWidget *pParent, const CMachine &machine,
                                 const QString &strGroup, CSnapshot snapshot /* = CSnapshot() */)
    : UINativeWizard(pParent, WizardType_CloneVM, "clone" /* help keyword */)
    , m_machine(machine)
    , m_snapshot(snapshot)
    , m_strGroup(strGroup)
    , m_iCloneModePageIndex(-1)
    , m_strCloneName(!machine.isNull() ? machine.GetName() : QString())
    , m_enmCloneMode(KCloneMode_MachineState)
{
#ifndef VBOX_WS_MAC
    /* Assign watermark: */
    setPixmapName(":/wizard_clone.png");
#else /* VBOX_WS_MAC */
    /* Assign background image: */
    setPixmapName(":/wizard_clone_bg.png");
#endif /* VBOX_WS_MAC */
}

void UIWizardCloneVM::setCloneModePageVisible(bool fIsFullClone)
{
    if (m_iCloneModePageIndex == -1)
        return;
    setPageVisible(m_iCloneModePageIndex, fIsFullClone);
}

bool UIWizardCloneVM::isCloneModePageVisible() const
{
    /* If we did not create the clone mode page return false: */
    if (m_iCloneModePageIndex == -1)
        return false;
    return isPageVisible(m_iCloneModePageIndex);
}

void UIWizardCloneVM::setCloneName(const QString &strCloneName)
{
    m_strCloneName = strCloneName;
}

const QString &UIWizardCloneVM::cloneName() const
{
    return m_strCloneName;
}

void UIWizardCloneVM::setCloneFilePath(const QString &strCloneFilePath)
{
    m_strCloneFilePath = strCloneFilePath;
}

const QString &UIWizardCloneVM::cloneFilePath() const
{
    return m_strCloneFilePath;
}

MACAddressClonePolicy UIWizardCloneVM::macAddressClonePolicy() const
{
    return m_enmMACAddressClonePolicy;
}

void UIWizardCloneVM::setMacAddressPolicy(MACAddressClonePolicy enmMACAddressClonePolicy)
{
    m_enmMACAddressClonePolicy = enmMACAddressClonePolicy;
}

bool UIWizardCloneVM::keepDiskNames() const
{
    return m_fKeepDiskNames;
}

void UIWizardCloneVM::setKeepDiskNames(bool fKeepDiskNames)
{
    m_fKeepDiskNames = fKeepDiskNames;
}

bool UIWizardCloneVM::keepHardwareUUIDs() const
{
    return m_fKeepHardwareUUIDs;
}

void UIWizardCloneVM::setKeepHardwareUUIDs(bool fKeepHardwareUUIDs)
{
    m_fKeepHardwareUUIDs = fKeepHardwareUUIDs;
}

bool UIWizardCloneVM::linkedClone() const
{
    return m_fLinkedClone;
}

void UIWizardCloneVM::setLinkedClone(bool fLinkedClone)
{
    m_fLinkedClone = fLinkedClone;
}

KCloneMode UIWizardCloneVM::cloneMode() const
{
    return m_enmCloneMode;
}

void UIWizardCloneVM::setCloneMode(KCloneMode enmCloneMode)
{
    m_enmCloneMode = enmCloneMode;
}

bool UIWizardCloneVM::machineHasSnapshot() const
{
    AssertReturn(!m_machine.isNull(), false);
    return m_machine.GetSnapshotCount() > 0;
}

bool UIWizardCloneVM::cloneVM()
{
    /* Prepare machine for cloning: */
    CMachine srcMachine = m_machine;

    /* If the user like to create a linked clone from the current machine, we have to take a little bit more action.
     * First we create an snapshot, so that new differencing images on the source VM are created. Based on that we
     * could use the new snapshot machine for cloning. */
    if (m_fLinkedClone && m_snapshot.isNull())
    {
        /* Compose snapshot name: */
        const QString strSnapshotName = tr("Linked Base for %1 and %2").arg(m_machine.GetName()).arg(m_strCloneName);

        /* Take the snapshot: */
        UINotificationProgressSnapshotTake *pNotification = new UINotificationProgressSnapshotTake(srcMachine,
                                                                                                   strSnapshotName,
                                                                                                   QString());
        UINotificationReceiver receiver;
        connect(pNotification, &UINotificationProgressSnapshotTake::sigSnapshotTaken,
                &receiver, &UINotificationReceiver::setReceiverProperty);
        if (!handleNotificationProgressNow(pNotification))
            return false;

        /* Acquire created snapshot id: */
        QUuid uSnapshotId = receiver.property("received_value").toUuid();

        /* Look for created snapshot: */
        const CSnapshot comCreatedSnapshot = m_machine.FindSnapshot(uSnapshotId.toString());
        if (comCreatedSnapshot.isNull())
        {
            UINotificationMessage::cannotFindSnapshotByName(m_machine, strSnapshotName, notificationCenter());
            return false;
        }

        /* Update machine for cloning finally: */
        srcMachine = comCreatedSnapshot.GetMachine();
    }

    /* Get VBox object: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();
    /* Create a new machine object: */
    CMachine cloneMachine = comVBox.CreateMachine(m_strCloneFilePath, m_strCloneName, KPlatformArchitecture_x86,
                                                  QVector<QString>(), QString(), QString(),
                                                  QString(), QString(), QString());
    if (!comVBox.isOk())
    {
        UINotificationMessage::cannotCreateMachine(comVBox, notificationCenter());
        return false;
    }

    /* Clone options vector to pass to cloning: */
    QVector<KCloneOptions> options;
    /* Set the selected MAC address policy: */
    switch (m_enmMACAddressClonePolicy)
    {
        case MACAddressClonePolicy_KeepAllMACs:
            options.append(KCloneOptions_KeepAllMACs);
            break;
        case MACAddressClonePolicy_KeepNATMACs:
            options.append(KCloneOptions_KeepNATMACs);
            break;
        default:
            break;
    }
    if (m_fKeepDiskNames)
        options.append(KCloneOptions_KeepDiskNames);
    if (m_fKeepHardwareUUIDs)
        options.append(KCloneOptions_KeepHwUUIDs);
    /* Linked clones requested? */
    if (m_fLinkedClone)
        options.append(KCloneOptions_Link);

    /* Clone VM: */
    UINotificationProgressMachineCopy *pNotification = new UINotificationProgressMachineCopy(srcMachine,
                                                                                             cloneMachine,
                                                                                             m_enmCloneMode,
                                                                                             options);
    gpNotificationCenter->append(pNotification);

    return true;
}

void UIWizardCloneVM::sltRetranslateUI()
{
    /* Call to base-class: */
    UINativeWizard::sltRetranslateUI();

    /* Translate wizard: */
    setWindowTitle(tr("Clone Virtual Machine"));
}

void UIWizardCloneVM::populatePages()
{
    QString strDefaultMachineFolder = gpGlobalSession->virtualBox().GetSystemProperties().GetDefaultMachineFolder();
    /* Create corresponding pages: */
    switch (mode())
    {
        case WizardMode_Basic:
        {
            addPage(new UIWizardCloneVMNamePathPage(m_strCloneName, strDefaultMachineFolder, m_strGroup));
            addPage(new UIWizardCloneVMTypePage(m_snapshot.isNull()));
            if (machineHasSnapshot())
                m_iCloneModePageIndex = addPage(new UIWizardCloneVMModePage(m_snapshot.isNull() ? false : m_snapshot.GetChildrenCount() > 0));
            break;
        }
        case WizardMode_Expert:
        {
            addPage(new UIWizardCloneVMExpertPage(m_machine.GetName(),
                                                  strDefaultMachineFolder,
                                                  m_snapshot.isNull(),
                                                  m_snapshot.isNull() ? false : m_snapshot.GetChildrenCount() > 0,
                                                  m_strGroup));
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid mode: %d", mode()));
            break;
        }
    }
}
