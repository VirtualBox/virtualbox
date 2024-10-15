; $Id: I8D-x86-32.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
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

%include "iprt/asmdefs.mac"


BEGINCODE

extern __U8D


;;
; 64-bit signed integer division.
;
; @returns  EDX:EAX Quotient, ECX:EBX Remainder.
; @param    EDX:EAX     Dividend.
; @param    ECX:EBX     Divisor
;
global __I8D
__I8D:
        ;
        ; We use __U8D to do the work, we take care of the signedness.
        ;
        or      edx, edx
        js      .negative_dividend

        or      ecx, ecx
        js      .negative_divisor_positive_dividend
        jmp     __U8D


.negative_divisor_positive_dividend:
        ; negate the divisor, do unsigned division, and negate the quotient.
        neg     ecx
        neg     ebx
        sbb     ecx, 0

        call    __U8D

        neg     edx
        neg     eax
        sbb     edx, 0
        ret

.negative_dividend:
        neg     edx
        neg     eax
        sbb     edx, 0

        or      ecx, ecx
        js      .negative_dividend_negative_divisor

.negative_dividend_positive_divisor:
        ; negate the dividend (above), do unsigned division, and negate both quotient and remainder
        call    __U8D

        neg     edx
        neg     eax
        sbb     edx, 0

.return_negated_remainder:
        neg     ecx
        neg     ebx
        sbb     ecx, 0
        ret

.negative_dividend_negative_divisor:
        ; negate both dividend (above) and divisor, do unsigned division, and negate the remainder.
        neg     ecx
        neg     ebx
        sbb     ecx, 0

        call    __U8D
        jmp     .return_negated_remainder

