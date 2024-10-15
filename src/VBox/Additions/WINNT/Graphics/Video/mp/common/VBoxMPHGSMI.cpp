/* $Id: VBoxMPHGSMI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Miniport HGSMI related functions
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include "VBoxMPHGSMI.h"
#include "VBoxMPCommon.h"
#include <iprt/alloc.h>

static DECLCALLBACK(void *) hgsmiEnvAlloc(void *pvEnv, HGSMISIZE cb)
{
    NOREF(pvEnv);
    return RTMemAlloc(cb);
}

static DECLCALLBACK(void) hgsmiEnvFree(void *pvEnv, void *pv)
{
    NOREF(pvEnv);
    RTMemFree(pv);
}

static HGSMIENV g_hgsmiEnvMP =
{
    NULL,
    hgsmiEnvAlloc,
    hgsmiEnvFree
};

/**
 * Helper function to register secondary displays (DualView).
 *
 * Note that this will not be available on pre-XP versions, and some editions on
 * XP will fail because they are intentionally crippled.
 *
 * HGSMI variant is a bit different because it uses only HGSMI interface (VBVA channel)
 * to talk to the host.
 */
void VBoxSetupDisplaysHGSMI(PVBOXMP_COMMON pCommon, PHYSICAL_ADDRESS phVRAM, uint32_t ulApertureSize,
                            uint32_t cbVRAM, uint32_t fCaps)
{
    /** @todo I simply converted this from Windows error codes.  That is wrong,
     * but we currently freely mix and match those (failure == rc > 0) and iprt
     * ones (failure == rc < 0) anyway.  This needs to be fully reviewed and
     * fixed. */
    LOGF_ENTER();

    memset(pCommon, 0, sizeof(*pCommon));
    pCommon->phVRAM = phVRAM;
    pCommon->ulApertureSize = ulApertureSize;
    pCommon->cbVRAM    = cbVRAM;
    pCommon->cDisplays = 1;
    pCommon->bHGSMI    = VBoxHGSMIIsSupported();

#if 1 /* Style that works for MSC and is easier to read. */

    if (pCommon->bHGSMI)
    {
        uint32_t offVRAMBaseMapping, cbMapping, offGuestHeapMemory, cbGuestHeapMemory, offHostFlags;
        VBoxHGSMIGetBaseMappingInfo(pCommon->cbVRAM, &offVRAMBaseMapping,
                                    &cbMapping, &offGuestHeapMemory,
                                    &cbGuestHeapMemory, &offHostFlags);

        /* Map the adapter information. It will be needed for HGSMI IO. */
        int rc = VBoxMPCmnMapAdapterMemory(pCommon, &pCommon->pvAdapterInformation, offVRAMBaseMapping, cbMapping);
        if (RT_SUCCESS(rc))
        {
            /* Setup an HGSMI heap within the adapter information area. */
            rc = VBoxHGSMISetupGuestContext(&pCommon->guestCtx,
                                            pCommon->pvAdapterInformation,
                                            cbGuestHeapMemory,
                                              offVRAMBaseMapping
                                            + offGuestHeapMemory,
                                            &g_hgsmiEnvMP);
            if (RT_SUCCESS(rc))
            {
                if (pCommon->bHGSMI) /* Paranoia caused by the structure of the original code, probably unnecessary. */
                {
                    /* Setup the host heap and the adapter memory. */
                    uint32_t offVRAMHostArea, cbHostArea;
                    VBoxHGSMIGetHostAreaMapping(&pCommon->guestCtx, pCommon->cbVRAM,
                                                offVRAMBaseMapping, &offVRAMHostArea,
                                                &cbHostArea);
                    if (cbHostArea)
                    {
                        /* Map the heap region.
                         *
                         * Note: the heap will be used for the host buffers submitted to the guest.
                         *       The miniport driver is responsible for reading FIFO and notifying
                         *       display drivers.
                         */
                        pCommon->cbMiniportHeap = cbHostArea;
                        rc = VBoxMPCmnMapAdapterMemory(pCommon, &pCommon->pvMiniportHeap, offVRAMHostArea, cbHostArea);
                        if (RT_SUCCESS(rc))
                        {
                            VBoxHGSMISetupHostContext(&pCommon->hostCtx,
                                                      pCommon->pvAdapterInformation,
                                                      offHostFlags,
                                                      pCommon->pvMiniportHeap,
                                                      offVRAMHostArea, cbHostArea);

                            if (pCommon->bHGSMI) /* Paranoia caused by the structure of the original code, probably unnecessary. */
                            {
                                /* Setup the information for the host. */
                                rc = VBoxHGSMISendHostCtxInfo(&pCommon->guestCtx,
                                                              offVRAMBaseMapping + offHostFlags,
                                                              fCaps,
                                                              offVRAMHostArea,
                                                              pCommon->cbMiniportHeap);
                                if (RT_SUCCESS(rc))
                                {
                                    /* Check whether the guest supports multimonitors. */
                                    if (pCommon->bHGSMI)
                                    {
                                        /* Query the configured number of displays. */
                                        pCommon->cDisplays = VBoxHGSMIGetMonitorCount(&pCommon->guestCtx);
                                        /* Query supported VBVA_SCREEN_F_* flags. */
                                        pCommon->u16SupportedScreenFlags = VBoxHGSMIGetScreenFlags(&pCommon->guestCtx);
                                        LOGF_LEAVE();
                                        return;
                                    }
                                }
                                else
                                    pCommon->bHGSMI = false;
                            }
                        }
                        else
                        {
                            pCommon->pvMiniportHeap = NULL;
                            pCommon->cbMiniportHeap = 0;
                            pCommon->bHGSMI = false;
                        }
                    }
                    else
                    {
                        /* Host has not requested a heap. */
                        pCommon->pvMiniportHeap = NULL;
                        pCommon->cbMiniportHeap = 0;
                    }
                }
            }
            else
            {
                LOG(("HGSMIHeapSetup failed rc = %d", rc));
                pCommon->bHGSMI = false;
            }
        }
        else
        {
            LOG(("VBoxMPCmnMapAdapterMemory failed rc = %d", rc));
            pCommon->bHGSMI = false;
        }
    }

    if (!pCommon->bHGSMI)
        VBoxFreeDisplaysHGSMI(pCommon);


#else /* MSC isn't able to keep track of what's initialized and what's not with this style of code.  Nor
         is it clear whether bHGSMI can be modified by the calls made by the code or just the code itself,
         which makes it hard to figure out!  This makes this coding style hard to maintain. */
    int rc = VINF_SUCCESS;
    uint32_t offVRAMBaseMapping, cbMapping, offGuestHeapMemory, cbGuestHeapMemory,
             offHostFlags, offVRAMHostArea, cbHostArea;

    if (pCommon->bHGSMI)
    {
        VBoxHGSMIGetBaseMappingInfo(pCommon->cbVRAM, &offVRAMBaseMapping,
                                    &cbMapping, &offGuestHeapMemory,
                                    &cbGuestHeapMemory, &offHostFlags);

        /* Map the adapter information. It will be needed for HGSMI IO. */
        rc = VBoxMPCmnMapAdapterMemory(pCommon, &pCommon->pvAdapterInformation, offVRAMBaseMapping, cbMapping);
        if (RT_FAILURE(rc))
        {
            LOG(("VBoxMPCmnMapAdapterMemory failed rc = %d", rc));
            pCommon->bHGSMI = false;
        }
        else
        {
            /* Setup an HGSMI heap within the adapter information area. */
            rc = VBoxHGSMISetupGuestContext(&pCommon->guestCtx,
                                            pCommon->pvAdapterInformation,
                                            cbGuestHeapMemory,
                                              offVRAMBaseMapping
                                            + offGuestHeapMemory,
                                            &g_hgsmiEnvMP);

            if (RT_FAILURE(rc))
            {
                LOG(("HGSMIHeapSetup failed rc = %d", rc));
                pCommon->bHGSMI = false;
            }
        }
    }

    /* Setup the host heap and the adapter memory. */
    if (pCommon->bHGSMI)
    {
        VBoxHGSMIGetHostAreaMapping(&pCommon->guestCtx, pCommon->cbVRAM,
                                    offVRAMBaseMapping, &offVRAMHostArea,
                                    &cbHostArea);
        if (cbHostArea)
        {

            /* Map the heap region.
             *
             * Note: the heap will be used for the host buffers submitted to the guest.
             *       The miniport driver is responsible for reading FIFO and notifying
             *       display drivers.
             */
            pCommon->cbMiniportHeap = cbHostArea;
            rc = VBoxMPCmnMapAdapterMemory (pCommon, &pCommon->pvMiniportHeap,
                                       offVRAMHostArea, cbHostArea);
            if (RT_FAILURE(rc))
            {
                pCommon->pvMiniportHeap = NULL;
                pCommon->cbMiniportHeap = 0;
                pCommon->bHGSMI = false;
            }
            else
                VBoxHGSMISetupHostContext(&pCommon->hostCtx,
                                          pCommon->pvAdapterInformation,
                                          offHostFlags,
                                          pCommon->pvMiniportHeap,
                                          offVRAMHostArea, cbHostArea);
        }
        else
        {
            /* Host has not requested a heap. */
            pCommon->pvMiniportHeap = NULL;
            pCommon->cbMiniportHeap = 0;
        }
    }

    if (pCommon->bHGSMI)
    {
        /* Setup the information for the host. */
        rc = VBoxHGSMISendHostCtxInfo(&pCommon->guestCtx,
                                      offVRAMBaseMapping + offHostFlags,
                                      fCaps,
                                      offVRAMHostArea,
                                      pCommon->cbMiniportHeap);

        if (RT_FAILURE(rc))
        {
            pCommon->bHGSMI = false;
        }
    }

    /* Check whether the guest supports multimonitors. */
    if (pCommon->bHGSMI)
    {
        /* Query the configured number of displays. */
        pCommon->cDisplays = VBoxHGSMIGetMonitorCount(&pCommon->guestCtx);
    }
    else
    {
        VBoxFreeDisplaysHGSMI(pCommon);
    }
#endif

    LOGF_LEAVE();
}

static bool VBoxUnmapAdpInfoCallback(void *pvCommon)
{
    PVBOXMP_COMMON pCommon = (PVBOXMP_COMMON)pvCommon;

    pCommon->hostCtx.pfHostFlags = NULL;
    return true;
}

void VBoxFreeDisplaysHGSMI(PVBOXMP_COMMON pCommon)
{
    VBoxMPCmnUnmapAdapterMemory(pCommon, &pCommon->pvMiniportHeap);
#ifdef VBOX_WDDM_MINIPORT
    VBoxSHGSMITerm(&pCommon->guestCtx.heapCtx);
#else
    HGSMIHeapDestroy(&pCommon->guestCtx.heapCtx);
#endif

    /* Unmap the adapter information needed for HGSMI IO. */
    VBoxMPCmnSyncToVideoIRQ(pCommon, VBoxUnmapAdpInfoCallback, pCommon);
    VBoxMPCmnUnmapAdapterMemory(pCommon, &pCommon->pvAdapterInformation);
}

