/* $Id: RTArchValToString.cpp 107123 2024-11-22 01:44:29Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - RTArchValToString.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <iprt/arch.h>
#include "internal/iprt.h"


RTDECL(const char *) RTArchValToString(uint32_t uArchVal)
{
    switch (uArchVal)
    {
        case RT_ARCH_VAL_X86_16:    return "16-bit x86";
        case RT_ARCH_VAL_X86:       return "x86";
        case RT_ARCH_VAL_AMD64:     return "AMD64";
        case RT_ARCH_VAL_ARM32:     return "ARM32";
        case RT_ARCH_VAL_ARM64:     return "ARM64";
        case RT_ARCH_VAL_SPARC32:   return "SPARC32";
        case RT_ARCH_VAL_SPARC64:   return "SPARC64";
        case 0:                     return "unknown/zero";
    }
    return "unknown";
}

