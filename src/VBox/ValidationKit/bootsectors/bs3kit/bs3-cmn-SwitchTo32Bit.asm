; $Id: bs3-cmn-SwitchTo32Bit.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SwitchTo32Bit
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


%if TMPL_BITS == 16
BS3_EXTERN_CMN Bs3SelProtFar32ToFlat32
BS3_EXTERN_CMN Bs3Syscall
%endif
%if TMPL_BITS != 32
BS3_EXTERN_DATA16 g_bBs3CurrentMode
TMPL_BEGIN_TEXT
%endif


;;
; @cproto   BS3_DECL(void) Bs3SwitchTo32Bit(void);
;
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
BS3_PROC_BEGIN_CMN Bs3SwitchTo32Bit, BS3_PBC_NEAR
%if TMPL_BITS == 32
        ret
%else
 %if TMPL_BITS == 16
        push    ax                      ; Reserve space for larger return value (adjusted in 32-bit code).
        push    eax
        pushfd
        push    edx
 %else
        pushfq
        mov     [rsp + 4], eax
 %endif
        cli

 %if TMPL_BITS == 16
        ; Check for v8086 mode, we need to exit it to enter 32-bit mode.
        mov     ax, seg g_bBs3CurrentMode
        mov     ds, ax
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        test    al, BS3_MODE_CODE_V86
        jz      .not_v8086

        ; Calc flat stack into edx.
        mov     dx, ss
        movzx   edx, dx
        shl     edx, 4
        add     dx, sp
        adc     edx, 0                  ; edx = flat stack address corresponding to ss:sp

        ; Switch to 16-bit ring0 and go on to do the far jump to 32-bit code.
        mov     ax, BS3_SYSCALL_TO_RING0
        call    Bs3Syscall

        mov     xAX, BS3_SEL_R0_CS32
        jmp     .do_far_jump
 %endif

.not_v8086:
 %if TMPL_BITS == 16
        ; Calc flat stack into edx.
        movzx   eax, sp
        push    ecx
        push    ebx
        push    ss
        push    eax
        call    Bs3SelProtFar32ToFlat32
        add     sp, 6
        shl     edx, 16
        mov     dx, ax                  ; edx = flat stack address corresponding to ss:sp
        pop     ebx
        pop     ecx
 %endif

        ; Calc ring addend.
        mov     ax, cs
        and     xAX, 3
        shl     xAX, BS3_SEL_RING_SHIFT
        add     xAX, BS3_SEL_R0_CS32

        ; Create far return for switching to 32-bit mode.
.do_far_jump:
        push    sAX
 %if TMPL_BITS == 16
        push    dword .thirty_two_bit wrt FLAT
        o32 retf
 %else
        push    .thirty_two_bit
        o64 retf
 %endif

BS3_SET_BITS 32
.thirty_two_bit:
        ; Load 32-bit segment registers.
        add     eax, BS3_SEL_R0_SS32 - BS3_SEL_R0_CS32
        mov     ss, ax
 %if TMPL_BITS == 16
        mov     esp, edx                ; Load flat stack address.
 %endif

        add     eax, BS3_SEL_R0_DS32 - BS3_SEL_R0_SS32
        mov     ds, ax
        mov     es, ax

        ; Update globals.
        and     byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], ~BS3_MODE_CODE_MASK
        or      byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_CODE_32

 %if TMPL_BITS == 16
        ; Adjust the return address.
        movzx   eax, word [esp + 4*3 + 2]
        add     eax, BS3_ADDR_BS3TEXT16
        mov     [esp + 4*3], eax
 %endif

        ; Restore and return.
 %if TMPL_BITS == 16
        pop     edx
 %endif
        popfd
        pop     eax
TONLY64 ret     4
TNOT64  ret
%endif
BS3_PROC_END_CMN   Bs3SwitchTo32Bit

;; @todo far 16-bit variant.

