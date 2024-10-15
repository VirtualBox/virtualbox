 ; $Id: CPUMRZA.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; CPUM - Raw-mode and Ring-0 Context Assembly Routines.
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
%include "VBox/asmdefs.mac"
%include "CPUMInternal.mac"
%include "iprt/x86.mac"
%include "VBox/vmm/cpum.mac"
%include "VBox/err.mac"



BEGINCODE


;;
; Saves the host FPU/SSE/AVX state.
;
; Will return with CR0.EM and CR0.TS cleared!  This is the normal state in ring-0.
;
; @returns  VINF_SUCCESS (0) or VINF_CPUM_HOST_CR0_MODIFIED. (EAX)
; @param    pCpumCpu  x86:[ebp+8] gcc:rdi msc:rcx     CPUMCPU pointer
;
align 16
BEGINPROC cpumRZSaveHostFPUState
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

        ;
        ; Prologue - xAX+xDX must be free for XSAVE/XRSTOR input.
        ;
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
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
        ; We may have to update CR0, indirectly or directly.  We must report any
        ; changes to the VT-x code.
        ;
        CPUMRZ_TOUCH_FPU_CLEAR_CR0_FPU_TRAPS_SET_RC xCX, xAX, pCpumCpu ; xCX is the return value (xAX scratch)

        ;
        ; Save the host state (xsave/fxsave will cause thread FPU state to be
        ; loaded on systems where we are allowed to use it in ring-0.
        ;
        CPUMR0_SAVE_HOST

        or      dword [pCpumCpu + CPUMCPU.fUseFlags], (CPUM_USED_FPU_HOST | CPUM_USED_FPU_SINCE_REM) ; Latter is not necessarily true, but normally yes.
        popf

        mov     eax, ecx                ; The return value from above.
%ifdef RT_ARCH_X86
        pop     esi
        pop     ebx
%endif
        leave
        ret
%undef pCpumCpu
%undef pXState
ENDPROC   cpumRZSaveHostFPUState


;;
; Saves the guest FPU/SSE/AVX state.
;
; @param    pCpumCpu  x86:[ebp+8] gcc:rdi msc:rcx     CPUMCPU pointer
; @param    fLeaveFpuAccessible  x86:[ebp+c] gcc:sil msc:dl      Whether to restore CR0 and XCR0 on
;                                                                the way out. Only really applicable to RC.
;
; @remarks  64-bit Windows drivers shouldn't use AVX registers without saving+loading:
;               https://msdn.microsoft.com/en-us/library/windows/hardware/ff545910%28v=vs.85%29.aspx?f=255&MSPPError=-2147217396
;           However the compiler docs have different idea:
;               https://msdn.microsoft.com/en-us/library/9z1stfyw.aspx
;           We'll go with the former for now.
;
align 16
BEGINPROC cpumRZSaveGuestFpuState
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

        ;
        ; Prologue - xAX+xDX must be free for XSAVE/XRSTOR input.
        ;
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
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

 %ifdef IN_RC
        mov     ecx, cr0                ; ecx = saved cr0
        test    ecx, X86_CR0_TS | X86_CR0_EM
        jz      .skip_cr0_write
        mov     eax, ecx
        and     eax, ~(X86_CR0_TS | X86_CR0_EM)
        mov     cr0, eax
.skip_cr0_write:
 %endif

 %ifndef VBOX_WITH_KERNEL_USING_XMM
        CPUMR0_SAVE_GUEST
 %else
        ;
        ; The XMM0..XMM15 registers have been saved already.  We exploit the
        ; host state here to temporarly save the non-volatile XMM registers,
        ; so we can load the guest ones while saving.  This is safe.
        ;

        ; Save caller's XMM registers.
        lea     pXState, [pCpumCpu + CPUMCPU.Host.XState]
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
        stmxcsr [pXState + X86FXSTATE.MXCSR]

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

        CPUMR0_SAVE_GUEST

        ; Restore caller's XMM registers.
        lea     pXState, [pCpumCpu + CPUMCPU.Host.XState]
        movdqa  xmm6,  [pXState + X86FXSTATE.xmm6 ]
        movdqa  xmm7,  [pXState + X86FXSTATE.xmm7 ]
        movdqa  xmm8,  [pXState + X86FXSTATE.xmm8 ]
        movdqa  xmm9,  [pXState + X86FXSTATE.xmm9 ]
        movdqa  xmm10, [pXState + X86FXSTATE.xmm10]
        movdqa  xmm11, [pXState + X86FXSTATE.xmm11]
        movdqa  xmm12, [pXState + X86FXSTATE.xmm12]
        movdqa  xmm13, [pXState + X86FXSTATE.xmm13]
        movdqa  xmm14, [pXState + X86FXSTATE.xmm14]
        movdqa  xmm15, [pXState + X86FXSTATE.xmm15]
        ldmxcsr        [pXState + X86FXSTATE.MXCSR]

 %endif

        and     dword [pCpumCpu + CPUMCPU.fUseFlags], ~CPUM_USED_FPU_GUEST
        mov     byte [pCpumCpu + CPUMCPU.Guest.fUsedFpuGuest], 0
 %ifdef IN_RC
        test    byte [ebp + 0ch], 1     ; fLeaveFpuAccessible
        jz      .no_cr0_restore
        CPUMRZ_RESTORE_CR0_IF_TS_OR_EM_SET ecx
.no_cr0_restore:
 %endif
        popf
%ifdef RT_ARCH_X86
        pop     esi
        pop     ebx
%endif
        leave
        ret
%undef pCpumCpu
%undef pXState
ENDPROC   cpumRZSaveGuestFpuState


;;
; Saves the guest XMM0..15 registers and MXCSR.
;
; The purpose is to actualize the register state for read-only use, so CR0 is
; restored in raw-mode context (so, the FPU/SSE/AVX CPU features can be
; inaccessible upon return).
;
; @param    pCpumCpu  x86:[ebp+8] gcc:rdi msc:rcx     CPUMCPU pointer
;
align 16
BEGINPROC cpumRZSaveGuestSseRegisters
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

%ifndef VBOX_WITH_KERNEL_USING_XMM
        ;
        ; Load xCX with the guest pXState.
        ;
 %ifdef ASM_CALL64_GCC
        mov     xCX, rdi
 %elifdef RT_ARCH_X86
        mov     xCX, dword [ebp + 8]
 %endif
        lea     xCX, [xCX + CPUMCPU.Guest.XState]

 %ifdef IN_RC
        ; Temporarily grant access to the SSE state. xDX must be preserved until CR0 is restored!
        mov     edx, cr0
        test    edx, X86_CR0_TS | X86_CR0_EM
        jz      .skip_cr0_write
        mov     eax, edx
        and     eax, ~(X86_CR0_TS | X86_CR0_EM)
        mov     cr0, eax
.skip_cr0_write:
 %endif

        ;
        ; Do the job.
        ;
        stmxcsr [xCX + X86FXSTATE.MXCSR]
        movdqa  [xCX + X86FXSTATE.xmm0 ], xmm0
        movdqa  [xCX + X86FXSTATE.xmm1 ], xmm1
        movdqa  [xCX + X86FXSTATE.xmm2 ], xmm2
        movdqa  [xCX + X86FXSTATE.xmm3 ], xmm3
        movdqa  [xCX + X86FXSTATE.xmm4 ], xmm4
        movdqa  [xCX + X86FXSTATE.xmm5 ], xmm5
        movdqa  [xCX + X86FXSTATE.xmm6 ], xmm6
        movdqa  [xCX + X86FXSTATE.xmm7 ], xmm7
 %if ARCH_BITS == 64
        movdqa  [xCX + X86FXSTATE.xmm8 ], xmm8
        movdqa  [xCX + X86FXSTATE.xmm9 ], xmm9
        movdqa  [xCX + X86FXSTATE.xmm10], xmm10
        movdqa  [xCX + X86FXSTATE.xmm11], xmm11
        movdqa  [xCX + X86FXSTATE.xmm12], xmm12
        movdqa  [xCX + X86FXSTATE.xmm13], xmm13
        movdqa  [xCX + X86FXSTATE.xmm14], xmm14
        movdqa  [xCX + X86FXSTATE.xmm15], xmm15
 %endif

 %ifdef IN_RC
        CPUMRZ_RESTORE_CR0_IF_TS_OR_EM_SET edx  ; Restore CR0 if we changed it above.
 %endif

%endif ; !VBOX_WITH_KERNEL_USING_XMM

        leave
        ret
ENDPROC   cpumRZSaveGuestSseRegisters

;;
; Saves the guest YMM0..15 registers.
;
; The purpose is to actualize the register state for read-only use, so CR0 is
; restored in raw-mode context (so, the FPU/SSE/AVX CPU features can be
; inaccessible upon return).
;
; @param    pCpumCpu  x86:[ebp+8] gcc:rdi msc:rcx     CPUMCPU pointer
;
align 16
BEGINPROC cpumRZSaveGuestAvxRegisters
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
%ifdef IN_RC
        push    xBX
%endif
SEH64_END_PROLOGUE

        ;
        ; Load xCX with the guest pXState.
        ;
%ifdef ASM_CALL64_GCC
        mov     xCX, rdi
%elifdef RT_ARCH_X86
        mov     xCX, dword [ebp + 8]
%endif
        lea     xCX, [xCX + CPUMCPU.Guest.XState]

%ifdef IN_RC
        ; Temporarily grant access to the SSE state. xBX must be preserved until CR0 is restored!
        mov     ebx, cr0
        test    ebx, X86_CR0_TS | X86_CR0_EM
        jz      .skip_cr0_write
        mov     eax, ebx
        and     eax, ~(X86_CR0_TS | X86_CR0_EM)
        mov     cr0, eax
.skip_cr0_write:
%endif

        ;
        ; Use XSAVE to do the job.
        ;
        ; Drivers shouldn't use AVX registers without saving+loading:
        ;     https://msdn.microsoft.com/en-us/library/windows/hardware/ff545910%28v=vs.85%29.aspx?f=255&MSPPError=-2147217396
        ; However the compiler docs have different idea:
        ;     https://msdn.microsoft.com/en-us/library/9z1stfyw.aspx
        ; We'll go with the former for now.
        ;
%ifdef VBOX_WITH_KERNEL_USING_XMM
        mov     eax, XSAVE_C_YMM
%else
        mov     eax, XSAVE_C_YMM | XSAVE_C_SSE ; The SSE component includes MXCSR.
%endif
        xor     edx, edx
%if ARCH_BITS == 64
        o64 xsave [xCX]
%else
        xsave   [xCX]
%endif

%ifdef IN_RC
        CPUMRZ_RESTORE_CR0_IF_TS_OR_EM_SET ebx  ; Restore CR0 if we changed it above.
        pop     xBX
%endif
        leave
        ret
ENDPROC   cpumRZSaveGuestAvxRegisters

