/* $Id: VBoxVMM.d 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVMM - Static dtrace probes.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

provider vboxvmm
{
    probe em__state__changed(struct VMCPU *a_pVCpu, int a_enmOldState, int a_enmNewState, int a_rc);
    /*^^VMM-ALT-TP: "%d -> %d (rc=%d)", a_enmOldState, a_enmNewState, a_rc */

    probe em__state__unchanged(struct VMCPU *a_pVCpu, int a_enmState, int a_rc);
    /*^^VMM-ALT-TP: "%d (rc=%d)", a_enmState, a_rc */

    probe em__raw__run__pre(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /*^^VMM-ALT-TP: "%04x:%08llx", (a_pCtx)->cs, (a_pCtx)->rip */

    probe em__raw__run__ret(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, int a_rc);
    /*^^VMM-ALT-TP: "%04x:%08llx rc=%d", (a_pCtx)->cs, (a_pCtx)->rip, (a_rc) */

    probe em__ff__high(struct VMCPU *a_pVCpu, uint32_t a_fGlobal, uint64_t a_fLocal, int a_rc);
    /*^^VMM-ALT-TP: "vm=%#x cpu=%#x rc=%d", (a_fGlobal), (a_fLocal), (a_rc) */

    probe em__ff__all(struct VMCPU *a_pVCpu, uint32_t a_fGlobal, uint64_t a_fLocal, int a_rc);
    /*^^VMM-ALT-TP: "vm=%#x cpu=%#x rc=%d", (a_fGlobal), (a_fLocal), (a_rc) */

    probe em__ff__all__ret(struct VMCPU *a_pVCpu, int a_rc);
    /*^^VMM-ALT-TP: "%d", (a_rc) */

    probe em__ff__raw(struct VMCPU *a_pVCpu, uint32_t a_fGlobal, uint64_t a_fLocal);
    /*^^VMM-ALT-TP: "vm=%#x cpu=%#x", (a_fGlobal), (a_fLocal) */

    probe em__ff__raw_ret(struct VMCPU *a_pVCpu, int a_rc);
    /*^^VMM-ALT-TP: "%d", (a_rc) */

    probe pdm__irq__get( struct VMCPU *a_pVCpu, uint32_t a_uTag, uint32_t a_idSource, uint32_t a_iIrq);
    probe pdm__irq__high(struct VMCPU *a_pVCpu, uint32_t a_uTag, uint32_t a_idSource);
    probe pdm__irq__low( struct VMCPU *a_pVCpu, uint32_t a_uTag, uint32_t a_idSource);
    probe pdm__irq__hilo(struct VMCPU *a_pVCpu, uint32_t a_uTag, uint32_t a_idSource);


    probe r0__gvmm__vm__created(void *a_pGVM, void *a_pVM, uint32_t a_Pid, void *a_hEMT0, uint32_t a_cCpus);
    probe r0__hmsvm__vmexit(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint64_t a_ExitCode, struct SVMVMCB *a_pVmcb);
    probe r0__hmvmx__vmexit(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint64_t a_ExitReason, uint64_t a_ExitQualification);
    probe r0__hmvmx__vmexit__noctx(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pIncompleteCtx, uint64_t a_ExitReason);

    probe r0__vmm__return__to__ring3__rc(struct VMCPU *a_pVCpu, struct CPUMCTX *p_Ctx, int a_rc);
    probe r0__vmm__return__to__ring3__hm(struct VMCPU *a_pVCpu, struct CPUMCTX *p_Ctx, int a_rc);
    probe r0__vmm__return__to__ring3__nem(struct VMCPU *a_pVCpu, struct CPUMCTX *p_Ctx, int a_rc);


    /** @name CPU Exception probes
     * These probes will intercept guest CPU exceptions as best we
     * can.  In some execution modes some of these probes may also
     * see non-guest exceptions as we don't try distiguish between
     * virtualization and guest exceptions before firing the probes.
     *
     * Using these probes may have a performance impact on guest
     * activities involving lots of exceptions.
     * @{
     */
    /** \#DE - integer divide error.  */
    probe xcpt__de(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** \#DB - debug fault / trap.  */
    probe xcpt__db(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint64_t a_dr6);
    /** \#BP - breakpoint (INT3).  */
    probe xcpt__bp(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** \#OF - overflow (INTO).  */
    probe xcpt__of(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** \#BR - bound range exceeded (BOUND).  */
    probe xcpt__br(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** \#UD - undefined opcode.  */
    probe xcpt__ud(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** \#NM - FPU not avaible and more.  */
    probe xcpt__nm(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** \#DF - double fault.  */
    probe xcpt__df(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** \#TS - TSS related fault.  */
    probe xcpt__ts(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t a_uErr);
    /** \#NP - segment not present.  */
    probe xcpt__np(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t a_uErr);
    /** \#SS - stack segment fault.  */
    probe xcpt__ss(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t a_uErr);
    /** \#GP - general protection fault.  */
    probe xcpt__gp(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t a_uErr);
    /** \#PF - page fault.  */
    probe xcpt__pf(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t a_uErr, uint64_t a_cr2);
    /** \#MF - math fault (FPU).  */
    probe xcpt__mf(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** \#AC - alignment check.  */
    probe xcpt__ac(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** \#XF - SIMD floating point exception.  */
    probe xcpt__xf(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** \#VE - virtualization exception.  */
    probe xcpt__ve(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** \#SX - security exception.  */
    probe xcpt__sx(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t a_uErr);
    /** @} */


    /** Software interrupt (INT XXh).
     * It may be very difficult to implement this probe when using hardware
     * virtualization, so maybe we have to drop it... */
    probe int__software(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint8_t a_iInterrupt);
    /** Hardware interrupt being dispatched.
     *
     * Relates to pdm__irq__get ...
     */
    probe int__hardware(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint8_t a_iInterrupt, uint32_t a_uTag, uint32_t a_idSource);

    /** @name Instruction probes
     * These are instructions normally related to VM exits.  These
     * probes differs from the exit probes in that we will try make
     * these instructions cause exits and fire the probe whenever
     * they are executed by the guest.  This means some of these
     * probes will have a noticable performance impact (like
     * instr__pause).
     * @{ */
    /** Instruction: HALT */
    probe instr__halt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: MWAIT */
    probe instr__mwait(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: MONITOR */
    probe instr__monitor(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: CPUID instruction (missing stuff in raw-mode). */
    probe instr__cpuid(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t uLeaf, uint32_t uSubLeaf);
    /** Instruction: INVD  */
    probe instr__invd(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: WBINVD */
    probe instr__wbinvd(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: INVLPG */
    probe instr__invlpg(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: RDTSC  */
    probe instr__rdtsc(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: RDTSCP */
    probe instr__rdtscp(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: RDPMC  */
    probe instr__rdpmc(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: RDMSR  */
    probe instr__rdmsr(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t a_idMsr);
    /** Instruction: WRMSR  */
    probe instr__wrmsr(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t a_idMsr, uint64_t a_uValue);
    /** Instruction: CRx read instruction (missing smsw in raw-mode,
     *  and reads in general in VT-x). */
    probe instr__crx__read(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint8_t a_iReg);
    /** Instruction: CRx write instruction. */
    probe instr__crx__write(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint8_t a_iReg);
    /** Instruction: DRx read instruction. */
    probe instr__drx__read(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint8_t a_iReg);
    /** Instruction: DRx write instruction. */
    probe instr__drx__write(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint8_t a_iReg);
    /** Instruction: PAUSE instruction (not in raw-mode). */
    probe instr__pause(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: XSETBV */
    probe instr__xsetbv(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: SIDT  */
    probe instr__sidt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: LIDT */
    probe instr__lidt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: SGDT */
    probe instr__sgdt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: LGDT */
    probe instr__lgdt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: SLDT */
    probe instr__sldt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: LLDT */
    probe instr__lldt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: STR */
    probe instr__str(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: LTR */
    probe instr__ltr(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: GETSEC */
    probe instr__getsec(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: RSM */
    probe instr__rsm(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: RDRAND */
    probe instr__rdrand(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: RDSEED */
    probe instr__rdseed(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: XSAVES */
    probe instr__xsaves(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: XRSTORS  */
    probe instr__xrstors(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VMCALL (intel) or VMMCALL (AMD) instruction. */
    probe instr__vmm__call(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);

    /** Instruction: VT-x VMCLEAR instruction. */
    probe instr__vmx__vmclear(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x VMLAUNCH */
    probe instr__vmx__vmlaunch(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x VMPTRLD */
    probe instr__vmx__vmptrld(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x VMPTRST */
    probe instr__vmx__vmptrst(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x VMREAD */
    probe instr__vmx__vmread(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x VMRESUME */
    probe instr__vmx__vmresume(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x VMWRITE */
    probe instr__vmx__vmwrite(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x VMXOFF */
    probe instr__vmx__vmxoff(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x VMXON */
    probe instr__vmx__vmxon(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x VMFUNC */
    probe instr__vmx__vmfunc(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x INVEPT */
    probe instr__vmx__invept(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x INVVPID */
    probe instr__vmx__invvpid(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: VT-x INVPCID */
    probe instr__vmx__invpcid(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);

    /** Instruction: AMD-V VMRUN */
    probe instr__svm__vmrun(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: AMD-V VMLOAD */
    probe instr__svm__vmload(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: AMD-V VMSAVE */
    probe instr__svm__vmsave(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: AMD-V STGI */
    probe instr__svm__stgi(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** Instruction: AMD-V CLGI */
    probe instr__svm__clgi(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** @} */


    /** @name VM exit probes
     * These are named exits with (in some cases at least) useful
     * information as arguments.  Unlike the instruction probes,
     * these will not change the number of VM exits and have much
     * less of an impact on VM performance.
     * @{ */
    /** VM Exit: Task switch. */
    probe exit__task__switch(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: HALT instruction.
     * @todo not yet implemented. */
    probe exit__halt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: MWAIT instruction. */
    probe exit__mwait(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: MONITOR instruction. */
    probe exit__monitor(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: CPUID instruction (missing stuff in raw-mode). */
    probe exit__cpuid(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t uLeaf, uint32_t uSubLeaf);
    /** VM Exit: INVD instruction.  */
    probe exit__invd(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: WBINVD instruction. */
    probe exit__wbinvd(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: INVLPG instruction. */
    probe exit__invlpg(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: RDTSC instruction.  */
    probe exit__rdtsc(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: RDTSCP instruction. */
    probe exit__rdtscp(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: RDPMC instruction.  */
    probe exit__rdpmc(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: RDMSR instruction.  */
    probe exit__rdmsr(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t a_idMsr);
    /** VM Exit: WRMSR instruction.  */
    probe exit__wrmsr(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint32_t a_idMsr, uint64_t a_uValue);
    /** VM Exit: CRx read instruction (missing smsw in raw-mode,
     *  and reads in general in VT-x). */
    probe exit__crx__read(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint8_t a_iReg);
    /** VM Exit: CRx write instruction. */
    probe exit__crx__write(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint8_t a_iReg);
    /** VM Exit: DRx read instruction. */
    probe exit__drx__read(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint8_t a_iReg);
    /** VM Exit: DRx write instruction. */
    probe exit__drx__write(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx, uint8_t a_iReg);
    /** VM Exit: PAUSE instruction (not in raw-mode). */
    probe exit__pause(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: XSETBV instruction. */
    probe exit__xsetbv(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: SIDT instruction.  */
    probe exit__sidt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: LIDT instruction. */
    probe exit__lidt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: SGDT instruction. */
    probe exit__sgdt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: LGDT instruction. */
    probe exit__lgdt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: SLDT instruction. */
    probe exit__sldt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: LLDT instruction. */
    probe exit__lldt(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: STR instruction. */
    probe exit__str(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: LTR instruction. */
    probe exit__ltr(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: GETSEC instruction. */
    probe exit__getsec(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: RSM instruction. */
    probe exit__rsm(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: RDRAND instruction. */
    probe exit__rdrand(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: RDSEED instruction. */
    probe exit__rdseed(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: XSAVES instruction. */
    probe exit__xsaves(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: XRSTORS instruction.  */
    probe exit__xrstors(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VMCALL (intel) or VMMCALL (AMD) instruction. */
    probe exit__vmm__call(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);

    /** VM Exit: VT-x VMCLEAR instruction. */
    probe exit__vmx__vmclear(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x VMLAUNCH instruction. */
    probe exit__vmx__vmlaunch(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x VMPTRLD instruction. */
    probe exit__vmx__vmptrld(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x VMPTRST instruction. */
    probe exit__vmx__vmptrst(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x VMREAD instruction. */
    probe exit__vmx__vmread(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x VMRESUME instruction. */
    probe exit__vmx__vmresume(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x VMWRITE instruction. */
    probe exit__vmx__vmwrite(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x VMXOFF instruction. */
    probe exit__vmx__vmxoff(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x VMXON instruction. */
    probe exit__vmx__vmxon(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x VMFUNC instruction. */
    probe exit__vmx__vmfunc(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x INVEPT instruction. */
    probe exit__vmx__invept(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x INVVPID instruction. */
    probe exit__vmx__invvpid(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x INVPCID instruction. */
    probe exit__vmx__invpcid(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x EPT violation. */
    probe exit__vmx__ept__violation(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x EPT misconfiguration. */
    probe exit__vmx__ept__misconfig(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x Virtual APIC page access. */
    probe exit__vmx__vapic__access(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: VT-x Virtual APIC page write needing virtualizing. */
    probe exit__vmx__vapic__write(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);

    /** VM Exit: AMD-V VMRUN instruction. */
    probe exit__svm__vmrun(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: AMD-V VMLOAD instruction. */
    probe exit__svm__vmload(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: AMD-V VMSAVE instruction. */
    probe exit__svm__vmsave(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: AMD-V STGI instruction. */
    probe exit__svm__stgi(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** VM Exit: AMD-V CLGI instruction. */
    probe exit__svm__clgi(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** @} */


    /** @name Misc VT-x and AMD-V execution events.
     * @{ */
    /** VT-x: Split-lock \#AC triggered by host having detection enabled. */
    probe vmx__split__lock(struct VMCPU *a_pVCpu, struct CPUMCTX *a_pCtx);
    /** @} */


    /** @name IPRT tracepoints we link in.
     * @{ */
    probe iprt__critsect__entered(void *a_pvCritSect, const char *a_pszLaterNm, int32_t a_cLockers, uint32_t a_cNestings);
    probe iprt__critsect__leaving(void *a_pvCritSect, const char *a_pszLaterNm, int32_t a_cLockers, uint32_t a_cNestings);
    probe iprt__critsect__waiting(void *a_pvCritSect, const char *a_pszLaterNm, int32_t a_cLockers, void *a_pvNativeThreadOwner);
    probe iprt__critsect__busy(   void *a_pvCritSect, const char *a_pszLaterNm, int32_t a_cLockers, void *a_pvNativeThreadOwner);

    probe iprt__critsectrw__excl_entered(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cNestings,
                                         uint32_t a_cWaitingReaders, uint32_t cWriters);
    probe iprt__critsectrw__excl_leaving(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cNestings,
                                         uint32_t a_cWaitingReaders, uint32_t cWriters);
    probe iprt__critsectrw__excl_waiting(void *a_pvCritSect, const char *a_pszLaterNm, uint8_t a_fWriteMode, uint32_t a_cWaitingReaders,
                                         uint32_t a_cReaders, uint32_t a_cWriters, void *a_pvNativeOwnerThread);
    probe iprt__critsectrw__excl_busy(   void *a_pvCritSect, const char *a_pszLaterNm, uint8_t a_fWriteMode, uint32_t a_cWaitingReaders,
                                         uint32_t a_cReaders, uint32_t a_cWriters, void *a_pvNativeOwnerThread);
    probe iprt__critsectrw__excl_entered_shared(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cNestings,
                                                uint32_t a_cWaitingReaders, uint32_t a_cWriters);
    probe iprt__critsectrw__excl_leaving_shared(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cNestings,
                                                uint32_t a_cWaitingReaders, uint32_t a_cWriters);
    probe iprt__critsectrw__shared_entered(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cReaders, uint32_t a_cNestings);
    probe iprt__critsectrw__shared_leaving(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cReaders, uint32_t a_cNestings);
    probe iprt__critsectrw__shared_waiting(void *a_pvCritSect, const char *a_pszLaterNm, void *a_pvNativeThreadOwner,
                                           uint32_t cWaitingReaders, uint32_t cWriters);
    probe iprt__critsectrw__shared_busy(   void *a_pvCritSect, const char *a_pszLaterNm, void *a_pvNativeThreadOwner,
                                           uint32_t a_cWaitingReaders, uint32_t a_cWriters);

    /** @} */
};

#pragma D attributes Evolving/Evolving/Common provider vboxvmm provider
#pragma D attributes Private/Private/Unknown  provider vboxvmm module
#pragma D attributes Private/Private/Unknown  provider vboxvmm function
#pragma D attributes Evolving/Evolving/Common provider vboxvmm name
#pragma D attributes Evolving/Evolving/Common provider vboxvmm args

