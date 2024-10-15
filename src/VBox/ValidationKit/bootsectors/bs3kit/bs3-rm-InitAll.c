/* $Id: bs3-rm-InitAll.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Initialize all components, real mode.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
//#define BS3_USE_RM_TEXT_SEG 1
#include "bs3kit-template-header.h"
#include "bs3-cmn-test.h"
#include "bs3kit-linker.h"
#include <iprt/asm-amd64-x86.h>


BS3_MODE_PROTO_NOSB(void, Bs3EnteredMode,(void));


#ifdef BS3_WITH_LOAD_CHECKSUMS
/**
 * Verifies the base image checksum.
 */
static void bs3InitVerifyChecksum(void)
{
    BS3BOOTSECTOR const RT_FAR *pBootSector   = (BS3BOOTSECTOR const RT_FAR *)BS3_FP_MAKE(0, 0x7c00);
    uint32_t                    cSectors      = pBootSector->cTotalSectors;
    uint32_t const              cSectorsSaved = cSectors;
    uint16_t                    uCurSeg       = (BS3_ADDR_LOAD >> 4); /* ASSUMES 16 byte aligned load address! */
    uint32_t                    uChecksum     = BS3_CALC_CHECKSUM_INITIAL_VALUE;
    while (cSectors > 0)
    {
        uint8_t const *pbSrc = BS3_FP_MAKE(uCurSeg, 0);
        if (cSectors >= _32K / 512)
        {
            uChecksum = Bs3CalcChecksum(uChecksum, pbSrc, _32K);
            cSectors -= _32K / 512;
            uCurSeg  += 0x800;
        }
        else
        {
            uChecksum = Bs3CalcChecksum(uChecksum, pbSrc, (uint16_t)cSectors * 512);
            break;
        }
    }
    Bs3TestPrintf("base image checksum: %#RX32, expected %#RX32 over (%#RX32 sectors, uCurSeg=%#x) sizeof(size_t)=%d\n",
                  uChecksum, pBootSector->dwSerialNumber, cSectorsSaved, uCurSeg, sizeof(size_t));
    if (uChecksum != pBootSector->dwSerialNumber)
    {
        Bs3TestPrintf("base image checksum mismatch: %#RX32, expected %#RX32 over %#RX32 sectors\n",
                      uChecksum, pBootSector->dwSerialNumber, cSectorsSaved);
        Bs3Panic();
    }
}
#endif /* BS3_WITH_LOAD_CHECKSUMS */


BS3_DECL(void) Bs3InitAll_rm(void)
{
    uint8_t volatile BS3_FAR  *pcTicksFlpyOff;

#ifdef BS3_WITH_LOAD_CHECKSUMS
    /* This must be done before we modify anything in the loaded image. */
    bs3InitVerifyChecksum();
#endif

    /*
     * Detect CPU first as the memory init code will otherwise use 386
     * instrunctions and cause trouble on older CPUs.
     */
    Bs3CpuDetect_rm_far();
    Bs3InitMemory_rm_far();
    Bs3InitGdt_rm_far();

#ifdef BS3_INIT_ALL_WITH_HIGH_DLLS
    /*
     * Load the high DLLs (if any) now, before we bugger up the PIC and
     * replace the IVT.
     */
    Bs3InitHighDlls_rm_far();
#endif

    /*
     * Before we disable all interrupts, try convince the BIOS to stop the
     * floppy motor, as it is kind of disturbing when the floppy light remains
     * on for the whole testcase execution.
     */
    ASMIntDisable(); /* (probably already disabled, but no guarantees) */
    pcTicksFlpyOff = (uint8_t volatile BS3_FAR *)BS3_FP_MAKE(0x40, 0x40);
    if (*pcTicksFlpyOff)
    {
        uint32_t volatile BS3_FAR *pcTicks = (uint32_t volatile BS3_FAR *)BS3_FP_MAKE(0x40, 0x6c);
        uint32_t cInitialTicks;

        *pcTicksFlpyOff = 1;            /* speed up the countdown, don't want to wait for two seconds here. */
        cInitialTicks   = *pcTicks;
        ASMIntEnable();

        while (*pcTicks == cInitialTicks)
            ASMHalt();
    }
    ASMIntDisable();
    Bs3PicSetup(false /*fForcedReInit*/);

    /*
     * Initialize IDTs and such.
     */
    if (g_uBs3CpuDetected & BS3CPU_F_LONG_MODE)
        Bs3Trap64Init();
    if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386)
        Bs3Trap32Init();
    if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80286)
        Bs3Trap16Init();
    Bs3TrapRmV86Init();

    /*
     * Perform a real-mode enter to make some final environment adjustments
     * (like installing our syscall).
     */
    Bs3EnteredMode_rm();
}

