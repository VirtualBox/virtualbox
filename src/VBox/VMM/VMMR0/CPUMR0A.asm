 ; $Id: CPUMR0A.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; CPUM - Ring-0 Assembly Routines (supporting HM and IEM).
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


;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%define RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"
%include "VBox/asmdefs.mac"
%include "VBox/vmm/vm.mac"
%include "VBox/err.mac"
%include "VBox/vmm/stam.mac"
%include "CPUMInternal.mac"
%include "iprt/x86.mac"
%include "VBox/vmm/cpum.mac"


BEGINCODE

;;
; Makes sure the EMTs have a FPU state associated with them on hosts where we're
; allowed to use it in ring-0 too.
;
; This ensure that we don't have to allocate the state lazily while trying to execute
; guest code with preemption disabled or worse.
;
; @cproto VMMR0_INT_DECL(void) CPUMR0RegisterVCpuThread(PVMCPU pVCpu);
;
BEGINPROC CPUMR0RegisterVCpuThread
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

%ifdef VMM_R0_TOUCH_FPU
        movdqa  xmm0, xmm0              ; hope this is harmless.
%endif

.return:
        xor     eax, eax                ; paranoia
        leave
        ret
ENDPROC   CPUMR0RegisterVCpuThread


%ifdef VMM_R0_TOUCH_FPU
;;
; Touches the host FPU state.
;
; @uses nothing (well, maybe cr0)
;
 %ifndef RT_ASM_WITH_SEH64 ; workaround for yasm 1.3.0 bug (error: prologue -1 bytes, must be <256)
ALIGNCODE(16)
 %endif
BEGINPROC CPUMR0TouchHostFpu
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

        movdqa  xmm0, xmm0              ; Hope this is harmless.

        leave
        ret
ENDPROC   CPUMR0TouchHostFpu
%endif ; VMM_R0_TOUCH_FPU


;;
; Saves the host FPU/SSE/AVX state and restores the guest FPU/SSE/AVX state.
;
; @returns  VINF_SUCCESS (0) or VINF_CPUM_HOST_CR0_MODIFIED. (EAX)
; @param    pCpumCpu  x86:[ebp+8] gcc:rdi msc:rcx     CPUMCPU pointer
;
; @remarks  64-bit Windows drivers shouldn't use AVX registers without saving+loading:
;               https://msdn.microsoft.com/en-us/library/windows/hardware/ff545910%28v=vs.85%29.aspx?f=255&MSPPError=-2147217396
;           However the compiler docs have different idea:
;               https://msdn.microsoft.com/en-us/library/9z1stfyw.aspx
;           We'll go with the former for now.
;
%ifndef RT_ASM_WITH_SEH64 ; workaround for yasm 1.3.0 bug (error: prologue -1 bytes, must be <256)
ALIGNCODE(16)
%endif
BEGINPROC cpumR0SaveHostRestoreGuestFPUState
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

        ;
        ; Prologue - xAX+xDX must be free for XSAVE/XRSTOR input.
        ;
%ifdef RT_ARCH_AMD64
 %ifdef RT_OS_WINDOWS
        mov     r11, rcx
 %else
        mov     r11, rdi
 %endif
 %define pCpumCpu   r11
 %define pXState    r10
%else
        push    ebx
        push    esi
        mov     ebx, dword [ebp + 8]
 %define pCpumCpu ebx
 %define pXState  esi
%endif

        pushf                           ; The darwin kernel can get upset or upset things if an
        cli                             ; interrupt occurs while we're doing fxsave/fxrstor/cr0.

        ;
        ; Save the host state.
        ;
        test    dword [pCpumCpu + CPUMCPU.fUseFlags], CPUM_USED_FPU_HOST
        jnz     .already_saved_host

        CPUMRZ_TOUCH_FPU_CLEAR_CR0_FPU_TRAPS_SET_RC xCX, xAX, pCpumCpu ; xCX is the return value for VT-x; xAX is scratch.

        CPUMR0_SAVE_HOST

%ifdef VBOX_WITH_KERNEL_USING_XMM
        jmp     .load_guest
%endif
.already_saved_host:
%ifdef VBOX_WITH_KERNEL_USING_XMM
        ; If we didn't save the host state, we must save the non-volatile XMM registers.
        lea     pXState, [pCpumCpu + CPUMCPU.Host.XState]
        stmxcsr [pXState + X86FXSTATE.MXCSR]
        movdqa  [pXState + X86FXSTATE.xmm6 ], xmm6
        movdqa  [pXState + X86FXSTATE.xmm7 ], xmm7
        movdqa  [pXState + X86FXSTATE.xmm8 ], xmm8
        movdqa  [pXState + X86FXSTATE.xmm9 ], xmm9
        movdqa  [pXState + X86FXSTATE.xmm10], xmm10
        movdqa  [pXState + X86FXSTATE.xmm11], xmm11
        movdqa  [pXState + X86FXSTATE.xmm12], xmm12
        movdqa  [pXState + X86FXSTATE.xmm13], xmm13
        movdqa  [pXState + X86FXSTATE.xmm14], xmm14
        movdqa  [pXState + X86FXSTATE.xmm15], xmm15

        ;
        ; Load the guest state.
        ;
.load_guest:
%endif
        CPUMR0_LOAD_GUEST

%ifdef VBOX_WITH_KERNEL_USING_XMM
        ; Restore the non-volatile xmm registers. ASSUMING 64-bit host.
        lea     pXState, [pCpumCpu + CPUMCPU.Host.XState]
        movdqa  xmm6,  [pXState + X86FXSTATE.xmm6]
        movdqa  xmm7,  [pXState + X86FXSTATE.xmm7]
        movdqa  xmm8,  [pXState + X86FXSTATE.xmm8]
        movdqa  xmm9,  [pXState + X86FXSTATE.xmm9]
        movdqa  xmm10, [pXState + X86FXSTATE.xmm10]
        movdqa  xmm11, [pXState + X86FXSTATE.xmm11]
        movdqa  xmm12, [pXState + X86FXSTATE.xmm12]
        movdqa  xmm13, [pXState + X86FXSTATE.xmm13]
        movdqa  xmm14, [pXState + X86FXSTATE.xmm14]
        movdqa  xmm15, [pXState + X86FXSTATE.xmm15]
        ldmxcsr        [pXState + X86FXSTATE.MXCSR]
%endif

        or      dword [pCpumCpu + CPUMCPU.fUseFlags], (CPUM_USED_FPU_GUEST | CPUM_USED_FPU_SINCE_REM | CPUM_USED_FPU_HOST)
        mov     byte [pCpumCpu + CPUMCPU.Guest.fUsedFpuGuest], 1
        popf

        mov     eax, ecx
.return:
%ifdef RT_ARCH_X86
        pop     esi
        pop     ebx
%endif
        leave
        ret
ENDPROC   cpumR0SaveHostRestoreGuestFPUState


;;
; Saves the guest FPU/SSE/AVX state and restores the host FPU/SSE/AVX state.
;
; @param    pCpumCpu  x86:[ebp+8] gcc:rdi msc:rcx     CPUMCPU pointer
;
; @remarks  64-bit Windows drivers shouldn't use AVX registers without saving+loading:
;               https://msdn.microsoft.com/en-us/library/windows/hardware/ff545910%28v=vs.85%29.aspx?f=255&MSPPError=-2147217396
;           However the compiler docs have different idea:
;               https://msdn.microsoft.com/en-us/library/9z1stfyw.aspx
;           We'll go with the former for now.
;
%ifndef RT_ASM_WITH_SEH64 ; workaround for yasm 1.3.0 bug (error: prologue -1 bytes, must be <256)
ALIGNCODE(16)
%endif
BEGINPROC cpumR0SaveGuestRestoreHostFPUState
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

        ;
        ; Prologue - xAX+xDX must be free for XSAVE/XRSTOR input.
        ;
%ifdef RT_ARCH_AMD64
 %ifdef RT_OS_WINDOWS
        mov     r11, rcx
 %else
        mov     r11, rdi
 %endif
 %define pCpumCpu   r11
 %define pXState    r10
%else
        push    ebx
        push    esi
        mov     ebx, dword [ebp + 8]
 %define pCpumCpu   ebx
 %define pXState    esi
%endif
        pushf                           ; The darwin kernel can get upset or upset things if an
        cli                             ; interrupt occurs while we're doing fxsave/fxrstor/cr0.

 %ifdef VBOX_WITH_KERNEL_USING_XMM
        ;
        ; Copy non-volatile XMM registers to the host state so we can use
        ; them while saving the guest state (we've gotta do this anyway).
        ;
        lea     pXState, [pCpumCpu + CPUMCPU.Host.XState]
        stmxcsr [pXState + X86FXSTATE.MXCSR]
        movdqa  [pXState + X86FXSTATE.xmm6], xmm6
        movdqa  [pXState + X86FXSTATE.xmm7], xmm7
        movdqa  [pXState + X86FXSTATE.xmm8], xmm8
        movdqa  [pXState + X86FXSTATE.xmm9], xmm9
        movdqa  [pXState + X86FXSTATE.xmm10], xmm10
        movdqa  [pXState + X86FXSTATE.xmm11], xmm11
        movdqa  [pXState + X86FXSTATE.xmm12], xmm12
        movdqa  [pXState + X86FXSTATE.xmm13], xmm13
        movdqa  [pXState + X86FXSTATE.xmm14], xmm14
        movdqa  [pXState + X86FXSTATE.xmm15], xmm15
 %endif

        ;
        ; Save the guest state if necessary.
        ;
        test    dword [pCpumCpu + CPUMCPU.fUseFlags], CPUM_USED_FPU_GUEST
        jz      .load_only_host

 %ifdef VBOX_WITH_KERNEL_USING_XMM
        ; Load the guest XMM register values we already saved in HMR0VMXStartVMWrapXMM.
        lea     pXState, [pCpumCpu + CPUMCPU.Guest.XState]
        movdqa  xmm0,  [pXState + X86FXSTATE.xmm0]
        movdqa  xmm1,  [pXState + X86FXSTATE.xmm1]
        movdqa  xmm2,  [pXState + X86FXSTATE.xmm2]
        movdqa  xmm3,  [pXState + X86FXSTATE.xmm3]
        movdqa  xmm4,  [pXState + X86FXSTATE.xmm4]
        movdqa  xmm5,  [pXState + X86FXSTATE.xmm5]
        movdqa  xmm6,  [pXState + X86FXSTATE.xmm6]
        movdqa  xmm7,  [pXState + X86FXSTATE.xmm7]
        movdqa  xmm8,  [pXState + X86FXSTATE.xmm8]
        movdqa  xmm9,  [pXState + X86FXSTATE.xmm9]
        movdqa  xmm10, [pXState + X86FXSTATE.xmm10]
        movdqa  xmm11, [pXState + X86FXSTATE.xmm11]
        movdqa  xmm12, [pXState + X86FXSTATE.xmm12]
        movdqa  xmm13, [pXState + X86FXSTATE.xmm13]
        movdqa  xmm14, [pXState + X86FXSTATE.xmm14]
        movdqa  xmm15, [pXState + X86FXSTATE.xmm15]
        ldmxcsr        [pXState + X86FXSTATE.MXCSR]
 %endif
        CPUMR0_SAVE_GUEST

        ;
        ; Load the host state.
        ;
.load_only_host:
        CPUMR0_LOAD_HOST

        ; Restore the CR0 value we saved in cpumR0SaveHostRestoreGuestFPUState or
        ; in cpumRZSaveHostFPUState.
        mov     xCX, [pCpumCpu + CPUMCPU.Host.cr0Fpu]
        CPUMRZ_RESTORE_CR0_IF_TS_OR_EM_SET xCX
        and     dword [pCpumCpu + CPUMCPU.fUseFlags], ~(CPUM_USED_FPU_GUEST | CPUM_USED_FPU_HOST)
        mov     byte [pCpumCpu + CPUMCPU.Guest.fUsedFpuGuest], 0

        popf
%ifdef RT_ARCH_X86
        pop     esi
        pop     ebx
%endif
        leave
        ret
%undef pCpumCpu
%undef pXState
ENDPROC   cpumR0SaveGuestRestoreHostFPUState


%if ARCH_BITS == 32
 %ifdef VBOX_WITH_64_BITS_GUESTS
;;
; Restores the host's FPU/SSE/AVX state from pCpumCpu->Host.
;
; @param    pCpumCpu  x86:[ebp+8] gcc:rdi msc:rcx     CPUMCPU pointer
;
  %ifndef RT_ASM_WITH_SEH64 ; workaround for yasm 1.3.0 bug (error: prologue -1 bytes, must be <256)
ALIGNCODE(16)
  %endif
BEGINPROC cpumR0RestoreHostFPUState
        ;
        ; Prologue - xAX+xDX must be free for XSAVE/XRSTOR input.
        ;
        push    ebp
        mov     ebp, esp
        push    ebx
        push    esi
        mov     ebx, dword [ebp + 8]
  %define pCpumCpu ebx
  %define pXState  esi

        ;
        ; Restore host CPU state.
        ;
        pushf                           ; The darwin kernel can get upset or upset things if an
        cli                             ; interrupt occurs while we're doing fxsave/fxrstor/cr0.

        CPUMR0_LOAD_HOST

        ; Restore the CR0 value we saved in cpumR0SaveHostRestoreGuestFPUState or
        ; in cpumRZSaveHostFPUState.
        ;; @todo What about XCR0?
        mov     xCX, [pCpumCpu + CPUMCPU.Host.cr0Fpu]
        CPUMRZ_RESTORE_CR0_IF_TS_OR_EM_SET xCX

        and     dword [pCpumCpu + CPUMCPU.fUseFlags], ~CPUM_USED_FPU_HOST
        popf

        pop     esi
        pop     ebx
        leave
        ret
  %undef pCpumCPu
  %undef pXState
ENDPROC   cpumR0RestoreHostFPUState
 %endif ; VBOX_WITH_64_BITS_GUESTS
%endif  ; ARCH_BITS == 32

