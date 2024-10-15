; $Id: bs3-cmn-PrintU32.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3PrintU32, Common.
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


BS3_EXTERN_CMN Bs3PrintStr

;;
; Prints a 32-bit unsigned integer value.
;
; @param    [xBP + xCB*2]   32-bit value to format and print.
;
BS3_PROC_BEGIN_CMN Bs3PrintU32, BS3_PBC_HYBRID
        BS3_CALL_CONV_PROLOG 1
        push    xBP
        mov     xBP, xSP
        push    sAX
        push    sDX
        push    sCX
        push    sBX
BONLY16 push    ds

        mov     eax, [xBP + xCB + cbCurRetAddr]

        ; Allocate a stack buffer and terminate it. ds:bx points ot the end.
        sub     xSP, 30h
BONLY16 mov     bx, ss
BONLY16 mov     ds, bx
        mov     xBX, xSP
        add     xBX, 2fh
        mov     byte [xBX], 0

        mov     ecx, 10                 ; what to divide by
.next:
        xor     edx, edx
        div     ecx                     ; edx:eax / ecx -> eax and rest in edx.
        add     dl, '0'
        dec     xBX
        mov     [BS3_ONLY_16BIT(ss:)xBX], dl
        cmp     eax, 0
        jnz     .next

        ; Print the string.
BONLY64 add     rsp, 18h
BONLY16 push    ss
        push    xBX
        BS3_CALL Bs3PrintStr, 1

        add     xSP, 30h + BS3_IF_16_32_64BIT(2, 0, 18h) + xCB
BONLY16 pop     ds
        pop     sBX
        pop     sCX
        pop     sDX
        pop     sAX
        leave
        BS3_CALL_CONV_EPILOG 1
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3PrintU32

