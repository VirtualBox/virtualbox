/* $Id: USBProxyDevice-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USB device proxy - the Linux backend.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_USBPROXY
#include <VBox/vmm/pdm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/stream.h>
#include <iprt/alloc.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/file.h>
#include "../USBProxyDevice.h"

#define INCL_BASE
#define INCL_ERRORS
#include <os2.h>
#include <usbcalls.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Structure for keeping track of the URBs for a device.
 */
typedef struct USBPROXYURBOS2
{
    /** Pointer to the virtual URB. */
    PVUSBURB                pUrb;
    /** Pointer to the next OS/2 URB. */
    struct USBPROXYURBOS2  *pNext;
    /** Pointer to the previous OS/2 URB. */
    struct USBPROXYURBOS2  *pPrev;
} USBPROXYURBOS2, *PUSBPROXYURBOS2;

/**
 * Data for the OS/2 usb proxy backend.
 */
typedef struct USBPROXYDEVOS2
{
    /** The async thread for this device.
     * Currently only one thread is used, but this might have to change... */
    RTTHREAD            Thread;
    /** Thread termination indicator. */
    bool volatile       fTerminate;
    /** The USB handle. */
    USBHANDLE           hDevice;
    /** Critical section protecting the lists. */
    RTCRITSECT          CritSect;
    /** For blocking reap calls. */
    RTSEMEVENT          EventSyncWait;
    /** List of URBs to process. Doubly linked. */
    PUSBPROXYURBOS2     pTodoHead;
    /** The tail pointer. */
    PUSBPROXYURBOS2     pTodoTail;
    /** The list of free linux URBs. Singly linked. */
    PUSBPROXYURBOS2     pFreeHead;
    /** The list of active linux URBs. Doubly linked.
     * We must maintain this so we can properly reap URBs of a detached device.
     * Only the split head will appear in this list. */
    PUSBPROXYURBOS2     pInFlightHead;
    /** The list of landed linux URBs. Doubly linked.
     * Only the split head will appear in this list. */
    PUSBPROXYURBOS2     pTaxingHead;
    /** The tail of the landed linux URBs. */
    PUSBPROXYURBOS2     pTaxingTail;
} USBPROXYDEVOS2, *PUSBPROXYDEVOS2;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef DYNAMIC_USBCALLS
static int usbProxyOs2GlobalInit(void);
#endif
static PUSBPROXYURBOS2 usbProxyOs2UrbAlloc(PUSBPROXYDEV pProxyDev);
static void usbProxyOs2UrbFree(PUSBPROXYDEV pProxyDev, PUSBPROXYURBOS2 pUrbOs2);
static DECLCALLBACK(int) usbProxyOs2AsyncThread(RTTHREAD Thread, void *pvProxyDev);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef DYNAMIC_USBCALLS
static HMODULE g_hmod;
static APIRET (APIENTRY *g_pfnUsbOpen)(PUSBHANDLE, USHORT, USHORT, USHORT, USHORT);
static APIRET (APIENTRY *g_pfnUsbClose)(USBHANDLE);
static APIRET (APIENTRY *g_pfnUsbCtrlMessage)(USBHANDLE, UCHAR, UCHAR, USHORT, USHORT, USHORT, void *, ULONG);
static APIRET (APIENTRY *g_pfnUsbBulkRead2)(USBHANDLE, UCHAR, UCHAR, BOOL, PULONG, void *, ULONG);
static APIRET (APIENTRY *g_pfnUsbBulkWrite2)(USBHANDLE, UCHAR, UCHAR, BOOL, ULONG, void *, ULONG);
#else
# define g_pfnUsbOpen UsbOpen
# define g_pfnUsbClose UsbClose
# define g_pfnUsbCtrlMessage UsbCtrlMessage
# define g_pfnUsbBulkRead2 UsbBulkRead2
# define g_pfnUsbBulkWrite2 UsbBulkWrite2
#endif



#ifdef DYNAMIC_USBCALLS
/**
 * Loads usbcalls.dll and resolves the symbols we need.
 *
 * The usbcalls.dll will not be unloaded.
 *
 * @returns VBox status code.
 */
static int usbProxyOs2GlobalInit(void)
{
    int rc = DosLoadModule(NULL, 0, (PCSZ)"usbcalls", &g_hmod);
    rc = RTErrConvertFromOS2(rc);
    if (RT_SUCCESS(rc))
    {
        if (    (rc = DosQueryProcAddr(g_hmod, 0, (PCSZ)"UsbOpen", (PPFN)&g_pfnUsbOpen)) == NO_ERROR
            &&  (rc = DosQueryProcAddr(g_hmod, 0, (PCSZ)"UsbClose", (PPFN)&g_pfnUsbClose)) == NO_ERROR
            &&  (rc = DosQueryProcAddr(g_hmod, 0, (PCSZ)"UsbCtrlMessage", (PPFN)&g_pfnUsbCtrlMessage)) == NO_ERROR
            &&  (rc = DosQueryProcAddr(g_hmod, 0, (PCSZ)"UsbBulkRead", (PPFN)&g_pfnUsbBulkRead)) == NO_ERROR
            &&  (rc = DosQueryProcAddr(g_hmod, 0, (PCSZ)"UsbBulkWrite", (PPFN)&g_pfnUsbBulkWrite)) == NO_ERROR
           )
        {

            return VINF_SUCCESS;
        }

        g_pfnUsbOpen = NULL;
        g_pfnUsbClose = NULL;
        g_pfnUsbCtrlMessage = NULL;
        g_pfnUsbBulkRead = NULL;
        g_pfnUsbBulkWrite = NULL;
        DosFreeModule(g_hmod);
    }

    g_hmod = NULLHANDLE;
    return rc;
}
#endif



/**
 * Allocates a OS/2 URB request structure.
 * @returns Pointer to an active URB request.
 * @returns NULL on failure.
 * @param   pProxyDev       The proxy device instance.
 */
static PUSBPROXYURBOS2 usbProxyOs2UrbAlloc(PUSBPROXYDEV pProxyDev)
{
    PUSBPROXYDEVOS2 pDevOs2 = (PUSBPROXYDEVOS2)pProxyDev->Backend.pv;
    PUSBPROXYURBOS2 pUrbOs2;

    RTCritSectEnter(&pDevOs2->CritSect);

    /*
     * Try remove a linux URB from the free list, if none there allocate a new one.
     */
    pUrbOs2 = pDevOs2->pFreeHead;
    if (pUrbOs2)
        pDevOs2->pFreeHead = pUrbOs2->pNext;
    else
    {
        RTCritSectLeave(&pDevOs2->CritSect);
        pUrbOs2 = (PUSBPROXYURBOS2)RTMemAlloc(sizeof(*pUrbOs2));
        if (!pUrbOs2)
            return NULL;
        RTCritSectEnter(&pDevOs2->CritSect);
    }

    /*
     * Link it into the active list
     */
    pUrbOs2->pPrev = NULL;
    pUrbOs2->pNext = pDevOs2->pInFlightHead;
    if (pUrbOs2->pNext)
        pUrbOs2->pNext->pPrev = pUrbOs2;
    pDevOs2->pInFlightHead = pUrbOs2;

    RTCritSectLeave(&pDevOs2->CritSect);
    return pUrbOs2;
}


/**
 * Frees a linux URB request structure.
 *
 * @param   pProxyDev       The proxy device instance.
 * @param   pUrbOs2         The linux URB to free.
 */
static void usbProxyOs2UrbFree(PUSBPROXYDEV pProxyDev, PUSBPROXYURBOS2 pUrbOs2)
{
    PUSBPROXYDEVOS2 pDevOs2 = (PUSBPROXYDEVOS2)pProxyDev->Backend.pv;

    RTCritSectEnter(&pDevOs2->CritSect);

    /*
     * Remove from the active list.
     */
    if (pUrbOs2->pNext)
        pUrbOs2->pNext->pPrev = pUrbOs2->pPrev;
    else if (pDevOs2->pTaxingTail == pUrbOs2)
        pDevOs2->pTaxingTail = pUrbOs2->pPrev;
    else if (pDevOs2->pTodoTail == pUrbOs2)
        pDevOs2->pTodoTail = pUrbOs2->pPrev;

    if (pUrbOs2->pPrev)
        pUrbOs2->pPrev->pNext = pUrbOs2->pNext;
    else if (pDevOs2->pTaxingHead == pUrbOs2)
        pDevOs2->pTaxingHead  = pUrbOs2->pNext;
    else if (pDevOs2->pInFlightHead == pUrbOs2)
        pDevOs2->pInFlightHead  = pUrbOs2->pNext;
    else if (pDevOs2->pTodoHead == pUrbOs2)
        pDevOs2->pTodoHead  = pUrbOs2->pNext;

    /*
     * Link it into the free list.
     */
    pUrbOs2->pPrev = NULL;
    pUrbOs2->pNext = pDevOs2->pFreeHead;
    pDevOs2->pFreeHead = pUrbOs2;

    RTCritSectLeave(&pDevOs2->CritSect);
}


/**
 * Thread for executing the URBs asynchronously.
 *
 * @returns VINF_SUCCESS.
 * @param   Thread      Thread handle (IPRT).
 * @param   pvProxyDev  Pointer to the proxy device we're servicing.
 */
static DECLCALLBACK(int) usbProxyOs2AsyncThread(RTTHREAD Thread, void *pvProxyDev)
{
    PUSBPROXYDEV pProxyDev = (PUSBPROXYDEV)pvProxyDev;
    PUSBPROXYDEVOS2 pDevOs2 = (PUSBPROXYDEVOS2)pProxyDev->Backend.pv;
    size_t cbLow = 0;
    void *pvLow = NULL;


    /*
     * The main loop.
     *
     * We're always in the critsect, except when waiting or submitting a URB.
     */
    int rc = RTCritSectEnter(&pDevOs2->CritSect); AssertRC(rc);

    while (!pDevOs2->fTerminate)
    {
        /*
         * Anything to do?
         */
        PUSBPROXYURBOS2 pUrbOs2 = pDevOs2->pTodoHead;
        if (pUrbOs2)
        {
            pDevOs2->pTodoHead = pUrbOs2->pNext;
            if (pUrbOs2->pNext)
                pUrbOs2->pNext->pPrev = NULL;
            else
                pDevOs2->pTodoTail = NULL;

            /*
             * Move it to the in-flight list and submit it.
             */
            pUrbOs2->pPrev = NULL;
            pUrbOs2->pNext = pDevOs2->pInFlightHead;
            if (pDevOs2->pInFlightHead)
                pDevOs2->pInFlightHead->pPrev = pUrbOs2;
            //else
            //    pDevOs2->pInFlightTail = pUrbOs2;
            Log3(("%s: usbProxyOs2AsyncThread: pPickup\n", pUrbOs2->pUrb->pszDesc));

            RTCritSectLeave(&pDevOs2->CritSect);

            /*
             * Process the URB.
             */
            PVUSBURB pUrb = pUrbOs2->pUrb;
            uint8_t *pbData = &pUrb->abData[0];
            ULONG cbData = pUrb->cbData;
            if (    (uintptr_t)pbData >= 0x20000000
                ||  ((uintptr_t)pbData & 0xfff))
            {
                if (cbData > cbLow)
                {
                    if (pvLow)
                        DosFreeMem(pvLow);
                    cbLow = (cbData + 0xffff) & ~0xffff;
                    rc = DosAllocMem(&pvLow, cbLow, PAG_WRITE | PAG_READ | OBJ_TILE | PAG_COMMIT);
                    if (rc)
                    {
                        cbLow = 0;
                        pvLow = NULL;
                    }
                }
                if (pvLow)
                    pbData = (uint8_t *)memcpy(pvLow, pbData, cbData);
            }

            switch (pUrb->enmType)
            {
                case VUSBXFERTYPE_MSG:
                {
                    PVUSBSETUP pSetup = (PVUSBSETUP)&pbData[0];
                    Log2(("%s: usbProxyOs2AsyncThread: CtlrMsg\n", pUrb->pszDesc));
                    rc = g_pfnUsbCtrlMessage(pDevOs2->hDevice,  /** @todo this API must take a endpoint number! */
                                             pSetup->bmRequestType,
                                             pSetup->bRequest,
                                             pSetup->wValue,
                                             pSetup->wIndex,
                                             pSetup->wLength,
                                             pSetup + 1,
                                             5*60000 /* min */);
                    break;
                }

                case VUSBXFERTYPE_BULK:
                {
                    /* there is a thing with altnative interface thing here...  */

                    if (pUrb->enmDir == VUSBDIRECTION_IN)
                    {
                        Log2(("%s: usbProxyOs2AsyncThread: BulkRead %d\n", pUrb->pszDesc, cbData));
                        rc = g_pfnUsbBulkRead2(pDevOs2->hDevice, pUrb->EndPt | 0x80, 0, !pUrb->fShortNotOk, &cbData, pbData, 500);//5*6000);
                    }
                    else
                    {
                        Log2(("%s: usbProxyOs2AsyncThread: BulkWrite %d\n", pUrb->pszDesc, cbData));
                        rc = g_pfnUsbBulkWrite2(pDevOs2->hDevice, pUrb->EndPt, 0, !pUrb->fShortNotOk, cbData, pbData, 500);//5*6000);
                    }
                    break;
                }

                case VUSBXFERTYPE_INTR:
                case VUSBXFERTYPE_ISOC:
                default:
                    Log2(("%s: usbProxyOs2AsyncThread: Unsupported\n", pUrb->pszDesc));
                    rc = USB_IORB_FAILED;
                    break;
            }

            /* unbuffer */
            if (pbData == pvLow)
                memcpy(pUrb->abData, pbData, pUrb->cbData);

            /* Convert rc to USB status code. */
            int orc = rc;
            if (!rc)
                pUrb->enmStatus = VUSBSTATUS_OK;
            else  if (rc == USB_ERROR_LESSTRANSFERED && !pUrb->fShortNotOk)
            {
                Assert(pUrb->cbData >= cbData);
                pUrb->cbData = cbData;
                pUrb->enmStatus = VUSBSTATUS_DATA_UNDERRUN;
            }
            else
                pUrb->enmStatus = VUSBSTATUS_STALL;
            Log2(("%s: usbProxyOs2AsyncThread: orc=%d enmStatus=%d cbData=%d \n", pUrb->pszDesc, orc, pUrb->enmStatus, pUrb->cbData)); NOREF(orc);

            /*
             * Retire it to the completed list
             */
            RTCritSectEnter(&pDevOs2->CritSect);

            pUrbOs2->pNext = NULL;
            pUrbOs2->pPrev = pDevOs2->pTaxingTail;
            if (pDevOs2->pTaxingTail)
                pDevOs2->pTaxingTail->pNext = pUrbOs2;
            else
                pDevOs2->pTaxingHead = pUrbOs2;
            pDevOs2->pTaxingTail = pUrbOs2;

            RTSemEventSignal(pDevOs2->EventSyncWait);
            Log2(("%s: usbProxyOs2AsyncThread: orc=%d enmStatus=%d cbData=%d!\n", pUrb->pszDesc, orc, pUrb->enmStatus, pUrb->cbData)); NOREF(orc);
        }
        else
        {
            RTThreadUserReset(Thread);
            RTCritSectLeave(&pDevOs2->CritSect);

            /*
             * Wait for something to do.
             */
            RTThreadUserWait(Thread, 30*1000 /* 30 sec */);

            RTCritSectEnter(&pDevOs2->CritSect);
        }
    }

    RTCritSectLeave(&pDevOs2->CritSect);
    if (pvLow)
        DosFreeMem(pvLow);
    return VINF_SUCCESS;
}


/**
 * Opens the /proc/bus/usb/bus/addr file.
 *
 * @returns VBox status code.
 * @param   pProxyDev       The device instance.
 * @param   pszAddress      The path to the device.
 */
static int usbProxyOs2Open(PUSBPROXYDEV pProxyDev, const char *pszAddress)
{
    LogFlow(("usbProxyOs2Open: pProxyDev=%p pszAddress=%s\n", pProxyDev, pszAddress));
    int rc;

    /*
     * Lazy init.
     */
#ifdef DYNAMIC_USBCALLS
    if (!g_pfnUsbOpen)
    {
        rc = usbProxyOs2GlobalInit();
        if (RT_FAILURE(rc))
            return rc;
    }
#else
static bool g_fInitialized = false;
    if (!g_fInitialized)
    {
        rc = InitUsbCalls();
        if (rc != NO_ERROR)
            return RTErrConvertFromOS2(rc);
        g_fInitialized = true;
    }
#endif

    /*
     * Parse out the open parameters from the address string.
     */
    uint16_t idProduct = 0;
    uint16_t idVendor = 0;
    uint16_t bcdDevice = 0;
    uint32_t iEnum = 0;
    const char *psz = pszAddress;
    do
    {
        const char chValue = *psz;
        AssertReleaseReturn(psz[1] == '=', VERR_INTERNAL_ERROR);
        uint64_t u64Value;
        int rc = RTStrToUInt64Ex(psz + 2, (char **)&psz, 0, &u64Value);
        AssertReleaseRCReturn(rc, rc);
        AssertReleaseReturn(!*psz || *psz == ';', rc);
        switch (chValue)
        {
            case 'p':   idProduct = (uint16_t)u64Value; break;
            case 'v':   idVendor = (uint16_t)u64Value; break;
            case 'r':   bcdDevice = (uint16_t)u64Value; break;
            case 'e':   iEnum = (uint16_t)u64Value; break;
            default:
                AssertReleaseMsgFailedReturn(("chValue=%#x\n", chValue), VERR_INTERNAL_ERROR);
        }
        if (*psz == ';')
            psz++;
    } while (*psz);


    /*
     * Try open (acquire) it.
     */
    USBHANDLE hDevice = 0;
    int urc = rc = g_pfnUsbOpen(&hDevice, idVendor, idProduct, bcdDevice, iEnum);
    if (!rc)
    {
        /*
         * Allocate and initialize the OS/2 backend data.
         */
        PUSBPROXYDEVOS2 pDevOs2 = (PUSBPROXYDEVOS2)RTMemAllocZ(sizeof(*pDevOs2));
        if (pDevOs2)
        {
            pDevOs2->hDevice = hDevice;
            pDevOs2->fTerminate = false;
            rc = RTCritSectInit(&pDevOs2->CritSect);
            if (RT_SUCCESS(rc))
            {
                rc = RTSemEventCreate(&pDevOs2->EventSyncWait);
                if (RT_SUCCESS(rc))
                {
                    pProxyDev->Backend.pv = pDevOs2;

                    /** @todo
                     * Determine the active configuration.
                     */
                    //pProxyDev->cIgnoreSetConfigs = 1;
                    //pProxyDev->iActiveCfg = 1;
                    pProxyDev->cIgnoreSetConfigs = 0;
                    pProxyDev->iActiveCfg = -1;

                    /*
                     * Create the async worker thread and we're done.
                     */
                    rc = RTThreadCreate(&pDevOs2->Thread, usbProxyOs2AsyncThread, pProxyDev, 0, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "usbproxy");
                    if (RT_SUCCESS(rc))
                    {
                        LogFlow(("usbProxyOs2Open(%p, %s): returns successfully - iActiveCfg=%d\n",
                                 pProxyDev, pszAddress, pProxyDev->iActiveCfg));
                        return VINF_SUCCESS;
                    }

                    /* failure */
                    RTSemEventDestroy(pDevOs2->EventSyncWait);
                }
                RTCritSectDelete(&pDevOs2->CritSect);
            }
            RTMemFree(pDevOs2);
        }
        else
            rc = VERR_NO_MEMORY;
        g_pfnUsbClose(hDevice);
    }
    else
        rc = VERR_VUSB_USBFS_PERMISSION; /** @todo fix me */

    Log(("usbProxyOs2Open(%p, %s) failed, rc=%Rrc! urc=%d\n", pProxyDev, pszAddress, rc, urc)); NOREF(urc);
    pProxyDev->Backend.pv = NULL;

    NOREF(pvBackend);
    return rc;
}


/**
 * Closes the proxy device.
 */
static void usbProxyOs2Close(PUSBPROXYDEV pProxyDev)
{
    LogFlow(("usbProxyOs2Close: pProxyDev=%s\n", pProxyDev->pUsbIns->pszName));
    PUSBPROXYDEVOS2 pDevOs2 = (PUSBPROXYDEVOS2)pProxyDev->Backend.pv;
    Assert(pDevOs2);
    if (!pDevOs2)
        return;

    /*
     * Tell the thread to terminate.
     */
    ASMAtomicXchgBool(&pDevOs2->fTerminate, true);
    int rc = RTThreadUserSignal(pDevOs2->Thread); AssertRC(rc);
    rc = RTThreadWait(pDevOs2->Thread, 60*1000 /* 1 min */, NULL); AssertRC(rc);

    /*
     * Now we can free all the resources and close the device.
     */
    RTCritSectDelete(&pDevOs2->CritSect);
    RTSemEventDestroy(pDevOs2->EventSyncWait);

    Assert(!pDevOs2->pInFlightHead);
    Assert(!pDevOs2->pTodoHead);
    Assert(!pDevOs2->pTodoTail);
    Assert(!pDevOs2->pTaxingHead);
    Assert(!pDevOs2->pTaxingTail);

    PUSBPROXYURBOS2 pUrbOs2;
    while ((pUrbOs2 = pDevOs2->pFreeHead) != NULL)
    {
        pDevOs2->pFreeHead = pUrbOs2->pNext;
        RTMemFree(pUrbOs2);
    }

    g_pfnUsbClose(pDevOs2->hDevice);
    pDevOs2->hDevice = 0;

    RTMemFree(pDevOs2);
    pProxyDev->Backend.pv = NULL;
    LogFlow(("usbProxyOs2Close: returns\n"));
}


/** @interface_method_impl{USBPROXYBACK,pfnReset} */
static int usbProxyOs2Reset(PUSBPROXYDEV pProxyDev, bool fResetOnLinux)
{
    return VINF_SUCCESS;
}


/**
 * SET_CONFIGURATION.
 *
 * The caller makes sure that it's not called first time after open or reset
 * with the active interface.
 *
 * @returns success indicator.
 * @param   pProxyDev       The device instance data.
 * @param   iCfg            The configuration to set.
 */
static int usbProxyOs2SetConfig(PUSBPROXYDEV pProxyDev, int iCfg)
{
    PUSBPROXYDEVOS2 pDevOs2 = (PUSBPROXYDEVOS2)pProxyDev->Backend.pv;
    LogFlow(("usbProxyOs2SetConfig: pProxyDev=%s cfg=%#x\n",
             pProxyDev->pUsbIns->pszName, iCfg));

    /*
     * This is sync - bad.
     */
    int rc = g_pfnUsbCtrlMessage(pDevOs2->hDevice,
                                 0x00,       /* bmRequestType - ?? */
                                 0x09,       /* bRequest      - ?? */
                                 iCfg,       /* wValue        - configuration */
                                 0,          /* wIndex*/
                                 0,          /* wLength */
                                 NULL,       /* pvData  */
                                 50          /* Timeout (ms) */);
    if (rc)
        LogFlow(("usbProxyOs2SetConfig: pProxyDev=%s cfg=%#X -> rc=%d\n", pProxyDev->pUsbIns->pszName, iCfg, rc));
    return rc == 0;
}


/**
 * Claims an interface.
 * @returns success indicator.
 */
static int usbProxyOs2ClaimInterface(PUSBPROXYDEV pProxyDev, int iIf)
{
    LogFlow(("usbProxyOs2ClaimInterface: pProxyDev=%s ifnum=%#x\n", pProxyDev->pUsbIns->pszName, iIf));
    return true;
}


/**
 * Releases an interface.
 * @returns success indicator.
 */
static int usbProxyOs2ReleaseInterface(PUSBPROXYDEV pProxyDev, int iIf)
{
    LogFlow(("usbProxyOs2ReleaseInterface: pProxyDev=%s ifnum=%#x\n", pProxyDev->pUsbIns->pszName, iIf));
    return true;
}


/**
 * SET_INTERFACE.
 *
 * @returns success indicator.
 */
static int usbProxyOs2SetInterface(PUSBPROXYDEV pProxyDev, int iIf, int iAlt)
{
    LogFlow(("usbProxyOs2SetInterface: pProxyDev=%p iIf=%#x iAlt=%#x\n", pProxyDev, iIf, iAlt));
    return true;
}


/**
 * Clears the halted endpoint 'EndPt'.
 */
static bool usbProxyOs2ClearHaltedEp(PUSBPROXYDEV pProxyDev, unsigned int EndPt)
{
    PUSBPROXYDEVOS2 pDevOs2 = (PUSBPROXYDEVOS2)pProxyDev->Backend.pv;
    LogFlow(("usbProxyOs2ClearHaltedEp: pProxyDev=%s EndPt=%x\n", pProxyDev->pUsbIns->pszName, EndPt));

    /*
     * This is sync - bad.
     */
    int rc = g_pfnUsbCtrlMessage(pDevOs2->hDevice,
                                 0x02,       /* bmRequestType - ?? */
                                 0x01,       /* bRequest      - ?? */
                                 0,          /* wValue        - endpoint halt */
                                 EndPt,      /* wIndex        - endpoint # */
                                 0,          /* wLength */
                                 NULL,       /* pvData  */
                                 50          /* Timeout (ms) */);
    if (rc)
        LogFlow(("usbProxyOs2ClearHaltedEp: pProxyDev=%s EndPt=%u -> rc=%d\n", pProxyDev->pUsbIns->pszName, EndPt, rc));
    return rc == 0;
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnUrbQueue}
 */
static int usbProxyOs2UrbQueue(PUSBPROXYDEV pProxyDev, PVUSBURB pUrb)
{
    PUSBPROXYDEVOS2 pDevOs2 = (PUSBPROXYDEVOS2)pProxyDev->Backend.pv;
    LogFlow(("usbProxyOs2UrbQueue: pProxyDev=%s pUrb=%p EndPt=%d cbData=%d\n",
             pProxyDev->pUsbIns->pszName, pUrb, pUrb->EndPt, pUrb->cbData));

    /*
     * Quickly validate the input.
     */
    switch (pUrb->enmDir)
    {
        case VUSBDIRECTION_IN:
        case VUSBDIRECTION_OUT:
            break;
        default:
            AssertMsgFailed(("usbProxyOs2UrbQueue: Invalid direction %d\n", pUrb->enmDir));
            return false;
    }

    switch (pUrb->enmType)
    {
        case VUSBXFERTYPE_MSG:
            break;
        case VUSBXFERTYPE_BULK:
            break;
/// @todo        case VUSBXFERTYPE_INTR:
//            break;
//        case VUSBXFERTYPE_ISOC:
//            break;
        default:
            return false;
    }

    /*
     * Allocate an OS/2 urb tracking structure, initialize it,
     * add it to the todo list, and wake up the async thread.
     */
    PUSBPROXYURBOS2 pUrbOs2 = usbProxyOs2UrbAlloc(pProxyDev);
    if (!pUrbOs2)
        return false;

    pUrbOs2->pUrb = pUrb;

    RTCritSectEnter(&pDevOs2->CritSect);

    pUrbOs2->pNext = NULL;
    pUrbOs2->pPrev = pDevOs2->pTodoTail;
    if (pDevOs2->pTodoTail)
        pDevOs2->pTodoTail->pNext = pUrbOs2;
    else
        pDevOs2->pTodoHead = pUrbOs2;
    pDevOs2->pTodoTail = pUrbOs2;

    RTCritSectLeave(&pDevOs2->CritSect);

    RTThreadUserSignal(pDevOs2->Thread);
    return true;
}


/**
 * Reap URBs in-flight on a device.
 *
 * @returns Pointer to a completed URB.
 * @returns NULL if no URB was completed.
 * @param   pProxyDev   The device.
 * @param   cMillies    Number of milliseconds to wait. Use 0 to not wait at all.
 */
static PVUSBURB usbProxyOs2UrbReap(PUSBPROXYDEV pProxyDev, RTMSINTERVAL cMillies)
{
    PVUSBURB pUrb = NULL;
    PUSBPROXYDEVOS2 pDevOs2 = (PUSBPROXYDEVOS2)pProxyDev->Backend.pv;

    RTCritSectEnter(&pDevOs2->CritSect);
    for (;;)
    {
        /*
         * Any URBs pending delivery?
         */
        PUSBPROXYURBOS2 pUrbOs2 = pDevOs2->pTaxingHead;
        if (pUrbOs2)
        {
            pUrb = pUrbOs2->pUrb;
            usbProxyOs2UrbFree(pProxyDev, pUrbOs2);
            break;
        }

        /*
         * Block for something to completed, if requested and sensible.
         */
        if (!cMillies)
            break;
        if (    !pDevOs2->pInFlightHead
            &&  !pDevOs2->pTodoHead)
            break;

        RTCritSectLeave(&pDevOs2->CritSect);

        int rc = RTSemEventWait(pDevOs2->EventSyncWait, cMillies);
        Assert(RT_SUCCESS(rc) || rc == VERR_TIMEOUT); NOREF(rc);
        cMillies = 0;

        RTCritSectEnter(&pDevOs2->CritSect);
    }
    RTCritSectLeave(&pDevOs2->CritSect);

    LogFlow(("usbProxyOs2UrbReap: dev=%s returns %p\n", pProxyDev->pUsbIns->pszName, pUrb));
    return pUrb;
}


/**
 * Cancels the URB.
 * The URB requires reaping, so we don't change its state.
 */
static void usbProxyOs2UrbCancel(PVUSBURB pUrb)
{
#if 0
    PUSBPROXYDEV pProxyDev = (PUSBPROXYDEV)pUrb->pDev;
    PUSBPROXYURBOS2 pUrbOs2 = (PUSBPROXYURBOS2)pUrb->Dev.pvProxyUrb;
    if (pUrbOs2->pSplitHead)
    {
        /* split */
        Assert(pUrbOs2 == pUrbOs2->pSplitHead);
        for (PUSBPROXYURBOS2 pCur = pUrbOs2; pCur; pCur = pCur->pSplitNext)
        {
            if (pCur->fSplitElementReaped)
                continue;
            if (    !usbProxyOs2DoIoCtl(pProxyDev, USBDEVFS_DISCARDURB, &pCur->KUrb, true, UINT32_MAX)
                ||  errno == ENOENT)
                continue;
            if (errno == ENODEV)
                break;
            Log(("usb-linux: Discard URB %p failed, errno=%d. pProxyDev=%s!!! (split)\n",
                 pUrb, errno, pProxyDev->pUsbIns->pszName));
        }
    }
    else
    {
        /* unsplit */
        if (    usbProxyOs2DoIoCtl(pProxyDev, USBDEVFS_DISCARDURB, &pUrbOs2->KUrb, true, UINT32_MAX)
            &&  errno != ENODEV /* deal with elsewhere. */
            &&  errno != ENOENT)
            Log(("usb-linux: Discard URB %p failed, errno=%d. pProxyDev=%s!!!\n",
                 pUrb, errno, pProxyDev->pUsbIns->pszName));
    }
#endif
}


/**
 * The Linux USB Proxy Backend.
 */
extern const USBPROXYBACK g_USBProxyDeviceHost =
{
    "host",
    usbProxyOs2Open,
    NULL,
    usbProxyOs2Close,
    usbProxyOs2Reset,
    usbProxyOs2SetConfig,
    usbProxyOs2ClaimInterface,
    usbProxyOs2ReleaseInterface,
    usbProxyOs2SetInterface,
    usbProxyOs2ClearHaltedEp,
    usbProxyOs2UrbQueue,
    usbProxyOs2UrbCancel,
    usbProxyOs2UrbReap,
    0
};

