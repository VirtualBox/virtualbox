; $Id: ASMMultU32ByU32DivByU32.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - Assembly Functions, ASMMultU32ByU32DivByU32.
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


;;
; Multiple a 32-bit by a 32-bit integer and divide the result by a 32-bit integer
; using a 64 bit intermediate result.
;
; @returns (u32A * u32B) / u32C.
; @param   u32A/ecx/edi     The 32-bit value (A).
; @param   u32B/edx/esi     The 32-bit value to multiple by A.
; @param   u32C/r8d/edx     The 32-bit value to divide A*B by.
;
; @cproto  DECLASM(uint32_t) ASMMultU32ByU32DivByU32(uint32_t u32A, uint32_t u32B, uint32_t u32C);
;
RT_BEGINPROC ASMMultU32ByU32DivByU32
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        mov     eax, ecx
        mul     edx
        div     r8d
 %else
        mov     eax, edi
        mov     ecx, edx
        mul     esi
        div     ecx
 %endif

%elifdef RT_ARCH_X86
        mov     eax, [esp + 04h]
        mul     dword [esp + 08h]
        div     dword [esp + 0ch]
%else
 %error "Unsupported arch."
 unsupported arch
%endif

        ret
ENDPROC ASMMultU32ByU32DivByU32
