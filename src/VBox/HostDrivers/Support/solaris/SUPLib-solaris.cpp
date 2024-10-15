/* $Id: SUPLib-solaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Library - Solaris specific parts.
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
#define LOG_GROUP LOG_GROUP_SUP
#ifdef IN_SUP_HARDENED_R3
# undef DEBUG /* Warning: disables RT_STRICT */
# ifndef LOG_DISABLED
#  define LOG_DISABLED
# endif
# define RTLOG_REL_DISABLED
# include <iprt/log.h>
#endif

#include <VBox/types.h>
#include <VBox/sup.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/path.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include "../SUPLibInternal.h"
#include "../SUPDrvIOC.h"

#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/zone.h>

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <zone.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Solaris device link - system. */
#define DEVICE_NAME_SYS       "/devices/pseudo/vboxdrv@0:vboxdrv"
/** Solaris device link - user. */
#define DEVICE_NAME_USR       "/devices/pseudo/vboxdrv@0:vboxdrvu"
/** Solaris device link - system (non-global zone). */
#define DEVICE_NAME_SYS_ZONE  "/dev/vboxdrv"
/** Solaris device link - user (non-global zone). */
#define DEVICE_NAME_USR_ZONE  "/dev/vboxdrvu"



DECLHIDDEN(int) suplibOsInit(PSUPLIBDATA pThis, bool fPreInited, uint32_t fFlags, SUPINITOP *penmWhat, PRTERRINFO pErrInfo)
{
    /*
     * Nothing to do if pre-inited.
     */
    if (fPreInited)
        return VINF_SUCCESS;

    /*
     * Open dummy files to preallocate file descriptors, see @bugref{4650}.
     */
    for (int i = 0; i < SUPLIB_FLT_DUMMYFILES; i++)
    {
        pThis->ahDummy[i] = -1;
        int hDummy = open("/dev/null", O_RDWR, 0);
        if (hDummy >= 0)
        {
            if (fcntl(hDummy, F_SETFD, FD_CLOEXEC) == 0)
                pThis->ahDummy[i] = hDummy;
            else
            {
                close(hDummy);
                LogRel(("Failed to set close on exec [%d] /dev/null! errno=%d\n", i, errno));
            }
        }
        else
            LogRel(("Failed to open[%d] /dev/null! errno=%d\n", i, errno));
    }

    /*
     * Try to open the device.
     */
    const char *pszDeviceNm;
    if (getzoneid() == GLOBAL_ZONEID)
        pszDeviceNm = fFlags & SUPR3INIT_F_UNRESTRICTED ? DEVICE_NAME_SYS : DEVICE_NAME_USR;
    else
        pszDeviceNm = fFlags & SUPR3INIT_F_UNRESTRICTED ? DEVICE_NAME_SYS_ZONE : DEVICE_NAME_USR_ZONE;
    int hDevice = open(pszDeviceNm, O_RDWR, 0);
    if (hDevice < 0)
    {
        int rc;
        switch (errno)
        {
            case ENODEV:    rc = VERR_VM_DRIVER_LOAD_ERROR; break;
            case EPERM:
            case EACCES:    rc = VERR_VM_DRIVER_NOT_ACCESSIBLE; break;
            case ENOENT:    rc = VERR_VM_DRIVER_NOT_INSTALLED; break;
            default:        rc = VERR_VM_DRIVER_OPEN_ERROR; break;
        }
        LogRel(("Failed to open \"%s\", errno=%d, rc=%Rrc\n", pszDeviceNm, errno, rc));
        return rc;
    }

    /*
     * Mark the file handle close on exec.
     */
    if (fcntl(hDevice, F_SETFD, FD_CLOEXEC) != 0)
    {
#ifdef IN_SUP_HARDENED_R3
        int rc = VERR_INTERNAL_ERROR;
#else
        int err = errno;
        int rc = RTErrConvertFromErrno(err);
        LogRel(("suplibOSInit: setting FD_CLOEXEC failed, errno=%d (%Rrc)\n", err, rc));
#endif
        close(hDevice);
        return rc;
    }

    pThis->hDevice       = hDevice;
    pThis->fUnrestricted = RT_BOOL(fFlags & SUPR3INIT_F_UNRESTRICTED);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) suplibOsTerm(PSUPLIBDATA pThis)
{
    /*
     * Close the dummy files first.
     */
    for (int i = 0; i < SUPLIB_FLT_DUMMYFILES; i++)
    {
        if (pThis->ahDummy[i] != -1)
        {
            close(pThis->ahDummy[i]);
            pThis->ahDummy[i] = -1;
        }
    }

    /*
     * Check if we're initialized
     */
    if (pThis->hDevice != (intptr_t)NIL_RTFILE)
    {
        if (close(pThis->hDevice))
            AssertFailed();
        pThis->hDevice = (intptr_t)NIL_RTFILE;
    }

    return VINF_SUCCESS;
}


#ifndef IN_SUP_HARDENED_R3

DECLHIDDEN(int) suplibOsInstall(void)
{
    return VERR_NOT_IMPLEMENTED;
}

DECLHIDDEN(int) suplibOsUninstall(void)
{
    return VERR_NOT_IMPLEMENTED;
}


DECLHIDDEN(int) suplibOsIOCtl(PSUPLIBDATA pThis, uintptr_t uFunction, void *pvReq, size_t cbReq)
{
    if (RT_LIKELY(ioctl(pThis->hDevice, uFunction, pvReq) >= 0))
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);
}


DECLHIDDEN(int) suplibOsIOCtlFast(PSUPLIBDATA pThis, uintptr_t uFunction, uintptr_t idCpu)
{
    int rc = ioctl(pThis->hDevice, uFunction, idCpu);
    if (rc == -1)
        rc = errno;
    return rc;
}


DECLHIDDEN(int) suplibOsPageAlloc(PSUPLIBDATA pThis, size_t cPages, uint32_t fFlags, void **ppvPages)
{
    RT_NOREF(pThis, fFlags);
    *ppvPages = mmap(NULL, cPages * PAGE_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON, -1, 0);
    if (*ppvPages != (void *)-1)
        return VINF_SUCCESS;
    if (errno == EAGAIN)
        return VERR_NO_MEMORY;
    return RTErrConvertFromErrno(errno);
}


DECLHIDDEN(int) suplibOsPageFree(PSUPLIBDATA pThis, void *pvPages, size_t cPages)
{
    NOREF(pThis);
    munmap(pvPages, cPages * PAGE_SIZE);
    return VINF_SUCCESS;
}

#endif /* !IN_SUP_HARDENED_R3 */

