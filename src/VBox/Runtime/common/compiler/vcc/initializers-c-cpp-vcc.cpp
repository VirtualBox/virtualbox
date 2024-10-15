/* $Id: initializers-c-cpp-vcc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Visual C++ Compiler - C & C++ Initializers and Terminators.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#define IPRT_COMPILER_VCC_WITH_C_INIT_TERM_SECTIONS
#define IPRT_COMPILER_VCC_WITH_CPP_INIT_SECTIONS
#include "internal/compiler-vcc.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef void (__cdecl *PFNVCINITTERM)(void);
typedef int  (__cdecl *PFNVCINITTERMRET)(void);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** @name Initializer arrays.
 *
 * The important thing here are the section names, the linker wi.
 *
 * @{ */
/** Start of the C initializer array. */
__declspec(allocate(".CRT$XIA"))    PFNVCINITTERMRET    g_apfnRTVccInitializers_C_Start     = NULL;
/** End of the C initializer array. */
__declspec(allocate(".CRT$XIZ"))    PFNVCINITTERMRET    g_apfnRTVccInitializers_C_End       = NULL;

/** Start of the C pre-terminator array. */
__declspec(allocate(".CRT$XPA"))    PFNVCINITTERM       g_apfnRTVccEarlyTerminators_C_Start = NULL;
/** End of the C pre-terminator array. */
__declspec(allocate(".CRT$XPZ"))    PFNVCINITTERM       g_apfnRTVccEarlyTerminators_C_End   = NULL;

/** Start of the C terminator array. */
__declspec(allocate(".CRT$XTA"))    PFNVCINITTERM       g_apfnRTVccTerminators_C_Start      = NULL;
/** End of the C terminator array. */
__declspec(allocate(".CRT$XTZ"))    PFNVCINITTERM       g_apfnRTVccTerminators_C_End        = NULL;

/** Start of the C++ initializer array. */
__declspec(allocate(".CRT$XCA"))    PFNVCINITTERM       g_apfnRTVccInitializers_Cpp_Start   = NULL;
/** End of the C++ initializer array. */
__declspec(allocate(".CRT$XCZ"))    PFNVCINITTERM       g_apfnRTVccInitializers_Cpp_End     = NULL;


/* Tell the linker to merge the .CRT* sections into .rdata */
#pragma comment(linker, "/merge:.CRT=.rdata ")
/** @} */


/**
 * Runs the C and C++ initializers.
 *
 * @returns 0 on success, non-zero return from C initalizer on failure.
 */
int rtVccInitializersRunInit(void)
{
    /*
     * Run the C initializers first.
     */
    for (PFNVCINITTERMRET *ppfn = &g_apfnRTVccInitializers_C_Start;
         (uintptr_t)ppfn < (uintptr_t)&g_apfnRTVccInitializers_C_End;
         ppfn++)
    {
        PFNVCINITTERMRET const pfn = *ppfn;
        if (pfn)
        {
            int const rc = pfn();
            if (RT_LIKELY(rc == 0))
            { /* likely */ }
            else
                return rc;
        }
    }

    /*
     * Run the C++ initializers.
     */
    for (PFNVCINITTERM *ppfn = &g_apfnRTVccInitializers_Cpp_Start;
         (uintptr_t)ppfn < (uintptr_t)&g_apfnRTVccInitializers_Cpp_End;
         ppfn++)
    {
        PFNVCINITTERM const pfn = *ppfn;
        if (pfn)
            pfn();
    }

    return 0;
}


/**
 * Runs the C terminator callbacks.
 */
void rtVccInitializersRunTerm(void)
{
    /*
     * First the early terminators.
     */
    for (PFNVCINITTERM *ppfn = &g_apfnRTVccEarlyTerminators_C_Start;
         (uintptr_t)ppfn < (uintptr_t)&g_apfnRTVccEarlyTerminators_C_End;
         ppfn++)
    {
        PFNVCINITTERM const pfn = *ppfn;
        if (pfn)
            pfn();
    }

    /*
     * Then the real terminator list.
     */
    for (PFNVCINITTERM *ppfn = &g_apfnRTVccTerminators_C_Start;
         (uintptr_t)ppfn < (uintptr_t)&g_apfnRTVccTerminators_C_End;
         ppfn++)
    {
        PFNVCINITTERM const pfn = *ppfn;
        if (pfn)
            pfn();
    }

}

