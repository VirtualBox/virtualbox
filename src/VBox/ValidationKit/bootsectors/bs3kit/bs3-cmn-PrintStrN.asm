; $Id: bs3-cmn-PrintStrN.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3PrintStrN.
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
; @cproto   BS3_DECL(void) Bs3PrintStrN_c16(const char BS3_FAR *pszString, size_t cchString);
;
; ASSUMES cchString < 64KB!
;
BS3_PROC_BEGIN_CMN Bs3PrintStrN, BS3_PBC_NEAR
        BS3_CALL_CONV_PROLOG 2
        push    xBP
        mov     xBP, xSP
        push    xAX
        push    xCX
        push    xBX
        push    xSI

%if TMPL_BITS == 16
        ; If we're in real mode or v8086 mode, call the VGA BIOS directly.
        mov     bl, [g_bBs3CurrentMode]
        cmp     bl, BS3_MODE_RM
        je      .do_bios_call
 %if 0
        test    bl, BS3_MODE_CODE_V86
        jz      .do_system_call
 %else
        jmp     .do_system_call
 %endif

        ;
        ; We can do the work right here.
        ;
.do_bios_call:
        push    ds
        lds     si, [xBP + xCB + cbCurRetAddr]          ; DS:SI -> string.
        cld
        mov     cx, [xBP + xCB + cbCurRetAddr + sCB]    ; Use CX for counting down.
        call    Bs3PrintStrN_c16_CX_Bytes_At_DS_SI
        pop     ds
        jmp     .return
%endif


        ;
        ; Need to do system call(s).
        ; String goes into CX:xSI, count into DX.
        ;
        ; We must ensure the string is real-mode addressable first, if not we
        ; must do it char-by-char.
        ;
.do_system_call:
%if TMPL_BITS == 16
        mov     cx, [xBP + xCB + cbCurRetAddr + 2]
%else
        mov     cx, ds
%endif
        mov     xSI, [xBP + xCB + cbCurRetAddr]
        mov     dx,  [xBP + xCB + cbCurRetAddr + sCB]
%if TMPL_BITS == 16

%else
        cmp     xSI, _1M
        jae     .char_by_char
%endif
        mov     ax, BS3_SYSCALL_PRINT_STR
        call    Bs3Syscall              ; near! no BS3_CALL!

.return:
        pop     xSI
        pop     xBX
        pop     xCX
        pop     xAX
        pop     xBP
        BS3_CALL_CONV_EPILOG 2
        BS3_HYBRID_RET

        ;
        ; Doesn't look like it's real-mode addressable.  So, char-by-char.
        ;
.char_by_char:
%if TMPL_BITS == 16
        push    es
        mov     es, cx
%endif
        cld
        test    dx, dx
        jz      .char_by_char_return
.char_by_char_loop:
        mov     ax, BS3_SYSCALL_PRINT_CHR
        mov     cl, [BS3_ONLY_16BIT(es:) xSI]
        call    Bs3Syscall              ; near! no BS3_CALL!
        inc     xSI
        dec     xDX
        jnz     .char_by_char_loop
.char_by_char_return:
%if TMPL_BITS == 16
        pop     es
%endif
        jmp     .return

BS3_PROC_END_CMN   Bs3PrintStrN

%if TMPL_BITS == 16

;
; This code is shared with the system handler.
;
; @param    CX          Number of byte sto print.
; @param    DS:SI       The string to print
; @uses     AX, BX, CX, SI
;
BS3_PROC_BEGIN Bs3PrintStrN_c16_CX_Bytes_At_DS_SI
        CPU 8086
        ; Check if CX is zero first.
        test    cx, cx
        jz      .bios_loop_done

        ; The loop, processing the string char-by-char.
.bios_loop:
        mov     bx, 0ff00h
        lodsb                           ; al = next char
        cmp     al, 0ah                 ; \n
        je      .bios_loop_newline
 %ifdef BS3_STRICT
        test    al, al
        jnz     .not_zero
        hlt
.not_zero:
 %endif
        mov     ah, 0eh
.bios_loop_int10h:
        int     10h
        loop    .bios_loop
.bios_loop_done:
        ret

.bios_loop_newline:
        mov     ax, 0e0dh               ; cmd + '\r'.
        int     10h
        mov     ax, 0e0ah               ; cmd + '\n'.
        mov     bx, 0ff00h
        jmp     .bios_loop_int10h
BS3_PROC_END   Bs3PrintStrN_c16_CX_Bytes_At_DS_SI


;
; Generate 16-bit far stub.
; Peformance critical, so don't penalize near calls.
;
BS3_CMN_FAR_STUB Bs3PrintStrN, 6

%endif ; TMPL_BITS == 16

