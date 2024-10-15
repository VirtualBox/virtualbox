/* $Id: path.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Path Manipulation.
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
#include "internal/iprt.h"
#include <iprt/path.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include "internal/path.h"
#include "internal/process.h"


#ifdef RT_OS_SOLARIS
/**
 * Hack to strip of the architecture subdirectory from the exec dir.
 *
 * @returns See RTPathExecDir.
 * @param   pszPath             See RTPathExecDir.
 * @param   cchPath             See RTPathExecDir.
 */
DECLINLINE(int) rtPathSolarisArchHack(char *pszPath, size_t cchPath)
{
    int rc = RTPathExecDir(pszPath, cchPath);
    if (RT_SUCCESS(rc))
    {
        const char *pszLast = RTPathFilename(pszPath);
        if (   !strcmp(pszLast, "amd64")
            || !strcmp(pszLast, "i386"))
            RTPathStripFilename(pszPath);
    }
    return rc;
}
#endif


RTDECL(int) RTPathExecDir(char *pszPath, size_t cchPath)
{
    AssertReturn(g_szrtProcExePath[0], VERR_WRONG_ORDER);

    /*
     * Calc the length and check if there is space before copying.
     */
    size_t cch = g_cchrtProcExeDir;
    if (cch < cchPath)
    {
        memcpy(pszPath, g_szrtProcExePath, cch);
        pszPath[cch] = '\0';
        return VINF_SUCCESS;
    }

    AssertMsgFailed(("Buffer too small (%zu <= %zu)\n", cchPath, cch));
    return VERR_BUFFER_OVERFLOW;
}


RTDECL(int) RTPathAppPrivateNoArch(char *pszPath, size_t cchPath)
{
#if !defined(RT_OS_WINDOWS) && defined(RTPATH_APP_PRIVATE)
    return RTStrCopy(pszPath, cchPath, RTPATH_APP_PRIVATE);
#elif defined(RT_OS_SOLARIS)
    return rtPathSolarisArchHack(pszPath, cchPath);
#else
    return RTPathExecDir(pszPath, cchPath);
#endif
}


RTDECL(int) RTPathAppPrivateArch(char *pszPath, size_t cchPath)
{
#if !defined(RT_OS_WINDOWS) && defined(RTPATH_APP_PRIVATE_ARCH)
    return RTStrCopy(pszPath, cchPath, RTPATH_APP_PRIVATE_ARCH);
#else
    return RTPathExecDir(pszPath, cchPath);
#endif
}


RTDECL(int) RTPathAppPrivateArchTop(char *pszPath, size_t cchPath)
{
#if !defined(RT_OS_WINDOWS) && defined(RTPATH_APP_PRIVATE_ARCH_TOP)
    return RTStrCopy(pszPath, cchPath, RTPATH_APP_PRIVATE_ARCH_TOP);
#elif !defined(RT_OS_WINDOWS) && defined(RTPATH_APP_PRIVATE_ARCH)
    return RTStrCopy(pszPath, cchPath, RTPATH_APP_PRIVATE_ARCH);
#elif defined(RT_OS_SOLARIS)
    return rtPathSolarisArchHack(pszPath, cchPath);
#else
    int rc = RTPathExecDir(pszPath, cchPath);
    return rc;
#endif
}


RTDECL(int) RTPathSharedLibs(char *pszPath, size_t cchPath)
{
#if !defined(RT_OS_WINDOWS) && defined(RTPATH_SHARED_LIBS)
    return RTStrCopy(pszPath, cchPath, RTPATH_SHARED_LIBS);
#else
    return RTPathExecDir(pszPath, cchPath);
#endif
}


RTDECL(int) RTPathAppDocs(char *pszPath, size_t cchPath)
{
#if !defined(RT_OS_WINDOWS) && defined(RTPATH_APP_DOCS)
    return RTStrCopy(pszPath, cchPath, RTPATH_APP_DOCS);
#elif defined(RT_OS_SOLARIS)
    return rtPathSolarisArchHack(pszPath, cchPath);
#else
    return RTPathExecDir(pszPath, cchPath);
#endif
}


RTR3DECL(int) RTPathGetMode(const char *pszPath, PRTFMODE pfMode)
{
    AssertPtrReturn(pfMode, VERR_INVALID_POINTER);

    RTFSOBJINFO ObjInfo;
    int rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
    if (RT_SUCCESS(rc))
        *pfMode = ObjInfo.Attr.fMode;

    return rc;
}
