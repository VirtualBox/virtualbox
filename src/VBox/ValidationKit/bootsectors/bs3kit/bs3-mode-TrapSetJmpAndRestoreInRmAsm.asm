; $Id: bs3-mode-TrapSetJmpAndRestoreInRmAsm.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3TrapSetJmpAndRestoreInRm helper
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
BS3_BEGIN_TEXT16
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_rm)
extern _Bs3TrapSetJmpAndRestore_c16

TMPL_BEGIN_TEXT
extern TMPL_NM(Bs3SwitchToRM)


;;
; Shared prologue code.
; @param    xAX     Where to jump to for the main event.
;
BS3_PROC_BEGIN_MODE Bs3TrapSetJmpAndRestoreInRmAsm, BS3_PBC_NEAR
        BS3_CALL_CONV_PROLOG 2
        push    xBP
        mov     xBP, xSP
        xPUSHF

        ;
        ; Save non-volatile registers so the DO function doesn't have to.
        ;
        push    xBX
        push    xCX
        push    xDX
        push    xSI
        push    xDI
%if TMPL_BITS != 64
        push    ds
        push    es
        push    ss
 %if TMPL_BITS != 16
        push    fs
        push    gs
 %endif
%endif
%if TMPL_BITS == 64
        push    r8
        push    r9
        push    r10
        push    r11
        push    r12
        push    r13
        push    r14
        push    r15
%endif

        ;
        ; Load EAX and EDX with the two pointers.
        ;
        mov     eax, [xBP + xCB + cbCurRetAddr]
        mov     edx, [xBP + xCB + cbCurRetAddr + sCB]

        ;
        ; Jump to 16-bit segment for the mode switching.
        ;
%if TMPL_BITS != 16
        jmp     .in_16bit_segment
BS3_BEGIN_TEXT16
        BS3_SET_BITS TMPL_BITS
.in_16bit_segment:
%endif

        ;
        ; Switch to real-mode.
        ;
        call    TMPL_NM(Bs3SwitchToRM)
        BS3_SET_BITS 16

        ;
        ; Now we do the
        ;
        push    edx
        push    eax
        call    _Bs3TrapSetJmpAndRestore_c16
        add     sp, 8h

        ;
        ; Switch back to the original mode.
        ;
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_rm)
        BS3_SET_BITS TMPL_BITS

        ;
        ; Jump back to the 32-bit or 64-bit segment.
        ;
%if TMPL_BITS != 16
        jmp     .in_text_segment
TMPL_BEGIN_TEXT
.in_text_segment:
%endif

        ;
        ; Restore registers.
        ;
%if TMPL_BITS == 16
        sub     bp, (1+5+3)*2
        mov     sp, bp
%elif TMPL_BITS == 32
        lea     xSP, [xBP - (1+5+5)*4]
%else
        lea     xSP, [xBP - (1+5+8)*8]
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     r11
        pop     r10
        pop     r9
        pop     r8
%endif
%if TMPL_BITS != 64
 %if TMPL_BITS != 16
        pop     gs
        pop     fs
 %endif
        pop     ss
        pop     es
        pop     ds
%endif
        pop     xDI
        pop     xSI
        pop     xDX
        pop     xCX
        pop     xBX
        xPOPF
        pop     xBP
        ret
BS3_PROC_END_MODE   Bs3TrapSetJmpAndRestoreInRmAsm

