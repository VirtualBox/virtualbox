; $Id: bootsector2-cpu-ac-loop.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Bootsector test for debug exceptions.
;
; Recommended (but not necessary):
;   VBoxManage setextradata bs-cpu-xcpt-2 VBoxInternal/Devices/VMMDev/0/Config/TestingEnabled  1
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
        %define BS2_WITH_TRAPRECS
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

        ;
        ; Execute the tests
        ;
        sub     esp, 20h

        ; Get the address of the #AC IDT entry.
        sidt    [esp]
        mov     eax, [esp + 2]
        add     eax, 8 * X86_XCPT_AC

        ; Make it execute in ring-3.
        mov     word [eax + 2], BS2_SEL_R3_CS32 ; u16Sel
        or      byte [eax + 5], 3 << 5          ; u2Dpl = 3

        ; Enable AC.
        mov     eax, cr0
        or      eax, X86_CR0_AM
        mov     cr0, eax

        ; Switch to ring-3
        call    Bs2ToRing3_p32

        ; Enable AC.
        pushfd
        or      dword [esp], X86_EFL_AC
        popfd

        ;; Test it. - won't work as the handle touches CR2, which traps in ring-3.
        ;BS2_TRAP_INSTR X86_XCPT_AC, 0, mov dword [esp + 3], 0

        ; Misalign the stack and use it.
        or      esp, 3
        push    esp                     ; this will loop forever on real intel hardware.
        and     esp, ~3h

        add     esp, 20h

        ;
        ; We're done.
        ;
        call    TestTerm_p32
        ret

.s_szTstName:
        db      'tstCpuAcLoop', 0
ENDPROC   main


;
; End sections and image.
;
%include "bootsector2-common-end.mac"

