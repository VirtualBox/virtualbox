; $Id: fmemset.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
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
public          _fmemset_


                .8086

_TEXT           segment public 'CODE' use16
                assume cs:_TEXT


;;
; memset taking a far pointer.
;
; cx, es may be modified; di is preserved
;
; @returns  dx:ax unchanged.
; @param    dx:ax   Pointer to the memory.
; @param    bl      The fill value.
; @param    cx      The number of bytes to fill.
;
_fmemset_:
                push    di

                mov     es, dx
                mov     di, ax
                xchg    al, bl
                rep stosb
                xchg    al, bl

                pop     di
                ret

_TEXT           ends
                end

