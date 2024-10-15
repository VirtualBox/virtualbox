/* $Id: VBoxMPSa.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 * Sorted array API
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
#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPSa_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPSa_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assert.h>

typedef struct CR_SORTARRAY
{
    uint32_t cBufferSize;
    uint32_t cSize;
    uint64_t *pElements;
} CR_SORTARRAY;


#ifndef IN_RING0
# define VBOXSADECL(_type) DECLEXPORT(_type)
#else
# define VBOXSADECL(_type) RTDECL(_type)
#endif


DECLINLINE(uint32_t) CrSaGetSize(const CR_SORTARRAY *pArray)
{
    return pArray->cSize;
}

DECLINLINE(uint64_t) CrSaGetVal(const CR_SORTARRAY *pArray, uint32_t i)
{
    Assert(i < pArray->cSize);
    return pArray->pElements[i];
}

DECLINLINE(const uint64_t*) CrSaGetElements(const CR_SORTARRAY *pArray)
{
    return pArray->pElements;
}

DECLINLINE(void) CrSaClear(CR_SORTARRAY *pArray)
{
    pArray->cSize = 0;
}

VBOXSADECL(int) CrSaInit(CR_SORTARRAY *pArray, uint32_t cInitBuffer);
VBOXSADECL(void) CrSaCleanup(CR_SORTARRAY *pArray);
/*
 * @return true if element is found */
VBOXSADECL(bool) CrSaContains(const CR_SORTARRAY *pArray, uint64_t element);

/*
 * @return VINF_SUCCESS  if element is added
 * VINF_ALREADY_INITIALIZED if element was in array already
 * VERR_NO_MEMORY - no memory
 *  */
VBOXSADECL(int) CrSaAdd(CR_SORTARRAY *pArray, uint64_t element);

/*
 * @return VINF_SUCCESS  if element is removed
 * VINF_ALREADY_INITIALIZED if element was NOT in array
 *  */
VBOXSADECL(int) CrSaRemove(CR_SORTARRAY *pArray, uint64_t element);

/*
 * @return VINF_SUCCESS on success
 * VERR_NO_MEMORY - no memory
 *  */
VBOXSADECL(void) CrSaIntersect(CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2);
VBOXSADECL(int) CrSaIntersected(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2, CR_SORTARRAY *pResult);

/*
 * @return VINF_SUCCESS on success
 * VERR_NO_MEMORY - no memory
 *  */
VBOXSADECL(int) CrSaUnited(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2, CR_SORTARRAY *pResult);

/*
 * @return VINF_SUCCESS on success
 * VERR_NO_MEMORY - no memory
 *  */
VBOXSADECL(int) CrSaClone(const CR_SORTARRAY *pArray1, CR_SORTARRAY *pResult);

VBOXSADECL(int) CrSaCmp(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2);

VBOXSADECL(bool) CrSaCovers(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPSa_h */
