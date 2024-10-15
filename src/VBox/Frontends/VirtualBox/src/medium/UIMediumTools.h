/* $Id: UIMediumTools.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumTools class declaration.
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

#ifndef FEQT_INCLUDED_SRC_medium_UIMediumTools_h
#define FEQT_INCLUDED_SRC_medium_UIMediumTools_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QUuid>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UIMediumDefs.h"

/* Forward declarations: */
class QMenu;
class QObject;
class QString;
class QWidget;
class UIActionPool;
class CMachine;
class CMedium;

/** UIMediumTools namespace. */
namespace UIMediumTools
{
    /** Generates details for passed @a comMedium.
      * @param  fPredictDiff  Brings whether medium will be marked differencing on attaching.
      * @param  fUseHtml      Brings whether HTML subsets should be used in the generated output. */
    SHARED_LIBRARY_STUFF QString storageDetails(const CMedium &comMedium,
                                                bool fPredictDiff,
                                                bool fUseHtml = true);

    /** Calculates @a cAmount of immutable images used by @a comMachine specified. */
    SHARED_LIBRARY_STUFF bool acquireAmountOfImmutableImages(const CMachine &comMachine,
                                                             ulong &cAmount);

    /** Searches extra data for the recently used folder path which corresponds to @a enmMediumType.
      * When that search fails it looks for recent folder extra data for other medium types.
      * As the last resort returns default vm folder path.
      * @param  enmMediumType  Passes the medium type. */
    SHARED_LIBRARY_STUFF QString defaultFolderPathForType(UIMediumDeviceType enmMediumType);

    /** Opens external medium from passed @a strMediumLocation.
      * @param  enmMediumType      Brings the medium type.
      * @param  strMediumLocation  Brings the file path to load medium from.
      * @param  pParent            Brings the error dialog parent. */
    SHARED_LIBRARY_STUFF QUuid openMedium(UIMediumDeviceType enmMediumType,
                                          const QString &strMediumLocation,
                                          QWidget *pParent = 0);

    /** Opens external medium using file-open dialog.
      * @param  enmMediumType     Brings the medium type.
      * @param  pParent           Brings the dialog parent.
      * @param  strDefaultFolder  Brings the folder to browse for medium.
      * @param  fUseLastFolder    Brings whether we should propose to use last used folder. */
    SHARED_LIBRARY_STUFF QUuid openMediumWithFileOpenDialog(UIMediumDeviceType enmMediumType,
                                                            QWidget *pParent = 0,
                                                            const QString &strDefaultFolder = QString(),
                                                            bool fUseLastFolder = false);

    /** Creates and shows a dialog (wizard) to create a medium of type @a enmMediumType.
      * @param  pActionPool              Brings the action-pool.
      * @param  pParent                  Brings the parent of the dialog,
      * @param  enmMediumType            Brings the medium type,
      * @param  strMachineFolder         Brings the machine folder,
      * @param  strMachineName           Brings the name of the machine,
      * @param  strMachineGuestOSTypeId  Brings the type ID of machine's guest os,
      * returns QUuid of the new medium. */
    SHARED_LIBRARY_STUFF QUuid openMediumCreatorDialog(UIActionPool *pActionPool,
                                                       QWidget *pParent,
                                                       UIMediumDeviceType enmMediumType,
                                                       const QString &strMachineFolder = QString(),
                                                       const QString &strMachineName = QString(),
                                                       const QString &strMachineGuestOSTypeId = QString());

    /** Prepares storage menu according passed parameters.
      * @param  pMenu              Brings the #QMenu to be prepared.
      * @param  pListener          Brings the listener #QObject, this @a menu being prepared for.
      * @param  pszSlotName        Brings the name of the SLOT in the @a pListener above, this menu will be handled with.
      * @param  comMachine         Brings the #CMachine object, this @a menu being prepared for.
      * @param  strControllerName  Brings the name of the #CStorageController in the @a machine above.
      * @param  storageSlot        Brings the #StorageSlot of the storage controller with @a strControllerName above. */
    SHARED_LIBRARY_STUFF void prepareStorageMenu(QMenu *pMenu,
                                                 QObject *pListener,
                                                 const char *pszSlotName,
                                                 const CMachine &comMachine,
                                                 const QString &strControllerName,
                                                 const StorageSlot &storageSlot);

    /** Updates @a comConstMachine storage with data described by @a target.
      * @param  comConstMachine  Brings the machine to update.
      * @param  target           Brings the medium target to update machine with.
      * @param  pActionPool      Brings the action-pool. */
    SHARED_LIBRARY_STUFF void updateMachineStorage(const CMachine &comConstMachine,
                                                   const UIMediumTarget &target,
                                                   UIActionPool *pActionPool);
}
/* Using this namespace globally: */
using namespace UIMediumTools;

#endif /* !FEQT_INCLUDED_SRC_medium_UIMediumTools_h */
