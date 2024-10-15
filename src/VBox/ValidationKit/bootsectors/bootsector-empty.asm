; $Id: bootsector-empty.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Empty bootsector can be used as example
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
    ; put the code here



hlt_again:
    hlt
    cli
    jmp hlt_again

    ;
    ; The GDT.
    ;
padding:
times 510 - (padding - start) db 0
    db 055h, 0aah

