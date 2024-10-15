/* $Id: tstVMStructSize.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstVMStructSize - testcase for check structure sizes/alignment
 *                   and to verify that HC and GC uses the same
 *                   representation of the structures.
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
#define IN_TSTVMSTRUCT 1
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/stam.h>
#include "PDMInternal.h"
#include <VBox/vmm/pdm.h>
#include "CFGMInternal.h"
#include "CPUMInternal.h"
#include "MMInternal.h"
#include "PGMInternal.h"
#include "SELMInternal.h"
#include "TRPMInternal.h"
#include "TMInternal.h"
#include "IOMInternal.h"
#include "SSMInternal.h"
#include "HMInternal.h"
#include "VMMInternal.h"
#include "DBGFInternal.h"
#include "GIMInternal.h"
#include "APICInternal.h"
#include "STAMInternal.h"
#include "VMInternal.h"
#include "EMInternal.h"
#include "IEMInternal.h"
#include "NEMInternal.h"
#include "../VMMR0/GMMR0Internal.h"
#include "../VMMR0/GVMMR0Internal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/gvm.h>
#include <VBox/param.h>
#include <VBox/dis.h>
#include <iprt/x86.h>

#include "tstHelp.h"
#include <iprt/stream.h>



int main()
{
    int rc = 0;
    RTPrintf("tstVMStructSize: TESTING\n");

    RTPrintf("info: struct VM: %d bytes\n", (int)sizeof(VM));

#define CHECK_PADDING_VM(align, member) \
    do \
    { \
        CHECK_PADDING(VM, member, align); \
        CHECK_MEMBER_ALIGNMENT(VM, member, align); \
        VM *p = NULL; NOREF(p); \
        if (sizeof(p->member.padding) >= (ssize_t)sizeof(p->member.s) + 128 + sizeof(p->member.s) / 20) \
            RTPrintf("warning: VM::%-8s: padding=%-5d s=%-5d -> %-4d  suggest=%-5u\n", \
                     #member, (int)sizeof(p->member.padding), (int)sizeof(p->member.s), \
                     (int)sizeof(p->member.padding) - (int)sizeof(p->member.s), \
                     (int)RT_ALIGN_Z(sizeof(p->member.s), (align))); \
    } while (0)


#define CHECK_PADDING_VMCPU(align, member) \
    do \
    { \
        CHECK_PADDING(VMCPU, member, align); \
        CHECK_MEMBER_ALIGNMENT(VMCPU, member, align); \
        VMCPU *p = NULL; NOREF(p); \
        if (sizeof(p->member.padding) >= (ssize_t)sizeof(p->member.s) + 128 + sizeof(p->member.s) / 20) \
            RTPrintf("warning: VMCPU::%-8s: padding=%-5d s=%-5d -> %-4d  suggest=%-5u\n", \
                     #member, (int)sizeof(p->member.padding), (int)sizeof(p->member.s), \
                     (int)sizeof(p->member.padding) - (int)sizeof(p->member.s), \
                     (int)RT_ALIGN_Z(sizeof(p->member.s), (align))); \
    } while (0)

#define CHECK_PADDING_UVM(align, member) \
    do \
    { \
        CHECK_PADDING(UVM, member, align); \
        CHECK_MEMBER_ALIGNMENT(UVM, member, align); \
        UVM *p = NULL; NOREF(p); \
        if (sizeof(p->member.padding) >= (ssize_t)sizeof(p->member.s) + 128 + sizeof(p->member.s) / 20) \
            RTPrintf("warning: UVM::%-8s: padding=%-5d s=%-5d -> %-4d  suggest=%-5u\n", \
                     #member, (int)sizeof(p->member.padding), (int)sizeof(p->member.s), \
                     (int)sizeof(p->member.padding) - (int)sizeof(p->member.s), \
                     (int)RT_ALIGN_Z(sizeof(p->member.s), (align))); \
    } while (0)

#define CHECK_PADDING_UVMCPU(align, member) \
    do \
    { \
        CHECK_PADDING(UVMCPU, member, align); \
        CHECK_MEMBER_ALIGNMENT(UVMCPU, member, align); \
        UVMCPU *p = NULL; NOREF(p); \
        if (sizeof(p->member.padding) >= (ssize_t)sizeof(p->member.s) + 128 + sizeof(p->member.s) / 20) \
            RTPrintf("warning: UVMCPU::%-8s: padding=%-5d s=%-5d -> %-4d  suggest=%-5u\n", \
                     #member, (int)sizeof(p->member.padding), (int)sizeof(p->member.s), \
                     (int)sizeof(p->member.padding) - (int)sizeof(p->member.s), \
                     (int)RT_ALIGN_Z(sizeof(p->member.s), (align))); \
    } while (0)

#define CHECK_PADDING_GVM(align, member) \
    do \
    { \
        CHECK_PADDING(GVM, member, align); \
        CHECK_MEMBER_ALIGNMENT(GVM, member, align); \
        GVM *p = NULL; NOREF(p); \
        if (sizeof(p->member.padding) >= (ssize_t)sizeof(p->member.s) + 128 + sizeof(p->member.s) / 20) \
            RTPrintf("warning: GVM::%-8s: padding=%-5d s=%-5d -> %-4d  suggest=%-5u\n", \
                     #member, (int)sizeof(p->member.padding), (int)sizeof(p->member.s), \
                     (int)sizeof(p->member.padding) - (int)sizeof(p->member.s), \
                     (int)RT_ALIGN_Z(sizeof(p->member.s), (align))); \
    } while (0)

#define CHECK_PADDING_GVMCPU(align, member) \
    do \
    { \
        CHECK_PADDING(GVMCPU, member, align); \
        CHECK_MEMBER_ALIGNMENT(GVMCPU, member, align); \
        GVMCPU *p = NULL; NOREF(p); \
        if (sizeof(p->member.padding) >= (ssize_t)sizeof(p->member.s) + 128 + sizeof(p->member.s) / 20) \
            RTPrintf("warning: GVMCPU::%-8s: padding=%-5d s=%-5d -> %-4d  suggest=%-5u\n", \
                     #member, (int)sizeof(p->member.padding), (int)sizeof(p->member.s), \
                     (int)sizeof(p->member.padding) - (int)sizeof(p->member.s), \
                     (int)RT_ALIGN_Z(sizeof(p->member.s), (align))); \
    } while (0)

#define PRINT_OFFSET(strct, member) \
    do \
    { \
        RTPrintf("info: %10s::%-24s offset %#6x (%6d) sizeof %4d\n",  #strct, #member, (int)RT_OFFSETOF(strct, member), (int)RT_OFFSETOF(strct, member), (int)RT_SIZEOFMEMB(strct, member)); \
    } while (0)


    CHECK_SIZE(uint128_t, 128/8);
    CHECK_SIZE(int128_t, 128/8);
    CHECK_SIZE(uint64_t, 64/8);
    CHECK_SIZE(int64_t, 64/8);
    CHECK_SIZE(uint32_t, 32/8);
    CHECK_SIZE(int32_t, 32/8);
    CHECK_SIZE(uint16_t, 16/8);
    CHECK_SIZE(int16_t, 16/8);
    CHECK_SIZE(uint8_t, 8/8);
    CHECK_SIZE(int8_t, 8/8);

    CHECK_SIZE(X86DESC, 8);
    CHECK_SIZE(X86DESC64, 16);
    CHECK_SIZE(VBOXIDTE, 8);
    CHECK_SIZE(VBOXIDTR, 10);
    CHECK_SIZE(VBOXGDTR, 10);
    CHECK_SIZE(VBOXTSS, 136);
    CHECK_SIZE(X86FXSTATE, 512);
    CHECK_SIZE(RTUUID, 16);
    CHECK_SIZE(X86PTE, 4);
    CHECK_SIZE(X86PD, X86_PAGE_4K_SIZE);
    CHECK_SIZE(X86PDE, 4);
    CHECK_SIZE(X86PT, X86_PAGE_4K_SIZE);
    CHECK_SIZE(X86PTEPAE, 8);
    CHECK_SIZE(X86PTPAE, X86_PAGE_4K_SIZE);
    CHECK_SIZE(X86PDEPAE, 8);
    CHECK_SIZE(X86PDPAE, X86_PAGE_4K_SIZE);
    CHECK_SIZE(X86PDPE, 8);
    CHECK_SIZE(X86PDPT, X86_PAGE_4K_SIZE);
    CHECK_SIZE(X86PML4E, 8);
    CHECK_SIZE(X86PML4, X86_PAGE_4K_SIZE);

    PRINT_OFFSET(VM, cpum);
    CHECK_PADDING_VM(64, cpum);
    CHECK_PADDING_VM(64, vmm);
    PRINT_OFFSET(VM, pgm);
    PRINT_OFFSET(VM, pgm.s.CritSectX);
    CHECK_PADDING_VM(64, pgm);
    PRINT_OFFSET(VM, hm);
    CHECK_PADDING_VM(64, hm);
    CHECK_PADDING_VM(64, trpm);
    CHECK_PADDING_VM(64, selm);
    CHECK_PADDING_VM(64, mm);
    CHECK_PADDING_VM(64, pdm);
    PRINT_OFFSET(VM, pdm.s.CritSect);
    CHECK_PADDING_VM(64, iom);
    CHECK_PADDING_VM(64, em);
    /*CHECK_PADDING_VM(64, iem);*/
    CHECK_PADDING_VM(64, nem);
    CHECK_PADDING_VM(64, tm);
    PRINT_OFFSET(VM, tm.s.VirtualSyncLock);
    CHECK_PADDING_VM(64, dbgf);
    CHECK_PADDING_VM(64, gim);
    CHECK_PADDING_VM(64, ssm);
    CHECK_PADDING_VM(8, vm);
    CHECK_PADDING_VM(8, cfgm);
    CHECK_PADDING_VM(8, apic);
    CHECK_PADDING_VM(8, iem);
    PRINT_OFFSET(VM, cfgm);
    PRINT_OFFSET(VM, apCpusR3);

    PRINT_OFFSET(VMCPU, cpum);
    CHECK_PADDING_VMCPU(64, iem);
    CHECK_PADDING_VMCPU(64, hm);
    CHECK_PADDING_VMCPU(64, em);
    CHECK_PADDING_VMCPU(64, nem);
    CHECK_PADDING_VMCPU(64, trpm);
    CHECK_PADDING_VMCPU(64, tm);
    CHECK_PADDING_VMCPU(64, vmm);
    CHECK_PADDING_VMCPU(64, pdm);
    CHECK_PADDING_VMCPU(64, iom);
    CHECK_PADDING_VMCPU(64, dbgf);
    CHECK_PADDING_VMCPU(64, gim);
    CHECK_PADDING_VMCPU(64, apic);

    PRINT_OFFSET(VMCPU, pgm);
    CHECK_PADDING_VMCPU(4096, pgm);
    CHECK_PADDING_VMCPU(4096, cpum);
    CHECK_PADDING_VMCPU(4096, cpum);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.svm.Vmcb, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.svm.abMsrBitmap, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.svm.abIoBitmap, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.vmx.Vmcs, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.vmx.ShadowVmcs, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.vmx.abVmreadBitmap, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.vmx.abVmwriteBitmap, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.vmx.aEntryMsrLoadArea, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.vmx.aExitMsrStoreArea, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.vmx.aExitMsrLoadArea, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.vmx.abMsrBitmap, 4096);
    CHECK_MEMBER_ALIGNMENT(VMCPU, cpum.s.Guest.hwvirt.vmx.abIoBitmap, 4096);

    PVM pVM = NULL; NOREF(pVM);

    /* the VMCPUs are page aligned TLB hit reasons. */
    CHECK_SIZE_ALIGNMENT(VMCPU, 4096);

    /* cpumctx */
    CHECK_MEMBER_ALIGNMENT(CPUMCTX, rax, 32);
    CHECK_MEMBER_ALIGNMENT(CPUMCTX, idtr.pIdt, 8);
    CHECK_MEMBER_ALIGNMENT(CPUMCTX, gdtr.pGdt, 8);
    CHECK_MEMBER_ALIGNMENT(CPUMCTX, SysEnter, 8);
    CHECK_MEMBER_ALIGNMENT(CPUMCTX, hwvirt, 8);

#if HC_ARCH_BITS == 32
    /* CPUMHOSTCTX - lss pair */
    if (RT_UOFFSETOF(CPUMHOSTCTX, esp) + 4 != RT_UOFFSETOF(CPUMHOSTCTX, ss))
    {
        RTPrintf("error! CPUMHOSTCTX lss has been split up!\n");
        rc++;
    }
#endif
    CHECK_SIZE_ALIGNMENT(CPUMCTX, 64);
    CHECK_SIZE_ALIGNMENT(CPUMHOSTCTX, 64);
    CHECK_SIZE_ALIGNMENT(CPUMCTXMSRS, 64);

    /* pdm */
    PRINT_OFFSET(PDMDEVINSR3, Internal);
    PRINT_OFFSET(PDMDEVINSR3, achInstanceData);
    CHECK_MEMBER_ALIGNMENT(PDMDEVINSR3, achInstanceData, 64);
    CHECK_PADDING(PDMDEVINSR3, Internal, 1);

    PRINT_OFFSET(PDMDEVINSR0, Internal);
    PRINT_OFFSET(PDMDEVINSR0, achInstanceData);
    CHECK_MEMBER_ALIGNMENT(PDMDEVINSR0, achInstanceData, 64);
    CHECK_PADDING(PDMDEVINSR0, Internal, 1);

    PRINT_OFFSET(PDMDEVINSRC, Internal);
    PRINT_OFFSET(PDMDEVINSRC, achInstanceData);
    CHECK_MEMBER_ALIGNMENT(PDMDEVINSRC, achInstanceData, 64);
    CHECK_PADDING(PDMDEVINSRC, Internal, 1);

    PRINT_OFFSET(PDMUSBINS, Internal);
    PRINT_OFFSET(PDMUSBINS, achInstanceData);
    CHECK_MEMBER_ALIGNMENT(PDMUSBINS, achInstanceData, 32);
    CHECK_PADDING(PDMUSBINS, Internal, 1);

    PRINT_OFFSET(PDMDRVINS, Internal);
    PRINT_OFFSET(PDMDRVINS, achInstanceData);
    CHECK_MEMBER_ALIGNMENT(PDMDRVINS, achInstanceData, 32);
    CHECK_PADDING(PDMDRVINS, Internal, 1);

    CHECK_PADDING2(PDMCRITSECT);
    CHECK_PADDING2(PDMCRITSECTRW);

    /* pgm */
    CHECK_MEMBER_ALIGNMENT(PGMCPU, GCPhysCR3, sizeof(RTGCPHYS));
    CHECK_MEMBER_ALIGNMENT(PGMCPU, aGCPhysGstPaePDs, sizeof(RTGCPHYS));
    CHECK_MEMBER_ALIGNMENT(PGMCPU, Dis, 8);
    CHECK_MEMBER_ALIGNMENT(PGMCPU, cPoolAccessHandler, 8);
    CHECK_MEMBER_ALIGNMENT(PGMPOOLPAGE, idx, sizeof(uint16_t));
    CHECK_MEMBER_ALIGNMENT(PGMPOOLPAGE, pvPageR3, sizeof(RTHCPTR));
    CHECK_MEMBER_ALIGNMENT(PGMPOOLPAGE, GCPhys, sizeof(RTGCPHYS));
    CHECK_SIZE(PGMPAGE, 16);
    CHECK_MEMBER_ALIGNMENT(PGMRAMRANGE, aPages, 16);
    CHECK_SIZE_ALIGNMENT(PGMREGMMIO2RANGE, 16);
    CHECK_MEMBER_ALIGNMENT(PGMROMRANGE, aPages, 16);

    /* TM */
    CHECK_MEMBER_ALIGNMENT(TM, aTimerQueues, 64);
    CHECK_MEMBER_ALIGNMENT(TM, VirtualSyncLock, sizeof(uintptr_t));

    /* misc */
    CHECK_PADDING3(EMCPU, u.FatalLongJump, u.achPaddingFatalLongJump);
    CHECK_SIZE_ALIGNMENT(VMMR0JMPBUF, 8);
#if 0
    PRINT_OFFSET(VM, fForcedActions);
    PRINT_OFFSET(VM, StatQemuToGC);
    PRINT_OFFSET(VM, StatGCToQemu);
#endif

    CHECK_MEMBER_ALIGNMENT(IOM, CritSect, sizeof(uintptr_t));
    CHECK_MEMBER_ALIGNMENT(EMCPU, u.achPaddingFatalLongJump, 32);
    CHECK_MEMBER_ALIGNMENT(EMCPU, aExitRecords, sizeof(EMEXITREC));
    CHECK_MEMBER_ALIGNMENT(PGM, CritSectX, sizeof(uintptr_t));
    CHECK_MEMBER_ALIGNMENT(PDM, CritSect, sizeof(uintptr_t));

    /* hm - 32-bit gcc won't align uint64_t naturally, so check. */
    CHECK_MEMBER_ALIGNMENT(HM, vmx, 8);
    CHECK_MEMBER_ALIGNMENT(HM, svm, 8);
    CHECK_MEMBER_ALIGNMENT(HM, ForR3.uMaxAsid, 8);
    CHECK_MEMBER_ALIGNMENT(HM, ForR3.vmx, 8);
    CHECK_MEMBER_ALIGNMENT(HM, PatchTree, 8);
    CHECK_MEMBER_ALIGNMENT(HM, aPatches, 8);
    CHECK_MEMBER_ALIGNMENT(HMCPU, vmx, 8);
    CHECK_MEMBER_ALIGNMENT(HMR0PERVCPU, vmx.pfnStartVm, 8);
    CHECK_MEMBER_ALIGNMENT(HMCPU, vmx.VmcsInfo, 8);
    CHECK_MEMBER_ALIGNMENT(HMCPU, vmx.VmcsInfoNstGst, 8);
    CHECK_MEMBER_ALIGNMENT(HMR0PERVCPU, vmx.RestoreHost, 8);
    CHECK_MEMBER_ALIGNMENT(HMCPU, vmx.LastError, 8);
    CHECK_MEMBER_ALIGNMENT(HMCPU, svm, 8);
    CHECK_MEMBER_ALIGNMENT(HMR0PERVCPU, svm.pfnVMRun, 8);
    CHECK_MEMBER_ALIGNMENT(HMCPU, Event, 8);
    CHECK_MEMBER_ALIGNMENT(HMCPU, Event.u64IntInfo, 8);
    CHECK_MEMBER_ALIGNMENT(HMR0PERVCPU, svm.Dis, 8);
    CHECK_MEMBER_ALIGNMENT(HMCPU, StatEntry, 8);

    /* Make sure the set is large enough and has the correct size. */
    CHECK_SIZE(VMCPUSET, 32);
    if (sizeof(VMCPUSET) * 8 < VMM_MAX_CPU_COUNT)
    {
        RTPrintf("error! VMCPUSET is too small for VMM_MAX_CPU_COUNT=%u!\n", VMM_MAX_CPU_COUNT);
        rc++;
    }

    RTPrintf("info: struct UVM: %d bytes\n", (int)sizeof(UVM));

    CHECK_PADDING_UVM(32, vm);
    CHECK_PADDING_UVM(32, mm);
    CHECK_PADDING_UVM(32, pdm);
    CHECK_PADDING_UVM(32, stam);

    RTPrintf("info: struct UVMCPU: %d bytes\n", (int)sizeof(UVMCPU));
    CHECK_PADDING_UVMCPU(32, vm);

    CHECK_PADDING_GVM(4, gvmm);
    CHECK_PADDING_GVM(4, gmm);
    CHECK_PADDING_GVMCPU(4, gvmm);

    /*
     * Check that the optimized access macros for PGMPAGE works correctly (kind of
     * obsolete after dropping raw-mode).
     */
    PGMPAGE Page;
    PGM_PAGE_CLEAR(&Page);

    CHECK_EXPR(PGM_PAGE_GET_HNDL_PHYS_STATE(&Page) == PGM_PAGE_HNDL_PHYS_STATE_NONE);
    CHECK_EXPR(PGM_PAGE_HAS_ANY_HANDLERS(&Page) == false);
    CHECK_EXPR(PGM_PAGE_HAS_ACTIVE_HANDLERS(&Page) == false);
    CHECK_EXPR(PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(&Page) == false);

    PGM_PAGE_SET_HNDL_PHYS_STATE(&Page, PGM_PAGE_HNDL_PHYS_STATE_ALL, false);
    CHECK_EXPR(PGM_PAGE_GET_HNDL_PHYS_STATE(&Page) == PGM_PAGE_HNDL_PHYS_STATE_ALL);
    CHECK_EXPR(PGM_PAGE_HAS_ANY_HANDLERS(&Page) == true);
    CHECK_EXPR(PGM_PAGE_HAS_ACTIVE_HANDLERS(&Page) == true);
    CHECK_EXPR(PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(&Page) == true);

    PGM_PAGE_SET_HNDL_PHYS_STATE(&Page, PGM_PAGE_HNDL_PHYS_STATE_WRITE, false);
    CHECK_EXPR(PGM_PAGE_GET_HNDL_PHYS_STATE(&Page) == PGM_PAGE_HNDL_PHYS_STATE_WRITE);
    CHECK_EXPR(PGM_PAGE_HAS_ANY_HANDLERS(&Page) == true);
    CHECK_EXPR(PGM_PAGE_HAS_ACTIVE_HANDLERS(&Page) == true);
    CHECK_EXPR(PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(&Page) == false);

#undef AssertFatal
#define AssertFatal(expr)           do { } while (0)
#undef AssertFatalMsg
#define AssertFatalMsg(expr, msg)   do { } while (0)
#undef Assert
#define Assert(expr)                do { } while (0)

    PGM_PAGE_CLEAR(&Page);
    CHECK_EXPR(PGM_PAGE_GET_HCPHYS_NA(&Page) == 0);
    PGM_PAGE_SET_HCPHYS(NULL, &Page, UINT64_C(0x0000fffeff1ff000));
    CHECK_EXPR(PGM_PAGE_GET_HCPHYS_NA(&Page) == UINT64_C(0x0000fffeff1ff000));
    PGM_PAGE_SET_HCPHYS(NULL, &Page, UINT64_C(0x0000000000001000));
    CHECK_EXPR(PGM_PAGE_GET_HCPHYS_NA(&Page) == UINT64_C(0x0000000000001000));

    PGM_PAGE_INIT(&Page, UINT64_C(0x0000feedfacef000), UINT32_C(0x12345678), PGMPAGETYPE_RAM, PGM_PAGE_STATE_ALLOCATED);
    CHECK_EXPR(PGM_PAGE_GET_HCPHYS_NA(&Page) == UINT64_C(0x0000feedfacef000));
    CHECK_EXPR(PGM_PAGE_GET_PAGEID(&Page) == UINT32_C(0x12345678));
    CHECK_EXPR(PGM_PAGE_GET_TYPE_NA(&Page)   == PGMPAGETYPE_RAM);
    CHECK_EXPR(PGM_PAGE_GET_STATE_NA(&Page)  == PGM_PAGE_STATE_ALLOCATED);


    /*
     * Report result.
     */
    if (rc)
        RTPrintf("tstVMStructSize: FAILURE - %d errors\n", rc);
    else
        RTPrintf("tstVMStructSize: SUCCESS\n");
    return rc;
}

