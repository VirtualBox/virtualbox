; $Id: bs3-wc16-U8DQ.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - 16-bit Watcom C/C++, 64-bit unsigned integer division.
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

BS3_EXTERN_CMN Bs3UInt64Div


;;
; 64-bit unsigned integer division, SS variant.
;
; @returns  ax:bx:cx:dx quotient.  (AX is the most significant, DX the least)
; @param    ax:bx:cx:dx     Dividend.
; @param    [ss:si]         Divisor
;
; @uses     Nothing.
;
global $_?U8DQ
$_?U8DQ:
        push    es
        push    ss
        pop     es
%ifdef ASM_MODEL_FAR_CODE
        push    cs
%endif
        call    $_?U8DQE
        pop     es
%ifdef ASM_MODEL_FAR_CODE
        retf
%else
        ret
%endif

;;
; 64-bit unsigned integer division, ES variant.
;
; @returns  ax:bx:cx:dx quotient. (AX is the most significant, DX the least)
; @param    ax:bx:cx:dx     Dividend.
; @param    [es:si]         Divisor
;
; @uses     Nothing.
;
global $_?U8DQE
$_?U8DQE:
        push    ds
        push    es

        ;
        ; Convert to a C __cdecl call - not doing this in assembly.
        ;

        ; Set up a frame of sorts, allocating 16 bytes for the result buffer.
        push    bp
        sub     sp, 10h
        mov     bp, sp

        ; Pointer to the return buffer.
        push    ss
        push    bp
        add     bp, 10h                 ; Correct bp.

        ; The divisor.
        push    word [es:si + 6]
        push    word [es:si + 4]
        push    word [es:si + 2]
        push    word [es:si]

        ; The dividend.
        push    ax
        push    bx
        push    cx
        push    dx

        call    Bs3UInt64Div

        ; Load the quotient.
        mov     ax, [bp - 10h + 6]
        mov     bx, [bp - 10h + 4]
        mov     cx, [bp - 10h + 2]
        mov     dx, [bp - 10h]

        mov     sp, bp
        pop     bp
        pop     es
        pop     ds
%ifdef ASM_MODEL_FAR_CODE
        retf
%else
        ret
%endif

