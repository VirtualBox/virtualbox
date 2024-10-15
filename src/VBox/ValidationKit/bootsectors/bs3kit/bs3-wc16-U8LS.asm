; $Id: bs3-wc16-U8LS.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - 16-bit Watcom C/C++, 64-bit integer left shift.
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
; 64-bit integer left shift.
;
; @returns  AX:BX:CX:DX (AX is the most significant, DX the least)
; @param    AX:BX:CX:DX Value to shift.
; @param    SI          Shift count.
;
global $_?U8LS
$_?U8LS:
global $_?I8LS
$_?I8LS:
        push    si

        ;
        ; The 16-bit watcom code differs from the 32-bit one in the way it
        ; handles the shift count. All 16-bit bits are used in the 16-bit
        ; code, we do the same as the 32-bit one as we don't want to wast
        ; time in the below loop.
        ;
        ; Using 8086 compatible approach here as it's less hazzle to write
        ; and smaller.
        ;
        and     si, 3fh

        jz      .return
.next_shift:
        shl     dx, 1
        rcl     cx, 1
        rcl     bx, 1
        rcl     ax, 1
        dec     si
        jnz     .next_shift

.return:
        pop     si
%ifdef ASM_MODEL_FAR_CODE
        retf
%else
        ret
%endif

