/* $Id: intnetr3ipc.h 114959 2026-08-10 14:41:23Z andreas.loeffler@oracle.com $ */
/** @file
 * Internal networking Ring-3 service IPC protocol.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_intnetr3ipc_h
#define VBOX_INCLUDED_intnetr3ipc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>

RT_C_DECLS_BEGIN

/** The local IPC service identifier. */
#ifndef INTNET_R3_SVC_NAME
# define INTNET_R3_SVC_NAME                "org.virtualbox.intnet"
#endif
/** Maximum generated per-user local IPC service name length, including the terminator. */
#define INTNET_R3_IPC_MAX_SERVICE_NAME      64
/** Protocol version. */
#define INTNET_R3_IPC_VERSION               UINT16_C(1)
/** Maximum size of an IntNet request payload. */
#define INTNET_R3_IPC_MAX_REQ               UINT32_C(65536)
/** Maximum shared-memory object name length, including the terminator. */
#define INTNET_R3_IPC_MAX_SHMEM_NAME        256
/** Maximum time allowed to complete a started local IPC frame. */
#define INTNET_R3_IPC_FRAME_TIMEOUT_MS      UINT32_C(30000)

/** Local IPC request header magic. */
#define INTNET_R3_IPC_REQ_MAGIC             RT_MAKE_U32_FROM_U8('I', 'N', 'R', 'Q')
/** Local IPC synchronous reply header magic. */
#define INTNET_R3_IPC_REPLY_MAGIC           RT_MAKE_U32_FROM_U8('I', 'N', 'R', 'P')
/** Local IPC receive-available notification header magic. */
#define INTNET_R3_IPC_POKE_MAGIC            RT_MAKE_U32_FROM_U8('I', 'N', 'R', 'K')

/** Header preceding an IntNet service request payload. */
typedef struct INTNETR3IPCREQHDR
{
    /** INTNET_R3_IPC_REQ_MAGIC. */
    uint32_t    u32Magic;
    /** INTNET_R3_IPC_VERSION. */
    uint16_t    u16Version;
    /** Size of this header in bytes. */
    uint16_t    cbHdr;
    /** Size of the request payload following this header. */
    uint32_t    cbReq;
    /** VMMR0_DO_INTNET_* operation to perform. */
    uint32_t    uOperation;
} INTNETR3IPCREQHDR;
/** Pointer to an IntNet service request header. */
typedef INTNETR3IPCREQHDR *PINTNETR3IPCREQHDR;

/** Header preceding an IntNet service reply or receive-available notification. */
typedef struct INTNETR3IPCREPLYHDR
{
    /** INTNET_R3_IPC_REPLY_MAGIC or INTNET_R3_IPC_POKE_MAGIC. */
    uint32_t    u32Magic;
    /** INTNET_R3_IPC_VERSION. */
    uint16_t    u16Version;
    /** Size of this header in bytes. */
    uint16_t    cbHdr;
    /** VBox status code for the request or notification. */
    int32_t     rc;
    /** Size of the reply payload following this header. */
    uint32_t    cbReply;
    /** Size of the optional terminated shared-memory name following the payload. */
    uint32_t    cbShMemName;
    /** Size of the shared-memory object in bytes. */
    uint64_t    cbShMem;
} INTNETR3IPCREPLYHDR;
/** Pointer to a const IntNet service reply header. */
typedef INTNETR3IPCREPLYHDR const *PCINTNETR3IPCREPLYHDR;

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_intnetr3ipc_h */
