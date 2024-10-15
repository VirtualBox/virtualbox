; $Id: bootsector2-cpu-a20-1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Bootsector that checks the A20 emulation.
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

;
; Include and execute the init code.
;
%define BS2_WITH_TRAPS
%define BS2_INIT_RM
%define BS2_INC_PE16
%define BS2_INC_PE32
%define BS2_INC_PP32
%define BS2_INC_PAE32
%define BS2_INC_LM64
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

        ;
        ; The actual tests.
        ;
        call    TestA20_1               ; must come first
        call    TestA20_rm_rm
        ;call    TestA20_rm_pe16
        call    TestA20_rm_pe32
        call    TestA20_rm_pp32
        ;call    TestA20_rm_pp16
        call    TestA20_rm_pae32
        call    TestA20_rm_lm64

        ;
        ; We're done.
        ;
        call    TestTerm_r86
        call    Bs2Panic

.s_szTstName:
        db      'tstA20-1', 0
.s_szInitialA20Status:
        db      'Initial A20 state', 0
ENDPROC   main


;
; Do some initial tests.
;
BEGINPROC TestA20_1
        push    eax
        push    edx
        push    ecx
        push    ebx
        push    edi

        ;
        ; Check that the A20 gate is disabled when we come from the BIOS.
        ;
        mov     ax, .s_szInitialA20Status
        call    TestSub_r86

        call    IsA20GateEnabled_rm
        mov     di, ax                  ; save A20 state in AX for bios test.
        cmp     al, 0
        je      .initial_state_done
        mov     ax, .s_szBadInitialA20Status
        call    TestFailed_r86
        jmp     .initial_state_done
.s_szInitialA20Status:
        db      'Initial A20 state', 0
.s_szBadInitialA20Status:
        db      'Initial A20 state is enabled, expected disabled', 10, 13, 0
.initial_state_done:
        call    TestSubDone_r86

        ;
        ; Disable it via the BIOS interface and check.
        ;
        mov     ax, .s_szBios
        call    TestSub_r86

        ; query support
        mov     ax, 2403h
        int     15h
        jnc     .bios_2403_ok
        movzx   edx, ax
        mov     ax, .s_szBios2403Error
        mov     cl, VMMDEV_TESTING_UNIT_NONE
        call    TestValueU32_r86
        jmp     .bios_2403_done
.bios_2403_ok:
        movzx   edx, al
        mov     ax, .s_szBios2403Mask
        mov     cl, VMMDEV_TESTING_UNIT_NONE
        call    TestValueU32_r86
.bios_2403_done:

        ; Check what the bios thinks the state is.
        call    BiosIsA20GateEnabled_rm
        cmp     ax, di
        je      .bios_2402_done
        push    di
        push    ax
        push    word ds
        push    word .s_szBios2402Error
        call    TestFailedF_r86
        add     sp, 8
.bios_2402_done:

        ; Loop to make sure we get all transitions and ends up with A20 disabled.
        mov     cx, 10h
.bios_loop:
        ; enable it
        mov     ax, 2401h
        push    cx                      ; paranoia that seems necessary for at least one AMI bios.
        int     15h
        pop     cx
        jnc     .bios_continue1
        mov     ax, .s_szBiosFailed2401
        jmp     .bios_failed
.bios_continue1:

        call    IsA20GateEnabled_rm
        cmp     al, 1
        je      .bios_continue2
        mov     ax, .s_szBiosEnableFailed
        jmp     .bios_failed
.bios_continue2:

        ; disable
        mov     ax, 2400h
        push    cx                      ; paranoia that seems necessary for at least one AMI bios.
        int     15h
        pop     cx
        jnc     .bios_continue3
        mov     ax, .s_szBiosFailed2400
        jmp     .bios_failed
.bios_continue3:
        call    IsA20GateEnabled_rm
        cmp     al, 0
        je      .bios_continue4
        mov     ax, .s_szBiosDisableFailed
        jmp     .bios_failed
.bios_continue4:

        loop    .bios_loop
        jmp     .bios_done
.s_szBios:
        db      'INT 15h AH=24 A20 Gate interface', 0
.s_szBios2403Mask:
        db      'AX=2403 return (AL)', 0
.s_szBios2403Error:
        db      'AX=2403 error (AX)', 10, 13,  0
.s_szBios2402Error:
        db      '2402h -> AX=%RX16 expected %RX16', 10, 13, 0
.s_szBiosFailed2400:
        db      '2400h interface failed', 10, 13, 0
.s_szBiosFailed2401:
        db      '2401h interface failed', 10, 13, 0
.s_szBiosDisableFailed:
        db      'BIOS failed to disable A20 (or bad CPU)', 10, 13, 0
.s_szBiosEnableFailed:
        db      'BIOS failed to enable A20', 10, 13, 0
.bios_failed:
        call    TestFailed_r86
.bios_done:
        call    TestSubDone_r86
        call    Bs2DisableA20ViaPortA_r86
        call    Bs2DisableA20ViaKbd_r86

        ;
        ; Test the fast A20 gate interface.
        ;
        mov     ax, .s_szFastA20
        call    TestSub_r86

        mov     cx, 10h
.fast_loop:
        call    Bs2EnableA20ViaPortA_r86
        call    IsA20GateEnabled_rm
        cmp     al, 1
        mov     ax, .s_szFastEnableFailed
        jne     .fast_failed

        call    Bs2DisableA20ViaPortA_r86
        call    IsA20GateEnabled_rm
        cmp     al, 0
        mov     ax, .s_szFastDisableFailed
        jne     .fast_failed
        loop    .fast_loop

        jmp     .fast_done
.s_szFastA20:
        db      'Fast A20 Gate Interface', 0
.s_szFastDisableFailed:
        db      'Fast A20 gate disabling failed', 10, 13, 0
.s_szFastEnableFailed:
        db      'Fast A20 gate enabling failed', 10, 13, 0
.fast_failed:
        call    TestFailed_r86
.fast_done:
        call    TestSubDone_r86
        call    Bs2DisableA20ViaPortA_r86
        call    Bs2DisableA20ViaKbd_r86

        ;
        ; Test the keyboard interface.
        ;
        mov     ax, .s_szKeyboardA20
        call    TestSub_r86

        mov     cx, 10h
.kbd_loop:
        call    Bs2EnableA20ViaKbd_r86
        call    IsA20GateEnabled_rm
        cmp     al, 1
        mov     ax, .s_szKbdEnableFailed
        jne     .kbd_failed

        call    Bs2DisableA20ViaKbd_r86
        call    IsA20GateEnabled_rm
        cmp     al, 0
        mov     ax, .s_szKbdDisableFailed
        jne     .kbd_failed
        loop    .kbd_loop

        jmp     .kbd_done
.s_szKeyboardA20:
        db      'Keyboard A20 Gate Interface', 0
.s_szKbdDisableFailed:
        db      'Disabling the A20 gate via the keyboard controller failed', 10, 13, 0
.s_szKbdEnableFailed:
        db      'Enabling the A20 gate via the keyboard controller failed', 10, 13, 0
.kbd_failed:
        call    TestFailed_r86
.kbd_done:
        call    TestSubDone_r86
        call    Bs2DisableA20ViaPortA_r86
        call    Bs2DisableA20ViaKbd_r86

        pop     edi
        pop     ebx
        pop     ecx
        pop     edx
        pop     eax
        ret
ENDPROC   TestA20_1


;;
; Checks if the A20 gate is enabled.
;
; This is do by temporarily changing a word at address 0000000h and see if this
; is reflected at address 0100000h (1 MB).  The word written is
; ~*(word *)0x100000h to make sure it won't accidentally match.
;
; @returns ax   1 if enabled, 0 if disabled.
;
BEGINPROC IsA20GateEnabled_rm
        push    ds
        push    es
        push    dx
        pushf
        cli

.once_again:
        xor     ax, ax
        mov     ds, ax
        dec     ax
        mov     es, ax

        mov     ax, [es:0010h]          ; 0ffff:0010 => 0100000h (1 MB)
        mov     dx, [ds:0000h]          ; 00000:0000 => 0000000h - save it
        not     ax
        mov     [ds:0000h], ax          ; 0000000h - write ~[0100000h]
        cmp     [es:0010h], ax          ; 0100000h - same as 0000000h if A20 is disabled.
        mov     [ds:0000h], dx          ; 0000000h - restore original value
        setne   al
        movzx   ax, al

        popf
        pop     dx
        pop     es
        pop     ds
        ret
ENDPROC   IsA20GateEnabled_rm

;;
; Checks if the BIOS thinks the A20 gate is enabled.
;
; @returns ax   1 if enabled, 0 if disabled.
;
BEGINPROC BiosIsA20GateEnabled_rm
        push    ecx
        push    eax

        mov     ax, 2402h
        int     15h
        jnc     .ok
        mov      al, 080h
.ok:
        mov     cx, ax
        pop     eax
        mov     ax, cx
        pop     ecx
        ret
ENDPROC   BiosIsA20GateEnabled_rm

;
; Instantiate the template code.
;
%include "bootsector2-template-footer.mac"  ; reset the initial environemnt.

%define TMPL_RM
%include "bootsector2-cpu-a20-1-template.mac"
;%define TMPL_CMN_V86
;%include "bootsector2-cpu-a20-1-template.mac"
%define TMPL_PE16
%include "bootsector2-cpu-a20-1-template.mac"
%define TMPL_PE32
%include "bootsector2-cpu-a20-1-template.mac"
;%define TMPL_PP16
;%include "bootsector2-cpu-a20-1-template.mac"
%define TMPL_PP32
%include "bootsector2-cpu-a20-1-template.mac"
;%define TMPL_PAE16
;%include "bootsector2-cpu-a20-1-template.mac"
%define TMPL_PAE32
%include "bootsector2-cpu-a20-1-template.mac"
;%define TMPL_LM16
;%include "bootsector2-cpu-a20-1-template.mac"
;%define TMPL_LM32
;%include "bootsector2-cpu-a20-1-template.mac"
%define TMPL_LM64
%include "bootsector2-cpu-a20-1-template.mac"


;
; End sections and image.
;
%include "bootsector2-common-end.mac"

