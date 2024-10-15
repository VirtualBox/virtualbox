; $Id: bs3-mode-TrapSystemCallHandler.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - System call trap handler.
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
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
BS3_EXTERN_DATA16 g_bBs3CurrentMode
%if TMPL_BITS != 64
BS3_EXTERN_DATA16 g_uBs3CpuDetected
%endif
%if TMPL_BITS == 16
BS3_EXTERN_DATA16 g_uBs3TrapEipHint
%endif
TMPL_BEGIN_TEXT

BS3_EXTERN_CMN Bs3SelProtFar32ToFlat32
BS3_EXTERN_CMN Bs3RegCtxConvertToRingX
BS3_EXTERN_CMN Bs3RegCtxRestore
BS3_EXTERN_CMN Bs3Panic

BS3_BEGIN_TEXT16
extern Bs3PrintStrN_c16_CX_Bytes_At_DS_SI
TMPL_BEGIN_TEXT


;;
; System call handler.
;
; This is an assembly trap handler that is called in response to a system call
; request from 'user' code.  The only fixed parameter is [ER]AX which contains
; the system call number.  Other registers are assigned on a per system call
; basis, ditto for which registers are preserved and which are used to return
; stuff.  Generally, though, we preserve all registers not used as return
; values or otherwise implicitly transformed by the call.
;
; Note! The 16-bit versions of this code must be careful with using extended
;       registers as we wish this code to work on real 80286 (maybe even 8086)
;       CPUs too!
;
BS3_PROC_BEGIN_MODE Bs3TrapSystemCallHandler, BS3_PBC_NEAR ; Near because we'll probably only ever need this from CGROUP16.
        ;
        ; This prologue is kind of complicated because of 80286 and older CPUs
        ; as well as different requirements for 64-bit and the other modes.
        ;
%define VAR_CALLER_BP      [xBP]
%if TMPL_BITS != 64
 %define VAR_CALLER_DS     [xBP         - xCB]
%endif
%define VAR_CALLER_BX      [xBP - sCB*1 - xCB] ; Note! the upper word is not clean on pre-386 (16-bit mode).
%define VAR_CALLER_AX      [xBP - sCB*2 - xCB]
%define VAR_CALLER_CX      [xBP - sCB*3 - xCB]
%define VAR_CALLER_DX      [xBP - sCB*4 - xCB]
%define VAR_CALLER_SI      [xBP - sCB*5 - xCB]
%define VAR_CALLER_SI_HI   [xBP - sCB*5 - xCB + 2]
%define VAR_CALLER_DI      [xBP - sCB*6 - xCB]
%define VAR_CALLER_DI_HI   [xBP - sCB*6 - xCB + 2]
%if TMPL_BITS == 16
 %define VAR_CALLER_EBP    [xBP - sCB*7 - xCB]
 %define VAR_CALLER_ESP    [xBP - sCB*8 - xCB]
 %define VAR_CALLER_EFLAGS [xBP - sCB*9 - xCB]
 %define VAR_CALLER_MODE   [xBP - sCB*9 - xCB*2]
 %define BP_TOP_STACK_EXPR xBP - sCB*9 - xCB*2
%else
 %define VAR_CALLER_MODE   [xBP - sCB*6 - xCB*2]
 %define BP_TOP_STACK_EXPR xBP - sCB*6 - xCB*2
%endif
        push    xBP
        mov     xBP, xSP
%if TMPL_BITS == 64
        push    0
        mov     [rsp+2], es
        mov     [rsp], ds
%else
        push    ds
 %ifdef TMPL_CMN_R86
        push    BS3_SEL_DATA16
 %else
        push    RT_CONCAT(BS3_SEL_R0_DS,TMPL_BITS)
 %endif
        pop     ds                      ; DS = BS3KIT_GRPNM_DATA16 or FLAT and we can safely access data
 %if TMPL_BITS == 16 && (TMPL_MODE == BS3_MODE_RM || TMPL_MODE == BS3_MODE_PE16)
        cmp     byte [BS3_DATA16_WRT(g_uBs3CpuDetected)], BS3CPU_80286
        jbe     .prologue_pre_80386
 %endif
%endif
        push    sBX
        push    sAX
        push    sCX
        push    sDX
        push    sSI
        push    sDI
%if TMPL_BITS == 16
        push    ebp
        push    esp
        pushfd
 %if TMPL_MODE == BS3_MODE_RM || TMPL_MODE == BS3_MODE_PE16
        jmp     .prologue_end

.prologue_pre_80386:
        push    bx                      ; dummy
        push    bx
        xor     bx, bx
        push    bx                      ; dummy
        push    ax
        push    bx                      ; dummy
        push    cx
        push    bx                      ; dummy
        push    dx
        push    bx                      ; dummy
        push    si
        push    bx                      ; dummy
        push    di
        sub     sp, 0ch                 ; dummy
 %endif
%endif
.prologue_end:

        ;
        ; VAR_CALLER_MODE: Save the current mode (important for v8086 with 16-bit kernel).
        ;
        xor     xBX, xBX
        mov     bl, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        push    xBX

        ;
        ; Dispatch the system call.
        ;
        cmp     ax, BS3_SYSCALL_LAST
        ja      .invalid_syscall
%if TMPL_BITS == 16
        mov     bx, ax
        shl     bx, 1
        jmp     word [cs:.aoffSyscallHandlers + bx]
%else
        movzx   ebx, ax
        mov     ebx, [.aoffSyscallHandlers + ebx * 4]
        jmp     xBX
%endif
.aoffSyscallHandlers:
%ifdef TMPL_16BIT
        dw      .invalid_syscall wrt CGROUP16
        dw      .print_chr       wrt CGROUP16
        dw      .print_str       wrt CGROUP16
        dw      .to_ringX        wrt CGROUP16
        dw      .to_ringX        wrt CGROUP16
        dw      .to_ringX        wrt CGROUP16
        dw      .to_ringX        wrt CGROUP16
        dw      .restore_ctx     wrt CGROUP16
%else
        dd      .invalid_syscall wrt FLAT
        dd      .print_chr       wrt FLAT
        dd      .print_str       wrt FLAT
        dd      .to_ringX        wrt FLAT
        dd      .to_ringX        wrt FLAT
        dd      .to_ringX        wrt FLAT
        dd      .to_ringX        wrt FLAT
        dd      .restore_ctx     wrt FLAT
%endif

        ;
        ; Invalid system call.
        ;
.invalid_syscall:
        int3
        jmp     .return

        ;
        ; Print char in the CL register.
        ;
        ; We use the vga bios teletype interrupt to do the writing, so we must
        ; be in some kind of real mode for this to work.  16-bit code segment
        ; requried for the mode switching code.
        ;
BS3_BEGIN_TEXT16
        BS3_SET_BITS TMPL_BITS
.print_chr:
%if TMPL_BITS != 64
        push    es
        mov     di, ss                  ; Must save and restore SS for supporting 16/32 and 32/16 caller/kernel ring-0 combinations.
%endif
%ifndef TMPL_CMN_R86
        ; Switch to real mode (20h param scratch area not required).
        extern  TMPL_NM(Bs3SwitchToRM)
        call    TMPL_NM(Bs3SwitchToRM)
        BS3_SET_BITS 16
%endif

        ; Print the character, turning '\n' into '\r\n'.
        cmp     cl, 0ah                 ; \n
        je      .print_chr_newline
        mov     ah, 0eh
        mov     al, cl
        mov     bx, 0ff00h
        int     10h
        jmp     .print_chr_done

.print_chr_newline:
        mov     ax, 0e0dh               ; cmd + \r
        mov     bx, 0ff00h
        int     10h
        mov     ax, 0e0ah               ; cmd + \n
        mov     bx, 0ff00h
        int     10h

.print_chr_done:
%ifndef TMPL_CMN_R86
        ; Switch back (20h param scratch area not required).
        extern  RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_rm)
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_rm)
        BS3_SET_BITS TMPL_BITS
%endif
%if TMPL_BITS != 64
        mov     ss, di
        pop     es
%endif
        jmp     .return
TMPL_BEGIN_TEXT


        ;
        ; Prints DX chars from the string pointed to by CX:xSI to the screen.
        ;
        ; We use the vga bios teletype interrupt to do the writing, so we must
        ; be in some kind of real mode for this to work.  The string must be
        ; accessible from real mode too.
        ;
.print_str:
%if TMPL_BITS != 64
        push    es
        push    ss                      ; Must save and restore SS for supporting 16/32 and 32/16 caller/kernel ring-0 combinations.
%endif
        ; Convert the incoming pointer to real mode (assuming caller checked
        ; that real mode can access it).
        call    .convert_ptr_arg_to_real_mode_ax_si
        mov     cx, VAR_CALLER_DX

        ; Switch to real mode (no 20h scratch required)
%ifndef TMPL_CMN_R86
 %if TMPL_BITS != 16
        jmp     .print_str_to_16bit
BS3_BEGIN_TEXT16
.print_str_to_16bit:
        BS3_SET_BITS TMPL_BITS
 %endif
        extern  TMPL_NM(Bs3SwitchToRM)
        call    TMPL_NM(Bs3SwitchToRM)
        BS3_SET_BITS 16
%endif
        ; Call code in Bs3PrintStrN to do the work.
        mov     ds, ax
        call    Bs3PrintStrN_c16_CX_Bytes_At_DS_SI

        ; Switch back (20h param scratch area not required).
%ifndef TMPL_CMN_R86
        extern  RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_rm)
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_rm)
 %if TMPL_BITS != 16
        BS3_SET_BITS TMPL_BITS
        jmp     .print_str_end
TMPL_BEGIN_TEXT
 %endif
.print_str_end:
%endif
%if TMPL_BITS != 64
        pop     ss
        pop     es
%endif
        jmp     .return


        ;
        ; Switch the caller to ring-0, ring-1, ring-2 or ring-3.
        ;
        ; This implement this by saving the entire register context, calling
        ; a transformation function (C) and restoring the modified register
        ; context using a generic worker.
        ;
.to_ringX:
        sub     xSP, BS3REGCTX_size
        mov     xBX, xSP                ; xBP = BS3REGCTX pointer.
        call    .save_context

%if TMPL_BITS == 32
        ; Convert xBP to flat pointer in 32-bit
        push    ss
        push    xBX
        call    Bs3SelProtFar32ToFlat32
        add     sSP, 8
        mov     xBX, xAX
%endif
        push    xBX                     ; Save pointer for the final restore call.

        ; Convert the register context from whatever it is to ring-0.
BONLY64 sub     rsp, 10h
        mov     ax, VAR_CALLER_AX
        sub     ax, BS3_SYSCALL_TO_RING0
        push    xAX
BONLY16 push    ss
        push    xBX
        BS3_CALL Bs3RegCtxConvertToRingX, 2
        add     xSP, sCB + xCB BS3_ONLY_64BIT(+ 10h)

        ; Restore the register context (does not return).
        pop     xBX                     ; restore saved pointer.
BONLY64 sub     rsp, 18h
BONLY16 push    ss
        push    xBX
        BS3_CALL Bs3RegCtxRestore, 1
        jmp     Bs3Panic


        ;
        ; Restore context pointed to by cx:xSI.
        ;
.restore_ctx:
        call    .convert_ptr_arg_to_cx_xSI
BONLY64 sub     rsp, 10h
        mov     xDX, VAR_CALLER_DX
        push    xDX
BONLY16 push    cx
        push    xSI
        BS3_CALL Bs3RegCtxRestore, 2
        jmp     Bs3Panic

        ;
        ; Return.
        ;
.return:
        pop     xBX                     ; saved mode
        mov     [BS3_DATA16_WRT(g_bBs3CurrentMode)], bl
%if TMPL_BITS == 16
        and     bl, BS3_MODE_CODE_MASK
        cmp     bl, BS3_MODE_CODE_V86
        je      .return_to_v8086_from_16bit_krnl
        cmp     bl, BS3_MODE_CODE_32
        je      .return_to_32bit_from_16bit_krnl
 %if TMPL_MODE == BS3_MODE_RM || TMPL_MODE == BS3_MODE_PE16
        cmp     byte [BS3_DATA16_WRT(g_uBs3CpuDetected)], BS3CPU_80286
        jbe     .return_pre_80386
 %endif

        popfd
        pop     esp
        pop     ebp
%endif
        pop     sDI
        pop     sSI
        pop     sDX
        pop     sCX
        pop     sAX
        pop     sBX
%if TMPL_BITS != 64
        pop     ds
        leave
        iret
%else
        mov     es, [rsp+2]
        mov     ds, [rsp]
        leave                           ; skips ds
        iretq
%endif

%if TMPL_BITS == 16
 %if TMPL_MODE == BS3_MODE_RM || TMPL_MODE == BS3_MODE_PE16
        ; Variant of the above for 80286 and older.
.return_pre_80386:
        add     sp, 0ch
        pop     di
        pop     bx                      ; dummy
        pop     si
        pop     bx                      ; dummy
        pop     dx
        pop     bx                      ; dummy
        pop     cx
        pop     bx                      ; dummy
        pop     ax
        pop     bx                      ; dummy
        pop     bx                      ; pushed twice
        pop     bx
        pop     ds
        pop     bp
        iret
 %endif

.return_to_v8086_from_16bit_krnl:
        int3
        jmp     .return_to_v8086_from_16bit_krnl

        ;
        ; Returning to 32-bit code may require us to expand and seed the eip
        ; and esp addresses in the iret frame since these are truncated when
        ; using a 16-bit interrupt handler.
        ;
        ; Incoming stack:        New stack diff cpl:
        ;   bp + 0ah: [ss]
        ;   bp + 08h: [sp]         bx + 38h: [ss]       New stack same cpl:
        ;   bp + 06h: flags
        ;   bp + 04h: cs           bx + 34h: [esp]        bx + 30h: eflags
        ;   bp + 02h: ip
        ;   --------------         bx + 30h: eflags       bx + 2ch: cs
        ;   bp + 00h: bp
        ;   bp - 02h: ds           bx + 2ch: cs           bx + 28h: eip
        ;                                                 -------------
        ;   bp - 06h: ebx          bx + 28h: eip          bx + 26h: bp
        ;                         --------------          bx + 24h: ds
        ;   bp - 0ah: eax          bx + 26h: bp
        ;                          bx + 24h: ds           bx + 20h: ebx
        ;   bp - 0eh: ecx
        ;                          bx + 20h: ebx          bx + 1ch: eax
        ;   bp - 12h: edx
        ;                          bx + 1ch: eax          bx + 18h: ecx
        ;   bp - 16h: esi
        ;                          bx + 18h: ecx          bx + 14h: edx
        ;   bp - 1ah: edi
        ;                          bx + 14h: edx          bx + 10h: esi
        ;   bp - 1eh: esp
        ;                          bx + 10h: esi          bx + 0ch: edi
        ;   bp - 22h: ebp
        ;                          bx + 0ch: edi          bx + 08h: esp
        ;   bp - 26h: eflags
        ;                          bx + 08h: esp          bx + 04h: ebp
        ;
        ;                          bx + 04h: ebp          bx + 00h: eflags
        ;
        ;                          bx + 00h: eflags
        ;
        ;
        ; If we're returning to the same CPL, we're still using the stack of
        ; the 32-bit caller.  The high ESP word does not need restoring.
        ;
        ; If we're returning to a lower CPL, there on a 16-bit ring-0 stack,
        ; however, the high ESP word is still that of the caller.
        ;
.return_to_32bit_from_16bit_krnl:
        mov     ax, cs
        and     al, 3
        mov     ah, 3
        and     ah, [xBP + xCB*2]
        ; The iret frame doubles in size, so allocate more stack.
        cmp     al, ah
        je      .return_to_32bit_from_16bit_krnl_same_cpl_sub_sp
        sub     sp, 2*2
.return_to_32bit_from_16bit_krnl_same_cpl_sub_sp:
        sub     sp, 3*2
        mov     bx, sp
        ; Copy the saved registers.
        xor     di, di
.return_to_32bit_from_16bit_krnl_copy_loop:
        mov     ecx, [bp + di - 26h]
        mov     [ss:bx + di], ecx
        add     di, 4
        cmp     di, 28h
        jb      .return_to_32bit_from_16bit_krnl_copy_loop
        ; Convert the 16-bit iret frame to a 32-bit iret frame.
        mov     ecx, [BS3_DATA16_WRT(g_uBs3TrapEipHint)]
        mov     cx,  [bp + 02h]         ; ip
        mov     [ss:bx + 28h], ecx
        mov     ecx, 0f00d0000h
        mov     cx,  [bp + 04h]         ; cs
        mov     [ss:bx + 2ch], ecx
        mov     ecx, [ss:bx]            ; caller eflags
        mov     cx,  [bp + 06h]         ; flags
        mov     [ss:bx + 30h], ecx
        cmp     al, ah
        jz      .return_to_32bit_from_16bit_krnl_do_return
        mov     ecx, [ss:bx + 08h]      ; caller esp
        mov     cx,  [bp + 08h]         ; sp
        mov     [ss:bx + 34h], ecx
        mov     ecx, 0f00d0000h
        mov     cx,  [bp + 0ah]         ; ss
        mov     [ss:bx + 38h], ecx
.return_to_32bit_from_16bit_krnl_do_return:
        popfd
        pop     ecx                     ; esp - only the high bits!
        mov     cx, sp
        mov     esp, ecx
        pop     ebp
        lea     bp, [bx + 26h]
        pop     edi
        pop     esi
        pop     edx
        pop     ecx
        pop     eax
        pop     ebx
        pop     ds
        leave
        iretd

%endif ; 16-bit


        ;
        ; Internal function. ss:xBX = Pointer to register frame (BS3REGCTX).
        ; @uses xAX
        ;
.save_context:
%if TMPL_BITS == 16
        cmp     byte [BS3_DATA16_WRT(g_uBs3CpuDetected)], BS3CPU_80386
        jae     .save_context_full

        ;
        ; 80286 or earlier.
        ;

        ; Clear the state area first.
        push    di
        xor     di, di
.save_context_16_clear_loop:
        mov     word [ss:bx + di], 0
        mov     word [ss:bx + di + 2], 0
        mov     word [ss:bx + di + 4], 0
        mov     word [ss:bx + di + 6], 0
        add     di, 8
        cmp     di, BS3REGCTX_size
        jb      .save_context_16_clear_loop
        pop     di

        ; Do the 8086/80186/80286 state saving.
        mov     ax, VAR_CALLER_AX
        mov     [ss:bx + BS3REGCTX.rax], ax
        mov     cx, VAR_CALLER_CX
        mov     [ss:bx + BS3REGCTX.rcx], ax
        mov     ax, VAR_CALLER_DX
        mov     [ss:bx + BS3REGCTX.rdx], ax
        mov     ax, VAR_CALLER_BX
        mov     [ss:bx + BS3REGCTX.rbx], ax
        mov     [ss:bx + BS3REGCTX.rsi], si
        mov     [ss:bx + BS3REGCTX.rdi], di
        mov     ax, VAR_CALLER_BP
        mov     [ss:bx + BS3REGCTX.rbp], ax
        mov     ax, VAR_CALLER_DS
        mov     [ss:bx + BS3REGCTX.ds], ax
        mov     [ss:bx + BS3REGCTX.es], es
        mov     ax, [xBP + xCB]
        mov     [ss:bx + BS3REGCTX.rip], ax
        mov     ax, [xBP + xCB*2]
        mov     [ss:bx + BS3REGCTX.cs], ax
        and     al, X86_SEL_RPL
        mov     [ss:bx + BS3REGCTX.bCpl], al
        cmp     al, 0
        je      .save_context_16_same
        mov     ax, [xBP + xCB*4]
        mov     [ss:bx + BS3REGCTX.rsp], ax
        mov     ax, [xBP + xCB*5]
        mov     [ss:bx + BS3REGCTX.ss], ax
        jmp     .save_context_16_done_stack
.save_context_16_same:
        mov     ax, bp
        add     ax, xCB * (1 + 3)
        mov     [ss:bx + BS3REGCTX.rsp], ax
        mov     ax, ss
        mov     [ss:bx + BS3REGCTX.ss], ax
.save_context_16_done_stack:
        mov     ax, [xBP + xCB*3]
        mov     [ss:bx + BS3REGCTX.rflags], ax
        mov     al, VAR_CALLER_MODE
        mov     [ss:bx + BS3REGCTX.bMode], al
        cmp     byte [BS3_DATA16_WRT(g_uBs3CpuDetected)], BS3CPU_80286
        jne     .save_context_16_return
        smsw    [ss:bx + BS3REGCTX.cr0]
        str     [ss:bx + BS3REGCTX.tr]
        sldt    [ss:bx + BS3REGCTX.ldtr]
.save_context_16_return:
        or      byte [ss:bx + BS3REGCTX.fbFlags], BS3REG_CTX_F_NO_AMD64 | BS3REG_CTX_F_NO_CR4
        ret
%endif ; TMPL_BITS == 16

        ;
        ; 80386 or later.
        ;
.save_context_full:

        ; Clear the state area.
        push    xDI
        xor     xDI, xDI
        AssertCompileSizeAlignment(BS3REGCTX, 16)
.save_context_full_clear_loop:
%if TMPL_BITS != 64
        mov     dword [ss:xBX + xDI], 0
        mov     dword [ss:xBX + xDI + 4], 0
        add     xDI, 8
%else
        mov     qword [xBX + xDI], 0
        mov     qword [xBX + xDI + 8], 0
        add     xDI, 10h
%endif
        cmp     xDI, BS3REGCTX_size
        jb      .save_context_full_clear_loop
        pop     xDI

        ; Do the 386+ state saving.
%if TMPL_BITS == 16                     ; save the high word of registered pushed on the stack.
        mov     ecx, VAR_CALLER_AX
        mov     [ss:bx + BS3REGCTX.rax], ecx
        mov     ecx, VAR_CALLER_CX
        mov     [ss:bx + BS3REGCTX.rcx], ecx
        mov     ecx, VAR_CALLER_DX
        mov     [ss:bx + BS3REGCTX.rdx], ecx
        mov     ecx, VAR_CALLER_BX
        mov     [ss:bx + BS3REGCTX.rbx], ecx
        mov     ecx, VAR_CALLER_EBP
        mov     [ss:bx + BS3REGCTX.rbp], ecx
        mov     ecx, VAR_CALLER_ESP
        mov     [ss:bx + BS3REGCTX.rsp], ecx
        mov     ecx, VAR_CALLER_SI
        mov     [ss:bx + BS3REGCTX.rsi], ecx
        mov     ecx, VAR_CALLER_DI
        mov     [ss:bx + BS3REGCTX.rdi], ecx
        mov     ecx, VAR_CALLER_EFLAGS
        mov     [ss:bx + BS3REGCTX.rflags], ecx

        ; Seed high EIP word if 32-bit CS.
        lar     ecx, [bp + 4]
        jnz     .save_context_full_done_16bit_high_word
        test    ecx, X86LAR_F_D
        jz      .save_context_full_done_16bit_high_word
        mov     ecx, [BS3_DATA16_WRT(g_uBs3TrapEipHint)]
        mov     [ss:bx + BS3REGCTX.rip], ecx
.save_context_full_done_16bit_high_word:
%endif ; 16-bit
        mov     xAX, VAR_CALLER_AX
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rax], xAX
        mov     xCX, VAR_CALLER_CX
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rcx], xCX
        mov     xAX, VAR_CALLER_DX
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rdx], xAX
        mov     xAX, VAR_CALLER_BX
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rbx], xAX
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rsi], sSI
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rdi], sDI
        mov     xAX, VAR_CALLER_BP
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rbp], xAX
%if TMPL_BITS != 64
        mov     ax, VAR_CALLER_DS
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.ds], ax
%else
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.ds], ds
%endif
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.es], es
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.fs], fs
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.gs], gs
        mov     xAX, [xBP + xCB]
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rip], xAX
        mov     ax, [xBP + xCB*2]
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.cs], ax
%if TMPL_MODE != BS3_MODE_RM
        and     al, X86_SEL_RPL
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.bCpl], al
        cmp     al, 0
        je      .save_context_full_same
        mov     xAX, [xBP + xCB*4]
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rsp], xAX
        mov     ax, [xBP + xCB*5]
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.ss], ax
        jmp     .save_context_full_done_stack
%else
        mov     byte [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.bCpl], 0
%endif
.save_context_full_same:
        mov     xAX, xBP
        add     xAX, xCB * (1 + 3)
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rsp], xAX
        mov     ax, ss
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.ss], ax
.save_context_full_done_stack:
        mov     xAX, [xBP + xCB*3]
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rflags], xAX

        mov     al, VAR_CALLER_MODE
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.bMode], al
%if TMPL_BITS == 64
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.r8], r8
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.r9], r9
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.r10], r10
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.r11], r11
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.r12], r12
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.r13], r13
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.r14], r14
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.r15], r15
%endif
        str     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.tr]
        sldt    [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.ldtr]
        mov     sAX, cr0
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.cr0], sAX
        mov     sAX, cr2
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.cr2], sAX
        mov     sAX, cr3
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.cr3], sAX
%if TMPL_BITS != 64
        test    byte [BS3_DATA16_WRT(g_uBs3CpuDetected)], (BS3CPU_F_CPUID >> 8)
        jnz     .have_cr4
        or      byte [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.fbFlags], BS3REG_CTX_F_NO_CR4
        jmp     .done_cr4
.have_cr4:
%endif
        mov     sAX, cr4
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.cr4], sAX
%if TMPL_BITS != 64
.done_cr4:
        or      byte [ss:xBX + BS3REGCTX.fbFlags], BS3REG_CTX_F_NO_AMD64

        ; Deal with extended v8086 frame.
 %if TMPL_BITS == 32
        test    dword [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rflags], X86_EFL_VM
        jz      .save_context_full_return
 %else
        test    byte VAR_CALLER_MODE, BS3_MODE_CODE_V86
        jz      .save_context_full_return
        mov     dword [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rflags], X86_EFL_VM
 %endif
        mov     xAX, [xBP + xCB*4]
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.rsp], xAX
        mov     ax, [xBP + xCB*5]
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.ss], ax
        mov     ax, [xBP + xCB*6]
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.es], ax
        mov     ax, [xBP + xCB*7]
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.ds], ax
        mov     ax, [xBP + xCB*8]
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.fs], ax
        mov     ax, [xBP + xCB*9]
        mov     [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.gs], ax
        mov     byte [BS3_NOT_64BIT(ss:) xBX + BS3REGCTX.bCpl], 3
        jmp     .save_context_full_return

%endif  ; !64-bit

.save_context_full_return:
        ret

%if TMPL_BITS == 16
        CPU 286
%endif

        ;
        ; Internal function for converting a syscall pointer parameter (cx:xSI)
        ; to a pointer we can use here in this context.
        ;
        ; Returns the result in cx:xSI.
        ; @uses xAX, xCX, xDX
        ;
.convert_ptr_arg_to_cx_xSI:
        call    .convert_ptr_arg_to_flat
%if TMPL_BITS == 16
        ; Convert to tiled address.
        mov     si, ax                  ; offset.
        shl     dx, X86_SEL_SHIFT
        add     dx, BS3_SEL_TILED
        mov     cx, dx
%else
        ; Just supply a flat selector.
        mov     xSI, xAX
        mov     cx, ds
%endif
        ret

        ;
        ; Internal function for converting a syscall pointer parameter (caller CX:xSI)
        ; to a real mode pointer.
        ;
        ; Returns the result in AX:SI.
        ; @uses xAX, xCX, xDX
        ;
.convert_ptr_arg_to_real_mode_ax_si:
        call    .convert_ptr_arg_to_flat
        mov     si, ax
%if TMPL_BITS == 16
        mov     ax, dx
%else
        shr     eax, 16
%endif
        shl     ax, 12
        ret

        ;
        ; Internal function for the above that wraps the Bs3SelProtFar32ToFlat32 call.
        ;
        ; @returns  eax (32-bit, 64-bit), dx+ax (16-bit).
        ; @uses     eax, ecx, edx
        ;
.convert_ptr_arg_to_flat:
%if TMPL_BITS == 16
        ; Convert to (32-bit) flat address first.
        test    byte VAR_CALLER_MODE, BS3_MODE_CODE_V86
        jz      .convert_ptr_arg_to_flat_prot_16

        mov     ax, VAR_CALLER_CX
        mov     dx, ax
        shl     ax, 4
        shr     dx, 12
        add     ax, VAR_CALLER_SI
        adc     dx, 0
        ret

.convert_ptr_arg_to_flat_prot_16:
        push    es
        push    bx
        push    word VAR_CALLER_CX      ; selector
        xor     ax, ax
        test    byte VAR_CALLER_MODE, BS3_MODE_CODE_16
        jnz     .caller_is_16_bit
        mov     ax, VAR_CALLER_SI_HI
.caller_is_16_bit:
        push    ax                      ; offset high
        push    word VAR_CALLER_SI      ; offset low
        call    Bs3SelProtFar32ToFlat32
        add     sp, 2*3
        pop     bx
        pop     es
        ret

%else ; 32 or 64 bit
        test    byte VAR_CALLER_MODE, BS3_MODE_CODE_V86
        jz      .convert_ptr_arg_to_cx_xSI_prot

        ; Convert real mode address to flat address and return it.
        movzx   eax, word VAR_CALLER_CX
        shl     eax, 4
        movzx   edx, word VAR_CALLER_SI
        add     eax, edx
        ret

        ; Convert to (32-bit) flat address.
.convert_ptr_arg_to_cx_xSI_prot:
 %if TMPL_BITS == 64
        push    r11
        push    r10
        push    r9
        push    r8
        sub     rsp, 10h
 %endif
        movzx   ecx, word VAR_CALLER_CX
        push    xCX
        mov     eax, VAR_CALLER_SI
        test    byte VAR_CALLER_MODE, BS3_MODE_CODE_16
        jz      .no_masking_offset
        and     eax, 0ffffh
.no_masking_offset:
        push    xAX
        BS3_CALL Bs3SelProtFar32ToFlat32,2
        add     xSP, xCB*2 BS3_ONLY_64BIT(+ 10h)
 %if TMPL_BITS == 64
        pop     r8
        pop     r9
        pop     r10
        pop     r11
 %endif
%endif
        ret

BS3_PROC_END_MODE   Bs3TrapSystemCallHandler

