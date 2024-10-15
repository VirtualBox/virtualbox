; $Id: remainderl.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - No-CRT remainderl - AMD64 & X86.
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
; See SUS.
; @returns st(0)
; @param    lrd1    [rbp + 10h]
; @param    lrd2    [rbp + 20h]
RT_NOCRT_BEGINPROC remainderl
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        SEH64_END_PROLOGUE

        ;
        ; Load the dividend into st0 and divisor into st1.
        ;
        fld     tword [xBP + 2*xCB + RTLRD_CB]
        fld     tword [xBP + 2*xCB]

        ;
        ; The fprem1 only does between 32 and 64 rounds, so we have to loop
        ; here till we've got a final result.  We count down in ECX to
        ; avoid getting stuck here...
        ;
        mov     ecx, 16384 / 32 + 4
.again:
        fprem1
        fstsw   ax
        test    ah, (X86_FSW_C2 >> 8)
        jz      .done
        dec     cx
        jnz     .again
%ifdef RT_STRICT
        int3
%endif

        ;
        ; Return the result.
        ;
.done:
        fstp    st1
        leave
        ret
ENDPROC   RT_NOCRT(remainderl)

