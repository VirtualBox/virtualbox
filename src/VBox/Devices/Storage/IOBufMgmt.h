/* $Id: IOBufMgmt.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices: I/O buffer management API.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Storage_IOBufMgmt_h
#define VBOX_INCLUDED_SRC_Storage_IOBufMgmt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <iprt/sg.h>

RT_C_DECLS_BEGIN

/**
 * Opaque I/O buffer manager.
 */
typedef struct IOBUFMGRINT *IOBUFMGR;
/** Pointer to a I/O buffer manager. */
typedef IOBUFMGR *PIOBUFMGR;

/* NIL I/O buffer manager handle. */
#define NIL_IOBUFMGR ((IOBUFMGR)0)

#define IOBUFMGR_F_DEFAULT             (0)
/** I/O buffer memory needs to be non pageable (for example because it contains sensitive data
 * which shouldn't end up in swap unencrypted). */
#define IOBUFMGR_F_REQUIRE_NOT_PAGABLE RT_BIT(0)

/**
 * I/O buffer descriptor.
 */
typedef struct IOBUFDESC
{
    /** S/G buffer. */
    RTSGBUF          SgBuf;
    /** Internal data */
    union
    {
#ifdef IOBUFDESCINT_DECLARED
        IOBUFDESCINT Int;
#endif
        uint8_t      abPadding[HC_ARCH_BITS == 32 ? 88 : 172];
    };
} IOBUFDESC;
/** Pointer to a I/O buffer descriptor. */
typedef IOBUFDESC *PIOBUFDESC;

/**
 * Creates I/O buffer manager.
 *
 * @returns VBox status code.
 * @param   phIoBufMgr    Where to store the handle to the I/O buffer manager on success.
 * @param   cbMax         The maximum amount of I/O memory to allow. Trying to allocate more than
 *                        this will lead to out of memory errors. 0 for "unlimited" size (only restriction
 *                        is the available memory on the host).
 * @param   fFlags        Combination of IOBUFMGR_F_*
 */
DECLHIDDEN(int) IOBUFMgrCreate(PIOBUFMGR phIoBufMgr, size_t cbMax, uint32_t fFlags);

/**
 * Destroys the given I/O buffer manager.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_STATE if there is still memory allocated by the given manager.
 * @param   hIoBufMgr          The I/O buffer manager.
 */
DECLHIDDEN(int) IOBUFMgrDestroy(IOBUFMGR hIoBufMgr);

/**
 * Allocates a I/O buffer and fills the descriptor.
 *
 * @returns VBox status code.
 * @retval  VERR_NO_MEMORY if there is not enough free memory to satisfy the request
 *          and partial allocations are not allowed or allocating some internal tracking
 *          structures failed.
 * @param   hIoBufMgr          The I/O buffer manager.
 * @param   pIoBufDesc         The I/O buffer descriptor to initialize on success.
 * @param   cbIoBuf            How much to allocate.
 * @param   pcbIoBufAllocated  Where to store amount of memory the manager was able to allocate
 *                             if there is not enough free memory to satisfy the complete request.
 *                             NULL if partial allocations are not supported.
 */
DECLHIDDEN(int) IOBUFMgrAllocBuf(IOBUFMGR hIoBufMgr, PIOBUFDESC pIoBufDesc, size_t cbIoBuf, size_t *pcbIoBufAllocated);

/**
 * Frees a given I/O buffer.
 *
 * @param   pIoBufDesc         The I/O buffer descriptor to free.
 */
DECLHIDDEN(void) IOBUFMgrFreeBuf(PIOBUFDESC pIoBufDesc);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Storage_IOBufMgmt_h */
