/* $Id: ClipboardImpl.h 115054 2026-08-17 16:27:08Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Console clipboard API.
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

#ifndef MAIN_INCLUDED_ClipboardImpl_h
#define MAIN_INCLUDED_ClipboardImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ClipboardWrap.h"

#include <VBox/GuestHost/SharedClipboard.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif

#include <vector>

class ClipboardSession;
class Console;

/**
 * Client-side implementation of live clipboard operations for a console.
 */
class ATL_NO_VTABLE Clipboard :
    public ClipboardWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(Clipboard)

    HRESULT FinalConstruct();
    void FinalRelease();

    HRESULT init(Console *aParent);
    void uninit();

    HRESULT i_readData(ClipboardAction_T aAction,
                       ClipboardSource_T *aSource,
                       com::Utf8Str &aMimeType,
                       std::vector<BYTE> &aBuffer);
    HRESULT i_readDataForFormat(ClipboardAction_T aAction,
                                uint32_t uFormat,
                                ClipboardSource_T *aSource,
                                com::Utf8Str &aMimeType,
                                std::vector<BYTE> &aBuffer);
    HRESULT i_readFormats(std::vector<com::Utf8Str> &aFormats);
    HRESULT i_readFormatObjects(std::vector<ComPtr<IClipboardFormat> > &aFormats);
    HRESULT i_readDataRaw(ClipboardAction_T aAction,
                          const com::Utf8Str &aRequestedMimeType,
                          ClipboardSource_T *aSource,
                          com::Utf8Str &aMimeType,
                          std::vector<BYTE> &aBuffer);
    HRESULT i_writeData(VBOXSHCLMAINCLIENTID aClientId,
                        ClipboardAction_T aAction,
                        ClipboardSource_T aSource,
                        const com::Utf8Str &aMimeType,
                        const std::vector<BYTE> &aBuffer,
                        ClipboardSource_T *aWrittenSource,
                        com::Utf8Str &aWrittenMimeType,
                        std::vector<BYTE> &aWrittenBuffer);
    HRESULT i_writeDataRaw(VBOXSHCLMAINCLIENTID aClientId,
                           ClipboardAction_T aAction,
                           ClipboardSource_T aSource,
                           const com::Utf8Str &aMimeType,
                           const std::vector<BYTE> &aBuffer,
                           ClipboardSource_T *aWrittenSource,
                           com::Utf8Str &aWrittenMimeType,
                           std::vector<BYTE> &aWrittenBuffer);
    HRESULT i_writeFormats(VBOXSHCLMAINCLIENTID aClientId,
                           const std::vector<com::Utf8Str> &aFormats);
    HRESULT i_writeFormatObjects(VBOXSHCLMAINCLIENTID aClientId,
                                 const std::vector<ComPtr<IClipboardFormat> > &aFormats);
    HRESULT i_getCurrentStateForEvent(ClipboardSource_T *aSource,
                                      std::vector<ComPtr<IClipboardFormat> > &aFormats);
    HRESULT i_getCurrentSource(ClipboardSource_T *aSource);
    LONG64 i_nextEventRevision();
    HRESULT i_reset();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    HRESULT i_transferCancel(ULONG aTransferId);
    HRESULT i_transferCancel(SHCLSESSIONID aServiceSessionId, SHCLTRANSFERID aTransferId, SHCLTRANSFERGEN aGeneration);
    /**
     * Handles a Shared Clipboard transfer lifecycle status delivered by the host service.
     *
     * @returns COM status code.
     * @param   aServiceSessionId   Shared Clipboard service session identifier.
     * @param   aTransferId         Shared Clipboard transfer identifier.
     * @param   aGeneration         Host-private transfer generation.
     * @param   aTransfer           Borrowed service transfer backing the data plane.
     * @param   enmShClSource       Data source recorded by the backing transfer.
     * @param   enmStatus           Transfer lifecycle status.
     * @param   vrcTransfer         Transfer status result code.
     */
    HRESULT i_handleTransferStatus(SHCLSESSIONID aServiceSessionId,
                                   SHCLTRANSFERID aTransferId,
                                   SHCLTRANSFERGEN aGeneration,
                                   PSHCLTRANSFER aTransfer,
                                   SHCLSOURCE enmShClSource,
                                   SHCLTRANSFERSTATUS enmStatus,
                                   int vrcTransfer);
#else
    HRESULT i_transferCancel(ULONG aTransferId);
#endif
    HRESULT i_readDataForGuest(uint32_t uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual);
    HRESULT i_requestData(const com::Utf8Str &aMimeType, ULONG *aRequestId);
    HRESULT i_reportData(ClipboardAction_T aAction, ClipboardSource_T aSource, uint32_t fFormat, const void *pvData, uint32_t cbData);
    HRESULT i_hostClipboardReportFormats(VBOXSHCLMAINCLIENTID aClientId,
                                         ClipboardAction_T aAction,
                                         ClipboardSource_T aSource,
                                         const std::vector<ComPtr<IClipboardFormat> > &aFormats);
    HRESULT i_hostClipboardProvideData(VBOXSHCLMAINCLIENTID aClientId,
                                       ULONG aRequestId,
                                       ClipboardAction_T aAction,
                                       ClipboardSource_T aSource,
                                       const com::Utf8Str &aMimeType,
                                       const std::vector<BYTE> &aBuffer);
    HRESULT i_hostClipboardSetData(VBOXSHCLMAINCLIENTID aClientId,
                                   ClipboardAction_T aAction,
                                   ClipboardSource_T aSource,
                                   const com::Utf8Str &aMimeType,
                                   const std::vector<BYTE> &aBuffer);
    HRESULT i_hostClipboardClear(VBOXSHCLMAINCLIENTID aClientId);
    HRESULT i_registerSession(VBOXSHCLMAINCLIENTID aClientId,
                              ClipboardSession *aSession,
                              uint32_t fFlags,
                              const ComPtr<IEventSource> &aEventSource);
    void i_unregisterSession(VBOXSHCLMAINCLIENTID aClientId);
    HRESULT i_fireSessionInitialState(VBOXSHCLMAINCLIENTID aClientId);

    bool i_reportFormats(VBOXSHCLMAINCLIENTID aClientId,
                         uint32_t fFormats,
                         ClipboardSource_T aSource,
                         bool fForceNotify = false,
                         bool fRequireCurrentClientId = false,
                         VBOXSHCLMAINCLIENTID aRequiredCurrentClientId = VBOX_SHCL_MAIN_CLIENT_NONE);
    void i_fireClipboardError(const com::Utf8Str &aId, const com::Utf8Str &aErrMsg, LONG aRc);
    void i_fireClipboardModeChanged(ClipboardMode_T aClipboardMode);
    void i_fireClipboardFileTransferModeChanged(bool fEnabled);
    void i_fireClipboardTransferEvent(VBOXSHCLMAINCLIENTID aClientId,
                                      IClipboardTransfer *aTransfer,
                                      ClipboardTransferState_T aState,
                                      ClipboardTransferInteraction_T aInteraction,
                                      const com::Utf8Str &aPath,
                                      const com::Utf8Str &aMessage,
                                      ClipboardError_T aError);
    int i_changeMode(ClipboardMode_T aClipboardMode);
    int i_changeFileTransferMode(bool fEnabled);

private:

    /** @name Wrapped IClipboard properties and methods
     * @{ */
    HRESULT getTransfers(ComPtr<IClipboardTransferManager> &aTransfers);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getHostClipboard(ComPtr<IHostClipboard> &aHostClipboard);
    HRESULT createFormat(const com::Utf8Str &aMimeType,
                         ComPtr<IClipboardFormat> &aFormat);
    HRESULT createSession(const std::vector<IClipboardSessionFlag_T> &aFlags,
                          ComPtr<IClipboardSession> &aSession);
    HRESULT createItem(ClipboardSource_T aSource,
                       const ComPtr<IClipboardFormat> &aFormat,
                       const std::vector<BYTE> &aBuffer,
                       ComPtr<IClipboardItem> &aItem);
    HRESULT readData(ClipboardAction_T aAction, ComPtr<IClipboardItem> &aItem);
    HRESULT readFormats(std::vector<ComPtr<IClipboardFormat> > &aFormats);
    HRESULT writeData(ClipboardAction_T aAction,
                      const ComPtr<IClipboardItem> &aItem,
                      ComPtr<IClipboardItem> &aWrittenItem);
    HRESULT readDataRaw(ClipboardAction_T aAction,
                        const com::Utf8Str &aRequestedMimeType,
                        ClipboardSource_T *aSource,
                        com::Utf8Str &aMimeType,
                        std::vector<BYTE> &aBuffer);
    HRESULT writeDataRaw(ClipboardAction_T aAction,
                         ClipboardSource_T aSource,
                         const com::Utf8Str &aMimeType,
                         const std::vector<BYTE> &aBuffer,
                         ClipboardSource_T *aWrittenSource,
                         com::Utf8Str &aWrittenMimeType,
                         std::vector<BYTE> &aWrittenBuffer);
    HRESULT writeFormats(const std::vector<ComPtr<IClipboardFormat> > &aFormats);
    HRESULT reset();
    HRESULT isFormatAvailable(ClipboardSource_T aSource,
                              const ComPtr<IClipboardFormat> &aFormat,
                              BOOL *aAvailable);
    HRESULT getSupportedFormats(ClipboardSource_T aSource,
                                std::vector<ComPtr<IClipboardFormat> > &aFormats);
    /** @} */

    /** @name Wrapped IInternalClipboardControl methods
     * @{ */
    HRESULT requestData(const com::Utf8Str &aMimeType,
                        ULONG *aRequestId);
    /**
     * Sets a Shared Clipboard transfer status for Main testcase coverage.
     *
     * @returns COM status code.
     * @param   aServiceSessionId   Shared Clipboard service session identifier.
     * @param   aTransferId         Shared Clipboard transfer identifier.
     * @param   aGeneration         Host-private transfer generation.
     * @param   aSource             Data source associated with the status.
     * @param   aStatus             Internal Shared Clipboard transfer status value.
     * @param   aResult             IPRT result associated with the status.
     */
    HRESULT setTransferStatus(ULONG aServiceSessionId,
                              ULONG aTransferId,
                              LONG64 aGeneration,
                              ClipboardSource_T aSource,
                              ULONG aStatus,
                              LONG aResult);
    /** @} */

    HRESULT i_createFormat(const com::Utf8Str &aMimeType, ComPtr<IClipboardFormat> &aFormat);
    HRESULT i_createItem(ClipboardSource_T aSource,
                         const com::Utf8Str &aMimeType,
                         const std::vector<BYTE> &aBuffer,
                         ComPtr<IClipboardItem> &aItem);
    void i_storeData(ClipboardAction_T aAction,
                     ClipboardSource_T aSource,
                     const com::Utf8Str &aMimeType,
                     const std::vector<BYTE> &aBuffer);
    bool i_storeDataIfCurrentClient(VBOXSHCLMAINCLIENTID aClientId,
                                    ClipboardAction_T aAction,
                                    ClipboardSource_T aSource,
                                    const com::Utf8Str &aMimeType,
                                    const std::vector<BYTE> &aBuffer,
                                    uint32_t fFormats);
    void i_storeDataLocked(ClipboardAction_T aAction,
                           ClipboardSource_T aSource,
                           const com::Utf8Str &aMimeType,
                           const std::vector<BYTE> &aBuffer);
    void i_clearPendingDataRequestsLocked();
    void i_removePendingDataRequestLocked(ULONG aRequestId);
    void i_removePendingDataRequest(ULONG aRequestId);
    void i_fireDataChanged(VBOXSHCLMAINCLIENTID aClientId,
                           ClipboardAction_T aAction,
                           ClipboardSource_T aSource,
                           const com::Utf8Str &aMimeType,
                           const std::vector<BYTE> &aBuffer);
    ULONG i_fireDataRequested(VBOXSHCLMAINCLIENTID aClientId,
                              ClipboardAction_T aAction,
                              ClipboardSource_T aSource,
                              uint32_t uFormat,
                              const std::vector<BYTE> *pResolvedBuffer = NULL);
    bool i_isClientIdRegisteredLocked(VBOXSHCLMAINCLIENTID aClientId) const;
    bool i_isClientFormatOwnerLocked(VBOXSHCLMAINCLIENTID aClientId, uint32_t fFormats, ClipboardSource_T aSource) const;
    VBOXSHCLMAINCLIENTID i_allocateClientId();

    struct SessionEventTarget
    {
        VBOXSHCLMAINCLIENTID mClientId;
        uint32_t             mfFlags;
        ComPtr<IEventSource> mEventSource;
    };
    void i_getSessionEventTargets(std::vector<SessionEventTarget> &aTargets,
                                  VBOXSHCLMAINCLIENTID aClientId,
                                  bool fPassive,
                                  bool fCheckReflection,
                                  uint32_t fFormats,
                                  ClipboardSource_T aSource);

    struct Data;
    Data *mData;

#ifdef VBOX_WITH_SHARED_CLIPBOARD
    /** Last Shared Clipboard format mask reported through the active service extension. */
    uint32_t m_fFormats;
#endif
};

#endif /* !MAIN_INCLUDED_ClipboardImpl_h */
