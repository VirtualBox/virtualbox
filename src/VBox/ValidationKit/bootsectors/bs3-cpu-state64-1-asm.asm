; $Id: bs3-cpu-state64-1-asm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - bs3-cpu-state64-1
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
%include "bs3kit.mac"


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BS3_BEGIN_DATA16
BS3_GLOBAL_DATA g_bs3CpuState64CtxCaller, BS3REGCTX_size
        resb    BS3REGCTX_size
BS3_GLOBAL_DATA g_bs3CpuState64CtxToLoad, BS3REGCTX_size
        resb    BS3REGCTX_size
BS3_GLOBAL_DATA g_bs3CpuState64CtxSaved, BS3REGCTX_size
        resb    BS3REGCTX_size

BS3_GLOBAL_DATA g_bs3CpuState64RCX, 8
        dq      1


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
BS3_BEGIN_TEXT64
EXTERN Bs3RegCtxRestore_c64
EXTERN Bs3RegCtxSave_c64


BS3_BEGIN_TEXT64
        BS3_SET_BITS 64

;;
;; Test worker that switches between 64-bit and 16-bit real mode,
;; only trashing RAX, BX, DS, RSP (preseved) and RIP.
;;
;; Caller puts the state to load in g_bs3CpuState64CtxToLoad, this function alters
;; the BX and RIP values before loading it.  It then switches to 16-bit real mode,
;; executes the worker given as input, re-enters long mode and saves the state to
;; g_bs3CpuState64CtxSaved.
;;
;; @param   rcx     Address of worker (16-bit) to invoke while in real-mode.
;;
BS3_PROC_BEGIN NAME(bs3CpuState64Worker)
        push    rbp
        mov     rbp, rsp
        sub     rsp, 40h
        mov     [rbp + 16], rcx

        ;
        ; Save the current register state so we can return with the exact state we entered.
        ;
        lea     rcx, [BS3_WRT_RIP(BS3_DATA_NM(g_bs3CpuState64CtxCaller)) wrt FLAT]
        mov     [rsp], rcx
        call    NAME(Bs3RegCtxSave_c64)

        ;
        ; Load the context.  We modify the state to be loaded so that it fits
        ; into the code flow here..
        ;
        lea     rcx, [BS3_WRT_RIP(BS3_DATA_NM(g_bs3CpuState64CtxToLoad)) wrt FLAT]
        mov     [rcx + BS3REGCTX.rsp], rsp
        ;lea     rdx, [BS3_WRT_RIP(.ctx_loaded) wrt FLAT] - absolute address cannot be relative. wtf?
        mov     edx, .ctx_loaded wrt FLAT
        mov     [rcx + BS3REGCTX.rip], rdx
        mov     edx, [rbp + 16]         ; Worker address. Putting it in the BX register relative to 16-bit CS.
        sub     edx, BS3_ADDR_BS3TEXT16
        mov     [rcx + BS3REGCTX.rbx], dx
        mov     edx, 0                  ; fFlags
        mov     [rsp], rcx
        mov     [rsp + 8], rdx
        call    NAME(Bs3RegCtxRestore_c64)
.ctx_loaded:

        ;
        ; Disable long mode.
        ;

        ; Construct a far return for switching to 16-bit code.
        push    BS3_SEL_R0_CS16
        push    .sixteen_bit_segment wrt CGROUP16
        xRETF
BS3_BEGIN_TEXT16
        BS3_SET_BITS 16
BS3_GLOBAL_LOCAL_LABEL .sixteen_bit_segment
        ; Make the DS usable from real mode.
        mov     ax, BS3_SEL_R0_DS16
        mov     ds, ax

        ; Exit to real mode.
        mov     eax, cr0
        and     eax, X86_CR0_NO_PE_NO_PG
        mov     cr0, eax
        jmp     CGROUP16:.reload_cs16
BS3_GLOBAL_LOCAL_LABEL .reload_cs16

        ;
        ; Jump to the 16-bit worker function that will make state modifications.
        ;
        jmp     bx
BS3_GLOBAL_LOCAL_LABEL .resume16

        ;
        ; Re-enter long mode.
        ;
        mov     eax, cr0
        or      eax, X86_CR0_PE | X86_CR0_PG
        mov     cr0, eax
        jmp     CGROUP16:.reload_cs_long_mode
BS3_GLOBAL_LOCAL_LABEL .reload_cs_long_mode
        ; Construct a far return for switching to 64-bit code.
        push    dword BS3_SEL_R0_CS64
        push    dword .sixtyfour_bit_segment wrt FLAT
        o32 retf
BS3_BEGIN_TEXT64
BS3_GLOBAL_LOCAL_LABEL .sixtyfour_bit_segment
        BS3_SET_BITS 64

        ;
        ; We're back in long mode, save the context.
        ;
        mov     [BS3_WRT_RIP(BS3_DATA_NM(g_bs3CpuState64RCX)) wrt FLAT], rcx
        lea     rcx, [BS3_WRT_RIP(BS3_DATA_NM(g_bs3CpuState64CtxSaved)) wrt FLAT]
        mov     [rsp], rcx
        call    NAME(Bs3RegCtxSave_c64)
        lea     rcx, [BS3_WRT_RIP(BS3_DATA_NM(g_bs3CpuState64CtxSaved)) wrt FLAT]
        mov     rax, [BS3_WRT_RIP(BS3_DATA_NM(g_bs3CpuState64RCX)) wrt FLAT]
        mov     [rcx + BS3REGCTX.rcx], rax

        ;
        ; Load the caller's context.
        ;
        lea     rcx, [BS3_WRT_RIP(BS3_DATA_NM(g_bs3CpuState64CtxCaller)) wrt FLAT]
        ;lea     rdx, [BS3_WRT_RIP(.return_sequence) wrt FLAT] - absolute address cannot be relative. wtf?
        mov     edx, .return_sequence wrt FLAT
        mov     [rcx + BS3REGCTX.rip], rdx
        mov     edx, 0
        mov     [rsp], rcx
        mov     [rsp + 8], rdx
        call    NAME(Bs3RegCtxRestore_c64)
.return_sequence:

        add     rsp, 40h
        pop     rbp
        ret
BS3_PROC_END   NAME(bs3CpuState64Worker)


BS3_BEGIN_TEXT16
;
; Real-mod modification workers for bs3CpuState64Worker.
;

BS3_PROC_BEGIN NAME(bs3CpuState64Worker_Nop)
        nop
        jmp     NAME(bs3CpuState64Worker.resume16)
BS3_PROC_END   NAME(bs3CpuState64Worker_Nop)


BS3_PROC_BEGIN NAME(bs3CpuState64Worker_ModAll32BitGrps)
        mov     eax, 0xc0ffee0d         ; C code hardcodes these values too.
        mov     ecx, 0xc0ffee1d
        mov     edx, 0xc0ffee2d
        mov     ebx, 0xc0ffee3d
        ; leave esp alone for now.
        mov     ebp, 0xc0ffee5d
        mov     esi, 0xc0ffee6d
        mov     edi, 0xc0ffee7d
        jmp     NAME(bs3CpuState64Worker.resume16)
BS3_PROC_END   NAME(bs3CpuState64Worker_ModAll32BitGrps)


BS3_PROC_BEGIN NAME(bs3CpuState64Worker_ModAll16BitGrps)
        mov     ax, 0xfad0         ; C code hardcodes these values too.
        mov     cx, 0xfad1
        mov     dx, 0xfad2
        mov     bx, 0xfad3
        ; leave esp alone for now.
        mov     bp, 0xfad5
        mov     si, 0xfad6
        mov     di, 0xfad7
        jmp     NAME(bs3CpuState64Worker.resume16)
BS3_PROC_END   NAME(bs3CpuState64Worker_ModAll16BitGrps)


BS3_PROC_BEGIN NAME(bs3CpuState64Worker_ModAll8BitGrps)
        mov     al, 0x10         ; C code hardcodes these values too.
        mov     ah, 0x11
        mov     cl, 0x20
        mov     ch, 0x21
        mov     dl, 0x30
        mov     dh, 0x31
        mov     bl, 0x40
        mov     bh, 0x41
        jmp     NAME(bs3CpuState64Worker.resume16)
BS3_PROC_END   NAME(bs3CpuState64Worker_ModAll8BitGrps)

BS3_PROC_BEGIN NAME(bs3CpuState64Worker_ModCr2)
        mov     eax, 0xf00dface ; C code hardcodes this value too.
        mov     cr2, eax
        jmp     NAME(bs3CpuState64Worker.resume16)
BS3_PROC_END   NAME(bs3CpuState64Worker_ModCr2)

;; @todo drX registers.

