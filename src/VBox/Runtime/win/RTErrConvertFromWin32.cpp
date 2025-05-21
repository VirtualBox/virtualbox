/* $Id: RTErrConvertFromWin32.cpp 168950 2025-05-21 08:25:13Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Convert win32 error codes to iprt status codes.
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
#include <iprt/win/windows.h>

#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/assert.h>


RTR3DECL(int)  RTErrConvertFromWin32(unsigned uNativeCode)
{
    /* very fast check for no error. */
    if (uNativeCode == ERROR_SUCCESS)
        return(VINF_SUCCESS);

    /* process error codes. */
    switch (uNativeCode)
    {
        case ERROR_INVALID_FUNCTION:        return VERR_INVALID_FUNCTION;
        case ERROR_FILE_NOT_FOUND:          return VERR_FILE_NOT_FOUND;
        case ERROR_PATH_NOT_FOUND:          return VERR_PATH_NOT_FOUND;
        case ERROR_TOO_MANY_OPEN_FILES:     return VERR_TOO_MANY_OPEN_FILES;
        case ERROR_ACCESS_DENIED:           return VERR_ACCESS_DENIED;
        case ERROR_NOACCESS:                return VERR_INVALID_POINTER; /* (STATUS_ACCESS_VIOLATION, STATUS_DATATYPE_MISALIGNMENT, STATUS_DATATYPE_MISALIGNMENT_ERROR) */

        case ERROR_INVALID_HANDLE:
        case ERROR_DIRECT_ACCESS_HANDLE:    return VERR_INVALID_HANDLE;

        case ERROR_NO_SYSTEM_RESOURCES: /** @todo better translation */
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:             return VERR_NO_MEMORY;

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
        case ERROR_LOCK_VIOLATION:          return VERR_FILE_LOCK_VIOLATION;
        case ERROR_HANDLE_EOF:              return VERR_EOF;
        case ERROR_NOT_LOCKED:              return VERR_FILE_NOT_LOCKED;
        case ERROR_DIR_NOT_EMPTY:           return VERR_DIR_NOT_EMPTY;

        case ERROR_HANDLE_DISK_FULL:
        case ERROR_DISK_FULL:               return VERR_DISK_FULL;

        case ERROR_NOT_SUPPORTED:           return VERR_NOT_SUPPORTED;

        case ERROR_INVALID_PARAMETER:
        case ERROR_BAD_ARGUMENTS:
        case ERROR_INVALID_FLAGS:           return VERR_INVALID_PARAMETER;

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
        case WAIT_TIMEOUT:
        case ERROR_SERVICE_REQUEST_TIMEOUT:
        case ERROR_COUNTER_TIMEOUT:
        case ERROR_TIMEOUT:                 return VERR_TIMEOUT;

        case ERROR_INVALID_NAME:
        case ERROR_BAD_DEVICE:
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

        case ERROR_PIPE_CONNECTED:
        case ERROR_PIPE_LISTENING:          return VERR_PIPE_IO_ERROR;

        case ERROR_OPERATION_ABORTED:       return VERR_INTERRUPTED;
        case ERROR_NO_UNICODE_TRANSLATION:  return VERR_NO_TRANSLATION;

        case RPC_S_INVALID_STRING_UUID:     return VERR_INVALID_UUID_FORMAT;

        case ERROR_PROC_NOT_FOUND:          return VERR_SYMBOL_NOT_FOUND;
        case ERROR_MOD_NOT_FOUND:           return VERR_MODULE_NOT_FOUND;

        case ERROR_INVALID_EXE_SIGNATURE:   return VERR_INVALID_EXE_SIGNATURE;
        case ERROR_BAD_EXE_FORMAT:          return VERR_BAD_EXE_FORMAT;
        case ERROR_FILE_CORRUPT:            return VERR_BAD_EXE_FORMAT;
        case ERROR_RESOURCE_DATA_NOT_FOUND: return VERR_NO_DATA; /// @todo fix ERROR_RESOURCE_DATA_NOT_FOUND translation
        case ERROR_INVALID_ADDRESS:         return VERR_INVALID_POINTER; /// @todo fix ERROR_INVALID_ADDRESS translation - dbghelp returns it on some line number queries.

        case ERROR_CANCELLED:               return VERR_CANCELLED;
        case ERROR_USER_MAPPED_FILE:        return VERR_SHARING_VIOLATION;
        case ERROR_DIRECTORY:               return VERR_NOT_A_DIRECTORY;

        case ERROR_TRUSTED_RELATIONSHIP_FAILURE:
        case ERROR_TRUSTED_DOMAIN_FAILURE:
                                            return VERR_AUTHENTICATION_FAILURE;
        case ERROR_LOGON_FAILURE:           return VERR_AUTHENTICATION_FAILURE;
        case ERROR_PRIVILEGE_NOT_HELD:      return VERR_PRIVILEGE_NOT_HELD;

        case ERROR_PASSWORD_EXPIRED:
        case ERROR_ACCOUNT_RESTRICTION:
        case ERROR_PASSWORD_RESTRICTION:
        case ERROR_ACCOUNT_DISABLED:        return VERR_ACCOUNT_RESTRICTED;

        case ERROR_INVALID_IMAGE_HASH:      return VERR_LDR_IMAGE_HASH;
        case ERROR_UNRECOGNIZED_VOLUME:     return VERR_MEDIA_NOT_RECOGNIZED;
        case ERROR_ELEVATION_REQUIRED:      return VERR_PROC_ELEVATION_REQUIRED;
        case ERROR_SYSTEM_INTEGRITY_POLICY_VIOLATION:   return VERR_SYSTEM_INTEGRITY_POLICY_VIOLATION;

        case ERROR_ENVVAR_NOT_FOUND:        return VERR_ENV_VAR_NOT_FOUND;


        case ERROR_SERVICE_ALREADY_RUNNING: return VERR_ALREADY_LOADED; /* Not the best match, but seen it with VBoxSup.sys. */
        case ERROR_NOT_FOUND:               return VERR_NOT_FOUND;
        case ERROR_INTERNAL_ERROR:          return VERR_INTERNAL_ERROR;


        /*
         * Winsocket errors are mostly BSD errno.h wrappers.
         * This is copied from RTErrConvertFromErrno() and checked against winsock.h.
         * Please, keep things in sync!
         */
#ifdef WSAEPERM
        case WSAEPERM:             return VERR_ACCESS_DENIED;                      /*   1 */
#endif
#ifdef WSAENOENT
        case WSAENOENT:            return VERR_FILE_NOT_FOUND;
#endif
#ifdef WSAESRCH
        case WSAESRCH:             return VERR_PROCESS_NOT_FOUND;
#endif
        case WSAEINTR:             return VERR_INTERRUPTED;
#ifdef WSAEIO
        case WSAEIO:               return VERR_DEV_IO_ERROR;
#endif
#ifdef WSAE2BIG
        case WSAE2BIG:             return VERR_TOO_MUCH_DATA;
#endif
#ifdef WSAENOEXEC
        case WSAENOEXEC:           return VERR_BAD_EXE_FORMAT;
#endif
        case WSAEBADF:             return VERR_INVALID_HANDLE;
#ifdef WSAECHILD
        case WSAECHILD:            return VERR_PROCESS_NOT_FOUND; //...            /*  10 */
#endif
        case WSAEWOULDBLOCK:       return VERR_TRY_AGAIN; /* EAGAIN */
#ifdef WSAENOMEM
        case WSAENOMEM:            return VERR_NO_MEMORY;
#endif
        case WSAEACCES:            return VERR_ACCESS_DENIED;
        case WSAEFAULT:            return VERR_INVALID_POINTER;
        //case WSAENOTBLK:           return VERR_;
#ifdef WSAEBUSY
        case WSAEBUSY:             return VERR_DEV_IO_ERROR;
#endif
#ifdef WSAEEXIST
        case WSAEEXIST:            return VERR_ALREADY_EXISTS;
#endif
        //case WSAEXDEV:
#ifdef WSAENODEV
        case WSAENODEV:            return VERR_NOT_SUPPORTED;
#endif
#ifdef WSAENOTDIR
        case WSAENOTDIR:           return VERR_PATH_NOT_FOUND;                     /*  20 */
#endif
#ifdef WSAEISDIR
        case WSAEISDIR:            return VERR_FILE_NOT_FOUND;
#endif
        case WSAEINVAL:            return VERR_INVALID_PARAMETER;
#ifdef WSAENFILE
        case WSAENFILE:            return VERR_TOO_MANY_OPEN_FILES;
#endif
        case WSAEMFILE:            return VERR_TOO_MANY_OPEN_FILES;
#ifdef WSAENOTTY
        case WSAENOTTY:            return VERR_INVALID_FUNCTION;
#endif
#ifdef WSAETXTBSY
        case WSAETXTBSY:           return VERR_SHARING_VIOLATION;
#endif
        //case WSAEFBIG:
#ifdef WSAENOSPC
        case WSAENOSPC:            return VERR_DISK_FULL;
#endif
#ifdef WSAESPIPE
        case WSAESPIPE:            return VERR_SEEK_ON_DEVICE;
#endif
#ifdef WSAEROFS
        case WSAEROFS:             return VERR_WRITE_PROTECT;                      /*  30 */
#endif
        //case WSAEMLINK:
#ifdef WSAEPIPE
        case WSAEPIPE:             return VERR_BROKEN_PIPE;
#endif
#ifdef WSAEDOM
        case WSAEDOM:              return VERR_INVALID_PARAMETER;
#endif
#ifdef WSAERANGE
        case WSAERANGE:            return VERR_INVALID_PARAMETER;
#endif
#ifdef WSAEDEADLK
        case WSAEDEADLK:           return VERR_DEADLOCK;
#endif
        case WSAENAMETOOLONG:      return VERR_FILENAME_TOO_LONG;
#ifdef WSAENOLCK
        case WSAENOLCK:            return VERR_FILE_LOCK_FAILED;
#endif
#ifdef WSAENOSYS
        case WSAENOSYS:            return VERR_NOT_SUPPORTED;
#endif
        case WSAENOTEMPTY:         return VERR_CANT_DELETE_DIRECTORY;
        case WSAELOOP:             return VERR_TOO_MANY_SYMLINKS;                  /*  40 */
        //case WSAENOMSG                42      /* No message of desired type */
        //case WSAEIDRM                 43      /* Identifier removed */
        //case WSAECHRNG                44      /* Channel number out of range */
        //case WSAEL2NSYNC              45      /* Level 2 not synchronized */
        //case WSAEL3HLT                46      /* Level 3 halted */
        //case WSAEL3RST                47      /* Level 3 reset */
        //case WSAELNRNG                48      /* Link number out of range */
        //case WSAEUNATCH               49      /* Protocol driver not attached */
        //case WSAENOCSI                50      /* No CSI structure available */
        //case WSAEL2HLT                51      /* Level 2 halted */
        //case WSAEBADE                 52      /* Invalid exchange */
        //case WSAEBADR                 53      /* Invalid request descriptor */
        //case WSAEXFULL                54      /* Exchange full */
        //case WSAENOANO                55      /* No anode */
        //case WSAEBADRQC               56      /* Invalid request code */
        //case WSAEBADSLT               57      /* Invalid slot */
        //case 58:
        //case WSAEBFONT                59      /* Bad font file format */
        //case WSAENOSTR                60      /* Device not a stream */
#ifdef WSAENODATA
        case WSAENODATA:           return  VERR_NO_DATA;
#endif
        //case WSAETIME                 62      /* Timer expired */
        //case WSAENOSR                 63      /* Out of streams resources */
#ifdef WSAENONET
        case WSAENONET:            return VERR_NET_NO_NETWORK;
#endif
        //case WSAENOPKG                65      /* Package not installed */
         //case WSAEREMOTE              66      /* Object is remote */
        //case WSAENOLINK               67      /* Link has been severed */
        //case WSAEADV                  68      /* Advertise error */
        //case WSAESRMNT                69      /* Srmount error */
        //case WSAECOMM                 70      /* Communication error on send */
        //case WSAEPROTO                71      /* Protocol error */
        //case WSAEMULTIHOP             72      /* Multihop attempted */
        //case WSAEDOTDOT               73      /* RFS specific error */
        //case WSAEBADMSG               74      /* Not a data message */
#ifdef WSAEOVERFLOW
        case WSAEOVERFLOW:         return VERR_TOO_MUCH_DATA;
#endif
#ifdef WSAENOTUNIQ
        case WSAENOTUNIQ:          return VERR_NET_NOT_UNIQUE_NAME;
#endif
#ifdef WSAEBADFD
        case WSAEBADFD:            return VERR_INVALID_HANDLE;
#endif
        //case WSAEREMCHG               78      /* Remote address changed */
        //case WSAELIBACC               79      /* Can not access a needed shared library */
        //case WSAELIBBAD               80      /* Accessing a corrupted shared library */
        //case WSAELIBSCN               81      /* .lib section in a.out corrupted */
        //case WSAELIBMAX               82      /* Attempting to link in too many shared libraries */
        //case WSAELIBEXEC              83      /* Cannot exec a shared library directly */
#ifdef WSAEILSEQ
        case WSAEILSEQ:            return VERR_NO_TRANSLATION;
#endif
#ifdef WSAERESTART
        case WSAERESTART:          return VERR_INTERRUPTED;
#endif
        //case WSAESTRPIPE              86      /* Streams pipe error */
        //case WSAEUSERS                87      /* Too many users */
        case WSAENOTSOCK:          return VERR_NET_NOT_SOCKET;
        case WSAEDESTADDRREQ:      return VERR_NET_DEST_ADDRESS_REQUIRED;
        case WSAEMSGSIZE:          return VERR_NET_MSG_SIZE;
        case WSAEPROTOTYPE:        return VERR_NET_PROTOCOL_TYPE;
        case WSAENOPROTOOPT:       return VERR_NET_PROTOCOL_NOT_AVAILABLE;
        case WSAEPROTONOSUPPORT:   return VERR_NET_PROTOCOL_NOT_SUPPORTED;
        case WSAESOCKTNOSUPPORT:   return VERR_NET_SOCKET_TYPE_NOT_SUPPORTED;
        case WSAEOPNOTSUPP:        return VERR_NET_OPERATION_NOT_SUPPORTED;
        case WSAEPFNOSUPPORT:      return VERR_NET_PROTOCOL_FAMILY_NOT_SUPPORTED;
        case WSAEAFNOSUPPORT:      return VERR_NET_ADDRESS_FAMILY_NOT_SUPPORTED;
        case WSAEADDRINUSE:        return VERR_NET_ADDRESS_IN_USE;
        case WSAEADDRNOTAVAIL:     return VERR_NET_ADDRESS_NOT_AVAILABLE;
        case WSAENETDOWN:          return VERR_NET_DOWN;
        case WSAENETUNREACH:       return VERR_NET_UNREACHABLE;
        case WSAENETRESET:         return VERR_NET_CONNECTION_RESET;
        case WSAECONNABORTED:      return VERR_NET_CONNECTION_ABORTED;
        case WSAECONNRESET:        return VERR_NET_CONNECTION_RESET_BY_PEER;
        case WSAENOBUFS:           return VERR_NET_NO_BUFFER_SPACE;
        case WSAEISCONN:           return VERR_NET_ALREADY_CONNECTED;
        case WSAENOTCONN:          return VERR_NET_NOT_CONNECTED;
        case WSAESHUTDOWN:         return VERR_NET_SHUTDOWN;
        case WSAETOOMANYREFS:      return VERR_NET_TOO_MANY_REFERENCES;
        case WSAETIMEDOUT:         return VERR_TIMEOUT;
        case WSAECONNREFUSED:      return VERR_NET_CONNECTION_REFUSED;
        case WSAEHOSTDOWN:         return VERR_NET_HOST_DOWN;
        case WSAEHOSTUNREACH:      return VERR_NET_HOST_UNREACHABLE;
        case WSAEALREADY:          return VERR_NET_ALREADY_IN_PROGRESS;
        case WSAEINPROGRESS:       return VERR_NET_IN_PROGRESS;
        case WSAEPROVIDERFAILEDINIT: return VERR_NET_INIT_FAILED;

        //case WSAESTALE                116     /* Stale NFS file handle */
        //case WSAEUCLEAN               117     /* Structure needs cleaning */
        //case WSAENOTNAM               118     /* Not a XENIX named type file */
        //case WSAENAVAIL               119     /* No XENIX semaphores available */
        //case WSAEISNAM                120     /* Is a named type file */
        //case WSAEREMOTEIO             121     /* Remote I/O error */
        case WSAEDQUOT:            return VERR_DISK_FULL;
#ifdef WSAENOMEDIUM
        case WSAENOMEDIUM:         return VERR_MEDIA_NOT_PRESENT;
#endif
#ifdef WSAEMEDIUMTYPE
        case WSAEMEDIUMTYPE:       return VERR_MEDIA_NOT_RECOGNIZED;
#endif
        case WSAEPROCLIM:          return VERR_MAX_PROCS_REACHED;

        //case WSAEDISCON:      (WSABASEERR+101)
        //case WSASYSNOTREADY          (WSABASEERR+91)
        //case WSAVERNOTSUPPORTED      (WSABASEERR+92)
        //case WSANOTINITIALISED       (WSABASEERR+93)

#ifdef WSAHOST_NOT_FOUND
        case WSAHOST_NOT_FOUND:     return VERR_NET_HOST_NOT_FOUND;
#endif
#ifdef WSATRY_AGAIN
        case WSATRY_AGAIN:          return VERR_TRY_AGAIN;
#endif
#ifndef WSANO_RECOVERY
        case WSANO_RECOVERY:        return VERR_IO_GEN_FAILURE;
#endif
#ifdef WSANO_DATA
        case WSANO_DATA:            return VERR_NET_ADDRESS_NOT_AVAILABLE;
#endif

        case 1272 /*STATUS_SMB_GUEST_LOGON_BLOCKED*/: return VERR_AUTHENTICATION_FAILURE;


#ifndef ERROR_NOT_A_REPARSE_POINT
# define ERROR_NOT_A_REPARSE_POINT 0x1126
#endif
        case ERROR_NOT_A_REPARSE_POINT: return VERR_NOT_SYMLINK;

        case NTE_BAD_ALGID:         return VERR_CR_PKIX_UNKNOWN_DIGEST_TYPE;

        case ERROR_SERVICE_DOES_NOT_EXIST: return VERR_NOT_FOUND;

#ifndef STATUS_ELEVATION_REQUIRED
# define STATUS_ELEVATION_REQUIRED 0xc000042c
#endif
        case STATUS_ELEVATION_REQUIRED: return VERR_PRIVILEGE_NOT_HELD;
    }

    /* unknown error. */
#if !defined(IN_SUP_HARDENED_R3) \
 && !defined(IPRT_NO_CRT) /* Please, don't drag log.cpp into the no-CRT images! */
    AssertLogRelMsgFailed(("Unhandled error %u\n", uNativeCode));
#else
    /* hardened main has no LogRel */
    AssertMsgFailed(("Unhandled error %u\n", uNativeCode));
#endif
    return VERR_UNRESOLVED_ERROR;
}

