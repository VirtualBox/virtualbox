; $Id: bs3-mode-SwitchTo32BitAndCallC.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - bs3SwitchTo32BitAndCallC
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
BS3_EXTERN_DATA16   g_bBs3CurrentMode
TMPL_BEGIN_TEXT

%ifdef BS3_STRICT
BS3_EXTERN_CMN      Bs3Panic
%endif

%if BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
BS3_EXTERN_CMN      Bs3SelRealModeCodeToFlat
%endif

%if TMPL_MODE == BS3_MODE_RM
extern              NAME(Bs3SwitchToPE32_rm)
extern              NAME(Bs3SwitchToRM_pe32)
%elif !BS3_MODE_IS_32BIT_CODE(TMPL_MODE)
BS3_EXTERN_CMN      Bs3SwitchTo32Bit
 %if BS3_MODE_IS_16BIT_CODE_NO_V86(TMPL_MODE)
extern              _Bs3SwitchTo16Bit_c32
 %elif BS3_MODE_IS_V86(TMPL_MODE)
extern              _Bs3SwitchTo16BitV86_c32
 %elif !BS3_MODE_IS_32BIT_CODE(TMPL_MODE)
extern              _Bs3SwitchTo64_c32
 %endif
%endif



;;
; @cproto   BS3_MODE_PROTO_STUB(int, Bs3SwitchTo32BitAndCallC,(PFNBS3FARADDRCONV fpfnCall, unsigned cbParams, ...));
;
BS3_PROC_BEGIN_MODE Bs3SwitchTo32BitAndCallC, BS3_PBC_HYBRID
        BS3_CALL_CONV_PROLOG 4
TONLY16 inc     xBP
        push    xBP
        mov     xBP, xSP
        push    xSI

        ;
        ; Push the arguments first.
        ;
TONLY16 mov     si,  [xBP + xCB + cbCurRetAddr + sCB]
TNOT16  mov     esi, [xBP + xCB + cbCurRetAddr + sCB]
%ifdef BS3_STRICT
        test    xSI, 3
        jz      .cbParams_ok
        call    Bs3Panic
.cbParams_ok:
        cmp     byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], TMPL_MODE
        je      .mode_ok
        call    Bs3Panic
.mode_ok:
%endif
        add     xSI, sCB - 1            ; round it up to nearest push size / dword.
        and     xSI, ~(sCB - 1)
        jz      .done_pushing           ; skip if zero
.push_more:
        push    xPRE [xBP + xCB + cbCurRetAddr + sCB + xCB + xSI - xCB]
        sub     xSI, xCB
        jnz     .push_more
        mov     xSI, xAX                ; restore xSI
.done_pushing:

        ;
        ; Load fpfnCall into eax.
        ;
%if BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        push    sPRE [xBP + xCB + cbCurRetAddr]
        BS3_CALL Bs3SelRealModeCodeToFlat, 1
        add     xSP, sCB
        rol     eax, 16
        mov     ax, dx
        rol     eax, 16
%else
        mov     eax, [xBP + xCB + cbCurRetAddr]
%endif

        ;
        ; Switch to 32-bit mode, if this is real mode pick PE32.
        ;
%if TMPL_MODE == BS3_MODE_RM
        call    NAME(Bs3SwitchToPE32_rm)
        BS3_SET_BITS 32
%elif !BS3_MODE_IS_32BIT_CODE(TMPL_MODE)
        call    Bs3SwitchTo32Bit
        BS3_SET_BITS 32
%endif

        ;
        ; Make the call.
        ;
        call    eax

        ;
        ; Return, preserving xAX.
        ;
%if BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        mov     edx, eax
        shr     edx, 16
%endif
%if TMPL_MODE == BS3_MODE_RM
        call    NAME(Bs3SwitchToRM_pe32)
%elif BS3_MODE_IS_16BIT_CODE_NO_V86(TMPL_MODE)
        call    _Bs3SwitchTo16Bit_c32
%elif BS3_MODE_IS_V86(TMPL_MODE)
        call    _Bs3SwitchTo16BitV86_c32
%elif !BS3_MODE_IS_32BIT_CODE(TMPL_MODE)
        call    _Bs3SwitchTo64_c32
%endif
        BS3_SET_BITS TMPL_BITS

        ; Epilog.
        lea     xSP, [xBP - xCB]
        pop     xSI
        pop     xBP
TONLY16 dec     xBP
        BS3_CALL_CONV_EPILOG 4
        BS3_HYBRID_RET
BS3_PROC_END_MODE   Bs3SwitchTo32BitAndCallC

