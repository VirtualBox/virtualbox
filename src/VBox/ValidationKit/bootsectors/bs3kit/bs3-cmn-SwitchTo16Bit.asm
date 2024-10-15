; $Id: bs3-cmn-SwitchTo16Bit.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SwitchTo16Bit
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

BS3_EXTERN_DATA16 g_bBs3CurrentMode
%if TMPL_BITS == 16
BS3_EXTERN_CMN Bs3Syscall
%endif
TMPL_BEGIN_TEXT


;;
; @cproto   BS3_DECL(void) Bs3SwitchTo16Bit(void);
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
BS3_PROC_BEGIN_CMN Bs3SwitchTo16Bit, BS3_PBC_NEAR
%if TMPL_BITS == 16
        push    ax
        push    ds

        ; Check g_bBs3CurrentMode whether we're in v8086 mode or not.
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        test    al, BS3_MODE_CODE_V86
        jz      .ret_16bit

        ; Switch to ring-0 if v8086 mode.
        mov     ax, BS3_SYSCALL_TO_RING0
        call    Bs3Syscall

.ret_16bit:
        pop     ds
        pop     ax
        ret

%else
        push    xAX
        push    xBX
        xPUSHF
        cli

        ; Calc new CS.
        mov     ax, cs
        and     xAX, 3
        shl     xAX, BS3_SEL_RING_SHIFT  ; ring addend.
        add     xAX, BS3_SEL_R0_CS16

        ; Construct a far return for switching to 16-bit code.
        push    xAX
        push    .sixteen_bit
        xRETF

BS3_BEGIN_TEXT16
BS3_GLOBAL_LOCAL_LABEL .sixteen_bit

        ; Load 16-bit segment registers.
        add     ax, BS3_SEL_R0_SS16 - BS3_SEL_R0_CS16
        mov     ss, ax

        add     ax, BS3_SEL_R0_DS16 - BS3_SEL_R0_SS16
        mov     ds, ax
        mov     es, ax

        ; Thunk the stack if necessary.
        mov     ebx, esp
        shr     ebx, 16
        jz      .stack_ok
int3 ; This is for later, just remove this int3 once needed.
        test    ax, X86_SEL_RPL
        jnz     .stack_rpl_must_be_0_for_custom_stacks
        shl     bx, X86_SEL_SHIFT
        add     bx, BS3_SEL_TILED
        mov     ss, bx
        movzx   esp, sp
.stack_ok:

        ; Update globals.
        and     byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], ~BS3_MODE_CODE_MASK
        or      byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_CODE_16

        popfd
TONLY64 pop     ebx
        pop     ebx
TONLY64 pop     eax
        pop     eax
TONLY64 add     sp, 4
        ret     (TMPL_BITS - 16) / 8    ; Return and pop 2 or 6 bytes of "parameters" (unused return value)

.stack_rpl_must_be_0_for_custom_stacks:
        int3
        jmp     .stack_rpl_must_be_0_for_custom_stacks
TMPL_BEGIN_TEXT
%endif
BS3_PROC_END_CMN   Bs3SwitchTo16Bit

;; @todo far 16-bit variant.

