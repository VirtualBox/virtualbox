/* $Id: VBoxSupLib-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - VBoxSupLib.dll, Windows.
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
#include <iprt/nt/nt-and-windows.h>

#include <iprt/path.h>


/**
 * The Dll main entry point.
 * @remarks The dllexport is for forcing the linker to generate an import
 *          library, so the build system doesn't get confused.
 */
extern "C" __declspec(dllexport)
BOOL __stdcall DllMainEntrypoint(HANDLE hModule, DWORD dwReason, PVOID pvReserved)
{
    RT_NOREF1(pvReserved);

    switch (dwReason)
    {
        /*
         * Make sure the DLL isn't ever unloaded.
         */
        case DLL_PROCESS_ATTACH:
        {
            WCHAR wszName[RTPATH_MAX];
            SetLastError(NO_ERROR);
            if (   GetModuleFileNameW((HMODULE)hModule, wszName, RT_ELEMENTS(wszName)) > 0
                && RtlGetLastWin32Error() == NO_ERROR)
            {
                int cExtraLoads = 2;
                while (cExtraLoads-- > 0)
                    LoadLibraryW(wszName);
            }
            break;
        }

        case DLL_THREAD_ATTACH:
        {
#ifdef VBOX_WITH_HARDENING
# ifndef VBOX_WITHOUT_DEBUGGER_CHECKS
            /*
             * Anti debugging hack that prevents most debug notifications from
             * ending up in the debugger.
             */
            NTSTATUS rcNt = NtSetInformationThread(GetCurrentThread(), ThreadHideFromDebugger, NULL, 0);
            if (!NT_SUCCESS(rcNt))
            {
                __debugbreak();
                return FALSE;
            }
# endif
#endif
            break;
        }

        case DLL_THREAD_DETACH:
            /* Nothing to do. */
            break;

        case DLL_PROCESS_DETACH:
            /* Nothing to do. */
            break;

        default:
            /* ignore */
            break;
    }
    return TRUE;
}

