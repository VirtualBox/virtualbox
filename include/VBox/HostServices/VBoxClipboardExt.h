/** @file
 * Shared Clipboard - Common header for the service extension.
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

#ifndef VBOX_INCLUDED_HostServices_VBoxClipboardExt_h
#define VBOX_INCLUDED_HostServices_VBoxClipboardExt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <VBox/GuestHost/SharedClipboard.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif

/** Opaque declaration of an HGCM Shared Clipboard client. */
typedef struct _SHCLCLIENT SHCLCLIENT, *PSHCLCLIENT;
/** Opaque declaration of a Shared Clipboard client command context. */
typedef struct _SHCLCLIENTCMDCTX SHCLCLIENTCMDCTX, *PSHCLCLIENTCMDCTX;
/** Opaque declaration of a Shared Clipboard transfer. */
typedef struct SHCLTRANSFER *PSHCLTRANSFER;
/** Opaque declaration of a Shared Clipboard reply. */
typedef struct _SHCLREPLY *PSHCLREPLY;

/** Opaque identity of a client owned exclusively by the HGCM service. */
typedef struct SHCLCLIENTOPAQUE *SHCLCLIENTHANDLE;
/** Opaque retained guest-data reply owned by the HGCM service. */
typedef struct SHCLGUESTDATATOKENOPAQUE *SHCLGUESTDATATOKEN;
/** Pointer to an opaque guest-data reply token. */
typedef SHCLGUESTDATATOKEN *PSHCLGUESTDATATOKEN;

struct SHCLTRANSPORT;
typedef struct SHCLTRANSPORT SHCLTRANSPORT;
typedef SHCLTRANSPORT *PSHCLTRANSPORT;
typedef SHCLTRANSPORT const *PCSHCLTRANSPORT;

/** Operations implemented by the HGCM service for an opaque client. */
typedef struct SHCLSVCOPS
{
    /** Size of this operation table. */
    uint32_t                    cbStruct;
    /** Applies service transfer policy and compatibility rules to a format mask. */
    DECLCALLBACKMEMBER(int, pfnFilterFormats, (SHCLCLIENTHANDLE hClient, bool fHostToGuest,
                                               SHCLFORMATS fFormats, SHCLFORMATS *pfFiltered));
    /** Queues a format announcement for the guest, or returns VINF_NO_CHANGE if policy suppresses it. */
    DECLCALLBACKMEMBER(int, pfnReportFormatsToGuest, (SHCLCLIENTHANDLE hClient, SHCLFORMATS fFormats,
                                                      SHCLFORMATS *pfReported));
    /** Queues guest data reads without waiting. */
    DECLCALLBACKMEMBER(int, pfnReadDataFromGuestAsync, (SHCLCLIENTHANDLE hClient, SHCLFORMATS fFormats,
                                                        PSHCLEVENT *ppEvent));
    /** Reads and waits for one guest clipboard format. */
    DECLCALLBACKMEMBER(int, pfnReadDataFromGuest, (SHCLCLIENTHANDLE hClient, SHCLFORMAT uFormat,
                                                   void **ppvData, uint32_t *pcbData));
    /** Validates and retains a guest reply before it is forwarded. */
    DECLCALLBACKMEMBER(int, pfnGuestDataBegin, (SHCLCLIENTHANDLE hClient, PSHCLCLIENTCMDCTX pCmdCtx,
                                                SHCLFORMAT uFormat, PSHCLGUESTDATATOKEN phToken));
    /** Signals and releases a retained guest reply token. */
    DECLCALLBACKMEMBER(int, pfnGuestDataComplete, (SHCLCLIENTHANDLE hClient, SHCLGUESTDATATOKEN hToken,
                                                   void const *pvData, uint32_t cbData));
    /** Releases a retained guest reply token without signalling it. */
    DECLCALLBACKMEMBER(void, pfnGuestDataCancel, (SHCLCLIENTHANDLE hClient, SHCLGUESTDATATOKEN hToken));
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Retains a transfer selected by ID. */
    DECLCALLBACKMEMBER(PSHCLTRANSFER, pfnTransferGetByIdRetained, (SHCLCLIENTHANDLE hClient,
                                                                  SHCLTRANSFERID idTransfer));
    /** Retains a transfer selected by its full generation key. */
    DECLCALLBACKMEMBER(PSHCLTRANSFER, pfnTransferGetByKeyRetained, (SHCLCLIENTHANDLE hClient,
                                                                   SHCLSESSIONID idSession,
                                                                   SHCLTRANSFERID idTransfer,
                                                                   SHCLTRANSFERGEN uGeneration));
    /** Creates and retains a service-owned transfer. */
    DECLCALLBACKMEMBER(int, pfnTransferCreate, (SHCLCLIENTHANDLE hClient, SHCLTRANSFERDIR enmDir,
                                                SHCLSOURCE enmSource, PSHCLTRANSFERCALLBACKS pCallbacks,
                                                SHCLTRANSFERID idTransfer, PSHCLTRANSFER *ppTransfer));
    /** Initializes a service-owned transfer. */
    DECLCALLBACKMEMBER(int, pfnTransferInit, (SHCLCLIENTHANDLE hClient, PSHCLTRANSFER pTransfer));
    /** Destroys a transfer selected by ID. */
    DECLCALLBACKMEMBER(void, pfnTransferDestroyById, (SHCLCLIENTHANDLE hClient, SHCLTRANSFERID idTransfer));
    /** Destroys all transfers for a disconnecting client. */
    DECLCALLBACKMEMBER(void, pfnTransferDestroyAll, (SHCLCLIENTHANDLE hClient));
    /** Initializes a guest-facing provider without exposing the service client. */
    DECLCALLBACKMEMBER(int, pfnTransferProviderInitGuest, (SHCLCLIENTHANDLE hClient, PSHCLTXPROVIDER pProvider));
#endif
} SHCLSVCOPS;
/** Pointer to a const service operation table. */
typedef SHCLSVCOPS const *PCSHCLSVCOPS;

/**
 * Non-owning transport value passed to Main instead of the service client.
 *
 * The service owns both referenced values from the successful backend-connect
 * callback through the matching backend-disconnect callback.  Operations are
 * synchronous and must not retain either value beyond the call.
 */
struct SHCLTRANSPORT
{
    /** Opaque service-owned client identity. */
    SHCLCLIENTHANDLE            hClient;
    /** Immutable service operation table. */
    PCSHCLSVCOPS                pOps;
};

/**
 * Checks whether a transport references a service client and operation table.
 *
 * @returns true if @a pTransport is structurally valid, false otherwise.
 * @param   pTransport          Transport to validate.  May be NULL.
 */
DECLINLINE(bool) ShClTransportIsValid(PCSHCLTRANSPORT pTransport)
{
    return    pTransport != NULL
           && pTransport->hClient != NULL
           && pTransport->pOps != NULL
           && pTransport->pOps->cbStruct == sizeof(*pTransport->pOps);
}

/**
 * Checks whether two transport values identify the same service endpoint.
 *
 * @returns true if both transports are valid and identify the same client and
 *          operation table, false otherwise.
 * @param   pLeft               First transport to compare.
 * @param   pRight              Second transport to compare.
 */
DECLINLINE(bool) ShClTransportIsEqual(PCSHCLTRANSPORT pLeft, PCSHCLTRANSPORT pRight)
{
    return    ShClTransportIsValid(pLeft)
           && ShClTransportIsValid(pRight)
           && pLeft->hClient == pRight->hClient
           && pLeft->pOps == pRight->pOps;
}

/** Sets a read / write callback. */
#define VBOX_CLIPBOARD_EXT_FN_SET_CALLBACK               (0)
/** The guest reports clipboard formats to the extension. */
#define VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST      (1)
/** Reports remote clipboard formats to the guest. */
#define VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_GUEST     (2)
/** The clipboard service requests clipboard data from the extension. */
#define VBOX_CLIPBOARD_EXT_FN_DATA_READ                  (3)
/** The clipboard service writes clipboard data to the extension. */
#define VBOX_CLIPBOARD_EXT_FN_DATA_WRITE                 (4)
/** The clipboard service announces an error to the extension. */
#define VBOX_CLIPBOARD_EXT_FN_ERROR                      (5)
/** The clipboard service initializes the backend. */
#define VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT               (6)
/** The clipboard service tears down the backend. */
#define VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY            (7)
/** The clipboard service connects the backend. */
#define VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT            (8)
/** The clipboard service disconnects the backend. */
#define VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT         (9)
/** The clipboard service syncs with the backend. */
#define VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC               (10)
/** Requests guest clipboard data for VRDE. */
#define VBOX_CLIPBOARD_EXT_FN_DATA_READ_VRDE             (11)
/** The clipboard service initiates the transfer of a file from the guest. */
#define VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER              (12)
/** Reserved. */
#define VBOX_CLIPBOARD_EXT_FN_RESERVED_13                (13)
/** The clipboard service requests the native transfer callback table. */
#define VBOX_CLIPBOARD_EXT_FN_TRANSFER_CALLBACKS         (14)

typedef DECLCALLBACKTYPE(int, FNSHCLEXTCALLBACK,(uint32_t u32Function, uint32_t u32Format, void *pvData, uint32_t cbData));
typedef FNSHCLEXTCALLBACK *PFNSHCLEXTCALLBACK;

/** Structure for holding Shared Clipboard service extension parameters. */
typedef struct _SHCLEXTPARMS
{
    union
    {
        /** Reports clipboard formats. */
        struct
        {
            SHCLFORMATS             uFormats;
            PSHCLCLIENT             pClient;
            SHCLSOURCE              enmSource;
        } ReportFormats;
        /** Reads / writes clipboard data. */
        struct
        {
            SHCLFORMAT              uFormat;
            void                   *pvData;
            uint32_t                cbData;
            uint32_t                cbActual;
            PSHCLCLIENT             pClient;
            void                   *pvReserved0;
            void                   *pvReserved1;
            PSHCLCLIENTCMDCTX       pCmdCtx;
            bool                    fHeadless;
        } ReadWriteData;
        /** Sets a read / write callback. */
        struct
        {
            PFNSHCLEXTCALLBACK      pfnCallback;
        } SetCallback;
        /** Reports a clipboard error. */
        struct
        {
            char                   *pszId;
            char                   *pszMsg;
            int                     rc;
        } Error;
        /** Sends / receives clipboard files. */
        struct
        {
            PSHCLCLIENT             pClient;
            PSHCLTRANSFER           pTransfer;
            SHCLSOURCE              enmShClSource;
            PSHCLREPLY              pReply;
        } FileTransferData;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        /** Queries Main for callbacks to attach to a new transfer. */
        struct
        {
            PSHCLCLIENT             pClient;
            PSHCLTRANSFERCALLBACKS  pCallbacks;
        } TransferCallbacks;
#endif
    } u;
} SHCLEXTPARMS;
/** Pointer to Shared Clipboard service extension parameters. */
typedef SHCLEXTPARMS *PSHCLEXTPARMS;
/** Pointer to const Shared Clipboard service extension parameters. */
typedef SHCLEXTPARMS const *PCSHCLEXTPARMS;

/**
 * Stores an opaque service transport in the reserved extension parameter slots.
 *
 * @param   pParms              Extension parameter block to update.
 * @param   pTransport          Transport value to store.
 */
DECLINLINE(void) ShClSvcExtSetTransport(PSHCLEXTPARMS pParms, PCSHCLTRANSPORT pTransport)
{
    pParms->u.ReadWriteData.pvReserved0 = (void *)pTransport->hClient;
    pParms->u.ReadWriteData.pvReserved1 = (void *)pTransport->pOps;
}

/**
 * Gets the opaque service transport stored in the reserved parameter slots.
 *
 * @returns Stored transport value.  Use ShClTransportIsValid() before use.
 * @param   pParms              Extension parameter block containing the transport.
 */
DECLINLINE(SHCLTRANSPORT) ShClSvcExtGetTransport(PCSHCLEXTPARMS pParms)
{
    SHCLTRANSPORT Transport;
    Transport.hClient = (SHCLCLIENTHANDLE)pParms->u.ReadWriteData.pvReserved0;
    Transport.pOps    = (PCSHCLSVCOPS)pParms->u.ReadWriteData.pvReserved1;
    return Transport;
}

#endif /* !VBOX_INCLUDED_HostServices_VBoxClipboardExt_h */
