/* $Id: VBoxNetAdp-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetAdapter - Network Adapter Driver (Host), Solaris Specific Code.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_NET_ADP_DRV
#include <VBox/log.h>
#include <iprt/errcore.h>
#include <VBox/version.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/initterm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/rand.h>

#include <sys/types.h>
#include <sys/dlpi.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/strsun.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/gld.h>

#include "../VBoxNetAdpInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define DEVICE_NAME              "vboxnet"
/** The module descriptions as seen in 'modinfo'. */
#define DEVICE_DESC_DRV          "VirtualBox NetAdp"
/** The maximum MTU size permittable, value taken from "Oracle Quad 10 Gb or Dual 40
 *  Gb Ethernet Adapter User's Guide". */
#define DEVICE_MAX_MTU_SIZE      9706


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int VBoxNetAdpSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd);
static int VBoxNetAdpSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd);
static int VBoxNetAdpSolarisQuiesceNotNeeded(dev_info_t *pDip);

/**
 * Streams: module info.
 */
static struct module_info g_VBoxNetAdpSolarisModInfo =
{
    0x0dd,                            /* module id */
    DEVICE_NAME,
    0,                                /* min. packet size */
    INFPSZ,                           /* max. packet size */
    0,                                /* hi-water mark */
    0                                 /* lo-water mark */
};

/**
 * Streams: read queue hooks.
 */
static struct qinit g_VBoxNetAdpSolarisReadQ =
{
    NULL,                             /* read */
    gld_rsrv,
    gld_open,
    gld_close,
    NULL,                             /* admin (reserved) */
    &g_VBoxNetAdpSolarisModInfo,
    NULL                              /* module stats */
};

/**
 * Streams: write queue hooks.
 */
static struct qinit g_VBoxNetAdpSolarisWriteQ =
{
    gld_wput,
    gld_wsrv,
    NULL,                             /* open */
    NULL,                             /* close */
    NULL,                             /* admin (reserved) */
    &g_VBoxNetAdpSolarisModInfo,
    NULL                              /* module stats */
};

/**
 * Streams: IO stream tab.
 */
static struct streamtab g_VBoxNetAdpSolarisStreamTab =
{
    &g_VBoxNetAdpSolarisReadQ,
    &g_VBoxNetAdpSolarisWriteQ,
    NULL,                           /* muxread init */
    NULL                            /* muxwrite init */
};

/**
 * cb_ops: driver char/block entry points
 */
static struct cb_ops g_VBoxNetAdpSolarisCbOps =
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
    &g_VBoxNetAdpSolarisStreamTab,
    D_MP,                           /* compat. flag */
    CB_REV                          /* revision */
};

/**
 * dev_ops: driver entry/exit and other ops.
 */
static struct dev_ops g_VBoxNetAdpSolarisDevOps =
{
    DEVO_REV,                       /* driver build revision */
    0,                              /* ref count */
    gld_getinfo,
    nulldev,                        /* identify */
    nulldev,                        /* probe */
    VBoxNetAdpSolarisAttach,
    VBoxNetAdpSolarisDetach,
    nodev,                          /* reset */
    &g_VBoxNetAdpSolarisCbOps,
    (struct bus_ops *)0,
    nodev,                          /* power */
    VBoxNetAdpSolarisQuiesceNotNeeded
};

/**
 * modldrv: export driver specifics to kernel
 */
static struct modldrv g_VBoxNetAdpSolarisDriver =
{
    &mod_driverops,                 /* extern from kernel */
    DEVICE_DESC_DRV " " VBOX_VERSION_STRING "r"  RT_XSTR(VBOX_SVN_REV),
    &g_VBoxNetAdpSolarisDevOps
};

/**
 * modlinkage: export install/remove/info to the kernel
 */
static struct modlinkage g_VBoxNetAdpSolarisModLinkage =
{
    MODREV_1,                       /* loadable module system revision */
    {
        &g_VBoxNetAdpSolarisDriver, /* adapter streams driver framework */
        NULL                        /* terminate array of linkage structures */
    }
};


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The default ethernet broadcast address */
static uchar_t achBroadcastAddr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/**
 * vboxnetadp_state_t: per-instance data
 */
typedef struct vboxnetadp_state_t
{
    dev_info_t   *pDip;           /* device info. */
    RTMAC         FactoryMac;     /* default 'factory' MAC address */
    RTMAC         CurrentMac;     /* current MAC address */
} vboxnetadp_state_t;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int vboxNetAdpSolarisGenerateMac(PRTMAC pMac);
static int vboxNetAdpSolarisSetMacAddress(gld_mac_info_t *pMacInfo, unsigned char *pszMacAddr);
static int vboxNetAdpSolarisSend(gld_mac_info_t *pMacInfo, mblk_t *pMsg);
static int vboxNetAdpSolarisStub(gld_mac_info_t *pMacInfo);
static int vboxNetAdpSolarisSetPromisc(gld_mac_info_t *pMacInfo, int fPromisc);
static int vboxNetAdpSolarisSetMulticast(gld_mac_info_t *pMacInfo, unsigned char *pMulticastAddr, int fMulticast);
static int vboxNetAdpSolarisGetStats(gld_mac_info_t *pMacInfo, struct gld_stats *pStats);


/**
 * Kernel entry points
 */
int _init(void)
{
    LogFunc((DEVICE_NAME ":_init\n"));

    /*
     * Prevent module autounloading.
     */
    modctl_t *pModCtl = mod_getctl(&g_VBoxNetAdpSolarisModLinkage);
    if (pModCtl)
        pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD;
    else
        LogRel((DEVICE_NAME ":failed to disable autounloading!\n"));

    /*
     * Initialize IPRT.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        rc = mod_install(&g_VBoxNetAdpSolarisModLinkage);
        if (!rc)
            return rc;

        LogRel((DEVICE_NAME ":mod_install failed. rc=%d\n", rc));
        RTR0Term();
    }
    else
        LogRel((DEVICE_NAME ":failed to initialize IPRT (rc=%d)\n", rc));

    return RTErrConvertToErrno(rc);
}


int _fini(void)
{
    LogFunc((DEVICE_NAME ":_fini\n"));

    /*
     * Undo the work done during start (in reverse order).
     */
    int rc = mod_remove(&g_VBoxNetAdpSolarisModLinkage);
    if (!rc)
        RTR0Term();

    return rc;
}


int _info(struct modinfo *pModInfo)
{
    LogFunc((DEVICE_NAME ":_info\n"));

    int rc = mod_info(&g_VBoxNetAdpSolarisModLinkage, pModInfo);

    Log((DEVICE_NAME ":_info returns %d\n", rc));
    return rc;
}


/**
 * Attach entry point, to attach a device to the system or resume it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Operation type (attach/resume).
 *
 * @returns corresponding solaris error code.
 */
static int VBoxNetAdpSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ":VBoxNetAdpSolarisAttach pDip=%p enmCmd=%d\n", pDip, enmCmd));

    int rc = -1;
    switch (enmCmd)
    {
        case DDI_ATTACH:
        {
            gld_mac_info_t *pMacInfo = gld_mac_alloc(pDip);
            if (pMacInfo)
            {
                vboxnetadp_state_t *pState = RTMemAllocZ(sizeof(vboxnetadp_state_t));
                if (pState)
                {
                    pState->pDip = pDip;

                    /*
                     * Setup GLD MAC layer registration info.
                     */
                    pMacInfo->gldm_reset = vboxNetAdpSolarisStub;
                    pMacInfo->gldm_start = vboxNetAdpSolarisStub;
                    pMacInfo->gldm_stop = vboxNetAdpSolarisStub;
                    pMacInfo->gldm_set_mac_addr = vboxNetAdpSolarisSetMacAddress;
                    pMacInfo->gldm_set_multicast = vboxNetAdpSolarisSetMulticast;
                    pMacInfo->gldm_set_promiscuous = vboxNetAdpSolarisSetPromisc;
                    pMacInfo->gldm_send = vboxNetAdpSolarisSend;
                    pMacInfo->gldm_intr = NULL;
                    pMacInfo->gldm_get_stats = vboxNetAdpSolarisGetStats;
                    pMacInfo->gldm_ioctl = NULL;
                    pMacInfo->gldm_ident = DEVICE_NAME;
                    pMacInfo->gldm_type = DL_ETHER;
                    pMacInfo->gldm_minpkt = 0;
                    pMacInfo->gldm_maxpkt = DEVICE_MAX_MTU_SIZE;
                    pMacInfo->gldm_capabilities = GLD_CAP_LINKSTATE;
                    AssertCompile(sizeof(RTMAC) == ETHERADDRL);

                    pMacInfo->gldm_addrlen = ETHERADDRL;
                    pMacInfo->gldm_saplen = -2;
                    pMacInfo->gldm_broadcast_addr = achBroadcastAddr;
                    pMacInfo->gldm_ppa = ddi_get_instance(pState->pDip);
                    pMacInfo->gldm_devinfo = pState->pDip;
                    pMacInfo->gldm_private = (caddr_t)pState;

                    /*
                     * We use a semi-random MAC addresses similar to a guest NIC's MAC address
                     * as the default factory address of the interface.
                     */
                    rc = vboxNetAdpSolarisGenerateMac(&pState->FactoryMac);
                    if (RT_SUCCESS(rc))
                    {
                        bcopy(&pState->FactoryMac, &pState->CurrentMac, sizeof(RTMAC));
                        pMacInfo->gldm_vendor_addr = (unsigned char *)&pState->FactoryMac;

                        /*
                         * Now try registering our GLD with the MAC layer.
                         * Registration can fail on some S10 versions when the MTU size is more than 1500.
                         * When we implement jumbo frames we should probably retry with MTU 1500 for S10.
                         */
                        rc = gld_register(pDip, (char *)ddi_driver_name(pDip), pMacInfo);
                        if (rc == DDI_SUCCESS)
                        {
                            ddi_report_dev(pDip);
                            gld_linkstate(pMacInfo, GLD_LINKSTATE_UP);
                            return DDI_SUCCESS;
                        }
                        else
                            LogRel((DEVICE_NAME ":VBoxNetAdpSolarisAttach failed to register GLD. rc=%d\n", rc));
                    }
                    else
                        LogRel((DEVICE_NAME ":VBoxNetAdpSolarisAttach failed to generate mac address.rc=%d\n"));

                    RTMemFree(pState);
                }
                else
                    LogRel((DEVICE_NAME ":VBoxNetAdpSolarisAttach failed to alloc state.\n"));

                gld_mac_free(pMacInfo);
            }
            else
                LogRel((DEVICE_NAME ":VBoxNetAdpSolarisAttach failed to alloc mac structure.\n"));
            return DDI_FAILURE;
        }

        case DDI_RESUME:
        {
            /* Nothing to do here... */
            return DDI_SUCCESS;
        }

        /* case DDI_PM_RESUME: */
        default:
            return DDI_FAILURE;
    }
}


/**
 * Detach entry point, to detach a device to the system or suspend it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Operation type (detach/suspend).
 *
 * @returns corresponding solaris error code.
 */
static int VBoxNetAdpSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ":VBoxNetAdpSolarisDetach pDip=%p enmCmd=%d\n", pDip, enmCmd));

    switch (enmCmd)
    {
        case DDI_DETACH:
        {
            /*
             * Unregister and clean up.
             */
            gld_mac_info_t *pMacInfo = ddi_get_driver_private(pDip);
            if (pMacInfo)
            {
                vboxnetadp_state_t *pState = (vboxnetadp_state_t *)pMacInfo->gldm_private;
                if (pState)
                {
                    gld_linkstate(pMacInfo, GLD_LINKSTATE_DOWN);
                    int rc = gld_unregister(pMacInfo);
                    if (rc == DDI_SUCCESS)
                    {
                        gld_mac_free(pMacInfo);
                        RTMemFree(pState);
                        return DDI_SUCCESS;
                    }
                    else
                        LogRel((DEVICE_NAME ":VBoxNetAdpSolarisDetach failed to unregister GLD from MAC layer.rc=%d\n", rc));
                }
                else
                    LogRel((DEVICE_NAME ":VBoxNetAdpSolarisDetach failed to get internal state.\n"));
            }
            else
                LogRel((DEVICE_NAME ":VBoxNetAdpSolarisDetach failed to get driver private GLD data.\n"));

            return DDI_FAILURE;
        }

        case DDI_RESUME:
        {
            /* Nothing to do here... */
            return DDI_SUCCESS;
        }

        /* case DDI_SUSPEND: */
        /* case DDI_HOTPLUG_DETACH: */
        default:
            return DDI_FAILURE;
    }
}


/**
 * Quiesce not-needed entry point, as Solaris 10 doesn't have any
 * ddi_quiesce_not_needed() function.
 *
 * @param   pDip            The module structure instance.
 *
 * @return  corresponding solaris error code.
 */
static int VBoxNetAdpSolarisQuiesceNotNeeded(dev_info_t *pDip)
{
    return DDI_SUCCESS;
}


static int vboxNetAdpSolarisGenerateMac(PRTMAC pMac)
{
    pMac->au8[0] = 0x08;
    pMac->au8[1] = 0x00;
    pMac->au8[2] = 0x27;
    RTRandBytes(&pMac->au8[3], 3);
    Log((DEVICE_NAME ":VBoxNetAdpSolarisGenerateMac Generated %.*Rhxs\n", sizeof(RTMAC), &pMac));
    return VINF_SUCCESS;
}


static int vboxNetAdpSolarisSetMacAddress(gld_mac_info_t *pMacInfo, unsigned char *pszMacAddr)
{
    vboxnetadp_state_t *pState = (vboxnetadp_state_t *)pMacInfo->gldm_private;
    if (pState)
    {
        bcopy(pszMacAddr, &pState->CurrentMac, sizeof(RTMAC));
        Log((DEVICE_NAME ":vboxNetAdpSolarisSetMacAddress updated MAC %.*Rhxs\n", sizeof(RTMAC), &pState->CurrentMac));
        return GLD_SUCCESS;
    }
    else
        LogRel((DEVICE_NAME ":vboxNetAdpSolarisSetMacAddress failed to get internal state.\n"));
    return GLD_FAILURE;
}


static int vboxNetAdpSolarisSend(gld_mac_info_t *pMacInfo, mblk_t *pMsg)
{
    while (pMsg)
    {
        mblk_t *pMsgNext = pMsg->b_cont;
        pMsg->b_cont = NULL;
        freemsg(pMsg);
        pMsg = pMsgNext;
    }
    return GLD_SUCCESS;
}


static int vboxNetAdpSolarisStub(gld_mac_info_t *pMacInfo)
{
    return GLD_SUCCESS;
}


static int vboxNetAdpSolarisSetMulticast(gld_mac_info_t *pMacInfo, unsigned char *pMulticastAddr, int fMulticast)
{
    return GLD_SUCCESS;
}


static int vboxNetAdpSolarisSetPromisc(gld_mac_info_t *pMacInfo, int fPromisc)
{
    /* Host requesting promiscuous intnet connection... */
    return GLD_SUCCESS;
}


static int vboxNetAdpSolarisGetStats(gld_mac_info_t *pMacInfo, struct gld_stats *pStats)
{
    /*
     * For now fake up stats. Stats like duplex and speed are better set as they
     * are used in utilities like dladm. Link state capabilities are critical
     * as they are used by ipadm while trying to restore persistent interface configs.
     */
    vboxnetadp_state_t *pState = (vboxnetadp_state_t *)pMacInfo->gldm_private;
    if (pState)
    {
        pStats->glds_speed                = 1000000000ULL;     /* Bits/sec. */
        pStats->glds_media                = GLDM_UNKNOWN;      /* Media/Connector Type */
        pStats->glds_intr                 = 0;                 /* Interrupt count */
        pStats->glds_norcvbuf             = 0;                 /* Recv. discards */
        pStats->glds_errxmt               = 0;                 /* Xmit errors */
        pStats->glds_errrcv               = 0;                 /* Recv. errors */
        pStats->glds_missed               = 0;                 /* Pkt Drops on Recv. */
        pStats->glds_underflow            = 0;                 /* Buffer underflows */
        pStats->glds_overflow             = 0;                 /* Buffer overflows */

        /* Ether */
        pStats->glds_frame                = 0;                 /* Align errors */
        pStats->glds_crc                  = 0;                 /* CRC errors */
        pStats->glds_duplex               = GLD_DUPLEX_FULL;   /* Link duplex state */
        pStats->glds_nocarrier            = 0;                 /* Carrier sense errors */
        pStats->glds_collisions           = 0;                 /* Xmit Collisions */
        pStats->glds_excoll               = 0;                 /* Frame discard due to excess collisions */
        pStats->glds_xmtlatecoll          = 0;                 /* Late collisions */
        pStats->glds_defer                = 0;                 /* Deferred Xmits */
        pStats->glds_dot3_first_coll      = 0;                 /* Single collision frames */
        pStats->glds_dot3_multi_coll      = 0;                 /* Multiple collision frames */
        pStats->glds_dot3_sqe_error       = 0;                 /* SQE errors */
        pStats->glds_dot3_mac_xmt_error   = 0;                 /* MAC Xmit errors */
        pStats->glds_dot3_mac_rcv_error   = 0;                 /* Mac Recv. errors */
        pStats->glds_dot3_frame_too_long  = 0;                 /* Frame too long errors */
        pStats->glds_short                = 0;                 /* Runt frames */

        pStats->glds_noxmtbuf             = 0;                 /* Xmit Buf errors */
        pStats->glds_xmtretry             = 0;                 /* Xmit retries */
        pStats->glds_multixmt             = 0;                 /* Multicast Xmits */
        pStats->glds_multircv             = 0;                 /* Multicast Recvs. */
        pStats->glds_brdcstxmt            = 0;                 /* Broadcast Xmits*/
        pStats->glds_brdcstrcv            = 0;                 /* Broadcast Recvs. */

        return GLD_SUCCESS;
    }
    else
        LogRel((DEVICE_NAME ":vboxNetAdpSolarisGetStats failed to get internal state.\n"));
    return GLD_FAILURE;
}

