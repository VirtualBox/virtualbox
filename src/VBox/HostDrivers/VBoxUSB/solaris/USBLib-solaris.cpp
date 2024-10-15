/** $Id: USBLib-solaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USBLib - Library for wrapping up the VBoxUSB functionality, Solaris flavor.
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
#include <VBox/usblib.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/process.h>
#include <iprt/env.h>
#include <iprt/path.h>
#include <iprt/string.h>

# include <sys/types.h>
# include <sys/stat.h>
# include <errno.h>
# include <unistd.h>
# include <string.h>
# include <limits.h>
# include <strings.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Logging class. */
#define USBLIBR3    "USBLibR3"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Reference counter. */
static uint32_t volatile g_cUsers = 0;
/** VBoxUSB Device handle. */
static RTFILE g_hFile = NIL_RTFILE;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int usblibDoIOCtl(unsigned iFunction, void *pvData, size_t cbData);


USBLIB_DECL(int) USBLibInit(void)
{
    LogFlow((USBLIBR3 ":USBLibInit\n"));

    /*
     * Already open?
     * This isn't properly serialized, but we'll be fine with the current usage.
     */
    if (g_cUsers)
    {
        ASMAtomicIncU32(&g_cUsers);
        return VINF_SUCCESS;
    }

    RTFILE File;
    int rc = RTFileOpen(&File, VBOXUSB_DEVICE_NAME, RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
    {
        LogRel((USBLIBR3 ":failed to open the VBoxUSB monitor device node '%s' rc=%Rrc\n", VBOXUSB_DEVICE_NAME, rc));
        return rc;
    }
    g_hFile = File;

    ASMAtomicIncU32(&g_cUsers);
    /*
     * Check the USBMonitor version.
     */
    VBOXUSBREQ_GET_VERSION Req;
    bzero(&Req, sizeof(Req));
    rc = usblibDoIOCtl(VBOXUSBMON_IOCTL_GET_VERSION, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
    {
        if (   Req.u32Major != VBOXUSBMON_VERSION_MAJOR
            || Req.u32Minor < VBOXUSBMON_VERSION_MINOR)
        {
            rc = VERR_VERSION_MISMATCH;
            LogRel((USBLIBR3 ":USBMonitor version mismatch! driver v%d.%d, expecting ~v%d.%d\n",
                        Req.u32Major, Req.u32Minor, VBOXUSBMON_VERSION_MAJOR, VBOXUSBMON_VERSION_MINOR));

            RTFileClose(File);
            g_hFile = NIL_RTFILE;
            ASMAtomicDecU32(&g_cUsers);
            return rc;
        }
    }
    else
    {
        LogRel((USBLIBR3 ":USBMonitor driver version query failed. rc=%Rrc\n", rc));
        RTFileClose(File);
        g_hFile = NIL_RTFILE;
        ASMAtomicDecU32(&g_cUsers);
        return rc;
    }

    return VINF_SUCCESS;
}


USBLIB_DECL(int) USBLibTerm(void)
{
    LogFlow((USBLIBR3 ":USBLibTerm\n"));

    if (!g_cUsers)
        return VERR_WRONG_ORDER;
    if (ASMAtomicDecU32(&g_cUsers) != 0)
        return VINF_SUCCESS;

    /*
     * We're the last guy, close down the connection.
     */
    RTFILE File = g_hFile;
    g_hFile = NIL_RTFILE;
    if (File == NIL_RTFILE)
        return VERR_INTERNAL_ERROR;

    int rc = RTFileClose(File);
    AssertRC(rc);
    return rc;
}


USBLIB_DECL(void *) USBLibAddFilter(PCUSBFILTER pFilter)
{
    LogFlow((USBLIBR3 ":USBLibAddFilter pFilter=%p\n", pFilter));

    VBOXUSBREQ_ADD_FILTER Req;
    Req.Filter = *pFilter;
    Req.uId = 0;

    int rc = usblibDoIOCtl(VBOXUSBMON_IOCTL_ADD_FILTER, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        return (void *)Req.uId;

    AssertMsgFailed((USBLIBR3 ":VBOXUSBMON_IOCTL_ADD_FILTER  failed! rc=%Rrc\n", rc));
    return NULL;
}


USBLIB_DECL(void) USBLibRemoveFilter(void *pvId)
{
    LogFlow((USBLIBR3 ":USBLibRemoveFilter pvId=%p\n", pvId));

    VBOXUSBREQ_REMOVE_FILTER Req;
    Req.uId = (uintptr_t)pvId;

    int rc = usblibDoIOCtl(VBOXUSBMON_IOCTL_REMOVE_FILTER, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        return;

    AssertMsgFailed((USBLIBR3 ":VBOXUSBMON_IOCTL_REMOVE_FILTER failed! rc=%Rrc\n", rc));
}


USBLIB_DECL(int) USBLibGetClientInfo(char *pszDeviceIdent, char **ppszClientPath, int *pInstance)
{
    LogFlow((USBLIBR3 ":USBLibGetClientInfo pszDeviceIdent=%s ppszClientPath=%p pInstance=%p\n",
                pszDeviceIdent, ppszClientPath, pInstance));

    AssertPtrReturn(pInstance, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppszClientPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszDeviceIdent, VERR_INVALID_PARAMETER);

    VBOXUSBREQ_CLIENT_INFO Req;
    bzero(&Req, sizeof(Req));
    RTStrPrintf(Req.szDeviceIdent, sizeof(Req.szDeviceIdent), "%s", pszDeviceIdent);

    int rc = usblibDoIOCtl(VBOXUSBMON_IOCTL_CLIENT_INFO, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
    {
        *pInstance = Req.Instance;
        rc = RTStrDupEx(ppszClientPath, Req.szClientPath);
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;

        LogRel((USBLIBR3 ":USBLibGetClientInfo RTStrDupEx failed! rc=%Rrc szClientPath=%s\n", rc, Req.szClientPath));
    }
    else
        LogRel((USBLIBR3 ":USBLibGetClientInfo VBOXUSBMON_IOCTL_CLIENTPATH failed! rc=%Rrc\n", rc));

    return rc;
}


USBLIB_DECL(int) USBLibResetDevice(char *pszDevicePath, bool fReattach)
{
    LogFlow((USBLIBR3 ":USBLibResetDevice pszDevicePath=%s\n", pszDevicePath));

    size_t cbPath = strlen(pszDevicePath) + 1;
    size_t cbReq  = sizeof(VBOXUSBREQ_RESET_DEVICE) + cbPath;
    VBOXUSBREQ_RESET_DEVICE *pReq = (VBOXUSBREQ_RESET_DEVICE *)RTMemTmpAllocZ(cbReq);
    if (RT_UNLIKELY(!pReq))
        return VERR_NO_MEMORY;

    pReq->fReattach = fReattach;
    if (strlcpy(pReq->szDevicePath, pszDevicePath, cbPath) >= cbPath)
    {
        LogRel((USBLIBR3 ":USBLibResetDevice buffer overflow. cbPath=%u pszDevicePath=%s\n", cbPath, pszDevicePath));
        return VERR_BUFFER_OVERFLOW;
    }

    int rc = usblibDoIOCtl(VBOXUSBMON_IOCTL_RESET_DEVICE, pReq, cbReq);
    if (RT_FAILURE(rc))
        LogRel((USBLIBR3 ":VBOXUSBMON_IOCTL_RESET_DEVICE failed! rc=%Rrc\n", rc));

    RTMemFree(pReq);
    return rc;
}


static int usblibDoIOCtl(unsigned iFunction, void *pvData, size_t cbData)
{
    if (g_hFile == NIL_RTFILE)
    {
        LogRel((USBLIBR3 ":IOCtl failed, device not open.\n"));
        return VERR_FILE_NOT_FOUND;
    }

    VBOXUSBREQ Hdr;
    Hdr.u32Magic = VBOXUSBMON_MAGIC;
    Hdr.cbData = cbData;    /* Don't include full size because the header size is fixed. */
    Hdr.pvDataR3 = pvData;

    int rc = ioctl(RTFileToNative(g_hFile), iFunction, &Hdr);
    if (rc < 0)
    {
        rc = errno;
        LogRel((USBLIBR3 ":IOCtl failed iFunction=%x errno=%d g_file=%d\n", iFunction, rc, RTFileToNative(g_hFile)));
        return RTErrConvertFromErrno(rc);
    }

    rc = Hdr.rc;
    if (RT_UNLIKELY(RT_FAILURE(rc)))
        LogRel((USBLIBR3 ":Function (%x) failed. rc=%Rrc\n", iFunction, rc));

    return rc;
}
