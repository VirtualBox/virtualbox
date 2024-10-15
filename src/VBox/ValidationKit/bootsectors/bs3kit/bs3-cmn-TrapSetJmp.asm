; $Id: bs3-cmn-TrapSetJmp.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3TrapSetJmp.
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
BS3_EXTERN_CMN Bs3RegCtxSave
%if TMPL_BITS == 16
BS3_EXTERN_CMN_FAR Bs3SelFar32ToFlat32
%endif
BS3_EXTERN_DATA16 g_Bs3TrapSetJmpCtx
BS3_EXTERN_DATA16 g_pBs3TrapSetJmpFrame
TMPL_BEGIN_TEXT


;;
; Sets up a one-shot set-jmp-on-trap.
;
; @uses     See, applicable C calling convention.
;
BS3_PROC_BEGIN_CMN Bs3TrapSetJmp, BS3_PBC_HYBRID
        BS3_CALL_CONV_PROLOG 1
        push    xBP
        mov     xBP, xSP
        push    xBX
BONLY64 sub     xSP, 20h

        ;
        ; Save the current register context.
        ;
BONLY16 push    ds
        BS3_LEA_MOV_WRT_RIP(xAX, BS3_DATA16_WRT(g_Bs3TrapSetJmpCtx))
        push    xAX
        BS3_CALL Bs3RegCtxSave, 1
        add     xSP, sCB

        ;
        ; Adjust the return context a little.
        ;
        BS3_LEA_MOV_WRT_RIP(xBX, BS3_DATA16_WRT(g_Bs3TrapSetJmpCtx))
        mov     xAX, [xBP + xCB]        ; The return address of this function
        mov     [xBX + BS3REGCTX.rip], xAX
%if TMPL_BITS == 16
        mov     xAX, [xBP + xCB+2]      ; The return address CS of this function.
        mov     [xBX + BS3REGCTX.cs], xAX
%endif
        mov     xAX, [xBP]
        mov     [xBX + BS3REGCTX.rbp], xAX
        lea     xAX, [xBP + xCB + cbCurRetAddr]
        mov     [xBX + BS3REGCTX.rsp], xAX
        mov     xAX, [xBP - xCB]
        mov     [xBX + BS3REGCTX.rbx], xAX
        xor     xAX, xAX
        mov     [xBX + BS3REGCTX.rax], xAX ; the return value.

        ;
        ; Fill the trap frame return structure.
        ;
        push    xDI
%if TMPL_BITS == 16
        push    es
        les     di, [xBP + xCB + cbCurRetAddr]
        mov     cx, BS3TRAPFRAME_size / 2
        mov     ax, 0faceh
        rep stosw
        pop     es
%else
        mov     xDI, [xBP + xCB*2]
        mov     ecx, BS3TRAPFRAME_size / 4
        mov     xAX, 0feedfaceh
        rep stosd
%endif
        pop     xDI

        ;
        ; Save the (flat) pointer to the trap frame return structure.
        ;
%if TMPL_BITS == 16
        xor     ax, ax
        push    word [xBP + xCB + cbCurRetAddr + 2]
        push    ax
        push    word [xBP + xCB + cbCurRetAddr]
        push    cs
        call    Bs3SelFar32ToFlat32
        add     sp, 6h
        mov     [BS3_DATA16_WRT(g_pBs3TrapSetJmpFrame)], ax
        mov     [2 + BS3_DATA16_WRT(g_pBs3TrapSetJmpFrame)], dx
%else
        mov     xAX, [xBP + xCB*2]
        mov     [BS3_DATA16_WRT(g_pBs3TrapSetJmpFrame)], eax
%endif

        ;
        ; Return 'true'.
        ;
        mov     xAX, 1
BONLY64 add     xSP, 20h
        pop     xBX
        pop     xBP
        BS3_CALL_CONV_EPILOG 1
        BS3_HYBRID_RET
BS3_PROC_END_CMN   Bs3TrapSetJmp

