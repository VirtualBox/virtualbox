; $Id: bs3-cmn-RegSetDrX.asm 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $
;; @file
; BS3Kit - Bs3RegSetDrX
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


BS3_EXTERN_CMN Bs3Panic
BS3_EXTERN_CMN Bs3Syscall
%if TMPL_BITS == 16
BS3_EXTERN_DATA16 g_bBs3CurrentMode
%endif
TMPL_BEGIN_TEXT

;TONLY16 CPU 386

;;
; @cproto   BS3_CMN_PROTO_STUB(void, Bs3RegSetDrX,(uint8_t iReg, RTCCUINTXREG uValue));
;
; @returns  Register value.
; @param    iRegister   The source register
; @param    uValue      The new Value.

; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
; @uses     No GPRs.
;
BS3_PROC_BEGIN_CMN Bs3RegSetDrX, BS3_PBC_HYBRID_SAFE
        BS3_CALL_CONV_PROLOG 2
        push    xBP
        mov     xBP, xSP
        push    sSI
        push    xDX

        mov     sSI, [xBP + xCB + cbCurRetAddr + xCB]

%if TMPL_BITS == 16
        ; If V8086 mode we have to go thru a syscall.
        test    byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_CODE_V86
        jnz     .via_system_call
        cmp     byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_RM
        je      .direct_access
%endif
        ; If not in ring-0, we have to make a system call.
        mov     dx, ss
        and     dx, X86_SEL_RPL
        jnz     .via_system_call

.direct_access:
        ; Switch (iRegister)
        mov     dl, [xBP + xCB + cbCurRetAddr]
        cmp     dl, 6
        jz      .set_dr6
        cmp     dl, 7
        jz      .set_dr7
        cmp     dl, 0
        jz      .set_dr0
        cmp     dl, 1
        jz      .set_dr1
        cmp     dl, 2
        jz      .set_dr2
        cmp     dl, 3
        jz      .set_dr3
        cmp     dl, 4
        jz      .set_dr4
        cmp     dl, 5
        jz      .set_dr5

        call    Bs3Panic

.set_dr0:
        mov     dr0, sSI
        jmp     .return
.set_dr1:
        mov     dr1, sSI
        jmp     .return
.set_dr2:
        mov     dr2, sSI
        jmp     .return
.set_dr3:
        mov     dr3, sSI
        jmp     .return
.set_dr4:
        mov     dr4, sSI
        jmp     .return
.set_dr5:
        mov     dr5, sSI
        jmp     .return
.set_dr7:
        mov     dr7, sSI
        jmp     .return
.set_dr6:
        mov     dr6, sSI
        jmp     .return

.via_system_call:
        mov     dl, [xBP + xCB + cbCurRetAddr]
        push    xAX
        mov     xAX, BS3_SYSCALL_SET_DRX
        call    Bs3Syscall
        pop     xAX

.return:
        pop     xDX
        pop     sSI
        pop     xBP
        BS3_CALL_CONV_EPILOG 2
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3RegSetDrX

