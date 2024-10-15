/* $Id: IOMR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IOM - Host Context Ring 0.
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
#define LOG_GROUP LOG_GROUP_IOM
#include <VBox/vmm/iom.h>
#include <VBox/vmm/pgm.h>
#include "IOMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/assertcompile.h>
#include <iprt/errcore.h>



/**
 * Initializes the per-VM data for the IOM.
 *
 * This is called from under the GVMM lock, so it should only initialize the
 * data so IOMR0CleanupVM and others will work smoothly.
 *
 * @param   pGVM    Pointer to the global VM structure.
 */
VMMR0_INT_DECL(void) IOMR0InitPerVMData(PGVM pGVM)
{
    AssertCompile(sizeof(pGVM->iom.s) <= sizeof(pGVM->iom.padding));
    AssertCompile(sizeof(pGVM->iomr0.s) <= sizeof(pGVM->iomr0.padding));

    iomR0IoPortInitPerVMData(pGVM);
    iomR0MmioInitPerVMData(pGVM);
}


/**
 * Called during ring-0 init (vmmR0InitVM).
 *
 * @returns VBox status code.
 * @param   pGVM    Pointer to the global VM structure.
 */
VMMR0_INT_DECL(int) IOMR0InitVM(PGVM pGVM)
{
    int rc = PGMR0HandlerPhysicalTypeSetUpContext(pGVM, PGMPHYSHANDLERKIND_MMIO, 0 /*fFlags*/,
                                                  iomMmioHandlerNew, iomMmioPfHandlerNew,
                                                  "MMIO", pGVM->iom.s.hNewMmioHandlerType);
    AssertRCReturn(rc, rc);
    return VINF_SUCCESS;
}


/**
 * Cleans up any loose ends before the GVM structure is destroyed.
 */
VMMR0_INT_DECL(void) IOMR0CleanupVM(PGVM pGVM)
{
    iomR0IoPortCleanupVM(pGVM);
    iomR0MmioCleanupVM(pGVM);
}

