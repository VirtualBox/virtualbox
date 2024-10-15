/* $Id: VBoxGuest-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Guest Additions Driver for Solaris.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/mutex.h>
#include <sys/pci.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/ddi_intr.h>
#include <sys/sunddi.h>
#include <sys/open.h>
#include <sys/sunldi.h>
#include <sys/policy.h>
#include <sys/file.h>
#undef u /* /usr/include/sys/user.h:249:1 is where this is defined to (curproc->p_user). very cool. */

#include "VBoxGuestInternal.h"
#include <VBox/log.h>
#include <VBox/version.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/process.h>
#include <iprt/mem.h>
#include <iprt/cdefs.h>
#include <iprt/asm.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The module name. */
#define DEVICE_NAME              "vboxguest"
/** The module description as seen in 'modinfo'. */
#define DEVICE_DESC              "VirtualBox GstDrv"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int vgdrvSolarisOpen(dev_t *pDev, int fFlag, int fType, cred_t *pCred);
static int vgdrvSolarisClose(dev_t Dev, int fFlag, int fType, cred_t *pCred);
static int vgdrvSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred);
static int vgdrvSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred);
static int vgdrvSolarisIOCtl(dev_t Dev, int iCmd, intptr_t pArg, int Mode, cred_t *pCred, int *pVal);
static int vgdrvSolarisIOCtlSlow(PVBOXGUESTSESSION pSession, int iCmd, int Mode, intptr_t iArgs);
static int vgdrvSolarisPoll(dev_t Dev, short fEvents, int fAnyYet, short *pReqEvents, struct pollhead **ppPollHead);

static int vgdrvSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pArg, void **ppResult);
static int vgdrvSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd);
static int vgdrvSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd);
static int vgdrvSolarisQuiesce(dev_info_t *pDip);

static int vgdrvSolarisAddIRQ(dev_info_t *pDip);
static void vgdrvSolarisRemoveIRQ(dev_info_t *pDip);
static uint_t vgdrvSolarisHighLevelISR(caddr_t Arg);
static uint_t vgdrvSolarisISR(caddr_t Arg);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * cb_ops: for drivers that support char/block entry points
 */
static struct cb_ops g_vgdrvSolarisCbOps =
{
    vgdrvSolarisOpen,
    vgdrvSolarisClose,
    nodev,                  /* b strategy */
    nodev,                  /* b dump */
    nodev,                  /* b print */
    vgdrvSolarisRead,
    vgdrvSolarisWrite,
    vgdrvSolarisIOCtl,
    nodev,                  /* c devmap */
    nodev,                  /* c mmap */
    nodev,                  /* c segmap */
    vgdrvSolarisPoll,
    ddi_prop_op,            /* property ops */
    NULL,                   /* streamtab  */
    D_NEW | D_MP,           /* compat. flag */
    CB_REV                  /* revision */
};

/**
 * dev_ops: for driver device operations
 */
static struct dev_ops g_vgdrvSolarisDevOps =
{
    DEVO_REV,               /* driver build revision */
    0,                      /* ref count */
    vgdrvSolarisGetInfo,
    nulldev,                /* identify */
    nulldev,                /* probe */
    vgdrvSolarisAttach,
    vgdrvSolarisDetach,
    nodev,                  /* reset */
    &g_vgdrvSolarisCbOps,
    (struct bus_ops *)0,
    nodev,                  /* power */
    vgdrvSolarisQuiesce
};

/**
 * modldrv: export driver specifics to the kernel
 */
static struct modldrv g_vgdrvSolarisModule =
{
    &mod_driverops,         /* extern from kernel */
    DEVICE_DESC " " VBOX_VERSION_STRING "r" RT_XSTR(VBOX_SVN_REV),
    &g_vgdrvSolarisDevOps
};

/**
 * modlinkage: export install/remove/info to the kernel
 */
static struct modlinkage g_vgdrvSolarisModLinkage =
{
    MODREV_1,               /* loadable module system revision */
    &g_vgdrvSolarisModule,
    NULL                    /* terminate array of linkage structures */
};

/**
 * State info for each open file handle.
 */
typedef struct
{
    /** Pointer to the session handle. */
    PVBOXGUESTSESSION       pSession;
    /** The process reference for posting signals */
    void                   *pvProcRef;
} vboxguest_state_t;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Device handle (we support only one instance). */
static dev_info_t          *g_pDip = NULL;
/** Opaque pointer to file-descriptor states */
static void                *g_pvgdrvSolarisState = NULL;
/** Device extention & session data association structure. */
static VBOXGUESTDEVEXT      g_DevExt;
/** IO port handle. */
static ddi_acc_handle_t     g_PciIOHandle;
/** MMIO handle. */
static ddi_acc_handle_t     g_PciMMIOHandle;
/** IO Port. */
static uint16_t             g_uIOPortBase;
/** Address of the MMIO region.*/
static caddr_t              g_pMMIOBase;
/** Size of the MMIO region. */
static off_t                g_cbMMIO;
/** Pointer to an array of interrupt handles. */
static ddi_intr_handle_t   *g_pahIntrs;
/** Handle to the soft interrupt. */
static ddi_softint_handle_t g_hSoftIntr;
/** The pollhead structure */
static pollhead_t           g_PollHead;
/** The IRQ Mutex */
static kmutex_t             g_IrqMtx;
/** The IRQ high-level Mutex. */
static kmutex_t             g_HighLevelIrqMtx;
/** Whether soft-ints are setup. */
static bool                 g_fSoftIntRegistered = false;

/** Additional IPRT function we need to drag in for vboxfs. */
PFNRT g_Deps[] =
{
    (PFNRT)RTErrConvertToErrno,
};


/**
 * Kernel entry points
 */
int _init(void)
{
    /*
     * Initialize IPRT R0 driver, which internally calls OS-specific r0 init.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        PRTLOGGER pRelLogger;
        static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
        rc = RTLogCreate(&pRelLogger, 0 /* fFlags */, "all",
                         "VBOX_RELEASE_LOG", RT_ELEMENTS(s_apszGroups), s_apszGroups,
                         RTLOGDEST_STDOUT | RTLOGDEST_DEBUGGER, NULL);
        if (RT_SUCCESS(rc))
            RTLogRelSetDefaultInstance(pRelLogger);
        else
            cmn_err(CE_NOTE, "failed to initialize driver logging rc=%d!\n", rc);

        /*
         * Prevent module autounloading.
         */
        modctl_t *pModCtl = mod_getctl(&g_vgdrvSolarisModLinkage);
        if (pModCtl)
            pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD;
        else
            LogRel((DEVICE_NAME ": failed to disable autounloading!\n"));

        rc = ddi_soft_state_init(&g_pvgdrvSolarisState, sizeof(vboxguest_state_t), 1);
        if (!rc)
        {
            rc = mod_install(&g_vgdrvSolarisModLinkage);
            if (rc)
                ddi_soft_state_fini(&g_pvgdrvSolarisState);
        }
    }
    else
    {
        cmn_err(CE_NOTE, "_init: RTR0Init failed. rc=%d\n", rc);
        return EINVAL;
    }

    return rc;
}


int _fini(void)
{
    LogFlow((DEVICE_NAME ":_fini\n"));
    int rc = mod_remove(&g_vgdrvSolarisModLinkage);
    if (!rc)
        ddi_soft_state_fini(&g_pvgdrvSolarisState);

    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
    RTLogDestroy(RTLogSetDefaultInstance(NULL));

    if (!rc)
        RTR0Term();
    return rc;
}


int _info(struct modinfo *pModInfo)
{
    /* LogFlow((DEVICE_NAME ":_info\n")); - Called too early, causing RTThreadPreemtIsEnabled warning. */
    return mod_info(&g_vgdrvSolarisModLinkage, pModInfo);
}


/**
 * Attach entry point, to attach a device to the system or resume it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Attach type (ddi_attach_cmd_t)
 *
 * @return  corresponding solaris error code.
 */
static int vgdrvSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd)
{
    LogFlow(("vgdrvSolarisAttach:\n"));
    switch (enmCmd)
    {
        case DDI_ATTACH:
        {
            if (g_pDip)
            {
                LogRel(("vgdrvSolarisAttach: Only one instance supported.\n"));
                return DDI_FAILURE;
            }

            /*
             * Enable resources for PCI access.
             */
            ddi_acc_handle_t PciHandle;
            int rc = pci_config_setup(pDip, &PciHandle);
            if (rc == DDI_SUCCESS)
            {
                /*
                 * Map the register address space.
                 */
                caddr_t baseAddr;
                ddi_device_acc_attr_t deviceAttr;
                deviceAttr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
                deviceAttr.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC;
                deviceAttr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
                deviceAttr.devacc_attr_access = DDI_DEFAULT_ACC;
                rc = ddi_regs_map_setup(pDip, 1, &baseAddr, 0, 0, &deviceAttr, &g_PciIOHandle);
                if (rc == DDI_SUCCESS)
                {
                    /*
                     * Read size of the MMIO region.
                     */
                    g_uIOPortBase = (uintptr_t)baseAddr;
                    rc = ddi_dev_regsize(pDip, 2, &g_cbMMIO);
                    if (rc == DDI_SUCCESS)
                    {
                        rc = ddi_regs_map_setup(pDip, 2, &g_pMMIOBase, 0, g_cbMMIO, &deviceAttr, &g_PciMMIOHandle);
                        if (rc == DDI_SUCCESS)
                        {
                            /*
                             * Call the common device extension initializer.
                             */
                            rc = VGDrvCommonInitDevExt(&g_DevExt, g_uIOPortBase,
                                                       NULL /*pvMmioReq*/,
                                                       g_pMMIOBase, g_cbMMIO,
#if ARCH_BITS == 64
                                                       VBOXOSTYPE_Solaris_x64,
#else
                                                       VBOXOSTYPE_Solaris,
#endif
                                                       VMMDEV_EVENT_MOUSE_POSITION_CHANGED);
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * Add IRQ of VMMDev.
                                 */
                                rc = vgdrvSolarisAddIRQ(pDip);
                                if (rc == DDI_SUCCESS)
                                {
                                    /*
                                     * Read host configuration.
                                     */
                                    VGDrvCommonProcessOptionsFromHost(&g_DevExt);

                                    rc = ddi_create_minor_node(pDip, DEVICE_NAME, S_IFCHR, 0 /* instance */, DDI_PSEUDO,
                                                               0 /* fFlags */);
                                    if (rc == DDI_SUCCESS)
                                    {
                                        g_pDip = pDip;
                                        pci_config_teardown(&PciHandle);
                                        return DDI_SUCCESS;
                                    }

                                    LogRel((DEVICE_NAME "::Attach: ddi_create_minor_node failed.\n"));
                                    vgdrvSolarisRemoveIRQ(pDip);
                                }
                                else
                                    LogRel((DEVICE_NAME "::Attach: vgdrvSolarisAddIRQ failed.\n"));
                                VGDrvCommonDeleteDevExt(&g_DevExt);
                            }
                            else
                                LogRel((DEVICE_NAME "::Attach: VGDrvCommonInitDevExt failed.\n"));
                            ddi_regs_map_free(&g_PciMMIOHandle);
                        }
                        else
                            LogRel((DEVICE_NAME "::Attach: ddi_regs_map_setup for MMIO region failed.\n"));
                    }
                    else
                        LogRel((DEVICE_NAME "::Attach: ddi_dev_regsize for MMIO region failed.\n"));
                    ddi_regs_map_free(&g_PciIOHandle);
                }
                else
                    LogRel((DEVICE_NAME "::Attach: ddi_regs_map_setup for IOport failed.\n"));
                pci_config_teardown(&PciHandle);
            }
            else
                LogRel((DEVICE_NAME "::Attach: pci_config_setup failed rc=%d.\n", rc));
            return DDI_FAILURE;
        }

        case DDI_RESUME:
        {
            /** @todo implement resume for guest driver. */
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
 * @return  corresponding solaris error code.
 */
static int vgdrvSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    LogFlow(("vgdrvSolarisDetach:\n"));
    switch (enmCmd)
    {
        case DDI_DETACH:
        {
            vgdrvSolarisRemoveIRQ(pDip);
            ddi_regs_map_free(&g_PciIOHandle);
            ddi_regs_map_free(&g_PciMMIOHandle);
            ddi_remove_minor_node(pDip, NULL);
            VGDrvCommonDeleteDevExt(&g_DevExt);
            g_pDip = NULL;
            return DDI_SUCCESS;
        }

        case DDI_SUSPEND:
        {
            /** @todo implement suspend for guest driver. */
            return DDI_SUCCESS;
        }

        default:
            return DDI_FAILURE;
    }
}


/**
 * Quiesce entry point, called by solaris kernel for disabling the device from
 * generating any interrupts or doing in-bound DMA.
 *
 * @param   pDip            The module structure instance.
 *
 * @return  corresponding solaris error code.
 */
static int vgdrvSolarisQuiesce(dev_info_t *pDip)
{
    int rc = ddi_intr_disable(g_pahIntrs[0]);
    if (rc != DDI_SUCCESS)
        return DDI_FAILURE;

    /** @todo What about HGCM/HGSMI touching guest-memory? */

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
 * @return  corresponding solaris error code.
 */
static int vgdrvSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pvArg, void **ppvResult)
{
    LogFlow(("vgdrvSolarisGetInfo:\n"));

    int rc = DDI_SUCCESS;
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

    NOREF(pvArg);
    return rc;
}


/**
 * User context entry points
 *
 * @remarks fFlags are the flags passed to open() or to ldi_open_by_name.  In
 *          the latter case the FKLYR flag is added to indicate that the caller
 *          is a kernel component rather than user land.
 */
static int vgdrvSolarisOpen(dev_t *pDev, int fFlags, int fType, cred_t *pCred)
{
    int                 rc;
    PVBOXGUESTSESSION   pSession = NULL;

    LogFlow(("vgdrvSolarisOpen:\n"));

    /*
     * Verify we are being opened as a character device.
     */
    if (fType != OTYP_CHR)
        return EINVAL;

    vboxguest_state_t *pState = NULL;
    unsigned iOpenInstance;
    for (iOpenInstance = 0; iOpenInstance < 4096; iOpenInstance++)
    {
        if (    !ddi_get_soft_state(g_pvgdrvSolarisState, iOpenInstance) /* faster */
            &&  ddi_soft_state_zalloc(g_pvgdrvSolarisState, iOpenInstance) == DDI_SUCCESS)
        {
            pState = ddi_get_soft_state(g_pvgdrvSolarisState, iOpenInstance);
            break;
        }
    }
    if (!pState)
    {
        Log(("vgdrvSolarisOpen: too many open instances."));
        return ENXIO;
    }

    /*
     * Create a new session.
     *
     * Note! The devfs inode with the gid isn't readily available here, so we cannot easily
     *       to the vbox group detection like on linux.  Read config instead?
     */
    if (!(fFlags & FKLYR))
    {
        uint32_t fRequestor = VMMDEV_REQUESTOR_USERMODE | VMMDEV_REQUESTOR_TRUST_NOT_GIVEN;
        if (crgetruid(pCred) == 0)
            fRequestor |= VMMDEV_REQUESTOR_USR_ROOT;
        else
            fRequestor |= VMMDEV_REQUESTOR_USR_USER;
        if (secpolicy_coreadm(pCred) == 0)
            fRequestor |= VMMDEV_REQUESTOR_GRP_WHEEL;
        /** @todo is there any way of detecting that the process belongs to someone on the physical console?
         * secpolicy_console() [== PRIV_SYS_DEVICES] doesn't look quite right, or does it? */
        fRequestor |= VMMDEV_REQUESTOR_CON_DONT_KNOW;
        fRequestor |= VMMDEV_REQUESTOR_NO_USER_DEVICE; /** @todo implement vboxuser device node. */

        rc = VGDrvCommonCreateUserSession(&g_DevExt, fRequestor, &pSession);
    }
    else
        rc = VGDrvCommonCreateKernelSession(&g_DevExt, &pSession);
    if (RT_SUCCESS(rc))
    {
        if (!(fFlags & FKLYR))
            pState->pvProcRef = proc_ref();
        else
            pState->pvProcRef = NULL;
        pState->pSession = pSession;
        *pDev = makedevice(getmajor(*pDev), iOpenInstance);
        Log(("vgdrvSolarisOpen: pSession=%p pState=%p pid=%d\n", pSession, pState, (int)RTProcSelf()));
        return 0;
    }

    /* Failed, clean up. */
    ddi_soft_state_free(g_pvgdrvSolarisState, iOpenInstance);

    LogRel((DEVICE_NAME "::Open: VGDrvCommonCreateUserSession failed. rc=%d\n", rc));
    return EFAULT;
}


static int vgdrvSolarisClose(dev_t Dev, int flag, int fType, cred_t *pCred)
{
    LogFlow(("vgdrvSolarisClose: pid=%d\n", (int)RTProcSelf()));

    PVBOXGUESTSESSION pSession = NULL;
    vboxguest_state_t *pState = ddi_get_soft_state(g_pvgdrvSolarisState, getminor(Dev));
    if (!pState)
    {
        Log(("vgdrvSolarisClose: failed to get pState.\n"));
        return EFAULT;
    }

    if (pState->pvProcRef != NULL)
    {
        proc_unref(pState->pvProcRef);
        pState->pvProcRef = NULL;
    }
    pSession = pState->pSession;
    pState->pSession = NULL;
    Log(("vgdrvSolarisClose: pSession=%p pState=%p\n", pSession, pState));
    ddi_soft_state_free(g_pvgdrvSolarisState, getminor(Dev));
    if (!pSession)
    {
        Log(("vgdrvSolarisClose: failed to get pSession.\n"));
        return EFAULT;
    }

    /*
     * Close the session.
     */
    if (pSession)
        VGDrvCommonCloseSession(&g_DevExt, pSession);
    return 0;
}


static int vgdrvSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    LogFlow((DEVICE_NAME "::Read\n"));

    vboxguest_state_t *pState = ddi_get_soft_state(g_pvgdrvSolarisState, getminor(Dev));
    if (!pState)
    {
        Log((DEVICE_NAME "::Close: failed to get pState.\n"));
        return EFAULT;
    }

    PVBOXGUESTSESSION pSession = pState->pSession;
    uint32_t u32CurSeq = ASMAtomicUoReadU32(&g_DevExt.u32MousePosChangedSeq);
    if (pSession->u32MousePosChangedSeq != u32CurSeq)
        pSession->u32MousePosChangedSeq = u32CurSeq;

    return 0;
}


static int vgdrvSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    LogFlow(("vgdrvSolarisWrite:\n"));
    return 0;
}


/** @def IOCPARM_LEN
 * Gets the length from the ioctl number.
 * This is normally defined by sys/ioccom.h on BSD systems...
 */
#ifndef IOCPARM_LEN
# define IOCPARM_LEN(x)     ( ((x) >> 16) & IOCPARM_MASK )
#endif


/**
 * Driver ioctl, an alternate entry point for this character driver.
 *
 * @param   Dev             Device number
 * @param   iCmd            Operation identifier
 * @param   iArgs           Arguments from user to driver
 * @param   Mode            Information bitfield (read/write, address space etc.)
 * @param   pCred           User credentials
 * @param   pVal            Return value for calling process.
 *
 * @return  corresponding solaris error code.
 */
static int vgdrvSolarisIOCtl(dev_t Dev, int iCmd, intptr_t iArgs, int Mode, cred_t *pCred, int *pVal)
{
    /*
     * Get the session from the soft state item.
     */
    vboxguest_state_t *pState = ddi_get_soft_state(g_pvgdrvSolarisState, getminor(Dev));
    if (!pState)
    {
        LogRel(("vgdrvSolarisIOCtl: no state data for %#x (%d)\n", Dev, getminor(Dev)));
        return EINVAL;
    }

    PVBOXGUESTSESSION pSession = pState->pSession;
    if (!pSession)
    {
        LogRel(("vgdrvSolarisIOCtl: no session in state data for %#x (%d)\n", Dev, getminor(Dev)));
        return DDI_SUCCESS;
    }

    /*
     * Deal with fast requests.
     */
    if (VBGL_IOCTL_IS_FAST(iCmd))
    {
        *pVal = VGDrvCommonIoCtlFast(iCmd, &g_DevExt, pSession);
        return 0;
    }

    /*
     * It's kind of simple if this is a kernel session, take slow path if user land.
     */
    if (pSession->R0Process == NIL_RTR0PROCESS)
    {
        if (IOCPARM_LEN(iCmd) == sizeof(VBGLREQHDR))
        {
            PVBGLREQHDR pHdr = (PVBGLREQHDR)iArgs;
            int rc;
            if (iCmd != VBGL_IOCTL_IDC_DISCONNECT)
                rc =VGDrvCommonIoCtl(iCmd, &g_DevExt, pSession, pHdr, RT_MAX(pHdr->cbIn, pHdr->cbOut));
            else
            {
                pState->pSession = NULL;
                rc = VGDrvCommonIoCtl(iCmd, &g_DevExt, pSession, pHdr, RT_MAX(pHdr->cbIn, pHdr->cbOut));
                if (RT_FAILURE(rc))
                    pState->pSession = pSession;
            }
            return rc;
        }
    }

    return vgdrvSolarisIOCtlSlow(pSession, iCmd, Mode, iArgs);
}


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
static int vgdrvSolarisIOCtlSlow(PVBOXGUESTSESSION pSession, int iCmd, int Mode, intptr_t iArg)
{
    int         rc;
    uint32_t    cbBuf = 0;
    union
    {
        VBGLREQHDR  Hdr;
        uint8_t     abBuf[64];
    }           StackBuf;
    PVBGLREQHDR  pHdr;


    /*
     * Read the header.
     */
    if (RT_UNLIKELY(IOCPARM_LEN(iCmd) != sizeof(StackBuf.Hdr)))
    {
        LogRel(("vgdrvSolarisIOCtlSlow: iCmd=%#x len %d expected %d\n", iCmd, IOCPARM_LEN(iCmd), sizeof(StackBuf.Hdr)));
        return EINVAL;
    }
    rc = ddi_copyin((void *)iArg, &StackBuf.Hdr, sizeof(StackBuf.Hdr), Mode);
    if (RT_UNLIKELY(rc))
    {
        LogRel(("vgdrvSolarisIOCtlSlow: ddi_copyin(,%#lx,) failed; iCmd=%#x. rc=%d\n", iArg, iCmd, rc));
        return EFAULT;
    }
    if (RT_UNLIKELY(StackBuf.Hdr.uVersion != VBGLREQHDR_VERSION))
    {
        LogRel(("vgdrvSolarisIOCtlSlow: bad header version %#x; iCmd=%#x\n", StackBuf.Hdr.uVersion, iCmd));
        return EINVAL;
    }
    cbBuf = RT_MAX(StackBuf.Hdr.cbIn, StackBuf.Hdr.cbOut);
    if (RT_UNLIKELY(   StackBuf.Hdr.cbIn < sizeof(StackBuf.Hdr)
                    || (StackBuf.Hdr.cbOut < sizeof(StackBuf.Hdr) && StackBuf.Hdr.cbOut != 0)
                    || cbBuf > _1M*16))
    {
        LogRel(("vgdrvSolarisIOCtlSlow: max(%#x,%#x); iCmd=%#x\n", StackBuf.Hdr.cbIn, StackBuf.Hdr.cbOut, iCmd));
        return EINVAL;
    }

    /*
     * Buffer the request.
     *
     * Note! Common code revalidates the header sizes and version. So it's
     *       fine to read it once more.
     */
    if (cbBuf <= sizeof(StackBuf))
        pHdr = &StackBuf.Hdr;
    else
    {
        pHdr = RTMemTmpAlloc(cbBuf);
        if (RT_UNLIKELY(!pHdr))
        {
            LogRel(("vgdrvSolarisIOCtlSlow: failed to allocate buffer of %d bytes for iCmd=%#x.\n", cbBuf, iCmd));
            return ENOMEM;
        }
    }
    rc = ddi_copyin((void *)iArg, pHdr, cbBuf, Mode);
    if (RT_UNLIKELY(rc))
    {
        LogRel(("vgdrvSolarisIOCtlSlow: copy_from_user(,%#lx, %#x) failed; iCmd=%#x. rc=%d\n", iArg, cbBuf, iCmd, rc));
        if (pHdr != &StackBuf.Hdr)
            RTMemFree(pHdr);
        return EFAULT;
    }

    /*
     * Process the IOCtl.
     */
    rc = VGDrvCommonIoCtl(iCmd, &g_DevExt, pSession, pHdr, cbBuf);

    /*
     * Copy ioctl data and output buffer back to user space.
     */
    if (RT_SUCCESS(rc))
    {
        uint32_t cbOut = pHdr->cbOut;
        if (RT_UNLIKELY(cbOut > cbBuf))
        {
            LogRel(("vgdrvSolarisIOCtlSlow: too much output! %#x > %#x; iCmd=%#x!\n", cbOut, cbBuf, iCmd));
            cbOut = cbBuf;
        }
        rc = ddi_copyout(pHdr, (void *)iArg, cbOut, Mode);
        if (RT_UNLIKELY(rc != 0))
        {
            /* this is really bad */
            LogRel(("vgdrvSolarisIOCtlSlow: ddi_copyout(,%p,%d) failed. rc=%d\n", (void *)iArg, cbBuf, rc));
            rc = EFAULT;
        }
    }
    else
        rc = EINVAL;

    if (pHdr != &StackBuf.Hdr)
        RTMemTmpFree(pHdr);
    return rc;
}


#if 0
/**
 * @note This code is duplicated on other platforms with variations, so please
 *       keep them all up to date when making changes!
 */
int VBOXCALL VBoxGuestIDC(void *pvSession, uintptr_t uReq, PVBGLREQHDR pReqHdr, size_t cbReq)
{
    /*
     * Simple request validation (common code does the rest).
     */
    int rc;
    if (   RT_VALID_PTR(pReqHdr)
        && cbReq >= sizeof(*pReqHdr))
    {
        /*
         * All requests except the connect one requires a valid session.
         */
        PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)pvSession;
        if (pSession)
        {
            if (   RT_VALID_PTR(pSession)
                && pSession->pDevExt == &g_DevExt)
                rc = VGDrvCommonIoCtl(uReq, &g_DevExt, pSession, pReqHdr, cbReq);
            else
                rc = VERR_INVALID_HANDLE;
        }
        else if (uReq == VBGL_IOCTL_IDC_CONNECT)
        {
            rc = VGDrvCommonCreateKernelSession(&g_DevExt, &pSession);
            if (RT_SUCCESS(rc))
            {
                rc = VGDrvCommonIoCtl(uReq, &g_DevExt, pSession, pReqHdr, cbReq);
                if (RT_FAILURE(rc))
                    VGDrvCommonCloseSession(&g_DevExt, pSession);
            }
        }
        else
            rc = VERR_INVALID_HANDLE;
    }
    else
        rc = VERR_INVALID_POINTER;
    return rc;
}
#endif


static int vgdrvSolarisPoll(dev_t Dev, short fEvents, int fAnyYet, short *pReqEvents, struct pollhead **ppPollHead)
{
    LogFlow(("vgdrvSolarisPoll: fEvents=%d fAnyYet=%d\n", fEvents, fAnyYet));

    vboxguest_state_t *pState = ddi_get_soft_state(g_pvgdrvSolarisState, getminor(Dev));
    if (RT_LIKELY(pState))
    {
        PVBOXGUESTSESSION pSession  = (PVBOXGUESTSESSION)pState->pSession;
        uint32_t u32CurSeq = ASMAtomicUoReadU32(&g_DevExt.u32MousePosChangedSeq);
        if (pSession->u32MousePosChangedSeq != u32CurSeq)
        {
            *pReqEvents |= (POLLIN | POLLRDNORM);
            pSession->u32MousePosChangedSeq = u32CurSeq;
        }
        else
        {
            *pReqEvents = 0;
            if (!fAnyYet)
                *ppPollHead = &g_PollHead;
        }

        return 0;
    }

    Log(("vgdrvSolarisPoll: no state data for %d\n", getminor(Dev)));
    return EINVAL;
}


/**
 * Sets IRQ for VMMDev.
 *
 * @returns Solaris error code.
 * @param   pDip     Pointer to the device info structure.
 */
static int vgdrvSolarisAddIRQ(dev_info_t *pDip)
{
    LogFlow(("vgdrvSolarisAddIRQ: pDip=%p\n", pDip));

    /* Get the types of interrupt supported for this hardware. */
    int fIntrType = 0;
    int rc = ddi_intr_get_supported_types(pDip, &fIntrType);
    if (rc == DDI_SUCCESS)
    {
        /* We only support fixed interrupts at this point, not MSIs. */
        if (fIntrType & DDI_INTR_TYPE_FIXED)
        {
            /* Verify the number of interrupts supported by this device. There can only be one fixed interrupt. */
            int cIntrCount = 0;
            rc = ddi_intr_get_nintrs(pDip, fIntrType, &cIntrCount);
            if (   rc == DDI_SUCCESS
                && cIntrCount == 1)
            {
                /* Allocated kernel memory for the interrupt handle. The allocation size is stored internally. */
                g_pahIntrs = RTMemAllocZ(cIntrCount * sizeof(ddi_intr_handle_t));
                if (g_pahIntrs)
                {
                    /* Allocate the interrupt for this device and verify the allocation. */
                    int cIntrAllocated;
                    rc = ddi_intr_alloc(pDip, g_pahIntrs, fIntrType, 0 /* interrupt number */, cIntrCount, &cIntrAllocated,
                                        DDI_INTR_ALLOC_NORMAL);
                    if (   rc == DDI_SUCCESS
                        && cIntrAllocated == 1)
                    {
                        /* Get the interrupt priority assigned by the system. */
                        uint_t uIntrPriority;
                        rc = ddi_intr_get_pri(g_pahIntrs[0], &uIntrPriority);
                        if (rc == DDI_SUCCESS)
                        {
                            /* Check if the interrupt priority is scheduler level or above, if so we need to use a high-level
                               and low-level interrupt handlers with corresponding mutexes. */
                            cmn_err(CE_CONT, "!vboxguest: uIntrPriority=%d hilevel_pri=%d\n", uIntrPriority, ddi_intr_get_hilevel_pri());
                            if (uIntrPriority >= ddi_intr_get_hilevel_pri())
                            {
                                /* Initialize the high-level mutex. */
                                mutex_init(&g_HighLevelIrqMtx, NULL /* pszDesc */, MUTEX_DRIVER, DDI_INTR_PRI(uIntrPriority));

                                /* Assign interrupt handler function to the interrupt handle. */
                                rc = ddi_intr_add_handler(g_pahIntrs[0], (ddi_intr_handler_t *)&vgdrvSolarisHighLevelISR,
                                                          NULL /* pvArg1 */, NULL /* pvArg2 */);

                                if (rc == DDI_SUCCESS)
                                {
                                    /* Add the low-level interrupt handler. */
                                    rc = ddi_intr_add_softint(pDip, &g_hSoftIntr, DDI_INTR_SOFTPRI_MAX,
                                                              (ddi_intr_handler_t *)&vgdrvSolarisISR, NULL /* pvArg1 */);
                                    if (rc == DDI_SUCCESS)
                                    {
                                        /* Initialize the low-level mutex at the corresponding level. */
                                        mutex_init(&g_IrqMtx, NULL /* pszDesc */,  MUTEX_DRIVER,
                                                   DDI_INTR_PRI(DDI_INTR_SOFTPRI_MAX));

                                        g_fSoftIntRegistered = true;
                                        /* Enable the high-level interrupt. */
                                        rc = ddi_intr_enable(g_pahIntrs[0]);
                                        if (rc == DDI_SUCCESS)
                                            return rc;

                                        LogRel((DEVICE_NAME "::AddIRQ: failed to enable interrupt. rc=%d\n", rc));
                                        mutex_destroy(&g_IrqMtx);
                                    }
                                    else
                                        LogRel((DEVICE_NAME "::AddIRQ: failed to add soft interrupt handler. rc=%d\n", rc));

                                    ddi_intr_remove_handler(g_pahIntrs[0]);
                                }
                                else
                                    LogRel((DEVICE_NAME "::AddIRQ: failed to add high-level interrupt handler. rc=%d\n", rc));

                                mutex_destroy(&g_HighLevelIrqMtx);
                            }
                            else
                            {
                                /* Interrupt handler runs at reschedulable level, initialize the mutex at the given priority. */
                                mutex_init(&g_IrqMtx, NULL /* pszDesc */, MUTEX_DRIVER, DDI_INTR_PRI(uIntrPriority));

                                /* Assign interrupt handler function to the interrupt handle. */
                                rc = ddi_intr_add_handler(g_pahIntrs[0], (ddi_intr_handler_t *)vgdrvSolarisISR,
                                                          NULL /* pvArg1 */, NULL /* pvArg2 */);
                                if (rc == DDI_SUCCESS)
                                {
                                    /* Enable the interrupt. */
                                    rc = ddi_intr_enable(g_pahIntrs[0]);
                                    if (rc == DDI_SUCCESS)
                                        return rc;

                                    LogRel((DEVICE_NAME "::AddIRQ: failed to enable interrupt. rc=%d\n", rc));
                                    mutex_destroy(&g_IrqMtx);
                                }
                            }
                        }
                        else
                            LogRel((DEVICE_NAME "::AddIRQ: failed to get priority of interrupt. rc=%d\n", rc));

                        Assert(cIntrAllocated == 1);
                        ddi_intr_free(g_pahIntrs[0]);
                    }
                    else
                        LogRel((DEVICE_NAME "::AddIRQ: failed to allocated IRQs. count=%d\n", cIntrCount));
                    RTMemFree(g_pahIntrs);
                }
                else
                    LogRel((DEVICE_NAME "::AddIRQ: failed to allocated IRQs. count=%d\n", cIntrCount));
            }
            else
                LogRel((DEVICE_NAME "::AddIRQ: failed to get or insufficient number of IRQs. rc=%d cIntrCount=%d\n", rc, cIntrCount));
        }
        else
            LogRel((DEVICE_NAME "::AddIRQ: fixed-type interrupts not supported. IntrType=%#x\n", fIntrType));
    }
    else
        LogRel((DEVICE_NAME "::AddIRQ: failed to get supported interrupt types. rc=%d\n", rc));
    return rc;
}


/**
 * Removes IRQ for VMMDev.
 *
 * @param   pDip     Pointer to the device info structure.
 */
static void vgdrvSolarisRemoveIRQ(dev_info_t *pDip)
{
    LogFlow(("vgdrvSolarisRemoveIRQ:\n"));

    int rc = ddi_intr_disable(g_pahIntrs[0]);
    if (rc == DDI_SUCCESS)
    {
        rc = ddi_intr_remove_handler(g_pahIntrs[0]);
        if (rc == DDI_SUCCESS)
            ddi_intr_free(g_pahIntrs[0]);
    }

    if (g_fSoftIntRegistered)
    {
        ddi_intr_remove_softint(g_hSoftIntr);
        mutex_destroy(&g_HighLevelIrqMtx);
        g_fSoftIntRegistered = false;
    }

    mutex_destroy(&g_IrqMtx);
    RTMemFree(g_pahIntrs);
}


/**
 * High-level Interrupt Service Routine for VMMDev.
 *
 * This routine simply dispatches a soft-interrupt at an acceptable IPL as
 * VGDrvCommonISR() cannot be called at a high IPL (scheduler level or higher)
 * due to pollwakeup() in VGDrvNativeISRMousePollEvent().
 *
 * @param   Arg     Private data (unused, will be NULL).
 * @returns DDI_INTR_CLAIMED if it's our interrupt, DDI_INTR_UNCLAIMED if it isn't.
 */
static uint_t vgdrvSolarisHighLevelISR(caddr_t Arg)
{
    bool const fOurIrq = VGDrvCommonIsOurIRQ(&g_DevExt);
    if (fOurIrq)
    {
        ddi_intr_trigger_softint(g_hSoftIntr, NULL /* Arg */);
        return DDI_INTR_CLAIMED;
    }
    return DDI_INTR_UNCLAIMED;
}


/**
 * Interrupt Service Routine for VMMDev.
 *
 * @param   Arg     Private data (unused, will be NULL).
 * @returns DDI_INTR_CLAIMED if it's our interrupt, DDI_INTR_UNCLAIMED if it isn't.
 */
static uint_t vgdrvSolarisISR(caddr_t Arg)
{
    LogFlow(("vgdrvSolarisISR:\n"));

    /* The mutex is required to protect against parallel executions (if possible?) and also the
       mouse notify registeration race between VGDrvNativeSetMouseNotifyCallback() and VGDrvCommonISR(). */
    mutex_enter(&g_IrqMtx);
    bool fOurIRQ = VGDrvCommonISR(&g_DevExt);
    mutex_exit(&g_IrqMtx);

    return fOurIRQ ? DDI_INTR_CLAIMED : DDI_INTR_UNCLAIMED;
}


void VGDrvNativeISRMousePollEvent(PVBOXGUESTDEVEXT pDevExt)
{
    LogFlow(("VGDrvNativeISRMousePollEvent:\n"));

    /*
     * Wake up poll waiters.
     */
    pollwakeup(&g_PollHead, POLLIN | POLLRDNORM);
}


bool VGDrvNativeProcessOption(PVBOXGUESTDEVEXT pDevExt, const char *pszName, const char *pszValue)
{
    RT_NOREF(pDevExt); RT_NOREF(pszName); RT_NOREF(pszValue);
    return false;
}


/**
 * Sets the mouse notification callback.
 *
 * @returns VBox status code.
 * @param   pDevExt   Pointer to the device extension.
 * @param   pNotify   Pointer to the mouse notify struct.
 */
int VGDrvNativeSetMouseNotifyCallback(PVBOXGUESTDEVEXT pDevExt, PVBGLIOCSETMOUSENOTIFYCALLBACK pNotify)
{
    /* Take the mutex here so as to not race with VGDrvCommonISR() which invokes the mouse notify callback. */
    mutex_enter(&g_IrqMtx);
    pDevExt->pfnMouseNotifyCallback   = pNotify->u.In.pfnNotify;
    pDevExt->pvMouseNotifyCallbackArg = pNotify->u.In.pvUser;
    mutex_exit(&g_IrqMtx);
    return VINF_SUCCESS;
}

