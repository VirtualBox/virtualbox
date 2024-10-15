/* $Id: AudioTestServiceInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * AudioTestService - Audio test execution server, Internal Header.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Audio_AudioTestServiceInternal_h
#define VBOX_INCLUDED_SRC_Audio_AudioTestServiceInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/getopt.h>
#include <iprt/stream.h>

#include "AudioTestServiceProtocol.h"

RT_C_DECLS_BEGIN

/** Opaque ATS transport layer specific client data. */
typedef struct ATSTRANSPORTCLIENT *PATSTRANSPORTCLIENT;
typedef PATSTRANSPORTCLIENT *PPATSTRANSPORTCLIENT;

/** Opaque ATS transport specific instance data. */
typedef struct ATSTRANSPORTINST *PATSTRANSPORTINST;
typedef PATSTRANSPORTINST *PPATSTRANSPORTINST;

/**
 * Transport layer descriptor.
 */
typedef struct ATSTRANSPORT
{
    /** The name. */
    char            szName[16];
    /** The description. */
    const char     *pszDesc;
    /** Pointer to an array of options. */
    PCRTGETOPTDEF   paOpts;
    /** The number of options in the array. */
    size_t          cOpts;

    /**
     * Print the usage information for this transport layer.
     *
     * @param   pStream             The stream to print the usage info to.
     *
     * @remarks This is only required if TXSTRANSPORT::cOpts is greater than 0.
     */
    DECLR3CALLBACKMEMBER(void, pfnUsage,(PRTSTREAM pStream));

    /**
     * Creates a transport instance.
     *
     * @returns IPRT status code.  On errors, the transport layer shall call
     *          RTMsgError to display the error details to the user.
     * @param   ppThis              Where to return the created transport instance on success.
     */
    DECLR3CALLBACKMEMBER(int, pfnCreate, (PPATSTRANSPORTINST ppThis));

    /**
     * Destroys a transport instance.
     *
     * On errors, the transport layer shall call RTMsgError to display the error
     * details to the user.
     *
     * @returns IPRT status code.  On errors, the transport layer shall call
     *          RTMsgError to display the error details to the user.
     * @param   pThis               The transport instance.
     *                              The pointer will be invalid on return.
     */
    DECLR3CALLBACKMEMBER(int, pfnDestroy, (PATSTRANSPORTINST pThis));

    /**
     * Handle an option.
     *
     * When encountering an options that is not part of the base options, we'll call
     * this method for each transport layer until one handles it.
     *
     * @retval  VINF_SUCCESS if handled.
     * @retval  VERR_TRY_AGAIN if not handled.
     * @retval  VERR_INVALID_PARAMETER if we should exit with a non-zero status.
     *
     * @param   pThis               Transport instance to set options for.
     * @param   ch                  The short option value.
     * @param   pVal                Pointer to the value union.
     *
     * @remarks This is only required if TXSTRANSPORT::cOpts is greater than 0.
     */
    DECLR3CALLBACKMEMBER(int, pfnOption,(PATSTRANSPORTINST pThis, int ch, PCRTGETOPTUNION pVal));

    /**
     * Starts a transport instance.
     *
     * @returns IPRT status code.  On errors, the transport layer shall call
     *          RTMsgError to display the error details to the user.
     * @param   pThis               Transport instance to initialize.
     */
    DECLR3CALLBACKMEMBER(int, pfnStart, (PATSTRANSPORTINST pThis));

    /**
     * Stops a transport instance, closing and freeing resources.
     *
     * On errors, the transport layer shall call RTMsgError to display the error
     * details to the user.
     *
     * @param   pThis               The transport instance.
     */
    DECLR3CALLBACKMEMBER(void, pfnStop, (PATSTRANSPORTINST pThis));

    /**
     * Waits for a new client to connect and returns the client specific data on
     * success.
     *
     * @returns VBox status code.
     * @param   pThis               The transport instance.
     * @param   msTimeout           Timeout (in ms) waiting for a connection to be established.
     *                              Use RT_INDEFINITE_WAIT to wait indefinitely.
     *                              This might or might not be supported by the specific transport implementation.
     * @param   pfFromServer        Returns \c true if the returned client is from a remote server (called a reverse connection),
     *                              or \c false if not (regular client). Optional and can be NULL.
     * @param   ppClientNew         Where to return the allocated client on success.
     *                              Must be destroyed with pfnDisconnect() when done.
     */
    DECLR3CALLBACKMEMBER(int, pfnWaitForConnect, (PATSTRANSPORTINST pThis, RTMSINTERVAL msTimeout, bool *pfFromServer, PPATSTRANSPORTCLIENT ppClientNew));

    /**
     * Disconnects a client and frees up its resources.
     *
     * @param   pThis               The transport instance.
     * @param   pClient             Client to disconnect.
     *                              The pointer will be invalid after calling.
     */
    DECLR3CALLBACKMEMBER(void, pfnDisconnect, (PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient));

    /**
     * Polls for incoming packets.
     *
     * @returns true if there are pending packets, false if there isn't.
     * @param   pThis               The transport instance.
     * @param   pClient             The client to poll for data.
     */
    DECLR3CALLBACKMEMBER(bool, pfnPollIn, (PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient));

    /**
     * Adds any pollable handles to the poll set.
     *
     * @returns IPRT status code.
     * @param   pThis               The transport instance.
     * @param   hPollSet            The poll set to add them to.
     * @param   pClient             The transport client structure.
     * @param   idStart             The handle ID to start at.
     */
    DECLR3CALLBACKMEMBER(int, pfnPollSetAdd, (PATSTRANSPORTINST pThis, RTPOLLSET hPollSet, PATSTRANSPORTCLIENT pClient, uint32_t idStart));

    /**
     * Removes the given client frmo the given pollset.
     *
     * @returns IPRT status code.
     * @param   pThis               The transport instance.
     * @param   hPollSet            The poll set to remove from.
     * @param   pClient             The transport client structure.
     * @param   idStart             The handle ID to remove.
     */
    DECLR3CALLBACKMEMBER(int, pfnPollSetRemove, (PATSTRANSPORTINST pThis, RTPOLLSET hPollSet, PATSTRANSPORTCLIENT pClient, uint32_t idStart));

    /**
     * Receives an incoming packet.
     *
     * This will block until the data becomes available or we're interrupted by a
     * signal or something.
     *
     * @returns IPRT status code.  On error conditions other than VERR_INTERRUPTED,
     *          the current operation will be aborted when applicable.  When
     *          interrupted, the transport layer will store the data until the next
     *          receive call.
     *
     * @param   pThis               The transport instance.
     * @param   pClient             The transport client structure.
     * @param   ppPktHdr            Where to return the pointer to the packet we've
     *                              read.  This is allocated from the heap using
     *                              RTMemAlloc (w/ ATSPKT_ALIGNMENT) and must be
     *                              free by calling RTMemFree.
     */
    DECLR3CALLBACKMEMBER(int, pfnRecvPkt, (PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient, PPATSPKTHDR ppPktHdr));

    /**
     * Sends an outgoing packet.
     *
     * This will block until the data has been written.
     *
     * @returns IPRT status code.
     * @retval  VERR_INTERRUPTED if interrupted before anything was sent.
     *
     * @param   pThis               The transport instance.
     * @param   pClient             The transport client structure.
     * @param   pPktHdr             The packet to send.  The size is given by
     *                              aligning the size in the header by
     *                              ATSPKT_ALIGNMENT.
     */
    DECLR3CALLBACKMEMBER(int, pfnSendPkt, (PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient, PCATSPKTHDR pPktHdr));

    /**
     * Sends a babble packet and disconnects the client (if applicable).
     *
     * @param   pThis               The transport instance.
     * @param   pClient             The transport client structure.
     * @param   pPktHdr             The packet to send.  The size is given by
     *                              aligning the size in the header by
     *                              ATSPKT_ALIGNMENT.
     * @param   cMsSendTimeout      The send timeout measured in milliseconds.
     */
    DECLR3CALLBACKMEMBER(void, pfnBabble, (PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient, PCATSPKTHDR pPktHdr, RTMSINTERVAL cMsSendTimeout));

    /**
     * Notification about a client HOWDY.
     *
     * @param   pThis               The transport instance.
     * @param   pClient             The transport client structure.
     */
    DECLR3CALLBACKMEMBER(void, pfnNotifyHowdy, (PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient));

    /**
     * Notification about a client BYE.
     *
     * For connection oriented transport layers, it would be good to disconnect the
     * client at this point.
     *
     * @param   pThis               The transport instance.
     * @param   pClient             The transport client structure.
     */
    DECLR3CALLBACKMEMBER(void, pfnNotifyBye, (PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient));

    /**
     * Notification about a REBOOT or SHUTDOWN.
     *
     * For connection oriented transport layers, stop listening for and
     * accepting at this point.
     *
     * @param   pThis               The transport instance.
     */
    DECLR3CALLBACKMEMBER(void, pfnNotifyReboot, (PATSTRANSPORTINST pThis));

    /** Non-zero end marker. */
    uint32_t u32EndMarker;
} ATSTRANSPORT;
/** Pointer to a const transport layer descriptor. */
typedef const struct ATSTRANSPORT *PCATSTRANSPORT;


extern ATSTRANSPORT const g_TcpTransport;

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Audio_AudioTestServiceInternal_h */

