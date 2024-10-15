/* $Id: DBGPlugInDiggers.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DbfPlugInDiggers - Debugger and Guest OS Digger Plug-in.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGC
#include <VBox/dbg.h>
#include <VBox/vmm/vmmr3vtable.h>
#include "DBGPlugIns.h"
#include <VBox/version.h>
#include <iprt/errcore.h>


DECLEXPORT(int) DbgPlugInEntry(DBGFPLUGINOP enmOperation, PUVM pUVM, PCVMMR3VTABLE pVMM, uintptr_t uArg)
{
    static PCDBGFOSREG s_aPlugIns[] =
    {
        &g_DBGDiggerDarwin,
        &g_DBGDiggerFreeBsd,
        &g_DBGDiggerLinux,
        &g_DBGDiggerOS2,
        &g_DBGDiggerSolaris,
        &g_DBGDiggerWinNt
    };

    switch (enmOperation)
    {
        case DBGFPLUGINOP_INIT:
        {
            if (uArg != VBOX_VERSION)
                return VERR_VERSION_MISMATCH;

            for (unsigned i = 0; i < RT_ELEMENTS(s_aPlugIns); i++)
            {
                int rc = pVMM->pfnDBGFR3OSRegister(pUVM, s_aPlugIns[i]);
                if (RT_FAILURE(rc))
                {
                    AssertRC(rc);
                    while (i-- > 0)
                        pVMM->pfnDBGFR3OSDeregister(pUVM, s_aPlugIns[i]);
                    return rc;
                }
            }
            return VINF_SUCCESS;
        }

        case DBGFPLUGINOP_TERM:
        {
            for (unsigned i = 0; i < RT_ELEMENTS(s_aPlugIns); i++)
            {
                int rc = pVMM->pfnDBGFR3OSDeregister(pUVM, s_aPlugIns[i]);
                AssertRC(rc);
            }
            return VINF_SUCCESS;
        }

        default:
            return VERR_NOT_SUPPORTED;
    }
}

