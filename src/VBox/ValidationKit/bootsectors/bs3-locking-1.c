/* $Id: bs3-locking-1.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - bs3-locking-1, 16-bit C code.
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
#include <bs3kit.h>
#include <iprt/asm-amd64-x86.h>

#include <VBox/VMMDevTesting.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static struct
{
    const char * BS3_FAR    pszName;
    uint32_t                cInnerLoops;
    uint32_t                uCtrlLo;
    uint32_t                uCtrlHi;
} g_aLockingTests[] =
{
#if 1
# if 1 /* no contention benchmark */
    {
        "None 0us/inf/0k",
        _32K,
        0,
        0,
    },
    {
        "RW None Exl 0us/inf/0k",
        _32K,
        0,
        0 | VMMDEV_TESTING_LOCKED_HI_TYPE_RW,
    },
# endif
    {
        "RW None Shr 0us/inf/0k",
        _32K,
        0,
        0 | VMMDEV_TESTING_LOCKED_HI_TYPE_RW | VMMDEV_TESTING_LOCKED_HI_EMT_SHARED,
    },
# if 1
    {
        "Contention 500us/250us/64k",
        2000 + 16384,
        500 | (UINT32_C(250) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        64 | VMMDEV_TESTING_LOCKED_HI_ENABLED,
    },
    {
        "Contention 100us/50us/8k",
        10000 + 4096,
        100 | (UINT32_C(50) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        8 | VMMDEV_TESTING_LOCKED_HI_ENABLED,
    },
    {
        "Contention 10us/1us/0k",
        16384 + 4096,
        10 | (UINT32_C(1) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        0 | VMMDEV_TESTING_LOCKED_HI_ENABLED,
    },
    {
        "Contention 500us/250us/64k poke",
        2000 + 16384,
        500 | (UINT32_C(250) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        64 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_POKE,
    },
    {
        "Contention 100us/50us/1k poke",
        10000 + 4096,
        100 | (UINT32_C(50) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        1 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_POKE,
    },
    {
        "Contention 500us/250us/64k poke void",
        2000 + 16384,
        500 | (UINT32_C(250) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        64 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_POKE | VMMDEV_TESTING_LOCKED_HI_BUSY_SUCCESS
    },
    {
        "Contention 50us/25us/8k poke void",
        20000 + 4096,
        50 | (UINT32_C(25) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        1 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_POKE | VMMDEV_TESTING_LOCKED_HI_BUSY_SUCCESS
    },
# endif
# if 1
    {
        "RW Contention Exl/Exl 50us/25us/16k",
        20000 + 4096,
        50 | (UINT32_C(25) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        16 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_TYPE_RW
    },
# endif
    {
        "RW Contention Shr/Exl 50us/25us/16k",
        20000 + 4096,
        50 | (UINT32_C(25) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        16 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_TYPE_RW | VMMDEV_TESTING_LOCKED_HI_THREAD_SHARED
    },
# if 1
    {
        "RW Contention Exl/Exl 50us/25us/16k poke",
        20000 + 4096,
        50 | (UINT32_C(25) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        16 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_TYPE_RW | VMMDEV_TESTING_LOCKED_HI_POKE
    },
# endif
    {
        "RW Contention Shr/Exl 50us/25us/16k poke",
        20000 + 4096,
        50 | (UINT32_C(25) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        16 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_TYPE_RW | VMMDEV_TESTING_LOCKED_HI_THREAD_SHARED
        | VMMDEV_TESTING_LOCKED_HI_POKE | VMMDEV_TESTING_LOCKED_HI_BUSY_SUCCESS
    },
# if 1
    {
        "RW Contention Exl/Exl 50us/25us/16k poke void",
        20000 + 4096,
        50 | (UINT32_C(25) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        16 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_TYPE_RW | VMMDEV_TESTING_LOCKED_HI_POKE
    },
# endif
    {
        "RW Contention Shr/Exl 50us/25us/16k poke void",
        20000 + 4096,
        50 | (UINT32_C(25) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        16 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_TYPE_RW | VMMDEV_TESTING_LOCKED_HI_THREAD_SHARED
        | VMMDEV_TESTING_LOCKED_HI_POKE | VMMDEV_TESTING_LOCKED_HI_BUSY_SUCCESS
    },
#endif

    {
        "RW Contention Exl/Shr 50us/25us/16k",
        20000 + 4096,
        50 | (UINT32_C(25) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        16 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_TYPE_RW | VMMDEV_TESTING_LOCKED_HI_EMT_SHARED
    },
    {
        "RW Contention Exl/Shr poke 250us/25us/16k",
        10000 + 4096,
        250 | (UINT32_C(25) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        16 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_TYPE_RW | VMMDEV_TESTING_LOCKED_HI_EMT_SHARED
        | VMMDEV_TESTING_LOCKED_HI_POKE
    },
    {
        "RW Contention Exl/Shr poke void 250us/25us/16k",
        10000 + 4096,
        250 | (UINT32_C(25) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        16 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_TYPE_RW | VMMDEV_TESTING_LOCKED_HI_EMT_SHARED
        | VMMDEV_TESTING_LOCKED_HI_POKE | VMMDEV_TESTING_LOCKED_HI_BUSY_SUCCESS
    },
    {
        "RW None Shr/Shr 50us/25us/16k",
        20000 + 4096,
        50 | (UINT32_C(25) << VMMDEV_TESTING_LOCKED_LO_WAIT_SHIFT),
        16 | VMMDEV_TESTING_LOCKED_HI_ENABLED | VMMDEV_TESTING_LOCKED_HI_TYPE_RW
           | VMMDEV_TESTING_LOCKED_HI_THREAD_SHARED | VMMDEV_TESTING_LOCKED_HI_EMT_SHARED
    },
};


BS3_DECL(void) Main_rm()
{
    uint64_t const  cNsPerTest = RT_NS_15SEC;
    unsigned        i;

    Bs3InitAll_rm();
    Bs3TestInit("bs3-locking-1");

    /*
     * Since this is a host-side test and we don't have raw-mode any more, we
     * just stay in raw-mode when doing the test.
     */
    for (i = 0; i < RT_ELEMENTS(g_aLockingTests); i++)
    {
        uint64_t const nsStart    = Bs3TestNow();
        uint64_t       cNsElapsed = 0;
        uint32_t       cTotal     = 0;
        uint32_t       j;

        Bs3TestSub(g_aLockingTests[i].pszName);
        ASMOutU32(VMMDEV_TESTING_IOPORT_LOCKED_LO, g_aLockingTests[i].uCtrlLo);
        ASMOutU32(VMMDEV_TESTING_IOPORT_LOCKED_HI, g_aLockingTests[i].uCtrlHi);

        for (j = 0; j < _2M && cTotal < _1G; j++)
        {

            /* The inner loop should avoid calling Bs3TestNow too often, while not overshooting the . */
            unsigned iInner = (unsigned)g_aLockingTests[i].cInnerLoops;
            cTotal += iInner;
            while (iInner-- > 0)
                ASMInU32(VMMDEV_TESTING_IOPORT_LOCKED_LO);

            cNsElapsed = Bs3TestNow() - nsStart;
            if (cNsElapsed >= cNsPerTest)
                break;
        }

        /* Disable locking. */
        ASMOutU32(VMMDEV_TESTING_IOPORT_LOCKED_HI, 0);

        Bs3TestValue("Loops", cTotal, VMMDEV_TESTING_UNIT_OCCURRENCES);
        Bs3TestValue("Elapsed", cNsElapsed, VMMDEV_TESTING_UNIT_NS);
        Bs3TestValue("PerLoop", cNsElapsed / cTotal, VMMDEV_TESTING_UNIT_NS_PER_OCCURRENCE);
    }

    Bs3TestTerm();
}

