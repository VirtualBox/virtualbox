/* $Id: dllmain-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Win32 DllMain (Ring-3).
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
#include <iprt/win/windows.h>
#include <iprt/thread.h>
#include <iprt/param.h>
#include "internal/thread.h"



/**
 * Increases the load count on the IPRT DLL so it won't unload.
 *
 * This is a separate function so as to not overflow the stack of threads with
 * very little of it.
 *
 * @param   hModule     The IPRT DLL module handle.
 */
DECL_NO_INLINE(static, void) EnsureNoUnload(HMODULE hModule)
{
    WCHAR wszName[RTPATH_MAX];
    SetLastError(NO_ERROR);
    if (   GetModuleFileNameW(hModule, wszName, RT_ELEMENTS(wszName)) > 0
        && GetLastError() == NO_ERROR)
    {
        int cExtraLoads = 32;
        while (cExtraLoads-- > 0)
            LoadLibraryW(wszName);
    }
}


/**
 * The Dll main entry point.
 */
BOOL __stdcall DllMain(HANDLE hModule, DWORD dwReason, PVOID pvReserved)
{
    RT_NOREF_PV(pvReserved);

    switch (dwReason)
    {
        /*
         * When attaching to a process, we'd like to make sure IPRT stays put
         * and doesn't get unloaded.
         */
        case DLL_PROCESS_ATTACH:
            EnsureNoUnload((HMODULE)hModule);
            break;

        case DLL_PROCESS_DETACH:
        case DLL_THREAD_ATTACH:
        default:
            /* ignore */
            break;

        case DLL_THREAD_DETACH:
            rtThreadWinTlsDestruction();
            rtThreadNativeDetach();
            break;
    }
    return TRUE;
}

