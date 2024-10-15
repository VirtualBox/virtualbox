/* $Id: SUPR0IdcClientComponent.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Driver - IDC Client Lib, Component APIs.
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


/**
 * Registers a component factory with SUPDRV.
 *
 * @returns VBox status code.
 * @param   pHandle         The IDC handle.
 * @param   pFactory        The factory to register.
 */
SUPR0DECL(int) SUPR0IdcComponentRegisterFactory(PSUPDRVIDCHANDLE pHandle, PCSUPDRVFACTORY pFactory)
{
    SUPDRVIDCREQCOMPREGFACTORY Req;

    /*
     * Validate the handle before we access it.
     */
    AssertPtrReturn(pHandle, VERR_INVALID_HANDLE);
    AssertPtrReturn(pHandle->s.pSession, VERR_INVALID_HANDLE);

    /*
     * Construct and fire off the request.
     */
    Req.Hdr.cb = sizeof(Req);
    Req.Hdr.rc = VERR_WRONG_ORDER;
    Req.Hdr.pSession = pHandle->s.pSession;
    Req.u.In.pFactory = pFactory;

    return supR0IdcNativeCall(pHandle, SUPDRV_IDC_REQ_COMPONENT_REGISTER_FACTORY, &Req.Hdr);
}


/**
 * Deregisters a component factory with SUPDRV.
 *
 * @returns VBox status code.
 * @param   pHandle         The IDC handle.
 * @param   pFactory        The factory to register.
 */
SUPR0DECL(int) SUPR0IdcComponentDeregisterFactory(PSUPDRVIDCHANDLE pHandle, PCSUPDRVFACTORY pFactory)
{
    SUPDRVIDCREQCOMPDEREGFACTORY Req;

    /*
     * Validate the handle before we access it.
     */
    AssertPtrReturn(pHandle, VERR_INVALID_HANDLE);
    AssertPtrReturn(pHandle->s.pSession, VERR_INVALID_HANDLE);

    /*
     * Construct and fire off the request.
     */
    Req.Hdr.cb = sizeof(Req);
    Req.Hdr.rc = VERR_WRONG_ORDER;
    Req.Hdr.pSession = pHandle->s.pSession;
    Req.u.In.pFactory = pFactory;

    return supR0IdcNativeCall(pHandle, SUPDRV_IDC_REQ_COMPONENT_DEREGISTER_FACTORY, &Req.Hdr);
}

