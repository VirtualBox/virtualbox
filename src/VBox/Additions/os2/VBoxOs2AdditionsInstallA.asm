; $Id: VBoxOs2AdditionsInstallA.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBoxOs2AdditionsInstallA - Watcom assembly file that defines the stack.
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

        .386p

DGROUP  group CONST,CONST2,_DATA,_BSS,STACK

CONST   segment use32 dword public 'DATA'
CONST   ends

CONST2  segment use32 dword public 'DATA'
CONST2  ends

_DATA   segment use32 dword public 'DATA'
_DATA   ends

_BSS    segment use32 dword public 'BSS'
_BSS    ends

STACK   segment use32 para stack 'STACK'
        db      1000h dup(?)
STACK   ends

        end

