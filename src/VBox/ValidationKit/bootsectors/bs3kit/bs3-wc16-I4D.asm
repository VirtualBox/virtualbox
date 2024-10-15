; $Id: bs3-wc16-I4D.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - 16-bit Watcom C/C++, 32-bit signed integer division.
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
; 32-bit signed integer division.
;
; @returns  DX:AX quotient, CX:BX remainder.
; @param    DX:AX           Dividend.
; @param    CX:BX           Divisor
;
; @uses     Nothing.
;
global $_?I4D
$_?I4D:
;; @todo no idea if we're getting the negative division stuff right here according to what watcom expectes...
extern TODO_NEGATIVE_SIGNED_DIVISION
        ; Move dividend into EDX:EAX
        shl     eax, 10h
        mov     ax, dx
        sar     dx, 0fh
        movsx   edx, dx

        ; Move divisor into ebx.
        shl     ebx, 10h
        mov     bx, cx

        ; Do it!
        idiv    ebx

        ; Reminder in to CX:BX
        mov     bx, dx
        shr     edx, 10h
        mov     cx, dx

        ; Quotient into DX:AX
        mov     edx, eax
        shr     edx, 10h

%ifdef ASM_MODEL_FAR_CODE
        retf
%else
        ret
%endif

