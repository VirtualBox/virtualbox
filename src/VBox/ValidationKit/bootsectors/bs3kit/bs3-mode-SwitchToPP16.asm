; $Id: bs3-mode-SwitchToPP16.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SwitchToPP16
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
%include "bs3kit-template-header.mac"

;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
%ifndef TMPL_PP16
BS3_BEGIN_TEXT16
extern  NAME(Bs3EnteredMode_pp16)
 %ifdef TMPL_PP32
 BS3_EXTERN_CMN Bs3SwitchTo16Bit
 %endif
TMPL_BEGIN_TEXT
%endif


;;
; Switch to 16-bit paged protected mode from any other mode.
;
; @cproto   BS3_DECL(void) Bs3SwitchToPP16(void);
;
; @uses     Nothing (except high 32-bit register parts).
;
; @remarks  Obviously returns to 16-bit mode, even if the caller was
;           in 32-bit or 64-bit mode.
;
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
%if TMPL_BITS == 16
BS3_GLOBAL_NAME_EX TMPL_NM(Bs3SwitchToPP16_Safe), function , 0
%endif
BS3_PROC_BEGIN_MODE Bs3SwitchToPP16, BS3_PBC_NEAR
%ifdef TMPL_PP16
        extern  BS3_CMN_NM(Bs3SwitchToRing0)
        call    BS3_CMN_NM(Bs3SwitchToRing0)
        push    ax
        mov     ax, BS3_SEL_R0_DS16
        mov     ds, ax
        mov     es, ax
        pop     ax
        ret

%elif BS3_MODE_IS_V86(TMPL_MODE)
        ;
        ; V8086 - Switch to 16-bit ring-0 and call worker for that mode.
        ;
        extern  BS3_CMN_NM(Bs3SwitchToRing0)
        call    BS3_CMN_NM(Bs3SwitchToRing0)
        extern %[BS3_MODE_R0_NM_ %+ TMPL_MODE](Bs3SwitchToPP16)
        jmp    %[BS3_MODE_R0_NM_ %+ TMPL_MODE](Bs3SwitchToPP16)

%else

        ;
        ; Switch to 16-bit text segment and prepare for returning in 16-bit mode.
        ;
 %if TMPL_BITS != 16
        shl     xPRE [xSP], TMPL_BITS - 16    ; Adjust the return address.
        add     xSP, xCB - 2

        ; Must be in 16-bit segment when calling Bs3SwitchToRM and Bs3SwitchTo16Bit.
        jmp     .sixteen_bit_segment
BS3_BEGIN_TEXT16
        BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .sixteen_bit_segment
 %endif

 %ifdef TMPL_PP32
        ;
        ; No need to go to real-mode here, we use the same CR3 and stuff.
        ; Just switch to 32-bit mode and call the Bs3EnteredMode routine to
        ; load the right descriptor tables.
        ;
        call    Bs3SwitchTo16Bit
        BS3_SET_BITS 16
        call    NAME(Bs3EnteredMode_pp16)
        ret
 %else

        ;
        ; Switch to real mode.
        ;
        extern  TMPL_NM(Bs3SwitchToRM)
        call    TMPL_NM(Bs3SwitchToRM)
        BS3_SET_BITS 16

        push    eax
        push    ecx
        pushfd
%ifdef BS3_STRICT
        mov     ax, ds
        cmp     ax, BS3_ADDR_BS3DATA16 >> 4
        je      .real_mode_ds_ok
        hlt
.real_mode_ds_ok:
%endif

        ;
        ; Get the page directory (returned in eax).
        ; Will lazy init page tables (in 16-bit prot mode).
        ;
        extern NAME(Bs3PagingGetRootForPP16_rm)
        call   NAME(Bs3PagingGetRootForPP16_rm)

        cli
        mov     cr3, eax

        ;
        ; Make sure PAE is really off and that PSE is enabled when supported.
        ;
BS3_EXTERN_DATA16 g_uBs3CpuDetected
BS3_BEGIN_TEXT16
        test    byte [1 + BS3_DATA16_WRT(g_uBs3CpuDetected)], (BS3CPU_F_CPUID >> 8)
        jz      .cr4_is_fine
        mov     eax, cr4
        mov     ecx, eax
        and     eax, ~(X86_CR4_PAE | X86_CR4_PSE)
        test    byte [1 + BS3_DATA16_WRT(g_uBs3CpuDetected)], (BS3CPU_F_PSE >> 8)
        jz      .no_pse
        or      eax, X86_CR4_PSE
.no_pse:
        cmp     eax, ecx
        je      .cr4_is_fine
        mov     cr4, eax
.cr4_is_fine:

        ;
        ; Load the GDT and enable PP16.
        ;
BS3_EXTERN_SYSTEM16 Bs3LgdtDef_Gdt
BS3_EXTERN_SYSTEM16 Bs3Lgdt_Gdt
BS3_BEGIN_TEXT16
        mov     ax, BS3SYSTEM16
        mov     ds, ax
        lgdt    [Bs3LgdtDef_Gdt]        ; Will only load 24-bit base!

        mov     eax, cr0
        or      eax, X86_CR0_PE | X86_CR0_PG
        mov     cr0, eax
        jmp     BS3_SEL_R0_CS16:.reload_cs_and_stuff
.reload_cs_and_stuff:

        ;
        ; Convert the (now) real mode stack to 16-bit.
        ;
        mov     ax, .stack_fix_return
        extern  NAME(Bs3ConvertRMStackToP16UsingCxReturnToAx_c16)
        jmp     NAME(Bs3ConvertRMStackToP16UsingCxReturnToAx_c16)
.stack_fix_return:

        ;
        ; Call rountine for doing mode specific setups.
        ;
        call    NAME(Bs3EnteredMode_pp16)

        ;
        ; Load full 32-bit GDT base address from 32-bit segment.
        ;
        push    ds
        mov     ax, BS3_SEL_SYSTEM16
        mov     ds, ax
        jmp     dword BS3_SEL_R0_CS32:.load_full_gdt_base wrt FLAT
.load_full_gdt_base:
        BS3_SET_BITS 32
        lgdt    [Bs3Lgdt_Gdt wrt BS3SYSTEM16]
        jmp     BS3_SEL_R0_CS16:.back_to_16bit
.back_to_16bit:
        BS3_SET_BITS 16
        pop     ds

        popfd
        pop     ecx
        pop     eax
        ret

 %endif ; !TMPL_PP32
 %if TMPL_BITS != 16
TMPL_BEGIN_TEXT
 %endif
%endif
BS3_PROC_END_MODE   Bs3SwitchToPP16


%if TMPL_BITS == 16
;;
; Custom far stub.
BS3_BEGIN_TEXT16_FARSTUBS
BS3_PROC_BEGIN_MODE Bs3SwitchToPP16, BS3_PBC_FAR
        inc         bp
        push        bp
        mov         bp, sp

        ; Call the real thing.
        call        TMPL_NM(Bs3SwitchToPP16)

 %if BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        ; Jmp to  common code for the tedious conversion.
        BS3_EXTERN_CMN Bs3SwitchHlpConvRealModeRetfPopBpDecBpAndReturn
        jmp         Bs3SwitchHlpConvRealModeRetfPopBpDecBpAndReturn
 %else
        pop         bp
        dec         bp
        retf
 %endif
BS3_PROC_END_MODE   Bs3SwitchToPP16

%else
;;
; Safe far return to non-BS3TEXT16 code.
BS3_EXTERN_CMN Bs3SwitchHlpConvFlatRetToRetfProtMode
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_PROC_BEGIN_MODE Bs3SwitchToPP16_Safe, BS3_PBC_NEAR
        call        Bs3SwitchHlpConvFlatRetToRetfProtMode ; Special internal function.  Uses nothing, but modifies the stack.
        call        TMPL_NM(Bs3SwitchToPP16)
        BS3_SET_BITS 16
        retf
BS3_PROC_END_MODE   Bs3SwitchToPP16_Safe
%endif

