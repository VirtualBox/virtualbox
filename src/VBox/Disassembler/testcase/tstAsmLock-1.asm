; $Id: tstAsmLock-1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Disassembly testcase - Valid lock sequences and related instructions.
;
; This is a build test, that means it will be assembled, disassembled,
; then the disassembly output will be assembled and the new binary will
; compared with the original.
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

; The disassembler doesn't do imm32 right for 64-bit stuff, so disable it for now.
; %define WITH_64_BIT_TESTS_IMM32

; The cmpxchg16b/8b stuff isn't handled correctly in 64-bit mode. In the 8b case
; it could be both yasm and the vbox disassembler. Have to check docs/gas/nasm.
; %define WITH_64_BIT_TESTS_CMPXCHG16B

; Seems there are some issues with the byte, word and dword variants of r8-15.
; Again, this could be yasm issues too...
; %define WITH_64_BIT_TESTS_BORKED_REGS

 %define WITH_64_BIT_TESTS
%endif

    BITS TEST_BITS

    ;
    ; ADC
    ;
        ; 80 /2 ib      ADC reg/mem8, imm8 - sans reg dst
    lock adc byte [1000h], byte 8
    lock adc byte [xBX], byte 8
    lock adc byte [xDI], byte 8
        ; 81 /2 i[wd]   ADC reg/memX, immX - sans reg dst
    lock adc word [1000h], word 090cch
    lock adc word [xBX], word 090cch
    lock adc word [xDI], word 090cch
    lock adc dword [1000h], dword 0cc90cc90h
    lock adc dword [xBX], dword 0cc90cc90h
    lock adc dword [xDI], dword 0cc90cc90h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock adc qword [1000h], dword 0cc90cc90h
    lock adc qword [rbx], dword 0cc90cc90h
    lock adc qword [rdi], dword 0cc90cc90h
    lock adc qword [r9], dword 0cc90cc90h
%endif
        ; 83 /2 ib      ADC reg/memX, imm8 - sans reg dst
    lock adc word [1000h], byte 07fh
    lock adc word [xBX], byte 07fh
    lock adc word [xDI], byte 07fh
    lock adc dword [1000h], byte 07fh
    lock adc dword [xBX], byte 07fh
    lock adc dword [xDI], byte 07fh
%ifdef WITH_64_BIT_TESTS
    lock adc qword [1000h], byte 07fh
    lock adc qword [rbx], byte 07fh
    lock adc qword [rdi], byte 07fh
    lock adc qword [r10], byte 07fh
%endif

        ; 10 /r         ADC reg/mem8, reg8 - sans reg dst
    lock adc byte [1000h], bl
    lock adc byte [xBX], bl
    lock adc byte [xSI], bl
        ; 11 /r         ADC reg/memX, regX - sans reg dst
    lock adc word [1000h], bx
    lock adc word [xBX], bx
    lock adc word [xSI], bx
    lock adc dword [1000h], ebx
    lock adc dword [xBX], ebx
    lock adc dword [xSI], ebx
%ifdef WITH_64_BIT_TESTS
    lock adc qword [1000h], rbx
    lock adc qword [rbx], rbx
    lock adc qword [rsi], rbx
    lock adc qword [r11], rbx
%endif

    ;
    ; ADD
    ;
        ; 80 /0 ib      ADD reg/mem8, imm8 - sans reg dst
    lock add byte [1000h], byte 8
    lock add byte [xBX], byte 8
    lock add byte [xDI], byte 8
        ; 81 /0 i[wd]   ADD reg/memX, immX - sans reg dst
    lock add word [1000h], word 090cch
    lock add word [xBX], word 090cch
    lock add word [xDI], word 090cch
    lock add dword [1000h], dword 0cc90cc90h
    lock add dword [xBX], dword 0cc90cc90h
    lock add dword [xDI], dword 0cc90cc90h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock add qword [1000h], dword 0cc90cc90h
    lock add qword [rbx], dword 0cc90cc90h
    lock add qword [rdi], dword 0cc90cc90h
    lock add qword [r9], dword 0cc90cc90h
%endif
        ; 83 /0 ib      ADD reg/memX, imm8 - sans reg dst
    lock add word [1000h], byte 07fh
    lock add word [xBX], byte 07fh
    lock add word [xDI], byte 07fh
    lock add dword [1000h], byte 07fh
    lock add dword [xBX], byte 07fh
    lock add dword [xDI], byte 07fh
%ifdef WITH_64_BIT_TESTS
    lock add qword [1000h], byte 07fh
    lock add qword [rbx], byte 07fh
    lock add qword [rdi], byte 07fh
    lock add qword [r10], byte 07fh
%endif

        ; 00 /r         ADD reg/mem8, reg8 - sans reg dst
    lock add byte [1000h], bl
    lock add byte [xBX], bl
    lock add byte [xSI], bl
        ; 01 /r         ADD reg/memX, regX - sans reg dst
    lock add word [1000h], bx
    lock add word [xBX], bx
    lock add word [xSI], bx
    lock add dword [1000h], ebx
    lock add dword [xBX], ebx
    lock add dword [xSI], ebx
%ifdef WITH_64_BIT_TESTS
    lock add qword [1000h], rbx
    lock add qword [rbx], rbx
    lock add qword [rsi], rbx
    lock add qword [r11], rbx
%endif

    ;
    ; AND
    ;
        ; 80 /4 ib      AND reg/mem8, imm8 - sans reg dst
    lock and byte [1000h], byte 8
    lock and byte [xBX], byte 8
    lock and byte [xDI], byte 8
        ; 81 /4 i[wd]   AND reg/memX, immX - sans reg dst
    lock and word [1000h], word 090cch
    lock and word [xBX], word 090cch
    lock and word [xDI], word 090cch
    lock and dword [1000h], dword 0cc90cc90h
    lock and dword [xBX], dword 0cc90cc90h
    lock and dword [xDI], dword 0cc90cc90h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock and qword [1000h], dword 0cc90cc90h
    lock and qword [rbx], dword 0cc90cc90h
    lock and qword [rdi], dword 0cc90cc90h
    lock and qword [r9], dword 0cc90cc90h
%endif
        ; 83 /4 ib      AND reg/memX, imm8 - sans reg dst
    lock and word [1000h], byte 07fh
    lock and word [xBX], byte 07fh
    lock and word [xDI], byte 07fh
    lock and dword [1000h], byte 07fh
    lock and dword [xBX], byte 07fh
    lock and dword [xDI], byte 07fh
%ifdef WITH_64_BIT_TESTS
    lock and qword [1000h], byte 07fh
    lock and qword [rbx], byte 07fh
    lock and qword [rdi], byte 07fh
    lock and qword [r10], byte 07fh
%endif

        ; 20 /r         AND reg/mem8, reg8 - sans reg dst
    lock and byte [1000h], bl
    lock and byte [xBX], bl
    lock and byte [xSI], bl
        ; 21 /r         AND reg/memX, regX - sans reg dst
    lock and word [1000h], bx
    lock and word [xBX], bx
    lock and word [xSI], bx
    lock and dword [1000h], ebx
    lock and dword [xBX], ebx
    lock and dword [xSI], ebx
%ifdef WITH_64_BIT_TESTS
    lock and qword [1000h], rbx
    lock and qword [rbx], rbx
    lock and qword [rsi], rbx
    lock and qword [r11], rbx
%endif

    ;
    ; BTC
    ;
        ; 0f bb /r      BTC reg/memX, regX (X != 8) - sans reg dst
    lock btc word [20cch], bx
    lock btc word [xBX], bx
    lock btc word [xDI], bx
    lock btc dword [20cch], ebx
    lock btc dword [xBX], ebx
    lock btc dword [xDI], ebx
%ifdef WITH_64_BIT_TESTS
    lock btc qword [20cch], rbx
    lock btc qword [rdx], rbx
    lock btc qword [rdi], r10
    lock btc qword [r8], r12
%endif
        ; 0f ba /7 ib   BTC reg/memX, imm8 (X != 8) - sans reg dst
    lock btc word [20cch], 15
    lock btc word [xBX], 15
    lock btc word [xDI], 15
    lock btc dword [20cch], 30
    lock btc dword [xBX], 30
    lock btc dword [xDI], 30
%ifdef WITH_64_BIT_TESTS
    lock btc qword [20cch], 60
    lock btc qword [rdx], 60
    lock btc qword [rdi], 60
    lock btc qword [r9], 60
    lock btc qword [r12], 60
%endif

    ;
    ; BTR
    ;
        ; 0f b3 /r      BTR reg/memX, regX (X != 8) - sans reg dst
    lock btr word [20cch], bx
    lock btr word [xBX], bx
    lock btr word [xDI], bx
    lock btr dword [20cch], ebx
    lock btr dword [xBX], ebx
    lock btr dword [xDI], ebx
%ifdef WITH_64_BIT_TESTS
    lock btr qword [20cch], rbx
    lock btr qword [rdx], rbx
    lock btr qword [rdi], r10
    lock btr qword [r8], r12
%endif
        ; 0f ba /6 ib   BTR reg/memX, imm8 (X != 8) - sans reg dst
    lock btr word [20cch], 15
    lock btr word [xBX], 15
    lock btr word [xDI], 15
    lock btr dword [20cch], 30
    lock btr dword [xBX], 30
    lock btr dword [xDI], 30
%ifdef WITH_64_BIT_TESTS
    lock btr qword [20cch], 60
    lock btr qword [rdx], 60
    lock btr qword [rdi], 60
    lock btr qword [r9], 60
    lock btr qword [r12], 60
%endif

    ;
    ; BTS
    ;
        ; 0f ab /r      BTS reg/memX, regX (X != 8) - sans reg dst
    lock bts word [20cch], bx
    lock bts word [xBX], bx
    lock bts word [xDI], bx
    lock bts dword [20cch], ebx
    lock bts dword [xBX], ebx
    lock bts dword [xDI], ebx
%if TEST_BITS == 64
    lock bts qword [20cch], rbx
    lock bts qword [rdx], rbx
    lock bts qword [rdi], r10
    lock bts qword [r8], r12
%endif
        ; 0f ba /5 ib   BTS reg/memX, imm8 (X != 8) - sans reg dst
    lock bts word [20cch], 15
    lock bts word [xBX], 15
    lock bts word [xDI], 15
    lock bts dword [20cch], 30
    lock bts dword [xBX], 30
    lock bts dword [xDI], 30
%if TEST_BITS == 64
    lock bts qword [20cch], 60
    lock bts qword [rdx], 60
    lock bts qword [rdi], 60
    lock bts qword [r9], 60
    lock bts qword [r12], 60
%endif

    ;
    ; CMPXCHG
    ;
        ; 0f b0 /r      CMPXCHG reg8/mem8, regX - with reg dst
    lock cmpxchg byte [30cch], cl
    lock cmpxchg byte [xBX], cl
    lock cmpxchg byte [xSI], cl
        ; 0f b1 /r      CMPXCHG regX/memX, regX - with reg dst
    lock cmpxchg word [30cch], cx
    lock cmpxchg word [xBX], cx
    lock cmpxchg word [xSI], cx
    lock cmpxchg dword [30cch], ecx
    lock cmpxchg dword [xBX], ecx
    lock cmpxchg dword [xSI], ecx
%ifdef WITH_64_BIT_TESTS
    lock cmpxchg qword [30cch], rcx
    lock cmpxchg qword [xBX], rcx
    lock cmpxchg qword [xSI], rcx
    lock cmpxchg qword [rdi], r8
    lock cmpxchg qword [r12], r9
%endif

    ;
    ; CMPXCHG8B
    ; CMPXCHG16B
    ;
    ;; @todo get back to cmpxchg8b and cmpxchg16b.
    lock cmpxchg8b qword [1000h]
    lock cmpxchg8b qword [xDI]
    lock cmpxchg8b qword [xDI+xBX]
%ifdef WITH_64_BIT_TESTS_CMPXCHG16B
    lock cmpxchg16b [1000h]
    lock cmpxchg16b [xDI]
    lock cmpxchg16b [xDI+xBX]
%endif

    ;
    ; DEC
    ;
        ; fe /1         DEC reg8/mem8 - sans reg dst
    lock dec byte [40cch]
    lock dec byte [xBX]
    lock dec byte [xSI]
        ; ff /1         DEC regX/memX - sans reg dst
    lock dec word [40cch]
    lock dec word [xBX]
    lock dec word [xSI]
    lock dec dword [40cch]
    lock dec dword [xBX]
    lock dec dword [xSI]
%ifdef WITH_64_BIT_TESTS
    lock dec qword [40cch]
    lock dec qword [xBX]
    lock dec qword [xSI]
    lock dec qword [r8]
    lock dec qword [r12]
%endif

    ;
    ; INC
    ;
        ; fe /0         INC reg8/mem8 - sans reg dst
    lock inc byte [40cch]
    lock inc byte [xBX]
    lock inc byte [xSI]
        ; ff /0         INC regX/memX - sans reg dst
    lock inc word [40cch]
    lock inc word [xBX]
    lock inc word [xSI]
    lock inc dword [40cch]
    lock inc dword [xBX]
    lock inc dword [xSI]
%ifdef WITH_64_BIT_TESTS
    lock inc qword [40cch]
    lock inc qword [xBX]
    lock inc qword [xSI]
    lock inc qword [r8]
    lock inc qword [r12]
%endif

    ;
    ; NEG
    ;
        ; f6 /3         NEG reg8/mem8 - sans reg dst
    lock neg byte [40cch]
    lock neg byte [xBX]
    lock neg byte [xSI]
        ; f7 /3         NEG regX/memX - sans reg dst
    lock neg word [40cch]
    lock neg word [xBX]
    lock neg word [xSI]
    lock neg dword [40cch]
    lock neg dword [xBX]
    lock neg dword [xSI]
%ifdef WITH_64_BIT_TESTS
    lock neg qword [40cch]
    lock neg qword [xBX]
    lock neg qword [xSI]
    lock neg qword [r8]
    lock neg qword [r12]
%endif

    ;
    ; NOT
    ;
        ; f6 /2         NOT reg8/mem8 - sans reg dst
    lock not byte [40cch]
    lock not byte [xBX]
    lock not byte [xSI]
        ; f7 /2         NOT regX/memX - sans reg dst
    lock not word [40cch]
    lock not word [xBX]
    lock not word [xSI]
    lock not dword [40cch]
    lock not dword [xBX]
    lock not dword [xSI]
%ifdef WITH_64_BIT_TESTS
    lock not qword [40cch]
    lock not qword [xBX]
    lock not qword [xSI]
    lock not qword [r8]
    lock not qword [r12]
%endif

    ;
    ; OR
    ;
        ; 80 /1 ib      OR reg/mem8, imm8 - sans reg dst
    lock or byte [1000h], byte 8
    lock or byte [xBX], byte 8
    lock or byte [xDI], byte 8
        ; 81 /1 i[wd]   OR reg/memX, immX - sans reg dst
    lock or word [1000h], word 090cch
    lock or word [xBX], word 090cch
    lock or word [xDI], word 090cch
    lock or dword [1000h], dword 0cc90cc90h
    lock or dword [xBX], dword 0cc90cc90h
    lock or dword [xDI], dword 0cc90cc90h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock or qword [1000h], dword 0cc90cc90h
    lock or qword [rbx], dword 0cc90cc90h
    lock or qword [rdi], dword 0cc90cc90h
    lock or qword [r9], dword 0cc90cc90h
%endif
        ; 83 /1 ib      OR reg/memX, imm8 - sans reg dst
    lock or word [1000h], byte 07fh
    lock or word [xBX], byte 07fh
    lock or word [xDI], byte 07fh
    lock or dword [1000h], byte 07fh
    lock or dword [xBX], byte 07fh
    lock or dword [xDI], byte 07fh
%ifdef WITH_64_BIT_TESTS
    lock or qword [1000h], byte 07fh
    lock or qword [rbx], byte 07fh
    lock or qword [rdi], byte 07fh
    lock or qword [r10], byte 07fh
%endif

        ; 08 /r         OR reg/mem8, reg8 - sans reg dst
    lock or byte [1000h], bl
    lock or byte [xBX], bl
    lock or byte [xSI], bl
        ; 09 /r         OR reg/memX, regX - sans reg dst
    lock or word [1000h], bx
    lock or word [xBX], bx
    lock or word [xSI], bx
    lock or dword [1000h], ebx
    lock or dword [xBX], ebx
    lock or dword [xSI], ebx
%ifdef WITH_64_BIT_TESTS
    lock or qword [1000h], rbx
    lock or qword [rbx], rbx
    lock or qword [rsi], rbx
    lock or qword [r11], rbx
%endif

    ;
    ; SBB
    ;
        ; 80 /3 ib      SBB reg/mem8, imm8 - sans reg dst
    lock sbb byte [1000h], byte 8
    lock sbb byte [xBX], byte 8
    lock sbb byte [xDI], byte 8
        ; 81 /3 i[wd]   SBB reg/memX, immX - sans reg dst
    lock sbb word [1000h], word 090cch
    lock sbb word [xBX], word 090cch
    lock sbb word [xDI], word 090cch
    lock sbb dword [1000h], dword 0cc90cc90h
    lock sbb dword [xBX], dword 0cc90cc90h
    lock sbb dword [xDI], dword 0cc90cc90h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock sbb qword [1000h], dword 0cc90cc90h
    lock sbb qword [rbx], dword 0cc90cc90h
    lock sbb qword [rdi], dword 0cc90cc90h
    lock sbb qword [r9], dword 0cc90cc90h
%endif
        ; 83 /3 ib      SBB reg/memX, imm8 - sans reg dst
    lock sbb word [1000h], byte 07fh
    lock sbb word [xBX], byte 07fh
    lock sbb word [xDI], byte 07fh
    lock sbb dword [1000h], byte 07fh
    lock sbb dword [xBX], byte 07fh
    lock sbb dword [xDI], byte 07fh
%ifdef WITH_64_BIT_TESTS
    lock sbb qword [1000h], byte 07fh
    lock sbb qword [rbx], byte 07fh
    lock sbb qword [rdi], byte 07fh
    lock sbb qword [r10], byte 07fh
%endif

        ; 18 /r         SBB reg/mem8, reg8 - sans reg dst
    lock sbb byte [1000h], bl
    lock sbb byte [xBX], bl
    lock sbb byte [xSI], bl
        ; 19 /r         SBB reg/memX, regX - sans reg dst
    lock sbb word [1000h], bx
    lock sbb word [xBX], bx
    lock sbb word [xSI], bx
    lock sbb dword [1000h], ebx
    lock sbb dword [xBX], ebx
    lock sbb dword [xSI], ebx
%ifdef WITH_64_BIT_TESTS
    lock sbb qword [1000h], rbx
    lock sbb qword [rbx], rbx
    lock sbb qword [rsi], rbx
    lock sbb qword [r11], rbx
%endif

    ;
    ; SUB
    ;
        ; 80 /5 ib      SUB reg/mem8, imm8 - sans reg dst
    lock sub byte [1000h], byte 8
    lock sub byte [xBX], byte 8
    lock sub byte [xDI], byte 8
        ; 81 /5 i[wd]   SUB reg/memX, immX - sans reg dst
    lock sub word [1000h], word 090cch
    lock sub word [xBX], word 090cch
    lock sub word [xDI], word 090cch
    lock sub dword [1000h], dword 0cc90cc90h
    lock sub dword [xBX], dword 0cc90cc90h
    lock sub dword [xDI], dword 0cc90cc90h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock sub qword [1000h], dword 0cc90cc90h
    lock sub qword [rbx], dword 0cc90cc90h
    lock sub qword [rdi], dword 0cc90cc90h
    lock sub qword [r9], dword 0cc90cc90h
%endif
        ; 83 /5 ib      SUB reg/memX, imm8 - sans reg dst
    lock sub word [1000h], byte 07fh
    lock sub word [xBX], byte 07fh
    lock sub word [xDI], byte 07fh
    lock sub dword [1000h], byte 07fh
    lock sub dword [xBX], byte 07fh
    lock sub dword [xDI], byte 07fh
%ifdef WITH_64_BIT_TESTS
    lock sub qword [1000h], byte 07fh
    lock sub qword [rbx], byte 07fh
    lock sub qword [rdi], byte 07fh
    lock sub qword [r10], byte 07fh
%endif

        ; 28 /r         SUB reg/mem8, reg8 - sans reg dst
    lock sub byte [1000h], bl
    lock sub byte [xBX], bl
    lock sub byte [xSI], bl
        ; 29 /r         SUB reg/memX, regX - sans reg dst
    lock sub word [1000h], bx
    lock sub word [xBX], bx
    lock sub word [xSI], bx
    lock sub dword [1000h], ebx
    lock sub dword [xBX], ebx
    lock sub dword [xSI], ebx
%ifdef WITH_64_BIT_TESTS
    lock sub qword [1000h], rbx
    lock sub qword [rbx], rbx
    lock sub qword [rsi], rbx
    lock sub qword [r11], rbx
%endif

    ;
    ; XADD
    ;
        ; 0f c0 /r      XADD reg/mem8, reg8 - sans reg dst
    lock xadd byte [1000h], bl
    lock xadd byte [xBX], bl
    lock xadd byte [xDI], bl
        ; 0f c1 /r      XADD reg/memX, immX - sans reg dst
    lock xadd word [1000h], cx
    lock xadd word [xBX], cx
    lock xadd word [xDI], cx
    lock xadd dword [1000h], edx
    lock xadd dword [xBX], edx
    lock xadd dword [xDI], edx
%ifdef WITH_64_BIT_TESTS
    lock xadd qword [1000h], rbx
    lock xadd qword [xBX], rbx
    lock xadd qword [xDI], rbx
    lock xadd qword [r8], rbx
    lock xadd qword [r12], r8
%endif

    ;
    ; XCHG
    ;
    ; Note: The operands can be switched around but the
    ;       encoding is the same.
    ;
        ; 86 /r         XCHG reg/mem8, imm8 - sans reg dst
    lock xchg byte [80cch], bl
    lock xchg byte [xBX], bl
    lock xchg byte [xSI], bl
%ifdef WITH_64_BIT_TESTS_BORKED_REGS
    lock xchg byte [rsi], r15b      ; turns into r15l which yasm doesn't grok
    lock xchg byte [r8], r15b       ; ditto
%endif
        ; 87 /r         XCHG reg/memX, immX - sans reg dst
    lock xchg word [80cch], bx
    lock xchg word [xBX], bx
    lock xchg word [xSI], bx
    lock xchg dword [80cch], ebx
    lock xchg dword [xBX], ebx
    lock xchg dword [xSI], ebx
%ifdef WITH_64_BIT_TESTS
    lock xchg qword [80cch], rbx
    lock xchg qword [xBX], rbx
    lock xchg qword [xSI], rbx
    lock xchg qword [xSI], r15
 %ifdef WITH_64_BIT_TESTS_BORKED_REGS
    lock xchg dword [xSI], r15d     ; turns into rdi
    lock xchg word [xSI], r15w      ; turns into rdi
 %endif
%endif

    ;
    ; XOR
    ;
        ; 80 /6 ib      XOR reg/mem8, imm8 - sans reg dst
    lock xor byte [1000h], byte 8
    lock xor byte [xBX], byte 8
    lock xor byte [xDI], byte 8
        ; 81 /6 i[wd]   XOR reg/memX, immX - sans reg dst
    lock xor word [1000h], word 090cch
    lock xor word [xBX], word 090cch
    lock xor word [xDI], word 090cch
    lock xor dword [1000h], dword 0cc90cc90h
    lock xor dword [xBX], dword 0cc90cc90h
    lock xor dword [xDI], dword 0cc90cc90h
%ifdef WITH_64_BIT_TESTS_IMM32
    lock xor qword [1000h], dword 0cc90cc90h
    lock xor qword [rbx], dword 0cc90cc90h
    lock xor qword [rdi], dword 0cc90cc90h
    lock xor qword [r9], dword 0cc90cc90h
%endif
        ; 83 /6 ib      XOR reg/memX, imm8 - sans reg dst
    lock xor word [1000h], byte 07fh
    lock xor word [xBX], byte 07fh
    lock xor word [xDI], byte 07fh
    lock xor dword [1000h], byte 07fh
    lock xor dword [xBX], byte 07fh
    lock xor dword [xDI], byte 07fh
%ifdef WITH_64_BIT_TESTS
    lock xor qword [1000h], byte 07fh
    lock xor qword [rbx], byte 07fh
    lock xor qword [rdi], byte 07fh
    lock xor qword [r10], byte 07fh
%endif

        ; 30 /r         XOR reg/mem8, reg8 - sans reg dst
    lock xor byte [1000h], bl
    lock xor byte [xBX], bl
    lock xor byte [xSI], bl
        ; 31 /r         XOR reg/memX, regX - sans reg dst
    lock xor word [1000h], bx
    lock xor word [xBX], bx
    lock xor word [xSI], bx
    lock xor dword [1000h], ebx
    lock xor dword [xBX], ebx
    lock xor dword [xSI], ebx
%ifdef WITH_64_BIT_TESTS
    lock xor qword [1000h], rbx
    lock xor qword [rbx], rbx
    lock xor qword [rsi], rbx
    lock xor qword [r11], rbx
%endif

