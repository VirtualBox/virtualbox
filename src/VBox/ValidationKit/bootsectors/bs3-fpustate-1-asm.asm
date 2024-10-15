; $Id: bs3-fpustate-1-asm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - bs3-fpustate-1, assembly helpers and template instantiation.
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


;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "bs3kit.mac"


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BS3_BEGIN_DATA16
;; @name Floating point constants.
; @{
g_r32_0dot1:    dd 0.1
g_r32_3dot2:    dd 3.2
g_r32_Zero:     dd 0.0
g_r32_One:      dd 1.0
g_r32_Two:      dd 2.0
g_r32_Three:    dd 3.0
g_r32_Ten:      dd 10.0
g_r32_Eleven:   dd 11.0
g_r32_ThirtyTwo:dd 32.0
g_r32_Min:      dd 000800000h
g_r32_Max:      dd 07f7fffffh
g_r32_Inf:      dd 07f800000h
g_r32_SNaN:     dd 07f800001h
g_r32_SNaNMax:  dd 07fbfffffh
g_r32_QNaN:     dd 07fc00000h
g_r32_QNaNMax:  dd 07fffffffh
g_r32_NegQNaN:  dd 0ffc00000h

g_r64_0dot1:    dq 0.1
g_r64_6dot9:    dq 6.9
g_r64_Zero:     dq 0.0
g_r64_One:      dq 1.0
g_r64_Two:      dq 2.0
g_r64_Three:    dq 3.0
g_r64_Ten:      dq 10.0
g_r64_Eleven:   dq 11.0
g_r64_ThirtyTwo:dq 32.0
g_r64_Min:      dq 00010000000000000h
g_r64_Max:      dq 07fefffffffffffffh
g_r64_Inf:      dq 07ff0000000000000h
g_r64_SNaN:     dq 07ff0000000000001h
g_r64_SNaNMax:  dq 07ff7ffffffffffffh
g_r64_NegQNaN:  dq 0fff8000000000000h
g_r64_QNaN:     dq 07ff8000000000000h
g_r64_QNaNMax:  dq 07fffffffffffffffh
g_r64_DnMin:    dq 00000000000000001h
g_r64_DnMax:    dq 0000fffffffffffffh


g_r80_0dot1:    dt 0.1
g_r80_3dot2:    dt 3.2
g_r80_Zero:     dt 0.0
g_r80_One:      dt 1.0
g_r80_Two:      dt 2.0
g_r80_Three:    dt 3.0
g_r80_Ten:      dt 10.0
g_r80_Eleven:   dt 11.0
g_r80_ThirtyTwo:dt 32.0
%ifdef __NASM__
g_r80_Min:      dq 08000000000000000h
                dw 00001h
g_r80_Max:      dq     0ffffffffffffffffh
                dw 07ffeh
g_r80_Inf:      dq     08000000000000000h
                dw 07fffh
g_r80_QNaN:     dq     0c000000000000000h
                dw 07fffh
g_r80_QNaNMax:  dq     0ffffffffffffffffh
                dw 07fffh
g_r80_NegQNaN:  dq     0c000000000000000h
                dw 0ffffh
g_r80_SNaN:     dq     08000000000000001h
                dw 07fffh
g_r80_SNaNMax:  dq     0bfffffffffffffffh
                dw 07fffh
g_r80_DnMin:    dq     00000000000000001h
                dw 00000h
g_r80_DnMax:    dq     07fffffffffffffffh
                dw 00000h
%else
g_r80_Min:      dt 000018000000000000000h
g_r80_Max:      dt 07ffeffffffffffffffffh
g_r80_Inf:      dt 07fff8000000000000000h
g_r80_QNaN:     dt 07fffc000000000000000h
g_r80_QNaNMax:  dt 07fffffffffffffffffffh
g_r80_NegQNaN:  dt 0ffffc000000000000000h
g_r80_SNaN:     dt 07fff8000000000000001h
g_r80_SNaNMax:  dt 07fffbfffffffffffffffh
g_r80_DnMin:    dt 000000000000000000001h
g_r80_DnMax:    dt 000007fffffffffffffffh
%endif

g_r32V1:        dd 3.2
g_r32V2:        dd -1.9
g_r64V1:        dq 6.4
g_r80V1:        dt 8.0

; Denormal numbers.
g_r32D0:        dd 000200000h
;; @}

;; @name Upconverted Floating point constants
; @{
;g_r80_r32_0dot1:        dt 0.1
%ifdef __NASM__
g_r80_r32_3dot2:        dq     0cccccd0000000000h
                        dw 04000h
%else
g_r80_r32_3dot2:        dt 04000cccccd0000000000h
%endif
;g_r80_r32_Zero:         dt 0.0
;g_r80_r32_One:          dt 1.0
;g_r80_r32_Two:          dt 2.0
;g_r80_r32_Three:        dt 3.0
;g_r80_r32_Ten:          dt 10.0
;g_r80_r32_Eleven:       dt 11.0
;g_r80_r32_ThirtyTwo:    dt 32.0
;; @}

;; @name Decimal constants.
; @{
g_u64Zero:      dd 0
g_u32Zero:      dw 0
g_u64Two:       dd 2
g_u32Two:       dw 2
;; @}


;
; Instantiate code templates.
;
BS3_INSTANTIATE_TEMPLATE_ESSENTIALS      "bs3-fpustate-1-template.mac"

