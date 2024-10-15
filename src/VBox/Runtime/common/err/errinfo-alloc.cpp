/* $Id: errinfo-alloc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Error Info, Allocators.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include "internal/iprt.h"
#include <iprt/err.h>

#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>



RTDECL(PRTERRINFO)  RTErrInfoAlloc(size_t cbMsg)
{
    PRTERRINFO pErrInfo;
    RTErrInfoAllocEx(cbMsg, &pErrInfo);
    return pErrInfo;
}


RTDECL(int)         RTErrInfoAllocEx(size_t cbMsg, PRTERRINFO *ppErrInfo)
{
    if (cbMsg == 0)
        cbMsg = _4K;
    else
        cbMsg = RT_ALIGN_Z(cbMsg, 256);

    PRTERRINFO pErrInfo;
    *ppErrInfo = pErrInfo = (PRTERRINFO)RTMemTmpAlloc(sizeof(*pErrInfo) + cbMsg);
    if (RT_UNLIKELY(!pErrInfo))
        return VERR_NO_TMP_MEMORY;

    RTErrInfoInit(pErrInfo, (char *)(pErrInfo + 1), cbMsg);
    pErrInfo->fFlags = RTERRINFO_FLAGS_T_ALLOC | RTERRINFO_FLAGS_MAGIC;
    return VINF_SUCCESS;
}


RTDECL(void)        RTErrInfoFree(PRTERRINFO pErrInfo)
{
    RTMemTmpFree(pErrInfo);
}

