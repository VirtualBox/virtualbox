; $Id: bootsector-pae.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Bootsector that switches the CPU info PAE mode.
;

;
; Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
; The contents of this file may alternatively be used under the terms
; of the Common Development and Distribution License Version 1.0
; (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
; in the VirtualBox distribution, in which case the provisions of the
; CDDL are applicable instead of those of the GPL.
;
; You may elect to license modified versions of this file under the
; terms and conditions of either the GPL or the CDDL or both.
;
; SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
;

%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"
%include "VBox/bios.mac"


;; The boot sector load address.
%define BS_ADDR         0x7c00
%define PDP_ADDR        0x9000
%define PD_ADDR         0xa000


BITS 16
start:
    ; Start with a jump just to follow the convention.
    jmp short the_code
    nop
times 3ah db 0

the_code:
    cli
    xor     edx, edx
    mov     ds, dx                      ; Use 0 based addresses

    ;
    ; Create a paging hierarchy
    ;
    mov     cx, 4
    xor     esi, esi                    ; physical address
    mov     ebx, PDP_ADDR
    mov     edi, PD_ADDR
pdptr_loop:
    ; The page directory pointer entry.
    mov     dword [ebx], edi
    or      word [bx], X86_PDPE_P
    mov     dword [ebx + 4], edx

    ; The page directory.
pd_loop:
    mov     dword [edi], esi
    or      word [di], X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_PS
    mov     dword [edi + 4], 0
    add     esi, 0x00200000             ; 2MB
    add     di, 8
    test    di, 0fffh
    jnz     pd_loop

    add     bx, 8
    loop    pdptr_loop

    ;
    ; Switch to protected mode.
    ;
    lgdt [(gdtr - start) + BS_ADDR]
    lidt [(idtr_null - start) + BS_ADDR]

    mov     eax, PDP_ADDR
    mov     cr3, eax

    mov     eax, cr4
    or      eax, X86_CR4_PAE | X86_CR4_PSE
    mov     cr4, eax

    mov     eax, cr0
    or      eax, X86_CR0_PE | X86_CR0_PG
    mov     cr0, eax
    jmp far 0x0008:((code32_start - start) + BS_ADDR) ; 8=32-bit CS

BITS 32
code32_start:
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ax, 0x18
    mov     es, ax
    mov     esp, 0x80000

    ; eye catchers
    mov     eax, 0xCafeBabe
    mov     ebx, eax
    mov     ecx, eax
    mov     edx, eax
    mov     edi, eax
    mov     esi, eax
    mov     ebp, eax

    ;
    ; Boch shutdown request.
    ;
    mov bl, 64
    mov dx, VBOX_BIOS_SHUTDOWN_PORT
    mov ax, VBOX_BIOS_OLD_SHUTDOWN_PORT
retry:
    mov ecx, 8
    mov esi, (szShutdown - start) + BS_ADDR
    rep outsb
    xchg dx, ax                         ; alternate between the new (VBox) and old (Bochs) ports.
    dec bl
    jnz retry
    ; Shutdown failed!
hlt_again:
    hlt
    cli
    jmp hlt_again

    ;
    ; The GDT.
    ;
align 8, db 0
gdt:
    dw  0,      0, 0,      0            ; null selector
    dw  0xffff, 0, 0x9b00, 0x00cf       ; 32 bit flat code  segment (0x08)
    dw  0xffff, 0, 0x9300, 0x00cf       ; 32 bit flat data  segment (0x10)
    dw  0xffff, 0, 0x9300, 0x00cf       ; 32 bit flat stack segment (0x18)

gdtr:
    dw  8*4-1                           ; limit 15:00
    dw  (gdt - start) + BS_ADDR         ; base  15:00
    db  0                               ; base  23:16
    db  0                               ; unused

idtr_null:
    dw  0                               ; limit 15:00
    dw  (gdt - start) + BS_ADDR         ; base  15:00
    db  0                               ; base  23:16
    db  0                               ; unused

szShutdown:
    db 'Shutdown', 0

    ;
    ; Padd the remainder of the sector with zeros and
    ; end it with the dos signature.
    ;
padding:
times 510 - (padding - start) db 0
    db 055h, 0aah

