/* $Id: VBoxSharedClipboardSvc-internal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard Service - Internal header.
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

#ifndef VBOX_INCLUDED_SRC_SharedClipboard_VBoxSharedClipboardSvc_internal_h
#define VBOX_INCLUDED_SRC_SharedClipboard_VBoxSharedClipboardSvc_internal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <algorithm>
#include <list>
#include <map>

#include <iprt/cpp/list.h> /* For RTCList. */
#include <iprt/list.h>
#include <iprt/semaphore.h>

#include <VBox/hgcmsvc.h>
#include <VBox/log.h>

#include <VBox/HostServices/Service.h>
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/SharedClipboard-transfers.h>

using namespace HGCM;

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
} SHCLCLIENTTRANSFERSTATE, *PSHCLCLIENTTRANSFERSTATE;

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
} SHCLCLIENTPODSTATE, *PSHCLCLIENTPODSTATE;

/** @name SHCLCLIENTSTATE_FLAGS_XXX
 * @note Part of saved state!
 * @{ */
/** No Shared Clipboard client flags defined. */
#define SHCLCLIENTSTATE_FLAGS_NONE              0
/** Client has a guest read operation active. Currently unused. */
#define SHCLCLIENTSTATE_FLAGS_READ_ACTIVE       RT_BIT(0)
/** Client has a guest write operation active. Currently unused. */
#define SHCLCLIENTSTATE_FLAGS_WRITE_ACTIVE      RT_BIT(1)
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
    struct SHCLCLIENTSTATE *pNext;
    struct SHCLCLIENTSTATE *pPrev;

    /** Backend-dependent opaque context structure.
     *  This contains data only known to a certain backend implementation.
     *  Optional and can be NULL. */
    SHCLCONTEXT            *pCtx;
    /** The client's HGCM ID. Not related to the session ID below! */
    uint32_t                uClientID;
    /** The client's session ID. */
    SHCLSESSIONID           uSessionID;
    /** Guest feature flags, VBOX_SHCL_GF_0_XXX. */
    uint64_t                fGuestFeatures0;
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
    /** Backends-specific transfers callbacks to use. */
    SHCLTRANSFERCALLBACKS       Callbacks;
    /** Backends-specific transfers provider to use. */
    SHCLTXPROVIDER              Provider;
} SHCLIENTTRANSFERS, *PSHCLIENTTRANSFERS;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/** Prototypes for the Shared Clipboard backend. */
struct SHCLBACKEND;
typedef SHCLBACKEND *PSHCLBACKEND;

/**
 * Structure for keeping data per (connected) HGCM client.
 */
typedef struct _SHCLCLIENT
{
    /** Pointer to associated backend, if any.
     *  Might be NULL if not being used. */
    PSHCLBACKEND                pBackend;
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

/**
 * Structure for keeping a single event source map entry.
 * Currently empty.
 */
typedef struct _SHCLEVENTSOURCEMAPENTRY
{
} SHCLEVENTSOURCEMAPENTRY;

/** Map holding information about connected HGCM clients. Key is the (unique) HGCM client ID.
 *  The value is a weak pointer to PSHCLCLIENT, which is owned by HGCM. */
typedef std::map<uint32_t, PSHCLCLIENT> ClipboardClientMap;

/** Map holding information about event sources. Key is the (unique) event source ID. */
typedef std::map<SHCLEVENTSOURCEID, SHCLEVENTSOURCEMAPENTRY> ClipboardEventSourceMap;

/** Simple queue (list) which holds deferred (waiting) clients. */
typedef std::list<uint32_t> ClipboardClientQueue;

/**
 * Structure for keeping the Shared Clipboard service extension state.
 *
 * A service extension is optional, and can be installed by a host component
 * to communicate with the Shared Clipboard host service.
 */
typedef struct _SHCLEXTSTATE
{
    /** Pointer to the actual service extension handle.
     *
     * Must return VERR_NOT_SUPPORTED if the extension didn't handle the requested function.
     * This will invoke the regular backend then.
     */
    PFNHGCMSVCEXT  pfnExtension;
    /** Opaque pointer to extension-provided data. Don't touch. */
    void          *pvExtension;
    /** The HGCM client ID currently assigned to this service extension.
     *  At the moment only one HGCM client can be assigned per extension. */
    uint32_t       uClientID;
    /** Whether the host service is reading clipboard data currently. */
    bool           fReadingData;
    /** Whether the service extension has sent the clipboard formats while
     *  the the host service is reading clipboard data from it. */
    bool           fDelayedAnnouncement;
    /** The actual clipboard formats announced while the host service
     *  is reading clipboard data from the extension. */
    uint32_t       fDelayedFormats;
} SHCLEXTSTATE, *PSHCLEXTSTATE;

extern SHCLEXTSTATE g_ExtState;

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
/** @} */

/** @name Service functions, accessible by the backends.
 * Locking is between the (host) service thread and the platform-dependent (window) thread.
 * @{
 */
int ShClSvcReadDataFromGuestAsync(PSHCLCLIENT pClient, SHCLFORMATS fFormats, PSHCLEVENT *ppEvent);
int ShClSvcReadDataFromGuest(PSHCLCLIENT pClient, SHCLFORMAT uFmt, void **ppv, uint32_t *pcb);
int ShClSvcGuestDataSignal(PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData);
int ShClSvcReportFormats(PSHCLCLIENT pClient, SHCLFORMATS fFormats);
PSHCLBACKEND ShClSvcGetBackend(void);
uint32_t ShClSvcGetMode(void);
bool ShClSvcGetHeadless(void);
bool ShClSvcLock(void);
void ShClSvcUnlock(void);
/** @} */

/** @name Platform-dependent implementations for the Shared Clipboard host service ("backends"),
 *        called *only* by the host service.
 * @{
 */
/**
 * Structure for keeping Shared Clipboard backend instance data.
 */
typedef struct SHCLBACKEND
{
    /** Callback table to use.
     *  Some callbacks might be optional and therefore NULL -- see the table for more details. */
    SHCLCALLBACKS Callbacks;
} SHCLBACKEND;
/** Pointer to a Shared Clipboard backend. */
typedef SHCLBACKEND *PSHCLBACKEND;

/**
 * Called on initialization.
 *
 * @param   pBackend    Shared Clipboard backend to initialize.
 * @param   pTable      The HGCM service call and parameter table.  Mainly for
 *                      adjusting the limits.
 */
int ShClBackendInit(PSHCLBACKEND pBackend, VBOXHGCMSVCFNTABLE *pTable);

/**
 * Called on destruction.
 *
 * @param   pBackend    Shared Clipboard backend to destroy.
 */
void ShClBackendDestroy(PSHCLBACKEND pBackend);

/**
 * Called when a new HGCM client connects.
 *
 * @param   pBackend            Shared Clipboard backend to set callbacks for.
 * @param   pCallbacks          Backend callbacks to use.
 *                              When NULL is specified, the backend's default callbacks are being used.
 */
void ShClBackendSetCallbacks(PSHCLBACKEND pBackend, PSHCLCALLBACKS pCallbacks);

/**
 * Called when a new HGCM client connects.
 *
 * @returns VBox status code.
 * @param   pBackend            Shared Clipboard backend to connect to.
 * @param   pClient             Shared Clipboard client context.
 * @param   fHeadless           Whether this is a headless connection or not.
 */
int ShClBackendConnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, bool fHeadless);

/**
 * Called when a HGCM client disconnects.
 *
 * @returns VBox status code.
 * @param   pBackend            Shared Clipboard backend to disconnect from.
 * @param   pClient             Shared Clipboard client context.
 */
int ShClBackendDisconnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient);

/**
 * Called when the guest reports available clipboard formats to the host OS.
 *
 * @returns VBox status code.
 * @param   pBackend            Shared Clipboard backend to announce formats to.
 * @param   pClient             Shared Clipboard client context.
 * @param   fFormats            The announced formats from the guest,
 *                              VBOX_SHCL_FMT_XXX.
 */
int ShClBackendReportFormats(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, SHCLFORMATS fFormats);

/**
 * Called when the guest wants to read host clipboard data.
 *
 * @returns VBox status code.
 * @param   pBackend            Shared Clipboard backend to read data from.
 * @param   pClient             Shared Clipboard client context.
 * @param   pCmdCtx             Shared Clipboard command context.
 * @param   uFormat             Clipboard format to read.
 * @param   pvData              Where to return the read clipboard data.
 * @param   cbData              Size (in bytes) of buffer where to return the clipboard data.
 * @param   pcbActual           Where to return the amount of bytes read.
 *
 * @todo    Document: Can return VINF_HGCM_ASYNC_EXECUTE to defer returning read
 *          data
 */
int ShClBackendReadData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT uFormat,
                        void *pvData, uint32_t cbData, uint32_t *pcbActual);

/**
 * Called when the guest writes clipboard data to the host.
 *
 * @returns VBox status code.
 * @param   pBackend            Shared Clipboard backend to write data to.
 * @param   pClient             Shared Clipboard client context.
 * @param   pCmdCtx             Shared Clipboard command context.
 * @param   uFormat             Clipboard format to write.
 * @param   pvData              Clipboard data to write.
 * @param   cbData              Size (in bytes) of buffer clipboard data to write.
 */
int ShClBackendWriteData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData);

/**
 * Called when synchronization of the clipboard contents of the host clipboard with the guest is needed.
 *
 * @returns VBox status code.
 * @param   pBackend            Shared Clipboard backend to synchronize.
 * @param   pClient             Shared Clipboard client context.
 */
int ShClBackendSync(PSHCLBACKEND pBackend, PSHCLCLIENT pClient);
/** @} */

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** @name Host implementations for Shared Clipboard transfers.
 * @{
 */
/**
 * Called before a transfer gets destroyed.
 *
 * @returns VBox status code.
 * @param   pBackend            Shared Clipboard backend to use.
 * @param   pClient             Shared Clipboard client context.
 * @param   pTransfer           Shared Clipboard transfer to destroy.
 */
int ShClBackendTransferDestroy(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer);
/**
 * Called after a transfer status got processed.
 *
 * @returns VBox status code.
 * @param   pBackend            Shared Clipboard backend to use.
 * @param   pClient             Shared Clipboard client context.
 * @param   pTransfer           Shared Clipboard transfer to process status for.
 * @param   enmSource           Transfer source which issues the reply.
 * @param   enmStatus           Transfer status.
 * @param   rcStatus            Status code (IPRT-style). Depends on \a enmStatus set.
 */
int ShClBackendTransferHandleStatusReply(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, SHCLSOURCE enmSource, SHCLTRANSFERSTATUS enmStatus, int rcStatus);
/** @} */
#endif

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** @name Internal Shared Clipboard transfer host service functions.
 * @{
 */
int ShClSvcTransferMsgClientHandler(PSHCLCLIENT pClient, VBOXHGCMCALLHANDLE callHandle, uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM paParms[], uint64_t tsArrival);
int ShClSvcTransferMsgHostHandler(uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
/** @} */

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
/** @} */
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/* Host unit testing interface */
#ifdef UNIT_TEST
uint32_t TestClipSvcGetMode(void);
#endif

#endif /* !VBOX_INCLUDED_SRC_SharedClipboard_VBoxSharedClipboardSvc_internal_h */

