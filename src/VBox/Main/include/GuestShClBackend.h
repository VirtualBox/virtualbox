/* $Id: GuestShClBackend.h 115050 2026-08-17 15:20:35Z andreas.loeffler@oracle.com $ */
/** @file
 * Main Shared Clipboard - Native backend dispatcher.
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

#ifndef MAIN_INCLUDED_GuestShClBackend_h
#define MAIN_INCLUDED_GuestShClBackend_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/HostServices/VBoxClipboardExt.h>

class GuestShClConn;
struct SHCLBACKENDOPS;
/** Pointer to a native Shared Clipboard backend operation table. */
typedef struct SHCLBACKENDOPS const *PCSHCLBACKENDOPS;

/**
 * Dispatches Shared Clipboard operations to a native platform backend.
 *
 * The selected operation table is fixed when the object is constructed.
 * Platform implementations and their entry points remain private to VBoxC.
 */
class ShClBackend
{
public:
    /** Creates a dispatcher using the native backend selected for this host. */
    ShClBackend(void);

    /** Destroys the dispatcher.  The backend must be disconnected first. */
    ~ShClBackend(void);

    /**
     * Initializes the selected native backend.
     *
     * @returns VBox status code.
     */
    int init(void);

    /** Destroys the selected native backend. */
    void destroy(void);

    /**
     * Replaces the native backend callback table for test purposes.
     *
     * Does nothing when the selected backend does not implement callback
     * replacement.
     *
     * @param   pCallbacks          Callback table, or NULL for backend defaults.
     */
    void setCallbacks(PSHCLCALLBACKS pCallbacks);

    /**
     * Connects a Main service connection to the selected native backend.
     *
     * @returns VBox status code.
     * @param   pConn               Main connection owning the service endpoint.
     */
    int connect(GuestShClConn *pConn);

    /**
     * Disconnects the current native backend context.
     *
     * @returns VBox status code.
     */
    int disconnect(void);

    /**
     * Reports guest clipboard formats to the selected native backend.
     *
     * @returns VBox status code.
     * @param   fFormats            Guest formats, VBOX_SHCL_FMT_XXX.
     */
    int reportFormats(SHCLFORMATS fFormats);

    /**
     * Reads native clipboard data for the guest.
     *
     * @returns VBox status code.
     * @param   uFormat             Clipboard format to read.
     * @param   pvData              Destination buffer.
     * @param   cbData              Destination buffer size in bytes.
     * @param   pcbActual           Where to return the required or actual byte count.
     */
    int readData(SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual);

    /**
     * Writes guest clipboard data to the selected native backend.
     *
     * @returns VBox status code.
     * @param   uFormat             Clipboard format to write.
     * @param   pvData              Clipboard data to write.
     * @param   cbData              Clipboard data size in bytes.
     */
    int writeData(SHCLFORMAT uFormat, void *pvData, uint32_t cbData);

    /**
     * Synchronizes the selected native backend with the guest.
     *
     * @returns VBox status code.
     */
    int sync(void);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /**
     * Gets the selected native backend's callbacks for a new transfer.
     *
     * @param   pCallbacks          Where to return the callback table.
     */
    void transferGetCallbacks(PSHCLTRANSFERCALLBACKS pCallbacks);

    /**
     * Handles a transfer status reply in the selected native backend.
     *
     * @returns VBox status code.
     * @param   pTransfer           Transfer whose status changed.
     * @param   enmSource           Source issuing the reply.
     * @param   enmStatus           New transfer status.
     * @param   rcStatus            Status-specific VBox status code.
     */
    int transferHandleStatusReply(PSHCLTRANSFER pTransfer, SHCLSOURCE enmSource,
                                  SHCLTRANSFERSTATUS enmStatus, int rcStatus);
#endif

private:
    /** No copy construction. */
    ShClBackend(ShClBackend const &rThat);
    /** No assignment. */
    ShClBackend &operator=(ShClBackend const &rThat);

    /** Selected native backend operation table; immutable. */
    PCSHCLBACKENDOPS           m_pOps;
    /** Opaque connection context owned by the selected native backend. */
    PSHCLCONTEXT               m_pCtx;
};

#endif /* !MAIN_INCLUDED_GuestShClBackend_h */
