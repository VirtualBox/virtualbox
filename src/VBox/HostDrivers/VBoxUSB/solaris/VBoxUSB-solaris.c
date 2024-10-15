/* $Id: VBoxUSB-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB Client Driver, Solaris Hosts.
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
#define LOG_GROUP LOG_GROUP_USB_DRV
#include <VBox/version.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/cdefs.h>
#include <VBox/sup.h>
#include <VBox/usblib-solaris.h>

#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/semaphore.h>
#include <iprt/mem.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/path.h>
#include <iprt/thread.h>
#include <iprt/dbg.h>

#define USBDRV_MAJOR_VER    2
#define USBDRV_MINOR_VER    0
#include <sys/usb/usba.h>
#include <sys/strsun.h>
#include "usbai_private.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The module name. */
#define DEVICE_NAME                                     "vboxusb"
/** The module description as seen in 'modinfo'. */
#define DEVICE_DESC_DRV                                 "VirtualBox USB"

/** -=-=-=-=-=-=- Standard Specifics -=-=-=-=-=-=- */
/** Max. supported endpoints. */
#define VBOXUSB_MAX_ENDPOINTS                           32
/** Size of USB Ctrl Xfer Header in bytes. */
#define VBOXUSB_CTRL_XFER_SIZE                          8
/**
 * USB2.0 (Sec. 9-13) Bits 10..0 is the max packet size; for high speed Isoc/Intr, bits 12..11 is
 * number of additional transaction opportunities per microframe.
 */
#define VBOXUSB_PKT_SIZE(pkt)                          (pkt & 0x07FF) * (1 + ((pkt >> 11) & 3))
/** Endpoint Xfer Type. */
#define VBOXUSB_XFER_TYPE(endp)                        ((endp)->EpDesc.bmAttributes & USB_EP_ATTR_MASK)
/** Endpoint Xfer Direction. */
#define VBOXUSB_XFER_DIR(endp)                         ((endp)->EpDesc.bEndpointAddress & USB_EP_DIR_IN)
/** Create an Endpoint index from an Endpoint address. */
#define VBOXUSB_GET_EP_INDEX(epaddr)                   (((epaddr) & USB_EP_NUM_MASK) + \
                                                       (((epaddr) & USB_EP_DIR_MASK) ? 16 : 0))


/** -=-=-=-=-=-=- Tunable Parameters -=-=-=-=-=-=- */
/** Time to wait while draining inflight UBRs on suspend, in seconds. */
#define VBOXUSB_DRAIN_TIME                              20
/** Ctrl Xfer timeout in seconds. */
#define VBOXUSB_CTRL_XFER_TIMEOUT                       15
/** Maximum URB queue length. */
#define VBOXUSB_URB_QUEUE_SIZE                         512
/** Maximum asynchronous requests per pipe. */
#define VBOXUSB_MAX_PIPE_ASYNC_REQS                     2

/** For enabling global symbols while debugging. **/
#if defined(DEBUG_ramshankar)
# define LOCAL
#else
# define LOCAL    static
#endif


/*********************************************************************************************************************************
*   Kernel Entry Hooks                                                                                                           *
*********************************************************************************************************************************/
int VBoxUSBSolarisOpen(dev_t *pDev, int fFlag, int fType, cred_t *pCred);
int VBoxUSBSolarisClose(dev_t Dev, int fFlag, int fType, cred_t *pCred);
int VBoxUSBSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred);
int VBoxUSBSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred);
int VBoxUSBSolarisIOCtl(dev_t Dev, int Cmd, intptr_t pArg, int Mode, cred_t *pCred, int *pVal);
int VBoxUSBSolarisPoll(dev_t Dev, short fEvents, int fAnyYet, short *pReqEvents, struct pollhead **ppPollHead);
int VBoxUSBSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pArg, void **ppResult);
int VBoxUSBSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd);
int VBoxUSBSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd);
int VBoxUSBSolarisPower(dev_info_t *pDip, int Component, int Level);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * cb_ops: for drivers that support char/block entry points
 */
static struct cb_ops g_VBoxUSBSolarisCbOps =
{
    VBoxUSBSolarisOpen,
    VBoxUSBSolarisClose,
    nodev,                      /* b strategy */
    nodev,                      /* b dump */
    nodev,                      /* b print */
    VBoxUSBSolarisRead,
    VBoxUSBSolarisWrite,
    VBoxUSBSolarisIOCtl,
    nodev,                      /* c devmap */
    nodev,                      /* c mmap */
    nodev,                      /* c segmap */
    VBoxUSBSolarisPoll,
    ddi_prop_op,                /* property ops */
    NULL,                       /* streamtab  */
    D_NEW | D_MP,               /* compat. flag */
    CB_REV,                     /* revision */
    nodev,                      /* c aread */
    nodev                       /* c awrite */
};

/**
 * dev_ops: for driver device operations
 */
static struct dev_ops g_VBoxUSBSolarisDevOps =
{
    DEVO_REV,                   /* driver build revision */
    0,                          /* ref count */
    VBoxUSBSolarisGetInfo,
    nulldev,                    /* identify */
    nulldev,                    /* probe */
    VBoxUSBSolarisAttach,
    VBoxUSBSolarisDetach,
    nodev,                      /* reset */
    &g_VBoxUSBSolarisCbOps,
    NULL,                       /* bus ops */
    VBoxUSBSolarisPower,
    ddi_quiesce_not_needed
};

/**
 * modldrv: export driver specifics to the kernel
 */
static struct modldrv g_VBoxUSBSolarisModule =
{
    &mod_driverops,             /* extern from kernel */
    DEVICE_DESC_DRV " " VBOX_VERSION_STRING "r" RT_XSTR(VBOX_SVN_REV),
    &g_VBoxUSBSolarisDevOps
};

/**
 * modlinkage: export install/remove/info to the kernel
 */
static struct modlinkage g_VBoxUSBSolarisModLinkage =
{
    MODREV_1,
    &g_VBoxUSBSolarisModule,
    NULL,
};

/**
 * vboxusb_ep_t: Endpoint structure with info. for managing an endpoint.
 */
typedef struct vboxusb_ep_t
{
    bool                    fInitialized;        /* Whether this Endpoint is initialized */
    usb_ep_descr_t          EpDesc;              /* Endpoint descriptor */
    usb_pipe_handle_t       pPipe;               /* Endpoint pipe handle */
    usb_pipe_policy_t       PipePolicy;          /* Endpoint policy */
    bool                    fIsocPolling;        /* Whether Isoc. IN polling is enabled */
    list_t                  hIsocInUrbs;         /* Isoc. IN inflight URBs */
    uint16_t                cIsocInUrbs;         /* Number of Isoc. IN inflight URBs */
    list_t                  hIsocInLandedReqs;   /* Isoc. IN landed requests */
    uint16_t                cbIsocInLandedReqs;  /* Cumulative size of landed Isoc. IN requests */
    size_t                  cbMaxIsocData;       /* Maximum size of Isoc. IN landed buffer */
} vboxusb_ep_t;

/**
 * vboxusb_isoc_req_t: Isoc IN. requests queued from device till they are reaped.
 */
typedef struct vboxusb_isoc_req_t
{
    mblk_t                 *pMsg;                /* Pointer to the data buffer */
    uint32_t                cIsocPkts;           /* Number of Isoc pkts */
    VUSBISOC_PKT_DESC       aIsocPkts[8];        /* Array of Isoc pkt descriptors */
    list_node_t             hListLink;
} vboxusb_isoc_req_t;

/**
 * VBOXUSB_URB_STATE: Internal USB URB state.
 */
typedef enum VBOXUSB_URB_STATE
{
    VBOXUSB_URB_STATE_FREE     = 0x00,
    VBOXUSB_URB_STATE_INFLIGHT = 0x04,
    VBOXUSB_URB_STATE_LANDED   = 0x08
} VBOXUSB_URB_STATE;

/**
 * vboxusb_urb_t: kernel URB representation.
 */
typedef struct vboxusb_urb_t
{
    void                   *pvUrbR3;             /* Userspace URB address (untouched, returned while reaping) */
    uint8_t                 bEndpoint;           /* Endpoint address */
    VUSBXFERTYPE            enmType;             /* Xfer type */
    VUSBDIRECTION           enmDir;              /* Xfer direction */
    VUSBSTATUS              enmStatus;           /* URB status */
    bool                    fShortOk;            /* Whether receiving less data than requested is acceptable */
    RTR3PTR                 pvDataR3;            /* Userspace address of the original data buffer */
    size_t                  cbDataR3;            /* Size of the data buffer */
    mblk_t                 *pMsg;                /* Pointer to the data buffer */
    uint32_t                cIsocPkts;           /* Number of Isoc pkts */
    VUSBISOC_PKT_DESC       aIsocPkts[8];        /* Array of Isoc pkt descriptors */
    VBOXUSB_URB_STATE       enmState;            /* URB state (free/in-flight/landed). */
    struct vboxusb_state_t *pState;              /* Pointer to the device instance */
    list_node_t             hListLink;           /* List node link handle */
} vboxusb_urb_t;

/**
 * vboxusb_power_t: Per Device Power Management info.
 */
typedef struct vboxusb_power_t
{
    uint_t                  PowerStates;         /* Bit mask of the power states */
    int                     PowerBusy;           /* Busy reference counter */
    bool                    fPowerWakeup;        /* Whether remote power wakeup is enabled */
    bool                    fPowerRaise;         /* Whether to raise the power level */
    uint8_t                 PowerLevel;          /* Current power level */
} vboxusb_power_t;

/**
 * vboxusb_state_t: Per Device instance state info.
 */
typedef struct vboxusb_state_t
{
    dev_info_t             *pDip;                /* Per instance device info. */
    usb_client_dev_data_t  *pDevDesc;            /* Parsed & complete device descriptor */
    uint8_t                 DevState;            /* Current USB Device state */
    bool                    fDefaultPipeOpen;    /* Whether the device (default control pipe) is closed */
    bool                    fPollPending;        /* Whether the userland process' poll is pending */
    kmutex_t                Mtx;                 /* Mutex state protection */
    usb_serialization_t     StateMulti;          /* State serialization */
    size_t                  cbMaxBulkXfer;       /* Maximum bulk xfer size */
    vboxusb_ep_t            aEps[VBOXUSB_MAX_ENDPOINTS]; /* Array of all endpoints structures */
    list_t                  hFreeUrbs;           /* List of free URBs */
    list_t                  hInflightUrbs;       /* List of inflight URBs */
    list_t                  hLandedUrbs;         /* List of landed URBs */
    uint32_t                cFreeUrbs;           /* Number of free URBs */
    uint32_t                cInflightUrbs;       /* Number of inflight URBs */
    uint32_t                cLandedUrbs;         /* Number of landed URBs */
    pollhead_t              PollHead;            /* Handle to pollhead for waking polling processes  */
    RTPROCESS               Process;             /* The process (pid) of the user session */
    VBOXUSBREQ_CLIENT_INFO  ClientInfo;          /* Registration data */
    vboxusb_power_t        *pPower;              /* Power Management */
    char                    szMfg[255];          /* Parsed manufacturer string */
    char                    szProduct[255];      /* Parsed product string */
} vboxusb_state_t;
AssertCompileMemberSize(vboxusb_state_t, szMfg,     USB_MAXSTRINGLEN);
AssertCompileMemberSize(vboxusb_state_t, szProduct, USB_MAXSTRINGLEN);


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
LOCAL int    vboxUsbSolarisInitEp(vboxusb_state_t *pState, usb_ep_data_t *pEpData);
LOCAL int    vboxUsbSolarisInitEpsForCfg(vboxusb_state_t *pState);
LOCAL int    vboxUsbSolarisInitEpsForIfAlt(vboxusb_state_t *pState, uint8_t bIf, uint8_t bAlt);
LOCAL void   vboxUsbSolarisDestroyAllEps(vboxusb_state_t *pState);
LOCAL void   vboxUsbSolarisDestroyEp(vboxusb_state_t *pState, vboxusb_ep_t *pEp);
LOCAL void   vboxUsbSolarisCloseAllPipes(vboxusb_state_t *pState, bool fControlPipe);
LOCAL int    vboxUsbSolarisOpenPipe(vboxusb_state_t *pState, vboxusb_ep_t *pEp);
LOCAL void   vboxUsbSolarisClosePipe(vboxusb_state_t *pState, vboxusb_ep_t *pEp);
LOCAL int    vboxUsbSolarisCtrlXfer(vboxusb_state_t *pState, vboxusb_ep_t *pEp, vboxusb_urb_t *pUrb);
LOCAL void   vboxUsbSolarisCtrlXferCompleted(usb_pipe_handle_t pPipe, usb_ctrl_req_t *pReq);
LOCAL int    vboxUsbSolarisBulkXfer(vboxusb_state_t *pState, vboxusb_ep_t *pEp, vboxusb_urb_t *purb);
LOCAL void   vboxUsbSolarisBulkXferCompleted(usb_pipe_handle_t pPipe, usb_bulk_req_t *pReq);
LOCAL int    vboxUsbSolarisIntrXfer(vboxusb_state_t *pState, vboxusb_ep_t *pEp, vboxusb_urb_t *pUrb);
LOCAL void   vboxUsbSolarisIntrXferCompleted(usb_pipe_handle_t pPipe, usb_intr_req_t *pReq);
LOCAL int    vboxUsbSolarisIsocXfer(vboxusb_state_t *pState, vboxusb_ep_t *pEp, vboxusb_urb_t *pUrb);
LOCAL void   vboxUsbSolarisIsocInXferCompleted(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq);
LOCAL void   vboxUsbSolarisIsocInXferError(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq);
LOCAL void   vboxUsbSolarisIsocOutXferCompleted(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq);
LOCAL vboxusb_urb_t  *vboxUsbSolarisGetIsocInUrb(vboxusb_state_t *pState, PVBOXUSBREQ_URB pUrbReq);
LOCAL vboxusb_urb_t  *vboxUsbSolarisQueueUrb(vboxusb_state_t *pState, PVBOXUSBREQ_URB pUrbReq, mblk_t *pMsg);
LOCAL VUSBSTATUS      vboxUsbSolarisGetUrbStatus(usb_cr_t Status);
LOCAL void   vboxUsbSolarisConcatMsg(vboxusb_urb_t *pUrb);
LOCAL void   vboxUsbSolarisDeQueueUrb(vboxusb_urb_t *pUrb, int URBStatus);
LOCAL void   vboxUsbSolarisNotifyComplete(vboxusb_state_t *pState);
LOCAL int    vboxUsbSolarisProcessIOCtl(int iFunction, void *pvState, int Mode, PVBOXUSBREQ pUSBReq, void *pvBuf,
                                        size_t *pcbDataOut);
LOCAL bool   vboxUsbSolarisIsUSBDevice(dev_info_t *pDip);

/** @name Device Operation Hooks
 * @{ */
LOCAL int    vboxUsbSolarisSendUrb(vboxusb_state_t *pState, PVBOXUSBREQ_URB pUrbReq, int Mode);
LOCAL int    vboxUsbSolarisReapUrb(vboxusb_state_t *pState, PVBOXUSBREQ_URB pUrbReq, int Mode);
LOCAL int    vboxUsbSolarisClearEndPoint(vboxusb_state_t *pState, uint8_t bEndpoint);
LOCAL int    vboxUsbSolarisSetConfig(vboxusb_state_t *pState, uint8_t bCfgValue);
LOCAL int    vboxUsbSolarisGetConfig(vboxusb_state_t *pState, uint8_t *pCfgValue);
LOCAL int    vboxUsbSolarisSetInterface(vboxusb_state_t *pState, uint8_t bIf, uint8_t bAlt);
LOCAL int    vboxUsbSolarisCloseDevice(vboxusb_state_t *pState, VBOXUSB_RESET_LEVEL enmReset);
LOCAL int    vboxUsbSolarisAbortPipe(vboxusb_state_t *pState, uint8_t bEndpoint);
LOCAL int    vboxUsbSolarisGetConfigIndex(vboxusb_state_t *pState, uint_t bCfgValue);
/** @} */

/** @name Hotplug & Power Management Hooks
 * @{ */
LOCAL void   vboxUsbSolarisNotifyUnplug(vboxusb_state_t *pState);
LOCAL int    vboxUsbSolarisDeviceDisconnected(dev_info_t *pDip);
LOCAL int    vboxUsbSolarisDeviceReconnected(dev_info_t *pDip);

LOCAL int    vboxUsbSolarisInitPower(vboxusb_state_t *pState);
LOCAL void   vboxUsbSolarisDestroyPower(vboxusb_state_t *pState);
LOCAL int    vboxUsbSolarisDeviceSuspend(vboxusb_state_t *pState);
LOCAL void   vboxUsbSolarisDeviceResume(vboxusb_state_t *pState);
LOCAL void   vboxUsbSolarisDeviceRestore(vboxusb_state_t *pState);
LOCAL void   vboxUsbSolarisPowerBusy(vboxusb_state_t *pState);
LOCAL void   vboxUsbSolarisPowerIdle(vboxusb_state_t *pState);
/** @} */

/** @name Monitor Hooks
 * @{ */
int          VBoxUSBMonSolarisRegisterClient(dev_info_t *pClientDip, PVBOXUSB_CLIENT_INFO pClientInfo);
int          VBoxUSBMonSolarisUnregisterClient(dev_info_t *pClientDip);
/** @} */

/** @name Callbacks from Monitor
 * @{ */
LOCAL int    vboxUsbSolarisSetConsumerCredentials(RTPROCESS Process, int Instance, void *pvReserved);
/** @} */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Global list of all device instances. */
static void *g_pVBoxUSBSolarisState;

/** The default endpoint descriptor */
static usb_ep_descr_t g_VBoxUSBSolarisDefaultEpDesc = { 7, 5, 0, USB_EP_ATTR_CONTROL, 8, 0 };

/** Size of the usb_ep_data_t struct (used to index into data). */
static size_t g_cbUsbEpData       = ~0UL;

/** The offset of usb_ep_data_t::ep_desc. */
static size_t g_offUsbEpDataDescr = ~0UL;


#ifdef LOG_ENABLED
/**
 * Gets the description of an Endpoint's transfer type.
 *
 * @param pEp       The Endpoint.
 * @returns The type of the Endpoint.
 */
static const char *vboxUsbSolarisEpType(vboxusb_ep_t *pEp)
{
    uint8_t uType = VBOXUSB_XFER_TYPE(pEp);
    switch (uType)
    {
        case 0:  return "CTRL";
        case 1:  return "ISOC";
        case 2:  return "BULK";
        default: return "INTR";
    }
}


/**
 * Gets the description of an Endpoint's direction.
 *
 * @param pEp       The Endpoint.
 * @returns The direction of the Endpoint.
 */
static const char *vboxUsbSolarisEpDir(vboxusb_ep_t *pEp)
{
    return VBOXUSB_XFER_DIR(pEp) == USB_EP_DIR_IN ? "IN " : "OUT";
}
#endif


/**
 * Caches device strings from the parsed device descriptors.
 *
 * @param   pState          The USB device instance.
 *
 * @remarks Must only be called after usb_get_dev_data().
 */
static void vboxUsbSolarisGetDeviceStrings(vboxusb_state_t *pState)
{
    AssertReturnVoid(pState);
    AssertReturnVoid(pState->pDevDesc);

    if (pState->pDevDesc->dev_product)
        strlcpy(&pState->szMfg[0], pState->pDevDesc->dev_mfg, sizeof(pState->szMfg));
    else
        strlcpy(&pState->szMfg[0], "<Unknown Manufacturer>", sizeof(pState->szMfg));

    if (pState->pDevDesc->dev_product)
        strlcpy(&pState->szProduct[0], pState->pDevDesc->dev_product, sizeof(pState->szProduct));
    else
        strlcpy(&pState->szProduct[0], "<Unnamed USB device>", sizeof(pState->szProduct));
}


/**
 * Queries the necessary symbols at runtime.
 *
 * @returns VBox status code.
 */
LOCAL int vboxUsbSolarisQuerySymbols(void)
{
    RTDBGKRNLINFO hKrnlDbgInfo;
    int rc = RTR0DbgKrnlInfoOpen(&hKrnlDbgInfo, 0 /* fFlags */);
    if (RT_SUCCESS(rc))
    {
        /*
         * Query and sanitize the size of usb_ep_data_t struct.
         */
        size_t cbPrevUsbEpData = g_cbUsbEpData;
        rc = RTR0DbgKrnlInfoQuerySize(hKrnlDbgInfo, "usba", "usb_ep_data_t", &g_cbUsbEpData);
        if (RT_FAILURE(rc))
        {
            LogRel(("Failed to query size of \"usb_ep_data_t\" in the \"usba\" module, rc=%Rrc\n", rc));
            return rc;
        }
        if (g_cbUsbEpData > _4K)
        {
            LogRel(("Size of \"usb_ep_data_t\" (%u bytes) seems implausible, too paranoid to continue\n", g_cbUsbEpData));
            return VERR_MISMATCH;
        }

        /*
         * Query and sanitizie the offset of usb_ep_data_t::ep_descr.
         */
        size_t offPrevUsbEpDataDescr = g_offUsbEpDataDescr;
        rc = RTR0DbgKrnlInfoQueryMember(hKrnlDbgInfo, "usba", "usb_ep_data_t", "ep_descr", &g_offUsbEpDataDescr);
        if (RT_FAILURE(rc))
        {
            LogRel(("Failed to query offset of usb_ep_data_t::ep_descr, rc=%Rrc\n", rc));
            return rc;
        }
        if (g_offUsbEpDataDescr > _4K - sizeof(usb_ep_descr_t))
        {
            LogRel(("Offset of \"ep_desrc\" (%u) seems implausible, too paranoid to continue\n", g_offUsbEpDataDescr));
            return VERR_MISMATCH;
        }

        /*
         * Log only when it changes / first time, since _init() seems to be called often (e.g. on failed attaches).
         * cmn_err, CE_CONT and '!' is used to not show the message on console during boot each time.
         */
        if (   cbPrevUsbEpData       != g_cbUsbEpData
            || offPrevUsbEpDataDescr != g_offUsbEpDataDescr)
        {
            cmn_err(CE_CONT, "!usba_ep_data_t is %lu bytes\n", g_cbUsbEpData);
            cmn_err(CE_CONT, "!usba_ep_data_t::ep_descr @ 0x%lx (%ld)\n", g_offUsbEpDataDescr, g_offUsbEpDataDescr);
        }

        RTR0DbgKrnlInfoRelease(hKrnlDbgInfo);
    }

    return rc;
}


/**
 * Kernel entry points
 */
int _init(void)
{
    LogFunc((DEVICE_NAME ": _init\n"));

    /*
     * Prevent module autounloading.
     */
    modctl_t *pModCtl = mod_getctl(&g_VBoxUSBSolarisModLinkage);
    if (pModCtl)
        pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD;
    else
        LogRel((DEVICE_NAME ": _init: failed to disable autounloading!\n"));

    /*
     * Initialize IPRT R0 driver, which internally calls OS-specific r0 init.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        rc = vboxUsbSolarisQuerySymbols();
        if (RT_FAILURE(rc))
        {
            RTR0Term();
            return EINVAL;
        }

        rc = ddi_soft_state_init(&g_pVBoxUSBSolarisState, sizeof(vboxusb_state_t), 4 /* pre-alloc */);
        if (!rc)
        {
            rc = mod_install(&g_VBoxUSBSolarisModLinkage);
            if (!rc)
                return rc;

            LogRel((DEVICE_NAME ": _init: mod_install failed! rc=%d\n", rc));
            ddi_soft_state_fini(&g_pVBoxUSBSolarisState);
        }
        else
            LogRel((DEVICE_NAME ": _init: failed to initialize soft state\n"));

        RTR0Term();
    }
    else
        LogRel((DEVICE_NAME ": _init: RTR0Init failed! rc=%d\n", rc));
    return RTErrConvertToErrno(rc);
}


int _fini(void)
{
    int rc;

    LogFunc((DEVICE_NAME ": _fini\n"));

    rc = mod_remove(&g_VBoxUSBSolarisModLinkage);
    if (!rc)
    {
        ddi_soft_state_fini(&g_pVBoxUSBSolarisState);
        RTR0Term();
    }

    return rc;
}


int _info(struct modinfo *pModInfo)
{
    LogFunc((DEVICE_NAME ": _info\n"));

    return mod_info(&g_VBoxUSBSolarisModLinkage, pModInfo);
}


/**
 * Attach entry point, to attach a device to the system or resume it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Attach type (ddi_attach_cmd_t)
 *
 * @returns Solaris error code.
 */
int VBoxUSBSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ": VBoxUSBSolarisAttach: pDip=%p enmCmd=%d\n", pDip, enmCmd));

    int rc;
    int instance = ddi_get_instance(pDip);
    vboxusb_state_t *pState = NULL;

    switch (enmCmd)
    {
        case DDI_ATTACH:
        {
            rc = ddi_soft_state_zalloc(g_pVBoxUSBSolarisState, instance);
            if (rc == DDI_SUCCESS)
            {
                pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, instance);
                if (RT_LIKELY(pState))
                {
                    pState->pDip             = pDip;
                    pState->pDevDesc         = NULL;
                    pState->fPollPending     = false;
                    pState->cInflightUrbs    = 0;
                    pState->cFreeUrbs        = 0;
                    pState->cLandedUrbs      = 0;
                    pState->Process          = NIL_RTPROCESS;
                    pState->pPower           = NULL;
                    bzero(pState->aEps, sizeof(pState->aEps));
                    list_create(&pState->hFreeUrbs, sizeof(vboxusb_urb_t), offsetof(vboxusb_urb_t, hListLink));
                    list_create(&pState->hInflightUrbs, sizeof(vboxusb_urb_t), offsetof(vboxusb_urb_t, hListLink));
                    list_create(&pState->hLandedUrbs, sizeof(vboxusb_urb_t), offsetof(vboxusb_urb_t, hListLink));

                    /*
                     * There is a bug in usb_client_attach() as of Nevada 120 which panics when we bind to
                     * a non-USB device. So check if we are really binding to a USB device or not.
                     */
                    if (vboxUsbSolarisIsUSBDevice(pState->pDip))
                    {
                        /*
                         * Here starts the USB specifics.
                         */
                        rc = usb_client_attach(pState->pDip, USBDRV_VERSION, 0);
                        if (rc == USB_SUCCESS)
                        {
                            pState->fDefaultPipeOpen = true;

                            /*
                             * Parse out the entire descriptor.
                             */
                            rc = usb_get_dev_data(pState->pDip, &pState->pDevDesc, USB_PARSE_LVL_ALL, 0 /* Unused */);
                            if (rc == USB_SUCCESS)
                            {
                                /*
                                 * Cache some device descriptor strings.
                                 */
                                vboxUsbSolarisGetDeviceStrings(pState);
#ifdef DEBUG_ramshankar
                                usb_print_descr_tree(pState->pDip, pState->pDevDesc);
#endif

                                /*
                                 * Initialize state locks.
                                 */
                                mutex_init(&pState->Mtx, NULL, MUTEX_DRIVER, pState->pDevDesc->dev_iblock_cookie);
                                pState->StateMulti = usb_init_serialization(pState->pDip, USB_INIT_SER_CHECK_SAME_THREAD);

                                /*
                                 * Get maximum bulk transfer size supported by the HCD.
                                 */
                                rc = usb_pipe_get_max_bulk_transfer_size(pState->pDip, &pState->cbMaxBulkXfer);
                                if (rc == USB_SUCCESS)
                                {
                                    Log((DEVICE_NAME ": VBoxUSBSolarisAttach: cbMaxBulkXfer=%d\n", pState->cbMaxBulkXfer));

                                    /*
                                     * Initialize the default endpoint.
                                     */
                                    rc = vboxUsbSolarisInitEp(pState, NULL /* pEp */);
                                    if (RT_SUCCESS(rc))
                                    {
                                        /*
                                         * Set the device state.
                                         */
                                        pState->DevState = USB_DEV_ONLINE;

                                        /*
                                         * Initialize power management for the device.
                                         */
                                        rc = vboxUsbSolarisInitPower(pState);
                                        if (RT_SUCCESS(rc))
                                        {
                                            /*
                                             * Initialize endpoints for the current config.
                                             */
                                            rc = vboxUsbSolarisInitEpsForCfg(pState);
                                            AssertRC(rc);

                                            /*
                                             * Publish the minor node.
                                             */
                                            rc = ddi_create_priv_minor_node(pDip, DEVICE_NAME, S_IFCHR, instance, DDI_PSEUDO, 0,
                                                        "none", "none", 0666);
                                            if (RT_LIKELY(rc == DDI_SUCCESS))
                                            {
                                                /*
                                                 * Register hotplug callbacks.
                                                 */
                                                rc = usb_register_hotplug_cbs(pState->pDip, &vboxUsbSolarisDeviceDisconnected,
                                                                              &vboxUsbSolarisDeviceReconnected);
                                                if (RT_LIKELY(rc == USB_SUCCESS))
                                                {
                                                    /*
                                                     * Register with our monitor driver.
                                                     */
                                                    bzero(&pState->ClientInfo, sizeof(pState->ClientInfo));
                                                    char szDevicePath[MAXPATHLEN];
                                                    ddi_pathname(pState->pDip, szDevicePath);
                                                    RTStrPrintf(pState->ClientInfo.szClientPath,
                                                                sizeof(pState->ClientInfo.szClientPath),
                                                                "/devices%s:%s", szDevicePath, DEVICE_NAME);
                                                    RTStrPrintf(pState->ClientInfo.szDeviceIdent,
                                                                sizeof(pState->ClientInfo.szDeviceIdent),
                                                                "%#x:%#x:%d:%s",
                                                                pState->pDevDesc->dev_descr->idVendor,
                                                                pState->pDevDesc->dev_descr->idProduct,
                                                                pState->pDevDesc->dev_descr->bcdDevice, szDevicePath);
                                                    pState->ClientInfo.Instance = instance;
                                                    pState->ClientInfo.pfnSetConsumerCredentials = &vboxUsbSolarisSetConsumerCredentials;
                                                    rc = VBoxUSBMonSolarisRegisterClient(pState->pDip, &pState->ClientInfo);
                                                    if (RT_SUCCESS(rc))
                                                    {
#if 0
                                                        LogRel((DEVICE_NAME ": Captured %s %s (Ident=%s)\n", pState->szMfg,
                                                                pState->szProduct, pState->ClientInfo.szDeviceIdent));
#else
                                                        /* Until IPRT R0 logging is fixed. See @bugref{6657#c7} */
                                                        cmn_err(CE_CONT, "Captured %s %s (Ident=%s)\n", pState->szMfg,
                                                                pState->szProduct, pState->ClientInfo.szDeviceIdent);
#endif
                                                        return DDI_SUCCESS;
                                                    }

                                                    LogRel((DEVICE_NAME ": VBoxUSBMonSolarisRegisterClient failed! rc=%d "
                                                            "path=%s instance=%d\n", rc, pState->ClientInfo.szClientPath,
                                                            instance));

                                                    usb_unregister_hotplug_cbs(pState->pDip);
                                                }
                                                else
                                                    LogRel((DEVICE_NAME ": VBoxUSBSolarisAttach: Failed to register hotplug callbacks! rc=%d\n", rc));

                                                ddi_remove_minor_node(pState->pDip, NULL);
                                            }
                                            else
                                                LogRel((DEVICE_NAME ": VBoxUSBSolarisAttach: ddi_create_minor_node failed! rc=%d\n", rc));

                                            mutex_enter(&pState->Mtx);
                                            vboxUsbSolarisDestroyPower(pState);
                                            mutex_exit(&pState->Mtx);
                                        }
                                        else
                                            LogRel((DEVICE_NAME ": VBoxUSBSolarisAttach: Failed to init power management! rc=%d\n", rc));
                                    }
                                    else
                                        LogRel((DEVICE_NAME ": VBoxUSBSolarisAttach: vboxUsbSolarisInitEp failed! rc=%d\n", rc));
                                }
                                else
                                    LogRel((DEVICE_NAME ": VBoxUSBSolarisAttach: usb_pipe_get_max_bulk_transfer_size failed! rc=%d\n", rc));

                                usb_fini_serialization(pState->StateMulti);
                                mutex_destroy(&pState->Mtx);
                                usb_free_dev_data(pState->pDip, pState->pDevDesc);
                            }
                            else
                                LogRel((DEVICE_NAME ": VBoxUSBSolarisAttach: Failed to get device descriptor. rc=%d\n", rc));

                            usb_client_detach(pState->pDip, NULL);
                        }
                        else
                            LogRel((DEVICE_NAME ": VBoxUSBSolarisAttach: usb_client_attach failed! rc=%d\n", rc));
                    }
                    else
                    {
                        /* This would appear on every boot if it were LogRel() */
                        Log((DEVICE_NAME ": VBoxUSBSolarisAttach: Not a USB device\n"));
                    }
                }
                else
                    LogRel((DEVICE_NAME ": VBoxUSBSolarisAttach: Failed to get soft state\n", sizeof(*pState)));

                ddi_soft_state_free(g_pVBoxUSBSolarisState, instance);
            }
            else
                LogRel((DEVICE_NAME ": VBoxUSBSolarisAttach: Failed to alloc soft state. rc=%d\n", rc));

            return DDI_FAILURE;
        }

        case DDI_RESUME:
        {
            pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, instance);
            if (RT_UNLIKELY(!pState))
            {
                LogRel((DEVICE_NAME ": VBoxUSBSolarisAttach: DDI_RESUME failed to get soft state on detach\n"));
                return DDI_FAILURE;
            }

            vboxUsbSolarisDeviceResume(pState);
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
 * @returns Solaris error code.
 */
int VBoxUSBSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ": VBoxUSBSolarisDetach: pDip=%p enmCmd=%d\n", pDip, enmCmd));

    int instance = ddi_get_instance(pDip);
    vboxusb_state_t *pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, instance);
    if (RT_UNLIKELY(!pState))
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisDetach: Failed to get soft state on detach\n"));
        return DDI_FAILURE;
    }

    switch (enmCmd)
    {
        case DDI_DETACH:
        {
            /*
             * At this point it must be assumed that the default control pipe has
             * already been closed by userland (via VBoxUSBSolarisClose() entry point).
             * Once it's closed we can no longer open or reference the device here.
             */

            /*
             * Notify userland if any that we're gone (while resetting device held by us).
             */
            mutex_enter(&pState->Mtx);
            pState->DevState = USB_DEV_DISCONNECTED;
            vboxUsbSolarisNotifyUnplug(pState);
            mutex_exit(&pState->Mtx);


            /*
             * Unregister hotplug callback events first without holding the mutex as the callbacks
             * would otherwise block on the mutex.
             */
            usb_unregister_hotplug_cbs(pDip);

            /*
             * Serialize: paranoid; drain other driver activity.
             */
            usb_serialize_access(pState->StateMulti, USB_WAIT, 0 /* timeout */);
            usb_release_access(pState->StateMulti);
            mutex_enter(&pState->Mtx);

            /*
             * Close all pipes.
             */
            vboxUsbSolarisCloseAllPipes(pState, true /* ControlPipe */);
            Assert(!pState->fDefaultPipeOpen);

            /*
             * Deinitialize power, destroy all endpoints.
             */
            vboxUsbSolarisDestroyPower(pState);
            vboxUsbSolarisDestroyAllEps(pState);

            /*
             * Free up all URB lists.
             */
            vboxusb_urb_t *pUrb = NULL;
            while ((pUrb = list_remove_head(&pState->hFreeUrbs)) != NULL)
            {
                if (pUrb->pMsg)
                    freemsg(pUrb->pMsg);
                RTMemFree(pUrb);
            }
            while ((pUrb = list_remove_head(&pState->hInflightUrbs)) != NULL)
            {
                if (pUrb->pMsg)
                    freemsg(pUrb->pMsg);
                RTMemFree(pUrb);
            }
            while ((pUrb = list_remove_head(&pState->hLandedUrbs)) != NULL)
            {
                if (pUrb->pMsg)
                    freemsg(pUrb->pMsg);
                RTMemFree(pUrb);
            }
            pState->cFreeUrbs     = 0;
            pState->cLandedUrbs   = 0;
            pState->cInflightUrbs = 0;
            list_destroy(&pState->hFreeUrbs);
            list_destroy(&pState->hInflightUrbs);
            list_destroy(&pState->hLandedUrbs);

            /*
             * Destroy locks, free up descriptor and detach from USBA.
             */
            mutex_exit(&pState->Mtx);
            usb_fini_serialization(pState->StateMulti);
            mutex_destroy(&pState->Mtx);

            usb_free_dev_data(pState->pDip, pState->pDevDesc);
            usb_client_detach(pState->pDip, NULL);

            /*
             * Deregister with our Monitor driver.
             */
            VBoxUSBMonSolarisUnregisterClient(pState->pDip);

            ddi_remove_minor_node(pState->pDip, NULL);

#if 0
            LogRel((DEVICE_NAME ": Released %s %s (Ident=%s)\n", pState->szMfg, pState->szProduct,
                    pState->ClientInfo.szDeviceIdent));
#else
            /* Until IPRT R0 logging is fixed. See @bugref{6657#c7} */
            cmn_err(CE_CONT, "Released %s %s (Ident=%s)\n", pState->szMfg, pState->szProduct, pState->ClientInfo.szDeviceIdent);
#endif

            ddi_soft_state_free(g_pVBoxUSBSolarisState, instance);
            pState = NULL;
            return DDI_SUCCESS;
        }

        case DDI_SUSPEND:
        {
            int rc = vboxUsbSolarisDeviceSuspend(pState);
            if (RT_SUCCESS(rc))
                return DDI_SUCCESS;

            return DDI_FAILURE;
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
 * @returns Solaris error code.
 */
int VBoxUSBSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pvArg, void **ppvResult)
{
    LogFunc((DEVICE_NAME ": VBoxUSBSolarisGetInfo\n"));

    vboxusb_state_t *pState = NULL;
    int instance = getminor((dev_t)pvArg);

    switch (enmCmd)
    {
        case DDI_INFO_DEVT2DEVINFO:
        {
            /*
             * One is to one mapping of instance & minor number as we publish only one minor node per device.
             */
            pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, instance);
            if (pState)
            {
                *ppvResult = (void *)pState->pDip;
                return DDI_SUCCESS;
            }
            else
                LogRel((DEVICE_NAME ": VBoxUSBSolarisGetInfo: Failed to get device state\n"));
            return DDI_FAILURE;
        }

        case DDI_INFO_DEVT2INSTANCE:
        {
            *ppvResult = (void *)(uintptr_t)instance;
            return DDI_SUCCESS;
        }

        default:
            return DDI_FAILURE;
    }
}


/**
 * Callback invoked from the VirtualBox USB Monitor driver when a VM process
 * tries to access this USB client instance.
 *
 * This determines which VM process will be allowed to open and access this USB
 * device.
 *
 * @returns  VBox status code.
 *
 * @param    Process        The VM process performing the client info. query.
 * @param    Instance       This client instance (the one set while we register
 *                          ourselves to the Monitor driver)
 * @param    pvReserved     Reserved for future, unused.
 */
LOCAL int vboxUsbSolarisSetConsumerCredentials(RTPROCESS Process, int Instance, void *pvReserved)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisSetConsumerCredentials: Process=%u Instance=%d\n", Process, Instance));
    vboxusb_state_t *pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, Instance);
    if (!pState)
    {
        LogRel((DEVICE_NAME ": vboxUsbSolarisSetConsumerCredentials: Failed to get device state for instance %d\n", Instance));
        return VERR_INVALID_STATE;
    }

    int rc = VINF_SUCCESS;
    mutex_enter(&pState->Mtx);

    if (pState->Process == NIL_RTPROCESS)
        pState->Process = Process;
    else
    {
        LogRel((DEVICE_NAME ": vboxUsbSolarisSetConsumerCredentials: Failed! Process %u already has client open\n",
                pState->Process));
        rc = VERR_RESOURCE_BUSY;
    }

    mutex_exit(&pState->Mtx);

    return rc;
}


int VBoxUSBSolarisOpen(dev_t *pDev, int fFlag, int fType, cred_t *pCred)
{
    LogFunc((DEVICE_NAME ": VBoxUSBSolarisOpen: pDev=%p fFlag=%d fType=%d pCred=%p\n", pDev, fFlag, fType, pCred));

    /*
     * Verify we are being opened as a character device
     */
    if (fType != OTYP_CHR)
        return EINVAL;

    /*
     * One is to one mapping. (Minor<=>Instance).
     */
    int instance = getminor((dev_t)*pDev);
    vboxusb_state_t *pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, instance);
    if (!pState)
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisOpen: Failed to get device state for instance %d\n", instance));
        return ENXIO;
    }

    mutex_enter(&pState->Mtx);

    /*
     * Only one user process can open a device instance at a time.
     */
    if (pState->Process != RTProcSelf())
    {
        if (pState->Process == NIL_RTPROCESS)
            LogRel((DEVICE_NAME ": VBoxUSBSolarisOpen: No prior information about authorized process\n"));
        else
            LogRel((DEVICE_NAME ": VBoxUSBSolarisOpen: Process %u is already using this device instance\n", pState->Process));

        mutex_exit(&pState->Mtx);
        return EPERM;
    }

    mutex_exit(&pState->Mtx);

    NOREF(fFlag);
    NOREF(pCred);

    return 0;
}


int VBoxUSBSolarisClose(dev_t Dev, int fFlag, int fType, cred_t *pCred)
{
    LogFunc((DEVICE_NAME ": VBoxUSBSolarisClose: Dev=%d fFlag=%d fType=%d pCred=%p\n", Dev, fFlag, fType, pCred));

    int instance = getminor((dev_t)Dev);
    vboxusb_state_t *pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, instance);
    if (RT_UNLIKELY(!pState))
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisClose: Failed to get device state for instance %d\n", instance));
        return ENXIO;
    }

    mutex_enter(&pState->Mtx);
    pState->fPollPending  = false;
    pState->Process       = NIL_RTPROCESS;
    mutex_exit(&pState->Mtx);

    return 0;
}


int VBoxUSBSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    LogFunc((DEVICE_NAME ": VBoxUSBSolarisRead\n"));
    return ENOTSUP;
}


int VBoxUSBSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    LogFunc((DEVICE_NAME ": VBoxUSBSolarisWrite\n"));
    return ENOTSUP;
}


int VBoxUSBSolarisPoll(dev_t Dev, short fEvents, int fAnyYet, short *pReqEvents, struct pollhead **ppPollHead)
{
    LogFunc((DEVICE_NAME ": VBoxUSBSolarisPoll: Dev=%d fEvents=%d fAnyYet=%d pReqEvents=%p\n", Dev, fEvents, fAnyYet, pReqEvents));

    /*
     * Get the device state (one to one mapping).
     */
    int instance = getminor((dev_t)Dev);
    vboxusb_state_t *pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, instance);
    if (RT_UNLIKELY(!pState))
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisPoll: No state data for %d\n", instance));
        return ENXIO;
    }

    mutex_enter(&pState->Mtx);

    /*
     * Disconnect event (POLLHUP) is invalid in "fEvents".
     */
    if (pState->DevState == USB_DEV_DISCONNECTED)
        *pReqEvents |= POLLHUP;
    else if (pState->cLandedUrbs)
        *pReqEvents |= POLLIN;
    else
    {
        *pReqEvents = 0;
        if (!fAnyYet)
        {
            *ppPollHead = &pState->PollHead;
            pState->fPollPending = true;
        }
    }

    mutex_exit(&pState->Mtx);

    return 0;
}


int VBoxUSBSolarisPower(dev_info_t *pDip, int Component, int Level)
{
    LogFunc((DEVICE_NAME ": VBoxUSBSolarisPower: pDip=%p Component=%d Level=%d\n", pDip, Component, Level));

    int instance = ddi_get_instance(pDip);
    vboxusb_state_t *pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, instance);
    if (RT_UNLIKELY(!pState))
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisPower: Failed! State Gone\n"));
        return DDI_FAILURE;
    }

    if (!pState->pPower)
        return DDI_SUCCESS;

    usb_serialize_access(pState->StateMulti, USB_WAIT, 0);
    mutex_enter(&pState->Mtx);

    int rc = USB_FAILURE;
    if (pState->DevState == USB_DEV_ONLINE)
    {
        /*
         * Check if we are transitioning to a valid power state.
         */
        if (!USB_DEV_PWRSTATE_OK(pState->pPower->PowerStates, Level))
        {
            switch (Level)
            {
                case USB_DEV_OS_PWR_OFF:
                {
                    if (pState->pPower->PowerBusy)
                        break;

                    /*
                     * USB D3 command.
                     */
                    pState->pPower->PowerLevel = USB_DEV_OS_PWR_OFF;
                    mutex_exit(&pState->Mtx);
                    rc = USB_SUCCESS; /* usb_set_device_pwrlvl3(pDip); */
                    mutex_enter(&pState->Mtx);
                    break;
                }

                case USB_DEV_OS_FULL_PWR:
                {
                    /*
                     * Can happen during shutdown of the OS.
                     */
                    pState->pPower->PowerLevel = USB_DEV_OS_FULL_PWR;
                    mutex_exit(&pState->Mtx);
                    rc = USB_SUCCESS; /* usb_set_device_pwrlvl0(pDip); */
                    mutex_enter(&pState->Mtx);
                    break;
                }

                default:    /* Power levels 1, 2 not implemented */
                    break;
            }
        }
        else
            Log((DEVICE_NAME ": VBoxUSBSolarisPower: USB_DEV_PWRSTATE_OK failed\n"));
    }
    else
        rc = USB_SUCCESS;

    mutex_exit(&pState->Mtx);
    usb_release_access(pState->StateMulti);
    return rc == USB_SUCCESS ? DDI_SUCCESS : DDI_FAILURE;
}


/** @def IOCPARM_LEN
 * Gets the length from the ioctl number.
 * This is normally defined by sys/ioccom.h on BSD systems...
 */
#ifndef IOCPARM_LEN
# define IOCPARM_LEN(Code)                      (((Code) >> 16) & IOCPARM_MASK)
#endif

int VBoxUSBSolarisIOCtl(dev_t Dev, int Cmd, intptr_t pArg, int Mode, cred_t *pCred, int *pVal)
{
    /* LogFunc((DEVICE_NAME ": VBoxUSBSolarisIOCtl: Dev=%d Cmd=%d pArg=%p Mode=%d\n", Dev, Cmd, pArg)); */

    /*
     * Get the device state (one to one mapping).
     */
    int instance = getminor((dev_t)Dev);
    vboxusb_state_t *pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, instance);
    if (RT_UNLIKELY(!pState))
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisIOCtl: No state data for %d\n", instance));
        return EINVAL;
    }

    /*
     * Read the request wrapper.
     */
    VBOXUSBREQ ReqWrap;
    if (IOCPARM_LEN(Cmd) != sizeof(ReqWrap))
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisIOCtl: Bad request %#x size=%d expected=%d\n", Cmd, IOCPARM_LEN(Cmd),
                sizeof(ReqWrap)));
        return ENOTTY;
    }

    int rc = ddi_copyin((void *)pArg, &ReqWrap, sizeof(ReqWrap), Mode);
    if (RT_UNLIKELY(rc))
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisIOCtl: ddi_copyin failed to read header pArg=%p Cmd=%d. rc=%d\n", pArg, Cmd, rc));
        return EINVAL;
    }

    if (ReqWrap.u32Magic != VBOXUSB_MAGIC)
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisIOCtl: Bad magic %#x; pArg=%p Cmd=%d\n", ReqWrap.u32Magic, pArg, Cmd));
        return EINVAL;
    }
    if (RT_UNLIKELY(   ReqWrap.cbData == 0
                    || ReqWrap.cbData > _1M*16))
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisIOCtl: Bad size %#x; pArg=%p Cmd=%d\n", ReqWrap.cbData, pArg, Cmd));
        return EINVAL;
    }

    /*
     * Read the request.
     */
    void *pvBuf = RTMemTmpAlloc(ReqWrap.cbData);
    if (RT_UNLIKELY(!pvBuf))
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisIOCtl: RTMemTmpAlloc failed to alloc %d bytes\n", ReqWrap.cbData));
        return ENOMEM;
    }

    rc = ddi_copyin((void *)(uintptr_t)ReqWrap.pvDataR3, pvBuf, ReqWrap.cbData, Mode);
    if (RT_UNLIKELY(rc))
    {
        RTMemTmpFree(pvBuf);
        LogRel((DEVICE_NAME ": VBoxUSBSolarisIOCtl: ddi_copyin failed! pvBuf=%p pArg=%p Cmd=%d. rc=%d\n", pvBuf, pArg, Cmd, rc));
        return EFAULT;
    }
    if (RT_UNLIKELY(   ReqWrap.cbData == 0
                    || pvBuf == NULL))
    {
        RTMemTmpFree(pvBuf);
        LogRel((DEVICE_NAME ": VBoxUSBSolarisIOCtl: Invalid request! pvBuf=%p cbData=%d\n", pvBuf, ReqWrap.cbData));
        return EINVAL;
    }

    /*
     * Process the IOCtl.
     */
    size_t cbDataOut = 0;
    rc = vboxUsbSolarisProcessIOCtl(Cmd, pState, Mode, &ReqWrap, pvBuf, &cbDataOut);
    ReqWrap.rc = rc;
    rc = 0;

    if (RT_UNLIKELY(cbDataOut > ReqWrap.cbData))
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisIOCtl: Too much output data %d expected %d Truncating!\n", cbDataOut,
                ReqWrap.cbData));
        cbDataOut = ReqWrap.cbData;
    }

    ReqWrap.cbData = cbDataOut;

    /*
     * Copy VBOXUSBREQ back to userspace (which contains rc for USB operation).
     */
    rc = ddi_copyout(&ReqWrap, (void *)pArg, sizeof(ReqWrap), Mode);
    if (RT_LIKELY(!rc))
    {
        /*
         * Copy payload (if any) back to userspace.
         */
        if (cbDataOut > 0)
        {
            rc = ddi_copyout(pvBuf, (void *)(uintptr_t)ReqWrap.pvDataR3, cbDataOut, Mode);
            if (RT_UNLIKELY(rc))
            {
                LogRel((DEVICE_NAME ": VBoxUSBSolarisIOCtl: ddi_copyout failed! pvBuf=%p pArg=%p Cmd=%d. rc=%d\n", pvBuf, pArg,
                        Cmd, rc));
                rc = EFAULT;
            }
        }
    }
    else
    {
        LogRel((DEVICE_NAME ": VBoxUSBSolarisIOCtl: ddi_copyout(1)failed! pReqWrap=%p pArg=%p Cmd=%d. rc=%d\n", &ReqWrap, pArg,
                Cmd, rc));
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
 * @param   pvState             The USB device instance.
 * @param   Mode                The IOCtl mode.
 * @param   pUSBReq             Pointer to the VBOXUSB request.
 * @param   pvBuf               Pointer to the ring-3 URB.
 * @param   pcbDataOut          Where to store the IOCtl OUT data size.
 */
LOCAL int vboxUsbSolarisProcessIOCtl(int iFunction, void *pvState, int Mode, PVBOXUSBREQ pUSBReq, void *pvBuf,
                                     size_t *pcbDataOut)
{
    /* LogFunc((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: iFunction=%d pvState=%p pUSBReq=%p\n", iFunction, pvState, pUSBReq)); */

    AssertPtrReturn(pvState, VERR_INVALID_PARAMETER);
    vboxusb_state_t *pState = (vboxusb_state_t *)pvState;
    size_t cbData = pUSBReq->cbData;
    int rc;

#define CHECKRET_MIN_SIZE(mnemonic, cbMin) \
    do { \
        if (RT_UNLIKELY(cbData < (cbMin))) \
        { \
            LogRel((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: " mnemonic ": cbData=%#zx (%zu) min is %#zx (%zu)\n", \
                 cbData, cbData, (size_t)(cbMin), (size_t)(cbMin))); \
            return VERR_BUFFER_OVERFLOW; \
        } \
        if (RT_UNLIKELY((cbMin) != 0 && !RT_VALID_PTR(pvBuf))) \
        { \
            LogRel((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: " mnemonic ": Invalid pointer %p\n", pvBuf)); \
            return VERR_INVALID_PARAMETER; \
        } \
    } while (0)

    switch (iFunction)
    {
        case VBOXUSB_IOCTL_SEND_URB:
        {
            CHECKRET_MIN_SIZE("SEND_URB", sizeof(VBOXUSBREQ_URB));

            PVBOXUSBREQ_URB pUrbReq = (PVBOXUSBREQ_URB)pvBuf;
            rc = vboxUsbSolarisSendUrb(pState, pUrbReq, Mode);
            *pcbDataOut = 0;
            Log((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: SEND_URB returned %d\n", rc));
            break;
        }

        case VBOXUSB_IOCTL_REAP_URB:
        {
            CHECKRET_MIN_SIZE("REAP_URB", sizeof(VBOXUSBREQ_URB));

            PVBOXUSBREQ_URB pUrbReq = (PVBOXUSBREQ_URB)pvBuf;
            rc = vboxUsbSolarisReapUrb(pState, pUrbReq, Mode);
            *pcbDataOut = sizeof(VBOXUSBREQ_URB);
            Log((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: REAP_URB returned %d\n", rc));
            break;
        }

        case VBOXUSB_IOCTL_CLEAR_EP:
        {
            CHECKRET_MIN_SIZE("CLEAR_EP", sizeof(VBOXUSBREQ_CLEAR_EP));

            PVBOXUSBREQ_CLEAR_EP pClearEpReq = (PVBOXUSBREQ_CLEAR_EP)pvBuf;
            rc = vboxUsbSolarisClearEndPoint(pState, pClearEpReq->bEndpoint);
            *pcbDataOut = 0;
            Log((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: CLEAR_EP returned %d\n", rc));
            break;
        }

        case VBOXUSB_IOCTL_SET_CONFIG:
        {
            CHECKRET_MIN_SIZE("SET_CONFIG", sizeof(VBOXUSBREQ_SET_CONFIG));

            PVBOXUSBREQ_SET_CONFIG pSetCfgReq = (PVBOXUSBREQ_SET_CONFIG)pvBuf;
            rc = vboxUsbSolarisSetConfig(pState, pSetCfgReq->bConfigValue);
            *pcbDataOut = 0;
            Log((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: SET_CONFIG returned %d\n", rc));
            break;
        }

        case VBOXUSB_IOCTL_SET_INTERFACE:
        {
            CHECKRET_MIN_SIZE("SET_INTERFACE", sizeof(VBOXUSBREQ_SET_INTERFACE));

            PVBOXUSBREQ_SET_INTERFACE pSetInterfaceReq = (PVBOXUSBREQ_SET_INTERFACE)pvBuf;
            rc = vboxUsbSolarisSetInterface(pState, pSetInterfaceReq->bInterface, pSetInterfaceReq->bAlternate);
            *pcbDataOut = 0;
            Log((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: SET_INTERFACE returned %d\n", rc));
            break;
        }

        case VBOXUSB_IOCTL_CLOSE_DEVICE:
        {
            CHECKRET_MIN_SIZE("CLOSE_DEVICE", sizeof(VBOXUSBREQ_CLOSE_DEVICE));

            PVBOXUSBREQ_CLOSE_DEVICE pCloseDeviceReq = (PVBOXUSBREQ_CLOSE_DEVICE)pvBuf;
            if (   pCloseDeviceReq->ResetLevel != VBOXUSB_RESET_LEVEL_REATTACH
                || (Mode & FKIOCTL))
            {
                rc = vboxUsbSolarisCloseDevice(pState, pCloseDeviceReq->ResetLevel);
            }
            else
            {
                /* Userland IOCtls are not allowed to perform a reattach of the device. */
                rc = VERR_NOT_SUPPORTED;
            }
            *pcbDataOut = 0;
            Log((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: CLOSE_DEVICE returned %d\n", rc));
            break;
        }

        case VBOXUSB_IOCTL_ABORT_PIPE:
        {
            CHECKRET_MIN_SIZE("ABORT_PIPE", sizeof(VBOXUSBREQ_ABORT_PIPE));

            PVBOXUSBREQ_ABORT_PIPE pAbortPipeReq = (PVBOXUSBREQ_ABORT_PIPE)pvBuf;
            rc = vboxUsbSolarisAbortPipe(pState, pAbortPipeReq->bEndpoint);
            *pcbDataOut = 0;
            Log((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: ABORT_PIPE returned %d\n", rc));
            break;
        }

        case VBOXUSB_IOCTL_GET_CONFIG:
        {
            CHECKRET_MIN_SIZE("GET_CONFIG", sizeof(VBOXUSBREQ_GET_CONFIG));

            PVBOXUSBREQ_GET_CONFIG pGetCfgReq = (PVBOXUSBREQ_GET_CONFIG)pvBuf;
            rc = vboxUsbSolarisGetConfig(pState, &pGetCfgReq->bConfigValue);
            *pcbDataOut = sizeof(VBOXUSBREQ_GET_CONFIG);
            Log((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: GET_CONFIG returned %d\n", rc));
            break;
        }

        case VBOXUSB_IOCTL_GET_VERSION:
        {
            CHECKRET_MIN_SIZE("GET_VERSION", sizeof(VBOXUSBREQ_GET_VERSION));

            PVBOXUSBREQ_GET_VERSION pGetVersionReq = (PVBOXUSBREQ_GET_VERSION)pvBuf;
            pGetVersionReq->u32Major = VBOXUSB_VERSION_MAJOR;
            pGetVersionReq->u32Minor = VBOXUSB_VERSION_MINOR;
            *pcbDataOut = sizeof(VBOXUSBREQ_GET_VERSION);
            rc = VINF_SUCCESS;
            Log((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: GET_VERSION returned %d\n", rc));
            break;
        }

        default:
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisProcessIOCtl: Unknown request %#x\n", iFunction));
            rc = VERR_NOT_SUPPORTED;
            *pcbDataOut = 0;
            break;
        }
    }

    pUSBReq->cbData = *pcbDataOut;
    return rc;
}


/**
 * Initializes device power management.
 *
 * @param   pState          The USB device instance.
 *
 * @returns VBox status code.
 */
LOCAL int vboxUsbSolarisInitPower(vboxusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisInitPower: pState=%p\n", pState));

    int rc = usb_handle_remote_wakeup(pState->pDip, USB_REMOTE_WAKEUP_ENABLE);
    if (rc == USB_SUCCESS)
    {
        vboxusb_power_t *pPower = RTMemAllocZ(sizeof(vboxusb_power_t));
        if (RT_LIKELY(pPower))
        {
            mutex_enter(&pState->Mtx);
            pState->pPower = pPower;
            pState->pPower->fPowerWakeup = false;
            mutex_exit(&pState->Mtx);

            uint_t PowerStates;
            rc = usb_create_pm_components(pState->pDip, &PowerStates);
            if (rc == USB_SUCCESS)
            {
                pState->pPower->fPowerWakeup = true;
                pState->pPower->PowerLevel = USB_DEV_OS_FULL_PWR;
                pState->pPower->PowerStates = PowerStates;

                rc = pm_raise_power(pState->pDip, 0 /* component */, USB_DEV_OS_FULL_PWR);

                if (rc != DDI_SUCCESS)
                {
                    LogRel((DEVICE_NAME ": vboxUsbSolarisInitPower: Failed to raise power level usb(%#x,%#x)\n",
                            pState->pDevDesc->dev_descr->idVendor, pState->pDevDesc->dev_descr->idProduct));
                }
            }
            else
                Log((DEVICE_NAME ": vboxUsbSolarisInitPower: Failed to create power components\n"));

            return VINF_SUCCESS;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        Log((DEVICE_NAME ": vboxUsbSolarisInitPower: Failed to enable remote wakeup, No PM!\n"));
        rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Destroys device power management.
 *
 * @param   pState          The USB device instance.
 * @remarks Requires the device state mutex to be held.
 */
LOCAL void vboxUsbSolarisDestroyPower(vboxusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisDestroyPower: pState=%p\n", pState));

    if (pState->pPower)
    {
        mutex_exit(&pState->Mtx);
        vboxUsbSolarisPowerBusy(pState);
        mutex_enter(&pState->Mtx);

        int rc = -1;
        if (   pState->pPower->fPowerWakeup
            && pState->DevState != USB_DEV_DISCONNECTED)
        {
            mutex_exit(&pState->Mtx);
            rc = pm_raise_power(pState->pDip, 0 /* component */, USB_DEV_OS_FULL_PWR);
            if (rc != DDI_SUCCESS)
                Log((DEVICE_NAME ": vboxUsbSolarisDestroyPower: Raising power failed! rc=%d\n", rc));

            rc = usb_handle_remote_wakeup(pState->pDip, USB_REMOTE_WAKEUP_DISABLE);
            if (rc != DDI_SUCCESS)
                Log((DEVICE_NAME ": vboxUsbSolarisDestroyPower: Failed to disable remote wakeup\n"));
        }
        else
            mutex_exit(&pState->Mtx);

        rc = pm_lower_power(pState->pDip, 0 /* component */, USB_DEV_OS_PWR_OFF);
        if (rc != DDI_SUCCESS)
            Log((DEVICE_NAME ": vboxUsbSolarisDestroyPower: Lowering power failed! rc=%d\n", rc));

        vboxUsbSolarisPowerIdle(pState);
        mutex_enter(&pState->Mtx);
        RTMemFree(pState->pPower);
        pState->pPower = NULL;
    }
}


/**
 * Converts Solaris' USBA URB status to VBox's USB URB status.
 *
 * @param   Status          Solaris USBA USB URB status.
 *
 * @returns VBox USB URB status.
 */
LOCAL VUSBSTATUS vboxUsbSolarisGetUrbStatus(usb_cr_t Status)
{
    switch (Status)
    {
        case USB_CR_OK:                 return VUSBSTATUS_OK;
        case USB_CR_CRC:                return VUSBSTATUS_CRC;
        case USB_CR_DEV_NOT_RESP:       return VUSBSTATUS_DNR;
        case USB_CR_DATA_UNDERRUN:      return VUSBSTATUS_DATA_UNDERRUN;
        case USB_CR_DATA_OVERRUN:       return VUSBSTATUS_DATA_OVERRUN;
        case USB_CR_STALL:              return VUSBSTATUS_STALL;
        /*
        case USB_CR_BITSTUFFING:
        case USB_CR_DATA_TOGGLE_MM:
        case USB_CR_PID_CHECKFAILURE:
        case USB_CR_UNEXP_PID:
        case USB_CR_BUFFER_OVERRUN:
        case USB_CR_BUFFER_UNDERRUN:
        case USB_CR_TIMEOUT:
        case USB_CR_NOT_ACCESSED:
        case USB_CR_NO_RESOURCES:
        case USB_CR_UNSPECIFIED_ERR:
        case USB_CR_STOPPED_POLLING:
        case USB_CR_PIPE_CLOSING:
        case USB_CR_PIPE_RESET:
        case USB_CR_NOT_SUPPORTED:
        case USB_CR_FLUSHED:
        case USB_CR_HC_HARDWARE_ERR:
        */
        default:                        return VUSBSTATUS_INVALID;
    }
}


/**
 * Converts Solaris' USBA error code to VBox's error code.
 *
 * @param   UsbRc           Solaris USBA error code.
 *
 * @returns VBox error code.
 */
static int vboxUsbSolarisToVBoxRC(int UsbRc)
{
    switch (UsbRc)
    {
        case USB_SUCCESS:           return VINF_SUCCESS;
        case USB_INVALID_ARGS:      return VERR_INVALID_PARAMETER;
        case USB_INVALID_PIPE:      return VERR_BAD_PIPE;
        case USB_INVALID_CONTEXT:   return VERR_INVALID_CONTEXT;
        case USB_BUSY:              return VERR_PIPE_BUSY;
        case USB_PIPE_ERROR:        return VERR_PIPE_IO_ERROR;
        /*
        case USB_FAILURE:
        case USB_NO_RESOURCES:
        case USB_NO_BANDWIDTH:
        case USB_NOT_SUPPORTED:
        case USB_PIPE_ERROR:
        case USB_NO_FRAME_NUMBER:
        case USB_INVALID_START_FRAME:
        case USB_HC_HARDWARE_ERROR:
        case USB_INVALID_REQUEST:
        case USB_INVALID_VERSION:
        case USB_INVALID_PERM:
        */
        default:                    return VERR_GENERAL_FAILURE;
    }
}


/**
 * Converts Solaris' USBA device state to VBox's error code.
 *
 * @param   uDeviceState        The USB device state to convert.
 *
 * @returns VBox error code.
 */
static int vboxUsbSolarisDeviceState(uint8_t uDeviceState)
{
    switch (uDeviceState)
    {
        case USB_DEV_ONLINE:        return VINF_SUCCESS;
        case USB_DEV_SUSPENDED:     return VERR_VUSB_DEVICE_IS_SUSPENDED;
        case USB_DEV_DISCONNECTED:
        case USB_DEV_PWRED_DOWN:    return VERR_VUSB_DEVICE_NOT_ATTACHED;
        default:                    return VERR_GENERAL_FAILURE;
    }
}


/**
 * Checks if the device is a USB device.
 *
 * @param   pDip            Pointer to this device info. structure.
 *
 * @returns If this is really a USB device returns true, otherwise false.
 */
LOCAL bool vboxUsbSolarisIsUSBDevice(dev_info_t *pDip)
{
    int rc = DDI_FAILURE;

    /*
     * Check device for "usb" compatible property, root hubs->device would likely mean parent has no "usb" property.
     */
    char **ppszCompatible = NULL;
    uint_t cCompatible;
    rc = ddi_prop_lookup_string_array(DDI_DEV_T_ANY, pDip, DDI_PROP_DONTPASS, "compatible", &ppszCompatible, &cCompatible);
    if (RT_LIKELY(rc == DDI_PROP_SUCCESS))
    {
        while (cCompatible--)
        {
            Log((DEVICE_NAME ": vboxUsbSolarisIsUSBDevice: Compatible[%d]=%s\n", cCompatible, ppszCompatible[cCompatible]));
            if (!strncmp(ppszCompatible[cCompatible], "usb", 3))
            {
                Log((DEVICE_NAME ": vboxUsbSolarisIsUSBDevice: Verified device as USB. pszCompatible=%s\n",
                     ppszCompatible[cCompatible]));
                ddi_prop_free(ppszCompatible);
                return true;
            }
        }

        ddi_prop_free(ppszCompatible);
        ppszCompatible = NULL;
    }
    else
        Log((DEVICE_NAME ": vboxUsbSolarisIsUSBDevice: USB property lookup failed, rc=%d\n", rc));

    /*
     * Check parent for "usb" compatible property.
     */
    dev_info_t *pParentDip = ddi_get_parent(pDip);
    if (pParentDip)
    {
        rc = ddi_prop_lookup_string_array(DDI_DEV_T_ANY, pParentDip, DDI_PROP_DONTPASS, "compatible", &ppszCompatible,
                                          &cCompatible);
        if (RT_LIKELY(rc == DDI_PROP_SUCCESS))
        {
            while (cCompatible--)
            {
                Log((DEVICE_NAME ": vboxUsbSolarisIsUSBDevice: Parent compatible[%d]=%s\n", cCompatible,
                     ppszCompatible[cCompatible]));
                if (!strncmp(ppszCompatible[cCompatible], "usb", 3))
                {
                    Log((DEVICE_NAME ": vboxUsbSolarisIsUSBDevice: Verified device as USB. parent pszCompatible=%s\n",
                            ppszCompatible[cCompatible]));
                    ddi_prop_free(ppszCompatible);
                    return true;
                }
            }

            ddi_prop_free(ppszCompatible);
            ppszCompatible = NULL;
        }
        else
            Log((DEVICE_NAME ": vboxUsbSolarisIsUSBDevice: USB parent property lookup failed. rc=%d\n", rc));
    }
    else
        Log((DEVICE_NAME ": vboxUsbSolarisIsUSBDevice: Failed to obtain parent device for property lookup\n"));

    return false;
}


/**
 * Submits a URB.
 *
 * @param   pState          The USB device instance.
 * @param   pUrbReq         Pointer to the VBox USB URB.
 * @param   Mode            The IOCtl mode.
 *
 * @returns VBox error code.
 */
LOCAL int vboxUsbSolarisSendUrb(vboxusb_state_t *pState, PVBOXUSBREQ_URB pUrbReq, int Mode)
{
    int iEpIndex = VBOXUSB_GET_EP_INDEX(pUrbReq->bEndpoint);
    Assert(iEpIndex >= 0 && iEpIndex < RT_ELEMENTS(pState->aEps));
    vboxusb_ep_t *pEp = &pState->aEps[iEpIndex];
    AssertPtrReturn(pEp, VERR_INVALID_POINTER);
    Assert(pUrbReq);

#if 0
    LogFunc((DEVICE_NAME ": vboxUsbSolarisSendUrb: pState=%p pUrbReq=%p bEndpoint=%#x[%d] enmDir=%#x enmType=%#x "
             "cbData=%d pvData=%p\n", pState, pUrbReq, pUrbReq->bEndpoint, iEpIndex, pUrbReq->enmDir,
             pUrbReq->enmType, pUrbReq->cbData, pUrbReq->pvData));
#endif

    if (RT_UNLIKELY(!pUrbReq->pvData))
    {
        LogRel((DEVICE_NAME ": vboxUsbSolarisSendUrb: Invalid request - No data\n"));
        return VERR_INVALID_POINTER;
    }

    /*
     * Allocate message block & copy userspace buffer for host to device Xfers and for
     * Control Xfers (since input has Setup header that needs copying).
     */
    mblk_t *pMsg = NULL;
    int rc = VINF_SUCCESS;
    if (   pUrbReq->enmDir == VUSBDIRECTION_OUT
        || pUrbReq->enmType == VUSBXFERTYPE_MSG)
    {
        pMsg = allocb(pUrbReq->cbData, BPRI_HI);
        if (RT_UNLIKELY(!pMsg))
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisSendUrb: Failed to allocate %u bytes\n", pUrbReq->cbData));
            return VERR_NO_MEMORY;
        }

        rc = ddi_copyin(pUrbReq->pvData, pMsg->b_wptr, pUrbReq->cbData, Mode);
        if (RT_UNLIKELY(rc))
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisSendUrb: ddi_copyin failed! rc=%d\n", rc));
            freemsg(pMsg);
            return VERR_NO_MEMORY;
        }

        pMsg->b_wptr += pUrbReq->cbData;
    }

    mutex_enter(&pState->Mtx);
    rc = vboxUsbSolarisDeviceState(pState->DevState);
    if (!pState->fDefaultPipeOpen)    /* Required for Isoc. IN Xfers which don't Xfer through the pipe after polling starts */
        rc = VERR_VUSB_DEVICE_NOT_ATTACHED;
    if (RT_SUCCESS(rc))
    {
        /*
         * Open the pipe if needed.
         */
        rc = vboxUsbSolarisOpenPipe(pState, pEp);
        if (RT_UNLIKELY(RT_FAILURE(rc)))
        {
            mutex_exit(&pState->Mtx);
            freemsg(pMsg);
            LogRel((DEVICE_NAME ": vboxUsbSolarisSendUrb: OpenPipe failed! pState=%p pUrbReq=%p bEndpoint=%#x enmDir=%#x "
                    "enmType=%#x cbData=%d pvData=%p rc=%d\n", pState, pUrbReq, pUrbReq->bEndpoint, pUrbReq->enmDir,
                    pUrbReq->enmType, pUrbReq->cbData, pUrbReq->pvData, rc));
            return VERR_BAD_PIPE;
        }

        mutex_exit(&pState->Mtx);

        vboxusb_urb_t *pUrb = NULL;
        if (   pUrbReq->enmType == VUSBXFERTYPE_ISOC
            && pUrbReq->enmDir  == VUSBDIRECTION_IN)
            pUrb = vboxUsbSolarisGetIsocInUrb(pState, pUrbReq);
        else
            pUrb = vboxUsbSolarisQueueUrb(pState, pUrbReq, pMsg);

        if (RT_LIKELY(pUrb))
        {
            switch (pUrb->enmType)
            {
                case VUSBXFERTYPE_MSG:
                {
                    rc = vboxUsbSolarisCtrlXfer(pState, pEp, pUrb);
                    break;
                }

                case VUSBXFERTYPE_BULK:
                {
                    rc = vboxUsbSolarisBulkXfer(pState, pEp, pUrb);
                    break;
                }

                case VUSBXFERTYPE_INTR:
                {
                    rc = vboxUsbSolarisIntrXfer(pState, pEp, pUrb);
                    break;
                }

                case VUSBXFERTYPE_ISOC:
                {
                    rc = vboxUsbSolarisIsocXfer(pState, pEp, pUrb);
                    break;
                }

                default:
                {
                    LogRelMax(5, (DEVICE_NAME ": vboxUsbSolarisSendUrb: URB type unsupported %d\n", pUrb->enmType));
                    rc = VERR_NOT_SUPPORTED;
                    break;
                }
            }

            if (RT_FAILURE(rc))
            {
                mutex_enter(&pState->Mtx);
                freemsg(pUrb->pMsg);
                pUrb->pMsg = NULL;
                pMsg = NULL;

                if (   pUrb->enmType == VUSBXFERTYPE_ISOC
                    && pUrb->enmDir  == VUSBDIRECTION_IN)
                {
                    RTMemFree(pUrb);
                    pUrb = NULL;
                }
                else
                {
                    /*
                     * Xfer failed, move URB back to the free list.
                     */
                    list_remove(&pState->hInflightUrbs, pUrb);
                    Assert(pState->cInflightUrbs > 0);
                    --pState->cInflightUrbs;

                    pUrb->enmState = VBOXUSB_URB_STATE_FREE;
                    Assert(!pUrb->pMsg);
                    list_insert_head(&pState->hFreeUrbs, pUrb);
                    ++pState->cFreeUrbs;
                }
                mutex_exit(&pState->Mtx);
            }
        }
        else
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisSendUrb: Failed to queue URB\n"));
            rc = VERR_NO_MEMORY;
            freemsg(pMsg);
        }
    }
    else
    {
        mutex_exit(&pState->Mtx);
        freemsg(pMsg);
    }

    return rc;
}


/**
 * Reaps a completed URB.
 *
 * @param   pState          The USB device instance.
 * @param   pUrbReq         Pointer to the VBox USB URB.
 * @param   Mode            The IOCtl mode.
 *
 * @returns VBox error code.
 */
LOCAL int vboxUsbSolarisReapUrb(vboxusb_state_t *pState, PVBOXUSBREQ_URB pUrbReq, int Mode)
{
    /* LogFunc((DEVICE_NAME ": vboxUsbSolarisReapUrb: pState=%p pUrbReq=%p\n", pState, pUrbReq)); */

    AssertPtrReturn(pUrbReq, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    mutex_enter(&pState->Mtx);
    rc = vboxUsbSolarisDeviceState(pState->DevState);
    if (!pState->fDefaultPipeOpen)
        rc = VERR_VUSB_DEVICE_NOT_ATTACHED;
    if (RT_SUCCESS(rc))
    {
        vboxusb_urb_t *pUrb = list_remove_head(&pState->hLandedUrbs);
        if (pUrb)
        {
            Assert(pState->cLandedUrbs > 0);
            --pState->cLandedUrbs;
        }

        /*
         * It is safe to access pUrb->pMsg outside the state mutex because this is from the landed URB list
         * and not the inflight URB list.
         */
        mutex_exit(&pState->Mtx);
        if (pUrb)
        {
            /*
             * Copy the URB which will then be copied to user-space.
             */
            pUrbReq->pvUrbR3   = pUrb->pvUrbR3;
            pUrbReq->bEndpoint = pUrb->bEndpoint;
            pUrbReq->enmType   = pUrb->enmType;
            pUrbReq->enmDir    = pUrb->enmDir;
            pUrbReq->enmStatus = pUrb->enmStatus;
            pUrbReq->pvData    = (void *)pUrb->pvDataR3;
            pUrbReq->cbData    = pUrb->cbDataR3;

            if (RT_LIKELY(pUrb->pMsg))
            {
                /*
                 * Copy the message back into the user buffer.
                 */
                if (RT_LIKELY(pUrb->pvDataR3 != NIL_RTR3PTR))
                {
                    Assert(!pUrb->pMsg->b_cont);      /* We really should have a single message block always. */
                    size_t cbData = RT_MIN(MBLKL(pUrb->pMsg), pUrb->cbDataR3);
                    pUrbReq->cbData = cbData;

                    if (RT_LIKELY(cbData))
                    {
                        rc = ddi_copyout(pUrb->pMsg->b_rptr, (void *)pUrbReq->pvData, cbData, Mode);
                        if (RT_UNLIKELY(rc))
                        {
                            LogRel((DEVICE_NAME ": vboxUsbSolarisReapUrb: ddi_copyout failed! rc=%d\n", rc));
                            pUrbReq->enmStatus = VUSBSTATUS_INVALID;
                        }
                    }

                    Log((DEVICE_NAME ": vboxUsbSolarisReapUrb: pvUrbR3=%p pvDataR3=%p cbData=%d\n", pUrbReq->pvUrbR3,
                         pUrbReq->pvData, pUrbReq->cbData));
                }
                else
                {
                    pUrbReq->cbData = 0;
                    rc = VERR_INVALID_POINTER;
                    LogRel((DEVICE_NAME ": vboxUsbSolarisReapUrb: Missing pvDataR3!!\n"));
                }

                /*
                 * Free buffer allocated in vboxUsbSolarisSendUrb or vboxUsbSolaris[Ctrl|Bulk|Intr]Xfer().
                 */
                freemsg(pUrb->pMsg);
                pUrb->pMsg = NULL;
            }
            else
            {
                if (   pUrb->enmType == VUSBXFERTYPE_ISOC
                    && pUrb->enmDir == VUSBDIRECTION_IN)
                {
                    pUrbReq->enmStatus = VUSBSTATUS_INVALID;
                    pUrbReq->cbData = 0;
                }
            }

            /*
             * Copy Isoc packet descriptors.
             */
            if (pUrb->enmType == VUSBXFERTYPE_ISOC)
            {
                AssertCompile(sizeof(pUrbReq->aIsocPkts) == sizeof(pUrb->aIsocPkts));
                pUrbReq->cIsocPkts = pUrb->cIsocPkts;

                for (unsigned i = 0; i < pUrb->cIsocPkts; i++)
                {
                    pUrbReq->aIsocPkts[i].cbPkt     = pUrb->aIsocPkts[i].cbPkt;
                    pUrbReq->aIsocPkts[i].cbActPkt  = pUrb->aIsocPkts[i].cbActPkt;
                    pUrbReq->aIsocPkts[i].enmStatus = pUrb->aIsocPkts[i].enmStatus;
                }

                if (pUrb->enmDir == VUSBDIRECTION_IN)
                {
                    RTMemFree(pUrb);
                    pUrb = NULL;
                }
            }

            if (pUrb)
            {
                /*
                 * Add URB back to the free list.
                 */
                Assert(!pUrb->pMsg);
                pUrb->cbDataR3 = 0;
                pUrb->pvDataR3 = NIL_RTR3PTR;
                pUrb->enmState = VBOXUSB_URB_STATE_FREE;
                mutex_enter(&pState->Mtx);
                list_insert_head(&pState->hFreeUrbs, pUrb);
                ++pState->cFreeUrbs;
                mutex_exit(&pState->Mtx);
            }
        }
        else
        {
            pUrbReq->pvUrbR3 = NULL;
            pUrbReq->cbData  = 0;
            pUrbReq->pvData  = NULL;
            pUrbReq->enmStatus = VUSBSTATUS_INVALID;
        }
    }
    else
        mutex_exit(&pState->Mtx);

    return rc;
}


/**
 * Clears a pipe (CLEAR_FEATURE).
 *
 * @param   pState          The USB device instance.
 * @param   bEndpoint       The Endpoint address.
 *
 * @returns VBox error code.
 */
LOCAL int vboxUsbSolarisClearEndPoint(vboxusb_state_t *pState, uint8_t bEndpoint)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisClearEndPoint: pState=%p bEndpoint=%#x\n", pState, bEndpoint));

    mutex_enter(&pState->Mtx);
    int rc = vboxUsbSolarisDeviceState(pState->DevState);
    if (RT_SUCCESS(rc))
    {
        int iEpIndex = VBOXUSB_GET_EP_INDEX(bEndpoint);
        Assert(iEpIndex >= 0 && iEpIndex < RT_ELEMENTS(pState->aEps));
        vboxusb_ep_t *pEp = &pState->aEps[iEpIndex];
        if (RT_LIKELY(pEp))
        {
            /*
             * Check if the endpoint is open to be cleared.
             */
            if (pEp->pPipe)
            {
                mutex_exit(&pState->Mtx);

                /*
                 * Synchronous reset pipe.
                 */
                usb_pipe_reset(pState->pDip, pEp->pPipe,
                                        USB_FLAGS_SLEEP,   /* Synchronous */
                                        NULL,              /* Completion callback */
                                        NULL);             /* Exception callback */

                mutex_enter(&pState->Mtx);

                Log((DEVICE_NAME ": vboxUsbSolarisClearEndPoint: bEndpoint=%#x[%d] returns %d\n", bEndpoint, iEpIndex, rc));

                rc = VINF_SUCCESS;
            }
            else
            {
                Log((DEVICE_NAME ": vboxUsbSolarisClearEndPoint: Not opened to be cleared. Faking success. bEndpoint=%#x\n",
                     bEndpoint));
                rc = VINF_SUCCESS;
            }
        }
        else
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisClearEndPoint: Endpoint missing! bEndpoint=%#x[%d]\n", bEndpoint, iEpIndex));
            rc = VERR_GENERAL_FAILURE;
        }
    }
    else
        Log((DEVICE_NAME ": vboxUsbSolarisClearEndPoint: Device not online, state=%d\n", pState->DevState));

    mutex_exit(&pState->Mtx);
    return rc;
}


/**
 * Sets configuration (SET_CONFIGURATION)
 *
 * @param   pState          The USB device instance.
 * @param   bConfig         The Configuration.
 *
 * @returns VBox error code.
 */
LOCAL int vboxUsbSolarisSetConfig(vboxusb_state_t *pState, uint8_t bConfig)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisSetConfig: pState=%p bConfig=%u\n", pState, bConfig));

    mutex_enter(&pState->Mtx);
    int rc = vboxUsbSolarisDeviceState(pState->DevState);
    if (RT_SUCCESS(rc))
    {
        vboxUsbSolarisCloseAllPipes(pState, false /* ControlPipe */);
        int iCfgIndex = vboxUsbSolarisGetConfigIndex(pState, bConfig);

        if (   iCfgIndex >= 0
            && iCfgIndex < pState->pDevDesc->dev_n_cfg)
        {
            /*
             * Switch Config synchronously.
             */
            mutex_exit(&pState->Mtx);
            rc = usb_set_cfg(pState->pDip, (uint_t)iCfgIndex, USB_FLAGS_SLEEP, NULL /* callback */, NULL /* callback data */);
            mutex_enter(&pState->Mtx);

            if (rc == USB_SUCCESS)
            {
                int rc2 = vboxUsbSolarisInitEpsForCfg(pState);
                AssertRC(rc2); NOREF(rc2);
                rc = VINF_SUCCESS;
            }
            else
            {
                LogRel((DEVICE_NAME ": vboxUsbSolarisSetConfig: usb_set_cfg failed for iCfgIndex=%#x bConfig=%u rc=%d\n",
                            iCfgIndex, bConfig, rc));
                rc = vboxUsbSolarisToVBoxRC(rc);
            }
        }
        else
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisSetConfig: Invalid iCfgIndex=%d bConfig=%u\n", iCfgIndex, bConfig));
            rc = VERR_OUT_OF_RANGE;
        }
    }

    mutex_exit(&pState->Mtx);

    return rc;
}


/**
 * Gets configuration (GET_CONFIGURATION)
 *
 * @param   pState          The USB device instance.
 * @param   pbConfig        Where to store the Configuration.
 *
 * @returns VBox error code.
 */
LOCAL int vboxUsbSolarisGetConfig(vboxusb_state_t *pState, uint8_t *pbConfig)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisGetConfig: pState=%p pbConfig=%p\n", pState, pbConfig));
    AssertPtrReturn(pbConfig, VERR_INVALID_POINTER);

    /*
     * Get Config synchronously.
     */
    uint_t bConfig;
    int rc = usb_get_cfg(pState->pDip, &bConfig, USB_FLAGS_SLEEP);
    if (RT_LIKELY(rc == USB_SUCCESS))
    {
        *pbConfig = bConfig;
        rc = VINF_SUCCESS;
    }
    else
    {
        LogRel((DEVICE_NAME ": vboxUsbSolarisGetConfig: Failed, rc=%d\n", rc));
        rc = vboxUsbSolarisToVBoxRC(rc);
    }

    Log((DEVICE_NAME ": vboxUsbSolarisGetConfig: Returns %d bConfig=%u\n", rc, *pbConfig));
    return rc;
}


/**
 * Sets interface (SET_INTERFACE) and alternate.
 *
 * @param   pState          The USB device instance.
 * @param   bIf             The Interface.
 * @param   bAlt            The Alternate setting.
 *
 * @returns VBox error code.
 */
LOCAL int vboxUsbSolarisSetInterface(vboxusb_state_t *pState, uint8_t bIf, uint8_t bAlt)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisSetInterface: pState=%p bIf=%#x bAlt=%#x\n", pState, bIf, bAlt));

    mutex_enter(&pState->Mtx);
    int rc = vboxUsbSolarisDeviceState(pState->DevState);
    if (RT_SUCCESS(rc))
    {
        /*
         * Set Interface & Alt setting synchronously.
         */
        mutex_exit(&pState->Mtx);
        rc = usb_set_alt_if(pState->pDip, bIf, bAlt, USB_FLAGS_SLEEP, NULL /* callback */, NULL /* callback data */);
        mutex_enter(&pState->Mtx);

        if (rc == USB_SUCCESS)
        {
            Log((DEVICE_NAME ": vboxUsbSolarisSetInterface: Success, bIf=%#x bAlt=%#x\n", bIf, bAlt, rc));
            int rc2 = vboxUsbSolarisInitEpsForIfAlt(pState, bIf, bAlt);
            AssertRC(rc2); NOREF(rc2);
            rc = VINF_SUCCESS;
        }
        else
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisSetInterface: usb_set_alt_if failed for bIf=%#x bAlt=%#x rc=%d\n", bIf, bAlt, rc));
            rc = vboxUsbSolarisToVBoxRC(rc);
        }
    }

    mutex_exit(&pState->Mtx);

    return rc;
}


/**
 * Closes the USB device and optionally resets it.
 *
 * @param   pState          The USB device instance.
 * @param   enmReset        The reset level.
 *
 * @returns VBox error code.
 */
LOCAL int vboxUsbSolarisCloseDevice(vboxusb_state_t *pState, VBOXUSB_RESET_LEVEL enmReset)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisCloseDevice: pState=%p enmReset=%d\n", pState, enmReset));

    mutex_enter(&pState->Mtx);
    int rc = vboxUsbSolarisDeviceState(pState->DevState);

    if (enmReset == VBOXUSB_RESET_LEVEL_CLOSE)
        vboxUsbSolarisCloseAllPipes(pState, true /* ControlPipe */);
    else
        vboxUsbSolarisCloseAllPipes(pState, false /* ControlPipe */);

    mutex_exit(&pState->Mtx);

    if (RT_SUCCESS(rc))
    {
        switch (enmReset)
        {
            case VBOXUSB_RESET_LEVEL_REATTACH:
                rc = usb_reset_device(pState->pDip, USB_RESET_LVL_REATTACH);
                break;

            case VBOXUSB_RESET_LEVEL_SOFT:
                rc = usb_reset_device(pState->pDip, USB_RESET_LVL_DEFAULT);
                break;

            default:
                rc = USB_SUCCESS;
                break;
        }

        rc = vboxUsbSolarisToVBoxRC(rc);
    }

    Log((DEVICE_NAME ": vboxUsbSolarisCloseDevice: Returns %d\n", rc));
    return rc;
}


/**
 * Aborts pending requests and reset the pipe.
 *
 * @param   pState          The USB device instance.
 * @param   bEndpoint       The Endpoint address.
 *
 * @returns VBox error code.
 */
LOCAL int vboxUsbSolarisAbortPipe(vboxusb_state_t *pState, uint8_t bEndpoint)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisAbortPipe: pState=%p bEndpoint=%#x\n", pState, bEndpoint));

    mutex_enter(&pState->Mtx);
    int rc = vboxUsbSolarisDeviceState(pState->DevState);
    if (RT_SUCCESS(rc))
    {
        int iEpIndex = VBOXUSB_GET_EP_INDEX(bEndpoint);
        Assert(iEpIndex >= 0 && iEpIndex < RT_ELEMENTS(pState->aEps));
        vboxusb_ep_t *pEp = &pState->aEps[iEpIndex];
        if (RT_LIKELY(pEp))
        {
            if (pEp->pPipe)
            {
                /*
                 * Aborting requests not supported for the default control pipe.
                 */
                if ((pEp->EpDesc.bEndpointAddress & USB_EP_NUM_MASK) == 0)
                {
                    mutex_exit(&pState->Mtx);
                    LogRel((DEVICE_NAME ": vboxUsbSolarisAbortPipe: Cannot reset default control pipe\n"));
                    return VERR_NOT_SUPPORTED;
                }

                mutex_exit(&pState->Mtx);
                usb_pipe_reset(pState->pDip, pEp->pPipe,
                               USB_FLAGS_SLEEP,  /* Synchronous */
                               NULL,             /* Completion callback */
                               NULL);            /* Callback's parameter */

                /*
                 * Allow pending async requests to complete.
                 */
                /** @todo this is most likely not required. */
                rc = usb_pipe_drain_reqs(pState->pDip, pEp->pPipe,
                                USB_FLAGS_SLEEP, /* Synchronous */
                                5,               /* Timeout (seconds) */
                                NULL,            /* Completion callback */
                                NULL);           /* Callback's parameter */

                mutex_enter(&pState->Mtx);

                Log((DEVICE_NAME ": vboxUsbSolarisAbortPipe: usb_pipe_drain_reqs returns %d\n", rc));
                rc = vboxUsbSolarisToVBoxRC(rc);
            }
            else
            {
                LogRel((DEVICE_NAME ": vboxUsbSolarisAbortPipe: pipe not open. bEndpoint=%#x\n", bEndpoint));
                rc = VERR_PIPE_IO_ERROR;
            }
        }
        else
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisAbortPipe: Invalid pipe bEndpoint=%#x[%d]\n", bEndpoint, iEpIndex));
            rc = VERR_INVALID_HANDLE;
        }
    }

    mutex_exit(&pState->Mtx);

    LogFunc((DEVICE_NAME ": vboxUsbSolarisAbortPipe: Returns %d\n", rc));
    return rc;
}


/**
 * Initializes an Endpoint.
 *
 * @param   pState          The USB device instance.
 * @param   pEpData         The Endpoint data (NULL implies the default
 *                          endpoint).
 *
 * @returns VBox error code.
 */
LOCAL int vboxUsbSolarisInitEp(vboxusb_state_t *pState, usb_ep_data_t *pEpData)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisInitEp: pState=%p pEpData=%p", pState, pEpData));

    /*
     * Is this the default endpoint?
     */
    usb_ep_descr_t *pEpDesc = NULL;
    vboxusb_ep_t *pEp = NULL;
    int iEpIndex;
    if (!pEpData)
    {
        iEpIndex = 0;
        pEpDesc = &g_VBoxUSBSolarisDefaultEpDesc;
    }
    else
    {
        iEpIndex = VBOXUSB_GET_EP_INDEX(pEpData->ep_descr.bEndpointAddress);
        pEpDesc  = (usb_ep_descr_t *)((uint8_t *)pEpData + g_offUsbEpDataDescr);
    }

    Assert(iEpIndex >= 0 && iEpIndex < RT_ELEMENTS(pState->aEps));
    pEp = &pState->aEps[iEpIndex];

    /*
     * Initialize the endpoint.
     */
    pEp->EpDesc = *pEpDesc;
    if (!pEp->fInitialized)
    {
        pEp->pPipe = NULL;
        bzero(&pEp->PipePolicy, sizeof(pEp->PipePolicy));
        pEp->PipePolicy.pp_max_async_reqs = VBOXUSB_MAX_PIPE_ASYNC_REQS;
        pEp->fIsocPolling = false;
        list_create(&pEp->hIsocInUrbs, sizeof(vboxusb_urb_t), offsetof(vboxusb_urb_t, hListLink));
        pEp->cIsocInUrbs = 0;
        list_create(&pEp->hIsocInLandedReqs, sizeof(vboxusb_isoc_req_t), offsetof(vboxusb_isoc_req_t, hListLink));
        pEp->cbIsocInLandedReqs = 0;
        pEp->cbMaxIsocData = 0;
        pEp->fInitialized = true;
    }

    Log((DEVICE_NAME ": vboxUsbSolarisInitEp: Success, %s[%2d] %s %s bEndpoint=%#x\n", !pEpData ? "Default " : "Endpoint",
         iEpIndex, vboxUsbSolarisEpType(pEp), vboxUsbSolarisEpDir(pEp), pEp->EpDesc.bEndpointAddress));
    return VINF_SUCCESS;
}


/**
 * Initializes Endpoints for the current configuration, all interfaces and
 * alternate setting 0 for each interface.
 *
 * @param   pState          The USB device instance.
 *
 * @returns VBox status code.
 */
LOCAL int vboxUsbSolarisInitEpsForCfg(vboxusb_state_t *pState)
{
    uint_t uCfgIndex = usb_get_current_cfgidx(pState->pDip);
    if (uCfgIndex >= pState->pDevDesc->dev_n_cfg)
    {
        LogRel((DEVICE_NAME ": vboxUsbSolarisInitEpsForCfg: Invalid current config index %u\n", uCfgIndex));
        return VERR_OUT_OF_RANGE;
    }

    usb_cfg_data_t *pConfig = &pState->pDevDesc->dev_cfg[uCfgIndex];
    uchar_t bConfig = pConfig->cfg_descr.bConfigurationValue;

    LogFunc((DEVICE_NAME ": vboxUsbSolarisInitEpsForCfg: pState=%p bConfig=%u uCfgIndex=%u\n", pState, bConfig, uCfgIndex));

    const uint_t cIfs = pConfig->cfg_n_if;
    for (uchar_t uIf = 0; uIf < cIfs; uIf++)
    {
        usb_if_data_t *pIf = &pConfig->cfg_if[uIf];
        const uint_t cAlts = pIf->if_n_alt;
        for (uchar_t uAlt = 0; uAlt < cAlts; uAlt++)
        {
            usb_alt_if_data_t *pAlt = &pIf->if_alt[uAlt];
            if (pAlt->altif_descr.bAlternateSetting == 0)   /* Refer USB 2.0 spec 9.6.5 "Interface" */
            {
                const uint_t cEps = pAlt->altif_n_ep;
                for (uchar_t uEp = 0; uEp < cEps; uEp++)
                {
                    uint8_t       *pbEpData = (uint8_t *)&pAlt->altif_ep[0];
                    usb_ep_data_t *pEpData  = (usb_ep_data_t *)(pbEpData + uEp * g_cbUsbEpData);
                    int rc = vboxUsbSolarisInitEp(pState, pEpData);
                    if (RT_FAILURE(rc))
                    {
                        LogRel((DEVICE_NAME ": vboxUsbSolarisInitEpsForCfg: Failed to init endpoint! "
                                "bConfig=%u bIf=%#x bAlt=%#x\n", bConfig, pAlt->altif_descr.bInterfaceNumber,
                                pAlt->altif_descr.bAlternateSetting));
                        return rc;
                    }
                }
                break;  /* move on to next interface. */
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * Initializes Endpoints for the given Interface & Alternate setting.
 *
 * @param   pState   The USB device instance.
 * @param   bIf      The Interface.
 * @param   bAlt     The Alterate.
 *
 * @returns VBox status code.
 */
LOCAL int vboxUsbSolarisInitEpsForIfAlt(vboxusb_state_t *pState, uint8_t bIf, uint8_t bAlt)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisInitEpsForIfAlt: pState=%p bIf=%d uAlt=%d\n", pState, bIf, bAlt));

    /* Doesn't hurt to be paranoid */
    uint_t uCfgIndex = usb_get_current_cfgidx(pState->pDip);
    if (uCfgIndex >= pState->pDevDesc->dev_n_cfg)
    {
        LogRel((DEVICE_NAME ": vboxUsbSolarisInitEpsForIfAlt: Invalid current config index %d\n", uCfgIndex));
        return VERR_OUT_OF_RANGE;
    }

    usb_cfg_data_t *pConfig = &pState->pDevDesc->dev_cfg[uCfgIndex];
    for (uchar_t uIf = 0; uIf < pConfig->cfg_n_if; uIf++)
    {
        usb_if_data_t *pInterface = &pConfig->cfg_if[uIf];
        const uint_t cAlts = pInterface->if_n_alt;
        for (uchar_t uAlt = 0; uAlt < cAlts; uAlt++)
        {
            usb_alt_if_data_t *pAlt = &pInterface->if_alt[uAlt];
            if (   pAlt->altif_descr.bInterfaceNumber  == bIf
                && pAlt->altif_descr.bAlternateSetting == bAlt)
            {
                const uint_t cEps = pAlt->altif_n_ep;
                for (uchar_t uEp = 0; uEp < cEps; uEp++)
                {
                    uint8_t       *pbEpData = (uint8_t *)&pAlt->altif_ep[0];
                    usb_ep_data_t *pEpData  = (usb_ep_data_t *)(pbEpData + uEp * g_cbUsbEpData);
                    int rc = vboxUsbSolarisInitEp(pState, pEpData);
                    if (RT_FAILURE(rc))
                    {
                        uint8_t bCfgValue = pConfig->cfg_descr.bConfigurationValue;
                        LogRel((DEVICE_NAME ": vboxUsbSolarisInitEpsForIfAlt: Failed to init endpoint! "
                                "bCfgValue=%u bIf=%#x bAlt=%#x\n", bCfgValue, bIf, bAlt));
                        return rc;
                    }
                }
                return VINF_SUCCESS;
            }
        }
    }
    return VERR_NOT_FOUND;
}


/**
 * Destroys all Endpoints.
 *
 * @param   pState          The USB device instance.
 *
 * @remarks Requires the state mutex to be held.
 *          Call only from Detach() or similar as callbacks
 */
LOCAL void vboxUsbSolarisDestroyAllEps(vboxusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisDestroyAllEps: pState=%p\n", pState));

    Assert(mutex_owned(&pState->Mtx));
    for (unsigned i = 0; i < VBOXUSB_MAX_ENDPOINTS; i++)
    {
        vboxusb_ep_t *pEp = &pState->aEps[i];
        if (pEp->fInitialized)
            vboxUsbSolarisDestroyEp(pState, pEp);
    }
}


/**
 * Destroys an Endpoint.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint.
 *
 * @remarks Requires the state mutex to be held.
 */
LOCAL void vboxUsbSolarisDestroyEp(vboxusb_state_t *pState, vboxusb_ep_t *pEp)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisDestroyEp: pState=%p pEp=%p\n", pState, pEp));

    Assert(pEp->fInitialized);
    Assert(mutex_owned(&pState->Mtx));
    vboxusb_urb_t *pUrb = list_remove_head(&pEp->hIsocInUrbs);
    while (pUrb)
    {
        if (pUrb->pMsg)
            freemsg(pUrb->pMsg);
        RTMemFree(pUrb);
        pUrb = list_remove_head(&pEp->hIsocInUrbs);
    }
    pEp->cIsocInUrbs = 0;
    list_destroy(&pEp->hIsocInUrbs);

    vboxusb_isoc_req_t *pIsocReq = list_remove_head(&pEp->hIsocInLandedReqs);
    while (pIsocReq)
    {
        kmem_free(pIsocReq, sizeof(vboxusb_isoc_req_t));
        pIsocReq = list_remove_head(&pEp->hIsocInLandedReqs);
    }
    pEp->cbIsocInLandedReqs = 0;
    list_destroy(&pEp->hIsocInLandedReqs);

    pEp->fInitialized = false;
}


/**
 * Closes all non-default pipes and drains the default pipe.
 *
 * @param   pState          The USB device instance.
 * @param   fDefault        Whether to close the default control pipe.
 *
 * @remarks Requires the device state mutex to be held.
 */
LOCAL void vboxUsbSolarisCloseAllPipes(vboxusb_state_t *pState, bool fDefault)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisCloseAllPipes: pState=%p\n", pState));

    for (int i = 1; i < VBOXUSB_MAX_ENDPOINTS; i++)
    {
        vboxusb_ep_t *pEp = &pState->aEps[i];
        if (   pEp
            && pEp->pPipe)
        {
            Log((DEVICE_NAME ": vboxUsbSolarisCloseAllPipes: Closing[%d]\n", i));
            vboxUsbSolarisClosePipe(pState, pEp);
        }
    }

    if (fDefault)
    {
        vboxusb_ep_t *pEp = &pState->aEps[0];
        if (   pEp
            && pEp->pPipe)
        {
            vboxUsbSolarisClosePipe(pState, pEp);
            Log((DEVICE_NAME ": vboxUsbSolarisCloseAllPipes: Closed default pipe\n"));
        }
    }
}


/**
 * Opens the pipe associated with an Endpoint.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint.
 * @remarks Requires the device state mutex to be held.
 *
 * @returns VBox status code.
 */
LOCAL int vboxUsbSolarisOpenPipe(vboxusb_state_t *pState, vboxusb_ep_t *pEp)
{
    Assert(mutex_owned(&pState->Mtx));

    /*
     * Make sure the Endpoint isn't open already.
     */
    if (pEp->pPipe)
        return VINF_SUCCESS;

    /*
     * Default Endpoint; already opened just copy the pipe handle.
     */
    if ((pEp->EpDesc.bEndpointAddress & USB_EP_NUM_MASK) == 0)
    {
        pEp->pPipe = pState->pDevDesc->dev_default_ph;
        Log((DEVICE_NAME ": vboxUsbSolarisOpenPipe: Default pipe opened\n"));
        return VINF_SUCCESS;
    }

    /*
     * Open the non-default pipe for the Endpoint.
     */
    mutex_exit(&pState->Mtx);
    int rc = usb_pipe_open(pState->pDip, &pEp->EpDesc, &pEp->PipePolicy, USB_FLAGS_NOSLEEP, &pEp->pPipe);
    mutex_enter(&pState->Mtx);
    if (rc == USB_SUCCESS)
    {
        LogFunc((DEVICE_NAME ": vboxUsbSolarisOpenPipe: Opened pipe, pState=%p pEp=%p\n", pState, pEp));
        usb_pipe_set_private(pEp->pPipe, (usb_opaque_t)pEp);

        /*
         * Determine input buffer size for Isoc. IN transfers.
         */
        if (   VBOXUSB_XFER_TYPE(pEp) == VUSBXFERTYPE_ISOC
            && VBOXUSB_XFER_DIR(pEp) == VUSB_DIR_TO_HOST)
        {
            /*
             * wMaxPacketSize bits 10..0 specifies maximum packet size which can hold 1024 bytes.
             * If bits 12..11 is non-zero, cbMax will be more than 1024 and thus the Endpoint is a
             * high-bandwidth Endpoint.
             */
            uint16_t cbMax = VBOXUSB_PKT_SIZE(pEp->EpDesc.wMaxPacketSize);
            if (cbMax <= 1024)
            {
                /* Buffer 1 second for highspeed and 8 seconds for fullspeed Endpoints. */
                pEp->cbMaxIsocData = 1000 * cbMax * 8;
            }
            else
            {
                /* Buffer about 400 milliseconds of data for highspeed high-bandwidth endpoints. */
                pEp->cbMaxIsocData = 400 * cbMax * 8;
            }
            Log((DEVICE_NAME ": vboxUsbSolarisOpenPipe: bEndpoint=%#x cbMaxIsocData=%u\n", pEp->EpDesc.bEndpointAddress,
                 pEp->cbMaxIsocData));
        }

        rc = VINF_SUCCESS;
    }
    else
    {
        LogRel((DEVICE_NAME ": vboxUsbSolarisOpenPipe: Failed! rc=%d pState=%p pEp=%p\n", rc, pState, pEp));
        rc = VERR_BAD_PIPE;
    }

    return rc;
}


/**
 * Closes the pipe associated with an Endpoint.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint.
 *
 * @remarks Requires the device state mutex to be held.
 */
LOCAL void vboxUsbSolarisClosePipe(vboxusb_state_t *pState, vboxusb_ep_t *pEp)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisClosePipe: pState=%p pEp=%p\n", pState, pEp));
    AssertPtr(pEp);

    if (pEp->pPipe)
    {
        /*
         * Default pipe: allow completion of pending requests.
         */
        if (pEp->pPipe == pState->pDevDesc->dev_default_ph)
        {
            mutex_exit(&pState->Mtx);
            usb_pipe_drain_reqs(pState->pDip, pEp->pPipe, 0, USB_FLAGS_SLEEP, NULL /* callback */, NULL /* callback arg. */);
            mutex_enter(&pState->Mtx);
            Log((DEVICE_NAME ": vboxUsbSolarisClosePipe: Closed default pipe\n"));
            pState->fDefaultPipeOpen = false;
        }
        else
        {
            /*
             * Stop Isoc. IN polling if required.
             */
            if (pEp->fIsocPolling)
            {
                pEp->fIsocPolling = false;
                mutex_exit(&pState->Mtx);
                usb_pipe_stop_isoc_polling(pEp->pPipe, USB_FLAGS_NOSLEEP);
                mutex_enter(&pState->Mtx);
            }

            /*
             * Non-default pipe: close it.
             */
            Log((DEVICE_NAME ": vboxUsbSolarisClosePipe: Pipe bmAttributes=%#x bEndpoint=%#x\n", pEp->EpDesc.bmAttributes,
                 pEp->EpDesc.bEndpointAddress));
            mutex_exit(&pState->Mtx);
            usb_pipe_close(pState->pDip, pEp->pPipe, USB_FLAGS_SLEEP, NULL /* callback */, NULL /* callback arg. */);
            mutex_enter(&pState->Mtx);
        }

        /*
         * Free the Endpoint data message block and reset pipe handle.
         */
        pEp->pPipe = NULL;

        Log((DEVICE_NAME ": vboxUsbSolarisClosePipe: Success, bEndpoint=%#x\n", pEp->EpDesc.bEndpointAddress));
    }

    Assert(pEp->pPipe == NULL);
}


/**
 * Finds the Configuration index for the passed in Configuration value.
 *
 * @param   pState          The USB device instance.
 * @param   bConfig         The Configuration.
 *
 * @returns The configuration index if found, otherwise -1.
 */
LOCAL int vboxUsbSolarisGetConfigIndex(vboxusb_state_t *pState, uint_t bConfig)
{
    for (int CfgIndex = 0; CfgIndex < pState->pDevDesc->dev_n_cfg; CfgIndex++)
    {
        usb_cfg_data_t *pConfig = &pState->pDevDesc->dev_cfg[CfgIndex];
        if (pConfig->cfg_descr.bConfigurationValue == bConfig)
            return CfgIndex;
    }

    return -1;
}


/**
 * Allocates and initializes an Isoc. In URB from the ring-3 equivalent.
 *
 * @param   pState          The USB device instance.
 * @param   pUrbReq         Opaque pointer to the complete request.
 *
 * @returns The allocated Isoc. In URB to be used.
 */
LOCAL vboxusb_urb_t *vboxUsbSolarisGetIsocInUrb(vboxusb_state_t *pState, PVBOXUSBREQ_URB pUrbReq)
{
    /*
     * Isoc. In URBs are not queued into the Inflight list like every other URBs.
     * For now we allocate each URB which gets queued into the respective Endpoint during Xfer.
     */
    vboxusb_urb_t *pUrb = RTMemAllocZ(sizeof(vboxusb_urb_t));
    if (RT_LIKELY(pUrb))
    {
        pUrb->enmState = VBOXUSB_URB_STATE_INFLIGHT;
        pUrb->pState = pState;

        if (RT_LIKELY(pUrbReq))
        {
            pUrb->pvUrbR3 = pUrbReq->pvUrbR3;
            pUrb->bEndpoint = pUrbReq->bEndpoint;
            pUrb->enmType = pUrbReq->enmType;
            pUrb->enmDir = pUrbReq->enmDir;
            pUrb->enmStatus = pUrbReq->enmStatus;
            pUrb->cbDataR3 = pUrbReq->cbData;
            pUrb->pvDataR3 = (RTR3PTR)pUrbReq->pvData;
            pUrb->cIsocPkts = pUrbReq->cIsocPkts;

            for (unsigned i = 0; i < pUrbReq->cIsocPkts; i++)
                pUrb->aIsocPkts[i].cbPkt = pUrbReq->aIsocPkts[i].cbPkt;

            pUrb->pMsg = NULL;
        }
    }
    else
        LogRel((DEVICE_NAME ": vboxUsbSolarisGetIsocInUrb: Failed to alloc %d bytes\n", sizeof(vboxusb_urb_t)));
    return pUrb;
}


/**
 * Queues a URB reusing previously allocated URBs as required.
 *
 * @param   pState          The USB device instance.
 * @param   pUrbReq         Opaque pointer to the complete request.
 * @param   pMsg            Pointer to the allocated request data.
 *
 * @returns The allocated URB to be used, or NULL upon failure.
 */
LOCAL vboxusb_urb_t *vboxUsbSolarisQueueUrb(vboxusb_state_t *pState, PVBOXUSBREQ_URB pUrbReq, mblk_t *pMsg)
{
    Assert(pUrbReq);
    LogFunc((DEVICE_NAME ": vboxUsbSolarisQueueUrb: pState=%p pUrbReq=%p\n", pState, pUrbReq));

    mutex_enter(&pState->Mtx);

    /*
     * Grab a URB from the free list.
     */
    vboxusb_urb_t *pUrb = list_remove_head(&pState->hFreeUrbs);
    if (pUrb)
    {
        Assert(pUrb->enmState == VBOXUSB_URB_STATE_FREE);
        Assert(!pUrb->pMsg);
        Assert(pState->cFreeUrbs > 0);
        --pState->cFreeUrbs;
    }
    else
    {
        /*
         * We can't discard "old" URBs. For instance, INTR IN URBs that don't complete as
         * they don't have a timeout can essentially take arbitrarily long to complete depending
         * on the device and it's not safe to discard them in case they -do- complete. However,
         * we also have to reasonably assume a device doesn't have too many pending URBs always.
         *
         * Thus we just use a large queue and simply refuse further transfers. This is not
         * a situation which normally ever happens as usually there are at most than 4 or 5 URBs
         * in-flight until we reap them.
         */
        uint32_t const cTotalUrbs = pState->cInflightUrbs + pState->cFreeUrbs + pState->cLandedUrbs;
        if (cTotalUrbs >= VBOXUSB_URB_QUEUE_SIZE)
        {
            mutex_exit(&pState->Mtx);
            LogRelMax(5, (DEVICE_NAME ": vboxUsbSolarisQueueUrb: Max queue size %u reached, refusing further transfers",
                          cTotalUrbs));
            return NULL;
        }

        /*
         * Allocate a new URB as we have no free URBs.
         */
        mutex_exit(&pState->Mtx);
        pUrb = RTMemAllocZ(sizeof(vboxusb_urb_t));
        if (RT_UNLIKELY(!pUrb))
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisQueueUrb: Failed to alloc %d bytes\n", sizeof(vboxusb_urb_t)));
            return NULL;
        }
        mutex_enter(&pState->Mtx);
    }

    /*
     * Add the URB to the inflight list.
     */
    list_insert_tail(&pState->hInflightUrbs, pUrb);
    ++pState->cInflightUrbs;

    Assert(!pUrb->pMsg);
    pUrb->pMsg      = pMsg;
    pUrb->pState    = pState;
    pUrb->enmState  = VBOXUSB_URB_STATE_INFLIGHT;
    pUrb->pvUrbR3   = pUrbReq->pvUrbR3;
    pUrb->bEndpoint = pUrbReq->bEndpoint;
    pUrb->enmType   = pUrbReq->enmType;
    pUrb->enmDir    = pUrbReq->enmDir;
    pUrb->enmStatus = pUrbReq->enmStatus;
    pUrb->fShortOk  = pUrbReq->fShortOk;
    pUrb->pvDataR3  = (RTR3PTR)pUrbReq->pvData;
    pUrb->cbDataR3  = pUrbReq->cbData;
    pUrb->cIsocPkts = pUrbReq->cIsocPkts;
    if (pUrbReq->enmType == VUSBXFERTYPE_ISOC)
    {
        for (unsigned i = 0; i < pUrbReq->cIsocPkts; i++)
            pUrb->aIsocPkts[i].cbPkt = pUrbReq->aIsocPkts[i].cbPkt;
    }

    mutex_exit(&pState->Mtx);
    return pUrb;
}


/**
 * Dequeues a completed URB into the landed list and informs user-land.
 *
 * @param   pUrb                The URB to move.
 * @param   URBStatus           The Solaris URB completion code.
 *
 * @remarks All pipes could be closed at this point (e.g. Device disconnected during inflight URBs)
 */
LOCAL void vboxUsbSolarisDeQueueUrb(vboxusb_urb_t *pUrb, int URBStatus)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisDeQueue: pUrb=%p\n", pUrb));
    AssertPtrReturnVoid(pUrb);

    pUrb->enmStatus = vboxUsbSolarisGetUrbStatus(URBStatus);
    if (pUrb->enmStatus != VUSBSTATUS_OK)
        Log((DEVICE_NAME ": vboxUsbSolarisDeQueueUrb: URB failed! URBStatus=%d bEndpoint=%#x\n", URBStatus, pUrb->bEndpoint));

    vboxusb_state_t *pState = pUrb->pState;
    if (RT_LIKELY(pState))
    {
        mutex_enter(&pState->Mtx);
        pUrb->enmState = VBOXUSB_URB_STATE_LANDED;

        /*
         * Remove it from the inflight list & move it to the landed list.
         */
        list_remove(&pState->hInflightUrbs, pUrb);
        Assert(pState->cInflightUrbs > 0);
        --pState->cInflightUrbs;

        list_insert_tail(&pState->hLandedUrbs, pUrb);
        ++pState->cLandedUrbs;

        vboxUsbSolarisNotifyComplete(pUrb->pState);
        mutex_exit(&pState->Mtx);
        return;
    }

    /* Well, let's at least not leak memory... */
    freemsg(pUrb->pMsg);
    pUrb->pMsg = NULL;
    pUrb->enmStatus = VUSBSTATUS_INVALID;

    LogRel((DEVICE_NAME ": vboxUsbSolarisDeQueue: State Gone\n"));
}


/**
 * Concatenates a chain message block into a single message block if possible.
 *
 * @param   pUrb                The URB to move.
 */
LOCAL void vboxUsbSolarisConcatMsg(vboxusb_urb_t *pUrb)
{
    /*
     * Concatenate the whole message rather than doing a chained copy while reaping.
     */
    if (   pUrb->pMsg
        && pUrb->pMsg->b_cont)
    {
        mblk_t *pFullMsg = msgpullup(pUrb->pMsg, -1 /* all data */);
        if (RT_LIKELY(pFullMsg))
        {
            freemsg(pUrb->pMsg);
            pUrb->pMsg = pFullMsg;
        }
        else
            LogRel((DEVICE_NAME ": vboxUsbSolarisConcatMsg: Failed. Expect glitches due to truncated data!\n"));
    }
}


/**
 * Wakes up a user process signalling URB completion.
 *
 * @param   pState          The USB device instance.
 * @remarks Requires the device state mutex to be held.
 */
LOCAL void vboxUsbSolarisNotifyComplete(vboxusb_state_t *pState)
{
    if (pState->fPollPending)
    {
        pollhead_t *pPollHead = &pState->PollHead;
        pState->fPollPending = false;
        mutex_exit(&pState->Mtx);
        pollwakeup(pPollHead, POLLIN);
        mutex_enter(&pState->Mtx);
    }
}


/**
 * Wakes up a user process signalling a device unplug events.
 *
 * @param   pState          The USB device instance.
 * @remarks Requires the device state mutex to be held.
 */
LOCAL void vboxUsbSolarisNotifyUnplug(vboxusb_state_t *pState)
{
    if (pState->fPollPending)
    {
        pollhead_t *pPollHead = &pState->PollHead;
        pState->fPollPending = false;
        mutex_exit(&pState->Mtx);
        pollwakeup(pPollHead, POLLHUP);
        mutex_enter(&pState->Mtx);
    }
}


/**
 * Performs a Control Xfer.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint for the Xfer.
 * @param   pUrb            The VBox USB URB.
 *
 * @returns VBox status code.
 */
LOCAL int vboxUsbSolarisCtrlXfer(vboxusb_state_t *pState, vboxusb_ep_t *pEp, vboxusb_urb_t *pUrb)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisCtrlXfer: pState=%p pEp=%p pUrb=%p enmDir=%d cbData=%d\n", pState, pEp, pUrb,
             pUrb->enmDir, pUrb->cbDataR3));

    AssertPtrReturn(pUrb->pMsg, VERR_INVALID_PARAMETER);
    const size_t cbData = pUrb->cbDataR3 > VBOXUSB_CTRL_XFER_SIZE ? pUrb->cbDataR3 - VBOXUSB_CTRL_XFER_SIZE : 0;

    /*
     * Allocate a wrapper request.
     */
    usb_ctrl_req_t *pReq = usb_alloc_ctrl_req(pState->pDip, cbData, USB_FLAGS_SLEEP);
    if (RT_LIKELY(pReq))
    {
        uchar_t *pSetupData = pUrb->pMsg->b_rptr;

        /*
         * Initialize the Ctrl Xfer Header.
         */
        pReq->ctrl_bmRequestType  = pSetupData[0];
        pReq->ctrl_bRequest       = pSetupData[1];
        pReq->ctrl_wValue         = (pSetupData[3] << VBOXUSB_CTRL_XFER_SIZE) | pSetupData[2];
        pReq->ctrl_wIndex         = (pSetupData[5] << VBOXUSB_CTRL_XFER_SIZE) | pSetupData[4];
        pReq->ctrl_wLength        = (pSetupData[7] << VBOXUSB_CTRL_XFER_SIZE) | pSetupData[6];

        if (   pUrb->enmDir == VUSBDIRECTION_OUT
            && cbData)
        {
            bcopy(pSetupData + VBOXUSB_CTRL_XFER_SIZE, pReq->ctrl_data->b_wptr, cbData);
            pReq->ctrl_data->b_wptr += cbData;
        }

        freemsg(pUrb->pMsg);
        pUrb->pMsg = NULL;

        /*
         * Initialize callbacks and timeouts.
         */
        pReq->ctrl_cb             = vboxUsbSolarisCtrlXferCompleted;
        pReq->ctrl_exc_cb         = vboxUsbSolarisCtrlXferCompleted;
        pReq->ctrl_timeout        = VBOXUSB_CTRL_XFER_TIMEOUT;
        pReq->ctrl_attributes     = USB_ATTRS_AUTOCLEARING | USB_ATTRS_SHORT_XFER_OK;
        pReq->ctrl_client_private = (usb_opaque_t)pUrb;

        /*
         * Submit the request.
         */
        int rc = usb_pipe_ctrl_xfer(pEp->pPipe, pReq, USB_FLAGS_NOSLEEP);
        if (RT_LIKELY(rc == USB_SUCCESS))
            return VINF_SUCCESS;

        LogRel((DEVICE_NAME ": vboxUsbSolarisCtrlXfer: Request failed! bEndpoint=%#x rc=%d\n", pUrb->bEndpoint, rc));

        usb_free_ctrl_req(pReq);
        return VERR_PIPE_IO_ERROR;
    }

    LogRel((DEVICE_NAME ": vboxUsbSolarisCtrlXfer: Failed to alloc request for %u bytes\n", cbData));
    return VERR_NO_MEMORY;
}


/**
 * Completion/Exception callback for Control Xfers.
 *
 * @param   pPipe            The Ctrl pipe handle.
 * @param   pReq             The Ctrl request.
 */
LOCAL void vboxUsbSolarisCtrlXferCompleted(usb_pipe_handle_t pPipe, usb_ctrl_req_t *pReq)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisCtrlXferCompleted: pPipe=%p pReq=%p\n", pPipe, pReq));
    Assert(pReq);
    Assert(!(pReq->ctrl_cb_flags & USB_CB_INTR_CONTEXT));

    vboxusb_urb_t *pUrb   = (vboxusb_urb_t *)pReq->ctrl_client_private;
    if (RT_LIKELY(pUrb))
    {
        /*
         * Funky stuff: We need to reconstruct the header for control transfers.
         * Let us chain along the data and concatenate the entire message.
         */
        mblk_t *pSetupMsg = allocb(sizeof(VUSBSETUP), BPRI_MED);
        if (RT_LIKELY(pSetupMsg))
        {
            VUSBSETUP SetupData;
            SetupData.bmRequestType = pReq->ctrl_bmRequestType;
            SetupData.bRequest      = pReq->ctrl_bRequest;
            SetupData.wValue        = pReq->ctrl_wValue;
            SetupData.wIndex        = pReq->ctrl_wIndex;
            SetupData.wLength       = pReq->ctrl_wLength;

            bcopy(&SetupData, pSetupMsg->b_wptr, sizeof(VUSBSETUP));
            pSetupMsg->b_wptr += sizeof(VUSBSETUP);

            /*
             * Should be safe to update pMsg here without the state mutex as typically nobody else
             * touches this URB in the inflight list.
             *
             * The reason we choose to use vboxUsbSolarisConcatMsg here is that we don't assume the
             * message returned by Solaris is one contiguous chunk in 'pMsg->b_rptr'.
             */
            Assert(!pUrb->pMsg);
            pUrb->pMsg = pSetupMsg;
            pUrb->pMsg->b_cont = pReq->ctrl_data;
            pReq->ctrl_data = NULL;
            vboxUsbSolarisConcatMsg(pUrb);
        }
        else
            LogRel((DEVICE_NAME ": vboxUsbSolarisCtrlXferCompleted: Failed to alloc %u bytes for header\n", sizeof(VUSBSETUP)));

        /*
         * Update the URB and move to landed list for reaping.
         */
        vboxUsbSolarisDeQueueUrb(pUrb, pReq->ctrl_completion_reason);
    }
    else
        LogRel((DEVICE_NAME ": vboxUsbSolarisCtrlXferCompleted: Extreme error! missing private data\n"));

    usb_free_ctrl_req(pReq);
}


/**
 * Performs a Bulk Xfer.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint for the Xfer.
 * @param   pUrb            The VBox USB URB.
 *
 * @returns VBox status code.
 * @remarks Any errors, the caller should free pUrb->pMsg.
 */
LOCAL int vboxUsbSolarisBulkXfer(vboxusb_state_t *pState, vboxusb_ep_t *pEp, vboxusb_urb_t *pUrb)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisBulkXfer: pState=%p pEp=%p pUrb=%p enmDir=%d cbData=%d\n", pState, pEp, pUrb,
             pUrb->enmDir, pUrb->cbDataR3));

    /*
     * Allocate a wrapper request.
     */
    size_t const cbAlloc = pUrb->enmDir == VUSBDIRECTION_IN ? pUrb->cbDataR3 : 0;
    usb_bulk_req_t *pReq = usb_alloc_bulk_req(pState->pDip, cbAlloc, USB_FLAGS_SLEEP);
    if (RT_LIKELY(pReq))
    {
        /*
         * Initialize Bulk Xfer, callbacks and timeouts.
         */
        usb_req_attrs_t fAttributes = USB_ATTRS_AUTOCLEARING;
        if (pUrb->enmDir == VUSBDIRECTION_OUT)
        {
            pReq->bulk_data = pUrb->pMsg;
            pUrb->pMsg = NULL;
        }
        else if (   pUrb->enmDir == VUSBDIRECTION_IN
                 && pUrb->fShortOk)
        {
            fAttributes |= USB_ATTRS_SHORT_XFER_OK;
        }

        Assert(!pUrb->pMsg);
        pReq->bulk_len            = pUrb->cbDataR3;
        pReq->bulk_cb             = vboxUsbSolarisBulkXferCompleted;
        pReq->bulk_exc_cb         = vboxUsbSolarisBulkXferCompleted;
        pReq->bulk_timeout        = 0;
        pReq->bulk_attributes     = fAttributes;
        pReq->bulk_client_private = (usb_opaque_t)pUrb;

        /* Don't obtain state lock here, we're just reading unchanging data... */
        if (RT_UNLIKELY(pUrb->cbDataR3 > pState->cbMaxBulkXfer))
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisBulkXfer: Requesting %d bytes when only %d bytes supported by device\n",
                        pUrb->cbDataR3, pState->cbMaxBulkXfer));
        }

        /*
         * Submit the request.
         */
        int rc = usb_pipe_bulk_xfer(pEp->pPipe, pReq, USB_FLAGS_NOSLEEP);
        if (RT_LIKELY(rc == USB_SUCCESS))
            return VINF_SUCCESS;

        LogRel((DEVICE_NAME ": vboxUsbSolarisBulkXfer: Request failed! Ep=%#x rc=%d cbData=%u\n", pUrb->bEndpoint, rc,
                pReq->bulk_len));

        usb_free_bulk_req(pReq);
        return VERR_PIPE_IO_ERROR;
    }

    LogRel((DEVICE_NAME ": vboxUsbSolarisBulkXfer: Failed to alloc bulk request\n"));
    return VERR_NO_MEMORY;
}


/**
 * Completion/Exception callback for Bulk Xfers.
 *
 * @param   pPipe           The Bulk pipe handle.
 * @param   pReq            The Bulk request.
 */
LOCAL void vboxUsbSolarisBulkXferCompleted(usb_pipe_handle_t pPipe, usb_bulk_req_t *pReq)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisBulkXferCompleted: pPipe=%p pReq=%p\n", pPipe, pReq));

    Assert(pReq);
    Assert(!(pReq->bulk_cb_flags & USB_CB_INTR_CONTEXT));

    vboxusb_ep_t *pEp = (vboxusb_ep_t *)usb_pipe_get_private(pPipe);
    if (RT_LIKELY(pEp))
    {
        vboxusb_urb_t *pUrb = (vboxusb_urb_t *)pReq->bulk_client_private;
        if (RT_LIKELY(pUrb))
        {
            Assert(!pUrb->pMsg);
            if (   pUrb->enmDir == VUSBDIRECTION_IN
                && pReq->bulk_data)
            {
                pUrb->pMsg = pReq->bulk_data;
                pReq->bulk_data = NULL;
                vboxUsbSolarisConcatMsg(pUrb);
            }

            /*
             * Update the URB and move to tail for reaping.
             */
            vboxUsbSolarisDeQueueUrb(pUrb, pReq->bulk_completion_reason);
        }
        else
            LogRel((DEVICE_NAME ": vboxUsbSolarisBulkXferCompleted: Extreme error! private request data missing!\n"));
    }
    else
        Log((DEVICE_NAME ": vboxUsbSolarisBulkXferCompleted: Pipe Gone!\n"));

    usb_free_bulk_req(pReq);
}


/**
 * Performs an Interrupt Xfer.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint for the Xfer.
 * @param   pUrb            The VBox USB URB.
 *
 * @returns VBox status code.
 * @remarks Any errors, the caller should free pUrb->pMsg.
 */
LOCAL int vboxUsbSolarisIntrXfer(vboxusb_state_t *pState, vboxusb_ep_t *pEp, vboxusb_urb_t *pUrb)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisIntrXfer: pState=%p pEp=%p pUrb=%p enmDir=%d cbData=%d\n", pState, pEp, pUrb,
             pUrb->enmDir, pUrb->cbDataR3));

    usb_intr_req_t *pReq = usb_alloc_intr_req(pState->pDip, 0 /* length */, USB_FLAGS_SLEEP);
    if (RT_LIKELY(pReq))
    {
        /*
         * Initialize Intr Xfer, callbacks & timeouts.
         */
        usb_req_attrs_t fAttributes = USB_ATTRS_AUTOCLEARING;
        if (pUrb->enmDir == VUSBDIRECTION_OUT)
        {
            pReq->intr_data = pUrb->pMsg;
            pUrb->pMsg = NULL;
        }
        else
        {
            Assert(pUrb->enmDir == VUSBDIRECTION_IN);
            fAttributes |= USB_ATTRS_ONE_XFER;
            if (pUrb->fShortOk)
                fAttributes |= USB_ATTRS_SHORT_XFER_OK;
        }

        Assert(!pUrb->pMsg);
        pReq->intr_len            = pUrb->cbDataR3; /* Not pEp->EpDesc.wMaxPacketSize */
        pReq->intr_cb             = vboxUsbSolarisIntrXferCompleted;
        pReq->intr_exc_cb         = vboxUsbSolarisIntrXferCompleted;
        pReq->intr_timeout        = 0;
        pReq->intr_attributes     = fAttributes;
        pReq->intr_client_private = (usb_opaque_t)pUrb;

        /*
         * Submit the request.
         */
        int rc = usb_pipe_intr_xfer(pEp->pPipe, pReq, USB_FLAGS_NOSLEEP);
        if (RT_LIKELY(rc == USB_SUCCESS))
            return VINF_SUCCESS;

        LogRel((DEVICE_NAME ": vboxUsbSolarisIntrXfer: usb_pipe_intr_xfer failed! rc=%d bEndpoint=%#x\n", rc, pUrb->bEndpoint));

        usb_free_intr_req(pReq);
        return VERR_PIPE_IO_ERROR;
    }

    LogRel((DEVICE_NAME ": vboxUsbSolarisIntrXfer: Failed to alloc intr request\n"));
    return VERR_NO_MEMORY;
}


/**
 * Completion/Exception callback for Intr Xfers.
 *
 * @param   pPipe           The Intr pipe handle.
 * @param   pReq            The Intr request.
 */
LOCAL void vboxUsbSolarisIntrXferCompleted(usb_pipe_handle_t pPipe, usb_intr_req_t *pReq)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisIntrXferCompleted: pPipe=%p pReq=%p\n", pPipe, pReq));

    Assert(pReq);
    Assert(!(pReq->intr_cb_flags & USB_CB_INTR_CONTEXT));

    vboxusb_urb_t *pUrb = (vboxusb_urb_t *)pReq->intr_client_private;
    if (RT_LIKELY(pUrb))
    {
        if (   pUrb->enmDir == VUSBDIRECTION_IN
            && pReq->intr_data)
        {
            pUrb->pMsg = pReq->intr_data;
            pReq->intr_data = NULL;
            vboxUsbSolarisConcatMsg(pUrb);
        }

        /*
         * Update the URB and move to landed list for reaping.
         */
        vboxUsbSolarisDeQueueUrb(pUrb, pReq->intr_completion_reason);
    }
    else
        LogRel((DEVICE_NAME ": vboxUsbSolarisIntrXferCompleted: Extreme error! private request data missing\n"));

    usb_free_intr_req(pReq);
}


/**
 * Performs an Isochronous Xfer.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint for the Xfer.
 * @param   pUrb            The VBox USB URB.
 *
 * @returns VBox status code.
 * @remarks Any errors, the caller should free pUrb->pMsg.
 */
LOCAL int vboxUsbSolarisIsocXfer(vboxusb_state_t *pState, vboxusb_ep_t *pEp, vboxusb_urb_t *pUrb)
{
    /* LogFunc((DEVICE_NAME ": vboxUsbSolarisIsocXfer: pState=%p pEp=%p pUrb=%p\n", pState, pEp, pUrb)); */

    /*
     * For Isoc. IN transfers we perform one request and USBA polls the device continuously
     * and supplies our Xfer callback with input data. We cannot perform one-shot Isoc. In transfers.
     */
    size_t cbData = (pUrb->enmDir == VUSBDIRECTION_IN ? pUrb->cIsocPkts * pUrb->aIsocPkts[0].cbPkt : 0);
    if (pUrb->enmDir == VUSBDIRECTION_IN)
    {
        Log((DEVICE_NAME ": vboxUsbSolarisIsocXfer: Isoc. IN - Queueing\n"));

        mutex_enter(&pState->Mtx);
        if (pEp->fIsocPolling)
        {
            /*
             * Queue a maximum of cbMaxIsocData bytes, else fail.
             */
            if (pEp->cbIsocInLandedReqs + cbData > pEp->cbMaxIsocData)
            {
                mutex_exit(&pState->Mtx);
                Log((DEVICE_NAME ": vboxUsbSolarisIsocXfer: Max Isoc. data %d bytes queued\n", pEp->cbMaxIsocData));
                return VERR_TOO_MUCH_DATA;
            }

            list_insert_tail(&pEp->hIsocInUrbs, pUrb);
            ++pEp->cIsocInUrbs;

            mutex_exit(&pState->Mtx);
            return VINF_SUCCESS;
        }
        mutex_exit(&pState->Mtx);
    }

    int rc = VINF_SUCCESS;
    usb_isoc_req_t *pReq = usb_alloc_isoc_req(pState->pDip, pUrb->cIsocPkts, cbData, USB_FLAGS_NOSLEEP);
    Log((DEVICE_NAME ": vboxUsbSolarisIsocXfer: enmDir=%#x cIsocPkts=%d aIsocPkts[0]=%d cbDataR3=%d\n", pUrb->enmDir,
                    pUrb->cIsocPkts, pUrb->aIsocPkts[0].cbPkt, pUrb->cbDataR3));
    if (RT_LIKELY(pReq))
    {
        /*
         * Initialize Isoc Xfer, callbacks & timeouts.
         */
        for (unsigned i = 0; i < pUrb->cIsocPkts; i++)
            pReq->isoc_pkt_descr[i].isoc_pkt_length = pUrb->aIsocPkts[i].cbPkt;

        if (pUrb->enmDir == VUSBDIRECTION_OUT)
        {
            pReq->isoc_data           = pUrb->pMsg;
            pReq->isoc_attributes     = USB_ATTRS_AUTOCLEARING | USB_ATTRS_ISOC_XFER_ASAP;
            pReq->isoc_cb             = vboxUsbSolarisIsocOutXferCompleted;
            pReq->isoc_exc_cb         = vboxUsbSolarisIsocOutXferCompleted;
            pReq->isoc_client_private = (usb_opaque_t)pUrb;
        }
        else
        {
            pReq->isoc_attributes     = USB_ATTRS_AUTOCLEARING | USB_ATTRS_ISOC_XFER_ASAP | USB_ATTRS_SHORT_XFER_OK;
            pReq->isoc_cb             = vboxUsbSolarisIsocInXferCompleted;
            pReq->isoc_exc_cb         = vboxUsbSolarisIsocInXferError;
            pReq->isoc_client_private = (usb_opaque_t)pState;
        }
        pReq->isoc_pkts_count         = pUrb->cIsocPkts;
        pReq->isoc_pkts_length        = 0;  /* auto compute */

        /*
         * Submit the request.
         */
        rc = usb_pipe_isoc_xfer(pEp->pPipe, pReq, USB_FLAGS_NOSLEEP);
        if (RT_LIKELY(rc == USB_SUCCESS))
        {
            if (pUrb->enmDir == VUSBDIRECTION_IN)
            {
                /*
                 * Add the first Isoc. IN URB to the queue as well.
                 */
                mutex_enter(&pState->Mtx);
                list_insert_tail(&pEp->hIsocInUrbs, pUrb);
                ++pEp->cIsocInUrbs;
                pEp->fIsocPolling = true;
                mutex_exit(&pState->Mtx);
            }

            return VINF_SUCCESS;
        }
        else
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisIsocXfer: usb_pipe_isoc_xfer failed! rc=%d\n", rc));
            rc = VERR_PIPE_IO_ERROR;

            if (pUrb->enmDir == VUSBDIRECTION_IN)
            {
                mutex_enter(&pState->Mtx);
                vboxusb_urb_t *pIsocFailedUrb = list_remove_tail(&pEp->hIsocInUrbs);
                if (pIsocFailedUrb)
                {
                    RTMemFree(pIsocFailedUrb);
                    --pEp->cIsocInUrbs;
                }
                pEp->fIsocPolling = false;
                mutex_exit(&pState->Mtx);
            }
        }

        if (pUrb->enmDir == VUSBDIRECTION_OUT)
        {
            freemsg(pUrb->pMsg);
            pUrb->pMsg = NULL;
        }

        usb_free_isoc_req(pReq);
    }
    else
    {
        LogRel((DEVICE_NAME ": vboxUsbSolarisIsocXfer: Failed to alloc isoc req for %d packets\n", pUrb->cIsocPkts));
        rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * Completion/Exception callback for Isoc IN Xfers.
 *
 * @param   pPipe           The Intr pipe handle.
 * @param   pReq            The Intr request.
 *
 * @remarks Completion callback executes in interrupt context!
 */
LOCAL void vboxUsbSolarisIsocInXferCompleted(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq)
{
    /* LogFunc((DEVICE_NAME ": vboxUsbSolarisIsocInXferCompleted: pPipe=%p pReq=%p\n", pPipe, pReq)); */

    vboxusb_state_t *pState = (vboxusb_state_t *)pReq->isoc_client_private;
    if (RT_LIKELY(pState))
    {
        vboxusb_ep_t *pEp = (vboxusb_ep_t *)usb_pipe_get_private(pPipe);
        if (   pEp
            && pEp->pPipe)
        {
#if 0
            /*
             * Stop polling if all packets failed.
             */
            if (pReq->isoc_error_count == pReq->isoc_pkts_count)
            {
                Log((DEVICE_NAME ": vboxUsbSolarisIsocInXferCompleted: Stopping polling! Too many errors\n"));
                mutex_exit(&pState->Mtx);
                usb_pipe_stop_isoc_polling(pPipe, USB_FLAGS_NOSLEEP);
                mutex_enter(&pState->Mtx);
                pEp->fIsocPolling = false;
            }
#endif

            /** @todo Query and verify this at runtime. */
            AssertCompile(sizeof(VUSBISOC_PKT_DESC) == sizeof(usb_isoc_pkt_descr_t));
            if (RT_LIKELY(pReq->isoc_data))
            {
                Log((DEVICE_NAME ": vboxUsbSolarisIsocInXferCompleted: cIsocInUrbs=%d cbIsocInLandedReqs=%d\n", pEp->cIsocInUrbs,
                     pEp->cbIsocInLandedReqs));

                mutex_enter(&pState->Mtx);

                /*
                 * If there are waiting URBs, satisfy the oldest one.
                 */
                if (   pEp->cIsocInUrbs > 0
                    && pEp->cbIsocInLandedReqs == 0)
                {
                    vboxusb_urb_t *pUrb = list_remove_head(&pEp->hIsocInUrbs);
                    if (RT_LIKELY(pUrb))
                    {
                        --pEp->cIsocInUrbs;
                        mutex_exit(&pState->Mtx);

                        for (unsigned i = 0; i < pReq->isoc_pkts_count; i++)
                        {
                            pUrb->aIsocPkts[i].cbActPkt = pReq->isoc_pkt_descr[i].isoc_pkt_actual_length;
                            pUrb->aIsocPkts[i].enmStatus = vboxUsbSolarisGetUrbStatus(pReq->isoc_pkt_descr[i].isoc_pkt_status);
                        }

                        pUrb->pMsg = pReq->isoc_data;
                        pReq->isoc_data = NULL;

                        /*
                         * Move to landed list
                         */
                        mutex_enter(&pState->Mtx);
                        list_insert_tail(&pState->hLandedUrbs, pUrb);
                        ++pState->cLandedUrbs;
                        vboxUsbSolarisNotifyComplete(pState);
                    }
                    else
                    {
                        /* Huh!? cIsocInUrbs is wrong then! Should never happen unless we decide to decrement cIsocInUrbs in
                           Reap time */
                        pEp->cIsocInUrbs = 0;
                        LogRel((DEVICE_NAME ": vboxUsbSolarisIsocInXferCompleted: Extreme error! Isoc. counter borked!\n"));
                    }

                    mutex_exit(&pState->Mtx);
                    usb_free_isoc_req(pReq);
                    return;
                }

                mutex_exit(&pState->Mtx);
            }
            else
                LogRel((DEVICE_NAME ": vboxUsbSolarisIsocInXferCompleted: Data missing\n"));
        }
        else
            LogRel((DEVICE_NAME ": vboxUsbSolarisIsocInXferCompleted: Pipe Gone\n"));
    }
    else
        Log((DEVICE_NAME ": vboxUsbSolarisIsocInXferCompleted: State Gone\n"));

    usb_free_isoc_req(pReq);
}


/**
 * Exception callback for Isoc IN Xfers.
 *
 * @param   pPipe           The Intr pipe handle.
 * @param   pReq            The Intr request.
 * @remarks Completion callback executes in interrupt context!
 */
LOCAL void vboxUsbSolarisIsocInXferError(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisIsocInXferError: pPipe=%p pReq=%p\n", pPipe, pReq));

    vboxusb_state_t *pState = (vboxusb_state_t *)pReq->isoc_client_private;
    if (RT_UNLIKELY(!pState))
    {
        Log((DEVICE_NAME ": vboxUsbSolarisIsocInXferError: State Gone\n"));
        usb_free_isoc_req(pReq);
        return;
    }

    mutex_enter(&pState->Mtx);
    vboxusb_ep_t *pEp = (vboxusb_ep_t *)usb_pipe_get_private(pPipe);
    if (RT_UNLIKELY(!pEp))
    {
        Log((DEVICE_NAME ": vboxUsbSolarisIsocInXferError: Pipe Gone\n"));
        mutex_exit(&pState->Mtx);
        usb_free_isoc_req(pReq);
        return;
    }

    switch(pReq->isoc_completion_reason)
    {
        case USB_CR_NO_RESOURCES:
        {
            /*
             * Resubmit the request in case the original request did not complete due to
             * immediately unavailable requests
             */
            mutex_exit(&pState->Mtx);
            usb_pipe_isoc_xfer(pPipe, pReq, USB_FLAGS_NOSLEEP);
            Log((DEVICE_NAME ": vboxUsbSolarisIsocInXferError: Resubmitted Isoc. IN request due to unavailable resources\n"));
            return;
        }

        case USB_CR_PIPE_CLOSING:
        case USB_CR_STOPPED_POLLING:
        case USB_CR_PIPE_RESET:
        {
            pEp->fIsocPolling = false;
            usb_free_isoc_req(pReq);
            break;
        }

        default:
        {
            Log((DEVICE_NAME ": vboxUsbSolarisIsocInXferError: Stopping Isoc. IN polling due to rc=%d\n",
                 pReq->isoc_completion_reason));
            pEp->fIsocPolling = false;
            mutex_exit(&pState->Mtx);
            usb_pipe_stop_isoc_polling(pPipe, USB_FLAGS_NOSLEEP);
            usb_free_isoc_req(pReq);
            mutex_enter(&pState->Mtx);
            break;
        }
    }

    /*
     * Dequeue i.e. delete the last queued Isoc In. URB. as failed.
     */
    vboxusb_urb_t *pUrb = list_remove_tail(&pEp->hIsocInUrbs);
    if (pUrb)
    {
        --pEp->cIsocInUrbs;
        Log((DEVICE_NAME ": vboxUsbSolarisIsocInXferError: Deleting last queued URB as it failed\n"));
        freemsg(pUrb->pMsg);
        RTMemFree(pUrb);
        vboxUsbSolarisNotifyComplete(pState);
    }

    mutex_exit(&pState->Mtx);
}


/**
 * Completion/Exception callback for Isoc OUT Xfers.
 *
 * @param   pPipe           The Intr pipe handle.
 * @param   pReq            The Intr request.
 * @remarks Completion callback executes in interrupt context!
 */
LOCAL void vboxUsbSolarisIsocOutXferCompleted(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisIsocOutXferCompleted: pPipe=%p pReq=%p\n", pPipe, pReq));

    vboxusb_ep_t *pEp = (vboxusb_ep_t *)usb_pipe_get_private(pPipe);
    if (RT_LIKELY(pEp))
    {
        vboxusb_urb_t *pUrb = (vboxusb_urb_t *)pReq->isoc_client_private;
        if (RT_LIKELY(pUrb))
        {
            size_t cbActPkt = 0;
            for (int i = 0; i < pReq->isoc_pkts_count; i++)
            {
                cbActPkt += pReq->isoc_pkt_descr[i].isoc_pkt_actual_length;
                pUrb->aIsocPkts[i].cbActPkt = pReq->isoc_pkt_descr[i].isoc_pkt_actual_length;
                pUrb->aIsocPkts[i].enmStatus = vboxUsbSolarisGetUrbStatus(pReq->isoc_pkt_descr[i].isoc_pkt_status);
            }

            Log((DEVICE_NAME ": vboxUsbSolarisIsocOutXferCompleted: cIsocPkts=%d cbData=%d cbActPkt=%d\n", pUrb->cIsocPkts,
                 pUrb->cbDataR3, cbActPkt));

            if (pReq->isoc_completion_reason == USB_CR_OK)
            {
                if (RT_UNLIKELY(pUrb->pMsg != pReq->isoc_data))  /* Paranoia */
                {
                    freemsg(pUrb->pMsg);
                    pUrb->pMsg = pReq->isoc_data;
                }
            }
            pReq->isoc_data = NULL;

            pUrb->cIsocPkts = pReq->isoc_pkts_count;
            pUrb->cbDataR3 = cbActPkt;

            /*
             * Update the URB and move to landed list for reaping.
             */
            vboxUsbSolarisDeQueueUrb(pUrb, pReq->isoc_completion_reason);
        }
        else
            Log((DEVICE_NAME ": vboxUsbSolarisIsocOutXferCompleted: Missing private data!?! Dropping OUT pUrb\n"));
    }
    else
        Log((DEVICE_NAME ": vboxUsbSolarisIsocOutXferCompleted: Pipe Gone\n"));

    usb_free_isoc_req(pReq);
}


/**
 * Callback when the device gets disconnected.
 *
 * @param   pDip            The module structure instance.
 *
 * @returns Solaris USB error code.
 */
LOCAL int vboxUsbSolarisDeviceDisconnected(dev_info_t *pDip)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisDeviceDisconnected: pDip=%p\n", pDip));

    int instance = ddi_get_instance(pDip);
    vboxusb_state_t *pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, instance);

    if (RT_LIKELY(pState))
    {
        /*
         * Serialize access: exclusive access to the state.
         */
        usb_serialize_access(pState->StateMulti, USB_WAIT, 0);
        mutex_enter(&pState->Mtx);

        pState->DevState = USB_DEV_DISCONNECTED;

        vboxUsbSolarisCloseAllPipes(pState, true /* ControlPipe */);
        vboxUsbSolarisNotifyUnplug(pState);

        mutex_exit(&pState->Mtx);
        usb_release_access(pState->StateMulti);

        return USB_SUCCESS;
    }

    LogRel((DEVICE_NAME ": vboxUsbSolarisDeviceDisconnected: Failed to get device state!\n"));
    return USB_FAILURE;
}


/**
 * Callback when the device gets reconnected.
 *
 * @param   pDip            The module structure instance.
 *
 * @returns Solaris USB error code.
 */
LOCAL int vboxUsbSolarisDeviceReconnected(dev_info_t *pDip)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisDeviceReconnected: pDip=%p\n", pDip));

    int instance = ddi_get_instance(pDip);
    vboxusb_state_t *pState = ddi_get_soft_state(g_pVBoxUSBSolarisState, instance);

    if (RT_LIKELY(pState))
    {
        vboxUsbSolarisDeviceRestore(pState);
        return USB_SUCCESS;
    }

    LogRel((DEVICE_NAME ": vboxUsbSolarisDeviceReconnected: Failed to get device state!\n"));
    return USB_FAILURE;
}


/**
 * Restores device state after a reconnect or resume.
 *
 * @param   pState          The USB device instance.
 */
LOCAL void vboxUsbSolarisDeviceRestore(vboxusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisDeviceRestore: pState=%p\n", pState));
    AssertPtrReturnVoid(pState);

    /*
     * Raise device power.
     */
    vboxUsbSolarisPowerBusy(pState);
    int rc = pm_raise_power(pState->pDip, 0 /* component */, USB_DEV_OS_FULL_PWR);

    /*
     * Check if the same device is resumed/reconnected.
     */
    rc = usb_check_same_device(pState->pDip,
                                NULL,           /* log handle */
                                USB_LOG_L2,     /* log level */
                                -1,             /* log mask */
                                USB_CHK_ALL,    /* check level */
                                NULL);          /* device string */

    if (rc != USB_SUCCESS)
    {
        mutex_enter(&pState->Mtx);
        pState->DevState = USB_DEV_DISCONNECTED;
        mutex_exit(&pState->Mtx);

        /* Do we need to inform userland here? */
        vboxUsbSolarisPowerIdle(pState);
        Log((DEVICE_NAME ": vboxUsbSolarisDeviceRestore: Not the same device\n"));
        return;
    }

    /*
     * Serialize access to not race with other PM functions.
     */
    usb_serialize_access(pState->StateMulti, USB_WAIT, 0);

    mutex_enter(&pState->Mtx);
    if (pState->DevState == USB_DEV_DISCONNECTED)
        pState->DevState = USB_DEV_ONLINE;
    else if (pState->DevState == USB_DEV_SUSPENDED)
        pState->DevState = USB_DEV_ONLINE;

    mutex_exit(&pState->Mtx);
    usb_release_access(pState->StateMulti);

    vboxUsbSolarisPowerIdle(pState);
}


/**
 * Restores device state after a reconnect or resume.
 *
 * @param   pState          The USB device instance.
 *
 * @returns VBox status code.
 */
LOCAL int vboxUsbSolarisDeviceSuspend(vboxusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisDeviceSuspend: pState=%p\n", pState));

    int rc = VERR_VUSB_DEVICE_IS_SUSPENDED;
    mutex_enter(&pState->Mtx);

    switch (pState->DevState)
    {
        case USB_DEV_SUSPENDED:
        {
            LogRel((DEVICE_NAME ": vboxUsbSolarisDeviceSuspend: Invalid device state %d\n", pState->DevState));
            break;
        }

        case USB_DEV_ONLINE:
        case USB_DEV_DISCONNECTED:
        case USB_DEV_PWRED_DOWN:
        {
            int PreviousState = pState->DevState;
            pState->DevState = USB_DEV_DISCONNECTED;

            /** @todo this doesn't make sense when for e.g. an INTR IN URB with infinite
             *        timeout is pending on the device. Fix suspend logic later. */
            /*
             * Drain pending URBs.
             */
            for (int i = 0; i < VBOXUSB_DRAIN_TIME; i++)
            {
                if (pState->cInflightUrbs < 1)
                    break;

                mutex_exit(&pState->Mtx);
                delay(drv_usectohz(100000));
                mutex_enter(&pState->Mtx);
            }

            /*
             * Deny suspend if we still have pending URBs.
             */
            if (pState->cInflightUrbs > 0)
            {
                pState->DevState = PreviousState;
                LogRel((DEVICE_NAME ": Cannot suspend %s %s (Ident=%s), %d inflight URBs\n", pState->szMfg, pState->szProduct,
                        pState->ClientInfo.szDeviceIdent, pState->cInflightUrbs));

                mutex_exit(&pState->Mtx);
                return VERR_RESOURCE_BUSY;
            }

            pState->cInflightUrbs = 0;

            /*
             * Serialize access to not race with Open/Detach/Close and
             * Close all pipes including the default pipe.
             */
            mutex_exit(&pState->Mtx);
            usb_serialize_access(pState->StateMulti, USB_WAIT, 0);
            mutex_enter(&pState->Mtx);

            vboxUsbSolarisCloseAllPipes(pState, true /* default pipe */);
            vboxUsbSolarisNotifyUnplug(pState);

            mutex_exit(&pState->Mtx);
            usb_release_access(pState->StateMulti);

            LogRel((DEVICE_NAME ": Suspended %s %s (Ident=%s)\n", pState->szMfg, pState->szProduct,
                    pState->ClientInfo.szDeviceIdent));
            return VINF_SUCCESS;
        }
    }

    mutex_exit(&pState->Mtx);
    Log((DEVICE_NAME ": vboxUsbSolarisDeviceSuspend: Returns %d\n", rc));
    return rc;
}


/**
 * Restores device state after a reconnect or resume.
 *
 * @param   pState          The USB device instance.
 */
LOCAL void vboxUsbSolarisDeviceResume(vboxusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisDeviceResume: pState=%p\n", pState));
    return vboxUsbSolarisDeviceRestore(pState);
}


/**
 * Flags the PM component as busy so the system will not manage it's power.
 *
 * @param   pState          The USB device instance.
 */
LOCAL void vboxUsbSolarisPowerBusy(vboxusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisPowerBusy: pState=%p\n", pState));
    AssertPtrReturnVoid(pState);

    mutex_enter(&pState->Mtx);
    if (pState->pPower)
    {
        pState->pPower->PowerBusy++;
        mutex_exit(&pState->Mtx);

        int rc = pm_busy_component(pState->pDip, 0 /* component */);
        if (rc != DDI_SUCCESS)
        {
            Log((DEVICE_NAME ": vboxUsbSolarisPowerBusy: Busy component failed! rc=%d\n", rc));
            mutex_enter(&pState->Mtx);
            pState->pPower->PowerBusy--;
            mutex_exit(&pState->Mtx);
        }
    }
    else
        mutex_exit(&pState->Mtx);
}


/**
 * Flags the PM component as idle so its power managed by the system.
 *
 * @param   pState          The USB device instance.
 */
LOCAL void vboxUsbSolarisPowerIdle(vboxusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ": vboxUsbSolarisPowerIdle: pState=%p\n", pState));
    AssertPtrReturnVoid(pState);

    if (pState->pPower)
    {
        int rc = pm_idle_component(pState->pDip, 0 /* component */);
        if (rc == DDI_SUCCESS)
        {
            mutex_enter(&pState->Mtx);
            Assert(pState->pPower->PowerBusy > 0);
            pState->pPower->PowerBusy--;
            mutex_exit(&pState->Mtx);
        }
        else
            Log((DEVICE_NAME ": vboxUsbSolarisPowerIdle: Idle component failed! rc=%d\n", rc));
    }
}

