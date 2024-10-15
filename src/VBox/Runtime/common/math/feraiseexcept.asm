; $Id: feraiseexcept.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT feraiseexcept - AMD64 & X86.
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


%ifdef RT_ARCH_AMD64
 %define RT_NOCRT_RAISE_FPU_EXCEPT_IN_SSE_MODE
%endif


BEGINCODE

;;
; Raises the given FPU/SSE exceptions.
;
; @returns  eax = 0 on success, -1 on failure.
; @param    fXcpt   32-bit: [xBP+8]; msc64: ecx; gcc64: edi; --  The exceptions to raise.
;                   Accepts X86_FSW_XCPT_MASK, but ignores X86_FSW_DE and X86_FSW_SF.
;
RT_NOCRT_BEGINPROC feraiseexcept
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
%ifndef RT_NOCRT_RAISE_FPU_EXCEPT_IN_SSE_MODE
        sub     xBP, 20h
        SEH64_ALLOCATE_STACK 20h
%endif
        SEH64_END_PROLOGUE

        ;
        ; Load the parameter into rcx.
        ;
%ifdef ASM_CALL64_GCC
        mov     rcx, rdi
%elifdef RT_ARCH_X86
        mov     ecx, [xBP + xCB*2]
%endif
%ifdef RT_STRICT
        test    ecx, ~X86_FSW_XCPT_MASK
        jz      .input_ok
        int3
.input_ok:
%endif

        ;
        ; We have to raise these buggers one-by-one and order is said to be important.
        ; We ASSUME that x86 runs is okay with the x87 raising the exception.
        ;

        ; 1. Invalid operation.  Like +0.0 / +0.0.
        test    cl, X86_FSW_IE
        jz      .not_ie
%ifdef RT_NOCRT_RAISE_FPU_EXCEPT_IN_SSE_MODE
        movss   xmm0, [g_r32Zero xWrtRIP]
        divss   xmm0, xmm0
%else
        fnstenv [xBP - 20h]
        or      byte [xBP - 20h + X86FSTENV32P.FSW], X86_FSW_IE
        fldenv  [xBP - 20h]
        fwait
%endif
.not_ie:

        ; 2. Division by zero.
        test    cl, X86_FSW_ZE
        jz      .not_ze
%ifdef RT_NOCRT_RAISE_FPU_EXCEPT_IN_SSE_MODE
        movss   xmm0, [g_r32One  xWrtRIP]
        movss   xmm1, [g_r32Zero xWrtRIP]
        divss   xmm0, xmm1
%else
        fnstenv [xBP - 20h]
        or      byte [xBP - 20h + X86FSTENV32P.FSW], X86_FSW_ZE
        fldenv  [xBP - 20h]
        fwait
%endif
.not_ze:

        ; 3. Overflow.
        test    cl, X86_FSW_OE
        jz      .not_oe
%ifdef RT_NOCRT_RAISE_FPU_EXCEPT_IN_SSE_MODE
        xorps   xmm0, [g_r32Large xWrtRIP]
        movss   xmm1, [g_r32Tiny  xWrtRIP]
        divss   xmm0, xmm1
%else
        fnstenv [xBP - 20h]
        or      byte [xBP - 20h + X86FSTENV32P.FSW], X86_FSW_OE
        fldenv  [xBP - 20h]
        fwait
%endif
.not_oe:

        ; 4. Underflow.
        test    cl, X86_FSW_UE
        jz      .not_ue
%ifdef RT_NOCRT_RAISE_FPU_EXCEPT_IN_SSE_MODE
        xorps   xmm0, [g_r32Tiny  xWrtRIP]
        movss   xmm1, [g_r32Large xWrtRIP]
        divss   xmm0, xmm1
%else
        fnstenv [xBP - 20h]
        or      byte [xBP - 20h + X86FSTENV32P.FSW], X86_FSW_UE
        fldenv  [xBP - 20h]
        fwait
%endif
.not_ue:

        ; 5. Precision.
        test    cl, X86_FSW_PE
        jz      .not_pe
%ifdef RT_NOCRT_RAISE_FPU_EXCEPT_IN_SSE_MODE
        xorps   xmm0, [g_r32Two   xWrtRIP]
        movss   xmm1, [g_r32Three xWrtRIP]
        divss   xmm0, xmm1
%else
        fnstenv [xBP - 20h]
        or      byte [xBP - 20h + X86FSTENV32P.FSW], X86_FSW_PE
        fldenv  [xBP - 20h]
        fwait
%endif
.not_pe:

        ; We currently do not raise X86_FSW_DE or X86_FSW_SF.

        ;
        ; Return success.
        ;
        xor     eax, eax
.return:
        leave
        ret
ENDPROC   RT_NOCRT(feraiseexcept)


%ifdef RT_NOCRT_RAISE_FPU_EXCEPT_IN_SSE_MODE
g_r32Zero:
        dd      0.0
g_r32One:
        dd      1.0
g_r32Two:
        dd      2.0
g_r32Three:
        dd      3.0
g_r32Large:
        dd      1.0e+38
g_r32Tiny:
        dd      1.0e-37
%endif

