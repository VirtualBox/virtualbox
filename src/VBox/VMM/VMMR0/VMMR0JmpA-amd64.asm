; $Id: VMMR0JmpA-amd64.asm 115015 2026-08-12 23:40:14Z knut.osmundsen@oracle.com $
;; @file
; VMM - R0 SetJmp / LongJmp routines for AMD64.
;

;
; Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
%define RT_ASM_WITH_SEH64_ALT
%include "VBox/asmdefs.mac"
%include "VMMInternal.mac"
%include "VBox/err.mac"
%include "VBox/param.mac"
%include "iprt/x86.mac"


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
extern NAME(RT_NOCRT(longjmp))


%ifdef RT_STRICT
 %define INCLUDE_NON_VOLATILE_REGS
%endif

BEGINCODE


;;
; This will save the stack and registers.
;
; @param    pJmpBuf msc:rcx gcc:rdi x86:[ebp+8]     Pointer to the jump buffer.
; @param    rc      msc:rdx gcc:rsi x86:[ebp+c]     The return code.
;
BEGINPROC vmmR0CallRing3LongJmp
    ;
    ; Save the registers on the stack (benefits ring-3 analysis).
    ;
    push    rbp                             ; paBP[0]
    SEH64_PUSH_xBP
    mov     rbp, rsp
    ;SEH64_SET_FRAME_xBP 0 - Do NOT include this (unnecessary). No initial RBP in the guru stack walking code, so we end up with
    ;                        RBP=0 afterwards, which isn't very useful...
    push    r15                             ; paBP[-1]
    SEH64_PUSH_GREG r15
    push    r14                             ; paBP[-2]
    SEH64_PUSH_GREG r14
    push    r13                             ; paBP[-3]
    SEH64_PUSH_GREG r13
    push    r12                             ; paBP[-4]
    SEH64_PUSH_GREG r12
    push    rbx                             ; paBP[-5]
    SEH64_PUSH_GREG rbx
    pushf                                   ; paBP[-6]
    SEH64_ALLOCATE_STACK 8
%ifdef ASM_CALL64_MSC
    push    rdi                             ; paBP[-7]
    SEH64_PUSH_GREG rdi
    push    rsi                             ; paBP[-8]
    SEH64_PUSH_GREG rsi
%endif
%ifdef INCLUDE_NON_VOLATILE_REGS
 %ifndef ASM_CALL64_MSC
    push    rdi
    SEH64_PUSH_GREG rdi
    push    rsi
    SEH64_PUSH_GREG rsi
 %endif
    push    0x0badf00d                      ; paBP[-9] (magic)
    SEH64_ALLOCATE_STACK 8
    push    r11                             ; paBP[-10]
    SEH64_PUSH_GREG r11
    push    r10                             ; paBP[-11]
    SEH64_PUSH_GREG r10
    push    r9                              ; paBP[-12]
    SEH64_PUSH_GREG r9
    push    r8                              ; paBP[-13]
    SEH64_PUSH_GREG r8
    push    rdx                             ; paBP[-14]
    SEH64_PUSH_GREG rdx
    push    rcx                             ; paBP[-15]
    SEH64_PUSH_GREG rcx
    push    rax                             ; paBP[-16]
    SEH64_PUSH_GREG rax
%endif
%ifdef ASM_CALL64_MSC
    sub     rsp, 0a0h
    SEH64_ALLOCATE_STACK 0a0h
    movdqa  [rsp + 000h], xmm6
    SEH64_SAVE_XMM128 xmm6, 00h
    movdqa  [rsp + 010h], xmm7
    SEH64_SAVE_XMM128 xmm7, 10h
    movdqa  [rsp + 020h], xmm8
    SEH64_SAVE_XMM128 xmm8, 20h
    movdqa  [rsp + 030h], xmm9
    SEH64_SAVE_XMM128 xmm9, 30h
    movdqa  [rsp + 040h], xmm10
    SEH64_SAVE_XMM128 xmm10, 40h
    movdqa  [rsp + 050h], xmm11
    SEH64_SAVE_XMM128 xmm11, 50h
    movdqa  [rsp + 060h], xmm12
    SEH64_SAVE_XMM128 xmm12, 60h
    movdqa  [rsp + 070h], xmm13
    SEH64_SAVE_XMM128 xmm13, 70h
    movdqa  [rsp + 080h], xmm14
    SEH64_SAVE_XMM128 xmm14, 80h
    movdqa  [rsp + 090h], xmm15
    SEH64_SAVE_XMM128 xmm15, 90h
%endif
SEH64_END_PROLOGUE

    ;
    ; Normalize the parameters.
    ;
%ifdef ASM_CALL64_MSC
    mov     eax, edx                    ; rc
    mov     rdx, rcx                    ; pJmpBuf
%else
    mov     rdx, rdi                    ; pJmpBuf
    mov     eax, esi                    ; rc
%endif

    ;
    ; Is the jump buffer armed?
    ;
    cmp     qword [xDX + VMMR0JMPBUF.Core.s.rip], byte 0
    je      .nok

    ;
    ; Also check that the stack is in the vicinity of the RSP we entered
    ; on so the stack mirroring below doesn't go wild.
    ;
    mov     rsi, rsp
    mov     rcx, [xDX + VMMR0JMPBUF.Core.s.rsp]
    sub     rcx, rsi
    cmp     rcx, _64K
    jnbe    .nok

    ;
    ; Save a PC and return PC here to assist unwinding in the debugger.
    ;
.unwind_point:
    lea     rcx, [RT_WRT_RIP(.unwind_point)]
    mov     [xDX + VMMR0JMPBUF.UnwindPc], rcx
    mov     rcx, [xDX + VMMR0JMPBUF.Core.s.rbp]
    lea     rcx, [rcx + 8]
    mov     [xDX + VMMR0JMPBUF.UnwindRetPcLocation], rcx
    mov     rcx, [rcx]
    mov     [xDX + VMMR0JMPBUF.UnwindRetPcValue], rcx

    ; Save RSP & RBP to enable stack dumps
    mov     [xDX + VMMR0JMPBUF.UnwindSp], rsp
    mov     rcx, rbp
    mov     [xDX + VMMR0JMPBUF.UnwindBp], rcx
    sub     rcx, 8                   ; ??  maybe 16?
    mov     [xDX + VMMR0JMPBUF.UnwindRetSp], rcx

    ;
    ; Make sure the direction flag is clear before we do any rep movsb below.
    ;
    cld

    ;
    ; Mirror the stack.
    ;
    xor     ebx, ebx

    mov     rdi, [xDX + VMMR0JMPBUF.pvStackBuf]
    or      rdi, rdi
    jz      .skip_stack_mirroring

    mov     ebx, [xDX + VMMR0JMPBUF.cbStackBuf]
    or      ebx, ebx
    jz      .skip_stack_mirroring

    mov     rcx, [xDX + VMMR0JMPBUF.Core.s.rsp]
    or      rcx, X86_PAGE_OFFSET_MASK   ; copy up to the page boundrary if possible
    sub     rcx, rsp

    cmp     rcx, rbx                    ; rbx = rcx = RT_MIN(rbx, rcx);
    jbe     .do_stack_buffer_big_enough
    mov     ecx, ebx                    ; too much to copy, limit to ebx
    jmp     .do_stack_copying
    int3
.do_stack_buffer_big_enough:
    mov     ebx, ecx                    ; ecx is smaller, update ebx for cbStackValid

.do_stack_copying:
    mov     rsi, rsp
    rep movsb

.skip_stack_mirroring:
    mov     [xDX + VMMR0JMPBUF.cbStackValid], ebx

    ;
    ; Do buffer mirroring.
    ;
    mov     rdi, [xDX + VMMR0JMPBUF.pMirrorBuf]
    or      rdi, rdi
    jz      .skip_buffer_mirroring
    mov     rsi, rdx
    mov     ecx, VMMR0JMPBUF_size
    rep movsb
.skip_buffer_mirroring:

    ;
    ; Call the actual long jump code.
    ;
%ifdef ASM_CALL64_MSC
    mov     rcx, rdx                        ; Parameter 0 - jmp_buf pointer.
    mov     rdx, rax                        ; Parameter 1 - return code.
%else
    mov     rdi, rdx                        ; Parameter 0 - jmp_buf pointer.
    mov     rsi, rax                        ; Parameter 1 - return code.
%endif
%ifdef RT_STRICT
    test    rsp, 15
    jz      .the_stack_is_aligned
    int3
.the_stack_is_aligned:
%endif
%ifdef VBOX_WITH_VBOXR0_AS_DLL
    call    NAME(RT_NOCRT(longjmp)) wrt ..plt
%else
    call    NAME(RT_NOCRT(longjmp))
%endif

.unexpected_return_loop:
    int3
    jmp     .unexpected_return_loop
    int3

    ;
    ; Failure
    ;
.nok:
    mov     eax, VERR_VMM_LONG_JMP_ERROR
%ifdef ASM_CALL64_MSC
 %ifdef INCLUDE_NON_VOLATILE_REGS
    add     rsp, 0a0h + ((1+7)*8)       ; skip XMM registers since they are unmodified, alignment and the volatile registers.
 %else
    add     rsp, 0a0h                   ; skip XMM registers since they are unmodified.
 %endif
%elifdef INCLUDE_NON_VOLATILE_REGS
    add     rsp, ((1+9)*8)              ; alignment and the volatile registers.
%endif
    popf
    pop     rbx
%ifdef ASM_CALL64_MSC
    pop     rsi
    pop     rdi
%endif
    pop     r12
    pop     r13
    pop     r14
    pop     r15
    leave
    ret
ENDPROC vmmR0CallRing3LongJmp

MARK_OBJECT_RETPOLINE_SAFE
