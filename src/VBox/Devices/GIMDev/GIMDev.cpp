/* $Id: GIMDev.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Interface Manager Device.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_GIM
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/gim.h>

#include "VBoxDD.h"
#include <iprt/alloc.h>
#include <iprt/semaphore.h>
#include <iprt/uuid.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define GIMDEV_DEBUG_LUN                998


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * GIM device.
 */
typedef struct GIMDEV
{
    /** Pointer to the device instance.
     * @note Only for getting our bearings when arriving in an interface method. */
    PPDMDEVINSR3                    pDevIns;

    /** LUN\#998: The debug interface. */
    PDMIBASE                        IDbgBase;
    /** LUN\#998: The stream port interface. */
    PDMISTREAM                      IDbgStreamPort;
    /** Pointer to the attached base debug driver. */
    R3PTRTYPE(PPDMIBASE)            pDbgDrvBase;
    /** The debug receive thread. */
    RTTHREAD                        hDbgRecvThread;
    /** Flag to indicate shutdown of the debug receive thread. */
    bool volatile                   fDbgRecvThreadShutdown;
    bool                            afAlignment1[ARCH_BITS / 8 - 1];
    /** The debug setup parameters. */
    GIMDEBUGSETUP                   DbgSetup;
    /** The debug transfer struct. */
    GIMDEBUG                        Dbg;
} GIMDEV;
/** Pointer to the GIM device state. */
typedef GIMDEV *PGIMDEV;
AssertCompileMemberAlignment(GIMDEV, IDbgBase, 8);

#ifndef VBOX_DEVICE_STRUCT_TESTCASE

#ifdef IN_RING3


/* -=-=-=-=-=-=-=-=- PDMIBASE on LUN#GIMDEV_DEBUG_LUN -=-=-=-=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) gimdevR3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PGIMDEV pThis = RT_FROM_MEMBER(pInterface, GIMDEV, IDbgBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThis->IDbgBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMISTREAM, &pThis->IDbgStreamPort);
    return NULL;
}


static DECLCALLBACK(int) gimDevR3DbgRecvThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF1(hThreadSelf);

    /*
     * Validate.
     */
    PPDMDEVINS pDevIns = (PPDMDEVINS)pvUser;
    AssertReturn(pDevIns, VERR_INVALID_PARAMETER);
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);

    PGIMDEV pThis = PDMDEVINS_2_DATA(pDevIns, PGIMDEV);
    AssertReturn(pThis, VERR_INVALID_POINTER);
    AssertReturn(pThis->DbgSetup.cbDbgRecvBuf, VERR_INTERNAL_ERROR);
    AssertReturn(pThis->Dbg.hDbgRecvThreadSem != NIL_RTSEMEVENTMULTI, VERR_INTERNAL_ERROR_2);
    AssertReturn(pThis->Dbg.pvDbgRecvBuf, VERR_INTERNAL_ERROR_3);

    PVM pVM = PDMDevHlpGetVM(pDevIns);
    AssertReturn(pVM, VERR_INVALID_POINTER);

    PPDMISTREAM pDbgDrvStream = pThis->Dbg.pDbgDrvStream;
    AssertReturn(pDbgDrvStream, VERR_INVALID_POINTER);

    for (;;)
    {
        /*
         * Read incoming debug data.
         */
        size_t cbRead = pThis->DbgSetup.cbDbgRecvBuf;
        int rc = pDbgDrvStream->pfnRead(pDbgDrvStream, pThis->Dbg.pvDbgRecvBuf, &cbRead);
        if (   RT_SUCCESS(rc)
            && cbRead > 0)
        {
            /*
             * Notify the consumer thread.
             */
            if (ASMAtomicReadBool(&pThis->Dbg.fDbgRecvBufRead) == false)
            {
                if (pThis->DbgSetup.pfnDbgRecvBufAvail)
                    pThis->DbgSetup.pfnDbgRecvBufAvail(pVM);
                pThis->Dbg.cbDbgRecvBufRead = cbRead;
                RTSemEventMultiReset(pThis->Dbg.hDbgRecvThreadSem);
                ASMAtomicWriteBool(&pThis->Dbg.fDbgRecvBufRead, true);
            }

            /*
             * Wait until the consumer thread has acknowledged reading of the
             * current buffer or we're asked to shut down.
             *
             * It is important that we do NOT re-invoke 'pfnRead' before the
             * current buffer is consumed, otherwise we risk data corruption.
             */
            while (   ASMAtomicReadBool(&pThis->Dbg.fDbgRecvBufRead) == true
                   && !pThis->fDbgRecvThreadShutdown)
            {
                RTSemEventMultiWait(pThis->Dbg.hDbgRecvThreadSem, RT_INDEFINITE_WAIT);
            }
        }
#ifdef RT_OS_LINUX
        else if (rc == VERR_NET_CONNECTION_REFUSED)
        {
            /*
             * With the current, simplistic PDMISTREAM interface, this is the best we can do.
             * Even using RTSocketSelectOne[Ex] on Linux returns immediately with 'ready-to-read'
             * on localhost UDP sockets that are not connected on the other end.
             */
            /** @todo Fix socket waiting semantics on localhost Linux unconnected UDP sockets. */
            RTThreadSleep(400);
        }
#endif
        else if (   rc != VINF_TRY_AGAIN
                 && rc != VERR_TRY_AGAIN
                 && rc != VERR_NET_CONNECTION_RESET_BY_PEER)
        {
            LogRel(("GIMDev: Debug thread terminating with rc=%Rrc\n", rc));
            break;
        }

        if (pThis->fDbgRecvThreadShutdown)
        {
            LogRel(("GIMDev: Debug thread shutting down\n"));
            break;
        }
    }

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) gimdevR3Reset(PPDMDEVINS pDevIns)
{
    NOREF(pDevIns);
    /* We do not deregister any MMIO2 regions as the regions are expected to be static. */
}



/**
 * @interface_method_impl{PDMDEVREG,pfnRelocate}
 */
static DECLCALLBACK(void) gimdevR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    NOREF(pDevIns);
    NOREF(offDelta);
#ifdef VBOX_WITH_RAW_MODE_KEEP
# error relocate pvPageRC
#endif
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) gimdevR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PGIMDEV  pThis    = PDMDEVINS_2_DATA(pDevIns, PGIMDEV);

    /*
     * Signal and wait for the debug thread to terminate.
     */
    if (pThis->hDbgRecvThread != NIL_RTTHREAD)
    {
        pThis->fDbgRecvThreadShutdown = true;
        if (pThis->Dbg.hDbgRecvThreadSem != NIL_RTSEMEVENT)
            RTSemEventMultiSignal(pThis->Dbg.hDbgRecvThreadSem);

        int rc = RTThreadWait(pThis->hDbgRecvThread, 20000, NULL /*prc*/);
        if (RT_SUCCESS(rc))
            pThis->hDbgRecvThread = NIL_RTTHREAD;
        else
        {
            LogRel(("GIMDev: Debug thread did not terminate, rc=%Rrc!\n", rc));
            return VERR_RESOURCE_BUSY;
        }
    }

    /*
     * Now clean up the semaphore & buffer now that the thread is gone.
     */
    if (pThis->Dbg.hDbgRecvThreadSem != NIL_RTSEMEVENT)
    {
        RTSemEventMultiDestroy(pThis->Dbg.hDbgRecvThreadSem);
        pThis->Dbg.hDbgRecvThreadSem = NIL_RTSEMEVENTMULTI;
    }
    if (pThis->Dbg.pvDbgRecvBuf)
    {
        RTMemFree(pThis->Dbg.pvDbgRecvBuf);
        pThis->Dbg.pvDbgRecvBuf = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) gimdevR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PGIMDEV pThis = PDMDEVINS_2_DATA(pDevIns, PGIMDEV);
    RT_NOREF2(iInstance, pCfg);

    Assert(iInstance == 0);

    /*
     * Initialize relevant state bits.
     */
    pThis->pDevIns                  = pDevIns;
    pThis->hDbgRecvThread           = NIL_RTTHREAD;
    pThis->Dbg.hDbgRecvThreadSem    = NIL_RTSEMEVENT;

    /*
     * Get debug setup requirements from GIM.
     */
    int rc = PDMDevHlpGIMGetDebugSetup(pDevIns, &pThis->DbgSetup);
    if (   RT_SUCCESS(rc)
        && pThis->DbgSetup.cbDbgRecvBuf > 0)
    {
        /*
         * Attach the stream driver for the debug connection.
         */
        PPDMISTREAM pDbgDrvStream = NULL;
        pThis->IDbgBase.pfnQueryInterface = gimdevR3QueryInterface;
        rc = PDMDevHlpDriverAttach(pDevIns, GIMDEV_DEBUG_LUN, &pThis->IDbgBase, &pThis->pDbgDrvBase, "GIM Debug Port");
        if (RT_SUCCESS(rc))
        {
            pDbgDrvStream = PDMIBASE_QUERY_INTERFACE(pThis->pDbgDrvBase, PDMISTREAM);
            if (pDbgDrvStream)
                LogRel(("GIMDev: LUN#%u: Debug port configured\n", GIMDEV_DEBUG_LUN));
            else
            {
                LogRel(("GIMDev: LUN#%u: No unit\n", GIMDEV_DEBUG_LUN));
                rc = VERR_INTERNAL_ERROR_2;
            }
        }
        else
        {
            pThis->pDbgDrvBase = NULL;
            LogRel(("GIMDev: LUN#%u: No debug port configured! rc=%Rrc\n", GIMDEV_DEBUG_LUN, rc));
        }

        if (!pDbgDrvStream)
        {
            Assert(rc != VINF_SUCCESS);
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Debug port configuration expected when GIM configured with debugging support"));
        }

        void *pvDbgRecvBuf = RTMemAllocZ(pThis->DbgSetup.cbDbgRecvBuf);
        if (RT_UNLIKELY(!pvDbgRecvBuf))
        {
            LogRel(("GIMDev: Failed to alloc %u bytes for debug receive buffer\n", pThis->DbgSetup.cbDbgRecvBuf));
            return VERR_NO_MEMORY;
        }

        /*
         * Update the shared debug struct.
         */
        pThis->Dbg.pDbgDrvStream    = pDbgDrvStream;
        pThis->Dbg.pvDbgRecvBuf     = pvDbgRecvBuf;
        pThis->Dbg.cbDbgRecvBufRead = 0;
        pThis->Dbg.fDbgRecvBufRead  = false;

        /*
         * Create the semaphore and the debug receive thread itself.
         */
        rc = RTSemEventMultiCreate(&pThis->Dbg.hDbgRecvThreadSem);
        AssertRCReturn(rc, rc);
        rc = RTThreadCreate(&pThis->hDbgRecvThread, gimDevR3DbgRecvThread, pDevIns, 0 /*cbStack*/, RTTHREADTYPE_IO,
                            RTTHREADFLAGS_WAITABLE, "GIMDebugRecv");
        if (RT_FAILURE(rc))
        {
            RTSemEventMultiDestroy(pThis->Dbg.hDbgRecvThreadSem);
            pThis->Dbg.hDbgRecvThreadSem = NIL_RTSEMEVENTMULTI;

            RTMemFree(pThis->Dbg.pvDbgRecvBuf);
            pThis->Dbg.pvDbgRecvBuf = NULL;
            return rc;
        }
    }

    /*
     * Register this device with the GIM component.
     */
    PDMDevHlpGIMDeviceRegister(pDevIns, pThis->DbgSetup.cbDbgRecvBuf ? &pThis->Dbg : NULL);

    /*
     * Get the MMIO2 regions from the GIM provider and make the registrations.
     */
/** @todo r=bird: consider ditching this as GIM doesn't actually make use of it */
    uint32_t        cRegions  = 0;
    PGIMMMIO2REGION paRegions = PDMDevHlpGIMGetMmio2Regions(pDevIns, &cRegions);
    if (   cRegions
        && paRegions)
    {
        for (uint32_t i = 0; i < cRegions; i++)
        {
            PGIMMMIO2REGION pCur = &paRegions[i];
            Assert(pCur->iRegion < 8);
            rc = PDMDevHlpMmio2Create(pDevIns, NULL, pCur->iRegion << 16, pCur->cbRegion, 0 /* fFlags */, pCur->szDescription,
                                      &pCur->pvPageR3, &pCur->hMmio2);
            AssertLogRelMsgRCReturn(rc, ("rc=%Rrc iRegion=%u cbRegion=%#x %s\n",
                                         rc, pCur->iRegion, pCur->cbRegion, pCur->szDescription),
                                    rc);
            pCur->fRegistered = true;
            pCur->pvPageR0 = NIL_RTR0PTR;
# ifdef VBOX_WITH_RAW_MODE_KEEP
            pCur->pvPageRC = NIL_RTRCPTR;
# endif

            LogRel(("GIMDev: Registered %s\n", pCur->szDescription));
        }
    }
    else
        Assert(cRegions == 0);

    /** @todo Register SSM: PDMDevHlpSSMRegister(). */
    /** @todo Register statistics: STAM_REG(). */
    /** @todo Register DBGFInfo: PDMDevHlpDBGFInfoRegister(). */

    return VINF_SUCCESS;
}


#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) gimdevRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    //PGIMDEV pThis = PDMDEVINS_2_DATA(pDevIns, PGIMDEV);

    /*
     * Map the MMIO2 regions into the context.
     */
/** @todo r=bird: consider ditching this as GIM doesn't actually make use of it */
    uint32_t        cRegions  = 0;
    PGIMMMIO2REGION paRegions = PDMDevHlpGIMGetMmio2Regions(pDevIns, &cRegions);
    if (   cRegions
        && paRegions)
    {
        for (uint32_t i = 0; i < cRegions; i++)
        {
            PGIMMMIO2REGION pCur = &paRegions[i];
            int rc = PDMDevHlpMmio2SetUpContext(pDevIns, pCur->hMmio2, 0,  0, &pCur->CTX_SUFF(pvPage));
            AssertLogRelMsgRCReturn(rc, ("rc=%Rrc iRegion=%u cbRegion=%#x %s\n",
                                         rc, pCur->iRegion, pCur->cbRegion, pCur->szDescription),
                                    rc);
            Assert(pCur->fRegistered);
        }
    }
    else
        Assert(cRegions == 0);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceGIMDev =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "GIMDev",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_REQUIRE_R0
                                    | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_MISC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(GIMDEV),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "VirtualBox GIM Device",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           gimdevR3Construct,
    /* .pfnDestruct = */            gimdevR3Destruct,
    /* .pfnRelocate = */            gimdevR3Relocate,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               gimdevR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           gimdevRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           gimdevRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

#endif  /* VBOX_DEVICE_STRUCT_TESTCASE */

