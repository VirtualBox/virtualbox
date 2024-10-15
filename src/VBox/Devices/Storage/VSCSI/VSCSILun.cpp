/* $Id: VSCSILun.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual SCSI driver: LUN handling
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
#define LOG_GROUP LOG_GROUP_VSCSI
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/types.h>
#include <VBox/vscsi.h>
#include <iprt/assert.h>
#include <iprt/mem.h>

#include "VSCSIInternal.h"

/** SBC descriptor */
extern VSCSILUNDESC g_VScsiLunTypeSbc;
/** MMC descriptor */
extern VSCSILUNDESC g_VScsiLunTypeMmc;
/** SSC descriptor */
extern VSCSILUNDESC g_VScsiLunTypeSsc;

/**
 * Array of supported SCSI LUN types.
 */
static PVSCSILUNDESC g_aVScsiLunTypesSupported[] =
{
    &g_VScsiLunTypeSbc,
    &g_VScsiLunTypeMmc,
#ifdef VBOX_WITH_VSCSI_SSC
    &g_VScsiLunTypeSsc,
#endif
};

VBOXDDU_DECL(int) VSCSILunCreate(PVSCSILUN phVScsiLun, VSCSILUNTYPE enmLunType,
                                 PVSCSILUNIOCALLBACKS pVScsiLunIoCallbacks,
                                 void *pvVScsiLunUser)
{
    PVSCSILUNINT  pVScsiLun     = NULL;
    PVSCSILUNDESC pVScsiLunDesc = NULL;

    AssertPtrReturn(phVScsiLun, VERR_INVALID_POINTER);
    AssertReturn(   enmLunType > VSCSILUNTYPE_INVALID
                 && enmLunType < VSCSILUNTYPE_LAST, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pVScsiLunIoCallbacks, VERR_INVALID_PARAMETER);

    for (unsigned idxLunType = 0; idxLunType < RT_ELEMENTS(g_aVScsiLunTypesSupported); idxLunType++)
    {
        if (g_aVScsiLunTypesSupported[idxLunType]->enmLunType == enmLunType)
        {
            pVScsiLunDesc = g_aVScsiLunTypesSupported[idxLunType];
            break;
        }
    }

    if (!pVScsiLunDesc)
        return VERR_VSCSI_LUN_TYPE_NOT_SUPPORTED;

    pVScsiLun = (PVSCSILUNINT)RTMemAllocZ(pVScsiLunDesc->cbLun);
    if (!pVScsiLun)
        return VERR_NO_MEMORY;

    pVScsiLun->pVScsiDevice         = NULL;
    pVScsiLun->pvVScsiLunUser       = pvVScsiLunUser;
    pVScsiLun->pVScsiLunIoCallbacks = pVScsiLunIoCallbacks;
    pVScsiLun->pVScsiLunDesc        = pVScsiLunDesc;

    int rc = vscsiIoReqInit(pVScsiLun);
    if (RT_SUCCESS(rc))
    {
        rc = vscsiLunGetFeatureFlags(pVScsiLun, &pVScsiLun->fFeatures);
        if (RT_SUCCESS(rc))
        {
            rc = pVScsiLunDesc->pfnVScsiLunInit(pVScsiLun);
            if (RT_SUCCESS(rc))
            {
                *phVScsiLun = pVScsiLun;
                return VINF_SUCCESS;
            }
        }
    }

    RTMemFree(pVScsiLun);

    return rc;
}

/**
 * Destroy virtual SCSI LUN.
 *
 * @returns VBox status code.
 * @param   hVScsiLun               The virtual SCSI LUN handle to destroy.
 */
VBOXDDU_DECL(int) VSCSILunDestroy(VSCSILUN hVScsiLun)
{
    PVSCSILUNINT pVScsiLun = (PVSCSILUNINT)hVScsiLun;

    AssertPtrReturn(pVScsiLun, VERR_INVALID_HANDLE);
    AssertReturn(!pVScsiLun->pVScsiDevice, VERR_VSCSI_LUN_ATTACHED_TO_DEVICE);
    AssertReturn(vscsiIoReqOutstandingCountGet(pVScsiLun) == 0, VERR_VSCSI_LUN_BUSY);

    int rc = pVScsiLun->pVScsiLunDesc->pfnVScsiLunDestroy(pVScsiLun);
    if (RT_FAILURE(rc))
        return rc;

    /* Make LUN invalid */
    pVScsiLun->pvVScsiLunUser       = NULL;
    pVScsiLun->pVScsiLunIoCallbacks = NULL;
    pVScsiLun->pVScsiLunDesc        = NULL;

    RTMemFree(pVScsiLun);

    return VINF_SUCCESS;
}

/**
 * Notify virtual SCSI LUN of media being mounted.
 *
 * @returns VBox status code.
 * @param   hVScsiLun               The virtual SCSI LUN
 *                                  mounting the medium.
 */
VBOXDDU_DECL(int) VSCSILunMountNotify(VSCSILUN hVScsiLun)
{
    int rc = VINF_SUCCESS;
    PVSCSILUNINT pVScsiLun = (PVSCSILUNINT)hVScsiLun;

    LogFlowFunc(("hVScsiLun=%p\n", hVScsiLun));
    AssertPtrReturn(pVScsiLun, VERR_INVALID_HANDLE);
    AssertReturn(vscsiIoReqOutstandingCountGet(pVScsiLun) == 0, VERR_VSCSI_LUN_BUSY);

    /* Mark the LUN as not ready so that LUN specific code can do its job. */
    pVScsiLun->fReady        = false;
    pVScsiLun->fMediaPresent = true;
    if (pVScsiLun->pVScsiLunDesc->pfnVScsiLunMediumInserted)
        rc = pVScsiLun->pVScsiLunDesc->pfnVScsiLunMediumInserted(pVScsiLun);

    return rc;
}

/**
 * Notify virtual SCSI LUN of media being unmounted.
 *
 * @returns VBox status code.
 * @param   hVScsiLun               The virtual SCSI LUN
 *                                  mounting the medium.
 */
VBOXDDU_DECL(int) VSCSILunUnmountNotify(VSCSILUN hVScsiLun)
{
    int rc = VINF_SUCCESS;
    PVSCSILUNINT pVScsiLun = (PVSCSILUNINT)hVScsiLun;

    LogFlowFunc(("hVScsiLun=%p\n", hVScsiLun));
    AssertPtrReturn(pVScsiLun, VERR_INVALID_HANDLE);
    AssertReturn(vscsiIoReqOutstandingCountGet(pVScsiLun) == 0, VERR_VSCSI_LUN_BUSY);

    pVScsiLun->fReady        = false;
    pVScsiLun->fMediaPresent = false;
    if (pVScsiLun->pVScsiLunDesc->pfnVScsiLunMediumRemoved)
        rc = pVScsiLun->pVScsiLunDesc->pfnVScsiLunMediumRemoved(pVScsiLun);

    return rc;
}
