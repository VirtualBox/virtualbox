/* $Id: RTPathAbsExDup.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathAbsExDup
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
#include "internal/iprt.h"
#include <iprt/path.h>
#include <iprt/errcore.h>
#include <iprt/param.h>
#include <iprt/string.h>



RTDECL(char *) RTPathAbsExDup(const char *pszBase, const char *pszPath, uint32_t fFlags)
{
    unsigned    cTries    = 16;
    size_t      cbAbsPath = RTPATH_MAX / 2;
    for (;;)
    {
        char  *pszAbsPath = RTStrAlloc(cbAbsPath);
        if (pszAbsPath)
        {
            size_t cbActual = cbAbsPath;
            int rc = RTPathAbsEx(pszBase, pszPath, fFlags, pszAbsPath, &cbActual);
            if (RT_SUCCESS(rc))
            {
                if (cbActual < cbAbsPath / 2)
                    RTStrRealloc(&pszAbsPath, cbActual + 1);
                return pszAbsPath;
            }

            RTStrFree(pszAbsPath);

            if (rc != VERR_BUFFER_OVERFLOW)
                break;

            if (--cTries == 0)
                break;

            cbAbsPath = RT_MAX(RT_ALIGN_Z(cbActual + 16, 64), cbAbsPath + 256);
        }
        else
            break;
    }
    return NULL;
}

