/* $Id: UIMediumItem.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumItem class implementation.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

/* GUI includes: */
#include "QIFileDialog.h"
#include "QIMessageBox.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UILocalMachineStuff.h"
#include "UIMediumItem.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UITranslator.h"

/* COM includes: */
#include "CMachine.h"
#include "CMediumAttachment.h"
#include "CMediumFormat.h"
#include "CStorageController.h"


/*********************************************************************************************************************************
*   Class UIMediumItem implementation.                                                                                           *
*********************************************************************************************************************************/

UIMediumItem::UIMediumItem(const UIMedium &guiMedium, QITreeWidget *pParent)
    : QITreeWidgetItem(pParent)
    , m_guiMedium(guiMedium)
{
    refresh();
}

UIMediumItem::UIMediumItem(const UIMedium &guiMedium, UIMediumItem *pParent)
    : QITreeWidgetItem(pParent)
    , m_guiMedium(guiMedium)
{
    refresh();
}

UIMediumItem::UIMediumItem(const UIMedium &guiMedium, QITreeWidgetItem *pParent)
    : QITreeWidgetItem(pParent)
    , m_guiMedium(guiMedium)
{
    refresh();
}

bool UIMediumItem::move()
{
    /* Open file-save dialog to choose location for current medium: */
    const QString strFileName = QIFileDialog::getSaveFileName(location(),
                                                              tr("Current extension (*.%1)")
                                                                 .arg(QFileInfo(location()).suffix()),
                                                              treeWidget(),
                                                              tr("Choose the location of this medium"),
                                                                 0, true, true);
    if (strFileName.isNull() || strFileName == location())
        return false;

    /* Search for corresponding medium: */
    CMedium comMedium = medium().medium();
    if (comMedium.isNull() || !comMedium.isOk())
        return false;

    /* Assign new medium location: */
    UINotificationProgressMediumMove *pNotification = new UINotificationProgressMediumMove(comMedium,
                                                                                           strFileName);
    connect(pNotification, &UINotificationProgressMediumMove::sigProgressFinished,
            this, &UIMediumItem::sltHandleMoveProgressFinished);
    gpNotificationCenter->append(pNotification);

    /* Positive: */
    return true;
}

bool UIMediumItem::release(bool fShowMessageBox, bool fInduced)
{
    /* Refresh medium and item: */
    m_guiMedium.refresh();
    refresh();

    /* Make sure medium was not released yet: */
    if (medium().curStateMachineIds().isEmpty())
        return true;

    /* Confirm release: */
    if (fShowMessageBox)
        if (!msgCenter().confirmMediumRelease(medium(), fInduced, treeWidget()))
            return false;

    /* Release: */
    foreach (const QUuid &uMachineId, medium().curStateMachineIds())
        if (!releaseFrom(uMachineId))
            return false;

    /* True by default: */
    return true;
}

void UIMediumItem::refreshAll()
{
    m_guiMedium.blockAndQueryState();
    refresh();
}

void UIMediumItem::setMedium(const UIMedium &guiMedium)
{
    m_guiMedium = guiMedium;
    refresh();
}

bool UIMediumItem::operator<(const QTreeWidgetItem &other) const
{
    int iColumn = treeWidget()->sortColumn();
    ULONG64 uThisValue = UITranslator::parseSize(      text(iColumn));
    ULONG64 uThatValue = UITranslator::parseSize(other.text(iColumn));
    return uThisValue && uThatValue ? uThisValue < uThatValue : QTreeWidgetItem::operator<(other);
}

bool UIMediumItem::isMediumModifiable() const
{
    if (medium().isNull())
        return false;
    if (m_enmDeviceType == UIMediumDeviceType_DVD || m_enmDeviceType == UIMediumDeviceType_Floppy)
        return false;
    foreach (const QUuid &uMachineId, medium().curStateMachineIds())
    {
        CMachine comMachine = gpGlobalSession->virtualBox().FindMachine(uMachineId.toString());
        if (comMachine.isNull())
            continue;
        if (comMachine.GetState() != KMachineState_PoweredOff &&
            comMachine.GetState() != KMachineState_Aborted &&
            comMachine.GetState() != KMachineState_AbortedSaved)
            return false;
    }
    return true;
}

bool UIMediumItem::isMediumAttachedTo(QUuid uId)
{
   if (medium().isNull())
        return false;
   return medium().curStateMachineIds().contains(uId);
}

bool UIMediumItem::changeMediumType(KMediumType enmNewType)
{
    /* Cache the list of VMs the medium is attached to. We will need this for the re-attachment: */
    QList<AttachmentCache> attachmentCacheList;
    foreach (const QUuid &uMachineId, medium().curStateMachineIds())
    {
        const CMachine &comMachine = gpGlobalSession->virtualBox().FindMachine(uMachineId.toString());
        if (comMachine.isNull())
            continue;
        foreach (const CStorageController &comController, comMachine.GetStorageControllers())
        {
            if (comController.isNull())
                continue;
            const QString strControllerName = comController.GetName();
            foreach (const CMediumAttachment &comAttachment, comMachine.GetMediumAttachmentsOfController(strControllerName))
            {
                if (comAttachment.isNull())
                    continue;
                const CMedium &comMedium = comAttachment.GetMedium();
                if (comMedium.isNull() || comMedium.GetId() != id())
                    continue;
                AttachmentCache attachmentCache;
                attachmentCache.m_uMachineId = uMachineId;
                attachmentCache.m_strControllerName = strControllerName;
                attachmentCache.m_enmControllerBus = comController.GetBus();
                attachmentCache.m_iAttachmentPort = comAttachment.GetPort();
                attachmentCache.m_iAttachmentDevice = comAttachment.GetDevice();
                attachmentCacheList << attachmentCache;
            }
        }
    }

    /* Detach the medium from all the VMs it's attached to: */
    if (!release(true, true))
        return false;

    /* Attempt to change medium type: */
    CMedium comMedium = medium().medium();
    comMedium.SetType(enmNewType);
    if (!comMedium.isOk())
    {
        UINotificationMessage::cannotChangeMediumParameter(comMedium);
        return false;
    }

    /* Reattach the medium to all the VMs it was previously attached: */
    foreach (const AttachmentCache &attachmentCache, attachmentCacheList)
        if (!attachTo(attachmentCache))
            return false;

    /* True finally: */
    return true;
}

QString UIMediumItem::defaultText() const
{
    return tr("%1, %2: %3, %4: %5", "col.1 text, col.2 name: col.2 text, col.3 name: col.3 text")
              .arg(text(0))
              .arg(parentTree()->headerItem()->text(1)).arg(text(1))
              .arg(parentTree()->headerItem()->text(2)).arg(text(2));
}

void UIMediumItem::sltHandleMoveProgressFinished()
{
    /* Recache item: */
    refreshAll();
}

void UIMediumItem::sltHandleMediumRemoveRequest(CMedium comMedium)
{
    /* Close medium finally: */
    comMedium.Close();
    if (!comMedium.isOk())
        UINotificationMessage::cannotCloseMedium(comMedium);
}

void UIMediumItem::refresh()
{
    /* Fill-in columns: */
    setIcon(0, m_guiMedium.icon());
    setText(0, m_guiMedium.name());
    setText(1, m_guiMedium.logicalSize());
    setText(2, m_guiMedium.size());
    /* All columns get the same tooltip: */
    QString strToolTip = m_guiMedium.toolTip();
    for (int i = 0; i < treeWidget()->columnCount(); ++i)
        setToolTip(i, strToolTip);

    /* Gather medium data: */
    m_fValid =    !m_guiMedium.isNull()
               && m_guiMedium.state() != KMediumState_Inaccessible;
    m_enmDeviceType = m_guiMedium.type();
    m_enmVariant = m_guiMedium.mediumVariant();
    m_fHasChildren = m_guiMedium.hasChildren();
    /* Gather medium options data: */
    m_options.m_enmMediumType = m_guiMedium.mediumType();
    m_options.m_strLocation = m_guiMedium.location();
    m_options.m_uLogicalSize = m_guiMedium.logicalSizeInBytes();
    m_options.m_strDescription = m_guiMedium.description();
    /* Gather medium details data: */
    m_details.m_aFields.clear();
    switch (m_enmDeviceType)
    {
        case UIMediumDeviceType_HardDisk:
        {
            m_details.m_aLabels << tr("Format:");
            m_details.m_aLabels << tr("Storage details:");
            m_details.m_aLabels << tr("Attached to:");
            m_details.m_aLabels << tr("Encryption key:");
            m_details.m_aLabels << tr("UUID:");

            m_details.m_aFields << hardDiskFormat();
            m_details.m_aFields << details();
            m_details.m_aFields << (usage().isNull() ?
                                    formatFieldText(tr("<i>Not&nbsp;Attached</i>"), false) :
                                    formatFieldText(usage()));
            m_details.m_aFields << (encryptionPasswordID().isNull() ?
                                    formatFieldText(tr("<i>Not&nbsp;Encrypted</i>"), false) :
                                    formatFieldText(encryptionPasswordID()));
            m_details.m_aFields << id().toString();

            break;
        }
        case UIMediumDeviceType_DVD:
        case UIMediumDeviceType_Floppy:
        {
            m_details.m_aLabels << tr("Attached to:");
            m_details.m_aLabels << tr("UUID:");

            m_details.m_aFields << (usage().isNull() ?
                                    formatFieldText(tr("<i>Not&nbsp;Attached</i>"), false) :
                                    formatFieldText(usage()));
            m_details.m_aFields << id().toString();
            break;
        }
        default:
            break;
    }
}

bool UIMediumItem::releaseFrom(const QUuid &uMachineId)
{
    /* Open session: */
    CSession session = openSession(uMachineId);
    if (session.isNull())
        return false;

    /* Get machine: */
    CMachine machine = session.GetMachine();

    /* Prepare result: */
    bool fSuccess = false;

    /* Release medium from machine: */
    if (releaseFrom(machine))
    {
        /* Save machine settings: */
        machine.SaveSettings();
        if (!machine.isOk())
            msgCenter().cannotSaveMachineSettings(machine, treeWidget());
        else
            fSuccess = true;
    }

    /* Close session: */
    session.UnlockMachine();

    /* Return result: */
    return fSuccess;
}

bool UIMediumItem::attachTo(const AttachmentCache &attachmentCache)
{
    /* Open session: */
    CSession comSession = openSession(attachmentCache.m_uMachineId);
    if (comSession.isNull())
        return false;

    /* Attach device to machine: */
    CMedium comMedium = medium().medium();
    KDeviceType enmDeviceType = comMedium.GetDeviceType();
    CMachine comMachine = comSession.GetMachine();
    comMachine.AttachDevice(attachmentCache.m_strControllerName,
                            attachmentCache.m_iAttachmentPort,
                            attachmentCache.m_iAttachmentDevice,
                            enmDeviceType,
                            comMedium);
    if (!comMachine.isOk())
        msgCenter().cannotAttachDevice(comMachine,
                                       mediumTypeToLocal(enmDeviceType),
                                       comMedium.GetLocation(),
                                       StorageSlot(attachmentCache.m_enmControllerBus,
                                                   attachmentCache.m_iAttachmentPort,
                                                   attachmentCache.m_iAttachmentDevice),
                                       parentTree());
    else
    {
        /* Save machine settings: */
        comMachine.SaveSettings();
        if (!comMachine.isOk())
            msgCenter().cannotSaveMachineSettings(comMachine, parentTree());
    }

    /* Close session: */
    comSession.UnlockMachine();

    /* True finally: */
    return true;
}

/* static */
QString UIMediumItem::formatFieldText(const QString &strText, bool fCompact /* = true */,
                                      const QString &strElipsis /* = "middle" */)
{
    QString strCompactString = QString("<compact elipsis=\"%1\">").arg(strElipsis);
    QString strInfo = QString("<nobr>%1%2%3</nobr>")
                              .arg(fCompact ? strCompactString : "")
                              .arg(strText.isEmpty() ? tr("--", "no info") : strText)
                              .arg(fCompact ? "</compact>" : "");
    return strInfo;
}


/*********************************************************************************************************************************
*   Class UIMediumItemHD implementation.                                                                                         *
*********************************************************************************************************************************/

UIMediumItemHD::UIMediumItemHD(const UIMedium &guiMedium, QITreeWidget *pParent)
    : UIMediumItem(guiMedium, pParent)
{
}

UIMediumItemHD::UIMediumItemHD(const UIMedium &guiMedium, UIMediumItem *pParent)
    : UIMediumItem(guiMedium, pParent)
{
}

UIMediumItemHD::UIMediumItemHD(const UIMedium &guiMedium, QITreeWidgetItem *pParent)
    : UIMediumItem(guiMedium, pParent)
{
}

bool UIMediumItemHD::remove(bool fShowMessageBox)
{
    /* Confirm medium removal: */
    if (fShowMessageBox)
        if (!msgCenter().confirmMediumRemoval(medium(), treeWidget()))
            return false;

    /* Propose to remove medium storage: */
    if (!maybeRemoveStorage())
        return false;

    /* True by default: */
    return true;
}

bool UIMediumItemHD::releaseFrom(CMachine comMachine)
{
    /* Was medium released from at least one attachment? */
    bool fAtLeastOneRelease = false;

    /* Enumerate attachments: */
    CMediumAttachmentVector attachments = comMachine.GetMediumAttachments();
    foreach (const CMediumAttachment &attachment, attachments)
    {
        /* Skip non-hard-disks: */
        if (attachment.GetType() != KDeviceType_HardDisk)
            continue;

        /* Skip unrelated hard-disks: */
        if (attachment.GetMedium().GetId() != id())
            continue;

        /* Remember controller: */
        CStorageController controller = comMachine.GetStorageControllerByName(attachment.GetController());

        /* Try to detach device: */
        comMachine.DetachDevice(attachment.GetController(), attachment.GetPort(), attachment.GetDevice());
        if (!comMachine.isOk())
        {
            /* Return failure: */
            msgCenter().cannotDetachDevice(comMachine, UIMediumDeviceType_HardDisk, location(),
                                           StorageSlot(controller.GetBus(), attachment.GetPort(), attachment.GetDevice()),
                                           treeWidget());
            return false;
        }
        else
            fAtLeastOneRelease = true;
    }

    /* Return whether there was at least one release: */
    return fAtLeastOneRelease;
}

bool UIMediumItemHD::maybeRemoveStorage()
{
    /* Acquire medium: */
    CMedium comMedium = medium().medium();

    /* We don't want to try to delete inaccessible storage as it will most likely fail.
     * Note that UIMessageCenter::confirmMediumRemoval() is aware of that and
     * will give a corresponding hint. Therefore, once the code is changed below,
     * the hint should be re-checked for validity. */
    bool fDeleteStorage = false;
    qulonglong uCapability = 0;
    foreach (KMediumFormatCapabilities capability, comMedium.GetMediumFormat().GetCapabilities())
        uCapability |= capability;
    if (state() != KMediumState_Inaccessible && uCapability & KMediumFormatCapabilities_File)
    {
        int rc = msgCenter().confirmDeleteHardDiskStorage(location(), treeWidget());
        if (rc == AlertButton_Cancel)
            return false;
        fDeleteStorage = rc == AlertButton_Choice1;
    }

    /* If user wish to delete storage: */
    if (fDeleteStorage)
    {
        /* Deleting medium storage first of all: */
        UINotificationProgressMediumDeletingStorage *pNotification = new UINotificationProgressMediumDeletingStorage(comMedium);
        connect(pNotification, &UINotificationProgressMediumDeletingStorage::sigMediumStorageDeleted,
                this, &UIMediumItemHD::sltHandleMediumRemoveRequest);
        gpNotificationCenter->append(pNotification);
    }
    /* Otherwise go to last step immediatelly: */
    else
        sltHandleMediumRemoveRequest(comMedium);

    /* True by default: */
    return true;
}


/*********************************************************************************************************************************
*   Class UIMediumItemCD implementation.                                                                                         *
*********************************************************************************************************************************/

UIMediumItemCD::UIMediumItemCD(const UIMedium &guiMedium, QITreeWidget *pParent)
    : UIMediumItem(guiMedium, pParent)
{
}

UIMediumItemCD::UIMediumItemCD(const UIMedium &guiMedium, QITreeWidgetItem *pParent)
    : UIMediumItem(guiMedium, pParent)
{
}

bool UIMediumItemCD::remove(bool fShowMessageBox)
{
    /* Confirm medium removal: */
    if (fShowMessageBox)
        if (!msgCenter().confirmMediumRemoval(medium(), treeWidget()))
            return false;

    /* Close optical-disk: */
    sltHandleMediumRemoveRequest(medium().medium());

    /* True by default: */
    return true;
}

bool UIMediumItemCD::releaseFrom(CMachine comMachine)
{
    /* Was medium released from at least one attachment? */
    bool fAtLeastOneRelease = false;

    /* Enumerate attachments: */
    CMediumAttachmentVector attachments = comMachine.GetMediumAttachments();
    foreach (const CMediumAttachment &attachment, attachments)
    {
        /* Skip non-optical-disks: */
        if (attachment.GetType() != KDeviceType_DVD)
            continue;

        /* Skip unrelated optical-disks: */
        if (attachment.GetMedium().GetId() != id())
            continue;

        /* Try to unmount device: */
        comMachine.MountMedium(attachment.GetController(), attachment.GetPort(), attachment.GetDevice(), CMedium(), false /* force */);
        if (!comMachine.isOk())
        {
            /* Return failure: */
            msgCenter().cannotRemountMedium(comMachine, medium(), false /* mount? */, false /* retry? */, treeWidget());
            return false;
        }
        else
            fAtLeastOneRelease = true;
    }

    /* Return whether there was at least one release: */
    return fAtLeastOneRelease;
}


/*********************************************************************************************************************************
*   Class UIMediumItemFD implementation.                                                                                         *
*********************************************************************************************************************************/

UIMediumItemFD::UIMediumItemFD(const UIMedium &guiMedium, QITreeWidget *pParent)
    : UIMediumItem(guiMedium, pParent)
{
}

UIMediumItemFD::UIMediumItemFD(const UIMedium &guiMedium, QITreeWidgetItem *pParent)
    : UIMediumItem(guiMedium, pParent)
{
}

bool UIMediumItemFD::remove(bool fShowMessageBox)
{
    /* Confirm medium removal: */
    if (fShowMessageBox)
        if (!msgCenter().confirmMediumRemoval(medium(), treeWidget()))
            return false;

    /* Close floppy-disk: */
    sltHandleMediumRemoveRequest(medium().medium());

    /* True by default: */
    return true;
}

bool UIMediumItemFD::releaseFrom(CMachine comMachine)
{
    /* Was medium released from at least one attachment? */
    bool fAtLeastOneRelease = false;

    /* Enumerate attachments: */
    CMediumAttachmentVector attachments = comMachine.GetMediumAttachments();
    foreach (const CMediumAttachment &attachment, attachments)
    {
        /* Skip non-floppy-disks: */
        if (attachment.GetType() != KDeviceType_Floppy)
            continue;

        /* Skip unrelated floppy-disks: */
        if (attachment.GetMedium().GetId() != id())
            continue;

        /* Try to unmount device: */
        comMachine.MountMedium(attachment.GetController(), attachment.GetPort(), attachment.GetDevice(), CMedium(), false /* force */);
        if (!comMachine.isOk())
        {
            /* Return failure: */
            msgCenter().cannotRemountMedium(comMachine, medium(), false /* mount? */, false /* retry? */, treeWidget());
            return false;
        }
        else
            fAtLeastOneRelease = true;
    }

    /* Return whether there was at least one release: */
    return fAtLeastOneRelease;
}
