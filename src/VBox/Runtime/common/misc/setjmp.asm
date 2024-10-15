; $Id: setjmp.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT setjmp & longjmp - AMD64 & X86.
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
; @param x86:[esp+4] msc:rcx gcc:rdi     The jump buffer pointer.
RT_NOCRT_BEGINPROC setjmp
%ifdef RT_ARCH_AMD64
 %ifndef ASM_CALL64_MSC
        mov     rcx, rdi
 %endif
        mov     rax, [rsp]
        mov     [rcx +  0h*8], rax      ; 0 - rip
        lea     rdx, [rsp + 8]
        mov     [rcx +  1h*8], rdx      ; 1 - rsp
        mov     [rcx +  2h*8], rbp
        mov     [rcx +  3h*8], r15
        mov     [rcx +  4h*8], r14
        mov     [rcx +  5h*8], r13
        mov     [rcx +  6h*8], r12
        mov     [rcx +  7h*8], rbx
 %ifdef ASM_CALL64_MSC
        mov     [rcx +  8h*8], rsi
        mov     [rcx +  9h*8], rdi
        movdqa  [rcx + 0ah*8], xmm6
        movdqa  [rcx + 0ch*8], xmm7
        movdqa  [rcx + 0eh*8], xmm8
        movdqa  [rcx + 10h*8], xmm9
        movdqa  [rcx + 12h*8], xmm10
        movdqa  [rcx + 14h*8], xmm11
        movdqa  [rcx + 16h*8], xmm12
        movdqa  [rcx + 18h*8], xmm13
        movdqa  [rcx + 1ah*8], xmm14
        movdqa  [rcx + 1ch*8], xmm15
  %ifndef RT_OS_WINDOWS
   %error "Fix setjmp.h"
  %endif
 %endif
%else
        mov     edx, [esp + 4h]
        mov     eax, [esp]
        mov     [edx + 0h*4], eax       ; eip
        lea     ecx, [esp + 4h]
        mov     [edx + 1h*4], ecx       ; esp
        mov     [edx + 2h*4], ebp
        mov     [edx + 3h*4], ebx
        mov     [edx + 4h*4], edi
        mov     [edx + 5h*4], esi
%endif
        xor     eax, eax
        ret
ENDPROC RT_NOCRT(setjmp)


;;
; @param x86:[esp+4] msc:rcx gcc:rdi     The jump buffer pointer.
; @param x86:[esp+8] msc:rdx gcc:rsi     Return value.
RT_NOCRT_BEGINPROC longjmp
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        mov     eax, edx                ; ret
 %else
        mov     rcx, rdi                ; jmp_buf
        mov     eax, esi                ; ret
 %endif
        mov     rbp,   [rcx +  2h*8]
        mov     r15,   [rcx +  3h*8]
        mov     r14,   [rcx +  4h*8]
        mov     r13,   [rcx +  5h*8]
        mov     r12,   [rcx +  6h*8]
        mov     rbx,   [rcx +  7h*8]
 %ifdef ASM_CALL64_MSC
        mov     rsi,   [rcx +  8h*8]
        mov     rdi,   [rcx +  9h*8]
        movdqa  xmm6,  [rcx + 0ah*8]
        movdqa  xmm7,  [rcx + 0ch*8]
        movdqa  xmm8,  [rcx + 0eh*8]
        movdqa  xmm9,  [rcx + 10h*8]
        movdqa  xmm10, [rcx + 12h*8]
        movdqa  xmm11, [rcx + 14h*8]
        movdqa  xmm12, [rcx + 16h*8]
        movdqa  xmm13, [rcx + 18h*8]
        movdqa  xmm14, [rcx + 1ah*8]
        movdqa  xmm15, [rcx + 1ch*8]
  %ifndef RT_OS_WINDOWS
   %error "Fix setjmp.h"
  %endif
 %endif
        test    eax, eax
        jnz     .fine
        inc     al
.fine:
        mov     rsp,   [rcx +  1h*8]
        jmp     qword  [rcx +  0h*8]
%else
        mov     edx, [esp + 4h]         ; jmp_buf
        mov     eax, [esp + 8h]         ; ret
        mov     esi, [edx + 5h*4]
        mov     edi, [edx + 4h*4]
        mov     ebx, [edx + 3h*4]
        mov     ebp, [edx + 2h*4]
        test    eax, eax
        jnz     .fine
        inc     al
.fine:
        mov     esp, [edx + 1h*4]
        jmp     dword [edx+ 0h*4]
%endif
ENDPROC RT_NOCRT(longjmp)

