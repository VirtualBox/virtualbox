; $Id: __U4D.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
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
public          __U4D

; MASM (ML.EXE) is used for PXE and no longer understands the .8086 directive.
; WASM is used for the BIOS and understands it just fine.
ifdef __WASM__
                .8086
endif


if VBOX_BIOS_CPU lt 80386
extrn _DoUInt32Div:near
endif


_TEXT           segment public 'CODE' use16
                assume cs:_TEXT


;;
; 32-bit unsigned division.
;
; @param    dx:ax   Dividend.
; @param    cx:bx   Divisor.
; @returns  dx:ax   Quotient.
;           cx:bx   Remainder.
;
__U4D:
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

                div     ecx                 ; eax:edx / ecx -> eax=quotient, edx=remainder.

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
                ;
                ; If the divisor is only 16-bit, use a fast path
                ;
                test    cx, cx
                jnz     do_it_the_hard_way

                div     bx              ; dx:ax / bx -> ax=quotient, dx=remainder

                mov     bx, dx          ; remainder in cx:bx, and we know cx=0

                xor     dx, dx          ; quotient in dx:ax, dx must be zero

                popf
                ret

do_it_the_hard_way:
                ; Call C function do this.
                push    ds
                push    es

                ;
                ; Convert to a C __cdecl call - not doing this in assembly.
                ;

                ; Set up a frame of sorts, allocating 4 bytes for the result buffer.
                push    bp
                sub     sp, 04h
                mov     bp, sp

                ; Pointer to the return buffer.
                push    ss
                push    bp
                add     bp, 04h                 ; Correct bp.

                ; The divisor.
                push    cx
                push    bx

                ; The dividend.
                push    dx
                push    ax

                call    _DoUInt32Div

                ; Load the remainder.
                mov     cx, [bp - 02h]
                mov     bx, [bp - 04h]

                ; The quotient is already in dx:ax

                mov     sp, bp
                pop     bp
                pop     es
                pop     ds
endif
                popf
                ret

_TEXT           ends
                end

