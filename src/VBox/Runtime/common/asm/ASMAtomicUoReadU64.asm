; $Id: ASMAtomicUoReadU64.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - ASMAtomicUoReadU64().
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
%include "iprt/asmdefs.mac"

BEGINCODE

;;
; Atomically reads 64-bit value.
;
; @param   pu64     x86:ebp+8
;
; @returns The current value. (x86:eax+edx)
;
;
RT_BEGINPROC ASMAtomicUoReadU64
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        mov     rax, [rcx]
 %else
        mov     rax, [rdi]
 %endif
        ret
%endif
%ifdef RT_ARCH_X86
        push    ebp
        mov     ebp, esp
        push    ebx
        push    edi

        xor     eax, eax
        xor     edx, edx
        mov     edi, [ebp+08h]
        xor     ecx, ecx
        xor     ebx, ebx
        lock cmpxchg8b [edi]

        pop     edi
        pop     ebx
        leave
        ret
%endif
ENDPROC ASMAtomicUoReadU64

