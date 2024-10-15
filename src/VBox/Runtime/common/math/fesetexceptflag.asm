; $Id: fesetexceptflag.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT fesetexceptflag - AMD64 & X86.
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
; Gets the pending exceptions.
;
; @returns  eax = 0 on success, non-zero on failure.
; @param    pfXcpts     32-bit: [xBP+8]; msc64: rcx; gcc64: rdi; -- pointer to fexcept_t (16-bit)
; @param    fXcptMask   32-bit: [xBP+c]; msc64: edx; gcc64: esi; -- X86_MXCSR_XCPT_FLAGS (X86_FSW_XCPT_MASK)
;                       Accepts X86_FSW_SF.
;
RT_NOCRT_BEGINPROC fesetexceptflag
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        sub     xSP, 10h
        SEH64_ALLOCATE_STACK 20h
        SEH64_END_PROLOGUE

        ;
        ; Load the parameter into ecx (*pfXcpts) and edx (fXcptMask) and validate the latter.
        ;
%ifdef ASM_CALL64_GCC
        movzx   ecx, word [rdi]
        mov     edx, esi
%elifdef ASM_CALL64_MSC
        movzx   ecx, word [rcx]
%elifdef RT_ARCH_X86
        mov     ecx, [xBP + xCB*2]
        movzx   ecx, word [ecx]
        mov     edx, [xBP + xCB*3]
%endif
%if 0
        and     ecx, X86_FSW_XCPT_MASK
        and     edx, X86_FSW_XCPT_MASK
%else
        or      eax, -1
        test    edx, ~X86_FSW_XCPT_MASK
        jnz     .return
        test    ecx, ~X86_FSW_XCPT_MASK
        jnz     .return
%endif

        ;
        ; Apply the AND mask to ECX and invert it so we can use it to clear flags
        ; before OR'ing in the new values.
        ;
        and     ecx, edx
        not     edx

        ;
        ; Make the modifications
        ;

        ; Modify the pending x87 exceptions (FSW).
        fnstenv [xBP - 20h]
        and     [xBP - 20h + X86FSTENV32P.FSW], dx
        or      [xBP - 20h + X86FSTENV32P.FSW], cx
        fldenv  [xSP - 20h]

%ifdef RT_ARCH_X86
        ; SSE supported (ecx preserved)?
        extern  NAME(rtNoCrtHasSse)
        call    NAME(rtNoCrtHasSse)
        test    al, al
        jz      .return_ok
%endif

        ; Modify the pending SSE exceptions (same bit positions as in FSW).
        stmxcsr [xBP - 10h]
        mov     eax, [xBP - 10h]
        or      edx, X86_FSW_XCPT_MASK & ~X86_MXCSR_XCPT_FLAGS      ; Don't mix X86_FSW_SF with X86_MXCSR_DAZ.
        and     ecx, X86_MXCSR_XCPT_FLAGS                           ; Ditto
        and     eax, edx
        or      eax, ecx
        mov     [xBP - 10h], eax
        ldmxcsr [xBP - 10h]

.return_ok:
        xor     eax, eax
.return:
        leave
        ret
ENDPROC   RT_NOCRT(fesetexceptflag)

