/* $Id: vcc-fakes-ws2_32.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Tricks to make the Visual C++ 2010 CRT work on NT4, W2K and XP - WS2_32.DLL.
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
#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/asm.h>
#include <iprt/string.h>

#ifndef RT_ARCH_X86
# error "This code is X86 only"
#endif

#define getaddrinfo                             Ignore_getaddrinfo
#define freeaddrinfo                            Ignore_freeaddrinfo

#include <iprt/win/winsock2.h>
#include <iprt/win/ws2tcpip.h>

#undef getaddrinfo
#undef freeaddrinfo


/** Dynamically resolves an kernel32 API.   */
#define RESOLVE_ME(ApiNm) \
    static bool volatile    s_fInitialized = false; \
    static decltype(ApiNm) *s_pfnApi = NULL; \
    decltype(ApiNm)        *pfnApi; \
    if (s_fInitialized) \
        pfnApi = s_pfnApi; \
    else \
    { \
        pfnApi = (decltype(pfnApi))GetProcAddress(GetModuleHandleW(L"wc2_32.dll"), #ApiNm); \
        s_pfnApi = pfnApi; \
        s_fInitialized = true; \
    } do {} while (0) \


extern "C"
__declspec(dllexport)
int WINAPI getaddrinfo(const char *pszNodeName, const char *pszServiceName, const struct addrinfo *pHints,
                       struct addrinfo **ppResults)
{
    RESOLVE_ME(getaddrinfo);
    if (pfnApi)
        return pfnApi(pszNodeName, pszServiceName, pHints, ppResults);

    /** @todo  fallback */
    WSASetLastError(WSAEAFNOSUPPORT);
    return WSAEAFNOSUPPORT;
}



extern "C"
__declspec(dllexport)
void WINAPI freeaddrinfo(struct addrinfo *pResults)
{
    RESOLVE_ME(freeaddrinfo);
    if (pfnApi)
        pfnApi(pResults);
    else
    {
        Assert(!pResults);
        /** @todo  fallback */
    }
}



/* Dummy to force dragging in this object in the link, so the linker
   won't accidentally use the symbols from kernel32. */
extern "C" int vcc100_ws2_32_fakes_cpp(void)
{
    return 42;
}

