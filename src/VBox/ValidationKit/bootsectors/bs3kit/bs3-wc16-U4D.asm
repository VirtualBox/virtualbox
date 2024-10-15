; $Id: bs3-wc16-U4D.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - 16-bit Watcom C/C++, 32-bit unsigned integer division.
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



;;
; 32-bit unsigned integer division.
;
; @returns  DX:AX quotient, CX:BX remainder.
; @param    DX:AX           Dividend.
; @param    CX:BX           Divisor
;
; @uses     Nothing.
;
%ifdef BS3KIT_WITH_REAL_WATCOM_INTRINSIC_NAMES
global __U4D
__U4D:
%endif
global $_?U4D
$_?U4D:
%if TMPL_BITS >= 32
        ; Move dividend into EDX:EAX
        shl     eax, 10h
        mov     ax, dx
        xor     edx, edx

        ; Move divisor into ebx.
        shl     ebx, 10h
        mov     bx, cx

        ; Do it!
        div     ebx

        ; Reminder in to CX:BX
        mov     bx, dx
        shr     edx, 10h
        mov     cx, dx

        ; Quotient into DX:AX
        mov     edx, eax
        shr     edx, 10h
%else
        push    ds
        push    es

        ;
        ; Convert to a C __cdecl call - too lazy to do this in assembly.
        ;

        ; Set up a frame of sorts, allocating 8 bytes for the result buffer.
        push    bp
        sub     sp, 08h
        mov     bp, sp

        ; Pointer to the return buffer.
        push    ss
        push    bp
        add     bp, 08h                 ; Correct bp.

        ; The divisor.
        push    cx
        push    bx

        ; The dividend.
        push    dx
        push    ax

        BS3_EXTERN_CMN Bs3UInt32Div
        call    Bs3UInt32Div

        ; Load the reminder.
        mov     cx, [bp - 08h + 6]
        mov     bx, [bp - 08h + 4]
        ; Load the quotient.
        mov     dx, [bp - 08h + 2]
        mov     ax, [bp - 08h]

        mov     sp, bp
        pop     bp
        pop     es
        pop     ds

%endif
%ifdef ASM_MODEL_FAR_CODE
        retf
%else
        ret
%endif

