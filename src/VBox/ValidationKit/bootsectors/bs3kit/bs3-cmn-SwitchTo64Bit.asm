; $Id: bs3-cmn-SwitchTo64Bit.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SwitchTo64Bit
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

%if TMPL_BITS != 64
BS3_EXTERN_DATA16 g_bBs3CurrentMode
TMPL_BEGIN_TEXT
%endif


;;
; @cproto   BS3_DECL(void) Bs3SwitchTo64Bit(void);
;
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
; @uses     No GPRs.
;
BS3_PROC_BEGIN_CMN Bs3SwitchTo64Bit, BS3_PBC_NEAR
%if TMPL_BITS == 64
        ret

%else
 %if TMPL_BITS == 16
        sub     sp, 6                   ; Space for extended return value (corrected in 64-bit mode).
 %else
        push    xPRE [xSP]              ; Duplicate the return address.
        and     dword [xSP + xCB], 0    ; Clear the high dword or it.
 %endif
        push    dword 0
        push    sAX
        push    dword 0
        pushfd
        cli

 %if TMPL_BITS == 16
        ; Check that this is LM16
        mov     ax, seg g_bBs3CurrentMode
        cmp     byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_LM16
        je      .ok_lm16
        int3
 .ok_lm16:
 %endif

        ; Calc ring addend.
        mov     ax, cs
        and     xAX, 3
        shl     xAX, BS3_SEL_RING_SHIFT
        add     xAX, BS3_SEL_R0_CS64

        ; setup far return.
        push    sAX
 %if TMPL_BITS == 16
        push    dword .sixty_four_bit wrt FLAT
        o32 retf
 %else
        push    .sixty_four_bit
        retf
 %endif

BS3_SET_BITS 64
.sixty_four_bit:

        ; Load 64-bit segment registers (SS64==DS64).
        add     eax, BS3_SEL_R0_DS64 - BS3_SEL_R0_CS64
        mov     ss, ax
        mov     ds, ax
        mov     es, ax

        ; Update globals.
        and     byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], ~BS3_MODE_CODE_MASK
        or      byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_CODE_64

 %if TMPL_BITS == 16
        movzx   eax, word [rsp + 8*2+6]
        add     eax, BS3_ADDR_BS3TEXT16
        mov     [rsp + 8*2], rax
 %endif

        popfq
        pop     rax
        ret
%endif
BS3_PROC_END_CMN   Bs3SwitchTo64Bit


;; @todo far 16-bit variant.

