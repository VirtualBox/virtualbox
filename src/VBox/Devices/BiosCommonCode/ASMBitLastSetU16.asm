; $Id: ASMBitLastSetU16.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BiosCommonCode - ASMBitLastSetU16() - borrowed from IPRT.
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
; SPDX-License-Identifier: GPL-3.0-only
;


;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
public _ASMBitLastSetU16

        .8086

_TEXT   segment public 'CODE' use16
        assume cs:_TEXT


;;
; Finds the last bit which is set in the given 16-bit integer.
;
; Bits are numbered from 1 (least significant) to 16.
;
; @returns (ax)     index [1..16] of the last set bit.
; @returns (ax)     0 if all bits are cleared.
; @param   u16      Integer to search for set bits.
;
; @cproto DECLASM(unsigned) ASMBitLastSetU16(uint32_t u16);
;
_ASMBitLastSetU16   proc
        .8086
        push    bp
        mov     bp, sp

        mov     cx, [bp + 2 + 2]
        test    cx, cx                  ; check if zero (eliminates checking dec ax result)
        jz      return_zero

        mov     ax, 16
next_bit:
        shl     cx, 1
        jc      return
        dec     ax
        jmp     next_bit

return_zero:
        xor     ax, ax
return:
        pop     bp
        ret
_ASMBitLastSetU16   endp

_TEXT           ends
                end

