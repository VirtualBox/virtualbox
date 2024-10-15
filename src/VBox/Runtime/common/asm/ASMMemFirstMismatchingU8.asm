; $Id: ASMMemFirstMismatchingU8.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - ASMMemFirstMismatchingU8().
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
%define RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"


BEGINCODE

;;
; Variant of ASMMemFirstMismatchingU8 with a fixed @a u8 value.
; We repeat the prolog and join the generic function.
;
RT_BEGINPROC ASMMemFirstNonZero
        ;
        ; Prologue.
        ;
%if ARCH_BITS != 64
        push    xBP
        mov     xBP, xSP
        push    xDI
 %if ARCH_BITS == 16
        push    es
 %endif
%elifdef ASM_CALL64_MSC
        mov     r9, rdi                 ; save rdi in r9
%endif
SEH64_END_PROLOGUE

        ;
        ; Normalize input; rdi=pv, rcx=cb, rax=0
        ;
 %if ARCH_BITS == 64
  %ifdef ASM_CALL64_MSC
        mov     rdi, rcx
        mov     rcx, rdx
        jrcxz   RT_CONCAT(NAME(ASMMemFirstMismatchingU8),.return_all_same)
        xor     eax, eax
  %else
        mov     rcx, rsi
        jrcxz   RT_CONCAT(NAME(ASMMemFirstMismatchingU8),.return_all_same)
        xor     eax, eax
  %endif

 %elif ARCH_BITS == 32
        mov     ecx, [ebp + 0ch]
        jecxz   RT_CONCAT(NAME(ASMMemFirstMismatchingU8),.return_all_same)
        mov     edi, [ebp + 08h]
        xor     eax, eax

 %elif ARCH_BITS == 16
        mov     cx, [bp + 08h]          ; cb
        jcxz    RT_CONCAT(NAME(ASMMemFirstMismatchingU8),.return16_all_same)
        les     di, [bp + 04h]          ; pv (far)
        xor     ax, ax

 %else
  %error "Invalid ARCH_BITS value"
 %endif

        ;
        ; Join ASMMemFirstMismatchingU8
        ;
        jmp     RT_CONCAT(NAME(ASMMemFirstMismatchingU8),.is_all_zero_joining)
ENDPROC    ASMMemFirstNonZero


;;
; Inverted memchr.
;
; @returns Pointer to the byte which doesn't equal u8.
; @returns NULL if all equal to u8.
;
; @param   msc:rcx gcc:rdi  pv      Pointer to the memory block.
; @param   msc:rdx gcc:rsi  cb      Number of bytes in the block. This MUST be aligned on 32-bit!
; @param   msc:r8b gcc:dl   u8      The value it's supposed to be filled with.
;
; @cproto DECLINLINE(void *) ASMMemFirstMismatchingU8(void const *pv, size_t cb, uint8_t u8)
;
RT_BEGINPROC ASMMemFirstMismatchingU8
        ;
        ; Prologue.
        ;
%if ARCH_BITS != 64
        push    xBP
        mov     xBP, xSP
        push    xDI
 %if ARCH_BITS == 16
        push    es
 %endif
%elifdef ASM_CALL64_MSC
        mov     r9, rdi                 ; save rdi in r9
%endif
SEH64_END_PROLOGUE

%if ARCH_BITS != 16
        ;
        ; The 32-bit and 64-bit variant of the code.
        ;

        ; Normalize input; rdi=pv, rcx=cb, rax=eight-times-u8
 %if ARCH_BITS == 64
  %ifdef ASM_CALL64_MSC
        mov     rdi, rcx
        mov     rcx, rdx
        jrcxz   .return_all_same
        movzx   r8d, r8b
        mov     rax, qword 0101010101010101h
        imul    rax, r8
  %else
        mov     rcx, rsi
        jrcxz   .return_all_same
        movzx   edx, dl
        mov     rax, qword 0101010101010101h
        imul    rax, rdx
  %endif

 %elif ARCH_BITS == 32
        mov     ecx, [ebp + 0ch]
        jecxz   .return_all_same
        mov     edi, [ebp + 08h]
        movzx   eax, byte [ebp + 10h]
        mov     ah, al
        movzx   edx, ax
        shl     eax, 16
        or      eax, edx
 %else
  %error "Invalid ARCH_BITS value"
 %endif

.is_all_zero_joining:
        cld

        ; Unaligned pointer? Align it (elsewhere).
        test    edi, xCB - 1
        jnz     .unaligned_pv
.aligned_pv:

        ; Do the dword/qword scan.
        mov     edx, xCB - 1
        and     edx, ecx                ; Remaining bytes for tail scan
 %if ARCH_BITS == 64
        shr     xCX, 3
        repe scasq
 %else
        shr     xCX, 2
        repe scasd
 %endif
        jne     .multibyte_mismatch

        ; Prep for tail scan.
        mov     ecx, edx

        ;
        ; Byte by byte scan.
        ;
.byte_by_byte:
        repe scasb
        jne     .return_xDI

.return_all_same:
        xor     eax, eax
 %ifdef ASM_CALL64_MSC
        mov     rdi, r9                 ; restore rdi
 %elif ARCH_BITS == 32
        pop     edi
        leave
 %endif
        ret

        ; Return after byte scan mismatch.
.return_xDI:
        lea     xAX, [xDI - 1]
 %ifdef ASM_CALL64_MSC
        mov     rdi, r9                 ; restore rdi
 %elif ARCH_BITS == 32
        pop     edi
        leave
 %endif
        ret

        ;
        ; Multibyte mismatch.  We rewind and do a byte scan of the remainder.
        ; (can't just search the qword as the buffer must be considered volatile).
        ;
.multibyte_mismatch:
        lea     xDI, [xDI - xCB]
        lea     xCX, [xCX * xCB + xCB]
        or      ecx, edx
        jmp     .byte_by_byte

        ;
        ; Unaligned pointer.  If it's worth it, align the pointer, but if the
        ; memory block is too small do the byte scan variant.
        ;
.unaligned_pv:
        cmp     xCX, 4*xCB              ; 4 steps seems reasonable.
        jbe     .byte_by_byte

        ; Unrolled buffer realignment.
 %if ARCH_BITS == 64
        dec     xCX
        scasb
        jne     .return_xDI
        test    edi, xCB - 1
        jz      .aligned_pv

        dec     xCX
        scasb
        jne     .return_xDI
        test    edi, xCB - 1
        jz      .aligned_pv

        dec     xCX
        scasb
        jne     .return_xDI
        test    edi, xCB - 1
        jz      .aligned_pv

        dec     xCX
        scasb
        jne     .return_xDI
        test    edi, xCB - 1
        jz      .aligned_pv
 %endif

        dec     xCX
        scasb
        jne     .return_xDI
        test    edi, xCB - 1
        jz      .aligned_pv

        dec     xCX
        scasb
        jne     .return_xDI
        test    edi, xCB - 1
        jz      .aligned_pv

        dec     xCX
        scasb
        jne     .return_xDI
        jmp     .aligned_pv


%else ; ARCH_BITS == 16

        ;
        ; The 16-bit variant of the code is a little simpler since we're
        ; working with two byte words in the 'fast' scan.  We also keep
        ; this separate from the 32-bit/64-bit code because that allows
        ; avoid a few rex prefixes here and there by using extended
        ; registers (e??) where we don't care about the whole register.
        ;
CPU 8086

        ; Load input parameters.
        mov     cx, [bp + 08h]          ; cb
        jcxz   .return16_all_same
        les     di, [bp + 04h]          ; pv (far)
        mov     al, [bp + 0ah]          ; u8
        mov     ah, al

.is_all_zero_joining:
        cld

        ; Align the pointer.
        test    di, 1
        jz      .word_scan

        dec     cx
        scasb
        jne     .return16_di
        jcxz    .return16_all_same

        ; Scan word-by-word.
.word_scan:
        mov     dx, cx
        shr     cx, 1
        repe scasw
        jne     .word_mismatch

        ; do we have a tail byte?
        test    dl, 1
        jz      .return16_all_same
        scasb
        jne     .return16_di

.return16_all_same:
        xor     ax, ax
        xor     dx, dx
.return16:
        pop     es
        pop     di
        pop     bp
        ret

.word_mismatch:
        ; back up a word.
        inc     cx
        sub     di, 2

        ; Do byte-by-byte scanning of the rest of the buffer.
        shl     cx, 1
        mov     dl, 1
        and     dl, [bp + 08h]          ; cb
        or      cl, dl
        repe scasb
        je      .return16_all_same

.return16_di:
        mov     ax, di
        dec     ax
        mov     dx, es
        jmp     .return16

%endif  ; ARCH_BITS == 16
ENDPROC ASMMemFirstMismatchingU8

