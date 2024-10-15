; $Id: tstAsmLock-2.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Disassembly testcase - Invalid invariants.
;
; The intention is to check in a binary using the --all-invalid mode
; of tstDisasm-2.
;
; There are some regX, reg/memX variations that aren't tested as
; they would require db'ing out the instructions (12 /r and 13 /r
; for instance).
;

;
; Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

%include "tstAsm.mac"
%if TEST_BITS == 64
; The disassembler doesn't do imm32 right for  64-bit stuff, so disable it for now.
; %define WITH_64_BIT_TESTS_IMM32
 %define WITH_64_BIT_TESTS
%endif

    BITS TEST_BITS

    ;
    ; ADC
    ;
        ; 14 ib         ADC AL, imm8
    lock adc al, byte 8
        ; 15 i[wd]      ADC [ER]AX, immX
    lock adc ax, word 16
    lock adc eax, dword 128
%ifdef WITH_64_BIT_TESTS_IMM32
    lock adc rax, dword 256
    lock adc rax, dword 0cc90cc90h
%endif
        ; 80 /2 ib      ADC reg/mem8, imm8 - with reg dst
    lock adc cl, byte 8
        ; 81 /2 i[wd]   ADC reg/memX, immX - with reg dst
    lock adc cx, word 1000h
    lock adc ecx, dword 100000h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock adc rcx, dword 100000h
%endif
        ; 83 /2 ib      ADC reg/memX, imm8 - with reg dst
    lock adc cx, byte 07fh
    lock adc ecx, byte 07fh
%ifdef WITH_64_BIT_TESTS_IMM32
    lock adc rcx, byte 07fh
%endif

        ; 10 /r         ADC reg/mem8, reg8 - with reg dst
    lock adc cl, bl
        ; 11 /r         ADC reg/memX, regX - with reg dst
    lock adc cx, bx
    lock adc ecx, ebx
%ifdef WITH_64_BIT_TESTS
    lock adc rcx, rbx
%endif
        ; 12 /r         ADC reg8, reg/mem8
    lock adc cl, [0badh]
        ; 13 /r         ADC regX, reg/memX
    lock adc cx, [0badh]
    lock adc ecx, [0badh]
%ifdef WITH_64_BIT_TESTS
    lock adc rcx, [0badh]
%endif

    ;
    ; ADD
    ;
        ; 04 ib         ADD AL, imm8
    lock add al, byte 8
        ; 05 i[wd]      ADD [ER]AX, immX
    lock add ax, word 16
    lock add eax, dword 128
%ifdef WITH_64_BIT_TESTS_IMM32
    lock add rax, dword 256
    lock add rax, dword 0cc90cc90h
%endif
        ; 80 /0 ib      ADD reg/mem8, imm8 - with reg dst
    lock add cl, byte 8
        ; 81 /0 i[wd]   ADD reg/memX, immX - with reg dst
    lock add cx, word 1000h
    lock add ecx, dword 100000h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock add rcx, dword 100000h
%endif
        ; 83 /0 ib      ADD reg/memX, imm8 - with reg dst
    lock add cx, byte 07fh
    lock add ecx, byte 07fh
%ifdef WITH_64_BIT_TESTS
    lock add rcx, byte 07fh
%endif

        ; 00 /r         ADD reg/mem8, reg8 - with reg dst
    lock add cl, bl
        ; 01 /r         ADD reg/memX, regX - with reg dst
    lock add cx, bx
    lock add ecx, ebx
%ifdef WITH_64_BIT_TESTS
    lock add rcx, rbx
%endif
        ; 02 /r         ADD reg8, reg/mem8
    lock add cl, [0badh]
        ; 03 /r         ADD regX, reg/memX
    lock add cx, [0badh]
    lock add ecx, [0badh]
%ifdef WITH_64_BIT_TESTS
    lock add rcx, [0badh]
%endif

    ;
    ; AND
    ;
        ; 24 ib         AND AL, imm8
    lock and al, byte 8
        ; 25 i[wd]      AND [ER]AX, immX
    lock and ax, word 16
    lock and eax, dword 128
%ifdef WITH_64_BIT_TESTS_IMM32
    lock and rax, dword 256
    lock and rax, dword 0cc90cc90h
%endif
        ; 80 /4 ib      AND reg/mem8, imm8 - with reg dst
    lock and cl, byte 8
        ; 81 /4 i[wd]   AND reg/memX, immX - with reg dst
    lock and cx, word 1000h
    lock and ecx, dword 100000h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock and rcx, dword 100000h
%endif
        ; 83 /4 ib      AND reg/memX, imm8 - with reg dst
    lock and cx, byte 07fh
    lock and ecx, byte 07fh
%ifdef WITH_64_BIT_TESTS
    lock and rcx, byte 07fh
%endif

        ; 20 /r         AND reg/mem8, reg8 - with reg dst
    lock and cl, bl
        ; 21 /r         AND reg/memX, regX - with reg dst
    lock and cx, bx
    lock and ecx, ebx
%ifdef WITH_64_BIT_TESTS
    lock and rcx, rbx
%endif
        ; 22 /r         AND reg8, reg/mem8
    lock and cl, [0badh]
        ; 23 /r         AND regX, reg/memX
    lock and cx, [0badh]
    lock and ecx, [0badh]
%ifdef WITH_64_BIT_TESTS
    lock and rcx, [0badh]
%endif

    ;
    ; BTC
    ;
        ; 0f bb /r      BTC reg/memX, regX (X != 8) - with reg dst
    lock btc cx, bx
    lock btc ecx, ebx
%ifdef WITH_64_BIT_TESTS
    lock btc rcx, rbx
    lock btc r8, rbx
    lock btc r10, r8
%endif
        ; 0f ba /7 ib   BTC reg/memX, imm8 (X != 8) - with reg dst
    lock btc cx, 15
    lock btc ecx, 30
%ifdef WITH_64_BIT_TESTS
    lock btc rcx, 60
    lock btc r8, 61
    lock btc r10, 3
%endif

    ;
    ; BTR
    ;
        ; 0f b3 /r      BTR reg/memX, regX (X != 8) - with reg dst
    lock btr cx, bx
    lock btr ecx, ebx
%ifdef WITH_64_BIT_TESTS
    lock btr rcx, rbx
    lock btr r8, rbx
    lock btr r10, r8
%endif
        ; 0f ba /6 ib   BTR reg/memX, imm8 (X != 8) - with reg dst
    lock btr cx, 15
    lock btr ecx, 30
%ifdef WITH_64_BIT_TESTS
    lock btr rcx, 60
    lock btr r8, 61
    lock btr r10, 3
%endif

    ;
    ; BTS
    ;
        ; 0f ab /r      BTS reg/memX, regX (X != 8) - with reg dst
    lock bts cx, bx
    lock bts ecx, ebx
%ifdef WITH_64_BIT_TESTS
    lock bts rcx, rbx
    lock bts r8, rbx
    lock bts r10, r8
%endif
        ; 0f ba /5 ib   BTS reg/memX, imm8 (X != 8) - with reg dst
    lock bts cx, 15
    lock bts ecx, 30
%ifdef WITH_64_BIT_TESTS
    lock bts rcx, 60
    lock bts r8, 61
    lock bts r10, 3
%endif

    ;
    ; CMPXCHG
    ;
        ; 0f b0 /r      CMPXCHG reg8/mem8, regX - with reg dst
    lock cmpxchg bl, cl
        ; 0f b1 /r      CMPXCHG regX/memX, regX - with reg dst
    lock cmpxchg bx, cx
    lock cmpxchg ebx, ecx
%ifdef WITH_64_BIT_TESTS
    lock cmpxchg rbx, rcx
%endif

    ;
    ; CMPXCHG8B
    ; CMPXCHG16B
    ;
        ; all valid.

    ;
    ; DEC
    ;
        ; fe /1         DEC reg8/mem8 - with reg dst
    lock dec bl
        ; ff /1         DEC regX/memX - with reg dst

%if TEST_BITS != 64     ; cannot force these two in 32 and 16 bit mode.
    db 066h, 0f0h, 0ffh, 0cbh
    db 0f0h, 0ffh, 0cbh
%else
    lock dec bx
    lock dec ebx
 %ifdef WITH_64_BIT_TESTS
    lock dec rbx
    lock dec r8
    lock dec r14
 %endif
%endif
%if TEST_BITS != 64
        ; 48 +rw        DEC reg16
    lock dec dx
        ; 48 +rd        DEC reg32
    lock dec edx
%endif

    ;
    ; INC
    ;
        ; fe /1         INC reg8/mem8 - with reg dst
    lock inc bl
        ; ff /1         INC regX/memX - with reg dst

%if TEST_BITS != 64     ; cannot force these two in 32 and 16 bit mode.
    db 066h, 0f0h, 0ffh, 0c3h
    db 0f0h, 0ffh, 0c3h
%else
    lock inc bx
    lock inc ebx
 %ifdef WITH_64_BIT_TESTS
    lock inc rbx
    lock inc r8
    lock inc r14
 %endif
%endif
%if TEST_BITS != 64
        ; 48 +rw        INC reg16
    lock inc dx
        ; 48 +rd        INC reg32
    lock inc edx
%endif

    ;
    ; NEG
    ;
        ; f6 /3         NEG reg8/mem8 - with reg dst
    lock neg bl
        ; f7 /3         NEG regX/memX - with reg dst
    lock neg bx
    lock neg ebx
%ifdef WITH_64_BIT_TESTS
    lock neg rbx
    lock neg r8
    lock neg r14
%endif

    ;
    ; NOT
    ;
        ; f6 /2         NOT reg8/mem8 - with reg dst
    lock not bl
        ; f7 /2         NOT regX/memX - with reg dst
    lock not bx
    lock not ebx
%ifdef WITH_64_BIT_TESTS
    lock not rbx
    lock not r8
    lock not r14
%endif

    ;
    ; OR
    ;
        ; 0C ib         OR AL, imm8
    lock or al, byte 8
        ; 0D i[wd]      OR [ER]AX, immX
    lock or ax, word 16
    lock or eax, dword 128
%ifdef WITH_64_BIT_TESTS_IMM32
    lock or rax, dword 256
    lock or rax, dword 0cc90cc90h
%endif
        ; 80 /1 ib      OR reg/mem8, imm8 - with reg dst
    lock or cl, byte 8
        ; 81 /1 i[wd]   OR reg/memX, immX - with reg dst
    lock or cx, word 1000h
    lock or ecx, dword 100000h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock or rcx, dword 100000h
%endif
        ; 83 /1 ib      OR reg/memX, imm8 - with reg dst
    lock or cx, byte 07fh
    lock or ecx, byte 07fh
%ifdef WITH_64_BIT_TESTS_IMM32
    lock or rcx, byte 07fh
%endif
        ; 08 /r         OR reg/mem8, reg8 - with reg dst
    lock or cl, bl
        ; 09 /r         OR reg/memX, regX - with reg dst
    lock or cx, bx
    lock or ecx, ebx
%ifdef WITH_64_BIT_TESTS
    lock or rcx, rbx
%endif
        ; 0A /r         OR reg8, reg/mem8
    lock or cl, [0badh]
        ; 0B /r         OR regX, reg/memX
    lock or cx, [0badh]
    lock or ecx, [0badh]
%ifdef WITH_64_BIT_TESTS
    lock or rcx, [0badh]
%endif

    ;
    ; SBB
    ;
        ; 1C ib         SBB AL, imm8
    lock sbb al, byte 8
        ; 1D i[wd]      SBB [ER]AX, immX
    lock sbb ax, word 16
    lock sbb eax, dword 128
%ifdef WITH_64_BIT_TESTS_IMM32
    lock sbb rax, dword 256
    lock sbb rax, dword 0cc90cc90h
%endif
        ; 80 /3 ib      SBB reg/mem8, imm8 - with reg dst
    lock sbb cl, byte 8
        ; 81 /3 i[wd]   SBB reg/memX, immX - with reg dst
    lock sbb cx, word 1000h
    lock sbb ecx, dword 100000h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock sbb rcx, dword 100000h
%endif
        ; 83 /3 ib      SBB reg/memX, imm8 - with reg dst
    lock sbb cx, byte 07fh
    lock sbb ecx, byte 07fh
%ifdef WITH_64_BIT_TESTS_IMM32
    lock sbb rcx, byte 07fh
%endif
        ; 18 /r         SBB reg/mem8, reg8 - with reg dst
    lock sbb cl, bl
        ; 19 /r         SBB reg/memX, regX - with reg dst
    lock sbb cx, bx
    lock sbb ecx, ebx
%ifdef WITH_64_BIT_TESTS
    lock sbb rcx, rbx
%endif
        ; 1A /r         SBB reg8, reg/mem8
    lock sbb cl, [0badh]
        ; 1B /r         SBB regX, reg/memX
    lock sbb cx, [0badh]
    lock sbb ecx, [0badh]
%ifdef WITH_64_BIT_TESTS
    lock sbb rcx, [0badh]
%endif

    ;
    ; SUB
    ;
        ; 2C ib         SUB AL, imm8
    lock sub al, byte 8
        ; 2D i[wd]      SUB [ER]AX, immX
    lock sub ax, word 16
    lock sub eax, dword 128
%ifdef WITH_64_BIT_TESTS_IMM32
    lock sub rax, dword 256
    lock sub rax, dword 0cc90cc90h
%endif
        ; 80 /5 ib      SUB reg/mem8, imm8 - with reg dst
    lock sub cl, byte 8
        ; 81 /5 i[wd]   SUB reg/memX, immX - with reg dst
    lock sub cx, word 1000h
    lock sub ecx, dword 100000h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock sub rcx, dword 100000h
%endif
        ; 83 /5 ib      SUB reg/memX, imm8 - with reg dst
    lock sub cx, byte 07fh
    lock sub ecx, byte 07fh
%ifdef WITH_64_BIT_TESTS_IMM32
    lock sub rcx, byte 07fh
%endif
        ; 28 /r         SUB reg/mem8, reg8 - with reg dst
    lock sub cl, bl
        ; 29 /r         SUB reg/memX, regX - with reg dst
    lock sub cx, bx
    lock sub ecx, ebx
%ifdef WITH_64_BIT_TESTS
    lock sub rcx, rbx
%endif
        ; 2A /r         SUB reg8, reg/mem8
    lock sub cl, [0badh]
        ; 2B /r         SUB regX, reg/memX
    lock sub cx, [0badh]
    lock sub ecx, [0badh]
%ifdef WITH_64_BIT_TESTS
    lock sub rcx, [0badh]
%endif

    ;
    ; XADD
    ;
        ; 0f c0 /r      XADD reg/mem8, reg8 - with reg dst
    lock xadd al, bl
        ; 0f c1 /r      XADD reg/memX, immX - with reg dst
    lock xadd cx, bx
    lock xadd ecx, ebx
%ifdef WITH_64_BIT_TESTS
    lock xadd rcx, rbx
    lock xadd r8, rbx
    lock xadd r10, r8
%endif

    ;
    ; XCHG
    ;
    ; Note: The operands can be switched around but the
    ;       encoding is the same.
    ;
        ; 90 +r[wdq]    XCHG [RE]ax, regX
    lock xchg ax, bx
    lock xchg eax, ecx
%ifdef WITH_64_BIT_TESTS
    lock xchg rax, rcx
    lock xchg rax, r10
%endif
        ; 86 /r         XCHG reg/mem8, imm8 - with reg dst
    lock xchg al, bl
%ifdef WITH_64_BIT_TESTS
    lock xchg r10b, cl
    lock xchg r10b, r15b
%endif
        ; 87 /r         XCHG reg/memX, immX - with reg dst
    lock xchg ax, bx
    lock xchg eax, ebx
%ifdef WITH_64_BIT_TESTS_IMM32
    lock xchg rax, rbx
    lock xchg r12, rbx
    lock xchg r14, r8
%endif

    ;
    ; XOR
    ;
        ; 34 ib         XOR AL, imm8
    lock xor al, byte 8
        ; 35 i[wd]      XOR [ER]AX, immX
    lock xor ax, word 16
    lock xor eax, dword 128
%ifdef WITH_64_BIT_TESTS_IMM32
    lock xor rax, dword 256
    lock xor rax, dword 0cc90cc90h
%endif
        ; 80 /6 ib      XOR reg/mem8, imm8 - with reg dst
    lock xor cl, byte 8
        ; 81 /6 i[wd]   XOR reg/memX, immX - with reg dst
    lock xor cx, word 1000h
    lock xor ecx, dword 100000h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock xor rcx, dword 100000h
%endif
        ; 83 /6 ib      XOR reg/memX, imm8 - with reg dst
    lock xor cx, byte 07fh
    lock xor ecx, byte 07fh
%ifdef WITH_64_BIT_TESTS_IMM32
    lock xor rcx, byte 07fh
%endif
        ; 30 /r         XOR reg/mem8, reg8 - with reg dst
    lock xor cl, bl
        ; 31 /r         XOR reg/memX, regX - with reg dst
    lock xor cx, bx
    lock xor ecx, ebx
%ifdef WITH_64_BIT_TESTS
    lock xor rcx, rbx
%endif
        ; 32 /r         XOR reg8, reg/mem8
    lock xor cl, [0badh]
        ; 33 /r         XOR regX, reg/memX
    lock xor cx, [0badh]
    lock xor ecx, [0badh]
%ifdef WITH_64_BIT_TESTS
    lock xor rcx, [0badh]
%endif

