/* $Id: RTDirExists-generic.cpp 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - RTDirExists, generic implementation.
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
#define LOG_GROUP RTLOGGROUP_DIR
#include "internal/iprt.h"

#include <iprt/dir.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/path.h>


RTDECL(bool) RTDirExists(const char *pszPath)
{
    RTFSOBJINFO ObjInfo;
    int rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
    bool fRc = RT_SUCCESS(rc)
            && RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode);
    LogFlow(("RTDirExists(%p:{%s}): returns %RTbool (%Rrc)\n", pszPath, pszPath, fRc, rc));
    return fRc;
}

