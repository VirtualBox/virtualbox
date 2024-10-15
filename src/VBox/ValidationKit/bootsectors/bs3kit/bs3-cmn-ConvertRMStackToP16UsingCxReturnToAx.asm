; $Id: bs3-cmn-ConvertRMStackToP16UsingCxReturnToAx.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3ConvertRMStackToP16UsingCxReturnToAx.
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

%include "bs3kit-template-header.mac"


%if TMPL_BITS != 16
 %error 16-bit only!
%endif

;;
; An internal helper for converting a real-mode stack into a 16-bit protected
; mode stack.
;
; This is used by the mode switchers that ends up in 16-bit mode.  It is
; assumed that we're in ring-0.
;
; @param    ax          The return address.
;
; @uses     cx, ss, esp
;
BS3_PROC_BEGIN_CMN Bs3ConvertRMStackToP16UsingCxReturnToAx, BS3_PBC_NEAR

        ;
        ; Check if it looks like the normal stack, if use BS3_SEL_R0_SS16.
        ;
        mov     cx, ss
        cmp     cx, 0
        jne     .stack_tiled
        mov     cx, BS3_SEL_R0_SS16
        mov     ss, cx
        jmp     ax

        ;
        ; Some custom stack address, just use the 16-bit tiled mappings
        ;
.stack_tiled:
int3                                    ; debug this, shouldn't happen yet.  Bs3EnteredMode_xxx isn't prepared.
        shl     cx, 4
        add     sp, cx
        mov     cx, ss
        jc      .stack_carry
        shr     cx, 12
        jmp     .stack_join_up_again
.stack_carry:
        shr     cx, 12
        inc     cx
.stack_join_up_again:
        shl     cx, 3
        adc     cx, BS3_SEL_TILED
        mov     ss, cx
        jmp     ax

BS3_PROC_END_CMN   Bs3ConvertRMStackToP16UsingCxReturnToAx

