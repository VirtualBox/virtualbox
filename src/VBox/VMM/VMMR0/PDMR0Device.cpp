/* $Id: PDMR0Device.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Pluggable Device and Driver Manager, R0 Device parts.
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
#include <VBox/vmm/apic.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/gvm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/gvmm.h>

#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/msi.h>
#include <VBox/sup.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/process.h>
#include <iprt/string.h>

#include "dtrace/VBoxVMM.h"
#include "PDMInline.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
extern DECLEXPORT(const PDMDEVHLPR0)    g_pdmR0DevHlp;
#ifdef VBOX_WITH_DBGF_TRACING
extern DECLEXPORT(const PDMDEVHLPR0)    g_pdmR0DevHlpTracing;
#endif
extern DECLEXPORT(const PDMPICHLP)      g_pdmR0PicHlp;
extern DECLEXPORT(const PDMIOAPICHLP)   g_pdmR0IoApicHlp;
extern DECLEXPORT(const PDMPCIHLPR0)    g_pdmR0PciHlp;
extern DECLEXPORT(const PDMIOMMUHLPR0)  g_pdmR0IommuHlp;
extern DECLEXPORT(const PDMHPETHLPR0)   g_pdmR0HpetHlp;
extern DECLEXPORT(const PDMPCIRAWHLPR0) g_pdmR0PciRawHlp;
RT_C_DECLS_END

/** List of PDMDEVMODREGR0 structures protected by the loader lock. */
static RTLISTANCHOR g_PDMDevModList;


/**
 * Pointer to the ring-0 device registrations for VMMR0.
 */
static const PDMDEVREGR0 *g_apVMM0DevRegs[] =
{
    &g_DeviceAPIC,
};

/**
 * Module device registration record for VMMR0.
 */
static PDMDEVMODREGR0 g_VBoxDDR0ModDevReg =
{
    /* .u32Version = */ PDM_DEVMODREGR0_VERSION,
    /* .cDevRegs = */   RT_ELEMENTS(g_apVMM0DevRegs),
    /* .papDevRegs = */ &g_apVMM0DevRegs[0],
    /* .hMod = */       NULL,
    /* .ListEntry = */  { NULL, NULL },
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Initializes the global ring-0 PDM data.
 */
VMMR0_INT_DECL(void) PDMR0Init(void *hMod)
{
    RTListInit(&g_PDMDevModList);
    g_VBoxDDR0ModDevReg.hMod = hMod;
    RTListAppend(&g_PDMDevModList, &g_VBoxDDR0ModDevReg.ListEntry);
}


/**
 * Used by PDMR0CleanupVM to destroy a device instance.
 *
 * This is done during VM cleanup so that we're sure there are no active threads
 * inside the device code.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   pDevIns     The device instance.
 * @param   idxR0Device The device instance handle.
 */
static int pdmR0DeviceDestroy(PGVM pGVM, PPDMDEVINSR0 pDevIns, uint32_t idxR0Device)
{
    /*
     * Assert sanity.
     */
    Assert(idxR0Device < pGVM->pdmr0.s.cDevInstances);
    AssertPtrReturn(pDevIns, VERR_INVALID_HANDLE);
    Assert(pDevIns->u32Version == PDM_DEVINSR0_VERSION);
    Assert(pDevIns->Internal.s.idxR0Device == idxR0Device);

    /*
     * Call the final destructor if there is one.
     */
    if (pDevIns->pReg->pfnFinalDestruct)
        pDevIns->pReg->pfnFinalDestruct(pDevIns);
    pDevIns->u32Version = ~PDM_DEVINSR0_VERSION;

    /*
     * Remove the device from the instance table.
     */
    Assert(pGVM->pdmr0.s.apDevInstances[idxR0Device] == pDevIns);
    pGVM->pdmr0.s.apDevInstances[idxR0Device] = NULL;
    if (idxR0Device + 1 == pGVM->pdmr0.s.cDevInstances)
        pGVM->pdmr0.s.cDevInstances = idxR0Device;

    /*
     * Free the DBGF tracing tracking structures if necessary.
     */
    if (pDevIns->Internal.s.hDbgfTraceEvtSrc != NIL_DBGFTRACEREVTSRC)
    {
        RTR0MemObjFree(pDevIns->Internal.s.hDbgfTraceObj, true);
        pDevIns->Internal.s.hDbgfTraceObj = NIL_RTR0MEMOBJ;
    }

    /*
     * Free the ring-3 mapping and instance memory.
     */
    RTR0MEMOBJ hMemObj = pDevIns->Internal.s.hMapObj;
    pDevIns->Internal.s.hMapObj = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(hMemObj, true);

    hMemObj = pDevIns->Internal.s.hMemObj;
    pDevIns->Internal.s.hMemObj = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(hMemObj, true);

    return VINF_SUCCESS;
}


/**
 * Initializes the per-VM data for the PDM.
 *
 * This is called from under the GVMM lock, so it only need to initialize the
 * data so PDMR0CleanupVM and others will work smoothly.
 *
 * @param   pGVM    Pointer to the global VM structure.
 */
VMMR0_INT_DECL(void) PDMR0InitPerVMData(PGVM pGVM)
{
    AssertCompile(sizeof(pGVM->pdm.s) <= sizeof(pGVM->pdm.padding));
    AssertCompile(sizeof(pGVM->pdmr0.s) <= sizeof(pGVM->pdmr0.padding));

    pGVM->pdmr0.s.cDevInstances = 0;
}


/**
 * Cleans up any loose ends before the GVM structure is destroyed.
 */
VMMR0_INT_DECL(void) PDMR0CleanupVM(PGVM pGVM)
{
    uint32_t i = pGVM->pdmr0.s.cDevInstances;
    while (i-- > 0)
    {
        PPDMDEVINSR0 pDevIns = pGVM->pdmr0.s.apDevInstances[i];
        if (pDevIns)
            pdmR0DeviceDestroy(pGVM, pDevIns, i);
    }

    i = pGVM->pdmr0.s.cQueues;
    while (i-- > 0)
    {
        if (pGVM->pdmr0.s.aQueues[i].pQueue != NULL)
            pdmR0QueueDestroy(pGVM, i);
    }
}


/**
 * Worker for PDMR0DeviceCreate that does the actual instantiation.
 *
 * Allocates a memory object and divides it up as follows:
 * @verbatim
   --------------------------------------
   ring-0 devins
   --------------------------------------
   ring-0 instance data
   --------------------------------------
   ring-0 PCI device data (optional) ??
   --------------------------------------
   page alignment padding
   --------------------------------------
   ring-3 devins
   --------------------------------------
   ring-3 instance data
   --------------------------------------
   ring-3 PCI device data (optional) ??
   --------------------------------------
  [page alignment padding                ] -
  [--------------------------------------]  \
  [raw-mode devins                       ]   \
  [--------------------------------------]   - Optional, only when raw-mode is enabled.
  [raw-mode instance data                ]   /
  [--------------------------------------]  /
  [raw-mode PCI device data (optional)?? ] -
   --------------------------------------
   shared instance data
   --------------------------------------
   default crit section
   --------------------------------------
   shared PCI device data (optional)
   --------------------------------------
   @endverbatim
 *
 * @returns VBox status code.
 * @param   pGVM             The global (ring-0) VM structure.
 * @param   pDevReg          The device registration structure.
 * @param   iInstance        The device instance number.
 * @param   cbInstanceR3     The size of the ring-3 instance data.
 * @param   cbInstanceRC     The size of the raw-mode instance data.
 * @param   hMod             The module implementing the device.
 * @param   hDbgfTraceEvtSrc The DBGF tarcer event source handle.
 * @param   RCPtrMapping     The raw-mode context mapping address, NIL_RTGCPTR if
 *                           not to include raw-mode.
 * @param   ppDevInsR3       Where to return the ring-3 device instance address.
 * @thread  EMT(0)
 */
static int pdmR0DeviceCreateWorker(PGVM pGVM, PCPDMDEVREGR0 pDevReg, uint32_t iInstance, uint32_t cbInstanceR3,
                                   uint32_t cbInstanceRC, RTRGPTR RCPtrMapping, DBGFTRACEREVTSRC hDbgfTraceEvtSrc,
                                   void *hMod, PPDMDEVINSR3 *ppDevInsR3)
{
    /*
     * Check that the instance number isn't a duplicate.
     */
    for (size_t i = 0; i < pGVM->pdmr0.s.cDevInstances; i++)
    {
        PPDMDEVINS pCur = pGVM->pdmr0.s.apDevInstances[i];
        AssertLogRelReturn(!pCur || pCur->pReg != pDevReg || pCur->iInstance != iInstance, VERR_DUPLICATE);
    }

    /*
     * Figure out how much memory we need and allocate it.
     */
    uint32_t const cbRing0     = RT_ALIGN_32(RT_UOFFSETOF(PDMDEVINSR0, achInstanceData) + pDevReg->cbInstanceCC, HOST_PAGE_SIZE);
    uint32_t const cbRing3     = RT_ALIGN_32(RT_UOFFSETOF(PDMDEVINSR3, achInstanceData) + cbInstanceR3,
                                             RCPtrMapping != NIL_RTRGPTR ? HOST_PAGE_SIZE : 64);
    uint32_t const cbRC        = RCPtrMapping != NIL_RTRGPTR ? 0
                               : RT_ALIGN_32(RT_UOFFSETOF(PDMDEVINSRC, achInstanceData) + cbInstanceRC, 64);
    uint32_t const cbShared    = RT_ALIGN_32(pDevReg->cbInstanceShared, 64);
    uint32_t const cbCritSect  = RT_ALIGN_32(sizeof(PDMCRITSECT), 64);
    uint32_t const cbMsixState = RT_ALIGN_32(pDevReg->cMaxMsixVectors * 16 + (pDevReg->cMaxMsixVectors + 7) / 8, _4K);
    uint32_t const cbPciDev    = RT_ALIGN_32(RT_UOFFSETOF_DYN(PDMPCIDEV, abMsixState[cbMsixState]), 64);
    uint32_t const cPciDevs    = RT_MIN(pDevReg->cMaxPciDevices, 8);
    uint32_t const cbPciDevs   = cbPciDev * cPciDevs;
    uint32_t const cbTotal     = RT_ALIGN_32(cbRing0 + cbRing3 + cbRC + cbShared + cbCritSect + cbPciDevs, HOST_PAGE_SIZE);
    AssertLogRelMsgReturn(cbTotal <= PDM_MAX_DEVICE_INSTANCE_SIZE,
                          ("Instance of '%s' is too big: cbTotal=%u, max %u\n",
                           pDevReg->szName, cbTotal, PDM_MAX_DEVICE_INSTANCE_SIZE),
                          VERR_OUT_OF_RANGE);

    RTR0MEMOBJ hMemObj;
    int rc = RTR0MemObjAllocPage(&hMemObj, cbTotal, false /*fExecutable*/);
    if (RT_FAILURE(rc))
        return rc;
    RT_BZERO(RTR0MemObjAddress(hMemObj), cbTotal);

    /* Map it. */
    RTR0MEMOBJ hMapObj;
    rc = RTR0MemObjMapUserEx(&hMapObj, hMemObj, (RTR3PTR)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE, RTR0ProcHandleSelf(),
                             cbRing0, cbTotal - cbRing0);
    if (RT_SUCCESS(rc))
    {
        PPDMDEVINSR0        pDevIns   = (PPDMDEVINSR0)RTR0MemObjAddress(hMemObj);
        struct PDMDEVINSR3 *pDevInsR3 = (struct PDMDEVINSR3 *)((uint8_t *)pDevIns + cbRing0);

        /*
         * Initialize the ring-0 instance.
         */
        pDevIns->u32Version             = PDM_DEVINSR0_VERSION;
        pDevIns->iInstance              = iInstance;
#ifdef VBOX_WITH_DBGF_TRACING
        pDevIns->pHlpR0                 = hDbgfTraceEvtSrc == NIL_DBGFTRACEREVTSRC ? &g_pdmR0DevHlp : &g_pdmR0DevHlpTracing;
#else
        pDevIns->pHlpR0                 = &g_pdmR0DevHlp;
#endif
        pDevIns->pvInstanceDataR0       = (uint8_t *)pDevIns + cbRing0 + cbRing3 + cbRC;
        pDevIns->pvInstanceDataForR0    = &pDevIns->achInstanceData[0];
        pDevIns->pCritSectRoR0          = (PPDMCRITSECT)((uint8_t *)pDevIns->pvInstanceDataR0 + cbShared);
        pDevIns->pReg                   = pDevReg;
        pDevIns->pDevInsForR3           = RTR0MemObjAddressR3(hMapObj);
        pDevIns->pDevInsForR3R0         = pDevInsR3;
        pDevIns->pvInstanceDataForR3R0  = &pDevInsR3->achInstanceData[0];
        pDevIns->cbPciDev               = cbPciDev;
        pDevIns->cPciDevs               = cPciDevs;
        for (uint32_t iPciDev = 0; iPciDev < cPciDevs; iPciDev++)
        {
            /* Note! PDMDevice.cpp has a copy of this code.  Keep in sync. */
            PPDMPCIDEV pPciDev = (PPDMPCIDEV)((uint8_t *)pDevIns->pCritSectRoR0 + cbCritSect + cbPciDev * iPciDev);
            if (iPciDev < RT_ELEMENTS(pDevIns->apPciDevs))
                pDevIns->apPciDevs[iPciDev] = pPciDev;
            pPciDev->cbConfig           = _4K;
            pPciDev->cbMsixState        = cbMsixState;
            pPciDev->idxSubDev          = (uint16_t)iPciDev;
            pPciDev->Int.s.idxSubDev    = (uint16_t)iPciDev;
            pPciDev->u32Magic           = PDMPCIDEV_MAGIC;
        }
        pDevIns->Internal.s.pGVM        = pGVM;
        pDevIns->Internal.s.pRegR0      = pDevReg;
        pDevIns->Internal.s.hMod        = hMod;
        pDevIns->Internal.s.hMemObj     = hMemObj;
        pDevIns->Internal.s.hMapObj     = hMapObj;
        pDevIns->Internal.s.pInsR3R0    = pDevInsR3;
        pDevIns->Internal.s.pIntR3R0    = &pDevInsR3->Internal.s;
        pDevIns->Internal.s.hDbgfTraceEvtSrc = hDbgfTraceEvtSrc;

        /*
         * Initialize the ring-3 instance data as much as we can.
         * Note! PDMDevice.cpp does this job for ring-3 only devices.  Keep in sync.
         */
        pDevInsR3->u32Version           = PDM_DEVINSR3_VERSION;
        pDevInsR3->iInstance            = iInstance;
        pDevInsR3->cbRing3              = cbTotal - cbRing0;
        pDevInsR3->fR0Enabled           = true;
        pDevInsR3->fRCEnabled           = RCPtrMapping != NIL_RTRGPTR;
        pDevInsR3->pvInstanceDataR3     = pDevIns->pDevInsForR3 + cbRing3 + cbRC;
        pDevInsR3->pvInstanceDataForR3  = pDevIns->pDevInsForR3 + RT_UOFFSETOF(PDMDEVINSR3, achInstanceData);
        pDevInsR3->pCritSectRoR3        = pDevIns->pDevInsForR3 + cbRing3 + cbRC + cbShared;
        pDevInsR3->pDevInsR0RemoveMe    = pDevIns;
        pDevInsR3->pvInstanceDataR0     = pDevIns->pvInstanceDataR0;
        pDevInsR3->pvInstanceDataRC     = RCPtrMapping == NIL_RTRGPTR
                                        ? NIL_RTRGPTR : pDevIns->pDevInsForRC + RT_UOFFSETOF(PDMDEVINSRC, achInstanceData);
        pDevInsR3->pDevInsForRC         = pDevIns->pDevInsForRC;
        pDevInsR3->pDevInsForRCR3       = pDevIns->pDevInsForR3 + cbRing3;
        pDevInsR3->pDevInsForRCR3       = pDevInsR3->pDevInsForRCR3 + RT_UOFFSETOF(PDMDEVINSRC, achInstanceData);
        pDevInsR3->cbPciDev             = cbPciDev;
        pDevInsR3->cPciDevs             = cPciDevs;
        for (uint32_t i = 0; i < RT_MIN(cPciDevs, RT_ELEMENTS(pDevIns->apPciDevs)); i++)
            pDevInsR3->apPciDevs[i] = pDevInsR3->pCritSectRoR3 + cbCritSect + cbPciDev * i;

        pDevInsR3->Internal.s.pVMR3     = pGVM->pVMR3;
        pDevInsR3->Internal.s.fIntFlags = RCPtrMapping == NIL_RTRGPTR ? PDMDEVINSINT_FLAGS_R0_ENABLED
                                        : PDMDEVINSINT_FLAGS_R0_ENABLED | PDMDEVINSINT_FLAGS_RC_ENABLED;
        pDevInsR3->Internal.s.hDbgfTraceEvtSrc = hDbgfTraceEvtSrc;

        /*
         * Initialize the raw-mode instance data as much as possible.
         */
        if (RCPtrMapping != NIL_RTRGPTR)
        {
            struct PDMDEVINSRC *pDevInsRC = RCPtrMapping == NIL_RTRGPTR ? NULL
                                          : (struct PDMDEVINSRC *)((uint8_t *)pDevIns + cbRing0 + cbRing3);

            pDevIns->pDevInsForRC           = RCPtrMapping;
            pDevIns->pDevInsForRCR0         = pDevInsRC;
            pDevIns->pvInstanceDataForRCR0  = &pDevInsRC->achInstanceData[0];

            pDevInsRC->u32Version           = PDM_DEVINSRC_VERSION;
            pDevInsRC->iInstance            = iInstance;
            pDevInsRC->pvInstanceDataRC     = pDevIns->pDevInsForRC + cbRC;
            pDevInsRC->pvInstanceDataForRC  = pDevIns->pDevInsForRC + RT_UOFFSETOF(PDMDEVINSRC, achInstanceData);
            pDevInsRC->pCritSectRoRC        = pDevIns->pDevInsForRC + cbRC + cbShared;
            pDevInsRC->cbPciDev             = cbPciDev;
            pDevInsRC->cPciDevs             = cPciDevs;
            for (uint32_t i = 0; i < RT_MIN(cPciDevs, RT_ELEMENTS(pDevIns->apPciDevs)); i++)
                pDevInsRC->apPciDevs[i] = pDevInsRC->pCritSectRoRC + cbCritSect + cbPciDev * i;

            pDevInsRC->Internal.s.pVMRC     = pGVM->pVMRC;
        }

        /*
         * If the device is being traced we have to set up a single page for tracking
         * I/O and MMIO region registrations so we can inject our own handlers.
         */
        if (hDbgfTraceEvtSrc != NIL_DBGFTRACEREVTSRC)
        {
            pDevIns->Internal.s.hDbgfTraceObj = NIL_RTR0MEMOBJ;
            rc = RTR0MemObjAllocPage(&pDevIns->Internal.s.hDbgfTraceObj, PDM_MAX_DEVICE_DBGF_TRACING_TRACK, false /*fExecutable*/);
            if (RT_SUCCESS(rc))
            {
                pDevIns->Internal.s.paDbgfTraceTrack      = (PPDMDEVINSDBGFTRACK)RTR0MemObjAddress(pDevIns->Internal.s.hDbgfTraceObj);
                pDevIns->Internal.s.idxDbgfTraceTrackNext = 0;
                pDevIns->Internal.s.cDbgfTraceTrackMax    = PDM_MAX_DEVICE_DBGF_TRACING_TRACK / sizeof(PDMDEVINSDBGFTRACK);
                RT_BZERO(pDevIns->Internal.s.paDbgfTraceTrack, PDM_MAX_DEVICE_DBGF_TRACING_TRACK);
            }
        }

        if (RT_SUCCESS(rc))
        {
            /*
             * Add to the device instance array and set its handle value.
             */
            AssertCompile(sizeof(pGVM->pdmr0.padding) == sizeof(pGVM->pdmr0));
            uint32_t idxR0Device = pGVM->pdmr0.s.cDevInstances;
            if (idxR0Device < RT_ELEMENTS(pGVM->pdmr0.s.apDevInstances))
            {
                pGVM->pdmr0.s.apDevInstances[idxR0Device]    = pDevIns;
                pGVM->pdmr0.s.cDevInstances                  = idxR0Device + 1;
                pGVM->pdm.s.apDevRing0Instances[idxR0Device] = pDevIns->pDevInsForR3;
                pDevIns->Internal.s.idxR0Device   = idxR0Device;
                pDevInsR3->Internal.s.idxR0Device = idxR0Device;

                /*
                 * Call the early constructor if present.
                 */
                if (pDevReg->pfnEarlyConstruct)
                    rc = pDevReg->pfnEarlyConstruct(pDevIns);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * We're done.
                     */
                    *ppDevInsR3 = RTR0MemObjAddressR3(hMapObj);
                    return rc;
                }

                /*
                 * Bail out.
                 */
                if (pDevIns->pReg->pfnFinalDestruct)
                    pDevIns->pReg->pfnFinalDestruct(pDevIns);

                pGVM->pdmr0.s.apDevInstances[idxR0Device] = NULL;
                Assert(pGVM->pdmr0.s.cDevInstances == idxR0Device + 1);
                pGVM->pdmr0.s.cDevInstances = idxR0Device;
            }
        }

        if (   hDbgfTraceEvtSrc != NIL_DBGFTRACEREVTSRC
            && pDevIns->Internal.s.hDbgfTraceObj != NIL_RTR0MEMOBJ)
            RTR0MemObjFree(pDevIns->Internal.s.hDbgfTraceObj, true);

        RTR0MemObjFree(hMapObj, true);
    }
    RTR0MemObjFree(hMemObj, true);
    return rc;
}


/**
 * Used by ring-3 PDM to create a device instance that operates both in ring-3
 * and ring-0.
 *
 * Creates an instance of a device (for both ring-3 and ring-0, and optionally
 * raw-mode context).
 *
 * @returns VBox status code.
 * @param   pGVM    The global (ring-0) VM structure.
 * @param   pReq    Pointer to the request buffer.
 * @thread  EMT(0)
 */
VMMR0_INT_DECL(int) PDMR0DeviceCreateReqHandler(PGVM pGVM, PPDMDEVICECREATEREQ pReq)
{
    LogFlow(("PDMR0DeviceCreateReqHandler: %s in %s\n", pReq->szDevName, pReq->szModName));

    /*
     * Validate the request.
     */
    AssertReturn(pReq->Hdr.cbReq == sizeof(*pReq), VERR_INVALID_PARAMETER);
    pReq->pDevInsR3 = NIL_RTR3PTR;

    int rc = GVMMR0ValidateGVMandEMT(pGVM, 0);
    AssertRCReturn(rc, rc);

    AssertReturn(pReq->fFlags           != 0, VERR_INVALID_FLAGS);
    AssertReturn(pReq->fClass           != 0, VERR_WRONG_TYPE);
    AssertReturn(pReq->uSharedVersion   != 0, VERR_INVALID_PARAMETER);
    AssertReturn(pReq->cbInstanceShared != 0, VERR_INVALID_PARAMETER);
    size_t const cchDevName = RTStrNLen(pReq->szDevName, sizeof(pReq->szDevName));
    AssertReturn(cchDevName < sizeof(pReq->szDevName), VERR_NO_STRING_TERMINATOR);
    AssertReturn(cchDevName > 0, VERR_EMPTY_STRING);
    AssertReturn(cchDevName < RT_SIZEOFMEMB(PDMDEVREG, szName), VERR_NOT_FOUND);

    size_t const cchModName = RTStrNLen(pReq->szModName, sizeof(pReq->szModName));
    AssertReturn(cchModName < sizeof(pReq->szModName), VERR_NO_STRING_TERMINATOR);
    AssertReturn(cchModName > 0, VERR_EMPTY_STRING);
    AssertReturn(pReq->cbInstanceShared <= PDM_MAX_DEVICE_INSTANCE_SIZE, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->cbInstanceR3 <= PDM_MAX_DEVICE_INSTANCE_SIZE, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->cbInstanceRC <= PDM_MAX_DEVICE_INSTANCE_SIZE, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->iInstance < 1024, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->iInstance < pReq->cMaxInstances, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->cMaxPciDevices <= 8, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->cMaxMsixVectors <= VBOX_MSIX_MAX_ENTRIES, VERR_OUT_OF_RANGE);

    /*
     * Reference the module.
     */
    void *hMod = NULL;
    rc = SUPR0LdrModByName(pGVM->pSession, pReq->szModName, &hMod);
    if (RT_FAILURE(rc))
    {
        LogRel(("PDMR0DeviceCreateReqHandler: SUPR0LdrModByName(,%s,) failed: %Rrc\n", pReq->szModName, rc));
        return rc;
    }

    /*
     * Look for the the module and the device registration structure.
     */
    int rcLock = SUPR0LdrLock(pGVM->pSession);
    AssertRC(rc);

    rc = VERR_NOT_FOUND;
    PPDMDEVMODREGR0 pMod;
    RTListForEach(&g_PDMDevModList, pMod, PDMDEVMODREGR0, ListEntry)
    {
        if (pMod->hMod == hMod)
        {
            /*
             * Found the module. We can drop the loader lock now before we
             * search the devices it registers.
             */
            if (RT_SUCCESS(rcLock))
            {
                rcLock = SUPR0LdrUnlock(pGVM->pSession);
                AssertRC(rcLock);
            }
            rcLock = VERR_ALREADY_RESET;

            PCPDMDEVREGR0 *papDevRegs = pMod->papDevRegs;
            size_t         i          = pMod->cDevRegs;
            while (i-- > 0)
            {
                PCPDMDEVREGR0 pDevReg = papDevRegs[i];
                LogFlow(("PDMR0DeviceCreateReqHandler: candidate #%u: %s %#x\n", i, pReq->szDevName, pDevReg->u32Version));
                if (   PDM_VERSION_ARE_COMPATIBLE(pDevReg->u32Version, PDM_DEVREGR0_VERSION)
                    && pDevReg->szName[cchDevName] == '\0'
                    && memcmp(pDevReg->szName, pReq->szDevName, cchDevName) == 0)
                {

                    /*
                     * Found the device, now check whether it matches the ring-3 registration.
                     */
                    if (   pReq->uSharedVersion   == pDevReg->uSharedVersion
                        && pReq->cbInstanceShared == pDevReg->cbInstanceShared
                        && pReq->cbInstanceRC     == pDevReg->cbInstanceRC
                        && pReq->fFlags           == pDevReg->fFlags
                        && pReq->fClass           == pDevReg->fClass
                        && pReq->cMaxInstances    == pDevReg->cMaxInstances
                        && pReq->cMaxPciDevices   == pDevReg->cMaxPciDevices
                        && pReq->cMaxMsixVectors  == pDevReg->cMaxMsixVectors)
                    {
                        rc = pdmR0DeviceCreateWorker(pGVM, pDevReg, pReq->iInstance, pReq->cbInstanceR3, pReq->cbInstanceRC,
                                                     NIL_RTRCPTR /** @todo new raw-mode */, pReq->hDbgfTracerEvtSrc,
                                                     hMod, &pReq->pDevInsR3);
                        if (RT_SUCCESS(rc))
                            hMod = NULL; /* keep the module reference */
                    }
                    else
                    {
                        LogRel(("PDMR0DeviceCreate: Ring-3 does not match ring-0 device registration (%s):\n"
                                "    uSharedVersion: %#x vs %#x\n"
                                "  cbInstanceShared: %#x vs %#x\n"
                                "      cbInstanceRC: %#x vs %#x\n"
                                "            fFlags: %#x vs %#x\n"
                                "            fClass: %#x vs %#x\n"
                                "     cMaxInstances: %#x vs %#x\n"
                                "    cMaxPciDevices: %#x vs %#x\n"
                                "   cMaxMsixVectors: %#x vs %#x\n"
                                ,
                                pReq->szDevName,
                                pReq->uSharedVersion,   pDevReg->uSharedVersion,
                                pReq->cbInstanceShared, pDevReg->cbInstanceShared,
                                pReq->cbInstanceRC,     pDevReg->cbInstanceRC,
                                pReq->fFlags,           pDevReg->fFlags,
                                pReq->fClass,           pDevReg->fClass,
                                pReq->cMaxInstances,    pDevReg->cMaxInstances,
                                pReq->cMaxPciDevices,   pDevReg->cMaxPciDevices,
                                pReq->cMaxMsixVectors,  pDevReg->cMaxMsixVectors));
                        rc = VERR_INCOMPATIBLE_CONFIG;
                    }
                }
            }
            break;
        }
    }

    if (RT_SUCCESS_NP(rcLock))
    {
        rcLock = SUPR0LdrUnlock(pGVM->pSession);
        AssertRC(rcLock);
    }
    SUPR0LdrModRelease(pGVM->pSession, hMod);
    return rc;
}


/**
 * Used by ring-3 PDM to call standard ring-0 device methods.
 *
 * @returns VBox status code.
 * @param   pGVM    The global (ring-0) VM structure.
 * @param   pReq    Pointer to the request buffer.
 * @param   idCpu   The ID of the calling EMT.
 * @thread  EMT(0), except for PDMDEVICEGENCALL_REQUEST which can be any EMT.
 */
VMMR0_INT_DECL(int) PDMR0DeviceGenCallReqHandler(PGVM pGVM, PPDMDEVICEGENCALLREQ pReq, VMCPUID idCpu)
{
    /*
     * Validate the request.
     */
    AssertReturn(pReq->Hdr.cbReq == sizeof(*pReq), VERR_INVALID_PARAMETER);

    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    AssertRCReturn(rc, rc);

    AssertReturn(pReq->idxR0Device < pGVM->pdmr0.s.cDevInstances, VERR_INVALID_HANDLE);
    PPDMDEVINSR0 pDevIns = pGVM->pdmr0.s.apDevInstances[pReq->idxR0Device];
    AssertPtrReturn(pDevIns, VERR_INVALID_HANDLE);
    AssertReturn(pDevIns->pDevInsForR3 == pReq->pDevInsR3, VERR_INVALID_HANDLE);

    /*
     * Make the call.
     */
    rc = VINF_SUCCESS /*VINF_NOT_IMPLEMENTED*/;
    switch (pReq->enmCall)
    {
        case PDMDEVICEGENCALL_CONSTRUCT:
            AssertMsgBreakStmt(pGVM->enmVMState < VMSTATE_CREATED, ("enmVMState=%d\n", pGVM->enmVMState), rc = VERR_INVALID_STATE);
            AssertReturn(idCpu == 0,  VERR_VM_THREAD_NOT_EMT);
            if (pDevIns->pReg->pfnConstruct)
                rc = pDevIns->pReg->pfnConstruct(pDevIns);
            break;

        case PDMDEVICEGENCALL_DESTRUCT:
            AssertMsgBreakStmt(pGVM->enmVMState < VMSTATE_CREATED || pGVM->enmVMState >= VMSTATE_DESTROYING,
                               ("enmVMState=%d\n", pGVM->enmVMState), rc = VERR_INVALID_STATE);
            AssertReturn(idCpu == 0,  VERR_VM_THREAD_NOT_EMT);
            if (pDevIns->pReg->pfnDestruct)
            {
                pDevIns->pReg->pfnDestruct(pDevIns);
                rc = VINF_SUCCESS;
            }
            break;

        case PDMDEVICEGENCALL_REQUEST:
            if (pDevIns->pReg->pfnRequest)
                rc = pDevIns->pReg->pfnRequest(pDevIns, pReq->Params.Req.uReq, pReq->Params.Req.uArg);
            else
                rc = VERR_INVALID_FUNCTION;
            break;

        default:
            AssertMsgFailed(("enmCall=%d\n", pReq->enmCall));
            rc = VERR_INVALID_FUNCTION;
            break;
    }

    return rc;
}


/**
 * Legacy device mode compatiblity.
 *
 * @returns VBox status code.
 * @param   pGVM    The global (ring-0) VM structure.
 * @param   pReq    Pointer to the request buffer.
 * @thread  EMT(0)
 */
VMMR0_INT_DECL(int) PDMR0DeviceCompatSetCritSectReqHandler(PGVM pGVM, PPDMDEVICECOMPATSETCRITSECTREQ pReq)
{
    /*
     * Validate the request.
     */
    AssertReturn(pReq->Hdr.cbReq == sizeof(*pReq), VERR_INVALID_PARAMETER);

    int rc = GVMMR0ValidateGVMandEMT(pGVM, 0);
    AssertRCReturn(rc, rc);

    AssertReturn(pReq->idxR0Device < pGVM->pdmr0.s.cDevInstances, VERR_INVALID_HANDLE);
    PPDMDEVINSR0 pDevIns = pGVM->pdmr0.s.apDevInstances[pReq->idxR0Device];
    AssertPtrReturn(pDevIns, VERR_INVALID_HANDLE);
    AssertReturn(pDevIns->pDevInsForR3 == pReq->pDevInsR3, VERR_INVALID_HANDLE);

    AssertReturn(pGVM->enmVMState == VMSTATE_CREATING, VERR_INVALID_STATE);

    /*
     * The critical section address can be in a few different places:
     *      1. shared data.
     *      2. nop section.
     *      3. pdm critsect.
     */
    PPDMCRITSECT pCritSect;
    if (pReq->pCritSectR3 == pGVM->pVMR3 + RT_UOFFSETOF(VM, pdm.s.NopCritSect))
    {
        pCritSect = &pGVM->pdm.s.NopCritSect;
        Log(("PDMR0DeviceCompatSetCritSectReqHandler: Nop - %p %#x\n", pCritSect, pCritSect->s.Core.u32Magic));
    }
    else if (pReq->pCritSectR3 == pGVM->pVMR3 + RT_UOFFSETOF(VM, pdm.s.CritSect))
    {
        pCritSect = &pGVM->pdm.s.CritSect;
        Log(("PDMR0DeviceCompatSetCritSectReqHandler: PDM - %p %#x\n", pCritSect, pCritSect->s.Core.u32Magic));
    }
    else
    {
        size_t offCritSect = pReq->pCritSectR3 - pDevIns->pDevInsForR3R0->pvInstanceDataR3;
        AssertLogRelMsgReturn(   offCritSect                       <  pDevIns->pReg->cbInstanceShared
                              && offCritSect + sizeof(PDMCRITSECT) <= pDevIns->pReg->cbInstanceShared,
                              ("offCritSect=%p pCritSectR3=%p cbInstanceShared=%#x (%s)\n",
                               offCritSect, pReq->pCritSectR3, pDevIns->pReg->cbInstanceShared, pDevIns->pReg->szName),
                              VERR_INVALID_POINTER);
        pCritSect = (PPDMCRITSECT)((uint8_t *)pDevIns->pvInstanceDataR0 + offCritSect);
        Log(("PDMR0DeviceCompatSetCritSectReqHandler: custom - %#x/%p %#x\n", offCritSect, pCritSect, pCritSect->s.Core.u32Magic));
    }
    AssertLogRelMsgReturn(pCritSect->s.Core.u32Magic == RTCRITSECT_MAGIC,
                          ("cs=%p magic=%#x dev=%s\n", pCritSect, pCritSect->s.Core.u32Magic, pDevIns->pReg->szName),
                          VERR_INVALID_MAGIC);

    /*
     * Make the update.
     */
    pDevIns->pCritSectRoR0 = pCritSect;

    return VINF_SUCCESS;
}


/**
 * Registers the device implementations living in a module.
 *
 * This should normally only be called during ModuleInit().  The should be a
 * call to PDMR0DeviceDeregisterModule from the ModuleTerm() function to undo
 * the effects of this call.
 *
 * @returns VBox status code.
 * @param   hMod            The module handle of the module being registered.
 * @param   pModReg         The module registration structure.  This will be
 *                          used directly so it must live as long as the module
 *                          and be writable.
 *
 * @note    Caller must own the loader lock!
 */
VMMR0DECL(int) PDMR0DeviceRegisterModule(void *hMod, PPDMDEVMODREGR0 pModReg)
{
    /*
     * Validate the input.
     */
    AssertPtrReturn(hMod, VERR_INVALID_HANDLE);
    Assert(SUPR0LdrIsLockOwnerByMod(hMod, true));

    AssertPtrReturn(pModReg, VERR_INVALID_POINTER);
    AssertLogRelMsgReturn(PDM_VERSION_ARE_COMPATIBLE(pModReg->u32Version, PDM_DEVMODREGR0_VERSION),
                          ("pModReg->u32Version=%#x vs %#x\n", pModReg->u32Version, PDM_DEVMODREGR0_VERSION),
                          VERR_VERSION_MISMATCH);
    AssertLogRelMsgReturn(pModReg->cDevRegs <= 256 && pModReg->cDevRegs > 0, ("cDevRegs=%u\n", pModReg->cDevRegs),
                          VERR_OUT_OF_RANGE);
    AssertLogRelMsgReturn(pModReg->hMod == NULL, ("hMod=%p\n", pModReg->hMod), VERR_INVALID_PARAMETER);
    AssertLogRelMsgReturn(pModReg->ListEntry.pNext == NULL, ("pNext=%p\n", pModReg->ListEntry.pNext), VERR_INVALID_PARAMETER);
    AssertLogRelMsgReturn(pModReg->ListEntry.pPrev == NULL, ("pPrev=%p\n", pModReg->ListEntry.pPrev), VERR_INVALID_PARAMETER);

    for (size_t i = 0; i < pModReg->cDevRegs; i++)
    {
        PCPDMDEVREGR0 pDevReg = pModReg->papDevRegs[i];
        AssertLogRelMsgReturn(RT_VALID_PTR(pDevReg), ("[%u]: %p\n", i, pDevReg), VERR_INVALID_POINTER);
        AssertLogRelMsgReturn(PDM_VERSION_ARE_COMPATIBLE(pDevReg->u32Version, PDM_DEVREGR0_VERSION),
                              ("pDevReg->u32Version=%#x vs %#x\n", pModReg->u32Version, PDM_DEVREGR0_VERSION), VERR_VERSION_MISMATCH);
        AssertLogRelMsgReturn(RT_VALID_PTR(pDevReg->pszDescription), ("[%u]: %p\n", i, pDevReg->pszDescription), VERR_INVALID_POINTER);
        AssertLogRelMsgReturn(pDevReg->uReserved0     == 0, ("[%u]: %#x\n", i, pDevReg->uReserved0),     VERR_INVALID_PARAMETER);
        AssertLogRelMsgReturn(pDevReg->fClass         != 0, ("[%u]: %#x\n", i, pDevReg->fClass),         VERR_INVALID_PARAMETER);
        AssertLogRelMsgReturn(pDevReg->fFlags         != 0, ("[%u]: %#x\n", i, pDevReg->fFlags),         VERR_INVALID_PARAMETER);
        AssertLogRelMsgReturn(pDevReg->cMaxInstances   > 0, ("[%u]: %#x\n", i, pDevReg->cMaxInstances),  VERR_INVALID_PARAMETER);
        AssertLogRelMsgReturn(pDevReg->cMaxPciDevices <= 8, ("[%u]: %#x\n", i, pDevReg->cMaxPciDevices), VERR_INVALID_PARAMETER);
        AssertLogRelMsgReturn(pDevReg->cMaxMsixVectors <= VBOX_MSIX_MAX_ENTRIES,
                              ("[%u]: %#x\n", i, pDevReg->cMaxMsixVectors), VERR_INVALID_PARAMETER);

        /* The name must be printable ascii and correctly terminated. */
        for (size_t off = 0; off < RT_ELEMENTS(pDevReg->szName); off++)
        {
            char ch = pDevReg->szName[off];
            AssertLogRelMsgReturn(RT_C_IS_PRINT(ch) || (ch == '\0' && off > 0),
                                  ("[%u]: off=%u  szName: %.*Rhxs\n", i, off, sizeof(pDevReg->szName), &pDevReg->szName[0]),
                                  VERR_INVALID_NAME);
            if (ch == '\0')
                break;
        }
    }

    /*
     * Add it, assuming we're being called at ModuleInit/ModuleTerm time only, or
     * that the caller has already taken the loader lock.
     */
    pModReg->hMod = hMod;
    RTListAppend(&g_PDMDevModList, &pModReg->ListEntry);

    return VINF_SUCCESS;
}


/**
 * Deregisters the device implementations living in a module.
 *
 * This should normally only be called during ModuleTerm().
 *
 * @returns VBox status code.
 * @param   hMod            The module handle of the module being registered.
 * @param   pModReg         The module registration structure.  This will be
 *                          used directly so it must live as long as the module
 *                          and be writable.
 *
 * @note    Caller must own the loader lock!
 */
VMMR0DECL(int) PDMR0DeviceDeregisterModule(void *hMod, PPDMDEVMODREGR0 pModReg)
{
    /*
     * Validate the input.
     */
    AssertPtrReturn(hMod, VERR_INVALID_HANDLE);
    Assert(SUPR0LdrIsLockOwnerByMod(hMod, true));

    AssertPtrReturn(pModReg, VERR_INVALID_POINTER);
    AssertLogRelMsgReturn(PDM_VERSION_ARE_COMPATIBLE(pModReg->u32Version, PDM_DEVMODREGR0_VERSION),
                          ("pModReg->u32Version=%#x vs %#x\n", pModReg->u32Version, PDM_DEVMODREGR0_VERSION),
                          VERR_VERSION_MISMATCH);
    AssertLogRelMsgReturn(pModReg->hMod == hMod || pModReg->hMod == NULL, ("pModReg->hMod=%p vs %p\n", pModReg->hMod, hMod),
                          VERR_INVALID_PARAMETER);

    /*
     * Unlink the registration record and return it to virgin conditions.  Ignore
     * the call if not registered.
     */
    if (pModReg->hMod)
    {
        pModReg->hMod = NULL;
        RTListNodeRemove(&pModReg->ListEntry);
        pModReg->ListEntry.pNext = NULL;
        pModReg->ListEntry.pPrev = NULL;
        return VINF_SUCCESS;
    }
    return VWRN_NOT_FOUND;
}

