; $Id: exp.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT exp - AMD64 & X86.
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
; Compute the e (2.7182818...) to the power of rd.
; @returns st(0) / xmm0
; @param    rd      [xSP + xCB*2] / xmm0
RT_NOCRT_BEGINPROC exp
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
        ;
        ; Convert to power of 2 and it'll be the same as exp2.
        ;
        fldl2e                              ; -> st0=log2(e); st1=input
        fmulp                               ; -> st0=input*log2(e)

        ;
        ; Split the job in two on the fraction and integer input parts.
        ;
        fld     st0                         ; Push a copy of the input on the stack.
        frndint                             ; st0 = (int)(input*log2(e))
        fsub    st1, st0                    ; st1 = input*log2(e) - (int)input*log2(e); i.e. st1 = fraction, st0 = integer.
        fxch                                ; st0 = fraction, st1 = integer.

        ; 1. Calculate on the fraction.
        f2xm1                               ; st0 = 2**fraction - 1.0
        fld1
        faddp                               ; st0 = 2**fraction

        ; 2. Apply the integer power of two.
        fscale                              ; st0 = result; st1 = integer part of input.
        fstp    st1                         ; st0 = result; no st1.

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
        ; +/-0.0: Return +1.0
        ;
.zero:
        ffreep  st0
        fld1
        jmp     .return_val

        ;
        ; -Inf: Return +0.0.
        ; +Inf: Return +Inf.  Join path with NaN.
        ;
.inf:
        test    cx, X86_FSW_C1              ; sign bit
        jz      .nan
        ffreep  st0
        fldz
        jmp     .return_val

        ;
        ; NaN: Return the input NaN value as is, if we can.
        ;
.nan:
%ifdef RT_ARCH_AMD64
        ffreep  st0
%endif
        jmp     .return
ENDPROC   RT_NOCRT(exp)

