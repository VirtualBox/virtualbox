/** @file
 * IPRT - Local IPC Server & Client.
 */

/*
 * Copyright (C) 2006-2025 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_localipc_h
#define IPRT_INCLUDED_localipc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/thread.h>

#ifdef IN_RING0
# error "There are no RTLocalIpc APIs available Ring-0 Host Context!"
#endif


RT_C_DECLS_BEGIN

/** @defgroup grp_rt_localipc   RTLocalIpc - Local IPC
 * @ingroup grp_rt
 * @{
 */

/** Handle to a local IPC server instance. */
typedef struct RTLOCALIPCSERVERINT     *RTLOCALIPCSERVER;
/** Pointer to a local IPC server handle. */
typedef RTLOCALIPCSERVER               *PRTLOCALIPCSERVER;
/** Local IPC server handle nil value. */
#define NIL_RTLOCALIPCSERVER            ((RTLOCALIPCSERVER)0)

/** Handle to a local ICP session instance. */
typedef struct RTLOCALIPCSESSIONINT    *RTLOCALIPCSESSION;
/** Pointer to a local ICP session handle. */
typedef RTLOCALIPCSESSION              *PRTLOCALIPCSESSION;
/** Local ICP session handle nil value. */
#define NIL_RTLOCALIPCSESSION           ((RTLOCALIPCSESSION)0)



/**
 * Create a local IPC server.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success and *phServer containing the instance handle.
 *
 * @param   phServer    Where to put the server instance handle.
 * @param   pszName     The server name.  This must be unique and not include
 *                      any special chars or slashes. It will be morphed into a
 *                      unique platform specific identifier.
 * @param   fFlags      Flags, see RTLOCALIPC_FLAGS_*.
 */
RTDECL(int) RTLocalIpcServerCreate(PRTLOCALIPCSERVER phServer, const char *pszName, uint32_t fFlags);

/** @name RTLocalIpcServerCreate flags
 * @{ */
/** Native name, as apposed to a portable one. */
#define RTLOCALIPC_FLAGS_NATIVE_NAME        RT_BIT_32(0)
/** The mask of valid flags. */
#define RTLOCALIPC_FLAGS_VALID_MASK         UINT32_C(0x00000001)
/** @} */

/**
 * Destroys a local IPC server.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if still other references or NIL.
 * @retval  VINF_OBJECT_DESTROYED if actually destroyed.
 *
 * @param   hServer     The server handle. The nil value is quietly ignored (VINF_SUCCESS).
 */
RTDECL(int) RTLocalIpcServerDestroy(RTLOCALIPCSERVER hServer);

/**
 * Grant the specified group access to the local IPC server socket.
 *
 * @returns IPRT status code.
 * @param   hServer     The server handle.
 * @param   gid         Group ID.
 */
RTDECL(int) RTLocalIpcServerGrantGroupAccess(RTLOCALIPCSERVER hServer, RTGID gid);

/**
 * Set access mode for IPC server socket.
 *
 * @returns IPRT status code.
 * @param   hServer     The server handle.
 * @param   fMode       Access mode.
 */
RTDECL(int) RTLocalIpcServerSetAccessMode(RTLOCALIPCSERVER hServer, RTFMODE fMode);

/**
 * Listen for clients.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success and *phClientSession containing the session handle.
 * @retval  VERR_CANCELLED if the listening was interrupted by RTLocalIpcServerCancel().
 *
 * @param   hServer             The server handle.
 * @param   phClientSession     Where to store the client session handle on success.
 *
 */
RTDECL(int) RTLocalIpcServerListen(RTLOCALIPCSERVER hServer, PRTLOCALIPCSESSION phClientSession);

/**
 * Cancel the current or subsequent RTLocalIpcServerListen call.
 *
 * @returns IPRT status code.
 * @param   hServer     The server handle. The nil value is quietly ignored (VINF_SUCCESS).
 */
RTDECL(int) RTLocalIpcServerCancel(RTLOCALIPCSERVER hServer);


/**
 * Connects to a local IPC server.
 *
 * This is used a client process (or thread).
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success and *phSession holding the session handle.
 *
 * @param   phSession           Where to store the sesson handle on success.
 * @param   pszName             The server name (see RTLocalIpcServerCreate for details).
 * @param   fFlags              Flags, RTLOCALIPC_C_FLAGS_XXX.
 */
RTDECL(int) RTLocalIpcSessionConnect(PRTLOCALIPCSESSION phSession, const char *pszName, uint32_t fFlags);

/** @name RTLOCALIPC_C_FLAGS_XXX - RTLocalIpcSessionConnect flags
 * @{ */
/** Native name, as apposed to a portable one. */
#define RTLOCALIPC_C_FLAGS_NATIVE_NAME      RT_BIT_32(0)
/** The mask of valid flags. */
#define RTLOCALIPC_C_FLAGS_VALID_MASK       UINT32_C(0x00000001)
/** @} */

/**
 * Closes the local IPC session.
 *
 * This can be used with sessions created by both RTLocalIpcSessionConnect
 * and RTLocalIpcServerListen.  It will release one cancel pending I/O and
 * relase one reference (typically the implict reference from the create API).
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if still other references or NIL.
 * @retval  VINF_OBJECT_DESTROYED if session destroyed.
 *
 * @param   hSession            The session handle. The nil value is quietly ignored (VINF_SUCCESS).
 */
RTDECL(int) RTLocalIpcSessionClose(RTLOCALIPCSESSION hSession);

/**
 * Retain a refence to the given session.
 *
 * @returns New reference count, UINT32_MAX if the handle is invalid.
 * @param   hSession            The session handle.
 */
RTDECL(uint32_t) RTLocalIpcSessionRetain(RTLOCALIPCSESSION hSession);

/**
 * Releases a refence to the given session.
 *
 * This differs from RTLocalIpcSessionClose in that it won't cancel any pending
 * I/O.  So, better call RTLocalIpcSessionClose if you want to terminate the
 * session.
 *
 * @returns New reference count, 0 if NIL handle, UINT32_MAX if the handle is
 *          invalid.
 * @param   hSession            The session handle.
 */
RTDECL(uint32_t) RTLocalIpcSessionRelease(RTLOCALIPCSESSION hSession);


/**
 * Receive data from the other end of an local IPC session.
 *
 * This will block if there isn't any data.
 *
 * @returns IPRT status code.
 * @retval  VERR_CANCELLED if the operation was cancelled by RTLocalIpcSessionCancel.
 *
 * @param   hSession            The session handle.
 * @param   pvBuf               Where to store the data.
 * @param   cbToRead            How much to read.  This is exact request if
 *                              pcbRead is NULL, otherwise it's an upper limit.
 * @param   pcbRead             Optional argument for indicating a partial read
 *                              and returning the number of bytes actually read.
 */
RTDECL(int) RTLocalIpcSessionRead(RTLOCALIPCSESSION hSession, void *pvBuf, size_t cbToRead, size_t *pcbRead);

/**
 * Receive pending data from the other end of an local IPC session.
 *
 * This will not block to wait for data.
 *
 * @returns IPRT status code.
 * @retval  VINF_TRY_AGAIN if no pending data (*pcbRead is set to 0).
 * @retval  VERR_CANCELLED if a previous operation was cancelled by
 *          RTLocalIpcSessionCancel (this operation isn't cancellable).
 *
 * @param   hSession            The session handle.
 * @param   pvBuf               Where to store the data.
 * @param   cbToRead            How much to read (upper limit).
 * @param   pcbRead             Where to return exactly how much was read.
 */
RTDECL(int) RTLocalIpcSessionReadNB(RTLOCALIPCSESSION hSession, void *pvBuf, size_t cbToRead, size_t *pcbRead);

/**
 * Send data to the other end of an local IPC session.
 *
 * This may or may not block until the data is received by the other party,
 * this is an implementation detail.  If you want to make sure that the data
 * has been received you should always call RTLocalIpcSessionFlush().
 *
 * @returns IPRT status code.
 * @retval  VERR_CANCELLED if the operation was cancelled by RTLocalIpcSessionCancel.
 *
 * @param   hSession            The session handle.
 * @param   pvBuf               The data to write.
 * @param   cbToWrite           How much to write.
 */
RTDECL(int) RTLocalIpcSessionWrite(RTLOCALIPCSESSION hSession, const void *pvBuf, size_t cbToWrite);

/**
 * Flush any buffered data and (perhaps) wait for the other party to receive it.
 *
 * The waiting for the other party to receive the data is
 * implementation dependent.
 *
 * @returns IPRT status code.
 * @retval  VERR_CANCELLED if the operation was cancelled by RTLocalIpcSessionCancel.
 *
 * @param   hSession            The session handle.
 */
RTDECL(int) RTLocalIpcSessionFlush(RTLOCALIPCSESSION hSession);

/**
 * Wait for data to become ready for reading or for the session to be
 * disconnected.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS when there is data to read.
 * @retval  VERR_TIMEOUT if no data became available within the specified period (@a cMillies)
 * @retval  VERR_BROKEN_PIPE if the session was disconnected.
 * @retval  VERR_CANCELLED if the operation was cancelled by RTLocalIpcSessionCancel.
 *
 * @param   hSession            The session handle.
 * @param   cMillies            The number of milliseconds to wait. Use RT_INDEFINITE_WAIT
 *                              to wait forever.
 *
 * @remark  VERR_INTERRUPTED will not be returned. If this is desired at some later point
 *          add a RTLocalIpcSessionWaitForDataNoResume() variant like we're using elsewhere.
 */
RTDECL(int) RTLocalIpcSessionWaitForData(RTLOCALIPCSESSION hSession, uint32_t cMillies);

/**
 * Cancells a pending or subsequent operation.
 *
 * Not all methods are cancellable, only those which are specfied
 * returning VERR_CANCELLED.  The others are assumed to not be blocking
 * for ever and ever.  However, the cancel is sticky, so the session must
 * basically be trashed (closed) after calling this method.
 *
 * @returns IPRT status code.
 *
 * @param   hSession            The session handle.
 */
RTDECL(int) RTLocalIpcSessionCancel(RTLOCALIPCSESSION hSession);

/**
 * Query the process ID of the other party.
 *
 * This is an optional feature which may not be implemented, so don't
 * depend on it and check for VERR_NOT_SUPPORTED.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS and *pProcess on success.
 * @retval  VERR_CANCELLED if the operation was cancelled by RTLocalIpcSessionCancel.
 * @retval  VERR_NOT_SUPPORTED and *pProcess = NIL_RTPROCESS if not supported.
 *
 * @param   hSession            The session handle.
 * @param   pProcess            Where to store the process ID.
 */
RTDECL(int) RTLocalIpcSessionQueryProcess(RTLOCALIPCSESSION hSession, PRTPROCESS pProcess);

/**
 * Query the user ID of the other party.
 *
 * This is an optional feature which may not be implemented, so don't
 * depend on it and check for VERR_NOT_SUPPORTED.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS and *pUid on success.
 * @retval  VERR_CANCELLED if the operation was cancelled by RTLocalIpcSessionCancel.
 * @retval  VERR_NOT_SUPPORTED and *pUid = NIL_RTUID if not supported.
 *
 * @param   hSession            The session handle.
 * @param   pUid                Where to store the user ID on success.
 */
RTDECL(int) RTLocalIpcSessionQueryUserId(RTLOCALIPCSESSION hSession, PRTUID pUid);

/**
 * Query the group ID of the other party.
 *
 * This is an optional feature which may not be implemented, so don't
 * depend on it and check for VERR_NOT_SUPPORTED.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS and *pUid on success.
 * @retval  VERR_CANCELLED if the operation was cancelled by RTLocalIpcSessionCancel.
 * @retval  VERR_NOT_SUPPORTED and *pGid = NIL_RTUID if not supported.
 *
 * @param   hSession            The session handle.
 * @param   pGid                Where to store the group ID on success.
 */
RTDECL(int) RTLocalIpcSessionQueryGroupId(RTLOCALIPCSESSION hSession, PRTGID pGid);

/** @} */
RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_localipc_h */

