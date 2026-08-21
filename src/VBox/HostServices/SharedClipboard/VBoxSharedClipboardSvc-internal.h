/* $Id: VBoxSharedClipboardSvc-internal.h 115102 2026-08-21 11:14:19Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - Internal service instance state.
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

#ifndef VBOX_INCLUDED_SRC_SharedClipboard_VBoxSharedClipboardSvc_internal_h
#define VBOX_INCLUDED_SRC_SharedClipboard_VBoxSharedClipboardSvc_internal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/critsect.h>
#include <iprt/string.h>

#include <VBox/HostServices/VBoxSharedClipboardSvc.h>
#include <VBox/HostServices/VBoxClipboardExt.h>


/**
 * State of the service extension which bridges the HGCM service to Main.
 */
typedef struct SHCLEXTSTATE
{
    /** Registered service extension entry point, or NULL. */
    PFNHGCMSVCEXT  pfnExtension;
    /** Opaque extension-provided data. */
    void          *pvExtension;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Serializes direct transfer callbacks and fences extension teardown. */
    RTCRITSECT     CritSectTransferCallbacks;
#endif
} SHCLEXTSTATE;


/**
 * Shared Clipboard host service instance state.
 */
typedef struct SHCLSERVICE
{
    /** HGCM service helper table. */
    PVBOXHGCMSVCHELPERS     pHelpers;
    /** Service-global critical section. */
    RTCRITSECT              CritSect;
    /** Current Shared Clipboard mode. */
    uint32_t                uMode;
    /** Next non-zero service session ID to assign to a client. */
    SHCLSESSIONID           idNextSession;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Current Shared Clipboard file transfer mode. */
    uint32_t                fTransferMode;
#endif
    /** Service extension state. */
    SHCLEXTSTATE            ExtState;
    /** The one active HGCM client.  This is a weak pointer owned by HGCM. */
    PSHCLCLIENT             pActiveClient;
    /** Host feature mask (VBOX_SHCL_HF_0_XXX). */
    uint64_t                fHostFeatures0;

    SHCLSERVICE()
        : pHelpers(NULL)
        , uMode(VBOX_SHCL_MODE_OFF)
        , idNextSession(1)
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        , fTransferMode(VBOX_SHCL_TRANSFER_MODE_F_NONE)
#endif
        , pActiveClient(NULL)
        , fHostFeatures0(VBOX_SHCL_HF_0_CONTEXT_ID
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                         | VBOX_SHCL_HF_0_TRANSFERS
#endif
                         )
    {
        RT_ZERO(CritSect);
        RT_ZERO(ExtState);
    }
} SHCLSERVICE;
/** Pointer to Shared Clipboard host service instance state. */
typedef SHCLSERVICE *PSHCLSERVICE;

/** The single Shared Clipboard HGCM host service instance. */
extern SHCLSERVICE g_ShClSvc;

/** @name Service-global locking.
 * @{ */
void shClSvcLock(void);
void shClSvcUnlock(void);
/** @} */

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** @name Service-global transfer policy.
 * @{ */
uint32_t shClSvcTransferModeGet(void);
/** @} */
#endif

/** @name Host-controlled service handling.
 * @{ */
int shClSvcHostModeSet(uint32_t uMode);
DECLCALLBACK(int) shClSvcHostCall(void *pvService, uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
/** @} */

/** @name Client/session and guest message handling.
 * @{ */
void shClSvcClientDestroy(PSHCLCLIENT pClient);
void shClSvcClientReset(PSHCLCLIENT pClient);
DECLCALLBACK(void) shClSvcClientCall(void *pvService, VBOXHGCMCALLHANDLE hCall, uint32_t u32ClientID,
                                     void *pvClient, uint32_t u32Function, uint32_t cParms,
                                     VBOXHGCMSVCPARM paParms[], uint64_t tsArrival);

int shClSvcClientNegotiateChunkSize(PSHCLCLIENT pClient, VBOXHGCMCALLHANDLE hCall,
                                    uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
int shClSvcClientReportFeatures(PSHCLCLIENT pClient, VBOXHGCMCALLHANDLE hCall,
                                uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
int shClSvcClientMsgQueryFeatures(VBOXHGCMCALLHANDLE hCall, uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
int shClSvcClientMsgPeek(PSHCLCLIENT pClient, VBOXHGCMCALLHANDLE hCall,
                         uint32_t cParms, VBOXHGCMSVCPARM paParms[], bool fWait);
int shClSvcClientMsgOldGet(PSHCLCLIENT pClient, VBOXHGCMCALLHANDLE hCall,
                           uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
int shClSvcClientMsgGet(PSHCLCLIENT pClient, VBOXHGCMCALLHANDLE hCall,
                        uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
int shClSvcClientMsgCancel(PSHCLCLIENT pClient, uint32_t cParms);
int shClSvcClientMsgReportFormats(PSHCLCLIENT pClient, uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
int shClSvcClientMsgDataRead(PSHCLCLIENT pClient, uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
int shClSvcClientMsgDataWrite(PSHCLCLIENT pClient, uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
int shClSvcClientMsgError(PSHCLCLIENT pClient, uint32_t cParms, VBOXHGCMSVCPARM paParms[], int *pRc);
/** @} */

/** @name Opaque service-to-Main transport.
 * @{ */
void shClSvcCreateTransport(PSHCLCLIENT pClient, PSHCLTRANSPORT pTransport);
/** @} */

/** @name Service extension bridge handling.
 * @{ */
bool shClSvcExtIsRegistered(void);
int  shClSvcExtInit(void);
int  shClSvcExtTerm(void);
int  shClSvcExtBackendInit(void);
int  shClSvcExtBackendConnect(PSHCLCLIENT pClient);
int  shClSvcExtBackendSync(PSHCLCLIENT pClient);
void shClSvcExtBackendDisconnect(PSHCLCLIENT pClient);
void shClSvcExtBackendDestroy(void);
/** Destroys the backend while the extension remains callable, then clears the
 *  matching registration. */
int  shClSvcExtUnregisterAndDestroy(void);
int  shClSvcExtReportFormatsToHost(PSHCLCLIENT pClient, SHCLFORMATS fFormats);
int  shClSvcExtReadData(PSHCLCLIENT pClient, SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual);
int  shClSvcExtWriteData(PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData);
int  shClSvcExtReportError(char *pszId, char *pszMsg, int rcError);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** Immutable service-to-Main transfer-status snapshot. */
typedef struct SHCLSVCEXTTRANSFERSTATUS
{
    /** Owning service session. */
    SHCLSESSIONID       idSession;
    /** Transfer ID within @a idSession. */
    SHCLTRANSFERID      idTransfer;
    /** Host-private transfer generation. */
    SHCLTRANSFERGEN     uGeneration;
    /** Transfer direction. */
    SHCLTRANSFERDIR     enmDir;
    /** Endpoint which owns the transfer data. */
    SHCLSOURCE          enmTransferSource;
    /** Endpoint which reported this status. */
    SHCLSOURCE          enmReplySource;
    /** Status to report. */
    SHCLTRANSFERSTATUS  enmStatus;
    /** Status-specific VBox result code. */
    int                 rcStatus;
} SHCLSVCEXTTRANSFERSTATUS;
/** Pointer to a service-to-Main transfer-status snapshot. */
typedef SHCLSVCEXTTRANSFERSTATUS *PSHCLSVCEXTTRANSFERSTATUS;
/** Pointer to a const service-to-Main transfer-status snapshot. */
typedef SHCLSVCEXTTRANSFERSTATUS const *PCSHCLSVCEXTTRANSFERSTATUS;

/** Immutable service-to-Main transfer-progress snapshot. */
typedef struct SHCLSVCEXTTRANSFERPROGRESS
{
    /** Owning service session. */
    SHCLSESSIONID       idSession;
    /** Transfer ID within @a idSession. */
    SHCLTRANSFERID      idTransfer;
    /** Host-private transfer generation. */
    SHCLTRANSFERGEN     uGeneration;
    /** Exact aggregate payload bytes processed. */
    uint64_t            cbProcessed;
    /** Exact aggregate payload bytes to process. */
    uint64_t            cbTotal;
} SHCLSVCEXTTRANSFERPROGRESS;
/** Pointer to a service-to-Main transfer-progress snapshot. */
typedef SHCLSVCEXTTRANSFERPROGRESS *PSHCLSVCEXTTRANSFERPROGRESS;
/** Pointer to a const service-to-Main transfer-progress snapshot. */
typedef SHCLSVCEXTTRANSFERPROGRESS const *PCSHCLSVCEXTTRANSFERPROGRESS;

int  shClSvcExtQueryTransferCallbacks(PSHCLCLIENT pClient, PSHCLTRANSFERCALLBACKS pCallbacks);
int  shClSvcExtNotifyTransferStatus(PSHCLCLIENT pClient, PCSHCLSVCEXTTRANSFERSTATUS pStatus);
int  shClSvcExtNotifyTransferDetachedStatus(PSHCLCLIENT pClient, PCSHCLSVCEXTTRANSFERSTATUS pStatus);
int  shClSvcExtNotifyTransferProgress(PSHCLCLIENT pClient, PCSHCLSVCEXTTRANSFERPROGRESS pProgress);
int  shClSvcExtNotifyTransferReset(PSHCLCLIENT pClient);
#endif
DECLCALLBACK(int) shClSvcRegisterExtension(void *pvService, PFNHGCMSVCEXT pfnExtension, void *pvExtension);
/** @} */

#endif /* !VBOX_INCLUDED_SRC_SharedClipboard_VBoxSharedClipboardSvc_internal_h */
