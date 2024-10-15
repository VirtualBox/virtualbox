/* $Id: VBoxNetFlt-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/assert.h>
#include <iprt/alloca.h>
#include <iprt/net.h>
#include <iprt/mem.h>
#include <iprt/thread.h>
#include <iprt/spinlock.h>
#include <iprt/crc.h>
#include <iprt/err.h>
#include <iprt/ctype.h>
#define VBOXNETFLT_SOLARIS_IPV6_POLLING
#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
# include <iprt/timer.h>
# include <iprt/time.h>
#endif

#include <inet/ip.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/kstr.h>
#include <sys/file.h>
#include <sys/sockio.h>
#include <sys/strsubr.h>
#include <sys/pathname.h>
#include <sys/t_kuser.h>

#include <sys/types.h>
#include <sys/dlpi.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/ethernet.h>
#include <sys/stat.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/strsun.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/ctf_api.h>

// Workaround for very strange define in sys/user.h
// #define u       (curproc->p_user)       /* user is now part of proc structure */
#ifdef u
#undef u
#endif

#define VBOXNETFLT_OS_SPECFIC 1
#include "../VBoxNetFltInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The module name. */
#define DEVICE_NAME                     "vboxflt"
/** The module descriptions as seen in 'modinfo'. */
#define DEVICE_DESC_DRV                 "VirtualBox NetDrv"
#define DEVICE_DESC_MOD                 "VirtualBox NetMod"

#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
/** Driver properties */
# define VBOXNETFLT_IP6POLLINTERVAL      "ipv6-pollinterval"
#endif

/** Maximum loopback packet queue size per interface */
#define VBOXNETFLT_LOOPBACK_SIZE        32

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


/*********************************************************************************************************************************
*   Global Functions                                                                                                             *
*********************************************************************************************************************************/
/**
 * Stream Driver hooks.
 */
static int VBoxNetFltSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pArg, void **ppvResult);
static int VBoxNetFltSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd);
static int VBoxNetFltSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd);
static int VBoxNetFltSolarisQuiesceNotNeeded(dev_info_t *pDip);

/**
 * Stream Module hooks.
 */
static int VBoxNetFltSolarisModOpen(queue_t *pQueue, dev_t *pDev, int fFile, int fStream, cred_t *pCred);
static int VBoxNetFltSolarisModClose(queue_t *pQueue, int fFile, cred_t *pCred);
static int VBoxNetFltSolarisModReadPut(queue_t *pQueue, mblk_t *pMsg);
static int VBoxNetFltSolarisModWritePut(queue_t *pQueue, mblk_t *pMsg);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Streams: module info.
 */
static struct module_info g_VBoxNetFltSolarisModInfo =
{
    0xbad,                          /* module id */
    DEVICE_NAME,
    0,                              /* min. packet size */
    INFPSZ,                         /* max. packet size */
    0,                              /* hi-water mark */
    0                               /* lo-water mark */
};

/**
 * Streams: read queue hooks.
 */
static struct qinit g_VBoxNetFltSolarisReadQ =
{
    VBoxNetFltSolarisModReadPut,
    NULL,                           /* service */
    VBoxNetFltSolarisModOpen,
    VBoxNetFltSolarisModClose,
    NULL,                           /* admin (reserved) */
    &g_VBoxNetFltSolarisModInfo,
    NULL                            /* module stats */
};

/**
 * Streams: write queue hooks.
 */
static struct qinit g_VBoxNetFltSolarisWriteQ =
{
    VBoxNetFltSolarisModWritePut,
    NULL,                           /* service */
    NULL,                           /* open */
    NULL,                           /* close */
    NULL,                           /* admin (reserved) */
    &g_VBoxNetFltSolarisModInfo,
    NULL                            /* module stats */
};

/**
 * Streams: IO stream tab.
 */
static struct streamtab g_VBoxNetFltSolarisStreamTab =
{
    &g_VBoxNetFltSolarisReadQ,
    &g_VBoxNetFltSolarisWriteQ,
    NULL,                           /* muxread init */
    NULL                            /* muxwrite init */
};

/**
 * cb_ops: driver char/block entry points
 */
static struct cb_ops g_VBoxNetFltSolarisCbOps =
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
    &g_VBoxNetFltSolarisStreamTab,
    D_NEW | D_MP | D_MTQPAIR | D_MTOUTPERIM | D_MTOCEXCL, /* compat. flag */
    CB_REV                          /* revision */
};

/**
 * dev_ops: driver entry/exit and other ops.
 */
static struct dev_ops g_VBoxNetFltSolarisDevOps =
{
    DEVO_REV,                       /* driver build revision */
    0,                              /* ref count */
    VBoxNetFltSolarisGetInfo,
    nulldev,                        /* identify */
    nulldev,                        /* probe */
    VBoxNetFltSolarisAttach,
    VBoxNetFltSolarisDetach,
    nodev,                          /* reset */
    &g_VBoxNetFltSolarisCbOps,
    (struct bus_ops *)0,
    nodev,                          /* power */
    VBoxNetFltSolarisQuiesceNotNeeded
};

/**
 * modldrv: export driver specifics to kernel
 */
static struct modldrv g_VBoxNetFltSolarisDriver =
{
    &mod_driverops,                 /* extern from kernel */
    DEVICE_DESC_DRV " " VBOX_VERSION_STRING "r" RT_XSTR(VBOX_SVN_REV),
    &g_VBoxNetFltSolarisDevOps
};

/**
 * fmodsw: streams module ops
 */
static struct fmodsw g_VBoxNetFltSolarisModOps =
{
    DEVICE_NAME,
    &g_VBoxNetFltSolarisStreamTab,
    D_NEW | D_MP | D_MTQPAIR | D_MTOUTPERIM | D_MTOCEXCL
};

/**
 * modlstrmod: streams module specifics to kernel
 */
static struct modlstrmod g_VBoxNetFltSolarisModule =
{
    &mod_strmodops,                 /* extern from kernel */
    DEVICE_DESC_MOD " " VBOX_VERSION_STRING "r" RT_XSTR(VBOX_SVN_REV),
    &g_VBoxNetFltSolarisModOps
};

/**
 * modlinkage: export install/remove/info to the kernel
 */
static struct modlinkage g_VBoxNetFltSolarisModLinkage =
{
    MODREV_1,                        /* loadable module system revision */
    {
        &g_VBoxNetFltSolarisDriver,  /* streams driver framework */
        &g_VBoxNetFltSolarisModule,  /* streams module framework */
        NULL                         /* terminate array of linkage structures */
    }
};

struct vboxnetflt_state_t;

/**
 * vboxnetflt_dladdr_t: DL SAP address format
 */
typedef struct vboxnetflt_dladdr_t
{
    ether_addr_t Mac;
    uint16_t SAP;
} vboxnetflt_dladdr_t;

#define VBOXNETFLT_DLADDRL        sizeof(vboxnetflt_dladdr_t)

/**
 * which stream is this?
 */
typedef enum VBOXNETFLTSTREAMTYPE
{
    kUndefined = 0,
    kIp4Stream = 0x1b,
    kIp6Stream = 0xcc,
    kArpStream = 0xab,
    kPromiscStream = 0xdf
} VBOXNETFLTSTREAMTYPE;

/**
 * loopback packet identifier
 */
typedef struct VBOXNETFLTPACKETID
{
    struct VBOXNETFLTPACKETID *pNext;
    uint16_t cbPacket;
    uint16_t Checksum;
    RTMAC SrcMac;
    RTMAC DstMac;
} VBOXNETFLTPACKETID;
typedef struct VBOXNETFLTPACKETID *PVBOXNETFLTPACKETID;

/**
 * vboxnetflt_stream_t: per-stream data (multiple streams per interface)
 */
typedef struct vboxnetflt_stream_t
{
    int DevMinor;                         /* minor device no. (for clone) */
    queue_t *pReadQueue;                  /* read side queue */
    struct vboxnetflt_stream_t *pNext;    /* next stream in list */
    PVBOXNETFLTINS volatile pThis;        /* the backend instance */
    VBOXNETFLTSTREAMTYPE Type;            /* the type of the stream */
} vboxnetflt_stream_t;

/**
 * vboxnetflt_promisc_stream_t: per-interface dedicated stream data
 */
typedef struct vboxnetflt_promisc_stream_t
{
    vboxnetflt_stream_t Stream;           /* dedicated/promiscuous stream */
    bool fPromisc;                        /* cached promiscuous value */
    bool fRawMode;                        /* whether raw mode request was successful */
    uint32_t ModeReqId;                   /* track MIOCTLs for swallowing our fake request acknowledgements */
#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
    PRTTIMER pIp6Timer;                   /* ipv6 stream poll timer for dynamic ipv6 stream attachment */
#endif
    size_t cLoopback;                     /* loopback queue size list */
    timeout_id_t volatile TimeoutId;      /* timeout id of promisc. req */
    PVBOXNETFLTPACKETID pHead;            /* loopback packet identifier head */
    PVBOXNETFLTPACKETID pTail;            /* loopback packet identifier tail */
} vboxnetflt_promisc_stream_t;

typedef struct vboxnetflt_promisc_params_t
{
    PVBOXNETFLTINS pThis;                 /* the backend instance */
    bool fPromiscOn;                      /* whether promiscuous req. on or off */
} vboxnetflt_promisc_params_t;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int vboxNetFltSolarisSetRawMode(vboxnetflt_promisc_stream_t *pPromiscStream);
/* static int vboxNetFltSolarisSetFastMode(queue_t *pQueue); */

static int vboxNetFltSolarisPhysAddrReq(queue_t *pQueue);
static void vboxNetFltSolarisCachePhysAddr(PVBOXNETFLTINS pThis, mblk_t *pPhysAddrAckMsg);
static int vboxNetFltSolarisBindReq(queue_t *pQueue, int SAP);
static int vboxNetFltSolarisNotifyReq(queue_t *pQueue);

/*  static int vboxNetFltSolarisUnitDataToRaw(PVBOXNETFLTINS pThis, mblk_t *pMsg, mblk_t **ppRawMsg); */
static int vboxNetFltSolarisRawToUnitData(mblk_t *pMsg, mblk_t **ppDlpiMsg);

static inline void vboxNetFltSolarisInitPacketId(PVBOXNETFLTPACKETID pTag, mblk_t *pMsg);
static int vboxNetFltSolarisQueueLoopback(PVBOXNETFLTINS pThis, vboxnetflt_promisc_stream_t *pPromiscStream, mblk_t *pMsg);
static bool vboxNetFltSolarisIsOurMBlk(PVBOXNETFLTINS pThis, vboxnetflt_promisc_stream_t *pPromiscStream, mblk_t *pMsg);

static mblk_t *vboxNetFltSolarisMBlkFromSG(PVBOXNETFLTINS pThis, PINTNETSG pSG, uint32_t fDst);
static unsigned vboxNetFltSolarisMBlkCalcSGSegs(PVBOXNETFLTINS pThis, mblk_t *pMsg);
static int vboxNetFltSolarisMBlkToSG(PVBOXNETFLTINS pThis, mblk_t *pMsg, PINTNETSG pSG, unsigned cSegs, uint32_t fSrc);
static int vboxNetFltSolarisRecv(PVBOXNETFLTINS pThis, vboxnetflt_stream_t *pStream, queue_t *pQueue, mblk_t *pMsg);
/* static mblk_t *vboxNetFltSolarisFixChecksums(mblk_t *pMsg); */
/* static void vboxNetFltSolarisAnalyzeMBlk(mblk_t *pMsg); */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Global device info handle. */
static dev_info_t *g_pVBoxNetFltSolarisDip = NULL;

/** The (common) global data. */
static VBOXNETFLTGLOBALS g_VBoxNetFltSolarisGlobals;

/** The list of all opened streams. */
vboxnetflt_stream_t *g_VBoxNetFltSolarisStreams = NULL;

/** Global mutex protecting open/close. */
static RTSEMFASTMUTEX g_VBoxNetFltSolarisMtx = NIL_RTSEMFASTMUTEX;

/** Global credentials using during open/close. */
static cred_t *g_pVBoxNetFltSolarisCred = NULL;

/**
 * g_VBoxNetFltInstance is the current PVBOXNETFLTINS to be associated with the stream being created
 * in ModOpen. This is just shared global data between the dynamic attach and the ModOpen procedure.
 */
PVBOXNETFLTINS volatile g_VBoxNetFltSolarisInstance = NULL;

/** Goes along with the instance to determine type of stream being opened/created. */
VBOXNETFLTSTREAMTYPE volatile g_VBoxNetFltSolarisStreamType = kUndefined;

#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
/** Global IPv6 polling interval */
static int g_VBoxNetFltSolarisPollInterval = -1;
#endif

static int s_off_vnode = -1;
#define VNODE_FOR_FILE_T(filetpointer)         (*(struct vnode **)((char *)(filetpointer) + s_off_vnode))


static int
vboxNetFltSolarisCtfGetMemberOffset(ctf_file_t *pCtfFile, const char *pszStruct, const char *pszMember, int *pOffset)
{
    AssertReturn(pCtfFile, VERR_INVALID_PARAMETER);
    AssertReturn(pszStruct, VERR_INVALID_PARAMETER);
    AssertReturn(pszMember, VERR_INVALID_PARAMETER);
    AssertReturn(pOffset, VERR_INVALID_PARAMETER);

    ctf_id_t TypeId = ctf_lookup_by_name(pCtfFile, pszStruct);
    if (TypeId != CTF_ERR)
    {
        ctf_membinfo_t MemberInfo;
        bzero(&MemberInfo, sizeof(MemberInfo));
        if (ctf_member_info(pCtfFile, TypeId, pszMember, &MemberInfo) != CTF_ERR)
        {
            *pOffset = (MemberInfo.ctm_offset >> 3);
            LogRel((DEVICE_NAME ":%s::%s at %d\n", pszStruct, pszMember, *pOffset));
            return VINF_SUCCESS;
        }
        else
            LogRel((DEVICE_NAME ":ctf_member_info failed for struct %s member %s\n", pszStruct, pszMember));
    }
    else
        LogRel((DEVICE_NAME ":ctf_lookup_by_name failed for struct %s\n", pszStruct));

    return VERR_NOT_FOUND;
}


static int
vboxNetFltSolarisProbeCtf(void)
{
    /*
     * CTF probing for fluid f_vnode member in file_t.
     */
    int rc = VERR_INTERNAL_ERROR;
    modctl_t *pModCtl = mod_hold_by_name("genunix");
    if (pModCtl)
    {
        int err;
        mutex_enter(&mod_lock);
        ctf_file_t *pCtfFile = ctf_modopen(pModCtl->mod_mp, &err);
        mutex_exit(&mod_lock);
        if (pCtfFile)
        {
            rc = vboxNetFltSolarisCtfGetMemberOffset(pCtfFile, "file_t", "f_vnode", &s_off_vnode);
            ctf_close(pCtfFile);
        }
        else
            LogRel((DEVICE_NAME ":ctf_modopen failed. err=%d\n", err));

        mod_release_mod(pModCtl);
    }
    else
        LogRel((DEVICE_NAME ":mod_hold_by_name failed.\n"));

    return rc;
}


/**
 * Kernel entry points
 */
int _init(void)
{
    LogFunc((DEVICE_NAME ":_init\n"));

    /*
     * Prevent module autounloading.
     */
    modctl_t *pModCtl = mod_getctl(&g_VBoxNetFltSolarisModLinkage);
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
        rc = vboxNetFltSolarisProbeCtf();
        if (RT_SUCCESS(rc))
        {
            /*
             * Initialize Solaris specific globals here.
             */
            g_VBoxNetFltSolarisStreams = NULL;
            g_VBoxNetFltSolarisInstance = NULL;
            g_pVBoxNetFltSolarisCred = crdup(kcred);
            if (RT_LIKELY(g_pVBoxNetFltSolarisCred))
            {
                rc = RTSemFastMutexCreate(&g_VBoxNetFltSolarisMtx);
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

                    RTSemFastMutexDestroy(g_VBoxNetFltSolarisMtx);
                    g_VBoxNetFltSolarisMtx = NIL_RTSEMFASTMUTEX;
                }
            }
            else
            {
                LogRel((DEVICE_NAME ":failed to allocate credentials.\n"));
                rc = VERR_NO_MEMORY;
            }
        }
        else
            LogRel((DEVICE_NAME ":vboxNetFltSolarisProbeCtf failed. rc=%d\n", rc));

        RTR0Term();
    }
    else
        LogRel((DEVICE_NAME ":failed to initialize IPRT (rc=%d)\n", rc));

    memset(&g_VBoxNetFltSolarisGlobals, 0, sizeof(g_VBoxNetFltSolarisGlobals));
    return RTErrConvertToErrno(rc);
}


int _fini(void)
{
    int rc;
    LogFunc((DEVICE_NAME ":_fini\n"));

    /*
     * Undo the work done during start (in reverse order).
     */
    rc = vboxNetFltTryDeleteIdcAndGlobals(&g_VBoxNetFltSolarisGlobals);
    if (RT_FAILURE(rc))
    {
        LogRel((DEVICE_NAME ":_fini - busy!\n"));
        return EBUSY;
    }

    rc = mod_remove(&g_VBoxNetFltSolarisModLinkage);
    if (!rc)
    {
        if (g_pVBoxNetFltSolarisCred)
        {
            crfree(g_pVBoxNetFltSolarisCred);
            g_pVBoxNetFltSolarisCred = NULL;
        }

        if (g_VBoxNetFltSolarisMtx != NIL_RTSEMFASTMUTEX)
        {
            RTSemFastMutexDestroy(g_VBoxNetFltSolarisMtx);
            g_VBoxNetFltSolarisMtx = NIL_RTSEMFASTMUTEX;
        }

        RTR0Term();
    }

    return rc;
}


int _info(struct modinfo *pModInfo)
{
    LogFunc((DEVICE_NAME ":_info\n"));

    int rc = mod_info(&g_VBoxNetFltSolarisModLinkage, pModInfo);

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
static int VBoxNetFltSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ":VBoxNetFltSolarisAttach pDip=%p enmCmd=%d\n", pDip, enmCmd));

    switch (enmCmd)
    {
        case DDI_ATTACH:
        {
            int rc = ddi_create_minor_node(pDip, DEVICE_NAME, S_IFCHR, 0 /* instance */, DDI_PSEUDO, CLONE_DEV);
            if (rc == DDI_SUCCESS)
            {
                g_pVBoxNetFltSolarisDip = pDip;
#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
                /*
                 * Get the user prop. for polling interval.
                 */
                int Interval = ddi_getprop(DDI_DEV_T_ANY, pDip, DDI_PROP_DONTPASS, VBOXNETFLT_IP6POLLINTERVAL, -1 /* default */);
                if (Interval == -1)
                    Log((DEVICE_NAME ":vboxNetFltSolarisSetupIp6Polling: no poll interval property specified. Skipping Ipv6 polling.\n"));
                else if (Interval < 1 || Interval > 120)
                {
                    LogRel((DEVICE_NAME ":vboxNetFltSolarisSetupIp6Polling: Invalid polling interval %d. Expected between 1 and 120 secs.\n",
                                        Interval));
                    Interval = -1;
                }

                g_VBoxNetFltSolarisPollInterval = Interval;
#endif
                ddi_report_dev(pDip);
                return DDI_SUCCESS;
            }
            else
                LogRel((DEVICE_NAME ":VBoxNetFltSolarisAttach failed to create minor node. rc%d\n", rc));
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
static int VBoxNetFltSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ":VBoxNetFltSolarisDetach pDip=%p enmCmd=%d\n", pDip, enmCmd));

    switch (enmCmd)
    {
        case DDI_DETACH:
        {
            ddi_remove_minor_node(pDip, NULL);
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
 * Quiesce not-needed entry point, as Solaris 10 doesn't have any
 * ddi_quiesce_not_needed() function.
 *
 * @param   pDip            The module structure instance.
 *
 * @return  corresponding solaris error code.
 */
static int VBoxNetFltSolarisQuiesceNotNeeded(dev_info_t *pDip)
{
    return DDI_SUCCESS;
}


/**
 * Info entry point, called by solaris kernel for obtaining driver info.
 *
 * @param   pDip            The module structure instance (do not use).
 * @param   enmCmd          Information request type.
 * @param   pvArg           Type specific argument.
 * @param   ppvResult       Where to store the requested info.
 *
 * @returns  corresponding solaris error code.
 */
static int VBoxNetFltSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pvArg, void **ppvResult)
{
    LogFunc((DEVICE_NAME ":VBoxNetFltSolarisGetInfo pDip=%p enmCmd=%d pArg=%p instance=%d\n", pDip, enmCmd,
                getminor((dev_t)pvArg)));

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
            return DDI_SUCCESS;
        }
    }

    return DDI_FAILURE;
}


/**
 * Stream module open entry point, initializes the queue and allows streams processing.
 *
 * @param   pQueue          Pointer to the read queue (cannot be NULL).
 * @param   pDev            Pointer to the dev_t associated with the driver at the end of the stream.
 * @param   fOpenMode       Open mode (always 0 for streams driver, thus ignored).
 * @param   fStreamMode     Stream open mode.
 * @param   pCred           Pointer to user credentials.
 *
 * @returns corresponding solaris error code.
 */
static int VBoxNetFltSolarisModOpen(queue_t *pQueue, dev_t *pDev, int fOpenMode, int fStreamMode, cred_t *pCred)
{
    Assert(pQueue);

    LogFunc((DEVICE_NAME ":VBoxNetFltSolarisModOpen pQueue=%p pDev=%p fOpenMode=%d fStreamMode=%d\n", pQueue, pDev,
            fOpenMode, fStreamMode));

    /*
     * Already open?
     */
    if (pQueue->q_ptr)
    {
        LogRel((DEVICE_NAME ":VBoxNetFltSolarisModOpen invalid open.\n"));
        return ENOENT;
    }

    /*
     * Check that the request was initiated by our code.
     *
     * This ASSUMES that crdup() will return a copy with a unique address and
     * not do any kind of clever pooling.  This check will when combined with
     * g_VBoxNetFltSolarisMtx prevent races and that the instance gets
     * associated with the wrong streams.
     */
    if (pCred != g_pVBoxNetFltSolarisCred)
    {
        LogRel((DEVICE_NAME ":VBoxNetFltSolarisModOpen invalid credentials.\n"));
        return EACCES;
    }

    /*
     * Check for the VirtualBox instance.
     */
    PVBOXNETFLTINS pThis = g_VBoxNetFltSolarisInstance;
    if (!pThis)
    {
        LogRel((DEVICE_NAME ":VBoxNetFltSolarisModOpen failed to get VirtualBox instance.\n"));
        return ENOENT;
    }

    /*
     * Check VirtualBox stream type.
     */
    if (   g_VBoxNetFltSolarisStreamType != kPromiscStream
        && g_VBoxNetFltSolarisStreamType != kArpStream
        && g_VBoxNetFltSolarisStreamType != kIp6Stream
        && g_VBoxNetFltSolarisStreamType != kIp4Stream)
    {
        LogRel((DEVICE_NAME ":VBoxNetFltSolarisModOpen failed due to undefined VirtualBox open mode. Type=%d\n",
                g_VBoxNetFltSolarisStreamType));
        return ENOENT;
    }

    /*
     * Get minor number. For clone opens provide a new dev_t.
     */
    minor_t DevMinor = 0;
    vboxnetflt_stream_t *pStream = NULL;
    vboxnetflt_stream_t **ppPrevStream = &g_VBoxNetFltSolarisStreams;
    if (fStreamMode == CLONEOPEN)
    {
        for (; (pStream = *ppPrevStream) != NULL; ppPrevStream = &pStream->pNext)
        {
            if (DevMinor < pStream->DevMinor)
                break;
            DevMinor++;
        }
        *pDev = makedevice(getmajor(*pDev), DevMinor);
    }
    else
        DevMinor = getminor(*pDev);

    if (g_VBoxNetFltSolarisStreamType == kPromiscStream)
    {
        vboxnetflt_promisc_stream_t *pPromiscStream = RTMemAlloc(sizeof(vboxnetflt_promisc_stream_t));
        if (RT_UNLIKELY(!pPromiscStream))
        {
            LogRel((DEVICE_NAME ":VBoxNetFltSolarisModOpen failed to allocate promiscuous stream data.\n"));
            return ENOMEM;
        }

        pPromiscStream->fPromisc = false;
        pPromiscStream->fRawMode = false;
        pPromiscStream->ModeReqId = 0;
        pPromiscStream->pHead = NULL;
        pPromiscStream->pTail = NULL;
        pPromiscStream->cLoopback = 0;
        pPromiscStream->TimeoutId = 0;
#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
        pPromiscStream->pIp6Timer = NULL;
#endif
        pStream = (vboxnetflt_stream_t *)pPromiscStream;
    }
    else
    {
        /*
         * Allocate & initialize per-stream data. Hook it into the (read and write) queue's module specific data.
         */
        pStream = RTMemAlloc(sizeof(vboxnetflt_stream_t));
        if (RT_UNLIKELY(!pStream))
        {
            LogRel((DEVICE_NAME ":VBoxNetFltSolarisModOpen failed to allocate stream data.\n"));
            return ENOMEM;
        }
    }
    pStream->DevMinor = DevMinor;
    pStream->pReadQueue = pQueue;

    /*
     * Pick up the current global VBOXNETFLTINS instance as
     * the one that we will associate this stream with.
     */
    ASMAtomicUoWritePtr(&pStream->pThis, pThis);
    pStream->Type = g_VBoxNetFltSolarisStreamType;
    switch (pStream->Type)
    {
        case kIp4Stream:        ASMAtomicUoWritePtr((void**)&pThis->u.s.pIp4Stream, pStream);        break;
        case kIp6Stream:        ASMAtomicUoWritePtr((void**)&pThis->u.s.pIp6Stream, pStream);        break;
        case kArpStream:        ASMAtomicUoWritePtr((void**)&pThis->u.s.pArpStream, pStream);        break;
        case kPromiscStream:    ASMAtomicUoWritePtr((void**)&pThis->u.s.pPromiscStream, pStream);    break;
        default:    /* Heh. */
        {
            LogRel((DEVICE_NAME ":VBoxNetFltSolarisModOpen huh!? Invalid stream type %d\n", pStream->Type));
            RTMemFree(pStream);
            return EINVAL;
        }
    }

    pQueue->q_ptr = pStream;
    WR(pQueue)->q_ptr = pStream;

    /*
     * Link it to the list of streams.
     */
    pStream->pNext = *ppPrevStream;
    *ppPrevStream = pStream;

    /*
     * Increment IntNet reference count for this stream.
     */
    vboxNetFltRetain(pThis, false /* fBusy */);

    qprocson(pQueue);

    /*
     * Don't hold the spinlocks across putnext calls as it could
     * (and does mostly) re-enter the put procedure on the same thread.
     */
    if (pStream->Type == kPromiscStream)
    {
        vboxnetflt_promisc_stream_t *pPromiscStream = (vboxnetflt_promisc_stream_t *)pStream;

        /*
         * Bind to SAP 0 (DL_ETHER).
         * Note: We don't support DL_TPR (token passing ring) SAP as that is unnecessary asynchronous
         * work to get DL_INFO_REQ acknowledgements and determine SAP based on the Mac Type etc.
         * Besides TPR doesn't really exist anymore practically as far as I know.
         */
        int rc = vboxNetFltSolarisBindReq(pStream->pReadQueue, 0 /* SAP */);
        if (RT_LIKELY(RT_SUCCESS(rc)))
        {
            /*
             * Request the physical address (we cache the acknowledgement).
             */
            rc = vboxNetFltSolarisPhysAddrReq(pStream->pReadQueue);
            if (RT_LIKELY(RT_SUCCESS(rc)))
            {
                /*
                 * Ask for DLPI link notifications, don't bother check for errors here.
                 */
                vboxNetFltSolarisNotifyReq(pStream->pReadQueue);

                /*
                 * Enable raw mode.
                 */
                rc = vboxNetFltSolarisSetRawMode(pPromiscStream);
                if (RT_FAILURE(rc))
                    LogRel((DEVICE_NAME ":vboxNetFltSolarisSetRawMode failed rc=%Rrc.\n", rc));
            }
            else
                LogRel((DEVICE_NAME ":vboxNetFltSolarisSetRawMode failed rc=%Rrc.\n", rc));
        }
        else
            LogRel((DEVICE_NAME ":vboxNetFltSolarisBindReq failed rc=%Rrc.\n", rc));
    }

    NOREF(fOpenMode);

    Log((DEVICE_NAME ":VBoxNetFltSolarisModOpen returns 0, DevMinor=%d pQueue=%p\n", DevMinor, pStream->pReadQueue));

    return 0;
}


/**
 * Stream module close entry point, undoes the work done on open and closes the stream.
 *
 * @param   pQueue          Pointer to the read queue (cannot be NULL).
 * @param   fOpenMode       Open mode (always 0 for streams driver, thus ignored).
 * @param   pCred           Pointer to user credentials.
 *
 * @returns  corresponding solaris error code.
 */
static int VBoxNetFltSolarisModClose(queue_t *pQueue, int fOpenMode, cred_t *pCred)
{
    Assert(pQueue);

    LogFunc((DEVICE_NAME ":VBoxNetFltSolarisModClose pQueue=%p fOpenMode=%d\n", pQueue, fOpenMode));

    vboxnetflt_stream_t *pStream = NULL;
    vboxnetflt_stream_t **ppPrevStream = NULL;

    /*
     * Get instance data.
     */
    pStream = (vboxnetflt_stream_t *)pQueue->q_ptr;
    if (RT_UNLIKELY(!pStream))
    {
        LogRel((DEVICE_NAME ":VBoxNetFltSolarisModClose failed to get stream.\n"));
        return ENXIO;
    }

    if (pStream->Type == kPromiscStream)
    {
        /*
         * If there are any timeout scheduled, we need to make sure they are cancelled.
         */
        vboxnetflt_promisc_stream_t *pPromiscStream = (vboxnetflt_promisc_stream_t *)pStream;
        timeout_id_t TimeoutId = ASMAtomicReadPtr(&pPromiscStream->TimeoutId);
        if (TimeoutId)
        {
            quntimeout(WR(pPromiscStream->Stream.pReadQueue), TimeoutId);
            ASMAtomicWritePtr(&pPromiscStream->TimeoutId, NULL);
        }

        flushq(pQueue, FLUSHALL);
        flushq(WR(pQueue), FLUSHALL);
    }

    qprocsoff(pQueue);

    if (pStream->Type == kPromiscStream)
    {
        vboxnetflt_promisc_stream_t *pPromiscStream = (vboxnetflt_promisc_stream_t *)pStream;

        mutex_enter(&pStream->pThis->u.s.hMtx);

        /*
         * Free-up loopback buffers.
         */
        PVBOXNETFLTPACKETID pCur = pPromiscStream->pHead;
        while (pCur)
        {
            PVBOXNETFLTPACKETID pNext = pCur->pNext;
            RTMemFree(pCur);
            pCur = pNext;
        }
        pPromiscStream->pHead = NULL;
        pPromiscStream->pTail = NULL;
        pPromiscStream->cLoopback = 0;

#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
        /*
         * Sheer paranoia.
         */
        if (pPromiscStream->pIp6Timer != NULL)
        {
            RTTimerStop(pPromiscStream->pIp6Timer);
            RTTimerDestroy(pPromiscStream->pIp6Timer);
            ASMAtomicUoWriteNullPtr(&pPromiscStream->pIp6Timer);
        }
#endif

        mutex_exit(&pStream->pThis->u.s.hMtx);
    }

    /*
     * Unlink it from the list of streams.
     */
    for (ppPrevStream = &g_VBoxNetFltSolarisStreams; (pStream = *ppPrevStream) != NULL; ppPrevStream = &pStream->pNext)
        if (pStream == (vboxnetflt_stream_t *)pQueue->q_ptr)
            break;
    *ppPrevStream = pStream->pNext;

    /*
     * Delete the stream.
     */
    switch (pStream->Type)
    {
        case kIp4Stream:        ASMAtomicUoWriteNullPtr(&pStream->pThis->u.s.pIp4Stream);     break;
        case kIp6Stream:        ASMAtomicUoWriteNullPtr(&pStream->pThis->u.s.pIp6Stream);     break;
        case kArpStream:        ASMAtomicUoWriteNullPtr(&pStream->pThis->u.s.pArpStream);     break;
        case kPromiscStream:    ASMAtomicUoWriteNullPtr(&pStream->pThis->u.s.pPromiscStream); break;
        default:    /* Heh. */
        {
            AssertRelease(pStream->Type);
            break;
        }
    }

    /*
     * Decrement IntNet reference count for this stream.
     */
    vboxNetFltRelease(pStream->pThis, false /* fBusy */);

    RTMemFree(pStream);
    pQueue->q_ptr = NULL;
    WR(pQueue)->q_ptr = NULL;

    NOREF(fOpenMode);
    NOREF(pCred);

    return 0;
}


/**
 * Read side put procedure for processing messages in the read queue.
 * All streams, bound and unbound share this read procedure.
 *
 * @param   pQueue      Pointer to the read queue.
 * @param   pMsg        Pointer to the message.
 *
 * @returns corresponding solaris error code.
 */
static int VBoxNetFltSolarisModReadPut(queue_t *pQueue, mblk_t *pMsg)
{
    if (!pMsg)
        return 0;

    LogFunc((DEVICE_NAME ":VBoxNetFltSolarisModReadPut pQueue=%p pMsg=%p\n", pQueue, pMsg));

    bool fSendUpstream = true;
    vboxnetflt_stream_t *pStream = pQueue->q_ptr;
    PVBOXNETFLTINS pThis = NULL;

    /*
     * In the unlikely case where VirtualBox crashed and this filter
     * is somehow still in the host stream we must try not to panic the host.
     */
    if (   pStream
        && pStream->Type == kPromiscStream)
    {
        fSendUpstream = false;
        pThis = ASMAtomicUoReadPtrT(&pStream->pThis, PVBOXNETFLTINS);
        if (RT_LIKELY(pThis))
        {
            /*
             * Retain the instance if we're filtering regardless of we are active or not
             * The reason being even when we are inactive we reference the instance (e.g
             * the promiscuous OFF acknowledgement case).
             */
            RTSpinlockAcquire(pThis->hSpinlock);
            const bool fActive = pThis->enmTrunkState == INTNETTRUNKIFSTATE_ACTIVE;
            vboxNetFltRetain(pThis, true /* fBusy */);
            RTSpinlockRelease(pThis->hSpinlock);

            vboxnetflt_promisc_stream_t *pPromiscStream = (vboxnetflt_promisc_stream_t *)pStream;

            switch (DB_TYPE(pMsg))
            {
                case M_DATA:
                {
                    Log((DEVICE_NAME ":VBoxNetFltSolarisModReadPut M_DATA\n"));

                    if (   fActive
                        && pPromiscStream->fRawMode)
                    {
                        vboxNetFltSolarisRecv(pThis, pStream, pQueue, pMsg);
                    }
                    break;
                }

                case M_PROTO:
                case M_PCPROTO:
                {
                    union DL_primitives *pPrim = (union DL_primitives *)pMsg->b_rptr;
                    t_uscalar_t Prim = pPrim->dl_primitive;

                    Log((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: M_PCPROTO %d\n", Prim));
                    switch (Prim)
                    {
                        case DL_NOTIFY_IND:
                        {
                            if (MBLKL(pMsg) < DL_NOTIFY_IND_SIZE)
                            {
                                LogRel((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: Invalid notification size; expected>=%d"
                                        " got=%d\n", DL_NOTIFY_IND_SIZE, MBLKL(pMsg)));
                                break;
                            }

                            dl_notify_ind_t *pNotifyInd = (dl_notify_ind_t *)pMsg->b_rptr;
                            switch (pNotifyInd->dl_notification)
                            {
                                case DL_NOTE_PHYS_ADDR:
                                {
                                    if (pNotifyInd->dl_data != DL_CURR_PHYS_ADDR)
                                        break;

                                    size_t cOffset = pNotifyInd->dl_addr_offset;
                                    size_t cbAddr = pNotifyInd->dl_addr_length;

                                    if (!cOffset || !cbAddr)
                                    {
                                        LogRel((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: DL_NOTE_PHYS_ADDR."
                                                "Invalid offset/addr.\n"));
                                        fSendUpstream = false;
                                        break;
                                    }

                                    bcopy(pMsg->b_rptr + cOffset, &pThis->u.s.MacAddr, sizeof(pThis->u.s.MacAddr));
                                    Log((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: DL_NOTE_PHYS_ADDR. New Mac=%.*Rhxs\n",
                                             sizeof(pThis->u.s.MacAddr), &pThis->u.s.MacAddr));
                                    break;
                                }

                                case DL_NOTE_LINK_UP:
                                {
                                    if (ASMAtomicXchgBool(&pThis->fDisconnectedFromHost, false))
                                        Log((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: DL_NOTE_LINK_UP.\n"));
                                    break;
                                }

                                case DL_NOTE_LINK_DOWN:
                                {
                                    if (!ASMAtomicXchgBool(&pThis->fDisconnectedFromHost, true))
                                        Log((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: DL_NOTE_LINK_DOWN.\n"));
                                    break;
                                }
                            }
                            break;
                        }

                        case DL_BIND_ACK:
                        {
                            /*
                             * Swallow our bind request acknowledgement.
                             */
                            Log((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: DL_BIND_ACK. Bound to requested SAP!\n"));
                            break;
                        }

                        case DL_PHYS_ADDR_ACK:
                        {
                            /*
                             * Swallow our physical address request acknowledgement.
                             */
                            vboxNetFltSolarisCachePhysAddr(pThis, pMsg);
                            break;
                        }

                        case DL_OK_ACK:
                        {
                            /*
                             * Swallow our fake promiscuous request acknowledgement.
                             */
                            dl_ok_ack_t *pOkAck = (dl_ok_ack_t *)pMsg->b_rptr;
                            if (pOkAck->dl_correct_primitive == DL_PROMISCON_REQ)
                            {
                                Log((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: M_PCPROTO: DL_OK_ACK: fPromisc is ON.\n"));
                                pPromiscStream->fPromisc = true;
                            }
                            else if (pOkAck->dl_correct_primitive == DL_PROMISCOFF_REQ)
                            {
                                Log((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: M_PCPROTO: DL_OK_ACK: fPromisc is OFF.\n"));
                                pPromiscStream->fPromisc = false;
                            }
                            break;
                        }
                    }
                    break;
                }

                case M_IOCACK:
                {
                    /*
                     * Swallow our fake raw/fast path mode request acknowledgement.
                     */
                    struct iocblk *pIOC = (struct iocblk *)pMsg->b_rptr;
                    if (pIOC->ioc_id == pPromiscStream->ModeReqId)
                    {
                        pPromiscStream->fRawMode = true;
                        Log((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: Mode acknowledgement. RawMode is %s\n",
                                pPromiscStream->fRawMode ? "ON" : "OFF"));
                    }
                    break;
                }

                case M_IOCNAK:
                {
                    /*
                     * Swallow our fake raw/fast path mode request not acknowledged.
                     */
                    struct iocblk *pIOC = (struct iocblk *)pMsg->b_rptr;
                    if (pIOC->ioc_id == pPromiscStream->ModeReqId)
                    {
                        pPromiscStream->fRawMode = false;
                        LogRel((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: WARNING! Mode not acknowledged. RawMode is %s\n",
                                pPromiscStream->fRawMode ? "ON" : "OFF"));
                    }
                    break;
                }

                case M_FLUSH:
                {
                    /*
                     * We must support flushing queues.
                     */
                    Log((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: M_FLUSH\n"));
                    if (*pMsg->b_rptr & FLUSHR)
                        flushq(pQueue, FLUSHALL);
                    break;
                }
            }

            vboxNetFltRelease(pThis, true /* fBusy */);
        }
        else
            LogRel((DEVICE_NAME ":VBoxNetFltSolarisModReadPut: Could not find VirtualBox instance!!\n"));
    }

    if (fSendUpstream)
    {
        /*
         * Don't queue up things here, can cause bad things to happen when the system
         * is under heavy loads and we need to jam across high priority messages which
         * if it's not done properly will end up in an infinite loop.
         */
        putnext(pQueue, pMsg);
    }
    else
    {
        /*
         * We need to free up the message if we don't pass it through.
         */
        freemsg(pMsg);
    }

    return 0;
}


/**
 * Write side put procedure for processing messages in the write queue.
 * All streams, bound and unbound share this write procedure.
 *
 * @param   pQueue      Pointer to the write queue.
 * @param   pMsg        Pointer to the message.
 *
 * @returns corresponding solaris error code.
 */
static int VBoxNetFltSolarisModWritePut(queue_t *pQueue, mblk_t *pMsg)
{
    LogFunc((DEVICE_NAME ":VBoxNetFltSolarisModWritePut pQueue=%p pMsg=%p\n", pQueue, pMsg));

    putnext(pQueue, pMsg);
    return 0;
}


/**
 * Put the stream in raw mode.
 *
 * @returns VBox status code.
 * @param   pPromiscStream  Pointer to the read queue.
 */
static int vboxNetFltSolarisSetRawMode(vboxnetflt_promisc_stream_t *pPromiscStream)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisSetRawMode pPromiscStream=%p\n", pPromiscStream));

    mblk_t *pRawMsg = NULL;
    pRawMsg = mkiocb(DLIOCRAW);
    if (RT_UNLIKELY(!pRawMsg))
        return VERR_NO_MEMORY;

    queue_t *pQueue = pPromiscStream->Stream.pReadQueue;
    if (!pQueue)
        return VERR_INVALID_POINTER;

    struct iocblk *pIOC = (struct iocblk *)pRawMsg->b_rptr;
    pPromiscStream->ModeReqId = pIOC->ioc_id;
    pIOC->ioc_count = 0;

    qreply(pQueue, pRawMsg);
    return VINF_SUCCESS;
}


#if 0
/**
 * Put the stream back in fast path mode.
 *
 * @returns VBox status code.
 * @param   pQueue      Pointer to the read queue.
 */
static int vboxNetFltSolarisSetFastMode(queue_t *pQueue)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisSetFastMode pQueue=%p\n", pQueue));

    mblk_t *pFastMsg = mkiocb(DL_IOC_HDR_INFO);
    if (RT_UNLIKELY(!pFastMsg))
        return VERR_NO_MEMORY;

    vboxnetflt_stream_t *pStream = pQueue->q_ptr;
    struct iocblk *pIOC = (struct iocblk *)pFastMsg->b_rptr;
    pStream->ModeReqId = pIOC->ioc_id;

    size_t cbReq = sizeof(dl_unitdata_req_t) + sizeof(vboxnetflt_dladdr_t);
    mblk_t *pDataReqMsg = allocb(cbReq, BPRI_MED);
    if (RT_UNLIKELY(!pDataReqMsg))
        return VERR_NO_MEMORY;

    DB_TYPE(pDataReqMsg) = M_PROTO;
    dl_unitdata_req_t *pDataReq = (dl_unitdata_req_t *)pDataReqMsg->b_rptr;
    pDataReq->dl_primitive = DL_UNITDATA_REQ;
    pDataReq->dl_dest_addr_length = sizeof(vboxnetflt_dladdr_t);
    pDataReq->dl_dest_addr_offset = sizeof(dl_unitdata_req_t);
    pDataReq->dl_priority.dl_min = 0;
    pDataReq->dl_priority.dl_max = 0;

    bzero(pDataReqMsg->b_rptr + sizeof(dl_unitdata_req_t), sizeof(vboxnetflt_dladdr_t));
    pDataReqMsg->b_wptr = pDataReqMsg->b_rptr + cbReq;

    /*
     * Link the data format request message into the header ioctl message.
     */
    pFastMsg->b_cont = pDataReqMsg;
    pIOC->ioc_count = msgdsize(pDataReqMsg);

    qreply(pQueue, pFastMsg);
    return VINF_SUCCESS;
}
#endif


/**
 * Callback function for qwriter to send promiscuous request messages
 * downstream.
 *
 * @param   pQueue          Pointer to the write queue.
 * @param   fPromisc        Whether to send promiscuous ON or OFF requests.
 *
 * @returns VBox status code.
 */
static int vboxNetFltSolarisPromiscReq(queue_t *pQueue, bool fPromisc)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisPromiscReq pQueue=%p fPromisc=%d\n", pQueue, fPromisc));

    t_uscalar_t Cmd;
    size_t cbReq = 0;
    if (fPromisc)
    {
        Cmd = DL_PROMISCON_REQ;
        cbReq = DL_PROMISCON_REQ_SIZE;
    }
    else
    {
        Cmd = DL_PROMISCOFF_REQ;
        cbReq = DL_PROMISCOFF_REQ_SIZE;
    }

    mblk_t *pPromiscPhysMsg = mexchange(NULL, NULL, cbReq, M_PROTO, Cmd);
    if (RT_UNLIKELY(!pPromiscPhysMsg))
        return VERR_NO_MEMORY;

    mblk_t *pPromiscSapMsg = mexchange(NULL, NULL, cbReq, M_PROTO, Cmd);
    if (RT_UNLIKELY(!pPromiscSapMsg))
    {
        freemsg(pPromiscPhysMsg);
        return VERR_NO_MEMORY;
    }

    if (fPromisc)
    {
        ((dl_promiscon_req_t *)pPromiscPhysMsg->b_rptr)->dl_level = DL_PROMISC_PHYS;
        ((dl_promiscon_req_t *)pPromiscSapMsg->b_rptr)->dl_level = DL_PROMISC_SAP;
    }
    else
    {
        ((dl_promiscoff_req_t *)pPromiscPhysMsg->b_rptr)->dl_level = DL_PROMISC_PHYS;
        ((dl_promiscoff_req_t *)pPromiscSapMsg->b_rptr)->dl_level = DL_PROMISC_SAP;
    }

    putnext(pQueue, pPromiscPhysMsg);
    putnext(pQueue, pPromiscSapMsg);

    return VINF_SUCCESS;
}


/**
 * Callback wrapper for qwriter() to safely send promiscuous requests. This is
 * called at the outer perimeter with exclusive lock held.
 *
 * @param pQueue            Pointer to the write queue.
 * @param pMsg              A one byte message indicates a Promisc ON, otherwise
 *                          a promiscuous OFF request. See
 *                          vboxNetFltSolarisPromiscReqWrap().
 */
static void vboxNetFltSolarisPromiscReqWrapExcl(queue_t *pQueue, mblk_t *pMsg)
{
    /*
     * Paranoia.
     */
    AssertReturnVoid(pQueue);
    if (RT_UNLIKELY(!pMsg))
        LogRel((DEVICE_NAME ":VBoxNetFltSolarisPromiscReqWrapExcl pQueue=%p missing message!\n", pQueue));

    bool fPromisc = (MBLKL(pMsg) == 1);
    freemsg(pMsg);
    pMsg = NULL;
    int rc = vboxNetFltSolarisPromiscReq(pQueue, fPromisc);
    if (RT_FAILURE(rc))
        LogRel((DEVICE_NAME ":VBoxNetFltSolarisPromiscReqWrapExcl vboxNetFltSolarisPromiscReq failed. rc=%d\n", rc));
}


/**
 * Callback wrapper for qtimeout() to safely send promiscuous requests. This is
 * called at the inner perimeter with shared lock.
 *
 * @param pvData            Pointer to vboxnetflt_promisc_params_t. See
 *                          vboxNetFltPortOsSetActive().
 */
static void vboxNetFltSolarisPromiscReqWrap(void *pvData)
{
    vboxnetflt_promisc_params_t *pParams = pvData;
    if (RT_LIKELY(pParams))
    {
        PVBOXNETFLTINS pThis = pParams->pThis;
        vboxnetflt_promisc_stream_t *pPromiscStream = ASMAtomicUoReadPtrT(&pThis->u.s.pPromiscStream,
                                                                          vboxnetflt_promisc_stream_t *);
        if (   pPromiscStream
            && pPromiscStream->Stream.pReadQueue)
        {
            /*
             * Use size of message to indicate to qwriter callback whether it must send
             * promiscuous On or Off messages. This is ugly but easier and more efficient than
             * scheduling two separate qwriter callbacks with prepared messages to putnext.
             */
            size_t cbMsg = pParams->fPromiscOn ? 1 : 2;
            mblk_t *pMsg = allocb(cbMsg, BPRI_HI);
            if (RT_UNLIKELY(!pMsg))
            {
                LogRel((DEVICE_NAME ":Failed to alloc message of %u bytes\n", cbMsg));
                return;
            }

            /*
             * Move the data pointer so we can use MBLKL, as MBLKSIZE gets the db_lim which is
             * always aligned.
             */
            pMsg->b_wptr += cbMsg;

            /*
             * Upgrade inner perimeter lock to exclusive outer perimeter lock and
             * then call putnext while we are at the outer perimeter.
             */
            qwriter(WR(pPromiscStream->Stream.pReadQueue), pMsg, vboxNetFltSolarisPromiscReqWrapExcl, PERIM_OUTER);
            ASMAtomicWritePtr(&pPromiscStream->TimeoutId, NULL);
        }
        RTMemFree(pParams);
    }
}


/**
 * Send a fake physical address request downstream.
 *
 * @returns VBox status code.
 * @param   pQueue      Pointer to the read queue.
 */
static int vboxNetFltSolarisPhysAddrReq(queue_t *pQueue)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisPhysAddrReq pQueue=%p\n", pQueue));

    t_uscalar_t Cmd = DL_PHYS_ADDR_REQ;
    size_t cbReq = DL_PHYS_ADDR_REQ_SIZE;
    mblk_t *pPhysAddrMsg = mexchange(NULL, NULL, cbReq, M_PROTO, Cmd);
    if (RT_UNLIKELY(!pPhysAddrMsg))
        return VERR_NO_MEMORY;

    dl_phys_addr_req_t *pPhysAddrReq = (dl_phys_addr_req_t *)pPhysAddrMsg->b_rptr;
    pPhysAddrReq->dl_addr_type = DL_CURR_PHYS_ADDR;

    qreply(pQueue, pPhysAddrMsg);
    return VINF_SUCCESS;
}


/**
 * Cache the MAC address into the VirtualBox instance given a physical
 * address acknowledgement message.
 *
 * @param   pThis       The instance.
 * @param   pMsg        Pointer to the physical address acknowledgement message.
 */
static void vboxNetFltSolarisCachePhysAddr(PVBOXNETFLTINS pThis, mblk_t *pMsg)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisCachePhysAddr pThis=%p pMsg=%p\n", pThis, pMsg));

    AssertCompile(sizeof(RTMAC) == ETHERADDRL);
    dl_phys_addr_ack_t *pPhysAddrAck = (dl_phys_addr_ack_t *)pMsg->b_rptr;
    if (pPhysAddrAck->dl_addr_length == sizeof(pThis->u.s.MacAddr))
    {
        bcopy(pMsg->b_rptr + pPhysAddrAck->dl_addr_offset, &pThis->u.s.MacAddr, sizeof(pThis->u.s.MacAddr));

        Log((DEVICE_NAME ":vboxNetFltSolarisCachePhysAddr: DL_PHYS_ADDR_ACK: Mac=%.*Rhxs\n",
                 sizeof(pThis->u.s.MacAddr), &pThis->u.s.MacAddr));

        if (vboxNetFltTryRetainBusyNotDisconnected(pThis))
        {
            Assert(pThis->pSwitchPort);
            if (pThis->pSwitchPort)
                pThis->pSwitchPort->pfnReportMacAddress(pThis->pSwitchPort, &pThis->u.s.MacAddr);
            vboxNetFltRelease(pThis, true /*fBusy*/);
        }
    }
    else
    {
        LogRel((DEVICE_NAME ":vboxNetFltSolarisCachePhysAddr: Invalid address size. expected=%d got=%d\n", ETHERADDRL,
                pPhysAddrAck->dl_addr_length));
    }
}


/**
 * Prepare DLPI bind request to a SAP.
 *
 * @returns VBox status code.
 * @param   pQueue      Pointer to the read queue.
 * @param   SAP         The SAP to bind the stream to.
 */
static int vboxNetFltSolarisBindReq(queue_t *pQueue, int SAP)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisBindReq SAP=%d\n", SAP));

    mblk_t *pBindMsg = mexchange(NULL, NULL, DL_BIND_REQ_SIZE, M_PROTO, DL_BIND_REQ);
    if (RT_UNLIKELY(!pBindMsg))
        return VERR_NO_MEMORY;

    dl_bind_req_t *pBindReq = (dl_bind_req_t *)pBindMsg->b_rptr;
    pBindReq->dl_sap = SAP;
    pBindReq->dl_max_conind = 0;
    pBindReq->dl_conn_mgmt = 0;
    pBindReq->dl_xidtest_flg = 0;
    pBindReq->dl_service_mode = DL_CLDLS;

    qreply(pQueue, pBindMsg);
    return VINF_SUCCESS;
}


/**
 * Prepare DLPI notifications request.
 *
 * @returns VBox status code.
 * @param   pQueue          Pointer to the read queue.
 */
static int vboxNetFltSolarisNotifyReq(queue_t *pQueue)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisNotifyReq\n"));

    mblk_t *pNotifyMsg = mexchange(NULL, NULL, DL_NOTIFY_REQ_SIZE, M_PROTO, DL_NOTIFY_REQ);
    if (RT_UNLIKELY(!pNotifyMsg))
        return VERR_NO_MEMORY;

    dl_notify_req_t *pNotifyReq = (dl_notify_req_t *)pNotifyMsg->b_rptr;
    pNotifyReq->dl_notifications = DL_NOTE_LINK_UP | DL_NOTE_LINK_DOWN | DL_NOTE_PHYS_ADDR;

    qreply(pQueue, pNotifyMsg);
    return VINF_SUCCESS;
}


/**
 * Opens the required device and returns the vnode_t associated with it.
 * We require this for the funny attach/detach routine.
 *
 * @returns VBox status code.
 * @param   pszDev          The device path.
 * @param   ppVNode         Where to store the vnode_t pointer associated with the opened device.
 * @param   ppVNodeHeld     Where to store the vnode_t required during closing of the device.
 * @param   ppUser          Open handle required while closing the device.
 */
static int vboxNetFltSolarisOpenDev(char *pszDev, vnode_t **ppVNode, vnode_t **ppVNodeHeld, TIUSER **ppUser)
{
    int rc;
    vnode_t *pVNodeHeld = NULL;
    rc = lookupname(pszDev, UIO_SYSSPACE, FOLLOW, NULLVPP, &pVNodeHeld);
    if (   !rc
        && pVNodeHeld)
    {
        TIUSER *pUser;
        rc = t_kopen((file_t *)NULL, pVNodeHeld->v_rdev, FREAD | FWRITE, &pUser, kcred);
        if (!rc)
        {
            if (   pUser
                && pUser->fp
                && VNODE_FOR_FILE_T(pUser->fp))
            {
                *ppVNode = VNODE_FOR_FILE_T(pUser->fp);
                *ppVNodeHeld = pVNodeHeld;
                *ppUser = pUser;
                return VINF_SUCCESS;
            }
            else
            {
                LogRel((DEVICE_NAME ":vboxNetFltSolarisOpenDev failed. pUser=%p fp=%p f_vnode=%p\n", pUser,
                        pUser ? pUser->fp : NULL, pUser && pUser->fp ? VNODE_FOR_FILE_T(pUser->fp) : NULL));
            }

            if (pUser)
                t_kclose(pUser, 0);
        }
        else
            LogRel((DEVICE_NAME ":vboxNetFltSolarisOpenDev t_kopen failed. rc=%d\n", rc));

        VN_RELE(pVNodeHeld);
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltSolarisOpenDev lookupname failed. rc=%d pVNodeHeld=%p\n", rc, pVNodeHeld));

    return VERR_PATH_NOT_FOUND;
}


/**
 * Close the device opened using vboxNetFltSolarisOpenDev.
 *
 * @param   pVNodeHeld      Pointer to the held vnode of the device.
 * @param   pUser           Pointer to the file handle.
 */
static void vboxNetFltSolarisCloseDev(vnode_t *pVNodeHeld, TIUSER *pUser)
{
    t_kclose(pUser, 0);
    VN_RELE(pVNodeHeld);
}


/**
 * Set the DLPI style-2 PPA via an attach request, Synchronous.
 * Waits for request acknowledgement and verifies the result.
 *
 * @returns VBox status code.
 * @param   hDevice        Layered device handle.
 * @param   PPA            Physical Point of Attachment (PPA) number.
 */
static int vboxNetFltSolarisAttachReq(ldi_handle_t hDevice, int PPA)
{
    int rc;
    mblk_t *pAttachMsg = mexchange(NULL, NULL, DL_ATTACH_REQ_SIZE, M_PROTO, DL_ATTACH_REQ);
    if (RT_UNLIKELY(!pAttachMsg))
        return VERR_NO_MEMORY;

    dl_attach_req_t *pAttachReq = (dl_attach_req_t *)pAttachMsg->b_rptr;
    pAttachReq->dl_ppa = PPA;

    rc = ldi_putmsg(hDevice, pAttachMsg);
    if (!rc)
    {
        rc = ldi_getmsg(hDevice, &pAttachMsg, NULL);
        if (!rc)
        {
            /*
             * Verify if the attach succeeded.
             */
            size_t cbMsg = MBLKL(pAttachMsg);
            if (cbMsg >= sizeof(t_uscalar_t))
            {
                union DL_primitives *pPrim = (union DL_primitives *)pAttachMsg->b_rptr;
                t_uscalar_t AckPrim = pPrim->dl_primitive;

                if (   AckPrim == DL_OK_ACK                     /* Success! */
                    && cbMsg == DL_OK_ACK_SIZE)
                {
                    rc = VINF_SUCCESS;
                }
                else if (  AckPrim == DL_ERROR_ACK              /* Error Ack. */
                        && cbMsg == DL_ERROR_ACK_SIZE)
                {
                    LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachReq ldi_getmsg succeeded, but unsupported op.\n"));
                    rc = VERR_NOT_SUPPORTED;
                }
                else                                            /* Garbled reply */
                {
                    LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachReq ldi_getmsg succeeded, but invalid op."
                            " expected %d recvd %d\n", DL_OK_ACK, AckPrim));
                    rc = VERR_INVALID_FUNCTION;
                }
            }
            else
            {
                LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachReq ldi_getmsg succeeded, but invalid size %d expected %d\n", cbMsg,
                            DL_OK_ACK_SIZE));
                rc = VERR_INVALID_FUNCTION;
            }
        }
        else
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachReq ldi_getmsg failed. rc=%d\n", rc));
            rc = VERR_INVALID_FUNCTION;
        }
    }
    else
    {
        LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachReq ldi_putmsg failed. rc=%d\n", rc));
        rc = VERR_UNRESOLVED_ERROR;
    }

    freemsg(pAttachMsg);
    return rc;
}


/**
 * Get the logical interface flags from the stream.
 *
 * @returns VBox status code.
 * @param   hDevice        Layered device handle.
 * @param   pInterface     Pointer to the interface.
 */
static int vboxNetFltSolarisGetIfFlags(ldi_handle_t hDevice, struct lifreq *pInterface)
{
    struct strioctl IOCReq;
    int rc;
    int ret;
    IOCReq.ic_cmd = SIOCGLIFFLAGS;
    IOCReq.ic_timout = 40;
    IOCReq.ic_len = sizeof(struct lifreq);
    IOCReq.ic_dp = (caddr_t)pInterface;
    rc = ldi_ioctl(hDevice, I_STR, (intptr_t)&IOCReq, FKIOCTL, kcred, &ret);
    if (!rc)
        return VINF_SUCCESS;

    return RTErrConvertFromErrno(rc);
}


/**
 * Sets the multiplexor ID from the interface.
 *
 * @returns VBox status code.
 * @param   pVNode      Pointer to the device vnode.
 * @param   pInterface  Pointer to the interface.
 */
static int vboxNetFltSolarisSetMuxId(vnode_t *pVNode, struct lifreq *pInterface)
{
    struct strioctl IOCReq;
    int rc;
    int ret;
    IOCReq.ic_cmd = SIOCSLIFMUXID;
    IOCReq.ic_timout = 40;
    IOCReq.ic_len = sizeof(struct lifreq);
    IOCReq.ic_dp = (caddr_t)pInterface;

    rc = strioctl(pVNode, I_STR, (intptr_t)&IOCReq, 0, K_TO_K, kcred, &ret);
    if (!rc)
        return VINF_SUCCESS;

    return RTErrConvertFromErrno(rc);
}


/**
 * Get the multiplexor file descriptor of the lower stream.
 *
 * @returns VBox status code.
 * @param   pVNode  Pointer to the device vnode.
 * @param   MuxId   The multiplexor ID.
 * @param   pFd     Where to store the lower stream file descriptor.
 */
static int vboxNetFltSolarisMuxIdToFd(vnode_t *pVNode, int MuxId, int *pFd)
{
    int ret;

    *pFd = -1; /* silence compiler warnings from -Wmaybe-uninitialized */
    int rc = strioctl(pVNode, _I_MUXID2FD, (intptr_t)MuxId, 0, K_TO_K, kcred, &ret);
    if (!rc)
    {
        *pFd = ret;
        return VINF_SUCCESS;
    }

    return RTErrConvertFromErrno(rc);
}


/**
 * Relinks the lower and the upper IPv4 stream.
 *
 * @returns VBox status code.
 * @param   pVNode      Pointer to the device vnode.
 * @param   pInterface  Pointer to the interface.
 * @param   IpMuxFd     The IP multiplexor ID.
 * @param   ArpMuxFd    The ARP multiplexor ID.
 */
static int vboxNetFltSolarisRelinkIp4(vnode_t *pVNode, struct lifreq *pInterface, int IpMuxFd, int ArpMuxFd)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisRelinkIp4: pVNode=%p pInterface=%p IpMuxFd=%d ArpMuxFd=%d\n", pVNode,
            pInterface, IpMuxFd, ArpMuxFd));

    int NewIpMuxId;
    int NewArpMuxId;
    int rc = strioctl(pVNode, I_PLINK, (intptr_t)IpMuxFd, 0, K_TO_K, kcred, &NewIpMuxId);
    int rc2 = strioctl(pVNode, I_PLINK, (intptr_t)ArpMuxFd, 0, K_TO_K, kcred, &NewArpMuxId);
    if (   !rc
        && !rc2)
    {
        pInterface->lifr_ip_muxid = NewIpMuxId;
        pInterface->lifr_arp_muxid = NewArpMuxId;
        rc = vboxNetFltSolarisSetMuxId(pVNode, pInterface);
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;

        LogRel((DEVICE_NAME ":vboxNetFltSolarisRelinkIp4: failed to set new Mux Id.\n"));
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltSolarisRelinkIp4: failed to link.\n"));

    return VERR_GENERAL_FAILURE;
}


/**
 * Relinks the lower and the upper IPv6 stream.
 *
 * @returns VBox status code.
 * @param   pVNode      Pointer to the device vnode.
 * @param   pInterface  Pointer to the interface.
 * @param   Ip6MuxFd    The IPv6 multiplexor ID.
 */
static int vboxNetFltSolarisRelinkIp6(vnode_t *pVNode, struct lifreq *pInterface, int Ip6MuxFd)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisRelinkIp6: pVNode=%p pInterface=%p Ip6MuxFd=%d\n", pVNode, pInterface, Ip6MuxFd));

    int NewIp6MuxId;
    int rc = strioctl(pVNode, I_PLINK, (intptr_t)Ip6MuxFd, 0, K_TO_K, kcred, &NewIp6MuxId);
    if (!rc)
    {
        pInterface->lifr_ip_muxid = NewIp6MuxId;
        rc = vboxNetFltSolarisSetMuxId(pVNode, pInterface);
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;

        LogRel((DEVICE_NAME ":vboxNetFltSolarisRelinkIp6: failed to set new Mux Id.\n"));
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltSolarisRelinkIp6: failed to link.\n"));

    return VERR_GENERAL_FAILURE;
}


/**
 * Dynamically find the position on the host stack where to attach/detach ourselves.
 *
 * @returns VBox status code.
 * @param   fAttach     Is this an attach or detach.
 * @param   pVNode      Pointer to the lower stream vnode.
 * @param   pModPos     Where to store the module position.
 */
static int vboxNetFltSolarisDetermineModPos(bool fAttach, vnode_t *pVNode, int *pModPos)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisDetermineModPos: fAttach=%d pVNode=%p pModPos=%p\n", fAttach, pVNode, pModPos));

    int cMod;
    int rc = strioctl(pVNode, I_LIST, (intptr_t)NULL, 0, K_TO_K, kcred, &cMod);
    if (!rc)
    {
        if (cMod < 1)
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisDetermineModPos: too few modules on host interface. cMod=%d\n"));
            return VERR_OUT_OF_RANGE;
        }

        /*
         * While attaching we make sure we are at the bottom most of the stack, excepting
         * the host driver.
         */
        Log((DEVICE_NAME ":vboxNetFltSolarisDetermineModPos: cMod=%d\n", cMod));
        if (fAttach)
        {
            *pModPos = cMod - 1;
            return VINF_SUCCESS;
        }

        /*
         * Detaching is a bit more complicated; since user could have altered the stack positions
         * we take the safe approach by finding our position.
         */
        struct str_list StrList;
        StrList.sl_nmods = cMod;
        StrList.sl_modlist = RTMemAllocZ(cMod * sizeof(struct str_list));
        if (RT_UNLIKELY(!StrList.sl_modlist))
        {
            Log((DEVICE_NAME ":vboxNetFltSolarisDetermineModPos: failed to alloc memory for StrList.\n"));
            return VERR_NO_MEMORY;
        }

        /*
         * Get the list of all modules on the stack.
         */
        int ret;
        rc = strioctl(pVNode, I_LIST, (intptr_t)&StrList, 0, K_TO_K, kcred, &ret);
        if (!rc)
        {
            /*
             * Find our filter.
             */
            for (int i = 0; i < StrList.sl_nmods; i++)
            {
                if (!strcmp(DEVICE_NAME, StrList.sl_modlist[i].l_name))
                {
                    Log((DEVICE_NAME ":vboxNetFltSolarisDetermineModPos: Success! Found %s at %d.\n", DEVICE_NAME, i));
                    *pModPos = i;
                    RTMemFree(StrList.sl_modlist);
                    return VINF_SUCCESS;
                }
            }

            LogRel((DEVICE_NAME ":vboxNetFltSolarisDetermineModPos: failed to find %s in the host stack.\n", DEVICE_NAME));
        }
        else
            LogRel((DEVICE_NAME ":vboxNetFltSolarisDetermineModPos: failed to get module information. rc=%d\n"));

        RTMemFree(StrList.sl_modlist);
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltSolarisDetermineModPos: failed to get list of modules on host interface. rc=%d\n", rc));
    return VERR_GENERAL_FAILURE;
}


/**
 * Opens up the DLPI style 2 link that requires explicit PPA attach
 * phase.
 *
 * @returns VBox status code.
 * @param   pThis       The instance.
 * @param   pDevId      Where to store the opened LDI device id.
 */
static int vboxNetFltSolarisOpenStyle2(PVBOXNETFLTINS pThis, ldi_ident_t *pDevId)
{
    /*
     * Strip out PPA from the device name, eg: "ce3".
     */
    char *pszDev = RTStrDup(pThis->szName);
    if (!pszDev)
        return VERR_NO_MEMORY;

    char *pszEnd = strchr(pszDev, '\0');
    while (--pszEnd > pszDev)
        if (!RT_C_IS_DIGIT(*pszEnd))
            break;
    pszEnd++;

    int rc = VERR_GENERAL_FAILURE;
    long PPA = -1;
    if (   pszEnd
        && ddi_strtol(pszEnd, NULL, 10, &PPA) == 0)
    {
        *pszEnd = '\0';
        char szDev[128];
        RTStrPrintf(szDev, sizeof(szDev), "/dev/%s", pszDev);

        /*
         * Try open the device as DPLI style 2.
         */
        rc = ldi_open_by_name(szDev, FREAD | FWRITE, kcred, &pThis->u.s.hIface, *pDevId);
        if (!rc)
        {
            /*
             * Attach the PPA explictly.
             */
            rc = vboxNetFltSolarisAttachReq(pThis->u.s.hIface, (int)PPA);
            if (RT_SUCCESS(rc))
            {
                RTStrFree(pszDev);
                return rc;
            }

            ldi_close(pThis->u.s.hIface, FREAD | FWRITE, kcred);
            pThis->u.s.hIface = NULL;
            LogRel((DEVICE_NAME ":vboxNetFltSolarisOpenStyle2 dl_attach failed. rc=%d szDev=%s PPA=%d rc=%d\n", rc, szDev, PPA));
        }
        else
            LogRel((DEVICE_NAME ":vboxNetFltSolarisOpenStyle2 Failed to open. rc=%d szDev=%s PPA=%d\n", rc, szDev, PPA));
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltSolarisOpenStyle2 Failed to construct PPA. pszDev=%s pszEnd=%s.\n", pszDev, pszEnd));

    RTStrFree(pszDev);
    return VERR_INTNET_FLT_IF_FAILED;
}


/**
 * Opens up dedicated stream on top of the interface.
 * As a side-effect, the stream gets opened during
 * the I_PUSH phase.
 *
 * @param   pThis       The instance.
 */
static int vboxNetFltSolarisOpenStream(PVBOXNETFLTINS pThis)
{
    ldi_ident_t DevId;
    DevId = ldi_ident_from_anon();
    int ret;

    /*
     * Figure out if this is a VLAN interface or not based on the interface name.
     * Only works for the VLAN PPA-hack based names. See @bugref{4854} for details.
     */
    char *pszEnd = strchr(pThis->szName, '\0');
    while (--pszEnd > pThis->szName)
        if (!RT_C_IS_DIGIT(*pszEnd))
            break;
    pszEnd++;
    uint32_t PPA = RTStrToUInt32(pszEnd);
    if (PPA > 1000)
    {
        pThis->u.s.fVLAN = true;
        LogRel((DEVICE_NAME ": %s detected as VLAN interface with VID=%u.\n", pThis->szName, PPA / 1000U));
    }

    /*
     * Try style-1 open first.
     */
    char szDev[128];
    RTStrPrintf(szDev, sizeof(szDev), "/dev/net/%s", pThis->szName);
    int rc = ldi_open_by_name(szDev, FREAD | FWRITE, kcred, &pThis->u.s.hIface, DevId);
    if (   rc
        && rc == ENODEV)    /* ENODEV is returned when resolvepath fails, not ENOENT */
    {
        /*
         * Fallback to non-ClearView style-1 open.
         */
        RTStrPrintf(szDev, sizeof(szDev), "/dev/%s", pThis->szName);
        rc = ldi_open_by_name(szDev, FREAD | FWRITE, kcred, &pThis->u.s.hIface, DevId);
    }

    if (rc)
    {
        /*
         * Try DLPI style 2.
         */
        rc = vboxNetFltSolarisOpenStyle2(pThis, &DevId);
        if (RT_FAILURE(rc))
            LogRel((DEVICE_NAME ":vboxNetFltSolarisOpenStream vboxNetFltSolarisOpenStyle2 failed. rc=%d\n", rc));
        else
            rc = 0;
    }

    ldi_ident_release(DevId);
    if (rc)
    {
        LogRel((DEVICE_NAME ":vboxNetFltSolarisOpenStream Failed to open '%s' rc=%d pszName='%s'\n", szDev, rc, pThis->szName));
        return VERR_INTNET_FLT_IF_FAILED;
    }

    rc = ldi_ioctl(pThis->u.s.hIface, I_FIND, (intptr_t)DEVICE_NAME, FKIOCTL, kcred, &ret);
    if (!rc)
    {
        if (!ret)
        {
            if (RT_LIKELY(g_pVBoxNetFltSolarisCred))        /* Paranoia */
            {
                rc = RTSemFastMutexRequest(g_VBoxNetFltSolarisMtx);
                AssertRCReturn(rc, rc);

                g_VBoxNetFltSolarisInstance = pThis;
                g_VBoxNetFltSolarisStreamType = kPromiscStream;

                rc = ldi_ioctl(pThis->u.s.hIface, I_PUSH, (intptr_t)DEVICE_NAME, FKIOCTL, g_pVBoxNetFltSolarisCred, &ret);

                g_VBoxNetFltSolarisInstance = NULL;
                g_VBoxNetFltSolarisStreamType = kUndefined;

                RTSemFastMutexRelease(g_VBoxNetFltSolarisMtx);
            }
            else
            {
                LogRel((DEVICE_NAME ":vboxNetFltSolarisOpenStream huh!? Missing credentials.\n"));
                rc = VERR_INVALID_POINTER;
            }

            if (!rc)
                return VINF_SUCCESS;

            LogRel((DEVICE_NAME ":vboxNetFltSolarisOpenStream Failed to push filter onto host interface '%s'\n", pThis->szName));
        }
        else
            return VINF_SUCCESS;
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltSolarisOpenStream Failed to search for filter in interface '%s'.\n", pThis->szName));

    ldi_close(pThis->u.s.hIface, FREAD | FWRITE, kcred);
    pThis->u.s.hIface = NULL;

    return VERR_INTNET_FLT_IF_FAILED;
}


/**
 * Closes the interface, thereby closing the dedicated stream.
 *
 * @param   pThis       The instance.
 */
static void vboxNetFltSolarisCloseStream(PVBOXNETFLTINS pThis)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisCloseStream pThis=%p\n"));

    if (pThis->u.s.hIface)
    {
        ldi_close(pThis->u.s.hIface, FREAD | FWRITE, kcred);
        pThis->u.s.hIface = NULL;
    }
}


/**
 * Dynamically attach under IPv4 and ARP streams on the host stack.
 *
 * @returns VBox status code.
 * @param   pThis       The instance.
 * @param   fAttach     Is this an attach or detach.
 */
static int vboxNetFltSolarisAttachIp4(PVBOXNETFLTINS pThis, bool fAttach)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisAttachIp4 pThis=%p fAttach=%d\n", pThis, fAttach));

    /*
     * Statutory Warning: Hackish code ahead.
     */
    char *pszModName = DEVICE_NAME;

    struct lifreq Ip4Interface;
    bzero(&Ip4Interface, sizeof(Ip4Interface));
    Ip4Interface.lifr_addr.ss_family = AF_INET;
    strncpy(Ip4Interface.lifr_name, pThis->szName, sizeof(Ip4Interface.lifr_name));

    struct strmodconf StrMod;
    StrMod.mod_name = pszModName;
    StrMod.pos = -1;        /* this is filled in later. */

    struct strmodconf ArpStrMod;
    bcopy(&StrMod, &ArpStrMod, sizeof(StrMod));

    int rc;
    int rc2;
    int ret;
    ldi_ident_t DeviceIdent = ldi_ident_from_anon();
    ldi_handle_t Ip4DevHandle;
    ldi_handle_t ArpDevHandle;

    /*
     * Open the IP and ARP streams as layered devices.
     */
    rc = ldi_open_by_name(IP_DEV_NAME, FREAD | FWRITE, kcred, &Ip4DevHandle, DeviceIdent);
    if (rc)
    {
        LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: failed to open the IP stream on '%s'.\n", pThis->szName));
        ldi_ident_release(DeviceIdent);
        return VERR_INTNET_FLT_IF_FAILED;
    }

    rc = ldi_open_by_name("/dev/arp", FREAD | FWRITE, kcred, &ArpDevHandle, DeviceIdent);
    if (rc)
    {
        LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: failed to open the ARP stream on '%s'.\n", pThis->szName));
        ldi_ident_release(DeviceIdent);
        ldi_close(Ip4DevHandle, FREAD | FWRITE, kcred);
        return VERR_INTNET_FLT_IF_FAILED;
    }

    ldi_ident_release(DeviceIdent);

    /*
     * Obtain the interface flags from IPv4.
     */
    rc = vboxNetFltSolarisGetIfFlags(Ip4DevHandle, &Ip4Interface);
    if (RT_SUCCESS(rc))
    {
        /*
         * Open the UDP stream. We sort of cheat here and obtain the vnode so that we can perform
         * things that are not possible from the layered interface.
         */
        vnode_t *pUdp4VNode = NULL;
        vnode_t *pUdp4VNodeHeld = NULL;
        TIUSER *pUdp4User = NULL;
        rc = vboxNetFltSolarisOpenDev(UDP_DEV_NAME, &pUdp4VNode, &pUdp4VNodeHeld, &pUdp4User);
        if (RT_SUCCESS(rc))
        {
            /*
             * Get the multiplexor IDs.
             */
            rc = ldi_ioctl(Ip4DevHandle, SIOCGLIFMUXID, (intptr_t)&Ip4Interface, FKIOCTL, kcred, &ret);
            if (!rc)
            {
                /*
                 * Get the multiplex file descriptor to the lower streams. Generally this is lost
                 * once a module is I_PLINK, we need to reobtain it for inserting/removing ourselves from the stack.
                 */
                int Ip4MuxFd;
                int ArpMuxFd;
                rc = vboxNetFltSolarisMuxIdToFd(pUdp4VNode, Ip4Interface.lifr_ip_muxid, &Ip4MuxFd);
                rc2 = vboxNetFltSolarisMuxIdToFd(pUdp4VNode, Ip4Interface.lifr_arp_muxid, &ArpMuxFd);
                if (   RT_SUCCESS(rc)
                    && RT_SUCCESS(rc2))
                {
                    /*
                     * We need to I_PUNLINK on these multiplexor IDs before we can start
                     * operating on the lower stream as insertions are direct operations on the lower stream.
                     */
                    rc = strioctl(pUdp4VNode, I_PUNLINK, (intptr_t)Ip4Interface.lifr_ip_muxid, 0, K_TO_K, kcred, &ret);
                    rc2 = strioctl(pUdp4VNode, I_PUNLINK, (intptr_t)Ip4Interface.lifr_arp_muxid, 0, K_TO_K, kcred, &ret);
                    if (   !rc
                        && !rc2)
                    {
                        /*
                         * Obtain the vnode from the useless userland file descriptor.
                         */
                        file_t *pIpFile = getf(Ip4MuxFd);
                        file_t *pArpFile = getf(ArpMuxFd);
                        if (   pIpFile
                            && pArpFile
                            && VNODE_FOR_FILE_T(pArpFile)
                            && VNODE_FOR_FILE_T(pIpFile))
                        {
                            vnode_t *pIp4VNode = VNODE_FOR_FILE_T(pIpFile);
                            vnode_t *pArpVNode = VNODE_FOR_FILE_T(pArpFile);

                            /*
                             * Find the position on the host stack for attaching/detaching ourselves.
                             */
                            rc = vboxNetFltSolarisDetermineModPos(fAttach, pIp4VNode, &StrMod.pos);
                            rc2 = vboxNetFltSolarisDetermineModPos(fAttach, pArpVNode, &ArpStrMod.pos);
                            if (   RT_SUCCESS(rc)
                                && RT_SUCCESS(rc2))
                            {
                                /*
                                 * Inject/Eject from the host IP stack.
                                 */

                                /*
                                 * Set global data which will be grabbed by ModOpen.
                                 * There is a known (though very unlikely) race here because
                                 * of the inability to pass user data while inserting.
                                 */
                                rc = RTSemFastMutexRequest(g_VBoxNetFltSolarisMtx);
                                AssertRCReturn(rc, rc);

                                if (fAttach)
                                {
                                    g_VBoxNetFltSolarisInstance = pThis;
                                    g_VBoxNetFltSolarisStreamType = kIp4Stream;
                                }

                                rc = strioctl(pIp4VNode, fAttach ? _I_INSERT : _I_REMOVE, (intptr_t)&StrMod, 0, K_TO_K,
                                            g_pVBoxNetFltSolarisCred, &ret);

                                if (fAttach)
                                {
                                    g_VBoxNetFltSolarisInstance = NULL;
                                    g_VBoxNetFltSolarisStreamType = kUndefined;
                                }

                                RTSemFastMutexRelease(g_VBoxNetFltSolarisMtx);

                                if (!rc)
                                {
                                    /*
                                     * Inject/Eject from the host ARP stack.
                                     */
                                    rc = RTSemFastMutexRequest(g_VBoxNetFltSolarisMtx);
                                    AssertRCReturn(rc, rc);

                                    if (fAttach)
                                    {
                                        g_VBoxNetFltSolarisInstance = pThis;
                                        g_VBoxNetFltSolarisStreamType = kArpStream;
                                    }

                                    rc = strioctl(pArpVNode, fAttach ? _I_INSERT : _I_REMOVE, (intptr_t)&ArpStrMod, 0, K_TO_K,
                                                g_pVBoxNetFltSolarisCred, &ret);

                                    if (fAttach)
                                    {
                                        g_VBoxNetFltSolarisInstance = NULL;
                                        g_VBoxNetFltSolarisStreamType = kUndefined;
                                    }

                                    RTSemFastMutexRelease(g_VBoxNetFltSolarisMtx);

                                    if (!rc)
                                    {
                                        /*
                                         * Our job's not yet over; we need to relink the upper and lower streams
                                         * otherwise we've pretty much screwed up the host interface.
                                         */
                                        rc = vboxNetFltSolarisRelinkIp4(pUdp4VNode, &Ip4Interface, Ip4MuxFd, ArpMuxFd);
                                        if (RT_SUCCESS(rc))
                                        {
                                            /*
                                             * Close the devices ONLY during the return from function case; otherwise
                                             * we end up close twice which is an instant kernel panic.
                                             */
                                            vboxNetFltSolarisCloseDev(pUdp4VNodeHeld, pUdp4User);
                                            ldi_close(ArpDevHandle, FREAD | FWRITE, kcred);
                                            ldi_close(Ip4DevHandle, FREAD | FWRITE, kcred);
                                            releasef(Ip4MuxFd);
                                            releasef(ArpMuxFd);

                                            Log((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: Success! %s %s@(IPv4:%d Arp:%d) "
                                                    "%s interface %s\n", fAttach ? "Injected" : "Ejected", StrMod.mod_name,
                                                    StrMod.pos, ArpStrMod.pos, fAttach ? "to" : "from", pThis->szName));
                                            return VINF_SUCCESS;
                                        }
                                        else
                                        {
                                            LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: Relinking failed. Mode=%s rc=%d.\n",
                                                    fAttach ? "inject" : "eject", rc));
                                        }

                                        /*
                                         * Try failing gracefully during attach.
                                         */
                                        if (fAttach)
                                            strioctl(pArpVNode, _I_REMOVE, (intptr_t)&StrMod, 0, K_TO_K, kcred, &ret);
                                    }
                                    else
                                    {
                                        LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: failed to %s the ARP stack. rc=%d\n",
                                                fAttach ? "inject into" : "eject from", rc));
                                    }

                                    if (fAttach)
                                        strioctl(pIp4VNode, _I_REMOVE, (intptr_t)&StrMod, 0, K_TO_K, kcred, &ret);

                                    vboxNetFltSolarisRelinkIp4(pUdp4VNode, &Ip4Interface, Ip4MuxFd, ArpMuxFd);
                                }
                                else
                                {
                                    LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: failed to %s the IP stack. rc=%d\n",
                                            fAttach ? "inject into" : "eject from", rc));
                                }
                            }
                            else
                            {
                                LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: failed to find position. rc=%d rc2=%d\n", rc,
                                        rc2));
                            }

                            releasef(Ip4MuxFd);
                            releasef(ArpMuxFd);
                        }
                        else
                            LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: failed to get vnode from MuxFd.\n"));
                    }
                    else
                    {
                        LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: failed to unlink upper stream rc=%d rc2=%d.\n", rc,
                                rc2));
                    }
                }
                else
                    LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: failed to get MuxFd from MuxId. rc=%d rc2=%d\n", rc, rc2));
            }
            else
                LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: failed to get Mux Ids. rc=%d\n", rc));
            vboxNetFltSolarisCloseDev(pUdp4VNodeHeld, pUdp4User);
        }
        else
            LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: failed to open UDP. rc=%d\n", rc));

        rc = VERR_INTNET_FLT_IF_FAILED;
    }
    else
    {
        /*
         * This would happen for interfaces that are not plumbed.
         */
        LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp4: Warning: seems '%s' is unplumbed.\n", pThis->szName));
        rc = VINF_SUCCESS;
    }

    ldi_close(ArpDevHandle, FREAD | FWRITE, kcred);
    ldi_close(Ip4DevHandle, FREAD | FWRITE, kcred);

    return rc;
}


/**
 * Dynamically attach under IPv6 on the host stack.
 *
 * @returns VBox status code.
 * @param   pThis       The instance.
 * @param   fAttach     Is this an attach or detach.
 */
static int vboxNetFltSolarisAttachIp6(PVBOXNETFLTINS pThis, bool fAttach)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisAttachIp6 pThis=%p fAttach=%d\n", pThis, fAttach));

    /*
     * Statutory Warning: Hackish code ahead.
     */
    char *pszModName = DEVICE_NAME;

    struct lifreq Ip6Interface;
    bzero(&Ip6Interface, sizeof(Ip6Interface));
    Ip6Interface.lifr_addr.ss_family = AF_INET6;
    strncpy(Ip6Interface.lifr_name, pThis->szName, sizeof(Ip6Interface.lifr_name));

    struct strmodconf StrMod;
    StrMod.mod_name = pszModName;
    StrMod.pos = -1;        /* this is filled in later. */

    int rc;
    int ret;
    ldi_ident_t DeviceIdent = ldi_ident_from_anon();
    ldi_handle_t Ip6DevHandle;

    /*
     * Open the IPv6 stream as a layered devices.
     */
    rc = ldi_open_by_name(IP6_DEV_NAME, FREAD | FWRITE, kcred, &Ip6DevHandle, DeviceIdent);
    ldi_ident_release(DeviceIdent);
    if (rc)
    {
        LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp6: failed to open the IPv6 stream on '%s'.\n", pThis->szName));
        return VERR_INTNET_FLT_IF_FAILED;
    }

    /*
     * Obtain the interface flags from IPv6.
     */
    rc = vboxNetFltSolarisGetIfFlags(Ip6DevHandle, &Ip6Interface);
    if (RT_SUCCESS(rc))
    {
        /*
         * Open the UDP stream. We sort of cheat here and obtain the vnode so that we can perform
         * things that are not possible from the layered interface.
         */
        vnode_t *pUdp6VNode = NULL;
        vnode_t *pUdp6VNodeHeld = NULL;
        TIUSER *pUdp6User = NULL;
        rc = vboxNetFltSolarisOpenDev(UDP6_DEV_NAME, &pUdp6VNode, &pUdp6VNodeHeld, &pUdp6User);
        if (RT_SUCCESS(rc))
        {
            /*
             * Get the multiplexor IDs.
             */
            rc = ldi_ioctl(Ip6DevHandle, SIOCGLIFMUXID, (intptr_t)&Ip6Interface, FKIOCTL, kcred, &ret);
            if (!rc)
            {
                /*
                 * Get the multiplex file descriptor to the lower streams. Generally this is lost
                 * once a module is I_PLINK, we need to reobtain it for inserting/removing ourselves from the stack.
                 */
                int Ip6MuxFd;
                rc = vboxNetFltSolarisMuxIdToFd(pUdp6VNode, Ip6Interface.lifr_ip_muxid, &Ip6MuxFd);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * We need to I_PUNLINK on these multiplexor IDs before we can start
                     * operating on the lower stream as insertions are direct operations on the lower stream.
                     */
                    rc = strioctl(pUdp6VNode, I_PUNLINK, (intptr_t)Ip6Interface.lifr_ip_muxid, 0, K_TO_K, kcred, &ret);
                    if (!rc)
                    {
                        /*
                         * Obtain the vnode from the useless userland file descriptor.
                         */
                        file_t *pIpFile = getf(Ip6MuxFd);
                        if (   pIpFile
                            && VNODE_FOR_FILE_T(pIpFile))
                        {
                            vnode_t *pIp6VNode = VNODE_FOR_FILE_T(pIpFile);

                            /*
                             * Find the position on the host stack for attaching/detaching ourselves.
                             */
                            rc = vboxNetFltSolarisDetermineModPos(fAttach, pIp6VNode, &StrMod.pos);
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * Set global data which will be grabbed by ModOpen.
                                 * There is a known (though very unlikely) race here because
                                 * of the inability to pass user data while inserting.
                                 */
                                rc = RTSemFastMutexRequest(g_VBoxNetFltSolarisMtx);
                                AssertRCReturn(rc, rc);

                                if (fAttach)
                                {
                                    g_VBoxNetFltSolarisInstance = pThis;
                                    g_VBoxNetFltSolarisStreamType = kIp6Stream;
                                }

                                /*
                                 * Inject/Eject from the host IPv6 stack.
                                 */
                                rc = strioctl(pIp6VNode, fAttach ? _I_INSERT : _I_REMOVE, (intptr_t)&StrMod, 0, K_TO_K,
                                            g_pVBoxNetFltSolarisCred, &ret);

                                if (fAttach)
                                {
                                    g_VBoxNetFltSolarisInstance = NULL;
                                    g_VBoxNetFltSolarisStreamType = kUndefined;
                                }

                                RTSemFastMutexRelease(g_VBoxNetFltSolarisMtx);

                                if (!rc)
                                {
                                    /*
                                     * Our job's not yet over; we need to relink the upper and lower streams
                                     * otherwise we've pretty much screwed up the host interface.
                                     */
                                    rc = vboxNetFltSolarisRelinkIp6(pUdp6VNode, &Ip6Interface, Ip6MuxFd);
                                    if (RT_SUCCESS(rc))
                                    {
                                        /*
                                         * Close the devices ONLY during the return from function case; otherwise
                                         * we end up close twice which is an instant kernel panic.
                                         */
                                        vboxNetFltSolarisCloseDev(pUdp6VNodeHeld, pUdp6User);
                                        ldi_close(Ip6DevHandle, FREAD | FWRITE, kcred);
                                        releasef(Ip6MuxFd);

                                        Log((DEVICE_NAME ":vboxNetFltSolarisAttachIp6: Success! %s %s@(IPv6:%d) "
                                                "%s interface %s\n", fAttach ? "Injected" : "Ejected", StrMod.mod_name,
                                                StrMod.pos, fAttach ? "to" : "from", pThis->szName));
                                        return VINF_SUCCESS;
                                    }
                                    else
                                    {
                                        LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp6: Relinking failed. Mode=%s rc=%d.\n",
                                                fAttach ? "inject" : "eject", rc));
                                    }

                                    if (fAttach)
                                        strioctl(pIp6VNode, _I_REMOVE, (intptr_t)&StrMod, 0, K_TO_K, kcred, &ret);

                                    vboxNetFltSolarisRelinkIp6(pUdp6VNode, &Ip6Interface, Ip6MuxFd);
                                }
                                else
                                {
                                    LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp6: failed to %s the IP stack. rc=%d\n",
                                            fAttach ? "inject into" : "eject from", rc));
                                }
                            }
                            else
                                LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp6: failed to find position. rc=%d\n", rc));

                            releasef(Ip6MuxFd);
                        }
                        else
                             LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp6: failed to get vnode from MuxFd.\n"));
                    }
                    else
                        LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp6: failed to unlink upper stream rc=%d.\n", rc));
                }
                else
                    LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp6: failed to get MuxFd from MuxId. rc=%d\n", rc));
            }
            else
                LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp6: failed to get Mux Ids. rc=%d\n", rc));

            vboxNetFltSolarisCloseDev(pUdp6VNodeHeld, pUdp6User);
        }
        else
            LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachIp6: failed to open UDP. rc=%d\n", rc));

        rc = VERR_INTNET_FLT_IF_FAILED;
    }
    else
    {
        Log((DEVICE_NAME ":vboxNetFltSolarisAttachIp6: failed to get IPv6 flags.\n", pThis->szName));
        rc = VERR_INTNET_FLT_IF_NOT_FOUND;
    }

    ldi_close(Ip6DevHandle, FREAD | FWRITE, kcred);

    return rc;
}


#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
/**
 * Ipv6 dynamic attachment timer callback to attach to the Ipv6 stream if needed.
 *
 * @param   pTimer          Pointer to the timer.
 * @param   pvData          Opaque pointer to the instance.
 * @param   iTick           Timer tick (unused).
 */
static void vboxNetFltSolarispIp6Timer(PRTTIMER pTimer, void *pvData, uint64_t iTick)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarispIp6Timer pTimer=%p pvData=%p\n", pTimer, pvData));

    PVBOXNETFLTINS pThis = (PVBOXNETFLTINS)pvData;
    if (   RT_LIKELY(pThis)
        && RT_LIKELY(pTimer))
    {
        vboxnetflt_stream_t *pIp6Stream = ASMAtomicUoReadPtrT(&pThis->u.s.pIp6Stream, vboxnetflt_stream_t *);
        bool fIp6Attaching = ASMAtomicUoReadBool(&pThis->u.s.fAttaching);
        if (   !pIp6Stream
            && !fIp6Attaching)
        {
            int rc = RTSemFastMutexRequest(pThis->u.s.hPollMtx);
            if (RT_SUCCESS(rc))
            {
                ASMAtomicUoWriteBool(&pThis->u.s.fAttaching, true);

                vboxNetFltSolarisAttachIp6(pThis, true /* fAttach */);

                ASMAtomicUoWriteBool(&pThis->u.s.fAttaching, false);
                RTSemFastMutexRelease(pThis->u.s.hPollMtx);
            }
            else
                LogRel((DEVICE_NAME ":vboxNetFltSolarispIp6Timer failed to obtain mutex. rc=%Rrc\n", rc));
        }
    }

    NOREF(iTick);
}


/**
 * Setups up a kernel timer based on the driver property for attaching to IPv6 stream
 * whenever the stream gets plumbed for the interface.
 *
 * @returns VBox status code.
 * @param   pThis           The instance.
 */
static int vboxNetFltSolarisSetupIp6Polling(PVBOXNETFLTINS pThis)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisSetupIp6Polling pThis=%p\n", pThis));

    int rc = VERR_GENERAL_FAILURE;
    vboxnetflt_promisc_stream_t *pPromiscStream = ASMAtomicUoReadPtrT(&pThis->u.s.pPromiscStream, vboxnetflt_promisc_stream_t *);
    if (RT_LIKELY(pPromiscStream))
    {
        if (RT_LIKELY(pPromiscStream->pIp6Timer == NULL))
        {
            /*
             * Validate IPv6 polling interval.
             */
            int Interval = g_VBoxNetFltSolarisPollInterval;
            if (Interval < 1 || Interval > 120)
            {
                LogRel((DEVICE_NAME ":vboxNetFltSolarisSetupIp6Polling: Invalid polling interval %d. Expected between"
                        " 1 and 120 secs.\n", Interval));
                return VERR_INVALID_PARAMETER;
            }

            /*
             * Setup kernel poll timer.
             */
            rc = RTTimerCreateEx(&pPromiscStream->pIp6Timer, Interval * (uint64_t)1000000000, RTTIMER_FLAGS_CPU_ANY,
                                vboxNetFltSolarispIp6Timer, (void *)pThis);
            if (RT_SUCCESS(rc))
            {
                rc = RTTimerStart(pPromiscStream->pIp6Timer, 10 * (uint64_t)1000000000 /* 10 seconds to blastoff */);
                Log((DEVICE_NAME ":vboxNetFltSolarisSetupIp6Polling: Ipv6 %d second timer begins firing in 10 seconds.\n",
                     Interval));
            }
            else
                LogRel((DEVICE_NAME ":vboxNetFltSolarisSetupIp6Polling: Failed to create timer. rc=%d\n", rc));
        }
        else
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisSetupIp6Polling: Polling already started.\n"));
            rc = VINF_SUCCESS;
        }
    }
    return rc;
}
#endif

/**
 * Wrapper for detaching ourselves from the interface.
 *
 * @returns VBox status code.
 * @param   pThis           The instance.
 * @remarks Owns the globals mutex, so re-requesting it anytime during this phase
 *          would panic the system (e.g. in vboxNetFltSolarisFindInstance).
 */
static int vboxNetFltSolarisDetachFromInterface(PVBOXNETFLTINS pThis)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisDetachFromInterface pThis=%p\n", pThis));

    ASMAtomicWriteBool(&pThis->fDisconnectedFromHost, true);
    vboxNetFltSolarisCloseStream(pThis);
    int rc = VINF_SUCCESS;
    if (pThis->u.s.pIp4Stream)
        rc = vboxNetFltSolarisAttachIp4(pThis, false /* fAttach */);
    if (pThis->u.s.pIp6Stream)
        rc = vboxNetFltSolarisAttachIp6(pThis, false /* fAttach */);

#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
    vboxnetflt_promisc_stream_t *pPromiscStream = ASMAtomicUoReadPtrT(&pThis->u.s.pPromiscStream, vboxnetflt_promisc_stream_t *);
    if (   pPromiscStream
        && pPromiscStream->pIp6Timer == NULL)
    {
        RTTimerStop(pPromiscStream->pIp6Timer);
        RTTimerDestroy(pPromiscStream->pIp6Timer);
        ASMAtomicUoWriteNullPtr(&pPromiscStream->pIp6Timer);
    }
#endif

    return rc;
}


/**
 * Wrapper for attaching ourselves to the interface.
 *
 * @returns VBox status code.
 * @param   pThis           The instance.
 */
static int vboxNetFltSolarisAttachToInterface(PVBOXNETFLTINS pThis)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisAttachToInterface pThis=%p\n", pThis));

    /*
     * Since this is asynchronous streams injection, let the attach succeed before we can start
     * processing the stream.
     */
    ASMAtomicWriteBool(&pThis->fDisconnectedFromHost, true);
    int rc = vboxNetFltSolarisOpenStream(pThis);
    if (RT_SUCCESS(rc))
    {
        rc = vboxNetFltSolarisAttachIp4(pThis, true /* fAttach */);
        if (RT_SUCCESS(rc))
        {
            /*
             * Ipv6 attaching is optional and can fail. We don't bother to bring down the whole
             * attach process just if Ipv6 interface is unavailable.
             */
            int rc2 = vboxNetFltSolarisAttachIp6(pThis, true /* fAttach */);

#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
            /*
             * If Ip6 interface is not plumbed and an Ip6 polling interval is specified, we need
             * to begin polling to attach on the Ip6 interface whenever it comes up.
             */
            if (   rc2 == VERR_INTNET_FLT_IF_NOT_FOUND
                && g_VBoxNetFltSolarisPollInterval != -1)
            {
                int rc3 = vboxNetFltSolarisSetupIp6Polling(pThis);
                if (RT_FAILURE(rc3))
                {
                    /*
                     * If we failed to setup Ip6 polling, warn in the release log and continue.
                     */
                    LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachToInterface IPv6 polling inactive. rc=%Rrc\n", rc3));
                }
            }
#endif

            /*
             * Report promiscuousness and capabilities.
             */
            if (vboxNetFltTryRetainBusyNotDisconnected(pThis))
            {
                Assert(pThis->pSwitchPort);
                /** @todo There is no easy way of obtaining the global host side promiscuous
                 * counter. Currently we just return false.  */
                pThis->pSwitchPort->pfnReportPromiscuousMode(pThis->pSwitchPort, false);
                pThis->pSwitchPort->pfnReportGsoCapabilities(pThis->pSwitchPort, 0,  INTNETTRUNKDIR_WIRE | INTNETTRUNKDIR_HOST);
                pThis->pSwitchPort->pfnReportNoPreemptDsts(pThis->pSwitchPort, 0 /* none */);
                vboxNetFltRelease(pThis, true /*fBusy*/);
            }

            /*
             * Ipv4 is successful, and maybe Ipv6, we're ready for transfers.
             */
            ASMAtomicWriteBool(&pThis->fDisconnectedFromHost, false);

            return VINF_SUCCESS;
        }

        vboxNetFltSolarisCloseStream(pThis);
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachToInterface vboxNetFltSolarisOpenStream failed rc=%Rrc\n", rc));

    return rc;
}


/**
 * Create a solaris message block from the SG list.
 *
 * @returns Solaris message block.
 * @param   pThis           The instance.
 * @param   pSG             Pointer to the scatter-gather list.
 * @param   fDst            The destination mask, INTNETTRUNKDIR_XXX. Ignored.
 */
static mblk_t *vboxNetFltSolarisMBlkFromSG(PVBOXNETFLTINS pThis, PINTNETSG pSG, uint32_t fDst)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisMBlkFromSG pThis=%p pSG=%p\n", pThis, pSG));

    mblk_t *pMsg = allocb(pSG->cbTotal, BPRI_MED);
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
    DB_TYPE(pMsg) = M_DATA;
    return pMsg;
}


/**
 * Calculate the number of segments required for this message block.
 *
 * @returns Number of segments.
 * @param   pThis   The instance
 * @param   pMsg    Pointer to the data message.
 */
static unsigned vboxNetFltSolarisMBlkCalcSGSegs(PVBOXNETFLTINS pThis, mblk_t *pMsg)
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
 * @returns VBox status code.
 * @param   pThis   The instance.
 * @param   pMsg    Pointer to the data message.
                    The caller must ensure it's not a control message block.
 * @param   pSG     Pointer to the SG.
 * @param   cSegs   Number of segments in the SG.
 *                  This should match the number in the message block exactly!
 * @param   fSrc    The source of the message.
 */
static int vboxNetFltSolarisMBlkToSG(PVBOXNETFLTINS pThis, mblk_t *pMsg, PINTNETSG pSG, unsigned cSegs, uint32_t fSrc)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisMBlkToSG pThis=%p pMsg=%p pSG=%p cSegs=%d\n", pThis, pMsg, pSG, cSegs));

    /*
     * Convert the message block to segments. Work INTNETSG::cbTotal.
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


/**
 * Converts raw mode M_DATA messages to M_PROTO DL_UNITDATA_IND format.
 *
 * @returns VBox status code.
 * @param   pMsg        Pointer to the raw message.
 * @param   ppDlpiMsg   Where to store the M_PROTO message.
 *
 * @remarks The original raw message would be no longer valid and will be
 *          linked as part of the new DLPI message. Callers must take care
 *          not to use the raw message if this routine is successful.
 */
static int vboxNetFltSolarisRawToUnitData(mblk_t *pMsg, mblk_t **ppDlpiMsg)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisRawToUnitData pMsg=%p\n", pMsg));

    if (DB_TYPE(pMsg) != M_DATA)
        return VERR_NO_MEMORY;

    size_t cbMsg = sizeof(dl_unitdata_ind_t) + 2 * sizeof(vboxnetflt_dladdr_t);
    mblk_t *pDlpiMsg = allocb(cbMsg, BPRI_MED);
    if (RT_UNLIKELY(!pDlpiMsg))
        return VERR_NO_MEMORY;

    DB_TYPE(pDlpiMsg) = M_PROTO;
    dl_unitdata_ind_t *pDlpiData = (dl_unitdata_ind_t *)pDlpiMsg->b_rptr;
    pDlpiData->dl_primitive = DL_UNITDATA_IND;
    pDlpiData->dl_dest_addr_length = VBOXNETFLT_DLADDRL;
    pDlpiData->dl_dest_addr_offset = sizeof(dl_unitdata_ind_t);
    pDlpiData->dl_src_addr_length = VBOXNETFLT_DLADDRL;
    pDlpiData->dl_src_addr_offset = VBOXNETFLT_DLADDRL + sizeof(dl_unitdata_ind_t);

    PRTNETETHERHDR pEthHdr = (PRTNETETHERHDR)pMsg->b_rptr;

    vboxnetflt_dladdr_t *pDlAddr = (vboxnetflt_dladdr_t *)(pDlpiMsg->b_rptr + pDlpiData->dl_dest_addr_offset);
    pDlAddr->SAP = RT_BE2H_U16(pEthHdr->EtherType);
    bcopy(&pEthHdr->DstMac, &pDlAddr->Mac, sizeof(RTMAC));

    pDlAddr = (vboxnetflt_dladdr_t *)(pDlpiMsg->b_rptr + pDlpiData->dl_src_addr_offset);
    pDlAddr->SAP = RT_BE2H_U16(pEthHdr->EtherType);
    bcopy(&pEthHdr->SrcMac, &pDlAddr->Mac, sizeof(RTMAC));

    pDlpiMsg->b_wptr = pDlpiMsg->b_rptr + cbMsg;

    /* Make the message point to the protocol header */
    pMsg->b_rptr += sizeof(RTNETETHERHDR);

    pDlpiMsg->b_cont = pMsg;
    *ppDlpiMsg = pDlpiMsg;
    return VINF_SUCCESS;
}

#if 0
/**
 * Converts DLPI M_PROTO messages to the raw mode M_DATA format.
 *
 * @returns VBox status code.
 * @param   pMsg        Pointer to the M_PROTO message.
 * @param   ppRawMsg    Where to store the converted message.
 *
 * @remarks If successful, the original pMsg is no longer valid, it will be deleted.
 *          Callers must take care not to continue to use pMsg after a successful
 *          call to this conversion routine.
 */
static int vboxNetFltSolarisUnitDataToRaw(PVBOXNETFLTINS pThis, mblk_t *pMsg, mblk_t **ppRawMsg)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisUnitDataToRaw pMsg=%p\n", pMsg));

    if (   !pMsg->b_cont
        || DB_TYPE(pMsg) != M_PROTO)
    {
        LogRel((DEVICE_NAME ":vboxNetFltSolarisUnitDataToRaw invalid input message.\n"));
        return VERR_NET_PROTOCOL_ERROR;
    }

    /*
     * Upstream consumers send/receive packets in the fast path mode.
     * We of course need to convert them into raw ethernet frames.
     */
    RTNETETHERHDR EthHdr;
    union DL_primitives *pPrim = (union DL_primitives *)pMsg->b_rptr;
    switch (pPrim->dl_primitive)
    {
        case DL_UNITDATA_IND:
        {
            /*
             * Receive side.
             */
            dl_unitdata_ind_t *pDlpiMsg = (dl_unitdata_ind_t *)pMsg->b_rptr;
            bcopy(pMsg->b_rptr + pDlpiMsg->dl_dest_addr_offset, &EthHdr.DstMac, sizeof(EthHdr.DstMac));
            bcopy(pMsg->b_rptr + pDlpiMsg->dl_src_addr_offset, &EthHdr.SrcMac, sizeof(EthHdr.SrcMac));

            vboxnetflt_dladdr_t *pDLSapAddr = (vboxnetflt_dladdr_t *)(pMsg->b_rptr + pDlpiMsg->dl_dest_addr_offset);
            EthHdr.EtherType = RT_H2BE_U16(pDLSapAddr->SAP);

            break;
        }

        case DL_UNITDATA_REQ:
        {
            /*
             * Send side.
             */
            dl_unitdata_req_t *pDlpiMsg = (dl_unitdata_req_t *)pMsg->b_rptr;

            bcopy(pMsg->b_rptr + pDlpiMsg->dl_dest_addr_offset, &EthHdr.DstMac, sizeof(EthHdr.DstMac));
            bcopy(&pThis->u.s.MacAddr, &EthHdr.SrcMac, sizeof(EthHdr.SrcMac));

            vboxnetflt_dladdr_t *pDLSapAddr = (vboxnetflt_dladdr_t *)(pMsg->b_rptr + pDlpiMsg->dl_dest_addr_offset);
            EthHdr.EtherType = RT_H2BE_U16(pDLSapAddr->SAP);

            break;
        }

        default:
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisUnitDataToRaw Unknown M_PROTO. This shouldn't be happening!!"));
            return VERR_NET_PROTOCOL_ERROR;
        }
    }

    /*
     * Let us just link it as a mblk_t chain rather than re-copy the entire message.
     * The vboxNetFltSolarisMBlkToSG function will handle chained mblk_t's.
     */
    size_t cbLen = sizeof(EthHdr);
    mblk_t *pEtherMsg = allocb(cbLen, BPRI_MED);
    if (RT_UNLIKELY(!pEtherMsg))
        return VERR_NO_MEMORY;

    DB_TYPE(pEtherMsg) = M_DATA;
    bcopy(&EthHdr, pEtherMsg->b_wptr, sizeof(EthHdr));
    pEtherMsg->b_wptr += cbLen;

    pEtherMsg->b_cont = pMsg->b_cont;

    /*
     * Change the chained blocks to type M_DATA.
     */
    for (mblk_t *pTmp = pEtherMsg->b_cont; pTmp; pTmp = pTmp->b_cont)
        DB_TYPE(pTmp) = M_DATA;

    pMsg->b_cont = NULL;
    freemsg(pMsg);

    *ppRawMsg = pEtherMsg;
    return VINF_SUCCESS;
}
#endif

/**
 * Initializes a packet identifier.
 *
 * @param   pTag        Pointer to the packed identifier.
 * @param   pMsg        Pointer to the message to be identified.
 *
 * @remarks Warning!!! This function assumes 'pMsg' is an unchained message.
 */
static inline void vboxNetFltSolarisInitPacketId(PVBOXNETFLTPACKETID pTag, mblk_t *pMsg)
{
    PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pMsg->b_rptr;
    size_t cbMsg = MBLKL(pMsg);

    pTag->cbPacket = cbMsg;
    pTag->Checksum = RTCrc32(pMsg->b_rptr, cbMsg);
    bcopy(&pEthHdr->SrcMac, &pTag->SrcMac, sizeof(RTMAC));
    bcopy(&pEthHdr->DstMac, &pTag->DstMac, sizeof(RTMAC));
}


/**
 * Queues a packet for loopback elimination.
 *
 * @returns VBox status code.
 * @param   pThis               The instance.
 * @param   pPromiscStream      Pointer to the promiscuous stream.
 * @param   pMsg                Pointer to the message.
 */
static int vboxNetFltSolarisQueueLoopback(PVBOXNETFLTINS pThis, vboxnetflt_promisc_stream_t *pPromiscStream, mblk_t *pMsg)
{
    Assert(pThis);
    Assert(pMsg);
    Assert(DB_TYPE(pMsg) == M_DATA);
    Assert(pPromiscStream);

    LogFunc((DEVICE_NAME ":vboxNetFltSolarisQueueLoopback pThis=%p pPromiscStream=%p pMsg=%p\n", pThis, pPromiscStream, pMsg));

    if (RT_UNLIKELY(pMsg->b_cont))
    {
        /*
         * We don't currently make chained messages in on Xmit
         * so this only needs to be supported when we do that.
         */
        return VERR_NOT_SUPPORTED;
    }

    size_t cbMsg = MBLKL(pMsg);
    if (RT_UNLIKELY(cbMsg < sizeof(RTNETETHERHDR)))
        return VERR_NET_MSG_SIZE;

    int rc = VINF_SUCCESS;
    mutex_enter(&pThis->u.s.hMtx);

    PVBOXNETFLTPACKETID pCur = NULL;
    if (pPromiscStream->cLoopback < VBOXNETFLT_LOOPBACK_SIZE
        || (   pPromiscStream->pHead
            && pPromiscStream->pHead->cbPacket == 0))
    {
        do
        {
            if (!pPromiscStream->pHead)
            {
                pCur = RTMemAlloc(sizeof(VBOXNETFLTPACKETID));
                if (RT_UNLIKELY(!pCur))
                {
                    rc = VERR_NO_MEMORY;
                    break;
                }

                vboxNetFltSolarisInitPacketId(pCur, pMsg);

                pCur->pNext = NULL;
                pPromiscStream->pHead = pCur;
                pPromiscStream->pTail = pCur;
                pPromiscStream->cLoopback++;

                Log((DEVICE_NAME ":vboxNetFltSolarisQueueLoopback initialized head. checksum=%u.\n",
                        pPromiscStream->pHead->Checksum));
                break;
            }
            else if (   pPromiscStream->pHead
                     && pPromiscStream->pHead->cbPacket == 0)
            {
                pCur = pPromiscStream->pHead;
                vboxNetFltSolarisInitPacketId(pCur, pMsg);

                Log((DEVICE_NAME ":vboxNetFltSolarisQueueLoopback re-used head checksum=%u cLoopback=%d.\n",
                        pCur->Checksum, pPromiscStream->cLoopback));
                break;
            }
            else
            {
                pCur = RTMemAlloc(sizeof(VBOXNETFLTPACKETID));
                if (RT_UNLIKELY(!pCur))
                {
                    rc = VERR_NO_MEMORY;
                    break;
                }

                vboxNetFltSolarisInitPacketId(pCur, pMsg);

                pCur->pNext = pPromiscStream->pHead;
                pPromiscStream->pHead = pCur;
                pPromiscStream->cLoopback++;

                Log((DEVICE_NAME ":vboxNetFltSolarisQueueLoopback added head checksum=%u cLoopback=%d.\n", pCur->Checksum,
                        pPromiscStream->cLoopback));
                break;
            }
        } while (0);
    }
    else
    {
        /*
         * Maximum loopback queue size reached. Re-use tail as head.
         */
        Assert(pPromiscStream->pHead);
        Assert(pPromiscStream->pTail);

        /*
         * Find tail's previous item.
         */
        PVBOXNETFLTPACKETID pPrev = NULL;
        pCur = pPromiscStream->pHead;

        /** @todo consider if this is worth switching to a double linked list... */
        while (pCur != pPromiscStream->pTail)
        {
            pPrev = pCur;
            pCur = pCur->pNext;
        }

        pPromiscStream->pTail = pPrev;
        pPromiscStream->pTail->pNext = NULL;
        pCur->pNext = pPromiscStream->pHead;
        pPromiscStream->pHead = pCur;

        vboxNetFltSolarisInitPacketId(pCur, pMsg);
        Log((DEVICE_NAME ":vboxNetFltSolarisQueueLoopback recycled tail!! checksum=%u cLoopback=%d\n", pCur->Checksum,
                pPromiscStream->cLoopback));
    }

    mutex_exit(&pThis->u.s.hMtx);

    return rc;
}


/**
 * Checks if the packet is enqueued for loopback as our own packet.
 *
 * @returns If it's our packet, returns true after dequeuing it, otherwise false.
 * @param   pThis               The instance.
 * @param   pPromiscStream      Pointer to the promiscuous stream.
 * @param   pMsg                Pointer to the message.
 */
static bool vboxNetFltSolarisIsOurMBlk(PVBOXNETFLTINS pThis, vboxnetflt_promisc_stream_t *pPromiscStream, mblk_t *pMsg)
{
    Assert(pThis);
    Assert(pPromiscStream);
    Assert(pMsg);
    Assert(DB_TYPE(pMsg) == M_DATA);

    LogFunc((DEVICE_NAME ":vboxNetFltSolarisIsOurMBlk pThis=%p pMsg=%p\n", pThis, pMsg));

    if (pMsg->b_cont)
    {
        /** Handle this when Xmit makes chained messages */
        return false;
    }

    size_t cbMsg = MBLKL(pMsg);
    if (cbMsg < sizeof(RTNETETHERHDR))
        return false;

    mutex_enter(&pThis->u.s.hMtx);

    PVBOXNETFLTPACKETID pPrev = NULL;
    PVBOXNETFLTPACKETID pCur = pPromiscStream->pHead;
    bool fIsOurPacket = false;
    while (pCur)
    {
        PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pMsg->b_rptr;
        if (   pCur->cbPacket != cbMsg
            || pCur->SrcMac.au8[0] != pEthHdr->SrcMac.au8[0]
            || pCur->SrcMac.au8[1] != pEthHdr->SrcMac.au8[1]
            || pCur->SrcMac.au8[2] != pEthHdr->SrcMac.au8[2]
            || pCur->SrcMac.au8[3] != pEthHdr->SrcMac.au8[3]
            || pCur->SrcMac.au8[4] != pEthHdr->SrcMac.au8[4]
            || pCur->SrcMac.au8[5] != pEthHdr->SrcMac.au8[5]
            || pCur->DstMac.au8[0] != pEthHdr->DstMac.au8[0]
            || pCur->DstMac.au8[1] != pEthHdr->DstMac.au8[1]
            || pCur->DstMac.au8[2] != pEthHdr->DstMac.au8[2]
            || pCur->DstMac.au8[3] != pEthHdr->DstMac.au8[3]
            || pCur->DstMac.au8[4] != pEthHdr->DstMac.au8[4]
            || pCur->DstMac.au8[5] != pEthHdr->DstMac.au8[5])
        {
            pPrev = pCur;
            pCur = pCur->pNext;
            continue;
        }

        uint16_t Checksum = RTCrc32(pMsg->b_rptr, cbMsg);
        if (pCur->Checksum != Checksum)
        {
            pPrev = pCur;
            pCur = pCur->pNext;
            continue;
        }

        /*
         * Yes, it really is our own packet, mark it as handled
         * and move it as a "free slot" to the head and return success.
         */
        pCur->cbPacket = 0;
        if (pPrev)
        {
            if (!pCur->pNext)
                pPromiscStream->pTail = pPrev;

            pPrev->pNext = pCur->pNext;
            pCur->pNext = pPromiscStream->pHead;
            pPromiscStream->pHead = pCur;
        }
        fIsOurPacket = true;

        Log((DEVICE_NAME ":vboxNetFltSolarisIsOurMBlk found packet %p Checksum=%u cLoopback=%d\n", pMsg, Checksum,
                    pPromiscStream->cLoopback));
        break;
    }

    Log((DEVICE_NAME ":vboxNetFltSolarisIsOurMBlk returns %d.\n", fIsOurPacket));
    mutex_exit(&pThis->u.s.hMtx);
    return fIsOurPacket;
}


/**
 * Helper.
 */
DECLINLINE(bool) vboxNetFltPortSolarisIsHostMac(PVBOXNETFLTINS pThis, PCRTMAC pMac)
{
    /*
     * MAC address change acknowledgements are intercepted on the read side
     * hence theoretically we are always update to date with any changes.
     */
    return pThis->u.s.MacAddr.au16[0] == pMac->au16[0]
        && pThis->u.s.MacAddr.au16[1] == pMac->au16[1]
        && pThis->u.s.MacAddr.au16[2] == pMac->au16[2];
}


/**
 * Worker for routing messages from the wire or from the host.
 *
 * @returns VBox status code.
 * @param   pThis       The instance.
 * @param   pStream     Pointer to the stream.
 * @param   pQueue      Pointer to the read queue.
 * @param   pMsg        Pointer to the message.
 */
static int vboxNetFltSolarisRecv(PVBOXNETFLTINS pThis, vboxnetflt_stream_t *pStream, queue_t *pQueue, mblk_t *pMsg)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisRecv pThis=%p pMsg=%p\n", pThis, pMsg));

    AssertCompile(sizeof(struct ether_header) == sizeof(RTNETETHERHDR));
    Assert(pStream->Type == kPromiscStream);

    vboxnetflt_promisc_stream_t *pPromiscStream = ASMAtomicUoReadPtrT(&pThis->u.s.pPromiscStream, vboxnetflt_promisc_stream_t *);
    if (RT_UNLIKELY(!pPromiscStream))
    {
        LogRel((DEVICE_NAME ":Promiscuous stream missing!! Failing to receive packet.\n"));
        return VERR_INVALID_POINTER;
    }

    /*
     * Paranoia...
     */
    if (RT_UNLIKELY(MBLKL(pMsg) < sizeof(RTNETETHERHDR)))
    {
        size_t cbMsg = msgdsize(pMsg);
        if (cbMsg < sizeof(RTNETETHERHDR))
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisRecv %s: packet too small. Dropping packet.\n", pThis->szName));
            return VINF_SUCCESS;
        }

        mblk_t *pFullMsg = msgpullup(pMsg, -1 /* all data blocks */);
        if (pFullMsg)
        {
            freemsg(pMsg);
            pMsg = pFullMsg;
        }
        else
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisRecv msgpullup failed.\n"));
            return VERR_NO_MEMORY;
        }
    }

    /*
     * Don't loopback packets we transmit to the wire.
     */
    if (vboxNetFltSolarisIsOurMBlk(pThis, pPromiscStream, pMsg))
    {
        Log((DEVICE_NAME ":Avoiding packet loopback.\n"));
        return VINF_SUCCESS;
    }

    /*
     * Figure out the source of the packet based on the source Mac address.
     */
    uint32_t fSrc = INTNETTRUNKDIR_WIRE;
    PRTNETETHERHDR pEthHdr = (PRTNETETHERHDR)pMsg->b_rptr;
    if (vboxNetFltPortSolarisIsHostMac(pThis, &pEthHdr->SrcMac))
        fSrc = INTNETTRUNKDIR_HOST;

    /*
     * Afaik; we no longer need to worry about incorrect checksums because we now use
     * a dedicated stream and don't intercept packets under IP/ARP which might be doing
     * checksum offloading.
     */
#if 0
    if (fSrc & INTNETTRUNKDIR_HOST)
    {
        mblk_t *pCorrectedMsg = vboxNetFltSolarisFixChecksums(pMsg);
        if (pCorrectedMsg)
            pMsg = pCorrectedMsg;
    }
    vboxNetFltSolarisAnalyzeMBlk(pMsg);
#endif

    /*
     * Solaris raw mode streams for priority-tagged VLAN does not strip the VLAN tag.
     * It zero's the VLAN-Id but keeps the tag intact as part of the Ethernet header.
     * We need to manually strip these tags out or the guests might get confused.
     */
    bool fCopied = false;
    bool fTagged = false;
    if (   pThis->u.s.fVLAN
        && pPromiscStream->fRawMode)
    {
        if (pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_VLAN))
        {
            if (msgdsize(pMsg) > sizeof(RTNETETHERHDR) + sizeof(VLANHEADER))
            {
                if (pMsg->b_cont)
                {
                    mblk_t *pFullMsg = msgpullup(pMsg, -1 /* all data blocks */);
                    if (pFullMsg)
                    {
                        /* Original pMsg will be freed by the caller */
                        pMsg = pFullMsg;
                        fCopied = true;
                    }
                    else
                    {
                        LogRel((DEVICE_NAME ":vboxNetFltSolarisRecv msgpullup failed.\n"));
                        return VERR_NO_MEMORY;
                    }
                }

                PVLANHEADER pVlanHdr = (PVLANHEADER)(pMsg->b_rptr + sizeof(RTNETETHERHDR) - sizeof(pEthHdr->EtherType));
                Log((DEVICE_NAME ":Recv VLAN Pcp=%u Cfi=%u Id=%u\n", VLAN_PRI(RT_BE2H_U16(pVlanHdr->Data)),
                            VLAN_CFI(RT_BE2H_U16(pVlanHdr->Data)), VLAN_ID(RT_BE2H_U16(pVlanHdr->Data))));
                if (   VLAN_PRI(RT_BE2H_U16(pVlanHdr->Data)) > 0
                    && VLAN_ID(RT_BE2H_U16(pVlanHdr->Data)) == 0)
                {
                    /*
                     * Create new Ethernet header with stripped VLAN tag.
                     */
                    size_t cbEthPrefix = sizeof(RTNETETHERHDR) - sizeof(pEthHdr->EtherType);
                    mblk_t *pStrippedMsg = allocb(cbEthPrefix, BPRI_MED);
                    if (RT_LIKELY(pStrippedMsg))
                    {
                        fTagged = true;

                        /*
                         * Copy ethernet header excluding the ethertype.
                         */
                        bcopy(pMsg->b_rptr, pStrippedMsg->b_wptr, cbEthPrefix);
                        pStrippedMsg->b_wptr += cbEthPrefix;

                        /*
                         * Link the rest of the message (ethertype + data, skipping VLAN header).
                         */
                        pMsg->b_rptr += cbEthPrefix + sizeof(VLANHEADER);
                        pStrippedMsg->b_cont = pMsg;
                        pMsg = pStrippedMsg;
                        Log((DEVICE_NAME ":Stripped VLAN tag.\n"));
                    }
                    else
                    {
                        LogRel((DEVICE_NAME ":vboxNetFltSolarisRecv insufficient memory for creating VLAN stripped packet"
                                " cbMsg=%u.\n", cbEthPrefix));
                        if (fCopied)
                            freemsg(pMsg);
                        return VERR_NO_MEMORY;
                    }
                }
            }
        }
    }

    /*
     * Route all received packets into the internal network.
     */
    unsigned cSegs = vboxNetFltSolarisMBlkCalcSGSegs(pThis, pMsg);
    PINTNETSG pSG = (PINTNETSG)alloca(RT_UOFFSETOF_DYN(INTNETSG, aSegs[cSegs]));
    int rc = vboxNetFltSolarisMBlkToSG(pThis, pMsg, pSG, cSegs, fSrc);
    if (RT_SUCCESS(rc))
        pThis->pSwitchPort->pfnRecv(pThis->pSwitchPort, NULL /* pvIf */, pSG, fSrc);
    else
        LogRel((DEVICE_NAME ":vboxNetFltSolarisMBlkToSG failed. rc=%d\n", rc));

    /*
     * If we've allocated the prefix before the VLAN tag in a new message, free that.
     */
    if (fTagged)
    {
        mblk_t *pTagMsg = pMsg->b_cont;
        pMsg->b_cont = NULL; /* b_cont could be the message from the caller or a copy we made (fCopied) */
        freemsg(pMsg);
        pMsg = pTagMsg;
    }

    /*
     * If we made an extra copy for VLAN stripping, we need to free that ourselves.
     */
    if (fCopied)
        freemsg(pMsg);

    return VINF_SUCCESS;
}

#if 0
/**
 * Finalize the message to be fed into the internal network.
 * Verifies and tries to fix checksums for TCP, UDP and IP.
 *
 * @returns Corrected message or NULL if no change was required.
 * @param   pMsg    Pointer to the message block.
 *                  This must not be DLPI linked messages, must be M_DATA.
 *
 * @remarks If this function returns a checksum adjusted message, the
 *          passed in input message has been freed and should not be
 *          referenced anymore by the caller.
 */
static mblk_t *vboxNetFltSolarisFixChecksums(mblk_t *pMsg)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisFixChecksums pMsg=%p\n"));

    Assert(DB_TYPE(pMsg) == M_DATA);

    if (MBLKL(pMsg) < sizeof(RTNETETHERHDR))
    {
        LogRel((DEVICE_NAME ":vboxNetFltSolarisFixChecksums Packet shorter than ethernet header size!\n"));
        return NULL;
    }

    PRTNETETHERHDR pEthHdr = (PRTNETETHERHDR)pMsg->b_rptr;
    if (pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_IPV4))
    {
        /*
         * Check if we have a complete packet or being fed a chain.
         */
        size_t cbIpPacket = 0;
        mblk_t *pFullMsg = NULL;
        if (pMsg->b_cont)
        {
            Log((DEVICE_NAME ":Chained mblk_t.\n"));

            /*
             * Handle chain by making a packet copy to verify if the IP checksum is correct.
             * Contributions to calculating IP checksums from a chained message block with
             * odd/non-pulled up sizes are welcome.
             */
            size_t cbFullMsg = msgdsize(pMsg);
            mblk_t *pFullMsg = allocb(cbFullMsg, BPRI_MED);
            Log((DEVICE_NAME ":msgdsize returns %d\n", cbFullMsg));
            if (RT_UNLIKELY(!pFullMsg))
            {
                LogRel((DEVICE_NAME ":vboxNetFltSolarisFixChecksums failed to alloc new message of %d bytes.\n", cbFullMsg));
                return NULL;
            }

            for (mblk_t *pTmp = pMsg; pTmp; pTmp = pTmp->b_cont)
            {
                if (DB_TYPE(pTmp) == M_DATA)
                {
                    bcopy(pTmp->b_rptr, pFullMsg->b_wptr, MBLKL(pTmp));
                    pFullMsg->b_wptr += MBLKL(pTmp);
                }
            }

            DB_TYPE(pFullMsg) = M_DATA;
            pEthHdr = (PRTNETETHERHDR)pFullMsg->b_rptr;
            cbIpPacket = MBLKL(pFullMsg) - sizeof(RTNETETHERHDR);
        }
        else
            cbIpPacket = MBLKL(pMsg) - sizeof(RTNETETHERHDR);

        /*
         * Check if the IP checksum is valid.
         */
        uint8_t *pbProtocol = (uint8_t *)(pEthHdr + 1);
        PRTNETIPV4 pIpHdr = (PRTNETIPV4)pbProtocol;
        size_t cbPayload = cbIpPacket - (pIpHdr->ip_hl << 2);
        bool fChecksumAdjusted = false;
        if (RTNetIPv4IsHdrValid(pIpHdr, cbPayload, cbPayload))
        {
            pbProtocol += (pIpHdr->ip_hl << 2);

            /*
             * Fix up TCP/UDP and IP checksums if they're incomplete/invalid.
             */
            if (pIpHdr->ip_p == RTNETIPV4_PROT_TCP)
            {
                PRTNETTCP pTcpHdr = (PRTNETTCP)pbProtocol;
                uint16_t TcpChecksum = RTNetIPv4TCPChecksum(pIpHdr, pTcpHdr, NULL);
                if (pTcpHdr->th_sum != TcpChecksum)
                {
                    pTcpHdr->th_sum = TcpChecksum;
                    fChecksumAdjusted = true;
                    Log((DEVICE_NAME ":fixed TCP checksum.\n"));
                }
            }
            else if (pIpHdr->ip_p == RTNETIPV4_PROT_UDP)
            {
                PRTNETUDP pUdpHdr = (PRTNETUDP)pbProtocol;
                uint16_t UdpChecksum = RTNetIPv4UDPChecksum(pIpHdr, pUdpHdr, pUdpHdr + 1);

                if (pUdpHdr->uh_sum != UdpChecksum)
                {
                    pUdpHdr->uh_sum = UdpChecksum;
                    fChecksumAdjusted = true;
                    Log((DEVICE_NAME ":Fixed UDP checksum."));
                }
            }
        }

        if (fChecksumAdjusted)
        {
            /*
             * If we made a copy and the checksum is corrected on the copy,
             * free the original, return the checksum fixed copy.
             */
            if (pFullMsg)
            {
                freemsg(pMsg);
                return pFullMsg;
            }

            return pMsg;
        }

        /*
         * If we made a copy and the checksum is NOT corrected, free the copy,
         * and return NULL.
         */
        if (pFullMsg)
            freemsg(pFullMsg);

        return NULL;
    }

    return NULL;
}


/**
 * Simple packet dump, used for internal debugging.
 *
 * @param   pMsg    Pointer to the message to analyze and dump.
 */
static void vboxNetFltSolarisAnalyzeMBlk(mblk_t *pMsg)
{
    LogFunc((DEVICE_NAME ":vboxNetFltSolarisAnalyzeMBlk pMsg=%p\n", pMsg));

    PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pMsg->b_rptr;
    uint8_t *pb = pMsg->b_rptr;
    if (pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_IPV4))
    {
        PRTNETIPV4 pIpHdr = (PRTNETIPV4)(pEthHdr + 1);
        size_t cbLen = MBLKL(pMsg) - sizeof(*pEthHdr);
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
    else if (pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_IPX_1)
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


/* -=-=-=-=-=- Common Hooks -=-=-=-=-=- */



void vboxNetFltPortOsSetActive(PVBOXNETFLTINS pThis, bool fActive)
{
    LogFunc((DEVICE_NAME ":vboxNetFltPortOsSetActive pThis=%p fActive=%d\n", pThis, fActive));

    /*
     * Enable/disable promiscuous mode.
     */
    vboxnetflt_promisc_params_t *pData = RTMemAllocZ(sizeof(vboxnetflt_promisc_params_t));
    if (RT_LIKELY(pData))
    {
        /*
         * See @bugref{5262} as to why we need to do all this qtimeout/qwriter tricks.
         */
        vboxnetflt_promisc_stream_t *pPromiscStream = ASMAtomicUoReadPtrT(&pThis->u.s.pPromiscStream,
                                                                          vboxnetflt_promisc_stream_t *);
        if (   pPromiscStream
            && pPromiscStream->Stream.pReadQueue)
        {
            pData->pThis      = pThis;
            pData->fPromiscOn = fActive;
            if (ASMAtomicReadPtr(&pPromiscStream->TimeoutId))
                quntimeout(WR(pPromiscStream->Stream.pReadQueue), pPromiscStream->TimeoutId);
            timeout_id_t TimeoutId = qtimeout(WR(pPromiscStream->Stream.pReadQueue), vboxNetFltSolarisPromiscReqWrap,
                                              pData, 1 /* ticks */);
            ASMAtomicWritePtr(&pPromiscStream->TimeoutId, TimeoutId);
            return; /* pData will be freed by vboxNetFltSolarisPromiscReqWrap() */
        }
        else
            LogRel((DEVICE_NAME ":vboxNetFltPortOsSetActive pThis=%p fActive=%d missing stream!\n", pThis, fActive));
        RTMemFree(pData);
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltPortOsSetActive out of memory!\n"));
}


int vboxNetFltOsDisconnectIt(PVBOXNETFLTINS pThis)
{
    LogFunc((DEVICE_NAME ":vboxNetFltOsDisconnectIt pThis=%p\n", pThis));

    vboxNetFltSolarisDetachFromInterface(pThis);

    return VINF_SUCCESS;
}


int  vboxNetFltOsConnectIt(PVBOXNETFLTINS pThis)
{
    /* Nothing to do here. */
    return VINF_SUCCESS;
}


void vboxNetFltOsDeleteInstance(PVBOXNETFLTINS pThis)
{
    LogFunc((DEVICE_NAME ":vboxNetFltOsDeleteInstance pThis=%p\n", pThis));

    mutex_destroy(&pThis->u.s.hMtx);

#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
    if (pThis->u.s.hPollMtx != NIL_RTSEMFASTMUTEX)
    {
        RTSemFastMutexDestroy(pThis->u.s.hPollMtx);
        pThis->u.s.hPollMtx = NIL_RTSEMFASTMUTEX;
    }
#endif

}


int vboxNetFltOsInitInstance(PVBOXNETFLTINS pThis, void *pvContext)
{
    LogFunc((DEVICE_NAME ":vboxNetFltOsInitInstance pThis=%p\n"));

    /*
     * Mutex used for loopback lockouts.
     */
    int rc = VINF_SUCCESS;
    mutex_init(&pThis->u.s.hMtx, NULL /* name */, MUTEX_DRIVER, NULL /* cookie */);
#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
    rc = RTSemFastMutexCreate(&pThis->u.s.hPollMtx);
    if (RT_SUCCESS(rc))
    {
#endif
        rc = vboxNetFltSolarisAttachToInterface(pThis);
        if (RT_SUCCESS(rc))
            return rc;

        LogRel((DEVICE_NAME ":vboxNetFltSolarisAttachToInterface failed. rc=%Rrc\n", rc));

#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
        RTSemFastMutexDestroy(pThis->u.s.hPollMtx);
        pThis->u.s.hPollMtx = NIL_RTSEMFASTMUTEX;
    }
    else
        LogRel((DEVICE_NAME ":vboxNetFltOsInitInstance failed to create poll mutex. rc=%Rrc\n", rc));
#endif

    mutex_destroy(&pThis->u.s.hMtx);

    NOREF(pvContext);
    return rc;
}


int vboxNetFltOsPreInitInstance(PVBOXNETFLTINS pThis)
{
    /*
     * Init. the solaris specific data.
     */
    pThis->u.s.hIface = NULL;
    pThis->u.s.pIp4Stream = NULL;
    pThis->u.s.pIp6Stream = NULL;
    pThis->u.s.pArpStream = NULL;
    pThis->u.s.pPromiscStream = NULL;
    pThis->u.s.fAttaching = false;
    pThis->u.s.fVLAN = false;
#ifdef VBOXNETFLT_SOLARIS_IPV6_POLLING
    pThis->u.s.hPollMtx = NIL_RTSEMFASTMUTEX;
#endif
    bzero(&pThis->u.s.MacAddr, sizeof(pThis->u.s.MacAddr));
    return VINF_SUCCESS;
}


bool vboxNetFltOsMaybeRediscovered(PVBOXNETFLTINS pThis)
{
    /*
     * We don't support interface rediscovery on Solaris hosts because the
     * filter is very tightly bound to the stream.
     */
    return false;
}


void vboxNetFltPortOsNotifyMacAddress(PVBOXNETFLTINS pThis, void *pvIfData, PCRTMAC pMac)
{
    NOREF(pThis); NOREF(pvIfData); NOREF(pMac);
}


int vboxNetFltPortOsConnectInterface(PVBOXNETFLTINS pThis, void *pvIf, void **ppvIfData)
{
    /* Nothing to do */
    NOREF(pThis); NOREF(pvIf); NOREF(ppvIfData);
    return VINF_SUCCESS;
}


int vboxNetFltPortOsDisconnectInterface(PVBOXNETFLTINS pThis, void *pvIfData)
{
    /* Nothing to do */
    NOREF(pThis); NOREF(pvIfData);
    return VINF_SUCCESS;
}


int vboxNetFltPortOsXmit(PVBOXNETFLTINS pThis, void *pvIfData, PINTNETSG pSG, uint32_t fDst)
{
    NOREF(pvIfData);
    LogFunc((DEVICE_NAME ":vboxNetFltPortOsXmit pThis=%p pSG=%p fDst=%d\n", pThis, pSG, fDst));

    int rc = VINF_SUCCESS;
    if (fDst & INTNETTRUNKDIR_WIRE)
    {
        vboxnetflt_promisc_stream_t *pPromiscStream = ASMAtomicUoReadPtrT(&pThis->u.s.pPromiscStream,
                                                                          vboxnetflt_promisc_stream_t *);
        if (RT_LIKELY(pPromiscStream))
        {
            mblk_t *pMsg = vboxNetFltSolarisMBlkFromSG(pThis, pSG, fDst);
            if (RT_LIKELY(pMsg))
            {
                Log((DEVICE_NAME ":vboxNetFltPortOsXmit INTNETTRUNKDIR_WIRE\n"));

                vboxNetFltSolarisQueueLoopback(pThis, pPromiscStream, pMsg);
                putnext(WR(pPromiscStream->Stream.pReadQueue), pMsg);
            }
            else
            {
                LogRel((DEVICE_NAME ":vboxNetFltPortOsXmit vboxNetFltSolarisMBlkFromSG failed.\n"));
                return VERR_NO_MEMORY;
            }
        }
    }

    if (fDst & INTNETTRUNKDIR_HOST)
    {
        /*
         * For unplumbed interfaces we would not be bound to IP or ARP.
         * We either bind to both or neither; so atomic reading one should be sufficient.
         */
        vboxnetflt_stream_t *pIp4Stream = ASMAtomicUoReadPtrT(&pThis->u.s.pIp4Stream, vboxnetflt_stream_t *);
        if (!pIp4Stream)
            return rc;

        /*
         * Create a message block and send it up the host stack (upstream).
         */
        mblk_t *pMsg = vboxNetFltSolarisMBlkFromSG(pThis, pSG, fDst);
        if (RT_LIKELY(pMsg))
        {
            PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pMsg->b_rptr;

            /*
             * Send message up ARP stream.
             */
            if (pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_ARP))
            {
                Log((DEVICE_NAME ":vboxNetFltPortOsXmit INTNETTRUNKDIR_HOST ARP\n"));

                vboxnetflt_stream_t *pArpStream = ASMAtomicUoReadPtrT(&pThis->u.s.pArpStream, vboxnetflt_stream_t *);
                if (pArpStream)
                {
                    /*
                     * Construct a DL_UNITDATA_IND style message for ARP as it doesn't understand fast path.
                     */
                    mblk_t *pDlpiMsg;
                    rc = vboxNetFltSolarisRawToUnitData(pMsg, &pDlpiMsg);
                    if (RT_SUCCESS(rc))
                    {
                        pMsg = pDlpiMsg;

                        queue_t *pArpReadQueue = pArpStream->pReadQueue;
                        putnext(pArpReadQueue, pMsg);
                    }
                    else
                    {
                        LogRel((DEVICE_NAME ":vboxNetFltSolarisRawToUnitData failed!\n"));
                        freemsg(pMsg);
                        rc = VERR_NO_MEMORY;
                    }
                }
                else
                    freemsg(pMsg);  /* Should really never happen... */
            }
            else
            {
                 vboxnetflt_stream_t *pIp6Stream = ASMAtomicUoReadPtrT(&pThis->u.s.pIp6Stream, vboxnetflt_stream_t *);
                 if (   pEthHdr->EtherType == RT_H2BE_U16(RTNET_ETHERTYPE_IPV6)
                     && pIp6Stream)
                 {
                     /*
                      * Send messages up IPv6 stream.
                      */
                     Log((DEVICE_NAME ":vboxNetFltPortOsXmit INTNETTRUNKDIR_HOST IPv6\n"));

                     pMsg->b_rptr += sizeof(RTNETETHERHDR);
                     queue_t *pIp6ReadQueue = pIp6Stream->pReadQueue;
                     putnext(pIp6ReadQueue, pMsg);
                 }
                 else
                 {
                    /*
                     * Send messages up IPv4 stream.
                     */
                    Log((DEVICE_NAME ":vboxNetFltPortOsXmit INTNETTRUNKDIR_HOST IPv4\n"));

                    pMsg->b_rptr += sizeof(RTNETETHERHDR);
                    queue_t *pIp4ReadQueue = pIp4Stream->pReadQueue;
                    putnext(pIp4ReadQueue, pMsg);
                }
            }
        }
        else
        {
            LogRel((DEVICE_NAME ":vboxNetFltSolarisMBlkFromSG failed.\n"));
            rc = VERR_NO_MEMORY;
        }
    }

    return rc;
}

