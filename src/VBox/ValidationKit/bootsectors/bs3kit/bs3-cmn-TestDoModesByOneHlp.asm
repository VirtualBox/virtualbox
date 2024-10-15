; $Id: bs3-cmn-TestDoModesByOneHlp.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3TestDoModesByOne Helpers for switching to the bit-count of the worker function.
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
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BS3_BEGIN_DATA16
BS3_GLOBAL_NAME_EX BS3_CMN_NM(g_pfnBs3TestDoModesByOneCurrent),,0
    RTCCPTR_DEF     0


;*********************************************************************************************************************************
;*  Exported Symbols                                                                                                             *
;*********************************************************************************************************************************
%ifdef BS3_STRICT
BS3_EXTERN_DATA16 g_bBs3CurrentMode
%endif

%if TMPL_BITS == 16
BS3_BEGIN_TEXT16
extern  _Bs3SelRealModeCodeToProtMode_c16
%endif


;;
; @cproto FNBS3TESTDOMODE
;
; @param    bMode       The current mode
; @uses     What allowed by calling convention and possibly mode, caller deals with it.
;

%if TMPL_BITS == 16
        ;
        ; For 16-bit workers.
        ;
BS3_BEGIN_TEXT16

BS3_SET_BITS 32
BS3_PROC_BEGIN _Bs3TestCallDoerTo16_c32
        push    xBP
        mov     xBP, xSP

        ; Load bMode into eax.
        movzx   eax, byte [xBP + xCB*2]
 %ifdef BS3_STRICT
        cmp     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        je      .ok_mode
        int3
.ok_mode:
 %endif
        ; Switch to 16-bit.
        extern  _Bs3SwitchTo16Bit_c32
        call    _Bs3SwitchTo16Bit_c32
        BS3_SET_BITS 16

        push    ax                                                  ; Worker bMode argument.

        ; Assuming real mode far pointer, convert protected mode before calling it.
        push    word [2 + BS3_DATA16_WRT(BS3_CMN_NM(g_pfnBs3TestDoModesByOneCurrent))]
        call    _Bs3SelRealModeCodeToProtMode_c16
        add     sp, 2

        push    cs                                                  ; return selector
        push    word .return                                        ; return address

        push    ax                                                  ; call converted selector
        push    word [BS3_DATA16_WRT(BS3_CMN_NM(g_pfnBs3TestDoModesByOneCurrent))]  ; call offset
        retf

.return:
        ; Switch back to 32-bit mode.
        extern  _Bs3SwitchTo32Bit_c16
        call    _Bs3SwitchTo32Bit_c16
        BS3_SET_BITS 32

        leave
        ret
BS3_PROC_END   _Bs3TestCallDoerTo16_c32


BS3_SET_BITS 64
BS3_PROC_BEGIN _Bs3TestCallDoerTo16_c64
        push    xBP
        mov     xBP, xSP

        ; Load bMode into eax.
        movzx   eax, cl
 %ifdef BS3_STRICT
        cmp     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        je      .ok_mode
        int3
.ok_mode:
 %endif
        ; Switch to 16-bit.
        extern  _Bs3SwitchTo16Bit_c64
        call    _Bs3SwitchTo16Bit_c64
        BS3_SET_BITS 16

        push    ax                                                  ; Worker bMode argument.

        ; Assuming real mode far pointer, convert protected mode before calling it.
        push    word [2 + BS3_DATA16_WRT(BS3_CMN_NM(g_pfnBs3TestDoModesByOneCurrent))]
        call    _Bs3SelRealModeCodeToProtMode_c16
        add     sp, 2

        push    cs                                                  ; return selector
        push    word .return                                        ; return address
        push    ax                                                  ; call converted selector
        push    word [BS3_DATA16_WRT(BS3_CMN_NM(g_pfnBs3TestDoModesByOneCurrent))] ; call offset
        retf

.return:
        ; Switch back to 64-bit mode.
        extern  _Bs3SwitchTo64Bit_c16
        call    _Bs3SwitchTo64Bit_c16
        BS3_SET_BITS 64

        leave
        ret
BS3_PROC_END   _Bs3TestCallDoerTo16_c64


%elif TMPL_BITS == 32
        ;
        ; For 32-bit workers.
        ;

BS3_BEGIN_TEXT16
BS3_SET_BITS 16
BS3_PROC_BEGIN _Bs3TestCallDoerTo32_f16
        push    xBP
        mov     xBP, xSP

        ; Load bMode into eax.
        movzx   eax, byte [xBP + xCB + sCB]
 %ifdef BS3_STRICT
        cmp     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        je      .ok_mode
        int3
.ok_mode:
 %endif
        ; Switch to 32-bit.
        extern  _Bs3SwitchTo32Bit_c16
        call    _Bs3SwitchTo32Bit_c16
        BS3_SET_BITS 32

        push    eax                     ; Worker bMode argument.

        test    al, BS3_MODE_CODE_V86
        jnz     .return_to_v86          ; Need to figure this while we still have the mode value.

        call    [BS3_DATA16_WRT(BS3_CMN_NM(g_pfnBs3TestDoModesByOneCurrent))]

        ; Switch back to 16-bit mode.
        extern  _Bs3SwitchTo16Bit_c32
        call    _Bs3SwitchTo16Bit_c32
        BS3_SET_BITS 16
.return:
        leave
        retf

        BS3_SET_BITS 32
.return_to_v86:
        call    [BS3_DATA16_WRT(BS3_CMN_NM(g_pfnBs3TestDoModesByOneCurrent))]

        ; Switch back to v8086 mode.
        extern  _Bs3SwitchTo16BitV86_c32
        call    _Bs3SwitchTo16BitV86_c32
        BS3_SET_BITS 16
        jmp     .return
BS3_PROC_END   _Bs3TestCallDoerTo32_f16


BS3_BEGIN_TEXT32
BS3_SET_BITS 64
BS3_PROC_BEGIN _Bs3TestCallDoerTo32_c64
        push    xBP
        mov     xBP, xSP

        ; Load bMode into eax.
        movzx   eax, cl
 %ifdef BS3_STRICT
        cmp     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        je      .ok_mode
        int3
.ok_mode:
 %endif
        ; Switch to 32-bit.
        extern  _Bs3SwitchTo32Bit_c64
        call    _Bs3SwitchTo32Bit_c64
        BS3_SET_BITS 32

        push    eax                     ; Worker bMode argument.
        call    [BS3_DATA16_WRT(BS3_CMN_NM(g_pfnBs3TestDoModesByOneCurrent))]

        ; Switch back to 64-bit mode.
        extern  _Bs3SwitchTo64Bit_c32
        call    _Bs3SwitchTo64Bit_c32
        BS3_SET_BITS 64

        leave
        ret
BS3_PROC_END   _Bs3TestCallDoerTo32_c64


%elif TMPL_BITS == 64
;
; 64-bit workers makes no sense, so skip that.
;
%else
 %error "TMPL_BITS!"
%endif

