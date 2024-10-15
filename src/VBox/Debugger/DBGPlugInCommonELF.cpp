/* $Id: DBGPlugInCommonELF.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGPlugInCommonELF - Common code for dealing with ELF images.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DBGF /// @todo add new log group.
#include "DBGPlugInCommonELF.h"

#include <VBox/vmm/vmmr3vtable.h>
#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/dbg.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct DBGDIGGERELFSEG
{
    /** The segment load address. */
    RTGCPTR         uLoadAddr;
    /** The last address in the segment. */
    RTGCPTR         uLastAddr;
    /** The segment index. */
    RTDBGSEGIDX     iSeg;
} DBGDIGGERELFSEG;
typedef DBGDIGGERELFSEG *PDBGDIGGERELFSEG;


/**
 * Links the segments of the module into the address space.
 *
 * @returns VBox status code on failure.
 *
 * @param   hAs     The address space.
 * @param   hMod    The module.
 * @param   paSegs  Array of segment indexes and load addresses.
 * @param   cSegs   The number of segments in the array.
 */
static int dbgDiggerCommonLinkElfSegs(RTDBGAS hAs, RTDBGMOD hMod, PDBGDIGGERELFSEG paSegs, uint32_t cSegs)
{
    for (uint32_t i = 0; i < cSegs; i++)
        if (paSegs[i].iSeg != NIL_RTDBGSEGIDX)
        {
            int rc = RTDbgAsModuleLinkSeg(hAs, hMod, paSegs[i].iSeg, paSegs[i].uLoadAddr, RTDBGASLINK_FLAGS_REPLACE);
            if (RT_FAILURE(rc))
            {
                RTDbgAsModuleUnlink(hAs, hMod);
                return rc;
            }
        }
    return VINF_SUCCESS;
}


/*
 * Instantiate the code templates for dealing with the two ELF versions.
 */

#define ELF_MODE 32
#include "DBGPlugInCommonELFTmpl.cpp.h"

#undef ELF_MODE
#define ELF_MODE 64
#include "DBGPlugInCommonELFTmpl.cpp.h"

