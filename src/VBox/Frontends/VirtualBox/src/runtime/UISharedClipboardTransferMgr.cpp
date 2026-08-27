/* $Id: UISharedClipboardTransferMgr.cpp 115134 2026-08-27 15:09:45Z andreas.loeffler@oracle.com $ */
/** @file
 * VBox Qt GUI - UISharedClipboardTransfer and UISharedClipboardTransferMgr class implementations.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
#include <QCoreApplication>

/* GUI includes: */
#include "UISharedClipboardTransferMgr.h"

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS

/* COM includes: */
#include <VBox/com/VirtualBox.h>

#include "CClipboardTransferEvent.h"
#include "CEvent.h"

/* Other VBox includes: */
#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_GUI
#include <VBox/log.h>


/** Returns a printable transfer direction name.
  * @returns Transfer direction name.
  * @param  enmDirection  Brings the direction to name. */
static const char *shclGuiTransferDirectionName(KClipboardTransferDirection enmDirection)
{
    switch (enmDirection)
    {
        case KClipboardTransferDirection_Any:
            return "Any";
        case KClipboardTransferDirection_ToGuest:
            return "ToGuest";
        case KClipboardTransferDirection_ToHost:
            return "ToHost";
        default:
            break;
    }
    return "Unknown";
}


/** Returns a printable clipboard source name.
  * @returns Clipboard source name.
  * @param  enmSource  Brings the source to name. */
static const char *shclGuiTransferSourceName(KClipboardSource enmSource)
{
    switch (enmSource)
    {
        case KClipboardSource_Host:
            return "Host";
        case KClipboardSource_Guest:
            return "Guest";
        case KClipboardSource_Remote:
            return "Remote";
        case KClipboardSource_Custom:
            return "Custom";
        default:
            break;
    }
    return "Unknown";
}


/** Returns a printable transfer state name.
  * @returns Transfer state name.
  * @param  enmState  Brings the state to name. */
static const char *shclGuiTransferStateName(KClipboardTransferState enmState)
{
    switch (enmState)
    {
        case KClipboardTransferState_Added:
            return "Added";
        case KClipboardTransferState_Removed:
            return "Removed";
        case KClipboardTransferState_InProgress:
            return "InProgress";
        case KClipboardTransferState_Interaction:
            return "Interaction";
        case KClipboardTransferState_Completed:
            return "Completed";
        case KClipboardTransferState_Canceled:
            return "Canceled";
        case KClipboardTransferState_Failed:
            return "Failed";
        default:
            break;
    }
    return "Unknown";
}


/** Returns a printable transfer interaction name.
  * @returns Transfer interaction name.
  * @param  enmInteraction  Brings the interaction to name. */
static const char *shclGuiTransferInteractionName(KClipboardTransferInteraction enmInteraction)
{
    switch (enmInteraction)
    {
        case KClipboardTransferInteraction_None:
            return "None";
        case KClipboardTransferInteraction_Approval:
            return "Approval";
        case KClipboardTransferInteraction_Destination:
            return "Destination";
        case KClipboardTransferInteraction_Overwrite:
            return "Overwrite";
        case KClipboardTransferInteraction_Rename:
            return "Rename";
        case KClipboardTransferInteraction_ErrorRecovery:
            return "ErrorRecovery";
        default:
            break;
    }
    return "Unknown";
}


UISharedClipboardTransfer::UISharedClipboardTransfer()
    : m_uId(0)
    , m_enmDirection(KClipboardTransferDirection_Any)
    , m_enmSource(KClipboardSource_Custom)
    , m_enmState(KClipboardTransferState_Added)
    , m_fProgressNotificationCreated(false)
{
}


UISharedClipboardTransfer::UISharedClipboardTransfer(const CClipboardTransfer &comTransfer)
    : m_comTransfer(comTransfer)
    , m_uId(0)
    , m_enmDirection(KClipboardTransferDirection_Any)
    , m_enmSource(KClipboardSource_Custom)
    , m_enmState(KClipboardTransferState_Added)
    , m_fProgressNotificationCreated(false)
{
    refreshFromTransfer();
}


ULONG UISharedClipboardTransfer::id() const
{
    return m_uId;
}


bool UISharedClipboardTransfer::isValid() const
{
    return m_comTransfer.isNotNull();
}


bool UISharedClipboardTransfer::isTerminal() const
{
    return    m_enmState == KClipboardTransferState_Removed
           || m_enmState == KClipboardTransferState_Completed
           || m_enmState == KClipboardTransferState_Canceled
           || m_enmState == KClipboardTransferState_Failed;
}


bool UISharedClipboardTransfer::claimProgressNotification()
{
    if (m_fProgressNotificationCreated)
        return false;

    m_fProgressNotificationCreated = true;
    return true;
}


bool UISharedClipboardTransfer::hasProgressNotification() const
{
    return m_fProgressNotificationCreated;
}


void UISharedClipboardTransfer::handleStateChange(CClipboardTransferManager &comTransferManager,
                                                  KClipboardTransferState enmState,
                                                  KClipboardTransferInteraction enmInteraction,
                                                  const QString &strPath,
                                                  const QString &strMessage,
                                                  KClipboardError enmError)
{
    m_enmState = enmState;

    LogRel2(("GUI: UISharedClipboardProvider: transfer %u %s, direction=%s, source=%s\n",
             (unsigned)m_uId,
             shclGuiTransferStateName(enmState),
             shclGuiTransferDirectionName(m_enmDirection),
             shclGuiTransferSourceName(m_enmSource)));

    if (!strMessage.isEmpty())
        LogRel2(("GUI: UISharedClipboardProvider: transfer %u message: %s\n",
                 (unsigned)m_uId, strMessage.toUtf8().constData()));
    if (enmError != KClipboardError_None)
        LogRel(("GUI: UISharedClipboardProvider: transfer %u error %d\n",
                (unsigned)m_uId, enmError));

    if (enmState == KClipboardTransferState_Interaction)
        handleInteraction(comTransferManager, enmInteraction, strPath, strMessage);
}


void UISharedClipboardTransfer::refreshFromTransfer()
{
    if (m_comTransfer.isNull())
        return;

    const ULONG uId = m_comTransfer.GetId();
    const KClipboardTransferDirection enmDirection = m_comTransfer.GetDirection();
    const KClipboardSource enmSource = m_comTransfer.GetSource();
    const KClipboardTransferState enmState = m_comTransfer.GetState();
    if (!m_comTransfer.isOk())
    {
        LogRel2(("GUI: UISharedClipboardProvider: failed to inspect clipboard transfer: %Rhrc\n",
                 m_comTransfer.lastRC()));
        return;
    }

    m_uId = uId;
    m_enmDirection = enmDirection;
    m_enmSource = enmSource;
    m_enmState = enmState;
}


void UISharedClipboardTransfer::handleInteraction(CClipboardTransferManager &comTransferManager,
                                                  KClipboardTransferInteraction enmInteraction,
                                                  const QString &strPath,
                                                  const QString &strMessage)
{
    if (enmInteraction == KClipboardTransferInteraction_None)
        return;

    if (enmInteraction == KClipboardTransferInteraction_Approval)
    {
        LogRel2(("GUI: UISharedClipboardProvider: approving transfer %u\n", (unsigned)m_uId));
        comTransferManager.Approve(m_comTransfer, 0 /* flags */);
        if (!comTransferManager.isOk())
            LogRel(("GUI: UISharedClipboardProvider: approving transfer %u failed: %Rhrc\n",
                    (unsigned)m_uId, comTransferManager.lastRC()));
        return;
    }

    LogRel(("GUI: UISharedClipboardProvider: unsupported transfer %u interaction %s; responding with Cancel\n",
            (unsigned)m_uId, shclGuiTransferInteractionName(enmInteraction)));
    if (!strPath.isEmpty())
        LogRel2(("GUI: UISharedClipboardProvider: transfer %u interaction path: %s\n",
                 (unsigned)m_uId, strPath.toUtf8().constData()));
    if (!strMessage.isEmpty())
        LogRel2(("GUI: UISharedClipboardProvider: transfer %u interaction message: %s\n",
                 (unsigned)m_uId, strMessage.toUtf8().constData()));

    comTransferManager.Respond(m_comTransfer,
                               enmInteraction,
                               strPath,
                               KClipboardTransferResponse_Cancel,
                               QString() /* response path */,
                               0 /* flags */);
    if (!comTransferManager.isOk())
        LogRel(("GUI: UISharedClipboardProvider: responding to transfer %u interaction %s failed: %Rhrc\n",
                (unsigned)m_uId,
                shclGuiTransferInteractionName(enmInteraction),
                comTransferManager.lastRC()));
}


UISharedClipboardTransferMgr::UISharedClipboardTransferMgr()
{
}


UISharedClipboardTransferMgr::UISharedClipboardTransferMgr(const CClipboardTransferManager &comTransferManager)
    : m_comTransferManager(comTransferManager)
{
}


void UISharedClipboardTransferMgr::setManager(const CClipboardTransferManager &comTransferManager)
{
    m_comTransferManager = comTransferManager;
}


void UISharedClipboardTransferMgr::detach()
{
    m_transfers.clear();
    m_comTransferManager.detach();
}


bool UISharedClipboardTransferMgr::isAvailable() const
{
    return m_comTransferManager.isNotNull();
}


bool UISharedClipboardTransferMgr::handleEvent(const CEvent &comEvent,
                                               CClipboardTransfer *pcomNotifyTransfer /* = 0 */,
                                               QString *pstrError /* = 0 */)
{
    if (pcomNotifyTransfer)
        pcomNotifyTransfer->detach();
    if (pstrError)
        pstrError->clear();

    if (!isAvailable())
        return false;

    CClipboardTransferEvent comTransferEvent(comEvent);
    if (comTransferEvent.isNull())
        return false;

    CClipboardTransfer comTransfer = comTransferEvent.GetTransfer();
    const KClipboardTransferState enmState = comTransferEvent.GetState();
    const KClipboardTransferInteraction enmInteraction = comTransferEvent.GetInteraction();
    const QString strPath = comTransferEvent.GetPath();
    const QString strMessage = comTransferEvent.GetMessage();
    const KClipboardError enmError = comTransferEvent.GetError();
    if (!comTransferEvent.isOk() || comTransfer.isNull())
    {
        LogRel(("GUI: UISharedClipboardProvider: failed to inspect clipboard transfer event: %Rhrc\n",
                comTransferEvent.lastRC()));
        return false;
    }

    const ULONG uId = comTransfer.GetId();
    if (!comTransfer.isOk())
    {
        LogRel(("GUI: UISharedClipboardProvider: failed to inspect clipboard transfer id: %Rhrc\n",
                comTransfer.lastRC()));
        return false;
    }

    UISharedClipboardTransfer *pTransfer = findOrCreateTransfer(comTransfer, enmState);
    if (!pTransfer)
        return false;

    pTransfer->handleStateChange(m_comTransferManager, enmState, enmInteraction, strPath, strMessage, enmError);

    /* An Added event only means that the delayed-rendering offer was created.
     * Show progress once Main reports that the transfer has actually started,
     * or reuse its completed progress if it failed before reaching that state. */
    const bool fNotifyProgress =    (   enmState == KClipboardTransferState_InProgress
                                     || enmState == KClipboardTransferState_Failed)
                                 && pcomNotifyTransfer
                                 && comTransfer.isNotNull()
                                 && pTransfer->claimProgressNotification();
    if (fNotifyProgress)
        *pcomNotifyTransfer = comTransfer;

    /* Once a progress notification exists, its IProgress object owns the terminal
     * error presentation.  Emitting a second generic error would duplicate it. */
    if (   pstrError
        && !pTransfer->hasProgressNotification()
        && (   enmState == KClipboardTransferState_Failed
            || enmError != KClipboardError_None))
    {
        if (!strMessage.isEmpty())
            *pstrError = strMessage;
        else
            *pstrError = QCoreApplication::translate("UISharedClipboardProvider", "A shared clipboard transfer failed.");
    }

    if (pTransfer->isTerminal())
        m_transfers.remove(uId);

    return fNotifyProgress;
}


void UISharedClipboardTransferMgr::updateHostTransferPublication()
{
    /* Intentionally empty: the generated GUI wrapper currently lacks the
     * transfer creation helpers needed to publish host-to-guest file lists. */
}


UISharedClipboardTransfer *UISharedClipboardTransferMgr::findOrCreateTransfer(const CClipboardTransfer &comTransfer,
                                                                              KClipboardTransferState enmState)
{
    const ULONG uId = comTransfer.GetId();
    if (!comTransfer.isOk())
    {
        LogRel(("GUI: UISharedClipboardProvider: failed to inspect clipboard transfer id: %Rhrc\n",
                comTransfer.lastRC()));
        return 0;
    }

    QMap<ULONG, UISharedClipboardTransfer>::iterator it = m_transfers.find(uId);
    if (it != m_transfers.end())
        return &it.value();

    if (enmState == KClipboardTransferState_Removed)
    {
        LogRel2(("GUI: UISharedClipboardProvider: transfer %u removed before it was tracked\n",
                 (unsigned)uId));
        return 0;
    }

    it = m_transfers.insert(uId, UISharedClipboardTransfer(comTransfer));
    LogRel2(("GUI: UISharedClipboardProvider: tracking transfer %u\n", (unsigned)uId));
    return &it.value();
}

#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
