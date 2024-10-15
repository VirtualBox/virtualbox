/* $Id: system-page-size-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemGetPageSize(), RTSystemGetPageShift(), RTSystemGetPageOffsetMask() and RTSystemPageAlignSize(), Posix ring-3.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#include <iprt/system.h>
#include "internal/iprt.h"
#include "internal/system.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>

#include <unistd.h>
#include <errno.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The page size used in bytes. */
static uint32_t  g_cbPage     = 0;
/** The page shift in bits. */
static uint32_t  g_cPageShift = 0;
/** The page offset mask. */
static uintptr_t g_fPageOff   = 0;


DECLHIDDEN(int) rtSystemInitPageSize(void)
{
    long cbPage = sysconf(_SC_PAGESIZE);
    if (cbPage == -1)
        return RTErrConvertFromErrno(errno);

    /* Some sanity check to fend of bogus values (should not happen). */
    AssertReturn(cbPage == _4K || cbPage == _16K || cbPage == _64K,
                 VERR_NOT_SUPPORTED);

    g_cbPage     = (uint32_t)cbPage;
    g_fPageOff   = (uintptr_t)cbPage - 1;
    g_cPageShift = ASMBitFirstSet(&g_cbPage, sizeof(uint32_t) * 8);
    return VINF_SUCCESS;
}


RTDECL(uint32_t) RTSystemGetPageSize(void)
{
    Assert(g_cbPage > 0);
    return g_cbPage;
}


RTDECL(uint32_t) RTSystemGetPageShift(void)
{
    Assert(g_cPageShift > 0);
    return g_cPageShift;
}


RTDECL(uintptr_t) RTSystemGetPageOffsetMask(void)
{
    Assert(g_fPageOff > 0);
    return g_fPageOff;
}


RTDECL(size_t) RTSystemPageAlignSize(size_t cb)
{
    Assert(g_cbPage > 0);
    return RT_ALIGN_Z(cb, g_cbPage);
}
