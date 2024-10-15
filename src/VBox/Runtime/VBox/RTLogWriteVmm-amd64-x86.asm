; $Id: RTLogWriteVmm-amd64-x86.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - RTLogWriteVmm - AMD64 & X86 for VBox.
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
%define  RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"
%include "VBox/vmm/cpuidcall.mac"

BEGINCODE

;;
; CPUID with EAX and ECX inputs, returning ALL output registers.
;
; @param    pch         x86:ebp+8   gcc:rdi      msc:rcx
; @param    cch         x86:ebp+c   gcc:esi      msc:edx
; @param    fRelease    x86:ebp+10  gcc:edx      msc:r8d
;
; @returns  EAX
;
RT_BEGINPROC RTLogWriteVmm
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        push    xBX
        SEH64_PUSH_GREG xBX
%ifndef ASM_CALL64_GCC
        push    xSI
        SEH64_PUSH_GREG xSI
%endif
        SEH64_END_PROLOGUE

%ifdef ASM_CALL64_MSC
 %if ARCH_BITS != 64
  %error ARCH_BITS mismatch?
 %endif
        mov     rsi, rcx                    ; pch
        mov     ebx, r8d                    ; fRelease; cch is the right register already.
%elifdef ASM_CALL64_GCC
        mov     ebx, edx                    ; fRelease
        mov     edx, esi                    ; cch
        mov     rsi, rdi                    ; pch
%elif ARCH_BITS == 32
        mov     esi, [xBP + 08h]            ; pch
        mov     edx, [xBP + 0ch]            ; cch
        movzx   ebx, byte [xBP + 10h]       ; fRelease
%else
 %error unsupported arch
%endif
        mov     eax, VBOX_CPUID_REQ_EAX_FIXED
        mov     ecx, VBOX_CPUID_REQ_ECX_FIXED | VBOX_CPUID_FN_LOG

        cpuid

%ifndef ASM_CALL64_GCC
        pop     xSI
%endif
        pop     xBX
        leave
        ret
ENDPROC RTLogWriteVmm

