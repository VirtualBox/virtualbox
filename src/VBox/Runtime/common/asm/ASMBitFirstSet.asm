; $Id: ASMBitFirstSet.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - ASMBitFirstSet().
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
; Finds the first set bit in a bitmap.
;
; @returns (32/64:eax, 16:ax+dx)   Index of the first zero bit.
; @returns (32/64:eax, 16:ax+dx)  -1 if no set bit was found.
; @param   msc:rcx gcc:rdi pvBitmap    Pointer to the bitmap.
; @param   msc:edx gcc:rsi cBits       The number of bits in the bitmap. Multiple of 32.
;
RT_BEGINPROC ASMBitFirstSet
        ;
        ; if (cBits)
        ; Put cBits in ecx first.
        ;
%if    ARCH_BITS == 64
 %ifdef ASM_CALL64_GCC
        mov     ecx, esi
 %else
        xchg    rcx, rdx                ; rdx=pvDst, ecx=cBits
 %endif
%elif ARCH_BITS == 32
        mov     ecx, [esp + 4 + 4]
%elif ARCH_BITS == 16
        push    bp
        mov     bp, sp
        mov     ecx, [bp + 4 + 4]
%endif
        or      ecx, ecx
        jz      short .failed
        ;{
        push    xDI

        ;    asm {...}
%if    ARCH_BITS == 64
 %ifdef ASM_CALL64_GCC
                                        ; rdi = start of scasd - already done
 %else
        mov     rdi, rdx                ; rdi = start of scasd (Note! xchg rdx,rcx above)
 %endif
%elif ARCH_BITS == 32
        mov     edi, [esp + 4]
%elif ARCH_BITS == 16
        mov     ax, [bp + 4 + 2]
        mov     di, [bp + 4]
        mov     es, ax                  ; es is volatile, no need to save.
%endif
        add     ecx, 31                 ; 32 bit aligned
        shr     ecx, 5                  ; number of dwords to scan.
        mov     xDX, xDI                ; xDX = saved pvBitmap
        xor     eax, eax
        repe scasd                      ; Scan for the first dword with any bit set.
        je      .failed_restore

        ; find the bit in question
        sub     xDI, 4                  ; one step back.
%if ARCH_BITS == 16
        movzx   edi, di
        mov     eax, [es:xDI]
%else
        mov     eax, [xDI]
%endif
        sub     xDI, xDX
        shl     edi, 3                  ; calc bit offset.

        bsf     ecx, eax
        jz      .failed_restore         ; race paranoia
        add     ecx, edi
        mov     eax, ecx

        ; return success
        pop     xDI
%if ARCH_BITS == 16
        mov     edx, eax
        shr     edx, 16
        leave
%endif
        ret

        ; failure
        ;}
        ;return -1;
.failed_restore:
        pop     xDI
.failed:
%if ARCH_BITS != 16
        mov     eax, 0ffffffffh
%else
        mov     ax, 0ffffh
        mov     dx, ax
        leave
%endif
        ret
ENDPROC ASMBitFirstSet

