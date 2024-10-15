/* $Id: NEMR3Native-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NEM - Native execution manager, native ring-3 Linux backend.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_NEM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/nem.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/trpm.h>
#include "NEMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <iprt/alloca.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/x86.h>

#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <linux/kvm.h>


/* Forward declarations of things called by the template. */
static int nemR3LnxInitSetupVm(PVM pVM, PRTERRINFO pErrInfo);


/* Instantiate the common bits we share with the ARMv8 KVM backend. */
#include "NEMR3NativeTemplate-linux.cpp.h"



/**
 * Does the early setup of a KVM VM.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pErrInfo            Where to always return error info.
 */
static int nemR3LnxInitSetupVm(PVM pVM, PRTERRINFO pErrInfo)
{
    AssertReturn(pVM->nem.s.fdVm != -1, RTErrInfoSet(pErrInfo, VERR_WRONG_ORDER, "Wrong initalization order"));

    /*
     * Enable user space MSRs and let us check everything KVM cannot handle.
     * We will set up filtering later when ring-3 init has completed.
     */
    struct kvm_enable_cap CapEn =
    {
        KVM_CAP_X86_USER_SPACE_MSR, 0,
        { KVM_MSR_EXIT_REASON_FILTER | KVM_MSR_EXIT_REASON_UNKNOWN | KVM_MSR_EXIT_REASON_INVAL, 0, 0, 0}
    };
    int rcLnx = ioctl(pVM->nem.s.fdVm, KVM_ENABLE_CAP, &CapEn);
    if (rcLnx == -1)
        return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "Failed to enable KVM_CAP_X86_USER_SPACE_MSR failed: %u", errno);

    /*
     * Create the VCpus.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];

        /* Create it. */
        pVCpu->nem.s.fdVCpu = ioctl(pVM->nem.s.fdVm, KVM_CREATE_VCPU, (unsigned long)idCpu);
        if (pVCpu->nem.s.fdVCpu < 0)
            return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "KVM_CREATE_VCPU failed for VCpu #%u: %d", idCpu, errno);

        /* Map the KVM_RUN area. */
        pVCpu->nem.s.pRun = (struct kvm_run *)mmap(NULL, pVM->nem.s.cbVCpuMmap, PROT_READ | PROT_WRITE, MAP_SHARED,
                                                   pVCpu->nem.s.fdVCpu, 0 /*offset*/);
        if ((void *)pVCpu->nem.s.pRun == MAP_FAILED)
            return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "mmap failed for VCpu #%u: %d", idCpu, errno);

        /* We want all x86 registers and events on each exit. */
        pVCpu->nem.s.pRun->kvm_valid_regs = KVM_SYNC_X86_REGS | KVM_SYNC_X86_SREGS | KVM_SYNC_X86_EVENTS;
    }
    return VINF_SUCCESS;
}


/**
 * Update the CPUID leaves for a VCPU.
 *
 * The KVM_SET_CPUID2 call replaces any previous leaves, so we have to redo
 * everything when there really just are single bit changes.  That said, it
 * looks like KVM update the XCR/XSAVE related stuff as well as the APIC enabled
 * bit(s), so it should suffice if we do this at startup, I hope.
 */
static int nemR3LnxUpdateCpuIdsLeaves(PVM pVM, PVMCPU pVCpu)
{
    uint32_t              cLeaves  = 0;
    PCCPUMCPUIDLEAF const paLeaves = CPUMR3CpuIdGetPtr(pVM, &cLeaves);
    struct kvm_cpuid2    *pReq = (struct kvm_cpuid2 *)alloca(RT_UOFFSETOF_DYN(struct kvm_cpuid2, entries[cLeaves + 2]));

    pReq->nent    = cLeaves;
    pReq->padding = 0;

    for (uint32_t i = 0; i < cLeaves; i++)
    {
        CPUMGetGuestCpuId(pVCpu, paLeaves[i].uLeaf, paLeaves[i].uSubLeaf, -1 /*f64BitMode*/,
                          &pReq->entries[i].eax,
                          &pReq->entries[i].ebx,
                          &pReq->entries[i].ecx,
                          &pReq->entries[i].edx);
        pReq->entries[i].function   = paLeaves[i].uLeaf;
        pReq->entries[i].index      = paLeaves[i].uSubLeaf;
        pReq->entries[i].flags      = !paLeaves[i].fSubLeafMask ? 0 : KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
        pReq->entries[i].padding[0] = 0;
        pReq->entries[i].padding[1] = 0;
        pReq->entries[i].padding[2] = 0;
    }

    int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_SET_CPUID2, pReq);
    AssertLogRelMsgReturn(rcLnx == 0, ("rcLnx=%d errno=%d cLeaves=%#x\n", rcLnx, errno, cLeaves), RTErrConvertFromErrno(errno));

    return VINF_SUCCESS;
}


int nemR3NativeInitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    /*
     * Make RTThreadPoke work again (disabled for avoiding unnecessary
     * critical section issues in ring-0).
     */
    if (enmWhat == VMINITCOMPLETED_RING3)
        VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, nemR3LnxFixThreadPoke, NULL);

    /*
     * Configure CPUIDs after ring-3 init has been done.
     */
    if (enmWhat == VMINITCOMPLETED_RING3)
    {
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            int rc = nemR3LnxUpdateCpuIdsLeaves(pVM, pVM->apCpusR3[idCpu]);
            AssertRCReturn(rc, rc);
        }
    }

    /*
     * Configure MSRs after ring-3 init is done.
     *
     * We only need to tell KVM which MSRs it can handle, as we already
     * requested KVM_MSR_EXIT_REASON_FILTER, KVM_MSR_EXIT_REASON_UNKNOWN
     * and KVM_MSR_EXIT_REASON_INVAL in nemR3LnxInitSetupVm, and here we
     * will use KVM_MSR_FILTER_DEFAULT_DENY.  So, all MSRs w/o a 1 in the
     * bitmaps should be deferred to ring-3.
     */
    if (enmWhat == VMINITCOMPLETED_RING3)
    {
        struct kvm_msr_filter MsrFilters = {0}; /* Structure with a couple of implicit paddings on 64-bit systems. */
        MsrFilters.flags = KVM_MSR_FILTER_DEFAULT_DENY;

        unsigned iRange = 0;
#define MSR_RANGE_BEGIN(a_uBase, a_uEnd, a_fFlags) \
        AssertCompile(0x3000 <= KVM_MSR_FILTER_MAX_BITMAP_SIZE * 8); \
        uint64_t RT_CONCAT(bm, a_uBase)[0x3000 / 64] = {0}; \
        do { \
            uint64_t * const pbm = RT_CONCAT(bm, a_uBase); \
            uint32_t   const uBase = UINT32_C(a_uBase); \
            uint32_t   const cMsrs = UINT32_C(a_uEnd) - UINT32_C(a_uBase); \
            MsrFilters.ranges[iRange].base   = UINT32_C(a_uBase); \
            MsrFilters.ranges[iRange].nmsrs  = cMsrs; \
            MsrFilters.ranges[iRange].flags  = (a_fFlags); \
            MsrFilters.ranges[iRange].bitmap = (uint8_t *)&RT_CONCAT(bm, a_uBase)[0]
#define MSR_RANGE_ADD(a_Msr) \
        do { Assert((uint32_t)(a_Msr) - uBase < cMsrs); ASMBitSet(pbm, (uint32_t)(a_Msr) - uBase); } while (0)
#define MSR_RANGE_END(a_cMinMsrs) \
            /* optimize the range size before closing: */ \
            uint32_t cBitmap = cMsrs / 64; \
            while (cBitmap > ((a_cMinMsrs) + 63 / 64) && pbm[cBitmap - 1] == 0) \
                cBitmap -= 1; \
            MsrFilters.ranges[iRange].nmsrs = cBitmap * 64; \
            iRange++; \
        } while (0)

        /* 1st Intel range: 0000_0000 to 0000_3000. */
        MSR_RANGE_BEGIN(0x00000000, 0x00003000, KVM_MSR_FILTER_READ | KVM_MSR_FILTER_WRITE);
        MSR_RANGE_ADD(MSR_IA32_TSC);
        MSR_RANGE_ADD(MSR_IA32_SYSENTER_CS);
        MSR_RANGE_ADD(MSR_IA32_SYSENTER_ESP);
        MSR_RANGE_ADD(MSR_IA32_SYSENTER_EIP);
        MSR_RANGE_ADD(MSR_IA32_CR_PAT);
        /** @todo more? */
        MSR_RANGE_END(64);

        /* 1st AMD range: c000_0000 to c000_3000 */
        MSR_RANGE_BEGIN(0xc0000000, 0xc0003000, KVM_MSR_FILTER_READ | KVM_MSR_FILTER_WRITE);
        MSR_RANGE_ADD(MSR_K6_EFER);
        MSR_RANGE_ADD(MSR_K6_STAR);
        MSR_RANGE_ADD(MSR_K8_GS_BASE);
        MSR_RANGE_ADD(MSR_K8_KERNEL_GS_BASE);
        MSR_RANGE_ADD(MSR_K8_LSTAR);
        MSR_RANGE_ADD(MSR_K8_CSTAR);
        MSR_RANGE_ADD(MSR_K8_SF_MASK);
        MSR_RANGE_ADD(MSR_K8_TSC_AUX);
        /** @todo add more? */
        MSR_RANGE_END(64);

        /** @todo Specify other ranges too? Like hyper-V and KVM to make sure we get
         *        the MSR requests instead of KVM. */

        int rcLnx = ioctl(pVM->nem.s.fdVm, KVM_X86_SET_MSR_FILTER, &MsrFilters);
        if (rcLnx == -1)
            return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                              "Failed to enable KVM_X86_SET_MSR_FILTER failed: %u", errno);
    }

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   CPU State                                                                                                                    *
*********************************************************************************************************************************/

/**
 * Worker that imports selected state from KVM.
 */
static int nemHCLnxImportState(PVMCPUCC pVCpu, uint64_t fWhat, PCPUMCTX pCtx, struct kvm_run *pRun)
{
    fWhat &= pVCpu->cpum.GstCtx.fExtrn;
    if (!fWhat)
        return VINF_SUCCESS;

    /*
     * Stuff that goes into kvm_run::s.regs.regs:
     */
    if (fWhat & (CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_GPRS_MASK))
    {
        if (fWhat & CPUMCTX_EXTRN_RIP)
            pCtx->rip       = pRun->s.regs.regs.rip;
        if (fWhat & CPUMCTX_EXTRN_RFLAGS)
            pCtx->rflags.u  = pRun->s.regs.regs.rflags;

        if (fWhat & CPUMCTX_EXTRN_RAX)
            pCtx->rax       = pRun->s.regs.regs.rax;
        if (fWhat & CPUMCTX_EXTRN_RCX)
            pCtx->rcx       = pRun->s.regs.regs.rcx;
        if (fWhat & CPUMCTX_EXTRN_RDX)
            pCtx->rdx       = pRun->s.regs.regs.rdx;
        if (fWhat & CPUMCTX_EXTRN_RBX)
            pCtx->rbx       = pRun->s.regs.regs.rbx;
        if (fWhat & CPUMCTX_EXTRN_RSP)
            pCtx->rsp       = pRun->s.regs.regs.rsp;
        if (fWhat & CPUMCTX_EXTRN_RBP)
            pCtx->rbp       = pRun->s.regs.regs.rbp;
        if (fWhat & CPUMCTX_EXTRN_RSI)
            pCtx->rsi       = pRun->s.regs.regs.rsi;
        if (fWhat & CPUMCTX_EXTRN_RDI)
            pCtx->rdi       = pRun->s.regs.regs.rdi;
        if (fWhat & CPUMCTX_EXTRN_R8_R15)
        {
            pCtx->r8        = pRun->s.regs.regs.r8;
            pCtx->r9        = pRun->s.regs.regs.r9;
            pCtx->r10       = pRun->s.regs.regs.r10;
            pCtx->r11       = pRun->s.regs.regs.r11;
            pCtx->r12       = pRun->s.regs.regs.r12;
            pCtx->r13       = pRun->s.regs.regs.r13;
            pCtx->r14       = pRun->s.regs.regs.r14;
            pCtx->r15       = pRun->s.regs.regs.r15;
        }
    }

    /*
     * Stuff that goes into kvm_run::s.regs.sregs.
     *
     * Note! The apic_base can be ignored because we gets all MSR writes to it
     *       and VBox always keeps the correct value.
     */
    bool fMaybeChangedMode = false;
    bool fUpdateCr3        = false;
    if (fWhat & (  CPUMCTX_EXTRN_SREG_MASK | CPUMCTX_EXTRN_TABLE_MASK | CPUMCTX_EXTRN_CR_MASK
                 | CPUMCTX_EXTRN_EFER      | CPUMCTX_EXTRN_APIC_TPR))
    {
        /** @todo what about Attr.n.u4LimitHigh?   */
#define NEM_LNX_IMPORT_SEG(a_CtxSeg, a_KvmSeg) do { \
            (a_CtxSeg).u64Base              = (a_KvmSeg).base; \
            (a_CtxSeg).u32Limit             = (a_KvmSeg).limit; \
            (a_CtxSeg).ValidSel = (a_CtxSeg).Sel = (a_KvmSeg).selector; \
            (a_CtxSeg).Attr.n.u4Type        = (a_KvmSeg).type; \
            (a_CtxSeg).Attr.n.u1DescType    = (a_KvmSeg).s; \
            (a_CtxSeg).Attr.n.u2Dpl         = (a_KvmSeg).dpl; \
            (a_CtxSeg).Attr.n.u1Present     = (a_KvmSeg).present; \
            (a_CtxSeg).Attr.n.u1Available   = (a_KvmSeg).avl; \
            (a_CtxSeg).Attr.n.u1Long        = (a_KvmSeg).l; \
            (a_CtxSeg).Attr.n.u1DefBig      = (a_KvmSeg).db; \
            (a_CtxSeg).Attr.n.u1Granularity = (a_KvmSeg).g; \
            (a_CtxSeg).Attr.n.u1Unusable    = (a_KvmSeg).unusable; \
            (a_CtxSeg).fFlags               = CPUMSELREG_FLAGS_VALID; \
            CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &(a_CtxSeg)); \
        } while (0)

        if (fWhat & CPUMCTX_EXTRN_SREG_MASK)
        {
            if (fWhat & CPUMCTX_EXTRN_ES)
                NEM_LNX_IMPORT_SEG(pCtx->es, pRun->s.regs.sregs.es);
            if (fWhat & CPUMCTX_EXTRN_CS)
                NEM_LNX_IMPORT_SEG(pCtx->cs, pRun->s.regs.sregs.cs);
            if (fWhat & CPUMCTX_EXTRN_SS)
                NEM_LNX_IMPORT_SEG(pCtx->ss, pRun->s.regs.sregs.ss);
            if (fWhat & CPUMCTX_EXTRN_DS)
                NEM_LNX_IMPORT_SEG(pCtx->ds, pRun->s.regs.sregs.ds);
            if (fWhat & CPUMCTX_EXTRN_FS)
                NEM_LNX_IMPORT_SEG(pCtx->fs, pRun->s.regs.sregs.fs);
            if (fWhat & CPUMCTX_EXTRN_GS)
                NEM_LNX_IMPORT_SEG(pCtx->gs, pRun->s.regs.sregs.gs);
        }
        if (fWhat & CPUMCTX_EXTRN_TABLE_MASK)
        {
            if (fWhat & CPUMCTX_EXTRN_GDTR)
            {
                pCtx->gdtr.pGdt     = pRun->s.regs.sregs.gdt.base;
                pCtx->gdtr.cbGdt    = pRun->s.regs.sregs.gdt.limit;
            }
            if (fWhat & CPUMCTX_EXTRN_IDTR)
            {
                pCtx->idtr.pIdt     = pRun->s.regs.sregs.idt.base;
                pCtx->idtr.cbIdt    = pRun->s.regs.sregs.idt.limit;
            }
            if (fWhat & CPUMCTX_EXTRN_LDTR)
                NEM_LNX_IMPORT_SEG(pCtx->ldtr, pRun->s.regs.sregs.ldt);
            if (fWhat & CPUMCTX_EXTRN_TR)
                NEM_LNX_IMPORT_SEG(pCtx->tr, pRun->s.regs.sregs.tr);
        }
        if (fWhat & CPUMCTX_EXTRN_CR_MASK)
        {
            if (fWhat & CPUMCTX_EXTRN_CR0)
            {
                if (pVCpu->cpum.GstCtx.cr0 != pRun->s.regs.sregs.cr0)
                {
                    CPUMSetGuestCR0(pVCpu, pRun->s.regs.sregs.cr0);
                    fMaybeChangedMode = true;
                }
            }
            if (fWhat & CPUMCTX_EXTRN_CR2)
                pCtx->cr2              = pRun->s.regs.sregs.cr2;
            if (fWhat & CPUMCTX_EXTRN_CR3)
            {
                if (pCtx->cr3 != pRun->s.regs.sregs.cr3)
                {
                    CPUMSetGuestCR3(pVCpu, pRun->s.regs.sregs.cr3);
                    fUpdateCr3 = true;
                }
            }
            if (fWhat & CPUMCTX_EXTRN_CR4)
            {
                if (pCtx->cr4 != pRun->s.regs.sregs.cr4)
                {
                    CPUMSetGuestCR4(pVCpu, pRun->s.regs.sregs.cr4);
                    fMaybeChangedMode = true;
                }
            }
        }
        if (fWhat & CPUMCTX_EXTRN_APIC_TPR)
            APICSetTpr(pVCpu, (uint8_t)pRun->s.regs.sregs.cr8 << 4);
        if (fWhat & CPUMCTX_EXTRN_EFER)
        {
            if (pCtx->msrEFER != pRun->s.regs.sregs.efer)
            {
                Log7(("NEM/%u: MSR EFER changed %RX64 -> %RX64\n", pVCpu->idCpu,  pVCpu->cpum.GstCtx.msrEFER, pRun->s.regs.sregs.efer));
                if ((pRun->s.regs.sregs.efer ^ pVCpu->cpum.GstCtx.msrEFER) & MSR_K6_EFER_NXE)
                    PGMNotifyNxeChanged(pVCpu, RT_BOOL(pRun->s.regs.sregs.efer & MSR_K6_EFER_NXE));
                pCtx->msrEFER = pRun->s.regs.sregs.efer;
                fMaybeChangedMode = true;
            }
        }
#undef NEM_LNX_IMPORT_SEG
    }

    /*
     * Debug registers.
     */
    if (fWhat & CPUMCTX_EXTRN_DR_MASK)
    {
        struct kvm_debugregs DbgRegs = {{0}};
        int rc = ioctl(pVCpu->nem.s.fdVCpu, KVM_GET_DEBUGREGS, &DbgRegs);
        AssertMsgReturn(rc == 0, ("rc=%d errno=%d\n", rc, errno), VERR_NEM_IPE_3);

        if (fWhat & CPUMCTX_EXTRN_DR0_DR3)
        {
            pCtx->dr[0] = DbgRegs.db[0];
            pCtx->dr[1] = DbgRegs.db[1];
            pCtx->dr[2] = DbgRegs.db[2];
            pCtx->dr[3] = DbgRegs.db[3];
        }
        if (fWhat & CPUMCTX_EXTRN_DR6)
            pCtx->dr[6] = DbgRegs.dr6;
        if (fWhat & CPUMCTX_EXTRN_DR7)
            pCtx->dr[7] = DbgRegs.dr7;
    }

    /*
     * FPU, SSE, AVX, ++.
     */
    if (fWhat & (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE | CPUMCTX_EXTRN_XCRx))
    {
        if (fWhat & (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE))
        {
            fWhat |= CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE; /* we do all or nothing at all */

            AssertCompile(sizeof(pCtx->XState) >= sizeof(struct kvm_xsave));
            int rc = ioctl(pVCpu->nem.s.fdVCpu, KVM_GET_XSAVE, &pCtx->XState);
            AssertMsgReturn(rc == 0, ("rc=%d errno=%d\n", rc, errno), VERR_NEM_IPE_3);
        }

        if (fWhat & CPUMCTX_EXTRN_XCRx)
        {
            struct kvm_xcrs Xcrs =
            {   /*.nr_xcrs = */ 2,
                /*.flags = */   0,
                /*.xcrs= */ {
                    { /*.xcr =*/ 0, /*.reserved=*/ 0, /*.value=*/ pCtx->aXcr[0] },
                    { /*.xcr =*/ 1, /*.reserved=*/ 0, /*.value=*/ pCtx->aXcr[1] },
                }
            };

            int rc = ioctl(pVCpu->nem.s.fdVCpu, KVM_GET_XCRS, &Xcrs);
            AssertMsgReturn(rc == 0, ("rc=%d errno=%d\n", rc, errno), VERR_NEM_IPE_3);

            pCtx->aXcr[0] = Xcrs.xcrs[0].value;
            pCtx->aXcr[1] = Xcrs.xcrs[1].value;
        }
    }

    /*
     * MSRs.
     */
    if (fWhat & (  CPUMCTX_EXTRN_KERNEL_GS_BASE | CPUMCTX_EXTRN_SYSCALL_MSRS | CPUMCTX_EXTRN_SYSENTER_MSRS
                 | CPUMCTX_EXTRN_TSC_AUX        | CPUMCTX_EXTRN_OTHER_MSRS))
    {
        union
        {
            struct kvm_msrs Core;
            uint64_t padding[2 + sizeof(struct kvm_msr_entry) * 32];
        }                   uBuf;
        uint64_t           *pauDsts[32];
        uint32_t            iMsr        = 0;
        PCPUMCTXMSRS const  pCtxMsrs    = CPUMQueryGuestCtxMsrsPtr(pVCpu);

#define ADD_MSR(a_Msr, a_uValue) do { \
            Assert(iMsr < 32); \
            uBuf.Core.entries[iMsr].index    = (a_Msr); \
            uBuf.Core.entries[iMsr].reserved = 0; \
            uBuf.Core.entries[iMsr].data     = UINT64_MAX; \
            pauDsts[iMsr] = &(a_uValue); \
            iMsr += 1; \
        } while (0)

        if (fWhat & CPUMCTX_EXTRN_KERNEL_GS_BASE)
            ADD_MSR(MSR_K8_KERNEL_GS_BASE, pCtx->msrKERNELGSBASE);
        if (fWhat & CPUMCTX_EXTRN_SYSCALL_MSRS)
        {
            ADD_MSR(MSR_K6_STAR,    pCtx->msrSTAR);
            ADD_MSR(MSR_K8_LSTAR,   pCtx->msrLSTAR);
            ADD_MSR(MSR_K8_CSTAR,   pCtx->msrCSTAR);
            ADD_MSR(MSR_K8_SF_MASK, pCtx->msrSFMASK);
        }
        if (fWhat & CPUMCTX_EXTRN_SYSENTER_MSRS)
        {
            ADD_MSR(MSR_IA32_SYSENTER_CS,  pCtx->SysEnter.cs);
            ADD_MSR(MSR_IA32_SYSENTER_EIP, pCtx->SysEnter.eip);
            ADD_MSR(MSR_IA32_SYSENTER_ESP, pCtx->SysEnter.esp);
        }
        if (fWhat & CPUMCTX_EXTRN_TSC_AUX)
            ADD_MSR(MSR_K8_TSC_AUX, pCtxMsrs->msr.TscAux);
        if (fWhat & CPUMCTX_EXTRN_OTHER_MSRS)
        {
            ADD_MSR(MSR_IA32_CR_PAT, pCtx->msrPAT);
            /** @todo What do we _have_ to add here?
             * We also have: Mttr*, MiscEnable, FeatureControl. */
        }

        uBuf.Core.pad   = 0;
        uBuf.Core.nmsrs = iMsr;
        int rc = ioctl(pVCpu->nem.s.fdVCpu, KVM_GET_MSRS, &uBuf);
        AssertMsgReturn(rc == (int)iMsr,
                        ("rc=%d iMsr=%d (->%#x) errno=%d\n",
                         rc, iMsr, (uint32_t)rc < iMsr ? uBuf.Core.entries[rc].index : 0, errno),
                        VERR_NEM_IPE_3);

        while (iMsr-- > 0)
            *pauDsts[iMsr] = uBuf.Core.entries[iMsr].data;
#undef ADD_MSR
    }

    /*
     * Interruptibility state and pending interrupts.
     */
    if (fWhat & (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI))
    {
        fWhat |= CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI; /* always do both, see export and interrupt FF handling */

        struct kvm_vcpu_events KvmEvents = {0};
        int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_GET_VCPU_EVENTS, &KvmEvents);
        AssertLogRelMsgReturn(rcLnx == 0, ("rcLnx=%d errno=%d\n", rcLnx, errno), VERR_NEM_IPE_3);

        if (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_RIP)
            pVCpu->cpum.GstCtx.rip = pRun->s.regs.regs.rip;

        CPUMUpdateInterruptShadowSsStiEx(&pVCpu->cpum.GstCtx,
                                         RT_BOOL(KvmEvents.interrupt.shadow & KVM_X86_SHADOW_INT_MOV_SS),
                                         RT_BOOL(KvmEvents.interrupt.shadow & KVM_X86_SHADOW_INT_STI),
                                         pVCpu->cpum.GstCtx.rip);
        CPUMUpdateInterruptInhibitingByNmi(&pVCpu->cpum.GstCtx, KvmEvents.nmi.masked != 0);

        if (KvmEvents.interrupt.injected)
        {
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportPendingInterrupt);
            TRPMAssertTrap(pVCpu, KvmEvents.interrupt.nr, !KvmEvents.interrupt.soft ? TRPM_HARDWARE_INT : TRPM_SOFTWARE_INT);
        }

        Assert(KvmEvents.nmi.injected == 0);
        Assert(KvmEvents.nmi.pending  == 0);
    }

    /*
     * Update the external mask.
     */
    pCtx->fExtrn &= ~fWhat;
    pVCpu->cpum.GstCtx.fExtrn &= ~fWhat;
    if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL))
        pVCpu->cpum.GstCtx.fExtrn = 0;

    /*
     * We sometimes need to update PGM on the guest status.
     */
    if (!fMaybeChangedMode && !fUpdateCr3)
    { /* likely */ }
    else
    {
        /*
         * Make sure we got all the state PGM might need.
         */
        Log7(("nemHCLnxImportState: fMaybeChangedMode=%d fUpdateCr3=%d fExtrnNeeded=%#RX64\n", fMaybeChangedMode, fUpdateCr3,
              pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_EFER) ));
        if (pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_EFER))
        {
            if (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_CR0)
            {
                if (pVCpu->cpum.GstCtx.cr0 != pRun->s.regs.sregs.cr0)
                {
                    CPUMSetGuestCR0(pVCpu, pRun->s.regs.sregs.cr0);
                    fMaybeChangedMode = true;
                }
            }
            if (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_CR3)
            {
                if (pCtx->cr3 != pRun->s.regs.sregs.cr3)
                {
                    CPUMSetGuestCR3(pVCpu, pRun->s.regs.sregs.cr3);
                    fUpdateCr3 = true;
                }
            }
            if (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_CR4)
            {
                if (pCtx->cr4 != pRun->s.regs.sregs.cr4)
                {
                    CPUMSetGuestCR4(pVCpu, pRun->s.regs.sregs.cr4);
                    fMaybeChangedMode = true;
                }
            }
            if (fWhat & CPUMCTX_EXTRN_EFER)
            {
                if (pCtx->msrEFER != pRun->s.regs.sregs.efer)
                {
                    Log7(("NEM/%u: MSR EFER changed %RX64 -> %RX64\n", pVCpu->idCpu,  pVCpu->cpum.GstCtx.msrEFER, pRun->s.regs.sregs.efer));
                    if ((pRun->s.regs.sregs.efer ^ pVCpu->cpum.GstCtx.msrEFER) & MSR_K6_EFER_NXE)
                        PGMNotifyNxeChanged(pVCpu, RT_BOOL(pRun->s.regs.sregs.efer & MSR_K6_EFER_NXE));
                    pCtx->msrEFER = pRun->s.regs.sregs.efer;
                    fMaybeChangedMode = true;
                }
            }

            pVCpu->cpum.GstCtx.fExtrn &= ~(CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_EFER);
            if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL))
                pVCpu->cpum.GstCtx.fExtrn = 0;
        }

        /*
         * Notify PGM about the changes.
         */
        if (fMaybeChangedMode)
        {
            int rc = PGMChangeMode(pVCpu, pVCpu->cpum.GstCtx.cr0, pVCpu->cpum.GstCtx.cr4,
                                   pVCpu->cpum.GstCtx.msrEFER, false /*fForce*/);
            AssertMsgReturn(rc == VINF_SUCCESS, ("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_NEM_IPE_1);
        }

        if (fUpdateCr3)
        {
            int rc = PGMUpdateCR3(pVCpu, pVCpu->cpum.GstCtx.cr3);
            if (rc == VINF_SUCCESS)
            { /* likely */ }
            else
                AssertMsgFailedReturn(("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_NEM_IPE_2);
        }
    }

    return VINF_SUCCESS;
}


/**
 * Interface for importing state on demand (used by IEM).
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context CPU structure.
 * @param   fWhat       What to import, CPUMCTX_EXTRN_XXX.
 */
VMM_INT_DECL(int) NEMImportStateOnDemand(PVMCPUCC pVCpu, uint64_t fWhat)
{
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnDemand);
    return nemHCLnxImportState(pVCpu, fWhat, &pVCpu->cpum.GstCtx, pVCpu->nem.s.pRun);
}


/**
 * Exports state to KVM.
 */
static int nemHCLnxExportState(PVM pVM, PVMCPU pVCpu, PCPUMCTX pCtx, struct kvm_run *pRun)
{
    uint64_t const fExtrn = ~pCtx->fExtrn & CPUMCTX_EXTRN_ALL;
    Assert((~fExtrn & CPUMCTX_EXTRN_ALL) != CPUMCTX_EXTRN_ALL);

    /*
     * Stuff that goes into kvm_run::s.regs.regs:
     */
    if (fExtrn & (CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_GPRS_MASK))
    {
        if (fExtrn & CPUMCTX_EXTRN_RIP)
            pRun->s.regs.regs.rip    = pCtx->rip;
        if (fExtrn & CPUMCTX_EXTRN_RFLAGS)
            pRun->s.regs.regs.rflags = pCtx->rflags.u;

        if (fExtrn & CPUMCTX_EXTRN_RAX)
            pRun->s.regs.regs.rax    = pCtx->rax;
        if (fExtrn & CPUMCTX_EXTRN_RCX)
            pRun->s.regs.regs.rcx    = pCtx->rcx;
        if (fExtrn & CPUMCTX_EXTRN_RDX)
            pRun->s.regs.regs.rdx    = pCtx->rdx;
        if (fExtrn & CPUMCTX_EXTRN_RBX)
            pRun->s.regs.regs.rbx    = pCtx->rbx;
        if (fExtrn & CPUMCTX_EXTRN_RSP)
            pRun->s.regs.regs.rsp    = pCtx->rsp;
        if (fExtrn & CPUMCTX_EXTRN_RBP)
            pRun->s.regs.regs.rbp    = pCtx->rbp;
        if (fExtrn & CPUMCTX_EXTRN_RSI)
            pRun->s.regs.regs.rsi    = pCtx->rsi;
        if (fExtrn & CPUMCTX_EXTRN_RDI)
            pRun->s.regs.regs.rdi    = pCtx->rdi;
        if (fExtrn & CPUMCTX_EXTRN_R8_R15)
        {
            pRun->s.regs.regs.r8     = pCtx->r8;
            pRun->s.regs.regs.r9     = pCtx->r9;
            pRun->s.regs.regs.r10    = pCtx->r10;
            pRun->s.regs.regs.r11    = pCtx->r11;
            pRun->s.regs.regs.r12    = pCtx->r12;
            pRun->s.regs.regs.r13    = pCtx->r13;
            pRun->s.regs.regs.r14    = pCtx->r14;
            pRun->s.regs.regs.r15    = pCtx->r15;
        }
        pRun->kvm_dirty_regs |= KVM_SYNC_X86_REGS;
    }

    /*
     * Stuff that goes into kvm_run::s.regs.sregs:
     *
     * The APIC base register updating is a little suboptimal... But at least
     * VBox always has the right base register value, so it's one directional.
     */
    uint64_t const uApicBase = APICGetBaseMsrNoCheck(pVCpu);
    if (   (fExtrn & (  CPUMCTX_EXTRN_SREG_MASK | CPUMCTX_EXTRN_TABLE_MASK | CPUMCTX_EXTRN_CR_MASK
                      | CPUMCTX_EXTRN_EFER      | CPUMCTX_EXTRN_APIC_TPR))
        || uApicBase != pVCpu->nem.s.uKvmApicBase)
    {
        if ((pVCpu->nem.s.uKvmApicBase ^ uApicBase) & MSR_IA32_APICBASE_EN)
            Log(("NEM/%u: APICBASE_EN changed %#010RX64 -> %#010RX64\n", pVCpu->idCpu, pVCpu->nem.s.uKvmApicBase, uApicBase));
        pRun->s.regs.sregs.apic_base = uApicBase;
        pVCpu->nem.s.uKvmApicBase    = uApicBase;

        if (fExtrn & CPUMCTX_EXTRN_APIC_TPR)
            pRun->s.regs.sregs.cr8   = CPUMGetGuestCR8(pVCpu);

#define NEM_LNX_EXPORT_SEG(a_KvmSeg, a_CtxSeg) do { \
            (a_KvmSeg).base     = (a_CtxSeg).u64Base; \
            (a_KvmSeg).limit    = (a_CtxSeg).u32Limit; \
            (a_KvmSeg).selector = (a_CtxSeg).Sel; \
            (a_KvmSeg).type     = (a_CtxSeg).Attr.n.u4Type; \
            (a_KvmSeg).s        = (a_CtxSeg).Attr.n.u1DescType; \
            (a_KvmSeg).dpl      = (a_CtxSeg).Attr.n.u2Dpl; \
            (a_KvmSeg).present  = (a_CtxSeg).Attr.n.u1Present; \
            (a_KvmSeg).avl      = (a_CtxSeg).Attr.n.u1Available; \
            (a_KvmSeg).l        = (a_CtxSeg).Attr.n.u1Long; \
            (a_KvmSeg).db       = (a_CtxSeg).Attr.n.u1DefBig; \
            (a_KvmSeg).g        = (a_CtxSeg).Attr.n.u1Granularity; \
            (a_KvmSeg).unusable = (a_CtxSeg).Attr.n.u1Unusable; \
            (a_KvmSeg).padding  = 0; \
        } while (0)

        if (fExtrn & CPUMCTX_EXTRN_SREG_MASK)
        {
            if (fExtrn & CPUMCTX_EXTRN_ES)
                NEM_LNX_EXPORT_SEG(pRun->s.regs.sregs.es, pCtx->es);
            if (fExtrn & CPUMCTX_EXTRN_CS)
                NEM_LNX_EXPORT_SEG(pRun->s.regs.sregs.cs, pCtx->cs);
            if (fExtrn & CPUMCTX_EXTRN_SS)
                NEM_LNX_EXPORT_SEG(pRun->s.regs.sregs.ss, pCtx->ss);
            if (fExtrn & CPUMCTX_EXTRN_DS)
                NEM_LNX_EXPORT_SEG(pRun->s.regs.sregs.ds, pCtx->ds);
            if (fExtrn & CPUMCTX_EXTRN_FS)
                NEM_LNX_EXPORT_SEG(pRun->s.regs.sregs.fs, pCtx->fs);
            if (fExtrn & CPUMCTX_EXTRN_GS)
                NEM_LNX_EXPORT_SEG(pRun->s.regs.sregs.gs, pCtx->gs);
        }
        if (fExtrn & CPUMCTX_EXTRN_TABLE_MASK)
        {
            if (fExtrn & CPUMCTX_EXTRN_GDTR)
            {
                pRun->s.regs.sregs.gdt.base  = pCtx->gdtr.pGdt;
                pRun->s.regs.sregs.gdt.limit = pCtx->gdtr.cbGdt;
                pRun->s.regs.sregs.gdt.padding[0] = 0;
                pRun->s.regs.sregs.gdt.padding[1] = 0;
                pRun->s.regs.sregs.gdt.padding[2] = 0;
            }
            if (fExtrn & CPUMCTX_EXTRN_IDTR)
            {
                pRun->s.regs.sregs.idt.base  = pCtx->idtr.pIdt;
                pRun->s.regs.sregs.idt.limit = pCtx->idtr.cbIdt;
                pRun->s.regs.sregs.idt.padding[0] = 0;
                pRun->s.regs.sregs.idt.padding[1] = 0;
                pRun->s.regs.sregs.idt.padding[2] = 0;
            }
            if (fExtrn & CPUMCTX_EXTRN_LDTR)
                NEM_LNX_EXPORT_SEG(pRun->s.regs.sregs.ldt, pCtx->ldtr);
            if (fExtrn & CPUMCTX_EXTRN_TR)
                NEM_LNX_EXPORT_SEG(pRun->s.regs.sregs.tr, pCtx->tr);
        }
        if (fExtrn & CPUMCTX_EXTRN_CR_MASK)
        {
            if (fExtrn & CPUMCTX_EXTRN_CR0)
                pRun->s.regs.sregs.cr0   = pCtx->cr0;
            if (fExtrn & CPUMCTX_EXTRN_CR2)
                pRun->s.regs.sregs.cr2   = pCtx->cr2;
            if (fExtrn & CPUMCTX_EXTRN_CR3)
                pRun->s.regs.sregs.cr3   = pCtx->cr3;
            if (fExtrn & CPUMCTX_EXTRN_CR4)
                pRun->s.regs.sregs.cr4   = pCtx->cr4;
        }
        if (fExtrn & CPUMCTX_EXTRN_EFER)
            pRun->s.regs.sregs.efer   = pCtx->msrEFER;

        RT_ZERO(pRun->s.regs.sregs.interrupt_bitmap); /* this is an alternative interrupt injection interface */

        pRun->kvm_dirty_regs |= KVM_SYNC_X86_SREGS;
    }

    /*
     * Debug registers.
     */
    if (fExtrn & CPUMCTX_EXTRN_DR_MASK)
    {
        struct kvm_debugregs DbgRegs = {{0}};

        if ((fExtrn & CPUMCTX_EXTRN_DR_MASK) != CPUMCTX_EXTRN_DR_MASK)
        {
            /* Partial debug state, we must get DbgRegs first so we can merge: */
            int rc = ioctl(pVCpu->nem.s.fdVCpu, KVM_GET_DEBUGREGS, &DbgRegs);
            AssertMsgReturn(rc == 0, ("rc=%d errno=%d\n", rc, errno), VERR_NEM_IPE_3);
        }

        if (fExtrn & CPUMCTX_EXTRN_DR0_DR3)
        {
            DbgRegs.db[0] = pCtx->dr[0];
            DbgRegs.db[1] = pCtx->dr[1];
            DbgRegs.db[2] = pCtx->dr[2];
            DbgRegs.db[3] = pCtx->dr[3];
        }
        if (fExtrn & CPUMCTX_EXTRN_DR6)
            DbgRegs.dr6 = pCtx->dr[6];
        if (fExtrn & CPUMCTX_EXTRN_DR7)
            DbgRegs.dr7 = pCtx->dr[7];

        int rc = ioctl(pVCpu->nem.s.fdVCpu, KVM_SET_DEBUGREGS, &DbgRegs);
        AssertMsgReturn(rc == 0, ("rc=%d errno=%d\n", rc, errno), VERR_NEM_IPE_3);
    }

    /*
     * FPU, SSE, AVX, ++.
     */
    if (fExtrn & (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE | CPUMCTX_EXTRN_XCRx))
    {
        if (fExtrn & (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE))
        {
            /** @todo could IEM just grab state partial control in some situations? */
            Assert(   (fExtrn & (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE))
                   ==           (CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE)); /* no partial states */

            AssertCompile(sizeof(pCtx->XState) >= sizeof(struct kvm_xsave));
            int rc = ioctl(pVCpu->nem.s.fdVCpu, KVM_SET_XSAVE, &pCtx->XState);
            AssertMsgReturn(rc == 0, ("rc=%d errno=%d\n", rc, errno), VERR_NEM_IPE_3);
        }

        if (fExtrn & CPUMCTX_EXTRN_XCRx)
        {
            struct kvm_xcrs Xcrs =
            {   /*.nr_xcrs = */ 2,
                /*.flags = */   0,
                /*.xcrs= */ {
                    { /*.xcr =*/ 0, /*.reserved=*/ 0, /*.value=*/ pCtx->aXcr[0] },
                    { /*.xcr =*/ 1, /*.reserved=*/ 0, /*.value=*/ pCtx->aXcr[1] },
                }
            };

            int rc = ioctl(pVCpu->nem.s.fdVCpu, KVM_SET_XCRS, &Xcrs);
            AssertMsgReturn(rc == 0, ("rc=%d errno=%d\n", rc, errno), VERR_NEM_IPE_3);
        }
    }

    /*
     * MSRs.
     */
    if (fExtrn & (  CPUMCTX_EXTRN_KERNEL_GS_BASE | CPUMCTX_EXTRN_SYSCALL_MSRS | CPUMCTX_EXTRN_SYSENTER_MSRS
                  | CPUMCTX_EXTRN_TSC_AUX        | CPUMCTX_EXTRN_OTHER_MSRS))
    {
        union
        {
            struct kvm_msrs Core;
            uint64_t padding[2 + sizeof(struct kvm_msr_entry) * 32];
        }                   uBuf;
        uint32_t            iMsr     = 0;
        PCPUMCTXMSRS const  pCtxMsrs = CPUMQueryGuestCtxMsrsPtr(pVCpu);

#define ADD_MSR(a_Msr, a_uValue) do { \
            Assert(iMsr < 32); \
            uBuf.Core.entries[iMsr].index    = (a_Msr); \
            uBuf.Core.entries[iMsr].reserved = 0; \
            uBuf.Core.entries[iMsr].data     = (a_uValue); \
            iMsr += 1; \
        } while (0)

        if (fExtrn & CPUMCTX_EXTRN_KERNEL_GS_BASE)
            ADD_MSR(MSR_K8_KERNEL_GS_BASE, pCtx->msrKERNELGSBASE);
        if (fExtrn & CPUMCTX_EXTRN_SYSCALL_MSRS)
        {
            ADD_MSR(MSR_K6_STAR,    pCtx->msrSTAR);
            ADD_MSR(MSR_K8_LSTAR,   pCtx->msrLSTAR);
            ADD_MSR(MSR_K8_CSTAR,   pCtx->msrCSTAR);
            ADD_MSR(MSR_K8_SF_MASK, pCtx->msrSFMASK);
        }
        if (fExtrn & CPUMCTX_EXTRN_SYSENTER_MSRS)
        {
            ADD_MSR(MSR_IA32_SYSENTER_CS,  pCtx->SysEnter.cs);
            ADD_MSR(MSR_IA32_SYSENTER_EIP, pCtx->SysEnter.eip);
            ADD_MSR(MSR_IA32_SYSENTER_ESP, pCtx->SysEnter.esp);
        }
        if (fExtrn & CPUMCTX_EXTRN_TSC_AUX)
            ADD_MSR(MSR_K8_TSC_AUX, pCtxMsrs->msr.TscAux);
        if (fExtrn & CPUMCTX_EXTRN_OTHER_MSRS)
        {
            ADD_MSR(MSR_IA32_CR_PAT, pCtx->msrPAT);
            /** @todo What do we _have_ to add here?
             * We also have: Mttr*, MiscEnable, FeatureControl. */
        }

        uBuf.Core.pad   = 0;
        uBuf.Core.nmsrs = iMsr;
        int rc = ioctl(pVCpu->nem.s.fdVCpu, KVM_SET_MSRS, &uBuf);
        AssertMsgReturn(rc == (int)iMsr,
                        ("rc=%d iMsr=%d (->%#x) errno=%d\n",
                         rc, iMsr, (uint32_t)rc < iMsr ? uBuf.Core.entries[rc].index : 0, errno),
                        VERR_NEM_IPE_3);
    }

    /*
     * Interruptibility state.
     *
     * Note! This I/O control function sets most fields passed in, so when
     *       raising an interrupt, NMI, SMI or exception, this must be done
     *       by the code doing the rasing or we'll overwrite it here.
     */
    if (fExtrn & (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI))
    {
        Assert(   (fExtrn & (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI))
               ==           (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI));

        struct kvm_vcpu_events KvmEvents = {0};

        KvmEvents.flags = KVM_VCPUEVENT_VALID_SHADOW;
        if (!CPUMIsInInterruptShadowWithUpdate(&pVCpu->cpum.GstCtx))
        { /* probably likely */ }
        else
            KvmEvents.interrupt.shadow = (CPUMIsInInterruptShadowAfterSs(&pVCpu->cpum.GstCtx)  ? KVM_X86_SHADOW_INT_MOV_SS : 0)
                                       | (CPUMIsInInterruptShadowAfterSti(&pVCpu->cpum.GstCtx) ? KVM_X86_SHADOW_INT_STI    : 0);

        /* No flag - this is updated unconditionally. */
        KvmEvents.nmi.masked = CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx);

        if (TRPMHasTrap(pVCpu))
        {
            TRPMEVENT enmType = TRPM_32BIT_HACK;
            uint8_t   bTrapNo = 0;
            TRPMQueryTrap(pVCpu, &bTrapNo, &enmType);
            Log(("nemHCLnxExportState: Pending trap: bTrapNo=%#x enmType=%d\n", bTrapNo, enmType));
            if (   enmType == TRPM_HARDWARE_INT
                || enmType == TRPM_SOFTWARE_INT)
            {
                KvmEvents.interrupt.soft     = enmType == TRPM_SOFTWARE_INT;
                KvmEvents.interrupt.nr       = bTrapNo;
                KvmEvents.interrupt.injected = 1;
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExportPendingInterrupt);
                TRPMResetTrap(pVCpu);
            }
            else
                AssertFailed();
        }

        int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_SET_VCPU_EVENTS, &KvmEvents);
        AssertLogRelMsgReturn(rcLnx == 0, ("rcLnx=%d errno=%d\n", rcLnx, errno), VERR_NEM_IPE_3);
    }

    /*
     * KVM now owns all the state.
     */
    pCtx->fExtrn = CPUMCTX_EXTRN_KEEPER_NEM | CPUMCTX_EXTRN_ALL;

    RT_NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * Query the CPU tick counter and optionally the TSC_AUX MSR value.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context CPU structure.
 * @param   pcTicks     Where to return the CPU tick count.
 * @param   puAux       Where to return the TSC_AUX register value.
 */
VMM_INT_DECL(int) NEMHCQueryCpuTick(PVMCPUCC pVCpu, uint64_t *pcTicks, uint32_t *puAux)
{
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatQueryCpuTick);
    // KVM_GET_CLOCK?
    RT_NOREF(pVCpu, pcTicks, puAux);
    return VINF_SUCCESS;
}


/**
 * Resumes CPU clock (TSC) on all virtual CPUs.
 *
 * This is called by TM when the VM is started, restored, resumed or similar.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context CPU structure of the calling EMT.
 * @param   uPausedTscValue The TSC value at the time of pausing.
 */
VMM_INT_DECL(int) NEMHCResumeCpuTickOnAll(PVMCC pVM, PVMCPUCC pVCpu, uint64_t uPausedTscValue)
{
    // KVM_SET_CLOCK?
    RT_NOREF(pVM, pVCpu, uPausedTscValue);
    return VINF_SUCCESS;
}


VMM_INT_DECL(uint32_t) NEMHCGetFeatures(PVMCC pVM)
{
    RT_NOREF(pVM);
    return NEM_FEAT_F_NESTED_PAGING
         | NEM_FEAT_F_FULL_GST_EXEC
         | NEM_FEAT_F_XSAVE_XRSTOR;
}



/*********************************************************************************************************************************
*   Execution                                                                                                                    *
*********************************************************************************************************************************/


VMMR3_INT_DECL(bool) NEMR3CanExecuteGuest(PVM pVM, PVMCPU pVCpu)
{
    /*
     * Only execute when the A20 gate is enabled as I cannot immediately
     * spot any A20 support in KVM.
     */
    RT_NOREF(pVM);
    Assert(VM_IS_NEM_ENABLED(pVM));
    return PGMPhysIsA20Enabled(pVCpu);
}


bool nemR3NativeSetSingleInstruction(PVM pVM, PVMCPU pVCpu, bool fEnable)
{
    NOREF(pVM); NOREF(pVCpu); NOREF(fEnable);
    return false;
}


void nemR3NativeNotifyFF(PVM pVM, PVMCPU pVCpu, uint32_t fFlags)
{
    int rc = RTThreadPoke(pVCpu->hThread);
    LogFlow(("nemR3NativeNotifyFF: #%u -> %Rrc\n", pVCpu->idCpu, rc));
    AssertRC(rc);
    RT_NOREF(pVM, fFlags);
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChanged(PVM pVM, bool fUseDebugLoop)
{
    RT_NOREF(pVM, fUseDebugLoop);
    return false;
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChangedPerCpu(PVM pVM, PVMCPU pVCpu, bool fUseDebugLoop)
{
    RT_NOREF(pVM, pVCpu, fUseDebugLoop);
    return false;
}


/**
 * Deals with pending interrupt FFs prior to executing guest code.
 */
static VBOXSTRICTRC nemHCLnxHandleInterruptFF(PVM pVM, PVMCPU pVCpu, struct kvm_run *pRun)
{
    RT_NOREF_PV(pVM);

    /*
     * Do not doing anything if TRPM has something pending already as we can
     * only inject one event per KVM_RUN call.  This can only happend if we
     * can directly from the loop in EM, so the inhibit bits must be internal.
     */
    if (!TRPMHasTrap(pVCpu))
    { /* semi likely */ }
    else
    {
        Assert(!(pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI)));
        Log8(("nemHCLnxHandleInterruptFF: TRPM has an pending event already\n"));
        return VINF_SUCCESS;
    }

    /*
     * First update APIC.  We ASSUME this won't need TPR/CR8.
     */
    if (VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_UPDATE_APIC))
    {
        APICUpdatePendingInterrupts(pVCpu);
        if (!VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC
                                      | VMCPU_FF_INTERRUPT_NMI  | VMCPU_FF_INTERRUPT_SMI))
            return VINF_SUCCESS;
    }

    /*
     * We don't currently implement SMIs.
     */
    AssertReturn(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_SMI), VERR_NEM_IPE_0);

    /*
     * In KVM the CPUMCTX_EXTRN_INHIBIT_INT and CPUMCTX_EXTRN_INHIBIT_NMI states
     * are tied together with interrupt and NMI delivery, so we must get and
     * synchronize these all in one go and set both CPUMCTX_EXTRN_INHIBIT_XXX flags.
     * If we don't we may lose the interrupt/NMI we marked pending here when the
     * state is exported again before execution.
     */
    struct kvm_vcpu_events KvmEvents = {0};
    int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_GET_VCPU_EVENTS, &KvmEvents);
    AssertLogRelMsgReturn(rcLnx == 0, ("rcLnx=%d errno=%d\n", rcLnx, errno), VERR_NEM_IPE_5);

    if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_RIP))
        pRun->s.regs.regs.rip = pVCpu->cpum.GstCtx.rip;

    KvmEvents.flags |= KVM_VCPUEVENT_VALID_SHADOW;
    if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_INHIBIT_INT))
        KvmEvents.interrupt.shadow = !CPUMIsInInterruptShadowWithUpdate(&pVCpu->cpum.GstCtx) ? 0
                                   :   (CPUMIsInInterruptShadowAfterSs(&pVCpu->cpum.GstCtx)  ? KVM_X86_SHADOW_INT_MOV_SS : 0)
                                     | (CPUMIsInInterruptShadowAfterSti(&pVCpu->cpum.GstCtx) ? KVM_X86_SHADOW_INT_STI    : 0);
    else
        CPUMUpdateInterruptShadowSsStiEx(&pVCpu->cpum.GstCtx,
                                         RT_BOOL(KvmEvents.interrupt.shadow & KVM_X86_SHADOW_INT_MOV_SS),
                                         RT_BOOL(KvmEvents.interrupt.shadow & KVM_X86_SHADOW_INT_STI),
                                         pRun->s.regs.regs.rip);

    if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_INHIBIT_NMI))
        KvmEvents.nmi.masked = CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx);
    else
        CPUMUpdateInterruptInhibitingByNmi(&pVCpu->cpum.GstCtx, KvmEvents.nmi.masked != 0);

    /* KVM will own the INT + NMI inhibit state soon: */
    pVCpu->cpum.GstCtx.fExtrn = (pVCpu->cpum.GstCtx.fExtrn & ~CPUMCTX_EXTRN_KEEPER_MASK)
                              | CPUMCTX_EXTRN_KEEPER_NEM | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI;

    /*
     * NMI? Try deliver it first.
     */
    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI))
    {
#if 0
        int rcLnx = ioctl(pVCpu->nem.s.fdVm, KVM_NMI, 0UL);
        AssertLogRelMsgReturn(rcLnx == 0, ("rcLnx=%d errno=%d\n", rcLnx, errno), VERR_NEM_IPE_5);
#else
        KvmEvents.flags      |= KVM_VCPUEVENT_VALID_NMI_PENDING;
        KvmEvents.nmi.pending = 1;
#endif
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_NMI);
        Log8(("Queuing NMI on %u\n", pVCpu->idCpu));
    }

    /*
     * APIC or PIC interrupt?
     */
    if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC))
    {
        if (pRun->s.regs.regs.rflags & X86_EFL_IF)
        {
            if (KvmEvents.interrupt.shadow == 0)
            {
                /*
                 * If CR8 is in KVM, update the VBox copy so PDMGetInterrupt will
                 * work correctly.
                 */
                if (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_APIC_TPR)
                    APICSetTpr(pVCpu, (uint8_t)pRun->cr8 << 4);

                uint8_t bInterrupt;
                int rc = PDMGetInterrupt(pVCpu, &bInterrupt);
                if (RT_SUCCESS(rc))
                {
                    Assert(KvmEvents.interrupt.injected == false);
#if 0
                    int rcLnx = ioctl(pVCpu->nem.s.fdVm, KVM_INTERRUPT, (unsigned long)bInterrupt);
                    AssertLogRelMsgReturn(rcLnx == 0, ("rcLnx=%d errno=%d\n", rcLnx, errno), VERR_NEM_IPE_5);
#else
                    KvmEvents.interrupt.nr       = bInterrupt;
                    KvmEvents.interrupt.soft     = false;
                    KvmEvents.interrupt.injected = true;
#endif
                    Log8(("Queuing interrupt %#x on %u: %04x:%08RX64 efl=%#x\n", bInterrupt, pVCpu->idCpu,
                          pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.eflags.u));
                }
                else if (rc == VERR_APIC_INTR_MASKED_BY_TPR) /** @todo this isn't extremely efficient if we get a lot of exits... */
                    Log8(("VERR_APIC_INTR_MASKED_BY_TPR\n")); /* We'll get a TRP exit - no interrupt window needed. */
                else
                    Log8(("PDMGetInterrupt failed -> %Rrc\n", rc));
            }
            else
            {
                pRun->request_interrupt_window = 1;
                Log8(("Interrupt window pending on %u (#2)\n", pVCpu->idCpu));
            }
        }
        else
        {
            pRun->request_interrupt_window = 1;
            Log8(("Interrupt window pending on %u (#1)\n", pVCpu->idCpu));
        }
    }

    /*
     * Now, update the state.
     */
    /** @todo skip when possible...   */
    rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_SET_VCPU_EVENTS, &KvmEvents);
    AssertLogRelMsgReturn(rcLnx == 0, ("rcLnx=%d errno=%d\n", rcLnx, errno), VERR_NEM_IPE_5);

    return VINF_SUCCESS;
}


/**
 * Handles KVM_EXIT_INTERNAL_ERROR.
 */
static VBOXSTRICTRC nemR3LnxHandleInternalError(PVMCPU pVCpu, struct kvm_run *pRun)
{
    Log(("NEM: KVM_EXIT_INTERNAL_ERROR! suberror=%#x (%d) ndata=%u data=%.*Rhxs\n", pRun->internal.suberror,
         pRun->internal.suberror, pRun->internal.ndata, sizeof(pRun->internal.data), &pRun->internal.data[0]));

    /*
     * Deal with each suberror, returning if we don't want IEM to handle it.
     */
    switch (pRun->internal.suberror)
    {
        case KVM_INTERNAL_ERROR_EMULATION:
        {
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_INTERNAL_ERROR_EMULATION),
                             pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitInternalErrorEmulation);
            break;
        }

        case KVM_INTERNAL_ERROR_SIMUL_EX:
        case KVM_INTERNAL_ERROR_DELIVERY_EV:
        case KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON:
        default:
        {
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_INTERNAL_ERROR_FATAL),
                             pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitInternalErrorFatal);
            const char *pszName;
            switch (pRun->internal.suberror)
            {
                case KVM_INTERNAL_ERROR_EMULATION:              pszName = "KVM_INTERNAL_ERROR_EMULATION"; break;
                case KVM_INTERNAL_ERROR_SIMUL_EX:               pszName = "KVM_INTERNAL_ERROR_SIMUL_EX"; break;
                case KVM_INTERNAL_ERROR_DELIVERY_EV:            pszName = "KVM_INTERNAL_ERROR_DELIVERY_EV"; break;
                case KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON: pszName = "KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON"; break;
                default:                                        pszName = "unknown"; break;
            }
            LogRel(("NEM: KVM_EXIT_INTERNAL_ERROR! suberror=%#x (%s) ndata=%u data=%.*Rhxs\n", pRun->internal.suberror, pszName,
                    pRun->internal.ndata, sizeof(pRun->internal.data), &pRun->internal.data[0]));
            return VERR_NEM_IPE_0;
        }
    }

    /*
     * Execute instruction in IEM and try get on with it.
     */
    Log2(("nemR3LnxHandleInternalError: Executing instruction at %04x:%08RX64 in IEM\n",
          pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip));
    VBOXSTRICTRC rcStrict = nemHCLnxImportState(pVCpu,
                                                IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_INHIBIT_INT
                                                 | CPUMCTX_EXTRN_INHIBIT_NMI,
                                                &pVCpu->cpum.GstCtx, pRun);
    if (RT_SUCCESS(rcStrict))
        rcStrict = IEMExecOne(pVCpu);
    return rcStrict;
}


/**
 * Handles KVM_EXIT_IO.
 */
static VBOXSTRICTRC nemHCLnxHandleExitIo(PVMCC pVM, PVMCPUCC pVCpu, struct kvm_run *pRun)
{
    /*
     * Input validation.
     */
    Assert(pRun->io.count > 0);
    Assert(pRun->io.size == 1 || pRun->io.size == 2 || pRun->io.size == 4);
    Assert(pRun->io.direction == KVM_EXIT_IO_IN || pRun->io.direction == KVM_EXIT_IO_OUT);
    Assert(pRun->io.data_offset < pVM->nem.s.cbVCpuMmap);
    Assert(pRun->io.data_offset + pRun->io.size * pRun->io.count <= pVM->nem.s.cbVCpuMmap);

    /*
     * We cannot easily act on the exit history here, because the I/O port
     * exit is stateful and the instruction will be completed in the next
     * KVM_RUN call.  There seems no way to avoid this.
     */
    EMHistoryAddExit(pVCpu,
                     pRun->io.count == 1
                     ? (  pRun->io.direction == KVM_EXIT_IO_IN
                        ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_IO_PORT_READ)
                        : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_IO_PORT_WRITE))
                     : (  pRun->io.direction == KVM_EXIT_IO_IN
                        ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_IO_PORT_STR_READ)
                        : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_IO_PORT_STR_WRITE)),
                     pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());

    /*
     * Do the requested job.
     */
    VBOXSTRICTRC    rcStrict;
    RTPTRUNION      uPtrData;
    uPtrData.pu8 = (uint8_t *)pRun + pRun->io.data_offset;
    if (pRun->io.count == 1)
    {
        if (pRun->io.direction == KVM_EXIT_IO_IN)
        {
            uint32_t uValue = 0;
            rcStrict = IOMIOPortRead(pVM, pVCpu, pRun->io.port, &uValue, pRun->io.size);
            Log4(("IOExit/%u: %04x:%08RX64: IN %#x LB %u -> %#x, rcStrict=%Rrc\n",
                  pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip,
                  pRun->io.port, pRun->io.size, uValue, VBOXSTRICTRC_VAL(rcStrict) ));
            if (IOM_SUCCESS(rcStrict))
            {
                if (pRun->io.size == 4)
                    *uPtrData.pu32 = uValue;
                else if (pRun->io.size == 2)
                    *uPtrData.pu16 = (uint16_t)uValue;
                else
                    *uPtrData.pu8  = (uint8_t)uValue;
            }
        }
        else
        {
            uint32_t const uValue = pRun->io.size == 4 ? *uPtrData.pu32
                                  : pRun->io.size == 2 ? *uPtrData.pu16
                                  :                      *uPtrData.pu8;
            rcStrict = IOMIOPortWrite(pVM, pVCpu, pRun->io.port, uValue, pRun->io.size);
            Log4(("IOExit/%u: %04x:%08RX64: OUT %#x, %#x LB %u rcStrict=%Rrc\n",
                  pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip,
                  pRun->io.port, uValue, pRun->io.size, VBOXSTRICTRC_VAL(rcStrict) ));
        }
    }
    else
    {
        uint32_t cTransfers = pRun->io.count;
        if (pRun->io.direction == KVM_EXIT_IO_IN)
        {
            rcStrict = IOMIOPortReadString(pVM, pVCpu, pRun->io.port, uPtrData.pv, &cTransfers, pRun->io.size);
            Log4(("IOExit/%u: %04x:%08RX64: REP INS %#x LB %u * %#x times -> rcStrict=%Rrc cTransfers=%d\n",
                  pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip,
                  pRun->io.port, pRun->io.size, pRun->io.count, VBOXSTRICTRC_VAL(rcStrict), cTransfers ));
        }
        else
        {
            rcStrict = IOMIOPortWriteString(pVM, pVCpu, pRun->io.port, uPtrData.pv, &cTransfers, pRun->io.size);
            Log4(("IOExit/%u: %04x:%08RX64: REP OUTS %#x LB %u * %#x times -> rcStrict=%Rrc cTransfers=%d\n",
                  pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip,
                  pRun->io.port, pRun->io.size, pRun->io.count, VBOXSTRICTRC_VAL(rcStrict), cTransfers ));
        }
        Assert(cTransfers == 0);
    }
    return rcStrict;
}


/**
 * Handles KVM_EXIT_MMIO.
 */
static VBOXSTRICTRC nemHCLnxHandleExitMmio(PVMCC pVM, PVMCPUCC pVCpu, struct kvm_run *pRun)
{
    /*
     * Input validation.
     */
    Assert(pRun->mmio.len <= sizeof(pRun->mmio.data));
    Assert(pRun->mmio.is_write <= 1);

    /*
     * We cannot easily act on the exit history here, because the MMIO port
     * exit is stateful and the instruction will be completed in the next
     * KVM_RUN call.  There seems no way to circumvent this.
     */
    EMHistoryAddExit(pVCpu,
                     pRun->mmio.is_write
                     ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_WRITE)
                     : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_READ),
                     pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());

    /*
     * Do the requested job.
     */
    VBOXSTRICTRC rcStrict;
    if (pRun->mmio.is_write)
    {
        rcStrict = PGMPhysWrite(pVM, pRun->mmio.phys_addr, pRun->mmio.data, pRun->mmio.len, PGMACCESSORIGIN_HM);
        Log4(("MmioExit/%u: %04x:%08RX64: WRITE %#x LB %u, %.*Rhxs -> rcStrict=%Rrc\n",
              pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip,
              pRun->mmio.phys_addr, pRun->mmio.len, pRun->mmio.len, pRun->mmio.data, VBOXSTRICTRC_VAL(rcStrict) ));
    }
    else
    {
        rcStrict = PGMPhysRead(pVM, pRun->mmio.phys_addr, pRun->mmio.data, pRun->mmio.len, PGMACCESSORIGIN_HM);
        Log4(("MmioExit/%u: %04x:%08RX64: READ %#x LB %u -> %.*Rhxs rcStrict=%Rrc\n",
              pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip,
              pRun->mmio.phys_addr, pRun->mmio.len, pRun->mmio.len, pRun->mmio.data, VBOXSTRICTRC_VAL(rcStrict) ));
    }
    return rcStrict;
}


/**
 * Handles KVM_EXIT_RDMSR
 */
static VBOXSTRICTRC nemHCLnxHandleExitRdMsr(PVMCPUCC pVCpu, struct kvm_run *pRun)
{
    /*
     * Input validation.
     */
    Assert(   pRun->msr.reason == KVM_MSR_EXIT_REASON_INVAL
           || pRun->msr.reason == KVM_MSR_EXIT_REASON_UNKNOWN
           || pRun->msr.reason == KVM_MSR_EXIT_REASON_FILTER);

    /*
     * We cannot easily act on the exit history here, because the MSR exit is
     * stateful and the instruction will be completed in the next KVM_RUN call.
     * There seems no way to circumvent this.
     */
    EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MSR_READ),
                     pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());

    /*
     * Do the requested job.
     */
    uint64_t uValue = 0;
    VBOXSTRICTRC rcStrict = CPUMQueryGuestMsr(pVCpu, pRun->msr.index, &uValue);
    pRun->msr.data = uValue;
    if (rcStrict != VERR_CPUM_RAISE_GP_0)
    {
        Log3(("MsrRead/%u: %04x:%08RX64: msr=%#010x (reason=%#x) -> %#RX64 rcStrict=%Rrc\n", pVCpu->idCpu,
              pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip, pRun->msr.index, pRun->msr.reason, uValue, VBOXSTRICTRC_VAL(rcStrict) ));
        pRun->msr.error = 0;
    }
    else
    {
        Log3(("MsrRead/%u: %04x:%08RX64: msr=%#010x (reason%#x)-> %#RX64 rcStrict=#GP!\n", pVCpu->idCpu,
              pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip, pRun->msr.index, pRun->msr.reason, uValue));
        pRun->msr.error = 1;
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * Handles KVM_EXIT_WRMSR
 */
static VBOXSTRICTRC nemHCLnxHandleExitWrMsr(PVMCPUCC pVCpu, struct kvm_run *pRun)
{
    /*
     * Input validation.
     */
    Assert(   pRun->msr.reason == KVM_MSR_EXIT_REASON_INVAL
           || pRun->msr.reason == KVM_MSR_EXIT_REASON_UNKNOWN
           || pRun->msr.reason == KVM_MSR_EXIT_REASON_FILTER);

    /*
     * We cannot easily act on the exit history here, because the MSR exit is
     * stateful and the instruction will be completed in the next KVM_RUN call.
     * There seems no way to circumvent this.
     */
    EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MSR_WRITE),
                     pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());

    /*
     * Do the requested job.
     */
    VBOXSTRICTRC rcStrict = CPUMSetGuestMsr(pVCpu, pRun->msr.index, pRun->msr.data);
    if (rcStrict != VERR_CPUM_RAISE_GP_0)
    {
        Log3(("MsrWrite/%u: %04x:%08RX64: msr=%#010x := %#RX64 (reason=%#x) -> rcStrict=%Rrc\n", pVCpu->idCpu,
              pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip, pRun->msr.index, pRun->msr.data, pRun->msr.reason, VBOXSTRICTRC_VAL(rcStrict) ));
        pRun->msr.error = 0;
    }
    else
    {
        Log3(("MsrWrite/%u: %04x:%08RX64: msr=%#010x := %#RX64 (reason%#x)-> rcStrict=#GP!\n", pVCpu->idCpu,
              pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip, pRun->msr.index, pRun->msr.data, pRun->msr.reason));
        pRun->msr.error = 1;
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}



static VBOXSTRICTRC nemHCLnxHandleExit(PVMCC pVM, PVMCPUCC pVCpu, struct kvm_run *pRun, bool *pfStatefulExit)
{
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitTotal);
    switch (pRun->exit_reason)
    {
        case KVM_EXIT_EXCEPTION:
            AssertFailed();
            break;

        case KVM_EXIT_IO:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitIo);
            *pfStatefulExit = true;
            return nemHCLnxHandleExitIo(pVM, pVCpu, pRun);

        case KVM_EXIT_MMIO:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitMmio);
            *pfStatefulExit = true;
            return nemHCLnxHandleExitMmio(pVM, pVCpu, pRun);

        case KVM_EXIT_IRQ_WINDOW_OPEN:
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_INTTERRUPT_WINDOW),
                             pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitIrqWindowOpen);
            Log5(("IrqWinOpen/%u: %d\n", pVCpu->idCpu, pRun->request_interrupt_window));
            pRun->request_interrupt_window = 0;
            return VINF_SUCCESS;

        case KVM_EXIT_SET_TPR:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitSetTpr);
            AssertFailed();
            break;

        case KVM_EXIT_TPR_ACCESS:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitTprAccess);
            AssertFailed();
            break;

        case KVM_EXIT_X86_RDMSR:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitRdMsr);
            *pfStatefulExit = true;
            return nemHCLnxHandleExitRdMsr(pVCpu, pRun);

        case KVM_EXIT_X86_WRMSR:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitWrMsr);
            *pfStatefulExit = true;
            return nemHCLnxHandleExitWrMsr(pVCpu, pRun);

        case KVM_EXIT_HLT:
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_HALT),
                             pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitHalt);
            Log5(("Halt/%u\n", pVCpu->idCpu));
            return VINF_EM_HALT;

        case KVM_EXIT_INTR: /* EINTR */
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_INTERRUPTED),
                             pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitIntr);
            Log5(("Intr/%u\n", pVCpu->idCpu));
            return VINF_SUCCESS;

        case KVM_EXIT_HYPERCALL:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitHypercall);
            AssertFailed();
            break;

        case KVM_EXIT_DEBUG:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitDebug);
            AssertFailed();
            break;

        case KVM_EXIT_SYSTEM_EVENT:
            AssertFailed();
            break;
        case KVM_EXIT_IOAPIC_EOI:
            AssertFailed();
            break;
        case KVM_EXIT_HYPERV:
            AssertFailed();
            break;

        case KVM_EXIT_DIRTY_RING_FULL:
            AssertFailed();
            break;
        case KVM_EXIT_AP_RESET_HOLD:
            AssertFailed();
            break;
        case KVM_EXIT_X86_BUS_LOCK:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitBusLock);
            AssertFailed();
            break;


        case KVM_EXIT_SHUTDOWN:
            AssertFailed();
            break;

        case KVM_EXIT_FAIL_ENTRY:
            LogRel(("NEM: KVM_EXIT_FAIL_ENTRY! hardware_entry_failure_reason=%#x cpu=%#x\n",
                    pRun->fail_entry.hardware_entry_failure_reason, pRun->fail_entry.cpu));
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_FAILED_ENTRY),
                             pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());
            return VERR_NEM_IPE_1;

        case KVM_EXIT_INTERNAL_ERROR:
            /* we're counting sub-reasons inside the function. */
            return nemR3LnxHandleInternalError(pVCpu, pRun);

        /*
         * Foreign and unknowns.
         */
        case KVM_EXIT_NMI:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_NMI on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_EPR:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_EPR on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_WATCHDOG:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_WATCHDOG on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_ARM_NISV:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_ARM_NISV on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_STSI:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_STSI on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_TSCH:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_TSCH on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_OSI:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_OSI on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_PAPR_HCALL:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_PAPR_HCALL on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_UCONTROL:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_UCONTROL on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_DCR:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_DCR on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_SIEIC:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_SIEIC on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_RESET:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_RESET on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_UNKNOWN:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_UNKNOWN on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        case KVM_EXIT_XEN:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_XEN on VCpu #%u at %04x:%RX64!\n", pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
        default:
            AssertLogRelMsgFailedReturn(("Unknown exit reason %u on VCpu #%u at %04x:%RX64!\n", pRun->exit_reason, pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip), VERR_NEM_IPE_1);
    }

    RT_NOREF(pVM, pVCpu, pRun);
    return VERR_NOT_IMPLEMENTED;
}


VBOXSTRICTRC nemR3NativeRunGC(PVM pVM, PVMCPU pVCpu)
{
    /*
     * Try switch to NEM runloop state.
     */
    if (VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED))
    { /* likely */ }
    else
    {
        VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED_EXEC_NEM_CANCELED);
        LogFlow(("NEM/%u: returning immediately because canceled\n", pVCpu->idCpu));
        return VINF_SUCCESS;
    }

    /*
     * The run loop.
     */
    struct kvm_run * const  pRun                = pVCpu->nem.s.pRun;
    const bool              fSingleStepping     = DBGFIsStepping(pVCpu);
    VBOXSTRICTRC            rcStrict            = VINF_SUCCESS;
    bool                    fStatefulExit       = false;  /* For MMIO and IO exits. */
    for (unsigned iLoop = 0;; iLoop++)
    {
        /*
         * Pending interrupts or such?  Need to check and deal with this prior
         * to the state syncing.
         */
        if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_UPDATE_APIC | VMCPU_FF_INTERRUPT_PIC
                                     | VMCPU_FF_INTERRUPT_NMI  | VMCPU_FF_INTERRUPT_SMI))
        {
            /* Try inject interrupt. */
            rcStrict = nemHCLnxHandleInterruptFF(pVM, pVCpu, pRun);
            if (rcStrict == VINF_SUCCESS)
            { /* likely */ }
            else
            {
                LogFlow(("NEM/%u: breaking: nemHCLnxHandleInterruptFF -> %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnStatus);
                break;
            }
        }

        /*
         * Do not execute in KVM if the A20 isn't enabled.
         */
        if (PGMPhysIsA20Enabled(pVCpu))
        { /* likely */ }
        else
        {
            rcStrict = VINF_EM_RESCHEDULE_REM;
            LogFlow(("NEM/%u: breaking: A20 disabled\n", pVCpu->idCpu));
            break;
        }

        /*
         * Ensure KVM has the whole state.
         */
        if ((pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL) != CPUMCTX_EXTRN_ALL)
        {
            int rc2 = nemHCLnxExportState(pVM, pVCpu, &pVCpu->cpum.GstCtx, pRun);
            AssertRCReturn(rc2, rc2);
        }

        /*
         * Poll timers and run for a bit.
         *
         * With the VID approach (ring-0 or ring-3) we can specify a timeout here,
         * so we take the time of the next timer event and uses that as a deadline.
         * The rounding heuristics are "tuned" so that rhel5 (1K timer) will boot fine.
         */
        /** @todo See if we cannot optimize this TMTimerPollGIP by only redoing
         *        the whole polling job when timers have changed... */
        uint64_t       offDeltaIgnored;
        uint64_t const nsNextTimerEvt = TMTimerPollGIP(pVM, pVCpu, &offDeltaIgnored); NOREF(nsNextTimerEvt);
        if (   !VM_FF_IS_ANY_SET(pVM, VM_FF_EMT_RENDEZVOUS | VM_FF_TM_VIRTUAL_SYNC)
            && !VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HM_TO_R3_MASK))
        {
            if (VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM_WAIT, VMCPUSTATE_STARTED_EXEC_NEM))
            {
                LogFlow(("NEM/%u: Entry @ %04x:%08RX64 IF=%d EFL=%#RX64 SS:RSP=%04x:%08RX64 cr0=%RX64\n",
                         pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip,
                         !!(pRun->s.regs.regs.rflags & X86_EFL_IF), pRun->s.regs.regs.rflags,
                         pRun->s.regs.sregs.ss.selector, pRun->s.regs.regs.rsp, pRun->s.regs.sregs.cr0));
                TMNotifyStartOfExecution(pVM, pVCpu);

                int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_RUN, 0UL);

                VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED_EXEC_NEM_WAIT);
                TMNotifyEndOfExecution(pVM, pVCpu, ASMReadTSC());

#ifdef LOG_ENABLED
                if (LogIsFlowEnabled())
                {
                    struct kvm_mp_state MpState = {UINT32_MAX};
                    ioctl(pVCpu->nem.s.fdVCpu, KVM_GET_MP_STATE, &MpState);
                    LogFlow(("NEM/%u: Exit  @ %04x:%08RX64 IF=%d EFL=%#RX64 CR8=%#x Reason=%#x IrqReady=%d Flags=%#x %#lx\n", pVCpu->idCpu,
                             pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip, pRun->if_flag,
                             pRun->s.regs.regs.rflags, pRun->s.regs.sregs.cr8, pRun->exit_reason,
                             pRun->ready_for_interrupt_injection, pRun->flags, MpState.mp_state));
                }
#endif
                fStatefulExit = false;
                if (RT_LIKELY(rcLnx == 0 || errno == EINTR))
                {
                    /*
                     * Deal with the exit.
                     */
                    rcStrict = nemHCLnxHandleExit(pVM, pVCpu, pRun, &fStatefulExit);
                    if (rcStrict == VINF_SUCCESS)
                    { /* hopefully likely */ }
                    else
                    {
                        LogFlow(("NEM/%u: breaking: nemHCLnxHandleExit -> %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
                        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnStatus);
                        break;
                    }
                }
                else
                {
                    int rc2 = RTErrConvertFromErrno(errno);
                    AssertLogRelMsgFailedReturn(("KVM_RUN failed: rcLnx=%d errno=%u rc=%Rrc\n", rcLnx, errno, rc2), rc2);
                }

                /*
                 * If no relevant FFs are pending, loop.
                 */
                if (   !VM_FF_IS_ANY_SET(   pVM,   !fSingleStepping ? VM_FF_HP_R0_PRE_HM_MASK    : VM_FF_HP_R0_PRE_HM_STEP_MASK)
                    && !VMCPU_FF_IS_ANY_SET(pVCpu, !fSingleStepping ? VMCPU_FF_HP_R0_PRE_HM_MASK : VMCPU_FF_HP_R0_PRE_HM_STEP_MASK) )
                { /* likely */ }
                else
                {

                    /** @todo Try handle pending flags, not just return to EM loops.  Take care
                     *        not to set important RCs here unless we've handled an exit. */
                    LogFlow(("NEM/%u: breaking: pending FF (%#x / %#RX64)\n",
                             pVCpu->idCpu, pVM->fGlobalForcedActions, (uint64_t)pVCpu->fLocalForcedActions));
                    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnFFPost);
                    break;
                }
            }
            else
            {
                LogFlow(("NEM/%u: breaking: canceled %d (pre exec)\n", pVCpu->idCpu, VMCPU_GET_STATE(pVCpu) ));
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnCancel);
                break;
            }
        }
        else
        {
            LogFlow(("NEM/%u: breaking: pending FF (pre exec)\n", pVCpu->idCpu));
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnFFPre);
            break;
        }
    } /* the run loop */


    /*
     * If the last exit was stateful, commit the state we provided before
     * returning to the EM loop so we have a consistent state and can safely
     * be rescheduled and whatnot.  This may require us to make multiple runs
     * for larger MMIO and I/O operations. Sigh^3.
     *
     * Note! There is no 'ing way to reset the kernel side completion callback
     *       for these stateful i/o exits.  Very annoying interface.
     */
    /** @todo check how this works with string I/O and string MMIO. */
    if (fStatefulExit && RT_SUCCESS(rcStrict))
    {
        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn);
        uint32_t const uOrgExit = pRun->exit_reason;
        for (uint32_t i = 0; ; i++)
        {
            pRun->immediate_exit = 1;
            int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_RUN, 0UL);
            Log(("NEM/%u: Flushed stateful exit -> %d/%d exit_reason=%d\n", pVCpu->idCpu, rcLnx, errno, pRun->exit_reason));
            if (rcLnx == -1 && errno == EINTR)
            {
                switch (i)
                {
                    case 0: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn1Loop); break;
                    case 1: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn2Loops); break;
                    case 2: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn3Loops); break;
                    default: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn4PlusLoops); break;
                }
                break;
            }
            AssertLogRelMsgBreakStmt(rcLnx == 0 && pRun->exit_reason == uOrgExit,
                                     ("rcLnx=%d errno=%d exit_reason=%d uOrgExit=%d\n", rcLnx, errno, pRun->exit_reason, uOrgExit),
                                     rcStrict = VERR_NEM_IPE_6);
            VBOXSTRICTRC rcStrict2 = nemHCLnxHandleExit(pVM, pVCpu, pRun, &fStatefulExit);
            if (rcStrict2 == VINF_SUCCESS || rcStrict2 == rcStrict)
            { /* likely */ }
            else if (RT_FAILURE(rcStrict2))
            {
                rcStrict = rcStrict2;
                break;
            }
            else
            {
                AssertLogRelMsgBreakStmt(rcStrict == VINF_SUCCESS,
                                         ("rcStrict=%Rrc rcStrict2=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict), VBOXSTRICTRC_VAL(rcStrict2)),
                                         rcStrict = VERR_NEM_IPE_7);
                rcStrict = rcStrict2;
            }
        }
        pRun->immediate_exit = 0;
    }

    /*
     * If the CPU is running, make sure to stop it before we try sync back the
     * state and return to EM.  We don't sync back the whole state if we can help it.
     */
    if (!VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_EXEC_NEM))
        VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_EXEC_NEM_CANCELED);

    if (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL)
    {
        /* Try anticipate what we might need. */
        uint64_t fImport = CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI /* Required for processing APIC,PIC,NMI & SMI FFs. */
                         | IEM_CPUMCTX_EXTRN_MUST_MASK /*?*/;
        if (   (rcStrict >= VINF_EM_FIRST && rcStrict <= VINF_EM_LAST)
            || RT_FAILURE(rcStrict))
            fImport = CPUMCTX_EXTRN_ALL;
# ifdef IN_RING0 /* Ring-3 I/O port access optimizations: */
        else if (   rcStrict == VINF_IOM_R3_IOPORT_COMMIT_WRITE
                 || rcStrict == VINF_EM_PENDING_R3_IOPORT_WRITE)
            fImport = CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RFLAGS;
        else if (rcStrict == VINF_EM_PENDING_R3_IOPORT_READ)
            fImport = CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RFLAGS;
# endif
        else if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_PIC | VMCPU_FF_INTERRUPT_APIC
                                          | VMCPU_FF_INTERRUPT_NMI | VMCPU_FF_INTERRUPT_SMI))
            fImport |= IEM_CPUMCTX_EXTRN_XCPT_MASK;

        if (pVCpu->cpum.GstCtx.fExtrn & fImport)
        {
            int rc2 = nemHCLnxImportState(pVCpu, fImport, &pVCpu->cpum.GstCtx, pRun);
            if (RT_SUCCESS(rc2))
                pVCpu->cpum.GstCtx.fExtrn &= ~fImport;
            else if (RT_SUCCESS(rcStrict))
                rcStrict = rc2;
            if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL))
                pVCpu->cpum.GstCtx.fExtrn = 0;
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturn);
        }
        else
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturnSkipped);
    }
    else
    {
        pVCpu->cpum.GstCtx.fExtrn = 0;
        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturnSkipped);
    }

    LogFlow(("NEM/%u: %04x:%08RX64 efl=%#08RX64 => %Rrc\n", pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
             pVCpu->cpum.GstCtx.rflags.u, VBOXSTRICTRC_VAL(rcStrict) ));
    return rcStrict;
}


/** @page pg_nem_linux NEM/linux - Native Execution Manager, Linux.
 *
 * This is using KVM.
 *
 */

