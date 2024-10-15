; $Id: fesetx87precision.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT fesetx87precision - AMD64 & X86.
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
; Sets the x87 hardware precision mode - IPRT extension.
;
; @returns  eax = previous precision mode, -1 on failure.
; @param    iPrecisionMode  32-bit: [xBP+8]     msc64: ecx      gcc64: edi
;
RT_NOCRT_BEGINPROC fesetx87precision
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        sub     xSP, 10h
        SEH64_ALLOCATE_STACK 10h
        SEH64_END_PROLOGUE

        ;
        ; Load the parameter into ecx.
        ;
        or      eax, -1
%ifdef ASM_CALL64_GCC
        mov     ecx, edi
%elifdef RT_ARCH_X86
        mov     ecx, [xBP + xCB*2]
%endif
        test    ecx, ~X86_FCW_PC_MASK
        jnz     .return

        ;
        ; Extract the current from the x87 FCW.
        ;
        fnstcw  [xBP - 10h]
        mov     dx, [xBP - 10h]
        mov     ax, dx
        and     dx, ~X86_FCW_PC_MASK
        or      dx, cx
        mov     [xBP - 10h], dx
        fldcw   [xBP - 10h]

        and     eax, X86_FCW_PC_MASK
.return:
        leave
        ret
ENDPROC   RT_NOCRT(fesetx87precision)

