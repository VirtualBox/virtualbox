; $Id: bootsector2-cpu-basic-1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Bootsector that checks the basic CPU operation.
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
%define BS2_INC_PP16
%define BS2_INC_PP32
%define BS2_INC_PAE32
%define BS2_INC_PAE16
%define BS2_INC_LM16
%define BS2_INC_LM32
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
        call    TestBasic1_rm_rm
        call    TestBasic1_rm_pe16
        call    TestBasic1_rm_pe32
        call    TestBasic1_rm_pp32
        call    TestBasic1_rm_pp16
        call    TestBasic1_rm_pae16
        call    TestBasic1_rm_pae32
        call    TestBasic1_rm_lm64
        call    TestBasic1_rm_lm32
        call    TestBasic1_rm_lm16

        ;
        ; We're done.
        ;
        call    TestTerm_r86
        call    Bs2Panic

.s_szTstName:
        db      'tstBasic1-1', 0
.s_szInitialBasic1Status:
        db      'Initial Basic1 state', 0
ENDPROC   main


;
; Instantiate the template code.
;
%include "bootsector2-template-footer.mac"  ; reset the initial environemnt.

%define TMPL_RM
%include "bootsector2-cpu-basic-1-template.mac"
;%define TMPL_CMN_V86
;%include "bootsector2-cpu-basic-1-template.mac"
%define TMPL_PE16
%include "bootsector2-cpu-basic-1-template.mac"
%define TMPL_PE32
%include "bootsector2-cpu-basic-1-template.mac"
%define TMPL_PP16
%include "bootsector2-cpu-basic-1-template.mac"
%define TMPL_PP32
%include "bootsector2-cpu-basic-1-template.mac"
%define TMPL_PAE16
%include "bootsector2-cpu-basic-1-template.mac"
%define TMPL_PAE32
%include "bootsector2-cpu-basic-1-template.mac"
%define TMPL_LM16
%include "bootsector2-cpu-basic-1-template.mac"
%define TMPL_LM32
%include "bootsector2-cpu-basic-1-template.mac"
%define TMPL_LM64
%include "bootsector2-cpu-basic-1-template.mac"


;
; End sections and image.
;
%include "bootsector2-common-end.mac"

