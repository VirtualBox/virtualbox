; $Id: cosf.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT cosf - AMD64 & X86.
;

;
; Copyright (C) 2006-2024 Oracle and/or its affiliates.
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


%define RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"


BEGINCODE

;;
; Compute the cosine of rf, measured in radians.
;
; @returns  st(0) / xmm0
; @param    rf      [rbp + xCB*2] / xmm0
;
RT_NOCRT_BEGINPROC cosf
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        sub     xSP, 20h
        SEH64_ALLOCATE_STACK 20h
        SEH64_END_PROLOGUE

%ifdef RT_OS_WINDOWS
        ;
        ; Make sure we use full precision and not the windows default of 53 bits.
        ;
;; @todo not sure if this makes any difference...
        fnstcw  [xBP - 20h]
        mov     ax, [xBP - 20h]
        or      ax, X86_FCW_PC_64       ; includes both bits, so no need to clear the mask.
        mov     [xBP - 1ch], ax
        fldcw   [xBP - 1ch]
%endif

        ;
        ; Load the input into st0.
        ;
%ifdef RT_ARCH_AMD64
        movss   [xBP - 10h], xmm0
        fld     dword [xBP - 10h]
%else
        fld     dword [xBP + xCB*2]
%endif

        ;
        ; The FCOS instruction has a very narrow range (-3pi/8 to 3pi/8) where it
        ; works reliably, so outside that we'll use the FSIN instruction instead
        ; as it has a larger good range (-5pi/4 to 1pi/4 for cosine).
        ; Input conversion follows: cosf(x) = sinf(x + pi/2)
        ;
        ; We examin the input and weed out non-finit numbers first.
        ;

        ; We only do the range check on normal finite numbers.
        fxam
        fnstsw  ax
        and     ax, X86_FSW_C3 | X86_FSW_C2 | X86_FSW_C0
        cmp     ax, X86_FSW_C2              ; Normal finite number (excluding zero)
        je      .finite
        cmp     ax, X86_FSW_C3              ; Zero
        je      .zero
        cmp     ax, X86_FSW_C3 | X86_FSW_C2 ; Denormals - treat them as zero.
        je      .zero
        cmp     ax, X86_FSW_C0              ; NaN - must handle it special,
        je      .nan

        ; Pass infinities and unsupported inputs to fcos, assuming it does the right thing.
        ; We also jump here if we get a finite number in the "good" range, see below.
.do_fcos:
        fcos
        jmp     .return_val

        ;
        ; Finite number.
        ;
        ; First check if it's a very tiny number where we can simply return 1.
        ; Next check if it's in the range where FCOS is reasonable, otherwise
        ; go to FSIN to do the work.
        ;
.finite:
        fld     st0
        fabs
        fld     qword [.s_r64TinyCosTo1 xWrtRIP]
        fcomip  st1
        ja      .zero_extra_pop

.not_that_tiny_input:
        fld     qword [.s_r64FCosOkay xWrtRIP]
        fcomip  st1
        ffreep  st0                         ; pop fabs(input)
        ja      .do_fcos                    ; jmp if fabs(input) < .s_r64FCosOkay

        ;
        ; If we have a positive number we subtract 3pi/2, for negative we add pi/2.
        ; We still have the FXAM result in AX.
        ;
.outside_fcos_range:
        test    ax, X86_FSW_C1              ; The sign bit.
        jnz     .adjust_negative_to_sine

        ; Calc -3pi/2 using FPU-internal pi constant.
        fldpi
        fadd    st0, st0                    ; st0=2pi
        fldpi
        fdiv    qword [.s_r64Two xWrtRIP]   ; st1=2pi; st0=pi/2
        fsubp   st1, st0                    ; st0=3pi/2
        fchs                                ; st0=-3pi/2
        jmp     .make_sine_adjustment

.adjust_negative_to_sine:
        ; Calc +pi/2.
        fldpi
        fdiv    qword [.s_r64Two xWrtRIP]   ; st1=2pi; st0=pi/2

.make_sine_adjustment:
        faddp   st1, st0

        ;
        ; Call internal sine worker to calculate st0=sin(st0)
        ;
.do_sine:
        mov     ecx, 0                      ; double
        extern  NAME(rtNoCrtMathSinCore)
        call    NAME(rtNoCrtMathSinCore)

        ;
        ; Return st0.
        ;
.return_val:
%ifdef RT_ARCH_AMD64
        fstp    dword [xBP - 10h]
        movss   xmm0, [xBP - 10h]
%endif
%ifdef RT_OS_WINDOWS
        fldcw   [xBP - 20h]                 ; restore original
%endif
.return:
        leave
        ret

        ;
        ; cosf(+/-0) = +1.0
        ;
.zero_extra_pop:
        ffreep  st0
.zero:
        ffreep  st0
        fld1
        jmp     .return_val

        ;
        ; Input is NaN, output it unmodified as far as we can (FLD changes SNaN
        ; to QNaN when masked).
        ;
.nan:
%ifdef RT_ARCH_AMD64
        ffreep  st0
%endif
        jmp     .return

        ;
        ; Local constants.
        ;
ALIGNCODE(8)
        ; About 2**-18. When fabs(input) is below this limit we can consider cosf(input) ~= 1.0.
.s_r64TinyCosTo1:
        dq  0.000244140625

        ; The absolute limit for the range which FCOS is expected to produce reasonable results.
.s_r64FCosOkay:
        dq  1.1780972450961724644225   ; 3*pi/8

.s_r64Two:
        dq  2.0
ENDPROC   RT_NOCRT(cosf)

