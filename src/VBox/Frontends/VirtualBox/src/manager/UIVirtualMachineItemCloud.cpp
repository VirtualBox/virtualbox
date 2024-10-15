/* $Id: UIVirtualMachineItemCloud.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualMachineItemCloud class implementation.
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
#include <QPointer>
#include <QTimer>

/* GUI includes: */
#include "UICloudNetworkingStuff.h"
#include "UIConverter.h"
#include "UIErrorString.h"
#include "UIIconPool.h"
#include "UINotificationCenter.h"
#include "UIProgressTask.h"
#include "UITranslationEventListener.h"
#include "UIThreadPool.h"
#include "UIVirtualMachineItemCloud.h"

/* COM includes: */
#include "CProgress.h"
#include "CVirtualBoxErrorInfo.h"
#include <VBox/com/VirtualBox.h> /* For GUEST_OS_ID_STR_X86. */


/** UIProgressTask extension performing cloud machine refresh task.
  * @todo rework this task to be a part of notification-center. */
class UIProgressTaskRefreshCloudMachine : public UIProgressTask
{
    Q_OBJECT;

public:

    /** Constructs @a comCloudMachine refresh task passing @a pParent to the base-class. */
    UIProgressTaskRefreshCloudMachine(QObject *pParent, const CCloudMachine &comCloudMachine);

protected:

    /** Creates and returns started progress-wrapper required to init UIProgressObject. */
    virtual CProgress createProgress() RT_OVERRIDE;
    /** Handles finished @a comProgress wrapper. */
    virtual void handleProgressFinished(CProgress &comProgress) RT_OVERRIDE;

private:

    /** Holds the cloud machine wrapper. */
    CCloudMachine  m_comCloudMachine;
};


/*********************************************************************************************************************************
*   Class UIProgressTaskRefreshCloudMachine implementation.                                                                      *
*********************************************************************************************************************************/

UIProgressTaskRefreshCloudMachine::UIProgressTaskRefreshCloudMachine(QObject *pParent, const CCloudMachine &comCloudMachine)
    : UIProgressTask(pParent)
    , m_comCloudMachine(comCloudMachine)
{
}

CProgress UIProgressTaskRefreshCloudMachine::createProgress()
{
    /* Prepare resulting progress-wrapper: */
    CProgress comResult;

    /* Initialize actual progress-wrapper: */
    CProgress comProgress = m_comCloudMachine.Refresh();
    if (!m_comCloudMachine.isOk())
        UINotificationMessage::cannotRefreshCloudMachine(m_comCloudMachine);
    else
        comResult = comProgress;

    /* Return progress-wrapper in any case: */
    return comResult;
}

void UIProgressTaskRefreshCloudMachine::handleProgressFinished(CProgress &comProgress)
{
    /* Handle progress-wrapper errors: */
    if (comProgress.isNotNull() && !comProgress.GetCanceled() && (!comProgress.isOk() || comProgress.GetResultCode() != 0))
        UINotificationMessage::cannotRefreshCloudMachine(comProgress);
}


/*********************************************************************************************************************************
*   Class UIVirtualMachineItemCloud implementation.                                                                              *
*********************************************************************************************************************************/

UIVirtualMachineItemCloud::UIVirtualMachineItemCloud(UIFakeCloudVirtualMachineItemState enmState)
    : UIVirtualMachineItem(UIVirtualMachineItemType_CloudFake)
    , m_enmMachineState(KCloudMachineState_Invalid)
    , m_enmFakeCloudItemState(enmState)
    , m_fUpdateRequiredByGlobalReason(false)
    , m_fUpdateRequiredByLocalReason(false)
    , m_pProgressTaskRefresh(0)
{
    prepare();
}

UIVirtualMachineItemCloud::UIVirtualMachineItemCloud(const CCloudMachine &comCloudMachine)
    : UIVirtualMachineItem(UIVirtualMachineItemType_CloudReal)
    , m_comCloudMachine(comCloudMachine)
    , m_enmMachineState(KCloudMachineState_Invalid)
    , m_enmFakeCloudItemState(UIFakeCloudVirtualMachineItemState_NotApplicable)
    , m_fUpdateRequiredByGlobalReason(false)
    , m_fUpdateRequiredByLocalReason(false)
    , m_pProgressTaskRefresh(0)
{
    prepare();
}

UIVirtualMachineItemCloud::~UIVirtualMachineItemCloud()
{
    cleanup();
}

void UIVirtualMachineItemCloud::setFakeCloudItemState(UIFakeCloudVirtualMachineItemState enmState)
{
    m_enmFakeCloudItemState = enmState;
    recache();
}

void UIVirtualMachineItemCloud::setFakeCloudItemErrorMessage(const QString &strErrorMessage)
{
    m_strFakeCloudItemErrorMessage = strErrorMessage;
    recache();
}

void UIVirtualMachineItemCloud::setUpdateRequiredByGlobalReason(bool fRequired)
{
    m_fUpdateRequiredByGlobalReason = fRequired;
}

void UIVirtualMachineItemCloud::setUpdateRequiredByLocalReason(bool fRequired)
{
    m_fUpdateRequiredByLocalReason = fRequired;
}

void UIVirtualMachineItemCloud::updateInfoAsync(bool fDelayed)
{
    /* Ignore refresh request if progress-task is absent: */
    if (!m_pProgressTaskRefresh)
        return;

    /* Schedule refresh request in a 10 or 0 seconds
     * if progress-task isn't already scheduled or running: */
    if (   !m_pProgressTaskRefresh->isScheduled()
        && !m_pProgressTaskRefresh->isRunning())
        m_pProgressTaskRefresh->schedule(fDelayed ? 10000 : 0);
}

void UIVirtualMachineItemCloud::waitForAsyncInfoUpdateFinished()
{
    /* Ignore cancel request if progress-task is absent: */
    if (!m_pProgressTaskRefresh)
        return;

    /* Mark update canceled in any case: */
    m_fUpdateRequiredByGlobalReason = false;
    m_fUpdateRequiredByLocalReason = false;

    /* Cancel refresh request
     * if progress-task already running: */
    if (m_pProgressTaskRefresh->isRunning())
        m_pProgressTaskRefresh->cancel();
}

void UIVirtualMachineItemCloud::recache()
{
    switch (itemType())
    {
        case UIVirtualMachineItemType_CloudFake:
        {
            /* Make sure cloud VM is NOT set: */
            AssertReturnVoid(m_comCloudMachine.isNull());

            /* Determine ID/name: */
            m_uId = QUuid();
            m_strName = QString();

            /* Determine whether VM is accessible: */
            m_fAccessible = m_strFakeCloudItemErrorMessage.isNull();
            m_strAccessError = m_strFakeCloudItemErrorMessage;

            /* Determine VM OS type: */
            m_strOSTypeId = GUEST_OS_ID_STR_X86("Other");

            /* Determine VM states: */
            m_enmMachineState = KCloudMachineState_Stopped;
            switch (m_enmFakeCloudItemState)
            {
                case UIFakeCloudVirtualMachineItemState_Loading:
                    m_machineStateIcon = UIIconPool::iconSet(":/state_loading_16px.png");
                    break;
                case UIFakeCloudVirtualMachineItemState_Done:
                    m_machineStateIcon = UIIconPool::iconSet(":/vm_new_16px.png");
                    break;
                default:
                    break;
            }

            /* Determine configuration access level: */
            m_enmConfigurationAccessLevel = ConfigurationAccessLevel_Null;

            /* Determine whether we should show this VM details: */
            m_fHasDetails = true;

            break;
        }
        case UIVirtualMachineItemType_CloudReal:
        {
            /* Make sure cloud VM is set: */
            AssertReturnVoid(m_comCloudMachine.isNotNull());

            /* Determine ID/name: */
            m_uId = m_comCloudMachine.GetId();
            m_strName = m_comCloudMachine.GetName();

            /* Determine whether VM is accessible: */
            m_fAccessible = m_comCloudMachine.GetAccessible();
            m_strAccessError = !m_fAccessible ? UIErrorString::formatErrorInfo(m_comCloudMachine.GetAccessError()) : QString();

            /* Determine VM OS type: */
            m_strOSTypeId = m_fAccessible ? m_comCloudMachine.GetOSTypeId() : GUEST_OS_ID_STR_X86("Other");

            /* Determine VM states: */
            m_enmMachineState = m_fAccessible ? m_comCloudMachine.GetState() : KCloudMachineState_Stopped;
            m_machineStateIcon = gpConverter->toIcon(m_enmMachineState);

            /* Determine configuration access level: */
            m_enmConfigurationAccessLevel = m_fAccessible ? ConfigurationAccessLevel_Full : ConfigurationAccessLevel_Null;

            /* Determine whether we should show this VM details: */
            m_fHasDetails = true;

            break;
        }
        default:
        {
            AssertFailed();
            break;
        }
    }

    /* Recache item pixmap: */
    recachePixmap();

    /* Retranslate finally: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVirtualMachineItemCloud::sltRetranslateUI);
}

void UIVirtualMachineItemCloud::recachePixmap()
{
    /* We are using icon corresponding to cached guest OS type: */
    if (   itemType() == UIVirtualMachineItemType_CloudFake
        && fakeCloudItemState() == UIFakeCloudVirtualMachineItemState_Loading)
        m_pixmap = generalIconPool().guestOSTypePixmapDefault("Cloud", &m_logicalPixmapSize);
    else
        m_pixmap = generalIconPool().guestOSTypePixmapDefault(m_strOSTypeId, &m_logicalPixmapSize);
}

bool UIVirtualMachineItemCloud::isItemEditable() const
{
    return    accessible()
           && itemType() == UIVirtualMachineItemType_CloudReal;
}

bool UIVirtualMachineItemCloud::isItemRemovable() const
{
    return    accessible()
           && itemType() == UIVirtualMachineItemType_CloudReal;
}

bool UIVirtualMachineItemCloud::isItemSaved() const
{
    return    accessible()
           && itemType() == UIVirtualMachineItemType_CloudReal
           && (   machineState() == KCloudMachineState_Stopped
               || machineState() == KCloudMachineState_Running);
}

bool UIVirtualMachineItemCloud::isItemPoweredOff() const
{
    return    accessible()
           && (   machineState() == KCloudMachineState_Stopped
               || machineState() == KCloudMachineState_Terminated);
}

bool UIVirtualMachineItemCloud::isItemStarted() const
{
    return    isItemRunning()
           || isItemPaused();
}

bool UIVirtualMachineItemCloud::isItemRunning() const
{
    return    accessible()
           && machineState() == KCloudMachineState_Running;
}

bool UIVirtualMachineItemCloud::isItemRunningHeadless() const
{
    return isItemRunning();
}

bool UIVirtualMachineItemCloud::isItemPaused() const
{
    return false;
}

bool UIVirtualMachineItemCloud::isItemStuck() const
{
    return false;
}

bool UIVirtualMachineItemCloud::isItemCanBeSwitchedTo() const
{
    return false;
}

void UIVirtualMachineItemCloud::sltRetranslateUI()
{
    /* If machine is accessible: */
    if (accessible())
    {
        if (itemType() == UIVirtualMachineItemType_CloudFake)
        {
            /* Update fake machine state name: */
            switch (m_enmFakeCloudItemState)
            {
                case UIFakeCloudVirtualMachineItemState_Loading:
                    m_strMachineStateName = tr("Loading ...");
                    break;
                case UIFakeCloudVirtualMachineItemState_Done:
                    m_strMachineStateName = tr("Empty");
                    break;
                default:
                    break;
            }

            /* Update tool-tip: */
            m_strToolTipText = m_strMachineStateName;
        }
        else
        {
            /* Update real machine state name: */
            m_strMachineStateName = gpConverter->toString(m_enmMachineState);

            /* Update tool-tip: */
            m_strToolTipText = QString("<nobr><b>%1</b></nobr><br>"
                                       "<nobr>%2</nobr>")
                                       .arg(m_strName)
                                       .arg(gpConverter->toString(m_enmMachineState));
        }
    }
    /* Otherwise: */
    else
    {
        /* We have our own translation for Null states: */
        m_strMachineStateName = tr("Inaccessible", "VM");

        /* Update tool-tip: */
        m_strToolTipText = tr("<nobr><b>%1</b></nobr><br>"
                              "<nobr>Inaccessible</nobr>",
                              "Inaccessible VM tooltip (name)")
                              .arg(m_strName);
    }
}

void UIVirtualMachineItemCloud::sltHandleRefreshCloudMachineInfoDone()
{
    /* Recache: */
    recache();

    /* Notify listeners: */
    emit sigRefreshFinished();

    /* Refresh again if required: */
    if (   m_fUpdateRequiredByGlobalReason
        || m_fUpdateRequiredByLocalReason)
        updateInfoAsync(true /* delayed? */);
}

void UIVirtualMachineItemCloud::prepare()
{
    /* Prepare progress-task if necessary: */
    if (itemType() == UIVirtualMachineItemType_CloudReal)
    {
        m_pProgressTaskRefresh = new UIProgressTaskRefreshCloudMachine(this, machine());
        if (m_pProgressTaskRefresh)
        {
            connect(m_pProgressTaskRefresh, &UIProgressTaskRefreshCloudMachine::sigProgressStarted,
                    this, &UIVirtualMachineItemCloud::sigRefreshStarted);
            connect(m_pProgressTaskRefresh, &UIProgressTaskRefreshCloudMachine::sigProgressFinished,
                    this, &UIVirtualMachineItemCloud::sltHandleRefreshCloudMachineInfoDone);
        }
    }

    /* Recache finally: */
    recache();
}

void UIVirtualMachineItemCloud::cleanup()
{
    /* Cleanup progress-task: */
    delete m_pProgressTaskRefresh;
    m_pProgressTaskRefresh = 0;
}


#include "UIVirtualMachineItemCloud.moc"
