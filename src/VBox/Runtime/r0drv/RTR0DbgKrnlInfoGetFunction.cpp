/* $Id: RTR0DbgKrnlInfoGetFunction.cpp 109383 2025-05-01 00:26:03Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - RTR0DbgKrnlInfoGetFunction, ring-0 drivers.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/dbg.h>

#include <iprt/errcore.h>


RTR0DECL(PFNRT) RTR0DbgKrnlInfoGetFunction(RTDBGKRNLINFO hKrnlInfo, const char *pszModule, const char *pszSymbol)
{
    void *pvSymbol = NULL;
    int rc = RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, pszModule, pszSymbol, &pvSymbol);
    if (RT_SUCCESS(rc))
#ifdef __cplusplus
        return reinterpret_cast<PFNRT>((uintptr_t)pvSymbol);
#else
        return (PFNRT)pvSymbol;
#endif
    return NULL;
}
RT_EXPORT_SYMBOL(RTR0DbgKrnlInfoGetFunction);

