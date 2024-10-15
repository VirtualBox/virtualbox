; $Id: bs3-mode-SwitchToLM32.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SwitchToLM32
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
; Switch to 32-bit long mode from any other mode.
;
; @cproto   BS3_DECL(void) Bs3SwitchToLM32(void);
;
; @uses     Nothing (except possibly high 32-bit and/or upper 64-bit register parts).
;
; @remarks  There are no IDT or TSS differences between LM16, LM32 and LM64 (unlike
;           PE16 & PE32, PP16 & PP32, and PAE16 & PAE32).
;
; @remarks  Obviously returns to 32-bit mode, even if the caller was in 16-bit
;           or 64-bit mode.  It doesn't not preserve the callers ring, but
;           instead changes to ring-0.
;
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
BS3_GLOBAL_NAME_EX TMPL_NM(Bs3SwitchToLM32_Safe), function, 0
BS3_PROC_BEGIN_MODE Bs3SwitchToLM32, BS3_PBC_NEAR
%ifdef TMPL_LM32
        ret

%elifdef TMPL_CMN_LM
        ;
        ; Already in long mode, just switch to 32-bit.
        ;
        extern  BS3_CMN_NM(Bs3SwitchTo32Bit)
        jmp     BS3_CMN_NM(Bs3SwitchTo32Bit)

%elif BS3_MODE_IS_V86(TMPL_MODE)
        ;
        ; V8086 - Switch to 16-bit ring-0 and call worker for that mode.
        ;
        extern  BS3_CMN_NM(Bs3SwitchToRing0)
        call    BS3_CMN_NM(Bs3SwitchToRing0)
        extern %[BS3_MODE_R0_NM_ %+ TMPL_MODE](Bs3SwitchToLM32)
        jmp    %[BS3_MODE_R0_NM_ %+ TMPL_MODE](Bs3SwitchToLM32)

%else
 %if TMPL_BITS == 16
        push    word 0                  ; save space for extending the return value.
 %endif

        ;
        ; Switch to 32-bit protected mode (for identify mapped pages).
        ;
        extern  TMPL_NM(Bs3SwitchToPE32)
        call    TMPL_NM(Bs3SwitchToPE32)
        BS3_SET_BITS 32
 %if TMPL_BITS == 16
        jmp     .thirty_two_bit_segment
BS3_BEGIN_TEXT32
BS3_GLOBAL_LOCAL_LABEL .thirty_two_bit_segment
 %endif

        push    eax
        push    ecx
        push    edx
        pushfd

        ;
        ; Make sure both PAE and PSE are enabled (requires pentium pro).
        ;
        mov     eax, cr4
        mov     ecx, eax
        or      eax, X86_CR4_PAE | X86_CR4_PSE
        cmp     eax, ecx
        je      .cr4_is_fine
        mov     cr4, eax
.cr4_is_fine:

        ;
        ; Get the page directory (returned in eax).
        ; Will lazy init page tables.
        ;
        extern NAME(Bs3PagingGetRootForLM64_pe32)
        call   NAME(Bs3PagingGetRootForLM64_pe32)

        cli
        mov     cr3, eax

        ;
        ; Enable long mode in EFER.
        ;
        mov     ecx, MSR_K6_EFER
        rdmsr
        or      eax, MSR_K6_EFER_LME
        wrmsr

        ;
        ; Enable paging and thereby activating LM64.
        ;
BS3_EXTERN_SYSTEM16 Bs3Lgdt_Gdt
BS3_BEGIN_TEXT32
        mov     eax, cr0
        or      eax, X86_CR0_PG
        mov     cr0, eax
        jmp     .in_lm32
.in_lm32:

        ;
        ; Call rountine for doing mode specific setups.
        ;
        extern  NAME(Bs3EnteredMode_lm32)
        call    NAME(Bs3EnteredMode_lm32)

        ;
        ; Load full 64-bit GDT base address from 64-bit segment.
        ;
        jmp     dword BS3_SEL_R0_CS64:.load_full_gdt_base wrt FLAT
.load_full_gdt_base:
        BS3_SET_BITS 64
        lgdt    [Bs3Lgdt_Gdt wrt FLAT]
        push    BS3_SEL_R0_CS32
        push    .back_to_32bit wrt FLAT
        o64 retf
.back_to_32bit:
        BS3_SET_BITS 32

        ;
        ; Restore ecx, eax and flags (IF).
        ;
 %if TMPL_BITS == 16
        movzx   eax, word [esp + 16 + 2] ; Load return address.
        add     eax, BS3_ADDR_BS3TEXT16  ; Convert it to a flat address.
        mov     [esp + 16], eax          ; Store it in the place right for 32-bit returns.
 %endif
        popfd
        pop     edx
        pop     ecx
        pop     eax
        ret

 %if TMPL_BITS != 32
TMPL_BEGIN_TEXT
 %endif
%endif
BS3_PROC_END_MODE   Bs3SwitchToLM32


%if TMPL_BITS == 16
;;
; Custom far stub.
BS3_BEGIN_TEXT16_FARSTUBS
BS3_PROC_BEGIN_MODE Bs3SwitchToLM32, BS3_PBC_FAR
        inc         bp
        push        bp
        mov         bp, sp

        ; Call the real thing.
        call        TMPL_NM(Bs3SwitchToLM32)
        BS3_SET_BITS 32

        ; Jmp to common code for the tedious conversion.
 %if BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        extern      _Bs3SwitchHlpConvRealModeRetfPopBpDecBpAndReturn_c32
        jmp         _Bs3SwitchHlpConvRealModeRetfPopBpDecBpAndReturn_c32
 %else
        extern      _Bs3SwitchHlpConvProtModeRetfPopBpDecBpAndReturn_c32
        jmp         _Bs3SwitchHlpConvProtModeRetfPopBpDecBpAndReturn_c32
 %endif
        BS3_SET_BITS 16
BS3_PROC_END_MODE   Bs3SwitchToLM32
%endif

