; $Id: bs3-cpu-basic-3-high-asm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - bs3-cpu-basic-3-high - Assembly code.
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


;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "bs3kit.mac"


;*********************************************************************************************************************************
;*  Defined Constants And Macros                                                                                                 *
;*********************************************************************************************************************************
%define LEA_RAX 01111111111111110h
%define LEA_RCX 02222222222222202h
%define LEA_RDX 03333333333333033h
%define LEA_RBX 04444444444440444h
%define LEA_RSP 058595a5d51525356h
%define LEA_RBP 05555555555555551h
%define LEA_RSI 06666666666666616h
%define LEA_RDI 07777777777777177h
%define LEA_R8  08888888888881888h
%define LEA_R9  09999999999999992h
%define LEA_R10 0aaaaaaaaaaaaaa2ah
%define LEA_R11 0bbbbbbbbbbbbb2bbh
%define LEA_R12 0cccccccccccc2ccch
%define LEA_R13 0ddddddddddddddd3h
%define LEA_R14 0eeeeeeeeeeeeee3eh
%define LEA_R15 0fffffffffffff3ffh


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
extern _Bs3Text16_StartOfSegment


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BS3_BEGIN_DATA64

;; Place to save esp/rsp when doing LEA variations involving esp/rsp.
BS3_GLOBAL_DATA g_bs3CpuBasic3_lea_rsp, 8
        dq  0

%ifdef WITH_TRACING
BS3_GLOBAL_DATA g_bs3CpuBasic3_lea_trace, 4
        dd  0
%endif

;
; Set up LM64 environment
;
%define TMPL_MODE BS3_MODE_LM64
%include "bs3kit-template-header.mac"


;;
; Tests 64-bit addressing using the LEA instruction.
;
TMPL_BEGIN_TEXT
export  BS3_CMN_NM(bs3CpuBasic3_lea_64)
BS3_PROC_BEGIN_CMN bs3CpuBasic3_lea_64, BS3_PBC_NEAR
        push    rax
        push    rcx
        push    rdx
        push    rbx
        push    rbp
        push    rsi
        push    rdi
        push    r8
        push    r9
        push    r10
        push    r11
        push    r12
        push    r13
        push    r14
        push    r15
.test_label:
        mov     [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))], rsp

        ;
        ; We generate all the permutations using a python script, as nasm ends
        ; up consuming >19 GiB of memory running the preprocessor code below.
        ; It also took forever (3+ min).
        ;
%if 1
 %include "bs3-cpu-basic-3-high-lea64.inc"
%else
        ;
        ; Loop thru all the modr/m memory encodings.
        ;
%assign iMod         0
%rep   3
 %assign iDstReg    0
 %rep   16
  %assign iMemReg   0
  %rep   16
   %assign iDstReg_Value %sel(iDstReg+1, LEA_RAX, LEA_RCX, LEA_RDX, LEA_RBX, LEA_RSP, LEA_RBP, LEA_RSI, LEA_RDI, \
                                         LEA_R8, LEA_R9, LEA_R10, LEA_R11, LEA_R12, LEA_R13, LEA_R14, LEA_R15)

   %if (iMemReg & 7) == 4
        ;
        ; SIB.
        ;
    %assign iBase       0
    %rep 16
     %if (iBase & 7) == 5 && iMod == 0
      %assign iBase_Value 0
     %else
      %assign iBase_Value %sel(iBase+1, LEA_RAX, LEA_RCX, LEA_RDX, LEA_RBX, LEA_RSP, LEA_RBP, LEA_RSI, LEA_RDI, \
                                        LEA_R8, LEA_R9, LEA_R10, LEA_R11, LEA_R12, LEA_R13, LEA_R14, LEA_R15)
     %endif

     %assign iIndex     0
     %assign cShift     0 ; we don't have enough room for checking all the shifts.
     %rep 16
      %assign iIndex_Value %sel(iIndex+1, LEA_RAX, LEA_RCX, LEA_RDX, LEA_RBX, 0, LEA_RBP, LEA_RSI, LEA_RDI, \
                                          LEA_R8, LEA_R9, LEA_R10, LEA_R11, LEA_R12, LEA_R13, LEA_R14, LEA_R15)

        ;
        ; We don't test all shift combinations, there just isn't enough space
        ; in the image for that.
        ;
      %assign cShiftLoops 4
      %rep cShiftLoops

        ;
        ; LEA+SIB w/ 64-bit operand size and 64-bit address size.
        ;
       %ifdef WITH_TRACING
        mov     dword [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_trace))], $
       %endif
        call    .load_regs
       %if iBase == 4 || iDstReg == 4
        mov     rsp, LEA_RSP
       %endif

        ; lea
       %assign  iValue  iBase_Value + (iIndex_Value << cShift)
        db      X86_OP_REX_W | ((iBase & 8) >> 3) | ((iIndex & 8) >> 2) | ((iDstReg & 8) >> 1)
        db      8dh, X86_MODRM_MAKE(iMod, iDstReg & 7, iMemReg & 7), X86_SIB_MAKE(iBase & 7, iIndex & 7, cShift)
       %if iMod == X86_MOD_MEM1
        db      -128
        %assign iValue  iValue - 128
       %elif iMod == X86_MOD_MEM4 || (iMod == 0 && (iBase & 7) == 5)
        dd      -07ffef3ffh
        %assign iValue  iValue - 07ffef3ffh
       %endif

        ; cmp iDstReg, iValue
       %if iValue <= 07fffffffh && iValue >= -080000000h
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      81h, X86_MODRM_MAKE(X86_MOD_REG, 7, iDstReg & 7)
        dd      iValue & 0ffffffffh
       %elif iDstReg != X86_GREG_xAX
        mov     rax, iValue
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, X86_GREG_xAX, iDstReg & 7)
       %else
        mov     rcx, iValue
        cmp     rax, rcx
       %endif
       %if iBase == 4 || iDstReg == 4
        mov     rdx, rsp
        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
       %endif
        jz      $+3
        int3

        ;
        ; LEA+SIB w/ 64-bit operand size and 32-bit address size.
        ;
       %ifdef WITH_TRACING
        mov     dword [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_trace))], $
       %endif
        call    .load_regs
       %if iBase == 4 || iDstReg == 4
        mov     rsp, LEA_RSP
       %endif

        ; lea
       %assign  iValue  iBase_Value + (iIndex_Value << cShift)
        db      X86_OP_PRF_SIZE_ADDR
        db      X86_OP_REX_W | ((iBase & 8) >> 3) | ((iIndex & 8) >> 2) | ((iDstReg & 8) >> 1)
        db      8dh, X86_MODRM_MAKE(iMod, iDstReg & 7, iMemReg & 7), X86_SIB_MAKE(iBase & 7, iIndex & 7, cShift)
       %if iMod == X86_MOD_MEM1
        db      +127
        %assign iValue  iValue + 127
       %elif iMod == X86_MOD_MEM4 || (iMod == 0 && (iBase & 7) == 5)
        dd      +0356786aeh
        %assign iValue  iValue + 0356786aeh
       %endif
       %assign  iValue  (iValue & 0ffffffffh)

        ; cmp iDstReg, iValue
       %if iValue <= 07fffffffh && iValue >= -080000000h
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      81h, X86_MODRM_MAKE(X86_MOD_REG, 7, iDstReg & 7)
        dd      iValue & 0ffffffffh
       %elif iDstReg != X86_GREG_xAX
        mov     rax, iValue
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, X86_GREG_xAX, iDstReg & 7)
       %else
        mov     rcx, iValue
        cmp     rax, rcx
       %endif
       %if iBase == 4 || iDstReg == 4
        mov     rdx, rsp
        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
       %endif
        jz      $+3
        int3

        ;
        ; LEA+SIB w/ 32-bit operand size and 64-bit address size.
        ;
       %ifdef WITH_TRACING
        mov     dword [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_trace))], $
       %endif
        call    .load_regs
       %if iBase == 4 || iDstReg == 4
        mov     rsp, LEA_RSP
       %endif

        ; lea
       %assign  iValue  iBase_Value + (iIndex_Value << cShift)
       %if (iBase | iIndex | iDstReg) & 8
        db      X86_OP_REX | ((iBase & 8) >> 3) | ((iIndex & 8) >> 2) | ((iDstReg & 8) >> 1)
       %endif
        db      8dh, X86_MODRM_MAKE(iMod, iDstReg & 7, iMemReg & 7), X86_SIB_MAKE(iBase & 7, iIndex & 7, cShift)
       %if iMod == X86_MOD_MEM1
        db      -18
        %assign iValue  iValue - 18
       %elif iMod == X86_MOD_MEM4 || (iMod == 0 && (iBase & 7) == 5)
        dd      -07fef3ffh
        %assign iValue  iValue - 07fef3ffh
       %endif
       %assign  iValue  (iValue & 0ffffffffh)

        ; cmp iDstReg, iValue
       %if iValue <= 07fffffffh && iValue >= -080000000h
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      81h, X86_MODRM_MAKE(X86_MOD_REG, 7, iDstReg & 7)
        dd      iValue & 0ffffffffh
       %elif iDstReg != X86_GREG_xAX
        mov     rax, iValue
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, X86_GREG_xAX, iDstReg & 7)
       %else
        mov     rcx, iValue
        cmp     rax, rcx
       %endif
       %if iBase == 4 || iDstReg == 4
        mov     rdx, rsp
        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
       %endif
        jz      $+3
        int3

        ;
        ; LEA+SIB w/ 32-bit operand size and 32-bit address size.
        ;
       %ifdef WITH_TRACING
        mov     dword [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_trace))], $
       %endif
        call    .load_regs
       %if iBase == 4 || iDstReg == 4
        mov     rsp, LEA_RSP
       %endif

        ; lea
       %assign  iValue  iBase_Value + (iIndex_Value << cShift)
        db      X86_OP_PRF_SIZE_ADDR
       %if (iBase | iIndex | iDstReg) & 8
        db      X86_OP_REX | ((iBase & 8) >> 3) | ((iIndex & 8) >> 2) | ((iDstReg & 8) >> 1)
       %endif
        db      8dh, X86_MODRM_MAKE(iMod, iDstReg & 7, iMemReg & 7), X86_SIB_MAKE(iBase & 7, iIndex & 7, cShift)
       %if iMod == X86_MOD_MEM1
        db      +17
        %assign iValue  iValue + 17
       %elif iMod == X86_MOD_MEM4 || (iMod == 0 && (iBase & 7) == 5)
        dd      +0356786h
        %assign iValue  iValue + 0356786h
       %endif
       %assign  iValue  (iValue & 0ffffffffh)

        ; cmp iDstReg, iValue
       %if iValue <= 07fffffffh && iValue >= -080000000h
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      81h, X86_MODRM_MAKE(X86_MOD_REG, 7, iDstReg & 7)
        dd      iValue & 0ffffffffh
       %elif iDstReg != X86_GREG_xAX
        mov     rax, iValue
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, X86_GREG_xAX, iDstReg & 7)
       %else
        mov     rcx, iValue
        cmp     rax, rcx
       %endif
       %if iBase == 4 || iDstReg == 4
        mov     rdx, rsp
        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
       %endif
        jz      $+3
        int3

        ;
        ; LEA+SIB w/ 16-bit operand size and 64-bit address size.
        ;
       %ifdef WITH_TRACING
        mov     dword [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_trace))], $
       %endif
        call    .load_regs
       %if iBase == 4 || iDstReg == 4
        mov     rsp, LEA_RSP
       %endif

        ; lea
       %assign  iValue  iBase_Value + (iIndex_Value << cShift)
        db      X86_OP_PRF_SIZE_OP
       %if (iBase | iIndex | iDstReg) & 8
        db      X86_OP_REX | ((iBase & 8) >> 3) | ((iIndex & 8) >> 2) | ((iDstReg & 8) >> 1)
       %endif
        db      8dh, X86_MODRM_MAKE(iMod, iDstReg & 7, iMemReg & 7), X86_SIB_MAKE(iBase & 7, iIndex & 7, cShift)
       %if iMod == X86_MOD_MEM1
        db      -18
        %assign iValue  iValue - 18
       %elif iMod == X86_MOD_MEM4 || (iMod == 0 && (iBase & 7) == 5)
        dd      -07fef3ffh
        %assign iValue  iValue - 07fef3ffh
       %endif
       %assign  iValue  (iValue & 0ffffh) | (iDstReg_Value & 0ffffffffffff0000h)

        ; cmp iDstReg, iValue
       %if iValue <= 07fffffffh && iValue >= -080000000h
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      81h, X86_MODRM_MAKE(X86_MOD_REG, 7, iDstReg & 7)
        dd      iValue & 0ffffffffh
       %elif iDstReg != X86_GREG_xAX
        mov     rax, iValue
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, X86_GREG_xAX, iDstReg & 7)
       %else
        mov     rcx, iValue
        cmp     rax, rcx
       %endif
       %if iBase == 4 || iDstReg == 4
        mov     rdx, rsp
        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
       %endif
        jz      $+3
        int3

        ;
        ; LEA+SIB w/ 16-bit operand size and 32-bit address size.
        ;
       %ifdef WITH_TRACING
        mov     dword [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_trace))], $
       %endif
        call    .load_regs
       %if iBase == 4 || iDstReg == 4
        mov     rsp, LEA_RSP
       %endif

        ; lea
       %assign  iValue  iBase_Value + (iIndex_Value << cShift)
       %if cShift & 2
        db      X86_OP_PRF_SIZE_OP, X86_OP_PRF_SIZE_ADDR
       %else
        db      X86_OP_PRF_SIZE_ADDR, X86_OP_PRF_SIZE_OP
       %endif
       %if (iBase | iIndex | iDstReg) & 8
        db      X86_OP_REX | ((iBase & 8) >> 3) | ((iIndex & 8) >> 2) | ((iDstReg & 8) >> 1)
       %endif
        db      8dh, X86_MODRM_MAKE(iMod, iDstReg & 7, iMemReg & 7), X86_SIB_MAKE(iBase & 7, iIndex & 7, cShift)
       %if iMod == X86_MOD_MEM1
        db      +17
        %assign iValue  iValue + 17
       %elif iMod == X86_MOD_MEM4 || (iMod == 0 && (iBase & 7) == 5)
        dd      +0356786h
        %assign iValue  iValue + 0356786h
       %endif
       %assign  iValue  (iValue & 0ffffh) | (iDstReg_Value & 0ffffffffffff0000h)

        ; cmp iDstReg, iValue
       %if iValue <= 07fffffffh && iValue >= -080000000h
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      81h, X86_MODRM_MAKE(X86_MOD_REG, 7, iDstReg & 7)
        dd      iValue & 0ffffffffh
       %elif iDstReg != X86_GREG_xAX
        mov     rax, iValue
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, X86_GREG_xAX, iDstReg & 7)
       %else
        mov     rcx, iValue
        cmp     rax, rcx
       %endif
       %if iBase == 4 || iDstReg == 4
        mov     rdx, rsp
        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
       %endif
        jz      $+3
        int3

       %assign cShift   (cShift + 1) & 3
      %endrep
      %assign iIndex    iIndex + 1
     %endrep
     %assign iBase      iBase + 1
    %endrep

   %else ; !SIB
        ;
        ; Plain lea reg, [reg] with disp according to iMod,
        ; or lea reg, [disp32] if iMemReg == 5 && iMod == 0.
        ;
    %if (iMemReg & 7) == 5 && iMod == 0
     %assign iMemReg_Value 0
    %else
     %assign iMemReg_Value %sel(iMemReg+1, LEA_RAX, LEA_RCX, LEA_RDX, LEA_RBX, LEA_RSP, LEA_RBP, LEA_RSI, LEA_RDI, \
                                           LEA_R8, LEA_R9, LEA_R10, LEA_R11, LEA_R12, LEA_R13, LEA_R14, LEA_R15)
    %endif

        ;
        ; 64-bit operand and address size first.
        ;
    %ifdef WITH_TRACING
        mov     dword [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_trace))], $
    %endif
        call    .load_regs
    %if iDstReg == 4
        mov     rsp, LEA_RSP
    %endif

        ; lea
    %assign   iValue  iMemReg_Value
        db      X86_OP_REX_W | ((iDstReg & 8) >> 1) | ((iMemReg & 8) >> 3)
        db      8dh, X86_MODRM_MAKE(iMod, iDstReg & 7, iMemReg & 7)
    %if iMod == X86_MOD_MEM1
        db      39
     %assign  iValue  iValue + 39
    %elif iMod == 0 && (iMemReg & 7) == 5
        dd      .load_regs - $ - 4
    %elif iMod == X86_MOD_MEM4
        dd      058739af8h
     %assign  iValue  iValue + 058739af8h
    %endif

        ; cmp iDstReg, iValue
    %if (iValue <= 07fffffffh && iValue >= -080000000h) || (iMod == 0 && (iMemReg & 7) == 5)
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      81h, X86_MODRM_MAKE(X86_MOD_REG, 7, iDstReg & 7)
     %if iMod == 0 && (iMemReg & 7) == 5
        dd      .load_regs wrt BS3FLAT
     %else
        dd      iValue & 0ffffffffh
     %endif
    %else
     %if iDstReg != iMemReg && iValue == iMemReg_Value ; This isn't entirely safe, but it saves a bit of space.
        db      X86_OP_REX_W | ((iDstReg & 8) >> 1) | ((iMemReg & 8) >> 3)
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, iDstReg & 7, iMemReg & 7)
     %elif iDstReg != X86_GREG_xAX
        mov     rax, iValue
        db      (X86_OP_REX_W | ((iDstReg & 8) >> 3))
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, X86_GREG_xAX, iDstReg & 7)
     %else
        mov     rcx, iValue
        cmp     rax, rcx
     %endif
    %endif
    %if iDstReg == 4
        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
    %endif
        jz      $+3
        int3

        ;
        ; 64-bit operand and 32-bit address size.
        ;
        call    .load_regs
    %if iDstReg == 4
        mov     rsp, LEA_RSP
    %endif

        ; lea
    %assign   iValue  iMemReg_Value
        db      X86_OP_PRF_SIZE_ADDR
        db      X86_OP_REX_W | ((iDstReg & 8) >> 1) | ((iMemReg & 8) >> 3)
        db      8dh, X86_MODRM_MAKE(iMod, iDstReg & 7, iMemReg & 7)
    %if iMod == X86_MOD_MEM1
        db      -92
     %assign  iValue  iValue - 92
    %elif iMod == 0 && (iMemReg & 7) == 5
        dd      .test_label - $ - 4
    %elif iMod == X86_MOD_MEM4
        dd      -038f8acf3h
     %assign  iValue  iValue - 038f8acf3h
    %endif
    %assign  iValue  iValue & 0ffffffffh

        ; cmp iDstReg, iValue
    %if (iValue <= 07fffffffh && iValue >= 0) || (iMod == 0 && (iMemReg & 7) == 5)
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      81h, X86_MODRM_MAKE(X86_MOD_REG, 7, iDstReg & 7)
     %if iMod == 0 && (iMemReg & 7) == 5
        dd      .test_label wrt BS3FLAT
     %else
        dd      iValue
     %endif
    %else
     %if iDstReg != X86_GREG_xAX
        mov     eax, iValue
        ;cmp     eax, %sel(iDstReg+1, rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15)
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, X86_GREG_xAX, iDstReg & 7)
     %else
        mov     ecx, iValue
        cmp     rax, rcx
     %endif
    %endif
    %if iDstReg == 4
        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
    %endif
        jz      $+3
        int3

        ;
        ; 32-bit operand and 64-bit address size.
        ;
        call    .load_regs
    %if iDstReg == 4
        mov     rsp, LEA_RSP
    %endif

        ; lea
    %assign   iValue  iMemReg_Value
    %if iDstReg >= 8 || iMemReg >= 8
        db      X86_OP_REX | ((iDstReg & 8) >> 1) | ((iMemReg & 8) >> 3)
    %endif
        db      8dh, X86_MODRM_MAKE(iMod, iDstReg & 7, iMemReg & 7)
    %if iMod == X86_MOD_MEM1
        db      16
     %assign  iValue  iValue + 16
    %elif iMod == 0 && (iMemReg & 7) == 5
        dd      .load_regs - $ - 4
    %elif iMod == X86_MOD_MEM4
        dd      0596829deh
     %assign  iValue  iValue + 0596829deh
    %endif
    %assign  iValue  iValue & 0ffffffffh

        ; cmp iDstReg, iValue
    %if (iValue <= 07fffffffh && iValue >= 0) || (iMod == 0 && (iMemReg & 7) == 5)
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      81h, X86_MODRM_MAKE(X86_MOD_REG, 7, iDstReg & 7)
     %if iMod == 0 && (iMemReg & 7) == 5
        dd      .load_regs wrt BS3FLAT
     %else
        dd      iValue
     %endif
    %else
     %if iDstReg != X86_GREG_xAX
        mov     eax, iValue
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, X86_GREG_xAX, iDstReg & 7)
     %else
        mov     ecx, iValue
        cmp     rax, rcx
     %endif
    %endif
    %if iDstReg == 4
        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
    %endif
        jz      $+3
        int3

        ;
        ; 16-bit operand and 64-bit address size.
        ;
        call    .load_regs
    %if iDstReg == 4
        mov     rsp, LEA_RSP
    %endif

        ; lea
    %assign   iValue  iMemReg_Value
        db      X86_OP_PRF_SIZE_OP
    %if iDstReg >= 8 || iMemReg >= 8
        db      X86_OP_REX | ((iDstReg & 8) >> 1) | ((iMemReg & 8) >> 3)
    %endif
        db      8dh, X86_MODRM_MAKE(iMod, iDstReg & 7, iMemReg & 7)
    %if iMod == X86_MOD_MEM1
        db      -16
     %assign  iValue  iValue - 16
    %elif iMod == 0 && (iMemReg & 7) == 5
        dd      _Bs3Text16_StartOfSegment - $ - 4 + 7 wrt BS3FLAT
     %assign  iValue  7
    %elif iMod == X86_MOD_MEM4
        dd      075682332h
     %assign  iValue  iValue + 075682332h
    %endif
    %assign  iValue  (iValue & 0ffffh) | (iDstReg_Value & 0ffffffffffff0000h)

        ; cmp iDstReg, iValue
    %if (iValue <= 07fffffffh && iValue >= -080000000h)
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      81h, X86_MODRM_MAKE(X86_MOD_REG, 7, iDstReg & 7)
        dd      iValue
    %elif iDstReg != X86_GREG_xAX
        mov     rax, iValue
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, X86_GREG_xAX, iDstReg & 7)
    %else
        mov     rcx, iValue
        cmp     rax, rcx
    %endif
    %if iDstReg == 4
        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
    %endif
        jz      $+3
        int3

        ;
        ; 16-bit operand and 32-bit address size.
        ;
        call    .load_regs
    %if iDstReg == 4
        mov     rsp, LEA_RSP
    %endif

        ; lea
    %assign   iValue  iMemReg_Value
        db      X86_OP_PRF_SIZE_OP
        db      X86_OP_PRF_SIZE_ADDR
    %if iDstReg >= 8 || iMemReg >= 8
        db      X86_OP_REX | ((iDstReg & 8) >> 1) | ((iMemReg & 8) >> 3)
    %endif
        db      8dh, X86_MODRM_MAKE(iMod, iDstReg & 7, iMemReg & 7)
    %if iMod == X86_MOD_MEM1
        db      99
     %assign  iValue  iValue + 99
    %elif iMod == 0 && (iMemReg & 7) == 5
        dd      _Bs3Text16_StartOfSegment - $ - 4 + 3347 wrt BS3FLAT
     %assign  iValue  3347
    %elif iMod == X86_MOD_MEM4
        dd      -075623432h
     %assign  iValue  iValue - 075623432h
    %endif
    %assign  iValue  (iValue & 0ffffh) | (iDstReg_Value & 0ffffffffffff0000h)

        ; cmp iDstReg, iValue
    %if (iValue <= 07fffffffh && iValue >= -080000000h)
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      81h, X86_MODRM_MAKE(X86_MOD_REG, 7, iDstReg & 7)
        dd      iValue
    %elif iDstReg != X86_GREG_xAX
        mov     rax, iValue
        db      X86_OP_REX_W | ((iDstReg & 8) >> 3)
        db      39h, X86_MODRM_MAKE(X86_MOD_REG, X86_GREG_xAX, iDstReg & 7)
    %else
        mov     rcx, iValue
        cmp     rax, rcx
    %endif
    %if iDstReg == 4
        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
    %endif
        jz      $+3
        int3

   %endif ; !SIB
   %assign iMemReg    iMemReg + 1
  %endrep
  %assign iDstReg     iDstReg + 1
 %endrep
 %assign iMod         iMod + 1
%endrep
%endif ; !python

        mov     rsp, [BS3_DATA16_WRT(BS3_DATA_NM(g_bs3CpuBasic3_lea_rsp))]
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     rdi
        pop     rsi
        pop     rbp
        pop     rbx
        pop     rdx
        pop     rcx
        pop     rax
        ret

.load_regs:
        mov     rax, LEA_RAX
        mov     rcx, LEA_RCX
        mov     rdx, LEA_RDX
        mov     rbx, LEA_RBX
        mov     rbp, LEA_RBP
        mov     rsi, LEA_RSI
        mov     rdi, LEA_RDI
        mov     r8,  LEA_R8
        mov     r9,  LEA_R9
        mov     r10, LEA_R10
        mov     r11, LEA_R11
        mov     r12, LEA_R12
        mov     r13, LEA_R13
        mov     r14, LEA_R14
        mov     r15, LEA_R15
        ret
BS3_PROC_END_CMN   bs3CpuBasic3_lea_64

