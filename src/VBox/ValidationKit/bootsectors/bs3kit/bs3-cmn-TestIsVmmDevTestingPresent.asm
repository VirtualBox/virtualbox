; $Id: bs3-cmn-TestIsVmmDevTestingPresent.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - bs3TestIsVmmDevTestingPresent
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
%include "VBox/VMMDevTesting.mac"

;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
BS3_EXTERN_DATA16 g_uBs3CpuDetected
TMPL_BEGIN_TEXT


;;
; @cproto   BS3_DECL(bool) bs3TestIsVmmDevTestingPresent_c16(void);
;
BS3_PROC_BEGIN_CMN bs3TestIsVmmDevTestingPresent, BS3_PBC_HYBRID_0_ARGS
        BS3_CALL_CONV_PROLOG 2
        push    xBP
        mov     xBP, xSP
        push    xDX

        ; Check the response from the NOP port.
        mov     dx, VMMDEV_TESTING_IOPORT_NOP
        cmp     byte [g_uBs3CpuDetected], BS3CPU_80386
        jb      .ancient_cpu
        in      eax, dx
        cmp     eax, VMMDEV_TESTING_NOP_RET
.set_ax_and_return:
        mov     ax, 0
        jne     .return
        mov     ax, 1

.return:
        pop     xDX
        pop     xBP
        BS3_CALL_CONV_EPILOG 2
        BS3_HYBRID_RET

.ancient_cpu:
        in      ax, dx
        cmp     ax, (VMMDEV_TESTING_NOP_RET & 0xffff)
        jmp     .set_ax_and_return
BS3_PROC_END_CMN   bs3TestIsVmmDevTestingPresent

