; $Id: ASMBitLastSetU32.asm 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $
;; @file
; IPRT - ASMBitLastSetU32().
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
; Finds the last bit which is set in the given 32-bit integer.
;
; Bits are numbered from 1 (least significant) to 32.
;
; @returns (xAX)        index [1..32] of the last set bit.
; @returns (xAX)        0 if all bits are cleared.
; @param   msc:ecx gcc:edi x86:stack u32  Integer to search for set bits.
;
; @cproto DECLASM(unsigned) ASMBitLastSetU32(uint32_t u32);
;
RT_BEGINPROC ASMBitLastSetU32
%if ARCH_BITS == 16
        CPU     8086
        push    bp
        mov     bp, sp

        ; high word.
        mov     ax, 32
        mov     cx, [bp + 2 + 4]
        test    cx, cx
        jnz     .next_bit

        ; low word.
        mov     al, 16
        or      cx, [bp + 2 + 2]
        jz      .return_zero

        ; find the bit that was set.
.next_bit:
        shl     cx, 1
        jc      .return
        dec     ax
        jmp     .next_bit

.return_zero:
        xor     ax, ax
.return:
        pop     bp
        ret

%else
 %if    ARCH_BITS == 64
  %ifdef ASM_CALL64_GCC
        bsr     eax, esi
  %else
        bsr     eax, ecx
  %endif
 %elif ARCH_BITS == 32
        bsr     eax, dword [esp + 4]
 %else
  %error "Missing or invalid ARCH_BITS."
 %endif
        jz      .return_zero
        inc     eax
.return:
        ret
.return_zero:
        xor     eax, eax
        ret
%endif
ENDPROC ASMBitLastSetU32

