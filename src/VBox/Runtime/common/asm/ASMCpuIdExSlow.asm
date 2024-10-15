; $Id: ASMCpuIdExSlow.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - ASMCpuIdExSlow().
;

;
; Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
; CPUID with EAX and ECX inputs, returning ALL output registers.
;
; @param    uOperator   x86:ebp+8   gcc:rdi      msc:rcx
; @param    uInitEBX    x86:ebp+c   gcc:rsi      msc:rdx
; @param    uInitECX    x86:ebp+10  gcc:rdx      msc:r8
; @param    uInitEDX    x86:ebp+14  gcc:rcx      msc:r9
; @param    pvEAX       x86:ebp+18  gcc:r8       msc:rbp+30h
; @param    pvEBX       x86:ebp+1c  gcc:r9       msc:rbp+38h
; @param    pvECX       x86:ebp+20  gcc:rbp+10h  msc:rbp+40h
; @param    pvEDX       x86:ebp+24  gcc:rbp+18h  msc:rbp+48h
;
; @returns  EAX
;
RT_BEGINPROC ASMCpuIdExSlow
        push    xBP
        mov     xBP, xSP
        push    xBX
%if ARCH_BITS == 32
        push    edi
%elif ARCH_BITS == 16
        push    di
        push    es
%endif

%ifdef ASM_CALL64_MSC
 %if ARCH_BITS != 64
  %error ARCH_BITS mismatch?
 %endif
        mov     eax, ecx
        mov     ebx, edx
        mov     ecx, r8d
        mov     edx, r9d
        mov     r8,  [rbp + 30h]
        mov     r9,  [rbp + 38h]
        mov     r10, [rbp + 40h]
        mov     r11, [rbp + 48h]
%elifdef ASM_CALL64_GCC
        mov     eax, edi
        mov     ebx, esi
        xchg    ecx, edx
        mov     r10, [rbp + 10h]
        mov     r11, [rbp + 18h]
%elif ARCH_BITS == 32
        mov     eax, [xBP + 08h]
        mov     ebx, [xBP + 0ch]
        mov     ecx, [xBP + 10h]
        mov     edx, [xBP + 14h]
        mov     edi, [xBP + 18h]
%elif ARCH_BITS == 16
        mov     eax, [xBP + 08h - 4]
        mov     ebx, [xBP + 0ch - 4]
        mov     ecx, [xBP + 10h - 4]
        mov     edx, [xBP + 14h - 4]
%else
 %error unsupported arch
%endif

        cpuid

%ifdef RT_ARCH_AMD64
        test    r8, r8
        jz      .store_ebx
        mov     [r8], eax
%elif ARCH_BITS == 32
        test    edi, edi
        jz      .store_ebx
        mov     [edi], eax
%else
        cmp     dword [bp + 18h - 4], 0
        je      .store_ebx
        les     di, [bp + 18h - 4]
        mov     [es:di], eax
%endif
.store_ebx:

%ifdef RT_ARCH_AMD64
        test    r9, r9
        jz      .store_ecx
        mov     [r9], ebx
%elif ARCH_BITS == 32
        mov     edi, [ebp + 1ch]
        test    edi, edi
        jz      .store_ecx
        mov     [edi], ebx
%else
        cmp     dword [bp + 1ch - 4], 0
        je      .store_ecx
        les     di, [bp + 1ch - 4]
        mov     [es:di], ebx
%endif
.store_ecx:

%ifdef RT_ARCH_AMD64
        test    r10, r10
        jz      .store_edx
        mov     [r10], ecx
%elif ARCH_BITS == 32
        mov     edi, [ebp + 20h]
        test    edi, edi
        jz      .store_edx
        mov     [edi], ecx
%else
        cmp     dword [bp + 20h - 4], 0
        je      .store_edx
        les     di, [bp + 20h - 4]
        mov     [es:di], ecx
%endif
.store_edx:

%ifdef RT_ARCH_AMD64
        test    r11, r11
        jz      .done
        mov     [r11], edx
%elif ARCH_BITS == 32
        mov     edi, [ebp + 24h]
        test    edi, edi
        jz      .done
        mov     [edi], edx
%else
        cmp     dword [bp + 24h - 4], 0
        je      .done
        les     di, [bp + 24h - 4]
        mov     [es:di], edx
%endif
.done:

%if ARCH_BITS == 32
        pop     edi
%elif ARCH_BITS == 16
        pop     es
        pop     di
%endif
        pop     xBX
        leave
        ret
ENDPROC ASMCpuIdExSlow

