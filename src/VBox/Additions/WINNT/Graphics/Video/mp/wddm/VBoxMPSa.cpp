/* $Id: VBoxMPSa.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 * Sorted array impl
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#include "common/VBoxMPUtils.h"
#include <iprt/err.h>
#include <iprt/mem.h>

#include <memory.h>

#include "VBoxMPSa.h"

VBOXSADECL(int) CrSaInit(CR_SORTARRAY *pArray, uint32_t cInitBuffer)
{
    pArray->cBufferSize = cInitBuffer;
    pArray->cSize = 0;
    if (cInitBuffer)
    {
        pArray->pElements = (uint64_t*)RTMemAlloc(cInitBuffer * sizeof (pArray->pElements[0]));
        if (!pArray->pElements)
        {
            WARN(("no memory"));
            /* sanity */
            pArray->cBufferSize = 0;
            return VERR_NO_MEMORY;
        }
    }
    else
        pArray->pElements = NULL;

    return VINF_SUCCESS;
}

VBOXSADECL(void) CrSaCleanup(CR_SORTARRAY *pArray)
{
    if (pArray->pElements)
        RTMemFree(pArray->pElements);

    CrSaInit(pArray, 0);
}

static int crSaSearch(const CR_SORTARRAY *pArray, uint64_t element)
{
    int iMin = 0;
    int iMax = pArray->cSize;
    int i = 0;

    while (iMin < iMax)
    {
        i = (iMax + iMin) / 2;

        uint64_t el = pArray->pElements[i];
        if (el == element)
            return i;
        else if (el < element)
            iMin = i + 1;
        else
            iMax = i;
    }

    return -1;
}

static void crSaDbgValidate(const CR_SORTARRAY *pArray)
{
    Assert(pArray->cSize <= pArray->cBufferSize);
    Assert(!pArray->pElements == !pArray->cBufferSize);
    if (!pArray->cSize)
        return;
    uint64_t cur = pArray->pElements[0];
    for (uint32_t i = 1; i < pArray->cSize; ++i)
    {
        Assert(pArray->pElements[i] > cur);
        cur = pArray->pElements[i];
    }
}

#ifdef DEBUG
# define crSaValidate crSaDbgValidate
#else
# define crSaValidate(_a) do {} while (0)
#endif

static int crSaInsAt(CR_SORTARRAY *pArray, uint32_t iPos, uint64_t element)
{
    if (pArray->cSize == pArray->cBufferSize)
    {
        uint32_t cNewBufferSize = pArray->cBufferSize + 16;
        uint64_t *pNew;
        if (pArray->pElements)
            pNew = (uint64_t*)RTMemRealloc(pArray->pElements, cNewBufferSize * sizeof (pArray->pElements[0]));
        else
            pNew = (uint64_t*)RTMemAlloc(cNewBufferSize * sizeof (pArray->pElements[0]));
        if (!pNew)
        {
            WARN(("no memory"));
            return VERR_NO_MEMORY;
        }

        pArray->pElements = pNew;
        pArray->cBufferSize = cNewBufferSize;
        crSaValidate(pArray);
    }

    for (int32_t i = (int32_t)pArray->cSize - 1; i >= (int32_t)iPos; --i)
    {
        pArray->pElements[i+1] = pArray->pElements[i];
    }

    pArray->pElements[iPos] = element;
    ++pArray->cSize;

    crSaValidate(pArray);

    return VINF_SUCCESS;
}

static void crSaDelAt(CR_SORTARRAY *pArray, uint32_t iPos)
{
    Assert(pArray->cSize > iPos);

    for (uint32_t i = iPos; i < pArray->cSize - 1; ++i)
    {
        pArray->pElements[i] = pArray->pElements[i+1];
    }

    --pArray->cSize;
}

static int crSaAdd(CR_SORTARRAY *pArray, uint64_t element)
{
    int iMin = 0;
    int iMax = pArray->cSize;
    int i = 0;
    uint64_t el;

    if (!iMax)
        return crSaInsAt(pArray, 0, element);

    el = element; /* Shup up MSC. */
    while (iMin < iMax)
    {
        i = (iMax + iMin) / 2;

        el = pArray->pElements[i];
        if (el == element)
            return VINF_ALREADY_INITIALIZED;
        else if (el < element)
            iMin = i + 1;
        else
            iMax = i;
    }

    if (el < element)
        return crSaInsAt(pArray, i+1, element);
    return crSaInsAt(pArray, i, element);
}

static int crSaRemove(CR_SORTARRAY *pArray, uint64_t element)
{
    int i = crSaSearch(pArray, element);
    if (i >= 0)
    {
        crSaDelAt(pArray, i);
        return VINF_SUCCESS;
    }
    return VINF_ALREADY_INITIALIZED;
}

/*
 *  * @return true if element is found */
VBOXSADECL(bool) CrSaContains(const CR_SORTARRAY *pArray, uint64_t element)
{
    return crSaSearch(pArray, element) >= 0;
}

VBOXSADECL(int) CrSaAdd(CR_SORTARRAY *pArray, uint64_t element)
{
    return crSaAdd(pArray, element);
}

VBOXSADECL(int) CrSaRemove(CR_SORTARRAY *pArray, uint64_t element)
{
    return crSaRemove(pArray, element);
}

static int crSaIntersected(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2, CR_SORTARRAY *pResult)
{
    int rc = VINF_SUCCESS;
    CrSaClear(pResult);

    for (uint32_t i = 0, j = 0; i < pArray1->cSize && j < pArray2->cSize; )
    {
        if (pArray1->pElements[i] == pArray2->pElements[j])
        {
            rc = CrSaAdd(pResult, pArray1->pElements[i]);
            if (rc < 0)
            {
                WARN(("CrSaAdd failed"));
                return rc;
            }

            ++i;
            ++j;
        }
        else if (pArray1->pElements[i] < pArray2->pElements[j])
        {
            ++i;
        }
        else
        {
            ++j;
        }
    }

    return VINF_SUCCESS;
}

static void crSaIntersect(CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2)
{
    for (uint32_t i = 0, j = 0; i < pArray1->cSize && j < pArray2->cSize; )
    {
        if (pArray1->pElements[i] == pArray2->pElements[j])
        {
            ++i;
            ++j;
        }
        else if (pArray1->pElements[i] < pArray2->pElements[j])
            crSaDelAt(pArray1, i);
        else
            ++j;
    }
}

/*
 * @return >= 0 success
 * < 0 - no memory
 *  */
VBOXSADECL(void) CrSaIntersect(CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2)
{
    crSaIntersect(pArray1, pArray2);
}

VBOXSADECL(int) CrSaIntersected(CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2, CR_SORTARRAY *pResult)
{
    return crSaIntersected(pArray1, pArray2, pResult);
}

static int crSaUnited(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2, CR_SORTARRAY *pResult)
{
    int rc = VINF_SUCCESS;
    CrSaClear(pResult);

    uint32_t i = 0, j = 0;
    uint32_t cResult = 0;
    while (i < pArray1->cSize && j < pArray2->cSize)
    {
        uint64_t element;
        if (pArray1->pElements[i] == pArray2->pElements[j])
        {
            element = pArray1->pElements[i];
            ++i;
            ++j;
        }
        else if (pArray1->pElements[i] < pArray2->pElements[j])
        {
            element = pArray1->pElements[i];
            ++i;
        }
        else
        {
            element = pArray1->pElements[j];
            ++j;
        }

        rc = crSaInsAt(pResult, cResult++, element);
        if (rc < 0)
        {
            WARN(("crSaInsAt failed"));
            return rc;
        }
    }

    uint32_t iTail;
    const CR_SORTARRAY *pTail;

    if (i < pArray1->cSize)
    {
        iTail = i;
        pTail = pArray1;
    }
    else if (j < pArray2->cSize)
    {
        iTail = j;
        pTail = pArray2;
    }
    else
    {
        iTail = 0;
        pTail = 0;
    }

    if (pTail)
    {
        for (;iTail < pTail->cSize; ++iTail)
        {
            rc = crSaInsAt(pResult, cResult++, pTail->pElements[iTail]);
            if (rc < 0)
            {
                WARN(("crSaInsAt failed"));
                return rc;
            }
        }
    }

    return VINF_SUCCESS;
}

VBOXSADECL(int) CrSaUnited(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2, CR_SORTARRAY *pResult)
{
    return crSaUnited(pArray1, pArray2, pResult);
}

static int crSaClone(const CR_SORTARRAY *pArray1, CR_SORTARRAY *pResult)
{
    CrSaClear(pResult);

    if (pArray1->cSize > pResult->cBufferSize)
    {
        CrSaCleanup(pResult);
        uint32_t cNewBufferSize = pArray1->cSize;
        uint64_t *pNew = (uint64_t*)RTMemAlloc(cNewBufferSize * sizeof (pResult->pElements[0]));
        if (!pNew)
        {
            WARN(("no memory"));
            return VERR_NO_MEMORY;
        }

        pResult->pElements = pNew;
        pResult->cBufferSize = cNewBufferSize;
        crSaValidate(pResult);
    }

    pResult->cSize = pArray1->cSize;
    memcpy(pResult->pElements, pArray1->pElements, pArray1->cSize * sizeof (pArray1->pElements[0]));
    return VINF_SUCCESS;
}

/*
 * @return VINF_SUCCESS on success
 * VERR_NO_MEMORY - no memory
 *  */
VBOXSADECL(int) CrSaClone(const CR_SORTARRAY *pArray1, CR_SORTARRAY *pResult)
{
    return crSaClone(pArray1, pResult);
}

static int crSaCmp(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2)
{
    int diff = CrSaGetSize(pArray1) - CrSaGetSize(pArray2);
    if (diff)
        return diff;

    return memcmp(pArray1->pElements, pArray2->pElements, pArray1->cSize * sizeof (pArray1->pElements[0]));
}

VBOXSADECL(int) CrSaCmp(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2)
{
    return crSaCmp(pArray1, pArray2);
}

static bool crSaCovers(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2)
{
    if (CrSaGetSize(pArray1) < CrSaGetSize(pArray2))
        return false;

    uint32_t i = 0, j = 0;
    while (j < pArray2->cSize)
    {
        if (i == pArray1->cSize)
            return false;

        if (pArray1->pElements[i] == pArray2->pElements[j])
        {
            ++i;
            ++j;
        }
        else if (pArray1->pElements[i] < pArray2->pElements[j])
            ++i;
        else
            return false;
    }

    return true;
}

VBOXSADECL(bool) CrSaCovers(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2)
{
    return crSaCovers(pArray1, pArray2);
}

