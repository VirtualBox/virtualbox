; $Id: memmove.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT memmove - AMD64 & X86.
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
; @param    pvDst   gcc: rdi  msc: rcx  x86:[esp+4]    wcall: eax
; @param    pvSrc   gcc: rsi  msc: rdx  x86:[esp+8]    wcall: edx
; @param    cb      gcc: rdx  msc: r8   x86:[esp+0ch]  wcall: ebx
RT_NOCRT_BEGINPROC memmove
        ; Prolog.
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        mov     r10, rdi                ; save
        mov     r11, rsi                ; save
        mov     rdi, rcx
        mov     rsi, rdx
        mov     rcx, r8
        mov     rdx, r8
 %else
        mov     rcx, rdx
 %endif
        mov     rax, rdi                ; save the return value
%else
        push    edi
        push    esi
 %ifdef ASM_CALL32_WATCOM
        mov     edi, eax
        mov     esi, edx
        mov     ecx, ebx
        mov     edx, ebx
 %else
        mov     edi, [esp + 04h + 8]
        mov     esi, [esp + 08h + 8]
        mov     ecx, [esp + 0ch + 8]
        mov     edx, ecx
        mov     eax, edi                ; save the return value
 %endif
%endif
        SEH64_END_PROLOGUE

        ;
        ; Decide which direction to perform the copy in.
        ;
%if 1 ; keep it simple for now.
        cmp     xDI, xSI
        jnb     .backward

        ;
        ; Slow/simple forward copy.
        ;
        cld
        rep movsb
        jmp .epilog

%else ; disabled - it seems to work, but play safe for now.
        ;sub     xAX, xSI
        ;jnb     .backward
        cmp     xDI, xSI
        jnb     .backward

        ;
        ; Fast forward copy.
        ;
.fast_forward:
        cld
%ifdef RT_ARCH_AMD64
        shr     rcx, 3
        rep movsq
%else
        shr     ecx, 2
        rep movsd
%endif

        ; The remaining bytes.
%ifdef RT_ARCH_AMD64
        test    dl, 4
        jz      .forward_dont_move_dword
        movsd
%endif
.forward_dont_move_dword:
        test    dl, 2
        jz      .forward_dont_move_word
        movsw
.forward_dont_move_word:
        test    dl, 1
        jz      .forward_dont_move_byte
        movsb
.forward_dont_move_byte:

%endif ; disabled

        ;
        ; The epilog.
        ;
.epilog:
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        mov     rdi, r10
        mov     rsi, r11
 %endif
%else
        pop     esi
        pop     edi
%endif
        ret

        ;
        ; Slow/simple backward copy.
        ;
ALIGNCODE(16)
.backward:
        ;; @todo check if they overlap.
        lea     xDI, [xDI + xCX - 1]
        lea     xSI, [xSI + xCX - 1]
        std
        rep movsb
        cld
        jmp .epilog
ENDPROC RT_NOCRT(memmove)

