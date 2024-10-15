; $Id: bootsector2-test1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; Bootsector that benchmarks I/O and MMIO roundtrip time.
;   VBoxManage setextradata bs-test1 VBoxInternal/Devices/VMMDev/0/Config/TestingEnabled  1
;   VBoxManage setextradata bs-test1 VBoxInternal/Devices/VMMDev/0/Config/TestingMMIO  1
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

;; The number of CPUID instructions to test.
%define TEST_INSTRUCTION_COUNT_CPUID    8000000

;; The number of RDTSC instructions to test.
%define TEST_INSTRUCTION_COUNT_RDTSC    4000000

;; The number of RDTSC instructions to test.
%define TEST_INSTRUCTION_COUNT_READCR4  1000000

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

%if 0
        ;
        ; IA32_FLUSH_CMD.
        ;
        mov     ax, .s_szTstFlushCmd
        call    TestSub_r86
        call    BenchmarkFlushCmd_rm_pp32
        call    BenchmarkFlushCmd_rm_pae32
        call    BenchmarkFlushCmd_rm_lm64
        call    BenchmarkFlushCmd_rm_pe16
        call    BenchmarkFlushCmd_rm_pe32
        call    BenchmarkFlushCmd_rm_rm
%endif

        ;
        ; CPUID.
        ;
        mov     ax, .s_szTstCpuId
        call    TestSub_r86
        call    BenchmarkCpuId_rm_pp32
        call    BenchmarkCpuId_rm_pae32
        call    BenchmarkCpuId_rm_lm64
        call    BenchmarkCpuId_rm_pe16
        call    BenchmarkCpuId_rm_pe32
        call    BenchmarkCpuId_rm_rm

        ;
        ; RDTSC.
        ;
        mov     ax, .s_szTstRdTsc
        call    TestSub_r86
        call    BenchmarkRdTsc_rm_pp32
        call    BenchmarkRdTsc_rm_pae32
        call    BenchmarkRdTsc_rm_lm64
        call    BenchmarkRdTsc_rm_pe16
        call    BenchmarkRdTsc_rm_pe32
        call    BenchmarkRdTsc_rm_rm

        ;
        ; Read CR4
        ;
        mov     ax, .s_szTstRdCr4
        call    TestSub_r86
        call    BenchmarkRdCr4_rm_pp32
        call    BenchmarkRdCr4_rm_pae32
        call    BenchmarkRdCr4_rm_lm64
        call    BenchmarkRdCr4_rm_pe16
        call    BenchmarkRdCr4_rm_pe32
        call    BenchmarkRdCr4_rm_rm

        ;
        ; I/O port access.
        ;
        mov     ax, .s_szTstNopIoPort
        call    TestSub_r86
        call    BenchmarkIoPortNop_rm_rm
        call    BenchmarkIoPortNop_rm_pe16
        call    BenchmarkIoPortNop_rm_pe32
        call    BenchmarkIoPortNop_rm_pp32
        call    BenchmarkIoPortNop_rm_pae32
        call    BenchmarkIoPortNop_rm_lm64

        ;
        ; MMIO access.
        ;
        mov     ax, .s_szTstNopMmio
        call    TestSub_r86
        call    BenchmarkMmioNop_rm_pp32
        call    BenchmarkMmioNop_rm_pae32
        call    BenchmarkMmioNop_rm_lm64
        call    BenchmarkMmioNop_rm_pe16
        call    BenchmarkMmioNop_rm_pe32
        call    BenchmarkMmioNop_rm_rm

        ;
        ; We're done.
        ;
        call    TestTerm_r86
        call    Bs2Panic

.s_szTstName:
        db      'tstIOIntr', 0
.s_szTstCpuId:
        db      'CPUID EAX=1', 0
.s_szTstFlushCmd:
        db      'IA32_FLUSH_CMD', 0
.s_szTstRdTsc:
        db      'RDTSC', 0
.s_szTstRdCr4:
        db      'Read CR4', 0
.s_szTstNopIoPort:
        db      'NOP I/O Port Access', 0
.s_szTstNopMmio:
        db      'NOP MMIO Access', 0
ENDPROC   main


;
; Instantiate the template code.
;
%include "bootsector2-template-footer.mac"  ; reset the initial environemnt.

%define TMPL_RM
%include "bootsector2-test1-template.mac"
;%define TMPL_CMN_V86
;%include "bootsector2-test1-template.mac"
%define TMPL_PE16
%include "bootsector2-test1-template.mac"
%define TMPL_PE32
%include "bootsector2-test1-template.mac"
;%define TMPL_PP16
;%include "bootsector2-test1-template.mac"
%define TMPL_PP32
%include "bootsector2-test1-template.mac"
;%define TMPL_PAE16
;%include "bootsector2-test1-template.mac"
%define TMPL_PAE32
%include "bootsector2-test1-template.mac"
;%define TMPL_LM16
;%include "bootsector2-test1-template.mac"
;%define TMPL_LM32
;%include "bootsector2-test1-template.mac"
%define TMPL_LM64
%include "bootsector2-test1-template.mac"


;
; End sections and image.
;
%include "bootsector2-common-end.mac"

