; $Id: bs3-mode-DiskRead.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3DiskRead
;

;
; Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
;*  External symbols                                                                                                             *
;*********************************************************************************************************************************
TMPL_BEGIN_TEXT
extern TMPL_NM(Bs3SwitchToRM)
BS3_BEGIN_TEXT16
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_rm)

BS3_EXTERN_DATA16   g_aBs3RmIvtOriginal
%ifdef TMPL_16BIT
BS3_EXTERN_CMN      Bs3SelProtFar16DataToRealMode
%else
BS3_EXTERN_CMN      Bs3SelFlatDataToRealMode
%endif



;;
; Performs a int 13h function 2 call.
;
; @cproto   BS3_MODE_PROTO_STUB(uint8_t, Bs3DiskRead,(uint8_t bDrive, uint16_t uCylinder, uint8_t uHead, uint8_t uSector,
;                                                     uint8_t cSectors, void RT_FAR *pvBuf));
;
; @returns  Register value (ax/eax). Zero on success, non-zero on failure.
;
; @uses     No GPRs (only return full register(s)). In 64-bit mode DS and ES is trashed.
;
; @remarks  ASSUMES we're in ring-0 when not in some kind of real mode.
; @remarks  ASSUMES we're on a 16-bit suitable stack.
;
TMPL_BEGIN_TEXT
BS3_PROC_BEGIN_MODE Bs3DiskRead, BS3_PBC_HYBRID
        push    xBP
        mov     xBP, xSP
;; @todo does not work on 286 and earlier!
        sPUSHF
        cli
        push    sBX
        push    sCX
        push    sDX
        push    sSI
        push    sDI
%ifndef TMPL_64BIT
        push    ds
        push    es
%endif

        ; Load/Save parameters.
%define a_bDrive                [xBP + xCB + cbCurRetAddr + xCB*0]
%define a_uCylinder             [xBP + xCB + cbCurRetAddr + xCB*1]
%define a_uCylinderHi           [xBP + xCB + cbCurRetAddr + xCB*1 + 1]
%define a_uHead                 [xBP + xCB + cbCurRetAddr + xCB*2]
%define a_uSector               [xBP + xCB + cbCurRetAddr + xCB*3]
%define a_cSectors              [xBP + xCB + cbCurRetAddr + xCB*4]
%define a_pvBuf                 [xBP + xCB + cbCurRetAddr + xCB*5]
%define a_pvBufSel              [xBP + xCB + cbCurRetAddr + xCB*5 + 2]

%ifdef TMPL_64BIT
        mov     a_bDrive, rcx           ; save bDrive
        mov     a_uCylinder, rdx        ; save uCylinder
        mov     a_uHead, r8             ; save uHead
        mov     a_uSector, r9           ; save uSector
        movzx   edx, cl                 ; dl = drive
%else
        mov     dl, a_bDrive            ; dl = drive
%endif

        ;
        ; Convert buffer pointer if not in real mode.
        ; Note! We clean the stack up in the epilogue.
        ;
%ifdef TMPL_64BIT
        sub     rsp, 20h
        mov     rcx, a_pvBuf
        call    Bs3SelFlatDataToRealMode
        mov     a_pvBuf, rax
%elifdef TMPL_32BIT
        mov     ecx, a_pvBuf
        push    ecx
        call    Bs3SelFlatDataToRealMode
        mov     a_pvBuf, eax
%elif !BS3_MODE_IS_RM_OR_V86(TMPL_MODE)
        push    word a_pvBufSel
        push    word a_pvBuf
        call    Bs3SelProtFar16DataToRealMode
        mov     a_pvBuf, ax
        mov     a_pvBufSel, dx
%endif

        ;
        ; Set up the BIOS call parameters.
        ;
        mov     ah, 02h                     ; read
        mov     al, a_cSectors
        mov     dh, a_uHead
        mov     dl, a_bDrive
        ; For 63.5MB support the entire CL is the sector number, so just treat floppies special.
        cmp     dl, 4                       ; ASSUMES the first 4 drive are potentially BIG floppies.
        jb      .floppy
        mov     cx, a_uCylinder
        xchg    ch, cl
        shl     cl, 6
        or      cl, a_uSector
        jmp     .setup_addr
.floppy:
        mov     ch, a_uCylinder
        mov     cl, a_uSector
.setup_addr:
        mov     bx, a_pvBuf
        mov     di, a_pvBufSel

        ;
        ; Switch to real mode, first we just to the 16-bit text segment.
        ; This preserve all 32-bit register values.
        ;
%if TMPL_MODE != BS3_MODE_RM
 %ifndef TMPL_16BIT
        jmp     .to_text16
BS3_BEGIN_TEXT16
.to_text16:
        BS3_SET_BITS TMPL_BITS
 %endif
        call    TMPL_NM(Bs3SwitchToRM)
        BS3_SET_BITS 16
%endif

        ;
        ; Make the call.
        ;
        mov     es, di
;        pushf
;        sti
        int     13h
;        popf

        ;
        ; Switch back.
        ;
%if TMPL_MODE != BS3_MODE_RM
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_rm)
        BS3_SET_BITS TMPL_BITS
 %ifndef TMPL_16BIT
        jmp     .from_text16
TMPL_BEGIN_TEXT
.from_text16:
 %endif
%endif
hlt

        ;
        ; Check that we didn't failed.
        ;
        jc      .failed_return_ah
        test    ah, ah
        jnz     .failed_return_ah
%ifdef BS3_STRICT
        cmp      al, a_cSectors
        je      .next
        int3
.next:
%endif
        ;
        ; Return success
        ;
        mov     al, 0

.return:
%ifndef TMPL_64BIT
        lea     xSP, [xBP - sCB * 6 - xCB*2]
        pop     es
        pop     ds
%else
        lea     xSP, [xBP - sCB * 6]
%endif
        pop     sDI
        pop     sSI
        pop     sDX
        pop     sCX
        pop     sBX
        sPOPF
        leave
        BS3_HYBRID_RET

        ;
        ; Failed.
        ;
.failed_return_ah:
        mov     al, ah
        cmp     al, 0
        jne     .return
        dec     al
        jmp     .return
BS3_PROC_END_MODE   Bs3DiskRead

