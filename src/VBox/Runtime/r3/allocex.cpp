/* $Id: allocex.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, Extended Alloc and Free Functions for Ring-3, posix.
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
#define RTMEM_NO_WRAP_TO_EF_APIS
#include <iprt/mem.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include "internal/magics.h"
#include "allocex.h"


RTDECL(int) RTMemAllocExTag(size_t cb, size_t cbAlignment, uint32_t fFlags, const char *pszTag, void **ppv) RT_NO_THROW_DEF
{
    RT_NOREF_PV(pszTag);

    /*
     * Validate and adjust input.
     */
    AssertMsgReturn(!(fFlags & ~RTMEMALLOCEX_FLAGS_VALID_MASK), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(RT_IS_POWER_OF_TWO(cbAlignment), VERR_INVALID_PARAMETER);
    AssertMsgReturn(cbAlignment <= sizeof(void *), ("%zu (%#x)\n", cbAlignment, cbAlignment), VERR_UNSUPPORTED_ALIGNMENT);

    if (fFlags & RTMEMALLOCEX_FLAGS_ANY_CTX)
        return VERR_NOT_SUPPORTED;

    /*
     * Align the request.
     */
    size_t cbAligned = cb;
    if (cbAlignment)
        cbAligned = RT_ALIGN_Z(cb, cbAlignment);
    else
        cbAligned = RT_ALIGN_Z(cb, sizeof(uint64_t));
    AssertMsgReturn(cbAligned >= cb && cbAligned <= ~(size_t)0, ("cbAligned=%#zx cb=%#zx", cbAligned, cb),
                    VERR_INVALID_PARAMETER);

    /*
     * Allocate the requested memory.
     */
    void *pv;
    if (fFlags & (RTMEMALLOCEX_FLAGS_16BIT_REACH | RTMEMALLOCEX_FLAGS_32BIT_REACH))
    {
        int rc;
        if (fFlags & RTMEMALLOCEX_FLAGS_16BIT_REACH)
            rc = rtMemAllocEx16BitReach(cbAligned + sizeof(RTMEMHDRR3), fFlags, &pv);
        else
            rc = rtMemAllocEx32BitReach(cbAligned + sizeof(RTMEMHDRR3), fFlags, &pv);
        if (RT_FAILURE(rc))
            return rc;
    }
    else if (fFlags & RTMEMALLOCEX_FLAGS_EXEC)
    {
        pv = RTMemPageAlloc(cbAligned + sizeof(RTMEMHDRR3));
        if (pv)
        {
            if (fFlags & RTMEMALLOCEX_FLAGS_ZEROED)
                RT_BZERO(pv, cbAligned + sizeof(RTMEMHDRR3));

            int rc = RTMemProtect(pv, cbAligned + sizeof(RTMEMHDRR3), RTMEM_PROT_EXEC | RTMEM_PROT_READ | RTMEM_PROT_WRITE);
            if (RT_FAILURE(rc))
            {
                RTMemPageFree(pv, cbAligned + sizeof(RTMEMHDRR3));
                return rc;
            }
        }
    }
    else if (fFlags & RTMEMALLOCEX_FLAGS_ZEROED)
        pv = RTMemAllocZ(cbAligned + sizeof(RTMEMHDRR3));
    else
        pv = RTMemAlloc(cbAligned + sizeof(RTMEMHDRR3));
    if (!pv)
        return VERR_NO_MEMORY;

    /*
     * Fill in the header and return.
     */
    PRTMEMHDRR3 pHdr = (PRTMEMHDRR3)pv;
    pHdr->u32Magic  = RTMEMHDR_MAGIC;
    pHdr->fFlags    = fFlags;
    pHdr->cb        = (uint32_t)cbAligned;
    pHdr->cbReq     = (uint32_t)cb;

    *ppv = pHdr + 1;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTMemAllocExTag);


RTDECL(void) RTMemFreeEx(void *pv, size_t cb) RT_NO_THROW_DEF
{
    if (!pv)
        return;
    AssertPtr(pv);

    PRTMEMHDRR3 pHdr = (PRTMEMHDRR3)pv - 1;
    AssertMsg(pHdr->u32Magic == RTMEMHDR_MAGIC, ("pHdr->u32Magic=%RX32 pv=%p cb=%#x\n", pHdr->u32Magic, pv, cb));
    pHdr->u32Magic = RTMEMHDR_MAGIC_DEAD;
    Assert(pHdr->cbReq == cb); RT_NOREF_PV(cb);

    if (pHdr->fFlags & (RTMEMALLOCEX_FLAGS_16BIT_REACH | RTMEMALLOCEX_FLAGS_32BIT_REACH))
        rtMemFreeExYyBitReach(pHdr, pHdr->cb + sizeof(*pHdr), pHdr->fFlags);
    else if (pHdr->fFlags & RTMEMALLOCEX_FLAGS_EXEC)
    {
        RTMemProtect(pHdr, pHdr->cb + sizeof(*pHdr), RTMEM_PROT_READ | RTMEM_PROT_WRITE);
        RTMemPageFree(pHdr, pHdr->cb + sizeof(*pHdr));
    }
    else
        RTMemFree(pHdr);
}
RT_EXPORT_SYMBOL(RTMemFreeEx);

