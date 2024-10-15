/* $Id: RTPathChangeToDosSlashes.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathChangeToDosSlashes
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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


/**
 * Changes all the slashes in the specified path to DOS style.
 *
 * Unless @a fForce is set, nothing will be done when on a UNIX flavored system
 * since paths wont work with DOS style slashes there.
 *
 * @returns @a pszPath.
 * @param   pszPath             The path to modify.
 * @param   fForce              Whether to force the conversion on non-DOS OSes.
 */
RTDECL(char *) RTPathChangeToDosSlashes(char *pszPath, bool fForce)
{
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    RT_NOREF_PV(fForce);
#else
    if (fForce)
#endif
    {
        char    ch;
        char   *psz = pszPath;
        while ((ch = *psz) != '\0')
        {
            if (ch == '/')
                *psz = '\\';
            psz++;
        }
    }
    return pszPath;
}

