/* $Id: tstLdrLoad.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Native Loader.
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


#include <iprt/ldr.h>
#include <iprt/stream.h>
#include <iprt/initterm.h>
#include <iprt/errcore.h>

int main(int argc, char **argv)
{
    int rcRet = 0;
    RTR3InitExe(argc, &argv, 0);

    /*
     * If no args, display usage.
     */
    if (argc <= 1)
    {
        RTPrintf("Syntax: %s [so/dll [so/dll [..]]\n", argv[0]);
        return 1;
    }

    /*
     * Iterate the arguments and treat all of them as so/dll paths.
     */
    for (int i = 1; i < argc; i++)
    {
        RTLDRMOD hLdrMod = (RTLDRMOD)(uintptr_t)0xbaadffaa;
        int rc = RTLdrLoad(argv[i], &hLdrMod);
        if (RT_SUCCESS(rc))
        {
            RTPrintf("tstLdrLoad: %d - %s\n", i, argv[i]);
            rc = RTLdrClose(hLdrMod);
            if (RT_FAILURE(rc))
            {
                RTPrintf("tstLdrLoad: rc=%Rrc RTLdrClose()\n", rc);
                rcRet++;
            }
        }
        else
        {
            RTPrintf("tstLdrLoad: rc=%Rrc RTLdrOpen('%s')\n", rc, argv[i]);
            rcRet++;
        }
    }

    /*
     * Summary.
     */
    if (!rcRet)
        RTPrintf("tstLdrLoad: SUCCESS\n");
    else
        RTPrintf("tstLdrLoad: FAILURE - %d errors\n", rcRet);

    return !!rcRet;
}
