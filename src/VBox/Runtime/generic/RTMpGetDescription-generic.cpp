/* $Id: RTMpGetDescription-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, Generic RTMpGetDescription.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/mp.h>
#include "internal/iprt.h"
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/err.h>
#include <iprt/string.h>



/**
 * Returns "Unknown" as description.
 *
 * @returns VERR_BUFFER_OVERFLOW or VINF_SUCCESS.
 * @param   pszBuf              Output buffer.
 * @param   cbBuf               Buffer size.
 */
static int rtMpGetDescriptionUnknown(char *pszBuf, size_t cbBuf)
{
    static const char s_szUnknown[] = "Unknown";
    if (cbBuf < sizeof(s_szUnknown))
        return VERR_BUFFER_OVERFLOW;
    memcpy(pszBuf, s_szUnknown, sizeof(s_szUnknown));
    return VINF_SUCCESS;
}


RTDECL(int) RTMpGetDescription(RTCPUID idCpu, char *pszBuf, size_t cbBuf)
{
    /*
     * Check that the specified cpu is valid & online.
     */
    if (idCpu != NIL_RTCPUID && !RTMpIsCpuOnline(idCpu))
        return RTMpIsCpuPossible(idCpu)
             ? VERR_CPU_OFFLINE
             : VERR_CPU_NOT_FOUND;

    /*
     * Construct the description string in a temporary buffer.
     */
    char        szString[4*4*3+1];
    RT_ZERO(szString);
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    if (!ASMHasCpuId())
        return rtMpGetDescriptionUnknown(pszBuf, cbBuf);

    uint32_t    uMax;
    uint32_t    uEBX, uECX, uEDX;
    ASMCpuId(0x80000000, &uMax, &uEBX, &uECX, &uEDX);
    if (uMax >= 0x80000002)
    {
        ASMCpuId(0x80000002,     &szString[0  + 0], &szString[0  + 4], &szString[0  + 8], &szString[0  + 12]);
        if (uMax >= 0x80000003)
            ASMCpuId(0x80000003, &szString[16 + 0], &szString[16 + 4], &szString[16 + 8], &szString[16 + 12]);
        if (uMax >= 0x80000004)
            ASMCpuId(0x80000004, &szString[32 + 0], &szString[32 + 4], &szString[32 + 8], &szString[32 + 12]);
    }
    else
    {
        ASMCpuId(0x00000000, &uMax, &uEBX, &uECX, &uEDX);
        ((uint32_t *)&szString[0])[0] = uEBX;
        ((uint32_t *)&szString[0])[1] = uEDX;
        ((uint32_t *)&szString[0])[2] = uECX;
    }

#elif defined(RT_ARCH_ARM64)
    RTCCINTREG uFreq;

#else
# error "PORTME or use RTMpGetDescription-generic-stub.cpp."
#endif

    /*
     * Copy it out into the buffer supplied by the caller.
     */
    char   *pszSrc = RTStrStrip(szString);
    size_t  cchSrc = strlen(pszSrc);
    if (cchSrc >= cbBuf)
        return VERR_BUFFER_OVERFLOW;
    memcpy(pszBuf, pszSrc, cchSrc + 1);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTMpGetDescription);

