/* $Id: UICloudNetworkingStuff.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudNetworkingStuff namespace implementation.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include "UICloudNetworkingStuff.h"
#include "UIErrorString.h"
#include "UIGlobalSession.h"
#include "UIMessageCenter.h"

/* COM includes: */
#include "CAppliance.h"
#include "CForm.h"
#include "CProgress.h"
#include "CStringArray.h"
#include "CVirtualBox.h"
#include "CVirtualBoxErrorInfo.h"
#include "CVirtualSystemDescription.h"


CCloudProviderManager UICloudNetworkingStuff::cloudProviderManager(UINotificationCenter *pParent /* = 0 */)
{
    /* Acquire VBox: */
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();
    if (comVBox.isNotNull())
    {
        /* Acquire cloud provider manager: */
        CCloudProviderManager comProviderManager = comVBox.GetCloudProviderManager();
        if (!comVBox.isOk())
            UINotificationMessage::cannotAcquireVirtualBoxParameter(comVBox, pParent);
        else
            return comProviderManager;
    }
    /* Null by default: */
    return CCloudProviderManager();
}

CCloudProviderManager UICloudNetworkingStuff::cloudProviderManager(QString &strErrorMessage)
{
    /* Acquire VBox: */
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();
    if (comVBox.isNotNull())
    {
        /* Acquire cloud provider manager: */
        CCloudProviderManager comProviderManager = comVBox.GetCloudProviderManager();
        if (!comVBox.isOk())
            strErrorMessage = UIErrorString::formatErrorInfo(comVBox);
        else
            return comProviderManager;
    }
    /* Null by default: */
    return CCloudProviderManager();
}

CCloudProvider UICloudNetworkingStuff::cloudProviderByShortName(const QString &strProviderShortName,
                                                                UINotificationCenter *pParent /* = 0 */)
{
    /* Acquire cloud provider manager: */
    CCloudProviderManager comProviderManager = cloudProviderManager(pParent);
    if (comProviderManager.isNotNull())
    {
        /* Acquire cloud provider: */
        CCloudProvider comProvider = comProviderManager.GetProviderByShortName(strProviderShortName);
        if (!comProviderManager.isOk())
            UINotificationMessage::cannotAcquireCloudProviderManagerParameter(comProviderManager, pParent);
        else
            return comProvider;
    }
    /* Null by default: */
    return CCloudProvider();
}

CCloudProvider UICloudNetworkingStuff::cloudProviderByShortName(const QString &strProviderShortName,
                                                                QString &strErrorMessage)
{
    /* Acquire cloud provider manager: */
    CCloudProviderManager comProviderManager = cloudProviderManager(strErrorMessage);
    if (comProviderManager.isNotNull())
    {
        /* Acquire cloud provider: */
        CCloudProvider comProvider = comProviderManager.GetProviderByShortName(strProviderShortName);
        if (!comProviderManager.isOk())
            strErrorMessage = UIErrorString::formatErrorInfo(comProviderManager);
        else
            return comProvider;
    }
    /* Null by default: */
    return CCloudProvider();
}

CCloudProfile UICloudNetworkingStuff::cloudProfileByName(const QString &strProviderShortName,
                                                         const QString &strProfileName,
                                                         UINotificationCenter *pParent /* = 0 */)
{
    /* Acquire cloud provider: */
    CCloudProvider comProvider = cloudProviderByShortName(strProviderShortName, pParent);
    if (comProvider.isNotNull())
    {
        /* Acquire cloud profile: */
        CCloudProfile comProfile = comProvider.GetProfileByName(strProfileName);
        if (!comProvider.isOk())
            UINotificationMessage::cannotAcquireCloudProviderParameter(comProvider, pParent);
        else
            return comProfile;
    }
    /* Null by default: */
    return CCloudProfile();
}

CCloudProfile UICloudNetworkingStuff::cloudProfileByName(const QString &strProviderShortName,
                                                         const QString &strProfileName,
                                                         QString &strErrorMessage)
{
    /* Acquire cloud provider: */
    CCloudProvider comProvider = cloudProviderByShortName(strProviderShortName, strErrorMessage);
    if (comProvider.isNotNull())
    {
        /* Acquire cloud profile: */
        CCloudProfile comProfile = comProvider.GetProfileByName(strProfileName);
        if (!comProvider.isOk())
            strErrorMessage = UIErrorString::formatErrorInfo(comProvider);
        else
            return comProfile;
    }
    /* Null by default: */
    return CCloudProfile();
}

CCloudClient UICloudNetworkingStuff::cloudClient(CCloudProfile comProfile,
                                                 UINotificationCenter *pParent /* = 0 */)
{
    /* Create cloud client: */
    CCloudClient comClient = comProfile.CreateCloudClient();
    if (!comProfile.isOk())
        UINotificationMessage::cannotCreateCloudClient(comProfile, pParent);
    else
        return comClient;
    /* Null by default: */
    return CCloudClient();
}

CCloudClient UICloudNetworkingStuff::cloudClient(CCloudProfile comProfile,
                                                 QString &strErrorMessage)
{
    /* Create cloud client: */
    CCloudClient comClient = comProfile.CreateCloudClient();
    if (!comProfile.isOk())
        strErrorMessage = UIErrorString::formatErrorInfo(comProfile);
    else
        return comClient;
    /* Null by default: */
    return CCloudClient();
}

CCloudClient UICloudNetworkingStuff::cloudClientByName(const QString &strProviderShortName,
                                                       const QString &strProfileName,
                                                       UINotificationCenter *pParent /* = 0 */)
{
    /* Acquire cloud profile: */
    CCloudProfile comProfile = cloudProfileByName(strProviderShortName, strProfileName, pParent);
    if (comProfile.isNotNull())
        return cloudClient(comProfile, pParent);
    /* Null by default: */
    return CCloudClient();
}

CCloudClient UICloudNetworkingStuff::cloudClientByName(const QString &strProviderShortName,
                                                       const QString &strProfileName,
                                                       QString &strErrorMessage)
{
    /* Acquire cloud profile: */
    CCloudProfile comProfile = cloudProfileByName(strProviderShortName, strProfileName, strErrorMessage);
    if (comProfile.isNotNull())
        return cloudClient(comProfile, strErrorMessage);
    /* Null by default: */
    return CCloudClient();
}

CVirtualSystemDescription UICloudNetworkingStuff::createVirtualSystemDescription(UINotificationCenter *pParent /* = 0 */)
{
    /* Acquire VBox: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();
    if (comVBox.isNotNull())
    {
        /* Create appliance: */
        CAppliance comAppliance = comVBox.CreateAppliance();
        if (!comVBox.isOk())
            UINotificationMessage::cannotCreateAppliance(comVBox, pParent);
        else
        {
            /* Append it with one (1) description we need: */
            comAppliance.CreateVirtualSystemDescriptions(1);
            if (!comAppliance.isOk())
                UINotificationMessage::cannotCreateVirtualSystemDescription(comAppliance, pParent);
            else
            {
                /* Get received description: */
                const QVector<CVirtualSystemDescription> descriptions = comAppliance.GetVirtualSystemDescriptions();
                AssertReturn(!descriptions.isEmpty(), CVirtualSystemDescription());
                return descriptions.at(0);
            }
        }
    }
    /* Null by default: */
    return CVirtualSystemDescription();
}

QVector<CCloudProvider> UICloudNetworkingStuff::listCloudProviders(UINotificationCenter *pParent /* = 0 */)
{
    /* Acquire cloud provider manager: */
    CCloudProviderManager comProviderManager = cloudProviderManager(pParent);
    if (comProviderManager.isNotNull())
    {
        /* Acquire cloud providers: */
        QVector<CCloudProvider> providers = comProviderManager.GetProviders();
        if (!comProviderManager.isOk())
            UINotificationMessage::cannotAcquireCloudProviderManagerParameter(comProviderManager, pParent);
        else
            return providers;
    }
    /* Return empty list by default: */
    return QVector<CCloudProvider>();
}

bool UICloudNetworkingStuff::cloudProviderId(const CCloudProvider &comCloudProvider,
                                             QUuid &uResult,
                                             UINotificationCenter *pParent /* = 0 */)
{
    const QUuid uId = comCloudProvider.GetId();
    if (!comCloudProvider.isOk())
        UINotificationMessage::cannotAcquireCloudProviderParameter(comCloudProvider, pParent);
    else
    {
        uResult = uId;
        return true;
    }
    return false;
}

bool UICloudNetworkingStuff::cloudProviderShortName(const CCloudProvider &comCloudProvider,
                                                    QString &strResult,
                                                    UINotificationCenter *pParent /* = 0 */)
{
    const QString strShortName = comCloudProvider.GetShortName();
    if (!comCloudProvider.isOk())
        UINotificationMessage::cannotAcquireCloudProviderParameter(comCloudProvider, pParent);
    else
    {
        strResult = strShortName;
        return true;
    }
    return false;
}

bool UICloudNetworkingStuff::cloudProviderName(const CCloudProvider &comCloudProvider,
                                               QString &strResult,
                                               UINotificationCenter *pParent /* = 0 */)
{
    const QString strName = comCloudProvider.GetName();
    if (!comCloudProvider.isOk())
        UINotificationMessage::cannotAcquireCloudProviderParameter(comCloudProvider, pParent);
    else
    {
        strResult = strName;
        return true;
    }
    return false;
}

QVector<CCloudProfile> UICloudNetworkingStuff::listCloudProfiles(const CCloudProvider &comCloudProvider,
                                                                 UINotificationCenter *pParent /* = 0 */)
{
    /* Check cloud provider: */
    if (comCloudProvider.isNotNull())
    {
        /* Acquire cloud providers: */
        QVector<CCloudProfile> profiles = comCloudProvider.GetProfiles();
        if (!comCloudProvider.isOk())
            UINotificationMessage::cannotAcquireCloudProviderParameter(comCloudProvider, pParent);
        else
            return profiles;
    }
    /* Return empty list by default: */
    return QVector<CCloudProfile>();
}

bool UICloudNetworkingStuff::cloudProfileName(const CCloudProfile &comCloudProfile,
                                              QString &strResult,
                                              UINotificationCenter *pParent /* = 0 */)
{
    const QString strName = comCloudProfile.GetName();
    if (!comCloudProfile.isOk())
        UINotificationMessage::cannotAcquireCloudProfileParameter(comCloudProfile, pParent);
    else
    {
        strResult = strName;
        return true;
    }
    return false;
}

bool UICloudNetworkingStuff::cloudProfileProperties(const CCloudProfile &comCloudProfile,
                                                    QVector<QString> &keys,
                                                    QVector<QString> &values,
                                                    UINotificationCenter *pParent /* = 0 */)
{
    QVector<QString> aKeys;
    QVector<QString> aValues;
    aValues = comCloudProfile.GetProperties(QString(), aKeys);
    if (!comCloudProfile.isOk())
        UINotificationMessage::cannotAcquireCloudProfileParameter(comCloudProfile, pParent);
    else
    {
        aValues.resize(aKeys.size());
        keys = aKeys;
        values = aValues;
        return true;
    }
    return false;
}

bool UICloudNetworkingStuff::listCloudImages(const CCloudClient &comCloudClient,
                                             CStringArray &comNames,
                                             CStringArray &comIDs,
                                             UINotificationCenter *pParent)
{
    /* Currently we are interested in Available images only: */
    const QVector<KCloudImageState> cloudImageStates  = QVector<KCloudImageState>()
                                                     << KCloudImageState_Available;

    /* List cloud images: */
    UINotificationProgressCloudImageList *pNotification =
        new UINotificationProgressCloudImageList(comCloudClient, cloudImageStates);
    UINotificationReceiver receiver1;
    UINotificationReceiver receiver2;
    QObject::connect(pNotification, &UINotificationProgressCloudImageList::sigImageNamesReceived,
                     &receiver1, &UINotificationReceiver::setReceiverProperty);
    QObject::connect(pNotification, &UINotificationProgressCloudImageList::sigImageIdsReceived,
                     &receiver2, &UINotificationReceiver::setReceiverProperty);
    if (pParent->handleNow(pNotification))
    {
        comNames = receiver1.property("received_value").value<CStringArray>();
        comIDs = receiver2.property("received_value").value<CStringArray>();
        return true;
    }

    /* Return false by default: */
    return false;
}

bool UICloudNetworkingStuff::listCloudSourceBootVolumes(const CCloudClient &comCloudClient,
                                                        CStringArray &comNames,
                                                        CStringArray &comIDs,
                                                        UINotificationCenter *pParent)
{
    /* List cloud source boot volumes: */
    UINotificationProgressCloudSourceBootVolumeList *pNotification =
        new UINotificationProgressCloudSourceBootVolumeList(comCloudClient);
    UINotificationReceiver receiver1;
    UINotificationReceiver receiver2;
    QObject::connect(pNotification, &UINotificationProgressCloudSourceBootVolumeList::sigImageNamesReceived,
                     &receiver1, &UINotificationReceiver::setReceiverProperty);
    QObject::connect(pNotification, &UINotificationProgressCloudSourceBootVolumeList::sigImageIdsReceived,
                     &receiver2, &UINotificationReceiver::setReceiverProperty);
    if (pParent->handleNow(pNotification))
    {
        comNames = receiver1.property("received_value").value<CStringArray>();
        comIDs = receiver2.property("received_value").value<CStringArray>();
        return true;
    }

    /* Return false by default: */
    return false;
}

bool UICloudNetworkingStuff::listCloudInstances(const CCloudClient &comCloudClient,
                                                CStringArray &comNames,
                                                CStringArray &comIDs,
                                                UINotificationCenter *pParent)
{
    /* List cloud instances: */
    UINotificationProgressCloudInstanceList *pNotification =
        new UINotificationProgressCloudInstanceList(comCloudClient);
    UINotificationReceiver receiver1;
    UINotificationReceiver receiver2;
    QObject::connect(pNotification, &UINotificationProgressCloudInstanceList::sigImageNamesReceived,
                     &receiver1, &UINotificationReceiver::setReceiverProperty);
    QObject::connect(pNotification, &UINotificationProgressCloudInstanceList::sigImageIdsReceived,
                     &receiver2, &UINotificationReceiver::setReceiverProperty);
    if (pParent->handleNow(pNotification))
    {
        comNames = receiver1.property("received_value").value<CStringArray>();
        comIDs = receiver2.property("received_value").value<CStringArray>();
        return true;
    }

    /* Return false by default: */
    return false;
}

bool UICloudNetworkingStuff::listCloudSourceInstances(const CCloudClient &comCloudClient,
                                                      CStringArray &comNames,
                                                      CStringArray &comIDs,
                                                      UINotificationCenter *pParent)
{
    /* List cloud source instances: */
    UINotificationProgressCloudSourceInstanceList *pNotification =
        new UINotificationProgressCloudSourceInstanceList(comCloudClient);
    UINotificationReceiver receiver1;
    UINotificationReceiver receiver2;
    QObject::connect(pNotification, &UINotificationProgressCloudSourceInstanceList::sigImageNamesReceived,
                     &receiver1, &UINotificationReceiver::setReceiverProperty);
    QObject::connect(pNotification, &UINotificationProgressCloudSourceInstanceList::sigImageIdsReceived,
                     &receiver2, &UINotificationReceiver::setReceiverProperty);
    if (pParent->handleNow(pNotification))
    {
        comNames = receiver1.property("received_value").value<CStringArray>();
        comIDs = receiver2.property("received_value").value<CStringArray>();
        return true;
    }

    /* Return false by default: */
    return false;
}

bool UICloudNetworkingStuff::exportDescriptionForm(const CCloudClient &comCloudClient,
                                                   const CVirtualSystemDescription &comDescription,
                                                   CVirtualSystemDescriptionForm &comResult,
                                                   UINotificationCenter *pParent)
{
    /* Prepare export VSD form: */
    UINotificationProgressExportVSDFormCreate *pNotification =
        new UINotificationProgressExportVSDFormCreate(comCloudClient, comDescription);
    UINotificationReceiver receiver;
    QObject::connect(pNotification, &UINotificationProgressExportVSDFormCreate::sigVSDFormCreated,
                     &receiver, &UINotificationReceiver::setReceiverProperty);
    if (pParent->handleNow(pNotification))
    {
        comResult = receiver.property("received_value").value<CVirtualSystemDescriptionForm>();
        return true;
    }

    /* False by default: */
    return false;
}

bool UICloudNetworkingStuff::importDescriptionForm(const CCloudClient &comCloudClient,
                                                   const CVirtualSystemDescription &comDescription,
                                                   CVirtualSystemDescriptionForm &comResult,
                                                   UINotificationCenter *pParent)
{
    /* Prepare import VSD form: */
    UINotificationProgressImportVSDFormCreate *pNotification =
        new UINotificationProgressImportVSDFormCreate(comCloudClient, comDescription);
    UINotificationReceiver receiver;
    QObject::connect(pNotification, &UINotificationProgressImportVSDFormCreate::sigVSDFormCreated,
                     &receiver, &UINotificationReceiver::setReceiverProperty);
    if (pParent->handleNow(pNotification))
    {
        comResult = receiver.property("received_value").value<CVirtualSystemDescriptionForm>();
        return true;
    }

    /* False by default: */
    return false;
}

bool UICloudNetworkingStuff::cloudMachineId(const CCloudMachine &comCloudMachine,
                                            QUuid &uResult,
                                            UINotificationCenter *pParent /* = 0 */)
{
    const QUuid uId = comCloudMachine.GetId();
    if (!comCloudMachine.isOk())
        UINotificationMessage::cannotAcquireCloudMachineParameter(comCloudMachine, pParent);
    else
    {
        uResult = uId;
        return true;
    }
    return false;
}

bool UICloudNetworkingStuff::cloudMachineName(const CCloudMachine &comCloudMachine,
                                              QString &strResult,
                                              UINotificationCenter *pParent /* = 0 */)
{
    const QString strName = comCloudMachine.GetName();
    if (!comCloudMachine.isOk())
        UINotificationMessage::cannotAcquireCloudMachineParameter(comCloudMachine, pParent);
    else
    {
        strResult = strName;
        return true;
    }
    return false;
}

bool UICloudNetworkingStuff::cloudMachineConsoleConnectionFingerprint(const CCloudMachine &comCloudMachine,
                                                                      QString &strResult,
                                                                      UINotificationCenter *pParent /* = 0 */)
{
    const QString strConsoleConnectionFingerprint = comCloudMachine.GetConsoleConnectionFingerprint();
    if (!comCloudMachine.isOk())
        UINotificationMessage::cannotAcquireCloudMachineParameter(comCloudMachine, pParent);
    else
    {
        strResult = strConsoleConnectionFingerprint;
        return true;
    }
    return false;
}

bool UICloudNetworkingStuff::cloudMachineSettingsForm(const CCloudMachine &comCloudMachine,
                                                      CForm &comResult,
                                                      UINotificationCenter *pParent)
{
    /* Acquire machine name first: */
    QString strMachineName;
    if (!cloudMachineName(comCloudMachine, strMachineName))
        return false;

    /* Prepare VM settings form: */
    UINotificationProgressCloudMachineSettingsFormCreate *pNotification =
        new UINotificationProgressCloudMachineSettingsFormCreate(comCloudMachine, strMachineName);
    UINotificationReceiver receiver;
    QObject::connect(pNotification, &UINotificationProgressCloudMachineSettingsFormCreate::sigSettingsFormCreated,
                     &receiver, &UINotificationReceiver::setReceiverProperty);
    if (pParent->handleNow(pNotification))
    {
        comResult = receiver.property("received_value").value<CForm>();
        return true;
    }

    /* False by default: */
    return false;
}

bool UICloudNetworkingStuff::cloudMachineSettingsForm(CCloudMachine comCloudMachine,
                                                      CForm &comResult,
                                                      QString &strErrorMessage)
{
    /* Prepare settings form: */
    CForm comForm;

    /* Now execute GetSettingsForm async method: */
    CProgress comProgress = comCloudMachine.GetSettingsForm(comForm);
    if (!comCloudMachine.isOk())
    {
        strErrorMessage = UIErrorString::formatErrorInfo(comCloudMachine);
        return false;
    }

    /* Wait for "Get settings form" progress: */
    comProgress.WaitForCompletion(-1);
    if (comProgress.GetCanceled())
        return false;
    if (!comProgress.isOk() || comProgress.GetResultCode() != 0)
    {
        strErrorMessage = UIErrorString::formatErrorInfo(comProgress);
        return false;
    }

    /* Return result: */
    comResult = comForm;
    return true;
}

bool UICloudNetworkingStuff::applyCloudMachineSettingsForm(const CCloudMachine &comCloudMachine,
                                                           const CForm &comForm,
                                                           UINotificationCenter *pParent)
{
    /* Acquire machine name first: */
    QString strMachineName;
    if (!cloudMachineName(comCloudMachine, strMachineName))
        return false;

    /* Apply VM settings form: */
    UINotificationProgressCloudMachineSettingsFormApply *pNotification =
        new UINotificationProgressCloudMachineSettingsFormApply(comForm, strMachineName);
    return pParent->handleNow(pNotification);
}

void UICloudNetworkingStuff::createCloudMachineClone(const QString &strProviderShortName,
                                                     const QString &strProfileName,
                                                     const CCloudMachine &comCloudMachine,
                                                     const QString &strCloneName,
                                                     UINotificationCenter *pParent)
{
    /* Create cloud client: */
    CCloudClient comCloudClient = cloudClientByName(strProviderShortName,
                                                    strProfileName,
                                                    pParent);
    if (comCloudClient.isNotNull())
    {
        /* Clone specified cloud machine asynchronously: */
        UINotificationProgressCloudMachineClone *pNotification =
            new UINotificationProgressCloudMachineClone(comCloudClient, comCloudMachine, strCloneName);
        pParent->append(pNotification);
    }
}
