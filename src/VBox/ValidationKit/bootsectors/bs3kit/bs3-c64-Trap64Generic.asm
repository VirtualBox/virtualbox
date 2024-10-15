; $Id: bs3-c64-Trap64Generic.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Trap, 64-bit assembly handlers.
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

;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "bs3kit-template-header.mac"

%ifndef TMPL_64BIT
 %error "64-bit only template"
%endif


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
BS3_EXTERN_DATA16 g_bBs3CurrentMode
BS3_EXTERN_DATA16 g_apfnBs3TrapHandlers_c64
TMPL_BEGIN_TEXT
BS3_EXTERN_CMN Bs3TrapDefaultHandler
BS3_EXTERN_CMN Bs3RegCtxRestore
TMPL_BEGIN_TEXT


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BS3_BEGIN_DATA16
;; Easy to access flat address of Bs3Trap64GenericEntries.
BS3_GLOBAL_DATA g_Bs3Trap64GenericEntriesFlatAddr, 4
        dd Bs3Trap64GenericEntries wrt FLAT


TMPL_BEGIN_TEXT

;;
; Generic entry points for IDT handlers, 8 byte spacing.
;
BS3_PROC_BEGIN Bs3Trap64GenericEntries
%macro Bs3Trap64GenericEntry 1
        db      06ah, i                 ; push imm8 - note that this is a signextended value.
        jmp     %1
        ALIGNCODE(8)
%assign i i+1
%endmacro

%assign i 0                             ; start counter.
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 0
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 1
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 2
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 3
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 4
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 5
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 6
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 7
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapErrCode ; 8
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 9
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapErrCode ; a
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapErrCode ; b
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapErrCode ; c
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapErrCode ; d
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapErrCode ; e
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; f  (reserved)
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 10
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapErrCode ; 11
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 12
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 13
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 14
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 15 (reserved)
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 16 (reserved)
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 17 (reserved)
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 18 (reserved)
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 19 (reserved)
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 1a (reserved)
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 1b (reserved)
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 1c (reserved)
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 1d (reserved)
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapErrCode ; 1e
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt   ; 1f (reserved)
%rep 224
        Bs3Trap64GenericEntry Bs3Trap64GenericTrapOrInt
%endrep
BS3_PROC_END  Bs3Trap64GenericEntries




;;
; Trap or interrupt (no error code).
;
BS3_PROC_BEGIN Bs3Trap64GenericTrapOrInt
        push    rbp                     ; 0
        mov     rbp, rsp
        pushfq                          ; -08h
        cld
        push    rdi

        ; Reserve space for the register and trap frame.
        mov     edi, (BS3TRAPFRAME_size + 15) / 16
.more_zeroed_space:
        push    qword 0
        push    qword 0
        dec     edi
        jnz     .more_zeroed_space
        mov     rdi, rsp                ; rdi points to trapframe structure.

        ; Free up rax.
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], rax

        ; Copy stuff from the stack over.
        mov     al, [rbp + 08h]
        mov     [rdi + BS3TRAPFRAME.bXcpt], al
        mov     rax, [rbp]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], rax
        mov     rax, [rbp - 08h]
        mov     [rdi + BS3TRAPFRAME.fHandlerRfl], rax
        mov     rax, [rbp - 10h]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], rax

        lea     rbp, [rbp + 08h]        ; iret - 8 (i.e. rbp frame chain location)
        jmp     Bs3Trap64GenericCommon
BS3_PROC_END   Bs3Trap64GenericTrapOrInt


;;
; Trap with error code.
;
BS3_PROC_BEGIN Bs3Trap64GenericTrapErrCode
        push    rbp                     ; 0
        mov     rbp, rsp
        pushfq                          ; -08h
        cld
        push    rdi

        ; Reserve space for the register and trap frame.
        mov     edi, (BS3TRAPFRAME_size + 15) / 16
.more_zeroed_space:
        push    qword 0
        push    qword 0
        dec     edi
        jnz     .more_zeroed_space
        mov     rdi, rsp                ; rdi points to trapframe structure.

        ; Free up rax.
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], rax

        ; Copy stuff from the stack over.
        mov     rax, [rbp + 10h]
        mov     [rdi + BS3TRAPFRAME.uErrCd], rax
        mov     al, [rbp + 08h]
        mov     [rdi + BS3TRAPFRAME.bXcpt], al
        mov     rax, [rbp]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], rax
        mov     rax, [rbp - 08h]
        mov     [rdi + BS3TRAPFRAME.fHandlerRfl], rax
        mov     rax, [rbp - 10h]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], rax

        lea     rbp, [rbp + 10h]        ; iret - 8 (i.e. rbp frame chain location)
        jmp     Bs3Trap64GenericCommon
BS3_PROC_END   Bs3Trap64GenericTrapErrCode


;;
; Common context saving code and dispatching.
;
; @param    rdi     Pointer to the trap frame.  The following members have been
;                   filled in by the previous code:
;                       - bXcpt
;                       - uErrCd
;                       - fHandlerRfl
;                       - Ctx.rax
;                       - Ctx.rbp
;                       - Ctx.rdi
;
; @param    rbp     Pointer to the dword before the iret frame, i.e. where rbp
;                   would be saved if this was a normal call.
;
BS3_PROC_BEGIN Bs3Trap64GenericCommon
        ;
        ; Fake RBP frame.
        ;
        mov     rax, [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp]
        mov     [rbp], rax

        ;
        ; Save the remaining GPRs and segment registers.
        ;
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], rcx
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], rdx
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], rbx
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], rsi
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.r8 ], r8
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.r9 ], r9
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.r10], r10
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.r11], r11
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.r12], r12
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.r13], r13
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.r14], r14
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.r15], r15
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], ds
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.es], es
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.fs], fs
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.gs], gs
        lea     rax, [rbp + 8h]
        mov     [rdi + BS3TRAPFRAME.uHandlerRsp], rax
        mov     [rdi + BS3TRAPFRAME.uHandlerSs], ss

        ;
        ; Load 32-bit data selector for the DPL we're executing at into DS, ES and SS.
        ; Save the handler CS value first.
        ;
        mov     ax, cs
        mov     [rdi + BS3TRAPFRAME.uHandlerCs], ax
        AssertCompile(BS3_SEL_RING_SHIFT == 8)
        and     al, 3
        mov     ah, al
        add     ax, BS3_SEL_R0_DS64
        mov     ds, ax
        mov     es, ax
        mov     ss, ax

        ;
        ; Copy and update the mode.
        ;
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], al
        and     al, ~BS3_MODE_CODE_MASK
        or      al, BS3_MODE_CODE_64
        mov     [BS3_DATA16_WRT(g_bBs3CurrentMode)], al

        ;
        ; Copy iret info.  Bless AMD for only doing one 64-bit iret frame layout.
        ;
        mov     rcx, [rbp + 08]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rip], rcx
        mov     cx,  [rbp + 10h]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.cs], cx
        and     cl, 3
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], cl
        mov     rcx, [rbp + 18h]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], rcx
        mov     rcx, [rbp + 20h]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], rcx
        mov     cx,  [rbp + 28h]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        mov     byte [rdi + BS3TRAPFRAME.cbIretFrame], 5*8

        ;
        ; Control registers.
        ;
        str     ax
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.tr], ax
        sldt    ax
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.ldtr], ax

        mov     ax, ss
        test    al, 3
        jnz     .skip_crX_because_cpl_not_0

        mov     rax, cr0
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0], rax
        mov     rax, cr2
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr2], rax
        mov     rax, cr3
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr3], rax
        mov     rax, cr4
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr4], rax
        jmp     .dispatch_to_handler

.skip_crX_because_cpl_not_0:
        or      byte [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.fbFlags], \
                BS3REG_CTX_F_NO_CR0_IS_MSW | BS3REG_CTX_F_NO_CR2_CR3 | BS3REG_CTX_F_NO_CR4
        smsw    [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0]

        ;
        ; Dispatch it to C code.
        ;
.dispatch_to_handler:                   ; The double fault code joins us here.
        movzx   ebx, byte [rdi + BS3TRAPFRAME.bXcpt]
        lea     rax, [BS3_DATA16_WRT(_g_apfnBs3TrapHandlers_c64)]
        mov     rax, [rax + rbx * 8]
        or      rax, rax
        jnz     .call_handler
        lea     rax, [BS3_WRT_RIP(Bs3TrapDefaultHandler)]
.call_handler:
        sub     rsp, 20h
        mov     [rsp], rdi
        mov     rcx, rdi
        call    rax

        ;
        ; Resume execution using trap frame.
        ;
        xor     edx, edx                        ; fFlags
        mov     [rsp + 8], rdx
        lea     rcx, [rdi + BS3TRAPFRAME.Ctx]   ; pCtx
        mov     [rsp], rcx
        call    Bs3RegCtxRestore
.panic:
        hlt
        jmp     .panic
BS3_PROC_END   Bs3Trap64GenericCommon

