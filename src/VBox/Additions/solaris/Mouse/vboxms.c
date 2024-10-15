/* $Id: vboxms.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Guest Additions Mouse Driver for Solaris.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_MOUSE
#include <VBox/VMMDev.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/log.h>
#include <VBox/version.h>
#include <iprt/assert.h>
#include <iprt/asm.h>

#ifndef TESTCASE
# include <sys/modctl.h>
# include <sys/msio.h>
# include <sys/stat.h>
# include <sys/ddi.h>
# include <sys/strsun.h>
# include <sys/stropts.h>
# include <sys/sunddi.h>
# include <sys/vuid_event.h>
# include <sys/vuid_wheel.h>
#undef u /* /usr/include/sys/user.h:249:1 is where this is defined to (curproc->p_user). very cool. */
#else  /* TESTCASE */
# undef IN_RING3
# define IN_RING0
#endif  /* TESTCASE */

#ifdef TESTCASE  /* Include this last as we . */
# include "testcase/solaris.h"
# include <iprt/test.h>
#endif  /* TESTCASE */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The module name. */
#define DEVICE_NAME              "vboxms"
/** The module description as seen in 'modinfo'. */
#define DEVICE_DESC              "VBoxMouseIntegr"


/*********************************************************************************************************************************
*   Internal functions used in global structures                                                                                 *
*********************************************************************************************************************************/

static int vbmsSolAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd);
static int vbmsSolDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd);
static int vbmsSolGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pvArg,
                          void **ppvResult);
static int vbmsSolOpen(queue_t *pReadQueue, dev_t *pDev, int fFlag,
                                int fMode, cred_t *pCred);
static int vbmsSolClose(queue_t *pReadQueue, int fFlag, cred_t *pCred);
static int vbmsSolWPut(queue_t *pWriteQueue, mblk_t *pMBlk);


/*********************************************************************************************************************************
*   Driver global structures                                                                                                     *
*********************************************************************************************************************************/

#ifndef TESTCASE  /* I see no value in including these in the test. */

/*
 * mod_info: STREAMS module information.
 */
static struct module_info g_vbmsSolModInfo =
{
    0,                      /* module id number */
    "vboxms",
    0,                      /* minimum packet size */
    INFPSZ,                 /* maximum packet size accepted */
    512,                    /* high water mark for data flow control */
    128                     /* low water mark */
};

/*
 * rinit: read queue structure for handling messages coming from below.  In
 * our case this means the host and the virtual hardware, so we do not need
 * the put and service procedures.
 */
static struct qinit g_vbmsSolRInit =
{
    NULL,                /* put */
    NULL,                /* service thread procedure */
    vbmsSolOpen,
    vbmsSolClose,
    NULL,                /* reserved */
    &g_vbmsSolModInfo,
    NULL                 /* module statistics structure */
};

/*
 * winit: write queue structure for handling messages coming from above.  Above
 * means user space applications: either Guest Additions user space tools or
 * applications reading pointer input.  Messages from the last most likely pass
 * through at least the "consms" console mouse streams module which multiplexes
 * hardware pointer drivers to a single virtual pointer.
 */
static struct qinit g_vbmsSolWInit =
{
    vbmsSolWPut,
    NULL,                   /* service thread procedure */
    NULL,                   /* open */
    NULL,                   /* close */
    NULL,                   /* reserved */
    &g_vbmsSolModInfo,
    NULL                    /* module statistics structure */
};

/**
 * streamtab: for drivers that support char/block entry points.
 */
static struct streamtab g_vbmsSolStreamTab =
{
    &g_vbmsSolRInit,
    &g_vbmsSolWInit,
    NULL,                   /* MUX rinit */
    NULL                    /* MUX winit */
};

/**
 * cb_ops: for drivers that support char/block entry points
 */
static struct cb_ops g_vbmsSolCbOps =
{
    nodev,                  /* open */
    nodev,                  /* close */
    nodev,                  /* b strategy */
    nodev,                  /* b dump */
    nodev,                  /* b print */
    nodev,                  /* c read */
    nodev,                  /* c write */
    nodev,                  /* c ioctl */
    nodev,                  /* c devmap */
    nodev,                  /* c mmap */
    nodev,                  /* c segmap */
    nochpoll,               /* c poll */
    ddi_prop_op,            /* property ops */
    &g_vbmsSolStreamTab,
    D_MP,
    CB_REV                  /* revision */
};

/**
 * dev_ops: for driver device operations
 */
static struct dev_ops g_vbmsSolDevOps =
{
    DEVO_REV,               /* driver build revision */
    0,                      /* ref count */
    vbmsSolGetInfo,
    nulldev,                /* identify */
    nulldev,                /* probe */
    vbmsSolAttach,
    vbmsSolDetach,
    nodev,                  /* reset */
    &g_vbmsSolCbOps,
    NULL,                   /* bus operations */
    nodev                   /* power */
};

/**
 * modldrv: export driver specifics to the kernel
 */
static struct modldrv g_vbmsSolModule =
{
    &mod_driverops,         /* extern from kernel */
    DEVICE_DESC " " VBOX_VERSION_STRING "r" RT_XSTR(VBOX_SVN_REV),
    &g_vbmsSolDevOps
};

/**
 * modlinkage: export install/remove/info to the kernel.
 */
static struct modlinkage g_vbmsSolModLinkage =
{
    MODREV_1,               /* loadable module system revision */
    &g_vbmsSolModule,
    NULL                    /* terminate array of linkage structures */
};

#else  /* TESTCASE */
static void *g_vbmsSolModLinkage;
#endif  /* TESTCASE */

/**
 * State info for each open file handle.
 */
typedef struct
{
    /** Device handle. */
    dev_info_t           *pDip;
    /** Mutex protecting the guest library against multiple initialistation or
     * uninitialisation. */
    kmutex_t              InitMtx;
    /** Initialisation counter for the guest library. */
    size_t                cInits;
    /** The STREAMS write queue which we need for sending messages up to
     * user-space. */
    queue_t              *pWriteQueue;
    /** Pre-allocated mouse status VMMDev request for use in the IRQ
     * handler. */
    VMMDevReqMouseStatus *pMouseStatusReq;
    /* The current greatest horizontal pixel offset on the screen, used for
     * absolute mouse position reporting.
     */
    int                   cMaxScreenX;
    /* The current greatest vertical pixel offset on the screen, used for
     * absolute mouse position reporting.
     */
    int                   cMaxScreenY;
} VBMSSTATE, *PVBMSSTATE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/** Global driver state.  Actually this could be allocated dynamically. */
static VBMSSTATE            g_OpenNodeState /* = { 0 } */;


/*********************************************************************************************************************************
*   Kernel entry points                                                                                                          *
*********************************************************************************************************************************/

/** Driver initialisation. */
int _init(void)
{
    int rc;
    LogRelFlow((DEVICE_NAME ": built on " __DATE__ " at " __TIME__ "\n"));
    mutex_init(&g_OpenNodeState.InitMtx, NULL, MUTEX_DRIVER, NULL);
    /*
     * Prevent module autounloading.
     */
    modctl_t *pModCtl = mod_getctl(&g_vbmsSolModLinkage);
    if (pModCtl)
        pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD;
    else
        LogRel((DEVICE_NAME ": failed to disable autounloading!\n"));
    rc = mod_install(&g_vbmsSolModLinkage);

    LogRelFlow((DEVICE_NAME ": initialisation returning %d.\n", rc));
    return rc;
}


#ifdef TESTCASE
/** Simple test of the flow through _init. */
static void test_init(RTTEST hTest)
{
    RTTestSub(hTest, "Testing _init");
    RTTEST_CHECK(hTest, _init() == 0);
}
#endif


/** Driver cleanup. */
int _fini(void)
{
    int rc;

    LogRelFlow((DEVICE_NAME ":_fini\n"));
    rc = mod_remove(&g_vbmsSolModLinkage);
    if (!rc)
        mutex_destroy(&g_OpenNodeState.InitMtx);

    return rc;
}


/** Driver identification. */
int _info(struct modinfo *pModInfo)
{
    int rc;
    LogRelFlow((DEVICE_NAME ":_info\n"));
    rc = mod_info(&g_vbmsSolModLinkage, pModInfo);
    LogRelFlow((DEVICE_NAME ":_info returning %d\n", rc));
    return rc;
}


/*********************************************************************************************************************************
*   Initialisation entry points                                                                                                  *
*********************************************************************************************************************************/

/**
 * Attach entry point, to attach a device to the system or resume it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Attach type (ddi_attach_cmd_t)
 *
 * @return  corresponding solaris error code.
 */
int vbmsSolAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd)
{
    LogRelFlow((DEVICE_NAME "::Attach\n"));
    switch (enmCmd)
    {
        case DDI_ATTACH:
        {
            int rc;
            /* Only one instance supported. */
            if (!ASMAtomicCmpXchgPtr(&g_OpenNodeState.pDip, pDip, NULL))
                return DDI_FAILURE;
            rc = ddi_create_minor_node(pDip, DEVICE_NAME, S_IFCHR, 0 /* instance */, DDI_PSEUDO, 0 /* flags */);
            if (rc == DDI_SUCCESS)
                return DDI_SUCCESS;
            ASMAtomicWritePtr(&g_OpenNodeState.pDip, NULL);
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
int vbmsSolDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    LogRelFlow((DEVICE_NAME "::Detach\n"));
    switch (enmCmd)
    {
        case DDI_DETACH:
        {
            ddi_remove_minor_node(pDip, NULL);
            ASMAtomicWritePtr(&g_OpenNodeState.pDip, NULL);
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
 * Info entry point, called by solaris kernel for obtaining driver info.
 *
 * @param   pDip            The module structure instance (do not use).
 * @param   enmCmd          Information request type.
 * @param   pvArg           Type specific argument.
 * @param   ppvResult       Where to store the requested info.
 *
 * @return  corresponding solaris error code.
 */
int vbmsSolGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pvArg,
                   void **ppvResult)
{
    LogRelFlow((DEVICE_NAME "::GetInfo\n"));

    int rc = DDI_SUCCESS;
    switch (enmCmd)
    {
        case DDI_INFO_DEVT2DEVINFO:
        {
            *ppvResult = (void *)g_OpenNodeState.pDip;
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


/*********************************************************************************************************************************
*   Main code                                                                                                                    *
*********************************************************************************************************************************/

static void vbmsSolNotify(void *pvState);
static void vbmsSolVUIDPutAbsEvent(PVBMSSTATE pState, ushort_t cEvent,
                                   int cValue);

/**
 * Open callback for the read queue, which we use as a generic device open
 * handler.
 */
int vbmsSolOpen(queue_t *pReadQueue, dev_t *pDev, int fFlag, int fMode,
                 cred_t *pCred)
{
    PVBMSSTATE pState = NULL;
    int rc = VINF_SUCCESS;

    NOREF(fFlag);
    NOREF(pCred);
    LogRelFlow((DEVICE_NAME "::Open, pWriteQueue=%p\n", WR(pReadQueue)));

    /*
     * Sanity check on the mode parameter - only open as a driver, not a
     * module, and we do cloning ourselves.
     */
    if (fMode)
    {
        LogRel(("::Open: invalid attempt to clone device."));
        return EINVAL;
    }

    pState = &g_OpenNodeState;
    mutex_enter(&pState->InitMtx);
    /*
     * Check and remember our STREAM queue.
     */
    if (   pState->pWriteQueue
        && (pState->pWriteQueue != WR(pReadQueue)))
    {
        mutex_exit(&pState->InitMtx);
        LogRel((DEVICE_NAME "::Open: unexpectedly called with a different queue to previous calls.  Exiting.\n"));
        return EINVAL;
    }
    if (!pState->cInits)
    {
        /*
         * Initialize IPRT R0 driver, which internally calls OS-specific r0
         * init, and create a new session.
         */
        rc = VbglR0InitClient();
        if (RT_SUCCESS(rc))
        {
            rc = VbglR0GRAlloc((VMMDevRequestHeader **)
                             &pState->pMouseStatusReq,
                             sizeof(*pState->pMouseStatusReq),
                             VMMDevReq_GetMouseStatus);
            if (RT_FAILURE(rc))
                VbglR0TerminateClient();
            else
            {
                int rc2;
                /* Initialise user data for the queues to our state and
                 * vice-versa. */
                pState->pWriteQueue = WR(pReadQueue);
                WR(pReadQueue)->q_ptr = (char *)pState;
                pReadQueue->q_ptr = (char *)pState;
                qprocson(pReadQueue);
                /* Enable our IRQ handler. */
                rc2 = VbglR0SetMouseNotifyCallback(vbmsSolNotify, (void *)pState);
                if (RT_FAILURE(rc2))
                    /* Log the failure.  I may well have not understood what
                     * is going on here, and the logging may help me. */
                    LogRelFlow(("Failed to install the event handler call-back, rc=%Rrc\n",
                                rc2));
            }
        }
    }
    if (RT_SUCCESS(rc))
        ++pState->cInits;
    mutex_exit(&pState->InitMtx);
    if (RT_FAILURE(rc))
    {
        LogRel(("open time initialisation failed. rc=%d\n", rc));
        ASMAtomicWriteNullPtr(&pState->pWriteQueue);
        return EINVAL;
    }
    return 0;
}


/**
 * Notification callback, called when the VBoxGuest mouse pointer is moved.
 * We send a VUID event up to user space.  We may send a miscalculated event
 * if a resolution change is half-way through, but that is pretty much to be
 * expected, so we won't worry about it.
 */
void vbmsSolNotify(void *pvState)
{
    PVBMSSTATE pState = (PVBMSSTATE)pvState;
    int rc;

    pState->pMouseStatusReq->mouseFeatures = 0;
    pState->pMouseStatusReq->pointerXPos = 0;
    pState->pMouseStatusReq->pointerYPos = 0;
    rc = VbglR0GRPerform(&pState->pMouseStatusReq->header);
    if (RT_SUCCESS(rc))
    {
        int cMaxScreenX  = pState->cMaxScreenX;
        int cMaxScreenY  = pState->cMaxScreenY;
        int x = pState->pMouseStatusReq->pointerXPos;
        int y = pState->pMouseStatusReq->pointerYPos;

        if (cMaxScreenX && cMaxScreenY)
        {
            vbmsSolVUIDPutAbsEvent(pState, LOC_X_ABSOLUTE,
                                   x * cMaxScreenX / VMMDEV_MOUSE_RANGE_MAX);
            vbmsSolVUIDPutAbsEvent(pState, LOC_Y_ABSOLUTE,
                                   y * cMaxScreenY / VMMDEV_MOUSE_RANGE_MAX);
        }
    }
}


void vbmsSolVUIDPutAbsEvent(PVBMSSTATE pState, ushort_t cEvent,
                            int cValue)
{
    queue_t *pReadQueue = RD(pState->pWriteQueue);
    mblk_t *pMBlk = allocb(sizeof(Firm_event), BPRI_HI);
    Firm_event *pEvent;
    AssertReturnVoid(cEvent == LOC_X_ABSOLUTE || cEvent == LOC_Y_ABSOLUTE);
    if (!pMBlk)
        return;  /* If kernel memory is short a missed event is acceptable! */
    pEvent = (Firm_event *)pMBlk->b_wptr;
    pEvent->id        = cEvent;
    pEvent->pair_type = FE_PAIR_DELTA;
    pEvent->pair      = cEvent == LOC_X_ABSOLUTE ? LOC_X_DELTA : LOC_Y_DELTA;
    pEvent->value     = cValue;
    uniqtime32(&pEvent->time);
    pMBlk->b_wptr += sizeof(Firm_event);
    /* Put the message on the queue immediately if it is not blocked. */
    if (canput(pReadQueue->q_next))
        putnext(pReadQueue, pMBlk);
    else
        putq(pReadQueue, pMBlk);
}


/**
 * Close callback for the read queue, which we use as a generic device close
 * handler.
 */
int vbmsSolClose(queue_t *pReadQueue, int fFlag, cred_t *pCred)
{
    PVBMSSTATE pState = (PVBMSSTATE)pReadQueue->q_ptr;

    LogRelFlow((DEVICE_NAME "::Close, pWriteQueue=%p\n", WR(pReadQueue)));
    NOREF(fFlag);
    NOREF(pCred);

    if (!pState)
    {
        Log((DEVICE_NAME "::Close: failed to get pState.\n"));
        return EFAULT;
    }

    mutex_enter(&pState->InitMtx);
    --pState->cInits;
    if (!pState->cInits)
    {
        VbglR0SetMouseStatus(0);
        /* Disable our IRQ handler. */
        VbglR0SetMouseNotifyCallback(NULL, NULL);
        qprocsoff(pReadQueue);

        /*
         * Close the session.
         */
        ASMAtomicWriteNullPtr(&pState->pWriteQueue);
        pReadQueue->q_ptr = NULL;
        VbglR0GRFree(&pState->pMouseStatusReq->header);
        VbglR0TerminateClient();
    }
    mutex_exit(&pState->InitMtx);
    return 0;
}


#ifdef TESTCASE
/** Simple test of vbmsSolOpen and vbmsSolClose. */
static void testOpenClose(RTTEST hTest)
{
    queue_t aQueues[2];
    dev_t device = 0;
    int rc;

    RTTestSub(hTest, "Testing vbmsSolOpen and vbmsSolClose");
    RT_ZERO(g_OpenNodeState);
    RT_ZERO(aQueues);
    doInitQueues(&aQueues[0]);
    rc = vbmsSolOpen(RD(&aQueues[0]), &device, 0, 0, NULL);
    RTTEST_CHECK(hTest, rc == 0);
    RTTEST_CHECK(hTest, g_OpenNodeState.pWriteQueue == WR(&aQueues[0]));
    vbmsSolClose(RD(&aQueues[0]), 0, NULL);
}
#endif


/* Helper for vbmsSolWPut. */
static int vbmsSolDispatchIOCtl(PVBMSSTATE pState, mblk_t *pMBlk);

/**
 * Handler for messages sent from above (user-space and upper modules) which
 * land in our write queue.
 */
int vbmsSolWPut(queue_t *pWriteQueue, mblk_t *pMBlk)
{
    PVBMSSTATE pState = (PVBMSSTATE)pWriteQueue->q_ptr;
    LogRelFlowFunc((DEVICE_NAME "::"));
    switch (pMBlk->b_datap->db_type)
    {
        case M_FLUSH:
            LogRelFlow(("M_FLUSH, FLUSHW=%RTbool, FLUSHR=%RTbool\n",
                        *pMBlk->b_rptr & FLUSHW, *pMBlk->b_rptr & FLUSHR));
            /* Flush the write queue if so requested. */
            if (*pMBlk->b_rptr & FLUSHW)
                flushq(pWriteQueue, FLUSHDATA);

            /* Flush the read queue if so requested. */
            if (*pMBlk->b_rptr & FLUSHR)
                flushq(RD(pWriteQueue), FLUSHDATA);

            /* We have no one below us to pass the message on to. */
            freemsg(pMBlk);
            return 0;
        /* M_IOCDATA is additional data attached to (at least) transparent
         * IOCtls.  We handle the two together here and separate them further
         * down. */
        case M_IOCTL:
        case M_IOCDATA:
        {
            int err;

            LogRelFlow((  pMBlk->b_datap->db_type == M_IOCTL
                        ? "M_IOCTL\n" : "M_IOCDATA\n"));
            err = vbmsSolDispatchIOCtl(pState, pMBlk);
            if (!err)
                qreply(pWriteQueue, pMBlk);
            else
                miocnak(pWriteQueue, pMBlk, 0, err);
            break;
        }
        default:
            LogRelFlow(("Unknown command, not acknowledging.\n"));
    }
    return 0;
}


#ifdef TESTCASE
/* Constants, definitions and test functions for testWPut. */
static const int g_ccTestWPutFirmEvent = VUID_FIRM_EVENT;
# define PVGFORMAT (&g_ccTestWPutFirmEvent)
# define CBGFORMAT (sizeof(g_ccTestWPutFirmEvent))
static const Ms_screen_resolution g_TestResolution = { 640, 480 };
# define PMSIOSRES (&g_TestResolution)
# define CBMSIOSRES (sizeof(g_TestResolution))

static inline bool testSetResolution(RTTEST hTest, queue_t *pWriteQueue,
                                     struct msgb *pMBlk)
{
    PVBMSSTATE pState = (PVBMSSTATE)pWriteQueue->q_ptr;
    RTTEST_CHECK_MSG_RET(hTest,    pState->cMaxScreenX
                               == g_TestResolution.width - 1,
                         (hTest, "pState->cMaxScreenX=%d\n",
                          pState->cMaxScreenX), false);
    RTTEST_CHECK_MSG_RET(hTest,    pState->cMaxScreenY
                               == g_TestResolution.height - 1,
                         (hTest, "pState->cMaxScreenY=%d\n",
                          pState->cMaxScreenY), false);
    return true;
}

/** Data table for testWPut. */
static struct
{
    int iIOCCmd;
    size_t cbData;
    const void *pvDataIn;
    size_t cbDataIn;
    const void *pvDataOut;
    size_t cbDataOut;
    int rcExp;
    bool (*pfnExtra)(RTTEST hTest, queue_t *pWriteQueue, struct msgb *pMBlk);
    bool fCanTransparent;
} g_asTestWPut[] =
{
   /* iIOCCmd          cbData           pvDataIn   cbDataIn
      pvDataOut   cbDataOut rcExp   pfnExtra           fCanTransparent */
    { VUIDGFORMAT,     sizeof(int),     NULL,      0,
      PVGFORMAT, CBGFORMAT, 0,      NULL,              true },
    { VUIDGFORMAT,     sizeof(int) - 1, NULL,      0,
      NULL,       0,        EINVAL, NULL,              false },
    { VUIDGFORMAT,     sizeof(int) + 1, NULL,      0,
      PVGFORMAT, CBGFORMAT, 0,      NULL,              true },
    { VUIDSFORMAT,     sizeof(int),     PVGFORMAT, CBGFORMAT,
      NULL,       0,        0,      NULL,              true },
    { MSIOSRESOLUTION, CBMSIOSRES,      PMSIOSRES, CBMSIOSRES,
      NULL,       0,        0,      testSetResolution, true },
    { VUIDGWHEELINFO,  0,               NULL,      0,
      NULL,       0,        EINVAL, NULL,              true }
};

# undef PVGFORMAT
# undef CBGFORMAT
# undef PMSIOSRES
# undef CBMSIOSRES

/* Helpers for testWPut. */
static void testWPutStreams(RTTEST hTest, unsigned i);
static void testWPutTransparent(RTTEST hTest, unsigned i);
static void testWPutIOCDataIn(RTTEST hTest, unsigned i);
static void testWPutIOCDataOut(RTTEST hTest, unsigned i);

/** Test WPut's handling of different IOCtls, which is bulk of the logic in
 * this file. */
static void testWPut(RTTEST hTest)
{
    unsigned i;

    RTTestSub(hTest, "Testing vbmsWPut");
    for (i = 0; i < RT_ELEMENTS(g_asTestWPut); ++i)
    {
        AssertReturnVoid(g_asTestWPut[i].cbDataIn <= g_asTestWPut[i].cbData);
        AssertReturnVoid(g_asTestWPut[i].cbDataOut <= g_asTestWPut[i].cbData);
        testWPutStreams(hTest, i);
        if (g_asTestWPut[i].fCanTransparent)
            testWPutTransparent(hTest, i);
        if (g_asTestWPut[i].fCanTransparent && g_asTestWPut[i].cbDataIn)
            testWPutIOCDataIn(hTest, i);
        if (g_asTestWPut[i].fCanTransparent && g_asTestWPut[i].cbDataOut)
            testWPutIOCDataOut(hTest, i);
    }
}


#define MSG_DATA_SIZE 1024

/** Simulate sending a streams IOCtl to WPut with the parameters from table
 * line @a i. */
void testWPutStreams(RTTEST hTest, unsigned i)
{
    queue_t aQueues[2];
    dev_t device = 0;
    struct msgb *pMBlk = allocb(sizeof(struct iocblk), BPRI_MED);
    struct msgb *pMBlkCont = allocb(MSG_DATA_SIZE, BPRI_MED);
    struct iocblk *pIOCBlk = pMBlk ? (struct iocblk *)pMBlk->b_rptr : NULL;
    int rc, cFormat = 0;

    AssertReturnVoid(pMBlk);
    AssertReturnVoidStmt(pMBlkCont, freemsg(pMBlk));
    RT_ZERO(aQueues);
    doInitQueues(&aQueues[0]);
    rc = vbmsSolOpen(RD(&aQueues[0]), &device, 0, 0, NULL);
    RTTEST_CHECK_MSG(hTest, rc == 0, (hTest, "i=%u, rc=%d\n", i, rc));
    RTTEST_CHECK_MSG(hTest,    g_OpenNodeState.pWriteQueue
                            == WR(&aQueues[0]), (hTest, "i=%u\n", i));
    pMBlk->b_datap->db_type = M_IOCTL;
    pIOCBlk->ioc_cmd = g_asTestWPut[i].iIOCCmd;
    pIOCBlk->ioc_count = g_asTestWPut[i].cbData;
    AssertReturnVoid(g_asTestWPut[i].cbData <= MSG_DATA_SIZE);
    memcpy(pMBlkCont->b_rptr, g_asTestWPut[i].pvDataIn,
           g_asTestWPut[i].cbDataIn);
    pMBlk->b_cont = pMBlkCont;
    rc = vbmsSolWPut(WR(&aQueues[0]), pMBlk);
    RTTEST_CHECK_MSG(hTest, pIOCBlk->ioc_error == g_asTestWPut[i].rcExp,
                     (hTest, "i=%u, IOCBlk.ioc_error=%d\n", i,
                      pIOCBlk->ioc_error));
    RTTEST_CHECK_MSG(hTest, pIOCBlk->ioc_count == g_asTestWPut[i].cbDataOut,
                     (hTest, "i=%u, ioc_count=%u\n", i, pIOCBlk->ioc_count));
    RTTEST_CHECK_MSG(hTest, !memcmp(pMBlkCont->b_rptr,
                                    g_asTestWPut[i].pvDataOut,
                                    g_asTestWPut[i].cbDataOut),
                     (hTest, "i=%u\n", i));
    /* Hack to ensure that miocpullup() gets called when needed. */
    if (g_asTestWPut[i].cbData > 0)
        RTTEST_CHECK_MSG(hTest, pMBlk->b_flag == 1, (hTest, "i=%u\n", i));
    if (!g_asTestWPut[i].rcExp)
        RTTEST_CHECK_MSG(hTest, RD(&aQueues[0])->q_first == pMBlk,
                         (hTest, "i=%u\n", i));
    if (g_asTestWPut[i].pfnExtra)
        if (!g_asTestWPut[i].pfnExtra(hTest, WR(&aQueues[0]), pMBlk))
            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Called from %s.\n",
                         __PRETTY_FUNCTION__);
    vbmsSolClose(RD(&aQueues[1]), 0, NULL);
    freemsg(pMBlk);
}


#define USER_ADDRESS 0xfeedbacc

/** Simulate sending a transparent IOCtl to WPut with the parameters from table
 * line @a i. */
void testWPutTransparent(RTTEST hTest, unsigned i)
{
    queue_t aQueues[2];
    dev_t device = 0;
    struct msgb *pMBlk = allocb(sizeof(struct iocblk), BPRI_MED);
    struct msgb *pMBlkCont = allocb(sizeof(void *), BPRI_MED);
    struct iocblk *pIOCBlk = pMBlk ? (struct iocblk *)pMBlk->b_rptr : NULL;
    struct copyreq *pCopyReq;
    int rc, cFormat = 0;

    /* if (g_asTestWPut[i].cbDataIn == 0 && g_asTestWPut[i].cbDataOut != 0)
        return; */  /* This case will be handled once the current ones work. */
    AssertReturnVoid(pMBlk);
    AssertReturnVoidStmt(pMBlkCont, freemsg(pMBlk));
    RT_ZERO(aQueues);
    doInitQueues(&aQueues[0]);
    rc = vbmsSolOpen(RD(&aQueues[0]), &device, 0, 0, NULL);
    RTTEST_CHECK_MSG(hTest, rc == 0, (hTest, "i=%u, rc=%d\n", i, rc));
    RTTEST_CHECK_MSG(hTest,    g_OpenNodeState.pWriteQueue
                            == WR(&aQueues[0]), (hTest, "i=%u\n", i));
    pMBlk->b_datap->db_type = M_IOCTL;
    pIOCBlk->ioc_cmd = g_asTestWPut[i].iIOCCmd;
    pIOCBlk->ioc_count = TRANSPARENT;
    *(void **)pMBlkCont->b_rptr = (void *)USER_ADDRESS;
    pMBlk->b_cont = pMBlkCont;
    rc = vbmsSolWPut(WR(&aQueues[0]), pMBlk);
    pCopyReq = (struct copyreq *)pMBlk->b_rptr;
    RTTEST_CHECK_MSG(hTest, (   (   g_asTestWPut[i].cbDataIn
                                 && (pMBlk->b_datap->db_type == M_COPYIN))
                             || (   g_asTestWPut[i].cbDataOut
                                 && (pMBlk->b_datap->db_type == M_COPYOUT))
                             || (   (g_asTestWPut[i].rcExp == 0)
                                 && pMBlk->b_datap->db_type == M_IOCACK)
                             || (pMBlk->b_datap->db_type == M_IOCNAK)),
                     (hTest, "i=%u, db_type=%u\n", i,
                      (unsigned) pMBlk->b_datap->db_type));
    /* Our TRANSPARENT IOCtls can only return non-zero if they have no payload.
     * Others should either return zero or be non-TRANSPARENT only. */
    if (pMBlk->b_datap->db_type == M_IOCNAK)
        RTTEST_CHECK_MSG(hTest, pIOCBlk->ioc_error == g_asTestWPut[i].rcExp,
                         (hTest, "i=%u, IOCBlk.ioc_error=%d\n", i,
                          pIOCBlk->ioc_error));
    if (g_asTestWPut[i].cbData)
    {
        RTTEST_CHECK_MSG(hTest, pCopyReq->cq_addr == (char *)USER_ADDRESS,
                         (hTest, "i=%u, cq_addr=%p\n", i, pCopyReq->cq_addr));
        RTTEST_CHECK_MSG(   hTest, pCopyReq->cq_size
                         ==   g_asTestWPut[i].cbDataIn
                            ? g_asTestWPut[i].cbDataIn
                            : g_asTestWPut[i].cbDataOut,
                         (hTest, "i=%u, cq_size=%llu\n", i,
                          (unsigned long long)pCopyReq->cq_size));
    }
    /* Implementation detail - check that the private pointer is correctly
     * set to the user address *for two direction IOCtls* or NULL otherwise. */
    if (g_asTestWPut[i].cbDataIn && g_asTestWPut[i].cbDataOut)
        RTTEST_CHECK_MSG(hTest, pCopyReq->cq_private == (mblk_t *)USER_ADDRESS,
                         (hTest, "i=%u, cq_private=%p\n", i,
                          pCopyReq->cq_private));
    else if (   (pMBlk->b_datap->db_type == M_COPYIN)
             || (pMBlk->b_datap->db_type == M_COPYOUT))
        RTTEST_CHECK_MSG(hTest, !pCopyReq->cq_private,
                         (hTest, "i=%u, cq_private=%p\n", i,
                          pCopyReq->cq_private));
    if (!g_asTestWPut[i].rcExp)
        RTTEST_CHECK_MSG(hTest, RD(&aQueues[0])->q_first == pMBlk,
                         (hTest, "i=%u\n", i));
    if (g_asTestWPut[i].pfnExtra && !g_asTestWPut[i].cbData)
        if (!g_asTestWPut[i].pfnExtra(hTest, WR(&aQueues[0]), pMBlk))
            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Called from %s.\n",
                         __PRETTY_FUNCTION__);
    vbmsSolClose(RD(&aQueues[1]), 0, NULL);
    freemsg(pMBlk);
}


/** Simulate sending follow-on IOCData messages to a transparent IOCtl to WPut
 * with the parameters from table line @a i. */
void testWPutIOCDataIn(RTTEST hTest, unsigned i)
{
    queue_t aQueues[2];
    dev_t device = 0;
    struct msgb *pMBlk = allocb(sizeof(struct copyresp), BPRI_MED);
    struct msgb *pMBlkCont = allocb(MSG_DATA_SIZE, BPRI_MED);
    struct copyresp *pCopyResp = pMBlk ? (struct copyresp *)pMBlk->b_rptr
                                       : NULL;
    void *pvData = pMBlkCont ? pMBlkCont->b_rptr : NULL;
    struct copyreq *pCopyReq;
    int rc, cFormat = 0;

    AssertReturnVoid(pMBlk);
    AssertReturnVoidStmt(pMBlkCont, freemsg(pMBlk));
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%s: i=%u\n", __PRETTY_FUNCTION__,
                 i);
    AssertReturnVoidStmt(g_asTestWPut[i].cbDataIn, freemsg(pMBlk));
    RT_ZERO(aQueues);
    doInitQueues(&aQueues[0]);
    rc = vbmsSolOpen(RD(&aQueues[0]), &device, 0, 0, NULL);
    RTTEST_CHECK_MSG(hTest, rc == 0, (hTest, "i=%u, rc=%d\n", i, rc));
    RTTEST_CHECK_MSG(hTest,    g_OpenNodeState.pWriteQueue
                            == WR(&aQueues[0]), (hTest, "i=%u\n", i));
    pMBlk->b_datap->db_type = M_IOCDATA;
    pCopyResp->cp_cmd = g_asTestWPut[i].iIOCCmd;
    if (g_asTestWPut[i].cbDataOut)
        pCopyResp->cp_private = USER_ADDRESS;
    AssertReturnVoid(g_asTestWPut[i].cbData <= MSG_DATA_SIZE);
    memcpy(pMBlkCont->b_rptr, g_asTestWPut[i].pvDataIn, g_asTestWPut[i].cbDataIn);
    pMBlk->b_cont = pMBlkCont;
    rc = vbmsSolWPut(WR(&aQueues[0]), pMBlk);
    pCopyReq = (struct copyreq *)pMBlk->b_rptr;
    RTTEST_CHECK_MSG(hTest, (   (   g_asTestWPut[i].cbDataOut
                                 && (pMBlk->b_datap->db_type == M_COPYOUT))
                             || (   (g_asTestWPut[i].rcExp == 0)
                                 && pMBlk->b_datap->db_type == M_IOCACK)
                             || (pMBlk->b_datap->db_type == M_IOCNAK)),
                     (hTest, "i=%u, db_type=%u\n", i,
                      (unsigned) pMBlk->b_datap->db_type));
    if (g_asTestWPut[i].cbDataOut)
    {
        RTTEST_CHECK_MSG(hTest, pCopyReq->cq_addr == (char *)pvData,
                         (hTest, "i=%u, cq_addr=%p\n", i, pCopyReq->cq_addr));
        RTTEST_CHECK_MSG(hTest, pCopyReq->cq_size == g_asTestWPut[i].cbData,
                         (hTest, "i=%u, cq_size=%llu\n", i,
                          (unsigned long long)pCopyReq->cq_size));
        RTTEST_CHECK_MSG(hTest, !memcmp(pvData, g_asTestWPut[i].pvDataOut,
                                        g_asTestWPut[i].cbDataOut),
                         (hTest, "i=%u\n", i));
    }
    RTTEST_CHECK_MSG(hTest, !pCopyReq->cq_private,
                     (hTest, "i=%u, cq_private=%p\n", i,
                      pCopyReq->cq_private));
    if (!g_asTestWPut[i].rcExp)
        RTTEST_CHECK_MSG(hTest, RD(&aQueues[0])->q_first == pMBlk,
                         (hTest, "i=%u\n", i));
    if (g_asTestWPut[i].pfnExtra && !g_asTestWPut[i].cbDataOut)
        if (!g_asTestWPut[i].pfnExtra(hTest, WR(&aQueues[0]), pMBlk))
            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Called from %s.\n",
                         __PRETTY_FUNCTION__);
    vbmsSolClose(RD(&aQueues[1]), 0, NULL);
    freemsg(pMBlk);
}


/** Simulate sending follow-on IOCData messages to a transparent IOCtl to WPut
 * with the parameters from table line @a i. */
void testWPutIOCDataOut(RTTEST hTest, unsigned i)
{
    queue_t aQueues[2];
    dev_t device = 0;
    struct msgb *pMBlk = allocb(sizeof(struct copyresp), BPRI_MED);
    struct copyresp *pCopyResp = pMBlk ? (struct copyresp *)pMBlk->b_rptr
                                       : NULL;
    int rc, cFormat = 0;

    AssertReturnVoid(pMBlk);
    AssertReturnVoidStmt(g_asTestWPut[i].cbDataOut, freemsg(pMBlk));
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%s: i=%u\n", __PRETTY_FUNCTION__,
                 i);
    RT_ZERO(aQueues);
    doInitQueues(&aQueues[0]);
    rc = vbmsSolOpen(RD(&aQueues[0]), &device, 0, 0, NULL);
    RTTEST_CHECK_MSG(hTest, rc == 0, (hTest, "i=%u, rc=%d\n", i, rc));
    RTTEST_CHECK_MSG(hTest,    g_OpenNodeState.pWriteQueue
                            == WR(&aQueues[0]), (hTest, "i=%u\n", i));
    pMBlk->b_datap->db_type = M_IOCDATA;
    pCopyResp->cp_cmd = g_asTestWPut[i].iIOCCmd;
    rc = vbmsSolWPut(WR(&aQueues[0]), pMBlk);
    RTTEST_CHECK_MSG(hTest, pMBlk->b_datap->db_type == M_IOCACK,
                     (hTest, "i=%u, db_type=%u\n", i,
                      (unsigned) pMBlk->b_datap->db_type));
    if (!g_asTestWPut[i].rcExp)
        RTTEST_CHECK_MSG(hTest, RD(&aQueues[0])->q_first == pMBlk,
                         (hTest, "i=%u\n", i));
    vbmsSolClose(RD(&aQueues[1]), 0, NULL);
    freemsg(pMBlk);
}
#endif


/** Data transfer direction of an IOCtl.  This is used for describing
 * transparent IOCtls, and @a UNSPECIFIED is not a valid value for them. */
enum IOCTLDIRECTION
{
    /** This IOCtl transfers no data. */
    NONE,
    /** This IOCtl only transfers data from user to kernel. */
    IN,
    /** This IOCtl only transfers data from kernel to user. */
    OUT,
    /** This IOCtl transfers data from user to kernel and back. */
    BOTH,
    /** We aren't saying anything about how the IOCtl transfers data. */
    UNSPECIFIED
};

/**
 * IOCtl handler function.
 * @returns 0 on success, error code on failure.
 * @param iCmd      The IOCtl command number.
 * @param pvData    Buffer for the user data.
 * @param cbBuffer  Size of the buffer in @a pvData or zero.
 * @param pcbData   Where to set the size of the data returned.  Required for
 *                  handlers which return data.
 * @param prc       Where to store the return code.  Default is zero.  Only
 *                  used for IOCtls without data for convenience of
 *                  implemention.
 */
typedef int FNVBMSSOLIOCTL(PVBMSSTATE pState, int iCmd, void *pvData,
                            size_t cbBuffer, size_t *pcbData, int *prc);
typedef FNVBMSSOLIOCTL *PFNVBMSSOLIOCTL;

/* Helpers for vbmsSolDispatchIOCtl. */
static int vbmsSolHandleIOCtl(PVBMSSTATE pState, mblk_t *pMBlk,
                               PFNVBMSSOLIOCTL pfnHandler,
                               int iCmd, size_t cbCmd,
                               enum IOCTLDIRECTION enmDirection);
static int vbmsSolVUIDIOCtl(PVBMSSTATE pState, int iCmd, void *pvData,
                             size_t cbBuffer, size_t *pcbData, int *prc);

/** Table of supported VUID IOCtls. */
struct
{
    /** The IOCtl number. */
    int iCmd;
    /** The size of the buffer which needs to be copied between user and kernel
     * space, or zero if unknown (must be known for tranparent IOCtls). */
    size_t cbBuffer;
    /** The direction the buffer data needs to be copied.  This must be
     * specified for transparent IOCtls. */
    enum IOCTLDIRECTION enmDirection;
} g_aVUIDIOCtlDescriptions[] =
{
   { VUIDGFORMAT,     sizeof(int),                  OUT         },
   { VUIDSFORMAT,     sizeof(int),                  IN          },
   { VUIDGADDR,       0,                            UNSPECIFIED },
   { VUIDGADDR,       0,                            UNSPECIFIED },
   { MSIOGETPARMS,    sizeof(Ms_parms),             OUT         },
   { MSIOSETPARMS,    sizeof(Ms_parms),             IN          },
   { MSIOSRESOLUTION, sizeof(Ms_screen_resolution), IN          },
   { MSIOBUTTONS,     sizeof(int),                  OUT         },
   { VUIDGWHEELCOUNT, sizeof(int),                  OUT         },
   { VUIDGWHEELINFO,  0,                            UNSPECIFIED },
   { VUIDGWHEELSTATE, 0,                            UNSPECIFIED },
   { VUIDSWHEELSTATE, 0,                            UNSPECIFIED }
};

/**
 * Handle a STREAMS IOCtl message for our driver on the write stream.  This
 * function takes care of the IOCtl logic only and does not call qreply() or
 * miocnak() at all - the caller must call these on success or failure
 * respectively.
 * @returns  0 on success or the IOCtl error code on failure.
 * @param  pState       pointer to the state structure.
 * @param  pMBlk        pointer to the STREAMS message block structure.
 */
static int vbmsSolDispatchIOCtl(PVBMSSTATE pState, mblk_t *pMBlk)
{
    struct iocblk *pIOCBlk = (struct iocblk *)pMBlk->b_rptr;
    int iCmd = pIOCBlk->ioc_cmd, iCmdType = iCmd & (0xff << 8);
    size_t cbBuffer;
    enum IOCTLDIRECTION enmDirection;

    LogRelFlowFunc((DEVICE_NAME "::pIOCBlk=%p, iCmdType=%c, iCmd=0x%x\n",
                    pIOCBlk, (char) (iCmdType >> 8), (unsigned)iCmd));
    switch (iCmdType)
    {
        case MSIOC:
        case VUIOC:
        {
            unsigned i;

            for (i = 0; i < RT_ELEMENTS(g_aVUIDIOCtlDescriptions); ++i)
                if (g_aVUIDIOCtlDescriptions[i].iCmd == iCmd)
                {
                    cbBuffer     = g_aVUIDIOCtlDescriptions[i].cbBuffer;
                    enmDirection = g_aVUIDIOCtlDescriptions[i].enmDirection;
                    return vbmsSolHandleIOCtl(pState, pMBlk,
                                              vbmsSolVUIDIOCtl, iCmd,
                                              cbBuffer, enmDirection);
                }
            return EINVAL;
        }
        default:
            return ENOTTY;
    }
}


/* Helpers for vbmsSolHandleIOCtl. */
static int vbmsSolHandleIOCtlData(PVBMSSTATE pState, mblk_t *pMBlk,
                                  PFNVBMSSOLIOCTL pfnHandler, int iCmd,
                                  size_t cbCmd,
                                  enum IOCTLDIRECTION enmDirection);

static int vbmsSolHandleTransparentIOCtl(PVBMSSTATE pState, mblk_t *pMBlk,
                                         PFNVBMSSOLIOCTL pfnHandler,
                                         int iCmd, size_t cbCmd,
                                         enum IOCTLDIRECTION enmDirection);

static int vbmsSolHandleIStrIOCtl(PVBMSSTATE pState, mblk_t *pMBlk,
                                   PFNVBMSSOLIOCTL pfnHandler, int iCmd);

static void vbmsSolAcknowledgeIOCtl(mblk_t *pMBlk, int cbData, int rc)
{
    struct iocblk *pIOCBlk = (struct iocblk *)pMBlk->b_rptr;

    pMBlk->b_datap->db_type = M_IOCACK;
    pIOCBlk->ioc_count = cbData;
    pIOCBlk->ioc_rval = rc;
    pIOCBlk->ioc_error = 0;
}

/**
 * Generic code for handling STREAMS-specific IOCtl logic and boilerplate.  It
 * calls the IOCtl handler passed to it without the handler having to be aware
 * of STREAMS structures, or whether this is a transparent (traditional) or an
 * I_STR (using a STREAMS structure to describe the data) IOCtl.  With the
 * caveat that we only support transparent IOCtls which pass all data in a
 * single buffer of a fixed size (I_STR IOCtls are restricted to a single
 * buffer anyway, but the caller can choose the buffer size).
 * @returns  0 on success or the IOCtl error code on failure.
 * @param  pState         pointer to the state structure.
 * @param  pMBlk          pointer to the STREAMS message block structure.
 * @param  pfnHandler     pointer to the right IOCtl handler function for this
 *                        IOCtl number.
 * @param  iCmd           IOCtl command number.
 * @param  cbCmd          size of the user space buffer for this IOCtl number,
 *                        used for processing transparent IOCtls.  Pass zero
 *                        for IOCtls with no maximum buffer size (which will
 *                        not be able to be handled as transparent) or with
 *                        no argument.
 * @param  enmDirection   data transfer direction of the IOCtl.
 */
static int vbmsSolHandleIOCtl(PVBMSSTATE pState, mblk_t *pMBlk,
                              PFNVBMSSOLIOCTL pfnHandler, int iCmd,
                              size_t cbCmd, enum IOCTLDIRECTION enmDirection)
{
    struct iocblk *pIOCBlk = (struct iocblk *)pMBlk->b_rptr;

    LogFlowFunc(("iCmd=0x%x, cbBuffer=%d, enmDirection=%d\n",
                 (unsigned)iCmd, (int)cbCmd, (int)enmDirection));
    if (pMBlk->b_datap->db_type == M_IOCDATA)
        return vbmsSolHandleIOCtlData(pState, pMBlk, pfnHandler, iCmd,
                                      cbCmd, enmDirection);
    else if (   pMBlk->b_datap->db_type == M_IOCTL
             && pIOCBlk->ioc_count == TRANSPARENT)
        return vbmsSolHandleTransparentIOCtl(pState, pMBlk, pfnHandler,
                                             iCmd, cbCmd, enmDirection);
    else if (pMBlk->b_datap->db_type == M_IOCTL)
        return vbmsSolHandleIStrIOCtl(pState, pMBlk, pfnHandler, iCmd);
    return EINVAL;
}


/**
 * Helper for vbmsSolHandleIOCtl.  This rather complicated-looking
 * code is basically the standard boilerplate for handling any streams IOCtl
 * additional data, which we currently only use for transparent IOCtls.
 * @copydoc vbmsSolHandleIOCtl
 */
static int vbmsSolHandleIOCtlData(PVBMSSTATE pState, mblk_t *pMBlk,
                                  PFNVBMSSOLIOCTL pfnHandler, int iCmd,
                                  size_t cbCmd,
                                  enum IOCTLDIRECTION enmDirection)
{
    struct copyresp *pCopyResp = (struct copyresp *)pMBlk->b_rptr;

    LogFlowFunc(("iCmd=0x%x, cbBuffer=%d, enmDirection=%d, cp_rval=%d, cp_private=%p\n",
                 (unsigned)iCmd, (int)cbCmd, (int)enmDirection,
                 (int)(uintptr_t)pCopyResp->cp_rval,
                 (void *)pCopyResp->cp_private));
    if (pCopyResp->cp_rval)  /* cp_rval is a pointer used as a boolean. */
        return EAGAIN;
    if ((pCopyResp->cp_private && enmDirection == BOTH) || enmDirection == IN)
    {
        size_t cbData = 0;
        void *pvData = NULL;
        int err;

        if (!pMBlk->b_cont)
            return EINVAL;
        pvData = pMBlk->b_cont->b_rptr;
        err = pfnHandler(pState, iCmd, pvData, cbCmd, &cbData, NULL);
        if (!err && enmDirection == BOTH)
            mcopyout(pMBlk, NULL, cbData, pCopyResp->cp_private, NULL);
        else if (!err && enmDirection == IN)
            vbmsSolAcknowledgeIOCtl(pMBlk, 0, 0);
        if ((err || enmDirection == IN) && pCopyResp->cp_private)
            freemsg(pCopyResp->cp_private);
        return err;
    }
    else
    {
        if (pCopyResp->cp_private)
            freemsg(pCopyResp->cp_private);
        AssertReturn(enmDirection == OUT || enmDirection == BOTH, EINVAL);
        vbmsSolAcknowledgeIOCtl(pMBlk, 0, 0);
        return 0;
    }
}

/**
 * Helper for vbmsSolHandleIOCtl.  This rather complicated-looking
 * code is basically the standard boilerplate for handling transparent IOCtls,
 * that is, IOCtls which are not re-packed inside STREAMS IOCtls.
 * @copydoc vbmsSolHandleIOCtl
 */
int vbmsSolHandleTransparentIOCtl(PVBMSSTATE pState, mblk_t *pMBlk,
                                  PFNVBMSSOLIOCTL pfnHandler, int iCmd,
                                  size_t cbCmd,
                                  enum IOCTLDIRECTION enmDirection)
{
    int err = 0, rc = 0;
    size_t cbData = 0;

    LogFlowFunc(("iCmd=0x%x, cbBuffer=%d, enmDirection=%d\n",
                 (unsigned)iCmd, (int)cbCmd, (int)enmDirection));
    if (   (enmDirection != NONE && !pMBlk->b_cont)
        || enmDirection == UNSPECIFIED)
        return EINVAL;
    if (enmDirection == IN || enmDirection == BOTH)
    {
        void *pUserAddr = NULL;
        /* We only need state data if there is something to copy back. */
        if (enmDirection == BOTH)
            pUserAddr = *(void **)pMBlk->b_cont->b_rptr;
            mcopyin(pMBlk, pUserAddr /* state data */, cbCmd, NULL);
        }
        else if (enmDirection == OUT)
    {
        mblk_t *pMBlkOut = allocb(cbCmd, BPRI_MED);
        void *pvData;

        if (!pMBlkOut)
            return EAGAIN;
        pvData = pMBlkOut->b_rptr;
        err = pfnHandler(pState, iCmd, pvData, cbCmd, &cbData, NULL);
        if (!err)
            mcopyout(pMBlk, NULL, cbData, NULL, pMBlkOut);
        else
            freemsg(pMBlkOut);
    }
    else
    {
        AssertReturn(enmDirection == NONE, EINVAL);
        err = pfnHandler(pState, iCmd, NULL, 0, NULL, &rc);
        if (!err)
            vbmsSolAcknowledgeIOCtl(pMBlk, 0, rc);
    }
    return err;
}

/**
 * Helper for vbmsSolHandleIOCtl.  This rather complicated-looking
 * code is basically the standard boilerplate for handling any streams IOCtl.
 * @copydoc vbmsSolHandleIOCtl
 */
static int vbmsSolHandleIStrIOCtl(PVBMSSTATE pState, mblk_t *pMBlk,
                                  PFNVBMSSOLIOCTL pfnHandler, int iCmd)
{
    struct iocblk *pIOCBlk = (struct iocblk *)pMBlk->b_rptr;
    uint_t cbBuffer = pIOCBlk->ioc_count;
    void *pvData = NULL;
    int err, rc = 0;
    size_t cbData = 0;

    LogFlowFunc(("iCmd=0x%x, cbBuffer=%u, b_cont=%p\n",
                 (unsigned)iCmd, cbBuffer, (void *)pMBlk->b_cont));
    if (cbBuffer && !pMBlk->b_cont)
        return EINVAL;
    /* Repack the whole buffer into a single message block if needed. */
    if (cbBuffer)
    {
        err = miocpullup(pMBlk, cbBuffer);
        if (err)
            return err;
        pvData = pMBlk->b_cont->b_rptr;
    }
    else if (pMBlk->b_cont)  /* consms forgets to set ioc_count. */
    {
        pvData = pMBlk->b_cont->b_rptr;
        cbBuffer =   pMBlk->b_cont->b_datap->db_lim
                   - pMBlk->b_cont->b_datap->db_base;
    }
    err = pfnHandler(pState, iCmd, pvData, cbBuffer, &cbData, &rc);
    if (!err)
    {
        LogRelFlowFunc(("pMBlk=%p, pMBlk->b_datap=%p, pMBlk->b_rptr=%p\n",
                        pMBlk, pMBlk->b_datap, pMBlk->b_rptr));
        vbmsSolAcknowledgeIOCtl(pMBlk, cbData, rc);
    }
    return err;
}


/**
 * Handle a VUID input device IOCtl.
 * @copydoc FNVBMSSOLIOCTL
 */
static int vbmsSolVUIDIOCtl(PVBMSSTATE pState, int iCmd, void *pvData,
                             size_t cbBuffer, size_t *pcbData, int *prc)
{
    LogRelFlowFunc((DEVICE_NAME "::pvData=%p " /* no '\n' */, pvData));
    switch (iCmd)
    {
        case VUIDGFORMAT:
        {
            LogRelFlowFunc(("VUIDGFORMAT\n"));
            if (cbBuffer < sizeof(int))
                return EINVAL;
            *(int *)pvData = VUID_FIRM_EVENT;
            *pcbData = sizeof(int);
            return 0;
        }
        case VUIDSFORMAT:
            LogRelFlowFunc(("VUIDSFORMAT\n"));
            /* We define our native format to be VUID_FIRM_EVENT, so there
             * is nothing more to do and we exit here on success or on
             * failure. */
            return 0;
        case VUIDGADDR:
        case VUIDSADDR:
            LogRelFlowFunc(("VUIDGADDR/VUIDSADDR\n"));
            return ENOTTY;
        case MSIOGETPARMS:
        {
            Ms_parms parms = { 0 };

            LogRelFlowFunc(("MSIOGETPARMS\n"));
            if (cbBuffer < sizeof(Ms_parms))
                return EINVAL;
            *(Ms_parms *)pvData = parms;
            *pcbData = sizeof(Ms_parms);
            return 0;
        }
        case MSIOSETPARMS:
            LogRelFlowFunc(("MSIOSETPARMS\n"));
            return 0;
        case MSIOSRESOLUTION:
        {
            Ms_screen_resolution *pResolution = (Ms_screen_resolution *)pvData;
            int rc;

            LogRelFlowFunc(("MSIOSRESOLUTION, cbBuffer=%d, sizeof(Ms_screen_resolution)=%d\n",
                            (int) cbBuffer,
                            (int) sizeof(Ms_screen_resolution)));
            if (cbBuffer < sizeof(Ms_screen_resolution))
                return EINVAL;
            LogRelFlowFunc(("%dx%d\n", pResolution->width,
                            pResolution->height));
            pState->cMaxScreenX = pResolution->width  - 1;
            pState->cMaxScreenY = pResolution->height - 1;
            /* Note: we don't disable this again until session close. */
            rc = VbglR0SetMouseStatus(  VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE
                                      | VMMDEV_MOUSE_NEW_PROTOCOL);
            if (RT_SUCCESS(rc))
                return 0;
            pState->cMaxScreenX = 0;
            pState->cMaxScreenY = 0;
            return ENODEV;
        }
        case MSIOBUTTONS:
        {
            LogRelFlowFunc(("MSIOBUTTONS\n"));
            if (cbBuffer < sizeof(int))
                return EINVAL;
            *(int *)pvData = 0;
            *pcbData = sizeof(int);
            return 0;
        }
        case VUIDGWHEELCOUNT:
        {
            LogRelFlowFunc(("VUIDGWHEELCOUNT\n"));
            if (cbBuffer < sizeof(int))
                return EINVAL;
            *(int *)pvData = 0;
            *pcbData = sizeof(int);
            return 0;
        }
        case VUIDGWHEELINFO:
        case VUIDGWHEELSTATE:
        case VUIDSWHEELSTATE:
            LogRelFlowFunc(("VUIDGWHEELINFO/VUIDGWHEELSTATE/VUIDSWHEELSTATE\n"));
            return EINVAL;
        default:
            LogRelFlowFunc(("Invalid IOCtl command %x\n", iCmd));
            return EINVAL;
    }
}


#ifdef TESTCASE
int main(void)
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstVBoxGuest-solaris", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);
    test_init(hTest);
    testOpenClose(hTest);
    testWPut(hTest);

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}
#endif

