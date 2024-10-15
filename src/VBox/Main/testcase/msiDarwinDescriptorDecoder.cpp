/* $Id: msiDarwinDescriptorDecoder.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * msiDarwinDescriptorDecoder
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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


#include <stdio.h>
#include <iprt/win/windows.h>  /* Avoid -Wall warnings. */



typedef DWORD (WINAPI *PFNMSIDECOMPOSEDESCRIPTORW)(PCWSTR pwszDescriptor,
                                                   LPWSTR pwszProductCode /*[40]*/,
                                                   LPWSTR pwszFeatureId /*[40]*/,
                                                   LPWSTR pwszComponentCode /*[40]*/,
                                                   DWORD *poffArguments);

int wmain(int cArgs, wchar_t **papwszArgs)
{
    HMODULE hmodMsi = LoadLibrary("msi.dll");
    PFNMSIDECOMPOSEDESCRIPTORW pfnMsiDecomposeDescriptorW;
    pfnMsiDecomposeDescriptorW = (PFNMSIDECOMPOSEDESCRIPTORW)GetProcAddress(hmodMsi, "MsiDecomposeDescriptorW");
    if (!pfnMsiDecomposeDescriptorW)
    {
        fprintf(stderr, "Failed to load msi.dll or resolve 'MsiDecomposeDescriptorW'\n");
        return 1;
    }

    int rcExit = 0;
    for (int iArg = 1; iArg < cArgs; iArg++)
    {
        wchar_t wszProductCode[40]   = { 0 };
        wchar_t wszFeatureId[40]     = { 0 };
        wchar_t wszComponentCode[40] = { 0 };
        DWORD   offArguments = ~(DWORD)0;
        DWORD dwErr = pfnMsiDecomposeDescriptorW(papwszArgs[iArg], wszProductCode, wszFeatureId, wszComponentCode, &offArguments);
        if (dwErr == 0)
        {
            fprintf(stderr,
                    "#%u: '%ls'\n"
                    " ->       Product=%ls\n"
                    " ->     FeatureId=%ls\n"
                    " -> ComponentCode=%ls\n"
                    " ->  offArguments=%#lx (%ld)\n"
                    , iArg, papwszArgs[iArg], wszProductCode, wszFeatureId, wszComponentCode, offArguments, offArguments);
        }
        else
        {
            fprintf(stderr,
                    "#%u: '%ls'\n"
                    " -> error %lu (%#lx)\n"
                    , iArg, papwszArgs[iArg], dwErr, dwErr);
            rcExit = 1;
        }
    }

    return rcExit;
}

