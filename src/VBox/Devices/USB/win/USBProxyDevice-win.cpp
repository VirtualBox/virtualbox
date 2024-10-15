/* $Id: USBProxyDevice-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USBPROXY - USB proxy, Win32 backend
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_USBPROXY
#include <iprt/win/windows.h>

#include <VBox/vmm/pdm.h>
#include <VBox/err.h>
#include <VBox/usb.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/asm.h>
#include "../USBProxyDevice.h"
#include <VBox/usblib.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct _QUEUED_URB
{
    PVUSBURB         urb;

    USBSUP_URB       urbwin;
    OVERLAPPED       overlapped;
    DWORD            cbReturned;
    bool             fCancelled;
} QUEUED_URB, *PQUEUED_URB;

typedef struct
{
    /* Critical section to protect this structure. */
    RTCRITSECT      CritSect;
    HANDLE          hDev;
    uint8_t         bInterfaceNumber;
    bool            fClaimed;
    /** Set if reaper should exit ASAP. */
    bool            fWakeUpNow;
    /** The allocated size of paHandles and paQueuedUrbs. */
    unsigned        cAllocatedUrbs;
    /** The number of URBs in the array. */
    unsigned        cQueuedUrbs;
    /** Array of pointers to the in-flight URB structures. */
    PQUEUED_URB    *paQueuedUrbs;
    /** Array of handles, this is parallel to paQueuedUrbs. */
    PHANDLE         paHandles;
    /* Event sempahore to wakeup the reaper thead. */
    HANDLE          hEventWakeup;
    /** Number of queued URBs waiting to get into the handle list. */
    unsigned        cPendingUrbs;
    /** Array of pending URBs. */
    PQUEUED_URB     aPendingUrbs[64];
} PRIV_USBW32, *PPRIV_USBW32;

/* All functions are returning 1 on success, 0 on error */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int usbProxyWinSetInterface(PUSBPROXYDEV p, int iIf, int setting);

/**
 * Converts the given Windows error code to VBox handling unplugged devices.
 *
 * @returns VBox status code.
 * @param   pProxDev    The USB proxy device instance.
 * @param   dwErr       Windows error code.
 */
static int usbProxyWinHandleUnpluggedDevice(PUSBPROXYDEV pProxyDev, DWORD dwErr)
{
#ifdef LOG_ENABLED
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);
#endif

    if (   dwErr == ERROR_INVALID_HANDLE_STATE
        || dwErr == ERROR_BAD_COMMAND)
    {
        Log(("usbproxy: device %x unplugged!! (usbProxyWinHandleUnpluggedDevice)\n", pPriv->hDev));
        pProxyDev->fDetached = true;
    }
    else
        AssertMsgFailed(("lasterr=%d\n", dwErr));
    return RTErrConvertFromWin32(dwErr);
}

/**
 * Open a USB device and create a backend instance for it.
 *
 * @returns VBox status code.
 */
static DECLCALLBACK(int) usbProxyWinOpen(PUSBPROXYDEV pProxyDev, const char *pszAddress)
{
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);

    int rc = VINF_SUCCESS;
    pPriv->cAllocatedUrbs = 32;
    pPriv->paHandles    = (PHANDLE)RTMemAllocZ(sizeof(pPriv->paHandles[0]) * pPriv->cAllocatedUrbs);
    pPriv->paQueuedUrbs = (PQUEUED_URB *)RTMemAllocZ(sizeof(pPriv->paQueuedUrbs[0]) * pPriv->cAllocatedUrbs);
    if (    pPriv->paQueuedUrbs
        &&  pPriv->paHandles)
    {
        /*
         * Open the device.
         */
        pPriv->hDev = CreateFile(pszAddress,
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_WRITE | FILE_SHARE_READ,
                                 NULL, // no SECURITY_ATTRIBUTES structure
                                 OPEN_EXISTING, // No special create flags
                                 FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, // overlapped IO
                                 NULL); // No template file
        if (pPriv->hDev != INVALID_HANDLE_VALUE)
        {
            Log(("usbProxyWinOpen: hDev=%p\n", pPriv->hDev));

            /*
             * Check the version
             */
            USBSUP_VERSION  version = {0};
            DWORD           cbReturned = 0;
            if (DeviceIoControl(pPriv->hDev, SUPUSB_IOCTL_GET_VERSION, NULL, 0, &version, sizeof(version), &cbReturned, NULL))
            {
                if (    version.u32Major == USBDRV_MAJOR_VERSION
#if USBDRV_MINOR_VERSION != 0
                    &&  version.u32Minor >=  USBDRV_MINOR_VERSION
#endif
                   )
                {
                    USBSUP_CLAIMDEV in;
                    in.bInterfaceNumber = 0;

                    cbReturned = 0;
                    if (DeviceIoControl(pPriv->hDev, SUPUSB_IOCTL_USB_CLAIM_DEVICE, &in, sizeof(in), &in, sizeof(in), &cbReturned, NULL))
                    {
                        if (in.fClaimed)
                        {
                            pPriv->fClaimed = true;
#if 0 /** @todo this needs to be enabled if windows chooses a default config. Test with the TrekStor GO Stick. */
                            pProxyDev->iActiveCfg = 1;
                            pProxyDev->cIgnoreSetConfigs = 1;
#endif

                            rc = RTCritSectInit(&pPriv->CritSect);
                            AssertRC(rc);
                            pPriv->hEventWakeup     = CreateEvent(NULL, FALSE, FALSE, NULL);
                            Assert(pPriv->hEventWakeup);

                            pPriv->paHandles[0] = pPriv->hEventWakeup;

                            return VINF_SUCCESS;
                        }

                        rc = VERR_GENERAL_FAILURE;
                        Log(("usbproxy: unable to claim device %x (%s)!!\n", pPriv->hDev, pszAddress));
                    }
                }
                else
                {
                    rc = VERR_VERSION_MISMATCH;
                    Log(("usbproxy: Version mismatch: %d.%d != %d.%d (cur)\n",
                         version.u32Major, version.u32Minor, USBDRV_MAJOR_VERSION, USBDRV_MINOR_VERSION));
                }
            }

            /* Convert last error if necessary */
            if (RT_SUCCESS(rc))
            {
                DWORD dwErr = GetLastError();
                Log(("usbproxy: last error %d\n", dwErr));
                rc = RTErrConvertFromWin32(dwErr);
            }

            CloseHandle(pPriv->hDev);
            pPriv->hDev = INVALID_HANDLE_VALUE;
        }
        else
        {
            Log(("usbproxy: FAILED to open '%s'! last error %d\n", pszAddress, GetLastError()));
            rc = VERR_FILE_NOT_FOUND;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    RTMemFree(pPriv->paQueuedUrbs);
    RTMemFree(pPriv->paHandles);
    return rc;
}

/**
 * Copy the device and free resources associated with the backend.
 */
static DECLCALLBACK(void) usbProxyWinClose(PUSBPROXYDEV pProxyDev)
{
    /* Here we just close the device and free up p->priv
     * there is no need to do anything like cancel outstanding requests
     * that will have been done already
     */
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);
    Assert(pPriv);
    if (!pPriv)
        return;
    Log(("usbProxyWinClose: %p\n", pPriv->hDev));

    if (pPriv->hDev != INVALID_HANDLE_VALUE)
    {
        Assert(pPriv->fClaimed);

        USBSUP_RELEASEDEV in;
        DWORD cbReturned = 0;
        in.bInterfaceNumber = pPriv->bInterfaceNumber;
        if (!DeviceIoControl(pPriv->hDev, SUPUSB_IOCTL_USB_RELEASE_DEVICE, &in, sizeof(in), NULL, 0, &cbReturned, NULL))
        {
            Log(("usbproxy: usbProxyWinClose: DeviceIoControl %#x failed with %#x!!\n", pPriv->hDev, GetLastError()));
        }
        if (!CloseHandle(pPriv->hDev))
            AssertLogRelMsgFailed(("usbproxy: usbProxyWinClose: CloseHandle %#x failed with %#x!!\n", pPriv->hDev, GetLastError()));
        pPriv->hDev = INVALID_HANDLE_VALUE;
    }

    CloseHandle(pPriv->hEventWakeup);
    RTCritSectDelete(&pPriv->CritSect);

    RTMemFree(pPriv->paQueuedUrbs);
    RTMemFree(pPriv->paHandles);
}


static DECLCALLBACK(int) usbProxyWinReset(PUSBPROXYDEV pProxyDev, bool fResetOnLinux)
{
    RT_NOREF(fResetOnLinux);
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);
    DWORD cbReturned;
    int  rc;

    Assert(pPriv);

    Log(("usbproxy: Reset %x\n", pPriv->hDev));

    /* Here we just need to assert reset signalling on the USB device */
    cbReturned = 0;
    if (DeviceIoControl(pPriv->hDev, SUPUSB_IOCTL_USB_RESET, NULL, 0, NULL, 0, &cbReturned, NULL))
    {
#if 0 /** @todo this needs to be enabled if windows chooses a default config. Test with the TrekStor GO Stick. */
        pProxyDev->iActiveCfg = 1;
        pProxyDev->cIgnoreSetConfigs = 2;
#else
        pProxyDev->iActiveCfg = -1;
        pProxyDev->cIgnoreSetConfigs = 0;
#endif
        return VINF_SUCCESS;
    }

    rc = GetLastError();
    if (rc == ERROR_DEVICE_REMOVED)
    {
        Log(("usbproxy: device %p unplugged!! (usbProxyWinReset)\n", pPriv->hDev));
        pProxyDev->fDetached = true;
    }
    return RTErrConvertFromWin32(rc);
}

static DECLCALLBACK(int) usbProxyWinSetConfig(PUSBPROXYDEV pProxyDev, int cfg)
{
    /* Send a SET_CONFIGURATION command to the device. We don't do this
     * as a normal control message, because the OS might not want to
     * be left out of the loop on such a thing.
     *
     * It would be OK to send a SET_CONFIGURATION control URB at this
     * point but it has to be synchronous.
    */
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);
    USBSUP_SET_CONFIG in;
    DWORD cbReturned;

    Assert(pPriv);

    Log(("usbproxy: Set config of %p to %d\n", pPriv->hDev, cfg));
    in.bConfigurationValue = cfg;

    /* Here we just need to assert reset signalling on the USB device */
    cbReturned = 0;
    if (DeviceIoControl(pPriv->hDev, SUPUSB_IOCTL_USB_SET_CONFIG, &in, sizeof(in), NULL, 0, &cbReturned, NULL))
        return VINF_SUCCESS;

    return usbProxyWinHandleUnpluggedDevice(pProxyDev, GetLastError());
}

static DECLCALLBACK(int) usbProxyWinClaimInterface(PUSBPROXYDEV pProxyDev, int iIf)
{
    /* Called just before we use an interface. Needed on Linux to claim
     * the interface from the OS, since even when proxying the host OS
     * might want to allow other programs to use the unused interfaces.
     * Not relevant for Windows.
     */
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);

    pPriv->bInterfaceNumber = iIf;

    Assert(pPriv);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) usbProxyWinReleaseInterface(PUSBPROXYDEV pProxyDev, int iIf)
{
    RT_NOREF(pProxyDev, iIf);
    /* The opposite of claim_interface. */
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) usbProxyWinSetInterface(PUSBPROXYDEV pProxyDev, int iIf, int setting)
{
    /* Select an alternate setting for an interface, the same applies
     * here as for set_config, you may convert this in to a control
     * message if you want but it must be synchronous
     */
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);
    USBSUP_SELECT_INTERFACE in;
    DWORD cbReturned;

    Assert(pPriv);

    Log(("usbproxy: Select interface of %x to %d/%d\n", pPriv->hDev, iIf, setting));
    in.bInterfaceNumber  = iIf;
    in.bAlternateSetting = setting;

    /* Here we just need to assert reset signalling on the USB device */
    cbReturned = 0;
    if (DeviceIoControl(pPriv->hDev, SUPUSB_IOCTL_USB_SELECT_INTERFACE, &in, sizeof(in), NULL, 0, &cbReturned, NULL))
        return VINF_SUCCESS;

    return usbProxyWinHandleUnpluggedDevice(pProxyDev, GetLastError());
}

/**
 * Clears the halted endpoint 'ep'.
 */
static DECLCALLBACK(int) usbProxyWinClearHaltedEndPt(PUSBPROXYDEV pProxyDev, unsigned int ep)
{
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);
    USBSUP_CLEAR_ENDPOINT in;
    DWORD cbReturned;

    Assert(pPriv);

    Log(("usbproxy: Clear endpoint %d of %x\n", ep, pPriv->hDev));
    in.bEndpoint = ep;

    cbReturned = 0;
    if (DeviceIoControl(pPriv->hDev, SUPUSB_IOCTL_USB_CLEAR_ENDPOINT, &in, sizeof(in), NULL, 0, &cbReturned, NULL))
        return VINF_SUCCESS;

    return usbProxyWinHandleUnpluggedDevice(pProxyDev, GetLastError());
}

/**
 * Aborts a pipe/endpoint (cancels all outstanding URBs on the endpoint).
 */
static int usbProxyWinAbortEndPt(PUSBPROXYDEV pProxyDev, unsigned int ep)
{
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);
    USBSUP_CLEAR_ENDPOINT in;
    DWORD cbReturned;

    Assert(pPriv);

    Log(("usbproxy: Abort endpoint %d of %x\n", ep, pPriv->hDev));
    in.bEndpoint = ep;

    cbReturned = 0;
    if (DeviceIoControl(pPriv->hDev, SUPUSB_IOCTL_USB_ABORT_ENDPOINT, &in, sizeof(in), NULL, 0, &cbReturned, NULL))
        return VINF_SUCCESS;

    return usbProxyWinHandleUnpluggedDevice(pProxyDev, GetLastError());
}

/**
 * @interface_method_impl{USBPROXYBACK,pfnUrbQueue}
 */
static DECLCALLBACK(int) usbProxyWinUrbQueue(PUSBPROXYDEV pProxyDev, PVUSBURB pUrb)
{
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);
    Assert(pPriv);

    /* Don't even bother if we can't wait for that many objects. */
    if (pPriv->cPendingUrbs + pPriv->cQueuedUrbs >= (MAXIMUM_WAIT_OBJECTS - 1))
        return VERR_OUT_OF_RESOURCES;
    if (pPriv->cPendingUrbs >= RT_ELEMENTS(pPriv->aPendingUrbs))
        return VERR_OUT_OF_RESOURCES;

    /*
     * Allocate and initialize a URB queue structure.
     */
    /** @todo pool these */
    PQUEUED_URB pQUrbWin = (PQUEUED_URB)RTMemAllocZ(sizeof(QUEUED_URB));
    if (!pQUrbWin)
        return VERR_NO_MEMORY;

    switch (pUrb->enmType)
    {
        case VUSBXFERTYPE_CTRL: pQUrbWin->urbwin.type = USBSUP_TRANSFER_TYPE_CTRL; break; /* you won't ever see these */
        case VUSBXFERTYPE_ISOC: pQUrbWin->urbwin.type = USBSUP_TRANSFER_TYPE_ISOC;
            pQUrbWin->urbwin.numIsoPkts = pUrb->cIsocPkts;
            for (unsigned i = 0; i < pUrb->cIsocPkts; ++i)
            {
                pQUrbWin->urbwin.aIsoPkts[i].cb   = pUrb->aIsocPkts[i].cb;
                pQUrbWin->urbwin.aIsoPkts[i].off  = pUrb->aIsocPkts[i].off;
                pQUrbWin->urbwin.aIsoPkts[i].stat = USBSUP_XFER_OK;
            }
            break;
        case VUSBXFERTYPE_BULK: pQUrbWin->urbwin.type = USBSUP_TRANSFER_TYPE_BULK; break;
        case VUSBXFERTYPE_INTR: pQUrbWin->urbwin.type = USBSUP_TRANSFER_TYPE_INTR; break;
        case VUSBXFERTYPE_MSG:  pQUrbWin->urbwin.type = USBSUP_TRANSFER_TYPE_MSG; break;
        default:
            AssertMsgFailed(("Invalid type %d\n", pUrb->enmType));
            return VERR_INVALID_PARAMETER;
    }

    switch (pUrb->enmDir)
    {
        case VUSBDIRECTION_SETUP:
            AssertFailed();
            pQUrbWin->urbwin.dir = USBSUP_DIRECTION_SETUP;
            break;
        case VUSBDIRECTION_IN:
            pQUrbWin->urbwin.dir = USBSUP_DIRECTION_IN;
            break;
        case VUSBDIRECTION_OUT:
            pQUrbWin->urbwin.dir = USBSUP_DIRECTION_OUT;
            break;
        default:
            AssertMsgFailed(("Invalid direction %d\n", pUrb->enmDir));
            return VERR_INVALID_PARAMETER;
    }

    Log(("usbproxy: Queue URB %p ep=%d cbData=%d abData=%p cIsocPkts=%d\n", pUrb, pUrb->EndPt, pUrb->cbData, pUrb->abData, pUrb->cIsocPkts));

    pQUrbWin->urb           = pUrb;
    pQUrbWin->urbwin.ep     = pUrb->EndPt;
    pQUrbWin->urbwin.len    = pUrb->cbData;
    pQUrbWin->urbwin.buf    = pUrb->abData;
    pQUrbWin->urbwin.error  = USBSUP_XFER_OK;
    pQUrbWin->urbwin.flags  = USBSUP_FLAG_NONE;
    if (pUrb->enmDir == VUSBDIRECTION_IN && !pUrb->fShortNotOk)
        pQUrbWin->urbwin.flags = USBSUP_FLAG_SHORT_OK;

    int rc = VINF_SUCCESS;
    pQUrbWin->overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (pQUrbWin->overlapped.hEvent != INVALID_HANDLE_VALUE)
    {
        pUrb->Dev.pvPrivate = pQUrbWin;

        if (   DeviceIoControl(pPriv->hDev, SUPUSB_IOCTL_SEND_URB,
                               &pQUrbWin->urbwin, sizeof(pQUrbWin->urbwin),
                               &pQUrbWin->urbwin, sizeof(pQUrbWin->urbwin),
                               &pQUrbWin->cbReturned, &pQUrbWin->overlapped)
            || GetLastError() == ERROR_IO_PENDING)
        {
            /* insert into the queue */
            RTCritSectEnter(&pPriv->CritSect);
            unsigned j = pPriv->cPendingUrbs;
            Assert(j < RT_ELEMENTS(pPriv->aPendingUrbs));
            pPriv->aPendingUrbs[j] = pQUrbWin;
            pPriv->cPendingUrbs++;
            RTCritSectLeave(&pPriv->CritSect);
            SetEvent(pPriv->hEventWakeup);
            return VINF_SUCCESS;
        }
        else
        {
            DWORD dwErr = GetLastError();
            if (   dwErr == ERROR_INVALID_HANDLE_STATE
                || dwErr == ERROR_BAD_COMMAND)
            {
                Log(("usbproxy: device %p unplugged!! (usbProxyWinUrbQueue)\n", pPriv->hDev));
                pProxyDev->fDetached = true;
            }
            else
                AssertMsgFailed(("dwErr=%X urbwin.error=%d (submit urb)\n", dwErr, pQUrbWin->urbwin.error));
            rc = RTErrConvertFromWin32(dwErr);
            CloseHandle(pQUrbWin->overlapped.hEvent);
            pQUrbWin->overlapped.hEvent = INVALID_HANDLE_VALUE;
        }
    }
#ifdef DEBUG_misha
    else
    {
        AssertMsgFailed(("FAILED!!, hEvent(0x%p)\n", pQUrbWin->overlapped.hEvent));
        rc = VERR_NO_MEMORY;
    }
#endif

    Assert(pQUrbWin->overlapped.hEvent == INVALID_HANDLE_VALUE);
    RTMemFree(pQUrbWin);
    return rc;
}

/**
 * Convert Windows proxy URB status to VUSB status.
 *
 * @returns VUSB status constant.
 * @param   win_status  Windows USB proxy status constant.
 */
static VUSBSTATUS usbProxyWinStatusToVUsbStatus(USBSUP_ERROR win_status)
{
    VUSBSTATUS      vusb_status;

    switch (win_status)
    {
        case USBSUP_XFER_OK:        vusb_status = VUSBSTATUS_OK; break;
        case USBSUP_XFER_STALL:     vusb_status = VUSBSTATUS_STALL; break;
        case USBSUP_XFER_DNR:       vusb_status = VUSBSTATUS_DNR; break;
        case USBSUP_XFER_CRC:       vusb_status = VUSBSTATUS_CRC; break;
        case USBSUP_XFER_NAC:       vusb_status = VUSBSTATUS_NOT_ACCESSED; break;
        case USBSUP_XFER_UNDERRUN:  vusb_status = VUSBSTATUS_DATA_UNDERRUN; break;
        case USBSUP_XFER_OVERRUN:   vusb_status = VUSBSTATUS_DATA_OVERRUN; break;
        default:
            AssertMsgFailed(("USB: Invalid error %d\n", win_status));
            vusb_status = VUSBSTATUS_DNR;
            break;
    }
    return vusb_status;
}

/**
 * Reap URBs in-flight on a device.
 *
 * @returns Pointer to a completed URB.
 * @returns NULL if no URB was completed.
 * @param   pProxyDev   The device.
 * @param   cMillies    Number of milliseconds to wait. Use 0 to not
 *                      wait at all.
 */
static DECLCALLBACK(PVUSBURB) usbProxyWinUrbReap(PUSBPROXYDEV pProxyDev, RTMSINTERVAL cMillies)
{
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);
    AssertReturn(pPriv, NULL);

    /*
     * There are some unnecessary calls, just return immediately or
     * WaitForMultipleObjects will fail.
     */
    if (   pPriv->cQueuedUrbs <= 0
        && pPriv->cPendingUrbs == 0)
    {
        Log(("usbproxy: Nothing pending\n"));
        if (   cMillies != 0
            && pPriv->cPendingUrbs == 0)
        {
            /* Wait for the wakeup call. */
            Log(("usbproxy: Waiting for wakeup call\n"));
            DWORD cMilliesWait = cMillies == RT_INDEFINITE_WAIT ? INFINITE : cMillies;
            DWORD rc = WaitForMultipleObjects(1, &pPriv->hEventWakeup, FALSE, cMilliesWait);
            Log(("usbproxy: Initial wait rc=%X\n", rc));
            if (rc != WAIT_OBJECT_0) {
                Log(("usbproxy: Initial wait failed, rc=%X\n", rc));
                return NULL;
            }
        }
        return NULL;
    }

again:
    /* Check for pending URBs. */
    Log(("usbproxy: %u pending URBs\n", pPriv->cPendingUrbs));
    if (pPriv->cPendingUrbs)
    {
        RTCritSectEnter(&pPriv->CritSect);

        /* Ensure we've got sufficient space in the arrays. */
        if (pPriv->cQueuedUrbs + pPriv->cPendingUrbs + 1 > pPriv->cAllocatedUrbs)
        {
            unsigned cNewMax = pPriv->cAllocatedUrbs + pPriv->cPendingUrbs + 1;
            void *pv = RTMemRealloc(pPriv->paHandles, sizeof(pPriv->paHandles[0]) * (cNewMax + 1)); /* One extra for the wakeup event. */
            if (!pv)
            {
                AssertMsgFailed(("RTMemRealloc failed for paHandles[%d]", cNewMax));
                //break;
            }
            pPriv->paHandles = (PHANDLE)pv;

            pv = RTMemRealloc(pPriv->paQueuedUrbs, sizeof(pPriv->paQueuedUrbs[0]) * cNewMax);
            if (!pv)
            {
                AssertMsgFailed(("RTMemRealloc failed for paQueuedUrbs[%d]", cNewMax));
                //break;
            }
            pPriv->paQueuedUrbs = (PQUEUED_URB *)pv;
            pPriv->cAllocatedUrbs = cNewMax;
        }

        /* Copy the pending URBs over. */
        for (unsigned i = 0; i < pPriv->cPendingUrbs; i++)
        {
            pPriv->paHandles[pPriv->cQueuedUrbs + i] = pPriv->aPendingUrbs[i]->overlapped.hEvent;
            pPriv->paQueuedUrbs[pPriv->cQueuedUrbs + i] = pPriv->aPendingUrbs[i];
        }
        pPriv->cQueuedUrbs += pPriv->cPendingUrbs;
        pPriv->cPendingUrbs = 0;
        pPriv->paHandles[pPriv->cQueuedUrbs] = pPriv->hEventWakeup;
        pPriv->paHandles[pPriv->cQueuedUrbs + 1] = INVALID_HANDLE_VALUE;

        RTCritSectLeave(&pPriv->CritSect);
    }

    /*
     * Wait/poll.
     *
     * ASSUMPTION: Multiple usbProxyWinUrbReap calls can not be run concurrently
     *   with each other so racing the cQueuedUrbs access/modification can not occur.
     *
     * However, usbProxyWinUrbReap can be run concurrently with usbProxyWinUrbQueue
     * and pPriv->paHandles access/realloc must be synchronized.
     *
     * NB: Due to the design of Windows overlapped I/O, DeviceIoControl calls to submit
     * URBs use individual event objects. When a new URB is submitted, we have to add its
     * event object to the list of objects that WaitForMultipleObjects is waiting on. Thus
     * hEventWakeup has dual purpose, serving to handle proxy wakeup calls meant to abort
     * reaper waits, but also waking up the reaper after every URB submit so that the newly
     * submitted URB can be added to the list of waiters.
     */
    unsigned cQueuedUrbs = ASMAtomicReadU32((volatile uint32_t *)&pPriv->cQueuedUrbs);
    DWORD cMilliesWait = cMillies == RT_INDEFINITE_WAIT ? INFINITE : cMillies;
    PVUSBURB pUrb = NULL;
    DWORD rc = WaitForMultipleObjects(cQueuedUrbs + 1, pPriv->paHandles, FALSE, cMilliesWait);
    Log(("usbproxy: Wait (%d milliseconds) returned with rc=%X\n", cMilliesWait, rc));

    /* If the wakeup event fired return immediately. */
    if (rc == WAIT_OBJECT_0 + cQueuedUrbs)
    {
        /* Get outta here flag set? If so, bail now. */
        if (ASMAtomicXchgBool(&pPriv->fWakeUpNow, false))
        {
            Log(("usbproxy: Reaper woken up, returning NULL\n"));
            return NULL;
        }

        /* A new URBs was queued through usbProxyWinUrbQueue() and needs to be
         * added to the wait list. Go again.
         */
        Log(("usbproxy: Reaper woken up after queuing new URB, go again.\n"));
        goto again;
    }

    AssertCompile(WAIT_OBJECT_0 == 0);
    if (/*rc >= WAIT_OBJECT_0 && */ rc < WAIT_OBJECT_0 + cQueuedUrbs)
    {
        RTCritSectEnter(&pPriv->CritSect);
        unsigned iUrb = rc - WAIT_OBJECT_0;
        PQUEUED_URB pQUrbWin = pPriv->paQueuedUrbs[iUrb];
        pUrb = pQUrbWin->urb;

        /*
         * Remove it from the arrays.
         */
        cQueuedUrbs = --pPriv->cQueuedUrbs;
        if (cQueuedUrbs != iUrb)
        {
            /* Move the array forward */
            for (unsigned i=iUrb;i<cQueuedUrbs;i++)
            {
                pPriv->paHandles[i]    = pPriv->paHandles[i+1];
                pPriv->paQueuedUrbs[i] = pPriv->paQueuedUrbs[i+1];
            }
        }
        pPriv->paHandles[cQueuedUrbs] = pPriv->hEventWakeup;
        pPriv->paHandles[cQueuedUrbs + 1] = INVALID_HANDLE_VALUE;
        pPriv->paQueuedUrbs[cQueuedUrbs] = NULL;
        RTCritSectLeave(&pPriv->CritSect);
        Assert(cQueuedUrbs == pPriv->cQueuedUrbs);

        /*
         * Update the urb.
         */
        pUrb->enmStatus = usbProxyWinStatusToVUsbStatus(pQUrbWin->urbwin.error);
        pUrb->cbData = (uint32_t)pQUrbWin->urbwin.len;
        if (pUrb->enmType == VUSBXFERTYPE_ISOC)
        {
            for (unsigned i = 0; i < pUrb->cIsocPkts; ++i)
            {
                /* NB: Windows won't change the packet offsets, but the packets may
                 * be only partially filled or completely empty.
                 */
                pUrb->aIsocPkts[i].enmStatus = usbProxyWinStatusToVUsbStatus(pQUrbWin->urbwin.aIsoPkts[i].stat);
                pUrb->aIsocPkts[i].cb = pQUrbWin->urbwin.aIsoPkts[i].cb;
            }
        }
        Log(("usbproxy: pUrb=%p (#%d) ep=%d cbData=%d status=%d cIsocPkts=%d ready\n",
             pUrb, rc - WAIT_OBJECT_0, pQUrbWin->urb->EndPt, pQUrbWin->urb->cbData, pUrb->enmStatus, pUrb->cIsocPkts));

        /* free the urb queuing structure */
        if (pQUrbWin->overlapped.hEvent != INVALID_HANDLE_VALUE)
        {
            CloseHandle(pQUrbWin->overlapped.hEvent);
            pQUrbWin->overlapped.hEvent = INVALID_HANDLE_VALUE;
        }
        RTMemFree(pQUrbWin);
    }
    else if (   rc == WAIT_FAILED
             || (rc >= WAIT_ABANDONED_0 && rc < WAIT_ABANDONED_0 + cQueuedUrbs))
        AssertMsgFailed(("USB: WaitForMultipleObjects %d objects failed with rc=%d and last error %d\n", cQueuedUrbs, rc, GetLastError()));

    return pUrb;
}


/**
 * Cancels an in-flight URB.
 *
 * The URB requires reaping, so we don't change its state.
 *
 * @remark  There isn't a way to cancel a specific URB on Windows.
 *          on darwin. The interface only supports the aborting of
 *          all URBs pending on an endpoint. Luckily that is usually
 *          exactly what the guest wants to do.
 */
static DECLCALLBACK(int) usbProxyWinUrbCancel(PUSBPROXYDEV pProxyDev, PVUSBURB pUrb)
{
    PPRIV_USBW32      pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);
    PQUEUED_URB       pQUrbWin  = (PQUEUED_URB)pUrb->Dev.pvPrivate;
    USBSUP_CLEAR_ENDPOINT   in;
    DWORD                   cbReturned;

    AssertPtrReturn(pQUrbWin, VERR_INVALID_PARAMETER);

    in.bEndpoint = pUrb->EndPt | ((pUrb->EndPt && pUrb->enmDir == VUSBDIRECTION_IN) ? 0x80 : 0);
    Log(("usbproxy: Cancel urb %p, endpoint %x\n", pUrb, in.bEndpoint));

    cbReturned = 0;
    if (DeviceIoControl(pPriv->hDev, SUPUSB_IOCTL_USB_ABORT_ENDPOINT, &in, sizeof(in), NULL, 0, &cbReturned, NULL))
        return VINF_SUCCESS;

    DWORD dwErr = GetLastError();
    if (   dwErr == ERROR_INVALID_HANDLE_STATE
        || dwErr == ERROR_BAD_COMMAND)
    {
        Log(("usbproxy: device %x unplugged!! (usbProxyWinUrbCancel)\n", pPriv->hDev));
        pProxyDev->fDetached = true;
        return VINF_SUCCESS; /* Fake success and deal with the unplugged device elsewhere. */
    }

    AssertMsgFailed(("lastErr=%ld\n", dwErr));
    return RTErrConvertFromWin32(dwErr);
}

static DECLCALLBACK(int) usbProxyWinWakeup(PUSBPROXYDEV pProxyDev)
{
    PPRIV_USBW32 pPriv = USBPROXYDEV_2_DATA(pProxyDev, PPRIV_USBW32);

    Log(("usbproxy: device %x wakeup\n", pPriv->hDev));
    ASMAtomicXchgBool(&pPriv->fWakeUpNow, true);
    SetEvent(pPriv->hEventWakeup);
    return VINF_SUCCESS;
}

/**
 * The Win32 USB Proxy Backend.
 */
extern const USBPROXYBACK g_USBProxyDeviceHost =
{
    /* pszName */
    "host",
    /* cbBackend */
    sizeof(PRIV_USBW32),
    usbProxyWinOpen,
    NULL,
    usbProxyWinClose,
    usbProxyWinReset,
    usbProxyWinSetConfig,
    usbProxyWinClaimInterface,
    usbProxyWinReleaseInterface,
    usbProxyWinSetInterface,
    usbProxyWinClearHaltedEndPt,
    usbProxyWinUrbQueue,
    usbProxyWinUrbCancel,
    usbProxyWinUrbReap,
    usbProxyWinWakeup,
    0
};

