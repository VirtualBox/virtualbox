/* $Id: RTFileReadAllEx-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTFileReadAllEx, generic implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <iprt/file.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>


RTDECL(int) RTFileReadAllEx(const char *pszFilename, RTFOFF off, RTFOFF cbMax, uint32_t fFlags, void **ppvFile, size_t *pcbFile)
{
    AssertReturn(!(fFlags & ~RTFILE_RDALL_VALID_MASK), VERR_INVALID_PARAMETER);

    RTFILE File;
    int rc = RTFileOpen(&File, pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | (fFlags & RTFILE_RDALL_O_DENY_MASK));
    if (RT_SUCCESS(rc))
    {
        rc = RTFileReadAllByHandleEx(File, off, cbMax, fFlags, ppvFile, pcbFile);
        RTFileClose(File);
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTFileReadAllEx);

