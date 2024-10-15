/* $Id: tstFileAppendWin-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Exploration of File Appending on Windows.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <stdio.h>
#include <string.h>
#include <stdarg.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static int g_cErrors = 0;


static int MyFailure(const char *pszFormat, ...)
{
    va_list va;

    printf("tstFileAppendWin-1: FATAL: ");
    va_start(va, pszFormat);
    vprintf(pszFormat, va);
    va_end(va);
    g_cErrors++;
    return 1;
}


void MyError(const char *pszFormat, ...)
{
    va_list va;

    printf("tstFileAppendWin-1: ERROR: ");
    va_start(va, pszFormat);
    vprintf(pszFormat, va);
    va_end(va);
    g_cErrors++;
}


int main()
{
    HANDLE hFile;
    LARGE_INTEGER off;
    DWORD cb;
    char szBuf[256];


    printf("tstFileAppendWin-1: TESTING...\n");

    /*
     * Open it write only and do some appending.
     * Checking that read fails and that the file position changes after the read.
     */
    DeleteFile("tstFileAppendWin-1.tst");
    hFile = CreateFile("tstFileAppendWin-1.tst",
                       (FILE_GENERIC_WRITE & ~FILE_WRITE_DATA),
                       FILE_SHARE_READ,
                       NULL,
                       CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return MyFailure("1st CreateFile: %d\n", GetLastError());

    off.QuadPart = 0;
    if (!SetFilePointerEx(hFile, off, &off, FILE_CURRENT))
        MyError("1st SetFilePointerEx failed: %d\n", GetLastError());
    else if (off.QuadPart != 0)
        MyError("unexpected position on open: %ld - expected 0\n", (long)off.QuadPart);

    if (!WriteFile(hFile, "0123456789", 10, &cb, NULL))
        MyError("write fail: %d\n", GetLastError());

    off.QuadPart = 0;
    if (!SetFilePointerEx(hFile, off, &off, FILE_CURRENT))
        MyError("2nd SetFilePointerEx failed: %d\n", GetLastError());
    else if (off.QuadPart != 10)
        MyError("unexpected position on write: %ld - expected 10\n", (long)off.QuadPart);
    else
        printf("tstFileAppendWin-1: off=%ld after first write\n", (long)off.QuadPart);

    SetLastError(0);
    if (ReadFile(hFile, szBuf, 1, &cb, NULL))
        MyError("read didn't fail! cb=%#lx lasterr=%d\n", (long)cb, GetLastError());

    off.QuadPart = 5;
    if (!SetFilePointerEx(hFile, off, &off, FILE_BEGIN))
        MyError("3rd SetFilePointerEx failed: %d\n", GetLastError());
    else if (off.QuadPart != 5)
        MyError("unexpected position after set file pointer: %ld - expected 5\n", (long)off.QuadPart);

    CloseHandle(hFile);

    /*
     * Open it write only and do some more appending.
     * Checking the initial position and that it changes after the write.
     */
    hFile = CreateFile("tstFileAppendWin-1.tst",
                       (FILE_GENERIC_WRITE & ~FILE_WRITE_DATA),
                       FILE_SHARE_READ,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return MyFailure("2nd CreateFile: %d\n", GetLastError());

    off.QuadPart = 0;
    if (!SetFilePointerEx(hFile, off, &off, FILE_CURRENT))
        MyError("4th SetFilePointerEx failed: %d\n", GetLastError());
    else if (off.QuadPart != 0)
        MyError("unexpected position on open: %ld - expected 0\n", (long)off.QuadPart);
    else
        printf("tstFileAppendWin-1: off=%ld on 2nd open\n", (long)off.QuadPart);

    if (!WriteFile(hFile, "abcdefghij", 10, &cb, NULL))
        MyError("2nd write failed: %d\n", GetLastError());

    off.QuadPart = 0;
    if (!SetFilePointerEx(hFile, off, &off, FILE_CURRENT))
        MyError("5th SetFilePointerEx failed: %d\n", GetLastError());
    else if (off.QuadPart != 20)
        MyError("unexpected position on 2nd write: %ld - expected 20\n", (long)off.QuadPart);
    else
        printf("tstFileAppendWin-1: off=%ld after 2nd write\n", (long)off.QuadPart);

    CloseHandle(hFile);

    /*
     * Open it read/write.
     * Check the initial position and read stuff. Then append some more and
     * check the new position and see that read returns 0/EOF. Finally,
     * do some seeking and read from a new position.
     */
    hFile = CreateFile("tstFileAppendWin-1.tst",
                       (FILE_GENERIC_WRITE & ~FILE_WRITE_DATA) | GENERIC_READ,
                       FILE_SHARE_READ,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return MyFailure("3rd CreateFile: %d\n", GetLastError());

    off.QuadPart = 0;
    if (!SetFilePointerEx(hFile, off, &off, FILE_CURRENT))
        MyError("6th SetFilePointerEx failed: %d\n", GetLastError());
    else if (off.QuadPart != 0)
        MyError("unexpected position on open: %ld - expected 0\n", (long)off.QuadPart);
    else
        printf("tstFileAppendWin-1: off=%ld on 3rd open\n", (long)off.QuadPart);

    if (!ReadFile(hFile, szBuf, 10, &cb, NULL) || cb != 10)
        MyError("1st ReadFile failed: %d\n", GetLastError());
    else if (memcmp(szBuf, "0123456789", 10))
        MyError("read the wrong stuff: %.10s - expected 0123456789\n", szBuf);

    off.QuadPart = 0;
    if (!SetFilePointerEx(hFile, off, &off, FILE_CURRENT))
        MyError("7th SetFilePointerEx failed: %d\n", GetLastError());
    else if (off.QuadPart != 10)
        MyError("unexpected position on 1st read: %ld - expected 0\n", (long)off.QuadPart);
    else
        printf("tstFileAppendWin-1: off=%ld on 1st read\n", (long)off.QuadPart);

    if (!WriteFile(hFile, "klmnopqrst", 10, &cb, NULL))
        MyError("3rd write failed: %d\n", GetLastError());

    off.QuadPart = 0;
    if (!SetFilePointerEx(hFile, off, &off, FILE_CURRENT))
        MyError("8th SetFilePointerEx failed: %d\n", GetLastError());
    else if (off.QuadPart != 30)
        MyError("unexpected position on 3rd write: %ld - expected 30\n", (long)off.QuadPart);
    else
        printf("tstFileAppendWin-1: off=%ld after 3rd write\n", (long)off.QuadPart);

    SetLastError(0);
    if (ReadFile(hFile, szBuf, 1, &cb, NULL) && cb != 0)
        MyError("read after write didn't fail! cb=%#lx lasterr=%d\n", (long)cb, GetLastError());

    off.QuadPart = 15;
    if (!SetFilePointerEx(hFile, off, &off, FILE_BEGIN))
        MyError("9th SetFilePointerEx failed: %d\n", GetLastError());
    else if (off.QuadPart != 15)
        MyError("unexpected position on 3rd write: %ld - expected 15\n", (long)off.QuadPart);
    else
    {
        if (!ReadFile(hFile, szBuf, 10, &cb, NULL) || cb != 10)
            MyError("1st ReadFile failed: %d\n", GetLastError());
        else if (memcmp(szBuf, "fghijklmno", 10))
            MyError("read the wrong stuff: %.10s - expected fghijklmno\n", szBuf);

        off.QuadPart = 0;
        if (!SetFilePointerEx(hFile, off, &off, FILE_CURRENT))
            MyError("10th SetFilePointerEx failed: %d\n", GetLastError());
        else if (off.QuadPart != 25)
            MyError("unexpected position on 2nd read: %ld - expected 25\n", (long)off.QuadPart);
        else
            printf("tstFileAppendWin-1: off=%ld after 2nd read\n", (long)off.QuadPart);
    }

    CloseHandle(hFile);

    /*
     * Open it read only + append and check that we cannot write to it.
     */
    hFile = CreateFile("tstFileAppendWin-1.tst",
                       FILE_APPEND_DATA | GENERIC_READ,
                       FILE_SHARE_READ,
                       NULL,
                       CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return MyFailure("4th CreateFile: %d\n", GetLastError());

    SetLastError(0);
    if (WriteFile(hFile, "pqrstuvwx", 10, &cb, NULL))
        MyError("write didn't on read-only+append open: %d cb=%#lx\n", GetLastError(), (long)cb);

    CloseHandle(hFile);
    DeleteFile("tstfileAppendWin-1.tst");

    if (g_cErrors)
        printf("tstFileAppendWin-1: FAILED\n");
    else
        printf("tstFileAppendWin-1: SUCCESS\n");
    return g_cErrors ? 1 : 0;
}

