; $Id: bs3-cmn-SwitchTo16BitV86.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SwitchTo16BitV86
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

BS3_EXTERN_DATA16   g_bBs3CurrentMode
BS3_EXTERN_CMN      Bs3SwitchToRing0
BS3_EXTERN_CMN      Bs3SelProtFar32ToFlat32
TMPL_BEGIN_TEXT


;;
; @cproto   BS3_DECL(void) Bs3SwitchTo16BitV86(void);
; @uses No general registers modified. Regment registers loaded with specific
;       values and the stack register converted to real mode (not ebp).
;
BS3_PROC_BEGIN_CMN Bs3SwitchTo16BitV86, BS3_PBC_NEAR
        ; Construct basic v8086 return frame.
BONLY16 movzx   esp, sp
        push    dword 0                                 ; +0x20: GS
        push    dword 0                                 ; +0x1c: FS
        push    dword BS3_SEL_DATA16                    ; +0x18: ES
        push    dword BS3_SEL_DATA16                    ; +0x14: DS
        push    dword 0                                 ; +0x10: SS - later
        push    dword 0                                 ; +0x0c: return ESP, later.
        pushfd
        or      dword [esp], X86_EFL_VM | X86_EFL_IOPL  ; +0x08: Set IOPL=3 and the VM flag (EFLAGS).
        push    dword BS3_SEL_TEXT16                    ; +0x04
        push    word 0
        push    word [esp + 24h - 2]                    ; +0x00
        ; Save registers and stuff.
        push    eax
        push    edx
        push    ecx
        push    ebx
 %if TMPL_BITS == 16
        push    ds

        ; Check g_bBs3CurrentMode whether we're in v8086 mode or not.
        mov     ax, seg g_bBs3CurrentMode
        mov     ds, ax
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        test    al, BS3_MODE_CODE_V86
        jz      .not_v8086

        pop     ds
        pop     ebx
        pop     ecx
        pop     edx
        pop     eax
        add     xSP, 0x24
        ret

.not_v8086:
        pop     ax                      ; Drop the push ds so the stacks are identical. Keep DS = BS3KIT_GRPNM_DATA16 though.
 %endif

        ; Ensure that we're in ring-0.
        mov     ax, ss
        test    ax, 3
        jz      .is_ring0
        call    Bs3SwitchToRing0
 %if TMPL_BITS == 16
        mov     ax, seg g_bBs3CurrentMode
        mov     ds, ax                  ; parnoia
 %endif
.is_ring0:

        ; Update globals.
        and     byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], ~BS3_MODE_CODE_MASK
        or      byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_CODE_V86

        ; Thunk return SS:ESP to real-mode address via 32-bit flat.
        lea     eax, [esp + 4*4 + 24h + xCB]
        push    ss
        push    eax
        BS3_CALL Bs3SelProtFar32ToFlat32, 2
        add     esp, sCB + xCB
        mov     [esp + 4*4 + 0ch], ax   ; high word is already zero
 %if TMPL_BITS == 16
        mov     [esp + 4*4 + 10h], dx
 %else
        shr     eax, 16
        mov     [esp + 4*4 + 10h], ax
 %endif

        ; Return to v8086 mode.
        pop     ebx
        pop     ecx
        pop     edx
        pop     eax
        iretd
BS3_PROC_END_CMN   Bs3SwitchTo16BitV86

;; @todo far 16-bit variant.

%endif ; ! 64-bit

