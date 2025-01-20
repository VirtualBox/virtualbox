; $Id: HMR0A.asm 107963 2025-01-20 16:26:58Z knut.osmundsen@oracle.com $
;; @file
; HM - Ring-0 VMX, SVM world-switch and helper routines.
;

;
; Copyright (C) 2006-2024 Oracle and/or its affiliates.
;
; This file is part of VirtualBox base platform packages, as
; available from https://www.virtualbox.org.
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation, in version 3 of the
; License.
;
; This program is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, see <https://www.gnu.org/licenses>.
;
; SPDX-License-Identifier: GPL-3.0-only
;

;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
;%define RT_ASM_WITH_SEH64  - trouble with SEH, alignment and (probably) 2nd pass optimizations.
%define RT_ASM_WITH_SEH64_ALT ; Use asmdefs.mac hackery for manually emitting unwind info.
%include "VBox/asmdefs.mac"
%include "VBox/err.mac"
%include "VBox/vmm/hm_vmx.mac"
%include "VBox/vmm/cpum.mac"
%include "VBox/vmm/gvm.mac"
%include "iprt/x86.mac"
%include "HMInternal.mac"

%ifndef RT_ARCH_AMD64
 %error AMD64 only.
%endif


;*********************************************************************************************************************************
;*  Defined Constants And Macros                                                                                                 *
;*********************************************************************************************************************************
;; The offset of the XMM registers in X86FXSTATE.
; Use define because I'm too lazy to convert the struct.
%define XMM_OFF_IN_X86FXSTATE   160

;; Spectre filler for 64-bit mode.
; Choosen to be an invalid address (also with 5 level paging).
%define SPECTRE_FILLER          0x02204204207fffff

;;
; Determine skipping restoring of GDTR, IDTR, TR across VMX non-root operation.
;
; @note This is normally done by hmR0VmxExportHostSegmentRegs and VMXRestoreHostState,
;       so much of this is untested code.
; @{
%define VMX_SKIP_GDTR
%define VMX_SKIP_TR
%define VBOX_SKIP_RESTORE_SEG
%ifdef RT_OS_DARWIN
 ; Load the NULL selector into DS, ES, FS and GS on 64-bit darwin so we don't
 ; risk loading a stale LDT value or something invalid.
 %define HM_64_BIT_USE_NULL_SEL
 ; Darwin (Mavericks) uses IDTR limit to store the CPU number so we need to always restore it.
 ; See @bugref{6875}.
 %undef VMX_SKIP_IDTR
%else
 %define VMX_SKIP_IDTR
%endif
;; @}

;; @def CALLEE_PRESERVED_REGISTER_COUNT
; Number of registers pushed by PUSH_CALLEE_PRESERVED_REGISTERS
%ifdef ASM_CALL64_GCC
 %define CALLEE_PRESERVED_REGISTER_COUNT 5
%else
 %define CALLEE_PRESERVED_REGISTER_COUNT 7
%endif

;; @def PUSH_CALLEE_PRESERVED_REGISTERS
; Macro for pushing all GPRs we must preserve for the caller.
%macro PUSH_CALLEE_PRESERVED_REGISTERS 0
        push    r15
        SEH64_PUSH_GREG r15
        %assign cbFrame         cbFrame + 8
        %assign frm_saved_r15   -cbFrame

        push    r14
        SEH64_PUSH_GREG r14
        %assign cbFrame         cbFrame + 8
        %assign frm_saved_r14   -cbFrame

        push    r13
        SEH64_PUSH_GREG r13
        %assign cbFrame         cbFrame + 8
        %assign frm_saved_r13   -cbFrame

        push    r12
        SEH64_PUSH_GREG r12
        %assign cbFrame         cbFrame + 8
        %assign frm_saved_r12   -cbFrame

        push    rbx
        SEH64_PUSH_GREG rbx
        %assign cbFrame         cbFrame + 8
        %assign frm_saved_rbx   -cbFrame

 %ifdef ASM_CALL64_MSC
        push    rsi
        SEH64_PUSH_GREG rsi
        %assign cbFrame         cbFrame + 8
        %assign frm_saved_rsi   -cbFrame

        push    rdi
        SEH64_PUSH_GREG rdi
        %assign cbFrame         cbFrame + 8
        %assign frm_saved_rdi   -cbFrame
 %endif
%endmacro

;; @def POP_CALLEE_PRESERVED_REGISTERS
; Counterpart to PUSH_CALLEE_PRESERVED_REGISTERS for use in the epilogue.
%macro POP_CALLEE_PRESERVED_REGISTERS 0
 %ifdef ASM_CALL64_MSC
        pop     rdi
        %assign cbFrame         cbFrame - 8
        %undef  frm_saved_rdi

        pop     rsi
        %assign cbFrame         cbFrame - 8
        %undef  frm_saved_rsi
 %endif
        pop     rbx
        %assign cbFrame         cbFrame - 8
        %undef  frm_saved_rbx

        pop     r12
        %assign cbFrame         cbFrame - 8
        %undef  frm_saved_r12

        pop     r13
        %assign cbFrame         cbFrame - 8
        %undef  frm_saved_r13

        pop     r14
        %assign cbFrame         cbFrame - 8
        %undef  frm_saved_r14

        pop     r15
        %assign cbFrame         cbFrame - 8
        %undef  frm_saved_r15
%endmacro


;; @def PUSH_RELEVANT_SEGMENT_REGISTERS
; Macro saving all segment registers on the stack.
; @param 1  Full width register name.
; @param 2  16-bit register name for \a 1.
; @cobbers rax, rdx, rcx
%macro PUSH_RELEVANT_SEGMENT_REGISTERS 2
 %ifndef VBOX_SKIP_RESTORE_SEG
  %error untested code. probably does not work any more!
  %ifndef HM_64_BIT_USE_NULL_SEL
        mov     %2, es
        push    %1
        mov     %2, ds
        push    %1
  %endif

        ; Special case for FS; Windows and Linux either don't use it or restore it when leaving kernel mode,
        ; Solaris OTOH doesn't and we must save it.
        mov     ecx, MSR_K8_FS_BASE
        rdmsr
        push    rdx
        push    rax
  %ifndef HM_64_BIT_USE_NULL_SEL
        push    fs
  %endif

   ; Special case for GS; OSes typically use swapgs to reset the hidden base register for GS on entry into the kernel.
   ; The same happens on exit.
        mov     ecx, MSR_K8_GS_BASE
        rdmsr
        push    rdx
        push    rax
  %ifndef HM_64_BIT_USE_NULL_SEL
        push    gs
  %endif
 %endif   ; !VBOX_SKIP_RESTORE_SEG
%endmacro ; PUSH_RELEVANT_SEGMENT_REGISTERS

;; @def POP_RELEVANT_SEGMENT_REGISTERS
; Macro restoring all segment registers on the stack.
; @param 1  Full width register name.
; @param 2  16-bit register name for \a 1.
; @cobbers rax, rdx, rcx
%macro POP_RELEVANT_SEGMENT_REGISTERS 2
 %ifndef VBOX_SKIP_RESTORE_SEG
  %error untested code. probably does not work any more!
        ; Note: do not step through this code with a debugger!
  %ifndef HM_64_BIT_USE_NULL_SEL
        xor     eax, eax
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
  %endif

  %ifndef HM_64_BIT_USE_NULL_SEL
        pop     gs
  %endif
        pop     rax
        pop     rdx
        mov     ecx, MSR_K8_GS_BASE
        wrmsr

  %ifndef HM_64_BIT_USE_NULL_SEL
        pop     fs
  %endif
        pop     rax
        pop     rdx
        mov     ecx, MSR_K8_FS_BASE
        wrmsr
        ; Now it's safe to step again

  %ifndef HM_64_BIT_USE_NULL_SEL
        pop     %1
        mov     ds, %2
        pop     %1
        mov     es, %2
  %endif
 %endif   ; !VBOX_SKIP_RESTORE_SEG
%endmacro ; POP_RELEVANT_SEGMENT_REGISTERS


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
%ifdef VBOX_WITH_KERNEL_USING_XMM
extern NAME(CPUMIsGuestFPUStateActive)
%endif


BEGINCODE


;;
; Used on platforms with poor inline assembly support to retrieve all the
; info from the CPU and put it in the @a pRestoreHost structure.
;
; @returns VBox status code
; @param   pRestoreHost   msc: rcx  gcc: rdi    Pointer to the RestoreHost struct.
; @param   fHaveFsGsBase  msc: dl   gcc: sil    Whether we can use rdfsbase or not.
;
ALIGNCODE(64)
BEGINPROC hmR0VmxExportHostSegmentRegsAsmHlp
%ifdef ASM_CALL64_MSC
 %define pRestoreHost rcx
%elifdef ASM_CALL64_GCC
 %define pRestoreHost rdi
%else
 %error Unknown calling convension.
%endif
        SEH64_END_PROLOGUE

        ; Start with the FS and GS base so we can trash DL/SIL.
%ifdef ASM_CALL64_MSC
        or      dl, dl
%else
        or      sil, sil
%endif
        jz      .use_rdmsr_for_fs_and_gs_base
        rdfsbase rax
        mov     [pRestoreHost + VMXRESTOREHOST.uHostFSBase], rax
        rdgsbase rax
        mov     [pRestoreHost + VMXRESTOREHOST.uHostGSBase], rax
.done_fs_and_gs_base:

        ; TR, GDTR and IDTR
        str     [pRestoreHost + VMXRESTOREHOST.uHostSelTR]
        sgdt    [pRestoreHost + VMXRESTOREHOST.HostGdtr]
        sidt    [pRestoreHost + VMXRESTOREHOST.HostIdtr]

        ; Segment registers.
        xor     eax, eax
        mov     eax, cs
        mov     [pRestoreHost + VMXRESTOREHOST.uHostSelCS], ax

        mov     eax, ss
        mov     [pRestoreHost + VMXRESTOREHOST.uHostSelSS], ax

        mov     eax, gs
        mov     [pRestoreHost + VMXRESTOREHOST.uHostSelGS], ax

        mov     eax, fs
        mov     [pRestoreHost + VMXRESTOREHOST.uHostSelFS], ax

        mov     eax, es
        mov     [pRestoreHost + VMXRESTOREHOST.uHostSelES], ax

        mov     eax, ds
        mov     [pRestoreHost + VMXRESTOREHOST.uHostSelDS], ax

        ret

ALIGNCODE(16)
.use_rdmsr_for_fs_and_gs_base:
%ifdef ASM_CALL64_MSC
        mov     r8, pRestoreHost
%endif

        mov     ecx, MSR_K8_FS_BASE
        rdmsr
        shl     rdx, 32
        or      rdx, rax
        mov     [r8 + VMXRESTOREHOST.uHostFSBase], rdx

        mov     ecx, MSR_K8_GS_BASE
        rdmsr
        shl     rdx, 32
        or      rdx, rax
        mov     [r8 + VMXRESTOREHOST.uHostGSBase], rdx

%ifdef ASM_CALL64_MSC
        mov     pRestoreHost, r8
%endif
        jmp     .done_fs_and_gs_base
%undef pRestoreHost
ENDPROC hmR0VmxExportHostSegmentRegsAsmHlp


;;
; Restores host-state fields.
;
; @returns VBox status code
; @param   f32RestoreHost   msc: ecx  gcc: edi   RestoreHost flags.
; @param   pRestoreHost     msc: rdx  gcc: rsi   Pointer to the RestoreHost struct.
;
ALIGNCODE(64)
BEGINPROC VMXRestoreHostState
%ifndef ASM_CALL64_GCC
        ; Use GCC's input registers since we'll be needing both rcx and rdx further
        ; down with the wrmsr instruction.  Use the R10 and R11 register for saving
        ; RDI and RSI since MSC preserve the two latter registers.
        mov     r10, rdi
        mov     r11, rsi
        mov     rdi, rcx
        mov     rsi, rdx
%endif
        SEH64_END_PROLOGUE

.restore_gdtr:
        test    edi, VMX_RESTORE_HOST_GDTR
        jz      .restore_idtr
        lgdt    [rsi + VMXRESTOREHOST.HostGdtr]

.restore_idtr:
        test    edi, VMX_RESTORE_HOST_IDTR
        jz      .restore_ds
        lidt    [rsi + VMXRESTOREHOST.HostIdtr]

.restore_ds:
        test    edi, VMX_RESTORE_HOST_SEL_DS
        jz      .restore_es
        mov     ax, [rsi + VMXRESTOREHOST.uHostSelDS]
        mov     ds, eax

.restore_es:
        test    edi, VMX_RESTORE_HOST_SEL_ES
        jz      .restore_tr
        mov     ax, [rsi + VMXRESTOREHOST.uHostSelES]
        mov     es, eax

.restore_tr:
        test    edi, VMX_RESTORE_HOST_SEL_TR
        jz      .restore_fs
        ; When restoring the TR, we must first clear the busy flag or we'll end up faulting.
        mov     dx, [rsi + VMXRESTOREHOST.uHostSelTR]
        mov     ax, dx
        and     eax, X86_SEL_MASK_OFF_RPL                       ; mask away TI and RPL bits leaving only the descriptor offset
        test    edi, VMX_RESTORE_HOST_GDT_READ_ONLY | VMX_RESTORE_HOST_GDT_NEED_WRITABLE
        jnz     .gdt_readonly_or_need_writable
        add     rax, qword [rsi + VMXRESTOREHOST.HostGdtr + 2]  ; xAX <- descriptor offset + GDTR.pGdt.
        and     dword [rax + 4], ~RT_BIT(9)                     ; clear the busy flag in TSS desc (bits 0-7=base, bit 9=busy bit)
        ltr     dx

.restore_fs:
        ;
        ; When restoring the selector values for FS and GS, we'll temporarily trash
        ; the base address (at least the high 32-bit bits, but quite possibly the
        ; whole base address), the wrmsr will restore it correctly. (VT-x actually
        ; restores the base correctly when leaving guest mode, but not the selector
        ; value, so there is little problem with interrupts being enabled prior to
        ; this restore job.)
        ; We'll disable ints once for both FS and GS as that's probably faster.
        ;
        test    edi, VMX_RESTORE_HOST_SEL_FS | VMX_RESTORE_HOST_SEL_GS
        jz      .restore_success
        pushfq
        cli                                   ; (see above)

        test    edi, VMX_RESTORE_HOST_CAN_USE_WRFSBASE_AND_WRGSBASE
        jz      .restore_fs_using_wrmsr

.restore_fs_using_wrfsbase:
        test    edi, VMX_RESTORE_HOST_SEL_FS
        jz      .restore_gs_using_wrgsbase
        mov     rax, qword [rsi + VMXRESTOREHOST.uHostFSBase]
        mov     cx, word [rsi + VMXRESTOREHOST.uHostSelFS]
        mov     fs, ecx
        wrfsbase rax

.restore_gs_using_wrgsbase:
        test    edi, VMX_RESTORE_HOST_SEL_GS
        jz      .restore_flags
        mov     rax, qword [rsi + VMXRESTOREHOST.uHostGSBase]
        mov     cx, word [rsi + VMXRESTOREHOST.uHostSelGS]
        mov     gs, ecx
        wrgsbase rax

.restore_flags:
        popfq

.restore_success:
        mov     eax, VINF_SUCCESS
%ifndef ASM_CALL64_GCC
        ; Restore RDI and RSI on MSC.
        mov     rdi, r10
        mov     rsi, r11
%endif
        ret

ALIGNCODE(8)
.gdt_readonly_or_need_writable:
        test    edi, VMX_RESTORE_HOST_GDT_NEED_WRITABLE
        jnz     .gdt_readonly_need_writable
.gdt_readonly:
        mov     rcx, cr0
        mov     r9, rcx
        add     rax, qword [rsi + VMXRESTOREHOST.HostGdtr + 2]  ; xAX <- descriptor offset + GDTR.pGdt.
        and     rcx, ~X86_CR0_WP
        mov     cr0, rcx
        and     dword [rax + 4], ~RT_BIT(9)                     ; clear the busy flag in TSS desc (bits 0-7=base, bit 9=busy bit)
        ltr     dx
        mov     cr0, r9
        jmp     .restore_fs

ALIGNCODE(8)
.gdt_readonly_need_writable:
        add     rax, qword [rsi + VMXRESTOREHOST.HostGdtrRw + 2]  ; xAX <- descriptor offset + GDTR.pGdtRw
        and     dword [rax + 4], ~RT_BIT(9)                     ; clear the busy flag in TSS desc (bits 0-7=base, bit 9=busy bit)
        lgdt    [rsi + VMXRESTOREHOST.HostGdtrRw]
        ltr     dx
        lgdt    [rsi + VMXRESTOREHOST.HostGdtr]                 ; load the original GDT
        jmp     .restore_fs

ALIGNCODE(8)
.restore_fs_using_wrmsr:
        test    edi, VMX_RESTORE_HOST_SEL_FS
        jz      .restore_gs_using_wrmsr
        mov     eax, dword [rsi + VMXRESTOREHOST.uHostFSBase]         ; uHostFSBase - Lo
        mov     edx, dword [rsi + VMXRESTOREHOST.uHostFSBase + 4h]    ; uHostFSBase - Hi
        mov     cx, word [rsi + VMXRESTOREHOST.uHostSelFS]
        mov     fs, ecx
        mov     ecx, MSR_K8_FS_BASE
        wrmsr

.restore_gs_using_wrmsr:
        test    edi, VMX_RESTORE_HOST_SEL_GS
        jz      .restore_flags
        mov     eax, dword [rsi + VMXRESTOREHOST.uHostGSBase]         ; uHostGSBase - Lo
        mov     edx, dword [rsi + VMXRESTOREHOST.uHostGSBase + 4h]    ; uHostGSBase - Hi
        mov     cx, word [rsi + VMXRESTOREHOST.uHostSelGS]
        mov     gs, ecx
        mov     ecx, MSR_K8_GS_BASE
        wrmsr
        jmp     .restore_flags
ENDPROC VMXRestoreHostState


;;
; Clears the MDS buffers using VERW.
ALIGNCODE(16)
BEGINPROC hmR0MdsClear
        SEH64_END_PROLOGUE
        sub     xSP, xCB
        mov     [xSP], ds
        verw    [xSP]
        add     xSP, xCB
        ret
ENDPROC   hmR0MdsClear


;;
; Dispatches an NMI to the host.
;
ALIGNCODE(16)
BEGINPROC VMXDispatchHostNmi
        ; NMI is always vector 2. The IDT[2] IRQ handler cannot be anything else. See Intel spec. 6.3.1 "External Interrupts".
        SEH64_END_PROLOGUE
        int 2
        ret
ENDPROC VMXDispatchHostNmi


;;
; Common restore logic for success and error paths.  We duplicate this because we
; don't want to waste writing the VINF_SUCCESS return value to the stack in the
; regular code path.
;
; @param    1   Zero if regular return, non-zero if error return.  Controls label emission.
; @param    2   fLoadSaveGuestXcr0 value
; @param    3   The (HM_WSF_IBPB_ENTRY | HM_WSF_L1D_ENTRY | HM_WSF_MDS_ENTRY) + HM_WSF_IBPB_EXIT value.
;               The entry values are either all set or not at all, as we're too lazy to flesh out all the variants.
; @param    4   The SSE saving/restoring: 0 to do nothing, 1 to do it manually, 2 to use xsave/xrstor.
;
; @note Important that this does not modify cbFrame or rsp.
; @note HM_WSF_SPEC_CTRL is handled differently here at the moment.
%macro RESTORE_STATE_VMX 4
        ; Restore base and limit of the IDTR & GDTR.
 %ifndef VMX_SKIP_IDTR
        lidt    [rsp + cbFrame + frm_saved_idtr]
 %endif
 %ifndef VMX_SKIP_GDTR
        lgdt    [rsp + cbFrame + frm_saved_gdtr]
 %endif

        ; Save the guest state and restore the non-volatile registers.  We use rcx=pGstCtx (&pVCpu->cpum.GstCtx) here.
        mov     [rsp + cbFrame + frm_guest_rcx], rcx
        mov     rcx, [rsp + cbFrame + frm_pGstCtx]

        mov     qword [rcx + CPUMCTX.eax], rax
        mov     qword [rcx + CPUMCTX.edx], rdx
        rdtsc
        mov     qword [rcx + CPUMCTX.ebp], rbp
        lea     rbp, [rsp + cbFrame]    ; re-establish the frame pointer as early as possible.
        shl     rdx, 20h
        or      rax, rdx                ; TSC value in RAX
        mov     rdx, [rbp + frm_guest_rcx]
        mov     qword [rcx + CPUMCTX.ecx], rdx
        mov     rdx, SPECTRE_FILLER     ; FILLER in RDX
        mov     qword [rcx + GVMCPU.hmr0 + HMR0PERVCPU.uTscExit - VMCPU.cpum.GstCtx], rax
        mov     qword [rcx + CPUMCTX.r8],  r8
        mov     r8, rdx
        mov     qword [rcx + CPUMCTX.r9],  r9
        mov     r9, rdx
        mov     qword [rcx + CPUMCTX.r10], r10
        mov     r10, rdx
        mov     qword [rcx + CPUMCTX.r11], r11
        mov     r11, rdx
        mov     qword [rcx + CPUMCTX.esi], rsi
 %ifdef ASM_CALL64_MSC
        mov     rsi, [rbp + frm_saved_rsi]
 %else
        mov     rsi, rdx
 %endif
        mov     qword [rcx + CPUMCTX.edi], rdi
 %ifdef ASM_CALL64_MSC
        mov     rdi, [rbp + frm_saved_rdi]
 %else
        mov     rdi, rdx
 %endif
        mov     qword [rcx + CPUMCTX.ebx], rbx
        mov     rbx, [rbp + frm_saved_rbx]
        mov     qword [rcx + CPUMCTX.r12], r12
        mov     r12,  [rbp + frm_saved_r12]
        mov     qword [rcx + CPUMCTX.r13], r13
        mov     r13,  [rbp + frm_saved_r13]
        mov     qword [rcx + CPUMCTX.r14], r14
        mov     r14,  [rbp + frm_saved_r14]
        mov     qword [rcx + CPUMCTX.r15], r15
        mov     r15,  [rbp + frm_saved_r15]

        mov     rax, cr2
        mov     qword [rcx + CPUMCTX.cr2], rax
        mov     rax, rdx

 %if %4 != 0
        ; Save the context pointer in r8 for the SSE save/restore.
        mov     r8, rcx
 %endif

 %if (%3) & HM_WSF_IBPB_EXIT
        ; Fight spectre (trashes rax, rdx and rcx).
  %if %1 = 0 ; Skip this in failure branch (=> guru)
        mov     ecx, MSR_IA32_PRED_CMD
        mov     eax, MSR_IA32_PRED_CMD_F_IBPB
        xor     edx, edx
        wrmsr
  %endif
 %endif

 %ifndef VMX_SKIP_TR
        ; Restore TSS selector; must mark it as not busy before using ltr!
        ; ASSUME that this is supposed to be 'BUSY' (saves 20-30 ticks on the T42p).
  %ifndef VMX_SKIP_GDTR
        lgdt    [rbp + frm_saved_gdtr]
  %endif
        movzx   eax, word [rbp + frm_saved_tr]
        mov     ecx, eax
        and     eax, X86_SEL_MASK_OFF_RPL           ; mask away TI and RPL bits leaving only the descriptor offset
        add     rax, [rbp + frm_saved_gdtr + 2]     ; eax <- GDTR.address + descriptor offset
        and     dword [rax + 4], ~RT_BIT(9)         ; clear the busy flag in TSS desc (bits 0-7=base, bit 9=busy bit)
        ltr     cx
 %endif
        movzx   edx, word [rbp + frm_saved_ldtr]
        test    edx, edx
        jz      %%skip_ldt_write
        lldt    dx
%%skip_ldt_write:

 %if %1 != 0
.return_after_vmwrite_error:
 %endif
        ; Restore segment registers.
        ;POP_RELEVANT_SEGMENT_REGISTERS rax, ax - currently broken.

 %if %2 != 0
        ; Restore the host XCR0.
        xor     ecx, ecx
        mov     eax, [rbp + frm_uHostXcr0]
        mov     edx, [rbp + frm_uHostXcr0 + 4]
        xsetbv
 %endif
%endmacro ; RESTORE_STATE_VMX


;;
; hmR0VmxStartVm template
;
; @param    1   The suffix of the variation.
; @param    2   fLoadSaveGuestXcr0 value
; @param    3   The HM_WSF_IBPB_ENTRY + HM_WSF_IBPB_EXIT value.
; @param    4   The SSE saving/restoring: 0 to do nothing, 1 to do it manually, 2 to use xsave/xrstor.
;               Drivers shouldn't use AVX registers without saving+loading:
;                   https://msdn.microsoft.com/en-us/library/windows/hardware/ff545910%28v=vs.85%29.aspx?f=255&MSPPError=-2147217396
;               However the compiler docs have different idea:
;                   https://msdn.microsoft.com/en-us/library/9z1stfyw.aspx
;               We'll go with the former for now.
;
%macro hmR0VmxStartVmTemplate 4

;;
; Prepares for and executes VMLAUNCH/VMRESUME (64 bits guest mode)
;
; @returns VBox status code
; @param    pVmcsInfo  msc:rcx, gcc:rdi       Pointer to the VMCS info (for cached host RIP and RSP).
; @param    pVCpu      msc:rdx, gcc:rsi       The cross context virtual CPU structure of the calling EMT.
; @param    fResume    msc:r8l, gcc:dl        Whether to use vmlauch/vmresume.
;
ALIGNCODE(64)
BEGINPROC RT_CONCAT(hmR0VmxStartVm,%1)
 %ifdef VBOX_WITH_KERNEL_USING_XMM
  %if %4 = 0
        ;
        ; The non-saving variant will currently check the two SSE preconditions and pick
        ; the right variant to continue with.  Later we can see if we can't manage to
        ; move these decisions into hmR0VmxUpdateStartVmFunction().
        ;
   %ifdef ASM_CALL64_MSC
        test    byte  [rdx + VMCPU.cpum.GstCtx + CPUMCTX.fUsedFpuGuest], 1
   %else
        test    byte  [rsi + VMCPU.cpum.GstCtx + CPUMCTX.fUsedFpuGuest], 1
   %endif
        jz      .save_xmm_no_need
   %ifdef ASM_CALL64_MSC
        cmp     dword [rdx + VMCPU.cpum.GstCtx + CPUMCTX.fXStateMask], 0
   %else
        cmp     dword [rsi + VMCPU.cpum.GstCtx + CPUMCTX.fXStateMask], 0
   %endif
        je      RT_CONCAT3(hmR0VmxStartVm,%1,_SseManual)
        jmp     RT_CONCAT3(hmR0VmxStartVm,%1,_SseXSave)
.save_xmm_no_need:
  %endif
 %endif
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        pushf
        cli

 %define frm_fRFlags         -008h
 %define frm_pGstCtx         -010h              ; Where we stash guest CPU context for use after the vmrun.
 %define frm_uHostXcr0       -020h              ; 128-bit
 %define frm_saved_gdtr      -02ah              ; 16+64:  Only used when VMX_SKIP_GDTR isn't defined
 %define frm_saved_tr        -02ch              ; 16-bit: Only used when VMX_SKIP_TR isn't defined
 %define frm_MDS_seg         -030h              ; 16-bit: Temporary storage for the MDS flushing.
 %define frm_saved_idtr      -03ah              ; 16+64:  Only used when VMX_SKIP_IDTR isn't defined
 %define frm_saved_ldtr      -03ch              ; 16-bit: always saved.
 %define frm_rcError         -040h              ; 32-bit: Error status code (not used in the success path)
 %define frm_guest_rcx       -048h              ; Temporary storage slot for guest RCX.
 %if %4 = 0
  %assign cbFrame             048h
 %else
  %define frm_saved_xmm6     -050h
  %define frm_saved_xmm7     -060h
  %define frm_saved_xmm8     -070h
  %define frm_saved_xmm9     -080h
  %define frm_saved_xmm10    -090h
  %define frm_saved_xmm11    -0a0h
  %define frm_saved_xmm12    -0b0h
  %define frm_saved_xmm13    -0c0h
  %define frm_saved_xmm14    -0d0h
  %define frm_saved_xmm15    -0e0h
  %define frm_saved_mxcsr    -0f0h
  %assign cbFrame             0f0h
 %endif
 %assign cbBaseFrame         cbFrame
        sub     rsp, cbFrame - 8h
        SEH64_ALLOCATE_STACK cbFrame

        ; Save all general purpose host registers.
        PUSH_CALLEE_PRESERVED_REGISTERS
        ;PUSH_RELEVANT_SEGMENT_REGISTERS xAX, ax - currently broken
        SEH64_END_PROLOGUE

        ;
        ; Unify the input parameter registers: r9=pVmcsInfo, rsi=pVCpu, bl=fResume, rdi=&pVCpu->cpum.GstCtx;
        ;
 %ifdef ASM_CALL64_GCC
        mov     r9,  rdi                ; pVmcsInfo
        mov     ebx, edx                ; fResume
 %else
        mov     r9,  rcx                ; pVmcsInfo
        mov     rsi, rdx                ; pVCpu
        mov     ebx, r8d                ; fResume
 %endif
        lea     rdi, [rsi + VMCPU.cpum.GstCtx]
        mov     [rbp + frm_pGstCtx], rdi

 %ifdef VBOX_STRICT
        ;
        ; Verify template preconditions / parameters to ensure HMSVM.cpp didn't miss some state change.
        ;
        cmp     byte [rsi + GVMCPU.hmr0 + HMR0PERVCPU.fLoadSaveGuestXcr0], %2
        mov     eax, VERR_VMX_STARTVM_PRECOND_0
        jne     NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).precond_failure_return)

        mov     eax, [rsi + GVMCPU.hmr0 + HMR0PERVCPU.fWorldSwitcher]
        and     eax, HM_WSF_IBPB_ENTRY | HM_WSF_L1D_ENTRY | HM_WSF_MDS_ENTRY | HM_WSF_IBPB_EXIT ; | HM_WSF_SPEC_CTRL
        cmp     eax, (%3)
        mov     eax, VERR_VMX_STARTVM_PRECOND_1
        jne     NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).precond_failure_return)

  %ifdef VBOX_WITH_KERNEL_USING_XMM
        mov     eax, VERR_VMX_STARTVM_PRECOND_2
        test    byte  [rsi + VMCPU.cpum.GstCtx + CPUMCTX.fUsedFpuGuest], 1
   %if   %4 = 0
        jnz     NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).precond_failure_return)
   %else
        jz      NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).precond_failure_return)

        mov     eax, VERR_VMX_STARTVM_PRECOND_3
        cmp     dword [rsi + VMCPU.cpum.GstCtx + CPUMCTX.fXStateMask], 0
    %if   %4 = 1
        jne     NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).precond_failure_return)
    %elif %4 = 2
        je      NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).precond_failure_return)
    %else
      %error Invalid template parameter 4.
    %endif
   %endif
  %endif
 %endif ; VBOX_STRICT

 %if %4 != 0
        ; Save the non-volatile SSE host register state.
        movdqa  [rbp + frm_saved_xmm6 ], xmm6
        movdqa  [rbp + frm_saved_xmm7 ], xmm7
        movdqa  [rbp + frm_saved_xmm8 ], xmm8
        movdqa  [rbp + frm_saved_xmm9 ], xmm9
        movdqa  [rbp + frm_saved_xmm10], xmm10
        movdqa  [rbp + frm_saved_xmm11], xmm11
        movdqa  [rbp + frm_saved_xmm12], xmm12
        movdqa  [rbp + frm_saved_xmm13], xmm13
        movdqa  [rbp + frm_saved_xmm14], xmm14
        movdqa  [rbp + frm_saved_xmm15], xmm15
        stmxcsr [rbp + frm_saved_mxcsr]

        ; Load the guest state related to the above non-volatile and volatile SSE registers. Trashes rcx, eax and edx.
        lea     rcx, [rdi + CPUMCTX.XState]
  %if %4 = 1 ; manual
        movdqa  xmm0,  [rcx + XMM_OFF_IN_X86FXSTATE + 000h]
        movdqa  xmm1,  [rcx + XMM_OFF_IN_X86FXSTATE + 010h]
        movdqa  xmm2,  [rcx + XMM_OFF_IN_X86FXSTATE + 020h]
        movdqa  xmm3,  [rcx + XMM_OFF_IN_X86FXSTATE + 030h]
        movdqa  xmm4,  [rcx + XMM_OFF_IN_X86FXSTATE + 040h]
        movdqa  xmm5,  [rcx + XMM_OFF_IN_X86FXSTATE + 050h]
        movdqa  xmm6,  [rcx + XMM_OFF_IN_X86FXSTATE + 060h]
        movdqa  xmm7,  [rcx + XMM_OFF_IN_X86FXSTATE + 070h]
        movdqa  xmm8,  [rcx + XMM_OFF_IN_X86FXSTATE + 080h]
        movdqa  xmm9,  [rcx + XMM_OFF_IN_X86FXSTATE + 090h]
        movdqa  xmm10, [rcx + XMM_OFF_IN_X86FXSTATE + 0a0h]
        movdqa  xmm11, [rcx + XMM_OFF_IN_X86FXSTATE + 0b0h]
        movdqa  xmm12, [rcx + XMM_OFF_IN_X86FXSTATE + 0c0h]
        movdqa  xmm13, [rcx + XMM_OFF_IN_X86FXSTATE + 0d0h]
        movdqa  xmm14, [rcx + XMM_OFF_IN_X86FXSTATE + 0e0h]
        movdqa  xmm15, [rcx + XMM_OFF_IN_X86FXSTATE + 0f0h]
        ldmxcsr        [rcx + X86FXSTATE.MXCSR]
  %elif %4 = 2 ; use xrstor/xsave
        mov     eax, [rsi + VMCPU.cpum.GstCtx + CPUMCTX.fXStateMask]
        and     eax, CPUM_VOLATILE_XSAVE_GUEST_COMPONENTS
        xor     edx, edx
        xrstor  [rcx]
  %else
   %error invalid template parameter 4
  %endif
 %endif

 %if %2 != 0
        ; Save the host XCR0 and load the guest one if necessary.
        ; Note! Trashes rax, rdx and rcx.
        xor     ecx, ecx
        xgetbv                              ; save the host one on the stack
        mov     [rbp + frm_uHostXcr0], eax
        mov     [rbp + frm_uHostXcr0 + 4], edx

        mov     eax, [rdi + CPUMCTX.aXcr]   ; load the guest one
        mov     edx, [rdi + CPUMCTX.aXcr + 4]
        xor     ecx, ecx                    ; paranoia; indicate that we must restore XCR0 (popped into ecx, thus 0)
        xsetbv
 %endif

        ; Save host LDTR.
        sldt    word [rbp + frm_saved_ldtr]

 %ifndef VMX_SKIP_TR
        ; The host TR limit is reset to 0x67; save & restore it manually.
        str     word [rbp + frm_saved_tr]
 %endif

 %ifndef VMX_SKIP_GDTR
        ; VT-x only saves the base of the GDTR & IDTR and resets the limit to 0xffff; we must restore the limit correctly!
        sgdt    [rbp + frm_saved_gdtr]
 %endif
 %ifndef VMX_SKIP_IDTR
        sidt    [rbp + frm_saved_idtr]
 %endif

        ; Load CR2 if necessary (expensive as writing CR2 is a synchronizing instruction - (bird: still expensive on 10980xe)).
        mov     rcx, qword [rdi + CPUMCTX.cr2]
        mov     rdx, cr2
        cmp     rcx, rdx
        je      .skip_cr2_write
        mov     cr2, rcx
.skip_cr2_write:

        ; Set the vmlaunch/vmresume "return" host RIP and RSP values if they've changed (unlikly).
        ; The vmwrite isn't quite for free (on an 10980xe at least), thus we check if anything changed
        ; before writing here.
        lea     rcx, [NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1)) wrt rip]
        cmp     rcx, [r9 + VMXVMCSINFO.uHostRip]
        jne     .write_host_rip
.wrote_host_rip:
        cmp     rsp, [r9 + VMXVMCSINFO.uHostRsp]
        jne     .write_host_rsp
.wrote_host_rsp:

        ;
        ; Fight spectre and similar. Trashes rax, rcx, and rdx.
        ;
 %if (%3) & (HM_WSF_IBPB_ENTRY | HM_WSF_L1D_ENTRY)  ; The eax:edx value is the same for the first two.
        AssertCompile(MSR_IA32_PRED_CMD_F_IBPB == MSR_IA32_FLUSH_CMD_F_L1D)
        mov     eax, MSR_IA32_PRED_CMD_F_IBPB
        xor     edx, edx
 %endif
 %if (%3) & HM_WSF_IBPB_ENTRY             ; Indirect branch barrier.
        mov     ecx, MSR_IA32_PRED_CMD
        wrmsr
 %endif
 %if (%3) & HM_WSF_L1D_ENTRY              ; Level 1 data cache flush.
        mov     ecx, MSR_IA32_FLUSH_CMD
        wrmsr
 %elif (%3) & HM_WSF_MDS_ENTRY            ; MDS flushing is included in L1D_FLUSH
        mov     word [rbp + frm_MDS_seg], ds
        verw    word [rbp + frm_MDS_seg]
 %endif

        ; Resume or start VM?
        cmp     bl, 0                   ; fResume

        ; Load guest general purpose registers.
        mov     rax, qword [rdi + CPUMCTX.eax]
        mov     rbx, qword [rdi + CPUMCTX.ebx]
        mov     rcx, qword [rdi + CPUMCTX.ecx]
        mov     rdx, qword [rdi + CPUMCTX.edx]
        mov     rbp, qword [rdi + CPUMCTX.ebp]
        mov     rsi, qword [rdi + CPUMCTX.esi]
        mov     r8,  qword [rdi + CPUMCTX.r8]
        mov     r9,  qword [rdi + CPUMCTX.r9]
        mov     r10, qword [rdi + CPUMCTX.r10]
        mov     r11, qword [rdi + CPUMCTX.r11]
        mov     r12, qword [rdi + CPUMCTX.r12]
        mov     r13, qword [rdi + CPUMCTX.r13]
        mov     r14, qword [rdi + CPUMCTX.r14]
        mov     r15, qword [rdi + CPUMCTX.r15]
        mov     rdi, qword [rdi + CPUMCTX.edi]

        je      .vmlaunch64_launch

        vmresume
        jc      NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).vmxstart64_invalid_vmcs_ptr)
        jz      NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).vmxstart64_start_failed)
        jmp     NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1)) ; here if vmresume detected a failure

.vmlaunch64_launch:
        vmlaunch
        jc      NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).vmxstart64_invalid_vmcs_ptr)
        jz      NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).vmxstart64_start_failed)
        jmp     NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1)) ; here if vmlaunch detected a failure


; Put these two outside the normal code path as they should rarely change.
ALIGNCODE(8)
.write_host_rip:
 %ifdef VBOX_WITH_STATISTICS
        inc     qword [rsi + VMCPU.hm + HMCPU.StatVmxWriteHostRip]
 %endif
        mov     [r9 + VMXVMCSINFO.uHostRip], rcx
        mov     eax, VMX_VMCS_HOST_RIP                      ;; @todo It is only strictly necessary to write VMX_VMCS_HOST_RIP when
        vmwrite rax, rcx                                    ;;       the VMXVMCSINFO::pfnStartVM function changes (eventually
 %ifdef VBOX_STRICT                                         ;;       take the Windows/SSE stuff into account then)...
        jna     NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).vmwrite_failed)
 %endif
        jmp     .wrote_host_rip

ALIGNCODE(8)
.write_host_rsp:
 %ifdef VBOX_WITH_STATISTICS
        inc     qword [rsi + VMCPU.hm + HMCPU.StatVmxWriteHostRsp]
 %endif
        mov     [r9 + VMXVMCSINFO.uHostRsp], rsp
        mov     eax, VMX_VMCS_HOST_RSP
        vmwrite rax, rsp
 %ifdef VBOX_STRICT
        jna     NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1).vmwrite_failed)
 %endif
        jmp     .wrote_host_rsp

ALIGNCODE(64)
GLOBALNAME RT_CONCAT(hmR0VmxStartVmHostRIP,%1)
        RESTORE_STATE_VMX 0, %2, %3, %4
        mov     eax, VINF_SUCCESS

.vmstart64_end:
 %if %4 != 0
        mov     r11d, eax               ; save the return code.

        ; Save the guest SSE state related to non-volatile and volatile SSE registers.
        lea     rcx, [r8 + CPUMCTX.XState]
  %if %4 = 1 ; manual
        stmxcsr [rcx + X86FXSTATE.MXCSR]
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 000h], xmm0
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 010h], xmm1
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 020h], xmm2
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 030h], xmm3
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 040h], xmm4
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 050h], xmm5
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 060h], xmm6
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 070h], xmm7
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 080h], xmm8
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 090h], xmm9
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0a0h], xmm10
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0b0h], xmm11
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0c0h], xmm12
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0d0h], xmm13
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0e0h], xmm14
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0f0h], xmm15
  %elif %4 = 2 ; use xrstor/xsave
        mov     eax, [r8 + CPUMCTX.fXStateMask]
        and     eax, CPUM_VOLATILE_XSAVE_GUEST_COMPONENTS
        xor     edx, edx
        xsave   [rcx]
  %else
   %error invalid template parameter 4
  %endif

        ; Restore the host non-volatile SSE register state.
        ldmxcsr [rbp + frm_saved_mxcsr]
        movdqa  xmm6,  [rbp + frm_saved_xmm6 ]
        movdqa  xmm7,  [rbp + frm_saved_xmm7 ]
        movdqa  xmm8,  [rbp + frm_saved_xmm8 ]
        movdqa  xmm9,  [rbp + frm_saved_xmm9 ]
        movdqa  xmm10, [rbp + frm_saved_xmm10]
        movdqa  xmm11, [rbp + frm_saved_xmm11]
        movdqa  xmm12, [rbp + frm_saved_xmm12]
        movdqa  xmm13, [rbp + frm_saved_xmm13]
        movdqa  xmm14, [rbp + frm_saved_xmm14]
        movdqa  xmm15, [rbp + frm_saved_xmm15]

        mov     eax, r11d
 %endif  ; %4 != 0

.return_with_restored_preserved_registers:
        lea     rsp, [rbp + frm_fRFlags]
        popf
        leave
        ret

        ;
        ; Error returns.
        ;
 %ifdef VBOX_STRICT
.vmwrite_failed:
        mov     dword [rsp + cbFrame + frm_rcError], VERR_VMX_INVALID_VMCS_FIELD
        jz      .return_after_vmwrite_error
        mov     dword [rsp + cbFrame + frm_rcError], VERR_VMX_INVALID_VMCS_PTR
        jmp     .return_after_vmwrite_error
 %endif
.vmxstart64_invalid_vmcs_ptr:
        mov     dword [rsp + cbFrame + frm_rcError], VERR_VMX_INVALID_VMCS_PTR_TO_START_VM
        jmp     .vmstart64_error_return
.vmxstart64_start_failed:
        mov     dword [rsp + cbFrame + frm_rcError], VERR_VMX_UNABLE_TO_START_VM
.vmstart64_error_return:
        RESTORE_STATE_VMX 1, %2, %3, %4
        mov     eax, [rbp + frm_rcError]
        jmp     .vmstart64_end

 %ifdef VBOX_STRICT
        ; Precondition checks failed.
.precond_failure_return:
        POP_CALLEE_PRESERVED_REGISTERS
  %if cbFrame != cbBaseFrame
   %error Bad frame size value: cbFrame, expected cbBaseFrame
  %endif
        jmp     .return_with_restored_preserved_registers
 %endif

 %undef frm_fRFlags
 %undef frm_pGstCtx
 %undef frm_uHostXcr0
 %undef frm_saved_gdtr
 %undef frm_saved_tr
 %undef frm_fNoRestoreXcr0
 %undef frm_saved_idtr
 %undef frm_saved_ldtr
 %undef frm_rcError
 %undef frm_guest_rax
 %undef cbFrame
ENDPROC RT_CONCAT(hmR0VmxStartVm,%1)
 %ifdef ASM_FORMAT_ELF
size NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1))  NAME(RT_CONCAT(hmR0VmxStartVm,%1) %+ _EndProc) - NAME(RT_CONCAT(hmR0VmxStartVmHostRIP,%1))
 %endif


%endmacro ; hmR0VmxStartVmTemplate

%macro hmR0VmxStartVmSseTemplate 2
hmR0VmxStartVmTemplate _SansXcr0_SansIbpbEntry_SansL1dEntry_SansMdsEntry_SansIbpbExit %+ %2, 0, 0                 | 0                | 0                | 0               , %1
hmR0VmxStartVmTemplate _WithXcr0_SansIbpbEntry_SansL1dEntry_SansMdsEntry_SansIbpbExit %+ %2, 1, 0                 | 0                | 0                | 0               , %1
hmR0VmxStartVmTemplate _SansXcr0_WithIbpbEntry_SansL1dEntry_SansMdsEntry_SansIbpbExit %+ %2, 0, HM_WSF_IBPB_ENTRY | 0                | 0                | 0               , %1
hmR0VmxStartVmTemplate _WithXcr0_WithIbpbEntry_SansL1dEntry_SansMdsEntry_SansIbpbExit %+ %2, 1, HM_WSF_IBPB_ENTRY | 0                | 0                | 0               , %1
hmR0VmxStartVmTemplate _SansXcr0_SansIbpbEntry_WithL1dEntry_SansMdsEntry_SansIbpbExit %+ %2, 0, 0                 | HM_WSF_L1D_ENTRY | 0                | 0               , %1
hmR0VmxStartVmTemplate _WithXcr0_SansIbpbEntry_WithL1dEntry_SansMdsEntry_SansIbpbExit %+ %2, 1, 0                 | HM_WSF_L1D_ENTRY | 0                | 0               , %1
hmR0VmxStartVmTemplate _SansXcr0_WithIbpbEntry_WithL1dEntry_SansMdsEntry_SansIbpbExit %+ %2, 0, HM_WSF_IBPB_ENTRY | HM_WSF_L1D_ENTRY | 0                | 0               , %1
hmR0VmxStartVmTemplate _WithXcr0_WithIbpbEntry_WithL1dEntry_SansMdsEntry_SansIbpbExit %+ %2, 1, HM_WSF_IBPB_ENTRY | HM_WSF_L1D_ENTRY | 0                | 0               , %1
hmR0VmxStartVmTemplate _SansXcr0_SansIbpbEntry_SansL1dEntry_WithMdsEntry_SansIbpbExit %+ %2, 0, 0                 | 0                | HM_WSF_MDS_ENTRY | 0               , %1
hmR0VmxStartVmTemplate _WithXcr0_SansIbpbEntry_SansL1dEntry_WithMdsEntry_SansIbpbExit %+ %2, 1, 0                 | 0                | HM_WSF_MDS_ENTRY | 0               , %1
hmR0VmxStartVmTemplate _SansXcr0_WithIbpbEntry_SansL1dEntry_WithMdsEntry_SansIbpbExit %+ %2, 0, HM_WSF_IBPB_ENTRY | 0                | HM_WSF_MDS_ENTRY | 0               , %1
hmR0VmxStartVmTemplate _WithXcr0_WithIbpbEntry_SansL1dEntry_WithMdsEntry_SansIbpbExit %+ %2, 1, HM_WSF_IBPB_ENTRY | 0                | HM_WSF_MDS_ENTRY | 0               , %1
hmR0VmxStartVmTemplate _SansXcr0_SansIbpbEntry_WithL1dEntry_WithMdsEntry_SansIbpbExit %+ %2, 0, 0                 | HM_WSF_L1D_ENTRY | HM_WSF_MDS_ENTRY | 0               , %1
hmR0VmxStartVmTemplate _WithXcr0_SansIbpbEntry_WithL1dEntry_WithMdsEntry_SansIbpbExit %+ %2, 1, 0                 | HM_WSF_L1D_ENTRY | HM_WSF_MDS_ENTRY | 0               , %1
hmR0VmxStartVmTemplate _SansXcr0_WithIbpbEntry_WithL1dEntry_WithMdsEntry_SansIbpbExit %+ %2, 0, HM_WSF_IBPB_ENTRY | HM_WSF_L1D_ENTRY | HM_WSF_MDS_ENTRY | 0               , %1
hmR0VmxStartVmTemplate _WithXcr0_WithIbpbEntry_WithL1dEntry_WithMdsEntry_SansIbpbExit %+ %2, 1, HM_WSF_IBPB_ENTRY | HM_WSF_L1D_ENTRY | HM_WSF_MDS_ENTRY | 0               , %1
hmR0VmxStartVmTemplate _SansXcr0_SansIbpbEntry_SansL1dEntry_SansMdsEntry_WithIbpbExit %+ %2, 0, 0                 | 0                | 0                | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _WithXcr0_SansIbpbEntry_SansL1dEntry_SansMdsEntry_WithIbpbExit %+ %2, 1, 0                 | 0                | 0                | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _SansXcr0_WithIbpbEntry_SansL1dEntry_SansMdsEntry_WithIbpbExit %+ %2, 0, HM_WSF_IBPB_ENTRY | 0                | 0                | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _WithXcr0_WithIbpbEntry_SansL1dEntry_SansMdsEntry_WithIbpbExit %+ %2, 1, HM_WSF_IBPB_ENTRY | 0                | 0                | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _SansXcr0_SansIbpbEntry_WithL1dEntry_SansMdsEntry_WithIbpbExit %+ %2, 0, 0                 | HM_WSF_L1D_ENTRY | 0                | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _WithXcr0_SansIbpbEntry_WithL1dEntry_SansMdsEntry_WithIbpbExit %+ %2, 1, 0                 | HM_WSF_L1D_ENTRY | 0                | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _SansXcr0_WithIbpbEntry_WithL1dEntry_SansMdsEntry_WithIbpbExit %+ %2, 0, HM_WSF_IBPB_ENTRY | HM_WSF_L1D_ENTRY | 0                | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _WithXcr0_WithIbpbEntry_WithL1dEntry_SansMdsEntry_WithIbpbExit %+ %2, 1, HM_WSF_IBPB_ENTRY | HM_WSF_L1D_ENTRY | 0                | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _SansXcr0_SansIbpbEntry_SansL1dEntry_WithMdsEntry_WithIbpbExit %+ %2, 0, 0                 | 0                | HM_WSF_MDS_ENTRY | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _WithXcr0_SansIbpbEntry_SansL1dEntry_WithMdsEntry_WithIbpbExit %+ %2, 1, 0                 | 0                | HM_WSF_MDS_ENTRY | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _SansXcr0_WithIbpbEntry_SansL1dEntry_WithMdsEntry_WithIbpbExit %+ %2, 0, HM_WSF_IBPB_ENTRY | 0                | HM_WSF_MDS_ENTRY | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _WithXcr0_WithIbpbEntry_SansL1dEntry_WithMdsEntry_WithIbpbExit %+ %2, 1, HM_WSF_IBPB_ENTRY | 0                | HM_WSF_MDS_ENTRY | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _SansXcr0_SansIbpbEntry_WithL1dEntry_WithMdsEntry_WithIbpbExit %+ %2, 0, 0                 | HM_WSF_L1D_ENTRY | HM_WSF_MDS_ENTRY | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _WithXcr0_SansIbpbEntry_WithL1dEntry_WithMdsEntry_WithIbpbExit %+ %2, 1, 0                 | HM_WSF_L1D_ENTRY | HM_WSF_MDS_ENTRY | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _SansXcr0_WithIbpbEntry_WithL1dEntry_WithMdsEntry_WithIbpbExit %+ %2, 0, HM_WSF_IBPB_ENTRY | HM_WSF_L1D_ENTRY | HM_WSF_MDS_ENTRY | HM_WSF_IBPB_EXIT, %1
hmR0VmxStartVmTemplate _WithXcr0_WithIbpbEntry_WithL1dEntry_WithMdsEntry_WithIbpbExit %+ %2, 1, HM_WSF_IBPB_ENTRY | HM_WSF_L1D_ENTRY | HM_WSF_MDS_ENTRY | HM_WSF_IBPB_EXIT, %1
%endmacro

hmR0VmxStartVmSseTemplate 0,,
%ifdef VBOX_WITH_KERNEL_USING_XMM
hmR0VmxStartVmSseTemplate 1,_SseManual
hmR0VmxStartVmSseTemplate 2,_SseXSave
%endif


;;
; hmR0SvmVmRun template
;
; @param    1   The suffix of the variation.
; @param    2   fLoadSaveGuestXcr0 value
; @param    3   The HM_WSF_IBPB_ENTRY + HM_WSF_IBPB_EXIT + HM_WSF_SPEC_CTRL value.
; @param    4   The SSE saving/restoring: 0 to do nothing, 1 to do it manually, 2 to use xsave/xrstor.
;               Drivers shouldn't use AVX registers without saving+loading:
;                   https://msdn.microsoft.com/en-us/library/windows/hardware/ff545910%28v=vs.85%29.aspx?f=255&MSPPError=-2147217396
;               However the compiler docs have different idea:
;                   https://msdn.microsoft.com/en-us/library/9z1stfyw.aspx
;               We'll go with the former for now.
;
%macro hmR0SvmVmRunTemplate 4

;;
; Prepares for and executes VMRUN (32-bit and 64-bit guests).
;
; @returns  VBox status code
; @param    pVM             msc:rcx,gcc:rdi     The cross context VM structure (unused).
; @param    pVCpu           msc:rdx,gcc:rsi     The cross context virtual CPU structure of the calling EMT.
; @param    HCPhysVmcb      msc:r8, gcc:rdx     Physical address of guest VMCB.
;
ALIGNCODE(64) ; This + immediate optimizations causes serious trouble for yasm and the SEH frames: prologue -28 bytes, must be <256
              ; So the SEH64_XXX stuff is currently not operational.
BEGINPROC RT_CONCAT(hmR0SvmVmRun,%1)
 %ifdef VBOX_WITH_KERNEL_USING_XMM
  %if %4 = 0
        ;
        ; The non-saving variant will currently check the two SSE preconditions and pick
        ; the right variant to continue with.  Later we can see if we can't manage to
        ; move these decisions into hmR0SvmUpdateVmRunFunction().
        ;
   %ifdef ASM_CALL64_MSC
        test    byte  [rdx + VMCPU.cpum.GstCtx + CPUMCTX.fUsedFpuGuest], 1
   %else
        test    byte  [rsi + VMCPU.cpum.GstCtx + CPUMCTX.fUsedFpuGuest], 1
   %endif
        jz      .save_xmm_no_need
   %ifdef ASM_CALL64_MSC
        cmp     dword [rdx + VMCPU.cpum.GstCtx + CPUMCTX.fXStateMask], 0
   %else
        cmp     dword [rsi + VMCPU.cpum.GstCtx + CPUMCTX.fXStateMask], 0
   %endif
        je      RT_CONCAT3(hmR0SvmVmRun,%1,_SseManual)
        jmp     RT_CONCAT3(hmR0SvmVmRun,%1,_SseXSave)
.save_xmm_no_need:
  %endif
 %endif
        push    rbp
        SEH64_PUSH_xBP
        mov     rbp, rsp
        SEH64_SET_FRAME_xBP 0
        pushf
  %assign cbFrame            40h
 %if %4 != 0
  %assign cbFrame            cbFrame + 16 * 11  ; Reserve space for 10x 128-bit XMM registers and MXCSR (32-bit)
 %endif
 %assign cbBaseFrame         cbFrame
        sub     rsp, cbFrame - 8h               ; We subtract 8 bytes for the above pushf
        SEH64_ALLOCATE_STACK cbFrame            ; And we have CALLEE_PRESERVED_REGISTER_COUNT following it.

 %define frm_fRFlags         -008h
 %define frm_uHostXcr0       -018h              ; 128-bit
 ;%define frm_fNoRestoreXcr0  -020h              ; Non-zero if we should skip XCR0 restoring.
 %define frm_pGstCtx         -028h              ; Where we stash guest CPU context for use after the vmrun.
 %define frm_HCPhysVmcbHost  -030h              ; Where we stash HCPhysVmcbHost for the vmload after vmrun.
 %define frm_uHostSpecCtrl   -040h              ; Saved IA32_MSR_SPEC_CTRL value.
 %if %4 != 0
  %define frm_saved_xmm6     -050h
  %define frm_saved_xmm7     -060h
  %define frm_saved_xmm8     -070h
  %define frm_saved_xmm9     -080h
  %define frm_saved_xmm10    -090h
  %define frm_saved_xmm11    -0a0h
  %define frm_saved_xmm12    -0b0h
  %define frm_saved_xmm13    -0c0h
  %define frm_saved_xmm14    -0d0h
  %define frm_saved_xmm15    -0e0h
  %define frm_saved_mxcsr    -0f0h
 %endif

        ; Manual save and restore:
        ;  - General purpose registers except RIP, RSP, RAX
        ;
        ; Trashed:
        ;  - CR2 (we don't care)
        ;  - LDTR (reset to 0)
        ;  - DRx (presumably not changed at all)
        ;  - DR7 (reset to 0x400)

        ; Save all general purpose host registers.
        PUSH_CALLEE_PRESERVED_REGISTERS
        SEH64_END_PROLOGUE
 %if cbFrame != (cbBaseFrame + 8 * CALLEE_PRESERVED_REGISTER_COUNT)
  %error Bad cbFrame value
 %endif

        ; Shuffle parameter registers so that r8=HCPhysVmcb and rsi=pVCpu.  (rdx & rcx will soon be trashed.)
 %ifdef ASM_CALL64_GCC
        mov     r8, rdx                         ; Put HCPhysVmcb in r8 like on MSC as rdx is trashed below.
 %else
        mov     rsi, rdx                        ; Put pVCpu in rsi like on GCC as rdx is trashed below.
        ;mov     rdi, rcx                        ; Put pVM in rdi like on GCC as rcx is trashed below.
 %endif

 %ifdef VBOX_STRICT
        ;
        ; Verify template preconditions / parameters to ensure HMSVM.cpp didn't miss some state change.
        ;
        cmp     byte [rsi + GVMCPU.hmr0 + HMR0PERVCPU.fLoadSaveGuestXcr0], %2
        mov     eax, VERR_SVM_VMRUN_PRECOND_0
        jne     .failure_return

        mov     eax, [rsi + GVMCPU.hmr0 + HMR0PERVCPU.fWorldSwitcher]
        and     eax, HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT | HM_WSF_SPEC_CTRL
        cmp     eax, (%3)
        mov     eax, VERR_SVM_VMRUN_PRECOND_1
        jne     .failure_return

  %ifdef VBOX_WITH_KERNEL_USING_XMM
        mov     eax, VERR_SVM_VMRUN_PRECOND_2
        test    byte  [rsi + VMCPU.cpum.GstCtx + CPUMCTX.fUsedFpuGuest], 1
   %if   %4 = 0
        jnz     .failure_return
   %else
        jz      .failure_return

        mov     eax, VERR_SVM_VMRUN_PRECOND_3
        cmp     dword [rsi + VMCPU.cpum.GstCtx + CPUMCTX.fXStateMask], 0
    %if   %4 = 1
        jne     .failure_return
    %elif %4 = 2
        je      .failure_return
    %else
      %error Invalid template parameter 4.
    %endif
   %endif
  %endif
 %endif ; VBOX_STRICT

 %if %4 != 0
        ; Save the non-volatile SSE host register state.
        movdqa  [rbp + frm_saved_xmm6 ], xmm6
        movdqa  [rbp + frm_saved_xmm7 ], xmm7
        movdqa  [rbp + frm_saved_xmm8 ], xmm8
        movdqa  [rbp + frm_saved_xmm9 ], xmm9
        movdqa  [rbp + frm_saved_xmm10], xmm10
        movdqa  [rbp + frm_saved_xmm11], xmm11
        movdqa  [rbp + frm_saved_xmm12], xmm12
        movdqa  [rbp + frm_saved_xmm13], xmm13
        movdqa  [rbp + frm_saved_xmm14], xmm14
        movdqa  [rbp + frm_saved_xmm15], xmm15
        stmxcsr [rbp + frm_saved_mxcsr]

        ; Load the guest state related to the above non-volatile and volatile SSE registers. Trashes rcx, eax and edx.
        lea     rcx, [rsi + VMCPU.cpum.GstCtx + CPUMCTX.XState]
  %if %4 = 1 ; manual
        movdqa  xmm0,  [rcx + XMM_OFF_IN_X86FXSTATE + 000h]
        movdqa  xmm1,  [rcx + XMM_OFF_IN_X86FXSTATE + 010h]
        movdqa  xmm2,  [rcx + XMM_OFF_IN_X86FXSTATE + 020h]
        movdqa  xmm3,  [rcx + XMM_OFF_IN_X86FXSTATE + 030h]
        movdqa  xmm4,  [rcx + XMM_OFF_IN_X86FXSTATE + 040h]
        movdqa  xmm5,  [rcx + XMM_OFF_IN_X86FXSTATE + 050h]
        movdqa  xmm6,  [rcx + XMM_OFF_IN_X86FXSTATE + 060h]
        movdqa  xmm7,  [rcx + XMM_OFF_IN_X86FXSTATE + 070h]
        movdqa  xmm8,  [rcx + XMM_OFF_IN_X86FXSTATE + 080h]
        movdqa  xmm9,  [rcx + XMM_OFF_IN_X86FXSTATE + 090h]
        movdqa  xmm10, [rcx + XMM_OFF_IN_X86FXSTATE + 0a0h]
        movdqa  xmm11, [rcx + XMM_OFF_IN_X86FXSTATE + 0b0h]
        movdqa  xmm12, [rcx + XMM_OFF_IN_X86FXSTATE + 0c0h]
        movdqa  xmm13, [rcx + XMM_OFF_IN_X86FXSTATE + 0d0h]
        movdqa  xmm14, [rcx + XMM_OFF_IN_X86FXSTATE + 0e0h]
        movdqa  xmm15, [rcx + XMM_OFF_IN_X86FXSTATE + 0f0h]
        ldmxcsr        [rcx + X86FXSTATE.MXCSR]
  %elif %4 = 2 ; use xrstor/xsave
        mov     eax, [rsi + VMCPU.cpum.GstCtx + CPUMCTX.fXStateMask]
        and     eax, CPUM_VOLATILE_XSAVE_GUEST_COMPONENTS
        xor     edx, edx
        xrstor  [rcx]
  %else
   %error invalid template parameter 4
  %endif
 %endif

 %if %2 != 0
        ; Save the host XCR0 and load the guest one if necessary.
        xor     ecx, ecx
        xgetbv                                  ; save the host XCR0 on the stack
        mov     [rbp + frm_uHostXcr0 + 8], rdx
        mov     [rbp + frm_uHostXcr0    ], rax

        mov     eax, [rsi + VMCPU.cpum.GstCtx + CPUMCTX.aXcr] ; load the guest XCR0
        mov     edx, [rsi + VMCPU.cpum.GstCtx + CPUMCTX.aXcr + 4]
        xor     ecx, ecx                        ; paranoia
        xsetbv
 %endif

 %if (%3) & HM_WSF_SPEC_CTRL
        ; Save host MSR_IA32_SPEC_CTRL and load the guest one (trashes rax, rdx, rcx, rbx).
        ; HACK ALERT! Boldly ASSUMES that CPUMCTXMSRS follows immediately after GstCtx (CPUMCTX).
        mov     rbx, [rsi + VMCPU.cpum.GstCtx + CPUMCTX_size + CPUMCTXMSRS.msr.SpecCtrl] ; rbx = guest IA32_SPEC_CTRL
        mov     ecx, MSR_IA32_SPEC_CTRL
        rdmsr                               ; edx:eax = host IA32_SPEC_CTRL value
        shl     rdx, 32
        or      rdx, rax                    ; rdx = host IA32_SPEC_CTRL value
        mov     [rbp + frm_uHostSpecCtrl], rdx
        cmp     rdx, rbx                    ; avoid wrmsr if we can.
        je      .skip_spec_ctrl_load
        mov     eax, ebx
        mov     rdx, rbx
        shr     rdx, 32
        wrmsr
.skip_spec_ctrl_load:
 %endif

        ; Save host fs, gs, sysenter msr etc.
        mov     rax, [rsi + GVMCPU.hmr0 + HMR0PERVCPU.svm + HMR0CPUSVM.HCPhysVmcbHost]
        mov     qword [rbp + frm_HCPhysVmcbHost], rax ; save for the vmload after vmrun
        lea     rsi, [rsi + VMCPU.cpum.GstCtx]
        mov     qword [rbp + frm_pGstCtx], rsi
        vmsave

 %if (%3) & HM_WSF_IBPB_ENTRY
        ; Fight spectre (trashes rax, rdx and rcx).
        mov     ecx, MSR_IA32_PRED_CMD
        mov     eax, MSR_IA32_PRED_CMD_F_IBPB
        xor     edx, edx
        wrmsr
 %endif

        ; Setup rax for VMLOAD.
        mov     rax, r8                         ; HCPhysVmcb (64 bits physical address; take low dword only)

        ; Load guest general purpose registers (rax is loaded from the VMCB by VMRUN).
        mov     rbx, qword [rsi + CPUMCTX.ebx]
        mov     rcx, qword [rsi + CPUMCTX.ecx]
        mov     rdx, qword [rsi + CPUMCTX.edx]
        mov     rdi, qword [rsi + CPUMCTX.edi]
        mov     rbp, qword [rsi + CPUMCTX.ebp]
        mov     r8,  qword [rsi + CPUMCTX.r8]
        mov     r9,  qword [rsi + CPUMCTX.r9]
        mov     r10, qword [rsi + CPUMCTX.r10]
        mov     r11, qword [rsi + CPUMCTX.r11]
        mov     r12, qword [rsi + CPUMCTX.r12]
        mov     r13, qword [rsi + CPUMCTX.r13]
        mov     r14, qword [rsi + CPUMCTX.r14]
        mov     r15, qword [rsi + CPUMCTX.r15]
        mov     rsi, qword [rsi + CPUMCTX.esi]

        ; Clear the global interrupt flag & execute sti to make sure external interrupts cause a world switch.
        clgi
        sti

        ; Load guest FS, GS, Sysenter MSRs etc.
        vmload

        ; Run the VM.
        vmrun

        ; Save guest fs, gs, sysenter msr etc.
        vmsave

        ; Load host fs, gs, sysenter msr etc.
        mov     rax, [rsp + cbFrame + frm_HCPhysVmcbHost] ; load HCPhysVmcbHost (rbp is not operational yet, thus rsp)
        vmload

        ; Set the global interrupt flag again, but execute cli to make sure IF=0.
        cli
        stgi

        ; Pop pVCpu (saved above) and save the guest GPRs (sans RSP and RAX).
        mov     rax, [rsp + cbFrame + frm_pGstCtx] ; (rbp still not operational)

        mov     qword [rax + CPUMCTX.edx], rdx
        mov     qword [rax + CPUMCTX.ecx], rcx
        mov     rcx, rax
        rdtsc
        mov     qword [rcx + CPUMCTX.ebp], rbp
        lea     rbp, [rsp + cbFrame]
        shl     rdx, 20h
        or      rax, rdx                ; TSC value in RAX
        mov     qword [rcx + CPUMCTX.r8],  r8
        mov     r8, SPECTRE_FILLER      ; SPECTRE filler in R8
        mov     qword [rcx + CPUMCTX.r9],  r9
        mov     r9, r8
        mov     qword [rcx + CPUMCTX.r10], r10
        mov     r10, r8
        mov     qword [rcx + GVMCPU.hmr0 + HMR0PERVCPU.uTscExit - VMCPU.cpum.GstCtx], rax
        mov     qword [rcx + CPUMCTX.r11], r11
        mov     r11, r8
        mov     qword [rcx + CPUMCTX.edi], rdi
 %ifdef ASM_CALL64_MSC
        mov     rdi, [rbp + frm_saved_rdi]
 %else
        mov     rdi, r8
 %endif
        mov     qword [rcx + CPUMCTX.esi], rsi
 %ifdef ASM_CALL64_MSC
        mov     rsi, [rbp + frm_saved_rsi]
 %else
        mov     rsi, r8
 %endif
        mov     qword [rcx + CPUMCTX.ebx], rbx
        mov     rbx, [rbp + frm_saved_rbx]
        mov     qword [rcx + CPUMCTX.r12], r12
        mov     r12, [rbp + frm_saved_r12]
        mov     qword [rcx + CPUMCTX.r13], r13
        mov     r13, [rbp + frm_saved_r13]
        mov     qword [rcx + CPUMCTX.r14], r14
        mov     r14, [rbp + frm_saved_r14]
        mov     qword [rcx + CPUMCTX.r15], r15
        mov     r15, [rbp + frm_saved_r15]

 %if %4 != 0 || ((%3) & HM_WSF_SPEC_CTRL)
        ; Set r8 = &pVCpu->cpum.GstCtx; for use below when saving and restoring SSE state as well as for IA32_SPEC_CTRL.
        mov     r8, rcx
 %endif

 %if (%3) & HM_WSF_IBPB_EXIT
        ; Fight spectre (trashes rax, rdx and rcx).
        mov     ecx, MSR_IA32_PRED_CMD
        mov     eax, MSR_IA32_PRED_CMD_F_IBPB
        xor     edx, edx
        wrmsr
 %endif

 %if %2 != 0
        ; Restore the host xcr0.
        xor     ecx, ecx
        mov     rdx, [rbp + frm_uHostXcr0 + 8]
        mov     rax, [rbp + frm_uHostXcr0]
        xsetbv
 %endif

 %if (%3) & HM_WSF_SPEC_CTRL
        ; Save guest MSR_IA32_SPEC_CTRL and load the host one (trashes rax, rdx, rcx, r10).
        mov     r10, [rbp + frm_uHostSpecCtrl] ; r10 = host IA32_SPEC_CTRL
        mov     ecx, MSR_IA32_SPEC_CTRL
        rdmsr                               ; edx:eax = guest IA32_SPEC_CTRL value
        shl     rdx, 32
        or      rdx, rax                    ; rdx = guest IA32_SPEC_CTRL value
        ; HACK ALERT! Boldly ASSUMES that CPUMCTXMSRS follows immediately after GstCtx (CPUMCTX).
        mov     [r8 + CPUMCTX_size + CPUMCTXMSRS.msr.SpecCtrl], rdx ; saved guest IA32_SPEC_CTRL
        cmp     rdx, r10                    ; avoid wrmsr if we can.
        je      .skip_spec_ctrl_restore
        mov     eax, r10d
        mov     rdx, r10
        shr     rdx, 32
        wrmsr
.skip_spec_ctrl_restore:
 %endif

 %if %4 != 0
        ; Save the guest SSE state related to non-volatile and volatile SSE registers.
        lea     rcx, [r8 + CPUMCTX.XState]
  %if %4 = 1 ; manual
        stmxcsr [rcx + X86FXSTATE.MXCSR]
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 000h], xmm0
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 010h], xmm1
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 020h], xmm2
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 030h], xmm3
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 040h], xmm4
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 050h], xmm5
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 060h], xmm6
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 070h], xmm7
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 080h], xmm8
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 090h], xmm9
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0a0h], xmm10
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0b0h], xmm11
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0c0h], xmm12
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0d0h], xmm13
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0e0h], xmm14
        movdqa  [rcx + XMM_OFF_IN_X86FXSTATE + 0f0h], xmm15
  %elif %4 = 2 ; use xrstor/xsave
        mov     eax, [r8 + CPUMCTX.fXStateMask]
        and     eax, CPUM_VOLATILE_XSAVE_GUEST_COMPONENTS
        xor     edx, edx
        xsave   [rcx]
  %else
   %error invalid template parameter 4
  %endif

        ; Restore the host non-volatile SSE register state.
        ldmxcsr [rbp + frm_saved_mxcsr]
        movdqa  xmm6,  [rbp + frm_saved_xmm6 ]
        movdqa  xmm7,  [rbp + frm_saved_xmm7 ]
        movdqa  xmm8,  [rbp + frm_saved_xmm8 ]
        movdqa  xmm9,  [rbp + frm_saved_xmm9 ]
        movdqa  xmm10, [rbp + frm_saved_xmm10]
        movdqa  xmm11, [rbp + frm_saved_xmm11]
        movdqa  xmm12, [rbp + frm_saved_xmm12]
        movdqa  xmm13, [rbp + frm_saved_xmm13]
        movdqa  xmm14, [rbp + frm_saved_xmm14]
        movdqa  xmm15, [rbp + frm_saved_xmm15]
 %endif  ; %4 != 0

        ; Epilogue (assumes we restored volatile registers above when saving the guest GPRs).
        mov     eax, VINF_SUCCESS
        add     rsp, cbFrame - 8h
        popf
        leave
        ret

 %ifdef VBOX_STRICT
        ; Precondition checks failed.
.failure_return:
        POP_CALLEE_PRESERVED_REGISTERS
 %if cbFrame != cbBaseFrame
  %error Bad frame size value: cbFrame
 %endif
        add     rsp, cbFrame - 8h
        popf
        leave
        ret
 %endif

%undef frm_uHostXcr0
%undef frm_fNoRestoreXcr0
%undef frm_pVCpu
%undef frm_HCPhysVmcbHost
%undef cbFrame
ENDPROC RT_CONCAT(hmR0SvmVmRun,%1)

%endmacro ; hmR0SvmVmRunTemplate

;
; Instantiate the hmR0SvmVmRun various variations.
;
hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_SansIbpbExit_SansSpecCtrl,           0, 0,                                                       0
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_SansIbpbExit_SansSpecCtrl,           1, 0,                                                       0
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_SansIbpbExit_SansSpecCtrl,           0, HM_WSF_IBPB_ENTRY,                                       0
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_SansIbpbExit_SansSpecCtrl,           1, HM_WSF_IBPB_ENTRY,                                       0
hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_WithIbpbExit_SansSpecCtrl,           0, HM_WSF_IBPB_EXIT,                                        0
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_WithIbpbExit_SansSpecCtrl,           1, HM_WSF_IBPB_EXIT,                                        0
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_WithIbpbExit_SansSpecCtrl,           0, HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT,                    0
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_WithIbpbExit_SansSpecCtrl,           1, HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT,                    0
hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_SansIbpbExit_WithSpecCtrl,           0, HM_WSF_SPEC_CTRL,                                        0
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_SansIbpbExit_WithSpecCtrl,           1, HM_WSF_SPEC_CTRL,                                        0
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_SansIbpbExit_WithSpecCtrl,           0, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY,                    0
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_SansIbpbExit_WithSpecCtrl,           1, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY,                    0
hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_WithIbpbExit_WithSpecCtrl,           0, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_EXIT,                     0
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_WithIbpbExit_WithSpecCtrl,           1, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_EXIT,                     0
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_WithIbpbExit_WithSpecCtrl,           0, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT, 0
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_WithIbpbExit_WithSpecCtrl,           1, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT, 0

%ifdef VBOX_WITH_KERNEL_USING_XMM
hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_SansIbpbExit_SansSpecCtrl_SseManual, 0, 0,                                                       1
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_SansIbpbExit_SansSpecCtrl_SseManual, 1, 0,                                                       1
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_SansIbpbExit_SansSpecCtrl_SseManual, 0, HM_WSF_IBPB_ENTRY,                                       1
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_SansIbpbExit_SansSpecCtrl_SseManual, 1, HM_WSF_IBPB_ENTRY,                                       1
hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_WithIbpbExit_SansSpecCtrl_SseManual, 0, HM_WSF_IBPB_EXIT,                                        1
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_WithIbpbExit_SansSpecCtrl_SseManual, 1, HM_WSF_IBPB_EXIT,                                        1
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_WithIbpbExit_SansSpecCtrl_SseManual, 0, HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT,                    1
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_WithIbpbExit_SansSpecCtrl_SseManual, 1, HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT,                    1
hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_SansIbpbExit_WithSpecCtrl_SseManual, 0, HM_WSF_SPEC_CTRL,                                        1
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_SansIbpbExit_WithSpecCtrl_SseManual, 1, HM_WSF_SPEC_CTRL,                                        1
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_SansIbpbExit_WithSpecCtrl_SseManual, 0, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY,                    1
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_SansIbpbExit_WithSpecCtrl_SseManual, 1, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY,                    1
hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_WithIbpbExit_WithSpecCtrl_SseManual, 0, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_EXIT,                     1
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_WithIbpbExit_WithSpecCtrl_SseManual, 1, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_EXIT,                     1
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_WithIbpbExit_WithSpecCtrl_SseManual, 0, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT, 1
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_WithIbpbExit_WithSpecCtrl_SseManual, 1, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT, 1

hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_SansIbpbExit_SansSpecCtrl_SseXSave,  0, 0,                                                       2
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_SansIbpbExit_SansSpecCtrl_SseXSave,  1, 0,                                                       2
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_SansIbpbExit_SansSpecCtrl_SseXSave,  0, HM_WSF_IBPB_ENTRY,                                       2
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_SansIbpbExit_SansSpecCtrl_SseXSave,  1, HM_WSF_IBPB_ENTRY,                                       2
hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_WithIbpbExit_SansSpecCtrl_SseXSave,  0, HM_WSF_IBPB_EXIT,                                        2
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_WithIbpbExit_SansSpecCtrl_SseXSave,  1, HM_WSF_IBPB_EXIT,                                        2
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_WithIbpbExit_SansSpecCtrl_SseXSave,  0, HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT,                    2
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_WithIbpbExit_SansSpecCtrl_SseXSave,  1, HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT,                    2
hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_SansIbpbExit_WithSpecCtrl_SseXSave,  0, HM_WSF_SPEC_CTRL,                                        2
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_SansIbpbExit_WithSpecCtrl_SseXSave,  1, HM_WSF_SPEC_CTRL,                                        2
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_SansIbpbExit_WithSpecCtrl_SseXSave,  0, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY,                    2
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_SansIbpbExit_WithSpecCtrl_SseXSave,  1, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY,                    2
hmR0SvmVmRunTemplate _SansXcr0_SansIbpbEntry_WithIbpbExit_WithSpecCtrl_SseXSave,  0, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_EXIT,                     2
hmR0SvmVmRunTemplate _WithXcr0_SansIbpbEntry_WithIbpbExit_WithSpecCtrl_SseXSave,  1, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_EXIT,                     2
hmR0SvmVmRunTemplate _SansXcr0_WithIbpbEntry_WithIbpbExit_WithSpecCtrl_SseXSave,  0, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT, 2
hmR0SvmVmRunTemplate _WithXcr0_WithIbpbEntry_WithIbpbExit_WithSpecCtrl_SseXSave,  1, HM_WSF_SPEC_CTRL | HM_WSF_IBPB_ENTRY | HM_WSF_IBPB_EXIT, 2
%endif
