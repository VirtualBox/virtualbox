; $Id: truncf.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT truncf - AMD64 & X86.
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
; Round to truncated integer value.
; @returns 32-bit: st(0)   64-bit: xmm0
; @param    rf      32-bit: [ebp + 8]   64-bit: xmm0
RT_NOCRT_BEGINPROC truncf
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        sub     xSP, 10h
        SEH64_ALLOCATE_STACK 10h
        SEH64_END_PROLOGUE

        ;
        ; Load the value into st(0).  This messes up SNaN values.
        ;
%ifdef RT_ARCH_AMD64
        movss   [xSP], xmm0
        fld     dword [xSP]
%else
        fld     dword [xBP + xCB*2]
%endif

        ;
        ; Return immediately if NaN or infinity.
        ;
        fxam
        fstsw   ax
        test    ax, X86_FSW_C0          ; C0 is set for NaN, Infinity and Empty register. The latter is not the case.
        jz      .input_ok
%ifdef RT_ARCH_AMD64
        ffreep  st0                     ; return the xmm0 register value unchanged, as FLD changes SNaN to QNaN.
%endif
        jmp     .return_val
.input_ok:

        ;
        ; Make it truncate up by modifying the fpu control word.
        ;
        fstcw   [xBP - 10h]
        mov     eax, [xBP - 10h]
        or      eax, X86_FCW_RC_ZERO    ; both bits set, so no need to clear anything first.
        mov     [xBP - 08h], eax
        fldcw   [xBP - 08h]

        ;
        ; Round ST(0) to integer.
        ;
        frndint

        ;
        ; Restore the fpu control word and return.
        ;
        fldcw   [xBP - 10h]

%ifdef RT_ARCH_AMD64
        fstp    dword [xSP]
        movss   xmm0, [xSP]
%endif
.return_val:
        leave
        ret
ENDPROC   RT_NOCRT(truncf)

