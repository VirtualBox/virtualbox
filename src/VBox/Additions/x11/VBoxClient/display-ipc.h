/* $Id: display-ipc.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - DRM IPC communication core function definitions.
 *
 * Definitions for IPC communication in between VBoxDRMClient and VBoxClient.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_display_ipc_h
#define GA_INCLUDED_SRC_x11_VBoxClient_display_ipc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

# include <iprt/assert.h>
# include <iprt/localipc.h>
# include <iprt/critsect.h>
# include <iprt/list.h>

/** Name of DRM IPC server.*/
# define VBOX_DRMIPC_SERVER_NAME    "DRMIpcServer"
/** A user group which is allowed to connect to IPC server. */
#define VBOX_DRMIPC_USER_GROUP      "vboxdrmipc"
/** Time in milliseconds to wait for host events. */
#define VBOX_DRMIPC_RX_TIMEOUT_MS   (500)
/** Time in milliseconds to relax in between unsuccessful connect attempts. */
#define VBOX_DRMIPC_RX_RELAX_MS     (500)
/** Size of RX buffer for IPC communication. */
#define VBOX_DRMIPC_RX_BUFFER_SIZE  (1024)
/** Maximum amount of TX messages which can be queued. */
#define VBOX_DRMIPC_TX_QUEUE_SIZE   (64)
/** Maximum number of physical monitor configurations we can process. */
#define VBOX_DRMIPC_MONITORS_MAX    (32)

/** Rectangle structure for geometry of a single screen. */
struct VBOX_DRMIPC_VMWRECT
{
    /** Monitor X offset. */
    int32_t x;
    /** Monitor Y offset. */
    int32_t y;
    /** Monitor width. */
    uint32_t w;
    /** Monitor height. */
    uint32_t h;
};
AssertCompileSize(struct VBOX_DRMIPC_VMWRECT, 16);

/** List of IPC commands issued by client to server. */
typedef enum VBOXDRMIPCSRVCMD
{
    /** Separate server and client commands by starting index. */
    VBOXDRMIPCSRVCMD_INVALID = 0x00,
    /** Client reports list of current display offsets. */
    VBOXDRMIPCSRVCMD_REPORT_DISPLAY_OFFSETS,
    /** Termination of commands list. */
    VBOXDRMIPCSRVCMD_MAX
} VBOXDRMIPCSRVCMD;

/** List of IPC commands issued by server to client. */
typedef enum VBOXDRMIPCCLTCMD
{
    /** Separate server and client commands by starting index. */
    VBOXDRMIPCCLTCMD_INVALID = 0x7F,
    /** Server requests client to set primary screen. */
    VBOXDRMIPCCLTCMD_SET_PRIMARY_DISPLAY,
    /** Termination of commands list. */
    VBOXDRMIPCCLTCMD_MAX
} VBOXDRMIPCCLTCMD;

/** IPC command header. */
typedef struct VBOX_DRMIPC_COMMAND_HEADER
{
    /** IPC command structure checksum, includes header and payload. */
    uint64_t u64Crc;
    /** IPC command identificator (opaque). */
    uint8_t idCmd;
    /** Size of payload data. */
    uint64_t cbData;

} VBOX_DRMIPC_COMMAND_HEADER;

/** Pointer to IPC command header. */
typedef VBOX_DRMIPC_COMMAND_HEADER *PVBOX_DRMIPC_COMMAND_HEADER;

/** IPC command VBOXDRMIPCCLTCMD_SET_PRIMARY_DISPLAY payload. */
typedef struct VBOX_DRMIPC_COMMAND_SET_PRIMARY_DISPLAY
{
    /* IPC command header. */
    VBOX_DRMIPC_COMMAND_HEADER Hdr;
    /** ID of display to be set as primary. */
    uint32_t idDisplay;

} VBOX_DRMIPC_COMMAND_SET_PRIMARY_DISPLAY;

/** Pointer to IPC command DRMIPCCOMMAND_SET_PRIMARY_DISPLAY payload. */
typedef VBOX_DRMIPC_COMMAND_SET_PRIMARY_DISPLAY *PVBOX_DRMIPC_COMMAND_SET_PRIMARY_DISPLAY;

/** IPC command VBOXDRMIPCSRVCMD_REPORT_DISPLAY_OFFSETS payload. */
typedef struct VBOX_DRMIPC_COMMAND_REPORT_DISPLAY_OFFSETS
{
    /* IPC command header. */
    VBOX_DRMIPC_COMMAND_HEADER Hdr;
    /** Number of displays which have changed offsets. */
    uint32_t cDisplays;
    /** Offsets data. */
    struct VBOX_DRMIPC_VMWRECT aDisplays[VBOX_DRMIPC_MONITORS_MAX];
} VBOX_DRMIPC_COMMAND_REPORT_DISPLAY_OFFSETS;

/** Pointer to IPC command DRMIPCCOMMAND_SET_PRIMARY_DISPLAY payload. */
typedef VBOX_DRMIPC_COMMAND_REPORT_DISPLAY_OFFSETS *PVBOX_DRMIPC_COMMAND_REPORT_DISPLAY_OFFSETS;

/** DRM IPC TX list entry. */
typedef struct VBOX_DRMIPC_TX_LIST_ENTRY
{
    /** The list node. */
    RTLISTNODE Node;
    /* IPC command header. */
    VBOX_DRMIPC_COMMAND_HEADER Hdr;
} VBOX_DRMIPC_TX_LIST_ENTRY;

/** Pointer to DRM IPC TX list entry. */
typedef VBOX_DRMIPC_TX_LIST_ENTRY *PVBOX_DRMIPC_TX_LIST_ENTRY;

/**
 * A callback function which is called by IPC client session thread when new message arrives.
 *
 * @returns IPRT status code.
 * @param   idCmd           Command ID to be executed (opaque).
 * @param   pvData          Command specific argument data.
 * @param   cbData          Size of command argument data as received over IPC.
 */
typedef DECLCALLBACKTYPE(int, FNDRMIPCRXCB, (uint8_t idCmd, void *pvData, uint32_t cbData));

/** Pointer to FNDRMIPCRXCB. */
typedef FNDRMIPCRXCB *PFNDRMIPCRXCB;

/** IPC session private data. */
typedef struct VBOX_DRMIPC_CLIENT
{
    /** Thread handle which dispatches this IPC client session. */
    RTTHREAD hThread;
    /** IPC session handle. */
    RTLOCALIPCSESSION hClientSession;
    /** TX message queue mutex. */
    RTCRITSECT CritSect;
    /** TX message queue (accessed under critsect). */
    VBOX_DRMIPC_TX_LIST_ENTRY TxList;
    /** Maximum number of messages which can be queued to TX message queue. */
    uint32_t cTxListCapacity;
    /** Actual number of messages currently queued to TX message queue (accessed under critsect). */
    uint32_t cTxListSize;
    /** IPC RX callback. */
    PFNDRMIPCRXCB pfnRxCb;
} VBOX_DRMIPC_CLIENT;

/** Pointer to IPC session private data. */
typedef VBOX_DRMIPC_CLIENT *PVBOX_DRMIPC_CLIENT;

/** Static initializer for VBOX_DRMIPC_CLIENT. */
#define VBOX_DRMIPC_CLIENT_INITIALIZER  { NIL_RTTHREAD, 0, { 0 }, { { NULL, NULL },  {0, 0, 0} }, 0, 0, NULL }

/**
 * Initialize IPC client private data.
 *
 * @return  IPRT status code.
 * @param   pClient             IPC client private data to be initialized.
 * @param   hThread             A thread which server IPC client connection.
 * @param   hClientSession      IPC session handle obtained from RTLocalIpcSessionXXX().
 * @param   cTxListCapacity     Maximum number of messages which can be queued for TX for this IPC session.
 * @param   pfnRxCb             IPC RX callback function pointer.
 */
RTDECL(int) vbDrmIpcClientInit(PVBOX_DRMIPC_CLIENT pClient, RTTHREAD hThread, RTLOCALIPCSESSION hClientSession,
                               uint32_t cTxListCapacity, PFNDRMIPCRXCB pfnRxCb);

/**
 * Releases IPC client private data resources.
 *
 * @return  IPRT status code.
 * @param   pClient     IPC session private data to be initialized.
 */
RTDECL(int) vbDrmIpcClientReleaseResources(PVBOX_DRMIPC_CLIENT pClient);

/**
 * Verify if remote IPC peer corresponds to a process which is running
 * from allowed user.
 *
 * @return  IPRT status code.
 * @param   hClientSession    IPC session handle.
 */
RTDECL(int) vbDrmIpcAuth(RTLOCALIPCSESSION hClientSession);

/**
 * Common function for both IPC server and client which is responsible
 * for handling IPC communication flow.
 *
 * @return  IPRT status code.
 * @param   pClient     IPC connection private data.
 */
RTDECL(int) vbDrmIpcConnectionHandler(PVBOX_DRMIPC_CLIENT pClient);

/**
 * Request remote IPC peer to set primary display.
 *
 * @return  IPRT status code.
 * @param   pClient     IPC session private data.
 * @param   idDisplay       ID of display to be set as primary.
 */
RTDECL(int) vbDrmIpcSetPrimaryDisplay(PVBOX_DRMIPC_CLIENT pClient, uint32_t idDisplay);

/**
 * Report to IPC server that display layout offsets have been changed (called by IPC client).
 *
 * @return  IPRT status code.
 * @param   pClient     IPC session private data.
 * @param   cDisplays   Number of monitors which have offsets changed.
 * @param   aDisplays   Offsets data.
 */
RTDECL(int) vbDrmIpcReportDisplayOffsets(PVBOX_DRMIPC_CLIENT pClient, uint32_t cDisplays, struct VBOX_DRMIPC_VMWRECT *aDisplays);

#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_display_ipc_h */
