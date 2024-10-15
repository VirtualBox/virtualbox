; $Id: bs3-mode-TestDoModesHlp.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3TestDoModes helpers
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


;*********************************************************************************************************************************
;*  Defined Constants And Macros                                                                                                 *
;*********************************************************************************************************************************
;
; We put most of this mess in the RMTEXT16 segment when in real mode.
;
%if TMPL_MODE == BS3_MODE_RM
 %define MY_BEGIN_TEXT          BS3_BEGIN_RMTEXT16
 %define MY_BEGIN_TEXT16        BS3_BEGIN_RMTEXT16
 %define MY_TEXT16_WRT(a_Label) a_Label wrt BS3GROUPRMTEXT16
%else
 %define MY_BEGIN_TEXT          TMPL_BEGIN_TEXT
 %define MY_BEGIN_TEXT16        BS3_BEGIN_TEXT16
 %define MY_TEXT16_WRT(a_Label) BS3_TEXT16_WRT(a_Label)
%endif


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
%if TMPL_MODE == BS3_MODE_RM
BS3_BEGIN_TEXT16_FARSTUBS
extern TMPL_FAR_NM(Bs3SwitchToRM)
extern TMPL_FAR_NM(Bs3SwitchToPE16)
extern TMPL_FAR_NM(Bs3SwitchToPE16_32)
extern TMPL_FAR_NM(Bs3SwitchToPE16_V86)
extern TMPL_FAR_NM(Bs3SwitchToPE32)
extern TMPL_FAR_NM(Bs3SwitchToPE32_16)
extern TMPL_FAR_NM(Bs3SwitchToPEV86)
extern TMPL_FAR_NM(Bs3SwitchToPP16)
extern TMPL_FAR_NM(Bs3SwitchToPP16_32)
extern TMPL_FAR_NM(Bs3SwitchToPP16_V86)
extern TMPL_FAR_NM(Bs3SwitchToPP32)
extern TMPL_FAR_NM(Bs3SwitchToPP32_16)
extern TMPL_FAR_NM(Bs3SwitchToPPV86)
extern TMPL_FAR_NM(Bs3SwitchToPAE16)
extern TMPL_FAR_NM(Bs3SwitchToPAE16_32)
extern TMPL_FAR_NM(Bs3SwitchToPAE16_V86)
extern TMPL_FAR_NM(Bs3SwitchToPAE32)
extern TMPL_FAR_NM(Bs3SwitchToPAE32_16)
extern TMPL_FAR_NM(Bs3SwitchToPAEV86)
extern TMPL_FAR_NM(Bs3SwitchToLM16)
extern TMPL_FAR_NM(Bs3SwitchToLM32)
extern TMPL_FAR_NM(Bs3SwitchToLM64)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_rm_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe16_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe16_v86_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe32_16_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pev86_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp16_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp16_v86_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp32_16_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_ppv86_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae16_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae16_v86_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae32_16_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_paev86_far)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_lm16_far)
%else
BS3_BEGIN_TEXT16
extern TMPL_NM(Bs3SwitchToRM)
extern TMPL_NM(Bs3SwitchToPE16)
extern TMPL_NM(Bs3SwitchToPE16_32)
extern TMPL_NM(Bs3SwitchToPE16_V86)
extern TMPL_NM(Bs3SwitchToPE32)
extern TMPL_NM(Bs3SwitchToPE32_16)
extern TMPL_NM(Bs3SwitchToPEV86)
extern TMPL_NM(Bs3SwitchToPP16)
extern TMPL_NM(Bs3SwitchToPP16_32)
extern TMPL_NM(Bs3SwitchToPP16_V86)
extern TMPL_NM(Bs3SwitchToPP32)
extern TMPL_NM(Bs3SwitchToPP32_16)
extern TMPL_NM(Bs3SwitchToPPV86)
extern TMPL_NM(Bs3SwitchToPAE16)
extern TMPL_NM(Bs3SwitchToPAE16_32)
extern TMPL_NM(Bs3SwitchToPAE16_V86)
extern TMPL_NM(Bs3SwitchToPAE32)
extern TMPL_NM(Bs3SwitchToPAE32_16)
extern TMPL_NM(Bs3SwitchToPAEV86)
extern TMPL_NM(Bs3SwitchToLM16)
extern TMPL_NM(Bs3SwitchToLM32)
extern TMPL_NM(Bs3SwitchToLM64)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_rm)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pe16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pe16_v86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pe32_16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pev86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pp16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pp16_v86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pp32_16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_ppv86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pae16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pae16_v86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pae32_16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_paev86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_lm16)
%endif
BS3_BEGIN_TEXT16
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pe16_32):wrt BS3FLAT
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pe32):wrt BS3FLAT
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pp16_32):wrt BS3FLAT
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pp32):wrt BS3FLAT
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pae16_32):wrt BS3FLAT
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pae32):wrt BS3FLAT
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_lm32):wrt BS3FLAT
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_lm64):wrt BS3FLAT


MY_BEGIN_TEXT16                         ; need the group definition
MY_BEGIN_TEXT

;;
; Shared prologue code.
; @param    xAX     Where to jump to for the main event.
;
BS3_GLOBAL_NAME_EX TMPL_NM(bs3TestCallDoerPrologue), , 0
        BS3_CALL_CONV_PROLOG 1
        push    xBP
        mov     xBP, xSP
        xPUSHF

        ; Save non-volatile registers so the DO function doesn't have to.
        push    xBX
        push    xCX
        push    xDX
        push    xSI
        push    xDI
%if TMPL_BITS != 64
        push    ds
        push    es
        push    ss
 %if TMPL_BITS != 16
        push    fs
        push    gs
 %endif
%endif
%if TMPL_BITS == 64
        push    r8
        push    r9
        push    r10
        push    r11
        push    r12
        push    r13
        push    r14
        push    r15
%endif

        ; Jump to the main code.
        jmp     xAX

;;
; Shared epilogue code.
; @param    xAX     Return code.
;
BS3_GLOBAL_NAME_EX TMPL_NM(bs3TestCallDoerEpilogue), , 0
        ; Restore registers.
%if TMPL_BITS == 16
        sub     bp, (1+5+3)*2
        mov     sp, bp
%elif TMPL_BITS == 32
        lea     xSP, [xBP - (1+5+5)*4]
%else
        lea     xSP, [xBP - (1+5+8)*8]
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     r11
        pop     r10
        pop     r9
        pop     r8
%endif
%if TMPL_BITS != 64
 %if TMPL_BITS != 16
        pop     gs
        pop     fs
 %endif
        pop     ss
        pop     es
        pop     ds
%endif
        pop     xDI
        pop     xSI
        pop     xDX
        pop     xCX
        pop     xBX
        xPOPF
        pop     xBP
        ret

;
; For checking that the mode switching macros doesn't screw up GPRs.
; Note! Does not work on pre 286 hardware! So, for debugging only.
;
%if 0
 %macro STRICT_SAVE_REGS 0
        movzx   esp, sp
        sub     esp, BS3REGCTX_size
        mov     [esp + BS3REGCTX.rax], eax
        mov     dword [esp + BS3REGCTX.rax+4], 0xdead0000
        mov     [esp + BS3REGCTX.rcx], ecx
        mov     dword [esp + BS3REGCTX.rcx+4], 0xdead0001
        mov     [esp + BS3REGCTX.rdx], edx
        mov     dword [esp + BS3REGCTX.rdx+4], 0xdead0002
        mov     [esp + BS3REGCTX.rbx], ebx
        mov     dword [esp + BS3REGCTX.rbx+4], 0xdead0003
        mov     [esp + BS3REGCTX.rbp], ebp
        mov     [esp + BS3REGCTX.rsp], esp
        mov     [esp + BS3REGCTX.rsi], esi
        mov     [esp + BS3REGCTX.rdi], edi
 %endmacro

 %macro STRICT_CHECK_REGS 0
%%_esp: cmp     [esp + BS3REGCTX.rsp], esp
        jne     %%_esp
%%_eax: cmp     [esp + BS3REGCTX.rax], eax
        jne     %%_eax
%%_ecx: mov     [esp + BS3REGCTX.rcx], ecx
        jne     %%_ecx
%%_edx: cmp     [esp + BS3REGCTX.rdx], edx
        jne     %%_edx
%%_ebx: cmp     [esp + BS3REGCTX.rbx], ebx
        jne     %%_ebx
%%_ebp: cmp     [esp + BS3REGCTX.rbp], ebp
        jne     %%_ebp
%%_esi: cmp     [esp + BS3REGCTX.rsi], esi
        jne     %%_esi
%%_edi: cmp     [esp + BS3REGCTX.rdi], edi
        jne     %%_edi
        add     esp, BS3REGCTX_size
 %endmacro
%else

 %macro STRICT_SAVE_REGS 0
 %endmacro
 %macro STRICT_CHECK_REGS 0
 %endmacro
%endif


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Real mode
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInRM(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInRM, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToRM)
%else
        call    TMPL_NM(Bs3SwitchToRM)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        mov     cx, BS3_MODE_RM
        push    cx
        push    cs
        mov     cx, .return
        push    cx
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_rm_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_rm)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInRM


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Unpage protection mode.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPE16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPE16, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPE16)
%else
        call    TMPL_NM(Bs3SwitchToPE16)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PE16
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe16_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pe16)
%endif
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPE16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPE16_32(uint32_t FlatWorkerAddr, uint8_t bMode);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPE16_32, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB + cbCurRetAddr]      ; Load function pointer.
        movzx   edx, byte [xBP + xCB + cbCurRetAddr + sCB] ; bMode

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPE16_32)
%else
        call    TMPL_NM(Bs3SwitchToPE16_32)
%endif
        BS3_SET_BITS 32
        STRICT_CHECK_REGS

        push    edx                     ; bMode
        call    eax

        STRICT_SAVE_REGS
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pe16_32)
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPE16_32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPE16_V86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPE16_V86, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPE16_V86)
%else
        call    TMPL_NM(Bs3SwitchToPE16_V86)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PE16_V86
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe16_v86_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pe16_v86)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPE16_V86

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPE32(uint32_t FlatWorkerAddr, uint8_t bMode);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPE32, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB + cbCurRetAddr]      ; Load function pointer.
        movzx   edx, byte [xBP + xCB + cbCurRetAddr + sCB] ; bMode

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPE32)
%else
        call    TMPL_NM(Bs3SwitchToPE32)
%endif
        BS3_SET_BITS 32
        STRICT_CHECK_REGS

        push    edx                     ; bMode
        call    eax

        STRICT_SAVE_REGS
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pe32)
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPE32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPE32_16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPE32_16, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPE32_16)
%else
        call    TMPL_NM(Bs3SwitchToPE32_16)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PE32_16
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe32_16_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pe32_16)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPE32_16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPEV86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPEV86, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPEV86)
%else
        call    TMPL_NM(Bs3SwitchToPEV86)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PEV86
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pev86_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pev86)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPEV86



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Page protection mode.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPP16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPP16, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPP16)
%else
        call    TMPL_NM(Bs3SwitchToPP16)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PP16
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp16_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pp16)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPP16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPP16_32(uint32_t uFlatWorkerAddr, uint8_t bMode);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPP16_32, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB + cbCurRetAddr]      ; Load function pointer.
        movzx   edx, byte [xBP + xCB + cbCurRetAddr + sCB] ; bMode

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPP16_32)
%else
        call    TMPL_NM(Bs3SwitchToPP16_32)
%endif
        BS3_SET_BITS 32
        STRICT_CHECK_REGS

        push    edx
        call    eax

        STRICT_SAVE_REGS
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pp16_32)
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPP16_32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPP16_V86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPP16_V86, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPP16_V86)
%else
        call    TMPL_NM(Bs3SwitchToPP16_V86)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PP16_V86
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp16_v86_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pp16_v86)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPP16_V86

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPP32(uint32_t uFlatWorkerAddr, uint8_t bMode);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPP32, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB + cbCurRetAddr]      ; Load function pointer.
        movzx   edx, byte [xBP + xCB + cbCurRetAddr + sCB] ; bMode

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPP32)
%else
        call    TMPL_NM(Bs3SwitchToPP32)
%endif
        BS3_SET_BITS 32
        STRICT_CHECK_REGS

        push    edx                     ; bMode
        call    eax

        STRICT_SAVE_REGS
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pp32)
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPP32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPP32_16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPP32_16, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPP32_16)
%else
        call    TMPL_NM(Bs3SwitchToPP32_16)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PP32_16
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp32_16_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pp32_16)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPP32_16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPPV86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPPV86, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPPV86)
%else
        call    TMPL_NM(Bs3SwitchToPPV86)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PPV86
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_ppv86_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_ppv86)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPPV86


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; PAE paged protection mode.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAE16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAE16, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPAE16)
%else
        call    TMPL_NM(Bs3SwitchToPAE16)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PAE16
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae16_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pae16)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPAE16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAE16_32(uint32_t uFlatWorkerAddr, uint8_t bMode);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAE16_32, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB + cbCurRetAddr]      ; Load function pointer.
        movzx   edx, byte [xBP + xCB + cbCurRetAddr + sCB] ; bMode

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPAE16_32)
%else
        call    TMPL_NM(Bs3SwitchToPAE16_32)
%endif
        BS3_SET_BITS 32
        STRICT_CHECK_REGS

        push    edx                     ; bMode
        call    eax

        STRICT_SAVE_REGS
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pae16_32)
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPAE16_32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAE16_V86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAE16_V86, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPAE16_V86)
%else
        call    TMPL_NM(Bs3SwitchToPAE16_V86)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PAE16_V86
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae16_v86_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pae16_v86)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPAE16_V86

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAE32(uint32_t uFlatWorkerAddr, uint8_t bMode);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAE32, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB + cbCurRetAddr]      ; Load function pointer.
        movzx   edx, byte [xBP + xCB + cbCurRetAddr + sCB] ; bMode

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPAE32)
%else
        call    TMPL_NM(Bs3SwitchToPAE32)
%endif
        BS3_SET_BITS 32
        STRICT_CHECK_REGS

        push    edx                     ; bMode
        call    eax

        STRICT_SAVE_REGS
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pae32)
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPAE32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAE32_16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAE32_16, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPAE32_16)
%else
        call    TMPL_NM(Bs3SwitchToPAE32_16)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PAE32_16
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae32_16_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_pae32_16)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPAE32_16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAEV86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAEV86, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToPAEV86)
%else
        call    TMPL_NM(Bs3SwitchToPAEV86)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_PAEV86
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_paev86_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_paev86)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPAEV86



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Long mode
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInLM16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInLM16, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
MY_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
BS3_GLOBAL_LOCAL_LABEL .doit
        mov     ax, [xBP + xCB + cbCurRetAddr]      ; Load far function pointer.
        mov     dx, [xBP + xCB + cbCurRetAddr + 2]

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToLM16)
%else
        call    TMPL_NM(Bs3SwitchToLM16)
%endif
        BS3_SET_BITS 16
        STRICT_CHECK_REGS

        push    BS3_MODE_LM16
        push    cs
        push    .return
        push    dx
        push    ax
        retf
.return:

        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_lm16_far)
%else
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_lm16)
%endif
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
MY_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInLM16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInLM32(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInLM32, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB + cbCurRetAddr]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToLM32)
%else
        call    TMPL_NM(Bs3SwitchToLM32)
%endif
        BS3_SET_BITS 32
        STRICT_CHECK_REGS

        and     esp, ~03h
        push    BS3_MODE_LM32
        call    eax

        STRICT_SAVE_REGS
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_lm32)
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInLM32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInLM64(uint32_t uFlatWorkerAddr, uint8_t bMode);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInLM64, BS3_PBC_NEAR
        BS3_LEA_MOV_WRT_RIP(xAX, MY_TEXT16_WRT(.doit))
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB + cbCurRetAddr]     ; Load function pointer.
        movzx   edx, byte [xBP + xCB + cbCurRetAddr + sCB] ; bMode

        ; Mode switch, make the call, switch back.
        STRICT_SAVE_REGS
%if TMPL_MODE == BS3_MODE_RM
        call far TMPL_FAR_NM(Bs3SwitchToLM64)
%else
        call    TMPL_NM(Bs3SwitchToLM64)
%endif
        BS3_SET_BITS 64
        STRICT_CHECK_REGS

        and     rsp, ~0fh
        sub     rsp, 18h
        push    rdx                     ; bMode
        BS3_CALL rax, 1

        STRICT_SAVE_REGS
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_lm64)
        BS3_SET_BITS TMPL_BITS
        STRICT_CHECK_REGS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInLM64

