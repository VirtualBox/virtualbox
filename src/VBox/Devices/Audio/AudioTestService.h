/* $Id: AudioTestService.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * AudioTestService - Audio test execution server, Public Header.
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

#ifndef VBOX_INCLUDED_SRC_Audio_AudioTestService_h
#define VBOX_INCLUDED_SRC_Audio_AudioTestService_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/tcp.h>

#include "AudioTestServiceInternal.h"

extern const PCATSTRANSPORT g_apTransports[];
extern const size_t         g_cTransports;

/** Default TCP/IP bind port the guest ATS (Audio Test Service) is listening on. */
#define ATS_TCP_DEF_BIND_PORT_GUEST                6042
/** Default TCP/IP bind port the host ATS is listening on. */
#define ATS_TCP_DEF_BIND_PORT_HOST                 6052
/** Default TCP/IP ATS bind port the ValidationKit Audio Driver ATS is listening on. */
#define ATS_TCP_DEF_BIND_PORT_VALKIT               6062
/** Default TCP/IP port the guest ATS is connecting to. */
#define ATS_TCP_DEF_CONNECT_PORT_GUEST              ATS_TCP_DEF_BIND_PORT_HOST
/** Default TCP/IP port the host ATS is connecting to the guest (needs NAT port forwarding). */
#define ATS_TCP_DEF_CONNECT_PORT_HOST_PORT_FWD     6072
/** Default TCP/IP port the host ATS is connecting to. */
#define ATS_TCP_DEF_CONNECT_PORT_VALKIT            ATS_TCP_DEF_BIND_PORT_VALKIT
/** Default TCP/IP address the host is connecting to. */
#define ATS_TCP_DEF_CONNECT_HOST_ADDR_STR          "127.0.0.1"
/** Default TCP/IP address the guest ATS connects to when
 *  running in client mode (reversed mode, needed for NATed VMs). */
#define ATS_TCP_DEF_CONNECT_GUEST_STR              "10.0.2.2"

/**
 * Structure for keeping an Audio Test Service (ATS) callback table.
 */
typedef struct ATSCALLBACKS
{
    /**
     * Tells the implementation that a new client connected. Optional.
     *
     * @param   pvUser          User-supplied pointer to context data. Optional.
     */
    DECLR3CALLBACKMEMBER(int, pfnHowdy, (void const *pvUser));

    /**
     * Tells the implementation that a client disconnected. Optional.
     *
     * @param   pvUser          User-supplied pointer to context data. Optional.
     */
    DECLR3CALLBACKMEMBER(int, pfnBye, (void const *pvUser));

    /**
     * Begins a test set. Optional.
     *
     * @returns VBox status code.
     * @param   pvUser          User-supplied pointer to context data. Optional.
     * @param   pszTag          Tag of test set to begin.
     */
    DECLR3CALLBACKMEMBER(int, pfnTestSetBegin, (void const *pvUser, const char *pszTag));

    /**
     * Ends the current test set. Optional.
     *
     * @returns VBox status code.
     * @param   pvUser          User-supplied pointer to context data. Optional.
     * @param   pszTag          Tag of test set to end.
     */
    DECLR3CALLBACKMEMBER(int, pfnTestSetEnd,   (void const *pvUser, const char *pszTag));

    /**
     * Marks the begin of sending a test set. Optional.
     *
     * @returns VBox status code.
     * @param   pvUser          User-supplied pointer to context data. Optional.
     * @param   pszTag          Tag of test set to begin sending.
     */
    DECLR3CALLBACKMEMBER(int, pfnTestSetSendBegin, (void const *pvUser, const char *pszTag));

    /**
     * Reads data from a test set for sending it.
     *
     * @returns VBox status code.
     * @param   pvUser          User-supplied pointer to context data. Optional.
     * @param   pszTag          Tag of test set to begin sending.
     * @param   pvBuf           Where to store the read test set data.
     * @param   cbBuf           Size of \a pvBuf (in bytes).
     * @param   pcbRead         Where to return the amount of read data in bytes. Optional and can be NULL.
     */
    DECLR3CALLBACKMEMBER(int, pfnTestSetSendRead,  (void const *pvUser, const char *pszTag, void *pvBuf, size_t cbBuf, size_t *pcbRead));

    /**
     * Marks the end of sending a test set. Optional.
     *
     * @returns VBox status code.
     * @param   pvUser          User-supplied pointer to context data. Optional.
     * @param   pszTag          Tag of test set to end sending.
     */
    DECLR3CALLBACKMEMBER(int, pfnTestSetSendEnd,   (void const *pvUser, const char *pszTag));

    /**
     * Plays a test tone.
     *
     * @returns VBox status code.
     * @param   pvUser          User-supplied pointer to context data. Optional.
     * @param   pToneParms      Tone parameters to use for playback.
     */
    DECLR3CALLBACKMEMBER(int, pfnTonePlay, (void const *pvUser, PAUDIOTESTTONEPARMS pToneParms));

    /**
     * Records a test tone.
     *
     * @returns VBox status code.
     * @param   pvUser          User-supplied pointer to context data. Optional.
     * @param   pToneParms      Tone parameters to use for recording.
     */
    DECLR3CALLBACKMEMBER(int, pfnToneRecord, (void const *pvUser, PAUDIOTESTTONEPARMS pToneParms));

    /** Pointer to opaque user-provided context data. */
    void const *pvUser;
} ATSCALLBACKS;
/** Pointer to a const ATS callbacks table. */
typedef const struct ATSCALLBACKS *PCATSCALLBACKS;

/**
 * Structure for keeping an Audio Test Service (ATS) instance.
 */
typedef struct ATSSERVER
{
    /** Pointer to the selected transport layer. */
    PCATSTRANSPORT       pTransport;
    /** Pointer to the transport instance. */
    PATSTRANSPORTINST    pTransportInst;
    /** The callbacks table. */
    ATSCALLBACKS         Callbacks;
    /** Whether server is in started state or not. */
    bool volatile        fStarted;
    /** Whether to terminate or not. */
    bool volatile        fTerminate;
    /** The main thread's poll set to handle new clients. */
    RTPOLLSET            hPollSet;
    /** Pipe for communicating with the serving thread about new clients. - read end */
    RTPIPE               hPipeR;
    /** Pipe for communicating with the serving thread about new clients. - write end */
    RTPIPE               hPipeW;
    /** Main thread waiting for connections. */
    RTTHREAD             hThreadMain;
    /** Thread serving connected clients. */
    RTTHREAD             hThreadServing;
    /** Critical section protecting the list of new clients. */
    RTCRITSECT           CritSectClients;
    /** List of new clients waiting to be picked up by the client worker thread. */
    RTLISTANCHOR         LstClientsNew;
} ATSSERVER;
/** Pointer to an Audio Test Service (ATS) instance. */
typedef ATSSERVER *PATSSERVER;

int AudioTestSvcInit(PATSSERVER pThis, PCATSCALLBACKS pCallbacks);
int AudioTestSvcDestroy(PATSSERVER pThis);
int AudioTestSvcHandleOption(PATSSERVER pThis, int ch, PCRTGETOPTUNION pVal);
int AudioTestSvcStart(PATSSERVER pThis);
int AudioTestSvcStop(PATSSERVER pThis);

/**
 * Enumeration for the server connection mode.
 * Only applies to certain transport implementation like TCP/IP.
 */
typedef enum ATSCONNMODE
{
    /** Both: Uses parallel client and server connection methods (via threads). */
    ATSCONNMODE_BOTH = 0,
    /** Client only: Connects to a server. */
    ATSCONNMODE_CLIENT,
    /** Server only: Listens for new incoming client connections. */
    ATSCONNMODE_SERVER,
    /** 32bit hack. */
    ATSCONNMODE_32BIT_HACK = 0x7fffffff
} ATSCONNMODE;

/** TCP/IP options for the ATS server.
 *  @todo Make this more abstract later. */
enum ATSTCPOPT
{
    ATSTCPOPT_CONN_MODE = 5000,
    ATSTCPOPT_BIND_ADDRESS,
    ATSTCPOPT_BIND_PORT,
    ATSTCPOPT_CONNECT_ADDRESS,
    ATSTCPOPT_CONNECT_PORT
};

#endif /* !VBOX_INCLUDED_SRC_Audio_AudioTestService_h */

