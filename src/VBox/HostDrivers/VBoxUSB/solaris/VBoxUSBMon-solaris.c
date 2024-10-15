/* $Id: VBoxUSBMon-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB Monitor Driver, Solaris Hosts.
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
#define LOG_GROUP  LOG_GROUP_USB_DRV
#include "VBoxUSBFilterMgr.h"
#include <VBox/usblib-solaris.h>
#include <VBox/version.h>
#include <VBox/log.h>
#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/version.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/process.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>

#define USBDRV_MAJOR_VER    2
#define USBDRV_MINOR_VER    0
#include <sys/usb/usba.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/mutex.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/open.h>
#include <sys/cmn_err.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The module name. */
#define DEVICE_NAME              "vboxusbmon"
/** The module description as seen in 'modinfo'. */
#define DEVICE_DESC_DRV          "VirtualBox USBMon"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int VBoxUSBMonSolarisOpen(dev_t *pDev, int fFlag, int fType, cred_t *pCred);
static int VBoxUSBMonSolarisClose(dev_t Dev, int fFlag, int fType, cred_t *pCred);
static int VBoxUSBMonSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred);
static int VBoxUSBMonSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred);
static int VBoxUSBMonSolarisIOCtl(dev_t Dev, int Cmd, intptr_t pArg, int Mode, cred_t *pCred, int *pVal);
static int VBoxUSBMonSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pArg, void **ppResult);
static int VBoxUSBMonSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd);
static int VBoxUSBMonSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * cb_ops: for drivers that support char/block entry points
 */
static struct cb_ops g_VBoxUSBMonSolarisCbOps =
{
    VBoxUSBMonSolarisOpen,
    VBoxUSBMonSolarisClose,
    nodev,                      /* b strategy */
    nodev,                      /* b dump */
    nodev,                      /* b print */
    VBoxUSBMonSolarisRead,
    VBoxUSBMonSolarisWrite,
    VBoxUSBMonSolarisIOCtl,
    nodev,                      /* c devmap */
    nodev,                      /* c mmap */
    nodev,                      /* c segmap */
    nochpoll,                   /* c poll */
    ddi_prop_op,                /* property ops */
    NULL,                       /* streamtab  */
    D_NEW | D_MP,               /* compat. flag */
    CB_REV                      /* revision */
};

/**
 * dev_ops: for driver device operations
 */
static struct dev_ops g_VBoxUSBMonSolarisDevOps =
{
    DEVO_REV,                   /* driver build revision */
    0,                          /* ref count */
    VBoxUSBMonSolarisGetInfo,
    nulldev,                    /* identify */
    nulldev,                    /* probe */
    VBoxUSBMonSolarisAttach,
    VBoxUSBMonSolarisDetach,
    nodev,                      /* reset */
    &g_VBoxUSBMonSolarisCbOps,
    (struct bus_ops *)0,
    nodev,                      /* power */
    ddi_quiesce_not_needed
};

/**
 * modldrv: export driver specifics to the kernel
 */
static struct modldrv g_VBoxUSBMonSolarisModule =
{
    &mod_driverops,             /* extern from kernel */
    DEVICE_DESC_DRV " " VBOX_VERSION_STRING "r" RT_XSTR(VBOX_SVN_REV),
    &g_VBoxUSBMonSolarisDevOps
};

/**
 * modlinkage: export install/remove/info to the kernel
 */
static struct modlinkage g_VBoxUSBMonSolarisModLinkage =
{
    MODREV_1,
    &g_VBoxUSBMonSolarisModule,
    NULL,
};

/**
 * Client driver info.
 */
typedef struct vboxusbmon_client_t
{
    dev_info_t                 *pDip;                       /* Client device info. pointer */
    VBOXUSB_CLIENT_INFO         Info;                       /* Client registration data. */
    struct vboxusbmon_client_t *pNext;                      /* Pointer to next client */
} vboxusbmon_client_t;

/**
 * Device state info.
 */
typedef struct
{
    RTPROCESS                   Process;                    /* The process (id) of the session */
} vboxusbmon_state_t;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Global Device handle we only support one instance. */
static dev_info_t *g_pDip = NULL;
/** Global Mutex. */
static kmutex_t g_VBoxUSBMonSolarisMtx;
/** Global list of client drivers registered with us. */
vboxusbmon_client_t *g_pVBoxUSBMonSolarisClients = NULL;
/** Opaque pointer to list of soft states. */
static void *g_pVBoxUSBMonSolarisState;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int vboxUSBMonSolarisProcessIOCtl(int iFunction, void *pvState, void *pvData, size_t cbData, size_t *pcbReturnedData);
static int vboxUSBMonSolarisResetDevice(char *pszDevicePath, bool fReattach);


/*********************************************************************************************************************************
*   Monitor Global Hooks                                                                                                         *
*********************************************************************************************************************************/
static int vboxUSBMonSolarisClientInfo(vboxusbmon_state_t *pState, PVBOXUSB_CLIENT_INFO pClientInfo);
int VBoxUSBMonSolarisRegisterClient(dev_info_t *pClientDip, PVBOXUSB_CLIENT_INFO pClientInfo);
int VBoxUSBMonSolarisUnregisterClient(dev_info_t *pClientDip);
int VBoxUSBMonSolarisElectDriver(usb_dev_descr_t *pDevDesc, usb_dev_str_t *pDevStrings, char *pszDevicePath, int Bus, int Port,
                                char **ppszDrv, void *pvReserved);


/**
 * Kernel entry points
 */
int _init(void)
{
    int rc;

    LogFunc((DEVICE_NAME ": _init\n"));

    g_pDip = NULL;

    /*
     * Prevent module autounloading.
     */
    modctl_t *pModCtl = mod_getctl(&g_VBoxUSBMonSolarisModLinkage);
    if (pModCtl)
        pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD;
    else
        LogRel((DEVICE_NAME ": _init: Failed to disable autounloading!\n"));

    /*
     * Initialize IPRT R0 driver, which internally calls OS-specific r0 init.
     */
    rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize global mutex.
         */
        mutex_init(&g_VBoxUSBMonSolarisMtx, NULL, MUTEX_DRIVER, NULL);
        rc = VBoxUSBFilterInit();
        if (RT_SUCCESS(rc))
        {
            rc = ddi_soft_state_init(&g_pVBoxUSBMonSolarisState, sizeof(vboxusbmon_state_t), 1);
            if (!rc)
            {
                rc = mod_install(&g_VBoxUSBMonSolarisModLinkage);
                if (!rc)
                    return rc;

                LogRel((DEVICE_NAME ": _init: mod_install failed! rc=%d\n", rc));
                ddi_soft_state_fini(&g_pVBoxUSBMonSolarisState);
            }
            else
                LogRel((DEVICE_NAME ": _init: ddi_soft_state_init failed! rc=%d\n", rc));
        }
        else
            LogRel((DEVICE_NAME ": _init: VBoxUSBFilterInit failed! rc=%d\n", rc));

        mutex_destroy(&g_VBoxUSBMonSolarisMtx);
        RTR0Term();
    }
    else
        LogRel((DEVICE_NAME ": _init: RTR0Init failed! rc=%d\n", rc));

    return -1;
}


int _fini(void)
{
    int rc;

    LogFunc((DEVICE_NAME ": _fini\n"));

    rc = mod_remove(&g_VBoxUSBMonSolarisModLinkage);
    if (!rc)
    {
        ddi_soft_state_fini(&g_pVBoxUSBMonSolarisState);
        VBoxUSBFilterTerm();
        mutex_destroy(&g_VBoxUSBMonSolarisMtx);

        RTR0Term();
    }
    return rc;
}


int _info(struct modinfo *pModInfo)
{
    LogFunc((DEVICE_NAME ": _info\n"));

    return mod_info(&g_VBoxUSBMonSolarisModLinkage, pModInfo);
}


/**
 * Attach entry point, to attach a device to the system or resume it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Attach type (ddi_attach_cmd_t)
 *
 * @returns corresponding solaris error code.
 */
static int VBoxUSBMonSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ": VBoxUSBMonSolarisAttach: pDip=%p enmCmd=%d\n", pDip, enmCmd));
    switch (enmCmd)
    {
        case DDI_ATTACH:
        {
            if (RT_UNLIKELY(g_pDip))
            {
                LogRel((DEVICE_NAME ": VBoxUSBMonSolarisAttach: Global instance already initialized\n"));
                return DDI_FAILURE;
            }

            g_pDip = pDip;
            int rc = ddi_create_priv_minor_node(pDip, DEVICE_NAME, S_IFCHR, 0 /* instance */, DDI_PSEUDO, 0 /* flags */,
                                                "none", "none", 0660);
            if (rc == DDI_SUCCESS)
            {
                rc = usb_register_dev_driver(g_pDip, VBoxUSBMonSolarisElectDriver);
                if (rc == DDI_SUCCESS)
                {
                    ddi_report_dev(pDip);
                    return DDI_SUCCESS;
                }

                LogRel((DEVICE_NAME ": VBoxUSBMonSolarisAttach: Failed to register driver election callback! rc=%d\n", rc));
            }
            else
                LogRel((DEVICE_NAME ": VBoxUSBMonSolarisAttach: ddi_create_minor_node failed! rc=%d\n", rc));
            return DDI_FAILURE;
        }

        case DDI_RESUME:
        {
            /* We don't have to bother about power management. */
            return DDI_SUCCESS;
        }

        default:
            return DDI_FAILURE;
    }
}


/**
 * Detach entry point, to detach a device to the system or suspend it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Attach type (ddi_attach_cmd_t)
 *
 * @returns corresponding solaris error code.
 */
static int VBoxUSBMonSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ": VBoxUSBMonSolarisDetach\n"));

    switch (enmCmd)
    {
        case DDI_DETACH:
        {
            /*
             * Free all registered clients' info.
             */
            mutex_enter(&g_VBoxUSBMonSolarisMtx);
            vboxusbmon_client_t *pCur = g_pVBoxUSBMonSolarisClients;
            while (pCur)
            {
                vboxusbmon_client_t *pNext = pCur->pNext;
                RTMemFree(pCur);
                pCur = pNext;
            }
            mutex_exit(&g_VBoxUSBMonSolarisMtx);

            usb_unregister_dev_driver(g_pDip);

            ddi_remove_minor_node(pDip, NULL);
            g_pDip = NULL;
            return DDI_SUCCESS;
        }

        case DDI_SUSPEND:
        {
            /* We don't have to bother about power management. */
            return DDI_SUCCESS;
        }

        default:
            return DDI_FAILURE;
    }
}


/**
 * Info entry point, called by solaris kernel for obtaining driver info.
 *
 * @param   pDip            The module structure instance (do not use).
 * @param   enmCmd          Information request type.
 * @param   pvArg           Type specific argument.
 * @param   ppvResult       Where to store the requested info.
 *
 * @returns corresponding solaris error code.
 */
static int VBoxUSBMonSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pvArg, void **ppvResult)
{
    int rc = DDI_SUCCESS;

    LogFunc((DEVICE_NAME ": VBoxUSBMonSolarisGetInfo\n"));

    switch (enmCmd)
    {
        case DDI_INFO_DEVT2DEVINFO:
        {
            *ppvResult = (void *)g_pDip;
            if (!*ppvResult)
                rc = DDI_FAILURE;
            break;
        }

        case DDI_INFO_DEVT2INSTANCE:
        {
            /* There can only be a single-instance of this driver and thus its instance number is 0. */
            *ppvResult = (void *)0;
            break;
        }

        default:
            rc = DDI_FAILURE;
            break;
    }
    return rc;
}


static int VBoxUSBMonSolarisOpen(dev_t *pDev, int fFlag, int fType, cred_t *pCred)
{
    vboxusbmon_state_t *pState = NULL;
    unsigned iOpenInstance;

    LogFunc((DEVICE_NAME ": VBoxUSBMonSolarisOpen\n"));

    /*
     * Verify we are being opened as a character device.
     */
    if (fType != OTYP_CHR)
        return EINVAL;

    /*
     * Verify that we're called after attach.
     */
    if (!g_pDip)
    {
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisOpen: Invalid state for opening\n"));
        return ENXIO;
    }

    for (iOpenInstance = 0; iOpenInstance < 4096; iOpenInstance++)
    {
        if (    !ddi_get_soft_state(g_pVBoxUSBMonSolarisState, iOpenInstance) /* faster */
            &&  ddi_soft_state_zalloc(g_pVBoxUSBMonSolarisState, iOpenInstance) == DDI_SUCCESS)
        {
            pState = ddi_get_soft_state(g_pVBoxUSBMonSolarisState, iOpenInstance);
            break;
        }
    }
    if (!pState)
    {
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisOpen: Too many open instances"));
        return ENXIO;
    }

    pState->Process = RTProcSelf();
    *pDev = makedevice(getmajor(*pDev), iOpenInstance);

    NOREF(fFlag);
    NOREF(pCred);

    return 0;
}


static int VBoxUSBMonSolarisClose(dev_t Dev, int fFlag, int fType, cred_t *pCred)
{
    vboxusbmon_state_t *pState = NULL;
    LogFunc((DEVICE_NAME ": VBoxUSBMonSolarisClose\n"));

    pState = ddi_get_soft_state(g_pVBoxUSBMonSolarisState, getminor(Dev));
    if (!pState)
    {
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisClose: Failed to get state\n"));
        return EFAULT;
    }

    /*
     * Remove all filters for this client process.
     */
    VBoxUSBFilterRemoveOwner(pState->Process);

    ddi_soft_state_free(g_pVBoxUSBMonSolarisState, getminor(Dev));
    pState = NULL;

    NOREF(fFlag);
    NOREF(fType);
    NOREF(pCred);

    return 0;
}


static int VBoxUSBMonSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    LogFunc((DEVICE_NAME ": VBoxUSBMonSolarisRead\n"));
    return 0;
}


static int VBoxUSBMonSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    LogFunc((DEVICE_NAME ": VBoxUSBMonSolarisWrite\n"));
    return 0;
}


/** @def IOCPARM_LEN
 * Gets the length from the ioctl number.
 * This is normally defined by sys/ioccom.h on BSD systems...
 */
#ifndef IOCPARM_LEN
# define IOCPARM_LEN(Code)                      (((Code) >> 16) & IOCPARM_MASK)
#endif

static int VBoxUSBMonSolarisIOCtl(dev_t Dev, int Cmd, intptr_t pArg, int Mode, cred_t *pCred, int *pVal)
{
    LogFunc((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: Dev=%d Cmd=%d pArg=%p Mode=%d\n", Dev, Cmd, pArg));

    /*
     * Get the session from the soft state item.
     */
    vboxusbmon_state_t *pState = ddi_get_soft_state(g_pVBoxUSBMonSolarisState, getminor(Dev));
    if (!pState)
    {
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: No state data for minor instance %d\n", getminor(Dev)));
        return EINVAL;
    }

    /*
     * Read the request wrapper. Though We don't really need wrapper struct. now
     * it's room for the future as Solaris isn't generous regarding the size.
     */
    VBOXUSBREQ ReqWrap;
    if (IOCPARM_LEN(Cmd) != sizeof(ReqWrap))
    {
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: bad request %#x size=%d expected=%d\n", Cmd, IOCPARM_LEN(Cmd),
                sizeof(ReqWrap)));
        return ENOTTY;
    }

    int rc = ddi_copyin((void *)pArg, &ReqWrap, sizeof(ReqWrap), Mode);
    if (RT_UNLIKELY(rc))
    {
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: ddi_copyin failed to read header pArg=%p Cmd=%d. rc=%d\n", pArg, Cmd, rc));
        return EINVAL;
    }

    if (ReqWrap.u32Magic != VBOXUSBMON_MAGIC)
    {
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: Bad magic %#x; pArg=%p Cmd=%d\n", ReqWrap.u32Magic, pArg, Cmd));
        return EINVAL;
    }
    if (RT_UNLIKELY(   ReqWrap.cbData == 0
                    || ReqWrap.cbData > _1M*16))
    {
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: Bad size %#x; pArg=%p Cmd=%d\n", ReqWrap.cbData, pArg, Cmd));
        return EINVAL;
    }

    /*
     * Read the request.
     */
    void *pvBuf = RTMemTmpAlloc(ReqWrap.cbData);
    if (RT_UNLIKELY(!pvBuf))
    {
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: RTMemTmpAlloc failed to alloc %d bytes\n", ReqWrap.cbData));
        return ENOMEM;
    }

    rc = ddi_copyin((void *)(uintptr_t)ReqWrap.pvDataR3, pvBuf, ReqWrap.cbData, Mode);
    if (RT_UNLIKELY(rc))
    {
        RTMemTmpFree(pvBuf);
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: ddi_copyin failed; pvBuf=%p pArg=%p Cmd=%d. rc=%d\n", pvBuf, pArg, Cmd,
                rc));
        return EFAULT;
    }
    if (RT_UNLIKELY(   ReqWrap.cbData != 0
                    && !RT_VALID_PTR(pvBuf)))
    {
        RTMemTmpFree(pvBuf);
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: pvBuf Invalid pointer %p\n", pvBuf));
        return EINVAL;
    }
    Log((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: pid=%d\n", (int)RTProcSelf()));

    /*
     * Process the IOCtl.
     */
    size_t cbDataReturned = 0;
    rc = vboxUSBMonSolarisProcessIOCtl(Cmd, pState, pvBuf, ReqWrap.cbData, &cbDataReturned);
    ReqWrap.rc = rc;
    rc = 0;

    if (RT_UNLIKELY(cbDataReturned > ReqWrap.cbData))
    {
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: Too much output data %d expected %d\n", cbDataReturned, ReqWrap.cbData));
        cbDataReturned = ReqWrap.cbData;
    }

    ReqWrap.cbData = cbDataReturned;

    /*
     * Copy the request back to user space.
     */
    rc = ddi_copyout(&ReqWrap, (void *)pArg, sizeof(ReqWrap), Mode);
    if (RT_LIKELY(!rc))
    {
        /*
         * Copy the payload (if any) back to user space.
         */
        if (cbDataReturned > 0)
        {
            rc = ddi_copyout(pvBuf, (void *)(uintptr_t)ReqWrap.pvDataR3, cbDataReturned, Mode);
            if (RT_UNLIKELY(rc))
            {
                LogRel((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: ddi_copyout failed; pvBuf=%p pArg=%p Cmd=%d. rc=%d\n", pvBuf,
                        pArg, Cmd, rc));
                rc = EFAULT;
            }
        }
    }
    else
    {
        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisIOCtl: ddi_copyout(1) failed pArg=%p Cmd=%d\n", pArg, Cmd));
        rc = EFAULT;
    }

    *pVal = rc;
    RTMemTmpFree(pvBuf);
    return rc;
}


/**
 * IOCtl processor for user to kernel and kernel to kernel communication.
 *
 * @returns  VBox status code.
 *
 * @param   iFunction           The requested function.
 * @param   pvState             Opaque pointer to driver state used for getting
 *                              ring-3 process (Id).
 * @param   pvData              The input/output data buffer. Can be NULL
 *                              depending on the function.
 * @param   cbData              The max size of the data buffer.
 * @param   pcbReturnedData     Where to store the amount of returned data.  Can
 *                              be NULL.
 */
static int vboxUSBMonSolarisProcessIOCtl(int iFunction, void *pvState, void *pvData, size_t cbData, size_t *pcbReturnedData)
{
    LogFunc((DEVICE_NAME ": vboxUSBMonSolarisProcessIOCtl: iFunction=%d pvBuf=%p cbBuf=%zu\n", iFunction, pvData, cbData));

    AssertPtrReturn(pvState, VERR_INVALID_POINTER);
    vboxusbmon_state_t *pState = (vboxusbmon_state_t *)pvState;
    int rc;

#define CHECKRET_MIN_SIZE(mnemonic, cbMin) \
    do { \
        if (RT_UNLIKELY(cbData < (cbMin))) \
        { \
            LogRel(("vboxUSBSolarisProcessIOCtl: " mnemonic ": cbData=%#zx (%zu) min is %#zx (%zu)\n", \
                 cbData, cbData, (size_t)(cbMin), (size_t)(cbMin))); \
            return VERR_BUFFER_OVERFLOW; \
        } \
        if (RT_UNLIKELY((cbMin) != 0 && !RT_VALID_PTR(pvData))) \
        { \
            LogRel(("vboxUSBSolarisProcessIOCtl: " mnemonic ": Invalid pointer %p\n", pvData)); \
            return VERR_INVALID_POINTER; \
        } \
    } while (0)

    switch (iFunction)
    {
        case VBOXUSBMON_IOCTL_ADD_FILTER:
        {
            CHECKRET_MIN_SIZE("ADD_FILTER", sizeof(VBOXUSBREQ_ADD_FILTER));

            VBOXUSBREQ_ADD_FILTER *pReq = (VBOXUSBREQ_ADD_FILTER *)pvData;
            PUSBFILTER pFilter = (PUSBFILTER)&pReq->Filter;

            Log(("vboxUSBMonSolarisProcessIOCtl: idVendor=%#x idProduct=%#x bcdDevice=%#x bDeviceClass=%#x "
                 "bDeviceSubClass=%#x bDeviceProtocol=%#x bBus=%#x bPort=%#x\n",
                      USBFilterGetNum(pFilter, USBFILTERIDX_VENDOR_ID),
                      USBFilterGetNum(pFilter, USBFILTERIDX_PRODUCT_ID),
                      USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_REV),
                      USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_CLASS),
                      USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_SUB_CLASS),
                      USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_PROTOCOL),
                      USBFilterGetNum(pFilter, USBFILTERIDX_BUS),
                      USBFilterGetNum(pFilter, USBFILTERIDX_PORT)));
            Log(("vboxUSBMonSolarisProcessIOCtl: Manufacturer=%s Product=%s Serial=%s\n",
                      USBFilterGetString(pFilter, USBFILTERIDX_MANUFACTURER_STR)  ? USBFilterGetString(pFilter, USBFILTERIDX_MANUFACTURER_STR)  : "<null>",
                      USBFilterGetString(pFilter, USBFILTERIDX_PRODUCT_STR)       ? USBFilterGetString(pFilter, USBFILTERIDX_PRODUCT_STR)       : "<null>",
                      USBFilterGetString(pFilter, USBFILTERIDX_SERIAL_NUMBER_STR) ? USBFilterGetString(pFilter, USBFILTERIDX_SERIAL_NUMBER_STR) : "<null>"));

            rc = USBFilterSetMustBePresent(pFilter, USBFILTERIDX_BUS, false /* fMustBePresent */);      AssertRC(rc);

            rc = VBoxUSBFilterAdd(pFilter, pState->Process, &pReq->uId);
            *pcbReturnedData = cbData;
            Log((DEVICE_NAME ": vboxUSBMonSolarisProcessIOCtl: ADD_FILTER (Process:%d) returned %d\n", pState->Process, rc));
            break;
        }

        case VBOXUSBMON_IOCTL_REMOVE_FILTER:
        {
            CHECKRET_MIN_SIZE("REMOVE_FILTER", sizeof(VBOXUSBREQ_REMOVE_FILTER));

            VBOXUSBREQ_REMOVE_FILTER *pReq = (VBOXUSBREQ_REMOVE_FILTER *)pvData;
            rc = VBoxUSBFilterRemove(pState->Process, (uintptr_t)pReq->uId);
            *pcbReturnedData = 0;
            Log((DEVICE_NAME ": vboxUSBMonSolarisProcessIOCtl: REMOVE_FILTER (Process:%d) returned %d\n", pState->Process, rc));
            break;
        }

        case VBOXUSBMON_IOCTL_RESET_DEVICE:
        {
            CHECKRET_MIN_SIZE("RESET_DEVICE", sizeof(VBOXUSBREQ_RESET_DEVICE));

            VBOXUSBREQ_RESET_DEVICE *pReq = (VBOXUSBREQ_RESET_DEVICE *)pvData;
            rc = vboxUSBMonSolarisResetDevice(pReq->szDevicePath, pReq->fReattach);
            *pcbReturnedData = 0;
            Log((DEVICE_NAME ": vboxUSBMonSolarisProcessIOCtl: RESET_DEVICE (Process:%d) returned %d\n", pState->Process, rc));
            break;
        }

        case VBOXUSBMON_IOCTL_CLIENT_INFO:
        {
            CHECKRET_MIN_SIZE("CLIENT_INFO", sizeof(VBOXUSBREQ_CLIENT_INFO));

            VBOXUSBREQ_CLIENT_INFO *pReq = (VBOXUSBREQ_CLIENT_INFO *)pvData;
            rc = vboxUSBMonSolarisClientInfo(pState, pReq);
            *pcbReturnedData = cbData;
            Log((DEVICE_NAME ": vboxUSBMonSolarisProcessIOCtl: CLIENT_INFO (Process:%d) returned %d\n", pState->Process, rc));
            break;
        }

        case VBOXUSBMON_IOCTL_GET_VERSION:
        {
            CHECKRET_MIN_SIZE("GET_VERSION", sizeof(VBOXUSBREQ_GET_VERSION));

            PVBOXUSBREQ_GET_VERSION pGetVersionReq = (PVBOXUSBREQ_GET_VERSION)pvData;
            pGetVersionReq->u32Major = VBOXUSBMON_VERSION_MAJOR;
            pGetVersionReq->u32Minor = VBOXUSBMON_VERSION_MINOR;
            *pcbReturnedData = sizeof(VBOXUSBREQ_GET_VERSION);
            rc = VINF_SUCCESS;
            Log((DEVICE_NAME ": vboxUSBMonSolarisProcessIOCtl: GET_VERSION returned %d\n", rc));
            break;
        }

        default:
        {
            LogRel((DEVICE_NAME ": vboxUSBMonSolarisProcessIOCtl: Unknown request (Process:%d) %#x\n", pState->Process,
                    iFunction));
            *pcbReturnedData = 0;
            rc = VERR_NOT_SUPPORTED;
            break;
        }
    }
    return rc;
}


static int vboxUSBMonSolarisResetDevice(char *pszDevicePath, bool fReattach)
{
    int rc = VERR_GENERAL_FAILURE;

    LogFunc((DEVICE_NAME ": vboxUSBMonSolarisResetDevice: pszDevicePath=%s fReattach=%d\n", pszDevicePath, fReattach));

    /*
     * Try grabbing the dev_info_t.
     */
    dev_info_t *pDeviceInfo = e_ddi_hold_devi_by_path(pszDevicePath, 0);
    if (pDeviceInfo)
    {
        ddi_release_devi(pDeviceInfo);

        /*
         * Grab the root device node from the parent hub for resetting.
         */
        dev_info_t *pTmpDeviceInfo = NULL;
        for (;;)
        {
            pTmpDeviceInfo = ddi_get_parent(pDeviceInfo);
            if (!pTmpDeviceInfo)
            {
                LogRel((DEVICE_NAME ":vboxUSBMonSolarisResetDevice: Failed to get parent device info for %s\n", pszDevicePath));
                return VERR_GENERAL_FAILURE;
            }

            if (ddi_prop_exists(DDI_DEV_T_ANY, pTmpDeviceInfo, DDI_PROP_DONTPASS, "usb-port-count"))   /* parent hub */
                break;

            pDeviceInfo = pTmpDeviceInfo;
        }

        /*
         * Try re-enumerating the device.
         */
        rc = usb_reset_device(pDeviceInfo, fReattach ? USB_RESET_LVL_REATTACH : USB_RESET_LVL_DEFAULT);
        Log((DEVICE_NAME ": vboxUSBMonSolarisResetDevice: usb_reset_device for %s level=%s rc=%d\n", pszDevicePath,
             fReattach ? "ReAttach" : "Default", rc));

        switch (rc)
        {
            case USB_SUCCESS:         rc = VINF_SUCCESS;                break;
            case USB_INVALID_PERM:    rc = VERR_PERMISSION_DENIED;      break;
            case USB_INVALID_ARGS:    rc = VERR_INVALID_PARAMETER;      break;
            case USB_BUSY:            rc = VERR_RESOURCE_BUSY;          break;
            case USB_INVALID_CONTEXT: rc = VERR_INVALID_CONTEXT;        break;
            case USB_FAILURE:         rc = VERR_GENERAL_FAILURE;        break;

            default:                  rc = VERR_UNRESOLVED_ERROR;       break;
        }
    }
    else
    {
        rc = VERR_INVALID_HANDLE;
        LogRel((DEVICE_NAME ": vboxUSBMonSolarisResetDevice: Cannot obtain device info for %s\n", pszDevicePath));
    }

    return rc;
}


/**
 * Query client driver information. This also has a side-effect that it informs
 * the client driver which upcoming VM process should be allowed to open it.
 *
 * @returns  VBox status code.
 * @param    pState         Pointer to the device state.
 * @param    pClientInfo    Pointer to the client info. object.
 */
static int vboxUSBMonSolarisClientInfo(vboxusbmon_state_t *pState, PVBOXUSB_CLIENT_INFO pClientInfo)
{
    LogFunc((DEVICE_NAME ": vboxUSBMonSolarisClientInfo: pState=%p pClientInfo=%p\n", pState, pClientInfo));

    AssertPtrReturn(pState, VERR_INVALID_POINTER);
    AssertPtrReturn(pClientInfo, VERR_INVALID_POINTER);

    mutex_enter(&g_VBoxUSBMonSolarisMtx);
    vboxusbmon_client_t *pCur = g_pVBoxUSBMonSolarisClients;
    vboxusbmon_client_t *pPrev = NULL;
    while (pCur)
    {
        if (strncmp(pClientInfo->szDeviceIdent, pCur->Info.szDeviceIdent, sizeof(pCur->Info.szDeviceIdent) - 1) == 0)
        {
            pClientInfo->Instance = pCur->Info.Instance;
            RTStrPrintf(pClientInfo->szClientPath, sizeof(pClientInfo->szClientPath), "%s", pCur->Info.szClientPath);

            /*
             * Inform the client driver that this is the client process that is going to open it. We can predict the future!
             */
            int rc;
            if (pCur->Info.pfnSetConsumerCredentials)
            {
                rc = pCur->Info.pfnSetConsumerCredentials(pState->Process, pCur->Info.Instance, NULL /* pvReserved */);
                if (RT_FAILURE(rc))
                    LogRel((DEVICE_NAME ": vboxUSBMonSolarisClientInfo: pfnSetConsumerCredentials failed! rc=%d\n", rc));
            }
            else
                rc = VERR_INVALID_FUNCTION;

            mutex_exit(&g_VBoxUSBMonSolarisMtx);

            Log((DEVICE_NAME ": vboxUSBMonSolarisClientInfo: Found %s, rc=%d\n", pClientInfo->szDeviceIdent, rc));
            return rc;
        }
        pPrev = pCur;
        pCur = pCur->pNext;
    }

    mutex_exit(&g_VBoxUSBMonSolarisMtx);

    LogRel((DEVICE_NAME ": vboxUSBMonSolarisClientInfo: Failed to find client %s\n", pClientInfo->szDeviceIdent));
    return VERR_NOT_FOUND;
}


/**
 * Registers client driver.
 *
 * @returns VBox status code.
 */
int VBoxUSBMonSolarisRegisterClient(dev_info_t *pClientDip, PVBOXUSB_CLIENT_INFO pClientInfo)
{
    LogFunc((DEVICE_NAME ": VBoxUSBMonSolarisRegisterClient: pClientDip=%p pClientInfo=%p\n", pClientDip, pClientInfo));
    AssertPtrReturn(pClientInfo, VERR_INVALID_PARAMETER);

    if (RT_LIKELY(g_pDip))
    {
        vboxusbmon_client_t *pClient = RTMemAllocZ(sizeof(vboxusbmon_client_t));
        if (RT_LIKELY(pClient))
        {
            pClient->Info.Instance = pClientInfo->Instance;
            strncpy(pClient->Info.szClientPath, pClientInfo->szClientPath, sizeof(pClient->Info.szClientPath));
            strncpy(pClient->Info.szDeviceIdent, pClientInfo->szDeviceIdent, sizeof(pClient->Info.szDeviceIdent));
            pClient->Info.pfnSetConsumerCredentials = pClientInfo->pfnSetConsumerCredentials;
            pClient->pDip = pClientDip;

            mutex_enter(&g_VBoxUSBMonSolarisMtx);
            pClient->pNext = g_pVBoxUSBMonSolarisClients;
            g_pVBoxUSBMonSolarisClients = pClient;
            mutex_exit(&g_VBoxUSBMonSolarisMtx);

            Log((DEVICE_NAME ": Client registered (ClientPath=%s Ident=%s)\n", pClient->Info.szClientPath,
                 pClient->Info.szDeviceIdent));
            return VINF_SUCCESS;
        }
        return VERR_NO_MEMORY;
    }
    return VERR_INVALID_STATE;
}


/**
 * Deregisters client driver.
 *
 * @returns VBox status code.
 */
int VBoxUSBMonSolarisUnregisterClient(dev_info_t *pClientDip)
{
    LogFunc((DEVICE_NAME ": VBoxUSBMonSolarisUnregisterClient: pClientDip=%p\n", pClientDip));
    AssertReturn(pClientDip, VERR_INVALID_PARAMETER);

    if (RT_LIKELY(g_pDip))
    {
        mutex_enter(&g_VBoxUSBMonSolarisMtx);

        vboxusbmon_client_t *pCur = g_pVBoxUSBMonSolarisClients;
        vboxusbmon_client_t *pPrev = NULL;
        while (pCur)
        {
            if (pCur->pDip == pClientDip)
            {
                if (pPrev)
                    pPrev->pNext = pCur->pNext;
                else
                    g_pVBoxUSBMonSolarisClients = pCur->pNext;

                mutex_exit(&g_VBoxUSBMonSolarisMtx);

                Log((DEVICE_NAME ": Client unregistered (ClientPath=%s Ident=%s)\n", pCur->Info.szClientPath,
                     pCur->Info.szDeviceIdent));
                RTMemFree(pCur);
                return VINF_SUCCESS;
            }
            pPrev = pCur;
            pCur = pCur->pNext;
        }

        mutex_exit(&g_VBoxUSBMonSolarisMtx);

        LogRel((DEVICE_NAME ": VBoxUSBMonSolarisUnregisterClient: Failed to find registered client %p\n", pClientDip));
        return VERR_NOT_FOUND;
    }
    return VERR_INVALID_STATE;
}


/**
 * USBA driver election callback.
 *
 * @returns USB_SUCCESS if we want to capture the device, USB_FAILURE otherwise.
 * @param   pDevDesc        The parsed device descriptor (does not include subconfigs).
 * @param   pDevStrings     Device strings: Manufacturer, Product, Serial Number.
 * @param   pszDevicePath   The physical path of the device being attached.
 * @param   Bus             The Bus number on which the device is on.
 * @param   Port            The Port number on the bus.
 * @param   ppszDrv         The name of the driver we wish to capture the device with.
 * @param   pvReserved      Reserved for future use.
 */
int VBoxUSBMonSolarisElectDriver(usb_dev_descr_t *pDevDesc, usb_dev_str_t *pDevStrings, char *pszDevicePath, int Bus, int Port,
                                char **ppszDrv, void *pvReserved)
{
    LogFunc((DEVICE_NAME ": VBoxUSBMonSolarisElectDriver: pDevDesc=%p pDevStrings=%p pszDevicePath=%s Bus=%d Port=%d\n", pDevDesc,
            pDevStrings, pszDevicePath, Bus, Port));

    AssertPtrReturn(pDevDesc, USB_FAILURE);
    AssertPtrReturn(pDevStrings, USB_FAILURE);

    /*
     * Create a filter from the device being attached.
     */
    USBFILTER Filter;
    USBFilterInit(&Filter, USBFILTERTYPE_CAPTURE);
    USBFilterSetNumExact(&Filter, USBFILTERIDX_VENDOR_ID, pDevDesc->idVendor, true);
    USBFilterSetNumExact(&Filter, USBFILTERIDX_PRODUCT_ID, pDevDesc->idProduct, true);
    USBFilterSetNumExact(&Filter, USBFILTERIDX_DEVICE_REV, pDevDesc->bcdDevice, true);
    USBFilterSetNumExact(&Filter, USBFILTERIDX_DEVICE_CLASS, pDevDesc->bDeviceClass, true);
    USBFilterSetNumExact(&Filter, USBFILTERIDX_DEVICE_SUB_CLASS, pDevDesc->bDeviceSubClass, true);
    USBFilterSetNumExact(&Filter, USBFILTERIDX_DEVICE_PROTOCOL, pDevDesc->bDeviceProtocol, true);
    USBFilterSetNumExact(&Filter, USBFILTERIDX_BUS, 0x0 /* Bus */, true); /* Use 0x0 as userland initFilterFromDevice function in Main: see comment on "SetMustBePresent" below */
    USBFilterSetNumExact(&Filter, USBFILTERIDX_PORT, Port, true);
    USBFilterSetStringExact(&Filter, USBFILTERIDX_MANUFACTURER_STR, pDevStrings->usb_mfg ? pDevStrings->usb_mfg : "",
                            true /*fMustBePresent*/, true /*fPurge*/);
    USBFilterSetStringExact(&Filter, USBFILTERIDX_PRODUCT_STR, pDevStrings->usb_product ? pDevStrings->usb_product : "",
                            true /*fMustBePresent*/, true /*fPurge*/);
    USBFilterSetStringExact(&Filter, USBFILTERIDX_SERIAL_NUMBER_STR, pDevStrings->usb_serialno ? pDevStrings->usb_serialno : "",
                            true /*fMustBePresent*/, true /*fPurge*/);

    /* This doesn't work like it should (USBFilterMatch fails on matching field (6) i.e. Bus despite this. Investigate later. */
    USBFilterSetMustBePresent(&Filter, USBFILTERIDX_BUS, false /* fMustBePresent */);

    Log((DEVICE_NAME ": VBoxUSBMonSolarisElectDriver: idVendor=%#x idProduct=%#x bcdDevice=%#x bDeviceClass=%#x "
         "bDeviceSubClass=%#x bDeviceProtocol=%#x bBus=%#x bPort=%#x\n",
              USBFilterGetNum(&Filter, USBFILTERIDX_VENDOR_ID),
              USBFilterGetNum(&Filter, USBFILTERIDX_PRODUCT_ID),
              USBFilterGetNum(&Filter, USBFILTERIDX_DEVICE_REV),
              USBFilterGetNum(&Filter, USBFILTERIDX_DEVICE_CLASS),
              USBFilterGetNum(&Filter, USBFILTERIDX_DEVICE_SUB_CLASS),
              USBFilterGetNum(&Filter, USBFILTERIDX_DEVICE_PROTOCOL),
              USBFilterGetNum(&Filter, USBFILTERIDX_BUS),
              USBFilterGetNum(&Filter, USBFILTERIDX_PORT)));
    Log((DEVICE_NAME ": VBoxUSBMonSolarisElectDriver: Manufacturer=%s Product=%s Serial=%s\n",
              USBFilterGetString(&Filter, USBFILTERIDX_MANUFACTURER_STR)  ? USBFilterGetString(&Filter, USBFILTERIDX_MANUFACTURER_STR)  : "<null>",
              USBFilterGetString(&Filter, USBFILTERIDX_PRODUCT_STR)       ? USBFilterGetString(&Filter, USBFILTERIDX_PRODUCT_STR)       : "<null>",
              USBFilterGetString(&Filter, USBFILTERIDX_SERIAL_NUMBER_STR) ? USBFilterGetString(&Filter, USBFILTERIDX_SERIAL_NUMBER_STR) : "<null>"));

    /*
     * Run through user filters and try to see if it has a match.
     */
    uintptr_t uId = 0;
    RTPROCESS Owner = VBoxUSBFilterMatch(&Filter, &uId);
    USBFilterDelete(&Filter);
    if (Owner == NIL_RTPROCESS)
    {
        Log((DEVICE_NAME ": VBoxUSBMonSolarisElectDriver: No matching filters, device %#x:%#x uninteresting\n",
             pDevDesc->idVendor, pDevDesc->idProduct));
        return USB_FAILURE;
    }

    *ppszDrv = ddi_strdup(VBOXUSB_DRIVER_NAME, KM_SLEEP);
#if 0
    LogRel((DEVICE_NAME ": Capturing %s %s %#x:%#x:%s Bus=%d Port=%d\n",
            pDevStrings->usb_mfg ? pDevStrings->usb_mfg : "<Unknown Manufacturer>",
            pDevStrings->usb_product ? pDevStrings->usb_product : "<Unnamed USB device>",
            pDevDesc->idVendor, pDevDesc->idProduct, pszDevicePath, Bus, Port));
#else
    /* Until IPRT R0 logging is fixed. See @bugref{6657#c7} */
    cmn_err(CE_CONT, "Capturing %s %s 0x%x:0x%x:%s Bus=%d Port=%d\n",
            pDevStrings->usb_mfg ? pDevStrings->usb_mfg : "<Unknown Manufacturer>",
            pDevStrings->usb_product ? pDevStrings->usb_product : "<Unnamed USB device>",
            pDevDesc->idVendor, pDevDesc->idProduct, pszDevicePath, Bus, Port);
#endif
    return USB_SUCCESS;
}

