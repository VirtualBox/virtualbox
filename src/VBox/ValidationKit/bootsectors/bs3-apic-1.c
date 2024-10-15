/* $Id: bs3-apic-1.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - bs3-apic-1, 16-bit C code.
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
#include <iprt/x86.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
BS3_DECL_CALLBACK(void)     ProtModeApicTests(void);


BS3_DECL(void) Main_rm()
{
    Bs3InitAll_rm();
    Bs3TestInit("bs3-apic-1");
    Bs3TestPrintf("g_uBs3CpuDetected=%#x\n", g_uBs3CpuDetected);
    Bs3TestSub("real-mode");

    /*
     * Check that there is an APIC
     */
    if (!(g_uBs3CpuDetected & BS3CPU_F_CPUID))
        Bs3TestFailed("CPUID not supported");
    else if (!(ASMCpuId_EDX(1) & X86_CPUID_FEATURE_EDX_MSR))
        Bs3TestFailed("No APIC: RDMSR/WRMSR not supported!");
    else if (!(ASMCpuId_EDX(1) & X86_CPUID_FEATURE_EDX_APIC))
        Bs3TestFailed("No APIC: CPUID(1) does not have EDX_APIC set!\n");
    else
    {
        uint64_t uApicBase2;
        uint64_t uApicBase = ASMRdMsr(MSR_IA32_APICBASE);
        Bs3TestPrintf("MSR_IA32_APICBASE=%#RX64 %s, %s cpu%s\n",
                      uApicBase,
                      uApicBase & MSR_IA32_APICBASE_EN ? "enabled" : "disabled",
                      uApicBase & MSR_IA32_APICBASE_BSP ? "bootstrap" : "slave",
                      uApicBase & MSR_IA32_APICBASE_EXTD ? ", x2apic" : "",
                      (uApicBase & X86_PAGE_4K_BASE_MASK) == MSR_IA32_APICBASE_ADDR ? ", !non-default address!" : "");

        /* Disable the APIC (according to wiki.osdev.org/APIC, disabling the
           APIC could require a CPU reset to re-enable it, but it works for us): */
        ASMWrMsr(MSR_IA32_APICBASE, uApicBase & ~(uint64_t)MSR_IA32_APICBASE_EN);
        uApicBase2 = ASMRdMsr(MSR_IA32_APICBASE);
        if (uApicBase2 == (uApicBase & ~(uint64_t)MSR_IA32_APICBASE_EN))
            Bs3TestPrintf("Disabling worked.\n");
        else
            Bs3TestFailedF("Disabling the APIC did not work (%#RX64)", uApicBase2);

        /* Enabling the APIC: */
        ASMWrMsr(MSR_IA32_APICBASE, uApicBase | MSR_IA32_APICBASE_EN);
        uApicBase2 = ASMRdMsr(MSR_IA32_APICBASE);
        if (uApicBase2 == (uApicBase | MSR_IA32_APICBASE_EN))
        {
            Bs3TestPrintf("Enabling worked.\n");

            /*
             * Do the rest of the testing in protected mode since we cannot
             * (easily) access the APIC address from real mode.
             */
            Bs3SwitchTo32BitAndCallC_rm(ProtModeApicTests, 0);
        }
        else
            Bs3TestFailedF("Enabling the APIC did not work (%#RX64)", uApicBase2);
    }

    Bs3TestTerm();
    Bs3Shutdown();
}

