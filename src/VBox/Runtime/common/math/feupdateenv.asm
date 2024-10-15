; $Id: feupdateenv.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT feupdateenv - AMD64 & X86.
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
extern  NAME(RT_NOCRT(fesetenv))
extern  NAME(RT_NOCRT(feraiseexcept))


;;
; Updates the FPU+SSE environment.
;
; This will restore @a pEnv and merge in pending exception flags.
;
; @returns  eax = 0 on success, -1 on failure.
; @param    pEnv    32-bit: [xBP+8]     msc64: rcx      gcc64: rdi - Saved environment.
;
RT_NOCRT_BEGINPROC feupdateenv
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        sub     xSP, 30h
        SEH64_ALLOCATE_STACK 30h
        SEH64_END_PROLOGUE

        ;
        ; Load the parameter into rcx.
        ;
%ifdef ASM_CALL64_GCC
        mov     rcx, rdi
%elifdef RT_ARCH_X86
        mov     ecx, [xBP + xCB*2]
%endif

        ;
        ; Save the pending exceptions.
        ;
%ifdef RT_ARCH_X86
        extern  NAME(rtNoCrtHasSse)
        call    NAME(rtNoCrtHasSse)     ; Preserves all except xAX.
        xor     edx, edx
        test    al, al
        jz      .no_sse
%endif
        stmxcsr [xBP - 10h]
        mov     edx, [xBP - 10h]
        and     edx, X86_MXCSR_XCPT_FLAGS
.no_sse:
        fnstsw  ax
        or      edx, eax
        mov     [xBP - 8h], edx        ; save the pending exceptions here (will apply X86_FSW_XCPT_MASK later).

        ;
        ; Call fesetenv to update the environment.
        ; Note! We have not yet modified the parameter registers for calling
        ;       convensions using them.  So, parameters only needs to be loaded
        ;       for the stacked based convention.
        ;
%ifdef RT_ARCH_X86
        mov     [xSP], ecx
%endif
        call    NAME(RT_NOCRT(fesetenv))

        ;
        ; Raise exceptions if any are pending.
        ;
%ifdef ASM_CALL64_GCC
        mov     edi, [xBP - 8h]
        and     edi, X86_FSW_XCPT_MASK
%elifdef ASM_CALL64_MSC
        mov     ecx, [xBP - 8h]
        and     ecx, X86_FSW_XCPT_MASK
%else
        mov     ecx, [xBP - 8h]
        and     ecx, X86_FSW_XCPT_MASK
        mov     [xSP], ecx
%endif
        jz      .no_exceptions_to_raise
        call    NAME(RT_NOCRT(feraiseexcept))
.no_exceptions_to_raise:

        ;
        ; Return success.
        ;
        xor     eax, eax
        leave
        ret
ENDPROC   RT_NOCRT(feupdateenv)

