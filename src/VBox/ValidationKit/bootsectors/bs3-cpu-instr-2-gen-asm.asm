; $Id: bs3-cpu-instr-2-gen-asm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - bs3-cpu-instr-2-gen - assembly helpers for test data generator.
;

;
; Copyright (C) 2024 Oracle and/or its affiliates.
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


;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"

BEGINCODE

%ifdef ASM_CALL64_MSC
 %define EFLAGS_PARAM_REG   r8d
%else
 %define EFLAGS_PARAM_REG   edx
%endif


;;
; @param 1 instruction
; @param 2 Whether it takes carry in.
; @param 3 Whether it has an 8-bit form.
%macro DO_BINARY 3

 %if %3 != 0
BEGINPROC   GenU8_ %+ %1
  %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
  %endif
  %if %2 != 0
        lahf
        and     ah, 0xfe
        shl     r8d, 8
        or      eax, r8d
        sahf
  %endif
        %1      cl, dl
        mov     [r9], cl
        pushf
        pop     rax
        ret
ENDPROC     GenU8_ %+ %1
 %endif

BEGINPROC   GenU16_ %+ %1
 %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
 %endif
 %if %2 != 0
        lahf
        and     ah, 0xfe
        shl     r8d, 8
        or      eax, r8d
        sahf
 %endif
        %1      cx, dx
        mov     [r9], cx
        pushf
        pop     rax
        ret
ENDPROC     GenU16_ %+ %1

BEGINPROC   GenU32_ %+ %1
 %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
 %endif
 %if %2 != 0
        lahf
        and     ah, 0xfe
        shl     r8d, 8
        or      eax, r8d
        sahf
 %endif
        %1      ecx, edx
        mov     [r9], ecx
        pushf
        pop     rax
        ret
ENDPROC     GenU32_ %+ %1

BEGINPROC   GenU64_ %+ %1
 %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
 %endif
 %if %2 != 0
        lahf
        and     ah, 0xfe
        shl     r8d, 8
        or      eax, r8d
        sahf
 %endif
        %1      rcx, rdx
        mov     [r9], rcx
        pushf
        pop     rax
        ret
ENDPROC     GenU64_ %+ %1

%endmacro

DO_BINARY and,  0, 1
DO_BINARY or,   0, 1
DO_BINARY xor,  0, 1
DO_BINARY test, 0, 1

DO_BINARY add,  0, 1
DO_BINARY adc,  1, 1
DO_BINARY sub,  0, 1
DO_BINARY sbb,  1, 1
DO_BINARY cmp,  0, 1

DO_BINARY bt,   0, 0
DO_BINARY btc,  0, 0
DO_BINARY btr,  0, 0
DO_BINARY bts,  0, 0


;;
; @param 1 instruction
; @param 2 Whether it takes carry in.
%macro DO_SHIFT 2

BEGINPROC   GenU8_ %+ %1
 %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
 %endif
        pushf
        and     dword [rsp], ~X86_EFL_STATUS_BITS
        or      dword [rsp], r8d
        popf
        xchg    rcx, rdx
        %1      dl, cl
        mov     [r9], dl
        pushf
        pop     rax
        ret
ENDPROC     GenU8_ %+ %1

BEGINPROC   GenU16_ %+ %1
 %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
 %endif
        pushf
        and     dword [rsp], ~X86_EFL_STATUS_BITS
        or      dword [rsp], r8d
        popf
        xchg    cx, dx
        %1      dx, cl
        mov     [r9], dx
        pushf
        pop     rax
        ret
ENDPROC     GenU16_ %+ %1

BEGINPROC   GenU32_ %+ %1
 %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
 %endif
        pushf
        and     dword [rsp], ~X86_EFL_STATUS_BITS
        or      dword [rsp], r8d
        popf
        xchg    rcx, rdx
        %1      edx, cl
        mov     [r9], edx
        pushf
        pop     rax
        ret
ENDPROC     GenU32_ %+ %1

BEGINPROC   GenU64_ %+ %1
 %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
 %endif
        pushf
        and     dword [rsp], ~X86_EFL_STATUS_BITS
        or      dword [rsp], r8d
        popf
        xchg    rcx, rdx
        %1      rdx, cl
        mov     [r9], rdx
        pushf
        pop     rax
        ret
ENDPROC     GenU64_ %+ %1


BEGINPROC   GenU8_ %+ %1 %+ _Ib
 %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
 %endif
        pushf
        and     dword [rsp], ~X86_EFL_STATUS_BITS
        or      dword [rsp], r8d
        popf

        movzx   r11d, dl
        mov     al, cl
        mov     rdx, r9

        lea     r10, [.first_imm wrt rip]
        lea     r10, [r10 + r11 * 8]        ;; @todo assert that the entry size is 8 bytes
        jmp     r10
.return:
        mov     [rdx], al
        pushf
        pop     rax
        ret

        ALIGNCODE(8)
.first_imm:
 %assign i 0
 %rep 256
        %1      al, i
        jmp     near .return
 %if i == 1
        db      0cch
 %endif
  %assign i i+1
 %endrep
ENDPROC     GenU8_ %+ %1 %+ _Ib

BEGINPROC   GenU16_ %+ %1 %+ _Ib
 %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
 %endif
        pushf
        and     dword [rsp], ~X86_EFL_STATUS_BITS
        or      dword [rsp], r8d
        popf

        movzx   r11d, dl
        mov     ax, cx
        mov     rdx, r9

        lea     r10, [.first_imm wrt rip]
        lea     r10, [r10 + r11]            ;; @todo assert that the entry size is 9 bytes
        lea     r10, [r10 + r11 * 8]
        jmp     r10
.return:
        mov     [rdx], ax
        pushf
        pop     rax
        ret

        ALIGNCODE(8)
.first_imm:
 %assign i 0
 %rep 256
        %1      ax, i
        jmp     near .return
 %if i == 1
        db      0cch
 %endif
  %assign i i+1
 %endrep
ENDPROC     GenU16_ %+ %1 %+ _Ib

BEGINPROC   GenU32_ %+ %1 %+ _Ib
 %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
 %endif
        pushf
        and     dword [rsp], ~X86_EFL_STATUS_BITS
        or      dword [rsp], r8d
        popf

        movzx   r11d, dl
        mov     eax, ecx
        mov     rdx, r9

        lea     r10, [.first_imm wrt rip]
        lea     r10, [r10 + r11 * 8]        ;; @todo assert that the entry size is 8 bytes
        jmp     r10
.return:
        mov     [rdx], eax
        pushf
        pop     rax
        ret

        ALIGNCODE(8)
.first_imm:
 %assign i 0
 %rep 256
        %1      eax, i
        jmp     near .return
 %if i == 1
        db      0cch
 %endif
  %assign i i+1
 %endrep
ENDPROC     GenU32_ %+ %1 %+ _Ib

BEGINPROC   GenU64_ %+ %1 %+ _Ib
 %ifdef ASM_CALL64_GCC
        mov     r9, rcx
        mov     r8, rdx
        mov     rdx, rsi
        mov     rcx, rdi
 %endif
        pushf
        and     dword [rsp], ~X86_EFL_STATUS_BITS
        or      dword [rsp], r8d
        popf

        movzx   r11d, dl
        mov     rax, rcx
        mov     rdx, r9

        lea     r10, [.first_imm wrt rip]
        lea     r10, [r10 + r11]            ;; @todo assert that the entry size is 9 bytes
        lea     r10, [r10 + r11 * 8]
        jmp     r10
.return:
        mov     [rdx], rax
        pushf
        pop     rax
        ret

        ALIGNCODE(8)
.first_imm:
 %assign i 0
 %rep 256
        %1      rax, i
        jmp     near .return
 %if i == 1
        db      0cch
 %endif
  %assign i i+1
 %endrep
ENDPROC     GenU64_ %+ %1 %+ _Ib


%endmacro

DO_SHIFT shl,  0
DO_SHIFT shr,  0
DO_SHIFT sar,  0
DO_SHIFT rol,  0
DO_SHIFT ror,  0
DO_SHIFT rcl,  1
DO_SHIFT rcr,  1

