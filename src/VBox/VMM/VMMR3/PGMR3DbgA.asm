; $Id: PGMR3DbgA.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; PGM - Page Manager and Monitor - Debugger & Debugging API Optimizations.
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
; SPDX-License-Identifier: GPL-3.0-only
;


;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%define RT_ASM_WITH_SEH64
%include "VBox/asmdefs.mac"

BEGINCODE ;; Doesn't end up in code seg on 64-bit darwin. weird.


;
; Common to all code below.
;
%ifdef ASM_CALL64_MSC
 %define pvNeedle       r8
 %define cbNeedle       r9d
 %define bTmp           dl
%elifdef ASM_CALL64_GCC
 %define pvNeedle       rdx
 %define cbNeedle       esi
 %define bTmp           r9b
%elifdef RT_ARCH_X86
 %define pvNeedle       dword [esp + 8h]
 %define cbNeedle       dword [esp + 10h]
%else
 %error "Unsupported arch!"
%endif

;;
; Searches for a 8 byte needle in steps of 8.
;
; In 32-bit mode, this will only actually search for a 8 byte needle.
;
; @param    pbHaystack  [msc:rcx,     gcc:rdi, x86:ebp+08h]  What to search thru.
; @param    cbHaystack  [msc:edx,     gcc:rsi, x86:ebp+0ch]  The amount of hay to search.
; @param    pvNeedle    [msc:r8,      gcc:rdx, x86:ebp+10h]  What we're searching for
; @param    cbNeedle    [msc:r9,      gcc:rcx, x86:esp+10h]  Size of what we're searcing for. Currently ignored.
;
; @remarks  ASSUMES pbHaystack is aligned at uAlign.
;
BEGINPROC pgmR3DbgFixedMemScan8Wide8Step
%ifdef ASM_CALL64_MSC
        mov     r10, rdi                ; save it
        mov     rdi, rcx                ; rdi=pbHaystack
        mov     ecx, edx                ; rcx=cbHaystack
        mov     rax, [r8]               ; *(uint64_t *)pvNeedle
%elifdef ASM_CALL64_GCC
        xchg    rcx, rsi                ; rcx=cbHaystack, rsi=cbNeedle
        mov     rax, [rdx]              ; *(uint64_t *)pvNeedle
%elifdef RT_ARCH_X86
        push    ebp
        mov     ebp, esp
        push    edi                     ; save it
        mov     edi, [ebp + 08h]        ; pbHaystack
        mov     ecx, [ebp + 0ch]        ; cbHaystack
        mov     eax, [ebp + 10h]        ; pvNeedle
        mov     edx, [eax + 4]          ; ((uint32_t *)pvNeedle)[1]
        mov     eax, [eax]              ; ((uint32_t *)pvNeedle)[0]
%else
 %error "Unsupported arch!"
%endif
SEH64_END_PROLOGUE

%ifdef RT_ARCH_X86
        ;
        ; No string instruction to help us here.  Do a simple tight loop instead.
        ;
        shr     ecx, 3
        jz      .return_null
.again:
        cmp     [edi], eax
        je      .needle_check
.continue:
        add     edi, 8
        dec     ecx
        jnz     .again
        jmp     .return_null

        ; Check the needle 2nd dword, caller can do the rest.
.needle_check:
        cmp     edx, [edi + 4]
        jne     .continue

.return_edi:
        mov     eax, edi

%else  ; RT_ARCH_AMD64
        cmp     ecx, 8
        jb      .return_null
.continue:
        shr     ecx, 3
        repne   scasq
        jne     .return_null
        ; check more of the needle if we can.
        mov     r11d, 8
        shl     ecx, 3
.needle_check:
        cmp     cbNeedle, r11d
        je      .return_edi
        cmp     ecx, r11d
        jb      .return_edi             ; returns success here as we've might've lost stuff while shifting ecx around.
        mov     bTmp, [pvNeedle + r11]
        cmp     bTmp, [xDI + r11 - 8]
        jne     .continue
        inc     r11d
        jmp     .needle_check

.return_edi:
        lea     xAX, [xDI - 8]
%endif ; RT_ARCH_AMD64

.return:
%ifdef ASM_CALL64_MSC
        mov     rdi, r10
%elifdef RT_ARCH_X86
        pop     edi
        leave
%endif
        ret

.return_null:
        xor     eax, eax
        jmp     .return
ENDPROC   pgmR3DbgFixedMemScan8Wide8Step


;;
; Searches for a 4 byte needle in steps of 4.
;
; @param    pbHaystack  [msc:rcx,     gcc:rdi, x86:esp+04h]  What to search thru.
; @param    cbHaystack  [msc:edx,     gcc:rsi, x86:esp+08h]  The amount of hay to search.
; @param    pvNeedle    [msc:r8,      gcc:rdx, x86:esp+0ch]  What we're searching for
; @param    cbNeedle    [msc:r9,      gcc:rcx, x86:esp+10h]  Size of what we're searcing for. Currently ignored.
;
; @remarks  ASSUMES pbHaystack is aligned at uAlign.
;
BEGINPROC pgmR3DbgFixedMemScan4Wide4Step
%ifdef ASM_CALL64_MSC
        mov     r10, rdi                ; save it
        mov     rdi, rcx                ; rdi=pbHaystack
        mov     ecx, edx                ; rcx=cbHaystack
        mov     eax, [r8]               ; *(uint32_t *)pvNeedle
%elifdef ASM_CALL64_GCC
        xchg    rcx, rsi                ; rcx=cbHaystack, rsi=cbNeedle
        mov     eax, [rdx]              ; *(uint32_t *)pvNeedle
%elifdef RT_ARCH_X86
        mov     edx, edi                ; save it
        mov     edi, [esp + 04h]        ; pbHaystack
        mov     ecx, [esp + 08h]        ; cbHaystack
        mov     eax, [esp + 0ch]        ; pvNeedle
        mov     eax, [eax]              ; *(uint32_t *)pvNeedle
%else
 %error "Unsupported arch!"
%endif
SEH64_END_PROLOGUE

.continue:
        cmp     ecx, 4
        jb      .return_null
        shr     ecx, 2
        repne   scasd
        jne     .return_null

%ifdef RT_ARCH_AMD64
        ; check more of the needle if we can.
        mov     r11d, 4
.needle_check:
        cmp     cbNeedle, r11d
        je      .return_edi
        cmp     ecx, r11d               ; don't bother converting ecx to bytes.
        jb      .return_edi
        mov     bTmp, [pvNeedle + r11]
        cmp     bTmp, [xDI + r11 - 4]
        jne     .continue
        inc     r11d
        jmp     .needle_check
%endif

.return_edi:
        lea     xAX, [xDI - 4]
.return:
%ifdef ASM_CALL64_MSC
        mov     rdi, r10
%elifdef RT_ARCH_X86
        mov     edi, edx
%endif
        ret

.return_null:
        xor     eax, eax
        jmp     .return
ENDPROC   pgmR3DbgFixedMemScan4Wide4Step


;;
; Searches for a 2 byte needle in steps of 2.
;
; @param    pbHaystack  [msc:rcx,     gcc:rdi, x86:esp+04h]  What to search thru.
; @param    cbHaystack  [msc:edx,     gcc:rsi, x86:esp+08h]  The amount of hay to search.
; @param    pvNeedle    [msc:r8,      gcc:rdx, x86:esp+0ch]  What we're searching for
; @param    cbNeedle    [msc:r9,      gcc:rcx, x86:esp+10h]  Size of what we're searcing for. Currently ignored.
;
; @remarks  ASSUMES pbHaystack is aligned at uAlign.
;
BEGINPROC pgmR3DbgFixedMemScan2Wide2Step
%ifdef ASM_CALL64_MSC
        mov     r10, rdi                ; save it
        mov     rdi, rcx                ; rdi=pbHaystack
        mov     ecx, edx                ; rcx=cbHaystack
        mov     ax, [r8]                ; *(uint16_t *)pvNeedle
%elifdef ASM_CALL64_GCC
        xchg    rcx, rsi                ; rcx=cbHaystack, rsi=cbNeedle
        mov     ax, [rdx]               ; *(uint16_t *)pvNeedle
%elifdef RT_ARCH_X86
        mov     edx, edi                ; save it
        mov     edi, [esp + 04h]        ; pbHaystack
        mov     ecx, [esp + 08h]        ; cbHaystack
        mov     eax, [esp + 0ch]        ; pvNeedle
        mov     ax, [eax]               ; *(uint16_t *)pvNeedle
%else
 %error "Unsupported arch!"
%endif
SEH64_END_PROLOGUE

.continue:
        cmp     ecx, 2
        jb      .return_null
        shr     ecx, 1
        repne   scasw
        jne     .return_null

%ifdef RT_ARCH_AMD64
        ; check more of the needle if we can.
        mov     r11d, 2
.needle_check:
        cmp     cbNeedle, r11d
        je      .return_edi
        cmp     ecx, r11d               ; don't bother converting ecx to bytes.
        jb      .return_edi
        mov     bTmp, [pvNeedle + r11]
        cmp     bTmp, [xDI + r11 - 2]
        jne     .continue
        inc     r11d
        jmp     .needle_check
%endif

.return_edi:
        lea     xAX, [xDI - 2]
.return:
%ifdef ASM_CALL64_MSC
        mov     rdi, r10
%elifdef RT_ARCH_X86
        mov     edi, edx
%endif
        ret

.return_null:
        xor     eax, eax
        jmp     .return
ENDPROC   pgmR3DbgFixedMemScan2Wide2Step


;;
; Searches for a 1 byte needle in steps of 1.
;
; @param    pbHaystack  [msc:rcx,     gcc:rdi, x86:esp+04h]  What to search thru.
; @param    cbHaystack  [msc:edx,     gcc:rsi, x86:esp+08h]  The amount of hay to search.
; @param    pvNeedle    [msc:r8,      gcc:rdx, x86:esp+0ch]  What we're searching for
; @param    cbNeedle    [msc:r9,      gcc:rcx, x86:esp+10h]  Size of what we're searcing for. Currently ignored.
;
BEGINPROC pgmR3DbgFixedMemScan1Wide1Step
%ifdef ASM_CALL64_MSC
        mov     r10, rdi                ; save it
        mov     rdi, rcx                ; rdi=pbHaystack
        mov     ecx, edx                ; rcx=cbHaystack
        mov     al, [r8]                ; *(uint8_t *)pvNeedle
%elifdef ASM_CALL64_GCC
        xchg    rcx, rsi                ; rcx=cbHaystack, rsi=cbNeedle
        mov     al, [rdx]               ; *(uint8_t *)pvNeedle
%elifdef RT_ARCH_X86
        mov     edx, edi                ; save it
        mov     edi, [esp + 04h]        ; pbHaystack
        mov     ecx, [esp + 08h]        ; cbHaystack
        mov     eax, [esp + 0ch]        ; pvNeedle
        mov     al, [eax]               ; *(uint8_t *)pvNeedle
%else
 %error "Unsupported arch!"
%endif
SEH64_END_PROLOGUE

        cmp     ecx, 1
        jb      .return_null
.continue:
        repne   scasb
        jne     .return_null

%ifdef RT_ARCH_AMD64
        ; check more of the needle if we can.
        mov     r11d, 1
.needle_check:
        cmp     cbNeedle, r11d
        je      .return_edi
        cmp     ecx, r11d
        jb      .return_edi
        mov     bTmp, [pvNeedle + r11]
        cmp     bTmp, [xDI + r11 - 1]
        jne     .continue
        inc     r11d
        jmp     .needle_check
%endif

.return_edi:
        lea     xAX, [xDI - 1]
.return:
%ifdef ASM_CALL64_MSC
        mov     rdi, r10
%elifdef RT_ARCH_X86
        mov     edi, edx
%endif
        ret

.return_null:
        xor     eax, eax
%ifdef ASM_CALL64_MSC
        mov     rdi, r10
%elifdef RT_ARCH_X86
        mov     edi, edx
%endif
        ret
ENDPROC   pgmR3DbgFixedMemScan1Wide1Step


;;
; Searches for a 4 byte needle in steps of 1.
;
; @param    pbHaystack  [msc:rcx,     gcc:rdi, x86:esp+04h]  What to search thru.
; @param    cbHaystack  [msc:edx,     gcc:rsi, x86:esp+08h]  The amount of hay to search.
; @param    pvNeedle    [msc:r8,      gcc:rdx, x86:esp+0ch]  What we're searching for
; @param    cbNeedle    [msc:r9,      gcc:rcx, x86:esp+10h]  Size of what we're searcing for. Currently ignored.
;
BEGINPROC pgmR3DbgFixedMemScan4Wide1Step
%ifdef ASM_CALL64_MSC
        mov     r10, rdi                ; save it
        mov     rdi, rcx                ; rdi=pbHaystack
        mov     ecx, edx                ; rcx=cbHaystack
        mov     eax, [r8]               ; *(uint32_t *)pvNeedle
%elifdef ASM_CALL64_GCC
        xchg    rcx, rsi                ; rcx=cbHaystack, rsi=cbNeedle
        mov     eax, [rdx]              ; *(uint32_t *)pvNeedle
%elifdef RT_ARCH_X86
        mov     edx, edi                ; save it
        mov     edi, [esp + 04h]        ; pbHaystack
        mov     ecx, [esp + 08h]        ; cbHaystack
        mov     eax, [esp + 0ch]        ; pvNeedle
        mov     eax, [eax]              ; *(uint32_t *)pvNeedle
%else
 %error "Unsupported arch!"
%endif
SEH64_END_PROLOGUE

        cmp     ecx, 1
        jb      .return_null
.continue:
        repne   scasb
        jne     .return_null
        cmp     ecx, 3
        jb      .return_null
        cmp     eax, [xDI - 1]
        jne     .continue

.return_edi:
        lea     xAX, [xDI - 1]
.return:
%ifdef ASM_CALL64_MSC
        mov     rdi, r10
%elifdef RT_ARCH_X86
        mov     edi, edx
%endif
        ret

.return_null:
        xor     eax, eax
%ifdef ASM_CALL64_MSC
        mov     rdi, r10
%elifdef RT_ARCH_X86
        mov     edi, edx
%endif
        ret
ENDPROC   pgmR3DbgFixedMemScan4Wide1Step

;;
; Searches for a 8 byte needle in steps of 1.
;
; @param    pbHaystack  [msc:rcx,     gcc:rdi, x86:esp+04h]  What to search thru.
; @param    cbHaystack  [msc:edx,     gcc:rsi, x86:esp+08h]  The amount of hay to search.
; @param    pvNeedle    [msc:r8,      gcc:rdx, x86:esp+0ch]  What we're searching for
; @param    cbNeedle    [msc:r9,      gcc:rcx, x86:esp+10h]  Size of what we're searcing for. Currently ignored.
;
; @remarks  The 32-bit version is currently identical to pgmR3DbgFixedMemScan4Wide1Step.
;
BEGINPROC pgmR3DbgFixedMemScan8Wide1Step
%ifdef ASM_CALL64_MSC
        mov     r10, rdi                ; save it
        mov     rdi, rcx                ; rdi=pbHaystack
        mov     ecx, edx                ; rcx=cbHaystack
        mov     rax, [r8]               ; *(uint64_t *)pvNeedle
%elifdef ASM_CALL64_GCC
        xchg    rcx, rsi                ; rcx=cbHaystack, rsi=cbNeedle
        mov     rax, [rdx]              ; *(uint64_t *)pvNeedle
%elifdef RT_ARCH_X86
        mov     edx, edi                ; save it
        mov     edi, [esp + 04h]        ; pbHaystack
        mov     ecx, [esp + 08h]        ; cbHaystack
        mov     eax, [esp + 0ch]        ; pvNeedle
        mov     eax, [eax]              ; *(uint32_t *)pvNeedle
%else
 %error "Unsupported arch!"
%endif
SEH64_END_PROLOGUE

        cmp     ecx, 1
        jb      .return_null
.continue:
        repne   scasb
        jne     .return_null
%ifdef RT_ARCH_AMD64
        cmp     ecx, 7
        jb      .check_smaller
        cmp     rax, [xDI - 1]
        jne     .continue
        jmp     .return_edi
.check_smaller:
%endif
        cmp     ecx, 3
        jb      .return_null
        cmp     eax, [xDI - 1]
        jne     .continue

.return_edi:
        lea     xAX, [xDI - 1]
.return:
%ifdef ASM_CALL64_MSC
        mov     rdi, r10
%elifdef RT_ARCH_X86
        mov     edi, edx
%endif
        ret

.return_null:
        xor     eax, eax
%ifdef ASM_CALL64_MSC
        mov     rdi, r10
%elifdef RT_ARCH_X86
        mov     edi, edx
%endif
        ret
ENDPROC   pgmR3DbgFixedMemScan8Wide1Step

