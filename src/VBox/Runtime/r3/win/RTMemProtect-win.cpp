/* $Id: RTMemProtect-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTMemProtect, Windows.
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
#define LOG_GROUP RTLOGGROUP_MEM
#include <iprt/win/windows.h>

#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/errcore.h>



RTDECL(int) RTMemProtect(void *pv, size_t cb, unsigned fProtect) RT_NO_THROW_DEF
{
    /*
     * Validate input.
     */
    if (cb == 0)
    {
        AssertMsgFailed(("!cb\n"));
        return VERR_INVALID_PARAMETER;
    }
    if (fProtect & ~(RTMEM_PROT_NONE | RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC))
    {
        AssertMsgFailed(("fProtect=%#x\n", fProtect));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Convert the flags.
     */
    int fProt;
    Assert(!RTMEM_PROT_NONE);
    switch (fProtect & (RTMEM_PROT_NONE | RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC))
    {
        case RTMEM_PROT_NONE:
            fProt = PAGE_NOACCESS;
            break;

        case RTMEM_PROT_READ:
            fProt = PAGE_READONLY;
            break;

        case RTMEM_PROT_READ | RTMEM_PROT_WRITE:
            fProt = PAGE_READWRITE;
            break;

        case RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC:
            fProt = PAGE_EXECUTE_READWRITE;
            break;

        case RTMEM_PROT_READ | RTMEM_PROT_EXEC:
            fProt = PAGE_EXECUTE_READWRITE;
            break;

        case RTMEM_PROT_WRITE:
            fProt = PAGE_READWRITE;
            break;

        case RTMEM_PROT_WRITE | RTMEM_PROT_EXEC:
            fProt = PAGE_EXECUTE_READWRITE;
            break;

        case RTMEM_PROT_EXEC:
            fProt = PAGE_EXECUTE_READWRITE;
            break;

        /* If the compiler had any brains it would warn about this case. */
        default:
            AssertMsgFailed(("fProtect=%#x\n", fProtect));
            return VERR_INTERNAL_ERROR;
    }

    /*
     * Align the request.
     */
    cb += (uintptr_t)pv & PAGE_OFFSET_MASK;
    pv = (void *)((uintptr_t)pv & ~(uintptr_t)PAGE_OFFSET_MASK);

    /*
     * Change the page attributes.
     */
    DWORD fFlags = 0;
    if (VirtualProtect(pv, cb, fProt, &fFlags))
        return VINF_SUCCESS;
    return RTErrConvertFromWin32(GetLastError());
}

