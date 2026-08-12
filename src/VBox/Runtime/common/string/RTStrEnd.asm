; $Id: RTStrEnd.asm 115006 2026-08-12 23:34:22Z knut.osmundsen@oracle.com $
;; @file
; IPRT - RTStrEnd - AMD64 & X86.
;

;
; Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

%include "iprt/asmdefs.mac"

BEGINCODE

;;
; @param    pszString   gcc: rdi  msc: rcx  x86:[esp+4]   wcall: eax
; @param    cchMax      gcc: rsi  msc: rdx  x86:[esp+8]   wcall: edx
;
RT_BEGINPROC RTStrEnd
        cld
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        or      rdx, rdx
        jz      .not_found_early

        mov     r9, rdi                 ; save rdi
        mov     rdi, rcx
        mov     rcx, rdx
 %else
        mov     rcx, rsi
        jrcxz   .not_found_early
 %endif

%else
 %ifdef ASM_CALL32_WATCOM
        push    edi                     ; (watcall preserves all register)
        push    ecx
        mov     ecx, edx
        jecxz   .not_found_early
        mov     edi, eax
 %else
        mov     ecx, [esp + 8]
        jecxz   .not_found_early
        mov     edx, edi                ; save edi
        mov     edi, [esp + 4]
 %endif
%endif
        xor     eax, eax                ; we're searching for zero

        ; do the search
        repne   scasb
        jne     .not_found

        ; found it
        lea     xAX, [xDI - 1]
%ifdef ASM_CALL64_MSC
        mov     rdi, r9
%endif
%ifdef RT_ARCH_X86
 %ifdef ASM_CALL32_WATCOM
        pop     ecx
        pop     edi
 %else
        mov     edi, edx
 %endif
%endif
        ret
        int3

.not_found:
%ifdef ASM_CALL64_MSC
        mov     rdi, r9
%endif
%ifdef RT_ARCH_X86
 %ifndef ASM_CALL32_WATCOM
        mov     edi, edx
 %endif
%endif
.not_found_early:
%ifdef RT_ARCH_X86
 %ifdef ASM_CALL32_WATCOM
        pop     ecx
        pop     edi
 %endif
%endif
        xor     eax, eax
        ret
ENDPROC RTStrEnd

MARK_OBJECT_RETPOLINE_SAFE
