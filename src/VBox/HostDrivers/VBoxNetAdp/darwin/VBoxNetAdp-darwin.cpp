/* $Id: VBoxNetAdp-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetAdp - Virtual Network Adapter Driver (Host), Darwin Specific Code.
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
#define LOG_GROUP LOG_GROUP_NET_ADP_DRV
#include "../../../Runtime/r0drv/darwin/the-darwin-kernel.h"

#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/version.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/semaphore.h>
#include <iprt/spinlock.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/alloca.h>

#include "../../darwin/VBoxNetSend.h"

#include <sys/systm.h>
RT_C_DECLS_BEGIN /* Buggy 10.4 headers, fixed in 10.5. */
#include <sys/kpi_mbuf.h>
RT_C_DECLS_END

#include <net/ethernet.h>
#include <net/if_ether.h>
#include <net/if_types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <miscfs/devfs/devfs.h>
RT_C_DECLS_BEGIN
#include <net/bpf.h>
RT_C_DECLS_END

#define VBOXNETADP_OS_SPECFIC 1
#include "../VBoxNetAdpInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The maximum number of SG segments.
 * Used to prevent stack overflow and similar bad stuff. */
#define VBOXNETADP_DARWIN_MAX_SEGS       32
#define VBOXNETADP_DARWIN_MAX_FAMILIES   4
#define VBOXNETADP_DARWIN_NAME           "vboxnet"
#define VBOXNETADP_DARWIN_MTU            1500
#define VBOXNETADP_DARWIN_DETACH_TIMEOUT 500

#define VBOXNETADP_FROM_IFACE(iface) ((PVBOXNETADP) ifnet_softc(iface))


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
static kern_return_t    VBoxNetAdpDarwinStart(struct kmod_info *pKModInfo, void *pvData);
static kern_return_t    VBoxNetAdpDarwinStop(struct kmod_info *pKModInfo, void *pvData);
RT_C_DECLS_END

static int VBoxNetAdpDarwinOpen(dev_t Dev, int fFlags, int fDevType, struct proc *pProcess);
static int VBoxNetAdpDarwinClose(dev_t Dev, int fFlags, int fDevType, struct proc *pProcess);
static int VBoxNetAdpDarwinIOCtl(dev_t Dev, u_long iCmd, caddr_t pData, int fFlags, struct proc *pProcess);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Declare the module stuff.
 */
RT_C_DECLS_BEGIN
extern kern_return_t _start(struct kmod_info *pKModInfo, void *pvData);
extern kern_return_t _stop(struct kmod_info *pKModInfo, void *pvData);

KMOD_EXPLICIT_DECL(VBoxNetAdp, VBOX_VERSION_STRING, _start, _stop)
DECL_HIDDEN_DATA(kmod_start_func_t *) _realmain = VBoxNetAdpDarwinStart;
DECL_HIDDEN_DATA(kmod_stop_func_t  *) _antimain = VBoxNetAdpDarwinStop;
DECL_HIDDEN_DATA(int)                 _kext_apple_cc = __APPLE_CC__;
RT_C_DECLS_END

/**
 * The (common) global data.
 */
static int   g_nCtlDev = -1; /* Major dev number */
static void *g_hCtlDev = 0;  /* FS dev handle */

/**
 * The character device switch table for the driver.
 */
static struct cdevsw    g_ChDev =
{
    /*.d_open     = */VBoxNetAdpDarwinOpen,
    /*.d_close    = */VBoxNetAdpDarwinClose,
    /*.d_read     = */eno_rdwrt,
    /*.d_write    = */eno_rdwrt,
    /*.d_ioctl    = */VBoxNetAdpDarwinIOCtl,
    /*.d_stop     = */eno_stop,
    /*.d_reset    = */eno_reset,
    /*.d_ttys     = */NULL,
    /*.d_select   = */eno_select,
    /*.d_mmap     = */eno_mmap,
    /*.d_strategy = */eno_strat,
    /*.d_getc     = */(void *)(uintptr_t)&enodev, //eno_getc,
    /*.d_putc     = */(void *)(uintptr_t)&enodev, //eno_putc,
    /*.d_type     = */0
};



static void vboxNetAdpDarwinComposeUUID(PVBOXNETADP pThis, PRTUUID pUuid)
{
    /* Generate UUID from name and MAC address. */
    RTUuidClear(pUuid);
    memcpy(pUuid->au8, "vboxnet", 7);
    pUuid->Gen.u8ClockSeqHiAndReserved = (pUuid->Gen.u8ClockSeqHiAndReserved & 0x3f) | 0x80;
    pUuid->Gen.u16TimeHiAndVersion = (pUuid->Gen.u16TimeHiAndVersion & 0x0fff) | 0x4000;
    pUuid->Gen.u8ClockSeqLow = pThis->iUnit;
    vboxNetAdpComposeMACAddress(pThis, (PRTMAC)pUuid->Gen.au8Node);
}

static errno_t vboxNetAdpDarwinOutput(ifnet_t pIface, mbuf_t pMBuf)
{
    /*
     * We are a dummy interface with all the real work done in
     * VBoxNetFlt bridged networking filter.  If anything makes it
     * this far, it must be a broadcast or a packet for an unknown
     * guest that intnet didn't know where to dispatch.  In that case
     * we must still do the BPF tap and stats.
     */
    bpf_tap_out(pIface, DLT_EN10MB, pMBuf, NULL, 0);
    ifnet_stat_increment_out(pIface, 1, mbuf_len(pMBuf), 0);

    mbuf_freem_list(pMBuf);
    return 0;
}

static void vboxNetAdpDarwinDetach(ifnet_t pIface)
{
    PVBOXNETADP pThis = VBOXNETADP_FROM_IFACE(pIface);
    Assert(pThis);
    Log2(("vboxNetAdpDarwinDetach: Signaling detach to vboxNetAdpUnregisterDevice.\n"));
    /* Let vboxNetAdpDarwinUnregisterDevice know that the interface has been detached. */
    RTSemEventSignal(pThis->u.s.hEvtDetached);
}

static errno_t vboxNetAdpDarwinDemux(ifnet_t pIface, mbuf_t pMBuf,
                                     char *pFrameHeader,
                                     protocol_family_t *pProtocolFamily)
{
    /*
     * Anything we get here comes from VBoxNetFlt bridged networking
     * filter where it has already been accounted for and fed to bpf.
     */
    return ether_demux(pIface, pMBuf, pFrameHeader, pProtocolFamily);
}


static errno_t vboxNetAdpDarwinIfIOCtl(ifnet_t pIface, unsigned long uCmd, void *pvData)
{
    errno_t error = 0;

    if (pvData == NULL)
    {
        /*
         * Common pattern in the kernel code is to make changes in the
         * net layer and then notify the device driver by calling its
         * ioctl function with NULL parameter, e.g.:
         *
         *   ifnet_set_flags(interface, ...);
         *   ifnet_ioctl(interface, 0, SIOCSIFFLAGS, NULL);
         *
         * These are no-ops for us, so tell the caller we succeeded
         * because some callers do check that return value.
         */
        switch (uCmd)
        {
            case SIOCSIFFLAGS:
                Log2(("VBoxNetAdp: %s%d: SIOCSIFFLAGS (null): flags = 0x%04hx\n",
                      ifnet_name(pIface), ifnet_unit(pIface),
                      (uint16_t)ifnet_flags(pIface)));
                return 0;

            case SIOCADDMULTI:
            case SIOCDELMULTI:
                Log2(("VBoxNetAdp: %s%d: SIOC%sMULTI (null)\n",
                      ifnet_name(pIface), ifnet_unit(pIface),
                      uCmd == SIOCADDMULTI ? "ADD" : "DEL"));
                return 0;
        }
    }

    Log2(("VBoxNetAdp: %s%d: %c%c '%c' %u len %u\n",
          ifnet_name(pIface), ifnet_unit(pIface),
          uCmd & IOC_OUT ? '<' : '-',
          uCmd & IOC_IN  ? '>' : '-',
          IOCGROUP(uCmd),
          uCmd & 0xff,
          IOCPARM_LEN(uCmd)));

    error = ether_ioctl(pIface, uCmd, pvData);
    return error;
}


int vboxNetAdpOsCreate(PVBOXNETADP pThis, PCRTMAC pMACAddress)
{
    int rc;
    struct ifnet_init_params Params;
    RTUUID uuid;
    struct sockaddr_dl mac;

    pThis->u.s.hEvtDetached = NIL_RTSEMEVENT;
    rc = RTSemEventCreate(&pThis->u.s.hEvtDetached);
    if (RT_FAILURE(rc))
    {
        printf("vboxNetAdpOsCreate: failed to create semaphore (rc=%d).\n", rc);
        return rc;
    }

    mac.sdl_len = sizeof(mac);
    mac.sdl_family = AF_LINK;
    mac.sdl_alen = ETHER_ADDR_LEN;
    mac.sdl_nlen = 0;
    mac.sdl_slen = 0;
    memcpy(LLADDR(&mac), pMACAddress->au8, mac.sdl_alen);

    RTStrPrintf(pThis->szName, VBOXNETADP_MAX_NAME_LEN, "%s%d", VBOXNETADP_NAME, pThis->iUnit);
    vboxNetAdpDarwinComposeUUID(pThis, &uuid);
    Params.uniqueid = uuid.au8;
    Params.uniqueid_len = sizeof(uuid);
    Params.name = VBOXNETADP_NAME;
    Params.unit = pThis->iUnit;
    Params.family = IFNET_FAMILY_ETHERNET;
    Params.type = IFT_ETHER;
    Params.output = vboxNetAdpDarwinOutput;
    Params.demux = vboxNetAdpDarwinDemux;
    Params.add_proto = ether_add_proto;
    Params.del_proto = ether_del_proto;
    Params.check_multi = ether_check_multi;
    Params.framer = ether_frameout;
    Params.softc = pThis;
    Params.ioctl = vboxNetAdpDarwinIfIOCtl;
    Params.set_bpf_tap = NULL;
    Params.detach = vboxNetAdpDarwinDetach;
    Params.event = NULL;
    Params.broadcast_addr = "\xFF\xFF\xFF\xFF\xFF\xFF";
    Params.broadcast_len = ETHER_ADDR_LEN;

    errno_t err = ifnet_allocate(&Params, &pThis->u.s.pIface);
    if (!err)
    {
        err = ifnet_attach(pThis->u.s.pIface, &mac);
        if (!err)
        {
            bpfattach(pThis->u.s.pIface, DLT_EN10MB, ETHER_HDR_LEN);

            err = ifnet_set_flags(pThis->u.s.pIface, IFF_RUNNING | IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST, 0xFFFF);
            if (!err)
            {
                ifnet_set_mtu(pThis->u.s.pIface, VBOXNETADP_MTU);
                VBoxNetSendDummy(pThis->u.s.pIface);
                return VINF_SUCCESS;
            }
            else
                Log(("vboxNetAdpDarwinRegisterDevice: Failed to set flags (err=%d).\n", err));
            ifnet_detach(pThis->u.s.pIface);
        }
        else
            Log(("vboxNetAdpDarwinRegisterDevice: Failed to attach to interface (err=%d).\n", err));
        ifnet_release(pThis->u.s.pIface);
    }
    else
        Log(("vboxNetAdpDarwinRegisterDevice: Failed to allocate interface (err=%d).\n", err));

    RTSemEventDestroy(pThis->u.s.hEvtDetached);
    pThis->u.s.hEvtDetached = NIL_RTSEMEVENT;

    return RTErrConvertFromErrno(err);
}

void vboxNetAdpOsDestroy(PVBOXNETADP pThis)
{
    /* Bring down the interface */
    int rc = VINF_SUCCESS;
    errno_t err;

    AssertPtr(pThis->u.s.pIface);
    Assert(pThis->u.s.hEvtDetached != NIL_RTSEMEVENT);

    err = ifnet_set_flags(pThis->u.s.pIface, 0, IFF_UP | IFF_RUNNING);
    if (err)
        Log(("vboxNetAdpDarwinUnregisterDevice: Failed to bring down interface "
             "(err=%d).\n", err));
    err = ifnet_detach(pThis->u.s.pIface);
    if (err)
        Log(("vboxNetAdpDarwinUnregisterDevice: Failed to detach interface "
             "(err=%d).\n", err));
    Log2(("vboxNetAdpDarwinUnregisterDevice: Waiting for 'detached' event...\n"));
    /* Wait until we get a signal from detach callback. */
    rc = RTSemEventWait(pThis->u.s.hEvtDetached, VBOXNETADP_DETACH_TIMEOUT);
    if (rc == VERR_TIMEOUT)
        LogRel(("VBoxAdpDrv: Failed to detach interface %s%d\n.",
                VBOXNETADP_NAME, pThis->iUnit));
    err = ifnet_release(pThis->u.s.pIface);
    if (err)
        Log(("vboxNetAdpUnregisterDevice: Failed to release interface (err=%d).\n", err));

    RTSemEventDestroy(pThis->u.s.hEvtDetached);
    pThis->u.s.hEvtDetached = NIL_RTSEMEVENT;
}

/**
 * Device open. Called on open /dev/vboxnetctl
 *
 * @param   Dev         The device number.
 * @param   fFlags      ???.
 * @param   fDevType    ???.
 * @param   pProcess    The process issuing this request.
 */
static int VBoxNetAdpDarwinOpen(dev_t Dev, int fFlags, int fDevType, struct proc *pProcess)
{
    RT_NOREF(Dev, fFlags, fDevType, pProcess);
#ifdef LOG_ENABLED
    char szName[128];
    szName[0] = '\0';
    proc_name(proc_pid(pProcess), szName, sizeof(szName));
    Log(("VBoxNetAdpDarwinOpen: pid=%d '%s'\n", proc_pid(pProcess), szName));
#endif
    return 0;
}

/**
 * Close device.
 */
static int VBoxNetAdpDarwinClose(dev_t Dev, int fFlags, int fDevType, struct proc *pProcess)
{
    RT_NOREF(Dev, fFlags, fDevType, pProcess);
    Log(("VBoxNetAdpDarwinClose: pid=%d\n", proc_pid(pProcess)));
    return 0;
}

/**
 * Device I/O Control entry point.
 *
 * @returns Darwin for slow IOCtls and VBox status code for the fast ones.
 * @param   Dev         The device number (major+minor).
 * @param   iCmd        The IOCtl command.
 * @param   pData       Pointer to the data (if any it's a SUPDRVIOCTLDATA (kernel copy)).
 * @param   fFlags      Flag saying we're a character device (like we didn't know already).
 * @param   pProcess    The process issuing this request.
 */
static int VBoxNetAdpDarwinIOCtl(dev_t Dev, u_long iCmd, caddr_t pData, int fFlags, struct proc *pProcess)
{
    RT_NOREF(Dev, fFlags, pProcess);
    uint32_t cbReq = IOCPARM_LEN(iCmd);
    PVBOXNETADPREQ pReq = (PVBOXNETADPREQ)pData;
    int rc;

    Log(("VBoxNetAdpDarwinIOCtl: param len %#x; iCmd=%#lx\n", cbReq, iCmd));
    switch (IOCBASECMD(iCmd))
    {
        case IOCBASECMD(VBOXNETADP_CTL_ADD):
        {
            if (   (IOC_DIRMASK & iCmd) != IOC_INOUT
                || cbReq < sizeof(VBOXNETADPREQ))
                return EINVAL;

            PVBOXNETADP pNew;
            Log(("VBoxNetAdpDarwinIOCtl: szName=%s\n", pReq->szName));
            rc = vboxNetAdpCreate(&pNew,
                                  pReq->szName[0] && RTStrEnd(pReq->szName, RT_MIN(cbReq, sizeof(pReq->szName))) ?
                                  pReq->szName : NULL);
            if (RT_FAILURE(rc))
                return rc == VERR_OUT_OF_RESOURCES ? ENOMEM : EINVAL;

            Assert(strlen(pReq->szName) < sizeof(pReq->szName));
            strncpy(pReq->szName, pNew->szName, sizeof(pReq->szName) - 1);
            pReq->szName[sizeof(pReq->szName) - 1] = '\0';
            Log(("VBoxNetAdpDarwinIOCtl: Added '%s'\n", pReq->szName));
            break;
        }

        case IOCBASECMD(VBOXNETADP_CTL_REMOVE):
        {
            if (!RTStrEnd(pReq->szName, RT_MIN(cbReq, sizeof(pReq->szName))))
                return EINVAL;

            PVBOXNETADP pAdp = vboxNetAdpFindByName(pReq->szName);
            if (!pAdp)
                return EINVAL;

            rc = vboxNetAdpDestroy(pAdp);
            if (RT_FAILURE(rc))
                return EINVAL;
            Log(("VBoxNetAdpDarwinIOCtl: Removed %s\n", pReq->szName));
            break;
        }

        default:
            printf("VBoxNetAdpDarwinIOCtl: unknown command %lx.\n", IOCBASECMD(iCmd));
            return EINVAL;
    }

    return 0;
}

int  vboxNetAdpOsInit(PVBOXNETADP pThis)
{
    /*
     * Init the darwin specific members.
     */
    pThis->u.s.pIface = NULL;
    pThis->u.s.hEvtDetached = NIL_RTSEMEVENT;

    return VINF_SUCCESS;
}

/**
 * Start the kernel module.
 */
static kern_return_t    VBoxNetAdpDarwinStart(struct kmod_info *pKModInfo, void *pvData)
{
    RT_NOREF(pKModInfo, pvData);
    int rc;

    /*
     * Initialize IPRT and find our module tag id.
     * (IPRT is shared with VBoxDrv, it creates the loggers.)
     */
    rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        Log(("VBoxNetAdpDarwinStart\n"));
        rc = vboxNetAdpInit();
        if (RT_SUCCESS(rc))
        {
            g_nCtlDev = cdevsw_add(-1, &g_ChDev);
            if (g_nCtlDev < 0)
            {
                LogRel(("VBoxAdp: failed to register control device."));
                rc = VERR_CANT_CREATE;
            }
            else
            {
                g_hCtlDev = devfs_make_node(makedev(g_nCtlDev, 0), DEVFS_CHAR,
                                            UID_ROOT, GID_WHEEL, 0600, VBOXNETADP_CTL_DEV_NAME);
                if (!g_hCtlDev)
                {
                    LogRel(("VBoxAdp: failed to create FS node for control device."));
                    rc = VERR_CANT_CREATE;
                }
            }
        }

        if (RT_SUCCESS(rc))
        {
            LogRel(("VBoxAdpDrv: version " VBOX_VERSION_STRING " r%d\n", VBOX_SVN_REV));
            return KMOD_RETURN_SUCCESS;
        }

        LogRel(("VBoxAdpDrv: failed to initialize device extension (rc=%d)\n", rc));
        RTR0Term();
    }
    else
        printf("VBoxAdpDrv: failed to initialize IPRT (rc=%d)\n", rc);

    return KMOD_RETURN_FAILURE;
}


/**
 * Stop the kernel module.
 */
static kern_return_t VBoxNetAdpDarwinStop(struct kmod_info *pKModInfo, void *pvData)
{
    RT_NOREF(pKModInfo, pvData);
    Log(("VBoxNetAdpDarwinStop\n"));

    vboxNetAdpShutdown();
    /* Remove control device */
    devfs_remove(g_hCtlDev);
    cdevsw_remove(g_nCtlDev, &g_ChDev);

    RTR0Term();

    return KMOD_RETURN_SUCCESS;
}
