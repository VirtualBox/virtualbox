; $Id: bs3-mode-BiosInt15hE820.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; BS3Kit - Bs3BiosInt15hE820
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
;*  Defined Constants And Macros                                                                                                 *
;*********************************************************************************************************************************
;; Signature: 'SMAP'
%define INT15_E820_SIGNATURE    0534d4150h


;*********************************************************************************************************************************
;*  External symbols                                                                                                             *
;*********************************************************************************************************************************
TMPL_BEGIN_TEXT
extern TMPL_NM(Bs3SwitchToRM)
BS3_BEGIN_TEXT16
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_Safe_rm)


;;
; Performs a int 15h function 0xe820 call.
;
; @cproto   BS3_MODE_PROTO_STUB(bool, Bs3BiosInt15hE820,(INT15E820ENTRY BS3_FAR *pEntry, uint32_t BS3_FAR *pcbEntry,
;                                                        uint32_t BS3_FAR *puContinuationValue));
;
; @returns Success indicator.
; @param   pEntry              The return buffer.
; @param   pcbEntry            Input: The size of the buffer (min 20 bytes);
;                              Output: The size of the returned data.
; @param   puContinuationValue Where to get and return the continuation value (EBX)
;                              Set to zero the for the first call.  Returned as zero
;                              after the last entry.
;
; @remarks  ASSUMES we're in ring-0 when not in some kind of real mode.
; @remarks  ASSUMES we're on a 16-bit suitable stack.
;
; @uses     rax
;
TMPL_BEGIN_TEXT
BS3_PROC_BEGIN_MODE Bs3BiosInt15hE820, BS3_PBC_HYBRID
        push    xBP
        mov     xBP, xSP
        sPUSHF
        cli
        push    sBX
        push    sCX
        push    sDX
        push    sSI
        push    sDI
%ifdef TMPL_16BIT
        push    ds
        push    es
%endif
        ; Load/Save parameters.
%define a_pEntry              [xBP + xCB + cbCurRetAddr + sCB*0]
%define a_pcbEntry            [xBP + xCB + cbCurRetAddr + sCB*1]
%define a_puContinuationValue [xBP + xCB + cbCurRetAddr + sCB*2]
%ifdef TMPL_64BIT
        mov     a_pEntry, rcx           ; save pEntry
        mov     a_pcbEntry, rdx         ; save pcbEntry
        mov     a_puContinuationValue, r8  ; save a_puContinuationValue
        mov     ebx, [r8]               ; uContinuationValue for int15
        mov     ecx, [rdx]              ; Buffer size for int15.
%elifdef TMPL_16BIT
        les     bx, a_pcbEntry
        mov     ecx, [es:bx]            ; Buffer size for int15.
        les     bx, a_puContinuationValue
        mov     ebx, [es:bx]            ; Buffer size for int15.
%else
        mov     ecx, a_pcbEntry
        mov     ecx, [ecx]              ; Buffer size for int15.
        mov     ebx, a_puContinuationValue
        mov     ebx, [ebx]              ; uContinuationValue for int15
%endif
        ;
        ; Check that the cbEntry isn't too big or too small before doing
        ; the stack allocation.  (Our BIOS doesn't check if too small.)
        ;
        cmp     ecx, 100h
        jae     .failed
        cmp     cl, 14h
        jb      .failed

%if TMPL_MODE != BS3_MODE_RM
        sub     xSP, xCX                ; allocate a temporary buffer on the stack.
        and     xSP, ~0fh
%endif

        ;
        ; Switch to real mode, first we just ot the 16-bit text segment.
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
%if TMPL_MODE == BS3_MODE_RM
        les     di, a_pEntry
%else
        push    ss                      ; es:di -> ss:sp
        pop     es
        mov     di, sp
%endif
        mov     edx, INT15_E820_SIGNATURE
        mov     eax, 0e820h             ; BIOS function number
        int     15h

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
        jc      .failed
        cmp     eax, INT15_E820_SIGNATURE
        jne     .failed
        cmp     ecx, 20
        jb      .failed

        ;
        ; Save the continuation value.
        ;
%ifdef TMPL_16BIT
        mov     eax, ebx
        lds     bx, a_puContinuationValue
        mov     [bx], eax
%else
        mov     xAX, a_puContinuationValue
        mov     [xAX], ebx
%endif

        ;
        ; Save the entry size.
        ;
%ifdef TMPL_16BIT
        lds     bx, a_pcbEntry
%else
        mov     xBX, a_pcbEntry
%endif
        mov     [xBX], ecx

%if TMPL_MODE != BS3_MODE_RM
        ;
        ; Copy the returned stuff into the caller's buffer.
        ;
        mov     xSI, xSP
 %ifdef TMPL_16BIT
        push    ss
        pop     es
        lds     di, a_pEntry
 %else
        mov     xDI, a_pEntry
 %endif
        cld
        rep movsb
%endif

        ;
        ; Return success
        ;
        mov     al, 1

.return:
%ifdef TMPL_16BIT
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
.failed:
        xor     al, al
        jmp     .return
BS3_PROC_END_MODE   Bs3BiosInt15hE820

