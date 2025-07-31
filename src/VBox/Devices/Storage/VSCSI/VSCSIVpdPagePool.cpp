/* $Id: VSCSIVpdPagePool.cpp 110132 2025-07-07 11:33:52Z michal.necasek@oracle.com $ */
/** @file
 * Virtual SCSI driver: VPD page pool
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_VSCSI
#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/mem.h>
#include <iprt/assert.h>

#include "VSCSIInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * A VSCSI VPD page.
 */
typedef struct VSCSIVPDPAGE
{
    /** List node. */
    RTLISTNODE    NodePages;
    /** Page size. */
    size_t        cbPage;
    /** Page data - variable size. */
    uint8_t       abPage[1];
} VSCSIVPDPAGE;
/** Pointer to a VPD page. */
typedef VSCSIVPDPAGE *PVSCSIVPDPAGE;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

int vscsiVpdPagePoolInit(PVSCSIVPDPOOL pVScsiVpdPool)
{
    RTListInit(&pVScsiVpdPool->ListPages);
    return VINF_SUCCESS;
}


void vscsiVpdPagePoolDestroy(PVSCSIVPDPOOL pVScsiVpdPool)
{
    PVSCSIVPDPAGE pIt, pItNext;

    RTListForEachSafe(&pVScsiVpdPool->ListPages, pIt, pItNext, VSCSIVPDPAGE, NodePages)
    {
        RTListNodeRemove(&pIt->NodePages);
        RTMemFree(pIt);
    }
}


int vscsiVpdPagePoolAllocNewPage(PVSCSIVPDPOOL pVScsiVpdPool, uint8_t uPage, size_t cbPage, uint8_t **ppbPage)
{
    int rc = VINF_SUCCESS;
    PVSCSIVPDPAGE pPage;

    /* Check that the page doesn't exist already. */
    RTListForEach(&pVScsiVpdPool->ListPages, pPage, VSCSIVPDPAGE, NodePages)
    {
        if (pPage->abPage[1] == uPage)
            return VERR_ALREADY_EXISTS;
    }

    pPage = (PVSCSIVPDPAGE)RTMemAllocZ(RT_UOFFSETOF_DYN(VSCSIVPDPAGE, abPage[cbPage]));
    if (pPage)
    {
        pPage->cbPage    = cbPage;
        pPage->abPage[1] = uPage;
        RTListAppend(&pVScsiVpdPool->ListPages, &pPage->NodePages);
        *ppbPage = &pPage->abPage[0];
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


int vscsiVpdPagePoolQueryPage(PVSCSIVPDPOOL pVScsiVpdPool, PVSCSIREQINT pVScsiReq, uint8_t uPage)
{
    PVSCSIVPDPAGE pPage;

    /* Check that the page doesn't exist already. */
    RTListForEach(&pVScsiVpdPool->ListPages, pPage, VSCSIVPDPAGE, NodePages)
    {
        if (pPage->abPage[1] == uPage)
        {
            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
            vscsiReqSetXferSize(pVScsiReq, pPage->cbPage);
            RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, &pPage->abPage[0], pPage->cbPage);
            return VINF_SUCCESS;
        }
    }

    return VERR_NOT_FOUND;
}

