; $Id: bs3-mode-SwitchToPE16.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SwitchToPE16
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
; Switch to 16-bit unpaged protected mode from any other mode.
;
; @cproto   BS3_DECL(void) Bs3SwitchToPE16(void);
;
; @uses     Nothing (except high 32-bit register parts).
;
; @remarks  Obviously returns to 16-bit mode, even if the caller was
;           in 32-bit or 64-bit mode.
;
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
%if TMPL_BITS == 16
BS3_GLOBAL_NAME_EX TMPL_NM(Bs3SwitchToPE16_Safe), function , 0
%endif
BS3_PROC_BEGIN_MODE Bs3SwitchToPE16, BS3_PBC_NEAR
%ifdef TMPL_PE16
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
        extern %[BS3_MODE_R0_NM_ %+ TMPL_MODE](Bs3SwitchToPE16)
        jmp    %[BS3_MODE_R0_NM_ %+ TMPL_MODE](Bs3SwitchToPE16)

%else
        ;
        ; Switch to 16-bit mode and prepare for returning in 16-bit mode.
        ;
 %if TMPL_BITS != 16
        shl     xPRE [xSP], TMPL_BITS - 16    ; Adjust the return address.
        add     xSP, xCB - 2

        ; Must be in 16-bit segment when calling Bs3SwitchTo16Bit.
        jmp     .sixteen_bit_segment
BS3_BEGIN_TEXT16
        BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .sixteen_bit_segment
 %endif

        ;
        ; Switch to real mode.
        ;
        extern  TMPL_NM(Bs3SwitchToRM)
        call    TMPL_NM(Bs3SwitchToRM)
        BS3_SET_BITS 16

        push    ax
        push    cx
        pushf
        cli

        ;
        ; Load the GDT and enable PE16.
        ;
BS3_EXTERN_SYSTEM16 Bs3Lgdt_Gdt
BS3_EXTERN_SYSTEM16 Bs3LgdtDef_Gdt
BS3_BEGIN_TEXT16
        mov     ax, BS3SYSTEM16
        mov     ds, ax
        lgdt    [Bs3LgdtDef_Gdt]        ; Will only load 24-bit base!

        smsw    ax
        or      ax, X86_CR0_PE
        lmsw    ax

        ;
        ; Convert from real mode stack to protected mode stack.
        ;
        mov     ax, .p16_stack
        extern  NAME(Bs3ConvertRMStackToP16UsingCxReturnToAx_c16)
        jmp     NAME(Bs3ConvertRMStackToP16UsingCxReturnToAx_c16)
.p16_stack:

        ;
        ; Call routine for doing mode specific setups.
        ;
        extern  NAME(Bs3EnteredMode_pe16)
        call    NAME(Bs3EnteredMode_pe16)

        ;
        ; Load full 32-bit GDT base address from 32-bit segment, if 386+ CPU.
        ;
        BS3_EXTERN_DATA16 g_uBs3CpuDetected
        BS3_BEGIN_TEXT16
        cmp     byte [g_uBs3CpuDetected], BS3CPU_80386
        jb      .old_cpu_skip_32bit_lgdt
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
.old_cpu_skip_32bit_lgdt:

        popf
        pop     cx
        pop     ax
        ret

 %if TMPL_BITS != 16
TMPL_BEGIN_TEXT
 %endif
%endif
BS3_PROC_END_MODE   Bs3SwitchToPE16


%if TMPL_BITS == 16
;;
; Custom far stub.
BS3_BEGIN_TEXT16_FARSTUBS
BS3_PROC_BEGIN_MODE Bs3SwitchToPE16, BS3_PBC_FAR
        inc         bp
        push        bp
        mov         bp, sp

        ; Call the real thing.
        call        TMPL_NM(Bs3SwitchToPE16)

 %if BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        ; Jmp to  common code for the tedious conversion.
        BS3_EXTERN_CMN Bs3SwitchHlpConvRealModeRetfPopBpDecBpAndReturn
        jmp         Bs3SwitchHlpConvRealModeRetfPopBpDecBpAndReturn
 %else
        pop         bp
        dec         bp
        retf
 %endif
BS3_PROC_END_MODE   Bs3SwitchToPE16

%else
;;
; Safe far return to non-BS3TEXT16 code.
BS3_EXTERN_CMN Bs3SwitchHlpConvFlatRetToRetfProtMode
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_PROC_BEGIN_MODE Bs3SwitchToPE16_Safe, BS3_PBC_NEAR
        call        Bs3SwitchHlpConvFlatRetToRetfProtMode ; Special internal function.  Uses nothing, but modifies the stack.
        call        TMPL_NM(Bs3SwitchToPE16)
        BS3_SET_BITS 16
        retf
BS3_PROC_END_MODE   Bs3SwitchToPE16_Safe
%endif

