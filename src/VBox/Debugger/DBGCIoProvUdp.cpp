/* $Id: DBGCIoProvUdp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, UDP I/O provider.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/dbg.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/mem.h>
#include <iprt/udp.h>
#include <iprt/assert.h>

#include "DBGCIoProvInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Debug console UDP connection data.
 */
typedef struct DBGCUDPSRV
{
    /** The I/O callback table for the console. */
    DBGCIO      Io;
    /** The socket of the connection. */
    RTSOCKET    hSock;
    /** The address of the peer. */
    RTNETADDR   NetAddrPeer;
    /** Flag whether the peer address was set. */
    bool        fPeerSet;
    /** Connection status. */
    bool        fAlive;
} DBGCUDPSRV;
/** Pointer to the instance data of the console UDP backend. */
typedef DBGCUDPSRV *PDBGCUDPSRV;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{DBGCIO,pfnDestroy}
 */
static DECLCALLBACK(void) dbgcIoProvUdpIoDestroy(PCDBGCIO pIo)
{
    RT_NOREF(pIo);
}


/**
 * @interface_method_impl{DBGCIO,pfnInput}
 */
static DECLCALLBACK(bool) dbgcIoProvUdpIoInput(PCDBGCIO pIo, uint32_t cMillies)
{
    PDBGCUDPSRV pUdpSrv = RT_FROM_MEMBER(pIo, DBGCUDPSRV, Io);
    if (!pUdpSrv->fAlive)
        return false;
    int rc = RTSocketSelectOne(pUdpSrv->hSock, cMillies);
    if (RT_FAILURE(rc) && rc != VERR_TIMEOUT)
        pUdpSrv->fAlive = false;
    return rc != VERR_TIMEOUT;
}


/**
 * @interface_method_impl{DBGCIO,pfnRead}
 */
static DECLCALLBACK(int) dbgcIoProvUdpIoRead(PCDBGCIO pIo, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    PDBGCUDPSRV pUdpSrv = RT_FROM_MEMBER(pIo, DBGCUDPSRV, Io);
    if (!pUdpSrv->fAlive)
        return VERR_INVALID_HANDLE;
    int rc = RTSocketReadFrom(pUdpSrv->hSock, pvBuf, cbBuf, pcbRead, &pUdpSrv->NetAddrPeer);
    if (RT_SUCCESS(rc) && pcbRead != NULL && *pcbRead == 0)
        rc = VERR_NET_SHUTDOWN;
    if (RT_FAILURE(rc))
        pUdpSrv->fAlive = false;
    pUdpSrv->fPeerSet = true;
    return rc;
}


/**
 * @interface_method_impl{DBGCIO,pfnWrite}
 */
static DECLCALLBACK(int) dbgcIoProvUdpIoWrite(PCDBGCIO pIo, const void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    PDBGCUDPSRV pUdpSrv = RT_FROM_MEMBER(pIo, DBGCUDPSRV, Io);
    if (   !pUdpSrv->fAlive
        || !pUdpSrv->fPeerSet)
        return VERR_INVALID_HANDLE;

    int rc = RTSocketWriteTo(pUdpSrv->hSock, pvBuf, cbBuf, &pUdpSrv->NetAddrPeer);
    if (RT_FAILURE(rc))
        pUdpSrv->fAlive = false;

    if (pcbWritten)
        *pcbWritten = cbBuf;

    return rc;
}


/**
 * @interface_method_impl{DBGCIO,pfnSetReady}
 */
static DECLCALLBACK(void) dbgcIoProvUdpIoSetReady(PCDBGCIO pIo, bool fReady)
{
    /* stub */
    NOREF(pIo);
    NOREF(fReady);
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnCreate}
 */
static DECLCALLBACK(int) dbgcIoProvUdpCreate(PDBGCIOPROV phDbgcIoProv, PCFGMNODE pCfg)
{
    /*
     * Get the port configuration.
     */
    uint32_t u32Port;
    int rc = CFGMR3QueryU32Def(pCfg, "Port", &u32Port, 5000);
    if (RT_FAILURE(rc))
    {
        LogRel(("Configuration error: Failed querying \"Port\" -> rc=%Rc\n", rc));
        return rc;
    }

    /*
     * Get the address configuration.
     */
    char szAddress[512];
    rc = CFGMR3QueryStringDef(pCfg, "Address", szAddress, sizeof(szAddress), "");
    if (RT_FAILURE(rc))
    {
        LogRel(("Configuration error: Failed querying \"Address\" -> rc=%Rc\n", rc));
        return rc;
    }

    PDBGCUDPSRV pUdpSrv = (PDBGCUDPSRV)RTMemAllocZ(sizeof(*pUdpSrv));
    if (RT_LIKELY(pUdpSrv))
    {
        pUdpSrv->Io.pfnDestroy  = dbgcIoProvUdpIoDestroy;
        pUdpSrv->Io.pfnInput    = dbgcIoProvUdpIoInput;
        pUdpSrv->Io.pfnRead     = dbgcIoProvUdpIoRead;
        pUdpSrv->Io.pfnWrite    = dbgcIoProvUdpIoWrite;
        pUdpSrv->Io.pfnPktBegin = NULL;
        pUdpSrv->Io.pfnPktEnd   = NULL;
        pUdpSrv->Io.pfnSetReady = dbgcIoProvUdpIoSetReady;
        pUdpSrv->fPeerSet       = false;
        pUdpSrv->fAlive         = true;

        /*
         * Create the server.
         */
        rc = RTUdpCreateServerSocket(szAddress, u32Port, &pUdpSrv->hSock);
        if (RT_SUCCESS(rc))
        {
            LogFlow(("dbgcIoProvUdpCreate: Created server on port %d %s\n", u32Port, szAddress));
            *phDbgcIoProv = (DBGCIOPROV)pUdpSrv;
            return rc;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnDestroy}
 */
static DECLCALLBACK(void) dbgcIoProvUdpDestroy(DBGCIOPROV hDbgcIoProv)
{
    PDBGCUDPSRV pUdpSrv = (PDBGCUDPSRV)hDbgcIoProv;

    RTSocketRelease(pUdpSrv->hSock);
    pUdpSrv->fAlive = false;
    RTMemFree(pUdpSrv);
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnWaitForConnect}
 */
static DECLCALLBACK(int) dbgcIoProvUdpWaitForConnect(DBGCIOPROV hDbgcIoProv, RTMSINTERVAL cMsTimeout, PCDBGCIO *ppDbgcIo)
{
    PDBGCUDPSRV pUdpSrv = (PDBGCUDPSRV)hDbgcIoProv;

    /* Wait for the first datagram. */
    int rc = RTSocketSelectOne(pUdpSrv->hSock, cMsTimeout);
    if (RT_SUCCESS(rc))
        *ppDbgcIo = &pUdpSrv->Io;
    return rc;
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnWaitInterrupt}
 */
static DECLCALLBACK(int) dbgcIoProvUdpWaitInterrupt(DBGCIOPROV hDbgcIoProv)
{
    RT_NOREF(hDbgcIoProv);
    /** @todo */
    return VINF_SUCCESS;
}


/**
 * UDP I/O provider registration record.
 */
const DBGCIOPROVREG g_DbgcIoProvUdp =
{
    /** pszName */
    "udp",
    /** pszDesc */
    "UDP I/O provider.",
    /** pfnCreate */
    dbgcIoProvUdpCreate,
    /** pfnDestroy */
    dbgcIoProvUdpDestroy,
    /** pfnWaitForConnect */
    dbgcIoProvUdpWaitForConnect,
    /** pfnWaitInterrupt */
    dbgcIoProvUdpWaitInterrupt
};

