/* $Id: RTProcQueryParent-r3-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Process, Windows.
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
#define LOG_GROUP RTLOGGROUP_PROCESS
#include <iprt/nt/nt.h>

#include <iprt/process.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>



RTR3DECL(int) RTProcQueryParent(RTPROCESS hProcess, PRTPROCESS phParent)
{
    NTSTATUS    rcNt;
    HANDLE      hClose = RTNT_INVALID_HANDLE_VALUE;
    HANDLE      hNtProc;

    /*
     * Open the process.  We take a shortcut if it's the current process.
     */
    if (hProcess == RTProcSelf())
        hNtProc = NtCurrentProcess();
    else
    {
        CLIENT_ID ClientId;
        ClientId.UniqueProcess = (HANDLE)(uintptr_t)hProcess;
        ClientId.UniqueThread  = NULL;

        OBJECT_ATTRIBUTES ObjAttrs;
        InitializeObjectAttributes(&ObjAttrs, NULL, OBJ_CASE_INSENSITIVE, NULL, NULL);

        rcNt = NtOpenProcess(&hClose, PROCESS_QUERY_LIMITED_INFORMATION, &ObjAttrs, &ClientId);
        if (!NT_SUCCESS(rcNt))
            rcNt = NtOpenProcess(&hClose, PROCESS_QUERY_INFORMATION, &ObjAttrs, &ClientId);
        if (!NT_SUCCESS(rcNt))
            return RTErrConvertFromNtStatus(rcNt);
        hNtProc = hClose;
    }

    /*
     * Query the information.
     */
    int rc;
    PROCESS_BASIC_INFORMATION BasicInfo;
    ULONG cbIgn;
    rcNt = NtQueryInformationProcess(hNtProc, ProcessBasicInformation, &BasicInfo, sizeof(BasicInfo), &cbIgn);
    if (NT_SUCCESS(rcNt))
    {
        *phParent = BasicInfo.InheritedFromUniqueProcessId;
        rc = VINF_SUCCESS;
    }
    else
        rc = RTErrConvertFromNtStatus(rcNt);

    /*
     * Clean up.
     */
    if (hClose != RTNT_INVALID_HANDLE_VALUE)
        NtClose(hClose);

    return rc;
}

