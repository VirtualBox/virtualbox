/* $Id: UISharedClipboardTransferMgr.h 115102 2026-08-21 11:14:19Z andreas.loeffler@oracle.com $ */
/** @file
 * VBox Qt GUI - UISharedClipboardTransfer and UISharedClipboardTransferMgr class declarations.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UISharedClipboardTransferMgr_h
#define FEQT_INCLUDED_SRC_runtime_UISharedClipboardTransferMgr_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS

/* Qt includes: */
#include <QMap>
#include <QString>

/* COM includes: */
#include "CClipboardTransfer.h"
#include "CClipboardTransferManager.h"

/* Forward declarations: */
class CEvent;

/** Lightweight frontend-side shared clipboard transfer tracker. */
class UISharedClipboardTransfer
{
public:

    /** Constructs an empty shared clipboard transfer tracker. */
    UISharedClipboardTransfer();
    /** Constructs a shared clipboard transfer tracker.
      * @param  comTransfer  Brings the COM transfer object to track. */
    UISharedClipboardTransfer(const CClipboardTransfer &comTransfer);

    /** Returns the transfer id.
      * @returns Transfer id. */
    ULONG id() const;
    /** Returns whether this tracker contains a live COM transfer.
      * @returns @c true if valid, @c false otherwise. */
    bool isValid() const;
    /** Returns whether the cached transfer state is terminal.
      * @returns @c true if terminal, @c false otherwise. */
    bool isTerminal() const;
    /** Marks this transfer as having a progress notification.
      * @returns @c true if the notification was newly marked, @c false if it was already marked. */
    bool claimProgressNotification();
    /** Returns whether a progress notification has been created for this transfer.
      * @returns @c true if created, @c false otherwise. */
    bool hasProgressNotification() const;

    /** Handles a transfer state change.
      * @param  comTransferManager  Brings the transfer manager to use for responses.
      * @param  enmState            Brings the new transfer state.
      * @param  enmInteraction      Brings the requested interaction, if any.
      * @param  strPath             Brings the path associated with the event, if any.
      * @param  strMessage          Brings the event message, if any.
      * @param  enmError            Brings the event error, if any. */
    void handleStateChange(CClipboardTransferManager &comTransferManager,
                           KClipboardTransferState enmState,
                           KClipboardTransferInteraction enmInteraction,
                           const QString &strPath,
                           const QString &strMessage,
                           KClipboardError enmError);

private:

    /** Refreshes cached immutable transfer attributes from the COM object. */
    void refreshFromTransfer();
    /** Handles a transfer interaction request.
      * @param  comTransferManager  Brings the transfer manager to use for responses.
      * @param  enmInteraction      Brings the requested interaction.
      * @param  strPath             Brings the path associated with the interaction, if any.
      * @param  strMessage          Brings the event message, if any. */
    void handleInteraction(CClipboardTransferManager &comTransferManager,
                           KClipboardTransferInteraction enmInteraction,
                           const QString &strPath,
                           const QString &strMessage);

    /** Holds the COM transfer object. */
    CClipboardTransfer m_comTransfer;
    /** Holds the cached transfer id. */
    ULONG m_uId;
    /** Holds the cached transfer direction. */
    KClipboardTransferDirection m_enmDirection;
    /** Holds the cached transfer source. */
    KClipboardSource m_enmSource;
    /** Holds the cached transfer state. */
    KClipboardTransferState m_enmState;
    /** Holds whether a progress notification has been created for this transfer. */
    bool m_fProgressNotificationCreated;
};

/** Shared clipboard transfer event manager for the GUI provider worker thread. */
class UISharedClipboardTransferMgr
{
public:

    /** Constructs an empty shared clipboard transfer manager. */
    UISharedClipboardTransferMgr();
    /** Constructs a shared clipboard transfer manager.
      * @param  comTransferManager  Brings the COM transfer manager to use. */
    UISharedClipboardTransferMgr(const CClipboardTransferManager &comTransferManager);

    /** Assigns the COM transfer manager.
      * @param  comTransferManager  Brings the COM transfer manager to use. */
    void setManager(const CClipboardTransferManager &comTransferManager);
    /** Detaches from COM and forgets all tracked transfers. */
    void detach();
    /** Returns whether a usable transfer manager is available.
      * @returns @c true if available, @c false otherwise. */
    bool isAvailable() const;

    /** Handles a clipboard transfer event.
      * @returns @c true if a progress notification should be shown, @c false otherwise.
      * @param  comEvent              Brings the raw clipboard transfer event.
      * @param  pcomNotifyTransfer   Where to return the transfer needing notification, optional.
      * @param  pstrError            Where to return an error message suitable for notification, optional. */
    bool handleEvent(const CEvent &comEvent,
                     CClipboardTransfer *pcomNotifyTransfer = 0,
                     QString *pstrError = 0);

    /** Extension point for host-to-guest transfer publication once the generated
      * transfer wrappers expose CreateTransfer/SetSourcePaths-style helpers. */
    void updateHostTransferPublication();

private:

    /** Finds or creates a transfer tracker.
      * @returns Transfer tracker, or @c 0 if the event should not be tracked.
      * @param  comTransfer  Brings the COM transfer object.
      * @param  enmState     Brings the current transfer state. */
    UISharedClipboardTransfer *findOrCreateTransfer(const CClipboardTransfer &comTransfer,
                                                    KClipboardTransferState enmState);

    /** Holds the COM transfer manager. */
    CClipboardTransferManager m_comTransferManager;
    /** Holds the tracked transfers by id. */
    QMap<ULONG, UISharedClipboardTransfer> m_transfers;
};

#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

#endif /* !FEQT_INCLUDED_SRC_runtime_UISharedClipboardTransferMgr_h */
