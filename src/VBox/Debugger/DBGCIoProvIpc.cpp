/* $Id: DBGCIoProvIpc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, IPC I/O provider.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#include <iprt/localipc.h>
#include <iprt/mem.h>
#include <iprt/assert.h>

#include "DBGCIoProvInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Debug console IPC connection data.
 */
typedef struct DBGCIPCCON
{
    /** The I/O callback table for the console. */
    DBGCIO               Io;
    /** The socket of the connection. */
    RTLOCALIPCSESSION    hSession;
    /** Connection status. */
    bool                 fAlive;
} DBGCIPCCON;
/** Pointer to the instance data of the console IPC backend. */
typedef DBGCIPCCON *PDBGCIPCCON;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{DBGCIO,pfnDestroy}
 */
static DECLCALLBACK(void) dbgcIoProvIpcIoDestroy(PCDBGCIO pIo)
{
    PDBGCIPCCON pIpcCon = RT_FROM_MEMBER(pIo, DBGCIPCCON, Io);
    RTLocalIpcSessionClose(pIpcCon->hSession);
    pIpcCon->fAlive =false;
    RTMemFree(pIpcCon);
}


/**
 * @interface_method_impl{DBGCIO,pfnInput}
 */
static DECLCALLBACK(bool) dbgcIoProvIpcIoInput(PCDBGCIO pIo, uint32_t cMillies)
{
    PDBGCIPCCON pIpcCon = RT_FROM_MEMBER(pIo, DBGCIPCCON, Io);
    if (!pIpcCon->fAlive)
        return false;
    int rc = RTLocalIpcSessionWaitForData(pIpcCon->hSession, cMillies);
    if (RT_FAILURE(rc) && rc != VERR_TIMEOUT)
        pIpcCon->fAlive = false;
    return rc != VERR_TIMEOUT;
}


/**
 * @interface_method_impl{DBGCIO,pfnRead}
 */
static DECLCALLBACK(int) dbgcIoProvIpcIoRead(PCDBGCIO pIo, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    PDBGCIPCCON pIpcCon = RT_FROM_MEMBER(pIo, DBGCIPCCON, Io);
    if (!pIpcCon->fAlive)
        return VERR_INVALID_HANDLE;
    int rc = RTLocalIpcSessionRead(pIpcCon->hSession, pvBuf, cbBuf, pcbRead);
    if (RT_SUCCESS(rc) && pcbRead != NULL && *pcbRead == 0)
        rc = VERR_NET_SHUTDOWN;
    if (RT_FAILURE(rc))
        pIpcCon->fAlive = false;
    return rc;
}


/**
 * @interface_method_impl{DBGCIO,pfnWrite}
 */
static DECLCALLBACK(int) dbgcIoProvIpcIoWrite(PCDBGCIO pIo, const void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    PDBGCIPCCON pIpcCon = RT_FROM_MEMBER(pIo, DBGCIPCCON, Io);
    if (!pIpcCon->fAlive)
        return VERR_INVALID_HANDLE;

    int rc = RTLocalIpcSessionWrite(pIpcCon->hSession, pvBuf, cbBuf);
    if (RT_FAILURE(rc))
        pIpcCon->fAlive = false;

    if (pcbWritten)
        *pcbWritten = cbBuf;

    return rc;
}


/**
 * @interface_method_impl{DBGCIO,pfnSetReady}
 */
static DECLCALLBACK(void) dbgcIoProvIpcIoSetReady(PCDBGCIO pIo, bool fReady)
{
    /* stub */
    NOREF(pIo);
    NOREF(fReady);
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnCreate}
 */
static DECLCALLBACK(int) dbgcIoProvIpcCreate(PDBGCIOPROV phDbgcIoProv, PCFGMNODE pCfg)
{
    /*
     * Get the address configuration.
     */
    char szAddress[512];
    int rc = CFGMR3QueryStringDef(pCfg, "Address", szAddress, sizeof(szAddress), "");
    if (RT_FAILURE(rc))
    {
        LogRel(("Configuration error: Failed querying \"Address\" -> rc=%Rc\n", rc));
        return rc;
    }

    /*
     * Create the server.
     */
    RTLOCALIPCSERVER hIpcSrv;
    rc = RTLocalIpcServerCreate(&hIpcSrv, szAddress, RTLOCALIPC_FLAGS_NATIVE_NAME);
    if (RT_SUCCESS(rc))
    {
        LogFlow(("dbgcIoProvIpcCreate: Created server on \"%s\"\n", szAddress));
        *phDbgcIoProv = (DBGCIOPROV)hIpcSrv;
        return rc;
    }

    return rc;
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnDestroy}
 */
static DECLCALLBACK(void) dbgcIoProvIpcDestroy(DBGCIOPROV hDbgcIoProv)
{
    int rc = RTLocalIpcServerDestroy((RTLOCALIPCSERVER)hDbgcIoProv);
    AssertRC(rc);
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnWaitForConnect}
 */
static DECLCALLBACK(int) dbgcIoProvIpcWaitForConnect(DBGCIOPROV hDbgcIoProv, RTMSINTERVAL cMsTimeout, PCDBGCIO *ppDbgcIo)
{
    RTLOCALIPCSERVER hIpcSrv = (RTLOCALIPCSERVER)hDbgcIoProv;
    RT_NOREF(cMsTimeout);

    RTLOCALIPCSESSION hSession = NIL_RTLOCALIPCSESSION;
    int rc = RTLocalIpcServerListen(hIpcSrv, &hSession);
    if (RT_SUCCESS(rc))
    {
        PDBGCIPCCON pIpcCon = (PDBGCIPCCON)RTMemAllocZ(sizeof(*pIpcCon));
        if (RT_LIKELY(pIpcCon))
        {
            pIpcCon->Io.pfnDestroy  = dbgcIoProvIpcIoDestroy;
            pIpcCon->Io.pfnInput    = dbgcIoProvIpcIoInput;
            pIpcCon->Io.pfnRead     = dbgcIoProvIpcIoRead;
            pIpcCon->Io.pfnWrite    = dbgcIoProvIpcIoWrite;
            pIpcCon->Io.pfnPktBegin = NULL;
            pIpcCon->Io.pfnPktEnd   = NULL;
            pIpcCon->Io.pfnSetReady = dbgcIoProvIpcIoSetReady;
            pIpcCon->hSession       = hSession;
            pIpcCon->fAlive         = true;
            *ppDbgcIo = &pIpcCon->Io;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * @interface_method_impl{DBGCIOPROVREG,pfnWaitInterrupt}
 */
static DECLCALLBACK(int) dbgcIoProvIpcWaitInterrupt(DBGCIOPROV hDbgcIoProv)
{
    return RTLocalIpcServerCancel((RTLOCALIPCSERVER)hDbgcIoProv);
}


/**
 * TCP I/O provider registration record.
 */
const DBGCIOPROVREG g_DbgcIoProvIpc =
{
    /** pszName */
    "ipc",
    /** pszDesc */
    "IPC I/O provider.",
    /** pfnCreate */
    dbgcIoProvIpcCreate,
    /** pfnDestroy */
    dbgcIoProvIpcDestroy,
    /** pfnWaitForConnect */
    dbgcIoProvIpcWaitForConnect,
    /** pfnWaitInterrupt */
    dbgcIoProvIpcWaitInterrupt
};

