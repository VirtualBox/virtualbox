/* $Id: dbgkrnlinfo-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Kernel debug information, Ring-0 Driver, Solaris Code.
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
#include "the-solaris-kernel.h"
#include "internal/iprt.h"

#include <iprt/dbg.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Solaris kernel debug info instance data.
 */
typedef struct RTDBGKRNLINFOINT
{
    /** Magic value (RTDBGKRNLINFO_MAGIC). */
    uint32_t volatile   u32Magic;
    /** The number of threads referencing this object. */
    uint32_t volatile   cRefs;
    /** Pointer to the genunix CTF handle. */
    ctf_file_t         *pGenUnixCTF;
    /** Pointer to the genunix module handle. */
    modctl_t           *pGenUnixMod;
} RTDBGKRNLINFOINT;
/** Pointer to the solaris kernel debug info instance data. */
typedef struct RTDBGKRNLINFOINT *PRTDBGKRNLINFOINT;


/**
 * Retains a kernel module and opens the CTF data associated with it.
 *
 * @param pszModule     The name of the module to open.
 * @param ppMod         Where to store the module handle.
 * @param ppCTF         Where to store the module's CTF handle.
 *
 * @return IPRT status code.
 */
static int rtR0DbgKrnlInfoModRetain(char *pszModule, modctl_t **ppMod, ctf_file_t **ppCTF)
{
    AssertPtrReturn(pszModule, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppMod, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppCTF, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    modid_t ModId = mod_name_to_modid(pszModule);
    if (ModId != -1)
    {
        *ppMod = mod_hold_by_id(ModId);
        if (*ppMod)
        {
            /*
             * Hold mod_lock as ctf_modopen may update the module with uncompressed CTF data.
             */
            int err;
            mutex_enter(&mod_lock);
            *ppCTF = ctf_modopen(((modctl_t *)*ppMod)->mod_mp, &err);
            mutex_exit(&mod_lock);
            mod_release_mod(*ppMod);

            if (*ppCTF)
                return VINF_SUCCESS;
            else
            {
                LogRel(("rtR0DbgKrnlInfoModRetain: ctf_modopen failed for '%s' err=%d\n", pszModule, err));
                rc = VERR_INTERNAL_ERROR_3;
            }
        }
        else
        {
            LogRel(("rtR0DbgKrnlInfoModRetain: mod_hold_by_id failed for '%s'\n", pszModule));
            rc = VERR_INTERNAL_ERROR_2;
        }
    }
    else
    {
        LogRel(("rtR0DbgKrnlInfoModRetain: mod_name_to_modid failed for '%s'\n", pszModule));
        rc = VERR_INTERNAL_ERROR;
    }

    return rc;
}


/**
 * Releases the kernel module and closes its CTF data.
 *
 * @param pMod          Pointer to the module handle.
 * @param pCTF          Pointer to the module's CTF handle.
 */
static void rtR0DbgKrnlInfoModRelease(modctl_t *pMod, ctf_file_t *pCTF)
{
    AssertPtrReturnVoid(pMod);
    AssertPtrReturnVoid(pCTF);

    ctf_close(pCTF);
}


/**
 * Helper for opening the specified kernel module.
 *
 * @param pszModule         The name of the module.
 * @param ppMod             Where to store the module handle.
 * @param ppCtf             Where to store the module's CTF handle.
 *
 * @returns Pointer to the CTF structure for the module.
 */
static int rtR0DbgKrnlInfoModRetainEx(const char *pszModule, modctl_t **ppMod, ctf_file_t **ppCtf)
{
    char *pszMod = RTStrDup(pszModule);
    if (RT_LIKELY(pszMod))
    {
        int rc = rtR0DbgKrnlInfoModRetain(pszMod, ppMod, ppCtf);
        RTStrFree(pszMod);
        if (RT_SUCCESS(rc))
        {
            AssertPtrReturn(*ppMod, VERR_INTERNAL_ERROR_2);
            AssertPtrReturn(*ppCtf, VERR_INTERNAL_ERROR_3);
        }
        return rc;
    }
    return VERR_NO_MEMORY;
}


RTR0DECL(int) RTR0DbgKrnlInfoOpen(PRTDBGKRNLINFO phKrnlInfo, uint32_t fFlags)
{
    AssertReturn(fFlags == 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phKrnlInfo, VERR_INVALID_POINTER);
    /* This can be called as part of IPRT init, in which case we have no thread preempt information yet. */
    if (g_frtSolInitDone)
        RT_ASSERT_PREEMPTIBLE();

    *phKrnlInfo = NIL_RTDBGKRNLINFO;
    PRTDBGKRNLINFOINT pThis = (PRTDBGKRNLINFOINT)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    char szGenUnixModName[] = "genunix";
    int rc = rtR0DbgKrnlInfoModRetain(szGenUnixModName, &pThis->pGenUnixMod, &pThis->pGenUnixCTF);
    if (RT_SUCCESS(rc))
    {
        pThis->u32Magic       = RTDBGKRNLINFO_MAGIC;
        pThis->cRefs          = 1;

        *phKrnlInfo = pThis;
        return VINF_SUCCESS;
    }

    LogRel(("RTR0DbgKrnlInfoOpen: rtR0DbgKrnlInfoModRetain failed rc=%d.\n", rc));
    RTMemFree(pThis);
    return rc;
}


RTR0DECL(uint32_t) RTR0DbgKrnlInfoRetain(RTDBGKRNLINFO hKrnlInfo)
{
    PRTDBGKRNLINFOINT pThis = hKrnlInfo;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs && cRefs < 100000);
    return cRefs;
}


RTR0DECL(uint32_t) RTR0DbgKrnlInfoRelease(RTDBGKRNLINFO hKrnlInfo)
{
    PRTDBGKRNLINFOINT pThis = hKrnlInfo;
    if (pThis == NIL_RTDBGKRNLINFO)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), UINT32_MAX);
    if (g_frtSolInitDone)
        RT_ASSERT_PREEMPTIBLE();

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    if (cRefs == 0)
    {
        pThis->u32Magic = ~RTDBGKRNLINFO_MAGIC;
        rtR0DbgKrnlInfoModRelease(pThis->pGenUnixMod, pThis->pGenUnixCTF);
        RTMemFree(pThis);
    }
    return cRefs;
}


RTR0DECL(int) RTR0DbgKrnlInfoQueryMember(RTDBGKRNLINFO hKrnlInfo, const char *pszModule, const char *pszStructure,
                                         const char *pszMember, size_t *poffMember)
{
    PRTDBGKRNLINFOINT pThis = hKrnlInfo;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    AssertPtrReturn(pszMember, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszStructure, VERR_INVALID_PARAMETER);
    AssertPtrReturn(poffMember, VERR_INVALID_PARAMETER);
    if (g_frtSolInitDone)
        RT_ASSERT_PREEMPTIBLE();

    ctf_file_t *pCtf = NULL;
    modctl_t   *pMod = NULL;
    if (!pszModule)
    {
        pCtf = pThis->pGenUnixCTF;
        pMod = pThis->pGenUnixMod;
    }
    else
    {
        int rc2 = rtR0DbgKrnlInfoModRetainEx(pszModule, &pMod, &pCtf);
        if (RT_FAILURE(rc2))
            return rc2;
        Assert(pMod);
        Assert(pCtf);
    }

    int rc = VERR_NOT_FOUND;
    ctf_id_t TypeIdent = ctf_lookup_by_name(pCtf, pszStructure);
    if (TypeIdent != CTF_ERR)
    {
        ctf_membinfo_t MemberInfo;
        RT_ZERO(MemberInfo);
        if (ctf_member_info(pCtf, TypeIdent, pszMember, &MemberInfo) != CTF_ERR)
        {
            *poffMember = (MemberInfo.ctm_offset >> 3);
            rc = VINF_SUCCESS;
        }
    }

    if (pszModule)
        rtR0DbgKrnlInfoModRelease(pMod, pCtf);
    return rc;
}


RTR0DECL(int) RTR0DbgKrnlInfoQuerySymbol(RTDBGKRNLINFO hKrnlInfo, const char *pszModule,
                                               const char *pszSymbol, void **ppvSymbol)
{
    PRTDBGKRNLINFOINT pThis = hKrnlInfo;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    AssertPtrReturn(pszSymbol, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(ppvSymbol, VERR_INVALID_PARAMETER);
    AssertReturn(!pszModule, VERR_MODULE_NOT_FOUND);
    if (g_frtSolInitDone)
        RT_ASSERT_PREEMPTIBLE();

    uintptr_t uValue = kobj_getsymvalue((char *)pszSymbol, 1 /* only kernel */);
    if (ppvSymbol)
        *ppvSymbol = (void *)uValue;
    if (uValue)
        return VINF_SUCCESS;
    return VERR_SYMBOL_NOT_FOUND;
}


RTR0DECL(int) RTR0DbgKrnlInfoQuerySize(RTDBGKRNLINFO hKrnlInfo, const char *pszModule, const char *pszType, size_t *pcbType)
{
    PRTDBGKRNLINFOINT pThis = hKrnlInfo;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    AssertPtrReturn(pszType, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbType, VERR_INVALID_PARAMETER);
    if (g_frtSolInitDone)
        RT_ASSERT_PREEMPTIBLE();

    modctl_t   *pMod = NULL;
    ctf_file_t *pCtf = NULL;
    if (!pszModule)
    {
        pCtf = pThis->pGenUnixCTF;
        pMod = pThis->pGenUnixMod;
    }
    else
    {
        int rc2 = rtR0DbgKrnlInfoModRetainEx(pszModule, &pMod, &pCtf);
        if (RT_FAILURE(rc2))
            return rc2;
        Assert(pMod);
        Assert(pCtf);
    }

    int rc = VERR_NOT_FOUND;
    ctf_id_t TypeIdent = ctf_lookup_by_name(pCtf, pszType);
    if (TypeIdent != CTF_ERR)
    {
        ssize_t cbType = ctf_type_size(pCtf, TypeIdent);
        if (cbType > 0)
        {
            *pcbType = cbType;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_WRONG_TYPE;
    }

    if (pszModule)
        rtR0DbgKrnlInfoModRelease(pMod, pCtf);
    return rc;
}

