/* $Id: RTErrConvertFromNtStatus.cpp 168950 2025-05-21 08:25:13Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Convert NT status codes to iprt status codes.
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
#include <ntstatus.h>
typedef long NTSTATUS;                  /** @todo figure out which headers to include to get this one typedef... */

#include <iprt/err.h>
#ifdef VBOX
# include <VBox/err.h>
#endif
#include <iprt/log.h>
#include <iprt/assert.h>



RTDECL(int)  RTErrConvertFromNtStatus(long lNativeCode)
{
    switch (lNativeCode)
    {
        case STATUS_SUCCESS:                return VINF_SUCCESS;

        case STATUS_ALERTED:                return VERR_INTERRUPTED;
        case STATUS_USER_APC:               return VERR_INTERRUPTED;

        case STATUS_INVALID_INFO_CLASS:     return VERR_INVALID_FUNCTION;
        case STATUS_DATATYPE_MISALIGNMENT:  return VERR_INVALID_POINTER;
        case STATUS_NO_MORE_FILES:          return VERR_NO_MORE_FILES;
        case STATUS_NO_MORE_ENTRIES:        return VERR_NO_MORE_FILES;
        case STATUS_NO_MEMORY:              return VERR_NO_MEMORY;

        case STATUS_INVALID_HANDLE:         return VERR_INVALID_HANDLE;
        case STATUS_INVALID_PARAMETER:      return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_1:    return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_2:    return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_3:    return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_4:    return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_5:    return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_6:    return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_7:    return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_8:    return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_9:    return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_10:   return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_11:   return VERR_INVALID_PARAMETER;
        case STATUS_INVALID_PARAMETER_12:   return VERR_INVALID_PARAMETER;
        case STATUS_NO_SUCH_DEVICE:         return VERR_FILE_NOT_FOUND;
        case STATUS_NO_SUCH_FILE:           return VERR_FILE_NOT_FOUND;
        case STATUS_INVALID_DEVICE_REQUEST: return VERR_IO_BAD_COMMAND;
        case STATUS_ACCESS_DENIED:          return VERR_ACCESS_DENIED;
        case STATUS_OBJECT_TYPE_MISMATCH:   return VERR_UNEXPECTED_FS_OBJ_TYPE;
        case STATUS_OBJECT_NAME_INVALID:    return VERR_INVALID_NAME;
        case STATUS_OBJECT_NAME_NOT_FOUND:  return VERR_FILE_NOT_FOUND;
        case STATUS_OBJECT_PATH_INVALID:    return VERR_INVALID_NAME;
        case STATUS_OBJECT_PATH_NOT_FOUND:  return VERR_PATH_NOT_FOUND;
        case STATUS_OBJECT_PATH_SYNTAX_BAD: return VERR_INVALID_NAME;
        case STATUS_BAD_NETWORK_PATH:       return VERR_NET_PATH_NOT_FOUND;
        case STATUS_NOT_A_DIRECTORY:        return VERR_NOT_A_DIRECTORY;
        case STATUS_DIRECTORY_NOT_EMPTY:    return VERR_DIR_NOT_EMPTY;
        case STATUS_SHARING_VIOLATION:      return VERR_SHARING_VIOLATION;
        case STATUS_NO_MEDIA_IN_DEVICE:     return VERR_DRIVE_IS_EMPTY;
        case STATUS_ACCESS_VIOLATION:       return VERR_INVALID_POINTER;
        case STATUS_FILE_IS_A_DIRECTORY:    return VERR_IS_A_DIRECTORY;

        case STATUS_REPARSE_POINT_NOT_RESOLVED:
                                            return VERR_TOO_MANY_SYMLINKS;
        case STATUS_UNEXPECTED_NETWORK_ERROR:
                                            return VERR_NET_IO_ERROR;
        case STATUS_INVALID_IMAGE_HASH:     return VERR_LDR_IMAGE_HASH;
        case STATUS_LOGON_FAILURE:          return VERR_AUTHENTICATION_FAILURE;
        case STATUS_INTERNAL_ERROR:         return VERR_INTERNAL_ERROR;
#ifdef VBOX
        case STATUS_TRUST_FAILURE:          return VERR_SUPLIB_NT_PROCESS_UNTRUSTED_5;
#endif
    }

    /* unknown error. */
#ifndef IN_SUP_HARDENED_R3
    AssertLogRelMsgFailed(("Unhandled error %#lx (%lu)\n", lNativeCode, lNativeCode));
#else
    /* hardened main has no LogRel */
    AssertMsgFailed(("Unhandled error %#lx (%lu)\n", lNativeCode, lNativeCode));
#endif
    return VERR_UNRESOLVED_ERROR;
}

