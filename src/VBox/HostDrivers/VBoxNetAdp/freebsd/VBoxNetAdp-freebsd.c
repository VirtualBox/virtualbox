/* $Id: VBoxNetAdp-freebsd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetAdp - Virtual Network Adapter Driver (Host), FreeBSD Specific Code.
 */

/*-
 * Copyright (c) 2009 Fredrik Lindberg <fli@shapeshifter.se>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <sys/param.h>
#undef PVM
#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/bpf.h>

#define LOG_GROUP LOG_GROUP_NET_ADP_DRV
#include <VBox/version.h>
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/spinlock.h>
#include <iprt/process.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/alloc.h>
#include <iprt/errcore.h>

#define VBOXNETADP_OS_SPECFIC 1
#include "../VBoxNetAdpInternal.h"

#if defined(__FreeBSD_version) && __FreeBSD_version >= 800500
# include <sys/jail.h>
# include <net/vnet.h>

# define VBOXCURVNET_SET(arg)           CURVNET_SET_QUIET(arg)
# define VBOXCURVNET_SET_FROM_UCRED()   VBOXCURVNET_SET(CRED_TO_VNET(curthread->td_ucred))
# define VBOXCURVNET_RESTORE()          CURVNET_RESTORE()

#else /* !defined(__FreeBSD_version) || __FreeBSD_version < 800500 */

# define VBOXCURVNET_SET(arg)
# define VBOXCURVNET_SET_FROM_UCRED()
# define VBOXCURVNET_RESTORE()

#endif /* !defined(__FreeBSD_version) || __FreeBSD_version < 800500 */

static int VBoxNetAdpFreeBSDCtrlioctl(struct cdev *, u_long, caddr_t, int flags,
    struct thread *);
static struct cdevsw vboxnetadp_cdevsw =
{
    .d_version = D_VERSION,
    .d_ioctl = VBoxNetAdpFreeBSDCtrlioctl,
    .d_read = (d_read_t *)nullop,
    .d_write = (d_write_t *)nullop,
    .d_name = VBOXNETADP_CTL_DEV_NAME,
};

static struct cdev *VBoxNetAdpFreeBSDcdev;

static int VBoxNetAdpFreeBSDModuleEvent(struct module *, int, void *);
static moduledata_t g_VBoxNetAdpFreeBSDModule = {
    "vboxnetadp",
    VBoxNetAdpFreeBSDModuleEvent,
    NULL
};

/** Declare the module as a pseudo device. */
DECLARE_MODULE(vboxnetadp, g_VBoxNetAdpFreeBSDModule, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(vboxnetadp, 1);
MODULE_DEPEND(vboxnetadp, vboxdrv, 1, 1, 1);
MODULE_DEPEND(vboxnetadp, vboxnetflt, 1, 1, 1);

/**
 * Module event handler
 */
static int
VBoxNetAdpFreeBSDModuleEvent(struct module *pMod, int enmEventType, void *pvArg)
{
    int rc = 0;

    Log(("VBoxNetAdpFreeBSDModuleEvent\n"));

    switch (enmEventType)
    {
        case MOD_LOAD:
            rc = RTR0Init(0);
            if (RT_FAILURE(rc))
            {
                Log(("RTR0Init failed %d\n", rc));
                return RTErrConvertToErrno(rc);
            }
            rc = vboxNetAdpInit();
            if (RT_FAILURE(rc))
            {
                RTR0Term();
                Log(("vboxNetAdpInit failed %d\n", rc));
                return RTErrConvertToErrno(rc);
            }
            /* Create dev node */
            VBoxNetAdpFreeBSDcdev = make_dev(&vboxnetadp_cdevsw, 0,
                UID_ROOT, GID_WHEEL, 0600, VBOXNETADP_CTL_DEV_NAME);

            break;

        case MOD_UNLOAD:
            vboxNetAdpShutdown();
            destroy_dev(VBoxNetAdpFreeBSDcdev);
            RTR0Term();
            break;
        case MOD_SHUTDOWN:
        case MOD_QUIESCE:
        default:
            return EOPNOTSUPP;
    }

    if (RT_SUCCESS(rc))
        return 0;
    return RTErrConvertToErrno(rc);
}

/**
 * Device I/O Control entry point.
 */
static int
VBoxNetAdpFreeBSDCtrlioctl(struct cdev *dev, u_long iCmd, caddr_t data, int flags, struct thread *td)
{
    PVBOXNETADP pAdp;
    PVBOXNETADPREQ pReq = (PVBOXNETADPREQ)data;
    struct ifnet *ifp;
    int rc;

    switch (iCmd)
    {
        case VBOXNETADP_CTL_ADD:
            if (   !(iCmd & IOC_INOUT)   /* paranoia*/
                || IOCPARM_LEN(iCmd) < sizeof(*pReq))
                return EINVAL;

            rc = vboxNetAdpCreate(&pAdp,
                                  pReq->szName[0] && RTStrEnd(pReq->szName, RT_MIN(IOCPARM_LEN(iCmd), sizeof(pReq->szName))) ?
                                  pReq->szName : NULL);
            if (RT_FAILURE(rc))
                return EINVAL;

            strncpy(pReq->szName, pAdp->szName, sizeof(pReq->szName) - 1);
            pReq->szName[sizeof(pReq->szName) - 1] = '\0';
            break;

        case VBOXNETADP_CTL_REMOVE:
            if (!RTStrEnd(pReq->szName, RT_MIN(sizeof(pReq->szName), IOCPARM_LEN(iCmd))))
                return EINVAL;

            pAdp = vboxNetAdpFindByName(pReq->szName);
            if (!pAdp)
                return EINVAL;

            rc = vboxNetAdpDestroy(pAdp);
            if (RT_FAILURE(rc))
                return EINVAL;

            break;

        default:
            return EINVAL;
    }
    return 0;
}

/**
 * Initialize device, just set the running flag.
 */
static void VBoxNetAdpFreeBSDNetinit(void *priv)
{
    PVBOXNETADP pThis = priv;
    struct ifnet *ifp = pThis->u.s.ifp;

    ifp->if_drv_flags |= IFF_DRV_RUNNING;
}

/**
 * Transmit packets.
 * netflt has already done everything for us so we just hand the
 * packets to BPF and increment the packet stats.
 */
static void VBoxNetAdpFreeBSDNetstart(struct ifnet *ifp)
{
    PVBOXNETADP pThis = ifp->if_softc;
    struct mbuf *m;

    if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) != IFF_DRV_RUNNING)
        return;

    ifp->if_drv_flags |= IFF_DRV_OACTIVE;
    while (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
    {
#if __FreeBSD_version >= 1100036
        if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
#else
        ifp->if_opackets++;
#endif
        IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
        BPF_MTAP(ifp, m);
        m_freem(m);
    }
    ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

/**
 * Interface ioctl handling
 */
static int VBoxNetAdpFreeBSDNetioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
    int error = 0;

    switch (cmd)
    {
        case SIOCSIFFLAGS:
            if (ifp->if_flags & IFF_UP)
            {
                if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
                    ifp->if_init(ifp->if_softc);
            }
            else
            {
                if (ifp->if_drv_flags & IFF_DRV_RUNNING)
                    ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
            }
            break;
        case SIOCGIFMEDIA:
        {
            struct ifmediareq *ifmr;
            int count;

            ifmr = (struct ifmediareq *)data;
            count = ifmr->ifm_count;
            ifmr->ifm_count = 1;
            ifmr->ifm_status = IFM_AVALID;
            ifmr->ifm_active = IFM_ETHER;
            ifmr->ifm_status |= IFM_ACTIVE;
            ifmr->ifm_current = ifmr->ifm_active;
            if (count >= 1)
            {
                int media = IFM_ETHER;
                error = copyout(&media, ifmr->ifm_ulist, sizeof(int));
            }
            break;
        }
        default:
            return ether_ioctl(ifp, cmd, data);
    }
    return error;
}

int vboxNetAdpOsInit(PVBOXNETADP pThis)
{
    pThis->u.s.ifp = NULL;
    return VINF_SUCCESS;;
}

int vboxNetAdpOsCreate(PVBOXNETADP pThis, PCRTMAC pMac)
{
    struct ifnet *ifp;

    VBOXCURVNET_SET_FROM_UCRED();
    ifp = if_alloc(IFT_ETHER);
    if (ifp == NULL)
        return VERR_NO_MEMORY;

    if_initname(ifp, VBOXNETADP_NAME, pThis->iUnit);
    ifp->if_softc = pThis;
    ifp->if_mtu = ETHERMTU;
    ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
    ifp->if_ioctl = VBoxNetAdpFreeBSDNetioctl;
    ifp->if_start = VBoxNetAdpFreeBSDNetstart;
    ifp->if_init = VBoxNetAdpFreeBSDNetinit;
    IFQ_SET_MAXLEN(&ifp->if_snd, IFQ_MAXLEN);
    ifp->if_snd.ifq_drv_maxlen = IFQ_MAXLEN;
    IFQ_SET_READY(&ifp->if_snd);
    ether_ifattach(ifp, (void *)pMac);
    ifp->if_baudrate = 0;

    strncpy(pThis->szName, ifp->if_xname, VBOXNETADP_MAX_NAME_LEN);
    pThis->u.s.ifp = ifp;
    VBOXCURVNET_RESTORE();
    return 0;
}

void vboxNetAdpOsDestroy(PVBOXNETADP pThis)
{
    struct ifnet *ifp;

    ifp = pThis->u.s.ifp;
    VBOXCURVNET_SET(ifp->if_vnet);
    ether_ifdetach(ifp);
    if_free(ifp);
    VBOXCURVNET_RESTORE();
}
