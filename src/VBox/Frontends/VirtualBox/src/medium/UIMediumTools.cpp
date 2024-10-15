/* $Id: UIMediumTools.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumTools class implementation.
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
#include <QApplication>
#include <QDir>
#include <QMenu>

/* GUI includes: */
#include "QIFileDialog.h"
#include "UIExtraDataManager.h"
#include "UIFDCreationDialog.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UILocalMachineStuff.h"
#include "UIMediumEnumerator.h"
#include "UIMediumSelector.h"
#include "UIMediumTools.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UINotificationCenter.h"
#include "UIVisoCreator.h"
#include "UIWizardNewVD.h"

/* COM includes: */
#include "CMachine.h"
#include "CMedium.h"
#include "CSession.h"
#include "CStorageController.h"
#include "CSystemProperties.h"


QString UIMediumTools::storageDetails(const CMedium &comMedium, bool fPredictDiff, bool fUseHtml /* = true */)
{
    /* Search for corresponding UI medium: */
    const QUuid uMediumID = comMedium.isNull() ? UIMedium::nullID() : comMedium.GetId();
    UIMedium guiMedium = gpMediumEnumerator->medium(uMediumID);
    if (!comMedium.isNull() && guiMedium.isNull())
    {
        /* UI medium may be new and not among cached media, request enumeration: */
        gpMediumEnumerator->enumerateMedia(CMediumVector() << comMedium);

        /* Search for corresponding UI medium again: */
        guiMedium = gpMediumEnumerator->medium(uMediumID);
        if (guiMedium.isNull())
        {
            /* Medium might be deleted already, return null string: */
            return QString();
        }
    }

    /* For differencing hard-disk we have to request
     * enumeration of whole tree based in it's root item: */
    if (   comMedium.isNotNull()
        && comMedium.GetDeviceType() == KDeviceType_HardDisk)
    {
        /* Traverse through parents to root to catch it: */
        CMedium comRootMedium;
        CMedium comParentMedium = comMedium.GetParent();
        while (comParentMedium.isNotNull())
        {
            comRootMedium = comParentMedium;
            comParentMedium = comParentMedium.GetParent();
        }
        /* Enumerate root if it's found and wasn't cached: */
        if (comRootMedium.isNotNull())
        {
            const QUuid uRootId = comRootMedium.GetId();
            if (gpMediumEnumerator->medium(uRootId).isNull())
                gpMediumEnumerator->enumerateMedia(CMediumVector() << comRootMedium);
        }
    }

    /* Return UI medium details: */
    return fUseHtml ? guiMedium.detailsHTML(true /* no diffs? */, fPredictDiff) :
                      guiMedium.details(true /* no diffs? */, fPredictDiff);
}

bool UIMediumTools::acquireAmountOfImmutableImages(const CMachine &comMachine, ulong &cAmount)
{
    /* Acquire state: */
    ulong cAmountOfImmutableImages = 0;
    const KMachineState enmState = comMachine.GetState();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        /// @todo Who knows why 13 years ago this condition was added ..
        if (enmState == KMachineState_Paused)
        {
            const CMediumAttachmentVector comAttachments = comMachine.GetMediumAttachments();
            fSuccess = comMachine.isOk();
            if (!fSuccess)
                UINotificationMessage::cannotAcquireMachineParameter(comMachine);
            else
            {
                /* Calculate the amount of immutable attachments: */
                foreach (const CMediumAttachment &comAttachment, comAttachments)
                {
                    /* Get the medium: */
                    const CMedium comMedium = comAttachment.GetMedium();
                    if (   comMedium.isNull() /* Null medium is valid case as well */
                        || comMedium.GetParent().isNull() /* Null parent is valid case as well */)
                        continue;
                    /* Get the base medium: */
                    const CMedium comBaseMedium = comMedium.GetBase();
                    fSuccess = comMedium.isOk();
                    if (!fSuccess)
                        UINotificationMessage::cannotAcquireMediumParameter(comMedium);
                    else
                    {
                        const KMediumType enmType = comBaseMedium.GetType();
                        fSuccess = comBaseMedium.isOk();
                        if (!fSuccess)
                            UINotificationMessage::cannotAcquireMediumParameter(comBaseMedium);
                        else if (enmType == KMediumType_Immutable)
                            ++cAmountOfImmutableImages;
                    }
                    if (!fSuccess)
                        break;
                }
            }
        }
    }
    if (fSuccess)
        cAmount = cAmountOfImmutableImages;
    return fSuccess;
}

QString UIMediumTools::defaultFolderPathForType(UIMediumDeviceType enmMediumType)
{
    QString strLastFolder;
    switch (enmMediumType)
    {
        case UIMediumDeviceType_HardDisk:
            strLastFolder = gEDataManager->recentFolderForHardDrives();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForOpticalDisks();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForFloppyDisks();
            break;
        case UIMediumDeviceType_DVD:
            strLastFolder = gEDataManager->recentFolderForOpticalDisks();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForFloppyDisks();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForHardDrives();
            break;
        case UIMediumDeviceType_Floppy:
            strLastFolder = gEDataManager->recentFolderForFloppyDisks();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForOpticalDisks();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForHardDrives();
            break;
        default:
            break;
    }

    if (strLastFolder.isEmpty())
        return gpGlobalSession->virtualBox().GetSystemProperties().GetDefaultMachineFolder();

    return strLastFolder;
}

QUuid UIMediumTools::openMedium(UIMediumDeviceType enmMediumType,
                                const QString &strMediumLocation,
                                QWidget *pParent /* = 0 */)
{
    /* Convert to native separators: */
    const QString strLocation = QDir::toNativeSeparators(strMediumLocation);

    /* Initialize variables: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Open corresponding medium: */
    CMedium comMedium = comVBox.OpenMedium(strLocation, mediumTypeToGlobal(enmMediumType), KAccessMode_ReadWrite, false);

    if (comVBox.isOk())
    {
        /* Prepare vbox medium wrapper: */
        UIMedium guiMedium = gpMediumEnumerator->medium(comMedium.GetId());

        /* First of all we should test if that medium already opened: */
        if (guiMedium.isNull())
        {
            /* And create new otherwise: */
            guiMedium = UIMedium(comMedium, enmMediumType, KMediumState_Created);
            gpMediumEnumerator->createMedium(guiMedium);
        }

        /* Return guiMedium id: */
        return guiMedium.id();
    }
    else
        msgCenter().cannotOpenMedium(comVBox, strLocation, pParent);

    return QUuid();
}

QUuid UIMediumTools::openMediumWithFileOpenDialog(UIMediumDeviceType enmMediumType,
                                                  QWidget *pParent,
                                                  const QString &strDefaultFolder /* = QString() */,
                                                  bool fUseLastFolder /* = false */)
{
    /* Initialize variables: */
    QList<QPair <QString, QString> > filters;
    QStringList backends;
    QStringList prefixes;
    QString strFilter;
    QString strTitle;
    QString allType;
    QString strLastFolder = defaultFolderPathForType(enmMediumType);

    /* For DVDs and Floppies always check first the last recently used medium folder.
     * For hard disk use the caller's setting: */
    fUseLastFolder = (enmMediumType == UIMediumDeviceType_DVD) || (enmMediumType == UIMediumDeviceType_Floppy);

    switch (enmMediumType)
    {
        case UIMediumDeviceType_HardDisk:
        {
            filters = HDDBackends(gpGlobalSession->virtualBox());
            strTitle = QApplication::translate("UIMediumTools", "Please choose a virtual hard disk file");
            allType = QApplication::translate("UIMediumTools", "All virtual hard disk files (%1)");
            break;
        }
        case UIMediumDeviceType_DVD:
        {
            filters = DVDBackends(gpGlobalSession->virtualBox());
            strTitle = QApplication::translate("UIMediumTools", "Please choose a virtual optical disk file");
            allType = QApplication::translate("UIMediumTools", "All virtual optical disk files (%1)");
            break;
        }
        case UIMediumDeviceType_Floppy:
        {
            filters = FloppyBackends(gpGlobalSession->virtualBox());
            strTitle = QApplication::translate("UIMediumTools", "Please choose a virtual floppy disk file");
            allType = QApplication::translate("UIMediumTools", "All virtual floppy disk files (%1)");
            break;
        }
        default:
            break;
    }
    QString strHomeFolder = fUseLastFolder && !strLastFolder.isEmpty() ? strLastFolder :
                            strDefaultFolder.isEmpty() ? gpGlobalSession->homeFolder() : strDefaultFolder;

    /* Prepare filters and backends: */
    for (int i = 0; i < filters.count(); ++i)
    {
        /* Get iterated filter: */
        QPair<QString, QString> item = filters.at(i);
        /* Create one backend filter string: */
        backends << QString("%1 (%2)").arg(item.first).arg(item.second);
        /* Save the suffix's for the "All" entry: */
        prefixes << item.second;
    }
    if (!prefixes.isEmpty())
        backends.insert(0, allType.arg(prefixes.join(" ").trimmed()));
    backends << QApplication::translate("UIMediumTools", "All files (*)");
    strFilter = backends.join(";;").trimmed();

    /* Create open file dialog: */
    QStringList files = QIFileDialog::getOpenFileNames(strHomeFolder, strFilter, pParent, strTitle, 0, true, true);

    /* If dialog has some result: */
    if (!files.empty() && !files[0].isEmpty())
    {
        QUuid uMediumId = openMedium(enmMediumType, files[0], pParent);
        if (enmMediumType == UIMediumDeviceType_DVD || enmMediumType == UIMediumDeviceType_Floppy ||
            (enmMediumType == UIMediumDeviceType_HardDisk && fUseLastFolder))
            gpMediumEnumerator->updateRecentlyUsedMediumListAndFolder(enmMediumType, gpMediumEnumerator->medium(uMediumId).location());
        return uMediumId;
    }
    return QUuid();
}

QUuid UIMediumTools::openMediumCreatorDialog(UIActionPool *pActionPool,
                                             QWidget *pParent,
                                             UIMediumDeviceType enmMediumType,
                                             const QString &strDefaultFolder /* = QString() */,
                                             const QString &strMachineName /* = QString() */,
                                             const QString &strMachineGuestOSTypeId /*= QString() */)
{
    /* Depending on medium-type: */
    QUuid uMediumId;
    switch (enmMediumType)
    {
        case UIMediumDeviceType_HardDisk:
            uMediumId = UIWizardNewVD::createVDWithWizard(pParent, strDefaultFolder, strMachineName, strMachineGuestOSTypeId);
            break;
        case UIMediumDeviceType_DVD:
            uMediumId = UIVisoCreatorDialog::createViso(pActionPool, pParent, strDefaultFolder, strMachineName);
            break;
        case UIMediumDeviceType_Floppy:
            uMediumId = UIFDCreationDialog::createFloppyDisk(pParent, strDefaultFolder, strMachineName);
            break;
        default:
            break;
    }
    if (uMediumId.isNull())
        return QUuid();

    /* Update the recent medium list only if the medium type is floppy since updating when a VISO is created is not optimal: */
    if (enmMediumType == UIMediumDeviceType_Floppy)
        gpMediumEnumerator->updateRecentlyUsedMediumListAndFolder(enmMediumType, gpMediumEnumerator->medium(uMediumId).location());
    return uMediumId;
}

void UIMediumTools::prepareStorageMenu(QMenu *pMenu,
                                       QObject *pListener,
                                       const char *pszSlotName,
                                       const CMachine &comMachine,
                                       const QString &strControllerName,
                                       const StorageSlot &storageSlot)
{
    /* Current attachment attributes: */
    const CMediumAttachment comCurrentAttachment = comMachine.GetMediumAttachment(strControllerName,
                                                                                  storageSlot.port,
                                                                                  storageSlot.device);
    const CMedium comCurrentMedium = comCurrentAttachment.GetMedium();
    const QUuid uCurrentID = comCurrentMedium.isNull() ? QUuid() : comCurrentMedium.GetId();
    const QString strCurrentLocation = comCurrentMedium.isNull() ? QString() : comCurrentMedium.GetLocation();

    /* Other medium-attachments of same machine: */
    const CMediumAttachmentVector comAttachments = comMachine.GetMediumAttachments();

    /* Determine device & medium types: */
    const UIMediumDeviceType enmMediumType = mediumTypeToLocal(comCurrentAttachment.GetType());
    AssertMsgReturnVoid(enmMediumType != UIMediumDeviceType_Invalid, ("Incorrect storage medium type!\n"));

    /* Prepare open-existing-medium action: */
    QAction *pActionOpenExistingMedium = pMenu->addAction(UIIconPool::iconSet(":/select_file_16px.png"),
                                                          QString(), pListener, pszSlotName);
    pActionOpenExistingMedium->setData(QVariant::fromValue(UIMediumTarget(strControllerName, comCurrentAttachment.GetPort(),
                                                                          comCurrentAttachment.GetDevice(), enmMediumType)));
    pActionOpenExistingMedium->setText(QApplication::translate("UIMediumTools", "Choose/Create a Disk Image..."));

    /* Prepare open medium file action: */
    QAction *pActionFileSelector = pMenu->addAction(UIIconPool::iconSet(":/select_file_16px.png"),
                                                    QString(), pListener, pszSlotName);
    pActionFileSelector->setData(QVariant::fromValue(UIMediumTarget(strControllerName, comCurrentAttachment.GetPort(),
                                                                    comCurrentAttachment.GetDevice(), enmMediumType,
                                                                    UIMediumTarget::UIMediumTargetType_WithFileDialog)));
    pActionFileSelector->setText(QApplication::translate("UIMediumTools", "Choose a Disk File..."));

    /* Insert separator: */
    pMenu->addSeparator();

    /* Get existing-host-drive vector: */
    CMediumVector comMedia;
    switch (enmMediumType)
    {
        case UIMediumDeviceType_DVD:    comMedia = gpGlobalSession->host().GetDVDDrives(); break;
        case UIMediumDeviceType_Floppy: comMedia = gpGlobalSession->host().GetFloppyDrives(); break;
        default: break;
    }
    /* Prepare choose-existing-host-drive actions: */
    foreach (const CMedium &comMedium, comMedia)
    {
        /* Make sure host-drive usage is unique: */
        bool fIsHostDriveUsed = false;
        foreach (const CMediumAttachment &comOtherAttachment, comAttachments)
        {
            if (comOtherAttachment != comCurrentAttachment)
            {
                const CMedium &comOtherMedium = comOtherAttachment.GetMedium();
                if (!comOtherMedium.isNull() && comOtherMedium.GetId() == comMedium.GetId())
                {
                    fIsHostDriveUsed = true;
                    break;
                }
            }
        }
        /* If host-drives usage is unique: */
        if (!fIsHostDriveUsed)
        {
            QAction *pActionChooseHostDrive = pMenu->addAction(UIMedium(comMedium, enmMediumType).name(), pListener, pszSlotName);
            pActionChooseHostDrive->setCheckable(true);
            pActionChooseHostDrive->setChecked(!comCurrentMedium.isNull() && comMedium.GetId() == uCurrentID);
            pActionChooseHostDrive->setData(QVariant::fromValue(UIMediumTarget(strControllerName,
                                                                               comCurrentAttachment.GetPort(),
                                                                               comCurrentAttachment.GetDevice(),
                                                                               enmMediumType,
                                                                               UIMediumTarget::UIMediumTargetType_WithID,
                                                                               comMedium.GetId().toString())));
        }
    }

    /* Get recent-medium list: */
    QStringList recentMediumList;
    QStringList recentMediumListUsed;
    switch (enmMediumType)
    {
        case UIMediumDeviceType_HardDisk: recentMediumList = gEDataManager->recentListOfHardDrives(); break;
        case UIMediumDeviceType_DVD:      recentMediumList = gEDataManager->recentListOfOpticalDisks(); break;
        case UIMediumDeviceType_Floppy:   recentMediumList = gEDataManager->recentListOfFloppyDisks(); break;
        default: break;
    }
    /* Prepare choose-recent-medium actions: */
    foreach (const QString &strRecentMediumLocationBase, recentMediumList)
    {
        /* Confirm medium uniqueness: */
        if (recentMediumListUsed.contains(strRecentMediumLocationBase))
            continue;
        /* Mark medium as known: */
        recentMediumListUsed << strRecentMediumLocationBase;
        /* Convert separators to native: */
        const QString strRecentMediumLocation = QDir::toNativeSeparators(strRecentMediumLocationBase);
        /* Confirm medium presence: */
        if (!QFile::exists(strRecentMediumLocation))
            continue;
        /* Make sure recent-medium usage is unique: */
        bool fIsRecentMediumUsed = false;
        if (enmMediumType != UIMediumDeviceType_DVD)
        {
            foreach (const CMediumAttachment &otherAttachment, comAttachments)
            {
                if (otherAttachment != comCurrentAttachment)
                {
                    const CMedium &comOtherMedium = otherAttachment.GetMedium();
                    if (!comOtherMedium.isNull() && comOtherMedium.GetLocation() == strRecentMediumLocation)
                    {
                        fIsRecentMediumUsed = true;
                        break;
                    }
                }
            }
        }
        /* If recent-medium usage is unique: */
        if (!fIsRecentMediumUsed)
        {
            QAction *pActionChooseRecentMedium = pMenu->addAction(QFileInfo(strRecentMediumLocation).fileName(),
                                                                  pListener, pszSlotName);
            pActionChooseRecentMedium->setCheckable(true);
            pActionChooseRecentMedium->setChecked(!comCurrentMedium.isNull() && strRecentMediumLocation == strCurrentLocation);
            pActionChooseRecentMedium->setData(QVariant::fromValue(UIMediumTarget(strControllerName,
                                                                                  comCurrentAttachment.GetPort(),
                                                                                  comCurrentAttachment.GetDevice(),
                                                                                  enmMediumType,
                                                                                  UIMediumTarget::UIMediumTargetType_WithLocation,
                                                                                  strRecentMediumLocation)));
            pActionChooseRecentMedium->setToolTip(strRecentMediumLocation);
        }
    }

    /* Last action for optical/floppy attachments only: */
    if (enmMediumType == UIMediumDeviceType_DVD || enmMediumType == UIMediumDeviceType_Floppy)
    {
        /* Insert separator: */
        pMenu->addSeparator();

        /* Prepare unmount-current-medium action: */
        QAction *pActionUnmountMedium = pMenu->addAction(QString(), pListener, pszSlotName);
        pActionUnmountMedium->setEnabled(!comCurrentMedium.isNull());
        pActionUnmountMedium->setData(QVariant::fromValue(UIMediumTarget(strControllerName, comCurrentAttachment.GetPort(),
                                                                         comCurrentAttachment.GetDevice())));
        pActionUnmountMedium->setText(QApplication::translate("UIMediumTools", "Remove Disk From Virtual Drive"));
        if (enmMediumType == UIMediumDeviceType_DVD)
            pActionUnmountMedium->setIcon(UIIconPool::iconSet(":/cd_unmount_16px.png", ":/cd_unmount_disabled_16px.png"));
        else if (enmMediumType == UIMediumDeviceType_Floppy)
            pActionUnmountMedium->setIcon(UIIconPool::iconSet(":/fd_unmount_16px.png", ":/fd_unmount_disabled_16px.png"));
    }
}

void UIMediumTools::updateMachineStorage(const CMachine &comConstMachine,
                                         const UIMediumTarget &target,
                                         UIActionPool *pActionPool)
{
    /* Mount (by default): */
    bool fMount = true;
    /* Null medium (by default): */
    CMedium comMedium;
    /* With null ID (by default): */
    QUuid uActualID;

    /* Current mount-target attributes: */
    const CStorageController comCurrentController = comConstMachine.GetStorageControllerByName(target.name);
    const KStorageBus enmCurrentStorageBus = comCurrentController.GetBus();
    const CMediumAttachment comCurrentAttachment = comConstMachine.GetMediumAttachment(target.name, target.port, target.device);
    const CMedium comCurrentMedium = comCurrentAttachment.GetMedium();
    const QUuid uCurrentID = comCurrentMedium.isNull() ? QUuid() : comCurrentMedium.GetId();
    const QString strCurrentLocation = comCurrentMedium.isNull() ? QString() : comCurrentMedium.GetLocation();

    /* Which additional info do we have? */
    switch (target.type)
    {
        /* Do we have an exact ID or do we let the user open a medium? */
        case UIMediumTarget::UIMediumTargetType_WithID:
        case UIMediumTarget::UIMediumTargetType_WithFileDialog:
        case UIMediumTarget::UIMediumTargetType_CreateAdHocVISO:
        case UIMediumTarget::UIMediumTargetType_CreateFloppyDisk:
        {
            /* New mount-target attributes: */
            QUuid uNewID;

            /* Invoke file-open dialog to choose medium ID: */
            if (target.mediumType != UIMediumDeviceType_Invalid && target.data.isNull())
            {
                /* Keyboard can be captured by machine-view.
                 * So we should clear machine-view focus to let file-open dialog get it.
                 * That way the keyboard will be released too.. */
                QWidget *pLastFocusedWidget = 0;
                if (QApplication::focusWidget())
                {
                    pLastFocusedWidget = QApplication::focusWidget();
                    pLastFocusedWidget->clearFocus();
                }
                /* Call for file-open dialog: */
                const QString strMachineFolder(QFileInfo(comConstMachine.GetSettingsFilePath()).absolutePath());
                QUuid uMediumID;
                if (target.type == UIMediumTarget::UIMediumTargetType_WithID)
                {
                    int iDialogReturn = UIMediumSelector::openMediumSelectorDialog(windowManager().mainWindowShown(), target.mediumType,
                                                                                   uCurrentID, uMediumID,
                                                                                   strMachineFolder, comConstMachine.GetName(),
                                                                                   comConstMachine.GetOSTypeId(), true /*fEnableCreate */,
                                                                                   comConstMachine.GetId(), pActionPool);
                    if (iDialogReturn == UIMediumSelector::ReturnCode_LeftEmpty &&
                        (target.mediumType == UIMediumDeviceType_DVD || target.mediumType == UIMediumDeviceType_Floppy))
                        fMount = false;
                }
                else if (target.type == UIMediumTarget::UIMediumTargetType_WithFileDialog)
                {
                    uMediumID = openMediumWithFileOpenDialog(target.mediumType, windowManager().mainWindowShown(),
                                                             strMachineFolder, false /* fUseLastFolder */);
                }
                else if(target.type == UIMediumTarget::UIMediumTargetType_CreateAdHocVISO)
                    UIVisoCreatorDialog::createViso(pActionPool, windowManager().mainWindowShown(),
                                                    strMachineFolder, comConstMachine.GetName());

                else if(target.type == UIMediumTarget::UIMediumTargetType_CreateFloppyDisk)
                    uMediumID = UIFDCreationDialog::createFloppyDisk(windowManager().mainWindowShown(), strMachineFolder, comConstMachine.GetName());

                /* Return focus back: */
                if (pLastFocusedWidget)
                    pLastFocusedWidget->setFocus();
                /* Accept new medium ID: */
                if (!uMediumID.isNull())
                    uNewID = uMediumID;
                else
                    /* Else just exit in case left empty is not chosen in medium selector dialog: */
                    if (fMount)
                        return;
            }
            /* Use medium ID which was passed: */
            else if (!target.data.isNull() && target.data != uCurrentID.toString())
                uNewID = QUuid(target.data);

            /* Should we mount or unmount? */
            fMount = !uNewID.isNull();

            /* Prepare target medium: */
            const UIMedium guiMedium = gpMediumEnumerator->medium(uNewID);
            comMedium = guiMedium.medium();
            uActualID = fMount ? uNewID : uCurrentID;
            break;
        }
        /* Do we have a recent location? */
        case UIMediumTarget::UIMediumTargetType_WithLocation:
        {
            /* Open medium by location and get new medium ID if any: */
            const QUuid uNewID = openMedium(target.mediumType, target.data);
            /* Else just exit: */
            if (uNewID.isNull())
                return;

            /* Should we mount or unmount? */
            fMount = uNewID != uCurrentID;

            /* Prepare target medium: */
            const UIMedium guiMedium = fMount ? gpMediumEnumerator->medium(uNewID) : UIMedium();
            comMedium = fMount ? guiMedium.medium() : CMedium();
            uActualID = fMount ? uNewID : uCurrentID;
            break;
        }
    }

    /* Do not unmount hard-drives: */
    if (target.mediumType == UIMediumDeviceType_HardDisk && !fMount)
        return;

    /* Get editable machine & session: */
    CMachine comMachine = comConstMachine;
    CSession comSession = tryToOpenSessionFor(comMachine);

    /* Remount medium to the predefined port/device: */
    bool fWasMounted = false;
    /* Hard drive case: */
    if (target.mediumType == UIMediumDeviceType_HardDisk)
    {
        /* Detaching: */
        comMachine.DetachDevice(target.name, target.port, target.device);
        fWasMounted = comMachine.isOk();
        if (!fWasMounted)
            msgCenter().cannotDetachDevice(comMachine, UIMediumDeviceType_HardDisk, strCurrentLocation,
                                           StorageSlot(enmCurrentStorageBus, target.port, target.device));
        else
        {
            /* Attaching: */
            comMachine.AttachDevice(target.name, target.port, target.device, KDeviceType_HardDisk, comMedium);
            fWasMounted = comMachine.isOk();
            if (!fWasMounted)
                msgCenter().cannotAttachDevice(comMachine, UIMediumDeviceType_HardDisk, strCurrentLocation,
                                               StorageSlot(enmCurrentStorageBus, target.port, target.device));
        }
    }
    /* Optical/floppy drive case: */
    else
    {
        /* Remounting: */
        comMachine.MountMedium(target.name, target.port, target.device, comMedium, false /* force? */);
        fWasMounted = comMachine.isOk();
        if (!fWasMounted)
        {
            /* Ask for force remounting: */
            if (msgCenter().cannotRemountMedium(comMachine, gpMediumEnumerator->medium(uActualID),
                                                fMount, true /* retry? */))
            {
                /* Force remounting: */
                comMachine.MountMedium(target.name, target.port, target.device, comMedium, true /* force? */);
                fWasMounted = comMachine.isOk();
                if (!fWasMounted)
                    msgCenter().cannotRemountMedium(comMachine, gpMediumEnumerator->medium(uActualID),
                                                    fMount, false /* retry? */);
            }
        }
    }

    /* Save settings: */
    if (fWasMounted)
    {
        comMachine.SaveSettings();
        if (!comMachine.isOk())
            msgCenter().cannotSaveMachineSettings(comMachine, windowManager().mainWindowShown());
    }

    /* Close session to editable comMachine if necessary: */
    if (!comSession.isNull())
        comSession.UnlockMachine();
}
