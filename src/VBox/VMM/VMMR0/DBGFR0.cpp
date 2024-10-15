/* $Id: DBGFR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, R0 part.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DBGF
#include "DBGFInternal.h"
#include <VBox/vmm/gvm.h>
#include <VBox/vmm/gvmm.h>
#include <VBox/vmm/vmm.h>

#include <VBox/log.h>
#include <VBox/sup.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/process.h>
#include <iprt/string.h>

#include "dtrace/VBoxVMM.h"



/**
 * Initializes the per-VM data for the DBGF.
 *
 * This is called from under the GVMM lock, so it only need to initialize the
 * data so DBGFR0CleanupVM and others will work smoothly.
 *
 * @param   pGVM    Pointer to the global VM structure.
 */
VMMR0_INT_DECL(void) DBGFR0InitPerVMData(PGVM pGVM)
{
    AssertCompile(sizeof(pGVM->dbgfr0.s) <= sizeof(pGVM->dbgfr0.padding));
    pGVM->dbgfr0.s.pTracerR0 = NULL;

    dbgfR0BpInit(pGVM);
}


/**
 * Cleans up any loose ends before the GVM structure is destroyed.
 */
VMMR0_INT_DECL(void) DBGFR0CleanupVM(PGVM pGVM)
{
#ifdef VBOX_WITH_DBGF_TRACING
    if (pGVM->dbgfr0.s.pTracerR0)
        dbgfR0TracerDestroy(pGVM, pGVM->dbgfr0.s.pTracerR0);
    pGVM->dbgfr0.s.pTracerR0 = NULL;
#endif

    dbgfR0BpDestroy(pGVM);
}

