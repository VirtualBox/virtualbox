; $Id: os2_utilA.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Os2UtilA - Watcom assembly file that defines the stack.
;

;
; Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

DGROUP  group _NULL,CONST,CONST2,STRINGS,_DATA,_BSS,STACK

_NULL   segment para public 'BEGDATA'
        dw      10 dup(0)
_NULL   ends

CONST   segment word public 'DATA'
CONST   ends

CONST2  segment word public 'DATA'
CONST2  ends

STRINGS segment word public 'DATA'
STRINGS ends

_DATA   segment word public 'DATA'
_DATA   ends

_BSS    segment word public 'BSS'
_BSS    ends

STACK   segment para stack 'STACK'
        db      1000h dup(?)
STACK   ends

        end

