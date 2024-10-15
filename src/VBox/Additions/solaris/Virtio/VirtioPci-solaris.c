/* $Id: VirtioPci-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Guest Additions - Virtio Driver for Solaris, PCI Hypervisor Interface.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include "VirtioPci-solaris.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <VBox/log.h>

#include <sys/pci.h>
#include <sys/param.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*
 * Pci Register offsets.
 */
#define VIRTIO_PCI_HOST_FEATURES            0x00
#define VIRTIO_PCI_GUEST_FEATURES           0x04
#define VIRTIO_PCI_QUEUE_PFN                0x08
#define VIRTIO_PCI_QUEUE_NUM                0x0C
#define VIRTIO_PCI_QUEUE_SEL                0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY             0x10
#define VIRTIO_PCI_STATUS                   0x12
#define VIRTIO_PCI_ISR                      0x13
#define VIRTIO_PCI_CONFIG                   0x14

#define VIRTIO_PCI_RING_ALIGN               PAGE_SIZE
#define VIRTIO_PCI_QUEUE_ADDR_SHIFT         PAGE_SHIFT

/**
 * virtio_pci_t: Private data per device instance.
 */
typedef struct virtio_pci_t
{
    ddi_acc_handle_t    hIO;            /* IO handle */
    caddr_t             addrIOBase;     /* IO base address */
} virtio_pci_t;

/**
 * virtio_pci_queue_t: Private data per queue instance.
 */
typedef struct virtio_pci_queue_t
{
    ddi_dma_handle_t    hDMA;           /* DMA handle. */
    ddi_acc_handle_t    hIO;            /* IO handle. */
    size_t              cbBuf;          /* Physical address of buffer. */
    paddr_t             physBuf;        /* Size of buffer. */
    pfn_t               pageBuf;        /* Page frame number of buffer. */
} virtio_pci_queue_t;

static ddi_device_acc_attr_t g_VirtioPciAccAttrRegs =
{
    DDI_DEVICE_ATTR_V0,                 /* Version */
    DDI_STRUCTURE_LE_ACC,               /* Structural data access in little endian. */
    DDI_STRICTORDER_ACC,                /* Strict ordering. */
    DDI_DEFAULT_ACC                     /* Default access, possible panic on errors*/
};

static ddi_device_acc_attr_t g_VirtioPciAccAttrRing =
{
    DDI_DEVICE_ATTR_V0,                 /* Version. */
    DDI_NEVERSWAP_ACC,                  /* Data access with no byte swapping*/
    DDI_STRICTORDER_ACC,                /* Strict ordering. */
    DDI_DEFAULT_ACC                     /* Default access, possible panic on errors*/
};

static ddi_dma_attr_t g_VirtioPciDmaAttrRing =
{
    DMA_ATTR_V0,                        /* Version. */
    0,                                  /* Lowest usable address. */
    0xffffffffffffffffULL,              /* Highest usable address. */
    0x7fffffff,                         /* Maximum DMAable byte count. */
    VIRTIO_PCI_RING_ALIGN,              /* Alignment in bytes. */
    0x7ff,                              /* Bitmap of burst sizes */
    1,                                  /* Minimum transfer. */
    0xffffffffU,                        /* Maximum transfer. */
    0xffffffffffffffffULL,              /* Maximum segment length. */
    1,                                  /* Maximum number of segments. */
    1,                                  /* Granularity. */
    0                                   /* Flags (reserved). */
};

/** Pointer to the interrupt handle vector */
static ddi_intr_handle_t   *g_pIntr;
/** Number of actually allocated interrupt handles */
static size_t               g_cIntrAllocated;
/** The IRQ Mutex */
static kmutex_t             g_IrqMtx;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void    *VirtioPciAlloc(PVIRTIODEVICE pDevice);
static void     VirtioPciFree(PVIRTIODEVICE pDevice);
static int      VirtioPciAttach(PVIRTIODEVICE pDevice);
static int      VirtioPciDetach(PVIRTIODEVICE pDevice);
static uint32_t VirtioPciGetFeatures(PVIRTIODEVICE pDevice);
static void     VirtioPciSetFeatures(PVIRTIODEVICE pDevice, uint32_t fFeatures);
static int      VirtioPciNotifyQueue(PVIRTIODEVICE pDevice, PVIRTIOQUEUE pQueue);
static void     VirtioPciGet(PVIRTIODEVICE pDevice, off_t off, void *pv, size_t cb);
static void     VirtioPciSet(PVIRTIODEVICE pDevice, off_t off, void *pv, size_t cb);
static void    *VirtioPciGetQueue(PVIRTIODEVICE pDevice, PVIRTIOQUEUE pQueue);
static void     VirtioPciPutQueue(PVIRTIODEVICE pDevice, PVIRTIOQUEUE pQueue);
static void     VirtioPciSetStatus(PVIRTIODEVICE pDevice, uint8_t Status);

static uint_t   VirtioPciISR(caddr_t Arg);
static int      VirtioPciSetupIRQ(dev_info_t *pDip);
static void     VirtioPciRemoveIRQ(dev_info_t *pDip);

/**
 * Hypervisor operations for Virtio Pci.
 */
VIRTIOHYPEROPS g_VirtioHyperOpsPci =
{
    VirtioPciAlloc,
    VirtioPciFree,
    VirtioPciAttach,
    VirtioPciDetach,
    VirtioPciGetFeatures,
    VirtioPciSetFeatures,
    VirtioPciNotifyQueue,
    VirtioPciGet,
    VirtioPciSet,
    VirtioPciGetQueue,
    VirtioPciPutQueue,
    VirtioPciSetStatus
};


/**
 * Virtio Pci private data allocation routine.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 * @return Allocated private data structure which must only be freed by calling
 *         VirtioPciFree().
 */
static void *VirtioPciAlloc(PVIRTIODEVICE pDevice)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioPciAlloc pDevice=%p\n", pDevice));
    virtio_pci_t *pPciData = RTMemAllocZ(sizeof(virtio_pci_t));
    return pPciData;
}


/**
 * Virtio Pci private data deallocation routine.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 */
static void VirtioPciFree(PVIRTIODEVICE pDevice)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioPciFree pDevice=%p\n", pDevice));
    virtio_pci_t *pPciData = pDevice->pvHyper;
    if (pPciData)
    {
        RTMemFree(pDevice->pvHyper);
        pDevice->pvHyper = NULL;
    }
}


/**
 * Virtio Pci attach routine, called from driver attach.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 *
 * @return Solaris DDI error code. DDI_SUCCESS or DDI_FAILURE.
 */
static int VirtioPciAttach(PVIRTIODEVICE pDevice)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioPciAttach pDevice=%p\n", pDevice));
    virtio_pci_t *pPciData = pDevice->pvHyper;
    AssertReturn(pPciData, DDI_FAILURE);

    int rc = ddi_regs_map_setup(pDevice->pDip,
                                1,                       /* reg. num */
                                &pPciData->addrIOBase,
                                0,                       /* offset */
                                0,                       /* length */
                                &g_VirtioPciAccAttrRegs,
                                &pPciData->hIO);
    if (rc == DDI_SUCCESS)
    {
        /*
         * Reset the device.
         */
        VirtioPciSetStatus(pDevice, 0);

        /*
         * Add interrupt handler.
         */
        VirtioPciSetupIRQ(pDevice->pDip);

        LogFlow((VIRTIOLOGNAME ":VirtioPciAttach: successfully mapped registers.\n"));
        return DDI_SUCCESS;
    }
    else
        LogRel((VIRTIOLOGNAME ":VirtioPciAttach: ddi_regs_map_setup failed. rc=%d\n", rc));
    return DDI_FAILURE;
}


/**
 * Virtio Pci detach routine, called from driver detach.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 *
 * @return Solaris DDI error code. DDI_SUCCESS or DDI_FAILURE.
 */
static int VirtioPciDetach(PVIRTIODEVICE pDevice)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioPciDetach pDevice=%p\n", pDevice));
    virtio_pci_t *pPciData = pDevice->pvHyper;
    AssertReturn(pPciData, DDI_FAILURE);

    VirtioPciRemoveIRQ(pDevice->pDip);
    ddi_regs_map_free(&pPciData->hIO);
    return DDI_SUCCESS;
}


/**
 * Get host supported features.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 *
 * @return Mask of host features.
 */
static uint32_t VirtioPciGetFeatures(PVIRTIODEVICE pDevice)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioPciGetFeatures pDevice=%p\n", pDevice));
    virtio_pci_t *pPciData = pDevice->pvHyper;
    AssertReturn(pPciData, 0);

    return ddi_get32(pPciData->hIO, (uint32_t *)(pPciData->addrIOBase + VIRTIO_PCI_HOST_FEATURES));
}


/**
 * Set guest supported features.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 * @param u32Features       Mask of guest supported features.
 */
static void VirtioPciSetFeatures(PVIRTIODEVICE pDevice, uint32_t u32Features)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioPciSetFeatures pDevice=%p\n", pDevice));
    virtio_pci_t *pPciData = pDevice->pvHyper;
    AssertReturnVoid(pPciData);

    ddi_put32(pPciData->hIO, (uint32_t *)(pPciData->addrIOBase + VIRTIO_PCI_GUEST_FEATURES), u32Features);
}


/**
 * Update the queue, notify the host.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 * @param pQueue            Pointer to the Queue that is doing the notification.
 *
 * @return Solaris DDI error code. DDI_SUCCESS or DDI_FAILURE.
 */
static int VirtioPciNotifyQueue(PVIRTIODEVICE pDevice, PVIRTIOQUEUE pQueue)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioPciNotifyQueue pDevice=%p pQueue=%p\n", pDevice, pQueue));
    virtio_pci_t *pPciData = pDevice->pvHyper;
    AssertReturn(pPciData, DDI_FAILURE);

    pQueue->Ring.pRingAvail->Index += pQueue->cBufs;
    pQueue->cBufs = 0;

    ASMCompilerBarrier();

    ddi_put16(pPciData->hIO, (uint16_t *)(pPciData->addrIOBase + VIRTIO_PCI_QUEUE_NOTIFY), pQueue->QueueIndex);
    cmn_err(CE_NOTE, "VirtioPciNotifyQueue\n");
}



/**
 * Virtio Pci set (write) routine.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 * @param off               Offset into the PCI config space.
 * @param pv                Pointer to the buffer to write from.
 * @param cb                Size of the buffer in bytes.
 */
static void VirtioPciSet(PVIRTIODEVICE pDevice, off_t off, void *pv, size_t cb)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioPciSet pDevice=%p\n", pDevice));
    virtio_pci_t *pPciData = pDevice->pvHyper;
    AssertReturnVoid(pPciData);

    uint8_t *pb = pv;
    for (size_t i = 0; i < cb; i++, pb++)
        ddi_put8(pPciData->hIO, (uint8_t *)(pPciData->addrIOBase + VIRTIO_PCI_CONFIG + off + i), *pb);
}


/**
 * Virtio Pci get (read) routine.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 * @param off               Offset into the PCI config space.
 * @param pv                Where to store the read data.
 * @param cb                Size of the buffer in bytes.
 */
static void VirtioPciGet(PVIRTIODEVICE pDevice, off_t off, void *pv, size_t cb)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioPciGet pDevice=%p off=%u pv=%p cb=%u\n", pDevice, off, pv, cb));
    virtio_pci_t *pPciData = pDevice->pvHyper;
    AssertReturnVoid(pPciData);

    uint8_t *pb = pv;
    for (size_t i = 0; i < cb; i++, pb++)
        *pb = ddi_get8(pPciData->hIO, (uint8_t *)(pPciData->addrIOBase + VIRTIO_PCI_CONFIG + off + i));
}


/**
 * Virtio Pci put queue routine. Places the queue and frees associated queue.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 * @param pQueue            Pointer to the queue.
 */
static void VirtioPciPutQueue(PVIRTIODEVICE pDevice, PVIRTIOQUEUE pQueue)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioPciPutQueue pDevice=%p pQueue=%p\n", pDevice, pQueue));
    AssertReturnVoid(pDevice);
    AssertReturnVoid(pQueue);

    virtio_pci_t *pPci = pDevice->pvHyper;
    AssertReturnVoid(pPci);
    virtio_pci_queue_t *pPciQueue = pQueue->pvData;
    if (RT_UNLIKELY(!pPciQueue))
    {
        LogRel((VIRTIOLOGNAME ":VirtioPciPutQueue missing Pci queue.\n"));
        return;
    }

    ddi_put16(pPci->hIO, (uint16_t *)(pPci->addrIOBase + VIRTIO_PCI_QUEUE_SEL), pQueue->QueueIndex);
    ddi_put32(pPci->hIO, (uint32_t *)(pPci->addrIOBase + VIRTIO_PCI_QUEUE_PFN), 0);

    ddi_dma_unbind_handle(pPciQueue->hDMA);
    ddi_dma_mem_free(&pPciQueue->hIO);
    ddi_dma_free_handle(&pPciQueue->hDMA);
    RTMemFree(pPciQueue);
}


/**
 * Virtio Pci get queue routine. Allocates a PCI queue and DMA resources.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 * @param pQueue            Where to store the queue.
 *
 * @return An allocated Virtio Pci queue, or NULL in case of errors.
 */
static void *VirtioPciGetQueue(PVIRTIODEVICE pDevice, PVIRTIOQUEUE pQueue)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioPciGetQueue pDevice=%p pQueue=%p\n", pDevice, pQueue));
    AssertReturn(pDevice, NULL);

    virtio_pci_t *pPci = pDevice->pvHyper;
    AssertReturn(pPci, NULL);

    /*
     * Select a Queue.
     */
    ddi_put16(pPci->hIO, (uint16_t *)(pPci->addrIOBase + VIRTIO_PCI_QUEUE_SEL), pQueue->QueueIndex);

    /*
     * Get the currently selected Queue's size.
     */
    pQueue->Ring.cDesc = ddi_get16(pPci->hIO, (uint16_t *)(pPci->addrIOBase + VIRTIO_PCI_QUEUE_NUM));
    if (RT_UNLIKELY(!pQueue->Ring.cDesc))
    {
        LogRel((VIRTIOLOGNAME ": VirtioPciGetQueue: Queue[%d] has no descriptors.\n", pQueue->QueueIndex));
        return NULL;
    }

    /*
     * Check if it's already active.
     */
    uint32_t QueuePFN = ddi_get32(pPci->hIO, (uint32_t *)(pPci->addrIOBase + VIRTIO_PCI_QUEUE_PFN));
    if (QueuePFN != 0)
    {
        LogRel((VIRTIOLOGNAME ":VirtioPciGetQueue: Queue[%d] is already used.\n", pQueue->QueueIndex));
        return NULL;
    }

    LogFlow(("Queue[%d] has %d slots.\n", pQueue->QueueIndex, pQueue->Ring.cDesc));

    /*
     * Allocate and initialize Pci queue data.
     */
    virtio_pci_queue_t *pPciQueue = RTMemAllocZ(sizeof(virtio_pci_queue_t));
    if (pPciQueue)
    {
        /*
         * Setup DMA.
         */
        size_t cbQueue = VirtioRingSize(pQueue->Ring.cDesc, VIRTIO_PCI_RING_ALIGN);
        int rc = ddi_dma_alloc_handle(pDevice->pDip, &g_VirtioPciDmaAttrRing, DDI_DMA_SLEEP, 0 /* addr */, &pPciQueue->hDMA);
        if (rc == DDI_SUCCESS)
        {
            rc = ddi_dma_mem_alloc(pPciQueue->hDMA, cbQueue, &g_VirtioPciAccAttrRing, DDI_DMA_CONSISTENT,
                                   DDI_DMA_SLEEP, 0 /* addr */, &pQueue->pQueue, &pPciQueue->cbBuf,
                                   &pPciQueue->hIO);
            if (rc == DDI_SUCCESS)
            {
                AssertRelease(pPciQueue->cbBuf >= cbQueue);
                ddi_dma_cookie_t DmaCookie;
                uint_t cCookies;
                rc = ddi_dma_addr_bind_handle(pPciQueue->hDMA, NULL /* addrspace */, pQueue->pQueue, pPciQueue->cbBuf,
                                              DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
                                              0 /* addr */, &DmaCookie, &cCookies);
                if (rc == DDI_SUCCESS)
                {
                    pPciQueue->physBuf = DmaCookie.dmac_laddress;
                    pPciQueue->pageBuf = pPciQueue->physBuf >> VIRTIO_PCI_QUEUE_ADDR_SHIFT;

                    LogFlow((VIRTIOLOGNAME ":VirtioPciGetQueue: Queue[%d]%p physBuf=%x pfn of Buf %#x\n", pQueue->QueueIndex,
                             pQueue->pQueue, pPciQueue->physBuf, pPciQueue->pageBuf));
                    cmn_err(CE_NOTE, ":VirtioPciGetQueue: Queue[%d]%p physBuf=%x pfn of Buf %x\n", pQueue->QueueIndex,
                             pQueue->pQueue, pPciQueue->physBuf, pPciQueue->pageBuf);

                    /*
                     * Activate the queue and initialize a ring for the queue.
                     */
                    memset(pQueue->pQueue, 0, pPciQueue->cbBuf);
                    ddi_put32(pPci->hIO, (uint32_t *)(pPci->addrIOBase + VIRTIO_PCI_QUEUE_PFN), pPciQueue->pageBuf);
                    VirtioRingInit(pQueue, pQueue->Ring.cDesc, pQueue->pQueue, VIRTIO_PCI_RING_ALIGN);
                    return pPciQueue;
                }
                else
                    LogRel((VIRTIOLOGNAME ":VirtioPciGetQueue: ddi_dma_addr_bind_handle failed. rc=%d\n", rc));

                ddi_dma_mem_free(&pPciQueue->hIO);
            }
            else
                LogRel((VIRTIOLOGNAME ":VirtioPciGetQueue: ddi_dma_mem_alloc failed for %u bytes rc=%d\n", cbQueue, rc));

            ddi_dma_free_handle(&pPciQueue->hDMA);
        }
        else
            LogRel((VIRTIOLOGNAME ":VirtioPciGetQueue: ddi_dma_alloc_handle failed. rc=%d\n", rc));

        RTMemFree(pPciQueue);
    }
    else
        LogRel((VIRTIOLOGNAME ":VirtioPciGetQueue: failed to alloc %u bytes for Pci Queue data.\n", sizeof(virtio_pci_queue_t)));

    return NULL;
}


/**
 * Set the Virtio PCI status bit.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 * @param Status            The status to set.
 */
static void VirtioPciSetStatus(PVIRTIODEVICE pDevice, uint8_t Status)
{
    virtio_pci_t *pPciData = pDevice->pvHyper;
    ddi_put8(pPciData->hIO, (uint8_t *)(pPciData->addrIOBase + VIRTIO_PCI_STATUS), Status);
}


/**
 * Sets up IRQ for Virtio PCI.
 *
 * @param   pDip     Pointer to the device info structure.
 *
 * @return Solaris error code.
 */
static int VirtioPciSetupIRQ(dev_info_t *pDip)
{
    LogFlow((VIRTIOLOGNAME ":VirtioPciSetupIRQ: pDip=%p\n", pDip));
    cmn_err(CE_NOTE, "VirtioPciSetupIRQ\n");

    int IntrType = 0;
    int rc = ddi_intr_get_supported_types(pDip, &IntrType);
    if (rc == DDI_SUCCESS)
    {
        /* We won't need to bother about MSIs. */
        if (IntrType & DDI_INTR_TYPE_FIXED)
        {
            int IntrCount = 0;
            rc = ddi_intr_get_nintrs(pDip, IntrType, &IntrCount);
            if (   rc == DDI_SUCCESS
                && IntrCount > 0)
            {
                int IntrAvail = 0;
                rc = ddi_intr_get_navail(pDip, IntrType, &IntrAvail);
                if (   rc == DDI_SUCCESS
                    && IntrAvail > 0)
                {
                    /* Allocated kernel memory for the interrupt handles. The allocation size is stored internally. */
                    g_pIntr = RTMemAllocZ(IntrCount * sizeof(ddi_intr_handle_t));
                    if (g_pIntr)
                    {
                        int IntrAllocated;
                        rc = ddi_intr_alloc(pDip, g_pIntr, IntrType, 0, IntrCount, &IntrAllocated, DDI_INTR_ALLOC_NORMAL);
                        if (   rc == DDI_SUCCESS
                            && IntrAllocated > 0)
                        {
                            g_cIntrAllocated = IntrAllocated;
                            uint_t uIntrPriority;
                            rc = ddi_intr_get_pri(g_pIntr[0], &uIntrPriority);
                            if (rc == DDI_SUCCESS)
                            {
                                /* Initialize the mutex. */
                                mutex_init(&g_IrqMtx, NULL, MUTEX_DRIVER, DDI_INTR_PRI(uIntrPriority));

                                /* Assign interrupt handler functions and enable interrupts. */
                                for (int i = 0; i < IntrAllocated; i++)
                                {
                                    rc = ddi_intr_add_handler(g_pIntr[i], (ddi_intr_handler_t *)VirtioPciISR,
                                                            NULL /* No Private Data */, NULL);
                                    if (rc == DDI_SUCCESS)
                                        rc = ddi_intr_enable(g_pIntr[i]);
                                    if (rc != DDI_SUCCESS)
                                    {
                                        /* Changing local IntrAllocated to hold so-far allocated handles for freeing. */
                                        IntrAllocated = i;
                                        break;
                                    }
                                }
                                if (rc == DDI_SUCCESS)
                                {
                                    cmn_err(CE_NOTE, "VirtioPciSetupIRQ success\n");
                                    return rc;
                                }

                                /* Remove any assigned handlers */
                                LogRel((VIRTIOLOGNAME ":VirtioPciSetupIRQ failed to assign IRQs allocated=%d\n", IntrAllocated));
                                for (int x = 0; x < IntrAllocated; x++)
                                    ddi_intr_remove_handler(g_pIntr[x]);
                            }
                            else
                                LogRel((VIRTIOLOGNAME ":VirtioPciSetupIRQ failed to get priority of interrupt. rc=%d\n", rc));

                            /* Remove allocated IRQs, too bad we can free only one handle at a time. */
                            for (int k = 0; k < g_cIntrAllocated; k++)
                                ddi_intr_free(g_pIntr[k]);
                        }
                        else
                            LogRel((VIRTIOLOGNAME ":VirtioPciSetupIRQ: failed to allocated IRQs. count=%d\n", IntrCount));
                        RTMemFree(g_pIntr);
                    }
                    else
                        LogRel((VIRTIOLOGNAME ":VirtioPciSetupIRQ: failed to allocated IRQs. count=%d\n", IntrCount));
                }
                else
                {
                    LogRel((VIRTIOLOGNAME ":VirtioPciSetupIRQ: failed to get or insufficient available IRQs. rc=%d IntrAvail=%d\n",
                            rc, IntrAvail));
                }
            }
            else
            {
                LogRel((VIRTIOLOGNAME ":VirtioPciSetupIRQ: failed to get or insufficient number of IRQs. rc=%d IntrCount=%d\n", rc,
                        IntrCount));
            }
        }
        else
            LogRel((VIRTIOLOGNAME ":VirtioPciSetupIRQ: invalid irq type. IntrType=%#x\n", IntrType));
    }
    else
        LogRel((VIRTIOLOGNAME ":VirtioPciSetupIRQ: failed to get supported interrupt types\n"));
    return rc;
}


/**
 * Removes IRQ for Virtio PCI device.
 *
 * @param   pDip     Pointer to the device info structure.
 */
static void VirtioPciRemoveIRQ(dev_info_t *pDip)
{
    LogFlow((VIRTIOLOGNAME ":VirtioPciRemoveIRQ pDip=%p:\n", pDip));

    for (int i = 0; i < g_cIntrAllocated; i++)
    {
        int rc = ddi_intr_disable(g_pIntr[i]);
        if (rc == DDI_SUCCESS)
        {
            rc = ddi_intr_remove_handler(g_pIntr[i]);
            if (rc == DDI_SUCCESS)
                ddi_intr_free(g_pIntr[i]);
        }
    }
    RTMemFree(g_pIntr);
    mutex_destroy(&g_IrqMtx);
}


/**
 * Interrupt Service Routine for Virtio PCI device.
 *
 * @param   Arg     Private data (unused, will be NULL).
 * @returns DDI_INTR_CLAIMED if it's our interrupt, DDI_INTR_UNCLAIMED if it isn't.
 */
static uint_t VirtioPciISR(caddr_t Arg)
{
    LogFlow((VIRTIOLOGNAME ":VBoxGuestSolarisISR\n"));
    cmn_err(CE_NOTE, "VBoxGuestSolarisISRd Arg=%p\n", Arg);

    mutex_enter(&g_IrqMtx);
    bool fOurIRQ = false;
    /*
     * Call the DeviceOps ISR routine somehow which should notify all Virtio queues
     * on the interrupt.
     */
    mutex_exit(&g_IrqMtx);

    return fOurIRQ ? DDI_INTR_CLAIMED : DDI_INTR_UNCLAIMED;
}

