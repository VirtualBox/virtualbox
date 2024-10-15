; $Id: bs3-mode-EnteredMode.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3EnteredMode
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
BS3_EXTERN_DATA16 g_uBs3CpuDetected
%endif
BS3_EXTERN_DATA16 g_bBs3CurrentMode
TMPL_BEGIN_TEXT

;;
; @cproto   BS3_DECL(void) Bs3EnteredMode(void);
;
; @uses     Nothing.
;
; @remarks  ASSUMES we're in ring-0 when not in some kind of real mode.
;
BS3_PROC_BEGIN_MODE Bs3EnteredMode, BS3_PBC_NEAR ; won't need this outside the switchers, so always near.
        push    xBP
        mov     xBP, xSP
        push    xAX
        push    xCX
        push    xDX
TONLY16 push    xBX
%if BS3_MODE_IS_64BIT_CODE(TMPL_MODE)
        push    r8
        push    r9
%endif

        ;
        ; Load stack selector (not always necessary) and sometimes CS too.
        ;
%if BS3_MODE_IS_RM_SYS(TMPL_MODE)
        xor     ax, ax
%elif BS3_MODE_IS_V86(TMPL_MODE)
        extern  v86_versions_of_Bs3EnteredMode_should_not_be_dragged_into_the_link
        call    v86_versions_of_Bs3EnteredMode_should_not_be_dragged_into_the_link
%elif BS3_MODE_IS_16BIT_CODE(TMPL_MODE)
        jmp     BS3_SEL_R0_CS16:.reloaded_cs
.reloaded_cs:
        mov     ax, BS3_SEL_R0_SS16
%elif BS3_MODE_IS_32BIT_CODE(TMPL_MODE)
        mov     ax, BS3_SEL_R0_SS32
%elif BS3_MODE_IS_64BIT_CODE(TMPL_MODE)
        mov     ax, BS3_SEL_R0_DS64
%else
 %error "TMPL_MODE"
%endif
        mov     ss, ax

        ;
        ; Load selector appropriate for accessing BS3SYSTEM16 data.
        ;
%if BS3_MODE_IS_16BIT_CODE(TMPL_MODE)
        mov     ax, BS3_SEL_SYSTEM16
%else
        mov     ax, RT_CONCAT(BS3_SEL_R0_DS,TMPL_BITS)
%endif
        mov     ds, ax

        ;
        ; Load the appropritate IDT or IVT.
        ; Always 64-bit in long mode, otherwise according to TMPL_BITS.
        ;
%if BS3_MODE_IS_RM_SYS(TMPL_MODE)
        BS3_EXTERN_SYSTEM16 Bs3Lidt_Ivt
        TMPL_BEGIN_TEXT
        lidt    [Bs3Lidt_Ivt]

%elif BS3_MODE_IS_16BIT_SYS(TMPL_MODE)
        BS3_EXTERN_SYSTEM16 Bs3Lidt_Idt16
        TMPL_BEGIN_TEXT
        lidt    [Bs3Lidt_Idt16 TMPL_WRT_SYSTEM16_OR_FLAT]

%elif BS3_MODE_IS_32BIT_SYS(TMPL_MODE)
        BS3_EXTERN_SYSTEM16 Bs3Lidt_Idt32
        TMPL_BEGIN_TEXT
        lidt    [Bs3Lidt_Idt32 TMPL_WRT_SYSTEM16_OR_FLAT]

%elif BS3_MODE_IS_64BIT_SYS(TMPL_MODE)
        BS3_EXTERN_SYSTEM16 Bs3Lidt_Idt64
        TMPL_BEGIN_TEXT
        lidt    [Bs3Lidt_Idt64 TMPL_WRT_SYSTEM16_OR_FLAT]
%else
 %error "TMPL_MODE"
%endif

%if !BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        ;
        ; Load the appropriate task selector.
        ; Always 64-bit in long mode, otherwise according to TMPL_BITS.
        ;
 %if BS3_MODE_IS_64BIT_SYS(TMPL_MODE)
        BS3_EXTERN_SYSTEM16 Bs3Gdte_Tss64
        TMPL_BEGIN_TEXT
        and     byte [5 + Bs3Gdte_Tss64 TMPL_WRT_SYSTEM16_OR_FLAT], ~X86_SEL_TYPE_SYS_TSS_BUSY_MASK
        mov     ax, BS3_SEL_TSS64

 %elif BS3_MODE_IS_16BIT_SYS(TMPL_MODE)
        BS3_EXTERN_SYSTEM16 Bs3Gdte_Tss16
        BS3_EXTERN_SYSTEM16 Bs3Gdte_Tss16DoubleFault
        TMPL_BEGIN_TEXT
        and     byte [5 + Bs3Gdte_Tss16            TMPL_WRT_SYSTEM16_OR_FLAT], ~X86_SEL_TYPE_SYS_TSS_BUSY_MASK
        and     byte [5 + Bs3Gdte_Tss16DoubleFault TMPL_WRT_SYSTEM16_OR_FLAT], ~X86_SEL_TYPE_SYS_TSS_BUSY_MASK
        mov     ax, BS3_SEL_TSS16

 %elif BS3_MODE_IS_32BIT_SYS(TMPL_MODE)
        BS3_EXTERN_SYSTEM16 Bs3Gdte_Tss32
        BS3_EXTERN_SYSTEM16 Bs3Gdte_Tss32DoubleFault
        BS3_EXTERN_SYSTEM16 Bs3Tss32
        BS3_EXTERN_SYSTEM16 Bs3Tss32DoubleFault
        TMPL_BEGIN_TEXT
        and     byte [5 + Bs3Gdte_Tss32            TMPL_WRT_SYSTEM16_OR_FLAT], ~X86_SEL_TYPE_SYS_TSS_BUSY_MASK
        and     byte [5 + Bs3Gdte_Tss32DoubleFault TMPL_WRT_SYSTEM16_OR_FLAT], ~X86_SEL_TYPE_SYS_TSS_BUSY_MASK
        mov     eax, cr3
        mov     [X86TSS32.cr3 + Bs3Tss32            TMPL_WRT_SYSTEM16_OR_FLAT], eax
        mov     [X86TSS32.cr3 + Bs3Tss32DoubleFault TMPL_WRT_SYSTEM16_OR_FLAT], eax
        mov     ax, BS3_SEL_TSS32
 %else
  %error "TMPL_BITS"
 %endif
        ltr     ax
%endif ; !TMPL_CMN_R86

%if !BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        ;
        ; Load the LDT.
        ;
        mov     ax, BS3_SEL_LDT
        lldt    ax
%endif

        ;
        ; Load ds and es; clear fs and gs.
        ;
%if BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        mov     ax, BS3_SEL_DATA16
%else
        mov     ax, RT_CONCAT(BS3_SEL_R0_DS,TMPL_BITS)
%endif
        mov     ds, ax
        mov     es, ax

%if TMPL_BITS == 16
        ; For restoring after Bs3Trap* calls below.
        push    ax
        push    ax

        cmp     byte [BS3_DATA16_WRT(g_uBs3CpuDetected)], BS3CPU_80286
        jbe     .skip_fs_gs
%endif
        xor     ax, ax
        mov     fs, ax
        mov     gs, ax
.skip_fs_gs:

        ;
        ; Set global indicating CPU mode.
        ;
        mov     byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], TMPL_MODE

        ;
        ; Install system call handler.
        ; Always 64-bit in long mode, otherwise according to TMPL_BITS.
        ;
%if BS3_MODE_IS_RM_SYS(TMPL_MODE)
        extern         _Bs3TrapSystemCallHandler_rm
        mov     word [ss: BS3_TRAP_SYSCALL*4], _Bs3TrapSystemCallHandler_rm wrt CGROUP16
        mov     word [ss: BS3_TRAP_SYSCALL*4 + 2], CGROUP16

%elif BS3_MODE_IS_16BIT_SYS(TMPL_MODE)
        BS3_EXTERN_CMN Bs3Trap16SetGate
        extern         TMPL_NM(Bs3TrapSystemCallHandler)
        BS3_BEGIN_TEXT16
        TMPL_BEGIN_TEXT
        push    0                       ; cParams
        push    TMPL_NM(Bs3TrapSystemCallHandler) wrt CGROUP16
        push    BS3_SEL_R0_CS16
        push    3                       ; DPL
        push    X86_SEL_TYPE_SYS_286_INT_GATE
        push    BS3_TRAP_SYSCALL
        BS3_CALL Bs3Trap16SetGate,6
        add     xSP, xCB * 6

%elif BS3_MODE_IS_32BIT_SYS(TMPL_MODE)
        BS3_EXTERN_CMN Bs3Trap32SetGate
        extern         TMPL_NM(Bs3TrapSystemCallHandler)
        TMPL_BEGIN_TEXT
        push    0                       ; cParams
        push    dword TMPL_NM(Bs3TrapSystemCallHandler) wrt FLAT
        push    BS3_SEL_R0_CS32
        push    3                       ; DPL
        push    X86_SEL_TYPE_SYS_386_INT_GATE
        push    BS3_TRAP_SYSCALL
        BS3_CALL Bs3Trap32SetGate,6
        add     xSP, xCB * 6

%elif BS3_MODE_IS_64BIT_SYS(TMPL_MODE)
        BS3_EXTERN_CMN Bs3Trap64SetGate
        extern         _Bs3TrapSystemCallHandler_lm64
        TMPL_BEGIN_TEXT
        push    0                       ; bIst
 %if BS3_MODE_IS_64BIT_CODE(TMPL_MODE)
        push    _Bs3TrapSystemCallHandler_lm64 wrt FLAT
 %else
        push    dword 0                 ; upper offset
        push    dword _Bs3TrapSystemCallHandler_lm64 wrt FLAT
 %endif
        push    BS3_SEL_R0_CS64
        push    3                       ; DPL
        push    AMD64_SEL_TYPE_SYS_INT_GATE
        push    BS3_TRAP_SYSCALL
        BS3_CALL Bs3Trap64SetGate,6
        add     xSP, xCB * 5 + 8
%else
 %error "TMPL_BITS"
%endif

%if TMPL_BITS == 16
        ; Restoring ds and es after the above calls.
        pop     es
        pop     ds
%endif

        ;
        ; Epilogue.
        ;
%if TMPL_BITS == 64
        pop     r9
        pop     r8
%endif
TONLY16 pop     xBX
        pop     xDX
        pop     xCX
        pop     xAX
%ifdef BS3_STRICT
        cmp     xBP, xSP
        je      .return_stack_ok
        int3
.return_stack_ok:
%endif
        pop     xBP
        ret
BS3_PROC_END_MODE   Bs3EnteredMode

