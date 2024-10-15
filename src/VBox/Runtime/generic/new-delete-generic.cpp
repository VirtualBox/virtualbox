/* $Id: new-delete-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, C++ electric fence.
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
#include <iprt/nocrt/new>

#include <iprt/assert.h>
#include <iprt/mem.h>



void *RT_NEW_DELETE_CDECL operator new(RT_NEW_DELETE_SIZE_T cb) RT_NEW_DELETE_THROWS_BAD_ALLOC
{
    void *pv = RTMemAlloc(cb);
#ifdef RT_EXCEPTIONS_ENABLED
    if (!pv)
        throw std::bad_alloc();
#endif
    return pv;
}


void *RT_NEW_DELETE_CDECL operator new(RT_NEW_DELETE_SIZE_T cb, const std::nothrow_t &) RT_NEW_DELETE_NOTHROW
{
    return RTMemAlloc(cb);
}


void *RT_NEW_DELETE_CDECL operator new(RT_NEW_DELETE_SIZE_T cb, void *pvPlacement) RT_NEW_DELETE_NOTHROW
{
    RT_NOREF(cb);
    return pvPlacement;
}


void RT_NEW_DELETE_CDECL operator delete(void *pv) RT_NEW_DELETE_NOTHROW
{
    RTMemFree(pv);
}


#ifdef __cpp_sized_deallocation
void RT_NEW_DELETE_CDECL operator delete(void *pv, RT_NEW_DELETE_SIZE_T cb) RT_NEW_DELETE_NOTHROW
{
    RT_NOREF_PV(cb);
    RTMemFree(pv);
}
#endif


void RT_NEW_DELETE_CDECL operator delete(void *pv, const std::nothrow_t &) RT_NEW_DELETE_NOTHROW
{
    RTMemFree(pv);
}


/*
 *
 * Array object form.
 * Array object form.
 * Array object form.
 *
 */

void *RT_NEW_DELETE_CDECL operator new[](RT_NEW_DELETE_SIZE_T cb) RT_NEW_DELETE_THROWS_BAD_ALLOC
{
    void *pv = RTMemAlloc(cb);
#ifdef RT_EXCEPTIONS_ENABLED
    if (!pv)
        throw std::bad_alloc();
#endif
    return pv;
}


void *RT_NEW_DELETE_CDECL operator new[](RT_NEW_DELETE_SIZE_T cb, const std::nothrow_t &) RT_NEW_DELETE_NOTHROW
{
    return RTMemAlloc(cb);
}


void *RT_NEW_DELETE_CDECL operator new[](RT_NEW_DELETE_SIZE_T cb, void *pvPlacement) RT_NEW_DELETE_NOTHROW
{
    RT_NOREF_PV(cb);
    return pvPlacement;
}


void RT_NEW_DELETE_CDECL operator delete[](void * pv) RT_NEW_DELETE_NOTHROW
{
    RTMemFree(pv);
}


#ifdef __cpp_sized_deallocation
void RT_NEW_DELETE_CDECL operator delete[](void * pv, RT_NEW_DELETE_SIZE_T cb) RT_NEW_DELETE_NOTHROW
{
    RT_NOREF_PV(cb);
    RTMemFree(pv);
}
#endif


void RT_NEW_DELETE_CDECL operator delete[](void *pv, const std::nothrow_t &) RT_NEW_DELETE_NOTHROW
{
    RTMemFree(pv);
}

