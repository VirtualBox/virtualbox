/* $Id: IEMR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Ring-0.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP   LOG_GROUP_IEM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pgm.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <iprt/errcore.h>



VMMR0_INT_DECL(int) IEMR0InitVM(PGVM pGVM)
{
    AssertCompile(sizeof(pGVM->iem.s) <= sizeof(pGVM->iem.padding));
    AssertCompile(sizeof(pGVM->aCpus[0].iem.s) <= sizeof(pGVM->aCpus[0].iem.padding));

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /*
     * Register the per-VM VMX APIC-access page handler type.
     */
    if (pGVM->cpum.ro.GuestFeatures.fVmx)
    {
        int rc = PGMR0HandlerPhysicalTypeSetUpContext(pGVM, PGMPHYSHANDLERKIND_ALL, PGMPHYSHANDLER_F_NOT_IN_HM,
                                                      iemVmxApicAccessPageHandler, iemVmxApicAccessPagePfHandler,
                                                      "VMX APIC-access page", pGVM->iem.s.hVmxApicAccessPage);
        AssertLogRelRCReturn(rc, rc);
    }
#endif
    return VINF_SUCCESS;
}

