/* $Id: ntGetTimerResolution.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Win32 (NT) testcase for getting the timer resolution.
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
#define _WIN32_WINNT 0x0500
#include <iprt/win/windows.h>
#include <iprt/types.h>
#include <stdio.h>

extern "C" {
/* from sysinternals. */
NTSYSAPI LONG NTAPI NtQueryTimerResolution(OUT PULONG MaximumResolution, OUT PULONG MinimumResolution, OUT PULONG CurrentResolution);
}


int main()
{
    ULONG Min = UINT32_MAX;
    ULONG Max = UINT32_MAX;
    ULONG Cur = UINT32_MAX;
    NtQueryTimerResolution(&Max, &Min, &Cur);
    printf("NtQueryTimerResolution -> Max=%08luns Min=%08luns Cur=%08luns\n", Min * 100, Max * 100, Cur * 100);

#if 0
    /* figure out the 100ns relative to the 1970 epoc. */
    SYSTEMTIME st;
    st.wYear = 1970;
    st.wMonth = 1;
    st.wDayOfWeek = 4; /* Thor's day. */
    st.wDay = 1;
    st.wHour = 0;
    st.wMinute = 0;
    st.wSecond = 0;
    st.wMilliseconds = 0;

    FILETIME ft;
    if (SystemTimeToFileTime(&st, &ft))
    {
        printf("epoc is %I64u (0x%08x%08x)\n", ft, ft.dwHighDateTime, ft.dwLowDateTime);
        if (FileTimeToSystemTime(&ft, &st))
            printf("unix epoc: %d-%02d-%02d %02d:%02d:%02d.%03d (week day %d)\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, st.wDayOfWeek);
        else
            printf("FileTimeToSystemTime failed, lasterr=%d\n", GetLastError());
    }
    else
        printf("SystemTimeToFileTime failed, lasterr=%d\n", GetLastError());

    ft.dwHighDateTime = 0;
    ft.dwLowDateTime = 0;
    if (FileTimeToSystemTime(&ft, &st))
        printf("nt time start: %d-%02d-%02d %02d:%02d:%02d.%03d (week day %d)\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, st.wDayOfWeek);
    else
        printf("FileTimeToSystemTime failed, lasterr=%d\n", GetLastError());
#endif
    return 0;
}

