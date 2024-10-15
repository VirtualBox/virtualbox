/* $Id: chk_stubs.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * glibc stubs for the VirtualBox Guest Addition X11 Client.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

/* If we want the binary to be usable with glibc 2.3, we have to prevent
 VBoxClient from containing later symbols.  This includes resolution of
 symbols from supc++ and gcc_eh. */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

extern int __sprintf_chk(char *psz, int fFlags, size_t cb, const char *pszFormat, ...);
int __sprintf_chk(char *psz, int fFlags, size_t cb, const char *pszFormat, ...)
{
    int rc;
    va_list va;

    (void)fFlags;
    va_start(va, pszFormat);
    rc = vsnprintf(psz, cb, pszFormat, va);
    va_end(va);
    return rc;
}

extern void __stack_chk_fail(void);
void __stack_chk_fail(void)
{
    fprintf(stderr, "Stack check failed!\n");
    _exit(1);
}

#ifdef __x86_64
/* Furthermore, wrap references to memcpy to force them to go to the right
 * version.  We are forced to do it this way because the shared libraries
 * supc++ and gcc_eh contain references which we cannot change. */

extern void *__wrap_memcpy(void *dest, const void *src, size_t n);

asm (".symver memcpy, memcpy@GLIBC_2.2.5");
void *__wrap_memcpy(void *dest, const void *src, size_t n)
{
    return memcpy(dest, src, n);
}
#endif
