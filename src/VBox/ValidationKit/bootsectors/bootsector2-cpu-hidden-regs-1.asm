; $Id: bootsector2-cpu-hidden-regs-1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Bootsector that shows/tests the content of hidden CPU registers.
;
; Requires VMMDevTesting. Enable it via VBoxManage:
;   VBoxManage setextradata bs-cpu-hidden-regs-1 VBoxInternal/Devices/VMMDev/0/Config/TestingEnabled  1

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


%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"
%include "VBox/VMMDevTesting.mac"

;
; Include and execute the init code.
;
        %define BS2_INIT_RM
        %define BS2_INC_PE16
        %define BS2_INC_PE32
        %define BS2_INC_PP32
        %define BS2_INC_LM64
        %define BS2_WITH_TRAPS
        %define BS2_WITH_MANUAL_LTR
        %include "bootsector2-common-init-code.mac"


;
; The benchmark driver
;
BEGINPROC main
        ;
        ; Test prologue.
        ;
        mov     ax, .s_szTstName
        call    TestInit_r86
        call    Bs2EnableA20_r86
        call    Bs2PanicIfVMMDevTestingIsMissing_r86

        call    reportPostBiosValues
        call    rmTests
        call    doTests_rm_pe32
        call    doTests_rm_pp32
        call    doTests_rm_lm64

        ;
        ; We're done.
        ;
        call    TestTerm_r86
        call    Bs2Panic

.s_szTstName:
        db      'tstCpuHidRegs', 0
ENDPROC   main


;
; Reports the values of interesting hidden registers as we start the test, i.e.
; right after the BIOS completed.
;
BEGINPROC reportPostBiosValues
        push    ax
        push    bx
        mov     ax, .s_szTstInitial
        call    TestSub_r86

        mov     ax, .s_szzStart
        call    TestValueRegSZZ_rm

.done
        pop     bx
        pop     ax
        ret

.s_szzStart:
        db      'BIOS - ldtr:ldtr', 0;
        db      'BIOS - ldtr_base:ldtr_base', 0;
        db      'BIOS - ldtr_limit:ldtr_lim', 0;
        db      'BIOS - ldtr_attr:ldtr_attr', 0;
        db      'BIOS - tr:tr', 0;
        db      'BIOS - tr_base:tr_base', 0;
        db      'BIOS - tr_limit:tr_lim', 0;
        db      'BIOS - tr_attr:tr_attr', 0;
        db      'BIOS - cs:cs', 0;
        db      'BIOS - cs_base:cs_base', 0;
        db      'BIOS - cs_limit:cs_lim', 0;
        db      'BIOS - cs_attr:cs_attr', 0;
        db      'BIOS - ss:ss', 0;
        db      'BIOS - ss_base:ss_base', 0;
        db      'BIOS - ss_limit:ss_lim', 0;
        db      'BIOS - ss_attr:ss_attr', 0;
        db      'BIOS - ds:ds', 0;
        db      'BIOS - ds_base:ds_base', 0;
        db      'BIOS - ds_limit:ds_lim', 0;
        db      'BIOS - ds_attr:ds_attr', 0;
        db      0,0,0,0 ; terminator
.s_szTstInitial:
        db      'Post BIOS Values', 0
ENDPROC   reportPostBiosValues


;
; Reports the values of interesting hidden registers as we start the test, i.e.
; right after the BIOS completed.
;
BEGINPROC rmTests
        push    eax
        push    ebx
        pushfd
        cli

        mov     ax, .s_szTstRM
        call    TestSub_r86

        ; Check if CS changes when leaving protected mode.
        mov     ax, .s_szzRMPre
        call    TestValueRegSZZ_rm
        mov     byte [cs:.s_dwDummy], 1
        call    Bs2EnterMode_rm_pe32
BITS 32
        mov     eax, .s_szzProt32
        call    TestValueRegSZZ_pe32
        ; mov     word [cs:.s_dwDummy], 2 - this shall GP(CS).
        call    Bs2ExitMode_pe32
BITS 16
        mov     ax, .s_szzRMPost
        call    TestValueRegSZZ_rm
        mov     dword [cs:.s_dwDummy], 3

        ;
        ; What happens if we make CS32 execute-only and return to real-mode.
        ;
        mov     byte [cs:.s_dwDummy], 1
        call    Bs2EnterMode_rm_pe16
        jmp     BS2_SEL_CS16_EO:.loaded_cs16_eo
.loaded_cs16_eo:
        mov     eax, .s_szzProtEO
        call    TestValueRegSZZ_pe16
        ; mov     ax, word [cs:.s_dwDummy] - this shall GP(CS).
        ; mov     word [cs:.s_dwDummy], 2 - this shall GP(CS).

        ; Leave real-mode ourselves.
        mov     eax, cr0
        and     eax, ~X86_CR0_PE
        mov     cr0, eax

        ; All but cs gets reloaded.
        xor     ax, ax
        mov     ss, ax
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax

        ; Display CS and do a test.
        mov     ax, .s_szzRMEO
        call    TestValueRegSZZ_rm

        mov     ax, [cs:.s_dwDummy]      ; works on intel
        mov     dword [cs:.s_dwDummy], 3 ; ditto

        jmp     far 0000:.load_rm_cs
.load_rm_cs:
        ; Display CS to check that it remained unchanged.
        mov     ax, .s_szzRMEO2
        call    TestValueRegSZZ_rm

        ; Cleanup everything properly.
        call    Bs2EnterMode_rm_pe32
BITS 32
        call    Bs2ExitMode_pe32
BITS 16

        popfd
        pop     ebx
        pop     eax
        ret

.s_dwDummy:
        dd      0
.s_szzRMPre:
        db      'RM Pre  - cs:cs', 0;
        db      'RM Pre  - cs_base:cs_base', 0;
        db      'RM Pre  - cs_limit:cs_lim', 0;
        db      'RM Pre  - cs_attr:cs_attr', 0;
        db      0,0,0,0 ; terminator
.s_szzProt32:
        db      'Prot32  - cs:cs', 0;
        db      'Prot32  - cs_base:cs_base', 0;
        db      'Prot32  - cs_limit:cs_lim', 0;
        db      'Prot32  - cs_attr:cs_attr', 0;
        db      0,0,0,0 ; terminator
.s_szzRMPost:
        db      'RM Post - cs:cs', 0;
        db      'RM Post - cs_base:cs_base', 0;
        db      'RM Post - cs_limit:cs_lim', 0;
        db      'RM Post - cs_attr:cs_attr', 0;
        db      0,0,0,0 ; terminator
.s_szzProtEO:
        db      'Prot 16 EO,L-1,NA - cs:cs', 0;
        db      'Prot 16 EO,L-1,NA - cs_base:cs_base', 0;
        db      'Prot 16 EO,L-1,NA - cs_limit:cs_lim', 0;
        db      'Prot 16 EO,L-1,NA - cs_attr:cs_attr', 0;
        db      0,0,0,0 ; terminator
.s_szzRMEO:
        db      'RM Post EO,L-1,NA - cs:cs', 0;
        db      'RM Post EO,L-1,NA - cs_base:cs_base', 0;
        db      'RM Post EO,L-1,NA - cs_limit:cs_lim', 0;
        db      'RM Post EO,L-1,NA - cs_attr:cs_attr', 0;
        db      0,0,0,0 ; terminator
.s_szzRMEO2:
        db      'RM CS(0) EO,L-1  - cs:cs', 0;
        db      'RM CS(0) EO,L-1  - cs_base:cs_base', 0;
        db      'RM CS(0) EO,L-1  - cs_limit:cs_lim', 0;
        db      'RM CS(0) EO,L-1  - cs_attr:cs_attr', 0;
        db      0,0,0,0 ; terminator
.s_szTstRM:
        db      'Real Mode Test', 0
ENDPROC   rmTests



;
; Instantiate the template code.
;
%include "bootsector2-template-footer.mac"  ; reset the initial environemnt.

%define TMPL_RM
%include "bootsector2-cpu-hidden-regs-1-template.mac"
;%define TMPL_CMN_V86
;%include "bootsector2-cpu-hidden-regs-1-template.mac"
%define TMPL_PE16
%include "bootsector2-cpu-hidden-regs-1-template.mac"
%define TMPL_PE32
%include "bootsector2-cpu-hidden-regs-1-template.mac"
;%define TMPL_PP16
;%include "bootsector2-cpu-hidden-regs-1-template.mac"
%define TMPL_PP32
%include "bootsector2-cpu-hidden-regs-1-template.mac"
;%define TMPL_PAE16
;%include "bootsector2-cpu-hidden-regs-1-template.mac"
;%define TMPL_PAE32
;%include "bootsector2-cpu-hidden-regs-1-template.mac"
;%define TMPL_LM16
;%include "bootsector2-cpu-hidden-regs-1-template.mac"
;%define TMPL_LM32
;%include "bootsector2-cpu-hidden-regs-1-template.mac"
%define TMPL_LM64
%include "bootsector2-cpu-hidden-regs-1-template.mac"


;
; End sections and image.
;
%include "bootsector2-common-end.mac"

