/* $Id: LoadVBoxDDU.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Runtime - Try Load VBoxDDU to get VFS chain providers from storage.
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
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/errcore.h>
#include <iprt/path.h>
#include <iprt/string.h>


/**
 * Class used for registering a VFS chain element provider.
 */
class LoadVBoxDDU
{
private:
    /** The VBoxDDU handle. */
    RTLDRMOD g_hLdrMod;

public:
    LoadVBoxDDU(void) : g_hLdrMod(NIL_RTLDRMOD)
    {
        int rc = RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);
        if (RT_SUCCESS(rc))
        {
            char szPath[RTPATH_MAX];

            /* Try private arch dir first. */
            rc = RTPathAppPrivateArch(szPath, sizeof(szPath));
            if (RT_SUCCESS(rc))
                rc = RTPathAppend(szPath, sizeof(szPath), "VBoxDDU");
            if (RT_SUCCESS(rc))
                rc = RTStrCat(szPath, sizeof(szPath), RTLdrGetSuff());
            if (RT_SUCCESS(rc))
            {
                rc = RTLdrLoad(szPath, &g_hLdrMod);
                if (RT_SUCCESS(rc))
                    return;
            }

            /* Try shared libs dir next. */
            rc = RTPathSharedLibs(szPath, sizeof(szPath));
            if (RT_SUCCESS(rc))
                rc = RTPathAppend(szPath, sizeof(szPath), "VBoxDDU");
            if (RT_SUCCESS(rc))
                rc = RTStrCat(szPath, sizeof(szPath), RTLdrGetSuff());
            if (RT_SUCCESS(rc))
            {
                rc = RTLdrLoad(szPath, &g_hLdrMod);
                if (RT_SUCCESS(rc))
                    return;
            }

            /* Try exec dir after that. */
            rc = RTPathExecDir(szPath, sizeof(szPath));
            if (RT_SUCCESS(rc))
                rc = RTPathAppend(szPath, sizeof(szPath), "VBoxDDU");
            if (RT_SUCCESS(rc))
                rc = RTStrCat(szPath, sizeof(szPath), RTLdrGetSuff());
            if (RT_SUCCESS(rc))
            {
                rc = RTLdrLoad(szPath, &g_hLdrMod);
                if (RT_SUCCESS(rc))
                    return;
            }

            /* Try exec dir parent after that. */
            rc = RTPathExecDir(szPath, sizeof(szPath));
            if (RT_SUCCESS(rc))
                rc = RTPathAppend(szPath, sizeof(szPath), ".." RTPATH_SLASH_STR "VBoxDDU");
            if (RT_SUCCESS(rc))
                rc = RTStrCat(szPath, sizeof(szPath), RTLdrGetSuff());
            if (RT_SUCCESS(rc))
            {
                rc = RTLdrLoad(szPath, &g_hLdrMod);
                if (RT_SUCCESS(rc))
                    return;
            }

            /* Didn't work out, don't sweat it. */
            g_hLdrMod = NIL_RTLDRMOD;
        }
    }

    ~LoadVBoxDDU()
    {
        if (g_hLdrMod != NIL_RTLDRMOD)
        {
            RTLdrClose(g_hLdrMod);
            g_hLdrMod = NIL_RTLDRMOD;
        }
    }

    static LoadVBoxDDU s_LoadVBoxDDU;
};

/* static*/ LoadVBoxDDU LoadVBoxDDU::s_LoadVBoxDDU;

