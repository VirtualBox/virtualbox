; $Id: bs3-cmn-RegSetTr.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3RegSetTr
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
BS3_EXTERN_SYSTEM16 Bs3Gdt
TMPL_BEGIN_TEXT


;;
; @cproto   BS3_CMN_PROTO_STUB(void, Bs3RegSetTr,(uint16_t uValue));
;
; @param    uValue      The value to set.

; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
; @uses     No GPRs.
;
BS3_PROC_BEGIN_CMN Bs3RegSetTr, BS3_PBC_HYBRID_SAFE
        BS3_CALL_CONV_PROLOG 1
        push    xBP
        mov     xBP, xSP
        push    xDX
        push    xAX
        push    xDI

        mov     dx, [xBP + xCB + cbCurRetAddr]

%if TMPL_BITS == 16
        ; If V8086 mode we have to go thru a syscall.
        test    byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_CODE_V86
        jnz     .via_system_call
%endif
        ; If not in ring-0, we have to make a system call.
        mov     ax, ss
        and     ax, X86_SEL_RPL
        jnz     .via_system_call

        ; Before we can load it, we must mark it non-busy.
%if TMPL_BITS == 16
        push    ds
        mov     ax, BS3_SEL_SYSTEM16
        mov     ds, ax
        mov     di, dx
        add     xDI, Bs3Gdt wrt BS3SYSTEM16
%else
        movzx   edi, dx
        add     xDI, Bs3Gdt wrt FLAT
%endif
        add     xDI, X86DESCGENERIC_BIT_OFF_TYPE / 8
        and     byte [xDI], ~(X86_SEL_TYPE_SYS_TSS_BUSY_MASK << (X86DESCGENERIC_BIT_OFF_TYPE % 8))
%if TMPL_BITS == 16
        pop     ds
%endif
        ltr     dx
        jmp     .return

.via_system_call:
        mov     ax, BS3_SYSCALL_SET_TR
        call    Bs3Syscall

.return:
        pop     xDI
        pop     xAX
        pop     xDX
        pop     xBP
        BS3_CALL_CONV_EPILOG 1
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3RegSetTr

