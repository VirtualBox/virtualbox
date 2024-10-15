/* $Id: GIMR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Interface Manager (GIM) - Host Context Ring-0.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_GIM
#include <VBox/vmm/gim.h>
#include "GIMInternal.h"
#include "GIMHvInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/err.h>


/**
 * Does ring-0 per-VM GIM initialization.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR0_INT_DECL(int) GIMR0InitVM(PVMCC pVM)
{
    if (!GIMIsEnabled(pVM))
        return VINF_SUCCESS;

    switch (pVM->gim.s.enmProviderId)
    {
        case GIMPROVIDERID_HYPERV:
            return gimR0HvInitVM(pVM);

        default:
            break;
    }
    return VINF_SUCCESS;
}


/**
 * Does ring-0 per-VM GIM termination.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR0_INT_DECL(int) GIMR0TermVM(PVMCC pVM)
{
    if (!GIMIsEnabled(pVM))
        return VINF_SUCCESS;

    switch (pVM->gim.s.enmProviderId)
    {
        case GIMPROVIDERID_HYPERV:
            return gimR0HvTermVM(pVM);

        default:
            break;
    }
    return VINF_SUCCESS;
}


/**
 * Updates the paravirtualized TSC supported by the GIM provider.
 *
 * @returns VBox status code.
 * @retval VINF_SUCCESS if the paravirt. TSC is setup and in use.
 * @retval VERR_GIM_NOT_ENABLED if no GIM provider is configured for this VM.
 * @retval VERR_GIM_PVTSC_NOT_AVAILABLE if the GIM provider does not support any
 *         paravirt. TSC.
 * @retval VERR_GIM_PVTSC_NOT_IN_USE if the GIM provider supports paravirt. TSC
 *         but the guest isn't currently using it.
 *
 * @param   pVM         The cross context VM structure.
 * @param   u64Offset   The computed TSC offset.
 *
 * @thread EMT(pVCpu)
 */
VMMR0_INT_DECL(int) GIMR0UpdateParavirtTsc(PVMCC pVM, uint64_t u64Offset)
{
    switch (pVM->gim.s.enmProviderId)
    {
        case GIMPROVIDERID_HYPERV:
            return gimR0HvUpdateParavirtTsc(pVM, u64Offset);

        case GIMPROVIDERID_KVM:
            return VINF_SUCCESS;

        case GIMPROVIDERID_NONE:
            return VERR_GIM_NOT_ENABLED;

        default:
            break;
    }
    return VERR_GIM_PVTSC_NOT_AVAILABLE;
}

