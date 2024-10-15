/* $Id: vcc-fakes-msvcrt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Tricks to make the Visual C++ 2010 CRT work on NT4, W2K and XP.
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
#include <iprt/assert.h>
#include <stdlib.h>


#ifndef RT_ARCH_X86
# error "This code is X86 only"
#endif



/* This one is static in libcmt, fortunately no rocket science.  */
extern "C" void __cdecl my_initterm(PFNRT *papfnStart, PFNRT *papfnEnd)
{
    for (; (uintptr_t)papfnStart < (uintptr_t)papfnEnd; papfnStart++)
        if (*papfnStart)
            (*papfnStart)();
}

extern "C" PFNRT __cdecl my_dllonexit(PFNRT pfnToAdd, PFNRT **ppapfnStart, PFNRT **ppapfnEnd)
{
    /* This is _very_ crude, but it'll probably do for our purposes... */
    size_t cItems = *ppapfnEnd - *ppapfnStart;
    *ppapfnStart = (PFNRT *)realloc(*ppapfnStart, (cItems + 1) * sizeof(**ppapfnStart));
    (*ppapfnStart)[cItems] = pfnToAdd;
    *ppapfnEnd = &(*ppapfnStart)[cItems + 1];
    return pfnToAdd;
}

extern "C" int _newmode;
extern "C" int __cdecl __setargv(void);
extern "C" int __cdecl _setargv(void);

extern "C" int __cdecl my_getmainargs(int *pcArgs, char ***ppapszArgs, char ***ppapszEnv, int fDoWildcardExp, int *pfNewMode)
{
    _newmode = *pfNewMode;

    Assert(!fDoWildcardExp);
    int rc = _setargv();
    if (rc >= 0)
    {
        *pcArgs     = __argc;
        *ppapszArgs = __argv;
        *ppapszEnv  = _environ;
    }
    return rc;
}

extern "C" void __cdecl my_setusermatherr(PFNRT pfnIgnore)
{
    RT_NOREF(pfnIgnore);
    /* pure stub. */
}

