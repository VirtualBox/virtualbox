; $Id: bs3-c64-Syscall64Generic.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Syscall, 64-bit assembly handlers.
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
TMPL_BEGIN_TEXT
BS3_EXTERN_CMN Bs3TrapDefaultHandler
BS3_EXTERN_CMN Bs3RegCtxRestore


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BS3_BEGIN_DATA16
;; Easy to access flat address of Bs3Syscall64Generic.
BS3_GLOBAL_DATA g_pfnBs3Syscall64GenericFlat, 4
        dd Bs3Syscall64Generic wrt FLAT
;; Easy to access flat address of Bs3Syscall64Generic.
BS3_GLOBAL_DATA g_pfnBs3Syscall64GenericCompatibilityFlat, 4
        dd Bs3Syscall64GenericCompatibility wrt FLAT


TMPL_BEGIN_TEXT

;;
; Generic function to load into LSTAR
;
; This will just skip 20h on the stack and set up a flat call frame there.
;
BS3_PROC_BEGIN Bs3Syscall64Generic
        lea     rsp, [rsp - 20h]
        push    rcx                         ; fake return address
        push    rbp                         ; 0
        mov     rbp, rsp
        push    0xffff                      ; rbp-08h: bXpct+cbIretFrame values
        jmp     Bs3Syscall64GenericCommon
BS3_PROC_END   Bs3Syscall64Generic


;;
; Generic function to load into CSTAR.
;
; Companion to Bs3Syscall64Generic.
;
BS3_PROC_BEGIN Bs3Syscall64GenericCompatibility
        lea     rsp, [rsp - 20h]
        push    rcx                         ; fake return address
        push    rbp                         ; 0
        mov     rbp, rsp
        push    0xfffe                      ; rbp-08h: bXpct+cbIretFrame values
        jmp     Bs3Syscall64GenericCommon
BS3_PROC_END   Bs3Syscall64GenericCompatibility


;;
; Common context saving code and dispatching.
;
; @param    rbp     Pointer to fake RBP frame.
;
BS3_PROC_BEGIN Bs3Syscall64GenericCommon
        pushfq                              ; rbp-10h
        cld
        push    rdi                         ; rbp-10h
        mov     di, ds
        push    rdi                         ; rbp-20h
        mov     di, ss
        mov     ds, di                      ; ds := ss

        ;
        ; Align the stack and reserve space for the register and trap frame.
        ;
        and     rsp, ~0xf
        mov     edi, (BS3TRAPFRAME_size + 15) / 16
.more_zeroed_space:
        push    qword 0
        push    qword 0
        dec     edi
        jnz     .more_zeroed_space
        mov     rdi, rsp                    ; rdi points to trapframe structure.

        ;
        ; Save rax so we can use it.
        ;
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], rax

        ;
        ; Mark the trap frame as a special one.
        ;
        mov     ax, [rbp - 08h]
        mov     word [rdi + BS3TRAPFRAME.bXcpt], ax ; Also sets cbIretFrame

        mov     word [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.cs], 0   ; We cannot tell.
        mov     word [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], 0   ; We cannot tell.
        mov     byte [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], 3 ; We cannot tell.

        mov     [rdi + BS3TRAPFRAME.uHandlerCs], cs
        mov     [rdi + BS3TRAPFRAME.uHandlerSs], ss

        ;
        ; Copy stuff from the stack over.
        ;
        mov     rax, [rbp]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], rax

        mov     rax, [rbp - 10h]
        mov     [rdi + BS3TRAPFRAME.fHandlerRfl], rax
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], r11 ; with RF cleared

        mov     rax, [rbp - 18h]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], rax

        lea     rax, [rbp + 20h + 8 + 8]
        mov     [rdi + BS3TRAPFRAME.uHandlerRsp], rax
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], rax

        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.rip], rcx

        mov     ax, [rbp - 20h]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], ax

        mov     [rdi + BS3TRAPFRAME.uHandlerSs], ss

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
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.es], es
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.fs], fs
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.gs], gs

        ;
        ; Load the SS selector into ES.
        ;
        mov     ax, ss
        mov     es, ax

        ;
        ; Copy and update the mode.
        ;
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], al
        mov     byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_LM64

        ;
        ; Control registers.
        ;
        str     ax
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.tr], ax
        sldt    ax
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.ldtr], ax

        mov     rax, cr0
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0], rax
        mov     rax, cr2
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr2], rax
        mov     rax, cr3
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr3], rax
        mov     rax, cr4
        mov     [rdi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr4], rax

        ;
        ; There are no _g_apfnBs3TrapHandlers_c64 entries for syscalls, but call
        ; Bs3TrapDefaultHandler to get the g_pBs3TrapSetJmpFrame handling & panic.
        ;
        sub     rsp, 20h
        mov     [rsp], rdi
        mov     rcx, rdi
        call    Bs3TrapDefaultHandler

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
BS3_PROC_END   Bs3Syscall64GenericCommon

