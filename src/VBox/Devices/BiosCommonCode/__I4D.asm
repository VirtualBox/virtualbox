; $Id: __I4D.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Compiler support routines.
;

;
; Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
; SPDX-License-Identifier: GPL-3.0-only
;


;*******************************************************************************
;*  Exported Symbols                                                           *
;*******************************************************************************
public          __I4D

if VBOX_BIOS_CPU lt 80386
extrn NeedToImplementOn8086__I4D:near
endif

; MASM (ML.EXE) is used for PXE and no longer understands the .8086 directive.
; WASM is used for the BIOS and understands it just fine.
ifdef __WASM__
                .8086
endif


_TEXT           segment public 'CODE' use16
                assume cs:_TEXT

;;
; 32-bit signed division.
;
; @param    dx:ax   Dividend.
; @param    cx:bx   Divisor.
; @returns  dx:ax   Quotient.
;           cx:bx   Remainder.
;
__I4D:
                pushf
if VBOX_BIOS_CPU ge 80386
                .386
                push    eax
                push    edx
                push    ecx

                rol     eax, 16
                mov     ax, dx
                ror     eax, 16
                xor     edx, edx

                shr     ecx, 16
                mov     cx, bx

                idiv    ecx                 ; eax:edx / ecx -> eax=quotient, edx=remainder.

                mov     bx, dx
                pop     ecx
                shr     edx, 16
                mov     cx, dx

                pop     edx
                ror     eax, 16
                mov     dx, ax
                add     sp, 2
                pop     ax
                rol     eax, 16
ifdef __WASM__
                .8086
endif

else
                call    NeedToImplementOn8086__I4D
endif
                popf
                ret


_TEXT           ends
                end

