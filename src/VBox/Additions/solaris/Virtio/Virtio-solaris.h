/* $Id: Virtio-solaris.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Guest Additions: Virtio Driver for Solaris, header.
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

#ifndef GA_INCLUDED_SRC_solaris_Virtio_Virtio_solaris_h
#define GA_INCLUDED_SRC_solaris_Virtio_Virtio_solaris_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <sys/sunddi.h>

/** Release log descriptive prefix. */
#define VIRTIOLOGNAME           "Virtio"
/** Buffer continues via the Next field */
#define VIRTIO_FLAGS_RING_DESC_NEXT       RT_BIT(0)
/** Buffer is write-only, else is read-only.  */
#define VIRTIO_FLAGS_RING_DESC_WRITE      RT_BIT(1)
/** Indirect buffer (buffer contains list of buffer descriptors) */
#define VIRTIO_FLAGS_RING_DESC_INDIRECT   RT_BIT(2)

/* Values from our Virtio.h */
#define VIRTIO_PCI_STATUS_ACK             0x01
#define VIRTIO_PCI_STATUS_DRV             0x02
#define VIRTIO_PCI_STATUS_DRV_OK          0x04
#define VIRTIO_PCI_STATUS_FAILED          0x80

/**
 * The ring descriptor table refers to the buffers the guest is using for the
 * device.
 */
struct VirtioRingDesc
{
    uint64_t                AddrBuf;            /* Physical address of buffer. */
    uint32_t                cbBuf;              /* Length of the buffer in bytes. */
    uint16_t                fFlags;             /* Flags of the next buffer. */
    uint16_t                Next;               /* Index of the next buffer. */
};
typedef struct VirtioRingDesc VIRTIORINGDESC;
typedef VIRTIORINGDESC *PVIRTIORINGDESC;

/**
 * The available ring refers to what descriptors are being offered to the
 * device.
 */
struct VirtioRingAvail
{
    uint16_t                fFlags;             /* Interrupt suppression flag. */
    uint16_t                Index;              /* Index of available ring. */
    uint16_t                aRings[1];          /* Array of indices into descriptor table. */
};
typedef struct VirtioRingAvail VIRTIORINGAVAIL;
typedef VIRTIORINGAVAIL *PVIRTIORINGAVAIL;

/**
 * The used ring refers to the buffers the device is done using them. The
 * element is a pair-descriptor refers to the buffer once the device is done
 * with the buffer.
 */
struct VirtioRingUsedElem
{
    uint32_t                Index;              /* Index of start of used descriptor chain. */
    uint32_t                cbElem;             /* Number of bytes written into the buffer. */
};
typedef struct VirtioRingUsedElem VIRTIORINGUSEDELEM;
typedef VIRTIORINGUSEDELEM *PVIRTIORINGUSEDELEM;

/**
 * The Virtio Ring which contains the descriptors.
 */
struct VirtioRing
{
    uint_t                  cDesc;              /* Number of descriptors. */
    PVIRTIORINGDESC         pRingDesc;          /* Pointer to ring descriptor. */
    PVIRTIORINGAVAIL        pRingAvail;         /* Pointer to available ring. */
    PVIRTIORINGUSEDELEM     pRingUsedElem;      /* Pointer to used ring element. */
};
typedef struct VirtioRing VIRTIORING;
typedef VIRTIORING *PVIRTIORING;

struct VirtioDevice;
struct VirtioQueue;

typedef void     *(*PFNVIRTIOALLOC)(struct VirtioDevice *pDevice);
typedef void      (*PFNVIRTIOFREE)(struct VirtioDevice *pDevice);
typedef int       (*PFNVIRTIOATTACH)(struct VirtioDevice *pDevice);
typedef int       (*PFNVIRTIODETACH)(struct VirtioDevice *pDevice);
typedef uint32_t  (*PFNVIRTIOGETFEATURES)(struct VirtioDevice *pDevice);
typedef void      (*PFNVIRTIOSETFEATURES)(struct VirtioDevice *pDevice, uint32_t fFeatures);
typedef void      (*PFNVIRTIOGET)(struct VirtioDevice *pDevice, off_t off, void *pv, size_t cb);
typedef void      (*PFNVIRTIOSET)(struct VirtioDevice *pDevice, off_t off, void *pv, size_t cb);
typedef void     *(*PFNVIRTIOGETQUEUE)(struct VirtioDevice *pDevice, struct VirtioQueue *pQueue);
typedef void      (*PFNVIRTIOPUTQUEUE)(struct VirtioDevice *pDevice, struct VirtioQueue *pQueue);
typedef int       (*PFNVIRTIONOTIFYQUEUE)(struct VirtioDevice *pDevice, struct VirtioQueue *pQueue);
typedef void      (*PFNVIRTIOSETSTATUS)(struct VirtioDevice *pDevice, uint8_t Status);

/**
 * Virtio device operations.
 */
struct VirtioDeviceOps
{
    PFNVIRTIOALLOC          pfnAlloc;           /* Device alloc. */
    PFNVIRTIOFREE           pfnFree;            /* Device free. */
    PFNVIRTIOATTACH         pfnAttach;          /* Device attach. */
    PFNVIRTIODETACH         pfnDetach;          /* Device detach. */
};
typedef struct VirtioDeviceOps VIRTIODEVICEOPS;
typedef VIRTIODEVICEOPS *PVIRTIODEVICEOPS;

/**
 * Hypervisor access operations.
 */
struct VirtioHyperOps
{
    PFNVIRTIOALLOC          pfnAlloc;           /* Hypervisor alloc. */
    PFNVIRTIOFREE           pfnFree;            /* Hypervisor free */
    PFNVIRTIOATTACH         pfnAttach;          /* Hypervisor attach. */
    PFNVIRTIODETACH         pfnDetach;          /* Hypervisor detach. */
    PFNVIRTIOGETFEATURES    pfnGetFeatures;     /* Hypervisor get features. */
    PFNVIRTIOSETFEATURES    pfnSetFeatures;     /* Hypervisor set features. */
    PFNVIRTIONOTIFYQUEUE    pfnNotifyQueue;     /* Hypervisor notify queue. */
    PFNVIRTIOGET            pfnGet;             /* Hypervisor get. */
    PFNVIRTIOSET            pfnSet;             /* Hypervisor set. */
    PFNVIRTIOGETQUEUE       pfnGetQueue;        /* Hypervisor get queue. */
    PFNVIRTIOPUTQUEUE       pfnPutQueue;        /* Hypervisor put queue. */
    PFNVIRTIOSETSTATUS      pfnSetStatus;       /* Hypervisor set status. */
};
typedef struct VirtioHyperOps VIRTIOHYPEROPS;
typedef VIRTIOHYPEROPS *PVIRTIOHYPEROPS;

/**
 * Virtio Queue into which buffers are posted.
 */
struct VirtioQueue
{
    VIRTIORING              Ring;               /* Ring buffer of this queue. */
    uint16_t                cBufs;              /* Number of pushed, unnotified buffers. */
    uint16_t                FreeHeadIndex;      /* Index of head of free list. */
    uint16_t                QueueIndex;         /* Index of this queue. */
    caddr_t                 pQueue;             /* Allocated DMA region for queue. */
    void                   *pvData;             /* Queue private data. */
};
typedef struct VirtioQueue VIRTIOQUEUE;
typedef VIRTIOQUEUE *PVIRTIOQUEUE;

/**
 * Virtio device descriptor, common to all Virtio devices.
 */
struct VirtioDevice
{
    dev_info_t             *pDip;               /* OS device info. */
    PVIRTIODEVICEOPS        pDeviceOps;         /* Device hooks. */
    void                   *pvDevice;           /* Device opaque data. */
    PVIRTIOHYPEROPS         pHyperOps;          /* Hypervisor hooks. */
    void                   *pvHyper;            /* Hypervisor opaque data. */
    uint32_t                fHostFeatures;      /* Features provided by the host. */
};
typedef struct VirtioDevice VIRTIODEVICE;
typedef VIRTIODEVICE *PVIRTIODEVICE;


int VirtioAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd, PVIRTIODEVICEOPS pDeviceOps, PVIRTIOHYPEROPS pHyperOps);
int VirtioDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd);

PVIRTIOQUEUE VirtioGetQueue(PVIRTIODEVICE pDevice, uint16_t Index);
void VirtioPutQueue(PVIRTIODEVICE pDevice, PVIRTIOQUEUE pQueue);

void VirtioRingInit(PVIRTIOQUEUE pQueue, uint_t cDescs, caddr_t virtBuf, ulong_t Align);
int  VirtioRingPush(PVIRTIOQUEUE pQueue, paddr_t physBuf, uint32_t cbBuf, uint16_t fFlags);
size_t VirtioRingSize(uint64_t cElements, ulong_t Align);

#endif /* !GA_INCLUDED_SRC_solaris_Virtio_Virtio_solaris_h */

