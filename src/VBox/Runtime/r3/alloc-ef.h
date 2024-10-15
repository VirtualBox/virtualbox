/* $Id: alloc-ef.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, electric fence.
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

#ifndef IPRT_INCLUDED_SRC_r3_alloc_ef_h
#define IPRT_INCLUDED_SRC_r3_alloc_ef_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#if defined(DOXYGEN_RUNNING)
# define RTALLOC_USE_EFENCE
# define RTALLOC_EFENCE_IN_FRONT
# define RTALLOC_EFENCE_FREE_FILL 'f'
#endif

/** @def RTALLOC_USE_EFENCE
 * If defined the electric fence put up for ALL allocations by RTMemAlloc(),
 * RTMemAllocZ(), RTMemRealloc(), RTMemTmpAlloc() and RTMemTmpAllocZ().
 */
#if 0
# define RTALLOC_USE_EFENCE
#endif

/** @def RTALLOC_EFENCE_SIZE_FACTOR
 * The size of the fence as a multiple of the system page size.
 */
#define RTALLOC_EFENCE_SIZE_FACTOR      1

/** @def RTALLOC_EFENCE_ALIGNMENT
 * The allocation alignment, power of two of course.
 *
 * Use this for working around misaligned sizes, usually stemming from
 * allocating a string or something after the main structure.  When you
 * encounter this, please fix the allocation to RTMemAllocVar or RTMemAllocZVar.
 */
#if 0
# define RTALLOC_EFENCE_ALIGNMENT       (ARCH_BITS / 8)
#else
# define RTALLOC_EFENCE_ALIGNMENT       1
#endif

/** @def RTALLOC_EFENCE_IN_FRONT
 * Define this to put the fence up in front of the block.
 * The default (when this isn't defined) is to up it up after the block.
 */
//# define RTALLOC_EFENCE_IN_FRONT

/** @def RTALLOC_EFENCE_TRACE
 * Define this to support actual free and reallocation of blocks.
 */
#define RTALLOC_EFENCE_TRACE

/** @def RTALLOC_EFENCE_FREE_DELAYED
 * This define will enable free() delay and protection of the freed data
 * while it's being delayed. The value of RTALLOC_EFENCE_FREE_DELAYED defines
 * the threshold of the delayed blocks.
 * Delayed blocks does not consume any physical memory, only virtual address space.
 * Requires RTALLOC_EFENCE_TRACE.
 */
#define RTALLOC_EFENCE_FREE_DELAYED     (20 * _1M)

/** @def RTALLOC_EFENCE_FREE_FILL
 * This define will enable memset(,RTALLOC_EFENCE_FREE_FILL,)'ing the user memory
 * in the block before freeing/decommitting it. This is useful in GDB since GDB
 * appears to be able to read the content of the page even after it's been
 * decommitted.
 * Requires RTALLOC_EFENCE_TRACE.
 */
#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS) || defined(DOXYGEN_RUNNING)
# define RTALLOC_EFENCE_FREE_FILL       'f'
#endif

/** @def RTALLOC_EFENCE_FILLER
 * This define will enable memset(,RTALLOC_EFENCE_FILLER,)'ing the allocated
 * memory when the API doesn't require it to be zero'd.
 */
#define RTALLOC_EFENCE_FILLER           0xef

/** @def RTALLOC_EFENCE_NOMAN_FILLER
 * This define will enable memset(,RTALLOC_EFENCE_NOMAN_FILLER,)'ing the
 * unprotected but not allocated area of memory, the so called no man's land.
 */
#define RTALLOC_EFENCE_NOMAN_FILLER     0xaa

/** @def RTALLOC_EFENCE_FENCE_FILLER
 * This define will enable memset(,RTALLOC_EFENCE_FENCE_FILLER,)'ing the
 * fence itself, as debuggers can usually read them.
 */
#define RTALLOC_EFENCE_FENCE_FILLER     0xcc

#if defined(DOXYGEN_RUNNING)
/** @def RTALLOC_EFENCE_CPP
 * This define will enable the new and delete wrappers.
 */
# define RTALLOC_EFENCE_CPP
#endif

#if defined(RUNNING_DOXYGEN)
/** @def RTALLOC_REPLACE_MALLOC
 * Replaces the malloc, calloc, realloc, free and friends in libc (experimental).
 * Set in LocalConfig.kmk. Requires RTALLOC_EFENCE_TRACE to work. */
# define RTALLOC_REPLACE_MALLOC
#endif
#if defined(RTALLOC_REPLACE_MALLOC) && !defined(RTALLOC_EFENCE_TRACE)
# error "RTALLOC_REPLACE_MALLOC requires RTALLOC_EFENCE_TRACE."
#endif


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#else
# include <sys/mman.h>
#endif
#include <iprt/avl.h>
#include <iprt/thread.h>


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Allocation types.
 */
typedef enum RTMEMTYPE
{
    RTMEMTYPE_RTMEMALLOC,
    RTMEMTYPE_RTMEMALLOCZ,
    RTMEMTYPE_RTMEMREALLOC,
    RTMEMTYPE_RTMEMFREE,
    RTMEMTYPE_RTMEMFREEZ,

    RTMEMTYPE_NEW,
    RTMEMTYPE_NEW_ARRAY,
    RTMEMTYPE_DELETE,
    RTMEMTYPE_DELETE_ARRAY
} RTMEMTYPE;

#ifdef RTALLOC_EFENCE_TRACE
/**
 * Node tracking a memory allocation.
 */
typedef struct RTMEMBLOCK
{
    /** Avl node code, key is the user block pointer. */
    AVLPVNODECORE   Core;
    /** Allocation type. */
    RTMEMTYPE       enmType;
    /** The unaligned size of the block. */
    size_t          cbUnaligned;
    /** The aligned size of the block. */
    size_t          cbAligned;
    /** The allocation tag (read-only string). */
    const char     *pszTag;
    /** The return address of the allocator function. */
    void           *pvCaller;
    /** Line number of the alloc call. */
    unsigned        iLine;
    /** File from within the allocation was made. */
    const char     *pszFile;
    /** Function from within the allocation was made. */
    const char     *pszFunction;
} RTMEMBLOCK, *PRTMEMBLOCK;

#endif


/*******************************************************************************
*   Internal Functions                                                         *
******************************************************************************/
RT_C_DECLS_BEGIN
RTDECL(void *)  rtR3MemAlloc(const char *pszOp, RTMEMTYPE enmType, size_t cbUnaligned, size_t cbAligned,
                             const char *pszTag, void *pvCaller, RT_SRC_POS_DECL);
RTDECL(void *)  rtR3MemRealloc(const char *pszOp, RTMEMTYPE enmType, void *pvOld, size_t cbNew,
                               const char *pszTag, void *pvCaller, RT_SRC_POS_DECL);
RTDECL(void)    rtR3MemFree(const char *pszOp, RTMEMTYPE enmType, void *pv, size_t cbUser, void *pvCaller, RT_SRC_POS_DECL);
RT_C_DECLS_END


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
#ifdef RTALLOC_REPLACE_MALLOC
RT_C_DECLS_BEGIN
extern void * (*g_pfnOrgMalloc)(size_t);
extern void * (*g_pfnOrgCalloc)(size_t, size_t);
extern void * (*g_pfnOrgRealloc)(void *, size_t);
extern void   (*g_pfnOrgFree)(void *);
RT_C_DECLS_END
#endif

#endif /* !IPRT_INCLUDED_SRC_r3_alloc_ef_h */

