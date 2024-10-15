; $Id: tstAsmLock-3.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Disassembly testcase - Invalid lock sequences for non-locking instructions.
;
; The intention is to check in a binary using the --all-invalid mode
; of tstDisasm-2.
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

    lock mov ebp, esp
    lock mov byte [0], 0
    lock mov word [0], 0
    lock mov dword [0], 0
    lock mov word [0], 01234h
    lock mov dword [0], 012348765h
    lock mov byte [ebx], 0
    lock mov [ebx], eax
    lock mov [ebx], ax
    lock mov [ebx], al
    lock mov [ebx], edx
    lock mov [ebx], dx
    lock mov [ebx], dl
    lock ret
%if TEST_BITS != 64
    lock pop  ebp
    lock push esp
%else
    lock pop  rbp
    lock push rsp
%endif

