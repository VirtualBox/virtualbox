; $Id: bs3-cmn-PrintChr.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3PrintChr.
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
BS3_EXTERN_CMN Bs3Syscall


TMPL_BEGIN_TEXT

;;
; @cproto   BS3_DECL(void) Bs3PrintChr_c16(char ch);
;
BS3_PROC_BEGIN_CMN Bs3PrintChr, BS3_PBC_NEAR
        BS3_CALL_CONV_PROLOG 1
        push    xBP
        mov     xBP, xSP
        push    xAX
        push    xCX
        push    xBX

%if TMPL_BITS == 16
        ; If we're in real mode or v8086 mode, call the VGA BIOS directly.
        mov     bl, [g_bBs3CurrentMode]
        cmp     bl, BS3_MODE_RM
        je      .do_vga_bios_call
 %if 0
        test    bl, BS3_MODE_CODE_V86
        jz      .do_system_call
 %else
        jmp     .do_system_call
 %endif

.do_vga_bios_call:
        mov     al, [xBP + xCB*2]       ; Load the char
        cmp     al, 0ah                 ; \n
        je      .newline
        mov     bx, 0ff00h
        mov     ah, 0eh
        int     10h
        jmp     .return
.newline:
        mov     ax, 0e0dh               ; cmd + '\r'.
        mov     bx, 0ff00h
        int     10h
        mov     ax, 0e0ah               ; cmd + '\n'.
        mov     bx, 0ff00h
        int     10h
        jmp     .return
%endif

.do_system_call:
        mov     cl, [xBP + xCB*2]       ; Load the char
        mov     ax, BS3_SYSCALL_PRINT_CHR
        call    Bs3Syscall              ; near! no BS3_CALL!

.return:
        pop     xBX
        pop     xCX
        pop     xAX
        pop     xBP
        BS3_CALL_CONV_EPILOG 1
        ret
BS3_PROC_END_CMN   Bs3PrintChr

;
; Generate 16-bit far stub.
; Peformance critical, so don't penalize near calls.
;
BS3_CMN_FAR_STUB Bs3PrintChr, 2

