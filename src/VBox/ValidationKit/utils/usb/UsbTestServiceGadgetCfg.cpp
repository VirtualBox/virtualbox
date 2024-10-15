/* $Id: UsbTestServiceGadgetCfg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestServ - Remote USB test configuration and execution server, USB gadget Cfg API.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "UsbTestServiceGadget.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Returns the gadget configuration item matching the given key.
 *
 * @returns Pointer to the configuration item on success or NULL if not found.
 * @param   paCfg             The configuration item array.
 * @param   pszKey            The key to look for.
 */
static PCUTSGADGETCFGITEM utsGadgetCfgGetItemFromKey(PCUTSGADGETCFGITEM paCfg, const char *pszKey)
{
    while (   paCfg
           && paCfg->pszKey)
    {
        if (!RTStrCmp(paCfg->pszKey, pszKey))
            return paCfg;

        paCfg++;
    }
    return NULL;
}



DECLHIDDEN(int) utsGadgetCfgQueryBool(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                      bool *pf)
{
    int rc = VERR_NOT_FOUND;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (pCfgItem)
    {
        if (pCfgItem->Val.enmType == UTSGADGETCFGTYPE_BOOLEAN)
        {
            *pf = pCfgItem->Val.u.f;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryBoolDef(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                         bool *pf, bool fDef)
{
    int rc = VERR_INVALID_PARAMETER;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (   !pCfgItem
        || pCfgItem->Val.enmType == UTSGADGETCFGTYPE_BOOLEAN)
    {
        *pf = pCfgItem ? pCfgItem->Val.u.f : fDef;
        rc = VINF_SUCCESS;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryString(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        char **ppszVal)
{
    int rc = VERR_NOT_FOUND;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (pCfgItem)
    {
        if (pCfgItem->Val.enmType == UTSGADGETCFGTYPE_STRING)
        {
            *ppszVal = RTStrDup(pCfgItem->Val.u.psz);
            if (*ppszVal)
                rc = VINF_SUCCESS;
            else
                rc = VERR_NO_STR_MEMORY;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryStringDef(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                           char **ppszVal, const char *pszDef)
{
    int rc = VERR_NOT_FOUND;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (   !pCfgItem
        || pCfgItem->Val.enmType == UTSGADGETCFGTYPE_STRING)
    {
        *ppszVal = RTStrDup(pCfgItem ? pCfgItem->Val.u.psz : pszDef);
        if (*ppszVal)
            rc = VINF_SUCCESS;
        else
            rc = VERR_NO_STR_MEMORY;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryU8(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                    uint8_t *pu8)
{
    int rc = VERR_NOT_FOUND;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (pCfgItem)
    {
        if (pCfgItem->Val.enmType == UTSGADGETCFGTYPE_UINT8)
        {
            *pu8 = pCfgItem->Val.u.u8;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryU8Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                       uint8_t *pu8, uint8_t u8Def)
{
    int rc = VERR_INVALID_PARAMETER;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (   !pCfgItem
        || pCfgItem->Val.enmType == UTSGADGETCFGTYPE_UINT8)
    {
        *pu8 = pCfgItem ? pCfgItem->Val.u.u8 : u8Def;
        rc = VINF_SUCCESS;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryU16(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint16_t *pu16)
{
    int rc = VERR_NOT_FOUND;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (pCfgItem)
    {
        if (pCfgItem->Val.enmType == UTSGADGETCFGTYPE_UINT16)
        {
            *pu16 = pCfgItem->Val.u.u16;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryU16Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint16_t *pu16, uint16_t u16Def)
{
    int rc = VERR_INVALID_PARAMETER;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (   !pCfgItem
        || pCfgItem->Val.enmType == UTSGADGETCFGTYPE_UINT16)
    {
        *pu16 = pCfgItem ? pCfgItem->Val.u.u16 : u16Def;
        rc = VINF_SUCCESS;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryU32(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint32_t *pu32)
{
    int rc = VERR_NOT_FOUND;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (pCfgItem)
    {
        if (pCfgItem->Val.enmType == UTSGADGETCFGTYPE_UINT32)
        {
            *pu32 = pCfgItem->Val.u.u32;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryU32Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint32_t *pu32, uint32_t u32Def)
{
    int rc = VERR_INVALID_PARAMETER;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (   !pCfgItem
        || pCfgItem->Val.enmType == UTSGADGETCFGTYPE_UINT32)
    {
        *pu32 = pCfgItem ? pCfgItem->Val.u.u32 : u32Def;
        rc = VINF_SUCCESS;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryU64(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint64_t *pu64)
{
    int rc = VERR_NOT_FOUND;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (pCfgItem)
    {
        if (pCfgItem->Val.enmType == UTSGADGETCFGTYPE_UINT64)
        {
            *pu64 = pCfgItem->Val.u.u64;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryU64Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint64_t *pu64, uint64_t u64Def)
{
    int rc = VERR_INVALID_PARAMETER;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (   !pCfgItem
        || pCfgItem->Val.enmType == UTSGADGETCFGTYPE_UINT64)
    {
        *pu64 = pCfgItem ? pCfgItem->Val.u.u64 : u64Def;
        rc = VINF_SUCCESS;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryS8(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                    int8_t *pi8)
{
    int rc = VERR_NOT_FOUND;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (pCfgItem)
    {
        if (pCfgItem->Val.enmType == UTSGADGETCFGTYPE_INT8)
        {
            *pi8 = pCfgItem->Val.u.i8;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryS8Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                       int8_t *pi8, uint8_t i8Def)
{
    int rc = VERR_INVALID_PARAMETER;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (   !pCfgItem
        || pCfgItem->Val.enmType == UTSGADGETCFGTYPE_INT8)
    {
        *pi8 = pCfgItem ? pCfgItem->Val.u.i8 : i8Def;
        rc = VINF_SUCCESS;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryS16(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint16_t *pi16)
{
    int rc = VERR_NOT_FOUND;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (pCfgItem)
    {
        if (pCfgItem->Val.enmType == UTSGADGETCFGTYPE_INT16)
        {
            *pi16 = pCfgItem->Val.u.i16;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryS16Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint16_t *pi16, uint16_t i16Def)
{
    int rc = VERR_INVALID_PARAMETER;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (   !pCfgItem
        || pCfgItem->Val.enmType == UTSGADGETCFGTYPE_INT16)
    {
        *pi16 = pCfgItem ? pCfgItem->Val.u.i16 : i16Def;
        rc = VINF_SUCCESS;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryS32(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint32_t *pi32)
{
    int rc = VERR_NOT_FOUND;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (pCfgItem)
    {
        if (pCfgItem->Val.enmType == UTSGADGETCFGTYPE_INT32)
        {
            *pi32 = pCfgItem->Val.u.i32;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryS32Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint32_t *pi32, uint32_t i32Def)
{
    int rc = VERR_INVALID_PARAMETER;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (   !pCfgItem
        || pCfgItem->Val.enmType == UTSGADGETCFGTYPE_INT32)
    {
        *pi32 = pCfgItem ? pCfgItem->Val.u.i32 : i32Def;
        rc = VINF_SUCCESS;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryS64(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint64_t *pi64)
{
    int rc = VERR_NOT_FOUND;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (pCfgItem)
    {
        if (pCfgItem->Val.enmType == UTSGADGETCFGTYPE_INT64)
        {
            *pi64 = pCfgItem->Val.u.i64;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}


DECLHIDDEN(int) utsGadgetCfgQueryS64Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint64_t *pi64, uint64_t i64Def)
{
    int rc = VERR_INVALID_PARAMETER;
    PCUTSGADGETCFGITEM pCfgItem = utsGadgetCfgGetItemFromKey(paCfg, pszKey);

    if (   !pCfgItem
        || pCfgItem->Val.enmType == UTSGADGETCFGTYPE_INT64)
    {
        *pi64 = pCfgItem ? pCfgItem->Val.u.i64 : i64Def;
        rc = VINF_SUCCESS;
    }

    return rc;
}

