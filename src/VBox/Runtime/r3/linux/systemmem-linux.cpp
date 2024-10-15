/* $Id: systemmem-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemQueryTotalRam, Linux ring-3.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <iprt/system.h>
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/string.h>

#include <stdio.h>
#include <errno.h>

/* Satisfy compiller warning */
#define __EXPORTED_HEADERS__
#include <sys/sysinfo.h>
#undef __EXPORTED_HEADERS__


RTDECL(int) RTSystemQueryTotalRam(uint64_t *pcb)
{
    AssertPtrReturn(pcb, VERR_INVALID_POINTER);

    struct sysinfo info;
    int rc = sysinfo(&info);
    if (rc == 0)
    {
        *pcb = (uint64_t)info.totalram * info.mem_unit;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
}


RTDECL(int) RTSystemQueryAvailableRam(uint64_t *pcb)
{
    AssertPtrReturn(pcb, VERR_INVALID_POINTER);

    FILE *pFile = fopen("/proc/meminfo", "r");
    if (pFile)
    {
        int rc = VERR_NOT_FOUND;
        uint64_t cbTotal = 0;
        uint64_t cbFree = 0;
        uint64_t cbBuffers = 0;
        uint64_t cbCached = 0;
        char sz[256];
        while (fgets(sz, sizeof(sz), pFile))
        {
            if (!strncmp(sz, RT_STR_TUPLE("MemTotal:")))
                rc = RTStrToUInt64Ex(RTStrStripL(&sz[sizeof("MemTotal:")]), NULL, 0, &cbTotal);
            else if (!strncmp(sz, RT_STR_TUPLE("MemFree:")))
                rc = RTStrToUInt64Ex(RTStrStripL(&sz[sizeof("MemFree:")]), NULL, 0, &cbFree);
            else if (!strncmp(sz, RT_STR_TUPLE("Buffers:")))
                rc = RTStrToUInt64Ex(RTStrStripL(&sz[sizeof("Buffers:")]), NULL, 0, &cbBuffers);
            else if (!strncmp(sz, RT_STR_TUPLE("Cached:")))
                rc = RTStrToUInt64Ex(RTStrStripL(&sz[sizeof("Cached:")]), NULL, 0, &cbCached);
            if (RT_FAILURE(rc))
                break;
        }
        fclose(pFile);
        if (RT_SUCCESS(rc))
        {
            *pcb = (cbFree + cbBuffers + cbCached) * _1K;
            return VINF_SUCCESS;
        }
    }
    /*
     * Fallback (e.g. /proc not mapped) to sysinfo. Less accurat because there
     * is no information about the cached memory. 'Cached:' from above is only
     * accessible through proc :-(
     */
    struct sysinfo info;
    int rc = sysinfo(&info);
    if (rc == 0)
    {
        *pcb = ((uint64_t)info.freeram + info.bufferram) * info.mem_unit;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
}

