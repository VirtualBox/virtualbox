/* $Id: ldr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Binary Image Loader.
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
#define LOG_GROUP RTLOGGROUP_LDR
#include <iprt/ldr.h>
#include "internal/iprt.h"

#include <iprt/alloc.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include "internal/ldr.h"


RTDECL(int) RTLdrGetSymbol(RTLDRMOD hLdrMod, const char *pszSymbol, void **ppvValue)
{
    LogFlow(("RTLdrGetSymbol: hLdrMod=%RTldrm pszSymbol=%p:{%s} ppvValue=%p\n",
             hLdrMod, pszSymbol, pszSymbol, ppvValue));
    /*
     * Validate input.
     */
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    AssertMsgReturn(pszSymbol, ("pszSymbol=%p\n", pszSymbol), VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppvValue, VERR_INVALID_POINTER);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    //AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), VERR_WRONG_ORDER);

    /*
     * Do it.
     */
    int rc;
    if (pMod->pOps->pfnGetSymbol)
        rc = pMod->pOps->pfnGetSymbol(pMod, pszSymbol, ppvValue);
    else
    {
        RTUINTPTR Value = 0;
        rc = pMod->pOps->pfnGetSymbolEx(pMod, NULL, 0, UINT32_MAX, pszSymbol, &Value);
        if (RT_SUCCESS(rc))
        {
            *ppvValue = (void *)(uintptr_t)Value;
            if ((uintptr_t)*ppvValue != Value)
                rc = VERR_BUFFER_OVERFLOW;
        }
    }
    LogFlow(("RTLdrGetSymbol: return %Rrc *ppvValue=%p\n", rc, *ppvValue));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrGetSymbol);


RTDECL(PFNRT) RTLdrGetFunction(RTLDRMOD hLdrMod, const char *pszSymbol)
{
    PFNRT pfn;
    int rc = RTLdrGetSymbol(hLdrMod, pszSymbol, (void **)&pfn);
    if (RT_SUCCESS(rc))
        return pfn;
    return NULL;
}
RT_EXPORT_SYMBOL(RTLdrGetFunction);


RTDECL(RTLDRFMT) RTLdrGetFormat(RTLDRMOD hLdrMod)
{
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), RTLDRFMT_INVALID);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    return pMod->enmFormat;
}
RT_EXPORT_SYMBOL(RTLdrGetFormat);


RTDECL(RTLDRTYPE) RTLdrGetType(RTLDRMOD hLdrMod)
{
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), RTLDRTYPE_INVALID);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    return pMod->enmType;
}
RT_EXPORT_SYMBOL(RTLdrGetType);


RTDECL(RTLDRENDIAN) RTLdrGetEndian(RTLDRMOD hLdrMod)
{
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), RTLDRENDIAN_INVALID);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    return pMod->enmEndian;
}
RT_EXPORT_SYMBOL(RTLdrGetEndian);


RTDECL(RTLDRARCH) RTLdrGetArch(RTLDRMOD hLdrMod)
{
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), RTLDRARCH_INVALID);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    return pMod->enmArch;
}
RT_EXPORT_SYMBOL(RTLdrGetArch);


RTDECL(int) RTLdrClose(RTLDRMOD hLdrMod)
{
    LogFlow(("RTLdrClose: hLdrMod=%RTldrm\n", hLdrMod));

    /*
     * Validate input.
     */
    if (hLdrMod == NIL_RTLDRMOD)
        return VINF_SUCCESS;
    AssertMsgReturn(rtldrIsValid(hLdrMod), ("hLdrMod=%p\n", hLdrMod), VERR_INVALID_HANDLE);
    PRTLDRMODINTERNAL pMod = (PRTLDRMODINTERNAL)hLdrMod;
    //AssertMsgReturn(pMod->eState == LDR_STATE_OPENED, ("eState=%d\n", pMod->eState), VERR_WRONG_ORDER);

    /*
     * Do it.
     */
    int rc = pMod->pOps->pfnClose(pMod);
    AssertRC(rc);
    pMod->eState = LDR_STATE_INVALID;
    pMod->u32Magic++;
    if (pMod->pReader)
    {
        rc = pMod->pReader->pfnDestroy(pMod->pReader);
        AssertRC(rc);
        pMod->pReader = NULL;
    }
    RTMemFree(pMod);

    LogFlow(("RTLdrClose: returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTLdrClose);


RTDECL(RTLDRARCH) RTLdrGetHostArch(void)
{
#if   defined(RT_ARCH_AMD64)
    RTLDRARCH enmArch = RTLDRARCH_AMD64;
#elif defined(RT_ARCH_X86)
    RTLDRARCH enmArch = RTLDRARCH_X86_32;
#elif defined(RT_ARCH_ARM) || defined(RT_ARCH_ARM32)
    RTLDRARCH enmArch = RTLDRARCH_ARM32;
#elif defined(RT_ARCH_ARM64)
    RTLDRARCH enmArch = RTLDRARCH_ARM64;
#else
    RTLDRARCH enmArch = RTLDRARCH_WHATEVER;
#endif
    return enmArch;
}
RT_EXPORT_SYMBOL(RTLdrGetHostArch);

