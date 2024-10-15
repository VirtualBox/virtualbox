; $Id: fegetexcept.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT fegetexcept - AMD64 & X86.
;

;
; Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
; Gets the mask of enabled exceptions, e.g. unmasked (BSD/GNU extension).
;
; @returns  eax = inverted x87/sse exception mask (X86_MXCSR_XCPT_FLAGS).
;           Will not return X86_FSW_SF.
;
RT_NOCRT_BEGINPROC fegetexcept
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        sub     xSP, 10h
        SEH64_ALLOCATE_STACK 10h
        SEH64_END_PROLOGUE

        ;
        ; Save control word and isolate the exception mask.
        ;
        ; On 64-bit we'll use the MXCSR since the windows compiler/CRT doesn't
        ; necessarily keep them in sync.  We'll still return the x87-style flags.
        ;
%ifdef RT_ARCH_AMD64
        stmxcsr [xBP - 10h]
        mov     eax, [xBP - 10h]
        shr     eax, X86_MXCSR_XCPT_MASK_SHIFT
%else
        fstcw   [xBP - 10h]
        movzx   eax, word [xBP - 10h]
%endif

        not     eax                         ; Invert it as we return the enabled rather than masked exceptions.
        and     eax, X86_MXCSR_XCPT_FLAGS   ; Use the SSE mask so we don't return X86_FSW_SF here.

.return_val:
        leave
        ret
ENDPROC   RT_NOCRT(fegetexcept)

