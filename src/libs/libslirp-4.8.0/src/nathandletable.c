/* $Id: nathandletable.c 109046 2025-04-02 18:40:55Z jack.doherty@oracle.com $ */
/** @file
 * libslirp: NAT Handle Table Wrapper
 */

/*
 * Copyright (C) 2006-2023 Oracle and/or its affiliates.
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

#ifdef VBOX
#ifdef _WIN32
#include "nathandletable.h"

RTHANDLETABLE g_hNATHandleTable = NIL_RTHANDLETABLE;

/**
 * Returns the windows SOCKET for file descriptor @a fd (aka handle).
 *
 * @returns Windows socket handle. NULL if @a fd is invalid.
 */
SOCKET libslirp_wrap_RTHandleTableLookup(int fd)
{
    RTHANDLETABLE const hHandleTable = g_hNATHandleTable;
    SOCKET hSock = (SOCKET)RTHandleTableLookup(hHandleTable, fd);
    Log6Func(("Looked up %d in %p and returned %d\n", fd, hHandleTable, hSock));
    return hSock;
}

/**
 * Allocates a file descriptor (handle) for windows SOCKET @a hSock.
 * @returns IPRT status code.
 */
int libslirp_wrap_RTHandleTableAlloc(SOCKET hSock, uint32_t *pHandle)
{
    /* Lazy create the handle table (leaked): */
    RTHANDLETABLE hHandleTable = g_hNATHandleTable;
    if (RT_LIKELY(hHandleTable != NIL_RTHANDLETABLE))
    { /* likely*/ }
    else
    {
        int rc = RTHandleTableCreate(&hHandleTable);
        AssertLogRelRCReturn(rc, rc);
        /** @todo potential race here? iff so, use cmpxchg from asm.h   */
        g_hNATHandleTable = hHandleTable;
    }

    int rc = RTHandleTableAlloc(hHandleTable, (void *)hSock, pHandle);
    Log6Func(("Creating sock %p in %p with handle %d\n", hSock, hHandleTable, *pHandle));
    return rc;
}

/**
 * Frees file descriptor (handle) @a fd after the associated socket has been
 * closed.
 *
 * @returns IPRT status code. (Shouldn't fail unless there are multiple
 *          concurrent closesocket calls.)
 */
int libslirp_wrap_RTHandleTableFree(int fd)
{
    RTHANDLETABLE const hHandleTable = g_hNATHandleTable;
    AssertReturn(hHandleTable != NIL_RTHANDLETABLE, VERR_INVALID_PARAMETER);

    void * const pvObsoleteSocket = RTHandleTableFree(hHandleTable, fd);
    Log6Func(("Freed handle %d (sock %p) from %p\n", fd, pvObsoleteSocket, hHandleTable));
    if (pvObsoleteSocket)
        return VINF_SUCCESS;

    return VERR_INVALID_PARAMETER;
}

#endif
#endif