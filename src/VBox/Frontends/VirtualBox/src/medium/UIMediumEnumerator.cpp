/* $Id: UIMediumEnumerator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumEnumerator class implementation.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <QFileInfo>
#include <QSet>

/* GUI includes: */
#include "UICommon.h"
#include "UIErrorString.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UILoggingDefs.h"
#include "UIMediumEnumerator.h"
#include "UINotificationCenter.h"
#include "UITask.h"
#include "UITranslationEventListener.h"
#include "UIThreadPool.h"
#include "UIVirtualBoxEventHandler.h"

/* COM includes: */
#include "CMachine.h"
#include "CMediumAttachment.h"
#include "CSnapshot.h"


/** Template function to convert a list of
  * abstract objects to a human readable string list.
  * @note T should have .toString() member implemented. */
template<class T> static QStringList toStringList(const QList<T> &list)
{
    QStringList l;
    foreach(const T &t, list)
        l << t.toString();
    return l;
}


/** UITask extension used for medium-enumeration purposes.
  * @note We made setting/getting medium a thread-safe stuff. But this wasn't
  *       dangerous for us before since setter/getter calls are splitted in time
  *       by enumeration logic. Previously we were even using
  *       property/setProperty API for that but latest Qt versions prohibits
  *       property/setProperty API usage from other than the GUI thread so we
  *       had to rework that stuff to be thread-safe for Qt >= 5.11. */
class UITaskMediumEnumeration : public UITask
{
    Q_OBJECT;

public:

    /** Constructs @a guiMedium enumeration task. */
    UITaskMediumEnumeration(const UIMedium &guiMedium)
        : UITask(UITask::Type_MediumEnumeration)
        , m_guiMedium(guiMedium)
    {}

    /** Returns GUI medium. */
    UIMedium medium() const
    {
        /* Acquire under a proper lock: */
        m_mutex.lock();
        const UIMedium guiMedium = m_guiMedium;
        m_mutex.unlock();
        return guiMedium;
    }

private:

    /** Contains medium-enumeration task body. */
    virtual void run() RT_OVERRIDE
    {
        /* Enumerate under a proper lock: */
        m_mutex.lock();
        m_guiMedium.blockAndQueryState();
        m_mutex.unlock();
    }

    /** Holds the mutex to access m_guiMedium member. */
    mutable QMutex  m_mutex;
    /** Holds the medium being enumerated. */
    UIMedium        m_guiMedium;
};


/*********************************************************************************************************************************
*   Class UIMediumEnumerator implementation.                                                                                     *
*********************************************************************************************************************************/

/* static */
UIMediumEnumerator *UIMediumEnumerator::s_pInstance = 0;

/* static */
QReadWriteLock UIMediumEnumerator::s_guiCleanupProtectionToken = QReadWriteLock();

/* static */
void UIMediumEnumerator::create()
{
    AssertReturnVoid(!s_pInstance);
    s_pInstance = new UIMediumEnumerator;
}

/* static */
void UIMediumEnumerator::destroy()
{
    AssertPtrReturnVoid(s_pInstance);

    s_guiCleanupProtectionToken.lockForWrite();
    delete s_pInstance;
    s_pInstance = 0;
    s_guiCleanupProtectionToken.unlock();
}

/* static */
UIMediumEnumerator *UIMediumEnumerator::instance()
{
    /* This is the fallback behavior, we need the lazy-init here
     * only to make sure gpMediumEnumerator is never NULL. */
    AssertPtr(s_pInstance);
    if (!s_pInstance)
        create();
    return s_pInstance;
}

/* static */
bool UIMediumEnumerator::exists()
{
    return !!s_pInstance;
}

UIMediumEnumerator::UIMediumEnumerator()
    : m_fFullMediumEnumerationRequested(false)
    , m_fMediumEnumerationInProgress(false)
{
    /* Allow UIMedium to be used in inter-thread signals: */
    qRegisterMetaType<UIMedium>();

    /* Prepare Main event handlers: */
    /* Machine related events: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineDataChange,
            this, &UIMediumEnumerator::sltHandleMachineDataChange);
    /* Medium related events: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigStorageControllerChange,
            this, &UIMediumEnumerator::sltHandleStorageControllerChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigStorageDeviceChange,
            this, &UIMediumEnumerator::sltHandleStorageDeviceChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMediumChange,
            this, &UIMediumEnumerator::sltHandleMediumChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMediumConfigChange,
            this, &UIMediumEnumerator::sltHandleMediumConfigChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMediumRegistered,
            this, &UIMediumEnumerator::sltHandleMediumRegistered);

    /* Prepare global thread-pool listener: */
    connect(uiCommon().threadPool(), &UIThreadPool::sigTaskComplete,
            this, &UIMediumEnumerator::sltHandleMediumEnumerationTaskComplete);

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIMediumEnumerator::sltRetranslateUI);

    /* We should make sure media map contains at least NULL medium object: */
    addNullMediumToMap(m_media);
    /* Notify listener about initial enumeration started/finished instantly: */
    LogRel(("GUI: UIMediumEnumerator: Initial medium-enumeration finished!\n"));
    emit sigMediumEnumerationStarted();
    emit sigMediumEnumerationFinished();

    /* Populate the list of medium names to be excluded from the recently used media extra data: */
#if 0 /* bird: This is counter productive as it is _frequently_ necessary to re-insert the
               viso to refresh the files (like after you rebuilt them on the host).
               The guest caches ISOs aggressively and files sizes may change. */
    m_recentMediaExcludeList << "ad-hoc.viso";
#endif
}

QList<QUuid> UIMediumEnumerator::mediumIDs() const
{
    /* Redirect request to subroutine under proper lock: */
    if (s_guiCleanupProtectionToken.tryLockForRead())
    {
        const QList<QUuid> mediaList = mediumIDsSub();
        s_guiCleanupProtectionToken.unlock();
        return mediaList;
    }
    return QList<QUuid>();
}

UIMedium UIMediumEnumerator::medium(const QUuid &uMediumID) const
{
    /* Redirect request to subroutine under proper lock: */
    if (s_guiCleanupProtectionToken.tryLockForRead())
    {
        const UIMedium guiMedium = mediumSub(uMediumID);
        s_guiCleanupProtectionToken.unlock();
        return guiMedium;
    }
    return UIMedium();
}

void UIMediumEnumerator::createMedium(const UIMedium &guiMedium)
{
    /* Redirect request to subroutine under proper lock: */
    if (s_guiCleanupProtectionToken.tryLockForRead())
    {
        createMediumSub(guiMedium);
        s_guiCleanupProtectionToken.unlock();
    }
}

void UIMediumEnumerator::enumerateMedia(const CMediumVector &comMedia /* = CMediumVector() */)
{
    /* Make sure UICommon is already valid: */
    AssertReturnVoid(uiCommon().isValid());
    /* Ignore the request during UICommon cleanup: */
    if (uiCommon().isCleaningUp())
        return;
    /* Ignore the request during startup snapshot restoring: */
    if (uiCommon().shouldRestoreCurrentSnapshot())
        return;

    /* Redirect request to subroutine under proper lock: */
    if (s_guiCleanupProtectionToken.tryLockForRead())
    {
        enumerateMediaSub(comMedia);
        s_guiCleanupProtectionToken.unlock();
    }
}

void UIMediumEnumerator::refreshMedia()
{
    /* Make sure UICommon is already valid: */
    AssertReturnVoid(uiCommon().isValid());
    /* Ignore the request during UICommon cleanup: */
    if (uiCommon().isCleaningUp())
        return;
    /* Ignore the request during startup snapshot restoring: */
    if (uiCommon().shouldRestoreCurrentSnapshot())
        return;

    /* Make sure enumeration is not already started: */
    if (isMediumEnumerationInProgress())
        return;

    /* We assume it's safe to call it without locking,
     * since we are performing blocking operation here. */
    gpMediumEnumerator->refreshMediaSub();
}

void UIMediumEnumerator::updateRecentlyUsedMediumListAndFolder(UIMediumDeviceType enmMediumType, QString strMediumLocation)
{
    /* Don't add the medium to extra data if its name is in exclude list, m_recentMediaExcludeList: */
    foreach (const QString &strExcludeName, m_recentMediaExcludeList)
        if (strMediumLocation.contains(strExcludeName))
            return;

    /* Remember the path of the last chosen medium: */
    switch (enmMediumType)
    {
        case UIMediumDeviceType_HardDisk:
            gEDataManager->setRecentFolderForHardDrives(QFileInfo(strMediumLocation).absolutePath());
            break;
        case UIMediumDeviceType_DVD:
            gEDataManager->setRecentFolderForOpticalDisks(QFileInfo(strMediumLocation).absolutePath());
            break;
        case UIMediumDeviceType_Floppy:
            gEDataManager->setRecentFolderForFloppyDisks(QFileInfo(strMediumLocation).absolutePath());
            break;
        default:
            break;
    }

    /* Update recently used list: */
    QStringList recentMediumList;
    switch (enmMediumType)
    {
        case UIMediumDeviceType_HardDisk: recentMediumList = gEDataManager->recentListOfHardDrives(); break;
        case UIMediumDeviceType_DVD:      recentMediumList = gEDataManager->recentListOfOpticalDisks(); break;
        case UIMediumDeviceType_Floppy:   recentMediumList = gEDataManager->recentListOfFloppyDisks(); break;
        default: break;
    }
    if (recentMediumList.contains(strMediumLocation))
        recentMediumList.removeAll(strMediumLocation);
    recentMediumList.prepend(strMediumLocation);
    while(recentMediumList.size() > 5)
        recentMediumList.removeLast();
    switch (enmMediumType)
    {
        case UIMediumDeviceType_HardDisk: gEDataManager->setRecentListOfHardDrives(recentMediumList); break;
        case UIMediumDeviceType_DVD:      gEDataManager->setRecentListOfOpticalDisks(recentMediumList); break;
        case UIMediumDeviceType_Floppy:   gEDataManager->setRecentListOfFloppyDisks(recentMediumList); break;
        default: break;
    }
    emit sigRecentMediaListUpdated(enmMediumType);
}

void UIMediumEnumerator::sltHandleMediumCreated(const CMedium &comMedium)
{
    /* Acquire device type: */
    const KDeviceType enmDeviceType = comMedium.GetDeviceType();
    if (!comMedium.isOk())
        UINotificationMessage::cannotAcquireMediumParameter(comMedium);
    else
    {
        /* Convert to medium type: */
        const UIMediumDeviceType enmMediumType = mediumTypeToLocal(enmDeviceType);

        /* Make sure we cached created medium in GUI: */
        createMedium(UIMedium(comMedium, enmMediumType, KMediumState_Created));
    }
}

void UIMediumEnumerator::sltRetranslateUI()
{
    /* Translating NULL UIMedium by recreating it: */
    if (m_media.contains(UIMedium::nullID()))
        m_media[UIMedium::nullID()] = UIMedium();
}

void UIMediumEnumerator::sltHandleMachineDataChange(const QUuid &uMachineId)
{
    //printf("MachineDataChange: machine-id=%s\n",
    //       uMachineId.toString().toUtf8().constData());
    LogRel2(("GUI: UIMediumEnumerator: MachineDataChange event received, Machine ID = {%s}\n",
             uMachineId.toString().toUtf8().constData()));

    /* Enumerate all the media of machine with this ID: */
    QList<QUuid> result;
    enumerateAllMediaOfMachineWithId(uMachineId, result);
}

void UIMediumEnumerator::sltHandleStorageControllerChange(const QUuid &uMachineId, const QString &strControllerName)
{
    //printf("StorageControllerChanged: machine-id=%s, controller-name=%s\n",
    //       uMachineId.toString().toUtf8().constData(), strControllerName.toUtf8().constData());
    LogRel2(("GUI: UIMediumEnumerator: StorageControllerChanged event received, Medium ID = {%s}, Controller Name = {%s}\n",
             uMachineId.toString().toUtf8().constData(), strControllerName.toUtf8().constData()));
}

void UIMediumEnumerator::sltHandleStorageDeviceChange(CMediumAttachment comAttachment, bool fRemoved, bool fSilent)
{
    //printf("StorageDeviceChanged: removed=%d, silent=%d\n",
    //       fRemoved, fSilent);
    LogRel2(("GUI: UIMediumEnumerator: StorageDeviceChanged event received, Removed = {%d}, Silent = {%d}\n",
             fRemoved, fSilent));

    /* Parse attachment: */
    QList<QUuid> result;
    parseAttachment(comAttachment, result);
}

void UIMediumEnumerator::sltHandleMediumChange(CMediumAttachment comAttachment)
{
    //printf("MediumChanged\n");
    LogRel2(("GUI: UIMediumEnumerator: MediumChanged event received\n"));

    /* Parse attachment: */
    QList<QUuid> result;
    parseAttachment(comAttachment, result);
}

void UIMediumEnumerator::sltHandleMediumConfigChange(CMedium comMedium)
{
    //printf("MediumConfigChanged\n");
    LogRel2(("GUI: UIMediumEnumerator: MediumConfigChanged event received\n"));

    /* Parse medium: */
    QList<QUuid> result;
    parseMedium(comMedium, result);
}

void UIMediumEnumerator::sltHandleMediumRegistered(const QUuid &uMediumId, KDeviceType enmMediumType, bool fRegistered)
{
    //printf("MediumRegistered: medium-id=%s, medium-type=%d, registered=%d\n",
    //       uMediumId.toString().toUtf8().constData(), enmMediumType, fRegistered);
    //printf(" Medium to recache: %s\n",
    //       uMediumId.toString().toUtf8().constData());
    LogRel2(("GUI: UIMediumEnumerator: MediumRegistered event received, Medium ID = {%s}, Medium type = {%d}, Registered = {%d}\n",
             uMediumId.toString().toUtf8().constData(), enmMediumType, fRegistered));

    /* New medium registered: */
    if (fRegistered)
    {
        /* Make sure this medium isn't already cached: */
        if (!mediumSub(uMediumId).isNull())
        {
            /* This medium can be known because of async event nature. Currently medium registration event comes
             * very late and other even unrelated events can come before it and request for this particular medium
             * enumeration, so we just ignore repetitive events but enumerate this UIMedium at least once if it
             * wasn't registered before. */
            if (!m_registeredMediaIds.contains(uMediumId))
            {
                LogRel2(("GUI: UIMediumEnumerator:  Medium {%s} is cached but not registered already, so will be enumerated..\n",
                         uMediumId.toString().toUtf8().constData()));
                createMediumEnumerationTask(m_media.value(uMediumId));

                /* Mark medium registered: */
                m_registeredMediaIds << uMediumId;
            }
        }
        else
        {
            /* Get VBox for temporary usage, it will cache the error info: */
            CVirtualBox comVBox = gpGlobalSession->virtualBox();
            /* Open existing medium, this API can be used to open known medium as well, using ID as location for that: */
            CMedium comMedium = comVBox.OpenMedium(uMediumId.toString(), enmMediumType, KAccessMode_ReadWrite, false);
            if (!comVBox.isOk())
                LogRel(("GUI: UIMediumEnumerator:  Unable to open registered medium! %s\n",
                        UIErrorString::simplifiedErrorInfo(comVBox).toUtf8().constData()));
            else
            {
                /* Create new UIMedium: */
                const UIMedium guiMedium(comMedium, UIMediumDefs::mediumTypeToLocal(comMedium.GetDeviceType()));
                const QUuid &uUIMediumKey = guiMedium.key();

                /* Cache corresponding UIMedium: */
                m_media.insert(uUIMediumKey, guiMedium);
                LogRel2(("GUI: UIMediumEnumerator:  Medium {%s} is now cached and will be enumerated..\n",
                         uUIMediumKey.toString().toUtf8().constData()));

                /* And notify listeners: */
                emit sigMediumCreated(uUIMediumKey);

                /* Enumerate corresponding UIMedium: */
                createMediumEnumerationTask(m_media.value(uMediumId));

                /* Mark medium registered: */
                m_registeredMediaIds << uMediumId;
            }
        }
    }
    /* Old medium unregistered: */
    else
    {
        /* Make sure this medium is still cached: */
        if (mediumSub(uMediumId).isNull())
        {
            /* This medium can be wiped out already because of async event nature. Currently
             * medium unregistration event comes very late and other even unrealted events
             * can come before it and request for this particular medium enumeration. If medium
             * enumeration is performed fast enough (before medium unregistration event comes),
             * medium will be wiped out already, so we just ignore it. */
            LogRel2(("GUI: UIMediumEnumerator:  Medium {%s} was not currently cached!\n",
                     uMediumId.toString().toUtf8().constData()));
        }
        else
        {
            /* Forget corresponding UIMedium: */
            m_media.remove(uMediumId);
            LogRel2(("GUI: UIMediumEnumerator:  Medium {%s} is no more cached!\n",
                     uMediumId.toString().toUtf8().constData()));

            /* And notify listeners: */
            emit sigMediumDeleted(uMediumId);

            /* Besides that we should enumerate all the
             * 1st level children of deleted medium: */
            QList<QUuid> result;
            enumerateAllMediaOfMediumWithId(uMediumId, result);
        }

        /* Mark medium unregistered: */
        m_registeredMediaIds.remove(uMediumId);
    }
}

void UIMediumEnumerator::sltHandleMediumEnumerationTaskComplete(UITask *pTask)
{
    /* Make sure that is one of our tasks: */
    if (pTask->type() != UITask::Type_MediumEnumeration)
        return;
    AssertReturnVoid(m_tasks.contains(pTask));

    /* Get enumerated UIMedium: */
    const UIMedium guiMedium = qobject_cast<UITaskMediumEnumeration*>(pTask)->medium();
    const QUuid uMediumKey = guiMedium.key();
    LogRel2(("GUI: UIMediumEnumerator: Medium with key={%s} enumerated\n",
             uMediumKey.toString().toUtf8().constData()));

    /* Remove task from internal set: */
    m_tasks.remove(pTask);

    /* Make sure such UIMedium still exists: */
    if (!m_media.contains(uMediumKey))
    {
        LogRel2(("GUI: UIMediumEnumerator: Medium with key={%s} already deleted by a third party\n",
                 uMediumKey.toString().toUtf8().constData()));
        return;
    }

    /* Check if UIMedium ID was changed: */
    const QUuid uMediumID = guiMedium.id();
    /* UIMedium ID was changed to nullID: */
    if (uMediumID == UIMedium::nullID())
    {
        /* Delete this UIMedium: */
        m_media.remove(uMediumKey);
        LogRel2(("GUI: UIMediumEnumerator: Medium with key={%s} closed and deleted (after enumeration)\n",
                 uMediumKey.toString().toUtf8().constData()));

        /* And notify listener about delete: */
        emit sigMediumDeleted(uMediumKey);
    }
    /* UIMedium ID was changed to something proper: */
    else if (uMediumID != uMediumKey)
    {
        /* We have to reinject enumerated UIMedium: */
        m_media.remove(uMediumKey);
        m_media[uMediumID] = guiMedium;
        m_media[uMediumID].setKey(uMediumID);
        LogRel2(("GUI: UIMediumEnumerator: Medium with key={%s} has it changed to {%s}\n",
                 uMediumKey.toString().toUtf8().constData(),
                 uMediumID.toString().toUtf8().constData()));

        /* And notify listener about delete/create: */
        emit sigMediumDeleted(uMediumKey);
        emit sigMediumCreated(uMediumID);
    }
    /* UIMedium ID was not changed: */
    else
    {
        /* Just update enumerated UIMedium: */
        m_media[uMediumID] = guiMedium;
        LogRel2(("GUI: UIMediumEnumerator: Medium with key={%s} updated\n",
                 uMediumID.toString().toUtf8().constData()));

        /* And notify listener about update: */
        emit sigMediumEnumerated(uMediumID);
    }

    /* If there are no more tasks we know about: */
    if (m_tasks.isEmpty())
    {
        /* Notify listener: */
        LogRel(("GUI: UIMediumEnumerator: Medium-enumeration finished!\n"));
        m_fMediumEnumerationInProgress = false;
        emit sigMediumEnumerationFinished();
    }
}

QList<QUuid> UIMediumEnumerator::mediumIDsSub() const
{
    /* Return keys of current media map: */
    return m_media.keys();
}

UIMedium UIMediumEnumerator::mediumSub(const QUuid &uMediumID) const
{
    /* Search through current media map
     * for the UIMedium with passed ID: */
    if (m_media.contains(uMediumID))
        return m_media.value(uMediumID);
    /* Return NULL UIMedium otherwise: */
    return UIMedium();
}

void UIMediumEnumerator::createMediumSub(const UIMedium &guiMedium)
{
    /* Get UIMedium ID: */
    const QUuid uMediumID = guiMedium.id();

    /* Do not create UIMedium(s) with incorrect ID: */
    AssertReturnVoid(!uMediumID.isNull());
    /* Make sure UIMedium doesn't exist already: */
    if (m_media.contains(uMediumID))
        return;

    /* Insert UIMedium: */
    m_media[uMediumID] = guiMedium;
    LogRel(("GUI: UIMediumEnumerator: Medium with key={%s} created\n", uMediumID.toString().toUtf8().constData()));

    /* Notify listener: */
    emit sigMediumCreated(uMediumID);
}

void UIMediumEnumerator::enumerateMediaSub(const CMediumVector &comMedia /* = CMediumVector() */)
{
    /* Compose new map of currently cached media & their children.
     * While composing we are using data from already cached media. */
    UIMediumMap guiMedia;
    addNullMediumToMap(guiMedia);
    if (comMedia.isEmpty())
    {
        /* Compose new map of all known media & their children: */
        addMediaToMap(gpGlobalSession->virtualBox().GetHardDisks(), guiMedia);
        addMediaToMap(gpGlobalSession->host().GetDVDDrives(), guiMedia);
        addMediaToMap(gpGlobalSession->virtualBox().GetDVDImages(), guiMedia);
        addMediaToMap(gpGlobalSession->host().GetFloppyDrives(), guiMedia);
        addMediaToMap(gpGlobalSession->virtualBox().GetFloppyImages(), guiMedia);
    }
    else
    {
        /* Compose new map of passed media & their children: */
        addMediaToMap(comMedia, guiMedia);
    }

    /* UICommon is cleaning up, abort immediately: */
    if (uiCommon().isCleaningUp())
        return;

    if (comMedia.isEmpty())
    {
        /* Replace existing media map since
         * we have full medium enumeration: */
        m_fFullMediumEnumerationRequested = true;
        m_media = guiMedia;
    }
    else
    {
        /* Throw the media to existing map: */
        foreach (const QUuid &uMediumId, guiMedia.keys())
            m_media[uMediumId] = guiMedia.value(uMediumId);
    }

    /* If enumeration hasn't yet started: */
    if (!m_fMediumEnumerationInProgress)
    {
        /* Notify listener about enumeration started: */
        LogRel(("GUI: UIMediumEnumerator: Medium-enumeration started...\n"));
        m_fMediumEnumerationInProgress = true;
        emit sigMediumEnumerationStarted();

        /* Make sure we really have more than one UIMedium (which is NULL): */
        if (   guiMedia.size() == 1
            && guiMedia.first().id() == UIMedium::nullID())
        {
            /* Notify listener about enumeration finished instantly: */
            LogRel(("GUI: UIMediumEnumerator: Medium-enumeration finished!\n"));
            m_fMediumEnumerationInProgress = false;
            emit sigMediumEnumerationFinished();
        }
    }

    /* Start enumeration for media with non-NULL ID: */
    foreach (const QUuid &uMediumID, guiMedia.keys())
        if (!uMediumID.isNull())
            createMediumEnumerationTask(guiMedia[uMediumID]);
}

void UIMediumEnumerator::refreshMediaSub()
{
    /* Make sure we are not already in progress: */
    AssertReturnVoid(!m_fMediumEnumerationInProgress);

    /* Refresh all cached media we have: */
    foreach (const QUuid &uMediumID, m_media.keys())
        m_media[uMediumID].refresh();
}

void UIMediumEnumerator::createMediumEnumerationTask(const UIMedium &guiMedium)
{
    /* Prepare medium-enumeration task: */
    UITask *pTask = new UITaskMediumEnumeration(guiMedium);
    /* Append to internal set: */
    m_tasks << pTask;
    /* Post into global thread-pool: */
    uiCommon().threadPool()->enqueueTask(pTask);
}

void UIMediumEnumerator::addNullMediumToMap(UIMediumMap &media)
{
    /* Insert NULL UIMedium to the passed media map.
     * Get existing one from the previous map if any. */
    const UIMedium guiMedium = m_media.contains(UIMedium::nullID())
                             ? m_media[UIMedium::nullID()]
                             : UIMedium();
    media.insert(UIMedium::nullID(), guiMedium);
}

void UIMediumEnumerator::addMediaToMap(const CMediumVector &inputMedia, UIMediumMap &outputMedia)
{
    /* Iterate through passed inputMedia vector: */
    foreach (const CMedium &comMedium, inputMedia)
    {
        /* If UICommon is cleaning up, abort immediately: */
        if (uiCommon().isCleaningUp())
            break;

        /* Insert UIMedium to the passed media map.
         * Get existing one from the previous map if any.
         * Create on the basis of comMedium otherwise. */
        const QUuid uMediumID = comMedium.GetId();
        const UIMedium guiMedium = m_media.contains(uMediumID)
                                 ? m_media.value(uMediumID)
                                 : UIMedium(comMedium, UIMediumDefs::mediumTypeToLocal(comMedium.GetDeviceType()));
        outputMedia.insert(guiMedium.id(), guiMedium);

        /* Insert comMedium children into map as well: */
        addMediaToMap(comMedium.GetChildren(), outputMedia);
    }
}

void UIMediumEnumerator::parseAttachment(CMediumAttachment comAttachment, QList<QUuid> &result)
{
    /* Make sure attachment is valid: */
    if (comAttachment.isNull())
    {
        LogRel2(("GUI: UIMediumEnumerator:  Attachment is NULL!\n"));
        /// @todo is this possible case?
        AssertFailed();
    }
    else
    {
        /* Acquire attachment medium: */
        CMedium comMedium = comAttachment.GetMedium();
        if (!comAttachment.isOk())
            LogRel(("GUI: UIMediumEnumerator:  Unable to acquire attachment medium! %s\n",
                    UIErrorString::simplifiedErrorInfo(comAttachment).toUtf8().constData()));
        else
        {
            /* Parse medium: */
            parseMedium(comMedium, result);

            // WORKAROUND:
            // In current architecture there is no way to determine medium previously mounted
            // to this attachment, so we will have to enumerate all other cached media which
            // belongs to the same VM, since they may no longer belong to it.

            /* Acquire parent VM: */
            CMachine comMachine = comAttachment.GetMachine();
            if (!comAttachment.isOk())
                LogRel(("GUI: UIMediumEnumerator:  Unable to acquire attachment parent machine! %s\n",
                        UIErrorString::simplifiedErrorInfo(comAttachment).toUtf8().constData()));
            else
            {
                /* Acquire machine ID: */
                const QUuid uMachineId = comMachine.GetId();
                if (!comMachine.isOk())
                    LogRel(("GUI: UIMediumEnumerator:  Unable to acquire machine ID! %s\n",
                            UIErrorString::simplifiedErrorInfo(comMachine).toUtf8().constData()));
                else
                {
                    /* Enumerate all the media of machine with this ID: */
                    enumerateAllMediaOfMachineWithId(uMachineId, result);
                }
            }
        }
    }
}

void UIMediumEnumerator::parseMedium(CMedium comMedium, QList<QUuid> &result)
{
    /* Make sure medium is valid: */
    if (comMedium.isNull())
    {
        /* This medium is NULL by some reason, the obvious case when this
         * can happen is when optical/floppy device is created empty. */
        LogRel2(("GUI: UIMediumEnumerator:  Medium is NULL!\n"));
    }
    else
    {
        /* Acquire medium ID: */
        const QUuid uMediumId = comMedium.GetId();
        if (!comMedium.isOk())
            LogRel(("GUI: UIMediumEnumerator:  Unable to acquire medium ID! %s\n",
                    UIErrorString::simplifiedErrorInfo(comMedium).toUtf8().constData()));
        else
        {
            //printf(" Medium to recache: %s\n", uMediumId.toString().toUtf8().constData());

            /* Make sure this medium is already cached: */
            if (mediumSub(uMediumId).isNull())
            {
                /* This medium isn't cached by some reason, which can be different.
                 * One of such reasons is when config-changed event comes earlier than
                 * corresponding registration event. For now we are ignoring that at all. */
                LogRel2(("GUI: UIMediumEnumerator:  Medium {%s} isn't cached yet!\n",
                         uMediumId.toString().toUtf8().constData()));
            }
            else
            {
                /* Enumerate corresponding UIMedium: */
                LogRel2(("GUI: UIMediumEnumerator:  Medium {%s} will be enumerated..\n",
                         uMediumId.toString().toUtf8().constData()));
                createMediumEnumerationTask(m_media.value(uMediumId));
                result << uMediumId;
            }
        }
    }
}

void UIMediumEnumerator::enumerateAllMediaOfMachineWithId(const QUuid &uMachineId, QList<QUuid> &result)
{
    /* For each the cached UIMedium we have: */
    foreach (const QUuid &uMediumId, mediumIDsSub())
    {
        /* Check if medium isn't NULL, used by our
         * machine and wasn't already enumerated. */
        const UIMedium guiMedium = mediumSub(uMediumId);
        if (   !guiMedium.isNull()
            && guiMedium.machineIds().contains(uMachineId)
            && !result.contains(uMediumId))
        {
            /* Enumerate corresponding UIMedium: */
            //printf(" Medium to recache: %s\n",
            //       uMediumId.toString().toUtf8().constData());
            LogRel2(("GUI: UIMediumEnumerator:  Medium {%s} of machine {%s} will be enumerated..\n",
                     uMediumId.toString().toUtf8().constData(),
                     uMachineId.toString().toUtf8().constData()));
            createMediumEnumerationTask(guiMedium);
            result << uMediumId;
        }
    }
}

void UIMediumEnumerator::enumerateAllMediaOfMediumWithId(const QUuid &uParentMediumId, QList<QUuid> &result)
{
    /* For each the cached UIMedium we have: */
    foreach (const QUuid &uMediumId, mediumIDsSub())
    {
        /* Check if medium isn't NULL, and is
         * a child of specified parent medium. */
        const UIMedium guiMedium = mediumSub(uMediumId);
        if (   !guiMedium.isNull()
            && guiMedium.parentID() == uParentMediumId)
        {
            /* Enumerate corresponding UIMedium: */
            //printf(" Medium to recache: %s\n",
            //       uMediumId.toString().toUtf8().constData());
            LogRel2(("GUI: UIMediumEnumerator:  Medium {%s} a child of medium {%s} will be enumerated..\n",
                     uMediumId.toString().toUtf8().constData(),
                     uParentMediumId.toString().toUtf8().constData()));
            createMediumEnumerationTask(guiMedium);
            result << uMediumId;
        }
    }
}


#include "UIMediumEnumerator.moc"
