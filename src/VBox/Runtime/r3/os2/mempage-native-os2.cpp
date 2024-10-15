/* $Id: mempage-native-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - rtMemPageNative*, OS/2 implementation.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#define INCL_BASE
#define INCL_ERRORS
#include <os2.h>

#include "internal/iprt.h"
#include <iprt/mem.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/param.h>
#include "internal/mem.h"


DECLHIDDEN(int) rtMemPageNativeAlloc(size_t cb, uint32_t fFlags, void **ppvRet)
{
    ULONG fAlloc = OBJ_ANY | PAG_COMMIT | PAG_READ | PAG_WRITE;
    if (fFlags & RTMEMPAGEALLOC_F_EXECUTABLE)
        fAlloc |= PAG_EXECUTE;
    APIRET rc = DosAllocMem(ppvRet, cb, fAlloc);
    if (rc == NO_ERROR)
        return VINF_SUCCESS;
    return RTErrConvertFromOS2(rc);
}


DECLHIDDEN(int) rtMemPageNativeFree(void *pv, size_t cb)
{
    APIRET rc = DosFreeMem(pv);
    AssertMsgReturn(rc == NO_ERROR, ("rc=%d pv=%p cb=%#zx\n", rc, pv, cb), RTErrConvertFromOS2(rc));
    RT_NOREF(cb);
    return VINF_SUCCESS;
}


DECLHIDDEN(uint32_t) rtMemPageNativeApplyFlags(void *pv, size_t cb, uint32_t fFlags)
{
    uint32_t fRet = 0;
    RT_NOREF(pv, cb, fFlags);
    return fRet;
}


DECLHIDDEN(void) rtMemPageNativeRevertFlags(void *pv, size_t cb, uint32_t fFlags)
{
    RT_NOREF(pv, cb, fFlags);
}

