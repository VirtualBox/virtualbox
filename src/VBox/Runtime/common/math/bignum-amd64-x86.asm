; $Id: bignum-amd64-x86.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - Big Integer Numbers, AMD64 and X86 Assembly Workers
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
%define RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"
%include "internal/bignum.mac"


;*********************************************************************************************************************************
;*  Defined Constants And Macros                                                                                                 *
;*********************************************************************************************************************************
%ifdef RT_ARCH_AMD64
 %macro sahf 0
  %error "SAHF not supported on ancient AMD64"
 %endmacro
 %macro lahf 0
  %error "LAHF not supported on ancient AMD64"
 %endmacro
%endif


BEGINCODE

;;
; Subtracts a number (pauSubtrahend) from a larger number (pauMinuend) and
; stores the result in pauResult.
;
; All three numbers are zero padded such that a borrow can be carried one (or
; two for 64-bit) elements beyond the end of the largest number.
;
; @returns nothing.
; @param    pauResult       x86:[ebp +  8]  gcc:rdi  msc:rcx
; @param    pauMinuend      x86:[ebp + 12]  gcc:rsi  msc:rdx
; @param    pauSubtrahend   x86:[ebp + 16]  gcc:rdx  msc:r8
; @param    cUsed           x86:[ebp + 20]  gcc:rcx  msc:r9
;
BEGINPROC rtBigNumMagnitudeSubAssemblyWorker
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_GCC
  %define pauResult         rdi
  %define pauMinuend        rsi
  %define pauSubtrahend     rdx
  %define cUsed             ecx
 %else
  %define pauResult         rcx
  %define pauMinuend        rdx
  %define pauSubtrahend     r8
  %define cUsed             r9d
 %endif
        xor     r11d, r11d              ; index register.

 %if RTBIGNUM_ELEMENT_SIZE == 4
        add     cUsed, 1                ; cUsed = RT_ALIGN(cUsed, 2) / 2
        shr     cUsed, 1
 %endif
        cmp     cUsed, 8                ; Skip the big loop if small number.
        jb      .small_job

        mov     r10d, cUsed
        shr     r10d, 3
        clc
.big_loop:
        mov     rax, [pauMinuend + r11]
        sbb     rax, [pauSubtrahend + r11]
        mov     [pauResult + r11], rax
        mov     rax, [pauMinuend    + r11 +  8]
        sbb     rax, [pauSubtrahend + r11 +  8]
        mov     [pauResult + r11 +  8], rax
        mov     rax, [pauMinuend    + r11 + 16]
        sbb     rax, [pauSubtrahend + r11 + 16]
        mov     [pauResult + r11 + 16], rax
        mov     rax, [pauMinuend    + r11 + 24]
        sbb     rax, [pauSubtrahend + r11 + 24]
        mov     [pauResult + r11 + 24], rax
        mov     rax, [pauMinuend    + r11 + 32]
        sbb     rax, [pauSubtrahend + r11 + 32]
        mov     [pauResult + r11 + 32], rax
        mov     rax, [pauMinuend    + r11 + 40]
        sbb     rax, [pauSubtrahend + r11 + 40]
        mov     [pauResult + r11 + 40], rax
        mov     rax, [pauMinuend    + r11 + 48]
        sbb     rax, [pauSubtrahend + r11 + 48]
        mov     [pauResult + r11 + 48], rax
        mov     rax, [pauMinuend    + r11 + 56]
        sbb     rax, [pauSubtrahend + r11 + 56]
        mov     [pauResult + r11 + 56], rax
        lea     r11, [r11 + 64]
        dec     r10d                    ; Does not change CF.
        jnz     .big_loop

 %if 0 ; Ancient AMD CPUs does have lahf/sahf, thus the mess in the %else.
        lahf                            ; Save CF
        and     cUsed, 7                ; Up to seven odd rounds.
        jz      .done
        sahf                            ; Restore CF.
        jmp     .small_loop             ; Skip CF=1 (clc).
 %else
        jnc     .no_carry
        and     cUsed, 7                ; Up to seven odd rounds.
        jz      .done
        stc
        jmp     .small_loop             ; Skip CF=1 (clc).
.no_carry:
        and     cUsed, 7                ; Up to seven odd rounds.
        jz      .done
 %endif
.small_job:
        clc
.small_loop:
        mov     rax, [pauMinuend + r11]
        sbb     rax, [pauSubtrahend + r11]
        mov     [pauResult + r11], rax
        lea     r11, [r11 + 8]
        dec     cUsed                   ; does not change CF.
        jnz     .small_loop
 %ifdef RT_STRICT
        jnc     .done
        int3
 %endif
.done:

%elifdef RT_ARCH_X86
        push    edi
        push    esi
        push    ebx

        mov     edi, [ebp + 08h]        ; pauResult
 %define pauResult      edi
        mov     ecx, [ebp + 0ch]        ; pauMinuend
 %define pauMinuend     ecx
        mov     edx, [ebp + 10h]        ; pauSubtrahend
 %define pauSubtrahend  edx
        mov     esi, [ebp + 14h]        ; cUsed
 %define cUsed          esi

        xor     ebx, ebx                ; index register.

        cmp     cUsed, 8                ; Skip the big loop if small number.
        jb      .small_job

        shr     cUsed, 3
        clc
.big_loop:
        mov     eax, [pauMinuend + ebx]
        sbb     eax, [pauSubtrahend + ebx]
        mov     [pauResult + ebx], eax
        mov     eax, [pauMinuend    + ebx +  4]
        sbb     eax, [pauSubtrahend + ebx +  4]
        mov     [pauResult + ebx +  4], eax
        mov     eax, [pauMinuend    + ebx +  8]
        sbb     eax, [pauSubtrahend + ebx +  8]
        mov     [pauResult + ebx +  8], eax
        mov     eax, [pauMinuend    + ebx + 12]
        sbb     eax, [pauSubtrahend + ebx + 12]
        mov     [pauResult + ebx + 12], eax
        mov     eax, [pauMinuend    + ebx + 16]
        sbb     eax, [pauSubtrahend + ebx + 16]
        mov     [pauResult + ebx + 16], eax
        mov     eax, [pauMinuend    + ebx + 20]
        sbb     eax, [pauSubtrahend + ebx + 20]
        mov     [pauResult + ebx + 20], eax
        mov     eax, [pauMinuend    + ebx + 24]
        sbb     eax, [pauSubtrahend + ebx + 24]
        mov     [pauResult + ebx + 24], eax
        mov     eax, [pauMinuend    + ebx + 28]
        sbb     eax, [pauSubtrahend + ebx + 28]
        mov     [pauResult + ebx + 28], eax
        lea     ebx, [ebx + 32]
        dec     cUsed                   ; Does not change CF.
        jnz     .big_loop

        lahf                            ; Save CF
        mov     cUsed, [ebp + 14h]      ; Up to three final rounds.
        and     cUsed, 7
        jz      .done
        sahf                            ; Restore CF.
        jmp     .small_loop             ; Skip CF=1 (clc).

.small_job:
        clc
.small_loop:
        mov     eax, [pauMinuend + ebx]
        sbb     eax, [pauSubtrahend + ebx]
        mov     [pauResult + ebx], eax
        lea     ebx, [ebx + 4]
        dec     cUsed                   ; Does not change CF
        jnz     .small_loop
 %ifdef RT_STRICT
        jnc     .done
        int3
 %endif
.done:

        pop     ebx
        pop     esi
        pop     edi
%else
 %error "Unsupported arch"
%endif

        leave
        ret
%undef pauResult
%undef pauMinuend
%undef pauSubtrahend
%undef cUsed
ENDPROC rtBigNumMagnitudeSubAssemblyWorker



;;
; Subtracts a number (pauSubtrahend) from a larger number (pauMinuend) and
; stores the result in pauResult.
;
; All three numbers are zero padded such that a borrow can be carried one (or
; two for 64-bit) elements beyond the end of the largest number.
;
; @returns nothing.
; @param    pauResultMinuend    x86:[ebp +  8]  gcc:rdi  msc:rcx
; @param    pauSubtrahend       x86:[ebp + 12]  gcc:rsi  msc:rdx
; @param    cUsed               x86:[ebp + 16]  gcc:rdx  msc:r8
;
BEGINPROC rtBigNumMagnitudeSubThisAssemblyWorker
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_GCC
  %define   pauResultMinuend    rdi
  %define   pauSubtrahend       rsi
  %define   cUsed               edx
 %else
  %define   pauResultMinuend    rcx
  %define   pauSubtrahend       rdx
  %define   cUsed               r8d
 %endif
        xor     r11d, r11d              ; index register.

 %if RTBIGNUM_ELEMENT_SIZE == 4
        add     cUsed, 1                ; cUsed = RT_ALIGN(cUsed, 2) / 2
        shr     cUsed, 1
 %endif
        cmp     cUsed, 8                ; Skip the big loop if small number.
        jb      .small_job

        mov     r10d, cUsed
        shr     r10d, 3
        clc
.big_loop:
        mov     rax, [pauSubtrahend + r11]
        sbb     [pauResultMinuend + r11], rax
        mov     rax, [pauSubtrahend + r11 +  8]
        sbb     [pauResultMinuend + r11 +  8], rax
        mov     rax, [pauSubtrahend + r11 + 16]
        sbb     [pauResultMinuend + r11 + 16], rax
        mov     rax, [pauSubtrahend + r11 + 24]
        sbb     [pauResultMinuend + r11 + 24], rax
        mov     rax, [pauSubtrahend + r11 + 32]
        sbb     [pauResultMinuend + r11 + 32], rax
        mov     rax, [pauSubtrahend + r11 + 40]
        sbb     [pauResultMinuend + r11 + 40], rax
        mov     rax, [pauSubtrahend + r11 + 48]
        sbb     [pauResultMinuend + r11 + 48], rax
        mov     rax, [pauSubtrahend + r11 + 56]
        sbb     [pauResultMinuend + r11 + 56], rax
        lea     r11, [r11 + 64]
        dec     r10d                    ; Does not change CF.
        jnz     .big_loop

 %if 0 ; Ancient AMD CPUs does have lahf/sahf, thus the mess in the %else.
        lahf                            ; Save CF
        and     cUsed, 7                ; Up to seven odd rounds.
        jz      .done
        sahf                            ; Restore CF.
        jmp     .small_loop             ; Skip CF=1 (clc).
 %else
        jnc     .no_carry
        and     cUsed, 7                ; Up to seven odd rounds.
        jz      .done
        stc
        jmp     .small_loop             ; Skip CF=1 (clc).
.no_carry:
        and     cUsed, 7                ; Up to seven odd rounds.
        jz      .done
 %endif
.small_job:
        clc
.small_loop:
        mov     rax, [pauSubtrahend + r11]
        sbb     [pauResultMinuend + r11], rax
        lea     r11, [r11 + 8]
        dec     cUsed                   ; does not change CF.
        jnz     .small_loop
 %ifdef RT_STRICT
        jnc     .done
        int3
 %endif
.done:

%elifdef RT_ARCH_X86
        push    edi
        push    ebx

        mov     edi, [ebp + 08h]        ; pauResultMinuend
 %define pauResultMinuend   edi
        mov     edx, [ebp + 0ch]        ; pauSubtrahend
 %define pauSubtrahend      edx
        mov     ecx, [ebp + 10h]        ; cUsed
 %define cUsed              ecx

        xor     ebx, ebx                ; index register.

        cmp     cUsed, 8                ; Skip the big loop if small number.
        jb      .small_job

        shr     cUsed, 3
        clc
.big_loop:
        mov     eax, [pauSubtrahend + ebx]
        sbb     [pauResultMinuend + ebx], eax
        mov     eax, [pauSubtrahend + ebx + 4]
        sbb     [pauResultMinuend + ebx + 4], eax
        mov     eax, [pauSubtrahend + ebx + 8]
        sbb     [pauResultMinuend + ebx + 8], eax
        mov     eax, [pauSubtrahend + ebx + 12]
        sbb     [pauResultMinuend + ebx + 12], eax
        mov     eax, [pauSubtrahend + ebx + 16]
        sbb     [pauResultMinuend + ebx + 16], eax
        mov     eax, [pauSubtrahend + ebx + 20]
        sbb     [pauResultMinuend + ebx + 20], eax
        mov     eax, [pauSubtrahend + ebx + 24]
        sbb     [pauResultMinuend + ebx + 24], eax
        mov     eax, [pauSubtrahend + ebx + 28]
        sbb     [pauResultMinuend + ebx + 28], eax
        lea     ebx, [ebx + 32]
        dec     cUsed                   ; Does not change CF.
        jnz     .big_loop

        lahf                            ; Save CF
        mov     cUsed, [ebp + 10h]      ; Up to seven odd rounds.
        and     cUsed, 7
        jz      .done
        sahf                            ; Restore CF.
        jmp     .small_loop             ; Skip CF=1 (clc).

.small_job:
        clc
.small_loop:
        mov     eax, [pauSubtrahend + ebx]
        sbb     [pauResultMinuend + ebx], eax
        lea     ebx, [ebx + 4]
        dec     cUsed                   ; Does not change CF
        jnz     .small_loop
 %ifdef RT_STRICT
        jnc     .done
        int3
 %endif
.done:

        pop     ebx
        pop     edi
%else
 %error "Unsupported arch"
%endif

        leave
        ret
ENDPROC rtBigNumMagnitudeSubThisAssemblyWorker


;;
; Shifts an element array one bit to the left, returning the final carry value.
;
; On 64-bit hosts the array is always zero padded to a multiple of 8 bytes, so
; we can use 64-bit operand sizes even if the element type is 32-bit.
;
; @returns The final carry value.
; @param    pauElements     x86:[ebp +  8]  gcc:rdi  msc:rcx
; @param    cUsed           x86:[ebp + 12]  gcc:rsi  msc:rdx
; @param    uCarry          x86:[ebp + 16]  gcc:rdx  msc:r8
;
BEGINPROC rtBigNumMagnitudeShiftLeftOneAssemblyWorker
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_GCC
  %define pauElements       rdi
  %define cUsed             esi
  %define uCarry            edx
 %else
  %define pauElements       rcx
  %define cUsed             edx
  %define uCarry            r8d
 %endif
%elifdef RT_ARCH_X86
  %define pauElements       ecx
        mov     pauElements, [ebp + 08h]
  %define cUsed             edx
        mov     cUsed, [ebp + 0ch]
  %define uCarry            eax
        mov     uCarry, [ebp + 10h]
%else
 %error "Unsupported arch."
%endif
        ; Lots to do?
        cmp     cUsed, 8
        jae     .big_loop_init

        ; Check for empty array.
        test    cUsed, cUsed
        jz      .no_elements
        jmp     .small_loop_init

        ; Big loop - 8 unrolled loop iterations.
.big_loop_init:
%ifdef RT_ARCH_AMD64
        mov     r11d, cUsed
%endif
        shr     cUsed, 3
        test    uCarry, uCarry          ; clear the carry flag
        jz      .big_loop
        stc
.big_loop:
%if RTBIGNUM_ELEMENT_SIZE == 8
        rcl     qword [pauElements], 1
        rcl     qword [pauElements + 8], 1
        rcl     qword [pauElements + 16], 1
        rcl     qword [pauElements + 24], 1
        rcl     qword [pauElements + 32], 1
        rcl     qword [pauElements + 40], 1
        rcl     qword [pauElements + 48], 1
        rcl     qword [pauElements + 56], 1
        lea     pauElements, [pauElements + 64]
%else
        rcl     dword [pauElements], 1
        rcl     dword [pauElements + 4], 1
        rcl     dword [pauElements + 8], 1
        rcl     dword [pauElements + 12], 1
        rcl     dword [pauElements + 16], 1
        rcl     dword [pauElements + 20], 1
        rcl     dword [pauElements + 24], 1
        rcl     dword [pauElements + 28], 1
        lea     pauElements, [pauElements + 32]
%endif
        dec     cUsed
        jnz     .big_loop

        ; More to do?
        pushf                           ; save carry flag (uCarry no longer used on x86).
%ifdef RT_ARCH_AMD64
        mov     cUsed, r11d
%else
        mov     cUsed, [ebp + 0ch]
%endif
        and     cUsed, 7
        jz      .restore_cf_and_return  ; Jump if we're good and done.
        popf                            ; Restore CF.
        jmp     .small_loop             ; Deal with the odd rounds.
.restore_cf_and_return:
        popf
        jmp     .carry_to_eax

        ; Small loop - One round at the time.
.small_loop_init:
        test    uCarry, uCarry          ; clear the carry flag
        jz      .small_loop
        stc
.small_loop:
%if RTBIGNUM_ELEMENT_SIZE == 8
        rcl     qword [pauElements], 1
        lea     pauElements, [pauElements + 8]
%else
        rcl     dword [pauElements], 1
        lea     pauElements, [pauElements + 4]
%endif
        dec     cUsed
        jnz     .small_loop

        ; Calculate return value.
.carry_to_eax:
        mov     eax, 0
        jnc     .return
        inc     eax
.return:
        leave
        ret

.no_elements:
        mov     eax, uCarry
        jmp     .return
ENDPROC rtBigNumMagnitudeShiftLeftOneAssemblyWorker


;;
; Performs a 128-bit by 64-bit division on 64-bit and
; a 64-bit by 32-bit divison on 32-bit.
;
; @returns nothing.
; @param    puQuotient          x86:[ebp +  8]  gcc:rdi  msc:rcx        Double element.
; @param    puRemainder         x86:[ebp + 12]  gcc:rsi  msc:rdx        Normal element.
; @param    uDividendHi         x86:[ebp + 16]  gcc:rdx  msc:r8
; @param    uDividendLo         x86:[ebp + 20]  gcc:rcx  msc:r9
; @param    uDivisior           x86:[ebp + 24]  gcc:r8   msc:[rbp + 30h]
;
BEGINPROC rtBigNumElement2xDiv2xBy1x
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

%ifdef RT_ARCH_AMD64
 %if RTBIGNUM_ELEMENT_SIZE == 4
  %error "sorry not implemented yet."
          sorry not implemented yet.
 %endif

 %define  uDividendHi           rdx
 %define  uDividendLo           rax
 %ifdef ASM_CALL64_GCC
  %define uDivisor              r8
  %define puQuotient            rdi
  %define puRemainder           rsi
        mov     rax, rcx
 %else
  %define puQuotient            rcx
  %define puRemainder           r11
  %define uDivisor              r10
        mov     r11, rdx
        mov     r10, [rbp + 30h]
        mov     rdx, r8
        mov     rax, r9
 %endif

%elifdef RT_ARCH_X86
        push    edi
        push    ebx

 %define uDividendHi            edx
        mov     uDividendHi, [ebp + 10h]
 %define uDividendLo            eax
        mov     uDividendLo, [ebp + 14h]
 %define uDivisor               ecx
        mov     uDivisor,    [ebp + 18h]
 %define puQuotient             edi
        mov     puQuotient,  [ebp + 08h]
 %define puRemainder            ebx
        mov     puRemainder, [ebp + 0ch]
%else
 %error "Unsupported arch."
%endif

%ifdef RT_STRICT
        ;
        ; The dividend shall not be zero.
        ;
        test    uDivisor, uDivisor
        jnz     .divisor_not_zero
        int3
.divisor_not_zero:
%endif

        ;
        ; Avoid division overflow.  This will calculate the high part of the quotient.
        ;
        mov     RTBIGNUM_ELEMENT_PRE [puQuotient + RTBIGNUM_ELEMENT_SIZE], 0
        cmp     uDividendHi, uDivisor
        jb      .do_divide
        push    xAX
        mov     xAX, xDX
        xor     edx, edx
        div     uDivisor
        mov     RTBIGNUM_ELEMENT_PRE [puQuotient + RTBIGNUM_ELEMENT_SIZE], xAX
        pop     xAX

        ;
        ; Perform the division and store the result.
        ;
.do_divide:
        div     uDivisor
        mov     RTBIGNUM_ELEMENT_PRE [puQuotient], xAX
        mov     RTBIGNUM_ELEMENT_PRE [puRemainder], xDX


%ifdef RT_ARCH_X86
        pop     ebx
        pop     edi
%endif
        leave
        ret
ENDPROC rtBigNumElement2xDiv2xBy1x


;;
; Performs the core of long multiplication.
;
; @returns nothing.
; @param    pauResult           x86:[ebp +  8]  gcc:rdi  msc:rcx        Initialized to zero.
; @param    pauMultiplier       x86:[ebp + 12]  gcc:rsi  msc:rdx
; @param    cMultiplier         x86:[ebp + 16]  gcc:rdx  msc:r8
; @param    pauMultiplicand     x86:[ebp + 20]  gcc:rcx  msc:r9
; @param    cMultiplicand       x86:[ebp + 24]  gcc:r8   msc:[rbp + 30h]
;
BEGINPROC rtBigNumMagnitudeMultiplyAssemblyWorker
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

%ifdef RT_ARCH_AMD64
 %if RTBIGNUM_ELEMENT_SIZE == 4
  %error "sorry not implemented yet."
          sorry not implemented yet.
 %endif

 %ifdef ASM_CALL64_GCC
  %define pauResult             rdi
  %define pauMultiplier         rsi
  %define cMultiplier           r9
  %define pauMultiplicand       rcx
  %define cMultiplicand         r8
        mov     r9d, edx                ; cMultiplier
        mov     r8d, r8d                ; cMultiplicand - paranoia
  %define uMultiplier           r10
  %define iMultiplicand         r11
 %else
  %define pauResult             rcx
  %define pauMultiplier         r11
  %define cMultiplier           r8
  %define pauMultiplicand       r9
  %define cMultiplicand         r10
        mov     pauMultiplier, rdx
        mov     r10d, dword [rbp + 30h] ; cMultiplicand
        mov     r8d, r8d                ; cMultiplier - paranoia
  %define uMultiplier           r12
        push    r12
  %define iMultiplicand         r13
        push    r13
 %endif

%elifdef RT_ARCH_X86
        push    edi
        push    esi
        push    ebx
        sub     esp, 10h
 %define pauResult              edi
        mov     pauResult,      [ebp + 08h]
 %define pauMultiplier          dword [ebp + 0ch]
 %define cMultiplier            dword [ebp + 10h]
 %define pauMultiplicand        ecx
        mov     pauMultiplicand, [ebp + 14h]
 %define cMultiplicand          dword [ebp + 18h]
 %define uMultiplier            dword [ebp - 10h]
 %define iMultiplicand          ebx

%else
 %error "Unsupported arch."
%endif

        ;
        ; Check that the multiplicand isn't empty (avoids an extra jump in the inner loop).
        ;
        cmp     cMultiplicand, 0
        je      .done

        ;
        ; Loop thru each element in the multiplier.
        ;
        ; while (cMultiplier-- > 0)
.multiplier_loop:
        cmp     cMultiplier, 0
        jz      .done
        dec     cMultiplier

        ; uMultiplier = *pauMultiplier
%ifdef RT_ARCH_X86
        mov     edx, pauMultiplier
        mov     eax, [edx]
        mov     uMultiplier, eax
%else
        mov     uMultiplier, [pauMultiplier]
%endif
        ; for (iMultiplicand = 0; iMultiplicand < cMultiplicand; iMultiplicand++)
        xor     iMultiplicand, iMultiplicand
.multiplicand_loop:
        mov     xAX, [pauMultiplicand + iMultiplicand * RTBIGNUM_ELEMENT_SIZE]
        mul     uMultiplier
        add     [pauResult + iMultiplicand * RTBIGNUM_ELEMENT_SIZE], xAX
        adc     [pauResult + iMultiplicand * RTBIGNUM_ELEMENT_SIZE + RTBIGNUM_ELEMENT_SIZE], xDX
        jnc     .next_multiplicand
        lea     xDX, [iMultiplicand + 2]
.next_adc:
        adc     RTBIGNUM_ELEMENT_PRE [pauResult + xDX * RTBIGNUM_ELEMENT_SIZE], 0
        inc     xDX
        jc      .next_adc

.next_multiplicand:
        inc     iMultiplicand                   ; iMultiplicand++
        cmp     iMultiplicand, cMultiplicand    ; iMultiplicand < cMultiplicand
        jb      .multiplicand_loop

        ; Advance and loop on multiplier.
        add     pauMultiplier, RTBIGNUM_ELEMENT_SIZE
        add     pauResult, RTBIGNUM_ELEMENT_SIZE
        jmp     .multiplier_loop

.done:

%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_GCC
 %else
        pop     r13
        pop     r12
 %endif
%elifdef RT_ARCH_X86
        add     esp, 10h
        pop     ebx
        pop     esi
        pop     edi
%endif
        leave
        ret
ENDPROC rtBigNumMagnitudeMultiplyAssemblyWorker

;;
; Assembly implementation of the D4 step of Knuth's division algorithm.
;
; This subtracts Divisor * Qhat from the dividend at the current J index.
;
; @returns true if negative result (unlikely), false if positive.
; @param    pauDividendJ        x86:[ebp +  8]  gcc:rdi  msc:rcx        Initialized to zero.
; @param    pauDivisor          x86:[ebp + 12]  gcc:rsi  msc:rdx
; @param    cDivisor            x86:[ebp + 16]  gcc:edx  msc:r8d
; @param    uQhat               x86:[ebp + 16]  gcc:rcx  msc:r9
;
BEGINPROC rtBigNumKnuthD4_MulSub
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
SEH64_END_PROLOGUE

%ifdef RT_ARCH_AMD64
 %if RTBIGNUM_ELEMENT_SIZE == 4
  %error "sorry not implemented yet."
          sorry not implemented yet.
 %endif

 %ifdef ASM_CALL64_GCC
  %define pauDividendJ          rdi
  %define pauDivisor            rsi
  %define cDivisor              r8
  %define uQhat                 rcx
        mov     r8d, edx                ; cDivisor
  %define uMulCarry             r11
 %else
  %define pauDividendJ          rcx
  %define pauDivisor            r10
  %define cDivisor              r8
  %define uQhat                 r9
        mov     r10, rdx                ; pauDivisor
        mov     r8d, r8d                ; cDivisor - paranoia
  %define uMulCarry             r11
 %endif

%elifdef RT_ARCH_X86
        push    edi
        push    esi
        push    ebx
 %define pauDividendJ           edi
        mov     pauDividendJ,   [ebp + 08h]
 %define pauDivisor             esi
        mov     pauDivisor,     [ebp + 0ch]
 %define cDivisor               ecx
        mov     cDivisor,       [ebp + 10h]
 %define uQhat                  dword [ebp + 14h]
 %define uMulCarry              ebx
%else
 %error "Unsupported arch."
%endif

%ifdef RT_STRICT
        ;
        ; Some sanity checks.
        ;
        cmp     cDivisor, 0
        jne     .cDivisor_not_zero
        int3
.cDivisor_not_zero:
%endif

        ;
        ; Initialize the loop.
        ;
        xor     uMulCarry, uMulCarry

        ;
        ; do ... while (cDivisor-- > 0);
        ;
.the_loop:
        ; RTUInt128MulU64ByU64(&uSub, uQhat, pauDivisor[i]);
        mov     xAX, uQhat
        mul     RTBIGNUM_ELEMENT_PRE [pauDivisor]
        ; RTUInt128AssignAddU64(&uSub, uMulCarry);
        add     xAX, uMulCarry
        adc     xDX, 0
        mov     uMulCarry, xDX
        ; Subtract uSub.s.Lo+fCarry from pauDividendJ[i]
        sub     [pauDividendJ], xAX
        adc     uMulCarry, 0
%ifdef RT_STRICT
        jnc     .uMulCarry_did_not_overflow
        int3
.uMulCarry_did_not_overflow:
%endif

        ; Advance.
        add     pauDividendJ, RTBIGNUM_ELEMENT_SIZE
        add     pauDivisor, RTBIGNUM_ELEMENT_SIZE
        dec     cDivisor
        jnz     .the_loop

        ;
        ; Final dividend element (no corresponding divisor element).
        ;
        sub     [pauDividendJ], uMulCarry
        sbb     eax, eax
        and     eax, 1

.done:
%ifdef RT_ARCH_AMD64
%elifdef RT_ARCH_X86
        pop     ebx
        pop     esi
        pop     edi
%endif
        leave
        ret
ENDPROC rtBigNumKnuthD4_MulSub

