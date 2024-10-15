; $Id: sinf.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT sinf - AMD64 & X86.
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
; Compute the sine of rd, measured in radians.
;
; @returns  st(0) / xmm0
; @param    rd      [rbp + xCB*2] / xmm0
;
RT_NOCRT_BEGINPROC sinf
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
        ; We examin the input and weed out non-finit numbers first.
        ;
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

        ; Pass infinities and unsupported inputs to fsin, assuming it does the right thing.
.do_sin:
        fsin
        jmp     .return_val

        ;
        ; Finite number.
        ;
.finite:
        ; For very tiny numbers, 0 < abs(input) < 2**-25, we can return the
        ; input value directly.
        fld     st0                         ; duplicate st0
        fabs                                ; make it an absolute (positive) value.
        fld     qword [.s_r64Tiny xWrtRIP]
        fcomip  st1                         ; compare s_r64Tiny and fabs(input)
        ja      .return_tiny_number_as_is   ; jump if fabs(input) is smaller

        ; FSIN is documented to be reasonable for the range ]-3pi/4,3pi/4[, so
        ; while we have fabs(input) loaded already, check for that here and
        ; allow rtNoCrtMathSinCore to assume it won't see values very close to
        ; zero, except by cos -> sin conversion where they won't be relevant to
        ; any assumpttions about precision approximation.
        fld     qword [.s_r64FSinOkay xWrtRIP]
        fcomip  st1
        ffreep  st0                         ; drop the fabs(input) value
        ja      .do_sin

        ;
        ; Call common sine/cos worker.
        ;
        mov     ecx, 0                      ; float
        extern  NAME(rtNoCrtMathSinCore)
        call    NAME(rtNoCrtMathSinCore)

        ;
        ; Run st0.
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
        ; As explained already, we can return tiny numbers directly too as the
        ; output from sinf(input) = input given our precision.
        ; We can skip the st0 -> xmm0 translation here, so follow the same path
        ; as .zero & .nan, after we've removed the fabs(input) value.
        ;
.return_tiny_number_as_is:
        ffreep  st0

        ;
        ; sinf(+/-0.0) = +/-0.0 (preserve the sign)
        ; We can skip the st0 -> xmm0 translation here, so follow the .nan code path.
        ;
.zero:

        ;
        ; Input is NaN, output it unmodified as far as we can (FLD changes SNaN
        ; to QNaN when masked).
        ;
.nan:
%ifdef RT_ARCH_AMD64
        ffreep  st0
%endif
        jmp     .return

ALIGNCODE(8)
        ; Ca. 2**-26, absolute value. Inputs closer to zero than this can be
        ; returns directly as the sinf(input) value should be basically the same
        ; given the precision we're working with and FSIN probably won't even
        ; manage that.
        ;; @todo experiment when FSIN gets better than this.
.s_r64Tiny:
        dq      1.49011612e-8
        ; The absolute limit of FSIN "good" range.
.s_r64FSinOkay:
        dq      2.356194490192344928845 ; 3pi/4
        ;dq      1.57079632679489661923  ; pi/2 - alternative.

ENDPROC   RT_NOCRT(sinf)

