; $Id: tstAsmMovzx-1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Disassembly testcase - Valid movzx sequences and related instructions.
;
; This is a build test, that means it will be assembled, disassembled,
; then the disassembly output will be assembled and the new binary will
; compared with the original.
;

;
; Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

    BITS TEST_BITS

    movzx ax, al
    movzx eax, al
    movzx ax, [ebx]
    movzx eax, byte [ebx]
    movzx eax, word [ebx]
%if TEST_BITS != 64
    movzx ax, [bx+si+8]
    movzx eax, byte [bx+si+8]
    movzx eax, word [bx+si+8]
%else
    movzx rax, al
    movzx rax, ax
    movzx rax, byte [rsi]
    movzx rax, word [rsi]
%endif

