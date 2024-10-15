/* $Id: RTErrConvertFromOS2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Convert OS/2 error codes to iprt status codes.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define INCL_ERRORS
#define INCL_DOSERRORS
#include <os2.h>
#undef RT_MAX

#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/assert.h>


RTDECL(int)  RTErrConvertFromOS2(unsigned uNativeCode)
{
    /* very fast check for no error. */
    if (uNativeCode == NO_ERROR)
        return VINF_SUCCESS;

    switch (uNativeCode)
    {
        case ERROR_INVALID_FUNCTION:        return VERR_INVALID_FUNCTION;
        case ERROR_FILE_NOT_FOUND:          return VERR_FILE_NOT_FOUND;
        case ERROR_PATH_NOT_FOUND:          return VERR_PATH_NOT_FOUND;
        case ERROR_TOO_MANY_OPEN_FILES:     return VERR_TOO_MANY_OPEN_FILES;
        case ERROR_ACCESS_DENIED:           return VERR_ACCESS_DENIED;

        case ERROR_INVALID_HANDLE:
        case ERROR_DIRECT_ACCESS_HANDLE:    return VERR_INVALID_HANDLE;

        case ERROR_NOT_ENOUGH_MEMORY:       return VERR_NO_MEMORY;

        case ERROR_INVALID_DRIVE:           return VERR_INVALID_DRIVE;
        case ERROR_CURRENT_DIRECTORY:       return VERR_CANT_DELETE_DIRECTORY;
        case ERROR_NOT_SAME_DEVICE:         return VERR_NOT_SAME_DEVICE;
        case ERROR_NO_MORE_FILES:           return VERR_NO_MORE_FILES;
        case ERROR_WRITE_PROTECT:           return VERR_WRITE_PROTECT;
        case ERROR_BAD_UNIT:                return VERR_IO_BAD_UNIT;
        case ERROR_NOT_READY:               return VERR_IO_NOT_READY;
        case ERROR_BAD_COMMAND:             return VERR_IO_BAD_COMMAND;
        case ERROR_CRC:                     return VERR_IO_CRC;
        case ERROR_BAD_LENGTH:              return VERR_IO_BAD_LENGTH;
        case ERROR_SEEK:                    return VERR_SEEK;
        case ERROR_NOT_DOS_DISK:            return VERR_DISK_INVALID_FORMAT;
        case ERROR_SECTOR_NOT_FOUND:        return VERR_IO_SECTOR_NOT_FOUND;
        case ERROR_WRITE_FAULT:             return VERR_WRITE_ERROR;
        case ERROR_READ_FAULT:              return VERR_READ_ERROR;
        case ERROR_GEN_FAILURE:             return VERR_IO_GEN_FAILURE;
        case ERROR_SHARING_VIOLATION:       return VERR_SHARING_VIOLATION;
        case ERROR_LOCK_VIOLATION:          return VERR_FILE_LOCK_FAILED;
        case ERROR_HANDLE_EOF:              return VERR_EOF;

        case ERROR_HANDLE_DISK_FULL:
        case ERROR_DISK_FULL:               return VERR_DISK_FULL;

        case ERROR_NOT_SUPPORTED:           return VERR_NOT_SUPPORTED;

        case ERROR_INVALID_PARAMETER:
        case ERROR_BAD_ARGUMENTS:
        case ERROR_PMM_INVALID_FLAGS:       return VERR_INVALID_PARAMETER;

        case ERROR_REM_NOT_LIST:            return VERR_NET_IO_ERROR;

        case ERROR_BAD_NETPATH:
        case ERROR_NETNAME_DELETED:         return VERR_NET_HOST_NOT_FOUND;

        case ERROR_BAD_NET_NAME:
        case ERROR_DEV_NOT_EXIST:           return VERR_NET_PATH_NOT_FOUND;

        case ERROR_NETWORK_BUSY:
        case ERROR_TOO_MANY_CMDS:
        case ERROR_TOO_MANY_NAMES:
        case ERROR_TOO_MANY_SESS:
        case ERROR_OUT_OF_STRUCTURES:       return VERR_NET_OUT_OF_RESOURCES;

        case ERROR_PRINTQ_FULL:
        case ERROR_NO_SPOOL_SPACE:
        case ERROR_PRINT_CANCELLED:         return VERR_NET_PRINT_ERROR;

        case ERROR_DUP_NAME:
        case ERROR_ADAP_HDW_ERR:
        case ERROR_BAD_NET_RESP:
        case ERROR_UNEXP_NET_ERR:
        case ERROR_BAD_REM_ADAP:
        case ERROR_NETWORK_ACCESS_DENIED:
        case ERROR_BAD_DEV_TYPE:
        case ERROR_SHARING_PAUSED:
        case ERROR_REQ_NOT_ACCEP:
        case ERROR_REDIR_PAUSED:
        case ERROR_ALREADY_ASSIGNED:
        case ERROR_INVALID_PASSWORD:
        case ERROR_NET_WRITE_FAULT:         return VERR_NET_IO_ERROR;

        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS:          return VERR_ALREADY_EXISTS;

        case ERROR_CANNOT_MAKE:             return VERR_CANT_CREATE;
        case ERROR_NO_PROC_SLOTS:           return VERR_MAX_PROCS_REACHED;
        case ERROR_TOO_MANY_SEMAPHORES:     return VERR_TOO_MANY_SEMAPHORES;
        case ERROR_EXCL_SEM_ALREADY_OWNED:  return VERR_EXCL_SEM_ALREADY_OWNED;
        case ERROR_SEM_IS_SET:              return VERR_SEM_IS_SET;
        case ERROR_TOO_MANY_SEM_REQUESTS:   return VERR_TOO_MANY_SEM_REQUESTS;
        case ERROR_SEM_OWNER_DIED:          return VERR_SEM_OWNER_DIED;
        case ERROR_DRIVE_LOCKED:            return VERR_DRIVE_LOCKED;
        case ERROR_BROKEN_PIPE:             return VERR_BROKEN_PIPE;
        case ERROR_OPEN_FAILED:             return VERR_OPEN_FAILED;

        case ERROR_BUFFER_OVERFLOW:
        case ERROR_INSUFFICIENT_BUFFER:     return VERR_BUFFER_OVERFLOW;

        case ERROR_NO_MORE_SEARCH_HANDLES:  return VERR_NO_MORE_SEARCH_HANDLES;

        case ERROR_SEM_TIMEOUT:
        case ERROR_TIMEOUT:                 return VERR_TIMEOUT;

        case ERROR_INVALID_NAME:
        case ERROR_BAD_PATHNAME:            return VERR_INVALID_NAME;

        case ERROR_NEGATIVE_SEEK:           return VERR_NEGATIVE_SEEK;
        case ERROR_SEEK_ON_DEVICE:          return VERR_SEEK_ON_DEVICE;

        case ERROR_SIGNAL_REFUSED:
        case ERROR_NO_SIGNAL_SENT:          return VERR_SIGNAL_REFUSED;

        case ERROR_SIGNAL_PENDING:          return VERR_SIGNAL_PENDING;
        case ERROR_MAX_THRDS_REACHED:       return VERR_MAX_THRDS_REACHED;
        case ERROR_LOCK_FAILED:             return VERR_FILE_LOCK_FAILED;
        case ERROR_SEM_NOT_FOUND:           return VERR_SEM_NOT_FOUND;
        case ERROR_FILENAME_EXCED_RANGE:    return VERR_FILENAME_TOO_LONG;
        case ERROR_INVALID_SIGNAL_NUMBER:   return VERR_SIGNAL_INVALID;

        case ERROR_BAD_PIPE:                return VERR_BAD_PIPE;
        case ERROR_PIPE_BUSY:               return VERR_PIPE_BUSY;
        case ERROR_NO_DATA:                 return VERR_NO_DATA;
        case ERROR_PIPE_NOT_CONNECTED:      return VERR_PIPE_NOT_CONNECTED;
        case ERROR_MORE_DATA:               return VERR_MORE_DATA;
        case ERROR_NOT_OWNER:               return VERR_NOT_OWNER;
        case ERROR_TOO_MANY_POSTS:          return VERR_TOO_MANY_POSTS;

        case ERROR_INTERRUPT:               return VERR_INTERRUPTED;

        case ERROR_BUSY:                    return VERR_MEMORY_BUSY;
        //case ERROR_NO_UNICODE_TRANSLATION:  return VERR_NO_TRANSLATION;
    }

    /* unknown error. */
    AssertLogRelMsgFailed(("Unhandled error %u\n", uNativeCode));
    return VERR_UNRESOLVED_ERROR;
}

