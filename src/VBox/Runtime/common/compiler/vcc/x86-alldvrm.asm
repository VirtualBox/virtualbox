; $Id: x86-alldvrm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - Visual C++ Compiler - signed 64-bit division support, x86.
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


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
extern __aulldvrm


;;
; Division of signed 64-bit values, returning both the quotient and remainder.
;
; @returns  EDX:EAX Quotient, EBX:ECX Remainder.
; @param    [esp+04h] [ebp+08h]     Dividend (64-bit)
; @param    [esp+0ch] [ebp+10h]     Divisor (64-bit)
;
; @note     The remainder registers are swapped compared to Watcom's I8D and U8D.
;
BEGINPROC_RAW   __alldvrm
        ;
        ; Load high parts so we can examine them for negativity.
        ;
        mov     edx, [esp + 08h]            ; dividend_hi
        mov     ecx, [esp + 10h]            ; divisor_hi

        ;
        ; We use __aulldvrm to do the work, we take care of the signedness.
        ;
        or      edx, edx
        js      .negative_dividend

        or      ecx, ecx
        js      .negative_divisor_positive_dividend

        ; Both positive, so same as unsigned division.
        jmp     __aulldvrm


        ;
        ; The rest of the code sets up a stack frame using EBP as it makes
        ; calls rather than tail jumps.
        ;

.negative_divisor_positive_dividend:
        push    ebp
        mov     ebp, esp

        ; Load the low values to as we will be pushing them and probably negating them.
        mov     eax, dword [ebp + 08h]      ; dividend_lo
        mov     ebx, dword [ebp + 10h]      ; divisor_lo

        ; negate the divisor, do unsigned division, and negate the quotient.
        neg     ecx
        neg     ebx
        sbb     ecx, 0

        push    ecx
        push    ebx
        push    edx
        push    eax
        call    __aulldvrm                  ; cleans up the the stack.

        neg     edx
        neg     eax
        sbb     edx, 0

        leave
        ret     10h

.negative_dividend:
        push    ebp
        mov     ebp, esp

        ; Load the low values to as we will be pushing them and probably negating them.
        mov     eax, dword [ebp + 08h]      ; dividend_lo
        mov     ebx, dword [ebp + 10h]      ; divisor_lo

        neg     edx
        neg     eax
        sbb     edx, 0

        or      ecx, ecx
        js      .negative_dividend_negative_divisor

.negative_dividend_positive_divisor:
        ; negate the dividend (above), do unsigned division, and negate both quotient and remainder

        push    ecx
        push    ebx
        push    edx
        push    eax
        call    __aulldvrm                  ; cleans up the the stack.

        neg     edx
        neg     eax
        sbb     edx, 0

.return_negated_remainder:
        neg     ebx
        neg     ecx
        sbb     ebx, 0

        leave
        ret     10h

.negative_dividend_negative_divisor:
        ; negate both dividend (above) and divisor, do unsigned division, and negate the remainder.
        neg     ecx
        neg     ebx
        sbb     ecx, 0

        push    ecx
        push    ebx
        push    edx
        push    eax
        call    __aulldvrm                  ; cleans up the the stack.

        jmp     .return_negated_remainder
ENDPROC_RAW     __alldvrm

