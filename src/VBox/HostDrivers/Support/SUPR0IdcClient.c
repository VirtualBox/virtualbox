/* $Id: SUPR0IdcClient.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Driver - IDC Client Lib, Core.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "SUPR0IdcClientInternal.h"
#include <iprt/errcore.h>
#include <iprt/asm.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static PSUPDRVIDCHANDLE volatile g_pMainHandle = NULL;


/**
 * Opens the IDC interface of the support driver.
 *
 * This will perform basic version negotiations and fail if the
 * minimum requirements aren't met.
 *
 * @returns VBox status code.
 * @param   pHandle             The handle structure (output).
 * @param   uReqVersion         The requested version. Pass 0 for default.
 * @param   uMinVersion         The minimum required version. Pass 0 for default.
 * @param   puSessionVersion    Where to store the session version. Optional.
 * @param   puDriverVersion     Where to store the session version. Optional.
 * @param   puDriverRevision    Where to store the SVN revision of the driver. Optional.
 */
SUPR0DECL(int) SUPR0IdcOpen(PSUPDRVIDCHANDLE pHandle, uint32_t uReqVersion, uint32_t uMinVersion,
                            uint32_t *puSessionVersion, uint32_t *puDriverVersion, uint32_t *puDriverRevision)
{
    unsigned uDefaultMinVersion;
    SUPDRVIDCREQCONNECT Req;
    int rc;

    /*
     * Validate and set failure return values.
     */
    AssertPtrReturn(pHandle, VERR_INVALID_POINTER);
    pHandle->s.pSession = NULL;

    AssertPtrNullReturn(puSessionVersion, VERR_INVALID_POINTER);
    if (puSessionVersion)
        *puSessionVersion = 0;

    AssertPtrNullReturn(puDriverVersion, VERR_INVALID_POINTER);
    if (puDriverVersion)
        *puDriverVersion = 0;

    AssertPtrNullReturn(puDriverRevision, VERR_INVALID_POINTER);
    if (puDriverRevision)
        *puDriverRevision = 0;

    AssertReturn(!uMinVersion || (uMinVersion & UINT32_C(0xffff0000)) == (SUPDRV_IDC_VERSION & UINT32_C(0xffff0000)), VERR_INVALID_PARAMETER);
    AssertReturn(!uReqVersion || (uReqVersion & UINT32_C(0xffff0000)) == (SUPDRV_IDC_VERSION & UINT32_C(0xffff0000)), VERR_INVALID_PARAMETER);

    /*
     * Handle default version input and enforce minimum requirements made
     * by this library.
     *
     * The clients will pass defaults (0), and only in the case that some
     * special API feature was just added will they set an actual version.
     * So, this is the place where can easily enforce a minimum IDC version
     * on bugs and similar. It corresponds a bit to what SUPR3Init is
     * responsible for.
     */
    uDefaultMinVersion = SUPDRV_IDC_VERSION & UINT32_C(0xffff0000);
    if (!uMinVersion || uMinVersion < uDefaultMinVersion)
        uMinVersion = uDefaultMinVersion;
    if (!uReqVersion || uReqVersion < uDefaultMinVersion)
        uReqVersion = uDefaultMinVersion;

    /*
     * Setup the connect request packet and call the OS specific function.
     */
    Req.Hdr.cb = sizeof(Req);
    Req.Hdr.rc = VERR_WRONG_ORDER;
    Req.Hdr.pSession = NULL;
    Req.u.In.u32MagicCookie = SUPDRVIDCREQ_CONNECT_MAGIC_COOKIE;
    Req.u.In.uMinVersion = uMinVersion;
    Req.u.In.uReqVersion = uReqVersion;
    rc = supR0IdcNativeOpen(pHandle, &Req);
    if (RT_SUCCESS(rc))
    {
        pHandle->s.pSession = Req.u.Out.pSession;
        if (puSessionVersion)
            *puSessionVersion = Req.u.Out.uSessionVersion;
        if (puDriverVersion)
            *puDriverVersion = Req.u.Out.uDriverVersion;
        if (puDriverRevision)
            *puDriverRevision = Req.u.Out.uDriverRevision;

        /*
         * We don't really trust anyone, make sure the returned
         * session and version values actually makes sense.
         */
        if (    RT_VALID_PTR(Req.u.Out.pSession)
            &&  Req.u.Out.uSessionVersion >= uMinVersion
            &&  (Req.u.Out.uSessionVersion & UINT32_C(0xffff0000)) == (SUPDRV_IDC_VERSION & UINT32_C(0xffff0000)))
        {
            ASMAtomicCmpXchgPtr(&g_pMainHandle, pHandle, NULL);
            return rc;
        }

        AssertMsgFailed(("pSession=%p uSessionVersion=0x%x (r%u)\n", Req.u.Out.pSession, Req.u.Out.uSessionVersion, Req.u.Out.uDriverRevision));
        rc = VERR_VERSION_MISMATCH;
        SUPR0IdcClose(pHandle);
    }

    return rc;
}


/**
 * Closes a IDC connection established by SUPR0IdcOpen.
 *
 * @returns VBox status code.
 * @param   pHandle     The IDC handle.
 */
SUPR0DECL(int) SUPR0IdcClose(PSUPDRVIDCHANDLE pHandle)
{
    SUPDRVIDCREQHDR Req;
    int rc;

    /*
     * Catch closed handles and check that the session is valid.
     */
    AssertPtrReturn(pHandle, VERR_INVALID_POINTER);
    if (!pHandle->s.pSession)
        return VERR_INVALID_HANDLE;
    AssertPtrReturn(pHandle->s.pSession, VERR_INVALID_HANDLE);

    /*
     * Create the request and hand it to the OS specific code.
     */
    Req.cb = sizeof(Req);
    Req.rc = VERR_WRONG_ORDER;
    Req.pSession = pHandle->s.pSession;
    rc = supR0IdcNativeClose(pHandle, &Req);
    if (RT_SUCCESS(rc))
    {
        pHandle->s.pSession = NULL;
        ASMAtomicCmpXchgPtr(&g_pMainHandle, NULL, pHandle);
    }
    return rc;
}


/**
 * Get the SUPDRV session for the IDC connection.
 *
 * This is for use with SUPDRV and component APIs that requires a valid
 * session handle.
 *
 * @returns The session handle on success, NULL if the IDC handle is invalid.
 *
 * @param   pHandle         The IDC handle.
 */
SUPR0DECL(PSUPDRVSESSION) SUPR0IdcGetSession(PSUPDRVIDCHANDLE pHandle)
{
    PSUPDRVSESSION pSession;
    AssertPtrReturn(pHandle, NULL);
    pSession = pHandle->s.pSession;
    AssertPtrReturn(pSession, NULL);
    return pSession;
}


/**
 * Looks up a IDC handle by session.
 *
 * @returns The IDC handle on success, NULL on failure.
 * @param   pSession    The session to lookup.
 *
 * @internal
 */
PSUPDRVIDCHANDLE supR0IdcGetHandleFromSession(PSUPDRVSESSION pSession)
{
    PSUPDRVIDCHANDLE pHandle = ASMAtomicUoReadPtrT(&g_pMainHandle, PSUPDRVIDCHANDLE);
    if (   RT_VALID_PTR(pHandle)
        && pHandle->s.pSession == pSession)
        return pHandle;
    return NULL;
}

