/* $Id: UIUpdateManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUpdateManager class implementation.
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
#include <QDir>
#include <QTimer>

/* GUI includes: */
#include "UIExecutionQueue.h"
#include "UIExtension.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UINotificationCenter.h"
#include "UIUpdateDefs.h"
#include "UIUpdateManager.h"
#include "UIVersion.h"
#ifdef VBOX_WITH_UPDATE_REQUEST
# include "UICommon.h"
#endif

/* COM includes: */
#include "CExtPack.h"
#include "CExtPackManager.h"


/** UIExecutionStep extension to check for the new VirtualBox version. */
class UIUpdateStepVirtualBox : public UIExecutionStep
{
    Q_OBJECT;

public:

    /** Constructs extension step.
      * @param  fForcedCall  Brings whether this customer has forced privelegies. */
    UIUpdateStepVirtualBox(bool fForcedCall);

    /** Executes the step. */
    virtual void exec() RT_OVERRIDE;

private:

    /** Holds whether this customer has forced privelegies. */
    bool  m_fForcedCall;

};


/** UIExecutionStep extension to check for the new VirtualBox Extension Pack version. */
class UIUpdateStepVirtualBoxExtensionPack : public UIExecutionStep
{
    Q_OBJECT;

public:

    /** Constructs extension step. */
    UIUpdateStepVirtualBoxExtensionPack();

    /** Executes the step. */
    virtual void exec() RT_OVERRIDE;

private slots:

    /** Handles downloaded Extension Pack.
      * @param  strSource  Brings the EP source.
      * @param  strTarget  Brings the EP target.
      * @param  strDigest  Brings the EP digest. */
    void sltHandleDownloadedExtensionPack(const QString &strSource,
                                          const QString &strTarget,
                                          const QString &strDigest);
};


/*********************************************************************************************************************************
*   Class UIUpdateStepVirtualBox implementation.                                                                                 *
*********************************************************************************************************************************/

UIUpdateStepVirtualBox::UIUpdateStepVirtualBox(bool fForcedCall)
    : m_fForcedCall(fForcedCall)
{
    Q_UNUSED(fForcedCall);
}

void UIUpdateStepVirtualBox::exec()
{
    /* Check for new VirtualBox version: */
    UINotificationProgressNewVersionChecker *pNotification =
        new UINotificationProgressNewVersionChecker(m_fForcedCall);
    connect(pNotification, &UINotificationProgressNewVersionChecker::sigProgressFinished,
            this, &UIUpdateStepVirtualBox::sigStepFinished);
    gpNotificationCenter->append(pNotification);
}


/*********************************************************************************************************************************
*   Class UIUpdateStepVirtualBoxExtensionPack implementation.                                                                    *
*********************************************************************************************************************************/

UIUpdateStepVirtualBoxExtensionPack::UIUpdateStepVirtualBoxExtensionPack()
{
}

void UIUpdateStepVirtualBoxExtensionPack::exec()
{
    /* Return if VirtualBox Manager issued a direct request to install EP: */
    if (gUpdateManager->isEPInstallationRequested())
    {
        emit sigStepFinished();
        return;
    }

    /* Return if already downloading: */
    if (UINotificationDownloaderExtensionPack::exists())
    {
        gpNotificationCenter->invoke();
        emit sigStepFinished();
        return;
    }

    /* Get extension pack manager: */
    CExtPackManager extPackManager = gpGlobalSession->virtualBox().GetExtensionPackManager();
    /* Return if extension pack manager is NOT available: */
    if (extPackManager.isNull())
    {
        emit sigStepFinished();
        return;
    }

    /* Get extension pack: */
    CExtPack extPack = extPackManager.Find(GUI_ExtPackName);
    /* Return if extension pack is NOT installed: */
    if (extPack.isNull())
    {
        emit sigStepFinished();
        return;
    }

    /* Get VirtualBox version: */
    UIVersion vboxVersion(UIVersionInfo::vboxVersionStringNormalized());
    /* Get extension pack version: */
    QString strExtPackVersion(extPack.GetVersion());

    /* If this version being developed: */
    if (vboxVersion.z() % 2 == 1)
    {
        /* If this version being developed on release branch (we use released one): */
        if (vboxVersion.z() < 97)
            vboxVersion.setZ(vboxVersion.z() - 1);
        /* If this version being developed on trunk (we skip check at all): */
        else
        {
            emit sigStepFinished();
            return;
        }
    }

    /* Get updated VirtualBox version: */
    const QString strVBoxVersion = vboxVersion.toString();

    /* Skip the check if the extension pack is equal to or newer than VBox. */
    if (UIVersion(strExtPackVersion) >= vboxVersion)
    {
        emit sigStepFinished();
        return;
    }

    QString strExtPackEdition(extPack.GetEdition());
    if (strExtPackEdition.contains("ENTERPRISE"))
    {
        /* Inform the user that he should update the extension pack: */
        UINotificationMessage::askUserToDownloadExtensionPack(GUI_ExtPackName, strExtPackVersion, strVBoxVersion);
        /* Never try to download for ENTERPRISE version: */
        emit sigStepFinished();
        return;
    }

    /* Ask the user about extension pack downloading: */
    if (!msgCenter().confirmLookingForExtensionPack(GUI_ExtPackName, strExtPackVersion))
    {
        emit sigStepFinished();
        return;
    }

    /* Download extension pack: */
    UINotificationDownloaderExtensionPack *pNotification = UINotificationDownloaderExtensionPack::instance(GUI_ExtPackName);
    /* After downloading finished => propose to install the Extension Pack: */
    connect(pNotification, &UINotificationDownloaderExtensionPack::sigExtensionPackDownloaded,
            this, &UIUpdateStepVirtualBoxExtensionPack::sltHandleDownloadedExtensionPack);
    /* Handle any signal as step-finished: */
    connect(pNotification, &UINotificationDownloaderExtensionPack::sigProgressFailed,
            this, &UIUpdateStepVirtualBoxExtensionPack::sigStepFinished);
    connect(pNotification, &UINotificationDownloaderExtensionPack::sigProgressCanceled,
            this, &UIUpdateStepVirtualBoxExtensionPack::sigStepFinished);
    connect(pNotification, &UINotificationDownloaderExtensionPack::sigProgressFinished,
            this, &UIUpdateStepVirtualBoxExtensionPack::sigStepFinished);
    /* Append and start notification: */
    gpNotificationCenter->append(pNotification);
}

void UIUpdateStepVirtualBoxExtensionPack::sltHandleDownloadedExtensionPack(const QString &strSource,
                                                                           const QString &strTarget,
                                                                           const QString &strDigest)
{
    /* Warn the user about extension pack was downloaded and saved, propose to install it: */
    if (msgCenter().proposeInstallExtentionPack(GUI_ExtPackName, strSource, QDir::toNativeSeparators(strTarget)))
        UIExtension::install(strTarget, strDigest, windowManager().mainWindowShown(), NULL);
    /* Propose to delete the downloaded extension pack: */
    if (msgCenter().proposeDeleteExtentionPack(QDir::toNativeSeparators(strTarget)))
    {
        /* Delete the downloaded extension pack: */
        QFile::remove(QDir::toNativeSeparators(strTarget));
        /* Get the list of old extension pack files in VirtualBox homefolder: */
        const QStringList oldExtPackFiles = QDir(gpGlobalSession->homeFolder()).entryList(QStringList("*.vbox-extpack"),
                                                                                    QDir::Files);
        /* Propose to delete old extension pack files if there are any: */
        if (oldExtPackFiles.size())
        {
            if (msgCenter().proposeDeleteOldExtentionPacks(oldExtPackFiles))
            {
                foreach (const QString &strExtPackFile, oldExtPackFiles)
                {
                    /* Delete the old extension pack file: */
                    QFile::remove(QDir::toNativeSeparators(QDir(gpGlobalSession->homeFolder()).filePath(strExtPackFile)));
                }
            }
        }
    }
}


/*********************************************************************************************************************************
*   Class UIUpdateManager implementation.                                                                                        *
*********************************************************************************************************************************/

/* static */
UIUpdateManager* UIUpdateManager::s_pInstance = 0;

UIUpdateManager::UIUpdateManager()
    : m_pQueue(new UIExecutionQueue(this))
    , m_fIsRunning(false)
    , m_uTime(1 /* day */ * 24 /* hours */ * 60 /* minutes */ * 60 /* seconds */ * 1000 /* ms */)
    , m_fEPInstallationRequested(false)
{
    /* Prepare instance: */
    if (s_pInstance != this)
        s_pInstance = this;

    /* Configure queue: */
    connect(m_pQueue, &UIExecutionQueue::sigQueueFinished, this, &UIUpdateManager::sltHandleUpdateFinishing);

#ifdef VBOX_WITH_UPDATE_REQUEST
    /* Ask updater to check for the first time, for Selector UI only: */
    if (gEDataManager->applicationUpdateEnabled() && uiCommon().uiType() == UIType_ManagerUI)
        QTimer::singleShot(0, this, SLOT(sltCheckIfUpdateIsNecessary()));
#endif /* VBOX_WITH_UPDATE_REQUEST */
}

UIUpdateManager::~UIUpdateManager()
{
    /* Cleanup instance: */
    if (s_pInstance == this)
        s_pInstance = 0;
}

/* static */
void UIUpdateManager::schedule()
{
    /* Ensure instance is NOT created: */
    if (s_pInstance)
        return;

    /* Create instance: */
    new UIUpdateManager;
}

/* static */
void UIUpdateManager::shutdown()
{
    /* Ensure instance is created: */
    if (!s_pInstance)
        return;

    /* Delete instance: */
    delete s_pInstance;
}

void UIUpdateManager::sltForceCheck()
{
    /* Force call for new version check: */
    sltCheckIfUpdateIsNecessary(true /* force call */);
}

void UIUpdateManager::sltCheckIfUpdateIsNecessary(bool fForcedCall /* = false */)
{
    /* If already running: */
    if (m_fIsRunning)
    {
        /* And we have a force-call: */
        if (fForcedCall)
            gpNotificationCenter->invoke();
        return;
    }

    /* Set as running: */
    m_fIsRunning = true;

    /* Load/decode curent update data: */
    VBoxUpdateData currentData;
    CHost comHost = gpGlobalSession->host();
    currentData.load(comHost);

    /* If update is really necessary: */
    if (
#ifdef VBOX_NEW_VERSION_TEST
        true ||
#endif
        fForcedCall || currentData.isCheckRequired())
    {
        /* Prepare update queue: */
        m_pQueue->enqueue(new UIUpdateStepVirtualBox(fForcedCall));
        m_pQueue->enqueue(new UIUpdateStepVirtualBoxExtensionPack);
        /* Start update queue: */
        m_pQueue->start();
    }
    else
        sltHandleUpdateFinishing();
}

void UIUpdateManager::sltHandleUpdateFinishing()
{
    /* Load/decode curent update data: */
    VBoxUpdateData currentData;
    CHost comHost = gpGlobalSession->host();
    currentData.load(comHost);
    /* Encode/save new update data: */
    VBoxUpdateData newData(currentData.isCheckEnabled(), currentData.updatePeriod(), currentData.updateChannel());
    newData.save(comHost);

#ifdef VBOX_WITH_UPDATE_REQUEST
    /* Ask updater to check for the next time: */
    QTimer::singleShot(m_uTime, this, SLOT(sltCheckIfUpdateIsNecessary()));
#endif /* VBOX_WITH_UPDATE_REQUEST */

    /* Set as not running: */
    m_fIsRunning = false;
}


#include "UIUpdateManager.moc"
