; $Id: fma-asm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT fma alternatives - AMD64 & X86.
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

BEGINCODE

;;
; Fused multiplication and add, intel version.
;
; @returns  st(0) / xmm0
; @param    rdFactor1      [rbp + 08h] / xmm0
; @param    rdFactor2      [rbp + 10h] / xmm1
; @param    rdAddend       [rbp + 18h] / xmm2
BEGINPROC rtNoCrtMathFma3
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        SEH64_END_PROLOGUE

%ifdef RT_ARCH_X86
        movsd   xmm0, qword [xBP + xCB*2 + 00h]
        movsd   xmm1, qword [xBP + xCB*2 + 08h]
        movsd   xmm2, qword [xBP + xCB*2 + 10h]
%endif

        vfmadd132sd xmm0, xmm2, xmm1    ; xmm0 = xmm0 * xmm1 + xmm2  (132 = multiply op1 with op3 and add op2)

%ifdef RT_ARCH_X86
        sub     xSP, 10h
        movsd   [xSP], xmm0
        fld     qword [xSP]
%endif
        leave
        ret
ENDPROC   rtNoCrtMathFma3


;;
; Fused multiplication and add, amd version.
;
; @returns  st(0) / xmm0
; @param    rdFactor1      [rbp + 08h] / xmm0
; @param    rdFactor2      [rbp + 10h] / xmm1
; @param    rdAddend       [rbp + 18h] / xmm2
BEGINPROC rtNoCrtMathFma4
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        SEH64_END_PROLOGUE

%ifdef RT_ARCH_X86
        movsd   xmm0, qword [xBP + xCB*2 + 00h]
        movsd   xmm1, qword [xBP + xCB*2 + 08h]
        movsd   xmm2, qword [xBP + xCB*2 + 10h]
%endif

        vfmaddsd xmm0, xmm0, xmm1, xmm2    ; xmm0 = xmm0 * xmm1 + xmm2

%ifdef RT_ARCH_X86
        sub     xSP, 10h
        movsd   [xSP], xmm0
        fld     qword [xSP]
%endif
        leave
        ret
ENDPROC   rtNoCrtMathFma4

