/* $Id: SUPDrv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDrv - The VirtualBox Support Driver - Solaris specifics.
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
#define LOG_GROUP LOG_GROUP_SUP_DRV
#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/modctl.h>
#include <sys/kobj.h>
#include <sys/kobj_impl.h>
#include <sys/open.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/file.h>
#include <sys/priv_names.h>
#include <vm/hat.h>
#undef u /* /usr/include/sys/user.h:249:1 is where this is defined to (curproc->p_user). very cool. */

#include "../SUPDrvInternal.h"
#include <VBox/log.h>
#include <VBox/param.h>
#include <VBox/version.h>
#include <iprt/semaphore.h>
#include <iprt/spinlock.h>
#include <iprt/mp.h>
#include <iprt/path.h>
#include <iprt/power.h>
#include <iprt/process.h>
#include <iprt/thread.h>
#include <iprt/initterm.h>
#include <iprt/alloc.h>
#include <iprt/string.h>
#include <iprt/err.h>

#include "dtrace/SUPDrv.h"

extern caddr_t hat_kpm_pfn2va(pfn_t); /* Found in vm/hat.h on solaris 11.3, but not on older like 10u7. */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The system device name. */
#define DEVICE_NAME_SYS          "vboxdrv"
/** The user device name. */
#define DEVICE_NAME_USR          "vboxdrvu"
/** The module description as seen in 'modinfo'. */
#define DEVICE_DESC              "VirtualBox HostDrv"
/** Maximum number of driver instances. */
#define DEVICE_MAXINSTANCES      16


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int VBoxDrvSolarisOpen(dev_t *pDev, int fFlag, int fType, cred_t *pCred);
static int VBoxDrvSolarisClose(dev_t Dev, int fFlag, int fType, cred_t *pCred);
static int VBoxDrvSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred);
static int VBoxDrvSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred);
static int VBoxDrvSolarisIOCtl(dev_t Dev, int Cmd, intptr_t pArgs, int mode, cred_t *pCred, int *pVal);

static int VBoxDrvSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t Cmd);
static int VBoxDrvSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t Cmd);
static int VBoxDrvSolarisQuiesceNotNeeded(dev_info_t *pDip);

static int VBoxSupDrvErr2SolarisErr(int rc);
static int VBoxDrvSolarisIOCtlSlow(PSUPDRVSESSION pSession, int Cmd, int Mode, intptr_t pArgs);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * cb_ops: for drivers that support char/block entry points
 */
static struct cb_ops g_VBoxDrvSolarisCbOps =
{
    VBoxDrvSolarisOpen,
    VBoxDrvSolarisClose,
    nodev,                        /* b strategy */
    nodev,                        /* b dump */
    nodev,                        /* b print */
    VBoxDrvSolarisRead,
    VBoxDrvSolarisWrite,
    VBoxDrvSolarisIOCtl,
    nodev,                        /* c devmap */
    nodev,                        /* c mmap */
    nodev,                        /* c segmap */
    nochpoll,                     /* c poll */
    ddi_prop_op,                  /* property ops */
    NULL,                         /* streamtab  */
    D_NEW | D_MP,                 /* compat. flag */
    CB_REV                        /* revision */
};

/**
 * dev_ops: for driver device operations
 */
static struct dev_ops g_VBoxDrvSolarisDevOps =
{
    DEVO_REV,                     /* driver build revision */
    0,                            /* ref count */
    nulldev,                      /* get info */
    nulldev,                      /* identify */
    nulldev,                      /* probe */
    VBoxDrvSolarisAttach,
    VBoxDrvSolarisDetach,
    nodev,                        /* reset */
    &g_VBoxDrvSolarisCbOps,
    (struct bus_ops *)0,
    nodev,                        /* power */
    VBoxDrvSolarisQuiesceNotNeeded
};

/**
 * modldrv: export driver specifics to the kernel
 */
static struct modldrv g_VBoxDrvSolarisModule =
{
    &mod_driverops,               /* extern from kernel */
    DEVICE_DESC " " VBOX_VERSION_STRING "r" RT_XSTR(VBOX_SVN_REV),
    &g_VBoxDrvSolarisDevOps
};

/**
 * modlinkage: export install/remove/info to the kernel
 */
static struct modlinkage g_VBoxDrvSolarisModLinkage =
{
    MODREV_1,                     /* loadable module system revision */
    {
        &g_VBoxDrvSolarisModule,
        NULL                      /* terminate array of linkage structures */
    }
};

#ifndef USE_SESSION_HASH
/**
 * State info for each open file handle.
 */
typedef struct
{
    /**< Pointer to the session data. */
    PSUPDRVSESSION pSession;
} vbox_devstate_t;
#else
/** State info. for each driver instance. */
typedef struct
{
    dev_info_t     *pDip;         /* Device handle */
} vbox_devstate_t;
#endif

/** Opaque pointer to list of state */
static void *g_pVBoxDrvSolarisState;

/** Device extention & session data association structure */
static SUPDRVDEVEXT         g_DevExt;

/** Hash table */
static PSUPDRVSESSION       g_apSessionHashTab[19];
/** Spinlock protecting g_apSessionHashTab. */
static RTSPINLOCK           g_Spinlock = NIL_RTSPINLOCK;
/** Calculates bucket index into g_apSessionHashTab.*/
#define SESSION_HASH(sfn)   ((sfn) % RT_ELEMENTS(g_apSessionHashTab))

/**
 * Kernel entry points
 */
int _init(void)
{
#if 0    /* No IPRT logging before RTR0Init() is done! */
    LogFlowFunc(("vboxdrv:_init\n"));
#endif

    /*
     * Prevent module autounloading.
     */
    modctl_t *pModCtl = mod_getctl(&g_VBoxDrvSolarisModLinkage);
    if (pModCtl)
        pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD;
    else
        cmn_err(CE_NOTE, "vboxdrv: failed to disable autounloading!\n");

    /*
     * Initialize IPRT R0 driver, which internally calls OS-specific r0 init.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize the device extension
         */
        rc = supdrvInitDevExt(&g_DevExt, sizeof(SUPDRVSESSION));
        if (RT_SUCCESS(rc))
        {
            cmn_err(CE_CONT, "!tsc::mode %s @ tentative %lu Hz\n", SUPGetGIPModeName(g_DevExt.pGip), g_DevExt.pGip->u64CpuHz);

            /*
             * Initialize the session hash table.
             */
            memset(g_apSessionHashTab, 0, sizeof(g_apSessionHashTab));
            rc = RTSpinlockCreate(&g_Spinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "VBoxDrvSol");
            if (RT_SUCCESS(rc))
            {
                rc = ddi_soft_state_init(&g_pVBoxDrvSolarisState, sizeof(vbox_devstate_t), 8);
                if (!rc)
                {
                    rc = mod_install(&g_VBoxDrvSolarisModLinkage);
                    if (!rc)
                        return rc; /* success */

                    ddi_soft_state_fini(&g_pVBoxDrvSolarisState);
                    LogRel(("vboxdrv: mod_install failed! rc=%d\n", rc));
                }
                else
                    LogRel(("vboxdrv: failed to initialize soft state.\n"));

                RTSpinlockDestroy(g_Spinlock);
                g_Spinlock = NIL_RTSPINLOCK;
            }
            else
            {
                LogRel(("VBoxDrvSolarisAttach: RTSpinlockCreate failed\n"));
                rc = RTErrConvertToErrno(rc);
            }
            supdrvDeleteDevExt(&g_DevExt);
        }
        else
        {
            LogRel(("VBoxDrvSolarisAttach: supdrvInitDevExt failed\n"));
            rc = EINVAL;
        }
        RTR0TermForced();
    }
    else
    {
        LogRel(("VBoxDrvSolarisAttach: failed to init R0Drv\n"));
        rc = RTErrConvertToErrno(rc);
    }
    memset(&g_DevExt, 0, sizeof(g_DevExt));

    return rc;
}


int _fini(void)
{
    LogFlowFunc(("vboxdrv:_fini\n"));

    /*
     * Undo the work we did at start (in the reverse order).
     */
    int rc = mod_remove(&g_VBoxDrvSolarisModLinkage);
    if (rc != 0)
        return rc;

    supdrvDeleteDevExt(&g_DevExt);

    rc = RTSpinlockDestroy(g_Spinlock);
    AssertRC(rc);
    g_Spinlock = NIL_RTSPINLOCK;

    RTR0TermForced();

    memset(&g_DevExt, 0, sizeof(g_DevExt));

    ddi_soft_state_fini(&g_pVBoxDrvSolarisState);
    return 0;
}


int _info(struct modinfo *pModInfo)
{
#if 0    /* No IPRT logging before RTR0Init() is done! And yes this is called before _init()!*/
    LogFlowFunc(("vboxdrv:_init\n"));
#endif
    int e = mod_info(&g_VBoxDrvSolarisModLinkage, pModInfo);
    return e;
}


/**
 * Attach entry point, to attach a device to the system or resume it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Operation type (attach/resume).
 *
 * @return  corresponding solaris error code.
 */
static int VBoxDrvSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd)
{
    LogFlowFunc(("VBoxDrvSolarisAttach\n"));

    switch (enmCmd)
    {
        case DDI_ATTACH:
        {
            int rc;
#ifdef USE_SESSION_HASH
            int instance = ddi_get_instance(pDip);
            vbox_devstate_t *pState;

            if (ddi_soft_state_zalloc(g_pVBoxDrvSolarisState, instance) != DDI_SUCCESS)
            {
                LogRel(("VBoxDrvSolarisAttach: state alloc failed\n"));
                return DDI_FAILURE;
            }

            pState = ddi_get_soft_state(g_pVBoxDrvSolarisState, instance);
#endif

            /*
             * Register for suspend/resume notifications
             */
            rc = ddi_prop_create(DDI_DEV_T_NONE, pDip, DDI_PROP_CANSLEEP /* kmem alloc can sleep */,
                                "pm-hardware-state", "needs-suspend-resume", sizeof("needs-suspend-resume"));
            if (rc != DDI_PROP_SUCCESS)
                LogRel(("vboxdrv: Suspend/Resume notification registration failed.\n"));

            /*
             * Register ourselves as a character device, pseudo-driver
             */
#ifdef VBOX_WITH_HARDENING
            rc = ddi_create_priv_minor_node(pDip, DEVICE_NAME_SYS, S_IFCHR, 0 /*minor*/, DDI_PSEUDO,
                                            0, NULL, NULL, 0600);
#else
            rc = ddi_create_priv_minor_node(pDip, DEVICE_NAME_SYS, S_IFCHR, 0 /*minor*/, DDI_PSEUDO,
                                            0, "none", "none", 0666);
#endif
            if (rc == DDI_SUCCESS)
            {
                rc = ddi_create_priv_minor_node(pDip, DEVICE_NAME_USR, S_IFCHR, 1 /*minor*/, DDI_PSEUDO,
                                                0, "none", "none", 0666);
                if (rc == DDI_SUCCESS)
                {
#ifdef USE_SESSION_HASH
                    pState->pDip = pDip;
#endif
                    ddi_report_dev(pDip);
                    return DDI_SUCCESS;
                }
                ddi_remove_minor_node(pDip, NULL);
            }

            return DDI_FAILURE;
        }

        case DDI_RESUME:
        {
#if 0
            RTSemFastMutexRequest(g_DevExt.mtxGip);
            if (g_DevExt.pGipTimer)
                RTTimerStart(g_DevExt.pGipTimer, 0);

            RTSemFastMutexRelease(g_DevExt.mtxGip);
#endif
            RTPowerSignalEvent(RTPOWEREVENT_RESUME);
            LogFlow(("vboxdrv: Awakened from suspend.\n"));
            return DDI_SUCCESS;
        }

        default:
            return DDI_FAILURE;
    }

    return DDI_FAILURE;
}


/**
 * Detach entry point, to detach a device to the system or suspend it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Operation type (detach/suspend).
 *
 * @return  corresponding solaris error code.
 */
static int VBoxDrvSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    LogFlowFunc(("VBoxDrvSolarisDetach\n"));
    switch (enmCmd)
    {
        case DDI_DETACH:
        {
#ifndef USE_SESSION_HASH
            ddi_remove_minor_node(pDip, NULL);
#else
            int instance = ddi_get_instance(pDip);
            vbox_devstate_t *pState = ddi_get_soft_state(g_pVBoxDrvSolarisState, instance);
            ddi_remove_minor_node(pDip, NULL);
            ddi_soft_state_free(g_pVBoxDrvSolarisState, instance);
#endif
            ddi_prop_remove_all(pDip);
            return DDI_SUCCESS;
        }

        case DDI_SUSPEND:
        {
#if 0
            RTSemFastMutexRequest(g_DevExt.mtxGip);
            if (g_DevExt.pGipTimer && g_DevExt.cGipUsers > 0)
                RTTimerStop(g_DevExt.pGipTimer);

            RTSemFastMutexRelease(g_DevExt.mtxGip);
#endif
            RTPowerSignalEvent(RTPOWEREVENT_SUSPEND);
            LogFlow(("vboxdrv: Falling to suspend mode.\n"));
            return DDI_SUCCESS;

        }

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
static int VBoxDrvSolarisQuiesceNotNeeded(dev_info_t *pDip)
{
    return DDI_SUCCESS;
}


/**
 * open() worker.
 */
static int VBoxDrvSolarisOpen(dev_t *pDev, int fFlag, int fType, cred_t *pCred)
{
    const bool          fUnrestricted = getminor(*pDev) == 0;
    PSUPDRVSESSION      pSession;
    int                 rc;

    LogFlowFunc(("VBoxDrvSolarisOpen: pDev=%p:%#x\n", pDev, *pDev));

    /*
     * Validate input
     */
    if (   (getminor(*pDev) != 0 && getminor(*pDev) != 1)
        || fType != OTYP_CHR)
        return EINVAL; /* See mmopen for precedent. */

#ifndef USE_SESSION_HASH
    /*
     * Locate a new device open instance.
     *
     * For each open call we'll allocate an item in the soft state of the device.
     * The item index is stored in the dev_t. I hope this is ok...
     */
    vbox_devstate_t *pState = NULL;
    unsigned iOpenInstance;
    for (iOpenInstance = 0; iOpenInstance < 4096; iOpenInstance++)
    {
        if (    !ddi_get_soft_state(g_pVBoxDrvSolarisState, iOpenInstance) /* faster */
            &&  ddi_soft_state_zalloc(g_pVBoxDrvSolarisState, iOpenInstance) == DDI_SUCCESS)
        {
            pState = ddi_get_soft_state(g_pVBoxDrvSolarisState, iOpenInstance);
            break;
        }
    }
    if (!pState)
    {
        LogRel(("VBoxDrvSolarisOpen: too many open instances.\n"));
        return ENXIO;
    }

    /*
     * Create a new session.
     */
    rc = supdrvCreateSession(&g_DevExt, true /* fUser */, fUnrestricted, &pSession);
    if (RT_SUCCESS(rc))
    {
        pSession->Uid = crgetruid(pCred);
        pSession->Gid = crgetrgid(pCred);

        pState->pSession = pSession;
        *pDev = makedevice(getmajor(*pDev), iOpenInstance);
        LogFlow(("VBoxDrvSolarisOpen: Dev=%#x pSession=%p pid=%d r0proc=%p thread=%p\n",
                 *pDev, pSession, RTProcSelf(), RTR0ProcHandleSelf(), RTThreadNativeSelf() ));
        return 0;
    }

    /* failed - clean up */
    ddi_soft_state_free(g_pVBoxDrvSolarisState, iOpenInstance);

#else
    /*
     * Create a new session.
     * Sessions in Solaris driver are mostly useless. It's however needed
     * in VBoxDrvSolarisIOCtlSlow() while calling supdrvIOCtl()
     */
    rc = supdrvCreateSession(&g_DevExt, true /* fUser */, fUnrestricted, &pSession);
    if (RT_SUCCESS(rc))
    {
        unsigned        iHash;

        pSession->Uid = crgetruid(pCred);
        pSession->Gid = crgetrgid(pCred);

        /*
         * Insert it into the hash table.
         */
# error "Only one entry per process!"
        iHash = SESSION_HASH(pSession->Process);
        RTSpinlockAcquire(g_Spinlock);
        pSession->pNextHash = g_apSessionHashTab[iHash];
        g_apSessionHashTab[iHash] = pSession;
        RTSpinlockRelease(g_Spinlock);
        LogFlow(("VBoxDrvSolarisOpen success\n"));
    }

    int instance;
    for (instance = 0; instance < DEVICE_MAXINSTANCES; instance++)
    {
        vbox_devstate_t *pState = ddi_get_soft_state(g_pVBoxDrvSolarisState, instance);
        if (pState)
            break;
    }

    if (instance >= DEVICE_MAXINSTANCES)
    {
        LogRel(("VBoxDrvSolarisOpen: All instances exhausted\n"));
        return ENXIO;
    }

    *pDev = makedevice(getmajor(*pDev), instance);
#endif

    return VBoxSupDrvErr2SolarisErr(rc);
}


static int VBoxDrvSolarisClose(dev_t Dev, int flag, int otyp, cred_t *cred)
{
    LogFlowFunc(("VBoxDrvSolarisClose: Dev=%#x\n", Dev));

#ifndef USE_SESSION_HASH
    /*
     * Get the session and free the soft state item.
     */
    vbox_devstate_t *pState = ddi_get_soft_state(g_pVBoxDrvSolarisState, getminor(Dev));
    if (!pState)
    {
        LogRel(("VBoxDrvSolarisClose: no state data for %#x (%d)\n", Dev, getminor(Dev)));
        return EFAULT;
    }

    PSUPDRVSESSION pSession = pState->pSession;
    pState->pSession = NULL;
    ddi_soft_state_free(g_pVBoxDrvSolarisState, getminor(Dev));

    if (!pSession)
    {
        LogRel(("VBoxDrvSolarisClose: no session in state data for %#x (%d)\n", Dev, getminor(Dev)));
        return EFAULT;
    }
    LogFlow(("VBoxDrvSolarisClose: Dev=%#x pSession=%p pid=%d r0proc=%p thread=%p\n",
            Dev, pSession, RTProcSelf(), RTR0ProcHandleSelf(), RTThreadNativeSelf() ));

#else
    const RTPROCESS Process = RTProcSelf();
    const unsigned  iHash = SESSION_HASH(Process);
    PSUPDRVSESSION  pSession;

    /*
     * Remove from the hash table.
     */
    RTSpinlockAcquire(g_Spinlock);
    pSession = g_apSessionHashTab[iHash];
    if (pSession)
    {
        if (pSession->Process == Process)
        {
            g_apSessionHashTab[iHash] = pSession->pNextHash;
            pSession->pNextHash = NULL;
        }
        else
        {
            PSUPDRVSESSION pPrev = pSession;
            pSession = pSession->pNextHash;
            while (pSession)
            {
                if (pSession->Process == Process)
                {
                    pPrev->pNextHash = pSession->pNextHash;
                    pSession->pNextHash = NULL;
                    break;
                }

                /* next */
                pPrev = pSession;
                pSession = pSession->pNextHash;
            }
        }
    }
    RTSpinlockRelease(g_Spinlock);
    if (!pSession)
    {
        LogRel(("VBoxDrvSolarisClose: WHAT?!? pSession == NULL! This must be a mistake... pid=%d (close)\n", (int)Process));
        return EFAULT;
    }
#endif

    /*
     * Close the session.
     */
    supdrvSessionRelease(pSession);
    return 0;
}


static int VBoxDrvSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    LogFlowFunc(("VBoxDrvSolarisRead"));
    return 0;
}


static int VBoxDrvSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    LogFlowFunc(("VBoxDrvSolarisWrite"));
    return 0;
}


/**
 * Driver ioctl, an alternate entry point for this character driver.
 *
 * @param   Dev             Device number
 * @param   iCmd            Operation identifier
 * @param   pArgs           Arguments from user to driver
 * @param   Mode            Information bitfield (read/write, address space etc.)
 * @param   pCred           User credentials
 * @param   pVal            Return value for calling process.
 *
 * @return  corresponding solaris error code.
 */
static int VBoxDrvSolarisIOCtl(dev_t Dev, int iCmd, intptr_t pArgs, int Mode, cred_t *pCred, int *pVal)
{
#ifndef USE_SESSION_HASH
    /*
     * Get the session from the soft state item.
     */
    vbox_devstate_t *pState = ddi_get_soft_state(g_pVBoxDrvSolarisState, getminor(Dev));
    if (!pState)
    {
        LogRel(("VBoxDrvSolarisIOCtl: no state data for %#x (%d)\n", Dev, getminor(Dev)));
        return EINVAL;
    }

    PSUPDRVSESSION  pSession = pState->pSession;
    if (!pSession)
    {
        LogRel(("VBoxDrvSolarisIOCtl: no session in state data for %#x (%d)\n", Dev, getminor(Dev)));
        return DDI_SUCCESS;
    }
#else
    const RTPROCESS     Process = RTProcSelf();
    const unsigned      iHash = SESSION_HASH(Process);
    PSUPDRVSESSION      pSession;
    const bool          fUnrestricted = getminor(Dev) == 0;

    /*
     * Find the session.
     */
    RTSpinlockAcquire(g_Spinlock);
    pSession = g_apSessionHashTab[iHash];
    while (pSession && pSession->Process != Process && pSession->fUnrestricted == fUnrestricted);
        pSession = pSession->pNextHash;
    RTSpinlockRelease(g_Spinlock);
    if (!pSession)
    {
        LogRel(("VBoxSupDrvIOCtl: WHAT?!? pSession == NULL! This must be a mistake... pid=%d iCmd=%#x Dev=%#x\n",
                    (int)Process, iCmd, (int)Dev));
        return EINVAL;
    }
#endif

    /*
     * Deal with the two high-speed IOCtl that takes it's arguments from
     * the session and iCmd, and only returns a VBox status code.
     */
    AssertCompile((SUP_IOCTL_FAST_DO_FIRST & 0xff) == (SUP_IOCTL_FLAG | 64));
    if (   (unsigned)(iCmd - SUP_IOCTL_FAST_DO_FIRST) < (unsigned)32
        && pSession->fUnrestricted)
    {
        *pVal = supdrvIOCtlFast(iCmd - SUP_IOCTL_FAST_DO_FIRST, pArgs, &g_DevExt, pSession);
        return 0;
    }

    return VBoxDrvSolarisIOCtlSlow(pSession, iCmd, Mode, pArgs);
}


/** @def IOCPARM_LEN
 * Gets the length from the ioctl number.
 * This is normally defined by sys/ioccom.h on BSD systems...
 */
#ifndef IOCPARM_LEN
# define IOCPARM_LEN(x)     ( ((x) >> 16) & IOCPARM_MASK )
#endif


/**
 * Worker for VBoxSupDrvIOCtl that takes the slow IOCtl functions.
 *
 * @returns Solaris errno.
 *
 * @param   pSession    The session.
 * @param   iCmd        The IOCtl command.
 * @param   Mode        Information bitfield (for specifying ownership of data)
 * @param   iArg        User space address of the request buffer.
 */
static int VBoxDrvSolarisIOCtlSlow(PSUPDRVSESSION pSession, int iCmd, int Mode, intptr_t iArg)
{
    int         rc;
    uint32_t    cbBuf = 0;
    union
    {
        SUPREQHDR   Hdr;
        uint8_t     abBuf[64];
    }           StackBuf;
    PSUPREQHDR  pHdr;


    /*
     * Read the header.
     */
    if (RT_UNLIKELY(IOCPARM_LEN(iCmd) != sizeof(StackBuf.Hdr)))
    {
        LogRel(("VBoxDrvSolarisIOCtlSlow: iCmd=%#x len %d expected %d\n", iCmd, IOCPARM_LEN(iCmd), sizeof(StackBuf.Hdr)));
        return EINVAL;
    }
    rc = ddi_copyin((void *)iArg, &StackBuf.Hdr, sizeof(StackBuf.Hdr), Mode);
    if (RT_UNLIKELY(rc))
    {
        LogRel(("VBoxDrvSolarisIOCtlSlow: ddi_copyin(,%#lx,) failed; iCmd=%#x. rc=%d\n", iArg, iCmd, rc));
        return EFAULT;
    }
    if (RT_UNLIKELY((StackBuf.Hdr.fFlags & SUPREQHDR_FLAGS_MAGIC_MASK) != SUPREQHDR_FLAGS_MAGIC))
    {
        LogRel(("VBoxDrvSolarisIOCtlSlow: bad header magic %#x; iCmd=%#x\n", StackBuf.Hdr.fFlags & SUPREQHDR_FLAGS_MAGIC_MASK, iCmd));
        return EINVAL;
    }
    cbBuf = RT_MAX(StackBuf.Hdr.cbIn, StackBuf.Hdr.cbOut);
    if (RT_UNLIKELY(    StackBuf.Hdr.cbIn < sizeof(StackBuf.Hdr)
                    ||  StackBuf.Hdr.cbOut < sizeof(StackBuf.Hdr)
                    ||  cbBuf > _1M*16))
    {
        LogRel(("VBoxDrvSolarisIOCtlSlow: max(%#x,%#x); iCmd=%#x\n", StackBuf.Hdr.cbIn, StackBuf.Hdr.cbOut, iCmd));
        return EINVAL;
    }

    /*
     * Buffer the request.
     */
    if (cbBuf <= sizeof(StackBuf))
        pHdr = &StackBuf.Hdr;
    else
    {
        pHdr = RTMemTmpAlloc(cbBuf);
        if (RT_UNLIKELY(!pHdr))
        {
            LogRel(("VBoxDrvSolarisIOCtlSlow: failed to allocate buffer of %d bytes for iCmd=%#x.\n", cbBuf, iCmd));
            return ENOMEM;
        }
    }
    rc = ddi_copyin((void *)iArg, pHdr, cbBuf, Mode);
    if (RT_UNLIKELY(rc))
    {
        LogRel(("VBoxDrvSolarisIOCtlSlow: copy_from_user(,%#lx, %#x) failed; iCmd=%#x. rc=%d\n", iArg, cbBuf, iCmd, rc));
        if (pHdr != &StackBuf.Hdr)
            RTMemFree(pHdr);
        return EFAULT;
    }

    /*
     * Process the IOCtl.
     */
    rc = supdrvIOCtl(iCmd, &g_DevExt, pSession, pHdr, cbBuf);

    /*
     * Copy ioctl data and output buffer back to user space.
     */
    if (RT_LIKELY(!rc))
    {
        uint32_t cbOut = pHdr->cbOut;
        if (RT_UNLIKELY(cbOut > cbBuf))
        {
            LogRel(("VBoxDrvSolarisIOCtlSlow: too much output! %#x > %#x; iCmd=%#x!\n", cbOut, cbBuf, iCmd));
            cbOut = cbBuf;
        }
        rc = ddi_copyout(pHdr, (void *)iArg, cbOut, Mode);
        if (RT_UNLIKELY(rc != 0))
        {
            /* this is really bad */
            LogRel(("VBoxDrvSolarisIOCtlSlow: ddi_copyout(,%p,%d) failed. rc=%d\n", (void *)iArg, cbBuf, rc));
            rc = EFAULT;
        }
    }
    else
        rc = EINVAL;

    if (pHdr != &StackBuf.Hdr)
        RTMemTmpFree(pHdr);
    return rc;
}


/**
 * The SUPDRV IDC entry point.
 *
 * @returns VBox status code, see supdrvIDC.
 * @param   uReq        The request code.
 * @param   pReq        The request.
 */
int VBOXCALL SUPDrvSolarisIDC(uint32_t uReq, PSUPDRVIDCREQHDR pReq)
{
    PSUPDRVSESSION  pSession;

    /*
     * Some quick validations.
     */
    if (RT_UNLIKELY(!RT_VALID_PTR(pReq)))
        return VERR_INVALID_POINTER;

    pSession = pReq->pSession;
    if (pSession)
    {
        if (RT_UNLIKELY(!RT_VALID_PTR(pSession)))
            return VERR_INVALID_PARAMETER;
        if (RT_UNLIKELY(pSession->pDevExt != &g_DevExt))
            return VERR_INVALID_PARAMETER;
    }
    else if (RT_UNLIKELY(uReq != SUPDRV_IDC_REQ_CONNECT))
        return VERR_INVALID_PARAMETER;

    /*
     * Do the job.
     */
    return supdrvIDC(uReq, &g_DevExt, pSession, pReq);
}


/**
 * Converts an supdrv error code to a solaris error code.
 *
 * @returns corresponding solaris error code.
 * @param   rc      IPRT status code.
 */
static int VBoxSupDrvErr2SolarisErr(int rc)
{
    switch (rc)
    {
        case VINF_SUCCESS:              return 0;
        case VERR_GENERAL_FAILURE:      return EACCES;
        case VERR_INVALID_PARAMETER:    return EINVAL;
        case VERR_INVALID_MAGIC:        return EILSEQ;
        case VERR_INVALID_HANDLE:       return ENXIO;
        case VERR_INVALID_POINTER:      return EFAULT;
        case VERR_LOCK_FAILED:          return ENOLCK;
        case VERR_ALREADY_LOADED:       return EEXIST;
        case VERR_PERMISSION_DENIED:    return EPERM;
        case VERR_VERSION_MISMATCH:     return ENOSYS;
    }

    return EPERM;
}


void VBOXCALL supdrvOSCleanupSession(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession)
{
    NOREF(pDevExt);
    NOREF(pSession);
}


void VBOXCALL supdrvOSSessionHashTabInserted(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, void *pvUser)
{
    NOREF(pDevExt); NOREF(pSession); NOREF(pvUser);
}


void VBOXCALL supdrvOSSessionHashTabRemoved(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, void *pvUser)
{
    NOREF(pDevExt); NOREF(pSession); NOREF(pvUser);
}


/**
 * Initializes any OS specific object creator fields.
 */
void VBOXCALL   supdrvOSObjInitCreator(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession)
{
    NOREF(pObj);
    NOREF(pSession);
}


/**
 * Checks if the session can access the object.
 *
 * @returns true if a decision has been made.
 * @returns false if the default access policy should be applied.
 *
 * @param   pObj        The object in question.
 * @param   pSession    The session wanting to access the object.
 * @param   pszObjName  The object name, can be NULL.
 * @param   prc         Where to store the result when returning true.
 */
bool VBOXCALL   supdrvOSObjCanAccess(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession, const char *pszObjName, int *prc)
{
    NOREF(pObj);
    NOREF(pSession);
    NOREF(pszObjName);
    NOREF(prc);
    return false;
}


bool VBOXCALL  supdrvOSGetForcedAsyncTscMode(PSUPDRVDEVEXT pDevExt)
{
    return false;
}


bool VBOXCALL  supdrvOSAreCpusOfflinedOnSuspend(void)
{
    /** @todo verify this. */
    return false;
}


bool VBOXCALL  supdrvOSAreTscDeltasInSync(void)
{
    return false;
}


#if  defined(VBOX_WITH_NATIVE_SOLARIS_LOADING) \
 && !defined(VBOX_WITHOUT_NATIVE_R0_LOADER)

int  VBOXCALL   supdrvOSLdrOpen(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const char *pszFilename)
{
    pImage->idSolMod   = -1;
    pImage->pSolModCtl = NULL;

# if 1 /* This approach requires _init/_fini/_info stubs. */
    /*
     * Construct a filename that escapes the module search path and let us
     * specify a root path.
     */
    /** @todo change this to use modctl and use_path=0. */
    const char *pszName = RTPathFilename(pszFilename);
    AssertReturn(pszName, VERR_INVALID_PARAMETER);
    char *pszSubDir = RTStrAPrintf2("../../../../../../../../../../..%.*s", pszName - pszFilename - 1, pszFilename);
    if (!pszSubDir)
        return VERR_NO_STR_MEMORY;
    int idMod = modload(pszSubDir, pszName);
    if (idMod == -1)
    {
        /* This is an horrible hack for avoiding the mod-present check in
           modrload on S10.  Fortunately, nobody else seems to be using that
           variable... */
        extern int swaploaded;
        int saved_swaploaded = swaploaded;
        swaploaded = 0;
        idMod = modload(pszSubDir, pszName);
        swaploaded = saved_swaploaded;
    }
    RTStrFree(pszSubDir);
    if (idMod == -1)
    {
        LogRel(("modload(,%s): failed, could be anything...\n", pszFilename));
        return VERR_LDR_GENERAL_FAILURE;
    }

    modctl_t *pModCtl = mod_hold_by_id(idMod);
    if (!pModCtl)
    {
        LogRel(("mod_hold_by_id(,%s): failed, weird.\n", pszFilename));
        /* No point in calling modunload. */
        return VERR_LDR_GENERAL_FAILURE;
    }
    pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD | MOD_NOUNLOAD; /* paranoia */

# else

    const int idMod = -1;
    modctl_t *pModCtl = mod_hold_by_name(pszFilename);
    if (!pModCtl)
    {
        LogRel(("mod_hold_by_name failed for '%s'\n", pszFilename));
        return VERR_LDR_GENERAL_FAILURE;
    }

    int rc = kobj_load_module(pModCtl, 0 /*use_path*/);
    if (rc != 0)
    {
        LogRel(("kobj_load_module failed with rc=%d for '%s'\n", rc, pszFilename));
        mod_release_mod(pModCtl);
        return RTErrConvertFromErrno(rc);
    }
# endif

    /*
     * Get the module info.
     *
     * Note! The text section is actually not at mi_base, but and the next
     *       alignment boundrary and there seems to be no easy way of
     *       getting at this address.  This sabotages supdrvOSLdrLoad.
     *       Bastards!
     */
    struct modinfo ModInfo;
    kobj_getmodinfo(pModCtl->mod_mp, &ModInfo);
    pImage->pvImage    = ModInfo.mi_base;
    pImage->idSolMod   = idMod;
    pImage->pSolModCtl = pModCtl;

    mod_release_mod(pImage->pSolModCtl);
    LogRel(("supdrvOSLdrOpen: succeeded for '%s' (mi_base=%p mi_size=%#x), id=%d ctl=%p\n",
            pszFilename, ModInfo.mi_base, ModInfo.mi_size, idMod, pModCtl));
    return VINF_SUCCESS;
}


int  VBOXCALL   supdrvOSLdrValidatePointer(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, void *pv,
                                           const uint8_t *pbImageBits, const char *pszSymbol)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pv); NOREF(pbImageBits); NOREF(pszSymbol);
    if (kobj_addrcheck(pImage->pSolModCtl->mod_mp, pv))
        return VERR_INVALID_PARAMETER;
    return VINF_SUCCESS;
}


/**
 * Resolves a module entry point address.
 *
 * @returns VBox status code.
 * @param   pImage              The image.
 * @param   pszSymbol           The symbol name.
 * @param   ppvValue            Where to store the value.  On input this holds
 *                              the symbol value SUPLib calculated.
 */
static int supdrvSolLdrResolvEp(PSUPDRVLDRIMAGE pImage, const char *pszSymbol, void **ppvValue)
{
    /* Don't try resolve symbols which, according to SUPLib, aren't there. */
    if (!*ppvValue)
        return VINF_SUCCESS;

    uintptr_t uValue = modlookup_by_modctl(pImage->pSolModCtl, pszSymbol);
    if (!uValue)
    {
        LogRel(("supdrvOSLdrLoad on %s failed to resolve %s\n", pImage->szName, pszSymbol));
        return VERR_SYMBOL_NOT_FOUND;
    }
    *ppvValue = (void *)uValue;
    return VINF_SUCCESS;
}


int  VBOXCALL   supdrvOSLdrLoad(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const uint8_t *pbImageBits, PSUPLDRLOAD pReq)
{
#if 0 /* This doesn't work because of text alignment. */
    /*
     * Comparing is very very difficult since text and data may be allocated
     * separately.
     */
    size_t cbCompare = RT_MIN(pImage->cbImageBits, 64);
    if (memcmp(pImage->pvImage, pbImageBits, cbCompare))
    {
        LogRel(("Image mismatch: %s (%p)\n", pImage->szName, pImage->pvImage));
        LogRel(("Native: %.*Rhxs\n", cbCompare, pImage->pvImage));
        LogRel(("SUPLib: %.*Rhxs\n", cbCompare, pbImageBits));
        return VERR_LDR_MISMATCH_NATIVE;
    }
#endif

    /*
     * Get the exported symbol addresses.
     */
    int rc;
    modctl_t *pModCtl = mod_hold_by_id(pImage->idSolMod);
    if (pModCtl && pModCtl == pImage->pSolModCtl)
    {
        uint32_t iSym = pImage->cSymbols;
        while (iSym-- > 0)
        {
            const char *pszSymbol = &pImage->pachStrTab[pImage->paSymbols[iSym].offName];
            uintptr_t uValue = modlookup_by_modctl(pImage->pSolModCtl, pszSymbol);
            if (!uValue)
            {
                LogRel(("supdrvOSLdrLoad on %s failed to resolve the exported symbol: '%s'\n", pImage->szName, pszSymbol));
                break;
            }
            uintptr_t offSymbol = uValue - (uintptr_t)pImage->pvImage;
            pImage->paSymbols[iSym].offSymbol = offSymbol;
            if (pImage->paSymbols[iSym].offSymbol != (int32_t)offSymbol)
            {
                LogRel(("supdrvOSLdrLoad on %s symbol out of range: %p (%s) \n", pImage->szName, offSymbol, pszSymbol));
                break;
            }
        }

        rc = iSym == UINT32_MAX ? VINF_SUCCESS : VERR_LDR_GENERAL_FAILURE;

        /*
         * Get the standard module entry points.
         */
        if (RT_SUCCESS(rc))
        {
            rc = supdrvSolLdrResolvEp(pImage, "ModuleInit", (void **)&pImage->pfnModuleInit);
            if (RT_SUCCESS(rc))
                rc = supdrvSolLdrResolvEp(pImage, "ModuleTerm", (void **)&pImage->pfnModuleTerm);

            switch (pReq->u.In.eEPType)
            {
                case SUPLDRLOADEP_VMMR0:
                {
                    if (RT_SUCCESS(rc))
                        rc = supdrvSolLdrResolvEp(pImage, "VMMR0EntryFast", (void **)&pReq->u.In.EP.VMMR0.pvVMMR0EntryFast);
                    if (RT_SUCCESS(rc))
                        rc = supdrvSolLdrResolvEp(pImage, "VMMR0EntryEx",   (void **)&pReq->u.In.EP.VMMR0.pvVMMR0EntryEx);
                    break;
                }

                case SUPLDRLOADEP_SERVICE:
                {
                    /** @todo we need the name of the entry point. */
                    return VERR_NOT_SUPPORTED;
                }
            }
        }

        mod_release_mod(pImage->pSolModCtl);
    }
    else
    {
        LogRel(("mod_hold_by_id failed in supdrvOSLdrLoad on %s: %p\n", pImage->szName, pModCtl));
        rc = VERR_LDR_MISMATCH_NATIVE;
    }
    return rc;
}


void VBOXCALL   supdrvOSLdrUnload(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
# if 1
    pImage->pSolModCtl->mod_loadflags &= ~MOD_NOUNLOAD;
    int rc = modunload(pImage->idSolMod);
    if (rc)
        LogRel(("modunload(%u (%s)) failed: %d\n", pImage->idSolMod, pImage->szName, rc));
# else
    kobj_unload_module(pImage->pSolModCtl);
# endif
    pImage->pSolModCtl = NULL;
    pImage->idSolMod   = NULL;
}

#else /* !VBOX_WITH_NATIVE_SOLARIS_LOADING */

int  VBOXCALL   supdrvOSLdrOpen(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const char *pszFilename)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pszFilename);
    return VERR_NOT_SUPPORTED;
}


int  VBOXCALL   supdrvOSLdrValidatePointer(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, void *pv,
                                           const uint8_t *pbImageBits, const char *pszSymbol)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pv); NOREF(pbImageBits); NOREF(pszSymbol);
    return VERR_NOT_SUPPORTED;
}


int  VBOXCALL   supdrvOSLdrLoad(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const uint8_t *pbImageBits, PSUPLDRLOAD pReq)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pbImageBits); NOREF(pReq);
    return VERR_NOT_SUPPORTED;
}


void VBOXCALL   supdrvOSLdrUnload(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    NOREF(pDevExt); NOREF(pImage);
}

#endif /* !VBOX_WITH_NATIVE_SOLARIS_LOADING */


void VBOXCALL   supdrvOSLdrNotifyOpened(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const char *pszFilename)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pszFilename);
}


void VBOXCALL   supdrvOSLdrNotifyUnloaded(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    NOREF(pDevExt); NOREF(pImage);
}


int  VBOXCALL   supdrvOSLdrQuerySymbol(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage,
                                       const char *pszSymbol, size_t cchSymbol, void **ppvSymbol)
{
    RT_NOREF(pDevExt, pImage, pszSymbol, cchSymbol, ppvSymbol);
    return VERR_WRONG_ORDER;
}


void VBOXCALL   supdrvOSLdrRetainWrapperModule(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    RT_NOREF(pDevExt, pImage);
    AssertFailed();
}


void VBOXCALL   supdrvOSLdrReleaseWrapperModule(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    RT_NOREF(pDevExt, pImage);
    AssertFailed();
}

#ifdef SUPDRV_WITH_MSR_PROBER

int VBOXCALL    supdrvOSMsrProberRead(uint32_t uMsr, RTCPUID idCpu, uint64_t *puValue)
{
/** @todo cmi_hdl_rdmsr can safely do this. there is also the on_trap() fun
 *        for catching traps that could possibly be used directly. */
    NOREF(uMsr); NOREF(idCpu); NOREF(puValue);
    return VERR_NOT_SUPPORTED;
}


int VBOXCALL    supdrvOSMsrProberWrite(uint32_t uMsr, RTCPUID idCpu, uint64_t uValue)
{
/** @todo cmi_hdl_wrmsr can safely do this. */
    NOREF(uMsr); NOREF(idCpu); NOREF(uValue);
    return VERR_NOT_SUPPORTED;
}


int VBOXCALL    supdrvOSMsrProberModify(RTCPUID idCpu, PSUPMSRPROBER pReq)
{
    NOREF(idCpu); NOREF(pReq);
    return VERR_NOT_SUPPORTED;
}

#endif /* SUPDRV_WITH_MSR_PROBER */


SUPR0DECL(int) SUPR0HCPhysToVirt(RTHCPHYS HCPhys, void **ppv)
{
    AssertReturn(!(HCPhys & PAGE_OFFSET_MASK), VERR_INVALID_POINTER);
    AssertReturn(HCPhys != NIL_RTHCPHYS, VERR_INVALID_POINTER);
    HCPhys >>= PAGE_SHIFT;
    AssertReturn(HCPhys <= physmax, VERR_INVALID_POINTER);
    *ppv = hat_kpm_pfn2va(HCPhys);
    return VINF_SUCCESS;
}


RTDECL(int) SUPR0PrintfV(const char *pszFormat, va_list va)
{
    /* cmn_err() acquires adaptive mutexes. Not preemption safe, see @bugref{6657}. */
    if (RTThreadPreemptIsEnabled(NIL_RTTHREAD))
    {
        char szMsg[512];
        RTStrPrintfV(szMsg, sizeof(szMsg) - 1, pszFormat, va);
        szMsg[sizeof(szMsg) - 1] = '\0';

        cmn_err(CE_CONT, "%s", szMsg);
    }
    return 0;
}


SUPR0DECL(uint32_t) SUPR0GetKernelFeatures(void)
{
    return 0;
}


SUPR0DECL(bool) SUPR0FpuBegin(bool fCtxHook)
{
    RT_NOREF(fCtxHook);
    return false;
}


SUPR0DECL(void) SUPR0FpuEnd(bool fCtxHook)
{
    RT_NOREF(fCtxHook);
}

