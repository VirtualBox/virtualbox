/* $Id: PDMDevice.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Pluggable Device and Driver Manager, Device parts.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PDM_DEVICE
#define PDMPCIDEV_INCLUDE_PRIVATE  /* Hack to get pdmpcidevint.h included at the right point. */
#include "PDMInternal.h"
#include <VBox/vmm/pdm.h>
#if defined(VBOX_VMM_TARGET_ARMV8)
# include <VBox/vmm/gic.h>
#else
# include <VBox/vmm/apic.h>
#endif
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/vmm.h>

#include <VBox/version.h>
#include <VBox/log.h>
#include <VBox/msi.h>
#include <VBox/err.h>
#include <iprt/alloc.h>
#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Internal callback structure pointer.
 * The main purpose is to define the extra data we associate
 * with PDMDEVREGCB so we can find the VM instance and so on.
 */
typedef struct PDMDEVREGCBINT
{
    /** The callback structure. */
    PDMDEVREGCB     Core;
    /** A bit of padding. */
    uint32_t        u32[4];
    /** VM Handle. */
    PVM             pVM;
    /** Pointer to the configuration node the registrations should be
     * associated with.  Can be NULL. */
    PCFGMNODE       pCfgNode;
} PDMDEVREGCBINT;
/** Pointer to a PDMDEVREGCBINT structure. */
typedef PDMDEVREGCBINT *PPDMDEVREGCBINT;
/** Pointer to a const PDMDEVREGCBINT structure. */
typedef const PDMDEVREGCBINT *PCPDMDEVREGCBINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)    pdmR3DevReg_Register(PPDMDEVREGCB pCallbacks, PCPDMDEVREG pReg);
static int                  pdmR3DevLoadModules(PVM pVM);
static int                  pdmR3DevLoad(PVM pVM, PPDMDEVREGCBINT pRegCB, const char *pszFilename, const char *pszName);




/**
 * This function will initialize the devices for this VM instance.
 *
 *
 * First of all this mean loading the builtin device and letting them
 * register themselves. Beyond that any additional device modules are
 * loaded and called for registration.
 *
 * Then the device configuration is enumerated, the instantiation order
 * is determined, and finally they are instantiated.
 *
 * After all devices have been successfully instantiated the primary
 * PCI Bus device is called to emulate the PCI BIOS, i.e. making the
 * resource assignments. If there is no PCI device, this step is of course
 * skipped.
 *
 * Finally the init completion routines of the instantiated devices
 * are called.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
int pdmR3DevInit(PVM pVM)
{
    LogFlow(("pdmR3DevInit:\n"));

    AssertRelease(!(RT_UOFFSETOF(PDMDEVINS, achInstanceData) & 15));
    AssertRelease(sizeof(pVM->pdm.s.pDevInstances->Internal.s) <= sizeof(pVM->pdm.s.pDevInstances->Internal.padding));

    /*
     * Load device modules.
     */
    int rc = pdmR3DevLoadModules(pVM);
    if (RT_FAILURE(rc))
        return rc;

#ifdef VBOX_WITH_USB
    /* ditto for USB Devices. */
    rc = pdmR3UsbLoadModules(pVM);
    if (RT_FAILURE(rc))
        return rc;
#endif

    /*
     * Get the RC & R0 devhlps and create the devhlp R3 task queue.
     */
    rc = PDMR3QueueCreateInternal(pVM, sizeof(PDMDEVHLPTASK), pVM->cCpus * 8, 0, pdmR3DevHlpQueueConsumer, true, "DevHlp",
                                  &pVM->pdm.s.hDevHlpQueue);
    AssertRCReturn(rc, rc);

    /*
     *
     * Enumerate the device instance configurations
     * and come up with a instantiation order.
     *
     */
    /* Switch to /Devices, which contains the device instantiations. */
    PCFGMNODE pDevicesNode = CFGMR3GetChild(CFGMR3GetRoot(pVM), "Devices");

    /*
     * Count the device instances.
     */
    PCFGMNODE pCur;
    PCFGMNODE pInstanceNode;
    unsigned cDevs = 0;
    for (pCur = CFGMR3GetFirstChild(pDevicesNode); pCur; pCur = CFGMR3GetNextChild(pCur))
        for (pInstanceNode = CFGMR3GetFirstChild(pCur); pInstanceNode; pInstanceNode = CFGMR3GetNextChild(pInstanceNode))
            cDevs++;
    if (!cDevs)
    {
        Log(("PDM: No devices were configured!\n"));
        return VINF_SUCCESS;
    }
    Log2(("PDM: cDevs=%u\n", cDevs));

    /*
     * Collect info on each device instance.
     */
    struct DEVORDER
    {
        /** Configuration node. */
        PCFGMNODE   pNode;
        /** Pointer to device. */
        PPDMDEV     pDev;
        /** Init order. */
        uint32_t    u32Order;
        /** VBox instance number. */
        uint32_t    iInstance;
    } *paDevs = (struct DEVORDER *)alloca(sizeof(paDevs[0]) * (cDevs + 1)); /* (One extra for swapping) */
    Assert(paDevs);
    unsigned i = 0;
    for (pCur = CFGMR3GetFirstChild(pDevicesNode); pCur; pCur = CFGMR3GetNextChild(pCur))
    {
        /* Get the device name. */
        char szName[sizeof(paDevs[0].pDev->pReg->szName)];
        rc = CFGMR3GetName(pCur, szName, sizeof(szName));
        AssertMsgRCReturn(rc, ("Configuration error: device name is too long (or something)! rc=%Rrc\n", rc), rc);

        /* Find the device. */
        PPDMDEV pDev = pdmR3DevLookup(pVM, szName);
        AssertLogRelMsgReturn(pDev, ("Configuration error: device '%s' not found!\n", szName), VERR_PDM_DEVICE_NOT_FOUND);

        /* Configured priority or use default based on device class? */
        uint32_t u32Order;
        rc = CFGMR3QueryU32(pCur, "Priority", &u32Order);
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        {
            uint32_t u32 = pDev->pReg->fClass;
            for (u32Order = 1; !(u32 & u32Order); u32Order <<= 1)
                /* nop */;
        }
        else
            AssertMsgRCReturn(rc, ("Configuration error: reading \"Priority\" for the '%s' device failed rc=%Rrc!\n", szName, rc), rc);

        /* Enumerate the device instances. */
        uint32_t const iStart = i;
        for (pInstanceNode = CFGMR3GetFirstChild(pCur); pInstanceNode; pInstanceNode = CFGMR3GetNextChild(pInstanceNode))
        {
            paDevs[i].pNode = pInstanceNode;
            paDevs[i].pDev = pDev;
            paDevs[i].u32Order = u32Order;

            /* Get the instance number. */
            char szInstance[32];
            rc = CFGMR3GetName(pInstanceNode, szInstance, sizeof(szInstance));
            AssertMsgRCReturn(rc, ("Configuration error: instance name is too long (or something)! rc=%Rrc\n", rc), rc);
            char *pszNext = NULL;
            rc = RTStrToUInt32Ex(szInstance, &pszNext, 0, &paDevs[i].iInstance);
            AssertMsgRCReturn(rc, ("Configuration error: RTStrToInt32Ex failed on the instance name '%s'! rc=%Rrc\n", szInstance, rc), rc);
            AssertMsgReturn(!*pszNext, ("Configuration error: the instance name '%s' isn't all digits. (%s)\n", szInstance, pszNext), VERR_INVALID_PARAMETER);

            /* next instance */
            i++;
        }

        /* check the number of instances */
        if (i - iStart > pDev->pReg->cMaxInstances)
            AssertLogRelMsgFailedReturn(("Configuration error: Too many instances of %s was configured: %u, max %u\n",
                                         szName, i - iStart, pDev->pReg->cMaxInstances),
                                        VERR_PDM_TOO_MANY_DEVICE_INSTANCES);
    } /* devices */
    Assert(i == cDevs);

    /*
     * Sort (bubble) the device array ascending on u32Order and instance number
     * for a device.
     */
    unsigned c = cDevs - 1;
    while (c)
    {
        unsigned j = 0;
        for (i = 0; i < c; i++)
            if (   paDevs[i].u32Order > paDevs[i + 1].u32Order
                || (   paDevs[i].u32Order  == paDevs[i + 1].u32Order
                    && paDevs[i].iInstance >  paDevs[i + 1].iInstance
                    && paDevs[i].pDev      == paDevs[i + 1].pDev) )
            {
                paDevs[cDevs] = paDevs[i + 1];
                paDevs[i + 1] = paDevs[i];
                paDevs[i] = paDevs[cDevs];
                j = i;
            }
        c = j;
    }


    /*
     *
     * Instantiate the devices.
     *
     */
    for (i = 0; i < cDevs; i++)
    {
        PDMDEVREGR3 const * const pReg = paDevs[i].pDev->pReg;

        /*
         * Gather a bit of config.
         */
        /* trusted */
        bool fTrusted;
        rc = CFGMR3QueryBool(paDevs[i].pNode, "Trusted", &fTrusted);
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
            fTrusted = false;
        else if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("configuration error: failed to query boolean \"Trusted\", rc=%Rrc\n", rc));
            return rc;
        }

        /* RZEnabled, R0Enabled, RCEnabled*/
        bool fR0Enabled = false;
        bool fRCEnabled = false;
        if (   (pReg->fFlags & (PDM_DEVREG_FLAGS_R0 | PDM_DEVREG_FLAGS_RC))
#ifdef VBOX_WITH_PGM_NEM_MODE
            && !PGMR3IsNemModeEnabled(pVM) /* No ring-0 in simplified memory mode. */
#endif
            && !SUPR3IsDriverless())
        {
            if (pReg->fFlags & PDM_DEVREG_FLAGS_R0)
            {
                if (pReg->fFlags & PDM_DEVREG_FLAGS_REQUIRE_R0)
                    fR0Enabled = true;
                else
                {
                    rc = CFGMR3QueryBoolDef(paDevs[i].pNode, "R0Enabled", &fR0Enabled,
                                            !(pReg->fFlags & PDM_DEVREG_FLAGS_OPT_IN_R0));
                    AssertLogRelRCReturn(rc, rc);
                }
            }

            if (pReg->fFlags & PDM_DEVREG_FLAGS_RC)
            {
                if (pReg->fFlags & PDM_DEVREG_FLAGS_REQUIRE_RC)
                    fRCEnabled = true;
                else
                {
                    rc = CFGMR3QueryBoolDef(paDevs[i].pNode, "RCEnabled", &fRCEnabled,
                                            !(pReg->fFlags & PDM_DEVREG_FLAGS_OPT_IN_RC));
                    AssertLogRelRCReturn(rc, rc);
                }
                fRCEnabled = false;
            }
        }

#ifdef VBOX_WITH_DBGF_TRACING
        DBGFTRACEREVTSRC hDbgfTraceEvtSrc = NIL_DBGFTRACEREVTSRC;
        bool fTracingEnabled = false;
        bool fGCPhysRwAll = false;
        rc = CFGMR3QueryBoolDef(paDevs[i].pNode, "TracingEnabled", &fTracingEnabled,
                                false);
        AssertLogRelRCReturn(rc, rc);
        if (fTracingEnabled)
        {
            rc = CFGMR3QueryBoolDef(paDevs[i].pNode, "TraceAllGstMemRw", &fGCPhysRwAll,
                                    false);
            AssertLogRelRCReturn(rc, rc);

            /* Traced devices need to be trusted for now. */
            if (fTrusted)
            {
                rc = DBGFR3TracerRegisterEvtSrc(pVM, pReg->szName, &hDbgfTraceEvtSrc);
                AssertLogRelRCReturn(rc, rc);
            }
            else
                AssertMsgFailedReturn(("configuration error: Device tracing needs a trusted device\n"), VERR_INCOMPATIBLE_CONFIG);
        }
#endif

        /* config node */
        PCFGMNODE pConfigNode = CFGMR3GetChild(paDevs[i].pNode, "Config");
        if (!pConfigNode)
        {
            rc = CFGMR3InsertNode(paDevs[i].pNode, "Config", &pConfigNode);
            if (RT_FAILURE(rc))
            {
                AssertMsgFailed(("Failed to create Config node! rc=%Rrc\n", rc));
                return rc;
            }
        }
        CFGMR3SetRestrictedRoot(pConfigNode);

        /*
         * Allocate the device instance and critical section.
         */
        AssertLogRelReturn(paDevs[i].pDev->cInstances < pReg->cMaxInstances,
                           VERR_PDM_TOO_MANY_DEVICE_INSTANCES);
        PPDMDEVINS   pDevIns;
        PPDMCRITSECT pCritSect;
        if (fR0Enabled || fRCEnabled)
        {
            AssertLogRel(fR0Enabled /* not possible to just enabled raw-mode atm. */);

            rc = PDMR3LdrLoadR0(pVM->pUVM, pReg->pszR0Mod, paDevs[i].pDev->pszR0SearchPath);
            if (RT_FAILURE(rc))
                return VMR3SetError(pVM->pUVM, rc, RT_SRC_POS, "Failed to load ring-0 module '%s' for device '%s'",
                                    pReg->pszR0Mod, pReg->szName);

            PDMDEVICECREATEREQ Req;
            Req.Hdr.u32Magic      = SUPVMMR0REQHDR_MAGIC;
            Req.Hdr.cbReq         = sizeof(Req);
            Req.pDevInsR3         = NULL;
            /** @todo Add tracer id in request so R0 can set up DEVINSR0 properly. */
            Req.fFlags            = pReg->fFlags;
            Req.fClass            = pReg->fClass;
            Req.cMaxInstances     = pReg->cMaxInstances;
            Req.uSharedVersion    = pReg->uSharedVersion;
            Req.cbInstanceShared  = pReg->cbInstanceShared;
            Req.cbInstanceR3      = pReg->cbInstanceCC;
            Req.cbInstanceRC      = pReg->cbInstanceRC;
            Req.cMaxPciDevices    = pReg->cMaxPciDevices;
            Req.cMaxMsixVectors   = pReg->cMaxMsixVectors;
            Req.iInstance         = paDevs[i].iInstance;
            Req.fRCEnabled        = fRCEnabled;
            Req.afReserved[0]     = false;
            Req.afReserved[1]     = false;
            Req.afReserved[2]     = false;
#ifdef VBOX_WITH_DBGF_TRACING
            Req.hDbgfTracerEvtSrc = hDbgfTraceEvtSrc;
#else
            Req.hDbgfTracerEvtSrc = NIL_DBGFTRACEREVTSRC;
#endif
            rc = RTStrCopy(Req.szDevName, sizeof(Req.szDevName), pReg->szName);
            AssertLogRelRCReturn(rc, rc);
            rc = RTStrCopy(Req.szModName, sizeof(Req.szModName), pReg->pszR0Mod);
            AssertLogRelRCReturn(rc, rc);

            rc = VMMR3CallR0Emt(pVM, pVM->apCpusR3[0], VMMR0_DO_PDM_DEVICE_CREATE, 0, &Req.Hdr);
            AssertLogRelMsgRCReturn(rc, ("VMMR0_DO_PDM_DEVICE_CREATE for %s failed: %Rrc\n", pReg->szName, rc), rc);

            pDevIns = Req.pDevInsR3;
            pCritSect = pDevIns->pCritSectRoR3;

            Assert(pDevIns->Internal.s.fIntFlags & PDMDEVINSINT_FLAGS_R0_ENABLED);
            AssertLogRelReturn(pDevIns->Internal.s.idxR0Device < PDM_MAX_RING0_DEVICE_INSTANCES, VERR_PDM_DEV_IPE_1);
            AssertLogRelReturn(pVM->pdm.s.apDevRing0Instances[pDevIns->Internal.s.idxR0Device] == pDevIns, VERR_PDM_DEV_IPE_1);
        }
        else
        {
            /* The code in this else branch works by the same rules as the PDMR0Device.cpp
               code, except there is only the ring-3 components of the device instance.
               Changes here may need to be reflected in PDMR0DEvice.cpp and vice versa! */
            uint32_t cb = RT_UOFFSETOF_DYN(PDMDEVINS, achInstanceData[pReg->cbInstanceCC]);
            cb  = RT_ALIGN_32(cb, 64);
            uint32_t const offShared   = cb;
            cb += RT_ALIGN_32(pReg->cbInstanceShared, 64);
            uint32_t const cbCritSect  = RT_ALIGN_32(sizeof(*pCritSect), 64);
            cb += cbCritSect;
            uint32_t const cbMsixState = RT_ALIGN_32(pReg->cMaxMsixVectors * 16 + (pReg->cMaxMsixVectors + 7) / 8, _4K);
            uint32_t const cbPciDev    = RT_ALIGN_32(RT_UOFFSETOF_DYN(PDMPCIDEV, abMsixState[cbMsixState]), 64);
            uint32_t const cPciDevs    = RT_MIN(pReg->cMaxPciDevices, 1024);
            uint32_t const cbPciDevs   = cbPciDev * cPciDevs;
            cb += cbPciDevs;
            AssertLogRelMsgReturn(cb <= PDM_MAX_DEVICE_INSTANCE_SIZE_R3,
                                  ("Device %s total instance size is to big: %u, max %u\n",
                                   pReg->szName, cb, PDM_MAX_DEVICE_INSTANCE_SIZE_R3),
                                  VERR_ALLOCATION_TOO_BIG);

#if 0  /* Several devices demands cacheline aligned data, if not page aligned. Real problem in NEM mode. */
            rc = MMR3HeapAllocZEx(pVM, MM_TAG_PDM_DEVICE, cb, (void **)&pDevIns);
            AssertLogRelMsgRCReturn(rc, ("Failed to allocate %zu bytes of instance data for device '%s'. rc=%Rrc\n",
                                         cb, pReg->szName, rc), rc);
#else
            pDevIns = (PPDMDEVINS)RTMemPageAllocZ(cb);
            AssertLogRelMsgReturn(pDevIns, ("Failed to allocate %zu bytes of instance data for device '%s'\n", cb, pReg->szName),
                                  VERR_NO_PAGE_MEMORY);
#endif

            /* Initialize it: */
            pDevIns->u32Version             = PDM_DEVINSR3_VERSION;
            pDevIns->iInstance              = paDevs[i].iInstance;
            pDevIns->cbRing3                = cb;
            //pDevIns->fR0Enabled           = false;
            //pDevIns->fRCEnabled           = false;
            pDevIns->pvInstanceDataR3       = (uint8_t *)pDevIns + offShared;
            pDevIns->pvInstanceDataForR3    = &pDevIns->achInstanceData[0];
            pCritSect = (PPDMCRITSECT)((uint8_t *)pDevIns + offShared + RT_ALIGN_32(pReg->cbInstanceShared, 64));
            pDevIns->pCritSectRoR3          = pCritSect;
            pDevIns->cbPciDev               = cbPciDev;
            pDevIns->cPciDevs               = cPciDevs;
            for (uint32_t iPciDev = 0; iPciDev < cPciDevs; iPciDev++)
            {
                PPDMPCIDEV pPciDev = (PPDMPCIDEV)((uint8_t *)pDevIns->pCritSectRoR3 + cbCritSect + cbPciDev * iPciDev);
                if (iPciDev < RT_ELEMENTS(pDevIns->apPciDevs))
                    pDevIns->apPciDevs[iPciDev] = pPciDev;
                pPciDev->cbConfig           = _4K;
                pPciDev->cbMsixState        = cbMsixState;
                pPciDev->idxSubDev          = (uint16_t)iPciDev;
                pPciDev->Int.s.idxSubDev    = (uint16_t)iPciDev;
                pPciDev->u32Magic           = PDMPCIDEV_MAGIC;
            }
        }

        pDevIns->pHlpR3                         = fTrusted ? &g_pdmR3DevHlpTrusted : &g_pdmR3DevHlpUnTrusted;
        pDevIns->pReg                           = pReg;
        pDevIns->pCfg                           = pConfigNode;
        //pDevIns->IBase.pfnQueryInterface        = NULL;
        //pDevIns->fTracing                       = 0;
        pDevIns->idTracing                      = ++pVM->pdm.s.idTracingDev;

        //pDevIns->Internal.s.pNextR3             = NULL;
        //pDevIns->Internal.s.pPerDeviceNextR3    = NULL;
        pDevIns->Internal.s.pDevR3              = paDevs[i].pDev;
        //pDevIns->Internal.s.pLunsR3             = NULL;
        //pDevIns->Internal.s.pfnAsyncNotify      = NULL;
        pDevIns->Internal.s.pCfgHandle          = paDevs[i].pNode;
        pDevIns->Internal.s.pVMR3               = pVM;
#ifdef VBOX_WITH_DBGF_TRACING
        pDevIns->Internal.s.hDbgfTraceEvtSrc    = hDbgfTraceEvtSrc;
#else
        pDevIns->Internal.s.hDbgfTraceEvtSrc    = NIL_DBGFTRACEREVTSRC;
#endif
        //pDevIns->Internal.s.pHeadPciDevR3       = NULL;
        pDevIns->Internal.s.fIntFlags          |= PDMDEVINSINT_FLAGS_SUSPENDED;
        //pDevIns->Internal.s.uLastIrqTag         = 0;

        rc = pdmR3CritSectInitDeviceAuto(pVM, pDevIns, pCritSect, RT_SRC_POS,
                                         "%s#%uAuto", pDevIns->pReg->szName, pDevIns->iInstance);
        AssertLogRelRCReturn(rc, rc);

        /*
         * Link it into all the lists.
         */
        RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

        /* The global instance FIFO. */
        PPDMDEVINS pPrev1 = pVM->pdm.s.pDevInstances;
        if (!pPrev1)
            pVM->pdm.s.pDevInstances = pDevIns;
        else
        {
            while (pPrev1->Internal.s.pNextR3)
                pPrev1 = pPrev1->Internal.s.pNextR3;
            pPrev1->Internal.s.pNextR3 = pDevIns;
        }

        /* The per device instance FIFO. */
        PPDMDEVINS pPrev2 = paDevs[i].pDev->pInstances;
        if (!pPrev2)
            paDevs[i].pDev->pInstances = pDevIns;
        else
        {
            while (pPrev2->Internal.s.pPerDeviceNextR3)
                pPrev2 = pPrev2->Internal.s.pPerDeviceNextR3;
            pPrev2->Internal.s.pPerDeviceNextR3 = pDevIns;
        }

#ifdef VBOX_WITH_DBGF_TRACING
        /*
         * Allocate memory for the MMIO/IO port registration tracking if DBGF tracing is enabled.
         */
        if (hDbgfTraceEvtSrc != NIL_DBGFTRACEREVTSRC)
        {
            pDevIns->Internal.s.paDbgfTraceTrack = (PPDMDEVINSDBGFTRACK)RTMemAllocZ(PDM_MAX_DEVICE_DBGF_TRACING_TRACK);
            if (!pDevIns->Internal.s.paDbgfTraceTrack)
            {
                LogRel(("PDM: Failed to construct '%s'/%d! %Rra\n", pDevIns->pReg->szName, pDevIns->iInstance, VERR_NO_MEMORY));
                if (VMR3GetErrorCount(pVM->pUVM) == 0)
                    VMSetError(pVM, rc, RT_SRC_POS, "Failed to construct device '%s' instance #%u",
                               pDevIns->pReg->szName, pDevIns->iInstance);
                RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
                return VERR_NO_MEMORY;
            }

            pDevIns->Internal.s.idxDbgfTraceTrackNext = 0;
            pDevIns->Internal.s.cDbgfTraceTrackMax = PDM_MAX_DEVICE_DBGF_TRACING_TRACK / sizeof(PDMDEVINSDBGFTRACK);
            pDevIns->pHlpR3 = &g_pdmR3DevHlpTracing;
        }
#endif

        /*
         * Call the constructor.
         */
        paDevs[i].pDev->cInstances++;
        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

        Log(("PDM: Constructing device '%s' instance %d...\n", pDevIns->pReg->szName, pDevIns->iInstance));
        rc = pDevIns->pReg->pfnConstruct(pDevIns, pDevIns->iInstance, pDevIns->pCfg);
        if (RT_FAILURE(rc))
        {
            LogRel(("PDM: Failed to construct '%s'/%d! %Rra\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
            if (VMR3GetErrorCount(pVM->pUVM) == 0)
                VMSetError(pVM, rc, RT_SRC_POS, "Failed to construct device '%s' instance #%u",
                           pDevIns->pReg->szName, pDevIns->iInstance);
            /* Because we're damn lazy, the destructor will be called even if
               the constructor fails.  So, no unlinking. */
            //paDevs[i].pDev->cInstances--;
            return rc == VERR_VERSION_MISMATCH ? VERR_PDM_DEVICE_VERSION_MISMATCH : rc;
        }

        /*
         * Call the ring-0 constructor if applicable.
         */
        if (fR0Enabled)
        {
            PDMDEVICEGENCALLREQ Req;
            RT_ZERO(Req.Params);
            Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
            Req.Hdr.cbReq    = sizeof(Req);
            Req.enmCall      = PDMDEVICEGENCALL_CONSTRUCT;
            Req.idxR0Device  = pDevIns->Internal.s.idxR0Device;
            Req.pDevInsR3    = pDevIns;
            rc = VMMR3CallR0Emt(pVM, pVM->apCpusR3[0], VMMR0_DO_PDM_DEVICE_GEN_CALL, 0, &Req.Hdr);
            pDevIns->Internal.s.fIntFlags |= PDMDEVINSINT_FLAGS_R0_CONTRUCT;
            if (RT_FAILURE(rc))
            {
                LogRel(("PDM: Failed to construct (ring-0) '%s'/%d! %Rra\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
                if (VMR3GetErrorCount(pVM->pUVM) == 0)
                    VMSetError(pVM, rc, RT_SRC_POS, "The ring-0 constructor of device '%s' instance #%u failed",
                               pDevIns->pReg->szName, pDevIns->iInstance);
                /* No unlinking, see above. */
                //paDevs[i].pDev->cInstances--;
                return rc == VERR_VERSION_MISMATCH ? VERR_PDM_DEVICE_VERSION_MISMATCH : rc;
            }
        }

    } /* for device instances */

#ifdef VBOX_WITH_USB
    /* ditto for USB Devices. */
    rc = pdmR3UsbInstantiateDevices(pVM);
    if (RT_FAILURE(rc))
        return rc;
#endif

    LogFlow(("pdmR3DevInit: returns %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}


/**
 * Performs the init complete callback after ring-0 and raw-mode has been
 * initialized.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
int pdmR3DevInitComplete(PVM pVM)
{
    int rc;

    /*
     * Iterate thru the device instances and work the callback.
     */
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    for (PPDMDEVINS pDevIns = pVM->pdm.s.pDevInstances; pDevIns; pDevIns = pDevIns->Internal.s.pNextR3)
    {
        if (pDevIns->pReg->pfnInitComplete)
        {
            RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
            PDMCritSectEnter(pVM, pDevIns->pCritSectRoR3, VERR_IGNORED);
            rc = pDevIns->pReg->pfnInitComplete(pDevIns);
            PDMCritSectLeave(pVM, pDevIns->pCritSectRoR3);
            if (RT_FAILURE(rc))
            {
                AssertMsgFailed(("InitComplete on device '%s'/%d failed with rc=%Rrc\n",
                                 pDevIns->pReg->szName, pDevIns->iInstance, rc));
                return rc;
            }
            RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
        }
    }
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);

#ifdef VBOX_WITH_USB
    rc = pdmR3UsbVMInitComplete(pVM);
    if (RT_FAILURE(rc))
    {
        Log(("pdmR3DevInit: returns %Rrc\n", rc));
        return rc;
    }
#endif

    LogFlow(("pdmR3DevInit: returns %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}


/**
 * Lookups a device structure by name.
 * @internal
 */
PPDMDEV pdmR3DevLookup(PVM pVM, const char *pszName)
{
    size_t const cchName = strlen(pszName);
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    for (PPDMDEV pDev = pVM->pdm.s.pDevs; pDev; pDev = pDev->pNext)
        if (   pDev->cchName == cchName
            && !strcmp(pDev->pReg->szName, pszName))
        {
            RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
            return pDev;
        }
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
    return NULL;
}


/**
 * Loads the device modules.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
static int pdmR3DevLoadModules(PVM pVM)
{
    /*
     * Initialize the callback structure.
     */
    PDMDEVREGCBINT RegCB;
    RegCB.Core.u32Version  = PDM_DEVREG_CB_VERSION;
    RegCB.Core.pfnRegister = pdmR3DevReg_Register;
    RegCB.pVM              = pVM;
    RegCB.pCfgNode         = NULL;

#if defined(VBOX_VMM_TARGET_ARMV8)
    /*
     * Register the internal VMM GIC device.
     */
    int rc = pdmR3DevReg_Register(&RegCB.Core, &g_DeviceGIC);
    AssertRCReturn(rc, rc);

    /*
     * Register the internal VMM GIC device, NEM variant.
     */
    rc = pdmR3DevReg_Register(&RegCB.Core, &g_DeviceGICNem);
    AssertRCReturn(rc, rc);
#else
    /*
     * Register the internal VMM APIC device.
     */
    int rc = pdmR3DevReg_Register(&RegCB.Core, &g_DeviceAPIC);
    AssertRCReturn(rc, rc);
#endif

    /*
     * Load the builtin module.
     */
    PCFGMNODE pDevicesNode = CFGMR3GetChild(CFGMR3GetRoot(pVM), "PDM/Devices");
    bool fLoadBuiltin;
    rc = CFGMR3QueryBool(pDevicesNode, "LoadBuiltin", &fLoadBuiltin);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
        fLoadBuiltin = true;
    else if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: Querying boolean \"LoadBuiltin\" failed with %Rrc\n", rc));
        return rc;
    }
    if (fLoadBuiltin)
    {
        /* make filename */
        char *pszFilename = pdmR3FileR3("VBoxDD", true /*fShared*/);
        if (!pszFilename)
            return VERR_NO_TMP_MEMORY;
        rc = pdmR3DevLoad(pVM, &RegCB, pszFilename, "VBoxDD");
        RTMemTmpFree(pszFilename);
        if (RT_FAILURE(rc))
            return rc;

        /* make filename */
        pszFilename = pdmR3FileR3("VBoxDD2", true /*fShared*/);
        if (!pszFilename)
            return VERR_NO_TMP_MEMORY;
        rc = pdmR3DevLoad(pVM, &RegCB, pszFilename, "VBoxDD2");
        RTMemTmpFree(pszFilename);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Load additional device modules.
     */
    PCFGMNODE pCur;
    for (pCur = CFGMR3GetFirstChild(pDevicesNode); pCur; pCur = CFGMR3GetNextChild(pCur))
    {
        /*
         * Get the name and path.
         */
        char szName[PDMMOD_NAME_LEN];
        rc = CFGMR3GetName(pCur, &szName[0], sizeof(szName));
        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
        {
            AssertMsgFailed(("configuration error: The module name is too long, cchName=%zu.\n", CFGMR3GetNameLen(pCur)));
            return VERR_PDM_MODULE_NAME_TOO_LONG;
        }
        else if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("CFGMR3GetName -> %Rrc.\n", rc));
            return rc;
        }

        /* the path is optional, if no path the module name + path is used. */
        char szFilename[RTPATH_MAX];
        rc = CFGMR3QueryString(pCur, "Path", &szFilename[0], sizeof(szFilename));
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
            strcpy(szFilename, szName);
        else if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("configuration error: Failure to query the module path, rc=%Rrc.\n", rc));
            return rc;
        }

        /* prepend path? */
        if (!RTPathHavePath(szFilename))
        {
            char *psz = pdmR3FileR3(szFilename, false /*fShared*/);
            if (!psz)
                return VERR_NO_TMP_MEMORY;
            size_t cch = strlen(psz) + 1;
            if (cch > sizeof(szFilename))
            {
                RTMemTmpFree(psz);
                AssertMsgFailed(("Filename too long! cch=%d '%s'\n", cch, psz));
                return VERR_FILENAME_TOO_LONG;
            }
            memcpy(szFilename, psz, cch);
            RTMemTmpFree(psz);
        }

        /*
         * Load the module and register it's devices.
         */
        RegCB.pCfgNode = pCur;
        rc = pdmR3DevLoad(pVM, &RegCB, szFilename, szName);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Loads one device module and call the registration entry point.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pRegCB          The registration callback stuff.
 * @param   pszFilename     Module filename.
 * @param   pszName         Module name.
 */
static int pdmR3DevLoad(PVM pVM, PPDMDEVREGCBINT pRegCB, const char *pszFilename, const char *pszName)
{
    /*
     * Load it.
     */
    int rc = pdmR3LoadR3U(pVM->pUVM, pszFilename, pszName);
    if (RT_SUCCESS(rc))
    {
        /*
         * Get the registration export and call it.
         */
        FNPDMVBOXDEVICESREGISTER *pfnVBoxDevicesRegister;
        rc = PDMR3LdrGetSymbolR3(pVM, pszName, "VBoxDevicesRegister", (void **)&pfnVBoxDevicesRegister);
        if (RT_SUCCESS(rc))
        {
            Log(("PDM: Calling VBoxDevicesRegister (%p) of %s (%s)\n", pfnVBoxDevicesRegister, pszName, pszFilename));
            rc = pfnVBoxDevicesRegister(&pRegCB->Core, VBOX_VERSION);
            if (RT_SUCCESS(rc))
                Log(("PDM: Successfully loaded device module %s (%s).\n", pszName, pszFilename));
            else
            {
                VMR3SetError(pVM->pUVM, rc, RT_SRC_POS, "VBoxDevicesRegister failed with rc=%Rrc for module %s (%s)",
                             rc, pszName, pszFilename);
                AssertMsgFailed(("VBoxDevicesRegister failed with rc=%Rrc for module %s (%s)\n", rc, pszName, pszFilename));
            }
        }
        else
        {
            AssertMsgFailed(("Failed to locate 'VBoxDevicesRegister' in %s (%s) rc=%Rrc\n", pszName, pszFilename, rc));
            if (rc == VERR_SYMBOL_NOT_FOUND)
                rc = VERR_PDM_NO_REGISTRATION_EXPORT;
            VMR3SetError(pVM->pUVM, rc, RT_SRC_POS, "Failed to locate 'VBoxDevicesRegister' in %s (%s) rc=%Rrc",
                         pszName, pszFilename, rc);
        }
    }
    else
        AssertMsgFailed(("Failed to load %s %s!\n", pszFilename, pszName));
    return rc;
}


/**
 * @interface_method_impl{PDMDEVREGCB,pfnRegister}
 */
static DECLCALLBACK(int) pdmR3DevReg_Register(PPDMDEVREGCB pCallbacks, PCPDMDEVREG pReg)
{
    /*
     * Validate the registration structure.
     */
    Assert(pReg);
    AssertMsgReturn(pReg->u32Version == PDM_DEVREG_VERSION,
                    ("Unknown struct version %#x!\n", pReg->u32Version),
                    VERR_PDM_UNKNOWN_DEVREG_VERSION);

    AssertMsgReturn(    pReg->szName[0]
                    &&  strlen(pReg->szName) < sizeof(pReg->szName)
                    &&  pdmR3IsValidName(pReg->szName),
                    ("Invalid name '%.*s'\n", sizeof(pReg->szName), pReg->szName),
                    VERR_PDM_INVALID_DEVICE_REGISTRATION);
    AssertMsgReturn(   !(pReg->fFlags & PDM_DEVREG_FLAGS_RC)
                    || (   pReg->pszRCMod[0]
                        && strlen(pReg->pszRCMod) < RT_SIZEOFMEMB(PDMDEVICECREATEREQ, szModName)),
                    ("Invalid GC module name '%s' - (Device %s)\n", pReg->pszRCMod, pReg->szName),
                    VERR_PDM_INVALID_DEVICE_REGISTRATION);
    AssertMsgReturn(   !(pReg->fFlags & PDM_DEVREG_FLAGS_R0)
                    || (   pReg->pszR0Mod[0]
                        && strlen(pReg->pszR0Mod) < RT_SIZEOFMEMB(PDMDEVICECREATEREQ, szModName)),
                    ("Invalid R0 module name '%s' - (Device %s)\n", pReg->pszR0Mod, pReg->szName),
                    VERR_PDM_INVALID_DEVICE_REGISTRATION);
    AssertMsgReturn((pReg->fFlags & PDM_DEVREG_FLAGS_HOST_BITS_MASK) == PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT,
                    ("Invalid host bits flags! fFlags=%#x (Device %s)\n", pReg->fFlags, pReg->szName),
                    VERR_PDM_INVALID_DEVICE_HOST_BITS);
    AssertMsgReturn((pReg->fFlags & PDM_DEVREG_FLAGS_GUEST_BITS_MASK),
                    ("Invalid guest bits flags! fFlags=%#x (Device %s)\n", pReg->fFlags, pReg->szName),
                    VERR_PDM_INVALID_DEVICE_REGISTRATION);
    AssertMsgReturn(pReg->fClass,
                    ("No class! (Device %s)\n", pReg->szName),
                    VERR_PDM_INVALID_DEVICE_REGISTRATION);
    AssertMsgReturn(pReg->cMaxInstances > 0,
                    ("Max instances %u! (Device %s)\n", pReg->cMaxInstances, pReg->szName),
                    VERR_PDM_INVALID_DEVICE_REGISTRATION);
    uint32_t const cbMaxInstance = pReg->fFlags & (PDM_DEVREG_FLAGS_RC | PDM_DEVREG_FLAGS_R0)
                                 ? PDM_MAX_DEVICE_INSTANCE_SIZE : PDM_MAX_DEVICE_INSTANCE_SIZE_R3;
    AssertMsgReturn(pReg->cbInstanceShared <= cbMaxInstance,
                    ("Instance size %u bytes! (Max %u; Device %s)\n", pReg->cbInstanceShared, cbMaxInstance, pReg->szName),
                    VERR_PDM_INVALID_DEVICE_REGISTRATION);
    AssertMsgReturn(pReg->cbInstanceCC <= cbMaxInstance,
                    ("Instance size %d bytes! (Max %u; Device %s)\n", pReg->cbInstanceCC, cbMaxInstance, pReg->szName),
                    VERR_PDM_INVALID_DEVICE_REGISTRATION);
    AssertMsgReturn(pReg->pfnConstruct,
                    ("No constructor! (Device %s)\n", pReg->szName),
                    VERR_PDM_INVALID_DEVICE_REGISTRATION);
    AssertLogRelMsgReturn((pReg->fFlags & PDM_DEVREG_FLAGS_GUEST_BITS_MASK) == PDM_DEVREG_FLAGS_GUEST_BITS_DEFAULT,
                          ("PDM: Rejected device '%s' because it didn't match the guest bits.\n", pReg->szName),
                          VERR_PDM_INVALID_DEVICE_GUEST_BITS);
    AssertLogRelMsg(pReg->u32VersionEnd == PDM_DEVREG_VERSION,
                    ("u32VersionEnd=%#x, expected %#x. (szName=%s)\n",
                     pReg->u32VersionEnd, PDM_DEVREG_VERSION, pReg->szName));
    AssertLogRelMsgReturn(pReg->cMaxPciDevices <= 8, ("%#x (szName=%s)\n", pReg->cMaxPciDevices, pReg->szName),
                          VERR_PDM_INVALID_DEVICE_REGISTRATION);
    AssertLogRelMsgReturn(pReg->cMaxMsixVectors <= VBOX_MSIX_MAX_ENTRIES,
                          ("%#x (szName=%s)\n", pReg->cMaxMsixVectors, pReg->szName),
                          VERR_PDM_INVALID_DEVICE_REGISTRATION);
    AssertLogRelMsgReturn(pReg->fFlags & PDM_DEVREG_FLAGS_NEW_STYLE /* the flag is required now */,
                          ("PDM_DEVREG_FLAGS_NEW_STYLE not set for szName=%s!\n", pReg->szName),
                          VERR_PDM_INVALID_DEVICE_REGISTRATION);

    /*
     * Check for duplicate and find FIFO entry at the same time.
     */
    PCPDMDEVREGCBINT pRegCB = (PCPDMDEVREGCBINT)pCallbacks;
    PVM const        pVM    = pRegCB->pVM;
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    PPDMDEV pDevPrev = NULL;
    PPDMDEV pDev     = pVM->pdm.s.pDevs;
    for (; pDev; pDevPrev = pDev, pDev = pDev->pNext)
        AssertMsgReturnStmt(strcmp(pDev->pReg->szName, pReg->szName),
                            ("Device '%s' already exists\n", pReg->szName),
                            RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw),
                            VERR_PDM_DEVICE_NAME_CLASH);

    /*
     * Allocate new device structure, initialize and insert it into the list.
     */
    int rc;
    pDev = (PPDMDEV)MMR3HeapAlloc(pRegCB->pVM, MM_TAG_PDM_DEVICE, sizeof(*pDev));
    if (pDev)
    {
        pDev->pNext      = NULL;
        pDev->cInstances = 0;
        pDev->pInstances = NULL;
        pDev->pReg       = pReg;
        pDev->cchName    = (uint32_t)strlen(pReg->szName);
        rc = CFGMR3QueryStringAllocDef(    pRegCB->pCfgNode, "RCSearchPath", &pDev->pszRCSearchPath, NULL);
        if (RT_SUCCESS(rc))
            rc = CFGMR3QueryStringAllocDef(pRegCB->pCfgNode, "R0SearchPath", &pDev->pszR0SearchPath, NULL);
        if (RT_SUCCESS(rc))
        {
            if (pDevPrev)
                pDevPrev->pNext = pDev;
            else
                pRegCB->pVM->pdm.s.pDevs = pDev;

            RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
            Log(("PDM: Registered device '%s'\n", pReg->szName));
            return VINF_SUCCESS;
        }

        MMR3HeapFree(pDev);
    }
    else
        rc = VERR_NO_MEMORY;

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
    return rc;
}


/**
 * Locates a LUN.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pszDevice       Device name.
 * @param   iInstance       Device instance.
 * @param   iLun            The Logical Unit to obtain the interface of.
 * @param   ppLun           Where to store the pointer to the LUN if found.
 * @thread  Try only do this in EMT...
 */
int pdmR3DevFindLun(PVM pVM, const char *pszDevice, unsigned iInstance, unsigned iLun, PPPDMLUN ppLun)
{
    /*
     * Iterate registered devices looking for the device.
     */
    size_t const cchDevice = strlen(pszDevice);
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    for (PPDMDEV pDev = pVM->pdm.s.pDevs; pDev; pDev = pDev->pNext)
    {
        if (    pDev->cchName == cchDevice
            &&  !memcmp(pDev->pReg->szName, pszDevice, cchDevice))
        {
            /*
             * Iterate device instances.
             */
            for (PPDMDEVINS pDevIns = pDev->pInstances; pDevIns; pDevIns = pDevIns->Internal.s.pPerDeviceNextR3)
            {
                if (pDevIns->iInstance == iInstance)
                {
                    /*
                     * Iterate luns.
                     */
                    for (PPDMLUN pLun = pDevIns->Internal.s.pLunsR3; pLun; pLun = pLun->pNext)
                    {
                        if (pLun->iLun == iLun)
                        {
                            RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
                            *ppLun = pLun;
                            return VINF_SUCCESS;
                        }
                    }
                    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
                    return VERR_PDM_LUN_NOT_FOUND;
                }
            }
            RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
            return VERR_PDM_DEVICE_INSTANCE_NOT_FOUND;
        }
    }
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
    return VERR_PDM_DEVICE_NOT_FOUND;
}


/**
 * Attaches a preconfigured driver to an existing device instance.
 *
 * This is used to change drivers and suchlike at runtime.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszDevice       Device name.
 * @param   iInstance       Device instance.
 * @param   iLun            The Logical Unit to obtain the interface of.
 * @param   fFlags          Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 * @param   ppBase          Where to store the base interface pointer. Optional.
 * @thread  EMT
 */
VMMR3DECL(int) PDMR3DeviceAttach(PUVM pUVM, const char *pszDevice, unsigned iInstance, unsigned iLun,
                                 uint32_t fFlags, PPPDMIBASE ppBase)
{
    if (ppBase)
        *ppBase = NULL;
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_EMT(pVM);
    LogFlow(("PDMR3DeviceAttach: pszDevice=%p:{%s} iInstance=%d iLun=%d fFlags=%#x ppBase=%p\n",
             pszDevice, pszDevice, iInstance, iLun, fFlags, ppBase));

    /*
     * Find the LUN in question.
     */
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    PPDMLUN pLun;
    int rc = pdmR3DevFindLun(pVM, pszDevice, iInstance, iLun, &pLun);
    if (RT_SUCCESS(rc))
    {
        /*
         * Can we attach anything at runtime?
         */
        PPDMDEVINS pDevIns = pLun->pDevIns;
        if (pDevIns->pReg->pfnAttach)
        {
            if (!pLun->pTop)
            {
                RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
                PDMCritSectEnter(pVM, pDevIns->pCritSectRoR3, VERR_IGNORED);
                rc = pDevIns->pReg->pfnAttach(pDevIns, iLun, fFlags);
                PDMCritSectLeave(pVM, pDevIns->pCritSectRoR3);
                RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
            }
            else
                rc = VERR_PDM_DRIVER_ALREADY_ATTACHED;
        }
        else
            rc = VERR_PDM_DEVICE_NO_RT_ATTACH;

        if (ppBase)
            *ppBase = pLun->pTop ? &pLun->pTop->IBase : NULL;
    }
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);

    if (ppBase)
        LogFlow(("PDMR3DeviceAttach: returns %Rrc *ppBase=%p\n", rc, *ppBase));
    else
        LogFlow(("PDMR3DeviceAttach: returns %Rrc\n", rc));
    return rc;
}


/**
 * Detaches a driver chain from an existing device instance.
 *
 * This is used to change drivers and suchlike at runtime.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszDevice       Device name.
 * @param   iInstance       Device instance.
 * @param   iLun            The Logical Unit to obtain the interface of.
 * @param   fFlags          Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 * @thread  EMT
 */
VMMR3DECL(int) PDMR3DeviceDetach(PUVM pUVM, const char *pszDevice, unsigned iInstance, unsigned iLun, uint32_t fFlags)
{
    return PDMR3DriverDetach(pUVM, pszDevice, iInstance, iLun, NULL, 0, fFlags);
}


/**
 * References the critical section associated with a device for the use by a
 * timer or similar created by the device.
 *
 * @returns Pointer to the critical section.
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         The device instance in question.
 *
 * @internal
 */
VMMR3_INT_DECL(PPDMCRITSECT) PDMR3DevGetCritSect(PVM pVM, PPDMDEVINS pDevIns)
{
    VM_ASSERT_EMT(pVM); RT_NOREF_PV(pVM);
    VM_ASSERT_STATE(pVM, VMSTATE_CREATING);
    AssertPtr(pDevIns);

    PPDMCRITSECT pCritSect = pDevIns->pCritSectRoR3;
    AssertPtr(pCritSect);
    pCritSect->s.fUsedByTimerOrSimilar = true;

    return pCritSect;
}


/**
 * Attaches a preconfigured driver to an existing device or driver instance.
 *
 * This is used to change drivers and suchlike at runtime.  The driver or device
 * at the end of the chain will be told to attach to whatever is configured
 * below it.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszDevice       Device name.
 * @param   iInstance       Device instance.
 * @param   iLun            The Logical Unit to obtain the interface of.
 * @param   fFlags          Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 * @param   ppBase          Where to store the base interface pointer. Optional.
 *
 * @thread  EMT
 */
VMMR3DECL(int) PDMR3DriverAttach(PUVM pUVM, const char *pszDevice, unsigned iInstance, unsigned iLun,
                                 uint32_t fFlags, PPPDMIBASE ppBase)
{
    LogFlow(("PDMR3DriverAttach: pszDevice=%p:{%s} iInstance=%d iLun=%d fFlags=%#x ppBase=%p\n",
             pszDevice, pszDevice, iInstance, iLun, fFlags, ppBase));
    if (ppBase)
        *ppBase = NULL;
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_EMT(pVM);

    /*
     * Find the LUN in question.
     */
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    PPDMLUN pLun;
    int rc = pdmR3DevFindLun(pVM, pszDevice, iInstance, iLun, &pLun);
    if (RT_SUCCESS(rc))
    {
        /*
         * Anything attached to the LUN?
         */
        PPDMDRVINS pDrvIns = pLun->pTop;
        if (!pDrvIns)
        {
            /* No, ask the device to attach to the new stuff. */
            PPDMDEVINS pDevIns = pLun->pDevIns;
            if (pDevIns->pReg->pfnAttach)
            {
                RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
                PDMCritSectEnter(pVM, pDevIns->pCritSectRoR3, VERR_IGNORED);
                rc = pDevIns->pReg->pfnAttach(pDevIns, iLun, fFlags);
                if (RT_SUCCESS(rc) && ppBase)
                    *ppBase = pLun->pTop ? &pLun->pTop->IBase : NULL;
                PDMCritSectLeave(pVM, pDevIns->pCritSectRoR3);
                RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
            }
            else
                rc = VERR_PDM_DEVICE_NO_RT_ATTACH;
        }
        else
        {
            /* Yes, find the bottom most driver and ask it to attach to the new stuff. */
            while (pDrvIns->Internal.s.pDown)
                pDrvIns = pDrvIns->Internal.s.pDown;
            if (pDrvIns->pReg->pfnAttach)
            {
                RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
                rc = pDrvIns->pReg->pfnAttach(pDrvIns, fFlags);
                RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
                if (RT_SUCCESS(rc) && ppBase)
                    *ppBase = pDrvIns->Internal.s.pDown
                            ? &pDrvIns->Internal.s.pDown->IBase
                            : NULL;
            }
            else
                rc = VERR_PDM_DRIVER_NO_RT_ATTACH;
        }
    }
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);

    if (ppBase)
        LogFlow(("PDMR3DriverAttach: returns %Rrc *ppBase=%p\n", rc, *ppBase));
    else
        LogFlow(("PDMR3DriverAttach: returns %Rrc\n", rc));
    return rc;
}


/**
 * Detaches the specified driver instance.
 *
 * This is used to replumb drivers at runtime for simulating hot plugging and
 * media changes.
 *
 * This is a superset of PDMR3DeviceDetach.  It allows detaching drivers from
 * any driver or device by specifying the driver to start detaching at.  The
 * only prerequisite is that the driver or device above implements the
 * pfnDetach callback (PDMDRVREG / PDMDEVREG).
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszDevice       Device name.
 * @param   iDevIns         Device instance.
 * @param   iLun            The Logical Unit in which to look for the driver.
 * @param   pszDriver       The name of the driver which to detach.  If NULL
 *                          then the entire driver chain is detatched.
 * @param   iOccurrence     The occurrence of that driver in the chain.  This is
 *                          usually 0.
 * @param   fFlags          Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 * @thread  EMT
 */
VMMR3DECL(int) PDMR3DriverDetach(PUVM pUVM, const char *pszDevice, unsigned iDevIns, unsigned iLun,
                                 const char *pszDriver, unsigned iOccurrence, uint32_t fFlags)
{
    LogFlow(("PDMR3DriverDetach: pszDevice=%p:{%s} iDevIns=%u iLun=%u pszDriver=%p:{%s} iOccurrence=%u fFlags=%#x\n",
             pszDevice, pszDevice, iDevIns, iLun, pszDriver, pszDriver, iOccurrence, fFlags));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM const pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_EMT(pVM);
    AssertPtr(pszDevice);
    AssertPtrNull(pszDriver);
    Assert(iOccurrence == 0 || pszDriver);
    Assert(!(fFlags & ~(PDM_TACH_FLAGS_NOT_HOT_PLUG)));

    /*
     * Find the LUN in question.
     */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
    PPDMLUN pLun;
    int rc = pdmR3DevFindLun(pVM, pszDevice, iDevIns, iLun, &pLun);
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the driver.
         */
        PPDMDRVINS pDrvIns = pLun->pTop;
        if (pDrvIns)
        {
            if (pszDriver)
            {
                while (pDrvIns)
                {
                    if (!strcmp(pDrvIns->pReg->szName, pszDriver))
                    {
                        if (iOccurrence == 0)
                            break;
                        iOccurrence--;
                    }
                    pDrvIns = pDrvIns->Internal.s.pDown;
                }
            }
            if (pDrvIns)
                rc = pdmR3DrvDetach(pVM, pDrvIns, fFlags);
            else
                rc = VERR_PDM_DRIVER_INSTANCE_NOT_FOUND;
        }
        else
            rc = VINF_PDM_NO_DRIVER_ATTACHED_TO_LUN;
    }
    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    LogFlow(("PDMR3DriverDetach: returns %Rrc\n", rc));
    return rc;
}


/**
 * Runtime detach and reattach of a new driver chain or sub chain.
 *
 * This is intended to be called on a non-EMT thread, this will instantiate the
 * new driver (sub-)chain, and then the EMTs will do the actual replumbing.  The
 * destruction of the old driver chain will be taken care of on the calling
 * thread.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszDevice       Device name.
 * @param   iDevIns         Device instance.
 * @param   iLun            The Logical Unit in which to look for the driver.
 * @param   pszDriver       The name of the driver which to detach and replace.
 *                          If NULL then the entire driver chain is to be
 *                          reattached.
 * @param   iOccurrence     The occurrence of that driver in the chain.  This is
 *                          usually 0.
 * @param   fFlags          Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 * @param   pCfg            The configuration of the new driver chain that is
 *                          going to be attached.  The subtree starts with the
 *                          node containing a Driver key, a Config subtree and
 *                          optionally an AttachedDriver subtree.
 *                          If this parameter is NULL, then this call will work
 *                          like at a non-pause version of PDMR3DriverDetach.
 * @param   ppBase          Where to store the base interface pointer to the new
 *                          driver.  Optional.
 *
 * @thread  Any thread. The EMTs will be involved at some point though.
 */
VMMR3DECL(int)  PDMR3DriverReattach(PUVM pUVM, const char *pszDevice, unsigned iDevIns, unsigned iLun,
                                    const char *pszDriver, unsigned iOccurrence, uint32_t fFlags,
                                    PCFGMNODE pCfg, PPPDMIBASE ppBase)
{
    NOREF(pUVM); NOREF(pszDevice); NOREF(iDevIns); NOREF(iLun); NOREF(pszDriver); NOREF(iOccurrence);
    NOREF(fFlags); NOREF(pCfg); NOREF(ppBase);
    return VERR_NOT_IMPLEMENTED;
}

