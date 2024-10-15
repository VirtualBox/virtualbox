/* $Id: DBGCIoProvInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, Internal I/O provider header file.
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

#ifndef DEBUGGER_INCLUDED_SRC_DBGCIoProvInternal_h
#define DEBUGGER_INCLUDED_SRC_DBGCIoProvInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/dbg.h>
#include <VBox/err.h>
#include <VBox/vmm/cfgm.h>


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/

/** An Opaque I/O provider handle. */
typedef struct DBGCIOPROVINT *DBGCIOPROV;
/** Pointer to an opaque I/O provider handle. */
typedef DBGCIOPROV *PDBGCIOPROV;


/**
 * I/O provider registration record.
 */
typedef struct DBGCIOPROVREG
{
    /** Unique name for the I/O provider. */
    const char                  *pszName;
    /** I/O provider description. */
    const char                  *pszDesc;

    /**
     * Creates an I/O provider instance from the given config.
     *
     * @returns VBox status code.
     * @param   phDbgcIoProv    Where to store the handle to the I/O provider instance on success.
     * @param   pCfg            The config to use.
     */
    DECLCALLBACKMEMBER(int, pfnCreate, (PDBGCIOPROV phDbgcIoProv, PCFGMNODE pCfg));

    /**
     * Destroys the given I/O provider instance.
     *
     * @param   hDbgcIoProv     The I/O provider instance handle to destroy.
     */
    DECLCALLBACKMEMBER(void, pfnDestroy, (DBGCIOPROV hDbgcIoProv));

    /**
     * Waits for someone to connect to the provider instance.
     *
     * @returns VBox status code.
     * @retval  VERR_TIMEOUT if the waiting time was exceeded without anyone connecting.
     * @retval  VERR_INTERRUPTED if the waiting was interrupted by DBGCIOPROVREG::pfnWaitInterrupt.
     * @param   hDbgcIoProv     The I/O provider instance handle.
     * @param   cMsTimeout      Number of milliseconds to wait, use RT_INDEFINITE_WAIT to wait indefinitely.
     * @param   ppDbgcIo        Where to return the I/O connection callback table upon a succesful return.
     */
    DECLCALLBACKMEMBER(int, pfnWaitForConnect, (DBGCIOPROV hDbgcIoProv, RTMSINTERVAL cMsTimeout, PCDBGCIO *ppDbgcIo));

    /**
     * Interrupts the thread waiting in DBGCIOPROVREG::pfnWaitForConnect.
     *
     * @returns VBox status code.
     * @param   hDbgcIoProv     The I/O provider instance handle.
     */
    DECLCALLBACKMEMBER(int, pfnWaitInterrupt, (DBGCIOPROV hDbgcIoProv));

} DBGCIOPROVREG;
/** Pointer to an I/O provider registration record. */
typedef DBGCIOPROVREG *PDBGCIOPROVREG;
/** Pointer toa const I/O provider registration record. */
typedef const DBGCIOPROVREG *PCDBGCIOPROVREG;


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
extern const DBGCIOPROVREG    g_DbgcIoProvTcp;
extern const DBGCIOPROVREG    g_DbgcIoProvUdp;
extern const DBGCIOPROVREG    g_DbgcIoProvIpc;


#endif /* !DEBUGGER_INCLUDED_SRC_DBGCIoProvInternal_h */

