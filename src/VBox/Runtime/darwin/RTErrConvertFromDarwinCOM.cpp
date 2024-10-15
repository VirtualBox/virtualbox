/* $Id: RTErrConvertFromDarwinCOM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Convert Darwin COM returns codes to iprt status codes.
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
#include <IOKit/IOCFPlugIn.h>

#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/assert.h>


RTDECL(int) RTErrConvertFromDarwinCOM(int32_t iNativeCode)
{
    /*
     * 'optimized' success case.
     */
    if (iNativeCode == S_OK)
        return VINF_SUCCESS;

    switch (iNativeCode)
    {
        //case E_UNEXPECTED:
        case E_NOTIMPL:             return VERR_NOT_IMPLEMENTED;
        case E_OUTOFMEMORY:         return VERR_NO_MEMORY;
        case E_INVALIDARG:          return VERR_INVALID_PARAMETER;
        //case E_NOINTERFACE:         return VERR_NOT_SUPPORTED;
        case E_POINTER:             return VERR_INVALID_POINTER;
        case E_HANDLE:              return VERR_INVALID_HANDLE;
        //case E_ABORT:               return VERR_CANCELLED;
        case E_FAIL:                return VERR_GENERAL_FAILURE;
        case E_ACCESSDENIED:        return VERR_ACCESS_DENIED;
    }

    /* unknown error. */
    AssertLogRelMsgFailed(("Unhandled error %#x\n", iNativeCode));
    return VERR_UNRESOLVED_ERROR;
}

