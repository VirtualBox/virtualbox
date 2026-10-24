; $Id: bs3-cpu-instr-3-asm.asm 112403 2026-01-11 19:29:08Z knut.osmundsen@oracle.com $
;; @file
; BS3Kit - bs3-cpu-instr-3
;

;
; Copyright (C) 2007-2026 Oracle and/or its affiliates.
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


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BS3_BEGIN_DATA16 16
;; Memory operand of the RIP relative tests (RIPREL, 64-bit code only): guard
; bytes, the operand (up to 32 bytes) at BS3CPUINSTR3_RIPREL_OFF and more guard
; bytes.  See bs3CpuInstr3_WorkerTestType1.
%define BS3CPUINSTR3_RIPREL_OFF 32
BS3_GLOBAL_DATA g_abBs3CpuInstr3RipRelBuf, 96
        times 96 db 0


;
; Instantiate code templates.
;
BS3_INSTANTIATE_COMMON_TEMPLATE          "bs3-cpu-instr-3-template.mac"
BS3_INSTANTIATE_TEMPLATE_WITH_WEIRD_ONES "bs3-cpu-instr-3-template.mac"

