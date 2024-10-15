; $Id: memset.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT memset - AMD64 & X86.
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
; @param    pvDst   gcc: rdi  msc: ecx  x86:[esp+4]    wcall: eax
; @param    ch      gcc: esi  msc: edx  x86:[esp+8]    wcall: edx
; @param    cb      gcc: rdx  msc: r8   x86:[esp+0ch]  wcall: ebx
RT_NOCRT_BEGINPROC memset
        cld
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        mov     r9, rdi                 ; save rdi in r9
        mov     rdi, rcx
        mov     r10, rcx                ; the return value.
        movzx   eax, dl
        cmp     r8, 32
        jb      .dobytes

        ; eax = (al << 24) | (al << 16) | (al << 8) | al;
        ; rdx = (eax << 32) | eax
        movzx   edx, dl
        mov     rax, qword 0101010101010101h
        imul    rax, rdx

        ; todo: alignment.
        mov     rcx, r8
        shr     rcx, 3
        rep stosq

        and     r8, 7
.dobytes:
        mov     rcx, r8
        rep stosb

        mov     rdi, r9                 ; restore rdi
        mov     rax, r10

 %else ; GCC
        mov     r10, rdi                ; the return value.
        movzx   eax, sil
        cmp     rdx, 32
        jb      .dobytes

        ; eax = (al << 24) | (al << 16) | (al << 8) | al;
        ; rdx = (eax << 32) | eax
        movzx   esi, sil
        mov     rax, qword 0101010101010101h
        imul    rax, rsi

        ; todo: alignment.
        mov     rcx, rdx
        shr     rcx, 3
        rep stosq

        and     rdx, 7
.dobytes:
        mov     rcx, rdx
        rep stosb

        mov     rax, r10
 %endif ; GCC

%else ; X86
        push    edi

 %ifdef ASM_CALL32_WATCOM
        push    eax
        mov     edi, eax
        mov     ecx, ebx
        movzx   eax, dl
 %else
        mov     ecx, [esp + 0ch + 4]
        movzx   eax, byte [esp + 08h + 4]
        mov     edi, [esp + 04h + 4]
 %endif
        cmp     ecx, 12
        jb      .dobytes

        ; eax = (al << 24) | (al << 16) | (al << 8) | al;
        mov     ah, al
        mov     edx, eax
        shl     edx, 16
        or      eax, edx

        mov     edx, ecx
        shr     ecx, 2
        rep stosd

        and     edx, 3
        mov     ecx, edx
.dobytes:
        rep stosb

 %ifdef ASM_CALL32_WATCOM
        pop     eax
        pop     edi
 %else
        pop     edi
        mov     eax, [esp + 4]
 %endif
%endif ; X86
        ret
ENDPROC RT_NOCRT(memset)

