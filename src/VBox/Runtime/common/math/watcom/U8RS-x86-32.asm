; $Id: U8RS-x86-32.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - 32-bit Watcom C/C++, 64-bit unsigned integer right shift.
;

;
; Copyright (C) 2007-2024 Oracle and/or its affiliates.
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


BEGINCODE


;;
; 64-bit unsigned integer right shift.
;
; @returns  EDX:EAX
; @param    EDX:EAX     Value to shift.
; @param    BL          Shift count (it's specified as ECX:EBX, but we only use BL).
;
global __U8RS
__U8RS:
        push    ecx                     ; We're allowed to trash ECX, but why bother.

        mov     cl, bl
        and     cl, 3fh
        test    cl, 20h
        jnz     .big_shift

        ; Shifting less than 32.
        shrd    eax, edx, cl
        shr     edx, cl

.return:
        pop     ecx
        ret

.big_shift:
        ; Shifting 32 or more.
        mov     eax, edx
        shr     eax, cl                 ; Only uses lower 5 bits.
        xor     edx, edx
        jmp     .return

