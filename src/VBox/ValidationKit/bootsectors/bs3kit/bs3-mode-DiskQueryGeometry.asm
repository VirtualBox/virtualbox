; $Id: bs3-mode-DiskQueryGeometry.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3DiskQueryGeometry
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
BS3_EXTERN_DATA16 g_aBs3RmIvtOriginal


;;
; Performs a int 13h function 8 call.
;
; @cproto   BS3_MODE_PROTO_STUB(uint8_t, Bs3DiskQueryGeometry,(uint8_t bDrive, uint16_t *puMaxCylinder,
;                                                              uint8_t *puMaxHead, uint8_t *puMaxSector));
;
; @returns  Register value (ax/eax). Zero on success, non-zero on failure.
;
; @uses     No GPRs (only return full register(s)). In 64-bit mode DS and ES is trashed.
;
; @remarks  ASSUMES we're in ring-0 when not in some kind of real mode.
; @remarks  ASSUMES we're on a 16-bit suitable stack.
;
TMPL_BEGIN_TEXT
BS3_PROC_BEGIN_MODE Bs3DiskQueryGeometry, BS3_PBC_HYBRID
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
%define a_puMaxCylinder         [xBP + xCB + cbCurRetAddr + xCB + sCB*0]
%define a_pMaxHead              [xBP + xCB + cbCurRetAddr + xCB + sCB*1]
%define a_puMaxSector           [xBP + xCB + cbCurRetAddr + xCB + sCB*2]

%ifdef TMPL_64BIT
        mov     a_bDrive, rcx           ; save bDrive
        mov     a_puMaxCylinder, rdx    ; save pcCylinders
        mov     a_pMaxHead, r8          ; save pcHeads
        mov     a_puMaxSector, r9       ; save pcSectors
        movzx   edx, cl                 ; dl = drive
%else
        mov     dl, a_bDrive            ; dl = drive
%endif

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
        ; Ralf Brown suggest setting es:di to NULL before the call.
        ; Doubt this is really needed in our case, but whatever.
        ;
        xor     di, di
        mov     es, di

        ;
        ; Make the call.
        ;
        mov     ax, 0800h                   ; ah=08h
        xor     di, di                      ; ralf brown suggestion to guard against bios bugs.
        mov     es, di
        int     13h

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
        ;
        ; Check that we didn't failed.
        ;
        jc      .failed_return_ah

        ;
        ; Save the return values.
        ;   CL[7:6]:CH = max cylinders.
        ;   DH         = max heads
        ;   CL[5:0]    = max sectors
        ; Other stuff we don't care about:
        ;   AH         = 0
        ;   AL         = undefined/zero
        ;   BL         = drive type
        ;   DL         = max drives.
        ;   ES:DI      = driver parameter table for floppies.
        ;
%ifdef TMPL_16BIT
        les     di, a_pMaxHead
        mov     [es:di], dh

        les     di, a_puMaxSector
        cmp     byte a_bDrive, 4            ; ASSUMES the first 4 drive are potentially BIG floppies.
        jge     .not_floppy_sectors
        mov     [es:di], cl                 ; For 63.5 MB the whole of CL is the sector number.
        mov     cl, 0                       ; CL does not contribute to the cylinder count, so zero it.
        jmp     .set_max_cylinder
.not_floppy_sectors:
        mov     al, 3fh
        and     al, cl
        mov     [es:di], al

.set_max_cylinder:
        les     di, a_puMaxCylinder
        shr     cl, 6
        xchg    cl, ch
        mov     [es:di], cx
%else
        mov     xDI, a_pMaxHead
        mov     [xDI], dh

        mov     xDI, a_puMaxSector
        cmp     byte a_bDrive, 4            ; ASSUMES the first 4 drive are potentially BIG floppies.
        jge     .not_floppy_sectors
        mov     [xDI], cl                   ; For 63.5 MB the whole of CL is the sector number.
        mov     cl, 0                       ; CL does not contribute to the cylinder count, so zero it.
        jmp     .set_max_cylinder
.not_floppy_sectors:
        mov     al, 3fh
        and     al, cl
        mov     [xDI], al

.set_max_cylinder:
        mov     xDI, a_puMaxCylinder
        shr     cl, 6
        xchg    cl, ch
        mov     [xDI], cx
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
BS3_PROC_END_MODE   Bs3DiskQueryGeometry

