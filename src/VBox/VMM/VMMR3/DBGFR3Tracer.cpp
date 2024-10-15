/* $Id: DBGFR3Tracer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, tracing parts.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DBGF
#include "DBGFInternal.h"
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/sup.h>

#include <VBox/version.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/buildconfig.h>
#include <iprt/alloc.h>
#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/tracelog.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The event descriptors written to the trace log. */

static const RTTRACELOGEVTDESC g_EvtSrcRegisterEvtDesc =
{
    "EvtSrc.Register",
    "An event source was registered",
    RTTRACELOGEVTSEVERITY_DEBUG,
    0,
    NULL
};


static const RTTRACELOGEVTDESC g_EvtSrcDeregisterEvtDesc =
{
    "EvtSrc.Deregister",
    "An event source was de-registered",
    RTTRACELOGEVTSEVERITY_DEBUG,
    0,
    NULL
};


static const RTTRACELOGEVTITEMDESC g_DevMmioCreateEvtItems[] =
{
    {"hMmioRegion",    "The MMIO region handle being returned by IOM",          RTTRACELOGTYPE_UINT64,  0},
    {"cbRegion",       "Size of the MMIO region in bytes",                      RTTRACELOGTYPE_UINT64,  0},
    {"fIomFlags",      "Flags passed to IOM",                                   RTTRACELOGTYPE_UINT32,  0},
    {"iPciRegion",     "PCI region used for a PCI device",                      RTTRACELOGTYPE_UINT32,  0},
};

static const RTTRACELOGEVTDESC g_DevMmioCreateEvtDesc =
{
    "Dev.MmioCreate",
    "MMIO region of a device is being created",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevMmioCreateEvtItems),
    &g_DevMmioCreateEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevMmioMapEvtItems[] =
{
    {"hMmioRegion",    "The MMIO region handle being mapped",                   RTTRACELOGTYPE_UINT64,  0},
    {"GCPhysMmioBase", "The guest physical address where the region is mapped", RTTRACELOGTYPE_UINT64,  0}
};

static const RTTRACELOGEVTDESC g_DevMmioMapEvtDesc =
{
    "Dev.MmioMap",
    "MMIO region of a device is being mapped",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevMmioMapEvtItems),
    &g_DevMmioMapEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevMmioUnmapEvtItems[] =
{
    {"hMmioRegion",    "The MMIO region handle being unmapped",                 RTTRACELOGTYPE_UINT64,  0}
};

static const RTTRACELOGEVTDESC g_DevMmioUnmapEvtDesc =
{
    "Dev.MmioUnmap",
    "MMIO region of a device is being unmapped",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevMmioUnmapEvtItems),
    &g_DevMmioUnmapEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevMmioRwEvtItems[] =
{
    {"hMmioRegion",    "The MMIO region handle being accessed",                 RTTRACELOGTYPE_UINT64,  0},
    {"offMmio",        "The offset in the MMIO region being accessed",          RTTRACELOGTYPE_UINT64,  0},
    {"cbXfer",         "Number of bytes being transfered",                      RTTRACELOGTYPE_UINT64,  0},
    {"u64Val",         "The value read or written",                             RTTRACELOGTYPE_UINT64,  0},
};

static const RTTRACELOGEVTDESC g_DevMmioReadEvtDesc =
{
    "Dev.MmioRead",
    "MMIO region of a device is being read",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevMmioRwEvtItems),
    &g_DevMmioRwEvtItems[0]
};

static const RTTRACELOGEVTDESC g_DevMmioWriteEvtDesc =
{
    "Dev.MmioWrite",
    "MMIO region of a device is being written",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevMmioRwEvtItems),
    &g_DevMmioRwEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevMmioFillEvtItems[] =
{
    {"hMmioRegion",    "The MMIO region handle being unmapped",                 RTTRACELOGTYPE_UINT64,  0},
    {"offMmio",        "The offset in the MMIO region being accessed",          RTTRACELOGTYPE_UINT64,  0},
    {"cbItem",         "Item size in bytes",                                    RTTRACELOGTYPE_UINT32,  0},
    {"cItems",         "Number of items being written",                         RTTRACELOGTYPE_UINT32,  0},
    {"u32Val",         "The value used for filling",                            RTTRACELOGTYPE_UINT32,  0},
};

static const RTTRACELOGEVTDESC g_DevMmioFillEvtDesc =
{
    "Dev.MmioFill",
    "MMIO region of a device is being filled",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevMmioFillEvtItems),
    &g_DevMmioFillEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevIoPortCreateEvtItems[] =
{
    {"hIoPorts",       "The I/O port region handle being returned by IOM",      RTTRACELOGTYPE_UINT64,  0},
    {"cPorts",         "Size of the region in number of ports",                 RTTRACELOGTYPE_UINT16,  0},
    {"fIomFlags",      "Flags passed to IOM",                                   RTTRACELOGTYPE_UINT32,  0},
    {"iPciRegion",     "PCI region used for a PCI device",                      RTTRACELOGTYPE_UINT32,  0},
};

static const RTTRACELOGEVTDESC g_DevIoPortCreateEvtDesc =
{
    "Dev.IoPortCreate",
    "I/O port region of a device is being created",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevIoPortCreateEvtItems),
    &g_DevIoPortCreateEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevIoPortMapEvtItems[] =
{
    {"hIoPorts",       "The I/O port region handle being mapped",               RTTRACELOGTYPE_UINT64,  0},
    {"IoPortBase",     "The I/O port base address where the region is mapped",  RTTRACELOGTYPE_UINT16,  0}
};

static const RTTRACELOGEVTDESC g_DevIoPortMapEvtDesc =
{
    "Dev.IoPortMap",
    "I/O port region of a device is being mapped",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevIoPortMapEvtItems),
    &g_DevIoPortMapEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevIoPortUnmapEvtItems[] =
{
    {"hIoPorts",       "The I/O port region handle being unmapped",             RTTRACELOGTYPE_UINT64,  0}
};

static const RTTRACELOGEVTDESC g_DevIoPortUnmapEvtDesc =
{
    "Dev.IoPortUnmap",
    "I/O port region of a device is being unmapped",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevIoPortUnmapEvtItems),
    &g_DevIoPortUnmapEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevIoPortRwEvtItems[] =
{
    {"hIoPorts",       "The I/O region handle being accessed",                  RTTRACELOGTYPE_UINT64,  0},
    {"offPort",        "The offset in the I/O port region being accessed",      RTTRACELOGTYPE_UINT16,  0},
    {"cbXfer",         "Number of bytes being transfered",                      RTTRACELOGTYPE_UINT64,  0},
    {"u32Val",         "The value read or written",                             RTTRACELOGTYPE_UINT32,  0},
};

static const RTTRACELOGEVTDESC g_DevIoPortReadEvtDesc =
{
    "Dev.IoPortRead",
    "I/O port region of a device is being read",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevIoPortRwEvtItems),
    &g_DevIoPortRwEvtItems[0]
};

static const RTTRACELOGEVTDESC g_DevIoPortWriteEvtDesc =
{
    "Dev.IoPortWrite",
    "I/O port region of a device is being written",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevIoPortRwEvtItems),
    &g_DevIoPortRwEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevIoPortRwStrEvtItems[] =
{
    {"hIoPorts",       "The I/O region handle being accesses",                  RTTRACELOGTYPE_UINT64,  0},
    {"offPort",        "The offset in the I/O port region being accessed",      RTTRACELOGTYPE_UINT16,  0},
    {"cbItem",         "Item size for the access",                              RTTRACELOGTYPE_UINT32,  0},
    {"cTransfersReq",  "Number of transfers requested by the guest",            RTTRACELOGTYPE_UINT32,  0},
    {"cTransfersRet",  "Number of transfers executed by the device",            RTTRACELOGTYPE_UINT32,  0}
};

static const RTTRACELOGEVTDESC g_DevIoPortReadStrEvtDesc =
{
    "Dev.IoPortReadStr",
    "I/O port region of a device is being read using REP INS",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevIoPortRwStrEvtItems),
    &g_DevIoPortRwStrEvtItems[0]
};

static const RTTRACELOGEVTDESC g_DevIoPortWriteStrEvtDesc =
{
    "Dev.IoPortWriteStr",
    "I/O port region of a device is being written using REP OUTS",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevIoPortRwStrEvtItems),
    &g_DevIoPortRwStrEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevIrqEvtItems[] =
{
    {"iIrq",          "The IRQ line",                                           RTTRACELOGTYPE_INT32,    0},
    {"fIrqLvl",       "The IRQ level",                                          RTTRACELOGTYPE_INT32,    0}
};

static const RTTRACELOGEVTDESC g_DevIrqEvtDesc =
{
    "Dev.Irq",
    "Device raised or lowered an IRQ line",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevIrqEvtItems),
    &g_DevIrqEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevIoApicMsiEvtItems[] =
{
    {"GCPhys",        "Physical guest address being written",                   RTTRACELOGTYPE_UINT64,   0},
    {"u32Val",        "value being written",                                    RTTRACELOGTYPE_UINT32,   0}
};

static const RTTRACELOGEVTDESC g_DevIoApicMsiEvtDesc =
{
    "Dev.IoApicMsi",
    "Device sent a MSI event through the I/O APIC",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevIoApicMsiEvtItems),
    &g_DevIoApicMsiEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevGCPhysRwStartEvtItems[] =
{
    {"GCPhys",        "Physical guest address being accessed",                  RTTRACELOGTYPE_UINT64,   0},
    {"cbXfer",        "Number of bytes being transfered",                       RTTRACELOGTYPE_UINT64,   0},
};


static const RTTRACELOGEVTDESC g_DevGCPhysReadEvtDesc =
{
    "Dev.GCPhysRead",
    "Device read data from guest physical memory",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevGCPhysRwStartEvtItems),
    &g_DevGCPhysRwStartEvtItems[0]
};


static const RTTRACELOGEVTDESC g_DevGCPhysWriteEvtDesc =
{
    "Dev.GCPhysWrite",
    "Device wrote data to guest physical memory",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevGCPhysRwStartEvtItems),
    &g_DevGCPhysRwStartEvtItems[0]
};


static const RTTRACELOGEVTITEMDESC g_DevRwDataEvtItems[] =
{
    {"abData",        "The data being read/written",                            RTTRACELOGTYPE_RAWDATA,  0}
};

static const RTTRACELOGEVTDESC g_DevRwDataEvtDesc =
{
    "Dev.RwData",
    "The data being read or written",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_DevRwDataEvtItems),
    &g_DevRwDataEvtItems[0]
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Returns an unused guest memory read/write data aggregation structure.
 *
 * @returns Pointer to a new aggregation structure or NULL if out of memory.
 * @param   pThis                   The DBGF tracer instance.
 */
static PDBGFTRACERGCPHYSRWAGG dbgfTracerR3EvtRwAggNew(PDBGFTRACERINSR3 pThis)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aGstMemRwData); i++)
    {
        if (pThis->aGstMemRwData[i].idEvtStart == DBGF_TRACER_EVT_HDR_ID_INVALID)
            return &pThis->aGstMemRwData[i];
    }

    return NULL;
}


/**
 * Find the guest memory read/write data aggregation structure for the given event ID.
 *
 * @returns Pointer to a new aggregation structure or NULL if not found.
 * @param   pThis                   The DBGF tracer instance.
 * @param   idEvtPrev               The event ID to look for.
 */
static PDBGFTRACERGCPHYSRWAGG dbgfTracerR3EvtRwAggFind(PDBGFTRACERINSR3 pThis, uint64_t idEvtPrev)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aGstMemRwData); i++)
    {
        if (   pThis->aGstMemRwData[i].idEvtStart != DBGF_TRACER_EVT_HDR_ID_INVALID
            && pThis->aGstMemRwData[i].idEvtPrev == idEvtPrev)
            return &pThis->aGstMemRwData[i];
    }

    return NULL;
}


/**
 * Common code for the guest memory and string I/O port read/write events.
 *
 * @returns VBox status code.
 * @param   pThis                   The DBGF tracer instance.
 * @param   pEvtHdr                 The event header.
 * @param   cbXfer                  Overall number of bytes of data for this event.
 * @param   pvData                  Initial data supplied in the event starting the aggregation.
 * @param   cbData                  Number of initial bytes of data.
 */
static int dbgfTracerR3EvtRwStartCommon(PDBGFTRACERINSR3 pThis, PCDBGFTRACEREVTHDR pEvtHdr, size_t cbXfer, const void *pvData, size_t cbData)
{
    /* Slow path, find an empty aggregation structure. */
    int rc = VINF_SUCCESS;
    PDBGFTRACERGCPHYSRWAGG pDataAgg = dbgfTracerR3EvtRwAggNew(pThis);
    if (RT_LIKELY(pDataAgg))
    {
        /* Initialize it. */
        pDataAgg->idEvtStart = pEvtHdr->idEvt;
        pDataAgg->idEvtPrev  = pEvtHdr->idEvt;
        pDataAgg->cbXfer     = cbXfer;
        pDataAgg->cbLeft     = pDataAgg->cbXfer;
        pDataAgg->offBuf     = 0;

        /* Need to reallocate the buffer to hold the complete data? */
        if (RT_UNLIKELY(pDataAgg->cbBufMax < pDataAgg->cbXfer))
        {
            uint8_t *pbBufNew = (uint8_t *)RTMemRealloc(pDataAgg->pbBuf, pDataAgg->cbXfer);
            if (RT_LIKELY(pbBufNew))
            {
                pDataAgg->pbBuf    = pbBufNew;
                pDataAgg->cbBufMax = pDataAgg->cbXfer;
            }
            else
                rc = VERR_NO_MEMORY;
        }

        if (RT_SUCCESS(rc))
        {
            memcpy(pDataAgg->pbBuf, pvData, cbData);
            pDataAgg->offBuf += cbData;
            pDataAgg->cbLeft -= cbData;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    if (RT_FAILURE(rc))
    {
        LogRelMax(10, ("DBGF: Creating new data aggregation structure for memory read/write failed with %Rrc, trace log will not contain data for this event!\n", rc));

        /* Write out the finish event without any data. */
        size_t cbEvtData = 0;
        rc = RTTraceLogWrEvtAdd(pThis->hTraceLog, &g_DevRwDataEvtDesc, RTTRACELOG_WR_ADD_EVT_F_GRP_FINISH,
                                pEvtHdr->idEvt, pEvtHdr->hEvtSrc, NULL, &cbEvtData);
        if (pDataAgg) /* Reset the aggregation event. */
            pDataAgg->idEvtStart = DBGF_TRACER_EVT_HDR_ID_INVALID;
    }

    return rc;
}


/**
 * Starts a new guest memory read/write event.
 *
 * @returns VBox status code.
 * @param   pThis                   The DBGF tracer instance.
 * @param   pEvtHdr                 The event header.
 * @param   pEvtGCPhysRw            The guest memory read/write event descriptor.
 * @param   pEvtDesc                The event descriptor written to the trace log.
 */
static int dbgfTracerR3EvtGCPhysRwStart(PDBGFTRACERINSR3 pThis, PCDBGFTRACEREVTHDR pEvtHdr,
                                        PCDBGFTRACEREVTGCPHYS pEvtGCPhysRw, PCRTTRACELOGEVTDESC pEvtDesc)
{
    /* Write out the event header first in any case. */
    int rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, pEvtDesc, RTTRACELOG_WR_ADD_EVT_F_GRP_START,
                                 pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtGCPhysRw->GCPhys, pEvtGCPhysRw->cbXfer);
    if (RT_SUCCESS(rc))
    {
        /*
         * If the amount of data is small enough to fit into the single event descriptor we can skip allocating
         * an aggregation tracking structure and write the event containing the complete data out immediately.
         */
        if (pEvtGCPhysRw->cbXfer <= sizeof(pEvtGCPhysRw->abData))
        {
            size_t cbEvtData = pEvtGCPhysRw->cbXfer;

            rc = RTTraceLogWrEvtAdd(pThis->hTraceLog, &g_DevRwDataEvtDesc, RTTRACELOG_WR_ADD_EVT_F_GRP_FINISH,
                                    pEvtHdr->idEvt, pEvtHdr->hEvtSrc, &pEvtGCPhysRw->abData[0], &cbEvtData);
        }
        else
            rc = dbgfTracerR3EvtRwStartCommon(pThis, pEvtHdr, pEvtGCPhysRw->cbXfer, &pEvtGCPhysRw->abData[0], sizeof(pEvtGCPhysRw->abData));
    }

    return rc;
}


/**
 * Starts a new I/O port string read/write event.
 *
 * @returns VBox status code.
 * @param   pThis                   The DBGF tracer instance.
 * @param   pEvtHdr                 The event header.
 * @param   pEvtIoPortStrRw         The I/O port string read/write event descriptor.
 * @param   cbXfer                  Number of bytes of valid data for this event.
 * @param   pEvtDesc                The event descriptor written to the trace log.
 */
static int dbgfTracerR3EvtIoPortStrRwStart(PDBGFTRACERINSR3 pThis, PCDBGFTRACEREVTHDR pEvtHdr,
                                           PCDBGFTRACEREVTIOPORTSTR pEvtIoPortStrRw, size_t cbXfer,
                                           PCRTTRACELOGEVTDESC pEvtDesc)
{
    /* Write out the event header first in any case. */
    int rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, pEvtDesc, RTTRACELOG_WR_ADD_EVT_F_GRP_START,
                                 pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtIoPortStrRw->hIoPorts, pEvtIoPortStrRw->offPort,
                                 pEvtIoPortStrRw->cbItem, pEvtIoPortStrRw->cTransfersReq, pEvtIoPortStrRw->cTransfersRet);
    if (RT_SUCCESS(rc))
    {
        /*
         * If the amount of data is small enough to fit into the single event descriptor we can skip allocating
         * an aggregation tracking structure and write the event containing the complete data out immediately.
         */
        if (cbXfer <= sizeof(pEvtIoPortStrRw->abData))
        {
            size_t cbEvtData = cbXfer;

            rc = RTTraceLogWrEvtAdd(pThis->hTraceLog, &g_DevRwDataEvtDesc, RTTRACELOG_WR_ADD_EVT_F_GRP_FINISH,
                                    pEvtHdr->idEvt, pEvtHdr->hEvtSrc, &pEvtIoPortStrRw->abData[0], &cbEvtData);
        }
        else
            rc = dbgfTracerR3EvtRwStartCommon(pThis, pEvtHdr, cbXfer, &pEvtIoPortStrRw->abData[0], sizeof(pEvtIoPortStrRw->abData));
    }

    return rc;
}


/**
 * Continues a previously started guest memory or string I/O port read/write event.
 *
 * @returns VBox status code.
 * @param   pThis                   The DBGF tracer instance.
 * @param   pEvtHdr                 The event header.
 * @param   pvData                  The data to log.
 */
static int dbgfTracerR3EvtRwContinue(PDBGFTRACERINSR3 pThis, PCDBGFTRACEREVTHDR pEvtHdr, void *pvData)
{
    int rc = VINF_SUCCESS;
    PDBGFTRACERGCPHYSRWAGG pDataAgg = dbgfTracerR3EvtRwAggFind(pThis, pEvtHdr->idEvtPrev);

    if (RT_LIKELY(pDataAgg))
    {
        size_t cbThisXfer = RT_MIN(pDataAgg->cbLeft, DBGF_TRACER_EVT_PAYLOAD_SZ);

        memcpy(pDataAgg->pbBuf + pDataAgg->offBuf, pvData, cbThisXfer);
        pDataAgg->offBuf += cbThisXfer;
        pDataAgg->cbLeft -= cbThisXfer;

        if (!pDataAgg->cbLeft)
        {
            /* All data aggregated, write it out and reset the structure. */
            rc = RTTraceLogWrEvtAdd(pThis->hTraceLog, &g_DevRwDataEvtDesc, RTTRACELOG_WR_ADD_EVT_F_GRP_FINISH,
                                    pDataAgg->idEvtStart, pEvtHdr->hEvtSrc, pDataAgg->pbBuf, &pDataAgg->cbXfer);
            pDataAgg->offBuf     = 0;
            pDataAgg->idEvtStart = DBGF_TRACER_EVT_HDR_ID_INVALID;
        }
        else
            pDataAgg->idEvtPrev = pEvtHdr->idEvt; /* So the next event containing more data can find the aggregation structure. */
    }
    else /* This can only happen if creating a new structure failed before. */
        rc = VERR_DBGF_TRACER_IPE_1;

    return rc;
}


/**
 * Processes the given event.
 *
 * @returns VBox status code.
 * @param   pThis                   The DBGF tracer instance.
 * @param   pEvtHdr                 The event to process.
 */
static int dbgfR3TracerEvtProcess(PDBGFTRACERINSR3 pThis, PDBGFTRACEREVTHDR pEvtHdr)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%p pEvtHdr=%p{idEvt=%llu,enmEvt=%u}\n",
                 pThis, pEvtHdr, pEvtHdr->idEvt, pEvtHdr->enmEvt));

    switch (pEvtHdr->enmEvt)
    {
        case DBGFTRACEREVT_SRC_REGISTER:
        {
            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_EvtSrcRegisterEvtDesc, RTTRACELOG_WR_ADD_EVT_F_GRP_START,
                                     pEvtHdr->hEvtSrc, 0 /*uParentGrpId*/);
            break;
        }
        case DBGFTRACEREVT_SRC_DEREGISTER:
        {
            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_EvtSrcDeregisterEvtDesc, RTTRACELOG_WR_ADD_EVT_F_GRP_FINISH,
                                     pEvtHdr->hEvtSrc, 0 /*uParentGrpId*/);
            break;
        }
        case DBGFTRACEREVT_MMIO_REGION_CREATE:
        {
            PCDBGFTRACEREVTMMIOCREATE pEvtMmioCreate = (PCDBGFTRACEREVTMMIOCREATE)(pEvtHdr + 1);

            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_DevMmioCreateEvtDesc, 0 /*fFlags*/,
                                     pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtMmioCreate->hMmioRegion, pEvtMmioCreate->cbRegion,
                                     pEvtMmioCreate->fIomFlags, pEvtMmioCreate->iPciRegion);
            break;
        }
        case DBGFTRACEREVT_MMIO_MAP:
        {
            PCDBGFTRACEREVTMMIOMAP pEvtMmioMap = (PCDBGFTRACEREVTMMIOMAP)(pEvtHdr + 1);

            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_DevMmioMapEvtDesc, 0 /*fFlags*/,
                                     pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtMmioMap->hMmioRegion, pEvtMmioMap->GCPhysMmioBase);
            break;
        }
        case DBGFTRACEREVT_MMIO_UNMAP:
        {
            PCDBGFTRACEREVTMMIOUNMAP pEvtMmioUnmap = (PCDBGFTRACEREVTMMIOUNMAP)(pEvtHdr + 1);

            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_DevMmioUnmapEvtDesc, 0 /*fFlags*/,
                                     pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtMmioUnmap->hMmioRegion);
            break;
        }
        case DBGFTRACEREVT_MMIO_READ:
        case DBGFTRACEREVT_MMIO_WRITE:
        {
            PCDBGFTRACEREVTMMIO pEvtMmioRw = (PCDBGFTRACEREVTMMIO)(pEvtHdr + 1);

            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog,
                                       pEvtHdr->enmEvt == DBGFTRACEREVT_MMIO_READ
                                     ? &g_DevMmioReadEvtDesc
                                     : &g_DevMmioWriteEvtDesc,
                                     0 /*fFlags*/,
                                     pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtMmioRw->hMmioRegion, pEvtMmioRw->offMmio,
                                     pEvtMmioRw->cbXfer, pEvtMmioRw->u64Val);
            break;
        }
        case DBGFTRACEREVT_MMIO_FILL:
        {
            PCDBGFTRACEREVTMMIOFILL pEvtMmioFill = (PCDBGFTRACEREVTMMIOFILL)(pEvtHdr + 1);

            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_DevMmioFillEvtDesc, 0 /*fFlags*/,
                                     pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtMmioFill->hMmioRegion, pEvtMmioFill->offMmio,
                                     pEvtMmioFill->cbItem, pEvtMmioFill->cItems, pEvtMmioFill->u32Item);
            break;
        }
        case DBGFTRACEREVT_IOPORT_REGION_CREATE:
        {
            PCDBGFTRACEREVTIOPORTCREATE pEvtIoPortCreate = (PCDBGFTRACEREVTIOPORTCREATE)(pEvtHdr + 1);

            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_DevIoPortCreateEvtDesc, 0 /*fFlags*/,
                                     pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtIoPortCreate->hIoPorts, pEvtIoPortCreate->cPorts,
                                     pEvtIoPortCreate->fIomFlags, pEvtIoPortCreate->iPciRegion);
            break;
        }
        case DBGFTRACEREVT_IOPORT_MAP:
        {
            PCDBGFTRACEREVTIOPORTMAP pEvtIoPortMap = (PCDBGFTRACEREVTIOPORTMAP)(pEvtHdr + 1);

            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_DevIoPortMapEvtDesc, 0 /*fFlags*/,
                                     pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtIoPortMap->hIoPorts, pEvtIoPortMap->IoPortBase);
            break;
        }
        case DBGFTRACEREVT_IOPORT_UNMAP:
        {
            PCDBGFTRACEREVTIOPORTUNMAP pEvtIoPortUnmap = (PCDBGFTRACEREVTIOPORTUNMAP)(pEvtHdr + 1);

            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_DevIoPortUnmapEvtDesc, 0 /*fFlags*/,
                                     pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtIoPortUnmap->hIoPorts);
            break;
        }
        case DBGFTRACEREVT_IOPORT_READ:
        case DBGFTRACEREVT_IOPORT_WRITE:
        {
            PCDBGFTRACEREVTIOPORT pEvtIoPortRw = (PCDBGFTRACEREVTIOPORT)(pEvtHdr + 1);

            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog,
                                       pEvtHdr->enmEvt == DBGFTRACEREVT_IOPORT_READ
                                     ? &g_DevIoPortReadEvtDesc
                                     : &g_DevIoPortWriteEvtDesc,
                                     0 /*fFlags*/,
                                     pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtIoPortRw->hIoPorts, pEvtIoPortRw->offPort,
                                     pEvtIoPortRw->cbXfer, pEvtIoPortRw->u32Val);
            break;
        }
        case DBGFTRACEREVT_IOPORT_READ_STR:
        case DBGFTRACEREVT_IOPORT_WRITE_STR:
        {
            PCRTTRACELOGEVTDESC pEvtDesc =   pEvtHdr->enmEvt == DBGFTRACEREVT_IOPORT_WRITE_STR
                                           ? &g_DevIoPortWriteStrEvtDesc
                                           : &g_DevIoPortReadStrEvtDesc;

            /* If the previous event ID is invalid this starts a new read/write we have to aggregate all the data for. */
            if (pEvtHdr->idEvtPrev == DBGF_TRACER_EVT_HDR_ID_INVALID)
            {
                PCDBGFTRACEREVTIOPORTSTR pEvtIoPortStrRw = (PCDBGFTRACEREVTIOPORTSTR)(pEvtHdr + 1);
                size_t cbXfer =   pEvtHdr->enmEvt == DBGFTRACEREVT_IOPORT_WRITE_STR
                                ? pEvtIoPortStrRw->cTransfersReq * pEvtIoPortStrRw->cbItem
                                : pEvtIoPortStrRw->cTransfersRet * pEvtIoPortStrRw->cbItem;

                rc = dbgfTracerR3EvtIoPortStrRwStart(pThis, pEvtHdr, pEvtIoPortStrRw, cbXfer, pEvtDesc);
            }
            else
            {
                /* Continuation of a started read or write, look up the right tracking structure and process the new data. */
                void *pvData = pEvtHdr + 1;
                rc = dbgfTracerR3EvtRwContinue(pThis, pEvtHdr, pvData);
            }
            break;
        }
        case DBGFTRACEREVT_IRQ:
        {
            PCDBGFTRACEREVTIRQ pEvtIrq = (PCDBGFTRACEREVTIRQ)(pEvtHdr + 1);

            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_DevIrqEvtDesc, 0 /*fFlags*/,
                                     pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtIrq->iIrq, pEvtIrq->fIrqLvl);
            break;
        }
        case DBGFTRACEREVT_IOAPIC_MSI:
        {
            PCDBGFTRACEREVTIOAPICMSI pEvtIoApicMsi = (PCDBGFTRACEREVTIOAPICMSI)(pEvtHdr + 1);

            rc = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_DevIrqEvtDesc, 0 /*fFlags*/,
                                     pEvtHdr->idEvt, pEvtHdr->hEvtSrc, pEvtIoApicMsi->GCPhys, pEvtIoApicMsi->u32Val);
            break;
        }
        case DBGFTRACEREVT_GCPHYS_READ:
        case DBGFTRACEREVT_GCPHYS_WRITE:
        {
            PCRTTRACELOGEVTDESC pEvtDesc =   pEvtHdr->enmEvt == DBGFTRACEREVT_GCPHYS_WRITE
                                           ? &g_DevGCPhysWriteEvtDesc
                                           : &g_DevGCPhysReadEvtDesc;

            /* If the previous event ID is invalid this starts a new read/write we have to aggregate all the data for. */
            if (pEvtHdr->idEvtPrev == DBGF_TRACER_EVT_HDR_ID_INVALID)
            {
                PCDBGFTRACEREVTGCPHYS pEvtGCPhysRw = (PCDBGFTRACEREVTGCPHYS)(pEvtHdr + 1);
                rc = dbgfTracerR3EvtGCPhysRwStart(pThis, pEvtHdr, pEvtGCPhysRw, pEvtDesc);
            }
            else
            {
                /* Continuation of a started read or write, look up the right tracking structure and process the new data. */
                void *pvData = pEvtHdr + 1;
                rc = dbgfTracerR3EvtRwContinue(pThis, pEvtHdr, pvData);
            }
            break;
        }
        default:
            AssertLogRelMsgFailed(("Invalid or unsupported event: %u!\n", pEvtHdr->enmEvt));
            break;
    }

    return rc;
}


/**
 * @callback_method_impl{FNRTTHREAD,
 *      DBGF Tracer flush thread}
 */
static DECLCALLBACK(int) dbgfR3TracerThreadFlush(RTTHREAD ThreadSelf, void *pvUser)
{
    PDBGFTRACERINSR3 pThis = (PDBGFTRACERINSR3)pvUser;
    PDBGFTRACERSHARED pShared = pThis->pSharedR3;
    PSUPDRVSESSION pSession = pThis->pVMR3->pSession;

    /* Release the waiter. */
    RTThreadUserSignal(ThreadSelf);

    /*
     * Process stuff until we're told to terminate.
     */
    for (;;)
    {
        ASMAtomicXchgBool(&pShared->fFlushThrdActive, false);
        if (!ASMAtomicXchgBool(&pShared->fEvtsWaiting, false))
        {
            int rc = SUPSemEventWaitNoResume(pSession, pShared->hSupSemEvtFlush, RT_INDEFINITE_WAIT);
            Assert(RT_SUCCESS(rc) || rc == VERR_INTERRUPTED); RT_NOREF(rc);

            if (RT_UNLIKELY(ASMAtomicReadBool(&pThis->fShutdown)))
                break;
        }

        ASMAtomicXchgBool(&pShared->fFlushThrdActive, true);

        uint64_t idEvtNow = ASMAtomicReadU64(&pShared->idEvt);
        uint64_t idEvt = pThis->idEvtLast;
        size_t cRingBufEvts = pShared->cbRingBuf / DBGF_TRACER_EVT_SZ;
        while (idEvt < idEvtNow)
        {
            uint64_t idxRingBuf = idEvt % cRingBufEvts; /* This gives the index in the ring buffer for the event. */
            PDBGFTRACEREVTHDR pEvtHdr = (PDBGFTRACEREVTHDR)(pThis->CTX_SUFF(pbRingBuf) + idxRingBuf * DBGF_TRACER_EVT_SZ);

            /*
             * If the event header contains the invalid ID the producer was interrupted or didn't get that far yet, spin a bit
             * and wait for the ID to become valid.
             */
            while (ASMAtomicReadU64(&pEvtHdr->idEvt) == DBGF_TRACER_EVT_HDR_ID_INVALID)
                RTThreadYield();

            int rc = dbgfR3TracerEvtProcess(pThis, pEvtHdr);
            if (RT_FAILURE(rc))
                LogRelMax(10, ("DBGF: Writing event failed with %Rrc, tracing log will be incomplete!\n", rc));

            ASMAtomicWriteU64(&pEvtHdr->idEvt, DBGF_TRACER_EVT_HDR_ID_INVALID);
            idEvt++;
        }

        pThis->idEvtLast = idEvt;
        ASMAtomicXchgBool(&pShared->fEvtsWaiting, false);
    }

    return VINF_SUCCESS;
}


/**
 * Registers a possible event descriptors with the created trace log for faster subsequent operations.
 *
 * @returns VBox status code.
 * @param   pThis                   The DBGF tracer instance.
 */
static int dbgfR3TracerTraceLogEvtDescRegister(PDBGFTRACERINSR3 pThis)
{
    int rc = RTTraceLogWrAddEvtDesc(pThis->hTraceLog, &g_DevMmioMapEvtDesc);
    if (RT_SUCCESS(rc))
        rc = RTTraceLogWrAddEvtDesc(pThis->hTraceLog, &g_DevMmioUnmapEvtDesc);
    if (RT_SUCCESS(rc))
        rc = RTTraceLogWrAddEvtDesc(pThis->hTraceLog, &g_DevMmioReadEvtDesc);
    if (RT_SUCCESS(rc))
        rc = RTTraceLogWrAddEvtDesc(pThis->hTraceLog, &g_DevMmioWriteEvtDesc);
    if (RT_SUCCESS(rc))
        rc = RTTraceLogWrAddEvtDesc(pThis->hTraceLog, &g_DevIoPortMapEvtDesc);
    if (RT_SUCCESS(rc))
        rc = RTTraceLogWrAddEvtDesc(pThis->hTraceLog, &g_DevIoPortUnmapEvtDesc);
    if (RT_SUCCESS(rc))
        rc = RTTraceLogWrAddEvtDesc(pThis->hTraceLog, &g_DevIoPortReadEvtDesc);
    if (RT_SUCCESS(rc))
        rc = RTTraceLogWrAddEvtDesc(pThis->hTraceLog, &g_DevIoPortWriteEvtDesc);
    if (RT_SUCCESS(rc))
        rc = RTTraceLogWrAddEvtDesc(pThis->hTraceLog, &g_DevIrqEvtDesc);
    if (RT_SUCCESS(rc))
        rc = RTTraceLogWrAddEvtDesc(pThis->hTraceLog, &g_DevIoApicMsiEvtDesc);

    return rc;
}


/**
 * Initializes the R3 and shared tarcer instance data and spins up the flush thread.
 *
 * @returns VBox status code.
 * @param   pThis                   The DBGF tracer instance.
 * @param   pszTraceFilePath        The path of the trace file to create.
 */
static int dbgfR3TracerInitR3(PDBGFTRACERINSR3 pThis, const char *pszTraceFilePath)
{
    PVM pVM = pThis->pVMR3;
    PDBGFTRACERSHARED pShared = pThis->pSharedR3;

    pThis->fShutdown = false;

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aGstMemRwData); i++)
        pThis->aGstMemRwData[i].idEvtStart = DBGF_TRACER_EVT_HDR_ID_INVALID;

    /* Try to create a file based trace log. */
    int rc = RTTraceLogWrCreateFile(&pThis->hTraceLog, RTBldCfgVersion(), pszTraceFilePath);
    AssertLogRelRCReturn(rc, rc);

    rc = dbgfR3TracerTraceLogEvtDescRegister(pThis);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Go through the whole ring buffer and initialize the event IDs of all entries
     * to invalid values.
     */
    uint64_t cEvtEntries = pShared->cbRingBuf / DBGF_TRACER_EVT_SZ;
    PDBGFTRACEREVTHDR pEvtHdr = (PDBGFTRACEREVTHDR)pThis->pbRingBufR3;
    for (uint32_t i = 0; i < cEvtEntries; i++)
    {
        pEvtHdr->idEvt = DBGF_TRACER_EVT_HDR_ID_INVALID;
        pEvtHdr++;
    }

    rc = SUPSemEventCreate(pVM->pSession, &pShared->hSupSemEvtFlush);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadCreate(&pThis->hThrdFlush, dbgfR3TracerThreadFlush, pThis, 0 /*cbStack*/, RTTHREADTYPE_IO,
                            RTTHREADFLAGS_WAITABLE, "DBGFTracer");
        if (RT_SUCCESS(rc))
        {
            rc = RTThreadUserWait(pThis->hThrdFlush, 10 * 1000);
            if (RT_SUCCESS(rc))
            {
                return VINF_SUCCESS;
            }
        }

        SUPSemEventClose(pVM->pSession, pShared->hSupSemEvtFlush);
    }

    return rc;
}


/**
 * Creates a DBGF tracer based on the given config and returns it.
 *
 * @returns VBox status code.
 * @param   pVM                     The cross context VM structure.
 * @param   fR0Enabled              Flag whether the tracer should have R0 support enabled.
 * @param   pszTraceFilePath        The path of the trace file to create.
 * @param   cbRingBuf               Size of the ring buffer in bytes.
 * @param   ppDbgfTracerR3          Where to store the pointer to the tracer on success.
 */
static int dbgfR3TracerCreate(PVM pVM, bool fR0Enabled, const char *pszTraceFilePath,
                              uint32_t cbRingBuf, PDBGFTRACERINSR3 *ppDbgfTracerR3)
{
    PDBGFTRACERINSR3 pThis = NULL;

    /*
     * Allocate the tracer instance.
     */
    if ((fR0Enabled /*|| fRCEnabled*/) && !SUPR3IsDriverless())
    {
        AssertLogRel(fR0Enabled /* not possible to just enabled raw-mode atm. */);

        DBGFTRACERCREATEREQ Req;
        Req.Hdr.u32Magic     = SUPVMMR0REQHDR_MAGIC;
        Req.Hdr.cbReq        = sizeof(Req);
        Req.pTracerInsR3     = NULL;
        Req.cbRingBuf        = cbRingBuf;
        Req.fRCEnabled       = false; /*fRCEnabled;*/
        Req.afReserved[0]    = false;
        Req.afReserved[1]    = false;
        Req.afReserved[2]    = false;
        int rc = VMMR3CallR0Emt(pVM, pVM->apCpusR3[0], VMMR0_DO_DBGF_TRACER_CREATE, 0, &Req.Hdr);
        AssertLogRelMsgRCReturn(rc, ("VMMR0_DO_DBGF_TRACER_CREATE failed: %Rrc\n", rc), rc);
        pThis = Req.pTracerInsR3;
    }
    else
    {
        /* The code in this else branch works by the same rules as the DBGFR0Tracer.cpp
           code, except there is only the ring-3 components of the tracer instance.
           Changes here may need to be reflected in DBGFR0Tracer.cpp and vice versa! */
        uint32_t cb = sizeof(DBGFTRACERINSR3);
        cb  = RT_ALIGN_32(cb, 64);
        const uint32_t offShared = cb;
        cb += sizeof(DBGFTRACERSHARED) + cbRingBuf;
        AssertLogRelMsgReturn(cb <= DBGF_MAX_TRACER_INSTANCE_SIZE_R3,
                              ("Tracer total instance size is to big: %u, max %u\n",
                               cb, DBGF_MAX_TRACER_INSTANCE_SIZE_R3),
                              VERR_ALLOCATION_TOO_BIG);

        int rc = MMR3HeapAllocZEx(pVM, MM_TAG_DBGF_TRACER, cb, (void **)&pThis);
        AssertLogRelMsgRCReturn(rc, ("Failed to allocate %zu bytes of instance data for tracer. rc=%Rrc\n",
                                     cb, rc), rc);

        /* Initialize it: */
        pThis->pNextR3     = NULL;
        pThis->pVMR3       = pVM;
        pThis->fR0Enabled  = false;
        pThis->pSharedR3   = (PDBGFTRACERSHARED)((uint8_t *)pThis + offShared);
        pThis->pbRingBufR3 = (uint8_t *)(pThis->pSharedR3 + 1);

        pThis->pSharedR3->idEvt            = 0;
        pThis->pSharedR3->cbRingBuf        = cbRingBuf;
        pThis->pSharedR3->fEvtsWaiting     = false;
        pThis->pSharedR3->fFlushThrdActive = false;
    }

    /* Initialize the rest of the R3 tracer instance and spin up the flush thread. */
    int rc = dbgfR3TracerInitR3(pThis, pszTraceFilePath);
    if (RT_SUCCESS(rc))
    {
        *ppDbgfTracerR3 = pThis;
        return rc;
    }

    /** @todo Cleanup. */
    LogFlow(("dbgfR3TracerCreate: returns %Rrc\n", rc));
    return rc;
}


/**
 * Initializes and configures the tracer if configured.
 *
 * @returns VBox status code.
 * @param   pVM                     The cross context VM pointer.
 */
DECLHIDDEN(int) dbgfR3TracerInit(PVM pVM)
{
    PUVM pUVM = pVM->pUVM;

    pUVM->dbgf.s.pTracerR3 = NULL;

    /*
     * Check the config and enable tracing if requested.
     */
    PCFGMNODE pDbgfNode = CFGMR3GetChild(CFGMR3GetRoot(pVM), "DBGF");
    bool fTracerEnabled;
    int rc = CFGMR3QueryBoolDef(pDbgfNode, "TracerEnabled", &fTracerEnabled, false);
    AssertRCReturn(rc, rc);
    if (fTracerEnabled)
    {
        bool fR0Enabled;
        uint32_t cbRingBuf = 0;
        char *pszTraceFilePath = NULL;
        rc = CFGMR3QueryBoolDef(pDbgfNode, "TracerR0Enabled", &fR0Enabled, false);
        if (RT_SUCCESS(rc))
            rc = CFGMR3QueryU32Def(pDbgfNode, "TracerRingBufSz", &cbRingBuf, _4M);
        if (RT_SUCCESS(rc))
            rc = CFGMR3QueryStringAlloc(pDbgfNode, "TracerFilePath", &pszTraceFilePath);
        if (RT_SUCCESS(rc))
        {
            AssertLogRelMsgReturn(cbRingBuf && cbRingBuf == (size_t)cbRingBuf,
                                  ("Tracing ringbuffer size %#RX64 is invalid\n", cbRingBuf),
                                  VERR_INVALID_PARAMETER);

            rc = dbgfR3TracerCreate(pVM, fR0Enabled, pszTraceFilePath, cbRingBuf, &pUVM->dbgf.s.pTracerR3);
        }

        if (pszTraceFilePath)
        {
            MMR3HeapFree(pszTraceFilePath);
            pszTraceFilePath = NULL;
        }
    }

    return rc;
}


/**
 * Terminates any configured tracer for the given VM instance.
 *
 * @param   pVM                     The cross context VM structure.
 */
DECLHIDDEN(void) dbgfR3TracerTerm(PVM pVM)
{
    PUVM pUVM = pVM->pUVM;

    if (pUVM->dbgf.s.pTracerR3)
    {
        PDBGFTRACERINSR3 pThis = pUVM->dbgf.s.pTracerR3;
        PDBGFTRACERSHARED pSharedR3 = pThis->CTX_SUFF(pShared);

        /* Tear down the flush thread. */
        ASMAtomicXchgBool(&pThis->fShutdown, true);
        SUPSemEventSignal(pVM->pSession, pSharedR3->hSupSemEvtFlush);

        int rc = RTThreadWait(pThis->hThrdFlush, RT_MS_30SEC, NULL);
        AssertLogRelMsgRC(rc, ("DBGF: Waiting for the tracer flush thread to terminate failed with %Rrc\n", rc));

        /* Close the trace log. */
        rc = RTTraceLogWrDestroy(pThis->hTraceLog);
        AssertLogRelMsgRC(rc, ("DBGF: Closing the trace log file failed with %Rrc\n", rc));

        SUPSemEventClose(pVM->pSession, pSharedR3->hSupSemEvtFlush);
        /* The instance memory is freed by MM or when the R0 component terminates. */
        pUVM->dbgf.s.pTracerR3 = NULL;
    }
}


/**
 * Registers a new event source with the given name and returns a tracer event source handle.
 *
 * @returns VBox status code.
 * @param   pVM                     The cross context VM structure.
 * @param   pszName                 The event source name.
 * @param   phEvtSrc                Where to return the handle to the event source on success.
 */
VMMR3_INT_DECL(int) DBGFR3TracerRegisterEvtSrc(PVM pVM, const char *pszName, PDBGFTRACEREVTSRC phEvtSrc)
{
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertReturn(pszName && *pszName != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(phEvtSrc, VERR_INVALID_POINTER);

    PUVM pUVM = pVM->pUVM;
    PDBGFTRACERINSR3 pThis = pUVM->dbgf.s.pTracerR3;

    DBGFTRACEREVTSRC hEvtSrc = ASMAtomicIncU64((volatile uint64_t *)&pThis->hEvtSrcNext) - 1;

    int rc = dbgfTracerR3EvtPostSingle(pVM, pThis, hEvtSrc, DBGFTRACEREVT_SRC_REGISTER,
                                       NULL /*pvEvtDesc*/, 0 /*cbEvtDesc*/, NULL /*pidEvt*/);
    if (RT_SUCCESS(rc))
        *phEvtSrc = hEvtSrc;

    return rc;
}


/**
 * Deregisters the given event source handle.
 *
 * @returns VBox status code.
 * @param   pVM                     The cross context VM structure.
 * @param   hEvtSrc                 The event source handle to deregister.
 */
VMMR3_INT_DECL(int) DBGFR3TracerDeregisterEvtSrc(PVM pVM, DBGFTRACEREVTSRC hEvtSrc)
{
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertReturn(hEvtSrc != NIL_DBGFTRACEREVTSRC, VERR_INVALID_HANDLE);

    PUVM pUVM = pVM->pUVM;
    PDBGFTRACERINSR3 pThis = pUVM->dbgf.s.pTracerR3;
    return dbgfTracerR3EvtPostSingle(pVM, pThis, hEvtSrc, DBGFTRACEREVT_SRC_DEREGISTER,
                                     NULL /*pvEvtDesc*/, 0 /*cbEvtDesc*/, NULL /*pidEvt*/);
}


/**
 * Registers an I/O port region create event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hRegion                 The I/O port region handle returned from IOM.
 * @param   cPorts                  Number of ports registered.
 * @param   fFlags                  Flags passed to IOM.
 * @param   iPciRegion              For a PCI device the region index used for the I/O ports.
 */
VMMR3_INT_DECL(int) DBGFR3TracerEvtIoPortCreate(PVM pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hRegion, RTIOPORT cPorts, uint32_t fFlags,
                                                uint32_t iPciRegion)
{
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertReturn(hEvtSrc != NIL_DBGFTRACEREVTSRC, VERR_INVALID_HANDLE);

    PUVM pUVM = pVM->pUVM;
    PDBGFTRACERINSR3 pThis = pUVM->dbgf.s.pTracerR3;

    DBGFTRACEREVTIOPORTCREATE EvtIoPortCreate;
    RT_ZERO(EvtIoPortCreate);
    EvtIoPortCreate.hIoPorts   = hRegion;
    EvtIoPortCreate.cPorts     = cPorts;
    EvtIoPortCreate.fIomFlags  = fFlags;
    EvtIoPortCreate.iPciRegion = iPciRegion;
    return dbgfTracerR3EvtPostSingle(pVM, pThis, hEvtSrc, DBGFTRACEREVT_IOPORT_REGION_CREATE,
                                     &EvtIoPortCreate, sizeof(EvtIoPortCreate), NULL /*pidEvt*/);
}


/**
 * Registers an MMIO region create event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hRegion                 The MMIO region handle returned from IOM.
 * @param   cbRegion                Size of the MMIO region in bytes.
 * @param   fFlags                  Flags passed to IOM.
 * @param   iPciRegion              For a PCI device the region index used for the MMIO region.
 */
VMMR3_INT_DECL(int) DBGFR3TracerEvtMmioCreate(PVM pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hRegion, RTGCPHYS cbRegion, uint32_t fFlags,
                                              uint32_t iPciRegion)
{
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertReturn(hEvtSrc != NIL_DBGFTRACEREVTSRC, VERR_INVALID_HANDLE);

    PUVM pUVM = pVM->pUVM;
    PDBGFTRACERINSR3 pThis = pUVM->dbgf.s.pTracerR3;

    DBGFTRACEREVTMMIOCREATE EvtMmioCreate;
    RT_ZERO(EvtMmioCreate);
    EvtMmioCreate.hMmioRegion = hRegion;
    EvtMmioCreate.cbRegion    = cbRegion;
    EvtMmioCreate.fIomFlags   = fFlags;
    EvtMmioCreate.iPciRegion  = iPciRegion;
    return dbgfTracerR3EvtPostSingle(pVM, pThis, hEvtSrc, DBGFTRACEREVT_MMIO_REGION_CREATE,
                                     &EvtMmioCreate, sizeof(EvtMmioCreate), NULL /*pidEvt*/);
}

