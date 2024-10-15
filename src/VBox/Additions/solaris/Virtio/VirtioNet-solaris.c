/* $Id: VirtioNet-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Guest Additions - Virtio Network Driver for Solaris.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include "Virtio-solaris.h"
#include "VirtioPci-solaris.h"

#include <sys/conf.h>
#include <sys/sunddi.h>
#include <sys/mac_provider.h>
#include <sys/strsun.h>
#include <sys/cmn_err.h>

#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define DEVICE_NAME               "virtnet"
/** The module descriptions as seen in 'modinfo'. */
#define DEVICE_DESC_DRV           "VirtualBox VirtioNet"

/** Copied from "mac_ether.h" - why the heck is this not public?? All Solaris
 *  mac clients use it... */
#define MAC_PLUGIN_IDENT_ETHER    "mac_ether"

/* Copied from our Virtio Device emulation. */
#define VIRTIO_NET_CSUM           0x00000001      /* Host handles pkts w/ partial csum */
#define VIRTIO_NET_GUEST_CSUM     0x00000002      /* Guest handles pkts w/ partial csum */
#define VIRTIO_NET_MAC            0x00000020      /* Host has given MAC address. */
#define VIRTIO_NET_GSO            0x00000040      /* Host handles pkts w/ any GSO type */
#define VIRTIO_NET_GUEST_TSO4     0x00000080      /* Guest can handle TSOv4 in. */
#define VIRTIO_NET_GUEST_TSO6     0x00000100      /* Guest can handle TSOv6 in. */
#define VIRTIO_NET_GUEST_ECN      0x00000200      /* Guest can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_GUEST_UFO      0x00000400      /* Guest can handle UFO in. */
#define VIRTIO_NET_HOST_TSO4      0x00000800      /* Host can handle TSOv4 in. */
#define VIRTIO_NET_HOST_TSO6      0x00001000      /* Host can handle TSOv6 in. */
#define VIRTIO_NET_HOST_ECN       0x00002000      /* Host can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_HOST_UFO       0x00004000      /* Host can handle UFO in. */
#define VIRTIO_NET_MRG_RXBUF      0x00008000      /* Host can merge receive buffers. */
#define VIRTIO_NET_STATUS         0x00010000      /* virtio_net_config.status available */
#define VIRTIO_NET_CTRL_VQ        0x00020000      /* Control channel available */
#define VIRTIO_NET_CTRL_RX        0x00040000      /* Control channel RX mode support */
#define VIRTIO_NET_CTRL_VLAN      0x00080000      /* Control channel VLAN filtering */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void      *VirtioNetDevAlloc(PVIRTIODEVICE pDevice);
static void       VirtioNetDevFree(PVIRTIODEVICE pDevice);
static int        VirtioNetDevAttach(PVIRTIODEVICE pDevice);
static int        VirtioNetDevDetach(PVIRTIODEVICE pDevice);

static int        VirtioNetAttach(dev_info_t *pDip, ddi_attach_cmd_t Cmd);
static int        VirtioNetDetach(dev_info_t *pDip, ddi_detach_cmd_t Cmd);

static int        VirtioNetStat(void *pvArg, uint_t cmdStat, uint64_t *pu64Val);
static int        VirtioNetStart(void *pvArg);
static void       VirtioNetStop(void *pvArg);
static int        VirtioNetSetPromisc(void *pvArg, boolean_t fPromiscOn);
static int        VirtioNetSetMulticast(void *pvArg, boolean_t fAdd, const uint8_t *pbMac);
static int        VirtioNetSetUnicast(void *pvArg, const uint8_t *pbMac);
static boolean_t  VirtioNetGetCapab(void *pvArg, mac_capab_t Capab, void *pvCapabData);
static mblk_t    *VirtioNetXmit(void *pvArg, mblk_t *pMsg);
static uint_t     VirtioNetISR(caddr_t addrArg);

static int        VirtioNetAttachQueues(PVIRTIODEVICE pDevice);
static void       VirtioNetDetachQueues(PVIRTIODEVICE pDevice);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Device operations for Virtio Net.
 */
VIRTIODEVICEOPS g_VirtioDeviceOpsNet =
{
    VirtioNetDevAlloc,
    VirtioNetDevFree,
    VirtioNetDevAttach,
    VirtioNetDevDetach
};

/**
 * virtio_net_t: Private data per Virtio Device.
 */
typedef struct virtio_net_t
{
    mac_handle_t        hMac;           /* Handle to the MAC layer. */
    RTMAC               MacAddr;        /* MAC address. */
    PVIRTIOQUEUE        pRxQueue;       /* Receive Queue. */
    PVIRTIOQUEUE        pTxQueue;       /* Xmit Queue. */
    PVIRTIOQUEUE        pCtrlQueue;     /* Control Queue. */
    kmem_cache_t       *pTxCache;       /* TX buffer cache. */
} virtio_net_t;

/**
 * virtio_txbuf_t: Virtio Net TX buffer.
 */
typedef struct virtio_net_txbuf_t
{
    ddi_dma_handle_t    hDMA;           /* DMA TX handle. */
} virtio_net_txbuf_t;

/*
 * virtio_net_header_t: Virtio Net TX/RX buffer header.
 */
typedef struct virtio_net_header_t
{
    uint8_t             u8Flags;        /* Flags. */
    uint8_t             u8GSOType;      /* GSO type. */
    uint16_t            u16HdrLen;      /* Length of this header. */
    uint16_t            u16GSOSize;     /* GSO length if applicable. */
    uint16_t            u16CSumStart;   /* Checksum start.*/
    uint16_t            u16CSumOffset;  /* Checksum offset.*/
} virtio_net_header_t;

/**
 * MAC layer hooks for VirtioNet.
 */
static mac_callbacks_t g_VirtioNetCallbacks =
{
    MC_GETCAPAB,                        /* Mask of available extra hooks. */
    VirtioNetStat,
    VirtioNetStart,
    VirtioNetStop,
    VirtioNetSetPromisc,
    VirtioNetSetMulticast,
    VirtioNetSetUnicast,
    VirtioNetXmit,
    NULL,                               /* Reserved. */
    NULL,                               /* IOCtl. */
    VirtioNetGetCapab,
};

/**
 * DMA transfer attributes for Xmit/Recv. buffers.
 */
static ddi_dma_attr_t g_VirtioNetBufDmaAttr =
{
    DMA_ATTR_V0,                    /* Version. */
    0,                              /* Lowest usable address. */
    0xffffffffffffffffULL,          /* Highest usable address. */
    0x7fffffff,                     /* Maximum DMAable byte count. */
    MMU_PAGESIZE,                   /* Alignment in bytes. */
    0x7ff,                          /* Bitmap of burst sizes */
    1,                              /* Minimum transfer. */
    0xffffffffU,                    /* Maximum transfer. */
    0xffffffffffffffffULL,          /* Maximum segment length. */
    1,                              /* Maximum number of segments. */
    1,                              /* Granularity. */
    0                               /* Flags (reserved). */
};

/**
 * cb_ops: driver char/block entry points
 */
static struct cb_ops g_VirtioNetCbOps =
{
    nulldev,                        /* cb open */
    nulldev,                        /* cb close */
    nodev,                          /* b strategy */
    nodev,                          /* b dump */
    nodev,                          /* b print */
    nodev,                          /* cb read */
    nodev,                          /* cb write */
    nodev,                          /* cb ioctl */
    nodev,                          /* c devmap */
    nodev,                          /* c mmap */
    nodev,                          /* c segmap */
    nochpoll,                       /* c poll */
    ddi_prop_op,                    /* property ops */
    NULL,                           /* streamtab */
    D_MP,                           /* compat. flag */
    CB_REV                          /* revision */
};

/**
 * dev_ops: driver entry/exit and other ops.
 */
static struct dev_ops g_VirtioNetDevOps =
{
    DEVO_REV,                       /* driver build revision */
    0,                              /* ref count */
    NULL,                           /* get info */
    nulldev,                        /* identify */
    nulldev,                        /* probe */
    VirtioNetAttach,
    VirtioNetDetach,
    nodev,                          /* reset */
    &g_VirtioNetCbOps,
    (struct bus_ops *)0,
    nodev                           /* power */
};

/**
 * modldrv: export driver specifics to kernel
 */
static struct modldrv g_VirtioNetDriver =
{
    &mod_driverops,                 /* extern from kernel */
    DEVICE_DESC_DRV " " VBOX_VERSION_STRING "r"  RT_XSTR(VBOX_SVN_REV),
    &g_VirtioNetDevOps
};

/**
 * modlinkage: export install/remove/info to the kernel
 */
static struct modlinkage g_VirtioNetModLinkage =
{
    MODREV_1,                       /* loadable module system revision */
    {
        &g_VirtioNetDriver,         /* driver framework */
        NULL                        /* terminate array of linkage structures */
    }
};


/**
 * Kernel entry points
 */
int _init(void)
{
    LogFlowFunc((VIRTIOLOGNAME ":_init\n"));

    /*
     * Prevent module autounloading.
     */
    modctl_t *pModCtl = mod_getctl(&g_VirtioNetModLinkage);
    if (pModCtl)
        pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD;
    else
        LogRel((VIRTIOLOGNAME ":failed to disable autounloading!\n"));

    /*
     * Initialize IPRT.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize Solaris specific globals here.
         */
        mac_init_ops(&g_VirtioNetDevOps, DEVICE_NAME);
        rc = mod_install(&g_VirtioNetModLinkage);
        if (!rc)
            return rc;

        LogRel((VIRTIOLOGNAME ":mod_install failed. rc=%d\n", rc));
        mac_fini_ops(&g_VirtioNetDevOps);
        RTR0Term();
        return rc;
    }
    else
        LogRel((VIRTIOLOGNAME ":failed to initialize IPRT (rc=%d)\n", rc));

    return RTErrConvertToErrno(rc);
}


int _fini(void)
{
    int rc;
    LogFlowFunc((VIRTIOLOGNAME ":_fini\n"));

    rc = mod_remove(&g_VirtioNetModLinkage);
    if (!rc)
    {
        mac_fini_ops(&g_VirtioNetDevOps);
        RTR0Term();
    }
    return rc;
}


int _info(struct modinfo *pModInfo)
{
    LogFlowFunc((VIRTIOLOGNAME ":_info\n"));

    int rc = mod_info(&g_VirtioNetModLinkage, pModInfo);

    LogFlow((VIRTIOLOGNAME ":_info returns %d\n", rc));
    return rc;
}


/**
 * Attach entry point, to attach a device to the system or resume it.
 *
 * @param   pDip            The module structure instance.
 * @param   Cmd             Operation type (attach/resume).
 *
 * @return corresponding solaris error code.
 */
static int VirtioNetAttach(dev_info_t *pDip, ddi_attach_cmd_t Cmd)
{
    return VirtioAttach(pDip, Cmd, &g_VirtioDeviceOpsNet, &g_VirtioHyperOpsPci);
}


/**
 * Detach entry point, to detach a device to the system or suspend it.
 *
 * @param   pDip            The module structure instance.
 * @param   Cmd             Operation type (detach/suspend).
 *
 * @return corresponding solaris error code.
 */
static int VirtioNetDetach(dev_info_t *pDip, ddi_detach_cmd_t Cmd)
{
    return VirtioDetach(pDip, Cmd);
}


/**
 * Virtio Net TX buffer constructor for kmem_cache_create().
 *
 * @param pvBuf             Pointer to the allocated buffer.
 * @param pvArg             Opaque private data.
 * @param fFlags            Propagated KM flag values.
 *
 * @return 0 on success, or -1 on failure.
 */
static int VirtioNetTxBufCreate(void *pvBuf, void *pvArg, int fFlags)
{
    virtio_net_txbuf_t *pTxBuf = pvBuf;
    PVIRTIODEVICE pDevice = pvArg;

    /** @todo ncookies handles? */
    int rc = ddi_dma_alloc_handle(pDevice->pDip, &g_VirtioNetBufDmaAttr,
                                  fFlags & KM_NOSLEEP ? DDI_DMA_DONTWAIT : DDI_DMA_SLEEP,
                                  0 /* Arg */, &pTxBuf->hDMA);
    if (rc == DDI_SUCCESS)
        return 0;
    return -1;
}


/**
 * Virtio Net TX buffer destructor for kmem_cache_create().
 *
 * @param pvBuf             Pointer to the allocated buffer.
 * @param pvArg
 */
static void VirtioNetTxBufDestroy(void *pvBuf, void *pvArg)
{
    NOREF(pvArg);
    virtio_net_txbuf_t *pTxBuf = pvBuf;

    ddi_dma_free_handle(&pTxBuf->hDMA);
}


/**
 * Virtio Net private data allocation routine.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 *
 * @return Allocated private data that must only be freed by calling
 *         VirtioNetDevFree().
 */
static void *VirtioNetDevAlloc(PVIRTIODEVICE pDevice)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioNetDevAlloc pDevice=%p\n", pDevice));

    AssertReturn(pDevice, NULL);
    virtio_net_t *pNet = RTMemAllocZ(sizeof(virtio_net_t));
    if (RT_LIKELY(pNet))
    {
        /*
         * Create a kernel memory cache for frequently allocated/deallocated
         * buffers.
         */
        char szCachename[KSTAT_STRLEN];
        RTStrPrintf(szCachename, sizeof(szCachename), "VirtioNet_Cache_%d", ddi_get_instance(pDevice->pDip));
        pNet->pTxCache = kmem_cache_create(szCachename,                /* Cache name */
                                           sizeof(virtio_net_txbuf_t), /* Size of buffers in cache */
                                           0,                          /* Align */
                                           VirtioNetTxBufCreate,       /* Buffer constructor */
                                           VirtioNetTxBufDestroy,      /* Buffer destructor */
                                           NULL,                       /* pfnReclaim */
                                           pDevice,                    /* Private data */
                                           NULL,                       /* "vmp", MBZ (man page) */
                                           0                           /* "cflags", MBZ (man page) */
                                           );
        if (RT_LIKELY(pNet->pTxCache))
            return pNet;
        else
            LogRel((VIRTIOLOGNAME ":kmem_cache_create failed.\n"));
    }
    else
        LogRel((VIRTIOLOGNAME ":failed to alloc %u bytes for Net instance.\n", sizeof(virtio_net_t)));

    return NULL;
}


/**
 * Virtio Net private data free routine.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 */
static void VirtioNetDevFree(PVIRTIODEVICE pDevice)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioNetDevFree pDevice=%p\n", pDevice));
    AssertReturnVoid(pDevice);

    virtio_net_t *pNet = pDevice->pvDevice;
    kmem_cache_destroy(pNet->pTxCache);
    RTMemFree(pNet);
    pDevice->pvDevice = NULL;
}


/**
 * Virtio Net device attach rountine.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 *
 * @return corresponding solaris error code.
 */
static int VirtioNetDevAttach(PVIRTIODEVICE pDevice)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioNetDevAttach pDevice=%p\n", pDevice));

    virtio_net_t *pNet = pDevice->pvDevice;
    mac_register_t *pMacRegHandle = mac_alloc(MAC_VERSION);
    if (pMacRegHandle)
    {
        pMacRegHandle->m_driver     = pDevice;
        pMacRegHandle->m_dip        = pDevice->pDip;
        pMacRegHandle->m_callbacks  = &g_VirtioNetCallbacks;
        pMacRegHandle->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
        pMacRegHandle->m_min_sdu    = 0;
        pMacRegHandle->m_max_sdu    = 1500;    /** @todo verify */
        /** @todo should we set the margin size? */
        pMacRegHandle->m_src_addr   = pNet->MacAddr.au8;

        /*
         * Get MAC from the host or generate a random MAC address.
         */
        if (pDevice->fHostFeatures & VIRTIO_NET_MAC)
        {
            pDevice->pHyperOps->pfnGet(pDevice, 0 /* offset */, &pNet->MacAddr.au8, sizeof(pNet->MacAddr));
            LogFlow((VIRTIOLOGNAME ":VirtioNetDevAttach: Obtained MAC address from host: %.6Rhxs\n", pNet->MacAddr.au8));
        }
        else
        {
            pNet->MacAddr.au8[0] = 0x08;
            pNet->MacAddr.au8[1] = 0x00;
            pNet->MacAddr.au8[2] = 0x27;
            RTRandBytes(&pNet->MacAddr.au8[3], 3);
            LogFlow((VIRTIOLOGNAME ":VirtioNetDevAttach: Generated MAC address %.6Rhxs\n", pNet->MacAddr.au8));
        }

        int rc = VirtioNetAttachQueues(pDevice);
        if (rc == DDI_SUCCESS)
        {
            rc = mac_register(pMacRegHandle, &pNet->hMac);
            if (rc == 0)
            {
                mac_link_update(pNet->hMac, LINK_STATE_DOWN);
                mac_free(pMacRegHandle);
                LogFlow((VIRTIOLOGNAME ":VirtioNetDevAttach: successfully registered mac.\n"));
                return DDI_SUCCESS;
            }
            else
                LogRel((VIRTIOLOGNAME ":VirtioNetDevAttach: mac_register failed. rc=%d\n", rc));

            VirtioNetDetachQueues(pDevice);
        }
        else
            LogRel((VIRTIOLOGNAME ":VirtioNetDevAttach: VirtioNetAttachQueues failed. rc=%d\n", rc));

        mac_free(pMacRegHandle);
    }
    else
        LogRel((VIRTIOLOGNAME ":VirtioNetDevAttach: mac_alloc failed. Invalid version!?!\n"));

    return DDI_FAILURE;
}


/**
 * Virtio Net device detach routine.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 *
 * @return corresponding solaris error code.
 */
static int VirtioNetDevDetach(PVIRTIODEVICE pDevice)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioNetDevDetach pDevice=%p\n", pDevice));
    virtio_net_t *pNet = pDevice->pvDevice;

    int rc = mac_unregister(pNet->hMac);
    if (rc == 0)
    {
        VirtioNetDetachQueues(pDevice);
        return DDI_SUCCESS;
    }
    else
        LogRel((VIRTIOLOGNAME ":VirtioNetDevDetach: mac_unregister failed. rc=%d\n", rc));

    return DDI_FAILURE;
}


/**
 * Attach the Virtio Net TX, RX and control queues.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 *
 * @return corresponding solaris error code.
 */
static int VirtioNetAttachQueues(PVIRTIODEVICE pDevice)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioNetAttachQueues pDevice=%p\n", pDevice));

    virtio_net_t *pNet = pDevice->pvDevice;

    pNet->pRxQueue = VirtioGetQueue(pDevice, 0 /* index */ );
    if (pNet->pRxQueue)
    {
        pNet->pTxQueue = VirtioGetQueue(pDevice, 1 /* index */);
        if (pNet->pTxQueue)
        {
            if (pDevice->fHostFeatures & VIRTIO_NET_CTRL_VQ)
            {
                pNet->pCtrlQueue = VirtioGetQueue(pDevice, 2 /* index */);
                if (pNet->pCtrlQueue)
                {
                    LogFlow((VIRTIOLOGNAME ":VirtioNetAttachQueues successfully obtained 3 queues.\n"));
                    return DDI_SUCCESS;
                }
                else
                    LogRel((VIRTIOLOGNAME ":VirtioNetAttachQueues: failed to get Control queue.\n"));
            }
            else
            {
                LogFlow((VIRTIOLOGNAME ":VirtioNetAttachQueues successfully obtained 2 queues.\n"));
                return DDI_SUCCESS;
            }

            VirtioPutQueue(pDevice, pNet->pTxQueue);
            pNet->pTxQueue = NULL;
        }
        else
            LogRel((VIRTIOLOGNAME ":VirtioNetAttachQueues: failed to get TX queue.\n"));

        VirtioPutQueue(pDevice, pNet->pRxQueue);
        pNet->pRxQueue = NULL;
    }
    else
        LogRel((VIRTIOLOGNAME ":VirtioNetAttachQueues: failed to get RX queue.\n"));

    return DDI_FAILURE;
}


/**
 * Detach the Virtio Net TX, RX and control queues.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 */
static void VirtioNetDetachQueues(PVIRTIODEVICE pDevice)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioNetDetachQueues pDevice=%p\n", pDevice));
    virtio_net_t *pNet = pDevice->pvDevice;

    if (   pDevice->fHostFeatures & VIRTIO_NET_CTRL_VQ
        && pNet->pCtrlQueue)
        VirtioPutQueue(pDevice, pNet->pCtrlQueue);

    if (pNet->pTxCache)
        VirtioPutQueue(pDevice, pNet->pTxQueue);

    if (pNet->pRxQueue)
        VirtioPutQueue(pDevice, pNet->pRxQueue);
}



/* -=-=-=-=- Virtio Net MAC layer callbacks  -=-=-=-=- */

/**
 * Virtio Net statistics.
 *
 * @param pvArg             Pointer to private data.
 * @param cmdStat           Which statistics to provide.
 * @param pu64Val           Where to write statistics data.
 *
 * @return corresponding solaris error code.
 */
static int VirtioNetStat(void *pvArg, uint_t cmdStat, uint64_t *pu64Val)
{
    NOREF(pvArg);
    NOREF(cmdStat);
    NOREF(pu64Val);
    return ENOTSUP;
}


/**
 * Virtio Net Start.
 *
 * @param pvArg             Pointer to private data.
 *
 * @return corresponding solaris error code.
 */
static int VirtioNetStart(void *pvArg)
{
    PVIRTIODEVICE pDevice = pvArg;
    virtio_net_t *pNet = pDevice->pvDevice;
    mac_link_update(pNet->hMac, LINK_STATE_UP);

    pDevice->pHyperOps->pfnSetStatus(pDevice, VIRTIO_PCI_STATUS_DRV_OK);
    return 0;
}


/**
 * Virtio Net Stop.
 *
 * @param pvArg             Pointer to private data.
 */
static void VirtioNetStop(void *pvArg)
{
    PVIRTIODEVICE pDevice = pvArg;
    virtio_net_t *pNet = pDevice->pvDevice;
    mac_link_update(pNet->hMac, LINK_STATE_DOWN);

    /*
     * I don't think we should set status here as the host checks the status on every Xmit. This means pending Xmits
     * would also be dropped.
     * @todo: Not sure what's the best way to signal connect/disconnect of the link to the host. Figure it out.
     */
}


/**
 * Virtio Net toggle Promiscuous mode.
 *
 * @param pvArg             Pointer to private data.
 * @param fPromiscOn        Promiscuous On/Off.
 *
 * @return corresponding solaris error code.
 */
static int VirtioNetSetPromisc(void *pvArg, boolean_t fPromiscOn)
{
    NOREF(pvArg);
    NOREF(fPromiscOn);
    return 0;
}


/**
 * Virtio Net set/add multicast address.
 *
 * @param pvArg             Pointer to private data.
 * @param fAdd              Whether to add multicast address or not.
 * @param pbMac             Pointer to the multicast MAC address to set/add.
 *
 * @return corresponding solaris error code.
 */
static int VirtioNetSetMulticast(void *pvArg, boolean_t fAdd, const uint8_t *pbMac)
{
    NOREF(pvArg);
    NOREF(fAdd);
    NOREF(pbMac);
    return 1;
}


/**
 * Virtio Net set unicast address.
 *
 * @param pvArg             Pointer to private data.
 * @param pbMac             Pointer to the unicast MAC address to set.
 *
 * @return corresponding solaris error code.
 */
static int VirtioNetSetUnicast(void *pvArg, const uint8_t *pbMac)
{
    NOREF(pvArg);
    NOREF(pbMac);
    return ENOTSUP;
}


/**
 * Virtio Net get capabilities hook.
 *
 * @param pvArg             Pointer to private data.
 * @param Capab             MAC layer capabilities.
 * @param pvCapabData       Pointer to store capability info.
 *
 * @return B_TRUE upon success, otherwise B_FALSE.
 */
static boolean_t VirtioNetGetCapab(void *pvArg, mac_capab_t Capab, void *pvCapabData)
{
    NOREF(pvArg);
    NOREF(Capab);
    NOREF(pvCapabData);
    return B_FALSE;
}


/**
 * Virtio Net Xmit hook.
 *
 * @param pvArg             Pointer to private data.
 * @param pMsg              Pointer to the message.
 *
 * @return Pointer to message not Xmited.
 */
static mblk_t *VirtioNetXmit(void *pvArg, mblk_t *pMsg)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioNetXmit pMsg=%p\n", pMsg));
    cmn_err(CE_NOTE, "Xmit pMsg=%p\n", pMsg);

    PVIRTIODEVICE pDevice = pvArg;
    virtio_net_t *pNet    = pDevice->pvDevice;
    bool fNotify          = false;

    while (pMsg)
    {
        mblk_t *pNextMsg = pMsg->b_next;

#if 0
        mblk_t *pHdr = allocb(sizeof(virtio_net_header_t), BPRI_HI);
        if (RT_UNLIKELY(!pHdr))
            break;

        virtio_net_header_t *pNetHdr = pHdr->b_rptr;
        memset(pNetHdr, 0, sizeof(virtio_net_header_t));
        pNetHdr->u8Flags       = VIRTIO_NET_GUEST_CSUM;
        pNetHdr->u16HdrLen     = sizeof(virtio_net_header_t);

        pHdr->b_wptr += sizeof(virtio_net_header_t);
        pHdr->b_cont = pMsg;
#endif

        virtio_net_txbuf_t *pTxBuf = kmem_cache_alloc(pNet->pTxCache, KM_SLEEP);
        if (!pTxBuf)
            break;

        ddi_dma_cookie_t DmaCookie;
        uint_t cCookies;
        int rc = ddi_dma_addr_bind_handle(pTxBuf->hDMA, NULL /* addrspace */, (char *)pMsg->b_rptr, MBLKL(pMsg),
                                          DDI_DMA_WRITE | DDI_DMA_STREAMING, DDI_DMA_SLEEP, 0 /* addr */,
                                          &DmaCookie, &cCookies);
        cmn_err(CE_NOTE, "VirtioNetXmit: MBLKL pMsg=%u\n", MBLKL(pMsg));
        if (rc != DDI_DMA_MAPPED)
        {
            LogRel((VIRTIOLOGNAME ":VirtioNetXmit failed to map address to DMA handle. rc=%d\n", rc));
            kmem_cache_free(pNet->pTxCache, pTxBuf);
            break;
        }

        /** @todo get 'cCookies' slots from the ring. */

        for (uint_t i = 0; i < cCookies; i++)
        {
            uint16_t fFlags = 0;
            if (i < cCookies - 1)
                fFlags |= VIRTIO_FLAGS_RING_DESC_NEXT;

            rc = VirtioRingPush(pNet->pTxQueue, DmaCookie.dmac_laddress, DmaCookie.dmac_size, fFlags);
            if (RT_FAILURE(rc))
            {
                LogRel((VIRTIOLOGNAME ":VirtioNetXmit failed. rc=%Rrc\n", rc));
                break;
            }

            ddi_dma_nextcookie(pTxBuf->hDMA, &DmaCookie);
        }

        pMsg = pNextMsg;
        fNotify = true;
        if (RT_FAILURE(rc))
        {
            ddi_dma_unbind_handle(pTxBuf->hDMA);
            break;
        }
    }

    if (fNotify)
        pDevice->pHyperOps->pfnNotifyQueue(pDevice, pNet->pTxQueue);

    return pMsg;
}


/**
 * Interrupt Service Routine for Virtio Net.
 *
 * @param   Arg     Private data (unused, will be NULL).
 * @returns DDI_INTR_CLAIMED if it's our interrupt, DDI_INTR_UNCLAIMED if it isn't.
 */
static uint_t VirtioNetISR(caddr_t Arg)
{
    cmn_err(CE_NOTE, "VirtioNetISR Arg=%p\n", Arg);
    NOREF(Arg);
    return DDI_INTR_UNCLAIMED;
}

