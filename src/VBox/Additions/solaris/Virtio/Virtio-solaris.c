/* $Id: Virtio-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Guest Additions - Virtio Driver for Solaris.
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
#include "Virtio-solaris.h"

#include <iprt/assert.h>
#include <iprt/mem.h>
#include <VBox/log.h>


/**
 * Virtio Attach routine that should be called from all Virtio drivers' attach
 * routines.
 *
 * @param pDip              The module structure instance.
 * @param enmCmd            Operation type (attach/resume).
 * @param pDeviceOps        Pointer to device ops structure.
 * @param pHyperOps         Pointer to hypervisor ops structure.
 *
 * @return Solaris DDI error code. DDI_SUCCESS or DDI_FAILURE.
 */
int VirtioAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd, PVIRTIODEVICEOPS pDeviceOps, PVIRTIOHYPEROPS pHyperOps)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioAttach: pDip=%p enmCmd=%d pDeviceOps=%p pHyperOps=%p\n", pDip, enmCmd, pDeviceOps, pHyperOps));

    AssertReturn(pDip, DDI_EINVAL);
    AssertReturn(pDeviceOps, DDI_EINVAL);
    AssertReturn(pHyperOps, DDI_EINVAL);

    if (enmCmd != DDI_ATTACH)
    {
        LogRel((VIRTIOLOGNAME ":VirtioAttach: Invalid enmCmd=%#x expected DDI_ATTACH\n", enmCmd));
        return DDI_FAILURE;
    }

    int rc = DDI_FAILURE;
    PVIRTIODEVICE pDevice = RTMemAllocZ(sizeof(VIRTIODEVICE));
    if (RT_LIKELY(pDevice))
    {
        pDevice->pDip = pDip;
        pDevice->pDeviceOps = pDeviceOps;
        pDevice->pHyperOps = pHyperOps;

        pDevice->pvDevice = pDevice->pDeviceOps->pfnAlloc(pDevice);
        if (RT_LIKELY(pDevice->pvDevice))
        {
            pDevice->pvHyper = pDevice->pHyperOps->pfnAlloc(pDevice);
            if (RT_LIKELY(pDevice->pvHyper))
            {
                /*
                 * Attach hypervisor interface and obtain features supported by host.
                 */
                rc = pDevice->pHyperOps->pfnAttach(pDevice);
                if (rc == DDI_SUCCESS)
                {
                    pDevice->fHostFeatures = pDevice->pHyperOps->pfnGetFeatures(pDevice);
                    LogFlow((VIRTIOLOGNAME ":VirtioAttach: Host features=%#x\n", pDevice->fHostFeatures));

                    /*
                     * Attach the device type interface.
                     */
                    rc = pDevice->pDeviceOps->pfnAttach(pDevice);
                    if (rc == DDI_SUCCESS)
                    {
                        ddi_set_driver_private(pDip, pDevice);
                        return DDI_SUCCESS;
                    }
                    else
                        LogRel((VIRTIOLOGNAME ":VirtioAttach: DeviceOps pfnAttach failed. rc=%d\n", rc));

                    pDevice->pHyperOps->pfnDetach(pDevice);
                }
                else
                    LogRel((VIRTIOLOGNAME ":VirtioAttach: HyperOps pfnAttach failed. rc=%d\n", rc));

                pDevice->pHyperOps->pfnFree(pDevice);
            }
            else
                LogRel((VIRTIOLOGNAME ":VirtioAttach: HyperOps->pfnAlloc failed!\n"));

            pDevice->pDeviceOps->pfnFree(pDevice);
        }
        else
            LogRel((VIRTIOLOGNAME ":VirtioAttach: DeviceOps->pfnAlloc failed!\n"));

        RTMemFree(pDevice);
    }
    else
        LogRel((VIRTIOLOGNAME ":VirtioAttach: failed to alloc %u bytes for device structure.\n", sizeof(VIRTIODEVICE)));

    return DDI_FAILURE;
}


/**
 * Virtio Detach routine that should be called from all Virtio drivers' detach
 * routines.
 *
 * @param pDip              The module structure instance.
 * @param enmCmd            Operation type (detach/suspend).
 *
 * @return Solaris DDI error code. DDI_SUCCESS or DDI_FAILURE.
 */
int VirtioDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    LogFlowFunc((VIRTIOLOGNAME ":VirtioDetach pDip=%p enmCmd=%d\n", pDip, enmCmd));

    PVIRTIODEVICE pDevice = ddi_get_driver_private(pDip);
    if (RT_UNLIKELY(!pDevice))
        return DDI_FAILURE;

    if (enmCmd != DDI_DETACH)
    {
        LogRel((VIRTIOLOGNAME ":VirtioDetach: Invalid enmCmd=%#x expected DDI_DETACH.\n", enmCmd));
        return DDI_FAILURE;
    }

    int rc = pDevice->pDeviceOps->pfnDetach(pDevice);
    if (rc == DDI_SUCCESS)
    {
        pDevice->pHyperOps->pfnDetach(pDevice);
        pDevice->pDeviceOps->pfnFree(pDevice);
        pDevice->pvDevice = NULL;
        pDevice->pHyperOps->pfnFree(pDevice);
        pDevice->pvHyper = NULL;

        ddi_set_driver_private(pDevice->pDip, NULL);
        RTMemFree(pDevice);
        return DDI_SUCCESS;
    }
    else
        LogRel((VIRTIOLOGNAME ":VirtioDetach: DeviceOps pfnDetach failed. rc=%d\n", rc));

    return DDI_FAILURE;
}


/**
 * Allocates a Virtio Queue object and assigns it an index.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 * @param Index             Queue index.
 *
 * @return A pointer to a Virtio Queue instance.
 */
PVIRTIOQUEUE VirtioGetQueue(PVIRTIODEVICE pDevice, uint16_t Index)
{
    PVIRTIOQUEUE pQueue = RTMemAllocZ(sizeof(VIRTIOQUEUE));
    if (RT_UNLIKELY(!pQueue))
    {
        LogRel((VIRTIOLOGNAME ":VirtioGetQueue: failed to alloc memory for %u bytes.\n", sizeof(VIRTIOQUEUE)));
        return NULL;
    }

    pQueue->QueueIndex = Index;
    pQueue->pvData     = pDevice->pHyperOps->pfnGetQueue(pDevice, pQueue);
    if (RT_UNLIKELY(!pQueue->pvData))
    {
        LogRel((VIRTIOLOGNAME ":VirtioGetQueue: HyperOps GetQueue failed!\n"));
        RTMemFree(pQueue);
        return NULL;
    }

    AssertReturn(pQueue->pQueue, NULL);
    AssertReturn(pQueue->Ring.cDesc > 0, NULL);

    /** @todo enable interrupt. */

    return pQueue;
}


/**
 * Puts a queue and destroys the instance.
 *
 * @param pDevice           Pointer to the Virtio device instance.
 * @param pQueue            Pointer to the Virtio queue.
 */
void VirtioPutQueue(PVIRTIODEVICE pDevice, PVIRTIOQUEUE pQueue)
{
    AssertReturnVoid(pDevice);
    AssertReturnVoid(pQueue);

    pDevice->pHyperOps->pfnPutQueue(pDevice, pQueue);
    RTMemFree(pQueue);
}

