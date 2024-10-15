/* $Id: alloc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation.
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
#ifndef RTMEM_NO_WRAP_TO_EF_APIS
# define RTMEM_NO_WRAP_TO_EF_APIS
#endif
#include <iprt/mem.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/string.h>



RTDECL(void *) RTMemDupTag(const void *pvSrc, size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvDst = RTMemAllocTag(cb, pszTag);
    if (pvDst)
        memcpy(pvDst, pvSrc, cb);
    return pvDst;
}
RT_EXPORT_SYMBOL(RTMemDupTag);


RTDECL(void *) RTMemDupExTag(const void *pvSrc, size_t cbSrc, size_t cbExtra, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvDst = RTMemAllocTag(cbSrc + cbExtra, pszTag);
    if (pvDst)
    {
        memcpy(pvDst, pvSrc, cbSrc);
        memset((uint8_t *)pvDst + cbSrc, 0, cbExtra);
    }
    return pvDst;
}
RT_EXPORT_SYMBOL(RTMemDupExTag);


RTDECL(void *)  RTMemReallocZTag(void *pvOld, size_t cbOld, size_t cbNew, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvDst = RTMemReallocTag(pvOld, cbNew, pszTag);
    if (pvDst && cbNew > cbOld)
        memset((uint8_t *)pvDst + cbOld, 0, cbNew - cbOld);
    return pvDst;
}
RT_EXPORT_SYMBOL(RTMemReallocZTag);

