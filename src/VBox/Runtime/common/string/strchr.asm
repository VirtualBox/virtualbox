; $Id: strchr.asm 115006 2026-08-12 23:34:22Z knut.osmundsen@oracle.com $
;; @file
; IPRT - No-CRT strchr - AMD64 & X86.
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
; @param    psz     gcc: rdi  msc: rcx  x86:[esp+4]  wcall: eax
; @param    ch      gcc: esi  msc: edx  x86:[esp+8]  wcall: edx
RT_NOCRT_BEGINPROC strchr
        cld

        ; check for ch == 0 and setup normal strchr.
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        or      dl, dl
        jz near .strlen
        mov     r9, rsi                 ; save rsi
        mov     rsi, rcx
 %else
        or      sil, sil
        jz near .strlen
        mov     edx, esi
        mov     rsi, rdi
 %endif
%else
 %ifndef ASM_CALL32_WATCOM
        mov     edx, [esp + 8]
 %endif
        or      dl, dl
        jz near .strlen
 %ifdef ASM_CALL32_WATCOM
        push    esi
        mov     esi, eax
 %else
        mov     ecx, esi                ; save esi
        mov     esi, [esp + 4]
 %endif
%endif

        ; do the search
.next:
        lodsb
        cmp     al, dl
        je      .found
        test    al, al
        jz      .not_found

        lodsb
        cmp     al, dl
        je      .found
        test    al, al
        jz      .not_found

        lodsb
        cmp     al, dl
        je      .found
        test    al, al
        jz      .not_found

        lodsb
        cmp     al, dl
        je      .found
        test    al, al
        jz      .not_found
        jmp .next
        int3

.found:
        lea     xAX, [xSI - 1]
%ifdef ASM_CALL64_MSC
        mov     rsi, r9
%endif
%ifdef RT_ARCH_X86
 %ifdef ASM_CALL32_WATCOM
        pop     esi
 %else
        mov     esi, ecx
 %endif
%endif
        ret
        int3

.not_found:
%ifdef ASM_CALL64_MSC
        mov     rsi, r9
%endif
%ifdef RT_ARCH_X86
 %ifdef ASM_CALL32_WATCOM
        pop     esi
 %else
        mov     esi, ecx
 %endif
%endif
        xor     eax, eax
        ret
        int3

;
; Special case: strchr(str, '\0');
;
align 16
.strlen:
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        mov     r9, rdi                 ; save rdi
        mov     rdi, rcx
 %endif
%else
 %ifdef ASM_CALL32_WATCOM
        push    edi
        push    ecx
        mov     edi, eax
 %else
        mov     edx, edi                ; save edi
        mov     edi, [esp + 4]
 %endif
%endif
        mov     xCX, -1
        xor     eax, eax
        repne scasb

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
ENDPROC RT_NOCRT(strchr)

MARK_OBJECT_RETPOLINE_SAFE
