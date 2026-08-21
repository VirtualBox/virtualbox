/* $Id: VBoxSharedClipboardSvc.h 115102 2026-08-21 11:14:19Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - HGCM protocol state and data transfer interfaces.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_HostServices_VBoxSharedClipboardSvc_h
#define VBOX_INCLUDED_HostServices_VBoxSharedClipboardSvc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/asm.h>
#include <iprt/list.h>
#include <iprt/semaphore.h>

#include <VBox/hgcmsvc.h>
#include <VBox/log.h>

#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/SharedClipboard-transfers.h>


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
struct SHCLCLIENTSTATE;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/**
 * A queued message for the guest.
 */
typedef struct _SHCLCLIENTMSG
{
    /** The queue list entry. */
    RTLISTNODE          ListEntry;
    /** Stored message ID (VBOX_SHCL_HOST_MSG_XXX). */
    uint32_t            idMsg;
    /** Context ID. */
    uint64_t            idCtx;
    /** Number of stored parameters in aParms. */
    uint32_t            cParms;
    /** HGCM parameters. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    VBOXHGCMSVCPARM     aParms[RT_FLEXIBLE_ARRAY];
} SHCLCLIENTMSG;
/** Pointer to a queue message for the guest.   */
typedef SHCLCLIENTMSG *PSHCLCLIENTMSG;

typedef struct SHCLCLIENTTRANSFERSTATE
{
    /** Directory of the transfer to start. */
    SHCLTRANSFERDIR enmTransferDir;

    /** Shared Clipboard (file) transfer mode. */
    uint32_t        uTransferMode;
} SHCLCLIENTTRANSFERSTATE;

/**
 * Structure for holding a single POD (plain old data) transfer.
 *
 * This mostly is plain text, but also can be stuff like bitmap (BMP) or other binary data.
 */
typedef struct SHCLCLIENTPODSTATE
{
    /** POD transfer direction. */
    SHCLTRANSFERDIR         enmDir;
    /** Format of the data to be read / written. */
    SHCLFORMAT              uFormat;
    /** How much data (in bytes) to read/write for the current operation. */
    uint64_t                cbToReadWriteTotal;
    /** How much data (in bytes) already has been read/written for the current operation. */
    uint64_t                cbReadWritten;
    /** Timestamp (in ms) of Last read/write operation. */
    uint64_t                tsLastReadWrittenMs;
} SHCLCLIENTPODSTATE;

/** @name SHCLCLIENTSTATE_FLAGS_XXX
 * @note Part of saved state!
 * @{ */
/** No Shared Clipboard client flags defined. */
#define SHCLCLIENTSTATE_FLAGS_NONE              0
/** @} */

/**
 * Structure needed to support backwards compatbility for old(er) Guest Additions (< 6.1),
 * which did not know the context ID concept then.
 */
typedef struct SHCLCLIENTLEGACYCID
{
    /** List node. */
    RTLISTNODE Node;
    /** The actual context ID. */
    uint64_t   uCID;
    /** Not used yet; useful to have it in the saved state though. */
    uint32_t   enmType;
    /** @todo Add an union here as soon as we utilize \a enmType. */
    SHCLFORMAT uFormat;
} SHCLCLIENTLEGACYCID;
/** Pointer to a SHCLCLIENTLEGACYCID struct. */
typedef SHCLCLIENTLEGACYCID *PSHCLCLIENTLEGACYCID;

/**
 * Structure for keeping legacy state, required for keeping backwards compatibility
 * to old(er) Guest Additions.
 */
typedef struct SHCLCLIENTLEGACYSTATE
{
    /** List of context IDs (of type SHCLCLIENTLEGACYCID) for older Guest Additions which (< 6.1)
     *  which did not know the concept of context IDs. */
    RTLISTANCHOR lstCID;
    /** Number of context IDs currently in \a lstCID. */
    uint16_t     cCID;
} SHCLCLIENTLEGACYSTATE;

/**
 * Structure for keeping generic client state data within the Shared Clipboard host service.
 * This structure needs to be serializable by SSM (must be a POD type).
 */
typedef struct SHCLCLIENTSTATE
{
    /** The client's HGCM ID. Not related to the session ID below! */
    uint32_t                uClientID;
    /** The client's session ID. */
    SHCLSESSIONID           uSessionID;
    /** Guest feature flags, VBOX_SHCL_GF_0_XXX. */
    RT_ALIGNAS_MEMB(8) uint64_t
                            fGuestFeatures0;
    /** Guest feature flags, VBOX_SHCL_GF_1_XXX. */
    uint64_t                fGuestFeatures1;
    /** Chunk size to use for data transfers. */
    uint32_t                cbChunkSize;
    /** Where the transfer sources its data from. */
    SHCLSOURCE              enmSource;
    /** Client state flags of type SHCLCLIENTSTATE_FLAGS_. */
    uint32_t                fFlags;
    /** POD (plain old data) state. */
    SHCLCLIENTPODSTATE      POD;
    /** The client's transfers state. */
    SHCLCLIENTTRANSFERSTATE Transfers;
    /** The current Shared Clipboard operation mode. */
    uint32_t                uMode;
} SHCLCLIENTSTATE, *PSHCLCLIENTSTATE;

typedef struct _SHCLCLIENTCMDCTX
{
    uint64_t uContextID;
} SHCLCLIENTCMDCTX, *PSHCLCLIENTCMDCTX;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Structure for keeping transfer-related data per HGCM client.
 */
typedef struct _SHCLIENTTRANSFERS
{
    /** Transfer context. */
    SHCLTRANSFERCTX             Ctx;
    /** Whether a service-session reset currently blocks new transfer admission. */
    bool volatile               fResetting;
} SHCLIENTTRANSFERS;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/**
 * Structure for keeping data per (connected) HGCM client.
 */
typedef struct _SHCLCLIENT
{
    /** HGCM service helpers used to complete deferred guest calls. */
    PVBOXHGCMSVCHELPERS         pHelpers;
    /** General client state data. */
    SHCLCLIENTSTATE             State;
    /** The critical section protecting the queue, event source and whatnot.   */
    RTCRITSECT                  CritSect;
    /** The client's message queue (SHCLCLIENTMSG). */
    RTLISTANCHOR                MsgQueue;
    /** Number of allocated messages (updated atomically, not under critsect). */
    uint32_t volatile           cMsgAllocated;
    /** Legacy cruft we have to keep to support old(er) Guest Additions. */
    SHCLCLIENTLEGACYSTATE       Legacy;
    /** The client's own event source.
     *  Needed for events which are not bound to a specific transfer. */
    SHCLEVENTSOURCE             EventSrc;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    SHCLIENTTRANSFERS           Transfers;
#endif
    /** Structure for keeping the client's pending (deferred return) state.
     *  A client is in a deferred state when it asks for the next HGCM message,
     *  but the service can't provide it yet. That way a client will block (on the guest side, does not return)
     *  until the service can complete the call. */
    struct
    {
        /** The client's HGCM call handle. Needed for completing a deferred call. */
        VBOXHGCMCALLHANDLE      hHandle;
        /** Message type (function number) to use when completing the deferred call.
         *  A non-0 value means the client is in pending mode. */
        uint32_t                uType;
        /** Parameter count to use when completing the deferred call. */
        uint32_t                cParms;
        /** Parameters to use when completing the deferred call. */
        PVBOXHGCMSVCPARM        paParms;
    } Pending;
} SHCLCLIENT, *PSHCLCLIENT;

AssertCompileMemberAlignment(SHCLCLIENT, State.fGuestFeatures0, 8);
AssertCompileMemberAlignment(SHCLCLIENT, State.fGuestFeatures1, 8);

/**
 * Returns a client's cached Shared Clipboard mode atomically.
 *
 * @returns Clipboard mode, or @c VBOX_SHCL_MODE_OFF for an invalid client.
 * @param   pClient             Client to query.
 */
DECLINLINE(uint32_t) ShClSvcClientGetMode(PSHCLCLIENT pClient)
{
    AssertPtrReturn(pClient, VBOX_SHCL_MODE_OFF);
    return ASMAtomicReadU32(&pClient->State.uMode);
}

/**
 * Returns a client's first negotiated guest-feature word atomically.
 *
 * @returns Guest feature word, or zero for an invalid client.
 * @param   pClient             Client to query.
 */
DECLINLINE(uint64_t) ShClSvcClientGetGuestFeatures0(PSHCLCLIENT pClient)
{
    AssertPtrReturn(pClient, VBOX_SHCL_GF_NONE);
    return ASMAtomicReadU64(&pClient->State.fGuestFeatures0);
}

/**
 * Returns a client's second negotiated guest-feature word atomically.
 *
 * @returns Guest feature word, or zero for an invalid client.
 * @param   pClient             Client to query.
 */
DECLINLINE(uint64_t) ShClSvcClientGetGuestFeatures1(PSHCLCLIENT pClient)
{
    AssertPtrReturn(pClient, VBOX_SHCL_GF_NONE);
    return ASMAtomicReadU64(&pClient->State.fGuestFeatures1);
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Returns a client's cached Shared Clipboard file-transfer mode atomically.
 *
 * @returns File-transfer mode, or disabled for an invalid client.
 * @param   pClient             Client to query.
 */
DECLINLINE(uint32_t) ShClSvcClientGetTransferMode(PSHCLCLIENT pClient)
{
    AssertPtrReturn(pClient, VBOX_SHCL_TRANSFER_MODE_F_NONE);
    return ASMAtomicReadU32(&pClient->State.Transfers.uTransferMode);
}
#endif

/** @name Service client functions.
 * @{
 */
PSHCLCLIENTMSG ShClSvcClientMsgAlloc(PSHCLCLIENT pClient, uint32_t uMsg, uint32_t cParms);
void ShClSvcClientMsgFree(PSHCLCLIENT pClient, PSHCLCLIENTMSG pMsg);
void ShClSvcClientMsgAdd(PSHCLCLIENT pClient, PSHCLCLIENTMSG pMsg, bool fAppend);

int ShClSvcClientInit(PSHCLCLIENT pClient, uint32_t uClientID); /* For testcases. */

void ShClSvcClientLock(PSHCLCLIENT pClient);
void ShClSvcClientUnlock(PSHCLCLIENT pClient);

int ShClSvcClientWakeup(PSHCLCLIENT pClient);
int shClSvcClientMsgAddAndWakeupClient(PSHCLCLIENT pClient, PSHCLCLIENTMSG pMsg);

void shClSvcMsgSetPeekReturn(PSHCLCLIENTMSG pMsg, PVBOXHGCMSVCPARM paDstParms, uint32_t cDstParms);
int shClSvcMsgSetOldWaitReturn(PSHCLCLIENTMSG pMsg, PVBOXHGCMSVCPARM paDstParms, uint32_t cDstParms);

SHCLFORMATS shClSvcHandleFormats(bool fHostToGuest, PSHCLCLIENT pClient, SHCLFORMATS fFormats);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
bool shClSvcClientTransfersAreAllowed(PSHCLCLIENT pClient);
#endif
/** @} */

/** @name Service functions shared with Main's clipboard backends.
 * Locking is between the (host) service thread and the platform-dependent (window) thread.
 * @{
 */
uint32_t ShClSvcGetMode(void);
/** @} */

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** @name Shared Clipboard transfer interface implementations for guest -> host transfers.
 * @{
 */
DECLCALLBACK(int) ShClSvcTransferIfaceGHRootListRead(PSHCLTXPROVIDERCTX pCtx);
DECLCALLBACK(int) ShClSvcTransferIfaceGHListOpen(PSHCLTXPROVIDERCTX pCtx, PSHCLLISTOPENPARMS pOpenParms, PSHCLLISTHANDLE phList);
DECLCALLBACK(int) ShClSvcTransferIfaceGHListClose(PSHCLTXPROVIDERCTX pCtx, SHCLLISTHANDLE hList);
DECLCALLBACK(int) ShClSvcTransferIfaceGHListHdrRead(PSHCLTXPROVIDERCTX pCtx, SHCLLISTHANDLE hList, PSHCLLISTHDR pListHdr);
DECLCALLBACK(int) ShClSvcTransferIfaceGHListEntryRead(PSHCLTXPROVIDERCTX pCtx, SHCLLISTHANDLE hList, PSHCLLISTENTRY pListEntry);
DECLCALLBACK(int) ShClSvcTransferIfaceGHObjOpen(PSHCLTXPROVIDERCTX pCtx, PSHCLOBJOPENCREATEPARMS pCreateParms, PSHCLOBJHANDLE phObj);
DECLCALLBACK(int) ShClSvcTransferIfaceGHObjClose(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj);
DECLCALLBACK(int) ShClSvcTransferIfaceGHObjRead(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj, void *pvData, uint32_t cbData, uint32_t fFlags, uint32_t *pcbRead);
int ShClSvcTransferGHRootListReadEntry(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, uint64_t idxEntry,
                                       PSHCLLISTENTRY *ppListEntry);
int ShClSvcTransferGHRootListReadHdr(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, PSHCLLISTHDR pHdr);
int shClSvcTransferSendStatusAsync(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, SHCLTRANSFERSTATUS enmSts,
                                   int rcTransfer, PSHCLEVENT *ppEvent);
/** @} */
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/* Host unit testing interface */
#endif /* !VBOX_INCLUDED_HostServices_VBoxSharedClipboardSvc_h */
