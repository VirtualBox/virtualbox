; $Id: ASMAtomicCmpXchgU8.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - ASMAtomicCmpXchgU8().
;

;
; Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
; Atomically compares and exchanges an unsigned 8-bit int.
;
; @param    pu8     x86:esp+4  msc:rcx  gcc:rdi
; @param    u8New   x86:esp+8  msc:dl   gcc:sil
; @param    u8Old   x86:esp+c  msc:r8l  gcc:dl
;
; @returns  bool result: true if successfully exchanged, false if not.
;           x86:al
;
RT_BEGINPROC ASMAtomicCmpXchgU8
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        mov     al, r8b
        lock cmpxchg [rcx], dl
 %else
        mov     al, dl
        lock cmpxchg [rdi], sil
 %endif
%else
        mov     ecx, [esp + 04h]
        mov     dl,  [esp + 08h]
        mov     al,  [esp + 0ch]
        lock cmpxchg [ecx], dl
%endif
        setz    al
        movzx   eax, al
        ret
ENDPROC ASMAtomicCmpXchgU8

