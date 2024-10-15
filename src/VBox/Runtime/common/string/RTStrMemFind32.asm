; $Id: RTStrMemFind32.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - RTStrMemFind32 - AMD64 & X86.
;

;
; Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
;
; This is just a 32-bit memchr.
;
; @param    pvHaystack      gcc: rdi  msc: ecx  x86:[esp+4]   wcall: eax
; @param    uNeedle         gcc: esi  msc: edx  x86:[esp+8]   wcall: edx
; @param    cbHaystack      gcc: rdx  msc: r8   x86:[esp+0ch] wcall: ebx
RT_BEGINPROC RTStrMemFind32
        cld
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        mov     r9, rdi                 ; save rdi
        mov     eax, edx
        mov     rdi, rcx
        mov     rcx, r8
 %else
        mov     rcx, rdx
        mov     eax, esi
 %endif
%else
 %ifdef ASM_CALL32_WATCOM
        mov     ecx, ebx
        xchg    eax, edx
        xchg    edi, edx                ; load and save edi.
 %else
        mov     ecx, [esp + 0ch]
        mov     edx, edi                ; save edi
        mov     eax, [esp + 8]
        mov     edi, [esp + 4]
 %endif
%endif
        cmp     xCX, 4
        jb      .not_found

        ; do the search
        repne   scasd
        jne     .not_found

        ; found it
        lea     xAX, [xDI - 4]
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
        xor     eax, eax
        ret
ENDPROC RTStrMemFind32

