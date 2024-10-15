/** $Id: VDIoBackend.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VBox HDD container test utility, async I/O backend
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_testcase_VDIoBackend_h
#define VBOX_INCLUDED_SRC_testcase_VDIoBackend_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/sg.h>

#include "VDDefs.h"

/** I/O backend handle. */
typedef struct VDIOBACKEND *PVDIOBACKEND;
/** Pointer to a I/O backend handle. */
typedef PVDIOBACKEND *PPVDIOBACKEND;

/** Storage handle. */
typedef struct VDIOSTORAGE *PVDIOSTORAGE;
/** Pointer to a storage handle. */
typedef PVDIOSTORAGE *PPVDIOSTORAGE;

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
int VDIoBackendCreate(PPVDIOBACKEND ppIoBackend);

/**
 * Destroys a memory I/O backend.
 *
 * @param pIoBackend     The backend to destroy.
 */
void VDIoBackendDestroy(PVDIOBACKEND pIoBackend);

int VDIoBackendStorageCreate(PVDIOBACKEND pIoBackend, const char *pszBackend,
                             const char *pszName, PFNVDIOCOMPLETE pfnComplete,
                             PPVDIOSTORAGE ppIoStorage);

void VDIoBackendStorageDestroy(PVDIOSTORAGE pIoStorage);

int VDIoBackendStorageSetSize(PVDIOSTORAGE pIoStorage, uint64_t cbSize);

int VDIoBackendStorageGetSize(PVDIOSTORAGE pIoStorage, uint64_t *pcbSize);

DECLHIDDEN(int) VDIoBackendDumpToFile(PVDIOSTORAGE pIoStorage, const char *pszPath);

/**
 * Enqueues a new I/O request.
 *
 * @returns IPRT status code.
 *
 * @param pIoStorage     Storage handle.
 * @param enmTxDir       The transfer direction.
 * @param off            Start offset of the transfer.
 * @param cbTransfer     Size of the transfer.
 * @param pSgBuf         S/G buffer to use.
 * @param pvUser         Opaque user data.
 * @param fSync          Flag whether to wait for the operation to complete.
 */
int VDIoBackendTransfer(PVDIOSTORAGE pIoStorage, VDIOTXDIR enmTxDir, uint64_t off,
                        size_t cbTransfer, PRTSGBUF pSgBuf, void *pvUser, bool fSync);

#endif /* !VBOX_INCLUDED_SRC_testcase_VDIoBackend_h */
