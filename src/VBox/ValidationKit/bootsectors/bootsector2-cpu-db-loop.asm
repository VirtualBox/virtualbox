; $Id: bootsector2-cpu-db-loop.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Bootsector test for debug exception loop.
;
; Recommended (but not necessary):
;   VBoxManage setextradata bs-cpu-db-loop VBoxInternal/Devices/VMMDev/0/Config/TestingEnabled  1
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


;*******************************************************************************
;*      Header Files                                                           *
;*******************************************************************************
%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"
%include "VBox/VMMDevTesting.mac"


;
; Include and execute the init code.
;
        %define BS2_INIT_PE32
        %define BS2_WITH_TRAPS
        %define BS2_WITH_XCPT_DB_CLEARING_TF
        %define BS2_INC_PE16
        %define BS2_INC_PE32
        %define BS2_INC_RM ; for SetCpuModeGlobals_rm
        %include "bootsector2-common-init-code.mac"


;
; The main() function.
;
BEGINPROC main
        BITS 32
        ;
        ; Test prologue.
        ;
        mov     ax, .s_szTstName
        call    TestInit_p32
        call    Bs2EnableA20_p32
        cli                             ; raw-mode hack
        sub     esp, 20h

        call    Bs2Thunk_p32_p16
        BITS 16

        ;
        ; We require a stack that can wrap around here.  The default stack
        ; doesn't allow us to do this, so we'll configure a custom one
        ; where the page tables usually are.
        ;
        mov     eax, [bs2Gdt + BS2_SEL_SS16]
        mov     ebx, [bs2Gdt + BS2_SEL_SS16 + 4]

        and     eax, 0xffff
        or      eax, (BS2_PXX_BASE & 0xffff) << 16
        and     ebx, 0x00ffff00
        or      ebx, BS2_PXX_BASE & 0xff000000
        or      ebx, (BS2_PXX_BASE & 0x00ff0000) >> 16
        mov     [bs2GdtSpare0], eax
        mov     [bs2GdtSpare0 + 4], ebx


        ;
        ; Switch the stack.
        ;
        mov     ax, ss
        mov     es, ax                  ; saved ss
        mov     edi, esp                ; saved esp

        mov     ax, BS2_SEL_SPARE0
        mov     ss, ax
        mov     esp, 0xfff0


        ;
        ; Arm the breakpoint.
        ;
        and     dword [esp + 2], 0
        sidt    [esp]
        mov     eax, [esp + 2]
        add     eax, 8
        mov     dr0, eax
        mov     eax, X86_DR7_RA1_MASK | X86_DR7_GE \
                   | X86_DR7_L0 | X86_DR7_G0 | X86_DR7_RW(0, X86_DR7_RW_RW) | X86_DR7_LEN(0, X86_DR7_LEN_DWORD)
        mov     dr7, eax

        ;
        ; Trigger a single step exception.
        ;
        pushf
        or      word [xSP], X86_EFL_TF
        popf
        xchg    eax, ebx
        xchg    edx, ecx                ; should get a #DB here.
        xchg    eax, ebx
        xchg    edx, ecx

        ;
        ; If we get thus far, we've failed.
        ;
        mov     ax, es                  ; restore ss
        mov     ss, ax
        mov     esp, edi                ; restore esp

        call    Bs2Thunk_p16_p32
        BITS 32

        mov     eax, .s_szFailed
        call    TestFailed_p32

        ;
        ; We're done.
        ;
        call    TestTerm_p32
        add     esp, 20h
        ret

.s_szTstName:
        db      'tstCpuDbLoop', 0
.s_szFailed:
        db      'no #DB loop detected',0
ENDPROC   main


;
; End sections and image.
;
%include "bootsector2-common-end.mac"

