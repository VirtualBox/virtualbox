/* $Id: ftp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Header file for FTP client / server implementations.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_ftp_h
#define IPRT_INCLUDED_ftp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/fs.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_ftp        RTFtp - FTP server and client.
 * @ingroup grp_rt
 * @{
 */

/** @defgroup grp_rt_ftpserver  RTFtpServer - FTP server implementation.
 * @{
 */

/** @todo the following three definitions may move the iprt/types.h later. */
/** FTP server handle. */
typedef R3PTRTYPE(struct RTFTPSERVERINTERNAL *) RTFTPSERVER;
/** Pointer to a FTP server handle. */
typedef RTFTPSERVER                            *PRTFTPSERVER;
/** Nil FTP client handle. */
#define NIL_RTFTPSERVER                         ((RTFTPSERVER)0)

/** Maximum length (in characters) a command can have (without parameters). */
#define RTFTPSERVER_MAX_CMD_LEN                 8

/**
 * Enumeration for defining the current server connection mode.
 */
typedef enum RTFTPSERVER_CONNECTION_MODE
{
    /** Normal mode, nothing to transfer. */
    RTFTPSERVER_CONNECTION_MODE_NORMAL = 0,
    /** Server is in passive mode (is listening). */
    RTFTPSERVER_CONNECTION_MODE_PASSIVE,
    /** Server connects via port to the client. */
    RTFTPSERVER_CONNECTION_MODE_MODE_PORT,
    /** The usual 32-bit hack. */
    RTFTPSERVER_CONNECTION_MODE_32BIT_HACK = 0x7fffffff
} RTFTPSERVER_CONNECTION_MODE;

/**
 * Enumeration for defining the data transfer mode.
 */
typedef enum RTFTPSERVER_TRANSFER_MODE
{
    /** Default if nothing else is set. */
    RTFTPSERVER_TRANSFER_MODE_STREAM = 0,
    RTFTPSERVER_TRANSFER_MODE_BLOCK,
    RTFTPSERVER_TRANSFER_MODE_COMPRESSED,
    /** The usual 32-bit hack. */
    RTFTPSERVER_DATA_MODE_32BIT_HACK = 0x7fffffff
} RTFTPSERVER_DATA_MODE;

/**
 * Enumeration for defining the data type.
 */
typedef enum RTFTPSERVER_DATA_TYPE
{
    /** Default if nothing else is set. */
    RTFTPSERVER_DATA_TYPE_ASCII = 0,
    RTFTPSERVER_DATA_TYPE_EBCDIC,
    RTFTPSERVER_DATA_TYPE_IMAGE,
    RTFTPSERVER_DATA_TYPE_LOCAL,
    /** The usual 32-bit hack. */
    RTFTPSERVER_DATA_TYPE_32BIT_HACK = 0x7fffffff
} RTFTPSERVER_DATA_TYPE;

/**
 * Enumeration for defining the struct type.
 */
typedef enum RTFTPSERVER_STRUCT_TYPE
{
    /** Default if nothing else is set. */
    RTFTPSERVER_STRUCT_TYPE_FILE = 0,
    RTFTPSERVER_STRUCT_TYPE_RECORD,
    RTFTPSERVER_STRUCT_TYPE_PAGE,
    /** The usual 32-bit hack. */
    RTFTPSERVER_STRUCT_TYPE_32BIT_HACK = 0x7fffffff
} RTFTPSERVER_STRUCT_TYPE;

/**
 * Enumeration for FTP server reply codes.
 *
 ** @todo Might needs more codes, not complete yet.
 */
typedef enum RTFTPSERVER_REPLY
{
    /** Invalid reply type, do not use. */
    RTFTPSERVER_REPLY_INVALID                        = 0,
    /** Data connection already open. */
    RTFTPSERVER_REPLY_DATACONN_ALREADY_OPEN          = 125,
    /** Command okay. */
    RTFTPSERVER_REPLY_FILE_STS_OK_OPENING_DATA_CONN  = 150,
    /** Command okay. */
    RTFTPSERVER_REPLY_OKAY                           = 200,
    /** Command not implemented, superfluous at this site. */
    RTFTPSERVER_REPLY_ERROR_CMD_NOT_IMPL_SUPERFLUOUS = 202,
    /** System status report. */
    RTFTPSERVER_REPLY_SYSTEM_STATUS                  = 211,
    /** Service ready for new user. */
    RTFTPSERVER_REPLY_READY_FOR_NEW_USER             = 220,
    /** Service is closing control connection. */
    RTFTPSERVER_REPLY_CLOSING_CTRL_CONN              = 221,
    /** Closing data connection. */
    RTFTPSERVER_REPLY_CLOSING_DATA_CONN              = 226,
    /** Requested file action okay, completed. */
    RTFTPSERVER_REPLY_FILE_ACTION_OKAY_COMPLETED     = 250,
    /** "PATHNAME" ok (created / exists). */
    RTFTPSERVER_REPLY_PATHNAME_OK                    = 257,
    /** User logged in, proceed. */
    RTFTPSERVER_REPLY_LOGGED_IN_PROCEED              = 230,
    /** User name okay, need password. */
    RTFTPSERVER_REPLY_USERNAME_OKAY_NEED_PASSWORD    = 331,
    /** Service not available, closing control connection. */
    RTFTPSERVER_REPLY_SVC_NOT_AVAIL_CLOSING_CTRL_CONN = 421,
    /** Can't open data connection. */
    RTFTPSERVER_REPLY_CANT_OPEN_DATA_CONN            = 425,
    /** Connection closed; transfer aborted. */
    RTFTPSERVER_REPLY_CONN_CLOSED_TRANSFER_ABORTED   = 426,
    /** Requested file action not taken. */
    RTFTPSERVER_REPLY_CONN_REQ_FILE_ACTION_NOT_TAKEN = 450,
    /** Requested action aborted; local error in processing. */
    RTFTPSERVER_REPLY_ACTION_ABORTED_LOCAL_ERROR     = 451,
    /** Syntax error, command unrecognized. */
    RTFTPSERVER_REPLY_ERROR_CMD_NOT_RECOGNIZED       = 500,
    /** Syntax error in parameters or arguments. */
    RTFTPSERVER_REPLY_ERROR_INVALID_PARAMETERS       = 501,
    /** Command not implemented. */
    RTFTPSERVER_REPLY_ERROR_CMD_NOT_IMPL             = 502,
    /** Bad sequence of commands. */
    RTFTPSERVER_REPLY_ERROR_BAD_SEQUENCE             = 503,
    /** Command not implemented for that parameter. */
    RTFTPSERVER_REPLY_ERROR_CMD_NOT_IMPL_PARAM       = 504,
    /** Not logged in. */
    RTFTPSERVER_REPLY_NOT_LOGGED_IN                  = 530,
    /** Requested action not taken. */
    RTFTPSERVER_REPLY_REQ_ACTION_NOT_TAKEN           = 550,
    /** The usual 32-bit hack. */
    RTFTPSERVER_REPLY_32BIT_HACK                     = 0x7fffffff
} RTFTPSERVER_REPLY;

/**
 * Structure for maintaining a FTP server client state.
 */
typedef struct RTFTPSERVERCLIENTSTATE
{
    /** Authenticated user (name). If NULL, no user has been logged in (yet). */
    char                       *pszUser;
    /** Current working directory.
     *  *Always* relative to the server's root directory (which is only is known to the actual implemenation). */
    char                       *pszCWD;
    /** Number of failed login attempts. */
    uint8_t                     cFailedLoginAttempts;
    /** Timestamp (in ms) of last command issued by the client. */
    uint64_t                    tsLastCmdMs;
    /** Current set data type. */
    RTFTPSERVER_DATA_TYPE       enmDataType;
    /** Current set struct type. */
    RTFTPSERVER_STRUCT_TYPE     enmStructType;
} RTFTPSERVERCLIENTSTATE;
/** Pointer to a FTP server client state. */
typedef RTFTPSERVERCLIENTSTATE *PRTFTPSERVERCLIENTSTATE;

/**
 * Structure for storing FTP server callback data.
 */
typedef struct RTFTPCALLBACKDATA
{
    /** Pointer to the client state. */
    PRTFTPSERVERCLIENTSTATE  pClient;
    /** Saved user pointer. */
    void                    *pvUser;
    /** Size (in bytes) of data at user pointer. */
    size_t                   cbUser;
} RTFTPCALLBACKDATA;
/** Pointer to FTP server callback data. */
typedef RTFTPCALLBACKDATA *PRTFTPCALLBACKDATA;

/**
 * Function callback table for the FTP server implementation.
 *
 * All callbacks are optional and therefore can be NULL.
 */
typedef struct RTFTPSERVERCALLBACKS
{
    /**
     * Callback which gets invoked when a user connected.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to generic callback data.
     * @param   pcszUser        User name.
     */
    DECLCALLBACKMEMBER(int, pfnOnUserConnect,(PRTFTPCALLBACKDATA pData, const char *pcszUser));
    /**
     * Callback which gets invoked when a user tries to authenticate with a password.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to generic callback data.
     * @param   pcszUser        User name to authenticate.
     * @param   pcszPassword    Password to authenticate with.
     */
    DECLCALLBACKMEMBER(int, pfnOnUserAuthenticate,(PRTFTPCALLBACKDATA pData, const char *pcszUser, const char *pcszPassword));
    /**
     * Callback which gets invoked when a user disconnected.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to generic callback data.
     * @param   pcszUser        User name which disconnected.
     */
    DECLCALLBACKMEMBER(int, pfnOnUserDisconnect,(PRTFTPCALLBACKDATA pData, const char *pcszUser));
    /**
     * Callback which gets invoked when the client wants to start reading or writing a file.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to generic callback data.
     * @param   pcsszPath       Relative path (to root directory) of file to open.
     * @param   fMode           File mode to use (IPRT stlye).
     * @param   ppvHandle       Opaque file handle only known to the callback implementation.
     */
    DECLCALLBACKMEMBER(int, pfnOnFileOpen,(PRTFTPCALLBACKDATA pData, const char *pcszPath, uint32_t fMode, void **ppvHandle));
    /**
     * Callback which gets invoked when the client wants to read from a file.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to generic callback data.
     * @param   pvHandle        Opaque file handle only known to the callback implementation.
     * @param   pvBuf           Where to store the read file data.
     * @param   cbToRead        How much (in bytes) to read. Must at least supply the size of pvBuf.
     * @param   pcbRead         How much (in bytes) was read. Optional.
     */
    DECLCALLBACKMEMBER(int, pfnOnFileRead,(PRTFTPCALLBACKDATA pData, void *pvHandle, void *pvBuf, size_t cbToRead, size_t *pcbRead));
    /**
     * Callback which gets invoked when the client is done reading from or writing to a file.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to generic callback data.
     * @param   ppvHandle       Opaque file handle only known to the callback implementation.
     */
    DECLCALLBACKMEMBER(int, pfnOnFileClose,(PRTFTPCALLBACKDATA pData, void *pvHandle));
    /**
     * Callback which gets invoked when the client wants to retrieve the size of a specific file.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to generic callback data.
     * @param   pcszPath        Relative path (to root directory) of file to retrieve size for.
     * @param   puSize          Where to store the file size on success.
     */
    DECLCALLBACKMEMBER(int, pfnOnFileGetSize,(PRTFTPCALLBACKDATA pData, const char *pcszPath, uint64_t *puSize));
    /**
     * Callback which gets invoked when the client wants to retrieve information about a file.
     *
     * @param   pData           Pointer to generic callback data.
     * @param   pcszPath        Relative path (to root directory) of file / directory to "stat". Optional.
     *                          If NULL, the current directory will be used.
     * @param   pFsObjInfo      Where to return the RTFSOBJINFO data on success. Optional.
     * @returns VBox status code.
     */
    DECLCALLBACKMEMBER(int, pfnOnFileStat,(PRTFTPCALLBACKDATA pData, const char *pcszPath, PRTFSOBJINFO pFsObjInfo));
    /**
     * Callback which gets invoked when setting the current working directory.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to generic callback data.
     * @param   pcszCWD         Current working directory to set.
     */
    DECLCALLBACKMEMBER(int, pfnOnPathSetCurrent,(PRTFTPCALLBACKDATA pData, const char *pcszCWD));
    /**
     * Callback which gets invoked when a client wants to retrieve the current working directory.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to generic callback data.
     * @param   pszPWD          Where to store the current working directory.
     * @param   cbPWD           Size of buffer in bytes.
     */
    DECLCALLBACKMEMBER(int, pfnOnPathGetCurrent,(PRTFTPCALLBACKDATA pData, char *pszPWD, size_t cbPWD));
    /**
     * Callback which gets invoked when the client wants to move up a directory (relative to the current working directory).
     *
     * @returns VBox status code.
     * @param   pData           Pointer to generic callback data.
     */
    DECLCALLBACKMEMBER(int, pfnOnPathUp,(PRTFTPCALLBACKDATA pData));
    /**
     * Callback which gets invoked when the server wants to open a directory for reading.
     *
     * @returns VBox status code. VERR_NO_MORE_FILES if listing is complete.
     * @param   pData           Pointer to generic callback data.
     * @param   pcszPath        Relative path (to root directory) of file / directory to list. Optional.
     *                          If NULL, the current directory will be listed.
     * @param   ppvHandle       Where to return the opaque directory handle.
     */
    DECLCALLBACKMEMBER(int, pfnOnDirOpen,(PRTFTPCALLBACKDATA pData, const char *pcszPath, void **ppvHandle));
    /**
     * Callback which gets invoked when the server wants to close a directory handle.
     *
     * @returns VBox status code. VERR_NO_MORE_FILES if listing is complete.
     * @param   pData           Pointer to generic callback data.
     * @param   pvHandle        Directory handle to close.
     */
    DECLCALLBACKMEMBER(int, pfnOnDirClose,(PRTFTPCALLBACKDATA pData, void *pvHandle));
    /**
     * Callback which gets invoked when the server wants to read the next directory entry.
     *
     * @returns VBox status code. VERR_NO_MORE_FILES if listing is complete.
     * @param   pData           Pointer to generic callback data.
     * @param   pvHandle        Directory handle to use for reading.
     * @param   pInfo           Where to store the FS object information.
     * @param   ppszEntry       Where to return the allocated string of the entry name.
     * @param   ppszOwner       Where to return the allocated string of the owner.
     * @param   ppszGroup       Where to return the allocated string of the group.
     * @param   ppszTarget      Where to return the allocated string of the target (if a link). Currently unused.
     */
    DECLCALLBACKMEMBER(int, pfnOnDirRead,(PRTFTPCALLBACKDATA pData, void *pvHandle, char **ppszEntry,
                                          PRTFSOBJINFO pInfo, char **ppszOwner, char **ppszGroup, char **ppszTarget));
} RTFTPSERVERCALLBACKS;
/** Pointer to a FTP server callback data table. */
typedef RTFTPSERVERCALLBACKS *PRTFTPSERVERCALLBACKS;

/**
 * Creates a FTP server instance.
 *
 * @returns IPRT status code.
 * @param   phFTPServer         Where to store the FTP server handle.
 * @param   pcszAddress         The address for creating a listening socket.
 *                              If NULL or empty string the server is bound to all interfaces.
 * @param   uPort               The port for creating a listening socket.
 * @param   pCallbacks          Callback table to use.
 * @param   pvUser              Pointer to user-specific data. Optional.
 * @param   cbUser              Size of user-specific data. Optional.
 */
RTR3DECL(int) RTFtpServerCreate(PRTFTPSERVER phFTPServer, const char *pcszAddress, uint16_t uPort,
                                PRTFTPSERVERCALLBACKS pCallbacks, void *pvUser, size_t cbUser);

/**
 * Destroys a FTP server instance.
 *
 * @returns IPRT status code.
 * @param   hFTPServer          Handle to the FTP server handle.
 */
RTR3DECL(int) RTFtpServerDestroy(RTFTPSERVER hFTPServer);

/** @} */

/** @} */
RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_ftp_h */

