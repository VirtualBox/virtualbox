/* $Id: DBGCIoProvTcp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, TCP I/O provider.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include <iprt/tcp.h>
#include <iprt/assert.h>

#include "DBGCIoProvInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Debug console TCP connection data.
 */
typedef struct DBGCTCPCON
{
    /** The I/O callback table for the console. */
    DBGCIO      Io;
    /** The socket of the connection. */
    RTSOCKET    hSock;
    /** Connection status. */
    bool        fAlive;
} DBGCTCPCON;
/** Pointer to the instance data of the console TCP backend. */
typedef DBGCTCPCON *PDBGCTCPCON;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{DBGCIO,pfnDestroy}
 */
static DECLCALLBACK(void) dbgcIoProvTcpIoDestroy(PCDBGCIO pIo)
{
    PDBGCTCPCON pTcpCon = RT_FROM_MEMBER(pIo, DBGCTCPCON, Io);
    RTSocketRelease(pTcpCon->hSock);
    pTcpCon->fAlive =false;
    RTMemFree(pTcpCon);
}


/**
 * @interface_method_impl{DBGCIO,pfnInput}
 */
static DECLCALLBACK(bool) dbgcIoProvTcpIoInput(PCDBGCIO pIo, uint32_t cMillies)
{
    PDBGCTCPCON pTcpCon = RT_FROM_MEMBER(pIo, DBGCTCPCON, Io);
    if (!pTcpCon->fAlive)
        return false;
    int rc = RTTcpSelectOne(pTcpCon->hSock, cMillies);
    if (RT_FAILURE(rc) && rc != VERR_TIMEOUT)
        pTcpCon->fAlive = false;
    return rc != VERR_TIMEOUT;
}


/**
 * @interface_method_impl{DBGCIO,pfnRead}
 */
static DECLCALLBACK(int) dbgcIoProvTcpIoRead(PCDBGCIO pIo, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    PDBGCTCPCON pTcpCon = RT_FROM_MEMBER(pIo, DBGCTCPCON, Io);
    if (!pTcpCon->fAlive)
        return VERR_INVALID_HANDLE;
    int rc = RTTcpRead(pTcpCon->hSock, pvBuf, cbBuf, pcbRead);
    if (RT_SUCCESS(rc) && pcbRead != NULL && *pcbRead == 0)
        rc = VERR_NET_SHUTDOWN;
    if (RT_FAILURE(rc))
        pTcpCon->fAlive = false;
    return rc;
}


/**
 * @interface_method_impl{DBGCIO,pfnWrite}
 */
static DECLCALLBACK(int) dbgcIoProvTcpIoWrite(PCDBGCIO pIo, const void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    PDBGCTCPCON pTcpCon = RT_FROM_MEMBER(pIo, DBGCTCPCON, Io);
    if (!pTcpCon->fAlive)
        return VERR_INVALID_HANDLE;

    int rc = RTTcpWrite(pTcpCon->hSock, pvBuf, cbBuf);
    if (RT_FAILURE(rc))
        pTcpCon->fAlive = false;

    if (pcbWritten)
        *pcbWritten = cbBuf;

    return rc;
}


/**
 * @interface_method_impl{DBGCIO,pfnSetReady}
 */
static DECLCALLBACK(void) dbgcIoProvTcpIoSetReady(PCDBGCIO pIo, bool fReady)
{
    /* stub */
    NOREF(pIo);
    NOREF(fReady);
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnCreate}
 */
static DECLCALLBACK(int) dbgcIoProvTcpCreate(PDBGCIOPROV phDbgcIoProv, PCFGMNODE pCfg)
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

    /*
     * Create the server.
     */
    PRTTCPSERVER pServer;
    rc = RTTcpServerCreateEx(szAddress, u32Port, &pServer);
    if (RT_SUCCESS(rc))
    {
        LogFlow(("dbgcIoProvTcpCreate: Created server on port %d %s\n", u32Port, szAddress));
        *phDbgcIoProv = (DBGCIOPROV)pServer;
        return rc;
    }

    return rc;
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnDestroy}
 */
static DECLCALLBACK(void) dbgcIoProvTcpDestroy(DBGCIOPROV hDbgcIoProv)
{
    int rc = RTTcpServerDestroy((PRTTCPSERVER)hDbgcIoProv);
    AssertRC(rc);
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnWaitForConnect}
 */
static DECLCALLBACK(int) dbgcIoProvTcpWaitForConnect(DBGCIOPROV hDbgcIoProv, RTMSINTERVAL cMsTimeout, PCDBGCIO *ppDbgcIo)
{
    PRTTCPSERVER pTcpSrv = (PRTTCPSERVER)hDbgcIoProv;
    RT_NOREF(cMsTimeout);

    RTSOCKET hSockCon = NIL_RTSOCKET;
    int rc = RTTcpServerListen2(pTcpSrv, &hSockCon);
    if (RT_SUCCESS(rc))
    {
        PDBGCTCPCON pTcpCon = (PDBGCTCPCON)RTMemAllocZ(sizeof(*pTcpCon));
        if (RT_LIKELY(pTcpCon))
        {
            pTcpCon->Io.pfnDestroy  = dbgcIoProvTcpIoDestroy;
            pTcpCon->Io.pfnInput    = dbgcIoProvTcpIoInput;
            pTcpCon->Io.pfnRead     = dbgcIoProvTcpIoRead;
            pTcpCon->Io.pfnWrite    = dbgcIoProvTcpIoWrite;
            pTcpCon->Io.pfnPktBegin = NULL;
            pTcpCon->Io.pfnPktEnd   = NULL;
            pTcpCon->Io.pfnSetReady = dbgcIoProvTcpIoSetReady;
            pTcpCon->hSock          = hSockCon;
            pTcpCon->fAlive         = true;
            *ppDbgcIo = &pTcpCon->Io;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnWaitInterrupt}
 */
static DECLCALLBACK(int) dbgcIoProvTcpWaitInterrupt(DBGCIOPROV hDbgcIoProv)
{
    PRTTCPSERVER pTcpSrv = (PRTTCPSERVER)hDbgcIoProv;

    RT_NOREF(pTcpSrv);
    /** @todo */
    return VINF_SUCCESS;
}


/**
 * TCP I/O provider registration record.
 */
const DBGCIOPROVREG g_DbgcIoProvTcp =
{
    /** pszName */
    "tcp",
    /** pszDesc */
    "TCP I/O provider.",
    /** pfnCreate */
    dbgcIoProvTcpCreate,
    /** pfnDestroy */
    dbgcIoProvTcpDestroy,
    /** pfnWaitForConnect */
    dbgcIoProvTcpWaitForConnect,
    /** pfnWaitInterrupt */
    dbgcIoProvTcpWaitInterrupt
};

