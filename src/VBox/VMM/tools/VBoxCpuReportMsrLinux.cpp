/* $Id: VBoxCpuReportMsrLinux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MsrLinux - Linux-specific MSR access.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "VBoxCpuReport.h"

#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/thread.h>

#ifndef RT_OS_WINDOWS
# include <unistd.h>
#else /* RT_OS_WINDOWS: for test compiling this file on windows */
# include <io.h>
int pread(int, void *, size_t, off_t);
int pwrite(int, void const *, size_t, off_t);
#endif
#include <fcntl.h>
#include <errno.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define MSR_DEV_NAME    "/dev/cpu/0/msr"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The /dev/xxx/msr file descriptor. */
static int              g_fdMsr;


/**
 * @interface_method_impl{VBCPUREPMSRACCESSORS,pfnMsrProberRead}
 */
static DECLCALLBACK(int) linuxMsrProberRead(uint32_t uMsr, RTCPUID idCpu, uint64_t *puValue, bool *pfGp)
{
    int  rc = VINF_SUCCESS;

    if (idCpu != NIL_RTCPUID)
        return VERR_INVALID_PARAMETER;

    if (g_fdMsr < 0)
        return VERR_INVALID_STATE;

    *pfGp = true;
    if (pread(g_fdMsr, puValue, sizeof(*puValue), uMsr) != sizeof(*puValue))
        rc = VERR_READ_ERROR;
    else
        *pfGp = false;

    return RT_SUCCESS(rc) && !pfGp;
}


/**
 * @interface_method_impl{VBCPUREPMSRACCESSORS,pfnMsrProberWrite}
 */
static DECLCALLBACK(int) linuxMsrProberWrite(uint32_t uMsr, RTCPUID idCpu, uint64_t uValue, bool *pfGp)
{
    int  rc = VINF_SUCCESS;

    if (idCpu != NIL_RTCPUID)
        return VERR_INVALID_PARAMETER;

    if (g_fdMsr < 0)
        return VERR_INVALID_STATE;

    *pfGp = true;
    if (pwrite(g_fdMsr, &uValue, sizeof(uValue), uMsr) != sizeof(uValue))
        rc = VERR_WRITE_ERROR;
    else
        *pfGp = false;

    return RT_SUCCESS(rc) && !pfGp;
}

/**
 * @interface_method_impl{VBCPUREPMSRACCESSORS,pfnMsrProberModify}
 */
static DECLCALLBACK(int) linuxMsrProberModify(uint32_t uMsr, RTCPUID idCpu, uint64_t fAndMask, uint64_t fOrMask,
                                              PSUPMSRPROBERMODIFYRESULT pResult)
{
    int         rc = VINF_SUCCESS;
    uint64_t    uBefore, uWrite, uAfter;
    int         rcBefore, rcWrite, rcAfter, rcRestore;

    if (idCpu != NIL_RTCPUID)
        return VERR_INVALID_PARAMETER;

    if (g_fdMsr < 0)
        return VERR_INVALID_STATE;

#if 0
    vbCpuRepDebug("MSR %#x\n", uMsr);
    RTThreadSleep(10);
#endif
    rcBefore = pread(g_fdMsr, &uBefore, sizeof(uBefore), uMsr);
    uWrite = (uBefore & fAndMask) | fOrMask;
    rcWrite = pwrite(g_fdMsr, &uWrite, sizeof(uWrite), uMsr);
    rcAfter = pread(g_fdMsr, &uAfter, sizeof(uAfter), uMsr);
    rcRestore = pwrite(g_fdMsr, &uBefore, sizeof(uBefore), uMsr);

#if 0
    vbCpuRepDebug("MSR: %#x, %#llx -> %#llx -> %#llx (%d/%d/%d/%d)\n",
                  uMsr, uBefore, uWrite, uAfter,
                  rcBefore, rcWrite != sizeof(uWrite), rcAfter, rcRestore);
#endif
    pResult->uBefore    = uBefore;
    pResult->uWritten   = uWrite;
    pResult->uAfter     = uAfter;
    pResult->fBeforeGp  = rcBefore  != sizeof(uBefore);
    pResult->fModifyGp  = rcWrite   != sizeof(uWrite);
    pResult->fAfterGp   = rcAfter   != sizeof(uAfter);
    pResult->fRestoreGp = rcRestore != sizeof(uBefore);

    return rc;
}


/**
 * @interface_method_impl{VBCPUREPMSRACCESSORS,pfnTerm}
 */
static DECLCALLBACK(void) linuxMsrProberTerm(void)
{
    if (g_fdMsr >= 0)
    {
        close(g_fdMsr);
        g_fdMsr = -1;
    }
}

int VbCpuRepMsrProberInitPlatform(PVBCPUREPMSRACCESSORS pMsrAccessors)
{
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, MSR_DEV_NAME, RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN);
    if (RT_SUCCESS(rc))
    {
        g_fdMsr = RTFileToNative(hFile);
        Assert(g_fdMsr != -1);

        pMsrAccessors->fAtomic             = false; /* Can't modify/restore MSRs without trip to R3. */
        pMsrAccessors->pfnMsrProberRead    = linuxMsrProberRead;
        pMsrAccessors->pfnMsrProberWrite   = linuxMsrProberWrite;
        pMsrAccessors->pfnMsrProberModify  = linuxMsrProberModify;
        pMsrAccessors->pfnTerm             = linuxMsrProberTerm;
        return VINF_SUCCESS;
    }
    vbCpuRepDebug("warning: Failed to open " MSR_DEV_NAME ": %Rrc\n", rc);
    return rc;
}
