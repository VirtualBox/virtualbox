; $Id: ASMBitFirstSetU64.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - ASMBitFirstSetU64().
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
; Finds the first bit which is set in the given 64-bit integer.
;
; Bits are numbered from 1 (least significant) to 64.
;
; @returns (xAX)        index [1..64] of the first set bit.
; @returns (xAX)        0 if all bits are cleared.
; @param   msc:rcx gcc:rdi x86:stack u64  Integer to search for set bits.
;
; @cproto DECLASM(unsigned) ASMBitFirstSetU64(uint64_t u64);
;
RT_BEGINPROC ASMBitFirstSetU64
%if ARCH_BITS == 16
        CPU     8086
        push    bp
        mov     bp, sp

        ; 15:0
        mov     ax, 1
        mov     cx, [bp + 2 + 2 + 0]
        test    cx, cx
        jnz     .next_bit

        ; 31:16
        mov     al, 16
        or      cx, [bp + 2 + 2 + 2]
        jnz     .next_bit

        ; 47:32
        mov     al, 32
        or      cx, [bp + 2 + 2 + 4]
        jnz     .next_bit

        ; 63:48
        mov     al, 48
        or      cx, [bp + 2 + 2 + 6]
        jz      .return_zero

        ; find the bit that was set.
.next_bit:
        shr     cx, 1
        jc      .return
        inc     ax
        jmp     .next_bit

.return_zero:
        xor     ax, ax
.return:
        pop     bp
        ret

%else
 %if    ARCH_BITS == 64
  %ifdef ASM_CALL64_GCC
        bsf     rax, rsi
  %else
        bsf     rax, rcx
  %endif
        jz      .return_zero
        inc     eax
        ret

 %elif ARCH_BITS == 32
        ; Check the first dword then the 2nd one.
        bsf     eax, dword [esp + 4 + 0]
        jnz     .check_2nd_dword
        inc     eax
        ret
.check_2nd_dword:
        bsf     eax, dword [esp + 4 + 4]
        jz      .return_zero
        add     eax, 32
        ret
 %else
  %error "Missing or invalid ARCH_BITS."
 %endif
.return_zero:
        xor     eax, eax
        ret
%endif
ENDPROC ASMBitFirstSetU64

