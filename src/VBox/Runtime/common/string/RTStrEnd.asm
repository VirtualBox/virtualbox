; $Id: RTStrEnd.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - RTStrEnd - AMD64 & X86.
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
        mov     ecx, edx
        jecxz   .not_found_early
        mov     edx, edi                ; save rdi
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
        mov     edi, edx
%endif
        ret

.not_found:
%ifdef ASM_CALL64_MSC
        mov     rdi, r9
%endif
%ifdef RT_ARCH_X86
        mov     edi, edx
%endif
.not_found_early:
        xor     eax, eax
        ret
ENDPROC RTStrEnd

