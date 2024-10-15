/* $Id: VBoxNetFltBow-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetFlt - Network Filter Driver (Host), Solaris Specific Code.
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
#define LOG_GROUP LOG_GROUP_NET_FLT_DRV
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/intnetinline.h>
#include <VBox/version.h>
#include <iprt/initterm.h>
#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/rand.h>
#include <iprt/net.h>
#include <iprt/spinlock.h>
#include <iprt/mem.h>

#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/gld.h>
#include <sys/sunddi.h>
#include <sys/strsubr.h>
#include <sys/dlpi.h>
#include <sys/dls_mgmt.h>
#include <sys/mac.h>
#include <sys/strsun.h>

#include <sys/vnic_mgmt.h>
#include <sys/mac_client.h>
#include <sys/mac_provider.h>
#include <sys/dls.h>
#include <sys/dld.h>
#include <sys/cred.h>


#define VBOXNETFLT_OS_SPECFIC 1
#include "../VBoxNetFltInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The module name. */
#define DEVICE_NAME                     "vboxbow"
/** The module descriptions as seen in 'modinfo'. */
#define DEVICE_DESC_DRV                 "VirtualBox NetBow"
/** The dynamically created VNIC name (hardcoded in NetIf-solaris.cpp).
 *  @todo move this define into a common header. */
#define VBOXBOW_VNIC_NAME               "vboxvnic"
/** The VirtualBox VNIC template name (hardcoded in NetIf-solaris.cpp).
 *  @todo move this define into a common header. */
#define VBOXBOW_VNIC_TEMPLATE_NAME      "vboxvnic_template"
/** Debugging switch for using symbols in kmdb */
# define LOCAL                          static
/** VBOXNETFLTVNIC::u32Magic */
# define VBOXNETFLTVNIC_MAGIC           0x0ddfaced

/** VLAN tag masking, should probably be in IPRT? */
#define VLAN_ID(vlan)          (((vlan) >>  0) & 0x0fffu)
#define VLAN_CFI(vlan)         (((vlan) >> 12) & 0x0001u)
#define VLAN_PRI(vlan)         (((vlan) >> 13) & 0x0007u)
#define VLAN_TAG(pri,cfi,vid)  (((pri) << 13) | ((cfi) << 12) | ((vid) << 0))

typedef struct VLANHEADER
{
    uint16_t Type;
    uint16_t Data;
} VLANHEADER;
typedef struct VLANHEADER *PVLANHEADER;

/* Private: from sys/vlan.h */
#ifndef VLAN_ID_NONE
# define VLAN_ID_NONE           0
#endif

/* Private: from sys/param.h */
#ifndef MAXLINKNAMESPECIFIER
# define MAXLINKNAMESPECIFIER   96 /* MAXLINKNAMELEN + ZONENAME_MAX */
#endif

/* Private: from sys/mac_client_priv.h, mac client function prototypes. */
extern uint16_t mac_client_vid(mac_client_handle_t hClient);
extern void     mac_client_get_resources(mac_client_handle_t hClient, mac_resource_props_t *pResources);
extern int      mac_client_set_resources(mac_client_handle_t hClient, mac_resource_props_t *pResources);


/*********************************************************************************************************************************
*   Kernel Entry Hooks                                                                                                           *
*********************************************************************************************************************************/
LOCAL int VBoxNetFltSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd);
LOCAL int VBoxNetFltSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd);
LOCAL int VBoxNetFltSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pArg, void **ppvResult);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * cb_ops: for drivers that support char/block entry points
 */
static struct cb_ops g_VBoxNetFltSolarisCbOps =
{
    nulldev,                    /* c open */
    nulldev,                    /* c close */
    nodev,                      /* b strategy */
    nodev,                      /* b dump */
    nodev,                      /* b print */
    nodev,                      /* c read */
    nodev,                      /* c write*/
    nodev,                      /* c ioctl*/
    nodev,                      /* c devmap */
    nodev,                      /* c mmap */
    nodev,                      /* c segmap */
    nochpoll,                   /* c poll */
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
static struct dev_ops g_VBoxNetFltSolarisDevOps =
{
    DEVO_REV,                   /* driver build revision */
    0,                          /* ref count */
    VBoxNetFltSolarisGetInfo,
    nulldev,                    /* identify */
    nulldev,                    /* probe */
    VBoxNetFltSolarisAttach,
    VBoxNetFltSolarisDetach,
    nodev,                      /* reset */
    &g_VBoxNetFltSolarisCbOps,
    NULL,                       /* bus ops */
    nodev,                      /* power */
    ddi_quiesce_not_needed
};

/**
 * modldrv: export driver specifics to the kernel
 */
static struct modldrv g_VBoxNetFltSolarisModule =
{
    &mod_driverops,             /* extern from kernel */
    DEVICE_DESC_DRV " " VBOX_VERSION_STRING "r" RT_XSTR(VBOX_SVN_REV),
    &g_VBoxNetFltSolarisDevOps
};

/**
 * modlinkage: export install/remove/info to the kernel
 */
static struct modlinkage g_VBoxNetFltSolarisModLinkage =
{
    MODREV_1,
    {
        &g_VBoxNetFltSolarisModule,
        NULL,
    }
};

/*
 * VBOXNETFLTVNICTEMPLATE: VNIC template information.
 */
typedef struct VBOXNETFLTVNICTEMPLATE
{
    /** The name of link on which the VNIC template is created on. */
    char                        szLinkName[MAXNAMELEN];
    /** The VLAN Id (can be VLAN_ID_NONE). */
    uint16_t                    uVLANId;
    /** Resources (bandwidth, CPU bindings, flow priority etc.) */
    mac_resource_props_t        Resources;
} VBOXNETFLTVNICTEMPLATE;
typedef struct VBOXNETFLTVNICTEMPLATE *PVBOXNETFLTVNICTEMPLATE;

/**
 * VBOXNETFLTVNIC: Per-VNIC instance data.
 */
typedef struct VBOXNETFLTVNIC
{
    /** Magic number (VBOXNETFLTVNIC_MAGIC). */
    uint32_t                    u32Magic;
    /** Whether we created the VNIC or not. */
    bool                        fCreated;
    /** Pointer to the VNIC template if any. */
    PVBOXNETFLTVNICTEMPLATE     pVNICTemplate;
    /** Pointer to the VirtualBox interface instance. */
    void                       *pvIf;
    /** The MAC handle. */
    mac_handle_t                hInterface;
    /** The VNIC link ID. */
    datalink_id_t               hLinkId;
    /** The MAC client handle */
    mac_client_handle_t         hClient;
    /** The unicast address handle. */
    mac_unicast_handle_t        hUnicast;
    /** The promiscuous handle. */
    mac_promisc_handle_t        hPromisc;
    /* The VNIC name. */
    char                        szName[MAXLINKNAMESPECIFIER];
    /** Handle to the next VNIC in the list. */
    list_node_t                 hNode;
} VBOXNETFLTVNIC;
typedef struct VBOXNETFLTVNIC *PVBOXNETFLTVNIC;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Global Device handle we only support one instance. */
static dev_info_t *g_pVBoxNetFltSolarisDip = NULL;
/** The (common) global data. */
static VBOXNETFLTGLOBALS g_VBoxNetFltSolarisGlobals;
/** Global next-free VNIC Id (never decrements). */
static volatile uint64_t g_VBoxNetFltSolarisVNICId;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
LOCAL mblk_t *vboxNetFltSolarisMBlkFromSG(PVBOXNETFLTINS pThis, PINTNETSG pSG, uint32_t fDst);
LOCAL unsigned vboxNetFltSolarisMBlkCalcSGSegs(PVBOXNETFLTINS pThis, mblk_t *pMsg);
LOCAL int vboxNetFltSolarisMBlkToSG(PVBOXNETFLTINS pThis, mblk_t *pMsg, PINTNETSG pSG, unsigned cSegs, uint32_t fSrc);
LOCAL void vboxNetFltSolarisRecv(void *pvData, mac_resource_handle_t hResource, mblk_t *pMsg, boolean_t fLoopback);
//LOCAL void vboxNetFltSolarisAnalyzeMBlk(mblk_t *pMsg);
LOCAL int vboxNetFltSolarisReportInfo(PVBOXNETFLTINS pThis, mac_handle_t hInterface, bool fIsVNIC);
LOCAL int vboxNetFltSolarisInitVNIC(PVBOXNETFLTINS pThis, PVBOXNETFLTVNIC pVNIC);
LOCAL int vboxNetFltSolarisInitVNICTemplate(PVBOXNETFLTINS pThis, PVBOXNETFLTVNICTEMPLATE pVNICTemplate);
LOCAL PVBOXNETFLTVNIC vboxNetFltSolarisAllocVNIC(void);
LOCAL void vboxNetFltSolarisFreeVNIC(PVBOXNETFLTVNIC pVNIC);
LOCAL void vboxNetFltSolarisDestroyVNIC(PVBOXNETFLTVNIC pVNIC);
LOCAL int vboxNetFltSolarisCreateVNIC(PVBOXNETFLTINS pThis, PVBOXNETFLTVNIC *ppVNIC);
DECLINLINE(int) vboxNetFltSolarisGetLinkId(const char *pszMacName, datalink_id_t *pLinkId);

/**
 * Kernel entry points
 */
int _init(void)
{
    Log((DEVICE_NAME ":_init\n"));

    /*
     * Prevent module autounloading.
     */
    modctl_t *pModCtl = mod_getctl(&g_VBoxNetFltSolarisModLinkage);
    if (pModCtl)
        pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD;
    else
        cmn_err(CE_NOTE, ":failed to disable autounloading!\n");

    /*
     * Initialize IPRT.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize the globals and connect to the support driver.
         *
         * This will call back vboxNetFltOsOpenSupDrv (and maybe vboxNetFltOsCloseSupDrv)
         * for establishing the connect to the support driver.
         */
        memset(&g_VBoxNetFltSolarisGlobals, 0, sizeof(g_VBoxNetFltSolarisGlobals));
        rc = vboxNetFltInitGlobalsAndIdc(&g_VBoxNetFltSolarisGlobals);
        if (RT_SUCCESS(rc))
        {
            rc = mod_install(&g_VBoxNetFltSolarisModLinkage);
            if (!rc)
                return rc;

            LogRel((DEVICE_NAME ":mod_install failed. rc=%d\n", rc));
            vboxNetFltTryDeleteIdcAndGlobals(&g_VBoxNetFltSolarisGlobals);
        }
        else
            LogRel((DEVICE_NAME ":failed to initialize globals.\n"));

        RTR0Term();
    }
    else
        cmn_err(CE_NOTE, "failed to initialize IPRT (rc=%d)\n", rc);

    memset(&g_VBoxNetFltSolarisGlobals, 0, sizeof(g_VBoxNetFltSolarisGlobals));
    return RTErrConvertToErrno(rc);
}


int _fini(void)
{
    int rc;
    Log((DEVICE_NAME ":_fini\n"));

    /*
     * Undo the work done during start (in reverse order).
     */
    rc = vboxNetFltTryDeleteIdcAndGlobals(&g_VBoxNetFltSolarisGlobals);
    if (RT_FAILURE(rc))
    {
        LogRel((DEVICE_NAME ":_fini - busy! rc=%d\n", rc));
        return EBUSY;
    }

    rc = mod_remove(&g_VBoxNetFltSolarisModLinkage);
    if (!rc)
        RTR0Term();

    return rc;
}


int _info(struct modinfo *pModInfo)
{
    /* _info() can be called before _init() so RTR0Init() might not be called at this point. */
    int rc = mod_info(&g_VBoxNetFltSolarisModLinkage, pModInfo);
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
LOCAL int VBoxNetFltSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd)
{
    Log((DEVICE_NAME ":VBoxNetFltSolarisAttach pDip=%p enmCmd=%d\n", pDip, enmCmd));

    switch (enmCmd)
    {
        case DDI_ATTACH:
        {
            g_pVBoxNetFltSolarisDip = pDip;
            return DDI_SUCCESS;
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
LOCAL int VBoxNetFltSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    Log((DEVICE_NAME ":VBoxNetFltSolarisDetach pDip=%p enmCmd=%d\n", pDip, enmCmd));

    switch (enmCmd)
    {
        case DDI_DETACH:
        {
            return DDI_SUCCESS;
        }

        case DDI_RESUME:
        {
            /* Nothing to do here... */
            return DDI_SUCCESS;
        }

        /* case DDI_PM_SUSPEND: */
        /* case DDI_HOT_PLUG_DETACH: */
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
LOCAL int VBoxNetFltSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pvArg, void **ppvResult)
{
    Log((DEVICE_NAME ":VBoxNetFltSolarisGetInfo pDip=%p enmCmd=%d pArg=%p instance=%d\n", pDip, enmCmd, getminor((dev_t)pvArg)));

    switch (enmCmd)
    {
        case DDI_INFO_DEVT2DEVINFO:
        {
            *ppvResult = g_pVBoxNetFltSolarisDip;
            return *ppvResult ? DDI_SUCCESS : DDI_FAILURE;
        }

        case DDI_INFO_DEVT2INSTANCE:
        {
            /* There can only be a single-instance of this driver and thus its instance number is 0. */
            *ppvResult = (void *)0;
            break;
        }
    }

    return DDI_FAILURE;
}


/**
 * Create a solaris message block from the SG list.
 *
 * @param   pThis           The instance.
 * @param   pSG             Pointer to the scatter-gather list.
 * @param   fDst            INTNETTRUNKDIR_XXX.
 *
 * @returns Solaris message block.
 */
DECLINLINE(mblk_t *) vboxNetFltSolarisMBlkFromSG(PVBOXNETFLTINS pThis, PINTNETSG pSG, uint32_t fDst)
{
    Log((DEVICE_NAME ":vboxNetFltSolarisMBlkFromSG pThis=%p pSG=%p\n", pThis, pSG));

    mblk_t *pMsg = allocb(pSG->cbTotal, BPRI_HI);
    if (RT_UNLIKELY(!pMsg))
    {
        LogRel((DEVICE_NAME ":vboxNetFltSolarisMBlkFromSG failed to alloc %d bytes for mblk_t.\n", pSG->cbTotal));
        return NULL;
    }

    /*
     * Single buffer copy. Maybe later explore the
     * need/possibility for using a mblk_t chain rather.
     */
    for (unsigned i = 0; i < pSG->cSegsUsed; i++)
    {
        if (pSG->aSegs[i].pv)
        {
            bcopy(pSG->aSegs[i].pv, pMsg->b_wptr, pSG->aSegs[i].cb);
            pMsg->b_wptr += pSG->aSegs[i].cb;
        }
    }
    return pMsg;
}


/**
 * Calculate the number of segments required for this message block.
 *
 * @param   pThis   The instance
 * @param   pMsg    Pointer to the data message.
 *
 * @returns Number of segments.
 */
LOCAL unsigned vboxNetFltSolarisMBlkCalcSGSegs(PVBOXNETFLTINS pThis, mblk_t *pMsg)
{
    unsigned cSegs = 0;
    for (mblk_t *pCur = pMsg; pCur; pCur = pCur->b_cont)
        if (MBLKL(pCur))
            cSegs++;

#ifdef PADD_RUNT_FRAMES_FROM_HOST
    if (msgdsize(pMsg) < 60)
        cSegs++;
#endif

    NOREF(pThis);
    return RT_MAX(cSegs, 1);
}


/**
 * Initializes an SG list from the given message block.
 *
 * @param   pThis   The instance.
 * @param   pMsg    Pointer to the data message.
                    The caller must ensure it's not a control message block.
 * @param   pSG     Pointer to the SG.
 * @param   cSegs   Number of segments in the SG.
 *                  This should match the number in the message block exactly!
 * @param   fSrc    The source of the message.
 *
 * @returns VBox status code.
 */
LOCAL int vboxNetFltSolarisMBlkToSG(PVBOXNETFLTINS pThis, mblk_t *pMsg, PINTNETSG pSG, unsigned cSegs, uint32_t fSrc)
{
    Log((DEVICE_NAME ":vboxNetFltSolarisMBlkToSG pThis=%p pMsg=%p pSG=%p cSegs=%d\n", pThis, pMsg, pSG, cSegs));

    /*
     * Convert the message block to segments. Works cbTotal and sets cSegsUsed.
     */
    IntNetSgInitTempSegs(pSG, 0 /*cbTotal*/, cSegs, 0 /*cSegsUsed*/);
    mblk_t *pCur = pMsg;
    unsigned iSeg = 0;
    while (pCur)
    {
        size_t cbSeg = MBLKL(pCur);
        if (cbSeg)
        {
            void *pvSeg = pCur->b_rptr;
            pSG->aSegs[iSeg].pv = pvSeg;
            pSG->aSegs[iSeg].cb = cbSeg;
            pSG->aSegs[iSeg].Phys = NIL_RTHCPHYS;
            pSG->cbTotal += cbSeg;
            iSeg++;
        }
        pCur = pCur->b_cont;
    }
    pSG->cSegsUsed = iSeg;

#ifdef PADD_RUNT_FRAMES_FROM_HOST
    if (pSG->cbTotal < 60 && (fSrc & INTNETTRUNKDIR_HOST))
    {
        Log((DEVICE_NAME ":vboxNetFltSolarisMBlkToSG pulling up to length.\n"));

        static uint8_t const s_abZero[128] = {0};
        pSG->aSegs[iSeg].Phys = NIL_RTHCPHYS;
        pSG->aSegs[iSeg].pv = (void *)&s_abZero[0];
        pSG->aSegs[iSeg].cb = 60 - pSG->cbTotal;
        pSG->cbTotal = 60;
        pSG->cSegsUsed++;
        Assert(iSeg + 1 < cSegs);
    }
#endif

    Log((DEVICE_NAME ":vboxNetFltSolarisMBlkToSG iSeg=%d pSG->cbTotal=%d msgdsize=%d\n", iSeg, pSG->cbTotal, msgdsize(pMsg)));
    return VINF_SUCCESS;
}


#if 0
/**
 * Simple packet dump, used for internal debugging.
 *
 * @param   pMsg    Pointer to the message to analyze and dump.
 */
LOCAL void vboxNetFltSolarisAnalyzeMBlk(mblk_t *pMsg)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisAnalyzeMBlk pMsg=%p\n", pMsg));

    PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pMsg->b_rptr;
    uint8_t *pb = pMsg->b_rptr;
    if (pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_IPV4))
    {
        PRTNETIPV4 pIpHdr = (PRTNETIPV4)(pEthHdr + 1);
        if (!pMsg->b_cont)
        {
            if (pIpHdr->ip_p == RTNETIPV4_PROT_ICMP)
                LogRel((DEVICE_NAME ":ICMP D=%.6Rhxs  S=%.6Rhxs  T=%04x\n", pb, pb + 6, RT_BE2H_U16(*(uint16_t *)(pb + 12))));
            else if (pIpHdr->ip_p == RTNETIPV4_PROT_TCP)
                LogRel((DEVICE_NAME ":TCP D=%.6Rhxs  S=%.6Rhxs\n", pb, pb + 6));
            else if (pIpHdr->ip_p == RTNETIPV4_PROT_UDP)
            {
                PCRTNETUDP pUdpHdr = (PCRTNETUDP)((uint32_t *)pIpHdr + pIpHdr->ip_hl);
                if (   RT_BE2H_U16(pUdpHdr->uh_sport) == 67
                    && RT_BE2H_U16(pUdpHdr->uh_dport) == 68)
                {
                    LogRel((DEVICE_NAME ":UDP bootp ack D=%.6Rhxs S=%.6Rhxs UDP_CheckSum=%04x Computex=%04x\n", pb, pb + 6,
                                RT_BE2H_U16(pUdpHdr->uh_sum), RT_BE2H_U16(RTNetIPv4UDPChecksum(pIpHdr, pUdpHdr, pUdpHdr + 1))));
                }
            }
        }
        else
        {
            Log((DEVICE_NAME ":Chained IP packet. Skipping validity check.\n"));
        }
    }
    else if (pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_VLAN))
    {
        PVLANHEADER pVlanHdr = (PVLANHEADER)(pMsg->b_rptr + sizeof(RTNETETHERHDR) - sizeof(pEthHdr->EtherType));
        LogRel((DEVICE_NAME ":VLAN Pcp=%u Cfi=%u Id=%u\n", VLAN_PRI(RT_BE2H_U16(pVlanHdr->Data)),
                VLAN_CFI(RT_BE2H_U16(pVlanHdr->Data)), VLAN_ID(RT_BE2H_U16(pVlanHdr->Data))));
        LogRel((DEVICE_NAME "%.*Rhxd\n", sizeof(VLANHEADER), pVlanHdr));
    }
    else if (pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_ARP))
    {
        PRTNETARPHDR pArpHdr = (PRTNETARPHDR)(pEthHdr + 1);
        LogRel((DEVICE_NAME ":ARP Op=%d\n", pArpHdr->ar_oper));
    }
    else if (pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_IPV6))
    {
        LogRel((DEVICE_NAME ":IPv6 D=%.6Rhxs S=%.6Rhxs\n", pb, pb + 6));
    }
    else if (   pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_IPX_1)
             || pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_IPX_2)
             || pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_IPX_3))
    {
        LogRel((DEVICE_NAME ":IPX packet.\n"));
    }
    else
    {
        LogRel((DEVICE_NAME ":Unknown EtherType=%x D=%.6Rhxs S=%.6Rhxs\n", RT_H2BE_U16(pEthHdr->EtherType), &pEthHdr->DstMac,
                    &pEthHdr->SrcMac));
        /* Log((DEVICE_NAME ":%.*Rhxd\n", MBLKL(pMsg), pMsg->b_rptr)); */
    }
}
#endif


/**
 * Helper.
 */
DECLINLINE(bool) vboxNetFltPortSolarisIsHostMac(PVBOXNETFLTINS pThis, PCRTMAC pMac)
{
    return pThis->u.s.MacAddr.au16[0] == pMac->au16[0]
        && pThis->u.s.MacAddr.au16[1] == pMac->au16[1]
        && pThis->u.s.MacAddr.au16[2] == pMac->au16[2];
}


/**
 * Receive (rx) entry point.
 *
 * @param   pvData          Private data.
 * @param   hResource       The resource handle.
 * @param   pMsg            The packet.
 * @param   fLoopback       Whether this is a loopback packet or not.
 */
LOCAL void vboxNetFltSolarisRecv(void *pvData, mac_resource_handle_t hResource, mblk_t *pMsg, boolean_t fLoopback)
{
    Log((DEVICE_NAME ":vboxNetFltSolarisRecv pvData=%p pMsg=%p fLoopback=%d cbData=%d\n", pvData, pMsg, fLoopback,
         pMsg ? MBLKL(pMsg) : 0));

    PVBOXNETFLTINS pThis = (PVBOXNETFLTINS)pvData;
    AssertPtrReturnVoid(pThis);
    AssertPtrReturnVoid(pMsg);

    /*
     * Active? Retain the instance and increment the busy counter.
     */
    if (!vboxNetFltTryRetainBusyActive(pThis))
    {
        freemsgchain(pMsg);
        return;
    }

    uint32_t fSrc = INTNETTRUNKDIR_WIRE;
    PRTNETETHERHDR pEthHdr = (PRTNETETHERHDR)pMsg->b_rptr;
    if (   MBLKL(pMsg) >= sizeof(RTNETETHERHDR)
        && vboxNetFltPortSolarisIsHostMac(pThis, &pEthHdr->SrcMac))
        fSrc = INTNETTRUNKDIR_HOST;

    /*
     * Route all received packets into the internal network.
     */
    uint16_t cFailed = 0;
    for (mblk_t *pCurMsg = pMsg; pCurMsg != NULL; pCurMsg = pCurMsg->b_next)
    {
        unsigned cSegs = vboxNetFltSolarisMBlkCalcSGSegs(pThis, pCurMsg);
        PINTNETSG pSG = (PINTNETSG)alloca(RT_UOFFSETOF_DYN(INTNETSG, aSegs[cSegs]));
        int rc = vboxNetFltSolarisMBlkToSG(pThis, pMsg, pSG, cSegs, fSrc);
        if (RT_SUCCESS(rc))
            pThis->pSwitchPort->pfnRecv(pThis->pSwitchPort, NULL, pSG, fSrc);
        else
            cFailed++;
    }
    vboxNetFltRelease(pThis, true /* fBusy */);

    if (RT_UNLIKELY(cFailed))
        LogRel((DEVICE_NAME ":vboxNetFltSolarisMBlkToSG failed for %u packets.\n", cFailed));

    freemsgchain(pMsg);

    NOREF(hResource);
}


#if 0
/**
 * MAC layer link notification hook.
 *
 * @param    pvArg          Opaque pointer to the instance.
 * @param    Type           Notification Type.
 *
 * @remarks This hook will be invoked for various changes to the underlying
 *          interface even when VMs aren't running so don't do any funky stuff
 *          here.
 */
LOCAL void vboxNetFltSolarisLinkNotify(void *pvArg, mac_notify_type_t Type)
{
    LogRel((DEVICE_NAME ":vboxNetFltSolarisLinkNotify pvArg=%p Type=%d\n", pvArg, Type));

    PVBOXNETFLTINS pThis = pvArg;
    AssertPtrReturnVoid(pThis);
    AssertPtrReturnVoid(pThis->u.s.hInterface);

    switch (Type)
    {
        case MAC_NOTE_LINK:
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisLinkNotify link state change\n"));
            link_state_t hLinkState = mac_stat_get(pThis->u.s.hInterface, MAC_STAT_LINK_STATE);
            bool fDisconnectedFromHost = hLinkState == LINK_STATE_UP ? false : true;
            if (fDisconnectedFromHost != ASMAtomicUoReadBool(&pThis->fDisconnectedFromHost))
            {
                ASMAtomicUoWriteBool(&pThis->fDisconnectedFromHost, fDisconnectedFromHost);
                LogRel((DEVICE_NAME ":vboxNetFltSolarisLinkNotify link state change: new state=%s\n",
                fDisconnectedFromHost ? "DOWN" : "UP"));
            }
            break;
        }

        default:
            return;
    }
}
#endif


/**
 * Report capabilities and MAC address to IntNet after obtaining the MAC address
 * of the underlying interface for a VNIC or the current interface if it's a
 * physical/ether-stub interface.
 *
 * @param   pThis           The instance.
 * @param   hInterface      The Interface handle.
 * @param   fIsVNIC         Whether this interface handle corresponds to a VNIC
 *                          or not.
 *
 * @remarks Retains the instance while doing it's job.
 * @returns VBox status code.
 */
LOCAL int vboxNetFltSolarisReportInfo(PVBOXNETFLTINS pThis, mac_handle_t hInterface, bool fIsVNIC)
{
    mac_handle_t hLowerMac = NULL;
    if (!fIsVNIC)
        hLowerMac = hInterface;
    else
    {
        hLowerMac = mac_get_lower_mac_handle(hInterface);
        if (RT_UNLIKELY(!hLowerMac))
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisReportInfo failed to get lower MAC handle for '%s'\n", pThis->szName));
            return VERR_INVALID_HANDLE;
        }
    }

    pThis->u.s.hInterface = hLowerMac;

#if 0
    /*
     * Try setup link notification hooks, this might fail if mac_no_notification()
     * doesn't support it. We won't bother using the private function since link notification
     * isn't critical for us and ignore failures.
     */
    pThis->u.s.hNotify = mac_notify_add(hLowerMac, vboxNetFltSolarisLinkNotify, pThis);
    if (!pThis->u.s.hNotify)
        LogRel((DEVICE_NAME ":vboxNetFltSolarisReportInfo Warning! Failed to setup link notification hook.\n"));
#endif

    mac_unicast_primary_get(hLowerMac, (uint8_t *)pThis->u.s.MacAddr.au8);
    if (vboxNetFltTryRetainBusyNotDisconnected(pThis))
    {
        Assert(pThis->pSwitchPort);
        Log((DEVICE_NAME ":vboxNetFltSolarisReportInfo phys mac %.6Rhxs\n", &pThis->u.s.MacAddr));
        pThis->pSwitchPort->pfnReportMacAddress(pThis->pSwitchPort, &pThis->u.s.MacAddr);
        pThis->pSwitchPort->pfnReportPromiscuousMode(pThis->pSwitchPort, false); /** @todo Promisc */
        pThis->pSwitchPort->pfnReportGsoCapabilities(pThis->pSwitchPort, 0, INTNETTRUNKDIR_WIRE | INTNETTRUNKDIR_HOST);
        pThis->pSwitchPort->pfnReportNoPreemptDsts(pThis->pSwitchPort, 0 /* none */);
        vboxNetFltRelease(pThis, true /*fBusy*/);
        return VINF_SUCCESS;
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltSolarisReportInfo failed to retain interface. pThis=%p\n", pThis));

    return VERR_INTNET_FLT_IF_BUSY;
}


/**
 * Initialize a VNIC, optionally from a template.
 *
 * @param   pThis           The instance.
 * @param   pVNIC           Pointer to the VNIC.
 *
 * @returns VBox status code.
 */
LOCAL int vboxNetFltSolarisInitVNIC(PVBOXNETFLTINS pThis, PVBOXNETFLTVNIC pVNIC)
{
    /*
     * Some paranoia.
     */
    AssertReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pVNIC, VERR_INVALID_PARAMETER);
    AssertReturn(pVNIC->hInterface, VERR_INVALID_POINTER);
    AssertReturn(pVNIC->hLinkId != DATALINK_INVALID_LINKID, VERR_INVALID_HANDLE);
    AssertReturn(!pVNIC->hClient, VERR_INVALID_POINTER);

    int rc = mac_client_open(pVNIC->hInterface, &pVNIC->hClient,
                         NULL,                                   /* name of this client */
                         MAC_OPEN_FLAGS_USE_DATALINK_NAME |      /* client name same as underlying NIC */
                         MAC_OPEN_FLAGS_MULTI_PRIMARY            /* allow multiple primary unicasts */
                         );
    if (RT_LIKELY(!rc))
    {
        if (pVNIC->pVNICTemplate)
            rc = mac_client_set_resources(pVNIC->hClient, &pVNIC->pVNICTemplate->Resources);

        if (RT_LIKELY(!rc))
        {
            Log((DEVICE_NAME ":vboxNetFltSolarisInitVNIC succesfully initialized VNIC.\n"));
            return VINF_SUCCESS;
        }
        else
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisInitVNIC mac_client_set_resources failed. rc=%d\n", rc));
            rc = VERR_INTNET_FLT_VNIC_INIT_FAILED;
        }

        mac_client_close(pVNIC->hClient, 0 /* flags */);
        pVNIC->hClient = NULL;
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltSolarisInitVNIC failed to open mac client for '%s' rc=%d\n", pThis->szName, rc));

    return VERR_INTNET_FLT_VNIC_OPEN_FAILED;
}



/**
 * Get the underlying link name for a VNIC (template).
 *
 * @return VBox status code.
 * @param   hVNICMacHandle      The handle to the VNIC.
 * @param   pszLowerLinkName    Where to store the lower-mac linkname, must be
 *                              at least MAXLINKNAMELEN in size.
 */
LOCAL int vboxNetFltSolarisGetLowerLinkName(mac_handle_t hVNICMacHandle, char *pszLowerLinkName)
{
    Assert(mac_is_vnic(hVNICMacHandle));
    mac_handle_t hPhysLinkHandle = mac_get_lower_mac_handle(hVNICMacHandle);
    if (RT_LIKELY(hPhysLinkHandle))
    {
        datalink_id_t PhysLinkId;
        const char *pszMacName = mac_name(hPhysLinkHandle);
        int rc = vboxNetFltSolarisGetLinkId(pszMacName, &PhysLinkId);
        if (RT_SUCCESS(rc))
        {
            rc = dls_mgmt_get_linkinfo(PhysLinkId, pszLowerLinkName, NULL /*class*/, NULL /*media*/, NULL /*flags*/);
            if (RT_LIKELY(!rc))
                return VINF_SUCCESS;

            LogRel((DEVICE_NAME ":vboxNetFltSolarisGetLowerLinkName failed to get link info. pszMacName=%s pszLowerLinkName=%s\n",
                    pszMacName, pszLowerLinkName));
            return VERR_INTNET_FLT_LOWER_LINK_INFO_NOT_FOUND;
        }

        LogRel((DEVICE_NAME ":vboxNetFltSolarisGetLowerLinkName failed to get link id. pszMacName=%s pszLowerLinkName=%s\n",
                pszMacName, pszLowerLinkName));
        return VERR_INTNET_FLT_LOWER_LINK_ID_NOT_FOUND;
    }

    LogRel((DEVICE_NAME ":vboxNetFltSolarisGetLowerLinkName failed to get lower-mac. pszLowerLinkName=%s\n", pszLowerLinkName));
    return VERR_INTNET_FLT_LOWER_LINK_OPEN_FAILED;
}


/**
 * Initializes the VNIC template. This involves opening the template VNIC to
 * retreive info. like the VLAN Id, underlying MAC address etc.
 *
 * @param   pThis           The VM connection instance.
 * @param   pVNICTemplate   Pointer to a VNIC template to initialize.
 *
 * @returns VBox status code.
 */
LOCAL int vboxNetFltSolarisInitVNICTemplate(PVBOXNETFLTINS pThis, PVBOXNETFLTVNICTEMPLATE pVNICTemplate)
{
    Log((DEVICE_NAME ":vboxNetFltSolarisInitVNICTemplate pThis=%p pVNICTemplate=%p\n", pThis, pVNICTemplate));

    AssertReturn(pVNICTemplate, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u.s.fIsVNICTemplate == true, VERR_INVALID_STATE);

    /*
     * Get the VNIC template's datalink ID.
     */
    datalink_id_t VNICLinkId;
    int rc = vboxNetFltSolarisGetLinkId(pThis->szName, &VNICLinkId);
    if (RT_SUCCESS(rc))
    {
        /*
         * Open the VNIC to obtain a MAC handle so as to retreive the VLAN ID.
         */
        mac_handle_t hInterface;
        rc = mac_open_by_linkid(VNICLinkId, &hInterface);
        if (!rc)
        {
            /*
             * Get the underlying linkname.
             */
            AssertCompile(sizeof(pVNICTemplate->szLinkName) >= MAXLINKNAMELEN);
            rc = vboxNetFltSolarisGetLowerLinkName(hInterface, pVNICTemplate->szLinkName);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Now open the VNIC template to retrieve the VLAN Id & resources.
                 */
                mac_client_handle_t hClient;
                rc = mac_client_open(hInterface, &hClient,
                                     NULL,                                   /* name of this client */
                                     MAC_OPEN_FLAGS_USE_DATALINK_NAME |      /* client name same as underlying NIC */
                                     MAC_OPEN_FLAGS_MULTI_PRIMARY            /* allow multiple primary unicasts */
                                     );
                if (RT_LIKELY(!rc))
                {
                    pVNICTemplate->uVLANId = mac_client_vid(hClient);
                    mac_client_get_resources(hClient, &pVNICTemplate->Resources);
                    mac_client_close(hClient, 0 /* fFlags */);
                    mac_close(hInterface);

                    LogRel((DEVICE_NAME ":vboxNetFltSolarisInitVNICTemplate successfully init. VNIC template. szLinkName=%s "
                            "VLAN Id=%u\n", pVNICTemplate->szLinkName, pVNICTemplate->uVLANId));
                    return VINF_SUCCESS;
                }
                else
                {
                    LogRel((DEVICE_NAME ":vboxNetFltSolarisInitVNICTemplate failed to open VNIC template. rc=%d\n", rc));
                    rc = VERR_INTNET_FLT_IF_FAILED;
                }
            }
            else
                LogRel((DEVICE_NAME ":vboxNetFltSolarisInitVNICTemplate failed to get lower linkname for VNIC template '%s'.\n",
                        pThis->szName));

            mac_close(hInterface);
        }
        else
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisInitVNICTemplate failed to open by link ID. rc=%d\n", rc));
            rc = VERR_INTNET_FLT_IF_FAILED;
        }
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltSolarisInitVNICTemplate failed to get VNIC template link Id. rc=%d\n", rc));

    return rc;
}


/**
 * Allocate a VNIC structure.
 *
 * @returns An allocated VNIC structure or NULL in case of errors.
 */
LOCAL PVBOXNETFLTVNIC vboxNetFltSolarisAllocVNIC(void)
{
    PVBOXNETFLTVNIC pVNIC = RTMemAllocZ(sizeof(VBOXNETFLTVNIC));
    if (RT_UNLIKELY(!pVNIC))
        return NULL;

    pVNIC->u32Magic        = VBOXNETFLTVNIC_MAGIC;
    pVNIC->fCreated        = false;
    pVNIC->pVNICTemplate   = NULL;
    pVNIC->pvIf            = NULL;
    pVNIC->hInterface      = NULL;
    pVNIC->hLinkId         = DATALINK_INVALID_LINKID;
    pVNIC->hClient         = NULL;
    pVNIC->hUnicast        = NULL;
    pVNIC->hPromisc        = NULL;
    RT_ZERO(pVNIC->szName);
    list_link_init(&pVNIC->hNode);
    return pVNIC;
}


/**
 * Frees an allocated VNIC.
 *
 * @param   pVNIC           Pointer to the VNIC.
 */
DECLINLINE(void) vboxNetFltSolarisFreeVNIC(PVBOXNETFLTVNIC pVNIC)
{
    RTMemFree(pVNIC);
}


/**
 * Destroy a created VNIC if it was created by us, or just
 * de-initializes the VNIC freeing up resources handles.
 *
 * @param   pVNIC           Pointer to the VNIC.
 */
LOCAL void vboxNetFltSolarisDestroyVNIC(PVBOXNETFLTVNIC pVNIC)
{
    AssertPtrReturnVoid(pVNIC);
    AssertMsgReturnVoid(pVNIC->u32Magic == VBOXNETFLTVNIC_MAGIC, ("pVNIC=%p u32Magic=%#x\n", pVNIC, pVNIC->u32Magic));
    if (pVNIC)
    {
        if (pVNIC->hClient)
        {
#if 0
            if (pVNIC->hUnicast)
            {
                mac_unicast_remove(pVNIC->hClient, pVNIC->hUnicast);
                pVNIC->hUnicast = NULL;
            }
#endif

            if (pVNIC->hPromisc)
            {
                mac_promisc_remove(pVNIC->hPromisc);
                pVNIC->hPromisc = NULL;
            }

            mac_rx_clear(pVNIC->hClient);

            mac_client_close(pVNIC->hClient, 0 /* fFlags */);
            pVNIC->hClient = NULL;
        }

        if (pVNIC->hInterface)
        {
            mac_close(pVNIC->hInterface);
            pVNIC->hInterface = NULL;
        }

        if (pVNIC->fCreated)
        {
            vnic_delete(pVNIC->hLinkId, 0 /* Flags */);
            pVNIC->hLinkId = DATALINK_INVALID_LINKID;
            pVNIC->fCreated = false;
        }

        if (pVNIC->pVNICTemplate)
        {
            RTMemFree(pVNIC->pVNICTemplate);
            pVNIC->pVNICTemplate = NULL;
        }
    }
}


/**
 * Create a non-persistent VNIC over the given interface.
 *
 * @param   pThis           The VM connection instance.
 * @param   ppVNIC          Where to store the created VNIC.
 *
 * @returns VBox status code.
 */
LOCAL int vboxNetFltSolarisCreateVNIC(PVBOXNETFLTINS pThis, PVBOXNETFLTVNIC *ppVNIC)
{
    Log((DEVICE_NAME ":vboxNetFltSolarisCreateVNIC pThis=%p\n", pThis));

    AssertReturn(pThis, VERR_INVALID_POINTER);
    AssertReturn(ppVNIC, VERR_INVALID_POINTER);

    int rc = VERR_INVALID_STATE;
    PVBOXNETFLTVNIC pVNIC = vboxNetFltSolarisAllocVNIC();
    if (RT_UNLIKELY(!pVNIC))
        return VERR_NO_MEMORY;

    /*
     * Set a random MAC address for now. It will be changed to the VM interface's
     * MAC address later, see vboxNetFltPortOsNotifyMacAddress().
     */
    RTMAC GuestMac;
    GuestMac.au8[0] = 0x08;
    GuestMac.au8[1] = 0x00;
    GuestMac.au8[2] = 0x27;
    RTRandBytes(&GuestMac.au8[3], 3);

    AssertCompile(sizeof(RTMAC) <= MAXMACADDRLEN);

    const char *pszLinkName       = pThis->szName;
    uint16_t uVLANId              = VLAN_ID_NONE;
    vnic_mac_addr_type_t AddrType = VNIC_MAC_ADDR_TYPE_FIXED;
    vnic_ioc_diag_t Diag          = VNIC_IOC_DIAG_NONE;
    int MacSlot                   = 0;
    int MacLen                    = sizeof(GuestMac);
    uint32_t fFlags               = 0;

    if (pThis->u.s.fIsVNICTemplate)
    {
        pVNIC->pVNICTemplate = RTMemAllocZ(sizeof(VBOXNETFLTVNICTEMPLATE));
        if (RT_UNLIKELY(!pVNIC->pVNICTemplate))
        {
            vboxNetFltSolarisFreeVNIC(pVNIC);
            return VERR_NO_MEMORY;
        }

        /*
         * Initialize the VNIC template.
         */
        rc = vboxNetFltSolarisInitVNICTemplate(pThis, pVNIC->pVNICTemplate);
        if (RT_FAILURE(rc))
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisCreateVNIC failed to initialize VNIC from VNIC template. rc=%Rrc\n", rc));
            vboxNetFltSolarisFreeVNIC(pVNIC);
            return rc;
        }

        pszLinkName = pVNIC->pVNICTemplate->szLinkName;
        uVLANId     = pVNIC->pVNICTemplate->uVLANId;
#if 0
        /*
         * Required only if we're creating a VLAN interface & not a VNIC with a VLAN Id.
         */
        if (uVLANId != VLAN_ID_NONE)
            fFlags |= MAC_VLAN;
#endif
        Log((DEVICE_NAME ":vboxNetFltSolarisCreateVNIC pThis=%p VLAN Id=%u\n", pThis, uVLANId));
    }

    /*
     * Make sure the dynamic VNIC we're creating doesn't already exists, if so pick a new instance.
     * This is to avoid conflicts with users manually creating VNICs whose name starts with VBOXBOW_VNIC_NAME.
     */
    do
    {
        AssertCompile(sizeof(pVNIC->szName) > sizeof(VBOXBOW_VNIC_NAME "18446744073709551615" /* UINT64_MAX */));
        RTStrPrintf(pVNIC->szName, sizeof(pVNIC->szName), "%s%RU64", VBOXBOW_VNIC_NAME, g_VBoxNetFltSolarisVNICId);
        mac_handle_t hTmpMacHandle;
        rc = mac_open_by_linkname(pVNIC->szName, &hTmpMacHandle);
        if (rc)
            break;
        mac_close(hTmpMacHandle);
        ASMAtomicIncU64(&g_VBoxNetFltSolarisVNICId);
    } while (1);

    /*
     * Create the VNIC under 'pszLinkName', which can be the one from the VNIC template or can
     * be a physical interface.
     */
    rc = vnic_create(pVNIC->szName, pszLinkName, &AddrType, &MacLen, GuestMac.au8, &MacSlot, 0 /* Mac-Prefix Length */, uVLANId,
                        fFlags, &pVNIC->hLinkId, &Diag, NULL /* Reserved */);
    if (!rc)
    {
        pVNIC->fCreated = true;
        ASMAtomicIncU64(&g_VBoxNetFltSolarisVNICId);

        /*
         * Now try opening the created VNIC.
         */
        rc = mac_open_by_linkid(pVNIC->hLinkId, &pVNIC->hInterface);
        if (!rc)
        {
            /*
             * Initialize the VNIC from the physical interface or the VNIC template.
             */
            rc = vboxNetFltSolarisInitVNIC(pThis, pVNIC);
            if (RT_SUCCESS(rc))
            {
                Log((DEVICE_NAME ":vboxNetFltSolarisCreateVNIC created VNIC '%s' over '%s' with random mac %.6Rhxs\n",
                     pVNIC->szName, pszLinkName, &GuestMac));
                *ppVNIC = pVNIC;
                return VINF_SUCCESS;
            }

            LogRel((DEVICE_NAME ":vboxNetFltSolarisCreateVNIC vboxNetFltSolarisInitVNIC failed. rc=%d\n", rc));
            mac_close(pVNIC->hInterface);
            pVNIC->hInterface = NULL;
        }
        else
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisCreateVNIC logrel failed to open VNIC '%s' over '%s'. rc=%d\n", pVNIC->szName,
                    pThis->szName, rc));
            rc = VERR_INTNET_FLT_VNIC_LINK_ID_NOT_FOUND;
        }

        vboxNetFltSolarisDestroyVNIC(pVNIC);
    }
    else
    {
        LogRel((DEVICE_NAME ":vboxNetFltSolarisCreateVNIC failed to create VNIC '%s' over '%s' rc=%d Diag=%d\n", pVNIC->szName,
                    pszLinkName, rc, Diag));
        rc = VERR_INTNET_FLT_VNIC_CREATE_FAILED;
    }

    vboxNetFltSolarisFreeVNIC(pVNIC);

    return rc;
}


/**
 * Wrapper for getting the datalink ID given the MAC name.
 *
 * @param    pszMacName     The MAC name.
 * @param    pLinkId        Where to store the datalink ID.
 *
 * @returns VBox status code.
 */
DECLINLINE(int) vboxNetFltSolarisGetLinkId(const char *pszMacName, datalink_id_t *pLinkId)
{
    /*
     * dls_mgmt_get_linkid() requires to be in a state to answer upcalls. We should always use this
     * first before resorting to other means to retrieve the MAC name.
     */
    int rc = dls_mgmt_get_linkid(pszMacName, pLinkId);
    if (rc)
        rc = dls_devnet_macname2linkid(pszMacName, pLinkId);

    if (RT_LIKELY(!rc))
        return VINF_SUCCESS;

    LogRel((DEVICE_NAME ":vboxNetFltSolarisGetLinkId failed for '%s'. rc=%d\n", pszMacName, rc));
    return RTErrConvertFromErrno(rc);
}


/**
 * Set the promiscuous mode RX hook.
 *
 * @param    pThis          The VM connection instance.
 * @param    pVNIC          Pointer to the VNIC.
 *
 * @returns VBox status code.
 */
DECLINLINE(int) vboxNetFltSolarisSetPromisc(PVBOXNETFLTINS pThis, PVBOXNETFLTVNIC pVNIC)
{
    int rc = VINF_SUCCESS;
    if (!pVNIC->hPromisc)
    {
        rc = mac_promisc_add(pVNIC->hClient, MAC_CLIENT_PROMISC_FILTERED, vboxNetFltSolarisRecv, pThis, &pVNIC->hPromisc,
                             MAC_PROMISC_FLAGS_NO_TX_LOOP | MAC_PROMISC_FLAGS_VLAN_TAG_STRIP | MAC_PROMISC_FLAGS_NO_PHYS);
        if (RT_UNLIKELY(rc))
            LogRel((DEVICE_NAME ":vboxNetFltSolarisSetPromisc failed. rc=%d\n", rc));
        rc = RTErrConvertFromErrno(rc);
    }
    return rc;
}


/**
 * Clear the promiscuous mode RX hook.
 *
 * @param   pThis           The VM connection instance.
 * @param   pVNIC           Pointer to the VNIC.
 */
DECLINLINE(void) vboxNetFltSolarisRemovePromisc(PVBOXNETFLTINS pThis, PVBOXNETFLTVNIC pVNIC)
{
    if (pVNIC->hPromisc)
    {
        mac_promisc_remove(pVNIC->hPromisc);
        pVNIC->hPromisc = NULL;
    }
}


/* -=-=-=-=-=- Common Hooks -=-=-=-=-=- */


void vboxNetFltPortOsSetActive(PVBOXNETFLTINS pThis, bool fActive)
{
    Log((DEVICE_NAME ":vboxNetFltPortOsSetActive pThis=%p fActive=%d\n", pThis, fActive));

    /*
     * Reactivate/quiesce the interface.
     */
    PVBOXNETFLTVNIC pVNIC = list_head(&pThis->u.s.hVNICs);
    if (fActive)
    {
        for (; pVNIC != NULL; pVNIC = list_next(&pThis->u.s.hVNICs, pVNIC))
            if (pVNIC->hClient)
            {
#if 0
                mac_rx_set(pVNIC->hClient, vboxNetFltSolarisRecv, pThis);
#endif
                vboxNetFltSolarisSetPromisc(pThis, pVNIC);
            }
    }
    else
    {
        for (; pVNIC != NULL; pVNIC = list_next(&pThis->u.s.hVNICs, pVNIC))
            if (pVNIC->hClient)
            {
#if 0
                mac_rx_clear(pVNIC->hClient);
#endif
                vboxNetFltSolarisRemovePromisc(pThis, pVNIC);
            }
    }
}


int vboxNetFltOsDisconnectIt(PVBOXNETFLTINS pThis)
{
    Log((DEVICE_NAME ":vboxNetFltOsDisconnectIt pThis=%p\n", pThis));
    return VINF_SUCCESS;
}


int  vboxNetFltOsConnectIt(PVBOXNETFLTINS pThis)
{
    Log((DEVICE_NAME ":vboxNetFltOsConnectIt pThis=%p\n", pThis));
    return VINF_SUCCESS;
}


void vboxNetFltOsDeleteInstance(PVBOXNETFLTINS pThis)
{
    Log((DEVICE_NAME ":vboxNetFltOsDeleteInstance pThis=%p\n", pThis));

    if (pThis->u.s.hNotify)
        mac_notify_remove(pThis->u.s.hNotify, B_TRUE /* Wait */);

    /*
     * Destroy all managed VNICs. If a VNIC was passed to us, there
     * will be only 1 item in the list, otherwise as many interfaces
     * that were somehow not destroyed using DisconnectInterface() will be
     * present.
     */
    PVBOXNETFLTVNIC pVNIC = NULL;
    while ((pVNIC = list_remove_head(&pThis->u.s.hVNICs)) != NULL)
    {
        vboxNetFltSolarisDestroyVNIC(pVNIC);
        vboxNetFltSolarisFreeVNIC(pVNIC);
    }

    list_destroy(&pThis->u.s.hVNICs);
}


int vboxNetFltOsInitInstance(PVBOXNETFLTINS pThis, void *pvContext)
{
    Log((DEVICE_NAME ":vboxNetFltOsInitInstance pThis=%p pvContext=%p\n", pThis, pvContext));

    /*
     * Figure out if the interface is a VNIC or a physical/etherstub/whatever NIC, then
     * do the actual VNIC creation if necessary in vboxNetFltPortOsConnectInterface().
     */
    mac_handle_t hInterface;
    int rc = mac_open_by_linkname(pThis->szName, &hInterface);
    if (RT_LIKELY(!rc))
    {
        rc = mac_is_vnic(hInterface);
        if (!rc)
        {
            Log((DEVICE_NAME ":vboxNetFltOsInitInstance pThis=%p physical interface '%s' detected.\n", pThis, pThis->szName));
            pThis->u.s.fIsVNIC = false;
        }
        else
        {
            pThis->u.s.fIsVNIC = true;
            if (RTStrNCmp(pThis->szName, VBOXBOW_VNIC_TEMPLATE_NAME, sizeof(VBOXBOW_VNIC_TEMPLATE_NAME) - 1) == 0)
            {
                Log((DEVICE_NAME ":vboxNetFltOsInitInstance pThis=%p VNIC template '%s' detected.\n", pThis, pThis->szName));
                pThis->u.s.fIsVNICTemplate = true;
            }
        }

        if (    pThis->u.s.fIsVNIC
            && !pThis->u.s.fIsVNICTemplate)
            Log((DEVICE_NAME ":vboxNetFltOsInitInstance pThis=%p VNIC '%s' detected.\n", pThis, pThis->szName));

        /*
         * Report info. (host MAC address, promiscuous, GSO capabilities etc.) to IntNet.
         */
        rc = vboxNetFltSolarisReportInfo(pThis, hInterface, pThis->u.s.fIsVNIC);
        if (RT_FAILURE(rc))
            LogRel((DEVICE_NAME ":vboxNetFltOsInitInstance failed to report info. rc=%d\n", rc));

        mac_close(hInterface);
    }
    else
    {
        LogRel((DEVICE_NAME ":vboxNetFltOsInitInstance failed to open link '%s'! rc=%d\n", pThis->szName, rc));
        rc = VERR_INTNET_FLT_IF_FAILED;
    }

    return rc;
}


int vboxNetFltOsPreInitInstance(PVBOXNETFLTINS pThis)
{
    /*
     * Init. the solaris specific data.
     */
    pThis->u.s.fIsVNIC         = false;
    pThis->u.s.fIsVNICTemplate = false;
    list_create(&pThis->u.s.hVNICs, sizeof(VBOXNETFLTVNIC), offsetof(VBOXNETFLTVNIC, hNode));
    pThis->u.s.hNotify         = NULL;
    RT_ZERO(pThis->u.s.MacAddr);
    return VINF_SUCCESS;
}


bool vboxNetFltOsMaybeRediscovered(PVBOXNETFLTINS pThis)
{
    /*
     * @todo Think about this.
     */
    return false;
}


int vboxNetFltPortOsXmit(PVBOXNETFLTINS pThis, void *pvIfData, PINTNETSG pSG, uint32_t fDst)
{
    /*
     * Validate parameters.
     */
    PVBOXNETFLTVNIC pVNIC = pvIfData;
    AssertPtrReturn(pVNIC, VERR_INVALID_POINTER);
    AssertMsgReturn(pVNIC->u32Magic == VBOXNETFLTVNIC_MAGIC,
                    ("Invalid magic=%#x (expected %#x)\n", pVNIC->u32Magic, VBOXNETFLTVNIC_MAGIC),
                    VERR_INVALID_MAGIC);

    /*
     * Xmit the packet down the appropriate VNIC interface.
     */
    int rc = VINF_SUCCESS;
    mblk_t *pMsg = vboxNetFltSolarisMBlkFromSG(pThis, pSG, fDst);
    if (RT_LIKELY(pMsg))
    {
        Log((DEVICE_NAME ":vboxNetFltPortOsXmit pThis=%p cbData=%d\n", pThis, MBLKL(pMsg)));

        mac_tx_cookie_t pXmitCookie = mac_tx(pVNIC->hClient, pMsg, 0 /* Hint */, MAC_DROP_ON_NO_DESC, NULL /* return message */);
        if (RT_LIKELY(!pXmitCookie))
            return VINF_SUCCESS;

        pMsg = NULL;
        rc = VERR_NET_IO_ERROR;
        LogRel((DEVICE_NAME ":vboxNetFltPortOsXmit Xmit failed pVNIC=%p.\n", pVNIC));
    }
    else
    {
        LogRel((DEVICE_NAME ":vboxNetFltPortOsXmit no memory for allocating Xmit packet.\n"));
        rc = VERR_NO_MEMORY;
    }

    return rc;
}


void vboxNetFltPortOsNotifyMacAddress(PVBOXNETFLTINS pThis, void *pvIfData, PCRTMAC pMac)
{
    Log((DEVICE_NAME ":vboxNetFltPortOSNotifyMacAddress pszIf=%s pszVNIC=%s MAC=%.6Rhxs\n", pThis->szName,
             ((PVBOXNETFLTVNIC)pvIfData)->szName, pMac));

    /*
     * Validate parameters.
     */
    PVBOXNETFLTVNIC pVNIC = pvIfData;
    AssertPtrReturnVoid(pVNIC);
    AssertMsgReturnVoid(pVNIC->u32Magic == VBOXNETFLTVNIC_MAGIC,
                        ("Invalid pVNIC=%p magic=%#x (expected %#x)\n", pvIfData, pVNIC->u32Magic, VBOXNETFLTVNIC_MAGIC));
    AssertMsgReturnVoid(pVNIC->hLinkId != DATALINK_INVALID_LINKID,
                        ("Invalid hLinkId pVNIC=%p magic=%#x\n", pVNIC, pVNIC->u32Magic));

    /*
     * Set the MAC address of the VNIC to the one used by the VM interface.
     */
    uchar_t au8GuestMac[MAXMACADDRLEN];
    bcopy(pMac->au8, au8GuestMac, sizeof(RTMAC));

    vnic_mac_addr_type_t AddrType = VNIC_MAC_ADDR_TYPE_FIXED;
    vnic_ioc_diag_t      Diag     = VNIC_IOC_DIAG_NONE;
    int                  MacSlot  = 0;
    int                  MacLen   = sizeof(RTMAC);

    int rc = vnic_modify_addr(pVNIC->hLinkId, &AddrType, &MacLen, au8GuestMac, &MacSlot, 0 /* Mac-Prefix Length */, &Diag);
    if (RT_LIKELY(!rc))
    {
        /*
         * Remove existing unicast address, promisc. and the RX hook.
         */
#if 0
        if (pVNIC->hUnicast)
        {
            mac_rx_clear(pVNIC->hClient);
            mac_unicast_remove(pVNIC->hClient, pVNIC->hUnicast);
            pVNIC->hUnicast = NULL;
        }
#endif

        if (pVNIC->hPromisc)
        {
            mac_promisc_remove(pVNIC->hPromisc);
            pVNIC->hPromisc = NULL;
        }

        mac_diag_t MacDiag = MAC_DIAG_NONE;
        /* uint16_t uVLANId = pVNIC->pVNICTemplate ? pVNIC->pVNICTemplate->uVLANId : 0; */
#if 0
        rc = mac_unicast_add(pVNIC->hClient, NULL, MAC_UNICAST_PRIMARY, &pVNIC->hUnicast, 0 /* VLAN Id */, &MacDiag);
#endif
        if (RT_LIKELY(!rc))
        {
            rc = vboxNetFltSolarisSetPromisc(pThis, pVNIC);
#if 0
            if (RT_SUCCESS(rc))
            {
                /*
                 * Set the RX receive function.
                 * This shouldn't be necessary as vboxNetFltPortOsSetActive() will be invoked after this, but in the future,
                 * if the guest NIC changes MAC address this may not be followed by a vboxNetFltPortOsSetActive() call,
                 * so set it here anyway.
                 */
                mac_rx_set(pVNIC->hClient, vboxNetFltSolarisRecv, pThis);
                Log((DEVICE_NAME ":vboxNetFltPortOsNotifyMacAddress successfully added unicast address %.6Rhxs\n", pMac));
            }
            else
                LogRel((DEVICE_NAME ":vboxNetFltPortOsNotifyMacAddress failed to set promiscuous mode. rc=%d\n", rc));
            mac_unicast_remove(pVNIC->hClient,  pVNIC->hUnicast);
            pVNIC->hUnicast = NULL;
#endif
        }
        else
        {
            LogRel((DEVICE_NAME ":vboxNetFltPortOsNotifyMacAddress failed to add primary unicast address. rc=%d Diag=%d\n", rc,
                    MacDiag));
        }
    }
    else
    {
        /*
         * They really ought to use EEXIST, but I'm afraid this error comes from the VNIC device driver directly.
         * Sequence: vnic_modify_addr()->mac_unicast_primary_set()->mac_update_macaddr() which uses a function pointer
         * to the MAC driver (calls mac_vnic_unicast_set() in our case). Documented here if the error code should change we know
         * where to look.
         */
        if (rc == ENOTSUP)
        {
            LogRel((DEVICE_NAME ":vboxNetFltPortOsNotifyMacAddress: failed! a VNIC with mac %.6Rhxs probably already exists.",
                    pMac, rc));
            LogRel((DEVICE_NAME ":vboxNetFltPortOsNotifyMacAddress: This NIC cannot establish connection. szName=%s szVNIC=%s\n",
                    pThis->szName, pVNIC->szName));
        }
        else
            LogRel((DEVICE_NAME ":vboxNetFltPortOsNotifyMacAddress failed! mac %.6Rhxs rc=%d Diag=%d\n", pMac, rc, Diag));
    }
}


int vboxNetFltPortOsConnectInterface(PVBOXNETFLTINS pThis, void *pvIf, void **ppvIfData)
{
    Log((DEVICE_NAME ":vboxNetFltPortOsConnectInterface pThis=%p pvIf=%p\n", pThis, pvIf));

    int rc = VINF_SUCCESS;

    /*
     * If the underlying interface is a physical interface or a VNIC template, we need to create
     * a VNIC per guest NIC.
     */
    if (  !pThis->u.s.fIsVNIC
        || pThis->u.s.fIsVNICTemplate)
    {
        PVBOXNETFLTVNIC pVNIC = NULL;
        rc = vboxNetFltSolarisCreateVNIC(pThis, &pVNIC);
        if (RT_SUCCESS(rc))
        {
            /*
             * VM Interface<->VNIC association so that we can Xmit/Recv on the right ones.
             */
            pVNIC->pvIf = pvIf;
            *ppvIfData = pVNIC;

            /*
             * Add the created VNIC to the list of VNICs we manage.
             */
            list_insert_tail(&pThis->u.s.hVNICs, pVNIC);
            return VINF_SUCCESS;
        }
        else
            LogRel((DEVICE_NAME ":vboxNetFltPortOsConnectInterface failed to create VNIC rc=%d\n", rc));
    }
    else
    {
        /*
         * This is a VNIC passed to us, use it directly.
         */
        PVBOXNETFLTVNIC pVNIC = vboxNetFltSolarisAllocVNIC();
        if (RT_LIKELY(pVNIC))
        {
            pVNIC->fCreated = false;

            rc = mac_open_by_linkname(pThis->szName, &pVNIC->hInterface);
            if (!rc)
            {
                /*
                 * Obtain the data link ID for this VNIC, it's needed for modifying the MAC address among other things.
                 */
                rc = vboxNetFltSolarisGetLinkId(pThis->szName, &pVNIC->hLinkId);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Initialize the VNIC and add it to the list of managed VNICs.
                     */
                    RTStrPrintf(pVNIC->szName, sizeof(pVNIC->szName), "%s", pThis->szName);
                    rc = vboxNetFltSolarisInitVNIC(pThis, pVNIC);
                    if (!rc)
                    {
                        pVNIC->pvIf = pvIf;
                        *ppvIfData = pVNIC;
                        list_insert_head(&pThis->u.s.hVNICs, pVNIC);
                        return VINF_SUCCESS;
                    }
                    else
                        LogRel((DEVICE_NAME ":vboxNetFltPortOsConnectInterface failed to initialize VNIC. rc=%d\n", rc));
                }
                else
                {
                    LogRel((DEVICE_NAME ":vboxNetFltPortOsConnectInterface failed to get link id for '%s'. rc=%d\n",
                            pThis->szName, rc));
                }
            }
            else
            {
                LogRel((DEVICE_NAME ":vboxNetFltPortOsConnectInterface failed to open VNIC '%s'. rc=%d\n", pThis->szName, rc));
                rc = VERR_OPEN_FAILED;
            }

            vboxNetFltSolarisFreeVNIC(pVNIC);
        }
        else
        {
            LogRel((DEVICE_NAME ":vboxNetFltOsInitInstance failed to allocate VNIC private data.\n"));
            rc = VERR_NO_MEMORY;
        }
    }

    return rc;
}


int vboxNetFltPortOsDisconnectInterface(PVBOXNETFLTINS pThis, void *pvIfData)
{
    Log((DEVICE_NAME ":vboxNetFltPortOsDisconnectInterface pThis=%p\n", pThis));

    /*
     * It is possible we get called when vboxNetFltPortOsConnectInterface() didn't succeed
     * in which case pvIfData will be NULL. See intnetR0NetworkCreateIf() pfnConnectInterface call
     * through reference counting in SUPR0ObjRelease() for the "pIf" object.
     */
    PVBOXNETFLTVNIC pVNIC = pvIfData;
    if (RT_LIKELY(pVNIC))
    {
        AssertMsgReturn(pVNIC->u32Magic == VBOXNETFLTVNIC_MAGIC,
                        ("Invalid magic=%#x (expected %#x)\n", pVNIC->u32Magic, VBOXNETFLTVNIC_MAGIC), VERR_INVALID_POINTER);

        /*
         * If the underlying interface is a physical interface or a VNIC template, we need to delete the created VNIC.
         */
        if (   !pThis->u.s.fIsVNIC
            || pThis->u.s.fIsVNICTemplate)
        {
            /*
             * Remove the VNIC from the list, destroy and free it.
             */
            list_remove(&pThis->u.s.hVNICs, pVNIC);
            Log((DEVICE_NAME ":vboxNetFltPortOsDisconnectInterface destroying pVNIC=%p\n", pVNIC));
            vboxNetFltSolarisDestroyVNIC(pVNIC);
            vboxNetFltSolarisFreeVNIC(pVNIC);
        }
    }

    return VINF_SUCCESS;
}

