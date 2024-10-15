; $Id: bs3-mode-SwitchToLM16.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SwitchToLM16
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
; Switch to 16-bit long mode from any other mode.
;
; @cproto   BS3_DECL(void) Bs3SwitchToLM16(void);
;
; @uses     Nothing (except possibly high 32-bit and/or upper 64-bit register parts).
;
; @remarks  Obviously returns to 16-bit mode, even if the caller was in 32-bit
;           or 64-bit mode.  It doesn't not preserve the callers ring, but
;           instead changes to ring-0.
;
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
%if TMPL_BITS == 16
BS3_GLOBAL_NAME_EX TMPL_NM(Bs3SwitchToLM16_Safe), function , 0
%endif
BS3_PROC_BEGIN_MODE Bs3SwitchToLM16, BS3_PBC_NEAR
%ifdef TMPL_LM16
        extern  BS3_CMN_NM(Bs3SwitchToRing0)
        call    BS3_CMN_NM(Bs3SwitchToRing0)
        push    ax
        mov     ax, BS3_SEL_R0_DS16
        mov     ds, ax
        mov     es, ax
        pop     ax
        ret

%elifdef TMPL_CMN_LM
        ;
        ; Already in long mode, just switch to 16-bit.
        ;
        extern  BS3_CMN_NM(Bs3SwitchTo16Bit)
        jmp     BS3_CMN_NM(Bs3SwitchTo16Bit)

%else
        ;
        ; Switch to LM32 and then switch to 64-bits (IDT & TSS are the same for
        ; LM16, LM32 and LM64, unlike the rest).
        ;
        ; (The long mode switching code is going via 32-bit protected mode, so
        ; Bs3SwitchToLM32 contains the actual code for switching to avoid
        ; unnecessary 32-bit -> 64-bit -> 32-bit trips.)
        ;
        extern  TMPL_NM(Bs3SwitchToLM32)
        call    TMPL_NM(Bs3SwitchToLM32)
        BS3_SET_BITS 32

        extern  _Bs3SwitchTo16Bit_c32
 %if TMPL_BITS == 16
        sub     esp, 2
        shr     dword [esp], 16
 %elif TMPL_BITS == 64
        pop     dword [esp + 4]
 %endif
        jmp     _Bs3SwitchTo16Bit_c32
%endif
BS3_PROC_END_MODE   Bs3SwitchToLM16


%if TMPL_BITS == 16
;;
; Custom far stub.
BS3_BEGIN_TEXT16_FARSTUBS
BS3_PROC_BEGIN_MODE Bs3SwitchToLM16, BS3_PBC_FAR
        inc         bp
        push        bp
        mov         bp, sp

        ; Call the real thing.
        call        TMPL_NM(Bs3SwitchToLM16)

 %if BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        ; Jmp to  common code for the tedious conversion.
        BS3_EXTERN_CMN Bs3SwitchHlpConvRealModeRetfPopBpDecBpAndReturn
        jmp         Bs3SwitchHlpConvRealModeRetfPopBpDecBpAndReturn
 %else
        pop         bp
        dec         bp
        retf
 %endif
BS3_PROC_END_MODE   Bs3SwitchToLM16

%else
;;
; Safe far return to non-BS3TEXT16 code.
BS3_EXTERN_CMN Bs3SwitchHlpConvFlatRetToRetfProtMode
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_PROC_BEGIN_MODE Bs3SwitchToLM16_Safe, BS3_PBC_NEAR
        call        Bs3SwitchHlpConvFlatRetToRetfProtMode ; Special internal function.  Uses nothing, but modifies the stack.
        call        TMPL_NM(Bs3SwitchToLM16)
        BS3_SET_BITS 16
        retf
BS3_PROC_END_MODE   Bs3SwitchToLM16_Safe

%endif

