/* $Id: TestExecServiceInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TestExecServ - Basic Remote Execution Service, Internal Header.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_TestExecServ_TestExecServiceInternal_h
#define VBOX_INCLUDED_SRC_TestExecServ_TestExecServiceInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/getopt.h>
#include <iprt/stream.h>

RT_C_DECLS_BEGIN

/**
 * Packet header.
 */
typedef struct TXSPKTHDR
{
    /** The unpadded packet length. This include this header. */
    uint32_t        cb;
    /** The CRC-32 for the packet starting from the opcode field.  0 if the packet
     *  hasn't been CRCed. */
    uint32_t        uCrc32;
    /** Packet opcode, an unterminated ASCII string.  */
    uint8_t         achOpcode[8];
} TXSPKTHDR;
AssertCompileSize(TXSPKTHDR, 16);
/** Pointer to a packet header. */
typedef TXSPKTHDR *PTXSPKTHDR;
/** Pointer to a packet header. */
typedef TXSPKTHDR const *PCTXSPKTHDR;
/** Pointer to a packet header pointer. */
typedef PTXSPKTHDR *PPTXSPKTHDR;

/** Packet alignment. */
#define TXSPKT_ALIGNMENT            16
/** Max packet size. */
#define TXSPKT_MAX_SIZE             _256K


/**
 * Transport layer descriptor.
 */
typedef struct TXSTRANSPORT
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
     * Handle an option.
     *
     * When encountering an options that is not part of the base options, we'll call
     * this method for each transport layer until one handles it.
     *
     * @retval  VINF_SUCCESS if handled.
     * @retval  VERR_TRY_AGAIN if not handled.
     * @retval  VERR_INVALID_PARAMETER if we should exit with a non-zero status.
     *
     * @param   ch                  The short option value.
     * @param   pVal                Pointer to the value union.
     *
     * @remarks This is only required if TXSTRANSPORT::cOpts is greater than 0.
     */
    DECLR3CALLBACKMEMBER(int, pfnOption,(int ch, PCRTGETOPTUNION pVal));

    /**
     * Initializes the transport layer.
     *
     * @returns IPRT status code.  On errors, the transport layer shall call
     *          RTMsgError to display the error details to the user.
     */
    DECLR3CALLBACKMEMBER(int, pfnInit,(void));

    /**
     * Terminate the transport layer, closing and freeing resources.
     *
     * On errors, the transport layer shall call RTMsgError to display the error
     * details to the user.
     */
    DECLR3CALLBACKMEMBER(void, pfnTerm,(void));

    /**
     * Polls for incoming packets.
     *
     * @returns true if there are pending packets, false if there isn't.
     */
    DECLR3CALLBACKMEMBER(bool, pfnPollIn,(void));

    /**
     * Adds any pollable handles to the poll set.
     *
     * This is optional and layers that doesn't have anything that can be polled
     * shall set this method pointer to NULL to indicate that pfnPollIn must be used
     * instead.
     *
     * @returns IPRT status code.
     * @param   hPollSet            The poll set to add them to.
     * @param   idStart             The handle ID to start at.
     */
    DECLR3CALLBACKMEMBER(int, pfnPollSetAdd,(RTPOLLSET hPollSet, uint32_t idStart));

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
     * @param   ppPktHdr            Where to return the pointer to the packet we've
     *                              read.  This is allocated from the heap using
     *                              RTMemAlloc (w/ TXSPKT_ALIGNMENT) and must be
     *                              free by calling RTMemFree.
     */
    DECLR3CALLBACKMEMBER(int, pfnRecvPkt,(PPTXSPKTHDR ppPktHdr));

    /**
     * Sends an outgoing packet.
     *
     * This will block until the data has been written.
     *
     * @returns IPRT status code.
     * @retval  VERR_INTERRUPTED if interrupted before anything was sent.
     *
     * @param   pPktHdr             The packet to send.  The size is given by
     *                              aligning the size in the header by
     *                              TXSPKT_ALIGNMENT.
     */
    DECLR3CALLBACKMEMBER(int, pfnSendPkt,(PCTXSPKTHDR pPktHdr));

    /**
     * Sends a babble packet and disconnects the client (if applicable).
     *
     * @param   pPktHdr             The packet to send.  The size is given by
     *                              aligning the size in the header by
     *                              TXSPKT_ALIGNMENT.
     * @param   cMsSendTimeout      The send timeout measured in milliseconds.
     */
    DECLR3CALLBACKMEMBER(void, pfnBabble,(PCTXSPKTHDR pPktHdr, RTMSINTERVAL cMsSendTimeout));

    /**
     * Notification about a client HOWDY.
     */
    DECLR3CALLBACKMEMBER(void, pfnNotifyHowdy,(void));

    /**
     * Notification about a client BYE.
     *
     * For connection oriented transport layers, it would be good to disconnect the
     * client at this point.
     */
    DECLR3CALLBACKMEMBER(void, pfnNotifyBye,(void));

    /**
     * Notification about a REBOOT or SHUTDOWN.
     *
     * For connection oriented transport layers, stop listening for and
     * accepting at this point.
     */
    DECLR3CALLBACKMEMBER(void, pfnNotifyReboot,(void));

    /** Non-zero end marker. */
    uint32_t u32EndMarker;
} TXSTRANSPORT;
/** Pointer to a const transport layer descriptor. */
typedef const struct TXSTRANSPORT *PCTXSTRANSPORT;


extern TXSTRANSPORT const g_TcpTransport;
extern TXSTRANSPORT const g_SerialTransport;
extern TXSTRANSPORT const g_FileSysTransport;
extern TXSTRANSPORT const g_GuestPropTransport;
extern TXSTRANSPORT const g_TestDevTransport;

extern uint32_t           g_cVerbose;

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_TestExecServ_TestExecServiceInternal_h */

