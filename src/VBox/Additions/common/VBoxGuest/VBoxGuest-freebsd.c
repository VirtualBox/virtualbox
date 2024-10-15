/* $Id: VBoxGuest-freebsd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Guest Additions Driver for FreeBSD.
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

/** @todo r=bird: This must merge with SUPDrv-freebsd.c before long. The two
 * source files should only differ on prefixes and the extra bits wrt to the
 * pci device. I.e. it should be diffable so that fixes to one can easily be
 * applied to the other. */


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
#include <sys/bus.h>
#include <sys/poll.h>
#include <sys/selinfo.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/lockmgr.h>
#include <sys/malloc.h>
#include <sys/file.h>
#include <sys/rman.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "VBoxGuestInternal.h"
#include <VBox/version.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/asm.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The module name. */
#define DEVICE_NAME  "vboxguest"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
struct VBoxGuestDeviceState
{
    /** Resource ID of the I/O port */
    int                iIOPortResId;
    /** Pointer to the I/O port resource. */
    struct resource   *pIOPortRes;
    /** Start address of the IO Port. */
    uint16_t           uIOPortBase;
    /** Resource ID of the MMIO area */
    int                iVMMDevMemResId;
    /** Pointer to the MMIO resource. */
    struct resource   *pVMMDevMemRes;
    /** Handle of the MMIO resource. */
    bus_space_handle_t VMMDevMemHandle;
    /** Size of the memory area. */
    bus_size_t         VMMDevMemSize;
    /** Mapping of the register space */
    void              *pMMIOBase;
    /** IRQ number */
    int                iIrqResId;
    /** IRQ resource handle. */
    struct resource   *pIrqRes;
    /** Pointer to the IRQ handler. */
    void              *pfnIrqHandler;
    /** VMMDev version */
    uint32_t           u32Version;
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/*
 * Character device file handlers.
 */
static d_fdopen_t vgdrvFreeBSDOpen;
static d_close_t  vgdrvFreeBSDClose;
static d_ioctl_t  vgdrvFreeBSDIOCtl;
static int        vgdrvFreeBSDIOCtlSlow(PVBOXGUESTSESSION pSession, u_long ulCmd, caddr_t pvData, struct thread *pTd);
static d_write_t  vgdrvFreeBSDWrite;
static d_read_t   vgdrvFreeBSDRead;
static d_poll_t   vgdrvFreeBSDPoll;

/*
 * IRQ related functions.
 */
static void vgdrvFreeBSDRemoveIRQ(device_t pDevice, void *pvState);
static int  vgdrvFreeBSDAddIRQ(device_t pDevice, void *pvState);
static int  vgdrvFreeBSDISR(void *pvState);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static MALLOC_DEFINE(M_VBOXGUEST, "vboxguest", "VirtualBox Guest Device Driver");

#ifndef D_NEEDMINOR
# define D_NEEDMINOR 0
#endif

/*
 * The /dev/vboxguest character device entry points.
 */
static struct cdevsw    g_vgdrvFreeBSDChrDevSW =
{
    .d_version =        D_VERSION,
    .d_flags =          D_TRACKCLOSE | D_NEEDMINOR,
    .d_fdopen =         vgdrvFreeBSDOpen,
    .d_close =          vgdrvFreeBSDClose,
    .d_ioctl =          vgdrvFreeBSDIOCtl,
    .d_read =           vgdrvFreeBSDRead,
    .d_write =          vgdrvFreeBSDWrite,
    .d_poll =           vgdrvFreeBSDPoll,
    .d_name =           "vboxguest"
};

/** Device extention & session data association structure. */
static VBOXGUESTDEVEXT      g_DevExt;

/** List of cloned device. Managed by the kernel. */
static struct clonedevs    *g_pvgdrvFreeBSDClones;
/** The dev_clone event handler tag. */
static eventhandler_tag     g_vgdrvFreeBSDEHTag;
/** Reference counter */
static volatile uint32_t    cUsers;
/** selinfo structure used for polling. */
static struct selinfo       g_SelInfo;

/**
 * DEVFS event handler.
 */
static void vgdrvFreeBSDClone(void *pvArg, struct ucred *pCred, char *pszName, int cchName, struct cdev **ppDev)
{
    int iUnit;
    int rc;

    Log(("vgdrvFreeBSDClone: pszName=%s ppDev=%p\n", pszName, ppDev));

    /*
     * One device node per user, si_drv1 points to the session.
     * /dev/vboxguest<N> where N = {0...255}.
     */
    if (!ppDev)
        return;
    if (strcmp(pszName, "vboxguest") == 0)
        iUnit =  -1;
    else if (dev_stdclone(pszName, NULL, "vboxguest", &iUnit) != 1)
        return;
    if (iUnit >= 256)
    {
        Log(("vgdrvFreeBSDClone: iUnit=%d >= 256 - rejected\n", iUnit));
        return;
    }

    Log(("vgdrvFreeBSDClone: pszName=%s iUnit=%d\n", pszName, iUnit));

    rc = clone_create(&g_pvgdrvFreeBSDClones, &g_vgdrvFreeBSDChrDevSW, &iUnit, ppDev, 0);
    Log(("vgdrvFreeBSDClone: clone_create -> %d; iUnit=%d\n", rc, iUnit));
    if (rc)
    {
        *ppDev = make_dev(&g_vgdrvFreeBSDChrDevSW,
                          iUnit,
                          UID_ROOT,
                          GID_WHEEL,
                          0664,
                          "vboxguest%d", iUnit);
        if (*ppDev)
        {
            dev_ref(*ppDev);
            (*ppDev)->si_flags |= SI_CHEAPCLONE;
            Log(("vgdrvFreeBSDClone: Created *ppDev=%p iUnit=%d si_drv1=%p si_drv2=%p\n",
                     *ppDev, iUnit, (*ppDev)->si_drv1, (*ppDev)->si_drv2));
            (*ppDev)->si_drv1 = (*ppDev)->si_drv2 = NULL;
        }
        else
            Log(("vgdrvFreeBSDClone: make_dev iUnit=%d failed\n", iUnit));
    }
    else
        Log(("vgdrvFreeBSDClone: Existing *ppDev=%p iUnit=%d si_drv1=%p si_drv2=%p\n",
             *ppDev, iUnit, (*ppDev)->si_drv1, (*ppDev)->si_drv2));
}

/**
 * File open handler
 *
 */
#if __FreeBSD_version >= 700000
static int vgdrvFreeBSDOpen(struct cdev *pDev, int fOpen, struct thread *pTd, struct file *pFd)
#else
static int vgdrvFreeBSDOpen(struct cdev *pDev, int fOpen, struct thread *pTd)
#endif
{
    int                 rc;
    PVBOXGUESTSESSION   pSession;
    uint32_t            fRequestor;
    struct ucred       *pCred = curthread->td_ucred;
    if (!pCred)
        pCred = curproc->p_ucred;

    LogFlow(("vgdrvFreeBSDOpen:\n"));

    /*
     * Try grab it (we don't grab the giant, remember).
     */
    if (!ASMAtomicCmpXchgPtr(&pDev->si_drv1, (void *)0x42, NULL))
        return EBUSY;

    /*
     * Create a new session.
     */
    fRequestor = VMMDEV_REQUESTOR_USERMODE | VMMDEV_REQUESTOR_TRUST_NOT_GIVEN;
    if (pCred && pCred->cr_uid == 0)
        fRequestor |= VMMDEV_REQUESTOR_USR_ROOT;
    else
        fRequestor |= VMMDEV_REQUESTOR_USR_USER;
    if (pCred && groupmember(0, pCred))
        fRequestor |= VMMDEV_REQUESTOR_GRP_WHEEL;
    fRequestor |= VMMDEV_REQUESTOR_NO_USER_DEVICE; /** @todo implement /dev/vboxuser
    if (!fUnrestricted)
        fRequestor |= VMMDEV_REQUESTOR_USER_DEVICE; */
    fRequestor |= VMMDEV_REQUESTOR_CON_DONT_KNOW; /** @todo see if we can figure out console relationship of pProc. */
    rc = VGDrvCommonCreateUserSession(&g_DevExt, fRequestor, &pSession);
    if (RT_SUCCESS(rc))
    {
        if (ASMAtomicCmpXchgPtr(&pDev->si_drv1, pSession, (void *)0x42))
        {
            Log(("vgdrvFreeBSDOpen: success - g_DevExt=%p pSession=%p rc=%d pid=%d\n", &g_DevExt, pSession, rc, (int)RTProcSelf()));
            ASMAtomicIncU32(&cUsers);
            return 0;
        }

        VGDrvCommonCloseSession(&g_DevExt, pSession);
    }

    LogRel(("vgdrvFreeBSDOpen: failed. rc=%d\n", rc));
    return RTErrConvertToErrno(rc);
}

/**
 * File close handler
 *
 */
static int vgdrvFreeBSDClose(struct cdev *pDev, int fFile, int DevType, struct thread *pTd)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)pDev->si_drv1;
    Log(("vgdrvFreeBSDClose: fFile=%#x pSession=%p\n", fFile, pSession));

    /*
     * Close the session if it's still hanging on to the device...
     */
    if (RT_VALID_PTR(pSession))
    {
        VGDrvCommonCloseSession(&g_DevExt, pSession);
        if (!ASMAtomicCmpXchgPtr(&pDev->si_drv1, NULL, pSession))
            Log(("vgdrvFreeBSDClose: si_drv1=%p expected %p!\n", pDev->si_drv1, pSession));
        ASMAtomicDecU32(&cUsers);
        /* Don't use destroy_dev here because it may sleep resulting in a hanging user process. */
        destroy_dev_sched(pDev);
    }
    else
        Log(("vgdrvFreeBSDClose: si_drv1=%p!\n", pSession));
    return 0;
}


/**
 * I/O control request.
 *
 * @returns depends...
 * @param   pDev        The device.
 * @param   ulCmd       The command.
 * @param   pvData      Pointer to the data.
 * @param   fFile       The file descriptor flags.
 * @param   pTd         The calling thread.
 */
static int vgdrvFreeBSDIOCtl(struct cdev *pDev, u_long ulCmd, caddr_t pvData, int fFile, struct thread *pTd)
{
    PVBOXGUESTSESSION pSession;
    devfs_get_cdevpriv((void **)&pSession);

    /*
     * Deal with the fast ioctl path first.
     */
    if (VBGL_IOCTL_IS_FAST(ulCmd))
        return VGDrvCommonIoCtlFast(ulCmd, &g_DevExt, pSession);

    return vgdrvFreeBSDIOCtlSlow(pSession, ulCmd, pvData, pTd);
}


/**
 * Deal with the 'slow' I/O control requests.
 *
 * @returns 0 on success, appropriate errno on failure.
 * @param   pSession    The session.
 * @param   ulCmd       The command.
 * @param   pvData      The request data.
 * @param   pTd         The calling thread.
 */
static int vgdrvFreeBSDIOCtlSlow(PVBOXGUESTSESSION pSession, u_long ulCmd, caddr_t pvData, struct thread *pTd)
{
    PVBGLREQHDR pHdr;
    uint32_t    cbReq = IOCPARM_LEN(ulCmd);
    void       *pvUser = NULL;

    /*
     * Buffered request?
     */
    if ((IOC_DIRMASK & ulCmd) == IOC_INOUT)
    {
        pHdr = (PVBGLREQHDR)pvData;
        if (RT_UNLIKELY(cbReq < sizeof(*pHdr)))
        {
            LogRel(("vgdrvFreeBSDIOCtlSlow: cbReq=%#x < %#x; ulCmd=%#lx\n", cbReq, (int)sizeof(*pHdr), ulCmd));
            return EINVAL;
        }
        if (RT_UNLIKELY(pHdr->uVersion != VBGLREQHDR_VERSION))
        {
            LogRel(("vgdrvFreeBSDIOCtlSlow: bad uVersion=%#x; ulCmd=%#lx\n", pHdr->uVersion, ulCmd));
            return EINVAL;
        }
        if (RT_UNLIKELY(   RT_MAX(pHdr->cbIn, pHdr->cbOut) != cbReq
                        || pHdr->cbIn < sizeof(*pHdr)
                        || (pHdr->cbOut < sizeof(*pHdr) && pHdr->cbOut != 0)))
        {
            LogRel(("vgdrvFreeBSDIOCtlSlow: max(%#x,%#x) != %#x; ulCmd=%#lx\n", pHdr->cbIn, pHdr->cbOut, cbReq, ulCmd));
            return EINVAL;
        }
    }
    /*
     * Big unbuffered request?
     */
    else if ((IOC_DIRMASK & ulCmd) == IOC_VOID && !cbReq)
    {
        /*
         * Read the header, validate it and figure out how much that needs to be buffered.
         */
        VBGLREQHDR Hdr;
        pvUser = *(void **)pvData;
        int rc = copyin(pvUser, &Hdr, sizeof(Hdr));
        if (RT_UNLIKELY(rc))
        {
            LogRel(("vgdrvFreeBSDIOCtlSlow: copyin(%p,Hdr,) -> %#x; ulCmd=%#lx\n", pvUser, rc, ulCmd));
            return rc;
        }
        if (RT_UNLIKELY(Hdr.uVersion != VBGLREQHDR_VERSION))
        {
            LogRel(("vgdrvFreeBSDIOCtlSlow: bad uVersion=%#x; ulCmd=%#lx\n", Hdr.uVersion, ulCmd));
            return EINVAL;
        }
        cbReq = RT_MAX(Hdr.cbIn, Hdr.cbOut);
        if (RT_UNLIKELY(   Hdr.cbIn < sizeof(Hdr)
                        || (Hdr.cbOut < sizeof(Hdr) && Hdr.cbOut != 0)
                        || cbReq > _1M*16))
        {
            LogRel(("vgdrvFreeBSDIOCtlSlow: max(%#x,%#x); ulCmd=%#lx\n", Hdr.cbIn, Hdr.cbOut, ulCmd));
            return EINVAL;
        }

        /*
         * Allocate buffer and copy in the data.
         */
        pHdr = (PVBGLREQHDR)RTMemTmpAlloc(cbReq);
        if (RT_UNLIKELY(!pHdr))
        {
            LogRel(("vgdrvFreeBSDIOCtlSlow: failed to allocate buffer of %d bytes; ulCmd=%#lx\n", cbReq, ulCmd));
            return ENOMEM;
        }
        rc = copyin(pvUser, pHdr, Hdr.cbIn);
        if (RT_UNLIKELY(rc))
        {
            LogRel(("vgdrvFreeBSDIOCtlSlow: copyin(%p,%p,%#x) -> %#x; ulCmd=%#lx\n",
                        pvUser, pHdr, Hdr.cbIn, rc, ulCmd));
            RTMemTmpFree(pHdr);
            return rc;
        }
        if (Hdr.cbIn < cbReq)
            RT_BZERO((uint8_t *)pHdr + Hdr.cbIn, cbReq - Hdr.cbIn);
    }
    else
    {
        Log(("vgdrvFreeBSDIOCtlSlow: huh? cbReq=%#x ulCmd=%#lx\n", cbReq, ulCmd));
        return EINVAL;
    }

    /*
     * Process the IOCtl.
     */
    int rc = VGDrvCommonIoCtl(ulCmd, &g_DevExt, pSession, pHdr, cbReq);
    if (RT_LIKELY(!rc))
    {
        /*
         * If unbuffered, copy back the result before returning.
         */
        if (pvUser)
        {
            uint32_t cbOut = pHdr->cbOut;
            if (cbOut > cbReq)
            {
                LogRel(("vgdrvFreeBSDIOCtlSlow: too much output! %#x > %#x; uCmd=%#lx!\n", cbOut, cbReq, ulCmd));
                cbOut = cbReq;
            }
            rc = copyout(pHdr, pvUser, cbOut);
            if (RT_UNLIKELY(rc))
                LogRel(("vgdrvFreeBSDIOCtlSlow: copyout(%p,%p,%#x) -> %d; uCmd=%#lx!\n", pHdr, pvUser, cbOut, rc, ulCmd));

            Log(("vgdrvFreeBSDIOCtlSlow: returns %d / %d ulCmd=%lx\n", 0, pHdr->rc, ulCmd));

            /* cleanup */
            RTMemTmpFree(pHdr);
        }
    }
    else
    {
        /*
         * The request failed, just clean up.
         */
        if (pvUser)
            RTMemTmpFree(pHdr);

        Log(("vgdrvFreeBSDIOCtlSlow: ulCmd=%lx pData=%p failed, rc=%d\n", ulCmd, pvData, rc));
        rc = EINVAL;
    }

    return rc;
}


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


static int vgdrvFreeBSDPoll(struct cdev *pDev, int fEvents, struct thread *td)
{
    int fEventsProcessed;

    LogFlow(("vgdrvFreeBSDPoll: fEvents=%d\n", fEvents));

    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)pDev->si_drv1;
    if (RT_UNLIKELY(!RT_VALID_PTR(pSession))) {
        Log(("vgdrvFreeBSDPoll: no state data for %s\n", devtoname(pDev)));
        return (fEvents & (POLLHUP|POLLIN|POLLRDNORM|POLLOUT|POLLWRNORM));
    }

    uint32_t u32CurSeq = ASMAtomicUoReadU32(&g_DevExt.u32MousePosChangedSeq);
    if (pSession->u32MousePosChangedSeq != u32CurSeq)
    {
        fEventsProcessed = fEvents & (POLLIN | POLLRDNORM);
        pSession->u32MousePosChangedSeq = u32CurSeq;
    }
    else
    {
        fEventsProcessed = 0;

        selrecord(td, &g_SelInfo);
    }

    return fEventsProcessed;
}

static int vgdrvFreeBSDWrite(struct cdev *pDev, struct uio *pUio, int fIo)
{
    return 0;
}

static int vgdrvFreeBSDRead(struct cdev *pDev, struct uio *pUio, int fIo)
{
    return 0;
}

static int vgdrvFreeBSDDetach(device_t pDevice)
{
    struct VBoxGuestDeviceState *pState = device_get_softc(pDevice);

    if (cUsers > 0)
        return EBUSY;

    /*
     * Reverse what we did in vgdrvFreeBSDAttach.
     */
    if (g_vgdrvFreeBSDEHTag != NULL)
        EVENTHANDLER_DEREGISTER(dev_clone, g_vgdrvFreeBSDEHTag);

    clone_cleanup(&g_pvgdrvFreeBSDClones);

    vgdrvFreeBSDRemoveIRQ(pDevice, pState);

    if (pState->pVMMDevMemRes)
        bus_release_resource(pDevice, SYS_RES_MEMORY, pState->iVMMDevMemResId, pState->pVMMDevMemRes);
    if (pState->pIOPortRes)
        bus_release_resource(pDevice, SYS_RES_IOPORT, pState->iIOPortResId, pState->pIOPortRes);

    VGDrvCommonDeleteDevExt(&g_DevExt);

    RTR0Term();

    return 0;
}


/**
 * Interrupt service routine.
 *
 * @returns Whether the interrupt was from VMMDev.
 * @param   pvState Opaque pointer to the device state.
 */
static int vgdrvFreeBSDISR(void *pvState)
{
    LogFlow(("vgdrvFreeBSDISR: pvState=%p\n", pvState));

    bool fOurIRQ = VGDrvCommonISR(&g_DevExt);

    return fOurIRQ ? 0 : 1;
}

void VGDrvNativeISRMousePollEvent(PVBOXGUESTDEVEXT pDevExt)
{
    LogFlow(("VGDrvNativeISRMousePollEvent:\n"));

    /*
     * Wake up poll waiters.
     */
    selwakeup(&g_SelInfo);
}


bool VGDrvNativeProcessOption(PVBOXGUESTDEVEXT pDevExt, const char *pszName, const char *pszValue)
{
    RT_NOREF(pDevExt); RT_NOREF(pszName); RT_NOREF(pszValue);
    return false;
}


/**
 * Sets IRQ for VMMDev.
 *
 * @returns FreeBSD error code.
 * @param   pDevice  Pointer to the device info structure.
 * @param   pvState  Pointer to the state info structure.
 */
static int vgdrvFreeBSDAddIRQ(device_t pDevice, void *pvState)
{
    int iResId = 0;
    int rc = 0;
    struct VBoxGuestDeviceState *pState = (struct VBoxGuestDeviceState *)pvState;

    pState->pIrqRes = bus_alloc_resource_any(pDevice, SYS_RES_IRQ, &iResId, RF_SHAREABLE | RF_ACTIVE);

#if __FreeBSD_version >= 700000
    rc = bus_setup_intr(pDevice, pState->pIrqRes, INTR_TYPE_BIO | INTR_MPSAFE, NULL, (driver_intr_t *)vgdrvFreeBSDISR, pState,
                        &pState->pfnIrqHandler);
#else
    rc = bus_setup_intr(pDevice, pState->pIrqRes, INTR_TYPE_BIO, (driver_intr_t *)vgdrvFreeBSDISR, pState, &pState->pfnIrqHandler);
#endif

    if (rc)
    {
        pState->pfnIrqHandler = NULL;
        return VERR_DEV_IO_ERROR;
    }

    pState->iIrqResId = iResId;

    return VINF_SUCCESS;
}

/**
 * Removes IRQ for VMMDev.
 *
 * @param   pDevice  Pointer to the device info structure.
 * @param   pvState  Opaque pointer to the state info structure.
 */
static void vgdrvFreeBSDRemoveIRQ(device_t pDevice, void *pvState)
{
    struct VBoxGuestDeviceState *pState = (struct VBoxGuestDeviceState *)pvState;

    if (pState->pIrqRes)
    {
        bus_teardown_intr(pDevice, pState->pIrqRes, pState->pfnIrqHandler);
        bus_release_resource(pDevice, SYS_RES_IRQ, 0, pState->pIrqRes);
    }
}

static int vgdrvFreeBSDAttach(device_t pDevice)
{
    int rc;
    int iResId;
    struct VBoxGuestDeviceState *pState;

    cUsers = 0;

    /*
     * Initialize IPRT R0 driver, which internally calls OS-specific r0 init.
     */
    rc = RTR0Init(0);
    if (RT_FAILURE(rc))
    {
        LogFunc(("RTR0Init failed.\n"));
        return ENXIO;
    }

    pState = device_get_softc(pDevice);

    /*
     * Allocate I/O port resource.
     */
    iResId                 = PCIR_BAR(0);
    pState->pIOPortRes     = bus_alloc_resource_any(pDevice, SYS_RES_IOPORT, &iResId, RF_ACTIVE);
    pState->uIOPortBase    = rman_get_start(pState->pIOPortRes);
    pState->iIOPortResId   = iResId;
    if (pState->uIOPortBase)
    {
        /*
         * Map the MMIO region.
         */
        iResId                   = PCIR_BAR(1);
        pState->pVMMDevMemRes    = bus_alloc_resource_any(pDevice, SYS_RES_MEMORY, &iResId, RF_ACTIVE);
        pState->VMMDevMemHandle  = rman_get_bushandle(pState->pVMMDevMemRes);
        pState->VMMDevMemSize    = rman_get_size(pState->pVMMDevMemRes);

        pState->pMMIOBase        = rman_get_virtual(pState->pVMMDevMemRes);
        pState->iVMMDevMemResId  = iResId;
        if (pState->pMMIOBase)
        {
            /*
             * Call the common device extension initializer.
             */
            rc = VGDrvCommonInitDevExt(&g_DevExt, pState->uIOPortBase,
                                       NULL /*pvMmioReq*/,
                                       pState->pMMIOBase, pState->VMMDevMemSize,
#if ARCH_BITS == 64
                                       VBOXOSTYPE_FreeBSD_x64,
#else
                                       VBOXOSTYPE_FreeBSD,
#endif
                                       VMMDEV_EVENT_MOUSE_POSITION_CHANGED);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Add IRQ of VMMDev.
                 */
                rc = vgdrvFreeBSDAddIRQ(pDevice, pState);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Read host configuration.
                     */
                    VGDrvCommonProcessOptionsFromHost(&g_DevExt);

                    /*
                     * Configure device cloning.
                     */
                    clone_setup(&g_pvgdrvFreeBSDClones);
                    g_vgdrvFreeBSDEHTag = EVENTHANDLER_REGISTER(dev_clone, vgdrvFreeBSDClone, 0, 1000);
                    if (g_vgdrvFreeBSDEHTag)
                    {
                        printf(DEVICE_NAME ": loaded successfully\n");
                        return 0;
                    }

                    printf(DEVICE_NAME ": EVENTHANDLER_REGISTER(dev_clone,,,) failed\n");
                    clone_cleanup(&g_pvgdrvFreeBSDClones);
                    vgdrvFreeBSDRemoveIRQ(pDevice, pState);
                }
                else
                    printf((DEVICE_NAME ": VGDrvCommonInitDevExt failed.\n"));
                VGDrvCommonDeleteDevExt(&g_DevExt);
            }
            else
                printf((DEVICE_NAME ": vgdrvFreeBSDAddIRQ failed.\n"));
        }
        else
            printf((DEVICE_NAME ": MMIO region setup failed.\n"));
    }
    else
        printf((DEVICE_NAME ": IOport setup failed.\n"));

    RTR0Term();
    return ENXIO;
}

static int vgdrvFreeBSDProbe(device_t pDevice)
{
    if ((pci_get_vendor(pDevice) == VMMDEV_VENDORID) && (pci_get_device(pDevice) == VMMDEV_DEVICEID))
        return 0;

    return ENXIO;
}

static device_method_t vgdrvFreeBSDMethods[] =
{
    /* Device interface. */
    DEVMETHOD(device_probe,  vgdrvFreeBSDProbe),
    DEVMETHOD(device_attach, vgdrvFreeBSDAttach),
    DEVMETHOD(device_detach, vgdrvFreeBSDDetach),
    {0,0}
};

static driver_t vgdrvFreeBSDDriver =
{
    DEVICE_NAME,
    vgdrvFreeBSDMethods,
    sizeof(struct VBoxGuestDeviceState),
};

static devclass_t vgdrvFreeBSDClass;

DRIVER_MODULE(vboxguest, pci, vgdrvFreeBSDDriver, vgdrvFreeBSDClass, 0, 0);
MODULE_VERSION(vboxguest, 1);

