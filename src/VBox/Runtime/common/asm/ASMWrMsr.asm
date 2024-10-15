; $Id: ASMWrMsr.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - ASMWrMsr().
;

;
; Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%include "iprt/asmdefs.mac"

BEGINCODE

;;
; Special version of ASMRdMsr that allow specifying the rdi value.
;
; @param    uMsr    msc=rcx, gcc=rdi, x86=[ebp+8]   The MSR to read.
; @param    uValue  msc=rdx, gcc=rsi, x86=[ebp+12]  The 64-bit value to write.
;
RT_BEGINPROC ASMWrMsr
%ifdef ASM_CALL64_MSC
        mov     rdi, rdx
        mov     eax, edx
        shr     rdx, 32
        wrmsr
        ret

%elifdef ASM_CALL64_GCC
        mov     ecx, edi
        mov     eax, esi
        mov     rdx, rsi
        shr     edx, 32
        wrmsr
        ret

%elif ARCH_BITS == 32
        push    ebp
        mov     ebp, esp
        push    edi
        mov     ecx, [ebp + 8]
        mov     eax, [ebp + 12]
        mov     edx, [ebp + 16]
        wrmsr
        pop     edi
        leave
        ret

%elif ARCH_BITS == 16
        push    bp
        mov     bp, sp
        push    eax
        push    ecx
        push    edx

        mov     ecx, [bp + 04h]
        mov     eax, [bp + 08h]
        mov     edx, [bp + 0ch]
        wrmsr

        pop     edx
        pop     ecx
        pop     eax
        leave
        ret
%else
 %error "Undefined arch?"
%endif
ENDPROC ASMWrMsr

