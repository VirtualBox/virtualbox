/* $Id: VBoxDev-haiku.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuest kernel driver, Haiku Guest Additions, implementation.
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

/*
 * This code is based on:
 *
 * VirtualBox Guest Additions for Haiku.
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    François Revol <revol@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <sys/param.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <OS.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <PCI.h>

#include "VBoxGuest-haiku.h"
#include "VBoxGuestInternal.h"
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/process.h>
#include <iprt/mem.h>
#include <iprt/asm.h>

#define DRIVER_NAME "vboxdev"
#define DEVICE_NAME "misc/vboxguest"
#define MODULE_NAME "generic/vboxguest"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
int32 api_version = B_CUR_DRIVER_API_VERSION;


/**
 * Driver open hook.
 *
 * @param name          The name of the device as returned by publish_devices.
 * @param flags         Open flags.
 * @param cookie        Where to store the session pointer.
 *
 * @return Haiku status code.
 */
static status_t vgdrvHaikuOpen(const char *name, uint32 flags, void **cookie)
{
    int rc;
    PVBOXGUESTSESSION pSession;

    LogFlow((DRIVER_NAME ":vgdrvHaikuOpen\n"));

    /*
     * Create a new session.
     */
    rc = VGDrvCommonCreateUserSession(&g_DevExt, VMMDEV_REQUESTOR_USERMODE, &pSession);
    if (RT_SUCCESS(rc))
    {
        Log((DRIVER_NAME ":vgdrvHaikuOpen success: g_DevExt=%p pSession=%p rc=%d pid=%d\n",&g_DevExt, pSession, rc,(int)RTProcSelf()));
        ASMAtomicIncU32(&cUsers);
        *cookie = pSession;
        return B_OK;
    }

    LogRel((DRIVER_NAME ":vgdrvHaikuOpen: failed. rc=%d\n", rc));
    return RTErrConvertToErrno(rc);
}


/**
 * Driver close hook.
 * @param cookie        The session.
 *
 * @return Haiku status code.
 */
static status_t vgdrvHaikuClose(void *cookie)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;
    Log(("vgdrvHaikuClose: pSession=%p\n", pSession));

    /** @todo r=ramshankar: should we really be using the session spinlock here? */
    RTSpinlockAcquire(g_DevExt.SessionSpinlock);

    /** @todo we don't know if it belongs to this session!! */
    if (sState.selectSync)
    {
        //dprintf(DRIVER_NAME "close: unblocking select %p %x\n", sState.selectSync, sState.selectEvent);
        notify_select_event(sState.selectSync, sState.selectEvent);
        sState.selectEvent = (uint8_t)0;
        sState.selectRef = (uint32_t)0;
        sState.selectSync = (void *)NULL;
    }

    RTSpinlockRelease(g_DevExt.SessionSpinlock);
    return B_OK;
}


/**
 * Driver free hook.
 * @param cookie        The session.
 *
 * @return Haiku status code.
 */
static status_t vgdrvHaikuFree(void *cookie)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;
    Log(("vgdrvHaikuFree: pSession=%p\n", pSession));

    /*
     * Close the session if it's still hanging on to the device...
     */
    if (RT_VALID_PTR(pSession))
    {
        VGDrvCommonCloseSession(&g_DevExt, pSession);
        ASMAtomicDecU32(&cUsers);
    }
    else
        Log(("vgdrvHaikuFree: si_drv1=%p!\n", pSession));
    return B_OK;
}


/**
 * Driver IOCtl entry.
 * @param cookie        The session.
 * @param op            The operation to perform.
 * @param data          The data associated with the operation.
 * @param len           Size of the data in bytes.
 *
 * @return Haiku status code.
 */
static status_t vgdrvHaikuIOCtl(void *cookie, uint32 op, void *data, size_t len)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;
    int rc;
    Log(("vgdrvHaikuIOCtl: cookie=%p op=0x%08x data=%p len=%lu)\n", cookie, op, data, len));

    /*
     * Validate the input.
     */
    if (RT_UNLIKELY(!RT_VALID_PTR(pSession)))
        return EINVAL;

    /*
     * Validate the request wrapper.
     */
#if 0
    if (IOCPARM_LEN(ulCmd) != sizeof(VBGLBIGREQ))
    {
        Log((DRIVER_NAME ": vgdrvHaikuIOCtl: bad request %lu size=%lu expected=%d\n", ulCmd, IOCPARM_LEN(ulCmd),
                                                                                        sizeof(VBGLBIGREQ)));
        return ENOTTY;
    }
#endif

    if (RT_UNLIKELY(len > _1M * 16))
    {
        dprintf(DRIVER_NAME ": vgdrvHaikuIOCtl: bad size %#x; pArg=%p Cmd=%lu.\n", (unsigned)len, data, op);
        return EINVAL;
    }

    /*
     * Read the request.
     */
    void *pvBuf = NULL;
    if (RT_LIKELY(len > 0))
    {
        pvBuf = RTMemTmpAlloc(len);
        if (RT_UNLIKELY(!pvBuf))
        {
            LogRel((DRIVER_NAME ":vgdrvHaikuIOCtl: RTMemTmpAlloc failed to alloc %d bytes.\n", len));
            return ENOMEM;
        }

        /** @todo r=ramshankar: replace with RTR0MemUserCopyFrom() */
        rc = user_memcpy(pvBuf, data, len);
        if (RT_UNLIKELY(rc < 0))
        {
            RTMemTmpFree(pvBuf);
            LogRel((DRIVER_NAME ":vgdrvHaikuIOCtl: user_memcpy failed; pvBuf=%p data=%p op=%d. rc=%d\n", pvBuf, data, op, rc));
            return EFAULT;
        }
        if (RT_UNLIKELY(!RT_VALID_PTR(pvBuf)))
        {
            RTMemTmpFree(pvBuf);
            LogRel((DRIVER_NAME ":vgdrvHaikuIOCtl: pvBuf invalid pointer %p\n", pvBuf));
            return EINVAL;
        }
    }
    Log(("vgdrvHaikuIOCtl: pSession=%p pid=%d.\n", pSession,(int)RTProcSelf()));

    /*
     * Process the IOCtl.
     */
    size_t cbDataReturned;
    rc = VGDrvCommonIoCtl(op, &g_DevExt, pSession, pvBuf, len, &cbDataReturned);
    if (RT_SUCCESS(rc))
    {
        rc = 0;
        if (RT_UNLIKELY(cbDataReturned > len))
        {
            Log(("vgdrvHaikuIOCtl: too much output data %d expected %d\n", cbDataReturned, len));
            cbDataReturned = len;
        }
        if (cbDataReturned > 0)
        {
            rc = user_memcpy(data, pvBuf, cbDataReturned);
            if (RT_UNLIKELY(rc < 0))
            {
                Log(("vgdrvHaikuIOCtl: user_memcpy failed; pvBuf=%p pArg=%p Cmd=%lu. rc=%d\n", pvBuf, data, op, rc));
                rc = EFAULT;
            }
        }
    }
    else
    {
        Log(("vgdrvHaikuIOCtl: VGDrvCommonIoCtl failed. rc=%d\n", rc));
        rc = EFAULT;
    }
    RTMemTmpFree(pvBuf);
    return rc;
}


/**
 * Driver select hook.
 *
 * @param cookie        The session.
 * @param event         The event.
 * @param ref           ???
 * @param sync          ???
 *
 * @return Haiku status code.
 */
static status_t vgdrvHaikuSelect(void *cookie, uint8 event, uint32 ref, selectsync *sync)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;
    status_t err = B_OK;

    switch (event)
    {
        case B_SELECT_READ:
            break;
        default:
            return EINVAL;
    }

    RTSpinlockAcquire(g_DevExt.SessionSpinlock);

    uint32_t u32CurSeq = ASMAtomicUoReadU32(&g_DevExt.u32MousePosChangedSeq);
    if (pSession->u32MousePosChangedSeq != u32CurSeq)
    {
        pSession->u32MousePosChangedSeq = u32CurSeq;
        notify_select_event(sync, event);
    }
    else if (sState.selectSync == NULL)
    {
        sState.selectEvent = (uint8_t)event;
        sState.selectRef = (uint32_t)ref;
        sState.selectSync = (void *)sync;
    }
    else
        err = B_WOULD_BLOCK;

    RTSpinlockRelease(g_DevExt.SessionSpinlock);

    return err;
}


/**
 * Driver deselect hook.
 * @param cookie        The session.
 * @param event         The event.
 * @param sync          ???
 *
 * @return Haiku status code.
 */
static status_t vgdrvHaikuDeselect(void *cookie, uint8 event, selectsync *sync)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;
    status_t err = B_OK;
    //dprintf(DRIVER_NAME "deselect(,%d,%p)\n", event, sync);

    RTSpinlockAcquire(g_DevExt.SessionSpinlock);

    if (sState.selectSync == sync)
    {
        //dprintf(DRIVER_NAME "deselect: dropping: %p %x\n", sState.selectSync, sState.selectEvent);
        sState.selectEvent = (uint8_t)0;
        sState.selectRef = (uint32_t)0;
        sState.selectSync = NULL;
    }
    else
        err = B_OK;

    RTSpinlockRelease(g_DevExt.SessionSpinlock);
    return err;
}


/**
 * Driver write hook.
 * @param cookie            The session.
 * @param position          The offset.
 * @param data              Pointer to the data.
 * @param numBytes          Where to store the number of bytes written.
 *
 * @return Haiku status code.
 */
static status_t vgdrvHaikuWrite(void *cookie, off_t position, const void *data, size_t *numBytes)
{
    *numBytes = 0;
    return B_OK;
}


/**
 * Driver read hook.
 * @param cookie            The session.
 * @param position          The offset.
 * @param data              Pointer to the data.
 * @param numBytes          Where to store the number of bytes read.
 *
 * @return Haiku status code.
 */
static status_t vgdrvHaikuRead(void *cookie, off_t position, void *data, size_t *numBytes)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;

    if (*numBytes == 0)
        return B_OK;

    uint32_t u32CurSeq = ASMAtomicUoReadU32(&g_DevExt.u32MousePosChangedSeq);
    if (pSession->u32MousePosChangedSeq != u32CurSeq)
    {
        pSession->u32MousePosChangedSeq = u32CurSeq;
        *numBytes = 1;
        return B_OK;
    }

    *numBytes = 0;
    return B_OK;
}



status_t init_hardware()
{
    return get_module(MODULE_NAME, (module_info **)&g_VBoxGuest);
}

status_t init_driver()
{
    return B_OK;
}

device_hooks *find_device(const char *name)
{
    static device_hooks s_vgdrvHaikuDeviceHooks =
    {
        vgdrvHaikuOpen,
        vgdrvHaikuClose,
        vgdrvHaikuFree,
        vgdrvHaikuIOCtl,
        vgdrvHaikuRead,
        vgdrvHaikuWrite,
        vgdrvHaikuSelect,
        vgdrvHaikuDeselect,
    };
    return &s_vgdrvHaikuDeviceHooks;
}

const char **publish_devices()
{
    static const char *s_papszDevices[] = { DEVICE_NAME, NULL };
    return s_papszDevices;
}

void uninit_driver()
{
    put_module(MODULE_NAME);
}

