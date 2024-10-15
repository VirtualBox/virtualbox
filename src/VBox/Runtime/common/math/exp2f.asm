; $Id: exp2f.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT exp2f - AMD64 & X86.
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
; Calculate two to the power of @a r32.
;
; @returns st(0) / xmm0
; @param    r32     [rbp + 8] / xmm0
RT_NOCRT_BEGINPROC exp2f
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
%ifdef RT_ARCH_AMD64
        sub     xSP, 10h
        SEH64_ALLOCATE_STACK 10h
%endif
        SEH64_END_PROLOGUE

        ;
        ; Load the value into st(0).
        ;
%ifdef RT_ARCH_AMD64
        movss   [xSP], xmm0
        fld     dword [xSP]
%else
        fld     dword [xBP + xCB*2]
%endif

        ;
        ; Return immediately if NaN or infinity.
        ;
        fxam
        fstsw   ax
        test    ax, X86_FSW_C0          ; C0 is set for NaN, Infinity and Empty register. The latter is not the case.
        jz      .input_ok
%ifdef RT_ARCH_AMD64
        ffreep  st0                     ; return the xmm0 register value unchanged, as FLD changes SNaN to QNaN.
%endif
        test    ax, X86_FSW_C2          ; C2 is clear for NaN (and Empty) but set for Infinity.
        jz      .return_val2
        test    ax, X86_FSW_C1          ; C1 = sign bit
        jz      .return_val2            ; Not sign, return +Inf.
%ifndef RT_ARCH_AMD64
        ffreep  st0
%endif
        fldz                            ; Signed, so return zero as that's a good approximation for 2**-Inf.
        jmp     .return_val
.input_ok:

        ;
        ; Split the job in two on the fraction and integer input parts.
        ;
        fld     st0                     ; Push a copy of the input on the stack.
        frndint                         ; st0 = (int)input
        fsub    st1, st0                ; st1 = input - (int)input; i.e. st1 = fraction, st0 = integer.
        fxch                            ; st0 = fraction, st1 = integer.

        ; 1. Calculate on the fraction.
        f2xm1                           ; st0 = 2**fraction - 1.0
        fld1
        faddp                           ; st0 = 2**fraction

        ; 2. Apply the integer power of two.
        fscale                          ; st0 = result; st1 = integer part of input.
        fstp    st1                     ; st0 = result; no st1.

.return_val:
%ifdef RT_ARCH_AMD64
        fstp    dword [xSP]
        movss   xmm0, [xSP]
%endif
.return_val2:
        leave
        ret
ENDPROC   RT_NOCRT(exp2f)

