; $Id: WinExit.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; 16-bit windows program that exits windows.
;
; Build: wcl -I%WATCOM%\h\win -l=windows -k4096 -fm WinExit.asm
;

;
; Copyright (C) 2018-2024 Oracle and/or its affiliates.
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



;.stack 4096
STACK   segment para stack 'STACK'
STACK   ends


extrn INITTASK:FAR
extrn INITAPP:FAR
extrn EXITWINDOWS:FAR
extrn WAITEVENT:FAR

_TEXT   segment word public 'CODE'
start:
        push    bp
        mov     bp, sp

        ;
        ; Initialize the windows app.
        ;
        call    INITTASK

        xor     ax, ax
        push    ax
        call    WAITEVENT

        push    di                      ; hInstance
        push    di
        call    INITAPP

        ;
        ; Do what we're here for, exitting windows.
        ;
        xor     ax, ax
        xor     cx, cx
        xor     dx, dx
        push    ax
        push    ax
        push    ax
        push    ax
        call    EXITWINDOWS

        ;
        ; Exit via DOS interrupt.
        ;
        xor     al, al
        mov     ah,04cH
        int     021h

        mov     sp, bp
        pop     bp
        ret

_TEXT   ends

end start

