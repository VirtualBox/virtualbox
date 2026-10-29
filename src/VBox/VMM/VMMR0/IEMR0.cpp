/* $Id: IEMR0.cpp 112403 2026-01-11 19:29:08Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Ring-0.
 */

/*
 * Copyright (C) 2011-2026 Oracle and/or its affiliates.
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
    AssertCompile(sizeof(pGVM->aCpus[0].iemr0.s) <= sizeof(pGVM->aCpus[0].iemr0.padding));

    /*
     * Initialize the ring-0 structures.
     * The target cpu config is copied from ring-3.
     */
    PVMCPUCC const      pVCpu0                   = &pGVM->aCpus[0];
#if IEM_CFG_TARGET_CPU == IEMTARGETCPU_DYNAMIC
    uint8_t const       uTargetCpu               = pVCpu0->iem.s.Core.uTargetCpu;
    AssertLogRelMsgReturn(uTargetCpu <= IEMTARGETCPU_CURRENT, ("uTargetCpu=%#x\n", uTargetCpu), VERR_INVALID_PARAMETER);
#endif
    uint8_t const       aidxTargetCpuEflFlavour0 = pVCpu0->iem.s.Core.aidxTargetCpuEflFlavour[0];
    uint8_t const       aidxTargetCpuEflFlavour1 = pVCpu0->iem.s.Core.aidxTargetCpuEflFlavour[1];
    AssertLogRelMsgReturn(   aidxTargetCpuEflFlavour0 < IEMTARGETCPU_EFL_BEHAVIOR_RESERVED
                          && aidxTargetCpuEflFlavour1 < IEMTARGETCPU_EFL_BEHAVIOR_RESERVED,
                          ("aidxTargetCpuEflFlavour = {%#x, %#x}\n", aidxTargetCpuEflFlavour0, aidxTargetCpuEflFlavour1),
                          VERR_INVALID_PARAMETER);
    CPUMCPUVENDOR const enmCpuVendor             = pVCpu0->iem.s.Core.enmCpuVendor;
    AssertLogRelMsgReturn(enmCpuVendor > CPUMCPUVENDOR_INVALID && enmCpuVendor <= CPUMCPUVENDOR_UNKNOWN,
                          ("enmCpuVendor=%d\n", enmCpuVendor),
                          VERR_INVALID_PARAMETER);

    for (VMCPUID idCpu = 0; idCpu < pGVM->cCpus; idCpu++)
    {
        PVMCPUCC const pVCpu = &pGVM->aCpus[idCpu];
        AssertCompile(sizeof(pVCpu->iem.s) <= sizeof(pVCpu->iem.padding)); /* (tstVMStruct can't do it's job w/o instruction stats) */

        /*
         * Host and guest CPU information.
         */
        ICORE(pVCpu).enmCpuVendor                     = enmCpuVendor;
        ICORE(pVCpu).aidxTargetCpuEflFlavour[0]       = aidxTargetCpuEflFlavour0;
        ICORE(pVCpu).aidxTargetCpuEflFlavour[1]       = aidxTargetCpuEflFlavour1;
#if IEM_CFG_TARGET_CPU == IEMTARGETCPU_DYNAMIC
        ICORE(pVCpu).uTargetCpu                       = uTargetCpu;
#endif

        /*
         * Mark all buffers free.
         */
        uint32_t iMemMap = RT_ELEMENTS(ICORE(pVCpu).aMemMappings);
        while (iMemMap-- > 0)
            ICORE(pVCpu).aMemMappings[iMemMap].fAccess = IEM_ACCESS_INVALID;
    }


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

