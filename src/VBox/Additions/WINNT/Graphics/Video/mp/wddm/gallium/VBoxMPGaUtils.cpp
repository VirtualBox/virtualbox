/* $Id: VBoxMPGaUtils.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface for WDDM kernel mode driver.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "VBoxMPGaUtils.h"

#include <iprt/asm.h>
#include <iprt/assert.h>

volatile uint32_t g_fu32GaLogControl =
      GALOG_GROUP_RELEASE
#ifdef DEBUG
    | GALOG_GROUP_TEST
//    | GALOG_GROUP_HOSTOBJECTS
//    | GALOG_GROUP_PRESENT
//    | GALOG_GROUP_DXGK
//    | GALOG_GROUP_SVGA
//    | GALOG_GROUP_SVGA_FIFO
#endif
    ;

/*
 * Helpers.
 */

void *GaMemAlloc(uint32_t cbSize)
{
    return ExAllocatePoolWithTag(NonPagedPool, cbSize, 'AGBV');
}

void *GaMemAllocZero(uint32_t cbSize)
{
    void *pvMem = GaMemAlloc(cbSize);
    if (pvMem)
        memset(pvMem, 0, cbSize);
    return pvMem;
}

void GaMemFree(void *pvMem)
{
    ExFreePool(pvMem);
}

NTSTATUS GaIdAlloc(uint32_t *pu32Bits,
                   uint32_t cbBits,
                   uint32_t u32Limit,
                   uint32_t *pu32Id)
{
    /* Find the first zero bit. */
    const int32_t i32Id = ASMBitFirstClear(pu32Bits, cbBits * 8);
    if (0 <= i32Id && i32Id < (int32_t)u32Limit)
    {
        ASMBitSet(pu32Bits, i32Id);
        *pu32Id = (uint32_t)i32Id;
        return STATUS_SUCCESS;
    }

    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS GaIdFree(uint32_t *pu32Bits,
                  uint32_t cbBits,
                  uint32_t u32Limit,
                  uint32_t u32Id)
{
    AssertReturn(u32Limit <= cbBits * 8, STATUS_INVALID_PARAMETER);
    AssertReturn(u32Id < u32Limit, STATUS_INVALID_PARAMETER);

    /* Clear the corresponding bit. */
    ASMBitClear(pu32Bits, (int32_t)u32Id);

    return STATUS_SUCCESS;
}
