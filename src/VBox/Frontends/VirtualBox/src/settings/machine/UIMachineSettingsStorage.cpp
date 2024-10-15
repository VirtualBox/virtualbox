/* $Id: UIMachineSettingsStorage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsStorage class implementation.
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

/* Qt includes: */
#include <QVBoxLayout>

/* GUI includes: */
#include "UIConverter.h"
#include "UIErrorString.h"
#include "UIMachineSettingsStorage.h"
#include "UIMedium.h"
#include "UIMediumEnumerator.h"
#include "UIStorageSettingsEditor.h"

/* COM includes: */
#include "CMediumAttachment.h"
#include "CStorageController.h"


/** Machine settings: Storage Attachment data structure. */
struct UIDataSettingsMachineStorageAttachment
{
    /** Constructs data. */
    UIDataSettingsMachineStorageAttachment() {}

    /** Returns whether @a another passed data is equal to this one. */
    bool equal(const UIDataSettingsMachineStorageAttachment &another) const
    {
        return true
               && (m_guiValue == another.m_guiValue)
               ;
    }

    /** Returns whether @a another passed data is equal to this one. */
    bool operator==(const UIDataSettingsMachineStorageAttachment &another) const { return equal(another); }
    /** Returns whether @a another passed data is different from this one. */
    bool operator!=(const UIDataSettingsMachineStorageAttachment &another) const { return !equal(another); }

    /** Holds the storage attachment data. */
    UIDataStorageAttachment  m_guiValue;
};


/** Machine settings: Storage Controller data structure. */
struct UIDataSettingsMachineStorageController
{
    /** Constructs data. */
    UIDataSettingsMachineStorageController() {}

    /** Returns whether @a another passed data is equal to this one. */
    bool equal(const UIDataSettingsMachineStorageController &another) const
    {
        return true
               && (m_guiValue == another.m_guiValue)
               ;
    }

    /** Returns whether @a another passed data is equal to this one. */
    bool operator==(const UIDataSettingsMachineStorageController &another) const { return equal(another); }
    /** Returns whether @a another passed data is different from this one. */
    bool operator!=(const UIDataSettingsMachineStorageController &another) const { return !equal(another); }

    /** Holds the storage controller data. */
    UIDataStorageController  m_guiValue;
};


/** Machine settings: Storage page data structure. */
struct UIDataSettingsMachineStorage
{
    /** Constructs data. */
    UIDataSettingsMachineStorage() {}

    /** Returns whether @a another passed data is equal to this one. */
    bool operator==(const UIDataSettingsMachineStorage & /* another */) const { return true; }
    /** Returns whether @a another passed data is different from this one. */
    bool operator!=(const UIDataSettingsMachineStorage & /* another */) const { return false; }
};


/*********************************************************************************************************************************
*   Class UIMachineSettingsStorage implementation.                                                                               *
*********************************************************************************************************************************/

UIMachineSettingsStorage::UIMachineSettingsStorage(UIActionPool *pActionPool)
    : m_pActionPool(pActionPool)
    , m_pCache(0)
    , m_pEditorStorageSettings(0)
{
    prepare();
}

UIMachineSettingsStorage::~UIMachineSettingsStorage()
{
    cleanup();
}

void UIMachineSettingsStorage::setChipsetType(KChipsetType enmType)
{
    if (m_pEditorStorageSettings)
        m_pEditorStorageSettings->setChipsetType(enmType);
}

bool UIMachineSettingsStorage::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIMachineSettingsStorage::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Prepare old data: */
    UIDataSettingsMachineStorage oldStorageData;

    /* Gather old data: */
    m_uMachineId = m_machine.GetId();
    m_strMachineName = m_machine.GetName();
    m_strMachineSettingsFilePath = m_machine.GetSettingsFilePath();
    m_strMachineGuestOSTypeId = m_machine.GetOSTypeId();

    /* For each controller: */
    const CStorageControllerVector &controllers = m_machine.GetStorageControllers();
    for (int iControllerIndex = 0; iControllerIndex < controllers.size(); ++iControllerIndex)
    {
        /* Prepare old data & cache key: */
        UIDataSettingsMachineStorageController oldControllerData;
        QString strControllerKey = QString::number(iControllerIndex);

        /* Check whether controller is valid: */
        const CStorageController &comController = controllers.at(iControllerIndex);
        if (!comController.isNull())
        {
            /* Gather old data: */
            oldControllerData.m_guiValue.m_strName = comController.GetName();
            oldControllerData.m_guiValue.m_enmBus = comController.GetBus();
            oldControllerData.m_guiValue.m_enmType = comController.GetControllerType();
            oldControllerData.m_guiValue.m_uPortCount = comController.GetPortCount();
            oldControllerData.m_guiValue.m_fUseHostIOCache = comController.GetUseHostIOCache();
            oldControllerData.m_guiValue.m_strKey = oldControllerData.m_guiValue.m_strName;
            /* Override controller cache key: */
            strControllerKey = oldControllerData.m_guiValue.m_strKey;

            /* Sort attachments before caching/fetching: */
            const CMediumAttachmentVector &attachmentVector =
                m_machine.GetMediumAttachmentsOfController(oldControllerData.m_guiValue.m_strName);
            QMap<StorageSlot, CMediumAttachment> attachmentMap;
            foreach (const CMediumAttachment &comAttachment, attachmentVector)
            {
                const StorageSlot storageSlot(oldControllerData.m_guiValue.m_enmBus,
                                              comAttachment.GetPort(), comAttachment.GetDevice());
                attachmentMap.insert(storageSlot, comAttachment);
            }
            const QList<CMediumAttachment> &attachments = attachmentMap.values();

            /* For each attachment: */
            for (int iAttachmentIndex = 0; iAttachmentIndex < attachments.size(); ++iAttachmentIndex)
            {
                /* Prepare old data & cache key: */
                UIDataSettingsMachineStorageAttachment oldAttachmentData;
                QString strAttachmentKey = QString::number(iAttachmentIndex);

                /* Check whether attachment is valid: */
                const CMediumAttachment &comAttachment = attachments.at(iAttachmentIndex);
                if (!comAttachment.isNull())
                {
                    /* Gather old data: */
                    oldAttachmentData.m_guiValue.m_enmDeviceType = comAttachment.GetType();
                    oldAttachmentData.m_guiValue.m_iPort = comAttachment.GetPort();
                    oldAttachmentData.m_guiValue.m_iDevice = comAttachment.GetDevice();
                    oldAttachmentData.m_guiValue.m_fPassthrough = comAttachment.GetPassthrough();
                    oldAttachmentData.m_guiValue.m_fTempEject = comAttachment.GetTemporaryEject();
                    oldAttachmentData.m_guiValue.m_fNonRotational = comAttachment.GetNonRotational();
                    oldAttachmentData.m_guiValue.m_fHotPluggable = comAttachment.GetHotPluggable();
                    const CMedium comMedium = comAttachment.GetMedium();
                    oldAttachmentData.m_guiValue.m_uMediumId = comMedium.isNull() ? UIMedium::nullID() : comMedium.GetId();
                    oldAttachmentData.m_guiValue.m_strKey = QString("%1:%2").arg(oldAttachmentData.m_guiValue.m_iPort)
                                                                            .arg(oldAttachmentData.m_guiValue.m_iDevice);
                    /* Override attachment cache key: */
                    strAttachmentKey = oldAttachmentData.m_guiValue.m_strKey;
                }

                /* Cache old data: */
                m_pCache->child(strControllerKey).child(strAttachmentKey).cacheInitialData(oldAttachmentData);
            }
        }

        /* Cache old data: */
        m_pCache->child(strControllerKey).cacheInitialData(oldControllerData);
    }

    /* Cache old data: */
    m_pCache->cacheInitialData(oldStorageData);

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

void UIMachineSettingsStorage::getFromCache()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pEditorStorageSettings)
        return;

    /* Load old data from cache: */
    m_pEditorStorageSettings->setMachineId(m_uMachineId);
    m_pEditorStorageSettings->setMachineName(m_strMachineName);
    m_pEditorStorageSettings->setMachineSettingsFilePath(m_strMachineSettingsFilePath);
    m_pEditorStorageSettings->setMachineGuestOSTypeId(m_strMachineGuestOSTypeId);

    /* Load old data from cache: */
    QList<UIDataStorageController> controllers;
    QList<QList<UIDataStorageAttachment> > attachments;

    /* For each controller: */
    for (int iControllerIndex = 0; iControllerIndex < m_pCache->childCount(); ++iControllerIndex)
    {
        /* Get controller cache: */
        const UISettingsCacheMachineStorageController &controllerCache = m_pCache->child(iControllerIndex);
        /* Get old data from cache: */
        const UIDataSettingsMachineStorageController &oldControllerData = controllerCache.base();

        /* For each attachment: */
        QList<UIDataStorageAttachment> controllerAttachments;
        for (int iAttachmentIndex = 0; iAttachmentIndex < controllerCache.childCount(); ++iAttachmentIndex)
        {
            /* Get attachment cache: */
            const UISettingsCacheMachineStorageAttachment &attachmentCache = controllerCache.child(iAttachmentIndex);
            /* Get old data from cache: */
            const UIDataSettingsMachineStorageAttachment &oldAttachmentData = attachmentCache.base();

            /* Append controller's attachment: */
            controllerAttachments << oldAttachmentData.m_guiValue;
        }

        /* Append controller & controller's attachments: */
        controllers << oldControllerData.m_guiValue;
        attachments << controllerAttachments;
    }

    /* Set to editor: */
    m_pEditorStorageSettings->setValue(controllers, attachments);

    /* Polish page finally: */
    polishPage();

    /* Revalidate: */
    revalidate();
}

void UIMachineSettingsStorage::putToCache()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pEditorStorageSettings)
        return;

    /* Prepare new data: */
    UIDataSettingsMachineStorage newStorageData;

    /* Save new data to cache: */
    QList<UIDataStorageController> controllers;
    QList<QList<UIDataStorageAttachment> > attachments;

    /* Get from editor: */
    m_pEditorStorageSettings->getValue(controllers, attachments);

    /* For each controller: */
    for (int iControllerIndex = 0; iControllerIndex < controllers.size(); ++iControllerIndex)
    {
        /* Acquire controller: */
        const UIDataStorageController &controller = controllers.at(iControllerIndex);

        /* Gather new data & cache key from model: */
        UIDataSettingsMachineStorageController newControllerData;
        newControllerData.m_guiValue = controller;
        const QString strControllerKey = newControllerData.m_guiValue.m_strKey;

        /* For each attachment: */
        const QList<UIDataStorageAttachment> &controllerAttachments = attachments.at(iControllerIndex);
        for (int iAttachmentIndex = 0; iAttachmentIndex < controllerAttachments.size(); ++iAttachmentIndex)
        {
            /* Acquire attachment: */
            const UIDataStorageAttachment &attachment = controllerAttachments.at(iAttachmentIndex);

            /* Gather new data & cache key from model: */
            UIDataSettingsMachineStorageAttachment newAttachmentData;
            newAttachmentData.m_guiValue = attachment;
            const QString strAttachmentKey = newAttachmentData.m_guiValue.m_strKey;

            /* Cache new data: */
            m_pCache->child(strControllerKey).child(strAttachmentKey).cacheCurrentData(newAttachmentData);
        }

        /* Cache new data: */
        m_pCache->child(strControllerKey).cacheCurrentData(newControllerData);
    }

    /* Cache new data: */
    m_pCache->cacheCurrentData(newStorageData);
}

void UIMachineSettingsStorage::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

bool UIMachineSettingsStorage::validate(QList<UIValidationMessage> &messages)
{
    /* Pass by default: */
    bool fPass = true;

    /* Prepare message: */
    UIValidationMessage message;

    /* Save new data to cache: */
    QList<UIDataStorageController> controllers;
    QList<QList<UIDataStorageAttachment> > attachments;

    /* Get from editor: */
    m_pEditorStorageSettings->getValue(controllers, attachments);

    /* Check controllers for name emptiness & coincidence.
     * Check attachments for the hd presence / uniqueness. */
    QMap<QString, QString> config;
    QMap<int, QString> names;
    /* For each controller: */
    for (int iControllerIndex = 0; iControllerIndex < controllers.size(); ++iControllerIndex)
    {
        const UIDataStorageController &controller = controllers.at(iControllerIndex);
        const QString strControllerName = controller.m_strName;

        /* Check for name emptiness: */
        if (strControllerName.isEmpty())
        {
            message.second << tr("No name is currently specified for the controller at position <b>%1</b>.")
                                 .arg(iControllerIndex + 1);
            fPass = false;
        }
        /* Check for name coincidence: */
        if (names.values().contains(strControllerName))
        {
            message.second << tr("The controller at position <b>%1</b> has the same name as the controller at position <b>%2</b>.")
                                 .arg(iControllerIndex + 1).arg(names.key(strControllerName) + 1);
            fPass = false;
        }
        else
            names.insert(iControllerIndex, strControllerName);

        /* For each attachment: */
        const QList<UIDataStorageAttachment> &controllerAttachments = attachments.at(iControllerIndex);
        for (int iAttachmentIndex = 0; iAttachmentIndex < controllerAttachments.size(); ++iAttachmentIndex)
        {
            const UIDataStorageAttachment &attachment = controllerAttachments.at(iAttachmentIndex);
            const StorageSlot guiAttachmentSlot = StorageSlot(controller.m_enmBus, attachment.m_iPort, attachment.m_iDevice);
            const KDeviceType enmDeviceType = attachment.m_enmDeviceType;
            const QString strKey = attachment.m_uMediumId.toString();
            const QString strValue = QString("%1 (%2)").arg(strControllerName, gpConverter->toString(guiAttachmentSlot));
            /* Check for emptiness: */
            if (gpMediumEnumerator->medium(QUuid(strKey)).isNull() && enmDeviceType == KDeviceType_HardDisk)
            {
                message.second << tr("No hard disk is selected for <i>%1</i>.")
                                     .arg(strValue);
                fPass = false;
            }
            /* Check for coincidence: */
            if (!gpMediumEnumerator->medium(QUuid(strKey)).isNull() && config.contains(strKey) && enmDeviceType != KDeviceType_DVD)
            {
                message.second << tr("<i>%1</i> is using a disk that is already attached to <i>%2</i>.")
                                     .arg(strValue).arg(config[strKey]);
                fPass = false;
            }
            else
                config.insert(strKey, strValue);
        }
    }

    /* Check for excessive controllers on Storage page controllers list: */
    QStringList excessiveList;
    const QMap<KStorageBus, int> currentType = m_pEditorStorageSettings->currentControllerTypes();
    const QMap<KStorageBus, int> maximumType = m_pEditorStorageSettings->maximumControllerTypes();
    for (int iStorageBusType = KStorageBus_IDE; iStorageBusType < KStorageBus_Max; ++iStorageBusType)
    {
        if (currentType[(KStorageBus)iStorageBusType] > maximumType[(KStorageBus)iStorageBusType])
        {
            QString strExcessiveRecord = QString("%1 (%2)");
            strExcessiveRecord = strExcessiveRecord.arg(QString("<b>%1</b>").arg(gpConverter->toString((KStorageBus)iStorageBusType)));
            strExcessiveRecord = strExcessiveRecord.arg(maximumType[(KStorageBus)iStorageBusType] == 1 ?
                                                        tr("at most one supported", "controller") :
                                                        tr("up to %1 supported", "controllers").arg(maximumType[(KStorageBus)iStorageBusType]));
            excessiveList << strExcessiveRecord;
        }
    }
    if (!excessiveList.isEmpty())
    {
        message.second << tr("The machine currently has more storage controllers assigned than a %1 chipset supports. "
                             "Please change the chipset type on the System settings page or reduce the number "
                             "of the following storage controllers on the Storage settings page: %2")
                             .arg(gpConverter->toString(m_pEditorStorageSettings->chipsetType()))
                             .arg(excessiveList.join(", "));
        fPass = false;
    }

    /* Serialize message: */
    if (!message.second.isEmpty())
        messages << message;

    /* Return result: */
    return fPass;
}

void UIMachineSettingsStorage::setConfigurationAccessLevel(ConfigurationAccessLevel enmLevel)
{
    /* Update model 'configuration access level': */
    m_pEditorStorageSettings->setConfigurationAccessLevel(enmLevel);
    /* Update 'configuration access level' of base class: */
    UISettingsPageMachine::setConfigurationAccessLevel(enmLevel);
}

void UIMachineSettingsStorage::sltRetranslateUI()
{
}

void UIMachineSettingsStorage::polishPage()
{
    m_pEditorStorageSettings->setConfigurationAccessLevel(configurationAccessLevel());
}

void UIMachineSettingsStorage::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheMachineStorage;
    AssertPtrReturnVoid(m_pCache);

    /* Start full medium-enumeration (if necessary): */
    if (!gpMediumEnumerator->isFullMediumEnumerationRequested())
        gpMediumEnumerator->enumerateMedia();

    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIMachineSettingsStorage::prepareWidgets()
{
    /* Create main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Create storage settings editor: */
        m_pEditorStorageSettings = new UIStorageSettingsEditor(this);
        if (m_pEditorStorageSettings)
        {
            addEditor(m_pEditorStorageSettings);
            m_pEditorStorageSettings->setActionPool(m_pActionPool);
            pLayout->addWidget(m_pEditorStorageSettings);
        }
    }
}

void UIMachineSettingsStorage::prepareConnections()
{
    connect(m_pEditorStorageSettings, &UIStorageSettingsEditor::sigValueChanged,
            this, &UIMachineSettingsStorage::revalidate);
}

void UIMachineSettingsStorage::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIMachineSettingsStorage::saveData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save storage settings from cache: */
    if (fSuccess && isMachineInValidMode() && m_pCache->wasChanged())
    {
        /* For each controller ('removing' step): */
        // We need to separately remove controllers first because
        // there could be limited amount of controllers available.
        for (int iControllerIndex = 0; fSuccess && iControllerIndex < m_pCache->childCount(); ++iControllerIndex)
        {
            /* Get controller cache: */
            const UISettingsCacheMachineStorageController &controllerCache = m_pCache->child(iControllerIndex);

            /* Remove controller marked for 'remove' or 'update' (if it can't be updated): */
            if (controllerCache.wasRemoved() || (controllerCache.wasUpdated() && !isControllerCouldBeUpdated(controllerCache)))
                fSuccess = removeStorageController(controllerCache);
        }

        /* For each controller ('updating' step): */
        // We need to separately update controllers first because
        // attachments should be removed/updated/created same separate way.
        for (int iControllerIndex = 0; fSuccess && iControllerIndex < m_pCache->childCount(); ++iControllerIndex)
        {
            /* Get controller cache: */
            const UISettingsCacheMachineStorageController &controllerCache = m_pCache->child(iControllerIndex);

            /* Update controller marked for 'update' (if it can be updated): */
            if (controllerCache.wasUpdated() && isControllerCouldBeUpdated(controllerCache))
                fSuccess = updateStorageController(controllerCache, true);
        }
        for (int iControllerIndex = 0; fSuccess && iControllerIndex < m_pCache->childCount(); ++iControllerIndex)
        {
            /* Get controller cache: */
            const UISettingsCacheMachineStorageController &controllerCache = m_pCache->child(iControllerIndex);

            /* Update controller marked for 'update' (if it can be updated): */
            if (controllerCache.wasUpdated() && isControllerCouldBeUpdated(controllerCache))
                fSuccess = updateStorageController(controllerCache, false);
        }

        /* For each controller ('creating' step): */
        // Finally we are creating new controllers,
        // with attachments which were released for sure.
        for (int iControllerIndex = 0; fSuccess && iControllerIndex < m_pCache->childCount(); ++iControllerIndex)
        {
            /* Get controller cache: */
            const UISettingsCacheMachineStorageController &controllerCache = m_pCache->child(iControllerIndex);

            /* Create controller marked for 'create' or 'update' (if it can't be updated): */
            if (controllerCache.wasCreated() || (controllerCache.wasUpdated() && !isControllerCouldBeUpdated(controllerCache)))
                fSuccess = createStorageController(controllerCache);
        }
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsStorage::removeStorageController(const UISettingsCacheMachineStorageController &controllerCache)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Remove controller: */
    if (fSuccess && isMachineOffline())
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineStorageController &oldControllerData = controllerCache.base();

        /* Search for a controller with the same name: */
        const CStorageController &comController = m_machine.GetStorageControllerByName(oldControllerData.m_guiValue.m_strName);
        fSuccess = m_machine.isOk() && comController.isNotNull();

        /* Make sure controller really exists: */
        if (fSuccess)
        {
            /* Remove controller with all the attachments at one shot: */
            m_machine.RemoveStorageController(oldControllerData.m_guiValue.m_strName);
            fSuccess = m_machine.isOk();
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsStorage::createStorageController(const UISettingsCacheMachineStorageController &controllerCache)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Create controller: */
    if (fSuccess && isMachineOffline())
    {
        /* Get new data from cache: */
        const UIDataSettingsMachineStorageController &newControllerData = controllerCache.data();

        /* Search for a controller with the same name: */
        const CMachine comMachine(m_machine);
        CStorageController comController = comMachine.GetStorageControllerByName(newControllerData.m_guiValue.m_strName);
        fSuccess = !comMachine.isOk() && comController.isNull();
        AssertReturn(fSuccess, false);

        /* Make sure controller doesn't exist: */
        if (fSuccess)
        {
            /* Create controller: */
            comController = m_machine.AddStorageController(newControllerData.m_guiValue.m_strName,
                                                           newControllerData.m_guiValue.m_enmBus);
            fSuccess = m_machine.isOk() && comController.isNotNull();
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
        else
        {
            /* Save controller type: */
            if (fSuccess)
            {
                comController.SetControllerType(newControllerData.m_guiValue.m_enmType);
                fSuccess = comController.isOk();
            }
            /* Save whether controller uses host IO cache: */
            if (fSuccess)
            {
                comController.SetUseHostIOCache(newControllerData.m_guiValue.m_fUseHostIOCache);
                fSuccess = comController.isOk();
            }
            /* Save controller port number: */
            if (   fSuccess
                && (   newControllerData.m_guiValue.m_enmBus == KStorageBus_SATA
                    || newControllerData.m_guiValue.m_enmBus == KStorageBus_SAS
                    || newControllerData.m_guiValue.m_enmBus == KStorageBus_PCIe
                    || newControllerData.m_guiValue.m_enmBus == KStorageBus_VirtioSCSI))
            {
                ULONG uNewPortCount = newControllerData.m_guiValue.m_uPortCount;
                if (fSuccess)
                {
                    uNewPortCount = qMax(uNewPortCount, comController.GetMinPortCount());
                    fSuccess = comController.isOk();
                }
                if (fSuccess)
                {
                    uNewPortCount = qMin(uNewPortCount, comController.GetMaxPortCount());
                    fSuccess = comController.isOk();
                }
                if (fSuccess)
                {
                    comController.SetPortCount(uNewPortCount);
                    fSuccess = comController.isOk();
                }
            }

            /* Show error message if necessary: */
            if (!fSuccess)
                notifyOperationProgressError(UIErrorString::formatErrorInfo(comController));

            /* For each attachment: */
            for (int iAttachmentIndex = 0; fSuccess && iAttachmentIndex < controllerCache.childCount(); ++iAttachmentIndex)
            {
                /* Get attachment cache: */
                const UISettingsCacheMachineStorageAttachment &attachmentCache = controllerCache.child(iAttachmentIndex);

                /* Create attachment if it was not 'removed': */
                if (!attachmentCache.wasRemoved())
                    fSuccess = createStorageAttachment(controllerCache, attachmentCache);
            }
        }
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsStorage::updateStorageController(const UISettingsCacheMachineStorageController &controllerCache,
                                                       bool fRemovingStep)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Update controller: */
    if (fSuccess)
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineStorageController &oldControllerData = controllerCache.base();
        /* Get new data from cache: */
        const UIDataSettingsMachineStorageController &newControllerData = controllerCache.data();

        /* Search for a controller with the same name: */
        CStorageController comController = m_machine.GetStorageControllerByName(oldControllerData.m_guiValue.m_strName);
        fSuccess = m_machine.isOk() && comController.isNotNull();

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
        else
        {
            /* Save controller type: */
            if (fSuccess && newControllerData.m_guiValue.m_enmType != oldControllerData.m_guiValue.m_enmType)
            {
                comController.SetControllerType(newControllerData.m_guiValue.m_enmType);
                fSuccess = comController.isOk();
            }
            /* Save whether controller uses IO cache: */
            if (fSuccess && newControllerData.m_guiValue.m_fUseHostIOCache != oldControllerData.m_guiValue.m_fUseHostIOCache)
            {
                comController.SetUseHostIOCache(newControllerData.m_guiValue.m_fUseHostIOCache);
                fSuccess = comController.isOk();
            }
            /* Save controller port number: */
            if (   fSuccess
                && newControllerData.m_guiValue.m_uPortCount != oldControllerData.m_guiValue.m_uPortCount
                && (   newControllerData.m_guiValue.m_enmBus == KStorageBus_SATA
                    || newControllerData.m_guiValue.m_enmBus == KStorageBus_SAS
                    || newControllerData.m_guiValue.m_enmBus == KStorageBus_PCIe
                    || newControllerData.m_guiValue.m_enmBus == KStorageBus_VirtioSCSI))
            {
                ULONG uNewPortCount = newControllerData.m_guiValue.m_uPortCount;
                if (fSuccess)
                {
                    uNewPortCount = qMax(uNewPortCount, comController.GetMinPortCount());
                    fSuccess = comController.isOk();
                }
                if (fSuccess)
                {
                    uNewPortCount = qMin(uNewPortCount, comController.GetMaxPortCount());
                    fSuccess = comController.isOk();
                }
                if (fSuccess)
                {
                    comController.SetPortCount(uNewPortCount);
                    fSuccess = comController.isOk();
                }
            }

            /* Show error message if necessary: */
            if (!fSuccess)
                notifyOperationProgressError(UIErrorString::formatErrorInfo(comController));

            // We need to separately remove attachments first because
            // there could be limited amount of attachments or media available.
            if (fRemovingStep)
            {
                /* For each attachment ('removing' step): */
                for (int iAttachmentIndex = 0; fSuccess && iAttachmentIndex < controllerCache.childCount(); ++iAttachmentIndex)
                {
                    /* Get attachment cache: */
                    const UISettingsCacheMachineStorageAttachment &attachmentCache = controllerCache.child(iAttachmentIndex);

                    /* Remove attachment marked for 'remove' or 'update' (if it can't be updated): */
                    if (   attachmentCache.wasRemoved()
                        || (attachmentCache.wasUpdated() && !isAttachmentCouldBeUpdated(attachmentCache)))
                        fSuccess = removeStorageAttachment(controllerCache, attachmentCache);
                }
            }
            else
            {
                /* For each attachment ('creating' step): */
                for (int iAttachmentIndex = 0; fSuccess && iAttachmentIndex < controllerCache.childCount(); ++iAttachmentIndex)
                {
                    /* Get attachment cache: */
                    const UISettingsCacheMachineStorageAttachment &attachmentCache = controllerCache.child(iAttachmentIndex);

                    /* Create attachment marked for 'create' or 'update' (if it can't be updated): */
                    if (   attachmentCache.wasCreated()
                        || (attachmentCache.wasUpdated() && !isAttachmentCouldBeUpdated(attachmentCache)))
                        fSuccess = createStorageAttachment(controllerCache, attachmentCache);

                    else

                    /* Update attachment marked for 'update' (if it can be updated): */
                    if (attachmentCache.wasUpdated() && isAttachmentCouldBeUpdated(attachmentCache))
                        fSuccess = updateStorageAttachment(controllerCache, attachmentCache);
                }
            }
        }
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsStorage::removeStorageAttachment(const UISettingsCacheMachineStorageController &controllerCache,
                                                       const UISettingsCacheMachineStorageAttachment &attachmentCache)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Remove attachment: */
    if (fSuccess)
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineStorageController &oldControllerData = controllerCache.base();
        /* Get old data from cache: */
        const UIDataSettingsMachineStorageAttachment &oldAttachmentData = attachmentCache.base();

        /* Search for an attachment with the same parameters: */
        const CMediumAttachment &comAttachment = m_machine.GetMediumAttachment(oldControllerData.m_guiValue.m_strName,
                                                                               oldAttachmentData.m_guiValue.m_iPort,
                                                                               oldAttachmentData.m_guiValue.m_iDevice);
        fSuccess = m_machine.isOk() && comAttachment.isNotNull();

        /* Make sure attachment really exists: */
        if (fSuccess)
        {
            /* Remove attachment: */
            m_machine.DetachDevice(oldControllerData.m_guiValue.m_strName,
                                   oldAttachmentData.m_guiValue.m_iPort,
                                   oldAttachmentData.m_guiValue.m_iDevice);
            fSuccess = m_machine.isOk();
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsStorage::createStorageAttachment(const UISettingsCacheMachineStorageController &controllerCache,
                                                       const UISettingsCacheMachineStorageAttachment &attachmentCache)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Create attachment: */
    if (fSuccess)
    {
        /* Get new data from cache: */
        const UIDataSettingsMachineStorageController &newControllerData = controllerCache.data();
        /* Get new data from cache: */
        const UIDataSettingsMachineStorageAttachment &newAttachmentData = attachmentCache.data();

        /* Search for an attachment with the same parameters: */
        const CMachine comMachine(m_machine);
        const CMediumAttachment &comAttachment = comMachine.GetMediumAttachment(newControllerData.m_guiValue.m_strName,
                                                                                newAttachmentData.m_guiValue.m_iPort,
                                                                                newAttachmentData.m_guiValue.m_iDevice);
        fSuccess = !comMachine.isOk() && comAttachment.isNull();
        AssertReturn(fSuccess, false);

        /* Make sure attachment doesn't exist: */
        if (fSuccess)
        {
            /* Create attachment: */
            const UIMedium vboxMedium = gpMediumEnumerator->medium(newAttachmentData.m_guiValue.m_uMediumId);
            const CMedium comMedium = vboxMedium.medium();
            m_machine.AttachDevice(newControllerData.m_guiValue.m_strName,
                                   newAttachmentData.m_guiValue.m_iPort,
                                   newAttachmentData.m_guiValue.m_iDevice,
                                   newAttachmentData.m_guiValue.m_enmDeviceType,
                                   comMedium);
            fSuccess = m_machine.isOk();
        }

        if (newAttachmentData.m_guiValue.m_enmDeviceType == KDeviceType_DVD)
        {
            /* Save whether this is a passthrough device: */
            if (fSuccess && isMachineOffline())
            {
                m_machine.PassthroughDevice(newControllerData.m_guiValue.m_strName,
                                            newAttachmentData.m_guiValue.m_iPort,
                                            newAttachmentData.m_guiValue.m_iDevice,
                                            newAttachmentData.m_guiValue.m_fPassthrough);
                fSuccess = m_machine.isOk();
            }
            /* Save whether this is a live cd device: */
            if (fSuccess)
            {
                m_machine.TemporaryEjectDevice(newControllerData.m_guiValue.m_strName,
                                               newAttachmentData.m_guiValue.m_iPort,
                                               newAttachmentData.m_guiValue.m_iDevice,
                                               newAttachmentData.m_guiValue.m_fTempEject);
                fSuccess = m_machine.isOk();
            }
        }
        else if (newAttachmentData.m_guiValue.m_enmDeviceType == KDeviceType_HardDisk)
        {
            /* Save whether this is a ssd device: */
            if (fSuccess && isMachineOffline())
            {
                m_machine.NonRotationalDevice(newControllerData.m_guiValue.m_strName,
                                              newAttachmentData.m_guiValue.m_iPort,
                                              newAttachmentData.m_guiValue.m_iDevice,
                                              newAttachmentData.m_guiValue.m_fNonRotational);
                fSuccess = m_machine.isOk();
            }
        }

        if (newControllerData.m_guiValue.m_enmBus == KStorageBus_SATA)
        {
            /* Save whether this device is hot-pluggable: */
            if (fSuccess && isMachineOffline())
            {
                m_machine.SetHotPluggableForDevice(newControllerData.m_guiValue.m_strName,
                                                   newAttachmentData.m_guiValue.m_iPort,
                                                   newAttachmentData.m_guiValue.m_iDevice,
                                                   newAttachmentData.m_guiValue.m_fHotPluggable);
                fSuccess = m_machine.isOk();
            }
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsStorage::updateStorageAttachment(const UISettingsCacheMachineStorageController &controllerCache,
                                                       const UISettingsCacheMachineStorageAttachment &attachmentCache)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Update attachment: */
    if (fSuccess)
    {
        /* Get new data from cache: */
        const UIDataSettingsMachineStorageController &newControllerData = controllerCache.data();
        /* Get new data from cache: */
        const UIDataSettingsMachineStorageAttachment &newAttachmentData = attachmentCache.data();

        /* Search for an attachment with the same parameters: */
        const CMediumAttachment &comAttachment = m_machine.GetMediumAttachment(newControllerData.m_guiValue.m_strName,
                                                                               newAttachmentData.m_guiValue.m_iPort,
                                                                               newAttachmentData.m_guiValue.m_iDevice);
        fSuccess = m_machine.isOk() && comAttachment.isNotNull();

        /* Make sure attachment doesn't exist: */
        if (fSuccess)
        {
            /* Remount attachment: */
            const UIMedium vboxMedium = gpMediumEnumerator->medium(newAttachmentData.m_guiValue.m_uMediumId);
            const CMedium comMedium = vboxMedium.medium();
            m_machine.MountMedium(newControllerData.m_guiValue.m_strName,
                                  newAttachmentData.m_guiValue.m_iPort,
                                  newAttachmentData.m_guiValue.m_iDevice,
                                  comMedium,
                                  true /* force? */);
            fSuccess = m_machine.isOk();
        }

        if (newAttachmentData.m_guiValue.m_enmDeviceType == KDeviceType_DVD)
        {
            /* Save whether this is a passthrough device: */
            if (fSuccess && isMachineOffline())
            {
                m_machine.PassthroughDevice(newControllerData.m_guiValue.m_strName,
                                            newAttachmentData.m_guiValue.m_iPort,
                                            newAttachmentData.m_guiValue.m_iDevice,
                                            newAttachmentData.m_guiValue.m_fPassthrough);
                fSuccess = m_machine.isOk();
            }
            /* Save whether this is a live cd device: */
            if (fSuccess)
            {
                m_machine.TemporaryEjectDevice(newControllerData.m_guiValue.m_strName,
                                               newAttachmentData.m_guiValue.m_iPort,
                                               newAttachmentData.m_guiValue.m_iDevice,
                                               newAttachmentData.m_guiValue.m_fTempEject);
                fSuccess = m_machine.isOk();
            }
        }
        else if (newAttachmentData.m_guiValue.m_enmDeviceType == KDeviceType_HardDisk)
        {
            /* Save whether this is a ssd device: */
            if (fSuccess && isMachineOffline())
            {
                m_machine.NonRotationalDevice(newControllerData.m_guiValue.m_strName,
                                              newAttachmentData.m_guiValue.m_iPort,
                                              newAttachmentData.m_guiValue.m_iDevice,
                                              newAttachmentData.m_guiValue.m_fNonRotational);
                fSuccess = m_machine.isOk();
            }
        }

        if (newControllerData.m_guiValue.m_enmBus == KStorageBus_SATA)
        {
            /* Save whether this device is hot-pluggable: */
            if (fSuccess && isMachineOffline())
            {
                m_machine.SetHotPluggableForDevice(newControllerData.m_guiValue.m_strName,
                                                   newAttachmentData.m_guiValue.m_iPort,
                                                   newAttachmentData.m_guiValue.m_iDevice,
                                                   newAttachmentData.m_guiValue.m_fHotPluggable);
                fSuccess = m_machine.isOk();
            }
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsStorage::isControllerCouldBeUpdated(const UISettingsCacheMachineStorageController &controllerCache) const
{
    /* IController interface doesn't allow to change 'bus' attribute but allows
     * to change 'name' attribute which can conflict with another one controller.
     * Both those attributes could be changed in GUI directly or indirectly.
     * For such cases we have to recreate IController instance,
     * for other cases we will update controller attributes only. */
    const UIDataSettingsMachineStorageController &oldControllerData = controllerCache.base();
    const UIDataSettingsMachineStorageController &newControllerData = controllerCache.data();
    return true
           && (newControllerData.m_guiValue.m_strName == oldControllerData.m_guiValue.m_strName)
           && (newControllerData.m_guiValue.m_enmBus == oldControllerData.m_guiValue.m_enmBus)
           ;
}

bool UIMachineSettingsStorage::isAttachmentCouldBeUpdated(const UISettingsCacheMachineStorageAttachment &attachmentCache) const
{
    /* IMediumAttachment could be indirectly updated through IMachine
     * only if attachment type, device and port were NOT changed and is one of the next types:
     * KDeviceType_Floppy or KDeviceType_DVD.
     * For other cases we will recreate attachment fully: */
    const UIDataSettingsMachineStorageAttachment &oldAttachmentData = attachmentCache.base();
    const UIDataSettingsMachineStorageAttachment &newAttachmentData = attachmentCache.data();
    return true
           && (newAttachmentData.m_guiValue.m_enmDeviceType == oldAttachmentData.m_guiValue.m_enmDeviceType)
           && (newAttachmentData.m_guiValue.m_iPort == oldAttachmentData.m_guiValue.m_iPort)
           && (newAttachmentData.m_guiValue.m_iDevice == oldAttachmentData.m_guiValue.m_iDevice)
           && (   newAttachmentData.m_guiValue.m_enmDeviceType == KDeviceType_Floppy
               || newAttachmentData.m_guiValue.m_enmDeviceType == KDeviceType_DVD)
           ;
}
