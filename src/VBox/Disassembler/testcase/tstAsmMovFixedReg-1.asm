; $Id: tstAsmMovFixedReg-1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Disassembly testcase - Valid mov immediate to fixed registers.
;
; This is a build test, that means it will be assembled, disassembled,
; then the disassembly output will be assembled and the new binary will
; compared with the original.
;

;
; Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

    mov al, 01h
    mov cl, 02h
    mov dl, 03h
    mov bl, 04h
    mov ah, 05h
    mov ch, 06h
    mov dh, 07h
    mov bh, 08h
%if TEST_BITS == 64
    mov spl, 09h
    mov bpl, 0ah
    mov sil, 0bh
    mov dil, 0ch
    mov r8b, 0dh
    mov r9b, 0eh
    mov r10b, 0fh
    mov r11b, 010h
    mov r12b, 011h
    mov r13b, 012h
    mov r14b, 013h
    mov r15b, 014h
%endif

    mov ax, 0f701h
    mov cx, 0f702h
    mov dx, 0f703h
    mov bx, 0f704h
    mov sp, 0f705h
    mov bp, 0f706h
    mov si, 0f707h
    mov di, 0f708h
%if TEST_BITS == 64
    mov r8w,  0f709h
    mov r9w,  0f70ah
    mov r10w, 0f70bh
    mov r11w, 0f70ch
    mov r12w, 0f70dh
    mov r13w, 0f70eh
    mov r14w, 0f70fh
    mov r15w, 0f710h
%endif

    mov eax, 0beeff701h
    mov ecx, 0beeff702h
    mov edx, 0beeff703h
    mov ebx, 0beeff704h
    mov esp, 0beeff705h
    mov ebp, 0beeff706h
    mov esi, 0beeff707h
    mov edi, 0beeff708h
%if TEST_BITS == 64
    mov r8d,  0beeff709h
    mov r9d,  0beeff70ah
    mov r10d, 0beeff70bh
    mov r11d, 0beeff70ch
    mov r12d, 0beeff70dh
    mov r13d, 0beeff70eh
    mov r14d, 0beeff70fh
    mov r15d, 0beeff710h
%endif

%if TEST_BITS == 64
    mov rax, 0feedbabef00df701h
    mov rcx, 0feedbabef00df702h
    mov rdx, 0feedbabef00df703h
    mov rbx, 0feedbabef00df704h
    mov rsp, 0feedbabef00df705h
    mov rbp, 0feedbabef00df706h
    mov rsi, 0feedbabef00df707h
    mov rdi, 0feedbabef00df708h
    mov r8,  0feedbabef00df709h
    mov r9,  0feedbabef00df70ah
    mov r10, 0feedbabef00df70bh
    mov r11, 0feedbabef00df70ch
    mov r12, 0feedbabef00df70dh
    mov r13, 0feedbabef00df70eh
    mov r14, 0feedbabef00df70fh
    mov r15, 0feedbabef00df710h
%endif

