/* $Id: nocrt-startup-dll-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - Windows EXE startup code.
 *
 * @note Does not run static constructors and destructors!
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
#include "internal/iprt.h"
#include "internal/process.h"

#include <iprt/nt/nt-and-windows.h>
#include <iprt/getopt.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#ifdef IPRT_NO_CRT
# include <iprt/asm.h>
# include <iprt/nocrt/stdlib.h>
#endif

#include "internal/compiler-vcc.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static volatile int32_t g_cAttached = 0;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID pvReserved);


DECL_NO_INLINE(static, BOOL) rtVccDllMainForward(HINSTANCE hInstance, DWORD dwReason, LPVOID pvReserved)
{
    return DllMain(hInstance, dwReason, pvReserved);
}



DECL_NO_INLINE(static, BOOL) rtVccDllMainProcessAttach(HINSTANCE hInstance, LPVOID pvReserved)
{
    /*
     * Initialize the CRT the first time thru.
     */
    if (g_cAttached == 0)
    {
        rtVccWinInitProcExecPath();

        int rc = rtVccInitializersRunInit();
        if (RT_FAILURE(rc))
            return FALSE;
    }
    g_cAttached++;

    /*
     * Call the DllMain function.
     */
    BOOL fRet = rtVccDllMainForward(hInstance, DLL_PROCESS_ATTACH, pvReserved);

    /*
     * On failure, we call the DllMain function again, decrement the init counter
     * and probably run termination callbacks.
     */
    if (!fRet)
    {
        rtVccDllMainForward(hInstance, DLL_PROCESS_DETACH, pvReserved);
        if (--g_cAttached == 0)
        {
            rtVccTermRunAtExit();
            rtVccInitializersRunTerm();
        }
    }
    return fRet;
}


DECL_NO_INLINE(static, BOOL) rtVccDllMainProcessDetach(HINSTANCE hInstance, LPVOID pvReserved)
{
    /*
     * Make sure there isn't an imbalance before calling DllMain and shutting
     * down our own internals.
     */
    if (g_cAttached <= 0)
        return FALSE;

    /*
     * Call DllMain.
     */
    BOOL fRet = rtVccDllMainForward(hInstance, DLL_PROCESS_DETACH, pvReserved);

    /*
     * Work g_cAttached and probably do uninitialization.  We'll do this regardless
     * of what DllMain returned.
     */
    if (--g_cAttached == 0)
    {
        rtVccTermRunAtExit();
        rtVccInitializersRunTerm();
    }
    return fRet;
}


extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE hInstance, DWORD dwReason, LPVOID pvReserved)
{
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
#ifdef RT_ARCH_X86
            rtVccWinInitBssOnNt3((PVOID)hInstance);
#endif
            rtVccInitSecurityCookie(); /* This function must be minimal because of this! */
            return rtVccDllMainProcessAttach(hInstance, pvReserved);

        case DLL_PROCESS_DETACH:
            return rtVccDllMainProcessDetach(hInstance, pvReserved);

        default:
            return rtVccDllMainForward(hInstance, dwReason, pvReserved);
    }
}

