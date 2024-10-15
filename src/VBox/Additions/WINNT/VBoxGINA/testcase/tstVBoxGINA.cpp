/* $Id: tstVBoxGINA.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstVBoxGINA.cpp - Simple testcase for invoking VBoxGINA.dll.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#include <iprt/win/windows.h>
#include <iprt/stream.h>

int main()
{
    DWORD dwErr;

    /*
     * Be sure that:
     * - the debug VBoxGINA gets loaded instead of a maybe installed
     *   release version in "C:\Windows\system32".
     */

    HMODULE hMod = LoadLibraryW(L"VBoxGINA.dll");
    if (hMod)
    {
        RTPrintf("VBoxGINA found\n");

        FARPROC pfnDebug = GetProcAddress(hMod, "VBoxGINADebug");
        if (pfnDebug)
        {
            RTPrintf("Calling VBoxGINA ...\n");
            dwErr = pfnDebug();
        }
        else
        {
            dwErr = GetLastError();
            RTPrintf("Could not load VBoxGINADebug, error=%u\n", dwErr);
        }

        FreeLibrary(hMod);
    }
    else
    {
        dwErr = GetLastError();
        RTPrintf("VBoxGINA.dll not found, error=%u\n", dwErr);
    }

    RTPrintf("Test returned: %s (%u)\n", dwErr == ERROR_SUCCESS ? "SUCCESS" : "FAILURE", dwErr);
    return dwErr == ERROR_SUCCESS ? 0 : 1;
}

