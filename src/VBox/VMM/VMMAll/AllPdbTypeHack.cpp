/* $Id: AllPdbTypeHack.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Debug info hack for the VM and VMCPU structures.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/stam.h>
#include "../include/PDMInternal.h"
#include <VBox/vmm/pdm.h>
#include "../include/CFGMInternal.h"
#include "../include/CPUMInternal.h"
#include "../include/MMInternal.h"
#include "../include/PGMInternal.h"
#include "../include/SELMInternal.h"
#include "../include/TRPMInternal.h"
#include "../include/TMInternal.h"
#include "../include/IOMInternal.h"
#ifdef IN_RING3
# include "../include/SSMInternal.h"
#endif
#include "../include/HMInternal.h"
#include "../include/VMMInternal.h"
#include "../include/DBGFInternal.h"
#include "../include/GIMInternal.h"
#include "../include/APICInternal.h"
#include "../include/STAMInternal.h"
#include "../include/VMInternal.h"
#include "../include/EMInternal.h"
#include "../include/IEMInternal.h"
#include "../include/NEMInternal.h"
#include "../VMMR0/GMMR0Internal.h"
#include "../VMMR0/GVMMR0Internal.h"
#include <VBox/vmm/vmcc.h>
#ifdef IN_RING3
# include <VBox/vmm/uvm.h>
#endif
#include <VBox/vmm/gvm.h>


extern "C" {

/* Global pointer variables as an alternative to the parameter list.  Just to ensure the precense of the types. */
PVM                 g_PdbTypeHack1 = NULL;
PVMCPU              g_PdbTypeHack2 = NULL;
PPDMCRITSECT        g_PdbTypeHack3 = NULL;
PPDMCRITSECTRW      g_PdbTypeHack4 = NULL;
PPDMDEVINS          g_PdbTypeHack5 = NULL;
PPDMDRVINS          g_PdbTypeHack6 = NULL;
PPDMUSBINS          g_PdbTypeHack7 = NULL;
PCVMCPU             g_PdbTypeHack8 = NULL;
CTX_SUFF(PVM)       g_PdbTypeHack9 = NULL;
CTX_SUFF(PVMCPU)    g_PdbTypeHack10 = NULL;

DECLEXPORT(uint32_t) PdbTypeHack(PVM pVM, PVMCPU pVCpu, PPDMCRITSECT pCs1, PPDMCRITSECTRW pCs2);
}

DECLEXPORT(uint32_t) PdbTypeHack(PVM pVM, PVMCPU pVCpu, PPDMCRITSECT pCs1, PPDMCRITSECTRW pCs2)
{
    /* Just some dummy operations accessing each type. Probably not necessary, but
       helps making sure we've included all we need to get at the internal stuff.. */
    return pVM->fGlobalForcedActions
         | (pVM == g_PdbTypeHack1)
         | (pVM == g_PdbTypeHack9)
         | pVCpu->fLocalForcedActions
         | (pVCpu == g_PdbTypeHack2)
         | (pVCpu == g_PdbTypeHack8)
         | (pVCpu == g_PdbTypeHack10)
         | pCs1->s.Core.fFlags
         | (pCs1 == g_PdbTypeHack3)
         | pCs2->s.Core.fFlags
         | (pCs2 == g_PdbTypeHack4)
         | g_PdbTypeHack5->Internal.s.idxR0Device
         | (g_PdbTypeHack5 != NULL)
         | (uint32_t)g_PdbTypeHack6->Internal.s.fDetaching
         | (g_PdbTypeHack6 != NULL)
         | (uint32_t)g_PdbTypeHack7->Internal.s.fVMSuspended
         | (g_PdbTypeHack7 != NULL);
}

