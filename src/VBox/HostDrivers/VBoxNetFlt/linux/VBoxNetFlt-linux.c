/* $Id: VBoxNetFlt-linux.c 110199 2025-07-11 13:54:21Z aleksey.ilyushin@oracle.com $ */
/** @file
 * VBoxNetFlt - Network Filter Driver (Host), Linux Specific Code.
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
#define LOG_GROUP LOG_GROUP_NET_FLT_DRV
#define VBOXNETFLT_LINUX_NO_XMIT_QUEUE
#include "the-linux-kernel.h"
#include "version-generated.h"
#include "revision-generated.h"
#include "product-generated.h"
#if RTLNX_VER_MIN(2,6,24)
# include <linux/nsproxy.h>
#endif
#if RTLNX_VER_MIN(3,10,0) /* proc_ns introduced */
# define VBOXNETFLT_LINUX_NAMESPACE_SUPPORT
# include <linux/proc_ns.h>
#endif
#if RTLNX_VER_MIN(6,4,10) || RTLNX_RHEL_RANGE(9,4, 9,99) || RTLNX_SUSE_MAJ_PREREQ(15, 6)
# include <net/gso.h>
#endif
#include <linux/netdevice.h>
#if RTLNX_VER_MAX(2,6,29) || RTLNX_VER_MIN(5,11,0)
# include <linux/ethtool.h>
#endif
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/miscdevice.h>
#include <linux/inetdevice.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/if_vlan.h>
#if RTLNX_VER_MIN(4,5,0)
# include <uapi/linux/pkt_cls.h>
#endif
#include <net/ipv6.h>
#include <net/if_inet6.h>
#include <net/addrconf.h>

#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/intnetinline.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/param.h>
#include <VBox/VBoxLnxModInline.h>
#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/spinlock.h>
#include <iprt/semaphore.h>
#include <iprt/initterm.h>
#include <iprt/process.h>
#include <iprt/mem.h>
#include <iprt/net.h>
#include <iprt/log.h>
#include <iprt/mp.h>
#include <iprt/mem.h>
#include <iprt/time.h>

#define VBOXNETFLT_OS_SPECFIC 1
#include "../VBoxNetFltInternal.h"

typedef struct VBOXNETFLTNOTIFIER {
    struct notifier_block Notifier;
    PVBOXNETFLTINS pThis;
} VBOXNETFLTNOTIFIER;
typedef struct VBOXNETFLTNOTIFIER *PVBOXNETFLTNOTIFIER;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define VBOX_FLT_NB_TO_INST(pNB)    RT_FROM_MEMBER(pNB, VBOXNETFLTINS, u.s.Notifier)
#define VBOX_FLT_PT_TO_INST(pPT)    RT_FROM_MEMBER(pPT, VBOXNETFLTINS, u.s.PacketType)
#ifndef VBOXNETFLT_LINUX_NO_XMIT_QUEUE
# define VBOX_FLT_XT_TO_INST(pXT)   RT_FROM_MEMBER(pXT, VBOXNETFLTINS, u.s.XmitTask)
#endif

#if RTLNX_VER_MIN(3,11,0)
# define VBOX_NETDEV_NOTIFIER_INFO_TO_DEV(ptr) netdev_notifier_info_to_dev(ptr)
#else
# define VBOX_NETDEV_NOTIFIER_INFO_TO_DEV(ptr) ((struct net_device *)ptr)
#endif

#if RTLNX_VER_MIN(3,5,0)
# define VBOX_SKB_KMAP_FRAG(frag) kmap_atomic(skb_frag_page(frag))
# define VBOX_SKB_KUNMAP_FRAG(vaddr) kunmap_atomic(vaddr)
#else
# if RTLNX_VER_MIN(3,2,0)
#  define VBOX_SKB_KMAP_FRAG(frag) kmap_atomic(skb_frag_page(frag), KM_SKB_DATA_SOFTIRQ)
#  define VBOX_SKB_KUNMAP_FRAG(vaddr) kunmap_atomic(vaddr, KM_SKB_DATA_SOFTIRQ)
# else
#  define VBOX_SKB_KMAP_FRAG(frag) kmap_atomic(frag->page, KM_SKB_DATA_SOFTIRQ)
#  define VBOX_SKB_KUNMAP_FRAG(vaddr) kunmap_atomic(vaddr, KM_SKB_DATA_SOFTIRQ)
# endif
#endif

#if RTLNX_VER_MIN(2,6,34)
# define VBOX_NETDEV_NAME(dev)              netdev_name(dev)
#else
# define VBOX_NETDEV_NAME(dev)              ((dev)->reg_state != NETREG_REGISTERED ? "(unregistered net_device)" : (dev)->name)
#endif

#if RTLNX_VER_MIN(2,6,25)
# define VBOX_IPV4_IS_LOOPBACK(addr)        ipv4_is_loopback(addr)
# define VBOX_IPV4_IS_LINKLOCAL_169(addr)   ipv4_is_linklocal_169(addr)
#else
# define VBOX_IPV4_IS_LOOPBACK(addr)        ((addr & htonl(IN_CLASSA_NET)) == htonl(0x7f000000))
# define VBOX_IPV4_IS_LINKLOCAL_169(addr)   ((addr & htonl(IN_CLASSB_NET)) == htonl(0xa9fe0000))
#endif

#if RTLNX_VER_MIN(2,6,22)
# define VBOX_SKB_RESET_NETWORK_HDR(skb)    skb_reset_network_header(skb)
# define VBOX_SKB_RESET_MAC_HDR(skb)        skb_reset_mac_header(skb)
# define VBOX_SKB_CSUM_OFFSET(skb)          skb->csum_offset
#else
# define VBOX_SKB_RESET_NETWORK_HDR(skb)    skb->nh.raw = skb->data
# define VBOX_SKB_RESET_MAC_HDR(skb)        skb->mac.raw = skb->data
# define VBOX_SKB_CSUM_OFFSET(skb)          skb->csum
#endif

#if RTLNX_VER_MIN(2,6,19)
# define VBOX_SKB_CHECKSUM_HELP(skb)        skb_checksum_help(skb)
#else
# define CHECKSUM_PARTIAL                   CHECKSUM_HW
# if RTLNX_VER_MIN(2,6,10)
#  define VBOX_SKB_CHECKSUM_HELP(skb)       skb_checksum_help(skb, 0)
# else
#  if RTLNX_VER_MIN(2,6,7)
#   define VBOX_SKB_CHECKSUM_HELP(skb)      skb_checksum_help(&skb, 0)
#  else
#   define VBOX_SKB_CHECKSUM_HELP(skb)      (!skb_checksum_help(skb))
#  endif
/* Versions prior 2.6.10 use stats for both bstats and qstats */
#  define bstats stats
#  define qstats stats
# endif
#endif

#if RTLNX_VER_MIN(6,9,0)
# define VBOX_SKB_FRAG_LEN(_pFrag)       ((_pFrag)->len)
# define VBOX_SKB_FRAG_OFFSET(_pFrag)    ((_pFrag)->offset)
#elif RTLNX_VER_MIN(5,4,0) || RTLNX_SUSE_MAJ_PREREQ(15, 2)
# define VBOX_SKB_FRAG_LEN(_pFrag)       ((_pFrag)->bv_len)
# define VBOX_SKB_FRAG_OFFSET(_pFrag)    ((_pFrag)->bv_offset)
#else /* < KERNEL_VERSION(5, 4, 0) */
# define VBOX_SKB_FRAG_LEN(_pFrag)       ((_pFrag)->size)
# define VBOX_SKB_FRAG_OFFSET(_pFrag)    ((_pFrag)->page_offset)
#endif /* > KERNEL_VERSION(6, 9, 0) */

#if RTLNX_VER_MIN(3,20,0) || RTLNX_RHEL_RANGE(7,2,  8,0) || RTLNX_RHEL_RANGE(6,8,  7,0)
# define VBOX_HAVE_SKB_VLAN
#endif

#ifdef VBOX_HAVE_SKB_VLAN
# define vlan_tx_tag_get(skb)       skb_vlan_tag_get(skb)
# define vlan_tx_tag_present(skb)   skb_vlan_tag_present(skb)
#endif

#ifndef NET_IP_ALIGN
# define NET_IP_ALIGN 2
#endif

#if 1
/** Create scatter / gather segments for fragments. When not used, we will
 *  linearize the socket buffer before creating the internal networking SG. */
# define VBOXNETFLT_SG_SUPPORT 1
#endif

#if RTLNX_VER_MIN(2,6,18)

/** Indicates that the linux kernel may send us GSO frames. */
# define VBOXNETFLT_WITH_GSO                1

/** This enables or disables the transmitting of GSO frame from the internal
 *  network and to the host.  */
# define VBOXNETFLT_WITH_GSO_XMIT_HOST      1

# if 0 /** @todo This is currently disable because it causes performance loss of 5-10%.  */
/** This enables or disables the transmitting of GSO frame from the internal
 *  network and to the wire. */
#  define VBOXNETFLT_WITH_GSO_XMIT_WIRE     1
# endif

/** This enables or disables the forwarding/flooding of GSO frame from the host
 *  to the internal network.  */
# define VBOXNETFLT_WITH_GSO_RECV           1

#endif /* RTLNX_VER_MIN(2,6,18) */

#if RTLNX_VER_MIN(2,6,29)
/** This enables or disables handling of GSO frames coming from the wire (GRO). */
# define VBOXNETFLT_WITH_GRO                1
#endif

/*
 * GRO support was backported to RHEL 5.4
 */
#if RTLNX_RHEL_MAJ_PREREQ(5, 4)
# define VBOXNETFLT_WITH_GRO                1
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int      __init VBoxNetFltLinuxInit(void);
static void     __exit VBoxNetFltLinuxUnload(void);
static void     vboxNetFltLinuxForwardToIntNet(PVBOXNETFLTINS pThis, struct sk_buff *pBuf);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * The (common) global data.
 */
static VBOXNETFLTGLOBALS g_VBoxNetFltGlobals;

module_init(VBoxNetFltLinuxInit);
module_exit(VBoxNetFltLinuxUnload);

MODULE_AUTHOR(VBOX_VENDOR);
MODULE_DESCRIPTION(VBOX_PRODUCT " Network Filter Driver");
MODULE_LICENSE("GPL");
#ifdef MODULE_VERSION
MODULE_VERSION(VBOX_VERSION_STRING " r" RT_XSTR(VBOX_SVN_REV) " (" RT_XSTR(INTNETTRUNKIFPORT_VERSION) ")");
#endif


#if RTLNX_VER_MAX(2,6,12) && defined(LOG_ENABLED)
unsigned dev_get_flags(const struct net_device *dev)
{
    unsigned flags;

    flags = (dev->flags & ~(IFF_PROMISC |
                            IFF_ALLMULTI |
                            IFF_RUNNING)) |
            (dev->gflags & (IFF_PROMISC |
                            IFF_ALLMULTI));

    if (netif_running(dev) && netif_carrier_ok(dev))
        flags |= IFF_RUNNING;

    return flags;
}
#endif /* RTLNX_VER_MAX(2,6,12) */


/**
 * Initialize module.
 *
 * @returns appropriate status code.
 */
static int __init VBoxNetFltLinuxInit(void)
{
    int rc;

    /* Check if modue loading was disabled. */
    if (!vbox_mod_should_load())
        return -EINVAL;

    /*
     * Initialize IPRT.
     */
    rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        Log(("VBoxNetFltLinuxInit\n"));

        /*
         * Initialize the globals and connect to the support driver.
         *
         * This will call back vboxNetFltOsOpenSupDrv (and maybe vboxNetFltOsCloseSupDrv)
         * for establishing the connect to the support driver.
         */
        memset(&g_VBoxNetFltGlobals, 0, sizeof(g_VBoxNetFltGlobals));
        rc = vboxNetFltInitGlobalsAndIdc(&g_VBoxNetFltGlobals);
        if (RT_SUCCESS(rc))
        {
            LogRel(("VBoxNetFlt: Successfully started.\n"));
            return 0;
        }

        LogRel(("VBoxNetFlt: failed to initialize device extension (rc=%d)\n", rc));
        RTR0Term();
    }
    else
        LogRel(("VBoxNetFlt: failed to initialize IPRT (rc=%d)\n", rc));

    memset(&g_VBoxNetFltGlobals, 0, sizeof(g_VBoxNetFltGlobals));
    return -RTErrConvertToErrno(rc);
}


/**
 * Unload the module.
 *
 * @todo We have to prevent this if we're busy!
 */
static void __exit VBoxNetFltLinuxUnload(void)
{
    int rc;
    Log(("VBoxNetFltLinuxUnload\n"));
    Assert(vboxNetFltCanUnload(&g_VBoxNetFltGlobals));

    /*
     * Undo the work done during start (in reverse order).
     */
    rc = vboxNetFltTryDeleteIdcAndGlobals(&g_VBoxNetFltGlobals);
    AssertRC(rc); NOREF(rc);

    RTR0Term();

    memset(&g_VBoxNetFltGlobals, 0, sizeof(g_VBoxNetFltGlobals));

    Log(("VBoxNetFltLinuxUnload - done\n"));
}


/**
 * We filter traffic from the host to the internal network
 * before it reaches the NIC driver.
 *
 * The current code uses a very ugly hack overriding hard_start_xmit
 * callback in the device structure, but it has been shown to give us a
 * performance boost of 60-100% though. Eventually we have to find some
 * less hacky way of getting this job done.
 */
#define VBOXNETFLT_WITH_HOST2WIRE_FILTER

#ifdef VBOXNETFLT_WITH_HOST2WIRE_FILTER

# if RTLNX_VER_MAX(2,6,29)

typedef struct ethtool_ops OVR_OPSTYPE;
# define OVR_OPS  ethtool_ops
# define OVR_XMIT pfnStartXmit

# else /* RTLNX_VER_MIN(2,6,29) */

typedef struct net_device_ops OVR_OPSTYPE;
# define OVR_OPS  netdev_ops
# define OVR_XMIT pOrgOps->ndo_start_xmit

# endif /* RTLNX_VER_MIN(2,6,29) */

/**
 * The overridden net_device_ops of the device we're attached to.
 *
 * As there is no net_device_ops structure in pre-2.6.29 kernels we override
 * ethtool_ops instead along with hard_start_xmit callback in net_device
 * structure.
 *
 * This is a very dirty hack that was created to explore how much we can improve
 * the host to guest transfers by not CC'ing the NIC. It turns out to be
 * the only way to filter outgoing packets for devices without TX queue.
 */
typedef struct VBoxNetDeviceOpsOverride
{
    /** Our overridden ops. */
    OVR_OPSTYPE                     Ops;
    /** Magic word. */
    uint32_t                        u32Magic;
    /** Pointer to the original ops. */
    OVR_OPSTYPE const              *pOrgOps;
# if RTLNX_VER_MAX(2,6,29)
    /** Pointer to the original hard_start_xmit function. */
    int (*pfnStartXmit)(struct sk_buff *pSkb, struct net_device *pDev);
# endif /* RTLNX_VER_MAX(2,6,29) */
    /** Pointer to the net filter instance. */
    PVBOXNETFLTINS                  pVBoxNetFlt;
    /** The number of filtered packages. */
    uint64_t                        cFiltered;
    /** The total number of packets */
    uint64_t                        cTotal;
} VBOXNETDEVICEOPSOVERRIDE, *PVBOXNETDEVICEOPSOVERRIDE;
/** VBOXNETDEVICEOPSOVERRIDE::u32Magic value. */
#define VBOXNETDEVICEOPSOVERRIDE_MAGIC  UINT32_C(0x00c0ffee)

/**
 * ndo_start_xmit wrapper that drops packets that shouldn't go to the wire
 * because they belong on the internal network.
 *
 * @returns NETDEV_TX_XXX.
 * @param   pSkb                The socket buffer to transmit.
 * @param   pDev                The net device.
 */
static int vboxNetFltLinuxStartXmitFilter(struct sk_buff *pSkb, struct net_device *pDev)
{
    PVBOXNETDEVICEOPSOVERRIDE   pOverride = (PVBOXNETDEVICEOPSOVERRIDE)pDev->OVR_OPS;
    uint8_t                     abHdrBuf[sizeof(RTNETETHERHDR) + sizeof(uint32_t) + RTNETIPV4_MIN_LEN];
    PCRTNETETHERHDR             pEtherHdr;
    PINTNETTRUNKSWPORT          pSwitchPort;
    uint32_t                    cbHdrs;


    /*
     * Validate the override structure.
     *
     * Note! We're racing vboxNetFltLinuxUnhookDev here.  If this was supposed
     *       to be production quality code, we would have to be much more
     *       careful here and avoid the race.
     */
    if (   !RT_VALID_PTR(pOverride)
        || pOverride->u32Magic != VBOXNETDEVICEOPSOVERRIDE_MAGIC
# if RTLNX_VER_MIN(2,6,29)
        || !RT_VALID_PTR(pOverride->pOrgOps)
# endif
        )
    {
        printk("vboxNetFltLinuxStartXmitFilter: bad override %p\n", pOverride);
        dev_kfree_skb(pSkb);
        return NETDEV_TX_OK;
    }
    pOverride->cTotal++;

    /*
     * Do the filtering base on the default OUI of our virtual NICs
     *
     * Note! In a real solution, we would ask the switch whether the
     *       destination MAC is 100% to be on the internal network and then
     *       drop it.
     */
    cbHdrs = skb_headlen(pSkb);
    cbHdrs = RT_MIN(cbHdrs, sizeof(abHdrBuf));
    pEtherHdr = (PCRTNETETHERHDR)skb_header_pointer(pSkb, 0, cbHdrs, &abHdrBuf[0]);
    if (   pEtherHdr
        && RT_VALID_PTR(pOverride->pVBoxNetFlt)
        && (pSwitchPort = pOverride->pVBoxNetFlt->pSwitchPort) != NULL
        && RT_VALID_PTR(pSwitchPort)
        && cbHdrs >= 6)
    {
        INTNETSWDECISION enmDecision;

        /** @todo consider reference counting, etc. */
        enmDecision = pSwitchPort->pfnPreRecv(pSwitchPort, pEtherHdr, cbHdrs, INTNETTRUNKDIR_HOST);
        if (enmDecision == INTNETSWDECISION_INTNET)
        {
            dev_kfree_skb(pSkb);
            pOverride->cFiltered++;
            return NETDEV_TX_OK;
        }
    }

    return pOverride->OVR_XMIT(pSkb, pDev);
}

/**
 * Hooks the device ndo_start_xmit operation of the device.
 *
 * @param   pThis               The net filter instance.
 * @param   pDev                The net device.
 */
static void vboxNetFltLinuxHookDev(PVBOXNETFLTINS pThis, struct net_device *pDev)
{
    PVBOXNETDEVICEOPSOVERRIDE   pOverride;

    /* Cancel override if ethtool_ops is missing (host-only case, @bugref{5712}) */
    if (!RT_VALID_PTR(pDev->OVR_OPS))
        return;
    /* Do not override netdev_ops for ixgbe driver as it causes panic, see Jira issue VBP-1774 */
    if (pDev->ethtool_ops && pDev->ethtool_ops->get_drvinfo)
    {
        struct ethtool_drvinfo Info;

        memset(&Info, 0, sizeof(Info));
        Info.cmd = ETHTOOL_GDRVINFO;
        pDev->ethtool_ops->get_drvinfo(pDev, &Info);
        if (strncmp(Info.driver, "ixgbe", 5) == 0)
        {
            printk("vboxnetflt: Skipping netdev_ops override for ixgbe driver, expect lower performance.\n");
            return;
        }
    }
    pOverride = RTMemAlloc(sizeof(*pOverride));
    if (!pOverride)
        return;
    pOverride->pOrgOps              = pDev->OVR_OPS;
    pOverride->Ops                  = *pDev->OVR_OPS;
# if RTLNX_VER_MAX(2,6,29)
    pOverride->pfnStartXmit         = pDev->hard_start_xmit;
# else /* RTLNX_VER_MIN(2,6,29) */
    pOverride->Ops.ndo_start_xmit   = vboxNetFltLinuxStartXmitFilter;
# endif /* RTLNX_VER_MIN(2,6,29) */
    pOverride->u32Magic             = VBOXNETDEVICEOPSOVERRIDE_MAGIC;
    pOverride->cTotal               = 0;
    pOverride->cFiltered            = 0;
    pOverride->pVBoxNetFlt          = pThis;

    RTSpinlockAcquire(pThis->hSpinlock); /* (this isn't necessary, but so what) */
    ASMAtomicWritePtr((void * volatile *)&pDev->OVR_OPS, pOverride);
# if RTLNX_VER_MAX(2,6,29)
    ASMAtomicXchgPtr((void * volatile *)&pDev->hard_start_xmit, vboxNetFltLinuxStartXmitFilter);
# endif /* RTLNX_VER_MAX(2,6,29) */
    RTSpinlockRelease(pThis->hSpinlock);
}

/**
 * Undos what vboxNetFltLinuxHookDev did.
 *
 * @param   pThis               The net filter instance.
 * @param   pDev                The net device.  Can be NULL, in which case
 *                              we'll try retrieve it from @a pThis.
 */
static void vboxNetFltLinuxUnhookDev(PVBOXNETFLTINS pThis, struct net_device *pDev)
{
    PVBOXNETDEVICEOPSOVERRIDE   pOverride;

    RTSpinlockAcquire(pThis->hSpinlock);
    if (!pDev)
        pDev = ASMAtomicUoReadPtrT(&pThis->u.s.pDev, struct net_device *);
    if (RT_VALID_PTR(pDev))
    {
        pOverride = (PVBOXNETDEVICEOPSOVERRIDE)pDev->OVR_OPS;
        if (   RT_VALID_PTR(pOverride)
            && pOverride->u32Magic == VBOXNETDEVICEOPSOVERRIDE_MAGIC
            && RT_VALID_PTR(pOverride->pOrgOps)
           )
        {
# if RTLNX_VER_MAX(2,6,29)
            ASMAtomicWritePtr((void * volatile *)&pDev->hard_start_xmit, pOverride->pfnStartXmit);
# endif /* RTLNX_VER_MAX(2,6,29) */
            ASMAtomicWritePtr((void const * volatile *)&pDev->OVR_OPS, pOverride->pOrgOps);
            ASMAtomicWriteU32(&pOverride->u32Magic, 0);
        }
        else
            pOverride = NULL;
    }
    else
        pOverride = NULL;
    RTSpinlockRelease(pThis->hSpinlock);

    if (pOverride)
    {
        printk("vboxnetflt: %llu out of %llu packets were not sent (directed to host)\n", pOverride->cFiltered, pOverride->cTotal);
        RTMemFree(pOverride);
    }
}

#endif /* VBOXNETFLT_WITH_HOST2WIRE_FILTER */


/**
 * Reads and retains the host interface handle.
 *
 * @returns The handle, NULL if detached.
 * @param   pThis
 */
DECLINLINE(struct net_device *) vboxNetFltLinuxRetainNetDev(PVBOXNETFLTINS pThis)
{
#if 0
    struct net_device *pDev = NULL;

    Log(("vboxNetFltLinuxRetainNetDev\n"));
    /*
     * Be careful here to avoid problems racing the detached callback.
     */
    RTSpinlockAcquire(pThis->hSpinlock);
    if (!ASMAtomicUoReadBool(&pThis->fDisconnectedFromHost))
    {
        pDev = (struct net_device *)ASMAtomicUoReadPtr((void * volatile *)&pThis->u.s.pDev);
        if (pDev)
        {
            dev_hold(pDev);
            Log(("vboxNetFltLinuxRetainNetDev: Device %p(%s) retained. ref=%d\n",
                 pDev, pDev->name,
#if RTLNX_VER_MIN(2,6,37)
                 netdev_refcnt_read(pDev)
#else
                 atomic_read(&pDev->refcnt)
#endif
                 ));
        }
    }
    RTSpinlockRelease(pThis->hSpinlock);

    Log(("vboxNetFltLinuxRetainNetDev - done\n"));
    return pDev;
#else
    return ASMAtomicUoReadPtrT(&pThis->u.s.pDev, struct net_device *);
#endif
}


/**
 * Release the host interface handle previously retained
 * by vboxNetFltLinuxRetainNetDev.
 *
 * @param   pThis           The instance.
 * @param   pDev            The vboxNetFltLinuxRetainNetDev
 *                          return value, NULL is fine.
 */
DECLINLINE(void) vboxNetFltLinuxReleaseNetDev(PVBOXNETFLTINS pThis, struct net_device *pDev)
{
#if 0
    Log(("vboxNetFltLinuxReleaseNetDev\n"));
    NOREF(pThis);
    if (pDev)
    {
        dev_put(pDev);
        Log(("vboxNetFltLinuxReleaseNetDev: Device %p(%s) released. ref=%d\n",
             pDev, pDev->name,
#if RTLNX_VER_MIN(2,6,37)
             netdev_refcnt_read(pDev)
#else
             atomic_read(&pDev->refcnt)
#endif
             ));
    }
    Log(("vboxNetFltLinuxReleaseNetDev - done\n"));
#endif
}

#define VBOXNETFLT_CB_TAG(skb) (0xA1C90000 | (skb->dev->ifindex & 0xFFFF))
#define VBOXNETFLT_SKB_TAG(skb) (*(uint32_t*)&((skb)->cb[sizeof((skb)->cb)-sizeof(uint32_t)]))

/**
 * Checks whether this is an mbuf created by vboxNetFltLinuxMBufFromSG,
 * i.e. a buffer which we're pushing and should be ignored by the filter callbacks.
 *
 * @returns true / false accordingly.
 * @param   pBuf            The sk_buff.
 */
DECLINLINE(bool) vboxNetFltLinuxSkBufIsOur(struct sk_buff *pBuf)
{
    return VBOXNETFLT_SKB_TAG(pBuf) == VBOXNETFLT_CB_TAG(pBuf);
}


/**
 * Checks whether this SG list contains a GSO packet.
 *
 * @returns true / false accordingly.
 * @param   pSG             The (scatter/)gather list.
 */
DECLINLINE(bool) vboxNetFltLinuxIsGso(PINTNETSG pSG)
{
#if defined(VBOXNETFLT_WITH_GSO_XMIT_WIRE) || defined(VBOXNETFLT_WITH_GSO_XMIT_HOST)
    return !((PDMNETWORKGSOTYPE)pSG->GsoCtx.u8Type == PDMNETWORKGSOTYPE_INVALID);
#else /* !VBOXNETFLT_WITH_GSO_XMIT_WIRE && !VBOXNETFLT_WITH_GSO_XMIT_HOST */
    return false;
#endif /* !VBOXNETFLT_WITH_GSO_XMIT_WIRE && !VBOXNETFLT_WITH_GSO_XMIT_HOST */
}


/**
 * Find out the frame size (of a single segment in case of GSO frames).
 *
 * @returns the frame size.
 * @param   pSG             The (scatter/)gather list.
 */
DECLINLINE(uint32_t) vboxNetFltLinuxFrameSize(PINTNETSG pSG)
{
    uint16_t u16Type = 0;
    uint32_t cbVlanTag = 0;
    if (pSG->aSegs[0].cb >= sizeof(RTNETETHERHDR))
        u16Type = RT_BE2H_U16(((PCRTNETETHERHDR)pSG->aSegs[0].pv)->EtherType);
    else if (pSG->cbTotal >= sizeof(RTNETETHERHDR))
    {
        uint32_t off = RT_UOFFSETOF(RTNETETHERHDR, EtherType);
        uint32_t i;
        for (i = 0; i < pSG->cSegsUsed; ++i)
        {
            if (off <= pSG->aSegs[i].cb)
            {
                if (off + sizeof(uint16_t) <= pSG->aSegs[i].cb)
                    u16Type = RT_BE2H_U16(*(uint16_t *)((uintptr_t)pSG->aSegs[i].pv + off));
                else if (i + 1 < pSG->cSegsUsed)
                    u16Type = RT_BE2H_U16(  ((uint16_t)( ((uint8_t *)pSG->aSegs[i].pv)[off] ) << 8)
                                          + *(uint8_t *)pSG->aSegs[i + 1].pv); /* ASSUMES no empty segments! */
                /* else: frame is too short. */
                break;
            }
            off -= pSG->aSegs[i].cb;
        }
    }
    if (u16Type == RTNET_ETHERTYPE_VLAN)
        cbVlanTag = 4;
    return (vboxNetFltLinuxIsGso(pSG) ? (uint32_t)pSG->GsoCtx.cbMaxSeg + pSG->GsoCtx.cbHdrsTotal : pSG->cbTotal) - cbVlanTag;
}


/**
 * Internal worker that create a linux sk_buff for a
 * (scatter/)gather list.
 *
 * @returns Pointer to the sk_buff.
 * @param   pThis           The instance.
 * @param   pSG             The (scatter/)gather list.
 * @param   fDstWire        Set if the destination is the wire.
 */
static struct sk_buff *vboxNetFltLinuxSkBufFromSG(PVBOXNETFLTINS pThis, PINTNETSG pSG, bool fDstWire)
{
    struct sk_buff *pPkt;
    struct net_device *pDev;
#if defined(VBOXNETFLT_WITH_GSO_XMIT_WIRE) || defined(VBOXNETFLT_WITH_GSO_XMIT_HOST)
    unsigned fGsoType = 0;
#endif

    if (pSG->cbTotal == 0)
    {
        LogRel(("VBoxNetFlt: Dropped empty packet coming from internal network.\n"));
        return NULL;
    }
    Log5(("VBoxNetFlt: Packet to %s of %d bytes (frame=%d).\n", fDstWire?"wire":"host", pSG->cbTotal, vboxNetFltLinuxFrameSize(pSG)));
    if (fDstWire && (vboxNetFltLinuxFrameSize(pSG) > ASMAtomicReadU32(&pThis->u.s.cbMtu) + 14))
    {
        static bool s_fOnce = true;
        if (s_fOnce)
        {
            s_fOnce = false;
            printk("VBoxNetFlt: Dropped over-sized packet (%d bytes) coming from internal network.\n", vboxNetFltLinuxFrameSize(pSG));
        }
        return NULL;
    }

    /** @todo We should use fragments mapping the SG buffers with large packets.
     *        256 bytes seems to be the a threshold used a lot for this.  It
     *        requires some nasty work on the intnet side though...  */
    /*
     * Allocate a packet and copy over the data.
     */
    pDev = ASMAtomicUoReadPtrT(&pThis->u.s.pDev, struct net_device *);
    pPkt = dev_alloc_skb(pSG->cbTotal + NET_IP_ALIGN);
    if (RT_UNLIKELY(!pPkt))
    {
        Log(("vboxNetFltLinuxSkBufFromSG: Failed to allocate sk_buff(%u).\n", pSG->cbTotal));
        pSG->pvUserData = NULL;
        return NULL;
    }
    pPkt->dev       = pDev;
    pPkt->ip_summed = CHECKSUM_NONE;

    /* Align IP header on 16-byte boundary: 2 + 14 (ethernet hdr size). */
    skb_reserve(pPkt, NET_IP_ALIGN);

    /* Copy the segments. */
    skb_put(pPkt, pSG->cbTotal);
    IntNetSgRead(pSG, pPkt->data);

#if defined(VBOXNETFLT_WITH_GSO_XMIT_WIRE) || defined(VBOXNETFLT_WITH_GSO_XMIT_HOST)
    /*
     * Setup GSO if used by this packet.
     */
    switch ((PDMNETWORKGSOTYPE)pSG->GsoCtx.u8Type)
    {
        default:
            AssertMsgFailed(("%u (%s)\n", pSG->GsoCtx.u8Type, PDMNetGsoTypeName((PDMNETWORKGSOTYPE)pSG->GsoCtx.u8Type) ));
            RT_FALL_THRU();
        case PDMNETWORKGSOTYPE_INVALID:
            fGsoType = 0;
            break;
        case PDMNETWORKGSOTYPE_IPV4_TCP:
            fGsoType = SKB_GSO_TCPV4;
            break;
        case PDMNETWORKGSOTYPE_IPV6_TCP:
            fGsoType = SKB_GSO_TCPV6;
            break;
    }
    if (fGsoType)
    {
        struct skb_shared_info *pShInfo = skb_shinfo(pPkt);

        pShInfo->gso_type = fGsoType | SKB_GSO_DODGY;
        pShInfo->gso_size = pSG->GsoCtx.cbMaxSeg;
        pShInfo->gso_segs = PDMNetGsoCalcSegmentCount(&pSG->GsoCtx, pSG->cbTotal);

        /*
         * We need to set checksum fields even if the packet goes to the host
         * directly as it may be immediately forwarded by IP layer @bugref{5020}.
         */
        Assert(skb_headlen(pPkt) >= pSG->GsoCtx.cbHdrsTotal);
        pPkt->ip_summed  = CHECKSUM_PARTIAL;
# if RTLNX_VER_MIN(2,6,22)
        pPkt->csum_start = skb_headroom(pPkt) + pSG->GsoCtx.offHdr2;
        if (fGsoType & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))
            pPkt->csum_offset = RT_UOFFSETOF(RTNETTCP, th_sum);
        else
            pPkt->csum_offset = RT_UOFFSETOF(RTNETUDP, uh_sum);
# else
        pPkt->h.raw = pPkt->data + pSG->GsoCtx.offHdr2;
        if (fGsoType & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))
            pPkt->csum = RT_UOFFSETOF(RTNETTCP, th_sum);
        else
            pPkt->csum = RT_UOFFSETOF(RTNETUDP, uh_sum);
# endif
        if (!fDstWire)
            PDMNetGsoPrepForDirectUse(&pSG->GsoCtx, pPkt->data, pSG->cbTotal, PDMNETCSUMTYPE_PSEUDO);
    }
#endif /* VBOXNETFLT_WITH_GSO_XMIT_WIRE || VBOXNETFLT_WITH_GSO_XMIT_HOST */

    /*
     * Finish up the socket buffer.
     */
    pPkt->protocol = eth_type_trans(pPkt, pDev);
    if (fDstWire)
    {
        VBOX_SKB_RESET_NETWORK_HDR(pPkt);

        /* Restore ethernet header back. */
        skb_push(pPkt, ETH_HLEN); /** @todo VLAN: +4 if VLAN? */
        VBOX_SKB_RESET_MAC_HDR(pPkt);
    }
    VBOXNETFLT_SKB_TAG(pPkt) = VBOXNETFLT_CB_TAG(pPkt);

    return pPkt;
}


/**
 * Return the offset where to start checksum computation from.
 *
 * @returns the offset relative to pBuf->data.
 * @param   pBuf                The socket buffer.
 */
DECLINLINE(unsigned) vboxNetFltLinuxGetChecksumStartOffset(struct sk_buff *pBuf)
{
#if RTLNX_VER_MIN(2,6,38)
    return skb_checksum_start_offset(pBuf);
#elif RTLNX_VER_MIN(2,6,22)
    return pBuf->csum_start - skb_headroom(pBuf);
#else
    unsigned char *pTransportHdr = pBuf->h.raw;
# if RTLNX_VER_MAX(2,6,19)
    /*
     * Try to work around the problem with CentOS 4.7 and 5.2 (2.6.9
     * and 2.6.18 kernels), they pass wrong 'h' pointer down. We take IP
     * header length from the header itself and reconstruct 'h' pointer
     * to TCP (or whatever) header.
     */
    if (pBuf->h.raw == pBuf->nh.raw && pBuf->protocol == htons(ETH_P_IP))
        pTransportHdr = pBuf->nh.raw + pBuf->nh.iph->ihl * 4;
# endif
    return pTransportHdr - pBuf->data;
#endif
}


/**
 * Initializes a SG list from an sk_buff.
 *
 * @param   pThis               The instance.
 * @param   pBuf                The sk_buff.
 * @param   pSG                 The SG.
 * @param   cbExtra             The number of bytes of extra space allocated immediately after the SG.
 * @param   cSegs               The number of segments allocated for the SG.
 *                              This should match the number in the mbuf exactly!
 * @param   fSrc                The source of the frame.
 * @param   pGsoCtx             Pointer to the GSO context if it's a GSO
 *                              internal network frame.  NULL if regular frame.
 */
static void vboxNetFltLinuxSkBufToSG(PVBOXNETFLTINS pThis, struct sk_buff *pBuf, PINTNETSG pSG,
                                     unsigned cbExtra, unsigned cSegs, uint32_t fSrc, PCPDMNETWORKGSO pGsoCtx)
{
    int i;
    NOREF(pThis);

#ifndef VBOXNETFLT_SG_SUPPORT
    Assert(!skb_shinfo(pBuf)->frag_list);
#else /* VBOXNETFLT_SG_SUPPORT */
    uint8_t *pExtra = (uint8_t *)&pSG->aSegs[cSegs];
    unsigned cbConsumed = 0;
    unsigned cbProduced = 0;

# if RTLNX_VER_MIN(2,6,27)
    /* Restore VLAN tag stripped by host hardware */
    if (vlan_tx_tag_present(pBuf))
    {
        uint8_t *pMac = pBuf->data;
        struct vlan_ethhdr *pVHdr = (struct vlan_ethhdr *)pExtra;
        Assert(ETH_ALEN * 2 + VLAN_HLEN <= cbExtra);
        memmove(pVHdr, pMac, ETH_ALEN * 2);
        /* Consume whole Ethernet header: 2 addresses + EtherType (see @bugref{8599}) */
        cbConsumed += ETH_ALEN * 2 + sizeof(uint16_t);
        pVHdr->h_vlan_proto = RT_H2N_U16(ETH_P_8021Q);
        pVHdr->h_vlan_TCI   = RT_H2N_U16(vlan_tx_tag_get(pBuf));
        pVHdr->h_vlan_encapsulated_proto = *(uint16_t*)(pMac + ETH_ALEN * 2);
        cbProduced += VLAN_ETH_HLEN;
    }
# endif /* RTLNX_VER_MIN(2,6,27) */

    if (pBuf->ip_summed == CHECKSUM_PARTIAL && pBuf->pkt_type == PACKET_OUTGOING)
    {
        unsigned uCsumStartOffset = vboxNetFltLinuxGetChecksumStartOffset(pBuf);
        unsigned uCsumStoreOffset = uCsumStartOffset + VBOX_SKB_CSUM_OFFSET(pBuf) - cbConsumed;
        Log3(("cbConsumed=%u cbProduced=%u uCsumStartOffset=%u uCsumStoreOffset=%u\n",
              cbConsumed, cbProduced, uCsumStartOffset, uCsumStoreOffset));
        Assert(cbProduced + uCsumStoreOffset + sizeof(uint16_t) <= cbExtra);
        /*
         * We assume that the checksum is stored at the very end of the transport header
         * so we will have all headers in a single fragment. If our assumption is wrong
         * we may see suboptimal performance.
         */
        memmove(pExtra + cbProduced,
                pBuf->data + cbConsumed,
                uCsumStoreOffset);
        unsigned uChecksum = skb_checksum(pBuf, uCsumStartOffset, pBuf->len - uCsumStartOffset, 0);
        *(uint16_t*)(pExtra + cbProduced + uCsumStoreOffset) = csum_fold(uChecksum);
        cbProduced += uCsumStoreOffset + sizeof(uint16_t);
        cbConsumed += uCsumStoreOffset + sizeof(uint16_t);
    }
#endif /* VBOXNETFLT_SG_SUPPORT */

    if (!pGsoCtx)
        IntNetSgInitTempSegs(pSG, pBuf->len + cbProduced - cbConsumed, cSegs, 0 /*cSegsUsed*/);
    else
        IntNetSgInitTempSegsGso(pSG, pBuf->len + cbProduced - cbConsumed, cSegs, 0 /*cSegsUsed*/, pGsoCtx);

    int iSeg = 0;
#ifdef VBOXNETFLT_SG_SUPPORT
    if (cbProduced)
    {
        pSG->aSegs[iSeg].cb = cbProduced;
        pSG->aSegs[iSeg].pv = pExtra;
        pSG->aSegs[iSeg++].Phys = NIL_RTHCPHYS;
    }
    pSG->aSegs[iSeg].cb = skb_headlen(pBuf) - cbConsumed;
    pSG->aSegs[iSeg].pv = pBuf->data + cbConsumed;
    pSG->aSegs[iSeg++].Phys = NIL_RTHCPHYS;
    Assert(iSeg <= pSG->cSegsAlloc);

# ifdef LOG_ENABLED
    if (pBuf->data_len)
        Log6(("  kmap_atomic:"));
# endif /* LOG_ENABLED */
    for (i = 0; i < skb_shinfo(pBuf)->nr_frags; i++)
    {
        skb_frag_t *pFrag = &skb_shinfo(pBuf)->frags[i];
        pSG->aSegs[iSeg].cb = VBOX_SKB_FRAG_LEN(pFrag);
        pSG->aSegs[iSeg].pv = VBOX_SKB_KMAP_FRAG(pFrag) + VBOX_SKB_FRAG_OFFSET(pFrag);
        Log6((" %p", pSG->aSegs[iSeg].pv));
        pSG->aSegs[iSeg++].Phys = NIL_RTHCPHYS;
        Assert(iSeg <= pSG->cSegsAlloc);
    }
    struct sk_buff *pFragBuf;
    for (pFragBuf = skb_shinfo(pBuf)->frag_list; pFragBuf; pFragBuf = pFragBuf->next)
    {
        pSG->aSegs[iSeg].cb = skb_headlen(pFragBuf);
        pSG->aSegs[iSeg].pv = pFragBuf->data;
        pSG->aSegs[iSeg++].Phys = NIL_RTHCPHYS;
        Assert(iSeg <= pSG->cSegsAlloc);
        for (i = 0; i < skb_shinfo(pFragBuf)->nr_frags; i++)
        {
            skb_frag_t *pFrag = &skb_shinfo(pFragBuf)->frags[i];
            pSG->aSegs[iSeg].cb = VBOX_SKB_FRAG_LEN(pFrag);
            pSG->aSegs[iSeg].pv = VBOX_SKB_KMAP_FRAG(pFrag) + VBOX_SKB_FRAG_OFFSET(pFrag);
            Log6((" %p", pSG->aSegs[iSeg].pv));
            pSG->aSegs[iSeg++].Phys = NIL_RTHCPHYS;
            Assert(iSeg <= pSG->cSegsAlloc);
        }
    }
# ifdef LOG_ENABLED
    if (pBuf->data_len)
        Log6(("\n"));
# endif /* LOG_ENABLED */
#else
    pSG->aSegs[iSeg].cb = pBuf->len;
    pSG->aSegs[iSeg].pv = pBuf->data;
    pSG->aSegs[iSeg++].Phys = NIL_RTHCPHYS;
#endif

    pSG->cSegsUsed = iSeg;

#if 0
    if (cbProduced)
    {
        LogRel(("vboxNetFltLinuxSkBufToSG: original packet dump:\n%.*Rhxd\n", pBuf->len-pBuf->data_len, skb_mac_header(pBuf)));
        LogRel(("vboxNetFltLinuxSkBufToSG: cbConsumed=%u cbProduced=%u cbExtra=%u\n", cbConsumed, cbProduced, cbExtra));
        uint32_t offset = 0;
        for (i = 0; i < pSG->cSegsUsed; ++i)
        {
            LogRel(("vboxNetFltLinuxSkBufToSG: seg#%d (%d bytes, starting at 0x%x):\n%.*Rhxd\n",
                    i, pSG->aSegs[i].cb, offset, pSG->aSegs[i].cb, pSG->aSegs[i].pv));
            offset += pSG->aSegs[i].cb;
        }
    }
#endif

#ifdef PADD_RUNT_FRAMES_FROM_HOST
    /*
     * Add a trailer if the frame is too small.
     *
     * Since we're getting to the packet before it is framed, it has not
     * yet been padded. The current solution is to add a segment pointing
     * to a buffer containing all zeros and pray that works for all frames...
     */
    if (pSG->cbTotal < 60 && (fSrc & INTNETTRUNKDIR_HOST))
    {
        Assert(pBuf->data_len == 0); /* Packets with fragments are never small! */
        static uint8_t const s_abZero[128] = {0};

        AssertReturnVoid(iSeg < cSegs);

        pSG->aSegs[iSeg].Phys = NIL_RTHCPHYS;
        pSG->aSegs[iSeg].pv = (void *)&s_abZero[0];
        pSG->aSegs[iSeg++].cb = 60 - pSG->cbTotal;
        pSG->cbTotal = 60;
        pSG->cSegsUsed++;
        Assert(iSeg <= pSG->cSegsAlloc)
    }
#endif

    Log6(("vboxNetFltLinuxSkBufToSG: allocated=%d, segments=%d frags=%d next=%p frag_list=%p pkt_type=%x fSrc=%x\n",
          pSG->cSegsAlloc, pSG->cSegsUsed, skb_shinfo(pBuf)->nr_frags, pBuf->next, skb_shinfo(pBuf)->frag_list, pBuf->pkt_type, fSrc));
    for (i = 0; i < pSG->cSegsUsed; i++)
        Log6(("vboxNetFltLinuxSkBufToSG:   #%d: cb=%d pv=%p\n",
              i, pSG->aSegs[i].cb, pSG->aSegs[i].pv));
}

/**
 * Packet handler; not really documented - figure it out yourself.
 *
 * @returns 0 or EJUSTRETURN - this is probably copy & pastry and thus wrong.
 */
#if RTLNX_VER_MIN(2,6,14)
static int vboxNetFltLinuxPacketHandler(struct sk_buff *pBuf,
                                        struct net_device *pSkbDev,
                                        struct packet_type *pPacketType,
                                        struct net_device *pOrigDev)
#else
static int vboxNetFltLinuxPacketHandler(struct sk_buff *pBuf,
                                        struct net_device *pSkbDev,
                                        struct packet_type *pPacketType)
#endif
{
    PVBOXNETFLTINS pThis;
    struct net_device *pDev;
    LogFlow(("vboxNetFltLinuxPacketHandler: pBuf=%p pSkbDev=%p pPacketType=%p\n",
             pBuf, pSkbDev, pPacketType));
#if RTLNX_VER_MIN(2,6,18)
    Log3(("vboxNetFltLinuxPacketHandler: skb len=%u data_len=%u truesize=%u next=%p nr_frags=%u gso_size=%u gso_seqs=%u gso_type=%x frag_list=%p pkt_type=%x\n",
          pBuf->len, pBuf->data_len, pBuf->truesize, pBuf->next, skb_shinfo(pBuf)->nr_frags, skb_shinfo(pBuf)->gso_size, skb_shinfo(pBuf)->gso_segs, skb_shinfo(pBuf)->gso_type, skb_shinfo(pBuf)->frag_list, pBuf->pkt_type));
# if RTLNX_VER_MIN(2,6,22)
    Log6(("vboxNetFltLinuxPacketHandler: packet dump follows:\n%.*Rhxd\n", pBuf->len-pBuf->data_len, skb_mac_header(pBuf)));
# endif
#else
    Log3(("vboxNetFltLinuxPacketHandler: skb len=%u data_len=%u truesize=%u next=%p nr_frags=%u tso_size=%u tso_seqs=%u frag_list=%p pkt_type=%x\n",
          pBuf->len, pBuf->data_len, pBuf->truesize, pBuf->next, skb_shinfo(pBuf)->nr_frags, skb_shinfo(pBuf)->tso_size, skb_shinfo(pBuf)->tso_segs, skb_shinfo(pBuf)->frag_list, pBuf->pkt_type));
#endif
    /*
     * Drop it immediately?
     */
    if (!pBuf)
        return 0;

    if (pBuf->pkt_type == PACKET_LOOPBACK)
    {
        /*
         * We are not interested in loopbacked packets as they will always have
         * another copy going to the wire.
         */
        Log2(("vboxNetFltLinuxPacketHandler: dropped loopback packet (cb=%u)\n", pBuf->len));
        dev_kfree_skb(pBuf); /* We must 'consume' all packets we get (@bugref{6539})! */
        return 0;
    }

    pThis = VBOX_FLT_PT_TO_INST(pPacketType);
    pDev = ASMAtomicUoReadPtrT(&pThis->u.s.pDev, struct net_device *);
    if (pDev != pSkbDev)
    {
        Log(("vboxNetFltLinuxPacketHandler: Devices do not match, pThis may be wrong! pThis=%p\n", pThis));
        kfree_skb(pBuf); /* This is a failure, so we use kfree_skb instead of dev_kfree_skb. */
        return 0;
    }

    Log6(("vboxNetFltLinuxPacketHandler: pBuf->cb dump:\n%.*Rhxd\n", sizeof(pBuf->cb), pBuf->cb));
    if (vboxNetFltLinuxSkBufIsOur(pBuf))
    {
        Log2(("vboxNetFltLinuxPacketHandler: got our own sk_buff, drop it.\n"));
        dev_kfree_skb(pBuf);
        return 0;
    }

#ifndef VBOXNETFLT_SG_SUPPORT
    {
        /*
         * Get rid of fragmented packets, they cause too much trouble.
         */
        unsigned int uMacLen = pBuf->mac_len;
        struct sk_buff *pCopy = skb_copy(pBuf, GFP_ATOMIC);
        dev_kfree_skb(pBuf);
        if (!pCopy)
        {
            LogRel(("VBoxNetFlt: Failed to allocate packet buffer, dropping the packet.\n"));
            return 0;
        }
        pBuf = pCopy;
        /* Somehow skb_copy ignores mac_len */
        pBuf->mac_len = uMacLen;
# if RTLNX_VER_MIN(2,6,27)
        /* Restore VLAN tag stripped by host hardware */
        if (vlan_tx_tag_present(pBuf) && skb_headroom(pBuf) >= VLAN_ETH_HLEN)
        {
            uint8_t *pMac = (uint8_t*)skb_mac_header(pBuf);
            struct vlan_ethhdr *pVHdr = (struct vlan_ethhdr *)(pMac - VLAN_HLEN);
            memmove(pVHdr, pMac, ETH_ALEN * 2);
            pVHdr->h_vlan_proto = RT_H2N_U16(ETH_P_8021Q);
            pVHdr->h_vlan_TCI   = RT_H2N_U16(vlan_tx_tag_get(pBuf));
            pBuf->mac_header   -= VLAN_HLEN;
            pBuf->mac_len      += VLAN_HLEN;
        }
# endif /* RTLNX_VER_MIN(2,6,27) */

# if RTLNX_VER_MIN(2,6,18)
        Log3(("vboxNetFltLinuxPacketHandler: skb copy len=%u data_len=%u truesize=%u next=%p nr_frags=%u gso_size=%u gso_seqs=%u gso_type=%x frag_list=%p pkt_type=%x\n",
              pBuf->len, pBuf->data_len, pBuf->truesize, pBuf->next, skb_shinfo(pBuf)->nr_frags, skb_shinfo(pBuf)->gso_size, skb_shinfo(pBuf)->gso_segs, skb_shinfo(pBuf)->gso_type, skb_shinfo(pBuf)->frag_list, pBuf->pkt_type));
#  if RTLNX_VER_MIN(2,6,22)
        Log6(("vboxNetFltLinuxPacketHandler: packet dump follows:\n%.*Rhxd\n", pBuf->len-pBuf->data_len, skb_mac_header(pBuf)));
#  endif /* RTLNX_VER_MIN(2,6,22) */
# else /* RTLNX_VER_MAX(2,6,18) */
        Log3(("vboxNetFltLinuxPacketHandler: skb copy len=%u data_len=%u truesize=%u next=%p nr_frags=%u tso_size=%u tso_seqs=%u frag_list=%p pkt_type=%x\n",
              pBuf->len, pBuf->data_len, pBuf->truesize, pBuf->next, skb_shinfo(pBuf)->nr_frags, skb_shinfo(pBuf)->tso_size, skb_shinfo(pBuf)->tso_segs, skb_shinfo(pBuf)->frag_list, pBuf->pkt_type));
# endif /* RTLNX_VER_MAX(2,6,18) */
    }
#endif /* !VBOXNETFLT_SG_SUPPORT */

#ifdef VBOXNETFLT_LINUX_NO_XMIT_QUEUE
    /* Forward it to the internal network. */
    vboxNetFltLinuxForwardToIntNet(pThis, pBuf);
#else /* !VBOXNETFLT_LINUX_NO_XMIT_QUEUE */
    /* Add the packet to transmit queue and schedule the bottom half. */
    skb_queue_tail(&pThis->u.s.XmitQueue, pBuf);
    schedule_work(&pThis->u.s.XmitTask);
    Log6(("vboxNetFltLinuxPacketHandler: scheduled work %p for sk_buff %p\n",
          &pThis->u.s.XmitTask, pBuf));
#endif /* !VBOXNETFLT_LINUX_NO_XMIT_QUEUE */

    /* It does not really matter what we return, it is ignored by the kernel. */
    return 0;
}

/**
 * Calculate the number of INTNETSEG segments the socket buffer will need.
 *
 * @returns Segment count.
 * @param   pBuf                The socket buffer.
 * @param   pcbTemp             Where to store the number of bytes of the part
 *                              of the socket buffer that will be copied to
 *                              a temporary storage.
 */
DECLINLINE(unsigned) vboxNetFltLinuxCalcSGSegments(struct sk_buff *pBuf, unsigned *pcbTemp)
{
    *pcbTemp = 0;
#ifdef VBOXNETFLT_SG_SUPPORT
    unsigned cSegs = 1 + skb_shinfo(pBuf)->nr_frags;
    if (pBuf->ip_summed == CHECKSUM_PARTIAL && pBuf->pkt_type == PACKET_OUTGOING)
    {
        *pcbTemp = vboxNetFltLinuxGetChecksumStartOffset(pBuf) + VBOX_SKB_CSUM_OFFSET(pBuf) + sizeof(uint16_t);
    }
# if RTLNX_VER_MIN(2,6,27)
    if (vlan_tx_tag_present(pBuf))
    {
        if (*pcbTemp)
            *pcbTemp += VLAN_HLEN;
        else
            *pcbTemp = VLAN_ETH_HLEN;
    }
# endif /* RTLNX_VER_MIN(2,6,27) */
    if (*pcbTemp)
        ++cSegs;
    struct sk_buff *pFrag;
    for (pFrag = skb_shinfo(pBuf)->frag_list; pFrag; pFrag = pFrag->next)
    {
        Log6(("vboxNetFltLinuxCalcSGSegments: frag=%p len=%d data_len=%d frags=%d frag_list=%p next=%p\n",
              pFrag, pFrag->len, pFrag->data_len, skb_shinfo(pFrag)->nr_frags, skb_shinfo(pFrag)->frag_list, pFrag->next));
        cSegs += 1 + skb_shinfo(pFrag)->nr_frags;
    }
#else
    unsigned cSegs = 1;
#endif
#ifdef PADD_RUNT_FRAMES_FROM_HOST
    /* vboxNetFltLinuxSkBufToSG adds a padding segment if it's a runt. */
    if (pBuf->len < 60)
        cSegs++;
#endif
    return cSegs;
}


/**
 * Destroy the intnet scatter / gather buffer created by
 * vboxNetFltLinuxSkBufToSG.
 *
 * @param   pSG             The (scatter/)gather list.
 * @param   pBuf            The original socket buffer that was used to create
 *                          the scatter/gather list.
 */
static void vboxNetFltLinuxDestroySG(PINTNETSG pSG, struct sk_buff *pBuf)
{
#ifdef VBOXNETFLT_SG_SUPPORT
    int i, iSeg = 1; /* Skip non-paged part of SKB */
    /* Check if the extra buffer behind SG structure was used for modified packet header */
    if (pBuf->data != pSG->aSegs[0].pv)
        ++iSeg; /* Skip it as well */
# ifdef LOG_ENABLED
    if (pBuf->data_len)
        Log6(("kunmap_atomic:"));
# endif /* LOG_ENABLED */
    /* iSeg now points to the first mapped fragment if there are any */
    for (i = 0; i < skb_shinfo(pBuf)->nr_frags; i++)
    {
        Log6((" %p", pSG->aSegs[iSeg].pv));
        VBOX_SKB_KUNMAP_FRAG(pSG->aSegs[iSeg++].pv);
    }
    struct sk_buff *pFragBuf;
    for (pFragBuf = skb_shinfo(pBuf)->frag_list; pFragBuf; pFragBuf = pFragBuf->next)
    {
        ++iSeg; /* Non-fragment (unmapped) portion of chained SKB */
        for (i = 0; i < skb_shinfo(pFragBuf)->nr_frags; i++)
        {
            Log6((" %p", pSG->aSegs[iSeg].pv));
            VBOX_SKB_KUNMAP_FRAG(pSG->aSegs[iSeg++].pv);
        }
    }
# ifdef LOG_ENABLED
    if (pBuf->data_len)
        Log6(("\n"));
# endif /* LOG_ENABLED */
#endif
    NOREF(pSG);
}

#ifdef LOG_ENABLED
/**
 * Logging helper.
 */
static void vboxNetFltDumpPacket(PINTNETSG pSG, bool fEgress, const char *pszWhere, int iIncrement)
{
    int i, offSeg;
    uint8_t *pInt, *pExt;
    static int iPacketNo = 1;
    iPacketNo += iIncrement;
    if (fEgress)
    {
        pExt = pSG->aSegs[0].pv;
        pInt = pExt + 6;
    }
    else
    {
        pInt = pSG->aSegs[0].pv;
        pExt = pInt + 6;
    }
    Log(("VBoxNetFlt: (int)%02x:%02x:%02x:%02x:%02x:%02x"
         " %s (%s)%02x:%02x:%02x:%02x:%02x:%02x (%u bytes) packet #%u\n",
         pInt[0], pInt[1], pInt[2], pInt[3], pInt[4], pInt[5],
         fEgress ? "-->" : "<--", pszWhere,
         pExt[0], pExt[1], pExt[2], pExt[3], pExt[4], pExt[5],
         pSG->cbTotal, iPacketNo));
    if (pSG->cSegsUsed == 1)
    {
        Log4(("%.*Rhxd\n", pSG->aSegs[0].cb, pSG->aSegs[0].pv));
    }
    else
    {
        for (i = 0, offSeg = 0; i < pSG->cSegsUsed; i++)
        {
            Log4(("-- segment %d at 0x%x (%d bytes)\n --\n%.*Rhxd\n",
                  i, offSeg, pSG->aSegs[i].cb, pSG->aSegs[i].cb, pSG->aSegs[i].pv));
            offSeg += pSG->aSegs[i].cb;
        }
    }
}
#else
# define vboxNetFltDumpPacket(a, b, c, d) do {} while (0)
#endif

#ifdef VBOXNETFLT_WITH_GSO_RECV

/**
 * Worker for vboxNetFltLinuxForwardToIntNet that checks if we can forwards a
 * GSO socket buffer without having to segment it.
 *
 * @returns true on success, false if needs segmenting.
 * @param   pThis               The net filter instance.
 * @param   pSkb                The GSO socket buffer.
 * @param   fSrc                The source.
 * @param   pGsoCtx             Where to return the GSO context on success.
 */
static bool vboxNetFltLinuxCanForwardAsGso(PVBOXNETFLTINS pThis, struct sk_buff *pSkb, uint32_t fSrc,
                                           PPDMNETWORKGSO pGsoCtx)
{
    PDMNETWORKGSOTYPE   enmGsoType;
    uint16_t            uEtherType;
    unsigned int        cbTransport;
    unsigned int        offTransport;
    unsigned int        cbTransportHdr;
    unsigned            uProtocol;
    union
    {
        RTNETIPV4           IPv4;
        RTNETIPV6           IPv6;
        RTNETTCP            Tcp;
        uint8_t             ab[40];
        uint16_t            au16[40/2];
        uint32_t            au32[40/4];
    }                   Buf;

    /*
     * Check the GSO properties of the socket buffer and make sure it fits.
     */
    /** @todo Figure out how to handle SKB_GSO_TCP_ECN! */
    if (RT_UNLIKELY( skb_shinfo(pSkb)->gso_type & ~(SKB_GSO_DODGY | SKB_GSO_TCPV6 | SKB_GSO_TCPV4) ))
    {
        Log5(("vboxNetFltLinuxCanForwardAsGso: gso_type=%#x\n", skb_shinfo(pSkb)->gso_type));
        return false;
    }
    if (RT_UNLIKELY(   skb_shinfo(pSkb)->gso_size < 1
                    || pSkb->len > VBOX_MAX_GSO_SIZE ))
    {
        Log5(("vboxNetFltLinuxCanForwardAsGso: gso_size=%#x skb_len=%#x (max=%#x)\n", skb_shinfo(pSkb)->gso_size, pSkb->len, VBOX_MAX_GSO_SIZE));
        return false;
    }

    /*
     * Switch on the ethertype.
     */
    uEtherType = pSkb->protocol;
    if (   uEtherType    == RT_H2N_U16_C(RTNET_ETHERTYPE_VLAN)
        && pSkb->mac_len == sizeof(RTNETETHERHDR) + sizeof(uint32_t))
    {
        uint16_t const *puEtherType = skb_header_pointer(pSkb, sizeof(RTNETETHERHDR) + sizeof(uint16_t), sizeof(uint16_t), &Buf);
        if (puEtherType)
            uEtherType = *puEtherType;
    }
    switch (uEtherType)
    {
        case RT_H2N_U16_C(RTNET_ETHERTYPE_IPV4):
        {
            unsigned int cbHdr;
            PCRTNETIPV4  pIPv4 = (PCRTNETIPV4)skb_header_pointer(pSkb, pSkb->mac_len, sizeof(Buf.IPv4), &Buf);
            if (RT_UNLIKELY(!pIPv4))
            {
                Log5(("vboxNetFltLinuxCanForwardAsGso: failed to access IPv4 hdr\n"));
                return false;
            }

            cbHdr       = pIPv4->ip_hl * 4;
            cbTransport = RT_N2H_U16(pIPv4->ip_len);
            if (RT_UNLIKELY(   cbHdr < RTNETIPV4_MIN_LEN
                            || cbHdr > cbTransport ))
            {
                Log5(("vboxNetFltLinuxCanForwardAsGso: invalid IPv4 lengths: ip_hl=%u ip_len=%u\n", pIPv4->ip_hl, RT_N2H_U16(pIPv4->ip_len)));
                return false;
            }
            cbTransport -= cbHdr;
            offTransport = pSkb->mac_len + cbHdr;
            uProtocol    = pIPv4->ip_p;
            if (uProtocol == RTNETIPV4_PROT_TCP)
                enmGsoType = PDMNETWORKGSOTYPE_IPV4_TCP;
            else if (uProtocol == RTNETIPV4_PROT_UDP)
                enmGsoType = PDMNETWORKGSOTYPE_IPV4_UDP;
            else /** @todo IPv6: 4to6 tunneling */
                enmGsoType = PDMNETWORKGSOTYPE_INVALID;
            break;
        }

        case RT_H2N_U16_C(RTNET_ETHERTYPE_IPV6):
        {
            PCRTNETIPV6 pIPv6 = (PCRTNETIPV6)skb_header_pointer(pSkb, pSkb->mac_len, sizeof(Buf.IPv6), &Buf);
            if (RT_UNLIKELY(!pIPv6))
            {
                Log5(("vboxNetFltLinuxCanForwardAsGso: failed to access IPv6 hdr\n"));
                return false;
            }

            cbTransport  = RT_N2H_U16(pIPv6->ip6_plen);
            offTransport = pSkb->mac_len + sizeof(RTNETIPV6);
            uProtocol    = pIPv6->ip6_nxt;
            /** @todo IPv6: Dig our way out of the other headers. */
            if (uProtocol == RTNETIPV4_PROT_TCP)
                enmGsoType = PDMNETWORKGSOTYPE_IPV6_TCP;
            else if (uProtocol == RTNETIPV4_PROT_UDP)
                enmGsoType = PDMNETWORKGSOTYPE_IPV6_UDP;
            else
                enmGsoType = PDMNETWORKGSOTYPE_INVALID;
            break;
        }

        default:
            Log5(("vboxNetFltLinuxCanForwardAsGso: uEtherType=%#x\n", RT_H2N_U16(uEtherType)));
            return false;
    }

    if (enmGsoType == PDMNETWORKGSOTYPE_INVALID)
    {
        Log5(("vboxNetFltLinuxCanForwardAsGso: Unsupported protocol %d\n", uProtocol));
        return false;
    }

    if (RT_UNLIKELY(   offTransport + cbTransport <= offTransport
                    || offTransport + cbTransport > pSkb->len
                    || cbTransport < (uProtocol == RTNETIPV4_PROT_TCP ? RTNETTCP_MIN_LEN : RTNETUDP_MIN_LEN)) )
    {
        Log5(("vboxNetFltLinuxCanForwardAsGso: Bad transport length; off=%#x + cb=%#x => %#x; skb_len=%#x (%s)\n",
              offTransport, cbTransport, offTransport + cbTransport, pSkb->len, PDMNetGsoTypeName(enmGsoType) ));
        return false;
    }

    /*
     * Check the TCP/UDP bits.
     */
    if (uProtocol == RTNETIPV4_PROT_TCP)
    {
        PCRTNETTCP pTcp = (PCRTNETTCP)skb_header_pointer(pSkb, offTransport, sizeof(Buf.Tcp), &Buf);
        if (RT_UNLIKELY(!pTcp))
        {
            Log5(("vboxNetFltLinuxCanForwardAsGso: failed to access TCP hdr\n"));
            return false;
        }

        cbTransportHdr = pTcp->th_off * 4;
        pGsoCtx->cbHdrsSeg = offTransport + cbTransportHdr;
        if (RT_UNLIKELY(   cbTransportHdr < RTNETTCP_MIN_LEN
                        || cbTransportHdr > cbTransport
                        || offTransport + cbTransportHdr >= UINT8_MAX
                        || offTransport + cbTransportHdr >= pSkb->len ))
        {
            Log5(("vboxNetFltLinuxCanForwardAsGso: No space for TCP header; off=%#x cb=%#x skb_len=%#x\n", offTransport, cbTransportHdr, pSkb->len));
            return false;
        }

    }
    else
    {
        Assert(uProtocol == RTNETIPV4_PROT_UDP);
        cbTransportHdr = sizeof(RTNETUDP);
        pGsoCtx->cbHdrsSeg = offTransport; /* Exclude UDP header */
        if (RT_UNLIKELY(   offTransport + cbTransportHdr >= UINT8_MAX
                        || offTransport + cbTransportHdr >= pSkb->len ))
        {
            Log5(("vboxNetFltLinuxCanForwardAsGso: No space for UDP header; off=%#x skb_len=%#x\n", offTransport, pSkb->len));
            return false;
        }
    }

    /*
     * We're good, init the GSO context.
     */
    pGsoCtx->u8Type       = enmGsoType;
    pGsoCtx->cbHdrsTotal  = offTransport + cbTransportHdr;
    pGsoCtx->cbMaxSeg     = skb_shinfo(pSkb)->gso_size;
    pGsoCtx->offHdr1      = pSkb->mac_len;
    pGsoCtx->offHdr2      = offTransport;
    pGsoCtx->u8Unused     = 0;

    return true;
}

/**
 * Forward the socket buffer as a GSO internal network frame.
 *
 * @returns IPRT status code.
 * @param   pThis               The net filter instance.
 * @param   pSkb                The GSO socket buffer.
 * @param   fSrc                The source.
 * @param   pGsoCtx             Where to return the GSO context on success.
 */
static int vboxNetFltLinuxForwardAsGso(PVBOXNETFLTINS pThis, struct sk_buff *pSkb, uint32_t fSrc, PCPDMNETWORKGSO pGsoCtx)
{
    int         rc;
    unsigned    cbExtra;
    unsigned    cSegs = vboxNetFltLinuxCalcSGSegments(pSkb, &cbExtra);
    PINTNETSG pSG = (PINTNETSG)alloca(RT_UOFFSETOF_DYN(INTNETSG, aSegs[cSegs]) + cbExtra);
    if (RT_LIKELY(pSG))
    {
        vboxNetFltLinuxSkBufToSG(pThis, pSkb, pSG, cbExtra, cSegs, fSrc, pGsoCtx);

        vboxNetFltDumpPacket(pSG, false, (fSrc & INTNETTRUNKDIR_HOST) ? "host" : "wire", 1);
        pThis->pSwitchPort->pfnRecv(pThis->pSwitchPort, NULL /* pvIf */, pSG, fSrc);

        vboxNetFltLinuxDestroySG(pSG, pSkb);
        rc = VINF_SUCCESS;
    }
    else
    {
        Log(("VBoxNetFlt: Dropping the sk_buff (failure case).\n"));
        rc = VERR_NO_MEMORY;
    }
    return rc;
}

#endif /* VBOXNETFLT_WITH_GSO_RECV */

/**
 * Worker for vboxNetFltLinuxForwardToIntNet.
 *
 * @returns VINF_SUCCESS or VERR_NO_MEMORY.
 * @param   pThis               The net filter instance.
 * @param   pBuf                The socket buffer.
 * @param   fSrc                The source.
 */
static int vboxNetFltLinuxForwardSegment(PVBOXNETFLTINS pThis, struct sk_buff *pBuf, uint32_t fSrc)
{
    int         rc;
    unsigned    cbExtra;
    unsigned    cSegs = vboxNetFltLinuxCalcSGSegments(pBuf, &cbExtra);
    PINTNETSG pSG = (PINTNETSG)alloca(RT_UOFFSETOF_DYN(INTNETSG, aSegs[cSegs]) + cbExtra);
    if (RT_LIKELY(pSG))
    {
        vboxNetFltLinuxSkBufToSG(pThis, pBuf, pSG, cbExtra, cSegs, fSrc, NULL /*pGsoCtx*/);

        vboxNetFltDumpPacket(pSG, false, (fSrc & INTNETTRUNKDIR_HOST) ? "host" : "wire", 1);
        pThis->pSwitchPort->pfnRecv(pThis->pSwitchPort, NULL /* pvIf */, pSG, fSrc);

        vboxNetFltLinuxDestroySG(pSG, pBuf);
        rc = VINF_SUCCESS;
    }
    else
    {
        Log(("VBoxNetFlt: Failed to allocate SG buffer.\n"));
        rc = VERR_NO_MEMORY;
    }
    return rc;
}


/**
 * I won't disclose what I do, figure it out yourself, including pThis referencing.
 *
 * @param   pThis       The net filter instance.
 * @param   pBuf        The socket buffer.
 * @param   fSrc        Where the packet comes from.
 */
static void vboxNetFltLinuxForwardToIntNetInner(PVBOXNETFLTINS pThis, struct sk_buff *pBuf, uint32_t fSrc)
{
#ifdef VBOXNETFLT_WITH_GSO
    if (skb_is_gso(pBuf))
    {
        PDMNETWORKGSO GsoCtx;
        Log6(("vboxNetFltLinuxForwardToIntNetInner: skb len=%u data_len=%u truesize=%u next=%p"
              " nr_frags=%u gso_size=%u gso_seqs=%u gso_type=%x frag_list=%p pkt_type=%x ip_summed=%d\n",
              pBuf->len, pBuf->data_len, pBuf->truesize, pBuf->next,
              skb_shinfo(pBuf)->nr_frags, skb_shinfo(pBuf)->gso_size,
              skb_shinfo(pBuf)->gso_segs, skb_shinfo(pBuf)->gso_type,
              skb_shinfo(pBuf)->frag_list, pBuf->pkt_type, pBuf->ip_summed));

        if (RT_LIKELY(fSrc & INTNETTRUNKDIR_HOST))
        {
            /*
             * skb_gso_segment does the following. Do we need to do it as well?
             */
# if RTLNX_VER_MIN(2,6,22)
            skb_reset_mac_header(pBuf);
            pBuf->mac_len = pBuf->network_header - pBuf->mac_header;
# else
            pBuf->mac.raw = pBuf->data;
            pBuf->mac_len = pBuf->nh.raw - pBuf->data;
# endif
        }

# ifdef VBOXNETFLT_WITH_GSO_RECV
        if (   (skb_shinfo(pBuf)->gso_type & (SKB_GSO_TCPV6 | SKB_GSO_TCPV4))
            && vboxNetFltLinuxCanForwardAsGso(pThis, pBuf, fSrc, &GsoCtx) )
            vboxNetFltLinuxForwardAsGso(pThis, pBuf, fSrc, &GsoCtx);
        else
# endif /* VBOXNETFLT_WITH_GSO_RECV */
        {
            /* Need to segment the packet */
            struct sk_buff *pNext;
            struct sk_buff *pSegment = skb_gso_segment(pBuf, 0 /*supported features*/);
            if (IS_ERR(pSegment))
            {
                LogRel(("VBoxNetFlt: Failed to segment a packet (%d).\n", PTR_ERR(pSegment)));
                return;
            }

            for (; pSegment; pSegment = pNext)
            {
                Log6(("vboxNetFltLinuxForwardToIntNetInner: segment len=%u data_len=%u truesize=%u next=%p"
                      " nr_frags=%u gso_size=%u gso_seqs=%u gso_type=%x frag_list=%p pkt_type=%x\n",
                      pSegment->len, pSegment->data_len, pSegment->truesize, pSegment->next,
                      skb_shinfo(pSegment)->nr_frags, skb_shinfo(pSegment)->gso_size,
                      skb_shinfo(pSegment)->gso_segs, skb_shinfo(pSegment)->gso_type,
                      skb_shinfo(pSegment)->frag_list, pSegment->pkt_type));
                pNext = pSegment->next;
                pSegment->next = 0;
                vboxNetFltLinuxForwardSegment(pThis, pSegment, fSrc);
                dev_kfree_skb(pSegment);
            }
        }
    }
    else
#endif /* VBOXNETFLT_WITH_GSO */
    {
        Log6(("vboxNetFltLinuxForwardToIntNetInner: ptk_type=%d ip_summed=%d len=%d"
              " data_len=%d headroom=%d hdr_len=%d csum_offset=%d\n",
              pBuf->pkt_type, pBuf->ip_summed, pBuf->len, pBuf->data_len, skb_headroom(pBuf),
              skb_headlen(pBuf), vboxNetFltLinuxGetChecksumStartOffset(pBuf)));
#ifndef VBOXNETFLT_SG_SUPPORT
        if (pBuf->ip_summed == CHECKSUM_PARTIAL && pBuf->pkt_type == PACKET_OUTGOING)
        {
# if RTLNX_VER_MIN(2,6,19)
            int rc = VBOX_SKB_CHECKSUM_HELP(pBuf);
# else
            /*
             * Try to work around the problem with CentOS 4.7 and 5.2 (2.6.9
             * and 2.6.18 kernels), they pass wrong 'h' pointer down. We take IP
             * header length from the header itself and reconstruct 'h' pointer
             * to TCP (or whatever) header.
             */
            unsigned char *tmp = pBuf->h.raw;
            if (pBuf->h.raw == pBuf->nh.raw && pBuf->protocol == htons(ETH_P_IP))
                pBuf->h.raw = pBuf->nh.raw + pBuf->nh.iph->ihl * 4;
            int rc = VBOX_SKB_CHECKSUM_HELP(pBuf);
            /* Restore the original (wrong) pointer. */
            pBuf->h.raw = tmp;
# endif
            if (rc)
            {
                LogRel(("VBoxNetFlt: Failed to compute checksum, dropping the packet.\n"));
                return;
            }
        }
#endif /* !VBOXNETFLT_SG_SUPPORT */
        vboxNetFltLinuxForwardSegment(pThis, pBuf, fSrc);
    }
}


/**
 * Temporarily adjust pBuf->data so it always points to the Ethernet header,
 * then forward it to the internal network.
 *
 * @param   pThis       The net filter instance.
 * @param   pBuf        The socket buffer.  This is consumed by this function.
 */
static void vboxNetFltLinuxForwardToIntNet(PVBOXNETFLTINS pThis, struct sk_buff *pBuf)
{
    uint32_t fSrc = pBuf->pkt_type == PACKET_OUTGOING ? INTNETTRUNKDIR_HOST : INTNETTRUNKDIR_WIRE;

    if (RT_UNLIKELY(fSrc & INTNETTRUNKDIR_WIRE))
    {
        /*
         * The packet came from the wire and the driver has already consumed
         * mac header. We need to restore it back. Moreover, after we are
         * through with this skb we need to restore its original state!
         */
        skb_push(pBuf, pBuf->mac_len);
        Log5(("vboxNetFltLinuxForwardToIntNet: mac_len=%d data=%p mac_header=%p network_header=%p\n",
              pBuf->mac_len, pBuf->data, skb_mac_header(pBuf), skb_network_header(pBuf)));
    }

    vboxNetFltLinuxForwardToIntNetInner(pThis, pBuf, fSrc);

    /*
     * Restore the original state of skb as there are other handlers this skb
     * will be provided to.
     */
    if (RT_UNLIKELY(fSrc & INTNETTRUNKDIR_WIRE))
        skb_pull(pBuf, pBuf->mac_len);

    dev_kfree_skb(pBuf);
}


#ifndef VBOXNETFLT_LINUX_NO_XMIT_QUEUE
/**
 * Work queue handler that forwards the socket buffers queued by
 * vboxNetFltLinuxPacketHandler to the internal network.
 *
 * @param   pWork               The work queue.
 */
# if RTLNX_VER_MIN(2,6,20)
static void vboxNetFltLinuxXmitTask(struct work_struct *pWork)
# else
static void vboxNetFltLinuxXmitTask(void *pWork)
# endif
{
    PVBOXNETFLTINS  pThis   = VBOX_FLT_XT_TO_INST(pWork);
    struct sk_buff *pBuf;

    Log6(("vboxNetFltLinuxXmitTask: Got work %p.\n", pWork));

    /*
     * Active? Retain the instance and increment the busy counter.
     */
    if (vboxNetFltTryRetainBusyActive(pThis))
    {
        while ((pBuf = skb_dequeue(&pThis->u.s.XmitQueue)) != NULL)
            vboxNetFltLinuxForwardToIntNet(pThis, pBuf);

        vboxNetFltRelease(pThis, true /* fBusy */);
    }
    else
    {
        /** @todo Shouldn't we just drop the packets here? There is little point in
         *        making them accumulate when the VM is paused and it'll only waste
         *        kernel memory anyway... Hmm. maybe wait a short while (2-5 secs)
         *        before start draining the packets (goes for the intnet ring buf
         *        too)? */
    }
}
#endif /* !VBOXNETFLT_LINUX_NO_XMIT_QUEUE */

/**
 * Reports the GSO capabilities of the hardware NIC.
 *
 * @param   pThis               The net filter instance.  The caller hold a
 *                              reference to this.
 */
static void vboxNetFltLinuxReportNicGsoCapabilities(PVBOXNETFLTINS pThis)
{
#if defined(VBOXNETFLT_WITH_GSO_XMIT_WIRE) || defined(VBOXNETFLT_WITH_GSO_XMIT_HOST)
    if (vboxNetFltTryRetainBusyNotDisconnected(pThis))
    {
        struct net_device  *pDev;
        unsigned int        fFeatures;

        RTSpinlockAcquire(pThis->hSpinlock);

        pDev = ASMAtomicUoReadPtrT(&pThis->u.s.pDev, struct net_device *);
        if (pDev)
            fFeatures = pDev->features;
        else
            fFeatures = 0;

        RTSpinlockRelease(pThis->hSpinlock);

        if (pThis->pSwitchPort)
        {
            /* Set/update the GSO capabilities of the NIC. */
            uint32_t fGsoCapabilites = 0;
            if (fFeatures & NETIF_F_TSO)
                fGsoCapabilites |= RT_BIT_32(PDMNETWORKGSOTYPE_IPV4_TCP);
            if (fFeatures & NETIF_F_TSO6)
                fGsoCapabilites |= RT_BIT_32(PDMNETWORKGSOTYPE_IPV6_TCP);
            Log3(("vboxNetFltLinuxReportNicGsoCapabilities: reporting wire %s%s\n",
                  (fGsoCapabilites & RT_BIT_32(PDMNETWORKGSOTYPE_IPV4_TCP)) ? "tso " : "",
                  (fGsoCapabilites & RT_BIT_32(PDMNETWORKGSOTYPE_IPV6_TCP)) ? "tso6 " : ""));
            pThis->pSwitchPort->pfnReportGsoCapabilities(pThis->pSwitchPort, fGsoCapabilites, INTNETTRUNKDIR_WIRE);
        }

        vboxNetFltRelease(pThis, true /*fBusy*/);
    }
#endif /* VBOXNETFLT_WITH_GSO_XMIT_WIRE || VBOXNETFLT_WITH_GSO_XMIT_HOST */
}

/**
 * Helper that determines whether the host (ignoreing us) is operating the
 * interface in promiscuous mode or not.
 */
static bool vboxNetFltLinuxPromiscuous(PVBOXNETFLTINS pThis)
{
    bool                fRc  = false;
    struct net_device * pDev = vboxNetFltLinuxRetainNetDev(pThis);
    if (pDev)
    {
        fRc = !!(pDev->promiscuity - (ASMAtomicUoReadBool(&pThis->u.s.fPromiscuousSet) & 1));
        LogFlow(("vboxNetFltPortOsIsPromiscuous: returns %d, pDev->promiscuity=%d, fPromiscuousSet=%d\n",
                 fRc, pDev->promiscuity, pThis->u.s.fPromiscuousSet));
        vboxNetFltLinuxReleaseNetDev(pThis, pDev);
    }
    return fRc;
}

/**
 * Does this device needs link state change signaled?
 * Currently we need it for our own VBoxNetAdp and TAP.
 */
static bool vboxNetFltNeedsLinkState(PVBOXNETFLTINS pThis, struct net_device *pDev)
{
    if (pDev->ethtool_ops && pDev->ethtool_ops->get_drvinfo)
    {
        struct ethtool_drvinfo Info;

        memset(&Info, 0, sizeof(Info));
        Info.cmd = ETHTOOL_GDRVINFO;
        pDev->ethtool_ops->get_drvinfo(pDev, &Info);
        Log3(("%s: driver=%.*s version=%.*s bus_info=%.*s\n",
              __FUNCTION__,
              sizeof(Info.driver),   Info.driver,
              sizeof(Info.version),  Info.version,
              sizeof(Info.bus_info), Info.bus_info));

        if (!strncmp(Info.driver, "vboxnet", sizeof(Info.driver)))
            return true;

#if RTLNX_VER_MIN(2,6,36) /* TAP started doing carrier */
        return !strncmp(Info.driver,   "tun", 4)
            && !strncmp(Info.bus_info, "tap", 4);
#endif
    }

    return false;
}

#if RTLNX_VER_MAX(2,6,18)
DECLINLINE(void) netif_tx_lock_bh(struct net_device *pDev)
{
    spin_lock_bh(&pDev->xmit_lock);
}

DECLINLINE(void) netif_tx_unlock_bh(struct net_device *pDev)
{
    spin_unlock_bh(&pDev->xmit_lock);
}
#endif

/**
 * Some devices need link state change when filter attaches/detaches
 * since the filter is their link in a sense.
 */
static void vboxNetFltSetLinkState(PVBOXNETFLTINS pThis, struct net_device *pDev, bool fLinkUp)
{
    if (vboxNetFltNeedsLinkState(pThis, pDev))
    {
        Log3(("%s: bringing device link %s\n",
              __FUNCTION__, fLinkUp ? "up" : "down"));
        netif_tx_lock_bh(pDev);
        if (fLinkUp)
            netif_carrier_on(pDev);
        else
            netif_carrier_off(pDev);
        netif_tx_unlock_bh(pDev);
    }
}

/**
 * Internal worker for vboxNetFltLinuxNotifierCallback.
 *
 * @returns VBox status code.
 * @param   pThis           The instance.
 * @param   pDev            The device to attach to.
 */
static int vboxNetFltLinuxAttachToInterface(PVBOXNETFLTINS pThis, struct net_device *pDev)
{
    bool fAlreadyAttached = false;
    LogFlow(("vboxNetFltLinuxAttachToInterface: pThis=%p (%s)\n", pThis, pThis->szName));

    /*
     * Retain and store the device.
     */
    dev_hold(pDev);

    RTSpinlockAcquire(pThis->hSpinlock);
    if (ASMAtomicUoReadPtrT(&pThis->u.s.pDev, struct net_device *))
        fAlreadyAttached = true;    /* Do not attach multiple times! */
    else
        ASMAtomicUoWritePtr(&pThis->u.s.pDev, pDev);
    RTSpinlockRelease(pThis->hSpinlock);

    if (fAlreadyAttached)
    {
        dev_put(pDev);
        Log(("vboxNetFltLinuxAttachToInterface: Not attaching to %p(%s), already attached to %p(%s).\n",
             pDev, pDev->name, pThis->u.s.pDev, pThis->u.s.pDev->name));
        return VINF_ALREADY_INITIALIZED;
    }

    Log(("vboxNetFltLinuxAttachToInterface: Device %p(%s) retained. ref=%d\n",
          pDev, pDev->name,
#if RTLNX_VER_MIN(2,6,37)
          netdev_refcnt_read(pDev)
#else
          atomic_read(&pDev->refcnt)
#endif
          ));
    Log(("vboxNetFltLinuxAttachToInterface: Got pDev=%p pThis=%p pThis->u.s.pDev=%p\n",
          pDev, pThis, ASMAtomicUoReadPtrT(&pThis->u.s.pDev, struct net_device *)));

    /* Get the mac address while we still have a valid net_device reference. */
    memcpy(&pThis->u.s.MacAddr, pDev->dev_addr, sizeof(pThis->u.s.MacAddr));
    /* Initialize MTU */
    pThis->u.s.cbMtu = pDev->mtu;

    /*
     * Install a packet filter for this device with a protocol wildcard (ETH_P_ALL).
     */
    pThis->u.s.PacketType.type = __constant_htons(ETH_P_ALL);
    pThis->u.s.PacketType.dev  = pDev;
    pThis->u.s.PacketType.func = vboxNetFltLinuxPacketHandler;
    dev_add_pack(&pThis->u.s.PacketType);
    ASMAtomicUoWriteBool(&pThis->u.s.fPacketHandler, true);
    Log(("vboxNetFltLinuxAttachToInterface: this=%p: Packet handler installed.\n", pThis));

#ifdef VBOXNETFLT_WITH_HOST2WIRE_FILTER
    vboxNetFltLinuxHookDev(pThis, pDev);
#endif

    /*
     * Are we the "carrier" for this device (e.g. vboxnet or tap)?
     */
    vboxNetFltSetLinkState(pThis, pDev, true);

    /*
     * Set indicators that require the spinlock. Be abit paranoid about racing
     * the device notification handle.
     */
    RTSpinlockAcquire(pThis->hSpinlock);
    pDev = ASMAtomicUoReadPtrT(&pThis->u.s.pDev, struct net_device *);
    if (pDev)
    {
        ASMAtomicUoWriteBool(&pThis->fDisconnectedFromHost, false);
        ASMAtomicUoWriteBool(&pThis->u.s.fRegistered, true);
        pDev = NULL; /* don't dereference it */
    }
    RTSpinlockRelease(pThis->hSpinlock);

    /*
     * Report GSO capabilities
     */
    Assert(pThis->pSwitchPort);
    if (vboxNetFltTryRetainBusyNotDisconnected(pThis))
    {
        vboxNetFltLinuxReportNicGsoCapabilities(pThis);
        pThis->pSwitchPort->pfnReportMacAddress(pThis->pSwitchPort, &pThis->u.s.MacAddr);
        pThis->pSwitchPort->pfnReportPromiscuousMode(pThis->pSwitchPort, vboxNetFltLinuxPromiscuous(pThis));
        pThis->pSwitchPort->pfnReportNoPreemptDsts(pThis->pSwitchPort, INTNETTRUNKDIR_WIRE | INTNETTRUNKDIR_HOST);
        vboxNetFltRelease(pThis, true /*fBusy*/);
    }

    LogRel(("VBoxNetFlt: attached to '%s' / %RTmac\n", pThis->szName, &pThis->u.s.MacAddr));
    return VINF_SUCCESS;
}


static int vboxNetFltLinuxUnregisterDevice(PVBOXNETFLTINS pThis, struct net_device *pDev)
{
    bool fRegistered;
    Assert(!pThis->fDisconnectedFromHost);

#ifdef VBOXNETFLT_WITH_HOST2WIRE_FILTER
    vboxNetFltLinuxUnhookDev(pThis, pDev);
#endif

    if (ASMAtomicCmpXchgBool(&pThis->u.s.fPacketHandler, false, true))
    {
        dev_remove_pack(&pThis->u.s.PacketType);
        Log(("vboxNetFltLinuxUnregisterDevice: this=%p: packet handler removed.\n", pThis));
    }

    RTSpinlockAcquire(pThis->hSpinlock);
    fRegistered = ASMAtomicXchgBool(&pThis->u.s.fRegistered, false);
    if (fRegistered)
    {
        ASMAtomicWriteBool(&pThis->fDisconnectedFromHost, true);
        ASMAtomicUoWriteNullPtr(&pThis->u.s.pDev);
    }
    RTSpinlockRelease(pThis->hSpinlock);

    if (fRegistered)
    {
#ifndef VBOXNETFLT_LINUX_NO_XMIT_QUEUE
        skb_queue_purge(&pThis->u.s.XmitQueue);
#endif
        Log(("vboxNetFltLinuxUnregisterDevice: this=%p: xmit queue purged.\n", pThis));
        Log(("vboxNetFltLinuxUnregisterDevice: Device %p(%s) released. ref=%d\n",
             pDev, pDev->name,
#if RTLNX_VER_MIN(2,6,37)
             netdev_refcnt_read(pDev)
#else
             atomic_read(&pDev->refcnt)
#endif
           ));
        dev_put(pDev);
    }

    return NOTIFY_OK;
}

static int vboxNetFltLinuxDeviceIsUp(PVBOXNETFLTINS pThis, struct net_device *pDev)
{
    /* Check if we are not suspended and promiscuous mode has not been set. */
    if (   pThis->enmTrunkState == INTNETTRUNKIFSTATE_ACTIVE
        && !ASMAtomicUoReadBool(&pThis->u.s.fPromiscuousSet))
    {
        /* Note that there is no need for locking as the kernel got hold of the lock already. */
        dev_set_promiscuity(pDev, 1);
        ASMAtomicWriteBool(&pThis->u.s.fPromiscuousSet, true);
        Log(("vboxNetFltLinuxDeviceIsUp: enabled promiscuous mode on %s (%d)\n", pThis->szName, pDev->promiscuity));
    }
    else
        Log(("vboxNetFltLinuxDeviceIsUp: no need to enable promiscuous mode on %s (%d)\n", pThis->szName, pDev->promiscuity));
    return NOTIFY_OK;
}

static int vboxNetFltLinuxDeviceGoingDown(PVBOXNETFLTINS pThis, struct net_device *pDev)
{
    /* Undo promiscuous mode if we has set it. */
    if (ASMAtomicUoReadBool(&pThis->u.s.fPromiscuousSet))
    {
        /* Note that there is no need for locking as the kernel got hold of the lock already. */
        dev_set_promiscuity(pDev, -1);
        ASMAtomicWriteBool(&pThis->u.s.fPromiscuousSet, false);
        Log(("vboxNetFltLinuxDeviceGoingDown: disabled promiscuous mode on %s (%d)\n", pThis->szName, pDev->promiscuity));
    }
    else
        Log(("vboxNetFltLinuxDeviceGoingDown: no need to disable promiscuous mode on %s (%d)\n", pThis->szName, pDev->promiscuity));
    return NOTIFY_OK;
}

/**
 * Callback for listening to MTU change event.
 *
 * We need to track changes of host's inteface MTU to discard over-sized frames
 * coming from the internal network as they may hang the TX queue of host's
 * adapter.
 *
 * @returns NOTIFY_OK
 * @param   pThis               The netfilter instance.
 * @param   pDev                Pointer to device structure of host's interface.
 */
static int vboxNetFltLinuxDeviceMtuChange(PVBOXNETFLTINS pThis, struct net_device *pDev)
{
    ASMAtomicWriteU32(&pThis->u.s.cbMtu, pDev->mtu);
    Log(("vboxNetFltLinuxDeviceMtuChange: set MTU for %s to %d\n", pThis->szName, pDev->mtu));
    return NOTIFY_OK;
}

#ifdef LOG_ENABLED
/** Stringify the NETDEV_XXX constants. */
static const char *vboxNetFltLinuxGetNetDevEventName(unsigned long ulEventType)
{
    const char *pszEvent = "NETDEV_<unknown>";
    switch (ulEventType)
    {
        case NETDEV_REGISTER: pszEvent = "NETDEV_REGISTER"; break;
        case NETDEV_UNREGISTER: pszEvent = "NETDEV_UNREGISTER"; break;
        case NETDEV_UP: pszEvent = "NETDEV_UP"; break;
        case NETDEV_DOWN: pszEvent = "NETDEV_DOWN"; break;
        case NETDEV_REBOOT: pszEvent = "NETDEV_REBOOT"; break;
        case NETDEV_CHANGENAME: pszEvent = "NETDEV_CHANGENAME"; break;
        case NETDEV_CHANGE: pszEvent = "NETDEV_CHANGE"; break;
        case NETDEV_CHANGEMTU: pszEvent = "NETDEV_CHANGEMTU"; break;
        case NETDEV_CHANGEADDR: pszEvent = "NETDEV_CHANGEADDR"; break;
        case NETDEV_GOING_DOWN: pszEvent = "NETDEV_GOING_DOWN"; break;
# ifdef NETDEV_FEAT_CHANGE
        case NETDEV_FEAT_CHANGE: pszEvent = "NETDEV_FEAT_CHANGE"; break;
# endif
    }
    return pszEvent;
}
#endif /* LOG_ENABLED */

/**
 * Callback for listening to netdevice events.
 *
 * This works the rediscovery, clean up on unregistration, promiscuity on
 * up/down, and GSO feature changes from ethtool.
 *
 * @returns NOTIFY_OK
 * @param   self                Pointer to our notifier registration block.
 * @param   ulEventType         The event.
 * @param   ptr                 Event specific, but it is usually the device it
 *                              relates to.
 */
static int vboxNetFltLinuxNotifierCallback(struct notifier_block *self, unsigned long ulEventType, void *ptr)

{
    PVBOXNETFLTINS      pThis = VBOX_FLT_NB_TO_INST(self);
    struct net_device  *pMyDev = ASMAtomicUoReadPtrT(&pThis->u.s.pDev, struct net_device *);
    struct net_device  *pDev  = VBOX_NETDEV_NOTIFIER_INFO_TO_DEV(ptr);
    int                 rc    = NOTIFY_OK;

    Log(("VBoxNetFlt: got event %s(0x%lx) on %s, pDev=%p pThis=%p pThis->u.s.pDev=%p\n",
         vboxNetFltLinuxGetNetDevEventName(ulEventType), ulEventType, pDev->name, pDev, pThis, pMyDev));

    if (ulEventType == NETDEV_REGISTER)
    {
#ifdef VBOXNETFLT_LINUX_NAMESPACE_SUPPORT
# if RTLNX_VER_MIN(3,19,0) /* ns_common introduced */
#  define VBOX_DEV_NET_NS_INUM(dev) dev_net(dev)->ns.inum
# else
#  define VBOX_DEV_NET_NS_INUM(dev) dev_net(dev)->proc_inum
# endif
        if (pThis->u.s.uNamespaceInode == 0 || pThis->u.s.uNamespaceInode == VBOX_DEV_NET_NS_INUM(pDev))
        {
            /* Skip namespace if it is present */
            const char *pcszIfName = strchr(pThis->szName, '/');
            if (pcszIfName)
                ++pcszIfName;
            else
                pcszIfName = pThis->szName;
            if (strcmp(pDev->name, pcszIfName) == 0)
                vboxNetFltLinuxAttachToInterface(pThis, pDev);
            else
                Log(("VBoxNetFlt: not attaching to '%s' as it does not match '%s'\n", pDev->name, pcszIfName));
        }
        else
            Log(("VBoxNetFlt: ignoring '%s' in wrong namespace (%u, expected %u)\n", pDev->name,
                 VBOX_DEV_NET_NS_INUM(pDev), pThis->u.s.uNamespaceInode));
#else /* !VBOXNETFLT_LINUX_NAMESPACE_SUPPORT */
#if RTLNX_VER_MIN(2,6,24) /* cgroups/namespaces introduced */
# if RTLNX_VER_MIN(2,6,26)
#  define VBOX_DEV_NET(dev)             dev_net(dev)
#  define VBOX_NET_EQ(n1, n2)           net_eq((n1), (n2))
# else
#  define VBOX_DEV_NET(dev)             ((dev)->nd_net)
#  define VBOX_NET_EQ(n1, n2)           ((n1) == (n2))
# endif
        struct net *pMyNet = current->nsproxy->net_ns;
        struct net *pDevNet = VBOX_DEV_NET(pDev);

        if (VBOX_NET_EQ(pDevNet, pMyNet))
#endif  /* namespaces */
        {
            if (strcmp(pDev->name, pThis->szName) == 0)
            {
                vboxNetFltLinuxAttachToInterface(pThis, pDev);
            }
        }
#endif /* !VBOXNETFLT_LINUX_NAMESPACE_SUPPORT */
    }
    else
    {
        if (pDev == pMyDev)
        {
            switch (ulEventType)
            {
                case NETDEV_UNREGISTER:
                    rc = vboxNetFltLinuxUnregisterDevice(pThis, pDev);
                    break;
                case NETDEV_UP:
                    rc = vboxNetFltLinuxDeviceIsUp(pThis, pDev);
                    break;
                case NETDEV_GOING_DOWN:
                    rc = vboxNetFltLinuxDeviceGoingDown(pThis, pDev);
                    break;
                case NETDEV_CHANGEMTU:
                    rc = vboxNetFltLinuxDeviceMtuChange(pThis, pDev);
                    break;
                case NETDEV_CHANGENAME:
                    break;
#ifdef NETDEV_FEAT_CHANGE
                case NETDEV_FEAT_CHANGE:
                    vboxNetFltLinuxReportNicGsoCapabilities(pThis);
                    break;
#endif
            }
        }
    }

    return rc;
}

/*
 * Initial enumeration of netdevs.  Called with NETDEV_REGISTER by
 * register_netdevice_notifier() under rtnl lock.
 */
static int vboxNetFltLinuxEnumeratorCallback(struct notifier_block *self, unsigned long ulEventType, void *ptr)
{
    PVBOXNETFLTINS pThis = ((PVBOXNETFLTNOTIFIER)self)->pThis;
    struct net_device *dev  = VBOX_NETDEV_NOTIFIER_INFO_TO_DEV(ptr);
    struct in_device *in_dev;
    struct inet6_dev *in6_dev;

    if (ulEventType != NETDEV_REGISTER)
        return NOTIFY_OK;

    if (RT_UNLIKELY(pThis->pSwitchPort->pfnNotifyHostAddress == NULL))
        return NOTIFY_OK;

    /*
     * IPv4
     */
#if RTLNX_VER_MIN(2,6,14)
    in_dev = __in_dev_get_rtnl(dev);
#else
    in_dev = __in_dev_get(dev);
#endif
    if (in_dev != NULL)
    {
        struct in_ifaddr *ifa;

        for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
            if (VBOX_IPV4_IS_LOOPBACK(ifa->ifa_address))
                return NOTIFY_OK;

            if (   dev != pThis->u.s.pDev
                && VBOX_IPV4_IS_LINKLOCAL_169(ifa->ifa_address))
                continue;

            Log(("%s: %s: IPv4 addr %RTnaipv4 mask %RTnaipv4\n",
                 __FUNCTION__, VBOX_NETDEV_NAME(dev),
                 ifa->ifa_address, ifa->ifa_mask));

            pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort,
                /* :fAdded */ true, kIntNetAddrType_IPv4, &ifa->ifa_address);
        }
    }

    /*
     * IPv6
     */
    in6_dev = __in6_dev_get(dev);
    if (in6_dev != NULL)
    {
        struct inet6_ifaddr *ifa;

        read_lock_bh(&in6_dev->lock);
#if RTLNX_VER_MIN(2,6,35)
        list_for_each_entry(ifa, &in6_dev->addr_list, if_list)
#else
        for (ifa = in6_dev->addr_list; ifa != NULL; ifa = ifa->if_next)
#endif
        {
            if (   dev != pThis->u.s.pDev
                && ipv6_addr_type(&ifa->addr) & (IPV6_ADDR_LINKLOCAL | IPV6_ADDR_LOOPBACK))
                continue;

            Log(("%s: %s: IPv6 addr %RTnaipv6/%u\n",
                 __FUNCTION__, VBOX_NETDEV_NAME(dev),
                 &ifa->addr, (unsigned)ifa->prefix_len));

            pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort,
                /* :fAdded */ true, kIntNetAddrType_IPv6, &ifa->addr);
        }
        read_unlock_bh(&in6_dev->lock);
    }

    return NOTIFY_OK;
}


static int vboxNetFltLinuxNotifierIPv4Callback(struct notifier_block *self, unsigned long ulEventType, void *ptr)
{
    PVBOXNETFLTINS     pThis = RT_FROM_MEMBER(self, VBOXNETFLTINS, u.s.NotifierIPv4);
    struct net_device *pDev, *pEventDev;
    struct in_ifaddr  *ifa   = (struct in_ifaddr *)ptr;
    bool               fMyDev;
    int                rc    = NOTIFY_OK;

    pDev = vboxNetFltLinuxRetainNetDev(pThis);
    pEventDev = ifa->ifa_dev->dev;
    fMyDev = (pDev == pEventDev);
    Log(("VBoxNetFlt: %s: IPv4 event %s(0x%lx) %s: addr %RTnaipv4 mask %RTnaipv4\n",
         pDev ? VBOX_NETDEV_NAME(pDev) : "<unknown>",
         vboxNetFltLinuxGetNetDevEventName(ulEventType), ulEventType,
         pEventDev ? VBOX_NETDEV_NAME(pEventDev) : "<unknown>",
         ifa->ifa_address, ifa->ifa_mask));

    if (pDev != NULL)
        vboxNetFltLinuxReleaseNetDev(pThis, pDev);

    if (VBOX_IPV4_IS_LOOPBACK(ifa->ifa_address))
        return NOTIFY_OK;

    if (   !fMyDev
        && VBOX_IPV4_IS_LINKLOCAL_169(ifa->ifa_address))
        return NOTIFY_OK;

    if (pThis->pSwitchPort->pfnNotifyHostAddress)
    {
        bool fAdded;
        if (ulEventType == NETDEV_UP)
            fAdded = true;
        else if (ulEventType == NETDEV_DOWN)
            fAdded = false;
        else
            return NOTIFY_OK;

        pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort, fAdded,
                                                 kIntNetAddrType_IPv4, &ifa->ifa_local);
    }

    return rc;
}


static int vboxNetFltLinuxNotifierIPv6Callback(struct notifier_block *self, unsigned long ulEventType, void *ptr)
{
    PVBOXNETFLTINS       pThis = RT_FROM_MEMBER(self, VBOXNETFLTINS, u.s.NotifierIPv6);
    struct net_device   *pDev, *pEventDev;
    struct inet6_ifaddr *ifa   = (struct inet6_ifaddr *)ptr;
    bool                 fMyDev;
    int                  rc    = NOTIFY_OK;

    pDev = vboxNetFltLinuxRetainNetDev(pThis);
    pEventDev = ifa->idev->dev;
    fMyDev = (pDev == pEventDev);
    Log(("VBoxNetFlt: %s: IPv6 event %s(0x%lx) %s: %RTnaipv6\n",
         pDev ? VBOX_NETDEV_NAME(pDev) : "<unknown>",
         vboxNetFltLinuxGetNetDevEventName(ulEventType), ulEventType,
         pEventDev ? VBOX_NETDEV_NAME(pEventDev) : "<unknown>",
         &ifa->addr));

    if (pDev != NULL)
        vboxNetFltLinuxReleaseNetDev(pThis, pDev);

    if (   !fMyDev
        && ipv6_addr_type(&ifa->addr) & (IPV6_ADDR_LINKLOCAL | IPV6_ADDR_LOOPBACK))
        return NOTIFY_OK;

    if (pThis->pSwitchPort->pfnNotifyHostAddress)
    {
        bool fAdded;
        if (ulEventType == NETDEV_UP)
            fAdded = true;
        else if (ulEventType == NETDEV_DOWN)
            fAdded = false;
        else
            return NOTIFY_OK;

        pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort, fAdded,
                                                 kIntNetAddrType_IPv6, &ifa->addr);
    }

    return rc;
}


bool vboxNetFltOsMaybeRediscovered(PVBOXNETFLTINS pThis)
{
    return !ASMAtomicUoReadBool(&pThis->fDisconnectedFromHost);
}

int  vboxNetFltPortOsXmit(PVBOXNETFLTINS pThis, void *pvIfData, PINTNETSG pSG, uint32_t fDst)
{
    struct net_device * pDev;
    int err;
    int rc = VINF_SUCCESS;
    IPRT_LINUX_SAVE_EFL_AC();
    NOREF(pvIfData);

    LogFlow(("vboxNetFltPortOsXmit: pThis=%p (%s)\n", pThis, pThis->szName));

    pDev = vboxNetFltLinuxRetainNetDev(pThis);
    if (pDev)
    {
        /*
         * Create a sk_buff for the gather list and push it onto the wire.
         */
        if (fDst & INTNETTRUNKDIR_WIRE)
        {
            struct sk_buff *pBuf = vboxNetFltLinuxSkBufFromSG(pThis, pSG, true);
            if (pBuf)
            {
                vboxNetFltDumpPacket(pSG, true, "wire", 1);
                Log6(("vboxNetFltPortOsXmit: pBuf->cb dump:\n%.*Rhxd\n", sizeof(pBuf->cb), pBuf->cb));
                Log6(("vboxNetFltPortOsXmit: dev_queue_xmit(%p)\n", pBuf));
                err = dev_queue_xmit(pBuf);
                if (err)
                    rc = RTErrConvertFromErrno(err);
            }
            else
                rc = VERR_NO_MEMORY;
        }

        /*
         * Create a sk_buff for the gather list and push it onto the host stack.
         */
        if (fDst & INTNETTRUNKDIR_HOST)
        {
            struct sk_buff *pBuf = vboxNetFltLinuxSkBufFromSG(pThis, pSG, false);
            if (pBuf)
            {
                vboxNetFltDumpPacket(pSG, true, "host", (fDst & INTNETTRUNKDIR_WIRE) ? 0 : 1);
                Log6(("vboxNetFltPortOsXmit: pBuf->cb dump:\n%.*Rhxd\n", sizeof(pBuf->cb), pBuf->cb));
                Log6(("vboxNetFltPortOsXmit: netif_rx_ni(%p)\n", pBuf));
#if RTLNX_VER_MIN(5,18,0) || RTLNX_RHEL_MIN(9,1)
                local_bh_disable();
                err = netif_rx(pBuf);
                local_bh_enable();
#else
                err = netif_rx_ni(pBuf);
#endif
                if (err)
                    rc = RTErrConvertFromErrno(err);
            }
            else
                rc = VERR_NO_MEMORY;
        }

        vboxNetFltLinuxReleaseNetDev(pThis, pDev);
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


void vboxNetFltPortOsSetActive(PVBOXNETFLTINS pThis, bool fActive)
{
    struct net_device *pDev;
    IPRT_LINUX_SAVE_EFL_AC();

    LogFlow(("vboxNetFltPortOsSetActive: pThis=%p (%s), fActive=%RTbool, fDisablePromiscuous=%RTbool\n",
             pThis, pThis->szName, fActive, pThis->fDisablePromiscuous));

    if (pThis->fDisablePromiscuous)
        return;

    pDev = vboxNetFltLinuxRetainNetDev(pThis);
    if (pDev)
    {
        /*
         * This api is a bit weird, the best reference is the code.
         *
         * Also, we have a bit or race conditions wrt the maintenance of
         * host the interface promiscuity for vboxNetFltPortOsIsPromiscuous.
         */
#ifdef LOG_ENABLED
        u_int16_t fIf;
        unsigned const cPromiscBefore = pDev->promiscuity;
#endif
        if (fActive)
        {
            Assert(!pThis->u.s.fPromiscuousSet);

            rtnl_lock();
            dev_set_promiscuity(pDev, 1);
            rtnl_unlock();
            pThis->u.s.fPromiscuousSet = true;
            Log(("vboxNetFltPortOsSetActive: enabled promiscuous mode on %s (%d)\n", pThis->szName, pDev->promiscuity));
        }
        else
        {
            if (pThis->u.s.fPromiscuousSet)
            {
                rtnl_lock();
                dev_set_promiscuity(pDev, -1);
                rtnl_unlock();
                Log(("vboxNetFltPortOsSetActive: disabled promiscuous mode on %s (%d)\n", pThis->szName, pDev->promiscuity));
            }
            pThis->u.s.fPromiscuousSet = false;

#ifdef LOG_ENABLED
            fIf = dev_get_flags(pDev);
            Log(("VBoxNetFlt: fIf=%#x; %d->%d\n", fIf, cPromiscBefore, pDev->promiscuity));
#endif
        }

        vboxNetFltLinuxReleaseNetDev(pThis, pDev);
    }
    IPRT_LINUX_RESTORE_EFL_AC();
}


int vboxNetFltOsDisconnectIt(PVBOXNETFLTINS pThis)
{
    /*
     * Remove packet handler when we get disconnected from internal switch as
     * we don't want the handler to forward packets to disconnected switch.
     */
    if (ASMAtomicCmpXchgBool(&pThis->u.s.fPacketHandler, false, true))
    {
        IPRT_LINUX_SAVE_EFL_AC();
        dev_remove_pack(&pThis->u.s.PacketType);
        Log(("vboxNetFltOsDisconnectIt: this=%p: Packet handler removed.\n", pThis));
        IPRT_LINUX_RESTORE_EFL_AC();
    }
    return VINF_SUCCESS;
}


int  vboxNetFltOsConnectIt(PVBOXNETFLTINS pThis)
{
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Report the GSO capabilities of the host and device (if connected).
     * Note! No need to mark ourselves busy here.
     */
    /** @todo duplicate work here now? Attach */
#if defined(VBOXNETFLT_WITH_GSO_XMIT_HOST)
    Log3(("vboxNetFltOsConnectIt: reporting host tso tso6\n"));
    pThis->pSwitchPort->pfnReportGsoCapabilities(pThis->pSwitchPort,
                                                 0
                                                 | RT_BIT_32(PDMNETWORKGSOTYPE_IPV4_TCP)
                                                 | RT_BIT_32(PDMNETWORKGSOTYPE_IPV6_TCP)
                                                 , INTNETTRUNKDIR_HOST);

#endif
    vboxNetFltLinuxReportNicGsoCapabilities(pThis);

    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


void vboxNetFltOsDeleteInstance(PVBOXNETFLTINS pThis)
{
    struct net_device  *pDev;
    bool                fRegistered;
    IPRT_LINUX_SAVE_EFL_AC();

#ifdef VBOXNETFLT_WITH_HOST2WIRE_FILTER
    vboxNetFltLinuxUnhookDev(pThis, NULL);
#endif

    /** @todo This code may race vboxNetFltLinuxUnregisterDevice (very very
     *        unlikely, but none the less).  Since it doesn't actually update the
     *        state (just reads it), it is likely to panic in some interesting
     *        ways. */

    RTSpinlockAcquire(pThis->hSpinlock);
    pDev = ASMAtomicUoReadPtrT(&pThis->u.s.pDev, struct net_device *);
    fRegistered = ASMAtomicXchgBool(&pThis->u.s.fRegistered, false);
    RTSpinlockRelease(pThis->hSpinlock);

    if (fRegistered)
    {
        vboxNetFltSetLinkState(pThis, pDev, false);

#ifndef VBOXNETFLT_LINUX_NO_XMIT_QUEUE
        skb_queue_purge(&pThis->u.s.XmitQueue);
#endif
        Log(("vboxNetFltOsDeleteInstance: this=%p: xmit queue purged.\n", pThis));
        Log(("vboxNetFltOsDeleteInstance: Device %p(%s) released. ref=%d\n",
             pDev, pDev->name,
#if RTLNX_VER_MIN(2,6,37)
             netdev_refcnt_read(pDev)
#else
             atomic_read(&pDev->refcnt)
#endif
             ));
        dev_put(pDev);
    }

    unregister_inet6addr_notifier(&pThis->u.s.NotifierIPv6);
    unregister_inetaddr_notifier(&pThis->u.s.NotifierIPv4);

    Log(("vboxNetFltOsDeleteInstance: this=%p: Notifier removed.\n", pThis));
    unregister_netdevice_notifier(&pThis->u.s.Notifier);
    module_put(THIS_MODULE);

    IPRT_LINUX_RESTORE_EFL_AC();
}


int  vboxNetFltOsInitInstance(PVBOXNETFLTINS pThis, void *pvContext)
{
    int err;
    IPRT_LINUX_SAVE_EFL_AC();
    NOREF(pvContext);

    pThis->u.s.Notifier.notifier_call = vboxNetFltLinuxNotifierCallback;
    err = register_netdevice_notifier(&pThis->u.s.Notifier);
    if (err)
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_INTNET_FLT_IF_FAILED;
    }
    if (!pThis->u.s.fRegistered)
    {
        unregister_netdevice_notifier(&pThis->u.s.Notifier);
        LogRel(("VBoxNetFlt: failed to find %s.\n", pThis->szName));
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_INTNET_FLT_IF_NOT_FOUND;
    }

    Log(("vboxNetFltOsInitInstance: this=%p: Notifier installed.\n", pThis));
    if (   pThis->fDisconnectedFromHost
        || !try_module_get(THIS_MODULE))
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_INTNET_FLT_IF_FAILED;
    }

    if (pThis->pSwitchPort->pfnNotifyHostAddress)
    {
        VBOXNETFLTNOTIFIER Enumerator;

        /*
         * register_inetaddr_notifier() and register_inet6addr_notifier()
         * do not call the callback for existing devices.  Enumerating
         * all network devices explicitly is a bit of an ifdef mess,
         * so co-opt register_netdevice_notifier() to do that for us.
         */
        RT_ZERO(Enumerator);
        Enumerator.Notifier.notifier_call = vboxNetFltLinuxEnumeratorCallback;
        Enumerator.pThis = pThis;

        err = register_netdevice_notifier(&Enumerator.Notifier);
        if (err)
        {
            LogRel(("%s: failed to enumerate network devices: error %d\n", __FUNCTION__, err));
            IPRT_LINUX_RESTORE_EFL_AC();
            return VINF_SUCCESS;
        }

        unregister_netdevice_notifier(&Enumerator.Notifier);

        pThis->u.s.NotifierIPv4.notifier_call = vboxNetFltLinuxNotifierIPv4Callback;
        err = register_inetaddr_notifier(&pThis->u.s.NotifierIPv4);
        if (err)
            LogRel(("%s: failed to register IPv4 notifier: error %d\n", __FUNCTION__, err));

        pThis->u.s.NotifierIPv6.notifier_call = vboxNetFltLinuxNotifierIPv6Callback;
        err = register_inet6addr_notifier(&pThis->u.s.NotifierIPv6);
        if (err)
            LogRel(("%s: failed to register IPv6 notifier: error %d\n", __FUNCTION__, err));
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}

int  vboxNetFltOsPreInitInstance(PVBOXNETFLTINS pThis)
{
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Init the linux specific members.
     */
    ASMAtomicUoWriteNullPtr(&pThis->u.s.pDev);
    pThis->u.s.fRegistered     = false;
    pThis->u.s.fPromiscuousSet = false;
    pThis->u.s.fPacketHandler  = false;
    memset(&pThis->u.s.PacketType, 0, sizeof(pThis->u.s.PacketType));
#ifdef VBOXNETFLT_LINUX_NAMESPACE_SUPPORT
    /* We should get the interface name in the form <namespace>/<ifname>, parse it. */
    pThis->u.s.uNamespaceInode = RTStrToUInt32(pThis->szName);
#else /* !VBOXNETFLT_LINUX_NAMESPACE_SUPPORT */
    pThis->u.s.uNamespaceInode = 0;
#endif /* !VBOXNETFLT_LINUX_NAMESPACE_SUPPORT */
#ifndef VBOXNETFLT_LINUX_NO_XMIT_QUEUE
    skb_queue_head_init(&pThis->u.s.XmitQueue);
# if RTLNX_VER_MIN(2,6,20)
    INIT_WORK(&pThis->u.s.XmitTask, vboxNetFltLinuxXmitTask);
# else
    INIT_WORK(&pThis->u.s.XmitTask, vboxNetFltLinuxXmitTask, &pThis->u.s.XmitTask);
# endif
#endif

    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


void vboxNetFltPortOsNotifyMacAddress(PVBOXNETFLTINS pThis, void *pvIfData, PCRTMAC pMac)
{
    NOREF(pThis); NOREF(pvIfData); NOREF(pMac);
}


int vboxNetFltPortOsConnectInterface(PVBOXNETFLTINS pThis, void *pvIf, void **pvIfData)
{
    /* Nothing to do */
    NOREF(pThis); NOREF(pvIf); NOREF(pvIfData);
    return VINF_SUCCESS;
}


int vboxNetFltPortOsDisconnectInterface(PVBOXNETFLTINS pThis, void *pvIfData)
{
    /* Nothing to do */
    NOREF(pThis); NOREF(pvIfData);
    return VINF_SUCCESS;
}

