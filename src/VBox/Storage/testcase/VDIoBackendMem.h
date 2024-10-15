/** $Id: VDIoBackendMem.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VBox HDD container test utility, async I/O memory backend
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_testcase_VDIoBackendMem_h
#define VBOX_INCLUDED_SRC_testcase_VDIoBackendMem_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/sg.h>

#include "VDDefs.h"

/** Memory backend handle. */
typedef struct VDIOBACKENDMEM *PVDIOBACKENDMEM;
/** Pointer to a memory backend handle. */
typedef PVDIOBACKENDMEM *PPVDIOBACKENDMEM;

/**
 * Completion handler.
 *
 * @returns IPRT status code.
 * @param   pvUser    Opaque user data.
 * @param   rcReq     Completion code for the request.
 */
typedef DECLCALLBACKTYPE(int, FNVDIOCOMPLETE,(void *pvUser, int rcReq));
/** Pointer to a completion handler. */
typedef FNVDIOCOMPLETE *PFNVDIOCOMPLETE;

/**
 * Creates a new memory I/O backend.
 *
 * @returns IPRT status code.
 *
 * @param ppIoBackend    Where to store the handle on success.
 */
int VDIoBackendMemCreate(PPVDIOBACKENDMEM ppIoBackend);

/**
 * Destroys a memory I/O backend.
 *
 * @returns IPRT status code.
 *
 * @param pIoBackend     The backend to destroy.
 */
int VDIoBackendMemDestroy(PVDIOBACKENDMEM pIoBackend);

/**
 * Enqueues a new I/O request.
 *
 * @returns IPRT status code.
 *
 * @param pIoBackend     The backend which should handle the
 *                       transfer.
 * @param pMemDisk       The memory disk the request is for.
 * @param enmTxDir       The transfer direction.
 * @param off            Start offset of the transfer.
 * @param cbTransfer     Size of the transfer.
 * @param pSgBuf         S/G buffer to use.
 * @param pfnComplete    Completion handler to call.
 * @param pvUser         Opaque user data.
 */
int VDIoBackendMemTransfer(PVDIOBACKENDMEM pIoBackend, PVDMEMDISK pMemDisk,
                           VDIOTXDIR enmTxDir, uint64_t off, size_t cbTransfer,
                           PRTSGBUF pSgBuf, PFNVDIOCOMPLETE pfnComplete, void *pvUser);

#endif /* !VBOX_INCLUDED_SRC_testcase_VDIoBackendMem_h */
