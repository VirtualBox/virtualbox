; $Id: ldexp.asm 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $
;; @file
; IPRT - No-CRT ldexp - AMD64 & X86.
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
; Computes rd * 2^exp
; @returns st(0) / xmm0
; @param    rd      [rbp + xCB*2] / xmm0
; @param    exp     [ebp + 10h]  gcc:edi  msc:edx
RT_NOCRT_BEGINPROC ldexp
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        sub     xSP, 10h
        SEH64_ALLOCATE_STACK 10h
        SEH64_END_PROLOGUE

        ;
        ; Load the value and scaling factor.
        ;
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_GCC
        mov     [rbp - 8h], edi
 %else
        mov     [rbp - 8h], edx
 %endif
        fild    dword [rbp - 8h]
        movsd   [rbp - 10h], xmm0
        fld     qword [rbp - 10h]
%else
        fild    dword [xBP + xCB*2 + 8]
        fld     qword [xBP + xCB*2]
%endif

        ;
        ; Do the scaling and return the result.
        ;
        fscale

        fstp    st1
%ifdef RT_ARCH_AMD64
        fstp    qword [rbp - 10h]
        movsd   xmm0, [rbp - 10h]
%endif

        leave
        ret
ENDPROC   RT_NOCRT(ldexp)

