/* $Id: SUPR0IdcClientInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Driver - Internal header for the IDC client library.
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

#ifndef VBOX_INCLUDED_SRC_Support_SUPR0IdcClientInternal_h
#define VBOX_INCLUDED_SRC_Support_SUPR0IdcClientInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <iprt/assert.h>

#ifdef RT_OS_WINDOWS
# include <iprt/nt/ntddk.h>
#endif


/**
 * The hidden part of SUPDRVIDCHANDLE.
 */
struct SUPDRVIDCHANDLEPRIVATE
{
    /** Pointer to the session handle. */
    PSUPDRVSESSION  pSession;
# ifdef RT_OS_WINDOWS
    /** Pointer to the NT device object. */
    PDEVICE_OBJECT  pDeviceObject;
    /** Pointer to the NT file object. */
    PFILE_OBJECT    pFileObject;
# endif
};
/** Indicate that the structure is present. */
#define SUPDRVIDCHANDLEPRIVATE_DECLARED 1

#include <VBox/sup.h>
#include "SUPDrvIDC.h"
AssertCompile(RT_SIZEOFMEMB(SUPDRVIDCHANDLE, apvPadding) >= sizeof(struct SUPDRVIDCHANDLEPRIVATE));

RT_C_DECLS_BEGIN
PSUPDRVIDCHANDLE supR0IdcGetHandleFromSession(PSUPDRVSESSION pSession);
int VBOXCALL supR0IdcNativeOpen(PSUPDRVIDCHANDLE pHandle, PSUPDRVIDCREQCONNECT pReq);
int VBOXCALL supR0IdcNativeClose(PSUPDRVIDCHANDLE pHandle, PSUPDRVIDCREQHDR pReq);
int VBOXCALL supR0IdcNativeCall(PSUPDRVIDCHANDLE pHandle, uint32_t iReq, PSUPDRVIDCREQHDR pReq);
RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Support_SUPR0IdcClientInternal_h */

