/* $Id: test-gccplugin-3.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Compiler plugin testcase \#3.
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
#if defined(__i386__) || defined(_M_IX86) || defined(__X86__)
# define RTCALL __attribute__((__cdecl__,__regparm__(0)))
#else
# define RTCALL
#endif
typedef struct HELPERS
{
    void (RTCALL * pfnPrintf)(struct HELPERS *pHlp, const char *pszFormat, ...)
        __attribute__((__iprt_format__(2, 3)));
} HELPERS;

extern void foo(struct HELPERS *pHlp);


void foo(struct HELPERS *pHlp)
{
    pHlp->pfnPrintf(pHlp, "%36 %#x %#x", "string", 42, 42); /// @todo missing 's', need to detect this.
}

