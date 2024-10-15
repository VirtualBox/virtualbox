; $Id: xmmsaving-asm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; xmmsaving - assembly helpers.
;

;
; Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
%include "VBox/vmm/stam.mac"


BEGINCODE


;;
; DECLASM(int) XmmSavingTestLoadSet(const MYXMMREGSET *pSet, const MYXMMREGSET *pPrevSet, PRTUINT128U pBadVal);
;
; @returns 0 on success, 1-based register number on failure.
; @param    pSet            The new set.
; @param    pPrevSet        The previous set. Can be NULL.
; @param    pBadVal         Where to store the actual register value on failure.
;
BEGINPROC XmmSavingTestLoadSet
    push    xBP
    mov     xBP, xSP
    sub     xSP, 32                     ; Space for storing an XMM register (in TEST_REG).
    and     xSP, ~31                    ; Align it.

    ; Unify register/arguments.
%ifdef ASM_CALL64_GCC
    mov     r8, rdx                     ; pBadVal
    mov     xCX, rdi                    ; pSet
    mov     xDX, rsi                    ; pPrevSet
%endif
%ifdef RT_ARCH_X86
    mov     xCX, [ebp + 8]              ; pSet
    mov     xDX, [ebp + 12]             ; pPrevSet
%endif

    test    xDX, xDX
    jz near .just_load

    ; Check that the old set is still correct.
%macro TEST_REG 1,
    movdqa  [xSP], xmm %+ %1
    mov     xAX, [xDX + %1 * 8]
    cmp     [xSP], xAX
    jne     %%bad
    mov     xAX, [xDX + %1 * 8 + xCB]
    cmp     [xSP + xCB], xAX
%ifdef RT_ARCH_X86
    jne     %%bad
    mov     xAX, [xDX + %1 * 8 + xCB*2]
    cmp     [xSP + xCB*2], xAX
    jne     %%bad
    mov     xAX, [xDX + %1 * 8 + xCB*3]
    cmp     [xSP + xCB*3], xAX
%endif
    je      %%next
%%bad:
    mov     eax, %1 + 1
    jmp     .return_copy_badval
%%next:
%endmacro

    TEST_REG 0
    TEST_REG 1
    TEST_REG 2
    TEST_REG 3
    TEST_REG 4
    TEST_REG 5
    TEST_REG 6
    TEST_REG 7
%ifdef RT_ARCH_AMD64
    TEST_REG 8
    TEST_REG 9
    TEST_REG 10
    TEST_REG 11
    TEST_REG 12
    TEST_REG 13
    TEST_REG 14
    TEST_REG 15
%endif

    ; Load the new state.
.just_load:
    movdqu  xmm0,  [xCX + 0*8]
    movdqu  xmm1,  [xCX + 1*8]
    movdqu  xmm2,  [xCX + 2*8]
    movdqu  xmm3,  [xCX + 3*8]
    movdqu  xmm4,  [xCX + 4*8]
    movdqu  xmm5,  [xCX + 5*8]
    movdqu  xmm6,  [xCX + 6*8]
    movdqu  xmm7,  [xCX + 7*8]
%ifdef RT_ARCH_AMD64
    movdqu  xmm8,  [xCX + 8*8]
    movdqu  xmm9,  [xCX + 9*8]
    movdqu  xmm10, [xCX + 10*8]
    movdqu  xmm11, [xCX + 11*8]
    movdqu  xmm12, [xCX + 12*8]
    movdqu  xmm13, [xCX + 13*8]
    movdqu  xmm14, [xCX + 14*8]
    movdqu  xmm15, [xCX + 15*8]
%endif
    xor     eax, eax
    jmp     .return

.return_copy_badval:
    ; don't touch eax here.
%ifdef RT_ARCH_X86
    mov     edx, [ebp + 16]
    mov     ecx, [esp]
    mov     [edx     ], ecx
    mov     ecx, [esp +  4]
    mov     [edx +  4], ecx
    mov     ecx, [esp +  8]
    mov     [edx +  8], ecx
    mov     ecx, [esp + 12]
    mov     [edx + 12], ecx
%else
    mov     rdx, [rsp]
    mov     rcx, [rsp + 8]
    mov     [r8], rdx
    mov     [r8 + 8], rcx
%endif
    jmp     .return

.return:
    leave
    ret
ENDPROC   XmmSavingTestLoadSet

