; $Id: bs3-c32-Trap32Generic.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Trap, 32-bit assembly handlers.
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

%ifndef TMPL_32BIT
 %error "32-bit only template"
%endif


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
BS3_EXTERN_DATA16 g_bBs3CurrentMode
BS3_EXTERN_DATA16 g_uBs3CpuDetected
BS3_EXTERN_DATA16 g_apfnBs3TrapHandlers_c32
BS3_EXTERN_SYSTEM16 Bs3Gdt
TMPL_BEGIN_TEXT
BS3_EXTERN_CMN Bs3TrapDefaultHandler
BS3_EXTERN_CMN Bs3RegCtxRestore
TMPL_BEGIN_TEXT


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BS3_BEGIN_DATA16
;; Easy to access flat address of Bs3Trap32GenericEntries.
BS3_GLOBAL_DATA g_Bs3Trap32GenericEntriesFlatAddr, 4
        dd Bs3Trap32GenericEntries wrt FLAT
;; Easy to access flat address of Bs3Trap32DoubleFaultHandler.
BS3_GLOBAL_DATA g_Bs3Trap32DoubleFaultHandlerFlatAddr, 4
        dd Bs3Trap32DoubleFaultHandler wrt FLAT


TMPL_BEGIN_TEXT

;;
; Generic entry points for IDT handlers, 8 byte spacing.
;
BS3_PROC_BEGIN Bs3Trap32GenericEntries
%macro Bs3Trap32GenericEntryNoErr 1
        push    byte 0                  ; 2 byte: fake error code.
        db      06ah, i                 ; 2 byte: push imm8 - note that this is a signextended value.
        jmp     near %1                 ; 5 byte
        ALIGNCODE(2)
%assign i i+1
%endmacro

%macro Bs3Trap32GenericEntryErrCd 1
        db      06ah, i                 ; 2 byte: push imm8 - note that this is a signextended value.
        jmp     near %1                 ; 5 byte
        db      0cch, 0cch              ; 2 byte: padding.
        ALIGNCODE(2)
%assign i i+1
%endmacro

%assign i 0                             ; start counter.
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 0
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 1
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 2
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 3
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 4
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 5
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 6
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 7
        Bs3Trap32GenericEntryErrCd bs3Trap32GenericTrapOrInt   ; 8
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 9
        Bs3Trap32GenericEntryErrCd bs3Trap32GenericTrapOrInt   ; a
        Bs3Trap32GenericEntryErrCd bs3Trap32GenericTrapOrInt   ; b
        Bs3Trap32GenericEntryErrCd bs3Trap32GenericTrapOrInt   ; c
        Bs3Trap32GenericEntryErrCd bs3Trap32GenericTrapOrInt   ; d
        Bs3Trap32GenericEntryErrCd bs3Trap32GenericTrapOrInt   ; e
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; f  (reserved)
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 10
        Bs3Trap32GenericEntryErrCd bs3Trap32GenericTrapOrInt   ; 11
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 12
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 13
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 14
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 15 (reserved)
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 16 (reserved)
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 17 (reserved)
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 18 (reserved)
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 19 (reserved)
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 1a (reserved)
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 1b (reserved)
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 1c (reserved)
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 1d (reserved)
        Bs3Trap32GenericEntryErrCd bs3Trap32GenericTrapOrInt   ; 1e
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt   ; 1f (reserved)
%rep 224
        Bs3Trap32GenericEntryNoErr bs3Trap32GenericTrapOrInt
%endrep
BS3_PROC_END  Bs3Trap32GenericEntries
AssertCompile(Bs3Trap32GenericEntries_EndProc - Bs3Trap32GenericEntries == 10*256)


;;
; Trap or interrupt with error code, faked if necessary.
;
BS3_PROC_BEGIN bs3Trap32GenericTrapOrInt
        push    ebp                     ; 0
        mov     ebp, esp
        pushfd                          ; -04h
        cld
        push    eax                     ; -08h
        push    edi                     ; -0ch
        lea     eax, [esp + (4+1+1)*4]  ; 4 pushes above, 1 exception number push, 1 error code.
        push    eax                     ; -10h = handler ESP
        add     eax, 3*4                ; 3 dword iret frame
        push    eax                     ; -14h = caller ESP if same CPL
        push    ss                      ; -18h
        push    ds                      ; -1ch

        ; Make sure we've got a flat DS. It makes everything so much simpler.
        mov     ax, ss
        and     al, 3
        AssertCompile(BS3_SEL_RING_SHIFT == 8)
        mov     ah, al
        add     ax, BS3_SEL_R0_DS32
        mov     ds, ax

        ;
        ; We may be comming from 16-bit code with a 16-bit SS.  Thunk it as
        ; the C code may assume flat SS and we'll mess up by using EBP/ESP/EDI
        ; instead of BP/SP/SS:DI. ASSUMES standard GDT selector.
        ;
        mov     ax, ss
        lar     eax, ax
        test    eax, X86LAR_F_D
        jz      .stack_thunk
        mov     ax, ss
        and     al, 3
        AssertCompile(BS3_SEL_RING_SHIFT == 8)
        mov     ah, al
        add     ax, BS3_SEL_R0_SS32
        mov     ss, ax
        jmp     .stack_flat
.stack_thunk:
        mov     di, ss
        and     edi, X86_SEL_MASK_OFF_RPL
        mov     al, [X86DESCGENERIC_BIT_OFF_BASE_HIGH1 / 8 + edi + Bs3Gdt wrt FLAT]
        mov     ah, [X86DESCGENERIC_BIT_OFF_BASE_HIGH2 / 8 + edi + Bs3Gdt wrt FLAT]
        shl     eax, 16
        mov     ax, [X86DESCGENERIC_BIT_OFF_BASE_LOW / 8   + edi + Bs3Gdt wrt FLAT] ; eax = SS.base
        movzx   ebp, bp                 ; SS:BP -> flat EBP.
        add     ebp, eax
        movzx   edi, sp                 ; SS:SP -> flat ESP in EAX.
        add     edi, eax
        mov     ax, ss
        and     al, 3
        AssertCompile(BS3_SEL_RING_SHIFT == 8)
        mov     ah, al
        add     ax, BS3_SEL_R0_SS32
        mov     ss, ax
        mov     esp, edi
        sub     dword [ebp - 10h], (4+1)*4   ; Recalc handler ESP in case of wraparound.
        add     word [ebp - 10h],  (4+1)*4
        sub     dword [ebp - 10h], (4+1+3)*4 ; Recalc caller ESP in case of wraparound.
        add     word [ebp - 10h],  (4+1+3)*4
.stack_flat:

        ; Reserve space for the register and trap frame.
        mov     eax, (BS3TRAPFRAME_size + 7) / 8
AssertCompileSizeAlignment(BS3TRAPFRAME, 8)
.more_zeroed_space:
        push    dword 0
        push    dword 0
        dec     eax
        jnz     .more_zeroed_space
        mov     edi, esp                ; edi points to trapframe structure.

        ; Copy stuff from the stack over.
        mov     eax, [ebp + 8]
;; @todo Do voodoo checks for 'int xx' or misguided hardware interrupts.
        mov     [edi + BS3TRAPFRAME.uErrCd], eax
        mov     al, [ebp + 4]
        mov     [edi + BS3TRAPFRAME.bXcpt], al
        mov     eax, [ebp]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], eax
        mov     eax, [ebp - 04h]
        mov     [edi + BS3TRAPFRAME.fHandlerRfl], eax
        mov     eax, [ebp - 08h]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], eax
        mov     eax, [ebp - 0ch]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], eax
        mov     eax, [ebp - 10h]
        mov     [edi + BS3TRAPFRAME.uHandlerRsp], eax
        mov     eax, [ebp - 14h]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], eax
        mov     ax, [ebp - 18h]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], ax
        mov     [edi + BS3TRAPFRAME.uHandlerSs], ax
        mov     ax, [ebp - 1ch]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], ax

        lea     ebp, [ebp + 8]          ; iret - 4 (i.e. ebp frame chain location)
        jmp     bs3Trap32GenericCommon
BS3_PROC_END   bs3Trap32GenericTrapErrCode


;;
; Common context saving code and dispatching.
;
; @param    edi     Pointer to the trap frame.  The following members have been
;                   filled in by the previous code:
;                       - bXcpt
;                       - uErrCd
;                       - fHandlerRfl
;                       - uHandlerRsp
;                       - uHandlerSs
;                       - Ctx.rax
;                       - Ctx.rbp
;                       - Ctx.rdi
;                       - Ctx.rsp - assuming same CPL
;                       - Ctx.ds
;                       - Ctx.ss
;
; @param    ebp     Pointer to the dword before the iret frame, i.e. where ebp
;                   would be saved if this was a normal call.
;
; @remarks This is a separate function for hysterical raisins.
;
BS3_PROC_BEGIN bs3Trap32GenericCommon
        ;
        ; Fake EBP frame.
        ;
        mov     eax, [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp]
        mov     [ebp], eax

        ;
        ; Save the remaining GPRs and segment registers.
        ;
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], ecx
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], edx
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], ebx
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], esi
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.es], es
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.fs], fs
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.gs], gs

        ;
        ; Load 32-bit data selector for the DPL we're executing at into DS and ES.
        ; Save the handler CS value first.
        ;
        mov     ax, cs
        mov     [edi + BS3TRAPFRAME.uHandlerCs], ax
        and     al, 3
        AssertCompile(BS3_SEL_RING_SHIFT == 8)
        mov     ah, al
        add     ax, BS3_SEL_R0_DS32
        mov     ds, ax
        mov     es, ax

        ;
        ; Copy and update the mode now that we've got a flat DS.
        ;
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], al
        and     al, ~BS3_MODE_CODE_MASK
        or      al, BS3_MODE_CODE_32
        mov     [BS3_DATA16_WRT(g_bBs3CurrentMode)], al

        ;
        ; Copy iret info.
        ;
        mov     ecx, [ebp + 4]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rip], ecx
        mov     ecx, [ebp + 12]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], ecx
        mov     cx, [ebp + 8]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.cs], cx
        test    dword [ebp + 12], X86_EFL_VM
        jnz     .iret_frame_v8086
        mov     ax, ss
        and     al, 3
        and     cl, 3
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], cl
        cmp     cl, al
        je      .iret_frame_same_cpl

.iret_frame_different_cpl:
        mov     ecx, [ebp + 16]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], ecx
        mov     cx, [ebp + 20]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        mov     byte [edi + BS3TRAPFRAME.cbIretFrame], 5*4
        jmp     .iret_frame_done

.iret_frame_v8086:
        mov     byte [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], 3
        or      byte [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], BS3_MODE_CODE_V86 ; paranoia ^ 2
        mov     ecx, [ebp + 16]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], ecx
        mov     cx, [ebp + 20]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        mov     cx, [ebp + 24]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.es], cx
        mov     cx, [ebp + 28]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], cx
        mov     cx, [ebp + 32]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.fs], cx
        mov     cx, [ebp + 36]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.gs], cx
        mov     byte [edi + BS3TRAPFRAME.cbIretFrame], 9*4
        jmp     .iret_frame_done

.iret_frame_same_cpl:                   ; (caller already set SS:RSP and uHandlerRsp for same CPL iret frames)
        mov     byte [edi + BS3TRAPFRAME.cbIretFrame], 3*4

.iret_frame_done:
        ;
        ; Control registers.
        ;
        str     ax
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.tr], ax
        sldt    ax
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.ldtr], ax

        mov     ax, ss
        test    al, 3
        jnz     .skip_crX_because_cpl_not_0

        mov     eax, cr3
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr3], eax
.save_cr0_cr2_cr4:                      ; The double fault code joins us here.
        mov     eax, cr0
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0], eax
        mov     eax, cr2
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr2], eax

        test    byte [1 + BS3_DATA16_WRT(g_uBs3CpuDetected)], (BS3CPU_F_CPUID >> 8) ; CR4 first appeared in later 486es.
        jz      .skip_cr4_because_not_there
        mov     eax, cr4
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr4], eax
        jmp     .set_flags

.skip_cr4_because_not_there:
        mov     byte [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.fbFlags], BS3REG_CTX_F_NO_CR4
        jmp     .set_flags

.skip_crX_because_cpl_not_0:
        or      byte [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.fbFlags], \
                BS3REG_CTX_F_NO_CR0_IS_MSW | BS3REG_CTX_F_NO_CR2_CR3 | BS3REG_CTX_F_NO_CR4
        smsw    [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0]
.set_flags:
        or      byte [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.fbFlags], BS3REG_CTX_F_NO_AMD64

        ;
        ; Dispatch it to C code.
        ;
.dispatch_to_handler:
        movzx   ebx, byte [edi + BS3TRAPFRAME.bXcpt]
        mov     eax, [ebx * 4 + BS3_DATA16_WRT(_g_apfnBs3TrapHandlers_c32)]
        or      eax, eax
        jnz     .call_handler
        mov     eax, Bs3TrapDefaultHandler
.call_handler:
        push    edi
        call    eax

        ;
        ; Resume execution using trap frame.
        ;
        push    0
        add     edi, BS3TRAPFRAME.Ctx
        push    edi
        call    Bs3RegCtxRestore
.panic:
        hlt
        jmp     .panic
BS3_PROC_END   bs3Trap32GenericCommon


;;
; Helper.
;
; @retruns  Flat address in eax.
; @param    ax
; @uses     eax
;
bs3Trap32TssInAxToFlatInEax:
        ; Get the GDT base address and find the descriptor address (EAX)
        sub     esp, 8+2
        sgdt    [esp]
        and     eax, 0fff8h
        add     eax, [esp + 2]          ; GDT base address.
        add     esp, 8+2

        ; Get the flat TSS address from the descriptor.
        mov     al, [eax + (X86DESCGENERIC_BIT_OFF_BASE_HIGH1 / 8)]
        mov     ah, [eax + (X86DESCGENERIC_BIT_OFF_BASE_HIGH2 / 8)]
        shl     eax, 16
        mov     ax, [eax + (X86DESCGENERIC_BIT_OFF_BASE_LOW / 8)]
        ret

;;
; Double fault handler.
;
; We don't have to load any selectors or clear anything in EFLAGS because the
; TSS specified sane values which got loaded during the task switch.
;
BS3_PROC_BEGIN Bs3Trap32DoubleFaultHandler
        push    0                       ; We'll copy the rip from the other TSS here later to create a more sensible call chain.
        push    ebp
        mov     ebp, esp

        pushfd                          ; Get handler flags.
        pop     ecx

        xor     edx, edx                ; NULL register.

        ;
        ; Allocate a zero filled trap frame.
        ;
        mov     eax, (BS3TRAPFRAME_size + 7) / 8
AssertCompileSizeAlignment(BS3TRAPFRAME, 8)
.more_zeroed_space:
        push    edx
        push    edx
        dec     eax
        jz      .more_zeroed_space
        mov     edi, esp

        ;
        ; Fill in the non-context trap frame bits.
        ;
        mov     [edi + BS3TRAPFRAME.fHandlerRfl], ecx
        mov     word [edi + BS3TRAPFRAME.bXcpt], X86_XCPT_DF
        mov     [edi + BS3TRAPFRAME.uHandlerCs], cs
        mov     [edi + BS3TRAPFRAME.uHandlerSs], ss
        lea     ecx, [ebp + 3*4]        ; two pushes, one error code.
        mov     [edi + BS3TRAPFRAME.uHandlerRsp], ecx
        mov     ecx, [ebp + 8]
        mov     [edi + BS3TRAPFRAME.uErrCd], ecx

        ;
        ; Copy the register state from the previous task segment.
        ;

        ; Find our TSS.
        str     ax
        call    bs3Trap32TssInAxToFlatInEax

        ; Find the previous TSS.
        mov     ax, [eax + X86TSS32.selPrev]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.tr], ax
        call    bs3Trap32TssInAxToFlatInEax

        ; Do the copying.
        mov     ecx, [eax + X86TSS32.eax]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], ecx
        mov     ecx, [eax + X86TSS32.ecx]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], ecx
        mov     ecx, [eax + X86TSS32.edx]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], ecx
        mov     ecx, [eax + X86TSS32.ebx]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], ecx
        mov     ecx, [eax + X86TSS32.esp]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], ecx
        mov     ecx, [eax + X86TSS32.ebp]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], ecx
        mov     [ebp], ecx              ; For better call stacks.
        mov     ecx, [eax + X86TSS32.esi]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], ecx
        mov     ecx, [eax + X86TSS32.edi]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], ecx
        mov     ecx, [eax + X86TSS32.esi]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], ecx
        mov     ecx, [eax + X86TSS32.eflags]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], ecx
        mov     ecx, [eax + X86TSS32.eip]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.rip], ecx
        mov     [ebp + 4], ecx          ; For better call stacks.
        mov     cx, [eax + X86TSS32.cs]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.cs], cx
        mov     cx, [eax + X86TSS32.ds]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], cx
        mov     cx, [eax + X86TSS32.es]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.es], cx
        mov     cx, [eax + X86TSS32.fs]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.fs], cx
        mov     cx, [eax + X86TSS32.gs]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.gs], cx
        mov     cx, [eax + X86TSS32.ss]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        mov     cx, [eax + X86TSS32.selLdt]             ; Note! This isn't necessarily the ldtr at the time of the fault.
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.ldtr], cx
        mov     cx, [eax + X86TSS32.cr3]                ; Note! This isn't necessarily the cr3 at the time of the fault.
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.cr3], ecx

        ;
        ; Set CPL; copy and update mode.
        ;
        mov     cl, [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.ss]
        and     cl, 3
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], cl

        mov     cl, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        mov     [edi + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], cl
        and     cl, ~BS3_MODE_CODE_MASK
        or      cl, BS3_MODE_CODE_32
        mov     [BS3_DATA16_WRT(g_bBs3CurrentMode)], cl

        ;
        ; Join code paths with the generic handler code.
        ;
        jmp     bs3Trap32GenericCommon.save_cr0_cr2_cr4
BS3_PROC_END   Bs3Trap32DoubleFaultHandler

