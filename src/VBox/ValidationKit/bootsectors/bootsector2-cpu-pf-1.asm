; $Id: bootsector2-cpu-pf-1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Bootsector test for various types of #PFs.
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


;*******************************************************************************
;*      Defined Constants And Macros                                           *
;*******************************************************************************
;; Base address at which we can start testing page tables and page directories.
%define TST_SCRATCH_PD_BASE             BS2_MUCK_ABOUT_BASE
;; Base address at which we can start testing the page pointer table.
%define TST_SCRATCH_PDPT_BASE           (1 << X86_PDPT_SHIFT)
;; Base address at which we can start testing the page map level 4.
%define TST_SCRATCH_PML4_BASE           ((1 << X86_PML4_SHIFT) + TST_SCRATCH_PD_BASE)

;; The number of loops done during calibration.
%define TST_CALIBRATE_LOOP_COUNT        10000
;; The desired benchmark period (seconds).
%define TST_BENCHMARK_PERIOD_IN_SECS    2



;
; Include and execute the init code.
;
        %define BS2_INIT_RM
        %define BS2_WITH_TRAPS
        %define BS2_INC_RM
;        %define BS2_INC_PP16
        %define BS2_INC_PP32
;        %define BS2_INC_PAE16
        %define BS2_INC_PAE32
;        %define BS2_INC_LM16
;        %define BS2_INC_LM32
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
        call    Bs2EnableA20_r86

        ;
        ; Execute the tests
        ;
%if 1
 %if 1
        xor     eax, eax                ; NXE=0 (N/A)
        call    NAME(DoTestsForMode_rm_pp32)
 %endif
 %if 1
        xor     eax, eax                ; NXE=0
        call    NAME(DoTestsForMode_rm_pae32)
        mov     eax, 1                  ; NXE=1
        call    NAME(DoTestsForMode_rm_pae32)
 %endif
 %if 1
        xor     eax, eax                ; NXE=0
        call    NAME(DoTestsForMode_rm_lm64)
        mov     eax, 1                  ; NXE=1
        call    NAME(DoTestsForMode_rm_lm64)
 %endif
%endif

        ;
        ; Execute benchmarks.
        ;
%if 1
        mov     ax, .s_szTstBenchmark
        call    NAME(TestSub_r86)
        call    NAME(DoBenchmarksForMode_rm_pp32)
        call    NAME(DoBenchmarksForMode_rm_pae32)
        call    NAME(DoBenchmarksForMode_rm_lm64)
        call    NAME(TestSubDone_r86)
%endif

        ;
        ; We're done.
        ;
        call    TestTerm_r86
        ret

.s_szTstBenchmark:
        db      'Benchmark', 0
.s_szTstName:
        db      'tstIOIntr', 0
.s_szTstX:
        db      'X', 0
ENDPROC   main


;
; Instantiate the template code.
;
%include "bootsector2-template-footer.mac"  ; reset the initial environemnt.

;%define TMPL_PP16
;%include "bootsector2-cpu-pf-1-template.mac"
%define TMPL_PP32
%include "bootsector2-cpu-pf-1-template.mac"
;%define TMPL_PAE16
;%include "bootsector2-cpu-pf-1-template.mac"
%define TMPL_PAE32
%include "bootsector2-cpu-pf-1-template.mac"
;%define TMPL_LM16
;%include "bootsector2-cpu-pf-1-template.mac"
;%define TMPL_LM32
;%include "bootsector2-cpu-pf-1-template.mac"
%define TMPL_LM64
%include "bootsector2-cpu-pf-1-template.mac"


;
; End sections and image.
;
%include "bootsector2-common-end.mac"

