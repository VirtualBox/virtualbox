; $Id: powf.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT powf - AMD64 & X86.
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

extern NAME(rtNoCrtMathPowCore)

;;
; Compute the rfBase to the power of rfExp.
; @returns st(0) / xmm0
; @param    rfBase      [xSP + xCB*2] / xmm0
; @param    rfExp       [xSP + xCB*2 + 4] / xmm1
;
RT_NOCRT_BEGINPROC powf
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        push    xBX
        SEH64_PUSH_GREG rbx
        sub     xSP, 30h - xCB
        SEH64_ALLOCATE_STACK 30h - xCB
        SEH64_END_PROLOGUE

        ;
        ; Load rdBase into st1 and rdExp into st0.
        ;
%ifdef RT_ARCH_AMD64
        movss   [xBP - 20h], xmm0
        fld     dword [xBP - 20h]
        fxam
        fnstsw  ax
        mov     dx, ax                      ; dx=fxam(base)

        movss   [xBP - 30h], xmm1
        fld     dword [xBP - 30h]
%else
        fld     dword [xBP + xCB*2]
        fxam
        fnstsw  ax
        mov     dx, ax                      ; dx=fxam(base)

        fld     dword [xBP + xCB*2 + RTLRD_CB]
%endif

        ;
        ; Call common worker for the calculation.
        ;
        mov     ebx, 1                      ; float
        call    NAME(rtNoCrtMathPowCore)

        ;
        ; Normally, we return with eax==0 and we have to load the result
        ; from st0 and into xmm0.
        ;
        cmp     eax, 0
        jne     .return_input_reg

        fstp    dword [xSP - 30h]
        movss   xmm0, [xSP - 30h]

.return:
        lea     xSP, [xBP - xCB]
        pop     xBX
        leave
        ret

        ;
        ; But sometimes, like if we have NaN or other special inputs, we should
        ; return the input as-is and ditch the st0 value.
        ;
.return_input_reg:
        ffreep  st0
        cmp     eax, 2
        je      .return_exp
%ifdef RT_STRICT
        cmp     eax, 1
        je      .return_base
        int3
%endif
.return_base:
        jmp     .return

.return_exp:
        movss   xmm0, xmm1
        jmp     .return
ENDPROC   RT_NOCRT(powf)

