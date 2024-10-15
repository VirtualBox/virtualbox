; $Id: VMMR0JmpA-amd64.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VMM - R0 SetJmp / LongJmp routines for AMD64.
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
%define RT_ASM_WITH_SEH64_ALT
%include "VBox/asmdefs.mac"
%include "VMMInternal.mac"
%include "VBox/err.mac"
%include "VBox/param.mac"


BEGINCODE

;;
; The setjmp variant used for calling Ring-3.
;
; This differs from the normal setjmp in that it will resume VMMRZCallRing3 if we're
; in the middle of a ring-3 call. Another differences is the function pointer and
; argument. This has to do with resuming code and the stack frame of the caller.
;
; @returns  VINF_SUCCESS on success or whatever is passed to vmmR0CallRing3LongJmp.
; @param    pJmpBuf msc:rcx gcc:rdi x86:[esp+0x04]     Our jmp_buf.
; @param    pfn     msc:rdx gcc:rsi x86:[esp+0x08]     The function to be called when not resuming.
; @param    pvUser1 msc:r8  gcc:rdx x86:[esp+0x0c]     The argument of that function.
; @param    pvUser2 msc:r9  gcc:rcx x86:[esp+0x10]     The argument of that function.
;
GLOBALNAME vmmR0CallRing3SetJmp2
GLOBALNAME vmmR0CallRing3SetJmpEx
BEGINPROC vmmR0CallRing3SetJmp
    ;
    ; Save the registers.
    ;
    push    rbp
    SEH64_PUSH_xBP
    mov     rbp, rsp
    SEH64_SET_FRAME_xBP 0
 %ifdef ASM_CALL64_MSC
    sub     rsp, 30h                    ; (10h is used by resume (??), 20h for callee spill area)
    SEH64_ALLOCATE_STACK 30h
SEH64_END_PROLOGUE
    mov     r11, rdx                    ; pfn
    mov     rdx, rcx                    ; pJmpBuf;
 %else
    sub     rsp, 10h                    ; (10h is used by resume (??))
    SEH64_ALLOCATE_STACK 10h
SEH64_END_PROLOGUE
    mov     r8, rdx                     ; pvUser1 (save it like MSC)
    mov     r9, rcx                     ; pvUser2 (save it like MSC)
    mov     r11, rsi                    ; pfn
    mov     rdx, rdi                    ; pJmpBuf
 %endif
    mov     [xDX + VMMR0JMPBUF.rbx], rbx
 %ifdef ASM_CALL64_MSC
    mov     [xDX + VMMR0JMPBUF.rsi], rsi
    mov     [xDX + VMMR0JMPBUF.rdi], rdi
 %endif
    mov     [xDX + VMMR0JMPBUF.rbp], rbp
    mov     [xDX + VMMR0JMPBUF.r12], r12
    mov     [xDX + VMMR0JMPBUF.r13], r13
    mov     [xDX + VMMR0JMPBUF.r14], r14
    mov     [xDX + VMMR0JMPBUF.r15], r15
    mov     xAX, [rbp + 8]              ; (not really necessary, except for validity check)
    mov     [xDX + VMMR0JMPBUF.rip], xAX
 %ifdef ASM_CALL64_MSC
    lea     r10, [rsp + 20h]            ; Must skip the callee spill area.
 %else
    mov     r10, rsp
 %endif
    mov     [xDX + VMMR0JMPBUF.rsp], r10
 %ifdef RT_OS_WINDOWS
    movdqa  [xDX + VMMR0JMPBUF.xmm6], xmm6
    movdqa  [xDX + VMMR0JMPBUF.xmm7], xmm7
    movdqa  [xDX + VMMR0JMPBUF.xmm8], xmm8
    movdqa  [xDX + VMMR0JMPBUF.xmm9], xmm9
    movdqa  [xDX + VMMR0JMPBUF.xmm10], xmm10
    movdqa  [xDX + VMMR0JMPBUF.xmm11], xmm11
    movdqa  [xDX + VMMR0JMPBUF.xmm12], xmm12
    movdqa  [xDX + VMMR0JMPBUF.xmm13], xmm13
    movdqa  [xDX + VMMR0JMPBUF.xmm14], xmm14
    movdqa  [xDX + VMMR0JMPBUF.xmm15], xmm15
 %endif
    pushf
    pop     xAX
    mov     [xDX + VMMR0JMPBUF.rflags], xAX

    ;
    ; Save the call then make it.
    ;
    mov     [xDX + VMMR0JMPBUF.pfn], r11
    mov     [xDX + VMMR0JMPBUF.pvUser1], r8
    mov     [xDX + VMMR0JMPBUF.pvUser2], r9

    mov     r12, rdx                    ; Save pJmpBuf.
 %ifdef ASM_CALL64_MSC
    mov     rcx, r8                     ; pvUser -> arg0
    mov     rdx, r9
 %else
    mov     rdi, r8                     ; pvUser -> arg0
    mov     rsi, r9
 %endif
    call    r11
    mov     rdx, r12                    ; Restore pJmpBuf

    ;
    ; Return like in the long jump but clear eip, no shortcuts here.
    ;
.proper_return:
%ifdef RT_OS_WINDOWS
    movdqa  xmm6,  [xDX + VMMR0JMPBUF.xmm6 ]
    movdqa  xmm7,  [xDX + VMMR0JMPBUF.xmm7 ]
    movdqa  xmm8,  [xDX + VMMR0JMPBUF.xmm8 ]
    movdqa  xmm9,  [xDX + VMMR0JMPBUF.xmm9 ]
    movdqa  xmm10, [xDX + VMMR0JMPBUF.xmm10]
    movdqa  xmm11, [xDX + VMMR0JMPBUF.xmm11]
    movdqa  xmm12, [xDX + VMMR0JMPBUF.xmm12]
    movdqa  xmm13, [xDX + VMMR0JMPBUF.xmm13]
    movdqa  xmm14, [xDX + VMMR0JMPBUF.xmm14]
    movdqa  xmm15, [xDX + VMMR0JMPBUF.xmm15]
%endif
    mov     rbx, [xDX + VMMR0JMPBUF.rbx]
%ifdef ASM_CALL64_MSC
    mov     rsi, [xDX + VMMR0JMPBUF.rsi]
    mov     rdi, [xDX + VMMR0JMPBUF.rdi]
%endif
    mov     r12, [xDX + VMMR0JMPBUF.r12]
    mov     r13, [xDX + VMMR0JMPBUF.r13]
    mov     r14, [xDX + VMMR0JMPBUF.r14]
    mov     r15, [xDX + VMMR0JMPBUF.r15]
    mov     rbp, [xDX + VMMR0JMPBUF.rbp]
    and     qword [xDX + VMMR0JMPBUF.rip], byte 0 ; used for valid check.
    mov     rsp, [xDX + VMMR0JMPBUF.rsp]
    push    qword [xDX + VMMR0JMPBUF.rflags]
    popf
    leave
    ret
ENDPROC vmmR0CallRing3SetJmp


;;
; Worker for VMMRZCallRing3.
; This will save the stack and registers.
;
; @param    pJmpBuf msc:rcx gcc:rdi x86:[ebp+8]     Pointer to the jump buffer.
; @param    rc      msc:rdx gcc:rsi x86:[ebp+c]     The return code.
;
BEGINPROC vmmR0CallRing3LongJmp
    ;
    ; Save the registers on the stack.
    ;
    push    rbp
    SEH64_PUSH_xBP
    mov     rbp, rsp
    SEH64_SET_FRAME_xBP 0
    push    r15
    SEH64_PUSH_GREG r15
    push    r14
    SEH64_PUSH_GREG r14
    push    r13
    SEH64_PUSH_GREG r13
    push    r12
    SEH64_PUSH_GREG r12
%ifdef ASM_CALL64_MSC
    push    rdi
    SEH64_PUSH_GREG rdi
    push    rsi
    SEH64_PUSH_GREG rsi
%endif
    push    rbx
    SEH64_PUSH_GREG rbx
    pushf
    SEH64_ALLOCATE_STACK 8
%ifdef RT_OS_WINDOWS
    sub     rsp, 0a0h
    SEH64_ALLOCATE_STACK 0a0h
    movdqa  [rsp + 000h], xmm6
    movdqa  [rsp + 010h], xmm7
    movdqa  [rsp + 020h], xmm8
    movdqa  [rsp + 030h], xmm9
    movdqa  [rsp + 040h], xmm10
    movdqa  [rsp + 050h], xmm11
    movdqa  [rsp + 060h], xmm12
    movdqa  [rsp + 070h], xmm13
    movdqa  [rsp + 080h], xmm14
    movdqa  [rsp + 090h], xmm15
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
    cmp     qword [xDX + VMMR0JMPBUF.rip], byte 0
    je      .nok

    ;
    ; Also check that the stack is in the vicinity of the RSP we entered
    ; on so the stack mirroring below doesn't go wild.
    ;
    mov     rsi, rsp
    mov     rcx, [xDX + VMMR0JMPBUF.rsp]
    sub     rcx, rsi
    cmp     rcx, _64K
    jnbe    .nok

    ;
    ; Save a PC and return PC here to assist unwinding.
    ;
.unwind_point:
    lea     rcx, [.unwind_point wrt RIP]
    mov     [xDX + VMMR0JMPBUF.UnwindPc], rcx
    mov     rcx, [xDX + VMMR0JMPBUF.rbp]
    lea     rcx, [rcx + 8]
    mov     [xDX + VMMR0JMPBUF.UnwindRetPcLocation], rcx
    mov     rcx, [rcx]
    mov     [xDX + VMMR0JMPBUF.UnwindRetPcValue], rcx

    ; Save RSP & RBP to enable stack dumps
    mov     [xDX + VMMR0JMPBUF.UnwindSp], rsp
    mov     rcx, rbp
    mov     [xDX + VMMR0JMPBUF.UnwindBp], rcx
    sub     rcx, 8
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

    mov     rcx, [xDX + VMMR0JMPBUF.rsp]
    sub     rcx, rsp
    and     rcx, ~0fffh                 ; copy up to the page boundrary

    cmp     rcx, rbx                    ; rbx = rcx = RT_MIN(rbx, rcx);
    jbe     .do_stack_buffer_big_enough
    mov     ecx, ebx                    ; too much to copy, limit to ebx
    jmp     .do_stack_copying
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
    ; Do the long jump.
    ;
%ifdef RT_OS_WINDOWS
    movdqa  xmm6,  [xDX + VMMR0JMPBUF.xmm6 ]
    movdqa  xmm7,  [xDX + VMMR0JMPBUF.xmm7 ]
    movdqa  xmm8,  [xDX + VMMR0JMPBUF.xmm8 ]
    movdqa  xmm9,  [xDX + VMMR0JMPBUF.xmm9 ]
    movdqa  xmm10, [xDX + VMMR0JMPBUF.xmm10]
    movdqa  xmm11, [xDX + VMMR0JMPBUF.xmm11]
    movdqa  xmm12, [xDX + VMMR0JMPBUF.xmm12]
    movdqa  xmm13, [xDX + VMMR0JMPBUF.xmm13]
    movdqa  xmm14, [xDX + VMMR0JMPBUF.xmm14]
    movdqa  xmm15, [xDX + VMMR0JMPBUF.xmm15]
%endif
    mov     rbx, [xDX + VMMR0JMPBUF.rbx]
%ifdef ASM_CALL64_MSC
    mov     rsi, [xDX + VMMR0JMPBUF.rsi]
    mov     rdi, [xDX + VMMR0JMPBUF.rdi]
%endif
    mov     r12, [xDX + VMMR0JMPBUF.r12]
    mov     r13, [xDX + VMMR0JMPBUF.r13]
    mov     r14, [xDX + VMMR0JMPBUF.r14]
    mov     r15, [xDX + VMMR0JMPBUF.r15]
    mov     rbp, [xDX + VMMR0JMPBUF.rbp]
    mov     rsp, [xDX + VMMR0JMPBUF.rsp]
    push    qword [xDX + VMMR0JMPBUF.rflags]
    popf
    leave
    ret

    ;
    ; Failure
    ;
.nok:
    mov     eax, VERR_VMM_LONG_JMP_ERROR
%ifdef RT_OS_WINDOWS
    add     rsp, 0a0h                   ; skip XMM registers since they are unmodified.
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

