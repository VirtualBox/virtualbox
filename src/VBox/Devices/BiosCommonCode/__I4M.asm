; $Id: __I4M.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
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
public          __I4M

if VBOX_BIOS_CPU lt 80386
extrn NeedToImplementOn8086__I4M:near
endif

; MASM (ML.EXE) is used for PXE and no longer understands the .8086 directive.
; WASM is used for the BIOS and understands it just fine.
ifdef __WASM__
                .8086
endif

_TEXT           segment public 'CODE' use16
                assume cs:_TEXT

;;
; 32-bit signed multiplication.
;
; @param    dx:ax   Factor 1.
; @param    cx:bx   Factor 2.
; @returns  dx:ax   Result.
; cx, es may be modified; di is preserved
;
__I4M:
                pushf
if VBOX_BIOS_CPU ge 80386
                .386
                push    eax
                push    edx
                push    ecx
                push    ebx

                rol     eax, 16
                mov     ax, dx
                ror     eax, 16
                xor     edx, edx

                shr     ecx, 16
                mov     cx, bx

                imul    ecx                 ; eax * ecx -> edx:eax

                pop     ebx
                pop     ecx

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
                call    NeedToImplementOn8086__I4M
endif

                popf
                ret


_TEXT           ends
                end

