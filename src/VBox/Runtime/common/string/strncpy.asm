; $Id: strncpy.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT strncpy - AMD64 & X86.
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
; @param    pszDst   gcc: rdi  msc: rcx  x86:[esp+4]   wcall: eax
; @param    pszSrc   gcc: rsi  msc: rdx  x86:[esp+8]   wcall: edx
; @param    cbMax    gcc: rdx  msc: r8   x86:[esp+12]  wcall: ebx
RT_NOCRT_BEGINPROC strncpy
        ; input
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
  %define pszDst rcx
  %define pszSrc rdx
  %define cbMax  r8
 %else
  %define pszDst rdi
  %define pszSrc rsi
  %define cbMax  rdx
 %endif
        mov     r9, pszDst
%else
 %ifdef ASM_CALL32_WATCOM
  mov   ecx, eax
  %define pszDst ecx
  %define pszSrc edx
  %define cbMax  ebx
 %else
        mov     ecx, [esp + 4]
        mov     edx, [esp + 8]
        push    ebx
        mov     ebx, [esp + 12 + 4]
  %define pszDst ecx
  %define pszSrc edx
  %define cbMax  ebx
 %endif
        push    pszDst
%endif

        ;
        ; The rolled out loop.
        ;
.next:
        cmp     cbMax, 4
        jb      .simple_intro

        mov     al, [pszSrc]
        mov     [pszDst], al
        test    al, al
        jz      .done

        mov     al, [pszSrc + 1]
        mov     [pszDst + 1], al
        test    al, al
        jz      .done

        mov     al, [pszSrc + 2]
        mov     [pszDst + 2], al
        test    al, al
        jz      .done

        mov     al, [pszSrc + 3]
        mov     [pszDst + 3], al
        test    al, al
        jz      .done

        add     pszDst, 4
        add     pszSrc, 4
        sub     cbMax,  4
        jmp     .next

        ;
        ; Char by char.
        ;
.simple_intro:
        test    cbMax, cbMax
        jz      .done

.simple_next:
        mov     al, [pszSrc]
        mov     [pszDst], al
        test    al, al
        jz      .done

        dec     cbMax
        jz      .done

        inc     pszSrc
        inc     pszDst
        jmp     .simple_next

.done:
%ifdef RT_ARCH_AMD64
        mov     rax, r9
%else
 %ifndef ASM_CALL32_WATCOM
        pop     ebx
 %endif
        pop     eax
%endif
        ret
ENDPROC RT_NOCRT(strncpy)

