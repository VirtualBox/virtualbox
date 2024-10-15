; $Id: RTUInt128MulByU64.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - RTUInt128MulByU64 - AMD64 implementation.
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
%include "internal/bignum.mac"


BEGINCODE

;;
; Multiplies a 128-bit number with a 64-bit one.
;
; @returns puResult.
; @param    puResult       x86:[ebp +  8]  gcc:rdi  msc:rcx
; @param    puValue1       x86:[ebp + 12]  gcc:rsi  msc:rdx
; @param    uValue2        x86:[ebp + 16]  gcc:rdx  msc:r8
;
RT_BEGINPROC RTUInt128MulByU64
;        SEH64_SET_FRAME_xSP 0
SEH64_END_PROLOGUE

%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_GCC
  %define puResult      rdi
  %define puValue1      rsi
  %define uValue2       r8
        mov     r8, rdx
 %else
  %define puResult      rcx
  %define puValue1      r9
  %define uValue2       r8
        mov     r9, rdx
 %endif

        ; puValue1->s.Lo * uValue2
        mov     rax, [puValue1]
        mul     uValue2
        mov     [puResult], rax
        mov     r11, rdx                ; Store the lower half of the result.

        ; puValue1->s.Hi * uValue2
        mov     rax, [puValue1 + 8]
        mul     uValue2
        add     r11, rax                ; Calc the second half of the result.
        mov     [puResult + 8], r11     ; Store the high half of the result.

        mov     rax, puResult

;%elifdef RT_ARCH_X86
%else
 %error "unsupported arch"
%endif

        ret
ENDPROC RTUInt128MulByU64

