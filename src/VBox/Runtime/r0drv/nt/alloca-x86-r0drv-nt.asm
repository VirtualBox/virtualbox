; $Id: alloca-x86-r0drv-nt.asm 115003 2026-08-12 23:16:08Z knut.osmundsen@oracle.com $
;; @file
; IPRT - Visual C++ __alloca__probe_16.
;

;
; Copyright (C) 2020-2026 Oracle and/or its affiliates.
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

;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%include "iprt/asmdefs.mac"

BEGINCODE

;;
; @returns  esp with eax subtracted and aligned to 16 bytes.
; @param    eax allocation size.
;
BEGINPROC _alloca_probe_16
        ; Sanitycheck the input.
%ifdef DEBUG
        cmp     eax, 0
        jne     .not_zero
        int3
.not_zero:
        cmp     eax, 4096
        jbe     .four_kb_or_less
        int3
.four_kb_or_less:
%endif

        ; Don't bother probing the stack as the allocation is supposed to be
        ; a lot smaller than 4KB.
        neg     eax
        lea     eax, [esp + eax + 4]
        and     eax, 0fffffff0h
        xchg    eax, esp
        jmp     [eax]
ENDPROC _alloca_probe_16

MARK_OBJECT_RETPOLINE_SAFE  ;; @todo retpoline: _alloc_probe_16 is doing an indirect call...
