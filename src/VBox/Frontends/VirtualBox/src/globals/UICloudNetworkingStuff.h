/* $Id: UICloudNetworkingStuff.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudNetworkingStuff namespace declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UICloudNetworkingStuff_h
#define FEQT_INCLUDED_SRC_globals_UICloudNetworkingStuff_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UINotificationCenter.h"

/* COM includes: */
#include "CCloudClient.h"
#include "CCloudMachine.h"
#include "CCloudProfile.h"
#include "CCloudProvider.h"
#include "CCloudProviderManager.h"
#include "CForm.h"

/** Cloud networking stuff namespace. */
namespace UICloudNetworkingStuff
{
    /** Acquires cloud provider manager,
      * using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF CCloudProviderManager cloudProviderManager(UINotificationCenter *pParent = 0);
    /** Acquires cloud provider manager,
      * using @a strErrorMessage to store messages to. */
    SHARED_LIBRARY_STUFF CCloudProviderManager cloudProviderManager(QString &strErrorMessage);
    /** Acquires cloud provider specified by @a strProviderShortName,
      * using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF CCloudProvider cloudProviderByShortName(const QString &strProviderShortName,
                                                                 UINotificationCenter *pParent = 0);
    /** Acquires cloud provider specified by @a strProviderShortName,
      * using @a strErrorMessage to store messages to. */
    SHARED_LIBRARY_STUFF CCloudProvider cloudProviderByShortName(const QString &strProviderShortName,
                                                                 QString &strErrorMessage);
    /** Acquires cloud profile specified by @a strProviderShortName and @a strProfileName,
      * using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF CCloudProfile cloudProfileByName(const QString &strProviderShortName,
                                                          const QString &strProfileName,
                                                          UINotificationCenter *pParent = 0);
    /** Acquires cloud profile specified by @a strProviderShortName and @a strProfileName,
      * using @a strErrorMessage to store messages to. */
    SHARED_LIBRARY_STUFF CCloudProfile cloudProfileByName(const QString &strProviderShortName,
                                                          const QString &strProfileName,
                                                          QString &strErrorMessage);
    /** Acquires cloud client created for @a comProfile,
      * using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF CCloudClient cloudClient(CCloudProfile comProfile,
                                                  UINotificationCenter *pParent = 0);
    /** Acquires cloud client created for @a comProfile,
      * using @a strErrorMessage to store messages to. */
    SHARED_LIBRARY_STUFF CCloudClient cloudClient(CCloudProfile comProfile,
                                                  QString &strErrorMessage);
    /** Acquires cloud client specified by @a strProviderShortName and @a strProfileName,
      * using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF CCloudClient cloudClientByName(const QString &strProviderShortName,
                                                        const QString &strProfileName,
                                                        UINotificationCenter *pParent = 0);
    /** Acquires cloud client specified by @a strProviderShortName and @a strProfileName,
      * using @a strErrorMessage to store messages to. */
    SHARED_LIBRARY_STUFF CCloudClient cloudClientByName(const QString &strProviderShortName,
                                                        const QString &strProfileName,
                                                        QString &strErrorMessage);

    /** Creates virtual system description, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF CVirtualSystemDescription createVirtualSystemDescription(UINotificationCenter *pParent = 0);

    /** Acquires cloud providers, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF QVector<CCloudProvider> listCloudProviders(UINotificationCenter *pParent = 0);

    /** Acquires @a comCloudProvider ID as a @a uResult, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool cloudProviderId(const CCloudProvider &comCloudProvider,
                                              QUuid &uResult,
                                              UINotificationCenter *pParent = 0);
    /** Acquires @a comCloudProvider short name as a @a strResult, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool cloudProviderShortName(const CCloudProvider &comCloudProvider,
                                                     QString &strResult,
                                                     UINotificationCenter *pParent = 0);
    /** Acquires @a comCloudProvider name as a @a strResult, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool cloudProviderName(const CCloudProvider &comCloudProvider,
                                                QString &strResult,
                                                UINotificationCenter *pParent = 0);

    /** Acquires cloud profiles of certain @a comCloudProvider, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF QVector<CCloudProfile> listCloudProfiles(const CCloudProvider &comCloudProvider,
                                                                  UINotificationCenter *pParent = 0);

    /** Acquires @a comCloudProfile name as a @a strResult, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool cloudProfileName(const CCloudProfile &comCloudProfile,
                                               QString &strResult,
                                               UINotificationCenter *pParent = 0);
    /** Acquires @a comCloudProfile properties as a @a keys/values using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool cloudProfileProperties(const CCloudProfile &comCloudProfile,
                                                     QVector<QString> &keys,
                                                     QVector<QString> &values,
                                                     UINotificationCenter *pParent = 0);

    /** Acquires cloud images of certain @a comCloudClient, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool listCloudImages(const CCloudClient &comCloudClient,
                                              CStringArray &comNames,
                                              CStringArray &comIDs,
                                              UINotificationCenter *pParent);
    /** Acquires cloud source boot volumes of certain @a comCloudClient, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool listCloudSourceBootVolumes(const CCloudClient &comCloudClient,
                                                         CStringArray &comNames,
                                                         CStringArray &comIDs,
                                                         UINotificationCenter *pParent);
    /** Acquires cloud instances of certain @a comCloudClient, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool listCloudInstances(const CCloudClient &comCloudClient,
                                                 CStringArray &comNames,
                                                 CStringArray &comIDs,
                                                 UINotificationCenter *pParent);
    /** Acquires cloud source instances of certain @a comCloudClient, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool listCloudSourceInstances(const CCloudClient &comCloudClient,
                                                       CStringArray &comNames,
                                                       CStringArray &comIDs,
                                                       UINotificationCenter *pParent);

    /** Acquires @a comCloudClient export description form as a @a comResult, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool exportDescriptionForm(const CCloudClient &comCloudClient,
                                                    const CVirtualSystemDescription &comDescription,
                                                    CVirtualSystemDescriptionForm &comResult,
                                                    UINotificationCenter *pParent);
    /** Acquires @a comCloudClient import description form as a @a comResult, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool importDescriptionForm(const CCloudClient &comCloudClient,
                                                    const CVirtualSystemDescription &comDescription,
                                                    CVirtualSystemDescriptionForm &comResult,
                                                    UINotificationCenter *pParent);

    /** Acquires @a comCloudMachine ID as a @a uResult, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool cloudMachineId(const CCloudMachine &comCloudMachine,
                                             QUuid &uResult,
                                             UINotificationCenter *pParent = 0);
    /** Acquires @a comCloudMachine name as a @a strResult, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool cloudMachineName(const CCloudMachine &comCloudMachine,
                                               QString &strResult,
                                               UINotificationCenter *pParent = 0);
    /** Acquires @a comCloudMachine console connection fingerprint as a @a strResult,
      * using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool cloudMachineConsoleConnectionFingerprint(const CCloudMachine &comCloudMachine,
                                                                       QString &strResult,
                                                                       UINotificationCenter *pParent = 0);

    /** Acquires @a comCloudMachine settings form as a @a comResult, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool cloudMachineSettingsForm(const CCloudMachine &comCloudMachine,
                                                       CForm &comResult,
                                                       UINotificationCenter *pParent);
    /** Acquires @a comCloudMachine settings form as a @a comResult, using @a strErrorMessage to store messages to.
      * @note  Be aware, this is a blocking function, it will hang for a time of progress being executed. */
    SHARED_LIBRARY_STUFF bool cloudMachineSettingsForm(CCloudMachine comCloudMachine,
                                                       CForm &comResult,
                                                       QString &strErrorMessage);

    /** Applies @a comCloudMachine @a comForm settings, using @a pParent to show messages according to. */
    SHARED_LIBRARY_STUFF bool applyCloudMachineSettingsForm(const CCloudMachine &comCloudMachine,
                                                            const CForm &comForm,
                                                            UINotificationCenter *pParent);

    /** Creates cloud machine clone.
      * @param  strProviderShortName  Brings short provider name.
      * @param  strProfileName        Brings profile name.
      * @param  comCloudMachine       Brings cloud machine instance of which being cloned.
      * @param  strCloneName          Brings clone name.
      * @param  pParent               Brings notification-center reference. */
    SHARED_LIBRARY_STUFF void createCloudMachineClone(const QString &strProviderShortName,
                                                      const QString &strProfileName,
                                                      const CCloudMachine &comCloudMachine,
                                                      const QString &strCloneName,
                                                      UINotificationCenter *pParent);
}

/* Using across any module who included us: */
using namespace UICloudNetworkingStuff;

#endif /* !FEQT_INCLUDED_SRC_globals_UICloudNetworkingStuff_h */
