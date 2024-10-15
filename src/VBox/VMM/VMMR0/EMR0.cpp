/* $Id: EMR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EM - Host Context Ring 0.
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
#define LOG_GROUP LOG_GROUP_EM
#include <VBox/vmm/em.h>
#include "EMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/gvm.h>
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/thread.h>



/**
 * Adjusts EM configuration options.
 *
 * @returns VBox status code.
 * @param   pGVM            The ring-0 VM structure.
 */
VMMR0_INT_DECL(int) EMR0InitVM(PGVM pGVM)
{
    /*
     * Override ring-0 exit optimizations settings.
     */
    PVMCPUCC pVCpu0 = &pGVM->aCpus[0];
    bool fEnabledR0                = pVCpu0->em.s.fExitOptimizationEnabled
                                  && pVCpu0->em.s.fExitOptimizationEnabledR0
                                  && (RTThreadPreemptIsPossible() || RTThreadPreemptIsPendingTrusty());
    bool fEnabledR0PreemptDisabled = fEnabledR0
                                  && pVCpu0->em.s.fExitOptimizationEnabledR0PreemptDisabled
                                  && RTThreadPreemptIsPendingTrusty();
    for (VMCPUID idCpu = 0; idCpu < pGVM->cCpus; idCpu++)
    {
        PVMCPUCC pVCpu = &pGVM->aCpus[idCpu];
        pVCpu->em.s.fExitOptimizationEnabledR0                = fEnabledR0;
        pVCpu->em.s.fExitOptimizationEnabledR0PreemptDisabled = fEnabledR0PreemptDisabled;
    }

    return VINF_SUCCESS;
}

