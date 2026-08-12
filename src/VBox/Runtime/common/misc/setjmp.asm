; $Id: setjmp.asm 115004 2026-08-12 23:33:28Z knut.osmundsen@oracle.com $
;; @file
; IPRT - No-CRT setjmp & longjmp - AMD64 & X86.
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
; The contents of this file may alternatively be used under the terms
; of the Common Development and Distribution License Version 1.0
; (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
; in the VirtualBox distribution, in which case the provisions of the
; CDDL are applicable instead of those of the GPL.
;
; You may elect to license modified versions of this file under the
; terms and conditions of either the GPL or the CDDL or both.
;
; SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
;

%define RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"

;
; We probably need to call RtlUnwind on Windows.
;
%ifdef RT_OS_WINDOWS
 %ifdef RT_ARCH_AMD64
EXTERN_IMP2 RtlUnwind
  %ifdef IN_RING0
EXTERN_IMP2 IoWithinStackLimits
  %endif
 %else
EXTERN_IMP2 RtlUnwind@16
  %ifdef IN_RING0
EXTERN_IMP2 IoWithinStackLimits@8
  %endif
  %ifdef RT_OS_WINDOWS
   %ifdef IN_RING3
    %ifndef IPRT_WITHOUT_NLG_STUFF
     %define WITH_NLG_STUFF 1                ; Note! Must be in sync with except-x86-vcc-asm.asm!
    %endif
    %ifdef WITH_NLG_STUFF
extern      __NLG_Notify
    %endif
   %endif
  %endif
 %endif
%endif


BEGINCODE

;
; On Windows we must now follow the official layout, because we may need to
; pass the buffer onto ntdll!RtlUnwind to do the actual long-jumping when a
; shadow stack is active.  See _JUMP_BUFFER structure in CRT/wine headers.
;
struc RTJMPBUF
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
  %ifndef RT_OS_WINDOWS
   %error "Fix setjmp.h"
  %endif
        .uFrame         resq 1
 %endif
        .uRbx           resq 1
        .uSp            resq 1
        .uRbp           resq 1
 %ifdef ASM_CALL64_MSC
        .uRsi           resq 1
        .uRdi           resq 1
 %endif
        .uR12           resq 1
        .uR13           resq 1
        .uR14           resq 1
        .uR15           resq 1
        .uPc            resq 1
 %ifdef ASM_CALL64_MSC
        .uMxCsr         resd 1
        .uFcw           resw 1
        .uReserved      resw 1
        .uXmm6          resq 2
        .uXmm7          resq 2
        .uXmm8          resq 2
        .uXmm9          resq 2
        .uXmm10         resq 2
        .uXmm11         resq 2
        .uXmm12         resq 2
        .uXmm13         resq 2
        .uXmm14         resq 2
        .uXmm15         resq 2
 %endif
%else  ; RT_ARCH_X86
        .uEbp           resd 1          ; 0x00  [0]
        .uEbx           resd 1          ; 0x04  [1]
        .uEdi           resd 1          ; 0x08  [2]
        .uEsi           resd 1          ; 0x0c  [3]
        .uSp            resd 1          ; 0x10  [4]
        .uPc            resd 1          ; 0x14  [5]
 %ifdef RT_OS_WINDOWS
  %define RTJMPBUF_SP_SAVED_AS_IS
        .pXcptRegRec    resd 1          ; 0x18  [6]
        .uTryLevel      resd 1          ; 0x1c  [7]
        ; __setjmp3 stuff
        .uCookie        resd 1          ; 0x20  [8] - 'VC20'
  %define RTJMPBUF_X86_COOKIE       '02CV'
        .uUnwindFunc    resd 1          ; 0x24  [9]
        .auUnwindData   resd 6          ; 0x28  [10-15]
 %endif
%endif ; RT_ARCH_X86
endstruc ; RTJMPBUF

%ifdef RT_OS_WINDOWS

; size: 5 ULONG_PTR
struc EXCEPTION_RECORD_1P
        .ExceptionCode          resd 1
        .ExceptionFlags         resd 1
        .ExceptionRecord        RTCCPTR_RES 1
        .ExceptionAddress       RTCCPTR_RES 1
        .NumberParameters       RTCCPTR_RES 1   ; Technically DWORD, but implicit padding for 64-bit.
        .ExceptionInformation   RTCCPTR_RES 1   ; Only one parameter.
endstruc ; EXCEPTION_RECORD_1P

struc NT_TIB
        .ExceptionList          RTCCPTR_RES 1
        .StackBase              RTCCPTR_RES 1
        .StackLimit             RTCCPTR_RES 1
        .SubSystemTib           RTCCPTR_RES 1
        .FiberData              RTCCPTR_RES 1
        .ArbitraryUserPointer   RTCCPTR_RES 1
        .Self                   RTCCPTR_RES 1
endstruc ; NT_TIB

%endif


%ifdef RT_OS_WINDOWS
;;
; Legacy interface.  The compiler emits call to _setjmp, passing a frame
; pointer (typically as the 2nd argument).  Fake this.
RT_NOCRT_BEGINPROC setjmp       ; no-alias
        SEH64_END_PROLOGUE
 %ifdef RT_ARCH_AMD64
        xor     edx, edx                    ; frame pointer
        jmp     NAME(_setjmp)
 %else
        ; Add the parameter count (zero) and call the underscored+3 variant.
        mov     eax, [esp + 4h]
        push    dword 0                     ; stack alignment padding.
        push    dword 0
        push    eax
        call    NAME(_setjmp3)
        add     esp, 12
        ; Update eip and esp so we don't end up in here.
        mov     ecx, [esp + 4h]
        mov     eax, [esp]
        mov     [ecx + RTJMPBUF.uPc], eax
  %ifdef RTJMPBUF_SP_SAVED_AS_IS
        mov     [ecx + RTJMPBUF.uSp], esp
  %else
        lea     edx, [esp + 4h]
        mov     [ecx + RTJMPBUF.uSp], edx
  %endif
        ret
 %endif
ENDPROC RT_NOCRT(setjmp)
 %define setjmp             _setjmp
%endif

;;
; @param x86:[esp+4] msc:rcx gcc:rdi     The jump buffer pointer.
%ifdef RT_OS_WINDOWS
; @param msc:rdx                         AMD64: Frame pointer.
; @param x86:[esp+8]                     x86: Extra parameter count.
; @param x86:[esp+12...]                 x86: Extra parameters.
 %ifdef RT_ARCH_AMD64
BEGINPROC _setjmp
 %else
BEGINPROC _setjmp3
 %endif
%else
RT_NOCRT_BEGINPROC setjmp
%endif
        SEH64_END_PROLOGUE

%ifdef RT_ARCH_AMD64
        ;
        ; RT_ARCH_AMD64
        ;
 %ifndef ASM_CALL64_MSC
        mov     rcx, rdi
 %endif
 %ifdef ASM_CALL64_MSC
        mov     [rcx + RTJMPBUF.uFrame], rdx
 %endif
        mov     [rcx + RTJMPBUF.uRbx], rbx
 %ifdef RTJMPBUF_SP_SAVED_AS_IS
        mov     [rcx + RTJMPBUF.uSp ], rsp
 %else
        lea     rdx, [rsp + 8]
        mov     [rcx + RTJMPBUF.uSp ], rdx
 %endif
        mov     [rcx + RTJMPBUF.uRbp], rbp
 %ifdef ASM_CALL64_MSC
        mov     [rcx + RTJMPBUF.uRsi], rsi
        mov     [rcx + RTJMPBUF.uRdi], rdi
 %endif
        mov     [rcx + RTJMPBUF.uR12], r12
        mov     [rcx + RTJMPBUF.uR13], r13
        mov     [rcx + RTJMPBUF.uR14], r14
        mov     [rcx + RTJMPBUF.uR15], r15
        mov     rdx, [rsp]
        mov     [rcx + RTJMPBUF.uPc ], rdx
        xor     eax, eax
 %ifdef ASM_CALL64_MSC
        mov     [rcx + RTJMPBUF.uReserved], ax
        stmxcsr [rcx + RTJMPBUF.uMxCsr]
        fnstcw  [rcx + RTJMPBUF.uFcw]
        movdqa  [rcx + RTJMPBUF.uXmm6 ], xmm6
        movdqa  [rcx + RTJMPBUF.uXmm7 ], xmm7
        movdqa  [rcx + RTJMPBUF.uXmm8 ], xmm8
        movdqa  [rcx + RTJMPBUF.uXmm9 ], xmm9
        movdqa  [rcx + RTJMPBUF.uXmm10], xmm10
        movdqa  [rcx + RTJMPBUF.uXmm11], xmm11
        movdqa  [rcx + RTJMPBUF.uXmm12], xmm12
        movdqa  [rcx + RTJMPBUF.uXmm13], xmm13
        movdqa  [rcx + RTJMPBUF.uXmm14], xmm14
        movdqa  [rcx + RTJMPBUF.uXmm15], xmm15
 %endif
%else
        ;
        ; RT_ARCH_X86
        ;
        mov     ecx, [esp + 4h]             ; buffer pointer
        mov     [ecx + RTJMPBUF.uEbp], ebp
        mov     [ecx + RTJMPBUF.uEbx], ebx
        mov     [ecx + RTJMPBUF.uEdi], edi
        mov     [ecx + RTJMPBUF.uEsi], esi
 %ifdef RTJMPBUF_SP_SAVED_AS_IS
        mov     [ecx + RTJMPBUF.uSp ], esp
 %else
        lea     eax, [esp + 4h]
        mov     [ecx + RTJMPBUF.uSp ], eax
 %endif
        mov     edx, [esp]
        mov     [ecx + RTJMPBUF.uPc ], edx
        xor     eax, eax
 %ifdef RT_OS_WINDOWS
        mov     dword [ecx + RTJMPBUF.uCookie], RTJMPBUF_X86_COOKIE
        mov     [ecx + RTJMPBUF.uUnwindFunc], eax
        mov     [ecx + RTJMPBUF.auUnwindData + 4*0], eax
        mov     [ecx + RTJMPBUF.auUnwindData + 4*1], eax
        mov     [ecx + RTJMPBUF.auUnwindData + 4*2], eax
        mov     [ecx + RTJMPBUF.auUnwindData + 4*3], eax
        mov     [ecx + RTJMPBUF.auUnwindData + 4*4], eax
        mov     [ecx + RTJMPBUF.auUnwindData + 4*5], eax

        ; Save the exception registration record (= frame).
        mov     edx, [fs:NT_TIB.ExceptionList]
        mov     [ecx + RTJMPBUF.pXcptRegRec], edx

        ; If it is -1, then set uTryLevel to -1 and return.
        cmp     edx, 0ffffffffh
        jne     .have_xcpt_reg_rec
        mov     [ecx + RTJMPBUF.uTryLevel], edx ; -1
  %ifdef RT_STRICT
        cmp     dword [esp + 4 + 4], 0      ; Just a guess...
        je      .strict_zero_args
        int3
.strict_zero_args:
  %endif
        ret
        int3

.have_xcpt_reg_rec:
        ; Get the parameter count.
        mov     eax, [esp + 4 + 4]          ; load the extra parameter count.
        or      eax, eax
        jz      .set_try_level_from_xcpt_reg_rec ; Jump if no parameters.

        ; Load the 1st parameter - the unwind function.
        mov     edx, [esp + 4 + 8]
        mov     [ecx + RTJMPBUF.uUnwindFunc], edx
        dec     eax
        jz      .set_try_level_from_xcpt_reg_rec_reload_ptr_first

        ; Load the 2nd parameter - the try level.
        mov     edx, [esp + 4 + 12]
        mov     [ecx + RTJMPBUF.uTryLevel], edx
        dec     eax
        jnz     .copy_unwind_data
        ret
        int3

.set_try_level_from_xcpt_reg_rec_reload_ptr_first:
        mov     edx, [ecx + RTJMPBUF.pXcptRegRec]
.set_try_level_from_xcpt_reg_rec:
        mov     edx, [edx + 12]             ; Something following the EXCEPTION_REGISTRATION_RECORD...
        mov     [ecx + RTJMPBUF.uTryLevel], edx
        ret
        int3

        ; Copy unwind data.
.copy_unwind_data:
        push    edi
        push    esi
        lea     esi, [esp + 4 + 16 + 2*4]
        lea     edi, [ecx + RTJMPBUF.auUnwindData]
        mov     ecx, eax
        cmp     eax, 6
        jbe     .six_or_less
        mov     ecx, 6
.six_or_less:
        cld
        rep movsd
        pop     esi
        pop     edi
        xor     eax, eax
 %endif
%endif ; RT_ARCH_X86

        ret
%ifdef RT_OS_WINDOWS
 %ifdef RT_ARCH_AMD64
ENDPROC _setjmp
 %else
ENDPROC _setjmp3
 %endif
%else
ENDPROC RT_NOCRT(setjmp)
%endif


;;
; @param x86:[esp+4] msc:rcx gcc:rdi     The jump buffer pointer.
; @param x86:[esp+8] msc:rdx gcc:rsi     Return value.
RT_NOCRT_BEGINPROC longjmp
%ifndef RT_ARCH_AMD64
        push    ebp
        mov     ebp, esp
%endif

%ifdef RT_OS_WINDOWS
        ;
        ; Allocate exception record and stack for call.
        ;
 %ifdef RT_ARCH_AMD64
  %define MY_FRAME_SIZE         (EXCEPTION_RECORD_1P_size + 2 * 8 + 4 * 8) ; 2 extra slots for alignment, 4 slots for param split.
  %define ADDR_EXPR_XCPT_REC    rsp + (4 * 8)
 %else
  %define MY_FRAME_SIZE         (EXCEPTION_RECORD_1P_size)
  %define ADDR_EXPR_XCPT_REC    ebp - EXCEPTION_RECORD_1P_size
 %endif
        sub     xSP, MY_FRAME_SIZE
        SEH64_ALLOCATE_STACK MY_FRAME_SIZE
%endif
        SEH64_END_PROLOGUE

        ;
        ; Put the return value into eax and make sure it isn't zero.
        ; Also load the jump-buffer pointer into xCX.
        ;
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        mov     eax, edx                        ; ret
 %else
        mov     eax, esi                        ; ret
        mov     rcx, rdi                        ; jmp_buf
 %endif
%else
        mov     eax, [ebp + 4 + 8]              ; ret
        mov     ecx, [ebp + 4 + 4]              ; jmp_buf
%endif
        test    eax, eax
        jnz     .ret_value_not_zero
        inc     eax
%ifdef RT_ARCH_X86
        mov     [ebp + 4 + 8], eax              ; save correct ret value so we can discard eax.
%endif
.ret_value_not_zero:


%ifdef RT_OS_WINDOWS
 %ifdef RT_ARCH_X86
        ;
        ; Check the cookie.  We only implement _setjmp3, so the jump buffer
        ; must be marked 'VC20'.
        ;
        cmp     dword [ecx + RTJMPBUF.uCookie], RTJMPBUF_X86_COOKIE
        je      .cookie_ok
        int3                                    ; better way to fail?
.cookie_ok:
 %endif

        ;
        ; Check that SP is sane.
        ;
        mov     xDX, [xCX + RTJMPBUF.uSp]

 %ifdef IN_RING3
        ; In ring-3 we just access the NT_TIB structure.
  %ifdef RT_ARCH_AMD64
        cmp     xDX, [gs:NT_TIB.StackBase]
  %else
        cmp     xDX, [fs:NT_TIB.StackBase]
  %endif
        jbe     .stack_ptr_ok1
        int3                                    ; better way to fail?
.stack_ptr_ok1:
  %ifdef RT_ARCH_AMD64
        cmp     xDX, [gs:NT_TIB.StackLimit]
  %else
        cmp     xDX, [fs:NT_TIB.StackLimit]
  %endif
        ja      .stack_ptr_ok2
        int3                                    ; better way to fail?
.stack_ptr_ok2:
 %elifdef IN_RING0
        ; In ring-0 NT_TIB can't be used and we should call IoWithinStackLimits
        ; (or KeQueryCurrentStackInformationEx) to do the sanity checks.
  %ifdef RT_ARCH_AMD64
        mov     [rsp + MY_FRAME_SIZE + 16], eax ; Save return value in shadow param 1 slot.
        mov     [rsp + MY_FRAME_SIZE + 8], rcx  ; Save jmp_buf pointer in shadow param 0 slot.
        mov     rcx, rdx                        ; param 0: RegionStart
        mov     edx, 8                          ; param 1: RegionSize
        call    IMP2(IoWithinStackLimits)
        test    al, al
        jnz     .r0_amd64_stack_ptr_ok
        int3                                    ; better way to fail?
.r0_amd64_stack_ptr_ok:
        mov     eax, [rsp + MY_FRAME_SIZE + 16] ; Restore return value.
        mov     rcx, [rsp + MY_FRAME_SIZE + 8]  ; Restore jmp_buf pointer.
  %else
        push    4                               ; param 1: RegionSize
        push    xDX                             ; param 0: RegionStart
        call    IMP2(IoWithinStackLimits@8)
        test    al, al
        jnz     .r0_x86_stack_ptr_ok
        int3                                    ; better way to fail?
.r0_x86_stack_ptr_ok:
        mov     ecx, [ebp + 8]                  ; Restore the jump buffer pointer from the parameter list.
        mov     eax, [ebp + 12]                 ; Reload the return value.
  %endif

 %endif

        ;
        ; We will probably need to use RtlUnwind here to do the unwinding.
        ;
        ; On AMD64 this will do the actual long jump.  Here we must call it
        ; whenever there is a shadow stack or when there is a frame.
        ;
        ; On X86 it will just do unwinding and we restore the registers.  So,
        ; we don't need to call it unless the exception registration record
        ; differs (if it's the same, it is probably set to NIL).  (We may
        ; have to call uUnwindFunc even if we can skip RtlUnwind.)
        ;
 %ifdef RT_ARCH_AMD64
        xor     edx, edx
        cmp     qword [rcx + RTJMPBUF.uFrame], byte 0
        jnz     .nt_restore

        db 0xfe, 0x48, 0x0f, 0x1e, 0xca         ; rdsspq rdx - a NOP unless CET is supported & enabled.
        test    rdx, rdx
        jz      .regular_restore
  %ifdef RT_STRICT
        int3                                    ; Expecting the compiler to give us a frame pointer in CET mode!
  %endif
.nt_restore:
 %else  ; RT_ARCH_X86
        mov     edx, [ecx + RTJMPBUF.pXcptRegRec]
        cmp     [fs:NT_TIB.ExceptionList], edx
        je      .maybe_do_unwind_callback       ; No NT unwinding required if the exception registration record is the same.
.nt_x86_need_unwind:
 %endif ; RT_ARCH_X86

        ;
        ; Initialize execption record.
        ;
.nt_init_xcpt_rec:
        mov     dword [ADDR_EXPR_XCPT_REC + EXCEPTION_RECORD_1P.ExceptionCode], 0x80000026 ; STATUS_LONGJUMP
        mov     dword [ADDR_EXPR_XCPT_REC + EXCEPTION_RECORD_1P.ExceptionFlags], 0
        mov     RTCCPTR_PRE [ADDR_EXPR_XCPT_REC + EXCEPTION_RECORD_1P.ExceptionRecord], 0
        mov     RTCCPTR_PRE [ADDR_EXPR_XCPT_REC + EXCEPTION_RECORD_1P.ExceptionAddress], 0
 %ifdef RT_ARCH_AMD64
        mov     RTCCPTR_PRE [ADDR_EXPR_XCPT_REC + EXCEPTION_RECORD_1P.NumberParameters], 1
        mov     RTCCPTR_PRE [ADDR_EXPR_XCPT_REC + EXCEPTION_RECORD_1P.ExceptionInformation], rcx
 %else
        mov     RTCCPTR_PRE [ADDR_EXPR_XCPT_REC + EXCEPTION_RECORD_1P.NumberParameters], 0
        mov     RTCCPTR_PRE [ADDR_EXPR_XCPT_REC + EXCEPTION_RECORD_1P.ExceptionInformation], 0
 %endif

        ;
        ; Call RtlUnwind (amd64 doesn't return, the x86 definitely does in W11
        ; build 26100).
        ;
 %ifdef RT_ARCH_AMD64
        mov     r9d, eax                        ; return value.
        lea     r8, [ADDR_EXPR_XCPT_REC]        ; Exception record.
        mov     rdx, [rcx + RTJMPBUF.uPc]       ; Target PC.
        mov     rcx, [rcx + RTJMPBUF.uFrame]    ; Target frame.
        call    IMP2(RtlUnwind)
  %ifdef RT_STRICT
        int3
  %endif
        jmp     .nt_init_xcpt_rec
        int3
 %else  ; RT_ARCH_X86
        push    0                               ; zero ('return value')
        lea     eax, [ADDR_EXPR_XCPT_REC]
        push    eax                             ; Exception record.
        push    .nt_unwind_x86_target           ; Target PC - !ignored!
        push    edx                             ; Target frame (RTJMPBUF.pXcptRegRec, loaded above).
        call    IMP2(RtlUnwind@16)
.nt_unwind_x86_target:
        ; Restore the pointer. Note! eax, ebx, ecx, edx, esi & edi are set to zero by RtlUnwind!
  %ifdef RT_STRICT
        lea     eax, [esp + MY_FRAME_SIZE]
        cmp     eax, ebp
        je      .ebp_not_changed
        int3
.ebp_not_changed:
  %endif
        mov     ecx, [esp + MY_FRAME_SIZE + 8]  ; Restore the jump buffer pointer from the parameter list.

        ;
        ; X86: Is there a unwind callback function we need to call?
        ;
.maybe_do_unwind_callback:
        mov     edx, [ecx + RTJMPBUF.uUnwindFunc]
        or      edx, edx
        jz      .regular_restore_with_eax_load

  %ifdef RT_STRICT
        cmp     dword [ecx + RTJMPBUF.pXcptRegRec], 0
        je      .strict_reg_rec_check_failed
        cmp     dword [ecx + RTJMPBUF.pXcptRegRec], -1
        je      .strict_reg_rec_check_failed
        jmp     .strict_reg_rec_check_done
.strict_reg_rec_check_failed:
        int3
.strict_reg_rec_check_done:
  %endif

        ; Call uUnwindFunc(jmp_buf).
        mov     ebx, esp                        ; Save ESP in EBX to guard against calling stdcall/cdecl fun
        mov     [esp], ecx
        call    edx
        mov     esp, ebx                        ; Restore ESP to pre-call state.
        mov     ecx, [esp + MY_FRAME_SIZE + 8]  ; Restore the jump buffer pointer from the parameter list.
.regular_restore_with_eax_load:
        mov     eax, [esp + MY_FRAME_SIZE + 12] ; Reload the return value.
 %endif ; RT_ARCH_X86
%endif ; RT_OS_WINDOWS

        ;
        ; Restore the state (the regular way).
        ;
.regular_restore:
%ifdef RT_ARCH_AMD64
        mov     rbp,   [rcx + RTJMPBUF.uRbp]
        mov     r15,   [rcx + RTJMPBUF.uR15]
        mov     r14,   [rcx + RTJMPBUF.uR14]
        mov     r13,   [rcx + RTJMPBUF.uR13]
        mov     r12,   [rcx + RTJMPBUF.uR12]
        mov     rbx,   [rcx + RTJMPBUF.uRbx]
 %ifdef ASM_CALL64_MSC
        mov     rsi,   [rcx + RTJMPBUF.uRsi]
        mov     rdi,   [rcx + RTJMPBUF.uRdi]
        movdqa  xmm6,  [rcx + RTJMPBUF.uXmm6]
        movdqa  xmm7,  [rcx + RTJMPBUF.uXmm7]
        movdqa  xmm8,  [rcx + RTJMPBUF.uXmm8]
        movdqa  xmm9,  [rcx + RTJMPBUF.uXmm9]
        movdqa  xmm10, [rcx + RTJMPBUF.uXmm10]
        movdqa  xmm11, [rcx + RTJMPBUF.uXmm11]
        movdqa  xmm12, [rcx + RTJMPBUF.uXmm12]
        movdqa  xmm13, [rcx + RTJMPBUF.uXmm13]
        movdqa  xmm14, [rcx + RTJMPBUF.uXmm14]
        movdqa  xmm15, [rcx + RTJMPBUF.uXmm15]

        ldmxcsr [rcx + RTJMPBUF.uMxCsr]
        fldcw   [rcx + RTJMPBUF.uFcw]
 %endif
%else  ; RT_ARCH_X86
 %ifdef RT_OS_WINDOWS
  %ifdef WITH_NLG_STUFF
        ; Debugger notifications.
        mov     ebx, eax                    ; save it
        mov     edx, 0                      ; code
        mov     eax, [ecx + RTJMPBUF.uPc]
        call    __NLG_Notify
        mov     eax, ebx                    ; restore
  %endif
 %endif
        ; Do the actual restoring.
        mov     esi, [ecx + RTJMPBUF.uEsi]
        mov     edi, [ecx + RTJMPBUF.uEdi]
        mov     ebx, [ecx + RTJMPBUF.uEbx]
        mov     ebp, [ecx + RTJMPBUF.uEbp]
%endif ; RT_ARCH_X86

        ; Finally restore the stack pointer and jump to the target IP.
        mov     xSP, [xCX + RTJMPBUF.uSp]
%ifdef RTJMPBUF_SP_SAVED_AS_IS
        add     xSP, xCB
%endif
        jmp     RTCCPTR_PRE [xCX + RTJMPBUF.uPc]
ENDPROC RT_NOCRT(longjmp)


%ifdef RT_OS_WINDOWS
 %ifndef RT_WITHOUT_NOCRT_WRAPPERS
  %ifndef RT_WITH_NOCRT_ALIASES
;;
; Windows alia for longjmp.
;
; The compiler only recognizes this symbol as longjmp and may not think it is
; necessary to call _setjmp3 with the appropriate C++ unwinding arguments if
; there isn't a longjmp nearby (tstNoCrt-1/Jmp4/ASAN/x86).
;
; See iprt/nocrt/setjmp.h or more.
;
BEGINPROC longjmp
        SEH64_END_PROLOGUE
   %ifndef IPRT_BUILD_TYPE_ASAN
        jmp     NAME(RT_NOCRT(longjmp))
   %else
        ; Must forward the request to the asan wrapper or we'll end up with weird
        ; false positive stack trashings, as asan doesn't unwind the stack protections.
        ; This means we're not testing our longjmp code in asan builds, but at least
        ; they don't fail.
        extern  NAME(__asan_wrap_longjmp)
        jmp     NAME(__asan_wrap_longjmp)
   %endif
ENDPROC   longjmp
  %endif ; !RT_WITH_NOCRT_ALIASES
 %endif ; !RT_WITHOUT_NOCRT_WRAPPERS
%endif ; RT_OS_WINDOWS

MARK_OBJECT_RETPOLINE_SAFE ;; @todo retpoline: there are indirect calls here!
