; $Id: bzero.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT bzero - AMD64 & X86.
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
; @param    pvDst   gcc: rdi  msc: rcx  x86:[esp+4]  wcall:eax
; @param    cb      gcc: rsi  msc: rdx  x86:[esp+8]  wcall:edx
RT_NOCRT_BEGINPROC bzero
%ifdef RT_OS_DARWIN
GLOBALNAME __bzero
%endif
        cld
%ifdef RT_ARCH_AMD64
        xor     eax, eax
 %ifdef ASM_CALL64_MSC
        mov     r9, rdi                 ; save rdi in r9
        mov     rdi, rcx

        ; todo: alignment?
        mov     rcx, rdx
        shr     rcx, 3
        rep stosq

        and     rdx, 7
        mov     rcx, rdx
        rep stosb

        mov     rdi, r9                 ; restore rdi

 %else ; GCC
        ; todo: alignment?
        mov     rcx, rsi
        shr     rcx, 3
        rep stosq

        and     rsi, 7
        mov     rcx, rsi
        rep stosb

 %endif ; GCC

%elif ARCH_BITS == 32
        push    ebp
        mov     ebp, esp
        push    edi

 %ifdef ASM_CALL32_WATCOM
        mov     ecx, edx
        mov     edi, eax
 %else
        mov     ecx, [ebp + 0ch]
        mov     edi, [ebp + 08h]
 %endif
        xor     eax, eax

        mov     edx, ecx
        shr     ecx, 2
        rep stosd

        and     edx, 3
        mov     ecx, edx
        rep stosb

        pop     edi
        leave

%elif ARCH_BITS == 16
        push    bp
        mov     bp, sp
        push    di

        mov     cx, [bp + 0ch]
        mov     di, [bp + 08h]
        xor     ax, ax

        ; align di.
        test    di, 1
        jz      .aligned
        jcxz    .done
        stosb
        dec     cx
        jz      .done

.aligned:
        mov     dx, cx
        shr     cx, 1
        rep stosw

        test    dl, 1
        jz      .done
        stosb

.done:
        pop     di
        pop     bp
%else
 %error ARCH_BITS
%endif ; X86
        ret
ENDPROC RT_NOCRT(bzero)

