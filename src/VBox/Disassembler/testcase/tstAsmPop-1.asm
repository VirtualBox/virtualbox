; $Id: tstAsmPop-1.asm 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $
;; @file
; Disassembly testcase - Valid pop sequences and related instructions.
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
%if TEST_BITS != 64
    pop     bp
    pop     ebp
    pop     word [bp]
    pop     dword [bp]
    pop     word [ebp]
    pop     dword [ebp]
%else
 %if 0 ; doesn't work yet
    pop     rbp
    pop     qword [rbp]
 %endif
%endif

