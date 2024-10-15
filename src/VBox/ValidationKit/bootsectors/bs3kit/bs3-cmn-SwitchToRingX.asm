; $Id: bs3-cmn-SwitchToRingX.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SwitchToRingX
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


BS3_EXTERN_CMN Bs3Syscall
%if TMPL_BITS == 16
BS3_EXTERN_DATA16 g_bBs3CurrentMode
%endif
TMPL_BEGIN_TEXT


;;
; @cproto   BS3_DECL(void) Bs3SwitchToRingX(uint8_t bRing);
;
; @param    bRing       The target ring (0..3).
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
; @uses     No GPRs.
;
BS3_PROC_BEGIN_CMN Bs3SwitchToRingX, BS3_PBC_HYBRID_SAFE
        BS3_CALL_CONV_PROLOG 1
        push    xBP
        mov     xBP, xSP
        push    xAX

%if TMPL_BITS == 16
        ; Check the current mode.
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]

        ; If real mode: Nothing we can do, but we'll bitch if the request isn't for ring-0.
        cmp     al, BS3_MODE_RM
        je      .return_real_mode

        ; If V8086 mode: Always do syscall and add a lock prefix to make sure it gets to the VMM.
        test    al, BS3_MODE_CODE_V86
        jnz     .just_do_it
%endif

        ; In protected mode: Check the CPL we're currently at skip syscall if ring-0 already.
        mov     ax, cs
        and     al, 3
        cmp     al, byte [xBP + xCB + cbCurRetAddr]
        je      .return

.just_do_it:
        mov     xAX, BS3_SYSCALL_TO_RING0
        add     al, [xBP + xCB + cbCurRetAddr]
        call    Bs3Syscall

%ifndef BS3_STRICT
.return_real_mode:
%endif
.return:
        pop     xAX
        pop     xBP
        BS3_CALL_CONV_EPILOG 1
        BS3_HYBRID_RET

%ifdef BS3_STRICT
; In real mode, only ring-0 makes any sense.
.return_real_mode:
        cmp     byte [xBP + xCB + cbCurRetAddr], 0
        je      .return
        int3
        jmp     .return
%endif
BS3_PROC_END_CMN   Bs3SwitchToRingX

