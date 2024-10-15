; $Id: bs3-c16-Trap16Generic.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Trap, 16-bit assembly handlers.
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
BS3_EXTERN_SYSTEM16 Bs3Gdt
TMPL_BEGIN_TEXT
BS3_EXTERN_CMN Bs3TrapDefaultHandler
BS3_EXTERN_CMN Bs3RegCtxRestore
TMPL_BEGIN_TEXT


;;
; Generic entry points for IDT handlers, 8 byte spacing.
;
BS3_PROC_BEGIN _Bs3Trap16GenericEntries
BS3_PROC_BEGIN Bs3Trap16GenericEntries
%macro Bs3Trap16GenericEntryNoErr 1
        push    byte 0                  ; 2 byte: fake error code
        db      06ah, i                 ; 2 byte: push imm8 - note that this is a signextended value.
        jmp     %1                      ; 3 byte
        ALIGNCODE(8)
%assign i i+1
%endmacro

%macro Bs3Trap16GenericEntryErrCd 1
        db      06ah, i                 ; 2 byte: push imm8 - note that this is a signextended value.
        jmp     %1                      ; 3 byte
        ALIGNCODE(8)
%assign i i+1
%endmacro

%assign i 0                             ; start counter.
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 0
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 1
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 2
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 3
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 4
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 5
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 6
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 7
        Bs3Trap16GenericEntryErrCd bs3Trap16GenericTrapOrInt   ; 8
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 9
        Bs3Trap16GenericEntryErrCd bs3Trap16GenericTrapOrInt   ; a
        Bs3Trap16GenericEntryErrCd bs3Trap16GenericTrapOrInt   ; b
        Bs3Trap16GenericEntryErrCd bs3Trap16GenericTrapOrInt   ; c
        Bs3Trap16GenericEntryErrCd bs3Trap16GenericTrapOrInt   ; d
        Bs3Trap16GenericEntryErrCd bs3Trap16GenericTrapOrInt   ; e
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; f  (reserved)
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 10
        Bs3Trap16GenericEntryErrCd bs3Trap16GenericTrapOrInt   ; 11
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 12
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 13
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 14
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 15 (reserved)
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 16 (reserved)
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 17 (reserved)
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 18 (reserved)
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 19 (reserved)
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 1a (reserved)
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 1b (reserved)
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 1c (reserved)
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 1d (reserved)
        Bs3Trap16GenericEntryErrCd bs3Trap16GenericTrapOrInt   ; 1e
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt   ; 1f (reserved)
%rep 224
        Bs3Trap16GenericEntryNoErr bs3Trap16GenericTrapOrInt
%endrep
BS3_PROC_END  Bs3Trap16GenericEntries
AssertCompile(Bs3Trap16GenericEntries_EndProc - Bs3Trap16GenericEntries == 8*256)


;;
; Trap or interrupt with error code, faked if necessary.
;
; Note! This code is going to "misbehave" if the high word of ESP is not cleared.
;
BS3_PROC_BEGIN _bs3Trap16GenericTrapOrInt
BS3_PROC_BEGIN bs3Trap16GenericTrapOrInt
CPU 386
        jmp     near bs3Trap16GenericTrapErrCode80286 ; Bs3Trap16Init adjusts this on 80386+
        push    ebp
        movzx   ebp, sp
        push    ebx                     ; BP - 04h
        pushfd                          ; BP - 08h
        cld
        push    edx                     ; BP - 0ch
        push    ss                      ; BP - 0eh
        push    esp                     ; BP - 12h

        ;
        ; We may be comming from 32-bit code where SS is flat and ESP has a non-
        ; zero high word. We need to thunk it for C code to work correctly with
        ; [BP+xx] and [SS:BX+xx] style addressing that leaves out the high word.
        ;
        ; Note! Require ring-0 handler for non-standard stacks (SS.DPL must equal CPL).
        ;
        mov     bx, ss
        lar     ebx, bx
        test    ebx, X86LAR_F_D
        jz      .stack_fine
        test    esp, 0ffff0000h
        jnz     .stack_thunk
.stack_load_r0_ss16:
        mov     bx, ss
        and     bl, 3
        AssertCompile(BS3_SEL_RING_SHIFT == 8)
        mov     bh, bl
        add     bx, BS3_SEL_R0_SS16
        jmp     .stack_load_bx_into_ss
.stack_thunk:
        mov     ebx, esp
        shr     ebx, 16
        shl     ebx, X86_SEL_SHIFT
        add     ebx, BS3_SEL_TILED_R0
        cmp     ebx, BS3_SEL_TILED_R0_LAST
        ja      .stack_esp_out_of_bounds
.stack_load_bx_into_ss:
        mov     ss, bx
.stack_fine:
        movzx   esp, sp

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

        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], eax
        mov     edx, [bp - 12h]         ; This isn't quite right for wrap arounds, but close enough for now
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], edx     ; high bits
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], edx             ; high bits
        mov     dx, [bp - 0eh]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], dx
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

        mov     dl, [bp + 4]
        mov     [ss:bx + BS3TRAPFRAME.bXcpt], dl

        mov     dx, [bp + 6]
;; @todo Do voodoo checks for 'int xx' or misguided hardware interrupts.
        mov     [ss:bx + BS3TRAPFRAME.uErrCd], dx

        add     bp, 6                   ; adjust so it points to the word before the iret frame.
        xor     dx, dx
        jmp     bs3Trap16GenericCommon

.stack_esp_out_of_bounds:
%ifdef BS3_STRICT
        int3
%endif
        jmp     .stack_esp_out_of_bounds
BS3_PROC_END   bs3Trap16GenericTrapErrCode

;;
; Trap with error code - 80286 code variant.
;
BS3_PROC_BEGIN bs3Trap16GenericTrapErrCode80286
CPU 286
        push    bp
        mov     bp, sp
        push    bx
        pushf
        cld

        ; Reserve space for the register and trap frame.
        mov     bx, (BS3TRAPFRAME_size + 7) / 8
.more_zeroed_space:
        push    0
        push    0
        push    0
        push    0
        dec     bx
        jnz     .more_zeroed_space
        mov     bx, sp

        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], ax
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], ss
        mov     [ss:bx + BS3TRAPFRAME.uHandlerSs], ss
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], dx
        mov     dx, [bp - 4]
        mov     [ss:bx + BS3TRAPFRAME.fHandlerRfl], dx
        mov     dx, [bp - 2]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], dx
        mov     dx, [bp]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], dx

        mov     dl, [bp + 2]
        mov     [ss:bx + BS3TRAPFRAME.bXcpt], dl

        mov     dx, [bp + 4]
;; @todo Do voodoo checks for 'int xx' or misguided hardware interrupts.
        mov     [ss:bx + BS3TRAPFRAME.uErrCd], dx

        add     bp, 4                   ; adjust so it points to the word before the iret frame.
        mov     dl, 1
        jmp     bs3Trap16GenericCommon
BS3_PROC_END   bs3Trap16GenericTrapErrCode80286


;;
; Common context saving code and dispatching.
;
; @param    bx      Pointer to the trap frame, zero filled.  The following members
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
;                       - Ctx.ss     - for same cpl frames
;                       - All other bytes are zeroed.
;
; @param    bp      Pointer to the word before the iret frame, i.e. where bp
;                   would be saved if this was a normal near call.
; @param    dx      One (1) if 286, zero (0) if 386+.
;
BS3_PROC_BEGIN bs3Trap16GenericCommon
CPU 286
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
CPU 286
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], cx
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], di
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], si
.save_segment_registers:
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], ds
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.es], es
        mov     [ss:bx + BS3TRAPFRAME.uHandlerCs], cs

        ;
        ; Load 16-bit data selector for the DPL we're executing at into DS and ES.
        ;
        mov     ax, ss
        and     ax, 3
        mov     cx, ax
        shl     ax, BS3_SEL_RING_SHIFT
        or      ax, cx
        add     ax, BS3_SEL_R0_DS16
        mov     ds, ax
        mov     es, ax

        ;
        ; Copy and update the mode now that we've got a flat DS.
        ;
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], al
        mov     cl, al
        and     cl, ~BS3_MODE_CODE_MASK
        or      cl, BS3_MODE_CODE_16
        mov     [BS3_DATA16_WRT(g_bBs3CurrentMode)], cl

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

        test    al, BS3_MODE_CODE_V86
        jnz     .iret_frame_v8086

        mov     ax, ss
        and     al, 3
        and     cl, 3
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], cl
        cmp     cl, al
        je      .iret_frame_same_cpl

.ret_frame_different_cpl:
        mov     cx, [bp + 10]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        mov     cx, [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], cx
        mov     byte [ss:bx + BS3TRAPFRAME.cbIretFrame], 5*2
        test    dx, dx
        jnz     .iret_frame_done
        jmp     .iret_frame_seed_high_eip_word

.iret_frame_same_cpl: ; (ss and high bits was saved by CPU specific part)
        lea     cx, [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], cx
        mov     byte [ss:bx + BS3TRAPFRAME.cbIretFrame], 3*2
        test    dx, dx
        jnz     .iret_frame_done
        jmp     .iret_frame_seed_high_eip_word

.iret_frame_v8086:
CPU 386
        or      dword [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], X86_EFL_VM
        mov     byte [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], 3
        or      byte [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], BS3_MODE_CODE_V86 ; paranoia ^ 2
%if 0 ;; @todo testcase: high ESP word from V86 mode, 16-bit TSS.
        movzx   ecx, word [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], ecx
%else
        mov     cx, word [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], cx
%endif
        mov     cx, [bp + 10]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        mov     cx, [bp + 12]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.es], cx
        mov     cx, [bp + 14]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], cx
        mov     cx, [bp + 16]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.fs], cx
        mov     cx, [bp + 18]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.gs], cx
        mov     byte [ss:bx + BS3TRAPFRAME.cbIretFrame], 9*2
        jmp     .iret_frame_done

        ;
        ; For 386 we do special tricks to supply the high word of EIP when
        ; arriving here from 32-bit code. (ESP was seeded earlier.)
        ;
.iret_frame_seed_high_eip_word:
        lar     eax, [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cs]
        jnz     .iret_frame_done
        test    eax, X86LAR_F_D
        jz      .iret_frame_done
        mov     ax, [g_uBs3TrapEipHint+2]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rip + 2], ax

.iret_frame_done:
        ;
        ; Control registers.
        ;
        str     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.tr]
        sldt    [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ldtr]
        test    dx, dx
        jnz     .save_286_control_registers
.save_386_control_registers:
CPU 386
        mov     ax, ss
        test    al, 3
        jnz     .skip_crX_because_cpl_not_0
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

.skip_crX_because_cpl_not_0:
        or      byte [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.fbFlags], \
                BS3REG_CTX_F_NO_CR2_CR3 | BS3REG_CTX_F_NO_CR4 | BS3REG_CTX_F_NO_CR0_IS_MSW

CPU 286
.save_286_control_registers:
        smsw    [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0]

.set_flags:                             ; The double fault code joins us here.
        or      byte [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.fbFlags], BS3REG_CTX_F_NO_AMD64

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
        push    0
        push    ss
        add     di, BS3TRAPFRAME.Ctx
        push    di
        call    Bs3RegCtxRestore
.panic:
        hlt
        jmp     .panic
BS3_PROC_END   bs3Trap16GenericCommon


;;
; Helper.
;
; @retruns  Flat address in es:di.
; @param    di
; @uses     eax
;
bs3Trap16TssInDiToFar1616InEsDi:
CPU 286
        push    ax

        ; ASSUME Bs3Gdt is being used.
        push    BS3_SEL_SYSTEM16
        pop     es
        and     di, 0fff8h
        add     di, Bs3Gdt wrt BS3SYSTEM16

        ; Load the TSS base into ax:di (di is low, ax high)
        mov     al, [es:di + (X86DESCGENERIC_BIT_OFF_BASE_HIGH1 / 8)]
        mov     ah, [es:di + (X86DESCGENERIC_BIT_OFF_BASE_HIGH2 / 8)]
        mov     di, [es:di + (X86DESCGENERIC_BIT_OFF_BASE_LOW / 8)]

        ; Convert ax to tiled selector, if not within the tiling area we read
        ; random BS3SYSTEM16 bits as that's preferable to #GP'ing.
        shl     ax, X86_SEL_SHIFT
        cmp     ax, BS3_SEL_TILED_LAST - BS3_SEL_TILED
%ifdef BS3_STRICT
        jbe     .tiled
        int3
%endif
        ja      .return                 ; don't crash again.
.tiled:
        add     ax, BS3_SEL_TILED
        mov     es, ax
.return:
        pop     ax
        ret


;;
; Double fault handler.
;
; We don't have to load any selectors or clear anything in EFLAGS because the
; TSS specified sane values which got loaded during the task switch.
;
; @param    dx      Zero (0) for indicating 386+ to the common code.
;
BS3_PROC_BEGIN _Bs3Trap16DoubleFaultHandler80386
BS3_PROC_BEGIN Bs3Trap16DoubleFaultHandler80386
CPU 386
        push    0                       ; We'll copy the rip from the other TSS here later to create a more sensible call chain.
        push    ebp
        mov     bp, sp
        pushfd                          ; Handler flags.

        ; Reserve space for the register and trap frame.
        mov     bx, (BS3TRAPFRAME_size + 15) / 16
.more_zeroed_space:
        push    dword 0
        push    dword 0
        push    dword 0
        push    dword 0
        dec     bx
        jz      .more_zeroed_space
        mov     bx, sp

        ;
        ; Fill in the high GRP register words before we mess them up.
        ;
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], eax
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], ebx
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], ecx
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], edx
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], esi
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], edi
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], ebp
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], esp

        ;
        ; FS and GS are not part of the 16-bit TSS because they are 386+ specfic.
        ;
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.fs], fs
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.gs], gs

        ;
        ; Fill in the non-context trap frame bits.
        ;
        mov     ecx, [bp - 4]
        mov     [ss:bx + BS3TRAPFRAME.fHandlerRfl], ecx
        mov     byte [ss:bx + BS3TRAPFRAME.bXcpt], X86_XCPT_DF
        mov     [ss:bx + BS3TRAPFRAME.uHandlerCs], cs
        mov     [ss:bx + BS3TRAPFRAME.uHandlerSs], ss
        mov     ecx, esp
        lea     cx, [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], ecx
        mov     cx, [bp + 6]
        mov     [ss:bx + BS3TRAPFRAME.uErrCd], cx

        ;
        ; Copy 80386+ control registers.
        ;
        mov     ecx, cr0
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0], ecx
        mov     ecx, cr2
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr2], ecx
        mov     ecx, cr3
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr3], ecx

        test    byte [1 + BS3_DATA16_WRT(g_uBs3CpuDetected)], (BS3CPU_F_CPUID >> 8) ; CR4 first appeared in later 486es.
        jz      .skip_cr4_because_not_there
        mov     ecx, cr4
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr4], ecx
        jmp     .common

.skip_cr4_because_not_there:
        mov     byte [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.fbFlags], BS3REG_CTX_F_NO_CR4

        ;
        ; Copy the register state from the previous task segment.
        ; The 80286 code with join us here.
        ;
.common:
CPU 286
        ; Find our TSS.
        str     di
        call    bs3Trap16TssInDiToFar1616InEsDi

        ; Find the previous TSS.
        mov     di, [es:di + X86TSS32.selPrev]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.tr], ax
        call    bs3Trap16TssInDiToFar1616InEsDi

        ; Do the copying.
        mov     cx, [es:di + X86TSS16.ax]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], cx
        mov     cx, [es:di + X86TSS16.cx]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], cx
        mov     cx, [es:di + X86TSS16.dx]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], cx
        mov     cx, [es:di + X86TSS16.bx]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], cx
        mov     cx, [es:di + X86TSS16.sp]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], cx
        mov     cx, [es:di + X86TSS16.bp]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], cx
        mov     [bp], cx                ; For better call stacks.
        mov     cx, [es:di + X86TSS16.si]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], cx
        mov     cx, [es:di + X86TSS16.di]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], cx
        mov     cx, [es:di + X86TSS16.si]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], cx
        mov     cx, [es:di + X86TSS16.flags]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], cx
        mov     cx, [es:di + X86TSS16.ip]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rip], cx
        mov     [bp + 2], cx            ; For better call stacks.
        mov     cx, [es:di + X86TSS16.cs]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cs], cx
        mov     cx, [es:di + X86TSS16.ds]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], cx
        mov     cx, [es:di + X86TSS16.es]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.es], cx
        mov     cx, [es:di + X86TSS16.ss]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        mov     cx, [es:di + X86TSS16.selLdt]             ; Note! This isn't necessarily the ldtr at the time of the fault.
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ldtr], cx

        ;
        ; Set CPL; copy and update mode.
        ;
        mov     cl, [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss]
        and     cl, 3
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], cl

        mov     cl, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], cl
        and     cl, ~BS3_MODE_CODE_MASK
        or      cl, BS3_MODE_CODE_16
        mov     [BS3_DATA16_WRT(g_bBs3CurrentMode)], cl

        ;
        ; Join code paths with the generic handler code.
        ;
        jmp     bs3Trap16GenericCommon.set_flags
BS3_PROC_END   Bs3Trap16DoubleFaultHandler


;;
; Double fault handler.
;
; We don't have to load any selectors or clear anything in EFLAGS because the
; TSS specified sane values which got loaded during the task switch.
;
; @param    dx      One (1) for indicating 386+ to the common code.
;
BS3_PROC_BEGIN _Bs3Trap16DoubleFaultHandler80286
BS3_PROC_BEGIN Bs3Trap16DoubleFaultHandler80286
CPU 286
        push    0                       ; We'll copy the rip from the other TSS here later to create a more sensible call chain.
        push    bp
        mov     bp, sp
        pushf                           ; Handler flags.

        ; Reserve space for the register and trap frame.
        mov     bx, (BS3TRAPFRAME_size + 7) / 8
.more_zeroed_space:
        push    0
        push    0
        push    0
        push    0
        dec     bx
        jz      .more_zeroed_space
        mov     bx, sp

        ;
        ; Fill in the non-context trap frame bits.
        ;
        mov     cx, [bp - 2]
        mov     [ss:bx + BS3TRAPFRAME.fHandlerRfl], cx
        mov     byte [ss:bx + BS3TRAPFRAME.bXcpt], X86_XCPT_DF
        mov     [ss:bx + BS3TRAPFRAME.uHandlerCs], cs
        mov     [ss:bx + BS3TRAPFRAME.uHandlerSs], ss
        lea     cx, [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], cx
        mov     cx, [bp + 6]
        mov     [ss:bx + BS3TRAPFRAME.uErrCd], cx

        ;
        ; Copy 80286 specific control register.
        ;
        smsw    [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0]

        jmp     Bs3Trap16DoubleFaultHandler80386.common
BS3_PROC_END   Bs3Trap16DoubleFaultHandler80286

