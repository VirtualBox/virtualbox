/* $Id: tstVBoxInstHlpRemTAP.cpp 110274 2025-07-16 18:22:54Z klaus.espenlaub@oracle.com $ */
/** @file
 * ???? - no idea what this is supposed to be
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#include <tchar.h>
#include <stdio.h>

#include <msi.h>
#include <msiquery.h>

typedef UINT (CALLBACK *PFNTSTFUNC)(MSIHANDLE);

int main(int argc, char *argv[])
{
    PFNTSTFUNC pfnFunc = NULL;
    UINT uRes = 0;

    HINSTANCE hinstLib = LoadLibrary(TEXT("../VBoxInstallHelper.dll"));
    if (hinstLib)
    {
        pfnFunc = (PFNTSTFUNC)GetProcAddress(hinstLib, argc >= 2 ? argv[1] : "UninstallTAPInstances");
        if (pfnFunc)
            uRes = pfnFunc(NULL);

        FreeLibrary(hinstLib);
    }

    /** @todo */
    if (!hinstLib || !pfnFunc)
        printf("ERROR: Could not call function!\n");
    else
        printf("Test returned: %u\n", uRes);

    return uRes;
}

