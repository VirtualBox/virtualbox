; $Id: bs3-cmn-MemSet.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3MemSet.
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

%include "bs3kit-template-header.mac"

;;
; @cproto   BS3_CMN_PROTO_NOSB(void, Bs3MemSet,(void BS3_FAR *pvDst, uint8_t bFiller, size_t cbDst));
;
BS3_PROC_BEGIN_CMN Bs3MemSet, BS3_PBC_HYBRID
        push    xBP
        mov     xBP, xSP
        push    xDI
%ifdef RT_ARCH_AMD64

        mov     rdi, rcx                ; rdi = pvDst
        mov     rcx, r8                 ; rcx = cbDst
        movzx   edx, dl                 ; bFiller
        mov     rax, 0101010101010101h
        mul     rdx
        mov     rcx, r8
        shr     rcx, 3                  ; calc qword count.
        cld
        rep stosq

        mov     rcx, r8                 ; cbDst
        and     rcx, 7                  ; calc trailing byte count.
        rep stosb

%elif ARCH_BITS == 16
        push    es

        mov     di, [bp + 2 + cbCurRetAddr]     ; pvDst.off
        mov     es, [bp + 2 + cbCurRetAddr + 2] ; pvDst.sel
        mov     al, [bp + 2 + cbCurRetAddr + 4] ; bFiller
        mov     ah, al
        mov     cx, [bp + 2 + cbCurRetAddr + 6] ; cbDst
        shr     cx, 1                           ; calc dword count.
        rep stosw

        mov     cx, [bp + 2 + cbCurRetAddr + 6] ; cbDst
        and     cx, 1                           ; calc tailing byte count.
        rep stosb

        pop     es

%elif ARCH_BITS == 32
        mov     edi, [ebp + 8]                          ; pvDst
        mov     al, byte [ebp + 4 + cbCurRetAddr + 4]   ; bFiller
        mov     ah, al
        mov     dx, ax
        shl     eax, 16
        mov     ax, dx                                  ; eax = RT_MAKE_U32_FROM_U8(bFiller, bFiller, bFiller, bFiller)
        mov     ecx, [ebp + 4 + cbCurRetAddr + 8]       ; cbDst
        shr     cx, 2                                   ; calc dword count.
        rep stosd

        mov     ecx, [ebp + 4 + cbCurRetAddr + 8]       ; cbDst
        and     ecx, 3                                  ; calc tailing byte count.
        rep stosb

%else
 %error "Unknown bitness."
%endif

        pop     xDI
        pop     xBP
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3MemSet

