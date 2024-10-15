; $Id: tstAsmSignExtend-1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Disassembly testcase - Valid sign extension instructions.
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

%include "tstAsm.mac"
    BITS TEST_BITS

    movsx ax, al
    movsx eax, al
    movsx eax, ax

    ;
    ; ParseImmByteSX
    ;

    ; 83 /x
    add eax, strict byte 8
    add eax, strict byte -1
    cmp ebx, strict byte -1

    add ax, strict byte 8
    add ax, strict byte -1
    cmp bx, strict byte -1

%if TEST_BITS == 64 ; check that these come out with qword values and not words or dwords.
    add rax, strict byte 8
    add rax, strict byte -1
    cmp rbx, strict byte -1
%endif

    ; push %Ib
    push strict byte -1
    push strict byte -128
    push strict byte 127

    ;; @todo imul
