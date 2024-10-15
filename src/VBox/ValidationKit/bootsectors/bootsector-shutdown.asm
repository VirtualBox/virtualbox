; $Id: bootsector-shutdown.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Bootsector for grub chainloading that shutdowns the VM.
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

%include "VBox/bios.mac"


BITS 16
start:
    ; Start with a jump just to follow the convention.
    jmp short the_code
    nop
times 3ah db 0

the_code:
    cli

    ;
    ; VBox/Bochs shutdown request - write "Shutdown" byte by byte to shutdown port.
    ;
    mov cx, 64
    mov dx, VBOX_BIOS_SHUTDOWN_PORT
    mov bx, VBOX_BIOS_OLD_SHUTDOWN_PORT
retry:
    mov al, 'S'
    out dx, al
    mov al, 'h'
    out dx, al
    mov al, 'u'
    out dx, al
    mov al, 't'
    out dx, al
    mov al, 'd'
    out dx, al
    mov al, 'o'
    out dx, al
    mov al, 'w'
    out dx, al
    mov al, 'n'
    out dx, al
    xchg dx, bx                         ; alternate between the new (VBox) and old (Bochs) ports.
    loop retry

    ;
    ; Shutdown failed!
    ;

    ;; @todo print some message before halting.
    hlt

    ;
    ; Padd the remainder of the sector with zeros and
    ; end it with the dos signature.
    ;
padding:
times 510 - (padding - start) db 0
    db 055h, 0aah

