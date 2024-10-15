; $Id: cidet-appA.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; CPU Instruction Decoding & Execution Tests - Ring-3 Driver Application, Assembly Code.
;

;
; Copyright (C) 2009-2024 Oracle and/or its affiliates.
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


;*******************************************************************************
;*  Header Files                                                               *
;*******************************************************************************
%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"
%include "cidet.mac"


;*******************************************************************************
;*      Global Variables                                                       *
;*******************************************************************************
%ifdef RT_ARCH_X86
;; Used by CidetAppSaveAndRestoreCtx when we have a tricky target stack.
g_uTargetEip    dd 0
g_uTargetCs     dw 0
%endif


;;
; Leave GS alone on 64-bit darwin (gs is 0, no ldt or gdt entry to load that'll
; restore the lower 32-bits of the base when saving and restoring the register).
%ifdef RT_OS_DARWIN
 %ifdef RT_ARCH_AMD64
  %define CIDET_LEAVE_GS_ALONE
 %endif
%endif



BEGINCODE

;;
; ASSUMES that it's called and the EIP/RIP is found on the stack.
;
; @param    pSaveCtx     ds:xCX     The context to save; DS, xDX and xCX have
;                                   already been saved by the caller.
; @param    pRestoreCtx  ds:xDX     The context to restore.
;
BEGINPROC   CidetAppSaveAndRestoreCtx
        ;
        ; Save the stack pointer and program counter first so we can later
        ; bypass this step if we need to.
        ;
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xAX * 8], xAX ; need scratch register.
        lea     xAX, [xSP + xCB]
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xSP * 8], xAX
        mov     word [xCX + CIDETCPUCTX.aSRegs + X86_SREG_SS * 2], ss
        mov     word [xCX + CIDETCPUCTX.aSRegs + X86_SREG_CS * 2], cs
        mov     xAX, [xSP]
        mov     [xCX + CIDETCPUCTX.rip], xAX
        jmp     CidetAppSaveAndRestoreCtx_1

GLOBALNAME CidetAppSaveAndRestoreCtx_NoSsSpCsIp
        mov     [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xAX * 8], xAX
CidetAppSaveAndRestoreCtx_1:

        ; Flags.
%ifdef RT_ARCH_AMD64
        pushfq
%else
        pushfd
%endif
        pop     xAX
        mov     [xCX + CIDETCPUCTX.rfl], xAX

        ; Segment registers.
        mov     word [xCX + CIDETCPUCTX.aSRegs + X86_SREG_ES * 2], es
        mov     word [xCX + CIDETCPUCTX.aSRegs + X86_SREG_FS * 2], fs
        mov     word [xCX + CIDETCPUCTX.aSRegs + X86_SREG_GS * 2], gs

        ; Remaining GPRs.
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xBX * 8], xBX
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xBP * 8], xBP
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xSI * 8], xSI
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xDI * 8], xDI
%ifdef RT_ARCH_AMD64
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x8  * 8], r8
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x9  * 8], r9
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x10 * 8], r10
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x11 * 8], r11
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x12 * 8], r12
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x13 * 8], r13
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x14 * 8], r14
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x15 * 8], r15
        xor     eax, eax
        mov     [xCX + CIDETCPUCTX.cr2], rax
 %ifndef CIDET_REDUCED_CTX
        mov     [xCX + CIDETCPUCTX.cr0], rax
        mov     [xCX + CIDETCPUCTX.cr3], rax
        mov     [xCX + CIDETCPUCTX.cr4], rax
        mov     [xCX + CIDETCPUCTX.cr8], rax
        mov     [xCX + CIDETCPUCTX.dr0], rax
        mov     [xCX + CIDETCPUCTX.dr1], rax
        mov     [xCX + CIDETCPUCTX.dr2], rax
        mov     [xCX + CIDETCPUCTX.dr3], rax
        mov     [xCX + CIDETCPUCTX.dr6], rax
        mov     [xCX + CIDETCPUCTX.dr7], rax
        mov     [xCX + CIDETCPUCTX.tr], ax
        mov     [xCX + CIDETCPUCTX.ldtr], ax
 %endif
%else
        xor     eax, eax
        mov     [xCX + CIDETCPUCTX.rfl + 4], eax
        mov     [xCX + CIDETCPUCTX.rip + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xAX * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xCX * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xDX * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xBX * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xSP * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xBP * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xSI * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xDI * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x8  * 8    ], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x8  * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x9  * 8    ], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x9  * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x10 * 8    ], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x10 * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x11 * 8    ], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x11 * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x12 * 8    ], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x12 * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x13 * 8    ], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x13 * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x14 * 8    ], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x14 * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x15 * 8    ], eax
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_x15 * 8 + 4], eax
        mov     [xCX + CIDETCPUCTX.cr2    ], eax
        mov     [xCX + CIDETCPUCTX.cr2 + 4], eax
 %ifndef CIDET_REDUCED_CTX
        mov     [xCX + CIDETCPUCTX.cr0    ], eax
        mov     [xCX + CIDETCPUCTX.cr0 + 4], eax
        mov     [xCX + CIDETCPUCTX.cr3    ], eax
        mov     [xCX + CIDETCPUCTX.cr3 + 4], eax
        mov     [xCX + CIDETCPUCTX.cr4    ], eax
        mov     [xCX + CIDETCPUCTX.cr4 + 4], eax
        mov     [xCX + CIDETCPUCTX.cr8    ], eax
        mov     [xCX + CIDETCPUCTX.cr8 + 4], eax
        mov     [xCX + CIDETCPUCTX.dr0    ], eax
        mov     [xCX + CIDETCPUCTX.dr0 + 4], eax
        mov     [xCX + CIDETCPUCTX.dr1    ], eax
        mov     [xCX + CIDETCPUCTX.dr1 + 4], eax
        mov     [xCX + CIDETCPUCTX.dr2    ], eax
        mov     [xCX + CIDETCPUCTX.dr2 + 4], eax
        mov     [xCX + CIDETCPUCTX.dr3    ], eax
        mov     [xCX + CIDETCPUCTX.dr3 + 4], eax
        mov     [xCX + CIDETCPUCTX.dr6    ], eax
        mov     [xCX + CIDETCPUCTX.dr6 + 4], eax
        mov     [xCX + CIDETCPUCTX.dr7    ], eax
        mov     [xCX + CIDETCPUCTX.dr7 + 4], eax
        mov     [xCX + CIDETCPUCTX.tr], ax
        mov     [xCX + CIDETCPUCTX.ldtr], ax
 %endif
%endif
        dec     xAX
        mov     [xCX + CIDETCPUCTX.uErr], xAX
%ifdef RT_ARCH_X86
        mov     [xCX + CIDETCPUCTX.uErr + 4], eax
%endif
        mov     [xCX + CIDETCPUCTX.uXcpt], eax

        ;
        ; Restore the other state (pointer in xDX).
        ;
NAME(CidetAppSaveAndRestoreCtx_Restore):

        ; Restore ES, FS, and GS.
        mov     es, [xDX + CIDETCPUCTX.aSRegs + X86_SREG_ES * 2]
        mov     fs, [xDX + CIDETCPUCTX.aSRegs + X86_SREG_FS * 2]
%ifndef CIDET_LEAVE_GS_ALONE
        mov     gs, [xDX + CIDETCPUCTX.aSRegs + X86_SREG_GS * 2]
%endif

        ; Restore most GPRs (except xCX, xAX and xSP).
        mov     xCX, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xCX * 8]
        mov     xBX, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xBX * 8]
        mov     xBP, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xBP * 8]
        mov     xSI, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xSI * 8]
        mov     xDI, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xDI * 8]
%ifdef RT_ARCH_AMD64
        mov     r8,  [xDX + CIDETCPUCTX.aGRegs + X86_GREG_x8  * 8]
        mov     r9,  [xDX + CIDETCPUCTX.aGRegs + X86_GREG_x9  * 8]
        mov     r10, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_x10 * 8]
        mov     r11, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_x11 * 8]
        mov     r12, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_x12 * 8]
        mov     r13, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_x13 * 8]
        mov     r14, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_x14 * 8]
        mov     r15, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_x15 * 8]
%endif

%ifdef RT_ARCH_AMD64
        ; Create an iret frame which restores SS:RSP, RFLAGS, and CS:RIP.
        movzx   eax, word [xDX + CIDETCPUCTX.aSRegs + X86_SREG_SS * 2]
        push    xAX
        push    qword [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xSP * 8]
        push    qword [xDX + CIDETCPUCTX.rfl]
        movzx   eax, word [xDX + CIDETCPUCTX.aSRegs + X86_SREG_CS * 2]
        push    xAX
        push    qword [xDX + CIDETCPUCTX.rip]

        ; Restore DS, xAX and xDX then do the iret.
        mov     ds, [xDX + CIDETCPUCTX.aSRegs + X86_SREG_DS * 2]
        mov     xAX, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xAX * 8]
        mov     xDX, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xDX * 8]
        iretq
%else
        ; In 32-bit mode iret doesn't restore CS:ESP for us, so we have to
        ; make a choice whether the SS:ESP is more important than EFLAGS.
        cmp     byte [xDX + CIDETCPUCTX.fTrickyStack], 0
        jne     .tricky_stack

        mov     ss,  [xDX + CIDETCPUCTX.aSRegs + X86_SREG_SS * 2]
        mov     xSP, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xSP * 8]

        push    dword [xDX + CIDETCPUCTX.rfl]                           ; iret frame
        movzx   eax, word [xDX + CIDETCPUCTX.aSRegs + X86_SREG_CS * 2]  ; iret frame
        push    xAX                                                     ; iret frame
        push    dword [xDX + CIDETCPUCTX.rip]                           ; iret frame

        mov     xAX, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xAX * 8]
        mov     ds,  [xDX + CIDETCPUCTX.aSRegs + X86_SREG_DS * 2]
        mov     xDX, [cs:xDX + CIDETCPUCTX.aGRegs + X86_GREG_xDX * 8]
        iretd

.tricky_stack:
        mov     xAX, [xDX + CIDETCPUCTX.rip]
        mov     [g_uTargetEip], xAX
        mov     ax, [xDX + CIDETCPUCTX.aSRegs + X86_SREG_CS * 2]
        mov     [g_uTargetCs], ax
        push    dword [xDX + CIDETCPUCTX.rfl]
        popfd
        mov     ss,  [xDX + CIDETCPUCTX.aSRegs + X86_SREG_SS * 2]
        mov     xSP, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xSP * 8]
        mov     xAX, [xDX + CIDETCPUCTX.aGRegs + X86_GREG_xAX * 8]
        mov     ds,  [xDX + CIDETCPUCTX.aSRegs + X86_SREG_DS * 2]
        mov     xDX, [cs:xDX + CIDETCPUCTX.aGRegs + X86_GREG_xDX * 8]
        jmp     far [cs:g_uTargetEip]
%endif
ENDPROC     CidetAppSaveAndRestoreCtx


;;
; C callable version of CidetAppSaveAndRestoreCtx more or less.
;
; @param    pSaveCtx     x86:esp+4  gcc:rdi  msc:rcx
; @param    pRestoreCtx  x86:esp+8  gcc:rsi  msc:rdx
BEGINPROC   CidetAppExecute
%ifdef RT_ARCH_X86
        mov     ecx, [esp + 4]
        mov     edx, [esp + 8]
%elifdef ASM_CALL64_GCC
        mov     rcx, rdi
        mov     rdx, rsi
%elifndef ASM_CALL64_MSC
 %error "unsupport arch."
%endif
        mov     word [xCX + CIDETCPUCTX.aSRegs + X86_SREG_DS * 2], ds
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xDX * 8], xDX
        mov     [xCX + CIDETCPUCTX.aGRegs + X86_GREG_xCX * 8], xCX
        jmp     NAME(CidetAppSaveAndRestoreCtx)
ENDPROC     CidetAppExecute


;;
; C callable restore function.
;
; @param    pRestoreCtx  x86:esp+4  gcc:rdi  msc:rcx
BEGINPROC   CidetAppRestoreCtx
%ifdef RT_ARCH_X86
        mov     edx, [esp + 4]
%elifdef ASM_CALL64_GCC
        mov     rdx, rdi
%elifdef ASM_CALL64_MSC
        mov     rdx, rcx
%else
 %error "unsupport arch."
%endif
        mov     ds, [cs:xDX + CIDETCPUCTX.aSRegs + X86_SREG_DS * 2]
        jmp     NAME(CidetAppSaveAndRestoreCtx_Restore)
ENDPROC     CidetAppRestoreCtx

