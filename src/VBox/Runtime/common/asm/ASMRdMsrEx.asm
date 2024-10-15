; $Id: ASMRdMsrEx.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - ASMRdMsrEx().
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
; @param    uEdi    msc=rdx, gcc=rsi, x86=[ebp+12]  The EDI/RDI value.
; @returns  MSR value in rax on amd64 and edx:eax on x86.
;
RT_BEGINPROC ASMRdMsrEx
%ifdef ASM_CALL64_MSC
proc_frame ASMRdMsrEx_DupWarningHack
        push    rdi
        [pushreg rdi]
[endprolog]
        and     ecx, ecx                ; serious paranoia
        mov     rdi, rdx
        xor     eax, eax
        xor     edx, edx
        rdmsr
        pop     rdi
        and     eax, eax                ; paranoia
        shl     rdx, 32
        or      rax, rdx
        ret
endproc_frame
%elifdef ASM_CALL64_GCC
        mov     ecx, edi
        mov     rdi, rsi
        xor     eax, eax
        xor     edx, edx
        rdmsr
        and     eax, eax                ; paranoia
        shl     rdx, 32
        or      rax, rdx
        ret
%elifdef RT_ARCH_X86
        push    ebp
        mov     ebp, esp
        push    edi
        xor     eax, eax
        xor     edx, edx
        mov     ecx, [ebp + 8]
        mov     edi, [ebp + 12]
        rdmsr
        pop     edi
        leave
        ret
%else
 %error "Undefined arch?"
%endif
ENDPROC ASMRdMsrEx

