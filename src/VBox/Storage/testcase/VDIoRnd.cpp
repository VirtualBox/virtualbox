/* $Id: VDIoRnd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox HDD container test utility - I/O data generator.
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

#define LOGGROUP LOGGROUP_DEFAULT
#include <iprt/log.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/assert.h>

#include "VDIoRnd.h"

/**
 * I/O random data generator instance data.
 */
typedef struct VDIORND
{
    /** Pointer to the buffer holding the random data. */
    uint8_t *pbPattern;
    /** Size of the buffer. */
    size_t   cbPattern;
    /** RNG */
    RTRAND   hRand;
} VDIORND;

int VDIoRndCreate(PPVDIORND ppIoRnd, size_t cbPattern, uint64_t uSeed)
{
    PVDIORND pIoRnd = NULL;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(ppIoRnd, VERR_INVALID_POINTER);

    pIoRnd = (PVDIORND)RTMemAllocZ(sizeof(VDIORND));
    if (pIoRnd)
        pIoRnd->pbPattern = (uint8_t *)RTMemPageAllocZ(cbPattern);

    if (   pIoRnd
        && pIoRnd->pbPattern)
    {
        pIoRnd->cbPattern = cbPattern;

        rc = RTRandAdvCreateParkMiller(&pIoRnd->hRand);
        if (RT_SUCCESS(rc))
        {
            RTRandAdvSeed(pIoRnd->hRand, uSeed);
            RTRandAdvBytes(pIoRnd->hRand, pIoRnd->pbPattern, cbPattern);
        }
        else
        {
            RTMemPageFree(pIoRnd->pbPattern, cbPattern);
            RTMemFree(pIoRnd);
        }
    }
    else
    {
        if (pIoRnd)
            RTMemFree(pIoRnd);
        rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
        *ppIoRnd = pIoRnd;

    return rc;
}

void VDIoRndDestroy(PVDIORND pIoRnd)
{
    AssertPtrReturnVoid(pIoRnd);

    RTRandAdvDestroy(pIoRnd->hRand);
    RTMemPageFree(pIoRnd->pbPattern, pIoRnd->cbPattern);
    RTMemFree(pIoRnd);
}

int VDIoRndGetBuffer(PVDIORND pIoRnd, void **ppv, size_t cb)
{
    AssertPtrReturn(pIoRnd, VERR_INVALID_POINTER);
    AssertPtrReturn(ppv, VERR_INVALID_POINTER);
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);

    if (cb > pIoRnd->cbPattern - 512)
        return VERR_INVALID_PARAMETER;

    *ppv = pIoRnd->pbPattern + RT_ALIGN_64(RTRandAdvU64Ex(pIoRnd->hRand, 0, pIoRnd->cbPattern - cb - 512), 512);
    return VINF_SUCCESS;
}


uint32_t VDIoRndGetU32Ex(PVDIORND pIoRnd, uint32_t uMin, uint32_t uMax)
{
    return RTRandAdvU32Ex(pIoRnd->hRand, uMin, uMax);
}

