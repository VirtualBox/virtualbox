; $Id: bs3-cmn-RegGetDrX.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3RegGetDrX
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
; @cproto   BS3_CMN_PROTO_STUB(RTCCUINTXREG, Bs3RegGetDrX,(uint8_t iReg));
;
; @returns  Register value.
; @param    iRegister   The source register
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
; @uses     No GPRs (only return full register(s)).
;
BS3_PROC_BEGIN_CMN Bs3RegGetDrX, BS3_PBC_HYBRID_SAFE
        BS3_CALL_CONV_PROLOG 1
        push    xBP
        mov     xBP, xSP

%if TMPL_BITS == 16
        ; If V8086 mode we have to go thru a syscall.
        test    byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_CODE_V86
        jnz     .via_system_call
        cmp     byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_RM
        je      .direct_access
%endif
        ; If not in ring-0, we have to make a system call.
        mov     ax, ss
        and     ax, X86_SEL_RPL
        jnz     .via_system_call

.direct_access:
        ; Switch (iRegister)
        mov     al, [xBP + xCB + cbCurRetAddr]
        cmp     al, 6
        jz      .get_dr6
        cmp     al, 7
        jz      .get_dr7
        cmp     al, 0
        jz      .get_dr0
        cmp     al, 1
        jz      .get_dr1
        cmp     al, 2
        jz      .get_dr2
        cmp     al, 3
        jz      .get_dr3
        cmp     al, 4
        jz      .get_dr4
        cmp     al, 5
        jz      .get_dr5
        call    Bs3Panic

.get_dr0:
        mov     sAX, dr0
        jmp     .return_fixup
.get_dr1:
        mov     sAX, dr1
        jmp     .return_fixup
.get_dr2:
        mov     sAX, dr2
        jmp     .return_fixup
.get_dr3:
        mov     sAX, dr3
        jmp     .return_fixup
.get_dr4:
        mov     sAX, dr4
        jmp     .return_fixup
.get_dr5:
        mov     sAX, dr5
        jmp     .return_fixup
.get_dr7:
        mov     sAX, dr7
        jmp     .return_fixup
.get_dr6:
        mov     sAX, dr6
        jmp     .return_fixup

.via_system_call:
        mov     xAX, BS3_SYSCALL_GET_DRX
        mov     dl, [xBP + xCB + cbCurRetAddr]
        call    Bs3Syscall
        jmp     .return

.return_fixup:
TONLY16 mov     edx, eax
TONLY16 shr     edx, 16
.return:
        pop     xBP
        BS3_CALL_CONV_EPILOG 1
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3RegGetDrX

