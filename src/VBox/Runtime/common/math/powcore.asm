; $Id: powcore.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT common pow code - AMD64 & X86.
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


%define RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"


BEGINCODE

extern NAME(RT_NOCRT(feraiseexcept))

;;
; Call feraiseexcept(%1)
%macro CALL_feraiseexcept_WITH 1
 %ifdef RT_ARCH_X86
        mov     dword [xSP], X86_FSW_IE
 %elifdef ASM_CALL64_GCC
        mov     edi, X86_FSW_IE
 %elifdef ASM_CALL64_MSC
        mov     ecx, X86_FSW_IE
 %else
  %error calling conv.
 %endif
        call    NAME(RT_NOCRT(feraiseexcept))
%endmacro


;;
; Compute the st1 to the power of st0.
;
; @returns  st(0) = result
;           eax = what's being returned:
;               0 - Just a value.
;               1 - The rBase value. Caller may take steps to ensure it's exactly the same.
;               2 - The rExp value.  Caller may take steps to ensure it's exactly the same.
; @param    rBase/st1       The base.
; @param    rExp/st0        The exponent
; @param    fFxamBase/dx    The status flags after fxam(rBase).
; @param    enmType/ebx     The original parameter and return types:
;                               0 - 32-bit / float
;                               1 - 64-bit / double
;                               2 - 80-bit / long double
;
BEGINPROC rtNoCrtMathPowCore
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        sub     xSP, 30h
        SEH64_ALLOCATE_STACK 30h
        SEH64_END_PROLOGUE

        ;
        ; Weed out special values, starting with the exponent.
        ;
        fxam
        fnstsw  ax
        mov     cx, ax                      ; cx=fxam(exp)

        and     ax, X86_FSW_C3 | X86_FSW_C2 | X86_FSW_C0
        cmp     ax, X86_FSW_C2              ; Normal finite number (excluding zero)
        je      .exp_finite
        cmp     ax, X86_FSW_C3              ; Zero
        je      .exp_zero
        cmp     ax, X86_FSW_C3 | X86_FSW_C2 ; Denormals
        je      .exp_finite
        cmp     ax, X86_FSW_C0 | X86_FSW_C2 ; Infinity.
        je      .exp_inf
        jmp     .exp_nan

.exp_finite:
        ;
        ; Detect special base values.
        ;
        mov     ax, dx                      ; ax=fxam(base)
        and     ax, X86_FSW_C3 | X86_FSW_C2 | X86_FSW_C0
        cmp     ax, X86_FSW_C2              ; Normal finite number (excluding zero)
        je      .base_finite
        cmp     ax, X86_FSW_C3              ; Zero
        je      .base_zero
        cmp     ax, X86_FSW_C3 | X86_FSW_C2 ; Denormals
        je      .base_finite
        cmp     ax, X86_FSW_C0 | X86_FSW_C2 ; Infinity.
        je      .base_inf
        jmp     .base_nan

.base_finite:
        ;
        ; 1 in the base is also special.
        ; Rule 6 (see below): base == +1 and exponent = whatever: Return +1.0
        ;
        fld1
        fcomip  st0, st2
        je      .return_base_value

        ;
        ; Check if the exponent is an integer value we can handle in a 64-bit
        ; GRP as that is simpler to handle accurately.
        ;
        ; In 64-bit integer range?
        fld     tword [.s_r80MaxInt xWrtRIP]
        fcomip  st0, st1
        jb      .not_integer_exp

        fld     tword [.s_r80MinInt xWrtRIP]
        fcomip  st0, st1
        ja      .not_integer_exp

        ; Convert it to integer.
        fld     st0                         ; -> st0=exp; st1=exp; st2=base
        fistp   qword [xBP - 8]             ; Save and pop 64-bit int (no non-popping version of this instruction).

        fild    qword [xBP - 8]             ; Load it again for comparison.
        fucomip st0, st1                    ; Compare integer exp and floating point exp to see if they are the same. Pop.
        jne     .not_integer_exp


        ;
        ;
        ; Ok, we've got an integer exponent value in that fits into a 64-bit.
        ; We'll multiply the base exponention bit by exponention bit, applying
        ; it as a factor for bits that are set.
        ;
        ;
.integer_exp:
        ; Load the integer value into edx:exx / rdx and ditch the floating point exponent.
        mov     xDX, [xBP - 8]
%ifdef RT_ARCH_X86
        mov     eax, [xBP - 8 + 4]
%endif
        ffreep  st0                         ; -> st0=base;

        ; Load a 1 onto the stack, we'll need it below as well as for converting
        ; a negative exponent to a positive one.
        fld1                                ; -> st0=1.0; st1=base;

        ; If the exponent is negative, negate it and change base to 1/base.
        or      xDX, xDX
        jns     .integer_exp_positive
        neg     xDX
%ifdef RT_ARCH_X86
        neg     eax
        sbb     edx, 0
%endif
        fdivr  st1, st0                    ; -> st0=1.0; st1=1/base
.integer_exp_positive:

        ;
        ; We'll process edx:eax / rdx bit by bit till it's zero, using st0 for
        ; the multiplication factor corresponding to the current exponent bit
        ; and st1 as the result.
        ;
        fxch                                ; -> st0=base; st1=1.0;
.integer_exp_loop:
%ifdef RT_ARCH_X86
        shrd    eax, edx, 1
%else
        shr     rdx, 1
%endif
        jnc     .integer_exp_loop_advance
        fmul    st1, st0

.integer_exp_loop_advance:
        ; Check if we're done.
%ifdef RT_ARCH_AMD64
        jz      .integer_exp_return         ; (we will have the flags for the shr rdx above)
%else
        shr     edx, 1                      ; complete the above shift operation

        mov     ecx, edx                    ; check if edx:eax is zero.
        or      ecx, eax
        jz      .integer_exp_return
%endif
        ; Calculate the factor for the next bit.
        fmul    st0, st0
        jmp     .integer_exp_loop

.integer_exp_return:
        ffreep  st0                         ; drop the factor -> st0=result; no st1.
        jmp     .return_val


        ;
        ;
        ; Non-integer or value was out of range for an int64_t.
        ;
        ; The approach here is the same as in exp.asm, only we have to do the
        ; log2(base) calculation first as it's a parameter and not a constant.
        ;
        ;
.not_integer_exp:

        ; First reject negative numbers.  We still have the fxam(base) status in dx.
        test    dx, X86_FSW_C1
        jnz     .base_negative_non_integer_exp

        ; Swap the items on the stack, so we can process the base first.
        fxch    st0, st1                    ; -> st0=base; st1=exponent;

        ;
        ; From log2.asm:
        ;
        ; The fyl2xp1 instruction (ST1=ST1*log2(ST0+1.0), popping ST0) has a
        ; valid ST0 range of 1(1-sqrt(0.5)) (approx 0.29289321881) on both
        ; sides of zero.  We try use it if we can.
        ;
.above_one:
        ; For both fyl2xp1 and fyl2xp1 we need st1=1.0.
        fld1
        fxch    st0, st1                    ; -> st0=base; st1=1.0; st2=exponent

        ; Check if the input is within the fyl2xp1 range.
        fld     qword [.s_r64AbsFyL2xP1InputMax xWrtRIP]
        fcomip  st0, st1
        jbe     .cannot_use_fyl2xp1

        fld     qword [.s_r64AbsFyL2xP1InputMin xWrtRIP]
        fcomip  st0, st1
        jae     .cannot_use_fyl2xp1

        ; Do the calculation.
.use_fyl2xp1:
        fsub    st0, st1                    ; -> st0=base-1; st1=1.0; st2=exponent
        fyl2xp1                             ; -> st0=1.0*log2(base-1.0+1.0); st1=exponent
        jmp     .done_log2

.cannot_use_fyl2xp1:
        fyl2x                               ; -> st0=1.0*log2(base); st1=exponent
.done_log2:

        ;
        ; From exp.asm:
        ;
        ; Convert to power of 2 and it'll be the same as exp2.
        ;
        fmulp                               ; st0=log2(base); st1=exponent  -> st0=pow2exp

        ;
        ; Split the job in two on the fraction and integer l2base parts.
        ;
        fld     st0                         ; Push a copy of the pow2exp on the stack.
        frndint                             ; st0 = (int)pow2exp
        fsub    st1, st0                    ; st1 = pow2exp - (int)pow2exp; i.e. st1 = fraction, st0 = integer.
        fxch                                ; st0 = fraction, st1 = integer.

        ; 1. Calculate on the fraction.
        f2xm1                               ; st0 = 2**fraction - 1.0
        fld1
        faddp                               ; st0 = 2**fraction

        ; 2. Apply the integer power of two.
        fscale                              ; st0 = result; st1 = integer part of pow2exp.
        fstp    st1                         ; st0 = result; no st1.

        ;
        ; Return st0.
        ;
.return_val:
        xor     eax, eax
.return:
        leave
        ret


        ;
        ;
        ; pow() has a lot of defined behavior for special values, which is why
        ; this is the largest and most difficult part of the code. :-)
        ;
        ; On https://pubs.opengroup.org/onlinepubs/9699919799/functions/pow.html
        ; there are 21 error conditions listed in the return value section.
        ; The code below refers to this by number.
        ;
        ; When we get here:
        ;       dx=fxam(base)
        ;       cx=fxam(exponent)
        ;       st1=base
        ;       st0=exponent
        ;

        ;
        ; 1. Finit base < 0 and finit non-interger exponent: -> domain error (#IE) + NaN.
        ;
        ; The non-integer exponent claim might be wrong, as we only check if it
        ; fits into a int64_t register.  But, I don't see how we can calculate
        ; it right now.
        ;
.base_negative_non_integer_exp:
        CALL_feraiseexcept_WITH X86_FSW_IE
        jmp     .return_nan

        ;
        ; 7. Exponent = +/-0.0, any base value including NaN: return +1.0
        ; Note! According to https://en.cppreference.com/w/c/numeric/math/pow a
        ;       domain error (#IE) occur if base=+/-0.  Not implemented.
.exp_zero:
.return_plus_one:
        fld1
        jmp     .return_pop_pop_val

        ;
        ;     6. Exponent = whatever and base = 1: Return 1.0
        ;    10. Exponent = +/-Inf and base = -1:  Return 1.0
        ;6+10 => Exponent = +/-Inf and |base| = 1: Return 1.0
        ;    11. Exponent = -Inf and |base| < 1:   Return +Inf
        ;    12. Exponent = -Inf and |base| > 1:   Return +0
        ;    13. Exponent = +Inf and |base| < 1:   Return +0
        ;    14. Exponent = +Inf and |base| > 1:   Return +Inf
        ;
        ; Note! Rule 4 would trigger for the same conditions as 11 when base == 0,
        ;       but it's optional to raise div/0 and it's apparently marked as
        ;       obsolete in C23, so not implemented.
        ;
.exp_inf:
        ; Check if base is NaN or unsupported.
        and     dx, X86_FSW_C3 | X86_FSW_C2 | X86_FSW_C0 ; fxam(base)
        cmp     dx, X86_FSW_C0
        jbe     .return_base_nan

        ; Calc fabs(base) and replace the exponent with 1.0 as we're very likely to need this here.
        ffreep  st0
        fabs
        fld1                                ; st0=1.0;  st1=|rdBase|
        fcomi   st0, st1
        je      .return_plus_one            ; Matches rule 6 + 10 (base is +/-1).
        ja      .exp_inf_base_smaller_than_one
.exp_inf_base_larger_than_one:
        test    cx, X86_FSW_C1              ; cx=faxm(exponent); C1=sign
        jz      .return_plus_inf            ; Matches rule 14 (exponent is +Inf).
        jmp     .return_plus_zero           ; Matches rule 12 (exponent is -Inf).

.exp_inf_base_smaller_than_one:
        test    cx, X86_FSW_C1              ; cx=faxm(exponent); C1=sign
        jnz     .return_plus_inf            ; Matches rule 11 (exponent is -Inf).
        jmp     .return_plus_zero           ; Matches rule 13 (exponent is +Inf).

        ;
        ; 6. Exponent = whatever and base = 1: Return 1.0
        ; 5. Unless specified elsewhere, return NaN if any of the parameters are NaN.
        ;
.exp_nan:
        ; Check if base is a number and possible 1.
        test    dx, X86_FSW_C2              ; dx=fxam(base); C2 is set for finite number, infinity and denormals.
        jz      .return_exp_nan
        fld1
        fcomip  st0, st2
        jne     .return_exp_nan
        jmp     .return_plus_one

        ;
        ; 4a. base == +/-0.0 and exp < 0 and exp is odd integer:  Return +/-Inf, raise div/0.
        ; 4b. base == +/-0.0 and exp < 0 and exp is not odd int:  Return +Inf, raise div/0.
        ;  8. base == +/-0.0 and exp > 0 and exp is odd integer:  Return +/-0.0
        ;  9. base == +/-0.0 and exp > 0 and exp is not odd int:  Return +0
        ;
        ; Note! Exponent must be finite and non-zero if we get here.
        ;
.base_zero:
        fldz
        fcomip  st0, st1
        jbe     .base_zero_plus_exp
.base_zero_minus_exp:
        mov     cx, dx                      ; stashing fxam(base) in CX because EDX is trashed by .is_exp_odd_integer
        call    .is_exp_odd_integer         ; trashes EDX but no ECX.
        or      eax, eax
        jz     .base_zero_minus_exp_not_odd_int

        ; Matching 4a.
.base_zero_minus_exp_odd_int:
        test    cx, X86_FSW_C1              ; base sign
        jz      .raise_de_and_return_plus_inf
.raise_de_and_return_minus_inf:
        CALL_feraiseexcept_WITH X86_FSW_DE
        jmp     .return_minus_inf
.raise_de_and_return_plus_inf:
        CALL_feraiseexcept_WITH X86_FSW_DE
        jmp     .return_plus_inf

        ; Matching 4b.
.base_zero_minus_exp_not_odd_int:
        CALL_feraiseexcept_WITH X86_FSW_DE
        jmp     .return_plus_inf

.base_zero_plus_exp:
        call    .is_exp_odd_integer
        or      eax, eax
        jnz     .return_base_value          ; Matching 8
.return_plus_zero:                          ; Matching 9
        fldz
        jmp     .return_pop_pop_val

        ;
        ;  15. base == -Inf and exp < 0 and exp is odd integer: Return -0
        ;  16. base == -Inf and exp < 0 and exp is not odd int: Return +0
        ;  17. base == -Inf and exp > 0 and exp is odd integer: Return -Inf
        ;  18. base == -Inf and exp > 0 and exp is not odd int: Return +Inf
        ;  19. base == +Inf and exp < 0:                        Return +0
        ;  20. base == +Inf and exp > 0:                        Return +Inf
        ;
        ; Note! Exponent must be finite and non-zero if we get here.
        ;
.base_inf:
        fldz
        fcomip  st0, st1
        jbe     .base_inf_plus_exp
.base_inf_minus_exp:
        test    dx, X86_FSW_C1
        jz      .return_plus_zero           ; Matches 19 (base == +Inf).
.base_minus_inf_minus_exp:
        call    .is_exp_odd_integer
        or      eax, eax
        jz      .return_plus_zero           ; Matches 16 (exp not odd and < 0, base == -Inf)
.return_minus_zero:                         ; Matches 15 (exp is odd and < 0, base == -Inf)
        fldz
        fchs
        jmp     .return_pop_pop_val

.base_inf_plus_exp:
        test    dx, X86_FSW_C1
        jz      .return_plus_inf            ; Matches 20 (base == +Inf).
.base_minus_inf_plus_exp:
        call    .is_exp_odd_integer
        or      eax, eax
        jnz     .return_minus_inf           ; Matches 17 (exp is odd and > 0, base == +Inf)
        jmp     .return_plus_inf            ; Matches 18 (exp not odd and > 0, base == +Inf)

        ;
        ; Return the exponent NaN (or whatever) value.
        ;
.return_exp_nan:
        fld     st0
        mov     eax, 2                      ; return param 2
        jmp     .return_pop_pop_val_with_eax

        ;
        ; Return the base NaN (or whatever) value.
        ;
.return_base_nan:
.return_base_value:
.base_nan:                  ; 5. Unless specified elsewhere, return NaN if any of the parameters are NaN.
        fld     st1
        mov     eax, 1                      ; return param 1
        jmp     .return_pop_pop_val_with_eax

        ;
        ; Pops the two values off the FPU stack and returns NaN.
        ;
.return_nan:
        fld     qword [.s_r64QNan xWrtRIP]
        jmp     .return_pop_pop_val

        ;
        ; Pops the two values off the FPU stack and returns +Inf.
        ;
.return_plus_inf:
        fld     qword [.s_r64PlusInf xWrtRIP]
        jmp     .return_pop_pop_val

        ;
        ; Pops the two values off the FPU stack and returns -Inf.
        ;
.return_minus_inf:
        fld     qword [.s_r64MinusInf xWrtRIP]
        jmp     .return_pop_pop_val

        ;
        ; Return st0, remove st1 and st2.
        ;
.return_pop_pop_val:
        xor     eax, eax
.return_pop_pop_val_with_eax:
        fstp    st2
        ffreep  st0
        jmp     .return


ALIGNCODE(8)
.s_r80MaxInt:
        dt  +9223372036854775807.0

ALIGNCODE(8)
.s_r80MinInt:
        dt  -9223372036854775807.0

ALIGNCODE(8)
       ;; The fyl2xp1 instruction only works between +/-1(1-sqrt(0.5)).
        ; These two variables is that range + 1.0, so we can compare directly
        ; with the input w/o any extra fsub and fabs work.
.s_r64AbsFyL2xP1InputMin:
        dq      0.708 ; -0.292 + 1.0
.s_r64AbsFyL2xP1InputMax:
        dq      1.292

.s_r64QNan:
        dq      RTFLOAT64U_QNAN_MINUS
.s_r64PlusInf:
        dq      RTFLOAT64U_INF_PLUS
.s_r64MinusInf:
        dq      RTFLOAT64U_INF_MINUS

        ;;
        ; Sub-function that checks if the exponent (st0) is an odd integer or not.
        ;
        ; @returns  eax = 1 if odd, 0 if even or not integer.
        ; @uses     eax, edx, eflags.
        ;
.is_exp_odd_integer:
        ;
        ; Save the FPU enviornment and mask all exceptions.
        ;
        fnstenv [xBP - 30h]
        mov     ax, [xBP - 30h + X86FSTENV32P.FCW]
        or      word [xBP - 30h + X86FSTENV32P.FCW], X86_FCW_MASK_ALL
        fldcw   [xBP - 30h + X86FSTENV32P.FCW]
        mov     [xBP - 30h + X86FSTENV32P.FCW], ax

        ;
        ; Convert to 64-bit integer (probably not 100% correct).
        ;
        fld     st0                         ; -> st0=exponent st1=exponent; st2=base;
        fistp   qword [xBP - 10h]
        fild    qword [xBP - 10h]           ; -> st0=int(exponent) st1=exponent; st2=base;
        fcomip  st0, st1                    ; -> st0=exponent; st1=base;
        jne     .is_exp_odd_integer__return_false ; jump if not integer.
        mov     xAX, [xBP - 10h]
%ifdef
        mov     edx, [xBP - 10h + 4]
%endif

        ;
        ; Check the lowest bit if it might be odd.
        ; This works both for positive and negative numbers.
        ;
        test    al, 1
        jz      .is_exp_odd_integer__return_false ; jump if even.

        ;
        ; If the result is negative, convert to positive.
        ;
%ifdef RT_ARCH_AMD64
        bt      rax, 63
%else
        bt      edx, 31
%endif
        jnc     .is_exp_odd_integer__positive
%ifdef RT_ARCH_AMD64
        neg     xAX
%else
        neg     edx
        neg     eax
        sbb     edx, 0
%endif
.is_exp_odd_integer__positive:

        ;
        ; Now find the most significant bit in the value so we can verify that
        ; the odd bit was part of the mantissa/fraction of the input.
        ;
        cmp     bl, 3                            ; Skip if 80-bit input, as it has a 64-bit mantissa which
        je      .is_exp_odd_integer__return_true ; makes it a 1 bit more precision than out integer reg(s).

%ifdef RT_ARCH_AMD64
        bsr     rax, rax
%else
        bsr     edx, edx
        jnz     .is_exp_odd_integer__high_dword_is_zero
        lea     eax, [edx + 20h]
        jmp     .is_exp_odd_integer__first_bit_in_eax
.is_exp_odd_integer__high_dword_is_zero:
        bsr     eax, eax
.is_exp_odd_integer__first_bit_in_eax:
%endif
        ;
        ; The limit is 53 for double precision (one implicit bit + 52 bits fraction),
        ; and 24 for single precision types.
        ;
        mov     ah, 53                      ; RTFLOAT64U_FRACTION_BITS + 1
        cmp     bl, 0
        jne     .is_exp_odd_integer__is_double_limit
        mov     ah, 24                      ; RTFLOAT32U_FRACTION_BITS + 1
.is_exp_odd_integer__is_double_limit:

        cmp     al, ah
        jae     .is_exp_odd_integer__return_false
        mov     eax, 1

        ; Return.
.is_exp_odd_integer__return_true:
        jmp     .is_exp_odd_integer__return
.is_exp_odd_integer__return_false:
        xor     eax, eax
.is_exp_odd_integer__return:
        ffreep  st0
        fldenv  [xBP - 30h]
        ret

ENDPROC   rtNoCrtMathPowCore

