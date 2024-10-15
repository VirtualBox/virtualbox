; $Id: rdtsc-asm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; RDTSC test, assembly code
;

;
; Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
%include "iprt/x86.mac"


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BEGINDATA
;;
; Where DoTscReads() returns the rdtsc values.
;
; @note The results are 32-bit value pairs in x86 mode and 64-bit pairs in
;       AMD64 mode.
GLOBALNAME g_aRdTscResults
%ifdef RT_ARCH_AMD64
        dq 0, 0
        dq 0, 0 ; first value stored
        dq 0, 0
        dq 0, 0
        dq 0, 0
        dq 0, 0
        dq 0, 0
%else
        dq 0, 0
        dd 0, 0 ; first value stored
        dd 0, 0
        dd 0, 0
%endif


BEGINCODE

;; Takes no arguments, returns number of values read into g_aRdTscResults.
BEGINPROC DoTscReads
        push    xBP
        mov     xBP, xSP
%ifdef RT_ARCH_AMD64
        mov     rax, 0feedfacecafebabeh
        mov     rdx, 0cafebabefeedfaceh
        mov     r8,  0deadbeefdeadbeefh
        mov     r9,  0deadbeefdeadbeefh
        mov     r10, 0deadbeefdeadbeefh
        mov     r11, 0deadbeefdeadbeefh
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15

        ; Read 6x TSC into registers.
        rdtsc
        mov     r8, rax
        mov     r9, rdx
        rdtsc
        mov     r10, rax
        mov     r11, rdx
        rdtsc
        mov     r12, rax
        mov     r13, rdx
        rdtsc
        mov     r14, rax
        mov     r15, rdx
        rdtsc
        mov     rbx, rax
        mov     rcx, rdx
        rdtsc

        ; Store the values (64-bit).
        mov     [NAME(g_aRdTscResults) + 10h xWrtRIP], r8
        mov     [NAME(g_aRdTscResults) + 18h xWrtRIP], r9
        mov     [NAME(g_aRdTscResults) + 20h xWrtRIP], r10
        mov     [NAME(g_aRdTscResults) + 28h xWrtRIP], r11
        mov     [NAME(g_aRdTscResults) + 30h xWrtRIP], r12
        mov     [NAME(g_aRdTscResults) + 38h xWrtRIP], r13
        mov     [NAME(g_aRdTscResults) + 40h xWrtRIP], r14
        mov     [NAME(g_aRdTscResults) + 48h xWrtRIP], r15
        mov     [NAME(g_aRdTscResults) + 50h xWrtRIP], rbx
        mov     [NAME(g_aRdTscResults) + 58h xWrtRIP], rcx
        mov     [NAME(g_aRdTscResults) + 60h xWrtRIP], rax
        mov     [NAME(g_aRdTscResults) + 68h xWrtRIP], rdx

        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx

        mov     eax, 6
%else
        mov     eax, 0feedfaceh
        mov     edx, 0cafebabeh
        push    esi
        push    edi
        push    ebx

        ; Read 3x TSC into registers.
        rdtsc
        mov     ebx, eax
        mov     ecx, edx
        rdtsc
        mov     esi, eax
        mov     edi, edx
        rdtsc

        ; Store values.
        mov     [NAME(g_aRdTscResults) + 08h], ebx
        mov     [NAME(g_aRdTscResults) + 0ch], ecx
        mov     [NAME(g_aRdTscResults) + 10h], esi
        mov     [NAME(g_aRdTscResults) + 14h], edi
        mov     [NAME(g_aRdTscResults) + 18h], eax
        mov     [NAME(g_aRdTscResults) + 1ch], edx

        pop     ebx
        pop     edi
        pop     esi

        mov     eax, 3
%endif
        leave
        ret
ENDPROC   DoTscReads

