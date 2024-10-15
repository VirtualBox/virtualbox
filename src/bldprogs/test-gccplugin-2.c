/* $Id: test-gccplugin-2.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Compiler plugin testcase \#2.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

/* Only valid stuff in this one. */
extern void MyIprtPrintf(const char *pszFormat, ...) __attribute__((__iprt_format__(1,2)));
extern void foo(const char *pszFormat, ...);

extern unsigned long long g_ull;

typedef unsigned long long RTGCPHYS;
extern RTGCPHYS g_GCPhys;

typedef RTGCPHYS *PRTGCPHYS;
extern PRTGCPHYS  g_pGCPhys;

#include <stdarg.h>

void foo(const char *pszFormat, ...)
{
    MyIprtPrintf("%s %RX64 %RGp %p %RGp", "foobar", g_ull, g_GCPhys, g_pGCPhys, *g_pGCPhys);
    MyIprtPrintf("%RX32 %d %s\n", 10, 42, "string");
    MyIprtPrintf("%u %.8Rhxs %d\n", 10, &g_ull, 42);
    MyIprtPrintf("%u %.8RhxD %d\n", 10, &g_ull, 42);
    {
        va_list va;
        int iValue;
        va_start(va, pszFormat);
        iValue = va_arg(va, int);
        va_end(va);
        MyIprtPrintf("%u\n", iValue);
    }
}

