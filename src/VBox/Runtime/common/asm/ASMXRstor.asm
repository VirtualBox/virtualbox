; $Id: ASMXRstor.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - ASMXRstor().
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

;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%define RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"

BEGINCODE

;;
; Loads extended CPU state.
; @param    pXStateArea Pointer to the XRSTOR state area.
;                       msc=rcx, gcc=rdi, x86=[esp+4]
; @param    fMask       The 64-bit state component mask.
;                       msc=rdx, gcc=rsi, x86=[esp+8]
;
RT_BEGINPROC ASMXRstor
SEH64_END_PROLOGUE
%ifdef ASM_CALL64_MSC
        mov     eax, edx
        shr     rdx, 32
        xrstor  [rcx]
%elifdef ASM_CALL64_GCC
        mov     rdx, rsi
        shr     rdx, 32
        mov     eax, esi
        xrstor  [rdi]
%elifdef RT_ARCH_X86
        mov     ecx, [esp + 4]
        mov     eax, [esp + 8]
        mov     edx, [esp + 12]
        xrstor  [ecx]
%else
 %error "Undefined arch?"
%endif
        ret
ENDPROC ASMXRstor

