/* $Id: mempage-native-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - rtMemPageNative*, Windows implementation.
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
#include <iprt/win/windows.h>

#include "internal/iprt.h"
#include <iprt/mem.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include "internal/mem.h"



DECLHIDDEN(int) rtMemPageNativeAlloc(size_t cb, uint32_t fFlags, void **ppvRet)
{
    void *pv = VirtualAlloc(NULL, cb, MEM_COMMIT,
                            !(fFlags & RTMEMPAGEALLOC_F_EXECUTABLE) ? PAGE_READWRITE : PAGE_EXECUTE_READWRITE);
    *ppvRet = pv;
    if (RT_LIKELY(pv != NULL))
        return VINF_SUCCESS;
    return RTErrConvertFromWin32(GetLastError());
}


DECLHIDDEN(int) rtMemPageNativeFree(void *pv, size_t cb)
{
    if (RT_LIKELY(VirtualFree(pv, 0, MEM_RELEASE)))
        return VINF_SUCCESS;
    int rc = RTErrConvertFromWin32(GetLastError());
    AssertMsgFailed(("rc=%d pv=%p cb=%#zx lasterr=%u\n", rc, pv, cb, GetLastError()));
    RT_NOREF(cb);
    return rc;
}


DECLHIDDEN(uint32_t) rtMemPageNativeApplyFlags(void *pv, size_t cb, uint32_t fFlags)
{
    uint32_t fRet = 0;

    if (fFlags & RTMEMPAGEALLOC_F_ADVISE_LOCKED)
    {
        /** @todo check why we get ERROR_WORKING_SET_QUOTA here. */
        BOOL const fOkay = VirtualLock(pv, cb);
        AssertMsg(fOkay || GetLastError() == ERROR_WORKING_SET_QUOTA, ("pv=%p cb=%d lasterr=%d\n", pv, cb, GetLastError()));
        if (fOkay)
            fRet |= RTMEMPAGEALLOC_F_ADVISE_LOCKED;
    }

    /** @todo Any way to apply RTMEMPAGEALLOC_F_ADVISE_NO_DUMP on windows? */

    return fRet;
}


DECLHIDDEN(void) rtMemPageNativeRevertFlags(void *pv, size_t cb, uint32_t fFlags)
{
    if (fFlags & RTMEMPAGEALLOC_F_ADVISE_LOCKED)
    {
        /** @todo check why we get ERROR_NOT_LOCKED here... */
        BOOL const fOkay = VirtualUnlock(pv, cb);
        AssertMsg(fOkay || GetLastError() == ERROR_NOT_LOCKED, ("pv=%p cb=%d lasterr=%d\n", pv, cb, GetLastError()));
        RT_NOREF(fOkay);
    }
}

