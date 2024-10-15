/* $Id: VBoxCpuReportMsrSup.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MsrSup - SupDrv-specific MSR access.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include "VBoxCpuReport.h"
#include <iprt/errcore.h>
#include <iprt/x86.h>


int VbCpuRepMsrProberInitSupDrv(PVBCPUREPMSRACCESSORS pMsrFunctions)
{
    int rc = SUPR3Init(NULL);
    if (RT_SUCCESS(rc))
    {
        /* Test if the MSR prober is available, since the interface is optional. The TSC MSR will exist on any supported CPU. */
        uint64_t uValue;
        bool     fGp;
        rc = SUPR3MsrProberRead(MSR_IA32_TSC, NIL_RTCPUID, &uValue, &fGp);
        if (   rc != VERR_NOT_IMPLEMENTED
            && rc != VERR_INVALID_FUNCTION)
        {
            pMsrFunctions->fAtomic            = true;
            pMsrFunctions->pfnMsrProberRead   = SUPR3MsrProberRead;
            pMsrFunctions->pfnMsrProberWrite  = SUPR3MsrProberWrite;
            pMsrFunctions->pfnMsrProberModify = SUPR3MsrProberModify;

            pMsrFunctions->pfnTerm            = NULL;
            return VINF_SUCCESS;

        }
        vbCpuRepDebug("warning: MSR probing not supported by the support driver (%Rrc).\n", rc);
    }
    else
        vbCpuRepDebug("warning: Unable to initialize the support library (%Rrc).\n", rc);
    return rc;
}

