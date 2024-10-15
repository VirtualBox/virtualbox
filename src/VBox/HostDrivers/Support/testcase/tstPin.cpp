/* $Id: tstPin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SUP Testcase - Memory locking interface (ring 3).
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
#include <VBox/sup.h>
#include <VBox/param.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>


#include "../SUPLibInternal.h"


int main(int argc, char **argv)
{
    RTTEST hTest;

    uint32_t fFlags = RTR3INIT_FLAGS_TRY_SUPLIB;
#if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32) /* For M1 at least. */
    fFlags |= SUPR3INIT_F_DRIVERLESS << RTR3INIT_FLAGS_SUPLIB_SHIFT;
#endif
    RTEXITCODE rcExit = RTTestInitExAndCreate(argc, &argv, fFlags, "tstPin", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    RTHCPHYS HCPhys;

    /*
     * Simple test.
     */
    RTTestISub("Simple");

    void *pv;
    int rc = SUPR3PageAlloc(1, 0, &pv);
    RTTESTI_CHECK_RC_OK(rc);
    RTTestIPrintf(RTTESTLVL_DEBUG, "rc=%Rrc, pv=%p\n", rc, pv);
    SUPPAGE aPages[1];
    rc = supR3PageLock(pv, 1, &aPages[0]);
    RTTESTI_CHECK_RC_OK(rc);
    RTTestIPrintf(RTTESTLVL_DEBUG, "rc=%Rrc pv=%p aPages[0]=%RHp\n", rc, pv, aPages[0]);
    RTThreadSleep(1500);
#if 0
    RTTestIPrintf(RTTESTLVL_DEBUG, "Unlocking...\n");
    RTThreadSleep(250);
    rc = SUPPageUnlock(pv);
    RTTestIPrintf(RTTESTLVL_DEBUG, "rc=%Rrc\n", rc);
    RTThreadSleep(1500);
#endif

    RTTestISubDone();

    RTTestISub("Extensive");

    /*
     * More extensive.
     */
    static struct
    {
        void       *pv;
        void       *pvAligned;
        SUPPAGE     aPages[16];
    } aPinnings[500];

    for (unsigned i = 0; i < RT_ELEMENTS(aPinnings); i++)
    {
        aPinnings[i].pv = NULL;
        SUPR3PageAlloc(0x10000 >> PAGE_SHIFT, 0, &aPinnings[i].pv);
        aPinnings[i].pvAligned = RT_ALIGN_P(aPinnings[i].pv, PAGE_SIZE);
        rc = supR3PageLock(aPinnings[i].pvAligned, 0xf000 >> PAGE_SHIFT, &aPinnings[i].aPages[0]);
        if (RT_SUCCESS(rc))
        {
            RTTestIPrintf(RTTESTLVL_DEBUG, "i=%d: pvAligned=%p pv=%p:\n", i, aPinnings[i].pvAligned, aPinnings[i].pv);
            memset(aPinnings[i].pv, 0xfa, 0x10000);
            unsigned c4GPluss = 0;
            for (unsigned j = 0; j < (0xf000 >> PAGE_SHIFT); j++)
                if (aPinnings[i].aPages[j].Phys >= _4G)
                {
                    RTTestIPrintf(RTTESTLVL_DEBUG, "%2d: vrt=%p phys=%RHp\n", j, (char *)aPinnings[i].pvAligned + (j << PAGE_SHIFT), aPinnings[i].aPages[j].Phys);
                    c4GPluss++;
                }
            RTTestIPrintf(RTTESTLVL_DEBUG, "i=%d: c4GPluss=%d\n", i, c4GPluss);
        }
        else
        {
            RTTestIFailed("SUPPageLock() failed with rc=%Rrc\n", rc);
            SUPR3PageFree(aPinnings[i].pv, 0x10000 >> PAGE_SHIFT);
            aPinnings[i].pv = aPinnings[i].pvAligned = NULL;
            break;
        }
    }

    for (unsigned i = 0; i < RT_ELEMENTS(aPinnings); i += 2)
    {
        if (aPinnings[i].pvAligned)
        {
            rc = supR3PageUnlock(aPinnings[i].pvAligned);
            RTTESTI_CHECK_MSG(RT_SUCCESS(rc), ("SUPPageUnlock(%p) -> rc=%Rrc\n", aPinnings[i].pvAligned, rc));
            memset(aPinnings[i].pv, 0xaf, 0x10000);
        }
    }

    for (unsigned i = 0; i < RT_ELEMENTS(aPinnings); i += 2)
    {
        if (aPinnings[i].pv)
        {
            memset(aPinnings[i].pv, 0xcc, 0x10000);
            SUPR3PageFree(aPinnings[i].pv, 0x10000 >> PAGE_SHIFT);
            aPinnings[i].pv = NULL;
        }
    }

    RTTestISubDone();


/* Support for allocating Ring-0 executable memory with contiguous physical backing isn't implemented on Solaris. */
#if !defined(RT_OS_SOLARIS)
    /*
     * Allocate a bit of contiguous memory.
     */
    RTTestISub("Contiguous memory");

    size_t cPages = RT_ALIGN_Z(15003, PAGE_SIZE) >> PAGE_SHIFT;

    pv = SUPR3ContAlloc(cPages, NULL, &HCPhys);
    if (pv && HCPhys)
    {
        RTTestIPrintf(RTTESTLVL_DEBUG, "SUPR3ContAlloc(15003) -> HCPhys=%llx pv=%p\n", HCPhys, pv);
        void *pv0 = pv;
        memset(pv0, 0xaf, 15003);
        pv = SUPR3ContAlloc(RT_ALIGN_Z(12999, PAGE_SIZE) >> PAGE_SHIFT, NULL, &HCPhys);
        if (pv && HCPhys)
        {
            RTTestIPrintf(RTTESTLVL_DEBUG, "SUPR3ContAlloc(12999) -> HCPhys=%llx pv=%p\n", HCPhys, pv);
            memset(pv, 0xbf, 12999);
            rc = SUPR3ContFree(pv, RT_ALIGN_Z(12999, PAGE_SIZE) >> PAGE_SHIFT);
            if (RT_FAILURE(rc))
                RTTestIPrintf(RTTESTLVL_DEBUG, "SUPR3ContFree failed! rc=%Rrc\n", rc);
        }
        else
            RTTestIFailed("SUPR3ContAlloc (2nd) failed!\n");
        memset(pv0, 0xaf, 15003);
        /* pv0 is intentionally not freed! */
    }
    else
        RTTestIFailed("SUPR3ContAlloc(%zu pages) failed!\n", cPages);

    RTTestISubDone();
#endif

    /*
     * Allocate a big chunk of virtual memory and then lock it.
     */
    RTTestISub("Big chunk");

    #define BIG_SIZE    72*1024*1024
    #define BIG_SIZEPP  (BIG_SIZE + PAGE_SIZE)
#if defined(RT_OS_SOLARIS)
    size_t cPages = RT_ALIGN_Z(15003, PAGE_SIZE) >> PAGE_SHIFT;
#endif
    pv     = NULL;
    cPages = BIG_SIZEPP >> PAGE_SHIFT;
    rc = SUPR3PageAlloc(cPages, 0, &pv);
    if (RT_SUCCESS(rc))
    {
        AssertPtr(pv);

        static SUPPAGE s_aPages[BIG_SIZE >> PAGE_SHIFT];
        void *pvAligned = RT_ALIGN_P(pv, PAGE_SIZE);
        rc = supR3PageLock(pvAligned, BIG_SIZE >> PAGE_SHIFT, &s_aPages[0]);
        if (RT_SUCCESS(rc))
        {
            /* dump */
            RTTestIPrintf(RTTESTLVL_DEBUG, "SUPPageLock(%p,%d,) succeeded!\n", pvAligned, BIG_SIZE);
            memset(pv, 0x42, BIG_SIZEPP);
            #if 0
            for (unsigned j = 0; j < (BIG_SIZE >> PAGE_SHIFT); j++)
                RTTestIPrintf(RTTESTLVL_DEBUG, "%2d: vrt=%p phys=%08x\n", j, (char *)pvAligned + (j << PAGE_SHIFT), (uintptr_t)s_aPages[j].pvPhys);
            #endif

            /* unlock */
            rc = supR3PageUnlock(pvAligned);
            if (RT_FAILURE(rc))
                RTTestIFailed("SUPPageUnlock(%p) failed with rc=%Rrc\n", pvAligned, rc);
            memset(pv, 0xcc, BIG_SIZEPP);
        }
        else
            RTTestIFailed("SUPPageLock(%p) failed with rc=%Rrc\n", pvAligned, rc);
        SUPR3PageFree(pv, cPages);
    }
    else
        RTTestIFailed("SUPPageAlloc(%zu pages) failed with rc=%Rrc\n", cPages, rc);

    RTTestISubDone();

     /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}
