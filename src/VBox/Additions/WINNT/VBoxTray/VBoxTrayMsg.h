/* $Id: VBoxTrayMsg.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxTrayMsg - Globally registered messages (RPC) to/from VBoxTray.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxTrayMsg_h
#define GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxTrayMsg_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** The IPC pipe's prefix (native).
 * Will be followed by the username VBoxTray runs under. */
#define VBOXTRAY_IPC_PIPE_PREFIX        "\\\\.\\pipe\\VBoxTrayIPC-"
/** The IPC header's magic. */
#define VBOXTRAY_IPC_HDR_MAGIC          0x19840804
/** IPC header version number. */
#define VBOXTRAY_IPC_HDR_VERSION        1
/** The max payload size accepted by VBoxTray.  Clients trying to send more
 *  will be disconnected. */
#define VBOXTRAY_IPC_MAX_PAYLOAD        _16K


/**
 * VBoxTray IPC message types.
 */
typedef enum VBOXTRAYIPCMSGTYPE
{
    /** Customary invalid zero value. */
    VBOXTRAYIPCMSGTYPE_INVALID = 0,
    /** Restarts VBoxTray - not implemented.
     * Payload: None.
     * Reply: None. */
    VBOXTRAYIPCMSGTYPE_RESTART,
    /** Shows a balloon message in the tray area.
     * Payload: VBOXTRAYIPCMSG_SHOW_BALLOON_MSG_T
     * Reply: None */
    VBOXTRAYIPCMSGTYPE_SHOW_BALLOON_MSG,
    /** Time since the last user input for the user VBoxTray is running as.
     * Payload: None.
     * Reply: VBOXTRAYIPCREPLY_USER_LAST_INPUT_T. */
    VBOXTRAYIPCMSGTYPE_USER_LAST_INPUT,
    /** End of valid types. */
    VBOXTRAYIPCMSGTYPE_END,
    /* Make sure the type is 32-bit wide. */
    VBOXTRAYIPCMSGTYPE_32BIT_HACK = 0x7fffffff
} VBOXTRAYIPCMSGTYPE;

/**
 * VBoxTray's IPC header.
 *
 * All messages have one of these.  The payload following it is optional and
 * specific to each individual message type.
 */
typedef struct VBOXTRAYIPCHEADER
{
    /** The header's magic (VBOXTRAY_IPC_HDR_MAGIC). */
    uint32_t            uMagic;
    /** Header version, must be 0 by now. */
    uint32_t            uVersion;
    /** Message type, a VBOXTRAYIPCMSGTYPE value. */
    VBOXTRAYIPCMSGTYPE  enmMsgType;
    /** Payload length in bytes.
     * When present, the payload follows this header. */
    uint32_t            cbPayload;
} VBOXTRAYIPCHEADER;
/** Pointer to a VBoxTray IPC header. */
typedef VBOXTRAYIPCHEADER *PVBOXTRAYIPCHEADER;

/**
 * Tells VBoxTray to show a balloon message in Windows' tray area.
 *
 * This may or may not work depending on the system's configuration / set user
 * preference.
 */
typedef struct VBOXTRAYIPCMSG_SHOW_BALLOON_MSG_T
{
    /** Length of the message string (no terminator). */
    uint32_t    cchMsg;
    /** Length of the title string (no terminator). */
    uint32_t    cchTitle;
    /** Message type. */
    uint32_t    uType;
    /** Time to show the message (in ms). */
    uint32_t    cMsTimeout;
    /** Variable length buffer containing two szero terminated strings, first is  */
    char        szzStrings[RT_FLEXIBLE_ARRAY];
} VBOXTRAYIPCMSG_SHOW_BALLOON_MSG_T;
typedef VBOXTRAYIPCMSG_SHOW_BALLOON_MSG_T *PVBOXTRAYIPCMSG_SHOW_BALLOON_MSG_T;

/**
 * Reply to VBOXTRAYIPCMSGTYPE_USER_LAST_INPUT
 */
typedef struct VBOXTRAYIPCREPLY_USER_LAST_INPUT_T
{
    /** How many seconds since the last user input event.
     * Set to UINT32_MAX if we don't know. */
    uint32_t    cSecSinceLastInput;
} VBOXTRAYIPCREPLY_USER_LAST_INPUT_T;
typedef VBOXTRAYIPCREPLY_USER_LAST_INPUT_T *PVBOXTRAYIPCREPLY_USER_LAST_INPUT_T;

#endif /* !GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxTrayMsg_h */

