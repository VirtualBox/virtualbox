/* $Id: GuestShClConn.h 115105 2026-08-24 16:57:58Z andreas.loeffler@oracle.com $ */
/** @file
 * Main Shared Clipboard - Service connection management.
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

#ifndef MAIN_INCLUDED_GuestShClConn_h
#define MAIN_INCLUDED_GuestShClConn_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestShClBackend.h"

#include <iprt/critsect.h>
#include <iprt/semaphore.h>

class GuestShCl;

/**
 * Represents Main's connection to one Shared Clipboard HGCM client.
 *
 * The HGCM service owns the opaque client referenced by the service endpoint,
 * whereas this object owns the corresponding native backend context.  Public
 * operations pin both values internally so that service disconnect can wait
 * for callers without exposing an ownership guard to users of this class.
 */
class GuestShClConn
{
public:
    /**
     * Creates an empty Shared Clipboard connection.
     *
     * @param   pOwner              GuestShCl instance owning this connection.  May be
     *                              NULL in an out-of-process testcase.
     * @throws  VBox status code if internal synchronization cannot be created.
     */
    GuestShClConn(GuestShCl *pOwner);

    /**
     * Destroys the connection object.
     *
     * The native backend and service client must have been disconnected first.
     */
    ~GuestShClConn(void);

    /**
     * Initializes the process-wide native clipboard backend.
     *
     * @returns VBox status code.
     */
    int initBackend(void);

    /**
     * Disconnects the current client, if any, and destroys the native backend.
     *
     * @returns VBox status code from disconnecting the active backend context.
     */
    int destroyBackend(void);

    /**
     * Replaces the native backend callback table for test purposes.
     *
     * @param   pCallbacks          Callback table, or NULL for backend defaults.
     */
    void setBackendCallbacks(PSHCLCALLBACKS pCallbacks);

    /**
     * Connects a service endpoint to the native clipboard backend.
     *
     * @returns VBox status code.
     * @param   pTransport          Service endpoint supplied by the HGCM service.
     */
    int connect(PCSHCLTRANSPORT pTransport);

    /**
     * Disconnects the service endpoint from the native clipboard backend.
     *
     * @returns VBox status code.
     * @param   pTransport          Service endpoint being disconnected.
     */
    int disconnect(PCSHCLTRANSPORT pTransport);

    /**
     * Checks whether a service endpoint identifies the active connection.
     *
     * @returns true if @a pTransport identifies this connection, otherwise false.
     * @param   pTransport          Service endpoint to compare.
     */
    bool matches(PCSHCLTRANSPORT pTransport) const;

    /**
     * Checks whether a service client is connected.
     *
     * @returns true if connected, otherwise false.
     */
    bool isConnected(void) const;

    /**
     * Queues a clipboard format announcement for the guest.
     *
     * @returns VBox status code.
     * @param   fFormats            Formats to report, VBOX_SHCL_FMT_XXX.
     * @param   pfReported          Where to return the filtered formats. Optional.
     */
    int reportFormatsToGuest(SHCLFORMATS fFormats, SHCLFORMATS *pfReported = NULL);

    /**
     * Publishes formats discovered by the native host clipboard backend.
     *
     * @returns VBox status code.
     * @param   fFormats            Formats to publish, VBOX_SHCL_FMT_XXX.
     */
    int reportLocalFormats(SHCLFORMATS fFormats);

    /**
     * Requests clipboard data from the guest without waiting for a reply if
     * the native backend is selected.
     *
     * @returns VBox status code.
     * @param   fFormats            Requested formats, VBOX_SHCL_FMT_XXX.
     * @param   ppEvent             Where to return the reply event. Optional.
     */
    int readDataFromGuestAsync(SHCLFORMATS fFormats, PSHCLEVENT *ppEvent);

    /**
     * Requests and waits for clipboard data from the guest if the native
     * backend is selected.
     *
     * @returns VBox status code.
     * @param   uFormat             Requested format, VBOX_SHCL_FMT_XXX.
     * @param   ppvData             Where to return the allocated data buffer.
     * @param   pcbData             Where to return the data size.
     */
    int readDataFromGuest(SHCLFORMAT uFormat, void **ppvData, uint32_t *pcbData);

    /**
     * Validates and retains a pending guest-data reply.
     *
     * @returns VBox status code.
     * @retval  VINF_SUCCESS if the reply was retained or if its event already
     *          expired.  In the latter case @a phToken is set to NULL.
     * @param   pCmdCtx             Command context identifying the pending reply.
     * @param   uFormat             Format carried by the reply.
     * @param   phToken             Where to return the retained reply token, or
     *                              NULL if the event already expired.  A returned
     *                              token pins the connection until it is passed to
     *                              guestDataComplete() or guestDataCancel().
     */
    int guestDataBegin(PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT uFormat, PSHCLGUESTDATATOKEN phToken);

    /**
     * Signals and releases a retained guest-data reply.
     *
     * @returns VBox status code.
     * @param   hToken              Retained reply token returned by guestDataBegin().
     * @param   pvData              Reply data. Optional when @a cbData is zero.
     * @param   cbData              Reply data size in bytes.
     */
    int guestDataComplete(SHCLGUESTDATATOKEN hToken, void const *pvData, uint32_t cbData);

    /**
     * Releases a retained guest-data reply without signalling it.
     *
     * @param   hToken              Retained reply token returned by guestDataBegin().
     */
    void guestDataCancel(SHCLGUESTDATATOKEN hToken);

    /**
     * Synchronizes the native clipboard backend with the guest.
     *
     * @returns VBox status code.
     */
    int syncBackend(void);

    /**
     * Reports guest formats to the native clipboard backend.
     *
     * @returns VBox status code.
     * @param   fFormats            Guest formats, VBOX_SHCL_FMT_XXX.
     */
    int reportFormatsToBackend(SHCLFORMATS fFormats);

    /**
     * Reads data from the native clipboard backend.
     *
     * @returns VBox status code.
     * @param   uFormat             Clipboard format to read.
     * @param   pvData              Destination buffer.
     * @param   cbData              Destination buffer size.
     * @param   pcbActual           Where to return the actual or required size.
     */
    int readDataFromBackend(SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual);

    /**
     * Writes guest data to the native clipboard backend.
     *
     * @returns VBox status code.
     * @param   uFormat             Clipboard format to write.
     * @param   pvData              Data buffer. Optional when @a cbData is zero.
     * @param   cbData              Data size in bytes.
     */
    int writeDataToBackend(SHCLFORMAT uFormat, void *pvData, uint32_t cbData);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /**
     * Acquires the specified service transport for a transfer operation.
     *
     * Prevents the associated connection from completing teardown while the
     * transport is acquired.  The call fails if @a pTransport is not the
     * current service endpoint or connection teardown has already begun.
     *
     * @returns VBox status code.
     * @param   pTransport          Service transport to acquire.
     *
     * On success, the caller must invoke transportRelease().
     */
    int transportAcquire(PCSHCLTRANSPORT pTransport);

    /**
     * Releases a service transport acquired by transportAcquire(), allowing
     * deferred connection teardown to proceed.
     */
    void transportRelease(void);

    /**
     * Returns the native backend callbacks for a new transfer.
     *
     * @returns VBox status code.
     * @param   pCallbacks          Where to return the callback table.
     */
    int transferGetCallbacks(PSHCLTRANSFERCALLBACKS pCallbacks);

    /**
     * Handles a transfer status reply in the native backend.
     *
     * @returns VBox status code.
     * @param   pTransfer           Transfer whose status changed.
     * @param   enmSource           Endpoint issuing the reply.
     * @param   enmStatus           New transfer status.
     * @param   rcStatus            Status-specific VBox status code.
     */
    int transferHandleStatusReply(PSHCLTRANSFER pTransfer, SHCLSOURCE enmSource,
                                  SHCLTRANSFERSTATUS enmStatus, int rcStatus);

    /**
     * Retains a service transfer selected by ID.
     *
     * @returns Retained transfer on success, or NULL if not found or disconnected.
     * @param   idTransfer          Transfer ID to look up.
     */
    PSHCLTRANSFER transferGetByIdRetained(SHCLTRANSFERID idTransfer);

    /**
     * Retains a service transfer selected by its complete generation key.
     *
     * @returns Retained transfer on success, or NULL if not found or disconnected.
     * @param   idSession           Service session ID.
     * @param   idTransfer          Transfer ID.
     * @param   uGeneration        Transfer generation.
     */
    PSHCLTRANSFER transferGetByKeyRetained(SHCLSESSIONID idSession, SHCLTRANSFERID idTransfer,
                                           SHCLTRANSFERGEN uGeneration);

    /**
     * Creates and retains a service-owned transfer.
     *
     * @returns VBox status code.
     * @param   enmDir              Transfer direction.
     * @param   enmSource           Transfer source.
     * @param   pCallbacks          Transfer callback table.
     * @param   idTransfer          Requested transfer ID.
     * @param   ppTransfer          Where to return the retained transfer.
     */
    int transferCreate(SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource, PSHCLTRANSFERCALLBACKS pCallbacks,
                       SHCLTRANSFERID idTransfer, PSHCLTRANSFER *ppTransfer);

    /**
     * Initializes a service-owned transfer.
     *
     * @returns VBox status code.
     * @param   pTransfer           Transfer to initialize.
     */
    int transferInit(PSHCLTRANSFER pTransfer);

    /**
     * Reports a host-side terminal transfer status through the service.
     *
     * @returns VBox status code.
     * @param   pTransfer           Transfer whose terminal state is being reported.
     * @param   enmStatus           Terminal transfer status.
     * @param   rcStatus            Status-specific result code.
     */
    int transferReportStatus(PSHCLTRANSFER pTransfer, SHCLTRANSFERSTATUS enmStatus, int rcStatus);

    /**
     * Destroys a service-owned transfer selected by ID.
     *
     * @param   idTransfer          Transfer ID to destroy.
     */
    void transferDestroyById(SHCLTRANSFERID idTransfer);

    /**
     * Destroys every transfer owned by the connected service client.
     *
     * This teardown operation remains available while disconnect is closing
     * the connection because transfer callbacks can retain backend state.
     */
    void transferDestroyAll(void);

    /**
     * Initializes a provider which reads transfer data from the guest.
     *
     * @returns VBox status code.
     * @param   pProvider           Provider to initialize.
     */
    int transferProviderInitGuest(PSHCLTXPROVIDER pProvider);

#endif

private:
    friend class GuestShCl;

    /** Internal connection lifecycle states. */
    enum State
    {
        /** No service client is associated with the object. */
        State_Disconnected = 0,
        /** The native backend is establishing its per-client context. */
        State_Connecting,
        /** The service client and native backend context are usable. */
        State_Connected,
        /** New calls are blocked while existing calls and backend workers drain. */
        State_Closing
    };

    /** No copy construction. */
    GuestShClConn(GuestShClConn const &rThat);
    /** No assignment. */
    GuestShClConn &operator=(GuestShClConn const &rThat);

    /**
     * Begins an operation using the current connection.
     *
     * @returns VBox status code.
     * @param   pTransport          Where to return the stable service endpoint.
     */
    int i_callBegin(PSHCLTRANSPORT pTransport);
    /**
     * Finishes an operation begun by i_callBegin().
     */
    void i_callEnd(void);
    /**
     * Waits for every operation begun by i_callBegin() to finish.
     */
    void i_waitForCalls(void);
    /**
     * Requests and waits for clipboard data from the guest without checking
     * which host clipboard provider is selected.
     *
     * @returns VBox status code.
     * @param   uFormat             Requested format, VBOX_SHCL_FMT_XXX.
     * @param   ppvData             Where to return the allocated data buffer.
     * @param   pcbData             Where to return the data size.
     */
    int i_readDataFromGuest(SHCLFORMAT uFormat, void **ppvData, uint32_t *pcbData);

    /** GuestShCl instance owning this object for the full object lifetime; immutable. */
    GuestShCl                  *m_pOwner;
    /** Serializes connection state and active-call accounting. */
    mutable RTCRITSECT          m_CritSect;
    /** Signalled while no service or backend calls are active. */
    RTSEMEVENTMULTI             m_hCallsDone;
    /** Current lifecycle state; protected by m_CritSect. */
    State                       m_enmState;
    /** Whether the process-wide native clipboard backend is initialized; protected by m_CritSect. */
    bool                        m_fBackendInitialized;
    /** Number of operations currently using m_Transport or m_Backend; protected by m_CritSect. */
    uint32_t                    m_cCalls;
    /** Non-owning endpoint for the service-owned HGCM client; protected by m_CritSect. */
    SHCLTRANSPORT               m_Transport;
    /** Native backend dispatcher and its platform connection context. */
    ShClBackend                 m_Backend;
};

#endif /* !MAIN_INCLUDED_GuestShClConn_h */
