; $Id: ASMMultU64ByU32DivByU32.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - Assembly Functions, ASMMultU64ByU32DivByU32.
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
; Multiple a 64-bit by a 32-bit integer and divide the result by a 32-bit integer
; using a 96 bit intermediate result.
;
; @returns (u64A * u32B) / u32C.
; @param   u64A/rcx/rdi     The 64-bit value.
; @param   u32B/edx/esi     The 32-bit value to multiple by A.
; @param   u32C/r8d/edx     The 32-bit value to divide A*B by.
;
; @cproto  DECLASM(uint64_t) ASMMultU64ByU32DivByU32(uint64_t u64A, uint32_t u32B, uint32_t u32C);
;
RT_BEGINPROC ASMMultU64ByU32DivByU32
%if ARCH_BITS == 64

 %ifdef ASM_CALL64_MSC
    mov     rax, rcx                    ; rax = u64A
    mov     r9d, edx                    ; should check the specs wrt to the high bits one day...
    mov     r8d, r8d                    ; be paranoid for the time being.
 %else
    mov     rax, rdi                    ; rax = u64A
    mov     r9d, esi                    ; r9d = u32B
    mov     r8d, edx                    ; r8d = u32C
 %endif
    mul     r9
    div     r8

%else ; 16 or 32 bit
    ;
    ; This implementation is converted from the GCC inline
    ; version of the code. Nothing additional has been done
    ; performance wise.
    ;
 %if ARCH_BITS == 16
    push    bp
    mov     bp, sp
    push    eax                         ; push all return registers to preserve high value (paranoia)
    push    ebx
    push    ecx
    push    edx
 %endif
    push    esi
    push    edi

 %if ARCH_BITS == 16
  %define u64A_Lo     [bp + 4 + 04h]
  %define u64A_Hi     [bp + 4 + 08h]
  %define u32B        [bp + 4 + 0ch]
  %define u32C        [bp + 4 + 10h]
 %else
  %define u64A_Lo     [esp + 04h + 08h]
  %define u64A_Hi     [esp + 08h + 08h]
  %define u32B        [esp + 0ch + 08h]
  %define u32C        [esp + 10h + 08h]
 %endif

    ; Load parameters into registers.
    mov     eax, u64A_Lo
    mov     esi, u64A_Hi
    mov     ecx, u32B
    mov     edi, u32C

    ; The body, just like the in
    mul     ecx                         ; eax = u64Lo.lo = (u64A.lo * u32B).lo
                                        ; edx = u64Lo.hi = (u64A.lo * u32B).hi
    xchg    eax, esi                    ; esi = u64Lo.lo
                                        ; eax = u64A.hi
    xchg    edx, edi                    ; edi = u64Low.hi
                                        ; edx = u32C
    xchg    edx, ecx                    ; ecx = u32C
                                        ; edx = u32B
    mul     edx                         ; eax = u64Hi.lo = (u64A.hi * u32B).lo
                                        ; edx = u64Hi.hi = (u64A.hi * u32B).hi
    add     eax, edi                    ; u64Hi.lo += u64Lo.hi
    adc     edx, 0                      ; u64Hi.hi += carry
    div     ecx                         ; eax = u64Hi / u32C
                                        ; edx = u64Hi % u32C
    mov     edi, eax                    ; edi = u64Result.hi = u64Hi / u32C
    mov     eax, esi                    ; eax = u64Lo.lo
    div     ecx                         ; u64Result.lo
    mov     edx, edi                    ; u64Result.hi

    ; epilogue
    pop     edi
    pop     esi
 %if ARCH_BITS == 16
    ;  DX:CX:BX:AX, where DX holds bits 15:0, CX bits 31:16, BX bits 47:32, and AX bits 63:48.
    mov     ax, [bp - 4*4]              ; dx = bits 15:0
    shr     eax, 16
    mov     ax, [bp - 3*4]              ; cx = bits 31:16
    mov     dx, [bp - 2*4]              ; bx = bits 47:32
    shr     edx, 16
    mov     dx, [bp - 1*4]              ; ax = bits 63:48
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    leave
 %endif
%endif
    ret
ENDPROC ASMMultU64ByU32DivByU32

