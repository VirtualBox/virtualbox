/* $Id: fs-stubs-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File System, Generic Stubs.
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
#define LOG_GROUP RTLOGGROUP_FS
#include <iprt/fs.h>
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include "internal/fs.h"



RTR3DECL(int) RTFsQuerySizes(const char *pszFsPath, RTFOFF *pcbTotal, RTFOFF *pcbFree,
                             uint32_t *pcbBlock, uint32_t *pcbSector)
{
    if (pcbTotal)
        *pcbTotal = _2G;
    if (pcbFree)
        *pcbFree = _1G;
    if (pcbBlock)
        *pcbBlock = _4K;
    if (pcbSector)
        *pcbSector = 512;
    LogFlow(("RTFsQuerySizes: success stub!\n"));
    return VINF_SUCCESS;
}


RTR3DECL(int) RTFsQuerySerial(const char *pszFsPath, uint32_t *pu32Serial)
{
    if (pu32Serial)
        *pu32Serial = 0xc0ffee;
    LogFlow(("RTFsQuerySerial: success stub!\n"));
    return VINF_SUCCESS;
}


RTR3DECL(int) RTFsQueryProperties(const char *pszFsPath, PRTFSPROPERTIES pProperties)
{
    pProperties->cbMaxComponent = 255;
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS) || defined(RT_OS_DARWIN)
    pProperties->fCaseSensitive = false;
#else
    pProperties->fCaseSensitive = true;
#endif
    pProperties->fCompressed = false;
    pProperties->fFileCompression = false;
    pProperties->fReadOnly = false;
    pProperties->fRemote = false;
    pProperties->fSupportsUnicode = true;
    LogFlow(("RTFsQueryProperties: success stub!\n"));
    return VINF_SUCCESS;
}


RTR3DECL(bool) RTFsIsCaseSensitive(const char *pszFsPath)
{
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS) || defined(RT_OS_DARWIN)
    return false;
#else
    return true;
#endif
}

