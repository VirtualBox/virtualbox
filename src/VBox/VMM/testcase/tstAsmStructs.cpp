/* $Id: tstAsmStructs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Testcase for checking offsets in the assembly structures shared with C/C++.
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
#include <VBox/vmm/cpum.h>
#include "CPUMInternal.h"
#define IPRT_WITHOUT_NAMED_UNIONS_AND_STRUCTS 1 /* For HMInternal */
#include "HMInternal.h"
#undef  IPRT_WITHOUT_NAMED_UNIONS_AND_STRUCTS /* probably not necessary */
#include "VMMInternal.h"
#include <VBox/vmm/vm.h>
#define GVM_C_STYLE_STRUCTURES
#include <VBox/vmm/gvm.h>
#include <VBox/vmm/hm_vmx.h>

#include "tstHelp.h"
#include <iprt/stream.h>

/* Hack for validating nested HMCPU structures. */
typedef HMCPU::HMCPUVMX HMCPUVMX;
typedef HMCPU::HMCPUSVM HMCPUSVM;
typedef HMR0PERVCPU::HMR0CPUVMX HMR0CPUVMX;
typedef HMR0PERVCPU::HMR0CPUSVM HMR0CPUSVM;

/* For sup.mac simplifications. */
#define SUPDRVTRACERUSRCTX32    SUPDRVTRACERUSRCTX
#define SUPDRVTRACERUSRCTX64    SUPDRVTRACERUSRCTX


int main()
{
    int rc = 0;
    RTPrintf("tstAsmStructs: TESTING\n");

#ifdef IN_RING3
# include "tstAsmStructsHC.h"
#else
# include "tstAsmStructsRC.h"
#endif

    if (rc)
        RTPrintf("tstAsmStructs: FAILURE - %d errors \n", rc);
    else
        RTPrintf("tstAsmStructs: SUCCESS\n");
    return rc;
}

