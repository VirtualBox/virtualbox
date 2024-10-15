; $Id: VMMR0JmpA-x86.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VMM - R0 SetJmp / LongJmp routines for X86.
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
%include "VBox/asmdefs.mac"
%include "VMMInternal.mac"
%include "VBox/err.mac"
%include "VBox/param.mac"


;*******************************************************************************
;*  Defined Constants And Macros                                               *
;*******************************************************************************
%define RESUME_MAGIC    07eadf00dh
%define STACK_PADDING   0eeeeeeeeh



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
BEGINPROC vmmR0CallRing3SetJmp
GLOBALNAME vmmR0CallRing3SetJmp2
GLOBALNAME vmmR0CallRing3SetJmpEx
    ;
    ; Save the registers.
    ;
    mov     edx, [esp + 4h]             ; pJmpBuf
    mov     [xDX + VMMR0JMPBUF.ebx], ebx
    mov     [xDX + VMMR0JMPBUF.esi], esi
    mov     [xDX + VMMR0JMPBUF.edi], edi
    mov     [xDX + VMMR0JMPBUF.ebp], ebp
    mov     xAX, [esp]
    mov     [xDX + VMMR0JMPBUF.eip], xAX
    lea     ecx, [esp + 4]              ; (used in resume)
    mov     [xDX + VMMR0JMPBUF.esp], ecx
    pushf
    pop     xAX
    mov     [xDX + VMMR0JMPBUF.eflags], xAX

    ;
    ; If we're not in a ring-3 call, call pfn and return.
    ;
    test    byte [xDX + VMMR0JMPBUF.fInRing3Call], 1
    jnz     .resume

    mov     ebx, edx                    ; pJmpBuf -> ebx (persistent reg)
%ifdef VMM_R0_SWITCH_STACK
    mov     esi, [ebx + VMMR0JMPBUF.pvSavedStack]
    test    esi, esi
    jz      .entry_error
 %ifdef VBOX_STRICT
    cmp     dword [esi], 0h
    jne     .entry_error
    mov     edx, esi
    mov     edi, esi
    mov     ecx, VMM_STACK_SIZE / 4
    mov     eax, STACK_PADDING
    repne stosd
 %endif
    lea     esi, [esi + VMM_STACK_SIZE - 32]
    mov     [esi + 1ch], dword 0deadbeefh ; Marker 1.
    mov     [esi + 18h], ebx            ; Save pJmpBuf pointer.
    mov     [esi + 14h], dword 00c00ffeeh ; Marker 2.
    mov     [esi + 10h], dword 0f00dbeefh ; Marker 3.
    mov     edx, [esp + 10h]            ; pvArg2
    mov     ecx, [esp + 0ch]            ; pvArg1
    mov     eax, [esp + 08h]            ; pfn
 %if 1                                  ; Use this to eat of some extra stack - handy for finding paths using lots of stack.
  %define FRAME_OFFSET 0
 %else
  %define FRAME_OFFSET 1024
 %endif
    mov     [esi - FRAME_OFFSET + 04h], edx
    mov     [esi - FRAME_OFFSET      ], ecx
    lea     esp, [esi - FRAME_OFFSET]   ; Switch stack!
    call    eax
    and     dword [esi + 1ch], byte 0   ; reset marker.

 %ifdef VBOX_STRICT
    ; Calc stack usage and check for overflows.
    mov     edi, [ebx + VMMR0JMPBUF.pvSavedStack]
    cmp     dword [edi], STACK_PADDING  ; Check for obvious stack overflow.
    jne     .stack_overflow
    mov     esi, eax                    ; save eax
    mov     eax, STACK_PADDING
    mov     ecx, VMM_STACK_SIZE / 4
    cld
    repe scasd
    shl     ecx, 2                      ; *4
    cmp     ecx, VMM_STACK_SIZE - 64    ; Less than 64 bytes left -> overflow as well.
    mov     eax, esi                    ; restore eax in case of overflow (esi remains used)
    jae     .stack_overflow_almost

    ; Update stack usage statistics.
    cmp     ecx, [ebx + VMMR0JMPBUF.cbUsedMax] ; New max usage?
    jle     .no_used_max
    mov     [ebx + VMMR0JMPBUF.cbUsedMax], ecx
.no_used_max:
    ; To simplify the average stuff, just historize before we hit div errors.
    inc     dword [ebx + VMMR0JMPBUF.cUsedTotal]
    test    [ebx + VMMR0JMPBUF.cUsedTotal], dword 0c0000000h
    jz      .no_historize
    mov     dword [ebx + VMMR0JMPBUF.cUsedTotal], 2
    mov     edi, [ebx + VMMR0JMPBUF.cbUsedAvg]
    mov     [ebx + VMMR0JMPBUF.cbUsedTotal], edi
    mov     dword [ebx + VMMR0JMPBUF.cbUsedTotal + 4], 0
.no_historize:
    add     [ebx + VMMR0JMPBUF.cbUsedTotal], ecx
    adc     dword [ebx + VMMR0JMPBUF.cbUsedTotal + 4], 0
    mov     eax, [ebx + VMMR0JMPBUF.cbUsedTotal]
    mov     edx, [ebx + VMMR0JMPBUF.cbUsedTotal + 4]
    mov     edi, [ebx + VMMR0JMPBUF.cUsedTotal]
    div     edi
    mov     [ebx + VMMR0JMPBUF.cbUsedAvg], eax

    mov     eax, esi                    ; restore eax (final, esi released)

    mov     edi, [ebx + VMMR0JMPBUF.pvSavedStack]
    mov     dword [edi], 0h             ; Reset the overflow marker.
 %endif ; VBOX_STRICT

%else  ; !VMM_R0_SWITCH_STACK
    mov     ecx, [esp + 0ch]            ; pvArg1
    mov     edx, [esp + 10h]            ; pvArg2
    mov     eax, [esp + 08h]            ; pfn
    sub     esp, 12                     ; align the stack on a 16-byte boundary.
    mov     [esp      ], ecx
    mov     [esp + 04h], edx
    call    eax
%endif ; !VMM_R0_SWITCH_STACK
    mov     edx, ebx                    ; pJmpBuf -> edx (volatile reg)

    ;
    ; Return like in the long jump but clear eip, no short cuts here.
    ;
.proper_return:
    mov     ebx, [xDX + VMMR0JMPBUF.ebx]
    mov     esi, [xDX + VMMR0JMPBUF.esi]
    mov     edi, [xDX + VMMR0JMPBUF.edi]
    mov     ebp, [xDX + VMMR0JMPBUF.ebp]
    mov     xCX, [xDX + VMMR0JMPBUF.eip]
    and     dword [xDX + VMMR0JMPBUF.eip], byte 0 ; used for valid check.
    mov     esp, [xDX + VMMR0JMPBUF.esp]
    push    dword [xDX + VMMR0JMPBUF.eflags]
    popf
    jmp     xCX

.entry_error:
    mov     eax, VERR_VMM_SET_JMP_ERROR
    jmp     .proper_return

.stack_overflow:
    mov     eax, VERR_VMM_SET_JMP_STACK_OVERFLOW
    mov     edx, ebx
    jmp     .proper_return

.stack_overflow_almost:
    mov     eax, VERR_VMM_SET_JMP_STACK_OVERFLOW
    mov     edx, ebx
    jmp     .proper_return

    ;
    ; Aborting resume.
    ;
.bad:
    and     dword [xDX + VMMR0JMPBUF.eip], byte 0 ; used for valid check.
    mov     edi, [xDX + VMMR0JMPBUF.edi]
    mov     esi, [xDX + VMMR0JMPBUF.esi]
    mov     ebx, [xDX + VMMR0JMPBUF.ebx]
    mov     eax, VERR_VMM_SET_JMP_ABORTED_RESUME
    ret

    ;
    ; Resume VMMRZCallRing3 the call.
    ;
.resume:
    ; Sanity checks.
%ifdef VMM_R0_SWITCH_STACK
    mov     eax, [xDX + VMMR0JMPBUF.pvSavedStack]
 %ifdef RT_STRICT
    cmp     dword [eax], STACK_PADDING
 %endif
    lea     eax, [eax + VMM_STACK_SIZE - 32]
    cmp     dword [eax + 1ch], 0deadbeefh       ; Marker 1.
    jne     .bad
 %ifdef RT_STRICT
    cmp     [esi + 18h], edx                    ; The saved pJmpBuf pointer.
    jne     .bad
    cmp     dword [esi + 14h], 00c00ffeeh       ; Marker 2.
    jne     .bad
    cmp     dword [esi + 10h], 0f00dbeefh       ; Marker 3.
    jne     .bad
 %endif
%else  ; !VMM_R0_SWITCH_STACK
    cmp     ecx, [xDX + VMMR0JMPBUF.SpCheck]
    jne     .bad
.espCheck_ok:
    mov     ecx, [xDX + VMMR0JMPBUF.cbSavedStack]
    cmp     ecx, VMM_STACK_SIZE
    ja      .bad
    test    ecx, 3
    jnz     .bad
    mov     edi, [xDX + VMMR0JMPBUF.esp]
    sub     edi, [xDX + VMMR0JMPBUF.SpResume]
    cmp     ecx, edi
    jne     .bad
%endif

%ifdef VMM_R0_SWITCH_STACK
    ; Switch stack.
    mov     esp, [xDX + VMMR0JMPBUF.SpResume]
%else
    ; Restore the stack.
    mov     ecx, [xDX + VMMR0JMPBUF.cbSavedStack]
    shr     ecx, 2
    mov     esi, [xDX + VMMR0JMPBUF.pvSavedStack]
    mov     edi, [xDX + VMMR0JMPBUF.SpResume]
    mov     esp, edi
    rep movsd
%endif ; !VMM_R0_SWITCH_STACK
    mov     byte [xDX + VMMR0JMPBUF.fInRing3Call], 0

    ;
    ; Continue where we left off.
    ;
%ifdef VBOX_STRICT
    pop     eax                         ; magic
    cmp     eax, RESUME_MAGIC
    je      .magic_ok
    mov     ecx, 0123h
    mov     [ecx], edx
.magic_ok:
%endif
    popf
    pop     ebx
    pop     esi
    pop     edi
    pop     ebp
    xor     eax, eax                    ; VINF_SUCCESS
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
    push    ebp
    mov     ebp, esp
    push    edi
    push    esi
    push    ebx
    pushf
%ifdef VBOX_STRICT
    push    RESUME_MAGIC
%endif

    ;
    ; Load parameters.
    ;
    mov     edx, [ebp + 08h]            ; pJmpBuf
    mov     eax, [ebp + 0ch]            ; rc

    ;
    ; Is the jump buffer armed?
    ;
    cmp     dword [xDX + VMMR0JMPBUF.eip], byte 0
    je      .nok

    ;
    ; Sanity checks.
    ;
    mov     edi, [xDX + VMMR0JMPBUF.pvSavedStack]
    test    edi, edi                    ; darwin may set this to 0.
    jz      .nok
    mov     [xDX + VMMR0JMPBUF.SpResume], esp
%ifndef VMM_R0_SWITCH_STACK
    mov     esi, esp
    mov     ecx, [xDX + VMMR0JMPBUF.esp]
    sub     ecx, esi

    ; two sanity checks on the size.
    cmp     ecx, VMM_STACK_SIZE         ; check max size.
    jnbe    .nok

    ;
    ; Copy the stack.
    ;
    test    ecx, 3                      ; check alignment
    jnz     .nok
    mov     [xDX + VMMR0JMPBUF.cbSavedStack], ecx
    shr     ecx, 2
    rep movsd
%endif ; !VMM_R0_SWITCH_STACK

    ; Save a PC here to assist unwinding.
.unwind_point:
    mov     dword [xDX + VMMR0JMPBUF.SavedEipForUnwind], .unwind_point
    mov     ecx, [xDX + VMMR0JMPBUF.ebp]
    lea     ecx, [ecx + 4]
    mov     [xDX + VMMR0JMPBUF.UnwindRetPcLocation], ecx

    ; Save ESP & EBP to enable stack dumps
    mov     ecx, ebp
    mov     [xDX + VMMR0JMPBUF.SavedEbp], ecx
    sub     ecx, 4
    mov     [xDX + VMMR0JMPBUF.SavedEsp], ecx

    ; store the last pieces of info.
    mov     ecx, [xDX + VMMR0JMPBUF.esp]
    mov     [xDX + VMMR0JMPBUF.SpCheck], ecx
    mov     byte [xDX + VMMR0JMPBUF.fInRing3Call], 1

    ;
    ; Do the long jump.
    ;
    mov     ebx, [xDX + VMMR0JMPBUF.ebx]
    mov     esi, [xDX + VMMR0JMPBUF.esi]
    mov     edi, [xDX + VMMR0JMPBUF.edi]
    mov     ebp, [xDX + VMMR0JMPBUF.ebp]
    mov     ecx, [xDX + VMMR0JMPBUF.eip]
    mov     [xDX + VMMR0JMPBUF.UnwindRetPcValue], ecx
    mov     esp, [xDX + VMMR0JMPBUF.esp]
    push    dword [xDX + VMMR0JMPBUF.eflags]
    popf
    jmp     ecx

    ;
    ; Failure
    ;
.nok:
%ifdef VBOX_STRICT
    pop     eax                         ; magic
    cmp     eax, RESUME_MAGIC
    je      .magic_ok
    mov     ecx, 0123h
    mov     [ecx], edx
.magic_ok:
%endif
    popf
    pop     ebx
    pop     esi
    pop     edi
    mov     eax, VERR_VMM_LONG_JMP_ERROR
    leave
    ret
ENDPROC vmmR0CallRing3LongJmp

