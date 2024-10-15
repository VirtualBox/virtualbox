; $Id: bs3-mode-SwitchToRM.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SwitchToRM
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

BS3_EXTERN_SYSTEM16 Bs3Gdt
%if TMPL_MODE == BS3_MODE_PE16
BS3_EXTERN_DATA16 g_uBs3CpuDetected
BS3_EXTERN_CMN Bs3KbdWrite
BS3_EXTERN_CMN Bs3KbdWait
%endif


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
%if TMPL_MODE == BS3_MODE_PE16
BS3_BEGIN_DATA16
;; Where to start restoring stack.
g_ResumeSp: dw 0xfeed
;; Where to start restoring stack.
g_ResumeSs: dw 0xface
%endif

TMPL_BEGIN_TEXT


;;
; Switch to real mode from any other mode.
;
; @cproto   BS3_DECL(void) Bs3SwitchToRM(void);
;
; @uses     GPRs and EFLAGS are unchanged (except high 32-bit register (AMD64) parts).
;           CS is loaded with CGROUP16.
;           SS:[RE]SP is converted to real mode address.
;           DS and ES are loaded with BS3DATA16_GROUP.
;           FS and GS are loaded with zero if present.
;
; @remarks  Obviously returns to 16-bit mode, even if the caller was
;           in 32-bit or 64-bit mode.
;
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
%if TMPL_BITS == 16
BS3_GLOBAL_NAME_EX TMPL_NM(Bs3SwitchToRM_Safe), function , 0
%endif
BS3_PROC_BEGIN_MODE Bs3SwitchToRM, BS3_PBC_NEAR
%ifdef TMPL_RM
        push    ax
        mov     ax, BS3_SEL_DATA16
        mov     ds, ax
        mov     es, ax
        pop     ax
        ret

%elif BS3_MODE_IS_V86(TMPL_MODE)
        ;
        ; V8086 - Switch to 16-bit ring-0 and call worker for that mode.
        ;
        extern  BS3_CMN_NM(Bs3SwitchToRing0)
        call    BS3_CMN_NM(Bs3SwitchToRing0)
        extern %[BS3_MODE_R0_NM_ %+ TMPL_MODE](Bs3SwitchToRM)
        jmp    %[BS3_MODE_R0_NM_ %+ TMPL_MODE](Bs3SwitchToRM)

%else
        ;
        ; Protected mode.
        ; 80286 requirements for PE16 clutters the code a little.
        ;
 %if TMPL_MODE == BS3_MODE_PE16
        cmp     byte [BS3_DATA16_WRT(g_uBs3CpuDetected)], BS3CPU_80286
        ja      .do_386_prologue
        push    ax
        push    bx
        pushf
        push    word 1
        jmp     .done_prologue
 %endif
.do_386_prologue:
        push    sAX
        push    sBX
        sPUSHF
 %if TMPL_MODE == BS3_MODE_PE16
        push    word 0
 %elif BS3_MODE_IS_64BIT_SYS(TMPL_MODE)
        push    sDX
        push    sCX
 %endif
.done_prologue:

        ;
        ; Get to 16-bit ring-0 and disable interrupts.
        ;
        extern  BS3_CMN_NM(Bs3SwitchToRing0)
        call    BS3_CMN_NM(Bs3SwitchToRing0)

        cli

 %if TMPL_MODE == BS3_MODE_PE16
        ;
        ; On 80286 we must reset the CPU to get back to real mode.
        ;
        CPU 286
        pop     ax
        push    ax
        test    ax, ax
        jz      .is_386_or_better

        ; Save registers and flags, storing SS:SP in at a known global address.
%ifdef BS3_STRICT
        mov     ax, 0feedh
        mov     bx, 0faceh
%endif
        push    di
        push    si
        push    bp
        push    bx
        push    dx
        push    cx
        push    ax
        pushf

        ; Convert ss:sp to real mode address.
        BS3_EXTERN_CMN Bs3SelProtFar32ToFlat32
        mov     ax, sp
        push    ss
        push    0
        push    ax
        call    Bs3SelProtFar32ToFlat32
        add     sp, 6

        mov     [g_ResumeSp], ax
        shl     dx, 12
        mov     [g_ResumeSs], dx

        ; Setup resume vector.
        mov     bx, BS3_SEL_R0_SS16
        mov     es, bx
        mov     word [es:467h],   .resume
        mov     word [es:467h+2], BS3_SEL_TEXT16

        mov     al, 0fh | 80h
        out     70h, al                 ; set register index
        in      al, 80h
        mov     al, 0ah                 ; shutdown action command - no EOI, no 287 reset.
        out     71h, al                 ; set cmos[f] = al - invoke testResume as early as possible.
        in      al, 71h                 ; flush

 %if 0 ; for testing in VM
        CPU 386
        mov     ax, BS3_SEL_R0_DS16
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax

        mov     eax, cr0
        and     ax, ~X86_CR0_PE
        mov     cr0, eax
        jmp     BS3_SEL_TEXT16:.resume
 %endif

        ; Port A reset. (FYI: tripple fault does not do the trick)
        in      al, 92h
        or      al, 1
        out     92h, al
        in      al, 80h                 ; flush
        mov     cx, 0ffffh
.reset_delay:
        loop    .reset_delay

        ; Keyboard controller reset.
        call    Bs3KbdWait
        push    0                       ; zero data (whatever.
        push    0fh                     ; KBD_CCMD_RESET
        call    Bs3KbdWrite
.forever:
        jmp     .forever

        ; This is the resume point. We should be in real mode now, at least in theory.
.resume:
        mov     ax, BS3_SEL_DATA16
        mov     ds, ax
        mov     es, ax
        mov     ax, [g_ResumeSp]
        mov     ss, [g_ResumeSs]
        mov     sp, ax

        popf
        pop     ax
        pop     cx
        pop     dx
        pop     bx
        pop     bp
        pop     si
        pop     di
 %ifdef BS3_STRICT
        cmp     ax, 0feedh
        jne     .bad_286_rm_switch
        cmp     bx, 0faceh
        jne     .bad_286_rm_switch
 %endif
        jmp     .enter_mode

 %ifdef BS3_STRICT
.bad_286_rm_switch:
        mov ax, 0e00h + 'Q'
        mov bx, 0ff00h
        int 10h
        jmp     .bad_286_rm_switch
 %endif

        CPU 386
 %elif TMPL_BITS != 16
        ;
        ; Must be in 16-bit segment when calling Bs3SwitchTo16Bit.
        ;
        jmp     .sixteen_bit_segment wrt FLAT
BS3_BEGIN_TEXT16
        BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .sixteen_bit_segment

        extern  BS3_CMN_NM(Bs3SwitchTo16Bit)
        call    BS3_CMN_NM(Bs3SwitchTo16Bit)
        BS3_SET_BITS 16
 %endif
        ;
        ; Before exiting to real mode we must load sensible selectors into the
        ; segment registers so the hidden parts (which doesn't get reloaded in
        ; real mode) are real mode compatible.
        ;
        ; ASSUMES BS3_SEL_R0_SS16 and BS3_SEL_R0_CS16 are both maxed out and
        ; has no funny bits set!
        ;
.is_386_or_better:
;; @todo Testcase: Experiment leaving weird stuff in the hidden segment registers.
        mov     ax, BS3_SEL_R0_DS16
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax

        ;
        ; Exit to real mode.
        ;
        mov     eax, cr0
        and     eax, X86_CR0_NO_PE_NO_PG
        mov     cr0, eax
        jmp     CGROUP16:.reload_cs
.reload_cs:

        ;
        ; Convert the stack (now 16-bit prot) to real mode.
        ;
        mov     ax, BS3_SEL_SYSTEM16
        mov     ds, ax
        mov     bx, ss
        and     bx, X86_SEL_MASK        ; ASSUMES GDT stack selector
        mov     al, [bx + 4 + Bs3Gdt]
        mov     ah, [bx + 7 + Bs3Gdt]
        add     sp, [bx + 2 + Bs3Gdt]   ; ASSUMES not expand down segment.
        adc     ax, 0
 %ifdef BS3_STRICT
        test    ax, 0fff0h
        jz      .stack_conv_ok
        int3
.stack_conv_ok:
 %endif
        shl     ax, 12
        mov     ss, ax
 %if TMPL_BITS != 16
        and     esp, 0ffffh
 %endif

 %if BS3_MODE_IS_64BIT_SYS(TMPL_MODE)
        ;
        ; Clear the long mode enable bit.
        ;
        mov     ecx, MSR_K6_EFER
        rdmsr
        and     eax, ~MSR_K6_EFER_LME
        wrmsr
 %endif

        ;
        ; Call routine for doing mode specific setups.
        ;
.enter_mode:
        extern  NAME(Bs3EnteredMode_rm)
        call    NAME(Bs3EnteredMode_rm)

 %if TMPL_MODE == BS3_MODE_PE16
        pop     ax
        test    ax, ax
        jz      .do_386_epilogue
        popf
        pop     bx
        pop     ax
        ret
 %endif
.do_386_epilogue:
 %if BS3_MODE_IS_64BIT_SYS(TMPL_MODE)
        pop     ecx
TONLY64 pop     eax
        pop     edx
TONLY64 pop     eax
 %endif
        popfd
TONLY64 pop     eax
        pop     ebx
TONLY64 pop     eax
        pop     eax
TONLY64 add     sp, 4
        retn    (TMPL_BITS - 16) / 8

 %if TMPL_BITS != 16
TMPL_BEGIN_TEXT
 %endif
%endif
BS3_PROC_END_MODE   Bs3SwitchToRM


%if TMPL_BITS == 16
;;
; Custom far stub.
BS3_BEGIN_TEXT16_FARSTUBS
BS3_PROC_BEGIN_MODE Bs3SwitchToRM, BS3_PBC_FAR
        inc         bp
        push        bp
        mov         bp, sp

        ; Call the real thing.
        call        TMPL_NM(Bs3SwitchToRM)

 %if !BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        ; Jmp to  common code for the tedious conversion.
        BS3_EXTERN_CMN Bs3SwitchHlpConvProtModeRetfPopBpDecBpAndReturn
        jmp         Bs3SwitchHlpConvProtModeRetfPopBpDecBpAndReturn
 %else
        pop         bp
        dec         bp
        retf
 %endif
BS3_PROC_END_MODE   Bs3SwitchToRM

%else
;;
; Safe far return to non-BS3TEXT16 code.
BS3_EXTERN_CMN Bs3SelFlatCodeToRealMode
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_PROC_BEGIN_MODE Bs3SwitchToRM_Safe, BS3_PBC_NEAR
 %if TMPL_BITS == 64
        push        xAX
        push        xCX
        sub         xSP, 20h

        mov         xCX, [xSP + xCB*2 + 20h]
        call        Bs3SelFlatCodeToRealMode ; well behaved assembly function, only clobbers ecx
        mov         [xSP + xCB*2 + 20h + 4], eax

        add         xSP, 20h
        pop         xCX
        pop         xAX
        add         xSP, 4
 %else
        xchg        eax, [xSP]
        push        xAX
        call        Bs3SelFlatCodeToRealMode ; well behaved assembly function, only clobbers eax
        add         xSP, 4
        xchg        [xSP], eax
 %endif
        call        TMPL_NM(Bs3SwitchToRM)
        BS3_SET_BITS 16
        retf
BS3_PROC_END_MODE   Bs3SwitchToRM_Safe
%endif

