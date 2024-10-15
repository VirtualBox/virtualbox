; $Id: bs3-cmn-RegGetXcr0.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3RegGetXcr0
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


;;
; @cproto   BS3_CMN_PROTO_STUB(uint64_t, Bs3RegGetXcr0,(void));
;
; @returns  Register value.
; @remarks  Does not require 20h of parameter scratch space in 64-bit mode.
;
; @uses     No GPRs, though 16-bit mode the upper 48-bits of RAX, RDX and RCX are cleared.
;
BS3_PROC_BEGIN_CMN Bs3RegGetXcr0, BS3_PBC_HYBRID_SAFE
        push    xBP
        mov     xBP, xSP
TONLY64 push    rdx

        ; Read the value.
TNOT16  push    sCX
        xor     ecx, ecx
        xgetbv
TNOT16  pop     sCX

        ; Move the edx:eax value into the appropriate return register(s).
%if TMPL_BITS == 16
        ; value [dx cx bx ax]
        ror     eax, 16
        mov     bx, ax
        mov     cx, dx
        shr     eax, 16
        shr     edx, 16
%elif TMPL_BITS == 64
        mov     eax, eax
        shr     rdx, 32
        or      rax, rdx
%endif

TONLY64 pop     rdx
        pop     xBP
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3RegGetXcr0

