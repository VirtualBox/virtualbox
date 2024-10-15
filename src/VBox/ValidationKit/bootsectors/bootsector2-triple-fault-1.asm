; $Id: bootsector2-triple-fault-1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Bootsector for testing triple faults.
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


%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"
%include "VBox/VMMDevTesting.mac"

;; The number of instructions to test.
%define TEST_INSTRUCTION_COUNT_IO       2000000

;; The number of instructions to test.
%define TEST_INSTRUCTION_COUNT_MMIO     750000

;; Define this to drop unnecessary test variations.
%define QUICK_TEST

;
; Include and execute the init code.
;
        %define BS2_INIT_RM
        %define BS2_INC_PE16
        %define BS2_INC_PE32
        %define BS2_INC_PP32
        %define BS2_INC_PAE32
        %define BS2_INC_LM64
        %include "bootsector2-common-init-code.mac"


;
; The test driver
;
BEGINPROC main
        ;
        ; Test prologue.
        ;
        mov     ax, .s_szTstName
        call    TestInit_r86
        call    Bs2EnableA20_r86

        ;
        ; Did we get here from a reboot triggered below?
        ;
        push    ds
        mov     ax, 2000h               ; 128 KB is enough for the test program
.boot_check_loop:
        mov     ds, ax
        cmp     dword [0], 064726962h
        jne     .boot_check_next
        cmp     dword [4], 062697264h
        je      .warm_reset_broken

.boot_check_next:
        mov     dword [0], 064726962h
        mov     dword [4], 062697264h
        add     ax, 1000h
        cmp     ax, 8000h
        jbe     .boot_check_loop
        pop     ds
        jmp     .fine

.warm_reset_broken:
        pop     ds
        mov     ax, .s_szWarmResetBroken
        call    NAME(TestFailed_r86)
        jmp     .done
.s_szWarmResetBroken:
        db      'Warm reset vector support is broken', 0dh, 0ah, 0
.fine:

        ;
        ; Test that the warm reset interface works.
        ;
        mov     ax, .s_szPrecondTest5
        call    NAME(TestSub_r86)
        mov     al, 05h
        call    NAME(SetWarmResetJmp)
        cmp     ax, 0
        jne     .precond_test_A
        call    NAME(TestReboot_r86)

.precond_test_A:
        mov     ax, .s_szPrecondTestA
        call    NAME(TestSub_r86)
        mov     al, 0Ah
        call    NAME(SetWarmResetJmp)
        cmp     ax, 0
        jne     .precond_test_A_passed
        call    NAME(TestReboot_r86)
.precond_test_A_passed:
        call    NAME(TestSubDone_r86)

        ;
        ; The real tests.
        ;


        ;
        ; We're done.
        ;
.done:
        call    NAME(TestTerm_r86)
        call    Bs2Panic

.s_szTstName:
        db      'tstTriple', 0
.s_szPrecondTest5:
        db      'Shutdown Action 5', 0
.s_szPrecondTestA:
        db      'Shutdown Action A', 0
ENDPROC   main



;;
; Sets up the warm reset vector.
;
; @param    ax          Where to resume exeuction.
; @param    dl          Shutdown action command to use, 5h or Fh.
;
; @uses     nothing
;
BEGINPROC SetUpWarmReset
        push    bp
        mov     bp, sp
        push    eax
        push    ebx
        push    ecx
        push    edx
        push    edi
        push    esi
        push    es

        ;
        ; Set up the warm reboot vector.
        ;
        mov     bx, 40h
        mov     es, bx

        mov     ecx, [es:67h]           ; debug
        mov     word [es:67h], ax
        mov     bx, cs
        mov     word [es:67h+2], bx

        mov     bx, [es:72h]            ; debug
        mov     word [es:72h], 1234h    ; warm reboot

        wbinvd

        mov     al, 0fh
        out     70h, al                 ; set register index
        in      al, 71h
        mov     ah, al                  ; debug
        mov     al, dl                  ; shutdown action command
        out     71h, al                 ; set cmos[f] = a - invoke testResume as early as possible.
        in      al, 71h                 ; debug / paranoia
        movzx   si, al

        ; Debug print.
%if 1
        mov     di, sp                  ; save sp (lazy bird)
        in      al, 64h
        push    ax                      ; kbd status
        push    si                      ; cmos[f] after
        mov     al, ah                  ; cmos[f] before
        push    ax
        push    word [0472h]            ; 40:72 word after
        push    bx                      ; 40:72 word before
        push    word [0467h]            ; 40:67 far addr after
        push    word [0469h]
        push    cx                      ; 40:67 far addr before
        shr     ecx, 16
        push    dx
        push    ds
        push    .s_szDebugFmt
        call    NAME(PrintF_r86)
        mov     sp, di                  ; restore sp.
;.forever:
;        cli
;        hlt
;        jmp     .forever
%endif

        pop     es
        pop     esi
        pop     edi
        pop     edx
        pop     ecx
        pop     ebx
        pop     eax
        leave
        ret

.s_szDebugFmt:
        db      'dbg: 40:67=%RX16:%RX16 (%RX16:%RX16) 40:72=%RX16 (%RX16) cmos[f]=%RX8 (%RX8) kbdsts=%RX8', 0dh, 0ah, 0
ENDPROC   SetUpWarmReset


;;
; Sets up the warm reset vector.
;
; @returns  ax = 0 on setup call, ax = 1 on resume return.
; @param    al          Shutdown action command to use, 5h or Fh.
; @uses     ax
;
BEGINPROC SetWarmResetJmp
        push    bp
        mov     bp, sp
        push    dx

        mov     dl, al
        mov     ax, .resume
        call    NAME(SetUpWarmReset)

%ifdef DEBUG
        push    cs
        push    .s_szDbg1
        call    NAME(PrintF_r86)
        add     sp, 4
%endif

        mov     ax, .s_ResumeRegs
        call    NAME(TestSaveRegisters_r86)

%ifdef DEBUG
        push    cs
        push    .s_szDbg2
        call    NAME(PrintF_r86)
        add     sp, 4
%endif

        mov     dx, [bp - 2]
        mov     [.s_ResumeRegs + BS2REGS.rdx], dx
        mov     ax, bp
        add     ax, 4
        mov     [.s_ResumeRegs + BS2REGS.rsp], ax
        mov     ax, [bp]
        mov     [.s_ResumeRegs + BS2REGS.rbp], ax
        mov     ax, [bp + 2]
        mov     [.s_ResumeRegs + BS2REGS.rip], ax
        mov     word [.s_ResumeRegs + BS2REGS.rax], 1

%ifdef DEBUG
        push    cs
        push    .s_szDbg3
        call    NAME(PrintF_r86)
        add     sp, 4
%endif

        xor     ax, ax
.done:
        pop     dx
        leave
        ret

.resume:
        cli
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        mov     ax, [.s_ResumeRegs + BS2REGS.ss]
        mov     ss, ax
        mov     esp, [.s_ResumeRegs + BS2REGS.rsp]
        mov     ebp, [.s_ResumeRegs + BS2REGS.rbp]

%ifdef DEBUG
        push    ds
        push    .s_szDbg4
        call    NAME(PrintF_r86)
        add     sp, 4
%endif

        mov     ax, .s_ResumeRegs
        call    NAME(TestRestoreRegisters_r86)
        mov     ax, [.s_ResumeRegs + BS2REGS.rip]
        push    ax
        mov     ax, 1
        ret
        ;jmp     word [.s_ResumeRegs + BS2REGS.rip]

.s_ResumeRegs:
        times (BS2REGS_size) db 0
%ifdef DEBUG
.s_szDbg1:
        db 'dbg 1', 0dh, 0ah, 0
.s_szDbg2:
        db 'dbg 2', 0dh, 0ah, 0
.s_szDbg3:
        db 'dbg 3', 0dh, 0ah, 0
.s_szDbg4:
        db 'dbg 4', 0dh, 0ah, 0
%endif
ENDPROC   SetWarmResetJmp


;;
; Reboot the machine.  Will not return.
;
BEGINPROC TestReboot_r86
%ifdef DEBUG
        ; Debug
        push    ds
        push    .s_szDbg
        call    NAME(PrintF_r86)
%endif
        ; Via port A
        in      al, 92h
        and     al, ~1
        out     92h, al
        or      al, 1
        out     92h, al
        in      al, 92h
.forever:
        cli
        hlt
        jmp     .forever
%ifdef DEBUG
.s_szDbg:
        db 'Rebooting...', 0dh, 0ah, 0
%endif
ENDPROC   TestReboot_r86


;
; Instantiate the template code.
;
%include "bootsector2-template-footer.mac"  ; reset the initial environemnt.

;%define TMPL_RM
;%include "bootsector2-test1-template.mac"
;%define TMPL_CMN_V86
;%include "bootsector2-test1-template.mac"
;%define TMPL_PE16
;%include "bootsector2-test1-template.mac"
;%define TMPL_PE32
;%include "bootsector2-test1-template.mac"
;%define TMPL_PP16
;%include "bootsector2-test1-template.mac"
;%define TMPL_PP32
;%include "bootsector2-test1-template.mac"
;%define TMPL_PAE16
;%include "bootsector2-test1-template.mac"
;%define TMPL_PAE32
;%include "bootsector2-test1-template.mac"
;%define TMPL_LM16
;%include "bootsector2-test1-template.mac"
;%define TMPL_LM32
;%include "bootsector2-test1-template.mac"
;%define TMPL_LM64
;%include "bootsector2-test1-template.mac"


;
; End sections and image.
;
%include "bootsector2-common-end.mac"

