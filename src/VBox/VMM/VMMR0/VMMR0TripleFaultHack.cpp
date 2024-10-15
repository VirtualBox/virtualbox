/* $Id: VMMR0TripleFaultHack.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM - Host Context Ring 0, Triple Fault Debugging Hack.
 *
 * Only use this when desperate.  May not work on all systems, esp. newer ones,
 * since it require BIOS support for the warm reset vector at 0467h.
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
#define LOG_GROUP LOG_GROUP_VMM
#include <VBox/vmm/vmm.h>
#include "VMMInternal.h"
#include <VBox/param.h>

#include <iprt/asm-amd64-x86.h>
#include <iprt/assert.h>
#include <iprt/memobj.h>
#include <iprt/mem.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTR0MEMOBJ   g_hMemPage0;
static RTR0MEMOBJ   g_hMapPage0;
static uint8_t     *g_pbPage0;

static RTR0MEMOBJ   g_hMemLowCore;
static RTR0MEMOBJ   g_hMapLowCore;
static uint8_t     *g_pbLowCore;
static RTHCPHYS     g_HCPhysLowCore;

/** @name For restoring memory we've overwritten.
 * @{ */
static uint32_t     g_u32SavedVector;
static uint16_t     g_u16SavedCadIndicator;
static void        *g_pvSavedLowCore;
/** @}  */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/* VMMR0TripleFaultHackA.asm */
DECLASM(void) vmmR0TripleFaultHackStart(void);
DECLASM(void) vmmR0TripleFaultHackEnd(void);
DECLASM(void) vmmR0TripleFaultHackTripleFault(void);


/**
 * Initalizes the triple fault / boot hack.
 *
 * Always call vmmR0TripleFaultHackTerm to clean up, even when this call fails.
 *
 * @returns VBox status code.
 */
int vmmR0TripleFaultHackInit(void)
{
    /*
     * Map the first page.
     */
    int rc = RTR0MemObjEnterPhys(&g_hMemPage0, 0, HOST_PAGE_SIZE, RTMEM_CACHE_POLICY_DONT_CARE);
    AssertRCReturn(rc, rc);
    rc = RTR0MemObjMapKernel(&g_hMapPage0, g_hMemPage0, (void *)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
    AssertRCReturn(rc, rc);
    g_pbPage0 = (uint8_t *)RTR0MemObjAddress(g_hMapPage0);
    LogRel(("0040:0067 = %04x:%04x\n", RT_MAKE_U16(g_pbPage0[0x467+2],  g_pbPage0[0x467+3]),  RT_MAKE_U16(g_pbPage0[0x467+0],  g_pbPage0[0x467+1]) ));

    /*
     * Allocate some "low core" memory.  If that fails, just grab some memory.
     */
    //rc = RTR0MemObjAllocPhys(&g_hMemLowCore, HOST_PAGE_SIZE, _1M - 1);
    //__debugbreak();
    rc = RTR0MemObjEnterPhys(&g_hMemLowCore, 0x7000, HOST_PAGE_SIZE, RTMEM_CACHE_POLICY_DONT_CARE);
    AssertRCReturn(rc, rc);
    rc = RTR0MemObjMapKernel(&g_hMapLowCore, g_hMemLowCore, (void *)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
    AssertRCReturn(rc, rc);
    g_pbLowCore = (uint8_t *)RTR0MemObjAddress(g_hMapLowCore);
    g_HCPhysLowCore = RTR0MemObjGetPagePhysAddr(g_hMapLowCore, 0);
    LogRel(("Low core at %RHp mapped at %p\n", g_HCPhysLowCore, g_pbLowCore));

    /*
     * Save memory we'll be overwriting.
     */
    g_pvSavedLowCore = RTMemAlloc(HOST_PAGE_SIZE);
    AssertReturn(g_pvSavedLowCore, VERR_NO_MEMORY);
    memcpy(g_pvSavedLowCore, g_pbLowCore, HOST_PAGE_SIZE);

    g_u32SavedVector = RT_MAKE_U32_FROM_U8(g_pbPage0[0x467], g_pbPage0[0x467+1], g_pbPage0[0x467+2], g_pbPage0[0x467+3]);
    g_u16SavedCadIndicator = RT_MAKE_U16(g_pbPage0[0x472], g_pbPage0[0x472+1]);

    /*
     * Install the code.
     */
    size_t cbCode = (uintptr_t)&vmmR0TripleFaultHackEnd - (uintptr_t)&vmmR0TripleFaultHackStart;
    AssertLogRelReturn(cbCode <= HOST_PAGE_SIZE, VERR_OUT_OF_RANGE);
    memcpy(g_pbLowCore, &vmmR0TripleFaultHackStart, cbCode);

    g_pbPage0[0x467+0] = 0x00;
    g_pbPage0[0x467+1] = 0x70;
    g_pbPage0[0x467+2] = 0x00;
    g_pbPage0[0x467+3] = 0x00;

    g_pbPage0[0x472+0] = 0x34;
    g_pbPage0[0x472+1] = 0x12;

    /*
     * Configure the status port and cmos shutdown command.
     */
    uint32_t fSaved = ASMIntDisableFlags();

    ASMOutU8(0x70, 0x0f);
    ASMOutU8(0x71, 0x0a);

    ASMOutU8(0x70, 0x05);
    ASMInU8(0x71);

    ASMReloadCR3();
    ASMWriteBackAndInvalidateCaches();

    ASMSetFlags(fSaved);

#if 1 /* For testing & debugging. */
    vmmR0TripleFaultHackTripleFault();
#endif

    return VINF_SUCCESS;
}


/**
 * Try undo the harm done by the init function.
 *
 * This may leave the system in an unstable state since we might have been
 * hijacking memory below 1MB that is in use by the kernel.
 */
void vmmR0TripleFaultHackTerm(void)
{
    /*
     * Restore overwritten memory.
     */
    if (   g_pvSavedLowCore
        && g_pbLowCore)
        memcpy(g_pbLowCore, g_pvSavedLowCore, HOST_PAGE_SIZE);

    if (g_pbPage0)
    {
        g_pbPage0[0x467+0] = RT_BYTE1(g_u32SavedVector);
        g_pbPage0[0x467+1] = RT_BYTE2(g_u32SavedVector);
        g_pbPage0[0x467+2] = RT_BYTE3(g_u32SavedVector);
        g_pbPage0[0x467+3] = RT_BYTE4(g_u32SavedVector);

        g_pbPage0[0x472+0] = RT_BYTE1(g_u16SavedCadIndicator);
        g_pbPage0[0x472+1] = RT_BYTE2(g_u16SavedCadIndicator);
    }

    /*
     * Fix the CMOS.
     */
    if (g_pvSavedLowCore)
    {
        uint32_t fSaved = ASMIntDisableFlags();

        ASMOutU8(0x70, 0x0f);
        ASMOutU8(0x71, 0x0a);

        ASMOutU8(0x70, 0x00);
        ASMInU8(0x71);

        ASMReloadCR3();
        ASMWriteBackAndInvalidateCaches();

        ASMSetFlags(fSaved);
    }

    /*
     * Release resources.
     */
    RTMemFree(g_pvSavedLowCore);
    g_pvSavedLowCore = NULL;

    RTR0MemObjFree(g_hMemLowCore, true /*fFreeMappings*/);
    g_hMemLowCore   = NIL_RTR0MEMOBJ;
    g_hMapLowCore   = NIL_RTR0MEMOBJ;
    g_pbLowCore     = NULL;
    g_HCPhysLowCore = NIL_RTHCPHYS;

    RTR0MemObjFree(g_hMemPage0, true /*fFreeMappings*/);
    g_hMemPage0     = NIL_RTR0MEMOBJ;
    g_hMapPage0     = NIL_RTR0MEMOBJ;
    g_pbPage0       = NULL;
}

