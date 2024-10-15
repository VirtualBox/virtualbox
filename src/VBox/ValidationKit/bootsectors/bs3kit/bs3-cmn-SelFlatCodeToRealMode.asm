; $Id: bs3-cmn-SelFlatCodeToRealMode.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3SelFlatCodeToRealMode.
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
;*      Header Files                                                                                                             *
;*********************************************************************************************************************************
%include "bs3kit-template-header.mac"


;*********************************************************************************************************************************
;*      Global Variables                                                                                                         *
;*********************************************************************************************************************************
BS3_EXTERN_DATA16 Bs3RmText16_EndOfSegment
BS3_EXTERN_DATA16 Bs3X0Text16_EndOfSegment
BS3_EXTERN_DATA16 Bs3X1Text16_EndOfSegment


;
; Make sure we can get at all the segments.
;
BS3_BEGIN_TEXT16
BS3_BEGIN_RMTEXT16
BS3_BEGIN_X0TEXT16
BS3_BEGIN_X1TEXT16
TMPL_BEGIN_TEXT


;;
; @cproto   BS3_CMN_PROTO(uint32_t, Bs3SelRealModeCodeToProtMode,(uint32_t uFlatAddr), false);
;
; @uses     Only return registers (ax:dx, eax, eax)
;
BS3_PROC_BEGIN_CMN Bs3SelFlatCodeToRealMode, BS3_PBC_NEAR
        BS3_CALL_CONV_PROLOG 1
        push    xBP
        mov     xBP, xSP
        push    xCX
        push    xBX
%if TMPL_BITS != 16
        push    xDX
%endif

        ;
        ; Load the real mode frame number into DX so we can compare with the
        ; segment frame numbers fixed up by the linker.
        ;
        ; Imagine: FlatAddr = 0x054321
        ;
        mov     dx, [xBP + xCB + cbCurRetAddr + 1]      ; dx = 0x0543
        mov     al, [xBP + xCB + cbCurRetAddr + 0]      ; al =     0x21
        mov     cl,4
        shl     dx, 4                                   ; dx =  0x5430
        shr     al, 4                                   ; al =    0x02
        or      dl, al                                  ; dx =  0x5432

        mov     ax, dx
        sub     ax, CGROUP16
        cmp     ax, 1000h
        jb      .bs3text16

        mov     ax, dx
        sub     ax, BS3GROUPRMTEXT16
        mov     bx, Bs3RmText16_EndOfSegment wrt BS3GROUPRMTEXT16
        add     bx, 15
        shr     bx, cl
        cmp     ax, bx
        jb      .bs3rmtext16

        mov     ax, dx
        sub     ax, BS3GROUPX0TEXT16
        mov     bx, Bs3X0Text16_EndOfSegment wrt BS3GROUPX0TEXT16
        add     bx, 15
        shr     bx, cl
        cmp     ax, bx
        jb      .bs3x0text16

        mov     ax, dx
        sub     ax, BS3GROUPX1TEXT16
        mov     bx, Bs3X1Text16_EndOfSegment wrt BS3GROUPX1TEXT16
        add     bx, 15
        shr     bx, cl
        cmp     ax, bx
        jb      .bs3x1text16

        extern  BS3_CMN_NM(Bs3Panic)
        call    BS3_CMN_NM(Bs3Panic)

        ;
        ; Load the real-mode frame into DX and calc the offset in AX.
        ;
.bs3x1text16:
        mov     dx, BS3GROUPX1TEXT16
        jmp     .calc_return
.bs3x0text16:
        mov     dx, BS3GROUPX0TEXT16
        jmp     .calc_return
.bs3rmtext16:
        mov     dx, BS3GROUPRMTEXT16
        jmp     .calc_return
.bs3text16:
        mov     dx, CGROUP16
.calc_return:
        ; Convert the real-mode frame into the low 16-bit base (BX).
        mov     bx, dx
        shl     bx, cl
        ; Subtract the 16-bit base from the flat address. (No need to consider
        ; the top half on either side.)
        mov     ax, [xBP + xCB + cbCurRetAddr + 0]
        sub     ax, bx
%if TMPL_BITS != 16
        ; Got a single 32-bit return register here.
        shl     edx, 16
        mov     dx, ax
        mov     eax, edx
        pop     xDX
%endif
        pop     xBX
        pop     xCX
        pop     xBP
        BS3_CALL_CONV_EPILOG 1
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3SelFlatCodeToRealMode

;
; We may be using the near code in some critical code paths, so don't
; penalize it.
;
BS3_CMN_FAR_STUB   Bs3SelFlatCodeToRealMode, 4

