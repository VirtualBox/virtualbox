; $Id: bs3-c16-TrapRmV86Generic.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Trap, 16-bit assembly handlers for real mode and v8086.
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

%ifndef TMPL_16BIT
 %error "16-bit only template"
%endif


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
BS3_EXTERN_DATA16 g_bBs3CurrentMode
BS3_EXTERN_DATA16 g_uBs3TrapEipHint
BS3_EXTERN_DATA16 g_uBs3CpuDetected
BS3_EXTERN_DATA16 g_apfnBs3TrapHandlers_c16
TMPL_BEGIN_TEXT
BS3_EXTERN_CMN Bs3TrapDefaultHandler
BS3_EXTERN_CMN Bs3RegCtxRestore
TMPL_BEGIN_TEXT


;;
; Generic entry points for IDT handlers, 8 byte spacing.
;
BS3_PROC_BEGIN _Bs3TrapRmV86GenericEntries
BS3_PROC_BEGIN Bs3TrapRmV86GenericEntries
%macro Bs3TrapRmV86GenericEntryNoErr 1
        push    ax                      ; 1 byte:  Reserve space for fake error cd.     (BP(+2) + 4)
        push    ax                      ; 1 byte:  Save AX                              (BP(+2) + 2)
        mov     ax, i | 00000h          ; 2 bytes: AL = trap/interrupt number; AH=indicate no error code
        jmp     %1                      ; 3 bytes: Jump to handler code
        ALIGNCODE(8)
%assign i i+1
%endmacro

%macro Bs3TrapRmV86GenericEntryErrCd 1
        Bs3TrapRmV86GenericEntryNoErr %1    ; No error code pushed in real mode or V86 mode.
%endmacro

%assign i 0                             ; start counter.
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 0
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 1
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 2
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 3
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 4
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 5
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 6
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 7
        Bs3TrapRmV86GenericEntryErrCd bs3TrapRmV86GenericTrapOrInt   ; 8
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 9
        Bs3TrapRmV86GenericEntryErrCd bs3TrapRmV86GenericTrapOrInt   ; a
        Bs3TrapRmV86GenericEntryErrCd bs3TrapRmV86GenericTrapOrInt   ; b
        Bs3TrapRmV86GenericEntryErrCd bs3TrapRmV86GenericTrapOrInt   ; c
        Bs3TrapRmV86GenericEntryErrCd bs3TrapRmV86GenericTrapOrInt   ; d
        Bs3TrapRmV86GenericEntryErrCd bs3TrapRmV86GenericTrapOrInt   ; e
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; f  (reserved)
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 10
        Bs3TrapRmV86GenericEntryErrCd bs3TrapRmV86GenericTrapOrInt   ; 11
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 12
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 13
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 14
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 15 (reserved)
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 16 (reserved)
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 17 (reserved)
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 18 (reserved)
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 19 (reserved)
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 1a (reserved)
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 1b (reserved)
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 1c (reserved)
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 1d (reserved)
        Bs3TrapRmV86GenericEntryErrCd bs3TrapRmV86GenericTrapOrInt   ; 1e
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt   ; 1f (reserved)
%rep 224
        Bs3TrapRmV86GenericEntryNoErr bs3TrapRmV86GenericTrapOrInt
%endrep
BS3_PROC_END  Bs3TrapRmV86GenericEntries
AssertCompile(Bs3TrapRmV86GenericEntries_EndProc - Bs3TrapRmV86GenericEntries == 8*256)


;;
; Trap or interrupt with error code, faked if necessary.
;
; early 386+ stack (movzx ebp, sp):
;       [bp + 000h]     ebp
;       [bp + 004h]     ax
;       [bp + 006h]     errcd                   [bp'+0] <--- bp at jmp to common code.
;       [bp + 008h]     cs                      [bp'+2]
;       [bp + 00ah]     ip                      [bp'+4]
;       [bp + 00ch]     flags                   [bp'+6]
;      ([bp + 00eh]     post-iret sp value)     [bp'+8]
;
BS3_PROC_BEGIN _bs3TrapRmV86GenericTrapOrInt
BS3_PROC_BEGIN bs3TrapRmV86GenericTrapOrInt
CPU 386
        jmp     near bs3TrapRmV86GenericTrapErrCode8086 ; Bs3TrapRmV86Init adjusts this on 80386+
        push    ebp
        movzx   ebp, sp
        push    ebx                     ; BP - 04h
        pushfd                          ; BP - 08h
        cld
        push    edx                     ; BP - 0ch
        push    ss                      ; BP - 0eh
        push    esp                     ; BP - 12h

        ; Reserve space for the register and trap frame.
        mov     bx, (BS3TRAPFRAME_size + 7) / 8
.more_zeroed_space:
        push    0
        push    0
        push    0
        push    0
        dec     bx
        jnz     .more_zeroed_space
        movzx   ebx, sp


        mov     edx, [bp - 12h]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], edx     ; high bits
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], edx             ; high bits
        mov     dx, [bp - 0eh]
        mov     [ss:bx + BS3TRAPFRAME.uHandlerSs], dx
        mov     edx, [bp - 0ch]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], edx
        mov     edx, [bp - 8]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], edx  ; high bits
        mov     [ss:bx + BS3TRAPFRAME.fHandlerRfl], edx
        mov     edx, [bp - 4]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], edx
        mov     edx, [bp]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], edx
        mov     edx, eax                                            ; high bits
        mov     dx, [bp + 4]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], edx

        mov     [ss:bx + BS3TRAPFRAME.bXcpt], al

        test    ah, 0ffh
        jz      .no_error_code
        mov     dx, [bp + 6]
        mov     [ss:bx + BS3TRAPFRAME.uErrCd], dx
.no_error_code:

        add     bp, 6                   ; adjust so it points to the word before the iret frame.
        xor     dx, dx
        jmp     bs3TrapRmV86GenericCommon
BS3_PROC_END   bs3TrapRmV86GenericTrapErrCode

;;
; Trap with error code - 8086/V20/80186/80286 code variant.
;
BS3_PROC_BEGIN bs3TrapRmV86GenericTrapErrCode8086
CPU 8086
        push    bp
        mov     bp, sp
        push    bx                      ; BP - 2
        pushf                           ; BP - 4
        push    ax                      ; BP - 6
        cld

        ; Reserve space for the register and trap frame.
        mov     bx, (BS3TRAPFRAME_size + 7) / 8
        xor     ax, ax
.more_zeroed_space:
        push    ax
        push    ax
        push    ax
        push    ax
        dec     bx
        jnz     .more_zeroed_space
        mov     bx, sp

        mov     [ss:bx + BS3TRAPFRAME.uHandlerSs], ss
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], dx
        mov     dx, [bp - 4]
        mov     [ss:bx + BS3TRAPFRAME.fHandlerRfl], dx
        mov     dx, [bp - 2]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], dx
        mov     dx, [bp]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], dx

        mov     dx, [bp + 2]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], dx

        mov     ax, [bp - 6]
        mov     [ss:bx + BS3TRAPFRAME.bXcpt], al

        test    ah, 0ffh
        jz      .no_error_code
        mov     dx, [bp + 4]
        mov     [ss:bx + BS3TRAPFRAME.uErrCd], dx
.no_error_code:

        add     bp, 4                   ; adjust so it points to the word before the iret frame.
        mov     dl, 1
        jmp     bs3TrapRmV86GenericCommon
BS3_PROC_END   bs3TrapRmV86GenericTrapErrCode8086


;;
; Common context saving code and dispatching.
;
; @param    ss:bx   Pointer to the trap frame, zero filled.  The following members
;                   have been filled in by the previous code:
;                       - bXcpt
;                       - uErrCd
;                       - fHandlerRFL
;                       - Ctx.eax
;                       - Ctx.edx
;                       - Ctx.ebx
;                       - Ctx.ebp
;                       - Ctx.rflags - high bits only.
;                       - Ctx.esp    - high bits only.
;                       - All other bytes are zeroed.
;
; @param    bp      Pointer to the word before the iret frame, i.e. where bp
;                   would be saved if this was a normal near call.
; @param    dx      One (1) if 286, zero (0) if 386+.
;
BS3_PROC_BEGIN bs3TrapRmV86GenericCommon
CPU 8086
        ;
        ; Fake EBP frame.
        ;
        mov     ax, [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp]
        mov     [bp], ax

        ;
        ; Save the remaining GPRs and segment registers.
        ;
        test    dx, dx
        jnz     .save_word_grps
CPU 386
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], ecx
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], edi
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], esi
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.fs], fs
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.gs], gs
        jmp     .save_segment_registers
.save_word_grps:
CPU 8086
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], cx
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], di
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], si
.save_segment_registers:
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], ds
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.es], es
        mov     [ss:bx + BS3TRAPFRAME.uHandlerCs], cs

        ;
        ; Load 16-bit BS3KIT_GRPNM_DATA16 into DS and ES so we can access globals.
        ;
        mov     ax, BS3KIT_GRPNM_DATA16
        mov     ds, ax
        mov     es, ax

        ;
        ; Copy the mode now that we've got a flat DS.  We don't need to update
        ; it as it didn't change.
        ;
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], al

        ;
        ; Copy iret info.
        ;
        lea     cx, [bp + 2]
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], cx
        mov     cx, [bp + 2]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rip], cx
        mov     cx, [bp + 6]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], cx
        mov     cx, [bp + 4]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cs], cx
        mov     cx, ss
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        lea     cx, [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], cx
        mov     byte [ss:bx + BS3TRAPFRAME.cbIretFrame], 3*2

        ; The VM flag and CPL.
        test    al, BS3_MODE_CODE_V86
        jz      .dont_set_vm
        or      byte [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags + 2], X86_EFL_VM >> 16
        mov     byte [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], 3
.dont_set_vm:


        ;
        ; Control registers.
        ;
        ; Since we're in real or v8086 here, we cannot save TR and LDTR.
        ; But get MSW (CR0) first since that's always accessible and we
        ; need it even on a 386 to check whether we're in v8086 mode or not.
        ;
        cmp     byte [BS3_DATA16_WRT(g_uBs3CpuDetected)], BS3CPU_80286
        jb      .skip_control_registers_because_80186_or_older
CPU 286
        smsw    ax
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0], ax

        test    dx, dx
        jnz     .set_flags
.save_386_control_registers:
CPU 386
        ; 386 control registers are not accessible from virtual 8086 mode.
        test    al, X86_CR0_PE
        jnz     .skip_crX_because_v8086
        mov     eax, cr0
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0], eax
        mov     eax, cr2
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr2], eax
        mov     eax, cr3
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr3], eax

        test    byte [1 + BS3_DATA16_WRT(g_uBs3CpuDetected)], (BS3CPU_F_CPUID >> 8) ; CR4 first appeared in later 486es.
        jz      .skip_cr4_because_not_there
        mov     eax, cr4
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr4], eax
        jmp     .set_flags

.skip_cr4_because_not_there:
        mov     byte [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.fbFlags], BS3REG_CTX_F_NO_CR4
        jmp     .set_flags

CPU 8086
.skip_control_registers_because_80186_or_older:
.skip_crX_because_v8086:
        or      byte [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.fbFlags], \
                BS3REG_CTX_F_NO_CR0_IS_MSW | BS3REG_CTX_F_NO_CR2_CR3 | BS3REG_CTX_F_NO_CR4
.set_flags:                             ; The double fault code joins us here.
        or      byte [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.fbFlags], BS3REG_CTX_F_NO_AMD64 | BS3REG_CTX_F_NO_TR_LDTR

        ;
        ; Dispatch it to C code.
        ;
.dispatch_to_handler:
        mov     di, bx
        mov     bl, byte [ss:bx + BS3TRAPFRAME.bXcpt]
        mov     bh, 0
        shl     bx, 1
        mov     bx, [bx + BS3_DATA16_WRT(_g_apfnBs3TrapHandlers_c16)]
        or      bx, bx
        jnz     .call_handler
        mov     bx, Bs3TrapDefaultHandler
.call_handler:
        push    ss
        push    di
        call    bx

        ;
        ; Resume execution using trap frame.
        ;
        xor     ax, ax
        push    ax
        push    ss
        add     di, BS3TRAPFRAME.Ctx
        push    di
        call    Bs3RegCtxRestore
.panic:
        hlt
        jmp     .panic
BS3_PROC_END   bs3TrapRmV86GenericCommon

