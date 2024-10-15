; $Id: feenableexcept.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT feenableexcept - AMD64 & X86.
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
; Enables a set of exceptions (BSD/GNU extension).
;
; @returns  eax = Previous enabled exceptions on success (not subject to fXcpt),
;                 -1 on failure.
; @param    fXcpt   32-bit: [xBP+8]     msc64: ecx      gcc64: edi - Mask of exceptions to enable.
;
RT_NOCRT_BEGINPROC feenableexcept
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
%ifdef ASM_CALL64_GCC
        mov     ecx, edi
%elifdef RT_ARCH_X86
        mov     ecx, [xBP + xCB*2]
%endif
        or      eax, -1
        test    ecx, ~X86_FCW_XCPT_MASK
%ifndef RT_STRICT
        jnz     .return
%else
        jz      .input_ok
        int3
        jmp     .return
.input_ok:
%endif

        ; Invert the mask as we're enabling the exceptions, not masking them.
        not     ecx

        ;
        ; Make the changes (old mask in eax).
        ;

        ; Modify the x87 mask first (ecx preserved).
        fstcw   [xBP - 10h]
%ifdef RT_ARCH_X86 ; Return the inverted x87 mask in 32-bit mode.
        mov     ax, word [xBP - 10h]
        and     eax, X86_FCW_XCPT_MASK
%endif
        and     word [xBP - 10h], cx
        fldcw   [xBP - 10h]

%ifdef RT_ARCH_X86
        ; SSE supported (ecx preserved)?
        extern  NAME(rtNoCrtHasSse)
        call    NAME(rtNoCrtHasSse)
        test    al, al
        jz      .return_ok
%endif

        ; Modify the SSE mask (modifies ecx).
        stmxcsr [xBP - 10h]
%ifdef RT_ARCH_AMD64 ; Return the inverted MXCSR exception mask on AMD64 because windows doesn't necessarily set the x87 one.
        mov     eax, [xBP - 10h]
        and     eax, X86_MXCSR_XCPT_MASK
        shr     eax, X86_MXCSR_XCPT_MASK_SHIFT
%endif
        rol     ecx, X86_MXCSR_XCPT_MASK_SHIFT
        and     [xBP - 10h], ecx
        ldmxcsr [xBP - 10h]

.return_ok:
        not     eax                     ; Invert it as we return the enabled rather than masked exceptions.
.return:
        leave
        ret
ENDPROC   RT_NOCRT(feenableexcept)

