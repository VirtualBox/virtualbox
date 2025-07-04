/* $Id: logbackdoor.cpp 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * VirtualBox Runtime - Guest Backdoor Logging.
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
#include <VBox/log.h>
#include "internal/iprt.h"
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/string.h>
#ifdef IN_GUEST_R3
# include <VBox/VBoxGuestLib.h>
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(size_t) rtLogBackdoorOutput(void *pv, const char *pachChars, size_t cbChars);


RTDECL(size_t) RTLogBackdoorPrintf(const char *pszFormat, ...)
{
    va_list args;
    size_t cb;

    va_start(args, pszFormat);
    cb = RTLogBackdoorPrintfV(pszFormat, args);
    va_end(args);

    return cb;
}

RT_EXPORT_SYMBOL(RTLogBackdoorPrintf);


RTDECL(size_t) RTLogBackdoorPrintfV(const char *pszFormat, va_list args)
{
    return RTLogFormatV(rtLogBackdoorOutput, NULL, pszFormat, args);
}

RT_EXPORT_SYMBOL(RTLogBackdoorPrintfV);


/**
 * Callback for RTLogFormatV which writes to the backdoor.
 * See PFNRTSTROUTPUT() for details.
 */
static DECLCALLBACK(size_t) rtLogBackdoorOutput(void *pvArg, const char *pachChars, size_t cbChars)
{
    RT_NOREF_PV(pvArg);
    RTLogWriteUser(pachChars, cbChars);
    return cbChars;
}


RTDECL(void) RTLogWriteUser(const char *pch, size_t cb)
{
#ifdef IN_GUEST_R3
    VbglR3WriteLog(pch, cb);
#else  /* !IN_GUEST_R3 */
# if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    const uint8_t *pau8 = (const uint8_t *)pch;
    if (cb > 1)
        ASMOutStrU8(RTLOG_DEBUG_PORT, pau8, cb);
    else if (cb)
        ASMOutU8(RTLOG_DEBUG_PORT, *pau8);
# elif defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
    /** @todo Later maybe */
    RT_NOREF(pch, cb);
# else
    /** @todo port me */
    RT_NOREF(pch, cb);
    RT_BREAKPOINT();
# endif
#endif /* !IN_GUEST_R3 */
}

RT_EXPORT_SYMBOL(RTLogWriteUser);

