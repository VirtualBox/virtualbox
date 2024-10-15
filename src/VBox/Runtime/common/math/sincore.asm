; $Id: sincore.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT common sin & cos - AMD64 & X86.
;

;
; Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
%include "iprt/x86.mac"


BEGINCODE

;;
; Internal sine and cosine worker that calculates the sine of st0 returning
; it in st0.
;
; When called by a sine function, fabs(st0) >= pi/2.
; When called by a cosine function, fabs(original input value) >= 3pi/8.
;
; That the input isn't a tiny number close to zero, means that we can do a bit
; cruder rounding when operating close to a pi/2 boundrary.  The value in the
; ecx register indicates the input precision and controls the crudeness of the
; rounding.
;
; @returns st0 = sine
; @param   st0      A finite number to calucate sine of.
; @param   ecx      Set to 0 if original input was a 32-bit float.
;                   Set to 1 if original input was a 64-bit double.
;                   set to 2 if original input was a 80-bit long double.
;
BEGINPROC   rtNoCrtMathSinCore
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        SEH64_END_PROLOGUE

        ;
        ; Load the pointer to the rounding crudeness factor into xDX.
        ;
        lea     xDX, [.s_ar64NearZero xWrtRIP]
        lea     xDX, [xDX + xCX * xCB]

        ;
        ; Finite number.  We want it in the range [0,2pi] and will preform
        ; a remainder division if it isn't.
        ;
        fcom    qword [.s_r64Max xWrtRIP]   ; compares st0 and 2*pi
        fnstsw  ax
        test    ax, X86_FSW_C3 | X86_FSW_C0 | X86_FSW_C2 ; C3 := st0 == mem;  C0 := st0 < mem;  C2 := unordered (should be the case);
        jz      .reduce_st0                 ; Jump if st0 > mem

        fcom    qword [.s_r64Min xWrtRIP]   ; compares st0 and 0.0
        fnstsw  ax
        test    ax, X86_FSW_C3 | X86_FSW_C0
        jnz     .reduce_st0                 ; Jump if st0 <= mem

        ;
        ; We get here if st0 is in the [0,2pi] range.
        ;
        ; Now, FSIN is documented to be reasonably accurate for the range
        ; -3pi/4 to +3pi/4, so we have to make some more effort to calculate
        ; in that range only.
        ;
.in_range:
        ; if (st0 < pi)
        fldpi
        fcom    st1                         ; compares st0 (pi) with st1 (the normalized value)
        fnstsw  ax
        test    ax, X86_FSW_C0              ; st1 > pi
        jnz     .larger_than_pi
        test    ax, X86_FSW_C3
        jnz     .equals_pi

        ;
        ; input in the range [0,pi[
        ;
.smaller_than_pi:
        fdiv    qword [.s_r64Two xWrtRIP]   ; st0 = pi/2

        ; if (st0 < pi/2)
        fcom    st1                         ; compares st0 (pi/2) with st1
        fnstsw  ax
        test    ax, X86_FSW_C0              ; st1 > pi
        jnz     .between_half_pi_and_pi
        test    ax, X86_FSW_C3
        jnz     .equals_half_pi

        ;
        ; The value is between zero and half pi, including the zero value.
        ;
        ; This is in range where FSIN works reasonably reliably. So drop the
        ; half pi in st0 and do the calculation.
        ;
.between_zero_and_half_pi:
        ; Check if we're so close to pi/2 that it makes no difference.
        fsub    st0, st1                    ; st0 = pi/2 - st1
        fcom    qword [xDX]
        fnstsw  ax
        test    ax, X86_FSW_C0 | X86_FSW_C3 ; st0 <= very small positive number.
        jnz     .equals_half_pi
        ffreep  st0

        ; Check if we're so close to zero that it makes no difference given the
        ; internal accuracy of the FPU.
        fcom    qword [xDX]
        fnstsw  ax
        test    ax, X86_FSW_C0 | X86_FSW_C3 ; st0 <= very small positive number.
        jnz     .equals_zero_popped_one

        ; Ok, calculate sine.
        fsin
        jmp     .return

        ;
        ; The value is in the range ]pi/2,pi[
        ;
        ; This is outside the comfortable FSIN range, but if we subtract PI and
        ; move to the ]-pi/2,0[ range we just have to change the sign to get
        ; the value we want.
        ;
.between_half_pi_and_pi:
        ; Check if we're so close to pi/2 that it makes no difference.
        fsubr   st0, st1                    ; st0 = st1 - st0
        fcom    qword [xDX]
        fnstsw  ax
        test    ax, X86_FSW_C0 | X86_FSW_C3 ; st0 <= very small positive number.
        jnz     .equals_half_pi
        ffreep  st0

        ; Check if we're so close to pi that it makes no difference.
        fldpi
        fsub    st0, st1                    ; st0 = st0 - st1
        fcom    qword [xDX]
        fnstsw  ax
        test    ax, X86_FSW_C0 | X86_FSW_C3 ; st0 <= very small positive number.
        jnz     .equals_pi
        ffreep  st0

        ; Ok, transform the value and calculate sine.
        fldpi
        fsubp   st1, st0

        fsin
        fchs
        jmp     .return

        ;
        ; input in the range ]pi,2pi[
        ;
.larger_than_pi:
        fsub    st1, st0                    ; st1 -= pi
        fdiv    qword [.s_r64Two xWrtRIP]   ; st0 = pi/2

        ; if (st0 < pi/2)
        fcom    st1                         ; compares st0 (pi/2) with reduced st1
        fnstsw  ax
        test    ax, X86_FSW_C0              ; st1 > pi
        jnz     .between_3_half_pi_and_2pi
        test    ax, X86_FSW_C3
        jnz     .equals_3_half_pi

        ;
        ; The value is in the the range: ]pi,3pi/2[
        ;
        ; The actual st0 is in the range ]pi,pi/2[ where FSIN is performing okay
        ; and we can get the desired result by changing the sign (-FSIN).
        ;
.between_pi_and_3_half_pi:
        ; Check if we're so close to pi/2 that it makes no difference.
        fsub    st0, st1                    ; st0 = pi/2 - st1
        fcom    qword [xDX]
        fnstsw  ax
        test    ax, X86_FSW_C0 | X86_FSW_C3 ; st0 <= very small positive number.
        jnz     .equals_3_half_pi
        ffreep  st0

        ; Check if we're so close to zero that it makes no difference given the
        ; internal accuracy of the FPU.
        fcom    qword [xDX]
        fnstsw  ax
        test    ax, X86_FSW_C0 | X86_FSW_C3 ; st0 <= very small positive number.
        jnz     .equals_pi_popped

        ; Ok, calculate sine and flip the sign.
        fsin
        fchs
        jmp     .return

        ;
        ; The value is in the last pi/2 of the range: ]3pi/2,2pi[
        ;
        ; Since FSIN should work reasonably well for ]-pi/2,pi], we can just
        ; subtract pi again (we subtracted pi at .larger_than_pi above) and
        ; run FSIN on it.  (st1 is currently in the range ]pi/2,pi[.)
        ;
.between_3_half_pi_and_2pi:
        ; Check if we're so close to pi/2 that it makes no difference.
        fsubr   st0, st1                    ; st0 = st1 - st0
        fcom    qword [xDX]
        fnstsw  ax
        test    ax, X86_FSW_C0 | X86_FSW_C3 ; st0 <= very small positive number.
        jnz     .equals_3_half_pi
        ffreep  st0

        ; Check if we're so close to pi that it makes no difference.
        fldpi
        fsub    st0, st1                    ; st0 = st0 - st1
        fcom    qword [xDX]
        fnstsw  ax
        test    ax, X86_FSW_C0 | X86_FSW_C3 ; st0 <= very small positive number.
        jnz     .equals_2pi
        ffreep  st0

        ; Ok, adjust input and calculate sine.
        fldpi
        fsubp   st1, st0
        fsin
        jmp     .return

        ;
        ; sin(0) = 0
        ; sin(pi) = 0
        ;
.equals_zero:
.equals_pi:
.equals_2pi:
        ffreep  st0
.equals_zero_popped_one:
.equals_pi_popped:
        ffreep  st0
        fldz
        jmp     .return

        ;
        ; sin(pi/2) = 1
        ;
.equals_half_pi:
        ffreep  st0
        ffreep  st0
        fld1
        jmp     .return

        ;
        ; sin(3*pi/2) = -1
        ;
.equals_3_half_pi:
        ffreep  st0
        ffreep  st0
        fld1
        fchs
        jmp     .return

        ;
        ; Return.
        ;
.return:
        leave
        ret

        ;
        ; Reduce st0 by reminder division by PI*2.  The result should be positive here.
        ;
        ;; @todo this is one of our weak spots (really any calculation involving PI is).
.reduce_st0:
        fldpi
        fadd    st0, st0
        fxch    st1                     ; st0=input (dividend) st1=2pi (divisor)
.again:
        fprem1
        fnstsw  ax
        test    ah, (X86_FSW_C2 >> 8)   ; C2 is set if partial result.
        jnz     .again                  ; Loop till C2 == 0 and we have a final result.

        ;
        ; Make sure the result is positive.
        ;
        fxam
        fnstsw  ax
        test    ax, X86_FSW_C1          ; The sign bit
        jz      .reduced_to_positive

        fadd    st0, st1                ; st0 += 2pi, which should make it positive

%ifdef RT_STRICT
        fxam
        fnstsw  ax
        test    ax, X86_FSW_C1
        jz      .reduced_to_positive
        int3
%endif

.reduced_to_positive:
        fstp    st1                     ; Get rid of the 2pi value.
        jmp     .in_range

ALIGNCODE(8)
.s_r64Max:
        dq +6.28318530717958647692      ; 2*pi
.s_r64Min:
        dq 0.0
.s_r64Two:
        dq 2.0
        ;;
        ; Close to 2/pi rounding limits for 32-bit, 64-bit and 80-bit floating point operations.
        ; Given that the original input is at least +/-3pi/8 (1.178) and that precision of the
        ; PI constant used during reduction/whatever, I think we can round to a whole pi/2
        ; step when we get close enough.
        ;
        ; Look to RTFLOAT64U for the format details, but 52 is the shift for the exponent field
        ; and 1023 is the exponent bias.  Since the format uses an implied 1 in the mantissa,
        ; we only have to set the exponent to get a valid number.
        ;
.s_ar64NearZero:
;; @todo check how sensible these really are...
        dq  (-18 + 1023) << 52          ; float / 32-bit / single precision input
        dq  (-40 + 1023) << 52          ; double / 64-bit / double precision input
        dq  (-52 + 1023) << 52          ; long double / 80-bit / extended precision input
ENDPROC     rtNoCrtMathSinCore

