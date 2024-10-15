/* $Id: tstHook.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxHook testcase.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/win/windows.h>
#include <VBoxHook.h>
#include <iprt/types.h>


int main()
{
    DWORD cbIgnores;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), RT_STR_TUPLE("Enabling global hook\r\n"), &cbIgnores, NULL);

    HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, VBOXHOOK_GLOBAL_WT_EVENT_NAME);

    VBoxHookInstallWindowTracker(GetModuleHandle("VBoxHook.dll"));

    /* wait for input. */
    uint8_t ch;
    ReadFile(GetStdHandle(STD_INPUT_HANDLE), &ch, sizeof(ch), &cbIgnores, NULL);

    /* disable hook. */
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), RT_STR_TUPLE("Disabling global hook\r\n"), &cbIgnores, NULL);
    VBoxHookRemoveWindowTracker();
    CloseHandle(hEvent);

    return 0;
}

