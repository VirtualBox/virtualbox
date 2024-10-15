; $Id: x86-allmul.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - Visual C++ Compiler - 64-bit multiplication support, x86.
;

;
; Copyright (C) 2023-2024 Oracle and/or its affiliates.
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


;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "iprt/asmdefs.mac"


;;
; Multiplication of signed or unsigned 64-bit values.
;
; @returns  edx:eax
; @param    [esp+04h]   Factor #1 (64-bit)
; @param    [esp+0ch]   Factor #2 (64-bit)
; @uses     ecx
;
BEGINPROC_RAW   __allmul
        ;
        ; See if this is a pure 32-bit multiplication. We might get lucky.
        ;
        mov     eax, [esp + 04h]            ; eax = F1.lo

        mov     edx, [esp + 04h+4]
        mov     ecx, [esp + 0ch+4]          ; ecx = F2.hi
        or      edx, ecx                    ; trashes edx but reduces number of branches.
        jnz     .complicated

        ; edx:eax = F1.lo * F2.lo
        mul     dword [esp + 0ch]

        ret     10h

        ;
        ; Complicated.
        ;
.complicated:
        ; ecx = F1.lo * F2.hi  (edx contains overflow here can be ignored)
        mul     ecx
        mov     ecx, eax

        ; ecx += F1.hi * F2.lo (edx can be ignored again)
        mov     eax, [esp + 04h+4]
        mul     dword [esp + 0ch]
        add     ecx, eax

        ; edx:eax = F1.lo * F2.lo
        mov     eax, [esp + 04h]
        mul     dword [esp + 0ch]

        ; Add ecx to the high part (edx).
        add     edx, ecx

        ret     10h
ENDPROC_RAW     __allmul

