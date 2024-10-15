; $Id: bs3-cmn-SwitchHlpConvFlatRetToRetfProtMode.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - SwitchHlpConvFlatRetToRetfProtMode
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
BS3_EXTERN_CMN Bs3SelFlatCodeToProtFar16

;;
; SwitchToXxx helper that converts a 32-bit or 64-bit flat return address
; into a 16-bit protected mode far return.
;
;
; The caller calls this routine before switching modes. The flat return
; to be converted is immediately after our own return address on the stack.
;
; @uses     Nothing.
; @remarks  No 16-bit version.
;
BS3_PROC_BEGIN_CMN Bs3SwitchHlpConvFlatRetToRetfProtMode, BS3_PBC_NEAR
 %if TMPL_BITS == 64
        push        xAX
        push        xCX
        sub         xSP, 20h

        mov         xCX, [xSP + xCB*3 + 20h]
        call        Bs3SelFlatCodeToProtFar16 ; well behaved assembly function, only clobbers ecx
        mov         [xSP + xCB*3 + 20h + 4], eax

        add         xSP, 20h
        pop         xCX
        pop         xAX
        ret         4
 %else
        xchg        eax, [xSP + xCB]
        push        xAX
        call        Bs3SelFlatCodeToProtFar16 ; well behaved assembly function, only clobbers eax
        add         xSP, 4
        xchg        [xSP + xCB], eax
        ret
 %endif
BS3_PROC_END_CMN   Bs3SwitchHlpConvFlatRetToRetfProtMode

%endif ; 32 || 64

