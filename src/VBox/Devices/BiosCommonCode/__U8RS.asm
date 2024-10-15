; $Id: __U8RS.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
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
public          __U8RS


                .8086

_TEXT           segment public 'CODE' use16
                assume cs:_TEXT

;;
; 64-bit unsigned right shift.
;
; @param    ax:bx:cx:dx Value. (AX is the most significant, DX the least)
; @param    si          Shift count.
; @returns  ax:bx:cx:dx Shifted value.
; si is zeroed
;
__U8RS:

                test    si, si
                jz      u8rs_quit
u8rs_rot:
                shr     ax, 1
                rcr     bx, 1
                rcr     cx, 1
                rcr     dx, 1
                dec     si
                jnz     u8rs_rot
u8rs_quit:
                ret

_TEXT           ends
                end

