/* $Id: tstX86-FpuSaveRestore.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstX86-FpuSaveRestore - Experimenting with saving and restoring FPU.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/x86.h>

DECLASM(void) MyFpuPrepXcpt(void);
DECLASM(void) MyFpuSave(PX86FXSTATE pState);
DECLASM(void) MyFpuStoreEnv(PX86FSTENV32P pEnv);
DECLASM(void) MyFpuRestore(PX86FXSTATE pState);
DECLASM(void) MyFpuLoadEnv(PX86FSTENV32P pEnv);

int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstX86-FpuSaveRestore", &hTest);
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;
    RTTestBanner(hTest);

    RTTestSub(hTest, "CS/DS Selector");

    RTTestIPrintf(RTTESTLVL_ALWAYS,  "Initial state (0x20 will be subtracted from IP):\n");
    /* Trigger an exception to make sure we've got something to look at. */
    MyFpuPrepXcpt();
    static X86FXSTATE FxState;
    MyFpuSave(&FxState);
    static X86FSTENV32P FpuEnv;
    MyFpuStoreEnv(&FpuEnv);
#ifdef RT_ARCH_AMD64
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FxState IP=%#06x%04x%08x\n",  FxState.Rsrvd1, FxState.CS, FxState.FPUIP);
#else
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FxState CS:IP=%#06x:%#010x\n",  FxState.CS, FxState.FPUIP);
#endif
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FpuEnv  CS:IP=%#06x:%#010x\n",  FpuEnv.FPUCS, FpuEnv.FPUIP);

    /* Modify the state a little so we can tell the difference. */
    static X86FXSTATE FxState2;
    FxState2 = FxState;
    FxState2.FPUIP -= 0x20;
    static X86FSTENV32P FpuEnv2;
    FpuEnv2 = FpuEnv;
    FpuEnv2.FPUIP  -= 0x20;

    /* Just do FXRSTOR. */
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "Just FXRSTOR:\n");
    MyFpuRestore(&FxState2);

    static X86FXSTATE FxStateJustRestore;
    MyFpuSave(&FxStateJustRestore);
    static X86FSTENV32P FpuEnvJustRestore;
    MyFpuStoreEnv(&FpuEnvJustRestore);
#ifdef RT_ARCH_AMD64
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FxState IP=%#06x%04x%08x\n",  FxStateJustRestore.Rsrvd1, FxStateJustRestore.CS, FxStateJustRestore.FPUIP);
#else
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FxState CS:IP=%#06x:%#010x\n",  FxStateJustRestore.CS, FxStateJustRestore.FPUIP);
#endif
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FpuEnv  CS:IP=%#06x:%#010x\n",  FpuEnvJustRestore.FPUCS, FpuEnvJustRestore.FPUIP);


    /* FXRSTORE + FLDENV */
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "FXRSTOR first, then FLDENV:\n");
    MyFpuRestore(&FxState2);
    MyFpuLoadEnv(&FpuEnv2);

    static X86FXSTATE FxStateRestoreLoad;
    MyFpuSave(&FxStateRestoreLoad);
    static X86FSTENV32P FpuEnvRestoreLoad;
    MyFpuStoreEnv(&FpuEnvRestoreLoad);
#ifdef RT_ARCH_AMD64
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FxState IP=%#06x%04x%08x\n",  FxStateRestoreLoad.Rsrvd1, FxStateRestoreLoad.CS, FxStateRestoreLoad.FPUIP);
#else
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FxState CS:IP=%#06x:%#010x\n",  FxStateRestoreLoad.CS, FxStateRestoreLoad.FPUIP);
#endif
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FpuEnv  CS:IP=%#06x:%#010x\n",  FpuEnvRestoreLoad.FPUCS, FpuEnvRestoreLoad.FPUIP);

    /* Reverse the order (FLDENV + FXRSTORE). */
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "FLDENV first, then FXRSTOR:\n");
    MyFpuLoadEnv(&FpuEnv2);
    MyFpuRestore(&FxState2);

    static X86FXSTATE FxStateLoadRestore;
    MyFpuSave(&FxStateLoadRestore);
    static X86FSTENV32P FpuEnvLoadRestore;
    MyFpuStoreEnv(&FpuEnvLoadRestore);
#ifdef RT_ARCH_AMD64
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FxState IP=%#06x%04x%08x\n",  FxStateLoadRestore.Rsrvd1, FxStateLoadRestore.CS, FxStateLoadRestore.FPUIP);
#else
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FxState CS:IP=%#06x:%#010x\n",  FxStateLoadRestore.CS, FxStateLoadRestore.FPUIP);
#endif
    RTTestIPrintf(RTTESTLVL_ALWAYS,  "  FpuEnv  CS:IP=%#06x:%#010x\n",  FpuEnvLoadRestore.FPUCS, FpuEnvLoadRestore.FPUIP);


    return RTTestSummaryAndDestroy(hTest);
}
