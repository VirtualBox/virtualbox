; $Id: bs3-cmn-Syscall.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3Syscall.
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


;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "bs3kit-template-header.mac"


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
%if TMPL_BITS == 16
BS3_EXTERN_DATA16 g_bBs3CurrentMode
%endif
BS3_EXTERN_DATA16 g_uBs3TrapEipHint
TMPL_BEGIN_TEXT


;;
; Worker for doing a syscall - Assembly only.
;
; This worker deals with the needing to use a different opcode
; sequence in v8086 mode as well as the high EIP word hint for
; the weird PE16_32, PP16_32 and PAE16_32 modes.
;
; @uses     Whatever the syscall modified (xBX and XBP are always saved).
;
BS3_PROC_BEGIN_CMN Bs3Syscall, BS3_PBC_HYBRID_0_ARGS ; (all parameters are in registers)
        push    xBP
        mov     xBP, xSP
        push    xBX

%if TMPL_BITS == 32
        mov     ebx, .return
        xchg    ebx, [BS3_DATA16_WRT(g_uBs3TrapEipHint)]
%elif TMPL_BITS == 16
        test    byte [BS3_DATA16_WRT(g_bBs3CurrentMode)], BS3_MODE_CODE_V86
        mov     bx, 0
        xchg    bx, [2 + BS3_DATA16_WRT(g_uBs3TrapEipHint)]
        jz      .normal

        db      0xf0                    ; Lock prefix for causing #UD in V8086 mode.
%endif
.normal:
        int     BS3_TRAP_SYSCALL

.return:
        ; Restore the EIP hint so the testcase code doesn't need to set it all the time.
%if TMPL_BITS == 32
        mov     [BS3_DATA16_WRT(g_uBs3TrapEipHint)], ebx
%elif TMPL_BITS == 16
        mov     [2 + BS3_DATA16_WRT(g_uBs3TrapEipHint)], bx
%endif

        pop     xBX
        pop     xBP
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3Syscall

