/* $Id: alloc-ef-cpp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include "alloc-ef.h"

#include <iprt/asm.h>
#include <new>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @todo test this on MSC */

/** MSC declares the operators as cdecl it seems. */
#ifdef _MSC_VER
# define RT_EF_CDECL    __cdecl
#else
# define RT_EF_CDECL
#endif

/** MSC doesn't use the standard namespace. */
#ifdef _MSC_VER
# define RT_EF_SIZE_T   size_t
#else
# define RT_EF_SIZE_T   std::size_t
#endif

/** The hint that we're throwing std::bad_alloc is not apprecitated by MSC. */
#ifdef RT_EXCEPTIONS_ENABLED
# ifdef _MSC_VER
#  define RT_EF_THROWS_BAD_ALLOC
#  define RT_EF_NOTHROW               RT_NO_THROW_DEF
# else
#  ifdef _GLIBCXX_THROW
#   define RT_EF_THROWS_BAD_ALLOC     _GLIBCXX_THROW(std::bad_alloc)
#  elif defined(__cplusplus) && (__cplusplus + 0) < 201700
#   define RT_EF_THROWS_BAD_ALLOC     throw(std::bad_alloc)
#  else
#   define RT_EF_THROWS_BAD_ALLOC     noexcept(false)
#  endif
#  define RT_EF_NOTHROW               throw()
# endif
#else  /* !RT_EXCEPTIONS_ENABLED */
# define RT_EF_THROWS_BAD_ALLOC
# define RT_EF_NOTHROW
#endif /* !RT_EXCEPTIONS_ENABLED */


void *RT_EF_CDECL operator new(RT_EF_SIZE_T cb) RT_EF_THROWS_BAD_ALLOC
{
    void *pv = rtR3MemAlloc("new", RTMEMTYPE_NEW, cb, cb, NULL, ASMReturnAddress(), NULL, 0, NULL);
    if (!pv)
        throw std::bad_alloc();
    return pv;
}


void *RT_EF_CDECL operator new(RT_EF_SIZE_T cb, const std::nothrow_t &) RT_EF_NOTHROW
{
    void *pv = rtR3MemAlloc("new nothrow", RTMEMTYPE_NEW, cb, cb, NULL, ASMReturnAddress(), NULL, 0, NULL);
    return pv;
}


void RT_EF_CDECL operator delete(void *pv) RT_EF_NOTHROW
{
    rtR3MemFree("delete", RTMEMTYPE_DELETE, pv, 0, ASMReturnAddress(), NULL, 0, NULL);
}


#ifdef __cpp_sized_deallocation
void RT_EF_CDECL operator delete(void *pv, RT_EF_SIZE_T cb) RT_EF_NOTHROW
{
    NOREF(cb);
    AssertMsgFailed(("cb ignored!\n"));
    rtR3MemFree("delete", RTMEMTYPE_DELETE, pv, 0, ASMReturnAddress(), NULL, 0, NULL);
}
#endif


void RT_EF_CDECL operator delete(void * pv, const std::nothrow_t &) RT_EF_NOTHROW
{
    rtR3MemFree("delete nothrow", RTMEMTYPE_DELETE, pv, 0, ASMReturnAddress(), NULL, 0, NULL);
}


/*
 *
 * Array object form.
 * Array object form.
 * Array object form.
 *
 */

void *RT_EF_CDECL operator new[](RT_EF_SIZE_T cb) RT_EF_THROWS_BAD_ALLOC
{
    void *pv = rtR3MemAlloc("new[]", RTMEMTYPE_NEW_ARRAY, cb, cb, NULL, ASMReturnAddress(), NULL, 0, NULL);
    if (!pv)
        throw std::bad_alloc();
    return pv;
}


void * RT_EF_CDECL operator new[](RT_EF_SIZE_T cb, const std::nothrow_t &) RT_EF_NOTHROW
{
    void *pv = rtR3MemAlloc("new[] nothrow", RTMEMTYPE_NEW_ARRAY, cb, cb, NULL, ASMReturnAddress(), NULL, 0, NULL);
    return pv;
}


void RT_EF_CDECL operator delete[](void * pv) RT_EF_NOTHROW
{
    rtR3MemFree("delete[]", RTMEMTYPE_DELETE_ARRAY, pv, 0, ASMReturnAddress(), NULL, 0, NULL);
}


#ifdef __cpp_sized_deallocation
void RT_EF_CDECL operator delete[](void * pv, RT_EF_SIZE_T cb) RT_EF_NOTHROW
{
    NOREF(cb);
    AssertMsgFailed(("cb ignored!\n"));
    rtR3MemFree("delete[]", RTMEMTYPE_DELETE_ARRAY, pv, 0, ASMReturnAddress(), NULL, 0, NULL);
}
#endif


void RT_EF_CDECL operator delete[](void *pv, const std::nothrow_t &) RT_EF_NOTHROW
{
    rtR3MemFree("delete[] nothrow", RTMEMTYPE_DELETE_ARRAY, pv, 0, ASMReturnAddress(), NULL, 0, NULL);
}

