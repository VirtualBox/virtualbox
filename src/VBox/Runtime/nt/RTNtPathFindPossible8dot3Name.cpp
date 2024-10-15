/* $Id: RTNtPathFindPossible8dot3Name.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Native NT, RTNtPathFindPossible8dot3Name.
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
#define LOG_GROUP RTLOGGROUP_FS
#ifdef IN_SUP_HARDENED_R3
# include <iprt/nt/nt-and-windows.h>
#else
# include <iprt/nt/nt.h>
#endif



/**
 * Checks whether the path could be containing alternative 8.3 names generated
 * by NTFS, FAT, or other similar file systems.
 *
 * @returns Pointer to the first component that might be an 8.3 name, NULL if
 *          not 8.3 path.
 * @param   pwszPath        The path to check.
 *
 * @remarks This is making bad ASSUMPTION wrt to the naming scheme of 8.3 names,
 *          however, non-tilde 8.3 aliases are probably rare enough to not be
 *          worth all the extra code necessary to open each path component and
 *          check if we've got the short name or not.
 */
RTDECL(PRTUTF16) RTNtPathFindPossible8dot3Name(PCRTUTF16 pwszPath)
{
    PCRTUTF16 pwszName = pwszPath;
    for (;;)
    {
        RTUTF16 wc = *pwszPath++;
        if (wc == '~')
        {
            /* Could check more here before jumping to conclusions... */
            if (pwszPath - pwszName <= 8+1+3)
                return (PRTUTF16)pwszName;
        }
        else if (wc == '\\' || wc == '/' || wc == ':')
            pwszName = pwszPath;
        else if (wc == 0)
            break;
    }
    return NULL;
}

