; $Id: bs3-timers-1-asm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - bs3-timers-1
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
%include "bs3kit.mac"


BS3_BEGIN_X0TEXT16

BS3_PROC_BEGIN NAME(bs3Timers1StiHltCli)
        ;
        ; Assuming interrupts are disabled on entry, the STI instruction should
        ; inhibit interrupts till after HLT, but they should be delivered before
        ; CLI is executed and takes effect.
        ;
        sti
        hlt
        ; Interrupts should be delivered here.
BS3_GLOBAL_NAME_EX NAME(bs3Timers1StiHltCli_IrqPc), function, 1
        cli

        ret
BS3_PROC_END   NAME(bs3Timers1StiHltCli)


BS3_PROC_BEGIN NAME(bs3Timers1StiStiCli)
        ;
        ; Assuming interrupts are disabled on entry, the first STI instruction
        ; should inhibit interrupts, whereas the next should not since interrupts
        ; are enabled now, so interrupts should be delivered after the 2nd STI and
        ; before the CLI is executed and takes effect.
        ;
        sti
        sti
        ; Interrupts should be delivered here.
BS3_GLOBAL_NAME_EX NAME(bs3Timers1StiStiCli_IrqPc), function, 1
        cli

        ret
BS3_PROC_END NAME(bs3Timers1StiStiCli)


BS3_PROC_BEGIN NAME(bs3Timers1PopfCli)
        ;
        ; Assuming interrupts are disabled on entry, the POPF instruction should
        ; enable interrupts with immediate effect, so they should be delivered
        ; before the CLI is executed and takes effect.
        ;
        push    xAX

        xPUSHF
        pop     xAX
        or      xAX, X86_EFL_IF
        push    xAX
        xPOPF
        ; Interrupts should be delivered here.
BS3_GLOBAL_NAME_EX NAME(bs3Timers1PopfCli_IrqPc), function, 1
        cli

        pop     xAX
        ret
BS3_PROC_END    NAME(bs3Timers1PopfCli)


BS3_PROC_BEGIN  NAME(bs3Timers1StiCli)
        ;
        ; Assuming interrupts are disabled on entry, the STI instruction should
        ; inhibit interrupts for one instruction, when that instruction is CLI
        ; no interrupt can be delivered.
        ;
        ; This is a negative test.
        ;
        sti
        cli

        ret
BS3_PROC_END    NAME(bs3Timers1StiCli)


BS3_PROC_BEGIN  NAME(bs3Timers1StiPopf)
        ;
        ; Assuming interrupts are disabled on entry, the STI instruction should
        ; inhibit interrupts for one instruction, when that instruction is a
        ; POPF loading EFLAGS with IF=0 no interrupt can be delivered.
        ;
        ; This is a negative test.
        ;
        push    xAX

        pushf                               ; save eflags with IF=0
        sti                                 ; set IF=1
        popf                                ; load eflags setting IF=0

        pop     xAX
        ret
BS3_PROC_END    NAME(bs3Timers1StiPopf)

