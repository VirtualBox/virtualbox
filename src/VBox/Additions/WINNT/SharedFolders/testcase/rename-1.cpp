/* $Id: rename-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Shared Folders FSD - Simple Testcase.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <old> <new>\n", argv[0]);
        return 2;
    }

    SetLastError(0);
    if (MoveFileExA(argv[1], argv[2], 0 /*fFlags*/))
        printf("%s: successfully renamed to: %s\n", argv[1], argv[2]);
    else
    {
        fprintf(stderr, "%s: MoveFileExA(,%s,0) failed: %u\n", argv[1], argv[2], GetLastError());
        return 1;
    }
    return 0;
}

