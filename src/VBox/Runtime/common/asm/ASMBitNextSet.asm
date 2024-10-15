; $Id: ASMBitNextSet.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - ASMBitNextSet().
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
; Finds the next set bit in a bitmap.
;
; @returns (32/64:eax, 16:ax+dx)   Index of the first zero bit.
; @returns (32/64:eax, 16:ax+dx)  -1 if no set bit was found.
; @param   msc:rcx gcc:rdi pvBitmap    Pointer to the bitmap.
; @param   msc:edx gcc:rsi cBits       The number of bits in the bitmap. Multiple of 32.
; @param   msc:r8d gcc:rcx iBitPrev    The previous bit, start searching after it.
;
; @remarks Not quite sure how much sense it makes to do this in assembly, but
;          it started out with the ASMBit* API, so that's why we still have it.
;
RT_BEGINPROC ASMBitNextSet
%if ARCH_BITS == 16
        push    bp
        mov     bp, sp
%endif
        push    xDI

        ;
        ; Align input registers: rdi=pvBitmap, ecx=iPrevBit
        ;
%if    ARCH_BITS == 64
 %ifdef ASM_CALL64_GCC
        ; ecx = iBitPrev param, rdi=pvBitmap param.
 %else
        mov     rdi, rcx                ; rdi=pvBits
        mov     ecx, r8d                ; ecx=iPrevBit
        mov     r8d, edx                ; r8d=cBits (saved for .scan_dwords)
 %endif
        mov     r9, rdi                 ; Save rdi for bit calculation.
%elif ARCH_BITS == 32
        mov     edi, [esp + 8]          ; edi=pvBits
        mov     ecx, [esp + 8 + 8]      ; edx=iPrevBit
%elif ARCH_BITS == 16
        mov     ax,  [bp + 4 + 2]
        mov     es, ax
        mov     di,  [bp + 4]           ; es:di=pvBits
        mov     ecx, [bp + 4 + 8]       ; edx=iPrevBit
%endif

        ;
        ; If iPrevBit and iPrevBit + 1 are in the same dword, inspect it for further bits.
        ;
        inc     ecx
        mov     eax, ecx
        shr     eax, 5
        shl     eax, 2                  ; eax=byte offset into bitmap of current dword.
        add     xDI, xAX                ; xDI=current dword address (of iPrevBit + 1).
        and     ecx, 31
        jz      .scan_dwords

%if ARCH_BITS == 16
        mov     edx, [es:di]            ; edx = current dword
%else
        mov     edx, [xDI]              ; edx = current dword
%endif
        shr     edx, cl                 ; Shift out bits that we have searched.
        jz      .next_dword             ; If zero, nothing to find. Go rep scasd.
        shl     edx, cl                 ; Shift it back so bsf will return the right index.

        bsf     edx, edx                ; edx=index of first set bit

        shl     eax, 3                  ; Turn eax back into a bit offset of the current dword.
        add     eax, edx                ; eax=bit offset

.return:
        pop     xDI
%if ARCH_BITS == 16
        mov     edx, eax
        shr     edx, 16
        leave
%endif
        ret

        ;
        ; Do dword scan.
        ;

        ; Skip empty dword.
.next_dword:
        add     xDI, 4                  ; Skip the empty dword.
        add     eax, 4

.scan_dwords:
        ; First load and align bit count.
%if    ARCH_BITS == 64
 %ifdef ASM_CALL64_GCC
        mov     ecx, esi
 %else
        mov     ecx, r8d
 %endif
%elif ARCH_BITS == 32
        mov     ecx, [esp + 8 + 4]
%elif ARCH_BITS == 16
        mov     ecx, [bp + 4 + 4]
%endif
        add     ecx, 31
        shr     ecx, 5                  ; ecx=bitmap size in dwords.

        ; Adjust ecx to how many dwords there are left to scan. (eax = current byte offset)
        shr     eax, 2                  ; eax=current dword offset.
        sub     ecx, eax
        jbe     .return_failure

        ; Do the scanning.
        xor     eax, eax
        repe scasd
        je      .return_failure

        ; Find the bit in question.
        sub     xDI, 4                  ; One step back.
%if ARCH_BITS == 16
        movzx   edi, di
        mov     eax, [es:xDI]
%else
        mov     eax, [xDI]
%endif
        bsf     eax, eax
        jz      .return_failure         ; race paranoia

        ; Calc the bit offset.
%if ARCH_BITS == 16
        sub     di, [bp + 4]
        movzx   edi, di
%elif ARCH_BITS == 32
        sub     xDI, [esp + 4]
%elif ARCH_BITS == 64
        sub     xDI, r9
%endif
        shl     edi, 3                  ; edi=bit offset of current dword.
        add     eax, edi
        jmp     .return

.return_failure:
        mov     eax, 0ffffffffh
        jmp     .return
ENDPROC ASMBitNextSet

