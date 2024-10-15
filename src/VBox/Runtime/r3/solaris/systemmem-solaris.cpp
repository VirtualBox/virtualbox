/* $Id: systemmem-solaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemQueryTotalRam, Solaris ring-3.
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
#include <iprt/critsect.h>
#include <iprt/once.h>
#include <iprt/param.h>

#include <errno.h>
#include <kstat.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Initialize globals once. */
static RTONCE           g_rtSysMemSolInitOnce = RTONCE_INITIALIZER;
/** Critical section serializing access to g_pKStatCtl and the other handles. */
static RTCRITSECT       g_rtSysMemSolCritSect;
/** The kstate control handle. */
static kstat_ctl_t     *g_pKStatCtl     = NULL;
/** The unix.system_pages handle. */
static kstat_t         *g_pUnixSysPages = NULL;
/** The zfs.archstats handle. */
static kstat_t         *g_pZfsArcStats  = NULL;


/** @callback_method_impl{FNRTONCE} */
static DECLCALLBACK(int) rtSysMemSolInit(void *pvUser)
{
    int rc = RTCritSectInit(&g_rtSysMemSolCritSect);
    if (RT_SUCCESS(rc))
    {
        g_pKStatCtl = kstat_open();
        if (g_pKStatCtl)
        {
            g_pUnixSysPages = kstat_lookup(g_pKStatCtl, (char *)"unix", 0, (char *)"system_pages");
            if (g_pUnixSysPages)
            {
                g_pZfsArcStats = kstat_lookup(g_pKStatCtl, (char *)"zfs",  0, (char *)"arcstats"); /* allow NULL */
                return VINF_SUCCESS;
            }

            rc = RTErrConvertFromErrno(errno);
            kstat_close(g_pKStatCtl);
            g_pKStatCtl = NULL;
        }
        else
            rc = RTErrConvertFromErrno(errno);
    }
    return rc;
}


/** @callback_method_impl{FNRTONCECLEANUP} */
static DECLCALLBACK(void) rtSysMemSolCleanUp(void *pvUser, bool fLazyCleanUpOk)
{
    RTCritSectDelete(&g_rtSysMemSolCritSect);

    kstat_close(g_pKStatCtl);
    g_pKStatCtl     = NULL;
    g_pUnixSysPages = NULL;
    g_pZfsArcStats  = NULL;

    NOREF(pvUser); NOREF(fLazyCleanUpOk);
}



RTDECL(int) RTSystemQueryTotalRam(uint64_t *pcb)
{
    AssertPtrReturn(pcb, VERR_INVALID_POINTER);

    int rc = RTOnceEx(&g_rtSysMemSolInitOnce, rtSysMemSolInit, rtSysMemSolCleanUp, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = RTCritSectEnter(&g_rtSysMemSolCritSect);
        if (RT_SUCCESS(rc))
        {
            if (kstat_read(g_pKStatCtl, g_pUnixSysPages, NULL) != -1)
            {
                kstat_named_t *pData = (kstat_named_t *)kstat_data_lookup(g_pUnixSysPages, (char *)"physmem");
                if (pData)
                    *pcb = (uint64_t)pData->value.ul * PAGE_SIZE;
                else
                    rc = RTErrConvertFromErrno(errno);
            }
            else
                rc = RTErrConvertFromErrno(errno);
            RTCritSectLeave(&g_rtSysMemSolCritSect);
        }
    }
    return rc;
}


RTDECL(int) RTSystemQueryAvailableRam(uint64_t *pcb)
{
    AssertPtrReturn(pcb, VERR_INVALID_POINTER);

    int rc = RTOnceEx(&g_rtSysMemSolInitOnce, rtSysMemSolInit, rtSysMemSolCleanUp, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = RTCritSectEnter(&g_rtSysMemSolCritSect);
        if (RT_SUCCESS(rc))
        {
            if (kstat_read(g_pKStatCtl, g_pUnixSysPages, NULL) != -1)
            {
                kstat_named_t *pData = (kstat_named_t *)kstat_data_lookup(g_pUnixSysPages, (char *)"freemem");
                if (pData)
                {
                    *pcb = (uint64_t)pData->value.ul * PAGE_SIZE;

                    /*
                     * Adjust for ZFS greedyness if possible.
                     * (c_min is the target minimum size of the cache, it is not
                     * an absolute minimum.)
                     */
                    if (   g_pZfsArcStats
                        && kstat_read(g_pKStatCtl, g_pZfsArcStats, NULL) != -1)
                    {
                        kstat_named_t *pCurSize = (kstat_named_t *)kstat_data_lookup(g_pZfsArcStats, (char *)"size");
                        kstat_named_t *pMinSize = (kstat_named_t *)kstat_data_lookup(g_pZfsArcStats, (char *)"c_min");
                        if (   pCurSize
                            && pMinSize
                            && pCurSize->value.ul > pMinSize->value.ul)
                        {
                            *pcb += pCurSize->value.ul - pMinSize->value.ul;
                        }
                    }
                }
                else
                    rc = RTErrConvertFromErrno(errno);
            }

            RTCritSectLeave(&g_rtSysMemSolCritSect);
        }
    }
    return rc;
}

