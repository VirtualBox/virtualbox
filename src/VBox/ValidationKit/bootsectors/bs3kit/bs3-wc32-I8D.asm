; $Id: bs3-wc32-I8D.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - 32-bit Watcom C/C++, 64-bit signed integer division.
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

%include "bs3kit-template-header.mac"

BS3_EXTERN_CMN Bs3Int64Div


;;
; 64-bit signed integer division.
;
; @returns  EDX:EAX Quotient, ECX:EBX Remainder.
; @param    EDX:EAX     Dividend.
; @param    ECX:EBX     Divisor
;
global $??I8D
$??I8D:
        ;
        ; Convert to a C __cdecl call - not doing this in assembly.
        ;

        ; Set up a frame, allocating 16 bytes for the result buffer.
        push    ebp
        mov     ebp, esp
        sub     esp, 10h

        ; Pointer to the return buffer.
        push    esp

        ; The dividend.
        push    ecx
        push    ebx

        ; The dividend.
        push    edx
        push    eax

        call    Bs3Int64Div

        ; Load the result.
        mov     ecx, [ebp - 10h + 12]
        mov     ebx, [ebp - 10h + 8]
        mov     edx, [ebp - 10h + 4]
        mov     eax, [ebp - 10h]

        leave
        ret

