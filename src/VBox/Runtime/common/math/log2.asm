; $Id: log2.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT log2 - AMD64 & X86.
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
; Compute the log2 of rd
; @returns st(0) / xmm0
; @param    rd      [xSP + xCB*2] / xmm0
RT_NOCRT_BEGINPROC log2
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        sub     xSP, 20h
        SEH64_ALLOCATE_STACK 20h
        SEH64_END_PROLOGUE

        ;
        ; Load the input into st0.
        ;
%ifdef RT_ARCH_AMD64
        movsd   [xBP - 10h], xmm0
        fld     qword [xBP - 10h]
%else
        fld     qword [xBP + xCB*2]
%endif

        ;
        ; Weed out non-normal values.
        ;
        fxam
        fnstsw  ax
        mov     cx, ax
        and     ax, X86_FSW_C3 | X86_FSW_C2 | X86_FSW_C0
        cmp     ax, X86_FSW_C2              ; Normal finite number (excluding zero)
        je      .finite
        cmp     ax, X86_FSW_C3              ; Zero
        je      .zero
        cmp     ax, X86_FSW_C3 | X86_FSW_C2 ; Denormals
        je      .finite
        cmp     ax, X86_FSW_C0 | X86_FSW_C2 ; Infinity.
        je      .inf
        jmp     .nan

.finite:
        ; Negative number?
        test    cx, X86_FSW_C1
        jnz     .negative

        ; Is it +1.0?
        fld1
        fcomip  st1
        jz      .plus_one

        ;
        ; The fyl2xp1 instruction (ST1=ST1*log2(ST0+1.0), popping ST0) has a
        ; valid ST0 range of 1(1-sqrt(0.5)) (approx 0.29289321881) on both
        ; sides of zero.  We try use it if we can.
        ;
.above_one:
        ; For both fyl2xp1 and fyl2xp1 we need st1=1.0.
        fld1
        fxch    st0, st1                    ; -> st0=input; st1=1.0

        ; Check if the input is within the fyl2xp1 range.
        fld     qword [.s_r64AbsFyL2xP1InputMax xWrtRIP]
        fcomip  st0, st1
        jbe     .cannot_use_fyl2xp1

        fld     qword [.s_r64AbsFyL2xP1InputMin xWrtRIP]
        fcomip  st0, st1
        jae     .cannot_use_fyl2xp1

        ; Do the calculation.
.use_fyl2xp1:
        fsub    st0, st1                    ; -> st0=input-1; st1=1.0
        fyl2xp1                             ; -> st0=1.0*log2(st0+1.0)
        jmp     .return_val

.cannot_use_fyl2xp1:
        fyl2x                               ; -> st0=1.0*log2(st0)

        ;
        ; Return st0.
        ;
.return_val:
%ifdef RT_ARCH_AMD64
        fstp    qword [xBP - 10h]
        movsd   xmm0, [xBP - 10h]
%endif
.return:
        leave
        ret


        ;
        ; +1.0: Return +0.0.
        ;
.plus_one:
        ffreep  st0
        fldz
        jmp     .return_val

        ;
        ; Negative numbers: Return NaN and raise invalid operation.
        ;
.negative:
.minus_inf:
        ; Raise invalid operation
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

        ; Load NaN
%ifdef RT_ARCH_AMD64
        movsd   xmm0, [.s_r64NaN xWrtRIP]
%else
        fld     qword [.s_r64NaN xWrtRIP]
%endif
        jmp     .return

        ;
        ; +/-0.0: Return inf and raise divide by zero error.
        ;
.zero:
        ffreep  st0

        ; Raise div/0
%ifdef RT_ARCH_X86
        mov     dword [xSP], X86_FSW_ZE
%elifdef ASM_CALL64_GCC
        mov     edi, X86_FSW_ZE
%elifdef ASM_CALL64_MSC
        mov     ecx, X86_FSW_ZE
%else
 %error calling conv.
%endif
        call    NAME(RT_NOCRT(feraiseexcept))

        ; Load +Inf
%ifdef RT_ARCH_AMD64
        movsd   xmm0, [.s_r64MinusInf xWrtRIP]
%else
        fld     qword [.s_r64MinusInf xWrtRIP]
%endif
        jmp     .return

        ;
        ; -Inf: Same as other negative numbers
        ; +Inf: return +Inf.  Join path with NaN.
        ;
.inf:
        test    cx, X86_FSW_C1              ; sign bit
        jnz     .minus_inf

        ;
        ; NaN: Return the input NaN value as is, if we can.
        ;
.nan:
%ifdef RT_ARCH_AMD64
        ffreep  st0
%endif
        jmp     .return

ALIGNCODE(8)
        ;; The fyl2xp1 instruction only works between +/-1(1-sqrt(0.5)).
        ; These two variables is that range + 1.0, so we can compare directly
        ; with the input w/o any extra fsub and fabs work.
.s_r64AbsFyL2xP1InputMin:
        dq      0.708 ; -0.292 + 1.0
.s_r64AbsFyL2xP1InputMax:
        dq      1.292
;.s_r64AbsFyL2xP1Range:
;        dq      0.292
.s_r64MinusInf:
        dq      RTFLOAT64U_INF_MINUS
.s_r64NaN:
        dq      RTFLOAT64U_QNAN_MINUS
ENDPROC   RT_NOCRT(log2)

