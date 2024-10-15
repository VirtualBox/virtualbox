/* $Id: AudioTestServiceClient.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * AudioTestServiceClient - Audio test execution server, Public Header.
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

#ifndef VBOX_INCLUDED_SRC_Audio_AudioTestServiceClient_h
#define VBOX_INCLUDED_SRC_Audio_AudioTestServiceClient_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "AudioTestServiceInternal.h"

/**
 * Structure for maintaining an ATS client.
 */
typedef struct ATSCLIENT
{
    /** Pointer to the selected transport layer. */
    PCATSTRANSPORT      pTransport;
    /** Pointer to the selected transport instance to use. */
    PATSTRANSPORTINST   pTransportInst;
    /** The opaque client instance. */
    PATSTRANSPORTCLIENT pTransportClient;
} ATSCLIENT;
/** Pointer to an ATS client. */
typedef ATSCLIENT *PATSCLIENT;

/** @name Creation / destruction.
 * @{ */
int AudioTestSvcClientCreate(PATSCLIENT pClient);
void AudioTestSvcClientDestroy(PATSCLIENT pClient);
/** @} */

/** @name Connection handling.
 * @{ */
int AudioTestSvcClientConnectEx(PATSCLIENT pClient, RTMSINTERVAL msTimeout);
int AudioTestSvcClientConnect(PATSCLIENT pClient);
int AudioTestSvcClientDisconnect(PATSCLIENT pClient);
/** @} */

/** @name Option handling.
 * @{ */
int AudioTestSvcClientHandleOption(PATSCLIENT pClient, int ch, PCRTGETOPTUNION pVal);
/** @} */

/** @name Test set handling.
 * @{ */
int AudioTestSvcClientTestSetBegin(PATSCLIENT pClient, const char *pszTag);
int AudioTestSvcClientTestSetEnd(PATSCLIENT pClient, const char *pszTag);
int AudioTestSvcClientTestSetDownload(PATSCLIENT pClient, const char *pszTag, const char *pszPathOutAbs);
/** @} */

/** @name Tone handling.
 * @{ */
int AudioTestSvcClientTonePlay(PATSCLIENT pClient, PAUDIOTESTTONEPARMS pToneParms);
int AudioTestSvcClientToneRecord(PATSCLIENT pClient, PAUDIOTESTTONEPARMS pToneParms);
/** @} */

#endif /* !VBOX_INCLUDED_SRC_Audio_AudioTestServiceClient_h */

