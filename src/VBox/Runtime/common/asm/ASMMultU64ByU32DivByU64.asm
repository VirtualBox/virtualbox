; $Id$
;; @file
; IPRT - Assembly Functions, ASMMultU64ByU32DivByU64.
;

;
; Copyright (C) 2026 Oracle and/or its affiliates.
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
; Multiple a 64-bit by a 32-bit integer and divide the result by a 64-bit integer
; using a 96 bit intermediate result.
;
; @returns (u64A * u32B) / u64C.
; @param   u64A/rcx/rdi     The 64-bit value.
; @param   u32B/edx/esi     The 32-bit value to multiple by A.
; @param   u64C/r8/rdx      The 64-bit value to divide A*B by.
;
; @cproto  DECLASM(uint64_t) ASMMultU64ByU32DivByU64(uint64_t u64A, uint32_t u32B, uint64_t u64C);
;
RT_BEGINPROC ASMMultU64ByU32DivByU64
%if ARCH_BITS == 64
 %ifdef ASM_CALL64_MSC
    mov     rax, rcx                    ; rax = u64A
    mov     r9d, edx                    ; r9d = u32B (high bits are cleared)
                                        ; r8d already holds the 64-bit divisor
 %else
    mov     rax, rdi                    ; rax = u64A
    mov     r9d, esi                    ; r9d = u32B
    mov     r8, rdx                     ; r8  = u64C
 %endif
    mul     r9
    div     r8
%else
%error "Implement me"
%endif
    ret
ENDPROC ASMMultU64ByU32DivByU64

MARK_OBJECT_RETPOLINE_SAFE
