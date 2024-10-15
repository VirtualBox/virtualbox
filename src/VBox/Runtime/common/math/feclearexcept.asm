; $Id: feclearexcept.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT feclearexcept - AMD64 & X86.
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
; Sets the hardware rounding mode.
;
; @returns  eax = 0 on success, non-zero on failure.
; @param    fXcpts  32-bit: [xBP+8]; msc64: ecx; gcc64: edi; -- Zero or more bits from X86_FSW_XCPT_MASK
;
RT_NOCRT_BEGINPROC feclearexcept
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        sub     xSP, 20h
        SEH64_ALLOCATE_STACK 20h
        SEH64_END_PROLOGUE

        ;
        ; Load the parameter into ecx, validate and adjust it.
        ;
%ifdef ASM_CALL64_GCC
        mov     ecx, edi
%elifdef RT_ARCH_X86
        mov     ecx, [xBP + xCB*2]
%endif
%if 0
        and     ecx, X86_FSW_XCPT_MASK
%else
        or      eax, -1
        test    ecx, ~X86_FSW_XCPT_MASK
        jnz     .return
%endif

        ; #IE implies #SF
        mov     al, cl
        and     al, X86_FSW_IE
        shl     al, X86_FSW_SF_BIT - X86_FSW_IE_BIT
        or      cl, al

        ; Make it into and AND mask suitable for clearing the specified exceptions.
        not     ecx

        ;
        ; Make the changes.
        ;

        ; Modify the x87 flags first (ecx preserved).
        cmp     ecx, ~X86_FSW_XCPT_MASK     ; This includes all the x87 exceptions, including stack error.
        jne    .partial_mask
        fnclex
        jmp     .do_sse

.partial_mask:
        fnstenv [xBP - 20h]
        and     word [xBP - 20h + 4], cx    ; The FCW is at offset 4 in the 32-bit prot mode layout
        fldenv  [xBP - 20h]                 ; Recalculates the FSW.ES flag.
.do_sse:

%ifdef RT_ARCH_X86
        ; SSE supported (ecx preserved)?
        extern  NAME(rtNoCrtHasSse)
        call    NAME(rtNoCrtHasSse)
        test    al, al
        jz      .return_ok
%endif

        ; Modify the SSE flags (modifies ecx).
        stmxcsr [xBP - 10h]
        or      ecx, X86_FSW_XCPT_MASK & ~X86_MXCSR_XCPT_FLAGS      ; Don't mix X86_FSW_SF with X86_MXCSR_DAZ.
        and     [xBP - 10h], ecx
        ldmxcsr [xBP - 10h]

.return_ok:
        xor     eax, eax
.return:
        leave
        ret
ENDPROC   RT_NOCRT(feclearexcept)

