; $Id: bs3-cmn-TestNow.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3TestNow.
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

BS3_EXTERN_DATA16 g_fbBs3VMMDevTesting
TMPL_BEGIN_TEXT

;;
; @cproto   BS3_DECL(uint64_t) Bs3TestNow(void);
;
; @uses     eflags, return register(s)
;
BS3_PROC_BEGIN_CMN Bs3TestNow, BS3_PBC_HYBRID
        BS3_CALL_CONV_PROLOG 0
        push    xBP
        mov     xBP, xSP
%if __BITS__ == 16
BONLY16 push    sAX
%else
        push    xCX
BONLY64 push    xDX
%endif

        cmp     byte [BS3_DATA16_WRT(g_fbBs3VMMDevTesting)], 0
        je      .no_vmmdev

        ; Read the lower timestamp.
        mov     dx, VMMDEV_TESTING_IOPORT_TS_LOW
        in      eax, dx
%if __BITS__ == 16
        mov     bx, ax                  ; Save the first word in BX (returned in DX).
        shr     eax, 16
        mov     cx, ax                  ; The second word is returned in CX.
%else
        mov     ecx, eax
%endif

        ; Read the high timestamp (latached in above read).
        mov     dx, VMMDEV_TESTING_IOPORT_TS_HIGH
        in      eax, dx
%if __BITS__ == 16
        mov     dx, bx                  ; The first word is returned in DX.
        mov     bx, ax                  ; The third word is returned in BX.
        shr     eax, 16                 ; The fourth word is returned in AX.
%elif __BITS__ == 32
        mov     edx, eax
        mov     eax, ecx
%else
        shl     rax, 32
        or      rax, rcx
%endif

.return:
%if __BITS__ == 16
        mov     [bp - sCB], ax          ; Update the AX part of the saved EAX.
        pop     sAX
%else
        pop     xCX
BONLY64 pop     xDX
%endif
        pop     xBP
        BS3_CALL_CONV_EPILOG 0
        BS3_HYBRID_RET

.no_vmmdev:
        ; No fallback, just zero the result.
%if __BITS__ == 16
        xor     ax, ax
        xor     bx, bx
        xor     cx, cx
        xor     dx, dx
%else
        xor     eax, eax
BONLY32 xor     edx, edx
%endif
        jmp     .return
BS3_PROC_END_CMN   Bs3TestNow

