; $Id: bs3-cmn-SwitchToRing1.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SwitchToRing1
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
BS3_EXTERN_CMN_FAR Bs3SwitchToRingX
TMPL_BEGIN_TEXT


;;
; @cproto   BS3_DECL(void) Bs3SwitchToRing1(void);
;
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
; @uses     No GPRs.
;
BS3_PROC_BEGIN_CMN Bs3SwitchToRing1, BS3_PBC_HYBRID_0_ARGS
%if TMPL_BITS == 64
        push    rcx
        sub     rsp, 20h
        mov     ecx, 1
        mov     [rsp], rcx
        call    Bs3SwitchToRingX
        add     rsp, 20h
        pop     rcx
%else
        push    1
TONLY16 push    cs
        call    Bs3SwitchToRingX
        add     xSP, xCB
%endif
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3SwitchToRing1

