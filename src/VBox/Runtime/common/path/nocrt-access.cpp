/* $Id: nocrt-access.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - access().
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#define IPRT_NO_CRT_FOR_3RD_PARTY
#include "internal/nocrt.h"
#include <iprt/nocrt/unistd.h>
#include <iprt/nocrt/errno.h>
#include <iprt/errcore.h>
#include <iprt/path.h>


#undef access
int RT_NOCRT(access)(const char *pszPath, int fFlags) RT_NOEXCEPT
{
    RTFSOBJINFO ObjInfo;
    int rc = RTPathQueryInfo(pszPath, &ObjInfo, RTFSOBJATTRADD_UNIX);
    if (RT_SUCCESS(rc))
    {
        if (fFlags == F_OK)
            return 0;
        rc = 0;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
        /** @todo A proper answer on windows would require reading the security
         *        attributes and stuff. Faking it for now. */
        if ((fFlags & W_OK) && (ObjInfo.Attr.fMode & RTFS_DOS_READONLY) && !RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
            rc = -1;
        else if ((fFlags & X_OK) && !(ObjInfo.Attr.fMode & (RTFS_UNIX_IXOTH | RTFS_UNIX_IXGRP | RTFS_UNIX_IXUSR)))
            rc = -1;
#else
# error "port me"
#endif
        if (!rc)
            return rc;
        errno = EACCES;
    }
    else
        errno = RTErrConvertToErrno(rc);
    return -1;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(access);

