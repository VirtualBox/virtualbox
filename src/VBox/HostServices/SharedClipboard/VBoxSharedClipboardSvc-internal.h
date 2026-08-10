/* $Id: VBoxSharedClipboardSvc-internal.h 114971 2026-08-10 17:29:14Z andreas.loeffler@oracle.com $ */
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

#include <iprt/string.h>

#include <VBox/HostServices/VBoxSharedClipboardSvc.h>


/**
 * Shared Clipboard host service instance state.
 */
typedef struct SHCLSERVICE
{
    /** The backend instance data.  Only one backend at a time is supported currently. */
    SHCLBACKEND             Backend;
    /** HGCM service helper table. */
    PVBOXHGCMSVCHELPERS     pHelpers;
    /** HGCM service function table. */
    VBOXHGCMSVCFNTABLE     *pTable;
    /** Service-global critical section. */
    RTCRITSECT              CritSect;
    /** Current Shared Clipboard mode. */
    uint32_t                uMode;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Current Shared Clipboard file transfer mode. */
    uint32_t                fTransferMode;
    /** Next non-zero service session ID to assign to a client. */
    SHCLSESSIONID           idNextSession;
#endif
    /** Service extension state. */
    SHCLEXTSTATE            ExtState;
    /** Connected HGCM clients keyed by client ID. */
    ClipboardClientMap      mapClients;
    /** Deferred clients ready to process new commands. */
    ClipboardClientQueue    listClientsDeferred;
    /** Host feature mask (VBOX_SHCL_HF_0_XXX). */
    uint64_t                fHostFeatures0;

    SHCLSERVICE()
        : pHelpers(NULL)
        , pTable(NULL)
        , uMode(VBOX_SHCL_MODE_OFF)
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        , fTransferMode(VBOX_SHCL_TRANSFER_MODE_F_NONE)
        , idNextSession(1)
#endif
        , fHostFeatures0(VBOX_SHCL_HF_0_CONTEXT_ID
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                         | VBOX_SHCL_HF_0_TRANSFERS
#endif
                         )
    {
        RT_ZERO(Backend);
        RT_ZERO(CritSect);
        RT_ZERO(ExtState);
    }
} SHCLSERVICE;
/** Pointer to Shared Clipboard host service instance state. */
typedef SHCLSERVICE *PSHCLSERVICE;

/** The single Shared Clipboard HGCM host service instance. */
extern SHCLSERVICE g_ShClSvc;

/* Transitional aliases.  These keep the initial instance-state patch small and
   will be removed as client/control/state code is split into local units. */
#define g_ShClBackend          (g_ShClSvc.Backend)
#define g_pHelpers             (g_ShClSvc.pHelpers)
#define g_pTable               (g_ShClSvc.pTable)
#define g_CritSect             (g_ShClSvc.CritSect)
#define g_uMode                (g_ShClSvc.uMode)
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# define g_fTransferMode       (g_ShClSvc.fTransferMode)
# define g_idNextSession       (g_ShClSvc.idNextSession)
#endif
#define g_ExtState             (g_ShClSvc.ExtState)
#define g_mapClients           (g_ShClSvc.mapClients)
#define g_listClientsDeferred  (g_ShClSvc.listClientsDeferred)
#define g_fHostFeatures0       (g_ShClSvc.fHostFeatures0)

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

/** @name Backend and extension bridge handling.
 * @{ */
int  shClSvcBackendInit(VBOXHGCMSVCFNTABLE *pTable);
int  shClSvcBackendConnect(PSHCLCLIENT pClient);
int  shClSvcBackendSync(PSHCLCLIENT pClient);
void shClSvcBackendDisconnect(PSHCLCLIENT pClient);
void shClSvcBackendDestroy(void);
int  shClSvcBackendReportFormatsToGuest(PSHCLCLIENT pClient, SHCLFORMATS fFormats, SHCLSOURCE enmSource);
int  shClSvcBackendReportFormatsToHost(PSHCLCLIENT pClient, SHCLFORMATS fFormats);
int  shClSvcBackendReadData(PSHCLCLIENT pClient, SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual);
int  shClSvcBackendWriteData(PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData);
DECLCALLBACK(int) shClSvcRegisterExtension(void *pvService, PFNHGCMSVCEXT pfnExtension, void *pvExtension);
/** @} */

#endif /* !VBOX_INCLUDED_SRC_SharedClipboard_VBoxSharedClipboardSvc_internal_h */
